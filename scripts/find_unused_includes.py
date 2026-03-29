#!/usr/bin/env python3
"""Find (and optionally remove) unused #include directives in C++ source files.

Uses compile_commands.json and clang's -fsyntax-only to test whether each
#include can be removed without breaking compilation.

Usage:
    python3 scripts/find_unused_includes.py                    # dry-run, all src/ files
    python3 scripts/find_unused_includes.py --file src/gui/panels/KadPanel.cpp
    python3 scripts/find_unused_includes.py --remove           # actually remove unused includes
    python3 scripts/find_unused_includes.py --jobs 8           # parallel workers
"""

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = PROJECT_ROOT / "build"
COMPILE_DB = BUILD_DIR / "compile_commands.json"

# Never remove these includes
SKIP_INCLUDES = {"pch.h", "stdafx.h"}

# Headers where -fsyntax-only succeeds with forward declarations but actual
# compilation needs full definitions.  These are never flagged as unused.
NEVER_REMOVE_PATTERNS = {
    "openssl/",    # OpenSSL headers — forward decls hide actual need
    "zlib.h",      # zlib — same issue with struct pointers
}

# Mapping from header basenames to the primary symbol(s) they provide.
# For Qt headers, the header name IS the class name.
# For project headers, we extract the class/type from the header file itself.
# This is used to avoid removing includes whose symbols are actually used
# but happen to be transitively provided by another header.


def extract_primary_symbol(inc_name: str) -> str | None:
    """Extract the primary symbol name from an include path.

    For Qt headers like <QVBoxLayout>, returns 'QVBoxLayout'.
    For project headers like "foo/Bar.h", returns 'Bar'.
    """
    basename = os.path.basename(inc_name)
    name, ext = os.path.splitext(basename)

    # Qt-style headers (no extension, starts with Q)
    if not ext and name.startswith("Q"):
        return name

    # Standard C++ headers — no obvious symbol
    if not ext:
        return None

    # Project headers: return name without extension
    if ext in (".h", ".hpp", ".hxx"):
        return name

    return None


def symbol_used_in_file(filepath: str, symbol: str, include_line_idx: int) -> bool:
    """Check if a symbol appears in the file outside of #include lines."""
    with open(filepath) as f:
        for i, line in enumerate(f):
            if i == include_line_idx:
                continue
            stripped = line.strip()
            if stripped.startswith("#include"):
                continue
            if symbol in line:
                return True
    return False


def load_compile_commands() -> dict[str, dict]:
    """Load compile_commands.json and index by source file path."""
    if not COMPILE_DB.exists():
        print(f"Error: {COMPILE_DB} not found. Run cmake with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON first.", file=sys.stderr)
        sys.exit(1)

    with open(COMPILE_DB) as f:
        entries = json.load(f)

    db: dict[str, dict] = {}
    for entry in entries:
        filepath = entry["file"]
        if filepath.endswith(".cpp") and "/src/" in filepath and "autogen" not in filepath and "moc_" not in filepath and "pch" not in filepath:
            db[filepath] = entry
    return db


def make_syntax_check_cmd(entry: dict) -> list[str]:
    """Convert a compile command to a syntax-only check."""
    cmd = entry["command"]
    # Parse into tokens
    tokens = shlex.split(cmd)

    result = []
    skip_next = False
    replaced_c = False
    for i, tok in enumerate(tokens):
        if skip_next:
            skip_next = False
            continue
        # Remove output file
        if tok == "-o":
            skip_next = True
            continue
        # Replace -c with -fsyntax-only
        if tok == "-c" and not replaced_c:
            result.append("-fsyntax-only")
            replaced_c = True
            continue
        # Remove PCH usage flags (we'll include pch via the source's own #include)
        if tok in ("-Xclang",) and i + 1 < len(tokens):
            next_tok = tokens[i + 1]
            if next_tok in ("-emit-pch", "-include-pch", "-include") or next_tok.endswith(".pch") or next_tok.endswith(".hxx"):
                skip_next = True
                continue
        if tok.endswith(".pch") or tok.endswith(".hxx") or tok.endswith(".hxx.cxx"):
            continue
        if tok == "-emit-pch" or tok == "-include-pch":
            continue
        # Remove -x c++-header (we're compiling source, not header)
        if tok == "-x" and i + 1 < len(tokens) and "header" in tokens[i + 1]:
            skip_next = True
            continue
        result.append(tok)

    if not replaced_c:
        # No -c found, just add -fsyntax-only
        result.append("-fsyntax-only")

    # Add -w to suppress all warnings (we only care about errors)
    result.append("-w")

    return result


def parse_includes(filepath: str) -> list[tuple[int, str, str]]:
    """Parse #include directives from a file.

    Returns list of (line_number, full_line, include_name).
    Skips includes inside preprocessor conditional blocks and protected includes.
    """
    includes = []
    conditional_depth = 0

    with open(filepath) as f:
        lines = f.readlines()

    for i, line in enumerate(lines):
        stripped = line.strip()

        # Track preprocessor conditionals
        if re.match(r"^\s*#\s*(if|ifdef|ifndef)\b", stripped):
            conditional_depth += 1
            continue
        if re.match(r"^\s*#\s*(endif)\b", stripped):
            conditional_depth = max(0, conditional_depth - 1)
            continue
        if re.match(r"^\s*#\s*(else|elif)\b", stripped):
            continue

        # Only consider top-level includes (not inside #ifdef blocks)
        if conditional_depth > 0:
            continue

        m = re.match(r'^\s*#\s*include\s+[<"]([^>"]+)[>"]', stripped)
        if m:
            inc_name = m.group(1)
            basename = os.path.basename(inc_name)
            if basename in SKIP_INCLUDES:
                continue
            if any(pat in inc_name for pat in NEVER_REMOVE_PATTERNS):
                continue
            includes.append((i, line, inc_name))

    return includes


def test_without_include(filepath: str, line_idx: int, pch_line_idx: int | None,
                         cmd: list[str], directory: str) -> bool:
    """Test if file compiles without the include at line_idx.

    Also comments out the PCH include so that transitive PCH-provided headers
    don't mask truly needed includes.

    Returns True if the include is UNUSED (compilation succeeds without it).
    """
    with open(filepath) as f:
        lines = f.readlines()

    lines[line_idx] = f"// REMOVED: {lines[line_idx]}"
    if pch_line_idx is not None:
        lines[pch_line_idx] = f"// REMOVED: {lines[pch_line_idx]}"

    # Write to temp file in same directory to preserve relative includes
    dirpath = os.path.dirname(filepath)
    fd, tmp_path = tempfile.mkstemp(suffix=".cpp", dir=dirpath)
    try:
        with os.fdopen(fd, "w") as f:
            f.writelines(lines)

        # Replace the source file path in the command with the temp file
        test_cmd = [tmp_path if tok == filepath else tok for tok in cmd]

        result = subprocess.run(
            test_cmd,
            cwd=directory,
            capture_output=True,
            timeout=30,
        )
        return result.returncode == 0
    except subprocess.TimeoutExpired:
        return False
    finally:
        os.unlink(tmp_path)


def find_pch_line(filepath: str) -> int | None:
    """Find the line index of the PCH #include (pch.h / stdafx.h)."""
    with open(filepath) as f:
        for i, line in enumerate(f):
            m = re.match(r'^\s*#\s*include\s+[<"]([^>"]+)[>"]', line.strip())
            if m and os.path.basename(m.group(1)) in SKIP_INCLUDES:
                return i
    return None


def test_baseline_without_pch(filepath: str, pch_line_idx: int | None,
                               cmd: list[str], directory: str) -> bool:
    """Verify the file compiles when PCH include is commented out.

    If the file doesn't compile without its PCH, we fall back to testing
    with PCH (which may produce false positives for PCH-covered headers).
    """
    if pch_line_idx is None:
        return True  # No PCH to remove

    with open(filepath) as f:
        lines = f.readlines()

    lines[pch_line_idx] = f"// REMOVED: {lines[pch_line_idx]}"

    dirpath = os.path.dirname(filepath)
    fd, tmp_path = tempfile.mkstemp(suffix=".cpp", dir=dirpath)
    try:
        with os.fdopen(fd, "w") as f:
            f.writelines(lines)

        test_cmd = [tmp_path if tok == filepath else tok for tok in cmd]
        result = subprocess.run(test_cmd, cwd=directory, capture_output=True, timeout=30)
        return result.returncode == 0
    except subprocess.TimeoutExpired:
        return False
    finally:
        os.unlink(tmp_path)


def check_file(filepath: str, entry: dict) -> list[tuple[int, str]]:
    """Check a single file for unused includes.

    Returns list of (line_number, include_name) for unused includes.
    """
    cmd = make_syntax_check_cmd(entry)
    directory = entry["directory"]

    # First verify the file compiles as-is
    result = subprocess.run(cmd, cwd=directory, capture_output=True, timeout=30)
    if result.returncode != 0:
        rel = os.path.relpath(filepath, PROJECT_ROOT)
        print(f"  SKIP {rel} (does not compile as-is)", file=sys.stderr)
        return []

    pch_line_idx = find_pch_line(filepath)

    # Check if file compiles without PCH — if so, we can detect PCH-masked includes
    pch_removable = test_baseline_without_pch(filepath, pch_line_idx, cmd, directory)
    effective_pch_line = pch_line_idx if pch_removable else None

    includes = parse_includes(filepath)
    unused = []

    for line_idx, full_line, inc_name in includes:
        if test_without_include(filepath, line_idx, effective_pch_line, cmd, directory):
            # Double-check: if the header's primary symbol is used in the file,
            # keep the include — it's only removable due to transitive includes
            symbol = extract_primary_symbol(inc_name)
            if symbol and symbol_used_in_file(filepath, symbol, line_idx):
                continue
            unused.append((line_idx + 1, inc_name))

    return unused


def remove_includes(filepath: str, unused_lines: list[int]) -> None:
    """Remove the specified lines from a file."""
    line_set = set(unused_lines)
    with open(filepath) as f:
        lines = f.readlines()

    with open(filepath, "w") as f:
        for i, line in enumerate(lines):
            if (i + 1) not in line_set:
                f.write(line)


def main():
    parser = argparse.ArgumentParser(description="Find unused #include directives")
    parser.add_argument("--file", help="Check a single file (path relative to project root)")
    parser.add_argument("--remove", action="store_true", help="Actually remove unused includes")
    parser.add_argument("--jobs", "-j", type=int, default=4, help="Parallel workers (default: 4)")
    parser.add_argument("--build-dir", help="Build directory (default: build/)")
    args = parser.parse_args()

    global BUILD_DIR, COMPILE_DB
    if args.build_dir:
        BUILD_DIR = Path(args.build_dir).resolve()
        COMPILE_DB = BUILD_DIR / "compile_commands.json"

    db = load_compile_commands()
    print(f"Loaded {len(db)} source files from compile_commands.json")

    # Filter to requested file(s)
    if args.file:
        target = (PROJECT_ROOT / args.file).resolve()
        target_str = str(target)
        if target_str not in db:
            print(f"Error: {args.file} not found in compile_commands.json", file=sys.stderr)
            print("Available files matching pattern:", file=sys.stderr)
            for k in sorted(db.keys()):
                if args.file.split("/")[-1] in k:
                    print(f"  {os.path.relpath(k, PROJECT_ROOT)}", file=sys.stderr)
            sys.exit(1)
        files_to_check = {target_str: db[target_str]}
    else:
        files_to_check = db

    total_unused = 0
    all_results: dict[str, list[tuple[int, str]]] = {}

    print(f"Checking {len(files_to_check)} files with {args.jobs} workers...\n")

    with ProcessPoolExecutor(max_workers=args.jobs) as executor:
        futures = {}
        for filepath, entry in sorted(files_to_check.items()):
            future = executor.submit(check_file, filepath, entry)
            futures[future] = filepath

        for future in as_completed(futures):
            filepath = futures[future]
            rel = os.path.relpath(filepath, PROJECT_ROOT)
            try:
                unused = future.result()
            except Exception as e:
                print(f"  ERROR {rel}: {e}", file=sys.stderr)
                continue

            if unused:
                all_results[filepath] = unused
                total_unused += len(unused)
                for line_num, inc_name in unused:
                    print(f"  {rel}:{line_num}: #include \"{inc_name}\"")

    print(f"\nFound {total_unused} unused includes in {len(all_results)} files")

    if args.remove and all_results:
        print("\nRemoving unused includes...")
        for filepath, unused in sorted(all_results.items()):
            rel = os.path.relpath(filepath, PROJECT_ROOT)
            lines_to_remove = [line_num for line_num, _ in unused]
            remove_includes(filepath, lines_to_remove)
            names = ", ".join(inc for _, inc in unused)
            print(f"  {rel}: removed {len(unused)} includes ({names})")
        print(f"\nDone. Removed {total_unused} includes. Run build to verify.")
    elif all_results and not args.remove:
        print("\nDry run — use --remove to actually delete them.")


if __name__ == "__main__":
    main()