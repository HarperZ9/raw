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
  |     TESObjectCELL +0x78 float waterHeight / TESWorldSpace +0x214 --> engineWorld.x (capillary wet-shoreline wicking)
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
| `TESObjectCELL` | `+0x40` / `+0x78` | `uint16_t` / `float` | Bit 0 = `IsInterior`, Bit 1 = `HasWater`; `waterHeight` (`+0x68` is `CELL_DATA*`/`EXTERIOR_DATA*`, `+0x70` is `TESObjectLAND*`; `TESWorldSpace +0x214` is `defaultWaterHeight`) | `engineWorld.x/z` |
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

## 4. Binary-Disassembled World, Terrain, Depth & Memory Struct Map (`SkyrimSE.exe 1.7.104.0`)

Every offset and RVA below was directly verified by x86-64 disassembly of `SkyrimSE.exe 1.7.104.0` (`TS=0x6A8C7046`, `SizeOfImage=0x3929000`) and recorded in `data/versions/skyrimse-1.7.104.tsv`. Startup Zydis verification (`rt_zydis_offsets.cpp`) re-validates key machine-code offsets at boot and fails closed before any write if an executable mismatch is detected.

### 4.1 `TESObjectLAND` (`vtable=0x1820C00`, `sizeof=0x48`) & `LoadedLandData`

| Struct | Offset | Type | Binary Proof (RVA) | Semantics & Invariants |
|---|---|---|---|---|
| `TESObjectLAND` | `+0x28` | `uint32_t` | `0x2AE613`, `0x2B08AA` | `dataFlags`: bit 0 = has height data, bit 3 (`0x08`) = 3D loaded (cleared in `UnloadData` `0x2AE5D0`) |
| `TESObjectLAND` | `+0x30` | `TESObjectCELL*` | `0x2AA2F9`, `0x2AD182` | Parent cell linkage; fallback for `GetWorldX/Y` when `loadedData == nullptr` |
| `TESObjectLAND` | `+0x38` | `NiPointer<QueuedFile>` | `0x2B6336`, `0x2AEC88` | Ref-counted queued land texture loader |
| `TESObjectLAND` | `+0x40` | `LoadedLandData*` | `0x2AE61F`, `0x2B3876` | Active heightmap quad data (NULL when unloaded) |
| `LoadedLandData` | `+0x00..+0x1F` | `NiNode*[4]` | `0x2B3924` | 4 quad terrain mesh pointers (`NW/NE/SW/SE`) |
| `LoadedLandData` | `+0x20` | `float[4][289]` | `0x2B390E..0x2B3924` | Height deltas for 4 quads x 17x17 (`0x121`) vertices (`4624` bytes) |
| `LoadedLandData` | `+0x4880` | `NiPointer<NiNode>` | `0x2B4C20` | Parent terrain scene node |
| `LoadedLandData` | `+0x4888` | `float` | `0x2B4C0C`, `0x2B088C` | `minHeight` (uninitialized sentinel `FLT_MAX = 0x7F7FFFFF`) |
| `LoadedLandData` | `+0x488C` | `float` | `0x2B4C16`, `0x2B089D` | `maxHeight` (uninitialized sentinel `-FLT_MAX = 0xFF7FFFFF`) |
| `LoadedLandData` | `+0x49B0` | `NiPointer<NiNode>` | `0x2AECDC` | Detached/attached terrain `NiNode` |
| `LoadedLandData` | `+0x49B8` / `+0x49BC` | `int32_t` | `0x2AD16D`, `0x2AD1CD` | `cellX`, `cellY` (`worldX = cellX << 12`, `4096.0u` per cell) |
| `LoadedLandData` | `+0x49C0` | `float` | `0x2B3933` | `baseHeight`: **true vertex Z = `heights[quad][v] + baseHeight`** |

> [!WARNING]
> **Hardcoded 37-Slot Pool Bottleneck (`0x2B4BB5..0x2B4BC6`):** `LoadedLandData::Dtor` (`0x2B4AD0`) recycles into a fixed 37-entry pool (`global 0x31DEE80 + 0x31DEE84 < 0x25`) guarded by spinlock `0x31DEF18`. Raising `uGridsToLoad` above `5` (`7x7 = 49` cells) overflows this 37-slot pool, causing unpooled heap churn and cell-transition stalls.

### 4.2 `TESObjectCELL` (`vtable=0x18240C8`, `sizeof=0x148`)

| Offset | Type | Binary Proof (RVA) | Semantics & Hidden Couplings |
|---|---|---|---|
| `+0x14` | `uint32_t` | `0x1E5D10` | `formID` |
| `+0x1A` | `uint8_t` | `0x146820` | `formType == 60` (`0x3C`) |
| `+0x40` | `uint16_t` | `0x2BEE00`, `0x2BBA70`, `0x2C52D0` | `cellFlags`: bit 0 = `IsInterior`, bit 1 = `HasWater`, bit 2 = `CantTravelFromHere`, bit 4 (`0x10`) = `NoLODWater`, bit 5 (`0x20`) = `PublicArea`, bit 7 (`0x80`) = `ShowSky`, bit 8 (`0x100`) = `UseSkyLighting` |
| `+0x42` | `uint16_t` | `sizeof 0x148` | `cellGameFlags` (runtime dirty/loaded flags) |
| `+0x44` | `uint8_t` | `0x2BA8F6` | `cellState`: `0=NotLoaded, 1=Unloading, 2=UnloadingData, 3=LoadingData, 4=Loaded, 5=Detaching, 6=AttachQueued, 7=Attached` |
| `+0x48` | `ExtraDataList` | `0x2B7F7A`, `0x2BB060` | Cell extra data (water type, encounter zone, occlusion) |
| `+0x68` | `CELL_DATA*` | `0x2BC626`, `0x2BC646` | **CRITICAL:** `EXTERIOR_DATA*` (`+0x00 = cellX`, `+0x04 = cellY`) for exteriors, `INTERIOR_DATA*` for interiors. Never confuse with `+0x70`! |
| `+0x70` | `TESObjectLAND*` | `0x2B7FBF`, `0x2C4A7B` | **CRITICAL:** Pointer to `TESObjectLAND` (NOT `EXTERIOR_DATA`!). Tested for `[land+0x28] & 7` in `0x2C4A7B`. |
| `+0x78` | `float` | `0x2BEE1A` | `waterHeight`: `>= 2147483648.0f` (`0x4F000000`) means inherit `TESWorldSpace::defaultWaterHeight`; missing water returns `-FLT_MAX` (`0xFF7FFFFF`). |
| `+0x80` | `NavMeshArray*` | `0x2B7FD6` | Cell navigation mesh array |
| `+0x94` / `+0xB0` | `uint32_t` / `void*` | `0x2D6E3E`, `0x2C052B` | Reference hash table capacity & entry buffer |
| `+0x120` | `BSSpinLock` | `0x2CBED0`, `0x2CBF02` | `+0x120 = ownerThreadId`, `+0x124 = lockCount` (acquired before reference mutation/unload) |
| `+0x128` | `TESWorldSpace*` | `0x2BEE2C`, `0x2CB133` | Parent exterior worldspace (`formType == 71`) |
| `+0x130` | `LOADED_CELL_DATA*` | `0x2CB0CE`, `0x2BD297` | Active 3D cell scenegraph (`[+0x00] = NiNode* cell3D`) |
| `+0x138` | `BGSLightingTemplate*` | `0x2CB1D0` | Interior/cell lighting template pointer |

### 4.3 `TESWorldSpace` (`vtable=0x182C120`, `sizeof=0x358`) & `BGSTerrainManager` (`sizeof=0xD0`)

| Struct | Offset | Type | Binary Proof (RVA) | Semantics |
|---|---|---|---|---|
| `TESWorldSpace` | `+0x90` | `BGSTerrainManager*` | `0x30B08C` | Worldspace LOD quadtree manager (active global pointer at `0x3203348`) |
| `TESWorldSpace` | `+0x98` | `TESClimate*` | `0x30B541` | Active climate |
| `TESWorldSpace` | `+0xA0` | `uint8_t` | `0x30DDB0`, `0x30DDE0` | `worldFlags`: bit 0 = `SmallWorld`, bit 1 = `CantFastTravel`, bit 5 (`0x20`) = `NoLODWater`, bit 7 (`0x80`) = `NoSky` |
| `TESWorldSpace` | `+0xA2` | `uint16_t` | `0x30DE3E`, `0x30DE8E` | `parentUseFlags`: bit 0 (`0x01`) = `UseLandData` (inherits `defaultLandHeight` & `defaultWaterHeight` from `parentWorld`) |
| `TESWorldSpace` | `+0xF0` | `NiTMap*` | `0x30F4C4` | Exterior `cellMap` (`(cellX<<16)|cellY -> TESObjectCELL*`) |
| `TESWorldSpace` | `+0xF8` | `TESObjectCELL*` | `0x30B06E` | `persistentCell` |
| `TESWorldSpace` | `+0x158` | `TESWorldSpace*` | `0x30DE32`, `0x30DE82` | `parentWorld` (child worldspaces like WhiterunWorld/WindhelmWorld inherit Tamriel land/water!) |
| `TESWorldSpace` | `+0x200` | `const char*` | `0x30B710` (slot 50) | `editorID` |
| `TESWorldSpace` | `+0x210` | `float` | `0x30DE30` | `defaultLandHeight` (default `-2048.0f` = `0xC5000000`) |
| `TESWorldSpace` | `+0x214` | `float` | `0x30DE80`, `0x30DEC0` | `defaultWaterHeight` |
| `TESWorldSpace` | `+0x218` | `float` | `0x30B611` | `distantLODMult` (default `1.0f`) |
| `BGSTerrainManager` | `+0x10` | `BGSTerrainNode*` | `0x515F52` | Root LOD quadtree node (each `BGSTerrainNode` is `0x50` bytes, `dtor=0x517800`) |
| `BGSTerrainManager` | `+0xB0` / `+0xC0` | `BGSTerrainNode*` | `0x515F56`, `0x515F62` | Node pool begin / end |

### 4.4 `NiCamera` (`vtable=0x1A485B8`, `sizeof=0x188`), `MemoryManager` & `BSShaderProperty`

| Struct | Offset | Type | Binary Proof (RVA) | Semantics |
|---|---|---|---|---|
| `NiCamera` | `+0x7C..+0x9C` | `float[9]` | `0xEF1335..0xEF13C2` | `world.rotate` (3x3 row-major) |
| `NiCamera` | `+0xA0..+0xA8` | `float[3]` | `0xEF131A..0xEF132D` | `world.translate` (world X, Y, Z) |
| `NiCamera` | `+0x110..+0x14C` | `float[4][4]` | `0xEF168F..0xEF16DB` | `worldToCam` combined view-projection matrix |
| `NiCamera` | `+0x150..+0x164` | `NiFrustum` | `0xEF142A..0xEF15AF` | `left(+0x150), right(+0x154), top(+0x158), bottom(+0x15C), near(+0x160), far(+0x164)` |
| `MemoryManager` | Global `0x369A810` | Singleton | `0xCDE250`, `0xCDE6A0` | Primary heap allocator (`Allocate=0xCDE250`, `Deallocate=0xCDE6A0`) |
| `ScrapHeap` | `+0x08/+0x48/+0x50/+0x80/+0x84` | `vtable=0x1A21930` | `0xCDF4C0`, `0xCDF660`, `0xCDF360` | Per-thread scratch allocator (`smallBlocks`, `baseAddress`, `endAddress`, `keepPages`, `numActiveAllocs`) |
| `BSShaderProperty` | `+0x34/+0x38/+0x40/+0x70/+0x78` | `vtable=0x1B2F2D8` | `0x14E77B0`, `0x14E7EC0`, `0x151CA80` | `lastRenderPassState`, `flags` (`uint64_t`: low32=SF1, high32=SF2), `renderPassList`, `effectData`, `material` (`BSLightingShaderMaterialBase*`) |

## 5. Hidden-Flag & Cross-Coupling Dependency Map (Why "House of Cards" Bugs Happen)

1. **`BGSTerrainManager::UpdateLODDistances` (`0x519A80`) Static Cache Trap:**
   - Copies & **squares** `fBlockLevel0Distance` (`-> 0x3203570`), `fBlockLevel1Distance` (`-> 0x3203574`), `fBlockMaximumDistance` (`-> 0x3203578`), and `fTreeLoadDistance` (`-> 0x320357C`), plus copies `fSplitDistanceMult` (`-> 0x3203580`), **`uLockedTerrainLOD:MapMenu` (`-> 0x3203584`)**, **`uLockedObjectMapLOD:MapMenu` (`-> 0x3203588`)**, **`uGridsToLoad:General` (`-> 0x320358C`)**, and **`uLargeRefLODGridSize:General` (`-> 0x3203590`)**.
   - **Consequence:** MapMenu LOD settings directly cross-couple into live world `BGSTerrainManager` LOD transitions; mutating `uGridsToLoad` without synchronizing `GridCellArray` + `0x320358C` causes out-of-bounds cell indexing (`SkyrimSE.exe+0x1A2E7E`) and LOD quadtree tears.
2. **`TESObjectCELL::cellFlags` (`+0x40`) + `TESWorldSpace::parentUseFlags` (`+0xA2`) Water/Land Inheritance Trap:**
   - If `cellFlags & 0x02` (`HasWater`) is clear, `GetExteriorWaterHeight` (`0x2BEE00`) returns `-FLT_MAX` regardless of `+0x78`.
   - If `+0x78 >= 2147483648.0f` (`0x4F000000`), it delegates to `TESWorldSpace::GetDefaultWaterHeight` (`0x30DE80`), which follows `parentWorld` (`+0x158`) ONLY while `parentUseFlags & 0x01` is set!
   - Writing to `+0x78` on a cell with sentinel `2147483648.0f`, or mutating `cellFlags` bit 0 (`IsInterior`) / bit 4 (`NoLODWater`), corrupts water planes across cell borders and breaks save serialization.
3. **`NiCamera::UpdateWorldData` (`0xEF1310`) Standard Non-Reversed-Z 24-Bit Depth Collapse & Large-Coordinate FP32 Jitter:**
   - `NiCamera` computes `1.0f / (far - near)` (`0xEF14DC`) and `far / (far - near)` (`0xEF15D5`) directly in FP32 with **non-reversed-Z** mapping (`near -> 0.0`, `far -> 1.0`).
   - With vanilla `fNearDistance = 15.0` and `far = 350,000`, 99.6% of D24S8 precision is spent within `15..3,750u`, leaving `< 1` ULP per `~40u` at mountain LOD distances (causing massive distant Z-fighting and horizon collapse).
   - **D32_FLOAT / Reversed-Z Note:** When promoting D24S8 depth to `D32_FLOAT` Reversed-Z in `raw::tx` (`DepthOwnership`/`WrappedContextOM`), foliage/alpha-tested draws that rely on `D3D11_COMPARISON_EQUAL` after a depth prepass must relax to `LESS_EQUAL` (standard Z) / `GREATER_EQUAL` (reversed-Z) to prevent FP32 rounding dropouts.
   - At far northern coordinates like Winterhold `(107126, 132705, 8900)`, raw world FP32 ULP is `0.0078u..0.0156u` (and `worldToCam` dot products suffer catastrophic cancellation unless camera-relative floating-origin subtraction is applied).
4. **`TESObjectCELL::spinLock` (`+0x120`) + `cellState` (`+0x44`) Unload/Load Race:**
   - Background loader/unloader threads transition `cellState` through `1..6` while holding `+0x120`. Reading `loadedData` (`+0x130`) or `cellLand->loadedData` (`+0x70 -> +0x40`) while `cellState != 7` (`kAttached`) or while `spinLock.lockCount != 0` risks racing `LoadedLandData::Dtor` (`0x2B4AD0`).
5. **`BSShaderProperty::flags` (`+0x38`, 64-bit `SF1 | (SF2 << 32)`) Cross-Coupling:**
   - `BSLightingShaderProperty` slot 57 (`0x151CA80`) tests `flags & 0x8000` (`SLSF1_ZBuffer_Test` / `Tree_Anim`) and `flags & 0x04` (`SLSF1_Skinned`), while slot 61 (`0x151A0B0`) routes render passes based on `0x1000` (`SLSF1_Model_Space_Normals`) and `0x02` (`SLSF1_ZBuffer_Write`). Clearing/corrupting `ZBuffer_Write` (`SF2 bit 0`) or `Double_Sided` (`SF2 bit 4`) globally corrupts shadows, decals, and depth prepass ownership simultaneously.
