@echo off
setlocal
cd /d "%~dp0"

rem Use the current project build as the single launch target.
set "EXE_PATH=build\HarmonizerStudio_artefacts\Release\Harmonizer Studio.exe"
if not exist "%EXE_PATH%" (
    echo [ERROR] Harmonizer Studio is not built. Please run build.bat first.
    exit /b 1
)

if /i "%~1"=="--print-exe" (
    echo %EXE_PATH%
    exit /b 0
)

start "" "%EXE_PATH%"
