# Sonic the Hedgehog 2 (2013) on the SEGA Dreamcast — how the port works

A native Dreamcast port of [RSDKv4-Decompilation](https://github.com/Rubberduckycooly/RSDKv4-Decompilation),
the decompiled Retro Engine behind the 2013 mobile remasters of *Sonic the
Hedgehog* and *Sonic the Hedgehog 2*. This tree is set up for Sonic 2. It runs
on KallistiOS, draws through the PowerVR, and holds a locked 60 fps on real
hardware — Hidden Palace and the special stages included.

The port takes its overall shape from SF94 / Michael Fadely's RSDKv5 Dreamcast
port (`RSDKv5-Decompilation`, branch `sf94/dreamcast-kallistios-pvr`), but
almost none of the code is shared: v4 is a different engine with a different
renderer, audio path and asset layout.

This document describes what the port does and how. To build it, see
[BUILD_DREAMCAST.md](BUILD_DREAMCAST.md).

## Status

The game is feature-complete and confirmed end to end on a real Dreamcast: boot,
the 2013 native menus, every zone and act including Hidden Palace, the half-pipe
special stages, the credits, pause and exit-to-menu, and VMU saves. It builds
and runs with either of two toolchains:

| | KallistiOS | GCC |
|---|---|---|
| development machine | 2.3.0 (master, `14bc62b6`) | 14.4.1 |
| Colab autobuilder | 2.2.2 (`v2.2.2-1-g5c274f02`) | 14.3.0 |

Everything the port calls in KallistiOS exists in both versions with compatible
signatures, and a build against 2.2.2 has been played on hardware without problems.

## Rendering

Almost everything is drawn by the PowerVR as textured quads:

- **Sprites**, through every engine draw function. A sprite that crosses a
  palette change, such as a water line, is split at that line rather than
  tinted all or nothing.
- **Every tile plane**, with exact per-line parallax and deformation. Tile
  layers are cut into bands at the water line, which is what keeps the
  underwater tint boundary exact in Hidden Palace, Aquatic Ruin and Chemical
  Plant.
- **The special-stage half-pipe.** The engine's 3D faces are queued as PVR
  quads and depth-sorted with `std::stable_sort` instead of the original
  O(n²) bubble sort. That cut a special-stage frame from 23–30 ms to 14–16 ms,
  which is what locks them at 60 fps.
- **Script rectangles and fades**, queued at the point in the draw order where
  the script drew them. The title emblem fades in that way: the script draws
  the emblem, then a full-screen black rectangle over it with falling alpha.

Sheet textures live in VRAM as twiddled 8bpp paletted surfaces, and everything
shares one translucent list with autosorting off, ordered by an explicit z ramp.
A software framebuffer still exists for the few effects that have not moved to
the PowerVR — tinted, scaled masks are one — and is uploaded as a single texture.

The 2013 menus have their own renderer (`DCNative.cpp`) for the 3D models they
are built from. Their textures are pre-converted to VQ-compressed `.dtex` files
and streamed from disc straight into VRAM in 32 KB chunks, so they cost no heap.
Textures the menu no longer needs are evicted and streamed back on demand. Menu
models are evicted the same way when a stage loads, which raises the free heap
at that point from about 280 KB to about 820 KB. They come back lazily when the menu draws them again,
into the same slots, because entities restored on exit to the menu still hold
pointers to them.

## Audio

No Vorbis is decoded on the console, and no kos-ports libraries are linked.
Audio runs on two engines at once:

- **Music** is headerless raw PCM, 22050 Hz stereo signed 8-bit, under the
  original `.ogg` names. A dedicated reader thread streams it off the disc
  through KallistiOS's native `fs_*` calls into a ring buffer, and the mixer
  upsamples it to 44100 Hz. Loop points stay as the engine stores them, as
  frame indices into the original 44100 Hz tracks, and are rescaled at run time.
- **Global sound effects** are raw PCM too, 22050 Hz mono signed 8-bit, loaded
  once at boot onto the engine's software mixer, which a KallistiOS
  `snd_stream` drives. They are widened to 16-bit in RAM, so the stock set
  holds 1,977,500 bytes for the whole run.
- **Stage sound effects** stay in `Data.rsdk` as PCM WAV. At each stage load
  they are decimated and uploaded into the AICA's own 2 MB of sound RAM at 8
  bits per sample, so they cost the main heap nothing and the SH4 no mixing
  time. Hidden Palace, the worst case, uses 702,912 of the 1,867,776 bytes
  available. Anything the AICA refuses falls back to a heap arena.

## Disc access

`Data.rsdk` is read from the disc unmodified. Three asset classes sit beside it
as loose files under `Data/`: music, global sound effects and the menu
textures. The port reads those directly; everything else comes from the pack.
`Data.rsdk` must not be repacked, and a loose file cannot override a packed one,
because the engine's `LoadFile` always resolves the pack first.

Reads from the pack are aligned to the 2048-byte GD-ROM sector, through a 32 KB
file buffer, so KallistiOS's streaming read path engages rather than falling
back to single-sector transfers. `FillFileBuffer` also retries short reads
instead of silently advancing past them. That was a latent bug in the original
decompilation. Contention between the music thread and the main thread made it
reachable, and on encrypted pack data it corrupted the menu's models.

## Saves

Saves go to the first memory card found, as two files, mirroring the engine's
own split so that neither can clobber the other:

- `SONIC2.VMS` — game progress, including the menu settings (box-art region,
  volumes, unlocks) and Time Attack records.
- `SONIC2R.VMS` — achievements and leaderboard scores.

Both carry a `vmu_pkg` header and a custom icon, so they show up properly in the
Dreamcast BIOS file manager, and the `.VMS` names mean a save copied off a card
is already a usable `.VMS`. Saves left under the older names (`SONIC2.SAV`,
`SONIC2.USR` and extensionless) are read and migrated on the next write.

A VMU write takes around 200–400 ms, so writes are deferred and done only where
a pause that long goes unnoticed: a stage load, a settled pause menu, and the
2013 menu's own screen transitions. Writing inline froze the game for a moment
at the signpost on a fast run.

The save code never overwrites a save it could not read. If a save is on the
card but fails to load — a busy maple frame, a read timeout, a card detected
late — the write is refused and logged rather than replacing it with defaults,
and a listed save that will not open is retried before giving up.

The VMU's screen shows a winged "2" from the moment the SEGA logo appears.

## Input

A standard controller in the first port:

| Dreamcast | Game |
|---|---|
| D-pad or analogue stick | directions |
| A, B, X, Y | A, B, C, Y |
| L and R triggers | L and R |
| Start | Start |
| L + R + Start | Select |

**Y opens the dev menu** when `DevMenu` is enabled in `settings.ini` on the
disc. It is off by default.

## Deliberately disabled

- **Networking**, and with it 2P VS: the menu entries are visible but do
  nothing. RSDKv4 has no split screen, so there is no local alternative.
- **The mod loader.** It cannot work from read-only media, and the stage sound
  effect classification depends on it staying off.

## Layout of the port

Everything Dreamcast-specific lives in `RSDKv4/Dreamcast/`:

- `DCGraphics.cpp` — the renderer core: PVR setup, the sheet texture cache, the
  sprite queue, the tileset uploader, list flushing, the fade overlay, the
  frozen frame the pause menu draws, and two of the save flush points.
- `DCNative.cpp` — the 2013 menu's model renderer, the `.dtex` loader and the
  texture eviction and re-streaming.
- `DCAudio.cpp` — music streaming, the shared WAV parser, the raw global sound
  effect loader, the AICA sound-RAM path, the fallback heap arena and the heap
  reporting.
- `DCSave.cpp` / `DCSaveIcon.h` — VMU saves.
- `DCInput.cpp` — the controller mapping.
- `DCSystem.cpp` — timing and the VMU screen icon.

The engine-side changes are mostly hooks guarded by `#if RETRO_USING_KOS`, in
`Drawing.cpp`, `Renderer.cpp`, `Scene.cpp`, `Scene3D.cpp`, `Audio.cpp`,
`Input.cpp`, `RetroEngine.cpp`, `Reader.cpp` / `Reader.hpp`, `Object.cpp`,
`Userdata.cpp` and `main.cpp`. `platforms/KallistiOS.cmake` is the platform
file; the root `CMakeLists.txt` is upstream's.

Outside the engine, `dreamcast/` holds the asset generation kit and the Colab
autobuilder, and `tools/dtex_convert.py` is the texture converter.

## Known limitations

- 2P VS does nothing, since networking is off.
- `Data.rsdk` must not be repacked.
- Special stages hold 60 fps but can spike briefly on frames where a pipe
  segment is rebuilt. Harmless.
- Over dcload, the music stream needs a broadband adapter; dcload-serial cannot
  keep up with it.

## Credits

RSDKv4-Decompilation by Rubberduckycooly and st×tic. Dreamcast techniques and
the overall approach follow SF94 / Michael Fadely's RSDKv5 Dreamcast port.
KallistiOS by Cryptic Allusion and contributors. `mkdcdisc` by Simulant.

*Sonic the Hedgehog 2* is © SEGA. This port ships no game data and requires a
copy you own.
