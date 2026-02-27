#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# localize.sh — Extract and compile Qt translation files for eMule Qt
#
# Usage:
#   ./scripts/localize.sh [options] [lang ...]
#
# Commands:
#   extract         Run lupdate to extract translatable strings into .ts files
#   compile         Run lrelease to compile .ts files into .qm binaries
#   add <lang>      Create a new .ts file for a language (e.g. "de", "fr", "zh_CN")
#   status          Show translation statistics for all .ts files
#
# Options:
#   --no-obsolete   Drop obsolete/vanished strings during extract
#   --qt-dir PATH   Override Qt installation directory
#
# If no command is given, runs both extract and compile.
#
# Examples:
#   ./scripts/localize.sh                         # extract + compile all
#   ./scripts/localize.sh extract                 # extract only
#   ./scripts/localize.sh compile                 # compile only
#   ./scripts/localize.sh add de fr es            # add German, French, Spanish
#   ./scripts/localize.sh extract --no-obsolete   # extract, drop old strings
#   ./scripts/localize.sh status                  # show stats
# ---------------------------------------------------------------------------

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TRANSLATIONS_DIR="$PROJECT_DIR/lang"

# Source directories to scan for tr() calls
SOURCE_DIRS=(
    "$PROJECT_DIR/src/gui"
)

# Base name for .ts/.qm files
TS_BASENAME="emuleqt"

# ---------------------------------------------------------------------------
# Find Qt tools
# ---------------------------------------------------------------------------

QT_DIR="${EMULE_QT_DIR:-}"

find_qt_tool() {
    local tool="$1"

    # 1) Explicit --qt-dir or EMULE_QT_DIR
    if [ -n "$QT_DIR" ] && [ -x "$QT_DIR/bin/$tool" ]; then
        echo "$QT_DIR/bin/$tool"
        return
    fi

    # 2) Known Qt installation path
    local known="/Users/daniel/Qt/6.10.2/macos/bin/$tool"
    if [ -x "$known" ]; then
        echo "$known"
        return
    fi

    # 3) Search common Qt installation patterns
    for candidate in /Users/*/Qt/*/macos/bin/$tool /opt/Qt/*/gcc_64/bin/$tool; do
        if [ -x "$candidate" ]; then
            echo "$candidate"
            return
        fi
    done

    # 4) Fall back to PATH
    if command -v "$tool" &>/dev/null; then
        command -v "$tool"
        return
    fi

    echo ""
}

LUPDATE="$(find_qt_tool lupdate)"
LRELEASE="$(find_qt_tool lrelease)"

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------

COMMAND=""
LANGUAGES=()
NO_OBSOLETE=false

while [ $# -gt 0 ]; do
    case "$1" in
        extract|compile|add|status)
            COMMAND="$1"
            shift
            ;;
        --no-obsolete)
            NO_OBSOLETE=true
            shift
            ;;
        --qt-dir)
            QT_DIR="$2"
            LUPDATE="$(find_qt_tool lupdate)"
            LRELEASE="$(find_qt_tool lrelease)"
            shift 2
            ;;
        -h|--help)
            head -28 "$0" | tail -26
            exit 0
            ;;
        *)
            LANGUAGES+=("$1")
            shift
            ;;
    esac
done

# Default: extract + compile
if [ -z "$COMMAND" ]; then
    COMMAND="all"
fi

# ---------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------

if [ -z "$LUPDATE" ]; then
    echo "Error: lupdate not found."
    echo "Set --qt-dir or EMULE_QT_DIR to your Qt installation (e.g. /Users/you/Qt/6.x.x/macos)"
    exit 1
fi

if [ -z "$LRELEASE" ]; then
    echo "Error: lrelease not found."
    echo "Set --qt-dir or EMULE_QT_DIR to your Qt installation (e.g. /Users/you/Qt/6.x.x/macos)"
    exit 1
fi

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

# Collect all existing .ts files
existing_ts_files() {
    find "$TRANSLATIONS_DIR" -name "${TS_BASENAME}_*.ts" -type f 2>/dev/null | sort
}

# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

do_extract() {
    echo "=== Extracting translatable strings ==="
    echo "  lupdate: $LUPDATE"
    echo "  Sources: ${SOURCE_DIRS[*]}"

    mkdir -p "$TRANSLATIONS_DIR"

    local ts_files
    ts_files="$(existing_ts_files)"

    if [ -z "$ts_files" ]; then
        echo "  No .ts files found. Creating English source template..."
        ts_files="$TRANSLATIONS_DIR/${TS_BASENAME}_en.ts"
    fi

    local lupdate_args=()
    for src in "${SOURCE_DIRS[@]}"; do
        lupdate_args+=("$src")
    done

    lupdate_args+=("-extensions" "cpp,h,ui,qml")
    lupdate_args+=("-locations" "relative")

    if [ "$NO_OBSOLETE" = true ]; then
        lupdate_args+=("-no-obsolete")
    fi

    lupdate_args+=("-ts")
    for ts in $ts_files; do
        lupdate_args+=("$ts")
    done

    echo "  Running: lupdate ${lupdate_args[*]}"
    "$LUPDATE" "${lupdate_args[@]}"

    echo ""
    echo "  Extraction complete. Translation files:"
    for ts in $ts_files; do
        local count
        count=$(grep -c '<message' "$ts" 2>/dev/null || echo "0")
        echo "    $(basename "$ts")  ($count strings)"
    done
}

do_compile() {
    echo "=== Compiling translation files ==="
    echo "  lrelease: $LRELEASE"

    local ts_files
    ts_files="$(existing_ts_files)"

    if [ -z "$ts_files" ]; then
        echo "  No .ts files found. Run 'extract' first."
        exit 1
    fi

    for ts in $ts_files; do
        local qm="${ts%.ts}.qm"
        echo "  $(basename "$ts") -> $(basename "$qm")"
        "$LRELEASE" -silent "$ts" -qm "$qm"
    done

    echo "  Compilation complete."
}

do_add() {
    if [ ${#LANGUAGES[@]} -eq 0 ]; then
        echo "Error: specify at least one language code (e.g. de, fr, zh_CN)"
        exit 1
    fi

    mkdir -p "$TRANSLATIONS_DIR"

    for lang in "${LANGUAGES[@]}"; do
        local ts="$TRANSLATIONS_DIR/${TS_BASENAME}_${lang}.ts"
        if [ -f "$ts" ]; then
            echo "  Already exists: $(basename "$ts")"
        else
            echo "  Creating: $(basename "$ts")"
            # Run lupdate to create the new .ts file with all current strings
            local lupdate_args=()
            for src in "${SOURCE_DIRS[@]}"; do
                lupdate_args+=("$src")
            done
            lupdate_args+=("-extensions" "cpp,h,ui,qml")
            lupdate_args+=("-locations" "relative")
            lupdate_args+=("-ts" "$ts")
            "$LUPDATE" "${lupdate_args[@]}" 2>&1 | grep -v "^Scanning" || true
            local count
            count=$(grep -c '<message' "$ts" 2>/dev/null || echo "0")
            echo "    $count strings to translate"
        fi
    done

    echo ""
    echo "Open .ts files in Qt Linguist to add translations:"
    echo "  $LUPDATE/../linguist"
}

do_status() {
    echo "=== Translation Status ==="
    echo ""

    local ts_files
    ts_files="$(existing_ts_files)"

    if [ -z "$ts_files" ]; then
        echo "  No .ts files found. Run './scripts/localize.sh add <lang>' to start."
        exit 0
    fi

    printf "  %-30s %8s %10s %10s %8s\n" "File" "Total" "Finished" "Unfinished" "Done"
    printf "  %-30s %8s %10s %10s %8s\n" "----" "-----" "--------" "----------" "----"

    for ts in $ts_files; do
        local total finished unfinished pct
        total=$(grep -c '<message' "$ts" 2>/dev/null || echo "0")
        unfinished=$(grep -c 'type="unfinished"' "$ts" 2>/dev/null || echo "0")
        finished=$((total - unfinished))
        if [ "$total" -gt 0 ]; then
            pct=$((finished * 100 / total))
        else
            pct=0
        fi
        printf "  %-30s %8d %10d %10d %7d%%\n" "$(basename "$ts")" "$total" "$finished" "$unfinished" "$pct"
    done
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

case "$COMMAND" in
    extract)
        do_extract
        ;;
    compile)
        do_compile
        ;;
    add)
        do_add
        ;;
    status)
        do_status
        ;;
    all)
        do_extract
        echo ""
        do_compile
        ;;
esac
