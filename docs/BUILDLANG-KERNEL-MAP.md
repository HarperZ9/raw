# RAW Engine BuildLang Physics, Atmosphere, Biome, Optics and Fluid Kernel Map

This document maps RAW Engine's deterministic C++23 and HLSL SM 6.6/6.10 scalar physics, atmosphere, meteorology, biomechanics, PBR optics, fluid dynamics, acoustic, and shadow kernels to their verified **BuildLang 1.4.0** counterparts under `native/bld/` (**5 modules, 30 verified kernels**):

1. [`native/bld/RawPhysicsKernels.bld`](../native/bld/RawPhysicsKernels.bld) (6 core scalar physics & anti-exploit kernels)
2. [`native/bld/RawAtmoWeather.bld`](../native/bld/RawAtmoWeather.bld) (7 planetary atmosphere, orbital solar, snowpack ODE & black-ice kernels)
3. [`native/bld/RawBiomeFlora.bld`](../native/bld/RawBiomeFlora.bld) (5 botanical biomechanics, fluvial erosion & phenology kernels)
4. [`native/bld/RawRenderOptics.bld`](../native/bld/RawRenderOptics.bld) (6 PBR BRDF, multi-scatter GGX, Christensen-Burley SSS, AgX & multi-bounce AO kernels)
5. [`native/bld/RawFluidDynamics.bld`](../native/bld/RawFluidDynamics.bld) (6 buoyancy, shallow-water, CFL, froxel scattering, Sabine-Eyring RT60 & PCSS kernels)

## Toolchain Verification Receipts (`buildc 1.4.0`)

Verified directly on this workstation with `C:\Users\Zain\.cargo\bin\buildc.exe` (`buildc 1.4.0`, **5/5 `check` PASS**, **5/5 `run` PASS**):

### Module 1: `native/bld/RawPhysicsKernels.bld`
- `buildc check`: Exit Code `0` (`Lexing... OK (763 tokens)`, `Parsing... OK (9 items)`, `Type checking... OK`, `No errors found`)
- `buildc run`: Exit Code `0`
  ```text
  wind_chill_c(-10C, 36km/h) = -20.302721936049934
  phase_hg(0.95, 0.72) = 0.6570597435555726 | phase_hg(-0.95, 0.72) = 0.007815231380744057
  beer_lambert(0.045, 2.0) = 0.9139311852712282
  davinci_pipe_radius(0.30, 0.30, 2.5) = 0.3958523732318682
  xpbd_lambda_delta(0.02, 0, 2.0, 1e-6, dt/4) = -0.009720062212751754
  crafting_cap_mult(100, 0.50) = 1.9914988713543762 | exploit(100, 100.0) = 2.25
  ```

### Module 2: `native/bld/RawAtmoWeather.bld`
- `buildc check`: Exit Code `0` (`Lexing... OK (987 tokens)`, `Parsing... OK (11 items)`, `Type checking... OK`, `No errors found`)
- `buildc run`: Exit Code `0`
  ```text
  rayleigh_density(0km) = 1 | rayleigh_density(8km) = 0.36787944117144233
  mie_density(1.2km, 1.2, 1.5) = 0.5518191617571635
  ozone_density(25km) = 1 | ozone_density(10km) = 0
  solar_declination_rad(172, 23.44) = 0.40907108129169373 | solar_cos_zenith(12.5h, 60N) = 0.803213213138012
  snowpack_step_m(melt) = 0.42308 | snowpack_step_m(accum) = 0.58
  black_ice_refreeze(0 -> -8C, 30s) = 0.7383179338314574
  ```

### Module 3: `native/bld/RawBiomeFlora.bld`
- `buildc check`: Exit Code `0` (`Lexing... OK (975 tokens)`, `Parsing... OK (9 items)`, `Type checking... OK`, `No errors found`)
- `buildc run`: Exit Code `0`
  ```text
  wind_deflection_m(trunk 18m/s) = 0.00876418111668535 | (bough 18m/s) = 0.006506105164191534
  snow_branch_sag_m(32kg/m2, 2.8m) = 0.04176656975828648
  stream_power_erosion(K=0.015, Q=4, S=0.35) = 0.010499999999999999
  autumn_phenology_blend(summer) = 0 | (october) = 0.7562061707245951
  phototropism_weight(top) = 0.8184881664000001 | (shaded) = 0.022618944000000002
  ```

### Module 4: `native/bld/RawRenderOptics.bld`
- `buildc check`: Exit Code `0` (`Lexing... OK (913 tokens)`, `Parsing... OK (10 items)`, `Type checking... OK`, `No errors found`)
- `buildc run`: Exit Code `0`
  ```text
  fresnel_schlick(0.04, 1.0) = 0.04 | (0.04, 0.05) = 0.7828297
  ggx_ndf(1.0, 0.35) = 21.211820820244274
  multi_scatter_boost(iron F0=0.56, nv=0.20, r=0.85) = 1.491643192488263
  burley_diffusion(r=0.5, d=1.0 red) = 0.11562705169167452 | (d=0.22 blue) = 0.2068407816930146
  agx_sigmoid(0.60) = 0.4842776 | (0.95) = 0.9762452234375046
  multi_bounce_ao_ch(ao=0.35, snow=0.88) = 0.7146357120000002 | (dark=0.08) = 0.35
  ```

### Module 5: `native/bld/RawFluidDynamics.bld`
- `buildc check`: Exit Code `0` (`Lexing... OK (617 tokens)`, `Parsing... OK (9 items)`, `Type checking... OK`, `No errors found`)
- `buildc run`: Exit Code `0`
  ```text
  buoyancy_accel(1800K flame) = 50.276211604095565
  swe_wave_celerity(g=9.81, h=1.5m) = 3.8360135557633264
  cfl_dt_limit(dx=0.125m, v=12m/s, cfl=0.8) = 0.008333333333333333
  froxel_slice_scatter(S=0.45, ext=0.08, ds=2.5m) = 1.0196395139363523
  sabine_rt60_s(Nordic crypt V=1800, S=960, a=0.08) = 3.6204026744154088
  pcss_penumbra_radius(near) = 0.025 | (far) = 0.4
  ```

## Kernel-by-Kernel Mapping (30 Kernels)

### 1. Core Physics Kernels (`native/bld/RawPhysicsKernels.bld`)

| # | BuildLang Function | Engine C++ / HLSL Counterpart | Governing Equation | Verified Invariant |
| --- | --- | --- | --- | --- |
| 1 | `wind_chill_c(air_c, wind_kmh)` | `src/engine_rt/rt_stealth_survival.cpp` (L114-L120) | `13.12 + 0.6215*T - 11.37*v^0.16 + 0.3965*T*v^0.16`, clamped `[-45, 40] C` | `-10 C`, `36 km/h` yields `-20.303 C` |
| 2 | `phase_hg(cos_theta, g)` | `AtmosphereLut.hlsl`, `Clouds.hlsl`, `LightFog.hlsl` | `(1 - g^2) / (4*pi * (1 + g^2 - 2*g*mu)^(3/2))` | `84.1x` forward-to-back ratio at `g=0.72` |
| 3 | `beer_lambert(sigma_t, dist)` | `RtWater.hlsl`, `RawMaterials3D.hlsli`, `RawCosmosSky.hlsli` | `exp(-max(sigma_t, 0) * max(dist, 0))` | Glacial blue (`0.045/m`, `2.0 m`) = `0.9139` |
| 4 | `davinci_pipe_radius(r_child_a, r_child_b, exponent_p)` | `src/worldgen/RawFloraEngine.cpp` (L650-L684) | `(r_a^p + r_b^p)^(1/p)`, `p in [1.8, 3.5]` | Equal `0.30 m` children (`p=2.5`) -> `0.3959 m` |
| 5 | `xpbd_lambda_delta(c_val, lambda_prev, inv_mass_sum, compliance, sub_dt)` | `src/d3d12_bridge/shaders/RawXpbdClothSim.hlsl` (L308-L334) | `alpha_tilde = alpha / dt^2`; `(-C - alpha_tilde*lambda) / (w + alpha_tilde)` | `-0.00972` restoring step, zero div-by-zero |
| 6 | `crafting_cap_mult(skill_0_100, fortify_frac, skill_weight, max_cap)` | `src/engine_rt/rt_progression_economy.cpp` (L132-L150) | `1 + span * tanh(raw_gain / span)`, `cap <= 2.25` | Master `1.9915x`; extreme exploit hard-capped at `2.25x` |

### 2. Atmosphere and Meteorology Kernels (`native/bld/RawAtmoWeather.bld`)

| # | BuildLang Function | Engine C++ / HLSL Counterpart | Governing Equation | Verified Invariant |
| --- | --- | --- | --- | --- |
| 7 | `rayleigh_density(h_km)` | `src/d3d12_bridge/shaders/AtmosphereLut.hlsl` (L41) | `exp(-max(h_km, 0) / 8.0)` | `1.0` at sea level, `1/e = 0.3679` at `8.0 km` |
| 8 | `mie_density(h_km, mie_scale_h, turbidity)` | `src/d3d12_bridge/shaders/AtmosphereLut.hlsl` (L42) | `exp(-max(h_km, 0) / max(mie_scale_h, 0.8)) * turbidity` | `1.5 / e = 0.5518` at `h=1.2 km, turb=1.5` |
| 9 | `ozone_density(h_km)` | `src/d3d12_bridge/shaders/AtmosphereLut.hlsl` (L44-L46) | Asymmetric cubic smoothstep over `[10, 42] km`, peak `25 km` | `1.0` at `25 km`, `0.0` at `<= 10 km` |
| 10 | `solar_declination_rad(day_of_year, axial_tilt_deg)` | `src/worldgen/RawWeatherSim.cpp` (L95-L100) | `tilt_rad * sin(2*pi * (doy - 80) / 365)` | Summer solstice day 172 = `+0.4091 rad` (`+23.44 deg`) |
| 11 | `solar_cos_zenith(game_hour, latitude_deg, declination_rad)` | `src/worldgen/RawWeatherSim.cpp` (L116-L123) | `sin(lat)*sin(dec) + cos(lat)*cos(dec)*cos(hour_angle)` | Solar noon (`12.5h`, `60N`, solstice) = `0.8032` |
| 12 | `snowpack_step_m(depth_m, temp_c, snow_fall_m_per_hr, q_solar_wm2, albedo, dt_hours)` | `src/worldgen/RawWeatherSim.cpp` (snowpack thermal ODE) | Accumulates when `T <= 1.5 C`; melts via degree-hour + `(1-albedo)*Q_solar` fusion | `0.50 m -> 0.4231 m` (warm melt), `0.50 m -> 0.58 m` (cold snow) |
| 13 | `black_ice_refreeze(prev_ice01, temp_c, melt_water_m, dt_sec)` | `RawWeatherSim.cpp` & `RawWorldEcosystem.cpp` | Exponential refreeze below `-1.5 C` with standing water; thaws above `+0.5 C` | `0.0 -> 0.7383` over `30 s` at `-8 C` |

### 3. Biomechanics, Hydrology and Phenology Kernels (`native/bld/RawBiomeFlora.bld`)

| # | BuildLang Function | Engine C++ / HLSL Counterpart | Governing Equation | Verified Invariant |
| --- | --- | --- | --- | --- |
| 14 | `wind_deflection_m(wind_mps, length_m, radius_m, young_gpa, tier_0_to_3)` | `src/worldgen/RawFloraEngine.cpp` (cantilever wind shear) | `tier_scale * (q_wind * L^4) / (8 * E * I)`, `I = (pi/4)*r^4` | `0.0088 m` trunk & `0.0065 m` bough tip deflection at `18 m/s` |
| 15 | `snow_branch_sag_m(snow_load_kg_m2, branch_len_m, radius_m, young_gpa, foliage_width_m)` | `src/worldgen/RawFloraEngine.cpp` (winter canopy sag) | `(load * g * width * L^4) / (8 * E * I)` | `0.0418 m` droop under `32 kg/m^2` wet snowpack |
| 16 | `stream_power_erosion(erodibility_k, discharge_q, slope_s, m_exp, n_exp)` | `src/worldgen/RawHydrologyGen.cpp` (fluvial incision) | `K * Q^m * S^n` (Whipple & Tucker stream-power law) | Exact `0.015 * sqrt(4.0) * 0.35 = 0.0105` |
| 17 | `autumn_phenology_blend(day_of_year, day_length_hr, temp_c)` | `src/worldgen/RawWeatherSim.cpp` (phenology0 senescence) | Seasonal window gated by photoperiod (`< 12.2 h`) & chill (`< 11 C`) | `0.0` in summer (`doy=185`), `0.7562` in October (`doy=288`) |
| 18 | `phototropism_weight(height_frac01, canopy_occlusion01, sun_dot)` | `src/worldgen/RawFloraEngine.cpp` (phototropism growth bias) | `(0.25 + 0.75*smoothstep(h)) * (1 - occ) * (0.35 + 0.65*sun_dot)` | Sunlit crown top `0.8185` vs shaded lower limb `0.0226` |

### 4. Rendering Optics and Post-Processing Kernels (`native/bld/RawRenderOptics.bld`)

| # | BuildLang Function | Engine C++ / HLSL Counterpart | Governing Equation | Verified Invariant |
| --- | --- | --- | --- | --- |
| 19 | `fresnel_schlick(f0, cos_theta)` | `src/d3d12_bridge/shaders/StrataBRDF.hlsli`, `Lighting.hlsl` | `f0 + (1 - f0) * (1 - cos_theta)^5` | Dielectric `0.04` at normal incidence, `0.7828` at grazing `cos=0.05` |
| 20 | `ggx_ndf(n_dot_h, roughness)` | `src/d3d12_bridge/shaders/StrataBRDF.hlsli` (L118-L125) | `alpha^2 / (pi * ((n_dot_h^2)*(alpha^2 - 1) + 1)^2)`, `alpha = r^2` | Peak lobe `21.2118` at `n_dot_h=1.0, r=0.35` |
| 21 | `multi_scatter_boost(f0, nv, roughness)` | `src/d3d12_bridge/shaders/StrataBRDF.hlsli` (L87-L104) | `1 + f0 * (1 / E_ss(nv, r) - 1)` (Turquin / Kulla-Conty) | Rough brushed iron (`F0=0.56, nv=0.20, r=0.85`) receives `1.4916x` compensation |
| 22 | `burley_diffusion(r, d)` | `src/d3d12_bridge/shaders/Sss.hlsl` | `(exp(-r/d) + exp(-r/(3*d))) / (8 * pi * d * r)` | Positive normalized diffusion (`0.1156` at `d=1.0`, `0.2068` at `d=0.22`) |
| 23 | `agx_sigmoid(x_norm)` | `src/d3d12_bridge/shaders/Tonemap.hlsl` (L46-L48) | 6th-order C2 AgX polynomial over normalized log2 `[0, 1]` | Monotonic midtone `0.4843` (`x=0.60`), smooth shoulder `0.9762` (`x=0.95`) |
| 24 | `multi_bounce_ao_ch(ao, albedo_ch)` | `src/d3d12_bridge/shaders/AoComposite.hlsl` (L47-L55) | `max(ao, ao * (a*ao^2 + b*ao + c))` (Jimenez 2016) | Snow (`0.88`) lifts `ao=0.35 -> 0.7146`; dark (`0.08`) stays `0.35` |

### 5. Fluid Dynamics, Volumetrics, Acoustics and Shadow Kernels (`native/bld/RawFluidDynamics.bld`)

| # | BuildLang Function | Engine C++ / HLSL Counterpart | Governing Equation | Verified Invariant |
| --- | --- | --- | --- | --- |
| 25 | `buoyancy_accel(temp_k, ambient_k, smoke_density, alpha_buoy, beta_smoke)` | `src/d3d12_bridge/shaders/RawFluidFireForces.hlsl` | `alpha * (T - T_amb) / T_amb - beta * rho_smoke` | `1800 K` torch plume yields `+50.276 m/s^2` net upward buoyancy |
| 26 | `swe_wave_celerity(gravity_mps2, depth_m)` | `src/d3d12_bridge/shaders/RawFluidWaterStep.hlsl` | `c = sqrt(g * h)` | `1.5 m` river depth wave speed = `3.836 m/s` |
| 27 | `cfl_dt_limit(cell_size_m, max_vel_mps, cfl_safety)` | `RawFluidSimHost.cpp`, `RawFluidFireAdvect.hlsl` | `cfl_safety * dx / max(v, eps)` | `dx=0.125 m, v=12 m/s, cfl=0.8` -> `dt <= 8.333 ms` |
| 28 | `froxel_slice_scatter(in_scatter, sigma_t, slice_thickness_m)` | `FroxelPass.hlsl`, `RawVolumetricFroxelSim.hlsl` | `S * (1 - exp(-sigma_t * ds)) / sigma_t` (Taylor limit as `sigma_t -> 0`) | `S=0.45, ext=0.08, ds=2.5 m` integrates to `1.0196` |
| 29 | `sabine_rt60_s(volume_m3, surface_area_m2, mean_alpha)` | `src/audio/RawAudioDsp.cpp` (FDN-8 room calibration) | `-0.161 * V / (S * ln(1 - alpha))` (Sabine-Eyring) | Nordic stone crypt (`V=1800, S=960, alpha=0.08`) = `3.620 s` RT60 |
| 30 | `pcss_penumbra_radius(receiver_z, blocker_z, light_size)` | `src/d3d12_bridge/shaders/RawShadowPcssSim.hlsl` | `max(z_rec - z_blk, 0) * light_size / max(z_blk, eps)` | Contact-hardening: `0.025` near blocker vs `0.40` far (`16x` softer) |

## Reproducing All 5 BuildLang Module Checks

```powershell
foreach ($m in @('RawPhysicsKernels','RawAtmoWeather','RawBiomeFlora','RawRenderOptics','RawFluidDynamics')) {
    buildc check "E:\wt\raw-rt9\native\bld\$m.bld"
    buildc run   "E:\wt\raw-rt9\native\bld\$m.bld"
}
```
