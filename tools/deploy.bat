@echo off
setlocal enabledelayedexpansion
REM ============================================================================
REM  deploy.bat — Deploy SkyrimBridge v3 to Skyrim install
REM
REM  Usage:
REM    tools\deploy.bat                          (uses default path)
REM    tools\deploy.bat "D:\Steam\...\Skyrim"    (custom path)
REM    tools\deploy.bat --mo2 "C:\MO2\overwrite" (MO2 overwrite dir)
REM ============================================================================

set "PROJECT_DIR=%~dp0.."
set "DLL_PATH=%PROJECT_DIR%\build\Release\SkyrimBridge_v3.dll"

REM Default Skyrim path — edit this for your system
set "SKYRIM_DIR=C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition"
set "MO2_MODE=0"

REM Parse arguments
if "%~1"=="--mo2" (
    set "MO2_MODE=1"
    set "SKYRIM_DIR=%~2"
) else if not "%~1"=="" (
    set "SKYRIM_DIR=%~1"
)

echo.
echo === SkyrimBridge v3.0 Deployer ===
echo Source:  %PROJECT_DIR%
if %MO2_MODE%==1 (
    echo Target:  %SKYRIM_DIR% (MO2 overwrite)
) else (
    echo Target:  %SKYRIM_DIR%
)
echo.

REM Verify DLL exists
if not exist "%DLL_PATH%" (
    echo ERROR: DLL not found at %DLL_PATH%
    echo        Run build.bat first.
    exit /b 1
)

REM Verify target directory exists
if not exist "%SKYRIM_DIR%" (
    echo ERROR: Target directory not found: %SKYRIM_DIR%
    echo        Pass your Skyrim install path as an argument.
    exit /b 1
)

REM Deploy DLL
set "PLUGIN_DIR=%SKYRIM_DIR%\Data\SKSE\Plugins"
if not exist "%PLUGIN_DIR%" mkdir "%PLUGIN_DIR%"
copy /y "%DLL_PATH%" "%PLUGIN_DIR%\SkyrimBridge_v3.dll" >nul
echo [OK] DLL -> %PLUGIN_DIR%\SkyrimBridge_v3.dll

REM Deploy config
set "CONFIG_DIR=%PLUGIN_DIR%\SkyrimBridge"
if not exist "%CONFIG_DIR%" mkdir "%CONFIG_DIR%"
if exist "%PROJECT_DIR%\config\WeatherParams.ini" (
    copy /y "%PROJECT_DIR%\config\WeatherParams.ini" "%CONFIG_DIR%\WeatherParams.ini" >nul
    echo [OK] Config -> %CONFIG_DIR%\WeatherParams.ini
)

REM Deploy shaders (only to enbseries/ in game root, not Data/)
set "ENB_DIR=%SKYRIM_DIR%\enbseries"
if not exist "%ENB_DIR%" mkdir "%ENB_DIR%"
if not exist "%ENB_DIR%\Helper" mkdir "%ENB_DIR%\Helper"
if not exist "%ENB_DIR%\UI" mkdir "%ENB_DIR%\UI"
if not exist "%ENB_DIR%\Addons" mkdir "%ENB_DIR%\Addons"

REM Copy core .fx files
for %%f in ("%PROJECT_DIR%\shader\*.fx") do (
    copy /y "%%f" "%ENB_DIR%\%%~nxf" >nul
    echo [OK] %%~nxf -> enbseries/
)

REM Copy Helper/ includes
for %%f in ("%PROJECT_DIR%\shader\Helper\*.fxh") do (
    copy /y "%%f" "%ENB_DIR%\Helper\%%~nxf" >nul
)
echo [OK] Helper/*.fxh -> enbseries/Helper/

REM Copy UI/ includes
for %%f in ("%PROJECT_DIR%\shader\UI\*.fxh") do (
    copy /y "%%f" "%ENB_DIR%\UI\%%~nxf" >nul
)
echo [OK] UI/*.fxh -> enbseries/UI/

REM Copy Addons/ includes
for %%f in ("%PROJECT_DIR%\shader\Addons\*.fxh") do (
    copy /y "%%f" "%ENB_DIR%\Addons\%%~nxf" >nul
)
echo [OK] Addons/*.fxh -> enbseries/Addons/

echo.
echo === Deployment Complete ===
echo.
echo Deployed files:
echo   1 DLL           -> Data\SKSE\Plugins\
echo   1 Config        -> Data\SKSE\Plugins\SkyrimBridge\
echo   9 Core shaders  -> enbseries\
echo   16 Helper .fxh  -> enbseries\Helper\
echo   9 UI .fxh       -> enbseries\UI\
echo   9 Addon .fxh    -> enbseries\Addons\
echo.
