// ---------------------------------------------------------------------
// RSDKv4 Dreamcast (KallistiOS) — VMU save backend.
//
// gamePath is "/cd/" on the DC (the disc — read-only), so the engine's normal
// file saves silently fail. This redirects the four save entry points to the
// VMU (memory card).
//
// Two files, mirroring the engine's own SData/UData split so a userdata write
// can never clobber game progress and vice-versa:
//   SONIC2.VMS  : saveRAM[]  (game progress; trailing-zero-trimmed)
//   SONIC2R.VMS : achievement statuses + leaderboard scores
// Each is wrapped in a proper VMU package (header + 32x32 icon) so it appears
// in the BIOS file manager. The first attached memory card is used.
//
// Writes are DEFERRED — the store entry points only raise a flag and the real
// VMU I/O happens in DC_FlushSaves(), at moments where a ~300ms pause is free.
// See the "deferred writing" block at the bottom of this file.
//
// Payload layout (little-endian ints; SH4 is little-endian):
//   SAV: [magic][ver=1][used]                          then used ints of saveRAM
//   USR: [magic][ver=1][achCount][lbCount]             then ach ints, then lb ints
// ---------------------------------------------------------------------
#include "../RetroEngine.hpp"

#if RETRO_USING_KOS

#include "DCSave.hpp"
#include "DCSaveIcon.h"

#include <kos.h>
#include <dc/vmu_pkg.h>
#include <dc/maple.h>
#include <dc/maple/vmu.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// The card stores whatever 12-character name it is handed; ".VMS" is purely a
// PC-side convention, but it is the one every memory-card tool is built around,
// and most of them take the on-card name from the file being imported. Naming
// the primary files .VMS means a save pulled off a card is already a usable
// .VMS, and one built on a PC drops back on without being renamed first.
//
// Entry [0] is the name written. The rest are names a save may still be sitting
// under and are tried on read: the pre-.VMS naming this port shipped with, and
// the extensionless form left behind by tools that strip the suffix on import.
// Guessing at names is safe because every payload starts with its own magic —
// opening the wrong file fails the magic check and falls through to the next
// candidate rather than loading garbage.
static const char *const DC_SAV_NAMES[] = { "SONIC2.VMS", "SONIC2.SAV", "SONIC2" };
static const char *const DC_USR_NAMES[] = { "SONIC2R.VMS", "SONIC2.USR", "SONIC2R" };
#define DC_NAMECOUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

// Set by dcVmuRead when a payload was found under one of the non-primary names.
// The next successful write to the primary name removes exactly that file and
// nothing else. Deliberately not "unlink every alternative name": those names
// are guesses, and "SONIC2" in particular could plausibly belong to some other
// homebrew. A name only lands here after we opened it and found OUR magic in
// it, so the file being removed is provably ours.
static const char *dcSavSupersede = NULL;
static const char *dcUsrSupersede = NULL;

// The write gate. dcVmuWrite unlinks the primary file before writing it again,
// so it may only do that to a file this boot has accounted for: one it loaded,
// or one a directory listing has just shown is not there.
//
// Without this, any boot whose load failed -- no VMU found, a read that timed
// out, a busy maple frame -- left saveRAM zeroed, Object.cpp:409 filled in
// defaults and queued a write, and the next flush unlinked the real save and
// put those defaults in its place. On real hardware with a stock VMU that is
// how a save was lost on 2026-11-22. A save that exists but was not loaded is
// now never touched: the write stays pending and the refusal is logged, so the
// worst case is one session's progress unsaved rather than a save destroyed.
//
// Sticky once set. A later read failing does not clear it, because a failed
// load leaves saveRAM alone and it still holds what was loaded earlier.
static bool dcSavPrimaryOK = false;
static bool dcUsrPrimaryOK = false;

#define DC_SAV_MAGIC (0x32564153) // 'SAV2'
#define DC_USR_MAGIC (0x32525355) // 'USR2'

// A 4-aligned scratch buffer big enough for the largest file body (whole
// saveRAM + userdata + VMU package header/icon). int[] => 4-byte aligned, which
// vmu_pkg_parse's returned data pointer inherits (header + icon are 4-multiples).
static int dcScratch[(int)(sizeof(int) * (5 + SAVEDATA_SIZE + ACHIEVEMENT_COUNT + LEADERBOARD_COUNT) + 2048) / (int)sizeof(int)];

// FNV-1a, to skip re-writing an unchanged payload.
static uint32 dcCRC(const void *p, int n)
{
    const uint8 *b = (const uint8 *)p;
    uint32       h = 2166136261u;
    for (int i = 0; i < n; ++i) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

// Path of the first attached memory card, e.g. "/vmu/a1". False if none.
static bool dcFindVMU(char *out)
{
    maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_MEMCARD);
    if (!dev)
        return false;
    sprintf(out, "/vmu/%c%d", 'a' + dev->port, dev->unit);
    return true;
}

// Is `fname` in the card's directory? 1 yes, 0 no, -1 the directory could not
// be read, which callers must treat as "maybe". fs_vmu reads the whole
// directory into memory when it is opened, so a NULL from fs_readdir is the
// genuine end of the listing; an I/O error shows up as the open failing.
static int dcVmuHasFile(const char *vmu, const char *fname)
{
    file_t d = fs_open(vmu, O_RDONLY | O_DIR);
    if (d == FILEHND_INVALID)
        return -1;

    int found = 0;
    const dirent_t *e;
    while ((e = fs_readdir(d)) != NULL) {
        if (!strcmp(e->name, fname)) {
            found = 1;
            break;
        }
    }
    fs_close(d);
    return found;
}

// Build a VMU package around `payload` and write it to <vmu>/<fname>. Skips the
// write (returns true) when the payload's CRC matches *lastCRC from a prior write.
static bool dcVmuWrite(const char *fname, const char **supersede, const char *descLong, const int *payload, int payloadBytes,
                       uint32 *lastCRC, bool *haveLast, bool *primaryOK)
{
    uint32 crc = dcCRC(payload, payloadBytes);
    if (*haveLast && crc == *lastCRC)
        return true; // unchanged since last successful write

    char vmu[32];
    if (!dcFindVMU(vmu)) {
        printf("[DCSave] no VMU attached; %s not written\n", fname);
        fflush(stdout);
        return false;
    }

    // See dcSavPrimaryOK. Checked before anything is built or unlinked.
    if (!*primaryOK) {
        int has = dcVmuHasFile(vmu, fname);
        if (has != 0) {
            printf("[DCSave] %s/%s %s on the card but was not loaded this boot -- NOT overwriting it\n", vmu, fname,
                   has > 0 ? "is" : "may be");
            fflush(stdout);
            return false; // stays pending; nothing on the card is touched
        }
        *primaryOK = true; // confirmed absent, so creating it is safe
    }

    vmu_pkg_t pkg;
    memset(&pkg, 0, sizeof(pkg));
    strncpy(pkg.desc_short, "SONIC 2", sizeof(pkg.desc_short) - 1);
    strncpy(pkg.desc_long, descLong, sizeof(pkg.desc_long) - 1);
    strncpy(pkg.app_id, "RSDKv4", sizeof(pkg.app_id) - 1);
    pkg.icon_cnt        = 1;
    pkg.icon_anim_speed = 0;
    pkg.eyecatch_type   = VMUPKG_EC_NONE;
    memcpy(pkg.icon_pal, DC_SAVE_ICON_PAL, sizeof(pkg.icon_pal));
    pkg.icon_data = (uint8 *)DC_SAVE_ICON_DATA;
    pkg.data_len  = payloadBytes;
    pkg.data      = (const uint8 *)payload;

    uint8 *blob = NULL;
    int    blobSize = 0;
    if (vmu_pkg_build(&pkg, &blob, &blobSize) < 0 || !blob) {
        printf("[DCSave] vmu_pkg_build failed for %s\n", fname);
        fflush(stdout);
        return false;
    }

    char path[48];
    sprintf(path, "%s/%s", vmu, fname);
    fs_unlink(path); // fs_vmu won't overwrite in place

    FILE *f  = fopen(path, "wb");
    bool  ok = false;
    if (f) {
        ok = (fwrite(blob, 1, blobSize, f) == (size_t)blobSize);
        fclose(f);
    }
    free(blob);

    if (ok) {
        *lastCRC  = crc;
        *haveLast = true;
        printf("[DCSave] wrote %s (%d B, ~%d blocks)\n", path, blobSize, (blobSize + 511) / 512);

        // Migration. The payload is now safely under the primary name, so give
        // the card back the blocks the old copy is holding — a VMU only has 200
        // of them and a stale duplicate is 3 of those. Strictly after a verified
        // write, so there is never a moment where neither copy exists.
        if (*supersede) {
            char old[48];
            sprintf(old, "%s/%s", vmu, *supersede);
            if (fs_unlink(old) == 0)
                printf("[DCSave] removed superseded %s\n", old);
            *supersede = NULL; // migrated; do not try again
        }
    }
    else {
        printf("[DCSave] write %s FAILED (VMU full? needs ~%d blocks)\n", path, (blobSize + 511) / 512);
    }
    fflush(stdout);
    return ok;
}

// Locate our payload inside a raw VMU file body. Primary path: vmu_pkg_parse.
// Fallback (if parse rejects the package — some KOS/emulator builds are strict
// about block-padding or CRC): scan the raw bytes for the 4-byte magic that
// begins every payload we write. Either way *plOut points into dcScratch.
//
// Returns 1 loaded; 0 confirmed absent; 2 read in full but holding none of our
// data, so safe to replace; -1 there (or possibly there) but could not be read.
// Only -1 keeps the write gate shut.
//
// A file that is listed but will not open is retried. Under KOS 2.2.2
// maple_frame_lock is a try-lock -- vmu_block_read gives up with MAPLE_EAGAIN
// the instant the VMU's frame is busy, where KOS master waits for it -- and
// fs_vmu reads the whole file at open, so any one block failing makes fopen
// return NULL for a healthy save. That used to be logged as "not present".
static int dcVmuReadNamed(const char *vmu, const char *fname, uint32 magic, int **plOut, int *countOut)
{
    char path[48];
    sprintf(path, "%s/%s", vmu, fname);

    FILE *f = NULL;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        f = fopen(path, "rb");
        if (f)
            break;
        int has = dcVmuHasFile(vmu, fname);
        if (has == 0) {
            printf("[DCSave] read: %s not present\n", path);
            fflush(stdout);
            return 0;
        }
        printf("[DCSave] read: %s %s but would not open (attempt %d of 3)\n", path,
               has > 0 ? "is listed" : "may exist", attempt);
        fflush(stdout);
        if (attempt < 3)
            thd_sleep(50);
    }
    if (!f)
        return -1;

    size_t got = fread(dcScratch, 1, sizeof(dcScratch), f);
    fclose(f);
    printf("[DCSave] read: %s -> %u bytes\n", path, (uint)got);
    fflush(stdout);
    if (got < 16)
        return -1;

    // Primary: parse the VMU package properly.
    vmu_pkg_t pkg;
    int       pr = vmu_pkg_parse((uint8 *)dcScratch, got, &pkg);
    if (pr >= 0 && pkg.data && pkg.data_len >= (int)sizeof(int)) {
        int *pl = (int *)pkg.data;
        if (pl[0] == (int)magic) {
            *plOut    = pl;
            *countOut = pkg.data_len / sizeof(int);
            printf("[DCSave] read: parsed via vmu_pkg (data_len=%d)\n", pkg.data_len);
            fflush(stdout);
            return 1;
        }
        printf("[DCSave] read: vmu_pkg parsed but magic mismatch (0x%08x)\n", (uint)pl[0]);
        fflush(stdout);
    }
    else {
        printf("[DCSave] read: vmu_pkg_parse rejected (%d) — scanning for magic\n", pr);
        fflush(stdout);
    }

    // Fallback: the header (128) + one icon frame (512) puts our payload at a
    // fixed 640-byte offset, but rather than assume that, scan the whole body for
    // the magic (4-aligned) so a differently-sized package still works.
    int words = (int)(got / sizeof(int));
    for (int i = 0; i < words; ++i) {
        if (dcScratch[i] == (int)magic) {
            *plOut    = &dcScratch[i];
            *countOut = words - i;
            printf("[DCSave] read: found payload by magic scan at +%d B\n", i * (int)sizeof(int));
            fflush(stdout);
            return 1;
        }
    }

    // Opened and read in full, and none of our data is in it -- a write cut off
    // by a power loss, say. There is nothing here to lose, so it may be replaced
    // (2). Refusing would leave the card unable to take a save ever again until
    // the file was deleted by hand. Contrast the -1 cases above, where the file
    // could not be read and may well hold a perfectly good save.
    printf("[DCSave] read: magic not found in %s\n", path);
    fflush(stdout);
    return 2;
}

// Try each candidate name in turn and stop at the first whose payload carries
// the expected magic. Opens the write gate (*primaryOK) only when the primary
// name was loaded or shown to be absent; "no VMU" and "there but unreadable"
// leave it as it was.
static bool dcVmuRead(const char *const *names, int nameCount, uint32 magic, int **plOut, int *countOut, const char **supersede,
                      bool *primaryOK)
{
    char vmu[32];
    if (!dcFindVMU(vmu)) {
        printf("[DCSave] read: no VMU attached\n");
        fflush(stdout);
        return false;
    }

    for (int i = 0; i < nameCount; ++i) {
        int r = dcVmuReadNamed(vmu, names[i], magic, plOut, countOut);
        if (i == 0 && r != -1)
            *primaryOK = true; // primary loaded, absent, or read and empty of our data
        if (r == 1) {
            *supersede = i ? names[i] : NULL; // non-primary => migrate on next write
            if (i) {
                printf("[DCSave] read: loaded from %s; next write moves it to %s\n", names[i], names[0]);
                fflush(stdout);
            }
            return true;
        }
    }
    return false;
}

// ---- game progress (saveRAM[]) ----

bool DC_LoadSaveRAM()
{
    int *pl, cnt;
    if (!dcVmuRead(DC_SAV_NAMES, DC_NAMECOUNT(DC_SAV_NAMES), DC_SAV_MAGIC, &pl, &cnt, &dcSavSupersede, &dcSavPrimaryOK)) {
        printf("[DCSave] no game save loaded\n");
        fflush(stdout);
        return false;
    }
    // pl[0]=magic, pl[1]=ver, pl[2]=used, pl[3..]=saveRAM
    if (cnt < 3) {
        printf("[DCSave] game save truncated\n");
        fflush(stdout);
        return false;
    }
    int used = pl[2];
    if (used < 0)
        used = 0;
    if (used > SAVEDATA_SIZE)
        used = SAVEDATA_SIZE;

    memset(saveRAM, 0, sizeof(saveRAM));
    for (int i = 0; i < used && (3 + i) < cnt; ++i)
        saveRAM[i] = pl[3 + i];

    printf("[DCSave] loaded game save (%d ints)\n", used);
    fflush(stdout);
    return true;
}

// Build the current saveRAM[] payload and write it. Only called from
// DC_FlushSaves(); DC_StoreSaveRAM() below just marks it pending.
static bool dcWriteSaveRAMNow()
{
    static uint32 lastCRC  = 0;
    static bool   haveLast = false;

    // trim trailing zeros (saveRAM is zero-initialised, so this is lossless)
    int used = SAVEDATA_SIZE;
    while (used > 0 && saveRAM[used - 1] == 0)
        --used;

    int n = 0;
    dcScratch[n++] = DC_SAV_MAGIC;
    dcScratch[n++] = 1; // version
    dcScratch[n++] = used;
    for (int i = 0; i < used; ++i)
        dcScratch[n++] = saveRAM[i];

    return dcVmuWrite(DC_SAV_NAMES[0], &dcSavSupersede, "Sonic 2 Save Data", dcScratch, n * sizeof(int), &lastCRC, &haveLast,
                      &dcSavPrimaryOK);
}

// ---- achievements + leaderboards ----

void DC_LoadUserdata()
{
    int *pl, cnt;
    if (!dcVmuRead(DC_USR_NAMES, DC_NAMECOUNT(DC_USR_NAMES), DC_USR_MAGIC, &pl, &cnt, &dcUsrSupersede, &dcUsrPrimaryOK) || cnt < 4) {
        // no (valid) userdata: leaderboards default to "no record set"
        for (int l = 0; l < LEADERBOARD_COUNT; ++l)
            leaderboards[l].score = 0x7FFFFFF;
        printf("[DCSave] no userdata on VMU (defaults applied)\n");
        fflush(stdout);
        return;
    }

    int savedAch = pl[2];
    int savedLb  = pl[3];
    int base     = 4;
    for (int a = 0; a < ACHIEVEMENT_COUNT; ++a)
        achievements[a].status = (a < savedAch && (base + a) < cnt) ? pl[base + a] : 0;

    int lbase = base + (savedAch > 0 ? savedAch : 0);
    for (int l = 0; l < LEADERBOARD_COUNT; ++l) {
        int v = (l < savedLb && (lbase + l) < cnt) ? pl[lbase + l] : 0;
        leaderboards[l].score = v ? v : 0x7FFFFFF;
    }
    printf("[DCSave] loaded userdata (%d ach, %d lb)\n", savedAch, savedLb);
    fflush(stdout);
}

// As above, for achievements + leaderboards.
static bool dcWriteUserdataNow()
{
    static uint32 lastCRC  = 0;
    static bool   haveLast = false;

    int n = 0;
    dcScratch[n++] = DC_USR_MAGIC;
    dcScratch[n++] = 1; // version
    dcScratch[n++] = ACHIEVEMENT_COUNT;
    dcScratch[n++] = LEADERBOARD_COUNT;
    for (int a = 0; a < ACHIEVEMENT_COUNT; ++a)
        dcScratch[n++] = achievements[a].status;
    for (int l = 0; l < LEADERBOARD_COUNT; ++l)
        dcScratch[n++] = leaderboards[l].score;

    return dcVmuWrite(DC_USR_NAMES[0], &dcUsrSupersede, "Sonic 2 Records", dcScratch, n * sizeof(int), &lastCRC, &haveLast,
                      &dcUsrPrimaryOK);
}

// ---- deferred writing ----
//
// A VMU write costs roughly 200-400ms: fs_unlink rewrites the directory and
// FAT, then the data blocks go over the maple bus at VMU flash-write speed.
// Done inline on the main thread that is a visible freeze, and the engine asks
// for it at the worst possible moments — AwardAchievement() (Userdata.cpp:937)
// and SetLeaderboard() (Userdata.cpp:1021) both call WriteUserdata() the
// instant the record lands. SetLeaderboard only writes when the new time BEATS
// the stored one, which is exactly why the freeze showed up on fast runs and
// not slow ones: crossing the signpost with a new best time, or the frame a
// boss finally dies.
//
// So the store entry points only raise a flag. DC_FlushSaves() does the real
// work, from the two places where a pause that long is already free: the stage
// load (loading screen up, and we are about to twiddle 256KB of tileset
// anyway) and a settled pause menu. Both are in DCGraphics.cpp.
//
// Nothing is snapshotted, deliberately. The payloads are rebuilt at flush time
// from the live saveRAM[] / achievements[] / leaderboards[] globals, so any
// number of updates between two flushes collapse into one write carrying the
// newest state, and there is no second copy of saveRAM to keep in sync.
//
// The pending flag is cleared only on success, so a flush with no VMU attached
// retries at the next flush point rather than dropping the data.
//
// Cost of deferring: an achievement or record earned and then powered off
// before the next stage load or pause is lost. In normal play the next stage
// load is seconds away.
static bool dcSavPending = false;
static bool dcUsrPending = false;

bool DC_StoreSaveRAM()
{
    dcSavPending = true;
    return true; // queued — the physical write happens in DC_FlushSaves()
}

bool DC_StoreUserdata()
{
    dcUsrPending = true;
    return true;
}

void DC_FlushSaves()
{
    // dcVmuWrite() returns true both for a real write and for a payload whose
    // CRC is unchanged since the last one, so a flush with nothing to do costs
    // only the payload rebuild — no VMU traffic, no wear.
    if (dcSavPending && dcWriteSaveRAMNow())
        dcSavPending = false;
    if (dcUsrPending && dcWriteUserdataNow())
        dcUsrPending = false;
}

#endif // RETRO_USING_KOS
