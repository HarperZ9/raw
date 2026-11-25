// RawFloraEngine.cpp: First-Principles Biomechanical Botany & 3D Procedural Tree Engine (`raw::worldgen`).
// Implements Runions/Prusinkiewicz 3D space colonization, phototropism, gravitropism, prevailing wind shear,
// Leonardian da Vinci pipe-model radius propagation (r_parent^p = sum r_child^p), terrain-slope buttress
// root flaring, shaded lower-limb self-pruning with stub knots, helical bark relief, real 3D volumetric
// foliage whorls/broadleaf fans, 64v/126t GPU meshlets, 3-tier LOD chains, and .obj/.glb export.
// Original work, MIT.

#include "RawFloraEngine.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numeric>

namespace raw::worldgen {
namespace {

constexpr float kPi          = 3.14159265358979323846f;
constexpr float kTwoPi       = 6.28318530717958647692f;
constexpr float kGoldenAngle = 2.39996322972865332223f;

struct FloraRng {
    uint32_t state = 0x6D2B79F5u;
    explicit FloraRng(uint32_t seed) : state(seed ? seed : 0x6D2B79F5u) {}

    uint32_t NextU32() {
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    float Next01() {
        return float(NextU32() & 0x00FFFFFFu) * (1.0f / 16777216.0f);
    }

    float Range(float lo, float hi) {
        return lo + (hi - lo) * Next01();
    }
};

struct SpeciesProfile {
    const char* name;
    float       defaultHeightM;
    float       defaultCrownRadiusM;
    float       crownBaseFrac;       // Fraction of height where main live crown begins
    float       pruneHeightFrac;     // Lower trunk fraction eligible for self-pruning stub knots
    float       apicalDominance;     // [0..1] 1=strong spire leader, 0=broad decurrent dome
    float       phototropism;        // Pull toward sunDir
    float       gravitropism;        // Positive = upward growth bias, negative = weeping/drooping
    float       windShearScale;      // Sensitivity to prevailingWindXY
    float       trunkTwistRadPerM;   // Helical bark grain twist rate
    float       barkRidgeAmp;        // Longitudinal bark flute depth fraction of radius
    float       barkFluteFreq;       // Number of primary helical flutes around circumference
    float       buttressFlare;       // Exponential base flare multiplier
    uint32_t    rootButtressCount;   // Number of terrain-gripping buttress roots
    uint32_t    minStems;            // 1 for single-trunk trees, 2-3 Birch, 5-8 Shrub
    uint32_t    maxStems;
    float       tipRadiusM;          // Biological terminal twig radius in meters
    float       girthScale;          // Trunk thickness factor
    bool        isConifer;           // True = 3D needle whorls/pads; false = 3D broadleaf/fern fans
    bool        isBirchBark;         // White bark with dark horizontal lenticel bands
    bool        isPendulous;         // Weeping drooping terminal shoots (Birch)
    bool        isWindsweptPads;     // Stratified downwind bonsai-like canopy pads (CoastalPine)
    bool        isUnderstoryShrub;   // Radiating articulated fern/juniper multi-stem cluster
};

constexpr SpeciesProfile kSpeciesProfiles[5] = {
    // 0: OldGrowthSitkaSpruce — towering 22-35m boreal spire, drooping layered boughs, fissured dark bark, north moss
    {
        "OldGrowthSitkaSpruce",
        28.0f, 5.8f, 0.18f, 0.28f,
        0.82f, 0.16f, -0.22f, 0.22f,
        0.42f, 0.12f, 6.0f, 0.72f, 5u, 1u, 1u,
        0.022f, 1.05f,
        true, false, false, false, false
    },
    // 1: WindsweptCoastalPine — gnarled leaning twisted trunk gripping granite, sculpted downwind canopy pads
    {
        "WindsweptCoastalPine",
        14.5f, 6.4f, 0.36f, 0.38f,
        0.34f, 0.24f, -0.06f, 0.92f,
        0.95f, 0.14f, 5.0f, 0.95f, 6u, 1u, 1u,
        0.026f, 1.18f,
        true, false, false, true, false
    },
    // 2: NordicSilverBirch — slender multi-stem white bark with dark lenticel bands & graceful pendulous twigs
    {
        "NordicSilverBirch",
        17.5f, 4.3f, 0.34f, 0.24f,
        0.64f, 0.28f, -0.34f, 0.35f,
        0.24f, 0.045f, 4.0f, 0.36f, 4u, 2u, 3u,
        0.015f, 0.76f,
        false, true, true, false, false
    },
    // 3: AncientGnarledOak — massive fluted buttress roots, wide spreading multi-order crown, broadleaf fans
    {
        "AncientGnarledOak",
        21.0f, 9.6f, 0.25f, 0.26f,
        0.22f, 0.34f, 0.08f, 0.26f,
        0.58f, 0.16f, 7.0f, 1.18f, 7u, 1u, 1u,
        0.024f, 1.38f,
        false, false, false, false, false
    },
    // 4: AlpineUnderstoryShrub — dense multi-stemmed fern/juniper/bush cluster with per-stem bend weights
    {
        "AlpineUnderstoryShrub",
        1.85f, 1.65f, 0.12f, 0.0f,
        0.38f, 0.30f, -0.18f, 0.45f,
        0.30f, 0.05f, 4.0f, 0.22f, 4u, 6u, 8u,
        0.012f, 0.68f,
        false, false, false, false, true
    },
};

inline Vec3 SafeNormalize(Vec3 v, Vec3 fallback = {0.0f, 0.0f, 1.0f}) {
    const float len = Length(v);
    if (!std::isfinite(len) || len < 1.0e-6f) return fallback;
    return {v.x / len, v.y / len, v.z / len};
}

inline void BuildOrthoBasis(Vec3 axis, Vec3* outT, Vec3* outB) {
    const Vec3 n = SafeNormalize(axis);
    const Vec3 ref = (std::fabs(n.z) < 0.92f) ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{1.0f, 0.0f, 0.0f};
    const Vec3 t = SafeNormalize(Cross(ref, n), {1.0f, 0.0f, 0.0f});
    const Vec3 b = SafeNormalize(Cross(n, t), {0.0f, 1.0f, 0.0f});
    *outT = t;
    *outB = b;
}

inline Vec3 TransportFrame(Vec3 prevT, Vec3 newAxis) {
    const Vec3 n = SafeNormalize(newAxis);
    Vec3 proj = prevT - n * Dot(prevT, n);
    if (Length(proj) < 1.0e-4f) {
        Vec3 t{}, b{};
        BuildOrthoBasis(n, &t, &b);
        return t;
    }
    return SafeNormalize(proj);
}

inline float LocalGroundZ(float x, float y, Vec3 terrainN) {
    const float nz = std::max(terrainN.z, 0.25f);
    return -(terrainN.x * x + terrainN.y * y) / nz;
}

// Normalizes the 8-channel TerrainVertex splat weights to sum strictly to 1.0.
inline void NormalizeSplat8(TerrainVertex& v) {
    float sum = 0.0f;
    for (int i = 0; i < 4; ++i) {
        v.splat0[i] = std::max(0.0f, v.splat0[i]);
        v.splat1[i] = std::max(0.0f, v.splat1[i]);
        sum += v.splat0[i] + v.splat1[i];
    }
    if (sum > 1.0e-5f) {
        const float inv = 1.0f / sum;
        for (int i = 0; i < 4; ++i) {
            v.splat0[i] *= inv;
            v.splat1[i] *= inv;
        }
    } else {
        v.splat0[0] = 1.0f;
    }
}

// Encodes physically-plausible bark splats compatible with RawTerrainWorld.hlsl:
//   splat0.x = RockGranite (fissured bark), splat0.y = SnowPack (snow / white birch bark),
//   splat0.z = GlacialIce (pale alpine lichen), splat0.w = BasaltColumn (dark bark crevices / birch lenticels),
//   splat1.y = MossMeadow (north-facing trunk moss), splat1.z = RiverPebbleSand (warm woody bark brown).
TerrainVertex MakeBarkVertex(Vec3 pos, Vec3 normal, Vec2 uv,
                             const SpeciesProfile& prof, FloraSpecies species,
                             float snowLoad01, float age01,
                             float curvAo, float bendFlex, float phaseHash,
                             float normHeight, uint32_t seed) {
    TerrainVertex v{};
    v.splat0[0] = 0.0f; // Clear TerrainVertex default Rock=1.0 before computing bark splats
    const Vec3 n = SafeNormalize(normal);
    v.pos[0] = pos.x; v.pos[1] = pos.y; v.pos[2] = pos.z;
    v.normal[0] = n.x; v.normal[1] = n.y; v.normal[2] = n.z;
    v.uv[0] = uv.x; v.uv[1] = uv.y;

    const float northMoss = Smoothstep(0.18f, 0.72f, n.y) * (1.0f - Smoothstep(0.35f, 0.85f, normHeight))
                          * (0.35f + 0.45f * age01);
    const float snowTop = (n.z > 0.45f && snowLoad01 > 0.01f)
                        ? Smoothstep(0.45f, 0.86f, n.z) * snowLoad01 * 0.78f
                        : 0.0f;
    const float lichen = Smoothstep(0.25f, 0.75f, n.z) * age01 * 0.14f
                       * (0.5f + 0.5f * std::sin(pos.x * 0.07f + pos.z * 0.05f + float(seed & 31u)));

    if (prof.isBirchBark) {
        // Silver birch: crisp white outer bark punctuated by horizontal dark lenticel bands!
        const float bandWave = std::sin(uv.y * 42.0f + std::sin(uv.x * kTwoPi * 2.0f + float(seed & 15u)) * 1.4f);
        const float lenticel = Smoothstep(0.62f, 0.88f, bandWave) + Smoothstep(0.14f, 0.0f, normHeight) * 0.65f;
        const float darkBand = Clamp01(lenticel);
        v.splat0[1] = std::max(0.78f * (1.0f - darkBand), snowTop); // White birch bark / snow
        v.splat0[3] = 0.58f * darkBand;                              // Dark charcoal lenticels
        v.splat1[2] = 0.24f * darkBand + 0.08f;                      // Warm papery underbark
        v.splat1[1] = northMoss * 0.35f;                             // Subtle basal moss
        v.splat0[2] = lichen * 0.30f;
    } else if (prof.isConifer) {
        // Dark fissured Sitka / Coastal Pine reddish-brown & charcoal bark + north moss
        const float fissure = Clamp01(0.5f - curvAo * 0.7f);
        v.splat0[0] = 0.32f * (1.0f - snowTop);
        v.splat0[3] = (0.38f + 0.24f * fissure) * (1.0f - snowTop);
        v.splat1[2] = 0.26f * (1.0f - snowTop);
        v.splat1[1] = northMoss * (prof.isWindsweptPads ? 0.45f : 0.72f);
        v.splat0[2] = lichen;
        v.splat0[1] = snowTop;
    } else {
        // Ancient Oak / Understory woody bark: warm brown-grey deeply furrowed bark + heavy old-growth moss
        const float fissure = Clamp01(0.5f - curvAo * 0.65f);
        v.splat0[0] = 0.36f * (1.0f - snowTop);
        v.splat0[3] = (0.24f + 0.22f * fissure) * (1.0f - snowTop);
        v.splat1[2] = 0.34f * (1.0f - snowTop);
        v.splat1[1] = northMoss * 0.62f;
        v.splat0[2] = lichen * 1.15f;
        v.splat0[1] = snowTop;
    }

    NormalizeSplat8(v);
    v.meta[0] = Clamp(curvAo, -1.0f, 1.0f);
    v.meta[1] = Clamp01(bendFlex);
    v.meta[2] = Clamp01(phaseHash);
    v.meta[3] = float(static_cast<uint32_t>(species));
    return v;
}

// Encodes 3D foliage cluster vertex attributes (Chlorophyll + Autumn Carotenoid/Anthocyanin + SnowOnTopBoughs + branch AO + bend).
TerrainVertex MakeFoliageVertex(Vec3 pos, Vec3 normal, Vec2 uv,
                                const FloraGrowthParams& params, bool isConifer,
                                float curvAo, float bendFlex, float phaseHash,
                                float tipTint01) {
    TerrainVertex v{};
    v.splat0[0] = 0.0f; // Clear TerrainVertex default Rock=1.0 before computing foliage splats
    const Vec3 n = SafeNormalize(normal);
    v.pos[0] = pos.x; v.pos[1] = pos.y; v.pos[2] = pos.z;
    v.normal[0] = n.x; v.normal[1] = n.y; v.normal[2] = n.z;
    v.uv[0] = uv.x; v.uv[1] = uv.y;

    const float effSnow = Clamp01(std::max(params.snowLoad01, params.canopySnowLoad01));
    // Snow accumulates physically on upper-facing bough surfaces (N.z > 0.45)!
    const float snowMask = (n.z > 0.45f && effSnow > 0.01f)
                         ? Smoothstep(0.45f, 0.85f, n.z) * effSnow * 0.84f
                         : 0.0f;

    // Seasonal phenology pigment modulation:
    //   - Conifers retain evergreen chlorophyll (only subtle 10% winter bronze shift).
    //   - Deciduous species (Birch/Oak/Shrub) transition cleanly from chlorophyll green (splat1.y)
    //     to golden carotenoid (splat1.z warm ochre) and russet-crimson anthocyanin (splat1.z + splat0.x, zero magma!).
    const float chloro = Clamp01(params.chlorophyll01);
    const float carot  = Clamp01(params.autumnCarotenoid01);
    const float antho  = Clamp01(params.autumnAnthocyanin01);
    const float decidScale = isConifer ? 0.10f : 1.0f;
    const float autumnShift = Clamp01(std::max(1.0f - chloro, (carot + antho) * (0.78f + 0.44f * tipTint01)) * decidScale);

    const float rem = std::max(1.0f - snowMask, 0.05f);
    const float greenBase = (0.78f + 0.14f * (1.0f - tipTint01)) * (1.0f - autumnShift * 0.82f);
    const float warmOchre = (0.08f + 0.14f * tipTint01) + autumnShift * (0.58f * carot + 0.34f * antho + 0.18f);
    const float russetRed = autumnShift * (0.42f * antho + 0.08f * carot);

    v.splat0[0] = rem * russetRed;                                                                  // Warm russet-crimson mineral tone
    v.splat0[1] = snowMask;                                                                         // SnowOnTopBoughs (N.z > 0.45)
    v.splat0[2] = rem * (0.02f + 0.03f * effSnow);                                                  // Frost/waxy cuticle sheen
    v.splat0[3] = rem * ((params.species == FloraSpecies::OldGrowthSitkaSpruce) ? 0.10f : 0.04f);   // Deep pine interior shade
    v.splat1[0] = 0.0f;                                                                             // Strictly 0 magma on flora!
    v.splat1[1] = rem * std::max(greenBase, 0.04f);                                                 // Chlorophyll green (SSS backlit)
    v.splat1[2] = rem * warmOchre;                                                                  // Golden carotenoid / warm tip tint
    v.splat1[3] = 0.0f;

    NormalizeSplat8(v);
    v.meta[0] = Clamp(curvAo, -1.0f, 1.0f);
    v.meta[1] = Clamp01(bendFlex);
    v.meta[2] = Clamp01(phaseHash);
    v.meta[3] = float(static_cast<uint32_t>(params.species));
    return v;
}

// ============================================================================
// Step 1: Biological Attraction Cloud & Space-Colonization Skeleton Builder
// ============================================================================
struct Attractor {
    Vec3 pos{};
    bool active = true;
};

void GenerateAttractionCloud(const SpeciesProfile& prof, const FloraGrowthParams& params,
                             float heightU, float crownRadiusU, Vec2 windDir, float windMag,
                             FloraRng& rng, std::vector<Attractor>* outAttractors) {
    outAttractors->clear();
    const uint32_t lod = std::min(params.lodLevel, 2u);
    uint32_t count = (lod == 0u) ? 185u : ((lod == 1u) ? 84u : 36u);
    if (prof.isUnderstoryShrub) {
        count = (lod == 0u) ? 90u : ((lod == 1u) ? 46u : 22u);
    }
    outAttractors->reserve(count);

    const float zMin = heightU * prof.crownBaseFrac;
    const float zMax = heightU * 1.02f;

    for (uint32_t i = 0; i < count; ++i) {
        const float u = rng.Next01();
        const float z = Lerp(zMin, zMax, u);
        const float tCrown = Clamp01((z - zMin) / std::max(zMax - zMin, 1.0f));
        float envR = crownRadiusU;

        if (prof.isConifer && !prof.isWindsweptPads) {
            // Sitka Spruce: tall cascading conical-paraboloid spire with distinct tiered whorls
            envR = crownRadiusU * std::pow(std::max(1.0f - tCrown * 0.94f, 0.06f), 0.72f);
        } else if (prof.isWindsweptPads) {
            // Windswept Coastal Pine: sculpted layered flat-topped pads swept strongly downwind
            const float padLayer = std::floor(tCrown * 3.0f) / 3.0f;
            envR = crownRadiusU * (0.48f + 0.56f * std::sin(Clamp01(tCrown * 0.9f + 0.1f) * kPi));
            const float padZ = Lerp(zMin, zMax, padLayer + 0.14f + rng.Range(-0.05f, 0.05f));
            const float ang = rng.Range(0.0f, kTwoPi);
            const float rad = envR * std::sqrt(rng.Next01());
            // Shift attractors asymmetrically downwind to sculpt flag-crown pads!
            const float windShift = crownRadiusU * (0.32f + 0.58f * tCrown) * Clamp(windMag, 0.2f, 1.6f);
            outAttractors->push_back({{std::cos(ang) * rad + windDir.x * windShift,
                                       std::sin(ang) * rad + windDir.y * windShift,
                                       padZ}, true});
            continue;
        } else if (prof.isBirchBark) {
            // Silver Birch: slender graceful oval dome
            envR = crownRadiusU * (0.35f + 0.72f * std::sin(Clamp01(tCrown) * kPi * 0.92f + 0.12f));
        } else if (prof.isUnderstoryShrub) {
            // Understory Shrub: hemispherical radiating bush dome
            envR = crownRadiusU * std::sin(Clamp01(0.15f + 0.85f * tCrown) * kPi * 0.85f);
        } else {
            // Ancient Gnarled Oak: wide majestic umbrella-dome crown
            envR = crownRadiusU * (0.42f + 0.68f * std::sin(std::pow(tCrown, 0.72f) * kPi * 0.92f + 0.10f));
        }

        const float ang = rng.Range(0.0f, kTwoPi);
        const float rad = envR * std::pow(rng.Next01(), 0.45f);
        const float windBias = crownRadiusU * 0.18f * tCrown * prof.windShearScale * Clamp(windMag, 0.0f, 1.6f);
        outAttractors->push_back({{std::cos(ang) * rad + windDir.x * windBias,
                                   std::sin(ang) * rad + windDir.y * windBias,
                                   z}, true});
    }
}

int32_t AddBranchNode(std::vector<BranchNode>& nodes, int32_t parentIdx, Vec3 pos, Vec3 dir,
                      uint8_t order, uint8_t stemId, float heightU, float phaseHash, bool isStub = false) {
    BranchNode n{};
    n.parentIndex      = parentIdx;
    n.position         = pos;
    n.direction        = SafeNormalize(dir);
    n.normalizedHeight = Clamp01(pos.z / std::max(heightU, 1.0f));
    n.windPhaseHash    = Clamp01(phaseHash);
    n.branchOrder      = order;
    n.stemId           = stemId;
    n.isDeadStub       = isStub;
    const int32_t idx = static_cast<int32_t>(nodes.size());
    nodes.push_back(n);
    if (parentIdx >= 0 && parentIdx < idx) {
        nodes[parentIdx].children.push_back(idx);
    }
    return idx;
}

void BuildBiologicalSkeleton(const SpeciesProfile& prof, const FloraGrowthParams& params,
                             float heightU, float crownRadiusU,
                             FloraRng& rng, std::vector<BranchNode>* outNodes,
                             FloraBuildStats* outStats) {
    outNodes->clear();
    const uint32_t lod = std::min(params.lodLevel, 2u);
    const Vec3 sunDir = SafeNormalize(params.sunDir, {0.35f, -0.45f, 0.82f});
    const float windMag = Length(params.prevailingWindXY);
    const Vec2 windDir = (windMag > 1.0e-4f) ? Vec2{params.prevailingWindXY.x / windMag, params.prevailingWindXY.y / windMag}
                                             : Vec2{1.0f, 0.0f};
    const float age01 = Clamp01(params.ageFactor01);
    const float snow01 = Clamp01(std::max(params.snowLoad01, params.canopySnowLoad01));

    // 1. Seed root collar node at origin (0, 0, 0)
    const int32_t rootIdx = AddBranchNode(*outNodes, -1, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
                                          0u, 0u, heightU, rng.Next01(), false);

    // 2. Grow primary trunk(s) / multi-stems with phototropism, wind lean, and organic S-curve gnarl
    uint32_t stemCount = prof.minStems;
    if (prof.maxStems > prof.minStems) {
        const uint32_t span = prof.maxStems - prof.minStems + 1u;
        stemCount = prof.minStems + (rng.NextU32() % span);
    }
    if (prof.isUnderstoryShrub) {
        if (lod == 1u) stemCount = std::max(4u, stemCount - 2u);
        if (lod == 2u) stemCount = std::max(3u, stemCount / 2u);
    } else if (prof.isBirchBark && lod == 2u) {
        stemCount = 2u;
    }
    outStats->stemCount = stemCount;

    const uint32_t trunkSteps = prof.isUnderstoryShrub
                              ? ((lod == 0u) ? 5u : ((lod == 1u) ? 4u : 3u))
                              : ((lod == 0u) ? 10u : ((lod == 1u) ? 7u : 5u));

    for (uint32_t s = 0; s < stemCount; ++s) {
        const float stemPhase = rng.Next01();
        const float stemAz = (float(s) / float(std::max(stemCount, 1u))) * kTwoPi + rng.Range(-0.28f, 0.28f);
        const float stemScaleH = (s == 0u) ? 1.0f : rng.Range(0.74f, 0.94f);
        const float targetStemH = heightU * stemScaleH * (prof.isConifer && !prof.isWindsweptPads ? 0.98f : 0.80f);
        const float stepLen = targetStemH / float(trunkSteps);

        Vec3 curPos{0.0f, 0.0f, 0.0f};
        Vec3 curDir{0.0f, 0.0f, 1.0f};
        if (stemCount > 1u) {
            const float spread = prof.isUnderstoryShrub ? rng.Range(0.45f, 0.88f) : rng.Range(0.14f, 0.28f);
            curDir = SafeNormalize({std::cos(stemAz) * spread, std::sin(stemAz) * spread, 1.0f});
        }

        int32_t prev = rootIdx;
        const float gnarlPhase = rng.Range(0.0f, kTwoPi);
        for (uint32_t k = 1u; k <= trunkSteps; ++k) {
            const float t = float(k) / float(trunkSteps);
            // Organic trunk sway + prevailing wind lean + sunlight phototropism
            const float gnarlAmp = prof.isWindsweptPads ? 0.26f : (prof.apicalDominance > 0.7f ? 0.05f : 0.16f);
            const Vec3 gnarlVec{
                std::cos(gnarlPhase + t * 4.2f) * gnarlAmp,
                std::sin(gnarlPhase + t * 3.7f) * gnarlAmp,
                0.0f};
            const Vec3 windLean{
                windDir.x * prof.windShearScale * Clamp(windMag, 0.0f, 1.6f) * (0.12f + 0.55f * t),
                windDir.y * prof.windShearScale * Clamp(windMag, 0.0f, 1.6f) * (0.12f + 0.55f * t),
                0.0f};
            const Vec3 sunPull = sunDir * (prof.phototropism * 0.18f);
            const Vec3 gravPull{0.0f, 0.0f, prof.isUnderstoryShrub ? (-0.28f * t) : (prof.apicalDominance * 0.45f)};

            curDir = SafeNormalize(curDir + gnarlVec + windLean * 0.22f + sunPull + gravPull);
            curPos = curPos + curDir * stepLen;
            if (curPos.z < stepLen * 0.25f) curPos.z = stepLen * 0.25f;
            prev = AddBranchNode(*outNodes, prev, curPos, curDir, 0u, static_cast<uint8_t>(s),
                                 heightU, stemPhase, false);
        }
    }

    // 3. Seed biological primary bough scaffold (ensures iconic tiered whorls / oak forks before colonization)
    const uint32_t scaffoldParentCount = static_cast<uint32_t>(outNodes->size());
    if (!prof.isUnderstoryShrub) {
        const uint32_t whorlCount = (lod == 0u) ? 16u : ((lod == 1u) ? 9u : 5u);
        for (uint32_t w = 0; w < whorlCount; ++w) {
            const float u = float(w) / float(std::max(whorlCount - 1u, 1u));
            const float targetFrac = Lerp(prof.crownBaseFrac, 0.93f, u);
            // Find closest trunk node to targetFrac
            int32_t bestTrunk = 0;
            float bestDiff = 1.0e9f;
            for (uint32_t ni = 1u; ni < scaffoldParentCount; ++ni) {
                const float d = std::fabs((*outNodes)[ni].normalizedHeight - targetFrac);
                if (d < bestDiff) {
                    bestDiff = d;
                    bestTrunk = static_cast<int32_t>(ni);
                }
            }
            const BranchNode anchor = (*outNodes)[bestTrunk];
            const float az = float(w) * kGoldenAngle + rng.Range(-0.22f, 0.22f);
            float env = 1.0f;
            if (prof.isConifer && !prof.isWindsweptPads) {
                env = std::pow(std::max(1.0f - u * 0.88f, 0.14f), 0.72f);
            } else {
                env = 0.45f + 0.60f * std::sin(u * kPi * 0.90f + 0.15f);
            }
            const float boughLen = crownRadiusU * (0.42f + 0.52f * env) * rng.Range(0.84f, 1.12f);
            const float pitch = prof.isConifer
                              ? (Lerp(-0.22f, 0.24f, u) - 0.18f * snow01)
                              : Lerp(0.24f, 0.56f, u);

            Vec3 bDir = SafeNormalize({
                std::cos(az) + windDir.x * prof.windShearScale * Clamp(windMag, 0.0f, 1.5f) * 0.55f,
                std::sin(az) + windDir.y * prof.windShearScale * Clamp(windMag, 0.0f, 1.5f) * 0.55f,
                pitch});

            const uint32_t segs = (lod == 0u) ? 3u : 2u;
            int32_t cur = bestTrunk;
            Vec3 p = anchor.position;
            const float bPhase = rng.Next01();
            for (uint32_t seg = 1u; seg <= segs; ++seg) {
                const float st = float(seg) / float(segs);
                const float droop = (prof.isConifer ? (0.14f + 0.20f * snow01) : (prof.isPendulous ? 0.35f : -0.05f)) * st;
                bDir = SafeNormalize(bDir + Vec3{0.0f, 0.0f, -droop} + sunDir * (prof.phototropism * 0.10f));
                p = p + bDir * (boughLen / float(segs));
                cur = AddBranchNode(*outNodes, cur, p, bDir, 1u, anchor.stemId, heightU, bPhase, false);
            }
        }
    }

    // 4. Genuine 3D Runions/Prusinkiewicz Space Colonization Loop!
    std::vector<Attractor> attractors;
    GenerateAttractionCloud(prof, params, heightU, crownRadiusU, windDir, windMag, rng, &attractors);

    const float stepD     = std::max(crownRadiusU * ((lod == 0u) ? 0.19f : ((lod == 1u) ? 0.26f : 0.34f)), heightU * 0.032f);
    const float killDist  = stepD * 1.45f;
    const float inflDist  = std::max(crownRadiusU * 1.25f, stepD * 5.2f);
    const float killSq    = killDist * killDist;
    const float inflSq    = inflDist * inflDist;
    const uint32_t maxNodes = prof.isUnderstoryShrub
                            ? ((lod == 0u) ? 140u : ((lod == 1u) ? 72u : 36u))
                            : ((lod == 0u) ? 260u : ((lod == 1u) ? 120u : 56u));
    const uint32_t maxIters = (lod == 0u) ? 14u : ((lod == 1u) ? 10u : 6u);

    std::vector<Vec3> pullSum;
    std::vector<uint32_t> pullCnt;

    for (uint32_t iter = 0u; iter < maxIters && outNodes->size() < maxNodes; ++iter) {
        const size_t nCount = outNodes->size();
        pullSum.assign(nCount, Vec3{0.0f, 0.0f, 0.0f});
        pullCnt.assign(nCount, 0u);

        uint32_t activeCount = 0u;
        for (auto& a : attractors) {
            if (!a.active) continue;
            float bestSq = inflSq;
            int32_t bestNode = -1;
            for (size_t ni = 1u; ni < nCount; ++ni) {
                const BranchNode& bn = (*outNodes)[ni];
                if (bn.isDeadStub) continue;
                const Vec3 diff = a.pos - bn.position;
                const float d2 = Dot(diff, diff);
                if (d2 < killSq) {
                    a.active = false;
                    bestNode = -1;
                    break;
                }
                if (d2 < bestSq) {
                    bestSq = d2;
                    bestNode = static_cast<int32_t>(ni);
                }
            }
            if (a.active && bestNode >= 0) {
                const Vec3 dir = SafeNormalize(a.pos - (*outNodes)[bestNode].position);
                pullSum[bestNode] = pullSum[bestNode] + dir;
                pullCnt[bestNode] += 1u;
                ++activeCount;
            }
        }
        if (activeCount == 0u) break;

        bool spawnedAny = false;
        for (size_t ni = 1u; ni < nCount && outNodes->size() < maxNodes; ++ni) {
            if (pullCnt[ni] == 0u) continue;
            const BranchNode parent = (*outNodes)[ni];
            const Vec3 attractDir = SafeNormalize(pullSum[ni]);

            // Combine space-colonization pull + phototropism + gravitropism + prevailing wind shear + momentum
            const Vec3 gravVec{0.0f, 0.0f, prof.gravitropism - (prof.isConifer ? 0.18f * snow01 : 0.0f)};
            const Vec3 windVec{windDir.x * prof.windShearScale * Clamp(windMag, 0.0f, 1.5f) * 0.25f,
                               windDir.y * prof.windShearScale * Clamp(windMag, 0.0f, 1.5f) * 0.25f, 0.0f};
            Vec3 growDir = SafeNormalize(
                attractDir * 0.62f +
                parent.direction * 0.28f +
                sunDir * (prof.phototropism * 0.20f) +
                gravVec * 0.24f +
                windVec);

            const Vec3 nextPos = parent.position + growDir * stepD;
            // Increment branch order if this is a fork or significant angular deviation
            const bool isFork = !parent.children.empty() || Dot(growDir, parent.direction) < 0.78f;
            const uint8_t nextOrder = static_cast<uint8_t>(std::min<uint32_t>(
                uint32_t(parent.branchOrder) + (isFork ? 1u : 0u), 3u));
            const float nextPhase = isFork ? rng.Next01() : parent.windPhaseHash;
            AddBranchNode(*outNodes, static_cast<int32_t>(ni), nextPos, growDir,
                          nextOrder, parent.stemId, heightU, nextPhase, false);
            spawnedAny = true;
        }
        if (!spawnedAny) break;
    }

    // 5. Pendulous weeping twigs for NordicSilverBirch (drooping graceful terminal curtain!)
    if (prof.isPendulous && lod <= 1u) {
        const size_t baseN = outNodes->size();
        const float weepStep = heightU * 0.048f;
        for (size_t ni = 1u; ni < baseN && outNodes->size() < maxNodes + 36u; ++ni) {
            const BranchNode bn = (*outNodes)[ni];
            if (bn.branchOrder >= 1u && bn.children.empty() && bn.normalizedHeight > 0.42f) {
                int32_t cur = static_cast<int32_t>(ni);
                Vec3 p = bn.position;
                Vec3 d = SafeNormalize({bn.direction.x * 0.45f, bn.direction.y * 0.45f, -0.88f});
                const uint32_t drops = (lod == 0u) ? 2u : 1u;
                for (uint32_t k = 0; k < drops; ++k) {
                    p = p + d * weepStep;
                    d = SafeNormalize({d.x * 0.6f + windDir.x * 0.15f, d.y * 0.6f + windDir.y * 0.15f, -0.95f});
                    cur = AddBranchNode(*outNodes, cur, p, d, 3u, bn.stemId, heightU, bn.windPhaseHash, false);
                }
            }
        }
    }

    // 6. Biological Self-Pruning: shaded lower limbs die off leaving gnarled stub knots!
    uint32_t prunedCount = 0u;
    if (prof.pruneHeightFrac > 0.05f && age01 > 0.25f) {
        // First, prune any non-trunk branches growing inside the deep shaded understory zone
        for (size_t ni = 1u; ni < outNodes->size(); ++ni) {
            BranchNode& bn = (*outNodes)[ni];
            if (bn.branchOrder >= 1u && bn.normalizedHeight < prof.pruneHeightFrac * (0.75f + 0.30f * age01)) {
                bn.isDeadStub = true;
                bn.branchOrder = 4u;
                bn.hasFoliage = false;
                ++prunedCount;
            }
        }
        // Also grow 2..5 characteristic gnarled dead stub knots on the lower shaded trunk (classic Sitka/Pine/Oak detail)
        const uint32_t desiredStubs = (lod == 0u) ? (3u + uint32_t(age01 * 2.5f)) : ((lod == 1u) ? 2u : 1u);
        for (uint32_t sk = 0; sk < desiredStubs; ++sk) {
            const float targetFrac = Lerp(0.11f, std::max(prof.pruneHeightFrac, 0.22f),
                                          (float(sk) + 0.5f) / float(desiredStubs));
            int32_t bestTrunk = -1;
            float bestDiff = 1.0e9f;
            for (uint32_t ni = 1u; ni < scaffoldParentCount; ++ni) {
                if ((*outNodes)[ni].branchOrder != 0u) continue;
                const float d = std::fabs((*outNodes)[ni].normalizedHeight - targetFrac);
                if (d < bestDiff) {
                    bestDiff = d;
                    bestTrunk = static_cast<int32_t>(ni);
                }
            }
            if (bestTrunk >= 0) {
                const BranchNode anchor = (*outNodes)[bestTrunk];
                const float az = float(sk) * kGoldenAngle + rng.Range(0.3f, 1.2f);
                const Vec3 sDir = SafeNormalize({std::cos(az), std::sin(az), rng.Range(-0.42f, -0.08f)});
                const float stubLen = crownRadiusU * rng.Range(0.12f, 0.22f);
                const Vec3 midP = anchor.position + sDir * (stubLen * 0.55f);
                const int32_t midIdx = AddBranchNode(*outNodes, bestTrunk, midP, sDir, 4u, anchor.stemId,
                                                     heightU, rng.Next01(), true);
                const Vec3 kinkDir = SafeNormalize(sDir + Vec3{rng.Range(-0.25f, 0.25f), rng.Range(-0.25f, 0.25f), -0.30f});
                AddBranchNode(*outNodes, midIdx, midP + kinkDir * (stubLen * 0.45f), kinkDir, 4u, anchor.stemId,
                              heightU, rng.Next01(), true);
                ++prunedCount;
            }
        }
    }
    outStats->prunedStubCount = prunedCount;

    // Mark foliage-bearing nodes (outer crown branches & terminal twigs, excluding dead stubs & bare lower trunk)
    for (size_t ni = 1u; ni < outNodes->size(); ++ni) {
        BranchNode& bn = (*outNodes)[ni];
        if (bn.isDeadStub) {
            bn.hasFoliage = false;
            continue;
        }
        const bool isTip = bn.children.empty();
        const bool inCrown = bn.normalizedHeight >= prof.crownBaseFrac * 0.88f;
        if (prof.isUnderstoryShrub) {
            bn.hasFoliage = (bn.normalizedHeight > 0.16f);
        } else {
            bn.hasFoliage = inCrown && (isTip || bn.branchOrder >= 2u ||
                                        (bn.branchOrder == 1u && bn.normalizedHeight > 0.60f));
        }
    }

    // 7. Leonardian da Vinci Pipe Model Back-Propagation (r_parent^p = sum r_child^p, p = 2.5 default)
    const float expP = Clamp(params.daVinciExponent, 1.8f, 3.5f);
    const float tipRadiusU = prof.tipRadiusM * params.unitsPerMeter * (0.85f + 0.30f * age01);

    // Compute topological depth (root = 0) so we can process strictly bottom-up (post-order) without recursion limits.
    std::vector<uint32_t> depth(outNodes->size(), 0u);
    std::vector<int32_t> order(outNodes->size());
    std::iota(order.begin(), order.end(), 0);
    for (size_t ni = 1u; ni < outNodes->size(); ++ni) {
        const int32_t p = (*outNodes)[ni].parentIndex;
        depth[ni] = (p >= 0) ? (depth[p] + 1u) : 0u;
    }
    std::sort(order.begin(), order.end(), [&](int32_t a, int32_t b) {
        return depth[a] > depth[b]; // Deepest leaves first
    });

    float minTipR = std::numeric_limits<float>::max();
    float maxTipR = 0.0f;
    for (int32_t ni : order) {
        BranchNode& bn = (*outNodes)[ni];
        if (bn.children.empty()) {
            bn.radius = bn.isDeadStub ? (tipRadiusU * 1.12f) : tipRadiusU;
            if (!bn.isDeadStub) {
                minTipR = std::min(minTipR, bn.radius);
                maxTipR = std::max(maxTipR, bn.radius);
            }
        } else {
            double sumPow = 0.0;
            float maxChildR = 0.0f;
            for (int32_t c : bn.children) {
                const float rc = (*outNodes)[c].radius;
                maxChildR = std::max(maxChildR, rc);
                sumPow += std::pow(double(rc), double(expP));
            }
            const float rPipe = static_cast<float>(std::pow(sumPow, 1.0 / double(expP)));
            // Cambium growth step guarantees smooth organic taper & strict r_parent > r_child everywhere.
            const float cambiumMin = maxChildR * 1.035f + tipRadiusU * 0.055f;
            bn.radius = std::max(rPipe, cambiumMin);
        }
    }

    // Apply species girth scaling + monotonically decreasing topological root flare (preserves strict monotonicity!)
    for (size_t ni = 0u; ni < outNodes->size(); ++ni) {
        BranchNode& bn = (*outNodes)[ni];
        const float dNorm = float(depth[ni]);
        const float flare = 1.0f + prof.buttressFlare * (0.65f + 0.35f * age01) * std::exp(-dNorm * 0.85f);
        bn.radius *= prof.girthScale * flare;
    }

    // 8. Compute hierarchical Euler-Bernoulli cantilever bend flexibility (`bendFlex` in [0..1])
    //    and verify pipe-model monotonicity across every parent->child edge.
    std::vector<float> arcDist(outNodes->size(), 0.0f);
    float maxArc = 1.0f;
    for (size_t ni = 1u; ni < outNodes->size(); ++ni) {
        const int32_t p = (*outNodes)[ni].parentIndex;
        if (p >= 0) {
            arcDist[ni] = arcDist[p] + Length((*outNodes)[ni].position - (*outNodes)[p].position);
            maxArc = std::max(maxArc, arcDist[ni]);
        }
    }

    bool monotonic = true;
    float actualMaxZ = 0.0f;
    for (size_t ni = 0u; ni < outNodes->size(); ++ni) {
        BranchNode& bn = (*outNodes)[ni];
        actualMaxZ = std::max(actualMaxZ, bn.position.z);
        if (ni == 0u) {
            bn.bendFlex = 0.0f;
        } else {
            const float arcFrac = Clamp01(arcDist[ni] / maxArc);
            const float orderBoost = (bn.branchOrder == 0u) ? 0.0f : (0.14f * float(std::min<uint8_t>(bn.branchOrder, 3u)));
            bn.bendFlex = Clamp01(std::pow(arcFrac, 1.35f) * 0.85f + orderBoost);
        }
        for (int32_t c : bn.children) {
            if (bn.radius < (*outNodes)[c].radius - 1.0e-5f) {
                monotonic = false;
            }
        }
    }

    // 8b. Physical Winter Snow-Load Cantilever Sag (`canopySnowLoad01`):
    //     Evergreen conifers retain full winter needle area -> heavy snow bends outer boughs downward via
    //     inextensible Euler-Bernoulli cantilever deflection! Bare abscised deciduous branches catch far less snow.
    const float sagLoad = Clamp01(params.canopySnowLoad01);
    if (sagLoad > 1.0e-4f) {
        const float foliageCatch = prof.isConifer ? 1.0f : Clamp01(1.0f - params.leafAbscission01 * 0.85f);
        const float effSag = sagLoad * foliageCatch;
        if (effSag > 1.0e-4f) {
            for (size_t ni = 1u; ni < outNodes->size(); ++ni) {
                BranchNode& bn = (*outNodes)[ni];
                if (bn.branchOrder == 0u && !prof.isUnderstoryShrub) continue;
                const float leverR = std::hypot(bn.position.x, bn.position.y);
                if (leverR < 1.0e-3f) continue;
                const float rawSag = effSag * leverR * (0.26f + 0.42f * bn.bendFlex * bn.bendFlex);
                const float sagZ = std::min(rawSag, leverR * 0.56f);
                const float rNew = std::sqrt(std::max(leverR * leverR - sagZ * sagZ, 0.0f));
                const float xyScale = rNew / leverR;
                bn.position.x *= xyScale;
                bn.position.y *= xyScale;
                bn.position.z = std::max(bn.position.z - sagZ, params.unitsPerMeter * 0.15f);
                bn.normalizedHeight = Clamp01(bn.position.z / std::max(heightU, 1.0f));
                bn.direction = SafeNormalize(bn.direction + Vec3{0.0f, 0.0f, -effSag * 0.45f * bn.bendFlex});
            }
        }
    }

    double canopyZSum = 0.0;
    uint32_t canopyZCount = 0u;
    actualMaxZ = 0.0f;
    for (size_t ni = 1u; ni < outNodes->size(); ++ni) {
        const BranchNode& bn = (*outNodes)[ni];
        actualMaxZ = std::max(actualMaxZ, bn.position.z);
        if (bn.branchOrder >= 1u || prof.isUnderstoryShrub) {
            canopyZSum += double(bn.position.z);
            ++canopyZCount;
        }
    }

    outStats->totalNodes         = static_cast<uint32_t>(outNodes->size());
    outStats->rootRadiusU        = outNodes->empty() ? 0.0f : (*outNodes)[0].radius;
    outStats->minTipRadiusU      = (minTipR < 1.0e20f) ? (minTipR * prof.girthScale) : tipRadiusU;
    outStats->maxTipRadiusU      = maxTipR * prof.girthScale;
    outStats->actualHeightU      = actualMaxZ;
    outStats->meanCanopyZ        = (canopyZCount > 0u) ? float(canopyZSum / double(canopyZCount)) : actualMaxZ * 0.5f;
    outStats->pipeModelMonotonic = monotonic && (outStats->rootRadiusU > outStats->maxTipRadiusU);
}

// ============================================================================
// Step 2: Watertight Manifold Tube & Buttress Root Meshing + Helical Bark Relief
// ============================================================================
struct TubeRing {
    Vec3  center{};
    Vec3  axis{0.0f, 0.0f, 1.0f};
    float radius     = 1.0f;
    float arcLen     = 0.0f;
    float normHeight = 0.0f;
    float bendFlex   = 0.0f;
    float phaseHash  = 0.0f;
    float ridgeScale = 1.0f;
};

// Appends a closed watertight generalized cylinder with helical bark flutes,
// closed bottom pole cap & closed top pole cap, and smooth analytical vertex normals.
void AppendWatertightBarkTube(const std::vector<TubeRing>& rings, uint32_t radialSegs,
                              const SpeciesProfile& prof, const FloraGrowthParams& params,
                              float unitsPerMeter,
                              std::vector<TerrainVertex>& verts, std::vector<uint32_t>& idx) {
    if (rings.size() < 2u || radialSegs < 3u) return;

    const uint32_t firstRingV = static_cast<uint32_t>(verts.size());
    const uint32_t ringStride = radialSegs + 1u; // Duplicate seam vertex for clean UV wrapping (topologically stitched at caps)
    const float twistRate = prof.trunkTwistRadPerM / std::max(unitsPerMeter, 1.0e-3f);
    const float age01 = Clamp01(params.ageFactor01);
    const float snow01 = Clamp01(std::max(params.snowLoad01, params.canopySnowLoad01));
    const float seedPhase = float(params.seed & 1023u) * 0.017f;

    Vec3 frameT{}, frameB{};
    BuildOrthoBasis(rings[0].axis, &frameT, &frameB);

    for (size_t r = 0; r < rings.size(); ++r) {
        const TubeRing& tr = rings[r];
        if (r > 0) {
            frameT = TransportFrame(frameT, tr.axis);
            frameB = SafeNormalize(Cross(tr.axis, frameT));
        }

        const float twist = tr.arcLen * twistRate;
        const float vCoord = tr.arcLen / std::max(unitsPerMeter * 1.4f, 1.0f);

        for (uint32_t s = 0; s <= radialSegs; ++s) {
            const float uFrac = float(s) / float(radialSegs);
            const float ang = uFrac * kTwoPi;
            const float helicalAng = ang + twist;

            // Helical bark grain ridges + fine micro-furrows baked into geometry & curvature meta.x!
            const float macroFlute = std::cos(helicalAng * prof.barkFluteFreq + seedPhase);
            const float microFurrow = std::sin(helicalAng * (prof.barkFluteFreq * 2.3f) - tr.arcLen * 0.03f + seedPhase * 1.7f);
            const float ridgeSignal = 0.68f * macroFlute + 0.32f * microFurrow;
            const float disp = 1.0f + prof.barkRidgeAmp * tr.ridgeScale * (0.65f + 0.35f * age01) * ridgeSignal;

            const float ca = std::cos(ang);
            const float sa = std::sin(ang);
            const Vec3 radial = SafeNormalize(frameT * ca + frameB * sa);
            const Vec3 pos = tr.center + radial * (tr.radius * disp);

            // Analytical helical bark normal perturbation along tangent
            const Vec3 tang = SafeNormalize(frameB * ca - frameT * sa);
            const float dRidge = -0.68f * prof.barkFluteFreq * std::sin(helicalAng * prof.barkFluteFreq + seedPhase)
                               + 0.32f * (prof.barkFluteFreq * 2.3f) * std::cos(helicalAng * (prof.barkFluteFreq * 2.3f) - tr.arcLen * 0.03f + seedPhase * 1.7f);
            const Vec3 nrm = SafeNormalize(radial - tang * (dRidge * prof.barkRidgeAmp * tr.ridgeScale * 0.14f));
            const float curvAo = Clamp(ridgeSignal * 0.52f - (1.0f - tr.normHeight) * 0.12f, -0.9f, 0.9f);

            verts.push_back(MakeBarkVertex(pos, nrm, {uFrac, vCoord}, prof, params.species,
                                           snow01, age01, curvAo, tr.bendFlex, tr.phaseHash,
                                           tr.normHeight, params.seed));
        }
    }

    // Side wall quads
    for (size_t r = 0; r + 1 < rings.size(); ++r) {
        const uint32_t rowA = firstRingV + static_cast<uint32_t>(r) * ringStride;
        const uint32_t rowB = firstRingV + static_cast<uint32_t>(r + 1) * ringStride;
        for (uint32_t s = 0; s < radialSegs; ++s) {
            const uint32_t a0 = rowA + s;
            const uint32_t a1 = rowA + s + 1u;
            const uint32_t b0 = rowB + s;
            const uint32_t b1 = rowB + s + 1u;
            idx.push_back(a0); idx.push_back(a1); idx.push_back(b1);
            idx.push_back(a0); idx.push_back(b1); idx.push_back(b0);
        }
    }

    // Watertight closed bottom cap
    {
        const TubeRing& bot = rings.front();
        const Vec3 botN = SafeNormalize(bot.axis * -1.0f);
        const uint32_t capIdx = static_cast<uint32_t>(verts.size());
        verts.push_back(MakeBarkVertex(bot.center, botN, {0.5f, 0.0f}, prof, params.species,
                                       0.0f, age01, -0.35f, bot.bendFlex, bot.phaseHash,
                                       bot.normHeight, params.seed));
        for (uint32_t s = 0; s < radialSegs; ++s) {
            idx.push_back(capIdx);
            idx.push_back(firstRingV + s + 1u);
            idx.push_back(firstRingV + s);
        }
    }

    // Watertight closed tip cap (slightly tapered point for organic twig / broken stub closure)
    {
        const TubeRing& top = rings.back();
        const Vec3 tipPos = top.center + top.axis * (top.radius * 0.85f);
        const uint32_t topRow = firstRingV + static_cast<uint32_t>(rings.size() - 1u) * ringStride;
        const uint32_t tipIdx = static_cast<uint32_t>(verts.size());
        verts.push_back(MakeBarkVertex(tipPos, top.axis, {0.5f, 1.0f}, prof, params.species,
                                       snow01, age01, 0.25f, top.bendFlex, top.phaseHash,
                                       top.normHeight, params.seed));
        for (uint32_t s = 0; s < radialSegs; ++s) {
            idx.push_back(tipIdx);
            idx.push_back(topRow + s);
            idx.push_back(topRow + s + 1u);
        }
    }
}

void MeshSkeletonBranches(const std::vector<BranchNode>& nodes, const SpeciesProfile& prof,
                          const FloraGrowthParams& params, float unitsPerMeter,
                          std::vector<TerrainVertex>& verts, std::vector<uint32_t>& idx) {
    if (nodes.size() < 2u) return;
    const uint32_t lod = std::min(params.lodLevel, 2u);

    // Decompose tree skeleton into continuous maximal branch chains (follow thickest child to stay smooth!)
    std::vector<bool> chainStarted(nodes.size(), false);
    for (size_t ni = 0u; ni < nodes.size(); ++ni) {
        const BranchNode& parent = nodes[ni];
        for (int32_t childIdx : parent.children) {
            if (chainStarted[childIdx]) continue;

            // Trace continuous chain starting at (ni -> childIdx)
            std::vector<int32_t> chain;
            chain.push_back(static_cast<int32_t>(ni));
            int32_t cur = childIdx;
            while (cur >= 0) {
                chainStarted[cur] = true;
                chain.push_back(cur);
                const BranchNode& cn = nodes[cur];
                if (cn.children.empty()) break;

                // Pick thickest child matching same branchOrder (or thickest overall for primary continuation)
                int32_t bestNext = -1;
                float bestR = -1.0f;
                for (int32_t gc : cn.children) {
                    if (chainStarted[gc]) continue;
                    if (nodes[gc].branchOrder == cn.branchOrder && nodes[gc].radius > bestR) {
                        bestR = nodes[gc].radius;
                        bestNext = gc;
                    }
                }
                if (bestNext < 0) {
                    for (int32_t gc : cn.children) {
                        if (!chainStarted[gc] && nodes[gc].radius > bestR) {
                            bestR = nodes[gc].radius;
                            bestNext = gc;
                        }
                    }
                }
                cur = bestNext;
            }

            if (chain.size() < 2u) continue;
            const uint8_t chainOrder = nodes[chain[1]].branchOrder;

            uint32_t radialSegs = 6u;
            if (chainOrder == 0u) {
                radialSegs = (lod == 0u) ? 10u : ((lod == 1u) ? 7u : 5u);
            } else if (chainOrder == 1u) {
                radialSegs = (lod == 0u) ? 6u : ((lod == 1u) ? 5u : 4u);
            } else {
                radialSegs = (lod == 0u) ? 5u : 4u;
            }

            std::vector<TubeRing> rings;
            rings.reserve(chain.size() + 1u);
            float accumArc = 0.0f;

            for (size_t k = 0; k < chain.size(); ++k) {
                const BranchNode& bn = nodes[chain[k]];
                Vec3 axis{};
                if (k == 0u) {
                    axis = SafeNormalize(nodes[chain[1]].position - bn.position, bn.direction);
                } else if (k + 1u == chain.size()) {
                    axis = SafeNormalize(bn.position - nodes[chain[k - 1]].position, bn.direction);
                } else {
                    axis = SafeNormalize(nodes[chain[k + 1]].position - nodes[chain[k - 1]].position, bn.direction);
                }
                if (k > 0u) {
                    accumArc += Length(bn.position - nodes[chain[k - 1]].position);
                }
                // If this chain branches off a much thicker parent, taper its entry collar smoothly inside the parent trunk
                float r = bn.radius;
                if (k == 0u && chainOrder > 0u) {
                    r = std::min(bn.radius, nodes[chain[1]].radius * 1.24f);
                }
                TubeRing tr{};
                tr.center     = bn.position;
                tr.axis       = axis;
                tr.radius     = std::max(r, unitsPerMeter * 0.008f);
                tr.arcLen     = accumArc;
                tr.normHeight = bn.normalizedHeight;
                tr.bendFlex   = bn.bendFlex;
                tr.phaseHash  = bn.windPhaseHash;
                tr.ridgeScale = (chainOrder == 0u) ? 1.0f : ((chainOrder == 1u) ? 0.65f : 0.35f);
                rings.push_back(tr);
            }

            AppendWatertightBarkTube(rings, radialSegs, prof, params, unitsPerMeter, verts, idx);
        }
    }
}

// Generates terrain-slope-gripping fluted buttress roots radiating from the lower trunk collar!
// Downhill roots naturally extend deeper into negative Z to grip sloped cliffs/hillsides.
void AppendTerrainButtressRoots(const SpeciesProfile& prof, const FloraGrowthParams& params,
                                float rootRadiusU, float heightU, float unitsPerMeter,
                                FloraRng& rng,
                                std::vector<TerrainVertex>& verts, std::vector<uint32_t>& idx,
                                FloraBuildStats* outStats) {
    const uint32_t lod = std::min(params.lodLevel, 2u);
    const Vec3 tn = SafeNormalize(params.terrainNormal, {0.0f, 0.0f, 1.0f});
    const uint32_t rootCount = (lod == 2u) ? std::max(3u, prof.rootButtressCount / 2u) : prof.rootButtressCount;
    const uint32_t radialSegs = (lod == 0u) ? 6u : 4u;
    const uint32_t ringCount = (lod == 0u) ? 5u : 4u;
    const float age01 = Clamp01(params.ageFactor01);

    float lowestZ = 0.0f;
    const float collarZ = std::min(heightU * 0.095f, rootRadiusU * 2.8f);
    const float baseReach = rootRadiusU * (2.6f + 1.5f * prof.buttressFlare) * (0.75f + 0.35f * age01);
    const float sinkDepth = std::max(rootRadiusU * 0.72f, unitsPerMeter * 0.28f);

    for (uint32_t k = 0u; k < rootCount; ++k) {
        const float az = (float(k) / float(rootCount)) * kTwoPi + rng.Range(-0.26f, 0.26f);
        const float reach = baseReach * rng.Range(0.82f, 1.24f);
        const float dx = std::cos(az);
        const float dy = std::sin(az);
        const float startR = rootRadiusU * rng.Range(0.36f, 0.54f);

        std::vector<TubeRing> rings;
        rings.reserve(ringCount);
        float arc = 0.0f;
        Vec3 prevP{};

        for (uint32_t s = 0u; s < ringCount; ++s) {
            const float t = float(s) / float(ringCount - 1u);
            const float distXY = Lerp(rootRadiusU * 0.35f, reach, std::pow(t, 0.92f));
            const float bendLat = std::sin(t * kPi) * reach * rng.Range(-0.08f, 0.08f);
            const float px = dx * distXY - dy * bendLat;
            const float py = dy * distXY + dx * bendLat;
            const float gZ = LocalGroundZ(px, py, tn);

            // Smooth S-arch from lower trunk collar down onto and into the exact local terrain slope plane!
            const float aboveGround = collarZ * std::pow(1.0f - t, 1.85f);
            const float bury = sinkDepth * std::pow(t, 1.6f);
            const float pz = Lerp(aboveGround, gZ - bury, Smoothstep(0.0f, 1.0f, t));
            lowestZ = std::min(lowestZ, pz);

            const Vec3 pos{px, py, pz};
            if (s > 0u) arc += Length(pos - prevP);
            prevP = pos;

            TubeRing tr{};
            tr.center     = pos;
            tr.radius     = std::max(startR * (1.0f - 0.76f * t), unitsPerMeter * 0.025f);
            tr.arcLen     = arc;
            tr.normHeight = 0.0f;
            tr.bendFlex   = 0.0f; // Roots stay 100% locked to the ground!
            tr.phaseHash  = 0.0f;
            tr.ridgeScale = 1.15f;
            rings.push_back(tr);
        }

        for (size_t s = 0; s < rings.size(); ++s) {
            if (s == 0u) {
                rings[s].axis = SafeNormalize(rings[1].center - rings[0].center);
            } else if (s + 1u == rings.size()) {
                rings[s].axis = SafeNormalize(rings[s].center - rings[s - 1].center);
            } else {
                rings[s].axis = SafeNormalize(rings[s + 1].center - rings[s - 1].center);
            }
        }
        AppendWatertightBarkTube(rings, radialSegs, prof, params, unitsPerMeter, verts, idx);
    }

    // Also add a buried central taproot plug extending down into the terrain slope so the trunk base never floats!
    {
        const float plugDepth = std::max(sinkDepth * 1.15f, std::fabs(lowestZ) * 0.35f + rootRadiusU * 0.6f);
        std::vector<TubeRing> plug(2);
        plug[0].center = {0.0f, 0.0f, -plugDepth};
        plug[0].axis = {0.0f, 0.0f, 1.0f};
        plug[0].radius = rootRadiusU * 0.72f;
        plug[0].bendFlex = 0.0f;
        plug[1].center = {0.0f, 0.0f, rootRadiusU * 0.45f};
        plug[1].axis = {0.0f, 0.0f, 1.0f};
        plug[1].radius = rootRadiusU * 0.96f;
        plug[1].arcLen = plugDepth + rootRadiusU * 0.45f;
        plug[1].bendFlex = 0.0f;
        AppendWatertightBarkTube(plug, (lod == 0u) ? 8u : 5u, prof, params, unitsPerMeter, verts, idx);
        lowestZ = std::min(lowestZ, -plugDepth);
    }

    outStats->rootButtressCount = rootCount;
    outStats->lowestRootZ       = lowestZ;
}

// ============================================================================
// Step 3: Real 3D Volumetric Foliage Clusters (ZERO Flat Cross-Cards!)
// ============================================================================

// Appends a watertight 3D arched sculpted needle spray / curved leaf cushion (top ridge + cupped flanks + bottom closure).
void AppendWatertight3DFoliageFrond(Vec3 origin, Vec3 fwd, Vec3 upHint,
                                    float lengthU, float halfWidthU, float thicknessU, float archDropU,
                                    const FloraGrowthParams& params, bool isConifer,
                                    float baseAo, float bendFlexBase, float bendFlexTip, float phaseHash,
                                    float tipTint01,
                                    std::vector<TerrainVertex>& verts, std::vector<uint32_t>& idx) {
    const Vec3 F = SafeNormalize(fwd, {1.0f, 0.0f, 0.0f});
    Vec3 R = Cross(F, upHint);
    if (Length(R) < 1.0e-4f) {
        Vec3 t{}, b{};
        BuildOrthoBasis(F, &t, &b);
        R = t;
    } else {
        R = SafeNormalize(R);
    }
    const Vec3 U = SafeNormalize(Cross(R, F));

    // Sculpted 3D frond topology: 1 base vertex + 1 mid-ring (4 verts: Top, Right, Bottom, Left) + 1 tip vertex = 6 verts, 8 watertight tris!
    const Vec3 pBase = origin;
    const Vec3 midC  = origin + F * (lengthU * 0.48f) - Vec3{0.0f, 0.0f, archDropU * 0.30f};
    const Vec3 pTip  = origin + F * lengthU - Vec3{0.0f, 0.0f, archDropU};

    const Vec3 pTop = midC + U * thicknessU;
    const Vec3 pBot = midC - U * (thicknessU * 0.52f);
    const Vec3 pRgt = midC + R * halfWidthU - U * (thicknessU * 0.14f);
    const Vec3 pLft = midC - R * halfWidthU - U * (thicknessU * 0.14f);

    const Vec3 nBase = SafeNormalize(F * -0.7f + U * 0.7f);
    const Vec3 nTop  = SafeNormalize(U * 0.92f + F * 0.24f);
    const Vec3 nBot  = SafeNormalize(U * -0.92f + F * 0.18f);
    const Vec3 nRgt  = SafeNormalize(R * 0.72f + U * 0.64f);
    const Vec3 nLft  = SafeNormalize(R * -0.72f + U * 0.64f);
    const Vec3 nTip  = SafeNormalize(F * 0.62f + U * 0.78f);

    FloraGrowthParams botParams = params;
    botParams.snowLoad01 = 0.0f;
    botParams.canopySnowLoad01 = 0.0f;

    const float flexMid = Lerp(bendFlexBase, bendFlexTip, 0.55f);
    const uint32_t v0 = static_cast<uint32_t>(verts.size());
    verts.push_back(MakeFoliageVertex(pBase, nBase, {0.5f, 0.0f}, params,    isConifer, baseAo - 0.18f, bendFlexBase, phaseHash, tipTint01 * 0.3f));
    verts.push_back(MakeFoliageVertex(pTop,  nTop,  {0.5f, 0.5f}, params,    isConifer, baseAo + 0.32f, flexMid,      phaseHash, tipTint01 * 0.8f));
    verts.push_back(MakeFoliageVertex(pRgt,  nRgt,  {1.0f, 0.5f}, params,    isConifer, baseAo + 0.14f, flexMid,      phaseHash, tipTint01 * 0.7f));
    verts.push_back(MakeFoliageVertex(pBot,  nBot,  {0.5f, 0.5f}, botParams, isConifer, baseAo - 0.38f, flexMid,      phaseHash, tipTint01 * 0.2f));
    verts.push_back(MakeFoliageVertex(pLft,  nLft,  {0.0f, 0.5f}, params,    isConifer, baseAo + 0.14f, flexMid,      phaseHash, tipTint01 * 0.7f));
    verts.push_back(MakeFoliageVertex(pTip,  nTip,  {0.5f, 1.0f}, params,    isConifer, baseAo + 0.38f, bendFlexTip,  phaseHash, tipTint01));

    // 4 front-half watertight faces (base -> mid ring)
    idx.push_back(v0 + 0); idx.push_back(v0 + 2); idx.push_back(v0 + 1);
    idx.push_back(v0 + 0); idx.push_back(v0 + 3); idx.push_back(v0 + 2);
    idx.push_back(v0 + 0); idx.push_back(v0 + 4); idx.push_back(v0 + 3);
    idx.push_back(v0 + 0); idx.push_back(v0 + 1); idx.push_back(v0 + 4);

    // 4 distal-half watertight faces (mid ring -> tip)
    idx.push_back(v0 + 1); idx.push_back(v0 + 2); idx.push_back(v0 + 5);
    idx.push_back(v0 + 2); idx.push_back(v0 + 3); idx.push_back(v0 + 5);
    idx.push_back(v0 + 3); idx.push_back(v0 + 4); idx.push_back(v0 + 5);
    idx.push_back(v0 + 4); idx.push_back(v0 + 1); idx.push_back(v0 + 5);
}

void Populate3DFoliageClusters(const std::vector<BranchNode>& nodes, const SpeciesProfile& prof,
                               const FloraGrowthParams& params, float crownRadiusU, float unitsPerMeter,
                               FloraRng& rng,
                               std::vector<TerrainVertex>& verts, std::vector<uint32_t>& idx,
                               FloraBuildStats* outStats) {
    const uint32_t lod = std::min(params.lodLevel, 2u);
    const float snow01 = Clamp01(std::max(params.snowLoad01, params.canopySnowLoad01));
    const float sagLoad = Clamp01(params.canopySnowLoad01);
    const float abscission = Clamp01(params.leafAbscission01);
    uint32_t clusterCount = 0u;
    uint32_t abscisedCount = 0u;

    for (size_t ni = 1u; ni < nodes.size(); ++ni) {
        const BranchNode& bn = nodes[ni];
        if (!bn.hasFoliage || bn.isDeadStub) continue;

        // Seasonal Deciduous Leaf Abscission:
        //   - Conifers (SitkaSpruce, CoastalPine) are evergreen and retain winter needle whorls!
        //   - Deciduous trees (Oak, Birch) shed leaves smoothly with leafAbscission01 (100% bare at >= 0.98).
        //   - Semi-deciduous Alpine Shrub sheds up to 65% of outer fern fronds while keeping hardy inner sprigs.
        if (!prof.isConifer && abscission > 0.005f) {
            const float dropProb = prof.isUnderstoryShrub ? (abscission * 0.65f) : abscission;
            if (dropProb >= 0.98f || rng.Next01() < dropProb) {
                ++abscisedCount;
                continue;
            }
        }

        const float baseFlex = Clamp01(bn.bendFlex);
        const float tipFlex  = Clamp01(std::max(bn.bendFlex + 0.24f, 0.72f));
        const float sunAo    = Clamp(-0.20f + 0.55f * bn.normalizedHeight, -0.45f, 0.45f);

        if (prof.isConifer) {
            // 3D Conifer Needle Whorl / Windswept Canopy Pad: radiating arched 3D needle sprays!
            const uint32_t sprays = prof.isWindsweptPads
                                  ? ((lod == 0u) ? 6u : ((lod == 1u) ? 4u : 3u))
                                  : ((lod == 0u) ? 5u : ((lod == 1u) ? 3u : 2u));
            const float sprayLen = std::max(crownRadiusU * (prof.isWindsweptPads ? 0.28f : 0.26f) * (0.65f + 0.45f * (1.0f - bn.normalizedHeight * 0.5f)),
                                            unitsPerMeter * 0.55f);
            const float sprayW   = sprayLen * (prof.isWindsweptPads ? 0.34f : 0.28f);
            const float sprayThk = sprayLen * 0.14f;

            for (uint32_t k = 0; k < sprays; ++k) {
                const float az = float(k) * kGoldenAngle + rng.Range(-0.28f, 0.28f);
                const float pitch = prof.isWindsweptPads
                                  ? (rng.Range(-0.08f, 0.16f) - 0.22f * sagLoad)
                                  : (rng.Range(-0.34f, 0.12f) - 0.18f * snow01 - 0.25f * sagLoad);
                const Vec3 sDir = SafeNormalize(
                    bn.direction * 0.35f + Vec3{std::cos(az), std::sin(az), pitch});
                const float droop = sprayLen * (prof.isWindsweptPads
                                                ? (0.10f + 0.28f * sagLoad)
                                                : (0.24f + 0.22f * snow01 + 0.30f * sagLoad));
                const float tint = rng.Range(0.15f, 0.85f);
                AppendWatertight3DFoliageFrond(bn.position, sDir, {0.0f, 0.0f, 1.0f},
                                               sprayLen * rng.Range(0.82f, 1.18f), sprayW, sprayThk, droop,
                                               params, true, sunAo, baseFlex, tipFlex,
                                               bn.windPhaseHash, tint, verts, idx);
            }
            ++clusterCount;
        } else if (prof.isUnderstoryShrub) {
            // Articulated Multi-Blade 3D Fern / Juniper Frond Rosette with per-stem bend weights!
            const uint32_t blades = (lod == 0u) ? 4u : ((lod == 1u) ? 3u : 2u);
            const float bladeLen = std::max(crownRadiusU * 0.42f * rng.Range(0.80f, 1.15f), unitsPerMeter * 0.35f);
            const float bladeW   = bladeLen * 0.26f;
            const float bladeThk = bladeLen * 0.11f;
            for (uint32_t k = 0; k < blades; ++k) {
                const float az = float(k) * kGoldenAngle + float(bn.stemId) * 0.9f + rng.Range(-0.25f, 0.25f);
                const Vec3 fDir = SafeNormalize(bn.direction * 0.45f + Vec3{std::cos(az), std::sin(az), rng.Range(0.05f, 0.52f) - 0.22f * sagLoad});
                const float droop = bladeLen * (0.30f + 0.18f * snow01 + 0.24f * sagLoad);
                AppendWatertight3DFoliageFrond(bn.position, fDir, {0.0f, 0.0f, 1.0f},
                                               bladeLen, bladeW, bladeThk, droop,
                                               params, false, sunAo, baseFlex, 1.0f,
                                               bn.windPhaseHash, rng.Range(0.2f, 0.9f), verts, idx);
            }
            ++clusterCount;
        } else {
            // Broadleaf Curved 3D Cluster Fan Rosette (Ancient Gnarled Oak / Nordic Silver Birch)
            const uint32_t fans = (lod == 0u) ? 5u : ((lod == 1u) ? 3u : 2u);
            const float fanLen = std::max(crownRadiusU * (prof.isBirchBark ? 0.24f : 0.22f) * rng.Range(0.84f, 1.22f),
                                          unitsPerMeter * 0.65f);
            const float fanW   = fanLen * (prof.isBirchBark ? 0.36f : 0.46f);
            const float fanThk = fanLen * 0.18f;

            for (uint32_t k = 0; k < fans; ++k) {
                const float az = float(k) * kGoldenAngle + rng.Range(-0.30f, 0.30f);
                const float elev = prof.isPendulous ? rng.Range(-0.55f, 0.15f) : rng.Range(-0.08f, 0.58f);
                const Vec3 fDir = SafeNormalize(bn.direction * 0.28f + Vec3{std::cos(az), std::sin(az), elev - 0.16f * sagLoad});
                const float droop = fanLen * ((prof.isPendulous ? 0.34f : 0.16f) + 0.22f * sagLoad);
                const float tint = rng.Range(0.20f, 0.95f);
                AppendWatertight3DFoliageFrond(bn.position, fDir, {0.0f, 0.0f, 1.0f},
                                               fanLen, fanW, fanThk, droop,
                                               params, false, sunAo, baseFlex, tipFlex,
                                               bn.windPhaseHash, tint, verts, idx);
            }
            ++clusterCount;
        }
    }

    outStats->foliageClusterCount  = clusterCount;
    outStats->abscisedClusterCount = abscisedCount;
}

}  // namespace

// ============================================================================
// Public API Implementation
// ============================================================================

const char* GetFloraSpeciesName(FloraSpecies species) {
    const uint32_t idx = static_cast<uint32_t>(species);
    if (idx >= static_cast<uint32_t>(FloraSpecies::Count)) return "UnknownFlora";
    return kSpeciesProfiles[idx].name;
}

FloraSpecies SelectFloraSpeciesForBiome(BiomeId biome, uint32_t seed, float slope01, float altitudeNorm01) {
    const uint32_t h = HashU32(seed ^ (static_cast<uint32_t>(biome) * 0x9E3779B9u));
    const float r = HashTo01(h);

    if (slope01 > 0.46f) {
        return (r < 0.72f) ? FloraSpecies::WindsweptCoastalPine : FloraSpecies::AlpineUnderstoryShrub;
    }
    switch (biome) {
        case BiomeId::OldGrowthConiferHighlands:
            if (r < 0.62f) return FloraSpecies::OldGrowthSitkaSpruce;
            if (r < 0.80f) return FloraSpecies::AncientGnarledOak;
            if (r < 0.90f) return FloraSpecies::NordicSilverBirch;
            return FloraSpecies::AlpineUnderstoryShrub;
        case BiomeId::GlacialFjordSpires:
            if (altitudeNorm01 > 0.68f) return FloraSpecies::AlpineUnderstoryShrub;
            return (r < 0.60f) ? FloraSpecies::WindsweptCoastalPine : FloraSpecies::OldGrowthSitkaSpruce;
        case BiomeId::AlpineMeltwaterDeltas:
            if (r < 0.44f) return FloraSpecies::NordicSilverBirch;
            if (r < 0.76f) return FloraSpecies::AncientGnarledOak;
            return FloraSpecies::AlpineUnderstoryShrub;
        case BiomeId::ObsidianBasaltCanyons:
            return (r < 0.65f) ? FloraSpecies::WindsweptCoastalPine : FloraSpecies::AlpineUnderstoryShrub;
        case BiomeId::BioluminescentCrystalTundra:
            return (r < 0.55f) ? FloraSpecies::NordicSilverBirch : FloraSpecies::OldGrowthSitkaSpruce;
        case BiomeId::GeothermalVolcanicCaldera:
        default:
            return (r < 0.55f) ? FloraSpecies::WindsweptCoastalPine : FloraSpecies::AlpineUnderstoryShrub;
    }
}

FloraGrowthParams MakeDefaultFloraParams(FloraSpecies species, uint32_t seed, uint32_t lodLevel) {
    FloraGrowthParams p{};
    p.species = (static_cast<uint32_t>(species) < static_cast<uint32_t>(FloraSpecies::Count))
              ? species : FloraSpecies::OldGrowthSitkaSpruce;
    p.seed = seed;
    p.lodLevel = std::min(lodLevel, 2u);
    const SpeciesProfile& prof = kSpeciesProfiles[static_cast<uint32_t>(p.species)];
    p.heightMeters = prof.defaultHeightM;
    p.crownRadiusMeters = prof.defaultCrownRadiusM;
    p.unitsPerMeter = 70.0f;
    return p;
}

void ApplyFloraSeasonPreset(FloraSeasonPreset preset, FloraGrowthParams* params) {
    if (!params) return;
    switch (preset) {
        case FloraSeasonPreset::SpringBudBreak:
            ApplyPhenologyToFloraParams(0.82f, 0.08f, 0.02f, 0.22f, 0.06f, params);
            break;
        case FloraSeasonPreset::SummerLush:
            ApplyPhenologyToFloraParams(1.00f, 0.00f, 0.00f, 0.00f, 0.00f, params);
            break;
        case FloraSeasonPreset::PeakAutumn:
            ApplyPhenologyToFloraParams(0.18f, 0.86f, 0.64f, 0.28f, 0.04f, params);
            break;
        case FloraSeasonPreset::DeepWinter:
            ApplyPhenologyToFloraParams(0.05f, 0.15f, 0.10f, 1.00f, 0.88f, params);
            break;
    }
}

void ApplyPhenologyToFloraParams(float chlorophyll01, float autumnCarotenoid01,
                                 float autumnAnthocyanin01, float leafAbscission01,
                                 float canopySnowLoad01, FloraGrowthParams* params) {
    if (!params) return;
    params->chlorophyll01       = Clamp01(chlorophyll01);
    params->autumnCarotenoid01  = Clamp01(autumnCarotenoid01);
    params->autumnAnthocyanin01 = Clamp01(autumnAnthocyanin01);
    params->leafAbscission01    = Clamp01(leafAbscission01);
    params->canopySnowLoad01    = Clamp01(canopySnowLoad01);
    params->snowLoad01          = std::max(Clamp01(params->snowLoad01), params->canopySnowLoad01);
}

bool GenerateFloraAsset(const FloraGrowthParams& params, FloraAsset* outAsset) {
    if (!outAsset) return false;
    const uint32_t spIdx = static_cast<uint32_t>(params.species);
    if (spIdx >= static_cast<uint32_t>(FloraSpecies::Count)) return false;
    if (params.lodLevel > 2u) return false;
    if (!std::isfinite(params.unitsPerMeter) || params.unitsPerMeter <= 0.0f) return false;
    if (!std::isfinite(params.daVinciExponent) || params.daVinciExponent < 1.2f || params.daVinciExponent > 5.0f) return false;

    const SpeciesProfile& prof = kSpeciesProfiles[spIdx];
    FloraRng rng(params.seed ^ (spIdx * 0x85EBCA6Bu + 0x27D4EB2Du));

    // Resolve biological dimensions (honoring explicit overrides or species defaults with subtle organic variance)
    const float heightM = (params.heightMeters > 0.0f)
                        ? params.heightMeters
                        : (prof.defaultHeightM * rng.Range(0.88f, 1.14f));
    const float crownM  = (params.crownRadiusMeters > 0.0f)
                        ? params.crownRadiusMeters
                        : (prof.defaultCrownRadiusM * rng.Range(0.90f, 1.12f));
    if (!std::isfinite(heightM) || !std::isfinite(crownM) || heightM < 0.25f || crownM < 0.15f) {
        return false;
    }

    const float heightU = heightM * params.unitsPerMeter;
    const float crownU  = crownM * params.unitsPerMeter;

    outAsset->species = params.species;
    outAsset->params  = params;
    outAsset->stats   = {};
    outAsset->skeleton.clear();

    // 1. Run biological growth simulation (Space Colonization + Phototropism + Gravitropism + Pipe Model)
    BuildBiologicalSkeleton(prof, params, heightU, crownU, rng, &outAsset->skeleton, &outAsset->stats);
    if (outAsset->skeleton.size() < 2u) return false;

    // 2. Build watertight manifold bark tubes, terrain-gripping buttress roots & 3D foliage clusters
    std::vector<TerrainVertex> srcVerts;
    std::vector<uint32_t> srcIndices;
    srcVerts.reserve(outAsset->skeleton.size() * 24u);
    srcIndices.reserve(outAsset->skeleton.size() * 64u);

    MeshSkeletonBranches(outAsset->skeleton, prof, params, params.unitsPerMeter, srcVerts, srcIndices);
    AppendTerrainButtressRoots(prof, params, outAsset->stats.rootRadiusU, heightU, params.unitsPerMeter,
                               rng, srcVerts, srcIndices, &outAsset->stats);
    Populate3DFoliageClusters(outAsset->skeleton, prof, params, crownU, params.unitsPerMeter,
                              rng, srcVerts, srcIndices, &outAsset->stats);

    if (srcVerts.empty() || srcIndices.size() < 3u) return false;

    // 3. Compute exact AABB and pack into DX12 AS+MS 64v/126t GPU meshlets via RawMeshPipeline::BuildMeshlets!
    Vec3 mn{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 mx{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    for (const auto& v : srcVerts) {
        mn.x = std::min(mn.x, v.pos[0]); mn.y = std::min(mn.y, v.pos[1]); mn.z = std::min(mn.z, v.pos[2]);
        mx.x = std::max(mx.x, v.pos[0]); mx.y = std::max(mx.y, v.pos[1]); mx.z = std::max(mx.z, v.pos[2]);
    }

    ProceduralMesh& pm = outAsset->mesh;
    pm.name            = std::string("flora_") + prof.name + "_lod" + std::to_string(params.lodLevel);
    pm.primaryMaterial = TerrainMaterialId::MossMeadow;
    pm.lodLevel        = params.lodLevel;
    pm.aabbMin         = mn;
    pm.aabbMax         = mx;

    const uint32_t clampedTris = std::clamp(params.maxMeshletTris, 16u, 126u);
    BuildMeshlets(srcVerts, srcIndices, 64u, clampedTris, &pm.vertices, &pm.indices, &pm.meshlets);
    return !pm.meshlets.empty() && !pm.vertices.empty() && !pm.indices.empty();
}

bool GenerateFloraMesh(const FloraGrowthParams& params, ProceduralMesh* outMesh, FloraBuildStats* outStats) {
    if (!outMesh) return false;
    FloraAsset asset{};
    if (!GenerateFloraAsset(params, &asset)) return false;
    *outMesh = std::move(asset.mesh);
    if (outStats) *outStats = asset.stats;
    return true;
}

bool GenerateFloraLodChain(const FloraGrowthParams& baseParams, FloraLodChain* outChain) {
    if (!outChain) return false;
    outChain->species = baseParams.species;
    for (uint32_t lod = 0u; lod < 3u; ++lod) {
        FloraGrowthParams lp = baseParams;
        lp.lodLevel = lod;
        if (!GenerateFloraMesh(lp, &outChain->lods[lod], &outChain->lodStats[lod])) {
            return false;
        }
    }
    return true;
}

bool BuildBiomechanicalTreeForProp(const PropInstance& prop, uint32_t lodLevel, float snowLoad01,
                                   uint32_t maxMeshletTris, ProceduralMesh* outMesh) {
    if (!outMesh) return false;
    const float slope01 = Clamp01(1.0f - SafeNormalize(prop.upNormal).z);
    FloraSpecies sp = SelectFloraSpeciesForBiome(prop.biome, prop.seed, slope01, 0.5f);
    if (prop.kind == PropKind::AlpineGrassClump) {
        sp = FloraSpecies::AlpineUnderstoryShrub;
    }

    FloraGrowthParams p = MakeDefaultFloraParams(sp, prop.seed, std::min(lodLevel, 2u));
    p.terrainNormal  = SafeNormalize(prop.upNormal, {0.0f, 0.0f, 1.0f});
    p.snowLoad01     = Clamp01(snowLoad01);
    p.maxMeshletTris = std::clamp(maxMeshletTris, 16u, 126u);
    // Scale physical dimensions cleanly by prop.scale (clamped to sane botanical bounds)
    const float sc = Clamp(prop.scale, 0.35f, 2.2f);
    p.heightMeters      *= sc;
    p.crownRadiusMeters *= sc;
    return GenerateFloraMesh(p, outMesh, nullptr);
}

bool ExportFloraToGlb(const FloraAsset& asset, const std::string& path, bool zUpToYUp) {
    return ExportMeshToGlbFile(asset.mesh, path, zUpToYUp);
}

bool ExportFloraToObj(const FloraAsset& asset, const std::string& path) {
    return ExportMeshToObjFile(asset.mesh, path);
}

bool ExportFloraLodChainToGlb(const FloraLodChain& chain, const std::string& dirPath,
                              const std::string& baseName, bool zUpToYUp) {
    if (dirPath.empty() || baseName.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(dirPath, ec);
    for (uint32_t lod = 0u; lod < 3u; ++lod) {
        const std::string filePath = dirPath + "/" + baseName + "_lod" + std::to_string(lod) + ".glb";
        if (!ExportMeshToGlbFile(chain.lods[lod], filePath, zUpToYUp)) {
            return false;
        }
    }
    return true;
}

}  // namespace raw::worldgen
