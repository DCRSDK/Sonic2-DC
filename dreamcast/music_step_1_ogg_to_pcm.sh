#!/usr/bin/env bash
# Sonic 2 / RSDKv4 Dreamcast — music.
#
# libtremor and libogg are not linked into the Dreamcast build, so nothing on the
# disc may still be a container. Music becomes HEADERLESS SIGNED 8-BIT PCM,
# STEREO, 22050 Hz, streamed straight off the GD-ROM by DC_MusReaderThread.
#
# STEREO. Not a preference — DC_MusFramesToBytes multiplies by two bytes per
# frame and the ring buffer is documented as "22050Hz stereo s8". Converted mono,
# every track plays at double speed with its loop point in the wrong place. This
# is the same value Sonic 1 uses and the opposite of Sonic CD's.
#
# 22050 against a 44100 device: DC_MusicReadSamples up-converts to the mixer's
# 44100 stereo S16, so the lower source rate costs nothing at the speaker and
# halves the bandwidth on a drive that has very little to spare.
#
# LOOP POINTS ARE NOT TOUCHED AND MUST NOT BE. The value in GameConfig.bin is a
# PCM frame index into the ORIGINAL 44100 Hz Vorbis, and DC_MusFramesToBytes
# rescales it at run time (DC_MUS_OGG_RATE 44100 -> DC_MUS_SRC_RATE 22050). If
# you have a table of loop points "recalculated for 22050", do NOT put those in
# GameConfig — the engine would rescale them a second time and every track would
# loop at half its intended position.
set -euo pipefail
IFS=$'\n\t'

die() { printf 'error: %s\n' "$*" >&2; exit 1; }

MUS_RATE=22050
MUS_CH=2

[[ $# -eq 1 ]] || die "Usage: ${0##*/} <stage_data_dir>"
stage=$1
[[ -d "$stage" ]] || die "not a directory: $stage"
[[ -d "$stage/Music" ]] || die "missing directory: $stage/Music"

command -v ffmpeg >/dev/null 2>&1 || die "ffmpeg not found in PATH"

orig_dir=$(pwd -P)
trap 'cd -- "$orig_dir" || true' EXIT
stage_abs=$(cd -P -- "$stage" && pwd -P)
cd -- "$stage_abs/Music"

n=0; skipped=0
while IFS= read -r -d '' in_file; do
  # Only real containers get converted. Binary-safe magic check: head in a
  # command substitution drops NUL bytes, so compare hex instead. Anything
  # already headerless is left alone, so a re-run is a no-op rather than ffmpeg
  # failing with "Invalid data found when processing input".
  if [[ "$(od -An -N4 -tx1 -- "$in_file" | tr -d ' \n')" != "4f676753" ]]; then
    skipped=$((skipped+1)); continue
  fi
  out_file="${in_file%.*}.s8"
  ffmpeg -nostdin -hide_banner -loglevel error -y -i "$in_file" \
    -ar "$MUS_RATE" -ac "$MUS_CH" -f s8 -- "$out_file"
  rm -f -- "$in_file"
  n=$((n+1))
done < <(find . -type f -iname '*.ogg' -print0)

printf 'music: %d tracks -> headerless s8 STEREO %d' "$n" "$MUS_RATE"
(( skipped )) && printf ', %d already raw and left alone' "$skipped"
printf '\n'
