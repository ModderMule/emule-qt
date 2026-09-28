#!/usr/bin/env python3
"""Generate resources/icons/UsenetSearch.ico -- the Usenet globe with a magnifier.

The Usenet indexer (Options page, search method, result tabs) used to borrow
Search.ico, the eD2K donkey with a magnifying glass, so it read as an eD2K
search. This reuses Usenet.ico's globe and swaps the newspaper badge for a lens:
at 16px globe + newspaper + lens would turn to mud.

Each size is drawn separately (see generate_usenet_icon.py for why).

Usage: python3 scripts/generate_usenet_search_icon.py
"""

import sys
from pathlib import Path

from generate_usenet_icon import RIM, globe
from icon_tools import SIZES, SS, report, scaled, write_ico

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("Pillow is required: pip3 install Pillow")

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "resources" / "icons" / "UsenetSearch.ico"

GLASS = (232, 242, 250)
FRAME = (66, 70, 78)
HANDLE = (96, 64, 40)
HALO = (255, 255, 255, 235)


def magnifier(d, detail, rim_w):
    """A lens with a handle to the lower right, as its own d x d RGBA image."""
    img = Image.new("RGBA", (d, d), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    halo = rim_w * 2
    lens = d * 0.60                       # lens diameter, upper-left
    x0 = y0 = halo
    x1 = y1 = x0 + lens
    frame_w = max(rim_w, round(d * 0.09))
    handle_w = max(rim_w * 3, round(d * 0.19))

    # Handle along the diagonal, from the lens rim to the corner.
    c = (x0 + x1) / 2
    start = c + lens * 0.33
    end = d - halo - handle_w * 0.3

    # Halo first: the lens has to separate from the globe behind it.
    draw.line([(start, start), (end, end)], fill=HALO, width=handle_w + halo * 2)
    draw.ellipse([x0 - halo, y0 - halo, x1 + halo, y1 + halo], fill=HALO)

    draw.line([(start, start), (end, end)], fill=HANDLE, width=handle_w)
    draw.ellipse([x0, y0, x1, y1], fill=GLASS, outline=FRAME, width=frame_w)

    if detail:
        glint = Image.new("RGBA", (d, d), (0, 0, 0, 0))
        g = lens * 0.22
        gx, gy = x0 + frame_w + g * 0.4, y0 + frame_w + g * 0.4
        ImageDraw.Draw(glint).ellipse([gx, gy, gx + g, gy + g * 0.7],
                                      fill=(*RIM, 70))
        img.alpha_composite(glint)

    return img


def render(size):
    """One frame, drawn at SSx and downsampled."""
    s = size * SS
    detail = size >= 32

    rim_w = scaled(1.0 if size <= 16 else size / 26.0)
    line_w = scaled(max(0.7, size / 48.0))

    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))

    g = round(s * 0.84)
    img.alpha_composite(globe(g, detail, line_w, rim_w), (0, 0))

    b = round(s * (0.66 if size <= 16 else 0.58))
    img.alpha_composite(magnifier(b, detail, rim_w), (s - b, s - b))

    return img.resize((size, size), Image.LANCZOS)


def main():
    frames = [render(n) for n in SIZES]
    write_ico(OUT, frames)
    report(ROOT, OUT)


if __name__ == "__main__":
    main()
