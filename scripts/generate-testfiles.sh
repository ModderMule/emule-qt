#!/usr/bin/env bash
# Generate test files for upload/download testing.
# Output: data/incoming/eMuleQt-testfile-20MB.bin       (compressible)
#         data/incoming/eMuleQt-testfile-20MB-random.bin (incompressible)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="$PROJECT_DIR/data/incoming"
SIZE=20971520  # 20 MB

mkdir -p "$OUT_DIR"

# 1) Compressible: repeating 78-byte text pattern
COMPRESSIBLE="$OUT_DIR/eMuleQt-testfile-20MB.bin"
PATTERN="eMuleQt test file data block - this pattern repeats to ensure compressibility. "
python3 -c "
p = b'$PATTERN'
with open('$COMPRESSIBLE', 'wb') as f:
    written = 0
    while written < $SIZE:
        chunk = p * (($SIZE - written) // len(p) or 1)
        f.write(chunk[:$SIZE - written])
        written += len(chunk[:$SIZE - written])
"
echo "Created: $COMPRESSIBLE ($(wc -c < "$COMPRESSIBLE") bytes)"

# 2) Incompressible: random data
RANDOM_FILE="$OUT_DIR/eMuleQt-testfile-20MB-random.bin"
dd if=/dev/urandom of="$RANDOM_FILE" bs=1048576 count=20 2>/dev/null
echo "Created: $RANDOM_FILE ($(wc -c < "$RANDOM_FILE") bytes)"
