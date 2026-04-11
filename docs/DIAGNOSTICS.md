# Diagnostics System

Playground includes a unified diagnostics system for monitoring all 53 subsystems, detecting mod conflicts, and surfacing proxy-side performance data. Everything is visible in the DebugGUI "Diagnostics" tab (INSERT key).

## Components

### 1. SystemHealth Monitor

**File:** `src/core/SystemHealth.h/cpp`

Per-system heartbeat tracking with three severity levels:

| Status | Meaning | Criteria |
|---|---|---|
| Green | Healthy | Initialized + enabled + heartbeating + no errors |
| Yellow | Degraded | Stale (>120 frames without heartbeat), disabled, or has warnings/errors |
| Red | Failed | Init failed, or 10+ cumulative errors |

**53 systems across 8 categories:**

| Category | Systems |
|---|---|
| Tracker (22) | Celestial, Atmosphere, Fog, Weather, Player, Camera, Interior, Shadow, Effects, Render, ImageSpace, Lights, ActorValues, Crosshair, Equipment, Quest, UIState, Region, Audio, NPCDetect, PerfMonitor, Scene |
| Backend (11) | ShaderCache, FeedbackProcessor, WriteBackProcessor, SharedMemoryBridge, WeatherParamComputer, WeatherSeparation, ParameterBinding, ExternBinding, ShaderPreProcessor, ENBGuiIntegration, WeatherEditor |
| Integration (5) | SceneObserver, EngineFixes, PapyrusBridge, EditorIDCache, CompatDetect |
| Pipeline (5) | D3D11Hook, ShaderDebug, ComputeManager, SRVInjector, RenderPipeline |
| Compute (4) | LuminanceHistogram, HiZPyramid, LUTManager, TAAManager |
| Rendering (12) | ToneMapManager, AtmosphereRenderer, MaterialClassifier, GTAORenderer, SSRRenderer, SDSMCascades, SSGIRenderer, VolumetricClouds, FrameGenerator, TemporalSuperRes, MotionVectorGen, ClusteredLighting |
| Debug (2) | DebugRenderer, PerfMonitor |
| Proxy (1) | ProxyDiagnostics |

**API:**
```cpp
auto& sh = SB::SystemHealth::Get();

// Registration (once at startup)
uint32_t id = sh.Register("MySystem", SB::SystemCategory::Backend);

// Per-frame updates
sh.SetInitialized(id, true);
sh.Heartbeat(id);           // proves system is alive
sh.ReportError(id, "null pointer in Update()");
sh.ReportWarning(id, "stale data");

// Query
sh.EvaluateAll();           // recomputes all statuses
sh.GetGreenCount();         // healthy system count
sh.GetSystem(id).status;    // HealthStatus::Green/Yellow/Red
```

### 2. CompatibilityProbe

**File:** `src/core/CompatibilityProbe.h/cpp`

Runtime conflict detection. Runs once at startup after all systems initialize, and can be re-triggered via the DebugGUI button.

**Probes:**

| Probe | What It Checks |
|---|---|
| LoadedModules | Enumerates all DLLs via EnumProcessModules, matches 23 known signatures |
| Overlays | Steam, Discord, RTSS, NVIDIA overlays (by GetModuleHandle) |
| ENBVersion | ENBGetSDKVersion export, checks range 1001-1002 |
| HookChain | Verifies SB proxy exports (PG_GetProxyInterface), D3DCompiler presence |
| SRVSlots | Documents SB's t17-t26 allocation |
| SKSEPlugins | Community Shaders, ENB Helper SE, ENB ParmLink |

**Severity levels:**
- **Info** — Detected, no conflict (e.g., Steam Overlay)
- **Warning** — Potential conflict, usually harmless (e.g., ReShade, Special K)
- **Error** — Active conflict, likely to cause issues (e.g., DXVK)

**API:**
```cpp
auto& cp = SB::CompatibilityProbe::Get();
cp.RunProbe();

for (auto& c : cp.GetConflicts()) {
    // c.source, c.target, c.severity, c.detail
}
for (auto& m : cp.GetModules()) {
    // m.name, m.path, m.sizeKB, m.isKnown, m.description
}
```

### 3. ProxyDiagnostics Bridge

**File:** `src/core/ProxyDiagnostics.h/cpp`

Reads d3d11 proxy statistics each frame via the ProxyInterface export. No proxy header dependency — uses a minimal struct mirror for ABI compatibility.

**Data surfaced:**

| Category | Fields |
|---|---|
| Frame Stats | drawCalls, rtSwitches, shaderChanges, frameCount |
| HDR | hdrCapable, hdrEnabled, hdrMaxNits, hdrPaperWhite, backbufferFormat |
| CB Dirty Tracking | cbMaps, cbSkipped, cbCommitted, cbTracked, cbSaveRate% |
| State Cache | srvRedundant/Total, blendRedundant/Total, dsRedundant/Total, rsRedundant/Total, totalSaveRate% |
| Occlusion Culling | occTested, occCulled, occCullRate% |
| Render Phase | phaseName, phaseId (9 phases) |
| Material Pipeline | matActive, matPatched, matCandidates, matClassified, deferredActive |

**API:**
```cpp
auto& pd = SB::ProxyDiagnostics::Get();
pd.Connect();   // once at init
pd.Update();    // per-frame

auto& snap = pd.GetSnapshot();
// snap.drawCalls, snap.cbSaveRate, snap.occCullRate, etc.
```

## DebugGUI Integration

The "Diagnostics" tab (15th tab, accessed via INSERT key) displays:

1. **Health Summary Bar** — Color-proportional bar showing green/yellow/red ratio across all 53 systems
2. **Per-Category Sections** — Collapsible tree nodes grouped by SystemCategory, each showing:
   - Status badge ([G]reen/[W]arning/[X] failed)
   - System name
   - Error count
   - Diagnostic message
3. **Proxy Dashboard** — Frame stats, CB tracking with progress bar, state cache redundancy breakdown, occlusion culling stats, HDR state, material pipeline
4. **Compatibility Reports** — Color-coded conflict reports with severity indicators, re-probe button, loaded module list (known modules highlighted)

## Log Output

At startup (kDataLoaded), SystemHealth logs:
```
Playground: SystemHealth — 48 green, 3 yellow, 2 red of 53 systems
Playground: CompatProbe: 127 modules, 8 reports (5 info, 2 warn, 1 error)
```

Periodic health reports continue every 18000 frames (~5 minutes at 60fps) if any tracker has errors.

## Adding New Systems

To add diagnostics coverage to a new system:

```cpp
// In main.cpp, add to RegisterAllSystemHealth():
static uint32_t s_shID_mySystem = 0;
s_shID_mySystem = sh.Register("MySystem", SB::SystemCategory::Rendering);

// In kDataLoaded, after initialization:
sh.SetInitialized(s_shID_mySystem, SB::MySystem::Get().IsInitialized());

// In DoFrameUpdate, per-frame:
sh.Heartbeat(s_shID_mySystem);
// or on error:
sh.ReportError(s_shID_mySystem, "description");
```

The system will automatically appear in the Diagnostics tab under its category.
