#Requires -Version 5.1
<#
.SYNOPSIS
    build-win.ps1 -- build eMule Qt on Windows (counterpart of build.sh).

.DESCRIPTION
    Run directly, this configures and builds the project.  Dot-sourced, it only
    defines its helper functions, so bundle-win.ps1 and debug-gui.ps1 can reuse
    the Qt / vcpkg / binary detection instead of duplicating it:

        . "$PSScriptRoot\build-win.ps1"

.EXAMPLE
    scripts\build-win.ps1
    scripts\build-win.ps1 -Clean -Config Debug
    scripts\build-win.ps1 -Bundle
    scripts\build-win.ps1 -NoVcpkg
    scripts\build-win.ps1 -QtDir C:\Qt\6.11.2\msvc2022_64

.NOTES
    Dot-sourcing runs the param block below in the CALLER's scope, so any
    variable a caller shares a name with is reset to the default here.  Callers
    therefore pass their own values through the dot-source:

        . "$PSScriptRoot\build-win.ps1" -Config $Config -NoBuild:$NoBuild

    Keep that in mind when adding a parameter: every dot-sourcing script that
    uses a variable of the same name must pass it through too.
#>
[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$Bundle,
    [switch]$NoBuild,
    [switch]$BootstrapVcpkg,
    [switch]$NoVcpkg,
    [ValidateSet('Release', 'Debug')][string]$Config = 'Release',
    [string]$QtDir,
    [string]$BuildDir
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

function Get-ProjectDir {
    Split-Path -Parent $PSScriptRoot
}

function Get-DefaultBuildDir {
    Join-Path (Get-ProjectDir) 'build'
}

<#
.SYNOPSIS
    Project version from the top-level CMakeLists.txt (e.g. "0.4.0").
#>
function Get-AppVersion {
    param([string]$ProjectDir = (Get-ProjectDir))

    $cmakeLists = Join-Path $ProjectDir 'CMakeLists.txt'
    if (-not (Test-Path $cmakeLists)) { return $null }

    $match = Select-String -Path $cmakeLists -Pattern '^\s+VERSION\s+(\d+\.\d+\.\d+)' |
        Select-Object -First 1
    if ($match) { return $match.Matches[0].Groups[1].Value }
    return $null
}

# ---------------------------------------------------------------------------
# Toolchain discovery
# ---------------------------------------------------------------------------

function Test-QtKit {
    param([string]$Path)
    return [bool]($Path -and (Test-Path (Join-Path $Path 'bin\Qt6Core.dll')))
}

<#
.SYNOPSIS
    Locate a Qt MSVC x64 kit, newest first.  Returns $null when none is found.

.DESCRIPTION
    Globs the version and the kit instead of matching a hardcoded list, so a
    newly released Qt (or a future msvc<year>_64 kit) is found without editing
    this script.  The msvc*_64 filter deliberately skips a sibling mingw_64
    kit -- the project is MSVC-only on Windows.
#>
function Resolve-QtDir {
    param([string]$QtDir)

    foreach ($candidate in @($QtDir, $env:QT_ROOT_DIR, $env:QTDIR, $env:CMAKE_PREFIX_PATH)) {
        if (Test-QtKit $candidate) { return (Resolve-Path $candidate).Path }
    }

    $roots = @('C:\Qt')
    if ($env:USERPROFILE) { $roots += (Join-Path $env:USERPROFILE 'Qt') }

    $found = foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        foreach ($versionDir in Get-ChildItem -Path $root -Directory -Filter '6.*' -ErrorAction SilentlyContinue) {
            $parsed = $null
            if (-not [version]::TryParse($versionDir.Name, [ref]$parsed)) { continue }
            foreach ($kit in Get-ChildItem -Path $versionDir.FullName -Directory -Filter 'msvc*_64' -ErrorAction SilentlyContinue) {
                if (Test-QtKit $kit.FullName) {
                    [pscustomobject]@{ Version = $parsed; Path = $kit.FullName }
                }
            }
        }
    }

    return ($found | Sort-Object Version -Descending | Select-Object -First 1).Path
}

<#
.SYNOPSIS
    Path to vcpkg.cmake, or $null when vcpkg is not installed.
#>
function Resolve-VcpkgToolchain {
    param([string]$BuildDir = (Get-DefaultBuildDir))

    $roots = @($env:VCPKG_ROOT, $env:VCPKG_INSTALLATION_ROOT, 'C:\vcpkg', (Join-Path $BuildDir 'vcpkg'))
    foreach ($root in $roots) {
        if (-not $root) { continue }
        $toolchain = Join-Path $root 'scripts\buildsystems\vcpkg.cmake'
        if (Test-Path $toolchain) { return $toolchain }
    }
    return $null
}

<#
.SYNOPSIS
    vcpkg triplet to build and to look for installed DLLs under.

.DESCRIPTION
    Local builds use x64-windows, which builds every dependency twice (debug and
    release).  CI sets VCPKG_TARGET_TRIPLET=x64-windows-release, which skips the
    debug half -- same dynamic CRT, same library linkage and same DLL names, but
    there is no debug\ subtree under the install root.

    Read from the environment rather than added as a script parameter: dot-sourcing
    re-runs this file's param block in the caller's scope (see .NOTES at the top),
    so a new parameter would have to be threaded through bundle-win.ps1 and
    debug-gui.ps1, and a caller that left it unset would override the default with
    an empty string.  VCPKG_TARGET_TRIPLET is also vcpkg's own variable name and is
    what CI must export for `vcpkg install` and `cmake` anyway.
#>
function Resolve-VcpkgTriplet {
    param([string]$Triplet)

    if ($Triplet) { return $Triplet }
    if ($env:VCPKG_TARGET_TRIPLET) { return $env:VCPKG_TARGET_TRIPLET }
    return 'x64-windows'
}

<#
.SYNOPSIS
    Clone and bootstrap vcpkg into <build>\vcpkg; returns its toolchain file.

.DESCRIPTION
    Runs whenever no vcpkg is already installed, because the dependencies are
    required rather than optional -- be aware that this is a large clone and the
    dependency build that follows at configure time takes a while.  Manifest mode
    installs everything listed in src/vcpkg.json, so no explicit package list is
    needed.  -NoVcpkg opts out for a machine that provides the libraries itself.
#>
function Install-Vcpkg {
    param([string]$BuildDir = (Get-DefaultBuildDir))

    $vcpkgDir = Join-Path $BuildDir 'vcpkg'

    if (-not (Test-Path (Join-Path $vcpkgDir 'vcpkg.exe'))) {
        if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
            throw 'git not found -- install from https://git-scm.com/download/win'
        }
        Write-Host ''
        Write-Host '=== Installing vcpkg ==='
        if (-not (Test-Path $vcpkgDir)) {
            New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
            & git clone https://github.com/microsoft/vcpkg.git $vcpkgDir | Out-Host
            if ($LASTEXITCODE -ne 0) { throw 'vcpkg clone failed.' }
        }
        # Out-Host, not a bare call: this function returns a path, and in
        # PowerShell whatever bootstrap-vcpkg.bat prints would otherwise be
        # concatenated onto that return value.  CMake then reports the whole
        # banner as a missing toolchain file.
        & (Join-Path $vcpkgDir 'bootstrap-vcpkg.bat') -disableMetrics | Out-Host
        if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed.' }
    }

    return (Join-Path $vcpkgDir 'scripts\buildsystems\vcpkg.cmake')
}

<#
.SYNOPSIS
    Find cmake/ninja on PATH, falling back to the copies Qt ships in Qt\Tools.
#>
function Resolve-BuildTool {
    param([Parameter(Mandatory)][ValidateSet('cmake', 'ninja')][string]$Name)

    $onPath = Get-Command $Name -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    $relative = if ($Name -eq 'cmake') { 'Tools\CMake_64\bin\cmake.exe' } else { 'Tools\Ninja\ninja.exe' }
    $roots = @('C:\Qt')
    if ($env:USERPROFILE) { $roots += (Join-Path $env:USERPROFILE 'Qt') }
    foreach ($root in $roots) {
        $candidate = Join-Path $root $relative
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

<#
.SYNOPSIS
    Import the MSVC environment when not already inside a Developer Prompt.

.DESCRIPTION
    The Ninja generator needs cl.exe on PATH.  Returns $true when the compiler
    is available afterwards.
#>
function Import-VsDevEnv {
    param([string]$Arch = 'amd64')

    # cl.exe on PATH is not proof of a usable environment: the Ninja generator
    # also needs INCLUDE and LIB, and only VsDevCmd sets those.  Visual Studio
    # leaves the compiler directory on the machine PATH in some installs, so
    # checking the compiler alone would skip the import and fail at link time.
    if ((Get-Command cl.exe -ErrorAction SilentlyContinue) -and $env:INCLUDE -and $env:LIB) {
        return $true
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $false }

    $vsPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null | Select-Object -First 1
    if (-not $vsPath) { return $false }

    $devCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
    if (-not (Test-Path $devCmd)) { return $false }

    Write-Host "Importing MSVC environment from: $vsPath"
    $quoted = '"' + $devCmd + '"'
    cmd /c "$quoted -arch=$Arch -no_logo && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            Set-Item -Path ('env:' + $matches[1]) -Value $matches[2] -ErrorAction SilentlyContinue
        }
    }

    return [bool]((Get-Command cl.exe -ErrorAction SilentlyContinue) -and $env:INCLUDE -and $env:LIB)
}

<#
.SYNOPSIS
    Newest *installed* Visual Studio generator name, or $null.

.DESCRIPTION
    Two sources, because neither alone is enough: `cmake --help` knows the exact
    generator spelling ("Visual Studio 18 2026") but happily lists versions that
    are not installed, while vswhere knows what is installed but not what cmake
    calls it.  Intersecting the two keeps a hardcoded year out of this script, so
    a future Visual Studio needs no edit here.

    Naming a generator whose toolset is absent is not a soft failure: cmake gets
    as far as probing MSBuild and then stops with MSB8020.

    Do not swap vswhere's installationVersion for catalog.productLineVersion --
    that reports "18" for VS 2026 but "2022" for VS 2022, so it is useless as a
    key.
#>
function Resolve-VsGenerator {
    param([string]$Generator)

    if ($Generator) { return $Generator }

    $cmake = Resolve-BuildTool -Name cmake
    if (-not $cmake) { return $null }

    # Assign the output: an uncaptured native call would append the whole
    # `cmake --help` text to this function's return value.
    $help = & $cmake --help 2>$null
    $listed = foreach ($line in $help) {
        # "* Visual Studio 18 2026        = Generates ..." -- the leading
        # asterisk marks cmake's default generator.
        if ($line -match '^(?<default>\*)?\s*(?<name>Visual Studio (?<major>\d+) \d{4})\s+=') {
            [pscustomobject]@{
                Major   = [int]$matches['major']
                Name    = $matches['name']
                Default = [bool]$matches['default']
            }
        }
    }
    if (-not $listed) { return $null }

    $installed = @()
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        # Deliberately no -latest: a machine with both 2022 and 2026 installed
        # must report both, so the intersection below can pick either.
        $installed = @(& $vswhere -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationVersion 2>$null |
            ForEach-Object { [int]($_ -split '\.')[0] })
    }

    $match = $listed |
        Where-Object { $installed -contains $_.Major } |
        Sort-Object Major -Descending |
        Select-Object -First 1
    if ($match) { return $match.Name }

    # vswhere missing, or it found nothing we recognise -- fall back to whatever
    # cmake defaults to, which is a Visual Studio generator wherever one exists.
    return ($listed | Where-Object { $_.Default } | Select-Object -First 1).Name
}

# ---------------------------------------------------------------------------
# Binary discovery
# ---------------------------------------------------------------------------

<#
.SYNOPSIS
    Locate emuleqt.exe / emulecored.exe across every layout we produce.

.DESCRIPTION
    Returns an object with .Path (or $null) and .Probed, the list of paths
    checked, so callers can print an actionable error.

    Layouts, in order:
      build\src\{gui,daemon}\           CMake single-config (Ninja)
      build\src\{gui,daemon}\<Config>\  CMake multi-config (Visual Studio)
      build\                            Ninja, alternate layout
      bin\<Config>\                     Visual Studio projects
      bin\<other config>\               last resort

    -PreferVsOutput moves bin\ to the front, which is what -NoBuild wants: no
    build ran, so the VS output is the most likely source of fresh binaries.
#>
function Find-EMuleBinary {
    param(
        [Parameter(Mandatory)][ValidateSet('emuleqt.exe', 'emulecored.exe')][string]$Name,
        [ValidateSet('Release', 'Debug')][string]$Config = 'Release',
        [string]$BuildDir = (Get-DefaultBuildDir),
        [string]$ProjectDir = (Get-ProjectDir),
        [switch]$PreferVsOutput
    )

    $subDir = if ($Name -eq 'emuleqt.exe') { 'gui' } else { 'daemon' }
    $altConfig = if ($Config -eq 'Debug') { 'Release' } else { 'Debug' }

    $cmakePaths = @(
        (Join-Path $BuildDir "src\$subDir\$Name")
        (Join-Path $BuildDir "src\$subDir\$Config\$Name")
        (Join-Path $BuildDir $Name)
    )
    $vsPaths = @(
        (Join-Path $ProjectDir "bin\$Config\$Name")
        (Join-Path $ProjectDir "bin\$altConfig\$Name")
    )

    $probed = if ($PreferVsOutput) { $vsPaths + $cmakePaths } else { $cmakePaths + $vsPaths }

    foreach ($candidate in $probed) {
        if (Test-Path $candidate) {
            return [pscustomobject]@{ Path = (Resolve-Path $candidate).Path; Probed = $probed }
        }
    }
    return [pscustomobject]@{ Path = $null; Probed = $probed }
}

<#
.SYNOPSIS
    Directory holding the MSVC redistributable DLLs, or $null.

.DESCRIPTION
    The Microsoft.VC*.CRT folder name is globbed rather than pinned to VC143,
    so a newer toolset (VS 18 and later) is picked up too.
#>
function Resolve-VcRedistDir {
    # 1. Set by the Visual Studio Developer Command Prompt
    if ($env:VCToolsRedistDir) {
        $hit = Get-ChildItem (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }

    # 2. VSINSTALLDIR, then 3. any installed Visual Studio
    $roots = @()
    if ($env:VSINSTALLDIR) { $roots += $env:VSINSTALLDIR }
    foreach ($programFiles in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if (-not $programFiles) { continue }
        $vsRoot = Join-Path $programFiles 'Microsoft Visual Studio'
        if (-not (Test-Path $vsRoot)) { continue }
        $roots += Get-ChildItem $vsRoot -Directory -ErrorAction SilentlyContinue |
            ForEach-Object { Get-ChildItem $_.FullName -Directory -ErrorAction SilentlyContinue } |
            ForEach-Object { $_.FullName }
    }

    foreach ($root in $roots) {
        $redist = Join-Path $root 'VC\Redist\MSVC'
        if (-not (Test-Path $redist)) { continue }
        $hit = Get-ChildItem $redist -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object { Get-ChildItem (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue } |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

<#
.SYNOPSIS
    Path to 7z.exe, or $null.

.DESCRIPTION
    7-Zip's installer does not put itself on PATH, so check the default install
    locations as well.  CI runners do have it on PATH.
#>
function Resolve-SevenZip {
    $onPath = Get-Command 7z -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    foreach ($programFiles in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if (-not $programFiles) { continue }
        $candidate = Join-Path $programFiles '7-Zip\7z.exe'
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

<#
.SYNOPSIS
    Directory holding vcpkg's runtime DLLs for $Config, or $null.

.DESCRIPTION
    The triplet used to be hardcoded here.  That made a triplet change a silent
    failure: this returned $null, bundle-win.ps1 printed a note and carried on, and
    the zip shipped without zlib1.dll while the job stayed green.  Pass -RequireVcpkg
    to bundle-win.ps1 to turn that into an error.
#>
function Resolve-VcpkgBinDir {
    param(
        [ValidateSet('Release', 'Debug')][string]$Config = 'Release',
        [string]$BuildDir = (Get-DefaultBuildDir),
        [string]$ProjectDir = (Get-ProjectDir),
        [string]$Triplet
    )

    $Triplet = Resolve-VcpkgTriplet -Triplet $Triplet

    # A release-only triplet has no debug\ subtree, so fall back to bin\ -- a Debug
    # build against one is refused up front in Invoke-EMuleBuild anyway.
    $suffixes = if ($Config -eq 'Debug') { @('debug\bin', 'bin') } else { @('bin') }

    $roots = @(
        (Join-Path $ProjectDir 'src\vcpkg_installed')   # MSBuild / `vcpkg install` run in src\
        (Join-Path $ProjectDir 'vcpkg_installed')
        (Join-Path $BuildDir 'vcpkg_installed')         # CMake manifest mode
    )
    if ($env:VCPKG_INSTALLATION_ROOT) {
        $roots += (Join-Path $env:VCPKG_INSTALLATION_ROOT 'installed')   # classic mode
    }

    foreach ($root in $roots) {
        foreach ($suffix in $suffixes) {
            $candidate = Join-Path $root "$Triplet\$suffix"
            if (Test-Path $candidate) { return $candidate }
        }
    }

    # Last resort: an install root exists, but under a triplet we did not expect.
    # Accept only an unambiguous single match -- picking one of several would
    # silently ship the DLLs of some other build, which is the failure this
    # function exists to prevent.
    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        $globbed = @(
            Get-ChildItem $root -Directory -ErrorAction SilentlyContinue |
                ForEach-Object { Join-Path $_.FullName 'bin' } |
                Where-Object { Test-Path $_ }
        )
        if ($globbed.Count -eq 1) {
            Write-Warning "vcpkg triplet '$Triplet' not installed; using $($globbed[0])"
            return $globbed[0]
        }
        if ($globbed.Count -gt 1) {
            throw ("Ambiguous vcpkg install roots under ${root}:`n  " +
                   ($globbed -join "`n  ") +
                   "`nSet VCPKG_TARGET_TRIPLET or pass -Triplet.")
        }
    }
    return $null
}

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

<#
.SYNOPSIS
    Configure (only when needed) and build.

.DESCRIPTION
    The configure step is skipped when a usable build tree already exists, which
    is what keeps the debug-gui.ps1 edit-run loop fast.  Use -Clean to force it.

    Returns nothing on purpose: cmake writes to stdout, and in PowerShell any
    uncaptured output from a function becomes part of its return value, so a
    caller doing $x = Invoke-EMuleBuild would collect the whole build log.
    Callers pass -BuildDir in and keep using their own copy.
#>
function Invoke-EMuleBuild {
    param(
        [ValidateSet('Release', 'Debug')][string]$Config = 'Release',
        [string]$QtDir,
        [string]$BuildDir,
        [switch]$NoBuild,
        [switch]$Clean,
        [switch]$BootstrapVcpkg,
        [switch]$NoVcpkg
    )

    $projectDir = Get-ProjectDir
    if (-not $BuildDir) { $BuildDir = Join-Path $projectDir 'build' }

    if ($NoBuild) {
        Write-Host ''
        Write-Host '=== Skipping build (-NoBuild) ==='
        return
    }

    if ($Clean -and (Test-Path $BuildDir)) {
        Write-Host "Cleaning build directory: $BuildDir"
        Remove-Item -Recurse -Force $BuildDir
    }

    $qt = Resolve-QtDir -QtDir $QtDir
    if (-not $qt) {
        throw "Qt (MSVC x64 kit) not found.`n" +
              "  Looked at: -QtDir, QT_ROOT_DIR, QTDIR, CMAKE_PREFIX_PATH,`n" +
              "             C:\Qt\6.*\msvc*_64, %USERPROFILE%\Qt\6.*\msvc*_64`n" +
              "  Install Qt 6 for MSVC 2022 64-bit, or pass the kit explicitly:`n" +
              "    scripts\build-win.ps1 -QtDir C:\Qt\6.11.2\msvc2022_64"
    }
    Write-Host "Using Qt: $qt"

    $cmake = Resolve-BuildTool -Name cmake
    if (-not $cmake) { throw 'CMake not found. Install from https://cmake.org/download/' }
    $ninja = Resolve-BuildTool -Name ninja
    if (-not $ninja) {
        throw 'Ninja not found. Install it, or use the Qt-bundled copy in Qt\Tools\Ninja.'
    }
    if (-not (Import-VsDevEnv)) {
        Write-Warning 'MSVC compiler (cl.exe) not on PATH and VsDevCmd could not be imported.'
        Write-Warning 'Run this from an "x64 Native Tools Command Prompt for VS" if configure fails.'
    }

    # -- Configure (only when there is no usable build tree) -----------------

    # A CMakeCache.txt alone is not proof of a successful configure: a configure
    # that failed part-way leaves the cache behind but no build.ninja, and
    # skipping configure there fails the build with a confusing ninja error.
    $cache = Join-Path $BuildDir 'CMakeCache.txt'
    $configured = (Test-Path $cache) -and (Test-Path (Join-Path $BuildDir 'build.ninja'))

    # Drop the leftovers of a failed configure.  They are not merely useless:
    # the cache pins CMAKE_CXX_COMPILER, so a tree that once picked the wrong
    # compiler keeps picking it no matter what this script passes below.
    if (-not $configured -and (Test-Path $cache)) {
        Write-Host "Discarding incomplete CMake cache in: $BuildDir"
        Remove-Item -Force $cache
        Remove-Item -Recurse -Force (Join-Path $BuildDir 'CMakeFiles') -ErrorAction SilentlyContinue
    }

    if (-not $configured) {
        # Not optional, despite the -BootstrapVcpkg switch: CMakeLists.txt calls
        # find_package(ZLIB REQUIRED) and find_package(OpenSSL REQUIRED), so a
        # configure without the src\vcpkg.json dependencies always fails.  Only
        # -NoVcpkg (for a machine that supplies them another way) skips this.
        $toolchain = $null
        if (-not $NoVcpkg) {
            $toolchain = Resolve-VcpkgToolchain -BuildDir $BuildDir
            if (-not $toolchain) { $toolchain = Install-Vcpkg -BuildDir $BuildDir }
        }

        # Refuse Debug against a release-only triplet up front: the dependencies
        # would be release-CRT while the app is debug-CRT, which corrupts the heap
        # on the first allocation that crosses the boundary.
        $triplet = Resolve-VcpkgTriplet
        if ($Config -eq 'Debug' -and $triplet -like '*-release') {
            throw ("Triplet '$triplet' builds release-only dependencies; a Debug " +
                   'build would mix debug and release CRTs. Unset ' +
                   'VCPKG_TARGET_TRIPLET or use -Config Release.')
        }

        Write-Host ''
        Write-Host '=== Configuring ==='
        $cmakeArgs = @(
            '-S', $projectDir
            '-B', $BuildDir
            '-G', 'Ninja'
            "-DCMAKE_BUILD_TYPE=$Config"
            "-DCMAKE_PREFIX_PATH=$qt"
            # Pin the compiler.  Qt installs MinGW under Qt\Tools and puts it on
            # PATH, and CMake's Ninja generator probes c++/g++ *before* cl, so an
            # unpinned configure silently builds with GCC against an MSVC Qt kit.
            '-DCMAKE_C_COMPILER=cl'
            '-DCMAKE_CXX_COMPILER=cl'
            # Name the ninja we resolved rather than letting CMake search PATH:
            # the Qt\Tools\Ninja fallback above is not on PATH, and CMake then
            # stops with "unable to find a build program corresponding to Ninja".
            "-DCMAKE_MAKE_PROGRAM=$ninja"
        )
        if ($toolchain) {
            Write-Host "Using vcpkg: $toolchain"
            # VCPKG_MANIFEST_MODE must be set explicitly: vcpkg only enables it
            # on its own when vcpkg.json sits at the CMake source root, and this
            # project keeps the manifest in src/.
            $cmakeArgs += @(
                "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
                '-DVCPKG_MANIFEST_MODE=ON'
                "-DVCPKG_MANIFEST_DIR=$(Join-Path $projectDir 'src')"
                "-DVCPKG_TARGET_TRIPLET=$triplet"
            )
        }
        else {
            Write-Warning 'Configuring without vcpkg (-NoVcpkg).'
            Write-Warning 'zlib, openssl, yaml-cpp, libarchive and miniupnpc must be findable by CMake.'
        }

        & $cmake @cmakeArgs
        if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    }
    else {
        Write-Host "Using existing CMake cache in: $BuildDir"
    }

    # -- Build ---------------------------------------------------------------

    Write-Host ''
    Write-Host '=== Building ==='
    & $cmake --build $BuildDir --config $Config --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

    Write-Host 'Build complete.'
}

# ---------------------------------------------------------------------------
# Entry point -- skipped when this file is dot-sourced for its helpers
# ---------------------------------------------------------------------------

if ($MyInvocation.InvocationName -ne '.') {
    if (-not $BuildDir) { $BuildDir = Get-DefaultBuildDir }
    Invoke-EMuleBuild -Config $Config -QtDir $QtDir -BuildDir $BuildDir `
        -NoBuild:$NoBuild -Clean:$Clean -BootstrapVcpkg:$BootstrapVcpkg -NoVcpkg:$NoVcpkg

    if ($Bundle) {
        Write-Host ''
        Write-Host '=== Running Windows bundle script ==='
        & "$PSScriptRoot\bundle-win.ps1" -QtDir $QtDir -Config $Config -BuildDir $BuildDir -NoBuild
    }
}
