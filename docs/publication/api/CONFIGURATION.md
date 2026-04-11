# Configuration Reference

SkyrimBridge uses three INI configuration files located in `Data/SKSE/Plugins/SkyrimBridge/`.

---

## FeedbackConfig.ini

Controls the GPU readback feedback loop and cross-shader ENB parameter readback.

### [Feedback] Section

Smoothing parameters for the 5x5 grid backbuffer sampling. All alphas are EMA (exponential moving average) — lower = smoother/slower, higher = noisier/faster.

| Key | Default | Description |
|---|---|---|
| `centerLumAlpha` | 0.05 | Smoothing for center pixel luminance |
| `sceneAvgAlpha` | 0.03 | Smoothing for scene average luminance |
| `sceneColorAlpha` | 0.08 | Smoothing for scene average color |
| `lumVelocityAlpha` | 0.10 | Smoothing for luminance rate of change |
| `colorShiftAlpha` | 0.10 | Smoothing for color shift detection |
| `sceneCutThreshold` | 0.30 | Luminance delta threshold for scene cut detection |
| `stabilityWindow` | 300.0 | Frame count for stability score calculation |

### [ENBReadback] Section

Up to 16 slots that read ENB shader parameters via `ENBGetParameter`. Values are available in `SB_Computed_ENBReadback` (slots 0-3 as .xyzw) and `SB_Computed_ENBReadback4` (slot 4 as float4).

**Format:** `SlotN = shader, paramName, size`

| Field | Description |
|---|---|
| `shader` | ENB shader filename (e.g., `enbadaptation.fx`) |
| `paramName` | UIName annotation of the target parameter |
| `size` | `4` for single float, `16` for float4 |

**Example:**
```ini
[ENBReadback]
Slot0 = enbadaptation.fx, AdaptedLuminance, 4
Slot1 = enbbloom.fx, BloomIntensity, 4
Slot2 = enbeffect.fx, ColorCorrection, 16
```

**Note:** Parameter names must match the UIName annotation in the target .fx file. If a parameter is not found, the slot silently fails (logged once at startup).

---

## WriteBackConfig.ini

INI-driven rules that write computed values back to game engine state via CommonLibSSE.

### Rule Format

Each rule is a `[Rule_N]` section (N = 0, 1, 2, ...):

```ini
[Rule_0]
Name=Descriptive name
Enabled=true
Target=CameraFOV
Source=AllDataField
SourceField=SB_Computed_Luminance.x
Scale=10.0
Offset=70.0
ClampMin=65.0
ClampMax=85.0
LerpAlpha=0.05
```

### Available Targets

| Target | Game Effect |
|---|---|
| `CameraFOV` | Third-person field of view (RE::PlayerCamera::worldFOV) |
| `CameraFOV1st` | First-person field of view (RE::PlayerCamera::firstPersonFOV) |
| `FogNearDist` | Fog near distance (RE::TESWeather::fogData) |
| `FogFarDist` | Fog far distance |
| `SunlightDiffuse_R/G/B` | Directional sunlight color (RE::Sky::sun->light) |
| `AmbientDiffuse_R/G/B` | Ambient light color |
| `ActorValue` | Player actor value (requires `ActorValueId`) |
| `TimeScale` | Game time multiplier (RE::Calendar::timeScale) |
| `GameHour` | Current game hour (RE::Calendar::gameHour) |

### Source Types

| Source | Keys | Description |
|---|---|---|
| `Fixed` | `FixedValue` | Constant float value |
| `AllDataField` | `SourceField` | Named field from AllData (e.g., `SB_Computed_Luminance.x`) |
| `ENBReadback` | `SourceIndex` | ENB readback slot index (from FeedbackConfig.ini) |

### Transform Pipeline

Applied in order: **Scale -> Offset -> Clamp -> Lerp**

| Key | Default | Description |
|---|---|---|
| `Scale` | 1.0 | Multiply factor |
| `Offset` | 0.0 | Additive offset (after scale) |
| `ClampMin` | -1e30 | Minimum output value |
| `ClampMax` | 1e30 | Maximum output value |
| `LerpAlpha` | 1.0 | Temporal smoothing [0..1], 1.0 = instant |

### Examples

**Luminance-driven FOV:**
```ini
[Rule_0]
Name=Luminance FOV Adjust
Enabled=true
Target=CameraFOV
Source=AllDataField
SourceField=SB_Computed_Luminance.x
Scale=10.0
Offset=70.0
ClampMin=65.0
ClampMax=85.0
LerpAlpha=0.05
```

**Fixed first-person FOV:**
```ini
[Rule_1]
Name=Fixed 1st Person FOV
Enabled=true
Target=CameraFOV1st
Source=Fixed
FixedValue=80.0
```

**ENB-driven fog distance:**
```ini
[Rule_2]
Name=ENB Fog Override
Enabled=true
Target=FogFarDist
Source=ENBReadback
SourceIndex=0
ClampMin=500.0
ClampMax=50000.0
LerpAlpha=0.1
```

---

## WeatherParams.ini

Per-weather parameter interpolation for the WeatherParameterComputer.

### Format

```ini
[shader.fx:GroupName:ParameterName]
Clear=value
Cloudy=value
Foggy=value
Rain=value
ThunderRain=value
Snow=value
Blizzard=value
Ash=value
Special=value
TransitionSpeed=1.0
SmoothLerp=true
```

### Weather Categories

| Category | Description |
|---|---|
| `Clear` | Pleasant weather, no precipitation |
| `Cloudy` | Overcast, no precipitation |
| `Foggy` | Heavy fog |
| `Rain` | Light to moderate rain |
| `ThunderRain` | Thunderstorm |
| `Snow` | Light to moderate snow |
| `Blizzard` | Heavy snow/blizzard |
| `Ash` | Volcanic ash (Solstheim) |
| `Special` | Unique/scripted weather |

### Keys

| Key | Default | Description |
|---|---|---|
| `TransitionSpeed` | 1.0 | Interpolation speed multiplier during weather transitions |
| `SmoothLerp` | true | Use smoothstep interpolation (false = linear) |
| `MinValue` | (none) | Clamp floor |
| `MaxValue` | (none) | Clamp ceiling |

Values are pushed as `SB_WP_*` parameters to the specified shader.

---

## File Locations

| File | Runtime Path |
|---|---|
| FeedbackConfig.ini | `Data/SKSE/Plugins/SkyrimBridge/FeedbackConfig.ini` |
| WriteBackConfig.ini | `Data/SKSE/Plugins/SkyrimBridge/WriteBackConfig.ini` |
| WeatherParams.ini | `Data/SKSE/Plugins/SkyrimBridge/WeatherParams.ini` |
| Shader cache | `Data/SKSE/Plugins/SkyrimBridge/ShaderCache/*.dxbc` |
| Weather separation | `Data/SKSE/Plugins/SkyrimBridge/WeatherSep/{FormID:08X}.ini` |
| SKSE log | `Documents/My Games/Skyrim Special Edition/SKSE/SkyrimBridge.log` |
| Shader error log | `Data/SKSE/Plugins/SkyrimBridge_ShaderErrors.log` |
