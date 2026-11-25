# RAW Engine BuildLang Physics Kernel Map

This document maps RAW Engine's deterministic C++23 and HLSL SM 6.6/6.10 scalar physics kernels to their verified **BuildLang 1.4.0** counterparts in [`native/bld/RawPhysicsKernels.bld`](../native/bld/RawPhysicsKernels.bld).

## Toolchain Verification Receipts

Verified directly on this workstation with `buildc 1.4.0` (`C:\Users\Zain\.cargo\bin\buildc.exe`):

1. **Type-Check (`buildc check native/bld/RawPhysicsKernels.bld`)**:
   - Exit Code: `0`
   - Receipt: `Lexing... OK (763 tokens)` / `Parsing... OK (9 items)` / `Type checking... OK` / `No errors found in 'E:\wt\raw-rt9\native\bld\RawPhysicsKernels.bld'`
2. **Direct Execution (`buildc run native/bld/RawPhysicsKernels.bld`)**:
   - Exit Code: `0`
   - Verified Live Output:
     ```text
     wind_chill_c(-10C, 36km/h) = -20.302721936049934
     phase_hg(0.95, 0.72) = 0.6570597435555726 | phase_hg(-0.95, 0.72) = 0.007815231380744057
     beer_lambert(0.045, 2.0) = 0.9139311852712282
     davinci_pipe_radius(0.30, 0.30, 2.5) = 0.3958523732318682
     xpbd_lambda_delta(0.02, 0, 2.0, 1e-6, dt/4) = -0.009720062212751754
     crafting_cap_mult(100, 0.50) = 1.9914988713543762 | exploit(100, 100.0) = 2.25
     ```

## Kernel-by-Kernel Mapping

| # | BuildLang Function (`RawPhysicsKernels.bld`) | Engine C++ / HLSL Counterpart | Governing Equation | Physical Invariant Verified |
| --- | --- | --- | --- | --- |
| 1 | `wind_chill_c(air_c, wind_kmh)` | `src/engine_rt/rt_stealth_survival.cpp` (L114-L120) | `13.12 + 0.6215*T - 11.37*v^0.16 + 0.3965*T*v^0.16` (for `T <= 10 C`, `v >= 4.8 km/h`), clamped to `[-45, 40] C` | Osczevski-Bluestein index at `-10 C`, `36 km/h` yields `-20.303 C` (`< air_c`, strictly bounded) |
| 2 | `phase_hg(cos_theta, g)` | `AtmosphereLut.hlsl`, `Clouds.hlsl`, `LightFog.hlsl` | `(1 - g^2) / (4*pi * (1 + g^2 - 2*g*mu)^(3/2))` | Strong forward Mie peak at `mu=0.95, g=0.72` (`0.6571`) vs backward lobe `mu=-0.95` (`0.0078`, `84.1x` ratio) |
| 3 | `beer_lambert(sigma_t, dist)` | `RtWater.hlsl`, `RawMaterials3D.hlsli`, `RawCosmosSky.hlsli` | `exp(-max(sigma_t, 0) * max(dist, 0))` | Monotonic spectral transmittance in `(0, 1]` (`0.9139` for glacial blue `sigma_t=0.045/m` at `2.0 m`) |
| 4 | `davinci_pipe_radius(r_child_a, r_child_b, exponent_p)` | `src/worldgen/RawFloraEngine.cpp` (L650-L684) | `(r_a^p + r_b^p)^(1/p)`, `p` clamped to `[1.8, 3.5]` | Strict area-conserving botanical taper (`r_parent = 0.3959 > 0.30` for equal children at `p=2.5`) |
| 5 | `xpbd_lambda_delta(c_val, lambda_prev, inv_mass_sum, compliance, sub_dt)` | `src/d3d12_bridge/shaders/RawXpbdClothSim.hlsl` (L308-L334) | `alpha_tilde = compliance / sub_dt^2`; `dLambda = (-C - alpha_tilde * lambda_prev) / (w_sum + alpha_tilde)` | Restoring compliance step `-0.00972` for `C=+0.02`, `w_sum=2.0`, `alpha=1e-6`, `sub_dt=1/240 s` (zero division-by-zero) |
| 6 | `crafting_cap_mult(skill_0_100, fortify_frac, skill_weight, max_cap)` | `src/engine_rt/rt_progression_economy.cpp` (`EvaluateCraftingCaps`, L132-L150) | `span = cap - 1`; `1 + span * tanh(raw_gain / span)`, `cap <= 2.25` | Smooth master progression (`1.9915x` at skill 100 + 50% fortify) while hard-capping extreme fortify exploit (`fort=100.0`) at `<= 2.25x` |

## Architecture Alignment Notes

- **Zero External Dependencies**: `RawPhysicsKernels.bld` uses only core BuildLang 1.4.0 primitives (`f64`, `if/else`, native `sqrt`/`pow`/`exp`/`tanh` math builtins, and `Console` effect for `main()`), with no external crate or header imports.
- **Exact Equation Parity**: Every constant, clamp range, denominator guard, and formula matches the corresponding production C++23 or HLSL SM 6.6/6.10 implementation in `E:\wt\raw-rt9`.
- **Reproducing Checks**:
  ```powershell
  buildc check E:\wt\raw-rt9\native\bld\RawPhysicsKernels.bld
  buildc run E:\wt\raw-rt9\native\bld\RawPhysicsKernels.bld
  ```
