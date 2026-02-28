# SkyrimBridge v3.0+ Innovation Roadmap
## Beyond Consolidation: What Only SkyrimBridge Can Do

**Author:** Zain Dana Harper  
**Date:** February 2026  
**Context:** SkyrimBridge v3.0 has consolidated ENB Extender, KiLoader, and enbParmLink into a single system with 105 float4 parameters across 19 domains. This document identifies innovation opportunities that leverage SkyrimBridge's unique position in the modding ecosystem.

---

## 1. The Strategic Position

SkyrimBridge occupies a position no other tool holds: **simultaneous deep game-state awareness AND renderer-level access.**

| Tool | Game State | Renderer Access | Post-Processing |
|---|---|---|---|
| ENBSeries | None (blind) | None | Full pipeline |
| Community Shaders | Minimal (some via hooks) | Full (shader replacement) | Via shader injection |
| ENB Extender | Camera + atmosphere only | None | Via ENB params |
| enbParmLink | Fragile memory reads | None | Via ENB params |
| **SkyrimBridge** | **105 float4 (19 domains)** | **D3D11 vtable hooks** | **Via ENB params** |

Community Shaders cannot run alongside ENB. SkyrimBridge works *with* ENB, enhancing it rather than replacing it. This is both a constraint (pixel shaders only, no compute) and an advantage (access to ENB's mature post-processing pipeline while providing data it can't get on its own).

The innovation thesis: **Every feature below requires knowledge that only SkyrimBridge provides to the shader pipeline.** These are not incremental improvements — they are categorically impossible without the game-state bridge.

---

## 2. The Unused Data Problem

### 2.1 Parameter Utilization Audit

Of 105 declared float4 parameters, current shader utilization is:

| Category | Status | Parameters |
|---|---|---|
| **Actively consumed by shaders** | ✓ Used | ~25 (SB_Sun_*, SB_Lightning, SB_Precipitation, SB_Fog_*, SB_IS_*, SB_Camera_Info, SB_Light_Summary, SB_Weather_Flags, SB_Player_Combat, SB_Player_Position, SB_Render_Frame, SB_Atmos_Sunlight) |
| **Used only in helper functions** | ○ Internal | ~18 (matrices, SB_Time, SB_Wind, SB_UI_Menus, SB_XHair_*, SB_Equip_*, SB_AV_*) |
| **Completely unused** | ✗ Dead | **~62** (moons, atmosphere colors, interior lighting, shadow data, damage FX, TAA jitter, all 3 nearby lights, actor values, quest state, volumetric scattering, etc.) |

**62 out of 105 parameters are declared, pushed every frame, and never read by any shader.** This represents both wasted bandwidth and massive untapped potential.

### 2.2 Highest-Impact Unused Data

| Parameter Group | Innovation Potential | Difficulty |
|---|---|---|
| SB_Masser_NDC/Dir, SB_Secunda_NDC/Dir | Moon-source god rays, dual moonlight | Medium |
| SB_Light0/1/2_PosRad, SB_Light0/1/2_Color | Local light bloom, light-aware fog | Low |
| SB_XHair_Info, SB_XHair_Pos, SB_XHair_Actor | Combat autofocus DOF, target highlighting | Medium |
| SB_Interior_DirColor/DirDir/Ambient/FogColor | Interior-correct post-processing | Low |
| SB_Equip_Right/Left/Armor/Flags | Spell-reactive FX, helmet overlay | Medium |
| SB_Player_Vitals, SB_FX_Damage | Health/stamina screen effects | Low |
| SB_Render_Jitter | Proper TAA integration | Low |
| SB_Weather_Transition | Smooth weather morph blending | Low |
| SB_Precip_Surface (wetness/snow cover) | Wet world darkening, snow accumulation | Medium |
| SB_Vol_Scatter, SB_Vol_Color | Volumetric parameter-driven scattering | Medium |
| SB_IS_DOF (engine DOF params) | Intelligent DOF that respects game intent | Low |
| SB_Quest_Progress, SB_Quest_Tracked | Quest-stage visual theming | High |

---

## 3. Innovation Categories

### 3.1 GAMEPLAY-REACTIVE RENDERING
*Things nobody can do because they need game-state + shader coordination*

#### A. Combat-Aware Depth of Field

**The problem:** enbdepthoffield.fx uses **zero** SB_ parameters. It has sophisticated autofocus (screen-center sampling with FPS weapon exclusion) but is completely blind to game context. It doesn't know if you're in combat, who you're targeting, or what you're doing.

**The innovation:**
```
SB_XHair_Info.x  = hasTarget (0/1)
SB_XHair_Info.y  = targetDistance (world units)
SB_XHair_Info.z  = targetType (0=none, 1=actor, 2=object, 3=static)
SB_XHair_Pos.xyz = target world position
SB_Player_Combat.x = inCombat (0/1)
SB_Player_Combat.y = isAttacking (0/1)
SB_Player_Combat.z = weaponDrawn (0/1)
```

| Mode | Behavior | Data Source |
|---|---|---|
| **Exploration** | Wide DOF, gentle background blur | Default (no combat, no target) |
| **Combat lock** | Autofocus snaps to crosshair target | SB_XHair_Pos → project to screen depth |
| **Power attack** | Radial blur burst from player | SB_Player_Combat.y transition |
| **Ranged aim** | Tighten focus on distant target, defocus near field | SB_XHair_Info.y > threshold |
| **Dialogue** | Pull focus to NPC face | SB_UI_Menus.y (dialogue flag) + SB_XHair_Pos |
| **Menu/Loading** | Disable DOF entirely | SB_UI_Menus.x / SB_UI_HUD.w |

**No existing DOF shader for Skyrim does context-aware autofocus.** Marty McFly's ADOF, Kitsuune's DOF, and every other implementation samples the depth buffer at screen center. They can't know about combat state, target identity, or dialogue.

**Implementation path:** Modify PS_Focus() in enbdepthoffield.fx to blend between scenefocus (existing screen sampling) and SB_XHair_Info.y (target-locked depth) weighted by SB_Player_Combat.x.

#### B. Spell-Reactive Visual Effects

**The data:** SB_Equip_Right and SB_Equip_Left encode equipped spell school:
```
SB_Equip_Right.z = spellSchool  (1=Alteration, 2=Conjuration, 3=Destruction, 
                                  4=Illusion, 5=Restoration)
SB_FX_Vision.x   = nightEyeActive
SB_FX_Vision.y   = detectLifeActive  
SB_FX_Vision.z   = auraWhisperActive
```

| Spell School | Visual Effect | Implementation |
|---|---|---|
| Destruction (Fire) | Heat shimmer distortion near hands, warm color shift | UV distortion in postpass, applied at screen bottom |
| Destruction (Frost) | Ice crystal overlay, cold color shift, breath fog | Overlay texture + desaturation in lens/postpass |
| Destruction (Shock) | Periodic lightning flicker, electric arc artifacts | Chromatic aberration spike + brightness pulse |
| Illusion | Subtle reality distortion, dreamlike haze | Slight barrel distortion + fog density increase |
| Conjuration | Soul-world color bleed (purple tint), portal glow | Tint overlay + bloom boost in dark purple |
| Restoration | Golden warmth, lens flare from healing hands | Warm tint + gentle bloom in gold/white |

**Nobody does this.** Community Shaders can't access equipment state. ENB doesn't know what's equipped. Only SkyrimBridge has SB_Equip_Right/Left.

#### C. Health/Vitals Screen Effects

**The data:** SB_Player_Vitals = (health%, stamina%, magicka%, level)

| Condition | Effect | Threshold |
|---|---|---|
| Low health (<25%) | Blood vignette, desaturation toward monochrome, heartbeat pulse | SB_Player_Vitals.x < 0.25 |
| Critical health (<10%) | Heavy tunnel vision, pulsing red overlay, blur | SB_Player_Vitals.x < 0.10 |
| Low stamina (<20%) | Heavy breathing blur (rhythmic), slight wobble | SB_Player_Vitals.y < 0.20 |
| Magicka depletion | Desaturation (magic fades from the world) | SB_Player_Vitals.z < 0.15 |
| SB_FX_Damage.x active | Impact flash (directional, from hit direction) | On damage event |

These are gameplay-enhancing effects that increase immersion. The engine's built-in low-health effects (red screen flash) are crude. A proper physically-motivated health vignette with depth-aware blood splatter would be a dramatic improvement.

---

### 3.2 MOONLIGHT RENDERING
*Completely novel — no Skyrim mod has ever done this*

SkyrimBridge tracks both moons (Masser and Secunda) with full NDC position, world direction, and phase data. **Every single moon parameter is currently unused.**

#### A. Moon God Rays

The existing god ray implementations (prepass, underwater, sunsprite) all use the sun position. At night, there's no volumetric light source. Moon god rays would:

- Use SB_Masser_NDC.xy / SB_Secunda_NDC.xy as ray origin points
- Scale intensity by moon phase (SB_Masser_NDC.w) and elevation
- Apply Beer-Lambert absorption tinting (moonlight is reflected sunlight, cooler color temperature)
- Only activate when ENightDayFactor < 0.3 and moon is above horizon

**Dual-moon volumetrics are unique to the Elder Scrolls universe.** No other game has two moons, and no Skyrim mod renders moonlight volumetrically.

#### B. Moon-Phase Night Lighting

Currently, night scenes in ENB presets use a single "night multiplier" for ambient/fog/bloom. But Skyrim's moons have phases:

```
SB_Masser_NDC.w = moonPhase (0.0 = new, 0.5 = half, 1.0 = full)
```

A full moon should produce noticeably brighter ambient lighting than a new moon. This could modulate:
- Fog scatter intensity
- Shadow visibility (moonlight shadow only at >75% phase)
- Ambient color temperature (full moon = slight blue shift)
- Star visibility (bright moon washes out stars)

#### C. Moonlit Atmospheric Scattering

The existing atmospheric fog shader (Effect_AtmosphericFog.fxh) scatters light from the sun direction. At night, it should scatter from the moon(s) instead:

```hlsl
float3 scatterDir = (ENightDayFactor > 0.5) 
    ? SB_Sun_Direction.xyz 
    : normalize(SB_Masser_Direction.xyz * SB_Masser_NDC.w 
              + SB_Secunda_Direction.xyz * SB_Secunda_NDC.w);
```

This gives physically-motivated night fog that shifts color and direction based on which moon is dominant.

---

### 3.3 LOCAL LIGHT AWARENESS
*3 nearest lights tracked per frame, zero consumption*

SB_Light0/1/2_PosRad (xyz=worldPos, w=radius) and SB_Light0/1/2_Color (rgb=color, a=intensity) are pushed every frame but never used. These represent the 3 brightest point lights near the camera.

#### A. Point Light Bloom

Current bloom (enbbloom.fx) is screen-space only — it blooms whatever is bright. It doesn't know *why* something is bright. With nearby light data:

- Project SB_Light0_PosRad.xyz to screen space
- Add focused bloom kernel at that screen position
- Color the bloom by SB_Light0_Color.rgb
- Scale by SB_Light0_Color.a / SB_Light0_PosRad.w (intensity / distance)

This creates light-source-aware bloom: a campfire produces warm orange bloom centered on the fire, not just on whatever pixels happen to be bright. A blue magic light produces blue-tinted bloom.

#### B. Light-Aware Volumetric Fog

Current fog shaders are lit only by the sun. In interiors or at night near torches, fog should be lit by nearby point lights:

```hlsl
for (int i = 0; i < 3; i++) {
    float4 lightPosRad = SB_NearbyLight(i);  // helper
    float4 lightColor  = SB_NearbyLightColor(i);
    float3 toLight = lightPosRad.xyz - worldPos;
    float  atten   = 1.0 - saturate(length(toLight) / lightPosRad.w);
    fogLighting += lightColor.rgb * lightColor.a * atten * atten;
}
```

This makes torch-lit fog in dungeons actually glow with warm light instead of being uniformly grey.

#### C. Lens Flare from Point Lights

enblens.fx currently generates lens flares from brightness peaks in the scene. With known light positions, it could generate physically-correct lens ghosts from specific light sources, with the flare pattern following the light-to-center axis rather than the arbitrary brightest-pixel axis.

---

### 3.4 INTERIOR LIGHTING INTELLIGENCE
*SkyrimBridge knows interior light direction and color — nothing uses it*

When indoors, SB_Interior_DirColor, SB_Interior_DirDir, SB_Interior_Ambient, SB_Interior_FogColor, and SB_Interior_FogDist are all populated. Currently, ENB shaders treat interiors as "not exterior" and apply generic profiles.

#### A. Directional Post-Process Lighting

In interiors, the dominant light direction (SB_Interior_DirDir) could drive:
- Contact shadow direction (currently uses sun direction, which is wrong indoors)
- God ray direction for window light shafts
- SSAO bent normal direction for more accurate AO
- Specular highlight direction for PBR materials

This is trivially implementable — just swap SB_Sun_Direction for SB_Interior_DirDir when SB_Interior_Flags.x > 0.5. The impact on visual correctness is significant.

#### B. Interior Fog Color Matching

Current fog in interiors is often the wrong color because ENB's fog parameters come from weather records (designed for exteriors). SB_Interior_FogColor gives the actual interior fog color the engine uses. Atmospheric fog, volumetric fog, and bloom could all reference this for correct interior rendering.

---

### 3.5 WEATHER-AWARE TRANSITIONS
*Smooth morphing instead of hard profile switching*

#### A. Continuous Weather Blending

SB_Weather_Transition provides exact transition progress between weather types:
```
SB_Weather_Transition.x = transitionProgress [0..1]
SB_Weather_Transition.y = currentWeatherID
SB_Weather_Transition.z = previousWeatherID  
SB_Weather_Transition.w = transitionDuration
```

Currently, ENB presets use the built-in `Weather.x/y` to detect weather and apply preset-specific settings. But Weather.x is the *current* weather classification (0-6), not a smooth blend. SB_Weather_Transition.x gives the exact interpolation alpha.

This enables: Instead of snapping from "clear day profile" to "rainy profile" when rain starts, smoothly morph every visual parameter over the transition duration. Fog density, color grading, bloom intensity, shadow softness — all interpolate smoothly.

#### B. Surface State Responsiveness

SB_Precip_Surface provides:
```
SB_Precip_Surface.x = surfaceWetness [0..1]
SB_Precip_Surface.y = snowCoverage  [0..1]
SB_Precip_Surface.z = puddleLevel   [0..1]  
```

These could drive:
- **Wet darkening**: All materials darken by surfaceWetness (modify scene brightness in enbeffect.fx)
- **Specular boost**: Wet surfaces are shinier (increase specular in PBR pass)
- **Snow tint**: Snow coverage shifts scene toward blue-white in exposed areas
- **Puddle reflections**: Increase SSR intensity on horizontal surfaces when puddleLevel > 0

---

### 3.6 POSTPASS AWARENESS
*enbeffectpostpass.fx has zero SB_ usage — massive opportunity*

The postpass handles color grading, film grain, vignette, sharpening, and final anti-aliasing. All of these could be context-aware:

| Effect | Current | With SkyrimBridge |
|---|---|---|
| Color grading | Static per ENB weather profile | Continuous interpolation via SB_Weather_Transition, interior color correction via SB_Interior_Ambient |
| Film grain | Fixed intensity | Scale by darkness (more grain at night, less in bright sun), increase in fog/rain |
| Vignette | Fixed shape | Widen during combat (tunnel vision at low health), remove in menus |
| Sharpening | Fixed strength | Reduce during fog/rain (soft atmospheric look), increase in clear weather |
| Chromatic aberration | Fixed | Spike during spell casting, damage impact, lightning strikes |

---

### 3.7 PERFORMANCE TELEMETRY (PerfGov Integration)
*Shaders that automatically degrade gracefully*

When SB_PerfGov is active, it pushes SB_Perf_FrameTime, SB_Perf_DrawCalls, SB_Perf_SceneState, and SB_Perf_Budget. Every expensive shader could use SB_AdaptiveQuality():

| Shader | Expensive Operation | Adaptive Scaling |
|---|---|---|
| SB_GTAO | Sample count (8→32) | SB_AdaptiveQuality maps headroom to sample count |
| SB_ContactShadows | Ray step count (16→64) | Reduce steps when over budget |
| SB_SSR | Ray march distance + steps | Shorter rays when scene is complex |
| SB_VolumetricFog | Volumetric samples per ray | Fewer samples in dense cities |
| SB_MotionBlur | Sample count per pixel | Reduce in combat (when it matters least for IQ) |
| enbbloom.fx | Downsample iterations | Fewer bloom passes when over budget |

**No ENB preset has ever dynamically adjusted its own quality based on real performance data.** Users currently choose a "quality tier" at install time. SB_AdaptiveQuality() makes every shader self-tuning.

---

## 4. New Data Domains (DLL-Side Expansion)

Beyond using existing unused parameters, the DLL could track new data:

### 4.1 Biome/Region Classification (New Domain 20)

```hlsl
float4 SB_Region_Type;     // .x = biomeEnum, .y = altitude, .z = isCoastal, .w = cellDensity
float4 SB_Region_Climate;  // .x = temperature, .y = humidity, .z = windExposure, .w = forestDensity
```

Biome detection via worldspace + grid position lookup against a table of Skyrim's climate regions. This enables:
- Tundra: desaturated, wide-open, cold color grading
- Pine forest: green-shifted, dappled light patterns
- Volcanic (Solstheim): red-orange haze, heat shimmer
- Icy mountains: blue shift, high contrast, enhanced snow effects

**Implementation:** Precomputed lookup table indexed by worldspace + cell grid coordinates. ~500 bytes of data for complete Skyrim coverage.

### 4.2 Audio State Tracking (New Domain 21)

```hlsl
float4 SB_Audio_Music;     // .x = combatMusic, .y = explorationMusic, .z = dungeonMusic, .w = townMusic
float4 SB_Audio_Ambient;   // .x = isSilent, .y = indoorReverb, .z = windVolume, .w = waterNearby
```

Music state is a reliable combat/exploration classifier that's separate from the player's actual combat state flag. Combat music starts before the player weapon-draws and persists through pursuit. This gives earlier and more accurate combat detection.

**Implementation:** Hook `BSAudioManager` or read from `BGSSoundDescriptor` playing states.

### 4.3 NPC/Actor Density (New Domain 22)

```hlsl
float4 SB_Scene_Actors;    // .x = loadedActorCount, .y = visibleActorCount, .z = hostileCount, .w = followerCount
float4 SB_Scene_Objects;   // .x = loadedRefCount, .y = dynamicObjCount, .z = lightCount, .w = particleEmitterCount
```

Scene complexity estimation for performance governance and visual adaptation. Dense crowds → reduce per-pixel work. Empty wilderness → maximize quality.

**Implementation:** Iterate `ProcessLists::GetSingleton()` actor arrays each frame.

### 4.4 Dragon State (New Domain 23)

```hlsl
float4 SB_Dragon_State;    // .x = dragonPresent, .y = isFlying, .z = isBreathing, .w = healthPct
float4 SB_Dragon_Pos;      // .xyz = dragonWorldPos, .w = distToPlayer
```

Dragon encounters are the most visually dramatic events in Skyrim and the most performance-demanding. Knowing a dragon is present enables:
- Enhanced particle bloom for breath attacks
- Camera shake via UV distortion
- Dramatic color grading shift
- PerfGov profile switch to "dragon encounter" (reduce LOD, preserve particle budget)

**Implementation:** Scan loaded actors for `RE::ActorTypeInfo::Dragon` via `ProcessLists`.

---

## 5. Implementation Priority Matrix

| Innovation | Impact | Effort | Dependencies | Priority |
|---|---|---|---|---|
| Combat-aware DOF | ★★★★★ | Medium | Modify enbdepthoffield.fx | **P0** |
| Interior directional lighting | ★★★★☆ | Low | Modify prepass + effect | **P0** |
| Point light bloom | ★★★★☆ | Low | Modify enbbloom.fx | **P0** |
| Weather transition blending | ★★★★☆ | Low | Modify enbeffect.fx, adaptation | **P0** |
| Health/vitals screen FX | ★★★★☆ | Low | New postpass technique or lens | **P1** |
| Spell-reactive FX | ★★★★☆ | Medium | New effect addon | **P1** |
| Moon god rays | ★★★★★ | Medium | Modify prepass + fog shader | **P1** |
| Light-aware volumetric fog | ★★★☆☆ | Medium | Modify SB_VolumetricFog | **P1** |
| Surface wetness/snow | ★★★☆☆ | Medium | Modify PBR + effect | **P2** |
| Context-aware postpass | ★★★☆☆ | Low | Modify postpass | **P2** |
| PerfGov adaptive quality | ★★★★★ | High | Requires SB_PerfGov.dll | **P2** |
| Biome/region detection | ★★★☆☆ | Medium | New DLL domain + LUT | **P2** |
| Dragon state tracking | ★★☆☆☆ | Low | New DLL domain | **P3** |
| Audio state tracking | ★★☆☆☆ | Medium | New DLL hooks | **P3** |
| Runtime occlusion injection | ★★★★★ | Very High | RE of BSOcclusionPlane | **P3** |

---

## 6. Competitive Differentiation Summary

### What Community Shaders Has That We Don't
- Direct shader replacement (we do post-processing only)
- Grass collision / grass lighting (geometry-level modification)
- Terrain blending (mesh-level)
- PBR material system (native shader overhaul)
- DLSS/FSR/XeSS upscaling + frame generation
- Cloud shadows (shadow map injection)

### What We Have That Community Shaders Can Never Have
- **Works with ENB** (CS crashes with ENB present)
- **Full game-state awareness** (105 float4 params)
- **Context-aware everything** (combat, dialogue, weather, equipment, health)
- **Moon tracking** (unique to SkyrimBridge)
- **3 nearest light tracking** (point light awareness)
- **Actor value access** (160+ player stats)
- **Crosshair target tracking** (who/what you're looking at)
- **Equipment awareness** (spell school, weapon type, armor)
- **Quest state** (story progression-driven visuals)
- **Performance telemetry** (PerfGov self-tuning shaders)

### The Positioning Statement

> SkyrimBridge makes ENB presets *alive*. Instead of static visual profiles that look the same whether you're fighting a dragon or reading a book, SkyrimBridge enables effects that respond to what's actually happening in the game. Moon god rays at night, combat-focused depth of field, spell-colored screen effects, health-reactive vignettes, and self-tuning performance — all powered by 105 real-time game-state parameters that no other tool provides.

---

## 7. Next Steps

1. **P0 implementations** — Combat DOF, interior directional fix, point light bloom, weather blending
2. **Write showcase shader** — Single "SB_GameplayFX.fxh" addon that demonstrates health/spell/combat effects
3. **Moon rendering prototype** — Moon god rays in prepass, moonlit fog in atmospheric shader
4. **PerfGov Tier 1** — FrameBudgetMonitor + ContextClassifier + SettingGovernor
5. **Documentation** — Shader author guide showing how to use each SB_ parameter group
6. **Community release** — Package as "SkyrimBridge Showcase Preset" to demonstrate capabilities

---

*Total estimated scope: P0 = 2-3 sessions, P1 = 3-4 sessions, P2 = 4-5 sessions, P3 = ongoing research*
