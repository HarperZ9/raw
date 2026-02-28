# Silent Horizons — Full Codebase Analysis & Improvement Plan

**Scope:** 27 `.fx` shaders + 22 `.fxh` headers · 65,400+ lines · 55 files  
**Date:** February 2026

---

## Executive Summary

The codebase has strong algorithmic foundations — physically-motivated rendering, production-quality SkyrimBridge integration, sophisticated UI layering. However, the rapid rebuild session introduced **systemic constant buffer issues** and **texture binding errors** that cascade across multiple shaders. There are also housekeeping opportunities around variant consolidation, include guards, and dead include removal.

**Critical (breaks rendering):** 3 issues  
**High (risks overflow or compilation):** 4 issues  
**Medium (quality/maintenance):** 5 issues  
**Low (polish):** 3 issues

---

## 🔴 CRITICAL Issues

### C1. Adaptation Shader — Wrong Texture Bindings

**Files:** `enbadaptation.fx`, `__4_`, `__5_`, `__6_` (all 4 variants)

ENB's adaptation pipeline provides **completely different** textures than standard post shaders:

| Expected (Adaptation) | Declared (Broken) |
|---|---|
| `TextureCurrent` (256×256 downscaled scene) | `TextureColor` (full-res) |
| `TexturePrevious` (1×1 previous frame) | `TextureAdaptation` (wrong) |
| `AdaptationParameters` (float4: min, max, sens, dt) | Not declared |

Only `enbadaptation__7_.fx` (Kingeric1992 original, 224 lines) has correct bindings.

Additionally, all 4 broken variants have a **single technique** instead of the required **two-technique structure** (Downsample 256→16, then Draw 16→1), and use the wrong VS signature (`VS_Basic` with float4 txcoord instead of `VS_Quad` with float2 + -7/256 offset).

**Impact:** Diagonal screen split, frozen/static image, completely non-functional adaptation.

**Fix delivered:** New `enbadaptation.fx` with correct bindings, two-technique structure, VS_Quad, and all advanced features (focus metering, histogram percentile anchoring, 7-TOD, SkyrimBridge modulation, asymmetric temporal smoothing) preserved.

### C2. Constant Buffer Overflow — Unused SkyrimBridge Include

**Files with 103 unused float4 SB params:**

| Shader | SB_ Usage | UI Includes | Estimated CB Slots | Risk |
|---|---|---|---|---|
| `enbeffectpostpass__1_.fx` | **0** | 6 | ~437 | 🔴 OVERFLOW |
| `enbeffectpostpass__5_.fx` | **0** | 6 | ~437 | 🔴 OVERFLOW |
| `enbdepthoffield.fx` | **0** | 6 | ~240+ | 🔴 OVERFLOW |
| `enbeffectprepass__1_.fx` | **0** | 9 | ~330+ | 🔴 OVERFLOW |
| `enblens.fx` | **0** (SB_ names are UI params, not SkyrimBridge data) | 40 | 600+ | 🟡 Possible |

All include `#include "Helper/SkyrimBridge.fxh"` adding 103 float4 constant buffer entries while referencing **zero** SB data parameters. ENB's constant buffer limit is typically 256-512 float4 slots.

**Fix delivered:** Postpass and DOF. Remaining: prepass v1, lens.

### C3. Adaptation Variant Duplication

`enbadaptation.fx` = `__4_` = `__5_` (identical MD5 hashes). `__6_` differs only in include order (enbHelper_Common moved from line 45 to line 97). All 4 have the same broken bindings.

**Impact:** Confusion about which file is deployed, wasted maintenance.

---

## 🟠 HIGH Issues

### H1. Prepass — Full SB Include with Heavy UI (Overflow Risk)

**File:** `enbeffectprepass.fx`

Uses only **4 SB data params** (`SB_Camera_Info`, `SB_Fog_FarColor`, `SB_Fog_NearColor`, `SB_Lightning` + `SB_IsActive` → `SB_Render_Frame`) but includes all 103. Combined with 9 UI include passes across 8 SHADERGROUP blocks, the total CB footprint is ~330+ slots.

**Recommendation:** Inline the 5 needed float4s instead of the full header.

### H2. Missing `enbglobals.fxh` Dependency

**Files:** `enbdepthoffield.fx` (line 136), `enblens.fx` (line 89)

Both include `enbglobals.fxh` which is NOT in the project. The include is inside `#if !LOCAL_OVERRIDE` — so it only compiles when `LOCAL_OVERRIDE` is not defined. This is a Kitsuune convention where enbglobals.fxh defines compile-time `#define` flags.

**Risk:** Will fail to compile if `LOCAL_OVERRIDE` isn't defined in the user's `enbdepthoffield_fx.ini` / `enblens_fx.ini`.

**Recommendation:** Either ship `enbglobals.fxh` with the preset, or ensure all INI files define `LOCAL_OVERRIDE 1`, or make the include error out gracefully with `#ifndef` guard and default defines.

### H3. Missing Addon Includes in Prepass

**Files:** `enbeffectprepass.fx`, `enbeffectprepass__1_.fx`

Both include 4 unguarded addon files:
- `Addons/PrePass_StylizationSuite.fxh`
- `Addons/PrePass_ParticleField.fxh`
- `Addons/PrePass_PhotoStudio.fxh`
- `Addons/PrePass_SnowCover.fxh`

These are NOT in the project. If the Addons/ directory is missing from the user's install, compilation fails.

**Recommendation:** Wrap each include with `#if __has_include("Addons/PrePass_StylizationSuite.fxh")` or define `*_LOADED` to 0 by default and let PrePassAddonTechniques.fxh handle the no-addon case (which it already does — if none are loaded, no addon techs are emitted).

### H4. Lens — Namespace Collision with SB_ Prefix

**File:** `enblens.fx`

Kitsuune's lens UI params use `SB_` prefix for Starburst parameters (SB_Strength, SB_Width, SB_Shape, SB_Thresh, etc.) which collides with SkyrimBridge's `SB_` namespace. The shader currently includes SkyrimBridge.fxh but uses **zero** actual SB data params — only the UI-named `SB_*` / `UISB_*` variables.

**Risk:** If any SkyrimBridge data parameter name happened to match a Kitsuune UI name, silent shadowing would occur. Currently no collisions exist, but the full 103-param include is wasteful.

**Recommendation:** Remove `#include "Helper/SkyrimBridge.fxh"` from lens since it's unused.

---

## 🟡 MEDIUM Issues

### M1. Missing Include Guards — 8 Headers

| Header | Lines | Risk |
|---|---|---|
| `Effect_AtmosphericFog.fxh` | 304 | Double-include → redefinition errors |
| `Effect_CRTShader.fxh` | 182 | Same |
| `Effect_CinematicFX.fxh` | 1310 | Same |
| `Effect_ProceduralLensDirt.fxh` | 289 | Same |
| `Effect_ProceduralWeatherFX.fxh` | 772 | Same |
| `PrePassAddonTechniques.fxh` | 67 | Same |
| `enbUI_CRT.fxh` | 39 | Same |
| `enbUI_CinematicFX.fxh` | 140 | Same |
| `enbUI_Fog.fxh` | 62 | Same |
| `enblut.fxh` | 52 | Same |

While most are currently only included once per shader, this is a maintenance hazard.

### M2. SkyrimBridge v1 vs v2 Version Split

`SkyrimBridge.fxh` (103 params, 35 helpers) is the deployed version. `SkyrimBridge__1_.fxh` (105 params, 39 helpers) adds:
- `SB_Vol_Color` (volumetric fog/haze color)
- `SB_Vol_Scatter` (inscatter/extinction coefficients)
- 4 additional helper functions

`SB_VolumetricFog.fx` already references `SB_Vol_Color` and `SB_Vol_Scatter` — but it includes `SkyrimBridge.fxh` (v1), not `SkyrimBridge__1_.fxh` (v2). These params will be undeclared at compile time.

**Recommendation:** Promote v2 (`__1_`) to be the canonical `SkyrimBridge.fxh`.

### M3. Sunsprite — Full SB Include for 3 Params

**File:** `enbsunsprite.fx`

Uses only `SB_IsActive()`, `SB_Sun_Color`, and `SB_Lightning` (3 float4s needed) but includes all 103. With 26 UI includes, total CB footprint is moderate but unnecessary.

**Recommendation:** Inline 3 params + `SB_IsActive()` helper.

### M4. Effect v1 vs v8 — UIGroup Enhancement Not Propagated

`enbeffect__8_.fx` adds UIGroup annotations (`"Bloom Mixing"`, `"Globals"`, `"Developer Tools"`) and Dropdown widgets. `enbeffect.fx` (v1) lacks these. V8 is strictly superior for UI organization.

**Recommendation:** Promote v8 as the canonical version.

### M5. Underwater Variant Consolidation

4 variants: v0 and v4 are Kitsuune originals (no SB), v2 and v3 are SB-integrated (nearly identical, v3 cleaned up duplicate hash aliases). Only one SB version should be canonical.

**Recommendation:** Promote v3 (cleanest SB version) as canonical `enbunderwater.fx`.

---

## 🟢 LOW Issues

### L1. `enbHelper_Common.fxh` — Implicit Dependencies

DNISep() functions use `ENightDayFactor` and `EInteriorFactor` without declaring them. FastLinDepth() depends on the including shader's depth encoding convention. This is standard HLSL header practice but could benefit from a comment documenting the required extern declarations.

### L2. CinematicFX — Documented Bug Comments

`Effect_CinematicFX.fxh` contains v2 bug documentation at lines 762 and 785 (chromatic aberration applied after blur, paired range double-counting center pixel). These appear to be fixed in the current code but the comments should be updated to "v2 bug (FIXED):" for clarity.

### L3. RenderTarget1024 Warning in Lens

`enblens.fx` line 425: `//R16B16G16A16F 64 bit hdr format, 1024*1024 size -- Bugged? Overwrites bloom output somehow`

This should be investigated — if RenderTarget1024 does overwrite bloom, it's a live rendering issue.

---

## Recommended File Consolidation

### Current → Canonical Mapping

| Current Files | Canonical | Action |
|---|---|---|
| `enbadaptation.fx`, `__4_`, `__5_`, `__6_` | **New rebuild** (delivered) | Replace all with fixed version |
| `enbadaptation__7_.fx` | **Archive** | Keep as reference only |
| `enbeffect.fx`, `enbeffect__8_.fx` | **`__8_`** (has UIGroup) | Promote v8 |
| `enbeffectpostpass__1_.fx`, `__5_.fx` | **`__5_`** (has RT fix on SMAA weight) | Remove SB include |
| `enbeffectprepass.fx`, `__1_.fx` | **`enbeffectprepass.fx`** (has SB integration) | Inline 5 SB params |
| `enbunderwater.fx`, `__2_`, `__3_`, `__4_` | **`__3_`** (cleanest SB version) | Promote v3 |
| `SkyrimBridge.fxh`, `__1_.fxh` | **`__1_`** (has Vol params) | Promote v2 |
| `enbUI_PostPass.fxh`, `__4_.fxh` | Investigate which is used | — |

### Post-Consolidation File Count

From 27 `.fx` + 22 `.fxh` (49 total) down to ~18 `.fx` + ~20 `.fxh` (38 total).

---

## Action Items (Priority Order)

1. ✅ **C1 Fixed:** Adaptation shader rebuilt with correct texture bindings
2. ✅ **C2 Partial:** Postpass and DOF SkyrimBridge includes removed
3. **C2 Remaining:** Remove unused SB include from `enblens.fx`, `enbeffectprepass__1_.fx`
4. **H1:** Inline 5 SB params in `enbeffectprepass.fx`
5. **H3:** Guard or provide addon includes for prepass
6. **M2:** Promote SkyrimBridge v2 as canonical
7. **M1:** Add include guards to 10 unguarded headers
8. **C3/M4/M5:** Consolidate variants (adaptation, effect, underwater, postpass)
9. **M3:** Inline 3 SB params in sunsprite
10. **H2:** Resolve enbglobals.fxh dependency
11. **L2/L3:** Clean up bug comments, investigate RT1024

---

## Constant Buffer Budget Summary (Post-Fix)

| Shader | ENB Externs | SB Params | UI Params | Total Est. | Status |
|---|---|---|---|---|---|
| `enbadaptation.fx` (new) | 13 | 16 inline | 46 | ~75 | ✅ Safe |
| `enbbloom.fx` | 14 | 103 full | ~35 | ~152 | ✅ Safe |
| `enbeffect.fx` | 14 | 103 full | ~80 | ~197 | ✅ Safe |
| `enbeffectprepass.fx` | 14 | 103→5 | ~200 | ~320→222 | 🟠→✅ |
| `enbeffectpostpass.fx` (fixed) | 10 | 0 | ~310 | ~320 | ✅ Safe |
| `enbdepthoffield.fx` (fixed) | 14 | 0 | ~130 | ~144 | ✅ Safe |
| `enblens.fx` | 14 | 103→0 | ~250 | ~367→264 | 🟠→✅ |
| `enbsunsprite.fx` | 14 | 103→3 | ~120 | ~237→137 | 🟠→✅ |
| `enbunderwater.fx` | 14 | 103 full | ~60 | ~177 | ✅ Safe |

ENB's typical CB limit: 256-512 float4 slots depending on version.
