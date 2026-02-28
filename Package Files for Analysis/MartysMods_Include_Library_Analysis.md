# MartysMods / iMMERSE Include Library Analysis
## Complete `mmx_*.fxh` Infrastructure Reference

---

## Architecture Overview

The `mmx_*.fxh` headers form the shared infrastructure layer for the entire iMMERSE suite. They follow a clean namespace-per-header pattern with `#pragma once` guards and explicit dependency chains. Every header includes `mmx_global.fxh` as the root dependency.

### Dependency Graph

```
mmx_global.fxh          ← ROOT (no dependencies)
├── mmx_depth.fxh        ← standalone
├── mmx_math.fxh         ← depends on global
│   ├── mmx_bxdf.fxh     ← depends on global + math
│   ├── mmx_harmonics.fxh← depends on math
│   ├── mmx_hash.fxh     ← depends on math
│   ├── mmx_qmc.fxh      ← depends on global
│   └── mmx_sfc.fxh      ← depends on math
├── mmx_camera.fxh       ← depends on global + depth
├── mmx_deferred.fxh     ← depends on global + math
├── mmx_texture.fxh      ← depends on global
├── mmx_colorspaces.fxh  ← standalone (no includes)
├── mmx_debug.fxh        ← standalone
├── mmx_input.fxh        ← standalone (HDR PQ/HLG)
└── mmx_fft.fxh          ← template-style (no #pragma once)
```

---

## 1. `mmx_global.fxh` — Foundation Layer

**Namespace:** Global scope (no namespace wrapper)
**Role:** Platform detection, DLSS/FSR/TAAU compatibility, core macros, fullscreen triangle VS

### Platform Detection

```hlsl
#define GPU_VENDOR_NVIDIA   0x10DE
#define GPU_VENDOR_AMD      0x1002
#define GPU_VENDOR_INTEL    0x8086

// Feature gates via __RENDERER__ intrinsic
_COMPUTE_SUPPORTED  // true for D3D11+
_BITWISE_SUPPORTED  // true for D3D10+
```

This is the mechanism by which all compute and bitwise operations are conditionally compiled across the suite. DX9 fallback paths exist throughout.

### DLSS/FSR/TAAU Resolution Scaling

Defines `BUFFER_WIDTH_DLSS` / `BUFFER_HEIGHT_DLSS` that scale internal render resolution when `_MARTYSMODS_TAAU_SCALE` is defined. All screen-space effects that work at internal resolution (normals, depth, AO) use these instead of `BUFFER_WIDTH/HEIGHT`.

Supported presets:
- DLSS Quality (1.5x), Balanced (1.72x), Performance (2x), Ultra Performance (3x)
- FSR Ultra Quality (1.3x), Quality (1.5x), Balanced (1.7x), Performance (2x)

### Core Macros

```hlsl
linearstep(a, b, x)    // saturated remap — missing from HLSL stdlib
exp10(x)               // pow(10, x) — missing from HLSL stdlib
safenormalize(x)       // normalize with epsilon guard (1e-8)
select(lhs, rhs, cond) // ternary with intuitive ordering (lerp-like)
dot2(x)                // dot(x, x) shorthand

// Multi-component min/max/median — generated via macro templates
min3, min4, max3, max4, med3   // all float/int vector overloads
minc, maxc, medc               // component-wise reduce
```

Notable: The `LOG2(x)` macro computes integer log2 at compile time for constants up to 16-bit, using cascading bit-shift tests. This enables compile-time loop bounds.

### Fullscreen Triangle VS

```hlsl
void FullscreenTriangleVS(in uint id : SV_VertexID, out float4 vpos, out float2 uv)
{
    uv = id.xx == uint2(2, 1) ? 2.0.xx : 0.0.xx;
    vpos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
```

Single-triangle fullscreen pass — 3 vertices, no vertex buffer. Industry standard approach that avoids the quad overshading penalty.

### MRT Output Structs

Pre-defined PSOUT1 through PSOUT4 for multi-render-target passes. Used extensively in Launchpad and RTGI.

---

## 2. `mmx_depth.fxh` — Depth Buffer Access

**Namespace:** `Depth`
**Role:** Universal depth buffer handling with ReShade's depth configuration system

### Configuration Defines

Handles all ReShade depth permutations:
- `RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN` — Y-flip correction
- `RESHADE_DEPTH_INPUT_IS_REVERSED` — reverse-Z (default: true)
- `RESHADE_DEPTH_INPUT_IS_LOGARITHMIC` — log depth approximation
- `RESHADE_DEPTH_LINEARIZATION_FAR_PLANE` — default 1000 units
- Scale/offset for X/Y pixel and UV adjustments

### Linearization

```hlsl
// Core formula (after log/reverse transforms):
depth /= far - depth * (far - 1.0);
```

Generates overloads for `float` through `float4` via macro template. The logarithmic depth approximation uses `x * lerp(x, 1.0, 0.04975)` — a polynomial that avoids `exp/log` transcendentals.

### UV Correction

`correct_uv()` applies flip, scale, and offset transforms to align depth buffer UVs with the color buffer. This is critical for games with mismatched depth/color buffer orientations.

---

## 3. `mmx_camera.fxh` — View/Projection Math

**Namespace:** `Camera`
**Dependencies:** `mmx_global.fxh`, `mmx_depth.fxh`
**Role:** UV ↔ projection space conversion, depth-to-Z transforms

### Projection Matrix (Optimized)

Rather than storing a full 4×4 matrix, precomputes simplified projection constants:

```hlsl
static const float3 uvtoprojADD = float3(-tan(radians(FOV) * 0.5).xx, 1.0) * BUFFER_ASPECT_RATIO_DLSS.yxx;
static const float3 uvtoprojMUL = float3(-2.0 * uvtoprojADD.xy, 0.0);
static const float4 projtouv    = float4(rcp(uvtoprojMUL.xy), -rcp(uvtoprojMUL.xy) * uvtoprojADD.xy);
```

This reduces `uv_to_proj` and `proj_to_uv` to a handful of MADs. Uses TAAU-scaled aspect ratios since projection operates on the actual depth buffer resolution.

### Key Functions

- `depth_to_z(depth)` — linear depth [0,1] → view-space Z distance
- `z_to_depth(z)` — inverse
- `uv_to_proj(uv, z)` — screen UV + Z → 3D view-space position
- `proj_to_uv(pos)` — 3D view-space → screen UV (perspective divide)

Overloads accept optional sampler+mip for direct depth lookup.

### FOV Configuration

Uses `_MARTYSMODS_GLOBAL_FOV` (default 60°) as a global define. This is a user-configured approximation since ReShade cannot query the game's actual projection matrix.

---

## 4. `mmx_math.fxh` — Mathematical Utilities

**Namespace:** `Math`
**Role:** Geometry, fast approximations, encodings, bitfield emulation

### Fast Approximations

```hlsl
fast_sign(x)  // branchless sign (≥0 → 1, else -1)
fast_acos(x)  // |error| < 0.017 rad, avoids transcendental acos()
              // -0.156583 * |x| + π/2, scaled by sqrt(1-|x|)
```

### 2D Rotation

Compact rotator representation using `float4` (cos, sin, -sin, cos):

```hlsl
get_rotator(phi)            // create rotator from angle
merge_rotators(ra, rb)      // compose two rotations
rotate_2D(v, r)             // apply rotator to 2D vector
```

Used extensively in sampling patterns (AO slices, ray directions).

### Tangent Frame Construction

```hlsl
float3x3 base_from_vector(float3 n)
```

Pixar's method (Duff et al. 2017) optimized for ALU count. Creates an orthonormal TBN basis from a single normal vector. The crossover boundary is shifted to `n.z >= 0.5` to prevent flip-flopping on flat surfaces — a practical refinement over the standard `n.z >= 0` threshold.

### Octahedral Encoding

Normal vector packing into 2 channels [0,1]:

```hlsl
octahedral_enc(float3 v) → float2    // 3D unit vector → 2D [0,1]
octahedral_dec(float2 o) → float3    // 2D [0,1] → 3D unit vector
```

Used in `Deferred::NormalsTexV3` (RGBA16 format: XY = shading normals, ZW = geometry normals).

### AABB Intersection

```hlsl
aabb_clip(p, min, max)      // clip point to AABB (for temporal history clamping)
aabb_hit_01(origin, dir)    // ray-AABB intersection in [0,1] space
inside_screen(uv)           // elegant: all(saturate(uv - uv*uv))
```

The `aabb_clip` function is the standard color-space clamping used in temporal reprojection — clips the history sample to the current frame's local color AABB to reject ghosting.

### Matrix Inversion

Full 3×3 and 4×4 matrix inversion via adjugate method with zero-determinant protection: `rcp(det + (abs(det) < 1e-8))`.

### Anisotropy Mapping

```hlsl
anisotropy_map(kernel, n, limit)   // distort 2D sampling kernel by surface normal
anisotropy_map2(kernel, n, limit)  // with elongation (cosine stretch)
```

Projects sampling kernels along the surface foreshortening direction. Used in AO and denoising to adapt filter shapes to surface orientation.

### Chebyshev Weighting

```hlsl
chebyshev_weight(mean, variance, xi)  // variance shadow map-style weighting
```

### DX9 Float Bitfield Emulation

Remarkable: Emulates a 24-bit integer bitfield using float arithmetic for DX9 compatibility:

```hlsl
bitfield_get(float bitfield, int bit)           // read bit via floor/frac
bitfield_set(inout float bitfield, int bit, bool value)  // write bit via exp2
```

Used by MXAO's horizon tracking on DX9, where actual integer bitwise ops are unavailable.

---

## 5. `mmx_bxdf.fxh` — BRDF/BSDF Library

**Namespace:** `BXDF`, `BXDF::GGX`
**Dependencies:** `mmx_global.fxh`, `mmx_math.fxh`
**Role:** Physically-based sampling, phase functions, GGX microfacet model

### Basic Sampling

```hlsl
sample_disc(u)      // uniform disk via sqrt(r) * cos/sin(θ)
sample_sphere(u)    // uniform sphere
ray_cosine(u, n)    // cosine-weighted hemisphere: normalize(sphere + n)
ray_uniform(u, n)   // uniform hemisphere with bias
boxmuller(u)        // Box-Muller transform for Gaussian sampling
boxmuller3D(u)      // 3D Gaussian via sphere * sqrt(-2*log(u))
```

The cosine-weighted hemisphere `normalize(sphere + n)` is the elegant one-liner approach — avoids the traditional cos(θ)/sin(θ) construction.

### Phase Functions (Henyey-Greenstein)

```hlsl
sample_phase_henyey_greenstein(u, g=0.75)    // importance sample HG phase
henyey_greenstein_cdf(cos_theta, g)           // CDF (optimized factoring)
henyey_greenstein_icdf(x, g)                  // inverse CDF for direct sampling
```

Used in volumetric fog for anisotropic scattering. The implementation pre-factors `g` terms for efficiency, computing `(1/g - g)/2` once.

### Fresnel

```hlsl
fresnel_schlick(cos_theta, F0)  // F0 + (1-F0) * (1-cos)^5
                                // via mad(f²*f²*f, 1-F0, F0)
```

Optimized to use a single MAD with precomputed `f^5`.

### GGX Microfacet Model

**Smith Geometry Functions:**

```hlsl
smith_G1(ndotx, alpha)                              // single-direction masking
smith_G2_heightcorrelated(ndotl, ndotv, alpha)       // full height-correlated
smith_G2_over_G1_heightcorrelated(alpha, ndotwi, ndotwo)  // ratio form
```

**VNDF Sampling (3 variants):**

1. **Standard VNDF** (`sample_vndf`) — Dupuy et al. spherical cap method. Takes a `coverage` parameter controlling the solid angle sampled. Same PDF as Heitz' original but more numerically stable.

2. **Bounded VNDF** (`sample_vndf_bounded`) — Tokuyoshi & Eto 2024 with Gilcher modification. Adds a `pdf_ratio` output that relates the bounded PDF to the standard VNDF PDF, enabling correct weighting with `F * G2/G1 * pdf_ratio`.

3. **Bounded Isotropic VNDF** (`sample_vndf_bounded_iso`) — Combined with Dupuy's TBN-less method. Eliminates the need for a pre-built tangent frame by decomposing `wi` into tangential and orthogonal components relative to `n`. The most efficient variant for isotropic materials.

**VNDF Bounded Key Innovation:**

The bounded approach computes a tighter integration domain:
```hlsl
float s = 1 + sqrt(saturate(1 - z2));
float k = (1 - a2) * s2 / (s2 + a2 * z2);
pdf_ratio = (k * wi.z + t) / (wi.z + t);
```

This reduces variance for smooth surfaces where the standard VNDF over-samples low-probability directions.

**NDF and Dominant Direction:**

```hlsl
ndf(ndoth, alpha)                    // GGX/Trowbridge-Reitz distribution
dominant_direction(n, v, alpha)      // off-specular peak shift
// f = (1 - roughness) * (sqrt(1-roughness) + roughness)
// result = lerp(n, reflect(-v,n), f)
```

---

## 6. `mmx_harmonics.fxh` — Spherical Harmonics

**Namespace:** `SphericalHarmonics`
**Dependencies:** `mmx_math.fxh`
**Role:** L1 (order-1) spherical harmonics for radiance caching

### SH Encoding

```hlsl
dir_to_sh(float3 v)  // direction → 4 SH coefficients (L0 + L1)
// c0 = 0.5 * sqrt(1/π)     — DC band
// c1 = sqrt(0.75/π)        — linear bands (-y, z, -x)
```

### Irradiance Evaluation

```hlsl
linear_eval_irradiance(sh_r, sh_g, sh_b, v, sharpness=1)
```

Standard L1 SH irradiance: projects direction through cosine-convolved SH bands. The `sharpness` parameter modulates the L1 contribution: `float4(2-sharpness, sharpness*0.667.xxx)`.

### L2 Zonal Harmonics Hallucination

```hlsl
hallucinate_zh3_irradiance(sh_r, sh_g, sh_b, v, sharpness=1)
```

This is the clever part — **estimates L2 zonal harmonics from L1 data** using luminance-weighted axis analysis:

1. Extracts the dominant axis from L1 luminance coefficients
2. Computes the anisotropy ratio: `|L1_projection| / L0`
3. Estimates L2 ZH coefficient: `L0 * (0.6*ratio + 0.08) * ratio`
4. Evaluates ZH2: `sqrt(5/16π) * (3*fZ² - 1)`
5. Adds to L1 evaluation with 0.25 weight

This provides better angular resolution than L1 alone without the storage cost of full L2 (9 coefficients → still only 4). Used in RTGI's radiance cache.

---

## 7. `mmx_hash.fxh` — Hash & RNG

**Namespace:** `Hash`
**Dependencies:** `mmx_math.fxh`
**Role:** Fast integer hashing, RNG state machine, bit packing

### Core Hash (Hash Prospector)

```hlsl
uint uhash(uint x)  // bias 0.107 — custom coefficients
{
    x ^= x >> 16;   x *= 0x21f0aaad;
    x ^= x >> 15;   x *= 0xd35a2d97;
    x ^= x >> 16;
    return x;
}
```

Found via hash prospector search. Three-round xorshift-multiply with low avalanche bias.

### Float Extraction (Bit Packing)

Multiple extraction patterns from a single uint:

```hlsl
uint_to_unorm(uint)   // 32 bits → 1 float [0,1]
uint_to_unorm2(uint)  // 16|16 bits → 2 floats
uint_to_unorm3(uint)  // 11|11|10 bits → 3 floats
uint_to_unorm4(uint)  // 8|8|8|8 bits → 4 floats
```

All use the IEEE 754 exponent trick: `asfloat((u >> shift) | 0x3F800000) - 1.0` to produce uniform [0,1] floats without division.

### RNG State Machine

```hlsl
float  next1D(inout uint rng_state)  // advance state, return 1 float
float2 next2D(inout uint rng_state)  // advance state, return 2 floats
float3 next3D(inout uint rng_state)  // advance state, return 3 floats
float4 next4D(inout uint rng_state)  // advance state, return 4 floats
```

Simple but effective: single hash per advance, multiple floats extracted from the result. The 2D variant gets 16 bits per channel, 3D gets 11/11/10, 4D gets 8/8/8/8.

### Hash Combine

```hlsl
hash_combine(inout uint state, uint value)
// state ^= value + 0x9e3779b9 + (state << 6) + (state >> 2)
```

Boost-style hash combine using the golden ratio constant.

---

## 8. `mmx_qmc.fxh` — Quasi-Monte Carlo Sequences

**Namespace:** `QMC`
**Dependencies:** `mmx_global.fxh`
**Role:** Low-discrepancy sampling sequences

### Roberts Sequences (Generalized Golden Ratio)

```hlsl
roberts1(idx, seed)  // 1D additive recurrence
roberts2(idx, seed)  // 2D: R2 sequence (Fibonacci-like)
roberts3(idx, seed)  // 3D
```

**DX10+ (integer arithmetic):** Uses Gilcher's "improved golden ratio sequences v2" — wrapping integer multiplication by irrational-derived constants, avoiding floating-point precision loss:
```hlsl
uint phi = 2654435769u;  // 1D: φ * 2^32
return float(phi * idx + useed) * exp2(-32.0);
```

**DX9 fallback:** Uses complementary coefficients with `frac()`:
```hlsl
return frac(seed + idx * 0.38196601125);  // 1/φ - 1
```

The complementary coefficients produce identical patterns but encounter numerical precision issues 2-3× later.

### Owen-Scrambled Sobol Sequences

Full implementation of the state-of-the-art sampling pipeline from Burley's 2020 paper:

```hlsl
sobol_raw(i)                         // raw Sobol (0,2)-sequence
sobol(i)                             // normalized to [0,1]²
scrambled_sobol(i, seed)             // Owen-scrambled via Laine-Karras hash
shuffled_scrambled_sobol(i, seed)    // shuffled + scrambled (decorrelated)
```

**Key building blocks:**

- `P(v)` — XOR prefix scan (cascading bit XOR)
- `JPJ(v)` — Reverse-bits variant of P (for inverse operations)
- `lk_hash(x, seed)` — Laine-Karras permutation hash (psychopath.io)
- `owen_scramble(p, seed)` — Owen scrambling via LK hash in reverse-bit order

**Optimization variants:**
- `optimize_lstar()` — L* discrepancy optimization
- `optimize_distance()` — maximum minimum distance optimization
- Both use `G()`, `mmdX()`, `mmdY()` transforms with padding-dependent bit manipulation

### Stratification Helper

```hlsl
get_stratificator(n_samples)                       // precompute coefficients
get_stratified_sample(per_sample_rand, strat, i)   // grid-based stratification
```

Forces quasi-random samples into a grid pattern for known sample counts. For non-square counts, distributes imperfectly but still provides better coverage than pure random.

---

## 9. `mmx_sfc.fxh` — Space-Filling Curves

**Namespace:** `SFC`
**Dependencies:** `mmx_math.fxh`
**Role:** Cache-coherent thread dispatch ordering

### Morton Z-Order

```hlsl
morton_i_to_xy(i)    // linear index → 2D coordinate
morton_xy_to_i(p)    // 2D coordinate → linear index
```

DX10+: Bit manipulation (interleave/deinterleave)
DX9: Loop-based emulation via `exp2` and `frac`

Used in compute shader dispatch to improve texture cache hit rates by keeping spatially adjacent threads adjacent in memory.

### Hilbert Curve

```hlsl
hilbert_i_to_xy(i, N)  // Gilcher's own implementation (DX10+ only)
```

Better locality than Morton but more expensive to compute. Single-loop implementation using XOR-based quadrant selection.

### H-Curve (Gilcher Original)

```hlsl
h_curve_i_to_xy(i, N)  // forward: index → coordinate
h_curve_xy_to_i(p, N)  // inverse: coordinate → index
```

Custom space-filling curve designed by Gilcher (Shadertoy reference: mtjSWc). Uses a hybrid approach with lookup-table base cases (packed in `0xAFFA5005` / `0x41BEBE41` magic constants) and recursive decomposition for larger sizes. Has both forward and inverse transforms, making it unique among the SFC implementations here.

---

## 10. `mmx_texture.fxh` — Texture Sampling

**Namespace:** `Texture`
**Dependencies:** `mmx_global.fxh`
**Role:** High-quality texture filtering beyond hardware bilinear

### Biquadratic (4-tap)

```hlsl
sample2D_biquadratic(s, uv, size)
```

Smooth interpolation using quadratic B-spline weights. 4 bilinear taps arranged as a cross pattern with offsets derived from `q*(q-1)+0.5`.

### B-Spline Bicubic (4-tap)

```hlsl
sample2D_bspline(s, uv, size)
```

**Highly optimized** — 25 ALU ops, 3 registers (down from 37/5 in naive implementation). Uses an approximation for the cubic offset: `d² * 0.12812 + d³ * 0.07188` with error below bilinear precision (|err|×255 < 0.2).

One texture coordinate resolves early for better latency hiding.

### Catmull-Rom (5-tap)

```hlsl
sample2D_catmullrom(s, uv, texsize)
```

Sharper than B-spline with C1 continuity. Uses the standard weight decomposition with an elegant closed-form normalization factor: `1 / (1 - (f-f²)(f-f²)/4)` — Gilcher's own derivation (PG23).

### 3D Volume Sampling (LUT)

```hlsl
sample3D_trilinear(s, uvw, size, atlas_idx)      // trilinear in atlas layout
sample3D_tetrahedral(s, uvw, size, atlas_idx)     // tetrahedral interpolation
```

Both support atlas-based 3D texture emulation (slices stacked vertically). The **tetrahedral variant** decomposes the cube into 6 tetrahedra and uses 3D barycentric coordinates with only 4 taps instead of trilinear's 8. DX9-safe with emulated integer math.

---

## 11. `mmx_colorspaces.fxh` — Color Space Conversions

**Namespace:** `Colorspace`
**Dependencies:** None
**Role:** Comprehensive color space conversion library

### Supported Conversions

| From | To | Notes |
|------|----|-------|
| sRGB | Linear | Standard IEC 61966-2-1 |
| Linear | sRGB | Inverse |
| RGB | HCV | Sam Hocevar / Emil Persson |
| RGB | HSL | Via HCV |
| HSL | RGB | Direct |
| RGB | HSV | Standard hexcone |
| HSV | RGB | Standard |
| RGB | XYZ | CIE 1931, D65 |
| XYZ | RGB | Inverse |
| XYZ | CIELAB | With D65 reference white |
| CIELAB | XYZ | Inverse |
| RGB | CIELAB | Compound |
| CIELAB | RGB | Compound |
| XYZ | LMS | Bradford chromatic adaptation |
| RGB | OKLab | Björn Ottosson's perceptual space |
| OKLab | RGB | Inverse |

### sRGB Luma

```hlsl
get_srgb_luma(float3 srgb)
// Linearizes → BT.709 luminance → re-encodes to sRGB gamma
```

Produces perceptually correct luma that matches the sRGB display encoding.

### OKLab

Full implementation of Ottosson's perceptual color space with both forward and inverse transforms using the exact published matrix coefficients. Used in ReGrade for perceptually uniform color grading operations.

---

## 12. `mmx_input.fxh` — HDR Input Handling

**Namespace:** Global scope
**Dependencies:** None
**Role:** HDR format detection and linearization

### Color Space Identification

```hlsl
BUFFER_COLOR_SPACE_SRGB    = 1
BUFFER_COLOR_SPACE_SCRGB   = 2  // Linear with negative values
BUFFER_COLOR_SPACE_ST2084  = 3  // PQ (Dolby)
BUFFER_COLOR_SPACE_HLG     = 4  // BBC/NHK Hybrid Log-Gamma
```

### Perceptual Quantizer (ST.2084)

```hlsl
pq_linearize(float3 E)      // PQ → linear [0,1]
pq_delinearize(float3 Y)    // linear [0,1] → PQ
```

Uses the exact SMPTE ST 2084 specification constants (m1=1305/8192, m2=2523/32, c1=107/128, c2=2413/128, c3=2392/128). The linearize path uses inverse exponents for better numerical precision.

---

## 13. `mmx_fft.fxh` — Fast Fourier Transform

**Namespace:** `FFT_INSTANCE` (macro-configured)
**Dependencies:** Implicit (TAU, etc.)
**Role:** GPU-based FFT for frequency-domain convolution

### Template Pattern

Unlike all other headers, this file intentionally lacks `#pragma once` — it's designed to be included multiple times with different `#define` configurations:

```hlsl
#define FFT_WORKING_SIZE  1024   // transform size
#define FFT_RADIX         8      // 2, 4, or 8
#define FFT_INSTANCE      MyFFT  // namespace name
#define FFT_AXIS          0      // 0=horizontal, 1=vertical
#define FFT_CHANNELS      2      // 2 or 4 (complex pairs)
```

### Algorithm

**Stockham auto-sort Cooley-Tukey** — avoids explicit bit-reversal permutation by alternating between input and output buffers at each stage.

**Radix implementations:**
- **Radix-2:** In-place butterfly: `z0+=z1; z1=z0-2*z1`
- **Radix-4:** Composed from 2× radix-2 + complex conjugate rotation
- **Radix-8:** Composed from 2× radix-4 + twiddle with `1/√2`

### Groupshared Memory Usage

```hlsl
groupshared float2 tgsm[FFT_WORKING_SIZE];
```

Each FFT pass:
1. Load data from texture into local registers
2. Perform radix butterfly on local data
3. Write to groupshared memory (transposed)
4. Barrier synchronize
5. Read back from groupshared (new stride)
6. Apply twiddle factors
7. Repeat until complete

For 4-channel mode, the process runs twice (two complex pairs) with interleaved barriers.

### Complex Arithmetic

```hlsl
complex_mul(c1, c2)  // Gauss trick: z=c1*c2; return (z.x-z.y, dot(c1*c2.yx, 1))
complex_conj(z)      // (re, -im)
```

The Gauss trick reduces complex multiplication from 4 to 3 multiplies at the cost of precision — trades accuracy for ALU savings.

### Output Normalization

All outputs scaled by `rsqrt(FFT_WORKING_SIZE)` — splits the `1/N` normalization equally between forward and inverse transforms.

---

## 14. `mmx_debug.fxh` — Debug Visualization

**Namespace:** `Debug`
**Role:** Viridis colormap for debug output

Single function: `viridis(float t)` — polynomial approximation of the matplotlib viridis colormap using 7 coefficients (degree-6 polynomial in Horner form).

---

## 15. Addon Binaries

### `lut_manager.addon32` / `lut_manager.addon64`
- PE32/PE32+ DLLs (ReShade native addons)
- Handles LUT texture loading and atlas management
- Provides the runtime texture binding that `sample3D_trilinear`/`sample3D_tetrahedral` in mmx_texture.fxh consume

### `ReGradeAddon.addon32` / `ReGradeAddon.addon64`
- PE32/PE32+ DLLs
- Compute shader dispatch for ReGrade operations
- Handles LUT building and atlas compilation that can't be done in pixel shaders alone

---

## Cross-Cutting Patterns & Insights for SkyrimBridge

### 1. Feature-Gate Architecture

The `_COMPUTE_SUPPORTED` / `_BITWISE_SUPPORTED` pattern is clean and universal. Every advanced feature has a DX9/DX10 fallback. This is directly applicable to SkyrimBridge's need to work across ENB (pixel-shader-only) and Community Shaders (compute-capable) backends.

### 2. Resolution Abstraction

The `BUFFER_*_DLSS` abstraction layer is elegant — all internal-resolution work uses these macros, while full-resolution work uses standard `BUFFER_*`. This pattern maps directly to ENB's potential need to handle different internal vs. output resolutions.

### 3. Namespace Organization

Each header owns exactly one namespace. Cross-header references use fully qualified names (e.g., `Math::octahedral_dec()` inside `mmx_deferred.fxh`). SkyrimBridge's `SB_*` shaders could adopt this pattern for their utility functions.

### 4. Sampling Infrastructure

The QMC library is exceptionally well-stocked:
- **Roberts sequences** for temporal jitter (fast, low-discrepancy, 128-frame cycling)
- **Owen-scrambled Sobol** for per-pixel decorrelation in ray tracing
- **Stratification** for known sample counts (AO slices, filter taps)

SkyrimBridge currently uses blue noise textures. The algebraic QMC approach could supplement or replace texture-based jitter where SkyrimBridge operates.

### 5. VNDF Sampling State of the Art

The bounded VNDF implementation (Tokuyoshi & Eto 2024 + Gilcher modification) represents the current state of the art for GGX importance sampling. The `pdf_ratio` output that relates bounded to unbounded PDFs is particularly clever — it allows mixing bounded sampling with standard `F * G2/G1` weighting.

SkyrimBridge's `SB_SSR.fx` could benefit from this approach for reflection ray generation.

### 6. SH Hallucination

The `hallucinate_zh3_irradiance` technique — estimating L2 zonal harmonics from L1 data — is a practical innovation for radiance caches with limited storage budget. SkyrimBridge's RTGI-equivalent could use this to get better angular resolution from the same memory footprint.

### 7. FFT Template Pattern

The multi-instance `#define`-based FFT is a creative workaround for ReShade FX's lack of templates/generics. Each `#include` with different defines creates a separate FFT namespace. While ENB doesn't support compute shaders, this pattern could inform how SkyrimBridge structures reusable shader components.

### 8. Tetrahedral LUT Interpolation

The tetrahedral 3D LUT sampling uses 4 texture fetches instead of trilinear's 8 — a 2× bandwidth reduction with minimal quality loss. This is directly applicable to any LUT-based color grading in SkyrimBridge.
