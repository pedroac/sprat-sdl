"""Draws the example's source frames. Run once; the PNGs are committed."""
import math
import os

from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sources")


def save(img, path):
    full = os.path.join(OUT, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    img.save(full)


# Hero run cycle: 48x48 frames, feet at (24, 44), facing right (the eye and nose show it).
SKIN = (255, 204, 153, 255)
SHIRT = (52, 120, 246, 255)
PANTS = (40, 40, 60, 255)
for i in range(6):
    phase = i / 6 * 2 * math.pi
    bob = round(abs(math.sin(phase)) * 2)
    img = Image.new("RGBA", (48, 48))
    d = ImageDraw.Draw(img)
    swing = math.sin(phase) * 7
    for side, k in ((-1, 1), (1, -1)):
        fx = 24 + k * swing
        d.line([(24, 32 - bob), (fx, 44)], fill=PANTS, width=4)
    d.rounded_rectangle([17, 18 - bob, 31, 33 - bob], radius=4, fill=SHIRT)
    d.line([(24, 22 - bob), (24 + swing * -0.8, 30 - bob)], fill=SKIN, width=3)
    d.ellipse([16, 4 - bob, 32, 20 - bob], fill=SKIN)
    d.polygon([(32, 11 - bob), (36, 13 - bob), (32, 15 - bob)], fill=SKIN)
    d.ellipse([26, 9 - bob, 29, 12 - bob], fill=(0, 0, 0, 255))
    save(img, f"hero/run_{i + 1:02d}.png")

# Coin spin: 24x24, centered.
for i in range(8):
    w = max(1, round(abs(math.cos(i / 8 * math.pi)) * 10))
    img = Image.new("RGBA", (24, 24))
    d = ImageDraw.Draw(img)
    d.ellipse([12 - w, 2, 12 + w, 21], fill=(250, 200, 30, 255), outline=(180, 120, 0, 255))
    if w > 3:
        d.line([(12, 7), (12, 16)], fill=(180, 120, 0, 255), width=2)
    save(img, f"coin/spin_{i + 1:02d}.png")

# Sword: tall and thin, so the packer turns it.
img = Image.new("RGBA", (12, 56))
d = ImageDraw.Draw(img)
d.polygon([(6, 0), (9, 6), (9, 40), (3, 40), (3, 6)], fill=(210, 220, 230, 255), outline=(120, 130, 140, 255))
d.rectangle([0, 40, 11, 43], fill=(150, 90, 30, 255))
d.rectangle([4, 44, 7, 55], fill=(100, 60, 20, 255))
save(img, "items/sword.png")

# Panel: an Android nine-patch (.9.png), 32x32 inside the 1px marker border, 8px corners.
inner = Image.new("RGBA", (32, 32))
d = ImageDraw.Draw(inner)
d.rounded_rectangle([0, 0, 31, 31], radius=8, fill=(30, 34, 52, 235), outline=(250, 200, 30, 255), width=3)
for c in ((5, 5), (26, 5), (5, 26), (26, 26)):
    d.ellipse([c[0] - 1, c[1] - 1, c[0] + 1, c[1] + 1], fill=(250, 200, 30, 255))
img = Image.new("RGBA", (34, 34))
img.paste(inner, (1, 1))
for x in range(8, 24):
    img.putpixel((x + 1, 0), (0, 0, 0, 255))
for y in range(8, 24):
    img.putpixel((0, y + 1), (0, 0, 0, 255))
save(img, "ui/panel.9.png")
