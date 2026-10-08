#!/usr/bin/env python3
"""Generate the application icon for Windows and Linux from the art we have.

resources/icons/Mule.ico is the classic donkey, but it stops at 64x64 and is
only ever loaded through Qt. Nothing puts an icon *into* emuleqt.exe, so
Explorer and the taskbar fall back to the generic one, and Linux has no
emuleqt.png for the `Icon=emuleqt` every .desktop file we write asks for.

Mule.icns carries the same donkey up to 512px. This takes the large frames from
there and everything up to 64px from Mule.ico's pixel art, and writes:

  resources/icons/MuleApp.ico            embedded by resources/appicon.rc
  resources/icons/app/emuleqt-<N>.png    installed into the hicolor theme

Usage: python3 scripts/generate_app_icon.py
"""

import struct
import sys
from io import BytesIO
from pathlib import Path

from icon_tools import report, write_ico

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: pip3 install Pillow")

ROOT = Path(__file__).resolve().parent.parent
ICONS = ROOT / "resources" / "icons"
OUT_ICO = ICONS / "MuleApp.ico"
OUT_PNG_DIR = ICONS / "app"

ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)
PNG_SIZES = (16, 32, 48, 64, 128, 256)

# Sizes Mule.ico has pixel art for. The icns is that 64px frame blown up and
# smoothed, so it is only the source for what is bigger than 64.
HAND_DRAWN = (16, 32, 64)


def icns_frames(path):
    """Every PNG frame in the icns, keyed by pixel width."""
    data = path.read_bytes()
    frames = {}
    offset = 8
    while offset + 8 <= len(data):
        length = struct.unpack(">I", data[offset + 4:offset + 8])[0]
        if length < 8:
            break
        blob = data[offset + 8:offset + length]
        if blob.startswith(b"\x89PNG"):
            img = Image.open(BytesIO(blob)).convert("RGBA")
            frames[img.width] = img
        offset += length
    return frames


def frame(size, large, small_ico):
    if size in HAND_DRAWN:
        # Ask for the 32-bit frame: the 16-colour one comes first in the file.
        return small_ico.getimage((size, size), 32).convert("RGBA")
    if size < max(HAND_DRAWN):
        return frame(max(HAND_DRAWN), large, small_ico).resize((size, size), Image.LANCZOS)
    if size in large:
        return large[size]
    # Smallest source that is still bigger, so nothing is ever scaled up.
    source = large[min(w for w in large if w > size)]
    return source.resize((size, size), Image.LANCZOS)


def main():
    large = icns_frames(ICONS / "Mule.icns")
    small_ico = Image.open(ICONS / "Mule.ico").ico

    # rc.exe takes PNG only for the 256px frame.
    write_ico(OUT_ICO, [frame(n, large, small_ico) for n in ICO_SIZES], png_from=256)
    report(ROOT, OUT_ICO, ICO_SIZES)

    OUT_PNG_DIR.mkdir(exist_ok=True)
    for n in PNG_SIZES:
        out = OUT_PNG_DIR / f"emuleqt-{n}.png"
        frame(n, large, small_ico).save(out, format="PNG", optimize=True)
        print(f"wrote {out.relative_to(ROOT)} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
