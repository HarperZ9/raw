# Playground v3 — Development Roadmap

## Path A: Ship Playground, Then Expand
Finish the core product, get real users, build reputation, then evolve into RenderBridge.

---

## Phase 1: Playground v3 Release (Target: 2-4 weeks)

### Milestone 1A: Core Data Pipeline Verification (BLOCKING — do first)

Everything depends on ENBSetParameter actually delivering data to shaders.

- [ ] **1A.1** Launch game, check SKSE log for SetParameter results
  - Expected: `"first push — X succeeded, 0 failed"`
  - If failures: debug with log output (shader names, param names, ENB state flag)
  - Log location: `Documents/My Games/Skyrim Special Edition/SKSE/Playground_v3.log`

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
- [ ] **1B.2** Open ENB editor (Shift+Enter), look for "Playground" bar in bar list
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
- [ ] **1D.7** SharedMemoryBridge: verify Playground_ENB.dllplugin reads shared memory
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
- [ ] **1F.6** Deploy.bat should copy: DLLs -> SKSE/plugins/, configs -> SKSE/plugins/Playground/, .dllplugin -> ROOT/
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

- [ ] **2D.1** Package Playground v3 alpha for 2-3 ENB preset authors
- [ ] **2D.2** Write quick-start guide (install, verify, use SB_ params in shaders)
- [ ] **2D.3** Collect feedback on: param naming, value ranges, missing data
- [ ] **2D.4** Iterate based on feedback (add/remove/rename params)

---

## Phase 3: Developer Tools + Shader Capture (2-3 weeks, can overlap Phase 2)

### Milestone 3A: Shader Source Capture Pipeline

Leverage existing D3DCompile hooks to capture and diff ENB's shader modifications.

- [ ] **3A.1** Add capture-to-disk mode in SB_ShaderDebug (save pSrcData to `ShaderCapture/{hash}.hlsl`)
- [ ] **3A.2** Capture vanilla baseline (ENB removed) — store BSShader bytecode by hash
- [ ] **3A.3** Capture ENB-modified shaders — store both HLSL source and compiled DXBC
- [ ] **3A.4** Build diff tool (compare vanilla vs ENB-modified shader assembly)
- [ ] **3A.5** Document ENB's slot usage from captured diffs (which CB/SRV/UAV slots ENB claims)

### Milestone 3B: Developer Tools (built into DebugGUI)

These tools leverage existing infrastructure with minimal new code.

- [ ] **3B.1** Shader Source Viewer: ImGui tab displaying captured D3DCompile source by shader name
- [ ] **3B.2** Weather Transition Analyzer: rolling sparkline graphs of all weather/atmosphere data over time
- [ ] **3B.3** EditorID Cross-Reference: crosshair target -> FormID/EditorID/mod lookup via EditorIDCache
- [ ] **3B.4** Light Inspector: ImGui panel showing all tracked lights with position, color, radius, shadow status

---

## Phase 4: Codebase Factoring + Documentation (2-3 weeks after Phase 3)

### Milestone 4A: Core/Adapter Split

- [ ] **4A.1** Define `IRenderBridgeCore` interface (shader hooks, cache, D3DCompile)
- [ ] **4A.2** Define `IGameStateProvider` interface (tracker data -> AllData)
- [ ] **4A.3** Move game-agnostic code to `src/core/renderbridge/`
- [ ] **4A.4** Move game-specific code to `src/skyrim/`
- [ ] **4A.5** Separate CMake targets: `renderbridge_core.lib` + `skyrimbridge.dll`

### Milestone 4B: Documentation + Nexus Release

- [ ] **4B.1** API reference for shader authors (SB_ param list, types, ranges)
- [ ] **4B.2** Integration guide for ENB preset authors
- [ ] **4B.3** Architecture document (for future contributors)
- [ ] **4B.4** Config file reference (all INI options documented)
- [ ] **4B.5** Nexus Mods page for Playground
- [ ] **4B.6** Nexus Mods page for Playground DevKit (tools as optional install)

---

## Phase 5: Fallout 4 Port (4-8 weeks after Phase 4)

The only realistic cross-game target. Same Creation Engine, established F4SE + CommonLibF4.

- [ ] **5.1** Set up F4SE + CommonLibF4 build environment
- [ ] **5.2** Port tracker interfaces (FO4 equivalents of RE:: singletons)
- [ ] **5.3** Adapt BridgeData for FO4 game objects (weapons, settlements, power armor, radiation)
- [ ] **5.4** Test with FO4 ENB presets
- [ ] **5.5** Ship as "FalloutBridge" F4SE plugin

---

## Phase 6: RenderBridge Platform (long-term, after user validation)

Only pursue after Playground has real users and proven value. Each sub-item is a separate project.

- [ ] **6.1** Data-driven shader patch system (INI-defined DXBC modifications)
- [ ] **6.2** ReShade addon interface (Core -> ReShade API, multi-game injection)
- [ ] **6.3** Forward-pass re-enablement (DXBCPatcher, GBufferManager for PBR GBuffer)
- [ ] **6.4** Public API specification for third-party plugins

---

## Decision Gates

| Gate | Criteria | Go/No-Go |
|------|----------|----------|
| After 1A | SetParameter works in-game | Must pass to continue |
| After 1C | 80%+ trackers produce valid data | Can ship alpha with known gaps |
| After 2D | At least 1 preset author finds it useful | Validates the concept |
| After 4A | Core builds without SKSE/CommonLibSSE | Ready for FO4 port |
| After 5.4 | FO4 bridge works with FO4 ENB | Validates cross-game |

## What NOT to Do (Scope Control)

- Do NOT re-add GPU pipeline (DXBCPatcher, GBufferManager, MaterialTracker) until Phase 6
- Do NOT attempt clustered Forward+ until after shipping
- Do NOT build ReShade addon until Core/Adapter split is clean
- Do NOT optimize performance until there are real users reporting perf issues
- Do NOT add new tracker domains — 24 is enough for v3 release
- Do NOT write new shader presets — focus on the data pipeline, let preset authors do shaders
- Do NOT build Scene Inference Engine, per-game adapters for non-Creation games, DX9 patcher, or runtime occlusion culling — these are multi-month research projects, not features
- Do NOT pursue "universal rendering middleware" identity until Playground ships and validates

## Deferred Vision Items (filed, not planned)

These ideas from the Strategic Vision document are technically interesting but premature:
- Scene Inference Engine (game-agnostic state from render pipeline observation)
- Clustered Forward+ with ENB compatibility
- Runtime occlusion culling / precombine replacement
- BRDF replacement in BSLightingShader
- True deferred lighting hybrid (MRT component separation)
- Navmesh overlay, collision visualizer, material audit tool
- Per-game adapters for non-Creation-Engine games (Cyberpunk, Elden Ring)
- DX9 bytecode patcher
