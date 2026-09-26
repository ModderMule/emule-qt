#!/usr/bin/env python3
"""Generate resources/icons/Torrent.ico -- a peer swarm with a download badge.

Used in front of torrent search results now, and meant for the Torrent toolbar
button once BitTorrent lands. It sits beside Usenet.ico, so it shares that
icon's palette: blue glossy nodes, dark-blue rims and a light halo around the
badge.

Each size is drawn separately (see generate_usenet_icon.py for why). At 16px
the swarm drops to four nodes and the links thicken, or it turns to mud.

Usage: python3 scripts/generate_torrent_icon.py
"""

import sys
from pathlib import Path

from icon_tools import SIZES, SS, report, scaled, write_ico

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("Pillow is required: pip3 install Pillow")

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "resources" / "icons" / "Torrent.ico"

# Shared with Usenet.ico's globe.
NODE_LIGHT = (128, 202, 240)
NODE_DARK = (23, 88, 158)
RIM = (14, 60, 104)
LINK = (40, 96, 150)
ARROW_LIGHT = (122, 214, 92)
ARROW_DARK = (38, 132, 44)
ARROW_RIM = (22, 84, 30)

# Node centres and radii, as fractions of the swarm box. The big centre node
# is the "you" every peer links to; the ring nodes also link to their
# neighbours so it reads as a mesh, not a star.
SWARM_FULL = [
    (0.48, 0.46, 0.19),
    (0.16, 0.17, 0.135),
    (0.82, 0.15, 0.135),
    (0.13, 0.75, 0.135),
    (0.60, 0.86, 0.12),
    (0.87, 0.55, 0.12),
]
LINKS_FULL = [(0, 1), (0, 2), (0, 3), (0, 4), (0, 5),
              (1, 2), (1, 3), (2, 5), (3, 4), (4, 5)]

SWARM_SMALL = [
    (0.46, 0.46, 0.25),
    (0.19, 0.19, 0.19),
    (0.83, 0.19, 0.19),
    (0.19, 0.83, 0.19),
]
LINKS_SMALL = [(0, 1), (0, 2), (0, 3), (1, 2), (1, 3)]


def node(d, rim_w, gloss):
    """One glossy sphere as its own d x d RGBA image."""
    img = Image.new("RGBA", (d, d), (0, 0, 0, 0))
    grad = Image.new("RGB", (d, d))
    px = grad.load()
    cx, cy, r = d * 0.34, d * 0.30, d * 0.95
    for y in range(d):
        for x in range(d):
            t = min(1.0, ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5 / r) ** 0.8
            px[x, y] = tuple(
                round(a + (b - a) * t) for a, b in zip(NODE_LIGHT, NODE_DARK)
            )
    img.paste(grad, (0, 0))

    if gloss:
        hl = Image.new("RGBA", (d, d), (0, 0, 0, 0))
        ImageDraw.Draw(hl).ellipse([d * 0.18, d * 0.12, d * 0.58, d * 0.42],
                                   fill=(255, 255, 255, 130))
        img.alpha_composite(hl)

    mask = Image.new("L", (d, d), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, d - 1, d - 1], fill=255)
    img.putalpha(mask)
    ImageDraw.Draw(img).ellipse([rim_w // 2, rim_w // 2,
                                 d - 1 - rim_w // 2, d - 1 - rim_w // 2],
                                outline=RIM, width=rim_w)
    return img


def swarm(box, small, link_w, rim_w):
    """The linked nodes, drawn into a box x box RGBA image."""
    img = Image.new("RGBA", (box, box), (0, 0, 0, 0))
    nodes, links = (SWARM_SMALL, LINKS_SMALL) if small else (SWARM_FULL, LINKS_FULL)

    d = ImageDraw.Draw(img)
    for a, b in links:
        ax, ay, _ = nodes[a]
        bx, by, _ = nodes[b]
        d.line([(ax * box, ay * box), (bx * box, by * box)],
               fill=LINK, width=link_w)

    for x, y, r in nodes:
        dia = max(2, round(2 * r * box))
        img.alpha_composite(node(dia, rim_w, gloss=not small),
                            (round(x * box - dia / 2), round(y * box - dia / 2)))
    return img


def badge(w, h, rim_w):
    """A green down arrow as its own RGBA image, with a light halo."""
    pad = rim_w * 2
    img = Image.new("RGBA", (w + pad * 2, h + pad * 2), (0, 0, 0, 0))

    x0, y0 = pad, pad
    shaft = w * 0.24
    head_y = y0 + h * 0.48
    cx = x0 + w / 2
    arrow = [(cx - shaft, y0), (cx + shaft, y0), (cx + shaft, head_y),
             (x0 + w - 1, head_y), (cx, y0 + h - 1), (x0, head_y),
             (cx - shaft, head_y)]

    # Halo first: the arrow has to separate from the swarm behind it.
    ImageDraw.Draw(img).polygon(arrow, fill=(255, 255, 255, 235),
                                outline=(255, 255, 255, 235), width=rim_w * 2)

    # Vertical gradient fill, clipped to the arrow.
    fill = Image.new("RGBA", img.size, (0, 0, 0, 0))
    fp = fill.load()
    for y in range(img.height):
        t = min(1.0, max(0.0, (y - y0) / max(1, h)))
        c = tuple(round(a + (b - a) * t) for a, b in zip(ARROW_LIGHT, ARROW_DARK))
        for x in range(img.width):
            fp[x, y] = (*c, 255)
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).polygon(arrow, fill=255)
    img.paste(fill, (0, 0), mask)

    ImageDraw.Draw(img).polygon(arrow, outline=ARROW_RIM,
                                width=max(1, round(rim_w * 0.75)))
    return img


def render(size):
    """One frame, drawn at SSx and downsampled."""
    s = size * SS
    small = size <= 16

    rim_w = scaled(1.0 if small else size / 30.0)
    link_w = scaled(1.0 if small else max(1.0, size / 22.0))

    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    box = round(s * (0.86 if small else 0.88))
    img.alpha_composite(swarm(box, small, link_w, rim_w), (0, 0))

    b = round(s * (0.50 if small else 0.46))
    tile = badge(round(b * 0.86), b, rim_w)
    img.alpha_composite(tile, (s - tile.width, s - tile.height))

    return img.resize((size, size), Image.LANCZOS)


def main():
    frames = [render(n) for n in SIZES]
    write_ico(OUT, frames)
    report(ROOT, OUT)


if __name__ == "__main__":
    main()
