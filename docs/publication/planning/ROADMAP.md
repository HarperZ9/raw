# SkyrimBridge v3 — Path A Development Roadmap

## Path A: Ship SkyrimBridge, Then Expand
Finish the core product, get real users, build reputation, then evolve into RenderBridge.

---

## Phase 1: SkyrimBridge v3 Release (Target: 2-4 weeks)

### Milestone 1A: Core Data Pipeline Verification (BLOCKING — do first)

Everything depends on ENBSetParameter actually delivering data to shaders.

- [ ] **1A.1** Launch game, check SKSE log for SetParameter results
  - Expected: `"first push — X succeeded, 0 failed"`
  - If failures: debug with log output (shader names, param names, ENB state flag)
  - Log location: `Documents/My Games/Skyrim Special Edition/SKSE/SkyrimBridge_v3.log`

- [ ] **1A.2** Verify enbhelperse.dll loads without ENB error
  - Check ENB log for "enbhelper" messages
  - Verify `IsLoaded` export returns true

- [ ] **1A.3** Verify ENBGetState works
  - Open ENB editor (Shift+Enter), check debug GUI shows "Editor active: YES"
  - Close editor, confirm it shows "no"

- [ ] **1A.4** Verify shader params are received in HLSL
  - Add temporary debug output in enbeffect.fx: `return float4(SB_Render_Frame.x / 1000.0, 0, 0, 1);`
  - Should show non-black (frame counter / 1000 = reddish after ~1000 frames)
  - Remove debug after confirming

- [ ] **1A.5** Verify dirty tracking works
  - After first push, log should show `"push #100 — X/150 params dirty"` (X < 150)
  - Most params should be stable after initial population

### Milestone 1B: ATB GUI Bars

- [ ] **1B.1** Confirm TwNewBar succeeds (check log for "TwNewBar succeeded")
- [ ] **1B.2** Open ENB editor (Shift+Enter), look for "SkyrimBridge" bar in bar list
- [ ] **1B.3** Expand bar, verify float4 values update in real-time
- [ ] **1B.4** If bars don't appear: add TwGetBarCount logging after creation to verify ATB state
- [ ] **1B.5** Test annotated shader bars (per-shader read-write params)
- [ ] **1B.6** Test Weather Editor bar (fog/color value editing)

### Milestone 1C: Tracker Validation (can parallelize with 1B)

Test each tracker domain produces sensible values. Use debug GUI (INSERT key).

- [ ] **1C.1** Celestial: TimeData, sun/moon positions, phase correct at different game hours
- [ ] **1C.2** Atmosphere: sky colors, sunlight diffuse match visual sky
- [ ] **1C.3** Fog: near/far distances, fog color match game fog
- [ ] **1C.4** Weather: wind, precipitation, lightning during storms
- [ ] **1C.5** Player: position, health/magicka/stamina ratios, combat state
- [ ] **1C.6** Camera: FOV, position, rotation matrices, 1st/3rd person detection
- [ ] **1C.7** Interior: IsInterior flag, ambient color, lighting template
- [ ] **1C.8** Shadow: shadow map resolution, bias, sun direction
- [ ] **1C.9** Effects: ImageSpace modifier data (blur, tint, saturation)
- [ ] **1C.10** Render: frame info, jitter, time dilation, game paused flag
- [ ] **1C.11** ImageSpace: HDR data, eye adaptation, bloom values
- [ ] **1C.12** Lights: nearby light count, brightest light color/radius
- [ ] **1C.13** Crosshair: target type, distance, health ratio
- [ ] **1C.14** Equipment: torch equipped, shield, weapon type
- [ ] **1C.15** UIState: menu open flags, HUD state, loading screen
- [ ] **1C.16** Region: location EditorID, worldspace, current cell
- [ ] **1C.17** Audio: music type, ambient volume level
- [ ] **1C.18** NPCDetect: detection level, nearby hostile count
- [ ] **1C.19** Scene: material counts, draw stats, shader flags
- [ ] **1C.20** PerfMonitor: frame time, GPU timing (if available)

### Milestone 1D: Subsystem Testing

- [ ] **1D.1** FeedbackProcessor: verify GPU readback produces non-zero luminance values
- [ ] **1D.2** WriteBackProcessor: test FOV write-back rule (modify WriteBackConfig.ini)
- [ ] **1D.3** ShaderCache: verify second launch shows cache hits in log
- [ ] **1D.4** ShaderPreProcessor: verify annotation parsing (check log for process count)
- [ ] **1D.5** ExternBindingProcessor: verify WVP matrix injection
- [ ] **1D.6** WeatherSeparationEngine: create a test weather INI, verify override applied
- [ ] **1D.7** SharedMemoryBridge: verify SkyrimBridge_ENB.dllplugin reads shared memory
- [ ] **1D.8** SceneObserver: verify vtable hooks installed (log message on init)
- [ ] **1D.9** EditorIDCache: verify cache populates (log shows "X editor IDs cached")
- [ ] **1D.10** PapyrusBridge: verify Papyrus functions registered (low priority)

### Milestone 1E: Edge Cases & Stability

- [ ] **1E.1** Loading screen: verify no crashes during cell transitions
- [ ] **1E.2** Main menu: verify SEH protection prevents crashes before game world loads
- [ ] **1E.3** Save/Load: verify no state corruption across save/load cycles
- [ ] **1E.4** Fast travel: verify tracker recovery after cell unload/reload
- [ ] **1E.5** Console commands: `coc` to different cells, verify no crashes
- [ ] **1E.6** Interior/Exterior transitions: verify IsInterior flag toggles correctly
- [ ] **1E.7** Weather transitions: verify smooth parameter blending (no sudden jumps)
- [ ] **1E.8** Extended play: run 30+ minutes, check for memory leaks or drift

### Milestone 1F: Config & Deploy Cleanup

- [ ] **1F.1** Deploy missing config files (FeedbackConfig.ini, WriteBackConfig.ini)
- [ ] **1F.2** Verify config paths resolve through MO2 VFS
- [ ] **1F.3** Create default WriteBackConfig.ini with all rules disabled (safe default)
- [ ] **1F.4** Create default FeedbackConfig.ini with conservative settings
- [ ] **1F.5** Update deploy.bat to target `E:\Modlists\SkyGroundChronicles\mods\Skyrim Bridge`
- [ ] **1F.6** Deploy.bat should copy: DLLs → SKSE/plugins/, configs → SKSE/plugins/SkyrimBridge/, .dllplugin → ROOT/
- [ ] **1F.7** Test deploy.bat full/restore cycle

---

## Phase 2: Shader Integration Polish (2-4 weeks after Phase 1)

### Milestone 2A: Bridge Alpha Shader Tuning

- [ ] **2A.1** Verify all 9 .fx shaders receive SB_ params (add SB_HasFeedback() checks)
- [ ] **2A.2** Tune enbeffect.fx: sun white balance, lightning flash, feedback contrast
- [ ] **2A.3** Tune enbbloom.fx: feedback-adaptive threshold, weather-responsive bloom
- [ ] **2A.4** Tune enbdepthoffield.fx: crosshair autofocus, underwater/menu suppression
- [ ] **2A.5** Tune enbadaptation.fx: torch bias, interior adjustment
- [ ] **2A.6** Tune enblens.fx: menu suppression, wet dirt effect
- [ ] **2A.7** Tune enbeffectpostpass.fx: fog color integration, lightning
- [ ] **2A.8** Tune enbeffectprepass.fx: snow cover, wind-driven particles
- [ ] **2A.9** Tune enbsunsprite.fx: game sun color tinting
- [ ] **2A.10** Tune enbunderwater.fx: game water color, submersion depth

### Milestone 2B: FeedbackProcessor Tuning

- [ ] **2B.1** Calibrate luminance sampling (center weight, grid distribution)
- [ ] **2B.2** Test temporal stability (scene cut detection, lum velocity)
- [ ] **2B.3** Configure ENBReadback slots for practical use cases
- [ ] **2B.4** Verify 1-frame delay doesn't cause visible artifacts

### Milestone 2C: Weather System Integration

- [ ] **2C.1** Create weather separation configs for 10-20 common weathers
- [ ] **2C.2** Test weather transitions (smooth blending between weather configs)
- [ ] **2C.3** Test interior-specific configs (if applicable)
- [ ] **2C.4** Document weather config format for preset authors

### Milestone 2D: External Testing

- [ ] **2D.1** Package SkyrimBridge v3 alpha for 2-3 ENB preset authors
- [ ] **2D.2** Write quick-start guide (install, verify, use SB_ params in shaders)
- [ ] **2D.3** Collect feedback on: param naming, value ranges, missing data
- [ ] **2D.4** Iterate based on feedback (add/remove/rename params)

---

## Phase 3: Codebase Factoring (2-3 weeks after Phase 2)

### Milestone 3A: Core/Adapter Split

- [ ] **3A.1** Define `IRenderBridgeCore` interface (shader hooks, cache, D3DCompile)
- [ ] **3A.2** Define `IGameStateProvider` interface (tracker data → AllData)
- [ ] **3A.3** Move game-agnostic code to `src/core/renderbridge/`
- [ ] **3A.4** Move game-specific code to `src/skyrim/`
- [ ] **3A.5** Separate CMake targets: `renderbridge_core.lib` + `skyrimbridge.dll`

### Milestone 3B: Documentation

- [ ] **3B.1** API reference for shader authors (SB_ param list, types, ranges)
- [ ] **3B.2** Integration guide for ENB preset authors
- [ ] **3B.3** Architecture document (for future contributors)
- [ ] **3B.4** Config file reference (all INI options documented)

---

## Phase 4: Developer Tools (2-3 weeks, can overlap Phase 3)

### Milestone 4A: Priority Tools (highest value for modding community)

- [ ] **4A.1** Light Inspector: ImGui panel showing all lights in current cell
- [ ] **4A.2** Weather Transition Analyzer: rolling graphs of atmospheric data
- [ ] **4A.3** EditorID Cross-Reference: point at object → FormID/EditorID/mod
- [ ] **4A.4** Shader Source Viewer: display captured D3DCompile source

### Milestone 4B: Ship as Optional DevKit

- [ ] **4B.1** Separate DevKit plugin (users install only if they want tools)
- [ ] **4B.2** Nexus Mods page for SkyrimBridge
- [ ] **4B.3** Nexus Mods page for SkyrimBridge DevKit

---

## Phase 5: Fallout 4 Port (4-6 weeks after Phase 3)

- [ ] **5.1** Set up F4SE + CommonLibF4 build environment
- [ ] **5.2** Port tracker interfaces (FO4 equivalents of RE:: singletons)
- [ ] **5.3** Adapt BridgeData for FO4 game objects
- [ ] **5.4** Test with FO4 ENB presets
- [ ] **5.5** Ship as "FalloutBridge" SKSE plugin

---

## Phase 6: RenderBridge Platform (6-8 weeks after Phase 5)

- [ ] **6.1** ReShade addon interface (Core → ReShade API)
- [ ] **6.2** Scene Inference Engine v0.1 (game-agnostic state detection)
- [ ] **6.3** Data-driven shader patch system (INI-defined modifications)
- [ ] **6.4** Public API specification for third-party plugins
- [ ] **6.5** Multi-game testing (at least 3 DX11 titles)

---

## Decision Gates

| Gate | Criteria | Go/No-Go |
|------|----------|----------|
| After 1A | SetParameter works in-game | Must pass to continue |
| After 1C | 80%+ trackers produce valid data | Can ship alpha with known gaps |
| After 2D | At least 1 preset author finds it useful | Validates the concept |
| After 3A | Core builds without SKSE/CommonLibSSE | Ready for FO4 port |
| After 5.4 | FO4 bridge works with FO4 ENB | Validates cross-game |

## What NOT to Do (Scope Control)

- Do NOT re-add GPU pipeline (DXBCPatcher, GBufferManager, MaterialTracker) until Phase 6
- Do NOT attempt clustered Forward+ until after shipping
- Do NOT build ReShade addon until Core/Adapter split is clean
- Do NOT optimize performance until there are real users reporting perf issues
- Do NOT add new tracker domains — 22 is enough for v3 release
- Do NOT write new shader presets — focus on the data pipeline, let preset authors do shaders
