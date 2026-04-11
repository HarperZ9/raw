# Playground v1.0 — TODO

**Updated:** 2026-03-11
**Status:** Phase 1 — Pre-release verification
**Next milestone:** In-game testing (1A)

---

## What's Built (complete, compiles, untested in-game)

- 24 data domains, 133 float4 params, dirty-tracked push to 9 ENB shaders
- d3d11.dll proxy: wraps Device/Context/SwapChain(1-4), ShaderManager (FNV-1a), RenderPhaseDetector (9 phases), CBDirtyTracker (60-80% skip), state cache (61-90% redundancy filter), MaterialPipeline (DXBC G-buffer patching), OcclusionCuller
- 14 GPU systems: ToneMap, Atmosphere, GTAO, SSR, SSGI, VolumetricClouds, FrameGenerator, TemporalSuperRes, ClusteredLighting, MaterialClassifier, GrassLighting, TreeLODLighting, WaterBlending, DynamicCubemaps
- Compute infrastructure: ComputeManager, SRVInjector (t17-t27), LuminanceHistogram, HiZPyramid, LUTManager, TAAManager
- Pipeline orchestration: RenderPassManager, RenderPipeline (3-stage), SceneCompositor
- Backend: ShaderCache, ShaderPreProcessor, ExternBindingProcessor, WeatherSeparationEngine, ParameterBindingEngine, FeedbackProcessor, WriteBackProcessor, SceneObserver, EditorIDCache, EngineFixes, PapyrusBridge
- ENB integration: ENBInterface (SDK v1002, all 23 state types), ENBGuiIntegration (ATB bars), SharedMemoryBridge, Playground_ENB.dllplugin (correct SDK callback pattern)
- enbhelperse.dll: 14 exports including HelperPluginStarted/InitProxyFunctions/SelectableObjects
- Debug: DebugGUI (15 tabs), DebugRenderer (3D wireframe), NavMeshVisualizer, SkeletonVisualizer
- FeatureManager: Kahn's topo sort, SEH-protected init, cascading enable/disable
- CompatDetect: NativeEditorID Fix, po3_Tweaks, ENB, Community Shaders detection
- ENB v504 binary analysis: full decompile at `ENB Binary Dump/` (6 docs)
- Proxy: NvOptimusEnablement + AmdPowerXpressRequestHighPerformance (force discrete GPU)

---

## >>> FOCUS AREA: Phase 1A — Core Data Pipeline Verification (BLOCKING)

Everything downstream depends on this. Do not move to Phase 1B-1F until 1A passes.

- [ ] **1A.1** Deploy to MO2 and launch game
  - Run `deploy.bat full` to push DLLs + configs + shaders
  - Launch via MO2, check SKSE log for `"first push — X succeeded, 0 failed"`
  - If failures: check shader names (UPPERCASE), param UIName matching, UIWidget annotations
  - Log: `Documents/My Games/Skyrim Special Edition/SKSE/Playground.log`

- [ ] **1A.2** Verify enbhelperse.dll loads clean
  - Check ENB's AntTweakBar GUI for "ENBHELPERSE.DLL" error labels
  - Should be absent now (HelperPluginStarted/InitProxyFunctions exports added)
  - Verify `IsLoaded` returns true in SKSE log

- [ ] **1A.3** Verify Playground_ENB.dllplugin loads (if ENB present)
  - Check `Playground_ENBPlugin.log` in Stock Game directory
  - Should show: "Found ENB module", "ENB API resolved", "Callback registered"
  - Should show: "SharedMemory connected" (may be delayed until SKSE plugin starts)
  - Verify first push: "X dirty params x 9 shaders = Y SetParameter calls"

- [ ] **1A.4** Verify ENBGetState works (v1002 state queries)
  - Open ENB editor (Shift+Enter), check debug GUI shows "Editor active: YES"
  - Test new v1002 queries: WeatherCurrent, TimeOfDay, NightDayFactor, InteriorFactor
  - Use `ENBInterface::GetStateFloat()` helper for float values

- [ ] **1A.5** Verify shader params are received in HLSL
  - Add temp debug in enbeffect.fx: `return float4(SB_Render_Frame.x / 1000.0, 0, 0, 1);`
  - Should show reddish tint (frame counter / 1000)
  - Remove debug after confirming

- [ ] **1A.6** Verify dirty tracking efficiency
  - After 300 frames, log should show `"push #300 — X/133 params dirty"` where X < 133
  - Typical: ~20-40 dirty params per frame after initial population

---

## Phase 1B: ATB GUI Bars

- [ ] **1B.1** TwNewBar succeeds (check log)
- [ ] **1B.2** "Playground" bar visible in ENB editor (Shift+Enter)
- [ ] **1B.3** Float4 values update in real-time in the bar
- [ ] **1B.4** Weather Editor bar (fog/color editing) functional

---

## Phase 1C: Tracker Validation (parallelize with 1B)

Use debug GUI (INSERT key or configured hotkey) to verify each domain.

- [ ] **1C.1** Celestial: sun/moon directions, time data, golden/blue hour segments
- [ ] **1C.2** Atmosphere: sky upper/lower/horizon colors match visual sky
- [ ] **1C.3** Fog: near/far distances and colors match game fog
- [ ] **1C.4** Weather: wind/precip/lightning during storms, CloudCover, AuroraFade
- [ ] **1C.5** Player: position, vitals ratios, combat state, beast form detection
- [ ] **1C.6** Camera: FOV (radians), View matrix rows, 1st/3rd person
- [ ] **1C.7** Interior: flag toggles, ambient color, lighting template
- [ ] **1C.8** Shadow: light direction, diffuse/ambient colors
- [ ] **1C.9** Effects: ImageSpace modifier (blur, tint, saturation)
- [ ] **1C.10** Render: frame counter, delta time, screen size, stencil ref
- [ ] **1C.11** ImageSpace: HDR, cinematic, DOF, IMOD values
- [ ] **1C.12** Lights: 3 nearest lights (pos/radius, color/intensity), summary
- [ ] **1C.13** Crosshair: target type, distance, health ratio
- [ ] **1C.14** Equipment: weapon type, torch, shield, armor flags
- [ ] **1C.15** UIState: menu/dialogue/inventory/HUD/cinematic flags
- [ ] **1C.16** Region: location, worldspace form IDs
- [ ] **1C.17** Audio: music type, ambient reverberation
- [ ] **1C.18** NPCDetect: nearby hostile count, threat level, detection
- [ ] **1C.19** Scene: material counts, draw stats, shader flags, water params
- [ ] **1C.20** PerfMonitor: frame time, GPU timing

---

## Phase 1D: Subsystem Testing

- [ ] **1D.1** FeedbackProcessor: GPU readback produces non-zero luminance
- [ ] **1D.2** WriteBackProcessor: test FOV write-back rule
- [ ] **1D.3** ShaderCache: second launch shows cache hits in log
- [ ] **1D.4** SharedMemoryBridge: Playground_ENB.dllplugin reads shared memory
- [ ] **1D.5** SceneObserver: vtable hooks installed (log message on init)
- [ ] **1D.6** EditorIDCache: cache populates ("X editor IDs cached")
- [ ] **1D.7** d3d11 proxy: verify NvOptimusEnablement forces discrete GPU on laptop (if available)
- [ ] **1D.8** CompatDetect: detects NativeEditorID Fix / po3_Tweaks correctly

---

## Phase 1E: Edge Cases & Stability

- [ ] **1E.1** Loading screen: no crashes during cell transitions
- [ ] **1E.2** Main menu: SEH protection prevents crashes before game world loads
- [ ] **1E.3** Save/Load: no state corruption across save/load cycles
- [ ] **1E.4** Fast travel: tracker recovery after cell unload/reload
- [ ] **1E.5** Console `coc` to various cells: no crashes
- [ ] **1E.6** Interior/Exterior transitions: IsInterior flag toggles correctly
- [ ] **1E.7** Weather transitions: smooth parameter blending (no jumps)
- [ ] **1E.8** Extended play: 30+ minutes, check for memory leaks or drift

---

## Phase 1F: Config & Deploy Cleanup

- [ ] **1F.1** Verify deploy.bat targets correct MO2 mod path
- [ ] **1F.2** Verify config paths resolve through MO2 VFS
- [ ] **1F.3** Default WriteBackConfig.ini: all rules disabled (safe)
- [ ] **1F.4** Default FeedbackConfig.ini: conservative settings
- [ ] **1F.5** Deploy.bat copies: DLLs → SKSE/plugins/, configs → SKSE/plugins/Playground/, .dllplugin → ROOT/enbseries/
- [ ] **1F.6** Test deploy.bat full/restore/check cycle

---

## Phase 2: Shader Integration Polish (after Phase 1 passes gate)

### Gate: 1A must pass. 80%+ of 1C trackers must produce valid data.

- [ ] **2A** Tune all 9 .fx shaders for SB_ param integration
- [ ] **2B** Calibrate FeedbackProcessor (luminance, scene cuts, temporal stability)
- [ ] **2C** Weather separation configs for 10-20 common weathers
- [ ] **2D** Package alpha for 2-3 preset authors, collect feedback, iterate

---

## Phase 3: Dev Tools + Shader Capture (can overlap Phase 2)

All tools are IMPLEMENTED in DebugGUI — need testing and polish:
- [ ] **3A** Shader Source Viewer: test disk-based .fx loading with #include resolution
- [ ] **3B** Weather Transition Analyzer: verify rolling sparkline graphs
- [ ] **3C** EditorID Cross-Reference: crosshair → FormID/EditorID/mod
- [ ] **3D** Light Inspector: all tracked lights with position/color/radius

---

## Phase 4: Core/Adapter Split + Documentation + Nexus Release

- [ ] **4A** IRenderBridgeCore / IGameStateProvider interface split
- [ ] **4B** Separate CMake targets: core.lib + skyrim.dll
- [ ] **4C** API reference for shader authors
- [ ] **4D** Integration guide for preset authors
- [ ] **4E** Nexus Mods pages (Playground + DevKit)

---

## Phase 5: Fallout 4 Port

- [ ] F4SE + CommonLibF4 build environment
- [ ] Port tracker interfaces for FO4 RE:: singletons
- [ ] Adapt BridgeData for FO4 objects (settlements, power armor, radiation)
- [ ] Test with FO4 ENB presets

---

## Phase 6: RenderBridge Platform (long-term, after user validation)

- [ ] Data-driven shader patch system
- [ ] ReShade addon interface
- [ ] Public API specification
- [ ] Multi-game testing

---

## Scope Control — Do NOT

- Do NOT tune GPU systems (GTAO/SSR/SSGI) until Phase 1 passes — they work but are opt-in
- Do NOT add new tracker domains — 24 is enough for v1 release
- Do NOT write shader presets — let preset authors do shaders
- Do NOT optimize performance until real users report issues
- Do NOT pursue clustered Forward+, neural materials, or raytracing bridge until shipped
- Do NOT build ReShade addon until Core/Adapter split is clean

---

## Decision Gates

| Gate | Criteria | Blocker? |
|------|----------|----------|
| After 1A | SetParameter works in-game | **HARD BLOCK** |
| After 1C | 80%+ trackers produce valid data | Can ship alpha with known gaps |
| After 2D | At least 1 preset author finds it useful | Validates the concept |
| After 4B | Core builds without SKSE/CommonLibSSE | Ready for FO4 port |

---

## Recently Completed (2026-03-11)

- [x] ENB v504 full binary analysis → `ENB Binary Dump/` (6 documents, 2400+ lines)
- [x] SB_ENBPlugin.cpp rewritten: correct ENB SDK pattern (zero exports, callback-based, SetParameter push)
- [x] ENBInterface.h: added all v1002 ENBStateType values (8-23) + GetStateFloat/GetStateULong helpers
- [x] d3d11_proxy: added NvOptimusEnablement + AmdPowerXpressRequestHighPerformance exports
- [x] enbhelperse: added HelperPluginStarted, InitProxyFunctions, SelectableObjects exports
- [x] All 4 targets build clean: Playground.dll, d3d11.dll, Playground_ENB.dll, enbhelperse.dll
- [x] ClusteredLighting: spot light angle + shadow flag extraction from TESObjectLIGH (was TODO)
- [x] GrassLightingRenderer: compute post-process (clustered multi-light + SSS + ambient boost)
- [x] TreeLODLightingRenderer: compute post-process (atmosphere ambient + directional correction)
- [x] WaterBlendingRenderer: compute post-process (depth blending + caustics + fog)
- [x] DynamicCubemapRenderer: real-time 6-face cubemap capture + mip gen at t30
- [x] CompatDetect updated: PG now OVERRIDES CS for Grass/TreeLOD/Water/Cubemaps (only ExtendedMaterials deferred)
- [x] All 7 targets build clean with 0 errors after all additions
