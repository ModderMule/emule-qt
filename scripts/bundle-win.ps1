#Requires -Version 5.1
<#
.SYNOPSIS
    bundle-win.ps1 -- build and package eMule Qt for Windows.

.DESCRIPTION
    Builds the daemon and GUI, bundles Qt runtime DLLs via windeployqt, copies
    the default config data, and creates a self-contained zip.

    Layout inside the zip:
      eMule\
        emuleqt.exe          GUI executable
        emulecored.exe       daemon executable
        config\              default config data (nodes.dat, eMule.tmpl,
                             server.met, webserver\...)
        lang\                compiled translation files (.qm)
        [Qt DLLs, platforms\, styles\, tls\, etc.]

.EXAMPLE
    scripts\bundle-win.ps1
    scripts\bundle-win.ps1 -QtDir C:\Qt\6.11.2\msvc2022_64 -Config Release
    scripts\bundle-win.ps1 -NoBuild
#>
[CmdletBinding()]
param(
    [string]$QtDir,
    [ValidateSet('Release', 'Debug')][string]$Config = 'Release',
    [switch]$NoBuild,
    [string]$BuildDir,
    # Treat missing vcpkg DLLs as an error rather than a note.  CI passes this: a
    # bundle without z.dll / libcrypto-3-x64.dll is a broken release, and the
    # artifact check downstream only verifies that a zip exists.
    [switch]$RequireVcpkg
)

$ErrorActionPreference = 'Stop'

# Dot-sourcing re-runs build-win.ps1's param block in this scope, which would
# reset the parameters we share names with -- so pass ours straight through.
. "$PSScriptRoot\build-win.ps1" -Config $Config -QtDir $QtDir -BuildDir $BuildDir -NoBuild:$NoBuild

$projectDir = Get-ProjectDir
if (-not $BuildDir) { $BuildDir = Join-Path $projectDir 'build' }

$appVersion = Get-AppVersion -ProjectDir $projectDir
if (-not $appVersion) { throw "Could not read VERSION from $projectDir\CMakeLists.txt" }

# -- Qt ----------------------------------------------------------------------

$qt = Resolve-QtDir -QtDir $QtDir
if (-not $qt) {
    throw "Qt directory not found. Pass it explicitly:`n" +
          "  scripts\bundle-win.ps1 -QtDir C:\Qt\6.11.2\msvc2022_64"
}
Write-Host "Using Qt: $qt"

$windeployqt = Join-Path $qt 'bin\windeployqt.exe'
if (-not (Test-Path $windeployqt)) {
    throw "windeployqt not found at $windeployqt"
}

# -- Configure & Build -------------------------------------------------------

if ($NoBuild) {
    Write-Host ''
    Write-Host '=== Skipping build (-NoBuild) ==='
}
else {
    Invoke-EMuleBuild -Config $Config -QtDir $qt -BuildDir $BuildDir
}

# -- Locate binaries ---------------------------------------------------------

# With -NoBuild the Visual Studio output in bin\ is the likely source; after a
# CMake build, the CMake output directories win.
$guiResult = Find-EMuleBinary -Name 'emuleqt.exe' -Config $Config -BuildDir $BuildDir -PreferVsOutput:$NoBuild
$daemonResult = Find-EMuleBinary -Name 'emulecored.exe' -Config $Config -BuildDir $BuildDir -PreferVsOutput:$NoBuild

foreach ($result in @($guiResult, $daemonResult)) {
    if (-not $result.Path) {
        Write-Host 'Error: binary not found.' -ForegroundColor Red
        foreach ($probe in $result.Probed) { Write-Host "  Checked: $probe" }
        exit 1
    }
}

Write-Host ''
Write-Host "GUI binary:    $($guiResult.Path)"
Write-Host "Daemon binary: $($daemonResult.Path)"

# -- Assemble staging directory ----------------------------------------------

$stageRoot = Join-Path $projectDir 'stage'
$stageDir = Join-Path $stageRoot 'eMule'
if (Test-Path $stageRoot) { Remove-Item -Recurse -Force $stageRoot }
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

Write-Host ''
Write-Host '=== Staging binaries ==='
Copy-Item $guiResult.Path (Join-Path $stageDir 'emuleqt.exe') -Force
Copy-Item $daemonResult.Path (Join-Path $stageDir 'emulecored.exe') -Force
Write-Host '  emuleqt.exe'
Write-Host '  emulecored.exe'

# -- Copy config data --------------------------------------------------------

$configSrc = Join-Path $projectDir 'data\config'
if (Test-Path $configSrc) {
    Write-Host ''
    Write-Host '=== Copying config data ==='
    Copy-Item $configSrc (Join-Path $stageDir 'config') -Recurse -Force
    Write-Host '  config\ copied'
}
else {
    Write-Warning "$configSrc not found -- skipping config data."
}

# -- Copy translation files --------------------------------------------------

# Prefer build-generated .qm files; fall back to the source lang\ directory.
$langDst = Join-Path $stageDir 'lang'
New-Item -ItemType Directory -Force -Path $langDst | Out-Null

$langCopied = $false
$langSources = @(
    (Join-Path $BuildDir "src\gui\$Config")
    (Join-Path $BuildDir 'src\gui')
    (Join-Path $projectDir "src\gui\$Config")
    (Join-Path $projectDir 'lang')
)
foreach ($langSrc in $langSources) {
    if (-not (Test-Path $langSrc)) { continue }
    $qmFiles = Get-ChildItem -Path $langSrc -Filter 'emuleqt_*.qm' -ErrorAction SilentlyContinue
    if ($qmFiles) {
        $qmFiles | Copy-Item -Destination $langDst -Force
        $langCopied = $true
    }
}
if ($langCopied) {
    Write-Host ''
    Write-Host '=== Copying translation files ==='
    Write-Host '  lang\ copied'
}
else {
    Write-Warning 'No .qm translation files found -- skipping lang data.'
}

# -- Run windeployqt ---------------------------------------------------------

Write-Host ''
Write-Host '=== Running windeployqt ==='
$deployMode = if ($Config -eq 'Debug') { '--debug' } else { '--release' }
& $windeployqt $deployMode --no-translations --no-system-d3d-compiler --no-opengl-sw (Join-Path $stageDir 'emuleqt.exe')
if ($LASTEXITCODE -ne 0) {
    Write-Warning 'windeployqt reported errors (continuing).'
}

# -- Copy OpenSSL DLLs if present --------------------------------------------

# Qt's network module needs OpenSSL at runtime.  windeployqt does not always
# copy them, so we look in the Qt bin directory and in the system-wide install.
#
# vcpkg goes first: that is the OpenSSL the build actually linked against, and the
# loop below stops at the first hit.  With a system OpenSSL first we would stage
# one build's DLLs against another build's import libraries and rely on the later
# blanket vcpkg copy to overwrite them.
$vcpkgBin = Resolve-VcpkgBinDir -Config $Config -BuildDir $BuildDir -ProjectDir $projectDir

$opensslSources = @()
if ($vcpkgBin) { $opensslSources += $vcpkgBin }
$opensslSources += @(
    (Join-Path $qt 'bin')
    'C:\Program Files\OpenSSL-Win64\bin'
    'C:\OpenSSL-Win64\bin'
)
foreach ($opensslDir in $opensslSources) {
    if (Test-Path (Join-Path $stageDir 'libssl-3-x64.dll')) { break }
    if (-not (Test-Path (Join-Path $opensslDir 'libssl-3-x64.dll'))) { continue }
    Write-Host "  Copying OpenSSL DLLs from $opensslDir"
    Copy-Item (Join-Path $opensslDir 'libssl-3-x64.dll') $stageDir -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $opensslDir 'libcrypto-3-x64.dll') $stageDir -Force -ErrorAction SilentlyContinue
}

# -- Copy MSVC C++ runtime DLLs ----------------------------------------------

# windeployqt does not bundle the Visual C++ runtime (vcruntime140.dll,
# msvcp140.dll, concrt140.dll, etc.), so the app would need the VC++
# Redistributable installed on the target without this.
#
# Copy every DLL in the redist folder rather than a fixed list: that folder is
# by definition the redistributable set, and a fixed list silently misses
# additions such as msvcp140_atomic_wait.dll, which C++23 std::atomic::wait
# pulls in.
$vcRedistDir = Resolve-VcRedistDir
if ($vcRedistDir) {
    Write-Host ''
    Write-Host "=== Copying MSVC runtime DLLs from $vcRedistDir ==="
    $vcDlls = Get-ChildItem -Path $vcRedistDir -Filter '*.dll' -ErrorAction SilentlyContinue
    $vcDlls | Copy-Item -Destination $stageDir -Force -ErrorAction SilentlyContinue
    Write-Host "  $($vcDlls.Count) MSVC runtime DLL(s) copied"
}
else {
    Write-Warning 'MSVC runtime DLLs not found -- install VC++ Redistributable on target.'
    Write-Warning '  Checked: VCToolsRedistDir, VSINSTALLDIR, installed Visual Studio paths.'
}

# -- Copy vcpkg runtime DLLs -------------------------------------------------

# $vcpkgBin was resolved above, for the OpenSSL search.
if (-not $vcpkgBin) {
    $msg = 'No vcpkg install root found -- z.dll, libcrypto-3-x64.dll, ' +
           'archive.dll and friends will be MISSING from the zip.'
    if ($RequireVcpkg) { throw $msg }
    Write-Warning $msg
}
else {
    Write-Host ''
    Write-Host "=== Copying vcpkg DLLs from $vcpkgBin ==="
    $vcpkgDlls = @(Get-ChildItem -Path $vcpkgBin -Filter '*.dll' -ErrorAction SilentlyContinue)
    if ($RequireVcpkg -and $vcpkgDlls.Count -eq 0) {
        throw "vcpkg bin directory '$vcpkgBin' contains no DLLs."
    }
    # No -ErrorAction SilentlyContinue on the copy: $ErrorActionPreference is Stop,
    # and a copy that fails must not be reported as a success.
    $vcpkgDlls | Copy-Item -Destination $stageDir -Force
    Write-Host "  $($vcpkgDlls.Count) vcpkg DLL(s) copied"
}

# -- Create zip --------------------------------------------------------------

$zipName = "eMuleQt-v$appVersion-win64.zip"
$zipPath = Join-Path $BuildDir $zipName
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

Write-Host ''
Write-Host "=== Creating $zipName ==="
# Compress-Archive can produce zips that extractors flag as password-protected,
# so we shell out to 7z instead.
$sevenZip = Resolve-SevenZip
if (-not $sevenZip) {
    throw '7z.exe not found -- install 7-Zip (https://www.7-zip.org/).'
}
& $sevenZip a -tzip -mx=7 $zipPath $stageDir
if ($LASTEXITCODE -ne 0) { throw '7z reported an error.' }

if (-not (Test-Path $zipPath)) {
    throw 'Failed to create zip file.'
}

Write-Host ''
Write-Host '=== Done ==='
Write-Host "Package: $zipPath"
Write-Host ''
Write-Host 'Staging contents:'
Get-ChildItem $stageDir | ForEach-Object { Write-Host "  $($_.Name)" }
