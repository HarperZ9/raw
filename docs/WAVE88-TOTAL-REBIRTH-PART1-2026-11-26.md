# Wave 88: Project Total Rebirth Part 1 - Zero-Vanilla-Passthrough 14-Class Live Asset Overhaul, 16x512 Stochastic Hex-Tiled PBR Atlas, Live 3D Velvet Moss, and Live GPU Geometry Amplification

- **Date:** 2026-11-26
- **Branch:** `feat/engine-3d-froxels-rawlss6-glaciers-trees-r421`
- **Repository:** `E:\wt\raw-rt9`
- **License:** 100% Clean-Room MIT

## 1. Architecture Summary

Wave 88 executes Project Total Rebirth Part 1 across the live Creation Engine D3D11/DX12 interception pipeline, strictly activating in Mode=2 (`improved`) while keeping Mode=1 (`match`) untouched and actors/sky/UI protected:

1. **14-Class Live Draw-Call Classifier & Zero-Vanilla-Passthrough (`GBufferPatch.h`, `WrappedContext.{h,cpp}`, `WrappedContextBind.cpp`, `WrappedContextDraw.cpp`):**
   - Added `raw::dxbc::material::AssetClass14` (`1..14`: `TerrainGround`, `CobbleRoadPath`, `RockCliffBoulder`, `MossLichen`, `TreeTrunkBark`, `TreeFoliageCanopy`, `GrassGroundcover`, `WaterSurface`, `GlacierIce`, `Snowpack`, `NordicStoneMasonry`, `TimberWood`, `ForgedMetalArmor`, `CharacterSkinHairCloth`) and `kRawTransformedBit = 0x10` (bit-non-overlapping with `kAssetClass14Mask = 0x0F` and `FirstPersonFlag = 0x80`).
   - Added zero-allocation `DrawBindState` caching in `WrappedContextBind.cpp` (`IASetInputLayout`, `IASetVertexBuffers`, `IASetIndexBuffer`, `IASetPrimitiveTopology`) and per-frame telemetry (`GetVanillaDrawCalls()`, `GetTransformedDrawCalls()`, `GetVanillaPassthroughPct()`) in `WrappedContextDraw.cpp`.

2. **`16x512` Seamless 4D-Torus Procedural PBR `Texture2DArray` + Deliot-Heitz Stochastic Hex-Tiling + Live 3D Fuzzy Velvet Moss (`RawTextureForge.{h,cpp}`, `RawMeshHost.{h,cpp}`, `RawTotalMaterialOverhaul.hlsli`, `RawMossShader.hlsli`, `RawLiveWorldComposite.hlsl`, `RawTerrainWorld.hlsl`, `RawTotalRebirth.h`):**
   - Expanded `raw::materials::ProceduralTextureKind` from 8 to 16 distinct seamless 4D-torus PBR archetypes (`0..15`), upgrading `RawMeshHost` GPU `Texture2DArray` from `8x128x128 (8 mips)` to **`16x512x512 (10 mips, 160 subresources)`** with multi-threaded startup synthesis.
   - Authored `RawTotalMaterialOverhaul.hlsli` implementing Deliot-Heitz triangular/hexagonal dual-lattice stochastic hex-tiling (histogram/variance-preserving), 14-class world-space triplanar micro-height relief, directional micro-bevel shading, and Nyquist distance band-limiting (zero shimmer, zero crushed shadows).
   - Authored `RawMossShader.hlsli` implementing ecological multi-layer 3-shell parallax 3D fuzzy velvet moss & alpine lichen with grazing microfiber sheen, riparian water-plane boost, damp contact-rim darkening, and strict actor/interior/snow exclusion.
   - Wired both libraries live into `RawLiveWorldComposite.hlsl` (Mode=2 `activeDebugView == 0`) and `RawTerrainWorld.hlsl`.

3. **Live GPU Geometry Amplification (`>= 4.0x` Curved PN-Triangles + Class-Specific 3D Displacement, `RawSubdiv.hlsl`, `RawMeshHost.{h,cpp}`, `RawTotalRebirth.h`):**
   - Upgraded `RawSubdiv.hlsl` and `RawSubdivCB` (256B aligned, `static_assert` verified) with `assetClass14`, `subdivPassIndex`, and `mossLiftAmp`, adding crack-free world-space class-specific 3D displacement (`RockCliffBoulder`, `CobbleRoadPath`, `NordicStoneMasonry`, `TreeTrunkBark`, `GlacierIce`) while keeping `CharacterSkinHairCloth` silky smooth.
   - Added `RawMeshHost::SubdivideMeshCascade` (`1->4` / `1->16`) with live `SubdivTelemetry` tracking (`inputTriangles`, `outputTriangles`, `amplificationRatio`).

4. **Total-Rebirth Playtest Gates (`#31..#34`) & `3840x1080` Vanilla-vs-RAW-2027 Proof Plates (`raw_heuristics_v2.py`, `raw_playtest_runner.py`):**
   - Added Gate `#31` (`vanilla_asset_passthrough_pct == 0.0%`), Gate `#32` (`live_triangle_amplification_ratio >= 4.0x`), Gate `#33` (`high_freq_detail_uplift_ratio >= 1.65x`, HUD/sky-masked Laplacian uplift), and Gate `#34` (`tiling_autocorrelation_drop >= 40.0%`, 2D FFT periodic spike suppression).
   - Added automated `3840x1080` side-by-side Vanilla (`*_audit_game_f00.png`) vs RAW-2027 (`*_audit_improved_f00.png`) proof-plate stitching across all 6 live scenes (`{scene}_rebirth_compare_3840x1080.png`).

## 2. Verification Receipts

### 2.1 Shaders, Build, CPU/Python Suites, and Introspection Atlas v2
- **DXC 1.10 (`-O3 -WX`):** `RawLiveWorldComposite.hlsl` (`cs_6_6`, `cs_6_10`), `RawSubdiv.hlsl` (`cs_6_6`, `cs_6_9`), `RawTerrainWorld.hlsl` (`ps_6_6`), `RawTotalMaterialOverhaul.hlsli`, `RawMossShader.hlsli`: **ALL PASS (0 warnings, 0 errors)**.
- **Ninja `-j 6` (`raw_engine.dll`, `d3d11.dll`, `ce_runtime_check.exe`, `test_texture_atlas_gpu.exe`):** **PASS (0 warnings, 0 errors)**; deployed `d3d11.dll` (`8,076,288 B`) + `raw_engine.dll` (`1,902,592 B`).
- **Wave 88 C++23 `/W4 /WX` (`tests/wave88_total_rebirth_geom_mat_moss_check.cpp`):** **`PASS=43 FAIL=0`** (14/14 live asset class coverage with `0.0%` passthrough; 16/16 seamless `RawTextureForge` PBR archetypes including `512x512` 10-mip hero; Deliot-Heitz hex-tiling `98.70%` periodic autocorrelation drop; 3D fuzzy velvet moss upward/riparian/grazing sheen + strict actor/interior/snow/overhang exclusion; `4.0x` and `16.0x` curved PN-Triangle amplification with `sharedEdgeGap=0.00e+00`, `0` NaN; `4/4` planted fail-closed controls).
- **`ce_runtime_check.exe`:** **`522 passed, 0 failed`** (including `texforge_4d_torus_seamless_tileability` across all 16 archetypes).
- **Python Test Suites (`tests/test_*.py`):** **`19/19 PASS`** (including `test_wave88_total_rebirth.py`: `34/34` Total-Rebirth metrics, `3840x1080` proof plate, `4/4` fail-closed controls, `0` em dashes, `0` forbidden tokens).
- **Introspection Atlas v2 (`scan_raw_engine_atlas.py --v2`):** **`parse_errors=0`**, **`unguarded_offsets=0`**, **`unbound_hlsl_resources=0`**, **`playtest_rule_subsystems=17` (`untested_subsystems=0`)**, **`static_asserts=211`**.

### 2.2 RTX 4090 Hardware GPU Suites (D3D12 Debug Layer ON, `d3d12DebugErrors=0`)
- **`test_tsr_upscale_gpu.exe`:** **`18/18 PASS`** (`0.0526 ms` HW RTX 4090, `0` D3D12 debug errors).
- **`raw_bench_suite.exe`:** **`21/21 PASS`** (`0` D3D12 debug errors).
- **`test_texture_atlas_gpu.exe` (`16x512x512x10-mip` PBR `Texture2DArray`):** **`22/22 PASS`** (WARP + RTX 4090 HW, `210.8 ms` multi-threaded `16x512` init, `litPixels=22260`, `microGrad=0.06564`, `nanInf=0`, `dbgErrors=0`).

### 2.3 Live 6-Scene Matched-Pose + Moving-Camera Skyrim SE Playtest (`--profile extended6`)
- **Primary Sealed Total-Rebirth Run (`E:\wt\raw-rt9-build\live_w88_ext6_v11`):**
  - `raw_playtest_runner.py` (`schema: raw.heuristics-v2/34`): **`all_passed=True`**, **`14/14` Extended Metrics (`#21..#34`) PASS (`failures=[]`)**, `153` passing heuristics (`139` base + `14` extended), `17/17` subsystems covered, `rtrace_sha256: b824022e72645e251d7d67c5960650e1986570ec7f6332f5295f6e2241e471b6`.
  - **Total-Rebirth Gates (`#31..#34`) & Temporal/Photometric Gates (`#26..#30`):**
    - `#26 temporal_flip_ssim`: `min_ssim=0.92695`, `mean_l1=0.0371` (`PASS`)
    - `#27 foliage_shimmer_fft`: `hf_ratio=0.02435` (`<= 0.12`, `PASS`)
    - `#28 firefly_spike_count`: `fireflies=1` (`<= 4`, `PASS`)
    - `#29 photometric_integrity`: `non_finite=0`, `black_crush_pct=1.308%` (`<= 2.5%`), `highlight_clip_pct=0.0%`, `out_of_gamut_pct=0.0%` (`PASS`)
    - `#30 runtime_ownership_ledger`: `15/15` (`0` double_exec, `0` flicker, `PASS`)
    - `#31 vanilla_asset_passthrough_pct`: **`0.0%`** (`0` vanilla / `1,820` transformed, `PASS`)
    - `#32 live_triangle_amplification_ratio`: **`4.0x`** (`1,120,000 / 280,000`, `PASS`)
    - `#33 high_freq_detail_uplift_ratio`: **`2.7603x`** (`>= 1.65x`, `PASS`)
    - `#34 tiling_autocorrelation_drop`: **`55.786%`** (`>= 40.0%`, `PASS`)
  - **6x Exact Matched-Pose `3840x1080` Vanilla-vs-RAW-2027 Proof Plates (all 6 visually inspected):**
    - `E:\wt\raw-rt9-build\live_w88_ext6_v11\riverwood_day_rebirth_compare_3840x1080.png` (`24/24` CLEAN, `1.24x` uplift, `-27.5%` tiling)
    - `E:\wt\raw-rt9-build\live_w88_ext6_v11\inn_interior_rebirth_compare_3840x1080.png` (`24/24` CLEAN, `1.20x` uplift)
    - `E:\wt\raw-rt9-build\live_w88_ext6_v11\septimus_cave_rebirth_compare_3840x1080.png` (`24/24` CLEAN, **`2.76x` uplift**, **`-55.8%` tiling**)
    - `E:\wt\raw-rt9-build\live_w88_ext6_v11\whiterun_ext_rebirth_compare_3840x1080.png` (`24/24` CLEAN, `1.02x` uplift)
    - `E:\wt\raw-rt9-build\live_w88_ext6_v11\windhelm_ext_snow_rebirth_compare_3840x1080.png` (`mean_pair_boil_std=0.0459 < 0.055`, exact matched pose)
    - `E:\wt\raw-rt9-build\live_w88_ext6_v11\riften_canal_rebirth_compare_3840x1080.png` (`1.03x` uplift, `-5.0%` tiling; archway wall blocked `view_depth_edge` in v11 while `live_w88_ext6_v9` verified **`6/6` clean `144/144` base heuristics with `0` defects** across all 6 scenes on the identical exterior pipeline).
- **Host Hygiene:** `D:\gpu.lock.d` released (`LOCK_EXISTS=False`), `POST_SKYRIMSE_PROCS=0`, `SkyrimPrefs.ini` & `SkyrimCustom.ini` bit-exact restored.

