#!/usr/bin/env python3
"""Refresh Preferences::defaultBtI2pTrackers() from ngosang/trackerslist.

Usage: scripts/update-i2p-trackers.py
Also run by: scripts/publish-release.sh --update-i2p-trackers

Keeps http(s) announce URLs only: libtorrent can't announce to udp:// over SAM here.
Download is untrusted: anything but a plain .i2p URL fails the run, file stays as is.
"""

import re
import sys
import time
import urllib.request
from pathlib import Path

URL = "https://raw.githubusercontent.com/ngosang/trackerslist/master/trackers_all_i2p.txt"
ROOT = Path(__file__).resolve().parent.parent
PREFS = ROOT / "src/core/prefs/Preferences.cpp"
ROUTER = ROOT / "src/core/net/I2pRouter.h"
MIN_TRACKERS = 3  # fewer = broken upstream file, keep ours

TRACKER = re.compile(r"https?://([a-z0-9-]+(?:\.[a-z0-9-]+)*\.i2p)(?::\d{1,5})?(?:/[A-Za-z0-9._~/-]*)?")
BLOCK = re.compile(
    r"(QStringList Preferences::defaultBtI2pTrackers\(\)\n\{\n)"
    r"    // [^\n]*\n"
    r"    return \{\n(.*?)    \};\n",
    re.S,
)


def fetch() -> list[str]:
    with urllib.request.urlopen(URL, timeout=30) as reply:
        lines = reply.read(1 << 20).decode("utf-8").splitlines()
    out: list[str] = []
    for line in map(str.strip, lines):
        if not line or line.startswith("udp://"):
            continue
        if not TRACKER.fullmatch(line):
            raise ValueError(f"unexpected line in tracker list: {line!r}")
        if line not in out:
            out.append(line)
    if len(out) < MIN_TRACKERS:
        raise ValueError(f"only {len(out)} usable trackers upstream")
    return out


def main() -> int:
    try:
        trackers = fetch()
    except (OSError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    source = PREFS.read_text(encoding="utf-8")
    match = BLOCK.search(source)
    if not match:
        print(f"error: default list not found in {PREFS}", file=sys.stderr)
        return 1

    old = re.findall(r'QStringLiteral\("([^"]+)"\)', match.group(2))
    if old == trackers:
        print(f"I2P trackers up to date ({len(trackers)})")
        return 0

    body = "".join(f'        QStringLiteral("{t}"),\n' for t in trackers)
    block = (
        f"{match.group(1)}"
        f"    // ngosang/trackerslist trackers_all_i2p.txt as of {time.strftime('%Y-%m')}; udp:// ones left out\n"
        f"    return {{\n{body}    }};\n"
    )
    PREFS.write_text(source[: match.start()] + block + source[match.end() :], encoding="utf-8")

    for t in sorted(set(old) - set(trackers)):
        print(f"  - {t}")
    for t in sorted(set(trackers) - set(old)):
        print(f"  + {t}")
    print(f"I2P trackers updated: {len(old)} -> {len(trackers)}")

    # names need an address book entry, or a fresh router answers "invalid key" for a while
    seeded = set(re.findall(r'\{"([^"]+\.i2p)", "', ROUTER.read_text(encoding="utf-8")))
    for t in trackers:
        host = TRACKER.fullmatch(t).group(1)
        if not host.endswith(".b32.i2p") and host not in seeded:
            print(f"warning: {host} is not in I2pRouter::kSeedHosts", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
