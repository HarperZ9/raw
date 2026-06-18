# RAW Architecture Reference

Copyright (c) 2026 Zain D. Harper (papacr0w). MIT-licensed — see `LICENSE`.

---

## Skyrim Engine Fundamentals

### Depth Convention
- Skyrim SE uses **standard D3D11 depth**: near=0, far=1
- HiZ CSCopy converts to **reversed-Z**: `1.0 - depth`
- All effect shaders receive reversed-Z from HiZ
- LinearDepth (t31) gives view-space Z in game units

### Render Phase Order
```
Unknown(0) → DepthPrepass(1) → ShadowMap(2) → GeometryMain(3)
→ Decals(4) → Sky(5) → AlphaBlend(6) → PostProcess(7) → UI(8)
```

### Projection Matrix (Row-Major)
```
[0]  = 1 / (aspect * tan(fov/2))
[5]  = 1 / tan(fov/2)
[10] = f / (f-n)
[11] = -(n*f) / (f-n)
[14] = 1.0
```

### Key Constants
```
near  = 15.0       (~21 cm)
far   = 353840.0   (~5 km)
gamma = 1.6        (Skyrim's gamma, not 2.2)
1 unit ~ 1.43 cm
```

---

## Mid-Frame Dispatch

1. PhaseDispatcher::OnPhaseChange fires from proxy callback
2. SceneMatrices::UpdateFromNiCamera() reads live camera
3. D3D11StateBackup saves full pipeline state (OM, RS, IA, VS, PS+CBs, CS)
4. OM targets unbound (prevents SRV/DSV hazard)
5. RenderPipeline::ExecuteStage runs all registered passes
6. State backup restored

---

## Rules for Effect Shaders

### Depth Reading
NEVER read game depth directly. Always read from HiZ (reversed-Z):
```cpp
auto& hiz = HiZPyramid::Get();
if (hiz.IsInitialized() && hiz.GetSRV())
    depthSRV = hiz.GetSRV();
```

### RT Format Guard
Any effect writing to ctx.gameSceneRTV MUST check the format first.
The phase detector sometimes fires on non-scene RTs (R8G8_UNORM temp textures).

### NDC to Pixel Conversion
```hlsl
// ProjMatrix[0][0] gives NDC. Multiply by half screen width for pixels:
float pixelRadius = worldRadius * Proj[0][0] / viewZ * float(ScreenDims.x) * 0.5;
```

### Scene Color Copy
CopyResource requires identical format+dimensions. Use lazy format matching:
check scene RT format at runtime, recreate copy texture on mismatch.

### Temporal Accumulation
- Clear history with ClearUnorderedAccessViewFloat (not ClearRenderTargetView)
- Skip history for first 3 frames (FrameIndex < 3)
- Temporal alpha 0.15-0.25 for acceptable convergence

### Sky Pixels at PostGeometry
Sky hasn't rendered yet. Effects writing to scene RTV must skip sky pixels
(depth < 0.0001 in reversed-Z) to prevent darkening the sky.

---

## SRV Slot Allocation

```
t17  LuminanceHistogram
t18  LUTManager
t19  HiZPyramid (reversed-Z depth pyramid)
t20  GTAORenderer (AO output)
t21  ClusteredLighting (cluster grid)
t22  TAAManager (temporal history)
t23  [available]
t24  [available]
t25  MaterialClassifier (material IDs)
t26  SSGIRenderer (GI output)
t27  SSRRenderer (reflections)
t28  ContactShadowRenderer (shadow mask)
t29  SkylightingRenderer (sky visibility)
t30  SharedGPUResources (blue noise)
t31  SharedGPUResources (linearized depth)
t32  IndirectSpecularRenderer (specular GI)
t33  VolumetricLightingRenderer (scatter + transmittance)
t34  DynamicCubemapRenderer (environment cubemap)
t35  ClusteredLighting (light buffer)
t36  ClusteredLighting (light index list)
t37  VolumetricClouds (cloud scatter + transmittance)
t38  AtmosphereRenderer (celestial bodies)
```

---

## Features

### ConfigManager (RAW.ini)
- Auto-saves 2 seconds after any change (debounced)
- Loads on startup, applies to all systems
- Path: `Data/SKSE/Plugins/RAW/RAW.ini`

### ShaderLoader (hot-reload)
- External `.hlsl` files override embedded strings
- Path: `Data/SKSE/Plugins/RAW/Shaders/`
- 85 shader files extracted, all editable without DLL recompilation
- Press F12 to invalidate cache and re-read from disk

### Presets
- Performance: AO (2 dirs/4 steps) + contact shadows only
- Quality: AO (4/8) + shadows + skylighting + SSR
- Ultra: Everything on (AO 6/12 + shadows + sky + SSR + SSGI)

### Tonemapping (12 operators)
AgX, ACES Fitted, Reinhard Extended, Hejl-Burgess, ACES Narkowicz,
AgX Punchy, PBR Neutral (Khronos), Uncharted 2, Lottes, Uchimura
(Gran Turismo), Tony McMapface, Linear

---

## Debug Controls

| Key | Function |
|-----|----------|
| INSERT | Toggle ImGui debug GUI |
| F12 | Hot-reload external shaders |
| F7 | Toggle mid-frame dispatch |
| F8 | Toggle compute passes |
| F9 | Toggle render passes |
| F10 | Frame capture (CSV) |
| F11 | GPU profiler overlay |

---

## File Map

### Infrastructure (keep)
| File | Purpose |
|------|---------|
| `main.cpp` | Plugin entry, init order, frame loop |
| `D3D11Hook.cpp/h` | Proxy connection, Present hook, ImGui |
| `D3D11StateBackup.h` | Full D3D11 state save/restore |
| `PhaseDispatcher.cpp/h` | Mid-frame dispatch from proxy callbacks |
| `RenderPipeline.cpp/h` | Pass orchestration + heartbeat |
| `RenderPassManager.cpp/h` | Fullscreen VS+PS infrastructure |
| `ComputeManager.cpp/h` | CS dispatch, state save/restore |
| `SceneData.cpp/h` | Camera matrices from NiCamera |
| `SharedGPUResources.cpp/h` | Linear depth (t31), blue noise (t30), vanilla params (b7) |
| `HiZPyramid.cpp/h` | Hierarchical depth, standard→reversed-Z |
| `ClusteredLighting.cpp/h` | Forward+ (2048 lights, 16x16x32) |
| `MaterialClassifier.cpp/h` | Material ID per pixel |
| `LuminanceHistogram.cpp/h` | 256-bin GPU histogram (t17) |
| `SRVInjector.cpp/h` | Shared SRV binding |
| `ShaderCache.cpp/h` | FNV-1a disk cache |
| `GPUProfiler.cpp/h` | Per-pass GPU timing |
| `FrameCapture.cpp/h` | Diagnostic capture + CSV |
| `DebugGUI.cpp/h` | ImGui overlay |

### Proxy (keep)
| File | Purpose |
|------|---------|
| `proxy_main.cpp` | DLL entry, ProxyInterface |
| `WrappedDevice.cpp/h` | ID3D11Device wrapper |
| `WrappedContext.cpp/h` | ID3D11DeviceContext wrapper |
| `WrappedSwapChain.cpp/h` | IDXGISwapChain wrapper, depth capture |
| `RenderPhaseDetector.cpp/h` | 9-phase heuristic classifier |
| `MaterialPipeline.cpp/h` | DXBC bytecode patching |
| `AlbedoExtractor.cpp/h` | G-buffer albedo extraction |
| `CBDirtyTracker.cpp/h` | CB upload optimization |
| `ProxyAPI.h` | Cross-DLL shared struct |

### Effects (28 renderers implemented, all from published papers)

| Renderer | Algorithm | Status |
|----------|-----------|--------|
| GTAORenderer | Jimenez 2019 bitmask SSGI | Compiles, spatial+temporal denoise, motion-vector reprojection |
| ContactShadowRenderer | Screen-space ray march | Compiles, spatial denoise |
| SSGIRenderer | Crassin 2011 voxel cone tracing (screen-space) | Compiles, 4-pass with temporal |
| SSRRenderer | McGuire & Mara 2014 Hi-Z march | Compiles, resolve+temporal with motion vectors |
| SkylightingRenderer | Screen-space hemisphere visibility | Compiles, spatial+temporal with motion vectors |
| BloomRenderer | Karis 2013 anti-firefly + Jimenez 2014 13-tap | Compiles, reversible Reinhard for HDR highlights |
| ToneMapManager | AgX/ACES/Reinhard + auto-exposure | Compiles |
| ColorPipeline | 12-stage mega-shader: film emulation, AgX, CDL, 3D LUT | Compiles |
| DoFRenderer | Physical thin-lens, N-gon bokeh | Compiles |
| SceneCompositor | AO/shadow/GI/SSR/cloud compositing | Compiles |
| + 18 more | Volumetric, SSS, clouds, atmosphere, TSR, frame gen, etc. | Compiles, deferred-init |

---

## Depth Acquisition (the actual mechanism) — added 2026-06-05 audit

> Keystone fact, previously undocumented. The game depth buffer is acquired
> **zero-copy via typeless format upgrade**, NOT via DSV ownership.

- `WrappedDevice::CreateTexture2D` (DepthIntercept) detects depth textures and upgrades
  the format to typeless (`D24_UNORM_S8_UINT -> R24G8_TYPELESS`,
  `D32_FLOAT -> R32_TYPELESS`) and adds `D3D11_BIND_SHADER_RESOURCE`.
- An SRV is created directly on the game's own depth texture and exposed as
  `ProxyInterface->gameDepthSRV`. No copy, no latency. This is the ReShade/ENB-standard
  approach and is the working path for all screen-space effects.

**Disabled dead logic:** the "depth ownership" path (`DepthOwnership.cpp`, DSV
substitution) is inert. `DepthOwn_SubstituteDSV` returns the game DSV unchanged
(`return gameDSV; // DISABLED`, line 170) because substitution produced wrong depth
values. Its call site in `WrappedContext::OMSetRenderTargets` is a live no-op. The file
is still compiled (CMakeLists line 156) but does nothing. Do not re-enable without first
understanding that failure mode.
