#!/usr/bin/env python3
"""LISA IconSet with PlayStation button symbols instead of the W/A/S/D key letters (combo display).
W -> triangle, A -> square, S -> cross, D -> circle (as the Vita port maps them); Q is kept.
Every replaced icon keeps the colours of the letter it replaces (fill, 1 px outline, black drop
shadow 2 px down-right), with the same 3 px stroke as the letters.
usage: make_ps_iconset.py ORIGINAL_ICONSET OUT_ICONSET [PREVIEW_PNG [OUT_OVERLAY]]
OUT_OVERLAY: same size, transparent except the replaced icons (only the port's own drawings, with the
colours of the letters they replace): what patches/ps_buttons.rb pastes over the player's IconSet."""
import sys
from collections import Counter
from PIL import Image

SRC, OUT = sys.argv[1], sys.argv[2]
PREVIEW = sys.argv[3] if len(sys.argv) > 3 else None
OVERLAY = sys.argv[4] if len(sys.argv) > 4 else None
im = Image.open(SRC).convert('RGBA')
assert im.size == (384, 432), im.size

# (row, column) of each letter icon -> symbol
SLOTS = {}
for r, cols in ((4, (8, 9, 10, 11)), (16, (3, 4, 5, 6)), (16, (8, 9, 10, 11)), (16, (12, 13, 14, 15)),
                (17, (3, 4, 5, 6)), (17, (8, 9, 10, 11)), (17, (12, 13, 14, 15))):
    for c, sym in zip(cols, ('triangle', 'square', 'cross', 'circle')):
        SLOTS[(r, c)] = sym

CX, CY = 11.5, 10.5          # centre of the 15x15 fill box x 4..18, y 3..17 (like the letters)
STROKE = 3.0

def inside(sym, x, y):
    dx, dy = x - CX, y - CY
    if sym == 'square':
        return max(abs(dx), abs(dy)) <= 7.5 and max(abs(dx), abs(dy)) > 7.5 - STROKE
    if sym == 'circle':
        d = (dx * dx + dy * dy) ** 0.5
        return 7.7 - STROKE < d <= 7.7
    if sym == 'cross':
        if max(abs(dx), abs(dy)) > 7.0:
            return False
        return abs(dx - dy) <= STROKE * 0.75 or abs(dx + dy) <= STROKE * 0.75
    if sym == 'triangle':
        # apex (CX, 3), base y = 17.5 from x = 4 to 19; signed distances to the 3 edges
        ax, ay, bx, by, cx, cy = CX, 2.6, 3.6, 17.6, 19.4, 17.6
        gx, gy = (ax + bx + cx) / 3, (ay + by + cy) / 3
        def dist(px, py, qx, qy):   # distance of (x, y) from edge p->q, > 0 on the triangle's side
            ex, ey = qx - px, qy - py
            n = (ex * ex + ey * ey) ** 0.5
            side = lambda u, v: ((u - px) * ey - (v - py) * ex) / n
            return side(x, y) if side(gx, gy) > 0 else -side(x, y)
        d = min(dist(ax, ay, bx, by), dist(bx, by, cx, cy), dist(cx, cy, ax, ay))
        return 0 <= d < STROKE
    raise ValueError(sym)

def mask(sym):
    m = [[False] * 24 for _ in range(24)]
    for y in range(24):
        for x in range(24):
            hits = sum(inside(sym, x + (i + 0.5) / 4, y + (j + 0.5) / 4) for i in range(4) for j in range(4))
            m[y][x] = hits >= 8
    return m

def dilate(m):
    return [[any(0 <= y + j < 24 and 0 <= x + i < 24 and m[y + j][x + i] for i in (-1, 0, 1) for j in (-1, 0, 1))
             for x in range(24)] for y in range(24)]

def palette(icon):
    cnt = Counter(p for p in icon.get_flattened_data() if p[3] > 0)
    shadow = min(cnt, key=lambda p: sum(p[:3]))          # the black drop shadow
    rest = [p for p, _ in cnt.most_common() if p != shadow]
    return rest[0], rest[1], shadow                       # fill (most pixels), outline, shadow

MASKS = {s: mask(s) for s in ('triangle', 'square', 'cross', 'circle')}
out = im.copy()
overlay = Image.new('RGBA', im.size, (0, 0, 0, 0))
for (r, c), sym in SLOTS.items():
    x0, y0 = c * 24, r * 24
    fill, outline, shadow = palette(im.crop((x0, y0, x0 + 24, y0 + 24)))
    F = MASKS[sym]
    FO = dilate(F)
    icon = Image.new('RGBA', (24, 24), (0, 0, 0, 0))
    for y in range(24):
        for x in range(24):
            if F[y][x]:
                icon.putpixel((x, y), fill)
            elif FO[y][x]:
                icon.putpixel((x, y), outline)
            elif any(0 <= y - k < 24 and 0 <= x - k < 24 and FO[y - k][x - k] for k in (1, 2)):
                icon.putpixel((x, y), shadow)
    out.paste(icon, (x0, y0))
    overlay.paste(icon, (x0, y0))
out.save(OUT)
if OVERLAY:
    overlay.save(OVERLAY)

if PREVIEW:
    rows = sorted({r for r, _ in SLOTS})
    sheet = Image.new('RGBA', (384 * 3, len(rows) * 2 * 24 * 3 + 8), (90, 90, 90, 255))
    for i, r in enumerate(rows):
        for k, src in enumerate((im, out)):
            band = src.crop((0, r * 24, 384, r * 24 + 24)).resize((384 * 3, 72), Image.NEAREST)
            sheet.paste(band, (0, (i * 2 + k) * 72), band)
    sheet.save(PREVIEW)
