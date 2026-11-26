# RAW DX12 Wave 75: Disocclusion-Safe 2-Slot ReSTIR-GI/PT, Online Multi-Resolution Hash Radiance Cache, and Heightfield-Coupled World-Space Puddle Drainage (2026-11-26)

## Summary

Wave 75 upgrades RAW DX12's ray-traced global illumination, spatial hash radiance caching, and world-space wet-surface hydrology cleanly across shaders and C++23 verification oracles:

1. **Disocclusion-Safe 2-Slot ReSTIR-GI/PT Reservoir Splatting & Generalized Balance MIS (`RawRestirHashCache.hlsli`, `RawRestirHashCache.h`, `RtShadow.hlsl`):**
   - `rawSelectTwoSlotReservoirLayer(...)`: Evaluates both primary reprojected history slot and a secondary disocclusion-safe staggered slot on temporal reuse (`k == 0`), selecting `argmin_{l in {0,1}} |log(z_cur / z_l)|` subject to `dot(n_cur, n_l) > 0.85` so background geometry uncovered behind swaying trees inherits valid temporal reservoirs instead of resetting to 1-spp noise.
   - `rawEvalRestirShiftJacobian(...)` + `rawEvalGeneralizedBalanceMis(...)`: Computes the exact bounded geometric shift-mapping Jacobian `|J_{T_{q_k -> q}}| = (|cos(theta'_s)| * ||x_s - x_k||^2) / (|cos(theta_s)| * ||x_s - x_q||^2)` (`[0.10, 10.0]`, zero NaN/Inf) and generalized balance heuristic MIS weight `m_k(y_k)` across spatial neighbors in `RtShadow.hlsl`.
2. **Online Multi-Resolution `L=8` World-Space Hash Radiance Cache (`RtCacheCommon.hlsli`, `RtCache.hlsl`, `RawRestirHashCache.h`):**
   - Adds `hashCacheKeyMultiRes(worldPos, worldNormal, lodLevel, baseCellSize)` (`L=8` cascading `2^lod` cell scales + quantized normal hash + tombstone-safe probing) and `sanitizeCache4(...)` strict finite clamping (`[0, 32]`).
   - Upgrades `RtCache.hlsl` resolve with relative-L2 online EMA adaptation (`relL2 = ||L_new - L_old||^2 / (||L_old||^2 + 0.01)`) and deterministic `maxAge` tombstone eviction (`0` NaN/Inf).
3. **Heightfield-Coupled World-Space Wetness & Puddle Drainage (`Weather.hlsli`, `RawRestirHashCache.h`):**
   - Adds pure absolute-world micro-heightfield `rawWorldMicroHeight(wp.xy)` and exact finite-difference Laplacian concavity `rawWorldDrainageConcavity(wp.xy)` (`nabla^2 H > 0`) coupled to `puddleField(wp.xy)` with strict `0.68 + 0.32 = 1.00` energy-conserving normalization and **zero screen-UV dependence** (`0.0` screen-edge residual across any camera pan or crop).
4. **Unit & Planted-Failure Verification Suites (`tests/wave75_restir_hash_drainage_check.cpp`, `tests/test_wave75_restir_hash_drainage.py`):**
   - Constant-radiance diffuse white-furnace unbiasedness (`|mean(ReSTIR_GI) - 0.8| < 0.015`) + **Planted Control A** (dropping shift-mapping Jacobian `|J_T|` causes `> 15%` furnace shift and fails closed!).
   - 2-slot disocclusion background recovery + normal mismatch rejection; `L=8` online hash cache convergence, deterministic tombstone eviction, + **Planted Control B** (NaN/Inf injection sanitized with `0` poison).
   - Screen-edge puddle invariance (`|d_center - d_edge_2px| == 0.0`) + **Planted Control C** (screen-space border vignette hack caught `> 0.20`) + **Planted Control D** (convex ridge `laplacianH < 0` receives zero drainage pooling boost).

## Verification Receipts

- **DXC 1.10 Matrix (`-WX -O3`, SM `6_6 / 6_9 / 6_10`):** `35/35 PASS`, `0` warnings (`RtShadow.hlsl` x 3, `RtCache.hlsl` x 3, `RtSkyFilter.hlsl` x 3, `FroxelPass.hlsl` x 9, `Rawlss5Pass.hlsl` x 3, `RawTsrUpscaleSim.hlsl` x 6, `Lighting.hlsl` x 3, `RtWater.hlsl` x 3, `Tonemap.hlsl` x 2).
- **Ninja + MSVC C++23 `/W4 /WX` (Agility SDK `1.722.1-preview`):** `d3d11.dll` clean (`0` warnings); `wave75_restir_hash_drainage_check.exe` **`9/9 PASS`** (`4/4` planted controls); `wave74_dynamics_coupling_check.exe` **`10/10 PASS`**; `wave73_antidrift_pacing_check.exe` **`12/12 PASS`**; `wave72_profile_abi_check.exe` **`14/14 PASS`**.
- **GPU & Runtime Suites (RTX 4090, `D3D12 DebugLayer=ON`, `0` errors):**
  - `ce_runtime_check.exe`: **`522/522 PASS`**
  - `test_tsr_upscale_gpu.exe`: **`18/18 PASS`** (`0` D3D12 Debug Layer errors on RTX 4090 + WARP)
  - `raw_bench_suite.exe`: **`21/21 PASS`** (`d3d12DebugErrors=0`, `NaN=0` across all 21 passes: Fluid `0.258 ms`, RC-GI `0.111 ms`, XPBD `0.0579 ms`, Froxels `0.0448 ms`, Clouds `0.0837 ms`, RAW-TSR+RCAS `0.0511 ms`, Water Caustics+Rays `0.0472 ms`, Snow/Footprint `0.0240 ms`)
  - Python suites: `test_wave75_restir_hash_drainage.py` **`3/3 PASS`**, `test_wave74_dynamics_coupling.py` **`3/3 PASS`**, `test_wave73_antidrift_pacing.py` **`3/3 PASS`**, `test_wave72_multigame_surpass.py` **PASS**, `test_visual_heuristics.py` **PASS**.
- **Live 4-Scene Moving-Camera Skyrim SE Audit (`--profile quick --burst 6 --stride 3`, RTX 4090):** **`96/96 PASS`** (`4/4` clean scenes, `0` defects, `PREFS_MATCH=True`, `CUSTOM_MATCH=True`, `POST_SKYRIMSE_PROCS=0`):
  - `riverwood_day`: `24/24 PASS` (`shadow=0.050948`, `ghost=0.067963`, `bark=0.000662`, `nrm=0.068666`)
  - `inn_interior`: `24/24 PASS` (`shadow=0.001197`, `ghost=0.045497`, `bark=0.0`, `nrm=0.004374`)
  - `septimus_cave`: `24/24 PASS` (`shadow=0.001197`, `ghost=0.000000`, `bark=0.0`, `nrm=0.000484`)
  - `windhelm_ext_snow`: `24/24 PASS` (`shadow=0.016810`, `ghost=0.029807`, `bark=0.0`, `nrm=0.012045`)
