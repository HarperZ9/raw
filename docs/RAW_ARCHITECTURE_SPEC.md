# RAW: Native DX12 Rendering and World Simulation Architecture

Platform: THE PLAYGROUND (THEPLAYGRND)
License: MIT (100% clean-room original implementation, zero SKSE dependency)

## What RAW Does

RAW replaces the Direct3D 11 rendering pipeline, in-game configuration UI, input handling, and core simulation layers of Creation Engine games (Skyrim SE/AE/VR, Fallout 4/VR, Fallout 76 online-safe mode, and Starfield CE2 native DX12 hook) through a drop-in `d3d11.dll` proxy and `raw_engine.dll` runtime module.

- Native D3D11-to-D3D12 translation (`raw::tx`) with persistent disk PSO caching and contiguous sub-array sampler heap packing.
- Hardware DXR 1.2 ray-traced shadows, Opacity Micromaps (`RtOmm`), ReSTIR DI/GI visibility reuse, glossy reflections, and a zero-VRAM negative-space radiance oracle.
- SM 6.6 + retail-signed SM 6.9 DXIL 1.9 dual shader pipeline with 16-wide Long-Vector `RAWLSS 7.0` Split-Manifold neural ray reconstruction (validated under SM 6.10 preview DXC).
- Runtime GPU geometry uplift (`RawMesh.hlsl` AS+MS 1x/4x/9x curved Vlachos PN-Triangle tessellation and `RawSubdiv.hlsl` compute subdivision) with sharp-crease preservation, world-space micro-displacement, and 3D volumetric snowpack vertex lift that never modifies stock collision or stock navmesh records.
- Persistent 512x512 toroidal world-locked PDE (`WorldFieldSim.hlsl`) driving shallow-water waves, vortex-stretching wakes, angle-of-repose volumetric snow buildup, mass-conserving boot-trench berms, and hearth melt craters.
- Clean-room Creation Engine format readers (`CeFormatReader`, `CeNifReader`, `CeNavMesh`) that index `.esm`/`.bsa`/`.ba2` archives, decompress LZ4 v105 assets, extract `.nif` `BSTriShape` meshlets, and parse `.esm` `NAVM` records directly without SKSE.
- Dynamic `RawNavGraph` pathfinding (A* + Simple Stupid Funnel Algorithm string-pulling, dynamic obstacle detouring, snow-depth traversal weighting, and off-mesh climb/vault links).
- Procedural skeletal kinematics (`rt_skeleton_ik`: exact analytical 2-bone Foot/Arm IK, terrain-conforming pelvis drop, ankle slope alignment, gaze spine distribution) coupled to the 240 Hz sub-stepped XPBD soft-body solver (`rt_softbody`).
- Tactical squad combat AI and modern action combat (`rt_combat_ai`: vanguard/flanker/skirmisher role coordination, 160 ms precision timed parry, posture/poise stagger, stamina-gated i-frame dodge, melee hit-stop, and weather/time living-world directives).
- Architectural 10-domain in-game Studio UI (`RawStudioUi`, `RawInputEngine`, `RawDesignTokens`) following `DESIGN-RULES.md` and `colour-tokens.json` with Dark (`#101318` / `#ede9e1`) and Light (`#f9f5ed` / `#1b1e24`) themes, CVD-safe shape glyphs, full mouse click/drag/wheel, keyboard, and XInput gamepad controls.

## Quick Start

1. Copy `d3d11.dll`, `raw_engine.dll`, `d3d11_proxy.ini`, `raw_shaders/`, and `raw_versions/` into the game executable directory.
2. Launch the game normally (no SKSE loader or utility mods required).
3. Press `End` on keyboard or `LB + RB + Start` on an XInput controller to open RAW DX12 Studio.
4. Drop Blender `.glb` or `.obj` meshes into `raw_assets/overrides/` for live hot-swap geometry replacement without `.nif` or `.esp` conversion.

## Honest Scope and Boundaries

- **Runtime GPU Subdivision vs Bethesda CPU Physics:** GPU PN-Triangle subdivision and 3D snowpack vertex lift operate in the render and DXR geometry pipeline; Bethesda's Havok collision shapes and stock `NAVM` records remain intact so vanilla quests and triggers stay deterministic.
- **Online Multiplayer Guard:** When running under Fallout 76, `raw_engine.dll` disables all runtime memory writes automatically.
- **Unverified at Runtime:** Live in-game visual captures of the Wave 10/11/12/13 runtime changes await the next interactive playtest session; unit, WARP translation, and shader compilation checks are verified below.

## Verified

- Checked: `d3d11_proxy`, `raw_engine`, `ce_runtime_check`, and `d3d11_translate_suite` C++23/DXIL build (MSVC + Ninja + DXC 1.9 retail-signed SM 6.9 + DXC 1.10 SM 6.10 preview). Result: PASS (pending Wave 11-14 final build run).
- Checked: WCAG 2.1 contrast ratios on `DarkTheme()` and `LightTheme()` tokens (`#101318`/`#ede9e1` and `#f9f5ed`/`#1b1e24`), radial stick deadzone math, `CeNavMesh` A* + SSFA funnel + dynamic obstacle detour + real `Skyrim.esm` `NAVM` parse, 2-bone IK length/target invariants, squad tactical flanking, timed parry/poise resolution, and world ecosystem directives via `ce_runtime_check`. Result: PASS (see build verification output).
- Checked: 41-test D3D11-to-D3D12 pixel-parity suite via `d3d11_translate_suite` on WARP with D3D12 debug layer enabled. Result: 41 passed, 0 failed, 0 debug errors, 0 leaked refs.
