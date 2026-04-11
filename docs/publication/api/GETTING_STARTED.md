# Getting Started with SkyrimBridge

This is the entry point for all SkyrimBridge documentation. Start here.

---

## What Is SkyrimBridge?

SkyrimBridge connects Skyrim's game engine to ENB shaders. It reads internal engine data (sun position, weather, fog, player state, camera matrices, nearby lights, etc.) and delivers it to your `.fx` shaders every frame as 122 named `float4` parameters.

Without SkyrimBridge, ENB shaders can only see what's on screen. With it, they know the actual game state — whether it's raining, how deep underwater the player is, what material a pixel belongs to, the exact sun direction, the player's health, and much more.

It ships as two files:

| File | What it is | Where it goes |
|------|-----------|---------------|
| `SkyrimBridge_v3.dll` | SKSE plugin. Reads game state, pushes data to GPU. | `Data/SKSE/Plugins/` |
| `SkyrimBridge_ENB.dllplugin` | ENB external plugin. Reads shared memory, serves data to ENB's pull API. | Game root (`enbseries/` or alongside `enbseries.ini`) |

The SKSE plugin does the heavy lifting. The ENB plugin is optional — it exists for ENB presets that use `ENBGetParameter()` instead of the constant buffer.

---

## Prerequisites

You need all of the following installed and working before SkyrimBridge will do anything.

### 1. Skyrim SE or AE

- **Skyrim Special Edition** (1.5.97) or **Anniversary Edition** (1.6.x)
- Must be a legitimate Steam or GOG copy (SKSE requires it)
- Both SE and AE are supported — SkyrimBridge uses Address Library for version-independent offsets

### 2. SKSE64 (Skyrim Script Extender)

SkyrimBridge is an SKSE plugin. Without SKSE, the DLL is never loaded.

- Download from [skse.silverlock.org](https://skse.silverlock.org/)
- Match the SKSE version to your game version exactly (SE build for 1.5.97, AE build for 1.6.x)
- Install by copying `skse64_loader.exe`, `skse64_1_5_97.dll` (or equivalent), and `Data/Scripts/` to your game directory
- **Verify it works**: launch via `skse64_loader.exe`, open the console (`~`), type `getskseversion`. You should see a version number.

### 3. Address Library for SKSE Plugins

SkyrimBridge uses `REL::RelocationID` to find game functions at runtime. This requires Address Library.

- **SE (1.5.97)**: [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- **AE (1.6.x)**: [Address Library for SKSE Plugins - Anniversary Edition](https://www.nexusmods.com/skyrimspecialedition/mods/32444) (same mod page, AE version)
- Install via your mod manager or manually into `Data/`
- This is a hard requirement — SkyrimBridge will fail to load without it

### 4. ENBSeries

SkyrimBridge pushes data to ENB shaders. You need ENB installed.

- Download from [enbdev.com](http://enbdev.com/download_mod_tesskyrimse.htm)
- You need at minimum: `d3d11.dll`, `d3dcompiler_46e.dll`, and `enbseries.ini` in your game root
- SDK version must be >= 1000 (any ENB binary from the last several years qualifies)
- An ENB preset is not strictly required (SkyrimBridge works with default ENB settings), but you'll want one to actually see the data being used

### 5. A Mod Manager (Recommended)

Not required, but strongly recommended:

- **Mod Organizer 2 (MO2)** — SkyrimBridge is fully compatible with MO2's virtual filesystem
- **Vortex** — Also works fine
- Manual installation is supported but more error-prone

---

## Installation

### Option A: Mod Manager (MO2 / Vortex)

Create a new mod with the following structure:

```
<your mod>/
|
|-- SKSE/
|   |-- Plugins/
|   |   |-- SkyrimBridge_v3.dll
|   |   |-- SkyrimBridge/
|   |       |-- WeatherParams.ini
|   |       |-- ShaderCache/           (created automatically at runtime)
|   |       |-- LUTs/                  (optional, for Texture3D color grading)
|
|-- ROOT/                              (MO2 "root" folder — contents go to game directory)
    |-- enbseries/
    |   |-- SkyrimBridge_ENB.dllplugin (optional — only needed for pull-model ENB presets)
    |   |
    |   |-- enbadaptation.fx           (9 shader files — replace existing)
    |   |-- enbbloom.fx
    |   |-- enbdepthoffield.fx
    |   |-- enbeffect.fx
    |   |-- enbeffectpostpass.fx
    |   |-- enbeffectprepass.fx
    |   |-- enblens.fx
    |   |-- enbsunsprite.fx
    |   |-- enbunderwater.fx
    |   |-- enbglobals.fxh
    |   |
    |   |-- Helper/                    (entire directory)
    |   |   |-- SkyrimBridge.fxh       (parameter declarations + helper functions)
    |   |   |-- SkyrimBridge_CB.fxh    (constant buffer overlay)
    |   |   |-- EotE_Common.fxh        (shared utilities)
    |   |   |-- EotE_Tonemappers.fxh
    |   |   |-- enbHelper_Common.fxh
    |   |   |-- enbHelper_Debug.fxh
    |   |   |-- enbHelper_Dither.fxh
    |   |   |-- enbUI_Primer.fxh
    |   |   |-- Effect_CAS.fxh
    |   |   |-- Effect_WeatherLUT.fxh
    |   |   |-- PrePassAddonTechniques.fxh
    |   |
    |   |-- UI/                        (entire directory — UI parameter definitions)
    |   |   |-- enbUI_Primer.fxh
    |   |   |-- enbUI_SkyrimBridge.fxh
    |   |   |-- enbUI_Lens.fxh
    |   |   |-- enbUI_DepthOfField.fxh
    |   |   |-- enbUI_PrePass.fxh
    |   |   |-- enbUI_PostPass.fxh
    |   |   |-- enbUI_Fog.fxh
    |   |   |-- enbUI_CinematicFX.fxh
    |   |   |-- enbUI_CRT.fxh
    |   |   |-- enbUI_SunSprite.fxh
    |   |
    |   |-- Addons/                    (entire directory — effect implementations)
    |   |   |-- Effect_AtmosphericFog.fxh
    |   |   |-- Effect_CinematicFX.fxh
    |   |   |-- Effect_CRTShader.fxh
    |   |   |-- Effect_ProceduralLensDirt.fxh
    |   |   |-- Effect_ProceduralWeatherFX.fxh
    |   |
    |   |-- Textures/
    |       |-- LUTs/
    |           |-- Weather/           (21 weather-specific LUT PNGs)
    |               |-- Clear_Day.png, Clear_Night.png, Clear_DawnDusk.png
    |               |-- Cloudy_*.png, Foggy_*.png, Rain_*.png
    |               |-- Snow_*.png, Thunder_*.png, Ash_*.png
```

Enable the mod and ensure it loads after your base ENB preset (so SkyrimBridge's shaders overwrite the preset's defaults).

**MO2 ROOT folder note**: Files in the `ROOT/` directory are deployed to the game's root folder (where `SkyrimSE.exe` lives), not to `Data/`. This is how ENB files reach the right place through MO2.

### Option B: Manual Installation

Copy files directly to your game directory:

```
<Skyrim SE>/
|-- Data/
|   |-- SKSE/
|       |-- Plugins/
|           |-- SkyrimBridge_v3.dll
|           |-- SkyrimBridge/
|               |-- WeatherParams.ini
|
|-- enbseries/
|   |-- SkyrimBridge_ENB.dllplugin
|   |-- Helper/   (copy entire directory)
|   |-- UI/       (copy entire directory)
|   |-- Addons/   (copy entire directory)
|   |-- Textures/ (copy entire directory)
|   |-- *.fx      (9 shader files — replace existing)
|   |-- enbglobals.fxh
```

**Important**: The 9 `.fx` files replace whatever your ENB preset shipped with. If you want to keep your existing preset's look, you'll need to merge the SkyrimBridge includes into your preset's shaders manually (see [SHADER_INTEGRATION.md](SHADER_INTEGRATION.md)).

---

## Verifying the Installation

1. **Launch the game** through SKSE (or MO2 with SKSE)
2. **Check the SKSE log** at `Documents/My Games/Skyrim Special Edition/SKSE/SkyrimBridge.log`
   - You should see: `SkyrimBridge v3.0.0 loaded — 122 parameters defined`
   - Then: `SkyrimBridge: ENB SDK connected`
   - Then: `SkyrimBridge: game data loaded — enabling ENB data push`
3. **In-game notification**: A message appears: *"SkyrimBridge v3.0.0 - ENB connected"*
4. **Press INSERT** to open the debug GUI overlay — if it appears, everything is working
5. **Press F10** to toggle the shader compilation diagnostics overlay

If ENB is not detected, you'll see *"SkyrimBridge v3.0.0 - No ENB"* — the DLL loaded but couldn't find ENBSeries.

---

## Understanding the File Structure

SkyrimBridge has three layers of files. Here's what each one does and when you'd touch it.

### Layer 1: The SKSE Plugin (C++ backend)

```
Data/SKSE/Plugins/SkyrimBridge_v3.dll
```

This is the engine. It hooks into Skyrim via SKSE and DirectX, reads game state every frame, and pushes data to the GPU. You never edit this file — it's a compiled binary.

### Layer 2: Configuration

```
Data/SKSE/Plugins/SkyrimBridge/
|-- WeatherParams.ini        Per-weather parameter interpolation rules
|-- WriteBackConfig.ini      Game state write-back rules (FOV, fog, lighting)
|-- FeedbackConfig.ini       GPU read-back slot configuration
|-- ShaderCache/             Auto-populated shader bytecode cache
|-- LUTs/                    Optional Texture3D color grading atlases (*.png)
```

These control SkyrimBridge's behavior. `WeatherParams.ini` is hot-reloadable — edit while playing and changes apply within 1-2 frames.

### Layer 3: ENB Shaders (HLSL)

```
enbseries/
|-- *.fx                     The 9 ENB effect shaders (these do the rendering)
|-- enbglobals.fxh           Shared globals
|
|-- Helper/                  SkyrimBridge integration layer
|   |-- SkyrimBridge.fxh     All 122 parameter declarations + helper functions
|   |-- SkyrimBridge_CB.fxh  Constant buffer at register(b7)
|   |-- EotE_Common.fxh      Math utilities, color space, tonemapping
|   |-- enbHelper_*.fxh      Color science, dithering, debug display
|   |-- Effect_*.fxh         Inline effect modules (CAS sharpening, weather LUTs)
|   |-- enbUI_Primer.fxh     UI macro system for ENB editor
|   |-- PrePassAddonTechniques.fxh  Auto-generated technique routing
|
|-- UI/                      ENB editor parameter definitions
|   |-- enbUI_*.fxh          Per-shader UI controls (sliders, toggles)
|
|-- Addons/                  Full effect implementations
|   |-- Effect_*.fxh         Atmospheric fog, cinematic FX, lens dirt, weather FX, CRT
|
|-- Textures/LUTs/Weather/   Weather-specific color grading LUTs (21 PNGs)
```

**If you're an ENB preset author**, you mainly work in the `.fx` files and the `Helper/` directory. The `.fx` files are the shader entry points that ENB compiles and runs. Everything in `Helper/`, `UI/`, and `Addons/` is `#include`d by them.

**If you're a shader developer** writing custom effects, start with [SHADER_INTEGRATION.md](SHADER_INTEGRATION.md) to learn how to access SkyrimBridge data from HLSL.

**If you just want to install and play**, you don't need to touch any of these — the included shaders use SkyrimBridge data out of the box.

---

## Documentation Guide

Here's what each doc covers and when you'd read it.

| Document | Audience | What It Covers |
|----------|----------|----------------|
| **[GETTING_STARTED.md](GETTING_STARTED.md)** (this file) | Everyone | Prerequisites, installation, file structure, doc map |
| **[SUMMARY.md](SUMMARY.md)** | Everyone | Brief overview of every SkyrimBridge capability |
| **[README.md](README.md)** | Everyone | Full feature list, changelog, directory layout, deployment |
| **[SHADER_INTEGRATION.md](SHADER_INTEGRATION.md)** | Shader authors | How to use SB data in HLSL — includes, helpers, common patterns |
| **[PARAMETER_REFERENCE.md](PARAMETER_REFERENCE.md)** | Shader authors | Complete list of all 122 float4 parameters with per-component docs |
| **[WEATHERPARAMS_REFERENCE.md](WEATHERPARAMS_REFERENCE.md)** | Preset authors | WeatherParams.ini format, all 16 weather parameters, editing guide |
| **[ARCHITECTURE.md](ARCHITECTURE.md)** | Developers | C++ internals — frame lifecycle, hook architecture, tracker pattern, build system |
| **[BORIS_INTEGRATION.md](BORIS_INTEGRATION.md)** | Boris / ENB devs | ENB SDK usage, shared memory binary layout, proposals for native support |

### Reading paths by role:

**"I just want to install this and play"**
1. This file (prerequisites + installation)
2. Done. The included shaders handle everything.

**"I'm making an ENB preset and want to use SkyrimBridge data"**
1. This file (install)
2. [SHADER_INTEGRATION.md](SHADER_INTEGRATION.md) (how to `#include` and use the data)
3. [PARAMETER_REFERENCE.md](PARAMETER_REFERENCE.md) (what data is available)
4. [WEATHERPARAMS_REFERENCE.md](WEATHERPARAMS_REFERENCE.md) (weather-reactive parameter tuning)

**"I want to understand how SkyrimBridge works internally"**
1. [SUMMARY.md](SUMMARY.md) (capabilities overview)
2. [ARCHITECTURE.md](ARCHITECTURE.md) (C++ internals, frame lifecycle, hooks)
3. [BORIS_INTEGRATION.md](BORIS_INTEGRATION.md) (ENB SDK integration details)

---

## GPU Resource Slots

SkyrimBridge occupies specific GPU register slots. If you're writing custom shaders, avoid these:

| Slot | Contents | Access |
|------|----------|--------|
| `b7` | Constant buffer — 122 float4 game state params (1952 bytes) | `SkyrimBridge_CB.fxh` |
| `b15` | Material type ID (uint, during game pass) | DXBCPatcher internal |
| `t15` | Material ID g-buffer (R8_UINT, screen resolution) | `SB_MaterialID` in shaders |
| `t17` | Luminance histogram (R32_FLOAT, 256x1) | `SB_LuminanceHist` |
| `t18` | Film color LUT (R8G8B8A8, 64x64x64 Texture3D) | `SB_FilmLUT` |
| `t19` | Hi-Z depth pyramid (R32_FLOAT, full mip chain) | `SB_HiZPyramid` |
| `s2` | LUT trilinear sampler | `SB_LUTSampler` |
| `u4` | Material ID UAV (game pass only) | DXBCPatcher internal |

Slots `t0`-`t14`, `t16`, `t20`+, `s0`-`s1`, `s3`+, `b0`-`b6`, `b8`-`b14` are all free for your use.

---

## In-Game Controls

| Key | Action |
|-----|--------|
| **INSERT** | Toggle ImGui debug overlay (shows all 122 parameters live) |
| **F10** | Toggle shader compilation diagnostics (errors, warnings, compile times) |
| **F11** | Clear shader error log |
| **F12** | Capture one frame of the D3D11 rendering pipeline to disk (Render Inspector) |

---

## Troubleshooting

**"SkyrimBridge v3.0.0 - No ENB"**
- ENBSeries is not installed, or `d3d11.dll` is missing from the game root
- Make sure ENB's `d3d11.dll` is in the same folder as `SkyrimSE.exe`

**No startup notification at all**
- SKSE is not loading the plugin. Check that `SkyrimBridge_v3.dll` is in `Data/SKSE/Plugins/`
- Verify SKSE itself works: open console, type `getskseversion`
- Check that Address Library is installed

**Shader compilation errors (F10 overlay shows red)**
- Usually means a `.fxh` file is missing from `enbseries/Helper/` or `enbseries/UI/`
- Verify all directories were copied completely

**Debug GUI doesn't appear (INSERT key)**
- The D3D11 hook may have failed. Check the SKSE log for `D3D11 hook initialized`
- Some overlay tools (RivaTuner, Discord overlay) can conflict — try disabling them

**ShaderCache not working**
- The cache directory (`Data/SKSE/Plugins/SkyrimBridge/ShaderCache/`) is created automatically
- Check the SKSE log for `ShaderCache` entries — cache hits show as `[cache hit]` with ~0ms compile time
- If using MO2, the cache may be in the overwrite folder

---

## Quick Reference: Minimal Shader Integration

If you're adding SkyrimBridge to an existing ENB preset, the minimum you need in each `.fx` file is:

```hlsl
// After ENB's built-in parameter declarations:
#include "Helper/SkyrimBridge_CB.fxh"

// In your pixel shader:
float4 PS_MyEffect(VS_OUTPUT IN) : SV_Target
{
    float3 color = TextureColor.Sample(Sampler0, IN.txcoord.xy).rgb;

    // Now you can use any SB_ parameter. For example:
    float wetness = SB_Precip_Surface.x;
    float sunElevation = SB_Sun_Direction.w;
    bool isInterior = SB_Interior_Flags.x > 0.5;

    return float4(color, 1.0);
}
```

See [SHADER_INTEGRATION.md](SHADER_INTEGRATION.md) for the full guide with helper functions, common patterns, and examples.
