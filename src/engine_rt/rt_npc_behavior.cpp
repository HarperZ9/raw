// rt_npc_behavior.cpp: Wave 49 Weather, Season, Time-of-Day & Economy-Driven NPC Daily Behavior &
// Crowd Ecology Controller (`raw::rt`).
// Original work, MIT.

#include "rt_npc_behavior.h"

#include "rt_live_config.h"
#include "rt_magic_physics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>

#ifndef RAW_SIM_PURE_TEST
#include "rt_economy_ledger.h"
#include "rt_view.h"
#include "rt_world_sim.h"
#endif

namespace raw::rt {
namespace {

inline float ClampF(float v, float lo, float hi) noexcept {
    if (!std::isfinite(v)) return lo;
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float Clamp01(float v) noexcept { return ClampF(v, 0.0f, 1.0f); }

// Physical Beaufort (0..12) to wind speed (m/s) and Osczevski-Bluestein wind-chill (C).
float ComputeWindChillC(float tempC, float beaufort) noexcept {
    const float b      = ClampF(beaufort, 0.0f, 12.0f);
    const float windMs = 0.836f * std::pow(b, 1.5f);
    const float vKmh   = windMs * 3.6f;
    if (tempC <= 10.0f && vKmh >= 4.8f) {
        const float vp = std::pow(vKmh, 0.16f);
        return 13.12f + 0.6215f * tempC - 11.37f * vp + 0.3965f * tempC * vp;
    }
    return tempC;
}

void GetSeasonalSchedule(WorldSeason season,
                         float& outDayStart, float& outDayEnd,
                         float& outMktOpen,  float& outMktClose) noexcept {
    // High-latitude Nordic (~60 deg N) daylight & market schedule
    switch (season) {
        case WorldSeason::Summer:
            outDayStart = 4.5f; outDayEnd   = 21.5f;
            outMktOpen  = 7.0f; outMktClose = 20.5f; // Open past 19:30 during long Nordic summer evenings!
            break;
        case WorldSeason::Spring:
            outDayStart = 6.0f; outDayEnd   = 19.5f;
            outMktOpen  = 7.5f; outMktClose = 19.0f;
            break;
        case WorldSeason::Autumn:
            outDayStart = 6.5f; outDayEnd   = 18.5f;
            outMktOpen  = 8.0f; outMktClose = 18.0f;
            break;
        case WorldSeason::Winter:
        default:
            outDayStart = 8.5f; outDayEnd   = 16.0f;
            outMktOpen  = 9.0f; outMktClose = 16.5f; // Short Nordic winter days: closed well before 19:30!
            break;
    }
}

struct NpcBehaviorRuntime {
    std::mutex            mu;
    bool                  initialized = false;
    uint32_t              tickCounter = 0u;
    NpcBehaviorEvaluation lastEval{};
} g_npc;

}  // namespace

const char* NpcArchetypeName(NpcArchetype arch) noexcept {
    switch (arch) {
        case NpcArchetype::Guard:    return "Guard";
        case NpcArchetype::Villager: return "Villager";
        case NpcArchetype::Merchant: return "Merchant";
        case NpcArchetype::Hunter:   return "Hunter";
        case NpcArchetype::Mage:     return "Mage";
        case NpcArchetype::Bandit:   return "Bandit";
        default:                     return "Unknown";
    }
}

const char* RoutineDirectiveName(RoutineDirective dir) noexcept {
    switch (dir) {
        case RoutineDirective::WorkOutdoor: return "WorkOutdoor";
        case RoutineDirective::Patrol:      return "Patrol";
        case RoutineDirective::MarketStall: return "MarketStall";
        case RoutineDirective::SeekShelter: return "SeekShelter";
        case RoutineDirective::HearthRest:  return "HearthRest";
        case RoutineDirective::HuntTrack:   return "HuntTrack";
        case RoutineDirective::AmbushWatch: return "AmbushWatch";
        default:                            return "Unknown";
    }
}

const char* PreferredElementName(PreferredElement elem) noexcept {
    switch (elem) {
        case PreferredElement::Physical: return "Physical";
        case PreferredElement::Fire:     return "Fire";
        case PreferredElement::Frost:    return "Frost";
        case PreferredElement::Shock:    return "Shock";
        default:                         return "Physical";
    }
}

WorldSeason SeasonFromSkyrimMonth(float month0To11) noexcept {
    const int m = static_cast<int>(std::floor(std::isfinite(month0To11) ? month0To11 : 7.0f)) % 12;
    const int norm = (m < 0) ? (m + 12) : m;
    if (norm == 11 || norm == 0 || norm == 1) return WorldSeason::Winter;
    if (norm >= 2 && norm <= 4)               return WorldSeason::Spring;
    if (norm >= 5 && norm <= 7)               return WorldSeason::Summer;
    return WorldSeason::Autumn;
}

NpcBehaviorEvaluation EvaluateNpcBehavior(const NpcBehaviorEnvInput& in) noexcept {
    NpcBehaviorEvaluation eval{};
    auto& s = eval.summary;

    float hour = std::isfinite(in.gameHour) ? std::fmod(in.gameHour, 24.0f) : 12.0f;
    if (hour < 0.0f) hour += 24.0f;

    const float tempC      = ClampF(in.surfaceTempC, -50.0f, 50.0f);
    const float beaufort   = ClampF(in.windBeaufort, 0.0f, 12.0f);
    const float rain01     = Clamp01(in.rain01);
    const float snow01     = Clamp01(in.snow01);
    const float fog01      = Clamp01(in.fog01);
    const float thunder01  = Clamp01(in.thunder01);
    const float scarcity01 = Clamp01(in.scarcity01);

    const float windChillC = ComputeWindChillC(tempC, beaufort);

    float dayStart = 6.0f, dayEnd = 20.0f, mktOpen = 7.0f, mktClose = 20.0f;
    GetSeasonalSchedule(in.season, dayStart, dayEnd, mktOpen, mktClose);

    s.behaviorEnabled     = 1u;
    s.season              = static_cast<uint8_t>(in.season);
    s.effectiveWindChillC = windChillC;
    s.daylightStartHour   = dayStart;
    s.daylightEndHour     = dayEnd;
    s.marketOpenHour      = mktOpen;
    s.marketCloseHour     = mktClose;

    const bool isDaylight    = (hour >= dayStart && hour <= dayEnd);
    const bool isMarketHours = (hour >= mktOpen && hour <= mktClose);

    // Compute smooth ambient darkness [0..1]: strictly 0 during bright midday
    float darkness01 = 0.0f;
    if (!isDaylight) {
        darkness01 = 1.0f;
    } else {
        const float marginStart = Clamp01((hour - dayStart) / 1.25f);
        const float marginEnd   = Clamp01((dayEnd - hour) / 1.25f);
        darkness01 = 1.0f - std::min(marginStart, marginEnd);
    }
    if (in.inInterior) darkness01 = std::max(darkness01, 0.55f);

    // Severe storm / blizzard / freezing hazard classification
    const bool severeBlizzard = (snow01 > 0.55f) || (windChillC < -18.0f);
    const bool heavyDownpour  = (rain01 > 0.55f) || (thunder01 > 0.45f);
    const bool severeWeather  = severeBlizzard || heavyDownpour;

    // Shelter pressure [0..1]: near 0 on sunny summer noon, >0.85 in winter midnight blizzard
    const float blizzardPress = Clamp01((snow01 - 0.25f) / 0.45f) * 0.70f +
                                Clamp01((-10.0f - windChillC) / 14.0f) * 0.60f;
    const float stormPress    = Clamp01((rain01 - 0.30f) / 0.45f) * 0.65f + thunder01 * 0.45f;
    const float nightPress    = (!isDaylight) ? 0.30f : 0.0f;
    s.shelterPressure01 = Clamp01(std::max(blizzardPress, stormPress) + nightPress);

    // Couple directly to rt_magic_physics EvaluateElementalSynergy for Mage & Bandit elemental bias!
    MagicEnvInput magicEnv{};
    magicEnv.surfaceTempC    = tempC;
    magicEnv.rainIntensity01 = rain01;
    magicEnv.snowIntensity01 = snow01;
    magicEnv.wetness01       = Clamp01(rain01 * 0.90f + fog01 * 0.20f);
    magicEnv.submerged       = false;
    magicEnv.thunderstorm01  = thunder01;
    magicEnv.windBeaufort    = beaufort;
    const ElementalSynergyState syn = EvaluateElementalSynergy(magicEnv);

    PreferredElement mageElem = PreferredElement::Fire;
    if (rain01 >= 0.30f || thunder01 >= 0.25f) {
        mageElem = (syn.shockDamageMult >= syn.frostDamageMult) ? PreferredElement::Shock : PreferredElement::Frost;
    } else if (snow01 >= 0.45f && tempC < -5.0f) {
        mageElem = (syn.frostDamageMult * syn.frostReachMult >= syn.fireDamageMult)
                       ? PreferredElement::Frost
                       : PreferredElement::Fire;
    } else {
        mageElem = PreferredElement::Fire;
    }

    PreferredElement banditElem = PreferredElement::Physical;
    if (rain01 >= 0.30f || thunder01 >= 0.25f) {
        banditElem = (thunder01 >= 0.25f || syn.shockDamageMult >= syn.frostDamageMult)
                         ? PreferredElement::Shock
                         : PreferredElement::Frost;
    } else if (tempC < -2.0f && rain01 < 0.20f) {
        banditElem = PreferredElement::Fire;
    } else {
        banditElem = PreferredElement::Physical;
    }

    const bool needTorch   = (darkness01 > 0.35f) || (fog01 > 0.55f) || (snow01 > 0.55f);
    const bool coldCampfire = (tempC < -5.0f) || (windChillC < -10.0f);

    // 0. Guard: always on duty; activates torch in darkness/blizzard/fog, narrows patrol & heightens alertness in storms
    {
        auto& g = eval.archetypes[static_cast<size_t>(NpcArchetype::Guard)];
        g.directive        = RoutineDirective::Patrol;
        g.preferredElement = PreferredElement::Physical;
        g.carryTorch       = needTorch;
        g.lightCampfire    = false;
        g.alertnessMult    = ClampF(1.0f + 0.25f * darkness01 + 0.22f * fog01 +
                                    0.25f * snow01 + 0.18f * thunder01 + 0.15f * scarcity01,
                                    0.85f, 1.85f);
        g.patrolRadiusMult = ClampF(1.0f - 0.30f * snow01 - 0.20f * rain01 - 0.12f * darkness01,
                                    0.45f, 1.15f);
        g.activity01       = 1.0f;
        s.guardAlertnessMult = g.alertnessMult;
    }

    // 1. Villager: outdoor labor in fair daylight, seeks shelter in blizzards/downpours, hearth rest at night
    {
        auto& v = eval.archetypes[static_cast<size_t>(NpcArchetype::Villager)];
        v.preferredElement = PreferredElement::Physical;
        if (severeWeather) {
            v.directive  = RoutineDirective::SeekShelter;
            v.activity01 = 0.10f;
        } else if (!isDaylight) {
            v.directive  = RoutineDirective::HearthRest;
            v.activity01 = 0.20f;
        } else {
            v.directive  = RoutineDirective::WorkOutdoor;
            v.activity01 = Clamp01(1.0f - 0.45f * rain01 - 0.40f * snow01);
        }
        v.carryTorch       = (!isDaylight && !severeWeather && v.directive != RoutineDirective::HearthRest);
        v.lightCampfire    = false;
        v.alertnessMult    = 1.0f + 0.20f * darkness01;
        v.patrolRadiusMult = v.activity01;
    }

    // 2. Merchant: operates seasonal market stall during open hours, retreats indoors in storms/after hours
    {
        auto& m = eval.archetypes[static_cast<size_t>(NpcArchetype::Merchant)];
        m.preferredElement = PreferredElement::Physical;
        if (severeWeather) {
            m.directive  = RoutineDirective::SeekShelter;
            m.activity01 = 0.05f;
        } else if (!isMarketHours) {
            m.directive  = RoutineDirective::HearthRest;
            m.activity01 = 0.0f;
        } else {
            m.directive  = RoutineDirective::MarketStall;
            m.activity01 = Clamp01((1.0f - 0.55f * rain01 - 0.50f * snow01 - 0.35f * thunder01) *
                                   (1.0f - 0.20f * scarcity01));
        }
        m.carryTorch       = (m.directive == RoutineDirective::MarketStall && darkness01 > 0.35f);
        m.lightCampfire    = false;
        m.alertnessMult    = 1.0f + 0.25f * scarcity01;
        m.patrolRadiusMult = m.activity01;
        s.marketActivity01 = m.activity01;
    }

    // 3. Hunter: tracks game with expanded range in winter/scarcity, lights campfires below -5 C
    {
        auto& h = eval.archetypes[static_cast<size_t>(NpcArchetype::Hunter)];
        h.preferredElement = PreferredElement::Physical;
        const float winterBoost = (in.season == WorldSeason::Winter) ? 0.25f : 0.0f;
        s.hunterForageRangeMult = ClampF(1.0f + 0.45f * scarcity01 + winterBoost, 1.0f, 1.75f);
        h.patrolRadiusMult      = s.hunterForageRangeMult;
        h.lightCampfire         = coldCampfire;
        if (snow01 > 0.72f || windChillC < -26.0f) {
            h.directive  = RoutineDirective::SeekShelter;
            h.activity01 = 0.15f;
        } else if (hour < 4.5f || hour > 22.0f) {
            h.directive  = RoutineDirective::HearthRest;
            h.activity01 = 0.25f;
        } else {
            h.directive  = RoutineDirective::HuntTrack;
            h.activity01 = Clamp01(0.95f - 0.35f * rain01 - 0.30f * snow01 + 0.15f * scarcity01);
        }
        h.carryTorch    = (h.directive == RoutineDirective::HuntTrack && darkness01 > 0.40f);
        h.alertnessMult = 1.15f + 0.20f * fog01;
    }

    // 4. Mage: aligns elemental preference with live rt_magic_physics weather synergy!
    {
        auto& mg = eval.archetypes[static_cast<size_t>(NpcArchetype::Mage)];
        mg.preferredElement = mageElem;
        if (severeBlizzard) {
            mg.directive  = RoutineDirective::SeekShelter;
            mg.activity01 = 0.25f;
        } else if (!isDaylight) {
            mg.directive  = (thunder01 > 0.35f) ? RoutineDirective::Patrol : RoutineDirective::HearthRest;
            mg.activity01 = (thunder01 > 0.35f) ? 0.80f : 0.35f;
        } else {
            mg.directive  = RoutineDirective::WorkOutdoor;
            mg.activity01 = 0.90f;
        }
        mg.carryTorch       = (mg.directive == RoutineDirective::Patrol && needTorch);
        mg.lightCampfire    = false;
        mg.alertnessMult    = 1.10f + 0.25f * thunder01;
        mg.patrolRadiusMult = 0.90f;
    }

    // 5. Bandit: opportunistic ambush watch exploiting fog/night/scarcity + elemental weather synergy
    {
        auto& bd = eval.archetypes[static_cast<size_t>(NpcArchetype::Bandit)];
        bd.preferredElement = banditElem;
        bd.lightCampfire    = coldCampfire;
        if (windChillC < -26.0f && snow01 > 0.75f) {
            bd.directive  = RoutineDirective::HearthRest;
            bd.activity01 = 0.30f;
        } else {
            bd.directive  = RoutineDirective::AmbushWatch;
            bd.activity01 = Clamp01(0.75f + 0.25f * darkness01 + 0.20f * fog01 + 0.20f * scarcity01);
        }
        bd.carryTorch       = (bd.directive == RoutineDirective::AmbushWatch && (darkness01 > 0.65f || snow01 > 0.60f));
        bd.alertnessMult    = ClampF(1.10f + 0.25f * darkness01 + 0.25f * fog01 + 0.20f * scarcity01, 0.90f, 1.80f);
        bd.patrolRadiusMult = ClampF(1.0f + 0.20f * scarcity01 - 0.25f * snow01, 0.55f, 1.35f);
    }

    // Pack summary flags, torch ratio & wildlife activity
    uint32_t torchCount = 0u;
    uint8_t  torchMask  = 0u;
    uint8_t  campMask   = 0u;
    for (size_t i = 0u; i < 6u; ++i) {
        s.activeDirective[i] = static_cast<uint8_t>(eval.archetypes[i].directive);
        if (eval.archetypes[i].carryTorch) {
            ++torchCount;
            torchMask |= static_cast<uint8_t>(1u << i);
        }
        if (eval.archetypes[i].lightCampfire) {
            campMask |= static_cast<uint8_t>(1u << i);
        }
    }
    s.carryTorchFlags        = torchMask;
    s.campfireFlags          = campMask;
    s.torchDeploymentRatio   = float(torchCount) / 6.0f;
    s.preferredBanditElement = static_cast<uint8_t>(banditElem);
    s.preferredMageElement   = static_cast<uint8_t>(mageElem);

    // Wildlife activity peaks at dawn/dusk & fair weather, drops sharply during blizzards & heavy thunderstorms
    const float crepuscularBoost = (std::fabs(hour - dayStart) < 2.0f || std::fabs(hour - dayEnd) < 2.0f) ? 0.18f : 0.0f;
    s.wildlifeActivity01 = Clamp01(0.82f + crepuscularBoost - 0.55f * snow01 - 0.45f * thunder01 - 0.30f * rain01);
    return eval;
}

// --- Runtime Lifecycle & C ABI ---

std::string NpcBehaviorInit() {
    const NpcBehaviorEnvInput defEnv{};
    const NpcBehaviorEvaluation eval = EvaluateNpcBehavior(defEnv);
    {
        std::lock_guard<std::mutex> lk(g_npc.mu);
        g_npc.initialized = true;
        g_npc.tickCounter = 0u;
        g_npc.lastEval    = eval;
    }
    const LiveEngineConfig cfg = GetLiveConfig();
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "[NpcBehavior] initialized (DynamicNpcBehavior=%u, 6 archetypes coupled to worldSim + economy + magicPhysics)",
                  cfg.dynamicNpcBehaviorEnabled ? 1u : 0u);
    return buf;
}

void NpcBehaviorUpdateEnv(const NpcBehaviorEnvInput& env) {
    if (!GetLiveConfig().dynamicNpcBehaviorEnabled) return;
    const NpcBehaviorEvaluation eval = EvaluateNpcBehavior(env);
    std::lock_guard<std::mutex> lk(g_npc.mu);
    g_npc.initialized = true;
    g_npc.lastEval    = eval;
}

void NpcBehaviorOnPresent() {
    if (!GetLiveConfig().dynamicNpcBehaviorEnabled) return;
#ifndef RAW_SIM_PURE_TEST
    {
        std::lock_guard<std::mutex> lk(g_npc.mu);
        if (!g_npc.initialized) g_npc.initialized = true;
        // Evaluate smoothly at ~4 Hz (every 15 presents) with zero allocations
        if ((g_npc.tickCounter++ % 15u) != 0u) return;
    }

    NpcBehaviorEnvInput env{};
    EngineView ev{};
    if (RAW_EngineView(&ev) > 0u) {
        env.gameHour   = ev.gameHour;
        env.season     = SeasonFromSkyrimMonth(ev.gameMonth);
        env.inInterior = (ev.interior != 0u);
    }

    RawWorldSimTelemetry ws{};
    if (RAW_GetWorldSimTelemetry(&ws)) {
        env.surfaceTempC = ws.ambientTempC;
        env.windBeaufort = ws.beaufortScale;
        env.rain01       = ws.synopticRain;
        env.snow01       = ws.synopticSnow;
        env.thunder01    = ws.synopticLightning;
        env.fog01        = Clamp01(ws.synopticCloud * 0.55f);
    }

    EconomyLedgerTelemetry ec{};
    if (EconomyLedgerGetTelemetry(&ec) && ec.initialized) {
        env.scarcity01 = ec.scarcity01;
    }

    NpcBehaviorUpdateEnv(env);
#endif
}

void NpcBehaviorShutdown() {
    std::lock_guard<std::mutex> lk(g_npc.mu);
    g_npc.initialized = false;
    g_npc.tickCounter = 0u;
}

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetNpcBehaviorState(raw::rt::RawNpcBehaviorState* out) {
    if (!out) return 0;
    const raw::rt::LiveEngineConfig cfg = raw::rt::GetLiveConfig();
    std::lock_guard<std::mutex> lk(raw::rt::g_npc.mu);
    *out = raw::rt::g_npc.lastEval.summary;
    out->behaviorEnabled = cfg.dynamicNpcBehaviorEnabled ? 1u : 0u;
    return raw::rt::g_npc.initialized ? 1 : 0;
}
