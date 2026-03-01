@echo off
setlocal EnableDelayedExpansion

:: ═══════════════════════════════════════════════════════════════════════════
::  SkyrimBridge v3.0 — Deployment Script
::
::  Deploys shaders, DLL, and config to the MO2 mod directory.
::
::  Usage:
::    deploy.bat              — Deploy full shaders + DLL + config
::    deploy.bat passthrough  — Deploy passthrough diagnostic shaders
::    deploy.bat restore      — Restore full shaders from backup
::    deploy.bat backup       — Back up current deployed shaders
::    deploy.bat check        — Verify all expected files exist
::
::  Author: Zain Dana Harper
:: ═══════════════════════════════════════════════════════════════════════════

:: ── Configuration ─────────────────────────────────────────────────────────
set "SHADER_SRC=C:\Users\Zain\SKSE\SkyrimBridge_v3\shader"
set "PASSTHROUGH_SRC=C:\Users\Zain\SKSE\SkyrimBridge_v3\shader\_passthrough"
set "BUILD_DIR=C:\Users\Zain\SKSE\SkyrimBridge_v3\build\Release"
set "CONFIG_SRC=C:\Users\Zain\SKSE\SkyrimBridge_v3\config"

set "ENB_MOD=E:\Modlists\SkyGroundChronicles\mods\ENB of the Elders"
set "ENB_DST=%ENB_MOD%\ROOT\enbseries"
set "DLL_DST=%ENB_MOD%\SKSE\plugins"
set "CFG_DST=%ENB_MOD%\SKSE\plugins\SkyrimBridge"

set "BACKUP_DIR=%ENB_DST%\_backup_rewrite"

:: ── Parse command ─────────────────────────────────────────────────────────
set "CMD=%~1"
if "%CMD%"=="" set "CMD=full"

if /i "%CMD%"=="passthrough" goto :deploy_passthrough
if /i "%CMD%"=="restore"     goto :restore_backup
if /i "%CMD%"=="backup"      goto :create_backup
if /i "%CMD%"=="check"       goto :check_manifest
if /i "%CMD%"=="full"        goto :deploy_full
echo Unknown command: %CMD%
echo Usage: deploy.bat [full^|passthrough^|restore^|backup^|check]
exit /b 1


:: ═══════════════════════════════════════════════════════════════════════════
:deploy_full
:: ═══════════════════════════════════════════════════════════════════════════
echo.
echo === SkyrimBridge Full Deployment ===
echo.

:: Deploy root .fx files
echo Deploying shader .fx files...
for %%f in ("%SHADER_SRC%\*.fx") do (
    copy /Y "%%f" "%ENB_DST%\" >nul
    echo   %%~nxf
)

:: Deploy root .fxh files
for %%f in ("%SHADER_SRC%\*.fxh") do (
    copy /Y "%%f" "%ENB_DST%\" >nul
    echo   %%~nxf
)

:: Deploy subdirectories
echo.
echo Deploying Helper/ ...
if not exist "%ENB_DST%\Helper" mkdir "%ENB_DST%\Helper"
xcopy /Y /S /Q "%SHADER_SRC%\Helper\*" "%ENB_DST%\Helper\" >nul
echo   Done (%SHADER_SRC%\Helper\)

echo Deploying UI/ ...
if not exist "%ENB_DST%\UI" mkdir "%ENB_DST%\UI"
xcopy /Y /S /Q "%SHADER_SRC%\UI\*" "%ENB_DST%\UI\" >nul
echo   Done (%SHADER_SRC%\UI\)

echo Deploying Addons/ ...
if not exist "%ENB_DST%\Addons" mkdir "%ENB_DST%\Addons"
xcopy /Y /S /Q "%SHADER_SRC%\Addons\*" "%ENB_DST%\Addons\" >nul
echo   Done (%SHADER_SRC%\Addons\)

:: Deploy DLL
echo.
echo Deploying DLL...
if not exist "%DLL_DST%" mkdir "%DLL_DST%"
if exist "%BUILD_DIR%\SkyrimBridge_v3.dll" (
    copy /Y "%BUILD_DIR%\SkyrimBridge_v3.dll" "%DLL_DST%\" >nul
    echo   SkyrimBridge_v3.dll
) else (
    echo   WARNING: DLL not found at %BUILD_DIR%\SkyrimBridge_v3.dll
    echo   Run build.bat first!
)

:: Deploy config
echo.
echo Deploying config...
if not exist "%CFG_DST%" mkdir "%CFG_DST%"
if exist "%CONFIG_SRC%\WeatherParams.ini" (
    copy /Y "%CONFIG_SRC%\WeatherParams.ini" "%CFG_DST%\" >nul
    echo   WeatherParams.ini
)
if exist "%CONFIG_SRC%\WeatherClasses.ini" (
    copy /Y "%CONFIG_SRC%\WeatherClasses.ini" "%CFG_DST%\" >nul
    echo   WeatherClasses.ini
)

echo.
echo === Full deployment complete ===
goto :check_manifest


:: ═══════════════════════════════════════════════════════════════════════════
:deploy_passthrough
:: ═══════════════════════════════════════════════════════════════════════════
echo.
echo === SkyrimBridge Passthrough Deployment (Diagnostic Mode) ===
echo.

:: Backup first
call :create_backup

echo.
echo Deploying passthrough shaders (7 failing shaders only)...
echo   Bloom and Lens are NOT touched (they work).
echo.

set "COUNT=0"
for %%f in (
    enbadaptation.fx
    enbeffectprepass.fx
    enbeffect.fx
    enbeffectpostpass.fx
    enbdepthoffield.fx
    enbsunsprite.fx
    enbunderwater.fx
) do (
    if exist "%PASSTHROUGH_SRC%\%%f" (
        copy /Y "%PASSTHROUGH_SRC%\%%f" "%ENB_DST%\%%f" >nul
        echo   %%f  [PASSTHROUGH]
        set /a COUNT+=1
    ) else (
        echo   WARNING: %PASSTHROUGH_SRC%\%%f not found!
    )
)

echo.
echo === Passthrough deployment complete (!COUNT! shaders replaced) ===
echo.
echo To restore full shaders: deploy.bat restore
exit /b 0


:: ═══════════════════════════════════════════════════════════════════════════
:restore_backup
:: ═══════════════════════════════════════════════════════════════════════════
echo.
echo === Restoring Full Shaders from Backup ===
echo.

if not exist "%BACKUP_DIR%" (
    echo ERROR: No backup found at %BACKUP_DIR%
    echo Run "deploy.bat backup" or "deploy.bat passthrough" first.
    exit /b 1
)

set "COUNT=0"
for %%f in (
    enbadaptation.fx
    enbeffectprepass.fx
    enbeffect.fx
    enbeffectpostpass.fx
    enbdepthoffield.fx
    enbsunsprite.fx
    enbunderwater.fx
) do (
    if exist "%BACKUP_DIR%\%%f" (
        copy /Y "%BACKUP_DIR%\%%f" "%ENB_DST%\%%f" >nul
        echo   %%f  [RESTORED]
        set /a COUNT+=1
    )
)

echo.
echo === Restore complete (!COUNT! shaders restored) ===
exit /b 0


:: ═══════════════════════════════════════════════════════════════════════════
:create_backup
:: ═══════════════════════════════════════════════════════════════════════════
echo Creating backup of deployed shaders...
if not exist "%BACKUP_DIR%" mkdir "%BACKUP_DIR%"

for %%f in (
    enbadaptation.fx
    enbeffectprepass.fx
    enbeffect.fx
    enbeffectpostpass.fx
    enbdepthoffield.fx
    enbsunsprite.fx
    enbunderwater.fx
) do (
    if exist "%ENB_DST%\%%f" (
        copy /Y "%ENB_DST%\%%f" "%BACKUP_DIR%\%%f" >nul
        echo   %%f  [BACKED UP]
    )
)
echo Backup saved to: %BACKUP_DIR%
exit /b 0


:: ═══════════════════════════════════════════════════════════════════════════
:check_manifest
:: ═══════════════════════════════════════════════════════════════════════════
echo.
echo === File Manifest Check ===
echo.

set "MISSING=0"
set "FOUND=0"

:: Check root shader files
echo Checking root shaders...
for %%f in (
    enbadaptation.fx
    enbbloom.fx
    enbdepthoffield.fx
    enbeffect.fx
    enbeffectpostpass.fx
    enbeffectprepass.fx
    enblens.fx
    enbsunsprite.fx
    enbunderwater.fx
    enbglobals.fxh
) do (
    if exist "%ENB_DST%\%%f" (
        set /a FOUND+=1
    ) else (
        echo   MISSING: %%f
        set /a MISSING+=1
    )
)

:: Check Helper/ files
echo Checking Helper/ ...
for %%f in (
    SkyrimBridge.fxh
    enbHelper_Common.fxh
    enbHelper_Debug.fxh
    enbHelper_Dither.fxh
    PrePassAddonTechniques.fxh
    Effect_AtmosphericFog.fxh
    Effect_CinematicFX.fxh
    Effect_CRTShader.fxh
    Effect_ProceduralLensDirt.fxh
    Effect_ProceduralWeatherFX.fxh
    enbUI_CinematicFX.fxh
    enbUI_CRT.fxh
    enbUI_DepthOfField.fxh
    enbUI_Fog.fxh
    enbUI_Lens.fxh
    enbUI_PostPass.fxh
) do (
    if exist "%ENB_DST%\Helper\%%f" (
        set /a FOUND+=1
    ) else (
        echo   MISSING: Helper\%%f
        set /a MISSING+=1
    )
)

:: Check UI/ files
echo Checking UI/ ...
for %%f in (
    enbUI_Primer.fxh
    enbUI_PrePass.fxh
    enbUI_SunSprite.fxh
    enbUI_CinematicFX.fxh
    enbUI_CRT.fxh
    enbUI_DepthOfField.fxh
    enbUI_Fog.fxh
    enbUI_Lens.fxh
    enbUI_PostPass.fxh
) do (
    if exist "%ENB_DST%\UI\%%f" (
        set /a FOUND+=1
    ) else (
        echo   MISSING: UI\%%f
        set /a MISSING+=1
    )
)

:: Check Addons/ files
echo Checking Addons/ ...
for %%f in (
    Effect_AtmosphericFog.fxh
    Effect_CinematicFX.fxh
    Effect_CRTShader.fxh
    Effect_ProceduralLensDirt.fxh
    Effect_ProceduralWeatherFX.fxh
    PrePass_ParticleField.fxh
    PrePass_PhotoStudio.fxh
    PrePass_SnowCover.fxh
    PrePass_StylizationSuite.fxh
) do (
    if exist "%ENB_DST%\Addons\%%f" (
        set /a FOUND+=1
    ) else (
        echo   MISSING: Addons\%%f
        set /a MISSING+=1
    )
)

:: Check DLL
echo Checking DLL...
if exist "%DLL_DST%\SkyrimBridge_v3.dll" (
    set /a FOUND+=1
) else (
    echo   MISSING: SkyrimBridge_v3.dll
    set /a MISSING+=1
)

:: Check config
echo Checking config...
if exist "%CFG_DST%\WeatherParams.ini" (
    set /a FOUND+=1
) else (
    echo   MISSING: config\WeatherParams.ini
    set /a MISSING+=1
)

:: Summary
echo.
if !MISSING! EQU 0 (
    echo All !FOUND! files present. Deployment verified.
) else (
    echo WARNING: !MISSING! file(s) missing, !FOUND! file(s) present.
)
echo.
exit /b !MISSING!
