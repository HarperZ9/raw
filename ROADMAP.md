# RAW — Development Roadmap

Copyright (c) 2026 Zain D. Harper (papacr0w). MIT-licensed — see `LICENSE`.

---

## Current State (2026-03-19)

RAW is a complete rendering infrastructure with 28 effect renderers, all shaders
rewritten from published papers. The infrastructure works (proxy, mid-frame dispatch,
pipeline orchestration). Effects compile but need in-game validation and tuning.

**What works:** Proxy, HiZ, linearized depth, phase detection, compositor pipeline,
config persistence, presets, 85 hot-reloadable shaders, GPU profiler, frame capture.

**What needs work:** Individual effect quality, developer tooling for rapid iteration,
in-game debugging without restart cycles.

---

## Phase 1: Developer Tools (NOW — 1-2 weeks)

The #1 bottleneck is the debug-fix-test cycle. Every shader change currently requires
either a DLL rebuild or a blind F12 reload with no feedback. Build the tools that
make everything else faster.

### 1A: Shader Error Overlay (CRITICAL)
Show shader compilation errors directly in-game so you never have to check log files.

- [x] **1A.1** On `ShaderLoader::Compile` failure, store error message + shader name
- [x] **1A.2** Render error list as ImGui overlay (red text, always-on-top)
- [x] **1A.3** Auto-clear errors on successful F12 reload
- [x] **1A.4** Show line numbers from D3DCompile error messages
- [x] **1A.5** Click error to copy shader name to clipboard

### 1B: Live Shader Source Viewer
View the HLSL source of any active shader in-game, with syntax highlighting.

- [x] **1B.1** ImGui tab: list all loaded shaders by name (Shaders tab)
- [x] **1B.2** Click shader to view source (from ShaderLoader cache)
- [x] **1B.3** Show which shaders loaded from disk vs embedded fallback (DISK/EMB labels)
- [x] **1B.4** Show compile time + bytecode size for each shader
- [x] **1B.5** "Open in Editor" button — launches external editor on the .hlsl file

### 1C: Per-Pass GPU Profiler (enhance existing)
The GPU profiler exists but needs polish.

- [x] **1C.1** Sort passes by GPU cost (heaviest first) — toggle checkbox
- [x] **1C.2** Color-coded bars (green < 1ms, yellow < 4ms, red > 4ms)
- [x] **1C.3** Show % of total frame budget per pass
- [x] **1C.4** Historical min/avg/max over last 5 seconds (hover tooltip)
- [x] **1C.5** "Disable" button next to each pass for A/B testing (X button per pass)

### 1D: Effect Output Visualizer
See what each effect actually outputs — the most important debug tool.

- [x] **1D.1** Debug view mode selector in compositor (Shaders tab > Debug Visualization)
- [x] **1D.2** Fullscreen modes: AO, GI(5x), SSR, clouds, shadows, skylighting
- [x] **1D.3** Split-screen comparison (left = without effects, right = with, yellow divider)
- [x] **1D.4** Depth buffer visualization (log-scaled linear depth at t7)
- [x] **1D.5** Normal buffer visualization (reconstructed from depth gradients, RGB-mapped)
- [x] **1D.6** Zero-output warnings when enabled effects have null SRV output

### 1E: Hot-Reload Pipeline (enhance F12)
Make F12 actually re-compile and swap shaders live, not just invalidate cache.

- [x] **1E.1** F12 invalidates ShaderLoader cache
- [x] **1E.2** Next frame: each renderer detects stale shader, recompiles from disk
- [x] **1E.3** On success: swap shader object live, show green notification
- [x] **1E.4** On failure: keep old shader, show red error overlay (1A)
- [x] **1E.5** Track which shaders changed on disk (file timestamp comparison)
- [x] **1E.6** Only recompile changed shaders (selective invalidation via last_write_time)

---

## Phase 2: Effect Quality (2-4 weeks after Phase 1)

With developer tools in place, systematically validate and tune each effect.
Do them in dependency order — compositor first, then effects that feed it.

### 2A: Core Effects (test one at a time)
- [ ] **2A.1** SceneCompositor — verify copy+blend roundtrip preserves scene
- [ ] **2A.2** GTAO — tune radius, intensity, step count for visible AO
- [ ] **2A.3** Contact Shadows — verify sun direction, tune ray length
- [ ] **2A.4** Skylighting — verify hemisphere sampling produces variation
- [ ] **2A.5** Bloom — verify threshold, mip chain, composite blend

### 2B: Advanced Effects
- [ ] **2B.1** SSR — verify ray hits, tune thickness/distance
- [ ] **2B.2** SSGI — verify voxel grid, check for race condition artifacts
- [ ] **2B.3** ToneMapping — A/B test all 12 curves, pick best default
- [ ] **2B.4** DoF — verify autofocus, tune bokeh quality
- [ ] **2B.5** VolumetricClouds — verify noise generation, tune density

### 2C: Polish
- [ ] **2C.1** Default preset that looks good out of the box
- [ ] **2C.2** Performance profiling — identify and fix GPU-heavy passes
- [ ] **2C.3** Temporal stability — verify no ghosting/flickering
- [ ] **2C.4** Edge cases — loading screens, cell transitions, menus

---

## Phase 3: Stability & Distribution (2-3 weeks after Phase 2)

### 3A: Robustness
- [ ] **3A.1** Extended play testing (1+ hour sessions)
- [ ] **3A.2** Save/load cycle stability
- [ ] **3A.3** Interior/exterior transition stability
- [ ] **3A.4** Memory leak detection (watch VRAM over time)
- [ ] **3A.5** Compatibility testing with common SKSE plugins

### 3B: Packaging
- [ ] **3B.1** FOMOD installer with preset selection
- [ ] **3B.2** User documentation (install guide, hotkey reference, FAQ)
- [ ] **3B.3** Nexus Mods page
- [ ] **3B.4** Screenshot comparison gallery (vanilla vs RAW)

---

## Phase 4: Expansion (after stable release)

### 4A: Additional Effects
- [ ] **4A.1** Motion blur (camera + per-object)
- [x] **4A.2** Atmospheric scattering — AtmosphereRenderer (Bruneton/Hillaire, LUT precompute)
- [x] **4A.3** Screen-space SSS — SubsurfaceScatteringRenderer (Burley diffusion, skin+foliage)
- [x] **4A.4** Volumetric lighting — VolumetricLightingRenderer (HG phase, Beer-Lambert, bilateral upsample)
- [x] **4A.5** Temporal super-resolution — TemporalSuperRes (Lanczos-2, AABB clip, material-aware CAS)

### 4B: Platform Features
- [ ] **4B.1** Preset sharing (export/import RAW_Preset.ini)
- [ ] **4B.2** Screenshot comparison mode (toggle effects with hotkey)
- [ ] **4B.3** Performance mode auto-detection (lower settings at low FPS)
- [ ] **4B.4** HDR10 output support (PQ encoding already implemented)

### 4C: Fallout 4 Port
- [ ] **4C.1** F4SE + CommonLibF4 build environment
- [ ] **4C.2** Port proxy (same D3D11 API, different game)
- [ ] **4C.3** Adapt SceneData for FO4 camera/rendering
- [ ] **4C.4** Ship as "RAW for Fallout 4"

---

## Decision Gates

| Gate | Criteria | Action |
|------|----------|--------|
| After 1E | Can edit shader, hit F12, see result in <2 seconds | Move to Phase 2 |
| After 2A | GTAO + Shadows + Sky look clean with no artifacts | Move to 2B |
| After 2C | Default preset looks better than vanilla | Move to Phase 3 |
| After 3A | 1 hour play session, zero crashes | Move to 3B |
| After 3B | Nexus page live | Move to Phase 4 |

## What NOT to Do

- Do NOT add more effects until existing ones look right
- Do NOT port to other games until Skyrim version is stable
- Do NOT build a "platform" or "SDK" — build a mod that works
- Do NOT optimize for performance until there are visible results to optimize
- Do NOT write more shader code without testing the last batch first

---

## ⚠ Ground-Truth Status (2026-06-05 audit) — supersedes conflicting statements above

A full-codebase audit found the "Current State" summary above (dated 2026-03-19) overstates
build status. Corrected, verifiable facts:

| Claim above | Ground truth | Evidence |
|---|---|---|
| "28 effect renderers … Effects compile" | ~28 renderers are *authored*, but **17 are excluded from the build** | `CMakeLists.txt` lines 93–109 (commented `# src/core/*.cpp`) |
| Phase 4A.2–4A.5 marked `[x]` (Atmosphere, SSS, VolLight, TSR) | Implemented in *source* only — **all four are build-excluded**, none validated in-game | same CMake block; `main.cpp` `// [DISABLED]` init guards |
| (implied) all effects use external HLSL | **TemporalSuperRes embeds its shaders as C++ strings** (`kUpscaleCS`, `kSharpenPS`) — buildable, but violates the "external HLSL only" convention | `TemporalSuperRes.cpp:85,267` |

**Accurate taxonomy of the ~28 renderers:**
- **~6 enabled by default** ("Active Effects" in README) — GTAO, Contact Shadows, Skylighting,
  SSR, SSGI, Scene Compositor, plus the post chain (Bloom/Color/ToneMap/TAA/Histogram) built but default-off.
- **~11 compile** in the current build.
- **17 excluded from the build** (source preserved): SDSM, GrassLighting, TreeLOD, WaterBlending,
  DynamicCubemap, VolumetricClouds, VolumetricLighting, SubsurfaceScattering, IndirectSpecular,
  ScreenSpaceDecal, ParticleLighting, DoF, Lens, Underwater, Atmosphere, FrameGenerator, TemporalSuperRes.

`[x]` in this roadmap means **implemented in source**, NOT *in the build* or *validated in-game*.

**Depth blocker (historical):** resolved via typeless-format-upgrade interception
(`WrappedDevice::CreateTexture2D`), not DSV ownership — see `ARCHITECTURE.md` › "Depth Acquisition".
The `DepthOwnership.cpp` DSV-substitution path is disabled dead logic.

See `STATUS.md` for the full per-module concreteness heatmap and the current plan of action.
