@echo off
REM ---------------------------------------------------------------------------
REM generate-vs.bat -- Generate a Visual Studio 2022 solution for eMule Qt
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

set "EXTRA_ARGS="
if not "%~1"=="" (
    set "EXTRA_ARGS=-DCMAKE_PREFIX_PATH=%~1"
)

echo === Generating Visual Studio 2022 solution ===
cmake -S "%PROJECT_DIR%" -B "%PROJECT_DIR%\build" -G "Visual Studio 17 2022" -A x64 %EXTRA_ARGS%

if errorlevel 1 (
    echo.
    echo Error: CMake configure failed.
    echo If Qt was not found, pass the Qt path as argument:
    echo   scripts\generate-vs.bat C:\Qt\6.10.2\msvc2022_64
    exit /b 1
)

echo.
echo === Done ===
echo Open build\eMuleQt.sln in Visual Studio.
