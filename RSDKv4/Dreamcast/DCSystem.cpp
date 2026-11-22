// RSDKv4 Dreamcast system helpers: timing
#include "../RetroEngine.hpp"

#if RETRO_USING_KOS

#include <kos.h>
#include <arch/timer.h>
#include <dc/maple.h>
#include <dc/maple/vmu.h>

#include "DCCommon.hpp"

unsigned long long DC_GetTicksPerSecond() { return 1000000000ULL; }

unsigned long long DC_GetTicks() { return timer_ns_gettime64(); }

void DC_Probe(int phase)
{
    // Boot-phase breadcrumb (log-only now that serial logging works).
    DC_LOG("[DC] probe %d\n", phase);
}

// ---------------------------------------------------------------------
// VMU LCD icon
// ---------------------------------------------------------------------
// Sonic 2 winged-emblem icon, drawn to fill the whole 48x32 VMU LCD (the
// emblem's ~1.5:1 aspect matches the LCD). 48 wide x 32 tall, 1 bit/pixel,
// 6 bytes/row, MSB = leftmost, 1 = dark/ink.
static const unsigned char dcVmuArt[192] = {
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x1F,0xF8,0x00,0x00, 0x00,0x00,0x70,0x0E,0x00,0x00,
    0x40,0x00,0xC0,0x03,0x00,0x02, 0x60,0x01,0x80,0x01,0x80,0x06, 0x58,0x03,0x00,0x00,0xC0,0x1A,
    0x47,0x86,0x00,0x00,0x61,0xE2, 0x40,0x7E,0x00,0x00,0x3E,0x02, 0x40,0x1E,0x00,0x00,0x3C,0x02,
    0x60,0x16,0x07,0xF0,0x28,0x06, 0x60,0x26,0x03,0xE8,0x34,0x06, 0x30,0x2B,0xFF,0xF0,0x24,0x0C,
    0x38,0x25,0xF5,0x50,0x34,0x0C, 0x3C,0x05,0x00,0x00,0x3C,0x1C, 0x0C,0x4B,0x00,0x00,0x02,0x30,
    0x1C,0x4F,0x00,0x00,0x52,0x38, 0x3E,0x4E,0x00,0x01,0xBA,0x7C, 0x26,0x56,0x1F,0xFF,0xF2,0x64,
    0x30,0x4E,0x00,0x00,0x6A,0x0C, 0x18,0x56,0x00,0x00,0x72,0x18, 0x1C,0x2E,0x00,0x00,0x6C,0x38,
    0x0E,0x3E,0x00,0x00,0x74,0x70, 0x07,0x2E,0x00,0x00,0x64,0xE0, 0x03,0xDE,0x00,0x00,0x7B,0xC0,
    0x01,0xFF,0xFF,0xFF,0xFF,0x80, 0x00,0x7F,0xFF,0xFF,0xFE,0x00, 0x00,0x3E,0x3F,0xFC,0x7C,0x00,
    0x01,0xF8,0xFF,0xFF,0x9F,0x80, 0x00,0xFF,0xFF,0xFF,0xFF,0x00, 0x00,0x3F,0xF0,0x0F,0xFC,0x00,
    0x00,0x7F,0xF8,0x1F,0xFE,0x00, 0x00,0xFF,0x07,0xE0,0xFF,0x00
};

// The VMU LCD framebuffer is addressed from the bottom-right (origin flipped
// both ways), 6 bytes per row. Built once, pushed to every connected VMU.
static unsigned char dcVmuLcd[192];
static bool dcVmuLcdReady = false;

void DC_VmuShowIcon()
{
    if (!dcVmuLcdReady) {
        memset(dcVmuLcd, 0, sizeof(dcVmuLcd));
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 48; ++x) {
                if (dcVmuArt[y * 6 + (x >> 3)] & (0x80 >> (x & 7)))
                    dcVmuLcd[(31 - y) * 6 + (47 - x) / 8] |= (unsigned char)(0x80 >> ((47 - x) & 7));
            }
        }
        dcVmuLcdReady = true;
    }

    // Push to every connected LCD (both controller slots, both VMU slots).
    maple_device_t *dev;
    for (int i = 0; (dev = maple_enum_type(i, MAPLE_FUNC_LCD)) != NULL; ++i) vmu_draw_lcd(dev, dcVmuLcd);
}

// Called once per frame from DC_ProcessInput.
//
// The icon used to be pushed only from LoadStageGIFFile (Scene.cpp), so it did
// not appear until the first STAGE loaded — i.e. not until you picked a save
// and started Sonic 2. The whole front end (SEGA screen, the Whitehead logo,
// the main menu) is native objects rather than stages, so a VMU sat blank
// through all of it. Polling here instead puts the emblem up on the very first
// frame, as soon as maple has finished its boot enumeration.
//
// Enumerating twice a second and pushing only when the LCD count changes keeps
// the steady-state cost at one walk of the maple device table per 30 frames,
// and still covers hot-plugging a VMU mid-game. The Scene.cpp call is left in
// place; it is harmless and costs nothing on a screen that is already loading.
void DC_VmuTick()
{
    static int cooldown  = 0;
    static int lastCount = -1;

    if (--cooldown > 0)
        return;
    cooldown = 30;

    int count = 0;
    while (maple_enum_type(count, MAPLE_FUNC_LCD) != NULL) ++count;

    if (count == lastCount)
        return;
    lastCount = count;

    if (count > 0)
        DC_VmuShowIcon();
}

#endif // RETRO_USING_KOS
