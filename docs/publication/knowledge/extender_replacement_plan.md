# ENB Extender/Helper Replacement Plan (Updated 2026-03-05)

## What The Extender Actually Does (Architecture)

The Extender operates primarily as a **D3DCompile pre-processor**:
1. Intercepts D3DCompile calls (same hook point as SB_ShaderDebug)
2. Pre-parses HLSL source for custom annotations (Separation, UIGroup, UIBinding, etc.)
3. Strips non-HLSL syntax before passing to real D3DCompile
4. Transforms fxgroup blocks into sequential technique naming
5. Processes `#pragma uidefine` into `#define` injections
6. Builds a parameter metadata database from parsed annotations
7. Manages per-weather .fx.ini config files
8. Drives ENB GUI panels using the metadata

## SkyrimBridge's Existing Infrastructure

We already have:
- D3DCompile hook (SB_ShaderDebug.cpp HookD3DCompile/HookD3DCompile2)
- Full source code access in the hook (pSrcData, SrcDataSize, pDefines)
- ShaderCache (pre-compilation cache check + post-compilation store)
- WeatherParameterComputer (per-weather interpolation, INI-driven)
- All camera matrices the Extender provides (ViewProj, InvViewProj, etc.) + 22 more domains
- ENBSetParameter/ENBGetParameter access
- .dllplugin (SB_ENBPlugin.cpp) with ENB SDK exports

## Implementation Phases

### Phase 1: Annotation Pre-Parser (`ShaderPreProcessor`) — COMPLETE 2026-03-05
- New files: `src/core/ShaderPreProcessor.h/cpp`
- Hook point: Inside HookD3DCompile, BEFORE cache check and real compile
- Parse custom annotations from shader source:
  - `Separation` (None/ExteriorWeather/Weather)
  - `UIGroup`, `UIGroupName`, `UIGroupOpen`
  - `UIVisible`, `UIReadOnly`, `UIBinding`, `UIBindingProperty`, `UIBindingCondition`
  - `UniqueName`, `UITopLevel`, `UIOrdering`, `UIIgnorePerfMode`
  - `ExternBinding`
  - `fxgroup` blocks
  - `#pragma uidefine`
- Strip non-HLSL annotations before passing to real D3DCompile
- Build in-memory `ParameterMetadata` database

### Phase 2: Weather Separation Engine — COMPLETE 2026-03-05
- Extends WeatherParameterComputer with annotation-driven separation
- Per-weather .fx.ini file management (load/save per weather FormID)
- Two modes: ExteriorWeather (exterior only) vs Weather (always)
- 4-slot ToD (ENB native) AND 6-slot ToD (SkyrimBridge exclusive)
- Runtime: use ENBSetParameter to override values based on current weather + ToD

### Phase 3: Extern Binding System — COMPLETE 2026-03-05
- Map ExternBinding names to SkyrimBridge AllData fields:
  - `WVPMatColumn[0-3]` → camera.ViewProjMatrix rows
  - `InvWVPMatColumn[0-3]` → camera.InvViewProj rows
  - `InvCamRotMatColumn[0-2]` → camera.ViewMatrix inverse rotation
  - PLUS all 149 SB params as potential extern bindings
- Inject values via define substitution or ENBSetParameter

### Phase 4: Source Transformation — COMPLETE 2026-03-05 (built into Phase 1)
- fxgroup → sequential technique naming (source-to-source transform in D3DCompile hook)
- #pragma uidefine → #define injection with .fx.ini persistence
- UIGroupBegin/UIGroupEnd scope handling

### Phase 5: Parameter Binding Engine — COMPLETE 2026-03-05
- Real-time parameter-to-parameter binding
- Condition evaluation (==, !=, <, >, <=, >=)
- Property toggling (visible/hidden/readonly/readwrite)
- Drives both ImGui overlay AND ENB GUI parameter state

### Phase 6: ENB GUI Integration / Object Window — COMPLETE 2026-03-05
- Use ENB SDK GUI callback in .dllplugin for native Shift+Enter panels
- Object Window: form browser using EditorIDCache + game singleton access
- Weather editor: per-weather parameter tables with ToD interpolation

## Key Insight
Almost everything the Extender does is a source-to-source transformation in the D3DCompile hook.
SkyrimBridge already owns that hook. The work is building the annotation parser and
the metadata-driven runtime systems on top of it.
