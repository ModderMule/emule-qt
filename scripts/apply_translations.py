#!/usr/bin/env python3
"""Apply translations from a JSON file to a Qt .ts file.

Usage: python3 apply_translations.py <ts_file> <json_file>

The JSON file should be a dict mapping English source strings to translated strings.
"""
import json
import sys
import xml.etree.ElementTree as ET


def apply_translations(ts_path: str, json_path: str) -> None:
    with open(json_path, "r", encoding="utf-8") as f:
        translations: dict[str, str] = json.load(f)

    tree = ET.parse(ts_path)
    root = tree.getroot()

    applied = 0
    skipped = 0

    for msg in root.iter("message"):
        src_el = msg.find("source")
        tr_el = msg.find("translation")
        if src_el is None or tr_el is None or src_el.text is None:
            continue

        src_text = src_el.text
        if src_text in translations:
            tr_el.text = translations[src_text]
            # Remove "unfinished" type attribute
            if "type" in tr_el.attrib:
                del tr_el.attrib["type"]
            applied += 1
        else:
            skipped += 1

    tree.write(ts_path, encoding="utf-8", xml_declaration=True)
    print(f"Applied {applied} translations, {skipped} untranslated in {ts_path}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <ts_file> <json_file>")
        sys.exit(1)
    apply_translations(sys.argv[1], sys.argv[2])