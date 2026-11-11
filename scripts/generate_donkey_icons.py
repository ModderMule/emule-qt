#!/usr/bin/env python3
"""Tray and dock icons from the dev avatar donkey (docs/avatars/avatar.svg).

The avatar's donkey alone: no ring, no background, no dev badge. Three moods:

  connected     the avatar donkey, ears up
  lowid         ears drooped + sleepy eye (or blindfold, --lowid=blindfold)
  disconnected  the connected donkey in greys

Writes:
  docs/avatars/donkey-<mood>.svg                vector sources (web, skins)
  resources/icons/TrayConnected.ico             tray, read through skinIcon()
  resources/icons/TrayLowID.ico
  resources/icons/TrayDisconnected.ico
  resources/icons/app/emuleqt-<N>.png           app/dock icon (Linux hicolor too)
  resources/icons/app/emuleqt-lowid-<N>.png     dock icon while LowID
  resources/icons/MuleApp.ico                   embedded by resources/appicon.rc
  resources/icons/Mule.icns                     macOS bundle icon (needs iconutil)

Needs cairosvg + Pillow, and libcairo on the loader path on macOS:
  DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib ~/.venvs/py/bin/python \
      scripts/generate_donkey_icons.py [--preview out.png]
"""

import argparse
import shutil
import subprocess
import sys
import tempfile
from io import BytesIO
from pathlib import Path

from icon_tools import report, write_ico

try:
    import cairosvg
    from PIL import Image
except ImportError as e:
    sys.exit(f"{e}: pip install cairosvg Pillow (and libcairo)")

ROOT = Path(__file__).resolve().parent.parent
ICONS = ROOT / "resources" / "icons"
SVG_DIR = ROOT / "docs" / "avatars"
APP_DIR = ICONS / "app"

TRAY_SIZES = (16, 20, 24, 32, 40, 48, 64)
APP_PNG_SIZES = (16, 32, 48, 64, 128, 256)
APP_ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)
ICNS_SIZES = (16, 32, 64, 128, 256, 512, 1024)

COLOR = {
    "fur0": "#e08a3c", "fur1": "#b8611f", "fur2": "#7a3c10",
    "ear0": "#f3b27a", "ear1": "#c77432",
    "mane": "#8a4513", "line": "#3a1a05",
    "muz0": "#ffffff", "muz1": "#d9dde2", "muz2": "#9aa2ab", "muzLine": "#2b3036",
    "nostril": "#4a5058", "cheek": "#ffd9b0", "cheekOp": "0.45",
    "band0": "#2f3b48", "band1": "#141b22",
}
GREY = {
    **COLOR,
    "fur0": "#b4b4b4", "fur1": "#8c8c8c", "fur2": "#5e5e5e",
    "ear0": "#d6d6d6", "ear1": "#9c9c9c",
    "mane": "#5c5c5c", "line": "#262626",
    "cheekOp": "0",
}

# Ear shapes from the avatar, pivot = where the ear leaves the head.
BACK_EAR = ("M178 250 C145 175 150 95 192 62 C222 105 232 180 226 238 Z",
            "M190 225 C170 170 172 120 192 92 C208 125 212 175 210 215 Z")
FRONT_EAR = ("M214 236 C246 152 330 108 432 100 C400 150 330 214 262 258 Z",
             "M238 222 C268 164 330 128 398 118 C368 156 318 196 266 232 Z")


def ear(paths, rotate=0, pivot=(0, 0)):
    xf = f' transform="rotate({rotate} {pivot[0]} {pivot[1]})"' if rotate else ""
    return (f'<g{xf}><path d="{paths[0]}" fill="url(#fur)"/>'
            f'<path d="{paths[1]}" fill="url(#earIn)" stroke="none"/></g>')


def eye(mood):
    if mood == "lowid":
        # half-shut lid, pupil sunk low, brow slanting down to the back
        return """
    <ellipse cx="284" cy="268" rx="42" ry="34" transform="rotate(-12 284 268)" fill="#ffffff"/>
    <circle cx="300" cy="282" r="15" fill="#111" stroke="none"/>
    <path d="M240 270 C246 238 300 226 326 252 C330 260 328 268 324 272 C300 262 268 262 240 270 Z"
          fill="url(#fur)"/>
    <path d="M244 272 C272 262 302 262 326 270" fill="none" stroke-width="7"/>
    <path d="M242 226 Q282 234 322 216" fill="none" stroke-width="7"/>"""
    return """
    <ellipse cx="284" cy="268" rx="42" ry="34" transform="rotate(-12 284 268)" fill="#ffffff"/>
    <circle cx="302" cy="270" r="17" fill="#111" stroke="none"/>
    <circle cx="308" cy="263" r="5" fill="#fff" stroke="none"/>
    <path d="M238 238 Q280 214 326 234" fill="none" stroke-width="7"/>"""


BLINDFOLD = """
    <path d="M146 262 C200 238 280 222 344 222 L350 290 C282 292 206 304 150 326 Z"
          fill="url(#band)"/>
    <path d="M150 300 C126 304 104 318 92 340 C112 336 130 330 152 318 Z" fill="url(#band)"/>
    <path d="M150 288 C122 278 98 280 80 292 C104 302 128 304 150 302 Z" fill="url(#band)"/>
    <path d="M160 276 C220 256 286 244 340 242" fill="none" stroke="#ffffff"
          stroke-opacity="0.18" stroke-width="5"/>"""


def donkey_svg(mood="connected", lowid_style="ears", pad=0.0, stroke=6.0):
    """The avatar donkey alone. @p pad is extra margin as a share of the side."""
    c = GREY if mood == "disconnected" else COLOR
    sad = mood == "lowid"
    blind = sad and lowid_style == "blindfold"
    droop = sad and lowid_style == "ears"

    if droop:
        back = ear(BACK_EAR, -128, (204, 244))
        front = ear(FRONT_EAR, -188, (236, 248))
    else:
        back = ear(BACK_EAR)
        front = ear(FRONT_EAR)

    # tight box around the drawing, then square + pad
    # same box for every mood so the dock icon does not jump on a swap;
    # height sets the side, the drooped ear tip still fits on the left
    x0, y0, x1, y1 = (96, 52, 470, 480)
    side = max(x1 - x0, y1 - y0)
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    side *= 1 + 2 * pad
    vb = f"{cx - side / 2:.1f} {cy - side / 2:.1f} {side:.1f} {side:.1f}"

    eye_svg = "" if blind else eye(mood)
    band = BLINDFOLD if blind else ""

    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vb}">
  <defs>
    <linearGradient id="fur" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="{c['fur0']}"/>
      <stop offset="0.55" stop-color="{c['fur1']}"/>
      <stop offset="1" stop-color="{c['fur2']}"/>
    </linearGradient>
    <linearGradient id="earIn" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="{c['ear0']}"/>
      <stop offset="1" stop-color="{c['ear1']}"/>
    </linearGradient>
    <radialGradient id="muzzle" cx="40%" cy="35%" r="70%">
      <stop offset="0" stop-color="{c['muz0']}"/>
      <stop offset="0.6" stop-color="{c['muz1']}"/>
      <stop offset="1" stop-color="{c['muz2']}"/>
    </radialGradient>
    <linearGradient id="band" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="{c['band0']}"/>
      <stop offset="1" stop-color="{c['band1']}"/>
    </linearGradient>
  </defs>
  <g stroke="{c['line']}" stroke-width="{stroke}" stroke-linejoin="round" stroke-linecap="round">
    <!-- neck, rounded off instead of running out of the frame -->
    <path d="M165 370 C150 410 144 440 148 458 Q232 494 316 462 C312 440 314 424 320 400 Z"
          fill="url(#fur)"/>
    {back}
    <!-- head -->
    <path d="M148 305 C138 232 196 196 262 206 C322 215 352 246 364 280 L372 392 C312 438 212 432 176 392 C158 370 151 338 148 305 Z"
          fill="url(#fur)"/>
    <!-- mane tuft -->
    <path d="M150 262 L118 250 L146 236 L112 214 L152 212 L128 186 L170 196 L176 232 Z"
          fill="{c['mane']}"/>
    {front}
    <!-- muzzle -->
    <ellipse cx="376" cy="352" rx="88" ry="74" fill="url(#muzzle)" stroke="{c['muzLine']}"/>
    <ellipse cx="412" cy="318" rx="14" ry="9" transform="rotate(-20 412 318)"
             fill="{c['nostril']}" stroke="none"/>
    <path d="{'M338 394 Q364 382 394 392' if sad else 'M336 384 Q362 400 394 388'}"
          fill="none" stroke="{c['nostril']}" stroke-width="5"/>
    {eye_svg}
    <ellipse cx="200" cy="330" rx="18" ry="12" fill="{c['cheek']}"
             fill-opacity="{c['cheekOp']}" stroke="none"/>
    {band}
  </g>
</svg>
"""


def render(svg, size):
    png = cairosvg.svg2png(bytestring=svg.encode(), output_width=size, output_height=size)
    return Image.open(BytesIO(png)).convert("RGBA")


def tray_svg(mood, style):
    # thicker outline so the shape survives 16 px
    return donkey_svg(mood, style, pad=0.0, stroke=9)


def tray_frame(mood, style, size):
    svg = tray_svg(mood, style) if size <= 32 else donkey_svg(mood, style)
    return render(svg, size)


def dock_frame(mood, style, size):
    # macOS leaves ~10 % air round the art; small sizes keep it tight
    pad = 0.06 if size >= 128 else 0.0
    return render(donkey_svg(mood, style, pad=pad, stroke=6 if size >= 64 else 9), size)


def write_icns(style):
    if not shutil.which("iconutil"):
        print("iconutil missing, Mule.icns left alone")
        return
    with tempfile.TemporaryDirectory() as tmp:
        iconset = Path(tmp) / "Mule.iconset"
        iconset.mkdir()
        for n in ICNS_SIZES[:-1]:
            dock_frame("connected", style, n).save(iconset / f"icon_{n}x{n}.png")
            dock_frame("connected", style, n * 2).save(iconset / f"icon_{n}x{n}@2x.png")
        out = ICONS / "Mule.icns"
        subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(out)], check=True)
        print(f"wrote {out.relative_to(ROOT)} ({out.stat().st_size} bytes)")


def preview(path, style):
    moods = ("connected", "lowid", "disconnected")
    sizes = (16, 22, 32, 64, 128)
    w = sum(sizes) + 20 * (len(sizes) + 1)
    sheet = Image.new("RGBA", (w * 2, (128 + 20) * len(moods) + 20), "#ececec")
    dark = Image.new("RGBA", (w, sheet.height), "#1e1e1e")
    sheet.paste(dark, (w, 0))
    for row, mood in enumerate(moods):
        y = 20 + row * 148
        for bg in (0, w):
            x = bg + 20
            for n in sizes:
                img = tray_frame(mood, style, n) if n <= 64 else dock_frame(mood, style, n)
                sheet.alpha_composite(img, (x, y + (128 - n) // 2))
                x += n + 20
    sheet.save(path)
    print(f"preview {path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lowid", choices=("ears", "blindfold"), default="ears")
    ap.add_argument("--preview", help="only write a preview sheet to this PNG")
    args = ap.parse_args()
    style = args.lowid

    if args.preview:
        preview(args.preview, style)
        return

    SVG_DIR.mkdir(exist_ok=True)
    for mood in ("connected", "lowid", "disconnected"):
        (SVG_DIR / f"donkey-{mood}.svg").write_text(donkey_svg(mood, style))

    tray = {"connected": "TrayConnected.ico", "lowid": "TrayLowID.ico",
            "disconnected": "TrayDisconnected.ico"}
    for mood, name in tray.items():
        out = ICONS / name
        write_ico(out, [tray_frame(mood, style, n) for n in TRAY_SIZES])
        report(ROOT, out, TRAY_SIZES)

    APP_DIR.mkdir(exist_ok=True)
    for n in APP_PNG_SIZES:
        for mood, suffix in (("connected", ""), ("lowid", "-lowid")):
            out = APP_DIR / f"emuleqt{suffix}-{n}.png"
            dock_frame(mood, style, n).save(out, format="PNG", optimize=True)
    print(f"wrote {APP_DIR.relative_to(ROOT)}/emuleqt[-lowid]-<N>.png")

    # rc.exe takes PNG only for the 256px frame
    out = ICONS / "MuleApp.ico"
    write_ico(out, [dock_frame("connected", style, n) for n in APP_ICO_SIZES], png_from=256)
    report(ROOT, out, APP_ICO_SIZES)

    write_icns(style)


if __name__ == "__main__":
    main()
