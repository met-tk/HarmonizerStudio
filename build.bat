@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo   Harmonizer Studio - Building with MSVC 2022
echo ===================================================

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if %errorlevel% neq 0 (
    echo [ERROR] Failed to initialize Visual Studio build environment.
    exit /b 1
)

if not exist build (
    cmake -B build -G "Visual Studio 17 2022" -A x64
    if %errorlevel% neq 0 (
        echo [ERROR] CMake configuration failed.
        exit /b 1
    )
)

cmake --build build --config Release --parallel
if %errorlevel% neq 0 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo ===================================================
echo   Build Successful! Executable:
echo   build\HarmonizerStudio_artefacts\Release\Harmonizer Studio.exe
echo ===================================================
