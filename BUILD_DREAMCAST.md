# Building Sonic the Hedgehog 2 (2013) for SEGA Dreamcast

There are two ways to run the result: a **self-booting CDI** for a GDEMU, an
emulator or a burned disc, or **over dcload** with the game data served from
your computer. Both start from the same ELF build and the same asset kit.

If you would rather not set any of this up, the Colab autobuilder in
`dreamcast/Sonic2_Dreamcast_Autobuilder.ipynb` does the whole CDI route in the
cloud. You upload your `Data.rsdk`, and optionally a `settings.ini`, and it
hands back a CDI.

For how the port itself works, see [README_DREAMCAST.md](README_DREAMCAST.md).

## What you need

- **A working KallistiOS environment.** Tested with GCC 14.4.1 / KOS 2.3.0
  (master) and GCC 14.3.0 / KOS 2.2.2. `kos-cc` and `kos-cmake` must be on
  `PATH` once `environ.sh` is sourced. **No kos-ports packages are needed** —
  the build decodes no Vorbis, so libtremor and libogg are not linked.
- **[mkdcdisc](https://gitlab.com/simulant/mkdcdisc)**, for the CDI route.
- **dcload-ip** on the console and **dc-tool-ip** on your computer, for the
  dcload route, plus a broadband adapter.
- **bash, ffmpeg (with ffprobe), and Python 3 with Pillow and NumPy**, for the
  asset kit.
- **Your own `Data.rsdk`** from the 2013 mobile remaster of Sonic 2. None of the
  game's data is included.
- Optionally Flycast or Redream for testing.

Linux and macOS work directly; on Windows use DreamSDK or WSL (Debian or Ubuntu
based preferably).

## 1. Build the ELF

```sh
source /opt/toolchains/dc/kos/environ.sh   # if not already in your .bashrc

cd RSDKv4-Decompilation
mkdir build-dc && cd build-dc
kos-cmake -DPLATFORM=KallistiOS -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

That produces `build-dc/RSDKv4.elf`. The configure step prints the data root
it was set up for — `RSDKv4/DC: game data root is /cd/` for a disc build.

After editing only `.cpp` files a plain `make` is enough. **Anything that
touches a header, `platforms/KallistiOS.cmake` or the renderer needs
`make clean && make`**, or you can end up running a binary that is half old.

Two build options exist:

- `-DRSDK_DEBUG=ON` turns on debug logging and the dev menu.
- `-DDC_NO_AUDIO=ON` skips audio initialisation entirely. It is a bisection
  aid, not something to ship.

## 2. Generate the assets

Music, global sound effects and the 2013 menu textures have to be converted
before the console can use them. The kit in `dreamcast/` reads them straight
out of your `Data.rsdk` and never modifies it:

```sh
cd dreamcast
./generate_assets.sh /path/to/Data.rsdk
```

This writes a `Data/` tree into `../build-dc`, which is where the ELF already
is. Give a second argument to write it elsewhere. `--skip-menu` leaves the menu
textures out, and `--menu-no-vq` writes uncompressed textures instead of VQ;
neither is needed normally.

Any `tools/<Name>.dtex` is copied over the converted `<Name>.png`, so custom
menu art survives a rebuild. The run finishes with `verify_assets.sh`, which
checks every converted file against its source and must end in `verify: OK`.
See `dreamcast/README.md` for the details of each format.

`Data.rsdk` itself is not copied, so put it beside the new tree:

```sh
cp /path/to/Data.rsdk ../build-dc/
```

`build-dc` then holds everything a disc needs:

```
build-dc/
├── RSDKv4.elf
├── Data.rsdk          unmodified
├── settings.ini       optional
└── Data/
    ├── Game/Menu/     menu textures, .dtex under .png names
    ├── Game/Models/   model textures, likewise
    ├── Music/         raw PCM under .ogg names
    └── SoundFX/Global/ raw PCM under .ogg names
```

## 3a. Build a CDI

From `build-dc`:

```sh
mkdcdisc -e RSDKv4.elf -d Data -f Data.rsdk -N -n SONIC2 -o Sonic2.cdi
```

- `-d Data` puts the converted tree on the disc. Leave it out and the game
  boots with no music and an incomplete menu.
- Add `-f settings.ini` to ship a settings file — for example with `DevMenu`
  turned on, so Y opens the dev menu. Without one the engine uses its
  defaults.
- `-N` turns padding off. A padded image grows far past the size of a CD-R; an
  unpadded one is around 160 MB.
- mkdcdisc derives the disc's serial from a hash of the ELF, so every build gets
  a new one. Pass `-s <serial>` to keep it fixed if you use a save device that
  stores saves per game, such as a VM2.

Test in Flycast or Redream, then copy the CDI to your GDEMU's SD card or burn
it.

## 3b. Run over dcload

For this the game reads its data from your computer instead of the disc, so it
needs an ELF built for that, in a **separate** build directory:

```sh
cd RSDKv4-Decompilation
mkdir build-pc && cd build-pc
kos-cmake -DPLATFORM=KallistiOS -DCMAKE_BUILD_TYPE=Release -DDC_DATA_ROOT=/pc/ ..
make -j$(nproc)
```

Configure should print `RSDKv4/DC: game data root is /pc/`. Keep this separate
from `build-dc`: `DC_DATA_ROOT` is cached, so once a build directory has been
configured for `/pc/` it stays that way even if you reconfigure without the
flag.

Generate the assets into the same directory and copy in `Data.rsdk`:

```sh
cd ../dreamcast
./generate_assets.sh /path/to/Data.rsdk ../build-pc
cp /path/to/Data.rsdk ../build-pc/
```

Then run it **from that directory**, because dc-tool's working directory is
what the console sees as `/pc/`:

```sh
cd ../build-pc
dc-tool-ip -t <dreamcast-ip> -x RSDKv4.elf
```

Console output appears in the terminal running dc-tool.

Two things to know:

- **Use a broadband adapter.** dcload-serial cannot sustain the music stream,
  which is 44,100 bytes a second of raw PCM.
- **`/pc/` is case-sensitive; the disc is not.** The kit names files exactly as
  the engine asks for them, so its output works over dcload. A `Data/` tree
  assembled some other way can work on disc and still miss files over `/pc/`
  if a name's case differs.

You can also send an ordinary disc build over dcload while reading `/cd/` from a
disc in the drive. That keeps the console output without putting game data on
the link.

## Troubleshooting

- **`KOS environment not found`** — source `environ.sh` first, and configure
  with `kos-cmake`, not plain `cmake`.
- **The game can't find its data** — check the root printed at configure time.
  A `build-dc` that was ever configured with `/pc/` stays `/pc/`; wipe the
  directory and configure again.
- **Strange behaviour right after editing a header** — `make clean && make`.
- **Silent music or sound effects, or invisible menu pieces** — re-run
  `generate_assets.sh` and read its `verify` output. Check that `Data/` made it
  onto the disc (`-d Data`) or into the dcload directory.
