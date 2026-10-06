from PIL import Image, ImageFont, ImageDraw

# usage: make_font_atlas.py <game>/Fonts/VCR_OSD_MONO.ttf <game>/Fonts/VCR_OSD_MONO_24.png
import sys
FONT_PATH = sys.argv[1]
OUTPUT_PATH = sys.argv[2]

FONT_SIZE = 24

# ASCII stampabile: spazio (32) fino a ~ (126)
FIRST_CHAR = 32
LAST_CHAR = 126

COLS = 16

# Celle volutamente un po' abbondanti.
CELL_W = 32
CELL_H = 40

chars = [chr(i) for i in range(FIRST_CHAR, LAST_CHAR + 1)]

rows = (len(chars) + COLS - 1) // COLS

atlas = Image.new(
    "RGBA",
    (COLS * CELL_W, rows * CELL_H),
    (0, 0, 0, 0)
)

draw = ImageDraw.Draw(atlas)

font = ImageFont.truetype(
    FONT_PATH,
    FONT_SIZE
)

for index, char in enumerate(chars):
    col = index % COLS
    row = index // COLS

    cell_x = col * CELL_W
    cell_y = row * CELL_H

    bbox = draw.textbbox(
        (0, 0),
        char,
        font=font
    )

    glyph_w = bbox[2] - bbox[0]
    glyph_h = bbox[3] - bbox[1]

    # Centra il carattere nella cella.
    x = cell_x + (CELL_W - glyph_w) // 2 - bbox[0]
    y = cell_y + (CELL_H - glyph_h) // 2 - bbox[1]

    draw.text(
        (x, y),
        char,
        font=font,
        fill=(255, 255, 255, 255)
    )

atlas.save(OUTPUT_PATH)

print("Atlas creato:")
print(OUTPUT_PATH)
print()
print("Dimensioni:", atlas.size)
print("FIRST_CHAR =", FIRST_CHAR)
print("LAST_CHAR  =", LAST_CHAR)
print("COLS       =", COLS)
print("CELL_W     =", CELL_W)
print("CELL_H     =", CELL_H)
