# Advanced CRT, VHS, Phosphor, Incandescence & Bloom Diffusion Research

Deep research for ENB HLSL (DX11 SM5.0, pixel shaders only). All techniques target the existing
SkyrimBridge ENB shader pipeline (enbeffectpostpass.fx CRT addon, enbbloom.fx, enbeffect.fx).

---

## 1. ADVANCED CRT EMULATION

### 1.1 Mask Types — Subpixel Geometry

Reference implementations: CRT-Royale (libretro), CRT-Geom (cgwg), Guest-Advanced, Cathode Retro.

**Aperture Grille (Trinitron)**
Vertical phosphor stripes with no horizontal structure. Easiest to render at lower resolutions
(1080p viable). Thin wires held under tension; no dot pitch, only stripe pitch.
```hlsl
// Aperture grille: RGB vertical stripes with smooth transitions
float3 ApertureGrille(float2 pixelPos, float maskScale, float maskDark) {
    float col = frac(pixelPos.x / (3.0 * maskScale));
    float3 mask;
    // Smooth subpixel transitions via smoothstep instead of hard if/else
    mask.r = smoothstep(0.333 - 0.15, 0.333 + 0.15, col) *
             (1.0 - smoothstep(0.667 - 0.15, 0.667 + 0.15, col));
    // ^ peak at 0.167 (center of R stripe)
    // Shift pattern for G and B similarly...
    // For perf, a simpler version:
    float phase = col * 3.0;
    mask.r = saturate(1.0 - abs(phase - 0.5) * 2.0);
    mask.g = saturate(1.0 - abs(phase - 1.5) * 2.0);
    mask.b = saturate(1.0 - abs(phase - 2.5) * 2.0);
    return lerp(maskDark, 1.0, mask);
}
```

**Slot Mask (Cromaclear / consumer TVs)**
Rectangular phosphor slots arranged in a staggered grid. Each row offsets horizontally.
```hlsl
float3 SlotMask(float2 pixelPos, float maskScale, float maskDark) {
    float rowOff = floor(fmod(pixelPos.y / maskScale, 2.0)) * 1.5;
    float phase = frac((pixelPos.x / maskScale + rowOff) / 3.0) * 3.0;
    float3 mask;
    mask.r = saturate(1.0 - abs(phase - 0.5) * 2.0);
    mask.g = saturate(1.0 - abs(phase - 1.5) * 2.0);
    mask.b = saturate(1.0 - abs(phase - 2.5) * 2.0);
    // Slot vertical gap: darken between slots
    float slotV = frac(pixelPos.y / (maskScale * 2.0));
    float slotGap = smoothstep(0.4, 0.5, abs(slotV - 0.5));
    mask = lerp(mask, mask * (1.0 - maskDark * 0.5), slotGap);
    return lerp(maskDark, 1.0, mask);
}
```

**Shadow Mask (triad / delta arrangement)**
Triangular dot triads on a hexagonal grid. Most complex geometry — needs 1440p+ to resolve properly.
```hlsl
float3 ShadowMask(float2 pixelPos, float maskScale, float maskDark) {
    float2 hex = pixelPos / maskScale * float2(1.0, 0.866); // sqrt(3)/2 row spacing
    float rowOff = floor(fmod(hex.y, 2.0)) * 0.5;
    float phase = frac((hex.x + rowOff) / 3.0) * 3.0;
    // Circular phosphor dots instead of rectangular
    float2 dotCenter = floor(hex) + 0.5;
    float dist = length(frac(hex) - 0.5);
    float dotMask = smoothstep(0.45, 0.3, dist); // circular falloff
    float3 mask;
    mask.r = (phase < 1.0) ? dotMask : maskDark;
    mask.g = (phase >= 1.0 && phase < 2.0) ? dotMask : maskDark;
    mask.b = (phase >= 2.0) ? dotMask : maskDark;
    return mask;
}
```

**Luminance-adaptive mask fade**: In CRT-Royale, masks fade out as brightness increases so they
don't reduce peak brightness. Critical for HDR-like results:
```hlsl
float3 maskFinal = lerp(1.0, maskRGB, maskIntensity * (1.0 - luma * maskBrightFade));
```

### 1.2 Phosphor Persistence / Temporal Decay

Real P22 phosphors have per-channel decay rates: Red ~1ms, Green ~100us, Blue ~200ns.
This creates color-dependent ghosting where red lingers longest.

```hlsl
// Per-channel exponential decay with different time constants
// Requires: previous frame buffer (TexturePrev), current time delta
float3 PhosphorPersistence(float3 current, float3 prev, float3 decayRate, float dt) {
    // decayRate = float3(0.92, 0.85, 0.70) for P22-like (R slow, B fast)
    float3 decay = exp(-dt * (1.0 / max(decayRate, 0.001)));
    return max(current, prev * decay);
}
```
**Implementation note for ENB**: ENB has no built-in previous-frame texture. You would need to
write the CRT output to a persistent RenderTarget and read it back next frame. This is feasible
using the technique chain (one technique reads the previous RT, composites, writes back).
**Performance**: Light (one extra texture read + lerp per pixel).
**Pipeline stage**: enbeffectpostpass.fx (CRT addon), requires a dedicated RT for persistence.

### 1.3 Beam Profile — Gaussian vs Generalized Gaussian

CRT-Royale uses a "generalized Gaussian" where the shape parameter varies with luminance:
bright pixels produce wider, more plateau-like beams; dark pixels produce narrow, peaked beams.

```hlsl
// Generalized Gaussian scanline beam
// shape: 2.0 = pure Gaussian, higher = more plateau
// sigma: standard deviation (beam width)
float ScanlineBeam(float dist, float sigma, float shape) {
    float x = abs(dist) / max(sigma, 0.001);
    // Generalized Gaussian: exp(-0.5 * |x|^shape)
    return exp(-0.5 * pow(x, shape));
}

// Luminance-modulated beam width
float LuminanceModulatedScanline(float scanDist, float luma,
    float minSigma, float maxSigma, float minShape, float maxShape) {
    float sigma = lerp(minSigma, maxSigma, luma); // bright = wider
    float shape = lerp(maxShape, minShape, luma);   // bright = more plateau
    return ScanlineBeam(scanDist, sigma, shape);
}
```
**Parameters**: minSigma [0.02-0.08], maxSigma [0.15-0.40], minShape [2.0], maxShape [4.0-32.0]
**Performance**: Light (trig/exp per pixel).
**Pipeline stage**: enbeffectpostpass.fx CRT addon.

### 1.4 Halation — CRT-Specific Light Scatter

Halation is NOT standard bloom. It is light from phosphors bouncing off the inner glass surface
and exciting nearby phosphors randomly. It is a broad, warm glow underneath the image.

CRT-Royale separates halation from diffusion:
- **Halation**: electrons bouncing under the glass, lighting random phosphors
- **Diffusion**: light passing through imperfect glass face, spreading outward

```hlsl
// CRT Halation approximation using pre-blurred mip
// Best done as a separate pass writing to a halation RT
float3 CRTHalation(float2 uv, float halationWeight, float diffusionWeight) {
    float3 scene = TextureColor.Sample(smpLinear, uv).rgb;
    // Use progressively blurred versions for the two effects
    float3 halation = TextureDownsampled.SampleLevel(smpLinear, uv, 3.0).rgb; // very blurry
    float3 diffusion = TextureDownsampled.SampleLevel(smpLinear, uv, 1.5).rgb; // moderate blur

    // Halation is warm-shifted (phosphor re-excitation has spectral bias)
    halation *= float3(1.2, 1.0, 0.8);

    return scene + halation * halationWeight + diffusion * diffusionWeight;
}
```
**In ENB**: Use TextureBloom (pre-blurred by enbbloom.fx) as the halation source. Sample at
different mip levels for halation vs diffusion. This avoids needing extra blur passes.
**Performance**: Light (2 texture fetches + blend).
**Pipeline stage**: enbeffectpostpass.fx, AFTER scanline/mask application.

### 1.5 Interlace Simulation

Real interlaced CRTs display odd fields then even fields at 60Hz (30fps effective). Three
approaches can be simulated:

```hlsl
// Field alternation: blank alternate lines each frame
float InterlaceField(float screenY, float frameCount, float interlaceIntensity) {
    // Determine current field (odd/even based on frame)
    float field = fmod(floor(frameCount), 2.0);
    float line = fmod(floor(screenY), 2.0);
    float isActive = (line == field) ? 1.0 : (1.0 - interlaceIntensity);
    return isActive;
}

// Bob deinterlace: shift image by half-line every other frame
float2 BobDeinterlace(float2 uv, float frameCount, float lineHeight) {
    float field = fmod(floor(frameCount), 2.0);
    uv.y += field * lineHeight * 0.5; // half-scanline shift
    return uv;
}

// Combing artifacts (motion areas show interleaved fields)
float3 CombingArtifact(float3 current, float3 motionVec, float2 uv, float combStrength) {
    float motion = length(motionVec.xy);
    float combMask = saturate(motion * 20.0); // only where motion exists
    float linePhase = frac(uv.y * ScreenSize.x * ScreenSize.w * 0.5);
    float combPattern = step(0.5, linePhase);
    // Interleave current frame with shifted version
    float2 shiftedUV = uv + float2(0, combPattern * 2.0 / ScreenSize.x / ScreenSize.w);
    float3 shifted = TextureColor.Sample(smpLinear, shiftedUV).rgb;
    return lerp(current, lerp(current, shifted, combPattern), combMask * combStrength);
}
```
**Performance**: Light (field alt) to Medium (combing needs motion estimation).
**Pipeline stage**: enbeffectpostpass.fx. Frame counter from Timer.x or SB_Render_Frame.x.

### 1.6 Curvature — Advanced Model

Beyond basic barrel distortion, add:
- Corner darkening (beam intensity drops at screen edges)
- Glass reflection simulation (fake specular from room lights)
- Tilt (asymmetric distortion)

```hlsl
float2 CRTCurvature(float2 uv, float curvatureX, float curvatureY, float2 tilt) {
    float2 c = uv * 2.0 - 1.0;
    // Apply tilt (asymmetric curvature)
    c *= 1.0 + tilt * c.yx;
    float r2 = dot(c, c);
    // Separate X/Y curvature for cylindrical (Trinitron) vs spherical
    c.x *= 1.0 + r2 * curvatureX;
    c.y *= 1.0 + r2 * curvatureY;
    return c * 0.5 + 0.5;
}

// Corner beam intensity falloff
float CornerBeamFalloff(float2 uv, float cornerDark, float cornerSmooth) {
    float2 edge = smoothstep(0.0, cornerSmooth, uv) *
                  smoothstep(0.0, cornerSmooth, 1.0 - uv);
    return pow(edge.x * edge.y, cornerDark);
}

// Glass reflection (fake environment reflection on curved glass)
float3 GlassReflection(float2 uv, float curvature, float reflectIntensity) {
    float2 c = uv * 2.0 - 1.0;
    // Curved glass normal approximation
    float3 normal = normalize(float3(-c * curvature * 2.0, 1.0));
    // Fake room light at top-left
    float3 lightDir = normalize(float3(-0.3, 0.5, 1.0));
    float spec = pow(saturate(dot(normal, lightDir)), 32.0);
    return float3(0.8, 0.85, 0.9) * spec * reflectIntensity;
}
```

### 1.7 CRT Color Profile — Phosphor Chromaticity

NTSC 1953 vs SMPTE-C vs PAL phosphor primaries and gamma curves. Real CRTs had
specific phosphor chromaticities that differ from sRGB.

```hlsl
// Phosphor chromaticity matrices (CIE xy -> RGB transform)
// NTSC 1953 primaries: R(0.67,0.33) G(0.21,0.71) B(0.14,0.08) D65
static const float3x3 NTSC_1953_TO_SRGB = float3x3(
    1.5073, -0.3725, -0.0832,
   -0.0275,  0.9350,  0.0670,
   -0.0272, -0.0401,  1.1677
);

// SMPTE-C (post-1987 US TV): R(0.63,0.34) G(0.31,0.595) B(0.155,0.07)
static const float3x3 SMPTE_C_TO_SRGB = float3x3(
    1.1058, -0.0727, -0.0201,
   -0.0185,  1.0286,  0.0053,
   -0.0060, -0.0259,  1.0567
);

// CRT gamma: shifted power law (BT.709)
// CRT display gamma typically 2.2-2.5
float3 CRTGamma(float3 color, float gamma) {
    return pow(max(color, 0.0), gamma);
}

// White point temperature: D65=6500K, D93=9300K (Japan)
float3 WhitePointShift(float3 color, float tempK) {
    // Planckian locus approximation for white balance
    float x = (tempK <= 7000.0) ?
        0.244063 + 0.09911e3/tempK + 2.9678e6/(tempK*tempK) - 4.6070e9/(tempK*tempK*tempK) :
        0.237040 + 0.24748e3/tempK + 1.9018e6/(tempK*tempK) - 2.0064e9/(tempK*tempK*tempK);
    float y = -3.0 * x * x + 2.87 * x - 0.275;
    // Convert daylight chromaticity shift to RGB multiplier (simplified)
    float3 wp = float3(x/y, 1.0, (1.0-x-y)/y);
    float3 d65 = float3(0.9505, 1.0, 1.0890); // D65 reference
    return color * (d65 / wp);
}
```

### 1.8 NTSC Composite Signal Processing

Full composite artifact simulation requires multi-pass (encode -> filter -> decode).
Simplified single-pass version for ENB:

```hlsl
// RGB to YIQ
float3 RGBtoYIQ(float3 rgb) {
    return float3(
        dot(rgb, float3(0.2989, 0.5870, 0.1140)),
        dot(rgb, float3(0.5959, -0.2744, -0.3216)),
        dot(rgb, float3(0.2115, -0.5229, 0.3114))
    );
}

// YIQ to RGB
float3 YIQtoRGB(float3 yiq) {
    return float3(
        dot(yiq, float3(1.0, 0.9563, 0.6210)),
        dot(yiq, float3(1.0, -0.2721, -0.6474)),
        dot(yiq, float3(1.0, -1.1070, 1.7046))
    );
}

// Single-pass NTSC composite artifact simulation
float3 NTSCComposite(float2 uv, float artifactStrength, float chromaBandwidth) {
    float3 yiq = RGBtoYIQ(TextureColor.Sample(smpPoint, uv).rgb);

    // Simulate composite encoding: modulate chroma onto carrier
    float phase = uv.x * ScreenSize.x * 0.5 * 3.14159265; // carrier frequency
    float scanlinePhase = floor(uv.y * ScreenSize.x * ScreenSize.w) * 2.094; // 120deg shift/line

    // Composite signal = Y + I*cos(wt) + Q*sin(wt)
    float composite = yiq.x + yiq.y * cos(phase + scanlinePhase)
                            + yiq.z * sin(phase + scanlinePhase);

    // Demodulate: multiply by carrier and low-pass filter (multi-tap average)
    float demodI = 0.0, demodQ = 0.0, demodY = 0.0;
    int taps = (int)(chromaBandwidth * 4.0 + 2.0); // 2-6 taps
    float invTaps = 1.0 / (float)(taps * 2 + 1);

    [unroll(13)]
    for(int i = -6; i <= 6; i++) {
        if(abs(i) > taps) continue;
        float2 sampleUV = uv + float2(i * PixelSize.x, 0);
        float3 s = RGBtoYIQ(TextureColor.Sample(smpPoint, sampleUV).rgb);
        float sp = (uv.x + i * PixelSize.x) * ScreenSize.x * 0.5 * 3.14159265 + scanlinePhase;
        float sig = s.x + s.y * cos(sp) + s.z * sin(sp);
        demodI += sig * cos(sp);
        demodQ += sig * sin(sp);
        demodY += sig;
    }
    demodI *= invTaps * 2.0;
    demodQ *= invTaps * 2.0;
    demodY *= invTaps;

    float3 decoded = YIQtoRGB(float3(demodY, demodI, demodQ));
    return lerp(TextureColor.Sample(smpLinear, uv).rgb, decoded, artifactStrength);
}
```

**Signal quality levels**:
- **RF (worst)**: Luma ~3 MHz, chroma ~0.5 MHz — heavy artifacts, dot crawl, rainbow fringing
- **Composite**: Luma ~4.2 MHz, chroma ~1.3 MHz — visible color bleed, some dot crawl
- **S-Video**: Separated Y/C — no dot crawl, still bandwidth-limited chroma (~1.3 MHz)
- **RGB/Component**: Full bandwidth each channel — clean, no crosstalk artifacts

Simulate by adjusting `chromaBandwidth` (0.2 = RF, 0.5 = Composite, 1.0 = S-Video, skip = RGB).

**Performance**: Medium-Heavy (multi-tap demodulation per pixel).
**Pipeline stage**: enbeffectpostpass.fx, as an early pass BEFORE scanlines/mask.

### 1.9 Convergence Errors (RGB Misregistration)

Cheap CRT monitors had imperfect beam alignment, especially at corners:
```hlsl
float3 ConvergenceError(float2 uv, float2 offsetR, float2 offsetB) {
    // Distance from center increases misregistration
    float2 center = uv - 0.5;
    float edgeFactor = dot(center, center) * 4.0; // stronger at edges
    float r = TextureColor.Sample(smpLinear, uv + offsetR * edgeFactor).r;
    float g = TextureColor.Sample(smpLinear, uv).g;
    float b = TextureColor.Sample(smpLinear, uv + offsetB * edgeFactor).b;
    return float3(r, g, b);
}
```
**Parameters**: offsetR/offsetB in pixel units (e.g., float2(0.5, 0.3) / float2(-0.3, -0.2) pixels).
**Performance**: Light (2 extra texture fetches).

---

## 2. VHS / ANALOG VIDEO DEGRADATION

### 2.1 YIQ/YUV Chroma Subsampling

VHS records chroma at vastly lower resolution than luma. NTSC VHS: luma ~3 MHz,
chroma ~0.5 MHz (effectively 4:1:0 subsampling in the horizontal axis).

```hlsl
float3 VHSChromaSubsample(float2 uv, float chromaBlurRadius) {
    float3 yiq = RGBtoYIQ(TextureColor.Sample(smpPoint, uv).rgb);

    // Low-pass filter chroma (I and Q) with wide horizontal blur
    float2 chromaAccum = 0.0;
    float totalWeight = 0.0;
    int samples = 8;

    [unroll]
    for(int i = -samples; i <= samples; i++) {
        float w = exp(-0.5 * (i * i) / (chromaBlurRadius * chromaBlurRadius));
        float2 sUV = uv + float2(i * PixelSize.x * 2.0, 0);
        float3 s = RGBtoYIQ(TextureColor.Sample(smpPoint, sUV).rgb);
        chromaAccum += s.yz * w;
        totalWeight += w;
    }

    yiq.yz = chromaAccum / totalWeight;
    return YIQtoRGB(yiq);
}
```
**Performance**: Medium (16-tap horizontal blur for chroma).

### 2.2 Head Switching Noise

At the bottom of each field, the video head switches between heads. This produces a
horizontal displacement band with noise in the bottom ~10-20 scanlines.

```hlsl
float3 HeadSwitchingNoise(float3 color, float2 uv, float time, float intensity) {
    float switchZone = smoothstep(0.92, 0.98, uv.y); // bottom 8% of screen

    // Horizontal displacement jitter
    float jitter = (frac(sin(dot(float2(floor(uv.y * 480.0), time * 100.0),
                   float2(12.9898, 78.233))) * 43758.5453) - 0.5) * 2.0;
    float2 displaced = uv + float2(jitter * 0.05 * switchZone * intensity, 0);

    // Noise injection
    float noise = frac(sin(dot(uv * 500.0 + time, float2(17.3, 41.7))) * 43758.5453);

    float3 result = TextureColor.Sample(smpLinear, displaced).rgb;
    result = lerp(result, float3(noise, noise, noise), switchZone * intensity * 0.5);
    return lerp(color, result, switchZone);
}
```

### 2.3 Tracking Errors

Horizontal line displacement, static bands, rolling — caused by misaligned playback head timing.

```hlsl
float2 TrackingError(float2 uv, float time, float severity) {
    // Random horizontal displacement bands
    float band = step(0.98, frac(sin(floor(uv.y * 50.0 + time * 3.0) * 17.13) * 43758.5));
    float displacement = sin(time * 7.0 + uv.y * 100.0) * severity * band;

    // Slow rolling (entire image shifts vertically over time)
    float roll = frac(time * 0.02 * severity);

    return float2(uv.x + displacement, frac(uv.y + roll));
}
```

### 2.4 Color Bleeding / Chroma Smear

Low chroma bandwidth causes color to smear rightward (due to tape head scan direction):
```hlsl
float3 ChromaSmear(float2 uv, float smearAmount) {
    float3 yiq = RGBtoYIQ(TextureColor.Sample(smpPoint, uv).rgb);

    // One-sided (rightward) weighted average for chroma
    float2 chromaSmeared = 0.0;
    float totalW = 0.0;
    [unroll]
    for(int i = 0; i < 12; i++) {
        float t = (float)i / 12.0;
        float w = exp(-t * 3.0); // exponential decay rightward
        float2 sUV = uv + float2(t * smearAmount * PixelSize.x * 8.0, 0);
        float3 s = RGBtoYIQ(TextureColor.Sample(smpPoint, sUV).rgb);
        chromaSmeared += s.yz * w;
        totalW += w;
    }
    yiq.yz = chromaSmeared / totalW;
    return YIQtoRGB(yiq);
}
```

### 2.5 Tape Noise — Dropout Lines and Snow

```hlsl
float3 TapeNoise(float3 color, float2 uv, float time, float dropoutRate, float snowAmount) {
    // Dropout lines: random horizontal white/black streaks
    float lineHash = frac(sin(floor(uv.y * 480.0) * 43.17 + floor(time * 30.0) * 17.31) * 43758.5);
    float isDropout = step(1.0 - dropoutRate, lineHash);
    float dropoutBright = step(0.5, frac(lineHash * 7.13)); // white or black dropout
    float dropoutWidth = frac(lineHash * 3.71) * 0.3 + 0.05;
    float dropoutX = frac(lineHash * 13.37);
    float inDropout = isDropout * step(dropoutX, uv.x) * step(uv.x, dropoutX + dropoutWidth);

    color = lerp(color, float3(dropoutBright, dropoutBright, dropoutBright), inDropout * 0.8);

    // Snow/static overlay
    float snow = frac(sin(dot(uv * ScreenSize.xy + time * 1000.0,
                  float2(12.9898, 78.233))) * 43758.5453);
    color = lerp(color, float3(snow, snow, snow), snowAmount * 0.15);

    return color;
}
```

### 2.6 Generation Loss

Each VHS copy reduces bandwidth. Simulate by progressive low-pass + noise accumulation:
```hlsl
float3 GenerationLoss(float2 uv, int generation) {
    // Each generation: wider low-pass + more noise + chroma shift
    float blurRadius = (float)generation * 1.5;
    float noiseAmount = (float)generation * 0.02;
    float chromaShift = (float)generation * 0.003;

    // Apply chroma subsampling with generation-dependent bandwidth
    float3 color = VHSChromaSubsample(uv, 2.0 + blurRadius);

    // Luma blur (bandwidth reduction)
    float3 yiq = RGBtoYIQ(color);
    float lumaBlur = 0.0;
    float tw = 0.0;
    [unroll]
    for(int i = -4; i <= 4; i++) {
        float w = exp(-0.5 * i * i / max(blurRadius * 0.5, 0.5));
        lumaBlur += RGBtoYIQ(TextureColor.Sample(smpLinear,
                   uv + float2(i * PixelSize.x, 0)).rgb).x * w;
        tw += w;
    }
    yiq.x = lumaBlur / tw;

    // Noise accumulation
    float noise = (frac(sin(dot(uv * 1000.0, float2(12.98, 78.23))) * 43758.5) - 0.5);
    yiq.x += noise * noiseAmount;
    yiq.y += noise * noiseAmount * 0.5 + chromaShift;

    return YIQtoRGB(yiq);
}
```

### 2.7 Time Base Errors / Flagging

Jittery horizontal timing, flagging at top of frame:
```hlsl
float2 TimeBaseError(float2 uv, float time, float jitterAmount, float flagAmount) {
    // Per-line horizontal jitter (time base instability)
    float lineIndex = floor(uv.y * 480.0);
    float lineJitter = (frac(sin(lineIndex * 12.17 + time * 60.0) * 43758.5) - 0.5);
    lineJitter *= jitterAmount * PixelSize.x * 3.0;

    // Flagging at top of frame (sync instability)
    float flagZone = smoothstep(0.08, 0.0, uv.y);
    float flag = sin(uv.y * 200.0 + time * 15.0) * flagAmount * flagZone;

    return float2(uv.x + lineJitter + flag, uv.y);
}
```

### 2.8 SP vs LP Mode

SP (Standard Play): Full bandwidth, ~3MHz luma, ~0.5MHz chroma.
LP (Long Play): Half tape speed, ~2MHz luma, ~0.3MHz chroma, more noise, more dropouts.
EP/SLP: 1/3 speed, ~1.5MHz luma, heavy degradation.

Simulate by scaling the parameters of all other VHS effects:
```hlsl
// SP=1.0, LP=0.67, EP=0.5
float modeScale = (mode == 0) ? 1.0 : (mode == 1) ? 0.67 : 0.5;
float lumaResolution = 3.0 * modeScale;  // MHz equivalent
float chromaResolution = 0.5 * modeScale;
float noiseFloor = 0.01 / modeScale;
float dropoutRate = 0.002 / modeScale;
```

### 2.9 Macrovision AGC Distortion

Brightness pumping caused by false sync pulses in VBI:
```hlsl
float MacrovisionPumping(float2 uv, float time, float intensity) {
    // Slow brightness oscillation (AGC hunting)
    float pump = sin(time * 2.5) * 0.15 + sin(time * 0.7) * 0.08;
    // More pronounced at top/bottom of frame
    float verticalBias = 1.0 + abs(uv.y - 0.5) * 0.5;
    return 1.0 + pump * intensity * verticalBias;
}
// Apply: color *= MacrovisionPumping(uv, time, intensity);
```

### Complete VHS Pipeline Order
1. Time base errors / flagging (UV distortion)
2. Tracking errors (UV distortion)
3. YIQ encoding + chroma subsampling
4. Chroma smear (rightward bias)
5. Head switching noise (bottom of frame)
6. Tape noise / dropouts
7. Macrovision brightness pumping
8. Generation loss (bandwidth + noise accumulation)

**Performance**: Medium-Heavy (full pipeline). Light if cherry-picking effects.
**Pipeline stage**: enbeffectpostpass.fx, as a separate technique pass.

---

## 3. PHOSPHOR PHYSICS

### 3.1 Phosphor Types and Spectral Characteristics

| Type | Color | Application | Persistence | Decay Time |
|------|-------|-------------|-------------|------------|
| P22  | RGB triad | Color TV | Short | R:~1ms, G:~100us, B:~200ns |
| P4   | White | B&W TV | Medium-short | ~60us |
| P31  | Green | Computer monitors | Medium | ~32us |
| P43  | Yellow-green | Oscilloscopes | Medium | ~1ms |

P22 is actually a family of ~7 phosphor formulations (sulfide/silicate/phosphate, all-sulfide,
sulfide/vanadate, sulfide/oxysulfide, sulfide/oxide variants).

### 3.2 Decay Curves — Bi-Exponential Model

Real phosphor decay is NOT single-exponential. It follows a bi-exponential curve with a fast
fluorescence component and a slow phosphorescence tail:

```hlsl
// Bi-exponential phosphor decay
// fast = fluorescence (main emission), slow = phosphorescence (afterglow)
float PhosphorDecay(float t, float fastRate, float slowRate, float slowRatio) {
    // fastRate: primary decay constant (e.g., 0.0001s for P22 blue)
    // slowRate: phosphorescence tail (e.g., 0.01s)
    // slowRatio: fraction of energy in slow component (e.g., 0.05)
    return (1.0 - slowRatio) * exp(-t / fastRate) + slowRatio * exp(-t / slowRate);
}

// Per-channel P22 decay (different rates per color)
float3 P22Decay(float t) {
    return float3(
        PhosphorDecay(t, 0.001,  0.020, 0.08),  // Red: slowest
        PhosphorDecay(t, 0.0001, 0.005, 0.03),  // Green: medium
        PhosphorDecay(t, 0.00002, 0.001, 0.01)  // Blue: fastest
    );
}
```

### 3.3 Luminance-Dependent Bloom

Higher beam current (brighter pixels) excites a wider area of phosphor, causing nonlinear
bloom spread that scales with intensity:

```hlsl
float3 PhosphorBloom(float2 uv, float bloomScale, float nonlinearity) {
    float3 center = TextureColor.Sample(smpLinear, uv).rgb;
    float luma = dot(center, float3(0.299, 0.587, 0.114));

    // Bloom radius scales nonlinearly with luminance
    float radius = bloomScale * pow(luma, nonlinearity); // nonlinearity > 1.0

    // 8-tap bloom with luminance-dependent radius
    float3 bloom = 0.0;
    [unroll]
    for(int i = 0; i < 8; i++) {
        float angle = i * 0.785398; // PI/4 increments
        float2 offset = float2(cos(angle), sin(angle)) * radius;
        bloom += TextureColor.Sample(smpLinear, uv + offset * PixelSize).rgb;
    }
    bloom /= 8.0;

    return lerp(center, max(center, bloom), saturate(luma * 2.0));
}
```

### 3.4 Burn-In Simulation

Static bright elements slowly damage phosphor coating. Requires temporal accumulation buffer:

```hlsl
// Accumulate burn-in over time (needs persistent RT)
float3 BurnInAccumulate(float3 currentFrame, float3 burnInBuffer, float burnRate) {
    float luma = dot(currentFrame, float3(0.299, 0.587, 0.114));
    // Only bright, static content burns in
    float burnContribution = saturate(luma - 0.7) * burnRate; // threshold at 0.7
    // Accumulate slowly
    float3 newBurnIn = max(burnInBuffer, burnInBuffer + currentFrame * burnContribution);
    return min(newBurnIn, 1.0); // clamp to prevent overflow
}

// Apply burn-in (subtract from current image — burned phosphors are dimmer)
float3 ApplyBurnIn(float3 color, float3 burnInMap, float burnVisibility) {
    // Burn-in reduces phosphor efficiency
    float3 burnDim = 1.0 - burnInMap * burnVisibility;
    // But burned areas also show their color when screen is dark
    float3 burnGhost = burnInMap * burnVisibility * 0.1; // faint ghost
    return color * burnDim + burnGhost;
}
```

### 3.5 Electron Beam Physics

- **Spot size vs beam current**: Higher current = wider beam spot (beam blooming)
- **Defocus at edges**: Longer electron path to screen edges = wider spot = softer image
- **Convergence errors**: RGB guns have slightly different deflection angles at screen edges

```hlsl
// Edge defocus: beam spot widens toward screen edges
float EdgeDefocus(float2 uv, float defocusStrength) {
    float2 center = uv - 0.5;
    float distFromCenter = length(center);
    // Quadratic increase in blur at edges
    return 1.0 + distFromCenter * distFromCenter * defocusStrength * 4.0;
}

// Apply as blur radius multiplier:
// float blur = baseBlur * EdgeDefocus(uv, 0.5);
```

**Performance**: Light-Medium per feature, but accumulating all physics = Medium-Heavy.
**Pipeline stage**: enbeffectpostpass.fx CRT addon; burn-in requires persistent RT across frames.

---

## 4. INCANDESCENCE / THERMAL GLOW

### 4.1 Blackbody Radiation — Temperature to Color

Planck's law maps temperature (Kelvin) to spectral radiance. For real-time use, polynomial
approximations are standard.

```hlsl
// Fast blackbody color from temperature (valid 1000K - 15000K)
// Based on CIE 1964 color matching via Planckian locus approximation
float3 BlackbodyColor(float tempK) {
    float3 color;
    float t = tempK / 1000.0;
    float t2 = t * t;

    // Red channel
    if(tempK < 6600.0)
        color.r = 1.0;
    else
        color.r = saturate(1.292936 * pow(t - 6.0, -0.1332047));

    // Green channel
    if(tempK < 6600.0)
        color.g = saturate(0.39008 * log(t) - 0.63184);
    else
        color.g = saturate(1.129891 * pow(t - 6.0, -0.0755148));

    // Blue channel
    if(tempK < 2000.0)
        color.b = 0.0;
    else if(tempK < 6600.0)
        color.b = saturate(0.54321 * log(t - 2.0) - 1.19625);
    else
        color.b = 1.0;

    return color;
}

// More accurate: Tanner Helland's fit (widely used in shaders)
float3 BlackbodyTannerHelland(float tempK) {
    float t = tempK / 100.0;
    float3 c;
    c.r = (t <= 66.0) ? 1.0 : saturate(1.2929 * pow(t - 60.0, -0.1332));
    c.g = (t <= 66.0) ? saturate(0.3901 * log(t) - 0.6318) :
                         saturate(1.1299 * pow(t - 60.0, -0.0755));
    c.b = (t >= 66.0) ? 1.0 :
          (t <= 19.0) ? 0.0 : saturate(0.5432 * log(t - 10.0) - 1.1963);
    return c;
}
```

### 4.2 Thermal Bloom — Temperature-Dependent Glow

Hot objects emit glow proportional to T^4 (Stefan-Boltzmann law). In screen space, detect hot
pixels by color temperature estimation and apply proportional bloom.

```hlsl
// Estimate "temperature" from pixel color (inverse blackbody)
float EstimateTemperature(float3 color) {
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    if(luma < 0.01) return 300.0; // ambient

    // Red-to-blue ratio as temperature proxy
    float rb = color.r / max(color.b, 0.001);

    // Very rough inverse: hot objects are red->yellow->white->blue
    // High R/B ratio = low temp (reddish), low ratio = high temp (bluish)
    float temp;
    if(rb > 3.0) temp = lerp(800.0, 2000.0, saturate((rb - 3.0) / 7.0));
    else if(rb > 1.0) temp = lerp(2000.0, 5000.0, saturate((3.0 - rb) / 2.0));
    else temp = lerp(5000.0, 10000.0, saturate((1.0 - rb) / 0.8));

    return temp * saturate(luma * 5.0); // dim pixels aren't hot
}

// Thermal bloom: hot pixels get wider glow
float3 ThermalBloom(float2 uv, float bloomScale) {
    float3 center = TextureColor.Sample(smpLinear, uv).rgb;
    float temp = EstimateTemperature(center);

    // Stefan-Boltzmann: radiance proportional to T^4
    float glowIntensity = pow(temp / 6500.0, 4.0) * 0.01;
    float glowRadius = bloomScale * sqrt(glowIntensity);

    // Radial bloom weighted by blackbody color
    float3 bloom = 0.0;
    [unroll]
    for(int i = 0; i < 8; i++) {
        float angle = i * 0.785398;
        float2 offset = float2(cos(angle), sin(angle)) * glowRadius;
        bloom += TextureColor.Sample(smpLinear, uv + offset * PixelSize).rgb;
    }
    bloom /= 8.0;

    // Tint bloom by blackbody color of estimated temperature
    float3 glowColor = BlackbodyColor(temp);
    bloom *= glowColor;

    return center + bloom * glowIntensity;
}
```

### 4.3 Incandescent Filament Rendering

Tungsten filament emission spectrum and color rendering index:
```hlsl
// Tungsten filament at operating temperature (~2700K typically)
// The spectrum is NOT a perfect blackbody due to tungsten emissivity
float3 TungstenEmission(float tempK) {
    float3 bb = BlackbodyColor(tempK);
    // Tungsten emissivity correction: slightly more red, less blue than ideal BB
    bb *= float3(1.05, 0.98, 0.85);
    return bb;
}
```

### 4.4 Practical Skyrim Application — Fire/Forge/Lava Detection

Without material IDs, detect hot objects from color + brightness + depth:

```hlsl
// Heuristic fire/lava/forge detection from screen-space data
float DetectIncandescence(float3 color, float depth) {
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));

    // Fire/lava signature: high R, medium G, low B, bright
    float fireScore = saturate(color.r - color.b) * // red dominant
                      saturate(color.r - color.g * 0.5) * // not pure yellow
                      saturate(luma * 3.0 - 0.5); // bright enough

    // Forge/ember signature: deep red-orange, moderate brightness
    float emberScore = saturate(color.r * 2.0 - color.g - color.b) *
                       saturate(luma - 0.1) * saturate(1.0 - luma);

    // Combine scores
    return saturate(fireScore + emberScore * 0.5);
}

// Apply incandescent glow to detected hot regions
float3 ApplyIncandescenceGlow(float2 uv, float3 color, float depth, float glowRadius) {
    float hotness = DetectIncandescence(color, depth);

    if(hotness < 0.05) return color;

    // Estimate temperature from color
    float temp = lerp(1200.0, 4000.0, saturate(hotness));
    float3 glowTint = BlackbodyColor(temp);

    // Apply radial glow proportional to hotness
    float3 glow = 0.0;
    float tw = 0.0;
    [unroll]
    for(int i = 0; i < 12; i++) {
        float angle = i * 0.5236; // 30-degree steps
        float2 offset = float2(cos(angle), sin(angle)) * glowRadius * hotness;
        float3 s = TextureColor.Sample(smpLinear, uv + offset * PixelSize).rgb;
        float sw = DetectIncandescence(s, depth);
        glow += s * sw;
        tw += sw;
    }
    if(tw > 0.0) glow /= tw;

    return color + glow * glowTint * hotness * 0.3;
}
```

### 4.5 Heat Distortion Combined with Glow

```hlsl
// Heat shimmer above hot surfaces using noise-based UV distortion
float2 HeatDistortion(float2 uv, float time, float heatMask, float strength) {
    // Two octaves of animated noise
    float2 noiseUV = uv * float2(20.0, 40.0) + float2(0, time * 2.0);
    float noise1 = frac(sin(dot(noiseUV, float2(12.9898, 78.233))) * 43758.5) - 0.5;
    noiseUV *= 2.0;
    noiseUV.y += time * 3.0;
    float noise2 = frac(sin(dot(noiseUV, float2(39.346, 11.135))) * 43758.5) - 0.5;

    float2 distortion = float2(noise1 + noise2 * 0.5, 0) * strength * heatMask;
    return uv + distortion * PixelSize * 3.0;
}
```

**Performance**: Medium (detection heuristic) + Medium (glow sampling).
**Pipeline stage**: enbeffect.fx or enbeffectpostpass.fx. Use SB_Computed_Luminance for
scene brightness context. Heat detection can leverage SB data (torch equipped, interior state).

---

## 5. ADVANCED LIGHT BLOOM & DIFFUSION

### 5.1 Physical Diffusion — Pro-Mist / Orton Effect

**Pro-Mist Filter**: Scatters highlights without reducing contrast much. The filter has micro-particles
embedded in glass that create localized scatter around bright points.

**Orton Effect**: Sharp + blurred composite. The blurred layer is multiplied (not screen-blended),
then the sharp layer overlaid. This creates glow while maintaining edge detail.

```hlsl
// Orton Effect: sharp * blurred composite
float3 OrtonEffect(float2 uv, float blurAmount, float intensity) {
    float3 sharp = TextureColor.Sample(smpLinear, uv).rgb;
    // Use pre-blurred mip for the soft layer
    float3 soft = TextureColor.SampleLevel(smpLinear, uv, blurAmount).rgb;
    // Orton: multiply sharp and soft, then overlay
    float3 orton = sharp * soft; // multiply blend darkens, but soft version glows
    // Screen blend to preserve highlights
    float3 screen = 1.0 - (1.0 - sharp) * (1.0 - soft * intensity);
    return lerp(sharp, screen, intensity);
}

// Pro-Mist: Scatter only from highlights, preserve shadows
float3 ProMistFilter(float2 uv, float threshold, float scatterStrength) {
    float3 scene = TextureColor.Sample(smpLinear, uv).rgb;
    float luma = dot(scene, float3(0.2126, 0.7152, 0.0722));

    // Extract highlights above threshold
    float3 highlights = max(scene - threshold, 0.0);

    // 16-tap Poisson disc scatter
    static const float2 poissonDisc[16] = {
        float2(-0.94201, -0.39906), float2(0.94558, -0.76890),
        float2(-0.09418, -0.92938), float2(0.34495,  0.29387),
        float2(-0.91588, -0.45771), float2(-0.81544,  0.56972),
        float2(-0.38277, -0.56915), float2(0.97484,  0.75648),
        float2(-0.27369,  0.00461), float2(0.16896, -0.13983),
        float2(-0.59149,  0.39748), float2(0.72063,  0.12545),
        float2(-0.69591, -0.17924), float2(0.20978, -0.49627),
        float2(0.60235,  0.56437), float2(-0.16938,  0.87514)
    };

    float3 scatter = 0.0;
    [unroll]
    for(int i = 0; i < 16; i++) {
        float2 offset = poissonDisc[i] * scatterStrength * 10.0;
        scatter += max(TextureColor.Sample(smpLinear,
                  uv + offset * PixelSize).rgb - threshold, 0.0);
    }
    scatter /= 16.0;

    // Soft composite
    return scene + scatter * 0.3;
}
```
**Performance**: Medium (16-tap Poisson for pro-mist).
**Pipeline stage**: enbeffect.fx (main post-process), after tonemapping adjustments.

### 5.2 Veiling Glare — Eye Internal Scatter

The human eye scatters ~1-2% of incoming light across the retina. ISO 9241 models this as a
power-law PSF: glare(angle) = 10 / angle^2 (Stiles-Holladay).

```hlsl
// Veiling glare approximation using wide-radius blur
float3 VeilingGlare(float2 uv, float scatterAmount) {
    float3 scene = TextureColor.Sample(smpLinear, uv).rgb;

    // Very wide blur using pre-computed mip chain (TextureBloom if available)
    // Alternatively, sample from progressively larger offsets
    float3 glare = 0.0;
    float totalW = 0.0;

    // Logarithmic spiral sampling for wide coverage
    [unroll]
    for(int i = 0; i < 16; i++) {
        float r = exp((float)i * 0.3) * 2.0; // exponentially growing radius
        float angle = i * 2.39996; // golden angle
        float2 offset = float2(cos(angle), sin(angle)) * r;
        float w = 1.0 / (1.0 + r * r); // 1/r^2 falloff (Stiles-Holladay)
        glare += TextureColor.SampleLevel(smpLinear,
                 uv + offset * PixelSize * scatterAmount, 0).rgb * w;
        totalW += w;
    }
    glare /= totalW;

    // Veiling glare is additive and not color-selective
    return scene + glare * 0.015 * scatterAmount;
}
```
**Performance**: Medium (16-tap spiral).
**Pipeline stage**: enbeffect.fx, after bloom addition.

### 5.3 Spectral Bloom — Wavelength-Dependent Scatter

Different wavelengths scatter differently through glass: in camera lenses, blue scatters slightly
more than red (short wavelengths diffract more). This creates warm cores with cool halos.

```hlsl
// Per-channel bloom with different radii
float3 SpectralBloom(float2 uv, float baseRadius) {
    // Red: least scatter, Blue: most scatter
    float3 radii = float3(baseRadius * 0.8, baseRadius, baseRadius * 1.3);
    float3 bloom;

    // Sample each channel at its own radius
    bloom.r = BlurSample(uv, radii.x).r;
    bloom.g = BlurSample(uv, radii.y).g;
    bloom.b = BlurSample(uv, radii.z).b;

    return bloom;
}

// Helper: 8-tap radial blur at given radius
float3 BlurSample(float2 uv, float radius) {
    float3 sum = 0.0;
    [unroll]
    for(int i = 0; i < 8; i++) {
        float angle = i * 0.785398;
        float2 offset = float2(cos(angle), sin(angle)) * radius;
        sum += TextureColor.Sample(smpLinear, uv + offset * PixelSize).rgb;
    }
    return sum / 8.0;
}
```
**Performance**: Medium-Heavy (3x bloom sampling or 24 taps).
**Pipeline stage**: enbbloom.fx, integrate into mip chain with per-channel radii.

### 5.4 Energy-Conserving Bloom

Standard bloom adds energy (brightens scene). Physically correct bloom redistributes energy:
the source should be dimmed by the amount of light scattered outward.

```hlsl
// Energy-conserving bloom: subtract bloom contribution from source
float3 EnergyConservingBloom(float3 scene, float3 bloomTexture, float bloomStrength) {
    // bloom = scattered light that left the source pixels
    // Subtract estimated scatter from source, add bloom back
    float3 sceneMinusScatter = scene * (1.0 - bloomStrength * 0.5);
    return sceneMinusScatter + bloomTexture * bloomStrength;
}

// Progressive mip-chain approach (Jimenez 2014 / Karis):
// Downsample: 13-tap weighted average per mip level
// Karis average on first downsample to prevent fireflies:
//   weight = 1.0 / (1.0 + dot(color, LUM_709))
// Upsample: 9-tap tent filter, additive blend between mips
// Mix: blend result with scene at controlled intensity
```
**Performance**: Light (just changes blend math).
**Pipeline stage**: enbeffect.fx when compositing TextureBloom.

### 5.5 Diffraction Spikes / Star Patterns

Aperture blades create diffraction spikes. N-blade aperture: N spikes (even N) or 2N spikes (odd N).

```hlsl
// Diffraction spike pattern from bright point
float DiffractionSpike(float2 uv, float2 sourceUV, int bladeCount, float spikeLength) {
    float2 delta = uv - sourceUV;
    float dist = length(delta);
    float angle = atan2(delta.y, delta.x);

    // Spike pattern: peaks at blade angles
    float spikePattern = pow(abs(cos(angle * (float)bladeCount * 0.5)), 16.0);

    // Intensity falls off with distance
    float falloff = 1.0 / (1.0 + dist * dist / (spikeLength * spikeLength));

    return spikePattern * falloff;
}

// Screen-space diffraction applied to bloom
float3 DiffractionBloom(float2 uv, int bladeCount, float spikeLength, float threshold) {
    float3 scene = TextureColor.Sample(smpLinear, uv).rgb;
    float3 bloom = 0.0;

    // Sample bright spots and apply spike pattern
    // In practice: apply to pre-thresholded bloom texture with directional blur
    // For each of bladeCount directions, do a 1D blur along that axis

    [unroll]
    for(int blade = 0; blade < 6; blade++) { // 6-blade aperture
        float angle = blade * 3.14159 / 6.0;
        float2 dir = float2(cos(angle), sin(angle));

        float3 lineBloom = 0.0;
        [unroll]
        for(int t = 1; t <= 8; t++) {
            float2 samplePos = uv + dir * (float)t * spikeLength * PixelSize;
            float w = 1.0 / (float)t; // 1/r falloff
            float3 s = max(TextureColor.Sample(smpLinear, samplePos).rgb - threshold, 0.0);
            lineBloom += s * w;
            // Also sample opposite direction
            samplePos = uv - dir * (float)t * spikeLength * PixelSize;
            s = max(TextureColor.Sample(smpLinear, samplePos).rgb - threshold, 0.0);
            lineBloom += s * w;
        }
        bloom += lineBloom;
    }

    return scene + bloom * 0.02;
}
```
**Performance**: Heavy (bladeCount * taps * 2 fetches). Use sparingly, or apply only to bloom buffer.
**Pipeline stage**: enbbloom.fx or enblens.fx.

### 5.6 Convolutional Bloom (FFT-Based)

The gold standard (iMMERSE Ultimate). A measured PSF kernel is convolved with the scene in
frequency domain. Without compute shaders, a pixel-shader approximation:

```hlsl
// Separable approximation of PSF convolution
// Instead of FFT, use cascaded directional blurs matching the PSF shape
// For a 6-blade star PSF: 3 separable passes (0deg, 60deg, 120deg)
// Each pass: horizontal-like blur along the rotated axis

float3 PSFConvolutionApprox(float2 uv, float kernelSize) {
    float3 result = 0.0;

    // 3 directional passes for hex-symmetric PSF
    float angles[3] = {0.0, 1.0472, 2.0944}; // 0, 60, 120 degrees

    [unroll]
    for(int d = 0; d < 3; d++) {
        float2 dir = float2(cos(angles[d]), sin(angles[d]));
        float3 lineSum = 0.0;
        float totalW = 0.0;

        [unroll]
        for(int i = -8; i <= 8; i++) {
            float w = exp(-0.5 * (float)(i*i) / (kernelSize * kernelSize));
            float2 offset = dir * (float)i * 2.0;
            lineSum += TextureColor.Sample(smpLinear,
                      uv + offset * PixelSize).rgb * w;
            totalW += w;
        }
        result += lineSum / totalW;
    }

    return result / 3.0;
}
```
**Performance**: Heavy (3 directions * 17 taps = 51 fetches).
**Pipeline stage**: enbbloom.fx, as replacement for standard Gaussian bloom.

### 5.7 Purkinje Shift — Night Vision Adaptation

At low light, peak sensitivity shifts from 555nm (green) toward 507nm (blue-green).
Rods are most sensitive at ~498nm. Below ~0.01 cd/m2, color vision fades entirely.

```hlsl
// Purkinje shift: scotopic/mesopic vision simulation
float3 PurkinjeShift(float3 color, float sceneAvgLuma) {
    // Transition from photopic (cones, 555nm peak) to scotopic (rods, 507nm peak)
    float adaptation = saturate(1.0 - sceneAvgLuma * 5.0); // 0=day, 1=dark

    // Scotopic luminance weights (rod sensitivity)
    float scotopicLuma = dot(color, float3(0.062, 0.608, 0.330)); // shifted to blue-green

    // At low light: desaturate and shift blue
    float3 scotopicColor = scotopicLuma * float3(0.4, 0.7, 1.0); // blue-shifted monochrome

    // Mesopic blend
    float3 result = lerp(color, scotopicColor, adaptation * 0.7);

    // Reduce overall sensitivity (dim scene)
    result *= lerp(1.0, 0.3, adaptation);

    return result;
}
```
**Parameters**: sceneAvgLuma from SB_Computed_Luminance.x.
**Performance**: Light (math only, no extra texture fetches).
**Pipeline stage**: enbeffect.fx, in final color grading. Tie to SB adaptation data.

### 5.8 Soft Glow with Edge Preservation — Bilateral Bloom

Standard bloom bleeds across sharp boundaries. Bilateral bloom respects depth and normal
discontinuities.

```hlsl
// Depth-aware bilateral bloom
float3 BilateralBloom(float2 uv, float bloomRadius, float depthThreshold) {
    float3 centerColor = TextureColor.Sample(smpLinear, uv).rgb;
    float centerDepth = TextureDepth.Sample(smpPoint, uv).x;

    float3 bloom = 0.0;
    float totalWeight = 0.0;

    [unroll]
    for(int y = -3; y <= 3; y++) {
        [unroll]
        for(int x = -3; x <= 3; x++) {
            float2 offset = float2(x, y) * bloomRadius;
            float2 sampleUV = uv + offset * PixelSize;

            float3 sampleColor = TextureColor.Sample(smpLinear, sampleUV).rgb;
            float sampleDepth = TextureDepth.Sample(smpPoint, sampleUV).x;

            // Spatial weight (Gaussian)
            float spatialW = exp(-0.5 * dot(float2(x,y), float2(x,y)) / 4.0);

            // Depth weight (bilateral)
            float depthDiff = abs(centerDepth - sampleDepth);
            float depthW = exp(-depthDiff * depthDiff / (depthThreshold * depthThreshold));

            float w = spatialW * depthW;
            bloom += sampleColor * w;
            totalWeight += w;
        }
    }

    return bloom / max(totalWeight, 0.001);
}
```
**Performance**: Medium-Heavy (49 taps with depth reads).
**Pipeline stage**: enbbloom.fx or enbeffect.fx (needs TextureDepth — not available in enbbloom.fx
unless using TextureOriginal's associated depth). Better in enbeffect.fx.

### 5.9 Anamorphic Lens Flare / Horizontal Streak

Anamorphic lenses create horizontal light streaks from bright sources (the cylindrical element
stretches the flare along one axis).

```hlsl
// Anamorphic horizontal streak bloom
float3 AnamorphicStreak(float2 uv, float threshold, float streakLength, float3 tint) {
    float3 scene = TextureColor.Sample(smpLinear, uv).rgb;

    // Extract bright pixels
    float luma = dot(scene, float3(0.2126, 0.7152, 0.0722));
    float brightPass = saturate(luma - threshold);

    // Horizontal-only blur (anamorphic streak)
    float3 streak = 0.0;
    float totalW = 0.0;

    [unroll]
    for(int i = -16; i <= 16; i++) {
        float w = exp(-0.5 * (float)(i*i) / (streakLength * streakLength));
        float2 sampleUV = uv + float2(i * 3.0, 0) * PixelSize;
        float3 s = TextureColor.Sample(smpLinear, sampleUV).rgb;
        float sl = dot(s, float3(0.2126, 0.7152, 0.0722));
        s *= saturate(sl - threshold);
        streak += s * w;
        totalW += w;
    }
    streak /= totalW;

    return scene + streak * tint * 0.5;
}
// tint: float3(0.6, 0.7, 1.0) for classic blue anamorphic
```
**Performance**: Medium (33-tap 1D blur).
**Pipeline stage**: enblens.fx or enbbloom.fx. Already partially in CinematicFX anamorphic effect.

### 5.10 Screen-Space Lens Flare (John Chapman Method)

Generate "ghosts" by sampling at positions mirrored through screen center from bright spots:

```hlsl
float3 ScreenSpaceLensFlare(float2 uv, float threshold, int ghostCount, float ghostSpacing) {
    // Flip UV through center
    float2 ghostUV = 1.0 - uv;
    float2 ghostDir = normalize(ghostUV - 0.5);

    float3 ghosts = 0.0;

    [unroll]
    for(int i = 0; i < 8; i++) {
        if(i >= ghostCount) break;
        float2 samplePos = frac(ghostUV + ghostDir * (float)i * ghostSpacing);
        float3 s = TextureColor.SampleLevel(smpLinear, samplePos, 2.0).rgb;
        float l = dot(s, float3(0.2126, 0.7152, 0.0722));
        s *= saturate(l - threshold);

        // Chromatic aberration per ghost
        float ca = (float)i * 0.002;
        float3 caColor;
        caColor.r = TextureColor.SampleLevel(smpLinear, samplePos + ca, 2.0).r;
        caColor.g = s.g;
        caColor.b = TextureColor.SampleLevel(smpLinear, samplePos - ca, 2.0).b;

        // Falloff toward edges
        float edgeFade = 1.0 - length(samplePos - 0.5) * 2.0;
        edgeFade = saturate(edgeFade * edgeFade);

        ghosts += caColor * saturate(l - threshold) * edgeFade;
    }

    return ghosts / (float)ghostCount;
}
```
**Performance**: Medium (ghostCount * 3 fetches).
**Pipeline stage**: enblens.fx.

### 5.11 Glare Patterns — Measured Camera PSF

Real camera lens glare can be approximated by convolving with a measured PSF. Without FFT in
pixel shaders, approximate with multi-directional star + halo:

```hlsl
// Camera glare = star pattern + halo ring
float3 CameraGlare(float2 uv, float threshold, int starPoints, float starSize, float haloRadius) {
    float3 star = 0.0;

    // Star rays
    [unroll]
    for(int r = 0; r < 8; r++) {
        if(r >= starPoints) break;
        float angle = r * 3.14159 / (float)starPoints;
        float2 dir = float2(cos(angle), sin(angle));

        [unroll]
        for(int t = 1; t <= 12; t++) {
            float w = 1.0 / ((float)t * (float)t);
            float2 pos = uv + dir * (float)t * starSize * PixelSize;
            float3 s = max(TextureColor.Sample(smpLinear, pos).rgb - threshold, 0.0);
            star += s * w;
        }
    }

    // Halo ring
    float3 halo = 0.0;
    [unroll]
    for(int h = 0; h < 16; h++) {
        float angle = h * 0.3927;
        float2 pos = uv + float2(cos(angle), sin(angle)) * haloRadius * PixelSize;
        float3 s = max(TextureColor.Sample(smpLinear, pos).rgb - threshold, 0.0);
        halo += s;
    }
    halo /= 16.0;

    return star * 0.01 + halo * 0.05;
}
```
**Performance**: Heavy (starPoints * 12 + 16 taps).
**Pipeline stage**: enblens.fx.

---

## Integration Summary for ENB Pipeline

| Effect | Best Shader Stage | Textures Needed | Performance | Priority |
|--------|------------------|-----------------|-------------|----------|
| CRT Mask (advanced) | enbeffectpostpass | TextureColor | Light | High |
| CRT Beam Profile | enbeffectpostpass | TextureColor | Light | High |
| CRT Halation | enbeffectpostpass | TextureColor + TextureBloom* | Light | High |
| CRT Interlace | enbeffectpostpass | TextureColor + Timer | Light | Medium |
| CRT NTSC Artifacts | enbeffectpostpass | TextureColor | Heavy | Medium |
| CRT Color Profile | enbeffectpostpass | TextureColor | Light | Medium |
| CRT Convergence | enbeffectpostpass | TextureColor | Light | Low |
| CRT Curvature (adv) | enbeffectpostpass | TextureColor | Light | High |
| VHS Full Pipeline | enbeffectpostpass | TextureColor + Timer | Heavy | Medium |
| VHS Lite (chroma+noise) | enbeffectpostpass | TextureColor + Timer | Medium | Medium |
| Phosphor Persistence | enbeffectpostpass | TextureColor + PrevRT | Light | Medium |
| Phosphor Burn-In | enbeffectpostpass | TextureColor + PersistRT | Light | Low |
| Blackbody Color | enbeffect/postpass | TextureColor | Light | High |
| Thermal Bloom | enbeffect | TextureColor + Depth | Medium | Medium |
| Heat Distortion | enbeffect | TextureColor + Timer | Light | Medium |
| Incandescence Detect | enbeffect | TextureColor + Depth | Medium | Medium |
| Pro-Mist Diffusion | enbeffect | TextureColor | Medium | High |
| Orton Effect | enbeffect | TextureColor (mips) | Light | High |
| Veiling Glare | enbeffect | TextureColor | Medium | Medium |
| Spectral Bloom | enbbloom | TextureDownsampled | Heavy | Medium |
| Energy-Conserv. Bloom | enbeffect | TextureBloom | Light | High |
| Diffraction Spikes | enbbloom/enblens | TextureColor | Heavy | Low |
| Anamorphic Streak | enblens | TextureColor | Medium | High |
| Bilateral Bloom | enbeffect | TextureColor + Depth | Heavy | Medium |
| Purkinje Shift | enbeffect | TextureColor + SB data | Light | High |
| SS Lens Flare | enblens | TextureColor (mips) | Medium | Medium |

*TextureBloom is output of enbbloom.fx and available in enbeffect.fx via TextureBloom.
TextureDownsampled is the 1024x1024 pre-downscale available in enbbloom.fx.

### Key Constraints for ENB Pixel Shaders (no compute)
1. **No UAV / RWTexture**: All output via SV_Target only
2. **No shared memory / groupshared**: No thread cooperation
3. **No FFT**: Must use separable convolution or cascaded blur approximations
4. **Limited passes**: Each technique = one draw call. Multi-pass via sequential techniques.
5. **enbeffectpostpass.fx is LDR** (R10G10B10A2): clamp to [0,1], 2-bit alpha
6. **Timer.x** = elapsed time in ms. SB_Render_Frame.x = frame counter.
7. **No previous frame texture** natively. Persistent RT trick: write to RT in one technique,
   read it back in the next technique on the following frame.

---

## Sources

- [CRT-Royale - Libretro Docs](https://docs.libretro.com/shader/crt_royale/)
- [CRT-Royale - Emulation Wiki](https://emulation.gametechwiki.com/index.php/CRT-Royale)
- [CRT-Royale Source (libretro/glsl-shaders)](https://github.com/libretro/glsl-shaders/tree/master/crt/shaders/crt-royale/src)
- [CRT-Royale scanline-functions.h](https://github.com/libretro/common-shaders/blob/master/crt/shaders/crt-royale/src/scanline-functions.h)
- [CRT Shader Masks - Filthy Pants](http://filthypants.blogspot.com/2020/02/crt-shader-masks.html)
- [CRT-Geom - Emulation Wiki](https://emulation.gametechwiki.com/index.php/CRT_Geom)
- [Guest-Advanced CRT (Libretro Forums)](https://forums.libretro.com/t/new-crt-shader-from-guest-crt-guest-advanced-updates/25444/2282)
- [Blur Busters CRT Beam Simulator](https://github.com/blurbusters/crt-beam-simulator)
- [Blur Busters CRT Beam Simulator HLSL](https://github.com/blurbusters/crt-beam-simulator/blob/main/crt-simulator.hlsl)
- [Phosphor Decay Math (Blur Busters Forums)](https://forums.blurbusters.com/viewtopic.php?t=418)
- [CRT Shaders Overview - Emulation Wiki](https://emulation.gametechwiki.com/index.php/CRT_Shaders)
- [Cathode Retro - Generating Signal](https://cathoderetro.com/docs/how/generating-signal.html)
- [Cathode Retro - Shader Reference](https://cathoderetro.com/docs/shader-reference/index.html)
- [Cathode Retro - Faking CRT](https://www.cathoderetro.com/docs/how/faking-crt.html)
- [Cathode Retro - Shadow Mask](https://cathoderetro.com/docs/shader-reference/crt-shaders/generate-shadow-mask.html)
- [Themaister's NTSC Shader Explained](https://retinaleclipse.tumblr.com/post/51384766987/themaisters-ntsc-shader-explained)
- [NTSC.fx ReShade Shader (Zackin5)](https://github.com/Zackin5/Misc-ReShade-Shaders/blob/master/NTSC.fx)
- [CRT-Guest-NTSC.fx ReShade](https://github.com/HelelSingh/CRT-Guest-ReShade/blob/main/Shaders/CRT-Guest-NTSC.fx)
- [MAME NTSC HLSL](https://github.com/mamedev/mame/blob/master/hlsl/ntsc.fx)
- [RetroTVFX (Composite/S-Video/RF)](https://github.com/GlaireDaggers/RetroTVFX)
- [Composite Video Decoding Theory](https://codeandlife.com/2012/10/09/composite-video-decoding-theory-and-practice/)
- [NTSC Video - NESdev Wiki](https://www.nesdev.org/wiki/NTSC_video)
- [VHS Shader - ReShade Forum](https://reshade.me/forum/shader-presentation/1258-vhs-shader)
- [VHS Image Effect Write-Up (Harry Alisavakis)](https://halisavakis.com/write-up-vhs-image-effect/)
- [VHS Tape Effect (Shadertoy)](https://www.shadertoy.com/view/Ms3XWH)
- [VHS Tape Shader (Shadertoy)](https://www.shadertoy.com/view/sltBWM)
- [VHS Compression (Shadertoy)](https://www.shadertoy.com/view/tsfXWj)
- [VHS Distortion (Shadertoy)](https://www.shadertoy.com/view/4dBGzK)
- [Macrovision Protection Explained](https://www.freevideoworkshop.com/macrovision-protection-in-vhs-and-dvd/)
- [Macrovision Demystified (Stanford)](https://cs.stanford.edu/people/eroberts/cs181/projects/1999-00/dmca-2k/macrovision.html)
- [P22 Phosphor Designations (CRT Database)](https://crtdatabase.com/faq/phosphor-designations)
- [Phosphor - Wikipedia](https://en.wikipedia.org/wiki/Phosphor)
- [CRT Color Calibration Guide](https://consolemods.org/wiki/CRT:CRT_Color_Calibration_Guide)
- [BlackBodyRadiation HLSL (zubetto/GitHub)](https://github.com/zubetto/BlackBodyRadiation)
- [BlackBodyRadiation.hlsl Source](https://github.com/zubetto/BlackBodyRadiation/blob/main/BlackBodyRadiation.hlsl)
- [Blackbody Rendering (Scratchapixel)](https://www.scratchapixel.com/lessons/cg-gems/blackbody/blackbody.html)
- [Blackbody Rendering (Miles Macklin)](https://blog.mmacklin.com/2010/12/29/blackbody-rendering/)
- [Blackbody Color (Shadertoy)](https://www.shadertoy.com/view/4tVBWW)
- [Physically-Based Real-Time Glare (DiVA Thesis)](https://www.diva-portal.org/smash/get/diva2:1629565/FULLTEXT01.pdf)
- [Physically-Based Lens Flare Rendering (Hullin et al.)](https://resources.mpi-inf.mpg.de/lensflareRendering/pdf/flare.pdf)
- [Physically Based Bloom (LearnOpenGL)](https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom)
- [Standard Bloom (LearnOpenGL)](https://learnopengl.com/Advanced-Lighting/Bloom)
- [Bloom (Wikipedia)](https://en.wikipedia.org/wiki/Bloom_(shader_effect))
- [Convolution Bloom Guide (Marty's Mods)](https://guides.martysmods.com/shaders/immerseultimate/convolutionbloom/)
- [iMMERSE Shaders (GitHub)](https://github.com/martymcmodding/iMMERSE)
- [Diffraction Chapter (GPU Gems/NVIDIA)](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-8-simulating-diffraction)
- [Screen Space Lens Flare (John Chapman)](https://john-chapman.github.io/2017/11/05/pseudo-lens-flare.html)
- [Anamorphic Lens Flares (Bart Wronski)](https://bartwronski.com/2015/03/09/anamorphic-lens-flares-and-visual-effects/)
- [SS Lens Flare (MJP)](https://mynameismjp.wordpress.com/2009/12/15/more-post-processing-tricks-lens-flare/)
- [Orton Bloom - ReShade Forum](https://reshade.me/forum/shader-presentation/4148-orton-bloom)
- [Purkinje Shift (Hanover College)](https://isle.hanover.edu/Ch03Eye/Ch03PurkinjieSim_evt.html)
- [Scotopic Vision (Wikipedia)](https://en.wikipedia.org/wiki/Scotopic_vision)
- [Mesopic Vision (Wikipedia)](https://en.wikipedia.org/wiki/Mesopic_vision)
- [Perceptually Based Tone Mapping (ACM)](https://dl.acm.org/doi/10.1145/2010324.1964937)
- [Depth-Aware Upsampling (c0de517e)](http://c0de517e.blogspot.com/2016/02/downsampled-effects-with-depth-aware.html)
- [Shadow Mask (Wikipedia)](https://en.wikipedia.org/wiki/Shadow_mask)
- [S-Video (Wikipedia)](https://en.wikipedia.org/wiki/S-Video)
- [Composite Video (Wikipedia)](https://en.wikipedia.org/wiki/Composite_video)
- [Barrel Distortion (Prideout)](https://prideout.net/barrel-distortion)
- [AdvancedCRT.hlsl (SweetFX)](https://github.com/zachsaw/RenderScripts/blob/master/RenderScripts/ImageProcessingShaders/SweetFX/AdvancedCRT.hlsl)
- [Interlacing Shader for CRTs (Filthy Pants)](http://filthypants.blogspot.com/2014/04/interlacing-shader-for-crts.html)
- [Deinterlacing (Wikipedia)](https://en.wikipedia.org/wiki/Deinterlacing)
- [Designing Large-Scale Phosphor Filter (Filthy Pants)](http://filthypants.blogspot.com/2013/02/designing-large-scale-phosphor.html)
- [NTSC (Wikipedia)](https://en.wikipedia.org/wiki/NTSC)
- [sRGB (Wikipedia)](https://en.wikipedia.org/wiki/SRGB)
- [Veiling Glare (Wikipedia)](https://en.wikipedia.org/wiki/Veiling_glare)
- [Planckian Locus (Wikipedia)](https://en.wikipedia.org/wiki/Planckian_locus)
