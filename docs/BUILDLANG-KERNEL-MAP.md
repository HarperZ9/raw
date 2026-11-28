# RAW Engine BuildLang Core Kernel Map (16 Modules, 96 Verified Kernels)

This document maps RAW Engine's deterministic C++23 and HLSL SM 6.6/6.10 scalar physics, atmosphere, meteorology, biomechanics, PBR optics, fluid dynamics, acoustics, shadows, combat/AI, regional economy, GPU pipeline, world-systems, volumetric clouds, archery, dungeon, animation IK, cinematic post-FX, profile-tiering, GPU particle dynamics, combat-mechanics, TSR upscaling, quest persuasion, water caustics, crafting, spellcraft, snow deformation, seasonal ecology, and algorithmic game-theory / information-theory kernels to their verified **BuildLang 1.4.0** counterparts under `native/bld/` (**16 modules, 96 verified kernels**):

1. [`native/bld/RawPhysicsKernels.bld`](../native/bld/RawPhysicsKernels.bld) (6 core scalar physics & anti-exploit kernels)
2. [`native/bld/RawAtmoWeather.bld`](../native/bld/RawAtmoWeather.bld) (7 planetary atmosphere, orbital solar, snowpack ODE & black-ice kernels)
3. [`native/bld/RawBiomeFlora.bld`](../native/bld/RawBiomeFlora.bld) (5 botanical biomechanics, fluvial erosion & phenology kernels)
4. [`native/bld/RawRenderOptics.bld`](../native/bld/RawRenderOptics.bld) (6 PBR BRDF, multi-scatter GGX, Christensen-Burley SSS, AgX & multi-bounce AO kernels)
5. [`native/bld/RawFluidDynamics.bld`](../native/bld/RawFluidDynamics.bld) (6 buoyancy, shallow-water, CFL, froxel scattering, Sabine-Eyring RT60 & PCSS kernels)
6. [`native/bld/RawCombatAi.bld`](../native/bld/RawCombatAi.bld) (6 timed-parry, arrow ballistics, ragdoll kinetic cap, block mitigation, stealth & 3D Octile A* kernels)
7. [`native/bld/RawEconomySim.bld`](../native/bld/RawEconomySim.bld) (6 9-hold scarcity pricing, Rothermel wildfire, blizzard caravan speed, cold-water breath, wet-elemental & GTAO kernels)
8. [`native/bld/RawGpuPipeline.bld`](../native/bld/RawGpuPipeline.bld) (6 froxel Z-slice, cascade texel-snap, SSR edge fade, PI frame governor, clustered sphere-AABB & event bounty kernels)
9. [`native/bld/RawWorldSystems.bld`](../native/bld/RawWorldSystems.bld) (6 log-luminance histogram, eye adaptation, diplomacy pricing, guard posture, audio crossfade & Purkinje night-vision kernels)
10. [`native/bld/RawCloudsDungeon.bld`](../native/bld/RawCloudsDungeon.bld) (6 dual-lobe HG clouds, Beer-Powder scattering, spherical cloud shell, bow draw power, dungeon connectivity & compass marker kernels)
11. [`native/bld/RawAnimKinematics.bld`](../native/bld/RawAnimKinematics.bld) (6 2-bone IK knee angle, critically damped spring-damper, CoC bokeh radius, motion-blur clamp, VRAM budget tier & shout cooldown kernels)
12. [`native/bld/RawParticlesCombat.bld`](../native/bld/RawParticlesCombat.bld) (6 particle bounce, curl-wind drag, timed-parry stagger, wet/frost elemental damage, armor mitigation & poise recovery kernels)
13. [`native/bld/RawTsrQuests.bld`](../native/bld/RawTsrQuests.bld) (6 Halton-2/3 jitter, YCoCg variance clamp, RCAS sharpening lobe, quest persuasion, smithing temper & encumbrance speed kernels)
14. [`native/bld/RawCausticsCrafting.bld`](../native/bld/RawCausticsCrafting.bld) (6 Snell refraction, underwater Beer-Lambert transmittance, caustic Jacobian clamp, anti-loop crafting cap, soul-gem budget & dual-cast efficiency kernels)
15. [`native/bld/RawSnowEcology.bld`](../native/bld/RawSnowEcology.bld) (6 snow footprint rim displacement, campfire snow melt, slush-to-black-ice freeze, storm shelter probability, camera spring zoom & seasonal photoperiod kernels)
16. [`native/bld/RawGameTheoryMath.bld`](../native/bld/RawGameTheoryMath.bld) (6 2x2 unilateral Nash regret, closed-form mixed equilibrium, fictitious-play EMA, subadditive prophet threshold, truncated discrete Laplace & binary KL-divergence stability kernels)

## Toolchain Verification Receipts (`buildc 1.4.0`)

Verified directly on this workstation with `C:\Users\Zain\.cargo\bin\buildc.exe` (`buildc 1.4.0`, **16/16 `check` PASS**, **16/16 `run` PASS**):

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

### Module 9: `native/bld/RawWorldSystems.bld`
- `buildc check`: Exit Code `0` (`Lexing... OK (857 tokens)`, `Parsing... OK (10 items)`, `Type checking... OK`, `No errors found`)
- `buildc run`: Exit Code `0`
  ```text
  log_lum_hist_bin(dark) = 0 | (0.18 mid) = 117
  eye_adapt_step(to sun) = 0.551251774896036 | (to cave) = 0.8277942154240827
  diplomacy_price_mult(hero) = 0.883 | (outlaw) = 1.35
  bounty_guard_posture(clean) = 0 | (3500g) = 3
  audio_crossfade_gain(0.5 in/out) = 0.7071067811865475 | 0.7071067811865476 | sumSq = 1
  purkinje_night_weight(night 0.004) = 0.19223593964334704 | (day 0.25) = 0
  ```

### Module 10: `native/bld/RawCloudsDungeon.bld`
- `buildc check`: Exit Code `0` (`Lexing... OK (1198 tokens)`, `Parsing... OK (11 items)`, `Type checking... OK`, `No errors found`)
- `buildc run`: Exit Code `0`
  ```text
  cloud_dual_hg(fwd 0.95) = 0.4971514897291038 | (back -0.75) = 0.04397230100970019
  beer_powder_scatter(sun tau=0.60) = 0.5542669825852977 | (edge tau=0.15) = 0.45652335777703124
  cloud_shell_hit_dist(zenith 1.5km) = 1.5 | (horizon 0) = 138.13851743811355
  bow_draw_power(half) = 0.604989831816444 | (full) = 1
  dungeon_conn_ratio(mst) = 1 | (loop) = 1.3333333333333333 | (broken) = 0.4666666666666666
  compass_marker_scale(220m clear) = 0.798027875783642 | (220m fog 0.85) = 0.4165139923901944
  ```

### Module 11: `native/bld/RawAnimKinematics.bld`
- `buildc check`: Exit Code `0` (`Lexing... OK (1122 tokens)`, `Parsing... OK (11 items)`, `Type checking... OK`, `No errors found`)
- `buildc run`: Exit Code `0`
  ```text
  ik_two_bone_knee_rad(90deg) = 1.5708638535897932 | (near-ext) = 2.843173036743716
  spring_damper_step(0.1s) = 0.1912078645890012 | (1.0s) = 0.9969808363488774
  coc_bokeh_radius_px(focus) = 0 | (far 50m) = 5.113636363636364 | (near 2m) = -8.522727272727273
  motion_blur_clamp(norm) = 5 | (clamped) = 24
  vram_budget_tier(ultra) = 0 | (safemode) = 3
  shout_cooldown_sec(1-word talos) = 12 | (3-word talos) = 25.8
  ```

### Module 12: `native/bld/RawParticlesCombat.bld`
- `buildc check`: Exit Code `0` (`Lexing... OK (836 tokens)`, `Parsing... OK (9 items)`, `Type checking... OK`, `No errors found`)
- `buildc run`: Exit Code `0`
  ```text
  particle_bounce_vz(-8.0) = 3.6 | (rest -0.8) = 0
  curl_wind_drag(calm) = 0.9516099923790157 | (gale) = 0.8728426324887193
  timed_parry_stagger(22ms) = 88.6 | (260ms miss) = 0
  weather_elem_damage(shock wet) = 72.5 | (frost wet+ice) = 70 | (fire wet) = 37.5
  armor_damage_mitigation(300 pen50%) = 0.18 | (1200 cap) = 0.8
  poise_recovery_step(hitstun) = 35 | (recovering) = 47.5
  ```

### Module 13: `native/bld/RawTsrQuests.bld`
- `buildc check`: Exit Code `0` (`Lexing... OK (896 tokens)`, `Parsing... OK (10 items)`, `Type checking... OK`, `No errors found`)
- `buildc run`: Exit Code `0`
  ```text
  halton_jitter_offset(1,2) = 0 | (2,2) = -0.25 | (1,3) = -0.16666666666666669
  ycocg_clamp_weight(in-box) = 1 | (disoccluded) = 0.14285714285714288
  rcas_lobe_weight(mid) = -0.12727272727272726 | (high-contrast) = -0.004081632653061225
  quest_persuasion_prob(novice) = 0.05 | (master) = 0.9763394218445388
  smithing_temper_bonus(50) = 7.123234724693209 | (100+perk) = 26.783610384750833
  encumbrance_speed_mult(light) = 1 | (heavy) = 0.88 | (over) = 0.42000000000000004
  ```

### Module 14: `native/bld/RawCausticsCrafting.bld`
- `buildc check`: Exit Code `0` (`Lexing... OK (743 tokens)`, `Parsing... OK (9 items)`, `Type checking... OK`, `No errors found`)
- `buildc run`: Exit Code `0`
  ```text
  snell_refract_cos(air->water 60deg) = 0.7602065399429697 | (water->air TIR) = 0
  water_spectral_trans(3m red) = 0.2175122735348992 | (3m blue) = 0.8971968385743043
  caustic_jacobian_clamp(focus 4x) = 4 | (singularity cap) = 6
  crafting_anti_loop_mult(legit) = 1.8572613277863683 | (restoration-loop cap) = 2.25
  soul_gem_cast_charges(grand nov) = 54.495912806539515 | (grand master) = 111.11111111111113
  dual_cast_eff_ratio(2.2x / 2.8x) = 0.7857142857142857
  ```

### Module 15: `native/bld/RawSnowEcology.bld`
- `buildc check`: Exit Code `0` (`Lexing... OK (1146 tokens)`, `Parsing... OK (11 items)`, `Type checking... OK`, `No errors found`)
- `buildc run`: Exit Code `0`
  ```text
  snow_rim_displace(center) = -0.16 | (rim peak) = 0.055999999999999994
  campfire_snow_melt(1m 60s) = 0.0008465011260362349 | (8m 60s) = 0
  slush_black_ice_frac(freeze -8C) = 0.9137064135006295 | (thaw +6C) = 0.07574669123157933
  storm_shelter_prob(calm) = 0.027990511187118247 | (blizzard) = 0.99
  camera_spring_zoom(220->420, 0.1s) = 310.2376727811947
  seasonal_photoperiod_hr(60N sum/eq/win) = 18.488845693795454 | 11.99948413328645 | 5.511154306204544
  ```

### Module 16: `native/bld/RawGameTheoryMath.bld`
- `buildc check`: Exit Code `0` (`Lexing... OK (997 tokens)`, `Parsing... OK (10 items)`, `Type checking... OK`, `No errors found`)
- `buildc run`: Exit Code `0`
  ```text
  bimatrix_unilateral_regret(NE 0.5,0.5) = 0 | (0.7,0.5 eps) = 0.19999999999999996
  nash_2x2_mixed_prob(pennies) = 0.5 | (feint) = 0.5833333333333333
  fictitious_play_ema_step(n=4) = 0.6 | (n=50 floor) = 0.525
  prophet_reservation_threshold(add) = 200 | (subadd) = 164.8
  truncated_discrete_laplace_w(k=2) = 0.36787944117144233 | (k=7 trunc) = 0
  kl_binary_stability_nats(0.5||0.5) = 0 | (0.75||0.50) = 0.13081203594113697
  ```

## Kernel-by-Kernel Mapping (96 Kernels)

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

### 9. World Systems, Diplomacy, Audio and Auto-Exposure Kernels (`native/bld/RawWorldSystems.bld`)

| # | BuildLang Function | Engine C++ / HLSL Counterpart | Governing Equation | Verified Invariant |
| --- | --- | --- | --- | --- |
| 49 | `log_lum_hist_bin(lum, min_log2, inv_log2_range)` | `src/d3d12_bridge/shaders/RawAutoExposureSim.hlsl` | Bin `0` if `lum <= 1e-5`, else `[1..255]` from normalized `log2(lum)` | Near-black -> bin `0`; mid-grey `0.18` -> bin `117` |
| 50 | `eye_adapt_step(cur_lum, target_lum, dt_sec, speed_up, speed_down)` | `src/d3d12_bridge/shaders/RawAutoExposureSim.hlsl` | Asymmetric EMA `cur + (tgt - cur) * (1 - exp(-rate * dt))` | Faster bright-sun adaptation (`0.05 -> 0.551`) vs slower cave entry (`1.0 -> 0.828`) |
| 51 | `diplomacy_price_mult(reputation, bounty_gold)` | `src/worldgen/RawFactionDiplomacy.cpp` | `clamp(1 - 0.18*(rep/100) + min(bounty/4000, 0.25), 0.82, 1.35)` | Hero (`+65`) gets `0.883x` discount; outlaw (`-60, 2500g`) capped at `1.35x` |
| 52 | `bounty_guard_posture(bounty_gold, reputation)` | `src/worldgen/RawFactionDiplomacy.cpp` | Thresholds: `0` Peaceful, `1` Watchful, `2` Arrest (`>=250g`), `3` KillOnSight (`>=2000g`) | Clean -> `0` (Peaceful); `3500g` -> `3` (KillOnSight) |
| 53 | `audio_crossfade_gain(t01, is_incoming)` | `src/engine_rt/rt_adaptive_audio_director.cpp` | Equal-power `sin(t * pi/2)` incoming / `cos(t * pi/2)` outgoing | At `t=0.5`: `0.7071^2 + 0.7071^2 == 1.0000` exact power preservation |
| 54 | `purkinje_night_weight(scene_lum, max_weight)` | `src/d3d12_bridge/shaders/Tonemap.hlsl` | Smooth scotopic shift strictly below `scene_lum < 0.018`, `0.0` above | Dim night `0.004` = `0.1922`; daylight `0.25` = exact `0.0` |

### 10. Volumetric Clouds, Archery, Dungeon and HUD Kernels (`native/bld/RawCloudsDungeon.bld`)

| # | BuildLang Function | Engine C++ / HLSL Counterpart | Governing Equation | Verified Invariant |
| --- | --- | --- | --- | --- |
| 55 | `cloud_dual_hg(cos_theta, g_fwd, g_back, blend)` | `src/d3d12_bridge/shaders/RawCloudRaymarchSim.hlsl`, `Clouds.hlsl` | `w * HG(mu, g_fwd) + (1 - w) * HG(mu, g_back)` | Forward silver-lining (`0.4972`) vs back lobe (`0.0440`, `11.3x` contrast) |
| 56 | `beer_powder_scatter(optical_depth, cos_theta, powder_gain)` | `src/d3d12_bridge/shaders/RawCloudRaymarchSim.hlsl`, `Clouds.hlsl` | `exp(-tau) * (1 + pw * (2*(1 - exp(-2*tau)) - 1))` | Bright sunward silver lining (`0.5543`) vs thin dark-edge powder (`0.4565`) |
| 57 | `cloud_shell_hit_dist(planet_r, obs_alt, shell_alt, ray_y)` | `src/d3d12_bridge/shaders/RawCloudRaymarchSim.hlsl` | Quadratic ray-sphere shell hit `-r0*mu +/- sqrt((r0*mu)^2 - (r0^2 - R^2))` | Zenith `1.5 km` shell = exact `1.5 km`; horizon ray = `138.14 km` |
| 58 | `bow_draw_power(draw_time_s, min_draw_s, full_draw_s, min_power)` | `src/engine_rt/rt_archery_mount_gmst.cpp` | `p_min + (1 - p_min) * clamp((t - t_min)/(t_full - t_min), 0, 1)^1.35` | Half draw = `0.6050`; full draw = exact `1.0` |
| 59 | `dungeon_conn_ratio(room_count, corridor_count, reachable_rooms)` | `src/worldgen/RawDungeonGen.cpp` | `(reachable / N) * clamp(corridors / (N - 1), 0, 2)` | Spanning tree = `1.0`, braided loop = `1.333`, disconnected = `0.467` |
| 60 | `compass_marker_scale(dist_m, near_m, far_m, fog_density01, min_s, max_s)` | `src/engine_rt/rt_archery_mount_gmst.cpp` | Fog-shrunk range + smoothstep proximity scale * weather attenuation | `220 m` clear = `0.7980` vs heavy blizzard (`fog=0.85`) = `0.4165` |

### 11. Inverse Kinematics, Spring Dynamics, Cinematic Post-FX and Profile Kernels (`native/bld/RawAnimKinematics.bld`)

| # | BuildLang Function | Engine C++ / HLSL Counterpart | Governing Equation | Verified Invariant |
| --- | --- | --- | --- | --- |
| 61 | `ik_two_bone_knee_rad(upper_len, lower_len, target_dist)` | `src/worldgen/RawAnimIkSolver.cpp` | Law of cosines `acos((L1^2 + L2^2 - d^2) / (2*L1*L2))` | Right triangle `(0.45, 0.45, 0.45*sqrt(2))` = `1.5709 rad` (`90 deg`); near-ext = `2.843 rad` |
| 62 | `spring_damper_step(pos, vel, target, omega, dt_sec)` | `src/worldgen/RawAnimIkSolver.cpp` | Critically damped analytical `target + (x0 + (v + w*x0)*dt) * exp(-w*dt)` | Unconditionally stable zero-overshoot: `0 -> 0.1912` (`0.1 s`) -> `0.9970` (`1.0 s`) |
| 63 | `coc_bokeh_radius_px(depth_m, focus_m, focal_mm, f_stop, sensor_h_mm, viewport_h_px, max_r)` | `src/d3d12_bridge/shaders/RawPostCinematicSim.hlsl` | Thin-lens signed CoC `0.5 * (f^2/N)*(z - z_f)/(z*(z_f - f)) * px_per_mm` | Exact `0.0 px` at focus; `+5.114 px` far (`50 m`), `-8.523 px` near (`2 m`) |
| 64 | `motion_blur_clamp(vx_px, vy_px, max_vel_px, shutter_scale)` | `src/d3d12_bridge/shaders/RawPostCinematicSim.hlsl` | `min(sqrt(vx^2 + vy^2) * shutter, max_vel_px)` | `(6,8)` @ `0.5x` shutter = exact `5.0 px`; `(90,120)` clamped to `24.0 px` |
| 65 | `vram_budget_tier(vram_used_mb, vram_budget_mb, frame_ms, target_ms)` | `src/engine_rt/rt_profile_presets.cpp` | Pressure thresholds on VRAM & frame-time ratio (`0` Ultra .. `3` SafeMode) | Nominal `4.2/16 GB @ 11.2 ms` -> `0` (Ultra); `15.4/16 GB @ 25 ms` -> `3` (SafeMode) |
| 66 | `shout_cooldown_sec(base_cd_s, recovery_mult, word_count)` | `src/engine_rt/rt_shouts_survival_gmst.cpp` | `max(base * word_scale(1.0/1.45/2.15) * clamp(mult, 0.2, 2.0), 3.0)` | Amulet of Talos (`0.8x`): 1-word = `12.0 s`, 3-word = `25.8 s` |

### 12. GPU Particle Dynamics and Combat Mechanics Kernels (`native/bld/RawParticlesCombat.bld`)

| # | BuildLang Function | Engine C++ / HLSL Counterpart | Governing Equation | Verified Invariant |
| --- | --- | --- | --- | --- |
| 67 | `particle_bounce_vz(vz, restitution, stop_thresh)` | `src/d3d12_bridge/shaders/RawGpuParticleSim.hlsl` | `-vz * clamp(e, 0, 0.98)` above rest threshold, else `0.0` | `-8.0 m/s` @ `e=0.45` bounces to `+3.6 m/s`; `-0.8 m/s` settles to `0.0` |
| 68 | `curl_wind_drag(rel_speed, drag_coeff, dt_sec)` | `src/d3d12_bridge/shaders/RawGpuParticleSim.hlsl` | `exp(-k * (1 + 0.12*v) * dt)` unconditionally in `(0, 1]` | Calm drag `0.9516` vs gale drag `0.8728` (monotonic damping) |
| 69 | `timed_parry_stagger(base_dmg, timing_ms, window_ms, stagger_mult)` | `src/engine_rt/rt_movement_combat.cpp` | `base * (1 + mult * (1 - t/win)^2)` inside `[0, 220 ms]`, else `0.0` | `22 ms` crisp parry = `88.6` stagger; `260 ms` late miss = `0.0` |
| 70 | `weather_elem_damage(base_dmg, wetness01, frost01, elem_id)` | `src/worldgen/RawCombatEngine.cpp`, `rt_magic_physics.cpp` | Wet/frost elemental matrix (Fire dampened, Frost/Shock boosted) | `50` base -> Shock wet `72.5`, Frost wet+ice `70.0`, Fire wet `37.5` |
| 71 | `armor_damage_mitigation(armor_rating, armor_pen01, max_cap)` | `src/worldgen/RawCombatEngine.cpp` | `clamp(armor * (1 - pen) * 0.0012, 0, min(cap, 0.80))` | `300` armor vs `50%` mace pen = `0.18`; `1200` armor hard-capped at `0.80` |
| 72 | `poise_recovery_step(cur_poise, max_poise, since_hit_s, delay_s, regen_rate, dt_sec)` | `src/worldgen/RawCombatEngine.cpp` | Hold during hitstun (`t < delay`), else `min(cur + rate*dt, max)` | Frozen at `35.0` during hitstun (`0.8 s`); regenerates to `47.5` post-delay |

### 13. Temporal Super-Resolution, Quest Persuasion and Crafting Kernels (`native/bld/RawTsrQuests.bld`)

| # | BuildLang Function | Engine C++ / HLSL Counterpart | Governing Equation | Verified Invariant |
| --- | --- | --- | --- | --- |
| 73 | `halton_jitter_offset(index, base)` | `src/d3d12_bridge/shaders/RawTsrUpscaleSim.hlsl` | Radical-inverse Halton(`base`) centered in `[-0.5, +0.5)` | `(1,2)=0.0`, `(2,2)=-0.25`, `(1,3)=-0.1667` |
| 74 | `ycocg_clamp_weight(hist_val, mean_val, sigma_val, gamma)` | `src/d3d12_bridge/shaders/RawTsrUpscaleSim.hlsl` | `1.0` inside `gamma*sigma` box, else `clamp(half_ext / dev, 0.05, 1.0)` | In-box history = `1.0`; disoccluded outlier damped to `0.1429` |
| 75 | `rcas_lobe_weight(luma_min, luma_max, sharpness)` | `src/d3d12_bridge/shaders/RawTsrUpscaleSim.hlsl` | Ringing-safe negative lobe in `[-0.1875, 0.0]` scaled by sharpness | Mid-contrast `-0.1273`; high-contrast edge clamped to `-0.0041` |
| 76 | `quest_persuasion_prob(speech_skill, difficulty, disposition)` | `src/worldgen/RawQuestDialogueSim.cpp` | `clamp(1 / (1 + exp(-0.12*(eff - diff))), 0.05, 0.98)` | Novice (`20` vs `75`) = `0.05`; master (`85, +20` vs `60`) = `0.9763` |
| 77 | `smithing_temper_bonus(smithing_skill, perk_mult, max_bonus)` | `src/engine_rt/rt_crafting_inventory_gmst.cpp` | `cap * tanh(raw / cap)` smooth diminishing-returns ceiling | Skill 50 = `7.12`; Skill 100 + 2x perk = `26.78` (bounded `< 40`) |
| 78 | `encumbrance_speed_mult(carry_wt, max_carry_wt)` | `src/engine_rt/rt_crafting_inventory_gmst.cpp` | `1.0` (`<=75%`), smooth `1.0 -> 0.85` (`75..100%`), walk lock `<=0.45` (`>100%`) | Light `1.0`, heavy (`95%`) `0.88`, over-encumbered (`120%`) `0.42` |

### 14. Water Caustics Optics and Crafting/Spellcraft Kernels (`native/bld/RawCausticsCrafting.bld`)

| # | BuildLang Function | Engine C++ / HLSL Counterpart | Governing Equation | Verified Invariant |
| --- | --- | --- | --- | --- |
| 79 | `snell_refract_cos(cos_i, eta_ratio)` | `src/d3d12_bridge/shaders/RawWaterCausticsSim.hlsl`, `RtWater.hlsl` | `sqrt(1 - eta^2*(1 - cos_i^2))`, returning `0.0` on Total Internal Reflection | Air->water `60 deg` steepens to `cos=0.7602`; water->air TIR = exact `0.0` |
| 80 | `water_spectral_trans(depth_m, sigma_ch, turbidity)` | `src/d3d12_bridge/shaders/RtWater.hlsl`, `RawWaterCausticsSim.hlsl` | `exp(-sigma_ch * (1 + 0.65*turbidity) * depth_m)` | `3 m` depth: red drops to `0.2175` while glacial blue stays `0.8972` |
| 81 | `caustic_jacobian_clamp(area_in, area_out, max_gain)` | `src/d3d12_bridge/shaders/RawWaterCausticsSim.hlsl` | `clamp(area_in / max(area_out, 1e-4), 0, clamp(max_gain, 1, 8))` | `4x` focal convergence = `4.0`; focal singularity clamped cleanly to `6.0` |
| 82 | `crafting_anti_loop_mult(alchemy_skill, fortify_stack, hard_cap)` | `src/engine_rt/rt_crafting_inventory_gmst.cpp` | `1 + span * tanh(raw / span)`, strictly bounded `< min(hard_cap, 2.50)` | Legit master `1.857x`; infinite restoration-loop capped at `2.25x` |
| 83 | `soul_gem_cast_charges(gem_capacity, base_cost, skill_0_100)` | `src/engine_rt/rt_spellcraft_gmst.cpp` | `capacity / max(base_cost * (1 - 0.55*skill01), 1.0)` | Grand gem (`3000`): novice `54.5` casts vs master `111.1` casts (`2.04x`) |
| 84 | `dual_cast_eff_ratio(base_magicka, base_magnitude, cost_mult, mag_mult)` | `src/engine_rt/rt_spellcraft_gmst.cpp` | `(mag_mult / cost_mult)` efficiency relative to single-cast baseline | Canonical 1.7.104 `2.2x / 2.8x` = `0.7857` efficiency per magicka |

### 15. Snow Deformation, Radiant Melt, Shelter AI and Photoperiod Kernels (`native/bld/RawSnowEcology.bld`)

| # | BuildLang Function | Engine C++ / HLSL Counterpart | Governing Equation | Verified Invariant |
| --- | --- | --- | --- | --- |
| 85 | `snow_rim_displace(r_norm, depth_m, rim_gain)` | `src/d3d12_bridge/shaders/RawSnowDeformSim.hlsl` | Bowl `-d*(1 - r^2)^2` inside `r<1`, raised rim `+d*g*sin^2(...)` in `[1, 1.75]` | Center `-0.16 m` depression with `+0.056 m` pushed-out rim peak |
| 86 | `campfire_snow_melt(dist_m, power_kw, air_temp_c, dt_sec)` | `src/d3d12_bridge/shaders/RawSeasonsThermals.hlsli` | Inverse-square radiant flux `P/(4*pi*(d^2+0.35))` minus sub-zero chill / `rho*L_f` | `25 kW` @ `1 m` melts `0.847 mm/min`; `8 m` away melts exact `0.0 m` |
| 87 | `slush_black_ice_frac(prev_ice, temp_c, slush_m, dt_sec)` | `src/d3d12_bridge/shaders/RawSeasonsThermals.hlsli` | Exponential freeze below `-1 C`, thaw above `+0.5 C`, hysteresis in between | `-8 C` freezes `0 -> 0.914` in `10 s`; `+6 C` thaws `0.9 -> 0.076` |
| 88 | `storm_shelter_prob(wind_bft, precip01, temp_c, indoor_dist_m)` | `src/engine_rt/rt_npc_behavior.cpp`, `RawWildlifeBoids.cpp` | Logistic storm severity (wind + precip + chill - shelter distance) in `[0.02, 0.99]` | Calm `14 C` = `0.028`; `-12 C` blizzard = `0.99` shelter-seeking |
| 89 | `camera_spring_zoom(cur_dist, target_dist, stiffness, dt_sec)` | `src/engine_rt/rt_camera_world_gmst.cpp` | Overshoot-free exponential `cur + (target - cur) * (1 - exp(-k*dt))` | `220 -> 420` @ `k=6, 0.1 s` steps smoothly to `310.24` (zero overshoot) |
| 90 | `seasonal_photoperiod_hr(lat_deg, decl_rad)` | `src/worldgen/RawWeatherSim.cpp`, `RawSeasonsThermals.hlsli` | `(24 / pi) * acos(-tan(lat) * tan(decl))` clamped `[0, 24]` | `60 deg N`: summer `18.49 h`, equinox `12.00 h`, winter `5.51 h` |

### 16. Algorithmic Game Theory, Prophet Inequalities and Information Stability Kernels (`native/bld/RawGameTheoryMath.bld`)

| # | BuildLang Function | Engine C++ / Research Counterpart | Governing Equation | Verified Invariant |
| --- | --- | --- | --- | --- |
| 91 | `bimatrix_unilateral_regret(p, q, a00..a11, b00..b11)` | MaximinKiteNE / Adaptive Combat AI (`RawStrategicAiEngine`) | `max(max(u_r0, u_r1) - u_row, max(u_c0, u_c1) - u_col)` | Matching Pennies `(0.5,0.5)` = exact `0.0`; `(0.7,0.5)` = exact `0.20` (`1/5`-NE) |
| 92 | `nash_2x2_mixed_prob(a00, a01, a10, a11)` | Adaptive Combat AI (`RawStrategicAiEngine`) | `clamp((a11 - a01) / (a00 - a01 - a10 + a11), 0, 1)` | Matching Pennies = `0.5`; asymmetric feint = `0.5833` |
| 93 | `fictitious_play_ema_step(emp_freq, best_resp, step_n, decay_floor)` | Lexicographic Fictitious Play AI (`RawStrategicAiEngine`) | `cur + max(1/(n+1), floor) * (br - cur)` in `[0, 1]` | `n=4` harmonic step `0.5 -> 0.6`; `n=50` floor step `0.5 -> 0.525` |
| 94 | `prophet_reservation_threshold(expected_opt, subadd_alpha, risk_aversion)` | Subadditive Prophet Inequality (`RawSettlementSim`, Merchant Bidding) | `(E[OPT] / (2 * alpha)) * (1 + 0.15 * risk)` | Additive (`alpha=1`) = exact `200.0` (`E[OPT]/2`); subadditive = `164.8` |
| 95 | `truncated_discrete_laplace_w(k_abs, scale_s, bound_b)` | Truncated Discrete Laplace Sampler (`L_N[1/3]` Lattice Formulation) | `exp(-|k| / s)` for `|k| <= b`, exact `0.0` outside | `k=2, s=2` = `1/e = 0.3679`; `k=7 > 6` = exact `0.0` |
| 96 | `kl_binary_stability_nats(p_raw, q_raw)` | Log-Rank KL-Stable Rounding (`RawStrategicAiEngine`) | `p*ln(p/q) + (1-p)*ln((1-p)/(1-q))` clamped `>= 0` | Identical `(0.5||0.5)` = exact `0.0`; `(0.75||0.50)` = `0.1308` nats |

## Reproducing All 16 BuildLang Module Checks

```powershell
foreach ($m in @('RawPhysicsKernels','RawAtmoWeather','RawBiomeFlora','RawRenderOptics','RawFluidDynamics','RawCombatAi','RawEconomySim','RawGpuPipeline','RawWorldSystems','RawCloudsDungeon','RawAnimKinematics','RawParticlesCombat','RawTsrQuests','RawCausticsCrafting','RawSnowEcology','RawGameTheoryMath')) {
    buildc check "E:\wt\raw-rt9\native\bld\$m.bld"
    buildc run   "E:\wt\raw-rt9\native\bld\$m.bld"
}
```
