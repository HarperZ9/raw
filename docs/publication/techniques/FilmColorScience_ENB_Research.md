# Film Color Science & Advanced LUT Architecture for ENB

## Research Document — SkyrimBridge / Silent Horizons

*Zain Dana Harper — March 2026*

---

## Executive Summary

This document synthesizes deep research into professional film color grading, the photochemical negative-to-print pipeline, TreyM's dual-stage LUT approach, Dehancer's densitometry-based film profiling, and the feasibility of true 3D LUT integration within ENB's HLSL shader constraints. The goal is to identify concrete, actionable improvements to Silent Horizons' film emulation system that push beyond the current state of the art in game modding.

**Key findings:**

1. The current 16³ 2D-strip LUT format is the primary bottleneck — it cannot capture cross-channel color interactions that define real film stock behavior
2. True 3D LUT support is **feasible in ENB** via Texture3D emulation using tiled 2D atlases, and this single change would be transformative
3. TreyM's dual-stage negative→print pipeline is the correct conceptual architecture, but most implementations lack the color science rigor of professional tools like Dehancer
4. The most impactful improvement is implementing a proper **photochemical emulation pipeline** in the shader: characteristic curve modeling → subtractive CMY color mixing → print stock contrast expansion
5. A Python-based LUT baking tool can generate scientifically grounded film stock LUTs from published sensitometric data

---

## Part I: How Professional Colorists Think About Color

### 1.1 The Modern Digital Color Pipeline

Professional colorists working in DaVinci Resolve, Baselight, or Lustre operate within a fundamentally different paradigm than ENB shader authors. The key distinction is **scene-referred vs. display-referred** thinking:

**Scene-referred (HDR/Linear):** The image exists as physical light values. A value of 2.0 means twice the luminance of 1.0. Color operations happen in linear light or a perceptually uniform log space. This is where your enbeffect.fx pipeline operates during stages 1-4 (bloom → tonemap → CG → AGIS).

**Display-referred (SDR/Gamma):** After the tonemap compresses the scene into [0,1], we're in display-referred territory. This is where LUTs are traditionally applied — they expect normalized Rec.709 gamma-encoded input and produce Rec.709 output. This is where your postpass LUT blending operates.

The critical insight is that **LUTs applied to display-referred images are inherently limited** because the tonemapper has already destroyed the luminance relationships that film stocks respond to. Professional colorists work with LUTs in scene-referred space (log-encoded) specifically because film's density response is logarithmic.

### 1.2 What Makes Great Colorists Great

Studying the work of colorists like Stefan Sonnenfeld (Company 3), Jill Bogdanowicz (Technicolor), and Walter Volpatto reveals consistent principles:

**Tonal separation through selective contrast:** Great grades don't just apply global contrast curves. They compress specific luminance zones while expanding others. The "Kodak 2383 look" is specifically about expanding mid-tone contrast while gently rolling highlights and lifting shadows into slightly warm territory.

**Cross-channel color coupling:** Real film doesn't process R, G, B independently. Each emulsion layer's response is influenced by adjacent layers through inter-image effects, halation, and chemical cross-talk. This is why a simple per-channel LUT can never fully capture a film stock's behavior — it requires 3D color mapping where the output blue depends not just on input blue, but on input red and green simultaneously.

**Perceptual uniformity:** Professional tools like DaVinci's Color Warper operate in perceptually uniform spaces (CIE Lab, IPT, JzAzBz). Adjustments that look "linear" in these spaces appear natural to the eye. Operating purely in RGB or even HSL creates uneven perceptual results.

### 1.3 Game Color Grading vs. Film Color Grading

Video game color grading has unique challenges that film doesn't face:

- **Infinite scene variety**: A film colorist grades specific shots. A game colorist must create a single pipeline that handles every possible scene — from bright snow fields to dark caves to orange sunsets
- **Non-authored lighting**: Unlike film, the lighting in every frame isn't carefully controlled by a cinematographer
- **Real-time constraints**: No offline rendering luxury; everything must run at 16ms or less

These constraints are exactly why ENB presets use per-TOD (Time of Day) parameter separation and weather blending — they're compensating for the lack of per-shot grading. Your 7-way ExtSep7 system and AGIS pipeline are already more sophisticated than most game engines' built-in grading.

---

## Part II: TreyM's Film Workshop — Architecture Deep Dive

### 2.1 The Dual-Stage Pipeline

TreyM's Film Workshop, originally an ENB mod for Fallout 4 (circa 2017), introduced a breakthrough concept to game modding: the **dual-stage LUT system** that emulates the physical film pipeline:

```
Digital Image → [Negative Stock LUT] → [Print Stock LUT] → Display
```

This mirrors the actual photochemical process:

```
Scene Light → [Camera Negative Film] → [Lab Development] → [Print Film] → [Projector/Screen]
```

The key innovation was recognizing that the "film look" is not a single transformation but the **product of two transforms** — the negative stock's spectral sensitivity and the print stock's density response. Different negative/print combinations produce radically different aesthetics, just as pairing Kodak Vision3 500T negative with Kodak 2383 print yields a different look than pairing it with Fujifilm 3513DI.

Film Workshop shipped with 5 negatives × 3 prints = 15 combinations in the free version, and 40+ stocks × multiple prints = 400+ combinations in the expanded version.

### 2.2 TreyM's Technical Approach

As a freelance colorist who knew HLSL, TreyM's approach was:

1. **Capture**: Take screenshots through a neutral identity LUT to establish the game's baseline color response
2. **Grade in Photoshop/DaVinci**: Apply the film emulation adjustments to the screenshot with the identity LUT overlaid, so the LUT captures the transformation
3. **Bake to 2D Strip LUT**: Export the graded identity as a 256×16 PNG (16³ lattice points)
4. **Dual application in shader**: Sample negative LUT first, then feed the result through the print LUT

### 2.3 Limitations of TreyM's Approach

While conceptually correct, TreyM's implementation has significant limitations:

**1D Strip LUT Precision Loss:** The 256×16 format encodes a 16×16×16 lattice — only 4,096 unique color mappings. Professional .cube files use 33³ = 35,937 or 65³ = 274,625 points. This means the ENB LUTs have **8.5× to 67× fewer sampling points** than industry-standard LUTs, causing visible quantization in smooth gradients and subtle color transitions.

**Bilinear vs. Tetrahedral Interpolation:** Your existing implementation supports both, which is excellent. However, the 16³ resolution makes interpolation accuracy even more critical — with so few lattice points, the interpolation method has an outsized impact on the final result.

**No Scene-Referred Application:** Both LUT stages are applied in display-referred (gamma-encoded) space. In a real film pipeline, the negative stock responds to *linear light*, not gamma-encoded values. This fundamentally changes the contrast and saturation behavior.

**Static Transformation:** A real negative stock's response changes with exposure level — pushed film looks different from pulled film. TreyM's LUTs are static transforms that can't adapt to scene brightness.

---

## Part III: The Science of Film — What LUTs Should Actually Model

### 3.1 The Characteristic Curve (H&D Curve)

Every film stock has a characteristic curve (Hurter & Driffield curve) that maps log exposure to optical density. This S-shaped curve has three critical regions:

**Toe (shadows):** Low exposure values produce minimal density. The toe region compresses shadow detail, creating the "lifted blacks" look associated with film. The toe's shape varies dramatically between stocks — Kodak Vision3 has a gentle toe, while Kodachrome has a steeper one.

**Linear region (mid-tones):** Where density increases approximately linearly with log exposure. The slope of this region is the film's **gamma** (γ). Negative stocks typically have γ ≈ 0.6-0.7 (low contrast, to preserve dynamic range). Print stocks have γ ≈ 2.5-3.0 (high contrast, to expand the neg's compressed range back to viewable contrast).

**Shoulder (highlights):** High exposure values asymptotically approach maximum density. This creates the "soft highlight rolloff" that's the most sought-after film characteristic. The shoulder behavior is what makes film "forgive" overexposure in a way digital sensors don't.

### 3.2 The Negative → Print Transform Chain

The complete photochemical pipeline involves these transforms, each with distinct color science:

```
Scene Luminance (linear)
    → Camera Lens (optical, wavelength-dependent)
    → Negative Film Emulsion (3-layer: Red/Green/Blue sensitive)
        Each layer has its own H&D curve
        Cross-layer effects (inter-image effects, halation)
    → Chemical Development (ECN-2 for motion picture)
        Temperature, time, agitation affect grain and contrast
    → Scanning/Telecine (Cineon/DPX log encoding)
    → Digital Grading (DaVinci, Baselight)
    → Print Stock (Kodak 2383, Fuji 3513)
        Its own 3-layer H&D response
        Different gamma than negative
    → Projection Light (D65 illuminant through print)
    → Screen (gain, ambient light)
```

**The most critical insight for ENB is that the negative and print transforms are fundamentally different operations:**

- The **negative transform** is a compression: wide dynamic range → narrow density range, with subtractive color mixing and logarithmic response
- The **print transform** is an expansion: narrow density range → viewable contrast, with its own subtractive color mixing, different spectral sensitivity, and typically warm-biased color rendering

### 3.3 Dehancer's Gold Standard Approach

Dehancer represents the current state of the art in film emulation. Their methodology is worth understanding deeply:

**Optical print sampling:** Rather than scanning negatives digitally (which introduces scanner-dependent artifacts), Dehancer optically prints each film stock onto photographic paper in their own darkroom. They control development chemistry, timing, and measure results with a spectrophotometer and densitometer.

**Three-exposure profiling:** Each stock is profiled at three exposure levels: -2 EV (underexposed), 0 EV (normal), and +2 EV (overexposed). This captures how the film's color response changes with exposure — something a single static LUT fundamentally cannot do.

**Non-linear interpolation model:** Between the three profiled exposures, Dehancer uses a custom non-linear interpolation model that accounts for perceptual color differences. They explicitly state that affine (linear) interpolation between sample points produces acceptable but not perceptually accurate results.

**Separated negative + print architecture:** Dehancer 5.0 introduced the ability to "print" any profiled negative onto different print media (Kodak 2383, Fuji 3513DI, Kodak Endura paper, or linear/Cineon output). This is architecturally identical to TreyM's concept but backed by real densitometric data.

### 3.4 Subtractive Color Mixing — The Missing Piece

Most game LUTs model film as an RGB→RGB transform, but real film uses **subtractive color mixing** (CMY). Each emulsion layer absorbs specific wavelengths:

- Cyan layer absorbs red light
- Magenta layer absorbs green light  
- Yellow layer absorbs blue light

The interaction between layers creates cross-channel effects that RGB LUTs can only approximate. For example, dense cyan and magenta layers together produce blue — but the specific *shade* of blue depends on the relative densities of each layer's H&D curve at that exposure level, which is a 3D relationship that 1D per-channel transforms can't capture.

This is the fundamental argument for 3D LUTs: they can encode arbitrary RGB→RGB mappings including cross-channel interactions.

---

## Part IV: 3D LUT Feasibility in ENB

### 4.1 The Core Challenge

ENB's shader model operates under several constraints:

1. **Texture2D only for loaded resources**: ENB loads textures via `ResourceName` annotations as 2D textures. There's no built-in Texture3D resource loading
2. **No .cube file parsing**: ENB's resource system handles image formats (PNG, DDS, BMP), not text-based LUT formats
3. **Shader Model 5.0**: Full DX11 feature set including Texture3D sampling, but only if you can get the data into a 3D texture

### 4.2 Solution: Tiled 2D Atlas → Software 3D Sampling

The standard solution used throughout the game industry is to **unwrap a 3D LUT into a 2D texture atlas**. This is exactly what your current 256×16 strip does for a 16³ LUT, but the approach scales to larger lattices.

**For a 33³ LUT (industry standard):**

A 33×33×33 lattice = 35,937 color entries. Unwrapped as a tiled 2D atlas:
- 33 tiles (one per blue slice), each 33×33 pixels
- Total texture: 1089 × 33 pixels (or rearranged as 33×33 tiles in a grid: e.g., 6×6 grid of 33×33 tiles = 198 × 198 pixels)
- This fits comfortably in a single texture

**For a 64³ LUT (high quality):**
- 64 tiles × 64×64 pixels each
- Arranged as 8×8 grid: 512 × 512 pixels — a standard texture size

**Optimal format for ENB: 512×512 PNG containing an 8×8 grid of 64×64 blue slices.**

This is the same approach used by Unreal Engine, Unity, OBS Studio, and Adobe's GPU-accelerated LUT processing.

### 4.3 HLSL Implementation: True 3D LUT Sampling from 2D Atlas

Here's the complete shader code for sampling a tiled 2D atlas as a 3D LUT, with both trilinear and tetrahedral interpolation:

```hlsl
//=============================================================================
// 3D LUT Sampling from Tiled 2D Atlas
// Supports both trilinear (GPU hardware) and tetrahedral (software) modes
//=============================================================================

// Configuration
#define LUT3D_SIZE       64    // Lattice size per axis (33 or 64)
#define LUT3D_TILES_X     8    // Tiles per row in atlas
#define LUT3D_TILES_Y     8    // Tiles per column in atlas
// Atlas dimensions: (LUT3D_SIZE * LUT3D_TILES_X) × (LUT3D_SIZE * LUT3D_TILES_Y)
// For 64³: 512 × 512 pixels

static const float  LUT3D_SCALE   = (float)(LUT3D_SIZE - 1) / (float)LUT3D_SIZE;
static const float  LUT3D_OFFSET  = 0.5 / (float)LUT3D_SIZE;
static const float2 LUT3D_ATLAS   = float2(LUT3D_SIZE * LUT3D_TILES_X,
                                            LUT3D_SIZE * LUT3D_TILES_Y);

// Convert 3D lattice coordinates to 2D atlas UV
float2 LUT3D_To2D(float3 uvw)
{
    float blueSlice = uvw.z * (LUT3D_SIZE - 1);
    float sliceFloor = floor(blueSlice);
    
    // Tile index in the grid
    float tileX = fmod(sliceFloor, LUT3D_TILES_X);
    float tileY = floor(sliceFloor / LUT3D_TILES_X);
    
    // UV within the tile (half-texel inset for proper sampling)
    float2 tileUV = float2(uvw.x * LUT3D_SCALE + LUT3D_OFFSET,
                           uvw.y * LUT3D_SCALE + LUT3D_OFFSET);
    
    // Final atlas UV
    float2 atlasUV;
    atlasUV.x = (tileX * LUT3D_SIZE + tileUV.x * LUT3D_SIZE) / LUT3D_ATLAS.x;
    atlasUV.y = (tileY * LUT3D_SIZE + tileUV.y * LUT3D_SIZE) / LUT3D_ATLAS.y;
    
    return atlasUV;
}

// Trilinear sampling (hardware-accelerated, fast)
float3 SampleLUT3D_Trilinear(Texture2D atlas, SamplerState samp, float3 color)
{
    float3 c = saturate(color);
    
    float blueSlice = c.b * (LUT3D_SIZE - 1);
    float sliceFloor = floor(blueSlice);
    float sliceCeil  = min(sliceFloor + 1.0, LUT3D_SIZE - 1);
    float sliceFrac  = blueSlice - sliceFloor;
    
    // Sample both blue slices and lerp
    float3 uvw0 = float3(c.r, c.g, sliceFloor / (LUT3D_SIZE - 1));
    float3 uvw1 = float3(c.r, c.g, sliceCeil  / (LUT3D_SIZE - 1));
    
    float3 c0 = atlas.SampleLevel(samp, LUT3D_To2D(uvw0), 0).rgb;
    float3 c1 = atlas.SampleLevel(samp, LUT3D_To2D(uvw1), 0).rgb;
    
    return lerp(c0, c1, sliceFrac);
}

// Point-sample a specific lattice point (for tetrahedral)
float3 LUT3D_FetchPoint(Texture2D atlas, int3 coord)
{
    coord = clamp(coord, 0, LUT3D_SIZE - 1);
    
    float tileX = fmod((float)coord.z, LUT3D_TILES_X);
    float tileY = floor((float)coord.z / LUT3D_TILES_X);
    
    float2 uv;
    uv.x = (tileX * LUT3D_SIZE + coord.x + 0.5) / LUT3D_ATLAS.x;
    uv.y = (tileY * LUT3D_SIZE + coord.y + 0.5) / LUT3D_ATLAS.y;
    
    return atlas.SampleLevel(Point_Sampler, uv, 0).rgb;
}

// Tetrahedral interpolation (superior accuracy, 4 texture fetches)
// Based on NVIDIA GTC 2010 poster and OpenColorIO implementation
float3 SampleLUT3D_Tetrahedral(Texture2D atlas, float3 color)
{
    float3 c = saturate(color) * (LUT3D_SIZE - 1);
    int3   base = (int3)floor(c);
    float3 f = c - (float3)base;
    
    // Fetch the base vertex (always needed)
    float3 v0 = LUT3D_FetchPoint(atlas, base);
    float3 v1, v2, v3;
    float  w0, w1, w2, w3;
    
    // Determine which tetrahedron the point falls in
    // 6 tetrahedra partition each cube cell
    if (f.r >= f.g)
    {
        if (f.g >= f.b)          // R >= G >= B
        {
            v1 = LUT3D_FetchPoint(atlas, base + int3(1,0,0));
            v2 = LUT3D_FetchPoint(atlas, base + int3(1,1,0));
            v3 = LUT3D_FetchPoint(atlas, base + int3(1,1,1));
            w0 = 1.0 - f.r; w1 = f.r - f.g; w2 = f.g - f.b; w3 = f.b;
        }
        else if (f.r >= f.b)     // R >= B >= G
        {
            v1 = LUT3D_FetchPoint(atlas, base + int3(1,0,0));
            v2 = LUT3D_FetchPoint(atlas, base + int3(1,0,1));
            v3 = LUT3D_FetchPoint(atlas, base + int3(1,1,1));
            w0 = 1.0 - f.r; w1 = f.r - f.b; w2 = f.b - f.g; w3 = f.g;
        }
        else                      // B >= R >= G
        {
            v1 = LUT3D_FetchPoint(atlas, base + int3(0,0,1));
            v2 = LUT3D_FetchPoint(atlas, base + int3(1,0,1));
            v3 = LUT3D_FetchPoint(atlas, base + int3(1,1,1));
            w0 = 1.0 - f.b; w1 = f.b - f.r; w2 = f.r - f.g; w3 = f.g;
        }
    }
    else
    {
        if (f.g >= f.b)
        {
            if (f.r >= f.b)       // G >= R >= B
            {
                v1 = LUT3D_FetchPoint(atlas, base + int3(0,1,0));
                v2 = LUT3D_FetchPoint(atlas, base + int3(1,1,0));
                v3 = LUT3D_FetchPoint(atlas, base + int3(1,1,1));
                w0 = 1.0 - f.g; w1 = f.g - f.r; w2 = f.r - f.b; w3 = f.b;
            }
            else                  // G >= B >= R
            {
                v1 = LUT3D_FetchPoint(atlas, base + int3(0,1,0));
                v2 = LUT3D_FetchPoint(atlas, base + int3(0,1,1));
                v3 = LUT3D_FetchPoint(atlas, base + int3(1,1,1));
                w0 = 1.0 - f.g; w1 = f.g - f.b; w2 = f.b - f.r; w3 = f.r;
            }
        }
        else                      // B >= G >= R
        {
            v1 = LUT3D_FetchPoint(atlas, base + int3(0,0,1));
            v2 = LUT3D_FetchPoint(atlas, base + int3(0,1,1));
            v3 = LUT3D_FetchPoint(atlas, base + int3(1,1,1));
            w0 = 1.0 - f.b; w1 = f.b - f.g; w2 = f.g - f.r; w3 = f.r;
        }
    }
    
    return v0 * w0 + v1 * w1 + v2 * w2 + v3 * w3;
}

// Unified dispatcher
float3 SampleLUT3D(Texture2D atlas, SamplerState samp, float3 color, int mode)
{
    if (mode == 0)
        return SampleLUT3D_Trilinear(atlas, samp, color);
    else
        return SampleLUT3D_Tetrahedral(atlas, color);
}
```

### 4.4 Performance Analysis

**Trilinear 3D (via 2D atlas):** 2 texture samples + 1 lerp = essentially free on modern GPUs. The only overhead vs. current bilinear 2D strip LUT is the additional math for tile coordinate computation.

**Tetrahedral 3D:** 4 texture samples with point filtering. Marginally more expensive than trilinear but produces measurably better results. Based on NVIDIA's 2010 benchmarks, tetrahedral can actually be faster than trilinear on some architectures because it requires 4 fetches instead of 8 (trilinear requires 2×4 = 8 fetches for true hardware trilinear of a 3D texture, vs. 4 for tetrahedral).

**Memory:** A 64³ atlas at 512×512 RGBA8 = 1MB. A 33³ atlas at ~200×200 = ~160KB. Negligible on any GPU from the last 15 years.

### 4.5 Compatibility with ENB's Resource System

ENB loads textures via annotations like:
```hlsl
Texture2D TexLUT3D_Negative <string ResourceName="Textures/LUTs/3D/Kodak_5219_500T_64.png";>;
Texture2D TexLUT3D_Print    <string ResourceName="Textures/LUTs/3D/Kodak_2383_D55_64.png";>;
```

The tiled 2D atlas approach requires **zero changes to ENB's resource loading**. It's just a PNG texture, loaded like any other. The 3D interpretation happens entirely in shader math.

---

## Part V: Proposed Architecture — Photochemical Emulation Pipeline

### 5.1 The Full Pipeline

Here's the proposed film emulation pipeline, replacing the current simple gamut + log encoding system in `ApplyFilmEmulation()`:

```
Post-Tonemap Rec.709 Image
    │
    ├── [1] Linearize (gamma decode)
    │      Remove sRGB gamma to work in linear light
    │
    ├── [2] Negative Stock Emulation
    │      Option A: 3D LUT (baked from H&D curves)
    │      Option B: Analytical per-channel H&D curves
    │         • Separate R/G/B characteristic curves
    │         • Logarithmic density response
    │         • Toe/shoulder/gamma per channel
    │         • Color temperature sensitivity
    │
    ├── [3] Film Grain (optional, before print)
    │      Grain is a property of the negative
    │      Density-dependent grain size and intensity
    │
    ├── [4] Print Stock Emulation
    │      3D LUT or analytical curves
    │         • High gamma expansion (2.5-3.0)
    │         • Subtractive CMY color rendering
    │         • Stock-specific color bias
    │         • Highlight rolloff characteristics
    │
    ├── [5] Halation (optional)
    │      Red-channel bloom from light scatter
    │      in film base. Applied between neg and print
    │      in the real pipeline, but can be approximated
    │      as a post-print red bloom.
    │
    └── [6] Re-encode to sRGB
           Apply sRGB gamma for display
```

### 5.2 Analytical Film Stock Modeling (No LUT Required)

For ultimate flexibility, characteristic curves can be modeled analytically. This allows runtime parameter adjustment (exposure push/pull, development time, etc.):

```hlsl
// Attempt at modeling a film emulsion layer's density response
// Based on published sensitometric data for Kodak Vision3 stocks
struct FilmStockParams
{
    float3 Fog;         // Base fog density per RGB layer (minimum density)
    float3 Gamma;       // Slope of linear region per layer
    float3 Dmax;        // Maximum density per layer
    float3 Speed;       // Film speed (log exposure offset) per layer
    float3 ToeLength;   // Toe region extent per layer
    float3 ShoulderLen; // Shoulder region extent per layer
    float  ColorTemp;   // Balanced color temperature (5500 = daylight, 3200 = tungsten)
};

// Attempt at characteristic curve approximation using a smooth spline
// This models the H&D curve as: D = Fog + (Dmax - Fog) * sigmoid(Gamma * (logE - Speed))
float3 CharacteristicCurve(float3 linearRGB, FilmStockParams stock)
{
    // Convert linear light to log exposure
    float3 logE = log10(max(linearRGB, 1e-10));
    
    // Apply speed offset (shifts curve along log-exposure axis)
    float3 x = (logE - stock.Speed) * stock.Gamma;
    
    // Smooth S-curve using modified sigmoid
    // The toe and shoulder parameters control asymmetry
    float3 toe = stock.ToeLength;
    float3 sho = stock.ShoulderLen;
    
    // Attempt at asymmetric sigmoid for realistic H&D approximation
    float3 t = x;
    float3 toeRegion  = toe * log(1.0 + exp(t / toe));
    float3 shoRegion  = sho - sho * log(1.0 + exp((sho - t) / sho));
    float3 curve = lerp(toeRegion, shoRegion, smoothstep(-1, 3, t));
    
    // Scale to density range
    float3 density = stock.Fog + (stock.Dmax - stock.Fog) * saturate(curve);
    
    return density;
}

// Color temperature sensitivity
// Tungsten-balanced film (e.g., 500T) under daylight → blue shift
// Daylight-balanced film (e.g., 250D) under tungsten → warm shift
float3 ApplyColorTempSensitivity(float3 color, float filmTemp, float sceneTemp)
{
    // Simplified chromatic adaptation using von Kries-style transform
    float tempRatio = filmTemp / max(sceneTemp, 1.0);
    float3 adaptation = float3(
        pow(tempRatio, 0.5),    // Red: less sensitive to temp mismatch
        1.0,                     // Green: reference
        pow(1.0/tempRatio, 0.5) // Blue: most sensitive
    );
    return color * adaptation;
}
```

### 5.3 Preset Film Stock Parameters

Based on published Kodak and Fujifilm technical data sheets, published characteristic curves, and Dehancer's known profiling results:

```hlsl
// These are approximations based on published sensitometric data.
// Actual values would need calibration against real film samples.

static const FilmStockParams Stock_Kodak5219_500T = {
    float3(0.10, 0.08, 0.12),  // Fog: slightly higher blue fog (cool stock)
    float3(0.62, 0.65, 0.58),  // Gamma: lower than print, blue slightly less
    float3(2.60, 2.70, 2.50),  // Dmax
    float3(-1.80, -1.90, -1.70), // Speed: fast stock, blue fastest
    float3(0.40, 0.35, 0.45),  // ToeLength
    float3(0.30, 0.30, 0.35),  // ShoulderLen
    3200.0                       // Tungsten balanced
};

static const FilmStockParams Stock_Kodak5207_250D = {
    float3(0.08, 0.06, 0.10),  // Fog: cleaner shadows
    float3(0.60, 0.63, 0.55),  // Gamma: fine grain stock, slightly less contrast
    float3(2.80, 2.90, 2.70),  // Dmax: higher Dmax = more detail in highlights
    float3(-1.50, -1.55, -1.40), // Speed: slower stock
    float3(0.35, 0.30, 0.40),  // ToeLength
    float3(0.35, 0.35, 0.40),  // ShoulderLen
    5500.0                       // Daylight balanced
};

static const FilmStockParams Print_Kodak2383 = {
    float3(0.05, 0.04, 0.06),  // Fog: very clean base
    float3(2.80, 2.70, 2.60),  // Gamma: HIGH - this is the contrast expansion
    float3(3.20, 3.10, 3.00),  // Dmax: high density capability
    float3(-0.50, -0.55, -0.45), // Speed
    float3(0.20, 0.18, 0.22),  // ToeLength: shorter toe = punchier shadows
    float3(0.25, 0.25, 0.30),  // ShoulderLen: warm highlight rolloff
    5500.0                       // Illuminant reference
};

static const FilmStockParams Print_Fuji3513DI = {
    float3(0.04, 0.03, 0.05),  // Fog: slightly cleaner than 2383
    float3(2.60, 2.70, 2.80),  // Gamma: blue gamma higher = cooler rendering
    float3(3.00, 3.10, 3.20),  // Dmax
    float3(-0.55, -0.50, -0.50), // Speed
    float3(0.22, 0.20, 0.18),  // ToeLength
    float3(0.30, 0.28, 0.22),  // ShoulderLen: cleaner highlight rolloff than 2383
    6000.0                       // Slightly cooler illuminant reference
};
```

### 5.4 Hybrid Approach: Analytical + 3D LUT

The most practical approach combines both:

1. **Analytical H&D curves** for the negative stock (fast, parametric, adjustable)
2. **3D LUT** for the print stock (captures the complex cross-channel behavior of print film)
3. **Shader parameters** for exposure push/pull, color temperature override, grain character

This gives maximum flexibility while maintaining color accuracy where it matters most (the print stock's subtractive color mixing).

---

## Part VI: Python LUT Generation Pipeline

### 6.1 Overview

A Python tool that generates scientifically-grounded 3D LUT atlases from published film stock data. The output is a tiled 2D PNG ready for use in ENB.

The tool implements:
- H&D curve modeling per RGB channel
- Negative→Print chain computation
- 3D LUT baking at configurable resolution (33³ or 64³)
- Export as tiled 2D atlas PNG
- Also exports .cube format for validation in DaVinci Resolve

### 6.2 Comparison: Current 16³ vs. Proposed 64³

| Feature | Current (16³ strip) | Proposed (64³ atlas) |
|---------|-------------------|---------------------|
| Lattice points | 4,096 | 262,144 |
| Cross-channel accuracy | None (bilinear per-axis) | Full 3D mapping |
| Texture size | 256×16 | 512×512 |
| Memory cost | ~16 KB | ~1 MB |
| Interpolation | Bilinear or Tetrahedral | Trilinear or Tetrahedral |
| Film pipeline | Single-stage | Negative + Print chain |
| Industry equivalence | Below consumer | Professional grade |

---

## Part VII: Immediate Action Items

### 7.1 Phase 1: 3D LUT Infrastructure (High Impact, Moderate Effort)

1. **Implement `SampleLUT3D_Trilinear` and `SampleLUT3D_Tetrahedral`** in a new helper header `enbHelper_LUT3D.fxh`
2. **Generate test 64³ identity atlas** to verify sampling correctness
3. **Add dual-LUT texture slots** to postpass: `TexLUT3D_Negative` and `TexLUT3D_Print`
4. **Wire into `PS_ColorGrade`** as an alternative to the existing 39-LUT blend system

### 7.2 Phase 2: Python LUT Generator (High Impact, Moderate Effort)

1. **Build the film stock modeling library** with published H&D curve data
2. **Generate initial stock set**: Kodak 5219 500T, Kodak 5207 250D, Kodak 2383, Fuji 3513DI
3. **Validate against Dehancer** by comparing output on identical test images
4. **Export as 64³ tiled atlases** for ENB consumption

### 7.3 Phase 3: Shader Pipeline Enhancement (Medium Impact, High Effort)

1. **Add linearization/re-encoding** around the film emulation stage
2. **Implement analytical H&D curve engine** as an alternative to LUT-based negatives
3. **Add exposure push/pull parameter** that shifts the H&D curve
4. **Implement CMY color head simulation** (subtractive printing color balance)
5. **Add halation pass** between negative and print stages (red-channel gaussian blur of bright areas)

### 7.4 Phase 4: SkyrimBridge Integration (Medium Impact, Low Effort)

1. **Time-of-day adaptive film temperature**: Dawn/sunset → warm scene temp (3200-4000K), day → daylight (5500K), interior → tungsten (2800-3200K). Feed scene color temperature to the negative's color sensitivity model
2. **Weather-adaptive exposure**: Rain → underexpose (pushed look), clear → normal
3. **Interior film stock switching**: Auto-select tungsten-balanced stocks for interiors

---

## Appendix A: Key Color Science References

1. Hurter & Driffield, "Photochemical Investigations and a New Method of Determination of the Sensitiveness of Photographic Plates" (1890) — original H&D curve
2. NVIDIA, "Real-Time Color Space Conversion for High Resolution Video" (GTC 2010) — tetrahedral interpolation on GPU
3. Kasson & Plouffe, "Tetrahedral Interpolation Technique for Color Space Conversion" (Grafica Obscura, 1995)
4. NVIDIA GPU Gems 2, Chapter 24, "Using Lookup Tables to Accelerate Color Transformations"
5. Juan Melara, "Print Film Emulation" (juanmelara.com.au) — free Kodak 2383 PFE LUTs with documentation
6. Dehancer, "How We Build Film Profiles" (dehancer.com/learn) — densitometry-based profiling methodology
7. Kodak, "VISION Color Print Film 2383/3383 Technical Data" — official sensitometric specifications
8. Vandenberg & Andriani, "A Survey on 3D-LUT Performance in 10-bit and 12-bit HDR BT.2100 PQ" — tetrahedral vs. trilinear accuracy comparison

## Appendix B: Film Stock Quick Reference

| Stock | Type | Balanced | Character |
|-------|------|----------|-----------|
| Kodak 5207 250D | Negative | Daylight | Fine grain, clean, natural |
| Kodak 5219 500T | Negative | Tungsten | Fast, slightly more grain, cool tint in daylight |
| Kodak 5213 200T | Negative | Tungsten | Classic cinema look, predecessor to 5219 |
| Kodak 2383 | Print | D55 | Warm, rich mid-tones, soft highlight rolloff, deep blacks |
| Kodak 2393 | Print | D55 | Higher contrast version of 2383 |
| Fuji 3510 | Print | D60 | Clean, neutral, slightly cool |
| Fuji 3513DI | Print | D60 | Digital intermediate optimized, cooler than 2383 |
| Fuji 3521XD | Print | D60 | Extended density range |
| Kodachrome 25 | Reversal | Daylight | Extremely saturated, warm, iconic |
| Kodachrome 64 | Reversal | Daylight | Similar to K25 but faster, slightly less saturated |
| Fuji Velvia 50 | Reversal | Daylight | Hyper-saturated greens and blues |
| Cinestill 800T | Negative | Tungsten | Vision3 500T with remjet removed, halation-prone |
