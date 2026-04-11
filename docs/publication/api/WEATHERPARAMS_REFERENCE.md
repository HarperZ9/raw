# SkyrimBridge v3.0 — WeatherParams.ini Reference

The `WeatherParameterComputer` reads `WeatherParams.ini` at startup and hot-reloads
it when the file changes. It interpolates per-weather values and pushes them to ENB
as additional SB_WP_* parameters each frame.

Location: `Data/SKSE/Plugins/SkyrimBridge/WeatherParams.ini`

---

## File Format

```ini
[ShaderFile:ParameterGroup:ParameterName]
; Weather values — one per weather type
Clear=<value>
Cloudy=<value>
Foggy=<value>
Rain=<value>
ThunderRain=<value>
Snow=<value>
Blizzard=<value>
Ash=<value>
Special=<value>

; Optional keys
TransitionSpeed=<float>    ; Lerp speed during weather transitions (default: 1.0)
MinValue=<float>           ; Clamp floor
MaxValue=<float>           ; Clamp ceiling
SmoothLerp=<true|false>    ; Use hermite smoothstep instead of linear lerp
SBParam=<SB_WP_Name>       ; SkyrimBridge parameter name to push
```

---

## Weather Categories

| Category | When Active |
|----------|-------------|
| Clear | Pleasant weather, no clouds |
| Cloudy | Overcast but no precipitation |
| Foggy | Fog-heavy weather types |
| Rain | Light to moderate rain |
| ThunderRain | Thunderstorms with lightning |
| Snow | Light to moderate snowfall |
| Blizzard | Heavy snow with strong wind |
| Ash | Solstheim ash storms |
| Special | Vampiric/special weather types |

The computer classifies the current weather by checking TESWeather flags in order:
Snow+high wind -> Blizzard, Snow -> Snow, Rain+thunder -> ThunderRain, Rain -> Rain,
Fog flag -> Foggy, Cloudy flag -> Cloudy, Pleasant flag -> Clear, else -> Special.

---

## Defined Parameters

### Bloom (enbbloom.fx)

```ini
[enbbloom.fx:Bloom:WeatherBloomIntensity]
SBParam=SB_WP_BloomInt
Clear=0.8
Cloudy=0.6
Foggy=0.4
Rain=0.35
ThunderRain=0.3
Snow=0.55
Blizzard=0.25
Ash=0.3
Special=0.5
TransitionSpeed=0.8
MinValue=0.0
MaxValue=2.0

[enbbloom.fx:Bloom:WeatherBloomRadius]
SBParam=SB_WP_BloomRad
Clear=1.0
Cloudy=1.1
Foggy=1.3
Rain=1.2
ThunderRain=1.1
Snow=1.15
Blizzard=1.4
Ash=1.25
Special=1.0
```

### Adaptation (enbadaptation.fx)

```ini
[enbadaptation.fx:Adaptation:WeatherAdaptSpeed]
SBParam=SB_WP_AdaptSpd
; Higher = faster eye adaptation
Clear=1.0
Cloudy=0.9
Foggy=0.7
Rain=0.8
ThunderRain=0.85
Snow=0.75
Blizzard=0.6
Ash=0.65
Special=1.0

[enbadaptation.fx:Adaptation:WeatherExposureBias]
SBParam=SB_WP_ExpBias
; Positive = brighter, negative = darker
Clear=0.0
Cloudy=-0.1
Foggy=-0.15
Rain=-0.2
ThunderRain=-0.25
Snow=0.1
Blizzard=-0.1
Ash=-0.2
Special=0.0
```

### Effect (enbeffect.fx)

```ini
[enbeffect.fx:Color:WeatherSaturation]
SBParam=SB_WP_Saturation
Clear=1.0
Cloudy=0.9
Foggy=0.75
Rain=0.7
ThunderRain=0.65
Snow=0.85
Blizzard=0.6
Ash=0.55
Special=0.8
SmoothLerp=true

[enbeffect.fx:Color:WeatherContrast]
SBParam=SB_WP_Contrast
Clear=1.05
Cloudy=1.0
Foggy=0.9
Rain=0.95
ThunderRain=1.05
Snow=0.95
Blizzard=0.85
Ash=0.9
Special=1.0

[enbeffect.fx:Color:WeatherColorTempShift]
SBParam=SB_WP_ColorTemp
; Positive = warm (yellow), negative = cool (blue)
Clear=0.05
Cloudy=-0.02
Foggy=-0.05
Rain=-0.1
ThunderRain=-0.12
Snow=-0.08
Blizzard=-0.15
Ash=0.1
Special=0.0
```

### PostPass (enbeffectpostpass.fx)

```ini
[enbeffectpostpass.fx:PostPass:WeatherSharpenStrength]
SBParam=SB_WP_Sharpen
Clear=1.0
Cloudy=0.9
Foggy=0.5
Rain=0.6
ThunderRain=0.7
Snow=0.8
Blizzard=0.4
Ash=0.5
Special=0.8

[enbeffectpostpass.fx:PostPass:WeatherGrainIntensity]
SBParam=SB_WP_Grain
Clear=0.02
Cloudy=0.03
Foggy=0.04
Rain=0.05
ThunderRain=0.06
Snow=0.03
Blizzard=0.07
Ash=0.08
Special=0.04
```

### PrePass (enbeffectprepass.fx)

```ini
[enbeffectprepass.fx:PrePass:WeatherAOIntensity]
SBParam=SB_WP_AO
Clear=1.0
Cloudy=0.9
Foggy=0.6
Rain=0.7
ThunderRain=0.75
Snow=0.8
Blizzard=0.5
Ash=0.6
Special=0.8

[enbeffectprepass.fx:PrePass:WeatherSSRIntensity]
SBParam=SB_WP_SSR
Clear=1.0
Cloudy=0.85
Foggy=0.4
Rain=1.2
ThunderRain=1.1
Snow=0.7
Blizzard=0.3
Ash=0.4
Special=0.8

[enbeffectprepass.fx:PrePass:WeatherGodRayIntensity]
SBParam=SB_WP_GodRay
Clear=1.0
Cloudy=0.5
Foggy=0.2
Rain=0.15
ThunderRain=0.1
Snow=0.3
Blizzard=0.05
Ash=0.1
Special=0.6
```

### DOF (enbdepthoffield.fx)

```ini
[enbdepthoffield.fx:DOF:WeatherDOFStrength]
SBParam=SB_WP_DOF
Clear=1.0
Cloudy=1.0
Foggy=0.6
Rain=0.8
ThunderRain=0.85
Snow=0.9
Blizzard=0.5
Ash=0.7
Special=1.0
```

### Lens (enblens.fx)

```ini
[enblens.fx:Lens:WeatherLensDirtIntensity]
SBParam=SB_WP_LensDirt
Clear=0.3
Cloudy=0.25
Foggy=0.15
Rain=0.5
ThunderRain=0.45
Snow=0.2
Blizzard=0.1
Ash=0.6
Special=0.3

[enblens.fx:Lens:WeatherRainOnLens]
SBParam=SB_WP_RainLens
Clear=0.0
Cloudy=0.0
Foggy=0.0
Rain=0.7
ThunderRain=0.85
Snow=0.0
Blizzard=0.0
Ash=0.0
Special=0.0
TransitionSpeed=1.5
SmoothLerp=true

[enblens.fx:Lens:WeatherFrostOnLens]
SBParam=SB_WP_FrostLens
Clear=0.0
Cloudy=0.0
Foggy=0.0
Rain=0.0
ThunderRain=0.0
Snow=0.3
Blizzard=0.7
Ash=0.0
Special=0.0
TransitionSpeed=0.5
SmoothLerp=true
```

---

## Editing While Playing

WeatherParams.ini supports hot-reload. Edit and save the file while the game
is running — changes take effect within 1-2 frames. No restart required.

## Adding Custom Parameters

Add a new `[section]` following the format above. The `SBParam` key determines
the ENB parameter name. Shaders can then declare:

```hlsl
float4 SB_WP_MyCustomParam;
```

And receive the interpolated value automatically.
