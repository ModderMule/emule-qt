#!/usr/bin/env python3
"""Generate CSS sprite sheets from eMule web server GIF icons.

Groups GIFs by prefix into horizontal strip PNGs and generates sprites.css.
Layout images (login_*, main_*, logo.jpg, favicon.ico) are kept as individual files.

Usage: python3 scripts/generate_sprites.py
"""

import os
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Pillow not installed. Install with: pip3 install Pillow")
    sys.exit(1)

WEBSERVER_DIR = Path(__file__).parent.parent / "data" / "webserver"

# Sprite group definitions: (sprite_filename, list of prefix patterns)
SPRITE_GROUPS = [
    ("sprite-progress.png", ["p_", "black.", "blue", "green.", "greenpercent.", "red.", "yellow."]),
    ("sprite-status.png", ["t_", "connecting.", "complete.", "completing.", "disconnected.", "failed."]),
    ("sprite-toolbar.png", ["l_"]),
    ("sprite-header.png", ["h_"]),
    ("sprite-stats.png", ["stats_"]),
    ("sprite-filetype.png", ["filetype_", "file.", "filedown."]),
    ("sprite-misc.png", [
        "ct_", "is_", "m_", "qs_", "arrow_", "checked", "high.", "low.",
        "add_server.", "transparent."
    ]),
]

# Files to skip (served as individual files)
SKIP_PREFIXES = ["login_", "main_", "logo.", "favicon."]


def matches_group(filename, prefixes):
    """Check if filename matches any of the given prefixes."""
    for prefix in prefixes:
        if prefix.endswith("."):
            if filename.startswith(prefix[:-1]) and "." in filename:
                base = filename.split(".")[0]
                if base == prefix[:-1]:
                    return True
        elif filename.startswith(prefix):
            return True
    return False


def should_skip(filename):
    """Check if file should be kept as individual (not sprited)."""
    for prefix in SKIP_PREFIXES:
        if prefix.endswith("."):
            if filename.startswith(prefix[:-1]):
                return True
        elif filename.startswith(prefix):
            return True
    return False


def generate_sprites():
    """Generate sprite sheets and CSS."""
    gif_files = sorted(WEBSERVER_DIR.glob("*.gif"))
    if not gif_files:
        print(f"No GIF files found in {WEBSERVER_DIR}")
        return

    css_rules = []
    css_rules.append("/* Auto-generated sprite CSS — do not edit manually */")
    css_rules.append(".icon { display: inline-block; background-repeat: no-repeat; }")
    css_rules.append("")

    total_sprites = 0

    for sprite_name, prefixes in SPRITE_GROUPS:
        # Collect matching files
        group_files = []
        for gif in gif_files:
            if should_skip(gif.name):
                continue
            if matches_group(gif.name, prefixes):
                group_files.append(gif)

        if not group_files:
            continue

        # Load images
        images = []
        for f in group_files:
            try:
                img = Image.open(f)
                img = img.convert("RGBA")
                images.append((f.stem, img))
            except Exception as e:
                print(f"  Warning: could not load {f.name}: {e}")

        if not images:
            continue

        # Create horizontal strip
        max_height = max(img.height for _, img in images)
        total_width = sum(img.width for _, img in images)

        sprite = Image.new("RGBA", (total_width, max_height), (0, 0, 0, 0))

        x_offset = 0
        for name, img in images:
            # Center vertically
            y_offset = (max_height - img.height) // 2
            sprite.paste(img, (x_offset, y_offset))

            # Generate CSS rule
            css_rules.append(
                f".icon-{name} {{ background: url({sprite_name}) -{x_offset}px 0; "
                f"width: {img.width}px; height: {img.height}px; }}"
            )

            x_offset += img.width
            total_sprites += 1

        # Save sprite sheet
        sprite_path = WEBSERVER_DIR / sprite_name
        sprite.save(sprite_path, "PNG", optimize=True)
        print(f"  Created {sprite_name}: {len(images)} icons, {total_width}x{max_height}px")

    # Write CSS
    css_path = WEBSERVER_DIR / "sprites.css"
    css_path.write_text("\n".join(css_rules) + "\n")
    print(f"  Generated sprites.css with {total_sprites} icon rules")


if __name__ == "__main__":
    print("Generating CSS sprite sheets...")
    generate_sprites()
    print("Done.")
