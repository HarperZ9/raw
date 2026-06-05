# RAW — Project Lineage (Canon)

> The canonical record of the rendering lineage that produced RAW. One evolving
> D3D11 post-processing line: **APEX -> SkyrimBridge (SKSE) -> Playground -> RAW**.
> APEX was the early prototype; RAW is the current and most modern generation.
> Authored throughout by Zain Dana Harper. All RAW code is clean-room original
> (MIT) -- inspired by published techniques, derived from no third-party source.
> Generated 2026-06-05; specifics carry confidence labels where they came from
> archaeology rather than direct verification.

---

## The line, in one breath

Each generation kept the idea and discarded the scaffolding:

1. **APEX** proved you could own the frame from a `d3d11.dll` proxy and inject a
   post-process stack with an in-game UI.
2. **SkyrimBridge** proved you could lift live engine state (weather, celestial,
   camera, player) out of Skyrim per frame and feed it to shaders.
3. **Playground** fused the two -- stopped being a guest of ENB and became the host:
   a pipeline-owning proxy with phase detection and mid-frame dispatch.
4. **RAW** kept Playground's architecture wholesale and modernized the form --
   modular `src/core` + `src/d3d11_proxy`, external-HLSL-only, clean depth model,
   complete dev-tooling.

---

## Generation 1 -- APEX (prototype)

- **Location:** `C:\Users\Zain\APEX` (kept as archive; not part of RAW's build).
- **Identity:** Standalone D3D11 post-processing injector for Bethesda games. A proxy
  `d3d11.dll` that forwards the real system DLL, VTable-hooks `IDXGISwapChain::Present`
  plus context methods, captures the final frame, and runs an ordered post-process stack
  with an ImGui menu. A separate SKSE bridge DLL supplied engine state. (confidence:
  moderate-high on identity; `APEX_DLL.cpp`/`APEX_Core.cpp` confirm the proxy shape.)
- **Era:** copyright 2024; bulk development late 2025; uploaded ~Apr 2026. (moderate)
- **Pioneered (carried forward in spirit):** the d3d11 proxy interception pattern;
  Present-hook frame capture; runtime shader compilation; in-game ImGui debug UI; config
  + preset persistence; debug visualization modes; a separate SKSE bridge for game state.
- **Non-transferable (superseded, and that is fine):** the unified single-file core
  (~8K LOC in one .cpp) and the post-process-only model. RAW owns the full pipeline
  mid-frame and is modular; APEX's monolith does not port and was never meant to.
- **IP:** clean. README credits Boris Vorontsov (ENB), the Community Shaders team, and
  Pascal Gilcher as conceptual inspiration; no third-party source was copied.

## Generation 2 -- SkyrimBridge (the SKSE generation)

- **Location:** `C:\Users\Zain\SKSE\PlaygroundOldSources` (SkyrimBridge v1-v3).
- **Identity:** An SKSE64 plugin that ran alongside ENB. It extracted ~70 float4
  engine-state parameters per frame (celestial, atmosphere, fog, weather, camera,
  player, interior, shadow) across ~10 tracker subsystems and pushed them into ENB
  shaders via `ENBSetParameter()`. ENB was still the rendering host -- this was a data
  bridge, not a pipeline owner. (confidence: high -- `SkyrimBridge`/`ENBSetParameter`
  strings verified on disk.)
- **Era:** v1.0.1 ~2026-02-22 -> v3 ~2026-02-27; final build ~2026-03-10. (moderate)
- **Pioneered (carried forward):** per-frame engine-state extraction -- the direct
  conceptual ancestor of RAW's `SceneObserver`, `SceneData`/`SceneMatrices`, and
  `WeatherParameterManager`. The insight that the renderer should be driven by live
  game state originates here.
- **Non-transferable (incompatible by design):** the ENB-as-host model. RAW replaces
  ENB and cannot coexist with it; the `ENBSetParameter` binding layer has no analogue in
  a pipeline-owning design and does not port.
- **IP:** clean. CREDITS acknowledge Boris Vorontsov, doodlum, and CommonLibSSE-NG.

## Generation 3 -- Playground (direct parent)

- **Location:** `C:\Users\Zain\SKSE\Playground` (RAW lives at `Playground\RAW\`).
- **Identity:** The pivot from guest to host. A d3d11 proxy + SKSE plugin that owns the
  whole frame: 9-phase render detection, mid-frame dispatch, HiZ depth, a fixed SRV slot
  map, compute infrastructure, and ~11-22 renderers. Replaces ENB and Community Shaders
  entirely. (confidence: high.)
- **Era:** v3 consolidation 2026-02-26 -> docs finalized 2026-03-14 -> restructure
  2026-04-11. A `Playground_Marty_Package` (v1.0.0, 2026-03-14) is a distribution/
  documentation sibling, not an ancestor.
- **Established (inherited by RAW wholesale):** proxy interception + 9-phase detection;
  mid-frame dispatch (`PhaseDispatcher` -> `RenderPipeline`); HiZ standard->reversed-Z;
  SRV slot conventions (t17-t38); `ComputeManager`/`SRVInjector`; INI config with dirty
  tracking; the renderer roster (GTAO, SSR, Bloom, ToneMap, Contact Shadows, Skylighting,
  SSGI, Clustered Lighting, Atmosphere, Material Classification, ...); the ImGui overlay.
- **Relationship to RAW:** RAW does not re-architect Playground -- it re-forms it. Same
  patterns; cleaner module boundaries (`src/core` + `src/d3d11_proxy`), external-HLSL
  only, and the depth model rebuilt (see below).
- **IP:** the Playground source and shaders are clean-room original (MIT, Zain Harper
  headers; `.fx` files reference ENB algorithms by name but copy no code). The research
  folders are NOT -- see "Non-transferable for IP reasons" below.

## Generation 4 -- RAW (current)

- **Location:** `C:\Users\Zain\SKSE\Playground\RAW` -- own CMakeLists; build from here.
- **Identity:** The modern generation. `src/core` (~40 modules) + `src/d3d11_proxy`
  (COM-wrapped proxy). External-HLSL-only (86 shaders, F12 hot-reload). Complete
  dev-tooling (in-game shader-error overlay, source viewer, GPU profiler, effect
  visualizer). See `STATUS.md` for the concreteness heatmap and current plan.
- **Net-new vs Playground:** depth acquired zero-copy via typeless format upgrade
  (`WrappedDevice::CreateTexture2D`), not DSV ownership (see `ARCHITECTURE.md`);
  external-HLSL compute model replacing embedded `.fx` stages; modular renderer files;
  the developer-tooling loop built out to production grade.

---

## What carried forward (the canon)

| Idea | Born in | Lives in RAW as |
|------|---------|-----------------|
| d3d11 proxy + Present-hook frame ownership | APEX | `src/d3d11_proxy/*`, `D3D11Hook` |
| In-game ImGui debug UI + runtime shader compile | APEX | `DebugGUI`, `ShaderLoader`, `SB_ShaderDebug` |
| Config + preset persistence | APEX | `ConfigManager` |
| Per-frame engine-state extraction | SkyrimBridge | `SceneObserver`, `SceneData`, `WeatherParameterManager` |
| Pipeline ownership + 9-phase detection | Playground | `RenderPhaseDetector`, `PhaseDispatcher` |
| Mid-frame dispatch + pass registry | Playground | `RenderPipeline`, `RenderPassManager` |
| HiZ reversed-Z + SRV slot map (t17-t38) | Playground | `HiZPyramid`, `SRVInjector`, `ARCHITECTURE.md` |
| Compute infrastructure | Playground | `ComputeManager` |
| Renderer roster (GTAO/SSR/Bloom/...) | Playground | `src/core/*Renderer.cpp` |

## What is non-transferable (and that is alright)

**Technical / architecturally incompatible -- superseded, not portable:**
- APEX's unified single-file core and post-process-only model.
- SkyrimBridge's ENB-as-host `ENBSetParameter` binding (RAW replaces ENB; mutually exclusive).
- Embedded `.fx` ENB-stage shader framework assumptions (RAW is external-HLSL compute).

**Non-transferable for IP reasons -- archived separately, NEVER enter RAW or a release:**
These contain proprietary reversed third-party material. They are preserved for the
operator's private reference only, outside the clean-room tree. See the archive manifest
at `SKSE\_LINEAGE_ARCHIVE\PROPRIETARY_ARCHIVE_MANIFEST.md`.
- `Playground\ENB Binary Dump\` -- reverse-engineered ENBSeries binaries (Boris Vorontsov).
- `Playground\ENB Forum Archives\` -- third-party forum/preset content (others' IP).
- `Playground\Engine Resources Skyrim\` and `...FO4\` -- Bethesda `SkyrimSE.exe` + Ghidra
  disassembly artifacts (proprietary engine).
- `Playground\FULL_SOURCE.txt` -- mixed concatenation; audit before any reuse.
- `Playground_Marty_Package\Inspiration and Techniques\` and the "Marty" branding -- the
  code is original, but the name and research framing imply derivation from Pascal
  Gilcher's iMMERSE. Keep both out of RAW's canon; RAW stands on its own clean-room work.

---

## Provenance statement (for any future release)

RAW is the original work of Zain Dana Harper, MIT-licensed, built clean-room. It draws on
published, publicly-described rendering techniques (GTAO/Jimenez, SSR/McGuire & Mara,
bloom/Karis-Jimenez, AgX, voxel cone tracing, etc.) and acknowledges Boris Vorontsov, the
Community Shaders team, and Pascal Gilcher as conceptual influences. It contains no source
code from ENBSeries, ReShade/iMMERSE, Community Shaders, or the Creation Engine. The
reverse-engineering archives above are research inputs, not ingredients, and are excluded
from the distributable tree.
