#!/usr/bin/env python3
"""Converts the LiveArea artwork in art/livearea/ to what the Vita expects, into sce_sys/.

Sources (any size, any PNG/JPG mode):
  art/livearea/icon0.png    -> sce_sys/icon0.png                         128 x 128  (bubble icon)
  art/livearea/bg.png       -> sce_sys/livearea/contents/bg.png          840 x 500  (LiveArea background)
  art/livearea/startup.png  -> sce_sys/livearea/contents/startup.png     280 x 158  (the "start" gate)
Per image:
  trim     near-black border rows/columns cut first (letterbox / pillarbox of a game screenshot);
  fit      'cover' = resized to fill the target, the excess cropped (anchor: 'center' or 'bottom');
           'contain' = whole image resized to fit, the empty sides filled by repeating each row's
           (or column's) edge pixel - right for vertical gradients like the startup background;
  resample BOX for integer downscales (pixel art stays sharp-edged), LANCZOS otherwise.
Optional art/livearea/layout.json: per-source edits applied first (shift_rows: rows from from_y moved
right by dx; shrink: content scaled down onto the fill colour, anchored at the bottom or centre;
move_text: a light text inside box lifted off a background that is uniform along each row, read at
column bg_x, and redrawn dy pixels lower with the same per-pixel coverage).
The Vita installer wants 8-bit palette PNGs for these (a true-colour PNG fails the install or shows
blank), so each one is quantized to at most 256 colours. Missing sources keep the current files.
Note: the shell builds the LiveArea when the app is first installed; if an update does not show the
new background, delete the app and install the VPK again (game data lives in ux0:data/).
usage: make_livearea.py [REPO_DIR]"""
import json, os, sys
from PIL import Image

repo = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', '..')
JOBS = [('icon0.png', 'sce_sys/icon0.png', (128, 128), dict(fit='cover')),
        ('bg.png', 'sce_sys/livearea/contents/bg.png', (840, 500), dict(trim=True, fit='cover', anchor='bottom')),
        ('startup.png', 'sce_sys/livearea/contents/startup.png', (280, 158), dict(trim=True, fit='contain'))]

def trim(im, thr=24):
    """Cut the border rows/columns whose mean brightness is below thr (black bars), plus 1 pixel."""
    g = im.convert('L')
    w, h = g.size
    px = g.load()
    col = [sum(px[x, y] for y in range(0, h, 4)) / len(range(0, h, 4)) for x in range(w)]
    row = [sum(px[x, y] for x in range(0, w, 4)) / len(range(0, w, 4)) for y in range(h)]
    def span(v):
        lo, hi = 0, len(v)
        while lo < hi and v[lo] < thr: lo += 1
        while hi > lo and v[hi - 1] < thr: hi -= 1
        return (lo + (1 if lo else 0), hi - (1 if hi < len(v) else 0))
    (l, r), (t, b) = span(col), span(row)
    return im if (l, t, r, b) == (0, 0, w, h) or r <= l or b <= t else im.crop((l, t, r, b))

def resize(im, w, h):
    integer = im.width % w == 0 and im.height % h == 0 and im.width // w == im.height // h
    return im.resize((w, h), Image.BOX if integer else Image.LANCZOS)

def cover(im, size, anchor):
    w, h = size
    s = max(w / im.width, h / im.height)
    im = resize(im, max(w, round(im.width * s)), max(h, round(im.height * s)))
    x = (im.width - w) // 2
    y = im.height - h if anchor == 'bottom' else (im.height - h) // 2
    return im.crop((x, y, x + w, y + h))

def contain(im, size):
    w, h = size
    s = min(w / im.width, h / im.height)
    iw, ih = max(1, round(im.width * s)), max(1, round(im.height * s))
    im = resize(im, iw, ih)
    out = Image.new(im.mode, size)
    x, y = (w - iw) // 2, (h - ih) // 2
    out.paste(im, (x, y))
    if iw < w:   # repeat the first / last column of each row
        out.paste(im.crop((0, 0, 1, ih)).resize((x, ih)), (0, y)) if x else None
        out.paste(im.crop((iw - 1, 0, iw, ih)).resize((w - x - iw, ih)), (x + iw, y))
    if ih < h:
        out.paste(im.crop((0, 0, iw, 1)).resize((iw, y)), (x, 0)) if y else None
        out.paste(im.crop((0, ih - 1, iw, ih)).resize((iw, h - y - ih)), (x, y + ih))
    return out

def shift_rows(im, from_y, dx, fill):
    out = im.copy()
    low = im.crop((0, from_y, im.width, im.height))
    out.paste(tuple(fill) + (255,), (0, from_y, im.width, im.height))
    out.paste(low, (dx, from_y))
    return out

def shrink(im, scale, anchor, fill):
    w, h = im.size
    nw, nh = round(w * scale), round(h * scale)
    out = Image.new('RGBA', (w, h), tuple(fill) + (255,))
    out.paste(im.resize((nw, nh), Image.LANCZOS), ((w - nw) // 2, h - nh if anchor == 'bottom' else (h - nh) // 2))
    return out

def move_text(im, box, dy, bg_x, color):
    """Coverage of the text colour over each row's background: a = (c - bg) / (color - bg), max over channels."""
    out = im.copy()
    src, dst = im.load(), out.load()
    x0, y0, x1, y1 = box
    bg = lambda y: src[bg_x, y]
    cov = {}
    for y in range(y0, y1):
        b = bg(y)
        for x in range(x0, x1):
            p = src[x, y]
            a = max(((p[k] - b[k]) / (color[k] - b[k]) if color[k] - b[k] > 20 else 0.0) for k in range(3))
            if a > 0.02:
                cov[(x, y)] = min(1.0, a)
            dst[x, y] = b
    for (x, y), a in cov.items():
        ny = y + dy
        if 0 <= ny < im.height:
            b = src[bg_x, ny]
            dst[x, ny] = tuple(round(b[k] * (1 - a) + color[k] * a) for k in range(3)) + (255,)
    return out

lp = os.path.join(repo, 'art/livearea/layout.json')
LAYOUT = json.load(open(lp)) if os.path.exists(lp) else {}

for src, dst, size, opt in JOBS:
    sp, dp = os.path.join(repo, 'art/livearea', src), os.path.join(repo, dst)
    if not os.path.exists(sp):
        print(f'{src}: no source, {dst} kept')
        continue
    im = Image.open(sp).convert('RGBA')
    src_size = im.size
    lay = LAYOUT.get(src, {})
    fill = lay.get('fill', [0, 0, 0])
    if 'shift_rows' in lay:
        im = shift_rows(im, lay['shift_rows']['from_y'], lay['shift_rows']['dx'], fill)
    if 'shrink' in lay:
        im = shrink(im, lay['shrink']['scale'], lay['shrink'].get('anchor', 'center'), fill)
    if 'move_text' in lay:
        t = lay['move_text']
        im = move_text(im, t['box'], t['dy'], t['bg_x'], t['color'])
    edits = [k for k in ('shift_rows', 'shrink', 'move_text') if k in lay]
    if opt.get('trim'):
        im = trim(im)
    if im.size != size:
        im = contain(im, size) if opt.get('fit') == 'contain' else cover(im, size, opt.get('anchor', 'center'))
    flat = Image.new('RGB', size, (0, 0, 0))
    flat.paste(im, mask=im.getchannel('A'))           # LiveArea images are opaque
    out = flat.quantize(colors=256, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG)
    os.makedirs(os.path.dirname(dp), exist_ok=True)
    out.save(dp, optimize=True)
    chk = Image.open(dp)
    assert chk.size == size and chk.mode == 'P', (dst, chk.size, chk.mode)
    print(f'{src} {src_size[0]}x{src_size[1]}{" (" + ", ".join(edits) + ")" if edits else ""} -> {dst} {size[0]}x{size[1]} palette ({len(chk.getpalette()) // 3} colours) {os.path.getsize(dp)} bytes')
