# RAW DX12 Wave 73: 10-Step Temporal Anti-Drift Guard and Deterministic 100 Hz Multi-Game Frame Pacing (2026-11-26)

## Summary

Wave 73 hardens RAW DX12 temporal stability and multi-game frame pacing across Skyrim SE (CE1 DX11->DX12 bridge), Fallout 4 (CE1.5 DX11->DX12 bridge), and Starfield (CE2 Native DX12 host):

1. **Deja-Inspired Null-Space 10-Step Temporal Anti-Drift Guard (`FroxelPass.hlsl`, `Rawlss5Pass.hlsl`, `RawTsrUpscaleSim.hlsl`):**
   - **Volumetric Froxels (`FroxelPass.hlsl`):** Replaces raw pre-multiplied `(inScat * sigma, sigma)` temporal blending with an albedo-preserving scattering blend (`albBlendRgb = clamp(lerp(curRad, prevRad, wHist), radMin, radMax) * blendExt`) and strict luminance convex-hull clamping `[minScatL, maxScatL]`. Prevents multi-frame volumetric fog darkening and bloom-bleed across sharp fog-density transitions.
   - **RAWLSS 6.5 / RAW-TSR (`Rawlss5Pass.hlsl`, `RawTsrUpscaleSim.hlsl`):** Enforces a YCoCg chroma-saturation envelope (`maxChromaEnv = max(length(cur.yz), length(clipped.yz))`) before inverse Karis expansion, then projects out Jensen-inequality Karis darkening along the linear-energy null space (`driftCorr = clamp(targetLinL / reconLinL, 0.90, 1.25)`), strictly clamped inside `[minPairLinL, maxPairLinL]` and gated off on dark shadows (`< 0.015`), HDR fireflies (`> 4.0`), and high-disocclusion/reactive pixels.
2. **Deterministic 100 Hz Fixed-Timestep Frame Pacing & Input-Latency Telemetry (`src/d3d12_bridge/RawFramePacing.h`, wired into `NativeDx12Host.h`):**
   - Header-only MIT C++23 module (`kFixedSimHz = 100.0`, `kFixedTickSec = 0.01`, `maxSubsteps = 8`) exporting per-title pacing contracts via `ResolveFramePacingPolicy(const EngineProfile&)`:
     - Skyrim SE / VR: `100 Hz` fixed step, Havok `>60 Hz` substep guard enabled, `45 ms` max input-latency budget.
     - Fallout 4 / VR: `100 Hz` fixed step, Havok + PreVis pacing guard enabled, `45 ms` max input-latency budget.
     - Starfield CE2 Native DX12: `100 Hz` fixed step, async-compute overlap enabled, `35 ms` max input-latency budget, surfaced directly inside `ValidateNativeDx12HostContract(...)`.
     - Fallout 76: fail-closed multiplayer guard (`multiplayerBlocked = true`, zero substeps).
   - Zero-allocation `DeterministicFramePacer` tracking exact fixed ticks, spike clamping without spiral-of-death, and rolling `p50 / p99 / jitter / maxInputLatencyMs` telemetry, plus `VerifyTenStepTemporalAntiDrift(...)` CPU verification oracle.
3. **Unit & Planted-Drift-Regression Test Suites (`tests/wave73_antidrift_pacing_check.cpp`, `tests/test_wave73_antidrift_pacing.py`):**
   - Verifies exact `100 Hz` cadence across `60 / 90 / 120 / 144 / 240 FPS`, `250 ms` frame-spike clamping, steady-state `<= 1e-6` fixed-point drift, moving-foliage `< 2.5%` luminance drift with `0` chroma overshoot, and `0` froxel albedo/luminance overshoot.
   - Verifies 4 planted-failure negative controls: injected `+2.5%/step` 10-frame accumulation bias, non-conservative specular MV gain (`1.18x`), input-latency staleness spike (`54.67 ms > 45 ms`), and cross-game / multiplayer contract violations.

## Verification Receipts

- **DXC 1.10 Matrix (`-WX -O3`, SM `6_6 / 6_9 / 6_10`):** `22/22 PASS`, `0` warnings (`FroxelPass.hlsl` x 3 entry points x 3 SMs, `Rawlss5Pass.hlsl` x 3 SMs, `RawTsrUpscaleSim.hlsl` x 2 entry points x 3 SMs, `Lighting.hlsl` x 2 SMs, `Tonemap.hlsl` x 2 SMs).
- **Ninja + MSVC C++23 `/W4 /WX` (Agility SDK `1.722.1-preview`):** `[14/14] d3d11.dll` clean (`0` warnings); `wave73_antidrift_pacing_check.exe` **`12/12 PASS`** (including `4/4` planted controls); `wave72_profile_abi_check.exe` **`14/14 PASS`** (`failures=0`).
- **GPU + Runtime Suites:** `test_tsr_upscale_gpu.exe` **`18/18 PASS`** on RTX 4090 (`0.0531 ms`, `35.2%` RMSE reduction, `0` D3D12 Debug Layer errors) and WARP (`0` Debug Layer errors); `ce_runtime_check.exe` **`522/522 PASS`**; `test_wave73_antidrift_pacing.py` **`3/3 PASS`**; `test_wave72_multigame_surpass.py` **PASS**; `test_visual_heuristics.py` **PASS**.
- **Live 4-Scene Moving-Camera Skyrim SE Audit (`--profile quick --burst 6 --stride 3`, RTX 4090):** **`96/96 PASS`** (`4/4` clean scenes, `0` defects, `PREFS_MATCH=True`, `CUSTOM_MATCH=True`, `POST_SKYRIMSE_PROCS=0`):
  - `riverwood_day`: `24/24 PASS` (`defects: none`, ghost `0.079622`, shadow stability `0.052773`, world-lock `1.250458`)
  - `inn_interior`: `24/24 PASS` (`defects: none`, ghost `0.028081`, shadow stability `0.001197`, world-lock `2.408265`)
  - `septimus_cave`: `24/24 PASS` (`defects: none`, ghost `0.000000`, shadow stability `0.001197`, world-lock `4.973842`)
  - `windhelm_ext_snow`: `24/24 PASS` (`defects: none`, ghost `0.079762`, shadow stability `0.027151`, world-lock `4.021699`)
