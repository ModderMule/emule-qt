#!/usr/bin/env python3
"""
Translate missing strings in Qt .ts translation files.

Usage:
    scripts/translate_missing.py show              # list untranslated strings
    scripts/translate_missing.py export            # export to lang/missing.json
    scripts/translate_missing.py apply             # apply from lang/missing.json
    scripts/translate_missing.py apply FILE.json   # apply from custom JSON file

Workflow after running lupdate:
    1. scripts/translate_missing.py export          # creates lang/missing.json
    2. Fill in translations in lang/missing.json    # manually or via LLM
    3. scripts/translate_missing.py apply           # writes into .ts files
    4. scripts/localize.sh compile                  # generate .qm binaries
"""

import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

LANG_DIR = Path(__file__).resolve().parent.parent / "lang"

LANGUAGES = ["de_DE", "es_ES", "fr_FR", "it_IT", "ja_JP", "ko_KR", "pt_BR", "zh_CN"]

DEFAULT_EXPORT = LANG_DIR / "missing.json"


def ts_path(lang: str) -> Path:
    return LANG_DIR / f"emuleqt_{lang}.ts"


def get_untranslated(lang: str) -> list[tuple[str, str]]:
    """Return list of (context_name, source_text) for unfinished translations."""
    path = ts_path(lang)
    if not path.exists():
        return []
    tree = ET.parse(path)
    results = []
    for ctx in tree.getroot().findall("context"):
        ctx_name = ctx.findtext("name", "")
        for msg in ctx.findall("message"):
            trans = msg.find("translation")
            if trans is not None and trans.get("type") == "unfinished":
                src = msg.findtext("source", "")
                if src:
                    results.append((ctx_name, src))
    return results


def cmd_show():
    """Print all untranslated strings grouped by language."""
    any_missing = False
    for lang in LANGUAGES:
        missing = get_untranslated(lang)
        if not missing:
            continue
        any_missing = True
        print(f"\n  {lang} ({len(missing)} missing):")
        for ctx, src in missing:
            display = src.replace("\n", "\\n")
            print(f"    [{ctx}] {display}")
    if not any_missing:
        print("  All languages are fully translated.")


def cmd_export():
    """Export untranslated strings to a JSON template for filling in translations.

    Format:
    {
        "source_text": {
            "_context": "ClassName",
            "de_DE": "",
            "es_ES": "",
            ...
        }
    }
    """
    # Collect unique source strings and their contexts across all languages
    sources: dict[str, str] = {}  # source -> context
    per_lang_missing: dict[str, set[str]] = {}

    for lang in LANGUAGES:
        missing = get_untranslated(lang)
        per_lang_missing[lang] = set()
        for ctx, src in missing:
            sources[src] = ctx
            per_lang_missing[lang].add(src)

    if not sources:
        print("  Nothing to export — all languages are fully translated.")
        return

    # Build export structure
    export: dict[str, dict[str, str]] = {}
    for src, ctx in sources.items():
        entry: dict[str, str] = {"_context": ctx}
        for lang in LANGUAGES:
            if src in per_lang_missing[lang]:
                entry[lang] = ""
        export[src] = entry

    DEFAULT_EXPORT.write_text(json.dumps(export, indent=2, ensure_ascii=False) + "\n",
                              encoding="utf-8")
    print(f"  Exported {len(export)} strings to {DEFAULT_EXPORT.relative_to(LANG_DIR.parent)}")
    print(f"  Fill in the empty values, then run: scripts/translate_missing.py apply")


def cmd_apply(json_path: Path = DEFAULT_EXPORT):
    """Read translations from JSON and write them into .ts files."""
    if not json_path.exists():
        print(f"  Error: {json_path} not found. Run 'export' first.", file=sys.stderr)
        sys.exit(1)

    data: dict[str, dict[str, str]] = json.loads(json_path.read_text(encoding="utf-8"))

    # Build lookup: source_text -> {lang: translation}
    translations: dict[str, dict[str, str]] = {}
    for src, entry in data.items():
        for lang in LANGUAGES:
            val = entry.get(lang, "")
            if val:
                translations.setdefault(src, {})[lang] = val

    if not translations:
        print("  No translations found in JSON (all values empty).")
        return

    for lang in LANGUAGES:
        path = ts_path(lang)
        if not path.exists():
            continue

        tree = ET.parse(path)
        root = tree.getroot()
        count = 0

        for ctx in root.findall("context"):
            for msg in ctx.findall("message"):
                trans = msg.find("translation")
                if trans is None or trans.get("type") != "unfinished":
                    continue
                src = msg.findtext("source", "")
                if src in translations and lang in translations[src]:
                    trans.text = translations[src][lang]
                    del trans.attrib["type"]
                    count += 1

        if count > 0:
            tree.write(str(path), encoding="utf-8", xml_declaration=True)
            _fix_ts_header(path)

        print(f"  {lang}: {count} strings applied")

    print(f"\n  Run 'scripts/localize.sh compile' to generate .qm files.")


def _fix_ts_header(path: Path):
    """Ensure Qt-compatible XML header with DOCTYPE."""
    content = path.read_text(encoding="utf-8")
    content = content.replace(
        "<?xml version='1.0' encoding='utf-8'?>",
        '<?xml version="1.0" encoding="utf-8"?>',
    )
    if "<!DOCTYPE TS>" not in content:
        content = content.replace(
            '<?xml version="1.0" encoding="utf-8"?>',
            '<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>',
        )
    path.write_text(content, encoding="utf-8")


def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        sys.exit(0)

    cmd = sys.argv[1]
    if cmd == "show":
        cmd_show()
    elif cmd == "export":
        cmd_export()
    elif cmd == "apply":
        json_path = Path(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_EXPORT
        cmd_apply(json_path)
    else:
        print(f"  Unknown command: {cmd}", file=sys.stderr)
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()