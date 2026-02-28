# SkyrimBridge v3.0 — Comprehensive Improvement Plan

**Analyst:** Claude (full codebase audit)
**Date:** February 27, 2026 (Revision 2 — post game-build file addition)
**Scope:** All files across `SkyrimBridge_v3/`, `SkyrimBridgeOldSources/`, shaders, C++, docs, configs

---

## Table of Contents

1. [Critical Findings (Bugs & Blockers)](#1-critical-findings)
2. [Project Organization & Hygiene](#2-project-organization)
3. [C++ DLL Improvements](#3-cpp-dll-improvements)
4. [Shader Fixes & Improvements](#4-shader-fixes)
5. [Performance Optimization](#5-performance-optimization)
6. [Feature Activation (62 Unused Parameters)](#6-feature-activation)
7. [New Feature Development](#7-new-features)
8. [Build System & Tooling](#8-build-system)
9. [Distribution & Release](#9-distribution)
10. [Long-Term Architecture (v4.0 Vision)](#10-long-term)
11. [Execution Order & Dependencies](#11-execution-order)

---

## 1. Critical Findings (Bugs & Blockers) {#1-critical-findings}

These must be fixed before any other work proceeds.

### 1.1 ~~BLOCKER: Missing Shader Include File~~ RESOLVED

**Status:** FIXED — Game-build files added `enbHelper_Common.fxh`, `enbHelper_Dither.fxh`, and `enbHelper_Debug.fxh` to `shader/Helper/`. The original missing-include blocker is resolved.

**However — NEW BLOCKER discovered (see 1.7 below):** 4 addon files are still missing.

### 1.1b NEW FILES ADDED FROM GAME BUILD

The following files were added and change the project landscape significantly:

**New shader files:**
| File | Size | Location | Notes |
|------|------|----------|-------|
| `enbunderwater.fx` | 73KB (1,574 lines) | `shader/` | Full underwater post-processing. NO SkyrimBridge integration. |
| `enbHelper_Common.fxh` | 26KB | `shader/Helper/` | Core utility library (luminance, color space, depth, SSS diffusion) |
| `enbHelper_Debug.fxh` | 4.5KB | `shader/Helper/` | On-screen float4 number display for debugging |
| `enbHelper_Dither.fxh` | 3.1KB | `shader/Helper/` | Gaussian blue-noise temporal dithering |
| `PrePassAddonTechniques.fxh` | 2.5KB | `shader/Helper/` | Technique routing table for prepass addon combos |

**New directories:**
| Directory | Files | Purpose |
|-----------|-------|---------|
| `shader/UI/` | 9 files (230KB total) | Original Silent Horizons ENB GUI control headers |
| `shader/Addons/` | 5 files (125KB total) | Original full-implementation effect modules |

**Critical architecture discovery — Two-Generation Shader System:**

The shader directory now contains TWO generations of code with overlapping filenames:

```
shader/
├── Helper/                      ← IMPROVED versions (SB-integrated patches)
│   ├── SkyrimBridge.fxh          ← Canonical (57KB, unique)
│   ├── enbHelper_Common.fxh      ← Utility library (unique)
│   ├── enbHelper_Debug.fxh       ← Debug display (unique)
│   ├── enbHelper_Dither.fxh      ← Dithering (unique)
│   ├── PrePassAddonTechniques.fxh← Technique router (unique)
│   ├── Effect_CinematicFX.fxh    ← 8.6KB — SB integration OVERLAY (hooks only, NOT full implementation)
│   ├── Effect_ProceduralWeatherFX.fxh ← 9.3KB — SB integration OVERLAY
│   ├── Effect_AtmosphericFog.fxh ← 8.8KB — SB integration OVERLAY
│   ├── Effect_CRTShader.fxh      ← 12.9KB — SB integration version
│   ├── Effect_ProceduralLensDirt.fxh ← 18.8KB — SB integration version
│   ├── enbUI_CRT.fxh             ← 6.7KB — Rewritten UI params (compact)
│   ├── enbUI_CinematicFX.fxh     ← 16.2KB — Rewritten UI params
│   ├── enbUI_DepthOfField.fxh    ← 28.7KB — Rewritten UI params
│   ├── enbUI_Fog.fxh             ← 10.3KB — Rewritten UI params
│   ├── enbUI_Lens.fxh            ← 34.1KB — Rewritten UI params
│   └── enbUI_PostPass.fxh        ← 26.4KB — Rewritten UI params
│
├── UI/                           ← ORIGINAL game-ready ENB GUI headers
│   ├── enbUI_Primer.fxh          ← 26.5KB — UI macro system (UNIQUE — no Helper/ copy)
│   ├── enbUI_PrePass.fxh         ← 16.6KB — SSS UI controls (UNIQUE — no Helper/ copy)
│   ├── enbUI_SunSprite.fxh       ← 35.5KB — Sun sprite UI (UNIQUE — no Helper/ copy)
│   ├── enbUI_DepthOfField.fxh    ← 34.1KB — DIFFERENT from Helper/ version
│   ├── enbUI_Lens.fxh            ← 62.8KB — DIFFERENT (2x larger than Helper/)
│   ├── enbUI_PostPass.fxh        ← 32.5KB — DIFFERENT from Helper/ version
│   ├── enbUI_CinematicFX.fxh     ← 12.4KB — DIFFERENT from Helper/
│   ├── enbUI_CRT.fxh             ← 3.2KB — DIFFERENT from Helper/
│   └── enbUI_Fog.fxh             ← 5.8KB — DIFFERENT from Helper/
│
└── Addons/                       ← ORIGINAL full-implementation effect modules
    ├── Effect_CinematicFX.fxh    ← 61KB — FULL 8-effect cinema suite (vs 8.6KB Helper/ patch)
    ├── Effect_ProceduralWeatherFX.fxh ← 29KB — FULL rain/frost (vs 9.3KB Helper/ patch)
    ├── Effect_AtmosphericFog.fxh ← 14KB — FULL atmospheric fog (vs 8.8KB Helper/ patch)
    ├── Effect_CRTShader.fxh      ← 8KB — FULL CRT effect
    └── Effect_ProceduralLensDirt.fxh ← 12.6KB — FULL lens dirt
```

**Include dependency graph:**
- 7 IMPROVED .fx files → `Helper/SkyrimBridge.fxh` only (self-contained UI params inline)
- `enbeffectprepass.fx` → `UI/enbUI_Primer.fxh` + `UI/enbUI_PrePass.fxh` + `Helper/SkyrimBridge.fxh` + `Helper/enbHelper_Common.fxh` + `Addons/PrePass_*.fxh` (4 MISSING!)
- `enbunderwater.fx` → `UI/enbUI_Primer.fxh` + `Helper/enbHelper_Common.fxh` (no SB integration)

### 1.7 NEW BLOCKER: 4 Missing Addon Includes in enbeffectprepass.fx

**Severity:** CRITICAL — `enbeffectprepass.fx` will fail to compile
**File:** `shader/enbeffectprepass.fx:483-506`
**Problem:** References 4 addon include files that do NOT exist anywhere in the project:
```hlsl
#include "Addons/PrePass_StylizationSuite.fxh"   // line 483 — NOT FOUND
#include "Addons/PrePass_ParticleField.fxh"       // line 492 — NOT FOUND
#include "Addons/PrePass_PhotoStudio.fxh"         // line 499 — NOT FOUND
#include "Addons/PrePass_SnowCover.fxh"           // line 506 — NOT FOUND
```

These are Silent Horizons addon modules that were never copied from the game install. HLSL `#include` will fail hard — unlike C/C++, there is no `__has_include`.

**Fix (choose one):**
- **Option A (recommended):** Create empty stub files that define nothing:
  ```hlsl
  // Addons/PrePass_StylizationSuite.fxh — stub (addon not installed)
  // Intentionally empty — see enbeffectprepass.fx guard macros
  ```
  The enbeffectprepass.fx already has `#ifdef _STYLIZATION_SUITE_` guards that handle the case where these aren't loaded — they just need to NOT fail on include.

- **Option B:** Wrap includes in a compile-time guard:
  ```hlsl
  #ifdef HAS_STYLIZATION_SUITE
  #include "Addons/PrePass_StylizationSuite.fxh"
  #endif
  ```

- **Option C:** Copy the actual addon files from the game install into `shader/Addons/`

**Effort:** 10 minutes (Option A) to 30 minutes (Option C)

### 1.8 NEW: Underwater Shader Has No SkyrimBridge Integration

**Severity:** MEDIUM — missed opportunity, not a bug
**File:** `shader/enbunderwater.fx` (1,574 lines, 73KB)
**Problem:** This sophisticated physically-based underwater shader (PBR absorption, caustics, god rays, Snell's window, bioluminescence, Tyndall scattering, bubbles, wet lens) has ZERO SkyrimBridge integration. It does not `#include "Helper/SkyrimBridge.fxh"`.

**Opportunities with SB integration:**
- `SB_Player_Water.z` — Player depth below water surface → modulate absorption/blur intensity
- `SB_Sun_Direction` — Accurate god ray direction (currently approximated)
- `SB_Weather_Wind` — Wind speed → wave distortion intensity
- `SB_Weather_Precip` — Rain → surface ripple enhancement
- `SB_Fog_Height.x` — Water surface Z for depth accuracy
- `SB_Atmos_Sunlight` — Sunlight color for caustic tinting
- `SB_Interior_Flags` — Suppress underwater effects in interior water (if needed)

**Effort:** 2-3 hours

### 1.9 NEW: Dual-Version Effect Files Need Merging Strategy

**Severity:** MEDIUM — confusing architecture, blocks feature work
**Problem:** Effect modules exist in TWO incompatible forms:

| File | Addons/ (Original) | Helper/ (SB Patch) | Relationship |
|------|--------------------|--------------------|--------------|
| Effect_CinematicFX.fxh | 61KB (full 8-effect suite) | 8.6KB (SB hooks only) | Patch needs base |
| Effect_ProceduralWeatherFX.fxh | 29KB (full rain/frost) | 9.3KB (SB hooks only) | Patch needs base |
| Effect_AtmosphericFog.fxh | 14KB (full fog system) | 8.8KB (SB hooks only) | Patch needs base |

The Helper/ versions are NOT standalone — they contain only the new UI parameters and SB-aware function wrappers. They need to be MERGED into the Addons/ originals to create complete SB-integrated versions.

**Fix:**
1. For each Effect_*.fxh, merge the SB hooks from Helper/ INTO the Addons/ implementation
2. Place the merged result in `shader/Helper/` (or a unified location)
3. Update .fx file includes to point to the merged versions
4. Remove the incomplete Helper/ patches

**Effort:** 3-4 hours for all 5 effect files

---

### 1.2 BUG: Weather Parameter Undeclared Identifier (screenshot bug)

**Severity:** HIGH — `enbbloom.fx` fails to compile in-game
**File:** `shader/enbbloom.fx:134` — `SB_WP_BloomInt` undeclared
**Evidence:** `bugs/Screenshot 2026-02-27 214720.png` — ENBSeries compile error

**Root Cause Analysis:**
The bug screenshot was taken at 21:47. The `SkyrimBridge.fxh` in `shader/Helper/` was updated at 21:56 (57,251 bytes) — 9 minutes AFTER the screenshot. The IMPROVED version (53,878 bytes, from 20:04) likely did NOT contain the `SB_WP_*` declarations. The 21:56 edit appears to have added the weather parameter section (Section 18b) to fix this.

**Fix:**
- Verify the current `shader/Helper/SkyrimBridge.fxh` (57KB version) contains all `SB_WP_*` declarations — CONFIRMED present at lines 325-361
- Test in-game that `enbbloom.fx` compiles without errors with the updated header
- Verify ALL shaders that use `#define SB_WEATHER_PARAMS` compile cleanly

**Effort:** 30 minutes (in-game verification)

### 1.3 BUG: SkyrimBridge.fxh Version Divergence

**Severity:** HIGH — IMPROVED directory is stale
**Problem:** Three different versions of `SkyrimBridge.fxh` exist:
  - `shader/Helper/SkyrimBridge.fxh` — **57,251 bytes** (21:56) — NEWEST, has WP section
  - `Package Files for Analysis/IMPROVED/SkyrimBridge.fxh` — **53,878 bytes** (20:04) — MISSING WP section
  - `Package Files for Analysis/SkyrimBridge.fxh` — older baseline

**Fix:**
- Declare `shader/Helper/SkyrimBridge.fxh` as the single source of truth
- Delete or archive the IMPROVED copy (or replace it with the current version)
- Add a `// CANONICAL COPY — do not edit copies elsewhere` comment to the header

**Effort:** 10 minutes

### 1.4 BUG: main_integration.cpp References Non-Existent Trackers

**Severity:** HIGH — code won't compile if integrated as-is
**File:** `src/main_integration.cpp:75-83`
**Problem:** References 7 tracker namespaces that don't exist in the codebase:
```cpp
data.imageSpace  = ImageSpaceTracker::Update();   // NOT IMPLEMENTED
data.lights      = LightTracker::Update();         // NOT IMPLEMENTED
data.actorValues = ActorValueTracker::Update();    // NOT IMPLEMENTED
data.crosshair   = CrosshairTracker::Update();     // NOT IMPLEMENTED
data.equipment   = EquipmentTracker::Update();     // NOT IMPLEMENTED
data.quests      = QuestTracker::Update();          // NOT IMPLEMENTED
data.uiState     = UIStateTracker::Update();        // NOT IMPLEMENTED
```

**Fix (choose one):**
- **Option A:** Comment out the unimplemented trackers and add `// TODO: Phase 2 expansion` markers. Build will work immediately.
- **Option B:** Implement stub versions that return zero-initialized structs. Allows gradual development.
- **Option C:** Gate with `#ifdef SKYRIMBRIDGE_PHASE2_TRACKERS` so they compile out cleanly.

**Effort:** 15 minutes (Option A) to 4-6 hours (Option B — full implementation)

### 1.5 STRUCTURAL: Cross-Directory Build Dependency

**Severity:** MEDIUM — fragile, breaks if directories move
**File:** `CMakeLists.txt:24`
**Problem:** `set(PARENT_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../SkyrimBridgeOldSources/src")` — Phase 1 source code lives in a DIFFERENT directory than the v3 project.

**Fix:**
- Copy the 15 Phase 1 `.cpp`/`.h` files from `SkyrimBridgeOldSources/src/` into `SkyrimBridge_v3/src/core/`
- Update `CMakeLists.txt` to reference local paths
- This makes `SkyrimBridge_v3/` fully self-contained and portable

**Effort:** 30 minutes

### 1.6 CRITICAL: No Version Control

**Severity:** HIGH — one bad edit = lost work
**Problem:** This is NOT a git repository. There are no backups, no branching, no history.

**Fix:**
```bash
cd C:\Users\Zain\SKSE
git init
git add SkyrimBridge_v3/
git commit -m "Initial commit: SkyrimBridge v3.0.0"
```
- Add `.gitignore` for `build/`, `*.dll`, `vcpkg_installed/`
- Consider GitHub private repo for remote backup
- The `SkyrimBridgeOldSources/` directory can be added as an archive commit or kept separate

**Effort:** 15 minutes

---

## 2. Project Organization & Hygiene {#2-project-organization}

### 2.1 Consolidate Directory Structure

**Current State (chaotic):**
```
SKSE/
├── SkyrimBridge_v3/              ← Active dev
│   ├── src/                      ← Only Phases 2-4 (3 files)
│   ├── shader/                   ← Deployed shaders (partially merged)
│   ├── Package Files for Analysis/ ← 3 versions of every shader
│   │   └── IMPROVED/             ← Stale copy
│   └── docs/
└── SkyrimBridgeOldSources/       ← Phase 1 sources + 6 archived versions
    ├── src/                      ← ACTUAL Phase 1 source code
    ├── shader/                   ← Yet another copy of shaders
    ├── SkyrimBridge1.2.0/
    ├── SkyrimBridge_v1.0.1_pipeline_fix/
    ├── SkyrimBridge_v3/          ← Confusingly named OLD v3
    ├── SilentHorizons_Install/
    ├── SilentHorizons_Release/
    └── package/
```

**Target State (clean):**
```
SKSE/SkyrimBridge_v3/             ← Single project root
├── src/
│   ├── core/                     ← Phase 1 (15 tracker modules + main)
│   ├── weather/                  ← Phase 2 (WeatherParameterComputer)
│   ├── bridge/                   ← Phase 3 (SharedMemoryBridge)
│   ├── compat/                   ← Phase 4 (ParmLinkCompat)
│   └── render/                   ← Layer 4 (GPU pipeline, when ready)
├── shader/
│   ├── enbseries/                ← Deployment-ready shader tree
│   │   ├── *.fx                  ← 8 core effect files
│   │   └── Helper/               ← All .fxh includes
│   └── addons/                   ← SB_ addon shaders
├── config/
├── docs/
├── tools/                        ← Build scripts, installers
└── archive/                      ← Old versions (reference only)
```

**Steps:**
1. Copy Phase 1 sources into `src/core/`
2. Move Phase 2/3/4 into proper subdirectories
3. Move `Package Files for Analysis/` to `archive/analysis/`
4. Update CMakeLists.txt for new paths
5. Verify build still works

**Effort:** 1-2 hours

### 2.2 Establish Single Source of Truth for Shaders

**Problem:** Same shader exists in 4+ locations with different versions:
- `shader/` (deployed — most recent)
- `Package Files for Analysis/` (original baseline)
- `Package Files for Analysis/IMPROVED/` (intermediate, stale)
- `SkyrimBridgeOldSources/shader/` (old)

**Fix:**
- `shader/` is the ONLY authoritative location
- Delete or archive all other copies
- Never edit shaders in Package Files or OldSources

**Effort:** 15 minutes

### 2.3 Clean Up Root SKSE Directory

**Problem:** Random files in root:
- `Screenshot 2026-02-27 155254.png` — stray screenshot
- `nul` — empty 46-byte file (likely accidental)

**Fix:** Delete or move to appropriate locations.

**Effort:** 2 minutes

---

## 3. C++ DLL Improvements {#3-cpp-dll-improvements}

### 3.1 Implement Missing Tracker Stubs

Create the 7 missing trackers referenced by `main_integration.cpp`. These provide the data for the 62 unused shader parameters.

| Tracker | Data Provided | RE Difficulty |
|---------|---------------|---------------|
| ImageSpaceTracker | HDR params, cinematic effects, DOF engine settings | Low — `RE::ImageSpaceManager` |
| LightTracker | 3 nearest point lights (pos, radius, color) | Medium — `RE::ShadowSceneNode` iteration |
| ActorValueTracker | Extended actor values (resistances, skills) | Low — `RE::PlayerCharacter::GetActorValue()` |
| CrosshairTracker | Crosshair target ref, distance, name | Low — `RE::CrosshairPickData` |
| EquipmentTracker | Weapon type, spell school, enchantment | Medium — `RE::ExtraEnchantment` |
| QuestTracker | Active quest stage, objective | Low — `RE::TESQuestManager` |
| UIStateTracker | Active menus, HUD state, loading screen | Low — `RE::UI::GetSingleton()` |

**Priority Implementation Order:**
1. ImageSpaceTracker (enables AGIS in enbeffect.fx)
2. LightTracker (enables point light bloom)
3. CrosshairTracker (enables combat-aware DOF)
4. EquipmentTracker (enables spell-reactive FX)
5. UIStateTracker (enables menu-aware effects)
6. ActorValueTracker (enables resistance overlays)
7. QuestTracker (lowest priority — niche use)

**Effort:** 2-3 hours per tracker (14-21 hours total)

### 3.2 Fix Known C++ Bugs (from IMPROVEMENTS.md)

The IMPROVEMENTS.md documents 14 fixes. Verify all are applied in the current OldSources/src/ files:

- [ ] Player speed: velocity not displacement (frame-rate independent)
- [ ] SunColor.rgb populated from weather colorData
- [ ] ShadowTracker LightDiffuse/Ambient populated
- [ ] Wind direction: proper 0-2π radians
- [ ] Lightning flash: stochastic model implemented
- [ ] Frame counter wraps at 2^20
- [ ] enbdepthoffield.fx in target list
- [ ] Surface wetness SB_Precip_Surface parameter
- [ ] Dirty tracking for parameter push
- [ ] Detect Dead vision effect tracked
- [ ] Version string centralized
- [ ] Window resize ImGui crash fixed
- [ ] DebugGUI version display

**Effort:** 1-2 hours to audit and verify

### 3.3 WeatherParameterComputer Robustness

**Current Issues:**
- Expression compiler fallback returns 0 (incomplete)
- Address redirect table has no concrete implementations
- `SB_GetWP()` shader function treats 0.0 as "unset" — problematic if a weather param genuinely equals 0.0

**Fixes:**
- Use a sentinel value (e.g., `SB_WP_State.w = 1.0` when active) instead of checking individual param != 0
- Complete the expression fallback or remove the dead code path
- Add unit tests for weather classification edge cases

**Effort:** 2-3 hours

### 3.4 ParmLinkCompat Completion

**Current Issues:**
- Regex-based expression parser handles only `lerp`, `smoothstep`, `clamp`
- Complex expressions silently return 0
- No address redirect implementations

**Fixes:**
- Implement basic arithmetic tree-walking evaluator (+-*/^)
- Map the ~30 most common ParmLink addresses to SB data fields
- Log warnings for expressions that fall to the 0 fallback
- Test with real-world enbParmLink.cfg files from popular ENB presets

**Effort:** 4-6 hours

### 3.5 SharedMemoryBridge Verification

**Current State:** Code written but untested.

**Verification Steps:**
- Build a minimal C++ consumer that reads the shared memory
- Verify AllData struct alignment (packed pragma)
- Test with OBS plugin or Python script
- Document the external consumer API

**Effort:** 2-3 hours

---

## 4. Shader Fixes & Improvements {#4-shader-fixes}

### 4.1 Fix Broken Shaders (from AUDIT_v3.3)

**4.1.1 Volumetric Compute Shadow Sampling (BROKEN)**
- **File:** `enbeffectprepass.fx` (volumetric section)
- **Bug:** Uses camera ViewProj matrix to sample shadow maps instead of light ViewProj
- **Impact:** Shadow contribution in volumetric fog is incorrect
- **Fix:** Replace with screen-space radial inscatter: march from pixel toward sun position, test depth buffer
- **Effort:** 3-4 hours

**4.1.2 Wet Surface World Position (BROKEN)**
- **File:** `enbeffectprepass.fx` or postpass
- **Bug:** Puddle noise uses screen UV instead of world XZ coordinates — puddles "swim" when camera rotates
- **Impact:** Puddles look wrong on any camera movement
- **Fix:** Reconstruct world position via `SB_Camera_InvViewProj * screenPos`, use `worldPos.xz` for puddle noise UVs
- **Effort:** 1 hour

### 4.2 Resolve Duplicate Shaders

**Problem (from AUDIT):**
- Duplicate SSR implementations (old and new)
- Duplicate volumetric/crepuscular shaders

**Fix:**
- Identify which version is active (loaded by ENB technique)
- Remove or `#ifdef` out the dead code
- Consolidate shared logic into helper .fxh files

**Effort:** 1-2 hours

### 4.3 Fix Include Path Architecture

**Problem:** ENB loads shaders from `[SkyrimRoot]/enbseries/`. The include paths in .fx files use `"Helper/SkyrimBridge.fxh"`. This means the deployed directory structure MUST be:
```
enbseries/
├── enbbloom.fx
├── enbeffect.fx
├── ...
└── Helper/
    ├── SkyrimBridge.fxh
    ├── enbHelper_Common.fxh  ← MISSING
    ├── enbHelper_Dither.fxh  ← MISSING
    ├── Effect_*.fxh
    └── enbUI_*.fxh
```

**Fix:**
- Copy `enbHelper_Common.fxh` and `enbHelper_Dither.fxh` into `shader/Helper/`
- Audit ALL `#include` directives across all .fx files
- Create a validation script that checks every include resolves

**Effort:** 30 minutes

### 4.4 Unify Shader Version Headers

**Problem:** Some shaders say v1.0, some v2.0, version comments are inconsistent.

**Fix:**
- Standardize all shader headers to v3.0.0
- Add consistent metadata block: version, author, last-modified date, SkyrimBridge.fxh version requirement

**Effort:** 30 minutes

---

## 5. Performance Optimization {#5-performance-optimization}

### 5.1 DOF Bokeh Sample Count (HIGH IMPACT)

**File:** `enbdepthoffield.fx`
**Problem:** Up to 90 samples per pixel at FULL resolution
**Fix:** Cap at 36 samples, render large-CoC bokeh at half resolution
**Expected Savings:** 50-60% DOF cost reduction
**Effort:** 1 hour

### 5.2 Anamorphic Flare Sample Count

**File:** `enblens.fx` or `enbsunsprite.fx`
**Problem:** Unbounded 101-sample loop
**Fix:** Hard cap at 16 samples regardless of width setting
**Expected Savings:** 80% reduction in flare pass cost
**Effort:** 30 minutes

### 5.3 SSS Tap Reduction

**File:** `enbeffectprepass.fx`
**Problem:** 25 taps x 2 passes = 50 texture fetches per skin pixel
**Fix:** Reduce to 17 taps (-8 to +8) — still excellent Christensen-Burley quality
**Expected Savings:** 32% reduction in SSS pass cost
**Effort:** 30 minutes

### 5.4 C++ Dirty Tracking Optimization

**Problem:** Currently pushes all ~120 float4 params every frame (420+ ENB API calls)
**Improvement documented:** Dirty tracking reduces to 30-50% of calls

**Verify:**
- Confirm dirty tracking is implemented in current ENBInterface.cpp
- If not, implement per-parameter change detection
- Use memcmp on each float4 before calling ENBSetParameter()

**Effort:** 1-2 hours

### 5.5 Shader LOD / Quality Tiers

**Problem:** No way for users to reduce effect quality for performance
**Fix:** Add a `SB_QUALITY` define (0=Low, 1=Medium, 2=High, 3=Ultra):
- Controls sample counts in DOF, SSS, GTAO, volumetrics
- Reduces resolution of blur passes
- Skips expensive features (bokeh shapes, anisotropic Kuwahara)

**Effort:** 2-3 hours

---

## 6. Feature Activation (62 Unused Parameters) {#6-feature-activation}

These parameters are ALREADY being pushed by the DLL but NO shader reads them. Each represents a quick-win feature.

### 6.1 Combat-Aware DOF (HIGHEST IMPACT)

**Parameters:** `SB_XHair_Info.y` (target distance), `SB_Player_Combat.x` (combat state)
**Shader:** `enbdepthoffield.fx`
**Implementation:**
```hlsl
float targetDist = SB_XHair_Info.y;
float inCombat = SB_Player_Combat.x;
float focusDist = lerp(sceneFocusDist, targetDist, inCombat * 0.8);
```
**Impact:** DOF that tracks your combat target — no other ENB preset can do this
**Effort:** 2-3 hours

### 6.2 Interior Directional Lighting Fix

**Parameters:** `SB_Interior_DirDir`, `EInteriorFactor`
**Shaders:** `enbeffectprepass.fx` (contact shadows), `Effect_AtmosphericFog.fxh`
**Implementation:** Swap `SB_Sun_Direction` for `SB_Interior_DirDir` when interior detected
**Impact:** Contact shadows and fog scatter use correct light direction indoors
**Effort:** 1 hour

### 6.3 Point Light Bloom

**Parameters:** `SB_Light0/1/2_PosRad`, `SB_Light0/1/2_Color`
**Shader:** `enbbloom.fx`
**Implementation:** Project light world positions to screen space, add localized bloom tinted by light color
**Impact:** Torches, campfires, and magic lights produce visible bloom halos
**Effort:** 2 hours

### 6.4 Weather Transition Smoothing

**Parameters:** `SB_Weather_Transition.x`
**Shaders:** `enbeffect.fx`, `enbadaptation.fx`, `Effect_AtmosphericFog.fxh`
**Implementation:** Use transition factor to blend effect parameters instead of snapping
**Impact:** Eliminates jarring visual pops when weather changes
**Effort:** 1 hour

### 6.5 Moon Phase Night Lighting

**Parameters:** `SB_Masser_NDC.w` (moon phase), `SB_Secunda_NDC`
**Shaders:** `enbadaptation.fx`, `Effect_AtmosphericFog.fxh`
**Implementation:** Modulate night ambient by moon phase (full moon = brighter, new moon = darker)
**Impact:** Nights feel different depending on moon cycle — unique to Elder Scrolls
**Effort:** 1-2 hours

### 6.6 Surface Wetness

**Parameters:** `SB_Precip_Surface` (wetness accumulation/drying)
**Shader:** `enbeffectprepass.fx`
**Implementation:** Darken surfaces + boost specular when wet, dry gradually after rain stops
**Impact:** World responds to rain/snow realistically
**Effort:** 1-2 hours (requires world position fix from 4.1.2)

### 6.7 Spell School Color Filtering

**Parameters:** `SB_Equip_Right.z`, `SB_Equip_Left.z`
**Shader:** `enbeffectpostpass.fx`
**Implementation:** Subtle color tint based on active spell school (purple for conjuration, gold for restoration, etc.)
**Impact:** Casting spells creates atmospheric screen effects
**Effort:** 2-3 hours

### 6.8 Health Vignette

**Parameters:** `SB_Player_Vitals.x` (health %), `SB_FX_Damage`
**Shader:** `enbeffectpostpass.fx`
**Implementation:** Blood-red vignette at <25% health, heartbeat pulse at <10%, directional flash on damage
**Impact:** Visceral gameplay feedback through post-processing
**Effort:** 2-3 hours

---

## 7. New Feature Development {#7-new-features}

### 7.0 Underwater Shader SkyrimBridge Integration

**File:** `shader/enbunderwater.fx` (1,574 lines — largest shader in the suite)
**Current State:** Zero SkyrimBridge integration. Uses `UI/enbUI_Primer.fxh` + `Helper/enbHelper_Common.fxh` only.

**What this shader does (already implemented, no SB):**
- PBR spectral water absorption (Beer-Lambert per-channel)
- Caustics (dual-layer noise or Voronoi)
- Underwater god rays
- Floating particles & sediment
- Procedural bubbles
- Snell's window / total internal reflection
- Bioluminescence particles
- Wet lens (near-surface droplets)
- Tyndall / directional particulate scattering
- Wave distortion with current simulation
- Depth blur with edge bleed reduction

**SkyrimBridge enhancements to add:**
1. `SB_Sun_Direction` → Accurate god ray direction (replaces approximation)
2. `SB_Player_Water.z` → Player depth → modulate absorption, blur, pressure effects
3. `SB_Weather_Wind` → Wind speed → wave distortion intensity + current direction
4. `SB_Weather_Precip` → Rain → surface ripple intensity boost
5. `SB_Fog_Height.x` → Water surface Z → better depth-from-surface calculation
6. `SB_Atmos_Sunlight` → Sunlight color → caustic tint + god ray color
7. `SB_Sun_NDC` → Sun screen position → god ray origin point
8. `SB_Celestial_Time` → Time of day → bioluminescence at night only
9. `SB_Interior_Flags` → Interior detection → different water behavior indoors

**Impact:** The underwater shader is already the most complex single shader (73KB). Adding SB data would make Skyrim's underwater experience feel truly dynamic — waves respond to weather, visibility changes with depth, god rays track the actual sun.

**Effort:** 2-3 hours

### 7.1 Moon God Rays (UNIQUE FEATURE)

**No other ENB preset has ever implemented this.**
**Parameters:** `SB_Masser_NDC`, `SB_Secunda_NDC`, `SB_Masser_Direction`, `SB_Secunda_Direction`
**Implementation:** New `Effect_MoonGodRays.fxh` or integrate into `enbsunsprite.fx`
- Ray march toward moon screen position at night
- Scale intensity by moon phase
- Apply cooler color temperature than sun rays
- Support BOTH moons during appropriate phases

**Effort:** 3-4 hours

### 7.2 Biome/Region Detection

**Implementation:** C++ lookup table mapping worldspace cell coordinates to climate zones
**Push:** `SB_Region_Type` (tundra, forest, volcanic, coastal, etc.)
**Shader Use:** Region-specific color grading, fog density, atmospheric scatter

**Effort:** 4-6 hours

### 7.3 Audio-Reactive Visuals

**Implementation:** Hook `RE::BSAudioManager` to detect music state
**Push:** `SB_Audio_Music` (combat, exploration, dungeon, town)
**Shader Use:** Combat music triggers more contrast/saturation, exploration = warmer tones

**Effort:** 4-6 hours

### 7.4 PerfGov (Self-Tuning Performance)

**Implementation:** Monitor GPU frame time, dynamically adjust shader quality
**Push:** `SB_Perf_Budget` (remaining ms in frame), `SB_Perf_Quality` (0-1 quality slider)
**Shader Use:** Reduce sample counts, skip expensive passes, lower resolution when needed
**Impact:** First ENB preset that maintains framerate automatically

**Effort:** 8-10 hours

---

## 8. Build System & Tooling {#8-build-system}

### 8.1 Self-Contained Build

After consolidating source files (Step 2.1), update CMakeLists.txt:
- Remove `PARENT_SRC_DIR` reference
- All sources under `SkyrimBridge_v3/src/`
- No external directory dependencies

**Effort:** 30 minutes (after 2.1)

### 8.2 Add Shader Validation Script

Create `tools/validate_shaders.py` or `tools/validate_shaders.bat`:
- Parse every `.fx` file for `#include` directives
- Verify each included file exists at the expected path
- Report missing files, version mismatches, orphaned helpers
- Run as pre-deploy check

**Effort:** 1 hour

### 8.3 Add Deploy Script

Create `tools/deploy.bat`:
```batch
@echo off
REM Copy DLL to Skyrim SKSE/Plugins
REM Copy shader/ contents to enbseries/
REM Copy config/ to SKSE/Plugins/SkyrimBridge/
```
- Configurable Skyrim install path
- Validates file existence before copy
- Optional MO2 overwrite detection

**Effort:** 30 minutes

### 8.4 Add CI-Friendly Build

- Ensure `build.bat` returns proper exit codes
- Add `--clean` flag for full rebuild
- Add `--deploy` flag to invoke deploy script
- Print DLL version and size on success

**Effort:** 30 minutes

---

## 9. Distribution & Release {#9-distribution}

### 9.1 FOMOD Installer

```
SkyrimBridge_v3.0.0/
├── fomod/
│   ├── info.xml           ← Mod name, version, author, description
│   └── ModuleConfig.xml   ← Installation options
├── 00_Core/
│   └── SKSE/Plugins/
│       ├── SkyrimBridge_v3.dll
│       └── SkyrimBridge/
│           └── WeatherParams.ini
├── 01_Shaders_Full/
│   └── enbseries/
│       ├── *.fx
│       └── Helper/*.fxh
├── 02_Shaders_HeaderOnly/
│   └── enbseries/
│       └── Helper/SkyrimBridge.fxh   ← For users with existing ENB preset
└── 03_Config_Templates/
    └── WeatherParams_*.ini           ← Per-preset templates
```

**Installation Options:**
- Core DLL (required)
- Full shader suite (for standalone use)
- Header only (for integrating into existing ENB preset)
- Config templates (preset-specific weather params)

**Effort:** 2-3 hours

### 9.2 Compatibility Patches

Create minimal patches for popular ENB presets that add SkyrimBridge awareness:

| Preset | Changes Needed | Effort |
|--------|----------------|--------|
| Rudy ENB | Add `#include` + weather/combat hooks | 1-2 hours |
| NAT.ENB | Add `#include` + atmosphere hooks | 1-2 hours |
| Pi-CHO ENB | Add `#include` + bloom/DOF hooks | 1-2 hours |
| Serio's ENB | Add `#include` + minimal hooks | 1 hour |

**Total Effort:** 4-8 hours

### 9.3 End-User Documentation

- Installation guide (manual, MO2, Vortex)
- Feature list with comparison screenshots
- Configuration guide (WeatherParams.ini editing)
- Troubleshooting FAQ
- For shader authors: SkyrimBridge.fxh API reference

**Effort:** 3-4 hours

---

## 10. Long-Term Architecture (v4.0 Vision) {#10-long-term}

### 10.1 Layer 4: GPU Render Pipeline

Re-enable the disabled render pipeline from `SilentHorizons_Install/src/`:

**Steps:**
1. Audit all API mismatches in RenderPipeline, GBufferPass, SSGI, ShaderHooks
2. Create missing `ViewportBackend.h` stub
3. Fix `PatchVTable` and `fnOMSetRTs` declarations
4. Gate behind `#ifdef SB_ENABLE_LAYER4` compile flag
5. Test G-Buffer capture (material ID, normals, depth)
6. Test SSGI integration

**Impact:** Enables true per-pixel material classification for all addon shaders
**Effort:** 8-12 hours

### 10.2 Bidirectional Architecture (v4.0)

As outlined in `SB_Bidirectional_Architecture.md`:
- Transform from read-only data bridge to write-enabled authoring platform
- Real-time weather/lighting editing with ENB preview
- Export to xEdit, CK, KreatE formats
- Would position SkyrimBridge as THE visual development environment for Skyrim

**Effort:** 40-80 hours (major version milestone)

### 10.3 Community Shaders Backend

Create an alternative data consumer for Community Shaders (via structured buffer at t30):
- `SkyrimBridge_CS.hlsl` already exists as a prototype
- Allows same game data to drive CS effects when ENB is not present
- Expands addressable market significantly

**Effort:** 8-12 hours

---

## 11. Execution Order & Dependencies {#11-execution-order}

### IMMEDIATE (Do First — Foundation)

| # | Task | Depends On | Effort | Impact |
|---|------|------------|--------|--------|
| 1 | Init git repository | — | 15 min | Safety net |
| 2 | ~~Copy missing shader includes (1.1)~~ DONE | — | — | Game build resolved this |
| 2b | Create stubs for 4 missing Addons/ (1.7) | — | 10 min | Unblocks prepass compile |
| 3 | Verify SkyrimBridge.fxh fix (1.2) | — | 30 min | Unblocks bloom |
| 4 | Consolidate SkyrimBridge.fxh (1.3) | 3 | 10 min | Prevents confusion |
| 5 | Fix main_integration.cpp stubs (1.4) | — | 15 min | Unblocks build |
| 6 | Copy Phase 1 sources local (1.5) | — | 30 min | Self-contained |
| 7 | Audit all shader includes (4.3) | 2b | 30 min | No more missing files |
| 7b | Decide UI/ vs Helper/ enbUI strategy (1.9) | 7 | 30 min | Resolve dual-version confusion |
| 8 | Verify DLL build compiles | 5, 6 | 15 min | Baseline confirmed |
| 9 | In-game smoke test | 7, 8 | 30 min | Everything works |

**Checkpoint: Buildable, deployable, no compile errors.**

### SESSION 2 (Quick Wins — Activate Unused Parameters)

| # | Task | Depends On | Effort | Impact |
|---|------|------------|--------|--------|
| 10 | Weather transition smoothing (6.4) | 9 | 1 hr | ★★★★☆ |
| 11 | Interior directional fix (6.2) | 9 | 1 hr | ★★★★☆ |
| 12 | Fix wet surface world pos (4.1.2) | 9 | 1 hr | ★★★☆☆ |
| 13 | Moon phase night lighting (6.5) | 9 | 1-2 hr | ★★★★☆ |

### SESSION 3 (High Impact Features)

| # | Task | Depends On | Effort | Impact |
|---|------|------------|--------|--------|
| 14 | Combat-aware DOF (6.1) | 9 | 2-3 hr | ★★★★★ |
| 15 | Point light bloom (6.3) | 9 | 2 hr | ★★★★☆ |
| 16 | Health vignette (6.8) | 9 | 2-3 hr | ★★★★☆ |

### SESSION 4 (Unique Features)

| # | Task | Depends On | Effort | Impact |
|---|------|------------|--------|--------|
| 17 | Moon god rays (7.1) | 13 | 3-4 hr | ★★★★★ |
| 18 | Spell school FX (6.7) | 9 | 2-3 hr | ★★★★☆ |
| 19 | Surface wetness (6.6) | 12 | 1-2 hr | ★★★★☆ |

### SESSION 5 (Performance)

| # | Task | Depends On | Effort | Impact |
|---|------|------------|--------|--------|
| 20 | DOF sample cap (5.1) | 9 | 1 hr | ★★★★★ |
| 21 | Anamorphic cap (5.2) | 9 | 30 min | ★★★☆☆ |
| 22 | SSS tap reduction (5.3) | 9 | 30 min | ★★★☆☆ |
| 23 | Volumetric shadow fix (4.1.1) | 9 | 3-4 hr | ★★★★☆ |
| 24 | Dirty tracking verify (5.4) | 8 | 1-2 hr | ★★★☆☆ |

### SESSION 6 (Distribution)

| # | Task | Depends On | Effort | Impact |
|---|------|------------|--------|--------|
| 25 | FOMOD installer (9.1) | 9 | 2-3 hr | ★★★★★ |
| 26 | End-user docs (9.3) | 25 | 3-4 hr | ★★★★★ |
| 27 | Deploy script (8.3) | 6 | 30 min | ★★★☆☆ |
| 28 | Shader validation script (8.2) | 7 | 1 hr | ★★★☆☆ |

### LATER (When Time Permits)

| # | Task | Effort | Impact |
|---|------|--------|--------|
| 29 | Implement 7 missing trackers (3.1) | 14-21 hr | ★★★★★ |
| 30 | Complete ParmLinkCompat (3.4) | 4-6 hr | ★★★☆☆ |
| 31 | Layer 4 reconciliation (10.1) | 8-12 hr | ★★★☆☆ |
| 32 | ENB preset patches (9.2) | 4-8 hr | ★★★★★ |
| 33 | PerfGov (7.4) | 8-10 hr | ★★★★☆ |
| 34 | Biome detection (7.2) | 4-6 hr | ★★★☆☆ |
| 35 | Audio-reactive (7.3) | 4-6 hr | ★★★☆☆ |
| 36 | Bidirectional v4.0 (10.2) | 40-80 hr | ★★★★★ |

---

## Summary Metrics

| Category | Tasks | Total Effort | Combined Impact |
|----------|-------|--------------|-----------------|
| Critical Fixes (Section 1) | 6 | 2-3 hours | Foundation |
| Organization (Section 2) | 3 | 2-3 hours | Maintainability |
| C++ Improvements (Section 3) | 5 | 10-15 hours | Data quality |
| Shader Fixes (Section 4) | 4 | 5-7 hours | Visual correctness |
| Performance (Section 5) | 5 | 4-6 hours | FPS recovery |
| Feature Activation (Section 6) | 8 | 12-18 hours | ★★★★★ |
| New Features (Section 7) | 4 | 20-28 hours | Innovation |
| Build & Tools (Section 8) | 4 | 2-3 hours | Developer QOL |
| Distribution (Section 9) | 3 | 9-15 hours | Community reach |
| Long-Term (Section 10) | 3 | 56-104 hours | v4.0 vision |

**Total Estimated Effort: ~120-200 hours across all sections**
**Immediate foundation work: ~3 hours to stable baseline**
**First release candidate: ~30-40 hours (Sections 1-6 + 8-9)**

---

*Generated from analysis of 260+ source files, 150+ shader files, 33+ documents, and 8 build configurations across the complete SkyrimBridge project tree.*
