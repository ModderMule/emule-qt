#!/usr/bin/env python3
"""Generate resources/icons/Usenet.ico -- a globe with a newspaper badge.

The Usenet toolbar button used to borrow Global.ico, the eD2K "Global search"
globe. That file only carries 16x16 frames, so the 32px toolbar upscaled it 2x
(4x on a retina display) and the button read small and blurry next to twelve
icons drawn from native 32x32 art.

This draws fresh art at 4x supersampling and emits 16/32/48/64 frames. Each size
is drawn separately rather than downsampled from one master: at 16px the badge's
rule lines and the globe's graticule turn to mud, so that frame drops both.

Every translucent detail is drawn on its own layer and alpha-composited --
ImageDraw writes RGBA tuples straight into the buffer instead of blending them,
so drawing "faint" lines directly onto the globe punches holes in it.

Usage: python3 scripts/generate_usenet_icon.py
"""

import sys
from pathlib import Path

from icon_tools import SIZES, SS, report, scaled, write_ico

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("Pillow is required: pip3 install Pillow")

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "resources" / "icons" / "Usenet.ico"

# Sampled from Global.ico so the new globe sits in the same palette.
OCEAN_LIGHT = (128, 202, 240)
OCEAN_DARK = (23, 88, 158)
LAND_LIGHT = (122, 198, 92)
LAND_DARK = (49, 130, 54)
RIM = (14, 60, 104)
PAPER = (252, 252, 250)
PAPER_FOLD = (206, 208, 202)
INK = (108, 112, 118)
MASTHEAD = (72, 76, 84)
PAPER_RIM = (66, 70, 78)


def globe(d, detail, line_w, rim_w):
    """The globe as its own d x d RGBA image, rim included."""
    img = Image.new("RGBA", (d, d), (0, 0, 0, 0))

    # -- ocean: a radial gradient, light source upper-left ------------------
    grad = Image.new("RGB", (d, d))
    px = grad.load()
    cx, cy, r = d * 0.34, d * 0.30, d * 0.95
    for y in range(d):
        for x in range(d):
            t = min(1.0, ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5 / r) ** 0.8
            px[x, y] = tuple(
                round(a + (b - a) * t) for a, b in zip(OCEAN_LIGHT, OCEAN_DARK)
            )
    img.paste(grad, (0, 0))

    # -- land ---------------------------------------------------------------
    # Deliberately not a real map: these read as continents at 16px, which a
    # faithful coastline does not.
    land = [
        [(0.08, 0.34), (0.24, 0.22), (0.38, 0.30), (0.35, 0.47),
         (0.21, 0.55), (0.09, 0.47)],
        [(0.47, 0.14), (0.67, 0.14), (0.79, 0.27), (0.67, 0.36),
         (0.51, 0.33), (0.44, 0.23)],
        [(0.53, 0.50), (0.71, 0.46), (0.85, 0.57), (0.75, 0.74),
         (0.57, 0.79), (0.48, 0.65)],
        [(0.17, 0.66), (0.33, 0.63), (0.39, 0.77), (0.26, 0.87), (0.15, 0.77)],
    ]
    draw = ImageDraw.Draw(img)
    for i, poly in enumerate(land):
        draw.polygon([(px_ * d, py * d) for px_, py in poly],
                     fill=LAND_LIGHT if i % 2 == 0 else LAND_DARK)

    # -- graticule and highlight, blended -----------------------------------
    if detail:
        over = Image.new("RGBA", (d, d), (0, 0, 0, 0))
        od = ImageDraw.Draw(over)
        for f in (0.24, 0.50, 0.76):          # meridians, as narrowing ellipses
            half = abs(f - 0.5) * d
            od.ellipse([d * 0.5 - half if f != 0.5 else d * 0.5 - line_w * 0.5,
                        0, d * 0.5 + half if f != 0.5 else d * 0.5 + line_w * 0.5,
                        d - 1], outline=(*RIM, 64), width=line_w)
        for f in (0.28, 0.50, 0.72):          # parallels
            od.line([(0, d * f), (d, d * f)], fill=(*RIM, 56), width=line_w)
        img.alpha_composite(over)

    hl = Image.new("RGBA", (d, d), (0, 0, 0, 0))
    ImageDraw.Draw(hl).ellipse([d * 0.12, d * 0.09, d * 0.40, d * 0.29],
                               fill=(255, 255, 255, 105))
    img.alpha_composite(hl)

    # -- clip to the circle, then rim it ------------------------------------
    mask = Image.new("L", (d, d), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, d - 1, d - 1], fill=255)
    img.putalpha(mask)

    ImageDraw.Draw(img).ellipse([rim_w // 2, rim_w // 2,
                                 d - 1 - rim_w // 2, d - 1 - rim_w // 2],
                                outline=RIM, width=rim_w)
    return img


def badge(w, h, detail, line_w, rim_w):
    """A folded newspaper page as its own RGBA image, with a light halo."""
    pad = rim_w * 2                      # room for the halo stroke
    img = Image.new("RGBA", (w + pad * 2, h + pad * 2), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    page_w = max(1, round(rim_w * 0.75))

    x0, y0, x1, y1 = pad, pad, pad + w - 1, pad + h - 1
    fold = w * 0.30
    body = [(x0, y0), (x1 - fold, y0), (x1, y0 + fold), (x1, y1), (x0, y1)]

    # Halo first: the page has to separate from the globe behind it.
    d.polygon(body, fill=(255, 255, 255, 235), outline=(255, 255, 255, 235),
              width=rim_w * 2)
    d.polygon(body, fill=PAPER, outline=PAPER_RIM, width=page_w)
    d.polygon([(x1 - fold, y0), (x1, y0 + fold), (x1 - fold, y0 + fold)],
              fill=PAPER_FOLD, outline=PAPER_RIM, width=page_w)

    if detail:
        inset = w * 0.16
        d.rectangle([x0 + inset, y0 + h * 0.30, x1 - inset, y0 + h * 0.40],
                    fill=MASTHEAD)
        for i in range(3):
            y = y0 + h * (0.55 + i * 0.15)
            d.line([(x0 + inset, y), (x1 - inset, y)], fill=INK, width=line_w)

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

    b = round(s * 0.44)
    tile = badge(b, b, detail, line_w, rim_w)
    img.alpha_composite(tile, (s - tile.width, s - tile.height))

    return img.resize((size, size), Image.LANCZOS)


def main():
    frames = [render(n) for n in SIZES]
    write_ico(OUT, frames)
    report(ROOT, OUT)


if __name__ == "__main__":
    main()
