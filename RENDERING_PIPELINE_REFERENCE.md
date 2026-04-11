# Playground Rendering Pipeline Reference

> **Last updated:** 2026-03-14
> **Author:** Zain Dana Harper
> **Version:** 1.0.0

This document is the authoritative reference for the Playground rendering pipeline. All agents, contributors, and future sessions MUST read this before modifying any rendering code.

---

## 1. Project Identity

**Playground** is a complete rendering platform for Skyrim SE (Creation Engine). It replaces both ENB and Community Shaders with full pipeline ownership via a d3d11.dll proxy + SKSE plugin architecture.

- **SKSE Plugin:** `Playground.dll` — effect shaders, scene data, compositor, debug GUI
- **D3D11 Proxy:** `d3d11.dll` — API wrapper, phase detection, state optimization
- **No ENB/CS coexistence.** Full pipeline ownership assumed.

---

## 2. Skyrim Engine Fundamentals

### 2.1 Depth Convention (CRITICAL)

**Skyrim SE uses STANDARD D3D11 depth: near=0, far=1.**

- Depth buffer format: `DXGI_FORMAT_R24G8_TYPELESS` (SRV: `R24_UNORM_X8_TYPELESS`)
- Projection matrix: `proj[10] = f/(f-n)`, `proj[14] = 1.0` (standard perspective)
- Typical values: near=15.0, far=353840.0

**All effect shaders expect REVERSED-Z (near=1, far=0).**

The HiZ CSCopy shader converts at the source:
```hlsl
DstMip[DTid.xy] = 1.0 - SrcDepth[DTid.xy];  // standard -> reversed-Z
```

**Rules:**
- NEVER read game depth directly in effect shaders. Always read from HiZ (reversed-Z).
- Sky pixels have rawDepth ~ 0.0 in reversed-Z. Sky check: `if (rawDepth < 0.0001)`.
- The linearization formula assumes reversed-Z: `viewZ = N*F / (N + z*(F-N))`.
- LinearDepth (t31) gives view-space Z in game units (1 unit ~ 1.43 cm).

### 2.2 Render Phase Order

The game renders in this order each frame:
```
Unknown -> DepthPrepass -> ShadowMap -> GeometryMain -> [Decals] -> [Sky] -> [AlphaBlend] -> PostProcess -> UI -> Present
```

Phase numbers: Unknown=0, DepthPrepass=1, ShadowMap=2, GeometryMain=3, Decals=4, Sky=5, AlphaBlend=6, PostProcess=7, UI=8.

**PostGeometry fires at GeometryMain(3) -> PostProcess(7) transition.**
At this point: opaque geometry is complete, depth buffer is valid, sky has NOT been rendered yet.

### 2.3 Coordinate Systems

- **World space:** X=East, Y=North, Z=Up (Skyrim convention)
- **View space:** X=Right, Y=Up, Z=Forward (into scene), camera at origin
- **NDC:** X=[-1,1], Y=[-1,1] (D3D11 clip space after perspective divide)
- **UV:** X=[0,1] left-to-right, Y=[0,1] top-to-bottom (D3D11 texture convention)

### 2.4 Projection Matrix Layout (Row-Major)

```
[0]  = 1 / (aspect * tan(fov/2))    // ProjMatrix[0][0]
[5]  = 1 / tan(fov/2)               // ProjMatrix[1][1]
[10] = f / (f-n)                     // depth mapping
[11] = -(n*f) / (f-n)               // depth offset
[14] = 1.0                           // perspective divide
```

**Common conversions:**
```hlsl
// NDC to pixels: multiply by ScreenDims * 0.5
float pixelRadius = ndcRadius * float(ScreenDims.x) * 0.5;

// UV to view-space position:
float2 ndc = float2(uv.x * 2 - 1, (1 - uv.y) * 2 - 1);
float3 viewPos = float3(ndc.x * linearZ / Proj[0][0],
                         ndc.y * linearZ / Proj[1][1],
                         linearZ);
```

---

## 3. Architecture Rules

### 3.1 Mid-Frame Dispatch

Effects run DURING game rendering via PhaseDispatcher callbacks:

1. PhaseDispatcher::OnPhaseChange fires
2. SceneMatrices::UpdateFromNiCamera() reads live camera data
3. D3D11StateBackup saves full pipeline state
4. OM targets UNBOUND (prevents SRV/DSV hazard)
5. RenderPipeline::ExecuteStage runs all passes for that stage
6. State backup RESTORED

**Rules:**
- Effects MUST NOT modify global D3D11 state. Save/restore CS state via ComputeManager.
- Scene color comes from `ctx.gameSceneRTV` (the game's active render target).
- Depth comes from HiZ pyramid (built at PostGeometry:1, reversed-Z).
- Linear depth comes from SharedGPUResources (t31, built at PostGeometry:2).

### 3.2 RT Format Guard (MANDATORY)

The phase detector sometimes fires when non-scene render targets are bound (e.g., R8G8_UNORM temp textures). Any effect that writes to `ctx.gameSceneRTV` MUST guard:

```cpp
if (ctx.gameSceneRTV) {
    ID3D11Resource* guardRes = nullptr;
    ctx.gameSceneRTV->GetResource(&guardRes);
    if (guardRes) {
        ID3D11Texture2D* guardTex = nullptr;
        guardRes->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&guardTex);
        guardRes->Release();
        if (guardTex) {
            D3D11_TEXTURE2D_DESC guardDesc;
            guardTex->GetDesc(&guardDesc);
            guardTex->Release();
            if (guardDesc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT &&
                guardDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
                guardDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
                guardDesc.Format != DXGI_FORMAT_R11G11B10_FLOAT &&
                guardDesc.Format != DXGI_FORMAT_R10G10B10A2_UNORM) {
                return;  // Not a valid scene RT — skip
            }
        }
    }
}
```

**Effects that need this guard:** Any that write to the game scene RTV or create a backbuffer UAV. Pure compute effects that only write to their own textures do NOT need it.

### 3.3 Scene Color Copy Pattern

The game's scene RT is `R16G16B16A16_FLOAT`. The swapchain backbuffer is `R8G8B8A8_UNORM`. `CopyResource` requires identical format+dimensions.

**Every effect that copies the scene MUST use lazy format matching:**
```cpp
D3D11_TEXTURE2D_DESC sceneDesc, copyDesc;
sceneTex->GetDesc(&sceneDesc);
m_copyTex->GetDesc(&copyDesc);
if (sceneDesc.Format != copyDesc.Format || sceneDesc.Width != copyDesc.Width || ...) {
    // Release old copy texture + SRV
    // Recreate with matching format/size
}
ctx.context->CopyResource(m_copyTex, sceneTex);
```

### 3.4 Pipeline Registration Order

`RenderPipeline::Initialize()` MUST be called BEFORE any effect tries to `AddPass()`. The `pipeline.IsInitialized()` check will silently skip registration if the pipeline isn't ready.

Current init order in main.cpp:
```
1. RenderPipeline::Initialize()      // FIRST
2. HiZPyramid::Initialize() + AddPass(PostGeometry:1)
3. SharedGPUResources::Initialize() + AddPass(PostGeometry:2)
4. Effect initializations + AddPass(PostGeometry:15+)
5. PhaseDispatcher::Initialize()
```

---

## 4. SRV Slot Allocation

| Slot | Owner | Type |
|------|-------|------|
| t17 | LuminanceHistogram | 256-bin histogram |
| t18 | LUTManager | 3D film LUT |
| t19 | HiZPyramid | Hierarchical depth (reversed-Z) |
| t20 | GTAORenderer | VB-SSGI output (bounce.rgb + ao.a) |
| t21 | ClusteredLighting | Cluster grid |
| t22 | TAAManager / ClusteredLighting | Temporal history / Light indices |
| t23 | AtmosphereRenderer | Transmittance LUT |
| t24 | AtmosphereRenderer | Scattering LUT |
| t25 | AtmosphereRenderer / MaterialClassifier | Celestial / Material ID |
| t26 | SSGIRenderer | GI output |
| t27 | SSRRenderer | Reflection output |
| t28 | ContactShadowRenderer | Shadow mask |
| t29 | SkylightingRenderer | Sky visibility |
| t30 | SharedGPUResources | Blue noise (128x128, R2 quasi-random) |
| t31 | SharedGPUResources | Linearized depth (R32_FLOAT) |
| t32 | IndirectSpecularRenderer | Indirect specular |
| t33 | VolumetricLightingRenderer | Volumetric scatter + transmittance |

**Known conflicts:** t20-22 overlap between effect outputs and ClusteredLighting. Acceptable because they bind at different pipeline stages.

---

## 5. PostGeometry Pipeline Order

| Priority | Pass | Type | Output |
|----------|------|------|--------|
| 1 | HiZPyramid | Infrastructure | t19 (reversed-Z depth pyramid) |
| 2 | SharedGPUResources | Infrastructure | t31 (linear depth) + t30 (blue noise) + b7 (vanilla params) |
| 15 | GTAO/VB-SSGI | Compute | t20 (AO + bounce, YCoCg) |
| 16 | ContactShadows | Compute | t28 (shadow mask, 1=lit) |
| 17 | Skylighting | Compute | t29 (sky visibility, 0-1) |
| 18 | GrassLighting | Compute+UAV | Backbuffer modification |
| 19 | TreeLODLighting | Compute+UAV | Backbuffer modification |
| 20 | SSGI | Compute | t26 (GI, YCoCg) |
| 21 | IndirectSpecular | Compute | t32 (specular GI) |
| 22 | ScreenSpaceDecals | Compute+UAV | Backbuffer modification |
| 23 | SubsurfaceScattering | Compute+UAV | Backbuffer modification |
| 24 | WaterBlending | Compute+UAV | Backbuffer modification |
| 25 | SSR | Compute | t27 (reflections, half-res) |
| 26 | DynamicCubemap | Compute | t30 (cubemap, 128x128) |
| 28 | ParticleLighting | Compute+UAV | Backbuffer modification |
| 80 | VolumetricClouds | Composite PS | Scene RTV modification |
| 90 | SceneCompositor | Composite PS | Scene RTV modification (applies AO/GI/SSR/shadows/sky) |

---

## 6. Effect Implementation Patterns

### 6.1 Writing a New PostGeometry Compute Effect

```cpp
class MyEffect {
    // 1. HLSL as embedded string constant
    static const char kMyCS[] = R"HLSL(
        Texture2D<float> DepthTex : register(t0);     // HiZ (reversed-Z)
        Texture2D<float> LinearDepth : register(t31);  // Pre-computed
        Texture2D<float4> BlueNoise : register(t30);   // R2 quasi-random
        RWTexture2D<float4> Output : register(u0);

        // Sky check (REVERSED-Z from HiZ)
        if (rawDepth < 0.0001) { Output[coord] = defaultVal; return; }

        // First-person skip
        float linearZ = LinearDepth.Load(int3(coord, 0));
        if (linearZ < 16.0) { Output[coord] = defaultVal; return; }

        // NDC -> pixel conversion (CRITICAL — don't forget * ScreenDims * 0.5)
        float pixelRadius = worldRadius * Proj[0][0] / viewZ * float(ScreenDims.x) * 0.5;
    )HLSL";

    // 2. Register pipeline pass AFTER RenderPipeline::Initialize()
    void Initialize() {
        auto& pl = RenderPipeline::Get();
        if (!pl.IsInitialized()) return;  // Don't silently fail
        m_handle = pl.AddPass({
            .name = "MyEffect", .stage = PipelineStage::PostGeometry,
            .priority = N,  // Check for conflicts!
            .execute = [this](PassContext& ctx) { ExecutePass(ctx); },
        });
    }

    // 3. ExecutePass pattern
    void ExecutePass(PassContext& ctx) {
        if (!m_initialized || !m_enabled) return;

        // RT format guard (if writing to scene)
        // ... (see Section 3.2)

        // Read depth from HiZ (NEVER from game depth directly)
        auto& hiz = HiZPyramid::Get();
        if (hiz.IsInitialized() && hiz.GetSRV())
            depthSRV = hiz.GetSRV();

        // Bind shared resources
        auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
        ctx.context->CSSetShaderResources(31, 1, &linearDepthSRV);

        // Save/restore CS state
        ComputeManager::Get().SaveCSState();
        // ... dispatch ...
        ComputeManager::Get().RestoreCSState();
    }
};
```

### 6.2 Writing a Composite PS Effect

Effects that write to the scene RTV (like SceneCompositor) must:
1. Copy the scene RT to a temp texture (format-matched)
2. Render a fullscreen quad reading from the copy, writing to the original RTV
3. Use the RT format guard

### 6.3 Temporal Accumulation Pattern

All effects with temporal history MUST follow this pattern:

```hlsl
// In temporal CB:
uint FrameIndex;  // passed from C++ m_frameIndex

// In temporal shader — MUST bypass history for first 3 frames:
if (FrameIndex < 3) { Output[coord] = current; return; }

// Then do normal temporal blend with alpha 0.15-0.25:
float alpha = lerp(TemporalAlpha, 0.5, rejection);
float4 result = lerp(history, current, alpha);
```

**Rules:**
- Initialize history buffers with `ClearUnorderedAccessViewFloat` (NOT `ClearRenderTargetView(nullptr,...)`).
- Use `FrameIndex < 3` to skip history entirely for first frames (prevents black ghosting).
- Temporal alpha should be 0.15-0.25 (not 0.05-0.1) for acceptable convergence speed.
- Without motion vectors, ghosting is inherent on camera movement. Accept this tradeoff or implement reprojection.

### 6.4 YCoCg Encoding Convention

GTAO bounce and SSGI GI outputs use YCoCg encoding for luminance-aware denoising:
```hlsl
// Encode (in effect shader)
float3 RGBtoYCoCg(float3 rgb) {
    return float3(0.25*rgb.r + 0.5*rgb.g + 0.25*rgb.b,
                  0.5*rgb.r - 0.5*rgb.b,
                 -0.25*rgb.r + 0.5*rgb.g - 0.25*rgb.b);
}
// Decode (in compositor)
float3 YCoCgToRGB(float3 ycocg) {
    return float3(ycocg.x + ycocg.y - ycocg.z,
                  ycocg.x + ycocg.z,
                  ycocg.x - ycocg.y - ycocg.z);
}
```

---

## 7. Common Pitfalls

| Pitfall | Symptom | Fix |
|---------|---------|-----|
| Reading game depth directly | Effects output neutral (all 1.0) | Always read from HiZ (reversed-Z) |
| NDC used as pixel offset | AO/sky radius invisible (~0.01 px) | Multiply by `ScreenDims.x * 0.5` |
| Pipeline init order wrong | Passes silently don't register | Init RenderPipeline BEFORE AddPass |
| CopyResource format mismatch | Silent fail, stale/black data | Lazy-recreate copy texture on mismatch |
| No RT format guard | Black smearing on non-scene RTs | Check format before writing to gameSceneRTV |
| Writing to scene before sky renders | Black sky at PostGeometry | Skip sky pixels (depth < 0.0001 reversed-Z) |
| Double intensity multiplication | Overblown effects | Apply intensity in ONE place only |
| Self-intersection in ray march | No SSR hits found | Large initial offset + minTravel guard |
| SRV slot collision | Effect output overwritten | Check slot allocation table above |
| Reversed Reinhard clamp | GI blow-out | Use `luma / (1 + luma/maxAdd)` |
| Temporal ghosting (black) | History buffers black/uninitialized | Pass FrameIndex to temporal CB, skip history for first 3 frames |
| History clear no-op | ClearRenderTargetView(nullptr,...) | Use ClearUnorderedAccessViewFloat on actual UAV |
| Temporal alpha too low | Slow convergence, persistent ghosting | Use 0.15-0.25, not 0.05-0.1 |

---

## 8. Rendering Technique Standards

### 8.1 Quality Targets (from ENB/CS Research)

| Technique | Reference Implementation | Our Approach |
|-----------|-------------------------|-------------|
| AO | MXAO visibility bitmask (Gilcher) | VB-SSGI with YCoCg bounce, 4 dirs x 8 steps |
| GI | RTGI voxel cone tracing (Gilcher) | Voxel SH2 128^3 grid, 8-ray hemisphere |
| SSR | Hi-Z tracing (McGuire/Mara) | View-space ray march, mip refinement |
| Shadows | Ray-marched contact shadows | Sun-direction, 32 steps, bilateral denoise |
| Skylighting | Upper-hemisphere probe (CS) | Voxel probe grid, 8-direction sampling |
| Bloom | Dual Kawase + Karis anti-firefly | Threshold + 6-level down/up chain |
| Tonemapping | AgX / ACES / Hejl | Multiple curves, auto-exposure histogram |
| DoF | Physical thin-lens (Bouma) | Ring bokeh, golden spiral, autofocus |
| Color | Tetrahedral LUT (Gilcher ReGrade) | 12-stage pipeline, LUT support |
| TAA | AABB clip + bicubic history | Motion-adaptive, anti-ghosting |

### 8.2 Intensity Defaults (Calibrated to ENB Aesthetics)

| Effect | Parameter | Default | Range | Notes |
|--------|-----------|---------|-------|-------|
| AO | aoRadius | 1.5 | 0.5-5.0 | World-space units (~2cm per unit) |
| AO | aoIntensity | 1.0 | 0-2 | Trace-stage multiplier |
| AO (compositor) | aoIntensity | 0.60 | 0-1 | Composite darkening strength |
| GI (SSGI) | giIntensity | 0.25 | 0-1 | Voxel trace multiplier (was 1.0, too hot) |
| GI (compositor) | giIntensity | 0.15 | 0-1 | Additive blend strength |
| GI (compositor) | giMaxAdd | 0.25 | 0-1 | Reinhard luminance cap |
| SSR | intensity | 1.0 | 0-1 | Reflection color multiplier |
| SSR (compositor) | ssrIntensity | 0.30 | 0-1 | Energy-conserving blend weight |
| Shadows (compositor) | shadowIntensity | 0.80 | 0-1 | Multiplicative darkening |
| Skylighting (compositor) | skylightIntensity | 0.50 | 0-1 | Ambient modulation |

---

## 9. File Structure

```
Playground/
  src/
    core/               # SKSE plugin — all effect renderers
      main.cpp          # Init, frame loop, system registration
      D3D11Hook.cpp     # Proxy connection, ImGui, Present hook
      PhaseDispatcher.cpp   # Mid-frame dispatch
      RenderPipeline.cpp    # Pass orchestration + heartbeat
      SceneCompositor.cpp   # Fullscreen composite (AO/GI/SSR/shadows/sky)
      SceneData.cpp         # Camera matrices, sun direction
      SharedGPUResources.cpp  # Linearized depth, blue noise, vanilla params
      HiZPyramid.cpp        # Hierarchical depth (standard -> reversed-Z)
      GTAORenderer.cpp      # VB-SSGI AO + bounce
      ContactShadowRenderer.cpp  # Sun-direction ray-marched shadows
      SkylightingRenderer.cpp    # Upper-hemisphere sky visibility
      SSRRenderer.cpp       # Screen-space reflections
      SSGIRenderer.cpp      # Voxel-based global illumination
      VolumetricClouds.cpp  # Procedural volumetric clouds
      ToneMapManager.cpp    # Auto-exposure + tone curves
      BloomRenderer.cpp     # Dual Kawase bloom
      DoFRenderer.cpp       # Physical depth of field
      ColorPipeline.cpp     # 12-stage color grading
      LensRenderer.cpp      # Lens flares, CA, vignette
      [... 10 more effect renderers]
    d3d11_proxy/        # Proxy DLL — API interception
      proxy_main.cpp    # DLL entry, ProxyInterface
      WrappedDevice.cpp # ID3D11Device wrapper
      WrappedContext.cpp    # ID3D11DeviceContext wrapper
      WrappedSwapChain.cpp  # IDXGISwapChain wrapper + depth capture
      RenderPhaseDetector.cpp   # 9-phase heuristic classifier
      ProxyAPI.h        # Shared struct between proxy and plugin
```

---

## 10. Key Constants

```cpp
// Near/far clip (from NiCamera)
near = 15.0f;    // ~21 cm
far  = 353840.0f; // ~5 km

// First-person depth threshold
FPDepthThreshold = 16.0f;  // ~23 cm (between ENB 11.76, CS 18.0)

// Skyrim gamma
gamma = 1.6f;  // Skyrim's sRGB-ish gamma (not 2.2)

// Blue noise
blueNoiseSize = 128;  // 128x128 R2 quasi-random, 4 channels

// Screen resolution (typical)
screenW = 3840, screenH = 2160;  // 4K

// Game unit scale
1 Skyrim unit ~ 1.43 cm ~ 0.56 inches
```

---

## 11. Debug Controls

| Key | Function |
|-----|----------|
| INSERT | Toggle ImGui debug GUI |
| F7 | Toggle mid-frame dispatch |
| F8 | Toggle compute passes |
| F9 | Toggle render passes |
| F10 | Toggle frame capture |
| F11 | Toggle GPU profiler overlay |

---

*This document should be updated whenever architectural decisions change, new effects are added, or pitfalls are discovered.*
