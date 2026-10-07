#!/usr/bin/env python3
"""Loading screen of the port (MKXP_VITA_QUIET_BOOT): boot/loading.png, 960x544, from the LiveArea
startup artwork. The artwork is scaled to the screen height and centred; the sides continue its
vertical gradient (above the ground) and its ground pattern; "Loading..." at the bottom right.
usage: make_loading.py ART_STARTUP_PNG FONT_TTF OUT_PNG"""
import sys
from PIL import Image, ImageDraw, ImageFont

src, font_path, out = sys.argv[1:4]
W, H = 960, 544
art = Image.open(src).convert('RGB')
aw = round(art.width * H / art.height)
art = art.resize((aw, H), Image.LANCZOS)
x0 = (W - aw) // 2
canvas = Image.new('RGB', (W, H))
apx, cpx = art.load(), canvas.load()
# ground: the first row from the bottom whose colour differs from the gradient's left column
ground = H
for y in range(H):
    if apx[aw // 2, y] == (0, 0, 0) or (y > H // 2 and abs(apx[0, y][0] - apx[0, y - 1][0]) > 40):
        ground = y
        break
for y in range(H):
    for x in range(W):
        if x0 <= x < x0 + aw:
            cpx[x, y] = apx[x - x0, y]
        elif y < ground:
            cpx[x, y] = apx[0 if x < x0 else aw - 1, y]     # the gradient goes on
        else:
            cpx[x, y] = apx[(x - x0) % aw, y]               # the ground pattern goes on
d = ImageDraw.Draw(canvas)
font = ImageFont.truetype(font_path, 28)
text = 'Loading...'
tw = d.textlength(text, font=font)
tx, ty = W - tw - 28, H - 28 - 28
d.text((tx + 2, ty + 2), text, font=font, fill=(40, 20, 10))
d.text((tx, ty), text, font=font, fill=(255, 255, 255))
canvas.save(out, optimize=True)
print('%s: %dx%d, artwork %dx%d at x=%d, ground from y=%d' % (out, W, H, aw, H, x0, ground))
