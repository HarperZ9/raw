# Rendering Research Reference

## Source Documents
- `Rendering_Algorithm_Improvements_Phase2 (1).docx` — Numerical verification of 7 improvements
- `Gilcher_Rendering_Techniques_Analysis.docx` — Gilcher/MartyModding technique analysis
- `RenderingTricks_BloomVolumetrics_Hacks.md` — VB-SSGI, filtering, temporal techniques
- `Overcoming_DX11_SM5_Limitations_Pixel_Shader_Pipelines (2).md` — Advanced VB-SSGI, A-Trous, SSR

## Verified Algorithmic Improvements (Phase 2 Doc)

### Smith-GGX k(α) Mapping — 3.3x accuracy at α=0.25
```hlsl
// OLD: float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
// NEW: float k = 0.2349 * roughness * roughness + 0.2689 * roughness;
```

### Lazanyi-Schlick Fresnel Correction — 2x accuracy
```hlsl
float f82 = 1.670 * f0 + (-1.315);
float3 F = F_Schlick(VdotH, f0) + f82 * VdotH * pow(1-VdotH, 6);
```

### Physically-Based Horizon Falloff — 12x lower L2 error
```hlsl
// OLD: hc = lerp(-1.0, hc, saturate(1.0 - hd*hd/(R*R*falloff)));
// NEW: float att = 1.0 / (1.0 + pow(hd / (aoRadius * 0.556), 1.6));
```
Matches disc-occluder solid angle formula. σ=0.556, p=1.597.

### Contact Shadow Penumbra — Physical profile
```hlsl
// V(d) = 0.5 + 0.6344*d - 0.0895*d^3 - 0.0425*d^5 (Chebyshev-5)
```

### Bloom: Separable + Bilinear = 12x bandwidth reduction
169 fetches → 14 fetches (2 passes × 7 bilinear taps).

### GTAO cn=0 Fast Path — 30% trig reduction
When `|cn| < 0.1`: `ao += saturate(0.5 - 0.5 * (t0*t0 + t1*t1))`.

### VB Cosine-Weighted Run Decomposition
For run [a,b] with sector width Δ=π/N and normal angle θ_n:
`contribution = sin((b+1)*Δ - π/2 - θ_n) - sin(a*Δ - π/2 - θ_n)`
56% of bitmasks have ≤2 runs, 87% have ≤3 runs.

### Chebyshev-5 atan — 86x more accurate than Euler at same cost

## Gilcher (MartyModding) Key Techniques

### MXAO Evolution
- qUINT: Bayer matrix sampling (low-discrepancy, less blur needed)
- iMMERSE v2: Visibility Bitmask + **cosine-weighted** (radiometrically correct)
- Key claim: cosine term reintroduced into bitmask accumulation

### Improved R2 Sequence (1-α coefficients)
```hlsl
// Standard (breaks at ~1024 samples in float32):
// α₁ = 1/φ₂ ≈ 0.7548776662, α₂ = 1/φ₂² ≈ 0.5698402910
// Gilcher improved (usable to >1.1M samples):
static const float R2_A = 0.2451223338;  // 1 - 1/φ₂
static const float R2_B = 0.4301597090;  // 1 - 1/φ₂²
```

### Hemisphere-Aware Falloff
Interpolate toward hemisphere horizon `cos(normal_angle ± π/2)` instead of -1.

### Depth MIP Hierarchy
Pre-filtered depth for cache-coherent distant sampling (McGuire 2012 SAO).

### Launchpad Shared Prepass
Compute normals, motion vectors, linearized depth once for all effects.

## VB-SSGI Reference Implementation Details

### Proper Slice-Plane Projection
```hlsl
float2 projN = float2(dot(viewNormal.xy, sliceDir), viewNormal.z);
float projNLen = length(projN);  // Per-slice weight
float normalAngle = atan2(projN.y, projN.x);  // For hemisphere masking
```

### Hemisphere Masking
```hlsl
uint hemisphereMask = FlagSectors(normalAngle - PI/2, normalAngle + PI/2);
uint visibleInHemi = (~occlusionBitmask) & hemisphereMask;
float sliceAO = countbits(visibleInHemi) / countbits(hemisphereMask);
```

### Reference Pipeline (multi-technique, not available in prepass)
T0: Depth MIP chain → T1: Half-res VB-SSGI → T2: Bilateral denoise → T3: Temporal accumulation → T4: Composite

### Performance Reference (1080p)
| Technique | GTX 1060 | RTX 3060 |
|-----------|----------|----------|
| VB-SSGI 12×5 half-res | ~3.5ms | ~2.0ms |
| Bilateral denoise 5-pass | ~0.45ms | ~0.20ms |
| Temporal accumulation | ~0.1ms | ~0.05ms |

## Temporal Filtering Techniques
- Variance clipping (Salvi 2016): clamp history to [mean ± γ·σ]
- 14-DOP clipping: 30-60% less ghosting than AABB
- Bicubic Catmull-Rom history: 4-tap approximation, sharper than bilinear
- Motion-adaptive gamma: γ=1.5 static, γ=0.5 moving
- STBN jitter: `frac(blueNoise2D + frameCount * 0.6180339887)`

## A-Trous Wavelet Filter (SVGF)
5-pass, stride 1→2→4→8→16, covers 31-texel radius.
Edge-stopping: normal^128, depth/σ, luminance/σ_L.
Fast scheduling (Dolp 2024): bit-reversal coordinate permutation for 1.5-2.5x speedup.

## Negative Results (don't bother)
- R2 Moebius transforms: no single transform beats canonical at all N
- SSR exponential stepping: loses far-field coverage (use hybrid)
- Karis bloom weight alternatives: 1/(1+L) is already near-optimal

---

## Vol 6: Advanced ENB Pipeline — Rendering Tricks, Hacks, and Optimizations

### ENB Pipeline Execution Order (9 stages, sequential)
```
1. enbeffectprepass    → R16G16B16A16F (HDR, full precision, first to run)
2. enbdepthoffield     → R16G16B16A16F (HDR)
3. enbbloom            → R16G16B16A16F (HDR) — output = bloom data ONLY
4. enbadaptation       → R32F 1×1 (scalar luminance) — output = TextureAdaptation
5. enblens             → R16G16B16A16F (HDR) — output = lens effects ONLY
6. enbeffect           → R16G16B16A16F (HDR) — composites bloom + lens + tonemaps
7. enbeffectpostpass   → ⚠ R10G10B10A2_UNORM (LDR, 2-bit alpha!) — ALL HDR work MUST be done before this
8. enbsunsprite        → R10G10B10A2 (LDR, additive sprites)
9. enbunderwater       → R10G10B10A2 (LDR, only when camera submerged)
```
**CRITICAL**: enbeffectpostpass format transition to R10G10B10A2 means:
- Values clamped to [0,1] — no HDR, no negative values
- Only 2 bits of alpha — can store 0.0, 0.33, 0.67, 1.0
- All color grading, film grain, vignette, etc. must account for this

### ENB Native Parameters (Complete Inventory)
```hlsl
float4 Timer;         // .x=elapsed seconds, .y=average fps, .z=frame counter 0-9999 (wraps), .w=delta time
float4 ScreenSize;    // .x=width, .y=1/width, .z=aspect(w/h), .w=1/aspect(h/w)
// Derived: PixelSize = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z)
//          ScreenRes = float2(ScreenSize.x, ScreenSize.x * ScreenSize.w)

float4 TimeOfDay1;    // .x=dawn, .y=sunrise, .z=day, .w=sunset (blend weights, sum ≈ 1.0)
float4 TimeOfDay2;    // .x=dusk, .y=night, .z/.w=unused
float  ENightDayFactor;  // 0.0=night, 1.0=day (smooth)
float  EInteriorFactor;  // 0.0=exterior, 1.0=interior (smooth)
float  FieldOfView;      // Vertical FOV in DEGREES

// Matrices (all available in every stage):
float4x4 MatrixVP;       // View × Projection (current frame)
float4x4 MatrixInverseVP; // Inverse of VP
float4x4 MatrixVPRotation; // VP without translation (skybox-only)
float4x4 MatrixView;     // View matrix
float4x4 MatrixInverseView; // Camera→World
float4x4 MatrixProjection;
float4x4 MatrixInverseProjection;
float4x4 MatrixPreviousVP; // Previous frame VP (for motion vectors)

float4 SunDirection;   // World-space unit vector toward sun (y=up)
float4 WeatherAndTime; // .x=current weather FormID, .y=outgoing weather FormID, .z=transition 0→1, .w=game time 0-24
```

### ENB Texture Availability (Per-Stage)
| Texture | Format | Available In | Notes |
|---------|--------|-------------|-------|
| TextureColor | R16G16B16A16F | All stages | Current frame HDR (LDR after enbeffect) |
| TextureOriginal | R10G10B10A2 | enbeffect onward | LDR snapshot before any ENB processing |
| TextureDepth | R32F | All stages | Reversed-Z (near=1.0, far≈0.0) |
| TextureNormals | R8G8B8A8 | All stages | Packed world-space normals |
| TextureBloom | varies | enbeffect onward | Output of enbbloom.fx |
| TextureAdaptation | R32F 1×1 | enbeffect onward | Scalar scene luminance |
| TextureLens | varies | enbeffect onward | Output of enblens.fx |
| RenderTargetRGBA32 | R8G8B8A8 | All stages | ⚠ UNORM [0,1] clamped |
| RenderTargetRGBA64 | R16G16B16A16 | All stages | ⚠ UNORM [0,1] — NOT float despite 64-bit! |
| RenderTargetRGBA64F | R16G16B16A16F | All stages | True HDR float, use this for HDR intermediates |
| RenderTargetR16F | R16F | All stages | Single-channel half-float |
| RenderTargetR32F | R32F | All stages | Single-channel full float |
| RenderTarget1024/512/256/128/64/32/16 | R32F | All stages | Fixed-size, good for downsampled work |

### Derivable Values (from ENB natives — no SkyrimBridge needed)
```hlsl
// Depth linearization (reversed-Z):
float z_ndc = TextureDepth.Sample(smp, uv).x;
float linearZ = MatrixProjection._43 / (z_ndc - MatrixProjection._33);  // view-space depth

// View-space position reconstruction:
float2 ndc = uv * 2.0 - 1.0;
float3 viewPos = float3(ndc * float2(MatrixInverseProjection._11, MatrixInverseProjection._22), 1.0) * linearZ;

// World-space position:
float4 worldPos = mul(float4(ndc, z_ndc, 1.0), MatrixInverseVP);
worldPos.xyz /= worldPos.w;

// Camera world position (from inverse view matrix):
float3 camPos = float3(MatrixInverseView._41, MatrixInverseView._42, MatrixInverseView._43);

// Motion vectors (current vs previous frame):
float4 prevClip = mul(float4(worldPos.xyz, 1.0), MatrixPreviousVP);
float2 prevUV = prevClip.xy / prevClip.w * 0.5 + 0.5;
float2 motionVec = uv - prevUV;

// Normal unpacking (ENB format):
float3 normal = TextureNormals.Sample(smp, uv).xyz * 2.0 - 1.0;

// FieldOfView → projection scale:
float tanHalfFov = tan(radians(FieldOfView) * 0.5);
```

### Performance Budgets (60 FPS target = 3-5ms total ENB budget)
| Technique | 1080p | 1440p | 4K |
|-----------|-------|-------|-----|
| GTAO 6 slices, 4 taps | 0.65ms | 1.35ms | 2.8ms |
| SSR 64+8 steps | 0.48ms | 0.98ms | 2.0ms |
| Contact Shadows 16 steps | 0.15ms | 0.30ms | 0.65ms |
| Height Fog (analytical) | 0.08ms | 0.15ms | 0.32ms |
| SSS separable 11-tap | 0.20ms | 0.40ms | 0.85ms |
| Bloom 6-level Kawase | 0.15ms | 0.30ms | 0.60ms |
| Full DoF (golden spiral) | 0.40ms | 0.80ms | 1.6ms |
| ACES tonemapping | 0.05ms | 0.10ms | 0.20ms |
| SMAA 1x (3 passes) | 0.25ms | 0.50ms | 1.0ms |
| CAS sharpening | 0.05ms | 0.10ms | 0.20ms |

### Cross-Stage Patterns

**KeepAlive (prevent dead-stripping):**
```hlsl
// Compiler can't prove Timer.w < 0 is always false, so param survives DCE
float4 SB_Retain(float4 param) { return (Timer.w < 0.0) ? param : 0.0; }
```

**RT Ping-Pong (multi-pass in single technique):**
```hlsl
// Pass N writes to RenderTargetRGBA64F, Pass N+1 reads it back via TextureRenderTarget
// Only base technique (with UIName) respects RenderTarget annotation
// Sub-techniques ALWAYS write to TextureColor
```

**Alpha Channel Packing:**
```hlsl
// Pack extra data into alpha of HDR RTs (R16G16B16A16F has full float alpha)
// Common: luminance, AO, material mask, CoC
// ⚠ R10G10B10A2 stages: only 2 bits alpha (0.0, 0.33, 0.67, 1.0)
```

**Temporal Accumulation (across frames):**
```hlsl
// Use RenderTargetRGBA64F as persistent buffer
// Read previous frame: TextureRenderTargetRGBA64F.Sample(smp, prevUV)
// Blend: lerp(history, current, 1.0 / max(accumFrames, 1.0))
// Reset on scene cut: if(Timer.z < 1.0) accumFrames = 0; // frame 0 after load
```

**TOD Interpolation (time-of-day blending):**
```hlsl
// TimeOfDay1/2 weights sum to ~1.0, use as blend factors:
float val = dawn * TimeOfDay1.x + sunrise * TimeOfDay1.y + day * TimeOfDay1.z
          + sunset * TimeOfDay1.w + dusk * TimeOfDay2.x + night * TimeOfDay2.y;
```

### Key Algorithm Reference (HLSL snippets)

**GTAO (cosine-weighted bitmask):**
- 6 slices × 4 taps per side, R2 jitter + temporal rotation
- Cosine integration: `ao += 0.25 * (cos(2*h_front - bias) - cos(2*h_back - bias))`
- Spatial: bilateral 3×3, temporal: variance clip + 0.95 blend

**SSR (linear march + binary refine):**
- View-space ray, 64 linear steps + 8 binary refinement
- Hi-Z acceleration: test coarser mip levels to skip empty space
- Fresnel weighting: `F0 + (1-F0) * pow5(1 - NdotV)`

**Dual Kawase Bloom:**
- Downsample: 5-tap pattern (center + 4 diagonal), halve resolution each level
- Upsample: 9-tap pattern (cardinal + diagonal + center), double resolution
- 6 levels typical, Karis average on first downsample to prevent fireflies

**Bokeh DoF (golden spiral kernel):**
- Focus detection: center-weighted 3×3 depth sample
- CoC: `coc = abs(depth - focusDepth) * cocScale`
- 64 samples on golden spiral, shaped by: cat's eye (cos falloff at edges), anamorphic (elliptical), swirly (tangential offset)

**ACES Hill Tonemapping:**
```hlsl
float3 ACESFitted(float3 color) {
    // sRGB → AP0 → RRT_SAT → RRT+ODT → AP1 → sRGB
    const float3x3 ACESInputMat = { {0.59719,0.35458,0.04823}, {0.07600,0.90834,0.01566}, {0.02840,0.13383,0.83777} };
    const float3x3 ACESOutputMat = { {1.60475,-0.53108,-0.07367}, {-0.10208,1.10813,-0.00605}, {-0.00327,-0.07276,1.07602} };
    float3 v = mul(ACESInputMat, color);
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return saturate(mul(ACESOutputMat, a / b));
}
```

**Brown-Conrady Lens Distortion:**
```hlsl
float2 r = (uv - 0.5) * 2.0;
float r2 = dot(r, r);
float barrel = 1.0 + k1*r2 + k2*r2*r2;  // k1=-0.15, k2=0.05 typical
float2 distortedUV = r * barrel * 0.5 + 0.5;
```

### Debugging Methodology
1. **Isolate stage**: Comment out technique body, return solid color → identifies which stage causes artifact
2. **Depth visualization**: `return linearZ / farClip` as heatmap — reveals depth precision issues
3. **Normal visualization**: `return normal * 0.5 + 0.5` — reveals normal reconstruction errors
4. **Motion vector visualization**: `return float4(abs(motionVec) * 100.0, 0, 1)` — reveals temporal issues
5. **Performance**: Use `Timer.w` (delta time) — if consistently > 16.67ms at 60fps, budget exceeded
6. **Common artifacts**: banding = insufficient precision (use dithering), halos = bilateral filter too wide, ghosting = temporal blend too aggressive
