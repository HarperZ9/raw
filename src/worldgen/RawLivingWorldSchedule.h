#pragma once
// RawLivingWorldSchedule.h: Clean-Room 24-Hour Weather, War & Wildfire-Coupled Living World NPC
// and Wildlife Schedule Engine (`raw::worldgen`). Pure portable C++23 with zero Windows or D3D12 headers.
// Couples:
//   - RawSettlementSim   (markets, inns, smithies, farms, hold commodity context & living state sync)
//   - RawWeatherSim      (seasonal daylight, blizzards/storms -> indoor shelter, town braziers, patrol recall)
//   - RawFactionDiplomacy (wartime curfews, civil-war tension & extra guard patrols)
//   - RawWorldEcosystem  (active wildfire detection -> immediate civilian evacuation & wildlife flight)
// Models 32+ citizens across 8 archetypes (Blacksmith, Merchant, Guard, Farmer, Innkeeper, Hunter,
// Mage, Priest) plus diurnal prey and nocturnal predator wildlife bands over a 24-hour seasonal clock.
// Original work, MIT.

#include "RawFactionDiplomacy.h"
#include "RawSettlementSim.h"
#include "RawWeatherSim.h"
#include "RawWorldEcosystem.h"

#include <array>
#include <cstdint>
#include <vector>

namespace raw::worldgen {

enum class CitizenArchetype : uint8_t {
    Blacksmith = 0,
    Merchant   = 1,
    Guard      = 2,
    Farmer     = 3,
    Innkeeper  = 4,
    Hunter     = 5,
    Mage       = 6,
    Priest     = 7,
    Count      = 8,
};

inline constexpr size_t kCitizenArchetypeCount = static_cast<size_t>(CitizenArchetype::Count);

enum class LocationKind : uint8_t {
    Home              = 0,
    Tavern            = 1,
    Market            = 2,
    Smithy            = 3,
    Farm              = 4,
    Barracks          = 5,
    Temple            = 6,
    ArcaneStudy       = 7,
    PatrolRoute       = 8,
    Wilds             = 9,
    EvacuationShelter = 10,
    Count             = 11,
};

enum class ScheduleActivity : uint8_t {
    Sleeping          = 0,
    Working           = 1,
    Patrolling        = 2,
    TavernRest        = 3,
    ShelteringWeather = 4,
    CurfewLockdown    = 5,
    EvacuatingWildfire = 6,
    Traveling         = 7,
    Count             = 8,
};

enum class WildlifeGuild : uint8_t {
    ElkHerd         = 0, // Diurnal prey
    HareWarren      = 1, // Diurnal / dawn prey
    TimberWolfPack  = 2, // Nocturnal pack predator
    SabreCatStalker = 3, // Nocturnal ambush predator
    Count           = 4,
};

inline constexpr size_t kWildlifeGuildCount = static_cast<size_t>(WildlifeGuild::Count);

struct CitizenDesc {
    uint32_t         id           = 0u;
    CitizenArchetype archetype    = CitizenArchetype::Farmer;
    uint8_t          shiftVariant = 0u;    // 0 = primary/day shift, 1 = secondary/night or late shift
    float            resilience01 = 0.50f; // Cold/storm tolerance [0..1]
    float            initialEnergy01 = 0.85f;
    float            initialMorale01 = 0.78f;
};

struct CitizenAgent {
    uint32_t         id                = 0u;
    CitizenArchetype archetype         = CitizenArchetype::Farmer;
    uint8_t          shiftVariant      = 0u;
    float            resilience01      = 0.50f;
    ScheduleActivity activity          = ScheduleActivity::Sleeping;
    LocationKind     location          = LocationKind::Home;
    bool             isIndoors         = true;
    float            energy01          = 0.85f;
    float            morale01          = 0.78f;
    float            shelterUrgency01  = 0.0f;
    float            workOutputTotal   = 0.0f;
    float            hoursWorked       = 0.0f;
    float            hoursSlept        = 0.0f;
    float            hoursInTavern     = 0.0f;
    float            hoursSheltered    = 0.0f;
    float            hoursEvacuated    = 0.0f;
};

struct WildlifeBandState {
    WildlifeGuild guild          = WildlifeGuild::ElkHerd;
    bool          nocturnal      = false;
    bool          predator       = false;
    bool          fleeingFire    = false;
    bool          shelteringCold = false;
    float         activity01     = 0.0f; // [0..1] active foraging / hunting level
};

struct LivingWorldContext {
    HoldId        hold                 = HoldId::Whiterun;
    float         timeOfDayHours       = 8.0f;  // [0, 24)
    float         dayLengthHours       = 12.0f; // [6, 18] seasonal daylight duration
    float         solarElevation       = 0.25f; // [-1, +1]
    SeasonId      season               = SeasonId::Summer;
    WeatherRegime weatherRegime        = WeatherRegime::ClearAlpine;
    float         ambientTempC         = 10.0f;
    float         windSpeedMps         = 3.0f;
    float         rain01               = 0.0f;
    float         snow01               = 0.0f;
    float         blizzardSeverity01   = 0.0f;  // [0..1]
    float         stormSeverity01      = 0.0f;  // [0..1]
    bool          settlementMarketOpen = true;
    uint32_t      settlementGuards     = 12u;
    float         settlementTavernOcc  = 0.30f;
    GuardPosture  guardPosture         = GuardPosture::Peaceful;
    float         civilWarTension01    = 0.0f;  // [0..1] from faction rivalry / hostile holds
    bool          wartimeCurfewActive  = false;
    uint32_t      activeFireCells      = 0u;
    float         ecosystemPeakTempC   = 12.0f;
    bool          wildfireActive       = false;
};

struct WorkShiftLedger {
    float totalWorkOutput = 0.0f;
    float grainWork       = 0.0f; // Farmer
    float smithingWork    = 0.0f; // Blacksmith
    float tradeWork       = 0.0f; // Merchant
    float huntingWork     = 0.0f; // Hunter
    float hospitalityWork = 0.0f; // Innkeeper
    float arcaneWork      = 0.0f; // Mage
    float templeWork      = 0.0f; // Priest
    float guardPatrolHrs  = 0.0f; // Guard
};

struct LivingWorldTelemetry {
    HoldId   hold                     = HoldId::Whiterun;
    uint32_t totalCitizens            = 0u;
    uint32_t indoorsCount             = 0u;
    uint32_t outdoorsCount            = 0u;
    uint32_t workingCount             = 0u;
    uint32_t patrollingCount          = 0u;
    uint32_t sleepingCount            = 0u;
    uint32_t tavernCount              = 0u;
    uint32_t shelteringWeatherCount   = 0u;
    uint32_t curfewCount              = 0u;
    uint32_t evacuatingCount          = 0u;
    bool     marketOpen               = false;
    bool     braziersLit              = false;
    float    brazierIntensity01       = 0.0f;
    bool     outdoorPatrolsCancelled  = false;
    bool     wartimeCurfewActive      = false;
    bool     wildfireEvacuationActive = false;
    float    meanEnergy01             = 0.0f;
    float    meanMorale01             = 0.0f;
    float    meanShelterUrgency01     = 0.0f;
    float    totalWorkOutput          = 0.0f;
    float    diurnalPreyActivity01    = 0.0f;
    float    nocturnalPredatorAct01   = 0.0f;
    float    predatorEncounterRisk01  = 0.0f;
    bool     wildlifeFleeingFire      = false;
    float    timeOfDayHours           = 8.0f;
    float    simHoursElapsed          = 0.0f;
};

const char* GetArchetypeName(CitizenArchetype archetype);
const char* GetLocationName(LocationKind loc);
const char* GetActivityName(ScheduleActivity act);
const char* GetWildlifeGuildName(WildlifeGuild guild);
bool        IsLocationIndoors(LocationKind loc);

// Pure helper: extracts a unified LivingWorldContext from any subset of the 4 live world systems.
LivingWorldContext BuildLivingWorldContext(HoldId                     hold,
                                           const RawSettlementSim*    settlement,
                                           const RawWeatherSim*       weather,
                                           const RawFactionDiplomacy* diplomacy,
                                           const RawWorldEcosystem*   ecosystem,
                                           bool                       wartimeCurfewOverride = false);

// Deterministic shelter-seeking probability [0..1] from ambient weather & citizen resilience.
float EvaluateStormShelterProbability(float ambientTempC,
                                      float windSpeedMps,
                                      float rain01,
                                      float snow01,
                                      float blizzard01,
                                      float resilience01);

class RawLivingWorldSchedule {
public:
    explicit RawLivingWorldSchedule(uint32_t seed = 202661u);

    void Reset(uint32_t seed = 202661u);

    // Populates a balanced roster of citizens across all 8 archetypes (clamped to >= 32 if < 32).
    // Returns number of citizens created.
    uint32_t PopulateDefaultHold(HoldId hold = HoldId::Whiterun, uint32_t citizenCount = 36u);

    // Adds a single custom citizen (validates archetype < Count and finite attributes).
    bool AddCitizen(const CitizenDesc& desc);

    // Advances schedules, shelter-seeking, work output, morale/energy recovery, town braziers,
    // and wildlife bands by `dtHours`. Zero/negative/non-finite dtHours is an exact no-op.
    void StepHours(float dtHours, const LivingWorldContext& ctx);

    // Convenience overload pulling live state directly from the 4 coupled world systems.
    void StepFromSystems(float                      dtHours,
                         HoldId                     hold,
                         const RawSettlementSim*    settlement,
                         const RawWeatherSim*       weather,
                         const RawFactionDiplomacy* diplomacy,
                         const RawWorldEcosystem*   ecosystem,
                         bool                       wartimeCurfewOverride = false);

    // Optional bridge pushing live market/guard/tavern occupancy back into a RawSettlementSim hold.
    void ApplyToHoldState(HoldState& holdState) const;

    const std::vector<CitizenAgent>&                         Citizens() const noexcept { return citizens_; }
    const std::array<WildlifeBandState, kWildlifeGuildCount>& Wildlife() const noexcept { return wildlife_; }
    const WorkShiftLedger&                                   WorkLedger() const noexcept { return ledger_; }
    const LivingWorldContext&                                LastContext() const noexcept { return lastCtx_; }

    LivingWorldTelemetry ComputeTelemetry() const;

private:
    void InitWildlifeBands();
    void UpdateWildlife(const LivingWorldContext& ctx);
    void EvaluateCitizenSchedule(CitizenAgent&             c,
                                 float                     dtHours,
                                 const LivingWorldContext& ctx,
                                 bool                      braziersLit,
                                 bool                      innkeeperActive,
                                 bool                      priestActive,
                                 bool                      cancelOuterPatrols);
    uint32_t NextRng();

    HoldId                                             hold_            = HoldId::Whiterun;
    uint32_t                                           rngState_        = 202661u;
    uint32_t                                           nextCitizenId_   = 1u;
    float                                              simHoursElapsed_ = 0.0f;
    bool                                               braziersLit_     = false;
    float                                              brazierIntensity_ = 0.0f;
    bool                                               outerPatrolsCancelled_ = false;
    LivingWorldContext                                 lastCtx_{};
    WorkShiftLedger                                    ledger_{};
    std::vector<CitizenAgent>                          citizens_;
    std::array<WildlifeBandState, kWildlifeGuildCount> wildlife_{};
};

}  // namespace raw::worldgen
