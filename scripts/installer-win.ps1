#Requires -Version 5.1
<#
.SYNOPSIS
    installer-win.ps1 -- build the Windows setup.exe with Inno Setup.

.DESCRIPTION
    Compiles installer\windows\emuleqt.iss around the tree bundle-win.ps1 staged
    (stage\eMule), so run that first.  Output:

        build\eMuleQt-v<version>-win64-setup.exe

    Needs Inno Setup 6.3 or later.  -InstallInno fetches it with Chocolatey when
    ISCC.exe is not found (what CI does).

.EXAMPLE
    scripts\bundle-win.ps1
    scripts\installer-win.ps1
    scripts\installer-win.ps1 -InstallInno
    scripts\installer-win.ps1 -Iscc "D:\Tools\Inno Setup 6\ISCC.exe"
#>
[CmdletBinding()]
param(
    [string]$BuildDir,
    [string]$StageDir,
    [string]$Iscc,
    [switch]$InstallInno
)

$ErrorActionPreference = 'Stop'

# Helpers only (Get-ProjectDir, Get-AppVersion); see the dot-sourcing note there
. "$PSScriptRoot\build-win.ps1" -BuildDir $BuildDir -NoBuild

function Resolve-Iscc {
    param([string]$Explicit)

    if ($Explicit) {
        if (Test-Path $Explicit) { return (Resolve-Path $Explicit).Path }
        throw "ISCC.exe not found at $Explicit"
    }

    $onPath = Get-Command ISCC -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    foreach ($programFiles in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if (-not $programFiles) { continue }
        # Newest first, should more than one major version be installed
        $found = Get-ChildItem (Join-Path $programFiles 'Inno Setup*\ISCC.exe') -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    return $null
}

$projectDir = Get-ProjectDir
if (-not $BuildDir) { $BuildDir = Join-Path $projectDir 'build' }
if (-not $StageDir) { $StageDir = Join-Path $projectDir 'stage\eMule' }

$appVersion = Get-AppVersion -ProjectDir $projectDir
if (-not $appVersion) { throw "Could not read VERSION from $projectDir\CMakeLists.txt" }

if (-not (Test-Path (Join-Path $StageDir 'emuleqt.exe'))) {
    throw "No staged bundle in $StageDir. Run scripts\bundle-win.ps1 first."
}

$isccPath = Resolve-Iscc -Explicit $Iscc
if (-not $isccPath -and $InstallInno) {
    Write-Host 'Installing Inno Setup with Chocolatey...'
    & choco install innosetup -y --no-progress
    if ($LASTEXITCODE -ne 0) { throw "choco install innosetup exited with $LASTEXITCODE" }
    $isccPath = Resolve-Iscc
}
if (-not $isccPath) {
    throw "Inno Setup (ISCC.exe) not found. Install it from https://jrsoftware.org/isdl.php,`n" +
          "pass -Iscc <path>, or use -InstallInno."
}
Write-Host "Using Inno Setup: $isccPath"

$script = Join-Path $projectDir 'installer\windows\emuleqt.iss'
& $isccPath "/DAppVersion=$appVersion" "/DStageDir=$StageDir" "/O$BuildDir" $script
if ($LASTEXITCODE -ne 0) { throw "ISCC exited with $LASTEXITCODE" }

$setup = Join-Path $BuildDir "eMuleQt-v$appVersion-win64-setup.exe"
if (-not (Test-Path $setup)) { throw "ISCC reported success but $setup is missing" }

$sizeMb = [math]::Round((Get-Item $setup).Length / 1MB, 1)
Write-Host "Installer: $setup ($sizeMb MB)"
