# SkyrimBridge v4.0 — Bidirectional Engine Control
## From Data Bridge to Runtime Authoring Platform

**Author:** Zain Dana Harper  
**Date:** February 2026  
**Status:** Architecture Research  

---

## 1. The Paradigm Shift

SkyrimBridge v1–v3 is **read-only**: it observes the engine and pushes data outward to shaders. The innovation you're identifying is that CommonLibSSE-NG gives us the same **write access** that KreatE uses, but we're in a unique position to go much further because we already have:

1. A mature ImGui overlay (Layer 1 viewport)
2. D3D11 hooks (Layer 4 ShaderHooks)  
3. ENB parameter push infrastructure (105+ float4s)
4. Full game-state tracking across 19 domains

The question isn't "can we do what KreatE does?" — it's "what becomes possible when a tool can simultaneously **read** engine forms, **write** engine forms, **see** the post-processed result through ENB, and **export** the values to CK/xEdit format?"

**Answer: You become the entire weather/lighting/imagespace authoring pipeline, in-game, in real-time, with full ENB post-processing preview.**

---

## 2. What's Writable via CommonLibSSE-NG

Every type listed below is a plain C++ struct with public members. CommonLibSSE exposes them with no access control. Writing to these members takes effect **immediately** — the engine reads them every frame during rendering.

### 2.1 RE::TESWeather — The Core Record

The weather form is the single most visually impactful record in Skyrim. It controls everything the player sees outdoors. All members are directly writable:

```
RE::TESWeather (FormType 0x36 = WTHR)
│
├── Color Matrix (DALC): 17 color types × 4 times of day
│   ├── [0]  Sky-Upper          (Dawn/Day/Dusk/Night) ← RGBA per slot
│   ├── [1]  Fog-Near
│   ├── [2]  Cloud (Unknown)
│   ├── [3]  Ambient
│   ├── [4]  Sunlight
│   ├── [5]  Sun disc
│   ├── [6]  Stars
│   ├── [7]  Sky-Lower
│   ├── [8]  Horizon
│   ├── [9]  Effect Lighting
│   ├── [10] Cloud LOD Diffuse
│   ├── [11] Cloud LOD Ambient
│   ├── [12] Fog-Far
│   ├── [13] Sky Statics
│   ├── [14] Water Multiplier
│   ├── [15] Sun Glare
│   └── [16] Moon Glare
│
├── Fog Distances: near/far plane + fog power, per time of day
│   ├── dayNear, dayFar, dayPower
│   ├── nightNear, nightFar, nightPower
│   ├── dawnNear, dawnFar   (interpolated from night→day)
│   └── duskNear, duskFar   (interpolated from day→night)
│
├── Wind: speed, direction
├── Transition: transDelta (how long weather changes take, in hours)
├── Sun: glare amount, damage to vampires
│
├── Cloud Layers (32 layers):
│   ├── cloudTexture[32]     — DDS texture paths
│   ├── cloudSpeed[32]       — movement speed per layer
│   ├── cloudAlpha[32][4]    — opacity per layer per time of day
│   └── cloudColor[32][4]    — RGBA per layer per time of day
│
├── Precipitation: pointer to BGSShaderParticleGeometryData form
├── Visual Effect: pointer to BGSReferenceEffect
│
├── ImageSpaces: TESImageSpace* [4]  — one per time of day
│   ├── [0] Dawn ImageSpace
│   ├── [1] Day ImageSpace
│   ├── [2] Dusk ImageSpace
│   └── [3] Night ImageSpace
│
├── Volumetric Lighting: BGSVolumetricLighting*
├── Sun/Moon Lens Flare: BGSLensFlare*
│
└── Sound: ambient sounds, event sounds
```

**Writing to these is trivial:**
```cpp
auto* sky = RE::Sky::GetSingleton();
auto* weather = sky->currentWeather;

// Change sunlight color at current time of day to warm orange
weather->colorData[RE::TESWeather::ColorTypes::kSunlight]
                  [RE::TESWeather::ColorTimes::kDay].red   = 255;
weather->colorData[RE::TESWeather::ColorTypes::kSunlight]
                  [RE::TESWeather::ColorTimes::kDay].green  = 180;
weather->colorData[RE::TESWeather::ColorTypes::kSunlight]
                  [RE::TESWeather::ColorTimes::kDay].blue   = 120;

// Change fog distance
weather->fogData.dayNear = 5000.0f;
weather->fogData.dayFar  = 50000.0f;

// Takes effect THIS FRAME. No reload needed.
```

### 2.2 RE::TESImageSpace — HDR/Cinematic/DOF

```
RE::TESImageSpace (FormType IMGS)
│
├── HDR Parameters:
│   ├── eyeAdaptSpeed        — how fast brightness adjusts (0–100)
│   ├── bloomBlurRadius      — bloom kernel size (integers only)
│   ├── bloomThreshold       — brightness threshold for bloom
│   ├── bloomScale           — bloom intensity multiplier
│   ├── targetLuminanceMin   — adaptation target range
│   ├── targetLuminanceMax
│   ├── sunlightScale        — sunlight contribution
│   └── skyScale             — sky brightness contribution
│
├── Cinematic Parameters:
│   ├── saturation           — color saturation
│   ├── brightness           — overall brightness
│   ├── contrast             — contrast amount
│   └── tint { R, G, B, amount }  — color overlay
│
└── DOF Parameters:
    ├── strength             — DOF intensity
    ├── distance             — focal distance
    ├── range                — focal range
    └── mode                 — front/back/both
```

### 2.3 RE::TESImageSpaceModifier — Animated Overrides

```
RE::TESImageSpaceModifier (FormType IMAD)
│
├── duration                 — how long the modifier plays
├── animatable               — bool: keyframed vs static
│
├── Per-parameter keyframe arrays:
│   ├── bloomBlurRadius[]    — bloom over time
│   ├── bloomScale[]         — bloom intensity over time
│   ├── brightness[]         — brightness over time
│   ├── contrast[]           — contrast over time
│   ├── saturation[]         — saturation over time
│   ├── tintColor[]          — tint RGB over time
│   ├── tintAmount[]         — tint strength over time
│   ├── blurRadius[]         — motion blur over time
│   ├── doubleVision[]       — double vision strength over time
│   ├── dofStrength[]        — DOF over time
│   ├── dofDistance[]        — DOF focal distance over time
│   ├── dofRange[]           — DOF range over time
│   └── radialBlur[]         — radial blur over time
│
└── Apply modes: add, multiply, or override base ImageSpace
```

### 2.4 RE::BGSLightingTemplate — Interior Lighting

```
RE::BGSLightingTemplate (FormType LGTM)
│
├── ambientColor             — RGB ambient fill
├── directionalColor         — RGB dominant light
├── directionalRotationXY    — light direction (azimuth/elevation)
├── directionalFade          — directional falloff
├── fogNearColor             — near fog RGB
├── fogFarColor              — far fog RGB
├── fogNear                  — fog start distance
├── fogFar                   — fog end distance
├── fogPower                 — fog density curve
├── fogClipDistance           — fog max clip
├── fogHeightMid             — fog height center
├── fogHeightRange           — fog height falloff
├── fogHeightSlope           — fog height gradient
└── directionalAmbientColors — DALC per-axis ambient (6 colors)
```

### 2.5 RE::BGSVolumetricLighting — God Rays

```
RE::BGSVolumetricLighting (FormType VOLI)
│
├── intensity                — overall intensity
├── customColor              — use custom color flag
├── red, green, blue         — custom color values
├── densityContribution      — scatter density
├── scatter                  — scatter coefficient
└── phaseFunction            — anisotropy (Henyey-Greenstein g)
```

### 2.6 RE::TESObjectCELL — Cell Properties

```
RE::TESObjectCELL (FormType CELL)
│
├── lightingTemplate         — BGSLightingTemplate* (interiors)
├── imageSpace               — TESImageSpace*
├── waterForm                — TESWaterForm*
├── skyOverride              — TESWeather* (interior sky override)
│
├── Interior-specific:
│   ├── ambient, directional, fog colors (inherits or overrides template)
│   ├── fog near/far distances
│   └── directionalRotation
│
└── Exterior-specific:
    ├── forceHideLand        — terrain visibility
    └── regions              — linked TESRegion forms
```

### 2.7 RE::Sky — Runtime Weather Control

```
RE::Sky::GetSingleton()
│
├── currentWeather           — active TESWeather*
├── lastWeather              — previous TESWeather*
├── currentClimate           — TESClimate*
├── currentRoom              — BGSShaderParticleGeometryData*
├── mode                     — skyMode enum
│
├── ForceWeather(TESWeather*, bool override)  — set weather immediately
├── SetWeather(TESWeather*, bool, bool)       — transition to weather
├── ResetWeather()                            — return to climate default
│
├── currentGameHour          — readable/writable
├── windSpeed                — current wind
├── windAngle                — current direction
│
└── sun, sunGlare, precipitationType — current rendering state
```

### 2.8 RE::INISettingCollection — All Game Settings

```
auto* settings = RE::INISettingCollection::GetSingleton();
auto* setting = settings->GetSetting("fLODFadeOutMultObjects:LOD");
setting->data.f = 25.0f;  // immediate effect, no reload

// Can read/write ANY INI setting at runtime
// ~2000+ settings covering every aspect of the engine
```

### 2.9 Additional Writable Forms

| RE Type | What It Controls | Write Access |
|---|---|---|
| `RE::TESWaterForm` | Water color, opacity, flow, depth, fog, damage | Full struct access |
| `RE::TESRegion` | Region weather probabilities, music, sounds | Can modify weather lists |
| `RE::TESClimate` | Sun/moon timing, weather type distribution | Full access |
| `RE::BGSShaderParticleGeometryData` | Rain/snow particle properties | Full access |
| `RE::BGSReferenceEffect` | Visual effects attached to weather | Pointer swappable |
| `RE::BGSLensFlare` | Sun/moon lens flare | Full access |
| `RE::TESGlobal` | Global variables (game hours, etc.) | Float value writable |
| `RE::ActorValueOwner` | Actor values on any actor | SetBase/ModBase/RestoreActorValue |
| `RE::BGSSoundDescriptor` | Sound properties | Volume, frequency |

---

## 3. What This Enables: The SkyrimBridge Studio

### 3.1 Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    SkyrimBridge v4.0                             │
│                                                                 │
│  ┌──────────────┐   ┌──────────────┐   ┌──────────────────┐   │
│  │   TRACKERS    │   │   EDITORS    │   │    EXPORTERS     │   │
│  │  (Read Path)  │──▶│ (Write Path) │──▶│  (Output Path)   │   │
│  │              │   │              │   │                  │   │
│  │ 19 domains   │   │ Weather      │   │ xEdit records   │   │
│  │ 105+ float4  │   │ ImageSpace   │   │ CK paste data   │   │
│  │ per-frame    │   │ LightTemplate│   │ ENB INI section  │   │
│  │ push to ENB  │   │ VolumetricLt │   │ KreatE preset    │   │
│  │              │   │ Cell Props   │   │ JSON snapshot    │   │
│  │              │   │ INI Settings │   │ Clipboard        │   │
│  └──────┬───────┘   └──────┬───────┘   └──────────────────┘   │
│         │                  │                                    │
│         ▼                  ▼                                    │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                   ImGui Interface                         │  │
│  │                                                          │  │
│  │  ┌─────────┐ ┌──────────┐ ┌────────┐ ┌──────────────┐  │  │
│  │  │ Monitor │ │  Editor  │ │  Diff  │ │    Export     │  │  │
│  │  │  Panel  │ │  Panel   │ │ Panel  │ │    Panel      │  │  │
│  │  │         │ │          │ │        │ │              │  │  │
│  │  │ Live    │ │ Color    │ │ vs ESP │ │ Copy as:     │  │  │
│  │  │ values  │ │ pickers  │ │ vs Van │ │  xEdit       │  │  │
│  │  │ from    │ │ Sliders  │ │ vs Pre │ │  CK          │  │  │
│  │  │ trackers│ │ Curves   │ │        │ │  ENB INI     │  │  │
│  │  │         │ │ Form IDs │ │ Red =  │ │  JSON        │  │  │
│  │  │         │ │ Presets  │ │ changed│ │  Clipboard   │  │  │
│  │  └─────────┘ └──────────┘ └────────┘ └──────────────┘  │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │               ENB Shader Pipeline                         │  │
│  │  Engine changes → SB reads → pushes to ENB → you see it  │  │
│  │  Full post-processing preview of every edit in real-time  │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### 3.2 The Feedback Loop (What Makes This Unique)

**KreatE's loop:**
```
Edit weather in ImGui → Engine renders with new values → See game renderer output
```

**SkyrimBridge Studio's loop:**
```
Edit weather in ImGui → Engine renders with new values 
    → SkyrimBridge trackers read new values
    → Push to ENB as float4 parameters
    → ENB post-processing runs with updated data
    → See FULL post-processed output (ENB bloom, color grading, fog, DOF, etc.)
    → Adjust ENB settings in tandem if needed
    → Export BOTH weather record AND matching ENB profile
```

This is the critical difference. KreatE shows you the game renderer output. **SkyrimBridge Studio shows you the final ENB-processed output.** For anyone making an ENB preset, this is the difference between guessing and seeing.

When you adjust sunlight color in the weather record, you immediately see how ENB's bloom responds, how color grading shifts, how the atmospheric fog changes. You can then adjust the ENB-side parameters (via ENB's own GUI) simultaneously, seeing the combined result. Then you export BOTH as a matched pair.

### 3.3 The Export System — Copy/Paste to CK or xEdit

This is where real productivity innovation happens. Currently, weather mod authoring looks like:

1. Open CK or xEdit
2. Tweak a color value (guess at what it will look like)
3. Save the plugin
4. Launch game, wait for load
5. Look at the result
6. Alt-tab back, adjust, repeat

Or with KreatE:

1. Launch game, tweak values in real-time (great!)
2. Save as KreatE preset (non-destructive, runtime-only)
3. **Manually recreate every value in CK or xEdit to make an actual plugin** ← the bottleneck

SkyrimBridge Studio eliminates step 3 entirely:

#### xEdit Export Format

When you click "Copy as xEdit Record," SkyrimBridge serializes the current weather state into the exact text format that xEdit uses for paste operations:

```
[00XXXXXX] WTHR "EditorID" [SKYRIM.ESM]
  DALC - Directional Ambient Lighting Colors
    Directional - Day
      X+  Red: 156  Green: 148  Blue: 140
      X-  Red: 80   Green: 85   Blue: 95
      Y+  Red: 130  Green: 128  Blue: 125
      Y-  Red: 70   Green: 72   Blue: 78
      Z+  Red: 180  Green: 175  Blue: 170
      Z-  Red: 60   Green: 62   Blue: 65
    Directional - Night
      ...
  NAM0 - Colors
    Sky-Upper
      Sunrise:  Red: 120  Green: 140  Blue: 180  Alpha: 0
      Day:      Red: 100  Green: 150  Blue: 210  Alpha: 0
      Sunset:   Red: 180  Green: 130  Blue: 100  Alpha: 0
      Night:    Red: 15   Green: 20   Blue: 40   Alpha: 0
    Fog-Near
      ...
  FNAM - Fog Distance
    Day Near:   3000.0
    Day Far:    80000.0
    Day Power:  0.8
    Night Near: 2000.0
    Night Far:  40000.0
    Night Power: 1.2
  DATA - Precipitation Data
    Wind Speed: 0.15
    Wind Direction: 180
    Trans Delta: 0.25
    Sun Glare: 0.45
    Sun Damage: 0
```

The user can paste this directly into xEdit via "Add Record" → paste, creating an override or new record. No manual transcription.

#### CK Export Format

For Creation Kit users, export the values as a CK-compatible batch:

```ini
; SkyrimBridge Studio Export — Weather: SkyrimClear [000012C0]
; Paste these values into the CK Weather editor
; Generated: 2026-02-27 16:45:03

[Colors]
SkyUpper_Dawn     = 120,140,180,0
SkyUpper_Day      = 100,150,210,0
SkyUpper_Dusk     = 180,130,100,0
SkyUpper_Night    = 15,20,40,0
FogNear_Dawn      = 200,190,175,0
FogNear_Day       = 220,215,210,0
...

[Fog]
DayNear           = 3000.0
DayFar            = 80000.0
DayPower          = 0.8
NightNear         = 2000.0
NightFar          = 40000.0
NightPower        = 1.2

[Wind]
Speed             = 0.15
Direction         = 180

[Transition]
Delta             = 0.25
```

#### ENB Weather Profile Export

Export the current weather's visual characteristics as an ENB `enbseries.ini` weather section:

```ini
; SkyrimBridge Studio — ENB Weather Profile for SkyrimClear [000012C0]
; Drop this into your [WEATHER] section

[WEATHER000012C0]
; Auto-generated from live game state + ENB post-processing values
IgnoreWeatherSystem=false

; These values match the game-side weather you edited:
DayAmbientColorMult=1.15
DayDirectionalColorMult=1.05
DayFogColorMult=0.95
DayFogNear=3000.0
DayFogFar=80000.0

NightAmbientColorMult=0.85
NightDirectionalColorMult=0.40
...
```

#### JSON Snapshot

Full machine-readable export for scripting/automation:

```json
{
  "formID": "000012C0",
  "editorID": "SkyrimClear",
  "plugin": "Skyrim.esm",
  "timestamp": "2026-02-27T16:45:03Z",
  "weather": {
    "colors": {
      "skyUpper": {
        "dawn":  { "r": 120, "g": 140, "b": 180, "a": 0 },
        "day":   { "r": 100, "g": 150, "b": 210, "a": 0 },
        "dusk":  { "r": 180, "g": 130, "b": 100, "a": 0 },
        "night": { "r":  15, "g":  20, "b":  40, "a": 0 }
      },
      "fogNear": { ... },
      ...
    },
    "fog": {
      "dayNear": 3000.0, "dayFar": 80000.0, "dayPower": 0.8,
      ...
    },
    "wind": { "speed": 0.15, "direction": 180 },
    "imageSpaces": {
      "dawn":  "000XXXXX",
      "day":   "000XXXXX",
      "dusk":  "000XXXXX",
      "night": "000XXXXX"
    },
    "volumetricLighting": "000XXXXX"
  }
}
```

### 3.4 The Diff System

Show what has been modified versus the original form data:

```
┌─ Weather Diff: SkyrimClear [000012C0] ──────────────────────┐
│                                                              │
│  Source: Skyrim.esm          Modified: Runtime (unsaved)     │
│                                                              │
│  CHANGED:                                                    │
│    Sunlight Day:     (240,230,210) → (255,180,120)  ■■■     │
│    Fog Near Day:     8000.0 → 3000.0                ■■      │
│    Fog Far Day:      120000.0 → 80000.0             ■       │
│    Ambient Day:      (150,148,145) → (156,148,140)  ■       │
│    Wind Speed:       0.10 → 0.15                    ■       │
│                                                              │
│  UNCHANGED: 85 values                                        │
│                                                              │
│  [Copy Changes to Clipboard]  [Revert All]  [Apply as ESP]  │
└──────────────────────────────────────────────────────────────┘
```

This diff works against:
- **Vanilla values** (hardcoded reference from Skyrim.esm)
- **Loaded ESP/ESM values** (what the current load order has)
- **KreatE preset values** (if KreatE preset is loaded)
- **Previous SkyrimBridge snapshot** (what you exported last)

---

## 4. Expanded Editor Modules

### 4.1 Weather Editor

The core editor. Covers the full WTHR record:

| Section | Controls | Data Source |
|---|---|---|
| **Color Matrix** | 17 color types × 4 times of day = 68 color pickers, with drag-and-drop row/column operations | `weather->colorData[][]` |
| **DALC** | 6 directional ambient colors × 4 times = 24 color pickers | `weather->directionalAmbientLightingColors` |
| **Fog** | Near/far/power sliders per time of day | `weather->fogData` |
| **Clouds** | Per-layer: speed, alpha[4], color[4], texture path | `weather->cloudSpeed/Alpha/Color/Texture` |
| **Wind** | Speed + direction compass widget | `weather->windSpeed/windDirection` |
| **Transition** | Duration slider (hours) | `weather->transDelta` |
| **Sun** | Glare amount, vampire damage | `weather->sunGlare/sunDamage` |
| **Linked Forms** | Form picker for: ImageSpace[4], VolumetricLighting, Precipitation, VisualEffect, LensFlare | Pointer fields |
| **Time Control** | Set game hour + speed, advance to Dawn/Day/Dusk/Night | `RE::Sky`, `RE::TESGlobal` |
| **Weather Control** | Force weather by FormID, reset to climate default | `RE::Sky::ForceWeather()` |

### 4.2 ImageSpace Editor

| Section | Controls | Data Source |
|---|---|---|
| **HDR** | Eye adapt speed, bloom (blur/threshold/scale), target luminance min/max, sunlight/sky scale | `imageSpace->data.hdr` |
| **Cinematic** | Saturation, brightness, contrast, tint RGBA | `imageSpace->data.cinematic` |
| **DOF** | Strength, distance, range, mode (none/front/back/both) | `imageSpace->data.dof` |

### 4.3 ImageSpace Modifier Editor

| Section | Controls | Data Source |
|---|---|---|
| **Timeline** | Duration, play/pause/seek | `isMod->duration` |
| **Keyframes** | Per-parameter curve editor with add/remove/drag keyframes | `isMod->data` arrays |
| **Parameters** | Bloom, brightness, contrast, saturation, tint, blur, DOF, radial blur, double vision | All keyframe arrays |
| **Apply Mode** | Add / Multiply per parameter | Flags |
| **Preview** | Apply modifier to current scene, scrub timeline | `RE::ImageSpaceManager::ApplyMod()` |

### 4.4 Lighting Template Editor (Interiors)

| Section | Controls | Data Source |
|---|---|---|
| **Ambient** | RGB color picker | `template->ambientColor` |
| **Directional** | RGB color + rotation (azimuth/elevation gizmo) | `template->directionalColor/Rotation` |
| **Fog** | Near/far color, distance, power, clip, height (mid/range/slope) | `template->fog*` |
| **DALC** | 6 directional colors | `template->directionalAmbientColors` |

### 4.5 Volumetric Lighting Editor

| Section | Controls | Data Source |
|---|---|---|
| **Intensity** | Master intensity slider | `vol->intensity` |
| **Color** | Custom color toggle + RGB | `vol->customColor, r, g, b` |
| **Scattering** | Density, scatter coefficient, phase function anisotropy | `vol->densityContribution, scatter, phaseFunction` |

### 4.6 Cell Lighting Editor

| Section | Controls | Data Source |
|---|---|---|
| **Template** | Form picker for lighting template | `cell->lightingTemplate` |
| **ImageSpace** | Form picker for ImageSpace | `cell->imageSpace` |
| **Overrides** | Per-cell ambient/directional/fog overrides | Cell data fields |
| **Water** | Form picker for water type | `cell->waterForm` |

### 4.7 INI Settings Editor

| Section | Controls | Data Source |
|---|---|---|
| **Searchable list** | Filter ~2000 settings by name/category | `RE::INISettingCollection` |
| **Inline edit** | Float/int/bool type-aware editors | `setting->data` |
| **Presets** | Save/load INI setting groups | File I/O |
| **PerfGov link** | Show which settings PerfGov is governing | SB_PerfGov integration |

---

## 5. Advanced Capabilities

### 5.1 KreatE Preset Import/Export

KreatE uses its own preset format. SkyrimBridge can:
- **Import** KreatE presets (read .json, apply to engine forms)
- **Export** to KreatE format (for users who want to switch tools)
- **Bidirectional sync** if both are running (read KreatE's managed forms, avoid conflicts)

### 5.2 ESP Generation (Stretch Goal)

Using libskyrim/libbsa or raw binary serialization, SkyrimBridge could write an actual .esp file containing the modified weather/IS/lighting records. This would let users:

1. Edit everything in-game with full ENB preview
2. Click "Export as ESP"  
3. Get a ready-to-use plugin file in their Data folder
4. No CK or xEdit needed at all

This is a significant engineering effort (ESP binary format serialization) but would be transformative for the workflow.

### 5.3 Papyrus Script Bridge (Future)

Expose SkyrimBridge data to Papyrus scripts, enabling mod authors to create gameplay-responsive visual effects:

```papyrus
; In a quest script
Float Function GetSkyrimBridgeFloat(String paramName) Global Native
; "SB_Weather_Flags.x" → returns weather classification

Event OnUpdate()
    Float weatherType = GetSkyrimBridgeFloat("SB_Weather_Flags.x")
    If weatherType == 3.0  ; Rainy
        ; Trigger quest-specific rain visual effect
        ApplyImageSpaceModifier(myRainISMod)
    EndIf
EndEvent
```

### 5.4 Inter-Plugin Communication

Other SKSE plugins could query SkyrimBridge for data or register their own parameters:

```cpp
// Third-party SKSE plugin API
namespace SkyrimBridge::API {
    // Read any SB parameter by name
    float4 GetParameter(const char* name);
    
    // Register custom parameters that get pushed to ENB
    void RegisterFloat4(const char* name, std::function<RE::NiPoint4()> getter);
    
    // Subscribe to edit events (for undo/redo integration)
    void OnFormEdited(RE::FormID id, std::function<void(RE::TESForm*)> callback);
}
```

This makes SkyrimBridge a platform other plugins can build on.

---

## 6. Competitive Analysis vs KreatE

| Feature | KreatE | SkyrimBridge Studio |
|---|---|---|
| Weather editing | ✓ Full | ✓ Full (identical scope) |
| ImageSpace editing | ✓ Full | ✓ Full |
| ISMod editing | ✓ Full | ✓ Full + timeline scrubbing |
| Lighting Template | ✓ Full | ✓ Full |
| Volumetric Lighting | ✓ Full | ✓ Full |
| Cell Lighting | ✓ Full | ✓ Full |
| **ENB post-processing preview** | ✗ Game renderer only | **✓ Full ENB pipeline** |
| **ENB weather INI export** | ✗ | **✓ Auto-generates ENB profiles** |
| **xEdit record export** | ✗ | **✓ Copy-paste to xEdit** |
| **CK value export** | ✗ | **✓ Formatted for CK** |
| **Diff vs loaded ESP** | ✗ | **✓ Shows all changes** |
| **JSON machine-readable export** | ✗ | **✓ Full serialization** |
| **ESP file generation** | ✗ | Future (stretch goal) |
| **Shader data push** | ✗ | **✓ 105+ float4 to ENB shaders** |
| **D3D11 hooks** | ✗ | **✓ Material classification, GBuffer** |
| **Performance telemetry** | ✗ | **✓ PerfGov integration** |
| **Inter-plugin API** | ✗ | Future |
| DALC fix | ✓ Built-in | Import KreatE's fix |
| Non-destructive presets | ✓ | ✓ (compatible format) |
| **INI settings editor** | ✗ | **✓ Full INI access** |

The fundamental difference: KreatE is a **weather authoring tool**. SkyrimBridge Studio is a **visual development environment** that encompasses weather authoring as one module among many.

---

## 7. Implementation Architecture

### 7.1 Module Structure

```
SkyrimBridge.dll
├── Core/
│   ├── BridgeData.h              — existing shared parameter block
│   ├── TrackerManager.h          — existing read-path orchestrator
│   └── ParameterPush.h           — existing ENB push system
│
├── Trackers/                     — existing 19 domain trackers (read path)
│   ├── CelestialTracker.h
│   ├── WeatherTracker.h
│   ├── ... (all existing)
│   └── AddonTracker.h
│
├── Editors/                      — NEW: write-path modules
│   ├── EditorBase.h              — common undo/redo, dirty-tracking
│   ├── WeatherEditor.h           — RE::TESWeather manipulation
│   ├── ImageSpaceEditor.h        — RE::TESImageSpace manipulation  
│   ├── ISModEditor.h             — RE::TESImageSpaceModifier
│   ├── LightingEditor.h          — RE::BGSLightingTemplate
│   ├── VolumetricEditor.h        — RE::BGSVolumetricLighting
│   ├── CellEditor.h              — RE::TESObjectCELL properties
│   ├── INIEditor.h               — RE::INISettingCollection
│   └── FormBrowser.h             — generic form lookup by type/ID/name
│
├── Export/                       — NEW: output-path modules
│   ├── ExportBase.h              — common clipboard/file operations
│   ├── XEditExporter.h           — xEdit record text format
│   ├── CKExporter.h              — CK-compatible value format
│   ├── ENBProfileExporter.h      — enbseries.ini weather section
│   ├── JSONExporter.h            — machine-readable full snapshot
│   ├── KreatEExporter.h          — KreatE preset format compat
│   └── DiffEngine.h              — compare runtime vs ESP/vanilla
│
├── UI/                           — existing ImGui + NEW editor panels
│   ├── Viewport.h                — existing Layer 1 data viewer
│   ├── EditorUI.h                — NEW: editor panel framework
│   ├── ColorPickerWidget.h       — HSV/RGB picker with hex input
│   ├── CurveEditor.h             — keyframe curve editing
│   ├── FormPicker.h              — dropdown form selector with search
│   ├── DiffPanel.h               — side-by-side diff display
│   └── ExportPanel.h             — export format selection + preview
│
├── Render/                       — existing Layer 4
│   ├── ShaderHooks.h             
│   └── ShaderHooks.cpp           
│
└── API/                          — NEW: inter-plugin API
    ├── SkyrimBridgeAPI.h         — public header for other plugins
    └── APIServer.h               — registration + dispatch
```

### 7.2 Undo/Redo System

Every editor operation goes through a command pattern:

```cpp
struct EditCommand {
    RE::FormID       formID;
    std::string      fieldPath;     // "colorData[4][1].red"
    std::variant<float, int, uint8_t, RE::Color> oldValue;
    std::variant<float, int, uint8_t, RE::Color> newValue;
    
    void Execute();   // apply newValue
    void Undo();      // apply oldValue
};

class EditHistory {
    std::vector<EditCommand> history;
    int currentIndex = -1;
    
    void Push(EditCommand cmd);
    void Undo();     // Ctrl+Z
    void Redo();     // Ctrl+Y
    void Clear();
    
    // Get all changes since last save/export
    std::vector<EditCommand> GetDirtyCommands() const;
};
```

### 7.3 Form Snapshot System

Before any edit, snapshot the original form data:

```cpp
class FormSnapshot {
    RE::FormID id;
    RE::FormType type;
    std::vector<uint8_t> originalData;  // raw memcpy of form struct
    
    static FormSnapshot Capture(RE::TESForm* form);
    void Restore();  // revert to captured state
    
    // Diff against current state
    std::vector<FieldDiff> ComputeDiff() const;
};
```

This enables "Revert to Original" at any time, and powers the diff system.

---

## 8. Development Phases

### Phase 1: Foundation (Editor Core)
- `EditorBase` with undo/redo
- `FormSnapshot` capture/restore
- `FormBrowser` with search by type/ID/editorID
- Weather Editor (color matrix only — the biggest visual impact)
- Basic diff display

### Phase 2: Full Weather + ImageSpace
- Complete weather editor (fog, clouds, wind, linked forms)
- ImageSpace editor (HDR, cinematic, DOF)
- Lighting template editor
- Volumetric lighting editor

### Phase 3: Export Pipeline  
- xEdit text format export
- CK value format export
- ENB weather profile generation
- JSON snapshot
- Clipboard integration (Win32 clipboard API)

### Phase 4: Advanced
- ISMod timeline editor
- Cell property editor
- INI settings editor
- KreatE preset import/export
- Diff system with ESP comparison

### Phase 5: Platform
- Inter-plugin API
- Papyrus bridge
- ESP file generation
- Automated ENB+weather preset bundling

---

## 9. Why This Matters

The Skyrim visual modding workflow is currently fragmented across 5+ tools:

1. **CK** — author weather/IS/lighting (no preview, requires game restart)
2. **xEdit** — edit records precisely (no preview, requires game restart)
3. **KreatE** — preview weather edits (no ENB preview, manual transcription to CK/xEdit)
4. **ENB GUI** — adjust ENB post-processing (no game-state awareness)
5. **ENB presets** — static INI files (no connection to weather records)

SkyrimBridge Studio collapses these into a single real-time environment where:
- You see the **full pipeline** (engine + ENB) live
- You edit **any parameter** (game-side or ENB-side) in real-time
- You export the result in **any format** (xEdit, CK, ENB INI, JSON)
- The game-side and ENB-side are **aware of each other** through the data bridge

Nobody has built this because nobody has had simultaneous read/write engine access AND ENB integration AND export tooling in the same plugin. SkyrimBridge is the only codebase that can do all three.

---

*This transforms SkyrimBridge from a shader data pipeline into the definitive visual development environment for Skyrim.*
