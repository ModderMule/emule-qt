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

The .vcxproj files reference vcpkg through paths relative to the solution —
`..\vcpkg_installed\x64-windows\include` and `..\vcpkg_installed\x64-windows\lib` — so
`VCPKG_ROOT` does **not** affect how MSBuild resolves them. What matters is that
`vcpkg install` was run from `src/` (as shown above), which is what creates
`src\vcpkg_installed\`. `VCPKG_ROOT` is still used by the PowerShell scripts to locate
`vcpkg.cmake`.

### Choosing a triplet

The scripts default to `x64-windows`, which builds every dependency twice (debug and
release). Set `VCPKG_TARGET_TRIPLET` to override it — CI uses `x64-windows-release`,
which skips the debug half and roughly halves the dependency build. It has the same
dynamic CRT, the same library linkage and the same DLL names, but no `debug\` subtree,
so `build-win.ps1 -Config Debug` refuses to run against it rather than mixing CRTs.

`scripts\build-win.ps1`, `scripts\generate-vs.ps1`, `scripts\bundle-win.ps1` and
`scripts\debug-gui.ps1` all honour it. The `.vcxproj` files do not — they are hardcoded
to `x64-windows`.

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

## Continuous integration

`.github/workflows/windows.yml` builds the same tree with Ninja rather than MSBuild, and is
the only consumer that used to diverge from the scripted path above. It now runs
**5:59**, down from 17:46 (release run `32633756834` → `32815586508`, 2026-08-25):

| Step | Before | Cold cache | Warm cache |
|---|---|---|---|
| Install dependencies (vcpkg) | 8:56 | 15:25 | **0:23** |
| Configure | 2:56 | 0:09 | **0:12** |
| Build | 4:25 | 4:00 | **3:13** |
| **Total** | **17:46** | ~21:00 | **5:59** |

Almost none of that was compilation — the Build step was always competitive with Linux.
The gap was dependency handling, fixed by three changes:

- **The vcpkg binary cache is persisted with `actions/cache`.** vcpkg's default store
  (`~\AppData\Local\vcpkg\archives`) lives on the ephemeral runner, so it faithfully saved
  every build into a directory destroyed minutes later and rebuilt OpenSSL from source on
  every run.
- **`VCPKG_TARGET_TRIPLET: x64-windows-release`** skips the debug variant of every
  dependency, which alone cost 3:21 of the old 8:56.
- **CI uses manifest mode** against `src/vcpkg.json`, like the scripts already did. It
  previously installed only `zlib openssl` and let `yaml-cpp`, `libarchive` and `miniupnpc`
  fall through to `FetchContent` — 304 of 549 ninja targets, and ~167s of the 174s configure
  spent on libarchive's `try_compile` probes. Ninja now builds 241 targets.

### Cache keys

Use the **split `actions/cache/restore` + `actions/cache/save`** pair, not single-step
`actions/cache`. A cold vcpkg build costs 15 minutes, and the single-step post-action does
not save when a later step fails (`save-always` is deprecated in v4.2). This is not
theoretical: run `32815586508` restored its cache from run `32810604154`, which *failed* —
single-step caching would have thrown that away and paid the 15 minutes again.

Keys carry `${{ github.run_id }}-${{ github.run_attempt }}` because GitHub caches are
immutable: a fixed key would freeze whatever the first run populated, including a partial
population from a run that died half way. Broad `restore-keys` make that safe — vcpkg's store
is content-addressed by package ABI hash, so a stale restore just misses and rebuilds.

The runner's vcpkg commit SHA is deliberately **excluded** from the key. `builtin-baseline`
pins the port trees, so a vcpkg HEAD bump does not change OpenSSL's ABI hash; putting the SHA
in the key would force a total miss on every image update. When MSVC genuinely bumps, the ABI
hash changes and vcpkg rebuilds on its own.

Caches evict after 7 days and this workflow runs on tags and dispatch only, so **most release
builds hit a cold cache**. `.github/workflows/vcpkg-cache-warm.yml.disabled` is a scheduled
job that keeps the archives alive; rename it to `.yml` to enable it. Note that a workflow is
disabled by its **file extension** — GitHub validates every `*.yml` under `.github/workflows/`
on push, so a fully commented-out workflow is an *invalid* workflow rather than an absent one
and fails on every push.

### ccache is blocked by precompiled headers

ccache is wired up (`CMAKE_CXX_COMPILER_LAUNCHER`) but only caches **13 of 222** compiles.
There are two independent blockers and only one is fixed:

1. **`/Zi` — fixed.** `cmake/CompilerSettings.cmake` now emits `/Z7` for Release, because
   ccache cannot cache the separate compiler-side PDB that `/Zi` produces. This was worth
   doing: it took the baseline from `0 / 522 (0.00%)` to non-zero.
2. **`target_precompile_headers` — still open.** `emulecore`
   (`src/core/CMakeLists.txt`) and `emuleqt` (`src/gui/CMakeLists.txt`) both use PCH, which
   MSVC implements as `/Yu` — also uncacheable. 206 of the 221 sources live in those two
   targets, 15 outside, against exactly 209 uncacheable / 13 cacheable.

**PCH and ccache are mutually exclusive on MSVC.** PCH makes cold builds fast; ccache makes
warm ones fast. PCH is currently the better trade — this workflow runs on tags, so its ccache
was logging `No cache found` on essentially every historical run anyway. Gating PCH off when a
compiler launcher is set is the way to test the alternative. Don't file the 5.86% hit rate as
a bug: it is a known trade-off, not a regression.

`/Z7` changes nothing about the shipped PDB — the linker's `/DEBUG` produces it either way.

### Debug symbols

Release builds upload a second artifact, `debug-symbols-win64-v<version>`. A minidump binds
to a PDB by **GUID + age** and every rebuild mints a new GUID, so the only way to symbolise a
crash from a published build is the PDB from that exact build.

It deliberately does **not** match `release.yml`'s `pattern: eMuleQt-v*`, so it never reaches
the release assets — which also means it expires with the normal 90-day artifact retention
while the release itself is permanent. Attaching symbols to a release is a deliberate change
to that allowlist, not something to do by accident.

### Bundle verification

A `dumpbin /dependents` step between Bundle and Upload expands the produced zip and validates
it. It exists because `Resolve-VcpkgBinDir` once hardcoded the triplet: change the triplet
without it and the resolver returned `$null`, `bundle-win.ps1` printed a *note* and continued,
`if-no-files-found: error` still passed, and the **job stayed green** while the released GUI
died at startup with `0xC0000135`. Never trust `if-no-files-found` to mean the artifact is
sound.

Two rules that step had to learn the hard way, both worth preserving in any similar check:

- **Never hardcode a dependency DLL name.** vcpkg ships zlib as **`z.dll`**, not `zlib1.dll`
  — zlib 1.3.2 rewrote its CMakeLists with `OUTPUT_NAME z` and dropped the old Windows
  `SUFFIX "1.dll"` rule. A hardcoded `zlib1.dll` failed a *correct* bundle. The check derives
  its expected set from `build\vcpkg_installed\<triplet>\bin` and asserts every one reached
  the zip. (The import library is still `zlib.lib`; only the DLL was renamed.)
- **Do not fail on a missing OS DLL just because the runner lacks it.** The runner is Windows
  Server; users are on desktop Windows. `mf/mfplat/mfreadwrite/evr` (Media Foundation),
  `uiautomationcore`, `d3d12` and `dxva2` legitimately differ between the two, and Qt's
  multimedia plugins import all of them. `Test-Path System32\x` on the runner is evidence
  about the wrong machine. Fail only on imports that were *supposed* to be bundled — anything
  vcpkg built, or a `Qt6*` module — and merely list the rest. Also stop parsing dumpbin at
  `delay load dependencies:`: those are optional at load time and windeployqt drops some
  deliberately (`--no-opengl-sw`, `--no-system-d3d-compiler`).

### Windows dependency set is now a superset

Sourcing libarchive from vcpkg pulls its default features, so the bundle ships 14 DLLs rather
than the previous 3 — `archive`, `bz2`, `charset-1`, `iconv-2`, `legacy`, `libcrypto-3-x64`,
`liblzma`, `libssl-3-x64`, `libxml2`, `lz4`, `miniupnpc`, `yaml-cpp`, `z`, `zstd`.

The old FetchContent build logged `Could NOT find BZip2 / LibLZMA / LZ4 / ZSTD` while
`ArchiveReader.cpp` calls `archive_read_support_filter_all()`, so Windows silently lacked
bzip2/xz/zstd/lz4 archive support and now has it. Windows is a **superset** of Linux and
macOS here — don't report the extra DLLs as bloat or the new formats as a regression.

`libxml2` and `libiconv` come in solely as libarchive dependencies and cost ~4.6 min of a cold
build (`libiconv` alone is 4.2 min). Dropping libarchive's `libxml2` feature would remove both
and still leave Windows ahead of where it was.
