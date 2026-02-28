@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo ============================================================
echo   SkyrimBridge v3.0 Build Script
echo ============================================================
echo.

:: Find vcpkg
where vcpkg >nul 2>&1
if errorlevel 1 (
    echo vcpkg not in PATH, searching common locations...
    if exist "C:\vcpkg\vcpkg.exe" (
        set "VCPKG_ROOT=C:\vcpkg"
    ) else if exist "%USERPROFILE%\vcpkg\vcpkg.exe" (
        set "VCPKG_ROOT=%USERPROFILE%\vcpkg"
    ) else if exist "D:\vcpkg\vcpkg.exe" (
        set "VCPKG_ROOT=D:\vcpkg"
    ) else (
        echo ERROR: vcpkg not found!
        echo Please install vcpkg and add it to PATH, or set VCPKG_ROOT
        exit /b 1
    )
)

if not defined VCPKG_ROOT (
    for /f "tokens=*" %%i in ('where vcpkg') do set "VCPKG_ROOT=%%~dpi"
    set "VCPKG_ROOT=!VCPKG_ROOT:~0,-1!"
    for %%i in ("!VCPKG_ROOT!") do set "VCPKG_ROOT=%%~dpi"
    set "VCPKG_ROOT=!VCPKG_ROOT:~0,-1!"
)

echo Using VCPKG_ROOT: %VCPKG_ROOT%
echo.

:: Configure
echo [1/2] Configuring CMake...
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" -DCMAKE_BUILD_TYPE=Release

if errorlevel 1 (
    echo.
    echo ERROR: CMake configuration failed!
    exit /b 1
)

echo.

:: Build
echo [2/2] Building Release...
cmake --build build --config Release --parallel

if errorlevel 1 (
    echo.
    echo ERROR: Build failed!
    exit /b 1
)

echo.
echo ============================================================
echo   Build completed successfully!
echo.
echo   Output: build\Release\SkyrimBridge_v3.dll
echo ============================================================

:: Check if output exists
if exist "build\Release\SkyrimBridge_v3.dll" (
    echo.
    echo DLL size:
    for %%A in ("build\Release\SkyrimBridge_v3.dll") do echo   %%~zA bytes
)

endlocal
