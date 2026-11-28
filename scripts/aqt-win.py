#!/usr/bin/env python3
"""aqt wrapper for Windows CI.

Qt moved Windows >= 6.8 repos to per-arch folders
(qt6_6121/qt6_6121_msvc2022_64/) but aqt 3.3.0 still asks qt6_6121/qt6_6121/.
Try aqt's path first, fall back to the per-arch one. Usage: same args as aqt.
"""
import sys

from aqt.__main__ import main
from aqt.archives import QtArchives
from aqt.metadata import Version

_orig = QtArchives._get_archives


def _get_archives(self):
    try:
        return _orig(self)
    except Exception:
        if self.os_name != "windows" or self.version < Version("6.8.0"):
            raise
    ver = f"qt{self.version.major}_{self._version_str()}"
    arch = self.arch.removeprefix("win64_")
    self.logger.info(f"retrying per-arch repo folder {ver}/{ver}_{arch}")
    self._get_archives_base(f"{ver}/{ver}_{arch}", self._target_packages())


QtArchives._get_archives = _get_archives

if __name__ == "__main__":
    sys.argv[0] = "aqt"
    sys.exit(main())
