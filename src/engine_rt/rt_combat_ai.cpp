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

}  // namespace raw::rt
