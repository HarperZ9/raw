<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/HarperZ9/raw/master/docs/art/hero-dark.svg">
  <img src="https://raw.githubusercontent.com/HarperZ9/raw/master/docs/art/hero-light.svg" alt="RAW: Native DirectX 12, Shader Model 6.9, and DXR 1.2 rendering and engine platform for Skyrim Special Edition." width="100%">
</picture>

# RAW

Native DirectX 12, Shader Model 6.9, DXR 1.2, and NVIDIA Streamline rendering and engine runtime for **The Elder Scrolls V: Skyrim Special Edition** (`1.6.1170.0` and `1.7.104.0`).

[![license](https://img.shields.io/badge/license-MIT-e6e1d6?style=flat-square&labelColor=1a1712)](https://github.com/HarperZ9/raw/blob/master/LICENSE)
![C++23](https://img.shields.io/badge/language-C%2B%2B23-e6e1d6?style=flat-square&labelColor=1a1712)
![DirectX 12](https://img.shields.io/badge/API-DX12%20%2B%20DXR%201.2%20%2B%20SM%206.9-e6e1d6?style=flat-square&labelColor=1a1712)

RAW translates Skyrim's D3D11 rendering pipeline directly into a native DirectX 12 render graph (`d3d11.dll` `raw::tx` + `d3d12_bridge`) paired with a compiled Creation Engine runtime (`raw_engine.dll`). It replaces Skyrim's 2011 lighting, shadows, reflections, sky, clouds, fog, water, post-processing, and legacy engine bottlenecks with hardware ray tracing, neural ray reconstruction, physical atmosphere and fluid simulation, and live in-game tuning.

> Not affiliated with or endorsed by Bethesda Softworks, NVIDIA, or ENBSeries. Requires a legally owned copy of Skyrim Special Edition. Provided as-is under the MIT license (NVIDIA Streamline binaries remain subject to NVIDIA's SDK license).

---

## Quick Install

1. Disable or remove conflicting graphics injectors: **ENBSeries** (`d3d11.dll`, `d3dcompiler_46e.dll`), **Community Shaders**, **ReShade**, and **SSE Display Tweaks**.
2. Extract `RAW-SkyrimSE-DX12-v1.0.0-r304.zip` into your Skyrim Special Edition root folder (next to `SkyrimSE.exe`), or into a Mod Organizer 2 Root Builder mod under `Root/`.
3. Launch `SkyrimSE.exe` (or `skse64_loader.exe` via MO2).

### In-Game Controls

| Key | Action |
|---|---|
| `End` | Open / close RAW DX12 Studio overlay (8 live tabs) |
| `PgUp` / `PgDn` | Switch studio tabs (`1:RT`, `2:Sky/Fog`, `3:Weather`, `4:Water`, `5:Skin/AO`, `6:Tonemap`, `7:LUTs/OV`, `8:Cinema/DoF`) |
| Arrow Keys | Adjust selected parameter live |
| `S` / `L` | Save / load user preset slot |
| `F10` | Cycle lighting mode (Improved / Match / Off) |

---

## Measured Performance (`1920x1080`, RTX 4090, Full RT + DLSS-RR Preset F)

All measurements below were collected passively over 300-frame windows in live Skyrim SE exterior gameplay (`1,779` to `1,791` tested draws, `~846k` triangles) with DXR 1.2 shadows/GI/reflections/OMMs, NVIDIA DLSS Ray Reconstruction (Preset F), Reflex, 3-layer volumetric clouds, 3D froxel fog, and RT water active:

| Metric | Measured Result | Notes |
|---|---|---|
| Live frame time (`measure`, 300 frames) | **8.31 ms to 12.78 ms** (`78` to `120 FPS`) | `p99`: `9.73` to `14.88 ms` |
| Native D3D12 render graph (`ao` + `lighting` + `fog`) | **2.47 ms to 3.68 ms** | `ao 0.21 ms`, `lighting 2.49–3.01 ms`, `fog 0.47–0.62 ms` |
| D3D11 -> D3D12 bridge overhead | **0.32 ms to 0.72 ms** | Zero-copy shared handles via `raw::tx` |
| Native DX12 Compute Hi-Z culling (`240x135`) | **152 to 353 draws culled/frame** | `44k` to `145k` triangles saved (`5.3%` to `17.4%`) |
| `RtSkin` skinned actor BLAS quality | **0 to 6 stretched tris** of `78k+` | Down from `1,478` stretched tris; `0` out-of-range indices |
| Papyrus VM governor (`vm_ms_total` / 300 frames) | **5.77 ms to 7.25 ms** | `~0.02 ms/frame` adaptive budget |

---

## Core Systems & Features

### 1. Native DirectX 12, DXR 1.2 & NVIDIA Streamline
- **`raw::tx` D3D11-to-DX12 Translation:** Adopts a native D3D12 Agility SDK `1.620.1` device, compiles all lighting/atmosphere/RT shaders as SM 6.9 DXIL, and disables 5 redundant 2011 Skyrim passes (`bUseWaterReflections`, `bScreenSpaceReflectionEnabled`, `bSAOEnable`, `bVolumetricLightingEnable`, `bDoDepthOfField`).
- **DXR 1.2 Inline Ray Tracing:** Hardware ray-traced sun and local-light shadows, Opacity Micromaps (OMMs) for alpha-tested foliage, Bounded VNDF Spherical-Cap GGX reflection sampling (`Tokuyoshi & Eto 2024`), 3-pass SVGF A-Trous GI denoiser, disocclusion-boosted RTAO (`RtSkyFilter.hlsl`), and Karis firefly-suppressed reflection filtering (`RtReflFilter.hlsl`).
- **NVIDIA Streamline:** Live DLSS Ray Reconstruction (Preset F) fed by a dedicated SM 6.9 `NormalUnpack.hlsl` linear world-space normal+roughness pass, DLAA Transformer (Preset K), and Reflex Low Latency.
- **Native DX12 Compute Hi-Z (`HiZDownsample.hlsl` + `OcclusionCuller.cpp`):** `1920x1080 -> 240x135` GPU depth reduction driving conservative frustum, sub-pixel, and Hi-Z occlusion culling.

### 2. Sky, 3-Layer Volumetric Clouds & Synoptic Weather
- **Hillaire SIGGRAPH 2020 Physical Atmosphere (`AtmosphereLut.hlsl`):** 256x64 Transmittance LUT, 32x32 Multiple-Scattering LUT, and 192x108 per-frame Sky-View LUT driven by true 24-hour astronomical solar elevation (`Sky::currentGameHour`) decoupled from `ShadowSceneNode->sunLight`'s night-time Moon flip (`isRealSun()`).
- **3-Layer 4D Ekman-Sheared Volumetric Clouds (`Clouds.hlsl`):**
  1. `46,000..90,000u` towering 3D cumulus with 8-octave path-traced-matched multiple scattering and 4-octave ridged-billow cauliflower edge erosion.
  2. `96,000u` mid-altitude altocumulus wave bands (`+34°` Ekman wind veer, `1.65x` speed).
  3. `130,000u` jet-stream cirrus & mares'-tails (`+54°` Ekman veer, `2.65x` speed) with twilight under-lighting and pearlescent iridescence.
- **Optical & Astrophysical Events (`LightFog.hlsl`):** Eddington solar limb darkening, crepuscular cloud-gap rays, 42°/51° spectral rainbows during sunlit rain, 22° warm lunar halo, nocturnal 557.7 nm airglow, Planck blackbody scintillating starfield, multi-band H-alpha/O-III galactic nebula, and 8-layer Nordic aurora.
- **Ground-Up Synoptic Weather Engine (`rt_weather.cpp`):** Continuous barometric pressure fronts (`982..1030 hPa`), regional climate biomes (`arctic`, `alpine`, `valley_forest`, `tundra`), diurnal valley mist and convective cumulus, Solstheim volcanic ashstorms with glowing embers, and 3D depth-occluded rain/snow/ash precipitation.

### 3. Fluid Water, Materials, Characters & Distant LODs
- **Decoupled Fluid Water (`RtWater.hlsl` + `BridgeWater.cpp`):** Runs after DLSS-RR so animated waves are never temporally frozen; Nyquist pixel-footprint band-limited 6-directional Gerstner swells, `28 u/s` downstream advection, hybrid screen-edge-crossfaded RT/SSR reflections, Snell's Window + Total Internal Reflection underwater optics, chromatic caustics, and shallow-stream rapids foam.
- **PBR Materials & Surface Physics (`Lighting.hlsl`):** 128 clustered lights with Frostbite/UE5 windowed falloff, EON energy-preserving rough diffuse (`Portsmouth et al. 2025`), Kulla-Conty multi-scattering GGX, MAT-01 dual-lobe clearcoat, Belcour-Barla Airy thin-film spectral iridescence, moisture-driven stone moss/lichen, steep-cliff tri-planar geological strata, 3D sastrugi snowdrifts with glacial subsurface scattering and diamond micro-glints, and live engine-`waterHeight` reflective water-bounce caustics on bridges and piers.
- **Characters & Foliage (`Lighting.hlsl`, `Sss.hlsl`, `DistantLod.hlsli`):** Biophysical skin diffusion, dual-lobe skin oil/pore sheen, wet eye cornea specular, Marschner dual-lobe hair, Charlie microfiber cloth sheen, dual-lobe chlorophyll foliage SSS + waxy cuticle sheen, and 24-hour Sun/Moon/weather-relit distant LODs and tree billboards.

### 4. Tonemaps, 12 Live Film/Art Modes & Creation Engine Runtime
- **Color Science & Camera Optics (`Tonemap.hlsl`, `DofPass.hlsl`, `BloomPass.hlsl`):** Troy Sobotka AgX + perceptual punchy look, Stephen Hill exact ACES RRT+ODT, Gran Turismo, Lottes, Hable, Purkinje scotopic night shift, 12-pass Karis HDR bloom, full-range cinema-prime DoF, 2-stage Kodak 5219 halation + 6-tap Panavision anamorphic flare, 6-blade diffraction starburst, Tiffen Black Pro-Mist 1/4 diffusion, cubic Cooke/Zeiss CA, and biomechanical figure-8 camera stride.
- **12 Built-In Film & Artistic Modes (`ToneX2 = 0..11`):** `Neutral`, `Nordic Alpine`, `Kodak 2383`, `Cinema Contrast`, `Kodak Vision3 500T`, `Fuji Velvia 50`, `Tarkovsky Polaroid`, `LomoChrome Purple`, plus 4 real-time artistic shaders (`Oil & Impasto Kuwahara Paint`, `Engraved Ink & Cross-Hatch`, `16-Bit Pixel-Art + Bayer Dither`, and `Romantic Watercolor`).
- **Creation Engine Runtime (`raw_engine.dll`):** False-save-corruption fix (`_setmaxstdio(8192)`), 120 Hz sub-stepped Havok physics with Foot/Look IK, tactical combat AI and weapon stagger tuning, expanded decal/grass/tree budgets, live `Sky`/`ShadowSceneNode`/`TESObjectCELL`/`UI` reads, and a built-in SEH-protected engine heuristic auditor (`engine commands=audit`, documented in `docs/ENGINE-MAP-1.7.104.md`).

---

## Building From Source

Requires Visual Studio 2022/2026 (C++23), CMake 3.25+, Ninja, and DirectX Shader Compiler (`dxc`).

```powershell
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target raw_engine d3d11_proxy
```

## Known Limits

- **Depth format:** `TxDepth32` stays disabled (`TxDepth32=0`) because Skyrim SE's compiled 24-bit DXBC depth shaders mismatch 32-bit float depth promotion.
- **Mod conflicts:** ENBSeries, Community Shaders, ReShade, and SSE Display Tweaks hook the same D3D11/swapchain entry points and must be disabled when running RAW.
- **Fallout 4 port:** Architecture is designed for cross-game reuse, but only Skyrim SE (`1.6.1170.0` and `1.7.104.0`) is built and verified today.
