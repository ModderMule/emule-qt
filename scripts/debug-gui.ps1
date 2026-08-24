#Requires -Version 5.1
<#
.SYNOPSIS
    Build, then start daemon and GUI separately for screenshot/debug testing.

.DESCRIPTION
    Windows counterpart of debug-gui.sh.  PowerShell rather than a .bat because
    this needs the daemon's PID, a guaranteed cleanup block, and a foreground
    wait on the GUI -- emuleqt is built WIN32_EXECUTABLE, and cmd's `start` does
    not wait for a GUI-subsystem process, so the daemon would be killed before a
    screenshot ever landed.

.EXAMPLE
    scripts\debug-gui.ps1 --screenshot C:\temp\kad.png --tab kad --subtab 1 --delay 8000

.EXAMPLE
    scripts\debug-gui.ps1 -Debugger

.EXAMPLE
    scripts\debug-gui.ps1 -NoBuild -Config Debug

.NOTES
    PositionalBinding is off so that GUI arguments are never mistaken for this
    script's parameters: without it PowerShell binds a leading "--screenshot"
    positionally to -Config and fails.  Pass GUI options in their double-dash
    form (--config, --tab); a single-dash -config would bind to this script.
#>
[CmdletBinding(PositionalBinding = $false)]
param(
    [switch]$Debugger,
    [switch]$NoBuild,
    [ValidateSet('Release', 'Debug')][string]$Config = 'Release',
    [string]$QtDir,
    [string]$BuildDir,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$GuiArgs
)

$ErrorActionPreference = 'Stop'

# Dot-sourcing re-runs build-win.ps1's param block in this scope, which would
# reset the parameters we share names with -- so pass ours straight through.
. "$PSScriptRoot\build-win.ps1" -Config $Config -QtDir $QtDir -BuildDir $BuildDir -NoBuild:$NoBuild

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

<#
.SYNOPSIS
    Scalar value from a nested "block: / key:" pair in a YAML file.

.DESCRIPTION
    A small state machine rather than a YAML library, so the script keeps its
    zero dependencies.  Only handles the one nesting level the preferences use:
    a top-level block header followed by indented keys.  Returns $Default when
    the file, the block or the key is missing.
#>
function Get-YamlScalar {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Block,
        [Parameter(Mandatory)][string]$Key,
        $Default = $null
    )

    if (-not (Test-Path $Path)) { return $Default }

    $inBlock = $false
    foreach ($line in Get-Content $Path) {
        if ($line -match '^\s*#') { continue }
        if ($line -match "^$([regex]::Escape($Block)):\s*$") { $inBlock = $true; continue }
        if ($line -match '^\S') { $inBlock = $false; continue }
        if ($inBlock -and $line -match "^\s+$([regex]::Escape($Key)):\s*(\S+)") { return $matches[1] }
    }
    return $Default
}

<#
.SYNOPSIS
    The config directory a given binary will use, mirroring AppConfig::configDir().

.DESCRIPTION
    Windows has no single fixed location: AppConfig.cpp peeks
    <exe-dir>\config\preferences.yml for transfer.multiUserSharing and picks
    0 = per-user %APPDATA%, 1 = all-users %ProgramData%, 2 = program-dir
    (portable, and the default when the file or the key is absent).

    In per-user mode the path ends in the *application* name, which differs
    between the two binaries -- "eMule Qt Core" for the daemon, "eMule Qt" for
    the GUI -- hence the -AppName parameter.

    -Override short-circuits everything: it is the --config value, which both
    binaries honour ahead of any of this.
#>
function Get-EMuleConfigDir {
    param(
        [Parameter(Mandatory)][string]$ExePath,
        [Parameter(Mandatory)][string]$AppName,
        [string]$Override
    )

    if ($Override) { return $Override }

    $exeDir = Split-Path -Parent $ExePath
    $portableDir = Join-Path $exeDir 'config'
    $mode = [int](Get-YamlScalar -Path (Join-Path $portableDir 'preferences.yml') `
                                 -Block 'transfer' -Key 'multiUserSharing' -Default 2)

    switch ($mode) {
        0 { return (Join-Path $env:APPDATA "eMule\$AppName") }
        1 { return (Join-Path $env:ProgramData 'eMule\eMule Qt') }
        default { return $portableDir }
    }
}

<#
.SYNOPSIS
    Copy data\config into a config directory that does not exist yet.

.DESCRIPTION
    AppConfig::seedBundledData() bails out in program-dir mode -- there the
    config\ next to the exe IS the config directory, so nothing is ever seeded
    into a fresh build tree and the GUI starts with no server.met and no Kad
    bootstrap nodes.  Seeding here fills that gap for debug runs.

    Deliberately all-or-nothing on the directory: an existing config dir is left
    completely alone, so preferences.yml and uistate.yml are never overwritten.
#>
function Initialize-EMuleConfigDir {
    param([Parameter(Mandatory)][string]$ConfigDir)

    if (Test-Path $ConfigDir) { return }

    $bundleDir = Join-Path (Get-ProjectDir) 'data\config'
    if (-not (Test-Path $bundleDir)) {
        Write-Warning "No bundled config at $bundleDir -- starting with an empty config dir."
        return
    }

    New-Item -ItemType Directory -Path $ConfigDir -Force | Out-Null
    Copy-Item -Path (Join-Path $bundleDir '*') -Destination $ConfigDir -Recurse -Force
    Write-Host "Seeded config from data\config -> $ConfigDir"
}

<#
.SYNOPSIS
    IPC port from preferences.yml, falling back to the built-in default.

.DESCRIPTION
    Mirrors Preferences.cpp: the port lives under the "ipc:" block and defaults
    to 4712.
#>
function Get-IpcPort {
    param([Parameter(Mandatory)][string]$ConfigDir)

    return [int](Get-YamlScalar -Path (Join-Path $ConfigDir 'preferences.yml') `
                                -Block 'ipc' -Key 'port' -Default 4712)
}

<#
.SYNOPSIS
    Block until the daemon's IPC server is listening, or the timeout expires.

.DESCRIPTION
    Replaces the flat "sleep 2" of debug-gui.sh: polling means a fast machine
    starts the GUI sooner and a slow one still waits long enough.  Falls back to
    a fixed sleep where Get-NetTCPConnection is unavailable.
#>
function Wait-DaemonReady {
    param(
        [Parameter(Mandatory)][System.Diagnostics.Process]$Process,
        [int]$Port,
        [int]$TimeoutSeconds = 10
    )

    if (-not (Get-Command Get-NetTCPConnection -ErrorAction SilentlyContinue)) {
        Start-Sleep -Seconds 2
        return
    }

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($Process.HasExited) {
            throw "Daemon exited during startup (exit code $($Process.ExitCode))."
        }
        $listening = Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue
        if ($listening) {
            Write-Host "Daemon listening on port $Port."
            return
        }
        Start-Sleep -Milliseconds 200
    }
    Write-Warning "Daemon not listening on port $Port after ${TimeoutSeconds}s -- starting GUI anyway."
}

<#
.SYNOPSIS
    Locate cdb.exe from the Windows SDK Debugging Tools, or $null.
#>
function Resolve-Cdb {
    $onPath = Get-Command cdb.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    foreach ($programFiles in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if (-not $programFiles) { continue }
        foreach ($arch in @('x64', 'arm64')) {
            $candidate = Join-Path $programFiles "Windows Kits\10\Debuggers\$arch\cdb.exe"
            if (Test-Path $candidate) { return $candidate }
        }
    }
    return $null
}

function Resolve-Devenv {
    $onPath = Get-Command devenv.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }

    $vsPath = & $vswhere -latest -products * -property installationPath 2>$null | Select-Object -First 1
    if (-not $vsPath) { return $null }

    $candidate = Join-Path $vsPath 'Common7\IDE\devenv.exe'
    if (Test-Path $candidate) { return $candidate }
    return $null
}

<#
.SYNOPSIS
    Resolve a required binary or exit with the full list of probed paths.
#>
function Get-RequiredBinary {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$BuildDir,
        [string]$Config = 'Release',
        [switch]$PreferVsOutput
    )

    $result = Find-EMuleBinary -Name $Name -Config $Config -BuildDir $BuildDir -PreferVsOutput:$PreferVsOutput
    if (-not $result.Path) {
        Write-Host "Error: $Name not found." -ForegroundColor Red
        foreach ($probe in $result.Probed) { Write-Host "  Checked: $probe" }
        exit 1
    }
    return $result.Path
}

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

if (-not $BuildDir) { $BuildDir = Get-DefaultBuildDir }

Invoke-EMuleBuild -Config $Config -QtDir $QtDir -BuildDir $BuildDir -NoBuild:$NoBuild

# ---------------------------------------------------------------------------
# Locate binaries
# ---------------------------------------------------------------------------

$guiExe = Get-RequiredBinary -Name 'emuleqt.exe' -BuildDir $BuildDir -Config $Config -PreferVsOutput:$NoBuild
$daemonExe = Get-RequiredBinary -Name 'emulecored.exe' -BuildDir $BuildDir -Config $Config -PreferVsOutput:$NoBuild

Write-Host ''
Write-Host "GUI binary:    $guiExe"
Write-Host "Daemon binary: $daemonExe"

# ---------------------------------------------------------------------------
# Config directories
# ---------------------------------------------------------------------------

# A --config among the GUI arguments has to reach the daemon too, or the two
# processes would read different preferences.yml files -- including different
# ipc ports, which is exactly what Wait-DaemonReady polls for.
# Both spellings QCommandLineParser accepts: "--config dir" and "--config=dir".
$configOverride = ''
for ($i = 0; $i -lt $GuiArgs.Count; $i++) {
    if ($GuiArgs[$i] -eq '--config' -and $i -lt $GuiArgs.Count - 1) {
        $configOverride = $GuiArgs[$i + 1]
        break
    }
    if ($GuiArgs[$i] -match '^--config=(.+)$') {
        $configOverride = $matches[1]
        break
    }
}

$daemonConfigDir = Get-EMuleConfigDir -ExePath $daemonExe -AppName 'eMule Qt Core' -Override $configOverride
$guiConfigDir = Get-EMuleConfigDir -ExePath $guiExe -AppName 'eMule Qt' -Override $configOverride

Write-Host "Config dir:    $daemonConfigDir"
Initialize-EMuleConfigDir -ConfigDir $daemonConfigDir
if ($guiConfigDir -ne $daemonConfigDir) {
    Write-Host "GUI config dir: $guiConfigDir"
    Initialize-EMuleConfigDir -ConfigDir $guiConfigDir
}

# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------

# Windows has no rpath, and a Ninja build tree keeps no Qt DLLs next to the
# executables, so both would fail to start with 0xC0000135 (DLL not found).
# Put Qt's bin -- and vcpkg's, when CMake has not already copied those DLLs --
# on PATH; Start-Process passes this environment on to both children.
$qtBinDir = Resolve-QtDir -QtDir $QtDir
if ($qtBinDir) {
    $env:PATH = (Join-Path $qtBinDir 'bin') + ';' + $env:PATH
}
else {
    Write-Warning 'Qt not found -- the daemon and GUI may fail to start (missing Qt DLLs).'
}

$vcpkgBinDir = Resolve-VcpkgBinDir -Config $Config -BuildDir $BuildDir
if ($vcpkgBinDir) { $env:PATH = $vcpkgBinDir + ';' + $env:PATH }

# Kill any leftover daemon from a previous run (pkill -f emulecored equivalent)
$leftovers = Get-Process emulecored -ErrorAction SilentlyContinue
if ($leftovers) {
    Write-Host "Killing $($leftovers.Count) leftover daemon process(es)..."
    $leftovers | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
}

# The daemon is a console app (no WIN32_EXECUTABLE), so -NoNewWindow keeps its
# log output in this console, interleaved with the GUI's -- see Log.h.
Write-Host ''
Write-Host 'Starting daemon...'
$daemonArgs = if ($configOverride) { @('--config', $configOverride) } else { @() }
$daemon = if ($daemonArgs) {
    Start-Process -FilePath $daemonExe -ArgumentList $daemonArgs -PassThru -NoNewWindow
}
else {
    Start-Process -FilePath $daemonExe -PassThru -NoNewWindow
}

try {
    Wait-DaemonReady -Process $daemon -Port (Get-IpcPort -ConfigDir $daemonConfigDir)

    # The GUI discovers this already-running daemon via resolveDaemonPath() and
    # only spawns its own on connection failure, so it leaves ours alone on quit.
    Write-Host ''
    if ($Debugger) {
        $cdb = Resolve-Cdb
        if ($cdb) {
            # -g/-G skip the initial and final breakpoints; -c runs a backtrace
            # and quits when the app faults.  cdb takes the target directly, so
            # there is no "--" separator as with lldb.
            Write-Host "Running GUI under cdb: $cdb"
            & $cdb -g -G -c 'kb;q' $guiExe @GuiArgs 2>&1 | Out-Host
        }
        else {
            $devenv = Resolve-Devenv
            if (-not $devenv) {
                throw "No debugger found.`n" +
                      "  Install the Windows SDK 'Debugging Tools for Windows' for cdb.exe,`n" +
                      "  or install Visual Studio for devenv.exe."
            }
            Write-Warning 'cdb.exe not found -- falling back to the Visual Studio debugger (interactive).'
            & $devenv /DebugExe $guiExe @GuiArgs 2>&1 | Out-Host
        }
    }
    else {
        # emuleqt is WIN32_EXECUTABLE, and PowerShell does NOT wait for a
        # GUI-subsystem process on its own -- it returns as soon as the process
        # starts, which would kill the daemon mid-screenshot.  Piping the output
        # makes it wait for the stream to close, i.e. for the process to exit,
        # and surfaces the GUI's log lines next to the daemon's at the same time.
        & $guiExe @GuiArgs 2>&1 | Out-Host
    }
}
finally {
    if ($daemon -and -not $daemon.HasExited) {
        Write-Host ''
        Write-Host 'Stopping daemon...'
        Stop-Process -Id $daemon.Id -Force -ErrorAction SilentlyContinue
    }
}
