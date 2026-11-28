#!/usr/bin/env bash
# Build qDonkey (libs/qDonkey) and run it on a simulator, paired with the local core.
# Usage: scripts/qdonkey-run.sh [android|ios] [--no-build] [--log] [--fresh]
#   android     Android emulator, AVD $QDONKEY_AVD (default qDonkey_API_37); default target
#   ios         iOS simulator $QDONKEY_SIM (default: first available iPhone); needs the
#               ios_simulator_arm64 kit (Qt 6.12+)
#   --no-build  install the last build
#   --log       follow the app's log after start (Ctrl-C ends it)
#   --fresh     clear the app's data first (profiles, pins, settings)
# The core: a running one on ipc.port is used; else build/src/daemon/emulecored is started and
# killed again when this script ends. Config: $EMULE_CONFIG (default ~/eMuleQt/Config).
# The emulator reaches the Mac's loopback as 10.0.2.2, so ipc.listenAddress 127.0.0.1 is enough.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$(pwd)
QD="$ROOT/libs/qDonkey"

TARGET=android
BUILD=true
LOG=false
FRESH=false
for a in "$@"; do
    case "$a" in
        android|ios) TARGET=$a ;;
        --no-build) BUILD=false ;;
        --log) LOG=true ;;
        --fresh) FRESH=true ;;
        -h|--help) sed -n '2,13p' "$0"; exit 0 ;;
        *) echo "unknown argument: $a" >&2; exit 2 ;;
    esac
done

QT_VER=${QT_VER:-6.12.1}
QT_DIR=${QT_DIR:-$HOME/Qt/$QT_VER}
NINJA=${NINJA:-$HOME/Qt/Tools/Ninja/ninja}
CONFIG=${EMULE_CONFIG:-$HOME/eMuleQt/Config}
PREFS="$CONFIG/preferences.yml"

# --- core -------------------------------------------------------------------------------------
# ipc block of preferences.yml: port + first token
yaml_ipc() {
    awk -v key="$1" '
        /^ipc:/ { inipc = 1; next }
        inipc && /^[^ ]/ { exit }
        inipc && key == "port" && $1 == "port:" { print $2; exit }
        inipc && key == "token" && $1 == "tokens:" { intok = 1; next }
        intok && $1 == "-" { print $2; exit }
    ' "$PREFS"
}
[ -f "$PREFS" ] || { echo "no $PREFS (set EMULE_CONFIG)" >&2; exit 1; }
IPC_PORT=$(yaml_ipc port); IPC_PORT=${IPC_PORT:-4712}
TOKEN=$(yaml_ipc token)
[ -n "$TOKEN" ] || { echo "no ipc.tokens entry in $PREFS: add one in Options > General > Core" >&2; exit 1; }

CORE_PID=""
cleanup() {
    if [ -n "$CORE_PID" ]; then
        echo "Stopping the core this script started (pid $CORE_PID)"
        kill "$CORE_PID" 2>/dev/null || true
        wait "$CORE_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

if nc -z 127.0.0.1 "$IPC_PORT" 2>/dev/null; then
    echo "Core already listening on 127.0.0.1:$IPC_PORT"
else
    if $BUILD || [ ! -x build/src/daemon/emulecored ]; then
        scripts/build.sh --target emulecored
    fi
    echo "Starting the core ($CONFIG)"
    ./build/src/daemon/emulecored --config "$CONFIG" &
    CORE_PID=$!
    for _ in $(seq 1 60); do
        nc -z 127.0.0.1 "$IPC_PORT" 2>/dev/null && break
        kill -0 "$CORE_PID" 2>/dev/null || { echo "core exited" >&2; CORE_PID=""; exit 1; }
        sleep 0.5
    done
    nc -z 127.0.0.1 "$IPC_PORT" || { echo "core not listening on $IPC_PORT" >&2; exit 1; }
fi

# a build dir configured with another Qt kit: start over
fresh_build_dir() {
    if [ -f "$1/CMakeCache.txt" ] && ! grep -q "$QT_DIR/" "$1/CMakeCache.txt"; then
        echo "Removing $1 (configured for another Qt)"
        rm -rf "$1"
    fi
}

# --- iOS --------------------------------------------------------------------------------------
if [ "$TARGET" = ios ]; then
    KIT="$QT_DIR/ios_simulator_arm64"
    [ -x "$KIT/bin/qt-cmake" ] || { echo "no Qt $QT_VER ios_simulator_arm64 kit (MaintenanceTool)" >&2; exit 1; }
    BUILD_DIR="$QD/build-ios-sim"
    BUNDLE_ID=org.emule-qt.qdonkey
    APP="$BUILD_DIR/Release-iphonesimulator/qDonkey.app"
    if $BUILD; then
        fresh_build_dir "$BUILD_DIR"
        # cmake again each run: Xcode links the file list of the last configure
        "$KIT/bin/qt-cmake" -S "$QD" -B "$BUILD_DIR" -G Xcode \
            -DQT_HOST_PATH="$QT_DIR/macos" -DCMAKE_OSX_SYSROOT=iphonesimulator \
            -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 >/dev/null
        xcodebuild -project "$BUILD_DIR/qDonkey.xcodeproj" -scheme qdonkey -configuration Release \
            -sdk iphonesimulator -arch arm64 CODE_SIGNING_ALLOWED=NO -quiet build
    fi
    [ -d "$APP" ] || { echo "no app at $APP (run without --no-build)" >&2; exit 1; }

    # simulator: a booted one, else $QDONKEY_SIM or the first available iPhone
    SIM=$(xcrun simctl list devices booted | awk -F'[()]' '/iPhone/ { print $2; exit }')
    if [ -z "$SIM" ]; then
        if [ -n "${QDONKEY_SIM:-}" ]; then
            SIM=$QDONKEY_SIM
        else
            SIM=$(xcrun simctl list devices available | awk -F'[()]' '/iPhone/ { print $2; exit }')
        fi
        [ -n "$SIM" ] || { echo "no iPhone simulator (Xcode > Settings > Components)" >&2; exit 1; }
        echo "Booting simulator $SIM"
        xcrun simctl boot "$SIM" 2>/dev/null || true
    fi
    # the window: Xcode 27 shows simulators in DeviceHub (replaces Simulator.app)
    open -a DeviceHub 2>/dev/null || open -a Simulator 2>/dev/null \
        || echo "no DeviceHub/Simulator app found: running headless"
    xcrun simctl bootstatus "$SIM" -b >/dev/null
    echo "Simulator $SIM booted"

    $FRESH && xcrun simctl uninstall "$SIM" "$BUNDLE_ID" 2>/dev/null || true
    xcrun simctl terminate "$SIM" "$BUNDLE_ID" 2>/dev/null || true
    xcrun simctl install "$SIM" "$APP"

    # pair link as launch argument (main.cpp): openurl asks "Open in qDonkey?" first, which needs
    # a click in the window. The simulator shares the Mac's loopback.
    xcrun simctl launch "$SIM" "$BUNDLE_ID" "qdonkey://pair?h=127.0.0.1&p=$IPC_PORT&t=$TOKEN" >/dev/null
    echo "qDonkey started on $SIM, paired with 127.0.0.1:$IPC_PORT"

    if $LOG; then
        xcrun simctl spawn "$SIM" log stream --level debug --predicate 'process == "qDonkey"'
    elif [ -n "$CORE_PID" ]; then
        echo "Core runs until Ctrl-C"
        wait "$CORE_PID"
    fi
    exit 0
fi

# --- Android ----------------------------------------------------------------------------------
SDK=${ANDROID_SDK_ROOT:-$HOME/Library/Android/sdk}
NDK=${ANDROID_NDK_ROOT:-$SDK/ndk/30.0.16248370}
export JAVA_HOME=${JAVA_HOME:-/Library/Java/JavaVirtualMachines/jdk-22.jdk/Contents/Home}
ADB="$SDK/platform-tools/adb"
BT="$SDK/build-tools/36.1.0"
AVD=${QDONKEY_AVD:-qDonkey_API_37}
BUILD_DIR="$QD/build-android"
PKG=org.emuleqt.qdonkey
APK_UNSIGNED="$BUILD_DIR/android-build/build/outputs/apk/release/android-build-release-unsigned.apk"
APK="$BUILD_DIR/qdonkey.apk"

if $BUILD; then
    fresh_build_dir "$BUILD_DIR"
    if [ ! -f "$BUILD_DIR/build.ninja" ]; then
        "$QT_DIR/android_arm64_v8a/bin/qt-cmake" -S "$QD" -B "$BUILD_DIR" -G Ninja \
            -DCMAKE_MAKE_PROGRAM="$NINJA" -DCMAKE_BUILD_TYPE=Release \
            -DANDROID_SDK_ROOT="$SDK" -DANDROID_NDK_ROOT="$NDK" \
            -DQT_HOST_PATH="$QT_DIR/macos"
    fi
    cmake --build "$BUILD_DIR"
fi
if [ ! -f "$APK" ] || [ "$APK_UNSIGNED" -nt "$APK" ]; then
    [ -f "$APK_UNSIGNED" ] || { echo "no APK at $APK_UNSIGNED (run without --no-build)" >&2; exit 1; }
    # release APK is unsigned: sign with the debug key
    "$BT/zipalign" -f -p 4 "$APK_UNSIGNED" "$APK.aligned"
    "$BT/apksigner" sign --ks "$HOME/.android/debug.keystore" --ks-pass pass:android \
        --key-pass pass:android --out "$APK" "$APK.aligned"
    rm -f "$APK.aligned"
fi

# emulator: reuse a running one, never touch an attached phone
EMU=$("$ADB" devices | awk '$1 ~ /^emulator-/ && $2 == "device" { print $1; exit }')
if [ -z "$EMU" ]; then
    # Mac keyboard into the emulator: off by default in new AVDs, read at boot
    AVD_INI="$HOME/.android/avd/$AVD.avd/config.ini"
    if [ -f "$AVD_INI" ] && ! grep -q '^hw.keyboard *= *yes' "$AVD_INI"; then
        sed -i '' -e '/^hw.keyboard *=/d' "$AVD_INI"
        echo "hw.keyboard=yes" >> "$AVD_INI"
    fi
    echo "Starting emulator $AVD"
    nohup "$SDK/emulator/emulator" -avd "$AVD" -no-snapshot-save >/dev/null 2>&1 &
    for _ in $(seq 1 120); do
        EMU=$("$ADB" devices | awk '$1 ~ /^emulator-/ { print $1; exit }')
        [ -n "$EMU" ] && break
        sleep 1
    done
    [ -n "$EMU" ] || { echo "emulator did not show up" >&2; exit 1; }
fi
A=("$ADB" -s "$EMU")
"${A[@]}" wait-for-device
until [ "$("${A[@]}" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; do sleep 1; done
echo "Emulator $EMU booted"

"${A[@]}" install -r "$APK" >/dev/null
$FRESH && "${A[@]}" shell pm clear "$PKG" >/dev/null
"${A[@]}" shell pm grant "$PKG" android.permission.POST_NOTIFICATIONS 2>/dev/null || true
"${A[@]}" shell am force-stop "$PKG"

# pair: the VIEW intent starts the app with the link (h/p/t only, no TLS: the core sees loopback)
LINK="qdonkey://pair?h=10.0.2.2&p=$IPC_PORT&t=$TOKEN"
"${A[@]}" shell am start -W -a android.intent.action.VIEW -d "'$LINK'" "$PKG" >/dev/null
echo "qDonkey started on $EMU, paired with 127.0.0.1:$IPC_PORT"

if $LOG; then
    sleep 1
    APP_PID=$("${A[@]}" shell pidof "$PKG" | tr -d '\r')
    "${A[@]}" logcat --pid="$APP_PID"
elif [ -n "$CORE_PID" ]; then
    echo "Core runs until Ctrl-C"
    wait "$CORE_PID"
fi
