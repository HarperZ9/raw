# RAW — Ground-Truth Status, Concreteness Heatmap & Plan

> Single source of truth. Generated 2026-06-05 from a full-codebase audit
> (56K LOC C++ across ~60 modules, 10K LOC HLSL across 86 shaders, 6 parallel
> module-cluster audits, each claim re-verified against source before inclusion).
> "Concreteness" = how much real, load-bearing engineering exists **and is wired into
> the live frame** — not visual quality. Scale 0–5: 5 = production-solid backbone ·
> 3 = wired + dispatched but unvalidated in-game · 1 = scaffold · 0 = stub/dead.

## Verified counts (the numbers other docs disagreed on)
- **~28** effect/post renderers authored.
- **17** excluded from the build right now — `CMakeLists.txt` lines 93–109 (commented).
- **~11** compile in the current build.
- **~6** enabled by default (README "Active Effects").
- These are not contradictions; older docs conflated "authored" with "compiles" with "enabled."

## Keystone: depth acquisition (resolves the historical blocker)
Live game depth is obtained **zero-copy via typeless format upgrade** in
`WrappedDevice::CreateTexture2D` (DepthIntercept): `D24_UNORM_S8_UINT -> R24G8_TYPELESS`
+ `BIND_SHADER_RESOURCE`, SRV created on the game's own texture, exposed as
`ProxyInterface->gameDepthSRV`. ReShade/ENB-standard. The "depth ownership" DSV-substitution
path (`DepthOwnership.cpp`) is **disabled dead logic** (`return gameDSV; // DISABLED`, line 170);
its call site is a live no-op.

---

## Heatmap

Legend: ✅ wired & live · 🟡 wired but artifacting/unvalidated · ⛔ build-excluded (real code) · 💀 dead/stub

### Tier A — Interception + dispatch backbone (avg ~4.6) — the crown jewel
| Module | Score | State |
|---|---|---|
| proxy_main / WrappedDevice / WrappedContext | 5 / 4.5 / 4 | ✅ d3d11.dll interposition, COM wrapping, draw/RT/clear intercept |
| DepthIntercept (in WrappedDevice) | 5 | ✅ solves depth acquisition (typeless upgrade) |
| RenderPhaseDetector | 4 | ✅ 9-phase heuristic |
| PhaseDispatcher / RenderPipeline / RenderPassManager | 5 / 5 / 4 | ✅ mid-frame state save/restore + pass registry |
| HiZPyramid | 5 | ✅ standard→reversed-Z mip pyramid from live depth (t19) |
| SceneData / SceneMatrices | 5 | ✅ camera matrix reconstruction |
| SharedGPUResources / SRVInjector / MotionVectorGen | 5 / 5 / 5 | ✅ linear depth+blue noise, slot injection, reprojection |
| ComputeManager / ShaderCache / ShaderLoader | 4 / 5 / 5 | ✅ CS infra, disk cache, external-HLSL hot-reload |
| ConfigManager / WeatherParameterManager | 5 / 4 | ✅ INI persistence, weather modulation |
| MaterialPipeline / AlbedoExtractor | 4 / 3.5 | ✅* DXBC patching; final G-buffer MRT bind unfinished |
| OcclusionCuller / CBDirtyTracker | 2.5 / 3 | 🟡 partial (Hi-Z cull math / CB write-back incomplete) |
| DepthOwnership | 0.5 | 💀 compiled but inert no-op |

### Tier B — Developer tooling (avg ~4.5) — force multiplier, audit-confirmed truthful
| Module | Score | State |
|---|---|---|
| DebugGUI / SB_ShaderDebug | 5 / 5 | ✅ ImGui control surface; D3DCompile IAT hook + in-game error overlay (file:line) |
| GPUProfiler / ShaderReload(F12) | 5 / 5 | ✅ timestamp queries + per-pass A/B; selective hot-reload |
| DebugRenderer / NavMesh / Skeleton | 5 / 4 / 4 | ✅ 3D overlay; SEH-guarded scene walks |
| FrameCapture / BootDiagnostics / TextureDump | 4 / 4 / 3 | ✅ CSV, telemetry, BMP dump |
| RmlD3D11 | 2 | ⚠ compiled (CMake L67) and referenced in `D3D11Hook.cpp`; role overlaps ImGui — needs an owner decision, NOT confirmed dead |

### Tier C — Live effects (avg ~3.7) — where validation pays off
| Renderer | Score | State |
|---|---|---|
| SceneCompositor / DenoiseManager | 5 / 5 | ✅ clamp/isnan-safe orchestrator; reusable denoise lib |
| GTAO (t20) / ContactShadows (t28) | 4 / 4 | ✅ README: GTAO "verified in-game"; shadows need tuning |
| Skylighting (t29) / SSR (t27) / SSGI (t26) | 3 / 3 / 3 | 🟡 probe passes are no-ops; SSR edge artifacts; SSGI green tint |
| ClusteredLighting / LuminanceHistogram / MaterialClassifier | 4 / 3 / 3 | ✅ Forward+, 256-bin histogram, material IDs |
| ImageSpaceTracker | 0 | 💀 header-only stub |

### Tier D — Post chain — built & wired but **never validated in-game**
| Stage | Score | State |
|---|---|---|
| Bloom / ColorPipeline / ToneMap | 5 / 5 / 5 | ✅ CB-overflow fixed (295dd0e); default-off, unvalidated |
| TAA / LuminanceHistogram / LUTManager | 5 / 5 / 5 | ✅ temporal AA, auto-exposure, 64³ LUT |
| DoF / Lens | 3 / 3 | ⛔ complete impls, build-excluded |
| FrameGenerator | 2 | ⛔ SAD optical flow; double-present integration untested |
| TemporalSuperRes | 2 | ⛔ buildable (embedded `kUpscaleCS`/`kSharpenPS`) but build-excluded + unvalidated; violates external-HLSL convention |

### Tier E — Latent effects (build-excluded, real code, low re-enable cost)
| Renderer | Score | Re-enable | Note |
|---|---|---|---|
| SubsurfaceScattering | 4 | LOW | Burley diffusion; only tuning blocks it |
| VolumetricLighting | 3 | LOW | 2 shaders + 3D volume |
| Underwater | 4 | MED | 5 shaders; most feature-complete |
| VolumetricClouds / Atmosphere | 4 / 4 | MED | need sky-pixel resolver hook |
| DynamicCubemap / Decals / ParticleLighting | 3 each | MED | specific integration hooks missing |
| GrassLighting / TreeLOD / WaterBlending | 3 each | LOW | thin CS over existing SRVs — polish |
| SDSMCascades | 2 | MED | histogram only; cascade algo incomplete |

---

## Audit corrections (errors caught during verification — recorded so they don't recur)
- **TemporalSuperRes is NOT "no shaders / not buildable."** It embeds real HLSL as C++ strings
  (`kUpscaleCS` @ TemporalSuperRes.cpp:85, `kSharpenPS` @ :267). It is *build-excluded*, not unbuildable.
- **RmlD3D11 is NOT confirmed-unused.** It is compiled (CMake L67) and referenced in `D3D11Hook.cpp`.
  Treat as "redundant-pending-decision," not dead.
- **Only `DepthOwnership` is cleanly verified dead logic** among the three originally suspected.

---

## Plan of Action (sequenced by concreteness × risk)

**Step 0 — Truth & hygiene (DONE 2026-06-05):** depth path documented in ARCHITECTURE.md;
ground-truth status errata added to ROADMAP.md; this STATUS.md created as the single source of truth.
Remaining optional hygiene: decide RmlD3D11's fate (keep as ImGui fallback or remove); the docs no
longer make conflicting build claims.

**Step 1 — Lock the shippable core (1 wk).** Validate via the existing F12+visualizer+profiler loop:
SceneCompositor round-trip → GTAO → ContactShadows → Bloom→ColorPipeline(AgX)→ToneMap.
This 4-effect stack is the MVP. Exit gate: clean and beats vanilla.

**Step 2 — Fix the two highest-payoff bugs (1 wk).** SSGI green tint (suspect: YCoCg reconstruction +
Co/Cg range clamp; verify SharedRAW.hlsli coefficients) and SSR edge artifacts (half-res upsample
mismatch — add per-pixel confidence term before composite).

**Step 3 — collect latent value, cheapest-first (optional pre-ship).** Re-enable
SubsurfaceScattering → VolumetricLighting → Underwater, one at a time (re-wire + tune, not new authorship).

**Step 4 — Stability & ship (ROADMAP Phase 3).** 1hr crash-free, save/load + cell transitions,
VRAM-leak watch, FOMOD + Nexus.

**Explicitly NOT now:** no new effects; no FO4 port; no FrameGen/TSR enable; don't re-enable DoF/Lens
until the core stack ships.

## One-sentence verdict
RAW is an **infrastructure success with a validation deficit**: the concrete backend (interception,
depth, dispatch, HiZ, dev-tooling) is genuinely built to a high bar, so the next month's entire return
comes from validating and fixing the effects that already run on it — not from writing more.
