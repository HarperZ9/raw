// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Tone mapping pixel shader.
// Implements 12 published tone curves selected by CurveType:
//   0 = AgX           (Troy Sobotka)
//   1 = ACES Fitted   (Stephen Hill)
//   2 = Reinhard Ext  (Reinhard et al., 2002)
//   3 = Hejl-Burgess  (Jim Hejl, GDC 2010)
//   4 = ACES Narkowicz (Krzysztof Narkowicz, 2014)
//   5 = AgX Punchy    (Troy Sobotka, saturated variant)
//   6 = PBR Neutral   (Khronos Group)
//   7 = Uncharted 2   (John Hable / Naughty Dog)
//   8 = Lottes        (Timothy Lottes, 2016)
//   9 = Uchimura      (Hajime Uchimura, Gran Turismo)
//  10 = Tony McMapface (perceptual balance)
//  11 = Linear        (debug passthrough)
//
// After tonemapping: Skyrim gamma 1.6 correction, then sRGB 2.2 encoding
// (SDR), or PQ ST.2084 encoding (HDR10).

cbuffer ToneMapCB : register(b0)
{
    float CurrentEV;
    int   CurveType;       // 0-11 tone curve selection
    int   HDROutput;       // 0=SDR, 1=HDR
    float PaperWhiteNits;
    float MaxNits;
    float VanillaInfluence;
    float Padding0, Padding1;
}
cbuffer VanillaParams : register(b7)
{
    // Row 0 — HDR
    float VP_EyeAdaptSpeed;
    float VP_BloomScale;
    float VP_BloomThreshold;
    float VP_SunlightScale;
    // Row 1 — Cinematic
    float VP_Saturation;
    float VP_Brightness;
    float VP_Contrast;
    float VP_TintAmount;
    // Row 2 — Tint + DOF
    float VP_TintR, VP_TintG, VP_TintB;
    float VP_DOFStrength;
    // Row 3 — DOF + IMOD
    float VP_DOFDistance, VP_DOFRange;
    float VP_IMODActive;
    float VP_IMODStrength;
}

static const float SKYRIM_GAMMA = 1.6;

Texture2D<float4> SceneColor : register(t0);
SamplerState PointSampler : register(s0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

// ── Vanilla cinematic grading ────────────────────────────────────────────
// Applies Skyrim's ImageSpace saturation, brightness, contrast, and tint.
float3 ApplyVanillaGrade(float3 color)
{
    // Saturation: lerp toward luminance
    float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(float3(lum, lum, lum), color, VP_Saturation);

    // Brightness: multiplicative scale
    color *= VP_Brightness;

    // Contrast: pivot around 0.5
    color = (color - 0.5) * VP_Contrast + 0.5;

    // Tint: lerp toward tint color
    float3 tint = float3(VP_TintR, VP_TintG, VP_TintB);
    color = lerp(color, color * tint, VP_TintAmount);

    return color;
}

// ── Curve 0: AgX (Troy Sobotka) ──────────────────────────────────────────
// Published specification: https://github.com/sobotka/AgX
//
// AgX is a display rendering transform designed for well-behaved behavior
// in high-saturation, high-dynamic-range regions.  It works by:
// 1. Transforming to a log-encoded AgX space via an inset matrix
// 2. Applying a contrast curve (6th order polynomial approximation)
// 3. Transforming back via an outset matrix

float3 AgXDefaultContrastApprox(float3 x)
{
    // 6th order polynomial fit to the AgX default contrast curve.
    // Attempt to match the response of the published AgX Base contrast
    // look from the Blender/AgX specification.
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return 15.5 * x4 * x2
         - 40.14 * x4 * x
         + 31.96 * x4
         - 6.868 * x2 * x
         + 0.4298 * x2
         + 0.1191 * x
         - 0.00232;
}

float3 AgX(float3 color)
{
    // AgX inset matrix: transforms from working space (sRGB linear primaries)
    // into the AgX log encoding space.
    // Published in the AgX specification by Troy Sobotka.
    static const float3x3 AgXInsetMatrix = float3x3(
        0.842479062253094,  0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772,  0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104
    );

    // AgX outset matrix: inverse transform back from AgX space to working space.
    static const float3x3 AgXOutsetMatrix = float3x3(
         1.19687900512017,  -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368,  1.15190312990417,  -0.0980434066481054,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116
    );

    // Encode into AgX log space
    // Clamp to avoid log2 of zero/negative
    color = max(color, 1e-10);
    color = mul(AgXInsetMatrix, color);

    // Log2 encoding, clamped to [min_ev, max_ev] range then normalized to [0,1]
    // AgX uses a log range of approximately [-12.47393, 4.026069]
    static const float AgXMinEV = -12.47393;
    static const float AgXMaxEV = 4.026069;
    color = log2(color);
    color = (color - AgXMinEV) / (AgXMaxEV - AgXMinEV);
    color = saturate(color);

    // Apply the contrast sigmoid approximation
    color = AgXDefaultContrastApprox(color);

    // Transform back to working space
    color = mul(AgXOutsetMatrix, color);
    color = max(color, 0.0);

    return color;
}

// ── Curve 1: ACES Fitted (Stephen Hill) ──────────────────────────────────
// Published: Stephen Hill, "ACES Filmic Tone Mapping Curve" blog post.
// RRT (Reference Rendering Transform) + ODT (Output Device Transform)
// fitted to a single rational polynomial per channel after matrix transforms.

float3 ACESFitted(float3 color)
{
    // sRGB -> ACEScg input matrix (RRT working space)
    static const float3x3 ACESInputMat = float3x3(
        0.59719, 0.35458, 0.04823,
        0.07600, 0.90834, 0.01566,
        0.02840, 0.13383, 0.83777
    );

    // ACEScg -> sRGB output matrix
    static const float3x3 ACESOutputMat = float3x3(
         1.60475, -0.53108, -0.07367,
        -0.10208,  1.10813, -0.00605,
        -0.00327, -0.07276,  1.07602
    );

    color = mul(ACESInputMat, color);

    // RRT + ODT approximation (rational polynomial)
    float3 a = color * (color + 0.0245786) - 0.000090537;
    float3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    color = a / b;

    color = mul(ACESOutputMat, color);
    return saturate(color);
}

// ── Curve 2: Reinhard Extended ───────────────────────────────────────────
// Published: Reinhard et al., "Photographic Tone Reproduction for Digital
// Images", SIGGRAPH 2002.
//
// Luminance-based mapping with a configurable white point that controls
// where the curve approaches 1.0.

float3 ReinhardExtended(float3 color, float whitePoint)
{
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    float wp2 = whitePoint * whitePoint;
    float mapped = (luma * (1.0 + luma / wp2)) / (1.0 + luma);
    return color * (mapped / max(luma, 0.001));
}

// ── Curve 3: Hejl-Burgess (Uncharted 2 simplified) ──────────────────────
// Published: Jim Hejl, "Filmic Tonemapping for Real-time Rendering",
// GDC 2010.
//
// Single-pass filmic curve with built-in gamma approximation.  The result
// already has a rough sRGB-like gamma baked in, but we still apply our
// full gamma pipeline afterward for consistency.

float3 Hejl(float3 color)
{
    color = max(0.0, color - 0.004);
    return (color * (6.2 * color + 0.5))
         / (color * (6.2 * color + 1.7) + 0.06);
}

// ── Additional operators (published mathematical specifications) ─────────

// ACES Narkowicz (Krzysztof Narkowicz, 2014 — simple fitted curve)
float3 ACESNarkowicz(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// AgX Punchy (Troy Sobotka — saturated variant with contrast push)
float3 AgXPunchy(float3 color)
{
    float3 base = AgX(color);
    // Push saturation + contrast for a punchier look
    float luma = dot(base, float3(0.2126, 0.7152, 0.0722));
    float3 sat = lerp(luma.xxx, base, 1.35);
    return saturate(sat * 1.05);
}

// PBR Neutral (Khronos Group — KHR_PBR_Neutral extension)
float3 PBRNeutral(float3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;
    float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return lerp(color, newPeak.xxx, g);
}

// Uncharted 2 / Hable (John Hable, Naughty Dog, GDC 2010)
float3 Uncharted2Partial(float3 x)
{
    const float A = 0.15, B = 0.50, C = 0.10;
    const float D = 0.20, E = 0.02, F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}
float3 Uncharted2(float3 color)
{
    float3 curr = Uncharted2Partial(color);
    float3 white = Uncharted2Partial(11.2.xxx);
    return curr / white;
}

// Lottes (Timothy Lottes, 2016 — AMD)
float3 Lottes(float3 color)
{
    const float a = 1.6, d = 0.977;
    const float hdrMax = 8.0, midIn = 0.18, midOut = 0.267;
    float b = (-pow(midIn, a) + pow(hdrMax, a) * midOut) /
              ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
    float c = (pow(hdrMax, a * d) * pow(midIn, a) - pow(hdrMax, a) * pow(midIn, a * d) * midOut) /
              ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
    return pow(color, a) / (pow(color, a * d) * b + c);
}

// Uchimura (Hajime Uchimura, Gran Turismo, CEDEC 2017)
float3 Uchimura(float3 x)
{
    const float P = 1.0, a = 1.0, m = 0.22, l = 0.4, c = 1.33, b = 0.0;
    float l0 = ((P - m) * l) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;
    float3 w0 = 1.0 - smoothstep(0.0, m, x);
    float3 w2 = step(m + l0, x);
    float3 w1 = 1.0 - w0 - w2;
    float3 T = m * pow(x / m, c) + b;
    float3 S = P - (P - S1) * exp(CP * (x - S0));
    float3 L = m + a * (x - m);
    return T * w0 + L * w1 + S * w2;
}

// Tony McMapface (perceptually balanced simple operator)
float3 TonyMcMapface(float3 color)
{
    float3 encoded = color / (color + 1.0);
    float luma = dot(encoded, float3(0.2126, 0.7152, 0.0722));
    float satFactor = 1.0 - pow(luma, 2.0) * 0.4;
    return lerp(luma.xxx, encoded, satFactor);
}

// ── PQ (ST.2084) encoding for HDR10 ─────────────────────────────────────
// Published: SMPTE ST 2084 (Perceptual Quantizer).
// Maps linear luminance (in normalized units where 1.0 = 10000 nits) to
// the PQ EOTF^-1 nonlinear encoding for HDR10 displays.

float3 LinearToPQ(float3 linearColor)
{
    // PQ constants from SMPTE ST.2084
    static const float m1 = 0.1593017578125;    // 2610 / 16384
    static const float m2 = 78.84375;           // 2523 / 32 * 128
    static const float c1 = 0.8359375;          // 3424 / 4096
    static const float c2 = 18.8515625;         // 2413 / 128 * 32
    static const float c3 = 18.6875;            // 2392 / 128 * 32

    // Input is in normalized linear light [0, 1] where 1.0 = 10000 nits
    float3 Lm1 = pow(max(linearColor, 0.0), m1);
    float3 numerator = c1 + c2 * Lm1;
    float3 denominator = 1.0 + c3 * Lm1;
    return pow(numerator / denominator, m2);
}

// ─────────────────────────────────────────────────────────────────────────

float4 main(VSOut input) : SV_Target
{
    float3 color = SceneColor.Sample(PointSampler, input.uv).rgb;

    // ── Linearize from Skyrim's gamma 1.6 space ─────────────────────
    // Skyrim's internal scene color is stored in a gamma 1.6 encoding.
    // Convert to linear light for tonemapping.
    color = pow(max(color, 0.0), SKYRIM_GAMMA);

    // ── Apply exposure ──────────────────────────────────────────────
    // CurrentEV is log2(avgLuminance / 0.18).
    // Exposure multiplier = 2^(-CurrentEV) = 0.18 / avgLuminance.
    float exposure = exp2(-CurrentEV);
    color *= exposure;

    // ── Apply vanilla cinematic grading (blended by VanillaInfluence) ──
    if (VanillaInfluence > 0.001)
    {
        float3 graded = ApplyVanillaGrade(color);
        color = lerp(color, graded, VanillaInfluence);
    }

    // ── Tone curve ──────────────────────────────────────────────────
    float3 mapped;
    if (CurveType == 0)
    {
        mapped = AgX(color);
    }
    else if (CurveType == 1)
    {
        mapped = ACESFitted(color);
    }
    else if (CurveType == 2)
    {
        // White point at 4.0 gives good highlight rolloff for game content
        mapped = ReinhardExtended(color, 4.0);
    }
    else if (CurveType == 3)
        mapped = Hejl(color);
    else if (CurveType == 4)
        mapped = ACESNarkowicz(color);
    else if (CurveType == 5)
        mapped = AgXPunchy(color);
    else if (CurveType == 6)
        mapped = PBRNeutral(color);
    else if (CurveType == 7)
        mapped = Uncharted2(color);
    else if (CurveType == 8)
        mapped = Lottes(color);
    else if (CurveType == 9)
        mapped = Uchimura(color);
    else if (CurveType == 10)
        mapped = TonyMcMapface(color);
    else
        mapped = saturate(color);  // 11 = Linear / debug passthrough

    // ── Output encoding ─────────────────────────────────────────────
    if (HDROutput)
    {
        // HDR10 PQ output (SMPTE ST.2084)
        // Scale from [0,1] scene-referred to nit-based, normalized to 10000 nits.
        float3 hdrColor = mapped * (PaperWhiteNits / 10000.0);
        hdrColor = min(hdrColor, MaxNits / 10000.0);
        return float4(LinearToPQ(hdrColor), 1.0);
    }
    else
    {
        // SDR: apply sRGB gamma encoding (linear -> 2.2 gamma)
        float3 sdrColor = pow(max(mapped, 0.0), 1.0 / 2.2);
        return float4(sdrColor, 1.0);
    }
}
