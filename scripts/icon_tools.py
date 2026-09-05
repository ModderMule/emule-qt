#!/usr/bin/env python3
"""Shared pieces of the .ico generators.

Every toolbar icon this project generates is drawn at SS x supersampling, once
per frame size, and packed into a hand-written ICONDIR. Both of those live here
so `generate_usenet_icon.py` and `generate_downloads_folder_icon.py` cannot
drift apart.

Not a script -- import it.
"""

import struct
from io import BytesIO

# The toolbar asks for 32 (64 on a retina display); 16 and 48 cover menus,
# list widgets and Windows' larger shell views.
SIZES = (16, 32, 48, 64)

SS = 4  # supersampling factor


def scaled(final_px):
    """A stroke width given in final pixels, in supersampled pixels."""
    return max(1, round(final_px * SS))


def write_ico(path, frames):
    """Write the ICONDIR by hand.

    Pillow's ICO writer resizes one image to every requested size; it has no way
    to take per-size art, which is the whole point of these generators. The
    container is six bytes of header plus a sixteen-byte entry each, so this is
    simpler than fighting it.
    """
    blobs = []
    for frame in frames:
        buf = BytesIO()
        frame.save(buf, format="PNG")   # Qt and Windows Vista+ both read PNG frames
        blobs.append(buf.getvalue())

    out = bytearray(struct.pack("<HHH", 0, 1, len(frames)))
    offset = 6 + 16 * len(frames)
    for frame, blob in zip(frames, blobs):
        out += struct.pack("<BBBBHHII",
                           0 if frame.width >= 256 else frame.width,
                           0 if frame.height >= 256 else frame.height,
                           0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    for blob in blobs:
        out += blob

    path.write_bytes(bytes(out))


def report(root, out, sizes=SIZES):
    """The one line every generator prints when it is done."""
    print(f"wrote {out.relative_to(root)} ({out.stat().st_size} bytes, "
          f"frames: {', '.join(f'{n}x{n}' for n in sizes)})")
