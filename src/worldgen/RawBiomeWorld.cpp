// RawBiomeWorld.cpp: 6-Biome Procedural Showcase World, 8-Channel PBR Splatting & Poisson Prop Distribution.
// Original work, MIT.
#include "RawBiomeWorld.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace raw::worldgen {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

constexpr BiomeSpec kBiomeSpecs[6] = {
    {BiomeId::GlacialFjordSpires, "Glacial Fjord Spires", 1.32f, -1500.0f, 340.0f, 0.55f,
     PropKind::GlacialIceArch, {0.32f, 0.78f, 0.94f}},
    {BiomeId::GeothermalVolcanicCaldera, "Geothermal Volcanic Caldera", 1.18f, 4200.0f, 360.0f, 0.50f,
     PropKind::VolcanicSteamVent, {1.00f, 0.34f, 0.08f}},
    {BiomeId::OldGrowthConiferHighlands, "Old-Growth Conifer Highlands", 1.00f, 400.0f, 220.0f, 0.92f,
     PropKind::ConiferTree, {0.16f, 0.42f, 0.20f}},
    {BiomeId::ObsidianBasaltCanyons, "Obsidian Basalt Canyons", 1.24f, 1800.0f, 280.0f, 0.72f,
     PropKind::BasaltColumnCluster, {0.22f, 0.24f, 0.29f}},
    {BiomeId::BioluminescentCrystalTundra, "Bioluminescent Crystal Tundra", 0.92f, -1100.0f, 310.0f, 0.68f,
     PropKind::CrystalMonolith, {0.52f, 0.38f, 0.98f}},
    {BiomeId::AlpineMeltwaterDeltas, "Alpine Meltwater Deltas", 0.64f, 1200.0f, 240.0f, 0.80f,
     PropKind::AlpineGrassClump, {0.18f, 0.68f, 0.66f}},
};

struct BiomeRng {
    uint32_t s = 0x51C0E11Au;
    explicit BiomeRng(uint32_t seed) : s(seed ? seed : 0x51C0E11Au) {}
    uint32_t NextU32() {
        uint32_t x = s;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        s = x;
        return x;
    }
    float Next01() { return float(NextU32() & 0x00FFFFFFu) * (1.0f / 16777216.0f); }
    float Range(float lo, float hi) { return lo + (hi - lo) * Next01(); }
};

float BiomeElevationShaperAdapter(float wx, float wy, const TectonicSample& tec,
                                  const HydrographySample& hydro, uint32_t seed, void*) {
    return ShapeBiomeElevation(wx, wy, tec, hydro, seed, nullptr);
}

}  // namespace

const BiomeSpec& GetBiomeSpec(BiomeId id) {
    const size_t idx = std::min<size_t>(static_cast<size_t>(id), 5u);
    return kBiomeSpecs[idx];
}

const char* GetBiomeName(BiomeId id) {
    return GetBiomeSpec(id).name;
}

const char* GetMaterialName(TerrainMaterialId id) {
    static constexpr const char* kNames[8] = {
        "RockGranite", "SnowPack", "GlacialIce", "BasaltColumn",
        "MagmaFissure", "MossMeadow", "RiverPebbleSand", "ArcaneCrystal",
    };
    return kNames[std::min<size_t>(static_cast<size_t>(id), 7u)];
}

const char* GetPropKindName(PropKind kind) {
    static constexpr const char* kNames[8] = {
        "ConiferTree", "AlpineBoulder", "GlacialIceArch", "SnowDrift",
        "BasaltColumnCluster", "VolcanicSteamVent", "CrystalMonolith", "AlpineGrassClump",
    };
    return kNames[std::min<size_t>(static_cast<size_t>(kind), 7u)];
}

BiomeBlendResult EvaluateBiomeBlend(float worldX, float worldY, uint32_t seed) {
    BiomeBlendResult res{};
    const Vec2 warped = DomainWarp2D(worldX, worldY, seed + 2027u, 11500.0f, 1.0f / 52000.0f);

    // Macro climate fields (continuous, deterministic across the entire infinite plane)
    const float tempNoise = Fbm2D(warped.x * (1.0f / 88000.0f), warped.y * (1.0f / 88000.0f), seed + 2111u, 3);
    const float latBias = Clamp01(0.5f - warped.y * (1.0f / 380000.0f));
    res.temperature01 = Clamp01(0.58f * tempNoise + 0.42f * latBias);
    res.moisture01 = Clamp01(Fbm2D(warped.x * (1.0f / 76000.0f) + 7.1f, warped.y * (1.0f / 76000.0f) - 4.3f,
                                   seed + 2203u, 3));
    res.volcanism01 = Clamp01(Fbm2D(warped.x * (1.0f / 68000.0f) - 13.2f, warped.y * (1.0f / 68000.0f) + 9.8f,
                                    seed + 2309u, 3));
    res.crystalResonance01 = Clamp01(Fbm2D(warped.x * (1.0f / 72000.0f) + 19.4f, warped.y * (1.0f / 72000.0f) + 14.1f,
                                           seed + 2411u, 3));

    // Showcase frontier compass anchors: heading out past any edge of Tamriel (or exploring around a
    // 6-sector compass ring) organically encounters all 6 showcase biomes with soft warped borders!
    // Sector centers (radians):
    //   0: Glacial Fjord Spires          (North,     +90 deg)
    //   4: Bioluminescent Crystal Tundra (NorthEast, +30 deg)
    //   1: Geothermal Volcanic Caldera   (East,      -30 deg)
    //   3: Obsidian Basalt Canyons       (SouthEast, -90 deg)
    //   5: Alpine Meltwater Deltas       (SouthWest, -150 deg)
    //   2: Old-Growth Conifer Highlands  (NorthWest, +150 deg)
    const float ang = std::atan2(warped.y, warped.x);
    constexpr float kSectorAngles[6] = {
        +0.50f * kPi, // 0: Glacial Fjord Spires (N)
        -0.166667f * kPi, // 1: Geothermal Volcanic Caldera (E/SE)
        +0.833333f * kPi, // 2: Old-Growth Conifer Highlands (NW)
        -0.50f * kPi, // 3: Obsidian Basalt Canyons (S)
        +0.166667f * kPi, // 4: Bioluminescent Crystal Tundra (NE)
        -0.833333f * kPi, // 5: Alpine Meltwater Deltas (SW)
    };

    const float dist = std::hypot(warped.x, warped.y);
    const float ringInfluence = Smoothstep(4000.0f, 36000.0f, dist);

    // Climate affinities (also work near origin or far out at arbitrary coordinates)
    const float clim[6] = {
        (1.0f - res.temperature01) * (0.4f + 0.6f * res.moisture01),              // 0 Glacial Fjord
        res.volcanism01 * (0.45f + 0.55f * res.temperature01),                    // 1 Volcanic Caldera
        res.moisture01 * (1.0f - std::fabs(res.temperature01 - 0.52f) * 1.4f),    // 2 Conifer Highlands
        (1.0f - res.moisture01) * (0.5f + 0.5f * res.volcanism01),                // 3 Basalt Canyons
        res.crystalResonance01 * (1.0f - res.temperature01 * 0.7f),               // 4 Crystal Tundra
        res.moisture01 * (0.45f + 0.55f * res.temperature01),                     // 5 Meltwater Deltas
    };

    float rawScores[6] = {};
    float maxScore = -1e9f;
    for (int i = 0; i < 6; ++i) {
        float da = ang - kSectorAngles[i];
        while (da > kPi) da -= kTwoPi;
        while (da < -kPi) da += kTwoPi;
        const float sectorAff = std::exp(-(da * da) / (2.0f * 0.44f * 0.44f));
        rawScores[i] = Lerp(clim[i], 0.65f * sectorAff + 0.35f * clim[i], ringInfluence);
        maxScore = std::max(maxScore, rawScores[i]);
    }

    // Softmax with crisp transition temperature (sharp biome identity + smooth organic transition borders)
    const float sharpness = 7.5f;
    float sumW = 0.0f;
    for (int i = 0; i < 6; ++i) {
        res.weights[size_t(i)] = std::exp((rawScores[i] - maxScore) * sharpness);
        sumW += res.weights[size_t(i)];
    }
    const float invSum = sumW > 0.0f ? (1.0f / sumW) : (1.0f / 6.0f);
    int best0 = 0, best1 = 1;
    for (int i = 0; i < 6; ++i) {
        res.weights[size_t(i)] *= invSum;
    }
    for (int i = 0; i < 6; ++i) {
        if (res.weights[size_t(i)] > res.weights[size_t(best0)]) {
            best1 = best0;
            best0 = i;
        } else if (i != best0 && res.weights[size_t(i)] > res.weights[size_t(best1)]) {
            best1 = i;
        }
    }
    res.primary = static_cast<BiomeId>(best0);
    res.secondary = static_cast<BiomeId>(best1);
    return res;
}

float ShapeBiomeElevation(float worldX, float worldY, const TectonicSample& tec,
                          const HydrographySample& hydro, uint32_t seed, BiomeBlendResult* outBlend) {
    const BiomeBlendResult b = EvaluateBiomeBlend(worldX, worldY, seed);
    if (outBlend) *outBlend = b;

    const float base = tec.baseHeightU;

    // 0. Glacial Fjord Spires: dramatic U-shaped sea fjords + jagged ice spires
    const float fjordSpire = std::pow(RidgedMultifractal2D(worldX * 0.00028f, worldY * 0.00028f, seed + 3001u, 4), 1.65f);
    const float fjordTrough = Smoothstep(0.15f, 0.55f, hydro.riverMask01 + (1.0f - tec.continent01) * 0.6f);
    const float hFjord = base * 1.15f + fjordSpire * 2650.0f - fjordTrough * 1450.0f;

    // 1. Geothermal Volcanic Caldera: collapsed ring rim + stepped terraces + incised magma rifts
    const WorleyResult cald = Worley2D(worldX * 0.000042f, worldY * 0.000042f, seed + 3109u);
    const float rimRing = std::exp(-std::pow((cald.f1 - 0.42f) / 0.14f, 2.0f));
    const float bowlDrop = (1.0f - Smoothstep(0.0f, 0.34f, cald.f1)) * 1100.0f;
    const float riftNoise = 1.0f - Smoothstep(0.01f, 0.09f, std::fabs(ValueNoise2DGrad(worldX * 0.00045f, worldY * 0.00045f, seed + 3163u).x - 0.5f));
    const float hVolcanic = base * 0.85f + rimRing * 2800.0f - bowlDrop - riftNoise * 240.0f;

    // 2. Old-Growth Conifer Highlands: glaciated granite slopes + terraced waterfall valleys
    const float highlandRoll = Fbm2D(worldX * 0.00024f, worldY * 0.00024f, seed + 3203u, 5);
    const float hConifer = base * 0.92f + (highlandRoll - 0.42f) * 1600.0f - hydro.riverDepthU * 1.25f;

    // 3. Obsidian Basalt Canyons: hexagonal Giant's Causeway stepped mesas + deep gorge rapids
    const float canyonRaw = base * 0.95f + RidgedMultifractal2D(worldX * 0.00018f, worldY * 0.00018f, seed + 3307u, 3) * 1850.0f;
    // Quantized stepped basalt terraces (smooth staircase)
    const float stepH = 380.0f;
    const float sIdx = std::floor(canyonRaw / stepH);
    const float sFrac = (canyonRaw / stepH) - sIdx;
    const float stepped = (sIdx + Smoothstep(0.18f, 0.82f, sFrac)) * stepH;
    const float hBasalt = stepped - hydro.riverDepthU * 2.15f;

    // 4. Bioluminescent Crystal Tundra: sweeping permafrost shelves + crystalline spire spurs
    const float tundraShelf = Fbm2D(worldX * 0.00016f, worldY * 0.00016f, seed + 3407u, 4);
    const float crystalSpur = std::pow(RidgedMultifractal2D(worldX * 0.00052f, worldY * 0.00052f, seed + 3461u, 3), 2.1f);
    const float hCrystal = base * 0.72f + (tundraShelf - 0.35f) * 900.0f + crystalSpur * 1450.0f;

    // 5. Alpine Meltwater Deltas: broad braided turquoise alluvial fans + low gravel bars
    const float braid1 = std::fabs(ValueNoise2DGrad(worldX * 0.00055f, worldY * 0.00032f, seed + 3511u).x - 0.5f);
    const float braid2 = std::fabs(ValueNoise2DGrad(worldX * 0.00048f + 11.3f, worldY * 0.00062f - 5.7f, seed + 3557u).x - 0.5f);
    const float braidChan = (1.0f - Smoothstep(0.01f, 0.085f, std::min(braid1, braid2)));
    const float hDelta = Lerp(base, -820.0f, 0.58f) - braidChan * 195.0f - hydro.riverDepthU * 0.75f;

    const float hArr[6] = {hFjord, hVolcanic, hConifer, hBasalt, hCrystal, hDelta};
    float finalH = 0.0f;
    for (int i = 0; i < 6; ++i) {
        finalH += hArr[i] * b.weights[size_t(i)];
    }
    if (hydro.lakeMask01 > 0.01f && finalH < hydro.waterLevelZ) {
        finalH = Lerp(finalH, hydro.waterLevelZ - 24.0f, hydro.lakeMask01 * 0.40f);
    }
    return finalH;
}

MaterialSplat8 EvaluateBiomeMaterials(Vec3 worldPos, Vec3 normal, float curvature, float flow,
                                      float erosion, float wetness, float riverDepth,
                                      const BiomeBlendResult& blend, float seaLevelZ, float snowLineZ,
                                      uint32_t seed) {
    const float slope = Clamp01(1.0f - normal.z);
    const float flat = Smoothstep(0.34f, 0.82f, normal.z);
    const float cliff = Smoothstep(0.26f, 0.62f, slope);

    // Per-channel raw accumulation weights:
    // 0:RockGranite, 1:SnowPack, 2:GlacialIce, 3:BasaltColumn, 4:MagmaFissure, 5:MossMeadow, 6:RiverPebbleSand, 7:ArcaneCrystal
    float w[8] = {0.08f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    // Biome 0: Glacial Fjord Spires
    const float bFjord = blend.weights[0];
    if (bFjord > 0.001f) {
        const float crevasse = Smoothstep(-0.05f, -0.45f, curvature);
        w[2] += bFjord * (0.52f + 0.55f * cliff + 0.40f * crevasse);
        w[1] += bFjord * (0.48f * flat + 0.25f * Smoothstep(snowLineZ - 1600.0f, snowLineZ, worldPos.z));
        w[0] += bFjord * 0.16f * cliff;
    }

    // Biome 1: Geothermal Volcanic Caldera
    const float bVolc = blend.weights[1];
    if (bVolc > 0.001f) {
        const float fissureNoise = ValueNoise2DGrad(worldPos.x * 0.0018f, worldPos.y * 0.0018f, seed + 4001u).x;
        const float crack = 1.0f - Smoothstep(0.02f, 0.14f, std::fabs(fissureNoise - 0.5f));
        const float magmaMask = Clamp01(crack * 0.85f + Smoothstep(0.05f, 0.55f, -curvature) * 0.55f + flow * 0.35f);
        w[3] += bVolc * (0.72f + 0.35f * cliff);
        w[4] += bVolc * (0.18f + 1.15f * magmaMask);
        w[0] += bVolc * 0.12f * erosion;
    }

    // Biome 2: Old-Growth Conifer Highlands
    const float bConif = blend.weights[2];
    if (bConif > 0.001f) {
        const float highSnow = Smoothstep(snowLineZ - 200.0f, snowLineZ + 900.0f, worldPos.z) * flat;
        w[5] += bConif * (0.75f * flat * (1.0f - highSnow));
        w[0] += bConif * (0.65f * cliff + 0.20f * erosion);
        w[1] += bConif * highSnow * 0.90f;
        w[6] += bConif * wetness * 0.35f;
    }

    // Biome 3: Obsidian Basalt Canyons
    const float bBasalt = blend.weights[3];
    if (bBasalt > 0.001f) {
        w[3] += bBasalt * (0.80f + 0.45f * cliff);
        w[0] += bBasalt * 0.22f * erosion;
        w[6] += bBasalt * (wetness * 0.55f + Smoothstep(10.0f, 120.0f, riverDepth) * 0.50f);
        w[5] += bBasalt * 0.14f * flat * wetness;
    }

    // Biome 4: Bioluminescent Crystal Tundra
    const float bCrys = blend.weights[4];
    if (bCrys > 0.001f) {
        const float ridgeVein = Smoothstep(0.08f, 0.55f, curvature) + cliff * 0.65f;
        w[1] += bCrys * (0.58f * flat);
        w[2] += bCrys * (0.28f + 0.25f * cliff);
        w[7] += bCrys * (0.35f + 0.85f * Clamp01(ridgeVein));
    }

    // Biome 5: Alpine Meltwater Deltas
    const float bDelta = blend.weights[5];
    if (bDelta > 0.001f) {
        const float shoal = Clamp01(wetness * 0.85f + Smoothstep(4.0f, 80.0f, riverDepth) * 0.80f + flow * 0.40f);
        w[6] += bDelta * (0.32f + 0.95f * shoal);
        w[5] += bDelta * (0.68f * flat * (1.0f - shoal * 0.65f));
        w[0] += bDelta * (0.35f * cliff);
    }

    // Global altitude snowcap & shoreline wet pebbles
    const float globalSnow = Smoothstep(snowLineZ + 600.0f, snowLineZ + 2200.0f, worldPos.z) * flat * (1.0f - bVolc * 0.85f);
    w[1] += globalSnow * 0.85f;
    const float shoreline = 1.0f - Smoothstep(seaLevelZ - 120.0f, seaLevelZ + 240.0f, worldPos.z);
    w[6] += shoreline * 0.45f * (1.0f - bFjord * 0.7f);

    float sum = 0.0f;
    int domIdx = 0;
    for (int i = 0; i < 8; ++i) {
        w[i] = std::max(0.0f, w[i]);
        sum += w[i];
        if (w[i] > w[domIdx]) domIdx = i;
    }
    const float inv = sum > 1.0e-6f ? (1.0f / sum) : 1.0f;
    for (int i = 0; i < 8; ++i) w[i] *= inv;

    MaterialSplat8 out{};
    out.splat0 = {w[0], w[1], w[2], w[3]};
    out.splat1 = {w[4], w[5], w[6], w[7]};
    out.emissiveMask = Clamp01(w[4] * 1.45f + w[7] * 0.85f);
    // Physical roughness estimate per dominant material mix (ice/crystal/wet cobble glossy; snow/moss/rock rougher)
    const float baseRough =
        w[0] * 0.78f + w[1] * 0.62f + w[2] * 0.14f + w[3] * 0.36f +
        w[4] * 0.28f + w[5] * 0.84f + w[6] * 0.54f + w[7] * 0.12f;
    out.roughnessHint = Clamp(Lerp(baseRough, 0.14f, wetness * 0.70f), 0.06f, 0.96f);
    out.dominant = static_cast<TerrainMaterialId>(domIdx);
    return out;
}

bool GenerateBiomeTerrainChunk(int32_t chunkX, int32_t chunkY, uint32_t lodLevel, const TerrainConfig& cfg,
                               uint8_t neighborCoarserMask, TerrainChunkData* outChunk) {
    if (!GenerateTerrainChunk(chunkX, chunkY, lodLevel, cfg, neighborCoarserMask, outChunk,
                              &BiomeElevationShaperAdapter, nullptr)) {
        return false;
    }
    const uint32_t S = outChunk->stride;
    const float step = outChunk->chunkSizeU / float(outChunk->gridN);
    for (uint32_t iy = 0; iy < S; ++iy) {
        const float wy = outChunk->originY + float(iy) * step;
        for (uint32_t ix = 0; ix < S; ++ix) {
            const float wx = outChunk->originX + float(ix) * step;
            const size_t vi = size_t(iy) * S + size_t(ix);
            const BiomeBlendResult blend = EvaluateBiomeBlend(wx, wy, cfg.seed);
            const MaterialSplat8 sp = EvaluateBiomeMaterials(
                {wx, wy, outChunk->heights[vi]}, outChunk->normals[vi], outChunk->curvatures[vi],
                outChunk->flow[vi], outChunk->erosion[vi], outChunk->wetness[vi], outChunk->riverDepth[vi],
                blend, cfg.seaLevelZ, cfg.snowLineZ, cfg.seed);
            outChunk->splat0[vi] = sp.splat0;
            outChunk->splat1[vi] = sp.splat1;
            outChunk->emissiveMask[vi] = sp.emissiveMask;
            outChunk->primaryBiome[vi] = static_cast<uint8_t>(blend.primary);
            outChunk->snow[vi] = sp.splat0.y;
        }
    }
    // Re-apply stitch mask so splat weights on coarser neighbor edges match seamlessly too!
    ApplySkirtlessLodStitch(*outChunk, neighborCoarserMask);
    return true;
}

uint32_t GenerateChunkProps(const TerrainChunkData& chunk, uint32_t seed, float minSpacingOverrideU,
                            std::vector<PropInstance>* outProps) {
    if (!outProps || chunk.heights.empty() || chunk.chunkSizeU < 64.0f) return 0u;
    outProps->clear();

    const float centerX = chunk.originX + chunk.chunkSizeU * 0.5f;
    const float centerY = chunk.originY + chunk.chunkSizeU * 0.5f;
    const BiomeBlendResult centerBiome = EvaluateBiomeBlend(centerX, centerY, seed);
    const BiomeSpec& spec = GetBiomeSpec(centerBiome.primary);

    const float minR = minSpacingOverrideU > 8.0f ? minSpacingOverrideU : spec.minPropSpacingU;
    const float cellSz = minR / 1.41421356f;
    const int gridDim = std::max(1, int(std::ceil(chunk.chunkSizeU / cellSz)));
    std::vector<int> spatialGrid(size_t(gridDim) * size_t(gridDim), -1);

    // Deterministic Bridson Poisson-disc sampling seeded by chunk coords + world seed.
    BiomeRng rng(Hash2I(chunk.chunkX, chunk.chunkY, seed ^ 0x9F4B2C17u));
    std::vector<Vec2> active;
    std::vector<Vec2> pts;

    auto fitsPoisson = [&](Vec2 cand) -> bool {
        if (cand.x < 0.0f || cand.y < 0.0f || cand.x >= chunk.chunkSizeU || cand.y >= chunk.chunkSizeU) return false;
        const int gx = int(cand.x / cellSz);
        const int gy = int(cand.y / cellSz);
        const float r2 = minR * minR;
        for (int dy = -2; dy <= 2; ++dy) {
            const int ny = gy + dy;
            if (ny < 0 || ny >= gridDim) continue;
            for (int dx = -2; dx <= 2; ++dx) {
                const int nx = gx + dx;
                if (nx < 0 || nx >= gridDim) continue;
                const int idx = spatialGrid[size_t(ny) * size_t(gridDim) + size_t(nx)];
                if (idx >= 0) {
                    const Vec2 d = pts[size_t(idx)] - cand;
                    if (Dot(d, d) < r2) return false;
                }
            }
        }
        return true;
    };

    auto pushPt = [&](Vec2 p) {
        const int idx = int(pts.size());
        pts.push_back(p);
        active.push_back(p);
        const int gx = std::min(gridDim - 1, std::max(0, int(p.x / cellSz)));
        const int gy = std::min(gridDim - 1, std::max(0, int(p.y / cellSz)));
        spatialGrid[size_t(gy) * size_t(gridDim) + size_t(gx)] = idx;
    };

    // Seed initial dart in chunk interior
    pushPt({rng.Range(minR * 0.25f, chunk.chunkSizeU - minR * 0.25f),
            rng.Range(minR * 0.25f, chunk.chunkSizeU - minR * 0.25f)});

    constexpr int kAttempts = 18;
    const size_t kMaxProps = 192u;
    while (!active.empty() && pts.size() < kMaxProps) {
        const size_t pick = size_t(rng.NextU32()) % active.size();
        const Vec2 base = active[pick];
        bool placed = false;
        for (int a = 0; a < kAttempts; ++a) {
            const float ang = rng.Next01() * kTwoPi;
            const float rad = rng.Range(minR, minR * 2.0f);
            const Vec2 cand{base.x + std::cos(ang) * rad, base.y + std::sin(ang) * rad};
            if (fitsPoisson(cand)) {
                pushPt(cand);
                placed = true;
                break;
            }
        }
        if (!placed) {
            active[pick] = active.back();
            active.pop_back();
        }
    }

    outProps->reserve(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) {
        const float u = Clamp01(pts[i].x / chunk.chunkSizeU);
        const float v = Clamp01(pts[i].y / chunk.chunkSizeU);
        const float wx = chunk.originX + pts[i].x;
        const float wy = chunk.originY + pts[i].y;
        const float wz = chunk.SampleHeightBilinear(u, v);
        const Vec3 n = chunk.SampleNormalBilinear(u, v);
        const BiomeBlendResult localB = EvaluateBiomeBlend(wx, wy, seed);
        const BiomeSpec& localSpec = GetBiomeSpec(localB.primary);

        // Density & slope gating
        if (rng.Next01() > localSpec.propDensity01) continue;
        if (n.z < 0.45f) continue;

        PropInstance inst{};
        inst.biome = localB.primary;
        inst.position = {wx, wy, wz};
        inst.upNormal = Normalize(n * 0.35f + Vec3{0.0f, 0.0f, 0.65f});
        inst.yawRad = rng.Next01() * kTwoPi;
        inst.seed = HashU32(rng.NextU32() ^ uint32_t(i * 2654435761u));

        const float roll = rng.Next01();
        switch (localB.primary) {
            case BiomeId::GlacialFjordSpires:
                if (roll < 0.24f) { inst.kind = PropKind::GlacialIceArch; inst.material = TerrainMaterialId::GlacialIce; inst.scale = rng.Range(0.95f, 1.65f); }
                else if (roll < 0.62f) { inst.kind = PropKind::ConiferTree; inst.material = TerrainMaterialId::MossMeadow; inst.scale = rng.Range(0.85f, 1.55f); }
                else if (roll < 0.84f) { inst.kind = PropKind::SnowDrift; inst.material = TerrainMaterialId::SnowPack; inst.scale = rng.Range(0.80f, 1.35f); }
                else { inst.kind = PropKind::AlpineBoulder; inst.material = TerrainMaterialId::RockGranite; inst.scale = rng.Range(0.75f, 1.40f); }
                break;
            case BiomeId::GeothermalVolcanicCaldera:
                if (roll < 0.38f) { inst.kind = PropKind::VolcanicSteamVent; inst.material = TerrainMaterialId::MagmaFissure; inst.scale = rng.Range(0.85f, 1.45f); }
                else if (roll < 0.78f) { inst.kind = PropKind::BasaltColumnCluster; inst.material = TerrainMaterialId::BasaltColumn; inst.scale = rng.Range(0.85f, 1.50f); }
                else { inst.kind = PropKind::AlpineBoulder; inst.material = TerrainMaterialId::BasaltColumn; inst.scale = rng.Range(0.80f, 1.45f); }
                break;
            case BiomeId::OldGrowthConiferHighlands:
                if (roll < 0.74f) { inst.kind = PropKind::ConiferTree; inst.material = TerrainMaterialId::MossMeadow; inst.scale = rng.Range(0.85f, 1.65f); }
                else if (roll < 0.88f) { inst.kind = PropKind::AlpineBoulder; inst.material = TerrainMaterialId::RockGranite; inst.scale = rng.Range(0.75f, 1.35f); }
                else { inst.kind = PropKind::AlpineGrassClump; inst.material = TerrainMaterialId::MossMeadow; inst.scale = rng.Range(0.85f, 1.40f); }
                break;
            case BiomeId::ObsidianBasaltCanyons:
                if (roll < 0.64f) { inst.kind = PropKind::BasaltColumnCluster; inst.material = TerrainMaterialId::BasaltColumn; inst.scale = rng.Range(0.85f, 1.55f); }
                else { inst.kind = PropKind::AlpineBoulder; inst.material = TerrainMaterialId::BasaltColumn; inst.scale = rng.Range(0.75f, 1.35f); }
                break;
            case BiomeId::BioluminescentCrystalTundra:
                if (roll < 0.44f) { inst.kind = PropKind::CrystalMonolith; inst.material = TerrainMaterialId::ArcaneCrystal; inst.scale = rng.Range(0.90f, 1.65f); }
                else if (roll < 0.76f) { inst.kind = PropKind::ConiferTree; inst.material = TerrainMaterialId::MossMeadow; inst.scale = rng.Range(0.80f, 1.45f); }
                else if (roll < 0.90f) { inst.kind = PropKind::SnowDrift; inst.material = TerrainMaterialId::SnowPack; inst.scale = rng.Range(0.80f, 1.30f); }
                else { inst.kind = PropKind::GlacialIceArch; inst.material = TerrainMaterialId::GlacialIce; inst.scale = rng.Range(0.75f, 1.25f); }
                break;
            case BiomeId::AlpineMeltwaterDeltas:
            default:
                if (roll < 0.45f) { inst.kind = PropKind::ConiferTree; inst.material = TerrainMaterialId::MossMeadow; inst.scale = rng.Range(0.80f, 1.45f); }
                else if (roll < 0.75f) { inst.kind = PropKind::AlpineGrassClump; inst.material = TerrainMaterialId::MossMeadow; inst.scale = rng.Range(0.85f, 1.45f); }
                else { inst.kind = PropKind::AlpineBoulder; inst.material = TerrainMaterialId::RiverPebbleSand; inst.scale = rng.Range(0.55f, 1.15f); }
                break;
        }
        outProps->push_back(inst);
    }
    return static_cast<uint32_t>(outProps->size());
}

}  // namespace raw::worldgen
