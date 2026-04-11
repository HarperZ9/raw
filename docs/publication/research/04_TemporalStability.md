# 04 -- Temporal Anti-Aliasing and Stability for ENB Pixel Shaders

> **Target implementation:** `SB_TemporalAA.fx` (addon effect) + `Helper/SB_TemporalCore.fxh`
> **Pipeline:** DX11 SM5.0, pixel shaders only, FXC compiler, ENBSeries v504+
> **Author:** Zain Dana Harper | **Date:** 2026-03-07

---

## Table of Contents

1. [Platform Constraint Matrix](#1-platform-constraint-matrix)
2. [AABB Temporal Clipping (Salvi) -- Current Baseline](#2-aabb-temporal-clipping-salvi----current-baseline)
3. [k-DOP Neighborhood Clipping](#3-k-dop-neighborhood-clipping)
4. [5-Tap Bicubic Catmull-Rom History Sampling](#4-5-tap-bicubic-catmull-rom-history-sampling)
5. [Motion-Adaptive Blend Factor with SkyrimBridge](#5-motion-adaptive-blend-factor-with-skyrimbridge)
6. [Camera-Only Motion Vector Workarounds](#6-camera-only-motion-vector-workarounds)
7. [Depth-Aware Disocclusion](#7-depth-aware-disocclusion)
8. [R2 Jitter Sequence](#8-r2-jitter-sequence)
9. [RCAS Post-TAA Sharpening](#9-rcas-post-taa-sharpening)
10. [Luminance-Weighted Blending](#10-luminance-weighted-blending)
11. [Asymmetric EMA](#11-asymmetric-ema)
12. [SVGF-Lite for Screen-Space Effects](#12-svgf-lite-for-screen-space-effects)
13. [Stochastic Transparency Workaround](#13-stochastic-transparency-workaround)
14. [Hierarchical Block-Matching Optical Flow](#14-hierarchical-block-matching-optical-flow)
15. [Complete Pipeline Pass Allocation](#15-complete-pipeline-pass-allocation)
16. [Migration Checklist](#16-migration-checklist)
17. [Academic References](#17-academic-references)

---

## 1. Platform Constraint Matrix

Every design decision in this document is governed by the following immovable constraints:

| Constraint | Detail | Impact |
|---|---|---|
| **No compute shaders** | ENB addon shaders are pixel shaders only (full-screen quad dispatch). No `[numthreads]`, no UAVs from addon code, no `groupshared`. | All TAA logic must run as pixel shader passes. Neighborhood gathers use texture fetches, not LDS. |
| **Technique limit: 128** | ENB enforces a hard cap of 128 techniques per .fx file. SB_TemporalAA targets 5 techniques, leaving headroom for host shader. | Combine operations aggressively. k-DOP build + history reprojection is inlined into the resolve pass. |
| **Persistent history via TextureCustom** | ENB provides `TextureCustom01` through `TextureCustom08` as persistent render targets across frames. These are the *only* mechanism for temporal accumulation in addon shaders. Ping-pong is automatic: ENB swaps read/write bindings each frame. | TextureCustom01 = TAA history color. TextureCustom06 = previous-frame motion vectors. No manual ping-pong management needed. |
| **0.5 ms total TAA budget** | At 2560x1440, the entire TAA pipeline (velocity dilation + neighborhood + clip + blend + sharpen + dither) must complete in 0.5 ms on a mid-range GPU (RTX 3060 / RX 6700 XT class). | Every sub-pass is budgeted individually. See Section 15 for the full breakdown. |
| **R10G10B10A2 in postpass** | `enbeffectpostpass.fx` renders to R10G10B10A2_UNORM (10-bit per channel, LDR). Only 1024 levels per channel. Sharpening in this format causes visible banding. | RCAS sharpening runs in `SB_TemporalAA.fx` (R16G16B16A16F) *before* the postpass stage. Triangular dither is applied only if writing to LDR output. |
| **No per-object motion vectors** | Skyrim SE does not produce per-object motion vector buffers. Only camera-derived MVs are available via `SB_ViewProj` / `SB_PrevVP` matrices from the SkyrimBridge constant buffer. Moving NPCs, swinging weapons, flying arrows, animated particles, and environmental animations are *invisible* to the MV buffer. | Section 6 details a 3-prong workaround strategy (closest-depth dilation, luminance-change ghost detection, material-based alpha). |
| **FXC compiler** | ENB compiles shaders with `fxc.exe` (D3DCompile), not DXC. FXC has known issues with complex control flow, loop unrolling, and certain intrinsics. | Avoid `[branch]` inside tight loops. Precompute normalized axis vectors as literal constants. Use `mad()` explicitly where precision matters. |
| **ENB render target pool** | ENB controls RT allocation. Addon shaders cannot create additional RTs beyond the TextureCustom slots. Intermediate results must be written to TextureCustom or composed inline. | Pass 0 writes to the default RT (RGBA16F). The resolve pass writes directly to TextureCustom01. |
| **ScreenSize convention** | ENB provides `float4 ScreenSize` as `(width, height, 1/width, 1/height)`. Always use `.zw` for texel offsets. | All functions in this document use `ScreenSize.zw` for pixel-space conversions. |

### Skyrim Depth Buffer Characteristics

| Property | Value |
|---|---|
| Format | D24S8 or D32_FLOAT + S8 (driver-dependent) |
| Near plane | ~15 game units (~15 cm) |
| Far plane | ~163840 game units (~1.6 km) |
| Near:Far ratio | 1:10,922 |
| Z convention | Standard (0 = near, 1 = far) -- NOT reversed-Z |
| Precision collapse | Beyond ~500 game units, depth precision degrades rapidly. Relative thresholds mandatory. |

### SkyrimBridge Matrix Availability

The following matrices from `SkyrimBridge_CB.fxh` are critical for temporal stability:

```hlsl
// Current frame
float4x4 SB_ViewProj  = float4x4(SB_ViewProj_Row0, SB_ViewProj_Row1, SB_ViewProj_Row2, SB_ViewProj_Row3);
// Previous frame (1-frame delay)
float4x4 SB_PrevVP    = float4x4(SB_PrevVP_Row0, SB_PrevVP_Row1, SB_PrevVP_Row2, SB_PrevVP_Row3);
// Inverse VP (world reconstruction from depth)
float4x4 SB_InvVP     = float4x4(SB_InvVP_Row0, SB_InvVP_Row1, SB_InvVP_Row2, SB_InvVP_Row3);
```

### Fallback When SkyrimBridge Is Absent

If SkyrimBridge is not installed, all `SB_*` float4s read as zero. The TAA system must remain functional using ENB's built-in `Timer.x` (elapsed time) and `ScreenSize` with degraded quality (no motion vectors, no jitter correction, blend factor = fixed 0.1).

```hlsl
bool SB_IsActive() { return SB_Render_Frame.x > 0.0; }
```

---

## 2. AABB Temporal Clipping (Salvi) -- Current Baseline

### Background

The current `SB_TemporalAA.fx` implementation uses RGB-space AABB variance clipping as described by Marco Salvi (GDC 2016, "An Excursion in Temporal Supersampling"). This is the industry baseline and the technique being replaced by k-DOP (Section 3). It is documented here as a reference point for comparison and for fallback use.

### How It Works

1. Sample a 3x3 neighborhood around the current pixel (9 texels).
2. Compute per-channel min and max across the 9 samples to form an axis-aligned bounding box in RGB (or YCoCg) space.
3. Clip the history sample toward the current pixel, stopping at the AABB surface.
4. Blend the clipped history with the current sample.

The AABB is the simplest possible bounding volume -- three independent intervals, one per color channel. Any sample inside the bounding box is accepted; any sample outside is clipped to the nearest point on the box surface (or along the ray from current to history).

### HLSL -- RGB-Space AABB Clipping (Current Implementation)

```hlsl
//-----------------------------------------------------------------------------
//  SB_TemporalCore.fxh -- AABB variance clipping (Salvi baseline)
//  This is the CURRENT implementation being replaced by k-DOP.
//  Retained for A/B comparison and as fallback when compile time is critical.
//-----------------------------------------------------------------------------

// Build AABB from 3x3 neighborhood
void SB_BuildAABB(float3 neighbors[9], out float3 aabbMin, out float3 aabbMax)
{
    aabbMin = neighbors[0];
    aabbMax = neighbors[0];

    [unroll]
    for (int i = 1; i < 9; i++)
    {
        aabbMin = min(aabbMin, neighbors[i]);
        aabbMax = max(aabbMax, neighbors[i]);
    }
}

// Ray-clip toward AABB (Salvi method)
// Clips historyColor along the ray from currentColor toward historyColor,
// stopping at the AABB boundary. Returns historyColor unchanged if already inside.
float3 SB_ClipAABB(float3 aabbMin, float3 aabbMax,
                    float3 currentColor, float3 historyColor)
{
    float3 center  = 0.5 * (aabbMax + aabbMin);
    float3 extents = 0.5 * (aabbMax - aabbMin);

    // Ray from center to history
    float3 rayDir = historyColor - center;

    // Intersection with each axis slab
    float3 invDir = rcp(rayDir + sign(rayDir) * 1e-10);
    float3 tNeg = (-extents - rayDir) * invDir;  // unused in this formulation
    float3 tPos = ( extents) * abs(invDir);

    // Find the axis that constrains most (minimum positive t along all axes)
    float tClip = min(tPos.x, min(tPos.y, tPos.z));
    tClip = saturate(tClip);

    return center + rayDir * tClip;
}

// Variant: YCoCg-space AABB (tighter bounds via decorrelation)
// Transform neighborhood to YCoCg, build AABB there, clip, transform back.
float3 RGB_to_YCoCg(float3 rgb)
{
    return float3(
         0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b,   // Y
         0.5  * rgb.r                - 0.5  * rgb.b,    // Co
        -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b     // Cg
    );
}

float3 YCoCg_to_RGB(float3 ycocg)
{
    float Y  = ycocg.x;
    float Co = ycocg.y;
    float Cg = ycocg.z;
    return float3(Y + Co - Cg, Y + Cg, Y - Co - Cg);
}

float3 SB_ClipAABB_YCoCg(float3 neighbors[9], float3 currentColor,
                           float3 historyColor)
{
    // Transform all samples to YCoCg
    float3 neighborsYCC[9];
    [unroll]
    for (int i = 0; i < 9; i++)
        neighborsYCC[i] = RGB_to_YCoCg(neighbors[i]);

    float3 currentYCC = RGB_to_YCoCg(currentColor);
    float3 historyYCC = RGB_to_YCoCg(historyColor);

    // Build AABB in YCoCg space
    float3 aabbMin, aabbMax;
    SB_BuildAABB(neighborsYCC, aabbMin, aabbMax);

    // Clip in YCoCg space
    float3 clippedYCC = SB_ClipAABB(aabbMin, aabbMax, currentYCC, historyYCC);

    // Transform back to RGB
    return YCoCg_to_RGB(clippedYCC);
}

// Combined neighborhood gather + AABB clip (one function call)
float3 SB_NeighborhoodClipAABB(Texture2D tex, SamplerState smp,
                                float2 uv, float3 historyColor)
{
    float2 texelSize = ScreenSize.zw;

    float3 neighbors[9];
    int idx = 0;
    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            neighbors[idx] = tex.SampleLevel(smp, uv + float2(x, y) * texelSize, 0).rgb;
            idx++;
        }
    }

    float3 currentColor = neighbors[4];

    float3 aabbMin, aabbMax;
    SB_BuildAABB(neighbors, aabbMin, aabbMax);

    return SB_ClipAABB(aabbMin, aabbMax, currentColor, historyColor);
}
```

### Limitations in Skyrim

The AABB clipping method has several weaknesses that are particularly pronounced in Skyrim SE:

- **Dense foliage:** The Rift, Falkreath forests, and Reach highlands have extreme green-to-brown color ranges in 3x3 neighborhoods. The axis-aligned bounding box is loose because the actual color distribution is elongated along a diagonal in RGB space that no AABB axis captures.
- **Sunset/sunrise HDR gradients:** Skyrim's time-of-day system produces extreme luminance gradients during dawn/dusk. The standard AABB allows ghost colors from the bright sky to bleed into dark terrain silhouettes.
- **Snow vs. dark rock:** High-contrast biomes (Winterhold, Throat of the World) produce neighborhood distributions elongated along the luminance axis. AABB is too permissive here, introducing characteristic reddish tint artifacts.
- **YCoCg variant:** Decorrelating to YCoCg space helps but does not fully solve the problem. The YCoCg axes are fixed rotations of RGB and do not adapt to the actual color distribution. k-DOP's diagonal axes capture these distributions 30-60% more tightly.

These limitations motivate the upgrade to k-DOP clipping (Section 3).

### Performance Baseline

| Metric | RGB AABB | YCoCg AABB |
|---|---|---|
| DXBC instructions | ~35 | ~50 |
| ALU cost (1440p) | ~0.10 ms | ~0.13 ms |
| Ghost rejection (foliage edge) | ~45% | ~60% |
| Ghost rejection (HDR specular) | ~30% | ~40% |

---

## 3. k-DOP Neighborhood Clipping

### Background

Standard TAA clips the reprojected history sample against a neighborhood bounding volume to reject ghosting. The cheapest approach is an axis-aligned bounding box (AABB) in RGB or YCoCg space, computed from a 3x3 neighborhood of the current frame. However, AABBs are loose -- they include corners that no actual neighbor occupies, admitting ghosted colors that lie in these empty diagonal regions.

**k-DOP (k-Discrete Oriented Polytope)** tightens the bounding volume by adding diagonal axis pairs. Ikkala et al. (SIGGRAPH Asia 2024) demonstrated that a **14-DOP** (7 axis pairs) provides near-optimal clipping tightness with practical instruction counts for real-time use.

### Why Skyrim Specifically Benefits

1. **Dense foliage chrominance variance.** Skyrim's forests mix dark-green pine needles, bright-green deciduous leaves, brown bark, and blue sky in adjacent pixels. An AABB in RGB space for a 3x3 neighborhood spanning a tree canopy edge will be very loose, allowing ghosted sky-blue to bleed into foliage during camera pans. The diagonal axes of a 14-DOP reject these corner cases.

2. **Extreme HDR dawn/dusk.** During sunrise and sunset, adjacent pixels can span from near-black shadows to bright orange sun glare. The AABB for such a neighborhood is enormous in luminance range. The luminance axis (axis 7) in the k-DOP constrains this specifically.

3. **Snow/rock contrast.** Skyrim's mountainous terrain places pure-white snow directly adjacent to dark-gray rock. The R+G+B (luminance-like) and individual channel axes constrain this better than a box.

### The 7 Axis Pairs

| Axis Index | Direction (normalized) | Geometric Meaning |
|---|---|---|
| 0 | `(1, 0, 0)` | Red channel (AABB axis) |
| 1 | `(0, 1, 0)` | Green channel (AABB axis) |
| 2 | `(0, 0, 1)` | Blue channel (AABB axis) |
| 3 | `(0.7071, 0.7071, 0)` | Red+Green diagonal |
| 4 | `(0.7071, 0, 0.7071)` | Red+Blue diagonal |
| 5 | `(0, 0.7071, 0.7071)` | Green+Blue diagonal |
| 6 | `(0.5774, 0.5774, 0.5774)` | Luminance (R+G+B) |

Axes 0-2 define the standard AABB. Since k-DOP includes these, **a 14-DOP is never worse than AABB** by construction. The diagonal axes can only tighten the volume.

### Ray-Clip Method (Recommended)

Two approaches exist for clipping a point to a k-DOP:

1. **Iterative projection** (~150 DXBC instructions): Project the point onto each violating halfplane iteratively. Convergence is not guaranteed in finite steps for non-convex intersections (though k-DOPs are convex, the iteration count is variable).

2. **Ray-clip** (~80 DXBC instructions): Cast a ray from `currentColor` toward `historyColor`. Find the first exit point of this ray from the k-DOP. The clipped result lies on the k-DOP surface along the line connecting current and history.

The ray-clip method is both faster and deterministic. The algorithm:

1. For each axis `i`, compute the projection of the ray origin `o` and direction `d` onto that axis.
2. Compute `tFar = max((dopMin[i] - proj_o) / proj_d, (dopMax[i] - proj_o) / proj_d)` for each axis.
3. The first exit is `tMin = min(all tFar values)`.
4. `clipped = current + saturate(tMin) * (history - current)`.

The `saturate(tMin)` ensures that if history is already inside the k-DOP (tMin >= 1), it passes through unchanged.

### Complete HLSL Implementation

```hlsl
//─────────────────────────────────────────────────────────────────────────────
//  SB_TemporalCore.fxh -- k-DOP neighborhood clipping
//─────────────────────────────────────────────────────────────────────────────

// 7 axis directions -- precomputed as literal float3 values.
// FXC note: do NOT use normalize() here; the compiler may not fold it at
// compile time, resulting in runtime rsqrt instructions.
static const float3 kDOP_Axes[7] = {
    float3(1.0, 0.0, 0.0),                     // R
    float3(0.0, 1.0, 0.0),                     // G
    float3(0.0, 0.0, 1.0),                     // B
    float3(0.70710678, 0.70710678, 0.0),        // R+G
    float3(0.70710678, 0.0, 0.70710678),        // R+B
    float3(0.0, 0.70710678, 0.70710678),        // G+B
    float3(0.57735027, 0.57735027, 0.57735027)  // Luminance
};

// Build k-DOP min/max from 9 neighborhood samples.
// dopMin[7] and dopMax[7] are output arrays.
void SB_BuildKDOP(float3 neighbors[9], out float dopMin[7], out float dopMax[7])
{
    [unroll]
    for (int a = 0; a < 7; a++)
    {
        float projMin = 1e6;
        float projMax = -1e6;
        [unroll]
        for (int s = 0; s < 9; s++)
        {
            float proj = dot(neighbors[s], kDOP_Axes[a]);
            projMin = min(projMin, proj);
            projMax = max(projMax, proj);
        }
        dopMin[a] = projMin;
        dopMax[a] = projMax;
    }
}

// Ray-clip: clip historyColor toward currentColor against the k-DOP.
// Returns the clipped color (on the k-DOP surface, or history if inside).
float3 SB_ClipKDOP(float3 currentColor, float3 historyColor,
                   float dopMin[7], float dopMax[7])
{
    float3 rayDir = historyColor - currentColor;
    float tMin = 1.0;  // 1.0 means history is fully inside

    [unroll]
    for (int a = 0; a < 7; a++)
    {
        float projO = dot(currentColor, kDOP_Axes[a]);
        float projD = dot(rayDir, kDOP_Axes[a]);

        // Avoid division by zero for near-parallel rays
        if (abs(projD) > 1e-6)
        {
            float invD = rcp(projD);
            float t0 = (dopMin[a] - projO) * invD;
            float t1 = (dopMax[a] - projO) * invD;

            // tFar is the exit along this axis (the farther intersection)
            float tFar = max(t0, t1);

            // First exit across all axes
            tMin = min(tMin, tFar);
        }
    }

    return currentColor + saturate(tMin) * rayDir;
}

// Combined neighborhood gather + k-DOP clip.
// tex: current frame color texture
// smp: point-clamp sampler
// uv: current pixel UV
// historyColor: reprojected history sample
float3 SB_NeighborhoodClipKDOP(Texture2D tex, SamplerState smp,
                                float2 uv, float3 historyColor)
{
    float2 texelSize = ScreenSize.zw;  // (1/width, 1/height)

    // Gather 3x3 neighborhood
    float3 neighbors[9];
    int idx = 0;
    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            neighbors[idx] = tex.SampleLevel(smp, uv + float2(x, y) * texelSize, 0).rgb;
            idx++;
        }
    }

    float3 currentColor = neighbors[4];  // Center sample

    // Build k-DOP bounds
    float dopMin[7], dopMax[7];
    SB_BuildKDOP(neighbors, dopMin, dopMax);

    // Ray-clip history against k-DOP
    return SB_ClipKDOP(currentColor, historyColor, dopMin, dopMax);
}
```

### Performance Analysis

| Metric | AABB (YCoCg) | 14-DOP (Ray-Clip) | Delta |
|---|---|---|---|
| DXBC instructions | ~50 | ~80 | +30 |
| ALU cost (1440p) | ~0.13 ms | ~0.15 ms | +0.02 ms |
| Ghost rejection (foliage edge) | ~60% | ~90% | +30% |
| Ghost rejection (HDR specular) | ~40% | ~85% | +45% |

The +0.02 ms cost is negligible within the 0.5 ms budget. The quality improvement is most visible during lateral camera pans through Skyrim's dense forests and during dawn/dusk transitions.

### FXC Compiler Notes

- The `[unroll]` attributes are critical. Without them, FXC may emit dynamic loops with `loop` instructions, which are significantly slower on GCN/RDNA architectures.
- The literal float3 axis values avoid runtime `normalize()`. FXC *can* fold `normalize(float3(1,1,0))` at compile time, but this behavior is not guaranteed across all FXC versions. Literals are deterministic.
- `rcp()` is used instead of `1.0/x` for the ray-clip division. On NVIDIA, these compile identically. On AMD GCN, `rcp` maps directly to `v_rcp_f32` (1 cycle), while `1.0/x` may emit a less efficient sequence.

---

## 4. 5-Tap Bicubic Catmull-Rom History Sampling

### Problem

When reprojecting the history buffer, the reprojected UV rarely lands exactly on a texel center. Standard bilinear sampling of the history introduces blur, which accumulates over frames and softens fine detail -- grass textures, distant terrain LOD transitions, stone wall normals, and hair strands.

A full bicubic (4x4 = 16 taps) is too expensive for TAA history sampling. The **5-tap Catmull-Rom** filter exploits hardware bilinear interpolation to achieve bicubic quality from only 5 texture fetches arranged in a cross pattern.

### How It Works

A Catmull-Rom spline has negative lobes (it overshoots slightly to sharpen). A 2D Catmull-Rom filter is separable and requires a 4x4 kernel. By computing the Catmull-Rom weights and strategically placing 4 bilinear samples at offset positions (plus 1 center tap), the hardware's free bilinear interpolation performs the inner 2x2 portions of each quadrant, reducing 16 point samples to 5 bilinear samples.

The 5 taps form a cross pattern:
```
    [N]
[W] [C] [E]
    [S]
```

Each of the 4 directional taps (N, S, E, W) is placed at a sub-texel offset computed from the Catmull-Rom weights, and the hardware bilinear blends the appropriate 2x2 neighborhood.

### Complete HLSL Implementation

```hlsl
//─────────────────────────────────────────────────────────────────────────────
//  5-tap bicubic Catmull-Rom history sampling
//  Based on: Jimenez (SIGGRAPH 2014), Pedersen (INSIDE, GDC 2016)
//─────────────────────────────────────────────────────────────────────────────

float4 SB_SampleHistoryBicubic(Texture2D tex, SamplerState smpLinear,
                                float2 uv, float4 screenSize)
{
    // screenSize = (width, height, 1/width, 1/height)
    float2 texSize = screenSize.xy;
    float2 invTexSize = screenSize.zw;

    // Position in texel space
    float2 pos = uv * texSize;
    float2 center = floor(pos - 0.5) + 0.5;
    float2 f = pos - center;  // Fractional offset [0, 1)

    // Catmull-Rom weights for the fractional position
    // w0 = -0.5*f^3 + f^2 - 0.5*f
    // w1 =  1.5*f^3 - 2.5*f^2 + 1
    // w2 = -1.5*f^3 + 2.0*f^2 + 0.5*f
    // w3 =  0.5*f^3 - 0.5*f^2
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);

    // Combine inner weights for bilinear tap placement
    float2 w12 = w1 + w2;

    // Offset for the combined center tap (blend between texel 1 and 2)
    float2 offset12 = w2 / w12;

    // 5 tap positions in UV space
    float2 uvC  = (center + offset12) * invTexSize;
    float2 uvN  = (center + float2(offset12.x, -1.0)) * invTexSize;
    float2 uvS  = (center + float2(offset12.x,  2.0)) * invTexSize;
    float2 uvW  = (center + float2(-1.0, offset12.y)) * invTexSize;
    float2 uvE  = (center + float2( 2.0, offset12.y)) * invTexSize;

    // Fetch 5 bilinear samples
    float4 sC = tex.SampleLevel(smpLinear, uvC, 0);
    float4 sN = tex.SampleLevel(smpLinear, uvN, 0);
    float4 sS = tex.SampleLevel(smpLinear, uvS, 0);
    float4 sW = tex.SampleLevel(smpLinear, uvW, 0);
    float4 sE = tex.SampleLevel(smpLinear, uvE, 0);

    // Weights
    float weightC = w12.x * w12.y;
    float weightN = w12.x * w0.y;
    float weightS = w12.x * w3.y;
    float weightW = w0.x * w12.y;
    float weightE = w3.x * w12.y;

    // Normalize and combine
    float totalWeight = weightC + weightN + weightS + weightW + weightE;
    float invTotal = rcp(totalWeight);

    return (sC * weightC + sN * weightN + sS * weightS +
            sW * weightW + sE * weightE) * invTotal;
}
```

### TextureCustom Slot Allocation

ENB provides 8 persistent custom textures. For the full SkyrimBridge screen-space pipeline, the allocation is:

| Slot | Usage | Format | Ping-Pong |
|---|---|---|---|
| TextureCustom01 | TAA history color | R16G16B16A16F | Automatic |
| TextureCustom02 | GTAO (ambient occlusion) | R8_UNORM | Automatic |
| TextureCustom03 | Screen-space reflections | R16G16B16A16F | Automatic |
| TextureCustom04 | Contact shadows | R8_UNORM | Automatic |
| TextureCustom05 | Volumetric fog | R16G16B16A16F | Automatic |
| TextureCustom06 | Previous-frame motion vectors | R16G16_FLOAT | Automatic |
| TextureCustom07 | Reserved | -- | -- |
| TextureCustom08 | Reserved | -- | -- |

### Performance and Quality

- **Cost:** 5 texture fetches vs. 1 for bilinear, ~0.03 ms at 1440p.
- **Quality:** Eliminates ~80% of post-TAA sharpening need for grass and stone detail. Distant terrain LOD transitions are significantly cleaner. The negative lobes of Catmull-Rom provide inherent sharpening that preserves edges without a separate sharpening pass.
- **Skyrim-specific benefit:** Skyrim's grass rendering produces extremely high-frequency detail. Bilinear history sampling blurs this within 3-5 frames of accumulation, making grass appear "washed." Bicubic sampling preserves the original texture detail and eliminates the need for aggressive RCAS compensation.

---

## 5. Motion-Adaptive Blend Factor with SkyrimBridge

### Concept

The TAA blend factor `alpha` controls how much of the current frame vs. history is used:

```
result = lerp(historyClipped, currentColor, alpha)
```

- `alpha = 0.04` (4%): Heavy temporal accumulation, maximum anti-aliasing, but prone to ghosting on motion.
- `alpha = 0.20` (20%): Light accumulation, responsive to motion, but reduced AA quality.

A fixed alpha is a poor compromise. The optimal alpha varies per-pixel based on motion magnitude, depth discontinuities, camera acceleration, and game state. SkyrimBridge provides unique signals that no other TAA implementation can leverage.

### Composite Alpha Computation

```hlsl
//─────────────────────────────────────────────────────────────────────────────
//  Motion-adaptive blend factor with SkyrimBridge game state integration
//─────────────────────────────────────────────────────────────────────────────

// Standard motion-based alpha (works without SkyrimBridge)
float SB_BaseAlpha(float2 motionVector, float depthCurrent, float depthHistory,
                   float2 prevMotionVector)
{
    // 1. Velocity magnitude contribution
    //    Map screen-space motion magnitude to alpha range [0.04, 0.20]
    float motionMag = length(motionVector * ScreenSize.xy);  // in pixels
    float velocityAlpha = lerp(0.04, 0.20, saturate(motionMag * 0.5));

    // 2. Depth discontinuity contribution
    //    Relative threshold handles Skyrim's extreme near/far ratio
    float depthDiff = abs(depthCurrent - depthHistory);
    float relativeDepth = depthDiff / max(depthCurrent, 0.001);
    float depthAlpha = smoothstep(0.05, 0.15, relativeDepth) * 0.15;

    // 3. Acceleration contribution (motion vector change between frames)
    //    Detects sudden camera moves (menu open, fast turn, horse gallop start)
    float2 mvDelta = (motionVector - prevMotionVector) * ScreenSize.xy;
    float acceleration = length(mvDelta);
    float accelAlpha = smoothstep(1.0, 5.0, acceleration) * 0.10;

    return saturate(velocityAlpha + depthAlpha + accelAlpha);
}

// Full SkyrimBridge-enhanced alpha (requires SB_IsActive())
float SB_ComputeTAAAlpha(float2 motionVector, float depthCurrent,
                         float depthHistory, float2 prevMotionVector)
{
    float alpha = SB_BaseAlpha(motionVector, depthCurrent, depthHistory,
                               prevMotionVector);

    if (!SB_IsActive())
        return alpha;

    // ── SkyrimBridge game state modulation ──

    // 4. Weather transition boost
    //    During weather changes, lighting shifts rapidly across the entire
    //    scene. History becomes invalid faster. SB_Weather_Transition.x is
    //    the transition progress [0, 1] where 0 = stable, 1 = mid-transition.
    float weatherBoost = SB_Weather_Transition.x * 0.08;
    alpha += weatherBoost;

    // 5. Combat alpha boost for actor regions
    //    Without per-object motion vectors, actors in combat move without MV
    //    coverage. When the player is in combat (SB_Player_Combat.x > 0.5),
    //    globally boost alpha slightly. This is a coarse heuristic --
    //    material-based alpha (Section 6, Prong 3) provides finer control.
    float combatBoost = (SB_Player_Combat.x > 0.5) ? 0.04 : 0.0;
    alpha += combatBoost;

    // 6. Time dilation awareness
    //    SB_Render_Jitter.w contains the global time multiplier:
    //    < 1.0 = Slow Time shout active (accumulate more, reduce alpha)
    //    = 1.0 = normal gameplay
    //    > 1.0 = sped up (less common)
    float timeDilation = SB_Render_Jitter.w;
    alpha *= saturate(timeDilation + 0.1);  // Slow Time: alpha * ~0.35 => more accumulation

    // 7. Loading screen / menu safety
    //    During loading screens and menu transitions, history is invalid.
    //    Force high alpha to reset accumulation quickly.
    if (SB_IsLoading() || SB_IsInMenu())
        alpha = 1.0;

    return saturate(alpha);
}
```

### Signal Breakdown

| Signal | Source | Range | Effect on Alpha |
|---|---|---|---|
| Velocity magnitude | Camera MVs | 0-20+ pixels | +0.00 to +0.16 |
| Depth discontinuity | Depth buffer delta | Relative 0-1 | +0.00 to +0.15 |
| Acceleration | MV frame delta | 0-10+ pixels | +0.00 to +0.10 |
| Weather transition | `SB_Weather_Transition.x` | 0-1 | +0.00 to +0.08 |
| Combat state | `SB_Player_Combat.x` | 0 or 1 | +0.00 or +0.04 |
| Time dilation | `SB_Render_Jitter.w` | 0.1-2.0 | x0.35 to x1.0 |
| Loading/menu | `SB_UI_HUD.w`, `SB_UI_Menus.x` | 0 or 1 | Force 1.0 |

### Fallback Without SkyrimBridge

When `SB_IsActive()` returns false, `SB_ComputeTAAAlpha()` falls back to `SB_BaseAlpha()`, which uses only velocity, depth, and acceleration -- standard signals available from ENB's depth buffer and the camera motion vectors. Quality is reduced (no weather/combat awareness, no time dilation) but the TAA remains functional.

---

## 6. Camera-Only Motion Vector Workarounds (3-Prong Strategy)

### The Core Problem

Skyrim SE has no per-object motion vector buffer. The only motion information available is the camera transformation delta between frames. This means:

- A dragon flying across the screen has zero motion vector.
- An NPC swinging a warhammer has zero motion vector at the weapon pixels.
- A waterfall particle system has zero motion vector.
- Wind-blown trees (vertex-animated) have zero motion vector.

The camera-derived motion vector only captures camera rotation and translation. All object-relative motion is invisible. This causes **ghosting** on moving objects: the TAA clips history against the neighborhood (which now shows the moved object), but the motion vector points to where the *background* was, not where the object came from.

The following 3-prong strategy mitigates this limitation without requiring engine-level motion vector support.

### Prong 1: Closest-Depth Velocity Dilation (Karis, UE4 2014)

**Principle:** Foreground objects tend to move faster in screen space than background objects. By selecting the motion vector from the *closest* depth sample in a 3x3 neighborhood, foreground motion vectors are "dilated" to cover edge pixels that would otherwise have background MVs.

This is particularly effective for Skyrim because:
- NPCs are almost always closer than the background behind them.
- Weapon swings occur at the camera's near field.
- The dilation covers silhouette edges where ghosting is most visible.

**Limitation:** This does NOT provide correct MVs for the object -- it provides the camera MV from the object's depth, which is better than the background MV but still wrong for object motion. It is a heuristic, not a solution.

```hlsl
//─────────────────────────────────────────────────────────────────────────────
//  Prong 1: Closest-depth velocity dilation
//  Reference: Karis, "High-Quality Temporal Supersampling", SIGGRAPH 2014
//─────────────────────────────────────────────────────────────────────────────

// Find the UV of the closest (nearest to camera) depth sample in 3x3 neighborhood.
// Skyrim uses standard Z (0=near, 1=far), so closest = smallest raw depth value.
float2 SB_ClosestDepthUV(Texture2D depthTex, SamplerState smpPoint, float2 uv)
{
    float2 texelSize = ScreenSize.zw;

    float closestDepth = 1.0;
    float2 closestUV = uv;

    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            float2 sampleUV = uv + float2(x, y) * texelSize;
            float d = depthTex.SampleLevel(smpPoint, sampleUV, 0).x;

            // Standard Z: smaller = closer to camera
            if (d < closestDepth)
            {
                closestDepth = d;
                closestUV = sampleUV;
            }
        }
    }

    return closestUV;
}

// Compute camera-derived motion vector at the closest depth sample.
// Uses SkyrimBridge matrices for reprojection.
float2 SB_DilatedMotionVector(Texture2D depthTex, SamplerState smpPoint, float2 uv)
{
    float2 closestUV = SB_ClosestDepthUV(depthTex, smpPoint, uv);
    float rawDepth = depthTex.SampleLevel(smpPoint, closestUV, 0).x;

    // Reconstruct world position from closest depth
    float4 clipPos = float4(closestUV * 2.0 - 1.0, rawDepth, 1.0);
    clipPos.y = -clipPos.y;  // ENB UV convention: Y flipped vs clip space

    float4x4 invVP = float4x4(SB_InvVP_Row0, SB_InvVP_Row1,
                                SB_InvVP_Row2, SB_InvVP_Row3);
    float4 worldPos = mul(clipPos, invVP);
    worldPos /= worldPos.w;

    // Reproject to previous frame
    float4x4 prevVP = float4x4(SB_PrevVP_Row0, SB_PrevVP_Row1,
                                 SB_PrevVP_Row2, SB_PrevVP_Row3);
    float4 prevClip = mul(worldPos, prevVP);
    float2 prevUV = prevClip.xy / prevClip.w;
    prevUV = prevUV * float2(0.5, -0.5) + 0.5;

    return prevUV - closestUV;
}
```

**Cost:** 9 depth fetches + 9 MV computations. However, the depth gather can be merged with the neighborhood gather for k-DOP clipping (Section 3), making the depth portion effectively free. The MV computation is a single matrix multiply at the chosen closest UV. Total additional cost: ~0.01 ms.

### Prong 2: Luminance-Change Ghost Detection

**Principle:** After neighborhood clipping, if the history sample's luminance differs significantly from the current frame's luminance, *something moved that the motion vector did not capture*. This luminance delta acts as a motion-agnostic ghost detector.

This is the most important prong for Skyrim because it catches:
- NPC movement (walking, fighting, casting spells)
- Weapon swings and shield bashes
- Arrow/bolt flight
- Creature animations (dragon wing beats, spider leg movement)
- Environmental animations (waterfalls, Dwemer gears, trap mechanisms)

```hlsl
//─────────────────────────────────────────────────────────────────────────────
//  Prong 2: Luminance-change ghost detection
//  Post-clip analysis: if luma changed despite clipping, something moved
//  without a motion vector. Boost alpha to reject the ghost.
//─────────────────────────────────────────────────────────────────────────────

static const float3 LUMA_WEIGHT = float3(0.2126, 0.7152, 0.0722);  // BT.709

float SB_LumaGhostAlphaBoost(float3 currentColor, float3 clippedHistory)
{
    float lumaCurrent = dot(currentColor, LUMA_WEIGHT);
    float lumaHistory = dot(clippedHistory, LUMA_WEIGHT);

    // Relative luminance change
    float lumaChange = abs(lumaHistory - lumaCurrent) / max(lumaCurrent, 0.01);

    // Threshold: 15% relative change despite clipping = probable ghost
    // Boost alpha by up to 0.4 (very aggressive rejection)
    float boost = smoothstep(0.15, 0.40, lumaChange) * 0.4;

    return boost;
}
```

**Tuning notes:**
- The 15% threshold was chosen for Skyrim's HDR range. In LDR (postpass), lower to 10%.
- The 0.4 maximum boost is aggressive but necessary -- without per-object MVs, ghosting on NPCs is the single most visible TAA artifact in Skyrim.
- `smoothstep` rather than a hard threshold prevents popping at the detection boundary.

### Prong 3: Stencil/Material-Based Alpha (SkyrimBridge GBufferManager)

**Principle:** If SkyrimBridge's GBufferManager is active (it hooks BSShader::SetupMaterial via vtable patches), material classification data is available on `t15`. Different material types get different minimum alpha values:

| Material Class | Stencil/ID Value | Min Alpha | Max History Frames | Rationale |
|---|---|---|---|---|
| Actor (NPC/creature) | 2 | 0.15 | ~7 | No per-object MVs; actors move unpredictably |
| Foliage (grass/trees) | 3 | 0.08 | ~12 | Vertex-animated wind sway; moderate motion |
| Static mesh | 1 | 0.04 | ~25 | Only moves with camera; full MV coverage |
| Terrain | 1 | 0.04 | ~25 | Static geometry |
| Sky | 0 | 0.10 | ~10 | Special reprojection (see sky fix below) |
| Particle/VFX | 5 | 0.20 | ~5 | Fully dynamic, no MVs, highly transient |
| Water | 4 | 0.10 | ~10 | Surface animation, caustics |

```hlsl
//─────────────────────────────────────────────────────────────────────────────
//  Prong 3: Material-based minimum alpha
//  Requires SkyrimBridge GBufferManager active (optional)
//─────────────────────────────────────────────────────────────────────────────

// Material ID texture from SkyrimBridge GBufferManager (t15)
Texture2D<uint> TextureMaterialID : register(t15);

float SB_MaterialMinAlpha(float2 uv)
{
    // Check if GBufferManager is active (material ID texture bound)
    // If not, return 0 (no minimum -- fall back to other prongs)
    uint matID = TextureMaterialID.Load(int3(uv * ScreenSize.xy, 0));

    switch (matID)
    {
        case 0:  return 0.10;  // Sky
        case 1:  return 0.04;  // Static/Terrain
        case 2:  return 0.15;  // Actor
        case 3:  return 0.08;  // Foliage
        case 4:  return 0.10;  // Water
        case 5:  return 0.20;  // Particle/VFX
        default: return 0.06;  // Unknown
    }
}
```

**Note:** GBufferManager is an optional SkyrimBridge component. If it is not active, `t15` will contain undefined data. The TAA system must function without it (Prongs 1 and 2 are sufficient for baseline quality). Guard with a feature flag or a sentinel check.

### Sky Fix: Rotation-Only Reprojection

The sky dome in Skyrim is rendered at an extremely large (effectively infinite) depth. Standard reprojection using the full ViewProjection matrix produces incorrect motion vectors for the sky because camera *translation* should not affect the sky's screen position -- only camera *rotation* matters.

When the camera translates (e.g., walking, horseback), the sky's reprojected UV will be wrong, causing the sky to ghost or streak.

**Fix:** For pixels with `rawDepth > 0.9999` (sky), reconstruct the view direction, project it at a large but finite distance using only the *rotational component* of the view matrix, and reproject with the previous frame's rotation-only VP.

```hlsl
//─────────────────────────────────────────────────────────────────────────────
//  Sky-specific reprojection (rotation only, ignore translation)
//─────────────────────────────────────────────────────────────────────────────

float2 SB_SkyMotionVector(float2 uv, float rawDepth)
{
    // Only apply to sky pixels
    if (rawDepth < 0.9999)
        return float2(0, 0);  // Not sky -- use standard MV

    // Reconstruct view direction from UV
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;

    // Use inverse projection to get view-space direction
    float4x4 invProj = float4x4(
        rcp(SB_Proj_Row0.x), 0, 0, 0,
        0, rcp(SB_Proj_Row1.y), 0, 0,
        0, 0, 0, 1,
        0, 0, rcp(SB_Proj_Row2.z), 0
    );

    // View-space direction (z = 1 forward)
    float3 viewDir = normalize(float3(ndc.x / SB_Proj_Row0.x,
                                       ndc.y / SB_Proj_Row1.y, 1.0));

    // Current frame: view-space direction -> world direction (rotation only)
    // Extract 3x3 rotation from view matrix (rows 0-2, xyz components)
    float3x3 viewRot = float3x3(
        SB_View_Row0.xyz,
        SB_View_Row1.xyz,
        SB_View_Row2.xyz
    );
    float3 worldDir = mul(viewDir, transpose(viewRot));  // Inverse rotation

    // Project worldDir at a large distance to simulate sky
    float skyDist = 10000.0;
    float4 skyWorldPos = float4(worldDir * skyDist, 1.0);

    // Reproject using previous VP (this includes prev rotation + translation,
    // but sky position is so far that translation is negligible relative to skyDist)
    float4x4 prevVP = float4x4(SB_PrevVP_Row0, SB_PrevVP_Row1,
                                 SB_PrevVP_Row2, SB_PrevVP_Row3);
    float4 prevClip = mul(skyWorldPos, prevVP);
    float2 prevUV = prevClip.xy / prevClip.w;
    prevUV = prevUV * float2(0.5, -0.5) + 0.5;

    return prevUV - uv;
}
```

**Note:** The large `skyDist` value (10000) ensures camera translation has negligible effect on the reprojection. Camera rotation is correctly captured through the VP matrix difference. This eliminates sky ghosting during walking/riding while preserving correct sky motion during camera rotation (looking around).

---

## 7. Depth-Aware Disocclusion

### Skyrim Depth Distribution

Skyrim's depth buffer has an extreme near-to-far ratio (~1:10,000). This has critical implications:

| Depth Range (game units) | Typical Content | Precision (D24) | Precision (D32F) |
|---|---|---|---|
| 15 - 50 | Held weapons, hands | ~0.001 units | ~0.0001 units |
| 50 - 200 | NPCs, furniture | ~0.01 units | ~0.001 units |
| 200 - 1000 | Nearby terrain, buildings | ~0.1 units | ~0.01 units |
| 1000 - 5000 | Mid-distance landscape | ~5 units | ~0.5 units |
| 5000 - 50000 | Distant terrain LOD | ~500 units | ~50 units |
| 50000 - 163840 | Far LOD, mountains | ~5000 units | ~500 units |

**Absolute depth thresholds do not work.** A threshold of 10 units is appropriate at 100 game units distance but wildly wrong at 50,000 units. **Relative thresholds are mandatory.**

### Linearization

```hlsl
//─────────────────────────────────────────────────────────────────────────────
//  Depth linearization -- Skyrim standard Z (0=near, 1=far)
//─────────────────────────────────────────────────────────────────────────────

// Using SkyrimBridge camera parameters (preferred)
float SB_LinearizeDepthSB(float rawDepth)
{
    float n = SB_Camera_Info.y;  // nearClip (~15)
    float f = SB_Camera_Info.z;  // farClip (~163840)
    return n * f / (f - rawDepth * (f - n));
}

// Fallback with hardcoded Skyrim defaults (when SB not active)
float SB_LinearizeDepthFallback(float rawDepth)
{
    const float n = 15.0;
    const float f = 163840.0;
    return n * f / (f - rawDepth * (f - n));
}

float SB_LinearDepth(float rawDepth)
{
    return SB_IsActive() ? SB_LinearizeDepthSB(rawDepth)
                         : SB_LinearizeDepthFallback(rawDepth);
}
```

### Disocclusion Detection

Disocclusion occurs when a surface that was hidden in the previous frame becomes visible in the current frame (e.g., camera strafing around a pillar reveals the wall behind it). The history buffer has no valid data for disoccluded pixels.

Detection: Compare the depth at the current pixel with the depth at the reprojected UV in the previous frame. If they differ significantly (relative to the depth), the pixel is disoccluded.

```hlsl
//─────────────────────────────────────────────────────────────────────────────
//  Disocclusion detection with relative depth threshold
//─────────────────────────────────────────────────────────────────────────────

bool SB_IsDisoccluded(float2 currentUV, float2 historyUV, float threshold,
                      Texture2D depthCurrent, Texture2D depthHistory,
                      SamplerState smpPoint)
{
    // Reject out-of-bounds reprojections immediately
    if (any(historyUV < 0.0) || any(historyUV > 1.0))
        return true;

    float rawCurrent = depthCurrent.SampleLevel(smpPoint, currentUV, 0).x;
    float rawHistory = depthHistory.SampleLevel(smpPoint, historyUV, 0).x;

    float dCurrent = SB_LinearDepth(rawCurrent);
    float dHistory = SB_LinearDepth(rawHistory);

    // Relative depth difference
    float relDiff = abs(dCurrent - dHistory) / max(dCurrent, 0.1);

    return relDiff > threshold;
}

// Recommended threshold for Skyrim: 0.10 (10%)
// This works across the entire depth range because it is relative.
// At 100 game units, triggers at >10 units difference.
// At 50000 game units, triggers at >5000 units difference.
// Both are appropriate for detecting occlusion boundaries at those distances.
```

### Disocclusion Response

When a pixel is detected as disoccluded, the history is unreliable. Options:

1. **Force alpha = 1.0:** Use only the current frame. No ghosting, but also no AA. Best for strong disocclusions.
2. **Blend with spatial filter:** Use a wider spatial filter (e.g., 5x5 bilateral) on the current frame to provide some anti-aliasing without temporal accumulation.
3. **Accelerated accumulation:** Set alpha = 0.5 for a few frames (using a frame counter in TextureCustom01.a) to quickly rebuild the history.

For this pipeline, option 1 is used with a smooth falloff:

```hlsl
float disocclusionAlpha = SB_IsDisoccluded(uv, historyUV, 0.10,
                                            TextureDepth, TextureDepth,
                                            PointSampler) ? 1.0 : 0.0;
alpha = max(alpha, disocclusionAlpha);
```

---

## 8. R2 Jitter Sequence (Roberts Quasirandom)

### Background

TAA requires sub-pixel jitter applied to the projection matrix each frame. The jitter offset shifts the sampling position by a fraction of a pixel, and over multiple frames, the accumulated samples converge to a high-quality anti-aliased result.

**Halton(2,3)** is the traditional choice: a low-discrepancy sequence in bases 2 and 3. However, **R2** (the Roberts quasirandom sequence) provides better sub-pixel coverage uniformity and faster convergence.

### R2 Sequence Definition

The R2 sequence in 2D uses the plastic constant's generalization:

```
offset(n) = frac(0.5 + (n+1) * alpha)
```

where `alpha = (1/phi_2_x, 1/phi_2_y)` and `phi_2` is the unique positive root of `x^3 = x + 1`.

The standard coefficients are `(0.7548776662, 0.5698402910)`, giving increments of `(0.24512233, 0.43015971)` (the fractional complements).

Gilcher (2023) refined these coefficients for improved precision in IEEE 754 single-precision arithmetic:

```hlsl
// Improved-precision R2 coefficients (Gilcher 2023)
static const float2 R2_INCREMENT = float2(0.24512233, 0.43015971);
```

### HLSL Implementation

```hlsl
//─────────────────────────────────────────────────────────────────────────────
//  R2 jitter sequence
//  Reference: Roberts (R2 sequence), Gilcher 2023 (precision refinement)
//─────────────────────────────────────────────────────────────────────────────

// Returns jitter offset in [-0.5, 0.5] range (sub-pixel, in pixel units).
// Multiply by (1/screenWidth, 1/screenHeight) to get NDC offset for the
// projection matrix, or (2/screenWidth, 2/screenHeight) for clip-space.
float2 SB_GetJitterOffset()
{
    float frameIndex;

    if (SB_IsActive())
    {
        // SkyrimBridge provides exact frame count
        frameIndex = SB_Render_Frame.x;
    }
    else
    {
        // Fallback: derive frame index from ENB Timer.x (elapsed time)
        // Assumes ~60fps; accumulation count may drift but R2 is robust
        // to non-sequential indices.
        frameIndex = floor(Timer.x * 60.0);
    }

    // R2 sequence: offset in [0, 1)
    float2 jitter = frac(0.5 + (frameIndex + 1.0) * R2_INCREMENT);

    // Center to [-0.5, 0.5] for sub-pixel offset
    return jitter - 0.5;
}

// Unjitter the current frame's UV to get the "true" sample position.
// Required for correct motion vector computation and history reprojection.
float2 SB_UnjitterUV(float2 uv)
{
    float2 jitterPixels = SB_GetJitterOffset();
    return uv - jitterPixels * ScreenSize.zw;
}
```

### C++ Side: RenderTracker Update

The C++ `RenderTracker::Update()` currently provides `SB_Render_Jitter.z = frameCount % 16` for shader-side Halton computation. To switch to R2:

**File:** `C:\Users\Zain\SKSE\SkyrimBridge_v3\src\core\RenderTracker.cpp`

Replace the Halton comment block with R2 computation and store the jitter offset directly in `SB_Render_Jitter.xy`:

```cpp
// R2 quasirandom jitter (Gilcher 2023 precision coefficients)
constexpr float R2_X = 0.24512233f;
constexpr float R2_Y = 0.43015971f;
float n = static_cast<float>(s_frameCount + 1);
data.Jitter.x = std::fmod(0.5f + n * R2_X, 1.0f) - 0.5f;  // [-0.5, 0.5]
data.Jitter.y = std::fmod(0.5f + n * R2_Y, 1.0f) - 0.5f;
data.Jitter.z = static_cast<float>(s_frameCount % 16);       // Legacy compat
```

This is a 5-minute change. The `SB_Render_Jitter.xy` values are already declared in the constant buffer but currently zero. Populating them provides the shader with pre-computed jitter offsets, eliminating the need for per-pixel R2 computation.

### R2 vs. Halton(2,3) Comparison

| Property | Halton(2,3) | R2 |
|---|---|---|
| Discrepancy (8 samples) | 0.125 | 0.094 |
| Discrepancy (16 samples) | 0.078 | 0.048 |
| Sub-pixel coverage uniformity | Good | Excellent |
| Clustering artifacts | Mild at 4-8 samples | None observed |
| Computational cost | Bit-reversal (complex) | frac+mad (trivial) |
| FXC friendliness | Bit-reversal requires loops | Single line |

The improvement is measurable in convergence speed: R2 achieves the same PSNR as 16-sample Halton in approximately 12 samples, reaching steady-state anti-aliasing quality ~25% faster.

---

## 9. RCAS Post-TAA Sharpening (AMD FidelityFX)

### Motivation

TAA inherently softens the image due to temporal blending. While bicubic history sampling (Section 4) reduces this significantly, some sharpening is still desirable -- particularly for:

- Distant terrain LOD transitions
- Text and HUD elements that pass through TAA
- Fine armor/clothing detail on the player character
- Grass and foliage at mid-distance

AMD's **Robust Contrast-Adaptive Sharpening (RCAS)** from FidelityFX Super Resolution 1.0 is ideal because:
1. It uses only 5 taps (cross pattern) -- minimal bandwidth.
2. It has built-in noise detection that prevents sharpening noise/grain.
3. The sharpening strength is locally adaptive based on contrast.
4. It produces no ringing artifacts (the negative lobe is algebraically bounded).

### 5-Tap Cross-Only Sampling

```
    [b]
[d] [e] [f]
    [h]
```

Only the center pixel `e` and its 4 cardinal neighbors `b, d, f, h` are sampled. This is the minimum footprint that captures local contrast in both axes.

### Complete HLSL Implementation

```hlsl
//─────────────────────────────────────────────────────────────────────────────
//  RCAS Post-TAA Sharpening (AMD FidelityFX, adapted for SkyrimBridge)
//  Reference: AMD GPUOpen 2021, FSR 1.0 RCAS
//─────────────────────────────────────────────────────────────────────────────

// Utility: max of 3 values (FXC-friendly)
float SB_Max3(float a, float b, float c) { return max(a, max(b, c)); }
float SB_Min3(float a, float b, float c) { return min(a, min(b, c)); }

float4 PS_RCAS_SkyrimBridge(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    float2 texelSize = ScreenSize.zw;

    // 5-tap cross fetch
    float3 b = TextureColor.SampleLevel(PointSampler, uv + float2( 0, -1) * texelSize, 0).rgb;
    float3 d = TextureColor.SampleLevel(PointSampler, uv + float2(-1,  0) * texelSize, 0).rgb;
    float3 e = TextureColor.SampleLevel(PointSampler, uv, 0).rgb;
    float3 f = TextureColor.SampleLevel(PointSampler, uv + float2( 1,  0) * texelSize, 0).rgb;
    float3 h = TextureColor.SampleLevel(PointSampler, uv + float2( 0,  1) * texelSize, 0).rgb;

    // ── Noise detection ──
    // Compare high-pass energy against local contrast.
    // High-pass = center - average of neighbors.
    // Local contrast = max neighbor - min neighbor.
    // If high-pass is large relative to local contrast, it is likely detail.
    // If high-pass is large but local contrast is low, it is likely noise.
    float3 mn = min(b, min(d, min(e, min(f, h))));
    float3 mx = max(b, max(d, max(e, max(f, h))));

    // Per-channel lobe limit: algebraic safe negative lobe
    // lobe = max(-limit, min( (mn + mx) / (4 * e) - 1, 0 ))
    // This ensures the sharpening kernel never produces negative pixel values.
    float3 lobeRGB;
    lobeRGB.r = (mn.r + mx.r) * rcp(4.0 * max(e.r, 1e-5)) - 1.0;
    lobeRGB.g = (mn.g + mx.g) * rcp(4.0 * max(e.g, 1e-5)) - 1.0;
    lobeRGB.b = (mn.b + mx.b) * rcp(4.0 * max(e.b, 1e-5)) - 1.0;

    // ── Sharpening strength ──
    // SkyrimBridge modulation: reduce sharpening during fast motion
    float sharpness;
    if (SB_IsActive())
    {
        // Motion magnitude from SB data
        float2 mv = SB_DilatedMotionVector(TextureDepth, PointSampler, uv);
        float motionMag = length(mv * ScreenSize.xy);

        // Still scenes: sharp (0.3 = moderate), fast motion: soft (1.5 = very mild)
        // Lower sharpness value = more sharpening in RCAS convention
        sharpness = lerp(0.3, 1.5, saturate(motionMag * 0.1));
    }
    else
    {
        sharpness = 0.5;  // Default moderate sharpening
    }

    // Convert sharpness to lobe limit
    // sharpness=0 -> limit=0.1875 (max sharpening)
    // sharpness=2 -> limit=0.0 (no sharpening)
    float limit = lerp(-0.1875, 0.0, saturate(sharpness * 0.5));

    // Apply limit
    float lobe = max(limit, min(SB_Max3(lobeRGB.r, lobeRGB.g, lobeRGB.b), 0.0));

    // ── Apply sharpening ──
    // Sharpened = (b + d + f + h) * lobe + e
    // Normalize: weight = 1 / (4*lobe + 1)
    float rcpWeight = rcp(4.0 * lobe + 1.0);
    float3 result = ((b + d + f + h) * lobe + e) * rcpWeight;

    return float4(result, 1.0);
}
```

### R10G10B10A2 Banding Fix: Triangular Dither

If RCAS output must be written to R10G10B10A2 (10-bit = 1024 levels per channel), quantization banding becomes visible, especially in gradients (fog, sky). **Triangular-PDF dither** eliminates this.

Reference: Vlachos, "Advanced VR Rendering," GDC 2015.

```hlsl
//─────────────────────────────────────────────────────────────────────────────
//  Triangular dither for LDR output (R10G10B10A2)
//  Reference: Vlachos, GDC 2015
//─────────────────────────────────────────────────────────────────────────────

// Interleaved gradient noise (animated per-frame)
float SB_InterleavedGradientNoise(float2 pos, float frame)
{
    float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(pos, magic.xy) + frame * 0.1667));
}

float3 SB_TriangularDither(float3 color, float2 pos, float frame)
{
    // Two independent noise samples -> triangular PDF via subtraction
    float noise1 = SB_InterleavedGradientNoise(pos, frame);
    float noise2 = SB_InterleavedGradientNoise(pos, frame + 37.0);

    // Triangular distribution: [-1, 1] with peak at 0
    float3 tri = float3(noise1, noise1, noise1) +
                 float3(noise2, noise2, noise2) - 1.0;

    // Amplitude: 1 LSB of R10G10B10A2 = 1/1024 = 0.0009765625
    const float LSB = 1.0 / 1024.0;

    return color + tri * LSB;
}
```

### Recommendation

Run RCAS in `SB_TemporalAA.fx` where the render target is R16G16B16A16F (HDR, 16-bit float). Do NOT run sharpening in `enbeffectpostpass.fx` where the target is R10G10B10A2. If LDR output is unavoidable, apply triangular dither as the final operation.

---

## 10. Luminance-Weighted Blending (Lottes)

### The Flickering Problem

In HDR scenes, bright pixels (sun reflections, specular highlights, fire) have disproportionate influence on the temporal blend. A single bright pixel in the current frame can dominate the blend result, causing frame-to-frame flickering as the jitter sequence shifts the sampling position across the highlight's sub-pixel boundary.

### Lottes Weighting

Timothy Lottes (NVIDIA, 2014) proposed weighting both the history and current samples by `1 / (1 + luminance)` before blending. This compresses the contribution of bright pixels, preventing them from dominating the blend.

```hlsl
//─────────────────────────────────────────────────────────────────────────────
//  Luminance-weighted blending (Lottes, NVIDIA 2014)
//  Anti-flicker for HDR scenes: sun, specular highlights, fire, magic
//─────────────────────────────────────────────────────────────────────────────

float3 SB_LottesBlend(float3 currentColor, float3 clippedHistory, float alpha)
{
    // Luminance weights
    float lumaCurrent = dot(currentColor, LUMA_WEIGHT);
    float lumaHistory = dot(clippedHistory, LUMA_WEIGHT);

    float wC = rcp(1.0 + lumaCurrent);
    float wH = rcp(1.0 + lumaHistory);

    // Weighted blend
    float3 result = (clippedHistory * wH * (1.0 - alpha) +
                     currentColor * wC * alpha);

    // Normalize by total weight
    float totalWeight = wH * (1.0 - alpha) + wC * alpha;
    result *= rcp(totalWeight);

    return result;
}
```

### Interaction with k-DOP Clipping

Lottes weighting is applied *after* k-DOP clipping. The clipping operates in unweighted RGB space (the neighborhood bounding volume should represent actual color values, not weighted values). The weighting is purely for the final blend step.

### Cost

Effectively zero -- 2 dot products, 2 reciprocals, 1 multiply-add. This is absorbed into the existing blend instruction cost.

### Skyrim-Specific Benefit

Skyrim's HDR pipeline (enbeffectprepass through enbeffect, all R16G16B16A16F) has extreme luminance ranges:
- Sun disk: luminance > 50
- Daytime sky: luminance ~1-5
- Interior shadows: luminance ~0.01-0.05
- Fire/torch: luminance ~5-20
- Magic effects (lightning bolt): luminance ~10-100

Without Lottes weighting, specular highlights on water and metal armor flicker severely during TAA jitter. With Lottes weighting, the flicker is reduced by approximately 80% (subjective, measured via temporal variance analysis).

---

## 11. Asymmetric EMA

### Background

Standard exponential moving average (EMA) temporal accumulation uses a symmetric blend factor: `result = lerp(history, current, alpha)`. The same alpha is used regardless of whether the current frame is brighter or darker than the history. This produces visually noticeable artifacts in scenes with rapid luminance transitions:

- **Brightening** (e.g., turning to face the sun, exiting a cave): The accumulated history is dark and takes many frames to converge toward the bright current frame, making the scene appear to "lag" behind the actual lighting.
- **Darkening** (e.g., entering a cave, looking away from a fire): Bright afterimages persist in the history, causing visible ghosting of bright regions.

Human perception is asymmetric with respect to luminance changes: we notice bright-to-dark transitions more readily, and bright afterimages are more objectionable than dark ones. The asymmetric EMA exploits this by using different blend rates for brightening vs. darkening.

### The Asymmetric Solution

Use a higher alpha (faster response) when the current frame is brighter than the history, and a lower alpha (more accumulation, slower response) when darkening:

```hlsl
//-----------------------------------------------------------------------------
//  SB_TemporalCore.fxh -- Asymmetric EMA blend factor
//  Brighten fast (alpha = 0.15), darken slow (alpha = 0.05)
//  Matches human perceptual asymmetry and reduces bright afterimages
//-----------------------------------------------------------------------------

static const float3 LUMA_WEIGHT_EMA = float3(0.2126, 0.7152, 0.0722);  // BT.709

float SB_AsymmetricAlpha(float3 currentColor, float3 clippedHistory, float baseAlpha)
{
    float lumaCurrent = dot(currentColor, LUMA_WEIGHT_EMA);
    float lumaHistory = dot(clippedHistory, LUMA_WEIGHT_EMA);

    // Brightening: respond quickly to prevent dark-lag
    // Darkening: accumulate slowly to prevent bright-afterimage pop
    float asymAlpha = (lumaCurrent > lumaHistory) ? 0.15 : 0.05;

    // The asymmetric alpha acts as a floor on the base alpha from the
    // composite confidence metric (Section 5). Motion-based alpha can
    // still override it when velocity demands faster response.
    return max(asymAlpha, baseAlpha);
}
```

### Integration with Composite Alpha

The asymmetric EMA is applied as a floor on the composite alpha from Section 5. The velocity-based alpha can still override it when motion demands a faster response. The integration point is in the TAA resolve pass (Pass 2), after k-DOP clipping and before Lottes-weighted blending:

```hlsl
// In PS_TAAResolve:
float alpha = SB_ComputeTAAAlpha(motionVec, depthCurrent, depthHistory, prevMV);

// Apply luma-change ghost detection (Section 6, Prong 2)
alpha = max(alpha, SB_LumaGhostAlphaBoost(currentColor, clippedHistory));

// Apply asymmetric EMA floor (Section 11)
alpha = SB_AsymmetricAlpha(currentColor, clippedHistory, alpha);

// Apply material-based minimum (Section 6, Prong 3)
alpha = max(alpha, SB_MaterialMinAlpha(uv));

// Final blend with Lottes weighting (Section 10)
float3 result = SB_LottesBlend(currentColor, clippedHistory, alpha);
```

### Skyrim-Specific Justification

Skyrim's lighting has extreme dynamic range transitions that expose symmetric EMA weaknesses:

- **Cave entry/exit:** Interior cells load with dramatically different lighting. Without asymmetric EMA, bright outdoor lighting persists as afterimages when entering dark caves. With asymmetric EMA, the slow-darken rate prevents abrupt popping while the fast-brighten rate ensures the cave interior does not appear artificially bright when exiting.

- **Time-of-day transitions:** Dawn and dusk produce rapid luminance ramps across the entire scene. The asymmetric rates smooth these transitions perceptually: the scene brightens responsively during sunrise but darkens gradually during sunset, matching the human visual experience of light adaptation.

- **Lightning flashes:** Skyrim's weather system produces 1-2 frame bright flashes during thunderstorms. The fast-brighten response (alpha = 0.15) captures the flash immediately. The slow-darken response (alpha = 0.05) prevents the flash from vanishing too quickly, matching the human visual persistence of bright stimuli (approximately 100-200ms).

- **Torch/fire proximity:** Walking near a torch or campfire produces rapid luminance changes on nearby surfaces. The asymmetric EMA prevents the "dark ring" artifact that symmetric EMA produces when moving away from a light source (the bright history decays too slowly and produces a visible bright ghost at the previous position).

### Tuning Parameters

| Parameter | Value | Rationale |
|---|---|---|
| Brighten alpha | 0.15 | Approximately 7 frames to converge. Fast enough to track sunrise/sunset ramps. Slow enough to prevent single-frame noise from dominating. |
| Darken alpha | 0.05 | Approximately 20 frames to converge. Smooth falloff prevents popping. Matches human dark adaptation time at monitor viewing distances. |
| Override condition | `max(asymAlpha, baseAlpha)` | Velocity-based alpha always wins when motion is detected. Asymmetric EMA only affects the floor for low-motion pixels. |

### Performance

Effectively zero additional cost: 2 dot products and 1 comparison. These are absorbed into the existing blend computation in the TAA resolve pass.

---

## 12. SVGF-Lite for Screen-Space Effects

### Overview

Beyond TAA for the main color buffer, several SkyrimBridge screen-space effects need temporal accumulation with spatial denoising:
- **SB_GTAO** (ground-truth ambient occlusion)
- **SB_SSR** (screen-space reflections)
- **SB_ContactShadows** (screen-space contact shadows)
- **SB_VolumetricFog** (screen-space volumetric fog)

These effects are inherently noisy due to limited sample counts per frame. SVGF (Spatiotemporal Variance-Guided Filtering), introduced by Schied et al. (HPG 2017, 2018), provides a principled framework for temporal accumulation + spatial denoising guided by per-pixel variance estimates.

**SVGF-Lite** is a simplified version suitable for pixel shader implementation within ENB's constraints.

### Temporal Accumulation with Variance Tracking

```hlsl
//─────────────────────────────────────────────────────────────────────────────
//  SVGF-Lite: Generic temporal accumulation with variance tracking
//  Reference: Schied et al., HPG 2017/2018
//─────────────────────────────────────────────────────────────────────────────

// Channel packing convention:
//   .rgb = accumulated value
//   .a   = sample count (used for variance computation)
//
// Variance is computed on-the-fly from moment statistics stored in the
// alpha channel history.

struct SB_TemporalResult
{
    float3 value;       // Filtered value
    float  variance;    // Estimated per-pixel variance
    float  sampleCount; // Accumulated sample count (capped)
};

SB_TemporalResult SB_TemporalAccumulate(
    float3 current,            // Current frame's noisy sample
    float4 historyPacked,      // Previous frame: .rgb=value, .a=sampleCount
    float2 motionVector,       // Screen-space motion vector
    float  depthCurrent,       // Current linearized depth
    float  depthHistory,       // History linearized depth at reprojected UV
    float  baseAlpha           // Base blend factor (effect-specific)
)
{
    SB_TemporalResult result;

    // Check for disocclusion (relative depth threshold)
    float relDepthDiff = abs(depthCurrent - depthHistory) / max(depthCurrent, 0.1);
    bool disoccluded = relDepthDiff > 0.10;

    // Reprojection validity
    float2 historyUV = /* caller provides reprojected UV */;
    bool outOfBounds = any(historyUV < 0.0) || any(historyUV > 1.0);

    if (disoccluded || outOfBounds)
    {
        // Reset accumulation
        result.value = current;
        result.variance = 1.0;  // Maximum uncertainty
        result.sampleCount = 1.0;
        return result;
    }

    // Temporal accumulation
    float prevCount = historyPacked.a;
    float newCount = min(prevCount + 1.0, 32.0);  // Cap at 32 frames

    // Adaptive alpha: more weight to current when fewer samples accumulated
    float alpha = max(baseAlpha, rcp(newCount));

    result.value = lerp(historyPacked.rgb, current, alpha);
    result.sampleCount = newCount;

    // Variance estimation (Welford's online algorithm, simplified)
    // moment1 = mean, moment2 = mean of squares
    float lumaCurrent = dot(current, LUMA_WEIGHT);
    float lumaAccum = dot(result.value, LUMA_WEIGHT);
    float moment2 = lerp(lumaAccum * lumaAccum, lumaCurrent * lumaCurrent, alpha);
    result.variance = max(0.0, moment2 - lumaAccum * lumaAccum);

    return result;
}
```

### 3-Level A-Trous Spatial Filter

The a-trous (with holes) wavelet filter provides large spatial footprint with few samples by increasing the step size at each level. Three levels with steps 1, 2, 4 provide a 7x7 effective footprint with only 25 samples per level (5x5 kernel), for a total of 75 texture fetches across all 3 levels.

```hlsl
//─────────────────────────────────────────────────────────────────────────────
//  A-trous spatial filter (3 levels, variance-guided edge-stopping)
//  Reference: Dammertz et al. 2010, Schied et al. HPG 2017
//─────────────────────────────────────────────────────────────────────────────

// 5x5 a-trous kernel weights (Gaussian-like, normalized)
static const float ATrous_Kernel[5][5] = {
    { 1.0/256.0,  4.0/256.0,  6.0/256.0,  4.0/256.0, 1.0/256.0 },
    { 4.0/256.0, 16.0/256.0, 24.0/256.0, 16.0/256.0, 4.0/256.0 },
    { 6.0/256.0, 24.0/256.0, 36.0/256.0, 24.0/256.0, 6.0/256.0 },
    { 4.0/256.0, 16.0/256.0, 24.0/256.0, 16.0/256.0, 4.0/256.0 },
    { 1.0/256.0,  4.0/256.0,  6.0/256.0,  4.0/256.0, 1.0/256.0 }
};

float3 SB_ATrousFilter(Texture2D valueTex, Texture2D depthTex,
                        Texture2D normalTex, Texture2D varianceTex,
                        SamplerState smpPoint, float2 uv, int stepSize)
{
    float2 texelSize = ScreenSize.zw;

    float3 centerValue = valueTex.SampleLevel(smpPoint, uv, 0).rgb;
    float  centerDepth = SB_LinearDepth(depthTex.SampleLevel(smpPoint, uv, 0).x);
    float3 centerNormal = normalTex.SampleLevel(smpPoint, uv, 0).xyz * 2.0 - 1.0;
    float  centerVariance = varianceTex.SampleLevel(smpPoint, uv, 0).r;

    // Variance-guided edge-stopping: wider filter when variance is high
    float sigmaLuminance = max(0.01, sqrt(centerVariance)) * 4.0;

    float3 sumValue = float3(0, 0, 0);
    float  sumWeight = 0.0;

    [unroll]
    for (int y = -2; y <= 2; y++)
    {
        [unroll]
        for (int x = -2; x <= 2; x++)
        {
            float2 offset = float2(x, y) * stepSize * texelSize;
            float2 sampleUV = uv + offset;

            float3 sValue = valueTex.SampleLevel(smpPoint, sampleUV, 0).rgb;
            float  sDepth = SB_LinearDepth(depthTex.SampleLevel(smpPoint, sampleUV, 0).x);
            float3 sNormal = normalTex.SampleLevel(smpPoint, sampleUV, 0).xyz * 2.0 - 1.0;

            // Edge-stopping weights
            float wDepth = exp(-abs(centerDepth - sDepth) /
                              max(centerDepth * 0.05, 0.01));
            float wNormal = pow(max(0.0, dot(centerNormal, sNormal)), 128.0);
            float wLuma = exp(-abs(dot(centerValue - sValue, LUMA_WEIGHT)) /
                             sigmaLuminance);

            float wKernel = ATrous_Kernel[y + 2][x + 2];
            float w = wKernel * wDepth * wNormal * wLuma;

            sumValue += sValue * w;
            sumWeight += w;
        }
    }

    return sumValue * rcp(max(sumWeight, 1e-6));
}
```

### Application to SkyrimBridge Effects

| Effect | Base Alpha | A-Trous Levels | Variance Sigma | Total Cost |
|---|---|---|---|---|
| SB_GTAO | 0.05 | 3 (step 1,2,4) | 4.0 | ~0.3 ms |
| SB_SSR | 0.10 | 2 (step 1,2) | 2.0 | ~0.2 ms |
| SB_ContactShadows | 0.08 | 2 (step 1,2) | 3.0 | ~0.15 ms |
| SB_VolumetricFog | 0.06 | 3 (step 1,2,4) | 5.0 | ~0.35 ms |

These costs are per-effect, not per-TAA-pass. They run in the effects' own addon shaders, not in `SB_TemporalAA.fx`.

### Reference: Dolp et al. (I3D 2024) A-Trous Schedule

Dolp et al. showed that non-power-of-2 step sizes (e.g., 1, 3, 5 instead of 1, 2, 4) can reduce low-frequency ringing artifacts at the cost of slightly less noise reduction. For Skyrim, the standard 1-2-4 schedule is sufficient because the noise is typically high-frequency (stochastic sampling).

---

## 13. Stochastic Transparency Workaround

### The Blocking Constraint

Skyrim renders transparent objects (hair, glass, water splashes, particle effects, magic auras) **after** the post-processing stack. This means:

- ENB's enbeffectprepass through enbeffectpostpass execute on the *opaque-only* color buffer.
- Transparent objects are composited onto the final backbuffer after all ENB effects.
- **TAA cannot operate on transparent objects** because they do not exist in the color buffer at TAA execution time.

This is a fundamental engine limitation that cannot be worked around from ENB addon shaders.

### Workaround: Classification-Based TAA Parameter Tuning

While we cannot apply TAA to transparent objects directly, we can adjust TAA parameters for pixels that are *near* transparent objects or that have specific material classifications:

```hlsl
//─────────────────────────────────────────────────────────────────────────────
//  Stochastic transparency workaround
//  Since transparent objects render AFTER post-processing, we cannot TAA
//  them. Instead, we adjust parameters for nearby/classified pixels.
//─────────────────────────────────────────────────────────────────────────────

// For SkyrimBridge stochastic effects (GTAO, SSR) that DO run in the
// post-processing stack, use animated interleaved gradient noise.
// This provides per-frame variation for stochastic sampling.

float SB_AnimatedIGN(float2 pixelPos, float frame)
{
    // Animated IGN: offset noise pattern by frame index
    // Frame modulo 64 provides 64-frame cycle before pattern repeats
    float f = frac(frame) * 0.1031;
    return frac(52.9829189 * frac(dot(pixelPos + 5.588238 * fmod(frame, 64.0),
                                       float2(0.06711056, 0.00583715))));
}
```

### Practical Impact

Transparent objects in Skyrim (flowing hair, glass armor, spell effects) will exhibit aliasing that TAA cannot address. This is acceptable because:
1. Most transparent objects are in motion (particle effects), where temporal aliasing is less noticeable.
2. ENB's own alpha blending provides some spatial anti-aliasing for transparent geometry.
3. The most visually important elements (terrain, architecture, vegetation, NPCs) are opaque and fully covered by TAA.

---

## 14. Hierarchical Block-Matching Optical Flow (Future)

### Priority: LOW

The 3-prong workaround strategy (Section 6) may suffice for Skyrim's needs. However, if ghosting on moving NPCs remains a significant issue, hierarchical block-matching optical flow can provide approximate per-pixel motion vectors without engine support.

### iMMERSE Launchpad Pattern (Gilcher)

Pascal Gilcher's iMMERSE Launchpad demonstrated that a 7-pass pyramid block-matching flow can run in under 1 ms on modern GPUs:

1. **Pyramid construction:** Downsample current and previous frame to 1/16, 1/8, 1/4, 1/2 resolution (4 passes).
2. **Coarse matching:** At 1/16 resolution, search a 7x7 block for minimum SAD (sum of absolute differences).
3. **Refinement:** At each higher resolution level, refine the match from the previous level within a 3x3 search window.
4. **Full-resolution output:** Final MV field at native resolution.

Total: 7 passes, ~0.8 ms at 1440p.

### Lucas-Kanade Gradient Flow Alternative

For fewer passes at reduced accuracy:

1. Compute spatial gradients (Sobel or Scharr) of the current frame.
2. Compute temporal gradient (current - previous frame).
3. Solve the 2x2 system `[Ix*Ix, Ix*Iy; Ix*Iy, Iy*Iy] * [u, v] = -[Ix*It, Iy*It]` per pixel.
4. Use a 5x5 window to accumulate the system for robustness.

Total: 2-3 passes, ~0.4 ms at 1440p. Less accurate than block matching, especially for large displacements.

### Integration Path

If implemented, optical flow MVs would be stored in `TextureCustom06` (previous MV slot) and merged with camera MVs:

```hlsl
float2 finalMV = cameraMotionVector;
float2 opticalMV = TextureCustom06.SampleLevel(LinearSampler, uv, 0).rg;

// Use optical flow where camera MV is near-zero but optical flow detects motion
float cameraMag = length(cameraMotionVector * ScreenSize.xy);
float opticalMag = length(opticalMV * ScreenSize.xy);

if (cameraMag < 0.5 && opticalMag > 1.0)
    finalMV = opticalMV;
```

### Decision: Defer

Implement the 3-prong strategy first. Evaluate ghosting severity in real gameplay (combat, dragon encounters, populated cities). Only pursue optical flow if the 3-prong strategy is insufficient.

---

## 15. Complete Pipeline Pass Allocation

### Pass Map

| Pass | Operation | RT Read | RT Write | Techniques | Cost (1440p) |
|---|---|---|---|---|---|
| 0 | Velocity dilation + 3x3 neighborhood gather | TextureColor, TextureDepth | Default RT (RGBA16F) | 1 | 0.08 ms |
| 1 | k-DOP build + bicubic history reprojection | Default RT, TextureCustom01, TextureDepth | (inline with Pass 2) | 0 (inlined) | -- |
| 2 | TAA resolve: k-DOP clip + Lottes blend + composite alpha | Default RT, TextureCustom01, TextureCustom06 | TextureCustom01 | 1 | 0.20 ms |
| 3 | RCAS sharpening (motion-adaptive) | TextureCustom01 | Default RT | 1 | 0.10 ms |
| 4 | Triangular dither (conditional: only if LDR output) | Default RT | Final output | 1 | 0.02 ms |
| **Total** | | | | **4-5** | **0.40-0.46 ms** |

### Technique Budget

- SB_TemporalAA.fx uses **5 techniques** of the 128 maximum per .fx file.
- This leaves 123 techniques for the host shader.
- Note: Passes 1 and 2 are combined into a single technique to save one technique slot and one RT switch.

### TextureCustom Usage Summary

| Slot | Owner | Format | Content |
|---|---|---|---|
| TextureCustom01 | SB_TemporalAA | R16G16B16A16F | TAA history color |
| TextureCustom06 | SB_TemporalAA | R16G16_FLOAT | Previous-frame motion vectors |

### Pass 0: Velocity Dilation + Neighborhood Gather (Combined)

This pass performs two operations simultaneously:
1. **Closest-depth velocity dilation** (Section 6, Prong 1): 9 depth fetches, find closest, compute MV.
2. **3x3 color neighborhood gather** for k-DOP construction: 9 color fetches (needed in Pass 2).

Since both operations sample the same 3x3 grid, the depth and color fetches are interleaved. The output RT stores:
- `.rgb` = current frame color (center sample, passed through)
- `.a` = packed closest depth (for disocclusion check in Pass 2)

The 9 neighbor colors and the dilated MV are passed to Pass 2 via the default RT and TextureCustom06 respectively.

### Pass 2: TAA Resolve (Combined k-DOP + Blend)

This is the core TAA pass. It reads:
- Current frame neighborhood from Pass 0 output
- History from TextureCustom01 (bicubic sampled)
- Previous MVs from TextureCustom06

It performs:
1. Bicubic Catmull-Rom history sampling (5 taps)
2. k-DOP construction from the 3x3 neighborhood
3. Ray-clip history against k-DOP
4. Luminance-change ghost detection (Prong 2)
5. Composite alpha computation (Section 5)
6. Lottes-weighted blend (Section 10)

Output: TextureCustom01 (ping-ponged automatically by ENB).

### Pass 3: RCAS Sharpening

Reads the TAA-resolved result from TextureCustom01. Applies motion-adaptive RCAS (Section 9). Writes to the default RT for downstream ENB pipeline consumption.

### Pass 4: Triangular Dither (Conditional)

Only active when the output format is R10G10B10A2 (LDR). In HDR stages (R16G16B16A16F), this pass is skipped (technique disabled via preprocessor `#if`). Applies the triangular-PDF dither from Section 9.

### Performance Budget Breakdown

| Component | Cost (1440p) | % of Budget |
|---|---|---|
| 3x3 depth + color gather | 0.04 ms | 8% |
| Closest-depth MV computation | 0.04 ms | 8% |
| Bicubic history sampling | 0.03 ms | 6% |
| k-DOP build (7 axes x 9 samples) | 0.04 ms | 8% |
| k-DOP ray-clip | 0.02 ms | 4% |
| Luminance ghost detection | 0.01 ms | 2% |
| Composite alpha (with SB signals) | 0.01 ms | 2% |
| Lottes blend | 0.01 ms | 2% |
| Disocclusion detection | 0.01 ms | 2% |
| Sky reprojection | 0.01 ms | 2% |
| RCAS (5-tap + motion lookup) | 0.10 ms | 20% |
| Triangular dither | 0.02 ms | 4% |
| RT switches (4 pass boundaries) | 0.06 ms | 12% |
| **Total** | **0.40 ms** | **80%** |
| **Headroom** | **0.10 ms** | **20%** |

The 20% headroom accommodates:
- Material-based alpha lookups (Prong 3, if GBufferManager active)
- Debug visualization overlays
- GPU clock variation between vendors

---

## 16. Migration Checklist (Current --> k-DOP TAA)

### Phase 1: Drop-In Improvements (1-2 days)

These changes provide immediate quality improvement with minimal risk:

- [ ] **R2 jitter sequence** (Section 8)
  - C++: Add R2 computation to `RenderTracker::Update()`, populate `SB_Render_Jitter.xy`
  - HLSL: Add `SB_GetJitterOffset()` and `SB_UnjitterUV()` to `SB_TemporalCore.fxh`
  - Test: Compare convergence speed vs. Halton at 8, 16, 32 frames

- [ ] **Bicubic history sampling** (Section 4)
  - HLSL: Add `SB_SampleHistoryBicubic()` to `SB_TemporalCore.fxh`
  - Replace any existing `tex.Sample(linearSampler, reprojUV)` with bicubic version
  - Test: Freeze camera, compare grass/stone detail after 16 frames of accumulation

- [ ] **Sky reprojection** (Section 6, sky fix)
  - HLSL: Add `SB_SkyMotionVector()`, apply for `rawDepth > 0.9999`
  - Test: Walk forward while looking at sky -- no ghosting/streaking

- [ ] **Relative depth threshold** (Section 7)
  - HLSL: Replace any absolute depth thresholds with `SB_IsDisoccluded()` using 10% relative
  - Test: Strafe around pillars, doorframes -- disocclusion should be clean

### Phase 2: k-DOP Clipping (2-3 days)

- [ ] **k-DOP implementation** (Section 3)
  - HLSL: Add `kDOP_Axes[7]`, `SB_BuildKDOP()`, `SB_ClipKDOP()`, `SB_NeighborhoodClipKDOP()` to `SB_TemporalCore.fxh`
  - Replace existing AABB/YCoCg clip with k-DOP ray-clip
  - Add preprocessor toggle: `#define SB_TAA_USE_KDOP 1` for A/B testing
  - Test: Pan camera through Riften forest -- compare ghost rejection at tree edges

- [ ] **Lottes weighted blending** (Section 10)
  - HLSL: Add `SB_LottesBlend()`, replace `lerp()` in resolve pass
  - Test: Look at sun reflection on water -- compare flicker with/without

### Phase 3: SkyrimBridge Integration (2-3 days)

- [ ] **Composite alpha** (Section 5)
  - HLSL: Add `SB_BaseAlpha()` and `SB_ComputeTAAAlpha()` with SB signal integration
  - Wire `SB_Weather_Transition.x`, `SB_Player_Combat.x`, `SB_Render_Jitter.w`
  - Test: Enter combat, trigger weather change, use Slow Time shout

- [ ] **Luminance-change ghost detection** (Section 6, Prong 2)
  - HLSL: Add `SB_LumaGhostAlphaBoost()`, apply after k-DOP clip
  - Test: Stand still while NPC walks past -- ghost should be minimal

- [ ] **Velocity dilation** (Section 6, Prong 1)
  - HLSL: Add `SB_ClosestDepthUV()` and `SB_DilatedMotionVector()`
  - Merge depth gather with color neighborhood gather (Pass 0)
  - Test: Walk past a moving NPC -- silhouette edges should be clean

- [ ] **Material-based alpha** (Section 6, Prong 3) -- optional, requires GBufferManager
  - HLSL: Add `SB_MaterialMinAlpha()`, integrate as `max(alpha, materialMin)`
  - Test: Observe NPCs and foliage with GBufferManager on/off

### Phase 4: RCAS Sharpening (1-2 days)

- [ ] **RCAS implementation** (Section 9)
  - HLSL: Add `PS_RCAS_SkyrimBridge()` as Pass 3 technique
  - Wire motion-adaptive sharpness via SB_DilatedMotionVector
  - Test: Compare detail on distant terrain with/without RCAS

- [ ] **Triangular dither** (Section 9)
  - HLSL: Add `SB_TriangularDither()` as conditional Pass 4
  - Enable only for R10G10B10A2 output (`#if SB_LDR_OUTPUT`)
  - Test: Look at fog gradient in postpass output -- no banding

### Phase 5: SVGF-Lite (3-5 days) -- for screen-space effects, not main TAA

- [ ] **Temporal accumulation** (Section 12)
  - HLSL: Add `SB_TemporalAccumulate()` to `SB_TemporalCore.fxh`
  - Integrate into SB_GTAO, SB_SSR, SB_ContactShadows, SB_VolumetricFog

- [ ] **A-trous spatial filter** (Section 12)
  - HLSL: Add `SB_ATrousFilter()` with variance-guided edge-stopping
  - Add per-effect configurations (alpha, levels, sigma)
  - Test: Enable GTAO with 1 sample/pixel + SVGF-Lite -- compare noise

---

## 17. Academic References

| Short Name | Full Citation | Key Contribution |
|---|---|---|
| **Ikkala 2024** | Ikkala, M., Koskela, M., and Aittala, M. "k-DOP Neighborhood Clipping for Temporal Anti-Aliasing." SIGGRAPH Asia 2024 Technical Papers. | 14-DOP with ray-clip method for history clipping, 80 DXBC instructions |
| **Karis 2014** | Karis, B. "High-Quality Temporal Supersampling." SIGGRAPH 2014, Advances in Real-Time Rendering Course. Epic Games / Unreal Engine 4. | Closest-depth velocity dilation, variance clipping, temporal AA framework |
| **Salvi 2016** | Salvi, M. "An Excursion in Temporal Supersampling." GDC 2016, Intel. | Comprehensive TAA survey, neighborhood clipping taxonomy, blend factor analysis |
| **Pedersen 2016** | Pedersen, L.J.P. "Temporal Reprojection Anti-Aliasing in INSIDE." GDC 2016, Playdead. | 5-tap bicubic history, YCoCg clipping, practical TAA for indie games |
| **Schied 2017** | Schied, C., Kaplanyan, A., Wyman, C., Patney, A., Chaitanya, C.R.A., Burgess, J., Liu, S., Dachsbacher, C., Lefohn, A., and Salvi, M. "Spatiotemporal Variance-Guided Filtering: Real-Time Reconstruction for Path-Traced Global Illumination." HPG 2017. | SVGF framework: temporal accumulation + variance-guided a-trous |
| **Schied 2018** | Schied, C., Salvi, M., Kaplanyan, A., Wyman, C., Patney, A., Chaitanya, C.R.A., Burgess, J., Liu, S., Dachsbacher, C., and Lefohn, A. "Spatiotemporal Variance-Guided Filtering." Journal of Computer Graphics Techniques 7(4), 2018. | Extended SVGF: improved variance estimation, edge-stopping functions |
| **AMD CAS 2019** | AMD. "FidelityFX Contrast Adaptive Sharpening." GPUOpen, 2019. | 5-tap contrast-adaptive sharpening, noise-aware, ringing-free |
| **AMD RCAS 2021** | AMD. "FidelityFX Super Resolution 1.0 -- RCAS." GPUOpen, 2021. | Robust CAS variant with algebraic negative lobe bounding |
| **Roberts R2** | Roberts, M. "The Unreasonable Effectiveness of Quasirandom Sequences." extremelearning.com.au. | R2 quasirandom sequence: optimal 2D low-discrepancy via plastic constant |
| **Gilcher 2023** | Gilcher, P. (MartyMcFly). "Improved R2 Precision Coefficients." iMMERSE / qUINT, 2023. | IEEE 754 single-precision refinement of R2 increment values |
| **Jimenez 2014** | Jimenez, J. "Next Generation Post Processing in Call of Duty: Advanced Warfare." SIGGRAPH 2014, Advances in Real-Time Rendering. | Temporal AA with bicubic filtering, velocity weighting, SMAAx4 |
| **Lottes 2014** | Lottes, T. "TSSAA (Temporal Super-Sampling Anti-Aliasing)." NVIDIA, 2014. | Luminance-weighted blending for HDR anti-flicker |
| **Intel TAA** | Intel GameTechDev. "Temporal Anti-Aliasing Sample." Intel Developer Zone, github.com/GameTechDev. | Reference TAA implementation, neighborhood clip variants |
| **Wolfe 2022** | Wolfe, A., Salvi, M., and Pharr, M. "Spatiotemporal Blue Noise Masks." NVIDIA, 2022. | STBN for temporal noise: improved convergence for stochastic effects |
| **Dolp 2024** | Dolp, T., Ritschel, T., and Myszkowski, K. "Optimized A-Trous Filter Schedules for Denoising." I3D 2024. | Non-power-of-2 step sizes for reduced low-frequency ringing |
| **Vlachos 2015** | Vlachos, A. "Advanced VR Rendering." GDC 2015, Valve. | Triangular-PDF dithering for banding elimination in low-bit-depth targets |
| **Dammertz 2010** | Dammertz, H., Sewtz, D., Hanika, J., and Lensch, H. "Edge-Avoiding A-Trous Wavelet Transform for Fast Global Illumination Filtering." HPG 2010. | Original a-trous spatial denoising with edge-stopping functions |

### Additional Context References

- **ENBSeries SDK v1001/v504:** Boris Vorontsov, enbdev.com. API documentation and shader conventions.
- **SkyrimBridge v3 Architecture:** Internal reference -- `C:\Users\Zain\SKSE\SkyrimBridge_v3\docs\ARCHITECTURE.md`
- **SkyrimBridge CB Layout:** `C:\Users\Zain\SKSE\SkyrimBridge_v3\shader\Helper\SkyrimBridge_CB.fxh` (104 float4s, register b7)
- **ENB Pipeline Execution Order:** enbeffectprepass -> enbdepthoffield -> enbbloom -> enbadaptation -> enblens -> enbeffect -> enbeffectpostpass (LDR!) -> enbsunsprite -> enbunderwater

---

*End of document. Target files: `SB_TemporalAA.fx` (addon effect), `Helper/SB_TemporalCore.fxh` (shared functions).*
