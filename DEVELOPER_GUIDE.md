# Playground Developer Guide

> For bug-fixing and contribution reference.
> Read `RENDERING_PIPELINE_REFERENCE.md` first — it has all architectural rules.

---

## Build Instructions

### Prerequisites
- Visual Studio 2022 (v18+)
- CMake 3.24+
- vcpkg at `C:/vcpkg` with triplet `x64-windows-static` (/MT)
- CommonLibSSE-NG (pulled via vcpkg)

### Build
```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release --target Playground --parallel
cmake --build build --config Release --target d3d11_proxy --parallel
```

### Output
- `build/Release/Playground.dll` — SKSE plugin
- `build/Release/d3d11.dll` — D3D11 proxy

### Deploy (MO2)
```
E:\Modlists\SkyGroundChronicles\mods\Playground\
  SKSE/plugins/Playground.dll
  SKSE/plugins/Playground/ShaderCache/    (auto-generated)
  SKSE/plugins/Playground/WeatherParams.ini
  SKSE/plugins/Playground/FeedbackConfig.ini
  SKSE/plugins/Playground/WriteBackConfig.ini
  ROOT/d3d11.dll
```

### Ignore `enb_gtav` target
This is a legacy ENB target with 36 unresolved externals. Only build `Playground` and `d3d11_proxy`.

---

## Architecture Overview

```
Game (SkyrimSE.exe)
  |
  |-- loads d3d11.dll (our proxy, intercepts all D3D11 API calls)
  |     |-- WrappedDevice / WrappedContext / WrappedSwapChain
  |     |-- RenderPhaseDetector (classifies 9 render phases via heuristics)
  |     |-- Fires OnPhaseChange callbacks to SKSE plugin
  |     |-- ProxyAPI.h: shared struct for cross-DLL communication
  |
  |-- loads Playground.dll (SKSE plugin, all rendering effects)
        |-- PhaseDispatcher: receives phase callbacks, dispatches pipeline stages
        |-- RenderPipeline: orchestrates all passes per stage
        |-- 22+ effect renderers (compute shaders + fullscreen PS passes)
        |-- SceneCompositor: composites PostGeometry effects onto scene
        |-- DebugGUI: ImGui overlay (INSERT key)
```

### Frame Lifecycle
```
1. Game starts frame
2. DepthPrepass phase → PostDepthPrepass dispatch (HiZ could run here)
3. GeometryMain phase → opaque geometry rendered
4. Phase transition 3→7 → PostGeometry dispatch:
   - HiZPyramid (pri 1): copies depth, converts standard→reversed-Z
   - SharedGPUResources (pri 2): linearizes depth to t31
   - Effects (pri 15-28): GTAO, shadows, skylighting, SSGI, SSR, etc.
   - SceneCompositor (pri 90): composites all effects onto scene RT
5. Sky, AlphaBlend, PostProcess phases
6. Phase transition 7→8 → PreUI dispatch:
   - Bloom, Lens, DoF, ColorPipeline, ToneMapping
7. UI phase
8. Present → PrePresent dispatch:
   - FrameGenerator, debug overlays, ImGui
```

---

## Known Issues & Current Status

### Working Effects (PostGeometry)
- **GTAO/VB-SSGI**: AO + bounce light. Temporal fixed (FrameIndex bypass).
- **Contact Shadows**: Sun-direction ray-marched. No temporal (spatial denoise only).
- **Skylighting**: Upper-hemisphere probe grid. Temporal fixed.
- **SSGI**: Voxel SH2 GI. History clear bug fixed. Intensity reduced (1.0→0.25).
- **SSR**: Self-intersection fixed (larger initial offset + minTravel). Temporal fixed.

### Effects Needing Investigation
- **SSR black squares**: Ray march may miss thin geometry. Hi-Z mip refinement logic doesn't actually sample Hi-Z mips (always reads LinearDepth at full-res). Consider implementing proper Hi-Z traversal.
- **VolumetricClouds**: Sky pixel mask added. Needs real-world testing with the sky fix.
- **GrassLighting / TreeLODLighting / WaterBlending / ParticleLighting / SSS / Decals**: All guarded and ready but not yet tested by user.

### PreUI Effects (All Audited Clean)
- Bloom, DoF, LensEffects, ColorPipeline, ToneMapping — ready to enable via DebugGUI.
- All handle scene RT format mismatches with lazy recreation.

### Architectural Fixes Applied (2026-03-14)
1. **Depth convention**: Skyrim uses standard depth (near=0, far=1). HiZ CSCopy converts to reversed-Z for all shaders.
2. **Pipeline init order**: RenderPipeline::Initialize must run before HiZ/SharedGPUResources AddPass.
3. **RT format guard**: All backbuffer-writing effects check scene RT format before writing.
4. **D3D11StateBackup**: Now saves/restores PS constant buffers (8 slots) and VS constant buffers (4 slots).
5. **Temporal ghosting**: All 4 temporal effects skip history for first 3 frames, increased alpha for faster convergence.
6. **SSGI history clear**: Was `ClearRenderTargetView(nullptr,...)` (no-op). Fixed to `ClearUnorderedAccessViewFloat`.
7. **NDC→pixel conversion**: `screenRadius * ScreenDims.x * 0.5` in GTAO and Skylighting.
8. **SRV slot deconfliction**: SSR t21→t27, VolumetricLighting t31→t33.
9. **Priority deconfliction**: No duplicate priorities in PostGeometry.
10. **Reinhard soft-clamp**: Fixed formula in SceneCompositor GI blend.

---

## Key Files

### Core Infrastructure
| File | Purpose |
|------|---------|
| `main.cpp` | Plugin entry, system initialization order, frame loop |
| `D3D11Hook.cpp` | Proxy connection, Present hook, ImGui init |
| `PhaseDispatcher.cpp` | Mid-frame dispatch, SceneMatrices update, state backup |
| `RenderPipeline.cpp` | Pass orchestration, heartbeat logging |
| `D3D11StateBackup.h` | Full pipeline state save/restore (OM, RS, IA, VS, PS, CS) |
| `SceneData.cpp` | Camera matrices from NiCamera, sun direction from RE::Sky |
| `SharedGPUResources.cpp` | Blue noise (t30), linear depth (t31), vanilla params CB (b7) |
| `HiZPyramid.cpp` | Hierarchical depth buffer, standard→reversed-Z conversion |
| `SceneCompositor.cpp` | Fullscreen composite (AO, GI, SSR, shadows, skylighting) |

### Effect Renderers
| File | Stage:Pri | Output |
|------|-----------|--------|
| `GTAORenderer.cpp` | PostGeo:15 | t20 (AO+bounce, YCoCg) |
| `ContactShadowRenderer.cpp` | PostGeo:16 | t28 (shadow mask) |
| `SkylightingRenderer.cpp` | PostGeo:17 | t29 (sky visibility) |
| `SSGIRenderer.cpp` | PostGeo:20 | t26 (GI, YCoCg) |
| `SSRRenderer.cpp` | PostGeo:25 | t27 (reflections) |
| `VolumetricClouds.cpp` | PostGeo:80 | Scene RT composite |
| `BloomRenderer.cpp` | PreUI:10 | Bloom texture |
| `DoFRenderer.cpp` | PreUI:30 | Scene RT composite |
| `ColorPipeline.cpp` | PreUI:50 | Scene RT composite |
| `ToneMapManager.cpp` | PreUI:100 | Scene RT composite |

### Proxy (d3d11_proxy/)
| File | Purpose |
|------|---------|
| `proxy_main.cpp` | DLL entry, ProxyInterface setup |
| `WrappedDevice.cpp` | ID3D11Device wrapper |
| `WrappedContext.cpp` | ID3D11DeviceContext wrapper, state cache |
| `WrappedSwapChain.cpp` | IDXGISwapChain wrapper, depth capture |
| `RenderPhaseDetector.cpp` | 9-phase heuristic classifier |
| `ProxyAPI.h` | Shared struct (must match D3D11Hook.cpp mirror) |

---

## Debugging

### Keys
| Key | Function |
|-----|----------|
| INSERT | Toggle ImGui debug GUI |
| F7 | Toggle mid-frame dispatch (disables all effects) |
| F8 | Toggle compute passes |
| F9 | Toggle render passes |
| F10 | Toggle frame capture (CSV export) |
| F11 | Toggle GPU profiler overlay |

### Log Files
- SKSE plugin: `Documents/My Games/Skyrim.INI/SKSE/Playground.log`
- Proxy: `[game dir]/overwrite/Root/d3d11_proxy.log`
- Shader errors: `[game dir]/Data/SKSE/Plugins/Playground_ShaderErrors.log`
- Frame capture: `[game dir]/SKSE/Plugins/Playground/FrameCapture.csv`

### Heartbeat
Every ~30s the log outputs:
```
Pipeline[f1800]: PostGeo=8/8 PreUI=3/6 PrePresent=4/4 | GPU=2.3ms (hot: VB-SSGI 0.8ms)
```
Shows enabled/registered pass counts per stage + total GPU time + hottest pass.

### Common Debug Patterns
- **Effect not visible**: Check `SceneCompositor[fN]` log — is `en=true` and `srv=` non-null?
- **Black smearing**: Missing RT format guard. Check `skipping non-scene RT` warnings.
- **Black sky**: D3D11StateBackup not restoring PS/VS constant buffers. Fixed in current build.
- **Ghosting**: Temporal history uninitialized or alpha too low. Check `FrameIndex < 3` bypass.
- **Silent fail**: `CopyResource` format mismatch. Check for `scene RT format/size changed` log.

---

## SRV Slot Map

```
t17  LuminanceHistogram     (256-bin histogram)
t18  LUTManager             (3D film LUT)
t19  HiZPyramid             (reversed-Z depth pyramid)
t20  GTAORenderer           (AO + bounce)
t21  ClusteredLighting      (cluster grid)
t22  TAAManager             (temporal history)
t23  AtmosphereRenderer     (transmittance LUT)
t24  AtmosphereRenderer     (scattering LUT)
t25  AtmosphereRenderer     (celestial) / MaterialClassifier
t26  SSGIRenderer           (GI output)
t27  SSRRenderer            (reflections)
t28  ContactShadowRenderer  (shadow mask)
t29  SkylightingRenderer    (sky visibility)
t30  SharedGPUResources     (blue noise 128x128)
t31  SharedGPUResources     (linearized depth R32_FLOAT)
t32  IndirectSpecularRenderer (specular GI)
t33  VolumetricLightingRenderer (scatter + transmittance)
```
