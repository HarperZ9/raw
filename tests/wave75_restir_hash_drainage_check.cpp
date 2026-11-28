// wave75_restir_hash_drainage_check.cpp: Standalone C++23 /W4 /WX verification for Wave 75
// (Disocclusion-Safe 2-Slot ReSTIR-GI/PT Reservoir Splatting with Shift-Mapping Jacobian & MIS,
// Online Multi-Resolution L=8 World-Space Hash Radiance Cache, Heightfield-Coupled World-Space
// Puddle Drainage with 0.0 Screen-Edge Residual, and 4 Planted-Failure Negative Controls).
// Original work, MIT.

#include <cmath>
#include <cstdio>
#include <limits>

#include "../src/d3d12_bridge/RawRestirHashCache.h"

namespace {

int g_pass = 0;
int g_fail = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        ++g_pass;
        std::printf("  [PASS] %s\n", name);
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s\n", name);
    }
}

}  // namespace

int main() {
    using namespace raw::bridge;

    std::printf("=== RAW Wave 75: ReSTIR-GI 2-Slot, Hash Radiance Cache & World Drainage Check ===\n");

    // 1. Constant-radiance diffuse white-furnace unbiasedness (rho = 0.8, L_env = 1.0)
    const FurnaceCheckResult furnaceOk = RunConstantRadianceFurnaceCheck(0.80, false);
    Check(furnaceOk.unbiasedPass && furnaceOk.absError < 0.015,
          "Constant-radiance ReSTIR-GI furnace with shift-mapping Jacobian is unbiased (|mean - 0.8| < 0.015)");

    // 2. Planted Failure Control A: drop shift-mapping Jacobian |J_T| -> > 15% luminance bias MUST fail!
    const FurnaceCheckResult furnaceBad = RunConstantRadianceFurnaceCheck(0.80, true);
    Check(!furnaceBad.unbiasedPass && furnaceBad.relError > 0.15,
          "Planted control A: missing shift-mapping Jacobian |J_T| shifts furnace >15% and fails gate");

    // 3. Disocclusion-safe 2-slot temporal reservoir selection recovers background layer cleanly
    {
        TwoSlotReservoirEntry fgOccluder{};
        fgOccluder.depth = 120.0;
        fgOccluder.normal = {0.0, 0.0, 1.0};
        fgOccluder.valid = true;

        TwoSlotReservoirEntry bgWall{};
        bgWall.depth = 482.0;
        bgWall.normal = {0.0, 1.0, 0.0};
        bgWall.valid = true;

        const int picked = SelectTwoSlotReservoirLayer(480.0, {0.0, 0.99, 0.14}, fgOccluder, bgWall);
        const int rejected = SelectTwoSlotReservoirLayer(480.0, {1.0, 0.0, 0.0}, fgOccluder, bgWall);
        Check(picked == 1 && rejected == -1,
              "2-slot reservoir ring selects matching background layer on disocclusion & rejects mismatched normal");
    }

    // 4. Online Multi-Resolution L=8 World-Space Hash Radiance Cache convergence, eviction & NaN resilience
    {
        OnlineMultiResHashCache cache(2048u);
        const Vec3d pos{1420.5, -880.2, 112.0};
        const Vec3d nrm{0.0, 0.0, 1.0};
        const Vec3d target{0.62, 0.48, 0.31};
        for (uint32_t f = 1u; f <= 16u; ++f) {
            cache.Update(pos, nrm, 2u, target, f);
        }
        Vec3d outRad{};
        const bool hit = cache.Query(pos, nrm, 2u, outRad);
        const double err = std::abs(outRad.x - target.x) + std::abs(outRad.y - target.y) + std::abs(outRad.z - target.z);
        Check(hit && err < 1.0e-3,
              "Multi-resolution L=8 hash radiance cache converges cleanly under online relative-L2 EMA");

        // Deterministic eviction after maxAge frames
        const uint32_t evCount = cache.EvictStale(100u, 32u);
        Vec3d afterEvict{};
        const bool hitAfter = cache.Query(pos, nrm, 2u, afterEvict);
        Check(evCount >= 1u && !hitAfter,
              "Hash radiance cache deterministically evicts stale entries via tombstones");

        // Planted Failure Control B: extreme NaN/Inf radiance injection MUST sanitize cleanly (0 poison!)
        const double qnan = std::numeric_limits<double>::quiet_NaN();
        const double pinf = std::numeric_limits<double>::infinity();
        cache.Update({50.0, 50.0, 10.0}, {0.0, 0.0, 1.0}, 0u, {qnan, pinf, -pinf}, 101u);
        Vec3d sanRad{};
        const bool hitSan = cache.Query({50.0, 50.0, 10.0}, {0.0, 0.0, 1.0}, 0u, sanRad);
        Check(hitSan && std::isfinite(sanRad.x) && std::isfinite(sanRad.y) && std::isfinite(sanRad.z) && sanRad.y <= 32.0,
              "Planted control B: NaN/Inf hash radiance cache injection sanitized (0 NaN/Inf stored)");
    }

    // 5. Screen-Edge Wetness / Puddle Invariance Test (Wave 75 Section 1.6 #2):
    // Find a world-space concave depression (laplacianH > 0) and verify 0.0 diff from screen center to 2px border!
    {
        double bestX = 0.0, bestY = 0.0, bestLap = -1.0;
        for (int ix = -20; ix <= 20; ++ix) {
            for (int iy = -20; iy <= 20; ++iy) {
                const double x = static_cast<double>(ix) * 35.0;
                const double y = static_cast<double>(iy) * 35.0;
                const WorldDrainagePuddleSample s = EvalWorldHeightfieldDrainagePuddle(x, y, 0.99, 1.0, 0.45, 0.55, 0.50, 0.0);
                if (s.laplacianH > bestLap && s.puddleDepth > 0.25) {
                    bestLap = s.laplacianH;
                    bestX = x;
                    bestY = y;
                }
            }
        }
        const WorldDrainagePuddleSample pCenter = EvalWorldHeightfieldDrainagePuddle(bestX, bestY, 0.99, 1.0, 0.45, 0.55, 0.50, 0.0);
        const WorldDrainagePuddleSample pEdge2px = EvalWorldHeightfieldDrainagePuddle(bestX, bestY, 0.99, 1.0, 0.45, 0.55, 2.0 / 1920.0, 0.0);
        Check(pCenter.allFinite && pEdge2px.allFinite && pCenter.laplacianH > 0.0 &&
                  pCenter.puddleDepth > 0.25 && std::abs(pCenter.puddleDepth - pEdge2px.puddleDepth) == 0.0,
              "Screen-edge puddle invariance: concave world depression has exact 0.0 diff at 2px border");

        // Planted Failure Control C: screen-space border vignette hack MUST be caught!
        const WorldDrainagePuddleSample pHacked = EvalWorldHeightfieldDrainagePuddle(bestX, bestY, 0.99, 1.0, 0.45, 0.55, 2.0 / 1920.0, 0.85);
        Check(std::abs(pCenter.puddleDepth - pHacked.puddleDepth) > 0.20,
              "Planted control C: screen-space border wetness falloff hack caught (>0.20 diff)");

        // Planted Failure Control D: convex ridge (laplacianH < 0) receives zero drainage pooling boost!
        double ridgeX = 0.0, ridgeY = 0.0, minLap = 1.0e9;
        for (int ix = -20; ix <= 20; ++ix) {
            for (int iy = -20; iy <= 20; ++iy) {
                const double x = static_cast<double>(ix) * 35.0;
                const double y = static_cast<double>(iy) * 35.0;
                const WorldDrainagePuddleSample s = EvalWorldHeightfieldDrainagePuddle(x, y, 0.99, 1.0, 0.45, 0.55, 0.50, 0.0);
                if (s.laplacianH < minLap) {
                    minLap = s.laplacianH;
                    ridgeX = x;
                    ridgeY = y;
                }
            }
        }
        const WorldDrainagePuddleSample pRidge = EvalWorldHeightfieldDrainagePuddle(ridgeX, ridgeY, 0.99, 1.0, 0.45, 0.55, 0.50, 0.0);
        Check(pRidge.laplacianH < 0.0 && pRidge.drainageMask == 0.0 && pRidge.puddleDepth < pCenter.puddleDepth,
              "Planted control D: convex terrain ridge (laplacianH < 0) has zero drainage pooling boost");
    }

    std::printf("=== Wave 75 Result: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
