# Phase 1 — Architecture Analysis: Pilgrim / ENB Extender / ParmLink

## Executive Summary

Three third-party tools currently provide "real-time weather-per-parameter" ENB adjustments:
**enbParmLink.dll**, **ENBExtender** (CameraData/AtmosphereData/EnhancedLightingData/Lenses),
and **KiLoader** (plugin loader). This analysis reverse-engineers their architecture and
maps every capability to Playground equivalents — establishing that Playground can
fully replace all three with a single, more powerful solution.

---

## 1. Architecture Overview

### How Pilgrim ENB Achieves Weather-Reactive Parameters

Pilgrim uses a 3-layer data pipeline:

```
┌─────────────────────────────────────────────────────────────┐
│ LAYER 1: Plugin Loading (KiLoader)                          │
│   KiLoader.kfg → loads .dll plugins at game boot            │
│   • enbParmLink.dllplugin                                   │
│   • enbExtender.dllplugin                                   │
│   Both push data via ENBSetShaderParameter()                │
├─────────────────────────────────────────────────────────────┤
│ LAYER 2: Data Extraction (ENB Extender)                     │
│   enbExtender.dll reads:                                    │
│   • RE::PlayerCamera → FOV, world position, near/far clip   │
│   • RE::Sky → weather colors, sun direction, fog params     │
│   • RE::BSShaderManager → lighting data                     │
│   Pushes to shaders as float4 extern params via headers:    │
│   • CameraData.fxh     (SB equivalent: SB_Camera_*)         │
│   • AtmosphereData.fxh  (SB equivalent: SB_Atmos_*)         │
│   • EnhancedLightingData.fxh (SB: SB_Shadow_*, SB_Interior_*) │
│   • Lenses.fxh          (SB: SB_IS_*, SB_FX_*)              │
├─────────────────────────────────────────────────────────────┤
│ LAYER 3: Expression Evaluation (enbParmLink)                │
│   enbParmLink.cfg contains ExprTk expressions:              │
│   • Reads ENB parameters: enb.fNightDayFactor, etc.         │
│   • Reads process memory: addr.getAbsFloat(address)         │
│   • Evaluates math: lerp, smoothstep, clamp, conditionals   │
│   • Writes back: enb.setFloat("shader.fx", "Group", "Name") │
│                                                              │
│   This enables per-parameter, per-weather overrides:        │
│     bloomIntensity := lerp(dayBloom, nightBloom, nightFactor)│
│     fogDensity := lerp(clearFog, stormFog, precipIntensity) │
└─────────────────────────────────────────────────────────────┘
```

### What Each Tool Provides

#### ENB Extender (CameraData.fxh)
```hlsl
float4 CameraData0;  // .x = worldPosX, .y = worldPosY, .z = worldPosZ, .w = FOV
float4 CameraData1;  // .x = nearClip, .y = farClip, .z = pitch, .w = yaw
float4 CameraData2;  // .x = roll, .y = isFirstPerson, .z = isThirdPerson
```
→ **Playground equivalent**: `SB_Camera_Info`, `SB_Camera_Angles`, `SB_Camera_WorldPos`
  (SB provides MORE: full View/Proj/VP/InvVP/PrevVP matrices — ENB Extender does not)

#### ENB Extender (AtmosphereData.fxh)
```hlsl
float4 AtmosData0;  // .rgb = sun direction, .a = sunGlare
float4 AtmosData1;  // .rgb = sun color
float4 AtmosData2;  // .rgb = ambient color
float4 AtmosData3;  // .rgb = directional ambient
float4 AtmosData4;  // .x = fogNearDist, .y = fogFarDist, .z = fogNearColor.r...
float4 AtmosData5;  // fog far color
```
→ **Playground equivalent**: `SB_Sun_Direction`, `SB_Sun_Color`, `SB_Atmos_Ambient`,
  `SB_Atmos_Sunlight`, `SB_Fog_NearColor`, `SB_Fog_FarColor`, `SB_Fog_Density`
  (SB provides MORE: 8 atmosphere channels, fog height data, weather transition %)

#### ENB Extender (EnhancedLightingData.fxh)
```hlsl
float4 EnhLightData0;  // .rgb = shadow caster direction
float4 EnhLightData1;  // .rgb = shadow caster diffuse
float4 EnhLightData2;  // .rgb = shadow caster ambient
float4 EnhLightData3;  // interior ambient + directional
```
→ **Playground equivalent**: `SB_Shadow_Direction`, `SB_Shadow_Diffuse`,
  `SB_Shadow_Ambient`, `SB_Interior_*`
  (SB provides MORE: full interior lighting template, interior fog, 3 nearby lights)

#### enbParmLink (Expression Evaluator)
```
// Reads ENB built-in state
nightFactor := enb.fNightDayFactor;
interiorFactor := enb.fCurrentLocationIndicator;

// Reads game memory directly (ASLR-dependent!)
skyPtr := addr.getAbsInt(0x03186BA8);
gameHour := addr.getAbsFloat(skyPtr + 0x1B0);

// Computes per-parameter values
bloomDay := 0.8; bloomNight := 1.2;
bloom := lerp(bloomDay, bloomNight, nightFactor);

// Pushes to specific shader parameters
enb.setFloat("enbbloom.fx", "ExternalParameters", "WeatherBloom", bloom);
```
→ **Playground equivalent**: All game memory reads are replaced by typed trackers
  (no raw memory addresses, no ASLR issues, no version-specific offsets).
  The expression evaluation is replaced by C++ computation in the tracker layer.

---

## 2. Capability Gap Analysis

| Capability | ENB Extender | ParmLink | Playground v2.0 |
|---|---|---|---|
| Camera position/FOV | ✅ Basic | ❌ | ✅ + matrices |
| Sun/moon position | ✅ Direction only | ❌ | ✅ + NDC, phases, both moons |
| Atmosphere colors (8 ch) | ✅ Partial (5) | ❌ | ✅ All 8 |
| Fog colors/distances | ✅ Basic | ❌ | ✅ + height fog |
| Shadow light data | ✅ Basic | ❌ | ✅ |
| Interior lighting | ✅ Basic | ❌ | ✅ + template + fog |
| Wind/weather | ❌ | Via memory | ✅ Full (flags, wind, precip, lightning) |
| Player state | ❌ | Via memory | ✅ Full (vitals, combat, water, movement) |
| Active magic effects | ❌ | ❌ | ✅ (night eye, detect, slow time, etc.) |
| Nearby lights | ❌ | ❌ | ✅ 3 nearest + summary |
| Actor values | ❌ | Via memory | ✅ 160+ values |
| Equipment | ❌ | Via memory | ✅ Full |
| Crosshair/target | ❌ | ❌ | ✅ |
| Quest state | ❌ | ❌ | ✅ |
| UI/menu state | ❌ | ❌ | ✅ |
| ImageSpace modifiers | ❌ | ❌ | ✅ |
| G-Buffer materials | ❌ | ❌ | ✅ |
| Motion vectors | ❌ | ❌ | ✅ |
| **Per-param weather control** | ❌ | ✅ (cfg) | **Phase 2 adds this** |
| **Expression evaluation** | ❌ | ✅ (ExprTk) | **Phase 4 adds this** |
| **Game memory access** | ❌ | ✅ (addr.*) | Not needed (typed API) |

### Key Finding

Playground already covers **100%** of ENB Extender's data and **~90%** of ParmLink's
game-state reading capability through typed, version-safe trackers. The two remaining
gaps are:

1. **Per-parameter weather computation** → Phase 2 (C++ WeatherParameterComputer)
2. **Expression evaluation for user customization** → Phase 4 (ParmLink compatibility layer)

---

## 3. The Per-Parameter Weather Problem

Pilgrim's approach via ParmLink:
- Define N weather presets (clear, cloudy, rain, snow, fog, etc.)
- For each ENB shader parameter (bloom intensity, saturation, fog density, etc.),
  define per-weather values
- Evaluate `lerp(weatherA_value, weatherB_value, transitionPercent)` every frame
- Push the interpolated value to the shader via `enb.setFloat()`

**Why this is better in C++:**
- ParmLink's ExprTk evaluator parses and evaluates text expressions every frame
- C++ does the same math in compiled native code (100x faster)
- C++ has direct access to `RE::Sky::currentWeatherPct` (no memory address guessing)
- C++ can read actual weather FormIDs and match against classification tables
- C++ can pre-compute ALL parameters in one pass, push them in bulk
- No ASLR, no version-specific offsets, no config file parsing at runtime

---

## 4. Recommended Architecture (Phases 2-4)

### Phase 2: WeatherParameterComputer (C++)
- New tracker in Playground that reads current/previous weather FormIDs
- Classifies weathers into categories (clear, cloudy, rain, snow, fog, ash, etc.)
- For each registered shader parameter, interpolates between category values
- Pushes pre-computed weather-adjusted values as new SB_ float4 parameters
- Users configure per-weather values in an INI file (not an expression language)

### Phase 3: SharedMemoryBridge
- Named shared memory region: `Playground_GameState`
- Writes AllData struct every frame
- External apps read without touching the game process
- Header-only C client library for external tools

### Phase 4: ParmLink Replacement
- Parse enbParmLink.cfg-compatible expression files
- Replace `addr.*` memory reads with Playground typed data
- Replace `enb.*` reads with Playground state
- Evaluate expressions using compiled C++ lambdas (no runtime parsing)
- Full backward compatibility: drop-in replacement for enbParmLink.dll
