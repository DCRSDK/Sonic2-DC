#!/usr/bin/env python3
"""Build rsdkv4_filelist.txt for the Sonic 2 Dreamcast asset pipeline.

    make_filelist.py --data <path/to/Data> --engine <path/to/RSDKv4> [--out FILE]

WHY THIS EXISTS, AND WHY IT IS NOT A HAND-WRITTEN LIST

RSDKv4 packs store only an MD5 of each lowercased path, so names cannot be
enumerated out of Data.rsdk -- rsdkv4_extract.py has to be told what to look
for. Sonic 1's list was assembled from three sources; this reproduces the same
three for Sonic 2 so the list is derived rather than remembered:

  sound effects   from Data/Game/GameConfig.bin (global) and each
                  Data/Stages/<folder>/StageConfig.bin (stage). These are the
                  engine's own path strings, so they cannot drift from what
                  LoadSfx will ask for.

  music           from the Data/Music directory listing. RSDKv4 names tracks
                  only in script bytecode (SetMusicTrack's first argument), so
                  there is nothing in the configs to read -- an extracted tree
                  is the authority.

  menu textures   from the LoadTexture() string literals in
                  RSDKv4/NativeObjects. Those are the ONLY paths
                  DC_NativeTryLoadDtex will ever fopen, so harvesting them
                  avoids converting retail art the engine never references
                  (which on Sonic 1 was ~1.5 MB of dead disc space).

Anything the extractor cannot find is reported by rsdkv4_extract.py, so a
missing line here fails loudly at extraction rather than quietly at run time.
"""
import argparse
import os
import re
import sys


# ---------------------------------------------------------------- config files

class Reader:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def skip(self, n):
        self.p += n

    def string(self):
        n = self.u8()
        s = self.d[self.p:self.p + n].decode('latin1')
        self.p += n
        return s


def read_gameconfig(path):
    """-> (global sfx paths, [(category, folder)]). Mirrors LoadGameConfig."""
    r = Reader(open(path, 'rb').read())
    r.string()                       # window text
    r.string()                       # description
    r.skip(0x60 * 3)                 # palette

    objects = r.u8()
    for _ in range(objects):
        r.string()                   # object names
    for _ in range(objects):
        r.string()                   # script paths

    variables = r.u8()
    for _ in range(variables):
        r.string()
        r.skip(4)

    sfx_count = r.u8()
    for _ in range(sfx_count):
        r.string()                   # sfx names
    sfx_paths = [r.string() for _ in range(sfx_count)]

    players = r.u8()
    for _ in range(players):
        r.string()

    folders = []
    for c in range(4):
        # Special stages are category 2 in the file but 3 in the engine; the
        # distinction does not matter for a folder list.
        count = r.u8()
        for _ in range(count):
            folder = r.string()
            r.string()               # id
            r.string()               # name
            r.u8()                   # highlighted
            folders.append((c, folder))
    return sfx_paths, folders


def read_stageconfig(path):
    """-> stage sfx paths. Mirrors the StageConfig half of LoadStageFiles."""
    r = Reader(open(path, 'rb').read())
    r.u8()                           # loadGlobals
    r.skip(0x20 * 3)                 # palette rows 0x60..0x80
    n = r.u8()
    for _ in range(n):
        r.string()                   # sfx names
    return [r.string() for _ in range(n)]


# ---------------------------------------------------------------- engine source

TEX_RE = re.compile(r'LoadTexture\s*\(\s*"([^"]+)"')
# Any Data/ image path literal, wherever it appears.
#
# THIS SECOND PATTERN IS NOT REDUNDANT. RecordsScreen.cpp:67 does
#     sprintf(pathBuf, "Data/Game/Menu/TimeAttack%d.png", textureID);
#     ... LoadTexture(pathBuf, TEXFMT_RGBA5551);
# so the path never appears as a LoadTexture argument and TEX_RE cannot see it.
# Harvesting on LoadTexture alone silently dropped TimeAttack1-4 -- four 512x512
# textures, the whole Time Attack records panel -- which was only caught by
# diffing this list against a disc built by hand. Matching the literal itself,
# whatever call consumes it, is the robust form.
# .png only, deliberately: DC_NativeTryLoadDtex is reached for .png paths, so a
# .png is the only thing the menu step converts. Widening this to .gif pulled in
# Data/Game/SystemText.gif, which the engine reads out of Data.rsdk through
# LoadFile and never wants loose.
PATH_RE = re.compile(r'"(Data/[^"]*\.png)"', re.I)
FMT_RE = re.compile(r'%0?\d*d')


def harvest_textures(engine_dir):
    """Data/ .png path literals under RSDKv4/, with %d expanded 1..4."""
    found = set()
    for dirpath, _dirs, files in os.walk(engine_dir):
        for fn in files:
            if not fn.endswith(('.cpp', '.hpp')):
                continue
            try:
                text = open(os.path.join(dirpath, fn), errors='ignore').read()
            except OSError:
                continue
            for lit in list(TEX_RE.findall(text)) + list(PATH_RE.findall(text)):
                # TEX_RE can return a non-image (nothing does today, but it
                # takes any first argument), so filter on extension here.
                if not lit.lower().endswith('.png'):
                    continue
                if FMT_RE.search(lit):
                    # Built at run time. 1..4 covers the TimeAttack art set,
                    # which is the only %d path in the tree.
                    for i in range(1, 5):
                        found.add(FMT_RE.sub(str(i), lit, count=1))
                else:
                    found.add(lit)
    return found


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument('--data', required=True, help='extracted Data/ directory')
    ap.add_argument('--engine', required=True, help='RSDKv4/ source directory')
    ap.add_argument('--out', default='-', help='output file, or - for stdout')
    a = ap.parse_args()

    data = os.path.abspath(a.data)
    if os.path.isdir(os.path.join(data, 'Data')):
        data = os.path.join(data, 'Data')

    gc = os.path.join(data, 'Game', 'GameConfig.bin')
    if not os.path.isfile(gc):
        sys.exit('no Game/GameConfig.bin under %s' % data)

    global_sfx, folders = read_gameconfig(gc)

    stage_sfx = []
    seen_folders = set()
    for _cat, folder in folders:
        if folder in seen_folders:
            continue
        seen_folders.add(folder)
        sc = os.path.join(data, 'Stages', folder, 'StageConfig.bin')
        if os.path.isfile(sc):
            try:
                stage_sfx.extend(read_stageconfig(sc))
            except (IndexError, UnicodeDecodeError):
                print('warn: could not parse %s' % sc, file=sys.stderr)

    music_dir = os.path.join(data, 'Music')
    music = sorted(f for f in os.listdir(music_dir)) if os.path.isdir(music_dir) else []
    if not music:
        print('warn: no Data/Music found - the music list will be empty, and music '
              'is the one class that cannot be recovered from the configs',
              file=sys.stderr)

    textures = harvest_textures(os.path.abspath(a.engine))

    out = sys.stdout if a.out == '-' else open(a.out, 'w')

    def w(line=''):
        print(line, file=out)

    w('# rsdkv4_filelist.txt - Sonic 2')
    w('#')
    w('# Generated by make_filelist.py. RSDKv4 packs store only an MD5 of each')
    w('# lowercased path, so rsdkv4_extract.py has to be told what to look for.')
    w('# Regenerate rather than hand-edit:')
    w('#   ./make_filelist.py --data <Data> --engine <RSDKv4> --out rsdkv4_filelist.txt')
    w()
    w('Data/Game/GameConfig.bin')
    w()
    w('# --- music (from the Data/Music listing; RSDKv4 names tracks only in')
    w('#     script bytecode, so there is nothing in the configs to read)')
    for f in music:
        w('Data/Music/%s' % f)
    w()
    w('# --- global sfx (GameConfig.bin sfx paths, relative to Data/SoundFX)')
    for p in dict.fromkeys(global_sfx):
        w('Data/SoundFX/%s' % p.replace('\\', '/'))
    w()
    w('# --- stage sfx (each StageConfig.bin). NOT staged loose - LoadSfx gates')
    w('#     the loose path on the sfx id being outside the stage range, so these')
    w('#     always come out of Data.rsdk. Extracted only so verify_assets.sh can')
    w('#     size-check against the originals.')
    for p in dict.fromkeys(stage_sfx):
        w('Data/SoundFX/%s' % p.replace('\\', '/'))
    w()
    w('# --- 2013 native menu (LoadTexture() literals in RSDKv4/NativeObjects -')
    w('#     the only paths DC_NativeTryLoadDtex will ever fopen)')
    for p in sorted(textures):
        w(p if p.startswith('Data/') else 'Data/%s' % p.lstrip('/'))

    if out is not sys.stdout:
        out.close()

    print('music %d, global sfx %d, stage sfx %d, menu textures %d'
          % (len(music), len(set(global_sfx)), len(set(stage_sfx)), len(textures)),
          file=sys.stderr)
    if not textures:
        print('warn: no LoadTexture() literals found - check --engine points at RSDKv4/',
              file=sys.stderr)


if __name__ == '__main__':
    main()
