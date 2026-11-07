#!/usr/bin/env python3
"""Rebuild resources/flags from flagpedia.net (public domain, Wikimedia Commons SVGs).

Each flag becomes <cc>.png (18x16), <cc>@2x.png and <cc>@3x.png: the flag with a
thin dark frame, 12 px tall incl. frame, centred on a transparent 18x16 canvas
like the MorphXT icons. Codes flagpedia lacks (old ISO codes, IP database
placeholders) are converted from the existing MorphXT .ico at 1x only.

Needs ImageMagick (`magick`) and network access to flagcdn.com.
Usage: scripts/update_flags.py [--cache DIR] [--keep-ico]
"""

import argparse
import json
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FLAGS = ROOT / "resources" / "flags"
QRC = ROOT / "resources" / "flags.qrc"
CDN = "https://flagcdn.com"

CANVAS = (18, 16)       # logical icon size
INNER_H = 10            # flag height inside the frame at 1x
INNER_MAX_W = 16        # widest flag inside the frame at 1x
FRAME = "#000000A0"     # MorphXT draws black; slightly softened for dark themes
SCALES = (1, 2, 3)

# our file name -> flagpedia code
RENAMED = {"eng": "gb-eng", "sco": "gb-sct", "wls": "gb-wls", "nir": "gb-nir"}
# served under a second name (qrc alias)
ALIASES = {"uk": "gb"}


def fetch(url: str, dest: Path) -> None:
    if dest.exists() and dest.stat().st_size > 0:
        return
    req = urllib.request.Request(url, headers={"User-Agent": "eMuleQt-flag-update"})
    with urllib.request.urlopen(req, timeout=30) as r:
        dest.write_bytes(r.read())


def image_size(path: Path) -> tuple[int, int]:
    out = subprocess.run(["magick", "identify", "-format", "%w %h", f"{path}[0]"],
                         check=True, capture_output=True, text=True).stdout.split()
    return int(out[0]), int(out[1])


def render(src: Path, dest: Path, scale: int) -> None:
    w, h = image_size(src)
    inner_h = INNER_H * scale
    inner_w = min(INNER_MAX_W * scale, round(inner_h * w / h))
    b = scale  # frame width: one logical pixel
    cw, ch = CANVAS[0] * scale, CANVAS[1] * scale
    subprocess.run([
        "magick", src, "-colorspace", "sRGB", "-filter", "Lanczos",
        "-resize", f"{inner_w}x{inner_h}!",
        "-bordercolor", FRAME, "-border", str(b),
        "-background", "none", "-gravity", "center", "-extent", f"{cw}x{ch}",
        "-strip", "-define", "png:color-type=6", dest,
    ], check=True)


def write_qrc() -> None:
    lines = [
        "<!DOCTYPE RCC>",
        '<RCC version="1.0">',
        "    <!-- Country flags (18x16 + @2x/@3x, ISO 3166-1 alpha-2, lower case) from",
        "         flagpedia.net, public domain; rebuilt by scripts/update_flags.py -->",
        '    <qresource prefix="/flags">',
    ]
    files = sorted(p.name for p in FLAGS.glob("*.png"))
    for f in files:
        lines.append(f'        <file alias="{f}">flags/{f}</file>')
    for alias, target in sorted(ALIASES.items()):
        for f in files:
            stem = f.removesuffix(".png")
            base, _, suffix = stem.partition("@")
            if base == target:
                name = alias + (f"@{suffix}" if suffix else "") + ".png"
                lines.append(f'        <file alias="{name}">flags/{f}</file>')
    lines += ["    </qresource>", "</RCC>", ""]
    QRC.write_text("\n".join(lines))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", type=Path, help="download dir (default: temp)")
    ap.add_argument("--keep-ico", action="store_true", help="don't delete the old .ico files")
    args = ap.parse_args()

    cache = args.cache or Path(tempfile.mkdtemp(prefix="flagpedia-"))
    cache.mkdir(parents=True, exist_ok=True)

    fetch(f"{CDN}/en/codes.json", cache / "codes.json")
    codes = set(json.loads((cache / "codes.json").read_text()))
    existing = {p.stem for p in FLAGS.glob("*.ico")} | {
        p.stem.partition("@")[0] for p in FLAGS.glob("*.png")}

    # every two-letter code flagpedia has, plus our extra names it can map
    wanted = {c: c for c in codes if len(c) == 2}
    wanted |= {ours: theirs for ours, theirs in RENAMED.items() if theirs in codes}
    legacy = sorted(existing - wanted.keys() - ALIASES.keys())

    for i, (ours, theirs) in enumerate(sorted(wanted.items()), 1):
        src = cache / f"{theirs}.png"
        fetch(f"{CDN}/h120/{theirs}.png", src)
        for s in SCALES:
            suffix = "" if s == 1 else f"@{s}x"
            render(src, FLAGS / f"{ours}{suffix}.png", s)
        print(f"\r{i}/{len(wanted)} {ours}   ", end="", flush=True)
    print()

    for cc in legacy:
        ico = FLAGS / f"{cc}.ico"
        if ico.exists():
            subprocess.run(["magick", f"{ico}[0]", "-strip", FLAGS / f"{cc}.png"], check=True)
        else:
            print(f"warning: no source for legacy flag {cc}", file=sys.stderr)
    print("kept from MorphXT:", " ".join(legacy))

    if not args.keep_ico:
        for ico in FLAGS.glob("*.ico"):
            ico.unlink()

    write_qrc()
    print(f"wrote {QRC.relative_to(ROOT)}; cache: {cache}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
