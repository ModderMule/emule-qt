#!/usr/bin/env bash
# Build emulecored in Debug mode and run under lldb to catch crashes.
set -e
cd "$(dirname "$0")/.."
cmake --build build --target emulecored --config Debug 2>&1 | tail -3
lldb -o run -o 'bt all' -o quit -- ./build/src/daemon/emulecored