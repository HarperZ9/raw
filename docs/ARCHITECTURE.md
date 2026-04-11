# Architecture

## Overview

Playground v3 is an SKSE64 plugin that bridges Skyrim's game engine state to ENB shaders. The architecture is built around a single principle: **ENBSetParameter is the only data path to ENB shaders**. No D3D11 device or context hooks are used for data delivery.

## Two Binaries

### Playground.dll (SKSE plugin)

The main plugin. Loads via SKSE, accesses game state through CommonLibSSE-NG, and pushes ~150 float4 parameters per frame to ENB shaders via `ENBSetParameter`. Also writes to shared memory for external consumers.

**Dependencies:** CommonLibSSE-NG, Dear ImGui, d3d11, dxgi, d3dcompiler

### Playground_ENB.dllplugin (ENB external plugin)

A standalone DLL with zero game/SKSE dependencies. Reads the shared memory region written by the main plugin and serves parameters to ENB shaders via `ENBGetParameter` (pull model). This provides a secondary data path for ENB presets that prefer the plugin model.

**Dependencies:** Windows headers only (C++17)

## Data Flow

```
 Game Engine (CommonLibSSE)
         |
         v
 [24 Domain Trackers]  <-- Each reads specific engine subsystem
         |
         v
     AllData struct     <-- ~150 Float4 fields, 24 domain sub-structs
         |
    +----+----+----+
    |    |    |    |
    v    v    v    v
  ENB  Shared Papyrus  Debug
  Set  Memory Bridge    GUI
  Param
```

### Phase 1: Data Collection

24 trackers run every frame in the ENB `BeginFrame` callback. Each tracker reads a specific game subsystem:

| Domain | Tracker | Source |
|---|---|---|
| 1. Celestial | CelestialTracker | RE::Sky, RE::Sun, RE::Moon |
| 2. Atmosphere | AtmosphereTracker | RE::Sky::skyColor, TESWeather |
| 3. Fog | FogTracker | RE::TESWeather::fogData |
| 4. Weather | WeatherTracker | RE::Sky, RE::Precipitation |
| 5. Player | PlayerTracker | RE::PlayerCharacter |
| 6. Camera | CameraTracker | RE::PlayerCamera, NiCamera |
| 7. Interior | InteriorTracker | RE::TESObjectCELL, BGSLightingTemplate |
| 8. Shadow | ShadowTracker | RE::ShadowSceneNode |
| 9. Effects | EffectsTracker | RE::MagicTarget, ActiveEffect |
| 10. Render | RenderTracker | RE::BSGraphics::State, BSTimer |
| 11. ImageSpace | ImageSpaceTracker | RE::ImageSpaceManager |
| 12. Lights | LightTracker | RE::NiPointLight (4096u radius scan) |
| 13. ActorValues | ActorValueTracker | RE::Actor::GetActorValue |
| 14. Crosshair | CrosshairTracker | RE::CrosshairPickData |
| 15. Equipment | EquipmentTracker | RE::Actor::GetEquippedObject |
| 16. Quest | QuestTracker | RE::TESQuest iteration |
| 17. UIState | UIStateTracker | RE::UI::IsMenuOpen |
| 18. Feedback | FeedbackProcessor | GPU backbuffer readback |
| 19. Region | RegionTracker | RE::TESObjectCELL::GetRegionList |
| 20. Audio | AudioTracker | RE::BSAudioManager |
| 21. NPCDetect | NPCDetectTracker | ProcessLists, detection levels |
| 22. Performance | PerfMonitor | D3D11 timestamp queries |
| 23. Scene | SceneObserver | BSShader vtable hooks |
| 24. Theme | ENBGetParameter | Theme index from enbeffect.fx |

### Phase 2: Post-Processing

After tracker collection:

1. **FeedbackProcessor** — merges GPU readback data (1-frame delay) + ENBGetParameter readback slots
2. **WriteBackProcessor** — applies INI-driven rules to modify game state (FOV, fog, lighting)
3. **SanitizeAllData** — replaces NaN/Inf with 0 in every float4

### Phase 3: Data Distribution

1. **ENBSetParameter** — pushes each dirty float4 to all 9 target shaders (primary path)
2. **WeatherParameterComputer** — pushes weather-interpolated SB_WP_* parameters
3. **ExternBindingProcessor** — pushes Extender-compatible extern bindings (matrices, game state)
4. **WeatherSeparationEngine** — pushes per-weather per-ToD parameter overrides
5. **ParameterBindingEngine** — evaluates conditional visibility/readonly rules
6. **SharedMemoryBridge** — writes AllData to named mapped file (`Playground_GameState`)
7. **PapyrusBridge** — updates cached data for Papyrus script consumers
8. **ENBGuiIntegration** — updates AntTweakBar panels in ENB editor

## Initialization Order

### kPostLoad (SKSE message)
1. EditorIDCache::Install() — hooks form loading to cache editor IDs
2. ENBInterface::Init() — resolves ENB SDK functions from d3d11.dll
3. ENBGuiIntegration::Init() — resolves ATB functions for native GUI

### kPostPostLoad
4. SetCallbackFunction(OnENBFrame) — registers per-frame callback
5. DebugGUI::Init() — initializes ImGui state

### kDataLoaded
6. SharedMemoryBridge::Initialize()
7. D3D11Hook::Init() — SwapChain Present hook for ImGui overlay
8. FeedbackProcessor::Initialize() + LoadConfig()
9. WriteBackProcessor::LoadConfig()
10. ShaderCache::Initialize() — must precede ShaderDebug
11. ShaderDebug::Install() — IAT hooks D3DCompile/D3DCompile2
12. WeatherParameterComputer::Initialize()
13. WeatherSeparationEngine::Initialize()
14. PerfMonitor::Initialize()
15. SceneObserver::Install() — BSShader vtable hooks
16. PapyrusBridge::Register()

## D3D11 Hooks (minimal)

Only two D3D11-level hooks exist:

1. **SwapChain::Present** — vtable hook for ImGui overlay rendering and FeedbackProcessor backbuffer readback. Does NOT touch the rendering pipeline.

2. **D3DCompile / D3DCompile2** — IAT hooks for shader compilation interception. Drives the ShaderPreProcessor (annotation parsing), ShaderCache (bytecode caching), and ShaderDebug (error diagnostics).

No hooks on ID3D11DeviceContext (Draw, PSSetShaderResources, etc.) or ID3D11Device. The only data path to ENB shaders is ENBSetParameter.

## Self-Healing System

```cpp
struct TrackerHealth {
    int  consecutiveErrors;
    bool disabled;
    uint32_t disabledAtFrame;
    static constexpr int kDisableThreshold = 5;
    static constexpr uint32_t kRetryInterval = 300;  // ~5s at 60fps
};
```

Each tracker is wrapped in try/catch with independent health tracking. After 5 consecutive errors, the tracker auto-disables and retries every 300 frames. Recovery is logged. All 5 SceneObserver vtable hooks are additionally wrapped in SEH (`__try/__except`).

Cell-dependent trackers (Lights, ActorValues, Crosshair, Quest, Region, NPCDetect) are skipped during loading screens (detected via `RE::UI::IsMenuOpen(LoadingMenu)`).

## Dirty Tracking

ENBSetParameter is the primary bottleneck. With ~150 params x 9 shaders = ~1350 potential calls/frame, dirty tracking is critical:

```cpp
// In PushAllData: skip unchanged float4s
if (memcmp(&current[i], &previous[i], 16) == 0) continue;
```

This reduces actual calls to ~200/frame during normal gameplay (mostly camera matrices and time-varying values).

## Thread Model

All Playground code runs on the render thread:
- OnENBFrame callback (BeginFrame phase)
- BSShader vtable hooks (SetupMaterial/SetupGeometry — called during Draw)
- HookedPresent (after Present)

No synchronization primitives are needed between these paths. The `s_gameReady` atomic flag is the only cross-thread communication (main thread sets it on kDataLoaded, render thread reads it in OnENBFrame).
