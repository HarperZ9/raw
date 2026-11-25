# Creation Engine Portability Architecture (Skyrim SE/AE, Fallout 4, Fallout 76, Starfield)

This document defines the shared architecture and per-title boundaries for running RAW's DX12 / SM 6.9 / DXR 1.2 rendering pipeline and `raw_engine.dll` core fixes across Bethesda's Creation Engine family.

## 1. Shared Engine Core (Zero Per-Game Rewrite)

Every module below compiles once and runs unchanged across all four titles via `raw::versions::EngineProfile` (`src/common/raw_engine_profile.h`):

| Layer | Components | Role Across Titles |
|---|---|---|
| **OS & Core Runtime Fixes** | `rt_core_fixes.cpp` | 0.5-1.0 ms timer precision, Windows 11 EcoQoS P-core scheduling, process-wide Low-Fragmentation Heap (LFH), 8192 CRT stdio handle ceiling |
| **Hardware & Silicon Scaling** | `HardwareProfile.cpp` | Auto-detects RTX 3060 through RTX 5090 tiers (`0..4`), SM 6.6-6.10, DXR 1.2 OMM/SER, Mesh Shaders, Work Graphs, auto `raw::tx` worker count |
| **DX12 Render Graph & Shaders** | `LightingPass.cpp`, `RtScene.cpp`, `RtCache.hlsl`, `RtShadow.hlsl`, `Rawlss5Pass.hlsl`, `FroxelPass.hlsl`, `WorldFieldSim.hlsl`, `Clouds.hlsl`, `RawTensorMath.hlsli` | Retail-signed SM 6.9 DXIL 1.9 + universal SM 6.6 fallback; Negative-Space Visibility/Entropy Radiance Oracle; RAW-MEGALIGHTS ReSTIR DI; RAWLSS 7.0 Split-Manifold 16-wide Long-Vector Ray Reconstruction; 4.3x Blelloch Block-Scan 3D Froxels; vortex-stretching MacCormack PDE fluid/weather grid |
| **Universal RTTI & Setting Scanner** | `raw_versions.h`, `rt_version.cpp` | Detects running executable (`SkyrimSE.exe`, `Fallout4.exe`, `Fallout76.exe`, `Starfield.exe`), loads `<prefix>-<version>.tsv` or scans MSVC RTTI vtables + `SettingT<*>` singletons + Address Library (`SKSE`/`F4SE`/`SFSE`) |

## 2. Per-Title Integration Matrix

| Title | Engine Gen | Graphics Entry Path | Core Engine Fixes Active | Porting Requirements |
|---|---|---|---|---|
| **Skyrim SE / AE / VR** | CE 1.0 (DX11) | `d3d11.dll` (`raw::tx` multi-threaded D3D11->D3D12) + `raw_engine.dll` | 30-360 Hz Havok decouple, 8192 stdio, LFH, EcoQoS, 0.5ms timer, lip-sync async load, cell-persistent light registry (breaks 4/7 light cap), adaptive Papyrus VM, uGrids live hook | Shipping baseline (verified) |
| **Fallout 4 / FO4VR** | CE 1.5 (DX11) | Same `d3d11.dll` (`raw::tx`) + `raw_engine.dll` | Same OS + 30-360 Hz Havok + 8192 stdio + FO4 shadow/LOD/godray/particle unlocks (preserves `bUseCombinedObjects` PreVis/Precombines) | Add `fallout4-<ver>.tsv` (or F4SE `version-*.bin` Address Library) + FO4 `ShadowSceneNode` / G-buffer CB slot mapping in `SceneLights.cpp` (deferred G-buffer vs Skyrim forward-lit CB layout) |
| **Fallout 76** | CE 1.8 (DX11) | `d3d11.dll` (`raw::tx`) + read-only `raw_engine.dll` | OS timer + EcoQoS + LFH only (`isMultiplayerOnline=true` hard-blocks all gameplay/VM/memory patches) | Read-only DX11->DX12 render graph only; never inject gameplay or memory detours into online clients |
| **Starfield** | CE 2.0 (Native DX12) | Direct DXGI / D3D12 command-queue hook -> `raw_d3d12_bridge` (bypasses `raw::tx` completely) + `raw_engine.dll` | OS timer + EcoQoS + LFH + present interval unlock (Havok 2022 already frame-independent) | Thin DXGI/D3D12 present/resource hook binding Starfield's native D3D12 G-buffer, depth, motion vectors, and `starfield-<ver>.tsv` / SFSE Address Library |

## 3. Core Creation Engine Limitations Eliminated

1. **Single-threaded D3D11 draw-call wall (CE 1.0 / 1.5 / 1.8):** Eliminated by `raw::tx` parallel command-list translation (`Workers=-1` auto-scaled in `TxMode.cpp`) + native DX12 `PSO` disk cache + async compute path.
2. **4-shadow-light / 7-point-light per-mesh flicker limit (CE 1.0 / 1.5):** Eliminated by `rt_livestate.cpp` cell-persistent world light registry + clustered `RAW-MEGALIGHTS` visibility-reused ReSTIR DI (`RtShadow.hlsl`).
3. **60 FPS Havok physics tie & high-refresh jitter (CE 1.0 / 1.5):** Eliminated by 30-360 Hz dynamic `fMaxTime` / `fMaxTimeComplex` + `uMaxNumPhysicsStepsPerUpdate >= 6` + multi-core `iNumThreads:Havok`.
4. **512-handle False Save Corruption Bug (CE 1.0 / 1.5):** Eliminated by universal `_setmaxstdio(8192)` across host CRT, `ucrtbase.dll`, and legacy `msvcr110`/`msvcr120`.
5. **15.6 ms timer quantization, E-core throttling & heap fragmentation (All CE1-CE2):** Eliminated by 0.5-1.0 ms timer precision, Windows 11 `ProcessPowerThrottling` disable, and process-wide LFH.
6. **NPC dialogue stutter & Papyrus script spikes (CE 1.0 / 1.5):** Eliminated by `bBackgroundLoadLipFiles:General=1` and frame-time-coupled adaptive Papyrus VM budgeting (`rt_papyrus.cpp`).
