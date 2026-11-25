// rt_combat_ai.cpp: Tactical Squad Combat AI, Modern Action Combat & Living World Ecosystem Simulation (Zero SKSE).
// Original work, MIT.
#include "rt_combat_ai.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>

#ifndef RAW_SIM_PURE_TEST
#include "rt_detour.h"
#include "rt_image.h"
#include "rt_version.h"
#include "rt_view.h"
#include "rt_weather.h"
#include <windows.h>
#endif

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

TacticalPolicyResult SolveTacticalStochasticGame(const TacticalGameState& st) {
    // 4-state discounted/mean-payoff stochastic combat game:
    // States: 0=Advantage (healthy+poise), 1=Contested, 2=Pressured (enemy swinging/low poise), 3=Critical (low HP/stamina)
    // Actions: 0=PressAttack, 1=FlankCircle, 2=GuardParry, 3=RetreatRecover
    const float hp   = std::clamp(st.hpFrac, 0.0f, 1.0f);
    const float stam = std::clamp(st.staminaFrac, 0.0f, 1.0f);
    const float pois = std::clamp(st.poiseFrac, 0.0f, 1.0f);
    const float closeW = std::clamp(1.0f - (st.distUnits - 90.0f) / 260.0f, 0.0f, 1.0f);
    const float tok    = st.hasAttackToken ? 1.0f : 0.0f;
    const float threat = st.enemyAttacking ? 1.0f : 0.0f;

    // Immediate state-conditioned reward matrix R[s][a] and stochastic transition kernels T[s][a][s']
    float R[4][4] = {
        { 0.92f * tok * closeW - 0.55f * threat,  0.58f,  0.35f + 0.65f * threat,  0.10f },
        { 0.68f * tok * closeW - 0.72f * threat,  0.54f,  0.48f + 0.78f * threat,  0.26f },
        { 0.20f * tok - 1.10f * threat,           0.32f,  0.65f + 0.92f * threat * stam, 0.58f },
        {-0.85f,                                  0.18f,  0.35f * stam,            1.05f + 0.45f * (1.0f - hp) }
    };
    static constexpr float T[4][4][4] = {
        { {0.65f, 0.25f, 0.08f, 0.02f}, {0.55f, 0.35f, 0.08f, 0.02f}, {0.50f, 0.40f, 0.08f, 0.02f}, {0.35f, 0.50f, 0.12f, 0.03f} },
        { {0.38f, 0.38f, 0.18f, 0.06f}, {0.42f, 0.44f, 0.11f, 0.03f}, {0.46f, 0.40f, 0.12f, 0.02f}, {0.30f, 0.52f, 0.15f, 0.03f} },
        { {0.15f, 0.25f, 0.38f, 0.22f}, {0.20f, 0.42f, 0.28f, 0.10f}, {0.36f, 0.40f, 0.18f, 0.06f}, {0.22f, 0.54f, 0.18f, 0.06f} },
        { {0.05f, 0.10f, 0.35f, 0.50f}, {0.08f, 0.28f, 0.36f, 0.28f}, {0.10f, 0.30f, 0.35f, 0.25f}, {0.18f, 0.48f, 0.24f, 0.10f} }
    };

    float V[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    constexpr float gamma = 0.82f;
    uint32_t it = 0;
    for (; it < 24u; ++it) {
        float nextV[4] = {};
        float maxDelta = 0.0f;
        for (int s = 0; s < 4; ++s) {
            float bestQ = -1.0e9f;
            for (int a = 0; a < 4; ++a) {
                float q = R[s][a];
                for (int sp = 0; sp < 4; ++sp) q += gamma * T[s][a][sp] * V[sp];
                if (q > bestQ) bestQ = q;
            }
            nextV[s] = bestQ;
            maxDelta = std::max(maxDelta, std::fabs(nextV[s] - V[s]));
        }
        for (int s = 0; s < 4; ++s) V[s] = nextV[s];
        if (maxDelta < 1.0e-4f) { ++it; break; }
    }

    // Softly project live continuous combat snapshot onto the 4-state belief simplex:
    float w[4] = {};
    if (hp < 0.28f || (hp < 0.40f && stam < 0.22f)) {
        w[3] = 0.85f; w[2] = 0.15f;
    } else if (st.enemyAttacking || pois < 0.35f) {
        w[2] = 0.75f; w[1] = 0.20f; w[3] = 0.05f;
    } else if (hp > 0.65f && stam > 0.45f && st.hasAttackToken) {
        w[0] = 0.78f; w[1] = 0.22f;
    } else {
        w[1] = 0.70f; w[0] = 0.20f; w[2] = 0.10f;
    }

    TacticalPolicyResult out{};
    out.iterations = it;
    int bestA = 1;
    float bestVal = -1.0e9f;
    for (int a = 0; a < 4; ++a) {
        float val = 0.0f;
        for (int s = 0; s < 4; ++s) {
            float q = R[s][a];
            for (int sp = 0; sp < 4; ++sp) q += gamma * T[s][a][sp] * V[sp];
            val += w[s] * q;
        }
        out.actionValues[a] = val;
        if (val > bestVal) { bestVal = val; bestA = a; }
    }
    out.bestAction = static_cast<TacticalAction>(bestA);
    out.expectedPayoff = bestVal;
    return out;
}

namespace {
void QuantizeD8Cpu(const float in8[8], float out8[8]) {
    int parity = 0;
    for (int i = 0; i < 8; ++i) {
        out8[i] = std::round(in8[i]);
        parity ^= (static_cast<int>(out8[i]) & 1);
    }
    if (parity != 0) {
        int best = 0;
        float maxD = -1.0f;
        for (int i = 0; i < 8; ++i) {
            const float ad = std::fabs(in8[i] - out8[i]);
            if (ad > maxD) { maxD = ad; best = i; }
        }
        out8[best] += (in8[best] - out8[best] >= 0.0f) ? 1.0f : -1.0f;
    }
}
}  // namespace

void QuantizeE8Lattice(const float in8[8], float scale, float out8[8]) {
    const float s = std::max(scale, 1.0e-4f);
    const float invS = 1.0f / s;
    float sc[8], sh[8], p0[8], p1[8];
    for (int i = 0; i < 8; ++i) {
        sc[i] = in8[i] * s;
        sh[i] = sc[i] - 0.5f;
    }
    QuantizeD8Cpu(sc, p0);
    QuantizeD8Cpu(sh, p1);
    float d0 = 0.0f, d1 = 0.0f;
    for (int i = 0; i < 8; ++i) {
        p1[i] += 0.5f;
        const float e0 = sc[i] - p0[i];
        const float e1 = sc[i] - p1[i];
        d0 += e0 * e0;
        d1 += e1 * e1;
    }
    const float* pick = (d0 <= d1) ? p0 : p1;
    for (int i = 0; i < 8; ++i) out8[i] = pick[i] * invS;
}

PhysicsSubstepPlan ComputePhysicsSubstep(float rawFrameDtSec) {
    PhysicsSubstepPlan p{};
    constexpr float kMinDt = 1.0f / 360.0f;
    constexpr float kMaxDt = 0.050f;         // clamp >50ms hitches so rigid bodies never tunnel/explode
    constexpr float kMaxSubstep = 1.0f / 120.0f;
    if (!std::isfinite(rawFrameDtSec) || rawFrameDtSec <= 0.0f) {
        p.clampedFrameDt = 1.0f / 60.0f;
        p.spikeClamped = true;
    } else if (rawFrameDtSec > kMaxDt) {
        p.clampedFrameDt = kMaxDt;
        p.spikeClamped = true;
    } else {
        p.clampedFrameDt = std::max(rawFrameDtSec, kMinDt);
    }
    const uint32_t steps = std::clamp<uint32_t>(
        static_cast<uint32_t>(std::ceil(p.clampedFrameDt / kMaxSubstep)), 1u, 6u);
    p.substeps = steps;
    p.substepDt = p.clampedFrameDt / static_cast<float>(steps);
    return p;
}

ThermalExposureState EvaluateThermalExposure(float altitudeZ, float windSpeed, float rain01, float snow01,
                                             bool inInterior, bool nearFire, float prevWarmth, float dtSec) {
    ThermalExposureState s{};
    const float dt = std::clamp(dtSec, 0.0f, 10.0f);
    // Atmospheric environmental lapse rate (-6.5 C per 1000m; Skyrim 1m ~= 70 world units):
    const float altMeters = std::max(0.0f, altitudeZ / 70.0f);
    const float baseTemp = inInterior ? 18.0f : (11.0f - altMeters * 0.0065f - std::clamp(snow01, 0.0f, 1.0f) * 14.0f);
    const float windChill = inInterior ? 0.0f : std::min(16.0f, std::max(0.0f, windSpeed) * 0.045f);
    const float wetDrain = inInterior ? 0.0f : (std::clamp(rain01, 0.0f, 1.0f) * 7.5f);
    s.ambientTempC = baseTemp - windChill - (nearFire ? -26.0f : 0.0f);

    float ratePerSec = 0.0f;
    if (nearFire) {
        ratePerSec = +8.5f;
    } else if (inInterior) {
        ratePerSec = +2.8f;
    } else {
        const float coldDelta = std::max(0.0f, 8.0f - (baseTemp - windChill));
        ratePerSec = -(coldDelta * 0.14f + wetDrain * 0.25f) + (coldDelta <= 0.0f ? 0.9f : 0.0f);
    }
    s.coreWarmth = std::clamp(prevWarmth + ratePerSec * dt, 0.0f, 100.0f);
    s.shiverIntensity = std::clamp((42.0f - s.coreWarmth) / 42.0f, 0.0f, 1.0f);
    s.freezingRisk = (s.coreWarmth < 25.0f);
    s.staminaRegenMult = std::clamp(0.40f + 0.65f * (s.coreWarmth / 100.0f) + (nearFire ? 0.10f : 0.0f), 0.35f, 1.15f);
    return s;
}

WeaponArcHitResult EvaluateWeaponArcHit(const float swingStart[3], const float swingEnd[3],
                                        const float targetCenter[3], float targetRadius,
                                        float targetHeight, float swingSpeed) {
    WeaponArcHitResult r{};
    if (!swingStart || !swingEnd || !targetCenter || targetRadius <= 0.0f || targetHeight <= 0.0f) return r;
    const float dx = swingEnd[0] - swingStart[0];
    const float dy = swingEnd[1] - swingStart[1];
    const float dz = swingEnd[2] - swingStart[2];
    const float segLen2 = dx * dx + dy * dy + dz * dz;
    float t = 0.0f;
    if (segLen2 > 1.0e-6f) {
        const float px = targetCenter[0] - swingStart[0];
        const float py = targetCenter[1] - swingStart[1];
        const float pz = targetCenter[2] - swingStart[2];
        t = std::clamp((px * dx + py * dy + pz * dz) / segLen2, 0.0f, 1.0f);
    }
    const float cx = swingStart[0] + dx * t;
    const float cy = swingStart[1] + dy * t;
    const float cz = swingStart[2] + dz * t;
    const float rx = cx - targetCenter[0];
    const float ry = cy - targetCenter[1];
    const float horizDist = std::sqrt(rx * rx + ry * ry);
    const float halfH = targetHeight * 0.5f;
    const float vertGap = std::max(0.0f, std::fabs(cz - targetCenter[2]) - halfH);
    const float radGap = std::max(0.0f, horizDist - targetRadius);
    r.closestDist = std::sqrt(radGap * radGap + vertGap * vertGap);
    r.impactParamT = t;
    r.hit = (horizDist <= targetRadius && vertGap <= 0.0f);
    if (r.hit) {
        const float sweetSpot = 0.72f + 0.58f * t; // tip strikes carry peak angular momentum
        const float spd = std::clamp(swingSpeed, 0.25f, 4.0f);
        r.momentumMult = sweetSpot * (0.75f + 0.35f * spd);
        r.poiseImpulse = 18.0f * r.momentumMult;
    }
    return r;
}

AcousticReverbState ComputeAcousticReverb(bool inInterior, float enclosureRadiusU,
                                          float snowCover01, float rain01,
                                          float windSpeed, bool underwater) {
    AcousticReverbState a{};
    const float snow = std::clamp(snowCover01, 0.0f, 1.0f);
    const float rain = std::clamp(rain01, 0.0f, 1.0f);
    const float wind = std::max(0.0f, windSpeed);
    if (underwater) {
        a.envClass = AcousticEnvClass::Underwater;
        a.rt60Sec = 0.42f;
        a.earlyReflGain = 0.30f;
        a.lowPassHz = 650.0f;
        a.airAbsorbDbPerM = 0.085f;
        return a;
    }
    // Convert Skyrim enclosure radius (70u ~= 1m) into an equivalent spherical room for Sabine RT60 = 0.161 * V / (S * alpha)
    // where V / S = R / 3.
    const float radiusM = std::clamp(enclosureRadiusU / 70.0f, 1.5f, 120.0f);
    if (inInterior) {
        const bool isCavern = (radiusM >= 14.0f);
        a.envClass = isCavern ? AcousticEnvClass::CavernVault : AcousticEnvClass::StoneInterior;
        // Effective absorption (furniture/wood/stone ~0.36; rough damp cavern rock ~0.22) with 1.5x architectural surface complexity:
        const float alpha = isCavern ? 0.22f : 0.36f;
        a.rt60Sec = std::clamp(0.161f * (radiusM / 4.5f) / alpha, 0.45f, 4.80f);
        a.earlyReflGain = std::clamp(0.68f - radiusM * 0.006f, 0.25f, 0.72f);
        a.lowPassHz = isCavern ? 9200.0f : 12800.0f;
        a.airAbsorbDbPerM = 0.008f;
        return a;
    }
    // Exterior: porous fresh snow acts as a broad acoustic absorber (ISO 9613 ground absorption); wind & rain add HF attenuation.
    a.envClass = (radiusM < 45.0f) ? AcousticEnvClass::AlpineValley : AcousticEnvClass::OpenSky;
    const float baseRt = (a.envClass == AcousticEnvClass::AlpineValley) ? 0.58f : 0.18f;
    a.rt60Sec = std::clamp(baseRt * (1.0f - 0.52f * snow) * (1.0f - 0.18f * rain), 0.06f, 1.10f);
    a.earlyReflGain = std::clamp((a.envClass == AcousticEnvClass::AlpineValley ? 0.26f : 0.08f) * (1.0f - 0.45f * snow), 0.02f, 0.35f);
    a.lowPassHz = std::clamp(19500.0f - snow * 5800.0f - rain * 3200.0f - std::min(wind * 12.0f, 3500.0f), 4500.0f, 20000.0f);
    a.airAbsorbDbPerM = 0.004f + snow * 0.012f + rain * 0.009f;
    return a;
}

SmoothCameraLockResult SolveSmoothCameraLockOn(const float prevCamPos[3], const float desiredCamPos[3],
                                               const float playerPos[3], const float* lockTargetPos,
                                               float baseFovDeg, float springOmega, float dtSec) {
    SmoothCameraLockResult out{};
    const float dt = std::clamp(dtSec, 1.0e-4f, 0.1f);
    const float w = std::clamp(springOmega, 2.0f, 35.0f);
    // Exact exponential critically-damped follow factor (framerate-independent 30..360 Hz):
    const float alpha = 1.0f - std::exp(-w * dt);
    if (prevCamPos && desiredCamPos) {
        for (int i = 0; i < 3; ++i) {
            out.smoothedPos[i] = prevCamPos[i] + (desiredCamPos[i] - prevCamPos[i]) * alpha;
        }
    }
    const float clampedBase = std::clamp(baseFovDeg, 60.0f, 110.0f);
    out.dynamicFovDeg = clampedBase;
    if (playerPos && lockTargetPos) {
        const float dx = lockTargetPos[0] - playerPos[0];
        const float dy = lockTargetPos[1] - playerPos[1];
        const float dz = lockTargetPos[2] - playerPos[2];
        const float horiz = std::sqrt(dx * dx + dy * dy);
        const float dist = std::sqrt(horiz * horiz + dz * dz);
        if (dist > 25.0f && dist < 1600.0f) {
            out.lockedOn = true;
            out.lockYawRad = std::atan2(dx, dy);
            out.lockPitchRad = std::clamp(std::atan2(dz, std::max(horiz, 1.0f)), -1.15f, 1.15f);
            // Pull combat FOV subtly wider at close range (+6 deg) for clean melee spatial awareness:
            const float closePull = std::clamp(1.0f - (dist - 120.0f) / 700.0f, 0.0f, 1.0f);
            out.dynamicFovDeg = std::clamp(clampedBase + closePull * 6.0f, 65.0f, 115.0f);
        }
    }
    return out;
}

DynamicEconomyState EvaluateDynamicEconomy(float storm01, float snowDepthU, float gameHour,
                                           uint32_t nearbyActors, bool inInterior) {
    DynamicEconomyState e{};
    const float storm = std::clamp(storm01, 0.0f, 1.0f);
    const float snowScarcity = std::clamp(snowDepthU / 36.0f, 0.0f, 1.0f);
    const bool lateNight = (gameHour < 6.0f || gameHour >= 21.0f);
    const float crowdRelief = std::clamp(float(nearbyActors) / 24.0f, 0.0f, 0.30f);
    e.scarcityIndex = std::clamp(storm * 0.55f + snowScarcity * 0.35f + (lateNight ? 0.12f : 0.0f) - crowdRelief + (inInterior ? -0.05f : 0.0f), 0.0f, 1.0f);
    e.buyPriceMult  = std::clamp(0.92f + e.scarcityIndex * 0.42f, 0.85f, 1.45f);
    e.sellPriceMult = std::clamp(0.95f + e.scarcityIndex * 0.28f, 0.85f, 1.30f);
    if (e.scarcityIndex < 0.20f) e.marketTier = 0;
    else if (e.scarcityIndex < 0.48f) e.marketTier = 1;
    else if (e.scarcityIndex < 0.75f) e.marketTier = 2;
    else e.marketTier = 3;
    return e;
}

SaveHealthReport InspectSaveHealth(const char* savesDirPath) {
    SaveHealthReport r{};
    if (!savesDirPath || !*savesDirPath) return r;
    std::error_code ec;
    if (!std::filesystem::exists(savesDirPath, ec)) return r;
    uint64_t maxBytes = 0;
    for (const auto& ent : std::filesystem::directory_iterator(savesDirPath, ec)) {
        if (!ent.is_regular_file(ec)) continue;
        const auto ext = ent.path().extension().string();
        if (ext == ".ess" || ext == ".ESS") {
            ++r.totalSaves;
            const uint64_t sz = ent.file_size(ec);
            if (sz > maxBytes) maxBytes = sz;
        }
    }
    r.largestSaveMb = float(double(maxBytes) / (1024.0 * 1024.0));
    r.bloatWarning = (r.largestSaveMb >= 18.0f);
    return r;
}

BoruvkaMstResult ComputeBoruvkaMst(uint32_t nodeCount, const std::vector<MstEdge>& edges) {
    BoruvkaMstResult res{};
    if (nodeCount == 0u) return res;
    std::vector<uint32_t> parent(nodeCount);
    for (uint32_t i = 0; i < nodeCount; ++i) parent[i] = i;
    auto findRoot = [&](uint32_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    uint32_t comps = nodeCount;
    std::vector<int32_t> cheapest(nodeCount, -1);
    while (comps > 1u) {
        std::fill(cheapest.begin(), cheapest.end(), -1);
        for (int32_t ei = 0; ei < static_cast<int32_t>(edges.size()); ++ei) {
            const auto& e = edges[size_t(ei)];
            if (e.u >= nodeCount || e.v >= nodeCount) continue;
            const uint32_t ru = findRoot(e.u);
            const uint32_t rv = findRoot(e.v);
            if (ru == rv) continue;
            if (cheapest[ru] < 0 || e.weight < edges[size_t(cheapest[ru])].weight) cheapest[ru] = ei;
            if (cheapest[rv] < 0 || e.weight < edges[size_t(cheapest[rv])].weight) cheapest[rv] = ei;
        }
        bool mergedAny = false;
        for (uint32_t i = 0; i < nodeCount; ++i) {
            const int32_t ei = cheapest[i];
            if (ei < 0) continue;
            const auto& e = edges[size_t(ei)];
            const uint32_t ru = findRoot(e.u);
            const uint32_t rv = findRoot(e.v);
            if (ru == rv) continue;
            parent[rv] = ru;
            res.treeEdges.push_back(e);
            res.totalWeight += e.weight;
            --comps;
            mergedAny = true;
        }
        ++res.rounds;
        if (!mergedAny) break;
    }
    res.components = comps;
    return res;
}

int LegendreSymbol(int a, int oddPrimeP) {
    if (oddPrimeP <= 2 || (oddPrimeP & 1) == 0) return 0;
    int m = a % oddPrimeP;
    if (m < 0) m += oddPrimeP;
    if (m == 0) return 0;
    int64_t res = 1;
    int64_t base = m;
    int exp = (oddPrimeP - 1) >> 1;
    while (exp > 0) {
        if (exp & 1) res = (res * base) % oddPrimeP;
        base = (base * base) % oddPrimeP;
        exp >>= 1;
    }
    return (res == 1) ? 1 : -1;
}

void EvaluateLoopCdContrast(const float passEarly[3], const float passRefined[3], float omega, float outRgb[3]) {
    if (!passEarly || !passRefined || !outRgb) return;
    const float w = std::clamp(omega, 0.0f, 0.50f);
    for (int i = 0; i < 3; ++i) {
        const float extrap = passRefined[i] + (passRefined[i] - passEarly[i]) * w;
        const float lo = std::max(0.0f, std::min(passEarly[i], passRefined[i]) * 0.85f);
        const float hi = std::max(passEarly[i], passRefined[i]) * 1.15f + 0.02f;
        outRgb[i] = std::clamp(extrap, lo, hi);
    }
}

float EvaluateTrappingSeparator(float curVal, float prevVal, float sigma, float speedPx, float edgeGate) {
    const float safeSig = std::max(sigma, 0.035f);
    const float zDist = std::fabs(curVal - prevVal) / safeSig;
    const float t = std::clamp((zDist - 1.65f) / (3.40f - 1.65f), 0.0f, 1.0f);
    const float barrier = t * t * (3.0f - 2.0f * t);
    const float motionGate = std::clamp(speedPx * 0.085f + (1.0f - std::clamp(edgeGate, 0.0f, 1.0f)) * 0.72f, 0.0f, 1.0f);
    return std::clamp(barrier * (0.45f + 0.55f * motionGate), 0.0f, 1.0f);
}

std::string ResolveSkyrimSavesDir(const char* docsSkyrimDir) {
    if (!docsSkyrimDir || !*docsSkyrimDir) return {};
    const std::filesystem::path baseDir(docsSkyrimDir);
    const std::filesystem::path iniPath = baseDir / "Skyrim.ini";
    std::string relPath = "Saves";
    std::ifstream in(iniPath);
    if (in) {
        std::string line;
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            size_t kStart = 0;
            while (kStart < key.size() && (key[kStart] == ' ' || key[kStart] == '\t')) ++kStart;
            key = key.substr(kStart);
            if (key == "sLocalSavePath" || key == "SLocalSavePath") {
                std::string val = line.substr(eq + 1);
                while (!val.empty() && (val.back() == '\r' || val.back() == '\n' || val.back() == ' ' || val.back() == '\t' || val.back() == '\\' || val.back() == '/')) {
                    val.pop_back();
                }
                size_t vStart = 0;
                while (vStart < val.size() && (val[vStart] == ' ' || val[vStart] == '\t')) ++vStart;
                val = val.substr(vStart);
                if (!val.empty()) relPath = val;
                break;
            }
        }
    }
    std::filesystem::path cand(relPath);
    if (cand.is_relative()) cand = baseDir / cand;
    std::error_code ec;
    if (std::filesystem::exists(cand, ec)) return cand.string();
    return (baseDir / "Saves").string();
}

DynamicGameplayGmsts ComputeDynamicGameplayGmsts(float economyBuyMult, float staminaRegenMult) {
    DynamicGameplayGmsts g{};
    const float econ = std::clamp(economyBuyMult, 0.85f, 1.45f);
    const float stam = std::clamp(staminaRegenMult, 0.70f, 1.30f);
    g.barterMin = std::clamp(1.35f * econ, 1.15f, 1.85f);
    g.barterMax = std::clamp(3.60f * (0.65f + 0.35f * econ), 3.10f, 4.40f);
    // Binary-verified 0x4214C0: Skyrim sprint stamina drain baseline is 15.0/sec; scale cleanly around 14.5/sec in [12, 18].
    g.sprintStaminaDrainMult = std::clamp(14.5f / stam, 12.0f, 18.0f);
    return g;
}

float StepExtremumSeekingGovernor(ExtremumSeekingState& st, float measuredCost, float dtSec,
                                  float xMin, float xMax, float ditherAmp, float omegaRadPerSec, float gain) {
    const float lo = std::min(xMin, xMax);
    const float hi = std::max(xMin, xMax);
    const float dt = std::clamp(dtSec, 1.0e-4f, 0.05f);
    const float amp = std::clamp(ditherAmp, 1.0e-4f, std::max((hi - lo) * 0.25f, 1.0e-4f));
    const float w = std::clamp(omegaRadPerSec, 1.0f, 80.0f);
    const float k = std::clamp(gain, 0.01f, 5.0f);
    st.xHat = std::clamp(st.xHat, lo, hi);
    if (!std::isfinite(measuredCost)) {
        return st.xHat;
    }
    const float safeCost = std::clamp(measuredCost, -1.0e4f, 1.0e4f);
    if (!st.init) {
        st.costLp = safeCost;
        st.init = true;
    }
    // Washout high-pass filter isolates oscillatory sensitivity d(cost)/dx * amp * sin(phase):
    const float lpAlpha = std::clamp(1.0f - std::exp(-(w * 0.35f) * dt), 0.01f, 0.65f);
    const float hpCost = safeCost - st.costLp;
    st.costLp += lpAlpha * hpCost;
    const float demod = hpCost * std::sin(st.phaseRad);
    // Gradient descent on unknown live cost surface:
    const float step = std::clamp(-k * demod * dt / amp, -(hi - lo) * 0.08f, (hi - lo) * 0.08f);
    st.xHat = std::clamp(st.xHat + step, lo, hi);
    st.phaseRad = std::fmod(st.phaseRad + w * dt, 6.28318530718f);
    return std::clamp(st.xHat + amp * std::sin(st.phaseRad), lo, hi);
}

BodyDamageResult EvaluateBodyDamageAndInjury(const BodyDamageInput& in) {
    BodyDamageResult r{};
    const float mass = std::clamp(in.weaponMassKg, 0.2f, 25.0f);
    const float spd  = std::clamp(in.swingSpeedMs, 0.5f, 35.0f);
    const float sharp = std::clamp(in.edgeSharpness, 0.0f, 1.0f);
    const float momentum = mass * spd;
    // Blunt impact penetrates heavy plate via kinetic shock; edged blades cleave light/unarmored tissue:
    const float effArmor = std::max(0.0f, in.armorRating) * (0.45f + 0.55f * sharp);
    const float armorMit = std::clamp(effArmor / (effArmor + 140.0f), 0.0f, 0.80f);
    float zoneMult = 1.0f;
    if (in.zone == BodyZone::Head) zoneMult = 2.15f;
    else if (in.zone == BodyZone::LeftArm || in.zone == BodyZone::RightArm) zoneMult = 0.82f;
    else if (in.zone == BodyZone::LeftLeg || in.zone == BodyZone::RightLeg) zoneMult = 0.88f;

    const float kineticScale = std::clamp(0.70f + momentum * 0.022f, 0.70f, 2.20f);
    r.finalDamage = std::max(0.0f, in.rawDamage) * zoneMult * kineticScale * (1.0f - armorMit);
    const float hpMax = std::max(in.targetHpMax, 1.0f);
    const float dmgFrac = r.finalDamage / hpMax;
    const float postHp = in.targetHpNow - r.finalDamage;

    r.bleedDps = sharp * r.finalDamage * (in.zone == BodyZone::Head ? 0.22f : 0.14f) * (1.0f - armorMit * 0.6f);
    r.staggerSec = std::clamp((momentum * 0.018f + dmgFrac * 1.4f) * (in.zone == BodyZone::Head ? 1.45f : 1.0f), 0.0f, 2.2f);
    r.limbCrippled = (dmgFrac >= 0.28f) && (in.zone != BodyZone::Torso);
    if (r.limbCrippled) {
        if (in.zone == BodyZone::LeftLeg || in.zone == BodyZone::RightLeg) r.moveSpeedMult = 0.62f;
        if (in.zone == BodyZone::LeftArm || in.zone == BodyZone::RightArm) r.attackSpeedMult = 0.70f;
    }
    // Physical lethal-threshold severing: requires sharp blade + sufficient cleave energy + lethal blow:
    if (postHp <= 0.0f && sharp >= 0.55f && r.finalDamage >= hpMax * 0.35f) {
        if (in.zone == BodyZone::Head) {
            r.decapitated = true;
            r.dismembered = true;
        } else if (in.zone != BodyZone::Torso) {
            r.dismembered = true;
        }
    }
    return r;
}

void InitVoxelProp(RawVoxelDestructible& prop, VoxelMaterial mat, bool hollowBox) {
    prop.material = mat;
    prop.solidCount = 0;
    const uint8_t strength = (mat == VoxelMaterial::Pottery) ? 65u
                           : (mat == VoxelMaterial::Ice)     ? 110u
                           : (mat == VoxelMaterial::Wood)    ? 145u : 235u;
    for (int z = 0; z < 8; ++z) {
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const bool wall = !hollowBox || x == 0 || x == 7 || y == 0 || y == 7 || z == 0 || z == 7;
                const uint8_t v = wall ? strength : 0u;
                prop.cells[(z * 8 + y) * 8 + x] = v;
                if (v > 0u) ++prop.solidCount;
            }
        }
    }
}

std::vector<VoxelFragment> ApplyVoxelImpact(RawVoxelDestructible& prop, const float localHit[3],
                                            const float impactDir[3], float impactForce, float blastRadiusCells) {
    std::vector<VoxelFragment> frags;
    if (!localHit || !impactDir || prop.solidCount == 0u || impactForce <= 0.0f) return frags;
    const float rad = std::clamp(blastRadiusCells, 0.5f, 8.0f);
    const float dirLen = std::sqrt(impactDir[0] * impactDir[0] + impactDir[1] * impactDir[1] + impactDir[2] * impactDir[2]);
    const float dxN = dirLen > 1e-4f ? impactDir[0] / dirLen : 0.0f;
    const float dyN = dirLen > 1e-4f ? impactDir[1] / dirLen : 1.0f;
    const float dzN = dirLen > 1e-4f ? impactDir[2] / dirLen : 0.0f;

    // 1) Carve impact fracture cavity using material tensile strength + directional stress cone:
    uint32_t shattered = 0;
    float shCenter[3] = {};
    for (int z = 0; z < 8; ++z) {
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                uint8_t& c = prop.cells[(z * 8 + y) * 8 + x];
                if (c == 0u) continue;
                const float vx = float(x) + 0.5f - localHit[0];
                const float vy = float(y) + 0.5f - localHit[1];
                const float vz = float(z) + 0.5f - localHit[2];
                const float dist = std::sqrt(vx * vx + vy * vy + vz * vz);
                if (dist > rad) continue;
                const float falloff = 1.0f - dist / rad;
                const float dmg = impactForce * (0.45f + 0.55f * falloff);
                if (dmg >= float(c)) {
                    c = 0u;
                    --prop.solidCount;
                    ++shattered;
                    shCenter[0] += float(x) + 0.5f;
                    shCenter[1] += float(y) + 0.5f;
                    shCenter[2] += float(z) + 0.5f;
                } else {
                    c = static_cast<uint8_t>(std::max(1.0f, float(c) - dmg));
                }
            }
        }
    }
    if (shattered > 0u) {
        VoxelFragment dust{};
        dust.voxelCount = shattered;
        dust.centerLocal[0] = shCenter[0] / float(shattered);
        dust.centerLocal[1] = shCenter[1] / float(shattered);
        dust.centerLocal[2] = shCenter[2] / float(shattered);
        const float vMag = std::clamp(impactForce * 0.06f, 2.0f, 35.0f);
        dust.impulseVel[0] = dxN * vMag;
        dust.impulseVel[1] = dyN * vMag;
        dust.impulseVel[2] = dzN * vMag + 3.5f;
        frags.push_back(dust);
    }

    // 2) 6-connected flood-fill to detect structurally detached islands (largest component stays anchored; smaller islands break free!):
    int32_t compId[512];
    std::fill(std::begin(compId), std::end(compId), -1);
    struct CompInfo { uint32_t count = 0; float sum[3] = {}; };
    std::vector<CompInfo> comps;
    int stack[512];
    static constexpr int kOff[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (int idx = 0; idx < 512; ++idx) {
        if (prop.cells[idx] == 0u || compId[idx] >= 0) continue;
        const int32_t cid = static_cast<int32_t>(comps.size());
        CompInfo ci{};
        int sp = 0;
        stack[sp++] = idx;
        compId[idx] = cid;
        while (sp > 0) {
            const int cur = stack[--sp];
            const int cz = cur >> 6, cy = (cur >> 3) & 7, cx = cur & 7;
            ++ci.count;
            ci.sum[0] += float(cx) + 0.5f;
            ci.sum[1] += float(cy) + 0.5f;
            ci.sum[2] += float(cz) + 0.5f;
            for (const auto& o : kOff) {
                const int nx = cx + o[0], ny = cy + o[1], nz = cz + o[2];
                if (nx < 0 || nx > 7 || ny < 0 || ny > 7 || nz < 0 || nz > 7) continue;
                const int nIdx = (nz << 6) | (ny << 3) | nx;
                if (prop.cells[nIdx] > 0u && compId[nIdx] < 0) {
                    compId[nIdx] = cid;
                    stack[sp++] = nIdx;
                }
            }
        }
        comps.push_back(ci);
    }
    if (comps.size() > 1u) {
        int32_t anchor = 0;
        for (int32_t c = 1; c < static_cast<int32_t>(comps.size()); ++c) {
            if (comps[size_t(c)].count > comps[size_t(anchor)].count) anchor = c;
        }
        for (int idx = 0; idx < 512; ++idx) {
            if (compId[idx] >= 0 && compId[idx] != anchor) {
                prop.cells[idx] = 0u;
                if (prop.solidCount > 0u) --prop.solidCount;
            }
        }
        for (int32_t c = 0; c < static_cast<int32_t>(comps.size()); ++c) {
            if (c == anchor) continue;
            const auto& ci = comps[size_t(c)];
            VoxelFragment island{};
            island.voxelCount = ci.count;
            island.centerLocal[0] = ci.sum[0] / float(ci.count);
            island.centerLocal[1] = ci.sum[1] / float(ci.count);
            island.centerLocal[2] = ci.sum[2] / float(ci.count);
            const float push = std::clamp(impactForce * 0.045f, 1.5f, 22.0f);
            island.impulseVel[0] = dxN * push + (island.centerLocal[0] - localHit[0]) * 1.2f;
            island.impulseVel[1] = dyN * push + (island.centerLocal[1] - localHit[1]) * 1.2f;
            island.impulseVel[2] = dzN * push + 2.5f;
            frags.push_back(island);
        }
    }
    return frags;
}

SynopticWeatherState StepSynopticWeatherSim(const float worldPos[3], float simTimeSec, float gameHour) {
    SynopticWeatherState w{};
    const float px = worldPos ? worldPos[0] : 0.0f;
    const float py = worldPos ? worldPos[1] : 0.0f;
    const float pz = worldPos ? worldPos[2] : 0.0f;
    // Continuous multi-scale synoptic pressure wave advecting across Skyrim's world coordinates:
    const float advX = px * 0.000014f - simTimeSec * 0.0018f;
    const float advY = py * 0.000011f + simTimeSec * 0.0012f;
    const float baro = std::sin(advX + advY) * 0.50f + std::cos(advX * 2.13f - advY * 1.67f + 1.3f) * 0.32f
                     + std::sin(simTimeSec * 0.0041f + 2.7f) * 0.18f;
    w.pressureHpa = std::clamp(1006.0f + baro * 26.0f, 972.0f, 1034.0f);
    // Low pressure trough (< 1002 hPa) generates deep fronts, gusting wind, precipitation & convective CAPE lightning:
    const float trough = std::clamp((1008.0f - w.pressureHpa) / 32.0f, 0.0f, 1.0f);
    w.cloudCover01 = std::clamp(0.18f + trough * 0.78f, 0.15f, 0.98f);
    const float northCold = std::clamp((py - 12000.0f) / 38000.0f, 0.0f, 1.0f);
    const float diurnalCool = (gameHour < 6.0f || gameHour > 20.0f) ? 450.0f : 0.0f;
    w.snowLineAltU = std::clamp(3200.0f - northCold * 2800.0f - trough * 650.0f - diurnalCool, 0.0f, 4500.0f);
    const float precip = std::clamp((trough - 0.38f) / 0.55f, 0.0f, 1.0f);
    const float freezeFrac = std::clamp((pz - (w.snowLineAltU - 450.0f)) / 900.0f, 0.0f, 1.0f);
    w.snowRate01 = precip * freezeFrac;
    w.rainRate01 = precip * (1.0f - freezeFrac);
    w.windSpeedMs = std::clamp(3.2f + trough * 22.0f + std::clamp(pz / 12000.0f, 0.0f, 1.0f) * 8.0f, 1.5f, 34.0f);
    w.stormLightning = std::clamp((trough - 0.65f) / 0.30f, 0.0f, 1.0f) * (1.0f - freezeFrac * 0.4f);
    return w;
}

CreatureGaitOutput EvaluateCreatureGaitKinematics(CreatureGaitKind kind, float moveSpeedU, float turnRateRadPerSec,
                                                  float terrainSlopeRad, float simTimeSec) {
    CreatureGaitOutput g{};
    const float spd = std::clamp(moveSpeedU, 0.0f, 1200.0f);
    const float turn = std::clamp(turnRateRadPerSec, -6.0f, 6.0f);
    const float slope = std::clamp(terrainSlopeRad, -0.75f, 0.75f);
    const float kTwoPi = 6.28318530718f;

    float baseStrideU = 135.0f;
    float maxBank = 0.24f;
    float bobAmp = 2.8f;
    if (kind == CreatureGaitKind::Quadruped) { baseStrideU = 175.0f; maxBank = 0.34f; bobAmp = 3.8f; }
    else if (kind == CreatureGaitKind::Mammoth) { baseStrideU = 260.0f; maxBank = 0.12f; bobAmp = 4.6f; }
    else if (kind == CreatureGaitKind::Dragon) { baseStrideU = 340.0f; maxBank = 0.58f; bobAmp = 9.5f; }

    g.cadenceHz = (kind == CreatureGaitKind::Dragon)
        ? std::clamp(0.45f + spd / 900.0f, 0.45f, 1.85f)
        : (spd > 5.0f ? std::clamp(spd / baseStrideU, 0.35f, 3.8f) : 0.0f);
    const float basePh = std::fmod(std::max(simTimeSec, 0.0f) * g.cadenceHz * kTwoPi, kTwoPi);
    if (kind == CreatureGaitKind::Biped) {
        g.stridePhaseRad[0] = basePh;
        g.stridePhaseRad[1] = std::fmod(basePh + 3.14159265f, kTwoPi);
        g.stridePhaseRad[2] = g.stridePhaseRad[1];
        g.stridePhaseRad[3] = basePh;
    } else if (kind == CreatureGaitKind::Quadruped) {
        // Trot-to-gallop phase transition above 320u/s:
        const bool gallop = (spd > 320.0f);
        g.stridePhaseRad[0] = basePh;
        g.stridePhaseRad[1] = std::fmod(basePh + (gallop ? 0.65f : 3.14159265f), kTwoPi);
        g.stridePhaseRad[2] = std::fmod(basePh + (gallop ? 3.14159265f : 3.14159265f), kTwoPi);
        g.stridePhaseRad[3] = std::fmod(basePh + (gallop ? 3.80f : 0.0f), kTwoPi);
    } else if (kind == CreatureGaitKind::Mammoth) {
        // Heavy 4-beat amble (always 3 feet on ground for massive weight feel):
        g.stridePhaseRad[0] = basePh;
        g.stridePhaseRad[1] = std::fmod(basePh + 1.5707963f, kTwoPi);
        g.stridePhaseRad[2] = std::fmod(basePh + 3.14159265f, kTwoPi);
        g.stridePhaseRad[3] = std::fmod(basePh + 4.71238898f, kTwoPi);
    } else {
        // Dragon: synchronized wings [0,1] + counter-phase tail/hind balance [2,3]:
        g.stridePhaseRad[0] = basePh;
        g.stridePhaseRad[1] = basePh;
        g.stridePhaseRad[2] = std::fmod(basePh + 2.3561945f, kTwoPi);
        g.stridePhaseRad[3] = std::fmod(basePh + 3.9269908f, kTwoPi);
    }
    const float spdNorm = std::clamp(spd / 420.0f, 0.0f, 1.0f);
    g.bodyBankRollRad = std::clamp(turn * spdNorm * 0.18f, -maxBank, maxBank);
    g.spinePitchRad   = std::clamp(slope * 0.65f + (spd > 280.0f ? 0.11f : 0.0f), -0.55f, 0.55f);
    g.verticalBobU    = (g.cadenceHz > 0.0f) ? (std::sin(basePh * 2.0f) * bobAmp * std::max(spdNorm, 0.25f)) : 0.0f;
    return g;
}

PlanetaryPhysicsState EvaluatePlanetaryPhysics(float daysPassed, float gameHour, const float worldPos[3],
                                               float windSpeedMs, float pressureHpa) {
    PlanetaryPhysicsState p{};
    constexpr double kTwoPi = 6.283185307179586;
    constexpr double kPlanetRadiusM = 6360000.0;
    constexpr double kUnitsPerMeter = 70.0;
    const double altU = worldPos ? std::max(double(worldPos[2]), -20000.0) : 0.0;
    const double altM = std::max(altU / kUnitsPerMeter, 0.0);
    // Exact inverse-square altitude lapse g(h) = g0 * (R / (R + h))^2:
    const double ratio = kPlanetRadiusM / (kPlanetRadiusM + altM);
    p.gravityMps2    = static_cast<float>(9.80665 * ratio * ratio);
    p.gravityUnitsS2 = static_cast<float>(double(p.gravityMps2) * kUnitsPerMeter);

    const double safeDays = std::isfinite(daysPassed) ? std::max(double(daysPassed), 0.0) : 0.0;
    const double safeHour = std::isfinite(gameHour) ? std::clamp(double(gameHour), 0.0, 24.0) : 12.0;
    const double totalHours = safeDays * 24.0 + safeHour;

    // Harmonic equilibrium tide from Nirn's twin moons + Sun (Masser M2, Secunda N2, Magnus S2):
    const double wM2 = kTwoPi / 12.4206;
    const double wN2 = kTwoPi / 12.6583;
    const double wS2 = kTwoPi / 12.0000;
    const double phM2 = totalHours * wM2;
    const double phN2 = totalHours * wN2 + 0.72;
    const double phS2 = (safeHour - 12.0) * wS2;

    constexpr double aM2 = 16.5;
    constexpr double aN2 = 6.2;
    constexpr double aS2 = 7.4;
    const double tideAstroU = aM2 * std::cos(phM2) + aN2 * std::cos(phN2) + aS2 * std::cos(phS2);
    const double dTideUPerHr = -(aM2 * wM2 * std::sin(phM2) + aN2 * wN2 * std::sin(phN2) + aS2 * wS2 * std::sin(phS2));
    // Inverse barometer effect: 1 hPa drop raises sea surface by ~1 cm (0.70 Skyrim units):
    const double safeHpa = std::isfinite(pressureHpa) ? std::clamp(double(pressureHpa), 950.0, 1050.0) : 1013.25;
    const double baroSurgeU = (1013.25 - safeHpa) * 0.70;

    p.tideHeightU    = static_cast<float>(std::clamp(tideAstroU + baroSurgeU, -28.0, 38.0));
    p.tideVelocityMs = static_cast<float>(std::clamp(dTideUPerHr * 0.042, -1.65, 1.65));

    const double alignMS = 0.5 + 0.5 * std::cos(phM2 - phS2);
    const double alignMN = 0.5 + 0.5 * std::cos(phM2 - phN2);
    p.springTide01 = static_cast<float>(std::clamp(alignMS * 0.65 + alignMN * 0.35, 0.0, 1.0));

    const double wMs = std::max(double(windSpeedMs), 0.2);
    const double bWind = std::pow(wMs / 0.836, 2.0 / 3.0);
    const double bCyclone = std::max(1010.0 - safeHpa, 0.0) * 0.085;
    p.beaufortScale = static_cast<float>(std::clamp(bWind + bCyclone, 0.5, 12.0));

    const double swellAng = -1.18 + 0.35 * std::sin(totalHours * 0.045);
    p.swellDirX = static_cast<float>(std::cos(swellAng));
    p.swellDirY = static_cast<float>(std::sin(swellAng));
    return p;
}

// =================================================================================================
// Wave 39 Pure Deterministic Gameplay Resolvers (compiled in raw_engine.dll AND ce_runtime_check)
// =================================================================================================

const DifficultyTierPair* NonSpongyDifficultyTable(size_t* outCount) {
    static constexpr DifficultyTierPair kTiers[6] = {
        {"Novice",     0.50f, 1.50f},
        {"Apprentice", 0.75f, 1.25f},
        {"Adept",      1.00f, 1.00f},
        {"Expert",     1.35f, 1.15f},
        {"Master",     1.75f, 1.35f},
        {"Legendary",  2.25f, 1.60f},
    };
    if (outCount) *outCount = 6;
    return kTiers;
}

LiveHitOutcome EvaluateLiveHitModifier(const LiveHitContext& ctx) {
    LiveHitOutcome out{};
    const float baseDmg = std::isfinite(ctx.totalDamage) ? std::max(ctx.totalDamage, 0.0f) : 0.0f;
    const float basePhys = std::isfinite(ctx.physicalDamage) ? std::max(ctx.physicalDamage, 0.0f) : baseDmg;
    const float baseBlk = std::isfinite(ctx.percentBlocked) ? std::clamp(ctx.percentBlocked, 0.0f, 1.0f) : 0.0f;

    out.outTotalDamage    = baseDmg;
    out.outPhysicalDamage = basePhys;
    out.outPercentBlocked = baseBlk;

    const bool blocked = (ctx.flags & 1u) != 0u;

    // 1. Timed Directional Parry / Deflect (220 ms precision guard window):
    if (blocked && ctx.blockElapsedMs >= 0.0f && ctx.blockElapsedMs <= 220.0f && ctx.defenderStamina > 5.0f) {
        out.timedParry              = true;
        out.outPercentBlocked       = 1.0f;
        out.outTotalDamage          = 0.0f;
        out.outPhysicalDamage       = 0.0f;
        out.outStaminaRestoreDef    = 18.0f;
        out.outAggressorPoiseBroken = true;
        out.outDefenderPoiseDelta   = +10.0f;
        return out;
    }

    // 2. Guard Break on heavy/power attack against exhausted defender stamina:
    if (blocked && ctx.isPowerAttack && ctx.defenderStamina <= 12.0f) {
        out.guardBroken       = true;
        out.outPercentBlocked = std::clamp(baseBlk * 0.35f, 0.0f, 0.45f);
        const float origPass  = std::max(1.0f - baseBlk, 0.15f);
        const float newPass   = 1.0f - out.outPercentBlocked;
        const float ratio     = std::clamp(newPass / origPass, 1.0f, 2.5f);
        out.outTotalDamage    = baseDmg * ratio;
        out.outPhysicalDamage = basePhys * ratio;
        out.outDefenderPoiseDelta = -45.0f;
    } else {
        out.outDefenderPoiseDelta = blocked ? -(ctx.isPowerAttack ? 22.0f : 10.0f)
                                            : -(ctx.isPowerAttack ? 38.0f : 18.0f);
    }

    // 3. Posture-Broken Counter-Hit Lethal Multiplier (1.45x):
    if (ctx.targetPoiseBroken && out.outTotalDamage > 0.0f) {
        out.counterHit        = true;
        out.outTotalDamage    *= 1.45f;
        out.outPhysicalDamage *= 1.45f;
    }
    return out;
}

float EvaluateTacticalAiAttackChance(const TacticalAiChanceInput& in) {
    float c = std::isfinite(in.baseChance) ? std::clamp(in.baseChance, 0.0f, 1.0f) : 0.35f;
    const float hp = std::clamp(in.hpFrac, 0.0f, 1.0f);
    const float st = std::clamp(in.staminaFrac, 0.0f, 1.0f);
    // Eliminate 2011 passive wait-in-line AI: active squad token & flankers press decisively.
    if (in.hasAttackToken) {
        c = std::max(c * 1.35f, 0.42f);
    }
    if (in.role == TacticalRole::FlankerLeft || in.role == TacticalRole::FlankerRight) {
        c *= 1.20f;
    } else if (in.role == TacticalRole::SupportCaster) {
        c *= 0.85f;
    }
    if (hp < 0.25f || st < 0.15f) {
        c *= 0.60f;
    }
    if (in.selfPoisePressured) {
        c *= 0.72f;
    }
    return std::clamp(c, 0.04f, 0.96f);
}

float EvaluateTacticalAiBlockChance(const TacticalAiChanceInput& in) {
    float c = std::isfinite(in.baseChance) ? std::clamp(in.baseChance, 0.0f, 1.0f) : 0.25f;
    const float hp = std::clamp(in.hpFrac, 0.0f, 1.0f);
    const float st = std::clamp(in.staminaFrac, 0.0f, 1.0f);
    if (hp < 0.45f) c += 0.24f;
    if (in.selfPoisePressured) c += 0.22f;
    if (in.targetPowerAttacking && st > 0.25f) c += 0.18f;
    if (st < 0.12f) c *= 0.45f;   // don't turtle into guard-break when exhausted
    return std::clamp(c, 0.0f, 0.92f);
}

float EvaluateTacticalAiBashChance(const TacticalAiChanceInput& in) {
    float c = std::isfinite(in.baseChance) ? std::clamp(in.baseChance, 0.0f, 1.0f) : 0.15f;
    const float st = std::clamp(in.staminaFrac, 0.0f, 1.0f);
    if (st < 0.20f) return std::clamp(c * 0.35f, 0.0f, 0.25f);
    if (in.targetPowerAttacking) c += 0.35f;   // interrupt enemy windup!
    if (in.targetBlocking)       c += 0.25f;   // crack enemy turtle guard!
    return std::clamp(c, 0.0f, 0.88f);
}

BarterEconomyGmsts EvaluateRegionalBarterEconomy(const BarterEconomyInput& in) {
    BarterEconomyGmsts out{};
    const int month = std::clamp(static_cast<int>(std::floor(in.gameMonth0_11)), 0, 11);
    const float storm = std::clamp(in.storm01, 0.0f, 1.0f);
    const float snow  = std::clamp(in.snowDepthU / 32.0f, 0.0f, 1.0f);
    const float northHold = std::clamp((in.worldY - 12000.0f) / 36000.0f, 0.0f, 1.0f);

    // Tamriel calendar seasonal supply cycle:
    //   Months 9..11, 0..1 (Frostfall..Sun's Dawn) = winter scarcity (+0.15..+0.28)
    //   Months 7..8 (Last Seed / Hearthfire) = harvest surplus (-0.10)
    float seasonBias = 0.0f;
    if (month == 10 || month == 11 || month == 0)      seasonBias = +0.24f;
    else if (month == 9 || month == 1)                 seasonBias = +0.14f;
    else if (month == 7 || month == 8)                 seasonBias = -0.10f;
    else if (month == 5 || month == 6)                 seasonBias = -0.04f;

    const float tradeSat = std::min(float(in.recentTradeCount) * 0.015f, 0.12f);
    const float scarcity = std::clamp(0.12f + seasonBias + storm * 0.26f + snow * 0.16f + northHold * 0.14f - tradeSat * 0.5f,
                                      0.0f, 1.0f);
    out.scarcity01    = scarcity;
    out.seasonalMult  = std::clamp(1.0f + seasonBias + storm * 0.18f + northHold * 0.10f, 0.85f, 1.48f);
    // Scarcity raises merchant buy prices (fBarterMin/Max up) and slightly lowers sell return when flooding market:
    const float priceScale = std::clamp(0.92f + scarcity * 0.44f, 0.88f, 1.40f);
    out.barterMin     = std::clamp(1.35f * priceScale, 1.15f, 1.95f);
    out.barterMax     = std::clamp(3.30f * (0.72f + 0.28f * priceScale), 2.85f, 4.35f);
    out.barterBuyMin  = std::clamp(1.05f + scarcity * 0.08f, 1.05f, 1.20f);
    out.barterSellMax = std::clamp(0.95f - tradeSat * 0.45f, 0.78f, 0.95f);
    return out;
}

#ifndef RAW_SIM_PURE_TEST
// =================================================================================================
// Wave 39 Live Creation Engine 1.7.104.0 Detours (Zydis-verified, Fail-Closed, SEH-guarded)
// =================================================================================================
namespace {

using HitDataPopulateFn = void (*)(uint8_t* hitData, uint8_t ignoreBlock);
using GetActorValueFn   = float (*)(void* avOwner, int32_t av);
using RestoreAvThunkFn  = void (*)(void* avOwner, int32_t mod, int32_t av, float val);
using AiAttackChanceFn  = float (*)(void* actor, void* target);
using AiBlockChanceFn   = float (*)(void* actor);
using AiBashChanceFn    = float (*)(void* actor, void* target);
using BarterProcMsgFn   = uint32_t (*)(void* menu, uint8_t* uiMsg);

struct ActorCombatRecord {
    uint64_t blockStartMs      = 0;
    uint64_t lastBlockedHitMs  = 0;
    uint64_t poiseBrokenUntil  = 0;
    float    poise             = 100.0f;
};

struct SavedBarterGmsts {
    bool   saved = false;
    float* pMin = nullptr;
    float* pMax = nullptr;
    float* pBuyMin = nullptr;
    float* pSellMax = nullptr;
    float  vMin = 1.4f, vMax = 2.4f, vBuyMin = 1.05f, vSellMax = 0.95f;
};

struct LiveGameplayRuntime {
    const uint8_t*     base = nullptr;
    uint32_t           rvaHandleEntries = 0;
    uint32_t           rvaPlayer = 0;
    GetActorValueFn    fnGetAv = nullptr;
    GetActorValueFn    fnGetPermAv = nullptr;
    RestoreAvThunkFn   fnRestoreAv = nullptr;

    HitDataPopulateFn  origHitPopulate = nullptr;
    AiAttackChanceFn   origAttackChance = nullptr;
    AiBlockChanceFn    origBlockChance = nullptr;
    AiBashChanceFn     origBashChance = nullptr;
    BarterProcMsgFn    origBarterProcMsg = nullptr;

    std::atomic<bool>  active{false};
    std::atomic<uint8_t>  hooksMask{0};
    std::atomic<uint32_t> hitsProcessed{0};
    std::atomic<uint32_t> timedParries{0};
    std::atomic<uint32_t> guardBreaks{0};
    std::atomic<uint32_t> counterHits{0};
    std::atomic<uint32_t> aiAttackEvals{0};
    std::atomic<uint32_t> aiBlockEvals{0};
    std::atomic<uint32_t> aiBashEvals{0};
    std::atomic<uint32_t> barterOpens{0};

    // Squad & weather snapshot published by WorldSimOnPresent:
    std::atomic<uint32_t> squadActors{0};
    std::atomic<uint8_t>  squadTokens{2};
    std::atomic<uint8_t>  squadAction{1};
    std::atomic<float>    storm01{0.0f};
    std::atomic<float>    snowDepthU{0.0f};
    std::atomic<float>    lastBarterMin{1.35f};
    std::atomic<float>    lastBarterMax{3.60f};

    std::mutex mu;
    std::unordered_map<uint32_t, ActorCombatRecord> records;
    SavedBarterGmsts barterSaved{};
    uint32_t recentTrades = 0;
} g_gp;

static uint64_t NowMonotonicMs() {
    LARGE_INTEGER f{}, c{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return f.QuadPart > 0 ? uint64_t((c.QuadPart * 1000ll) / f.QuadPart) : GetTickCount64();
}

// Pure POD SEH helper: validated BSPointerHandleManager::Entry (0x21A4280) lookup (bit26=inUse, bits20..25=age).
static uint8_t* SehResolveActorHandle(uint32_t handle) {
    if (!handle || !g_gp.base || !g_gp.rvaHandleEntries) return nullptr;
    __try {
        const uint32_t idx = handle & 0x000FFFFFu;
        if (idx == 0u || idx >= 0x00100000u) return nullptr;
        const uint8_t* entry = g_gp.base + g_gp.rvaHandleEntries + static_cast<size_t>(idx) * 0x10u;
        const uint32_t bits = *reinterpret_cast<const uint32_t*>(entry + 0x00);
        if ((bits & 0x04000000u) == 0u) return nullptr;                       // inUse bit 26
        if ((bits & 0x03F00000u) != (handle & 0x03F00000u)) return nullptr;   // age bits 20..25
        const uint8_t* refObj = *reinterpret_cast<const uint8_t* const*>(entry + 0x08);
        if (!refObj || reinterpret_cast<uintptr_t>(refObj) < 0x10000ull) return nullptr;
        auto* refr = const_cast<uint8_t*>(refObj - 0x20);
        // Verify FormType == 0x3E (Actor / kACHR = 62) at TESForm+0x1A:
        if (refr[0x1A] != 0x3Eu) return nullptr;
        return refr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static uint8_t* SehGetPlayerPtr() {
    if (!g_gp.base || !g_gp.rvaPlayer) return nullptr;
    __try {
        return *reinterpret_cast<uint8_t* const*>(g_gp.base + g_gp.rvaPlayer);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static bool SehReadActorAv(uint8_t* actor, int32_t av, float* outCur, float* outPerm) {
    if (!actor || !g_gp.fnGetAv) return false;
    __try {
        void* avOwner = actor + 0xB8;
        if (outCur)  *outCur  = g_gp.fnGetAv(avOwner, av);
        if (outPerm) *outPerm = g_gp.fnGetPermAv ? g_gp.fnGetPermAv(avOwner, av) : 100.0f;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SehRestoreActorStamina(uint8_t* actor, float amount) {
    if (!actor || !g_gp.fnRestoreAv || !(amount > 0.0f)) return;
    __try {
        // ActorValueOwner::RestoreActorValueThunk (0x6C51F0): (avOwner=actor+0xB8, mod=2 kDamage, av=26 kStamina, val=+amount)
        g_gp.fnRestoreAv(actor + 0xB8, 2, 26, amount);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

struct HitDataPod {
    uint32_t aggressorHandle;
    uint32_t targetHandle;
    float    totalDamage;
    float    physicalDamage;
    float    percentBlocked;
    uint32_t flags;
    uint8_t  flags2;
};

static bool SehReadHitData(const uint8_t* hitData, HitDataPod* out) {
    if (!hitData || !out) return false;
    __try {
        out->aggressorHandle = *reinterpret_cast<const uint32_t*>(hitData + 0x18);
        out->targetHandle    = *reinterpret_cast<const uint32_t*>(hitData + 0x1C);
        out->totalDamage     = *reinterpret_cast<const float*>(hitData + 0x50);
        out->physicalDamage  = *reinterpret_cast<const float*>(hitData + 0x54);
        out->percentBlocked  = *reinterpret_cast<const float*>(hitData + 0x5C);
        out->flags           = *reinterpret_cast<const uint32_t*>(hitData + 0x80);
        out->flags2          = *(hitData + 0x82);
        return std::isfinite(out->totalDamage) && std::isfinite(out->percentBlocked);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SehWriteHitData(uint8_t* hitData, float totalDmg, float physDmg, float pctBlocked) {
    if (!hitData) return;
    __try {
        *reinterpret_cast<float*>(hitData + 0x50) = totalDmg;
        *reinterpret_cast<float*>(hitData + 0x54) = physDmg;
        *reinterpret_cast<float*>(hitData + 0x5C) = pctBlocked;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static bool SehReadUiMsgType(const uint8_t* uiMsg, uint32_t* outType) {
    if (!uiMsg || !outType) return false;
    __try {
        *outType = *reinterpret_cast<const uint32_t*>(uiMsg + 0x08);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SehWriteFloatPtr(float* p, float v) {
    if (!p || !std::isfinite(v)) return;
    __try {
        *p = v;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static bool SehReadFloatPtr(const float* p, float* out) {
    if (!p || !out) return false;
    __try {
        *out = *p;
        return std::isfinite(*out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehIsActorAttacking(const uint8_t* actor) {
    if (!actor) return false;
    __try {
        // ActorState::attackState sits in bits 28..31 of actor+0xC8 (verified in 0x8F2783)
        const uint32_t st = (*reinterpret_cast<const uint32_t*>(actor + 0xC8)) >> 28;
        return st >= 1u && st <= 4u;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehIsActorBlocking(const uint8_t* actor) {
    if (!actor) return false;
    __try {
        // ActorState::weaponState / block bit at actor+0xCC (verified in 0x7F0CF7)
        const uint32_t st = *reinterpret_cast<const uint32_t*>(actor + 0xCC);
        return (st & 0x00000100u) != 0u;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void HookHitDataPopulate(uint8_t* hitData, uint8_t ignoreBlock) {
    if (g_gp.origHitPopulate) {
        g_gp.origHitPopulate(hitData, ignoreBlock);
    }
    if (!g_gp.active.load(std::memory_order_acquire) || !hitData) return;

    HitDataPod pod{};
    if (!SehReadHitData(hitData, &pod)) return;
    g_gp.hitsProcessed.fetch_add(1u, std::memory_order_relaxed);

    uint8_t* targetActor = SehResolveActorHandle(pod.targetHandle);
    uint8_t* playerPtr   = SehGetPlayerPtr();
    float defStam = 100.0f, defStamMax = 100.0f;
    if (targetActor) {
        SehReadActorAv(targetActor, 26 /*kStamina*/, &defStam, &defStamMax);
        defStamMax = std::max(defStamMax, 1.0f);
    }

    const uint64_t nowMs = NowMonotonicMs();
    float blockElapsedMs = 999.0f;
    float defPoise = 100.0f;
    bool targetPoiseBroken = false;
    {
        std::lock_guard<std::mutex> lk(g_gp.mu);
        if (g_gp.records.size() > 128u) g_gp.records.clear();
        auto& rec = g_gp.records[pod.targetHandle];
        if ((pod.flags & 1u) != 0u) {
            if (rec.blockStartMs == 0 || (nowMs - rec.lastBlockedHitMs) > 450ull) {
                rec.blockStartMs = nowMs;
            }
            blockElapsedMs = static_cast<float>(nowMs - rec.blockStartMs);
            rec.lastBlockedHitMs = nowMs;
        } else {
            rec.blockStartMs = 0;
        }
        defPoise = rec.poise;
        targetPoiseBroken = (rec.poiseBrokenUntil > nowMs);
    }

    LiveHitContext ctx{};
    ctx.totalDamage        = pod.totalDamage;
    ctx.physicalDamage     = pod.physicalDamage;
    ctx.percentBlocked     = pod.percentBlocked;
    ctx.flags              = pod.flags;
    ctx.isPowerAttack      = (pod.flags2 & 1u) != 0u;
    ctx.defenderStamina    = defStam;
    ctx.defenderStaminaMax = defStamMax;
    ctx.blockElapsedMs     = blockElapsedMs;
    ctx.defenderPoise      = defPoise;
    ctx.targetPoiseBroken  = targetPoiseBroken;
    ctx.isTargetPlayer     = (targetActor != nullptr && targetActor == playerPtr);

    const LiveHitOutcome res = EvaluateLiveHitModifier(ctx);

    {
        std::lock_guard<std::mutex> lk(g_gp.mu);
        auto& tRec = g_gp.records[pod.targetHandle];
        tRec.poise = std::clamp(tRec.poise + res.outDefenderPoiseDelta, 0.0f, 100.0f);
        if (res.guardBroken || tRec.poise <= 0.0f) {
            tRec.poiseBrokenUntil = nowMs + 2200ull;
            tRec.poise = 100.0f;
        } else if (res.counterHit) {
            tRec.poiseBrokenUntil = 0;
        }
        if (res.outAggressorPoiseBroken && pod.aggressorHandle != 0u) {
            auto& aRec = g_gp.records[pod.aggressorHandle];
            aRec.poiseBrokenUntil = nowMs + 2500ull;
            aRec.poise = 0.0f;
        }
    }

    if (res.timedParry) {
        g_gp.timedParries.fetch_add(1u, std::memory_order_relaxed);
        SehWriteHitData(hitData, res.outTotalDamage, res.outPhysicalDamage, res.outPercentBlocked);
        if (targetActor && res.outStaminaRestoreDef > 0.0f) {
            SehRestoreActorStamina(targetActor, res.outStaminaRestoreDef);
        }
    } else if (res.guardBroken || res.counterHit) {
        if (res.guardBroken) g_gp.guardBreaks.fetch_add(1u, std::memory_order_relaxed);
        if (res.counterHit)  g_gp.counterHits.fetch_add(1u, std::memory_order_relaxed);
        SehWriteHitData(hitData, res.outTotalDamage, res.outPhysicalDamage, res.outPercentBlocked);
    }
}

static float HookCombatAttackChance(void* actor, void* target) {
    const float base = g_gp.origAttackChance ? g_gp.origAttackChance(actor, target) : 0.35f;
    if (!g_gp.active.load(std::memory_order_acquire) || !actor) return base;
    g_gp.aiAttackEvals.fetch_add(1u, std::memory_order_relaxed);

    float hp = 100.0f, hpMax = 100.0f, stam = 100.0f, stamMax = 100.0f;
    SehReadActorAv(static_cast<uint8_t*>(actor), 24 /*kHealth*/, &hp, &hpMax);
    SehReadActorAv(static_cast<uint8_t*>(actor), 26 /*kStamina*/, &stam, &stamMax);

    TacticalAiChanceInput in{};
    in.baseChance     = base;
    in.hpFrac         = hp / std::max(hpMax, 1.0f);
    in.staminaFrac    = stam / std::max(stamMax, 1.0f);
    in.hasAttackToken = (g_gp.squadTokens.load(std::memory_order_relaxed) > 0u);
    const uintptr_t hash = (reinterpret_cast<uintptr_t>(actor) >> 4) % 3u;
    in.role = (hash == 0u) ? TacticalRole::Vanguard : (hash == 1u ? TacticalRole::FlankerLeft : TacticalRole::FlankerRight);
    in.selfPoisePressured = (g_gp.squadAction.load(std::memory_order_relaxed) == static_cast<uint8_t>(TacticalAction::GuardParry));
    return EvaluateTacticalAiAttackChance(in);
}

static float HookCombatBlockChance(void* actor) {
    const float base = g_gp.origBlockChance ? g_gp.origBlockChance(actor) : 0.25f;
    if (!g_gp.active.load(std::memory_order_acquire) || !actor) return base;
    g_gp.aiBlockEvals.fetch_add(1u, std::memory_order_relaxed);

    float hp = 100.0f, hpMax = 100.0f, stam = 100.0f, stamMax = 100.0f;
    SehReadActorAv(static_cast<uint8_t*>(actor), 24, &hp, &hpMax);
    SehReadActorAv(static_cast<uint8_t*>(actor), 26, &stam, &stamMax);

    TacticalAiChanceInput in{};
    in.baseChance         = base;
    in.hpFrac             = hp / std::max(hpMax, 1.0f);
    in.staminaFrac        = stam / std::max(stamMax, 1.0f);
    in.selfPoisePressured = (g_gp.squadAction.load(std::memory_order_relaxed) == static_cast<uint8_t>(TacticalAction::GuardParry));
    return EvaluateTacticalAiBlockChance(in);
}

static float HookCombatBashChance(void* actor, void* target) {
    const float base = g_gp.origBashChance ? g_gp.origBashChance(actor, target) : 0.15f;
    if (!g_gp.active.load(std::memory_order_acquire) || !actor) return base;
    g_gp.aiBashEvals.fetch_add(1u, std::memory_order_relaxed);

    float stam = 100.0f, stamMax = 100.0f;
    SehReadActorAv(static_cast<uint8_t*>(actor), 26, &stam, &stamMax);

    TacticalAiChanceInput in{};
    in.baseChance           = base;
    in.staminaFrac          = stam / std::max(stamMax, 1.0f);
    in.targetPowerAttacking = target ? SehIsActorAttacking(static_cast<const uint8_t*>(target)) : false;
    in.targetBlocking       = target ? SehIsActorBlocking(static_cast<const uint8_t*>(target)) : false;
    return EvaluateTacticalAiBashChance(in);
}

static uint32_t HookBarterProcessMessage(void* menu, uint8_t* uiMsg) {
    uint32_t msgType = 0;
    const bool gotType = g_gp.active.load(std::memory_order_acquire) && SehReadUiMsgType(uiMsg, &msgType);

    if (gotType && (msgType == 1u /*kShow*/ || msgType == 4u /*kUpdate*/)) {
        EngineView ev{};
        RAW_EngineView(&ev);
        float cal6[6] = {201.0f, 7.0f, 17.0f, 12.0f, 0.0f, 20.0f};
        RAW_GetCalendarState(cal6);

        BarterEconomyInput econIn{};
        econIn.gameMonth0_11 = cal6[1];
        econIn.gameDay1_31   = cal6[2];
        econIn.storm01       = g_gp.storm01.load(std::memory_order_relaxed);
        econIn.snowDepthU    = g_gp.snowDepthU.load(std::memory_order_relaxed);
        econIn.worldY        = (ev.ok & 2u) ? ev.playerPos[1] : 0.0f;
        {
            std::lock_guard<std::mutex> lk(g_gp.mu);
            econIn.recentTradeCount = g_gp.recentTrades;
            if (!g_gp.barterSaved.saved && g_gp.barterSaved.pMin) {
                SehReadFloatPtr(g_gp.barterSaved.pMin, &g_gp.barterSaved.vMin);
                SehReadFloatPtr(g_gp.barterSaved.pMax, &g_gp.barterSaved.vMax);
                SehReadFloatPtr(g_gp.barterSaved.pBuyMin, &g_gp.barterSaved.vBuyMin);
                SehReadFloatPtr(g_gp.barterSaved.pSellMax, &g_gp.barterSaved.vSellMax);
                g_gp.barterSaved.saved = true;
            }
        }
        const BarterEconomyGmsts gm = EvaluateRegionalBarterEconomy(econIn);
        SehWriteFloatPtr(g_gp.barterSaved.pMin, gm.barterMin);
        SehWriteFloatPtr(g_gp.barterSaved.pMax, gm.barterMax);
        SehWriteFloatPtr(g_gp.barterSaved.pBuyMin, gm.barterBuyMin);
        SehWriteFloatPtr(g_gp.barterSaved.pSellMax, gm.barterSellMax);
        g_gp.lastBarterMin.store(gm.barterMin, std::memory_order_relaxed);
        g_gp.lastBarterMax.store(gm.barterMax, std::memory_order_relaxed);
        if (msgType == 1u) g_gp.barterOpens.fetch_add(1u, std::memory_order_relaxed);
    }

    const uint32_t res = g_gp.origBarterProcMsg ? g_gp.origBarterProcMsg(menu, uiMsg) : 0u;

    if (gotType && msgType == 3u /*kHide*/) {
        std::lock_guard<std::mutex> lk(g_gp.mu);
        if (g_gp.barterSaved.saved) {
            SehWriteFloatPtr(g_gp.barterSaved.pMin, g_gp.barterSaved.vMin);
            SehWriteFloatPtr(g_gp.barterSaved.pMax, g_gp.barterSaved.vMax);
            SehWriteFloatPtr(g_gp.barterSaved.pBuyMin, g_gp.barterSaved.vBuyMin);
            SehWriteFloatPtr(g_gp.barterSaved.pSellMax, g_gp.barterSaved.vSellMax);
        }
        g_gp.recentTrades = std::min(g_gp.recentTrades + 1u, 20u);
    }
    return res;
}

}  // namespace

std::string GameplayHooksInit(const Image& img) {
    if (!AreBinaryOffsetsVerified()) {
        return "gameplay-hooks: REFUSED (fail-closed: startup Zydis binary offset verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "gameplay-hooks: skipped (non-CE1 or online profile)";
    }
    g_gp.base             = img.base;
    g_gp.rvaHandleEntries = Addr("BSPointerHandleManager::Entry::GetHandleEntries");
    g_gp.rvaPlayer        = Addr("PlayerCharacter::Singleton");

    const uint32_t rvaGetAv     = Addr("fn:ActorValueOwner::GetActorValue");
    const uint32_t rvaGetPermAv = Addr("fn:ActorValueOwner::GetPermanentActorValue");
    const uint32_t rvaRestoreAv = Addr("fn:ActorValueOwner::RestoreActorValueThunk");
    g_gp.fnGetAv     = rvaGetAv     ? reinterpret_cast<GetActorValueFn>(const_cast<uint8_t*>(img.base) + rvaGetAv) : nullptr;
    g_gp.fnGetPermAv = rvaGetPermAv ? reinterpret_cast<GetActorValueFn>(const_cast<uint8_t*>(img.base) + rvaGetPermAv) : nullptr;
    g_gp.fnRestoreAv = rvaRestoreAv ? reinterpret_cast<RestoreAvThunkFn>(const_cast<uint8_t*>(img.base) + rvaRestoreAv) : nullptr;

    auto settingFloat = [&](const char* name) -> float* {
        const uint32_t r = Addr(name);
        return r ? reinterpret_cast<float*>(const_cast<uint8_t*>(img.base) + r + 8u) : nullptr;
    };
    g_gp.barterSaved.pMin     = settingFloat("fBarterMin");
    g_gp.barterSaved.pMax     = settingFloat("fBarterMax");
    g_gp.barterSaved.pBuyMin  = settingFloat("fBarterBuyMin");
    g_gp.barterSaved.pSellMax = settingFloat("fBarterSellMax");

    uint8_t mask = 0;
    auto tryHook = [&](const char* key, void* hook, void** orig, uint8_t bit) -> bool {
        const uint32_t rva = Addr(key);
        if (!rva) return false;
        const std::string r = InstallDetour(img, rva, hook, orig);
        if (r.rfind("ok:", 0) == 0) {
            mask |= bit;
            return true;
        }
        return false;
    };

    tryHook("fn:HitData::Populate",       reinterpret_cast<void*>(&HookHitDataPopulate),      reinterpret_cast<void**>(&g_gp.origHitPopulate),   1u << 0);
    tryHook("fn:CombatAI::GetAttackChance", reinterpret_cast<void*>(&HookCombatAttackChance), reinterpret_cast<void**>(&g_gp.origAttackChance),  1u << 1);
    tryHook("fn:CombatAI::GetBlockChance",  reinterpret_cast<void*>(&HookCombatBlockChance),  reinterpret_cast<void**>(&g_gp.origBlockChance),   1u << 2);
    tryHook("fn:CombatAI::GetBashChance",   reinterpret_cast<void*>(&HookCombatBashChance),   reinterpret_cast<void**>(&g_gp.origBashChance),    1u << 3);
    tryHook("fn:BarterMenu::ProcessMessage",reinterpret_cast<void*>(&HookBarterProcessMessage),reinterpret_cast<void**>(&g_gp.origBarterProcMsg),1u << 4);

    g_gp.hooksMask.store(mask, std::memory_order_release);
    g_gp.active.store(mask != 0u, std::memory_order_release);

    char buf[256];
    std::snprintf(buf, sizeof buf,
                  "[GameplayHooks] live detours active mask=0x%02X (HitData=%u AttackAI=%u BlockAI=%u BashAI=%u Barter=%u)",
                  mask, (mask & 1u) ? 1u : 0u, (mask & 2u) ? 1u : 0u, (mask & 4u) ? 1u : 0u,
                  (mask & 8u) ? 1u : 0u, (mask & 16u) ? 1u : 0u);
    return buf;
}

void GameplayHooksShutdown() {
    g_gp.active.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lk(g_gp.mu);
    if (g_gp.barterSaved.saved) {
        SehWriteFloatPtr(g_gp.barterSaved.pMin, g_gp.barterSaved.vMin);
        SehWriteFloatPtr(g_gp.barterSaved.pMax, g_gp.barterSaved.vMax);
        SehWriteFloatPtr(g_gp.barterSaved.pBuyMin, g_gp.barterSaved.vBuyMin);
        SehWriteFloatPtr(g_gp.barterSaved.pSellMax, g_gp.barterSaved.vSellMax);
        g_gp.barterSaved.saved = false;
    }
    g_gp.records.clear();
}

void GameplayHooksUpdateSquadSnapshot(uint32_t activeActors, uint8_t attackTokens, uint8_t tacticalAction,
                                      float storm01, float snowDepthU) {
    g_gp.squadActors.store(activeActors, std::memory_order_relaxed);
    g_gp.squadTokens.store(attackTokens, std::memory_order_relaxed);
    g_gp.squadAction.store(tacticalAction, std::memory_order_relaxed);
    g_gp.storm01.store(std::clamp(storm01, 0.0f, 1.0f), std::memory_order_relaxed);
    g_gp.snowDepthU.store(std::clamp(snowDepthU, 0.0f, 64.0f), std::memory_order_relaxed);
}

bool GameplayHooksGetStats(GameplayHookTelemetry* out) {
    if (!out) return false;
    out->hooksActive   = g_gp.hooksMask.load(std::memory_order_relaxed);
    out->hitsProcessed = g_gp.hitsProcessed.load(std::memory_order_relaxed);
    out->timedParries  = g_gp.timedParries.load(std::memory_order_relaxed);
    out->guardBreaks   = g_gp.guardBreaks.load(std::memory_order_relaxed);
    out->counterHits   = g_gp.counterHits.load(std::memory_order_relaxed);
    out->aiAttackEvals = g_gp.aiAttackEvals.load(std::memory_order_relaxed);
    out->aiBlockEvals  = g_gp.aiBlockEvals.load(std::memory_order_relaxed);
    out->aiBashEvals   = g_gp.aiBashEvals.load(std::memory_order_relaxed);
    out->barterOpens   = g_gp.barterOpens.load(std::memory_order_relaxed);
    out->lastBarterMin = g_gp.lastBarterMin.load(std::memory_order_relaxed);
    out->lastBarterMax = g_gp.lastBarterMax.load(std::memory_order_relaxed);
    return true;
}

std::string GameplayHooksStatsJson() {
    GameplayHookTelemetry t{};
    GameplayHooksGetStats(&t);
    char b[320];
    std::snprintf(b, sizeof b,
                  "\"hooks_mask\":%u,\"hits\":%u,\"parries\":%u,\"guard_breaks\":%u,\"counter_hits\":%u,"
                  "\"ai_atk\":%u,\"ai_blk\":%u,\"ai_bash\":%u,\"barter_opens\":%u,\"barter_min\":%.2f,\"barter_max\":%.2f",
                  t.hooksActive, t.hitsProcessed, t.timedParries, t.guardBreaks, t.counterHits,
                  t.aiAttackEvals, t.aiBlockEvals, t.aiBashEvals, t.barterOpens, t.lastBarterMin, t.lastBarterMax);
    return b;
}
#endif

}  // namespace raw::rt
