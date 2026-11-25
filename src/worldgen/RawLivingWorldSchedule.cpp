// RawLivingWorldSchedule.cpp: Clean-Room 24-Hour Weather, War & Wildfire-Coupled Living World NPC
// and Wildlife Schedule Engine (`raw::worldgen`). Pure portable C++23 with zero external SDKs.
// Original work, MIT.

#include "RawLivingWorldSchedule.h"

#include <algorithm>
#include <cmath>

namespace raw::worldgen {

namespace {

inline float SafeClamp01(float v) {
    if (!std::isfinite(v)) return 0.0f;
    return std::clamp(v, 0.0f, 1.0f);
}

inline float ClampRange(float v, float lo, float hi) {
    if (!std::isfinite(v)) return lo;
    return std::clamp(v, lo, hi);
}

inline float WrapHour24(float h) {
    if (!std::isfinite(h)) return 0.0f;
    float r = std::fmod(h, 24.0f);
    if (r < 0.0f) r += 24.0f;
    return r;
}

inline bool HourInInterval(float h, float startH, float endH) {
    // Supports wrap-around intervals such as [22, 6).
    const float x = WrapHour24(h);
    const float a = WrapHour24(startH);
    const float b = WrapHour24(endH);
    if (a <= b) {
        return x >= a && x < b;
    }
    return x >= a || x < b;
}

}  // namespace

const char* GetArchetypeName(CitizenArchetype archetype) {
    switch (archetype) {
        case CitizenArchetype::Blacksmith: return "Blacksmith";
        case CitizenArchetype::Merchant:   return "Merchant";
        case CitizenArchetype::Guard:      return "Guard";
        case CitizenArchetype::Farmer:     return "Farmer";
        case CitizenArchetype::Innkeeper:  return "Innkeeper";
        case CitizenArchetype::Hunter:     return "Hunter";
        case CitizenArchetype::Mage:       return "Mage";
        case CitizenArchetype::Priest:     return "Priest";
        default:                           return "Unknown";
    }
}

const char* GetLocationName(LocationKind loc) {
    switch (loc) {
        case LocationKind::Home:              return "Home";
        case LocationKind::Tavern:            return "Tavern";
        case LocationKind::Market:            return "Market";
        case LocationKind::Smithy:            return "Smithy";
        case LocationKind::Farm:              return "Farm";
        case LocationKind::Barracks:          return "Barracks";
        case LocationKind::Temple:            return "Temple";
        case LocationKind::ArcaneStudy:       return "ArcaneStudy";
        case LocationKind::PatrolRoute:       return "PatrolRoute";
        case LocationKind::Wilds:             return "Wilds";
        case LocationKind::EvacuationShelter: return "EvacuationShelter";
        default:                              return "Unknown";
    }
}

const char* GetActivityName(ScheduleActivity act) {
    switch (act) {
        case ScheduleActivity::Sleeping:           return "Sleeping";
        case ScheduleActivity::Working:            return "Working";
        case ScheduleActivity::Patrolling:         return "Patrolling";
        case ScheduleActivity::TavernRest:         return "TavernRest";
        case ScheduleActivity::ShelteringWeather:  return "ShelteringWeather";
        case ScheduleActivity::CurfewLockdown:     return "CurfewLockdown";
        case ScheduleActivity::EvacuatingWildfire: return "EvacuatingWildfire";
        case ScheduleActivity::Traveling:          return "Traveling";
        default:                                   return "Unknown";
    }
}

const char* GetWildlifeGuildName(WildlifeGuild guild) {
    switch (guild) {
        case WildlifeGuild::ElkHerd:         return "ElkHerd";
        case WildlifeGuild::HareWarren:      return "HareWarren";
        case WildlifeGuild::TimberWolfPack:  return "TimberWolfPack";
        case WildlifeGuild::SabreCatStalker: return "SabreCatStalker";
        default:                             return "Unknown";
    }
}

bool IsLocationIndoors(LocationKind loc) {
    switch (loc) {
        case LocationKind::Home:
        case LocationKind::Tavern:
        case LocationKind::Barracks:
        case LocationKind::Temple:
        case LocationKind::ArcaneStudy:
        case LocationKind::EvacuationShelter:
            return true;
        case LocationKind::Market:
        case LocationKind::Smithy:
        case LocationKind::Farm:
        case LocationKind::PatrolRoute:
        case LocationKind::Wilds:
        default:
            return false;
    }
}

float EvaluateStormShelterProbability(float ambientTempC,
                                      float windSpeedMps,
                                      float rain01,
                                      float snow01,
                                      float blizzard01,
                                      float resilience01) {
    const float tC     = std::isfinite(ambientTempC) ? ambientTempC : 8.0f;
    const float wind   = std::max(0.0f, std::isfinite(windSpeedMps) ? windSpeedMps : 0.0f);
    const float rain   = SafeClamp01(rain01);
    const float snow   = SafeClamp01(snow01);
    const float bliz   = SafeClamp01(blizzard01);
    const float resil  = SafeClamp01(resilience01);

    // Cold wind-chill discomfort + heavy rain/snow/blizzard pressure:
    const float coldStress = SafeClamp01((-tC + 2.0f) / 20.0f);
    const float windStress = SafeClamp01((wind - 6.0f) / 16.0f);
    const float precip     = std::max(rain * 0.78f, snow * 0.85f);
    const float rawUrgency = 0.85f * bliz + 0.50f * precip + 0.35f * coldStress * windStress + 0.25f * windStress;
    const float adjusted   = rawUrgency - 0.18f * resil;

    // Smooth logistic-like bounded probability [0..1]:
    const float x = (adjusted - 0.32f) * 7.5f;
    const float p = 1.0f / (1.0f + std::exp(-x));
    return SafeClamp01(p);
}

LivingWorldContext BuildLivingWorldContext(HoldId                     hold,
                                           const RawSettlementSim*    settlement,
                                           const RawWeatherSim*       weather,
                                           const RawFactionDiplomacy* diplomacy,
                                           const RawWorldEcosystem*   ecosystem,
                                           bool                       wartimeCurfewOverride) {
    LivingWorldContext ctx{};
    ctx.hold = hold;

    if (settlement) {
        if (const HoldState* hs = settlement->GetHold(hold)) {
            ctx.settlementMarketOpen = hs->marketOpen;
            ctx.settlementGuards     = hs->activeGuards;
            ctx.settlementTavernOcc  = SafeClamp01(hs->tavernOccupancy01);
        }
        const SettlementTelemetry st = settlement->ComputeWorldTelemetry();
        ctx.timeOfDayHours = WrapHour24(st.timeOfDayHours);
    }

    if (weather) {
        const CalendarState&       cal = weather->GetCalendarState();
        const WeatherGpuUniforms&  gpu = weather->GetGpuUniforms();
        const WeatherRegime        reg = weather->GetActiveRegime();

        ctx.timeOfDayHours = WrapHour24(cal.timeOfDayHours);
        ctx.dayLengthHours = ClampRange(cal.dayLengthHours, 6.0f, 18.0f);
        ctx.solarElevation = ClampRange(cal.solarElevation, -1.0f, 1.0f);
        ctx.season         = cal.season;
        ctx.weatherRegime  = reg;
        ctx.ambientTempC   = std::isfinite(gpu.seasonThermal0.w) ? gpu.seasonThermal0.w : 8.0f;

        // Convert world-unit/s wind (70 u/m) or fallback to m/s magnitude safely:
        const float windU = std::hypot(gpu.cloudWind.x, gpu.cloudWind.y);
        ctx.windSpeedMps  = std::isfinite(windU) ? (windU / 70.0f) : 3.0f;
        // Also incorporate Beaufort scale floor in case cloudWind was scaled differently:
        const float beaufortMps = std::max(0.0f, gpu.beaufortScale) * 2.2f;
        ctx.windSpeedMps        = std::max(ctx.windSpeedMps, beaufortMps);

        ctx.rain01 = SafeClamp01(gpu.weather.w);
        ctx.snow01 = SafeClamp01(gpu.weather.z);

        float bliz = 0.0f;
        if (reg == WeatherRegime::Blizzard) {
            bliz = std::max(0.85f, SafeClamp01(0.55f * ctx.snow01 + 0.45f * SafeClamp01(ctx.windSpeedMps / 16.0f)));
        } else if (ctx.snow01 > 0.35f && ctx.windSpeedMps > 10.0f) {
            bliz = SafeClamp01(0.6f * ctx.snow01 + 0.4f * SafeClamp01((ctx.windSpeedMps - 8.0f) / 14.0f));
        }
        ctx.blizzardSeverity01 = SafeClamp01(bliz);

        float storm = std::max(ctx.blizzardSeverity01, ctx.rain01 * 0.80f);
        if (reg == WeatherRegime::Thunderstorm) {
            storm = std::max(storm, 0.85f);
        } else if (reg == WeatherRegime::Rainstorm) {
            storm = std::max(storm, 0.65f);
        }
        ctx.stormSeverity01 = SafeClamp01(storm);
    }

    if (diplomacy) {
        if (const HoldStanding* hs = diplomacy->GetHoldStanding(hold)) {
            ctx.guardPosture = hs->guardPosture;
        }
        const FactionStanding* imp = diplomacy->GetFactionStanding(FactionId::ImperialLegion);
        const FactionStanding* stm = diplomacy->GetFactionStanding(FactionId::Stormcloaks);
        const DiplomacyTelemetry dt = diplomacy->ComputeTelemetry();

        float rivalry = 0.0f;
        if (imp && stm) {
            rivalry = SafeClamp01(std::fabs(imp->reputation - stm->reputation) / 100.0f);
        }
        const float hostileFrac = SafeClamp01(static_cast<float>(dt.hostileHoldCount) / 3.0f);
        ctx.civilWarTension01   = SafeClamp01(std::max(rivalry, hostileFrac));

        const bool holdHostile = (ctx.guardPosture == GuardPosture::ArrestOnSight ||
                                  ctx.guardPosture == GuardPosture::KillOnSight);
        ctx.wartimeCurfewActive = wartimeCurfewOverride || holdHostile || (ctx.civilWarTension01 >= 0.55f);
    } else {
        ctx.wartimeCurfewActive = wartimeCurfewOverride;
    }

    if (ecosystem) {
        const EcoFieldTelemetry et = ecosystem->ComputeTelemetry();
        ctx.activeFireCells    = et.activeFireCells;
        ctx.ecosystemPeakTempC = et.peakTempC;
        ctx.wildfireActive     = (et.activeFireCells >= 4u) || (et.peakTempC >= 180.0f && et.activeFireCells >= 1u);
    }

    return ctx;
}

RawLivingWorldSchedule::RawLivingWorldSchedule(uint32_t seed) {
    Reset(seed);
}

void RawLivingWorldSchedule::Reset(uint32_t seed) {
    rngState_              = (seed == 0u) ? 202661u : seed;
    hold_                  = HoldId::Whiterun;
    nextCitizenId_         = 1u;
    simHoursElapsed_       = 0.0f;
    braziersLit_           = false;
    brazierIntensity_      = 0.0f;
    outerPatrolsCancelled_ = false;
    lastCtx_               = LivingWorldContext{};
    ledger_                = WorkShiftLedger{};
    citizens_.clear();
    InitWildlifeBands();
}

uint32_t RawLivingWorldSchedule::NextRng() {
    uint32_t x = rngState_;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x << 5u;
    rngState_ = (x == 0u) ? 202661u : x;
    return rngState_;
}

void RawLivingWorldSchedule::InitWildlifeBands() {
    wildlife_[0] = WildlifeBandState{WildlifeGuild::ElkHerd,         false, false, false, false, 0.70f};
    wildlife_[1] = WildlifeBandState{WildlifeGuild::HareWarren,      false, false, false, false, 0.65f};
    wildlife_[2] = WildlifeBandState{WildlifeGuild::TimberWolfPack,  true,  true,  false, false, 0.15f};
    wildlife_[3] = WildlifeBandState{WildlifeGuild::SabreCatStalker, true,  true,  false, false, 0.10f};
}

uint32_t RawLivingWorldSchedule::PopulateDefaultHold(HoldId hold, uint32_t citizenCount) {
    if (static_cast<size_t>(hold) >= kHoldCount) {
        return 0u;
    }
    const uint32_t count = std::max(citizenCount, 32u);
    hold_                = hold;
    citizens_.clear();
    citizens_.reserve(count);
    nextCitizenId_   = 1u;
    simHoursElapsed_ = 0.0f;
    ledger_          = WorkShiftLedger{};

    // Guaranteed representation across all 8 archetypes with realistic Nordic town proportions:
    // First 32 slots cover all 8 archetypes cleanly (Guard x8, Farmer x6, Merchant x5,
    // Blacksmith x3, Hunter x3, Innkeeper x2, Mage x2, Priest x3 = 32), remaining slots cycle.
    static constexpr std::array<CitizenArchetype, 32> kBaseRoster = {
        CitizenArchetype::Guard,      CitizenArchetype::Guard,      CitizenArchetype::Guard,      CitizenArchetype::Guard,
        CitizenArchetype::Guard,      CitizenArchetype::Guard,      CitizenArchetype::Guard,      CitizenArchetype::Guard,
        CitizenArchetype::Farmer,     CitizenArchetype::Farmer,     CitizenArchetype::Farmer,     CitizenArchetype::Farmer,
        CitizenArchetype::Farmer,     CitizenArchetype::Farmer,     CitizenArchetype::Merchant,   CitizenArchetype::Merchant,
        CitizenArchetype::Merchant,   CitizenArchetype::Merchant,   CitizenArchetype::Merchant,   CitizenArchetype::Blacksmith,
        CitizenArchetype::Blacksmith, CitizenArchetype::Blacksmith, CitizenArchetype::Hunter,     CitizenArchetype::Hunter,
        CitizenArchetype::Hunter,     CitizenArchetype::Innkeeper,  CitizenArchetype::Innkeeper,  CitizenArchetype::Mage,
        CitizenArchetype::Mage,       CitizenArchetype::Priest,     CitizenArchetype::Priest,     CitizenArchetype::Priest,
    };

    for (uint32_t i = 0u; i < count; ++i) {
        const CitizenArchetype arch = (i < kBaseRoster.size())
            ? kBaseRoster[i]
            : static_cast<CitizenArchetype>(i % kCitizenArchetypeCount);

        float baseResil = 0.45f;
        switch (arch) {
            case CitizenArchetype::Guard:      baseResil = 0.72f; break;
            case CitizenArchetype::Hunter:     baseResil = 0.70f; break;
            case CitizenArchetype::Blacksmith: baseResil = 0.62f; break;
            case CitizenArchetype::Farmer:     baseResil = 0.50f; break;
            case CitizenArchetype::Merchant:   baseResil = 0.35f; break;
            case CitizenArchetype::Innkeeper:  baseResil = 0.40f; break;
            case CitizenArchetype::Mage:       baseResil = 0.38f; break;
            case CitizenArchetype::Priest:     baseResil = 0.42f; break;
            default:                           baseResil = 0.45f; break;
        }
        const float jitter = (static_cast<float>(NextRng() & 0xFFu) / 255.0f - 0.5f) * 0.14f;

        CitizenDesc d{};
        d.id              = nextCitizenId_++;
        d.archetype       = arch;
        d.shiftVariant    = static_cast<uint8_t>(i & 1u);
        d.resilience01    = ClampRange(baseResil + jitter, 0.15f, 0.90f);
        d.initialEnergy01 = 0.85f;
        d.initialMorale01 = 0.78f;
        AddCitizen(d);
    }

    return static_cast<uint32_t>(citizens_.size());
}

bool RawLivingWorldSchedule::AddCitizen(const CitizenDesc& desc) {
    if (static_cast<size_t>(desc.archetype) >= kCitizenArchetypeCount) {
        return false;
    }
    if (!std::isfinite(desc.resilience01) || !std::isfinite(desc.initialEnergy01) ||
        !std::isfinite(desc.initialMorale01)) {
        return false;
    }

    CitizenAgent c{};
    c.id           = (desc.id != 0u) ? desc.id : nextCitizenId_++;
    c.archetype    = desc.archetype;
    c.shiftVariant = desc.shiftVariant;
    c.resilience01 = SafeClamp01(desc.resilience01);
    c.energy01     = SafeClamp01(desc.initialEnergy01);
    c.morale01     = SafeClamp01(desc.initialMorale01);
    c.activity     = ScheduleActivity::Sleeping;
    c.location     = LocationKind::Home;
    c.isIndoors    = true;
    citizens_.push_back(c);
    return true;
}

void RawLivingWorldSchedule::UpdateWildlife(const LivingWorldContext& ctx) {
    const float h       = WrapHour24(ctx.timeOfDayHours);
    const float dayLen  = ClampRange(ctx.dayLengthHours, 6.0f, 18.0f);
    const float sunrise = 12.0f - 0.5f * dayLen;
    const float sunset  = 12.0f + 0.5f * dayLen;

    // Smooth daylight factor [0..1]: 1.0 in daytime, 0.0 deep night, soft 1.5h twilight ramp.
    float daylight01 = 0.0f;
    if (h >= sunrise && h <= sunset) {
        const float sinceDawn = SafeClamp01((h - sunrise) / 1.5f);
        const float untilDusk = SafeClamp01((sunset - h) / 1.5f);
        daylight01            = std::min(sinceDawn, untilDusk);
    } else {
        daylight01 = 0.0f;
    }
    const float night01 = 1.0f - daylight01;

    // Crepuscular boost around sunrise & sunset:
    const float dawnDist = std::fabs(h - sunrise);
    const float duskDist = std::fabs(h - sunset);
    const float crep01   = SafeClamp01(1.0f - std::min(dawnDist, duskDist) / 2.0f);

    const float coldSuppress = SafeClamp01(0.75f * ctx.blizzardSeverity01 + 0.25f * ctx.stormSeverity01);

    for (auto& band : wildlife_) {
        band.fleeingFire    = ctx.wildfireActive;
        band.shelteringCold = (ctx.blizzardSeverity01 >= 0.65f);

        if (ctx.wildfireActive) {
            // Wildfire drives wildlife out of foraging/hunting grounds immediately.
            band.activity01 = 0.08f;
            continue;
        }

        if (!band.nocturnal) {
            // Diurnal prey (Elk, Hare): high daylight + dawn foraging, low nocturnal activity.
            float base = 0.14f + 0.72f * daylight01 + 0.14f * crep01;
            base *= (1.0f - 0.55f * coldSuppress);
            band.activity01 = SafeClamp01(base);
        } else {
            // Nocturnal predators (Wolves, SabreCats): low daytime denning, strong nocturnal/dusk hunting.
            float base = 0.12f + 0.74f * night01 + 0.18f * crep01;
            // Wolves are cold-hardy; blizzards reduce even predator range modestly.
            const float predCold = (band.guild == WildlifeGuild::TimberWolfPack) ? 0.25f : 0.40f;
            base *= (1.0f - predCold * coldSuppress);
            band.activity01 = SafeClamp01(base);
        }
    }
}

void RawLivingWorldSchedule::EvaluateCitizenSchedule(CitizenAgent&             c,
                                                     float                     dtHours,
                                                     const LivingWorldContext& ctx,
                                                     bool                      braziersLit,
                                                     bool                      innkeeperActive,
                                                     bool                      priestActive,
                                                     bool                      cancelOuterPatrols) {
    const float h       = WrapHour24(ctx.timeOfDayHours);
    const float dayLen  = ClampRange(ctx.dayLengthHours, 6.0f, 18.0f);
    const float sunrise = 12.0f - 0.5f * dayLen;
    const float sunset  = 12.0f + 0.5f * dayLen;

    c.shelterUrgency01 = EvaluateStormShelterProbability(
        ctx.ambientTempC, ctx.windSpeedMps, ctx.rain01, ctx.snow01, ctx.blizzardSeverity01, c.resilience01);

    const bool severeBlizzard = (ctx.blizzardSeverity01 >= 0.55f);
    const bool heavyStorm     = (ctx.stormSeverity01 >= 0.70f);
    const bool weatherShelter = severeBlizzard || heavyStorm || (c.shelterUrgency01 >= 0.62f);

    // 1. Highest-priority override: WILDFIRE EVACUATION (RawWorldEcosystem coupling).
    if (ctx.wildfireActive) {
        if (c.archetype == CitizenArchetype::Guard && c.shiftVariant == 0u) {
            // Half of guards defend the shelter perimeter; everyone else evacuates indoors.
            c.activity = ScheduleActivity::Patrolling;
            c.location = LocationKind::EvacuationShelter;
        } else {
            c.activity = ScheduleActivity::EvacuatingWildfire;
            c.location = LocationKind::EvacuationShelter;
        }
    } else if (c.archetype == CitizenArchetype::Guard) {
        // 2. Guard logic: wartime surges patrols; severe blizzard recalls non-essential outdoor patrols.
        const bool isDayShift = (c.shiftVariant == 0u);
        const bool onNormalShift = isDayShift ? HourInInterval(h, 6.0f, 18.0f) : HourInInterval(h, 18.0f, 6.0f);
        const bool wartimesurge  = ctx.wartimeCurfewActive;

        if (cancelOuterPatrols && !wartimesurge) {
            // Peacetime severe blizzard cancels outdoor patrols -> shelter at Barracks!
            c.activity = ScheduleActivity::ShelteringWeather;
            c.location = LocationKind::Barracks;
        } else if (cancelOuterPatrols && wartimesurge) {
            // Wartime blizzard: on-shift guards hold sheltered gate posts, off-shift rest in Barracks.
            if (onNormalShift) {
                c.activity = ScheduleActivity::ShelteringWeather;
                c.location = LocationKind::Barracks;
            } else {
                c.activity = ScheduleActivity::Sleeping;
                c.location = LocationKind::Barracks;
            }
        } else if (wartimesurge) {
            // Wartime curfew deploys EXTRA guard patrols (on-shift + alert reserve when rested).
            if (onNormalShift || c.energy01 > 0.30f) {
                c.activity = ScheduleActivity::Patrolling;
                c.location = LocationKind::PatrolRoute;
            } else {
                c.activity = ScheduleActivity::Sleeping;
                c.location = LocationKind::Barracks;
            }
        } else {
            // Normal peacetime guard schedule:
            if (onNormalShift) {
                c.activity = ScheduleActivity::Patrolling;
                c.location = LocationKind::PatrolRoute;
            } else if (HourInInterval(h, 18.0f, 22.0f) && isDayShift) {
                c.activity = ScheduleActivity::TavernRest;
                c.location = LocationKind::Tavern;
            } else {
                c.activity = ScheduleActivity::Sleeping;
                c.location = LocationKind::Barracks;
            }
        }
    } else {
        // 3. Civilian & specialist archetypes (Blacksmith, Merchant, Farmer, Innkeeper, Hunter, Mage, Priest).
        const bool nightCurfewHours = ctx.wartimeCurfewActive && HourInInterval(h, 19.0f, 6.0f);

        if (nightCurfewHours) {
            // Wartime curfew locks civilians indoors at Home (Innkeepers stay inside Tavern).
            c.activity = ScheduleActivity::CurfewLockdown;
            c.location = (c.archetype == CitizenArchetype::Innkeeper) ? LocationKind::Tavern : LocationKind::Home;
        } else {
            // Determine nominal archetype schedule slot:
            ScheduleActivity nominalAct = ScheduleActivity::Sleeping;
            LocationKind     nominalLoc = LocationKind::Home;

            switch (c.archetype) {
                case CitizenArchetype::Blacksmith:
                    if (HourInInterval(h, 7.0f, 18.0f)) {
                        nominalAct = ScheduleActivity::Working;
                        nominalLoc = LocationKind::Smithy;
                    } else if (HourInInterval(h, 18.0f, 22.0f)) {
                        nominalAct = ScheduleActivity::TavernRest;
                        nominalLoc = LocationKind::Tavern;
                    } else {
                        nominalAct = ScheduleActivity::Sleeping;
                        nominalLoc = LocationKind::Home;
                    }
                    break;

                case CitizenArchetype::Merchant:
                    if (HourInInterval(h, 8.0f, 19.0f) && ctx.settlementMarketOpen) {
                        nominalAct = ScheduleActivity::Working;
                        nominalLoc = LocationKind::Market;
                    } else if (HourInInterval(h, 19.0f, 22.5f)) {
                        nominalAct = ScheduleActivity::TavernRest;
                        nominalLoc = LocationKind::Tavern;
                    } else {
                        nominalAct = ScheduleActivity::Sleeping;
                        nominalLoc = LocationKind::Home;
                    }
                    break;

                case CitizenArchetype::Farmer: {
                    // Farmers follow the seasonal astronomical daylight window!
                    const float workStart = std::max(5.5f, sunrise);
                    const float workEnd   = std::min(19.5f, sunset);
                    if (h >= workStart && h < workEnd) {
                        nominalAct = ScheduleActivity::Working;
                        nominalLoc = LocationKind::Farm;
                    } else if (HourInInterval(h, workEnd, 21.5f)) {
                        nominalAct = ScheduleActivity::TavernRest;
                        nominalLoc = LocationKind::Tavern;
                    } else {
                        nominalAct = ScheduleActivity::Sleeping;
                        nominalLoc = LocationKind::Home;
                    }
                    break;
                }

                case CitizenArchetype::Innkeeper:
                    // Innkeepers operate tavern shifts morning through late night (indoor!).
                    if (c.shiftVariant == 0u ? HourInInterval(h, 7.0f, 23.0f) : HourInInterval(h, 11.0f, 2.0f)) {
                        nominalAct = ScheduleActivity::Working;
                        nominalLoc = LocationKind::Tavern;
                    } else {
                        nominalAct = ScheduleActivity::Sleeping;
                        nominalLoc = LocationKind::Tavern;
                    }
                    break;

                case CitizenArchetype::Hunter: {
                    const float huntStart = std::max(5.0f, sunrise - 0.5f);
                    const float huntEnd   = std::min(17.5f, sunset - 0.5f);
                    if (h >= huntStart && h < huntEnd) {
                        nominalAct = ScheduleActivity::Working;
                        nominalLoc = LocationKind::Wilds;
                    } else if (HourInInterval(h, huntEnd, 22.0f)) {
                        nominalAct = ScheduleActivity::TavernRest;
                        nominalLoc = LocationKind::Tavern;
                    } else {
                        nominalAct = ScheduleActivity::Sleeping;
                        nominalLoc = LocationKind::Home;
                    }
                    break;
                }

                case CitizenArchetype::Mage:
                    // Mages study indoors afternoon through midnight (astronomical & arcane shifts).
                    if (HourInInterval(h, 11.0f, 23.5f)) {
                        nominalAct = ScheduleActivity::Working;
                        nominalLoc = LocationKind::ArcaneStudy;
                    } else if (HourInInterval(h, 9.0f, 11.0f)) {
                        nominalAct = ScheduleActivity::TavernRest;
                        nominalLoc = LocationKind::Tavern;
                    } else {
                        nominalAct = ScheduleActivity::Sleeping;
                        nominalLoc = LocationKind::ArcaneStudy;
                    }
                    break;

                case CitizenArchetype::Priest:
                    // Priests tend indoor Temple services dawn through evening.
                    if (HourInInterval(h, 6.0f, 20.0f)) {
                        nominalAct = ScheduleActivity::Working;
                        nominalLoc = LocationKind::Temple;
                    } else if (HourInInterval(h, 20.0f, 22.0f)) {
                        nominalAct = ScheduleActivity::TavernRest;
                        nominalLoc = LocationKind::Tavern;
                    } else {
                        nominalAct = ScheduleActivity::Sleeping;
                        nominalLoc = LocationKind::Temple;
                    }
                    break;

                default:
                    nominalAct = ScheduleActivity::Sleeping;
                    nominalLoc = LocationKind::Home;
                    break;
            }

            // Weather shelter override: outdoor work is driven indoors to Tavern (day/evening) or Home (night).
            if (weatherShelter && !IsLocationIndoors(nominalLoc)) {
                c.activity = ScheduleActivity::ShelteringWeather;
                c.location = HourInInterval(h, 8.0f, 22.5f) ? LocationKind::Tavern : LocationKind::Home;
            } else {
                c.activity = nominalAct;
                c.location = nominalLoc;
            }
        }
    }

    c.isIndoors = IsLocationIndoors(c.location);

    // 4. Integrate energy, morale, and work-shift production deterministically over dtHours:
    switch (c.activity) {
        case ScheduleActivity::Working: {
            c.hoursWorked += dtHours;
            c.energy01 = SafeClamp01(c.energy01 - 0.055f * dtHours);
            const float eff = (0.45f + 0.55f * c.energy01) * (0.50f + 0.50f * c.morale01);
            const float out = eff * dtHours;
            c.workOutputTotal       += out;
            ledger_.totalWorkOutput += out;
            switch (c.archetype) {
                case CitizenArchetype::Farmer:     ledger_.grainWork       += out; break;
                case CitizenArchetype::Blacksmith: ledger_.smithingWork    += out; break;
                case CitizenArchetype::Merchant:   ledger_.tradeWork       += out; break;
                case CitizenArchetype::Hunter:     ledger_.huntingWork     += out; break;
                case CitizenArchetype::Innkeeper:  ledger_.hospitalityWork += out; break;
                case CitizenArchetype::Mage:       ledger_.arcaneWork      += out; break;
                case CitizenArchetype::Priest:     ledger_.templeWork      += out; break;
                default: break;
            }
            // Mild morale satisfaction from productive work, slight penalty if cold outside:
            const float coldPenalty = (!c.isIndoors && ctx.ambientTempC < 0.0f) ? 0.025f : 0.0f;
            c.morale01 = SafeClamp01(c.morale01 + (0.008f - coldPenalty) * dtHours);
            break;
        }

        case ScheduleActivity::Patrolling: {
            c.hoursWorked           += dtHours;
            ledger_.guardPatrolHrs  += dtHours;
            c.energy01               = SafeClamp01(c.energy01 - 0.065f * dtHours);
            const float brazierBonus = braziersLit ? 0.010f : 0.0f;
            const float stormDrain   = 0.030f * ctx.stormSeverity01;
            c.morale01               = SafeClamp01(c.morale01 + (brazierBonus - stormDrain - 0.005f) * dtHours);
            break;
        }

        case ScheduleActivity::Sleeping: {
            c.hoursSlept += dtHours;
            c.energy01    = SafeClamp01(c.energy01 + 0.145f * dtHours);
            c.morale01    = SafeClamp01(c.morale01 + 0.045f * dtHours);
            break;
        }

        case ScheduleActivity::TavernRest: {
            c.hoursInTavern += dtHours;
            const float hostBonus    = innkeeperActive ? 0.040f : 0.015f;
            const float warmthBonus  = braziersLit ? 0.020f : 0.008f;
            c.energy01               = SafeClamp01(c.energy01 + 0.040f * dtHours);
            c.morale01               = SafeClamp01(c.morale01 + (0.065f + hostBonus + warmthBonus) * dtHours);
            break;
        }

        case ScheduleActivity::ShelteringWeather: {
            c.hoursSheltered += dtHours;
            const bool inTavern = (c.location == LocationKind::Tavern);
            c.energy01          = SafeClamp01(c.energy01 + 0.035f * dtHours);
            const float mDelta  = inTavern ? (innkeeperActive ? 0.035f : 0.015f) : -0.010f;
            c.morale01          = SafeClamp01(c.morale01 + mDelta * dtHours);
            break;
        }

        case ScheduleActivity::CurfewLockdown: {
            c.hoursSheltered += dtHours;
            c.energy01        = SafeClamp01(c.energy01 + 0.090f * dtHours);
            c.morale01        = SafeClamp01(c.morale01 - 0.025f * dtHours);
            break;
        }

        case ScheduleActivity::EvacuatingWildfire: {
            c.hoursEvacuated += dtHours;
            c.energy01        = SafeClamp01(c.energy01 - 0.040f * dtHours);
            const float calm  = priestActive ? 0.018f : 0.0f;
            c.morale01        = SafeClamp01(c.morale01 - (0.075f - calm) * dtHours);
            break;
        }

        case ScheduleActivity::Traveling:
        default: {
            c.energy01 = SafeClamp01(c.energy01 - 0.030f * dtHours);
            break;
        }
    }
}

void RawLivingWorldSchedule::StepHours(float dtHours, const LivingWorldContext& ctx) {
    if (!std::isfinite(dtHours) || dtHours <= 0.0f) {
        return;
    }

    // Sub-step in <= 1.0 hour increments so 24-hour clock transitions stay crisp and accurate:
    const int   steps = std::max(1, static_cast<int>(std::ceil(dtHours)));
    const float subDt = dtHours / static_cast<float>(steps);

    LivingWorldContext stepCtx = ctx;
    for (int s = 0; s < steps; ++s) {
        stepCtx.timeOfDayHours = WrapHour24(ctx.timeOfDayHours + static_cast<float>(s) * subDt);
        lastCtx_               = stepCtx;
        hold_                  = stepCtx.hold;

        const float h       = stepCtx.timeOfDayHours;
        const float dayLen  = ClampRange(stepCtx.dayLengthHours, 6.0f, 18.0f);
        const float sunrise = 12.0f - 0.5f * dayLen;
        const float sunset  = 12.0f + 0.5f * dayLen;
        const bool  isDark  = (h < sunrise || h >= sunset || stepCtx.solarElevation < 0.05f);

        // Town braziers ignite at night, in freezing cold (< 2 C), or during blizzards/storms!
        const bool severeStorm    = (stepCtx.blizzardSeverity01 >= 0.55f || stepCtx.stormSeverity01 >= 0.70f);
        const bool freezingCold   = (stepCtx.ambientTempC <= 2.0f);
        braziersLit_              = !stepCtx.wildfireActive && (isDark || severeStorm || freezingCold);
        brazierIntensity_         = braziersLit_
            ? SafeClamp01(0.45f + (isDark ? 0.30f : 0.0f) + 0.30f * stepCtx.blizzardSeverity01 +
                      (freezingCold ? 0.15f : 0.0f))
            : 0.0f;

        outerPatrolsCancelled_ = (stepCtx.blizzardSeverity01 >= 0.65f);

        // Check whether at least one Innkeeper / Priest is present in the hold:
        bool innkeeperActive = false;
        bool priestActive    = false;
        for (const auto& c : citizens_) {
            if (c.archetype == CitizenArchetype::Innkeeper) innkeeperActive = true;
            if (c.archetype == CitizenArchetype::Priest)    priestActive    = true;
        }

        for (auto& c : citizens_) {
            EvaluateCitizenSchedule(c, subDt, stepCtx, braziersLit_, innkeeperActive, priestActive,
                                    outerPatrolsCancelled_);
        }

        UpdateWildlife(stepCtx);
        simHoursElapsed_ += subDt;
    }

    // Advance recorded clock to end of step:
    lastCtx_.timeOfDayHours = WrapHour24(ctx.timeOfDayHours + dtHours);
}

void RawLivingWorldSchedule::StepFromSystems(float                      dtHours,
                                             HoldId                     hold,
                                             const RawSettlementSim*    settlement,
                                             const RawWeatherSim*       weather,
                                             const RawFactionDiplomacy* diplomacy,
                                             const RawWorldEcosystem*   ecosystem,
                                             bool                       wartimeCurfewOverride) {
    const LivingWorldContext ctx =
        BuildLivingWorldContext(hold, settlement, weather, diplomacy, ecosystem, wartimeCurfewOverride);
    StepHours(dtHours, ctx);
}

void RawLivingWorldSchedule::ApplyToHoldState(HoldState& holdState) const {
    const LivingWorldTelemetry t = ComputeTelemetry();
    holdState.marketOpen         = t.marketOpen;
    holdState.activeGuards       = std::max(2u, t.patrollingCount);
    const float tavRatio         = (t.totalCitizens > 0u)
        ? SafeClamp01(static_cast<float>(t.tavernCount) / std::max(1.0f, 0.40f * static_cast<float>(t.totalCitizens)))
        : 0.15f;
    holdState.tavernOccupancy01  = ClampRange(tavRatio, 0.05f, 1.0f);
}

LivingWorldTelemetry RawLivingWorldSchedule::ComputeTelemetry() const {
    LivingWorldTelemetry t{};
    t.hold                     = hold_;
    t.totalCitizens            = static_cast<uint32_t>(citizens_.size());
    t.braziersLit              = braziersLit_;
    t.brazierIntensity01       = brazierIntensity_;
    t.outdoorPatrolsCancelled  = outerPatrolsCancelled_;
    t.wartimeCurfewActive      = lastCtx_.wartimeCurfewActive;
    t.wildfireEvacuationActive = lastCtx_.wildfireActive;
    t.totalWorkOutput          = ledger_.totalWorkOutput;
    t.timeOfDayHours           = lastCtx_.timeOfDayHours;
    t.simHoursElapsed          = simHoursElapsed_;

    uint32_t marketWorkers = 0u;
    double   sumEnergy     = 0.0;
    double   sumMorale     = 0.0;
    double   sumUrgency    = 0.0;

    for (const auto& c : citizens_) {
        if (c.isIndoors) {
            ++t.indoorsCount;
        } else {
            ++t.outdoorsCount;
        }
        if (c.location == LocationKind::Tavern) {
            ++t.tavernCount;
        }
        if (c.location == LocationKind::Market && c.activity == ScheduleActivity::Working) {
            ++marketWorkers;
        }
        switch (c.activity) {
            case ScheduleActivity::Working:            ++t.workingCount; break;
            case ScheduleActivity::Patrolling:         ++t.patrollingCount; break;
            case ScheduleActivity::Sleeping:           ++t.sleepingCount; break;
            case ScheduleActivity::ShelteringWeather:  ++t.shelteringWeatherCount; break;
            case ScheduleActivity::CurfewLockdown:     ++t.curfewCount; break;
            case ScheduleActivity::EvacuatingWildfire: ++t.evacuatingCount; break;
            default: break;
        }
        sumEnergy  += c.energy01;
        sumMorale  += c.morale01;
        sumUrgency += c.shelterUrgency01;
    }

    t.marketOpen = (marketWorkers > 0u) && !lastCtx_.wildfireActive;

    if (t.totalCitizens > 0u) {
        const double inv        = 1.0 / static_cast<double>(t.totalCitizens);
        t.meanEnergy01          = static_cast<float>(sumEnergy * inv);
        t.meanMorale01          = static_cast<float>(sumMorale * inv);
        t.meanShelterUrgency01  = static_cast<float>(sumUrgency * inv);
    }

    float preySum = 0.0f;
    int   preyCnt = 0;
    float predSum = 0.0f;
    int   predCnt = 0;
    bool  flee    = false;

    for (const auto& w : wildlife_) {
        if (w.fleeingFire) flee = true;
        if (w.predator) {
            predSum += w.activity01;
            ++predCnt;
        } else {
            preySum += w.activity01;
            ++preyCnt;
        }
    }

    t.diurnalPreyActivity01   = (preyCnt > 0) ? (preySum / static_cast<float>(preyCnt)) : 0.0f;
    t.nocturnalPredatorAct01  = (predCnt > 0) ? (predSum / static_cast<float>(predCnt)) : 0.0f;
    t.wildlifeFleeingFire     = flee;
    t.predatorEncounterRisk01 = flee
        ? 0.05f
        : SafeClamp01(t.nocturnalPredatorAct01 * (t.outdoorsCount > 0u ? 0.85f : 0.45f));

    return t;
}

}  // namespace raw::worldgen
