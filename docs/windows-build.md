# Building eMuleQt on Windows

## Prerequisites

- **Visual Studio 2022 or later** (the `src/eMuleQt.sln` projects target the v143 toolset)
- **Qt 6.8.3** (or later) for MSVC 2022 x64 (with `httpserver` module)
- **Qt VS Tools** extension (v3.04+)
- **vcpkg** package manager

## Install vcpkg dependencies

```powershell
# Clone vcpkg if not already installed
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat

# Set VCPKG_ROOT environment variable
[System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\vcpkg", "User")

# Install dependencies (from src/ where the .sln lives)
cd path\to\eMuleQt\src
C:\vcpkg\vcpkg install --triplet x64-windows
```

This installs: `zlib`, `openssl`, `yaml-cpp`, `libarchive`, `miniupnpc`.

> **Visual Studio manifest mode:** If you have vcpkg integrated with VS (`vcpkg integrate install`), opening the solution should auto-install dependencies from `src/vcpkg.json`. If it doesn't, run `vcpkg install` manually from `src/` as shown above.

## Qt Setup

1. Install Qt 6.8.3 (or later) via the Qt Online Installer
2. Select the **MSVC 2022 64-bit** kit and the **Qt HTTP Server** additional module
3. In Qt VS Tools, register the installation as `6.8.3_msvc2022_64`

## Build

1. Open `src/eMuleQt.sln` in Visual Studio 2022
2. Select **Release|x64** or **Debug|x64**
3. Build Solution (Ctrl+Shift+B)

The .vcxproj files reference vcpkg via `$(VCPKG_ROOT)\installed\x64-windows\include` and `\lib`. If you installed vcpkg elsewhere, ensure the `VCPKG_ROOT` environment variable is set correctly.

## Fallback: Manual library installation

If not using vcpkg, install libraries to the paths expected by the project files:

| Library | Expected include path | Expected lib path |
|---------|----------------------|-------------------|
| OpenSSL | `C:\Program Files\OpenSSL-Win64\include` | `C:\Program Files\OpenSSL-Win64\lib` |
| zlib | `C:\Program Files\zlib\include` | `C:\Program Files\zlib\lib` |
| miniupnpc | `C:\Program Files\miniupnpc\include` | `C:\Program Files\miniupnpc\lib` |
| yaml-cpp | `C:\Program Files\yaml-cpp\include` | `C:\Program Files\yaml-cpp\lib` |
| libarchive | `C:\Program Files\libarchive\include` | `C:\Program Files\libarchive\lib` |

Note: The zlib library file must be named `zlib.lib` (standard Windows/vcpkg name), not `z.lib`.

## Scripted build, bundle and debug run

Four PowerShell scripts cover the command-line workflow. They share their Qt, vcpkg, Visual Studio
and binary detection: `build-win.ps1` defines those helpers, and the other three dot-source it.

```powershell
# Configure (only when needed) and build
scripts\build-win.ps1
scripts\build-win.ps1 -Clean -Config Debug

# On a machine without vcpkg the first run clones and bootstraps it automatically
# (the dependencies are required, so configure cannot succeed without them).
# Opt out only when the libraries are installed to the paths listed above:
scripts\build-win.ps1 -NoVcpkg

# Package a self-contained zip into build\
scripts\bundle-win.ps1
scripts\bundle-win.ps1 -NoBuild        # package an existing Visual Studio build

# Generate a Visual Studio solution into build-vs\ with a CMake VS generator
scripts\generate-vs.ps1
scripts\generate-vs.ps1 -Generator "Visual Studio 17 2022"

# Build, start the daemon, then run the GUI against it
scripts\debug-gui.ps1
scripts\debug-gui.ps1 --screenshot C:\temp\kad.png --tab kad --subtab 1 --delay 8000
scripts\debug-gui.ps1 -Debugger        # run the GUI under cdb, or the VS debugger
scripts\debug-gui.ps1 -NoBuild         # skip the build, use bin\Release\
```

If the execution policy blocks them, run via
`powershell -NoProfile -ExecutionPolicy Bypass -File scripts\debug-gui.ps1 ...`.

Qt is auto-detected from `C:\Qt\6.*\msvc*_64` (newest first) or `%USERPROFILE%\Qt\...`, and can be
overridden with `-QtDir`. `QT_ROOT_DIR`, `QTDIR` and `CMAKE_PREFIX_PATH` are honoured too, which is
how CI passes its kit.

### generate-vs.ps1

Bootstraps vcpkg if needed, then configures a CMake build tree with a Visual Studio generator and
manifest-mode dependencies. The result is a *generated* solution in `build-vs\`, separate from the
hand-maintained `src\eMuleQt.sln` above — `eMuleQt.slnx` with a Visual Studio 2026 generator (the
new XML solution format), `eMuleQt.sln` with an older one. The script prints the path it produced.
It uses `build-vs\` rather than `build\` so it can coexist with the Ninja tree the other scripts
use; CMake will not reuse a build directory configured with a different generator. The vcpkg clone
stays shared at `build\vcpkg`.

The generator is detected at runtime by intersecting the versions `cmake --help` lists with the
installations `vswhere` reports, so a newer Visual Studio needs no edit. That intersection matters:
`cmake --help` lists generators for Visual Studio versions that are not installed, and picking one
of those fails late with `MSB8020: The build tools ... (Platform Toolset = 'v143') cannot be found`.
Override with `-Generator "Visual Studio 17 2022"` when several are installed.

### debug-gui.ps1

The Windows counterpart of `scripts/debug-gui.sh`: it builds, kills any leftover daemon, starts
`emulecored`, waits for its IPC port to listen, then runs `emuleqt` in the foreground with every
remaining argument passed through untouched. The daemon is always stopped again on exit, including
on Ctrl-C.

It is PowerShell rather than a `.bat` because the daemon lifecycle needs things `cmd` lacks: a PID
for the background daemon (`taskkill /IM` would kill every `emulecored`), a `finally` block to
replace `trap EXIT`, sub-second sleeps, and above all a foreground wait — `emuleqt` is built
`WIN32_EXECUTABLE`, and `start` does not wait for a GUI-subsystem process, so the daemon would be
killed before a screenshot ever landed.
