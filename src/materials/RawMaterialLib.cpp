// RawMaterialLib.cpp: Standalone C++20/23 Physically-Measured Material Registry & Optical Evaluators.
// Original work, MIT.
#include "RawMaterialLib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <utility>

namespace raw::materials {
namespace {

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kHcOverKbNmK = 1.4387768e7f;

inline bool IsFinite3(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

inline float Clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

inline float Smoothstep(float e0, float e1, float x) {
    const float t = Clamp01((x - e0) / std::max(e1 - e0, 1.0e-6f));
    return t * t * (3.0f - 2.0f * t);
}

std::string EscapeJson(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\n') {
            out += "\\n";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string FmtFloat(float v) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.6g", static_cast<double>(v));
    return std::string(buf);
}

PhysicalMaterial MakeDielectric(
    MaterialId id, MaterialFamily fam, const char* name, const char* desc,
    Vec3 albedo, float baseIor540, float cauchyB, Vec3 absorbSigma,
    float roughness, float sssMfp, float clearcoat, float coatRough, float thinFilmNm,
    float porosity = 0.0f, float patina = 0.0f) {
    PhysicalMaterial m{};
    m.id = id;
    m.family = fam;
    m.name = name;
    m.description = desc;
    m.albedo = albedo;
    m.baseIor540 = baseIor540;
    m.cauchyB = cauchyB;
    m.etaRGB = EvaluateCauchyRGB(baseIor540, cauchyB);
    m.kRGB = {0.0f, 0.0f, 0.0f};
    m.absorbSigma = absorbSigma;
    m.roughness = roughness;
    m.metallic = 0.0f;
    m.sssMeanFreePathM = sssMfp;
    m.clearcoatWeight = clearcoat;
    m.clearcoatRoughness = coatRough;
    m.thinFilmNm = thinFilmNm;
    m.porosity = porosity;
    m.patina = patina;
    return m;
}

PhysicalMaterial MakeMetal(
    MaterialId id, const char* name, const char* desc,
    Vec3 etaRGB, Vec3 kRGB, float roughness, float anisotropy,
    float edgeWear, float patina, float thinFilmNm = 0.0f, float blackbodyTempK = 0.0f) {
    PhysicalMaterial m{};
    m.id = id;
    m.family = MaterialFamily::ForgedMetal;
    m.name = name;
    m.description = desc;
    m.etaRGB = etaRGB;
    m.kRGB = kRGB;
    m.baseIor540 = etaRGB.y;
    m.albedo = EvaluateConductorFresnelRGB(1.0f, etaRGB, kRGB);
    m.roughness = roughness;
    m.anisotropy = anisotropy;
    m.metallic = 1.0f;
    m.edgeWear = edgeWear;
    m.patina = patina;
    m.thinFilmNm = thinFilmNm;
    m.blackbodyTempK = blackbodyTempK;
    return m;
}

}  // namespace

float EvaluateCauchyIor(float baseIor540, float cauchyB, float lambdaNm) {
    if (!std::isfinite(baseIor540) || !std::isfinite(cauchyB) || !std::isfinite(lambdaNm) || lambdaNm <= 1.0f) {
        return 1.0f;
    }
    const float lamUm = lambdaNm * 0.001f;
    const float refUm = kLambdaGreenNm * 0.001f;
    const float A = baseIor540 - cauchyB / (refUm * refUm);
    return A + cauchyB / (lamUm * lamUm);
}

Vec3 EvaluateCauchyRGB(float baseIor540, float cauchyB) {
    return {
        EvaluateCauchyIor(baseIor540, cauchyB, kLambdaRedNm),
        EvaluateCauchyIor(baseIor540, cauchyB, kLambdaGreenNm),
        EvaluateCauchyIor(baseIor540, cauchyB, kLambdaBlueNm)
    };
}

float EvaluateDielectricFresnel(float cosThetaI, float ior) {
    if (!std::isfinite(cosThetaI) || !std::isfinite(ior) || ior < 1.0f) {
        return 1.0f;
    }
    const float ci = Clamp01(cosThetaI);
    const float sin2T = (1.0f - ci * ci) / (ior * ior);
    if (sin2T >= 1.0f) {
        return 1.0f;
    }
    const float ct = std::sqrt(std::max(1.0f - sin2T, 0.0f));
    const float rs = (ci - ior * ct) / std::max(ci + ior * ct, 1.0e-6f);
    const float rp = (ior * ci - ct) / std::max(ior * ci + ct, 1.0e-6f);
    return Clamp01(0.5f * (rs * rs + rp * rp));
}

Vec3 EvaluateConductorFresnelRGB(float cosThetaI, Vec3 eta, Vec3 k) {
    const float c = Clamp01(cosThetaI);
    const float c2 = c * c;
    const float s2 = std::max(1.0f - c2, 0.0f);
    auto evalCh = [&](float e, float kk) -> float {
        if (!std::isfinite(e) || !std::isfinite(kk) || e < 0.0f || kk < 0.0f) {
            return 0.0f;
        }
        const float e2 = e * e;
        const float k2 = kk * kk;
        const float t0 = e2 - k2 - s2;
        const float a2plusb2 = std::sqrt(std::max(t0 * t0 + 4.0f * e2 * k2, 0.0f));
        const float a = std::sqrt(std::max(0.5f * (a2plusb2 + t0), 0.0f));
        const float t1 = a2plusb2 + c2;
        const float t2 = 2.0f * a * c;
        const float rs = (t1 - t2) / std::max(t1 + t2, 1.0e-6f);
        const float t3 = c2 * a2plusb2 + s2 * s2;
        const float t4 = t2 * s2;
        const float rp = rs * (t3 - t4) / std::max(t3 + t4, 1.0e-6f);
        return Clamp01(0.5f * (rs + rp));
    };
    return {evalCh(eta.x, k.x), evalCh(eta.y, k.y), evalCh(eta.z, k.z)};
}

Vec3 EvaluatePlanckBlackbodyRGB(float tempK, float* outHdrRadiance) {
    if (!std::isfinite(tempK) || tempK < 700.0f) {
        if (outHdrRadiance) *outHdrRadiance = 0.0f;
        return {0.0f, 0.0f, 0.0f};
    }
    const float T = std::clamp(tempK, 700.0f, 12000.0f);
    const float exR = kHcOverKbNmK / (kLambdaRedNm * T);
    const float exG = kHcOverKbNmK / (kLambdaGreenNm * T);
    const float exB = kHcOverKbNmK / (kLambdaBlueNm * T);
    const float exMin = std::min({exR, exG, exB});
    auto planckShifted = [exMin](float lamNm, float ex) -> float {
        const float w5 = std::pow(550.0f / lamNm, 5.0f);
        const float num = w5 * std::exp(-std::min(ex - exMin, 60.0f));
        const float den = std::max(1.0f - std::exp(-std::min(ex, 60.0f)), 1.0e-6f);
        return num / den;
    };
    const float r = planckShifted(kLambdaRedNm, exR);
    const float g = planckShifted(kLambdaGreenNm, exG);
    const float b = planckShifted(kLambdaBlueNm, exB);
    const float peak = std::max({r, g, b});
    if (outHdrRadiance) {
        const float visRamp = Smoothstep(750.0f, 1150.0f, T);
        const float tNorm = T / 2000.0f;
        *outHdrRadiance = visRamp * std::min(tNorm * tNorm * tNorm * tNorm, 18.0f);
    }
    if (peak <= 1.0e-7f) {
        return {0.0f, 0.0f, 0.0f};
    }
    return {Clamp01(r / peak), Clamp01(g / peak), Clamp01(b / peak)};
}

Vec3 EvaluateBeerLambert(Vec3 absorbSigma, float distanceMeters) {
    const float d = std::max(distanceMeters, 0.0f);
    auto tr = [d](float s) -> float {
        if (!std::isfinite(s) || s < 0.0f) return 1.0f;
        return std::clamp(std::exp(-s * d), 0.0f, 1.0f);
    };
    return {tr(absorbSigma.x), tr(absorbSigma.y), tr(absorbSigma.z)};
}

Vec3 EvaluateThinFilmAiryRGB(float cosTheta, float opdNm) {
    if (!std::isfinite(opdNm) || opdNm <= 1.0f) {
        return {1.0f, 1.0f, 1.0f};
    }
    const float c = Clamp01(cosTheta);
    const float sin2 = std::max(1.0f - c * c, 0.0f);
    const float effOpd = opdNm * std::sqrt(std::max(1.0f - sin2 * 0.56f, 0.12f));
    return {
        Clamp01(0.55f + 0.45f * std::cos(kTwoPi * effOpd / kLambdaRedNm)),
        Clamp01(0.55f + 0.45f * std::cos(kTwoPi * effOpd / kLambdaGreenNm)),
        Clamp01(0.55f + 0.45f * std::cos(kTwoPi * effOpd / kLambdaBlueNm))
    };
}

Vec3 ComputeNormalIncidenceF0(const PhysicalMaterial& m) {
    if (m.metallic >= 0.5f) {
        return EvaluateConductorFresnelRGB(1.0f, m.etaRGB, m.kRGB);
    }
    return {
        EvaluateDielectricFresnel(1.0f, m.etaRGB.x),
        EvaluateDielectricFresnel(1.0f, m.etaRGB.y),
        EvaluateDielectricFresnel(1.0f, m.etaRGB.z)
    };
}

bool ValidateMaterialPhysics(const PhysicalMaterial& m, std::string* outReason) {
    auto fail = [&](const char* msg) -> bool {
        if (outReason) *outReason = msg;
        return false;
    };
    if (m.name.empty()) return fail("material name is empty");
    if (static_cast<uint8_t>(m.family) >= static_cast<uint8_t>(MaterialFamily::Count)) {
        return fail("invalid material family");
    }
    if (!IsFinite3(m.albedo) || !IsFinite3(m.etaRGB) || !IsFinite3(m.kRGB) || !IsFinite3(m.absorbSigma)) {
        return fail("non-finite optical vector");
    }
    if (!std::isfinite(m.baseIor540) || !std::isfinite(m.cauchyB) || !std::isfinite(m.roughness) ||
        !std::isfinite(m.anisotropy) || !std::isfinite(m.metallic) || !std::isfinite(m.sssMeanFreePathM) ||
        !std::isfinite(m.blackbodyTempK) || !std::isfinite(m.clearcoatWeight) ||
        !std::isfinite(m.clearcoatRoughness) || !std::isfinite(m.thinFilmNm)) {
        return fail("non-finite scalar parameter");
    }
    if (m.albedo.x < 0.0f || m.albedo.x > 1.0f ||
        m.albedo.y < 0.0f || m.albedo.y > 1.0f ||
        m.albedo.z < 0.0f || m.albedo.z > 1.0f) {
        return fail("albedo violates energy conservation [0, 1]");
    }
    if (m.roughness < 0.01f || m.roughness > 1.0f) return fail("roughness out of [0.01, 1.0]");
    if (m.metallic < 0.0f || m.metallic > 1.0f) return fail("metallic out of [0, 1]");
    if (m.anisotropy < -1.0f || m.anisotropy > 1.0f) return fail("anisotropy out of [-1, 1]");
    if (m.cauchyB < 0.0f) return fail("negative Cauchy dispersion B violates normal dispersion");
    if (m.blackbodyTempK < 0.0f || m.blackbodyTempK > 20000.0f) return fail("blackbody temperature out of bounds");
    if (m.sssMeanFreePathM < 0.0f) return fail("negative SSS mean-free-path");
    if (m.absorbSigma.x < 0.0f || m.absorbSigma.y < 0.0f || m.absorbSigma.z < 0.0f) {
        return fail("negative Beer-Lambert absorption violates energy conservation");
    }
    if (m.metallic < 0.5f) {
        if (m.baseIor540 < 1.0f || m.etaRGB.x < 1.0f || m.etaRGB.y < 1.0f || m.etaRGB.z < 1.0f) {
            return fail("dielectric IOR must be >= 1.0");
        }
        if (m.cauchyB > 0.0f && !(m.etaRGB.z > m.etaRGB.y && m.etaRGB.y > m.etaRGB.x)) {
            return fail("Cauchy dispersion RGB must satisfy n_blue > n_green > n_red");
        }
    } else {
        if (m.etaRGB.x <= 0.0f || m.etaRGB.y <= 0.0f || m.etaRGB.z <= 0.0f ||
            m.kRGB.x < 0.0f || m.kRGB.y < 0.0f || m.kRGB.z < 0.0f) {
            return fail("conductor complex IOR must have eta > 0 and k >= 0");
        }
    }
    const Vec3 f0 = ComputeNormalIncidenceF0(m);
    if (f0.x < 0.0f || f0.x > 1.0f || f0.y < 0.0f || f0.y > 1.0f || f0.z < 0.0f || f0.z > 1.0f) {
        return fail("Fresnel F0 outside [0, 1]");
    }
    return true;
}

GpuMaterialRecord PackGpuMaterialRecord(const PhysicalMaterial& m) {
    GpuMaterialRecord rec{};
    rec.albedo[0] = Clamp01(m.albedo.x);
    rec.albedo[1] = Clamp01(m.albedo.y);
    rec.albedo[2] = Clamp01(m.albedo.z);
    rec.roughness = std::clamp(m.roughness, 0.02f, 0.98f);

    rec.etaRGB[0] = m.etaRGB.x;
    rec.etaRGB[1] = m.etaRGB.y;
    rec.etaRGB[2] = m.etaRGB.z;
    rec.anisotropy = std::clamp(m.anisotropy, -1.0f, 1.0f);

    const Vec3 kOrSig = (m.metallic >= 0.5f) ? m.kRGB : m.absorbSigma;
    rec.kOrAbsorbRGB[0] = kOrSig.x;
    rec.kOrAbsorbRGB[1] = kOrSig.y;
    rec.kOrAbsorbRGB[2] = kOrSig.z;
    rec.metallic = Clamp01(m.metallic);

    rec.blackbodyTempK = std::max(m.blackbodyTempK, 0.0f);
    rec.thinFilmNm = std::max(m.thinFilmNm, 0.0f);
    rec.clearcoatWeight = Clamp01(m.clearcoatWeight);

    const uint32_t famBits = static_cast<uint32_t>(m.family) & 0xFFu;
    const uint32_t idBits  = static_cast<uint32_t>(m.id) & 0xFFu;
    const uint32_t crBits  = static_cast<uint32_t>(std::lround(Clamp01(m.clearcoatRoughness) * 255.0f)) & 0xFFu;
    const uint32_t sssBits = static_cast<uint32_t>(std::lround(Clamp01(m.sssMeanFreePathM / 0.5f) * 255.0f)) & 0xFFu;
    rec.packedMeta = famBits | (idBits << 8u) | (crBits << 16u) | (sssBits << 24u);
    return rec;
}

MaterialFamily UnpackGpuMaterialFamily(const GpuMaterialRecord& rec) {
    const uint8_t f = static_cast<uint8_t>(rec.packedMeta & 0xFFu);
    return f < static_cast<uint8_t>(MaterialFamily::Count)
        ? static_cast<MaterialFamily>(f)
        : MaterialFamily::StoneBasaltMasonry;
}

uint16_t UnpackGpuMaterialIdLo(const GpuMaterialRecord& rec) {
    return static_cast<uint16_t>((rec.packedMeta >> 8u) & 0xFFu);
}

float UnpackGpuClearcoatRoughness(const GpuMaterialRecord& rec) {
    return static_cast<float>((rec.packedMeta >> 16u) & 0xFFu) * (1.0f / 255.0f);
}

float UnpackGpuSssMfpMeters(const GpuMaterialRecord& rec) {
    return static_cast<float>((rec.packedMeta >> 24u) & 0xFFu) * (0.5f / 255.0f);
}

std::string_view FamilyName(MaterialFamily family) {
    switch (family) {
        case MaterialFamily::MagicArcane:            return "MagicArcane";
        case MaterialFamily::FrostGlacial:           return "FrostGlacial";
        case MaterialFamily::HeatMagma:              return "HeatMagma";
        case MaterialFamily::ForgedMetal:            return "ForgedMetal";
        case MaterialFamily::RefractiveGlassGem:     return "RefractiveGlassGem";
        case MaterialFamily::StoneBasaltMasonry:     return "StoneBasaltMasonry";
        case MaterialFamily::OrganicWoodBarkLeather: return "OrganicWoodBarkLeather";
        case MaterialFamily::SnowBotanical:          return "SnowBotanical";
        default:                                     return "Unknown";
    }
}

std::string ExportMaterialToJson(const PhysicalMaterial& m) {
    std::ostringstream os;
    os << "{"
       << "\"id\":" << static_cast<uint16_t>(m.id) << ","
       << "\"family\":\"" << FamilyName(m.family) << "\","
       << "\"name\":\"" << EscapeJson(m.name) << "\","
       << "\"description\":\"" << EscapeJson(m.description) << "\","
       << "\"albedo\":[" << FmtFloat(m.albedo.x) << "," << FmtFloat(m.albedo.y) << "," << FmtFloat(m.albedo.z) << "],"
       << "\"etaRGB\":[" << FmtFloat(m.etaRGB.x) << "," << FmtFloat(m.etaRGB.y) << "," << FmtFloat(m.etaRGB.z) << "],"
       << "\"kRGB\":[" << FmtFloat(m.kRGB.x) << "," << FmtFloat(m.kRGB.y) << "," << FmtFloat(m.kRGB.z) << "],"
       << "\"absorbSigma\":[" << FmtFloat(m.absorbSigma.x) << "," << FmtFloat(m.absorbSigma.y) << "," << FmtFloat(m.absorbSigma.z) << "],"
       << "\"baseIor540\":" << FmtFloat(m.baseIor540) << ","
       << "\"cauchyB\":" << FmtFloat(m.cauchyB) << ","
       << "\"roughness\":" << FmtFloat(m.roughness) << ","
       << "\"anisotropy\":" << FmtFloat(m.anisotropy) << ","
       << "\"metallic\":" << FmtFloat(m.metallic) << ","
       << "\"sssMeanFreePathM\":" << FmtFloat(m.sssMeanFreePathM) << ","
       << "\"blackbodyTempK\":" << FmtFloat(m.blackbodyTempK) << ","
       << "\"clearcoatWeight\":" << FmtFloat(m.clearcoatWeight) << ","
       << "\"clearcoatRoughness\":" << FmtFloat(m.clearcoatRoughness) << ","
       << "\"thinFilmNm\":" << FmtFloat(m.thinFilmNm)
       << "}";
    return os.str();
}

MaterialRegistry::MaterialRegistry() {
    PopulateCanonicalDefaults();
}

void MaterialRegistry::PopulateCanonicalDefaults() {
    materials_.clear();
    materials_.reserve(32);

    // 1. Magic / Arcane
    materials_.push_back(MakeDielectric(
        MaterialId::AetherialPlasma, MaterialFamily::MagicArcane,
        "AetherialPlasma", "Refractive violet-azure arcane plasma with thin-film crystal aura",
        {0.32f, 0.56f, 0.96f}, 1.58f, 0.014f, {0.35f, 0.14f, 0.05f},
        0.10f, 0.12f, 0.85f, 0.04f, 440.0f));
    materials_.push_back(MakeDielectric(
        MaterialId::SoulGemResonance, MaterialFamily::MagicArcane,
        "SoulGemResonance", "Faceted amethyst soul gem with volumetric rune interference",
        {0.58f, 0.28f, 0.92f}, 1.62f, 0.018f, {0.22f, 0.42f, 0.09f},
        0.07f, 0.18f, 0.90f, 0.03f, 510.0f));
    materials_.push_back(MakeDielectric(
        MaterialId::EnchantedRuneIridescent, MaterialFamily::MagicArcane,
        "EnchantedRuneIridescent", "Soap-crystal thin-film iridescent runic glaze",
        {0.22f, 0.82f, 0.86f}, 1.55f, 0.012f, {0.28f, 0.08f, 0.10f},
        0.14f, 0.08f, 0.78f, 0.05f, 385.0f));

    // 2. Frost & Glacial Ice (Warren & Brandt 2008: IOR 1.3114 at 540nm, zero time-pulse)
    materials_.push_back(MakeDielectric(
        MaterialId::GlacialIce, MaterialFamily::FrostGlacial,
        "GlacialIce", "Deep Arctic glacial ice (Warren & Brandt 2008, IOR 1.3114) with turquoise Beer-Lambert absorption and internal cleavage planes",
        {0.18f, 0.52f, 0.68f}, 1.3114f, 0.00285f, {0.42f, 0.085f, 0.022f},
        0.10f, 0.35f, 0.65f, 0.055f, 0.0f));
    materials_.push_back(MakeDielectric(
        MaterialId::HoarfrostCrystal, MaterialFamily::FrostGlacial,
        "HoarfrostCrystal", "Crystalline hoarfrost needle rime with high grazing micro-glint",
        {0.72f, 0.84f, 0.94f}, 1.3114f, 0.00285f, {0.32f, 0.14f, 0.07f},
        0.42f, 0.14f, 0.25f, 0.12f, 0.0f));
    materials_.push_back(MakeDielectric(
        MaterialId::PackedRimeIce, MaterialFamily::FrostGlacial,
        "PackedRimeIce", "Wind-compressed milky blue-white glacier rime with suspended air bubbles",
        {0.54f, 0.70f, 0.82f}, 1.3114f, 0.00285f, {0.38f, 0.16f, 0.08f},
        0.34f, 0.22f, 0.35f, 0.10f, 0.0f));

    // 3. Heat, Magma & Forge Embers (1000K-3400K Planck blackbody)
    {
        auto magma = MakeDielectric(
            MaterialId::MoltenMagma, MaterialFamily::HeatMagma,
            "MoltenMagma", "2200K incandescent silicate lava with dark basalt fissures",
            {0.05f, 0.045f, 0.048f}, 1.54f, 0.005f, {2.5f, 2.5f, 2.5f},
            0.32f, 0.0f, 0.30f, 0.14f, 0.0f);
        magma.blackbodyTempK = 2200.0f;
        materials_.push_back(std::move(magma));

        auto crust = MakeDielectric(
            MaterialId::BasaltCoolingCrust, MaterialFamily::HeatMagma,
            "BasaltCoolingCrust", "Vesicular black basalt cooling crust over 1350K magma cracks",
            {0.045f, 0.043f, 0.046f}, 1.53f, 0.004f, {4.0f, 4.0f, 4.0f},
            0.84f, 0.0f, 0.05f, 0.20f, 0.0f, 0.55f);
        crust.blackbodyTempK = 1350.0f;
        materials_.push_back(std::move(crust));

        auto ironHot = MakeMetal(
            MaterialId::IncandescentForgeIron,
            "IncandescentForgeIron", "2900K white-gold forged iron fresh from the smelter",
            {2.8834f, 2.9211f, 2.6460f}, {3.0466f, 2.9236f, 2.8050f}, 0.30f, 0.25f, 0.35f, 0.05f, 0.0f, 2900.0f);
        ironHot.family = MaterialFamily::HeatMagma;
        materials_.push_back(std::move(ironHot));

        auto embers = MakeDielectric(
            MaterialId::SmolderingEmbers, MaterialFamily::HeatMagma,
            "SmolderingEmbers", "1150K cherry-red hearth charcoal embers with ash crust",
            {0.04f, 0.038f, 0.038f}, 1.48f, 0.0f, {5.0f, 5.0f, 5.0f},
            0.88f, 0.0f, 0.0f, 0.25f, 0.0f, 0.72f);
        embers.blackbodyTempK = 1150.0f;
        materials_.push_back(std::move(embers));
    }

    // 4. Forged Metals (Mitsuba-validated measured n + ik at Rec.709 RGB 615/540/465 nm, dE2000 <= 0.037)
    materials_.push_back(MakeMetal(
        MaterialId::IronForged, "IronForged", "Hand-forged Nordic iron (Johnson & Christy 1974 Fe) with hammer relief and rust patina",
        {2.8834f, 2.9211f, 2.6460f}, {3.0466f, 2.9236f, 2.8050f}, 0.34f, 0.22f, 0.38f, 0.24f));
    materials_.push_back(MakeMetal(
        MaterialId::NordicSteel, "NordicSteel", "Brushed tempered Nordic steel (Karlsson & Ribbing 1982) with beveled edge-wear",
        {2.2923f, 2.0072f, 1.7357f}, {4.0276f, 3.6166f, 3.2072f}, 0.24f, 0.45f, 0.52f, 0.10f, 360.0f));
    materials_.push_back(MakeMetal(
        MaterialId::SilverPolished, "SilverPolished", "Measured optical silver (Johnson & Christy 1972 Ag) with high broadband reflectance",
        {0.0595f, 0.0569f, 0.0469f}, {4.1367f, 3.5047f, 2.8028f}, 0.12f, 0.15f, 0.20f, 0.08f));
    materials_.push_back(MakeMetal(
        MaterialId::GoldMeasured, "GoldMeasured", "Measured physical gold (Johnson & Christy 1972 Au) with warm spectral dispersion",
        {0.2142f, 0.4890f, 1.3319f}, {3.2508f, 2.3389f, 1.8693f}, 0.16f, 0.20f, 0.22f, 0.04f));
    materials_.push_back(MakeMetal(
        MaterialId::DwemerBrass, "DwemerBrass", "Burnished Dwemer bronze-brass alloy (Querry 1985 Cu-Zn) with verdigris crevice patina",
        {0.4505f, 0.5490f, 0.9470f}, {3.3895f, 2.6680f, 1.9200f}, 0.26f, 0.35f, 0.42f, 0.28f));
    materials_.push_back(MakeMetal(
        MaterialId::EbonyVolcanicMetal, "EbonyVolcanicMetal", "Dark volcanic ebony alloy with tempered oil-slick thin-film sheen",
        {2.65f, 2.58f, 2.72f}, {1.95f, 1.88f, 2.12f}, 0.18f, 0.30f, 0.35f, 0.05f, 480.0f));
    materials_.push_back(MakeMetal(
        MaterialId::GlassSteelAlloy, "GlassSteelAlloy", "Malachite-infused crystalline glass-steel alloy with emerald specular shift",
        {1.75f, 2.18f, 2.04f}, {2.35f, 3.15f, 2.85f}, 0.14f, 0.28f, 0.25f, 0.02f, 520.0f));
    materials_.push_back(MakeMetal(
        MaterialId::DaedricObsidianMetal, "DaedricObsidianMetal", "Volcanic dark forged alloy veined with 1450K incandescent ember fissures",
        {2.30f, 2.15f, 2.10f}, {2.10f, 1.75f, 1.65f}, 0.22f, 0.18f, 0.30f, 0.08f, 410.0f, 1450.0f));

    // 5. True Refractive Glass, Amber & Gemstones (Lab-measured Cauchy RGB dispersion + Beer-Lambert absorption)
    materials_.push_back(MakeDielectric(
        MaterialId::CrownGlass, MaterialFamily::RefractiveGlassGem,
        "CrownGlass", "Optical BK7 crown glass (Schott BK7: IOR 1.5190, Cauchy B=0.00409) with crisp Fresnel & subtle green edge tint",
        {0.96f, 0.99f, 0.97f}, 1.5190f, 0.00409f, {0.06f, 0.025f, 0.045f},
        0.03f, 0.0f, 0.0f, 0.03f, 0.0f));
    materials_.push_back(MakeDielectric(
        MaterialId::QuartzAmber, MaterialFamily::RefractiveGlassGem,
        "QuartzAmber", "Warm honey-amber & quartz (Ghosh 1999: IOR 1.5464, Cauchy B=0.00394) with rich golden Beer-Lambert absorption",
        {0.94f, 0.62f, 0.14f}, 1.5464f, 0.00394f, {0.10f, 0.52f, 1.85f},
        0.05f, 0.08f, 0.20f, 0.04f, 0.0f));
    materials_.push_back(MakeDielectric(
        MaterialId::SapphireCrystal, MaterialFamily::RefractiveGlassGem,
        "SapphireCrystal", "Corundum sapphire gemstone (Malitson & Dodge 1972: IOR 1.7711, Cauchy B=0.00537) with cobalt absorption",
        {0.12f, 0.34f, 0.92f}, 1.7711f, 0.00537f, {1.65f, 0.68f, 0.08f},
        0.035f, 0.0f, 0.0f, 0.03f, 0.0f));
    materials_.push_back(MakeDielectric(
        MaterialId::RubyCrystal, MaterialFamily::RefractiveGlassGem,
        "RubyCrystal", "Corundum ruby gemstone (Malitson & Dodge 1972: IOR 1.7711, Cauchy B=0.00537) with deep crimson absorption",
        {0.90f, 0.10f, 0.18f}, 1.7711f, 0.00537f, {0.08f, 3.40f, 2.80f},
        0.035f, 0.0f, 0.0f, 0.03f, 0.0f));
    materials_.push_back(MakeDielectric(
        MaterialId::DiamondCut, MaterialFamily::RefractiveGlassGem,
        "DiamondCut", "Brilliant-cut diamond (Phillip & Taft 1964: IOR 2.4240, Cauchy B=0.01116) with intense prismatic dispersion fire",
        {0.98f, 0.99f, 1.00f}, 2.4240f, 0.01116f, {0.015f, 0.012f, 0.018f},
        0.02f, 0.0f, 0.0f, 0.02f, 0.0f));

    // 6. Stone, Basalt & Masonry
    materials_.push_back(MakeDielectric(
        MaterialId::ColumnarBasalt, MaterialFamily::StoneBasaltMasonry,
        "ColumnarBasalt", "Dark volcanic columnar basalt with vesicular micro-porosity and wet darkening",
        {0.12f, 0.125f, 0.135f}, 1.52f, 0.0f, {3.0f, 3.0f, 3.0f},
        0.78f, 0.0f, 0.0f, 0.14f, 0.0f, 0.68f, 0.25f));
    materials_.push_back(MakeDielectric(
        MaterialId::NordicAshlarStone, MaterialFamily::StoneBasaltMasonry,
        "NordicAshlarStone", "Hand-dressed Nordic masonry blocks with weathered mortar crevices",
        {0.34f, 0.33f, 0.31f}, 1.50f, 0.0f, {2.5f, 2.5f, 2.5f},
        0.74f, 0.0f, 0.0f, 0.16f, 0.0f, 0.58f, 0.32f));
    materials_.push_back(MakeDielectric(
        MaterialId::MossyGranite, MaterialFamily::StoneBasaltMasonry,
        "MossyGranite", "Coarse quartz-feldspar granite colonized by alpine lichen and crevice moss",
        {0.28f, 0.30f, 0.26f}, 1.54f, 0.0f, {2.5f, 2.5f, 2.5f},
        0.80f, 0.0f, 0.0f, 0.18f, 0.0f, 0.52f, 0.65f));

    // 7. Organic Wood, Bark & Leather
    {
        auto bark = MakeDielectric(
            MaterialId::NordicPineBark, MaterialFamily::OrganicWoodBarkLeather,
            "NordicPineBark", "Deeply furrowed coniferous bark with anisotropic vertical fibers",
            {0.16f, 0.11f, 0.075f}, 1.46f, 0.0f, {3.0f, 3.0f, 3.0f},
            0.86f, 0.0f, 0.0f, 0.22f, 0.0f, 0.65f, 0.20f);
        bark.anisotropy = 0.55f;
        materials_.push_back(std::move(bark));

        auto oak = MakeDielectric(
            MaterialId::OiledOakTimber, MaterialFamily::OrganicWoodBarkLeather,
            "OiledOakTimber", "Hand-planed oak timber finished with warm linseed clearcoat",
            {0.26f, 0.165f, 0.095f}, 1.50f, 0.0f, {2.0f, 2.0f, 2.0f},
            0.46f, 0.0f, 0.48f, 0.14f, 0.0f, 0.30f, 0.10f);
        oak.anisotropy = 0.68f;
        materials_.push_back(std::move(oak));

        auto leather = MakeDielectric(
            MaterialId::TannedLeather, MaterialFamily::OrganicWoodBarkLeather,
            "TannedLeather", "Vegetable-tanned full-grain oiled leather with pebble micro-relief",
            {0.19f, 0.11f, 0.065f}, 1.48f, 0.0f, {2.5f, 2.5f, 2.5f},
            0.42f, 0.01f, 0.36f, 0.16f, 0.0f, 0.35f, 0.15f);
        leather.anisotropy = 0.12f;
        materials_.push_back(std::move(leather));
    }

    // 8. Snow, Slush & Botanical Conifers
    materials_.push_back(MakeDielectric(
        MaterialId::FreshAlpineSnow, MaterialFamily::SnowBotanical,
        "FreshAlpineSnow", "Dry crystalline alpine snow with ice micro-glints and blue subsurface scatter",
        {0.82f, 0.86f, 0.91f}, 1.31f, 0.0028f, {0.35f, 0.12f, 0.05f},
        0.64f, 0.24f, 0.0f, 0.12f, 0.0f));
    materials_.push_back(MakeDielectric(
        MaterialId::ThawedSlush, MaterialFamily::SnowBotanical,
        "ThawedSlush", "Wet melting snow slush with glossy water clearcoat",
        {0.48f, 0.54f, 0.60f}, 1.33f, 0.0030f, {0.42f, 0.18f, 0.09f},
        0.22f, 0.10f, 0.72f, 0.06f, 0.0f));
    materials_.push_back(MakeDielectric(
        MaterialId::NordicSpruceFoliage, MaterialFamily::SnowBotanical,
        "NordicSpruceFoliage", "Old-growth boreal spruce needles with chlorophyll subsurface transmission and epicuticular wax",
        {0.085f, 0.195f, 0.075f}, 1.44f, 0.0f, {0.95f, 0.28f, 1.15f},
        0.58f, 0.06f, 0.22f, 0.18f, 0.0f));
}

bool MaterialRegistry::RegisterMaterial(PhysicalMaterial mat, std::string* outErr) {
    if (!ValidateMaterialPhysics(mat, outErr)) {
        return false;
    }
    for (auto& existing : materials_) {
        if (existing.id == mat.id || existing.name == mat.name) {
            existing = std::move(mat);
            return true;
        }
    }
    materials_.push_back(std::move(mat));
    return true;
}

const PhysicalMaterial* MaterialRegistry::FindById(MaterialId id) const {
    for (const auto& m : materials_) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

const PhysicalMaterial* MaterialRegistry::FindByName(std::string_view name) const {
    for (const auto& m : materials_) {
        if (m.name == name) return &m;
    }
    return nullptr;
}

std::vector<const PhysicalMaterial*> MaterialRegistry::ListByFamily(MaterialFamily family) const {
    std::vector<const PhysicalMaterial*> out;
    for (const auto& m : materials_) {
        if (m.family == family) out.push_back(&m);
    }
    return out;
}

std::vector<GpuMaterialRecord> MaterialRegistry::BuildGpuBuffer() const {
    std::vector<GpuMaterialRecord> out;
    out.reserve(materials_.size());
    for (const auto& m : materials_) {
        out.push_back(PackGpuMaterialRecord(m));
    }
    return out;
}

std::string MaterialRegistry::ExportAllToJson() const {
    std::ostringstream os;
    os << "{\"schema\":\"raw.materials.v1\",\"count\":" << materials_.size() << ",\"materials\":[";
    for (size_t i = 0; i < materials_.size(); ++i) {
        if (i > 0) os << ",";
        os << ExportMaterialToJson(materials_[i]);
    }
    os << "]}";
    return os.str();
}

const MaterialRegistry& DefaultMaterialRegistry() {
    static const MaterialRegistry kInstance;
    return kInstance;
}

}  // namespace raw::materials
