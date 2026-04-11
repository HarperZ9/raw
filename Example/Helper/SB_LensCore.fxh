//=============================================================================
//  SB_LensCore.fxh — Physically-Based Lens Optics Core
//
//  Polynomial ghost rendering (Hullin 2012 / Bodonyi 2025)
//  ABCD ray transfer matrices (Lee-Eisemann 2013)
//  Coating quality model (Schlick Fresnel)
//  Brown-Conrady distortion with anamorphic extension
//  6-band spectral chromatic aberration (Wyman-Sloan-Shirley CIE 2013)
//  Veiling glare (Spencer-Shirley 1995 / Vos 2003 bloom-mip proxy)
//
//  Author: Zain Dana Harper — March 2026
//=============================================================================

#ifndef SB_LENS_CORE_FXH
#define SB_LENS_CORE_FXH


//=== LENS PRESET INDEX ===//
// 0 = Cooke Speed Panchro 50mm f/2 (1920s cinema, warm, heavy veiling)
// 1 = Zeiss Planar T* 50mm f/1.4 (modern clinical precision)
// 2 = MIR-1 37mm f/2.8 (Soviet wide-angle, swirly bokeh)
// 3 = Panavision Primo 50mm (modern cinema reference)


//=== POLYNOMIAL GHOST OPTICS (Hullin 2012) ===//

// Degree-3 rotationally symmetric polynomial coefficients for one ghost path.
// 7 coefficients define the complete mapping from sensor plane to ghost image.
struct LensPolyCoeffs
{
    float2 xy_linear;     // [magnification, lateral_shift]
    float2 xy_cubic_A;    // [pos^3 coeff, dir^3 coeff]
    float2 xy_cubic_B;    // [pos^2*dir coeff (coma), pos*dir^2 coeff (astigmatism)]
    float  cross_term;    // pos*(pos.dir) field curvature
    float  _pad;
};

// Evaluate polynomial ghost mapping for one bounce path.
// pos: normalized position on sensor plane [-1, 1]
// dir: normalized ray direction (derived from light source position)
float2 EvalGhostPoly(LensPolyCoeffs coeffs, float2 pos, float2 dir)
{
    float pp = dot(pos, pos);   // |p|^2
    float dd = dot(dir, dir);   // |d|^2
    float pd = dot(pos, dir);   // p . d

    float2 result;

    // x-component
    result.x  = coeffs.xy_linear.x * pos.x;           // magnification
    result.x += coeffs.xy_linear.y * dir.x;            // lateral shift
    result.x += coeffs.xy_cubic_A.x * pos.x * pp;      // barrel distortion
    result.x += coeffs.xy_cubic_A.y * dir.x * dd;      // spherical aberration
    result.x += coeffs.xy_cubic_B.x * dir.x * pp;      // coma
    result.x += coeffs.xy_cubic_B.y * pos.x * dd;      // astigmatism
    result.x += coeffs.cross_term   * pos.x * pd;      // field curvature

    // y-component (identical coefficients by rotational symmetry)
    result.y  = coeffs.xy_linear.x * pos.y;
    result.y += coeffs.xy_linear.y * dir.y;
    result.y += coeffs.xy_cubic_A.x * pos.y * pp;
    result.y += coeffs.xy_cubic_A.y * dir.y * dd;
    result.y += coeffs.xy_cubic_B.x * dir.y * pp;
    result.y += coeffs.xy_cubic_B.y * pos.y * dd;
    result.y += coeffs.cross_term   * pos.y * pd;

    return result;
}


//=== ABCD RAY TRANSFER MATRICES (Lee-Eisemann 2013) ===//

struct GhostABCD
{
    float2x2 mat;       // ABCD matrix [A,B; C,D]
    float2   offset;    // lateral offset from decentration
    float    intensity; // transmission factor (product of reflectances)
    float    _pad;
};

// Transform sensor UV to ghost image UV using ABCD matrix (first-order optics).
// 4 MAD per ghost — 7x cheaper than polynomial.
float2 EvalGhostABCD(GhostABCD ghost, float2 uv, float2 dir)
{
    float2 outPos;
    outPos.x = ghost.mat._11 * uv.x + ghost.mat._12 * dir.x + ghost.offset.x;
    outPos.y = ghost.mat._11 * uv.y + ghost.mat._12 * dir.y + ghost.offset.y;
    return outPos;
}


//=== COATING QUALITY MODEL ===//

// Compute single-surface reflectance from coating quality parameter.
// quality: 0.0 = uncoated (~4%), 1.0 = modern multi-coat (~0.3%)
float GetSurfaceReflectance(float coatingQuality)
{
    return exp(lerp(log(0.04), log(0.003), coatingQuality));
}

// Ghost intensity for a 2-bounce path through a 10-surface system.
float GetGhostIntensity(float quality)
{
    float R = GetSurfaceReflectance(quality);
    float T = 1.0 - R;
    return R * R * pow(T, 8.0); // R^2 * T^(N-2), N=10
}


//=== LENS PRESETS — GHOST COEFFICIENTS ===//
// Each preset defines 2 hero ghosts (polynomial) and 6 ABCD ghosts.
// Hero ghosts carry the lens character; ABCD ghosts fill background.

// --- Cooke Speed Panchro 50mm f/2 (1920s cinema) ---
// Strong barrel, noticeable coma, warm halation
static const LensPolyCoeffs COOKE_HERO[2] =
{
    { float2(-0.85, 0.12), float2(0.045, 0.008), float2(0.032, 0.018), 0.015, 0.0 },
    { float2(-0.60, 0.20), float2(0.038, 0.012), float2(0.028, 0.022), 0.020, 0.0 },
};
static const GhostABCD COOKE_ABCD[6] =
{
    { float2x2(-0.82, 0.10, -0.05, 0.95), float2(0.00, 0.00), 1.0, 0.0 },
    { float2x2( 0.75,-0.08,  0.04, 0.90), float2(0.01, 0.00), 0.8, 0.0 },
    { float2x2(-0.55, 0.15, -0.03, 0.85), float2(0.00, 0.01), 0.6, 0.0 },
    { float2x2( 0.48,-0.12,  0.06, 0.80), float2(0.00,-0.01), 0.5, 0.0 },
    { float2x2(-0.38, 0.18, -0.02, 0.75), float2(0.01, 0.01), 0.4, 0.0 },
    { float2x2( 0.30,-0.10,  0.03, 0.70), float2(-0.01,0.00), 0.3, 0.0 },
};

// --- Zeiss Planar T* 50mm f/1.4 (modern precision) ---
// Minimal distortion, near-zero aberrations, T* multi-coat
static const LensPolyCoeffs ZEISS_HERO[2] =
{
    { float2(-0.92, 0.05), float2(0.003, 0.001), float2(0.005, 0.002), 0.002, 0.0 },
    { float2(-0.70, 0.08), float2(0.004, 0.002), float2(0.006, 0.003), 0.003, 0.0 },
};
static const GhostABCD ZEISS_ABCD[6] =
{
    { float2x2(-0.90, 0.04, -0.02, 0.98), float2(0.00, 0.00), 1.0, 0.0 },
    { float2x2( 0.85,-0.03,  0.01, 0.96), float2(0.00, 0.00), 0.7, 0.0 },
    { float2x2(-0.65, 0.06, -0.01, 0.94), float2(0.00, 0.00), 0.5, 0.0 },
    { float2x2( 0.55,-0.05,  0.02, 0.92), float2(0.00, 0.00), 0.4, 0.0 },
    { float2x2(-0.42, 0.07, -0.01, 0.90), float2(0.00, 0.00), 0.3, 0.0 },
    { float2x2( 0.35,-0.04,  0.01, 0.88), float2(0.00, 0.00), 0.2, 0.0 },
};

// --- MIR-1 37mm f/2.8 (Soviet wide-angle, swirly bokeh) ---
// Strong field curvature, pronounced coma, swirl
static const LensPolyCoeffs MIR1_HERO[2] =
{
    { float2(-0.78, 0.18), float2(0.065, 0.022), float2(0.055, 0.038), 0.042, 0.0 },
    { float2(-0.52, 0.25), float2(0.058, 0.028), float2(0.048, 0.042), 0.048, 0.0 },
};
static const GhostABCD MIR1_ABCD[6] =
{
    { float2x2(-0.75, 0.16, -0.08, 0.88), float2(0.02, 0.01), 1.0, 0.0 },
    { float2x2( 0.68,-0.14,  0.06, 0.82), float2(-0.01, 0.02), 0.9, 0.0 },
    { float2x2(-0.50, 0.20, -0.05, 0.78), float2(0.01,-0.01), 0.7, 0.0 },
    { float2x2( 0.42,-0.18,  0.08, 0.72), float2(0.02, 0.00), 0.6, 0.0 },
    { float2x2(-0.35, 0.22, -0.04, 0.68), float2(0.00, 0.02), 0.5, 0.0 },
    { float2x2( 0.28,-0.15,  0.06, 0.65), float2(-0.01,-0.01), 0.4, 0.0 },
};

// --- Panavision Primo 50mm (modern cinema reference) ---
// Ultra-low distortion, controlled flare, neutral
static const LensPolyCoeffs PRIMO_HERO[2] =
{
    { float2(-0.95, 0.03), float2(0.002, 0.001), float2(0.004, 0.003), 0.001, 0.0 },
    { float2(-0.72, 0.06), float2(0.003, 0.001), float2(0.005, 0.003), 0.002, 0.0 },
};
static const GhostABCD PRIMO_ABCD[6] =
{
    { float2x2(-0.93, 0.03, -0.01, 0.99), float2(0.00, 0.00), 1.0, 0.0 },
    { float2x2( 0.88,-0.02,  0.01, 0.97), float2(0.00, 0.00), 0.6, 0.0 },
    { float2x2(-0.68, 0.05, -0.01, 0.95), float2(0.00, 0.00), 0.4, 0.0 },
    { float2x2( 0.58,-0.04,  0.01, 0.93), float2(0.00, 0.00), 0.3, 0.0 },
    { float2x2(-0.45, 0.06, -0.01, 0.91), float2(0.00, 0.00), 0.2, 0.0 },
    { float2x2( 0.38,-0.03,  0.01, 0.89), float2(0.00, 0.00), 0.15, 0.0 },
};


//=== GHOST SAMPLING HELPERS ===//

// Circular aperture mask — confines ghosts to lens barrel
float GhostApertureMask(float2 centeredUV, float radius)
{
    float dist = length(centeredUV) / radius;
    return 1.0 - smoothstep(0.8, 1.0, dist);
}


//=== BROWN-CONRADY DISTORTION ===//

struct DistortionPreset
{
    float k1, k2, k3;    // Radial distortion coefficients
    float p1, p2;         // Tangential distortion coefficients
    float anamorphic;     // Desqueeze factor (1.0 = spherical, 2.0 = 2x anamorphic)
};

// 4 distortion presets matching lens presets
static const DistortionPreset DISTORTION_PRESETS[4] =
{
    // Cooke Panchro: moderate barrel
    { -0.12,  0.04, -0.008,  0.001, 0.001, 1.0 },
    // Zeiss Planar: near-zero
    { -0.005, 0.001, 0.0,    0.0,   0.0,   1.0 },
    // MIR-1: strong barrel + tangential
    { -0.18,  0.06, -0.015,  0.003, 0.002, 1.0 },
    // Panavision Primo: negligible
    { -0.003, 0.0005, 0.0,   0.0,   0.0,   1.0 },
};

// Apply Brown-Conrady distortion with anamorphic extension.
// uv: input UV [0,1], aspect: screen aspect ratio
float2 ApplyBrownConrady(float2 uv, DistortionPreset preset, float aspect, float strength)
{
    float2 centered = uv - 0.5;
    centered.x *= aspect;

    // Anamorphic pre-squeeze
    centered.x /= preset.anamorphic;

    float r2 = dot(centered, centered);
    float r4 = r2 * r2;
    float r6 = r4 * r2;

    // Radial distortion (scaled by strength)
    float radialScale = 1.0 + (preset.k1 * r2 + preset.k2 * r4 + preset.k3 * r6) * strength;

    // Tangential distortion
    float2 tangential;
    tangential.x = (2.0 * preset.p1 * centered.x * centered.y
                 + preset.p2 * (r2 + 2.0 * centered.x * centered.x)) * strength;
    tangential.y = (preset.p1 * (r2 + 2.0 * centered.y * centered.y)
                 + 2.0 * preset.p2 * centered.x * centered.y) * strength;

    float2 distorted = centered * radialScale + tangential;

    // Anamorphic desqueeze
    distorted.x *= preset.anamorphic;

    distorted.x /= aspect;
    distorted += 0.5;
    return distorted;
}

// Fade to black at UV boundaries to prevent edge clamping artifacts
float DistortionBorderMask(float2 distortedUV)
{
    float2 fade = smoothstep(0.0, 0.02, distortedUV) * smoothstep(0.0, 0.02, 1.0 - distortedUV);
    return fade.x * fade.y;
}


//=== 6-BAND SPECTRAL CHROMATIC ABERRATION ===//
// Wyman-Sloan-Shirley (JCGT 2013) CIE approximation

static const float WAVELENGTHS[6] = { 420.0, 460.0, 520.0, 580.0, 620.0, 680.0 };

// CIE 1931 2-degree observer XYZ at each sample wavelength (normalized)
static const float3 CIE_XYZ_WEIGHTS[6] =
{
    float3(0.0529, 0.0040, 0.2819),   // 420nm: deep violet
    float3(0.0956, 0.0600, 0.5668),   // 460nm: blue
    float3(0.0633, 0.7100, 0.0782),   // 520nm: green (luminance peak)
    float3(0.9163, 0.8700, 0.0017),   // 580nm: yellow
    float3(0.8544, 0.3810, 0.0000),   // 620nm: orange
    float3(0.1501, 0.0270, 0.0000),   // 680nm: red
};

// XYZ -> linear sRGB (D65)
static const float3x3 XYZ_TO_SRGB = float3x3(
     3.2404542, -1.5371385, -0.4985314,
    -0.9692660,  1.8760108,  0.0415560,
     0.0556434, -0.2040259,  1.0572252
);

float3 XYZToLinearSRGB(float3 xyz)
{
    return mul(XYZ_TO_SRGB, xyz);
}

// Per-wavelength radial scale factors — crown glass (BK7, Abbe ~64)
static const float CA_SCALES_CROWN[6] =
{
    1.00326,   // 420nm
    1.00196,   // 460nm
    1.00000,   // 520nm (reference)
    0.99872,   // 580nm
    0.99791,   // 620nm
    0.99706,   // 680nm
};

// Per-wavelength radial scale factors — flint glass (SF11, Abbe ~25, stronger dispersion)
static const float CA_SCALES_FLINT[6] =
{
    1.01085,   // 420nm
    1.00652,   // 460nm
    1.00000,   // 520nm
    0.99576,   // 580nm
    0.99308,   // 620nm
    0.99025,   // 680nm
};

// Longitudinal CA: mip-level offset per wavelength (blue/red slightly defocused)
static const float LCA_MIP_OFFSET[6] =
{
    0.4,    // 420nm
    0.2,    // 460nm
    0.0,    // 520nm (in focus)
    0.1,    // 580nm
    0.25,   // 620nm
    0.5,    // 680nm
};

// Evaluate 6-band spectral CA.
// tex: source texture, smp: linear sampler
// uv: screen UV [0,1], center: optical axis UV
// caScales: per-wavelength radial scale array (crown or flint)
// strength: overall CA intensity, fovScale: FOV-based scale factor
float3 EvalSpectralCA(Texture2D tex, SamplerState smp,
                      float2 uv, float2 center,
                      float strength, float fovScale)
{
    float2 fromCenter = uv - center;

    float3 xyzAccum = 0.0;
    float weightSum = 0.0;

    [unroll]
    for (int i = 0; i < 6; i++)
    {
        float radialScale = lerp(1.0, CA_SCALES_CROWN[i], strength * fovScale);
        float2 sampleUV = center + fromCenter * radialScale;
        float mipLevel = LCA_MIP_OFFSET[i] * strength;

        float3 s = tex.SampleLevel(smp, sampleUV, mipLevel).rgb;
        float lum = dot(s, float3(0.2126, 0.7152, 0.0722));

        xyzAccum += lum * CIE_XYZ_WEIGHTS[i];
        weightSum += CIE_XYZ_WEIGHTS[i].y;
    }

    xyzAccum /= max(weightSum, 0.001);
    return max(XYZToLinearSRGB(xyzAccum), 0.0);
}

// Flint glass variant (stronger dispersion)
float3 EvalSpectralCA_Flint(Texture2D tex, SamplerState smp,
                            float2 uv, float2 center,
                            float strength, float fovScale)
{
    float2 fromCenter = uv - center;

    float3 xyzAccum = 0.0;
    float weightSum = 0.0;

    [unroll]
    for (int i = 0; i < 6; i++)
    {
        float radialScale = lerp(1.0, CA_SCALES_FLINT[i], strength * fovScale);
        float2 sampleUV = center + fromCenter * radialScale;
        float mipLevel = LCA_MIP_OFFSET[i] * strength;

        float3 s = tex.SampleLevel(smp, sampleUV, mipLevel).rgb;
        float lum = dot(s, float3(0.2126, 0.7152, 0.0722));

        xyzAccum += lum * CIE_XYZ_WEIGHTS[i];
        weightSum += CIE_XYZ_WEIGHTS[i].y;
    }

    xyzAccum /= max(weightSum, 0.001);
    return max(XYZToLinearSRGB(xyzAccum), 0.0);
}


//=== VEILING GLARE (bloom-mip proxy) ===//

// Fraction of incident light lost to inter-reflections and scattering.
float ComputeVeilingFraction(float coatingQuality)
{
    float R = GetSurfaceReflectance(coatingQuality);
    return 1.0 - pow(1.0 - R, 10.0); // 10-surface system
}


//=== THORIUM OXIDE LENS YELLOWING ===//

// Vintage lenses containing thorium oxide (ThO2) develop a yellow-brown tint
// over decades due to radiation-induced color center formation in glass.
// Blue light is absorbed preferentially (F-center absorption peak ~420nm).
// Common in 1960-80s lenses: Super Takumar 50/1.4, Canon FL, Minolta MC.
// yellowing: 0.0 = pristine, 1.0 = moderately aged, 3.0 = heavily yellowed.
float3 ThoriumYellowing(float3 color, float yellowing)
{
    // Spectral absorption: blue > green >> red (F-center absorption spectrum)
    float3 absorption = pow(float3(1.0, 0.97, 0.88), yellowing);
    return color * absorption;
}


#endif // SB_LENS_CORE_FXH
