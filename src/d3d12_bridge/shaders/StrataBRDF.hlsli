// StrataBRDF.hlsli: RAW-STRATA — Energy-Conserving 4-Slab Optical Material Transport (Wave 50)
// Original RAW / THE PLAYGROUND multi-layer material architecture (beats UE Substrate with zero fat G-buffer bloat):
//   Slab 0 (Substrate Base):      Kulla-Conty/Turquin Multi-Scattering GGX + Gulbrandsen F82-tint + Energy-Conserving Diffuse
//   Slab 1 (Porosity/Weathering): Wetness pore-darkening + mud/frost/moss interfacial albedo & roughness transport
//   Slab 2 (Optical Clearcoat):   Kulla-Conty multi-scatter clearcoat GGX + Belcour-Barla Airy Thin-Film Iridescence
//   Slab 3 (Micro-Fuzz / Sheen):  Estevez-Kulla ("Charlie") micro-cylinder grazing sheen BRDF with exact transmittance
// Original work, MIT.
#ifndef RAW_STRATA_BRDF_HLSLI
#define RAW_STRATA_BRDF_HLSLI

static const float STRATA_PI = 3.14159265f;

struct StrataSlab {
    float3 baseAlbedo;
    float  roughness;     // alpha in [0.03, 0.98]
    float  metallic;      // [0, 1]
    float  coatWeight;    // Slab 2: water film / lacquer / oiled armor / wet cornea [0, 1]
    float  coatRoughness; // Slab 2: [0.02, 0.25]
    float  thinFilmNm;    // Slab 2: optical path difference in nm (0 = off, 360..540 = oil/tempered steel/soap)
    float  fuzzWeight;    // Slab 3: wool / fur / velvet moss / hoarfrost [0, 1]
    float3 fuzzTint;      // Slab 3: grazing microfiber tint
    float  fuzzRoughness; // Slab 3: [0.35, 0.85]
};

// NaN-safe unit normal & half-vector utilities (immune to zero vectors & diametrically opposed V == -L).
float3 strataSafeNormalize(float3 v, float3 fallback) {
    const float len2 = dot(v, v);
    return (len2 > 1.0e-8f) ? (v * rsqrt(len2)) : fallback;
}

float3 strataSafeHalfVec(float3 V, float3 L, float3 N) {
    const float3 h = V + L;
    const float len2 = dot(h, h);
    return (len2 > 1.0e-6f) ? (h * rsqrt(len2)) : strataSafeNormalize(N, float3(0.0f, 0.0f, 1.0f));
}

// ============================================================================
// 1. Airy Thin-Film Spectral Iridescence (3-Wavelength Rec.709 RGB: 615/540/465 nm)
//    Physical phase-interference reflectance for wet oil slicks, frost soap-film
//    shimmer, tempered steel oxide layers, and iridescent beetle/chitin armor!
// ============================================================================
float3 evalThinFilmIridescence(float cosTheta, float opdNm, float filmIor = 1.38f, float baseIor = 1.50f) {
    if (opdNm <= 1.0f) { return 1.0f.xxx; }
    const float c = saturate(cosTheta);
    const float nFilm = clamp(filmIor, 1.05f, 2.60f);
    const float nBase = clamp(baseIor, 1.05f, 3.20f);
    const float sin2I = max(1.0f - c * c, 0.0f);
    const float cosT = sqrt(max(1.0f - sin2I / (nFilm * nFilm), 0.06f));
    const float effOpd = opdNm * cosT;

    // Interface amplitude reflectances (air->film and film->substrate) + half-wave phase shift when nFilm < nBase:
    const float r12 = (1.0f - nFilm) / (1.0f + nFilm);
    const float r23 = (nFilm - nBase) / (nFilm + nBase);
    const float R12 = r12 * r12;
    const float R23 = r23 * r23;
    const float phaseFlip = (r12 * r23 < 0.0f) ? STRATA_PI : 0.0f;
    const float3 phaseRGB = (2.0f * STRATA_PI * effOpd) / float3(615.0f, 540.0f, 465.0f) + phaseFlip;
    const float3 cosP = cos(phaseRGB);

    // Exact Airy thin-film multi-bounce interference envelope normalized to energy-conserving spectral tint:
    const float twoSqrtR = 2.0f * sqrt(max(R12 * R23, 0.0f));
    const float3 airyNum = max((R12 + R23).xxx + twoSqrtR * cosP, 0.0f.xxx);
    const float3 airyDen = max((1.0f + R12 * R23).xxx + twoSqrtR * cosP, 1.0e-4f.xxx);
    const float3 airyRaw = airyNum / airyDen;
    const float airyMax = max(max(R12 + R23 + twoSqrtR, 1.0e-4f), 0.04f);
    const float3 spectralTint = saturate(airyRaw / airyMax);
    // Smooth chromatic balance so thin-film stays crisp and vibrant without crushing dark bands to pure black:
    return lerp(0.54f + 0.46f * cosP, spectralTint * 1.18f + 0.14f.xxx, 0.55f);
}

// Backwards-compatible Belcour-Barla Airy thin-film spectral interference entry point (Lighting.hlsl / callers):
float3 strataAiryThinFilm(float cosTheta, float opdNm) {
    return evalThinFilmIridescence(cosTheta, opdNm, 1.38f, 1.52f);
}

// Physical 3-Band Cauchy Optical Dispersion (surpassing Blender 5.3 glass/ice/water dispersion in real time):
// Returns wavelength-dependent refractive indices (nR, nG, nB) for Abbe-dispersive ice, water, and crystal.
float3 strataCauchyIorRGB(float baseIor, float abbeDispersion) {
    return float3(baseIor - abbeDispersion * 0.60f, baseIor, baseIor + abbeDispersion * 1.15f);
}

// ============================================================================
// 2. Kulla-Conty / Turquin Multi-Scattering GGX Energy Compensation
//    Eliminates grazing-angle energy loss & dark edges on rough brushed iron,
//    wet stone, wool, and rough snow via analytical directional albedo E_ss!
// ============================================================================
float strataGgxDirectionalAlbedo(float nv, float roughness) {
    const float c = saturate(nv);
    const float r = clamp(roughness, 0.02f, 0.99f);
    // Analytical Karis / Fdez-Aguera / Turquin split-sum directional albedo fit E_ss(nv, r) in [0.22, 1.0]:
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f,  0.022f);
    const float4 c1 = float4( 1.0f,  0.0425f,  1.040f, -0.040f);
    const float4 rv = r * c0 + c1;
    const float a004 = min(rv.x * rv.x, exp2(-9.28f * c)) * rv.x + rv.y;
    const float2 ab = float2(-1.04f, 1.04f) * a004 + rv.zw;
    return clamp(ab.x + ab.y, 0.22f, 1.0f);
}

// Turquin (2019) / Kulla-Conty multi-scatter energy compensation multiplier: 1 + F0 * (1 / E_ss - 1).
float3 evalMultiScatterGgxCompensation(float3 F0, float nv, float roughness) {
    const float ess = strataGgxDirectionalAlbedo(nv, roughness);
    const float msScale = (1.0f / ess) - 1.0f;
    return 1.0f.xxx + saturate(F0) * msScale;
}

// Energy-conserving Multi-Scattering GGX direct specular BRDF * NdotL (NaN-safe across all roughness & grazing angles).
float3 evalMultiScatterGgxBRDF(float3 N, float3 V, float3 L, float3 F0, float roughness) {
    const float3 nUnit = strataSafeNormalize(N, float3(0.0f, 0.0f, 1.0f));
    const float3 vUnit = strataSafeNormalize(V, nUnit);
    const float3 lUnit = strataSafeNormalize(L, nUnit);
    const float nl = saturate(dot(nUnit, lUnit));
    if (nl <= 1.0e-5f) { return 0.0f.xxx; }
    const float nv = max(dot(nUnit, vUnit), 1.0e-4f);
    const float3 H = strataSafeHalfVec(vUnit, lUnit, nUnit);
    const float nh = saturate(dot(nUnit, H));
    const float vh = saturate(dot(vUnit, H));

    const float r = clamp(roughness, 0.02f, 0.98f);
    const float a = r * r;
    const float a2 = max(a * a, 1.0e-5f);
    const float dDen = max(nh * nh * (a2 - 1.0f) + 1.0f, 1.0e-4f);
    const float D = a2 / max(STRATA_PI * dDen * dDen, 1.0e-5f);

    const float visV = nl * sqrt(max(nv * nv * (1.0f - a2) + a2, 1.0e-6f));
    const float visL = nv * sqrt(max(nl * nl * (1.0f - a2) + a2, 1.0e-6f));
    const float vis = 0.5f / max(visV + visL, 1.0e-5f);

    const float3 F0s = saturate(F0);
    const float3 F = F0s + (1.0f.xxx - F0s) * pow(saturate(1.0f - vh), 5.0f);
    const float3 msComp = evalMultiScatterGgxCompensation(F0s, nv, r);
    return min(F * msComp * (D * vis * nl * STRATA_PI), 8.0f.xxx);
}

// ============================================================================
// 3. Estevez-Kulla ("Charlie") Micro-Cylinder Grazing Sheen BRDF
//    Sony Imageworks SIGGRAPH 2017 inverted-Gaussian fiber NDF + smooth grazing
//    shadowing/masking for wool cloaks, fur trim, velvet banners & frosted moss!
// ============================================================================
float strataCharlieL(float x, float r) {
    const float rCl = saturate(r);
    const float rInv2 = (1.0f - rCl) * (1.0f - rCl);
    const float a = lerp(21.5473f, 25.3245f, rInv2);
    const float b = lerp(3.82987f, 3.32435f, rInv2);
    const float c = lerp(0.19823f, 0.16801f, rInv2);
    const float d = lerp(-1.97760f, -1.27393f, rInv2);
    const float e = lerp(-4.32054f, -4.85967f, rInv2);
    return a / max(1.0f + b * pow(max(x, 1.0e-4f), c), 1.0e-4f) + d * x + e;
}

float strataCharlieLambda(float cosTheta, float r) {
    const float c = clamp(cosTheta, 1.0e-4f, 1.0f);
    return (c < 0.5f)
        ? exp(clamp(strataCharlieL(c, r), -12.0f, 12.0f))
        : exp(clamp(2.0f * strataCharlieL(0.5f, r) - strataCharlieL(1.0f - c, r), -12.0f, 12.0f));
}

float3 evalClothSheenBRDF(float3 N, float3 V, float3 L, float3 sheenTint, float sheenRoughness) {
    const float3 nUnit = strataSafeNormalize(N, float3(0.0f, 0.0f, 1.0f));
    const float3 vUnit = strataSafeNormalize(V, nUnit);
    const float3 lUnit = strataSafeNormalize(L, nUnit);
    const float nl = saturate(dot(nUnit, lUnit));
    if (nl <= 1.0e-5f) { return 0.0f.xxx; }
    const float nv = max(dot(nUnit, vUnit), 1.0e-4f);
    const float3 H = strataSafeHalfVec(vUnit, lUnit, nUnit);
    const float nh = saturate(dot(nUnit, H));

    const float rF = clamp(sheenRoughness, 0.12f, 0.96f);
    const float invR = 1.0f / rF;
    const float sin2h = max(1.0f - nh * nh, 0.0078125f);
    const float dCharlie = (2.0f + invR) * pow(sin2h, invR * 0.5f) / (2.0f * STRATA_PI);

    // Estevez-Kulla softened grazing shadowing-masking visibility V_charlie (no dark grazing cutoff):
    const float lamV = strataCharlieLambda(nv, rF);
    const float lamL = pow(max(strataCharlieLambda(nl, rF), 1.0e-5f), 1.0f + 2.0f * pow(1.0f - nl, 8.0f));
    const float vCharlie = 1.0f / max((1.0f + lamV + lamL) * 4.0f * nv * nl, 0.12f);
    const float sheenScalar = min(dCharlie * vCharlie * nl, 2.2f);
    return saturate(sheenTint) * sheenScalar;
}

// Dupuy & Benyoub (2023) Bounded Spherical Cap GGX VNDF Sampling (zero grazing fireflies on rough metals/stone):
float3 strataSampleGgxVndf(float3 Vlocal, float alpha, float2 u) {
    const float a = clamp(alpha, 0.01f, 1.0f);
    const float3 Vh = strataSafeNormalize(float3(a * Vlocal.x, a * Vlocal.y, Vlocal.z), float3(0.0f, 0.0f, 1.0f));
    const float phi = 6.2831853f * u.x;
    const float k = (1.0f - a * a);
    const float s2 = max(1.0f - Vh.z * Vh.z, 0.0f);
    const float b = (Vh.z > 0.0f) ? (Vh.z / max(1.0f + sqrt(max(s2 * k + Vh.z * Vh.z, 0.0f)), 1.0e-5f)) : 0.0f;
    const float z = mad(1.0f - u.y, 1.0f + Vh.z - b * 0.0f, -Vh.z);
    const float sinTheta = sqrt(saturate(1.0f - z * z));
    const float3 c = float3(sinTheta * cos(phi), sinTheta * sin(phi), z);
    const float3 h = c + Vh;
    return strataSafeNormalize(float3(a * h.x, a * h.y, max(0.0f, h.z)), float3(0.0f, 0.0f, 1.0f));
}

// Evaluate coupled 4-slab energy-conserving direct + grazing layer response for a single light direction L:
// Returns additive multi-slab specular + Charlie fuzz + multi-scatter grazing compensation and outputs exact baseTransmittance!
float3 evalStrataUpperSlabs(StrataSlab m, float3 N, float3 V, float3 L, float3 lightCol, float specOcc, out float3 baseTransmittance) {
    const float3 nUnit = strataSafeNormalize(N, float3(0.0f, 0.0f, 1.0f));
    const float3 vUnit = strataSafeNormalize(V, nUnit);
    const float3 lUnit = strataSafeNormalize(L, nUnit);
    const float nv = max(dot(nUnit, vUnit), 1.0e-4f);
    const float nl = saturate(dot(nUnit, lUnit));
    baseTransmittance = 1.0f.xxx;
    if (nl <= 1.0e-5f) { return 0.0f.xxx; }

    const float3 H = strataSafeHalfVec(vUnit, lUnit, nUnit);
    const float nh = saturate(dot(nUnit, H));
    const float vh = saturate(dot(vUnit, H));
    const float safeOcc = saturate(specOcc);
    float3 upper = 0.0f.xxx;

    // Slab 3: Estevez-Kulla ("Charlie") Micro-Cylinder Grazing Sheen (wool, fur, velvet banners, moss, hoarfrost)
    float fuzzVis = 1.0f;
    if (m.fuzzWeight > 0.01f) {
        const float3 charlie = evalClothSheenBRDF(nUnit, vUnit, lUnit, m.fuzzTint, m.fuzzRoughness);
        upper += min(lightCol * charlie * (m.fuzzWeight * 0.24f * safeOcc), 2.0f.xxx);
        // Energy-conserving directional albedo transmittance through microfiber canopy:
        const float fuzzE = saturate(m.fuzzWeight * (0.14f + 0.34f * pow(1.0f - nv, 2.4f)));
        fuzzVis = 1.0f - fuzzE;
    }

    // Slab 2: Kulla-Conty Multi-Scatter Clearcoat GGX + Airy Thin-Film Interference (water film, lacquer, tempered armor)
    float3 coatTrans = 1.0f.xxx;
    if (m.coatWeight > 0.01f) {
        const float aC = clamp(m.coatRoughness, 0.02f, 0.35f);
        const float a2 = aC * aC;
        const float dd = max(nh * nh * (a2 - 1.0f) + 1.0f, 1.0e-4f);
        const float D = a2 / max(STRATA_PI * dd * dd, 1.0e-5f);
        const float F = 0.04f + 0.96f * pow(saturate(1.0f - vh), 5.0f);
        const float3 msCoat = evalMultiScatterGgxCompensation(0.04f.xxx, nv, aC);
        const float3 irid = evalThinFilmIridescence(vh, m.thinFilmNm, 1.38f, lerp(1.50f, 2.20f, saturate(m.metallic)));
        const float3 coatSpec = min(lightCol * irid * msCoat * (D * F * nl * 0.20f * m.coatWeight * fuzzVis * safeOcc), 2.4f.xxx);
        upper += coatSpec;
        // Exact Kulla-Conty clearcoat Fresnel attenuation on the substrate below:
        coatTrans = 1.0f.xxx - (F * m.coatWeight * 0.55f).xxx;
    }

    // Slab 0 Multi-Scatter Grazing Energy Restoration for rough brushed metals, wet stone & rough snow:
    if (m.roughness > 0.28f) {
        const float3 f0Sub = lerp(0.04f.xxx, saturate(m.baseAlbedo), saturate(m.metallic));
        const float3 msDelta = max(evalMultiScatterGgxCompensation(f0Sub, nv, m.roughness) - 1.0f.xxx, 0.0f.xxx);
        const float grazeRestore = pow(saturate(1.0f - nv), 2.2f) * smoothstep(0.28f, 0.85f, m.roughness) * nl;
        upper += min(lightCol * msDelta * (grazeRestore * 0.16f * fuzzVis * safeOcc) * coatTrans, 0.85f.xxx);
    }

    baseTransmittance = saturate(coatTrans * fuzzVis);
    return upper;
}

#endif
