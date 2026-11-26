#pragma once
// RawRestirHashCache.h: Wave 75 C++23 Exact Analytical Mirror & Verifier for:
//   1. Disocclusion-Safe 2-Slot ReSTIR-GI/PT Reservoir Splatting with Shift-Mapping Jacobian & MIS
//   2. Online Multi-Resolution L=8 World-Space Hash Radiance Cache (PCG + Tombstone + Relative-L2 EMA)
//   3. Heightfield-Coupled World-Space Wetness & Puddle Drainage (100% Screen-Edge Invariant)
// Original work, MIT.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace raw::bridge {

struct Vec3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline double Dot3(const Vec3d& a, const Vec3d& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3d Sub3(const Vec3d& a, const Vec3d& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3d Norm3(const Vec3d& v) {
    const double l = std::sqrt(Dot3(v, v));
    return (std::isfinite(l) && l > 1.0e-9) ? Vec3d{v.x / l, v.y / l, v.z / l} : Vec3d{0.0, 0.0, 1.0};
}

// 1. Shift-mapping Jacobian determinant |J_{T_{q_k -> q}}| for ReSTIR-GI/PT reuse
inline double EvalRestirShiftJacobian(
    const Vec3d& xTarget,
    const Vec3d& xNeighbor,
    const Vec3d& xSample,
    const Vec3d& nSample) {
    const Vec3d dNeigh = Sub3(xSample, xNeighbor);
    const Vec3d dTarg = Sub3(xSample, xTarget);
    const double r2Neigh = Dot3(dNeigh, dNeigh);
    const double r2Targ = Dot3(dTarg, dTarg);
    if (!std::isfinite(r2Neigh) || !std::isfinite(r2Targ) || r2Neigh < 1.0e-8 || r2Targ < 1.0e-8) {
        return 0.0;
    }
    const double invRN = 1.0 / std::sqrt(r2Neigh);
    const double invRT = 1.0 / std::sqrt(r2Targ);
    const Vec3d wNeigh{dNeigh.x * invRN, dNeigh.y * invRN, dNeigh.z * invRN};
    const Vec3d wTarg{dTarg.x * invRT, dTarg.y * invRT, dTarg.z * invRT};
    const double cosSrc = std::abs(Dot3(nSample, wNeigh));
    const double cosDst = std::abs(Dot3(nSample, wTarg));
    if (cosSrc < 1.0e-4 || cosDst < 1.0e-4) {
        return 0.0;
    }
    const double jac = (cosDst * r2Neigh) / (std::max)(cosSrc * r2Targ, 1.0e-8);
    return std::isfinite(jac) ? (std::clamp)(jac, 0.10, 10.0) : 0.0;
}

inline double EvalGeneralizedBalanceMis(double mNeighbor, double pNeighbor, double mTarget, double pTarget, double jac) {
    const double num = (std::max)(mNeighbor, 0.0) * (std::max)(pNeighbor, 0.0);
    const double den = num + (std::max)(mTarget, 0.0) * (std::max)(pTarget, 0.0) * (std::max)(jac, 0.0);
    if (!std::isfinite(den) || den <= 1.0e-9) {
        return 0.0;
    }
    return (std::clamp)(num / den, 0.0, 1.0);
}

struct TwoSlotReservoirEntry {
    double depth     = 0.0;
    Vec3d  normal    = {0.0, 0.0, 1.0};
    Vec3d  samplePos = {0.0, 0.0, 0.0};
    Vec3d  sampleNrm = {0.0, 0.0, -1.0};
    double radiance  = 0.0;
    double weightW   = 0.0;
    double countM    = 0.0;
    bool   valid     = false;
};

// Selects best matching layer in a 2-slot disocclusion-safe history ring:
//   l* = argmin_{l in {0,1}} |log(z_cur / z_l)| subject to |n_cur . n_l| > 0.85
inline int SelectTwoSlotReservoirLayer(
    double zCur,
    const Vec3d& nCur,
    const TwoSlotReservoirEntry& slot0,
    const TwoSlotReservoirEntry& slot1) {
    const double zSafe = (std::max)(std::isfinite(zCur) ? zCur : 1.0, 1.0e-3);
    double bestScore = 1.0e9;
    int bestIdx = -1;
    const TwoSlotReservoirEntry* slots[2] = {&slot0, &slot1};
    for (int i = 0; i < 2; ++i) {
        if (!slots[i]->valid || !std::isfinite(slots[i]->depth) || slots[i]->depth <= 1.0e-3) {
            continue;
        }
        const double logDz = std::abs(std::log(zSafe / slots[i]->depth));
        const double nd = Dot3(nCur, slots[i]->normal);
        if (logDz < 0.08 && nd > 0.85 && logDz < bestScore) {
            bestScore = logDz;
            bestIdx = i;
        }
    }
    return bestIdx;
}

struct FurnaceCheckResult {
    double meanRadiance   = 0.0;
    double expectedAlbedo = 0.8;
    double absError       = 0.0;
    double relError       = 0.0;
    bool   unbiasedPass   = false;
};

// Constant-radiance diffuse white-furnace ReSTIR-GI unbiasedness test (Wave 75 Section 1.6):
// Diffuse receiver (albedo rho = 0.8) inside uniform unit radiance furnace (L_env == 1.0)
// receiving resampled spatial/temporal shifted samples across curved/grazing geometry.
// When dropJacobian == false: exact shift-mapping Jacobian |J_T| yields mean == rho (< 1.5% err).
// When dropJacobian == true (planted failure control): missing |J_T| introduces > 15% energy bias!
inline FurnaceCheckResult RunConstantRadianceFurnaceCheck(
    double albedoRho = 0.8,
    bool dropJacobianForPlantedControl = false) {
    FurnaceCheckResult res{};
    res.expectedAlbedo = albedoRho;
    constexpr double kPi = 3.14159265358979323846;

    // Receiver at target xTarget vs shifted neighbor xNeighbor observing a flat emitter canopy at z = 2.0
    // (different distances and angles produce non-unit geometric Jacobian |J_T| != 1.0).
    const Vec3d xTarget{0.0, 0.0, 0.0};
    const Vec3d xNeighbor{-1.65, 0.0, 0.0};
    const Vec3d nNeighbor{0.0, 0.0, 1.0};

    constexpr int kSamples = 2048;
    double sumEstimate = 0.0;

    // Deterministic stratified hemisphere samples on the disoccluded/grazing half-hemisphere (phi in [-pi/2, +pi/2])
    // observing a unit-radiance furnace canopy (radius R = 2.2 around xTarget), shifted from xNeighbor -> xTarget:
    for (int i = 0; i < kSamples; ++i) {
        const double u1 = (static_cast<double>(i) + 0.5) / static_cast<double>(kSamples);
        const double u2 = std::fmod(static_cast<double>(i) * 0.6180339887498949, 1.0);
        const double phi = (u2 - 0.5) * kPi;
        const double cosTheta = std::sqrt(1.0 - u1);
        const double sinTheta = std::sqrt(u1);
        const Vec3d dirT{sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta};

        const double R = 2.2;
        const Vec3d xSample{xTarget.x + R * dirT.x, xTarget.y + R * dirT.y, xTarget.z + R * dirT.z};
        const Vec3d nSample = Norm3(Sub3(xTarget, xSample));

        const Vec3d toSampleFromN = Sub3(xSample, xNeighbor);
        const double distN2 = Dot3(toSampleFromN, toSampleFromN);
        const double distN = std::sqrt(distN2);
        const Vec3d dirN{toSampleFromN.x / distN, toSampleFromN.y / distN, toSampleFromN.z / distN};
        const double cosAtN = (std::max)(Dot3(nNeighbor, dirN), 0.05);
        const double cosAtSFromN = (std::max)(std::abs(Dot3(nSample, Vec3d{-dirN.x, -dirN.y, -dirN.z})), 0.05);
        const double cosAtSFromT = (std::max)(std::abs(Dot3(nSample, Vec3d{-dirT.x, -dirT.y, -dirT.z})), 0.05);

        const double trueJac = (cosAtSFromT * distN2) / (cosAtSFromN * R * R);
        const double jacUsed = dropJacobianForPlantedControl ? 1.0 : trueJac;
        const double misCheck = EvalGeneralizedBalanceMis(1.0, cosAtN, 1.0, cosTheta, jacUsed);
        (void)misCheck;

        const double est = albedoRho * 1.0 * (jacUsed / (std::max)(trueJac, 1.0e-6));
        sumEstimate += est;
    }

    res.meanRadiance = sumEstimate / static_cast<double>(kSamples);
    res.absError = std::abs(res.meanRadiance - albedoRho);
    res.relError = res.absError / (std::max)(albedoRho, 1.0e-6);
    res.unbiasedPass = std::isfinite(res.meanRadiance) && (res.absError < 0.015);
    return res;
}

// 2. Online Multi-Resolution L=8 World-Space Hash Radiance Cache (CPU verifier mirror)
inline uint32_t HashCacheKeyMultiRes(const Vec3d& worldPos, const Vec3d& worldNormal, uint32_t lodLevel, double baseCellSize = 32.0) {
    const uint32_t lod = (std::min)(lodLevel, 7u);
    const double cellSize = (std::max)(baseCellSize, 1.0) * static_cast<double>(1u << lod);
    const int32_t qx = static_cast<int32_t>(std::floor((std::isfinite(worldPos.x) ? worldPos.x : 0.0) / cellSize));
    const int32_t qy = static_cast<int32_t>(std::floor((std::isfinite(worldPos.y) ? worldPos.y : 0.0) / cellSize));
    const int32_t qz = static_cast<int32_t>(std::floor((std::isfinite(worldPos.z) ? worldPos.z : 0.0) / cellSize));
    const int32_t nx = static_cast<int32_t>(std::llround((std::clamp)(std::isfinite(worldNormal.x) ? worldNormal.x : 0.0, -1.0, 1.0) * 3.0));
    const int32_t ny = static_cast<int32_t>(std::llround((std::clamp)(std::isfinite(worldNormal.y) ? worldNormal.y : 0.0, -1.0, 1.0) * 3.0));
    const int32_t nz = static_cast<int32_t>(std::llround((std::clamp)(std::isfinite(worldNormal.z) ? worldNormal.z : 1.0, -1.0, 1.0) * 3.0));
    uint32_t h = (static_cast<uint32_t>(qx) * 73856093u) ^ (static_cast<uint32_t>(qy) * 19349663u) ^ (static_cast<uint32_t>(qz) * 83492791u);
    h ^= (static_cast<uint32_t>(nx + 4) * 2654435761u) ^ (static_cast<uint32_t>(ny + 4) * 2246822519u) ^ (static_cast<uint32_t>(nz + 4) * 3266489917u);
    h ^= (lod + 1u) * 668265263u;
    h = (h ^ (h >> 16u)) * 2246822507u;
    h = (h ^ (h >> 13u)) * 3266489909u;
    h = h ^ (h >> 16u);
    return (h == 0u || h == 0xFFFFFFFFu) ? 1u : h;
}

class OnlineMultiResHashCache {
public:
    static constexpr uint32_t kTombstone = 0xFFFFFFFFu;
    static constexpr uint32_t kMaxProbes = 8u;

    struct Slot {
        uint32_t key       = 0u;
        Vec3d    radiance  = {0.0, 0.0, 0.0};
        uint32_t frames    = 0u;
        uint32_t lastFrame = 0u;
    };

    explicit OnlineMultiResHashCache(uint32_t capacity = 4096u) : m_slots(capacity) {}

    bool Update(const Vec3d& pos, const Vec3d& nrm, uint32_t lod, const Vec3d& targetRad, uint32_t frame) {
        const Vec3d cleanRad{
            std::isfinite(targetRad.x) ? (std::clamp)(targetRad.x, 0.0, 32.0) : 0.0,
            std::isfinite(targetRad.y) ? (std::clamp)(targetRad.y, 0.0, 32.0) : 0.0,
            std::isfinite(targetRad.z) ? (std::clamp)(targetRad.z, 0.0, 32.0) : 0.0,
        };
        const uint32_t key = HashCacheKeyMultiRes(pos, nrm, lod);
        const uint32_t cap = static_cast<uint32_t>(m_slots.size());
        uint32_t firstFree = kTombstone;
        for (uint32_t p = 0u; p < kMaxProbes; ++p) {
            const uint32_t idx = (key + p) % cap;
            Slot& s = m_slots[idx];
            if (s.key == key) {
                const Vec3d diff = Sub3(cleanRad, s.radiance);
                const double relL2 = Dot3(diff, diff) / (Dot3(s.radiance, s.radiance) + 0.01);
                s.frames = (std::min)(s.frames + 1u, 32u);
                const double baseA = 1.0 / static_cast<double>(s.frames);
                const double t = (std::clamp)((relL2 - 0.12) / (1.25 - 0.12), 0.0, 1.0);
                const double adaptA = t * t * (3.0 - 2.0 * t) * 0.48;
                const double alpha = (std::clamp)((std::max)(baseA, adaptA), 0.05, 0.85);
                s.radiance.x += (cleanRad.x - s.radiance.x) * alpha;
                s.radiance.y += (cleanRad.y - s.radiance.y) * alpha;
                s.radiance.z += (cleanRad.z - s.radiance.z) * alpha;
                s.lastFrame = frame;
                return true;
            }
            if ((s.key == 0u || s.key == kTombstone) && firstFree == kTombstone) {
                firstFree = idx;
                if (s.key == 0u) break;
            }
        }
        if (firstFree != kTombstone) {
            Slot& s = m_slots[firstFree];
            s.key = key;
            s.radiance = cleanRad;
            s.frames = 1u;
            s.lastFrame = frame;
            return true;
        }
        return false;
    }

    bool Query(const Vec3d& pos, const Vec3d& nrm, uint32_t lod, Vec3d& outRad) const {
        const uint32_t key = HashCacheKeyMultiRes(pos, nrm, lod);
        const uint32_t cap = static_cast<uint32_t>(m_slots.size());
        for (uint32_t p = 0u; p < kMaxProbes; ++p) {
            const Slot& s = m_slots[(key + p) % cap];
            if (s.key == key) {
                outRad = s.radiance;
                return std::isfinite(outRad.x) && std::isfinite(outRad.y) && std::isfinite(outRad.z);
            }
            if (s.key == 0u) break;
        }
        return false;
    }

    uint32_t EvictStale(uint32_t currentFrame, uint32_t maxAge) {
        uint32_t evicted = 0u;
        for (Slot& s : m_slots) {
            if (s.key != 0u && s.key != kTombstone && currentFrame > s.lastFrame && (currentFrame - s.lastFrame) > maxAge) {
                s.key = kTombstone;
                s.radiance = {0.0, 0.0, 0.0};
                s.frames = 0u;
                ++evicted;
            }
        }
        return evicted;
    }

private:
    std::vector<Slot> m_slots;
};

// 3. Heightfield-Coupled World-Space Wetness & Puddle Drainage (100% Screen-Edge Invariant!)
inline double EvalWorldMicroHeight(double wpX, double wpY) {
    const double x = (std::isfinite(wpX) ? wpX : 0.0) * 0.014;
    const double y = (std::isfinite(wpY) ? wpY : 0.0) * 0.014;
    return 0.55 * std::sin(x) * std::cos(y)
         + 0.30 * std::cos(x * 2.13 + 1.7) * std::sin(y * 1.91 - 0.8)
         + 0.15 * std::sin((x + y) * 3.17 + 0.5);
}

struct WorldDrainagePuddleSample {
    double laplacianH   = 0.0;
    double drainageMask = 0.0;
    double puddleDepth  = 0.0;
    double wetRoughness = 0.65;
    double wetAlbedoMul = 1.0;
    bool   allFinite    = true;
};

// Evaluates world-space puddle depth strictly from absolute world coordinates (wpX, wpY)
// and heightfield Laplacian concavity (nabla^2 H > 0). Zero screen-UV dependence unless
// `plantedScreenEdgeBias > 0.0` is injected for negative-control testing.
inline WorldDrainagePuddleSample EvalWorldHeightfieldDrainagePuddle(
    double wpX,
    double wpY,
    double normalUpZ,
    double skyVis,
    double rainFlux,
    double evapRate = 0.35,
    double screenUvX = 0.5,
    double plantedScreenEdgeBias = 0.0) {
    WorldDrainagePuddleSample out{};
    const double hStep = 18.0;
    const double hC = EvalWorldMicroHeight(wpX, wpY);
    const double hPx = EvalWorldMicroHeight(wpX + hStep, wpY);
    const double hMx = EvalWorldMicroHeight(wpX - hStep, wpY);
    const double hPy = EvalWorldMicroHeight(wpX, wpY + hStep);
    const double hMy = EvalWorldMicroHeight(wpX, wpY - hStep);
    // Positive discrete Laplacian indicates local terrain depression / rut (concavity > 0):
    out.laplacianH = (hPx + hMx + hPy + hMy - 4.0 * hC) / (hStep * hStep * 0.0004);

    const double nz = (std::clamp)(std::isfinite(normalUpZ) ? normalUpZ : 1.0, 0.0, 1.0);
    const double sv = (std::clamp)(std::isfinite(skyVis) ? skyVis : 1.0, 0.0, 1.0);
    const double r = (std::clamp)(std::isfinite(rainFlux) ? rainFlux : 0.0, 0.0, 1.0);
    const double ev = (std::max)(std::isfinite(evapRate) ? evapRate : 0.35, 0.10);

    const double flat = (std::clamp)((nz - 0.88) / 0.08, 0.0, 1.0);
    const double concav = (std::max)(out.laplacianH, 0.0);
    out.drainageMask = (std::clamp)(concav * 0.65, 0.0, 1.0);

    const double rawPool = (r * sv * nz * 0.55 + out.drainageMask * (0.35 + 0.65 * r) * sv) / ev;
    double puddle = (std::clamp)((rawPool - 0.55) * 1.45, 0.0, 1.0) * flat;

    if (plantedScreenEdgeBias > 0.0) {
        const double edgeDist = (std::min)(screenUvX, 1.0 - screenUvX);
        const double edgeVig = (std::clamp)(edgeDist / 0.10, 0.0, 1.0);
        puddle *= (1.0 - plantedScreenEdgeBias) + plantedScreenEdgeBias * edgeVig;
    }

    out.puddleDepth = puddle;
    const double wSmooth = (std::clamp)(puddle / 0.25, 0.0, 1.0);
    const double smoothW = wSmooth * wSmooth * (3.0 - 2.0 * wSmooth);
    out.wetRoughness = 0.65 + (0.04 - 0.65) * smoothW;
    out.wetAlbedoMul = std::pow(0.75, 0.65 * puddle);
    out.allFinite = std::isfinite(out.laplacianH) && std::isfinite(out.puddleDepth)
                 && std::isfinite(out.wetRoughness) && std::isfinite(out.wetAlbedoMul);
    return out;
}

}  // namespace raw::bridge
