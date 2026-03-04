@echo off
setlocal enabledelayedexpansion
REM ---------------------------------------------------------------------------
REM bundle-win.bat -- build and package eMule Qt for Windows
REM
REM Builds the daemon and GUI, bundles Qt runtime DLLs via windeployqt,
REM copies the default config data, and creates a self-contained zip.
REM
REM Usage:
REM   scripts\bundle-win.bat [build-dir] [qt-dir]
REM
REM   build-dir   Path to the CMake build directory (default: build)
REM   qt-dir      Path to the Qt MSVC kit (default: auto-detect)
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

REM -- Parse arguments --------------------------------------------------------

set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=build"

set "QT_DIR=%~2"
if "%QT_DIR%"=="" (
    REM Try common Qt install locations
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
    echo Error: Qt directory not found. Pass it as second argument.
    echo Usage: scripts\bundle-win.bat [build-dir] [qt-dir]
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

echo.
echo === Configuring ===
cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_DIR%"
if errorlevel 1 (
    echo Error: CMake configure failed.
    exit /b 1
)

echo.
echo === Building ===
cmake --build "%BUILD_DIR%" --config Release --parallel
if errorlevel 1 (
    echo Error: Build failed.
    exit /b 1
)

REM -- Locate binaries --------------------------------------------------------

REM Multi-config generators (MSVC) put binaries under Release/
set "GUI_BIN=%BUILD_DIR%\src\gui\Release\emuleqt.exe"
set "DAEMON_BIN=%BUILD_DIR%\src\daemon\Release\emulecored.exe"

REM Single-config generators (Ninja) put binaries directly in the target dir
if not exist "%GUI_BIN%" set "GUI_BIN=%BUILD_DIR%\src\gui\emuleqt.exe"
if not exist "%DAEMON_BIN%" set "DAEMON_BIN=%BUILD_DIR%\src\daemon\emulecored.exe"

if not exist "%GUI_BIN%" (
    echo Error: GUI binary not found.
    echo   Checked: %BUILD_DIR%\src\gui\Release\emuleqt.exe
    echo   Checked: %BUILD_DIR%\src\gui\emuleqt.exe
    exit /b 1
)
if not exist "%DAEMON_BIN%" (
    echo Error: Daemon binary not found.
    echo   Checked: %BUILD_DIR%\src\daemon\Release\emulecored.exe
    echo   Checked: %BUILD_DIR%\src\daemon\emulecored.exe
    exit /b 1
)

echo.
echo GUI binary:    %GUI_BIN%
echo Daemon binary: %DAEMON_BIN%

REM -- Assemble staging directory ---------------------------------------------

set "STAGE_DIR=%BUILD_DIR%\stage\eMule"
if exist "%BUILD_DIR%\stage" rmdir /s /q "%BUILD_DIR%\stage"
mkdir "%STAGE_DIR%"

echo.
echo === Staging binaries ===
copy /y "%GUI_BIN%" "%STAGE_DIR%\emuleqt.exe" >nul
copy /y "%DAEMON_BIN%" "%STAGE_DIR%\emulecored.exe" >nul
echo   emuleqt.exe
echo   emulecored.exe

REM -- Copy config data -------------------------------------------------------

set "CONFIG_SRC=%PROJECT_DIR%\data\config"
set "CONFIG_DST=%STAGE_DIR%\config"

if exist "%CONFIG_SRC%" (
    echo.
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
for %%G in ("%BUILD_DIR%\src\gui\Release\emuleqt_*.qm" "%BUILD_DIR%\src\gui\emuleqt_*.qm" "%PROJECT_DIR%\lang\emuleqt_*.qm") do (
    if exist "%%G" (
        copy /y "%%G" "%LANG_DST%\" >nul 2>&1
        set "LANG_COPIED=1"
    )
)
if "%LANG_COPIED%"=="1" (
    echo.
    echo === Copying translation files ===
    echo   lang\ copied
) else (
    echo Warning: No .qm translation files found -- skipping lang data.
)

REM -- Run windeployqt --------------------------------------------------------

echo.
echo === Running windeployqt ===
"%WINDEPLOYQT%" --release --no-translations --no-system-d3d-compiler --no-opengl-sw "%STAGE_DIR%\emuleqt.exe"
if errorlevel 1 (
    echo Warning: windeployqt reported errors (continuing).
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

REM -- Create zip --------------------------------------------------------------

set "ZIP_NAME=eMuleQt-win64.zip"
set "ZIP_PATH=%PROJECT_DIR%\%ZIP_NAME%"
if exist "%ZIP_PATH%" del "%ZIP_PATH%"

echo.
echo === Creating %ZIP_NAME% ===
pushd "%BUILD_DIR%\stage"
powershell -NoProfile -Command "Compress-Archive -Path 'eMule' -DestinationPath '%ZIP_PATH%' -Force"
popd

if exist "%ZIP_PATH%" (
    echo.
    echo === Done ===
    echo Package: %ZIP_PATH%
    echo.
    echo Staging contents:
    dir /b "%STAGE_DIR%"
) else (
    echo Error: Failed to create zip file.
    exit /b 1
)

endlocal
