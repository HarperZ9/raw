# RAW — Rendering Advancement Workshop

Copyright (c) 2026 Zain D. Harper (papacr0w). All rights reserved.

A D3D11 rendering platform for Skyrim SE. Proxy-based pipeline ownership
with mid-frame effect dispatch via SKSE plugin.

**Status: Active development. Core effects functional, tuning in progress.**

## Active Effects

| Effect | Algorithm | Status |
|--------|-----------|--------|
| GTAO | Jimenez 2019 visibility bitmask | Functional, verified in-game |
| Contact Shadows | Screen-space directional ray march | Functional, needs tuning |
| Skylighting | Screen-space hemisphere visibility | Functional, noise reduction in progress |
| SSR | McGuire & Mara 2014 Hi-Z march | Visible artifacts, needs work |
| SSGI | Voxel cone tracing | Green tint issue, needs rework |
| Scene Compositor | Multi-effect blend with multi-bounce AO | Functional |

## Active Post-Processing

| Stage | Status |
|-------|--------|
| Bloom (Karis + Jimenez 13-tap) | In build, untested |
| Color Pipeline (AgX, 7 tonemappers) | In build, untested |
| Tone Map Manager (auto-exposure) | In build, untested |

## Architecture

- **RendererBase** — shared base class for all renderers (state, init, enable)
- **SharedRAW.hlsli** — common HLSL utilities (`#include` enabled via `D3D_COMPILE_STANDARD_FILE_INCLUDE`)
- **d3d11.dll proxy** — wraps Device, Context, SwapChain
- **Phase detection** — DSV-unbind heuristic classifies 9 render phases
- **Mid-frame dispatch** — effects run at PostGeometry; PrePresent fallback for legacy mode
- **HiZ pyramid** — standard-to-reversed-Z depth with mip chain
- **External HLSL only** — no embedded shader strings; all shaders loaded from disk, F12 hot-reload
- **Weather system** — 9 categories, auto-modulates effect intensities

## Disabled (source preserved, removed from build)

17 renderers removed from compilation pending individual polish:
DoF, Lens, Underwater, Atmosphere, Volumetric Clouds, Volumetric Lighting,
SSS, Frame Gen, TSR, Grass Lighting, Tree LOD, Water Blending,
Dynamic Cubemap, SDSM, Indirect Specular, Screen-Space Decals, Particle Lighting.

## Build

```
cd RAW
cmake -B build -S . -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release --target RAW --target d3d11_proxy
```

## Deploy (MO2)

```
<mod>/SKSE/plugins/RAW.dll
<mod>/SKSE/plugins/RAW/Shaders/*.hlsl
<mod>/SKSE/plugins/RAW/Shaders/*.hlsli
<mod>/Root/d3d11.dll
```

## In-Game

1. Press **INSERT** to open debug GUI
2. Click **Recommended** for safe defaults (GTAO + Contact Shadows + Skylighting + AgX)
3. **Pipeline Status** section shows proxy/depth/dispatch diagnostics

## Shader Development Workflow (No Restarts)

```
F5  = Dump all effect textures to Data/SKSE/Plugins/RAW/Captures/
F7  = Toggle mid-frame dispatch
F12 = Hot-reload all HLSL shaders from disk
F11 = GPU profiler overlay (per-pass timing)
F10 = 600-frame performance capture to CSV
```

**The loop:** edit .hlsl in your text editor → F12 to reload → see result instantly.
Use debug modes (SceneCompositor Debug View dropdown) to isolate each effect.
F5 dumps BMP files you can inspect at full resolution in any image viewer.

## Build

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release --target RAW --parallel
cmake --build build --config Release --target d3d11_proxy --parallel
```

Requires: VS2022, CMake 3.24+, vcpkg (x64-windows-static), CommonLibSSE-NG.

## Deploy (MO2)

```
[mod folder]/SKSE/plugins/RAW.dll
[mod folder]/SKSE/plugins/RAW/RAW.ini          (auto-generated)
[mod folder]/SKSE/plugins/RAW/Shaders/*.hlsl    (85 files)
[mod folder]/SKSE/plugins/RAW/ShaderCache/      (auto-generated)
[mod folder]/ROOT/d3d11.dll
```

## Hotkeys

| Key | Function |
|-----|----------|
| INSERT | Toggle debug GUI |
| F7 | Toggle mid-frame dispatch |
| F8 | Toggle compute passes |
| F9 | Toggle render passes |
| F10 | Frame capture (CSV) |
| F11 | GPU profiler |
| F12 | Hot-reload shaders |

## Documentation

- `ARCHITECTURE.md` — depth conventions, SRV slots, pipeline rules
- `LICENSE.md` — copyright and third-party licenses
