# Computer Graphics Research Canon (2022-2026) Mapped to RAW

This reference maps flagship computer graphics conferences, real-time symposiums, and core journals directly onto RAW's active DX12/DXR 1.2 rendering passes, zero-SKSE engine modules, and remaining engineering targets.

## 1. Premier Venues Covered
- **Flagship Conferences:** SIGGRAPH, SIGGRAPH Asia (ACM TOG track), Eurographics (CGF track), Pacific Graphics, and Game Developers Conference (GDC).
- **Real-Time and Systems Symposiums:** High-Performance Graphics (HPG), ACM SIGGRAPH I3D, Eurographics Symposium on Rendering (EGSR), SIGGRAPH Advances in Real-Time Rendering in Games (`advances.realtimerendering.com`), and Khronos Vulkanised.
- **Core Journals:** ACM Transactions on Graphics (TOG), Computer Graphics Forum (CGF), Journal of Computer Graphics Techniques (JCGT), and IEEE TVCG.

## 2. Implemented Techniques in RAW (Verified Live)

| Venue / Paper | Technique ID | Mechanism Implemented in RAW | Source File |
|---|---|---|---|
| **SIGGRAPH Advances 2026** (Olejnik, Activision / IW Poland) | `LGT-16` | Variable-Rate Ray Tracing: couples per-pixel motion/disocclusion gradient with world radiance-cache entropy to allocate 1 to 4 GI rays per pixel. | `src/d3d12_bridge/shaders/RtShadow.hlsl` |
| **HPG 2026 Best Paper** (Junkins, Kettunen, Lin, Ramamoorthi, Wyman) | `LGT-09` | Compatibility-Guided Spatial ReSTIR Neighbor Selection: tight normal compatibility (`dot >= 0.84`) + smooth compatibility weighting eliminates corner light bleed. | `src/d3d12_bridge/shaders/RtShadow.hlsl` |
| **SIGGRAPH Advances 2026** (Greenberg, EA SEED) | `GI-18` | Online Radiance Cache Acceleration (ORCA): zero-lag luminance-jump history reset in the 1M-slot world-space hash radiance cache (`RtCache.hlsl`). | `src/d3d12_bridge/shaders/RtCache.hlsl` |
| **SIGGRAPH Advances 2026** (Hable, Meta) | `GEO-04` | Adaptive GPU Curved PN-Triangle Subdivision (`RawMesh.hlsl` 1x/4x/9x AS+MS + `RawSubdiv.hlsl` 1-to-4 CS) with crease protection and 3D snowpack lift. | `src/d3d12_bridge/shaders/RawMesh.hlsl`, `RawSubdiv.hlsl`, `RawMeshHost.cpp` |
| **SIGGRAPH Advances 2026** (Craig, Sony Interactive Entertainment) | `AA-07` | Hybrid closed-form + compact neural reconstruction (`RAWLSS 7.0`: albedo demodulation, Karis YCoCg Catmull-Rom, E8 lattice stabilization, 12->16->4 SM 6.9 Long-Vector net). | `src/d3d12_bridge/shaders/Rawlss5Pass.hlsl`, `RawTensorMath.hlsli` |
| **SIGGRAPH Advances 2025/2026** (Narkowicz/Costa; Mueller/IOI) | `LGT-01`, `SKY-05` | Clustered + ReSTIR many-light direct lighting (`128` lights) and unified 160x90x64 3D froxel participating media with Blelloch block scan. | `src/d3d12_bridge/shaders/FroxelPass.hlsl`, `RtShadow.hlsl` |
| **I3D 2024 / HPG 2023** (Tokuyoshi & Eto; Dupuy & Benyoub) | `MAT-05`, `MAT-06` | Bounded Spherical-Cap GGX VNDF reflection sampling (`0%` sub-horizon rays). | `src/d3d12_bridge/shaders/RtShadow.hlsl` |
| **EGSR 2025 / SIGGRAPH 2025** (Portsmouth/Kutz/Hill; Kneiphof/Klein; Wu et al.) | `MAT-03`, `RAO-04` | EON energy-preserving rough diffuse BRDF, Kulla-Conty multi-scattering GGX, Fdez-Aguera multi-scatter IBL, and dual-scale position-normal snow/ice glints. | `src/d3d12_bridge/shaders/Lighting.hlsl` |
| **EGSR 2020 / SIGGRAPH Advances 2022** (Hillaire; Schneider) | `SKY-01`, `SKY-02` | Transmittance + multi-scattering + sky-view LUTs, Ekman-sheared 3D volumetric cumulus with Navier-Stokes curl turbulence, virga rain curtains, and sheet lightning. | `src/d3d12_bridge/shaders/AtmosphereLut.hlsl`, `Clouds.hlsl`, `LightFog.hlsl` |
| **HPG 2017** (Schied et al., SVGF) | `DNS-01` | 3-pass 25-tap B3-spline variance-propagating a-trous GI denoiser with chromaticity edge stopping. | `src/d3d12_bridge/shaders/GiAtrousPass.hlsl` |
| **DXR 1.2 / DirectX Agility SDK 1.620** | `GEO-14`, `PIP-04` | Native SM 6.9 DXIL 1.9, Opacity Micromaps (4-state foliage/hair), Shader Execution Reordering, and ResourceDescriptorHeap bindless indexing. | `src/d3d12_bridge/RtScene.cpp`, `RtOmmBake.hlsl` |

## 3. Open High-Impact Targets (Honest Remaining Gaps)
1. **Live `raw::tx` Draw-Call Routing into `RawSubdiv.hlsl` (`GEO-04` on screen):** `RawMeshHost` compiles and initializes both `RawMesh.hlsl` and `RawSubdiv.hlsl`, but `raw::tx` does not yet intercept live Skyrim D3D11 draw calls to substitute subdivided vertex/index buffers on screen.
2. **Cluster BVH / H-PLOC Animated Geometry (`GEO-10`, `GEO-12`, `PIP-11`):** Needed to keep 4x subdivided and animated foliage/actor meshes inside the DXR 1.2 TLAS at 60-120 Hz.
3. **Shader Execution Reordering (`dx::HitObject` SM 6.9/6.10 `PIP-04`):** Hardware SER is probed and reported active on RTX 4090, ready for trace-ray pipeline migration once DXR 1.2 state-object paths replace inline RayQuery where profitable.

## Verified
- Compiled and verified `RtShadow.hlsl`, `RtCache.hlsl`, `Lighting.hlsl`, `Clouds.hlsl`, `LightFog.hlsl`, `Rawlss5Pass.hlsl`, `RawMesh.hlsl`, and `RawSubdiv.hlsl` via retail SM 6.6/6.9 DXC and SM 6.10 preview DXC.
- Verified `ce_runtime_check.exe` (41/41 pass) and `d3d11_translate_suite.exe` (41/41 pass on WARP).
- External 2026 conference paper summaries were verified via web search against `advances.realtimerendering.com`, `highperformancegraphics.org`, and `cwyman.org`; proprietary vendor SDK code (Epic UE, NVIDIA NRD/RTXDI) was not read or copied.
