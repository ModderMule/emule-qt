@echo off
setlocal enabledelayedexpansion
REM ---------------------------------------------------------------------------
REM bundle-win.bat -- build and package eMule Qt for Windows
REM
REM Builds the daemon and GUI, bundles Qt runtime DLLs via windeployqt,
REM copies the default config data, and creates a self-contained zip.
REM
REM Usage:
REM   scripts\bundle-win.bat [qt-dir] [config] [--no-build]
REM
REM   qt-dir      Path to the Qt MSVC kit (default: auto-detect)
REM   config      Build configuration: Release or Debug (default: Release)
REM   --no-build  Skip CMake configure and build (use existing binaries from VS)
REM
REM Examples:
REM   scripts\bundle-win.bat C:\Qt\6.10.2\msvc2022_64 Release
REM   scripts\bundle-win.bat C:\Qt\6.10.2\msvc2022_64 --no-build
REM   scripts\bundle-win.bat --no-build
REM
REM VS output directory:
REM   bin\release\           all .exe and .lib from all four VS projects
REM   bin\debug\             (same for Debug config)
REM
REM Layout inside the zip:
REM   eMule\
REM     emuleqt.exe          GUI executable
REM     emulecored.exe       daemon executable
REM     config\              default config data
REM       nodes.dat
REM       eMule.tmpl
REM       server.met
REM       webserver\...
REM     lang\                compiled translation files (.qm)
REM     [Qt DLLs, platforms\, styles\, tls\, etc.]
REM ---------------------------------------------------------------------------

set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%\.."
set "PROJECT_DIR=%CD%"
popd
for /f "usebackq delims=" %%V in (`powershell -NoProfile -Command "(Select-String -Path '%PROJECT_DIR%\CMakeLists.txt' -Pattern '^\s+VERSION\s+(\d+\.\d+\.\d+)').Matches[0].Groups[1].Value"`) do set "APP_VERSION=%%V"

REM -- Parse arguments --------------------------------------------------------

set "BUILD_DIR=%PROJECT_DIR%\build"

set "DO_BUILD=1"
for %%A in (%*) do (
    if /i "%%~A"=="--no-build" set "DO_BUILD=0"
)

set "CONFIG=%~2"
if "%CONFIG%"=="" set "CONFIG=Release"
if "%CONFIG%"=="--no-build" set "CONFIG=Release"

set "QT_DIR=%~1"
if "%QT_DIR%"=="--no-build" set "QT_DIR="
if "%QT_DIR%"=="" (
    REM Try common Qt install locations
    for %%V in ("6.10.2" "6.10.1" "6.10.0" "6.9.0" "6.8.0" "6.7.0") do (
        if exist "C:\Qt\%%~V\msvc2022_64" (
            set "QT_DIR=C:\Qt\%%~V\msvc2022_64"
            goto :qt_found
        )
        if exist "%USERPROFILE%\Qt\%%~V\msvc2022_64" (
            set "QT_DIR=%USERPROFILE%\Qt\%%~V\msvc2022_64"
            goto :qt_found
        )
    )
    echo Error: Qt directory not found. Pass it as second argument.
    echo Usage: scripts\bundle-win.bat [qt-dir]
    exit /b 1
)
:qt_found
echo Using Qt: %QT_DIR%

set "WINDEPLOYQT=%QT_DIR%\bin\windeployqt.exe"
if not exist "%WINDEPLOYQT%" (
    echo Error: windeployqt not found at %WINDEPLOYQT%
    exit /b 1
)

REM -- Configure & Build ------------------------------------------------------

if "%DO_BUILD%"=="0" (
    echo(
    echo === Skipping build ^(--no-build^) ===
    goto :skip_build
)

echo(
echo === Configuring ===
set "VCPKG_CMAKE="
if defined VCPKG_ROOT set "VCPKG_CMAKE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if not defined VCPKG_CMAKE if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG_CMAKE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake"

if defined VCPKG_CMAKE (
    cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_PREFIX_PATH="%QT_DIR%" -DCMAKE_TOOLCHAIN_FILE="%VCPKG_CMAKE%" -DVCPKG_MANIFEST_DIR="%PROJECT_DIR%\src"
) else (
    cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_PREFIX_PATH="%QT_DIR%"
)
if errorlevel 1 (
    echo Error: CMake configure failed.
    exit /b 1
)

echo(
echo === Building ===
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
if errorlevel 1 (
    echo Error: Build failed.
    exit /b 1
)

:skip_build

REM -- Locate binaries --------------------------------------------------------

REM VS projects output to bin\{debug,release}\ in the project root.
REM CMake multi-config goes to build\src\{gui,daemon}\%CONFIG%\.
REM CMake single-config (Ninja) goes to build\src\{gui,daemon}\.
REM
REM When DO_BUILD=1 (CMake build was performed), search CMake output first
REM to avoid picking up stale/wrong-config binaries from bin\.
REM When DO_BUILD=0 (--no-build), search VS bin\ output first.

set "GUI_BIN="
set "DAEMON_BIN="

if "%DO_BUILD%"=="1" (
    REM CMake build performed -- prefer CMake output directories
    REM 1. Ninja single-config (build root)
    if exist "%BUILD_DIR%\emuleqt.exe" set "GUI_BIN=%BUILD_DIR%\emuleqt.exe"
    if exist "%BUILD_DIR%\emulecored.exe" set "DAEMON_BIN=%BUILD_DIR%\emulecored.exe"
    REM 2. Alternate Ninja layout (build\src\{gui,daemon}\)
    if not defined GUI_BIN if exist "%BUILD_DIR%\src\gui\emuleqt.exe" set "GUI_BIN=%BUILD_DIR%\src\gui\emuleqt.exe"
    if not defined DAEMON_BIN if exist "%BUILD_DIR%\src\daemon\emulecored.exe" set "DAEMON_BIN=%BUILD_DIR%\src\daemon\emulecored.exe"
    REM 3. CMake multi-config
    if not defined GUI_BIN if exist "%BUILD_DIR%\src\gui\%CONFIG%\emuleqt.exe" set "GUI_BIN=%BUILD_DIR%\src\gui\%CONFIG%\emuleqt.exe"
    if not defined DAEMON_BIN if exist "%BUILD_DIR%\src\daemon\%CONFIG%\emulecored.exe" set "DAEMON_BIN=%BUILD_DIR%\src\daemon\%CONFIG%\emulecored.exe"
    REM 4. Fallback to VS bin\ output
    if not defined GUI_BIN if exist "%PROJECT_DIR%\bin\%CONFIG%\emuleqt.exe" set "GUI_BIN=%PROJECT_DIR%\bin\%CONFIG%\emuleqt.exe"
    if not defined DAEMON_BIN if exist "%PROJECT_DIR%\bin\%CONFIG%\emulecored.exe" set "DAEMON_BIN=%PROJECT_DIR%\bin\%CONFIG%\emulecored.exe"
) else (
    REM --no-build: prefer VS bin\ output (most common scenario)
    REM 1. VS output matching requested config
    if exist "%PROJECT_DIR%\bin\%CONFIG%\emuleqt.exe" set "GUI_BIN=%PROJECT_DIR%\bin\%CONFIG%\emuleqt.exe"
    if exist "%PROJECT_DIR%\bin\%CONFIG%\emulecored.exe" set "DAEMON_BIN=%PROJECT_DIR%\bin\%CONFIG%\emulecored.exe"
    REM 2. VS output alternate config
    set "ALT_CONFIG=Debug"
    if /i "%CONFIG%"=="Debug" set "ALT_CONFIG=Release"
)
REM (delayed expansion needed for ALT_CONFIG set inside the if block above)
if "%DO_BUILD%"=="0" (
    if not defined GUI_BIN if exist "%PROJECT_DIR%\bin\!ALT_CONFIG!\emuleqt.exe" set "GUI_BIN=%PROJECT_DIR%\bin\!ALT_CONFIG!\emuleqt.exe"
    if not defined DAEMON_BIN if exist "%PROJECT_DIR%\bin\!ALT_CONFIG!\emulecored.exe" set "DAEMON_BIN=%PROJECT_DIR%\bin\!ALT_CONFIG!\emulecored.exe"
    REM 3. Fallback to CMake multi-config
    if not defined GUI_BIN if exist "%BUILD_DIR%\src\gui\%CONFIG%\emuleqt.exe" set "GUI_BIN=%BUILD_DIR%\src\gui\%CONFIG%\emuleqt.exe"
    if not defined DAEMON_BIN if exist "%BUILD_DIR%\src\daemon\%CONFIG%\emulecored.exe" set "DAEMON_BIN=%BUILD_DIR%\src\daemon\%CONFIG%\emulecored.exe"
    REM 4. Fallback to CMake single-config (Ninja) -- alternate layout
    if not defined GUI_BIN if exist "%BUILD_DIR%\src\gui\emuleqt.exe" set "GUI_BIN=%BUILD_DIR%\src\gui\emuleqt.exe"
    if not defined DAEMON_BIN if exist "%BUILD_DIR%\src\daemon\emulecored.exe" set "DAEMON_BIN=%BUILD_DIR%\src\daemon\emulecored.exe"
    REM 5. Fallback to CMake single-config (Ninja) -- build root
    if not defined GUI_BIN if exist "%BUILD_DIR%\emuleqt.exe" set "GUI_BIN=%BUILD_DIR%\emuleqt.exe"
    if not defined DAEMON_BIN if exist "%BUILD_DIR%\emulecored.exe" set "DAEMON_BIN=%BUILD_DIR%\emulecored.exe"
)

if not defined GUI_BIN (
    echo Error: GUI binary not found.
    echo   Checked: %BUILD_DIR%\emuleqt.exe
    echo   Checked: %BUILD_DIR%\src\gui\{%CONFIG%,}\emuleqt.exe
    echo   Checked: %PROJECT_DIR%\bin\{Release,Debug}\emuleqt.exe
    exit /b 1
)
if not defined DAEMON_BIN (
    echo Error: Daemon binary not found.
    echo   Checked: %BUILD_DIR%\emulecored.exe
    echo   Checked: %BUILD_DIR%\src\daemon\{%CONFIG%,}\emulecored.exe
    echo   Checked: %PROJECT_DIR%\bin\{Release,Debug}\emulecored.exe
    exit /b 1
)

echo(
echo GUI binary:    %GUI_BIN%
echo Daemon binary: %DAEMON_BIN%

REM -- Assemble staging directory ---------------------------------------------

set "STAGE_DIR=%PROJECT_DIR%\stage\eMule"
if exist "%PROJECT_DIR%\stage" rmdir /s /q "%PROJECT_DIR%\stage"
mkdir "%STAGE_DIR%"

echo(
echo === Staging binaries ===
copy /y "%GUI_BIN%" "%STAGE_DIR%\emuleqt.exe" >nul
copy /y "%DAEMON_BIN%" "%STAGE_DIR%\emulecored.exe" >nul
echo   emuleqt.exe
echo   emulecored.exe

REM -- Copy config data -------------------------------------------------------

set "CONFIG_SRC=%PROJECT_DIR%\data\config"
set "CONFIG_DST=%STAGE_DIR%\config"

if exist "%CONFIG_SRC%" (
    echo(
    echo === Copying config data ===
    xcopy /s /e /i /q "%CONFIG_SRC%" "%CONFIG_DST%" >nul
    echo   config\ copied
) else (
    echo Warning: %CONFIG_SRC% not found -- skipping config data.
)

REM -- Copy translation files ---------------------------------------------------

set "LANG_DST=%STAGE_DIR%\lang"
mkdir "%LANG_DST%" 2>nul

REM Prefer build-generated .qm files; fall back to source lang\ directory
set "LANG_COPIED=0"
for %%D in ("%BUILD_DIR%\src\gui\%CONFIG%" "%BUILD_DIR%\src\gui" "%PROJECT_DIR%\src\gui\%CONFIG%" "%PROJECT_DIR%\lang") do (
    if exist "%%~D\" (
        dir /b "%%~D\emuleqt_*.qm" >nul 2>&1 && (
            copy /y "%%~D\emuleqt_*.qm" "%LANG_DST%\" >nul 2>&1
            set "LANG_COPIED=1"
        )
    )
)
if "%LANG_COPIED%"=="1" (
    echo(
    echo === Copying translation files ===
    echo   lang\ copied
) else (
    echo Warning: No .qm translation files found -- skipping lang data.
)

REM -- Run windeployqt --------------------------------------------------------

echo(
echo === Running windeployqt ===
set "DEPLOY_MODE=--release"
if /i "%CONFIG%"=="Debug" set "DEPLOY_MODE=--debug"
"%WINDEPLOYQT%" %DEPLOY_MODE% --no-translations --no-system-d3d-compiler --no-opengl-sw "%STAGE_DIR%\emuleqt.exe"
if errorlevel 1 (
    echo Warning: windeployqt reported errors ^(continuing^).
)

REM -- Copy OpenSSL DLLs if present -------------------------------------------

REM Qt's network module needs OpenSSL at runtime.  windeployqt does not
REM always copy them, so we look in the Qt bin directory and in the
REM system-wide OpenSSL install.
for %%D in ("%QT_DIR%\bin" "C:\Program Files\OpenSSL-Win64\bin" "C:\OpenSSL-Win64\bin") do (
    if exist "%%~D\libssl-3-x64.dll" (
        if not exist "%STAGE_DIR%\libssl-3-x64.dll" (
            echo   Copying OpenSSL DLLs from %%~D
            copy /y "%%~D\libssl-3-x64.dll" "%STAGE_DIR%\" >nul 2>&1
            copy /y "%%~D\libcrypto-3-x64.dll" "%STAGE_DIR%\" >nul 2>&1
        )
    )
)

REM -- Copy MSVC C++ runtime DLLs ----------------------------------------------

REM windeployqt does not bundle the Visual C++ runtime (vcruntime140.dll,
REM msvcp140.dll, concrt140.dll, etc.).  We locate them from the VS redist
REM directory and copy them so the app runs without the VC++ Redistributable.

set "VCRT_DIR="

REM 1. VCToolsRedistDir is set by the VS Developer Command Prompt
if defined VCToolsRedistDir (
    if exist "%VCToolsRedistDir%x64\Microsoft.VC143.CRT" (
        set "VCRT_DIR=%VCToolsRedistDir%x64\Microsoft.VC143.CRT"
    )
)

REM 2. Fallback: scan VSINSTALLDIR
if not defined VCRT_DIR if defined VSINSTALLDIR (
    for /d %%R in ("%VSINSTALLDIR%\VC\Redist\MSVC\*") do (
        if exist "%%R\x64\Microsoft.VC143.CRT" set "VCRT_DIR=%%R\x64\Microsoft.VC143.CRT"
    )
)

REM 3. Fallback: common VS 2022 install paths
if not defined VCRT_DIR (
    for %%E in (Enterprise Professional Community BuildTools) do (
        for /d %%R in ("C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Redist\MSVC\*") do (
            if exist "%%R\x64\Microsoft.VC143.CRT" set "VCRT_DIR=%%R\x64\Microsoft.VC143.CRT"
        )
    )
)

if defined VCRT_DIR (
    echo(
    echo === Copying MSVC runtime DLLs from %VCRT_DIR% ===
    copy /y "%VCRT_DIR%\concrt140.dll" "%STAGE_DIR%\" >nul 2>&1
    copy /y "%VCRT_DIR%\msvcp140.dll" "%STAGE_DIR%\" >nul 2>&1
    copy /y "%VCRT_DIR%\msvcp140_1.dll" "%STAGE_DIR%\" >nul 2>&1
    copy /y "%VCRT_DIR%\msvcp140_2.dll" "%STAGE_DIR%\" >nul 2>&1
    copy /y "%VCRT_DIR%\vcruntime140.dll" "%STAGE_DIR%\" >nul 2>&1
    copy /y "%VCRT_DIR%\vcruntime140_1.dll" "%STAGE_DIR%\" >nul 2>&1
    echo   MSVC runtime DLLs copied
) else (
    echo Warning: MSVC runtime DLLs not found -- install VC++ Redistributable on target.
    echo   Checked: %%VCToolsRedistDir%%, %%VSINSTALLDIR%%, common VS 2022 paths.
)

REM -- Copy vcpkg runtime DLLs if present --------------------------------------

set "VCPKG_BIN_SUFFIX=bin"
if /i "%CONFIG%"=="Debug" set "VCPKG_BIN_SUFFIX=debug\bin"

set "VCPKG_BIN="
for %%P in ("%PROJECT_DIR%\src\vcpkg_installed\x64-windows\%VCPKG_BIN_SUFFIX%" "%PROJECT_DIR%\vcpkg_installed\x64-windows\%VCPKG_BIN_SUFFIX%" "%BUILD_DIR%\vcpkg_installed\x64-windows\%VCPKG_BIN_SUFFIX%") do (
    if "!VCPKG_BIN!"=="" (
        if exist "%%~P" set "VCPKG_BIN=%%~P"
    )
)

if defined VCPKG_BIN (
    echo(
    echo === Copying vcpkg DLLs from %VCPKG_BIN% ===
    copy /y "%VCPKG_BIN%\*.dll" "%STAGE_DIR%\" >nul 2>&1
    echo   vcpkg DLLs copied
) else (
    echo Note: No vcpkg_installed directory found -- skipping vcpkg DLLs.
)

REM -- Create zip --------------------------------------------------------------

set "ZIP_NAME=emuleqt-v%APP_VERSION%-win64.zip"
set "ZIP_PATH=%PROJECT_DIR%\%ZIP_NAME%"
if exist "%ZIP_PATH%" del "%ZIP_PATH%"

echo(
echo === Creating %ZIP_NAME% ===
powershell -NoProfile -Command "Compress-Archive -Path '%STAGE_DIR%' -DestinationPath '%ZIP_PATH%' -Force"

if exist "%ZIP_PATH%" (
    echo(
    echo === Done ===
    echo Package: %ZIP_PATH%
    echo(
    echo Staging contents:
    dir /b "%STAGE_DIR%"
) else (
    echo Error: Failed to create zip file.
    exit /b 1
)

endlocal
