# Building eMuleQt on Windows

## Prerequisites

- **Visual Studio 2022** (v143 toolset)
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
