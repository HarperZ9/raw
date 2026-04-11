# Backend Systems Detail

## ShaderCache (`src/core/ShaderCache.h/cpp`)
- FNV-1a 64-bit hash of source+defines+entry+target+flags -> disk cache at `Data/SKSE/Plugins/SkyrimBridge/ShaderCache/{hash:016X}.dxbc`
- Integrates into SB_ShaderDebug D3DCompile hooks (cache check before compile, store after)
- In-memory LRU (`unordered_map<uint64_t, vector<uint8_t>>`) avoids repeated disk reads
- Eliminates 5-15s startup compilation penalty (ENB has no disk shader cache)

## SceneObserver Vtable Hooks (2026-03-05)
- **BSLightingShader::SetupMaterial** (vtable[0] idx 4): Reads specular/roughness/subsurface/rimLight/envMapScale per draw
- **BSLightingShader::SetupGeometry** (vtable[0] idx 6): Reads BSRenderPass numLights, passEnum, LODMode per draw
- **BSWaterShader::SetupMaterial** (vtable[0] idx 4): Reads water plane/color/specular/reflection/refraction/fresnel/wave params
- **BSEffectShader::SetupMaterial** (vtable[0] idx 4): Reads effect baseColor/colorScale/softFalloffDepth/falloffOpacity
- All use `write_vfunc()` on `RE::VTABLE_*[0]` -- clean vtable patching, no trampoline needed
- BSShader vtable layout: 0=dtor, 1=DeleteThis, 2=SetupTechnique, 3=RestoreTechnique, 4=SetupMaterial, 5=RestoreMaterial, 6=SetupGeometry, 7=RestoreGeometry
- BSRenderPass::LODMode is a bitfield struct (`.index: 7`, `.singleLevel: 1`), NOT an enum
- NiPlane uses `.constant` member (NOT `.d`) for the plane distance

## ENB Extender Replacement (2026-03-05)
- **ShaderPreProcessor** (`src/core/ShaderPreProcessor.h/cpp`): Annotation parser + source transformer in D3DCompile hook
  - Parses `< type key = value; >` annotation blocks on variables and techniques
  - Transforms: fxgroup -> sequential techniques, `#pragma uidefine` -> `#define` injection, strip string vars
  - AnnotationDatabase singleton: persistent metadata store keyed by unique param name, generation counter
  - Integrated into both HookD3DCompile/HookD3DCompile2 -- runs BEFORE cache check, cleaned source used for cache + compile
- **ExternBindingProcessor** (`src/core/ExternBindingProcessor.h/cpp`): Maps Extender ExternBinding names to AllData fields
  - WVPMatColumn[0-3], InvWVPMatColumn[0-3], InvCamRotMatColumn[0-2]
  - Also: GameTime, WindSpeed, IsInterior, FOV, NearClip, FarClip, CameraPosition, SunDirection, SunColor
  - Pushes via ENBSetParameter to specific shader's UIName each frame
- **WeatherSeparationEngine** (`src/core/WeatherSeparationEngine.h/cpp`): Per-weather per-ToD parameter overrides
  - Annotation-driven: reads Separation="ExteriorWeather"/"Weather" from AnnotationDatabase
  - Per-weather .fx.ini files in `Data/SKSE/Plugins/SkyrimBridge/WeatherSep/{FormID:08X}.ini`
  - 4-slot ToD (Morning/Day/Sunset/Night) + 6-slot ToD (Dawn/Sunrise/Day/Sunset/Dusk/Night)
  - Smoothstep interpolation between ToD slots + weather transition blend
- **ParameterBindingEngine** (`src/core/ParameterBindingEngine.h/cpp`): Conditional param-to-param binding
  - UIBinding/UIBindingCondition/UIBindingProperty annotations -> runtime rule evaluation
  - Condition ops: ==, !=, <, <=, >, >= against float values
  - Properties: hidden/visible/readonly/readwrite -- drives GUI state
- **ENBGuiIntegration** (`src/core/ENBGuiIntegration.h/cpp`): Native ATB panels inside ENB's Shift+Enter editor
  - Resolves 14 ATB exports from d3d11.dll (TwNewBar, TwAddVarRO/RW/CB, TwDefine, TwDefineStruct, TwDefineEnumFromString, etc.)
  - **AllData bar**: Read-only game state with smart widgets -- Color4 (COLOR3F+alpha), Dir4 (DIR3F+w), Float4 (x/y/z/w) per-param classification
  - **Per-shader annotated bars**: Built from AnnotationDatabase. Widget types from UIWidget (Color/Spinner/Dropdown), read-write via TwAddVarCB + ENBGet/SetParameter, UIGroup hierarchy, UIBinding visibility toggling via TwDefine
  - Editor-only sync (ENBGetParameter loop gated behind ENBIsEditorActive), generation-based rebuild, try/catch safety
  - Deferred bar creation: waits for TwGetBarCount()>0, 300-frame retry limit
  - No Shutdown at process exit (intentional -- TwDeleteBar during static destruction risks crash if d3d11.dll already unloaded)
- All 5 systems integrated into main.cpp frame update loop with try/catch safety
- **DebugGUI tabs**: Annotations, Param Editor, Object Window, ENB GUI (ATB status + annotated bar counts + binding state + editor sync status)
- D3DCompile hooks: null guard + try/catch fallback on pre-processor failure, ReadFloat handles scientific notation

## SB_ShaderDebug (D3DCompile diagnostic overlay)
- IAT hooks D3DCompile/D3DCompile2 via PE header scanning
- **PatchAllIATEntries must use SEH + VirtualQuery** -- MO2's usvfs_x64.dll has non-standard PE layout that crashes raw PE walks
- Overlay rendering driven by D3D11Hook::HookedPresent (no separate Present vtable hook)
- F10 toggle overlay, F11 clear errors, log to Data/SKSE/Plugins/SkyrimBridge_ShaderErrors.log

## Reliability Hardening (2026-03-05)
- **Self-healing tracker system**: TrackerHealth struct tracks per-tracker error counts. Auto-disables after 5 consecutive errors, retries every 300 frames (~5s).
- **SEH protection**: All 5 SceneObserver hooks + HookedPresent wrapped in `__try/__except`
- **Loading screen awareness**: Detects `LoadingMenu` via `RE::UI`, skips cell-dependent trackers during loading
- **NaN sanitization**: SanitizeAllData() checks every float4 via kParamTable before PushAllData
- **Atomic flags**: s_gameReady and s_guiVisible are std::atomic<bool>
- **Null safety fixes**: RegionTracker, WriteBackProcessor, RenderTracker
- **Threading model**: BSShader hooks + OnENBFrame + HookedPresent all run on render thread (single-threaded)

## Removed GPU Features (files on disk, not compiled)
- LuminanceHistogram (t17), LUTManager (t18/s2), HiZPyramid (t19), ConstantBuffer (b7), GBufferManager (t15/u4), DXBCPatcher (b15)
- No D3D11 resource slots are used by the active build
