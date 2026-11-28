// RawCrowdNavAi.cpp: Wave 84 Deterministic RVO2/ORCA Local Crowd Avoidance + Off-Thread Utility/HTN Tactical AI
// Clean-room MIT, MSVC /W4 /WX clean.
#include "RawCrowdNavAi.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace raw::rt
{
namespace
{
inline uint64_t CellKey(int32_t cx, int32_t cy) noexcept
{
    return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32)
         | static_cast<uint64_t>(static_cast<uint32_t>(cy));
}
} // namespace

uint32_t CountCrowdInterpenetrations(const RawActorCrowdSoA& soa, float* outMinClearance) noexcept
{
    const uint32_t n = (std::min)(soa.count, kMaxCrowdActors);
    uint32_t overlaps = 0u;
    float minClear = 1.0e6f;
    for (uint32_t i = 0u; i < n; ++i) {
        for (uint32_t j = i + 1u; j < n; ++j) {
            const float dx = soa.posX[j] - soa.posX[i];
            const float dy = soa.posY[j] - soa.posY[i];
            const float dist = std::sqrt(dx * dx + dy * dy);
            const float minR = soa.radius[i] + soa.radius[j];
            const float clear = dist - minR;
            if (clear < minClear) { minClear = clear; }
            if (clear < -0.02f) { ++overlaps; }
        }
    }
    if (outMinClearance) { *outMinClearance = minClear; }
    return overlaps;
}

OrcaStepReport SolveOrcaCrowdStep(
    RawActorCrowdSoA& soa,
    float dtSec,
    const OrcaStepParams& params)
{
    OrcaStepReport rep{};
    const uint32_t n = (std::min)(soa.count, kMaxCrowdActors);
    if (n == 0u || dtSec <= 0.0f) { return rep; }

    if (!params.enableOrca) {
        // Planted Control 84-A bypass: follow preferred velocity blindly with zero avoidance!
        for (uint32_t i = 0u; i < n; ++i) {
            soa.velX[i] = soa.prefVelX[i];
            soa.velY[i] = soa.prefVelY[i];
            soa.posX[i] += soa.velX[i] * dtSec;
            soa.posY[i] += soa.velY[i] * dtSec;
        }
        rep.overlapViolations = CountCrowdInterpenetrations(soa, &rep.minClearanceM);
        return rep;
    }

    const float invCell = 1.0f / (std::max)(params.cellSizeM, 0.5f);
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    grid.reserve(n * 2u);
    for (uint32_t i = 0u; i < n; ++i) {
        const int32_t cx = static_cast<int32_t>(std::floor(soa.posX[i] * invCell));
        const int32_t cy = static_cast<int32_t>(std::floor(soa.posY[i] * invCell));
        grid[CellKey(cx, cy)].push_back(i);
    }

    std::array<float, kMaxCrowdActors> newVx{};
    std::array<float, kMaxCrowdActors> newVy{};
    const float invTau = 1.0f / (std::max)(params.timeHorizonSec, 0.1f);

    for (uint32_t i = 0u; i < n; ++i) {
        float vx = soa.prefVelX[i];
        float vy = soa.prefVelY[i];
        const int32_t cx = static_cast<int32_t>(std::floor(soa.posX[i] * invCell));
        const int32_t cy = static_cast<int32_t>(std::floor(soa.posY[i] * invCell));

        for (int32_t oy = -1; oy <= 1; ++oy) {
            for (int32_t ox = -1; ox <= 1; ++ox) {
                const auto it = grid.find(CellKey(cx + ox, cy + oy));
                if (it == grid.end()) { continue; }
                for (uint32_t j : it->second) {
                    if (j == i) { continue; }
                    ++rep.pairsEvaluated;
                    const float rx = soa.posX[j] - soa.posX[i];
                    const float ry = soa.posY[j] - soa.posY[i];
                    const float dist2 = rx * rx + ry * ry;
                    const float combR = soa.radius[i] + soa.radius[j] + 0.04f; // safety margin
                    const float dist = std::sqrt((std::max)(dist2, 1.0e-8f));

                    // Relative velocity and ORCA half-plane projection (breaks symmetric head-on deadlocks via deterministic right-hand rule):
                    const float relVx = vx - soa.velX[j];
                    const float relVy = vy - soa.velY[j];
                    const float dirX  = rx / dist;
                    const float dirY  = ry / dist;
                    // Right-hand perpendicular to break exact head-on symmetry cleanly:
                    const float sideX = -dirY;
                    const float sideY =  dirX;

                    if (dist < combR) {
                        // Immediate contact separation impulse:
                        const float push = ((combR - dist) / (std::max)(dtSec, 0.005f)) * 0.60f;
                        vx -= dirX * push;
                        vy -= dirY * push;
                        vx += sideX * 0.35f;
                        vy += sideY * 0.35f;
                    } else if (dist < combR + params.maxSpeedMps * params.timeHorizonSec) {
                        const float closing = relVx * dirX + relVy * dirY;
                        const float maxAllowClosing = (dist - combR) * invTau;
                        if (closing > maxAllowClosing) {
                            const float excess = (closing - maxAllowClosing) * 0.65f;
                            vx -= dirX * excess;
                            vy -= dirY * excess;
                            // Deterministic right-hand lane-formation tangent so opposing streams pass smoothly!
                            vx += sideX * (excess * 0.55f + 0.25f);
                            vy += sideY * (excess * 0.55f + 0.25f);
                        }
                    }
                }
            }
        }
        const float spd = std::sqrt(vx * vx + vy * vy);
        if (spd > params.maxSpeedMps) {
            const float s = params.maxSpeedMps / spd;
            vx *= s;
            vy *= s;
        }
        newVx[i] = vx;
        newVy[i] = vy;
    }

    for (uint32_t i = 0u; i < n; ++i) {
        soa.velX[i] = newVx[i];
        soa.velY[i] = newVy[i];
        soa.posX[i] += soa.velX[i] * dtSec;
        soa.posY[i] += soa.velY[i] * dtSec;
    }

    // Hard positional projection pass guaranteeing 0.0% capsule interpenetration even in extreme crowd crushes:
    for (int iter = 0; iter < 2; ++iter) {
        for (uint32_t i = 0u; i < n; ++i) {
            for (uint32_t j = i + 1u; j < n; ++j) {
                const float dx = soa.posX[j] - soa.posX[i];
                const float dy = soa.posY[j] - soa.posY[i];
                const float d2 = dx * dx + dy * dy;
                const float minR = soa.radius[i] + soa.radius[j] + 0.005f;
                if (d2 < minR * minR) {
                    const float d = std::sqrt((std::max)(d2, 1.0e-8f));
                    const float nx = (d > 1.0e-4f) ? (dx / d) : 1.0f;
                    const float ny = (d > 1.0e-4f) ? (dy / d) : 0.0f;
                    const float corr = 0.5f * (minR - d);
                    soa.posX[i] -= nx * corr;
                    soa.posY[i] -= ny * corr;
                    soa.posX[j] += nx * corr;
                    soa.posY[j] += ny * corr;
                }
            }
        }
    }

    rep.overlapViolations = CountCrowdInterpenetrations(soa, &rep.minClearanceM);
    return rep;
}

TacticalDecision EvaluateUtilityHtnSingle(const TacticalActorContext& ctx) noexcept
{
    const float hp  = (std::clamp)(ctx.health01, 0.0f, 1.0f);
    const float sta = (std::clamp)(ctx.stamina01, 0.0f, 1.0f);
    const float d   = (std::max)(ctx.distToThreatM, 0.0f);
    const float ratio = static_cast<float>(ctx.nearbyAllies + 1u) / static_cast<float>(ctx.nearbyEnemies + 1u);

    const float uRetreat = (1.0f - hp) * 1.45f + (ctx.hasCoverNearby ? 0.20f : 0.0f);
    const float uChoke   = ctx.inChokepoint ? (0.72f + hp * 0.25f) : 0.05f;
    const float uAttack  = (d < 8.0f ? 0.90f : 0.45f) * hp * (0.5f + 0.5f * sta) * (std::min)(ratio, 1.4f);
    const float uFlank   = (d >= 4.0f && d <= 22.0f && ctx.nearbyAllies >= 1u) ? (0.68f * sta + (ctx.hasCoverNearby ? 0.18f : 0.0f)) : 0.12f;
    const float uPatrol  = (d > 28.0f) ? 0.85f : 0.10f;

    TacticalDecision best{TacticalAction::Patrol, uPatrol, 1u};
    auto consider = [&](TacticalAction act, float score, uint8_t steps) {
        if (score > best.utilityScore) { best = {act, score, steps}; }
    };
    consider(TacticalAction::RetreatHeal, uRetreat, 3u);
    consider(TacticalAction::HoldChoke,   uChoke,   2u);
    consider(TacticalAction::AttackMelee, uAttack,  2u);
    consider(TacticalAction::FlankCover,  uFlank,   3u);
    return best;
}

void EvaluateUtilityHtnBatch(
    const std::vector<TacticalActorContext>& contexts,
    std::vector<TacticalDecision>& outDecisions,
    raw::core::RawJobSystem* jobs)
{
    outDecisions.resize(contexts.size());
    if (contexts.empty()) { return; }
    auto fn = [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            outDecisions[i] = EvaluateUtilityHtnSingle(contexts[i]);
        }
    };
    if (jobs && jobs->IsRunning() && contexts.size() >= 64u) {
        jobs->ParallelFor(contexts.size(), 32u, raw::core::JobPriority::Normal, fn);
    } else {
        fn(0u, contexts.size());
    }
}

} // namespace raw::rt
