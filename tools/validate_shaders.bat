@echo off
setlocal enabledelayedexpansion
REM ============================================================================
REM  validate_shaders.bat — Verify all shader #include paths resolve
REM
REM  Usage: Run from the SkyrimBridge_v3 root directory
REM         tools\validate_shaders.bat
REM ============================================================================

set SHADER_DIR=%~dp0..\shader
set ERRORS=0
set CHECKED=0

echo.
echo === SkyrimBridge Shader Include Validator ===
echo Scanning: %SHADER_DIR%
echo.

REM Check all .fx files
for %%f in ("%SHADER_DIR%\*.fx") do (
    call :check_includes "%%f"
)

REM Check all .fxh files in subdirectories
for /r "%SHADER_DIR%" %%f in (*.fxh) do (
    call :check_includes "%%f"
)

echo.
echo === Results ===
echo Checked: %CHECKED% include directives
if %ERRORS% equ 0 (
    echo Status:  ALL INCLUDES RESOLVE
    echo.
    exit /b 0
) else (
    echo Status:  %ERRORS% MISSING FILE(S) FOUND
    echo.
    exit /b 1
)

:check_includes
set "FILE=%~1"
for /f "tokens=*" %%L in ('findstr /n /c:"#include" "%FILE%"') do (
    set "LINE=%%L"
    REM Skip commented-out includes
    echo !LINE! | findstr /c:"//" >nul 2>&1
    if errorlevel 1 (
        REM Extract the include path from between quotes
        for /f "tokens=2 delims=^"" %%P in ("!LINE!") do (
            set /a CHECKED+=1
            set "INCPATH=%SHADER_DIR%\%%P"
            if not exist "!INCPATH!" (
                echo MISSING: %%P
                echo   Referenced by: %FILE%
                set /a ERRORS+=1
            )
        )
    )
)
exit /b
