#!/usr/bin/env python3
"""Regenerate the source lists in src/usenet/emuleusenet.vcxproj(.filters).

The CMake build globs; MSBuild does not. Keeping the two in step by hand is the
step that gets skipped, and the failure is a link error on Windows only. A
header with Q_OBJECT must land in <QtMoc>, not <ClInclude>, or moc never runs
for it and the vtable goes missing.
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
USENET = ROOT / "src" / "usenet"
PROJ = USENET / "emuleusenet.vcxproj"
FILTERS = USENET / "emuleusenet.vcxproj.filters"


def win(path: pathlib.Path) -> str:
    return str(path.relative_to(USENET)).replace("/", "\\")


def collect():
    sources, plain_headers, moc_headers = [], [], []
    for p in sorted(USENET.rglob("*")):
        if p.suffix == ".cpp":
            sources.append(win(p))
        elif p.suffix == ".h":
            text = p.read_text(encoding="utf-8")
            (moc_headers if "Q_OBJECT" in text else plain_headers).append(win(p))
    return sources, plain_headers, moc_headers


def replace_block(text: str, new_block: str, marker: str) -> str:
    """Swap the ItemGroup that contains `marker` for new_block."""
    pattern = re.compile(r"  <ItemGroup>\r?\n(?:(?!</ItemGroup>).)*?"
                         + re.escape(marker)
                         + r"(?:(?!</ItemGroup>).)*?</ItemGroup>\r?\n",
                         re.S)
    match = pattern.search(text)
    if not match:
        raise SystemExit(f"could not locate the ItemGroup containing {marker}")
    return text[:match.start()] + new_block + text[match.end():]


def main():
    sources, plain_headers, moc_headers = collect()

    proj = PROJ.read_text(encoding="utf-8-sig")
    cl = "  <ItemGroup>\n"
    for s in sources:
        cl += f'    <ClCompile Include="{s}" />\n'
    cl += "  </ItemGroup>\n"
    proj = replace_block(proj, cl, "<ClCompile Include=")

    inc = "  <ItemGroup>\n"
    for h in plain_headers:
        inc += f'    <ClInclude Include="{h}" />\n'
    for h in moc_headers:
        inc += f'    <QtMoc Include="{h}" />\n'
    inc += "  </ItemGroup>\n"
    proj = replace_block(proj, inc, "Include=\"UsenetSession.h\"")
    PROJ.write_bytes("﻿".encode("utf-8") + proj.encode("utf-8"))

    filters = FILTERS.read_text(encoding="utf-8-sig")
    fcl = "  <ItemGroup>\r\n"
    for s in sources:
        fcl += f'    <ClCompile Include="{s}">\r\n      <Filter>Source Files</Filter>\r\n    </ClCompile>\r\n'
    fcl += "  </ItemGroup>\r\n"
    filters = replace_block(filters, fcl, "<ClCompile Include=")

    finc = "  <ItemGroup>\r\n"
    for h in plain_headers:
        finc += f'    <ClInclude Include="{h}">\r\n      <Filter>Header Files</Filter>\r\n    </ClInclude>\r\n'
    for h in moc_headers:
        finc += f'    <QtMoc Include="{h}">\r\n      <Filter>Header Files</Filter>\r\n    </QtMoc>\r\n'
    finc += "  </ItemGroup>\r\n"
    filters = replace_block(filters, finc, "Include=\"UsenetSession.h\"")
    FILTERS.write_bytes("﻿".encode("utf-8") + filters.encode("utf-8"))

    print(f"emuleusenet: {len(sources)} sources, "
          f"{len(plain_headers)} headers, {len(moc_headers)} moc headers")


if __name__ == "__main__":
    main()
