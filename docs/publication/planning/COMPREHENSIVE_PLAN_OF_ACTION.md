# SkyrimBridge v3.0 — Comprehensive Plan of Action
## Systematic Development Roadmap

**Author:** Zain Dana Harper
**Date:** February 27, 2026
**Revision:** 1.0

---

## Executive Summary

After comprehensive analysis of all project files across three directories:
- `SkyrimBridge_v3/` — Current active development
- `SkyrimBridgeOldSources/` — Historical versions and disabled Layer 4 code
- `Package Files for Analysis/` — Original and IMPROVED shader files

This document establishes a systematic plan to bring SkyrimBridge v3.0 to production-ready status and beyond.

---

## Current State Assessment

### What's Working (Layers 1-3)

| Component | Status | Notes |
|-----------|--------|-------|
| SKSE Plugin Loading | ✅ Complete | Loads on SE 1.6.x, AE compatible |
| ENB SDK Integration | ✅ Complete | Detects ENB, pushes parameters |
| 10 Data Trackers | ✅ Complete | ~120 float4 params pushed/frame |
| Debug GUI (ImGui) | ✅ Complete | INSERT key toggle |
| Phase 2: WeatherParameterComputer | ✅ Complete | INI-driven interpolation |
| Phase 3: SharedMemoryBridge | ✅ Complete | External app data access |
| Phase 4: ParmLinkCompat | ✅ Complete | Drop-in enbParmLink replacement |

### What's Improved (Shaders)

| Category | Original | IMPROVED | Change |
|----------|----------|----------|--------|
| SkyrimBridge.fxh | v1.0 (33KB) | v3.0.0 (54KB) | +63% — 9 new helper sections |
| 8 Core FX files | 720KB total | 158KB total | -78% — cleaner, integrated |
| 5 Effect headers | Variable | v3.2.0 | Full SB integration |
| 6 UI headers | Partial SB | All have SHADERGROUP 99 | Complete |

### What's Disabled (Layer 4)

| Component | Location | Issue |
|-----------|----------|-------|
| G-Buffer Capture | `SkyrimBridgeOldSources/SkyrimBridge_v3/src/render/` | API mismatches |
| SSGI | Same location | GPUTimerQueries mismatch |
| ShaderHooks | Same location | PatchVTable not declared |
| ViewportManager | Same location | Missing ViewportBackend.h |

### What's Not Started (Layer 5)

- Runtime Occlusion Plane Injection — Requires RE of BSOcclusionPlane

---

## Phase-by-Phase Plan of Action

### PHASE 1: Immediate Consolidation (Priority P0)

**Goal:** Merge all improvements into a single, buildable, testable package.

#### Task 1.1: Merge IMPROVED Shaders to Main
**Effort:** 30 minutes
**Dependencies:** None

```
Source: Package Files for Analysis/IMPROVED/
Target: SkyrimBridge_v3/shader/

Files to copy:
- SkyrimBridge.fxh (v3.0.0)
- All 8 enb*.fx files
- All 5 Effect_*.fxh files
- All 6 enbUI_*.fxh files
```

**Verification:** Load ENB in-game, confirm no shader compile errors.

#### Task 1.2: Verify DLL Build
**Effort:** 15 minutes
**Dependencies:** None

```batch
cd C:\Users\Zain\SKSE\SkyrimBridge_v3
build.bat
```

**Expected output:** `SkyrimBridge_v3.dll` in `build/Release/`

**Verification:** Copy to Skyrim's SKSE/Plugins folder, check `skse64.log` for load confirmation.

#### Task 1.3: Create Test Checklist
**Effort:** 30 minutes
**Dependencies:** 1.1, 1.2

Create systematic test procedure:
- [ ] DLL loads without crash
- [ ] Debug GUI appears (INSERT key)
- [ ] All 10 tracker sections show data
- [ ] ENB parameters visible in ENB shader editor (Shift+Enter)
- [ ] Weather transitions smoothly interpolate
- [ ] Interior/exterior detection works
- [ ] Combat state detection works
- [ ] Spell school detection works (if casting)

---

### PHASE 2: Utilize Unused Parameters (Priority P0)

**Goal:** The Innovation Roadmap identified 62 of 105 parameters as "completely unused." Activate the most impactful ones.

#### Task 2.1: Combat-Aware DOF
**Effort:** 2-3 hours
**Dependencies:** Phase 1 complete
**Impact:** ★★★★★

Modify `enbdepthoffield.fx` to use:
- `SB_XHair_Info.y` — Target distance for autofocus
- `SB_Player_Combat.x` — Combat state for focus mode switching
- `SB_UI_Menus.y` — Dialogue detection for NPC focus

**Implementation:**
```hlsl
// In PS_Focus() or equivalent
float targetDist = SB_XHair_Info.y;
float inCombat = SB_Player_Combat.x;
float focusDist = lerp(sceneFocusDist, targetDist, inCombat * 0.8);
```

#### Task 2.2: Interior Directional Lighting Fix
**Effort:** 1 hour
**Dependencies:** Phase 1 complete
**Impact:** ★★★★☆

Current: Contact shadows use `SB_Sun_Direction` indoors (wrong).
Fix: Swap to `SB_Interior_DirDir` when `EInteriorFactor > 0.5`.

Affects:
- `enbeffectprepass.fx` — Contact shadow direction
- `Effect_AtmosphericFog.fxh` — Light scatter direction

#### Task 2.3: Point Light Bloom
**Effort:** 2 hours
**Dependencies:** Phase 1 complete
**Impact:** ★★★★☆

Currently unused: `SB_Light0/1/2_PosRad`, `SB_Light0/1/2_Color`

Modify `enbbloom.fx` to:
1. Project light positions to screen space
2. Add localized bloom at those positions
3. Tint bloom by light color

#### Task 2.4: Weather Transition Smoothing
**Effort:** 1 hour
**Dependencies:** Phase 1 complete
**Impact:** ★★★★☆

Currently: Weather profiles snap on weather change.
Fix: Use `SB_Weather_Transition.x` for smooth interpolation.

Affects:
- `enbeffect.fx` — Color grading parameters
- `enbadaptation.fx` — Exposure targets
- `Effect_AtmosphericFog.fxh` — Fog density/color

---

### PHASE 3: Health & Combat Feedback (Priority P1)

**Goal:** Implement gameplay-reactive visual effects that only SkyrimBridge can provide.

#### Task 3.1: Health Vignette System
**Effort:** 2-3 hours
**Dependencies:** Phase 1 complete
**Impact:** ★★★★☆

Parameters to use:
- `SB_Player_Vitals.x` — Health percentage
- `SB_FX_Damage` — Recent damage events

Implementation in `enbeffectpostpass.fx`:
- Blood-red vignette at health < 25%
- Pulsing heartbeat effect at health < 10%
- Directional impact flash on damage

#### Task 3.2: Spell-Reactive Visual Effects
**Effort:** 3-4 hours
**Dependencies:** Phase 1 complete
**Impact:** ★★★★☆

Parameters to use:
- `SB_Equip_Right.z` / `SB_Equip_Left.z` — Spell school
- `SB_FX_Vision` — Night eye, detect life, etc.

Create new helper in `SkyrimBridge.fxh`:
```hlsl
int SB_GetActiveSpellSchool()
{
    int right = (int)SB_Equip_Right.z;
    int left = (int)SB_Equip_Left.z;
    return max(right, left);  // Return dominant spell school
}

float3 SB_GetSpellSchoolColor(int school)
{
    // 1=Alteration, 2=Conjuration, 3=Destruction, 4=Illusion, 5=Restoration
    if (school == 1) return float3(0.8, 0.9, 0.5);  // Yellow-green
    if (school == 2) return float3(0.6, 0.3, 0.9);  // Purple
    if (school == 3) return float3(1.0, 0.4, 0.1);  // Orange-red
    if (school == 4) return float3(0.3, 0.7, 0.9);  // Cyan
    if (school == 5) return float3(1.0, 0.9, 0.4);  // Gold
    return float3(1.0, 1.0, 1.0);
}
```

Apply subtle color filtering in `enbeffectpostpass.fx` based on active spell.

---

### PHASE 4: Moonlight Rendering (Priority P1)

**Goal:** Implement dual-moon god rays and moon-aware lighting — unique to Elder Scrolls.

#### Task 4.1: Moon God Rays
**Effort:** 3-4 hours
**Dependencies:** Phase 1 complete
**Impact:** ★★★★★ (Unique feature)

Currently unused: `SB_Masser_NDC`, `SB_Masser_Direction`, `SB_Secunda_NDC`, `SB_Secunda_Direction`

Implement in `enbsunsprite.fx` or new `Effect_MoonGodRays.fxh`:
- Ray march toward moon screen position at night
- Scale intensity by moon phase (`SB_Masser_NDC.w`)
- Apply cooler color temperature than sun rays
- Support both moons simultaneously during appropriate phases

#### Task 4.2: Moon-Phase Night Lighting
**Effort:** 1-2 hours
**Dependencies:** Phase 1 complete
**Impact:** ★★★★☆

Modulate night scene brightness based on `SB_Masser_NDC.w`:
- Full moon: +15% ambient, visible moon shadows
- New moon: -20% ambient, increased star visibility

Affects:
- `enbadaptation.fx` — Night exposure targets
- `Effect_AtmosphericFog.fxh` — Night fog density

---

### PHASE 5: Layer 4 Reconciliation (Priority P2)

**Goal:** Re-enable the disabled render pipeline code.

#### Task 5.1: Audit Layer 4 Code
**Effort:** 1-2 hours
**Dependencies:** None

Review files in `SkyrimBridgeOldSources/SkyrimBridge_v3/src/render/`:
- `RenderPipeline.h/cpp`
- `GBufferPass.h/cpp`
- `SSGI.h/cpp`

Document all API mismatches and missing declarations.

#### Task 5.2: Create ViewportBackend.h Stub
**Effort:** 30 minutes
**Dependencies:** 5.1

The ViewportManager code references a missing `ViewportBackend.h`. Either:
- Create stub implementation
- Remove viewport dependency from build

#### Task 5.3: Reconcile ShaderHooks
**Effort:** 2-3 hours
**Dependencies:** 5.1

Fix `PatchVTable` and `fnOMSetRTs` declarations.
Align D3D11 hook infrastructure with current codebase.

#### Task 5.4: Test G-Buffer Capture
**Effort:** 2-3 hours
**Dependencies:** 5.2, 5.3

Once APIs aligned:
1. Re-enable render pipeline in CMakeLists.txt
2. Build and test G-Buffer capture
3. Verify normal/depth/material ID readback

#### Task 5.5: Test SSGI Integration
**Effort:** 2-3 hours
**Dependencies:** 5.4

With G-Buffer working:
1. Enable SSGI pass
2. Verify global illumination output
3. Integrate with ENB composite pass

---

### PHASE 6: Performance Optimization (Priority P2)

**Goal:** Address known performance issues from AUDIT_v3.3.

#### Task 6.1: Fix DOF Sample Count
**Effort:** 1 hour
**Dependencies:** Phase 1 complete

Current: Up to 90 samples/pixel at full resolution.
Fix: Cap at 36 samples, run at half-resolution for large CoC.

#### Task 6.2: Fix Anamorphic Sample Count
**Effort:** 30 minutes
**Dependencies:** Phase 1 complete

Current: 101-sample loop (unbounded).
Fix: Hard cap at 16 samples regardless of width setting.

#### Task 6.3: Reduce SSS Tap Count
**Effort:** 30 minutes
**Dependencies:** Phase 1 complete

Current: 25 taps × 2 passes.
Fix: Reduce to 17 taps (-8 to +8). Still excellent quality.

#### Task 6.4: Fix Volumetric Compute Shadow Sampling
**Effort:** 3-4 hours
**Dependencies:** Phase 1 complete

Current: Uses camera ViewProj for shadow map sampling (wrong).
Fix: Replace with screen-space radial inscatter (march from pixel toward sun position, test depth buffer).

#### Task 6.5: Fix Wet Surface World Position
**Effort:** 1 hour
**Dependencies:** Phase 1 complete

Current: Puddles use screen UV (swim on camera rotation).
Fix: Reconstruct world position via InvViewProj, use world XZ for noise.

---

### PHASE 7: Distribution Preparation (Priority P2)

**Goal:** Create release-ready package for community testing.

#### Task 7.1: Create FOMOD Installer
**Effort:** 2-3 hours
**Dependencies:** Phases 1-3 complete

Structure:
```
SkyrimBridge_v3_Installer/
├── fomod/
│   ├── info.xml
│   └── ModuleConfig.xml
├── 00_Core/
│   └── SKSE/Plugins/SkyrimBridge_v3.dll
├── 01_ENB_Integration/
│   └── Data/enbseries/ (all shader files)
└── 02_Config/
    └── Data/enbseries/WeatherParams.ini
```

#### Task 7.2: Write End-User Documentation
**Effort:** 2-3 hours
**Dependencies:** Phases 1-3 complete

Contents:
- Installation guide (manual + MO2 + Vortex)
- Compatibility notes (ENB versions, other mods)
- Feature list with screenshots
- Troubleshooting FAQ

#### Task 7.3: Create Compatibility Patches
**Effort:** 4-6 hours
**Dependencies:** 7.1, 7.2

Patches for popular ENB presets:
- Rudy ENB
- NAT.ENB
- Pi-CHO ENB
- Serio's ENB

Each patch modifies preset shaders to include SkyrimBridge.fxh and use SB_ parameters.

---

### PHASE 8: Advanced Features (Priority P3)

**Goal:** Implement innovation roadmap features.

#### Task 8.1: PerfGov Integration
**Effort:** 8-10 hours
**Dependencies:** Phases 1-6 complete

Components:
1. FrameBudgetMonitor — Track GPU frame time
2. ContextClassifier — Detect scene complexity
3. SettingGovernor — Adjust INI settings dynamically

Push to shaders via new SB_Perf_* parameters.

#### Task 8.2: Biome/Region Detection
**Effort:** 4-6 hours
**Dependencies:** Phase 1 complete

Create lookup table for Skyrim's climate regions (~500 bytes).
Push SB_Region_Type and SB_Region_Climate parameters.

#### Task 8.3: Audio State Tracking
**Effort:** 4-6 hours
**Dependencies:** Phase 1 complete

Hook BSAudioManager to detect:
- Combat music playing
- Exploration music
- Dungeon ambient
- Town ambient

Push SB_Audio_Music and SB_Audio_Ambient parameters.

---

## Priority Summary

| Priority | Phase | Tasks | Effort | Impact |
|----------|-------|-------|--------|--------|
| **P0** | Phase 1 | Consolidation | 1.5 hours | Foundation |
| **P0** | Phase 2 | Unused Parameters | 6-8 hours | ★★★★★ |
| **P1** | Phase 3 | Health/Combat FX | 5-7 hours | ★★★★☆ |
| **P1** | Phase 4 | Moonlight | 4-6 hours | ★★★★★ |
| **P2** | Phase 5 | Layer 4 | 8-12 hours | ★★★☆☆ |
| **P2** | Phase 6 | Performance | 6-8 hours | ★★★★☆ |
| **P2** | Phase 7 | Distribution | 8-12 hours | ★★★★★ |
| **P3** | Phase 8 | Advanced | 16-22 hours | ★★★☆☆ |

---

## Recommended Execution Order

### Session 1: Foundation (2-3 hours)
1. Task 1.1: Merge IMPROVED shaders
2. Task 1.2: Verify DLL build
3. Task 1.3: Test checklist
4. Task 2.4: Weather transition smoothing (quick win)

### Session 2: High-Impact Integrations (4-5 hours)
1. Task 2.1: Combat-aware DOF
2. Task 2.2: Interior directional lighting fix
3. Task 2.3: Point light bloom

### Session 3: Gameplay Feedback (5-6 hours)
1. Task 3.1: Health vignette system
2. Task 3.2: Spell-reactive effects

### Session 4: Moonlight (4-5 hours)
1. Task 4.1: Moon god rays
2. Task 4.2: Moon-phase night lighting

### Session 5: Performance & Polish (4-5 hours)
1. Tasks 6.1-6.5: All performance fixes
2. Testing and verification

### Session 6: Distribution (4-5 hours)
1. Task 7.1: FOMOD installer
2. Task 7.2: Documentation
3. Initial preset patches

---

## Success Metrics

### Functional Metrics
- [ ] DLL loads without crash on SE 1.6.x and AE
- [ ] All 120 parameters pushed to ENB every frame
- [ ] Debug GUI shows accurate real-time data
- [ ] Weather transitions are visually smooth
- [ ] Combat DOF focuses on target
- [ ] Health vignette appears at low health
- [ ] Moon god rays visible on clear nights
- [ ] No shader compile errors

### Performance Metrics
- [ ] < 2ms overhead at 1440p with all effects
- [ ] DOF < 4ms at half-res
- [ ] Volumetric fog < 2ms
- [ ] No frame drops during weather transitions

### Quality Metrics
- [ ] Zero crashes in 2-hour play session
- [ ] Clean ENB GUI parameter display
- [ ] User documentation complete
- [ ] FOMOD installer tested with MO2

---

## Risk Mitigation

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| ENB API changes | Medium | High | Version-check all SDK calls |
| Shader compile errors | Low | Medium | Test all shaders before merge |
| Performance regression | Medium | Medium | Profile before/after each phase |
| Layer 4 API mismatch unresolvable | Medium | Low | Keep Layer 4 optional |
| Community Shaders conflict | Low | Low | Document incompatibility |

---

## Conclusion

SkyrimBridge v3.0 is in a strong position with Layers 1-3 complete and a comprehensive set of IMPROVED shaders ready for integration. The immediate priorities are:

1. **Merge and verify** — Get everything into a single buildable package
2. **Activate unused parameters** — 62 of 105 parameters are pushed but never consumed
3. **Implement unique features** — Moon god rays, combat DOF, health feedback

The project represents a unique value proposition: **the only tool that gives ENB shaders full game-state awareness**. Community Shaders cannot run with ENB. enbParmLink requires fragile memory addresses. ENB Extender provides only basic camera/atmosphere data. SkyrimBridge provides everything and works alongside ENB's mature post-processing pipeline.

**Next immediate action:** Begin Session 1 — merge IMPROVED shaders and verify build.

---

*Document generated from analysis of 260+ source files across 3 directories.*
