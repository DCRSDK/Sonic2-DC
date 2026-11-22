# Sonic 2 / RSDKv4 — Dreamcast asset generation

The Sonic 2 counterpart to the Sonic 1 kit. Same shape: pack in, loose `Data/`
tree out, verified on the way.

```
./generate_assets.sh <path/to/Data.rsdk> [stage_dir] [--menu-no-vq] [--skip-menu]
```

Defaults the stage to `../build-dc`, which is where the loose tree has to end up
next to `Data.rsdk` anyway. Requires bash, python3, ffmpeg, ffprobe, plus
python3-pil and python3-numpy for the menu textures (`--skip-menu` drops both).

## Output

| what | format | read by |
|---|---|---|
| `Data/Music/*.ogg` | headerless s8, **stereo**, 22050 | `DC_MusReaderThread` |
| `Data/SoundFX/Global/*.ogg` | headerless s8, **mono**, 22050 | `DC_LoadRawSfx` |
| `Data/Game/Menu/*.png` | VQ `.dtex` | `DC_NativeTryLoadDtex` |
| `Data/Game/Models/*.png` | VQ `.dtex` | same |

Everything keeps its original extension — the audio because GameConfig resolves
by filename, the textures because the loader identifies `.dtex` by its magic.
Only the contents change.

## The one difference from the Sonic 1 kit that will bite

**Sonic 1's global sfx are `.wav`. Sonic 2's are `.ogg`.**

Both hold the same headerless s8 mono PCM afterwards and both keep their
original name, but copying Sonic 1's `*.wav` glob across would match nothing
under Sonic 2's `Data/SoundFX/Global` and convert nothing, silently. That is why
`verify_assets.sh` checks Global for **both** failure modes here: a file still
holding a real Ogg container (nothing converted it) and a file holding RIFF
(converted without `-f s8`).

Music is stereo in both — and mono in Sonic CD. Do not copy that one either.

## Before the first run: two files this kit does not ship

**`rsdkv4_extract.py`** — copy it from the Sonic 1 kit. It is game-agnostic;
only the filelist it consumes is per-game. `generate_assets.sh` names it in the
error if it is missing.

**`rsdkv4_filelist.txt`** — generate it:

```
./make_filelist.py --data <extracted Data> --engine ../RSDKv4 --out rsdkv4_filelist.txt
```

RSDKv4 stores only an MD5 of each lowercased path, so the extractor cannot
enumerate names and has to be handed the list. `make_filelist.py` derives it
from the same three sources the Sonic 1 list was assembled from:

* **sound effects** from `GameConfig.bin` (global) and each
  `Stages/<folder>/StageConfig.bin` (stage) — the engine's own path strings, so
  they cannot drift from what `LoadSfx` will ask for;
* **music** from the `Data/Music` listing, because RSDKv4 names tracks only in
  script bytecode as `SetMusicTrack`'s first argument — there is nothing in the
  configs to read;
* **menu textures** from the `LoadTexture()` literals in
  `RSDKv4/NativeObjects`, with `%d` patterns expanded 1..4. Those are the only
  paths `DC_NativeTryLoadDtex` will ever `fopen`, so harvesting them avoids
  converting retail art the engine never references.

This list was deliberately **not** written out by hand from the shipped disc.
Music is the class that cannot be recovered from the configs, and a missing line
is a missing track with nothing in the log to say so.

## Two bugs found while building this

**The EXIT trap swallows a clean exit.** Both kits had:

```sh
cleanup() { [[ -n "$work" && -d "$work" ]] && rm -rf -- "$work"; }
trap cleanup EXIT
```

An EXIT trap's final command status replaces the status passed to `exit`. On
every run that takes a pre-extracted tree rather than the pack, `$work` is
empty, the `[[ ]] && rm` chain evaluates false, the trap returns 1 — and the
script exits **1** immediately after printing `verify: OK`. Anything reading the
status (CI, a notebook cell under `set -e`, an `&&` chain) treats a clean run as
a failure. Fixed here by ending the function with `return 0`.
**The Sonic 1 `generate_assets.sh` still has this** — same line, line 128.

**Stage sfx cost one byte per frame, not two.** An earlier Sonic 2 budget script
charged AICA residency at 2 bytes per frame and used a 65520-frame cap. At
`DC_AICA_SAMPLE_BITS 8` a resident frame is **one** byte, padded up to 32, with
the divisor starting at 2 for sources at or above 32000 Hz and incrementing
until the count fits a **65504**-frame cap. That model reproduces all 24
`[DCSfx] aica` lines from a hardware log exactly, including the three files that
need `/3`, and totals Hidden Palace's 23 sounds to 702,912 bytes — the figure in
the log. The old model claimed 1,405,824. Nothing on disc changes size either
way, which is why no size check caught it.

## What `verify_assets.sh` catches

A headerless file says nothing about itself, so a wrong rate or channel count is
a pitch shift you notice by ear or not at all. Structural checks always, plus an
exact size check against each source duration when the original tree is given —
`bytes = rate × channels × seconds`, and nothing else can be true.

All six failure modes were planted and confirmed to fail the run:

| planted | caught as |
|---|---|
| global sfx left as real Vorbis | still a real Ogg container |
| global sfx muxed to RIFF (no `-f s8`) | `DC_LoadRawSfx` will refuse this |
| a real PNG left in `Game/Menu` | the DC cannot decode it |
| a loose `GameConfig.bin` staged | overrides the packed one |
| odd byte count in a track | not whole stereo frames |
| music converted mono | `0.50x — wrong rate or channel count?` |

That last one is the Sonic CD mistake, caught by arithmetic rather than by ear.

## Do not touch the music loop points

The value in `GameConfig.bin` is a PCM frame index into the **original 44100 Hz
Vorbis**, and `DC_MusFramesToBytes` rescales it at run time
(`DC_MUS_OGG_RATE` 44100 → `DC_MUS_SRC_RATE` 22050). The shipped values are
already correct. A table of loop points "recalculated for 22050" would be
rescaled a second time and every track would loop at half its intended position.

## Verified

* Full pipeline runs clean on a synthetic Sonic 2 shaped tree, exits 0, and is
  idempotent — a second pass over its own output converts nothing and still
  verifies.
* Output sizes match the arithmetic: a 2.0 s track lands at exactly 88,200 bytes
  (22050 × 2 ch × 1 byte × 2 s).
* `make_filelist.py` dedupes a sound shared by several sfx slots, expands
  `TimeAttack%d.png` to 1..4, and pulls stage sfx out of the StageConfigs.
* The menu step's format choice matches the shipped disc: binary alpha picks
  VQ/1555, graded alpha picks VQ/4444.

## The Colab autobuilder

`Sonic2_Dreamcast_Autobuilder.ipynb` is the Sonic 2 counterpart to the Sonic 1
notebook, same two-section shape: **Section A** compiles the ELF and runs this
kit to produce a build kit, **Section B** masters that kit into `Sonic2.cdi`
with mkdcdisc in about two minutes. Split so a re-master costs two minutes
instead of a recompile.

It takes **one upload** — your own `Data.rsdk` — and ships no game data.

Three things in it are worth knowing:

**`REPO_URL` is deliberately empty.** A plausible-looking wrong URL clones
somebody else's tree, compiles it, and fails twenty minutes later as a cmake
error nobody can place. Fill it in before the first run; a local path works too,
since `git clone` accepts one.

**`KOS_COMMIT` is pinned to `14bc62b6`, the same commit the Sonic 1 notebook
uses.** That is not copied on faith — a Sonic 2 boot log off the development
machine reads `KallistiOS v2.3.0 [dreamcast/pristine] / Git revision:
14bc62b6-dirty / sh-elf-gcc (GCC) 14.4.1 20260715`, so both ports are developed
against the same KOS tree. It is a commit and not a tag because there is no
`v2.3.0` tag: 2.3.0 is the version master reports, and the newest release tag is
`v2.2.2`. On master the toolchain builder is `utils/kos-chain`, not
`utils/dc-chain`.

**It preflights this kit before the compile, not after.** `rsdkv4_extract.py`,
`rsdkv4_filelist.txt`, `generate_assets.sh` and `verify_assets.sh` all have to
be committed under `dreamcast/`, and the notebook checks for them in step 2/6
with the `make_filelist.py` command in the error — because discovering a missing
filelist on the far side of a 25-minute compile wastes a whole Colab session.

Section A also prints `sh-elf-size` on the ELF. The engine is almost entirely
statically allocated, so BSS is what decides how much heap is left on a 16 MB
machine; about 12.1 MB is normal for this port.

### Verified on the notebook

* Parses as nbformat 4 with the expected four cells, and both code cells parse
  as Python.
* `REPO_URL` empty stops the run **before** apt, so the useless 25-minute path
  is never entered.
* The upload step was exercised four ways: nothing uploaded; an RSDKv3 pack
  (rejected by magic — that is the Sonic CD datafile and needs the other
  builder); an `RSDKvB` pack too small to be Sonic 2 (warned, not failed, since
  a modified pack is legitimate); and a browser-mangled `Data (1).rsdk` (picked
  up, because the search is by extension and not by exact name).
* The clone-then-preflight path runs for real against a repo whose `dreamcast/`
  is missing `rsdkv4_filelist.txt`, and fails with the generate command in the
  message.
* Section B's `mkdcdisc` argv and verification list were exercised with the
  shell stubbed: `-f settings.ini` appears only when the repo ships one, the
  `Data/Game/Menu` needle only when the menu art was staged, and a second run
  over an existing image clears it rather than hitting mkdcdisc's exit 4.

### Not verified, and why

**Section A has run end to end** against the real working tree and the Sonic 1
toolchain archive (2026-11-22): ELF compiled, assets extracted and converted,
`verify: OK`, build kit written. See *Checked against the shipped build-dc*
below.

**Section B has mastered a real CDI** (2026-11-22). The test sandbox cannot
reach gitlab.com, so mkdcdisc was supplied as a source checkout (main at
`fc6a7ab`, v0.0.4) placed where the cell would have cloned it; the cell's own
`meson setup -Dpng=enabled` + `ninja` built it in 18 s. Result: a 157 MB
`Sonic2.cdi`, all eight structural checks passing, and a content search of the
image finding the custom `Generic.dtex` and the uploaded `settings.ini` inside
it while the retail Generic texture is absent. Not yet booted — that is the
remaining test, and it is also the first boot of an ELF built against KOS 2.2.2
rather than master.

**The from-source `kos-chain` path is untested** for the same reason. The
prebuilt-toolchain path is the one to use — but see the section below for what
the Sonic 1 archive actually contains, which is not what this notebook pins.

**Step 4/4's needles are substring matches over the whole image**, so a needle
as short as `MENU` could in principle match compressed bytes inside `Data.rsdk`
and pass when the directory really was dropped. It catches a silently missing
directory, which is what it is for; it does not prove one is present, and it is
no substitute for booting the disc.

## The toolchain the Sonic 1 autobuilder uses

Inspected 2026-11-21 from the `.tar.xz` itself, not from its description. It is
136 MiB compressed, 434 MiB unpacked, and every entry is relative under
`opt/toolchains/dc` with no absolute or `..` paths, so `tar -xf -C /` places it
correctly.

| | the archive | what the Sonic 2 port is built against |
|---|---|---|
| KOS branch | `v2.2.x` | `master` |
| KOS commit | `5c274f02` (`v2.2.2-1-g5c274f02`) | `14bc62b6` |
| `kos/version.h` reports | 2.2.2 | 2.3.0 |
| toolchain builder | `utils/dc-chain` | `utils/kos-chain` |
| GCC | 14.3.0 | 14.4.1 |
| newlib | 4.5.0 | — |
| arm-eabi | absent | — |

So it is a **different KOS line and a different GCC**, not the same combination.
The working tree is clean against `5c274f02`, and the clone is dated
2026-08-31. The two commits share a subject and author date
(`kos-chain: Force C++14 to build GCC`, 8 Jul 2026) because the same change was
applied to both branches — the trees differ.

It will very probably still build this port. All 13 KOS headers and all 53 KOS
symbols the engine references exist in 2.2.2. Of the five declarations that
differ, four are cosmetic — `uint32`/`uint32_t`, `uint32_t`/`size_t` for a
32-bit count, function versus `static inline`, `int`/`unsigned int` — and the
fifth is `fs_total`, which returns `size_t` on 2.2.2 and `ssize_t` on master.
That one is already cast explicitly in `DCAudio.cpp` with a comment
anticipating the drift. Nothing in the port is `#if`'d on `KOS_VERSION`, and
`pvr_filter_mode_t` (the one parameter type that was widened) exists in both.

**And it does build.** The Sonic 2 working tree compiled first time against
this archive, with no source changes: zero errors, and none of the 317 warnings
involve a KOS header or KOS symbol — they are all engine-side and pre-existing
(126 sign-compare in `String.hpp`, 69 MSVC `#pragma warning` lines GCC ignores,
unused variables, one `-Wrestrict` in `Debug.cpp`). 15 seconds on two cores.

| | this build | the shipped ELF (earlier session) |
|---|---|---|
| text | 676,745 | 677,081 |
| data | 5,044 | 4,632 |
| bss | 12,769,952 | 12,700,208 |

BSS is 68 KB larger. That cannot be attributed from here — the shipped figure
was taken from an ELF built against KOS master from an earlier state of the
tree, so the KOS version and the source both moved. Building the same tree
against `14bc62b6` would separate the two.

### The pin does not protect you here

`KOS_COMMIT` is inert whenever a prebuilt archive is supplied: `have_compiler()`
and `have_kos()` both succeed, `fetch_kos()` never runs, and the pin is never
compared against anything. Both notebooks have this shape. That is why Section A
now prints the KOS commit it actually found, flags it when it is not the pinned
one, and lists any uncommitted changes.

### If you want the pinned combination in Colab

The archive's KOS is a **full** clone and already contains `14bc62b6`, so
checking it out in place and re-running `make -C /opt/toolchains/dc/kos`
rebuilds KOS master with GCC 14.3.0 in 10–15 minutes instead of the 60–90 a
fresh `kos-chain` costs. That still leaves GCC at 14.3.0 rather than 14.4.1.
Not implemented or tested in the notebook.

## Checked against the shipped build-dc

The full pipeline was run against `build-dc/Data.rsdk` and every output compared
by MD5 with your own `build-dc/Data`, the known-good disc tree.

**Music: 44 of 44 byte-identical. Global sfx: 43 of 43 byte-identical** —
`SignPost2p.ogg` matches your `SignPost2P.ogg` byte for byte; only the case of
the name differs (GameConfig's spelling versus the PC data's), and ISO9660 does
not care.

**Menu art: 8 of 46 byte-identical, all 46 equivalent.** Same source PNGs (all
46 checked), same converter (one version exists on the machine; your textures
were made four minutes after it was saved), and the converter is deterministic —
numpy 2.2.6 and 2.5.3 produce identical output. The textures still differ
because k-means measures distance with a float32 matrix multiply
(`dtex_convert.py:145`), which goes through BLAS; its rounding depends on the
CPU and library build, near-ties in `argmin` flip, and sixteen iterations
compound it. So VQ bytes are not reproducible across machines, and a Colab disc
will never match a local one bit for bit. Quality is what matters, so both were
decoded with a decoder first proven exact on synthetic textures, then measured
against the source: across the 37 that differ, **mean +0.002 dB, every one
within 0.4 dB**, 27 within 0.05.

Two earlier claims of mine were wrong and are corrected here: the 8 matches are
*not* textures that skipped k-means — every one went through it and happened to
converge identically — and the difference is *not* the numpy version.

### What the comparison caught

* **`make_filelist.py` dropped TimeAttack1-4.** `RecordsScreen.cpp` builds the
  path with `sprintf` and passes a buffer to `LoadTexture`, so harvesting only
  `LoadTexture("...")` literals cannot see it. Now harvests `.png` path
  literals wherever they appear.
* **`verify_assets.sh` failed 8 good sounds.** Its 1% size tolerance is too
  tight for 0.03 s sounds, where ffprobe's duration and the decoded sample count
  disagree by up to one Vorbis block overlap. All 8 were byte-identical to the
  shipped files. Tolerance is now the larger of 1% and 128 frames, and a
  planted 2x, 0.5x and stereo ScoreAdd (the smallest file) all still fail.
* **The notebook's KOS report killed the build it was meant to describe.** Tar
  run as root keeps the archive's owner (uid 1000); git refuses that repo as
  "dubious ownership". Now uses `safe.directory` and cannot stop the run.
* **`BUILD_INFO.txt` recorded the pin, not the toolchain.** It said KOS master /
  GCC 14.4.0 for an ELF built with KOS 2.2.2 / GCC 14.3.0. Now records the
  compiler's own version string and the KOS tree's `git describe`.
* **The preflight missed `dtex_convert.py`.** The first full run compiled and
  then died at the menu step. Now checked at step 2.

### Differences that are correct

* `Amazon.png` and `Text_KO@1x.png` are on your disc but nothing in the engine
  names them. The kit does not produce them.
* `Text_KO.png` and `VirtualDPadM.png` are named by the engine but are not in
  the pack; the extractor skips them. `VirtualDPadM` is the mobile D-pad and is
  never created on the Dreamcast.
* Your disc carries 69 loose `SoundFX/Stage/*.wav`. Nothing reads them —
  `Audio.cpp:784` sends stage sfx to the pack, deliberately — so the kit does
  not stage them and `verify_assets.sh` warns if they are present.

### Before you push

These are untracked in git today, so a clone — and therefore Colab — will not
have them:

* `dreamcast/` — all of it.
* **`tools/dtex_convert.py`** — without it the menu cannot be converted. The
  notebook now stops at step 2 and says so, rather than after the compile.
* **`tools/Generic.dtex`** — your custom art. `generate_assets.sh` now copies
  every `tools/<Name>.dtex` over the converted `<Name>.png` after the menu step
  and before verify, so pushing it to `tools/` at the tree root is all it needs.
  Your shipped `Generic.png` is this file byte for byte; without the copy, every
  rebuild quietly put the retail texture back.

`settings.ini` is deliberately **not** in that list. It is an optional upload:
drop one into `/content` beside `Data.rsdk` and it goes on the disc; leave it
out and the engine uses its built-in defaults. A `dreamcast/settings.ini`
committed to the repo is used only when nothing is uploaded.
