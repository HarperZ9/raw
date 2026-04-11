# Film Color Grading Techniques for Skyrim ENB
## Advanced Research Compendium — Continuation

*Compiled March 2026 for SkyrimBridge / Silent Horizons ENB Development*
*Continues from: ShaderTechniques_MasterReference.md (Sections 6–7)*

---

## Table of Contents

1. [Photochemical Film Pipeline — The Complete Model](#1-photochemical-film-pipeline)
2. [Subtractive Color & Film Density](#2-subtractive-color--film-density)
3. [Film Stock Characteristic Curves](#3-film-stock-characteristic-curves)
4. [Negative-to-Print Emulation Pipeline](#4-negative-to-print-emulation-pipeline)
5. [AgX Display Rendering Transform — Deep Dive](#5-agx-display-rendering-transform)
6. [Technicolor Process Emulation](#6-technicolor-process-emulation)
7. [Halation — Physics and Implementation](#7-halation--physics-and-implementation)
8. [Advanced Film Grain — Beyond Overlay](#8-advanced-film-grain)
9. [Printer Lights & CMY Color Head](#9-printer-lights--cmy-color-head)
10. [Integration Architecture for Silent Horizons](#10-integration-architecture)
11. [Bibliography & Sources](#11-bibliography)

---

## 1. Photochemical Film Pipeline — The Complete Model

The fundamental insight from professional colorists (CinePrint35, Dehancer, PixelTools Film Lab) is that authentic film emulation requires modeling the **entire photochemical chain**, not just applying a LUT at the end. The pipeline is:

```
Scene Light
  → Camera Negative (Kodak Vision3 5219/5207/5213, Fuji Eterna, etc.)
    → Optical Printing (light passes through negative onto print stock)
      → Print Film (Kodak 2383, Fuji 3513)
        → Projection / Scan
          → Display
```

Each stage introduces distinct transformations:

**Camera Negative Stage:**
- Logarithmic light capture (inherent to silver halide chemistry)
- Per-layer spectral sensitivity (RGB layers have different response curves)
- Inter-layer coupling (dye coupler cross-talk between emulsion layers)
- Characteristic curve shape (toe, linear region, shoulder — unique per stock)

**Optical Printing Stage:**
- Subtractive color mixing (CMY dyes block light — saturated colors darken)
- Print stock's own characteristic curve imposed on top
- Printer lights (R/G/B exposure controls — the original "color grading")
- Highlight rolloff from chemical density ceiling

**Projection/Scan Stage:**
- Display transform (gamma, white point)
- Additional contrast from projection optics

### Why This Matters for ENB

The community standard approach (TreyM's LUT pipeline, l00ping's effects) applies a single LUT after tonemapping. This captures the **output appearance** but misses the **process behaviors** — how the image responds to different inputs. A scene with a bright red neon sign behaves very differently through a subtractive pipeline than through a LUT. The neon gets denser and darker as it saturates (film behavior), rather than brighter and more vivid (digital/additive behavior).

**For SkyrimBridge:** Weather data, time-of-day, and combat state can drive parameters that modulate the film pipeline stages independently. A thunderstorm could increase print contrast while reducing negative sensitivity. Combat could shift the characteristic curve to emphasize shadows (lifted blacks, reduced toe).

---

## 2. Subtractive Color & Film Density

### The Core Principle

In physical film, color is created by **dye layers that block light** (subtractive). As a color becomes more saturated, more dye is deposited, which means less light passes through. Therefore:

**Saturated colors should appear DARKER, not brighter.**

This is the single most important distinction between digital color and film color, and is called **density**. Standard digital tools (and standard ENB saturation operations) are additive — increasing saturation increases luminance. Film does the opposite.

### HLSL Implementation — Film Density

```hlsl
// Subtractive density: as saturation increases, luminance decreases
// This is the "Hollywood secret sauce" referenced by multiple colorists
float3 ApplySubtractiveDensity(float3 color, float densityAmount)
{
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    
    // Compute saturation as distance from achromatic axis
    float3 delta = color - luma;
    float saturation = length(delta);
    
    // Film density: higher saturation = lower luminance
    // Using power curve for natural rolloff
    float densityFactor = 1.0 - saturate(saturation * densityAmount);
    
    // Apply density reduction to luminance while preserving chrominance
    float3 chroma = (luma > 0.001) ? (color / luma) : float3(1, 1, 1);
    float newLuma = luma * densityFactor;
    
    return chroma * newLuma;
}
```

### Advanced: Per-Hue Density (Chen Spherical Model)

Professional tools (like the "Hue Shift" DCTL) use the Chen spherical color model rather than HSL/HSV for density operations, because HSL/HSV introduce discontinuities at the primary/secondary boundaries. The approach:

```hlsl
// Per-hue density control — different film stocks have different
// density responses per color region
// Red/magenta tends to densify more than green/cyan in Kodak stocks
float3 ApplyPerHueDensity(float3 color, float6 hueWeights)
{
    // hueWeights: [Red, Yellow, Green, Cyan, Blue, Magenta]
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    float3 hsv = RGBtoHSV(color);
    float hue = hsv.x; // 0..1
    float sat = hsv.y;
    
    // 6-band hue weighting with smooth interpolation
    float hueIdx = hue * 6.0;
    float w0 = smoothstep(1.0, 0.0, abs(frac(hueIdx / 6.0 - 0.0 / 6.0) * 6.0 - 3.0) - 2.0);
    float w1 = smoothstep(1.0, 0.0, abs(frac(hueIdx / 6.0 - 1.0 / 6.0) * 6.0 - 3.0) - 2.0);
    // ... (repeat for all 6 hue bands)
    
    float densityWeight = w0 * hueWeights[0] + w1 * hueWeights[1] /* + ... */;
    
    // Apply density proportional to saturation and hue weight
    float density = sat * densityWeight;
    float3 result = color * (1.0 - density);
    
    return max(result, 0.0);
}
```

### Hue Shifts Under Density

Real film doesn't just darken saturated colors — it **shifts their hue** as density increases. On Kodak stocks, reds shift toward orange/yellow as they densify. Blues shift toward cyan. This is because the three dye layers have different density ceilings and coupling characteristics.

```hlsl
// Film-like hue shift under density
// As colors saturate and densify, they rotate in hue space
float3 ApplyDensityHueShift(float3 color, float amount)
{
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    float sat = length(color - luma);
    
    // Hue rotation proportional to saturation × density
    float3 hsv = RGBtoHSV(color);
    
    // Kodak-style: reds → warm, blues → cool
    // The shift direction depends on where in hue space we are
    float hueShift = sat * amount * 0.02; // Very subtle
    
    // Warm hues (reds/yellows) shift toward yellow
    // Cool hues (blues/cyans) shift toward cyan
    float warmMask = smoothstep(0.0, 0.15, hsv.x) * smoothstep(0.45, 0.30, hsv.x);
    float coolMask = smoothstep(0.50, 0.65, hsv.x) * smoothstep(0.85, 0.70, hsv.x);
    
    hsv.x += hueShift * warmMask * 0.5;  // Warm → more yellow
    hsv.x -= hueShift * coolMask * 0.3;  // Cool → more cyan
    hsv.x = frac(hsv.x); // Wrap
    
    return HSVtoRGB(hsv);
}
```

---

## 3. Film Stock Characteristic Curves

### The Hurter-Driffield (H&D) Curve

Every film stock has a characteristic curve (also called sensitometric curve or D-log H curve) that describes how it maps exposure (log H) to density (D). The curve has three regions:

```
Density
  |          ___________  ← Shoulder (highlight compression)
  |         /
  |        /  ← Linear region (gamma/contrast)
  |       /
  |    __/    ← Toe (shadow lift)
  |___/
  +-------------------→ Log Exposure
```

### Key Stock Characteristics

**Kodak Vision3 500T (5219) — The Modern Standard:**
- Tungsten balanced (3200K)
- Wide latitude (~14 stops)
- Moderate gamma in linear region
- Gentle toe — shadow detail is preserved but slightly warm
- Smooth shoulder — highlights roll off gracefully
- Warm skin tones, rich shadow detail
- Notable halation in highlights (red/orange glow)
- Used in: Dunkirk, La La Land, The Revenant, Star Wars VII-IX

**Kodak Vision3 250D (5207):**
- Daylight balanced (5500K)
- Slightly narrower latitude than 500T
- Higher contrast (steeper linear region)
- Cleaner highlights, more vivid colors
- Less halation than 500T
- Used in: Marriage Story, Little Women, Midsommar

**Kodak Vision3 200T (5213):**
- Tungsten balanced, finer grain than 500T
- Most controlled highlight rolloff of the Vision3 family
- Excellent in high-contrast scenarios
- Slightly cooler than 500T

**Fuji Eterna (various — discontinued):**
- Cooler overall palette than Kodak
- Muted saturation ("pastel" quality)
- Beautiful skin tone rendering (less warm than Kodak)
- Softer grain structure
- Used in: LOTR (printed on Fuji), Lost in Translation

**Kodak 2383 (Print Film):**
- The ONLY color print film still manufactured
- Applied as final stage — imparts S-curve contrast
- Warms highlights, adds density to shadows
- Characteristic orange/warm bias in upper midtones
- Available in DaVinci Resolve as built-in LUT (D55/D60/D65 white points)
- Expected input: Cineon Log encoded image

### HLSL: Parameterized Characteristic Curve

```hlsl
// Attempt to analytically model a characteristic curve with
// independently controllable toe, linear region, and shoulder
// Based on Lottes (AMD) parameterized curve, adapted for film use
float FilmCurve(float x, float toe, float gamma, float shoulder, float whitePoint)
{
    // Toe region: power function
    float toeRegion = pow(x / (x + toe), gamma);
    
    // Shoulder region: soft clamp toward white point
    float shoulderRegion = 1.0 - pow(1.0 - saturate(x / whitePoint), shoulder);
    
    // Blend between toe-dominated (shadows) and shoulder-dominated (highlights)
    float blend = saturate(x / (whitePoint * 0.5));
    
    return lerp(toeRegion, shoulderRegion, blend * blend);
}

// Apply per-channel with different parameters to emulate
// inter-layer coupling differences between film stocks
float3 ApplyFilmCharacteristic(float3 logColor, FilmStockParams stock)
{
    float3 result;
    result.r = FilmCurve(logColor.r, stock.toeR, stock.gammaR, stock.shoulderR, stock.wpR);
    result.g = FilmCurve(logColor.g, stock.toeG, stock.gammaG, stock.shoulderG, stock.wpG);
    result.b = FilmCurve(logColor.b, stock.toeB, stock.gammaB, stock.shoulderB, stock.wpB);
    return result;
}
```

### Film Stock Preset Values

Approximate parameters derived from published sensitometric data:

```hlsl
// Kodak Vision3 500T character
static const FilmStockParams STOCK_KODAK_500T = {
    // toe:   R=0.12, G=0.10, B=0.14  (blue has deeper toe — warm shadows)
    // gamma: R=0.58, G=0.55, B=0.52  (red has slightly more contrast)
    // shoulder: R=2.2, G=2.4, B=2.8  (blue compresses first — warm highlights)
    // white point: R=16.0, G=14.0, B=12.0
    0.12, 0.10, 0.14,  // toeR, toeG, toeB
    0.58, 0.55, 0.52,  // gammaR, gammaG, gammaB
    2.2,  2.4,  2.8,   // shoulderR, shoulderG, shoulderB
    16.0, 14.0, 12.0   // wpR, wpG, wpB
};

// Fuji Eterna character
static const FilmStockParams STOCK_FUJI_ETERNA = {
    // Cooler: less red toe, more blue shoulder headroom
    0.10, 0.10, 0.10,  // More neutral toe
    0.52, 0.55, 0.56,  // Blue has more contrast
    2.6,  2.4,  2.2,   // Red compresses first — cooler highlights
    14.0, 14.0, 15.0
};
```

---

## 4. Negative-to-Print Emulation Pipeline

### The Full Shader Pipeline

The state-of-the-art approach (used by Dehancer, CinePrint35, PixelTools Film Lab) structures the entire emulation as a multi-stage pipeline that mirrors the physical process:

```hlsl
float3 FullFilmEmulation(float3 linearScene, FilmEmulationParams params)
{
    // ──── Stage 1: Camera Transform (IDT) ────
    // Convert from sRGB linear to scene-referred working space
    float3 scene = mul(MAT_709_to_AP1, linearScene);
    
    // ──── Stage 2: Exposure (Printer Lights) ────
    // R/G/B gain controls — this is the original "color timing"
    scene *= params.printerLights; // float3 of per-channel gains
    
    // ──── Stage 3: Log Encoding ────
    // Encode to log space matching the target negative stock
    float3 logScene = LogEncode_LogC(max(scene, DELTA));
    
    // ──── Stage 4: Negative Film Response ────
    // Apply characteristic curve of the negative stock
    float3 negative = ApplyFilmCharacteristic(logScene, params.negStock);
    
    // ──── Stage 5: Subtractive Color / Density ────
    // Film density — saturated colors darken
    negative = ApplySubtractiveDensity(negative, params.densityAmount);
    
    // ──── Stage 6: Inter-layer Cross-talk ────
    // Simulate dye coupler cross-contamination between emulsion layers
    negative = ApplyCrossTalk(negative, params.crossTalkMatrix);
    
    // ──── Stage 7: Print Film Response ────
    // Apply the print stock's own characteristic curve
    // This is Kodak 2383 for most theatrical work
    float3 print = ApplyFilmCharacteristic(negative, params.printStock);
    
    // ──── Stage 8: Halation ────
    // Red/orange glow around bright areas (done in linear before display)
    // NOTE: This requires a spatial blur — see Section 7
    // print = ApplyHalation(print, params.halation);
    
    // ──── Stage 9: Output Display Transform ────
    // Convert back to display space
    float3 display = mul(MAT_AP1_to_709, print);
    
    return saturate(display);
}
```

### Cross-talk Matrix

Dye coupler cross-contamination creates subtle color shifts that are fundamental to the "film look." Each layer bleeds slightly into adjacent layers:

```hlsl
// Cross-talk: each channel picks up a fraction of adjacent channels
// This matrix is specific to the negative stock
float3 ApplyCrossTalk(float3 color, float3x3 crossTalkMat)
{
    // Example cross-talk matrix for Kodak Vision3:
    // Red picks up ~5% green and ~2% blue
    // Green picks up ~3% red and ~3% blue
    // Blue picks up ~2% red and ~4% green
    return mul(crossTalkMat, color);
}

static const float3x3 CROSSTALK_KODAK_V3 = float3x3(
    0.93, 0.05, 0.02,  // Red output
    0.03, 0.94, 0.03,  // Green output
    0.02, 0.04, 0.94   // Blue output
);

static const float3x3 CROSSTALK_FUJI_ETERNA = float3x3(
    0.95, 0.03, 0.02,
    0.02, 0.95, 0.03,
    0.01, 0.03, 0.96   // Fuji has less cross-talk — "cleaner" separation
);
```

---

## 5. AgX Display Rendering Transform — Deep Dive

AgX (created by Troy Sobotka) is a display rendering transform designed to mimic how chemical film handles color and light. It provides smooth highlight rolloff, natural desaturation toward white, and hue-stable behavior across the dynamic range — properties that ACES famously struggles with.

### AgX Pipeline Steps

```
1. Assume sRGB linear working space
2. Clip negative values (scene-referred data only)
3. Apply "inset" matrix (gamut reshaping — maps sRGB primaries inward)
4. Log2 encode (maps dynamic range to 0..1)
5. Apply sigmoid tonescale (the actual "tone curve")
6. Apply EOTF (gamma for display)
7. Optional: Apply "look" (saturation boost for "Punchy" variant)
```

### HLSL Implementation (Analytical — No LUT)

Based on the bWFuanVzYWth/AgX analytical implementation:

```hlsl
// AgX inset matrix — maps sRGB primaries inward to prevent
// out-of-gamut values during the tonemap
static const float3x3 AGX_INSET = float3x3(
    0.8424790622530940, 0.0784336015616190, 0.0792237324783498,
    0.0423282422610123, 0.8784686295903880, 0.0791846348885739,
    0.0423756549057051, 0.0784336015616190, 0.8791907271818960
);

static const float3x3 AGX_INSET_INV = float3x3(
     1.19687900512017, -0.0980208811401368, -0.0990434085205346,
    -0.0528968517574562,  1.15190312990417,  -0.0989611768448433,
    -0.0529716355144438, -0.0980434501171241,  1.15107367264116
);

// AgX sigmoid curve — piecewise for precision
// Computed analytically (not polynomial approximation — avoids banding)
float3 AgXSigmoid(float3 x)
{
    const float threshold = 0.6060606060606061;
    const float a_up = 69.86278913545539;
    const float a_down = 59.507875;
    const float b_up = 3.25;      // 13.0 / 4.0
    const float b_down = 3.0;
    const float c_up = -0.307692; // -4.0 / 13.0
    const float c_down = -0.333333; // -1.0 / 3.0
    
    float3 mask = step(x, threshold);
    float3 a = lerp(a_up, a_down, mask);
    float3 b = lerp(b_up, b_down, mask);
    float3 c = lerp(c_up, c_down, mask);
    
    return 0.5 + ((-2.0 * threshold) + 2.0 * x) 
           * pow(1.0 + a * pow(abs(x - threshold), b), c);
}

float3 AgXTonemap(float3 color)
{
    const float MIN_EV = -12.47393;
    const float MAX_EV =   4.02607;
    const float RANGE  = MAX_EV - MIN_EV;
    
    // Step 1: Apply inset matrix
    color = mul(AGX_INSET, color);
    
    // Step 2: Log2 encode with EV range clamping
    color = clamp(log2(max(color, 1e-10)), MIN_EV, MAX_EV);
    color = (color - MIN_EV) / RANGE;
    
    // Step 3: Apply sigmoid tonescale
    color = AgXSigmoid(color);
    
    // Step 4: Apply EOTF (sRGB gamma)
    color = pow(max(color, 0.0), 2.2);
    
    // Step 5: Apply outset (inverse inset)
    color = mul(AGX_INSET_INV, color);
    
    // Step 6: Display EOTF
    color = pow(max(color, 0.0), 1.0 / 2.2);
    
    return saturate(color);
}

// "Punchy" look — adds saturation back post-tonemap
float3 AgXPunchy(float3 agxResult)
{
    // ASC-CDL saturation boost with fixed luma coefficients
    const float3 lumaCoeff = float3(0.2126, 0.7152, 0.0722);
    float luma = dot(agxResult, lumaCoeff);
    float3 saturated = luma + 1.35 * (agxResult - luma); // 35% saturation boost
    return saturate(saturated);
}
```

### Why AgX Beats ACES for Film Emulation

- **Hue stability:** ACES notoriously shifts blues toward purple and reds toward orange under high intensity. AgX's inset matrix prevents out-of-gamut values, keeping hues stable.
- **Highlight rolloff:** AgX desaturates toward white in a film-like manner. ACES's RRT can produce over-saturated highlights.
- **Shadow behavior:** AgX maintains a clean toe without the "lifted" look that some ACES implementations produce.
- **Simplicity:** The entire transform is a matrix, log encode, and sigmoid. No LUT required.

### Caveats for ENB Usage

AgX expects scene-referred linear input. In ENB, `TextureColor` is already tonemapped/graded by the game. Applying AgX on display-referred data won't produce the intended behavior. Options:

1. **Use AgX in `enbeffect.fx` BEFORE game tonemapping** — replace the game's tonemap entirely
2. **Reverse the game tonemap first** — using inverse Reinhard or similar, then apply AgX
3. **Use AgX as a "look" rather than a DRT** — apply the sigmoid curve without the log encode step, using it as a contrast/rolloff shaper

For SkyrimBridge, option 1 is ideal since we already have AGIS (Automatic Game Image-Space) control and can selectively bypass the game's tonemapping.

---

## 6. Technicolor Process Emulation

### Two-Strip Technicolor (Process 2/3, 1922–1932)

Two-strip Technicolor used only red and green (cyan-green) separation — no blue channel. This creates a distinctive palette where blues appear as cyan, yellows are muted, and skin tones have a characteristic orange warmth.

```hlsl
float3 Technicolor2Strip(float3 color)
{
    // Extract red and green records (blue is discarded)
    float redRecord = color.r;
    float greenRecord = color.g;
    
    // Dye the records with complementary colors
    // Red record → dyed Cyan (complementary of red)
    // Green record → dyed Orange-Red (complementary of green)
    float3 cyanDye = float3(0.0, 1.0, 1.0) * redRecord;
    float3 redDye  = float3(1.0, 0.35, 0.0) * greenRecord;
    
    // Subtractive combination (multiply in dye space)
    // Convert to transmittance, multiply, convert back
    float3 combined = cyanDye * redDye;
    
    // Alternatively, additive blend of the dyed records:
    combined = cyanDye + redDye;
    combined = saturate(combined);
    
    // Adjust contrast and saturation to taste
    float luma = dot(combined, float3(0.2126, 0.7152, 0.0722));
    combined = lerp(luma, combined, 1.3); // Boost saturation
    
    return combined;
}
```

### Three-Strip Technicolor (Process 4, 1932–1955)

The full three-strip process is what people think of as "the Technicolor look" — vivid, saturated colors with a distinctive richness. The key technical detail is that the Technicolor camera used **very selective filters** that created purer color separations than modern monopack film.

Rob Legato's technique for "The Aviator" VFX (digitally recreating the Technicolor look) involved using chroma keys to create **matte filters** that remove cross-talk between channels before recombination. This increases color purity and saturation in a way that mimics the original camera's selective filters.

```hlsl
float3 Technicolor3Strip(float3 color, float purity, float keyStrength)
{
    // Step 1: Separate into R, G, B records
    float rRecord = color.r;
    float gRecord = color.g;
    float bRecord = color.b;
    
    // Step 2: Cross-talk removal (the Legato technique)
    // Remove other-channel contamination to increase purity
    // This simulates the selective filters in the Technicolor camera
    rRecord = max(rRecord - (gRecord + bRecord) * keyStrength * 0.5, 0.0);
    gRecord = max(gRecord - (rRecord + bRecord) * keyStrength * 0.5, 0.0);
    bRecord = max(bRecord - (rRecord + gRecord) * keyStrength * 0.5, 0.0);
    
    // Renormalize
    float totalOrig = color.r + color.g + color.b + 0.001;
    float totalNew  = rRecord + gRecord + bRecord + 0.001;
    float normFactor = totalOrig / totalNew;
    rRecord *= normFactor;
    gRecord *= normFactor;
    bRecord *= normFactor;
    
    // Step 3: Dye with complementary colors
    // Red record → Cyan dye (blocks red, passes green+blue)
    // Green record → Magenta dye (blocks green, passes red+blue)
    // Blue record → Yellow dye (blocks blue, passes red+green)
    float3 cyanLayer   = float3(1.0 - rRecord, 1.0, 1.0);
    float3 magentaLayer = float3(1.0, 1.0 - gRecord, 1.0);
    float3 yellowLayer  = float3(1.0, 1.0, 1.0 - bRecord);
    
    // Step 4: Subtractive combination (multiply dye transmittances)
    float3 result = cyanLayer * magentaLayer * yellowLayer;
    
    // Step 5: Optional Key image (adds contrast, conceals fringing)
    // Early Technicolor used a B&W "key" frame from the green record
    float key = gRecord * 0.5;
    result *= (1.0 - key * 0.15); // Subtle darkening
    
    // Step 6: Blend with original based on purity amount
    result = lerp(color, result, purity);
    
    return saturate(result);
}
```

### The "Moby Dick" Desaturation Technique

The 1956 Moby Dick used "wide-cut" (broad) separation filters instead of clean-cut filters, causing intentional cross-talk between the three B&W matrices. When recombined via dye transfer, this produced a very desaturated, painterly look. This can be emulated by increasing the `keyStrength` parameter to negative values (adding cross-talk instead of removing it) and reducing overall saturation.

---

## 7. Halation — Physics and Implementation

### Physical Basis

Halation occurs when bright light passes through the emulsion, bounces off the film base, and re-exposes the emulsion from behind. The anti-halation layer (a dye coating on the back) was designed to prevent this, but isn't 100% effective. The result is a red/orange glow around bright highlights because:

1. The anti-halation dye absorbs blue and green light more effectively
2. Red light penetrates deeper into the emulsion and base
3. The re-exposure primarily affects the red-sensitive layer

### Shader Implementation

Halation requires a **spatial blur** of the bright areas, applied primarily to the red channel:

```hlsl
// Halation is best implemented as a separate pass
// Pass 1: Extract bright areas, blur in red-weighted space
// Pass 2: Add back to image

float3 ExtractHalationSource(float3 color, float threshold)
{
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    float mask = smoothstep(threshold, threshold + 0.3, luma);
    
    // Halation color is warm — primarily red with some green
    // Mimics the spectral characteristics of light passing through
    // the anti-halation layer
    float3 halationColor = float3(1.0, 0.5, 0.15); // Warm orange-red
    
    return color * mask * halationColor;
}

// After Gaussian blur of the halation source:
float3 ApplyHalation(float3 scene, float3 halationBlurred, float amount)
{
    // Halation should be applied in LINEAR space
    // (before any gamma/display transform)
    // It's additive — light is being added back to the image
    return scene + halationBlurred * amount;
}
```

### ENB Integration Note

In ENB's pipeline, halation would ideally live in `enbeffectprepass.fx` or `enblens.fx` where you have access to render targets for multi-pass blur. The current `Effect_CinematicFX.fxh` already has a halation implementation. Key improvements from this research:

1. **Apply in linear space**, not after tonemapping
2. **Use warm color tint** (not just luminance blur)
3. **Threshold based on scene luminance**, not post-tonemap values
4. **Scale with SkyrimBridge** — torch/fire sources should produce more halation

---

## 8. Advanced Film Grain — Beyond Overlay

### Dehancer's Approach: Volumetric 3D Grain

Dehancer treats grain not as a 2D texture overlay but as **3D particles in the emulsion volume**. Each grain particle is rotated, shifted, and sometimes clustered. The key insight:

**Grain is superimposed OVER the image (not multiplied onto it), accounting for reflection, refraction, and diffusion in the emulsion layers.**

### Negative vs. Positive Grain

- **Negative grain:** More visible in highlights (because highlights = more silver halide exposure = more grain). This is what you see on raw scanned film.
- **Positive grain (print grain):** More visible in shadows (because shadows in the print = most light exposure during printing = most grain on the print stock). This is what you see in a projected film.

For a projected-film look, you want **both** — negative grain that's stronger in highlights, plus print grain that's stronger in shadows.

```hlsl
float3 ApplyFilmGrain(float3 color, float2 uv, float time,
                       float negAmount, float printAmount, float chromaAmount)
{
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    
    // Generate grain (use existing noise function)
    float3 grain = GenerateGrain(uv, time); // Returns -1..1 per channel
    
    // Chroma grain: different noise per channel creates color grain
    // Film grain IS chromatic — each dye layer has independent grain
    float3 chromaGrain = grain;
    chromaGrain.r = GenerateGrain(uv + float2(0.13, 0.27), time).r;
    chromaGrain.b = GenerateGrain(uv + float2(0.43, 0.71), time).r;
    grain = lerp(grain.rrr, chromaGrain, chromaAmount);
    
    // Negative grain distribution (stronger in highlights)
    float negMask = smoothstep(0.1, 0.8, luma);
    float3 negGrainResult = color + grain * negAmount * negMask * 0.05;
    
    // Print grain distribution (stronger in shadows)
    float printMask = smoothstep(0.8, 0.1, luma);
    float3 printGrainResult = negGrainResult + grain * printAmount * printMask * 0.03;
    
    // Grain should never clip to absolute black
    // Real film always has some base density — no true zero
    return max(printGrainResult, 0.005);
}
```

### Grain Size and Film Format

Grain size is inversely related to the film format size. 35mm grain is finer than 16mm grain for the same stock. For ENB:

- **35mm standard (1920p):** Grain at 1:1 pixel scale
- **16mm emulation:** Grain at ~2x pixel scale (or render grain at half res)
- **65mm/IMAX:** Grain at ~0.5x pixel scale (barely visible)

---

## 9. Printer Lights & CMY Color Head

### What Are Printer Lights?

Before digital color grading existed, the ONLY color control in post-production was **printer lights** — physical apertures controlling how much red, green, and blue light passed through the negative during optical printing. These are the original "color wheels."

Printer lights are specified in integer values (historically 1–50, with 25 being "normal"). Each step represents approximately a 1/12 stop change.

### HLSL: Printer Lights System

```hlsl
// Printer lights: per-channel exposure control in log space
// Mimics the physical process of controlling light intensity
// during optical printing
float3 ApplyPrinterLights(float3 logColor, float3 printerLights)
{
    // printerLights: 0.0 = default, positive = more light, negative = less
    // Each unit ≈ 1/12 stop
    float3 exposure = printerLights * (1.0 / 12.0); // Convert to stops
    return logColor + exposure; // Addition in log space = multiplication in linear
}
```

### CMY Color Head

Modern Dehancer and professional tools offer a "CMY Color Head" — the subtractive equivalent of printer lights. Instead of controlling how much R/G/B light passes through, you control how much Cyan/Magenta/Yellow filtration is applied:

```hlsl
// CMY Color Head: subtractive primary control
// Adding Cyan removes Red, adding Magenta removes Green, etc.
float3 ApplyCMYHead(float3 color, float3 cmy)
{
    // cmy: Cyan, Magenta, Yellow filtration amounts (0..1)
    // Subtractive: adding a filter removes its complement
    float3 filter = float3(
        1.0 - cmy.x,  // Cyan removes red
        1.0 - cmy.y,  // Magenta removes green
        1.0 - cmy.z   // Yellow removes blue
    );
    return color * filter;
}
```

---

## 10. Integration Architecture for Silent Horizons

### Recommended Pipeline Position

```
enbbloom.fx        → HDR bloom (already complete)
enbeffect.fx       → Tonemapping + Color Grading (already complete)
                     INSERT: Film emulation here, after tonemap, before dither
enbeffectpostpass.fx → KiSuite + LUT + final effects (already complete)
```

The film emulation system should slot into `enbeffect.fx` within the existing `ApplyFilmEmulation()` function, which currently handles gamut transform and log encoding but not the full pipeline.

### Feature Matrix for SkyrimBridge Integration

| Film Feature | SkyrimBridge Data Source | Behavior |
|---|---|---|
| Negative stock selection | Time of day, weather | Dawn/dusk → warmer stocks, storms → higher contrast |
| Print density | Ambient light level | Darker scenes → less density, brighter → more |
| Halation intensity | Light source proximity | Torches/fire → more halation |
| Grain amount | ISO-equivalent (exposure) | Dark interiors → more grain (higher "ISO") |
| Printer lights | Weather color temperature | Weather-driven color shifts via printer lights |
| Subtractive density | Combat state | Combat → enhanced density for dramatic feel |
| Cross-talk amount | Interior/exterior | Interiors → slightly more cross-talk (warmer) |

### UI Organization

```
[Film Emulation]
├── Enable Film Emulation ── bool
├── Film Pipeline
│   ├── Negative Stock ── dropdown [Kodak 500T, 250D, 200T, Fuji Eterna, Custom]
│   ├── Print Stock ── dropdown [Kodak 2383, Linear, Custom]
│   ├── Density Amount ── float 0..2 (subtractive saturation)
│   ├── Cross-talk ── float 0..1
│   └── Printer Lights R/G/B ── float -2..2
├── Halation
│   ├── Enable ── bool
│   ├── Threshold ── float 0.5..1.0
│   ├── Radius ── float 1..20
│   ├── Amount ── float 0..1
│   └── Tint R/G/B ── color
├── Grain
│   ├── Enable ── bool
│   ├── Negative Amount ── float 0..1
│   ├── Print Amount ── float 0..1
│   ├── Chroma ── float 0..1
│   └── Size ── float 0.5..3.0
└── Process Emulation
    ├── Mode ── dropdown [None, Technicolor 2-Strip, Technicolor 3-Strip]
    ├── Purity ── float 0..1
    └── Key Image ── float 0..1
```

---

## 11. Bibliography & Sources

### Academic / Industry

1. **AgX Display Rendering Transform** — Troy Sobotka, OpenColorIO config (2022)
   - Analytical HLSL port: github.com/bWFuanVzYWth/AgX (MIT License)
   - ReShade HLSL port: github.com/MrLixm/AgXc (MIT License)
   - Minimal implementation: iolite-engine.com/blog_posts/minimal_agx_implementation (MIT)

2. **"HDR Color Grading and Display in Frostbite"** — Alex Fry, GDC 2017
   - Pre-grade vs post-grade pipeline ordering

3. **ACES (Academy Color Encoding System)** — Academy of Motion Picture Arts and Sciences
   - Stephen Hill sRGB fit
   - Narkowicz approximation

4. **Kodak Vision3 Technical Data** — Kodak Professional
   - 5219 (500T), 5207 (250D), 5213 (200T) sensitometric curves

5. **"THE TECHNICOLOR PROCESS OF THREE-COLOR CINEMATOGRAPHY"** — Original Technicolor documentation
   - widescreenmuseum.com/oldcolor/ball.htm

### Professional Colorist Tools & Techniques

6. **CinePrint35** — Tom Bolles (cinematographer)
   - Film emulation PowerGrades from lab-scanned film stocks
   - Halation in linear space, subtractive color, per-stock tone/color mapping

7. **Dehancer Pro** — Dehancer
   - Camera transform → negative → print → finishing pipeline
   - Volumetric 3D grain model with negative/positive modes
   - 60+ film profiles, halation, bloom, gate weave

8. **PixelTools Film Lab** — Professional PowerGrade collection
   - DaVinci Wide Gamut workflow, subtractive density, modular node structure

9. **Mononodes** — Rebuilding Kodak 2383 LUT from first principles
   - mononodes.com/photochemical-film-look/
   - RGB curve analysis of Kodak 2383 D55 LUT

10. **Movie Density DCTL** — Iridescent Color
    - Per-hue density control using Chen spherical model
    - Subtractive saturation with hue shift

### ENB Community (from prior research)

11. **TreyM** — LOG tonemapping, CFL architecture, LUT pipeline
12. **l00ping** — ReShade-to-ENB porting, AGCC system
13. **KingEric1992** — 3D LUT sampling, histogram adaptation
14. **Prod80** — Color balance, split-tone, Technicolor emulation for ReShade
15. **CeeJay.dk** — Vibrance, LumaSharp, curves effects

### Key Concepts Glossary

- **DRT (Display Rendering Transform):** Converts scene-referred HDR to display-referred SDR
- **IDT (Input Device Transform):** Converts camera-specific data to working space
- **Sensitometric curve:** A film stock's response to exposure (density vs. log exposure)
- **Cineon:** Standard log encoding for scanned film (10-bit, 0.002 density per code value)
- **Printer lights:** R/G/B exposure controls for optical printing (original color grading)
- **Imbibition:** Physical dye transfer process used in Technicolor printing
- **Halation:** Re-exposure of emulsion from light bouncing off film base
- **Density:** Optical density of dye in film — higher density = darker, more saturated
