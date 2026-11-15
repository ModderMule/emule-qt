#!/usr/bin/env python3
"""Generate resources/icons/Memory.ico -- a RAM module.

For the Memory branch of the statistics tree (not in MFC). Green board with dark
chips, gold contacts along the bottom edge and the key notch.

Each size is drawn separately at SSx and downsampled (see generate_usenet_icon.py).

Usage: python3 scripts/generate_memory_icon.py
"""

import sys
from pathlib import Path

from icon_tools import SIZES, SS, report, scaled, write_ico

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("Pillow is required: uv sync (or pip3 install Pillow)")

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "resources" / "icons" / "Memory.ico"

BOARD_LIGHT = (70, 170, 90)
BOARD_DARK = (30, 115, 55)
RIM = (15, 70, 30)
CHIP = (35, 38, 45)
CHIP_HL = (90, 95, 110)
GOLD = (235, 185, 60)
GOLD_DARK = (170, 120, 25)


def render(size):
    s = size * SS
    small = size <= 16
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    rim_w = scaled(1.0 if small else size / 32.0)

    # board: full width, vertically centred, a bit lower than half
    x0, x1 = 0, s - 1
    y0 = round(s * (0.22 if small else 0.24))
    y1 = round(s * (0.80 if small else 0.78))
    radius = scaled(1.0 if small else size / 24.0)
    d.rounded_rectangle([x0, y0, x1, y1], radius=radius, fill=(*RIM, 255))
    # vertical gradient inside the rim
    for y in range(y0 + rim_w, y1 - rim_w + 1):
        t = (y - y0) / max(1, y1 - y0)
        c = tuple(round(a + (b - a) * t) for a, b in zip(BOARD_LIGHT, BOARD_DARK))
        d.line([x0 + rim_w, y, x1 - rim_w, y], fill=(*c, 255))

    # gold contacts along the bottom, key notch off-centre
    pins_top = y1 - round((y1 - y0) * (0.30 if small else 0.26))
    pin_count = 4 if small else 8 if size <= 32 else 10
    pitch = (x1 - x0 - 2 * rim_w) / pin_count
    notch = pin_count * 5 // 8
    for i in range(pin_count):
        if i == notch and not small:
            continue
        px0 = round(x0 + rim_w + i * pitch + pitch * 0.18)
        px1 = round(x0 + rim_w + (i + 1) * pitch - pitch * 0.18)
        d.rectangle([px0, pins_top, px1, y1 - rim_w], fill=(*GOLD, 255))
        if not small:
            d.rectangle([px0, pins_top, px1, pins_top + scaled(0.5)], fill=(*GOLD_DARK, 255))
    # notch cut into the bottom edge
    nx = round(x0 + rim_w + notch * pitch + pitch * 0.5)
    nw = max(scaled(1.0), round(pitch * 0.35))
    d.rectangle([nx - nw // 2, y1 - round((y1 - y0) * 0.16), nx + nw // 2, y1], fill=(0, 0, 0, 0))

    # chips on the upper part of the board
    chips = 3 if small else 4
    chip_top = y0 + rim_w + round((y1 - y0) * 0.10)
    chip_bot = pins_top - round((y1 - y0) * 0.10)
    gap = (x1 - x0) * (0.07 if small else 0.06)
    cw = ((x1 - x0) - 2 * rim_w - gap * (chips + 1)) / chips
    for i in range(chips):
        cx0 = round(x0 + rim_w + gap + i * (cw + gap))
        cx1 = round(cx0 + cw)
        d.rectangle([cx0, chip_top, cx1, chip_bot], fill=(*CHIP, 255))
        if not small:
            d.rectangle([cx0, chip_top, cx1, chip_top + scaled(size / 48.0)], fill=(*CHIP_HL, 255))

    return img.resize((size, size), Image.LANCZOS)


def main():
    frames = [render(n) for n in SIZES]
    write_ico(OUT, frames)
    report(ROOT, OUT)


if __name__ == "__main__":
    main()
