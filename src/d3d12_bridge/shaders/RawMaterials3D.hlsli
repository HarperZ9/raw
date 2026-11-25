// RawMaterials3D.hlsli: Production SM 6.6 / 6.10 Energy-Conserving 3D Physical Materials Library for RAW.
// Zero external textures, zero hidden time-pulse modulation, clean-room MIT, reusable across RAW, Playground & general gamedev.
//
// Core 3D Material Evaluators (callable directly by Lighting.hlsl, LightFog.hlsl, RawTerrainWorld.hlsl):
//   - evalFrostGlacier3D(...)      : Multi-layer Snell parallax internal fractures + crystal facets + turquoise Beer-Lambert absorption (100% static!)
//   - evalRefractiveGlassGem3D(...): Cauchy 3-band RGB dispersion + Beer-Lambert thickness tint + dual-bounce internal facet caustics + Fresnel
//   - evalForgedMetal3D(...)       : Exact complex-IOR (n+ik) conductor Fresnel + Gulbrandsen edge tint + anisotropic brushed GGX + edge-wear + patina
//   - evalHeatMagma3D(...)         : Exact Planck blackbody (1000K..3400K) + Worley cooling basalt crust cracks + incandescent forge embers
//   - evalArcaneMagic3D(...)       : Belcour-Barla Airy thin-film iridescence + volumetric Snell parallax rune lattice + refractive Fresnel aura
//   - evalMaterialFromRecord3D(...): Unified 64-byte StructuredBuffer<RawGpuMaterialRecord> dispatcher across all 8 physical families
//
// Original work, MIT.
#ifndef RAW_MATERIALS_3D_HLSLI
#define RAW_MATERIALS_3D_HLSLI

#include "RawProceduralNoise.hlsli"

static const float RAW_MAT_PI = 3.14159265358979f;

// Canonical material family IDs (bit-identical to raw::materials::MaterialFamily in src/materials/RawMaterialLib.h).
static const uint RAW_MAT_FAMILY_MAGIC_ARCANE = 0u;
static const uint RAW_MAT_FAMILY_FROST_ICE    = 1u;
static const uint RAW_MAT_FAMILY_HEAT_MAGMA   = 2u;
static const uint RAW_MAT_FAMILY_FORGED_METAL = 3u;
static const uint RAW_MAT_FAMILY_GLASS_GEM    = 4u;
static const uint RAW_MAT_FAMILY_STONE_BASALT = 5u;
static const uint RAW_MAT_FAMILY_WOOD_LEATHER = 6u;
static const uint RAW_MAT_FAMILY_SNOW_BOTANIC = 7u;

// 64-byte GPU-packable physical material record (matches raw::materials::GpuMaterialRecord byte-for-byte).
struct RawGpuMaterialRecord {
    float3 albedo;          // Linear Rec.709 base reflectance / transmission tint [0, 1]
    float  roughness;       // Linear perceptual roughness [0.02, 0.98]
    float3 etaRGB;          // Real IOR at (615, 540, 465) nm (Cauchy RGB for dielectrics; measured eta for conductors)
    float  anisotropy;      // Brushed/fiber anisotropy in [-1, 1]
    float3 kOrAbsorbRGB;    // Conductor extinction k (if metallic >= 0.5) else Beer-Lambert sigma_a [1/m]
    float  metallic;        // Metallic weight [0, 1]
    float  blackbodyTempK;  // Planck blackbody emission temperature in Kelvin (0 = none, 1000..3400+)
    float  thinFilmNm;      // Optical path difference in nm for Airy thin-film interference (0 = off)
    float  clearcoatWeight; // Optical clearcoat weight [0, 1]
    uint   packedMeta;      // bits 0..7: familyId, 8..15: materialId, 16..23: coatRough_u8, 24..31: sssMfp_u8 (0..0.5m)
};

// Unified energy-conserving shading result returned by every 3D material evaluator.
struct RawMaterialEvalResult {
    float3 albedo;          // Modified surface diffuse/internal albedo [0, 1]
    float3 normal;          // World-space unit normal with 3D micro-facet/fracture relief
    float  roughness;       // Effective GGX roughness [0.02, 0.98]
    float  metallic;        // Effective metallic weight [0, 1]
    float3 F0;              // Normal-incidence spectral reflectance [0, 1]
    float3 specularAdd;     // Direct GGX + internal caustic + dispersion specular contribution
    float3 emissive;        // Self-emitted HDR radiance (blackbody / arcane)
    float3 transmittance;   // Remaining optical transmittance through upper reflection/medium [0, 1]
    float  clearcoat;       // Clearcoat weight [0, 1]
    float  clearcoatRough;  // Clearcoat roughness [0.02, 0.35]
};

// ============================================================================
// Shared Microfacet & Optical Helpers (energy-conserving, NaN-free, static)
// ============================================================================

void rawBuildOrthonormalBasis(float3 N, out float3 T, out float3 B) {
    const float3 nUnit = (dot(N, N) > 1.0e-8f) ? normalize(N) : float3(0.0f, 0.0f, 1.0f);
    const float3 upRef = (abs(nUnit.z) < 0.92f) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    T = normalize(cross(upRef, nUnit));
    B = cross(nUnit, T);
}

// NaN-safe half-vector helper: falls back to surface normal N when V and L are diametrically opposed (V == -L).
float3 rawSafeHalfVec(float3 V, float3 L, float3 N) {
    const float3 sumVL = V + L;
    const float len2 = dot(sumVL, sumVL);
    if (len2 > 1.0e-6f) { return sumVL * rsqrt(len2); }
    return (dot(N, N) > 1.0e-8f) ? normalize(N) : float3(0.0f, 0.0f, 1.0f);
}

// Cauchy 3-band dispersion anchored at green 540 nm: n(lambda) = A + B / lambda_um^2.
// Guarantees strict normal dispersion n_B > n_G > n_R whenever cauchyB > 0.
float3 rawCauchyIorRGB(float baseIor540, float cauchyB) {
    const float b = max(cauchyB, 0.0f);
    const float invG2 = 1.0f / (0.540f * 0.540f);
    const float A = max(baseIor540, 1.0f) - b * invG2;
    const float3 lamUm = float3(0.615f, 0.540f, 0.465f);
    return max(A + b / (lamUm * lamUm), 1.0f.xxx);
}

// Belcour-Barla Airy thin-film spectral interference at Rec.709 RGB wavelengths (615, 540, 465 nm).
float3 rawAiryThinFilmRGB(float cosTheta, float opdNm) {
    if (opdNm <= 1.0f) { return 1.0f.xxx; }
    const float c = saturate(cosTheta);
    const float sin2 = max(1.0f - c * c, 0.0f);
    const float effOpd = opdNm * sqrt(max(1.0f - sin2 * 0.56f, 0.12f));
    return 0.55f + 0.45f * cos(6.2831853f * effOpd / float3(615.0f, 540.0f, 465.0f));
}

// Isotropic GGX + height-correlated Smith visibility + Kulla-Conty energy compensation.
float rawGgxSpecTerm(float nh, float nv, float nl, float alpha, float f0Scalar) {
    const float nlSafe = max(nl, 0.0f);
    if (nlSafe <= 1.0e-5f) { return 0.0f; }
    const float nvSafe = max(nv, 1.0e-4f);
    const float a = clamp(alpha, 0.02f, 0.98f);
    const float a2 = a * a;
    const float dDen = max(nh * nh * (a2 - 1.0f) + 1.0f, 1.0e-4f);
    const float D = a2 / max(RAW_MAT_PI * dDen * dDen, 1.0e-5f);
    const float visV = nlSafe * sqrt(nvSafe * nvSafe * (1.0f - a2) + a2);
    const float visL = nvSafe * sqrt(nlSafe * nlSafe * (1.0f - a2) + a2);
    const float V = 0.5f / max(visV + visL, 1.0e-5f);
    const float ess = saturate(1.0f - a * (0.42f * (1.0f - nvSafe) + 0.14f));
    const float kulla = 1.0f + f0Scalar * ((1.0f - ess) / max(ess, 0.25f));
    return min(D * V * kulla * nlSafe * RAW_MAT_PI, 8.0f);
}

// Wave 44 Physical Brushed-Metal Anisotropic GGX Specular BRDF (Burley/Heitz correlated Smith V_aniso
// + world-locked micro-brushing grain along reconstructed tangent T + Kulla-Conty energy compensation).
// Returns energy-conserving specular reflectance * NdotL (irradiance-over-pi convention matching Lighting.hlsl).
float3 evalBrushedMetalAniso(
    float3 worldPosAbs,
    float3 N,
    float3 V,
    float3 L,
    float3 F0,
    float  roughness,
    float  anisotropy)
{
    const float3 nUnit = (dot(N, N) > 1.0e-8f) ? normalize(N) : float3(0.0f, 0.0f, 1.0f);
    const float3 vUnit = (dot(V, V) > 1.0e-8f) ? normalize(V) : nUnit;
    const float3 lUnit = (dot(L, L) > 1.0e-8f) ? normalize(L) : nUnit;
    if (dot(nUnit, lUnit) <= 1.0e-5f) { return 0.0f.xxx; }

    float3 T, B;
    rawBuildOrthonormalBasis(nUnit, T, B);

    // World-locked forged brushed-steel micro-grooves along reconstructed armor/blade tangent (100% static):
    const float uBrush = dot(worldPosAbs, T) * 0.20f;
    const float vBrush = dot(worldPosAbs, B) * 1.55f;
    const float wBrush = dot(worldPosAbs, nUnit) * 0.25f;
    const float4 grainG = rawNoise3Grad(float3(uBrush, vBrush, wBrush) + float3(3.7f, 11.3f, 5.9f));
    const float3 nBrushed = normalize(nUnit + T * (grainG.y * 0.045f) + B * (grainG.z * 0.095f));
    // Re-orthogonalize tangent basis around perturbed brushed normal:
    const float3 tB = normalize(T - nBrushed * dot(T, nBrushed));
    const float3 bB = cross(nBrushed, tB);

    const float nv = max(dot(nBrushed, vUnit), 1.0e-4f);
    const float nl = max(dot(nBrushed, lUnit), 0.0f);
    if (nl <= 1.0e-5f) { return 0.0f.xxx; }

    const float3 H = rawSafeHalfVec(vUnit, lUnit, nBrushed);
    const float nh = saturate(dot(nBrushed, H));
    const float vh = saturate(dot(vUnit, H));

    // Burley / Heitz anisotropic GGX aspect mapping (authentic elongated highlights along brushed curves):
    const float r = clamp(roughness, 0.05f, 0.96f);
    const float aniso = clamp(anisotropy, -0.85f, 0.85f);
    const float aspect = sqrt(max(1.0f - abs(aniso) * 0.90f, 0.12f));
    const float aBase = r * r;
    const float ax = max((aniso >= 0.0f) ? (aBase / aspect) : (aBase * aspect), 0.012f);
    const float ay = max((aniso >= 0.0f) ? (aBase * aspect) : (aBase / aspect), 0.012f);

    const float th = dot(tB, H);
    const float bh = dot(bB, H);
    const float dDen = max((th * th) / (ax * ax) + (bh * bh) / (ay * ay) + nh * nh, 1.0e-5f);
    const float D = 1.0f / max(RAW_MAT_PI * ax * ay * dDen * dDen, 1.0e-5f);

    // Height-correlated anisotropic Smith masking-shadowing visibility V_aniso (Heitz 2014):
    const float tv = dot(tB, vUnit), bv = dot(bB, vUnit);
    const float tl = dot(tB, lUnit), bl = dot(bB, lUnit);
    const float lamV = nl * sqrt(ax * ax * tv * tv + ay * ay * bv * bv + nv * nv);
    const float lamL = nv * sqrt(ax * ax * tl * tl + ay * ay * bl * bl + nl * nl);
    const float visAniso = 0.5f / max(lamV + lamL, 1.0e-5f);

    // Schlick-Gulbrandsen F82-tint conductor Fresnel + Kulla-Conty multi-scattering compensation:
    const float f1 = pow(1.0f - vh, 5.0f);
    const float3 F0s = saturate(F0);
    const float3 F = saturate(lerp(F0s, 1.0f.xxx, f1) - F0s * (0.12f * vh * f1 * (1.0f - vh)));
    const float ess = saturate(1.0f - r * (0.40f * (1.0f - nv) + 0.12f));
    const float3 kulla = 1.0f + F0s * ((1.0f - ess) / max(ess, 0.25f));

    return min(F * kulla * (RAW_MAT_PI * D * visAniso * nl), 8.0f.xxx);
}

// EON Energy-Preserving Oren-Nayar Rough Diffuse BRDF (Portsmouth, Kutz, Hill 2025, JCGT — MIT clean-room).
// Exact white-furnace energy conservation at rough=1.0 for stone, basalt, timber, snow, and matte patina.
float3 evalEonDiffuse3D(float3 rho, float rough, float3 N, float3 V, float3 L) {
    const float nDotL = saturate(dot(N, L));
    const float nDotV = saturate(dot(N, V));
    const float lDotV = dot(L, V);
    const float s = lDotV - nDotL * nDotV;
    const float invT = (s > 0.0f) ? (1.0f / max(max(nDotL, nDotV), 1.0e-4f)) : 1.0f;
    const float r = saturate(rough);
    const float AF = 1.0f / (1.0f + (0.5f - 2.0f / (3.0f * RAW_MAT_PI)) * r);
    const float3 fSS = (rho / RAW_MAT_PI) * AF * (1.0f + r * s * invT);
    const float E_l = AF * (1.0f + r * (0.1808f * (1.0f - nDotL)));
    const float E_v = AF * (1.0f + r * (0.1808f * (1.0f - nDotV)));
    const float E_avg = AF * (1.0f + r * 0.0904f);
    const float3 fMS = (rho * rho / max(1.0f - rho * (1.0f - E_avg), 1.0e-4f))
                     * max(1.0f - E_l, 0.0f) * max(1.0f - E_v, 0.0f)
                     / (RAW_MAT_PI * max(1.0f - E_avg, 1.0e-4f));
    return max(fSS + fMS, 0.0f.xxx);
}

// ============================================================================
// 1. FROST & GLACIAL ICE (100% STATIC / ZERO-TIME-PULSE, ZERO VORONOI CELLS!)
//    Wave 47 3-layer refracted parallax anisotropic planar stress fractures
//    along refract(-V, N, 1.0/1.309) at 0.08m / 0.24m / 0.52m attenuated by
//    Beer-Lambert glacial transmittance (shallow frost-white -> deep sapphire-
//    teal) + trapped micro-air-bubble glints in top 0.15m + sub-zero hoarfrost
//    grazing ridge rim, preserving 75%+ of authored glacial ice wall relief.
// ============================================================================
RawMaterialEvalResult evalFrostGlacier3D(
    float3 worldPos,
    float3 N,
    float3 V,
    float3 L,
    float3 lightCol,
    float3 baseAlbedo,
    float  thicknessU,
    float  snowDust)
{
    RawMaterialEvalResult res;
    const float3 nUnit = (dot(N, N) > 1.0e-8f) ? normalize(N) : float3(0.0f, 0.0f, 1.0f);
    const float3 vUnit = (dot(V, V) > 1.0e-8f) ? normalize(V) : nUnit;
    const float3 lUnit = (dot(L, L) > 1.0e-8f) ? normalize(L) : nUnit;
    const float nv = max(dot(nUnit, vUnit), 1.0e-4f);
    const float effThick = clamp(thicknessU, 2.0f, 120.0f);
    const float thickScale = clamp(effThick / 42.0f, 0.45f, 1.45f);

    // Exact Snell refraction into crystalline glacial ice (IOR 1.309, 100% static — ZERO time modulation!).
    const float kIceIor = 1.309f;
    const float3 refrV = refract(-vUnit, nUnit, 1.0f / kIceIor);
    const float3 rayDir = (dot(refrV, refrV) > 1.0e-4f) ? normalize(refrV) : -vUnit;

    // Beer-Lambert glacial cyan-blue optical absorption (red dies fastest -> shallow white, deep sapphire-teal):
    const float3 sigmaIce = float3(0.42f, 0.11f, 0.045f);
    const float invNvOptical = 1.0f / max(nv, 0.25f);

    // 3-Layer Refracted Parallax Anisotropic Planar Stress Fractures at depths 0.08m, 0.24m, 0.52m
    // (ZERO Worley/Voronoi polygons: pure rotated domain-warped planar ridge-line shear fractures!).
    const float4 macroWarp = rawNoise3Grad(worldPos * 0.011f + float3(5.7f, 13.1f, 2.9f));
    const float3 warpOff = (macroWarp.yzw - 0.5f.xxx) * 14.0f;

    // Depth slice 0 (0.08m = ~5.6u): shallow crisp frost-white surface stress cracks
    const float3 pS0 = worldPos + rayDir * (0.08f * 70.0f * thickScale) + warpOff * 0.45f;
    const float3 qS0 = float3(
        pS0.x * 0.036f + pS0.y * 0.015f - pS0.z * 0.022f,
        -pS0.x * 0.008f + pS0.y * 0.012f + pS0.z * 0.044f,
        pS0.x * 0.011f - pS0.y * 0.038f + pS0.z * 0.010f) + float3(3.1f, 7.4f, 1.9f);
    const float4 nS0 = rawNoise3Grad(qS0 + macroWarp.yzw * 0.55f);
    const float zone0 = smoothstep(0.54f, 0.84f, rawNoise3Grad(pS0 * 0.0095f + float3(29.3f, 11.7f, 19.1f)).x);
    const float crack0 = (1.0f - smoothstep(0.0f, 0.095f, abs(nS0.x * 2.0f - 1.0f))) * zone0;
    const float3 trans0 = exp(-sigmaIce * (0.08f * (1.0f + invNvOptical)));

    // Depth slice 1 (0.24m = ~16.8u): mid-depth refracted icy-cyan oblique cleavage planes
    const float3 pS1 = worldPos + rayDir * (0.24f * 70.0f * thickScale) + warpOff * 0.90f;
    const float3 qS1 = float3(
        pS1.x * 0.014f - pS1.y * 0.034f + pS1.z * 0.018f,
        pS1.x * 0.032f + pS1.y * 0.010f + pS1.z * 0.026f,
        -pS1.x * 0.012f + pS1.y * 0.015f + pS1.z * 0.048f) + float3(17.3f, 5.2f, 9.7f);
    const float4 nS1 = rawNoise3Grad(qS1 + nS0.yzw * 0.65f);
    const float zone1 = smoothstep(0.50f, 0.82f, rawNoise3Grad(pS1 * 0.0082f + float3(7.1f, 33.4f, 13.9f)).x);
    const float crack1 = (1.0f - smoothstep(0.0f, 0.135f, abs(nS1.x * 2.0f - 1.0f))) * zone1;
    const float3 trans1 = exp(-sigmaIce * (0.24f * (1.0f + invNvOptical)));

    // Depth slice 2 (0.52m = ~36.4u): deep internal sapphire-teal pressure fracture sheets
    const float3 pS2 = worldPos + rayDir * (0.52f * 70.0f * thickScale) + warpOff * 1.35f;
    const float3 qS2 = float3(
        -pS2.x * 0.024f + pS2.y * 0.022f + pS2.z * 0.014f,
        pS2.x * 0.016f + pS2.y * 0.028f - pS2.z * 0.032f,
        pS2.x * 0.028f - pS2.y * 0.009f + pS2.z * 0.020f) + float3(41.7f, 19.3f, 27.1f);
    const float4 nS2 = rawNoise3Grad(qS2 + nS1.yzw * 0.55f);
    const float zone2 = smoothstep(0.46f, 0.80f, rawNoise3Grad(pS2 * 0.0070f + float3(23.9f, 4.7f, 37.3f)).x);
    const float crack2 = (1.0f - smoothstep(0.0f, 0.180f, abs(nS2.x * 2.0f - 1.0f))) * zone2;
    const float3 trans2 = exp(-sigmaIce * (0.52f * (1.0f + invNvOptical)));

    // Smooth stratified glacial compression bands (horizontal-biased blue-teal layers):
    const float4 strata1 = rawNoise3Grad(float3(pS1.xy * 0.015f, pS1.z * 0.046f) + float3(3.1f, 7.4f, 1.9f));
    const float4 strata2 = rawNoise3Grad(float3(pS2.xy * 0.030f, pS2.z * 0.082f) + float3(17.3f, 5.2f, 9.7f));
    const float strataBand = saturate(strata1.x * 0.64f + strata2.x * 0.36f);

    // Smooth glacial micro-relief normal that preserves authored wall sculpting + shallow crack edge glint:
    const float4 facetGrad = rawNoise3Grad(worldPos * 0.085f + float3(11.1f, 3.7f, 23.3f));
    const float3 rawPert = facetGrad.yzw * 0.072f + strata1.yzw * 0.040f + nS0.yzw * (crack0 * 0.085f);
    const float3 tangPert = rawPert - nUnit * dot(rawPert, nUnit);
    const float3 nIce = normalize(nUnit + tangPert);

    // Deep Beer-Lambert glacial body + layered refracted fracture radiance (shallow frost-white -> deep sapphire-teal):
    const float pathMeters = (effThick * 0.014f) * invNvOptical;
    const float3 transDeep = exp(-sigmaIce * pathMeters);
    const float3 deepArcticCore = lerp(float3(0.032f, 0.135f, 0.275f), float3(0.082f, 0.305f, 0.440f), strataBand) * transDeep;
    const float3 fractureScatter = float3(0.90f, 0.96f, 1.00f) * (crack0 * 0.32f) * trans0
                                 + float3(0.42f, 0.84f, 0.96f) * (crack1 * 0.26f) * trans1
                                 + float3(0.14f, 0.56f, 0.82f) * (crack2 * 0.22f) * trans2;

    // Subsurface forward-transmitted cobalt-turquoise glacial scatter when backlit by sun/torch:
    const float backLit = pow(saturate(dot(-vUnit, lUnit)), 3.0f);
    const float3 sssGlow = lightCol * float3(0.10f, 0.38f, 0.56f) * (backLit * (0.35f + 0.65f * transDeep.b) * 0.38f);

    // Trapped Micro-Air-Bubble Glints inside top 0.15m ice layer (100% static world-locked, view-dependent specular):
    const float dust = saturate(snowDust);
    const float3 H = rawSafeHalfVec(vUnit, lUnit, nIce);
    const float3 pBubA = (worldPos + rayDir * (0.06f * 70.0f * thickScale)) * 0.82f;
    const float3 pBubB = (worldPos + rayDir * (0.14f * 70.0f * thickScale)) * 0.58f + float3(19.3f, 7.1f, 31.7f);
    const float3 hBubA = rawHash33(floor(pBubA));
    const float3 hBubB = rawHash33(floor(pBubB));
    const float dBubA  = length(frac(pBubA) - (0.20f + 0.60f * hBubA.yzx));
    const float dBubB  = length(frac(pBubB) - (0.20f + 0.60f * hBubB.zxy));
    const float3 nBubA = normalize(nIce + (hBubA - 0.5f.xxx) * 1.35f);
    const float3 nBubB = normalize(nIce + (hBubB - 0.5f.xxx) * 1.45f);
    const float glintBubA = (hBubA.x > 0.82f && dBubA < 0.30f)
        ? pow(saturate(dot(nBubA, H)), 145.0f) * smoothstep(0.30f, 0.06f, dBubA) * 0.85f : 0.0f;
    const float glintBubB = (hBubB.x > 0.84f && dBubB < 0.32f)
        ? pow(saturate(dot(nBubB, H)), 120.0f) * smoothstep(0.32f, 0.08f, dBubB) * 0.62f : 0.0f;
    const float3 bubbleGlint = (glintBubA * exp(-sigmaIce * 0.06f) + glintBubB * exp(-sigmaIce * 0.14f))
                             * (1.0f - dust * 0.70f);

    // Sub-Zero Hoarfrost Edge Rim along grazing ridges & cold crevices + static hoarfrost crystal sparkles:
    const float grazeRim = pow(1.0f - nv, 2.8f);
    const float hoarCrevice = smoothstep(0.46f, 0.84f, facetGrad.x) * (0.26f + 0.74f * dust);
    const float hoarEdge = saturate(grazeRim * (0.24f + 0.76f * dust) * (0.45f + 0.55f * facetGrad.x) + hoarCrevice * 0.24f);
    float flakeGlint = 0.0f;
    if (dust > 0.04f || hoarEdge > 0.14f) {
        const float3 frostCell = floor(worldPos * 0.65f);
        const float3 frostHash = rawHash33(frostCell);
        const float3 flakeN = normalize(nIce + (frostHash - 0.5f) * 1.05f);
        flakeGlint = (frostHash.x > 0.80f) ? pow(saturate(dot(flakeN, H)), 180.0f) * max(dust, hoarEdge * 0.55f) * 0.62f : 0.0f;
    }

    // Crisp dielectric Fresnel specular glaze at exact ice IOR 1.309 (F0 = 0.018):
    const float vh = saturate(dot(vUnit, H));
    const float nh = saturate(dot(nIce, H));
    const float Fdie = rawDielectricFresnel(vh, kIceIor);
    const float crackRough = saturate(crack0 * 0.75f + crack1 * 0.30f);
    const float glazeRough = clamp(lerp(lerp(0.062f, 0.20f, crackRough), 0.58f, max(dust, hoarEdge * 0.52f)), 0.05f, 0.82f);
    const float specLobe = rawGgxSpecTerm(nh, max(dot(nIce, vUnit), 1.0e-4f), saturate(dot(nIce, lUnit)), glazeRough, 0.018f);

    // Preserve 75%+ of Skyrim's authored glacial ice wall relief & luminance while enriching deep sapphire-teal optics:
    const float baseLum = max(dot(baseAlbedo, float3(0.2126f, 0.7152f, 0.0722f)), 0.08f);
    const float3 absorbedBase = baseAlbedo * lerp(float3(0.90f, 0.98f, 1.06f), float3(0.74f, 0.96f, 1.12f), (1.0f - transDeep.r) * 0.55f);
    const float3 iceBody = absorbedBase * 0.76f
                         + (deepArcticCore * clamp(baseLum * 1.45f, 0.32f, 1.25f)) * 0.24f
                         + fractureScatter * clamp(baseLum * 1.35f + 0.22f, 0.30f, 1.20f);

    const float frostBlend = saturate(max(dust * 0.55f, hoarEdge * 0.34f));
    res.albedo = saturate(lerp(iceBody, float3(0.76f, 0.85f, 0.93f), frostBlend));
    res.normal = nIce;
    res.roughness = glazeRough;
    res.metallic = 0.0f;
    res.F0 = 0.018f.xxx;
    res.specularAdd = min(lightCol * (Fdie * specLobe * 0.38f + flakeGlint.xxx + bubbleGlint * 0.55f) + sssGlow, 3.0f.xxx);
    res.emissive = 0.0f.xxx;
    res.transmittance = saturate(transDeep * (1.0f - Fdie));
    res.clearcoat = saturate((1.0f - max(dust, hoarEdge * 0.65f)) * 0.74f);
    res.clearcoatRough = 0.048f;
    return res;
}

// ============================================================================
// 2. TRUE REFRACTIVE GLASS, AMBER & GEMSTONES
//    Cauchy 3-band RGB dispersion (crown glass 1.52, quartz/amber 1.54,
//    sapphire/ruby 1.77, diamond 2.42) + Beer-Lambert thickness absorption +
//    dual-bounce internal facet reflection & prismatic caustics + Fresnel.
// ============================================================================
RawMaterialEvalResult evalRefractiveGlassGem3D(
    float3 worldPos,
    float3 N,
    float3 V,
    float3 L,
    float3 lightCol,
    float3 envCol,
    float  baseIor,
    float  cauchyB,
    float3 absorbSigma,
    float  thicknessU,
    float  facetScale)
{
    RawMaterialEvalResult res;
    const float3 nUnit = normalize(N);
    const float3 vUnit = normalize(V);
    const float3 lUnit = normalize(L);
    const float effIor = clamp(baseIor, 1.05f, 2.65f);
    const float3 iorRGB = rawCauchyIorRGB(effIor, cauchyB);

    // Optional Voronoi crystal facet normal sculpting (facetScale == 0 -> smooth optical glass):
    float3 nGem = nUnit;
    float facetEdge = 1.0f;
    if (facetScale > 0.001f) {
        const float4 wFac = rawWorley3D(worldPos * facetScale);
        const float3 cellTilt = (rawHash33(floor(worldPos * facetScale)) - 0.5f) * 0.38f;
        facetEdge = smoothstep(0.02f, 0.14f, wFac.y);
        nGem = normalize(nUnit + cellTilt * facetEdge);
    }

    const float nv = max(dot(nGem, vUnit), 1.0e-4f);
    const float nl = max(dot(nGem, lUnit), 0.0f);
    const float3 H = rawSafeHalfVec(vUnit, lUnit, nGem);
    const float vh = saturate(dot(vUnit, H));
    const float nh = saturate(dot(nGem, H));

    // Exact per-band Fresnel reflectance at viewing angle & half-vector:
    const float3 Fview = float3(
        rawDielectricFresnel(nv, iorRGB.r),
        rawDielectricFresnel(nv, iorRGB.g),
        rawDielectricFresnel(nv, iorRGB.b));
    const float3 Fhalf = float3(
        rawDielectricFresnel(vh, iorRGB.r),
        rawDielectricFresnel(vh, iorRGB.g),
        rawDielectricFresnel(vh, iorRGB.b));
    const float3 F0rgb = pow((iorRGB - 1.0f) / (iorRGB + 1.0f), 2.0f);

    // 3-band Cauchy Snell refraction vectors into the crystal/glass volume:
    const float3 refrR = refract(-vUnit, nGem, 1.0f / iorRGB.r);
    const float3 refrG = refract(-vUnit, nGem, 1.0f / iorRGB.g);
    const float3 refrB = refract(-vUnit, nGem, 1.0f / iorRGB.b);

    // Beer-Lambert optical thickness absorption through the medium (dual-bounce path):
    const float pathM = clamp(thicknessU, 0.5f, 80.0f) * 0.02f / max(nv, 0.25f);
    const float3 transSingle = exp(-max(absorbSigma, 0.0f.xxx) * pathM);
    const float3 transDual   = transSingle * transSingle;

    // Dual-bounce internal facet reflection / prismatic "fire" caustics:
    // Reflect each refracted RGB ray off a synthetic back-wall facet normal and test alignment with L:
    const float3 backN = normalize(-nGem + (facetScale > 0.001f
        ? (rawHash33(floor((worldPos + refrG * thicknessU * 0.35f) * max(facetScale, 0.05f))) - 0.5f) * 0.75f
        : 0.0f.xxx));
    const float3 b2R = reflect(refrR, backN);
    const float3 b2G = reflect(refrG, backN);
    const float3 b2B = reflect(refrB, backN);

    // Critical-angle Total Internal Reflection boost (high-IOR diamond/sapphire trap and focus light intensely!):
    const float sinCrit2 = 1.0f / (effIor * effIor);
    const float tirBoost = saturate(1.0f - sinCrit2) * 1.45f;
    const float3 fireAlign = float3(
        pow(saturate(dot(normalize(b2R), lUnit)), 24.0f),
        pow(saturate(dot(normalize(b2G), lUnit)), 24.0f),
        pow(saturate(dot(normalize(b2B), lUnit)), 24.0f));
    const float3 internalFire = lightCol * fireAlign * transDual * (1.0f - Fview) * (0.45f + tirBoost);

    // Crisp outer specular highlight + energy-conserving Fresnel environment reflection:
    const float glassRough = (facetScale > 0.001f) ? lerp(0.035f, 0.14f, 1.0f - facetEdge) : 0.03f;
    const float specLobe = rawGgxSpecTerm(nh, nv, nl, glassRough, F0rgb.g);
    const float3 outerSpec = lightCol * Fhalf * specLobe * 0.45f + envCol * Fview;

    res.albedo = saturate(transSingle * (1.0f - Fview));
    res.normal = nGem;
    res.roughness = glassRough;
    res.metallic = 0.0f;
    res.F0 = saturate(F0rgb);
    res.specularAdd = min(outerSpec + internalFire, 4.5f.xxx);
    res.emissive = 0.0f.xxx;
    res.transmittance = saturate(transSingle * (1.0f - Fview));
    res.clearcoat = 0.0f;
    res.clearcoatRough = 0.03f;
    return res;
}

// ============================================================================
// 3. FORGED METALS (Measured Complex-IOR n + ik)
//    Exact complex-IOR conductor Fresnel + Gulbrandsen grazing response +
//    anisotropic brushed GGX + hammer-forge micro-relief + edge-wear + patina.
// ============================================================================
RawMaterialEvalResult evalForgedMetal3D(
    float3 worldPos,
    float3 N,
    float3 V,
    float3 L,
    float3 lightCol,
    float3 envCol,
    float3 etaRGB,
    float3 kRGB,
    float  roughness,
    float  anisotropy,
    float  edgeWear,
    float  patinaAmt)
{
    RawMaterialEvalResult res;
    const float3 nUnit = normalize(N);
    const float3 vUnit = normalize(V);
    const float3 lUnit = normalize(L);

    float3 T, B;
    rawBuildOrthonormalBasis(nUnit, T, B);

    // Anisotropic brushed metal micro-grooves + forged hammer dimples (100% static world-space):
    const float brushCoordT = dot(worldPos, T) * 0.18f;
    const float brushCoordB = dot(worldPos, B) * 1.45f;
    const float4 brushGrad = rawNoise3Grad(float3(brushCoordT, brushCoordB, dot(worldPos, nUnit) * 0.25f));
    const float4 hammerGrad = rawNoise3Grad(worldPos * 0.075f + float3(5.3f, 11.7f, 2.9f));
    const float3 nMetal = normalize(nUnit + (T * (brushGrad.y * 0.06f) + B * (brushGrad.z * 0.12f) + hammerGrad.yzw * 0.05f));

    const float nv = max(dot(nMetal, vUnit), 1.0e-4f);
    const float nl = max(dot(nMetal, lUnit), 0.0f);
    const float3 H = rawSafeHalfVec(vUnit, lUnit, nMetal);
    const float nh = saturate(dot(nMetal, H));
    const float vh = saturate(dot(vUnit, H));

    // Edge-wear clears patina & polishes exposed bevels; patina settles in dimples/crevices:
    const float wear = saturate(edgeWear);
    const float patNoise = rawFbm3Grad(worldPos * 0.095f + float3(13.2f, 4.7f, 8.1f), 3).x;
    const float patinaMask = saturate(patinaAmt * smoothstep(0.38f, 0.74f, patNoise) * (1.0f - wear * 0.85f));
    const float effMetallic = saturate(1.0f - patinaMask * 0.92f);
    const float baseRough = clamp(lerp(roughness, max(roughness * 0.55f, 0.08f), wear * 0.75f), 0.04f, 0.92f);
    const float effRough = lerp(baseRough, 0.78f, patinaMask);

    // Exact complex-IOR conductor Fresnel F(cosTheta, eta + ik):
    const float3 F0cond = rawComplexConductorFresnel(1.0f, etaRGB, kRGB);
    const float3 FcondH = rawComplexConductorFresnel(vh, etaRGB, kRGB);
    const float3 FcondV = rawComplexConductorFresnel(nv, etaRGB, kRGB);

    // Anisotropic GGX microfacet distribution (Burley/Heitz parameterization):
    const float aniso = clamp(anisotropy, -0.92f, 0.92f) * (1.0f - patinaMask);
    const float aspect = sqrt(max(1.0f - abs(aniso) * 0.9f, 0.10f));
    const float aBase = effRough * effRough;
    const float ax = max((aniso >= 0.0f) ? (aBase / aspect) : (aBase * aspect), 0.015f);
    const float ay = max((aniso >= 0.0f) ? (aBase * aspect) : (aBase / aspect), 0.015f);
    const float th = dot(T, H);
    const float bh = dot(B, H);
    const float dDen = (th * th) / (ax * ax) + (bh * bh) / (ay * ay) + nh * nh;
    const float Daniso = 1.0f / max(RAW_MAT_PI * ax * ay * dDen * dDen, 1.0e-5f);

    // Anisotropic height-correlated Smith visibility:
    const float tv = dot(T, vUnit), bv = dot(B, vUnit);
    const float tl = dot(T, lUnit), bl = dot(B, lUnit);
    const float lamV = nl * sqrt(ax * ax * tv * tv + ay * ay * bv * bv + nv * nv);
    const float lamL = nv * sqrt(ax * ax * tl * tl + ay * ay * bl * bl + nl * nl);
    const float Vaniso = 0.5f / max(lamV + lamL, 1.0e-5f);

    // Kulla-Conty multi-scatter compensation for rough conductors:
    const float ess = saturate(1.0f - effRough * (0.40f * (1.0f - nv) + 0.12f));
    const float3 kulla = 1.0f + F0cond * ((1.0f - ess) / max(ess, 0.25f));

    // Blend conductor reflectance with dielectric patina (oxidized rust / copper verdigris):
    const float3 patinaAlbedo = (F0cond.r > F0cond.b * 1.25f)
        ? float3(0.14f, 0.30f, 0.24f)   // verdigris patina on bronze/brass/gold alloys
        : float3(0.18f, 0.11f, 0.08f);  // iron-oxide rust patina on iron/steel
    const float3 Fmetal = lerp(0.04f.xxx, FcondH, effMetallic);
    const float3 Fenv   = lerp(0.04f.xxx, FcondV, effMetallic);
    const float3 directSpec = lightCol * Fmetal * kulla * min(Daniso * Vaniso * nl * RAW_MAT_PI, 8.0f);
    const float3 envSpec = envCol * Fenv * lerp(0.85f, 0.35f, effRough);

    res.albedo = lerp(F0cond * 0.12f, patinaAlbedo, patinaMask);
    res.normal = nMetal;
    res.roughness = effRough;
    res.metallic = effMetallic;
    res.F0 = lerp(0.04f.xxx, F0cond, effMetallic);
    res.specularAdd = min(directSpec + envSpec, 4.5f.xxx);
    res.emissive = 0.0f.xxx;
    res.transmittance = saturate(1.0f - Fenv) * (1.0f - effMetallic);
    res.clearcoat = 0.0f;
    res.clearcoatRough = 0.12f;
    return res;
}

// ============================================================================
// 4. HEAT, MAGMA & FORGE EMBERS (1000K..3400K Planck Blackbody)
//    Exact Planck blackbody radiator + Worley polygonal cooling basalt crust +
//    curl-warped incandescent fracture fissures + dark vesicular slag relief.
// ============================================================================
RawMaterialEvalResult evalHeatMagma3D(
    float3 worldPos,
    float3 N,
    float3 V,
    float3 L,
    float3 lightCol,
    float  coreTempK,
    float  crustAmount,
    float  flowPhase)
{
    RawMaterialEvalResult res;
    const float3 nUnit = normalize(N);
    const float3 vUnit = normalize(V);
    const float3 lUnit = normalize(L);

    // Curl-warped magma flow domain (pass flowPhase=0 for static forge coals / cooled fissures):
    const float3 curlOff = rawCurlNoise3D(worldPos * 0.024f + float3(0.0f, 0.0f, flowPhase * 0.12f)) * 4.2f;
    const float3 pFlow = worldPos + curlOff;

    // Polygonal Worley basalt cooling crust plates + fine secondary thermal fissures:
    const float4 wPlates = rawWorley3D(pFlow * 0.045f);
    const float4 wMicro  = rawWorley3D(pFlow * 0.145f + float3(9.1f, 3.7f, 15.2f));
    const float4 vesic   = rawFbm3Grad(worldPos * 0.22f, 3);

    const float crust = saturate(crustAmount);
    const float crackWidth = lerp(0.42f, 0.07f, crust);
    const float primaryCrack = 1.0f - smoothstep(0.015f, crackWidth, wPlates.y);
    const float secondaryCrack = (1.0f - smoothstep(0.01f, crackWidth * 0.65f, wMicro.y)) * 0.55f;
    const float heatMask = saturate(max(primaryCrack, secondaryCrack * (1.0f - crust * 0.45f)));

    // Local blackbody temperature field: core cracks reach coreTempK (1000..3400K),
    // crust shoulders cool down through 1100K cherry-red into 450K dark basalt slag.
    const float maxT = clamp(coreTempK, 900.0f, 3800.0f);
    const float localTempK = lerp(450.0f, maxT, pow(heatMask, 0.72f));

    float hdrRad = 0.0f;
    const float3 bbColor = rawPlanckBlackbodyRGB(localTempK, hdrRad);

    // Crisp cooling basalt plate normal with sunken glowing seams + vesicular basalt micro-roughness:
    const float3 nMagma = normalize(nUnit + vesic.yzw * (0.28f * (1.0f - heatMask * 0.75f)));
    const float nv = max(dot(nMagma, vUnit), 1.0e-4f);
    const float nl = max(dot(nMagma, lUnit), 0.0f);
    const float3 H = rawSafeHalfVec(vUnit, lUnit, nMagma);
    const float nh = saturate(dot(nMagma, H));
    const float vh = saturate(dot(vUnit, H));

    // Cooled basalt crust is dark matte obsidian-basalt; molten magma surface is glossy viscous silicate:
    const float effRough = lerp(0.84f, 0.22f, heatMask);
    const float Fdie = rawDielectricFresnel(vh, 1.54f);
    const float specLobe = rawGgxSpecTerm(nh, nv, nl, effRough, 0.045f);

    res.albedo = lerp(float3(0.045f, 0.042f, 0.046f) * (0.75f + 0.50f * vesic.x), float3(0.09f, 0.03f, 0.01f), heatMask);
    res.normal = nMagma;
    res.roughness = effRough;
    res.metallic = 0.0f;
    res.F0 = 0.045f.xxx;
    res.specularAdd = min(lightCol * Fdie * specLobe * 0.28f, 2.5f.xxx);
    res.emissive = min(bbColor * hdrRad * (0.65f + 0.35f * heatMask), 14.0f.xxx);
    res.transmittance = saturate(1.0f - Fdie).xxx;
    res.clearcoat = heatMask * 0.35f;
    res.clearcoatRough = 0.14f;
    return res;
}

// ============================================================================
// 5. MAGIC & ARCANE (Thin-Film Iridescence + Volumetric Rune Depth + Aura)
//    Belcour-Barla Airy interference + 3-layer Snell parallax rune crystal
//    lattice + refractive Fresnel aura (100% static when phaseOffset == 0).
// ============================================================================
RawMaterialEvalResult evalArcaneMagic3D(
    float3 worldPos,
    float3 N,
    float3 V,
    float3 L,
    float3 lightCol,
    float3 arcaneTint,
    float  thinFilmNm,
    float  runeDepth,
    float  phaseOffset)
{
    RawMaterialEvalResult res;
    const float3 nUnit = normalize(N);
    const float3 vUnit = normalize(V);
    const float3 lUnit = normalize(L);
    const float nv = max(dot(nUnit, vUnit), 1.0e-4f);

    // Refract view ray into arcane crystal/aether medium (IOR 1.58):
    const float kArcaneIor = 1.58f;
    const float3 refrV = refract(-vUnit, nUnit, 1.0f / kArcaneIor);
    const float3 rayDir = (dot(refrV, refrV) > 1.0e-4f) ? normalize(refrV) : -vUnit;
    const float depthScale = clamp(runeDepth, 1.0f, 36.0f);

    // 3-layer volumetric parallax rune & crystalline interference lattice:
    float runeAccum = 0.0f;
    float plasmaAccum = 0.0f;
    [unroll] for (int layer = 0; layer < 3; ++layer) {
        const float dFrac = (float(layer) + 1.0f) * 0.3333f;
        const float3 pL = worldPos + rayDir * (depthScale * dFrac);
        const float3 curlP = rawCurlNoise3D(pL * 0.045f + float3(0.0f, 0.0f, phaseOffset * 0.25f));
        const float4 wLat = rawWorley3D(pL * 0.095f + curlP * 0.35f);
        // Crisp geometric glyph/crystal edges inside the medium:
        const float glyphLine = 1.0f - smoothstep(0.015f, 0.085f, wLat.y);
        const float coreNode  = 1.0f - smoothstep(0.04f, 0.22f, wLat.x);
        const float atten = exp(-dFrac * 1.15f);
        runeAccum += glyphLine * atten * 0.62f;
        plasmaAccum += coreNode * atten * 0.45f;
    }

    // Surface crystalline facet normal + Belcour-Barla Airy thin-film spectral iridescence:
    const float4 surfGrad = rawNoise3Grad(worldPos * 0.14f);
    const float3 nArc = normalize(nUnit + surfGrad.yzw * 0.14f);
    const float nvArc = max(dot(nArc, vUnit), 1.0e-4f);
    const float nlArc = max(dot(nArc, lUnit), 0.0f);
    const float3 H = rawSafeHalfVec(vUnit, lUnit, nArc);
    const float nh = saturate(dot(nArc, H));
    const float vh = saturate(dot(vUnit, H));

    const float effOpd = max(thinFilmNm, 320.0f) + surfGrad.x * 140.0f;
    const float3 iridView = rawAiryThinFilmRGB(nvArc, effOpd);
    const float3 iridHalf = rawAiryThinFilmRGB(vh, effOpd);

    const float Fdie = rawDielectricFresnel(vh, kArcaneIor);
    const float Fview = rawDielectricFresnel(nvArc, kArcaneIor);
    const float specLobe = rawGgxSpecTerm(nh, nvArc, nlArc, 0.12f, 0.051f);

    // Grazing refractive Fresnel aura + deep volumetric rune emission (strictly bounded, zero blowout):
    const float auraRim = pow(1.0f - nvArc, 2.8f);
    const float3 tintSafe = saturate(arcaneTint);
    const float3 deepBody = tintSafe * (0.18f + 0.32f * plasmaAccum) * iridView;
    const float3 runeGlow = lerp(tintSafe, iridView, 0.48f) * (saturate(runeAccum) * 1.35f + saturate(plasmaAccum) * 0.45f);
    const float3 auraGlow = lerp(tintSafe, iridView, 0.60f) * (auraRim * (0.45f + 0.55f * Fview) * 0.85f);

    res.albedo = saturate(deepBody);
    res.normal = nArc;
    res.roughness = 0.12f;
    res.metallic = 0.0f;
    res.F0 = saturate(0.051f * iridView);
    res.specularAdd = min(lightCol * iridHalf * (Fdie * specLobe * 0.45f), 3.0f.xxx);
    res.emissive = min(runeGlow + auraGlow, 4.0f.xxx);
    res.transmittance = saturate((1.0f - Fview).xxx);
    res.clearcoat = 0.85f;
    res.clearcoatRough = 0.04f;
    return res;
}

// ============================================================================
// 6. UNIFIED GPU MATERIAL RECORD DISPATCHER (All 8 Families from StructuredBuffer)
// ============================================================================
RawMaterialEvalResult evalMaterialFromRecord3D(
    RawGpuMaterialRecord rec,
    float3 worldPos,
    float3 N,
    float3 V,
    float3 L,
    float3 lightCol,
    float3 envCol)
{
    const uint familyId = rec.packedMeta & 0xFFu;
    const float coatRough = max(float((rec.packedMeta >> 16u) & 0xFFu) * (1.0f / 255.0f), 0.02f);
    const float sssMfp    = float((rec.packedMeta >> 24u) & 0xFFu) * (0.5f / 255.0f);

    if (familyId == RAW_MAT_FAMILY_MAGIC_ARCANE) {
        return evalArcaneMagic3D(worldPos, N, V, L, lightCol, rec.albedo, max(rec.thinFilmNm, 420.0f), 14.0f, 0.0f);
    }
    if (familyId == RAW_MAT_FAMILY_FROST_ICE) {
        return evalFrostGlacier3D(worldPos, N, V, L, lightCol, rec.albedo, 28.0f, saturate(rec.roughness - 0.15f));
    }
    if (familyId == RAW_MAT_FAMILY_HEAT_MAGMA) {
        return evalHeatMagma3D(worldPos, N, V, L, lightCol, max(rec.blackbodyTempK, 1800.0f), 0.58f, 0.0f);
    }
    if (familyId == RAW_MAT_FAMILY_FORGED_METAL) {
        RawMaterialEvalResult m = evalForgedMetal3D(
            worldPos, N, V, L, lightCol, envCol,
            rec.etaRGB, rec.kOrAbsorbRGB, rec.roughness, rec.anisotropy, 0.25f, 0.15f);
        if (rec.blackbodyTempK > 800.0f) {
            float bbRad = 0.0f;
            const float3 bbCol = rawPlanckBlackbodyRGB(rec.blackbodyTempK, bbRad);
            const float4 crack = rawWorley3D(worldPos * 0.09f);
            const float vein = 1.0f - smoothstep(0.02f, 0.11f, crack.y);
            m.emissive = min(bbCol * bbRad * vein * 0.65f, 6.0f.xxx);
        }
        return m;
    }
    if (familyId == RAW_MAT_FAMILY_GLASS_GEM) {
        const float cauchyB = max((rec.etaRGB.b - rec.etaRGB.r) * 0.50f * (0.465f * 0.465f), 0.004f);
        const float facet = (rec.etaRGB.g > 1.60f) ? 0.12f : 0.0f;
        return evalRefractiveGlassGem3D(
            worldPos, N, V, L, lightCol, envCol,
            rec.etaRGB.g, cauchyB, rec.kOrAbsorbRGB, 12.0f, facet);
    }

    // Families 5 (Stone/Basalt), 6 (Wood/Bark/Leather), 7 (Snow/Botanical): unified physical dielectric evaluator.
    RawMaterialEvalResult res;
    const float3 nUnit = normalize(N);
    const float3 vUnit = normalize(V);
    const float3 lUnit = normalize(L);
    const float4 microG = rawFbm3Grad(worldPos * ((familyId == RAW_MAT_FAMILY_WOOD_LEATHER) ? 0.18f : 0.11f), 3);
    const float3 nPert = normalize(nUnit + microG.yzw * 0.16f);
    const float nv = max(dot(nPert, vUnit), 1.0e-4f);
    const float nl = max(dot(nPert, lUnit), 0.0f);
    const float3 H = rawSafeHalfVec(vUnit, lUnit, nPert);
    const float nh = saturate(dot(nPert, H));
    const float vh = saturate(dot(vUnit, H));

    const float ior = max(rec.etaRGB.g, 1.30f);
    const float f0s = pow((ior - 1.0f) / (ior + 1.0f), 2.0f);
    const float Fdie = rawDielectricFresnel(vh, ior);
    const float specLobe = rawGgxSpecTerm(nh, nv, nl, rec.roughness, f0s);

    float3 sssAdd = 0.0f.xxx;
    if (sssMfp > 0.005f) {
        const float backV = pow(saturate(dot(-vUnit, lUnit)), 3.0f);
        const float3 sssTint = (familyId == RAW_MAT_FAMILY_SNOW_BOTANIC && rec.albedo.g > rec.albedo.b * 1.1f)
            ? float3(0.42f, 0.92f, 0.30f)   // chlorophyll botanical transmission
            : float3(0.32f, 0.56f, 0.84f);  // alpine snow glacial subsurface
        sssAdd = lightCol * sssTint * (backV * saturate(sssMfp * 4.0f) * 0.28f);
    }

    res.albedo = saturate(rec.albedo * (0.88f + 0.24f * microG.x));
    res.normal = nPert;
    res.roughness = clamp(rec.roughness, 0.04f, 0.98f);
    res.metallic = 0.0f;
    res.F0 = f0s.xxx;
    res.specularAdd = min(lightCol * Fdie * specLobe * 0.35f + envCol * f0s * (1.0f - rec.roughness * 0.7f) + sssAdd, 3.0f.xxx);
    res.emissive = 0.0f.xxx;
    res.transmittance = saturate((1.0f - Fdie).xxx);
    res.clearcoat = saturate(rec.clearcoatWeight);
    res.clearcoatRough = coatRough;
    return res;
}

// ============================================================================
// 7. PHYSICAL MARSCHNER 3-LOBE ANISOTROPIC HAIR BRDF (R + TT + TRT)
//    Marschner et al. (SIGGRAPH 2003) / Chiang et al. (2016) clean-room MIT:
//      - R   : colorless cuticle reflection shifted toward tip (keratin IOR 1.55)
//      - TT  : warm melanin-transmitted backlit forward-scattering halo
//      - TRT : deep pigment-tinted internal reflection shifted toward root
// ============================================================================
float rawMarschnerGauss(float beta, float theta) {
    const float b = max(beta, 0.04f);
    return exp(-0.5f * (theta * theta) / (b * b)) / (2.5066283f * b);
}

float3 evalMarschnerHair3Lobe(
    float3 T,          // Unit hair strand tangent (root -> tip)
    float3 N,          // Macro surface normal
    float3 V,          // View direction (toward eye)
    float3 L,          // Light direction (toward light)
    float3 hairAlbedo, // Linear hair pigment reflectance
    float  roughness)  // Longitudinal roughness [0.22..0.58]
{
    const float3 tUnit = normalize(T);
    const float3 vUnit = normalize(V);
    const float3 lUnit = normalize(L);

    const float sinTi = clamp(dot(tUnit, lUnit), -0.999f, 0.999f);
    const float sinTo = clamp(dot(tUnit, vUnit), -0.999f, 0.999f);
    const float thetaI = asin(sinTi);
    const float thetaO = asin(sinTo);
    const float thetaH = 0.5f * (thetaI + thetaO);
    const float thetaD = 0.5f * (thetaI - thetaO);
    const float cosTd  = max(cos(thetaD), 0.12f);

    // Azimuthal projection onto plane perpendicular to strand tangent T:
    const float3 lPerp = lUnit - tUnit * sinTi;
    const float3 vPerp = vUnit - tUnit * sinTo;
    const float lPerpLen = length(lPerp);
    const float vPerpLen = length(vPerp);
    const float cosPhi = (lPerpLen > 1.0e-4f && vPerpLen > 1.0e-4f)
        ? clamp(dot(lPerp, vPerp) / (lPerpLen * vPerpLen), -1.0f, 1.0f)
        : 0.0f;

    // Keratin IOR 1.55 dielectric Fresnel at half-difference angle:
    const float cosHalf = sqrt(max(0.5f * (1.0f + cosPhi), 0.0f)) * cosTd;
    const float F_R = rawDielectricFresnel(cosHalf, 1.55f);

    // Cuticle scale tilt (~ -5.5 deg toward tip): R shifts toward tip, TRT shifts toward root!
    const float alphaR   = -0.096f;
    const float alphaTT  = -0.5f * alphaR;
    const float alphaTRT = -1.5f * alphaR;

    const float betaR   = clamp(roughness * 0.42f, 0.09f, 0.28f);
    const float betaTT  = max(betaR * 0.58f, 0.06f);
    const float betaTRT = min(betaR * 2.05f, 0.52f);

    // Longitudinal scattering Gaussians M_R, M_TT, M_TRT:
    const float M_R   = rawMarschnerGauss(betaR,   thetaH - alphaR);
    const float M_TT  = rawMarschnerGauss(betaTT,  thetaH - alphaTT);
    const float M_TRT = rawMarschnerGauss(betaTRT, thetaH - alphaTRT);

    // Azimuthal lobes N_R (broad front), N_TT (strong backlit forward peak), N_TRT (secondary caustic):
    const float cosHalfPhi = sqrt(max(0.5f * (1.0f + cosPhi), 0.0f));
    const float N_R   = 0.25f * cosHalfPhi;
    const float N_TT  = exp(-pow((1.0f + cosPhi) * 1.85f, 2.0f)) * 0.65f;
    const float N_TRT = (0.17f + 0.38f * exp(-pow((cosPhi - 0.55f) * 2.2f, 2.0f)));

    // Melanin Beer-Lambert absorption along internal hair cylinder paths:
    const float3 baseCol = clamp(hairAlbedo, 0.035f.xxx, 0.96f.xxx);
    const float3 A_TT    = pow(baseCol, 0.72f) * (1.0f - F_R) * (1.0f - F_R);
    const float3 A_TRT   = pow(baseCol, 1.38f) * (1.0f - F_R) * (1.0f - F_R) * F_R;

    // Soft wrap shadow gate along macro scalp normal so back-facing hair receives backlit TT without front-glare artifacts:
    const float nlMacro = dot(normalize(N), lUnit);
    const float frontGate = saturate(nlMacro * 0.85f + 0.28f);
    const float backGate  = saturate(0.45f - nlMacro * 0.55f) * saturate(dot(-vUnit, lUnit) * 0.75f + 0.35f);

    const float3 lobeR   = float3(0.96f, 0.97f, 1.00f) * (M_R * N_R * F_R * frontGate * 0.95f);
    const float3 lobeTT  = A_TT  * float3(1.12f, 0.96f, 0.82f) * (M_TT * N_TT * backGate * 1.15f);
    const float3 lobeTRT = A_TRT * (M_TRT * N_TRT * frontGate * 2.40f);

    return min((lobeR + lobeTT + lobeTRT) / max(cosTd * cosTd, 0.25f), 2.8f.xxx);
}

#endif
