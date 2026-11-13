#!/usr/bin/env python3
"""
publish-skin.py — publish skin bundles (zip) to the emule-qt.org web server.

Each zip holds one *.eMuleSkin.ini (top level or in one top folder). The skin id is
that file's name without ".eMuleSkin.ini"; publishing a zip with the same id again
overwrites the skin. Files on the server:

  /pub/skins/<id>.zip                the bundle as given
  /pub/skins/<id>.preview.<ext>      preview image cut out of the bundle (optional)

and an entry in the "skins" object of the live emuleqt-version.json:

  "skins": { "<id>": { "name", "url", "sha256", "size", "date",
                       "author"?, "description"?,
                       "preview"?: { "url", "sha256", "size" } } }

"date" is the zip's modification time (UTC). The GUI lists these entries in
Browse Skins and re-installs an installed skin whose sha256 changed
(src/gui/app/SkinInstaller.cpp, docs/skins.md).

Preview: <bundle>/preview/overview.png, else the ini's [Skin] Preview= path (any
section's Preview= key as fallback), else none.

.env keys as for publish-release.sh: PUBLISH_SSH_USER/PASS/HOST, PUBLISH_REMOTE_PATH,
optional PUBLISH_LOCAL_JSON (gets a copy of the new manifest) and PUBLISH_BASE_URL
(default https://emule-qt.org/pub). Needs rsync, ssh and sshpass.

Usage:
  scripts/publish-skin.py SKIN.zip [SKIN2.zip ...] [--dry-run]
  scripts/publish-skin.py --remove ID [--remove ID2 ...]
  scripts/publish-skin.py --list
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import posixpath
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

DEFAULT_BASE_URL = "https://emule-qt.org/pub"
MANIFEST_NAME = "emuleqt-version.json"
SKINS_DIR = "skins"

# keep in sync with SkinInstaller.cpp
ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")
MAX_ZIP_BYTES = 64 * 1024 * 1024
MAX_UNPACKED_BYTES = 256 * 1024 * 1024
MAX_ENTRIES = 5000
MAX_PREVIEW_BYTES = 8 * 1024 * 1024
PREVIEW_EXTS = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp"}
INI_SUFFIX = ".emuleskin.ini"


def die(msg: str) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


# --- .env ---------------------------------------------------------------------

def load_env(root: Path) -> dict[str, str]:
    """Same rules as publish-release.sh: PUBLISH_* keys only."""
    env_file = root / ".env"
    if not env_file.is_file():
        die(f"{env_file} not found")
    out: dict[str, str] = {}
    for raw in env_file.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, val = line.split("=", 1)
        key = "".join(key.split())
        if not key.startswith("PUBLISH_") or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key):
            continue
        val = val.strip()
        if len(val) >= 2 and val[0] == val[-1] and val[0] in "\"'":
            val = val[1:-1]
        out[key] = val
    for key in ("PUBLISH_SSH_USER", "PUBLISH_SSH_PASS", "PUBLISH_SSH_HOST", "PUBLISH_REMOTE_PATH"):
        if not out.get(key):
            die(f"missing/empty in {env_file}: {key}")
    return out


# --- ini ----------------------------------------------------------------------

def decode_ini(raw: bytes) -> str:
    # as SkinEngine::decodeIni: UTF-16 with BOM, UTF-8, else ANSI
    if raw.startswith((b"\xff\xfe", b"\xfe\xff")):
        text = raw.decode("utf-16")
    elif raw.startswith(b"\xef\xbb\xbf"):
        text = raw[3:].decode("utf-8", errors="replace")
    else:
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError:
            text = raw.decode("latin-1")
    return text.replace("\r", "")


def parse_ini(text: str) -> dict[str, dict[str, str]]:
    """section -> key -> value, both lower-cased; first occurrence wins (SkinEngine::parseIni)."""
    sections: dict[str, dict[str, str]] = {}
    current: dict[str, str] | None = None
    for line in text.split("\n"):
        line = line.strip()
        if not line or line[0] in ";#":
            continue
        if line.startswith("["):
            close = line.find("]")
            name = line[1:close] if close > 0 else line[1:]
            current = sections.setdefault(name.strip().lower(), {})
            continue
        eq = line.find("=")
        if current is None or eq <= 0:
            continue
        key = line[:eq].strip().lower()
        value = line[eq + 1:].strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        if value and key not in current:
            current[key] = value
    return sections


def ini_value(sections: dict[str, dict[str, str]], key: str) -> str:
    """[Skin] key first, then the same key in any section."""
    if v := sections.get("skin", {}).get(key):
        return v
    for values in sections.values():
        if v := values.get(key):
            return v
    return ""


def split_camel(name: str) -> str:
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", name).replace("_", " ").strip()


# --- bundle -------------------------------------------------------------------

@dataclass
class Bundle:
    zip_path: Path
    skin_id: str
    name: str
    author: str
    description: str
    sha256: str
    size: int
    date: str
    preview_name: str = ""          # member name inside the zip
    preview_bytes: bytes = field(default=b"", repr=False)

    @property
    def preview_ext(self) -> str:
        ext = posixpath.splitext(self.preview_name)[1].lower()
        return ".jpg" if ext == ".jpeg" else ext


def safe_member(name: str) -> bool:
    if name.startswith(("/", "\\")) or re.match(r"^[A-Za-z]:", name):
        return False
    return ".." not in re.split(r"[\\/]", name)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def inspect_bundle(zip_path: Path, id_override: str, name_override: str) -> Bundle:
    if not zip_path.is_file():
        die(f"{zip_path} not found")
    size = zip_path.stat().st_size
    if size == 0 or size > MAX_ZIP_BYTES:
        die(f"{zip_path.name}: size {size} outside 1..{MAX_ZIP_BYTES} bytes")
    try:
        zf = zipfile.ZipFile(zip_path)
    except zipfile.BadZipFile as e:
        die(f"{zip_path.name}: not a zip ({e})")
    with zf:
        infos = zf.infolist()
        if len(infos) > MAX_ENTRIES:
            die(f"{zip_path.name}: {len(infos)} entries, max {MAX_ENTRIES}")
        if sum(i.file_size for i in infos) > MAX_UNPACKED_BYTES:
            die(f"{zip_path.name}: unpacks to more than {MAX_UNPACKED_BYTES} bytes")
        if any(i.flag_bits & 0x1 for i in infos):
            die(f"{zip_path.name}: encrypted entries")
        bad = [i.filename for i in infos if not safe_member(i.filename)]
        if bad:
            die(f"{zip_path.name}: unsafe member names: {bad[:5]}")
        names = {i.filename.replace("\\", "/"): i for i in infos}

        # one ini, at the top or in one top folder (the GUI installs the same way)
        inis = [n for n in names if n.lower().endswith(INI_SUFFIX) and n.count("/") <= 1
                and not n.endswith("/")]
        if len(inis) != 1:
            die(f"{zip_path.name}: need exactly one *.eMuleSkin.ini at the top or in one "
                f"top folder, found {inis or 'none'}")
        ini = inis[0]
        root = posixpath.dirname(ini)
        tops = {n.split("/", 1)[0] for n in names if not n.startswith("__MACOSX/")}
        if root and tops != {root}:
            die(f"{zip_path.name}: the ini is in {root}/ but the zip has other top entries: "
                f"{sorted(tops - {root})[:5]}")

        base = posixpath.basename(ini)
        skin_id = id_override or base[: -len(INI_SUFFIX)]
        if not ID_RE.fullmatch(skin_id):
            die(f"{zip_path.name}: skin id '{skin_id}' must match {ID_RE.pattern} (--id to override)")

        sections = parse_ini(decode_ini(zf.read(names[ini])))
        name = name_override or ini_value(sections, "name") or split_camel(skin_id)

        def member(rel: str) -> str:
            rel = rel.replace("\\", "/")
            if posixpath.isabs(rel) or re.match(r"^[A-Za-z]:", rel) or not safe_member(rel):
                return ""
            full = posixpath.normpath(posixpath.join(root, rel)) if root else posixpath.normpath(rel)
            if full in names:
                return full
            lower = {n.lower(): n for n in names}   # Windows-made skins are case-blind
            return lower.get(full.lower(), "")

        preview = member("preview/overview.png")
        if not preview and (spec := ini_value(sections, "preview")):
            preview = member(spec)
            if not preview:
                print(f"warning: {zip_path.name}: Preview={spec} not in the bundle", file=sys.stderr)
        preview_bytes = b""
        if preview:
            info = names[preview]
            ext = posixpath.splitext(preview)[1].lower()
            if ext not in PREVIEW_EXTS:
                print(f"warning: {zip_path.name}: preview {preview} is no image, skipped", file=sys.stderr)
                preview = ""
            elif info.file_size > MAX_PREVIEW_BYTES:
                print(f"warning: {zip_path.name}: preview {preview} over {MAX_PREVIEW_BYTES} bytes, skipped",
                      file=sys.stderr)
                preview = ""
            else:
                preview_bytes = zf.read(info)

    mtime = dt.datetime.fromtimestamp(zip_path.stat().st_mtime, dt.timezone.utc)
    return Bundle(zip_path=zip_path, skin_id=skin_id, name=name,
                  author=ini_value(sections, "author"),
                  description=ini_value(sections, "description"),
                  sha256=sha256_file(zip_path), size=size,
                  date=mtime.strftime("%Y-%m-%dT%H:%M:%SZ"),
                  preview_name=preview, preview_bytes=preview_bytes)


def url_file_name(url: str) -> str:
    """File name of a published URL, without the ?v= cache key."""
    return posixpath.basename(urllib.parse.urlsplit(url).path) if url else ""


def manifest_entry(b: Bundle, base_url: str) -> dict:
    entry: dict = {
        "name": b.name,
        # ?v= = content hash: the CDN caches by URL, an overwrite under the same URL
        # would serve the old zip against the new sha256 for hours
        "url": f"{base_url}/{SKINS_DIR}/{b.skin_id}.zip?v={b.sha256[:16]}",
        "sha256": b.sha256,
        "size": b.size,
        "date": b.date,
    }
    if b.author:
        entry["author"] = b.author
    if b.description:
        entry["description"] = b.description
    if b.preview_bytes:
        preview_sha = hashlib.sha256(b.preview_bytes).hexdigest()
        entry["preview"] = {
            "url": f"{base_url}/{SKINS_DIR}/{b.skin_id}.preview{b.preview_ext}?v={preview_sha[:16]}",
            "sha256": preview_sha,
            "size": len(b.preview_bytes),
        }
    return entry


# --- server -------------------------------------------------------------------

class Server:
    def __init__(self, env: dict[str, str]):
        self.env = env
        self.remote_path = env["PUBLISH_REMOTE_PATH"].rstrip("/")
        self.target = f'{env["PUBLISH_SSH_USER"]}@{env["PUBLISH_SSH_HOST"]}'
        self.ssh = ["sshpass", "-e", "ssh", "-o", "StrictHostKeyChecking=accept-new"]
        self.proc_env = {**os.environ, "SSHPASS": env["PUBLISH_SSH_PASS"]}

    def run(self, args: list[str], what: str) -> None:
        if subprocess.run(args, env=self.proc_env).returncode != 0:
            die(f"{what} failed")

    def remote_shell(self, command: str, what: str) -> None:
        self.run(self.ssh + [self.target, command], what)

    def rsync(self, src: str, dst: str, what: str) -> None:
        self.run(["rsync", "-az", "-e", " ".join(self.ssh), src, dst], what)

    def remote(self, rel: str) -> str:
        return f"{self.target}:{self.remote_path}/{rel}"

    def pull_manifest(self, dest: Path) -> dict:
        self.rsync(self.remote(MANIFEST_NAME), str(dest), "downloading the live manifest")
        try:
            data = json.loads(dest.read_text(encoding="utf-8"))
        except json.JSONDecodeError as e:
            die(f"live manifest is no JSON: {e}")
        if not isinstance(data, dict) or not data.get("latest"):
            die("live manifest has no 'latest' -- refusing to rewrite it")
        return data

    def push_manifest(self, manifest: dict, work: Path) -> None:
        path = work / "manifest.new.json"
        path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        if local := self.env.get("PUBLISH_LOCAL_JSON"):
            shutil.copyfile(path, local)
        self.rsync(str(path), self.remote(MANIFEST_NAME), "manifest upload")


def check_served(url: str, sha256: str) -> None:
    """The file is served as uploaded: clients check size and SHA-256."""
    sep = "&" if "?" in url else "?"
    req = urllib.request.Request(f"{url}{sep}t={int(time.time())}", headers={"User-Agent": "publish-skin"})
    h = hashlib.sha256()
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            for chunk in iter(lambda: resp.read(1 << 20), b""):
                h.update(chunk)
    except OSError as e:
        die(f"{url} not reachable after upload: {e}")
    if h.hexdigest() != sha256:
        die(f"{url} served differs from the upload")


# --- main ---------------------------------------------------------------------

def main() -> None:
    ap = argparse.ArgumentParser(description="Publish eMule Qt skin bundles to emule-qt.org.")
    ap.add_argument("zips", nargs="*", type=Path, help="skin zip(s)")
    ap.add_argument("--id", default="", help="skin id (default: the ini name); one zip only")
    ap.add_argument("--name", default="", help="display name (default: [Skin] Name= or the id)")
    ap.add_argument("--remove", action="append", default=[], metavar="ID", help="unpublish a skin")
    ap.add_argument("--list", action="store_true", help="print the published skins")
    ap.add_argument("--dry-run", action="store_true", help="check the zips, print the entries, upload nothing")
    args = ap.parse_args()

    if not args.zips and not args.remove and not args.list:
        ap.error("give a skin zip, --remove ID or --list")
    if (args.id or args.name) and len(args.zips) != 1:
        ap.error("--id/--name need exactly one zip")
    for rid in args.remove:
        if not ID_RE.fullmatch(rid):
            die(f"bad skin id '{rid}'")

    bundles = [inspect_bundle(z, args.id, args.name) for z in args.zips]
    ids = [b.skin_id for b in bundles]
    if len(set(ids)) != len(ids):
        die(f"two zips share a skin id: {ids}")

    root = Path(subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True,
                               text=True).stdout.strip() or Path(__file__).resolve().parent.parent)
    if args.dry_run:
        base_url = DEFAULT_BASE_URL
        for b in bundles:
            print(json.dumps({b.skin_id: manifest_entry(b, base_url)}, indent=2))
            print(f"  preview: {b.preview_name or 'none'}")
        return

    env = load_env(root)
    base_url = env.get("PUBLISH_BASE_URL", DEFAULT_BASE_URL).rstrip("/")
    for tool in ("rsync", "ssh", "sshpass"):
        if not shutil.which(tool):
            die(f"{tool} not installed" + (" (brew install hudochenkov/sshpass/sshpass)" if tool == "sshpass" else ""))
    server = Server(env)

    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        manifest = server.pull_manifest(work / "manifest.json")
        skins = manifest.get("skins")
        if not isinstance(skins, dict):
            skins = {}

        if args.list:
            for sid, e in sorted(skins.items()):
                print(f"{sid}: {e.get('name', '')}  {e.get('date', '')}  {e.get('size', 0)} bytes")
            if not bundles and not args.remove:
                return

        if bundles:
            server.remote_shell(f"mkdir -p '{server.remote_path}/{SKINS_DIR}'", "creating the skins dir")
        for b in bundles:
            entry = manifest_entry(b, base_url)
            old = skins.get(b.skin_id, {})
            if old.get("sha256") == b.sha256:
                print(f"{b.skin_id}: same zip already published, re-uploading anyway")
            staged = work / f"{b.skin_id}.zip"
            shutil.copyfile(b.zip_path, staged)
            print(f"Uploading {b.skin_id}.zip ({b.size} bytes, sha256 {b.sha256}) ...")
            server.rsync(str(staged), server.remote(f"{SKINS_DIR}/{b.skin_id}.zip"), "zip upload")
            check_served(entry["url"], b.sha256)
            if prev := entry.get("preview"):
                staged_preview = work / url_file_name(prev["url"])
                staged_preview.write_bytes(b.preview_bytes)
                server.rsync(str(staged_preview), server.remote(f"{SKINS_DIR}/{staged_preview.name}"),
                             "preview upload")
                check_served(prev["url"], prev["sha256"])
            # a preview with another extension, or none any more: drop the old file
            old_preview = url_file_name((old.get("preview") or {}).get("url", ""))
            if old_preview and old_preview != url_file_name((entry.get("preview") or {}).get("url", "")):
                name = old_preview
                if re.fullmatch(r"[A-Za-z0-9._-]+", name):
                    server.remote_shell(f"rm -f '{server.remote_path}/{SKINS_DIR}/{name}'", "old preview removal")
            skins[b.skin_id] = entry

        for rid in args.remove:
            if rid not in skins:
                print(f"{rid}: not in the manifest, removing its files anyway")
            server.remote_shell(f"rm -f '{server.remote_path}/{SKINS_DIR}/{rid}.zip' "
                                f"'{server.remote_path}/{SKINS_DIR}/{rid}.preview.'*",
                                f"removing {rid}")
            skins.pop(rid, None)

        if bundles or args.remove:
            manifest["skins"] = dict(sorted(skins.items()))
            print("Publishing the manifest ...")
            server.push_manifest(manifest, work)
            for b in bundles:
                print(f"Published skin {b.skin_id} -> {base_url}/{SKINS_DIR}/{b.skin_id}.zip")
            for rid in args.remove:
                print(f"Removed skin {rid}")


if __name__ == "__main__":
    main()
