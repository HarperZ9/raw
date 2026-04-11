// =============================================================================
// ColorPipeline --- Multi-stage color grading pixel shader
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// References:
//   Oklab perceptual color space -- Bjorn Ottosson, 2020
//   Color temperature approximation -- Tanner Helland (1000K-40000K)
//   CIE color science, photographic film chemistry
//   Triangular-PDF dithering -- Gjoel / Christensen 2012
// =============================================================================

struct VSOut
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

cbuffer ColorPipelineCB : register(b0)
{
    // row 0
    float   CurrentEV;
    float   DeltaTime;
    uint    StageMask;
    int     ToneCurve;

    // row 1
    float   FilmToe;
    float   FilmShoulder;
    float   FilmGamma;
    float   FilmDensity;

    // row 2
    float   InterimageStrength;
    float   PurkinjeStrength;
    float   HuntStrength;
    float   LocalTMStrength;

    // row 3
    float   PrinterR;
    float   PrinterG;
    float   PrinterB;
    float   ColorTemp;

    // row 4
    float   ShadowTintA;
    float   ShadowTintB;
    float   ShadowTintStrength;
    float   HighlightTintA;

    // row 5
    float   HighlightTintB;
    float   HighlightTintStrength;
    float   CDLSlopeR;
    float   CDLSlopeG;

    // row 6
    float   CDLSlopeB;
    float   CDLSaturation;
    float   CDLOffsetR;
    float   CDLOffsetG;

    // row 7
    float   CDLOffsetB;
    float   CDLPower;
    float   BleachAmount;
    float   Saturation;

    // row 8
    float   DitherSeed;
    int     HDROutput;
    float   PaperWhiteNits;
    float   MaxNits;

    // row 9
    float   WhiteBalanceTemp;
    float   ExposureComp;
    float   PunchySaturation;
    float   StevensAdaptation;

    // row 10
    float   LiftR, LiftG, LiftB;
    float   GammaR;

    // row 11
    float   GammaG, GammaB;
    float   GainR, GainG;

    // row 12
    float   GainB;
    float   Vibrance;
    float   SCurveContrast;
    float   VanillaInfluence;

    // row 13
    float   FilmToeR, FilmToeG, FilmToeB;
    float   FilmShoulderR;

    // row 14
    float   FilmShoulderG, FilmShoulderB;
    float   FilmGammaR, FilmGammaG;

    // row 15
    float   FilmGammaB;
    float   FilmPrintToe;
    float   FilmPrintShoulder;
    float   FilmPrintGamma;
}
cbuffer VanillaParams : register(b7)
{
    // Row 0 -- HDR
    float VP_EyeAdaptSpeed;
    float VP_BloomScale;
    float VP_BloomThreshold;
    float VP_SunlightScale;
    // Row 1 -- Cinematic
    float VP_Saturation;
    float VP_Brightness;
    float VP_Contrast;
    float VP_TintAmount;
    // Row 2 -- Tint + DOF
    float VP_TintR, VP_TintG, VP_TintB;
    float VP_DOFStrength;
    // Row 3 -- DOF + IMOD
    float VP_DOFDistance, VP_DOFRange;
    float VP_IMODActive;
    float VP_IMODStrength;
}

// ---- Stage bitmask flags (must match C++ enum) ----
static const uint STAGE_EXPOSURE  = 1u;
static const uint STAGE_STEVENS   = 2u;
static const uint STAGE_PURKINJE  = 4u;
static const uint STAGE_LOCALTM   = 8u;
static const uint STAGE_FILM      = 16u;
static const uint STAGE_CONTRAST  = 32u;
static const uint STAGE_HUNT      = 64u;
static const uint STAGE_TONEMAP   = 128u;
static const uint STAGE_AGXPUNCHY = 256u;
static const uint STAGE_GRADE     = 512u;
static const uint STAGE_EXTGRADE  = 1024u;
static const uint STAGE_DITHER    = 2048u;

Texture2D<float4> SceneColor : register(t0);  // Backbuffer copy
Texture2D<float4> BloomTex   : register(t1);  // BloomRenderer output
Texture3D<float4> LUTVolume  : register(t18); // 3D LUT (64^3 from LUTManager)
SamplerState LinearSampler : register(s0);
SamplerState PointSampler  : register(s1);
SamplerState LUTSampler    : register(s2);   // Trilinear clamp for LUT

// =============================================================================
// Oklab color space -- Bjorn Ottosson, 2020
// Linear sRGB -> Oklab and Oklab -> Linear sRGB
// Published matrices: https://bottosson.github.io/posts/oklab/
// =============================================================================

float3 LinearToOklab(float3 c)
{
    // Step 1: linear sRGB -> LMS (cone response)
    float l = 0.4122214708f * c.r + 0.5363325363f * c.g + 0.0514459929f * c.b;
    float m = 0.2119034982f * c.r + 0.6806995451f * c.g + 0.1073969566f * c.b;
    float s = 0.0883024619f * c.r + 0.2817188376f * c.g + 0.6299787005f * c.b;

    // Step 2: cube root (perceptual nonlinearity)
    float l_ = pow(max(l, 0.0), 1.0 / 3.0);
    float m_ = pow(max(m, 0.0), 1.0 / 3.0);
    float s_ = pow(max(s, 0.0), 1.0 / 3.0);

    // Step 3: LMS' -> Lab
    return float3(
        0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
        1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
        0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_
    );
}

float3 OklabToLinear(float3 lab)
{
    // Step 1: Lab -> LMS'
    float l_ = lab.x + 0.3963377774f * lab.y + 0.2158037573f * lab.z;
    float m_ = lab.x - 0.1055613458f * lab.y - 0.0638541728f * lab.z;
    float s_ = lab.x - 0.0894841775f * lab.y - 1.2914855480f * lab.z;

    // Step 2: cube (invert cube root)
    float l = l_ * l_ * l_;
    float m = m_ * m_ * m_;
    float s = s_ * s_ * s_;

    // Step 3: LMS -> linear sRGB
    return float3(
         4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
        -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
        -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s
    );
}

// =============================================================================
// White Balance -- Tanner Helland's color temperature approximation
// Maps Kelvin (1000-40000) to an RGB multiplier.
// Source: tannerhelland.com/4435/convert-temperature-rgb-algorithm-code/
// =============================================================================

float3 KelvinToRGB(float kelvin)
{
    float temp = clamp(kelvin, 1000.0, 40000.0) / 100.0;
    float3 rgb;

    // Red
    if (temp <= 66.0)
        rgb.r = 1.0;
    else
        rgb.r = saturate(1.29293618606 * pow(temp - 60.0, -0.1332047592));

    // Green
    if (temp <= 66.0)
        rgb.g = saturate(0.39008157876 * log(temp) - 0.63184144378);
    else
        rgb.g = saturate(1.12989086090 * pow(temp - 60.0, -0.0755148492));

    // Blue
    if (temp >= 66.0)
        rgb.b = 1.0;
    else if (temp <= 19.0)
        rgb.b = 0.0;
    else
        rgb.b = saturate(0.54320678911 * log(temp - 10.0) - 1.19625408914);

    return rgb;
}

float3 ApplyWhiteBalance(float3 color, float tempKelvin)
{
    float3 source = KelvinToRGB(tempKelvin);
    float3 reference = KelvinToRGB(6500.0);    // D65 reference white
    return color * (reference / max(source, 1e-6));
}

// =============================================================================
// Exposure -- Simple EV multiplication
// =============================================================================

float3 ApplyExposure(float3 color, float ev)
{
    return color * pow(2.0, ev);
}

// =============================================================================
// Log-domain contrast -- around 18% grey pivot
// =============================================================================

float3 ApplyContrast(float3 color, float contrast)
{
    float3 logColor = log2(max(color, 1e-6));
    float  logPivot = log2(0.18);
    return exp2((logColor - logPivot) * contrast + logPivot);
}


// =============================================================================
// ColorPipeline --- Multi-stage color grading pixel shader (continued)
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
// =============================================================================

// =============================================================================
// Luminance-preserving saturation
// =============================================================================

float3 ApplySaturation(float3 color, float sat)
{
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    return lerp(luma.xxx, color, sat);
}

// =============================================================================
// Vibrance -- saturation boost that protects already-saturated colors
// Low-saturation pixels receive more boost than high-saturation ones.
// =============================================================================

float3 ApplyVibrance(float3 color, float vibrance)
{
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    float maxC = max(color.r, max(color.g, color.b));
    float minC = min(color.r, min(color.g, color.b));
    float chroma = maxC - minC;
    // Existing saturation ratio: 0 for grey, ~1 for fully saturated
    float existingSat = (maxC > 1e-6) ? (chroma / maxC) : 0.0;
    // Inverse weight: boost unsaturated pixels more
    float weight = 1.0 - existingSat;
    float effectiveSat = 1.0 + vibrance * weight;
    return lerp(luma.xxx, color, effectiveSat);
}

// =============================================================================
// Lift / Gamma / Gain -- 3-way color correction
// =============================================================================

float3 ApplyLiftGammaGain(float3 color, float3 lift, float3 gamma, float3 gain)
{
    // Shadows (lift):   color = color + lift * (1 - color)
    color = color + lift * (1.0 - color);

    // Midtones (gamma): color = pow(color, 1.0 / gamma)
    float3 safeGamma = max(gamma, 0.01);
    color = pow(max(color, 0.0), 1.0 / safeGamma);

    // Highlights (gain): color = color * gain
    color = color * gain;

    return color;
}

// =============================================================================
// Split Toning -- shadow and highlight tint in Oklab space
// Shadow tint weighted by (1 - luma), highlight tint by luma.
// Tint values are Oklab a,b channels for perceptually uniform blending.
// =============================================================================

float3 ApplySplitToning(float3 color,
                        float shadowA, float shadowB, float shadowStr,
                        float highlightA, float highlightB, float highlightStr)
{
    float3 lab = LinearToOklab(max(color, 0.0));
    float  L = saturate(lab.x);  // perceptual lightness [0,1]

    // Shadow tint: weight by darkness
    float shadowWeight  = (1.0 - L) * shadowStr;
    lab.y += shadowA * shadowWeight;
    lab.z += shadowB * shadowWeight;

    // Highlight tint: weight by lightness
    float highlightWeight = L * highlightStr;
    lab.y += highlightA * highlightWeight;
    lab.z += highlightB * highlightWeight;

    return max(OklabToLinear(lab), 0.0);
}

// =============================================================================
// Film Grain -- density-dependent procedural grain
// Uses procedural blue-noise-like hash for spatial distribution.
// Grain intensity rolls off in bright areas (multiplicative, not additive).
// =============================================================================

// Quality hash for grain -- ALU-based, avoids texture dependency
float GrainHash(float2 p, float seed)
{
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33 + seed);
    return frac((p3.x + p3.y) * p3.z);
}

float3 ApplyFilmGrain(float3 color, float2 pixelCoord, float density, float rolloff, float seed)
{
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    // Density-dependent: more grain in shadows, less in highlights
    float grainStrength = density * (1.0 - saturate(luma / max(rolloff, 0.01)));
    // Procedural blue-noise-like pattern
    float noise = GrainHash(pixelCoord, seed);
    // Multiplicative grain preserves color ratios
    float grainMul = 1.0 + (noise - 0.5) * 2.0 * grainStrength;
    return color * max(grainMul, 0.0);
}

// =============================================================================
// Vignette -- cos^4(theta) optical vignette model
// =============================================================================

float ComputeVignette(float2 uv, float strength)
{
    float2 d = uv - 0.5;
    float r2 = dot(d, d) * 4.0;
    return pow(saturate(1.0 - r2 * strength), 2.0);
}

// =============================================================================
// Blue noise triangular-PDF dithering -- prevents banding on 8-bit output
// Gjoel / Christensen 2012 -- triangular distribution from uniform
// =============================================================================

float3 ApplyDither(float3 color, float2 pixelCoord, float seed)
{
    // Generate two independent uniform noise values per channel
    float3 noise1 = float3(
        GrainHash(pixelCoord, seed + 0.0),
        GrainHash(pixelCoord, seed + 1.7),
        GrainHash(pixelCoord, seed + 3.1)
    );
    float3 noise2 = float3(
        GrainHash(pixelCoord, seed + 5.3),
        GrainHash(pixelCoord, seed + 7.9),
        GrainHash(pixelCoord, seed + 11.3)
    );
    // Triangular PDF: sum of two uniform -> triangle distribution in [-1, 1]
    float3 tri = noise1 + noise2 - 1.0;
    // Scale to 1 LSB for 8-bit output
    color += tri / 255.0;
    return color;
}

// =============================================================================
// Tonemapping operators — Reference: ShaderTechniques_MasterReference.md
// ToneCurve enum: 0=AgX, 1=ACES, 2=ReinhardExt, 3=Hejl, 4=Hable, 5=Lottes, 6=GranTurismo, 7=None
// =============================================================================

// AgX (Sobotka 2022) — formation-based, preserves hue through highlight compression
static const float3x3 AGX_INSET = float3x3(
    0.842479062253094, 0.0784336015616190, 0.0792237324783498,
    0.0423282422610123, 0.878468629590388, 0.0791846348885739,
    0.0423756549057051, 0.0784336015616190, 0.879190727181896);
static const float3x3 AGX_OUTSET = float3x3(
    1.19687900512017, -0.0980208811401368, -0.0990434085205346,
   -0.0528968517574562, 1.15190312990417, -0.0989611768448433,
   -0.0529716355144438, -0.0980434501171241, 1.15107367264116);

float3 AgXTonemap(float3 c)
{
    c = mul(AGX_INSET, max(c, 0.0));
    c = clamp(log2(max(c, 1e-10)), -12.47393, 4.02607);
    c = (c + 12.47393) / 16.5;
    // Piecewise sigmoid
    float3 x = c;
    float3 t = step(x, 0.5);
    float3 lo = 0.5 * pow(2.0 * x, 2.8);
    float3 hi = 1.0 - 0.5 * pow(2.0 * (1.0 - x), 2.8);
    c = lerp(hi, lo, t);
    c = pow(max(c, 0.0), 2.2);
    c = mul(AGX_OUTSET, c);
    return pow(max(c, 0.0), 1.0 / 2.2);
}

// ACES Narkowicz fit (Krzysztof Narkowicz 2015)
float3 ACESTonemap(float3 c)
{
    c *= 0.6; // pre-expose
    return saturate((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14));
}

// Reinhard Extended with configurable white point
float3 ReinhardExtTonemap(float3 c)
{
    float wp = 4.0;
    return c * (1.0 + c / (wp * wp)) / (1.0 + c);
}

// Hejl-Burgess (Jim Hejl 2010, includes built-in gamma approximation)
float3 HejlTonemap(float3 c)
{
    float3 x = max(c - 0.004, 0.0);
    return (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
}

// Hable/Uncharted 2 (John Hable 2010)
float3 HableMap(float3 x)
{
    float A=0.15, B=0.50, C=0.10, D=0.20, E=0.02, F=0.30;
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F)) - E/F;
}
float3 HableTonemap(float3 c)
{
    float W = 11.2;
    return HableMap(c * 2.0) / HableMap(W.xxx);
}

// Lottes (Timothy Lottes 2016)
float3 LottesTonemap(float3 c)
{
    float3 a = 1.6, d = 0.977, m = 0.18, l = 0.267, s = 0.0;
    float3 h = a * d;
    float3 b = (-pow(m, -a*d) + pow(m, -a)) / ((pow(m, -a*d*l) - pow(m, -a*l)) * s);
    return pow(max(c, 0.0), a) / (pow(max(c, 0.0), a * d) * b + 1.0);
}

// Gran Turismo (Hajime Uchimura 2017)
float3 GTTonemap(float3 c)
{
    float P=1.0, a=1.0, m=0.22, l=0.4, cs=1.33, b=0.0;
    float l0 = ((P-m)*l) / a;
    float S0 = m + l0, S1 = m + a*l0;
    float C2 = (a*P) / (P - S1);
    float CP = -C2 / P;
    float3 w0 = 1.0 - smoothstep(0.0, m, c);
    float3 w2 = step(m + l0, c);
    float3 w1 = 1.0 - w0 - w2;
    float3 T = m * pow(c / m, cs.xxx) + b;
    float3 L = m + a * (c - m);
    float3 S = P - (P - S1) * exp(CP * (c - S0));
    return T * w0 + L * w1 + S * w2;
}

float3 ApplyTonemap(float3 c, int curve)
{
    if (curve == 0) return AgXTonemap(c);
    if (curve == 1) return ACESTonemap(c);
    if (curve == 2) return ReinhardExtTonemap(c);
    if (curve == 3) return HejlTonemap(c);
    if (curve == 4) return HableTonemap(c);
    if (curve == 5) return LottesTonemap(c);
    if (curve == 6) return GTTonemap(c);
    return saturate(c); // 7=None/linear clamp
}

// =============================================================================
// Film Emulation — per-channel negative/print stock curves from CB parameters
// Reference: FilmColorGrading_Techniques_Research.md
// =============================================================================

float FilmCurve(float x, float toe, float gamma, float shoulder)
{
    float toeR = pow(max(x, 0.0) / (max(x, 0.0) + toe), gamma);
    float shoR = 1.0 - pow(saturate(1.0 - x / shoulder), 2.0);
    float blend = saturate(x / (shoulder * 0.5));
    return lerp(toeR, shoR, blend * blend);
}

float3 ApplyFilmNegative(float3 c)
{
    c.r = FilmCurve(c.r, FilmToeR, FilmGammaR, FilmShoulderR);
    c.g = FilmCurve(c.g, FilmToeG, FilmGammaG, FilmShoulderG);
    c.b = FilmCurve(c.b, FilmToeB, FilmGammaB, FilmShoulderB);
    return c;
}

float3 ApplyFilmPrint(float3 c)
{
    c.r = FilmCurve(c.r, FilmPrintToe, FilmPrintGamma, FilmPrintShoulder);
    c.g = FilmCurve(c.g, FilmPrintToe, FilmPrintGamma, FilmPrintShoulder);
    c.b = FilmCurve(c.b, FilmPrintToe, FilmPrintGamma, FilmPrintShoulder);
    return c;
}

float3 ApplySubtractiveDensity(float3 c, float density)
{
    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    float3 delta = c - luma;
    float sat = length(delta);
    float factor = 1.0 - saturate(sat * density);
    float3 chroma = (luma > 0.001) ? (c / luma) : 1.0;
    return chroma * (luma * factor);
}

// Inter-layer cross-talk — dye coupler contamination between emulsion layers
// Kodak V3 matrix: small off-diagonal terms simulate how each dye layer
// bleeds into its neighbors (red->green, green->blue, etc.)
static const float3x3 CROSSTALK_KODAK_V3 = float3x3(
    0.93, 0.05, 0.02,
    0.03, 0.94, 0.03,
    0.02, 0.04, 0.94);

float3 ApplyCrossTalk(float3 c, float strength)
{
    float3x3 mat = lerp(float3x3(1,0,0, 0,1,0, 0,0,1), CROSSTALK_KODAK_V3, strength);
    return mul(mat, c);
}

// Halation approximation — uses bloom texture as spatial blur proxy
// Real halation is a red-channel glow from light bouncing off film base
// We approximate by biasing bloom warm and adding it as an underlay
float3 ApplyHalation(float3 c, float3 bloom, float amount)
{
    float3 halationTint = float3(1.0, 0.5, 0.15); // Warm orange-red film base bounce
    float bloomLuma = dot(bloom, float3(0.2126, 0.7152, 0.0722));
    float3 halation = bloom * halationTint * bloomLuma; // Intensity-weighted warm glow
    return c + halation * amount;
}

// Technicolor 2-Strip (1922-1932) — red and green dye layers only
float3 Technicolor2Strip(float3 c)
{
    float3 cyanDye = float3(0.0, 1.0, 1.0) * c.r;
    float3 redDye  = float3(1.0, 0.35, 0.0) * c.g;
    float3 combined = saturate(cyanDye + redDye);
    float luma = dot(combined, float3(0.2126, 0.7152, 0.0722));
    return lerp(luma, combined, 1.3);
}

// Technicolor 3-Strip (1932-1955) — full CMY with Legato cross-talk removal
float3 Technicolor3Strip(float3 c, float purity)
{
    float r = c.r, g = c.g, b = c.b;
    float key = 0.15;
    r = max(r - (g + b) * key * 0.5, 0.0);
    g = max(g - (r + b) * key * 0.5, 0.0);
    b = max(b - (r + g) * key * 0.5, 0.0);
    float norm = (c.r + c.g + c.b + 0.001) / (r + g + b + 0.001);
    r *= norm; g *= norm; b *= norm;
    float3 result = float3(1.0 - r, 1.0, 1.0) * float3(1.0, 1.0 - g, 1.0) * float3(1.0, 1.0, 1.0 - b);
    return lerp(c, result, purity);
}

// =============================================================================
// Grade — ASC-CDL, Printer Lights, Bleach Bypass
// =============================================================================

float3 ApplyASCCDL(float3 c, float3 slope, float3 offset, float power, float sat)
{
    c = pow(max(c * slope + offset, 0.0), power);
    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    return lerp(luma.xxx, c, sat);
}

float3 ApplyPrinterLights(float3 c, float3 printer)
{
    return c * pow(2.0, (printer - 25.0) / 12.0); // 25=neutral, each unit=1/12 stop
}

float3 ApplyBleachBypass(float3 c, float amount)
{
    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    float3 bleached = lerp(c, luma.xxx, amount) * (1.0 + amount * 0.5);
    return lerp(c, bleached, amount);
}

// =============================================================================
// 3D LUT — samples Texture3D volume via hardware trilinear (LUTManager at t18)
// =============================================================================

float3 ApplyLUT3D(float3 c)
{
    float3 uvw = saturate(c);
    // Half-texel inset for proper volume sampling (64^3)
    float scale = 63.0 / 64.0;
    float offset = 0.5 / 64.0;
    uvw = uvw * scale + offset;
    return LUTVolume.SampleLevel(LUTSampler, uvw, 0).rgb;
}

// =============================================================================
// Local Tone Mapping — bloom-driven local contrast
// =============================================================================

float3 ApplyLocalTM(float3 c, float2 uv, float strength)
{
    float3 bloom = BloomTex.Sample(LinearSampler, uv).rgb;
    float localLuma = dot(bloom, float3(0.2126, 0.7152, 0.0722));
    float globalLuma = dot(c, float3(0.2126, 0.7152, 0.0722));
    float ratio = globalLuma / max(localLuma, 0.001);
    float boost = lerp(1.0, saturate(ratio), strength);
    return c * boost;
}

// =============================================================================
// Output encoding — sRGB gamma or PQ (ST.2084)
// =============================================================================

float3 LinearToSRGB(float3 c)
{
    float3 lo = c * 12.92;
    float3 hi = 1.055 * pow(max(c, 0.0), 1.0 / 2.4) - 0.055;
    return lerp(hi, lo, step(c, 0.0031308));
}

// Skyrim gamma 1.6 output (CS approach — Skyrim's textures were authored
// for a nonlinear framebuffer, so 2.2 over-darkens shadows)
float3 LinearToSkyrimGamma(float3 c)
{
    return pow(max(c, 0.0), 1.0 / 1.6);
}

float3 LinearToPQ(float3 c, float paperWhite, float maxNits)
{
    c *= paperWhite / 10000.0;
    float m1=0.1593017578125, m2=78.84375, c1=0.8359375, c2=18.8515625, c3=18.6875;
    float3 Ym1 = pow(max(c, 0.0), m1);
    return pow((c1 + c2 * Ym1) / (1.0 + c3 * Ym1), m2);
}

// =============================================================================
// Main pixel shader entry point
// =============================================================================

float4 main(VSOut input) : SV_Target
{
    float2 uv = input.texcoord;
    float2 pixelCoord = input.position.xy;
    float3 color = SceneColor.Sample(PointSampler, uv).rgb;

    // ---- White Balance + Exposure (STAGE_EXPOSURE) --------------------------
    if (StageMask & STAGE_EXPOSURE)
    {
        color = ApplyWhiteBalance(color, WhiteBalanceTemp);
        color = ApplyExposure(color, ExposureComp);
    }

    // ---- Local Tone Mapping (STAGE_LOCALTM) — bloom-driven contrast ---------
    if (StageMask & STAGE_LOCALTM)
    {
        color = ApplyLocalTM(color, uv, LocalTMStrength);
    }

    // ---- Film Pipeline (STAGE_FILM) — full photochemical emulation ----------
    if (StageMask & STAGE_FILM)
    {
        // Negative stock: per-channel H&D curves
        if (FilmToeR > 0.001)
            color = ApplyFilmNegative(color);
        // Subtractive density: saturated colors darken (the "Hollywood secret")
        if (FilmDensity > 0.001)
            color = ApplySubtractiveDensity(color, FilmDensity);
        // Inter-layer cross-talk: dye coupler contamination
        if (InterimageStrength > 0.001)
            color = ApplyCrossTalk(color, InterimageStrength);
        // Print stock: high-gamma expansion curves
        if (FilmPrintToe > 0.001)
            color = ApplyFilmPrint(color);
        // Halation: warm glow from bloom (approximates film base light bounce)
        float3 bloom = BloomTex.Sample(LinearSampler, uv).rgb;
        if (FilmGamma > 0.001 && any(bloom > 0.0)) // Repurpose FilmGamma as halation amount
            color = ApplyHalation(color, bloom, FilmGamma * 0.5);
    }

    // ---- Contrast (STAGE_CONTRAST) — log-domain around 18% grey -------------
    if (StageMask & STAGE_CONTRAST)
    {
        color = ApplyContrast(color, SCurveContrast);
    }

    // ---- Tonemapping (STAGE_TONEMAP) — HDR to display range -----------------
    if (StageMask & STAGE_TONEMAP)
    {
        color = ApplyTonemap(max(color, 0.0), ToneCurve);
    }

    // ---- AgX Punchy (STAGE_AGXPUNCHY) — post-tonemap saturation boost -------
    if (StageMask & STAGE_AGXPUNCHY)
    {
        float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
        color = luma + PunchySaturation * (color - luma);
        color = saturate(color);
    }

    // ---- Grade (STAGE_GRADE) — CDL + Printer Lights + Bleach + Saturation ---
    if (StageMask & STAGE_GRADE)
    {
        // Printer Lights (per-channel log exposure)
        if (any(float3(PrinterR, PrinterG, PrinterB) != 25.0))
            color = ApplyPrinterLights(color, float3(PrinterR, PrinterG, PrinterB));

        // ASC-CDL (slope, offset, power, saturation)
        float3 slope = float3(CDLSlopeR, CDLSlopeG, CDLSlopeB);
        if (any(slope != 1.0) || any(float3(CDLOffsetR, CDLOffsetG, CDLOffsetB) != 0.0))
            color = ApplyASCCDL(color, slope,
                float3(CDLOffsetR, CDLOffsetG, CDLOffsetB), CDLPower, CDLSaturation);

        // Bleach Bypass
        if (BleachAmount > 0.001)
            color = ApplyBleachBypass(color, BleachAmount);

        // Saturation
        color = ApplySaturation(color, Saturation);

        // Split Toning (Oklab perceptual blending)
        color = ApplySplitToning(color,
            ShadowTintA, ShadowTintB, ShadowTintStrength,
            HighlightTintA, HighlightTintB, HighlightTintStrength);
    }

    // ---- Extended Grade (STAGE_EXTGRADE) — Vibrance + LGG -------------------
    if (StageMask & STAGE_EXTGRADE)
    {
        color = ApplyVibrance(color, Vibrance);
        color = ApplyLiftGammaGain(color,
            float3(LiftR, LiftG, LiftB),
            float3(GammaR, GammaG, GammaB),
            float3(GainR, GainG, GainB));
    }

    // ---- 3D LUT (when LUTManager provides a volume at t18) -----------------
    // Applied after all grading, before spatial effects
    float3 lutResult = LUTVolume.SampleLevel(LUTSampler, float3(0.5, 0.5, 0.5), 0).rgb;
    bool lutValid = any(lutResult != 0.0); // Check if LUT is actually bound
    if (lutValid)
        color = ApplyLUT3D(color);

    // ---- Film Grain (STAGE_FILM) — density-dependent, after all color ops ---
    if (StageMask & STAGE_FILM)
    {
        color = ApplyFilmGrain(color, pixelCoord, FilmDensity * 0.5, FilmShoulder, DitherSeed);
    }

    // ---- Vignette (STAGE_HUNT) — cos^4 optical model ------------------------
    if (StageMask & STAGE_HUNT)
    {
        color *= ComputeVignette(uv, HuntStrength);
    }

    // ---- Output encoding ----------------------------------------------------
    // 0=sRGB 2.2, 1=PQ (HDR10), 2=scRGB passthrough, 3=Skyrim gamma 1.6
    color = max(color, 0.0);
    if (HDROutput == 1)
        color = LinearToPQ(color, PaperWhiteNits, MaxNits);
    else if (HDROutput == 3)
        color = LinearToSkyrimGamma(color);  // CS-style: preserves Skyrim's intended shadow look
    else if (HDROutput == 0)
        color = LinearToSRGB(color);

    // ---- Dither (STAGE_DITHER) — blue noise anti-banding, after encoding ----
    if (StageMask & STAGE_DITHER)
    {
        color = ApplyDither(color, pixelCoord, DitherSeed);
    }

    return float4(color, 1.0);
}
