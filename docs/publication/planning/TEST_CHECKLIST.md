# SkyrimBridge v3.0 Test Checklist

## Phase 1 Consolidation - Verification

### Build Artifacts
- [x] SkyrimBridge_v3.dll compiles successfully
- [x] SkyrimBridge_v3.lib generated
- [x] config/WeatherParams.ini copied to build output

### Installation Test
- [ ] Copy SkyrimBridge_v3.dll to `Data/SKSE/Plugins/`
- [ ] Copy config folder to `Data/SKSE/Plugins/`
- [ ] Copy shader/*.fx to ENB shader directory
- [ ] Copy shader/Helper/*.fxh to ENB Helper directory
- [ ] Verify no file conflicts with existing installations

### Shader Files Verification (20 files merged)
#### Main Shaders (8 .fx files)
- [ ] enbadaptation.fx loads without errors
- [ ] enbbloom.fx loads without errors
- [ ] enbdepthoffield.fx loads without errors
- [ ] enbeffect.fx loads without errors
- [ ] enbeffectpostpass.fx loads without errors
- [ ] enbeffectprepass.fx loads without errors
- [ ] enblens.fx loads without errors
- [ ] enbsunsprite.fx loads without errors

#### Helper Headers (12 .fxh files)
- [ ] SkyrimBridge.fxh (v3.0.0) - core data bridge included correctly
- [ ] Effect_AtmosphericFog.fxh - no compilation errors
- [ ] Effect_CinematicFX.fxh - no compilation errors
- [ ] Effect_CRTShader.fxh - no compilation errors
- [ ] Effect_ProceduralLensDirt.fxh - no compilation errors
- [ ] Effect_ProceduralWeatherFX.fxh - no compilation errors
- [ ] enbUI_CinematicFX.fxh - UI parameters visible in ENB GUI
- [ ] enbUI_CRT.fxh - UI parameters visible in ENB GUI
- [ ] enbUI_DepthOfField.fxh - UI parameters visible in ENB GUI
- [ ] enbUI_Fog.fxh - UI parameters visible in ENB GUI
- [ ] enbUI_Lens.fxh - UI parameters visible in ENB GUI
- [ ] enbUI_PostPass.fxh - UI parameters visible in ENB GUI

---

## Layer 1: Debug GUI Tests
- [ ] Shift+F12 opens ImGui debug overlay
- [ ] All 10 trackers display data in overlay:
  - [ ] Celestial: sun/moon angles, time of day
  - [ ] Atmosphere: ambient colors, sky colors
  - [ ] Fog: distances, colors, density
  - [ ] Weather: current weather ID, transition progress
  - [ ] Player: health, magicka, stamina, combat state
  - [ ] Camera: position, rotation, FOV
  - [ ] Interior: interior flag, lighting template
  - [ ] Shadow: shadow quality, cascade distances
  - [ ] Effects: active effects, duration
  - [ ] Render: resolution, framerate

---

## Layer 2: Data Trackers Tests
- [ ] Data updates in real-time (~60fps)
- [ ] No significant FPS impact (<1% performance drop)
- [ ] Interior/exterior transition detection works
- [ ] Combat state detection accurate
- [ ] Health/magicka/stamina values accurate

---

## Layer 3: ENB Integration Tests
- [ ] SkyrimBridge.fxh included without errors
- [ ] `#define SKYRIMBRIDGE_FXH` activates bridge code
- [ ] ENB_GetParameter reads float4 values correctly
- [ ] SHADERGROUP 99 UI sections visible in ENB menu

### Parameter Categories (19 domains)
- [ ] Celestial data readable (SB_Celestial_Sun, SB_Celestial_Moon, etc.)
- [ ] Atmosphere data readable (SB_Atmo_SunDir, SB_Atmo_Ambient, etc.)
- [ ] Fog data readable (SB_Fog_Near, SB_Fog_Far, etc.)
- [ ] Weather data readable (SB_Weather_Current, SB_Weather_Trans)
- [ ] Player data readable (SB_Player_Stats, SB_Player_Combat, etc.)
- [ ] Camera data readable (SB_Camera_Pos, SB_Camera_Dir, etc.)
- [ ] Interior data readable (SB_Interior_Flags, SB_Interior_Light)
- [ ] Shadow data readable
- [ ] Effects data readable
- [ ] Render data readable

---

## IMPROVED Shader Feature Tests

### SkyrimBridge Combat Integration
- [ ] Combat state affects DOF intensity (enbdepthoffield.fx)
- [ ] Combat state affects lens effects (enblens.fx)
- [ ] Combat state affects bloom behavior (enbbloom.fx)

### SkyrimBridge Weather Integration
- [ ] Weather affects fog density (enbeffect.fx)
- [ ] Weather affects bloom intensity (enbbloom.fx)
- [ ] Weather transitions are smooth

### SkyrimBridge Interior/Exterior Integration
- [ ] Interior detection works
- [ ] Interior lighting adaptation activates
- [ ] Exterior-specific effects disable indoors

### SkyrimBridge Health Feedback (if enabled)
- [ ] Low health affects screen effects
- [ ] Critical health triggers visual warning

---

## Performance Tests
- [ ] No shader compilation errors in ENB log
- [ ] Framerate stable (compare with/without SkyrimBridge)
- [ ] No memory leaks (extended play session)
- [ ] Fast travel doesn't cause issues
- [ ] Cell transition smooth

---

## Known Issues to Watch
1. 62 of 105 parameters pushed but not consumed (documented, not a bug)
2. Layer 4 (Render Pipeline) disabled - API mismatch with ENB 0.5xx
3. Layer 5 (Occlusion) not implemented

---

## Test Environment
- Skyrim SE/AE version: ___________
- ENB version: ___________
- SkyrimBridge_v3 version: 3.0.0
- Date tested: ___________
- Tester: ___________

---

## Phase 1 Sign-off
- [ ] All critical tests pass
- [ ] No game-breaking bugs
- [ ] Ready to proceed to Phase 2

Signed: _________________________ Date: _____________
