# Playground v3.0

**Rendering platform for Skyrim's Creation Engine.**

Playground is an SKSE64 plugin that reads Skyrim engine state via CommonLibSSE-NG, pushes it to ENB shaders as ~150 float4 parameters, and provides a full GPU rendering pipeline with compute-based post-processing, lighting, and diagnostics. Designed for universal compatibility from vanilla SE+SKSE to 3000-mod Wabbajack lists.

## What It Does

### Data Bridge (24 Domains, ~150 float4s)
- **Game state tracking** — Celestial, atmosphere, fog, weather, player, camera, interior lighting, shadows, effects, render state, image space, lights, actor values, crosshair, equipment, quests, UI state, regions, audio, NPC detection, performance, scene composition, theme
- **Dirty tracking** — Per-float4 memcmp skips unchanged params (~200 ENBSetParameter calls/frame instead of ~1100)
- **GPU feedback loop** — 1-frame-delay backbuffer readback, 5x5 Gaussian sampling, 4-bin luminance histogram, temporal scene analysis

### Rendering Pipeline (11 Shader Systems)
- **Tone mapping** — HDR autoexposure with AgX/ACES/PQ curves
- **Atmosphere** — Physically-based Rayleigh+Mie scattering LUTs
- **Material classification** — Per-pixel material ID buffer
- **GTAO** — Ground Truth Ambient Occlusion (Jimenez 2016, cosine-weighted horizon search)
- **SSR** — Screen-space reflections via Hi-Z accelerated ray march (Stachowiak 2015)
- **SSGI** — Screen-space global illumination
- **SDSM** — Sample Distribution Shadow Maps (depth histogram cascade optimization)
- **Volumetric clouds** — Ray-marched cloud rendering
- **Frame generation** — Temporal frame interpolation
- **Temporal super resolution** — Compute-based upscaling with motion vectors
- **Clustered lighting** — Forward+ light culling (Light Limit Fix replacement)

### GPU Compute Infrastructure
- **ComputeManager** — Shader compilation, dispatch, state save/restore
- **SRVInjector** — Binds compute outputs at t17-t26 for ENB shader consumption
- **LuminanceHistogram** — 256-bin GPU-parallel histogram (t17)
- **HiZPyramid** — Hierarchical depth buffer for SSR/SSAO ray marching (t19)
- **LUTManager** — 64^3 Texture3D film color grading (t18)
- **TAAManager** — Compute-based temporal anti-aliasing (t22)
- **RenderPipeline** — 3-stage pass orchestration (PreENB/PostENB/PrePresent)

### Backend Systems
- **Shader bytecode cache** — FNV-1a hash disk cache, eliminates 5-15s ENB startup compilation
- **ENB Extender replacement** — Annotation parser, weather separation, extern binding, parameter binding, native ATB GUI
- **Scene observer** — BSShader vtable hooks for per-draw material properties
- **Write-back processor** — INI-driven game state modification (FOV, fog, sunlight, actor values)
- **Shared memory bridge** — Memory-mapped file for external apps (OBS, LED sync, companion tools)
- **Papyrus bridge** — Native functions for mod script authors

### D3D11 Proxy (Optional, Separate DLL)
- Full D3D11 API wrapper with Draw/RT/Shader/Present hooks
- HDR swap chain (R16G16B16A16_FLOAT + scRGB)
- Constant buffer dirty tracking (skip redundant GPU uploads)
- State cache redundancy filtering (SRV, blend, depth-stencil, rasterizer)
- GPU occlusion culling infrastructure
- Render phase detection (9 phases: depth prepass through UI)
- Material pipeline (G-buffer extraction for deferred rendering)

### Diagnostics
- **SystemHealth** — Per-system red/yellow/green monitoring across 53 systems in 8 categories
- **CompatibilityProbe** — Runtime conflict detection (DLL fingerprinting, overlay detection, SRV slot analysis, hook chain verification)
- **ProxyDiagnostics** — Bridges all proxy-side stats (CB tracking, state cache, occlusion, HDR, phases) to the debug GUI
- **DebugGUI** — 15-tab ImGui overlay (INSERT key): Domains, Game Editor, Subsystems, Raw Params, Annotations, Param Editor, Object Window, ENB GUI, Weather Editor, Weather Analyzer, Shader Viewer, EditorID XRef, Light Inspector, Debug Viz, Diagnostics

## Compatibility

Playground is architecturally designed for universal mod compatibility. See [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) for the full compatibility guarantee.

**Key points:**
- All 22 trackers are **read-only** — no memory patches, no form edits, no record injection
- **Self-healing** — trackers auto-disable after 5 errors, retry every ~5 seconds
- **SEH protection** — access violations caught and recovered from automatically
- **NaN/Inf sanitization** — corrupt floats from any source are clamped before ENB push
- **Automatic feature negotiation** — defers to Community Shaders, ENBParmLink, NativeEditorID Fix when detected
- **Loading screen awareness** — cell-dependent trackers pause during transitions

**Compatible with:** ENBSeries, Community Shaders, ENB ParmLink, NativeEditorID Fix, po3_Tweaks, SSE Engine Fixes, Address Library, Steam/Discord/RTSS overlays, all Wabbajack modlists.

**Conflict detection for:** ReShade (warning), Special K (warning), DXVK (error — compute shaders may not work).

## Requirements

| Requirement | Version |
|---|---|
| Skyrim SE/AE | 1.5.97+ / 1.6.x |
| SKSE64 | 2.0.x+ |
| Address Library for SKSE | SE or AE build |
| ENBSeries for Skyrim SE | v0.4xx+ (SDK 1000+) |
| Visual C++ Redistributable | 2022 |

## Installation

### For Users (MO2/Vortex)

1. Install as a mod with this structure:
```
SKSE/plugins/Playground.dll
SKSE/plugins/Playground/FeedbackConfig.ini
SKSE/plugins/Playground/WriteBackConfig.ini
SKSE/plugins/Playground/WeatherParams.ini
```

2. Place ENB shaders in `enbseries/` alongside your ENB preset.

3. (Optional) Place `Playground_ENB.dllplugin` in your Skyrim root next to `d3d11.dll` for the external ENB plugin.

4. (Optional) The d3d11 proxy is a separate DLL. Only install it if you want HDR output, CB dirty tracking, or the deferred rendering pipeline. It replaces ENB's d3d11.dll and cannot coexist with ENB's wrapper.

### In-Game Controls

| Key | Action |
|---|---|
| INSERT | Toggle debug GUI overlay (15 tabs) |
| F10 | Toggle shader compilation overlay |
| F11 | Clear shader error list |

## Building from Source

### Prerequisites

- Visual Studio 2022 (v143 toolset)
- CMake 3.24+
- [vcpkg](https://github.com/microsoft/vcpkg) at `C:/vcpkg`

### Build

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release --parallel
```

### Build Outputs

| Target | Output | Description |
|---|---|---|
| Playground | `Playground.dll` (1.1 MB) | Main SKSE plugin |
| Playground_ENB | `Playground_ENB.dllplugin` | Standalone ENB plugin (no SKSE) |
| enbhelperse | `enbhelperse.dll` | ENB Helper SE replacement |
| d3d11_proxy | `d3d11.dll` (186 KB) | D3D11 API proxy (optional) |
| d3dcompiler_proxy | `d3dcompiler_47.dll` | Shader capture proxy |
| d3dcompiler_43_proxy | `D3DCompiler_43.dll` | ENB shader capture proxy |
| d3dcompiler_46e_proxy | `d3dcompiler_46e.dll` | ENB Boris compiler proxy |

### Dependencies (resolved by vcpkg)

- **CommonLibSSE-NG** — Skyrim reverse engineering library
- **Dear ImGui** — debug GUI overlay (DX11 + Win32 bindings)
- System: `d3d11.lib`, `dxgi.lib`, `d3dcompiler.lib`

## Architecture

```
Skyrim Engine (CommonLibSSE)
        |
        v
[24 Domain Trackers] --> AllData struct (~150 float4s)
        |                       |                    |
        v                       v                    v
  ENBSetParameter          SharedMemory         PapyrusBridge
  (9 .fx shaders)          (external apps)      (mod scripts)
        |
        v
  ENB Shader Pipeline (9 stages: prepass -> bloom -> adaptation -> effect -> postpass -> ...)
        |
        +-- GPU Compute Pipeline (ComputeManager + SRVInjector)
        |     |-- LuminanceHistogram (t17)
        |     |-- LUTManager (t18)
        |     |-- HiZPyramid (t19)
        |     |-- GTAO (t20)
        |     |-- SSR (t21)
        |     |-- TAA (t22)
        |     +-- SSGI (t26)
        |
        +-- Rendering Pipeline (RenderPassManager + RenderPipeline)
              |-- ToneMapManager
              |-- AtmosphereRenderer
              |-- MaterialClassifier
              |-- SDSMCascades
              |-- VolumetricClouds
              |-- ClusteredLighting
              +-- FrameGenerator / TemporalSuperRes / MotionVectorGen
```

### Frame Lifecycle

1. **BeginFrame** — RenderPipeline executes PreENB passes (HiZ, GTAO, SSR, SSGI)
2. **Collect** — 22 trackers read engine state via CommonLibSSE (SEH-protected)
3. **Process** — FeedbackProcessor merges GPU readback, WriteBackProcessor applies rules
4. **Diagnose** — SystemHealth heartbeats, ProxyDiagnostics snapshot, health evaluation
5. **Sanitize** — NaN/Inf scrubbed from all float4s
6. **Push** — ENBSetParameter to 9 shaders (dirty-tracked), weather separation, extern binding
7. **Compute** — HiZPyramid, SDSM, ClusteredLighting dispatch, SRV injection
8. **Share** — SharedMemoryBridge writes, PapyrusBridge updates
9. **PostENB** — RenderPipeline executes PostENB passes
10. **Present** — FeedbackProcessor reads backbuffer, PrePresent passes execute

### Self-Healing

Each tracker has independent health tracking. After 5 consecutive errors, a tracker auto-disables and retries every 300 frames (~5s). Recovery is logged. Cell-dependent trackers (lights, NPCs, crosshair, regions) are skipped during loading screens. The entire frame update is SEH-wrapped to catch access violations during game state transitions.

## Documentation

| Document | Description |
|---|---|
| [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) | Universal compatibility guarantee and conflict resolution |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | System design, data flow, initialization order |
| [docs/PARAMETER_REFERENCE.md](docs/PARAMETER_REFERENCE.md) | Complete float4 parameter reference (all 24 domains) |
| [docs/SHADER_INTEGRATION.md](docs/SHADER_INTEGRATION.md) | Guide for ENB shader authors |
| [docs/CONFIGURATION.md](docs/CONFIGURATION.md) | INI file reference (Feedback, WriteBack, WeatherParams) |
| [docs/SHARED_MEMORY.md](docs/SHARED_MEMORY.md) | External app integration via shared memory |
| [docs/EXTENDER_COMPAT.md](docs/EXTENDER_COMPAT.md) | ENB Extender replacement systems |
| [docs/DIAGNOSTICS.md](docs/DIAGNOSTICS.md) | Diagnostics system (SystemHealth, CompatibilityProbe, ProxyDiagnostics) |
| [docs/CREDITS.md](docs/CREDITS.md) | Credits, acknowledgments, and third-party references |

## Project Structure

```
Playground_v3/
  src/
    core/               # Main SKSE plugin (65+ source files)
      main.cpp          # Entry point, frame callback, tracker orchestration
      BridgeData.h      # AllData struct, Float4, parameter table
      ENBInterface.h    # ENB SDK function resolution
      SystemHealth.*    # Per-system health monitoring (53 systems)
      CompatibilityProbe.* # Runtime conflict detection
      ProxyDiagnostics.*   # Proxy stats bridge
      ...               # 22 trackers + 11 renderers + compute + backend
    d3d11_proxy/        # Optional D3D11 API proxy (separate DLL)
    enb_plugin/         # Standalone ENB .dllplugin (no SKSE dependency)
    enb_helper/         # ENB Helper SE replacement
    d3dcompiler_*_proxy/ # Shader capture proxies (3 variants)
    *.cpp/h             # Phase 2-4: WeatherParams, SharedMemory, ParmLink
  config/               # INI configuration files
  shader/               # ENB .fx shaders + helpers + addons
  docs/                 # Documentation
  CMakeLists.txt        # Build configuration (7 targets)
  vcpkg.json            # Dependency manifest
  deploy.bat            # Deployment script (5 modes)
```

## License

MIT License. See individual file headers for per-file attribution.

## Author

**Zain Dana Harper**

---

*Playground v3.0.0 — Rendering platform for Creation Engine*
