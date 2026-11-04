#!/usr/bin/env python3
"""Generate resources/icons/I2P.ico -- a 3x3 grid of dots, pink to violet.

For the Options > BitTorrent > I2P page and I2P torrent peers (issue #118). The dot
grid is the usual I2P signifier; each dot gets a small highlight so it sits with the
glossy toolbar icons.

Each size is drawn separately at SSx and downsampled (see generate_usenet_icon.py).

Usage: python3 scripts/generate_i2p_icon.py
"""

import sys
from pathlib import Path

from icon_tools import SIZES, SS, report, write_ico

try:
    from PIL import Image, ImageDraw, ImageFilter
except ImportError:
    sys.exit("Pillow is required: pip3 install Pillow")

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "resources" / "icons" / "I2P.ico"

PINK = (0xE8, 0x5C, 0xB4)
VIOLET = (0x6A, 0x2C, 0xC8)


def mix(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def render(size):
    s = size * SS
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    pitch = s / 3.0
    # small sizes: bigger dots, so the grid still reads at 16 px
    r = pitch * (0.44 if size <= 16 else 0.40)
    for gy in range(3):
        for gx in range(3):
            cx, cy = (gx + 0.5) * pitch, (gy + 0.5) * pitch
            col = mix(PINK, VIOLET, (gx + gy) / 4.0)
            dot = Image.new("RGBA", (s, s), (0, 0, 0, 0))
            d = ImageDraw.Draw(dot)
            d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=col + (255,))
            if size > 16:
                # highlight up-left, blurred
                hl = Image.new("RGBA", (s, s), (0, 0, 0, 0))
                hr = r * 0.45
                hx, hy = cx - r * 0.32, cy - r * 0.32
                ImageDraw.Draw(hl).ellipse((hx - hr, hy - hr, hx + hr, hy + hr), fill=(255, 255, 255, 110))
                hl = hl.filter(ImageFilter.GaussianBlur(hr * 0.5))
                # clip to the dot
                hl.putalpha(Image.composite(hl.getchannel("A"), Image.new("L", (s, s), 0), dot.getchannel("A")))
                dot.alpha_composite(hl)
            img.alpha_composite(dot)
    return img.resize((size, size), Image.LANCZOS)


def main():
    frames = [render(n) for n in SIZES]
    write_ico(OUT, frames)
    report(ROOT, OUT)


if __name__ == "__main__":
    main()
