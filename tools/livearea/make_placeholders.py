#!/usr/bin/env python3
"""Placeholder LiveArea artwork (the port's own, plain text on a dark background) in art/livearea/,
to be replaced by the real artwork. usage: make_placeholders.py [REPO_DIR]"""
import os, sys
from PIL import Image, ImageDraw, ImageFont

repo = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', '..')
out = os.path.join(repo, 'art/livearea')
os.makedirs(out, exist_ok=True)
BOLD = '/usr/share/fonts/liberation-sans-fonts/LiberationSans-Bold.ttf'
REG = '/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf'
RED, BONE, DARK = (176, 24, 24), (232, 222, 200), (16, 12, 12)

def gradient(size):
    im = Image.new('RGB', size)
    d = ImageDraw.Draw(im)
    for y in range(size[1]):
        t = y / max(1, size[1] - 1)
        d.line([(0, y), (size[0], y)], fill=(int(40 - 24 * t), int(10 - 4 * t), int(10 - 4 * t)))
    return im

def centred(d, size, y, text, font, fill):
    w = d.textlength(text, font=font)
    d.text(((size[0] - w) / 2, y), text, font=font, fill=fill)

icon = gradient((128, 128)); d = ImageDraw.Draw(icon)
centred(d, (128, 128), 26, 'LISA', ImageFont.truetype(BOLD, 44), RED)
centred(d, (128, 128), 78, 'VITA', ImageFont.truetype(BOLD, 22), BONE)
icon.save(os.path.join(out, 'icon0.png'))

bg = gradient((840, 500)); d = ImageDraw.Draw(bg)
centred(d, (840, 500), 150, 'LISA', ImageFont.truetype(BOLD, 140), RED)
centred(d, (840, 500), 310, 'THE PAINFUL', ImageFont.truetype(BOLD, 44), BONE)
centred(d, (840, 500), 440, 'PS Vita port - placeholder artwork', ImageFont.truetype(REG, 18), (120, 110, 100))
bg.save(os.path.join(out, 'bg.png'))

st = Image.new('RGB', (280, 158), DARK); d = ImageDraw.Draw(st)
d.rectangle([4, 4, 275, 153], outline=RED, width=3)
centred(d, (280, 158), 34, 'LISA', ImageFont.truetype(BOLD, 56), RED)
centred(d, (280, 158), 102, 'THE PAINFUL', ImageFont.truetype(BOLD, 20), BONE)
st.save(os.path.join(out, 'startup.png'))
print('placeholders in', out)
