@echo off
setlocal enabledelayedexpansion
REM ---------------------------------------------------------------------------
REM generate-vs.bat -- Generate a Visual Studio 2022 solution for eMule Qt
REM
REM Automatically installs missing dependencies (OpenSSL, ZLIB) via vcpkg,
REM auto-detects Qt, and generates the VS solution.
REM
REM Usage:
REM   scripts\generate-vs.bat [qt-dir]
REM
REM   qt-dir   Optional path to Qt MSVC kit (auto-detected if omitted)
REM
REM After running, open build\eMuleQt.sln in Visual Studio.
REM ---------------------------------------------------------------------------

set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%\.."
set "PROJECT_DIR=%CD%"
popd

REM -- Check for CMake ----------------------------------------------------------

where cmake >nul 2>&1
if errorlevel 1 (
    echo Error: CMake not found. Install from https://cmake.org/download/
    exit /b 1
)

REM -- Check for git (needed by vcpkg and FetchContent) -------------------------

where git >nul 2>&1
if errorlevel 1 (
    echo Error: git not found. Install from https://git-scm.com/download/win
    exit /b 1
)

REM -- Setup vcpkg for OpenSSL and ZLIB -----------------------------------------

set "VCPKG_DIR=%PROJECT_DIR%\build\vcpkg"

if not exist "%VCPKG_DIR%\vcpkg.exe" (
    echo.
    echo === Installing vcpkg ===
    if not exist "%VCPKG_DIR%" (
        git clone https://github.com/microsoft/vcpkg.git "%VCPKG_DIR%"
    )
    call "%VCPKG_DIR%\bootstrap-vcpkg.bat" -disableMetrics
    if errorlevel 1 (
        echo Error: vcpkg bootstrap failed.
        exit /b 1
    )
)

REM -- Install OpenSSL and ZLIB via vcpkg ---------------------------------------

echo.
echo === Checking dependencies via vcpkg ===
"%VCPKG_DIR%\vcpkg.exe" install openssl:x64-windows zlib:x64-windows
if errorlevel 1 (
    echo Error: vcpkg install failed.
    exit /b 1
)

set "VCPKG_TOOLCHAIN=%VCPKG_DIR%\scripts\buildsystems\vcpkg.cmake"

REM -- Detect Qt ----------------------------------------------------------------

set "QT_DIR=%~1"
if "%QT_DIR%"=="" (
    for %%V in (6.10.2 6.10.1 6.10.0 6.9.0 6.8.0 6.7.0) do (
        if exist "C:\Qt\%%V\msvc2022_64" (
            set "QT_DIR=C:\Qt\%%V\msvc2022_64"
            goto :qt_found
        )
        if exist "%USERPROFILE%\Qt\%%V\msvc2022_64" (
            set "QT_DIR=%USERPROFILE%\Qt\%%V\msvc2022_64"
            goto :qt_found
        )
    )
    echo.
    echo Warning: Qt not auto-detected. Pass it as argument:
    echo   scripts\generate-vs.bat C:\Qt\6.10.2\msvc2022_64
    echo.
    echo Continuing without Qt path hint...
    set "QT_PREFIX_ARG="
    goto :configure
)
:qt_found
echo.
echo Using Qt: %QT_DIR%
set "QT_PREFIX_ARG=-DCMAKE_PREFIX_PATH=%QT_DIR%"

REM -- Configure ----------------------------------------------------------------

:configure
echo.
echo === Generating Visual Studio 2022 solution ===
cmake -S "%PROJECT_DIR%" -B "%PROJECT_DIR%\build" ^
    -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
    %QT_PREFIX_ARG%

if errorlevel 1 (
    echo.
    echo Error: CMake configure failed.
    echo.
    echo Usage: scripts\generate-vs.bat [qt-dir]
    echo Example: scripts\generate-vs.bat C:\Qt\6.10.2\msvc2022_64
    exit /b 1
)

echo.
echo === Done ===
echo Open build\eMuleQt.sln in Visual Studio.

endlocal