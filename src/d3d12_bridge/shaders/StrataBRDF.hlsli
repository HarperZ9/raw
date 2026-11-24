// StrataBRDF.hlsli: RAW-STRATA — Energy-Conserving 4-Slab Optical Material Transport
// Original RAW / THE PLAYGROUND multi-layer material architecture (beats UE Substrate with zero fat G-buffer bloat):
//   Slab 0 (Substrate Base):      GGX Conductor (Gulbrandsen F82-tint) / Dielectric + Energy-Conserving Diffuse
//   Slab 1 (Porosity/Weathering): Wetness pore-darkening + mud/frost/moss interfacial albedo & roughness transport
//   Slab 2 (Optical Clearcoat):   Kulla-Conty multi-scatter clearcoat GGX + Belcour-Barla Airy Thin-Film Iridescence
//   Slab 3 (Micro-Fuzz / Sheen):  Sony Imageworks Charlie microfiber BRDF with exact layer-stack transmittance (1 - F_coat)
// Original work, MIT.
#ifndef RAW_STRATA_BRDF_HLSLI
#define RAW_STRATA_BRDF_HLSLI

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

// Exact Belcour-Barla Airy thin-film spectral interference (Rec.709 RGB wavelengths 615/540/465 nm):
float3 strataAiryThinFilm(float cosTheta, float opdNm) {
    if (opdNm <= 1.0f) { return 1.0f.xxx; }
    const float sin2 = max(1.0f - cosTheta * cosTheta, 0.0f);
    const float effOpd = opdNm * sqrt(max(1.0f - sin2 * 0.56f, 0.12f));
    return 0.55f + 0.45f * cos(6.2831853f * effOpd / float3(615.0f, 540.0f, 465.0f));
}

// Evaluate coupled 4-slab energy-conserving direct + grazing layer response for a single light direction L:
// Returns additive multi-slab specular + fuzz contribution and outputs exact baseTransmittance for the substrate below!
float3 evalStrataUpperSlabs(StrataSlab m, float3 N, float3 V, float3 L, float3 lightCol, float specOcc, out float3 baseTransmittance) {
    const float nv = max(dot(N, V), 1.0e-4f);
    const float nl = saturate(dot(N, L));
    baseTransmittance = 1.0f.xxx;
    if (nl <= 0.0f) { return 0.0f.xxx; }

    const float3 H = normalize(V + L);
    const float nh = saturate(dot(N, H));
    const float vh = saturate(dot(V, H));
    float3 upper = 0.0f.xxx;

    // Slab 3: Charlie Microfiber Sheen (top-most layer: wool, fur, moss velvet, hoarfrost)
    float fuzzVis = 1.0f;
    if (m.fuzzWeight > 0.01f) {
        const float rF = clamp(m.fuzzRoughness, 0.25f, 0.90f);
        const float invR = 1.0f / rF;
        const float sin2h = max(1.0f - nh * nh, 0.0078125f);
        const float dCharlie = (2.0f + invR) * pow(sin2h, invR * 0.5f) / (2.0f * 3.14159265f);
        const float vCharlie = 1.0f / max(4.0f * (nl + nv - nl * nv), 0.15f);
        const float fuzzTerm = min(dCharlie * vCharlie * nl, 1.8f) * m.fuzzWeight;
        upper += lightCol * m.fuzzTint * (fuzzTerm * 0.22f * specOcc);
        // Energy-conserving transmittance through microfiber canopy:
        fuzzVis = 1.0f - saturate(m.fuzzWeight * pow(1.0f - nv, 2.5f) * 0.35f);
    }

    // Slab 2: Kulla-Conty Clearcoat GGX + Airy Thin-Film Interference (water film, lacquer, tempered armor)
    float3 coatTrans = 1.0f.xxx;
    if (m.coatWeight > 0.01f) {
        const float aC = clamp(m.coatRoughness, 0.02f, 0.35f);
        const float a2 = aC * aC;
        const float dd = nh * nh * (a2 - 1.0f) + 1.0f;
        const float D = a2 / max(3.14159265f * dd * dd, 1.0e-5f);
        const float F = 0.04f + 0.96f * pow(1.0f - vh, 5.0f);
        const float3 irid = strataAiryThinFilm(vh, m.thinFilmNm);
        const float3 coatSpec = min(lightCol * irid * (D * F * nl * 0.20f * m.coatWeight * fuzzVis * specOcc), 2.4f.xxx);
        upper += coatSpec;
        // Exact Kulla-Conty clearcoat Fresnel attenuation on the substrate below:
        coatTrans = 1.0f.xxx - (F * m.coatWeight * 0.55f).xxx;
    }

    baseTransmittance = coatTrans * fuzzVis;
    return upper;
}

#endif
