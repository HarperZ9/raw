# RAW Engine BuildLang Core Kernel Map (8 Modules, 48 Verified Kernels)

This document maps RAW Engine's deterministic C++23 and HLSL SM 6.6/6.10 scalar physics, atmosphere, meteorology, biomechanics, PBR optics, fluid dynamics, acoustics, shadows, combat/AI, regional economy, and GPU pipeline kernels to their verified **BuildLang 1.4.0** counterparts under `native/bld/` (**8 modules, 48 verified kernels**):

1. [`native/bld/RawPhysicsKernels.bld`](../native/bld/RawPhysicsKernels.bld) (6 core scalar physics & anti-exploit kernels)
2. [`native/bld/RawAtmoWeather.bld`](../native/bld/RawAtmoWeather.bld) (7 planetary atmosphere, orbital solar, snowpack ODE & black-ice kernels)
3. [`native/bld/RawBiomeFlora.bld`](../native/bld/RawBiomeFlora.bld) (5 botanical biomechanics, fluvial erosion & phenology kernels)
4. [`native/bld/RawRenderOptics.bld`](../native/bld/RawRenderOptics.bld) (6 PBR BRDF, multi-scatter GGX, Christensen-Burley SSS, AgX & multi-bounce AO kernels)
5. [`native/bld/RawFluidDynamics.bld`](../native/bld/RawFluidDynamics.bld) (6 buoyancy, shallow-water, CFL, froxel scattering, Sabine-Eyring RT60 & PCSS kernels)
6. [`native/bld/RawCombatAi.bld`](../native/bld/RawCombatAi.bld) (6 timed-parry, arrow ballistics, ragdoll kinetic cap, block mitigation, stealth & 3D Octile A* kernels)
7. [`native/bld/RawEconomySim.bld`](../native/bld/RawEconomySim.bld) (6 9-hold scarcity pricing, Rothermel wildfire, blizzard caravan speed, cold-water breath, wet-elemental & GTAO kernels)
8. [`native/bld/RawGpuPipeline.bld`](../native/bld/RawGpuPipeline.bld) (6 froxel Z-slice, cascade texel-snap, SSR edge fade, PI frame governor, clustered sphere-AABB & event bounty kernels)

## Toolchain Verification Receipts (`buildc 1.4.0`)

Verified directly on this workstation with `C:\Users\Zain\.cargo\bin\buildc.exe` (`buildc 1.4.0`, **8/8 `check` PASS**, **8/8 `run` PASS**):

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

### Module 6: `native/bld/RawCombatAi.bld`
- `buildc check`: Exit Code `0` (`Lexing... OK (1090 tokens)`, `Parsing... OK (11 items)`, `Type checking... OK`, `No errors found`)
- `buildc run`: Exit Code `0`
  ```text
  parry_posture_damage(25ms) = 1.0397766012396694 | (250ms miss) = 0
  arrow_drop_z(1.0s, g=9.81, wind=10) = -4.84968526966475
  ragdoll_impulse_n(arrow) = 6.86335870404108 | (10k dmg cap) = 47.99999996503734
  block_mitigation(shield) = 0.7695 | (weapon) = 0.505
  stealth_audible_radius(dry) = 231.86432639999995 | (rainstorm) = 133.68612095999995
  octile_3d_heuristic(640, 384, 128) = 843.8580079512685
  ```

### Module 7: `native/bld/RawEconomySim.bld`
- `buildc check`: Exit Code `0` (`Lexing... OK (945 tokens)`, `Parsing... OK (9 items)`, `Type checking... OK`, `No errors found`)
- `buildc run`: Exit Code `0`
  ```text
  hold_commodity_price(surplus) = 7.367531668195611 | (scarce) = 24.622888266898325
  wildfire_spread_rate(calm) = 0.3435833333333333 | (gale+upslope) = 2.942056746453832
  caravan_weather_speed(clear) = 4.5 | (blizzard) = 0
  cold_water_breath_s(14C) = 45 | (-1C glacial) = 20.531250000000004
  elemental_wet_mult(shock wet) = 72.5 | (fire wet) = 37.5
  gtao_slice_vis(open hemi) = 1 | (occluded corner) = 0.22984884706593012
  ```

### Module 8: `native/bld/RawGpuPipeline.bld`
- `buildc check`: Exit Code `0` (`Lexing... OK (983 tokens)`, `Parsing... OK (12 items)`, `Type checking... OK`, `No errors found`)
- `buildc run`: Exit Code `0`
  ```text
  froxel_log_slice(24) = 0 | (1000) = 31.466387455277125
  cascade_texel_snap(+12.37, 0.25) = 12.25 | (-12.37, 0.25) = -12.5
  ssr_edge_fade(center) = 1 | (edge 0.03) = 0.15625
  frame_governor_step(22ms over) = 0.92000065999868 | (11ms under) = 0.8656000791998416
  sphere_aabb_dist_sq(inside) = 0 | (outside 3,0,4) = 25
  event_scarcity_reward(150g, 1.8x, urg=0.75) = 349.08750000000003
  ```

## Kernel-by-Kernel Mapping (48 Kernels)

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

### 6. Combat, Ballistics, Stealth and Pathfinding Kernels (`native/bld/RawCombatAi.bld`)

| # | BuildLang Function | Engine C++ / HLSL Counterpart | Governing Equation | Verified Invariant |
| --- | --- | --- | --- | --- |
| 31 | `parry_posture_damage(base_dmg, parry_dt_ms, window_ms, guard_max)` | `src/engine_rt/rt_movement_combat.cpp` | `dmg * (1.25 + 1.35 * (1 - dt/win)^2) / guard_max` inside `220 ms` window | `25 ms` parry breaks guard (`1.0398`); `250 ms` late miss = `0.0` |
| 32 | `arrow_drop_z(flight_time_s, gravity_mps2, wind_mps, drag_k)` | `rt_ai_packages.cpp`, `rt_magic_physics.cpp` | `-0.5 * g * t^2 * (0.65 + 0.35 * exp(-0.5*k*t)) - 0.015*|w|*t^2*k` | `1.0 s` flight under `9.81 m/s^2` & `10 m/s` wind drops `-4.850 m` |
| 33 | `ragdoll_impulse_n(proj_mass_kg, impact_speed_mps, damage_amt, is_ranged)` | `src/engine_rt/rt_physics_camera_gmst.cpp` (L145-L167) | `ceiling * tanh(raw_imp / ceiling)` (`22 N` ranged, `48 N` melee) | Arrow = `6.863 N`; `10,000`-dmg melee smoothly capped `< 48.0 N` |
| 34 | `block_mitigation(block_skill, shield_armor, is_shield, max_cap)` | `src/engine_rt/rt_physics_camera_gmst.cpp` (L169-L195) | Shield `0.45 + 0.25*s + 0.0022*armor` vs weapon `0.28 + 0.30*s`, cap `<= 0.85` | Skill 75 + armor 60 shield = `0.7695` vs weapon `0.505` |
| 35 | `stealth_audible_radius(boot_weight, sneak_skill, rain01, wind_bft, is_running)` | `src/engine_rt/rt_stealth_social_gmst.cpp` (L150-L167) | `(8 + 0.68*boot) * moveMult * (1 - 0.55*s) * weatherMask * 28` | Dry `231.9u` damped to `133.7u` in heavy rainstorm |
| 36 | `octile_3d_heuristic(dx, dy, dz)` | `src/worldgen/RawNavMeshAi.cpp` | `max(|dx|,|dy|) + (sqrt(2)-1)*min(|dx|,|dy|) + 0.35*|dz|` | Admissible 3D Octile distance `(640,384,128)` = `843.86u` |

### 7. Regional Economy, Wildfire, Survival and GTAO Kernels (`native/bld/RawEconomySim.bld`)

| # | BuildLang Function | Engine C++ / HLSL Counterpart | Governing Equation | Verified Invariant |
| --- | --- | --- | --- | --- |
| 37 | `hold_commodity_price(base_price, stock, target_stock)` | `src/worldgen/RawSettlementSim.cpp` | `base * clamp((target / max(stock, 0.05*target))^0.65, 0.45, 3.25)` | Surplus `7.37g` vs scarce `24.62g` (`3.34x` scarcity signal) |
| 38 | `wildfire_spread_rate(base_rate_mps, wind_mps, slope_tan, moisture01)` | `src/worldgen/RawWorldEcosystem.cpp` | `base * (1 - m/0.72) * (1 + 0.24*v^1.35 + 5.275*slope^2)` | Calm `0.344 m/s` accelerates to `2.942 m/s` under gale + upslope |
| 39 | `caravan_weather_speed(base_kmh, blizzard01, pass_exposure01)` | `src/worldgen/RawSettlementSim.cpp` | `0.0` when `blizzard01 > 0.60`; smooth exposure drag below | Clear `4.5 km/h` -> snowbound `0.0 km/h` at `blizzard01=0.85` |
| 40 | `cold_water_breath_s(base_breath_s, water_temp_c, frost_resist01)` | `rt_world_rules_gmst.cpp`, `rt_stealth_survival.cpp` | `base * (1 - chill01 * 0.58 * (1 - resist))` | Warm `45.0 s` drops to `20.53 s` in `-1 C` Sea of Ghosts |
| 41 | `elemental_wet_mult(base_dmg, wetness01, elem_kind)` | `rt_magic_physics.cpp`, `RawSpells3D.hlsli` | Shock `1+0.45*w`, Frost `1+0.30*w`, Fire `1-0.25*w` | Soaked `50` dmg -> Shock `72.5`, Fire `37.5` |
| 42 | `gtao_slice_vis(h1_rad, h2_rad, n_rad)` | `src/d3d12_bridge/shaders/RawGtaoSim.hlsl` | `0.25 * sum(-cos(2*h_i - n) + cos(n) + 2*h_i*sin(n))` | Open hemisphere `1.000`; occluded corner `[-0.5, 0.5]` = `0.2298` |

### 8. GPU Pipeline and Frame-Pacing Kernels (`native/bld/RawGpuPipeline.bld`)

| # | BuildLang Function | Engine C++ / HLSL Counterpart | Governing Equation | Verified Invariant |
| --- | --- | --- | --- | --- |
| 43 | `froxel_log_slice(view_z, z_near, z_far, num_slices)` | `LightingCommon.hlsli` (L64-L67), `RawVolumetricFroxelSim.hlsl` | `clamp(log(z / z_near) / log(z_far / z_near), 0, 1) * (N - 1)` | Exact `0.0` at `z_near=24`; logarithmic mid-slice `31.47` at `z=1000` |
| 44 | `cascade_texel_snap(coord_ls, texel_size_ls)` | `src/d3d12_bridge/RawShadowPcssHost.cpp` | `floor(coord / texel_size) * texel_size` | Exact texel snapping: `+12.37 -> +12.25`, `-12.37 -> -12.50` (`0.25` step) |
| 45 | `ssr_edge_fade(u, v, margin)` | `RawSsrSim.hlsl`, `EnvCubePass.hlsl` | `smoothstep(0, margin, min(min(u,1-u), min(v,1-v)))` | Center `(0.5,0.5)` = `1.0`; border `(0.03,0.5)` fades smoothly to `0.1563` |
| 46 | `frame_governor_step(cur_scale, frame_ms, target_ms, kp_down, kp_up)` | `src/engine_rt/rt_frame_governor.cpp` | Asymmetric PI step clamped `[0.50, 1.00]` (`kp_down > kp_up`) | `22 ms` drops `1.00 -> 0.920`; `11 ms` gently lifts `0.85 -> 0.866` |
| 47 | `sphere_aabb_dist_sq(cx, cy, cz, min_x..max_z)` | `src/d3d12_bridge/shaders/RawLightClusterSim.hlsl` | Exact Arvo squared distance from clamped AABB point | Inside box = `0.0`; outside `(3,0,4)` = exact `25.0` (`5^2`) |
| 48 | `event_scarcity_reward(base_gold, price_ratio, urgency01)` | `src/worldgen/RawWorldEventDirector.cpp` | `base * (0.75 + 0.55*price_ratio) * (1 + 0.45*urgency)` | `150g` base at `1.8x` scarcity & `0.75` urgency = `349.1g` |

## Reproducing All 8 BuildLang Module Checks

```powershell
foreach ($m in @('RawPhysicsKernels','RawAtmoWeather','RawBiomeFlora','RawRenderOptics','RawFluidDynamics','RawCombatAi','RawEconomySim','RawGpuPipeline')) {
    buildc check "E:\wt\raw-rt9\native\bld\$m.bld"
    buildc run   "E:\wt\raw-rt9\native\bld\$m.bld"
}
```
