#!/usr/bin/env bash
# Sonic 2 / RSDKv4 Dreamcast — GLOBAL sound effects.
#
# Sonic 2 ships its global sounds as Vorbis under Data/SoundFX/Global, with a
# .ogg extension. They become HEADERLESS SIGNED 8-BIT PCM, MONO, 22050 Hz — the
# format DC_LoadRawSfx expects (DC_RAWSFX_RATE 22050, DC_RAWSFX_CHANNELS 1).
#
# *** THE EXTENSION DIFFERS FROM SONIC 1. ***  Sonic 1's global set is .wav;
# Sonic 2's is .ogg. Both end up holding the same headerless s8 mono PCM, and
# both keep their original name because GameConfig resolves by filename. Copying
# Sonic 1's '*.wav' glob over would match nothing here and silently convert
# nothing at all.
#
# MONO here, STEREO for music: different engine paths. Music goes through the
# streaming reader; global sfx are one mono voice each on the software mixer.
#
# -f s8 matters. Without it ffmpeg writes a real RIFF wav with the right name,
# and DC_LoadRawSfx has an explicit RIFF guard — it refuses the file and falls
# back to the packed Vorbis, which the DC cannot decode. Silent, and verify
# catches it.
#
# GLOBAL ONLY, deliberately. LoadSfx gates the loose path on the sfx ID being
# outside the stage range, so a stage sfx never reaches DC_LoadRawSfx: it goes
# through LoadFile, out of Data.rsdk, into AICA sound RAM. Staging
# Data/SoundFX/Stage/ loose does nothing at all.
#
# WHAT THIS SET COSTS, because it is the biggest single heap consumer in the
# system: the software mixer widens each 8-bit sample to 16-bit, so a sound
# occupies TWICE its file size, malloc'd at boot and never freed. On the stock
# set that is 1,977,500 bytes resident — confirmed against the [DCSfx] raw: lines
# of a hardware log — out of a heap with about 2.9 MB free at audio init. Half of
# it is padding an 8-bit source gains nothing from.
set -euo pipefail
IFS=$'\n\t'

die() { printf 'error: %s\n' "$*" >&2; exit 1; }

SFX_RATE=22050
SFX_CH=1

[[ $# -eq 1 ]] || die "Usage: ${0##*/} <stage_data_dir>"
stage=$1
[[ -d "$stage" ]] || die "not a directory: $stage"
[[ -d "$stage/SoundFX/Global" ]] || die "missing directory: $stage/SoundFX/Global"

command -v ffmpeg >/dev/null 2>&1 || die "ffmpeg not found in PATH"

orig_dir=$(pwd -P)
trap 'cd -- "$orig_dir" || true' EXIT
stage_abs=$(cd -P -- "$stage" && pwd -P)
cd -- "$stage_abs/SoundFX/Global"

n=0; skipped=0
while IFS= read -r -d '' in_file; do
  if [[ "$(od -An -N4 -tx1 -- "$in_file" | tr -d ' \n')" != "4f676753" ]]; then
    skipped=$((skipped+1)); continue
  fi
  tmp="${in_file%.*}.s8"
  ffmpeg -nostdin -hide_banner -loglevel error -y -i "$in_file" \
    -ar "$SFX_RATE" -ac "$SFX_CH" -f s8 -- "$tmp"
  mv -f -- "$tmp" "$in_file"
  n=$((n+1))
done < <(find . -type f -iname '*.ogg' -print0)

printf 'global sfx: %d -> headerless s8 mono %d' "$n" "$SFX_RATE"
(( skipped )) && printf ', %d already raw and left alone' "$skipped"
printf '\n'
