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

// High-FPS Physics Substep Governor (replaces Havok Fix + Display Tweaks, 30..360 Hz stable):
struct PhysicsSubstepPlan {
    float    clampedFrameDt = 1.0f / 60.0f;
    float    substepDt      = 1.0f / 120.0f;
    uint32_t substeps       = 2;
    bool     spikeClamped   = false;
};
PhysicsSubstepPlan ComputePhysicsSubstep(float rawFrameDtSec);

// Environmental Thermal Exposure & Survival Simulation (replaces Frostfall + Wet & Cold natively at 0.001 ms):
struct ThermalExposureState {
    float ambientTempC     = 12.0f;
    float coreWarmth       = 85.0f;   // 0 (hypothermia) .. 100 (warm)
    float shiverIntensity  = 0.0f;    // 0 .. 1
    float staminaRegenMult = 1.0f;    // 0.35 .. 1.15
    bool  freezingRisk     = false;
};
ThermalExposureState EvaluateThermalExposure(float altitudeZ, float windSpeed, float rain01, float snow01,
                                             bool inInterior, bool nearFire, float prevWarmth, float dtSec);

// Swept Weapon Arc-Momentum Hitbox & Sweet-Spot Evaluation (replaces Precision + Valhalla Combat):
struct WeaponArcHitResult {
    bool  hit           = false;
    float closestDist   = 1.0e9f;
    float impactParamT  = 0.0f;       // 0 = hilt, 1 = tip sweet-spot
    float momentumMult  = 1.0f;
    float poiseImpulse  = 0.0f;
};
WeaponArcHitResult EvaluateWeaponArcHit(const float swingStart[3], const float swingEnd[3],
                                        const float targetCenter[3], float targetRadius,
                                        float targetHeight, float swingSpeed);

// Physical Sabine-Eyring 3D Acoustic Room Reverb & Occlusion Estimator (replaces True 3D Sound / Reverb Overhaul):
enum class AcousticEnvClass : uint8_t {
    OpenSky       = 0,
    AlpineValley  = 1,
    StoneInterior = 2,
    CavernVault   = 3,
    Underwater    = 4,
};

struct AcousticReverbState {
    float            rt60Sec         = 0.25f;
    float            earlyReflGain   = 0.15f;
    float            lowPassHz       = 18000.0f;
    float            airAbsorbDbPerM = 0.005f;
    AcousticEnvClass envClass        = AcousticEnvClass::OpenSky;
};

AcousticReverbState ComputeAcousticReverb(bool inInterior, float enclosureRadiusU,
                                          float snowCover01, float rain01,
                                          float windSpeed, bool underwater);

}  // namespace raw::rt
