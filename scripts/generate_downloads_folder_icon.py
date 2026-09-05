#!/usr/bin/env python3
"""Generate resources/icons/DownloadsFolder.ico -- an open folder taking an arrow.

The "Downloads Folder" toolbar button needs a 32px frame (64 on a retina
display). Every folder icon inherited from the MFC set -- FolderOpen.ico,
Folders.ico, Incoming.ico -- carries 16x16 art only, so pointing the button at
one of them would upscale it 4x and it would read a size smaller than its
neighbours.

Palette and shape follow the originals: FolderOpen.ico's amber, and the green
down arrow of Transfer.ico / Download.ico, so the toolbar reads as one family.

Drawn at 4x supersampling, one frame per size: at 16px the fold line and the
arrow's outline turn to mud, so that frame drops both.

Usage: python3 scripts/generate_downloads_folder_icon.py
"""

import sys
from pathlib import Path

from icon_tools import SIZES, SS, report, scaled, write_ico

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("Pillow is required: pip3 install Pillow")

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "resources" / "icons" / "DownloadsFolder.ico"

# Amber sampled from FolderOpen.ico (185,134,0 .. 255,233,88); the back panel
# sits a shade darker so the open folder reads as two surfaces at 16px.
FRONT_LIGHT = (255, 226, 118)
FRONT_DARK = (238, 176, 12)
BACK_LIGHT = (226, 176, 30)
BACK_DARK = (188, 136, 0)
FOLDER_RIM = (120, 82, 0)

# Green from Transfer.ico's down arrow, given a gradient.
ARROW_LIGHT = (126, 226, 96)
ARROW_DARK = (28, 138, 44)
ARROW_RIM = (16, 82, 24)


def vgradient(w, h, top, bottom):
    """A w x h vertical gradient, as its own RGB image."""
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        t = y / max(1, h - 1)
        row = tuple(round(a + (b - a) * t) for a, b in zip(top, bottom))
        for x in range(w):
            px[x, y] = row
    return img


def shape(size, points, top, bottom, rim, rim_w):
    """One filled polygon with a vertical gradient and an outline."""
    layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).polygon(points, fill=255)
    layer.paste(vgradient(size, size, top, bottom), (0, 0), mask)
    ImageDraw.Draw(layer).polygon(points, outline=rim, width=rim_w)
    return layer


def render(size):
    """One frame, drawn at SSx and downsampled."""
    s = size * SS
    detail = size >= 32
    rim_w = scaled(1.0 if size <= 16 else size / 30.0)

    def p(pts):
        return [(x * s, y * s) for x, y in pts]

    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))

    # -- back panel, with the tab, drawn as one outline ----------------------
    back = p([(0.04, 0.34), (0.04, 0.22), (0.40, 0.22), (0.47, 0.32),
              (0.95, 0.32), (0.95, 0.80), (0.04, 0.80)])
    img.alpha_composite(shape(s, back, BACK_LIGHT, BACK_DARK, FOLDER_RIM, rim_w))

    # -- the arrow, between the two panels so it drops *into* the folder -----
    arrow = p([(0.39, 0.05), (0.61, 0.05), (0.61, 0.31), (0.75, 0.31),
               (0.50, 0.60), (0.25, 0.31), (0.39, 0.31)])
    img.alpha_composite(
        shape(s, arrow, ARROW_LIGHT, ARROW_DARK, ARROW_RIM,
              rim_w if detail else max(1, rim_w // 2)))

    # -- front panel: wider at the bottom, the classic open folder ----------
    front = p([(0.17, 0.47), (1.00, 0.47), (0.87, 0.92), (0.00, 0.92)])
    img.alpha_composite(shape(s, front, FRONT_LIGHT, FRONT_DARK, FOLDER_RIM, rim_w))

    # A fold crease along the front panel's top edge. Pure decoration, and the
    # first thing to go at 16px.
    if detail:
        crease = Image.new("RGBA", (s, s), (0, 0, 0, 0))
        ImageDraw.Draw(crease).line(
            p([(0.19, 0.53), (0.97, 0.53)]), fill=(*FOLDER_RIM, 70), width=rim_w)
        img.alpha_composite(crease)

    return img.resize((size, size), Image.LANCZOS)


def main():
    frames = [render(n) for n in SIZES]
    write_ico(OUT, frames)
    report(ROOT, OUT)


if __name__ == "__main__":
    main()
