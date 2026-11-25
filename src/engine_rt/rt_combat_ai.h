#pragma once
// rt_combat_ai.h: Tactical Squad Combat AI, Modern Action Combat & Living World Ecosystem Simulation (Zero SKSE).
// Features:
//   - Squad role coordinator (Vanguard / FlankerLeft / FlankerRight / Skirmisher / Support) eliminating 2011 conga lines
//   - Precision timed parry + posture/poise stagger + stamina-gated i-frame dodge + melee impact hit-stop
//   - Weather/time-of-day living world ecosystem directives (shelter seeking, hearth warming, dusk torches)
// Original work, MIT.

#include <cstdint>
#include <vector>

namespace raw::rt {

enum class TacticalRole : uint8_t {
    Vanguard         = 0,
    FlankerLeft      = 1,
    FlankerRight     = 2,
    SkirmisherRanged = 3,
    SupportCaster    = 4,
};

struct CombatAgentSnapshot {
    uint32_t actorId = 0;
    float    pos[3]  = {};
    float    hpFrac  = 1.0f;
    bool     isRanged = false;
    bool     isCaster = false;
};

struct TacticalOrder {
    uint32_t     actorId = 0;
    TacticalRole role = TacticalRole::Vanguard;
    float        desiredPos[3] = {};
    float        aggression = 0.5f;   // 0..1 pacing token (prevents 5 simultaneous attacks)
    bool         attackToken = false;
};

struct CombatActorState {
    float poiseCurrent     = 100.0f;
    float poiseMax         = 100.0f;
    float poiseRegenDelay  = 0.0f;
    float stamina          = 100.0f;
    float staminaMax       = 100.0f;
    float blockElapsedSec  = 0.0f;
    float dodgeIFrameSec   = 0.0f;
    float dodgeCooldownSec = 0.0f;
    float staggerSec       = 0.0f;
    float hitStopSec       = 0.0f;
    bool  blockHeld        = false;
};

struct IncomingHit {
    float hpDamage      = 0.0f;
    float poiseDamage   = 0.0f;
    bool  isHeavy       = false;
    bool  isUnblockable = false;
};

struct HitResolution {
    bool  perfectParry    = false;
    bool  dodgedIFrame    = false;
    bool  blocked         = false;
    bool  defenderStagger = false;
    bool  attackerStagger = false;
    float appliedHpDamage = 0.0f;
    float appliedStamina  = 0.0f;
    float hitStopMs       = 0.0f;
};

enum class WorldNpcDirective : uint8_t {
    NormalRoutine = 0,
    SeekShelter   = 1,
    WarmAtBrazier = 2,
    EquipTorch    = 3,
    AlertPatrol   = 4,
};

// Assigns non-overlapping tactical squad roles and flanking anchor positions around targetPos.
std::vector<TacticalOrder> CoordinateSquadTactics(const float targetPos[3],
                                                  const float targetForward[2],
                                                  const std::vector<CombatAgentSnapshot>& agents,
                                                  uint32_t maxSimultaneousAttackers = 2);

// Advances defender combat timers (block window, dodge i-frames, poise regen, stagger, hit-stop).
void StepCombatTimers(CombatActorState& st, bool blockHeldNow, bool blockEdgeNow, bool dodgeEdgeNow, float dtSec);

// Resolves an incoming strike against defender state with precision timed parry, i-frame dodge, and poise break.
HitResolution ResolveCombatHit(CombatActorState& defender, CombatActorState& attacker,
                               const IncomingHit& hit, float parryWindowMs, float hitStopBaseMs);

// Evaluates weather, volumetric snow depth, and 24-hour clock to determine living-world NPC behavior.
WorldNpcDirective EvaluateWorldEcosystem(float gameHour, float rain01, float snow01, float snowDepthU,
                                         bool nearFire, bool inCombat);

// Mean-Payoff / Condon Simple Stochastic Game Tactical Policy Solver:
// Computes optimal action over 4 combat states via exact Bellman value iteration (converges monotonically in <= 24 steps).
enum class TacticalAction : uint8_t {
    PressAttack    = 0,
    FlankCircle    = 1,
    GuardParry     = 2,
    RetreatRecover = 3,
};

struct TacticalGameState {
    float hpFrac         = 1.0f;
    float staminaFrac    = 1.0f;
    float poiseFrac      = 1.0f;
    float distUnits      = 140.0f;
    bool  hasAttackToken = true;
    bool  enemyAttacking = false;
};

struct TacticalPolicyResult {
    TacticalAction bestAction = TacticalAction::FlankCircle;
    float          actionValues[4] = {};
    float          expectedPayoff  = 0.0f;
    uint32_t       iterations      = 0;
};

TacticalPolicyResult SolveTacticalStochasticGame(const TacticalGameState& st);

// Exact CPU reference of the O(1) 8D E8 optimal sphere-packing lattice quantizer (matches RawTensorMath.hlsli::rawQuantizeE8).
void QuantizeE8Lattice(const float in8[8], float scale, float out8[8]);

}  // namespace raw::rt
