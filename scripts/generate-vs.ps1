#Requires -Version 5.1
<#
.SYNOPSIS
    generate-vs.ps1 -- generate a Visual Studio solution for eMule Qt.

.DESCRIPTION
    Installs the src\vcpkg.json dependencies through vcpkg manifest mode, detects
    Qt and the installed Visual Studio, then configures a CMake build tree with a
    Visual Studio generator.

    The solution lands in build-vs\ rather than build\ so that it can coexist
    with the Ninja tree build-win.ps1 produces: CMake refuses to reuse a build
    directory that was configured with a different generator.

    Afterwards, open the solution the final line names -- eMuleQt.slnx with a
    Visual Studio 2026 generator, eMuleQt.sln with an older one.  Either way it
    is a separate thing from the hand-maintained src\eMuleQt.sln that
    docs\windows-build.md describes.

.EXAMPLE
    scripts\generate-vs.ps1
    scripts\generate-vs.ps1 -QtDir C:\Qt\6.11.2\msvc2022_64
    scripts\generate-vs.ps1 -Generator "Visual Studio 17 2022"

.NOTES
    Dot-sourcing build-win.ps1 re-runs its param block in this scope, so -QtDir
    and -BuildDir are passed straight through below.  -Generator has no
    counterpart there and therefore survives untouched.
#>
[CmdletBinding()]
param(
    [string]$QtDir,
    [string]$BuildDir,
    [string]$Generator
)

$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\build-win.ps1" -QtDir $QtDir -BuildDir $BuildDir

$projectDir = Get-ProjectDir
if (-not $BuildDir) { $BuildDir = Join-Path $projectDir 'build-vs' }

# -- Tools -------------------------------------------------------------------

$cmake = Resolve-BuildTool -Name cmake
if (-not $cmake) {
    throw 'CMake not found. Install from https://cmake.org/download/'
}

# vcpkg clones its registry, and CMakeLists.txt pulls third-party dependencies
# via FetchContent, so git is needed even when vcpkg is already bootstrapped.
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw 'git not found. Install from https://git-scm.com/download/win'
}

# -- Dependencies ------------------------------------------------------------

# No -BuildDir here on purpose: the clone belongs in build\vcpkg, shared with the
# Ninja tree, rather than a second copy underneath build-vs\.
$toolchain = Resolve-VcpkgToolchain
if (-not $toolchain) { $toolchain = Install-Vcpkg }
Write-Host "Using vcpkg: $toolchain"

# -- Qt ----------------------------------------------------------------------

$qtArgs = @()
$qt = Resolve-QtDir -QtDir $QtDir
if ($qt) {
    Write-Host "Using Qt: $qt"
    $qtArgs = @("-DCMAKE_PREFIX_PATH=$qt")
}
else {
    Write-Warning 'Qt not auto-detected. Pass it explicitly:'
    Write-Warning '  scripts\generate-vs.ps1 -QtDir C:\Qt\6.11.2\msvc2022_64'
    Write-Warning 'Continuing without a Qt path hint -- find_package(Qt6) will likely fail.'
}

# -- Generator ---------------------------------------------------------------

$vsGenerator = Resolve-VsGenerator -Generator $Generator
if (-not $vsGenerator) {
    throw "No installed Visual Studio with the C++ workload found.`n" +
          "  Install the 'Desktop development with C++' workload, or name a`n" +
          '  generator explicitly: -Generator "Visual Studio 17 2022"'
}
Write-Host "Using generator: $vsGenerator"

# -- Configure ---------------------------------------------------------------

Write-Host ''
Write-Host "=== Generating $vsGenerator solution ==="
# VCPKG_MANIFEST_MODE must be set explicitly: vcpkg only turns it on by itself
# when vcpkg.json sits at the CMake source root, and this project keeps the
# manifest in src\.  Without these three flags the dependencies are never
# installed and configure fails on the first find_package (ZLIB).
& $cmake -S $projectDir -B $BuildDir `
    -G $vsGenerator -A x64 `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    '-DVCPKG_MANIFEST_MODE=ON' `
    "-DVCPKG_MANIFEST_DIR=$(Join-Path $projectDir 'src')" `
    "-DVCPKG_TARGET_TRIPLET=$(Resolve-VcpkgTriplet)" `
    @qtArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed (exit $LASTEXITCODE)."
}

Write-Host ''
Write-Host '=== Done ==='
# Look the file up rather than naming it: Visual Studio 2026 generators write the
# new XML solution format (eMuleQt.slnx), older ones write eMuleQt.sln.
$solution = Get-ChildItem -Path $BuildDir -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -in '.slnx', '.sln' } |
    Select-Object -First 1
if ($solution) {
    Write-Host "Open $($solution.FullName) in Visual Studio."
}
else {
    Write-Warning "Configure succeeded but no solution file was found in $BuildDir."
}
