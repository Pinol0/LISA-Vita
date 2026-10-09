#!/usr/bin/env python3
"""Checks the screens saved by MKXP_VITA_SKY_SNAP (sky_mapNNN_fF.png + qa.log SKY_SNAP lines) against
what RGSS draws for the sky: the map's parallax tiled from (ox, oy), with the screen tone's gray part
(luma mix, as RGSS / mkxp-z GrayShader). Only the top third of the screen is compared, and only where
every map layer has a fully transparent tile (map data read through tools/data-analysis/map_layers.rb,
tile alpha from the tileset images; autotiles count as opaque).

  python3 tools/sky_check.py BUILD_DIR GAME_DIR      (GAME_DIR: Data/ and Graphics/ inside; needs ruby)

One line per screen: PASS / FAIL (share of sky pixels within 4 of the expected colour, >= 85% passes),
or SKIP (no parallax, dark screen of a fade, RGB tone, sky covered by tiles). Exit 1 if any FAIL.
d105 (bug: a new parallax kept the old 32x32 tiling) FAILs maps 27, 36, 37, 38 (ox=0 after a parallax change);
a fixed build (MKXP_VITA_PLANE_BITMAP) must PASS them.
"""
import json
import os
import re
import subprocess
import sys

from PIL import Image

LINE = re.compile(r'SKY_SNAP map=(\d+) f=(\d+) file=(\S+) saved=true parallax="([^"]*)" plane=ox=(-?\d+) oy=(-?\d+) .*?'
                  r'screen_tone=\((-?\d+),(-?\d+),(-?\d+),(-?\d+)\).*? display=(-?[\d.]+),(-?[\d.]+)')
HERE = os.path.dirname(os.path.abspath(__file__))


def load_maps(game, ids):
    out = subprocess.run(['ruby', os.path.join(HERE, 'data-analysis', 'map_layers.rb')] + [str(i) for i in ids],
                         env=dict(os.environ, RGSS_DATA=os.path.join(game, 'Data', '')), capture_output=True,
                         text=True, check=True).stdout
    return {int(k): v for k, v in json.loads(out).items()}


class TileAlpha:
    """Does a tile id have any visible pixel? A5 (1536-1663) and B-E (1-1023) from the images; 0 and
    the first B tile are empty; autotiles (A1-A4, 2048+) are treated as opaque."""
    def __init__(self, game, names):
        self.imgs = {}
        for i, n in enumerate(names):
            if not n:
                continue
            for ext in ('.png', '.jpg'):
                p = os.path.join(game, 'Graphics', 'Tilesets', n + ext)
                if os.path.exists(p):
                    self.imgs[i] = Image.open(p).convert('RGBA')
                    break
        self.memo = {}

    def opaque(self, tid):
        if tid in self.memo:
            return self.memo[tid]
        if tid <= 0:
            r = False
        elif 1536 <= tid < 1664:
            r = self.cell(4, tid - 1536, 8)
        elif tid < 1024:
            r = self.cell(5 + tid // 256, tid % 256, 16)
        else:
            r = True
        self.memo[tid] = r
        return r

    def cell(self, sheet, i, cols):
        img = self.imgs.get(sheet)
        if img is None:
            return True
        if cols == 8:
            x, y = (i % 8) * 32, (i // 8) * 32
        else:   # B-E: two 8-column halves of 128 tiles
            x, y = ((i % 8) + (i // 128) * 8) * 32, ((i % 128) // 8) * 32
        if x + 32 > img.width or y + 32 > img.height:
            return True
        return img.crop((x, y, x + 32, y + 32)).getextrema()[3][1] > 0


def find_parallax(game, name):
    d = os.path.join(game, 'Graphics', 'Parallaxes')
    for ext in ('.png', '.jpg'):
        p = os.path.join(d, name + ext)
        if os.path.exists(p):
            return p
    return None


def check(build, game):
    log = open(os.path.join(build, 'qa.log'), errors='replace').read()
    seen = {}
    for m in LINE.finditer(log):
        seen[m.group(3)] = m.groups()   # the last line for a file is the one of the file on disk
    maps = load_maps(game, sorted({int(v[0]) for v in seen.values()}))
    alphas = {}
    fails = 0
    for f, (mid, fr, _, par, ox, oy, tr, tg, tb, gray, dx, dy) in sorted(seen.items()):
        shot_path = os.path.join(build, f)
        if not par or not os.path.exists(shot_path):
            print(f'SKIP  {f}: no parallax' if not par else f'SKIP  {f}: file missing')
            continue
        if (int(tr), int(tg), int(tb)) != (0, 0, 0):
            print(f'SKIP  {f}: RGB tone')
            continue
        src = find_parallax(game, par)
        if not src:
            print(f'SKIP  {f}: parallax {par} not found')
            continue
        shot = Image.open(shot_path).convert('RGB')
        pimg = Image.open(src).convert('RGB')
        w, h = shot.size
        pw, ph = pimg.size
        g = int(gray) / 255.0
        sp, pp = shot.load(), pimg.load()
        mp = maps[int(mid)]
        ta = alphas.setdefault(int(mid), TileAlpha(game, mp['tileset']))
        mw, mh, cells = mp['w'], mp['h'], mp['cells']

        def covered(x, y):
            tx, ty = int((x + float(dx) * 32) // 32), int((y + float(dy) * 32) // 32)
            if not (0 <= tx < mw and 0 <= ty < mh):
                return False
            return any(ta.opaque(cells[tx + ty * mw + z * mw * mh]) for z in range(mp['layers']))

        total = good = bright = 0
        for y in range(0, h // 3, 2):
            for x in range(0, w, 4):
                if covered(x, y):
                    continue
                c = pp[(x + int(ox)) % pw, (y + int(oy)) % ph]
                luma = 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]
                e = [c[i] + (luma - c[i]) * g for i in range(3)]
                s = sp[x, y]
                bright += sum(s)
                total += 1
                good += all(abs(s[i] - e[i]) <= 4 for i in range(3))
        if total < (h // 3 // 2) * (w // 4) // 10:
            print(f'SKIP  {f}: sky covered by tiles')
            continue
        if bright / (3 * total) < 40:
            print(f'SKIP  {f}: dark screen (fade)')
            continue
        share = good / total
        ok = share >= 0.85
        fails += not ok
        print(f'{"PASS" if ok else "FAIL"}  {f}: map {mid} parallax "{par}" ox={ox} oy={oy} gray={gray}: '
              f'{share:.0%} of the sky as expected')
    return fails


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    sys.exit(1 if check(sys.argv[1], sys.argv[2]) else 0)
