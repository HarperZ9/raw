#pragma once
// RawCrowdNavAi.h: Wave 84 Deterministic RVO2/ORCA Local Crowd Avoidance + Off-Thread Utility/HTN Tactical AI
// Prevents actor clumping and capsule interpenetration across 64..1,024+ dense crowd flows and
// executes deterministic Utility + HTN tactical decisions across Wave 83 lock-free worker threads.
// Clean-room MIT, zero external dependencies, MSVC /W4 /WX clean.
#include "RawActorCrowdEcs.h"

#include <cstdint>
#include <vector>

namespace raw::rt
{

enum class TacticalAction : uint8_t
{
    Patrol      = 0u,
    AttackMelee = 1u,
    FlankCover  = 2u,
    RetreatHeal = 3u,
    HoldChoke   = 4u
};

struct OrcaStepParams
{
    float timeHorizonSec = 2.0f;
    float maxSpeedMps    = 4.5f;
    float cellSizeM      = 2.5f;
    bool  enableOrca     = true; // false = planted control 84-A bypass
};

struct OrcaStepReport
{
    uint32_t pairsEvaluated    = 0u;
    uint32_t overlapViolations = 0u;
    float    minClearanceM     = 1.0e6f;
};

struct TacticalActorContext
{
    float    health01          = 1.0f;
    float    stamina01         = 1.0f;
    float    distToThreatM     = 20.0f;
    uint32_t nearbyAllies      = 1u;
    uint32_t nearbyEnemies     = 1u;
    bool     hasCoverNearby    = false;
    bool     inChokepoint      = false;
};

struct TacticalDecision
{
    TacticalAction action       = TacticalAction::Patrol;
    float          utilityScore = 0.0f;
    uint8_t        htnPlanLen   = 1u;
};

// Deterministic RVO2 / ORCA reciprocal half-plane local avoidance + separation projection.
// Writes collision-free steering velocities into soa.velX/velY and returns overlap audit metrics.
OrcaStepReport SolveOrcaCrowdStep(
    RawActorCrowdSoA& soa,
    float dtSec,
    const OrcaStepParams& params = {});

// Measures pairwise capsule overlap violations (dist < r_i + r_j - 0.02 m) across active actors.
uint32_t CountCrowdInterpenetrations(const RawActorCrowdSoA& soa, float* outMinClearance = nullptr) noexcept;

// Pure deterministic Utility -> HTN tactical planner (safe on any worker thread, 0 shared mutable state).
TacticalDecision EvaluateUtilityHtnSingle(const TacticalActorContext& ctx) noexcept;

// Dispatches a batch of tactical decisions across Wave 83 lock-free workers (bit-identical to single-thread!).
void EvaluateUtilityHtnBatch(
    const std::vector<TacticalActorContext>& contexts,
    std::vector<TacticalDecision>& outDecisions,
    raw::core::RawJobSystem* jobs = nullptr);

} // namespace raw::rt
