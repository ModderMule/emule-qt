#!/usr/bin/env python3
"""Splash screen from the avatar donkey (docs/avatars/avatar.svg look).

Dark avatar gradient, green network nodes, donkey in the green avatar ring,
"eMule Qt" wordmark + URL on the left. The bottom strip stays empty: the GUI
draws version + copyright there at runtime (like MFC CSplashScreen).

Writes:
  docs/avatars/splash.svg                 vector source
  resources/images/Splash.png             350x300, MFC splash size
  resources/images/Splash@2x.png          QPixmap picks these on HiDPI
  resources/images/Splash@3x.png

Needs cairosvg + Pillow, and libcairo on the loader path on macOS:
  DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib ~/.venvs/py/bin/python \
      scripts/generate_splash.py
"""

import re
import sys
from pathlib import Path

from generate_donkey_icons import donkey_svg

try:
    import cairosvg
except ImportError as e:
    sys.exit(f"{e}: pip install cairosvg (and libcairo)")

ROOT = Path(__file__).resolve().parent.parent
IMAGES = ROOT / "resources" / "images"
SVG_OUT = ROOT / "docs" / "avatars" / "splash.svg"

# logical size = MFC Logo.jpg; drawing units are 2x
W, H = 350, 300
U = 2
SCALES = (1, 2, 3)

FONT = 'font-family="Helvetica Neue, Arial, sans-serif"'
GREEN = "#41cd52"


def donkey(x, y, size):
    """Avatar donkey as a nested <svg>, ids prefixed so it can be embedded."""
    s = donkey_svg("connected")
    vb = re.search(r'viewBox="([^"]+)"', s).group(1)
    inner = s.split(">", 1)[1].rsplit("</svg>", 1)[0]
    inner = inner.replace('id="', 'id="d_').replace("url(#", "url(#d_")
    return f'<svg x="{x}" y="{y}" width="{size}" height="{size}" viewBox="{vb}">{inner}</svg>'


def nodes():
    pts = [(60, 150), (130, 95), (200, 40), (70, 240), (440, 170), (470, 250),
           (400, 80), (560, 60), (640, 140), (610, 260)]
    edges = [(0, 1), (1, 2), (0, 3), (4, 5), (4, 6), (6, 7), (7, 8), (8, 9), (5, 9)]
    lines = "".join(f'<line x1="{pts[a][0]}" y1="{pts[a][1]}" x2="{pts[b][0]}" y2="{pts[b][1]}"/>'
                    for a, b in edges)
    dots = "".join(f'<circle cx="{x}" cy="{y}" r="{7 if i % 2 else 9}"/>'
                   for i, (x, y) in enumerate(pts))
    return (f'<g stroke="{GREEN}" stroke-opacity="0.22" stroke-width="3" fill="{GREEN}" '
            f'fill-opacity="0.35">{lines}{dots}</g>')


def wordmark(x, y, size):
    # two texts: cairosvg spaces a tspan after letter-spacing badly
    w1, gap = 2.62 * size, 0.18 * size
    return (f'<text x="{x}" y="{y}" {FONT} font-size="{size}" font-weight="800" fill="#ffffff" '
            f'letter-spacing="-2">eMule</text>'
            f'<text x="{x + w1 + gap}" y="{y}" {FONT} font-size="{size}" font-weight="700" '
            f'fill="{GREEN}">Qt</text>')


def splash_svg():
    w, h = W * U, H * U
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">
  <defs>
    <radialGradient id="bg" cx="40%" cy="35%" r="85%">
      <stop offset="0" stop-color="#2b3b4d"/>
      <stop offset="1" stop-color="#0d141c"/>
    </radialGradient>
  </defs>
  <rect width="{w}" height="{h}" fill="url(#bg)"/>
  {nodes()}
  <circle cx="500" cy="290" r="190" fill="none" stroke="{GREEN}" stroke-width="10" stroke-opacity="0.9"/>
  <circle cx="500" cy="290" r="203" fill="none" stroke="#2f8cff" stroke-width="3" stroke-opacity="0.35"/>
  {donkey(300, 70, 400)}
  {wordmark(40, 300, 64)}
  <text x="44" y="345" {FONT} font-size="22" fill="#8fa3b8">www.emule-qt.org</text>
  <rect x="44" y="366" width="170" height="6" rx="3" fill="{GREEN}"/>
</svg>
'''


def main():
    svg = splash_svg()
    SVG_OUT.write_text(svg)
    print(f"wrote {SVG_OUT.relative_to(ROOT)}")
    for n in SCALES:
        out = IMAGES / ("Splash.png" if n == 1 else f"Splash@{n}x.png")
        cairosvg.svg2png(bytestring=svg.encode(), write_to=str(out),
                         output_width=W * n, output_height=H * n)
        print(f"wrote {out.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
