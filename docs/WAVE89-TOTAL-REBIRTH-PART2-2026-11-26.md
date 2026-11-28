# Wave 89: Project Total Rebirth Part 2 (2026-11-26)

## Objective
Fix all 5 visual defects caught during the Wave 88 3840x1080 human eye-audit and elevate live on-screen world geometry with 4-step silhouette-safe Parallax Occlusion Mapping (POM) sculpted cobblestones, wheel-rut micro-puddles, fractured rock cliff strata, and automated visual-defect regression gates `#35..#37`.

## Root-Cause Fixes for All 5 Wave 88 Eye-Audit Defects
1. **Defect A (`riften_canal` green horse):**
   - Root cause: Two compounded classification bugs: (a) `src/d3d11_proxy/GBufferVariants.cpp` skipped VS CB10 80-bone palette inspection when `g_sceneLightsActive == true` and `SceneLights_DrawWorld() == nullptr`, and (b) Skyrim's horse fur shader binds a soft-lighting map (`t12`), so `GBufferAnalyse` tagged the horse body `SoftLit (base == 5u)`, and both `GBufferVariants.cpp` and `GBufferPatch.h::ResolveMaterialClass` refused to promote `SoftLit (5)` to `SkinnedActor (14)`.
   - Fix: Always promote non-alpha-cutout 80-bone skinned meshes (`hasSkinLayout && !isAlphaCutout && vsCb10 >= 3840u`) from `Lit (1)` OR `SoftLit (5)` to `SkinnedActor (14)` while leaving SpeedTree alpha-cutout trees as `SoftLit (5)`, and reinforce warm-fur hue exclusions in `RawWeathering.hlsli`, `RawMossShader.hlsli`, and `RawTotalRebirth.h`.
   - Visual proof (`live_w89_ext6_v6/riften_canal_rebirth_compare_3840x1080.png`): Full-frame close-up of the Riften horse shows natural dappled silver-grey coat, black mane, natural dark eyes, zero green (`Gate #35: green_bleed_frac = 0.0167 <= 0.05, green_excess_over_red = 0.0051 <= 0.02, PASS`).
2. **Defect B (`windhelm_ext_snow` spring-green winter twigs):**
   - Root cause: `Lighting.hlsl` (`addLight`, `speciesTint`, `foliageSpecTint`, canopy SSS/saturation) and `RawLiveWorldComposite.hlsl` applied bright green chlorophyll tinting unconditionally to all `base == 5u` draws, including frost-covered winter brambles and golden autumn birch trees.
   - Fix: Split `base == 5u` foliage cleanly into living green (`albedo.g > albedo.r * 1.04 && albedo.g > albedo.b * 1.08`), golden autumn (`albedo.r > albedo.b * 1.15 && albedo.r >= albedo.g * 0.96`), and frosted winter twigs (bright neutral hoarfrost micro-sparkle with zero green).
   - Visual proof (`live_w89_ext6_v6/windhelm_ext_snow_rebirth_compare_3840x1080.png`): `Gate #36: green_dominance_delta = 0.0035 <= 0.02, spring_green_flip_frac = 0.0159 <= 0.05, PASS`.
3. **Defect C (`septimus_cave` leopard-spot glacier ice):**
   - Root cause: Isotropic cellular ridge noise created round spots on smooth glacier walls.
   - Fix: Replaced with oblique hex-stochastic non-periodic glacial flow striations, sparse zone-masked hairline stress fissures, deep Beer-Lambert sapphire subsurface depth, and wet microfacet specular glints in `RawTotalMaterialOverhaul.hlsli`, `RawSubdiv.hlsl`, and `RawTotalRebirth.h`.
   - Visual proof (`live_w89_ext6_v6/septimus_cave_rebirth_compare_3840x1080.png`): Flowing oblique glacial strata with zero leopard spots and zero polygon web (`detail_uplift_ratio = 2.0184x >= 1.65x`, `ice_anisotropy = 1.5764 >= 1.18`, `ice_blotch_ratio = 0.1914 <= 0.45`, `PASS`).
4. **Defect D (`whiterun_ext` blotchy blue barn & wooden barrel):**
   - Root cause: Three exterior blue-wood ice/frost paths (`Lighting.hlsl` `iceMask`, `RawDecals3D.hlsli` `frostEdge`, and `RawTotalMaterialOverhaul.hlsli` `GlacierIce`) matched exterior painted Nordic blue wood (`albedo.b > albedo.r`) and applied glacier/frost splatter over wooden planks and barrels.
   - Fix: Restricted `iceMask` and `frostEdge` strictly to interior ice caverns (`sunColour.w < 0.5`) and `base == 2u / 6u` glacier shaders, classified exterior painted blue timber as `AssetClass14::TimberWood` in both HLSL and `GBufferPatch.h`, excluded painted blue wood from `Lighting.hlsl`'s `isRockCliff` streak pass, and shaded wood with strictly directional 22:1 wood-grain fibers (`minMul = 0.84`).
   - Visual proof (`live_w89_ext6_v6/whiterun_ext_rebirth_compare_3840x1080.png` + `whiterun_ext_audit_improved_f00.png`): Clean weathered Nordic blue barn planks, warm brown horse, sculpted 3D cobblestone road (`Gate #37: wood_anisotropy = 1.2456 >= 1.18, wood_blotch_ratio = 0.3799 <= 0.45, PASS`).
5. **Defect E (`inn_interior` dark back alcoves):**
   - Fix: Raised shadowed interior floor (`minMul = 0.84`) in `RawTotalMaterialOverhaul.hlsli` and added warm hearth indirect ambient bounce lift (`intLum < 0.085`) in `RawLiveWorldComposite.hlsl`.
   - Visual proof (`live_w89_ext6_v6/inn_interior_rebirth_compare_3840x1080.png`): Warm golden hearth firelight illuminates back-wall alcoves, pelts, tables, chairs, and timber beams cleanly (`inn_interior` `0` defects).

## Part 2 World Surface Overhaul
- **4-Step Silhouette-Safe POM (`rawRebirthEvalPomDisplacement` / `EvalPomDisplacement4Step`):** World-space triplanar heightfield ray-march along the clamped tangent view vector on `CobbleRoadPath`, `RockCliffBoulder`, `TerrainGround`, and `NordicStoneMasonry`, strictly masked to zero at depth silhouettes and beyond 24m.
- **Sculpted Roads & Wheel-Rut Micro-Puddles:** Recessed cobblestone mortar AO, chamfered flagstone relief, and subtle reflective wheel-rut micro-puddles in road hollows coupled to live `rain01`.
- **Deep Fractured Cliff Strata:** Sedimentary rock ledges, sparse cleavage fractures, and granite micro-grit with close-range `dxtSafe` attenuation preventing 2011 DXT1 block magnification.

## Verified Receipts (Wave 89 Final)
- **DXC 1.10 (`-O3 -WX`, SM `6_6/6_9/6_10`):** `7/7 PASS` (`Lighting.hlsl`, `RawLiveWorldComposite.hlsl`, `RawSubdiv.hlsl`, `RawTerrainWorld.hlsl`).
- **Ninja Build (`-j 6`):** `d3d11.dll`, `raw_engine.dll`, `ce_runtime_check.exe`, `test_texture_atlas_gpu.exe` built cleanly (`0` warnings).
- **C++23 `/W4 /WX` Suite (`tests/wave89_total_rebirth_part2_check.cpp`):** `21/21 PASS` (`glacier_aniso = 2.357` vs `planted_spot_aniso = 1.031`, horse `SoftLit -> SkinnedActor` promotion locked, POM bounds & silhouette/actor/distance controls all verified).
- **CE Runtime Suite (`ce_runtime_check.exe`):** `522/522 PASS`.
- **Python Test Suites (`tests/test_*.py`):** `20/20 PASS` (`tests/test_wave89_total_rebirth_part2.py`: `5/5 PASS`).
- **Atlas v2 (`scan_raw_engine_atlas.py --v2`):** `unguarded_offsets = 0`, `unbound_hlsl_resources = 0`, `untested_subsystems = 0` (`17/17`), `hlsl_resources = 640`.
- **RTX 4090 GPU Suites (`d3d12DebugErrors = 0`):** `test_tsr_upscale_gpu.exe` (`18/18 PASS`), `raw_bench_suite.exe` (`21/21 PASS`), `test_texture_atlas_gpu.exe` (`22/22 PASS`).
- **Live 6-Scene Skyrim SE Audit (`--profile extended6`, `E:\wt\raw-rt9-build\live_w89_ext6_v6`):**
  - `raw.heuristics-v2/37`: **`17/17` extended (`#21..#37`) + `142` base = `159` checks, `all_passed: true`** (`detail_uplift = 2.0184x`, `tiling_drop = 60.258%`, `Gate #35 = PASS`, `Gate #36 = PASS`, `Gate #37 = PASS`, `0` NaNs, `0` D3D12 debug errors, `POST_LOCK_EXISTS = False`, `POST_SKYRIMSE_PROCS = 0`).
  - `3840x1080` Visual Eye-Audit Plates (all 6 visually inspected):
    - `E:\wt\raw-rt9-build\live_w89_ext6_v6\riften_canal_rebirth_compare_3840x1080.png`
    - `E:\wt\raw-rt9-build\live_w89_ext6_v6\windhelm_ext_snow_rebirth_compare_3840x1080.png`
    - `E:\wt\raw-rt9-build\live_w89_ext6_v6\septimus_cave_rebirth_compare_3840x1080.png`
    - `E:\wt\raw-rt9-build\live_w89_ext6_v6\whiterun_ext_rebirth_compare_3840x1080.png`
    - `E:\wt\raw-rt9-build\live_w89_ext6_v6\inn_interior_rebirth_compare_3840x1080.png`
    - `E:\wt\raw-rt9-build\live_w89_ext6_v6\riverwood_day_rebirth_compare_3840x1080.png`
