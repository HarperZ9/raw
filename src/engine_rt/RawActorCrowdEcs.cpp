// RawActorCrowdEcs.cpp: Wave 84 Cache-Aligned 1,024-Actor SoA Crowd ECS & 4-Tier Adaptive Scheduler
// Clean-room MIT, MSVC /W4 /WX clean.
#include "RawActorCrowdEcs.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace raw::rt
{
namespace
{
inline bool ShouldTickTier(uint8_t tier, uint64_t tickIdx) noexcept
{
    switch (tier) {
        case 0u: return true;                   // 100 Hz
        case 1u: return (tickIdx % 2ull) == 0u; // 50 Hz
        case 2u: return (tickIdx % 4ull) == 0u; // 25 Hz
        default: return (tickIdx % 20ull) == 0u;// 5 Hz
    }
}

inline float TierDtScale(uint8_t tier) noexcept
{
    switch (tier) {
        case 0u: return 1.0f;
        case 1u: return 2.0f;
        case 2u: return 4.0f;
        default: return 20.0f;
    }
}
} // namespace

uint32_t RawActorCrowdScheduler::SanitizeCrowdSoA(RawActorCrowdSoA& soa) noexcept
{
    if (soa.count > kMaxCrowdActors) { soa.count = kMaxCrowdActors; }
    uint32_t repaired = 0u;
    for (uint32_t i = 0u; i < soa.count; ++i) {
        if (!std::isfinite(soa.posX[i])) { soa.posX[i] = 0.0f; ++repaired; }
        if (!std::isfinite(soa.posY[i])) { soa.posY[i] = 0.0f; ++repaired; }
        if (!std::isfinite(soa.posZ[i])) { soa.posZ[i] = 0.0f; ++repaired; }
        if (!std::isfinite(soa.velX[i])) { soa.velX[i] = 0.0f; ++repaired; }
        if (!std::isfinite(soa.velY[i])) { soa.velY[i] = 0.0f; ++repaired; }
        if (!std::isfinite(soa.velZ[i])) { soa.velZ[i] = 0.0f; ++repaired; }
        if (!std::isfinite(soa.prefVelX[i])) { soa.prefVelX[i] = 0.0f; ++repaired; }
        if (!std::isfinite(soa.prefVelY[i])) { soa.prefVelY[i] = 0.0f; ++repaired; }
        if (!std::isfinite(soa.radius[i]) || soa.radius[i] < 0.15f || soa.radius[i] > 5.0f) {
            soa.radius[i] = 0.42f;
            ++repaired;
        }
        if (!std::isfinite(soa.health[i])) { soa.health[i] = 100.0f; ++repaired; }
        if (!std::isfinite(soa.yaw[i])) { soa.yaw[i] = 0.0f; ++repaired; }
        if (!std::isfinite(soa.animPhaseSec[i])) { soa.animPhaseSec[i] = 0.0f; ++repaired; }
    }
    return repaired;
}

CrowdTierCounts RawActorCrowdScheduler::AssignAdaptiveTiers(
    RawActorCrowdSoA& soa,
    float camX, float camY, float camZ) noexcept
{
    CrowdTierCounts out{};
    const uint32_t n = (std::min)(soa.count, kMaxCrowdActors);
    out.activeTotal = n;

    // Deterministic distance ordering so Tier 0/1/2 caps always prioritize nearest visible actors.
    struct DistIdx { float distSq; uint32_t idx; uint8_t vis; };
    std::array<DistIdx, kMaxCrowdActors> order{};
    for (uint32_t i = 0u; i < n; ++i) {
        const float dx = soa.posX[i] - camX;
        const float dy = soa.posY[i] - camY;
        const float dz = soa.posZ[i] - camZ;
        order[i] = {dx * dx + dy * dy + dz * dz, i, soa.visibilityMask[i]};
    }
    std::sort(order.begin(), order.begin() + static_cast<ptrdiff_t>(n),
              [](const DistIdx& a, const DistIdx& b) {
                  if (a.vis != b.vis) { return a.vis > b.vis; }
                  if (a.distSq != b.distSq) { return a.distSq < b.distSq; }
                  return a.idx < b.idx;
              });

    for (uint32_t k = 0u; k < n; ++k) {
        const uint32_t i = order[k].idx;
        const float d2   = order[k].distSq;
        const bool vis   = (order[k].vis != 0u);
        uint8_t assigned = 3u;
        if (vis && d2 < (22.0f * 22.0f) && out.tier0 < kCapTier0) {
            assigned = 0u;
            ++out.tier0;
        } else if (vis && d2 < (55.0f * 55.0f) && out.tier1 < kCapTier1) {
            assigned = 1u;
            ++out.tier1;
        } else if (vis && d2 < (140.0f * 140.0f) && out.tier2 < kCapTier2) {
            assigned = 2u;
            ++out.tier2;
        } else {
            assigned = 3u;
            ++out.tier3;
        }
        soa.tier[i]    = assigned;
        soa.lodStep[i] = assigned;
    }
    return out;
}

uint32_t RawActorCrowdScheduler::StepCrowdTick(
    RawActorCrowdSoA& soa,
    uint64_t tickIndex,
    float dtSec,
    raw::core::RawJobSystem* jobs)
{
    const uint32_t n = (std::min)(soa.count, kMaxCrowdActors);
    if (n == 0u || !std::isfinite(dtSec) || dtSec <= 0.0f) { return 0u; }

    std::atomic<uint32_t> stepped{0u};
    auto kernel = [&](size_t begin, size_t end) {
        uint32_t localCount = 0u;
        for (size_t i = begin; i < end; ++i) {
            const uint8_t t = soa.tier[i];
            if (!ShouldTickTier(t, tickIndex)) { continue; }
            const float effDt = dtSec * TierDtScale(t);
            // Smooth damped velocity approach toward preferred ORCA/nav velocity:
            const float alpha = (std::min)(effDt * 8.0f, 1.0f);
            soa.velX[i] += (soa.prefVelX[i] - soa.velX[i]) * alpha;
            soa.velY[i] += (soa.prefVelY[i] - soa.velY[i]) * alpha;
            soa.posX[i] += soa.velX[i] * effDt;
            soa.posY[i] += soa.velY[i] * effDt;
            soa.posZ[i] += soa.velZ[i] * effDt;
            const float spd2 = soa.velX[i] * soa.velX[i] + soa.velY[i] * soa.velY[i];
            if (spd2 > 1.0e-4f) {
                soa.yaw[i] = std::atan2(soa.velY[i], soa.velX[i]);
            }
            soa.animPhaseSec[i] = std::fmod(soa.animPhaseSec[i] + effDt, 60.0f);
            ++localCount;
        }
        stepped.fetch_add(localCount, std::memory_order_relaxed);
    };

    if (jobs && jobs->IsRunning() && n >= 128u) {
        jobs->ParallelFor(n, 64u, raw::core::JobPriority::High, kernel);
    } else {
        kernel(0u, n);
    }
    return stepped.load(std::memory_order_acquire);
}

} // namespace raw::rt
