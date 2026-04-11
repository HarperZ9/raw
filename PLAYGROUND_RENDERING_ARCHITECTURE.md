# Playground Rendering Architecture
## ENB vs Community Shaders vs Playground — A Complete Technical Comparison

> **Playground** — "Where modders go to make their daydreams a reality"
> **Author:** Zain Dana Harper | **Version:** 1.0.0 | **Date:** 2026-03-14

---

## 1. The Three Approaches to Skyrim SE Rendering

Skyrim SE (Creation Engine, DirectX 11) has three major rendering modification approaches, each with fundamentally different architectures:

| | **ENB** (Boris Vorontsov) | **Community Shaders** | **Playground** |
|---|---|---|---|
| **Hook** | `IDXGISwapChain::Present` | `D3DCompile` IAT patch | Full `d3d11.dll` proxy |
| **Timing** | Post-composite (Present) | Shader replacement (inline) | Mid-frame dispatch (5 stages) |
| **Pipeline** | Sequential 9-stage post-process | Shader bytecode injection | Full pipeline ownership |
| **G-Buffer** | No access | Builds its own via shader replacement | Material pipeline + proxy API |
| **Game State** | None (only GPU data) | Via SKSE plugin | Full SKSE + NiCamera + RE::Sky |
| **Depth Access** | Backbuffer copy at Present | Inline via replaced shaders | Live depth via proxy-captured SRV |
| **Temporal** | Limited (RT reset per stage) | Persistent render targets | Persistent history with ping-pong |
| **Coexistence** | Cannot run with CS | Cannot run with ENB | Replaces both completely |

---

## 2. ENB Architecture (Boris Vorontsov)

### 2.1 Hook Layer

ENB intercepts `IDXGISwapChain::Present` via a custom `d3d11.dll` that proxies all D3D11 API calls. When the game calls Present, ENB runs its entire post-processing pipeline before the frame is actually presented.

**Timing limitation:** ENB can ONLY see the fully composited backbuffer. It cannot intercept mid-frame (during geometry, lighting, or sky rendering). Every effect runs on the final image.

### 2.2 Pipeline Stages

ENB runs 9 sequential shader stages in Present order:

```
Stage 1: enbeffectprepass.fx    — Raw backbuffer (R16G16B16A16_FLOAT HDR)
Stage 2: enbbloom.fx            — Bloom generation (8 sub-passes)
Stage 3: enbadaptation.fx       — Luminance adaptation (downsample + histogram)
Stage 4: enblens.fx             — Lens effects (configurable passes)
Stage 5: enbeffect.fx           — Main compositing, tonemapping, color grading
Stage 6: Custom technique shaders — Community addon effects
Stage 7: enbeffectpostpass.fx   — Post-processing (FORMAT DROPS TO R10G10B10A2_UNORM!)
Stage 8: enbsunsprite.fx        — Procedural sun rendering
Stage 9: enbunderwater.fx       — Underwater effects
```

**Critical limitation:** At stage 7, the render target format drops from HDR float to `R10G10B10A2_UNORM` (10-bit color + 2-bit alpha). Any shader running after this point loses HDR precision and has severely limited alpha channel.

### 2.3 Available Data

ENB shaders receive:
- **Backbuffer** — the fully rendered scene (already lit, shaded, post-processed by the game)
- **Depth buffer** — as a shader resource view (raw D3D11 depth)
- **Matrices** — ViewProjection, InverseVP, View, InverseView, Projection, InverseProjection, PreviousVP
- **Timer** — game time (`.x`=current, `.y`=elapsed, `.z`=frame time, `.w`=smoothed delta)
- **ScreenSize** — resolution + reciprocal
- **SunDirection** — world-space sun vector
- **TimeOfDay** — 6-phase time factor
- **WeatherAndTime** — weather transition data

ENB **cannot** access:
- Game state (player health, combat, inventory, active spells)
- Material classification (what type of surface is at each pixel)
- Per-object properties (is this skin? metal? foliage?)
- Normal maps in screen space (only reconstructed from depth)
- The game's internal render targets before compositing

### 2.4 ENB Shader Author Ecosystem

Six key authors have defined the ENB shader landscape:

| Author | Signature | Philosophy |
|--------|-----------|------------|
| **Kitsuune** | Hi-Z SSR, per-weather LUTs, DSRS, bilateral denoise | Per-weather customization, infrastructure-first |
| **MartyMcModding** | iMMERSE (MXAO, RTGI, FFT Bloom, optical flow) | Research-grade algorithms, mathematical rigor |
| **TreyM** | Film stock emulation, LOG-space tonemapping, anamorphic cinema | Cinematic authenticity via film science |
| **JawZ** | MSL library, modular architecture, DNI separation | Community accessibility, reusable building blocks |
| **l00ping** | NAT system, per-weather rendering, atmospheric authenticity | Weather as the rendering foundation |
| **Adyss** | Screen-space god rays, modular addons, ReShade porting | Bridging effect ecosystems |

### 2.5 ENB Limitations

1. **Post-composite only** — cannot modify lighting, shadows, or materials during rendering
2. **No material awareness** — cannot distinguish skin from metal from foliage
3. **No game state** — cannot react to combat, weather transitions, location
4. **Sequential pipeline** — effects run one after another, no parallel dispatch
5. **LDR format drop** — stage 7+ loses HDR precision
6. **No temporal persistence across stages** — render targets reset between stages
7. **2-bit alpha in post-pass** — severely limits alpha-dependent techniques

---

## 3. Community Shaders Architecture

### 3.1 Hook Layer

Community Shaders hooks `D3DCompile` via IAT patching. When the game compiles its internal shaders at startup, CS intercepts the compilation and replaces the HLSL source with modified versions that include additional features (extra render targets, modified lighting equations, etc.).

**Key advantage:** CS runs INLINE with the game's own rendering. Its modified shaders execute during the actual geometry/lighting passes, not as a post-process.

### 3.2 Capabilities

- **True deferred rendering** — can redirect game render targets to build a G-buffer
- **Material classification** — modified pixel shaders can output material IDs
- **Normal maps in screen space** — captured during material rendering
- **Temporal persistence** — creates its own render targets that persist across frames
- **Full shader control** — replaces every game shader with custom versions

### 3.3 Limitations

1. **Cannot run alongside ENB** — both modify the rendering pipeline incompatibly
2. **No Present hook** — cannot inject post-process stages
3. **Shader replacement complexity** — must maintain compatibility with every game shader variant
4. **No mid-frame dispatch** — effects fire as part of shader execution, not at controlled pipeline points
5. **Limited game state access** — only what SKSE provides, no direct NiCamera/RE::Sky reads

---

## 4. Playground Architecture

### 4.1 Design Philosophy

Playground takes **full pipeline ownership**. Rather than hooking one API call (ENB) or replacing shaders (CS), it wraps the entire D3D11 API surface. This gives it visibility into every draw call, state change, and resource operation the game performs.

### 4.2 Hook Layer: d3d11.dll Proxy

Playground ships a custom `d3d11.dll` that wraps:
- `ID3D11Device` — intercepts texture/buffer/shader creation
- `ID3D11DeviceContext` — intercepts all draw calls, state changes, resource operations
- `IDXGISwapChain` (through SwapChain4) — intercepts Present, resize, format changes

The proxy identifies the game's depth buffer by intercepting `ClearDepthStencilView` and creates a direct SRV on the live depth texture (no copy needed).

### 4.3 RenderPhaseDetector

The proxy classifies the game's rendering into 9 phases using heuristic analysis of draw call patterns, render target changes, and shader signatures:

```
Unknown(0) → DepthPrepass(1) → ShadowMap(2) → GeometryMain(3)
→ Decals(4) → Sky(5) → AlphaBlend(6) → PostProcess(7) → UI(8)
```

Phase transitions fire callbacks to the SKSE plugin, enabling **mid-frame dispatch** — running custom effects at precise points during the game's rendering pipeline.

### 4.4 Mid-Frame Dispatch Pipeline

Unlike ENB (post-composite) or CS (inline with shaders), Playground dispatches effects at 5 controlled points:

```
PostDepthPrepass  — After depth-only rendering; Hi-Z pyramid available
PostGeometry      — After opaque geometry; depth + scene color valid
PostSky           — After sky dome; clouds/atmosphere inject here
PreUI             — Before HUD; final chance for scene modification
PrePresent        — Inside Present; debug overlays only
```

**PostGeometry** is the primary dispatch point. At this stage:
- The depth buffer contains all opaque geometry
- The scene render target has fully lit, shaded opaque surfaces
- Sky has NOT been rendered yet (important for compositor behavior)
- Alpha-blended effects (particles, decals) have NOT been rendered

### 4.5 State Management

Each mid-frame dispatch:
1. Saves the complete D3D11 pipeline state (OM, RS, IA, VS, PS CBs/SRVs/samplers, CS)
2. Unbinds OM targets (prevents SRV/DSV read-write hazards)
3. Updates SceneMatrices from NiCamera (live camera data)
4. Executes all registered pipeline passes in priority order
5. Restores the complete pipeline state

This ensures the game's rendering is not corrupted by effect execution.

### 4.6 Depth Handling

**Skyrim uses standard D3D11 depth (near=0, far=1).** All effect shaders expect reversed-Z (near=1, far=0). The HiZ CSCopy shader converts at the source:

```hlsl
DstMip[DTid.xy] = 1.0 - SrcDepth[DTid.xy];
```

SharedGPUResources then linearizes the reversed-Z depth into view-space Z at register `t31`, available to all effects.

---

## 5. Rendering Technique Comparison

### 5.1 Ambient Occlusion

| Technique | ENB | CS | Playground |
|-----------|-----|-----|------------|
| **SSAO** | enbeffectprepass.fx (basic) | XeGTAO | VB-SSGI (visibility bitmask) |
| **Multi-bounce** | Not available | Approximation | YCoCg SH2 bounce gathering |
| **Material-aware** | No | Yes (GBuffer) | Yes (MaterialClassifier) |
| **Temporal** | Per-frame only | Persistent history | Ping-pong with FrameIndex bypass |

**Playground approach:** Visibility Bitmask SSGI (VB-SSGI) based on Activision/Jimenez 2019. 32-bit bitmask encodes visibility of each elevation slice. Enables multi-bounce AO + short-range indirect bounce with YCoCg encoding for luminance-aware denoising.

### 5.2 Global Illumination

| Technique | ENB | CS | Playground |
|-----------|-----|-----|------------|
| **GI Method** | Indirect light (very subtle additive) | SSGI via screen-space rays | Voxel SH2 cone tracing |
| **Resolution** | Full-res post-process | Half-res | 128^3 voxel grid, half-res output |
| **Color space** | RGB | RGB | YCoCg (better luminance denoising) |
| **Chroma** | N/A | N/A | Subsampled CoCg at 64^3 |

**Playground approach:** 128^3 voxel grid stores scene radiance as SH2 (spherical harmonics L0+L1). 4-8 hemisphere rays per half-res pixel. Temporal accumulation with confidence-based rejection.

### 5.3 Screen-Space Reflections

| Technique | ENB | CS | Playground |
|-----------|-----|-----|------------|
| **Method** | Not available (Boris cannot do SSR) | Hi-Z tracing | View-space ray march with mip refinement |
| **Resolution** | N/A | Full-res | Half-res with bilateral upsample |
| **Temporal** | N/A | Reprojection | Confidence-weighted accumulation |
| **Depth source** | N/A | Inline | HiZ pyramid (reversed-Z) |

**Playground approach:** View-space ray march using linearized depth at t31. Anti-self-intersection via large initial offset + minimum travel distance. Half-res output with temporal denoise.

### 5.4 Contact Shadows

| Technique | ENB | CS | Playground |
|-----------|-----|-----|------------|
| **Method** | Not available | Screen-space ray march | Sun-direction ray march |
| **Direction** | N/A | Sun-aware | World-to-view sun transform |
| **Steps** | N/A | 16-32 | 16 (configurable) |
| **Denoise** | N/A | Spatial | Bilateral spatial (5x5) |

### 5.5 Bloom

| Technique | ENB | CS | Playground |
|-----------|-----|-----|------------|
| **Method** | enbbloom.fx (8 stages) | N/A | Dual Kawase + Karis anti-firefly |
| **HDR** | Yes (R16G16B16A16_FLOAT) | N/A | Yes (full HDR chain) |
| **Threshold** | Boris-calibrated | N/A | Soft threshold with knee |
| **Spectral** | No | N/A | Per-mip tinting available |

### 5.6 Tonemapping

| Technique | ENB | CS | Playground |
|-----------|-----|-----|------------|
| **Curves** | Hejl, Reinhard, custom | N/A | AgX, ACES Hill, Reinhard, Hejl, Linear |
| **Auto-exposure** | enbadaptation.fx histogram | N/A | 256-bin GPU histogram at t17 |
| **Gamma** | Game default (2.2) | Game default | Skyrim 1.6 correction |
| **HDR output** | Limited | N/A | PQ ST.2084 BT.2020 ready |

### 5.7 Depth of Field

| Technique | ENB | CS | Playground |
|-----------|-----|-----|------------|
| **Method** | enbeffect.fx (various presets) | N/A | Physical thin-lens (ring bokeh, golden spiral) |
| **Autofocus** | Center-pixel | N/A | Depth grid sampling |
| **Bokeh** | Disc/hex per preset | N/A | Ring-based, anamorphic, chromatic |
| **Resolution** | Full-res | N/A | Full-res with CoC-based gathering |

### 5.8 Color Grading

| Technique | ENB | CS | Playground |
|-----------|-----|-----|------------|
| **Pipeline** | enbeffect.fx (per-preset) | N/A | 12-stage mega-shader |
| **LUT** | External .png LUT support | N/A | 64^3 tetrahedral interpolation at t18 |
| **Per-weather** | Via weather .ini files | N/A | WeatherSeparationEngine |
| **Film emulation** | TreyM's film stock shaders | N/A | Film curves, grain, halation |

---

## 6. What Playground Can Do That Neither ENB Nor CS Can

### 6.1 Mid-Frame Scene Access

ENB only sees the final composited backbuffer. CS modifies shaders but can't dispatch custom compute passes at controlled points. Playground dispatches at 5 mid-frame stages with live depth, scene color, and camera data.

### 6.2 Material-Aware Post-Processing

The d3d11 proxy's MaterialPipeline patches game shaders via DXBC bytecode modification to output to a 3-target G-buffer. Combined with MaterialClassifier, effects can distinguish skin from metal from foliage — enabling material-specific AO, SSS, and reflection behavior.

### 6.3 Game State Integration

Via SKSE plugin + CommonLibSSE-NG, Playground reads:
- `RE::Main::WorldRootCamera()` — live NiCamera for view/projection matrices
- `RE::Sky::GetSingleton()` — sun direction, weather data
- `RE::PlayerCamera::GetSingleton()` — FOV, camera mode
- Player state (health, combat, location, race)

This enables combat-reactive effects, location-aware volumetrics, and weather-driven rendering.

### 6.4 Clustered Forward+ Lighting

2048 dynamic lights organized in a 16x16x32 cluster grid with temporal caching and log-depth slicing. No other Skyrim rendering system provides GPU-accelerated many-light support.

### 6.5 Full Temporal Pipeline

Persistent render targets with ping-pong history buffers, confidence-based rejection, and frame-index-aware initialization. Effects accumulate data across frames for noise reduction without ghosting artifacts.

---

## 7. Rendering Technique Reference

### 7.1 Algorithms Used

| Category | Algorithm | Reference |
|----------|-----------|-----------|
| AO | Visibility Bitmask SSGI | Activision / Jimenez et al. 2019 |
| GI | Voxel SH2 Cone Tracing | Custom (YCoCg + CoCg subsampling) |
| SSR | View-Space Ray March | McGuire & Mara (adapted) |
| Shadows | Screen-Space Ray March | Standard contact shadow technique |
| Skylighting | Hemisphere Probe Grid | 128x128x64 3D voxel, 8-direction |
| Bloom | Dual Kawase + Karis | Kawase 2015, Karis (UE4) |
| Tonemapping | AgX / ACES / Hejl | Hill (Gran Turismo), Hejl-Burgess |
| DoF | Physical Thin-Lens | Ring bokeh, golden spiral sampling |
| Color | 12-Stage Pipeline | Split toning, LUT, film curves |
| TAA | AABB Clip + Bicubic History | Salvi variance clipping |
| Clouds | Procedural Volumetric | Ray march through noise layers |
| Lighting | Clustered Forward+ | Olsson et al. 2012 (adapted) |
| Noise | R2 Quasi-Random Blue Noise | Roberts 2018 |
| Encoding | YCoCg SH2 | Lossless luminance-chrominance split |
| Denoise | Bilateral + Temporal | Depth-aware edge stopping |

### 7.2 Key Quality Patterns (from ENB/CS Research)

- **Karis anti-firefly**: Weight 2x2 blocks by `1/(1+luma)` on first bloom downsample
- **Skyrim gamma 1.6**: The game's gamma is 1.6, not 2.2. Linearize accordingly.
- **First-person depth threshold**: 16.0 view-space units (between ENB's 11.76 and CS's 18.0)
- **Reinhard soft-clamp**: `clampedLuma = luma / (1 + luma / maxAdd)` — NOT `compressed / luma * maxAdd`
- **Blue noise jitter**: `frac(blueNoise + frameIndex * 0.6180339887)` (golden ratio temporal offset)
- **Energy-conservative volumetrics**: Accumulate inscatter BEFORE updating transmittance

---

## 8. Pipeline Execution Order

### PostGeometry (mid-frame, priority order)
```
 1  HiZPyramid           — Depth pyramid (standard→reversed-Z conversion)
 2  SharedGPUResources    — Linear depth (t31) + blue noise (t30) + vanilla params (b7)
15  GTAO/VB-SSGI          — AO + bounce (t20, YCoCg)
16  ContactShadows        — Shadow mask (t28)
17  Skylighting           — Sky visibility (t29)
18  GrassLighting         — Backbuffer modification
19  TreeLODLighting       — Backbuffer modification
20  SSGI                  — Voxel GI (t26, YCoCg)
21  IndirectSpecular      — Specular GI (t32)
22  ScreenSpaceDecals     — Backbuffer modification
23  SubsurfaceScattering  — Backbuffer modification
24  WaterBlending         — Backbuffer modification
25  SSR                   — Reflections (t27, half-res)
26  DynamicCubemap        — Environment map (t30)
28  ParticleLighting      — Emissive scatter
80  VolumetricClouds      — Cloud composite (sky-pixel masked)
90  SceneCompositor       — Composites AO/GI/SSR/shadows/skylighting onto scene
```

### PreUI (after scene, before HUD)
```
 5  UnderwaterRenderer    — Beer-Lambert absorption, caustics (conditional)
10  Bloom                 — Dual Kawase bloom generation
20  LensEffects           — Flares, CA, vignette, distortion
30  DoF                   — Physical thin-lens depth of field
50  ColorPipeline         — 12-stage color grading
100 ToneMapping           — Auto-exposure + tone curves (AgX/ACES/Hejl)
```

### PrePresent (debug only)
```
900 FrameGenerator        — DLSS 3-style frame synthesis
```

---

## 9. SRV Slot Allocation

```
t17  LuminanceHistogram      256-bin GPU histogram
t18  LUTManager              64^3 tetrahedral film LUT
t19  HiZPyramid              Reversed-Z depth pyramid (12 mips)
t20  GTAORenderer            AO + bounce (YCoCg, .a=AO)
t21  ClusteredLighting       Cluster grid (16x16x32)
t22  TAAManager              Temporal history
t23  AtmosphereRenderer      Transmittance LUT
t24  AtmosphereRenderer      Scattering LUT
t25  AtmosphereRenderer      Celestial overlay / MaterialClassifier
t26  SSGIRenderer            GI output (YCoCg)
t27  SSRRenderer             Reflections (half-res, .a=confidence)
t28  ContactShadowRenderer   Shadow mask (1=lit, 0=shadowed)
t29  SkylightingRenderer     Sky visibility (0=occluded, 1=open)
t30  SharedGPUResources      Blue noise (128x128, 4 channels, R2 quasi-random)
t31  SharedGPUResources      Linearized depth (R32_FLOAT, view-space Z)
t32  IndirectSpecularRenderer Specular GI
t33  VolumetricLightingRenderer Scatter + transmittance
```

---

## 10. Debug & Development

### Hotkeys
| Key | Function |
|-----|----------|
| INSERT | Toggle ImGui debug GUI (enable/disable all effects) |
| F7 | Toggle mid-frame dispatch |
| F8 | Toggle compute passes |
| F9 | Toggle render passes |
| F10 | Frame capture (CSV export) |
| F11 | GPU profiler overlay |

### Heartbeat
Every ~30 seconds:
```
Pipeline[f1800]: PostGeo=8/8 PreUI=3/6 PrePresent=4/4 | GPU=2.3ms (hot: VB-SSGI 0.8ms)
```

### Log Files
- Plugin: `Documents/My Games/Skyrim.INI/SKSE/Playground.log`
- Proxy: `[game dir]/overwrite/Root/d3d11_proxy.log`
- Shader errors: `Data/SKSE/Plugins/Playground_ShaderErrors.log`

---

*This document describes Playground's rendering architecture as of v1.0.0 (2026-03-14). It is intended as a standalone reference for developers working on the pipeline.*
