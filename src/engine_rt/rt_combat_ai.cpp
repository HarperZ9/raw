// rt_combat_ai.cpp: Tactical Squad Combat AI, Modern Action Combat & Living World Ecosystem Simulation (Zero SKSE).
// Original work, MIT.
#include "rt_combat_ai.h"

#include <algorithm>
#include <cmath>

namespace raw::rt {

std::vector<TacticalOrder> CoordinateSquadTactics(const float targetPos[3],
                                                  const float targetForward[2],
                                                  const std::vector<CombatAgentSnapshot>& agents,
                                                  uint32_t maxSimultaneousAttackers) {
    std::vector<TacticalOrder> orders;
    if (!targetPos || agents.empty()) return orders;

    float fx = targetForward ? targetForward[0] : 0.0f;
    float fy = targetForward ? targetForward[1] : 1.0f;
    const float fl = std::sqrt(fx * fx + fy * fy);
    if (fl > 1.0e-4f) { fx /= fl; fy /= fl; } else { fx = 0.0f; fy = 1.0f; }
    const float rx = fy, ry = -fx;  // right vector perpendicular to target forward

    // Sort indices by distance to target so closest melee fighter becomes Vanguard and subsequent fighters flank!
    struct DistIdx { size_t idx; float d2; };
    std::vector<DistIdx> sorted;
    sorted.reserve(agents.size());
    for (size_t i = 0; i < agents.size(); ++i) {
        const float dx = agents[i].pos[0] - targetPos[0];
        const float dy = agents[i].pos[1] - targetPos[1];
        sorted.push_back({i, dx * dx + dy * dy});
    }
    std::sort(sorted.begin(), sorted.end(), [](const DistIdx& a, const DistIdx& b) { return a.d2 < b.d2; });

    uint32_t meleeSlot = 0;
    uint32_t rangedSlot = 0;
    uint32_t tokensIssued = 0;

    orders.resize(agents.size());
    for (const auto& entry : sorted) {
        const auto& ag = agents[entry.idx];
        TacticalOrder ord{};
        ord.actorId = ag.actorId;
        ord.desiredPos[2] = targetPos[2];

        if (ag.isCaster) {
            ord.role = TacticalRole::SupportCaster;
            const float sign = (rangedSlot & 1u) ? 1.0f : -1.0f;
            ord.desiredPos[0] = targetPos[0] - fx * 420.0f + rx * (sign * 180.0f);
            ord.desiredPos[1] = targetPos[1] - fy * 420.0f + ry * (sign * 180.0f);
            ord.aggression = 0.65f;
            ++rangedSlot;
        } else if (ag.isRanged) {
            ord.role = TacticalRole::SkirmisherRanged;
            const float sign = (rangedSlot & 1u) ? -1.0f : 1.0f;
            ord.desiredPos[0] = targetPos[0] + fx * 380.0f + rx * (sign * 240.0f);
            ord.desiredPos[1] = targetPos[1] + fy * 380.0f + ry * (sign * 240.0f);
            ord.aggression = 0.72f;
            ++rangedSlot;
        } else {
            if (meleeSlot == 0u) {
                ord.role = TacticalRole::Vanguard;
                ord.desiredPos[0] = targetPos[0] + fx * 115.0f;
                ord.desiredPos[1] = targetPos[1] + fy * 115.0f;
                ord.aggression = 0.95f;
            } else if ((meleeSlot & 1u) == 1u) {
                ord.role = TacticalRole::FlankerLeft;
                const float ring = 135.0f + float((meleeSlot - 1u) / 2u) * 45.0f;
                ord.desiredPos[0] = targetPos[0] - rx * ring - fx * 45.0f;
                ord.desiredPos[1] = targetPos[1] - ry * ring - fy * 45.0f;
                ord.aggression = 0.78f;
            } else {
                ord.role = TacticalRole::FlankerRight;
                const float ring = 135.0f + float((meleeSlot - 1u) / 2u) * 45.0f;
                ord.desiredPos[0] = targetPos[0] + rx * ring - fx * 45.0f;
                ord.desiredPos[1] = targetPos[1] + ry * ring - fy * 45.0f;
                ord.aggression = 0.78f;
            }
            if (tokensIssued < maxSimultaneousAttackers && ag.hpFrac > 0.15f) {
                ord.attackToken = true;
                ++tokensIssued;
            }
            ++meleeSlot;
        }
        orders[entry.idx] = ord;
    }
    return orders;
}

void StepCombatTimers(CombatActorState& st, bool blockHeldNow, bool blockEdgeNow, bool dodgeEdgeNow, float dtSec) {
    const float dt = std::clamp(dtSec, 0.0f, 0.1f);
    st.hitStopSec       = std::max(0.0f, st.hitStopSec - dt);
    st.staggerSec       = std::max(0.0f, st.staggerSec - dt);
    st.dodgeIFrameSec   = std::max(0.0f, st.dodgeIFrameSec - dt);
    st.dodgeCooldownSec = std::max(0.0f, st.dodgeCooldownSec - dt);
    st.poiseRegenDelay  = std::max(0.0f, st.poiseRegenDelay - dt);

    // Precision block timer starts at 0.0 on rising block edge and increments while held
    if (blockEdgeNow) {
        st.blockHeld = true;
        st.blockElapsedSec = 0.0f;
    } else if (blockHeldNow) {
        st.blockHeld = true;
        st.blockElapsedSec += dt;
    } else {
        st.blockHeld = false;
        st.blockElapsedSec = 10.0f;
    }

    // Stamina-gated directional dodge-step (240 ms invulnerability window, 22 stamina cost)
    if (dodgeEdgeNow && st.staggerSec <= 0.0f && st.dodgeCooldownSec <= 0.0f && st.stamina >= 22.0f) {
        st.stamina -= 22.0f;
        st.dodgeIFrameSec = 0.24f;
        st.dodgeCooldownSec = 0.55f;
    }

    // Smooth stamina & poise recovery outside hit-stop/stagger
    if (st.staggerSec <= 0.0f) {
        st.stamina = std::min(st.staminaMax, st.stamina + (st.blockHeld ? 8.0f : 24.0f) * dt);
        if (st.poiseRegenDelay <= 0.0f) {
            st.poiseCurrent = std::min(st.poiseMax, st.poiseCurrent + 30.0f * dt);
        }
    }
}

HitResolution ResolveCombatHit(CombatActorState& defender, CombatActorState& attacker,
                               const IncomingHit& hit, float parryWindowMs, float hitStopBaseMs) {
    HitResolution res{};
    // 1. I-Frame dodge evades cleanly
    if (defender.dodgeIFrameSec > 0.0f) {
        res.dodgedIFrame = true;
        return res;
    }

    // 2. Precision Timed Parry (within parryWindowMs of raising guard)
    const float parryWindowSec = std::clamp(parryWindowMs, 60.0f, 350.0f) * 0.001f;
    if (!hit.isUnblockable && defender.blockHeld && defender.blockElapsedSec <= parryWindowSec && defender.staggerSec <= 0.0f) {
        res.perfectParry = true;
        res.attackerStagger = true;
        res.hitStopMs = hitStopBaseMs * 1.35f;
        attacker.poiseCurrent = 0.0f;
        attacker.staggerSec = 1.40f;
        attacker.hitStopSec = res.hitStopMs * 0.001f;
        defender.hitStopSec = res.hitStopMs * 0.001f;
        return res;
    }

    // 3. Standard Guard Block (absorbs 80% HP damage at stamina + reduced poise cost)
    if (!hit.isUnblockable && defender.blockHeld && defender.stamina > 5.0f && defender.staggerSec <= 0.0f) {
        res.blocked = true;
        res.appliedHpDamage = hit.hpDamage * 0.20f;
        res.appliedStamina = std::min(defender.stamina, hit.poiseDamage * 0.55f);
        defender.stamina = std::max(0.0f, defender.stamina - res.appliedStamina);
        defender.poiseCurrent = std::max(0.0f, defender.poiseCurrent - hit.poiseDamage * 0.40f);
        defender.poiseRegenDelay = 2.0f;
        res.hitStopMs = hitStopBaseMs * 0.65f;
        if (defender.poiseCurrent <= 0.0f || defender.stamina <= 0.0f) {
            res.defenderStagger = true;
            defender.staggerSec = 1.15f;
        }
        return res;
    }

    // 4. Clean Unblocked Hit (full HP & poise damage + melee hit-stop)
    res.appliedHpDamage = hit.hpDamage;
    defender.poiseCurrent = std::max(0.0f, defender.poiseCurrent - hit.poiseDamage);
    defender.poiseRegenDelay = 2.2f;
    res.hitStopMs = hit.isHeavy ? (hitStopBaseMs * 1.25f) : hitStopBaseMs;
    if (defender.poiseCurrent <= 0.0f) {
        res.defenderStagger = true;
        defender.staggerSec = hit.isHeavy ? 1.35f : 0.90f;
        defender.poiseCurrent = defender.poiseMax;  // reset after stagger break
    }
    return res;
}

WorldNpcDirective EvaluateWorldEcosystem(float gameHour, float rain01, float snow01, float snowDepthU,
                                         bool nearFire, bool inCombat) {
    if (inCombat) return WorldNpcDirective::AlertPatrol;
    const bool severeStorm = (rain01 > 0.55f) || (snow01 > 0.60f && snowDepthU > 12.0f);
    if (severeStorm) return nearFire ? WorldNpcDirective::WarmAtBrazier : WorldNpcDirective::SeekShelter;
    const bool freezing = (snow01 > 0.25f || snowDepthU > 8.0f);
    if (freezing && nearFire) return WorldNpcDirective::WarmAtBrazier;
    const bool isNight = (gameHour < 5.75f || gameHour >= 19.5f);
    if (isNight) return WorldNpcDirective::EquipTorch;
    return WorldNpcDirective::NormalRoutine;
}

}  // namespace raw::rt
