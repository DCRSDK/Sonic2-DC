# **Sonic the Hedgehog 2 (2013) for SEGA Dreamcast**
A Sonic 2/RSDKv4 port for SEGA Dreamcast based on the RSDKv4 Decompilation, using the mobile version as base.

<!-- Placeholder: replace this URL with the Sonic 2 header image. -->
![Sonic 2 header](https://example.com/sonic2-header.png)

+ Requires ffmpeg and Python 3 (with Pillow and NumPy) for asset generation, as well as an existing KallistiOS and mkdcdisc install for compiling the .elf and building the CDI.
+ Compatible with GCC 14.4.1 / KOS 2.3.0, or GCC 14.3.0 / KOS 2.2.2. Build with anything else at your own risk.
+ Build in Linux or MacOS environments, or Windows via DreamSDK or WSL (Debian or Ubuntu based preferably)
+ Build from your own KallistiOS setup by cloning this git and following the instructions in BUILD_DREAMCAST.md or use the autobuilder [HERE](dreamcast/Sonic2_Dreamcast_Autobuilder.ipynb)
+ Bring your own Sonic 2 Data.rsdk (MUST be the 2013 mobile remaster; Origins is untested and presumed unsupported).

NO ASSETS ARE PROVIDED IN THIS RELEASE.

## Asset generation

Music, global sound effects and the 2013 menu textures have to be converted
before the Dreamcast can use them. Music and global sound effects play as raw
PCM rather than being decoded from Vorbis on the SH4, and the menu art becomes
VQ-compressed `.dtex` textures that stream straight into VRAM, since most of it
is too large for the console to decode as PNG. Everything else, stage sound
effects included, streams out of Data.rsdk untouched.

```sh
cd dreamcast
./generate_assets.sh /path/to/Data.rsdk [stage_dir]
```

`stage_dir` defaults to `../build-dc`. Any `tools/<Name>.dtex` is copied over
the converted `<Name>.png`, which is how this port's custom menu art reaches the
disc. See `dreamcast/README.md` for the format contract and the verifier — a
headerless file carries no sample rate, so a wrong conversion is a silent pitch
shift rather than an error.

## Running it

**From a disc or GDEMU** — the default build. Package with mkdcdisc as
described in BUILD_DREAMCAST.md, or let the autobuilder do it.

**Over dcload-ip** — configure with `-DDC_DATA_ROOT=/pc/` in its own build
directory (`build-pc`) and serve its contents as `/pc/`, which is dc-tool's
working directory, so the loose `Data/` tree has to sit beside `Data.rsdk`
there. Use a broadband adapter: dcload-serial cannot keep up with the music
stream. You can also boot over dcload while reading `/cd/` from a disc in the
drive, which keeps the console output without putting game data on the link.

**What Works:**
+ Sonic 2 full game, every zone including Hidden Palace, plus the Special Stages — locked 60fps on real hardware
+ Sonic, Tails and Knuckles
+ The 2013 remaster menus, Time Attack and the ending sequence
+ Saves, achievements and Time Attack records on VMU, including a custom icon in the Dreamcast BIOS save menu and a winged "2" on the VMU's screen
+ Music and global sound effects streamed as raw PCM (no Vorbis decoding on-console), with stage sound effects held in the AICA's own sound RAM

**What doesn't work:**
+ 2P VS — the menu entries are there but do nothing. It needs networking, and RSDKv4 has no split screen
+ Anything else needing networking — disabled
+ Mod loader — disabled
+ Leaderboard uploads (local scores are kept, there is nowhere to send them)
+ Anything else I may have missed or overlooked in testing (you tell me)

Dev menu is NOT enabled by default. You can access the Dev menu by placing a settings.ini with it toggled to ON from the PC decompilation (delete all sections other than the topmost with the Dev menu flag) at the root of the disc when you go to build your CDI — the autobuilder takes one as an optional upload alongside your Data.rsdk. It is accessed by pressing the Y button. When selecting stages in the Dev menu press Start button for regular play, or the A button for debug mode

**Special Thanks**
+ Rubberduckycooly & everyone involved with the RSDKv4 decompilation - without that none of this would have been possible
+ SF94 & everyone involved with the Sonic Mania Dreamcast port - this port was largely inspired by it, you guys rock!
+ Everyone who helped me behind the scenes - you're all the best!

**Notice regarding AI use:**

This was almost entirely created using Claude AI. I do not consider myself a coder, more a project director. If you have any views regarding AI use that would otherwise prevent you from trying this port out, that's entirely understandable, enjoy your life as you see fit.

Sonic the Hedgehog 2 (C) SEGA

Original RSDK author: Christian "Taxman" Whitehead

Decompilation authors: Rubberduckycooly and st×tic

# **SUPPORT THE OFFICIAL RELEASE**
