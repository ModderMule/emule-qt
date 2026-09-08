#!/usr/bin/env python3
"""Generate CSS sprite sheets from eMule web server GIF icons.

Groups GIFs by prefix into horizontal strip PNGs and generates sprites.css.
Layout images (login_*, main_*, logo.jpg, favicon.ico) are kept as individual files.

Source art lives in data/legacy/webserver/ and is read-only input: it is kept out
of data/config/ on purpose, because the bundlers copy that directory wholesale
into every DMG/tarball/zip and the app seeds it into every user's config dir.
Only the generated sheets belong there.

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

SOURCE_DIR = Path(__file__).parent.parent / "data" / "legacy" / "webserver"
OUTPUT_DIR = Path(__file__).parent.parent / "data" / "config" / "webserver"
ICONS_DIR = Path(__file__).parent.parent / "resources" / "icons"

# The comment/rating marks, built straight from the GUI's .ico resources into a
# sheet of their own. Separate from SPRITE_GROUPS on purpose: that generator
# rewrites the whole of sprites.css from the source GIFs, and those GIFs are no
# longer in the tree (see the note above), so touching it would drop every rule
# whose art is missing. This pair regenerates from files that *are* present.
#
# "rating_none" is a deliberate blank: the template always emits the mark span,
# and an unrated row is by far the common case.
RATING_SPRITE = "sprite-rating.png"
RATING_CSS = "sprite-rating.css"
RATING_ICONS = [
    ("rating_none", None),                  # transparent placeholder
    ("rating_0", "FileRating0.ico"),        # not rated (has a comment only)
    ("rating_1", "FileRating1.ico"),        # Invalid / Corrupt / Fake
    ("rating_2", "FileRating2.ico"),        # Poor
    ("rating_3", "FileRating3.ico"),        # Fair
    ("rating_4", "FileRating4.ico"),        # Good
    ("rating_5", "FileRating5.ico"),        # Excellent
    ("rating_search", "emuleCollSearch.ico"),  # a Kad note lookup is running
    ("rating_fake", "RatingBad.ico"),       # the red "not what it claims" mark
]
RATING_ICON_PX = 16

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
SKIP_PREFIXES = ["login_", "main_", "logo.", "favicon.", "stats_back."]

# Fewer sprite-able GIFs than this means the source set is not really present.
# The full original set is 174 files, restored to data/legacy/webserver/.
MIN_SOURCE_GIFS = 100


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
    gif_files = sorted(SOURCE_DIR.glob("*.gif"))
    if not gif_files:
        print(f"No GIF files found in {SOURCE_DIR}")
        return

    # This function rewrites the whole of sprites.css from whatever GIFs it finds,
    # so a partial set silently deletes every rule whose art is absent -- and the
    # source GIFs were removed from the tree once the sheets were generated (see
    # the module note). Refuse rather than truncate; restore the GIFs first.
    layout_gifs = {g.name for g in gif_files if should_skip(g.name)}
    if len(gif_files) - len(layout_gifs) < MIN_SOURCE_GIFS:
        print(f"  Refusing to regenerate sprites.css: only "
              f"{len(gif_files) - len(layout_gifs)} source GIFs present, expected "
              f"{MIN_SOURCE_GIFS}+.")
        print("  Restore them first, e.g. from the commit that deleted them.")
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
        sprite_path = OUTPUT_DIR / sprite_name
        sprite.save(sprite_path, "PNG", optimize=True)
        print(f"  Created {sprite_name}: {len(images)} icons, {total_width}x{max_height}px")

    # Write CSS
    css_path = OUTPUT_DIR / "sprites.css"
    css_path.write_text("\n".join(css_rules) + "\n")
    print(f"  Generated sprites.css with {total_sprites} icon rules")


def generate_rating_sprite():
    """Build sprite-rating.png + sprite-rating.css from the GUI .ico resources.

    Self-contained: reads nothing from SOURCE_DIR and never opens sprites.css,
    so it cannot clobber the pre-generated sheets.
    """
    images = []
    for name, ico in RATING_ICONS:
        if ico is None:
            images.append((name, Image.new("RGBA", (RATING_ICON_PX, RATING_ICON_PX), (0, 0, 0, 0))))
            continue
        path = ICONS_DIR / ico
        if not path.exists():
            print(f"  Warning: missing {path}")
            return
        img = Image.open(path).convert("RGBA")
        if img.size != (RATING_ICON_PX, RATING_ICON_PX):
            img = img.resize((RATING_ICON_PX, RATING_ICON_PX), Image.LANCZOS)
        images.append((name, img))

    sprite = Image.new("RGBA", (RATING_ICON_PX * len(images), RATING_ICON_PX), (0, 0, 0, 0))
    rules = [
        "/* Auto-generated by scripts/generate_sprites.py — do not edit manually */",
        "/* Comment/rating marks for the web UI, built from resources/icons/*.ico. */",
    ]
    for i, (name, img) in enumerate(images):
        x = i * RATING_ICON_PX
        sprite.paste(img, (x, 0))
        rules.append(
            f".icon-{name} {{ background: url({RATING_SPRITE}) -{x}px 0; "
            f"width: {RATING_ICON_PX}px; height: {RATING_ICON_PX}px; }}"
        )

    sprite.save(OUTPUT_DIR / RATING_SPRITE, "PNG", optimize=True)
    (OUTPUT_DIR / RATING_CSS).write_text("\n".join(rules) + "\n")
    print(f"  Created {RATING_SPRITE}: {len(images)} icons, "
          f"{RATING_ICON_PX * len(images)}x{RATING_ICON_PX}px")
    print(f"  Generated {RATING_CSS} with {len(images)} icon rules")


if __name__ == "__main__":
    print("Generating CSS sprite sheets...")
    generate_sprites()
    generate_rating_sprite()
    print("Done.")
