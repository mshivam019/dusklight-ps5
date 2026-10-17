#!/usr/bin/env python3
# Dusklight for the PlayStation 5 - the DualSense's symbols for the game's button prompts.
# SPDX-License-Identifier: MIT
"""Makes ps5/art/glyphs/*.png: replacement textures for the letters on the game's buttons.

The game draws a button as a coloured shape with a white letter over it, each letter a small
texture of its own. Dusklight replaces a texture by a file named after it (Dolphin's scheme:
tex1_<width>x<height>_<hash of its texels>_<format>.png), so each picture here carries the name
of the letter it stands in for, and nothing of the letter itself: a cross for A, a circle for B,
a square for X and a triangle for Y, as Dusklight's default mapping has them (A south, B east,
X west, Y north), and "R1" for Z. The names are the GameCube USA disc's.

The symbols are Kenney's Input Prompts (www.kenney.nl, CC0: ps5/art/kenney/): of each of its
outlined buttons the outline is dropped, since the game draws the button itself, and what is
inside is kept.

Needs Python with Pillow.
"""
from pathlib import Path

from PIL import Image

SCALE = 4  # the replacement is this many times the original's size
art = Path(__file__).resolve().parent
out = art / "glyphs"
out.mkdir(exist_ok=True)


def inside(source):
    """The picture without its outline: the connected shape with the largest bounds goes."""
    alpha = Image.open(art / "kenney" / (source + ".png")).convert("RGBA").getchannel("A")
    w, h = alpha.size
    values = list(alpha.tobytes())
    solid = [v > 40 for v in values]
    label = [0] * (w * h)
    shapes = []  # (bounds' area, label)
    for start in range(w * h):
        if not solid[start] or label[start]:
            continue
        n = len(shapes) + 1
        label[start] = n
        stack = [start]
        x0 = x1 = start % w
        y0 = y1 = start // w
        while stack:
            p = stack.pop()
            x, y = p % w, p // w
            x0, x1, y0, y1 = min(x0, x), max(x1, x), min(y0, y), max(y1, y)
            for q in (p - 1 if x else -1, p + 1 if x < w - 1 else -1, p - w, p + w):
                if 0 <= q < w * h and solid[q] and not label[q]:
                    label[q] = n
                    stack.append(q)
        shapes.append(((x1 - x0) * (y1 - y0), n))
    outline = max(shapes)[1]
    keep = Image.frombytes("L", (w, h), bytes(a if solid[i] and label[i] != outline else 0 for i, a in enumerate(values)))
    return keep.crop(keep.getbbox())


def glyph(name, w, h, source, fill=0.8):
    symbol = inside(source)
    box = (w * SCALE * fill, h * SCALE * fill)
    k = min(box[0] / symbol.width, box[1] / symbol.height)
    symbol = symbol.resize((max(1, round(symbol.width * k)), max(1, round(symbol.height * k))), Image.LANCZOS)
    mask = Image.new("L", (w * SCALE, h * SCALE), 0)
    mask.paste(symbol, ((mask.width - symbol.width) // 2, (mask.height - symbol.height) // 2))
    white = Image.new("RGBA", mask.size, (255, 255, 255, 0))
    white.putalpha(mask)
    white.save(out / (name + ".png"))


glyph("tex1_32x46_683dc853245a0f36_0", 32, 46, "playstation_button_cross_outline")     # A
glyph("tex1_24x40_b191153a917b3b50_0", 24, 40, "playstation_button_circle_outline")    # B
glyph("tex1_32x45_3b03f19ac102dd38_0", 32, 45, "playstation_button_square_outline")    # X
glyph("tex1_32x40_a57e14b2809290fc_0", 32, 40, "playstation_button_triangle_outline")  # Y
glyph("tex1_24x40_1868b6990a352fae_2", 24, 40, "playstation_trigger_r1_outline", 0.95)  # Z
print(sorted(p.name for p in out.glob("*.png")))
