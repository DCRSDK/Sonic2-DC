#!/usr/bin/env bash
# ---------------------------------------------------------------------
# Sonic 2 / RSDKv4 — Dreamcast asset conversion
#
#   ./generate_assets.sh <path/to/Data.rsdk> [stage_dir] [--menu-no-vq] [--skip-menu]
#
# Takes the game's own datafile, pulls out the three asset classes the Dreamcast
# build reads loose, converts them, and leaves everything else to stream out of
# the untouched Data.rsdk.
#
# WHAT COMES OUT, AND WHERE
#
#   <stage_dir>/Data/Music/*.ogg           headerless signed 8-bit, STEREO, 22050
#   <stage_dir>/Data/SoundFX/Global/*.ogg  headerless signed 8-bit, MONO,   22050
#   <stage_dir>/Data/Game/Menu/*.png       VQ .dtex, streamed straight to VRAM
#   <stage_dir>/Data/Game/Models/*.png     ditto
#
# Every one keeps its original extension. The audio does because GameConfig.bin
# resolves assets by filename; the textures do because DC_NativeTryLoadDtex
# identifies the format by its 'DTEX' magic, not the extension. Only the
# CONTENTS change -- none of these is a container or a PNG any more. The names
# are a lie the loaders are in on.
#
# Data.rsdk itself is never modified. The disc needs BOTH: the loose Data/ tree
# built here, and Data.rsdk beside it for everything else (sprites, tilesets,
# scripts, stage sfx).
#
# THE ONE DIFFERENCE FROM THE SONIC 1 KIT THAT WILL BITE
#
#   Sonic 1's global sfx are .wav. SONIC 2's ARE .ogg.
#
# Both hold the same headerless s8 mono PCM afterwards, and both keep their
# original name. But copying Sonic 1's '*.wav' glob across would match nothing
# under Sonic 2's Data/SoundFX/Global and convert nothing at all, silently.
# Music is STEREO in both (and MONO in Sonic CD -- do not copy that one either).
#
# SOURCE IS THE PACK, NOT A PRE-EXTRACTED TREE
#
# Extraction is built in (rsdkv4_extract.py), so nothing has to be unpacked by
# hand. That also removes the trap a loose-tree flow has: a tree already
# converted once looks exactly like a pristine one, and re-running feeds 8-bit
# headerless PCM back into ffmpeg. Extracting fresh every run means the input is
# pristine by construction.
#
# RSDKv4 stores only an MD5 of each lowercased path, so names cannot be
# enumerated -- they have to be known in advance. rsdkv4_filelist.txt is that
# list; make_filelist.py derives it from GameConfig.bin, the StageConfigs, the
# Data/Music listing and the LoadTexture() literals in RSDKv4/NativeObjects.
#
# A loose tree is still accepted as the first argument, for anyone who already
# has one extracted.
#
# REQUIREMENTS: bash, python3, ffmpeg, ffprobe, and for the menu textures
# python3-pil plus python3-numpy (numpy only for VQ). --skip-menu drops both.
# ---------------------------------------------------------------------
set -euo pipefail
IFS=$'\n\t'

die() { printf 'error: %s\n' "$*" >&2; exit 1; }
usage() {
  printf 'Usage: %s <path/to/Data.rsdk> [stage_dir] [--menu-no-vq] [--skip-menu]\n' \
    "${0##*/}" >&2
  exit 2
}

src_in=""
stage_arg=""
menu_vq=1
skip_menu=0

for arg in "$@"; do
  case "$arg" in
    --menu-no-vq) menu_vq=0 ;;
    --skip-menu)  skip_menu=1 ;;
    -*)           usage ;;
    *)            if   [[ -z "$src_in"    ]]; then src_in=$arg
                  elif [[ -z "$stage_arg" ]]; then stage_arg=$arg
                  else usage; fi ;;
  esac
done

[[ -n "$src_in" ]] || usage
[[ -e "$src_in" ]] || die "no such file or directory: $src_in"

for tool in ffmpeg ffprobe; do
  command -v "$tool" >/dev/null 2>&1 || die "$tool not found in PATH"
done

script_dir=$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)

# ---- resolve the input --------------------------------------------------
# Three things are accepted, in this order: the pack itself, a directory holding
# it, or an already-extracted loose tree.
pack=
loose=
if [[ -f "$src_in" ]]; then
  pack=$(cd -P -- "$(dirname -- "$src_in")" && pwd -P)/$(basename -- "$src_in")
elif [[ -d "$src_in" ]]; then
  d=$(cd -P -- "$src_in" && pwd -P)
  for cand in "$d/Data.rsdk" "$d/data.rsdk"; do
    [[ -f "$cand" ]] && { pack=$cand; break; }
  done
  if [[ -z "$pack" ]]; then
    [[ -d "$d/Music" && -d "$d/SoundFX/Global" ]] \
      || die "$d holds neither Data.rsdk nor a Music/ + SoundFX/Global/ tree"
    loose=$d
  fi
fi

work=
# `return 0` is load bearing. An EXIT trap's final command status replaces the
# status passed to `exit`, so with $work empty -- which is every run that takes a
# pre-extracted tree instead of the pack -- the [[ ]] && rm chain evaluates false,
# the trap returns 1, and the script exits 1 after printing "verify: OK".
# Anything reading the status (CI, a notebook cell under set -e, an && chain)
# then treats a clean run as a failure.
cleanup() { [[ -n "$work" && -d "$work" ]] && rm -rf -- "$work"; return 0; }
trap cleanup EXIT

if [[ -n "$pack" ]]; then
  command -v python3 >/dev/null 2>&1 || die "python3 not found in PATH (needed to read Data.rsdk)"
  [[ -f "$script_dir/rsdkv4_extract.py" ]] || die \
    "missing $script_dir/rsdkv4_extract.py — copy it from the Sonic 1 kit; it is game-agnostic"
  [[ -f "$script_dir/rsdkv4_filelist.txt" ]] || die \
    "missing $script_dir/rsdkv4_filelist.txt — generate it first:
    ./make_filelist.py --data <extracted Data> --engine ../RSDKv4 --out rsdkv4_filelist.txt
  RSDKv4 packs store only an MD5 of each path, so the extractor cannot
  enumerate names and has to be handed the list."

  work=$(mktemp -d)
  printf -- '-- extracting from %s\n' "$pack"
  # -I so the extractor cannot pick up a stray module from the data directory.
  python3 -I "$script_dir/rsdkv4_extract.py" "$pack" "$work/extracted"
  src=$work/extracted
else
  printf -- '-- using pre-extracted tree %s\n' "$loose"
  src=$loose
fi

[[ -d "$src/Music"          ]] || die "no Music/ came out of the source"
[[ -d "$src/SoundFX/Global" ]] || die "no SoundFX/Global/ came out of the source"

# Default the stage to build-dc, which is where the loose Data/ tree has to end
# up next to Data.rsdk anyway.
stage_in=${stage_arg:-$script_dir/../build-dc}
mkdir -p -- "$stage_in"
stage=$(cd -P -- "$stage_in" && pwd -P)

printf -- '-- staging into %s/Data\n' "$stage"
mkdir -p -- "$stage/Data/SoundFX"
rm -rf -- "$stage/Data/Music" "$stage/Data/SoundFX/Global"
cp -R -- "$src/Music"          "$stage/Data/"
cp -R -- "$src/SoundFX/Global" "$stage/Data/SoundFX/"

# Only the PNGs out of Game/, never the whole folder. The disc carries exactly
# the converted textures under Data/Game and nothing else: a loose
# Game/GameConfig.bin beside Data.rsdk would override the packed one on the Drop
# Dash tree, which lists it in Reader.cpp's loose allowlist. Quiet way to ship
# the wrong config.
if (( ! skip_menu )); then
  rm -rf -- "$stage/Data/Game/Menu" "$stage/Data/Game/Models"
  for sub in Menu Models; do
    if [[ -d "$src/Game/$sub" ]]; then
      mkdir -p -- "$stage/Data/Game/$sub"
      find "$src/Game/$sub" -maxdepth 1 -type f -iname '*.png' \
        -exec cp -- {} "$stage/Data/Game/$sub/" \;
    fi
  done
fi

printf -- '-- converting music\n'
"$script_dir/music_step_1_ogg_to_pcm.sh"    "$stage/Data"
"$script_dir/music_step_2_rename_to_ogg.sh" "$stage/Data"

printf -- '-- converting global sound effects\n'
"$script_dir/sfx_step_1_global_to_raw.sh"   "$stage/Data"

if (( skip_menu )); then
  printf -- '-- 2013 menu textures skipped\n'
elif [[ ! -d "$stage/Data/Game/Menu" ]]; then
  printf -- '-- no Game/Menu came out of the source, nothing to convert\n'
else
  printf -- '-- converting 2013 menu textures\n'
  if (( menu_vq )); then
    "$script_dir/menu_step_1_dtex.sh" "$stage/Data"
  else
    "$script_dir/menu_step_1_dtex.sh" "$stage/Data" --no-vq
  fi

  # Custom art: tools/<Name>.dtex is copied over the converted <Name>.png.
  # The shipped disc's Generic.png is tools/Generic.dtex, hand-made, so without
  # this every rebuild quietly put the retail one back. Done before the verify
  # step, so verify_assets.sh checks it like any other texture.
  for ov in "$script_dir/../tools"/*.dtex; do
    [[ -f "$ov" ]] || continue
    base=$(basename -- "$ov" .dtex)
    for sub in Menu Models; do
      if [[ -f "$stage/Data/Game/$sub/$base.png" ]]; then
        cp -- "$ov" "$stage/Data/Game/$sub/$base.png"
        printf 'menu: custom art tools/%s.dtex -> Game/%s/%s.png\n' "$base" "$sub" "$base"
      fi
    done
  done
fi

# The source tree is what makes the size check possible -- bytes must equal
# rate * channels * source duration -- so verify before the temp dir goes.
printf -- '-- verifying\n'
"$script_dir/verify_assets.sh"              "$stage/Data" "$src"

printf -- '-- DONE: %s/Data\n' "$stage"
if [[ -n "$pack" ]]; then
  printf -- '   remember Data.rsdk still has to sit beside it on the disc.\n'
fi
exit 0
