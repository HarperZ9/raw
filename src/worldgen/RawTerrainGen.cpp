// RawTerrainGen.cpp: Multi-scale procedural geological terrain synthesis engine (`raw::worldgen`).
// Original work, MIT.
#include "RawTerrainGen.h"

#include <algorithm>
#include <atomic>
#include <limits>

namespace raw::worldgen {

using TerrainHydrologyHookFn = void (*)(TerrainChunkData& chunk, const TerrainConfig& cfg);

namespace {

std::atomic<TerrainHydrologyHookFn> g_terrainHydrologyHook{nullptr};
std::atomic<bool>                   g_enableHydrologyErosion{false};

constexpr float kTwoPi = 6.28318530717958647692f;

struct FastRng {
    uint32_t s = 0x9E3779B9u;
    explicit FastRng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t NextU32() {
        uint32_t x = s;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        s = x;
        return x;
    }
    float Next01() { return float(NextU32() & 0x00FFFFFFu) * (1.0f / 16777216.0f); }
};

float SampleGridBilinear(const std::vector<float>& grid, int w, int h, float fx, float fy,
                         float* outGx, float* outGy) {
    const float cx = Clamp(fx, 0.0f, float(w - 2) + 0.999f);
    const float cy = Clamp(fy, 0.0f, float(h - 2) + 0.999f);
    const int ix = int(cx);
    const int iy = int(cy);
    const float u = cx - float(ix);
    const float v = cy - float(iy);

    const float h00 = grid[size_t(iy) * size_t(w) + size_t(ix)];
    const float h10 = grid[size_t(iy) * size_t(w) + size_t(ix + 1)];
    const float h01 = grid[size_t(iy + 1) * size_t(w) + size_t(ix)];
    const float h11 = grid[size_t(iy + 1) * size_t(w) + size_t(ix + 1)];

    if (outGx) *outGx = (h10 - h00) * (1.0f - v) + (h11 - h01) * v;
    if (outGy) *outGy = (h01 - h00) * (1.0f - u) + (h11 - h10) * u;

    const float a = h00 + (h10 - h00) * u;
    const float b = h01 + (h11 - h01) * u;
    return a + (b - a) * v;
}

float EvaluateBaseHeightAt(float wx, float wy, const TerrainConfig& cfg,
                           ElevationShaperFn shaper, void* shaperUser,
                           TectonicSample* outTec, HydrographySample* outHydro, float* outBlend) {
    const float blend = EvaluateTamrielBorderBlend(wx, wy, cfg.border);
    const TectonicSample tec = EvaluateTectonicPlate(wx, wy, cfg);
    const HydrographySample hydro = EvaluateHydrography(wx, wy, tec, cfg);
    float h = tec.baseHeightU;
    if (shaper) {
        h = shaper(wx, wy, tec, hydro, cfg.seed, shaperUser);
    } else {
        h -= hydro.riverDepthU;
        if (hydro.lakeMask01 > 0.01f && h < hydro.waterLevelZ) {
            h = Lerp(h, hydro.waterLevelZ - 18.0f, hydro.lakeMask01 * 0.45f);
        }
    }
    if (cfg.border.enabled) {
        h = Lerp(cfg.border.tamrielEdgeBaseZ, h, blend);
    }
    if (outTec) *outTec = tec;
    if (outHydro) *outHydro = hydro;
    if (outBlend) *outBlend = blend;
    return h;
}

}  // namespace

uint32_t HashU32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

uint32_t Hash2I(int32_t x, int32_t y, uint32_t seed) {
    uint32_t h = uint32_t(x) * 374761393u ^ uint32_t(y) * 668265263u ^ (seed * 2246822519u);
    return HashU32(h);
}

uint32_t Hash3I(int32_t x, int32_t y, int32_t z, uint32_t seed) {
    uint32_t h = uint32_t(x) * 374761393u ^ uint32_t(y) * 668265263u ^
                 uint32_t(z) * 1442695041u ^ (seed * 2246822519u);
    return HashU32(h);
}

float HashTo01(uint32_t h) {
    return float(h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

Vec3 ValueNoise2DGrad(float x, float y, uint32_t seed) {
    const int32_t ix = int32_t(std::floor(x));
    const int32_t iy = int32_t(std::floor(y));
    const float fx = x - float(ix);
    const float fy = y - float(iy);

    const float ux = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
    const float uy = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);
    const float dux = 30.0f * fx * fx * (fx * (fx - 2.0f) + 1.0f);
    const float duy = 30.0f * fy * fy * (fy * (fy - 2.0f) + 1.0f);

    const float a = HashTo01(Hash2I(ix, iy, seed));
    const float b = HashTo01(Hash2I(ix + 1, iy, seed));
    const float c = HashTo01(Hash2I(ix, iy + 1, seed));
    const float d = HashTo01(Hash2I(ix + 1, iy + 1, seed));

    const float k0 = a;
    const float k1 = b - a;
    const float k2 = c - a;
    const float k3 = a - b - c + d;

    const float val = k0 + k1 * ux + k2 * uy + k3 * ux * uy;
    const float gx = dux * (k1 + k3 * uy);
    const float gy = duy * (k2 + k3 * ux);
    return {val, gx, gy};
}

Vec4 ValueNoise3DGrad(float x, float y, float z, uint32_t seed) {
    const int32_t ix = int32_t(std::floor(x));
    const int32_t iy = int32_t(std::floor(y));
    const int32_t iz = int32_t(std::floor(z));
    const float fx = x - float(ix);
    const float fy = y - float(iy);
    const float fz = z - float(iz);

    const float ux = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
    const float uy = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);
    const float uz = fz * fz * fz * (fz * (fz * 6.0f - 15.0f) + 10.0f);
    const float dux = 30.0f * fx * fx * (fx * (fx - 2.0f) + 1.0f);
    const float duy = 30.0f * fy * fy * (fy * (fy - 2.0f) + 1.0f);
    const float duz = 30.0f * fz * fz * (fz * (fz - 2.0f) + 1.0f);

    const float a = HashTo01(Hash3I(ix, iy, iz, seed));
    const float b = HashTo01(Hash3I(ix + 1, iy, iz, seed));
    const float c = HashTo01(Hash3I(ix, iy + 1, iz, seed));
    const float d = HashTo01(Hash3I(ix + 1, iy + 1, iz, seed));
    const float e = HashTo01(Hash3I(ix, iy, iz + 1, seed));
    const float f1 = HashTo01(Hash3I(ix + 1, iy, iz + 1, seed));
    const float g = HashTo01(Hash3I(ix, iy + 1, iz + 1, seed));
    const float h = HashTo01(Hash3I(ix + 1, iy + 1, iz + 1, seed));

    const float k0 = a;
    const float k1 = b - a;
    const float k2 = c - a;
    const float k3 = e - a;
    const float k4 = a - b - c + d;
    const float k5 = a - c - e + g;
    const float k6 = a - b - e + f1;
    const float k7 = -a + b + c - d + e - f1 - g + h;

    const float val = k0 + k1 * ux + k2 * uy + k3 * uz +
                      k4 * ux * uy + k5 * uy * uz + k6 * uz * ux + k7 * ux * uy * uz;
    const float gx = dux * (k1 + k4 * uy + k6 * uz + k7 * uy * uz);
    const float gy = duy * (k2 + k5 * uz + k4 * ux + k7 * uz * ux);
    const float gz = duz * (k3 + k6 * ux + k5 * uy + k7 * ux * uy);
    return {val, gx, gy, gz};
}

float Fbm2D(float x, float y, uint32_t seed, int octaves, float lacunarity, float gain) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f, freq = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        const Vec3 n = ValueNoise2DGrad(x * freq, y * freq, seed + uint32_t(i) * 1013u);
        sum += n.x * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.0f ? (sum / norm) : 0.0f;
}

Vec3 Fbm2DGrad(float x, float y, uint32_t seed, int octaves, float lacunarity, float gain) {
    float sum = 0.0f, gx = 0.0f, gy = 0.0f, amp = 0.5f, norm = 0.0f, freq = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        const Vec3 n = ValueNoise2DGrad(x * freq, y * freq, seed + uint32_t(i) * 1013u);
        sum += n.x * amp;
        gx += n.y * amp * freq;
        gy += n.z * amp * freq;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    if (norm <= 0.0f) return {0.0f, 0.0f, 0.0f};
    const float inv = 1.0f / norm;
    return {sum * inv, gx * inv, gy * inv};
}

float RidgedMultifractal2D(float x, float y, uint32_t seed, int octaves, float lacunarity, float gain) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f, freq = 1.0f, weight = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        const float n = ValueNoise2DGrad(x * freq, y * freq, seed + uint32_t(i) * 1597u).x;
        float sig = 1.0f - std::fabs(n * 2.0f - 1.0f);
        sig = sig * sig * weight;
        weight = Clamp(sig * 1.85f, 0.15f, 1.0f);
        sum += sig * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.0f ? (sum / norm) : 0.0f;
}

WorleyResult Worley2D(float x, float y, uint32_t seed) {
    const int32_t cx = int32_t(std::floor(x));
    const int32_t cy = int32_t(std::floor(y));
    WorleyResult res{};
    res.f1 = 1e9f;
    res.f2 = 1e9f;
    for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            const int32_t nx = cx + dx;
            const int32_t ny = cy + dy;
            const uint32_t h1 = Hash2I(nx, ny, seed);
            const uint32_t h2 = Hash2I(nx, ny, seed ^ 0xA511E9B3u);
            const float px = float(nx) + 0.12f + 0.76f * HashTo01(h1);
            const float py = float(ny) + 0.12f + 0.76f * HashTo01(h2);
            const float dist = std::hypot(x - px, y - py);
            if (dist < res.f1) {
                res.f2 = res.f1;
                res.f1 = dist;
                res.cellId = h1;
                res.cellCenter = {px, py};
            } else if (dist < res.f2) {
                res.f2 = dist;
            }
        }
    }
    res.edgeDist = std::max(0.0f, res.f2 - res.f1);
    return res;
}

Vec2 DomainWarp2D(float x, float y, uint32_t seed, float strength, float freq) {
    const float wx = (Fbm2D(x * freq + 17.3f, y * freq - 9.1f, seed + 41u, 3) - 0.5f) * 2.0f;
    const float wy = (Fbm2D(x * freq - 11.7f, y * freq + 23.4f, seed + 83u, 3) - 0.5f) * 2.0f;
    return {x + wx * strength, y + wy * strength};
}

float EvaluateTamrielBorderBlend(float worldX, float worldY, const TamrielBorderConfig& cfg) {
    if (!cfg.enabled) return 1.0f;
    const float dx = std::max(0.0f, std::fabs(worldX) - cfg.halfExtentX);
    const float dy = std::max(0.0f, std::fabs(worldY) - cfg.halfExtentY);
    const float distOutside = std::hypot(dx, dy);
    if (distOutside <= 0.0f) return 0.0f;
    const float apron = std::max(cfg.blendApronU, 1.0f);
    return QuinticSmoothstep(0.0f, apron, distOutside);
}

TectonicSample EvaluateTectonicPlate(float worldX, float worldY, const TerrainConfig& cfg) {
    TectonicSample out{};
    const Vec2 warped = DomainWarp2D(worldX, worldY, cfg.seed + 19u, 9200.0f, 1.0f / 42000.0f);

    // Worley tectonic plates (~56,000 world units across)
    const WorleyResult plate = Worley2D(warped.x * (1.0f / 56000.0f), warped.y * (1.0f / 56000.0f), cfg.seed + 101u);
    const float plateBase = HashTo01(plate.cellId);
    const float macroFbm = Fbm2D(warped.x * (1.0f / 38000.0f), warped.y * (1.0f / 38000.0f), cfg.seed + 211u, 4);

    // Plate boundary compression yields mountain ranges; divergence yields volcanic faults & rifts.
    const float boundaryProximity = 1.0f - Smoothstep(0.02f, 0.34f, plate.edgeDist);
    const float ridgeNoise = RidgedMultifractal2D(warped.x * (1.0f / 14500.0f), warped.y * (1.0f / 14500.0f),
                                                  cfg.seed + 307u, 5, 2.08f, 0.52f);
    const float detailHills = Fbm2D(worldX * (1.0f / 4800.0f), worldY * (1.0f / 4800.0f), cfg.seed + 409u, 4);

    out.continent01 = Clamp01(0.55f * plateBase + 0.45f * macroFbm);
    out.ridge01 = Clamp01(ridgeNoise * (0.45f + 0.75f * boundaryProximity));
    out.fault01 = Clamp01(boundaryProximity * Smoothstep(0.35f, 0.85f, HashTo01(plate.cellId ^ 0x55AA55AAu)));

    const float shelfZ = cfg.seaLevelZ + (out.continent01 - 0.34f) * cfg.heightScaleU * 0.85f;
    const float alpineZ = std::pow(out.ridge01, 1.45f) * cfg.heightScaleU * 0.92f;
    const float rollZ = (detailHills - 0.5f) * cfg.heightScaleU * 0.22f;
    out.baseHeightU = shelfZ + alpineZ + rollZ;
    return out;
}

HydrographySample EvaluateHydrography(float worldX, float worldY, const TectonicSample& tec, const TerrainConfig& cfg) {
    HydrographySample out{};
    const Vec2 mw = DomainWarp2D(worldX, worldY, cfg.seed + 521u, 3600.0f, 1.0f / 19000.0f);
    const WorleyResult basin = Worley2D(mw.x * (1.0f / 26000.0f), mw.y * (1.0f / 26000.0f), cfg.seed + 613u);

    // Main river channels follow Voronoi drainage valley seams (edgeDist near 0).
    const float meander = (ValueNoise2DGrad(worldX * (1.0f / 6500.0f), worldY * (1.0f / 6500.0f), cfg.seed + 701u).x - 0.5f) * 0.035f;
    const float chanDist = std::fabs(basin.edgeDist + meander);
    const float lowlandGate = 1.0f - Smoothstep(cfg.snowLineZ + 600.0f, cfg.snowLineZ + 2200.0f, tec.baseHeightU);
    out.riverMask01 = (1.0f - Smoothstep(0.008f, 0.068f, chanDist)) * lowlandGate;

    // Smooth parabolic river channel incision
    out.riverDepthU = out.riverMask01 * out.riverMask01 * (260.0f + 140.0f * (1.0f - tec.ridge01));

    // Alpine meltwater lakes form inside flat basin depressions
    const float lakeCandidate = (1.0f - Smoothstep(0.08f, 0.26f, basin.f1)) * Smoothstep(0.32f, 0.65f, tec.continent01);
    out.lakeMask01 = Clamp01(lakeCandidate * (1.0f - tec.ridge01));
    out.waterLevelZ = std::max(cfg.seaLevelZ, cfg.seaLevelZ + 480.0f + 900.0f * HashTo01(basin.cellId ^ 0x13579BDFu));

    // Bank & coastal wetness halo
    const float riverBankWet = (1.0f - Smoothstep(0.03f, 0.16f, chanDist)) * lowlandGate;
    const float coastalWet = 1.0f - Smoothstep(cfg.seaLevelZ - 100.0f, cfg.seaLevelZ + 380.0f, tec.baseHeightU);
    out.wetness01 = Clamp01(std::max({out.riverMask01, riverBankWet * 0.85f, out.lakeMask01, coastalWet}));

    // Flow direction points roughly perpendicular to cell center offset along basin seam
    const Vec2 toCenter{basin.cellCenter.x * 26000.0f - mw.x, basin.cellCenter.y * 26000.0f - mw.y};
    out.flowDir = Normalize(Vec2{-toCenter.y, toCenter.x});
    return out;
}

TerrainSample SampleTerrainPoint(float worldX, float worldY, const TerrainConfig& cfg,
                                 ElevationShaperFn shaper, void* shaperUser) {
    TectonicSample tec{};
    HydrographySample hydro{};
    float blend = 1.0f;
    const float hC = EvaluateBaseHeightAt(worldX, worldY, cfg, shaper, shaperUser, &tec, &hydro, &blend);

    // High-precision symmetric finite-difference step for exact derivatives & Laplacian mean curvature.
    const float eps = std::max(cfg.chunkSizeU / 128.0f, 8.0f);
    const float hPx = EvaluateBaseHeightAt(worldX + eps, worldY, cfg, shaper, shaperUser, nullptr, nullptr, nullptr);
    const float hNx = EvaluateBaseHeightAt(worldX - eps, worldY, cfg, shaper, shaperUser, nullptr, nullptr, nullptr);
    const float hPy = EvaluateBaseHeightAt(worldX, worldY + eps, cfg, shaper, shaperUser, nullptr, nullptr, nullptr);
    const float hNy = EvaluateBaseHeightAt(worldX, worldY - eps, cfg, shaper, shaperUser, nullptr, nullptr, nullptr);

    const float dhdx = (hPx - hNx) / (2.0f * eps);
    const float dhdy = (hPy - hNy) / (2.0f * eps);
    const Vec3 n = Normalize({-dhdx, -dhdy, 1.0f});

    const float d2x = (hPx - 2.0f * hC + hNx) / (eps * eps);
    const float d2y = (hPy - 2.0f * hC + hNy) / (eps * eps);
    const float meanCurv = Clamp(-(d2x + d2y) * 140.0f, -1.0f, 1.0f);

    TerrainSample s{};
    s.heightZ = hC;
    s.normal = n;
    s.meanCurvature = meanCurv;
    s.slope01 = Clamp01(1.0f - n.z);
    s.riverDepthU = hydro.riverDepthU * blend;
    s.flowAccum01 = Clamp01(hydro.riverMask01 * 0.85f + std::max(0.0f, -meanCurv) * 0.45f);
    s.erosionWear01 = Clamp01(s.slope01 * 0.7f + s.flowAccum01 * 0.4f);
    const float altSnow = Smoothstep(cfg.snowLineZ - 500.0f, cfg.snowLineZ + 900.0f, hC);
    const float flatGate = Smoothstep(0.32f, 0.78f, n.z);
    s.snowMask01 = Clamp01(altSnow * flatGate);
    s.wetness01 = Clamp01(hydro.wetness01 * blend);
    s.borderBlend01 = blend;
    return s;
}

OverhangSdfResult EvaluateOverhangSdf(Vec3 worldPos, float terrainHeightAtXY, float slope01,
                                      uint8_t primaryBiome, uint32_t seed) {
    // Base signed distance to heightfield surface: >0 below surface (solid), <0 above surface (air).
    // Note: positive inside solid rock/ice, negative in open air/carved void!
    auto evalScalar = [&](Vec3 p, float& outArch, float& outCave) -> float {
        float dSolid = terrainHeightAtXY - p.z;
        outArch = 0.0f;
        outCave = 0.0f;

        // 1. Stratified cliff overhang undercuts on steep escarpments (slope01 > 0.30)
        const float cliffW = Smoothstep(0.28f, 0.65f, slope01);
        if (cliffW > 0.01f) {
            const Vec4 strata = ValueNoise3DGrad(p.x * 0.0022f, p.y * 0.0022f, p.z * 0.0095f, seed + 901u);
            const float undercut = (strata.x - 0.56f) * 220.0f * cliffW;
            dSolid -= undercut;
        }

        // 2. Glacial Fjord sea-arches & Basalt Canyon natural stone bridges
        // Periodic deterministic arch cells every ~3072 world units along fjords/canyons
        const float cellSpan = 3072.0f;
        const int32_t ax = int32_t(std::floor(p.x / cellSpan));
        const int32_t ay = int32_t(std::floor(p.y / cellSpan));
        const uint32_t ah = Hash2I(ax, ay, seed + 1201u);
        const float archChance = (primaryBiome == 0u || primaryBiome == 3u) ? 0.62f : 0.18f;
        if (HashTo01(ah) < archChance) {
            const float cx = (float(ax) + 0.5f) * cellSpan;
            const float cy = (float(ay) + 0.5f) * cellSpan;
            const float ang = HashTo01(ah ^ 0x3C6EF372u) * kTwoPi;
            const float ca = std::cos(ang), sa = std::sin(ang);
            const float lx = (p.x - cx) * ca + (p.y - cy) * sa;
            const float ly = -(p.x - cx) * sa + (p.y - cy) * ca;
            const float archBaseZ = terrainHeightAtXY - 260.0f;
            const float lz = p.z - archBaseZ;
            // Elliptical arch tunnel bore along local X (width 420u, height 240u, length 540u)
            const float boreR = std::hypot(ly / 210.0f, lz / 180.0f);
            const float lenGate = Smoothstep(380.0f, 180.0f, std::fabs(lx));
            if (lenGate > 0.001f) {
                const float tunnelVoid = (1.0f - boreR) * 180.0f * lenGate;
                if (tunnelVoid > 0.0f) {
                    dSolid -= tunnelVoid; // Carve open air beneath the arch span!
                    outArch = Clamp01(lenGate * (1.0f - Smoothstep(0.65f, 1.15f, boreR)));
                }
            }
        }

        // 3. Subsurface ice caves in Bioluminescent Crystal Tundra (biome 4) or Glacial Fjord (biome 0)
        if (primaryBiome == 4u || primaryBiome == 0u) {
            const float depthBelow = terrainHeightAtXY - p.z;
            if (depthBelow > 80.0f && depthBelow < 900.0f) {
                const Vec4 c1 = ValueNoise3DGrad(p.x * 0.0016f, p.y * 0.0016f, p.z * 0.0032f, seed + 1409u);
                const Vec4 c2 = ValueNoise3DGrad(p.x * 0.0016f + 13.7f, p.y * 0.0016f - 8.2f, p.z * 0.0032f, seed + 1481u);
                const float worm = std::hypot(c1.x - 0.5f, c2.x - 0.5f);
                const float caveClear = (0.145f - worm) * 780.0f;
                if (caveClear > 0.0f) {
                    dSolid -= caveClear;
                    outCave = Clamp01(caveClear / 160.0f);
                }
            }
        }
        return dSolid;
    };

    OverhangSdfResult res{};
    res.sdfU = evalScalar(worldPos, res.archMask01, res.caveMask01);

    const float e = 6.0f;
    float dummyA = 0.0f, dummyC = 0.0f;
    const float gx = evalScalar({worldPos.x + e, worldPos.y, worldPos.z}, dummyA, dummyC) -
                     evalScalar({worldPos.x - e, worldPos.y, worldPos.z}, dummyA, dummyC);
    const float gy = evalScalar({worldPos.x, worldPos.y + e, worldPos.z}, dummyA, dummyC) -
                     evalScalar({worldPos.x, worldPos.y - e, worldPos.z}, dummyA, dummyC);
    const float gz = evalScalar({worldPos.x, worldPos.y, worldPos.z + e}, dummyA, dummyC) -
                     evalScalar({worldPos.x, worldPos.y, worldPos.z - e}, dummyA, dummyC);
    // Outward air normal points toward decreasing solid SDF (-grad)
    res.normal = Normalize({-gx, -gy, -gz});
    return res;
}

float TerrainChunkData::SampleHeightBilinear(float u, float v) const {
    if (heights.empty() || stride < 2u) return 0.0f;
    const float fx = Clamp01(u) * float(gridN);
    const float fy = Clamp01(v) * float(gridN);
    return SampleGridBilinear(heights, int(stride), int(stride), fx, fy, nullptr, nullptr);
}

Vec3 TerrainChunkData::SampleNormalBilinear(float u, float v) const {
    if (normals.empty() || stride < 2u) return {0.0f, 0.0f, 1.0f};
    const float fx = Clamp(Clamp01(u) * float(gridN), 0.0f, float(gridN) - 0.001f);
    const float fy = Clamp(Clamp01(v) * float(gridN), 0.0f, float(gridN) - 0.001f);
    const uint32_t ix = uint32_t(fx);
    const uint32_t iy = uint32_t(fy);
    const float su = fx - float(ix);
    const float sv = fy - float(iy);
    const Vec3 n00 = normals[iy * stride + ix];
    const Vec3 n10 = normals[iy * stride + ix + 1u];
    const Vec3 n01 = normals[(iy + 1u) * stride + ix];
    const Vec3 n11 = normals[(iy + 1u) * stride + ix + 1u];
    const Vec3 a = n00 * (1.0f - su) + n10 * su;
    const Vec3 b = n01 * (1.0f - su) + n11 * su;
    return Normalize(a * (1.0f - sv) + b * sv);
}

void ApplySkirtlessLodStitch(TerrainChunkData& chunk, uint8_t neighborCoarserMask) {
    chunk.stitchMask = neighborCoarserMask;
    if (neighborCoarserMask == 0u || chunk.gridN < 2u || chunk.heights.size() != size_t(chunk.stride) * chunk.stride) {
        return;
    }
    const uint32_t N = chunk.gridN;
    const uint32_t S = chunk.stride;
    auto lerpVert = [&](uint32_t dst, uint32_t a, uint32_t b) {
        chunk.heights[dst] = 0.5f * (chunk.heights[a] + chunk.heights[b]);
        if (chunk.normals.size() == chunk.heights.size()) {
            chunk.normals[dst] = Normalize(chunk.normals[a] + chunk.normals[b]);
        }
        if (chunk.curvatures.size() == chunk.heights.size()) {
            chunk.curvatures[dst] = 0.5f * (chunk.curvatures[a] + chunk.curvatures[b]);
        }
        if (chunk.splat0.size() == chunk.heights.size()) {
            chunk.splat0[dst] = {
                0.5f * (chunk.splat0[a].x + chunk.splat0[b].x),
                0.5f * (chunk.splat0[a].y + chunk.splat0[b].y),
                0.5f * (chunk.splat0[a].z + chunk.splat0[b].z),
                0.5f * (chunk.splat0[a].w + chunk.splat0[b].w),
            };
        }
        if (chunk.splat1.size() == chunk.heights.size()) {
            chunk.splat1[dst] = {
                0.5f * (chunk.splat1[a].x + chunk.splat1[b].x),
                0.5f * (chunk.splat1[a].y + chunk.splat1[b].y),
                0.5f * (chunk.splat1[a].z + chunk.splat1[b].z),
                0.5f * (chunk.splat1[a].w + chunk.splat1[b].w),
            };
        }
    };

    if (neighborCoarserMask & StitchNegX) {
        for (uint32_t iy = 1u; iy < N; iy += 2u) {
            lerpVert(iy * S + 0u, (iy - 1u) * S + 0u, (iy + 1u) * S + 0u);
        }
    }
    if (neighborCoarserMask & StitchPosX) {
        for (uint32_t iy = 1u; iy < N; iy += 2u) {
            lerpVert(iy * S + N, (iy - 1u) * S + N, (iy + 1u) * S + N);
        }
    }
    if (neighborCoarserMask & StitchNegY) {
        for (uint32_t ix = 1u; ix < N; ix += 2u) {
            lerpVert(0u * S + ix, 0u * S + (ix - 1u), 0u * S + (ix + 1u));
        }
    }
    if (neighborCoarserMask & StitchPosY) {
        for (uint32_t ix = 1u; ix < N; ix += 2u) {
            lerpVert(N * S + ix, N * S + (ix - 1u), N * S + (ix + 1u));
        }
    }
}

bool GenerateTerrainChunk(int32_t chunkX, int32_t chunkY, uint32_t lodLevel, const TerrainConfig& cfg,
                          uint8_t neighborCoarserMask, TerrainChunkData* outChunk,
                          ElevationShaperFn shaper, void* shaperUser) {
    if (!outChunk || lodLevel > 3u || cfg.chunkSizeU < 64.0f || cfg.heightScaleU <= 0.0f) {
        return false;
    }

    const uint32_t gridN = 64u >> lodLevel; // 64, 32, 16, 8
    const uint32_t stride = gridN + 1u;
    const float cellStep = cfg.chunkSizeU / float(gridN);
    const float originX = float(chunkX) * cfg.chunkSizeU;
    const float originY = float(chunkY) * cfg.chunkSizeU;

    outChunk->chunkX = chunkX;
    outChunk->chunkY = chunkY;
    outChunk->lodLevel = lodLevel;
    outChunk->gridN = gridN;
    outChunk->stride = stride;
    outChunk->stitchMask = neighborCoarserMask;
    outChunk->chunkSizeU = cfg.chunkSizeU;
    outChunk->originX = originX;
    outChunk->originY = originY;

    const size_t vertCount = size_t(stride) * stride;
    outChunk->heights.assign(vertCount, 0.0f);
    outChunk->normals.assign(vertCount, {0.0f, 0.0f, 1.0f});
    outChunk->curvatures.assign(vertCount, 0.0f);
    outChunk->flow.assign(vertCount, 0.0f);
    outChunk->erosion.assign(vertCount, 0.0f);
    outChunk->snow.assign(vertCount, 0.0f);
    outChunk->wetness.assign(vertCount, 0.0f);
    outChunk->riverDepth.assign(vertCount, 0.0f);
    outChunk->borderBlend.assign(vertCount, 1.0f);
    outChunk->splat0.assign(vertCount, {1.0f, 0.0f, 0.0f, 0.0f});
    outChunk->splat1.assign(vertCount, {0.0f, 0.0f, 0.0f, 0.0f});
    outChunk->emissiveMask.assign(vertCount, 0.0f);
    outChunk->primaryBiome.assign(vertCount, 0u);

    // Build halo-padded elevation grid so erosion & central-difference derivatives are clean at chunk edges.
    constexpr int kHalo = 3;
    const int extW = int(stride) + 2 * kHalo;
    std::vector<float> extBase(size_t(extW) * size_t(extW), 0.0f);
    std::vector<float> extH(size_t(extW) * size_t(extW), 0.0f);
    std::vector<float> extFlow(size_t(extW) * size_t(extW), 0.0f);
    std::vector<float> extErode(size_t(extW) * size_t(extW), 0.0f);

    for (int gy = 0; gy < extW; ++gy) {
        const float wy = originY + float(gy - kHalo) * cellStep;
        for (int gx = 0; gx < extW; ++gx) {
            const float wx = originX + float(gx - kHalo) * cellStep;
            TectonicSample tec{};
            HydrographySample hydro{};
            float blend = 1.0f;
            const float h = EvaluateBaseHeightAt(wx, wy, cfg, shaper, shaperUser, &tec, &hydro, &blend);
            const size_t idx = size_t(gy) * size_t(extW) + size_t(gx);
            extBase[idx] = h;
            extH[idx] = h;
            if (gx >= kHalo && gx < kHalo + int(stride) && gy >= kHalo && gy < kHalo + int(stride)) {
                const size_t vi = size_t(gy - kHalo) * stride + size_t(gx - kHalo);
                outChunk->wetness[vi] = Clamp01(hydro.wetness01 * blend);
                outChunk->riverDepth[vi] = hydro.riverDepthU * blend;
                outChunk->borderBlend[vi] = blend;
            }
        }
    }

    // 1. Hydraulic Droplet Erosion (stream-power deposition/carving on halo grid)
    if (cfg.enableHydraulicErosion && cfg.hydraulicDroplets > 0u) {
        const uint32_t droplets = std::max(16u, cfg.hydraulicDroplets >> lodLevel);
        FastRng rng(Hash2I(chunkX, chunkY, cfg.seed ^ 0xE205104Du));
        constexpr int kMaxSteps = 32;
        const float inertia = 0.22f;
        const float minSlope = 0.01f;
        const float capacityFactor = 4.5f;
        const float depositRate = 0.24f;
        const float erodeRate = 0.28f;
        const float evaporate = 0.04f;
        const float gravity = 4.0f;

        for (uint32_t d = 0; d < droplets; ++d) {
            float px = 1.5f + rng.Next01() * float(extW - 3);
            float py = 1.5f + rng.Next01() * float(extW - 3);
            float dx = 0.0f, dy = 0.0f;
            float speed = 1.0f, water = 1.0f, sediment = 0.0f;

            for (int step = 0; step < kMaxSteps; ++step) {
                const int ix = int(px);
                const int iy = int(py);
                if (ix < 1 || iy < 1 || ix >= extW - 2 || iy >= extW - 2) break;
                const float u = px - float(ix);
                const float v = py - float(iy);

                float gx = 0.0f, gy = 0.0f;
                const float hOld = SampleGridBilinear(extH, extW, extW, px, py, &gx, &gy);
                gx /= cellStep;
                gy /= cellStep;

                dx = dx * inertia - gx * (1.0f - inertia);
                dy = dy * inertia - gy * (1.0f - inertia);
                const float len = std::hypot(dx, dy);
                if (len < 1.0e-5f) {
                    const float ang = rng.Next01() * kTwoPi;
                    dx = std::cos(ang);
                    dy = std::sin(ang);
                } else {
                    dx /= len;
                    dy /= len;
                }

                const float nx = px + dx;
                const float ny = py + dy;
                if (nx < 1.0f || ny < 1.0f || nx >= float(extW - 2) || ny >= float(extW - 2)) break;

                const float hNew = SampleGridBilinear(extH, extW, extW, nx, ny, nullptr, nullptr);
                const float dH = hNew - hOld;
                const float cap = std::max(-dH, minSlope * cellStep) * speed * water * capacityFactor;

                const size_t i00 = size_t(iy) * size_t(extW) + size_t(ix);
                const size_t i10 = i00 + 1u;
                const size_t i01 = i00 + size_t(extW);
                const size_t i11 = i01 + 1u;
                const float w00 = (1.0f - u) * (1.0f - v);
                const float w10 = u * (1.0f - v);
                const float w01 = (1.0f - u) * v;
                const float w11 = u * v;

                extFlow[i00] += water * w00 * 0.25f;
                extFlow[i10] += water * w10 * 0.25f;
                extFlow[i01] += water * w01 * 0.25f;
                extFlow[i11] += water * w11 * 0.25f;

                if (sediment > cap || dH > 0.0f) {
                    const float dep = (dH > 0.0f) ? std::min(dH, sediment) : (sediment - cap) * depositRate;
                    sediment -= dep;
                    extH[i00] += dep * w00;
                    extH[i10] += dep * w10;
                    extH[i01] += dep * w01;
                    extH[i11] += dep * w11;
                } else {
                    const float ero = std::min((cap - sediment) * erodeRate, -dH * 0.85f);
                    const float clampedEro = std::min(ero, cellStep * 0.22f);
                    sediment += clampedEro;
                    extH[i00] -= clampedEro * w00;
                    extH[i10] -= clampedEro * w10;
                    extH[i01] -= clampedEro * w01;
                    extH[i11] -= clampedEro * w11;
                    extErode[i00] += clampedEro * w00;
                    extErode[i10] += clampedEro * w10;
                    extErode[i01] += clampedEro * w01;
                    extErode[i11] += clampedEro * w11;
                }

                speed = std::sqrt(std::max(speed * speed - dH * gravity / std::max(cellStep, 1.0f), 0.1f));
                water *= (1.0f - evaporate);
                px = nx;
                py = ny;
                if (water < 0.02f) break;
            }
        }
    }

    // 2. Thermal Talus Erosion (angle-of-repose scree relaxation)
    const float maxDiff = cfg.talusAngleTan * cellStep;
    std::vector<float> talusDelta(extH.size(), 0.0f);
    for (uint32_t it = 0; it < cfg.thermalIterations; ++it) {
        std::fill(talusDelta.begin(), talusDelta.end(), 0.0f);
        for (int gy = 1; gy < extW - 1; ++gy) {
            for (int gx = 1; gx < extW - 1; ++gx) {
                const size_t c = size_t(gy) * size_t(extW) + size_t(gx);
                const float hc = extH[c];
                const size_t nb[4] = {c - 1u, c + 1u, c - size_t(extW), c + size_t(extW)};
                float dMax = 0.0f, dSum = 0.0f;
                float diffs[4] = {};
                for (int k = 0; k < 4; ++k) {
                    const float diff = hc - extH[nb[k]];
                    if (diff > maxDiff) {
                        diffs[k] = diff - maxDiff;
                        dSum += diffs[k];
                        dMax = std::max(dMax, diffs[k]);
                    }
                }
                if (dSum > 1.0e-5f) {
                    const float move = 0.35f * dMax;
                    talusDelta[c] -= move;
                    extErode[c] += move * 0.5f;
                    for (int k = 0; k < 4; ++k) {
                        if (diffs[k] > 0.0f) {
                            talusDelta[nb[k]] += move * (diffs[k] / dSum);
                        }
                    }
                }
            }
        }
        for (size_t i = 0; i < extH.size(); ++i) extH[i] += talusDelta[i];
    }

    // 3. Copy eroded heights into chunk with boundary-consistent taper (ensures bit-exact watertight seams!).
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();
    for (uint32_t iy = 0; iy < stride; ++iy) {
        const float edgeY = std::min(float(iy), float(gridN - iy));
        const float fadeY = Smoothstep(0.0f, 2.0f, edgeY);
        for (uint32_t ix = 0; ix < stride; ++ix) {
            const float edgeX = std::min(float(ix), float(gridN - ix));
            const float fade = fadeY * Smoothstep(0.0f, 2.0f, edgeX);
            const size_t ei = size_t(iy + kHalo) * size_t(extW) + size_t(ix + kHalo);
            const size_t vi = size_t(iy) * stride + size_t(ix);
            const float blendedH = Lerp(extBase[ei], extH[ei], fade * outChunk->borderBlend[vi]);
            outChunk->heights[vi] = blendedH;
            minZ = std::min(minZ, blendedH);
            maxZ = std::max(maxZ, blendedH);
            outChunk->flow[vi] = Clamp01(extFlow[ei] * 0.55f + (outChunk->riverDepth[vi] > 1.0f ? 0.65f : 0.0f));
            outChunk->erosion[vi] = Clamp01(extErode[ei] / std::max(cellStep * 0.35f, 1.0f));
        }
    }
    outChunk->minZ = minZ;
    outChunk->maxZ = maxZ;

    // 4. Compute exact analytical/central normals, mean curvature & default snow/rock splat.
    const float inv2Step = 1.0f / (2.0f * cellStep);
    const float invStep2 = 1.0f / (cellStep * cellStep);
    for (uint32_t iy = 0; iy < stride; ++iy) {
        for (uint32_t ix = 0; ix < stride; ++ix) {
            const int gx = int(ix) + kHalo;
            const int gy = int(iy) + kHalo;
            const size_t vi = size_t(iy) * stride + size_t(ix);

            auto sampleFinalH = [&](int cx, int cy) -> float {
                if (cx >= kHalo && cx < kHalo + int(stride) && cy >= kHalo && cy < kHalo + int(stride)) {
                    return outChunk->heights[size_t(cy - kHalo) * stride + size_t(cx - kHalo)];
                }
                return extBase[size_t(cy) * size_t(extW) + size_t(cx)];
            };

            const float hC = outChunk->heights[vi];
            const float hL = sampleFinalH(gx - 1, gy);
            const float hR = sampleFinalH(gx + 1, gy);
            const float hD = sampleFinalH(gx, gy - 1);
            const float hU = sampleFinalH(gx, gy + 1);

            const float dhdx = (hR - hL) * inv2Step;
            const float dhdy = (hU - hD) * inv2Step;
            const Vec3 n = Normalize({-dhdx, -dhdy, 1.0f});
            outChunk->normals[vi] = n;

            const float lap = (hL + hR + hD + hU - 4.0f * hC) * invStep2;
            outChunk->curvatures[vi] = Clamp(-lap * 140.0f, -1.0f, 1.0f);

            const float altSnow = Smoothstep(cfg.snowLineZ - 500.0f, cfg.snowLineZ + 900.0f, hC);
            const float flatGate = Smoothstep(0.35f, 0.80f, n.z);
            const float snowW = Clamp01(altSnow * flatGate);
            outChunk->snow[vi] = snowW;
            outChunk->splat0[vi] = {1.0f - snowW, snowW, 0.0f, 0.0f};
        }
    }

    // 4b. Optional geomorphological hydrology erosion & D8 watershed river carving hook
    //     (off by default so all existing deterministic terrain tests remain 100% bit-exact).
    if (g_enableHydrologyErosion.load(std::memory_order_acquire)) {
        if (const TerrainHydrologyHookFn hook = g_terrainHydrologyHook.load(std::memory_order_acquire)) {
            hook(*outChunk, cfg);
        }
    }

    // 5. Enforce exact watertight skirtless LOD edge stitching on any coarser neighbor edge.
    ApplySkirtlessLodStitch(*outChunk, neighborCoarserMask);
    return true;
}

void RegisterTerrainGenHydrologyHook(TerrainHydrologyHookFn hook, bool enabled) noexcept {
    g_terrainHydrologyHook.store(hook, std::memory_order_release);
    g_enableHydrologyErosion.store(enabled && (hook != nullptr), std::memory_order_release);
}

bool QueryTerrainGenHydrologyHookEnabled() noexcept {
    return g_enableHydrologyErosion.load(std::memory_order_acquire) &&
           (g_terrainHydrologyHook.load(std::memory_order_acquire) != nullptr);
}

}  // namespace raw::worldgen
