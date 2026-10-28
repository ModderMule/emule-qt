#!/usr/bin/env python3
"""Generate resources/icons/Hoster.ico -- a cloud with a download badge.

For the Hoster toolbar button and its options page (downloads from file hosters,
docs/hoster.md). It shares the palette of Torrent.ico and Usenet.ico: a glossy blue
body with a dark-blue rim, and the same green arrow with a light halo.

Each size is drawn separately at SSx and downsampled (see generate_usenet_icon.py).

Usage: python3 scripts/generate_hoster_icon.py
"""

import sys
from pathlib import Path

from icon_tools import SIZES, SS, report, scaled, write_ico
from generate_torrent_icon import NODE_DARK, NODE_LIGHT, RIM, badge

try:
    from PIL import Image, ImageChops, ImageDraw, ImageFilter
except ImportError:
    sys.exit("Pillow is required: pip3 install Pillow")

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "resources" / "icons" / "Hoster.ico"

# Lobes of the cloud as (cx, cy, r) fractions of its box; the flat base is a
# rounded rectangle under them.
LOBES = [(0.30, 0.58, 0.20), (0.52, 0.42, 0.27), (0.75, 0.58, 0.20)]
BASE = (0.10, 0.52, 0.92, 0.80)


def cloud(box, rim_w, gloss):
    """The cloud, drawn into a box x box RGBA image."""
    mask = Image.new("L", (box, box), 0)
    d = ImageDraw.Draw(mask)
    for cx, cy, r in LOBES:
        d.ellipse([(cx - r) * box, (cy - r) * box, (cx + r) * box, (cy + r) * box], fill=255)
    x0, y0, x1, y1 = BASE
    d.rounded_rectangle([x0 * box, y0 * box, x1 * box, y1 * box], radius=(y1 - y0) * box / 2, fill=255)

    grad = Image.new("RGBA", (box, box))
    px = grad.load()
    for y in range(box):
        t = min(1.0, max(0.0, (y / box - 0.18) / 0.65)) ** 0.9
        c = tuple(round(a + (b - a) * t) for a, b in zip(NODE_LIGHT, NODE_DARK))
        for x in range(box):
            px[x, y] = (*c, 255)
    img = Image.new("RGBA", (box, box), (0, 0, 0, 0))
    img.paste(grad, (0, 0), mask)

    if gloss:
        hl = Image.new("RGBA", (box, box), (0, 0, 0, 0))
        ImageDraw.Draw(hl).ellipse([box * 0.34, box * 0.20, box * 0.66, box * 0.40], fill=(255, 255, 255, 120))
        hl_mask = Image.new("L", (box, box), 0)
        hl_mask.paste(mask)
        img.alpha_composite(Image.composite(hl, Image.new("RGBA", (box, box), (0, 0, 0, 0)), hl_mask))

    # rim: the mask minus the mask shrunk by the rim width
    inner = mask.filter(ImageFilter.MinFilter(rim_w * 2 + 1))
    img.paste(Image.new("RGBA", (box, box), (*RIM, 255)), (0, 0), ImageChops.subtract(mask, inner))
    return img


def render(size):
    s = size * SS
    small = size <= 16
    rim_w = scaled(1.0 if small else size / 30.0)
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    box = s
    img.alpha_composite(cloud(box, rim_w, gloss=not small), (0, -round(s * 0.08)))
    b = round(s * (0.52 if small else 0.48))
    tile = badge(round(b * 0.86), b, rim_w)
    img.alpha_composite(tile, (s - tile.width, s - tile.height))
    return img.resize((size, size), Image.LANCZOS)


def main():
    frames = [render(n) for n in SIZES]
    write_ico(OUT, frames)
    report(ROOT, OUT)


if __name__ == "__main__":
    main()
