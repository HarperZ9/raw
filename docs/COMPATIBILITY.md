# Compatibility

Playground is designed for universal mod compatibility. It works identically on vanilla Skyrim SE + SKSE64 and on a fully modded 3000+ mod Wabbajack list. This document explains the architectural guarantees that make this possible.

## Design Principles

### 1. Read-Only Game Access

All 22 game-state trackers are strictly read-only. Playground reads engine state through CommonLibSSE-NG's documented API surface:

- **No memory patches** — Playground does not write arbitrary bytes to game memory
- **No form edits** — No ESP/ESM records are created, modified, or injected
- **No hook conflicts** — The core plugin uses only ENB's official callback API (ENBSetCallbackFunction) and IAT hooks on D3DCompile (for shader caching). No game function detouring
- **No exclusive resources** — All GPU resources (SRV slots t17-t26, compute UAVs) are only bound during ENB's own render passes and released immediately after

The only system that writes game state is WriteBackProcessor, which is INI-driven, off by default, and operates through CommonLibSSE's typed setters (not raw memory writes).

### 2. Self-Healing Fault Isolation

Every tracker runs in its own try/catch block with independent health tracking:

```
Error → consecutiveErrors++ → if >= 5 → auto-disable
                                         ↓
                               wait 300 frames (~5 sec)
                                         ↓
                                    retry once
                                         ↓
                               success → re-enable + log recovery
                               failure → back to disabled
```

A single crashing tracker (e.g., from a mod that removes expected forms) cannot cascade to other trackers or to the ENB data push. The remaining 21 trackers continue running normally.

### 3. SEH Protection

The entire per-frame update (`DoFrameUpdate`) is wrapped in Windows Structured Exception Handling (`__try/__except`). Access violations from:
- Null game singletons at the main menu
- Forms deleted by mods during gameplay
- Stale pointers during cell transitions

...are caught, logged, and the frame update is skipped. After 5 consecutive SEH exceptions, the frame update is paused entirely until the game world loads (kNewGame or kPostLoadGame), then automatically re-enabled.

### 4. NaN/Inf Sanitization

Every float4 parameter is sanitized before being pushed to ENB:

```cpp
if (!std::isfinite(v.x)) v.x = 0.0f;  // repeat for y, z, w
```

This prevents corrupt data from any source (mod-modified forms, uninitialized fields, division by zero in game code) from propagating to ENB shaders and causing visual artifacts.

### 5. Loading Screen Awareness

Cell-dependent trackers are automatically skipped during loading screens:

| Always Run | Skipped During Loading |
|---|---|
| Celestial, Atmosphere, Fog, Weather | Lights, ActorValues, Crosshair |
| Player, Camera, Interior, Shadow | Quest, Region, NPCDetect |
| Effects, Render, ImageSpace | |
| UIState, Equipment, Audio | |
| PerfMonitor, Scene | |

This prevents stale pointer derefs and wasted work during cell transitions, which is when most crashes occur in heavily modded setups.

## Mod Compatibility Matrix

### Fully Compatible (No Conflicts)

| Mod/Tool | Playground Behavior |
|---|---|
| **ENBSeries** (v0.4xx+) | Primary data path. Uses ENBSetParameter/GetParameter only |
| **Community Shaders** | Feature negotiation: defers lighting/shadows/materials to CS when detected |
| **ENB ParmLink** | Replaces ParmLink's functionality; if ParmLink is loaded, logs info and coexists |
| **NativeEditorID Fix** | Detects and uses its EditorID function; disables own EditorID cache |
| **po3_Tweaks** | Detects po3's EditorID cache; same behavior as NativeEditorID Fix |
| **SSE Engine Fixes** | No overlap. Engine patches are separate (BSSpinLock threshold only) |
| **Address Library** | Required dependency. Used via CommonLibSSE-NG |
| **SmoothCam / Better 3P** | No conflict. Reads camera state, doesn't modify it |
| **MCM Helper / SkyUI** | No overlap. No MCM menus |
| **Crash Logger / .NET Script Framework** | No conflict. SEH handling doesn't interfere |
| **RaceMenu / CBBE / bodyslide** | No overlap. Doesn't touch meshes or textures |
| **DynDOLOD / TexGen** | No overlap. Doesn't modify LOD or texture generation |
| **Open Cities / JK's Skyrim** | Cell structure changes don't affect Playground (uses singletons, not cell data directly) |
| **All Wabbajack modlists** | Compatible by design (read-only access, fault isolation) |

### Compatible with Warnings

| Mod/Tool | Notes |
|---|---|
| **ReShade** | Detected at runtime. Warning: ReShade's own SRV/RT hooks may interfere with Playground's t17-t26 injection. Testing recommended. Usually works fine |
| **Special K** | Detected at runtime. Warning: Shared swap chain hooks. HDR features may conflict if both attempt HDR output |
| **Steam/Discord/RTSS Overlays** | Detected at runtime. Info-level report only. These hook Present but don't interfere with Playground |

### Known Incompatibilities

| Mod/Tool | Issue | Mitigation |
|---|---|---|
| **DXVK** | Translates D3D11 to Vulkan. Playground's compute shaders (CS5.0) may not execute correctly under translation | Detected at runtime with Error-level report. Disable compute features if using DXVK |
| **Playground d3d11 proxy + ENB d3d11.dll** | Both are d3d11.dll wrappers. Only one can load | Don't install both. The SKSE plugin (Playground.dll) works fine with ENB's d3d11.dll |

## Feature Negotiation

Playground automatically detects overlapping plugins at `kPostLoad` and adjusts its behavior:

```
CompatDetect::Detect()
  ├─ NativeEditorID Fix found? → Use its GetEditorID(), skip Playground's EditorIDCache hooks
  ├─ po3_Tweaks found?         → Use its GetEditorID(), skip Playground's EditorIDCache hooks
  ├─ Community Shaders found?  → Defer: LightLimitFix, ScreenSpaceShadows, GrassLighting,
  │                               ExtendedMaterials, WaterBlending, TreeLODLighting, DynamicCubemaps
  ├─ ENBSeries found?          → Defer: PostProcessing (Playground uses ENB's pipeline, not its own)
  └─ Playground Proxy found?   → Enable proxy-side features (CB tracking, phases, HDR)
```

Features can be force-enabled/disabled via `CompatDetect::ForceEnable(feature, bool)` for advanced users.

## Runtime Diagnostics

The Diagnostics system provides three layers of compatibility verification:

### SystemHealth Monitor
Tracks 53 systems across 8 categories with per-frame health evaluation:
- **Green** — Initialized, running, no errors
- **Yellow** — Degraded (stale data, occasional errors, disabled)
- **Red** — Failed initialization or persistent errors

Visible in the DebugGUI "Diagnostics" tab (INSERT key).

### CompatibilityProbe
Runtime conflict detection that runs at startup and on-demand:
- Enumerates all loaded DLLs and matches against 23 known signatures
- Detects graphics overlays (Steam, Discord, RTSS, NVIDIA, Fraps)
- Verifies Playground proxy presence and ENB SDK version
- Reports SRV slot allocation
- Re-probe button in DebugGUI for runtime re-checks

### ProxyDiagnostics
Bridges d3d11 proxy statistics to the DebugGUI:
- Draw calls, RT switches, shader changes per frame
- CB dirty tracking efficiency (% of GPU uploads skipped)
- State cache redundancy filtering (% of redundant state changes eliminated)
- Render phase identification
- Material pipeline status

## Resource Slots

Playground claims the following D3D11 shader resource slots during ENB passes only:

| Slot | System | Format | Notes |
|---|---|---|---|
| t17 | LuminanceHistogram | R32_FLOAT 256x1 | GPU-parallel histogram |
| t18 | LUTManager | R8G8B8A8 64^3 | Film color grading Texture3D |
| t19 | HiZPyramid | R32_FLOAT mipped | Hierarchical depth buffer |
| t20 | GTAORenderer | R16_FLOAT | Ambient occlusion |
| t21 | SSRRenderer | R16G16B16A16_FLOAT | Screen-space reflections |
| t22 | TAAManager | R16G16B16A16_FLOAT | Temporal anti-aliasing |
| t26 | SSGIRenderer | R16G16B16A16_FLOAT | Screen-space global illumination |
| s2 | LUTManager | Trilinear clamp | LUT sampler |
| s3 | TAAManager | Linear clamp | TAA sampler |

These slots are:
- Only bound during ENB shader passes (SRVInjector checks pass state)
- Released immediately after ENB's pipeline completes
- Not used by any known ENB preset, Community Shaders, or other mod

## Hook Points

Playground uses minimal, non-conflicting hook points:

| Hook | Method | Purpose |
|---|---|---|
| ENB callback | ENBSetCallbackFunction (official API) | Per-frame data collection |
| D3DCompile IAT | Import address table redirect | Shader caching + compilation diagnostics |
| BSShader vtables | Virtual method replacement (4 methods) | Scene observation (material counting) |
| D3D11 Present | Window message hook (WndProc) | ImGui debug overlay input |

None of these conflict with other SKSE plugins, ENB, or game patches. The BSShader vtable hooks call the original methods after Playground's observation code runs.

## Troubleshooting

### Tracker Disabled Warning
If you see "Playground: [tracker] auto-disabled" in the SKSE log, a mod is causing that tracker to throw exceptions. The tracker will automatically retry. If it consistently fails, a specific mod is likely modifying the forms that tracker reads. Use the Diagnostics tab to identify which tracker and check your mod list.

### No ENB Data
If SetParameter returns 0, check:
1. ENB SDK version (must be 1000+, ideally v504/SDK 1002)
2. Shader UIWidget annotations (required for ENB to register parameters)
3. Shader name casing (must be UPPERCASE: "ENBEFFECT.FX")

### Debug GUI Not Appearing
Press INSERT. If nothing appears:
1. Check SKSE log for "D3D11 hook initialized"
2. Verify ImGui is rendering (try Shift+Enter for ENB editor, then INSERT)
3. Check for conflicting ImGui hooks from other mods

### Performance Impact
Playground's core (trackers + ENB push) adds <0.1ms per frame. GPU compute features (GTAO, SSR, SSGI, etc.) add variable cost depending on resolution and settings. Each feature can be individually disabled. The FeatureManager + PerfMonitor automatically scales quality if GPU budget exceeds 95%.
