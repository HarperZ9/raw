#pragma once
// RawCombatSandboxEngine.h: Clean-Room First-Principles RPG Combat, Stamina/Poise, 220ms Timed-Parry,
// Weather Elemental Synergy, Ballistic Raycast & Tactical Squad Encounter Engine (`raw::worldgen`).
// Pure portable C++23 with zero Windows, D3D12, or proprietary SDK dependencies.
// Couples:
//   - `RawSkeletonAnimEngine` (24-bone combat swing phases & live weapon-hand reach)
//   - `RawNavMeshAi`          (flanking, ranged standoff, morale retreat & separation tactics)
//   - `RawWorldPhysics`       (LOS checks, swept ballistic arrow obstacle raycasts & terrain grounding).
// Original work, MIT.

#include "RawNavMeshAi.h"
#include "RawSkeletonAnimEngine.h"
#include "RawWorldPhysics.h"

#include <cstdint>
#include <vector>

namespace raw::worldgen {

inline constexpr float kTimedParryWindowSec = 0.220f; // 220 ms directional timed-parry window

enum class CombatFaction : uint8_t {
    PlayerAllies = 0,
    Hostiles     = 1,
};

enum class CombatRole : uint8_t {
    MeleeWarrior = 0,
    ShieldBearer = 1,
    Archer       = 2,
    Battlemage   = 3,
};

enum class CombatActionState : uint8_t {
    Idle        = 0,
    Windup      = 1,
    ActiveSwing = 2,
    Recovery    = 3,
    Blocking    = 4,
    Staggered   = 5,
    BowDrawing  = 6,
    Casting     = 7,
    Dead        = 8,
};

enum class DamageElement : uint8_t {
    Physical = 0,
    Fire     = 1,
    Frost    = 2,
    Shock    = 3,
};

struct CombatWeatherInput {
    float wetness01    = 0.0f;  // [0..1] rain or water immersion
    float ambientTempC = 12.0f; // Sub-zero cold reduces stamina regeneration
    float windSpeedMps = 2.0f;  // Crosswind drift on arrows
    Vec2  windDirXY{1.0f, 0.0f};
};

struct CombatActor {
    uint32_t          id                = 0u;
    CombatFaction     faction           = CombatFaction::PlayerAllies;
    CombatRole        role              = CombatRole::MeleeWarrior;
    Vec3              position{};
    Vec3              facing{1.0f, 0.0f, 0.0f};
    float             moveSpeedU        = 250.0f;
    float             weaponReachU      = 145.0f;
    float             hp                = 200.0f;
    float             maxHp             = 200.0f;
    float             stamina           = 120.0f;
    float             maxStamina        = 120.0f;
    float             magicka           = 100.0f;
    float             maxMagicka        = 100.0f;
    float             poise             = 100.0f;
    float             maxPoise          = 100.0f;
    float             armorRating       = 120.0f;
    float             baseAttackDamage  = 28.0f;
    float             basePoiseDamage   = 42.0f;
    DamageElement     attackElement     = DamageElement::Physical;

    CombatActionState state             = CombatActionState::Idle;
    float             stateTimerSec     = 0.0f;
    float             blockAgeSec       = 0.0f; // <= 0.220s triggers timed parry!
    float             staggerTimerSec   = 0.0f;
    float             poiseRegenDelayS  = 0.0f;
    float             shoutCooldownSec  = 0.0f;
    float             animPhaseSec      = 0.0f;
    bool              swingHitApplied   = false;
    bool              powerAttackActive = false;
    uint32_t          targetId          = 0u;
    bool              aiControlled      = true;
};

struct CombatProjectile {
    uint32_t      id             = 0u;
    uint32_t      shooterId      = 0u;
    CombatFaction shooterFaction = CombatFaction::PlayerAllies;
    Vec3          position{};
    Vec3          velocity{};
    float         damage         = 24.0f;
    float         poiseDamage    = 18.0f;
    DamageElement element        = DamageElement::Physical;
    float         ageSec         = 0.0f;
    float         maxLifetimeSec = 4.5f;
    bool          active         = true;
};

struct HitResolution {
    bool  landed             = false;
    bool  parried            = false;
    bool  blocked            = false;
    bool  poiseBroken        = false;
    float finalDamage        = 0.0f;
    float poiseDamageApplied = 0.0f;
};

struct CombatSandboxTelemetry {
    uint32_t aliveAllies       = 0u;
    uint32_t aliveHostiles     = 0u;
    uint32_t totalHitsLanded   = 0u;
    uint32_t totalParries      = 0u;
    uint32_t totalPoiseBreaks  = 0u;
    uint32_t totalArrowsFired  = 0u;
    uint32_t totalArrowBlocks  = 0u;
    uint32_t activeProjectiles = 0u;
    double   lastTickMs        = 0.0;
};

// Pure first-principles combat physics evaluators:
float EvaluateCombatElementalMult(DamageElement element, const CombatWeatherInput& wx) noexcept;
float EvaluateThermalStaminaRegenMult(float ambientTempC) noexcept;
float EvaluateArmorMitigation(float armorRating) noexcept;

class RawCombatSandboxEngine {
public:
    void Clear();

    uint32_t           SpawnActor(const CombatActor& spec);
    CombatActor*       FindActor(uint32_t id);
    const CombatActor* FindActor(uint32_t id) const;

    const std::vector<CombatActor>&      Actors() const noexcept { return actors_; }
    const std::vector<CombatProjectile>& Projectiles() const noexcept { return projectiles_; }

    // Explicit deterministic combat commands:
    bool     StartMeleeAttack(uint32_t actorId, uint32_t targetId, bool powerAttack = false);
    bool     RaiseBlock(uint32_t actorId);
    bool     TriggerShout(uint32_t actorId, float baseCooldownSec = 18.0f);
    uint32_t FireBallisticProjectile(uint32_t shooterId, Vec3 targetPos,
                                     float speedU = 2200.0f,
                                     DamageElement elem = DamageElement::Physical);

    // Resolves a direct melee strike with 220ms directional parry, poise break, armor & weather synergy:
    HitResolution ResolveMeleeHit(CombatActor& attacker, CombatActor& defender,
                                  float rawDamage, float rawPoiseDamage,
                                  DamageElement element, const CombatWeatherInput& wx);

    // Samples world-space weapon hand position from RawSkeletonAnimEngine for an actor's current combat phase:
    static Vec3 EvaluateActorWeaponHandPos(const CombatActor& actor);

    // Advances the entire combat simulation (AI state machines, melee swings, parries, projectiles, physics LOS):
    CombatSandboxTelemetry Step(float dtSec,
                                const CombatWeatherInput& wx,
                                const RawWorldPhysics* physics = nullptr,
                                const RawNavMesh* navOpt = nullptr);

    CombatSandboxTelemetry ComputeTelemetry() const;

private:
    uint32_t FindNearestEnemy(const CombatActor& self) const;
    void     StepActorAiAndStates(float dt, const CombatWeatherInput& wx,
                                  const RawWorldPhysics* physics, const RawNavMesh* navOpt);
    void     StepProjectiles(float dt, const CombatWeatherInput& wx, const RawWorldPhysics* physics);

    std::vector<CombatActor>      actors_;
    std::vector<CombatProjectile> projectiles_;
    uint32_t                      nextActorId_      = 1u;
    uint32_t                      nextProjId_       = 1u;
    uint32_t                      totalHitsLanded_  = 0u;
    uint32_t                      totalParries_     = 0u;
    uint32_t                      totalPoiseBreaks_ = 0u;
    uint32_t                      totalArrowsFired_ = 0u;
    uint32_t                      totalArrowBlocks_ = 0u;
    double                        lastTickMs_       = 0.0;
};

}  // namespace raw::worldgen
