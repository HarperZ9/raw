# SkyrimSE.exe 1.7.104.0 Bit-Level Engine Map, Relationship Graph & 2011-to-2027 Replacement Matrix

> Authoritative runtime reference for `raw_engine.dll` (`src/engine_rt/`), `d3d11_proxy` (`src/d3d11_proxy/`), and `d3d12_bridge` (`src/d3d12_bridge/`). Every offset listed under **Verified Live** has been read and validated against live `SkyrimSE.exe 1.7.104.0` memory; every offset under **Planned Map** is explicitly marked unverified until probed live.

## 1. End-to-End Bit-Level Signal Flow (Creation Engine -> DX12/DXR 1.2)

```text
SkyrimSE.exe 1.7.104.0 Memory
  |-- Sky::GetSingleton()
  |     +0x48  TESWeather* currentWeather     --> rt_weather.cpp --> PG_SetWeatherState / PG_SetWeatherExt
  |     +0x50  TESWeather* lastWeather        --> WeatherDrive.cpp --> BridgeHookLighting.cpp --> cbuffer P
  |     +0xA0  Precipitation* (+0x84 density)     (weather[4], clouds[4], cloudWind[4], timeEnv[4])
  |     +0xD8  NiColor skyColor[0..2] probed  --> live telemetry verified: [0.76,0.58,0.53] / [0.17,0.12,0.09]
  |     +0x1B0 float currentGameHour (0..24)  --> timeEnv.x, astroSunElev (timeEnv.y), isRealSun()
  |     +0x1B8 float currentWeatherPct (0..1) --> smooth weather hysteresis blend
  |
  |-- BSGraphics::RendererShadowState (ShaderState RVA)
  |     +0x00  ShadowSceneNode*
  |              +0x130 BSTArray<BSLight*> activeLights      --> RAW_EngineLights --> SceneLights.cpp
  |              +0x200 BSLight* sunLight (Sun day / Moon nt)--> sunDir.xyz (SHADOW CASTER ONLY)
  |              +0x230 BSTArray<BSLight*> shadowCasters     --> all promoted to flag 2 (shadow casting)
  |     +0x20  float timers[5]
  |     +0x44  uint8_t isInterior (shader flag)
  |     +0xB0  float gridCenter[2]
  |     +0xBC  uint32_t technique
  |     +0xFC  float ambientSpecular[4]
  |     +0x10C float characterLight[4]
  |     +0x128 uint32_t cameraInWater         --> FLAG_UNDERWATER
  |     +0x144 float waterIntersect
  |
  |-- PlayerCamera / TESObjectCELL / UI::Singleton (rt_view.cpp)
  |     TESCamera +0x28 -> +0x18 camStateId   --> engineWorld.y (0=1st, 1=3rd)
  |     TESObjectCELL +0x40 cellFlags bit 0   --> isInterior (true engine cell ownership, zero cascade flicker)
  |     TESObjectCELL +0x70 float waterHeight --> engineWorld.x (capillary wet-shoreline wicking)
  |     UI::Singleton +0x160 numPausesGame    --> timeEnv.w (isolates Map/Inventory from DoF/Froxels)
  |
  |-- D3D11 -> raw::tx -> D3D12Bridge Shared Surfaces (Zero-Copy Shared Handles)
        Depth (R32F), Normal (RGBA8 oct), Albedo (RGBA8), Specular (RGBA8: mask, gloss, classId),
        ShadowMask (RGBA8), ShadowAtlas (R32F), Motion (RG16F), Emissive/SoftMap (RGBA16F),
        WaterLayer (RGBA32F: depth, viewNormal.xy, class=2.0), BedDepth, PreWater (RGBA16F)
```

## 2. Verified Live Offsets & Heuristic Invariants (`SkyrimSE.exe 1.7.104.0`)

| Struct | Offset | Type | Live Heuristic Invariant | Current Consumer |
|---|---|---|---|---|
| `Sky` | `+0x48` / `+0x50` | `TESWeather*` | Non-null `cur`; `formId` at `+0x14` (e.g. `0x0000081A`) | `rt_weather.cpp` |
| `Sky` | `+0x1B0` | `float` | `0.0 <= gh <= 24.0`; monotonic with game clock | `timeEnv.x`, `astroSunDir()`, `isRealSun()` |
| `Sky` | `+0x1B8` | `float` | `0.0 <= pct <= 1.0` | `rt_weather.cpp` |
| `TESWeather` | `+0x664 + 0x00` | `uint8_t` | `windSpeed` `0..255` -> `45..425 u/s` | `cloudWind.xy` |
| `TESWeather` | `+0x664 + 0x0A` | `uint8_t` | `lightningFrequency` (**inverted**: `255`=off, `0`=max) | `timeEnv.z` (gated on flag bit 4) |
| `TESWeather` | `+0x664 + 0x0B` | `uint8_t` | Flags: `1`=Pleasant, `2`=Cloudy, `4`=Rainy, `8`=Snow | `weather[4]`, `clouds.x` |
| `TESWeather` | `+0x664 + 0x0F` | `uint8_t` | `windDirection` `0..255` -> `0..2pi` | `cloudWind.xy` |
| `TESWeather` | `+0x678 + 0x04` | `float` | `fogFar` (`5,000..150,000u`) | `FogOf()` |
| `ShadowSceneNode` | `+0x130` | `BSTArray<BSLight*>` | `size <= 4096`; importance-ranked top 128 | `cbuffer P lightPosR/lightColS[128]` |
| `ShadowSceneNode` | `+0x200` | `BSLight*` | **Day=Sun, Night=Moon**; elevation clamped `>15°` | `sunDir.xyz` (shadows & lunar beams) |
| `BSLight -> NiLight` | `+0x48` | `NiLight*` | `NiAVObject +0x7C` rot col0 = dir; `+0xA0` = world pos | `rt_livestate.cpp` |
| `NiLight` | `+0x11C/+0x128/+0x134` | `float[3]/f/f` | `color = diffuse * fade`; `radius > 0` | `SceneLights.cpp` |
| `TESObjectCELL` | `+0x40` / `+0x70` | `uint16_t` / `float` | Bit 0 = `IsInterior`; `waterHeight` (e.g. `65.2u`) | `engineWorld.x/z` |
| `TESCamera` | `+0x28 -> +0x18` | `uint32_t` | `0`=1st, `1`=3rd, `<=16` valid | `engineWorld.y` |
| `UI::Singleton` | `+0x160` | `uint32_t` | `numPausesGame > 0` in Menu/Map/Inventory | `timeEnv.w` |
| `BGSDecalManager` | `0x21A3DF8` | Singleton* | Live `audit`: `decal_mgr: true` | `rt_audit.cpp` / `rt_settings.cpp` |
| `BGSGrassManager` | `0x21A05F8` | Singleton* | Live `audit`: `grass_mgr: true` | `rt_audit.cpp` |
| `ImageSpaceManager` | `0x33D40A0` | Singleton* | Live `audit`: `img_space: true` | `rt_audit.cpp` |
| `TESWaterSystem` | `0x21A05E8` | Singleton* | Live `audit`: `water_sys: true` | `rt_audit.cpp` |
| `Sky` (colors) | `+0xD8` / `+0x138` | `NiColor` | Live `audit`: `+0xD8 == ssn.sun_col [0.75,0.71,0.67]`; `+0x138 == [0.45,0.66,0.80]` sky blue (`16/17` finite, `12` non-zero) | `rt_audit.cpp` |

## 3. Complete 2011 -> 2027 Ground-Up Replacement Status Matrix

| Subsystem | 2011 Skyrim Implementation | RAW Ground-Up Replacement | Status | Next S++ Target |
|---|---|---|---|---|
| **DirectX / Frame Graph** | D3D11 single-threaded forward/deferred | `raw::tx` D3D11->DX12 + SM 6.9 + DXR 1.2 + DLSS-RR Preset F | Live (`8.35 ms`) | Split-submit async compute BLAS/TLAS overlap |
| **Occlusion Culling** | CPU occlusion planes + precombines | Native SM 6.9 DX12 Compute Hi-Z (`240x135`, `129 KB`) | Live (`321` culled) | GPU-driven 2-phase ExecuteIndirect (`GEO-02`) |
| **Sky & Atmosphere** | Painted sky dome + 2D sun billboard (`0.0`ed) | Hillaire SIGGRAPH 2020 Transmittance/MS/SkyView LUT + `isRealSun()` + 22° lunar halo + 42°/51° rainbow + airglow | Live (`#288`) | Wire verified `Sky::skyColor[17]` + `Sky::sun`/`Moon` node transforms |
| **Volumetric Clouds** | Flat scrolling 2D cloud layers | `44,000u` Nubis Evolved (`SKY-02`) 3D cumulus + `125,000u` twilight cirrus + lunar cloud scattering | Live (`#288`) | Weather-map storm-cell convection + cloud-shadowed volumetric shafts |
| **Lighting & GI** | 4 shadow lights, Lambert, flat ambient | 128 clustered lights, DXR 1.2 RT shadows/GI/reflections, SVGF 3-pass A-Trous, Bounded VNDF (`MAT-05`), EON (`MAT-03`), snow glints (`MAT-07`) | Live (`#288`) | Multi-bounce world radiance cache / ReSTIR GI (`GI-20`) |
| **Volumetrics & Fog** | Flat metric distance fog + 2011 VL (OFF) | View-Z-exact `160x90x64` 3D Froxel grid + 16-step temporal shafts + moonbeams + interior dust motes | Live (`#288`) | Per-light shadowed interior froxels (`SKY-07`) |
| **Water** | Flat plane + upside-down reflection pass (OFF) | DLSS-RR-before-RtWater + Nyquist-filtered 6-dir Gerstner + RT/SSR + Snell's Window/TIR + caustics + shoreline runup | Live (`#288`) | Map `TESWaterSystem::Singleton` per-body flow vectors + FFT cascade (`WAT-02`) |
| **LODs & Billboards** | Low-res mountain LODs + flat tree cards | 24h Sun/Moon/weather-synced `DistantLod.hlsli` + geological strata + relit conical tree billboards (`FOL-04`) | Live (`#288`) | Octahedral impostors (`FOL-05`) + procedural GPU grass (`FOL-06`) |
| **Skinned Actors (RT)** | Not in BVH | `RtSkin.hlsl` FP16 renorm + cam-relative pre-sub (`0` stretched) | Live (`0` stretched) | Map `NiSkinInstance` bone palettes to eliminate remaining ~800 floating tris |
| **Decals & Meshes** | Low 2011 decal caps, z-fighting | Raised `iMaxDecalsPerFrame=256`, `fDecalLOD1/2=4000/6000` | Live (`#286`) | Map `BGSDecalManager` + `BSLightingShaderProperty` material flags |
