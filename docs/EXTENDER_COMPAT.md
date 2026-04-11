# ENB Extender Compatibility

Playground v3 includes a complete replacement for [ENB Extender](https://www.nexusmods.com/skyrimspecialedition/mods/104824) (Kitsuune's annotation system). Five integrated systems handle annotation parsing, extern bindings, weather separation, parameter binding, and native GUI panels.

## Overview

| System | File | Purpose |
|---|---|---|
| ShaderPreProcessor | `ShaderPreProcessor.h/cpp` | HLSL annotation parser + source transforms |
| ExternBindingProcessor | `ExternBindingProcessor.h/cpp` | Game data -> shader extern variables |
| WeatherSeparationEngine | `WeatherSeparationEngine.h/cpp` | Per-weather per-ToD parameter overrides |
| ParameterBindingEngine | `ParameterBindingEngine.h/cpp` | Conditional param-to-param visibility/readonly |
| ENBGuiIntegration | `ENBGuiIntegration.h/cpp` | Native ATB panels in ENB's Shift+Enter editor |

---

## 1. ShaderPreProcessor

Intercepts shader source in the D3DCompile hook to parse custom annotations and transform source code.

### Annotation Parsing

Parses `< type key = value; ... >` blocks on variable and technique declarations:

```hlsl
float4 myParam < string UIName = "My Parameter";
                 string UIWidget = "Color";
                 float UIMin = 0.0; float UIMax = 1.0;
                 string Separation = "ExteriorWeather";
                 string UIGroup = "Colors.Primary";
                 string UIBinding = "myToggle";
                 string UIBindingCondition = "==1"; >;
```

### Supported Annotations

#### Standard ENB
| Annotation | Type | Description |
|---|---|---|
| `UIName` | string | Display name in ENB editor |
| `UIWidget` | string | Widget type: Spinner, Color, Vector, Dropdown, Quality |
| `UIMin` / `UIMax` | float | Value range limits |
| `UIHidden` | int | 1 = hidden from editor |
| `UIList` | string | Dropdown items (comma-separated) |

#### Extender: Weather Separation
| Annotation | Type | Description |
|---|---|---|
| `Separation` | string | `"ExteriorWeather"` or `"Weather"` |

#### Extender: UI Grouping
| Annotation | Type | Description |
|---|---|---|
| `UIGroup` | string | Dot-separated hierarchy (e.g., `"Colors.Primary"`) |
| `UIGroupName` | string | Display name for the group |
| `UIGroupOpen` | bool | Group starts expanded |
| `UITopLevel` | bool | Top-level group (no parent) |
| `UIOrdering` | int | Sort order within group |
| `UIVisible` | bool | Initial visibility state |
| `UIReadOnly` | bool | Prevent editing |
| `UIIgnorePerfMode` | bool | Ignore performance mode override |

#### Extender: Parameter Binding
| Annotation | Type | Description |
|---|---|---|
| `UIBinding` | string | Source parameter name to bind against |
| `UIBindingFile` | string | Source shader file (default: same shader) |
| `UIBindingProperty` | string | `readonly`, `readwrite`, `hidden`, `visible` |
| `UIBindingCondition` | string | Comparison: `==2`, `!=0`, `<5`, `>=1`, etc. |

#### Extender: Unique Name
| Annotation | Type | Description |
|---|---|---|
| `UniqueName` | string | Override for database key (instead of shaderFile::varName) |

#### Extender: Extern Binding
| Annotation | Type | Description |
|---|---|---|
| `ExternBinding` | string | Game data source name (see ExternBindingProcessor) |

### Source Transformations

1. **fxgroup -> sequential techniques**: Transforms `fxgroup` blocks into sequential technique naming (`BaseName`, `BaseName1`, etc.)
2. **#pragma uidefine -> #define**: Injects `#define` directives from annotation-controlled toggles
3. **Strip string vars**: Removes `string` variable declarations (UI-only markers for group begin/end/separator)

### AnnotationDatabase

Persistent singleton that stores all parsed parameter metadata. Keyed by unique name (`shaderFile::varName` or custom `UniqueName`). Increments a generation counter on every `MergeFromShader` call, enabling downstream systems to detect changes.

---

## 2. ExternBindingProcessor

Maps ENB Extender `ExternBinding` annotation names to live game data from AllData.

### Supported Bindings

| ExternBinding Name | Source | Description |
|---|---|---|
| `WVPMatColumn0..3` | CameraData.ViewProjMatrix | ViewProj column extraction (row-major) |
| `InvWVPMatColumn0..3` | CameraData.InvViewProj | Inverse ViewProj columns |
| `InvCamRotMatColumn0..2` | Derived from ViewMatrix | Inverse camera rotation columns |
| `GameTime` | RenderData.FrameInfo.y | Accumulated game time |
| `WindSpeed` | WeatherData.WindLive.x | Sky wind speed |
| `IsInterior` | InteriorData.IsInterior.x | Interior flag |
| `FOV` | CameraData.Info.x | Field of view (degrees) |
| `NearClip` | CameraData.Info.y | Near clip plane |
| `FarClip` | CameraData.Info.z | Far clip plane |
| `CameraPosition` | CameraData.WorldPos | Camera world position |
| `SunDirection` | CelestialData.SunDirection | Sun direction vector |
| `SunColor` | AtmosphereData.SunlightColor | Sunlight color |

Values are pushed via `ENBSetParameter` to the specific shader's UIName each frame.

---

## 3. WeatherSeparationEngine

Per-weather, per-time-of-day parameter overrides driven by `Separation` annotations.

### How It Works

1. ShaderPreProcessor parses `Separation="ExteriorWeather"` or `Separation="Weather"` annotations
2. WeatherSeparationEngine reads per-weather override files
3. Each frame: interpolates between current/previous weather + time-of-day slots
4. Pushes overridden values via ENBSetParameter

### Override File Format

Files in `Data/SKSE/Plugins/Playground/WeatherSep/{FormID:08X}.ini`:

```ini
; 4-slot ToD
[Morning]
myParam.x = 0.5

[Day]
myParam.x = 1.0

[Sunset]
myParam.x = 0.7

[Night]
myParam.x = 0.1
```

Also supports 6-slot ToD (Dawn/Sunrise/Day/Sunset/Dusk/Night).

### Interpolation

- **ToD**: Smoothstep blending between adjacent time slots
- **Weather transition**: Linear blend between outgoing and incoming weather using Sky::currentWeatherPct
- **Exterior only**: `ExteriorWeather` separation skips interiors

---

## 4. ParameterBindingEngine

Evaluates conditional visibility and readonly rules based on `UIBinding` annotations.

### Rule Format

```hlsl
float4 detail < string UIBinding = "enableDetail";
                string UIBindingCondition = "==1";
                string UIBindingProperty = "visible"; >;
```

This means: `detail` is visible only when `enableDetail` equals 1.

### Supported Conditions

| Operator | Example | Meaning |
|---|---|---|
| `==` | `==1` | Equal to 1 |
| `!=` | `!=0` | Not equal to 0 |
| `<` | `<5` | Less than 5 |
| `<=` | `<=10` | Less than or equal to 10 |
| `>` | `>0` | Greater than 0 |
| `>=` | `>=1` | Greater than or equal to 1 |

### Properties

| Property | Effect |
|---|---|
| `hidden` | Hide parameter when condition is true |
| `visible` | Show parameter when condition is true |
| `readonly` | Make read-only when condition is true |
| `readwrite` | Make editable when condition is true |

### Evaluation

Runs every frame after ENBSetParameter push. Reads the source parameter's current value, evaluates the condition, and applies the property. For `hidden`/`visible`, drives ATB visibility via `TwDefine`.

---

## 5. ENBGuiIntegration

Creates native AntTweakBar (ATB) panels inside ENB's Shift+Enter GUI editor.

### Two Panel Types

**AllData Bar** — Read-only game state with smart widgets:
- Color4 type (COLOR3F + alpha float) for color parameters
- Dir4 type (DIR3F + w float) for direction parameters
- Float4 type (x/y/z/w sub-fields) for generic parameters
- Automatic classification by parameter name heuristics

**Per-Shader Annotated Bars** — Read-write parameters from AnnotationDatabase:
- Widget type mapped from UIWidget annotation (Color, Spinner, Dropdown)
- UIGroup hierarchy preserved
- Read-write via TwAddVarCB + ENBGet/SetParameter callbacks
- UIBinding-driven visibility toggling via TwDefine

### ATB Functions Resolved

14 ATB exports resolved from d3d11.dll at runtime:
`TwNewBar`, `TwDeleteBar`, `TwAddVarRO`, `TwAddVarRW`, `TwAddVarCB`, `TwAddSeparator`, `TwDefine`, `TwDefineStruct`, `TwDefineEnumFromString`, `TwGetBarCount`, `TwGetBarByIndex`, `TwGetBarName`, `TwRemoveVar`, `TwRemoveAllVars`

### Lifecycle

- Bar creation deferred until TwGetBarCount() > 0 (proves ATB is initialized)
- Annotated bars rebuilt when AnnotationDatabase generation changes
- ENBGetParameter sync loop gated behind ENBIsEditorActive (editor-only overhead)
- No Shutdown at process exit (intentional — SKSE plugins are process-lifetime)
