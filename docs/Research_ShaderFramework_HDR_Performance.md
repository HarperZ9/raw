# Research: Shader Framework, HDR, and Performance
**SkyrimBridge v3 — March 2026**
**Author: Zain Dana Harper + Claude Opus 4.6**

---

## Table of Contents
1. [Shader Framework Design](#1-shader-framework-design)
2. [Native HDR Implementation](#2-native-hdr-implementation)
3. [Performance Optimizations](#3-performance-optimizations)

---

## 1. Shader Framework Design

### 1.1 Current State

SkyrimBridge has 5 independent GPU systems, each managing their own resources:

| System | Slots | Resources | Pattern |
|--------|-------|-----------|---------|
| LuminanceHistogram | t17 | R32_FLOAT 256×1, StructuredBuffers, staging | Compute (2-pass) |
| LUTManager | t18, s2 | R8G8B8A8 64³ Texture3D, trilinear sampler | Static load |
| HiZPyramid | t19 | R32_FLOAT mipped, per-mip UAV/SRV | Compute (N-pass) |
| TAAManager | t22, s3 | R16G16B16A16F ping-pong pair, trilinear sampler | Compute (resolve) |
| RenderPassManager | — | Dynamic PS/VS, dynamic CB, managed RTs | Rasterization |

**Infrastructure layers:**
- **ComputeManager** — Shader compilation, resource creation (textures, buffers, CBs), CS state save/restore, dispatch helpers
- **SRVInjector** — Binds SRVs at t17+ and samplers at s2+ for ENB shaders
- **RenderPipeline** — Pass orchestration (PreENB/PostENB/PrePresent), managed RT pool
- **RenderPassManager** — Fullscreen + mesh VS+PS+Draw with full state save/restore

**Embedded HLSL locations:**
- `RenderPassManager.cpp` — Fullscreen VS (SV_VertexID → position + UV)
- `LuminanceHistogram.cpp` — Histogram CS + reduction CS
- `HiZPyramid.cpp` — Copy CS + downsample CS
- `TAAManager.cpp` — TAA resolve CS
- `PipelineTest.cpp` — Vignette PS + Film Grain PS

### 1.2 What a Unified Framework Needs

**Problem:** Each GPU system duplicates boilerplate — resource creation, slot management, shader compilation, state tracking. Adding new effects requires writing 200+ lines of D3D11 resource management code.

**Goal:** A shader framework that makes adding a new effect as simple as:
```cpp
auto pass = ShaderFramework::CreateEffect({
    .name = "SSAO",
    .stage = PipelineStage::PreENB,
    .shader = kSSAO_HLSL,
    .inputs = { { "Depth", SlotType::SRV, t19 }, { "Normals", SlotType::SRV, t20 } },
    .outputs = { { "AO", Format::R8_UNORM, Scale::Full } },
    .srvSlot = 20,  // exposed to ENB at t20
});
```

### 1.3 Proposed Framework Architecture

```
┌──────────────────────────────────────────────────┐
│                  ShaderFramework                  │
│  ┌─────────────┐  ┌─────────────┐  ┌──────────┐ │
│  │ ResourcePool │  │ ShaderLib   │  │ SlotMap  │ │
│  │  Textures    │  │  CS/VS/PS   │  │  t17-t31 │ │
│  │  Buffers     │  │  HLSL bank  │  │  s2-s7   │ │
│  │  Samplers    │  │  Hot-reload │  │  b0-b3   │ │
│  └─────────────┘  └─────────────┘  └──────────┘ │
│  ┌─────────────────────────────────────────────┐ │
│  │              EffectGraph                     │ │
│  │  Directed acyclic graph of effects.          │ │
│  │  Auto-resolves input/output dependencies.    │ │
│  │  Topological sort → execution order.         │ │
│  └─────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────┘
         │                    │
         ▼                    ▼
   ComputeManager      RenderPassManager
   (CS dispatch)        (VS+PS+Draw)
```

**Key components:**

1. **ResourcePool** — Centralized texture/buffer/sampler management with reference counting
   - Named resources: `pool.GetOrCreate("SSAO_Buffer", R8_UNORM, fullRes)`
   - Automatic format matching and lifetime management
   - Shared across all effects (no duplicate textures)

2. **ShaderLib** — Compiled shader cache with optional hot-reload
   - Compile-once at init, store CS/VS/PS by name
   - Optional file-based HLSL loading for development (hot-reload on F5)
   - Embedded HLSL strings for release builds

3. **SlotMap** — Central registry of t-slot and s-slot assignments
   - Prevents slot collisions between systems
   - Auto-injects via SRVInjector
   - Documents which slots are used/available

4. **EffectGraph** — Declarative effect dependency graph
   - Effects declare inputs and outputs by name
   - Framework resolves dependencies and orders execution
   - Dead effects (no consumers) can be auto-disabled

### 1.4 Resource Slot Plan

**Currently used:**
| Slot | Owner | Format | Description |
|------|-------|--------|-------------|
| t0-t16 | Skyrim/ENB | Various | Game textures, ENB intermediates |
| t17 | LuminanceHistogram | R32_FLOAT 256×1 | Histogram data |
| t18 | LUTManager | R8G8B8A8 64³ | Film LUT |
| t19 | HiZPyramid | R32_FLOAT mipped | Hierarchical depth |
| t20 | (reserved) | R8_UNORM | SSAO output |
| t21 | (reserved) | R16G16B16A16F half | SSR output |
| t22 | TAAManager | R16G16B16A16F | Temporal history |
| s0-s1 | ENB | — | ENB point/linear |
| s2 | LUTManager | Trilinear clamp | LUT sampler |
| s3 | TAAManager | Trilinear clamp | History sampler |

**Available for framework use:** t20, t21, t23-t31, s4-s7, b1-b3

### 1.5 Implementation Priority

1. SlotMap (prevent collisions, self-documenting)
2. ResourcePool (eliminate per-system resource boilerplate)
3. ShaderLib (centralized compilation + optional hot-reload)
4. EffectGraph (automatic dependency resolution — later phase)

---

## 2. Native HDR Implementation

### 2.1 How RenoDX Works (DX11 HDR Injection)

RenoDX is a ReShade-based addon that injects HDR into DX11/DX12 games. Its approach:

1. **Swap chain interception:** Hooks `IDXGIFactory::CreateSwapChain` to change the backbuffer format from `R8G8B8A8_UNORM` or `R10G10B10A2_UNORM` to `R16G16B16A16_FLOAT` (scRGB) or `R10G10B10A2_UNORM` (HDR10 PQ).

2. **Tonemapping bypass/replacement:** Intercepts the game's tonemapping pass and replaces it with an HDR-aware tonemap that outputs to the wider dynamic range. Instead of clamping to [0,1] SDR, it maps to [0, maxNits/80] scRGB or PQ-encoded values.

3. **Color space conversion:** Final pass converts from linear scRGB to PQ (ST.2084) for HDR10 output, or leaves as scRGB for Windows HDR (scRGB autocomposition).

4. **Per-game tuning:** Each game mod provides shader replacements specific to that game's rendering pipeline — the tonemapper, UI rendering, bloom, etc.

### 2.2 HDR Output Formats

| Standard | Format | Bits | Color Space | Transfer | Max Nits |
|----------|--------|------|-------------|----------|----------|
| HDR10 | R10G10B10A2_UNORM | 10-bit | BT.2020 | PQ (ST.2084) | 10,000 |
| HDR10+ | R10G10B10A2_UNORM | 10-bit | BT.2020 | PQ + dynamic metadata | 10,000 |
| Dolby Vision | R10G10B10A2/R16 | 12-bit* | BT.2020 | PQ + DV RPU | 10,000 |
| scRGB | R16G16B16A16_FLOAT | 16-bit | BT.709 extended | Linear | Unbounded |

**Dolby Vision 12-bit reality:**
- DV Profile 8.1 (gaming) uses 10-bit PQ base layer + 2-bit enhancement (RPU metadata)
- True 12-bit per channel is NOT supported on any current PC GPU output
- PC games use DV Profile 8.4 (IPT-PQ), but support is extremely limited
- **Verdict: Dolby Vision is NOT feasible for a Skyrim mod.** It requires display manufacturer licensing, hardware metadata generation, and OS-level integration that doesn't exist for injected DX11 rendering.

**HDR10+ reality:**
- HDR10+ adds dynamic metadata per scene/frame (peak brightness, content light level)
- Standard HDR10 uses static metadata (set once for the session)
- HDR10+ dynamic metadata on PC is supported through `DXGI_HDR_METADATA_TYPE_HDR10PLUS`
- **Feasible but limited benefit** — the dynamic metadata mainly helps TVs optimize their local dimming. For gaming, the visual difference over HDR10 is minimal.

**Recommended target: HDR10 via scRGB swap chain.**

### 2.3 Implementation Path for SkyrimBridge

#### Step 1: Swap Chain Format Override

```cpp
// In D3D11Hook::HookGameSwapChain, after getting the swap chain:
// Check if Windows HDR is enabled
DXGI_OUTPUT_DESC1 outputDesc;
IDXGIOutput* output = nullptr;
s_swapChain->GetContainingOutput(&output);
IDXGIOutput6* output6 = nullptr;
output->QueryInterface(&output6);
output6->GetDesc1(&outputDesc);
bool hdrSupported = (outputDesc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);

// If HDR is on, recreate swap chain with R16G16B16A16_FLOAT
if (hdrSupported) {
    DXGI_SWAP_CHAIN_DESC desc;
    s_swapChain->GetDesc(&desc);
    desc.BufferDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    // Recreate via factory...
}
```

**Critical issue:** We can't easily recreate the swap chain — Skyrim and ENB both hold references to it. Instead, we'd need to:
- Hook `IDXGIFactory::CreateSwapChain` BEFORE Skyrim creates it (very early hook)
- Or use `IDXGISwapChain::ResizeBuffers` to change the format (risky, invalidates all RTVs)

#### Step 2: ENB Pipeline Interaction

**This is the hardest part.** ENB's 9-stage pipeline:

```
Stage 1-6: R16G16B16A16_FLOAT (HDR linear) ← Already HDR internally!
Stage 7+:  R10G10B10A2_UNORM  (LDR)        ← Tonemapped to SDR here
```

ENB already works in HDR for the first 6 stages. The problem is stages 7+:
- `enbeffectpostpass.fx` receives R10G10B10A2_UNORM (LDR, 0-1 range)
- ENB's internal tonemapping (between stage 6 and 7) crushes the HDR range
- If we skip ENB's tonemapping, stages 7+ would need to handle HDR values

**Options:**

**Option A: Post-ENB HDR lift (easiest)**
- Let ENB do its full SDR pipeline as normal
- In our PrePresent pass (after ENB's stage 9), apply inverse tonemap + HDR expansion
- Convert from SDR [0,1] back to HDR [0, maxNits/80]
- Apply PQ curve for HDR10 output
- **Pros:** No ENB changes needed, works with any ENB preset
- **Cons:** Quality loss from SDR→HDR roundtrip, can't recover clipped highlights

**Option B: ENB HDR passthrough (best quality, hardest)**
- Modify the swap chain to R16G16B16A16_FLOAT
- ENB stages 1-6 naturally work in HDR — no change needed
- Intercept the stage 6→7 transition (ENB's internal tonemapper)
- Replace ENB's tonemapper with an HDR-aware one that maps to PQ/scRGB
- Requires understanding ENB's internal render target management
- **Pros:** True HDR pipeline end-to-end
- **Cons:** May break ENB presets that expect LDR in postpass, requires ENB configuration changes

**Option C: Hybrid — HDR compute pass at PrePresent**
- Keep ENB pipeline as-is (SDR output)
- At PrePresent, read the backbuffer (SDR)
- Apply inverse tonemapping to estimate original HDR values
- Use our LuminanceHistogram data for intelligent highlight reconstruction
- Apply scene-dependent HDR expansion (more boost in dark scenes, less in bright)
- Convert to PQ/scRGB and set swap chain color space
- **Pros:** No ENB modifications, leverages our existing compute pipeline
- **Cons:** Estimated HDR (not true HDR), but can look very good

#### Step 3: ENB Configuration for HDR

ENB would need these settings for proper HDR function:

```ini
[GLOBAL]
; Disable ENB's internal tonemapping (let SB handle it)
UseEffect=false          ; or custom preset with no tonemap in enbeffect.fx
; Keep stages 1-6 in HDR — they already are
; Stage 7+ format override — Boris would need to add this

[FIX]
; Disable ENB's gamma/sRGB corrections (HDR is linear)
FixGameBugs=false
```

**Boris cooperation needed:** True HDR passthrough would require Boris to:
1. Add an option to keep R16G16B16A16_FLOAT through all 9 stages (no LDR conversion)
2. Add a config to disable ENB's internal tonemapping
3. Alternatively, add a callback BETWEEN stage 6 and 7 so SB can inject

Without Boris's cooperation, **Option C (hybrid HDR) is the most realistic approach.**

### 2.4 HDR Implementation Plan

**Phase 1: scRGB swap chain + basic HDR output**
- Hook swap chain creation to use R16G16B16A16_FLOAT
- Set DXGI color space to scRGB linear via `IDXGISwapChain3::SetColorSpace1`
- Add PrePresent pass that converts SDR backbuffer to scRGB (simple gain + PQ)
- Expose HDR settings in debug GUI (max nits, paper white, saturation boost)

**Phase 2: Intelligent SDR→HDR reconstruction**
- Use LuminanceHistogram p95/max to estimate scene dynamic range
- Per-pixel highlight reconstruction from luminance analysis
- Shoulder curve that expands SDR highlights into HDR range
- Preserve ENB color grading (apply expansion after ENB's artistic choices)

**Phase 3: HDR10 metadata + display calibration**
- Set HDR10 static metadata via `IDXGISwapChain4::SetHDRMetaData`
- MaxMasteringLuminance, MinMasteringLuminance, MaxContentLightLevel
- Per-frame MaxFrameAverageLightLevel from histogram
- In-game calibration UI (max nits slider, paper white slider)

### 2.5 HDR Verdict

| Target | Feasibility | Quality | Effort |
|--------|-------------|---------|--------|
| HDR10 (scRGB swap chain + SDR→HDR lift) | High | Good | Medium |
| HDR10 (full pipeline HDR passthrough) | Low without Boris | Excellent | Very High |
| HDR10+ (dynamic metadata) | Medium | Good+ | Medium (on top of HDR10) |
| Dolby Vision | Not feasible | — | — |

**Recommendation:** Start with Option C (hybrid HDR10 via scRGB), which is fully under our control. If Boris adds HDR pipeline support in a future ENB version, we can upgrade to Option B.

---

## 3. Performance Optimizations

### 3.1 Shader Model 5.1 on DirectX 11

**Short answer: SM5.1 is NOT available on DX11.**

| Feature | SM5.0 (DX11) | SM5.1 (DX12) |
|---------|-------------|-------------|
| Resource binding | Register-based | Descriptor heap |
| `space` keyword | No | Yes |
| Bindless resources | No | Yes |
| Root signatures | No | Yes |
| Dynamic indexing of resources | Limited | Full |
| Wave intrinsics | No | SM6.0+ (DX12) |
| Mesh/amplification shaders | No | SM6.5+ (DX12) |

**D3DCompile will compile `cs_5_1` / `ps_5_1` targets**, but:
- The resulting bytecode uses DX12 resource binding model
- DX11 runtime will reject SM5.1 bytecode — `CreatePixelShader` returns `E_INVALIDARG`
- Feature Level 11_1 (available on DX11 hardware) adds some features (UAV at every stage, logical blend ops) but NOT SM5.1 syntax

**What we CAN use on DX11 at FL11_1:**
- Typed UAV loads (read UAVs in PS, not just CS)
- UAVs at every pipeline stage (VS, HS, DS, GS, PS — not just CS/PS)
- Logical blend operations (AND, OR, XOR)
- Target-independent rasterization (TIR)
- Constant buffer offset/partial update

**Verdict:** No SM5.1 upgrade path on DX11. Skyrim would need a DX12 port (like Starfield's Creation Engine 2) to access SM5.1+. We're at the ceiling of SM5.0.

### 3.2 Performance Techniques Within Our Control

#### A. GPU Occlusion Culling via Hi-Z (we have the infrastructure!)

We already have a Hi-Z pyramid at t19. We can use it for:

**Approach:** CPU-side occlusion query using Hi-Z readback
1. ReadBack the coarsest Hi-Z mip (e.g., 4×4) each frame → 16 depth values
2. For each potential occludee (large meshes, distant objects), test its bounding box against the Hi-Z
3. If fully occluded, disable the draw via BSShader hooks (skip SetupGeometry)

**Reality check:** This is very hard to integrate because:
- Skyrim's renderer is CPU-driven — draw decisions are made before our Hi-Z is ready (1-frame delay)
- Modifying BSShader draw calls risks visual artifacts (popping)
- The game already does frustum culling — occlusion culling adds marginal benefit
- **Better use:** Expose Hi-Z to ENB shaders for SSR/SSAO ray marching efficiency (already done at t19)

#### B. Stencil-Based Early-Out for Post-Processing

**Concept:** Skip expensive per-pixel work for sky pixels (typically 30-60% of screen)

Skyrim's stencil buffer classifies pixels:
- 0 = sky (no geometry, just skybox/atmosphere)
- Non-zero = rendered geometry

**Implementation:**
```hlsl
// In our custom passes, early-out on sky pixels
float stencil = SB_StencilBuffer.Load(int3(pixelCoord, 0));
if (stencil == 0) discard;  // sky — skip expensive computation
```

We already read stencil metadata in RenderTracker (`StencilInfo`). We could:
1. Create an SRV for the stencil buffer (D24S8 → R8_UINT view of stencil)
2. Inject at a t-slot for our custom passes
3. Use it to skip SSAO/SSR/DOF computation on sky pixels

**Estimated savings:** 30-60% pixel shader work for fullscreen effects in outdoor scenes.

#### C. Compute Shader Offloading

Replace CPU-bound operations with GPU compute:

| Current (CPU) | Proposed (GPU) | Savings |
|---------------|----------------|---------|
| 5×5 grid luminance sampling (FeedbackProcessor) | Already done: LuminanceHistogram CS | Eliminated CPU readback delay |
| Weather parameter interpolation | CS-based interpolation with LUT | Negligible (CPU is fine for this) |
| NaN sanitization of AllData | CS validation pass | Marginal (memcpy-bound, not worth it) |
| Dirty tracking (memcmp per float4) | — | Already optimal (CPU cache-friendly) |

**New compute opportunities:**

1. **Depth-aware blur/DOF** — Pre-compute a CoC (circle of confusion) map from depth, then blur. Much faster than ENB's per-pixel DOF in enbdepthoffield.fx.

2. **Bilateral upsampling** — Compute half-res effects (SSAO, volumetric fog) then bilateral-upsample to full-res using depth edges. 4× fewer pixels processed.

3. **Async compute readback** — Use double-buffered staging for histogram/Hi-Z readback to hide latency. Already implemented for histogram.

#### D. Draw Call Batching / Indirect Drawing

**Not feasible on DX11 without engine modification.** DX11 supports `DrawInstancedIndirect` and `DrawIndexedInstancedIndirect`, but:
- Skyrim's renderer issues draw calls from C++ (BSShader pipeline)
- We can't intercept individual draw calls efficiently enough to batch them
- This would require modifying Skyrim's scene graph traversal (engine-level change)
- Community Shaders faces the same limitation

#### E. Temporal Techniques (Most Practical Performance Gain)

**Temporal reprojection** allows computing expensive effects at fractional resolution:

1. **Checkerboard rendering** — Compute SSAO/SSR at 50% pixels per frame, fill gaps with temporal reprojection. 2× speedup, minimal quality loss.

2. **Temporal accumulation** — Spread multi-sample effects across frames. Sample 1 ray per pixel per frame, accumulate over 4-16 frames with rejection. Ray-marched volumetrics, reflections, AO.

3. **TAA history reuse** — Our TAAManager already maintains a history buffer. Other effects can read t22 to get stable temporal data for free.

**This is where our pipeline has the biggest advantage.** ENB can't do temporal effects because it has no persistent state between frames. We have TAAManager with ping-pong history, ComputeManager for dispatch, and SRVInjector for exposure to ENB shaders.

#### F. Skyrim-Specific Optimizations

1. **Shadow map caching** — Skyrim re-renders shadow maps every frame, even for static geometry. A compute pass could cache shadow maps and only update when the sun angle changes beyond a threshold. This would save the most expensive single operation in the renderer.
   - **Feasibility:** Requires hooking the shadow map render pass (BSShader vtable for BSShadowDirectionalLight), which is possible but complex.

2. **LOD transition smoothing** — Use our Hi-Z to detect LOD pop-in and apply cross-fade dithering. Purely visual, no perf gain, but improves perceived quality.

3. **Loading screen prediction** — Pre-compile shaders for the next cell during loading screens. Our ShaderCache can warm the cache.

### 3.3 Community Approaches

| Project | Technique | Relevance to SB |
|---------|-----------|-----------------|
| SSE Display Tweaks | Frame limiter, loading screen FPS unlock, memory management | Complementary, no overlap |
| Community Shaders | DX11 hooks, custom shader injection, grass/water/lighting replacement | Similar architecture, different focus (replaces game shaders vs post-process) |
| ENB | Temporal filtering in adaptation, bilateral blur in AO | We can do better with persistent compute state |
| ENB Helper | Time/weather data for ENB | We replace this entirely (enbhelperse.dll) |

### 3.4 Performance Priority Matrix

| Technique | Impact | Feasibility | Effort | Priority |
|-----------|--------|-------------|--------|----------|
| Stencil early-out for sky | High (30-60% pixel savings) | High | Low | **P1** |
| Temporal checkerboard SSAO | High (2× speedup) | High | Medium | **P1** |
| Bilateral half-res effects | High (4× fewer pixels) | High | Medium | **P2** |
| Depth-aware compute DOF | Medium (replaces ENB DOF) | Medium | Medium | **P2** |
| Shadow map caching | Very High | Low (engine hooks) | Very High | **P3** |
| Hi-Z occlusion culling | Low (game already culls) | Low | High | **P4** |

---

## Summary & Recommendations

### Immediate Actions (This Session)
1. Design the SlotMap + ResourcePool as the foundation of the shader framework
2. Document the HDR hybrid approach (Option C) as the realistic target

### Short-Term (Next 2-3 Sessions)
1. Implement stencil buffer SRV injection (free perf for all custom passes)
2. Build shader framework v1 (ResourcePool + ShaderLib + SlotMap)
3. Prototype HDR10 output pass at PrePresent

### Medium-Term (2-4 Weeks)
1. Implement temporal checkerboard rendering infrastructure
2. Build HDR calibration UI in debug GUI
3. Add half-res bilateral upsample utility to framework
4. Implement first "real" effect: compute SSAO with stencil skip + temporal accumulation

### Not Feasible
- SM5.1 (DX11 ceiling, no upgrade path)
- Dolby Vision (requires display licensing + hardware metadata)
- Draw call batching (requires engine-level renderer changes)
- GPU-driven rendering (DX12-only paradigm)
