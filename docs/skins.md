# Skins

Skin = `*.eMuleSkin.ini` profile (MFC format: `[Icons]`, `[Bitmaps]`, `[Colors]`) plus its files,
optionally a toolbar strip `*.eMuleToolbar.kad02.{png,bmp,gif}`. Parsed by `SkinEngine`,
toolbar strip by `ToolbarSkin`. Local folder: `<config>/skins/`.

## Published skins

Shipped skins live on the web server, not in the release bundles.

```
/pub/skins/<id>.zip               bundle as uploaded
/pub/skins/<id>.preview.<ext>     preview image cut out of the bundle (optional)
/pub/emuleqt-version.json         "skins" object, next to the binaries
```

`<id>` = ini file name without `.eMuleSkin.ini`, `[A-Za-z0-9][A-Za-z0-9._-]{0,63}`.

Manifest entry (written by `scripts/publish-skin.py`, parsed by `VersionManifest`):

```json
"skins": {
  "CyberneticNeon": {
    "name": "Cybernetic Neon",
    "url": "https://emule-qt.org/pub/skins/CyberneticNeon.zip",
    "sha256": "…", "size": 4949824,
    "date": "2026-11-06T13:41:40Z",
    "author": "…", "description": "…",
    "preview": { "url": "https://emule-qt.org/pub/skins/CyberneticNeon.preview.png",
                 "sha256": "…", "size": 322171 }
  }
}
```

`date` = zip mtime (UTC). `author`/`description`/`preview` optional. Entries without
url + sha256 + size are ignored by the GUI.

## Bundle layout

- exactly one `*.eMuleSkin.ini`, at the zip top or inside one top folder (Finder/Explorer zips)
- preview: `preview/overview.png`, else `[Skin] Preview=<relative path>` in the ini, else none
- optional `[Skin]` keys: `Name=`, `Author=`, `Description=`, `Preview=`
- toolbar strip anywhere in the bundle root or one folder below
- limits: zip ≤ 64 MiB, unpacked ≤ 256 MiB, ≤ 5000 entries, preview ≤ 8 MiB

```ini
[Skin]
Name=Cybernetic Neon
Author=someone
Preview=preview/overview.png
```

## Publishing

```
scripts/publish-skin.py SKIN.zip [SKIN2.zip ...]   # upload + manifest entry
scripts/publish-skin.py SKIN.zip --dry-run          # check zip, print entry
scripts/publish-skin.py --remove <id>               # unpublish
scripts/publish-skin.py --list
```

Same `.env` keys as `publish-release.sh` (`PUBLISH_SSH_*`, `PUBLISH_REMOTE_PATH`, optional
`PUBLISH_LOCAL_JSON`, `PUBLISH_BASE_URL`). Same id again = overwrite; clients pick up the new
sha256 on their next update check. Only the `skins` key of the live manifest is rewritten.

## GUI

- toolbar context menu → **Browse Skins...** (`SkinBrowserDialog`): list from the last manifest
  of `VersionChecker`; none fetched yet → quiet fetch (no version dialog, interval untouched)
- preview: installed bundle's own preview first, else the manifest preview (sha256-checked)
- **Apply**: download (size + sha256 check), unpack to `<config>/skins/<id>/`, apply profile +
  bundle strip. A strip from another bundle is dropped; a hand-picked one stays if the bundle has none
- **Restore Default Skin**: no profile, no strip
- installed bundles carry `.skinrelease.json` (sha256, date); each manifest fetch re-installs a
  bundle whose sha256 changed (`SkinInstaller::updateInstalled`), the active one is re-applied
- update checks only run with version checks on (Options → General)
- Skin Profiles / Toolbar Skins submenus also list bundles in `skins/<id>/`
