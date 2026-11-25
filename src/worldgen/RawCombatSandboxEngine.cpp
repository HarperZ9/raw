// RawCombatSandboxEngine.cpp: Clean-Room First-Principles RPG Combat, Stamina/Poise, 220ms Timed-Parry,
// Weather Elemental Synergy, Ballistic Raycast & Tactical Squad Encounter Engine.
// Original work, MIT. Zero Windows / D3D12 / proprietary SDK dependencies.

#include "RawCombatSandboxEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace raw::worldgen {
namespace {

inline bool IsFinite3(Vec3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

inline Vec2 NormalizeXY(Vec3 v) noexcept {
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    return (len > 1.0e-5f) ? Vec2{v.x / len, v.y / len} : Vec2{1.0f, 0.0f};
}

inline float DistXY(Vec3 a, Vec3 b) noexcept {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

bool HasLos(const RawWorldPhysics* physics, Vec3 a, Vec3 b) {
    if (!physics) return true;
    const Vec3 p0{a.x, a.y, a.z + 96.0f};
    const Vec3 p1{b.x, b.y, b.z + 96.0f};
    const Vec3 d = p1 - p0;
    const float dist = Length(d);
    if (dist < 1.0e-3f) return true;
    const RayHit hit = physics->RayCast(p0, d / dist, dist - 2.0f);
    return !hit.hit;
}

}  // namespace

// ============================================================================
// Pure First-Principles Combat Physics Evaluators
// ============================================================================

float EvaluateCombatElementalMult(DamageElement element, const CombatWeatherInput& wx) noexcept {
    const float wet  = Clamp01(std::isfinite(wx.wetness01) ? wx.wetness01 : 0.0f);
    const float temp = std::isfinite(wx.ambientTempC) ? wx.ambientTempC : 12.0f;

    switch (element) {
        case DamageElement::Shock:
            return 1.0f + 0.40f * wet;
        case DamageElement::Fire:
            return 1.0f - 0.28f * wet;
        case DamageElement::Frost: {
            const float coldBoost = (temp < 0.0f) ? (0.12f * Clamp01(-temp / 20.0f)) : 0.0f;
            return 1.0f + 0.30f * wet + coldBoost;
        }
        case DamageElement::Physical:
        default:
            return 1.0f;
    }
}

float EvaluateThermalStaminaRegenMult(float ambientTempC) noexcept {
    const float temp = std::isfinite(ambientTempC) ? ambientTempC : 12.0f;
    if (temp >= 0.0f) {
        return 1.0f;
    }
    return 1.0f - 0.38f * Clamp01(-temp / 20.0f);
}

float EvaluateArmorMitigation(float armorRating) noexcept {
    const float ar = std::max(0.0f, std::isfinite(armorRating) ? armorRating : 0.0f);
    return Clamp(ar / (ar + 350.0f), 0.0f, 0.80f);
}

// ============================================================================
// Engine State Management
// ============================================================================

void RawCombatSandboxEngine::Clear() {
    actors_.clear();
    projectiles_.clear();
    nextActorId_      = 1u;
    nextProjId_       = 1u;
    totalHitsLanded_  = 0u;
    totalParries_     = 0u;
    totalPoiseBreaks_ = 0u;
    totalArrowsFired_ = 0u;
    totalArrowBlocks_ = 0u;
    lastTickMs_       = 0.0;
}

uint32_t RawCombatSandboxEngine::SpawnActor(const CombatActor& spec) {
    CombatActor a = spec;
    a.id          = nextActorId_++;
    a.maxHp       = std::max(1.0f, a.maxHp);
    a.hp          = Clamp(a.hp, 0.0f, a.maxHp);
    a.maxStamina  = std::max(1.0f, a.maxStamina);
    a.stamina     = Clamp(a.stamina, 0.0f, a.maxStamina);
    a.maxMagicka  = std::max(1.0f, a.maxMagicka);
    a.magicka     = Clamp(a.magicka, 0.0f, a.maxMagicka);
    a.maxPoise    = std::max(1.0f, a.maxPoise);
    a.poise       = Clamp(a.poise, 0.0f, a.maxPoise);
    const Vec2 f2 = NormalizeXY(a.facing);
    a.facing      = {f2.x, f2.y, 0.0f};
    if (a.hp <= 0.0f) {
        a.state = CombatActionState::Dead;
    }
    actors_.push_back(a);
    return a.id;
}

CombatActor* RawCombatSandboxEngine::FindActor(uint32_t id) {
    for (CombatActor& a : actors_) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

const CombatActor* RawCombatSandboxEngine::FindActor(uint32_t id) const {
    for (const CombatActor& a : actors_) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

// ============================================================================
// Explicit Combat Actions & Timed-Parry Resolution
// ============================================================================

bool RawCombatSandboxEngine::StartMeleeAttack(uint32_t actorId, uint32_t targetId, bool powerAttack) {
    CombatActor* a = FindActor(actorId);
    if (!a || a->state == CombatActionState::Dead || a->state == CombatActionState::Staggered) {
        return false;
    }
    const float staminaCost = powerAttack ? 28.0f : 12.0f;
    if (a->stamina < staminaCost * 0.5f) {
        return false;
    }
    a->stamina           = std::max(0.0f, a->stamina - staminaCost);
    a->state             = CombatActionState::Windup;
    a->stateTimerSec     = powerAttack ? 0.34f : 0.20f;
    a->swingHitApplied   = false;
    a->powerAttackActive = powerAttack;
    a->targetId          = targetId;
    a->animPhaseSec      = 0.0f;
    return true;
}

bool RawCombatSandboxEngine::RaiseBlock(uint32_t actorId) {
    CombatActor* a = FindActor(actorId);
    if (!a || a->state == CombatActionState::Dead || a->state == CombatActionState::Staggered) {
        return false;
    }
    a->state         = CombatActionState::Blocking;
    a->blockAgeSec   = 0.0f; // Starts fresh 220 ms timed-parry window!
    a->stateTimerSec = 0.65f;
    return true;
}

bool RawCombatSandboxEngine::TriggerShout(uint32_t actorId, float baseCooldownSec) {
    CombatActor* caster = FindActor(actorId);
    if (!caster || caster->state == CombatActionState::Dead || caster->shoutCooldownSec > 0.0f) {
        return false;
    }
    caster->shoutCooldownSec = std::max(2.0f, baseCooldownSec);

    // Unrelenting Force frontal stagger cone (range 480u, cos(35 deg) ~= 0.819):
    const Vec2 fwd = NormalizeXY(caster->facing);
    for (CombatActor& other : actors_) {
        if (other.id == caster->id || other.faction == caster->faction ||
            other.state == CombatActionState::Dead) {
            continue;
        }
        const Vec2 toOther{other.position.x - caster->position.x, other.position.y - caster->position.y};
        const float dist = Length(toOther);
        if (dist > 480.0f || dist < 1.0e-3f) continue;
        const Vec2 dir = toOther * (1.0f / dist);
        if (Dot(fwd, dir) >= 0.70f) {
            other.poise = 0.0f;
            other.state = CombatActionState::Staggered;
            other.staggerTimerSec = 1.20f;
            other.poiseRegenDelayS = 2.0f;
            ++totalPoiseBreaks_;
        }
    }
    return true;
}

uint32_t RawCombatSandboxEngine::FireBallisticProjectile(uint32_t shooterId, Vec3 targetPos,
                                                         float speedU, DamageElement elem) {
    CombatActor* shooter = FindActor(shooterId);
    if (!shooter || shooter->state == CombatActionState::Dead || !IsFinite3(targetPos)) {
        return 0u;
    }
    const Vec3 launchPos{shooter->position.x, shooter->position.y, shooter->position.z + 96.0f};
    const Vec3 aimPos{targetPos.x, targetPos.y, targetPos.z + 80.0f};
    const Vec3 delta = aimPos - launchPos;
    const float dist = Length(delta);
    if (dist < 1.0f) {
        return 0u;
    }
    const float safeSpeed = Clamp(speedU, 400.0f, 6000.0f);
    // Ballistic gravity lead compensation (0.5 * g * t^2 upward aim lead):
    constexpr float kArrowGravityZ = 380.0f;
    const float flightT = dist / safeSpeed;
    Vec3 aimLead = delta;
    aimLead.z += 0.5f * kArrowGravityZ * flightT * flightT;
    const Vec3 dir = Normalize(aimLead);

    CombatProjectile p{};
    p.id             = nextProjId_++;
    p.shooterId      = shooter->id;
    p.shooterFaction = shooter->faction;
    p.position       = launchPos;
    p.velocity       = dir * safeSpeed;
    p.damage         = shooter->baseAttackDamage;
    p.poiseDamage    = shooter->basePoiseDamage * 0.5f;
    p.element        = elem;
    p.active         = true;
    projectiles_.push_back(p);
    ++totalArrowsFired_;
    return p.id;
}

HitResolution RawCombatSandboxEngine::ResolveMeleeHit(CombatActor& attacker, CombatActor& defender,
                                                      float rawDamage, float rawPoiseDamage,
                                                      DamageElement element, const CombatWeatherInput& wx) {
    HitResolution res{};
    if (attacker.state == CombatActionState::Dead || defender.state == CombatActionState::Dead) {
        return res;
    }

    const Vec2 toAttacker = NormalizeXY(attacker.position - defender.position);
    const Vec2 defFwd     = NormalizeXY(defender.facing);
    const bool frontal    = (Dot(defFwd, toAttacker) > 0.25f);

    // 1. Check 220ms directional timed parry!
    if (defender.state == CombatActionState::Blocking && frontal &&
        defender.blockAgeSec <= kTimedParryWindowSec) {
        res.landed             = false;
        res.parried            = true;
        res.finalDamage        = 0.0f;
        res.poiseDamageApplied = 0.0f;

        // Riposte stagger on attacker + small stamina refund to defender:
        attacker.state           = CombatActionState::Staggered;
        attacker.staggerTimerSec = 1.15f;
        attacker.poise           = 0.0f;
        attacker.poiseRegenDelayS = 2.0f;
        defender.stamina         = std::min(defender.maxStamina, defender.stamina + 14.0f);
        ++totalParries_;
        return res;
    }

    const float elemMult = EvaluateCombatElementalMult(element, wx);
    const float armorMit = (element == DamageElement::Physical) ? EvaluateArmorMitigation(defender.armorRating) : 0.0f;
    float dmg   = std::max(0.0f, rawDamage * elemMult * (1.0f - armorMit));
    float poise = std::max(0.0f, rawPoiseDamage * (element == DamageElement::Frost ? elemMult : 1.0f));

    // 2. Sustained directional shield/weapon block (outside 220ms parry window):
    if (defender.state == CombatActionState::Blocking && frontal) {
        res.blocked = true;
        dmg   *= 0.30f;
        poise *= 0.35f;
        const float stamDrain = 18.0f + rawPoiseDamage * 0.25f;
        defender.stamina = std::max(0.0f, defender.stamina - stamDrain);
        if (defender.stamina <= 0.1f) {
            // Guard break!
            poise = defender.poise + 1.0f;
        }
    }

    res.landed             = true;
    res.finalDamage        = dmg;
    res.poiseDamageApplied = poise;
    ++totalHitsLanded_;

    defender.hp               = std::max(0.0f, defender.hp - dmg);
    defender.poise           -= poise;
    defender.poiseRegenDelayS = 2.2f;

    if (defender.hp <= 0.0f) {
        defender.state = CombatActionState::Dead;
    } else if (defender.poise <= 0.0f) {
        res.poiseBroken          = true;
        defender.state           = CombatActionState::Staggered;
        defender.staggerTimerSec = 0.95f;
        defender.poise           = defender.maxPoise; // Reset poise pool after stagger
        ++totalPoiseBreaks_;
    }
    return res;
}

Vec3 RawCombatSandboxEngine::EvaluateActorWeaponHandPos(const CombatActor& actor) {
    AnimBlendParams bp{};
    bp.phaseSec = actor.animPhaseSec;
    if (actor.state == CombatActionState::Windup || actor.state == CombatActionState::ActiveSwing) {
        bp.primaryClip = AnimClipId::MeleeSwing;
    } else if (actor.state == CombatActionState::BowDrawing) {
        bp.primaryClip = AnimClipId::BowDraw;
    } else {
        bp.primaryClip = AnimClipId::Idle;
    }
    const float yaw = std::atan2(actor.facing.y, actor.facing.x) - 1.5707963f;
    SkeletonPose pose{};
    RawSkeletonAnimEngine::EvaluateCharacterRig(actor.position, yaw, bp, nullptr, pose);
    return pose.world[static_cast<size_t>(BoneId::HandR)].pos;
}

// ============================================================================
// Coupled Simulation Tick (AI + Melee + Projectiles + Physics LOS)
// ============================================================================

uint32_t RawCombatSandboxEngine::FindNearestEnemy(const CombatActor& self) const {
    uint32_t bestId = 0u;
    float    bestD  = 1.0e20f;
    for (const CombatActor& other : actors_) {
        if (other.faction == self.faction || other.state == CombatActionState::Dead) {
            continue;
        }
        const float d = DistXY(self.position, other.position);
        if (d < bestD) {
            bestD  = d;
            bestId = other.id;
        }
    }
    return bestId;
}

void RawCombatSandboxEngine::StepActorAiAndStates(float dt, const CombatWeatherInput& wx,
                                                  const RawWorldPhysics* physics,
                                                  const RawNavMesh* navOpt) {
    (void)navOpt;
    const float stamRegenMult = EvaluateThermalStaminaRegenMult(wx.ambientTempC);

    for (size_t i = 0u; i < actors_.size(); ++i) {
        CombatActor& a = actors_[i];
        if (a.state == CombatActionState::Dead) continue;

        a.animPhaseSec += dt;

        // Resource & cooldown updates:
        if (a.shoutCooldownSec > 0.0f) {
            a.shoutCooldownSec = std::max(0.0f, a.shoutCooldownSec - dt);
        }
        if (a.state != CombatActionState::Windup && a.state != CombatActionState::ActiveSwing &&
            a.state != CombatActionState::Blocking) {
            a.stamina = std::min(a.maxStamina, a.stamina + 16.0f * stamRegenMult * dt);
        }
        a.magicka = std::min(a.maxMagicka, a.magicka + 12.0f * dt);

        if (a.poiseRegenDelayS > 0.0f) {
            a.poiseRegenDelayS = std::max(0.0f, a.poiseRegenDelayS - dt);
        } else if (a.poise < a.maxPoise) {
            a.poise = std::min(a.maxPoise, a.poise + 22.0f * dt);
        }

        // Handle active timed states:
        if (a.state == CombatActionState::Staggered) {
            a.staggerTimerSec -= dt;
            if (a.staggerTimerSec <= 0.0f) {
                a.state           = CombatActionState::Idle;
                a.staggerTimerSec = 0.0f;
            }
            continue;
        }
        if (a.state == CombatActionState::Blocking) {
            a.blockAgeSec   += dt;
            a.stateTimerSec -= dt;
            if (a.stateTimerSec <= 0.0f) {
                a.state = CombatActionState::Idle;
            }
            continue;
        }
        if (a.state == CombatActionState::Windup) {
            a.stateTimerSec -= dt;
            if (a.stateTimerSec <= 0.0f) {
                a.state         = CombatActionState::ActiveSwing;
                a.stateTimerSec = 0.14f;
            }
            continue;
        }
        if (a.state == CombatActionState::ActiveSwing) {
            if (!a.swingHitApplied) {
                a.swingHitApplied = true;
                if (CombatActor* tgt = FindActor(a.targetId)) {
                    if (tgt->state != CombatActionState::Dead &&
                        DistXY(a.position, tgt->position) <= a.weaponReachU + 25.0f) {
                        const float dmg   = a.powerAttackActive ? (a.baseAttackDamage * 1.45f) : a.baseAttackDamage;
                        const float poise = a.powerAttackActive ? (a.basePoiseDamage * 1.55f) : a.basePoiseDamage;
                        ResolveMeleeHit(a, *tgt, dmg, poise, a.attackElement, wx);
                    }
                }
            }
            a.stateTimerSec -= dt;
            if (a.stateTimerSec <= 0.0f && a.state == CombatActionState::ActiveSwing) {
                a.state         = CombatActionState::Recovery;
                a.stateTimerSec = 0.24f;
            }
            continue;
        }
        if (a.state == CombatActionState::Recovery) {
            a.stateTimerSec -= dt;
            if (a.stateTimerSec <= 0.0f) {
                a.state = CombatActionState::Idle;
            }
            continue;
        }
        if (a.state == CombatActionState::BowDrawing || a.state == CombatActionState::Casting) {
            a.stateTimerSec -= dt;
            if (a.stateTimerSec <= 0.0f) {
                if (CombatActor* tgt = FindActor(a.targetId)) {
                    if (tgt->state != CombatActionState::Dead) {
                        const float spd = (a.role == CombatRole::Battlemage) ? 1800.0f : 2300.0f;
                        FireBallisticProjectile(a.id, tgt->position, spd, a.attackElement);
                    }
                }
                a.state         = CombatActionState::Recovery;
                a.stateTimerSec = 0.45f;
            }
            continue;
        }

        // Idle AI tactical decision & flanking/standoff movement:
        if (!a.aiControlled) continue;

        const uint32_t enemyId = FindNearestEnemy(a);
        if (enemyId == 0u) continue;
        a.targetId = enemyId;
        const CombatActor* enemy = FindActor(enemyId);
        if (!enemy) continue;

        const Vec2 toEnemy = NormalizeXY(enemy->position - a.position);
        const float dist   = DistXY(a.position, enemy->position);
        a.facing = {toEnemy.x, toEnemy.y, 0.0f};

        const bool lowHpRetreat = (a.hp / a.maxHp < 0.22f);
        Vec2 moveDir{};

        if (lowHpRetreat) {
            moveDir = {-toEnemy.x, -toEnemy.y};
        } else if (a.role == CombatRole::Archer || a.role == CombatRole::Battlemage) {
            const bool los = HasLos(physics, a.position, enemy->position);
            if (!los) {
                // Lateral strafe to clear obstacle LOS:
                const float side = ((a.id & 1u) == 0u) ? 1.0f : -1.0f;
                moveDir = {-toEnemy.y * side, toEnemy.x * side};
            } else if (dist < 420.0f) {
                moveDir = {-toEnemy.x, -toEnemy.y};
            } else if (dist > 950.0f) {
                moveDir = toEnemy;
            } else {
                // In standoff firing band with clear LOS -> draw bow / cast spell!
                a.state         = (a.role == CombatRole::Battlemage) ? CombatActionState::Casting
                                                                     : CombatActionState::BowDrawing;
                a.stateTimerSec = 0.42f;
                continue;
            }
        } else {
            // MeleeWarrior / ShieldBearer: reactively timed-block incoming enemy windup, else flank & strike!
            if (a.role == CombatRole::ShieldBearer && enemy->state == CombatActionState::Windup &&
                dist <= enemy->weaponReachU + 40.0f && enemy->stateTimerSec <= 0.18f) {
                RaiseBlock(a.id);
                continue;
            }
            if (dist <= a.weaponReachU) {
                const bool power = (a.stamina > 70.0f && ((a.id + totalHitsLanded_) % 3u == 0u));
                StartMeleeAttack(a.id, enemy->id, power);
                continue;
            }
            // Flanking angular offset so squad fans out instead of conga-lining:
            const float flankSign = ((a.id & 1u) == 0u) ? 1.0f : -1.0f;
            const float flankWeight = Clamp01((dist - a.weaponReachU) / 300.0f) * 0.45f;
            const Vec2 perp{-toEnemy.y * flankSign, toEnemy.x * flankSign};
            moveDir = Normalize(toEnemy + perp * flankWeight);
        }

        // Local ally/enemy separation:
        Vec2 sep{};
        for (size_t j = 0u; j < actors_.size(); ++j) {
            if (i == j || actors_[j].state == CombatActionState::Dead) continue;
            const Vec2 diff{a.position.x - actors_[j].position.x, a.position.y - actors_[j].position.y};
            const float d = Length(diff);
            if (d < 68.0f && d > 1.0e-4f) {
                sep = sep + diff * ((68.0f - d) / (68.0f * d));
            }
        }
        const Vec2 totalDir = Normalize(moveDir + sep * 0.8f);
        a.position.x += totalDir.x * a.moveSpeedU * dt;
        a.position.y += totalDir.y * a.moveSpeedU * dt;

        if (physics) {
            float gz = a.position.z;
            if (physics->SampleTerrainHeight(a.position.x, a.position.y, &gz)) {
                a.position.z = gz;
            }
        }
    }
}

void RawCombatSandboxEngine::StepProjectiles(float dt, const CombatWeatherInput& wx,
                                             const RawWorldPhysics* physics) {
    constexpr float kArrowGravityZ = -380.0f;
    const Vec2 windDir = Normalize(wx.windDirXY);
    const float windAcc = std::max(0.0f, wx.windSpeedMps) * 4.5f;

    for (CombatProjectile& p : projectiles_) {
        if (!p.active) continue;
        p.ageSec += dt;
        if (p.ageSec >= p.maxLifetimeSec) {
            p.active = false;
            continue;
        }

        const Vec3 prevPos = p.position;
        p.velocity.x += windDir.x * windAcc * dt;
        p.velocity.y += windDir.y * windAcc * dt;
        p.velocity.z += kArrowGravityZ * dt;
        const Vec3 nextPos = prevPos + p.velocity * dt;

        const Vec3 seg = nextPos - prevPos;
        const float segLen = Length(seg);
        if (segLen < 1.0e-4f) {
            p.position = nextPos;
            continue;
        }
        const Vec3 segDir = seg / segLen;

        // 1. Check world obstacle/terrain interception first via swept RayCast:
        float maxT = segLen;
        bool blockedByWorld = false;
        if (physics) {
            const RayHit wh = physics->RayCast(prevPos, segDir, segLen);
            if (wh.hit) {
                maxT           = wh.distance;
                blockedByWorld = true;
            }
        }

        // 2. Check enemy actor hit cylinders strictly BEFORE any obstacle hit distance `maxT`:
        CombatActor* hitActor = nullptr;
        float bestActorT      = maxT;
        for (CombatActor& cand : actors_) {
            if (cand.faction == p.shooterFaction || cand.state == CombatActionState::Dead) {
                continue;
            }
            // Closest point on segment [0..maxT] to actor center cylinder (radius 30u, Z in [0..135u]):
            const Vec3 actorMid{cand.position.x, cand.position.y, cand.position.z + 72.0f};
            const float projT = Clamp(Dot(actorMid - prevPos, segDir), 0.0f, bestActorT);
            const Vec3  pt    = prevPos + segDir * projT;
            const float dxy   = DistXY(pt, cand.position);
            if (dxy <= 30.0f && pt.z >= cand.position.z - 5.0f && pt.z <= cand.position.z + 138.0f) {
                bestActorT = projT;
                hitActor   = &cand;
            }
        }

        if (hitActor != nullptr) {
            CombatActor* shooter = FindActor(p.shooterId);
            CombatActor fallbackShooter{};
            fallbackShooter.position = prevPos;
            ResolveMeleeHit(shooter ? *shooter : fallbackShooter, *hitActor,
                            p.damage, p.poiseDamage, p.element, wx);
            p.active = false;
        } else if (blockedByWorld) {
            ++totalArrowBlocks_;
            p.active = false;
        } else {
            p.position = nextPos;
        }
    }

    projectiles_.erase(
        std::remove_if(projectiles_.begin(), projectiles_.end(), [](const CombatProjectile& p) {
            return !p.active;
        }),
        projectiles_.end());
}

CombatSandboxTelemetry RawCombatSandboxEngine::Step(float dtSec,
                                                    const CombatWeatherInput& wx,
                                                    const RawWorldPhysics* physics,
                                                    const RawNavMesh* navOpt) {
    if (!std::isfinite(dtSec) || dtSec <= 0.0f) {
        return ComputeTelemetry();
    }
    const auto t0 = std::chrono::steady_clock::now();
    const float dt = std::min(dtSec, 0.10f);

    StepActorAiAndStates(dt, wx, physics, navOpt);
    StepProjectiles(dt, wx, physics);

    const auto t1 = std::chrono::steady_clock::now();
    lastTickMs_ = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return ComputeTelemetry();
}

CombatSandboxTelemetry RawCombatSandboxEngine::ComputeTelemetry() const {
    CombatSandboxTelemetry t{};
    for (const CombatActor& a : actors_) {
        if (a.state == CombatActionState::Dead) continue;
        if (a.faction == CombatFaction::PlayerAllies) ++t.aliveAllies;
        else                                          ++t.aliveHostiles;
    }
    t.totalHitsLanded   = totalHitsLanded_;
    t.totalParries      = totalParries_;
    t.totalPoiseBreaks  = totalPoiseBreaks_;
    t.totalArrowsFired  = totalArrowsFired_;
    t.totalArrowBlocks  = totalArrowBlocks_;
    t.activeProjectiles = static_cast<uint32_t>(projectiles_.size());
    t.lastTickMs        = lastTickMs_;
    return t;
}

}  // namespace raw::worldgen
