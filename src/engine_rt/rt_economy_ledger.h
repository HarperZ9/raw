#pragma once
// rt_economy_ledger.h: Persistent 9-Hold Living Skyrim Economy Ledger & Dynamic NPC Weather/Night Ecosystem Heuristics.
// Zero-SKSE native Creation Engine simulation uniting:
//   - Accurate Tamriel (x, y) worldspace classification across all 9 Skyrim Holds
//   - 5 live regional commodity markets (FoodGrain, TimberFuel, OreMetal, FursClothing, AlchemySupplies)
//   - 14 connected inter-hold road/coastal caravan routes with blizzard & storm disruption physics
//   - 365-day Tamriel seasonal harvest/winter supply-demand curves
//   - Player barter market impact (local oversupply sell-price depression + multi-day caravan export recovery)
//   - Atomic fail-closed JSON persistence (raw_engine_out\economy_ledger.json)
//   - Dynamic NPC weather/night shelter, hearth & torch heuristics
// Original work, MIT.

#include "rt_combat_ai.h"

#include <array>
#include <cstdint>
#include <string>

namespace raw::rt {

enum class SkyrimHold : uint8_t {
    Whiterun   = 0,  // Whiterun (central tundra plains: grain/livestock hub)
    Haafingar  = 1,  // Solitude (NW imperial maritime port: trade/furs/alchemy)
    Eastmarch  = 2,  // Windhelm (NE volcanic/arctic coast: ore/smithing/timber)
    TheRift    = 3,  // Riften (SE autumn birch woods: honey/alchemy/timber)
    TheReach   = 4,  // Markarth (W mountain crags: silver/ore powerhouse)
    Falkreath  = 5,  // Falkreath (S deep pine forest: timber/furs)
    Winterhold = 6,  // Winterhold (NE glacier: arcane alchemy/arctic furs, severe winter food/fuel scarcity)
    ThePale    = 7,  // Dawnstar (N arctic coast: furs/fish/quicksilver, winter vulnerability)
    Hjaalmarch = 8,  // Morthal (N salt marsh: swamp alchemy/timber)
    Count      = 9,
};

enum class CommodityKind : uint8_t {
    FoodGrain       = 0,
    TimberFuel      = 1,
    OreMetal        = 2,
    FursClothing    = 3,
    AlchemySupplies = 4,
    Count           = 5,
};

inline constexpr size_t kSkyrimHoldCount    = static_cast<size_t>(SkyrimHold::Count);
inline constexpr size_t kCommodityCount     = static_cast<size_t>(CommodityKind::Count);
inline constexpr size_t kCaravanRouteCount  = 14u;

const char* SkyrimHoldName(SkyrimHold hold);
const char* SkyrimHoldCapital(SkyrimHold hold);
const char* CommodityName(CommodityKind kind);

// Maps Tamriel exterior worldspace (x, y) coordinates to the governing Skyrim Hold.
SkyrimHold ClassifySkyrimHold(float worldX, float worldY);

// Converts Tamriel calendar month (0..11, 0=Morning Star .. 11=Evening Star) & day (1..31) to 0..364 day-of-year.
uint32_t ComputeTamrielDayOfYear(float month0_11, float day1_31);

struct HoldCommodityMarket {
    float stock          = 100.0f;  // current inventory [10 .. 320], equilibrium = 100
    float baseProduction = 12.0f;   // daily baseline hold output
    float baseDemand     = 12.0f;   // daily baseline hold consumption
    float playerSurplus  = 0.0f;    // player-dumped glut (>0) or buyout deficit (<0)
    float priceIndex     = 1.0f;    // derived live market index [0.60 .. 1.90]
};

struct HoldEconomyState {
    SkyrimHold hold            = SkyrimHold::Whiterun;
    float      arcticExposure  = 0.25f;  // 0.10 (southern) .. 1.00 (glacier)
    float      localStorm01    = 0.0f;
    float      localSnowDepthU = 0.0f;
    float      scarcity01      = 0.0f;
    float      disruption01    = 0.0f;
    uint32_t   playerTrades    = 0;
    std::array<HoldCommodityMarket, kCommodityCount> markets{};
};

struct CaravanRouteState {
    SkyrimHold from            = SkyrimHold::Whiterun;
    SkyrimHold to              = SkyrimHold::Falkreath;
    float      baseCapacity    = 10.0f;  // max commodity units transported per day
    float      disruption01    = 0.0f;   // 0 (clear roads) .. 0.90 (blizzard blocked)
    float      lastFlowUnits   = 0.0f;   // total units moved across this route on last step
    bool       isArcticPass    = false;
    bool       isSeaRoute      = false;
};

struct EconomyDayStepInput {
    uint32_t dayOfYear      = 228u;  // 0..364 (Skyrim start: 17 Last Seed ~= day 228)
    float    storm01        = 0.0f;  // 0..1 regional/synoptic storm intensity
    float    snowDepthU     = 0.0f;  // 0..48 Skyrim units snow cover
    float    rain01         = 0.0f;  // 0..1 thunderstorm/rain intensity
    SkyrimHold focusHold    = SkyrimHold::Whiterun;
    bool     applyStormGlobally = true;  // true for regional front, false to weight strongly around focusHold
};

struct HoldBarterMultipliers {
    SkyrimHold    hold               = SkyrimHold::Whiterun;
    float         buyPriceMult       = 1.0f;   // >1 = merchant charges more (scarcity)
    float         sellPriceMult      = 1.0f;   // <1 = merchant pays less (oversupply)
    float         scarcity01         = 0.0f;   // 0..1 aggregate local shortage
    float         caravanDisruption01 = 0.0f;  // 0..1 connected caravan blockage
    float         seasonalModifier   = 1.0f;   // >1 winter squeeze, <1 harvest abundance
    float         barterMin          = 1.35f;  // Skyrim fBarterMin GMST target
    float         barterMax          = 3.40f;  // Skyrim fBarterMax GMST target
    float         barterBuyMin       = 1.05f;  // Skyrim fBarterBuyMin GMST target
    float         barterSellMax      = 0.95f;  // Skyrim fBarterSellMax GMST target
    uint8_t       marketTier         = 1u;     // 0=Surplus, 1=Balanced, 2=Tight, 3=Crisis
    CommodityKind dominantShortage   = CommodityKind::FoodGrain;
    CommodityKind dominantSurplus    = CommodityKind::FoodGrain;
};

struct NpcEcosystemInput {
    float      gameHour            = 12.0f;
    float      rain01              = 0.0f;
    float      snow01              = 0.0f;
    float      storm01             = 0.0f;
    float      snowDepthU          = 0.0f;
    float      windSpeedMs         = 4.0f;
    float      ambientTempC        = 12.0f;
    bool       inInterior          = false;
    bool       nearFire            = false;
    bool       inCombat            = false;
    SkyrimHold hold                = SkyrimHold::Whiterun;
    uint32_t   nearbyActors        = 0u;
    float      caravanDisruption01 = 0.0f;
};

struct NpcEcosystemDecision {
    WorldNpcDirective directive        = WorldNpcDirective::NormalRoutine;
    float             shelterUrgency01 = 0.0f;  // 0..1 drive to seek indoor shelter / brazier
    float             torchNeed01      = 0.0f;  // 0..1 drive to carry/equip torch
    float             alertness01      = 0.0f;  // 0..1 guard/patrol readiness
    bool              shouldCarryTorch = false;
    bool              shouldSeekShelter = false;
    bool              shouldWarmAtHearth = false;
};

NpcEcosystemDecision EvaluateNpcShelterTorchHeuristic(const NpcEcosystemInput& in);

class EconomyLedger {
public:
    EconomyLedger();

    void ResetToBaseline();

    // Advances the 9-Hold regional economy by 1 simulated Tamriel day:
    // applies seasonal production/demand curves, storm caravan disruption, inter-hold trade flows,
    // player surplus export/decay, and recomputes all commodity price indices.
    void StepDay(const EconomyDayStepInput& in);

    // Records player barter activity in a hold.
    // playerSoldToMerchant = true injects local surplus (depressing local sell return until caravans export it);
    // playerSoldToMerchant = false draws down local inventory (raising local scarcity/buy price).
    void RecordPlayerTrade(SkyrimHold hold, CommodityKind commodity, float quantityUnits, bool playerSoldToMerchant);

    // Records a broad mixed-goods player trade session (used when BarterMenu closes without item-level breakdown).
    void RecordPlayerBarterSession(SkyrimHold hold, float intensityUnits = 12.0f);

    // Queries live Hold-specific barter multipliers and bounded Skyrim GMST targets for BarterMenu::ProcessMessage.
    HoldBarterMultipliers QueryHoldBarterMultipliers(SkyrimHold hold,
                                                     float liveStorm01 = 0.0f,
                                                     float liveSnowDepthU = 0.0f,
                                                     uint32_t dayOfYear = 228u) const;

    const HoldEconomyState& GetHold(SkyrimHold hold) const;
    const CaravanRouteState& GetRoute(size_t index) const;
    uint32_t TotalDaysSimulated() const { return totalDaysSimulated_; }
    uint32_t TotalPlayerTrades() const { return totalPlayerTrades_; }
    uint32_t LastDayOfYear() const { return lastDayOfYear_; }
    float    LastCalendarDaysPassed() const { return lastCalendarDaysPassed_; }
    void     SetLastCalendarDaysPassed(float d) { lastCalendarDaysPassed_ = d; }

    // Clean fail-closed JSON serialization & atomic disk persistence.
    std::string SerializeToJson() const;
    bool        DeserializeFromJson(const std::string& jsonText);
    bool        SaveToJson(const std::string& filePath) const;
    bool        LoadFromJson(const std::string& filePath);

private:
    void RecomputeHoldMetrics(HoldEconomyState& h, uint32_t dayOfYear);

    std::array<HoldEconomyState, kSkyrimHoldCount>    holds_{};
    std::array<CaravanRouteState, kCaravanRouteCount> routes_{};
    uint32_t totalDaysSimulated_     = 0u;
    uint32_t totalPlayerTrades_      = 0u;
    uint32_t lastDayOfYear_          = 228u;
    float    lastCalendarDaysPassed_ = -1.0f;
};

struct EconomyLedgerTelemetry {
    uint8_t  initialized         = 0u;
    uint8_t  activeHold          = 0u;   // SkyrimHold
    uint8_t  marketTier          = 1u;
    uint8_t  npcDirective        = 0u;   // WorldNpcDirective
    uint32_t daysSimulated       = 0u;
    uint32_t dayOfYear           = 228u;
    uint32_t totalPlayerTrades   = 0u;
    uint32_t barterQueries       = 0u;
    uint32_t jsonSaves           = 0u;
    float    buyPriceMult        = 1.0f;
    float    sellPriceMult       = 1.0f;
    float    scarcity01          = 0.0f;
    float    caravanDisruption01 = 0.0f;
    float    npcShelterUrgency01 = 0.0f;
    float    npcTorchNeed01      = 0.0f;
    float    npcAlertness01      = 0.0f;
};

#ifndef RAW_SIM_PURE_TEST
// Live process-wide thread-safe singleton wrappers used by module_main.cpp, rt_world_sim.cpp, and rt_combat_ai.cpp.
std::string EconomyLedgerInit(const std::string& jsonPath);
void        EconomyLedgerShutdown();

// Called from WorldSimOnPresent: updates last exterior hold, steps days when Calendar::daysPassed advances,
// updates regional storm/caravan disruption, stores live NPC ecosystem telemetry, and auto-persists when dirty.
HoldBarterMultipliers EconomyLedgerOnWorldTick(const float playerPos[3],
                                               bool inInterior,
                                               float daysPassed,
                                               float gameMonth0_11,
                                               float gameDay1_31,
                                               float storm01,
                                               float rain01,
                                               float snowDepthU,
                                               const NpcEcosystemDecision& npcDecision);

// Called from BarterMenu::ProcessMessage (0x900580) on kShow / kUpdate:
// uses exterior coordinates or last known exterior hold when shopping inside an interior building.
HoldBarterMultipliers EconomyLedgerQueryLiveBarter(const float playerPos[3],
                                                   bool inInterior,
                                                   float storm01,
                                                   float snowDepthU,
                                                   float gameMonth0_11,
                                                   float gameDay1_31);

// Called from BarterMenu::ProcessMessage on kHide to record market impact and mark ledger dirty for persistence.
void EconomyLedgerRecordLiveBarterSession(const float playerPos[3], bool inInterior, float intensityUnits = 12.0f);

bool EconomyLedgerGetTelemetry(EconomyLedgerTelemetry* out);
bool EconomyLedgerSaveNow();
#endif

}  // namespace raw::rt
