#pragma once
// RawSettlementSim.h: Clean-Room 9-Hold Living Settlement, Weather-Driven Trade Caravan & Supply/Demand
// Economy Simulation (`raw::worldgen`). Pure portable C++23 with zero Windows or D3D12 headers.
// Models all 9 Skyrim Holds connected by a 15-edge Tamriel road-distance & mountain-pass graph:
//   - 6 Commodities (Grain, Timber, IronOre, Pelts, Mead, SoulGems) with hold production & consumption
//   - Autonomous surplus->deficit trade caravans with exact transit commodity & gold conservation
//   - Regional blizzard pass blockades (`blizzard01 > 0.6` snowbounds caravans -> scarcity price spikes!)
//   - Daily living settlement activity (`marketOpen`, `activeGuards`, `tavernOccupancy01` from time/season/blizzard).
// Original work, MIT.

#include <array>
#include <cstdint>
#include <vector>

namespace raw::worldgen {

enum class HoldId : uint8_t {
    Whiterun   = 0,
    Solitude   = 1,
    Windhelm   = 2,
    Riften     = 3,
    Markarth   = 4,
    Falkreath  = 5,
    Morthal    = 6,
    Dawnstar   = 7,
    Winterhold = 8,
    Count      = 9,
};

enum class CommodityKind : uint8_t {
    Grain    = 0,
    Timber   = 1,
    IronOre  = 2,
    Pelts    = 3,
    Mead     = 4,
    SoulGems = 5,
    Count    = 6,
};

inline constexpr size_t kHoldCount      = static_cast<size_t>(HoldId::Count);
inline constexpr size_t kCommodityCount = static_cast<size_t>(CommodityKind::Count);

struct RoadRoute {
    HoldId a                      = HoldId::Whiterun;
    HoldId b                      = HoldId::Falkreath;
    float  distanceKm             = 24.0f;
    float  mountainPassExposure01 = 0.35f; // [0..1] severity of alpine pass snowdrifts
    float  banditRisk01           = 0.15f; // [0..1] chance of roadside ambush delay
};

struct HoldWeather {
    float blizzard01 = 0.0f; // [0..1]; > 0.6 snowbounds caravans touching this hold
    float rain01     = 0.0f; // [0..1]
    float tempC      = 6.0f; // Local ambient temperature
};

struct SettlementWeatherMap {
    std::array<HoldWeather, kHoldCount> byHold{};
    float seasonDaylightHours         = 12.0f; // [6..18] astronomical daylight duration
    bool  enableProductionConsumption = true;  // Set false to verify exact transit conservation
    bool  enableBanditAmbushes        = true;
};

struct HoldState {
    HoldId                             id                = HoldId::Whiterun;
    const char*                        name              = "";
    float                              treasuryGold      = 5000.0f;
    std::array<float, kCommodityCount> stock{};
    std::array<float, kCommodityCount> targetStock{};
    std::array<float, kCommodityCount> prodPerHour{};
    std::array<float, kCommodityCount> consPerHour{};
    std::array<float, kCommodityCount> price{};

    // Daily living settlement schedule & shelter state:
    bool     marketOpen        = true;
    uint32_t activeGuards      = 12u;
    float    tavernOccupancy01 = 0.30f;
};

struct TradeCaravan {
    uint32_t      id                     = 0u;
    HoldId        originHold             = HoldId::Whiterun;
    HoldId        destHold               = HoldId::Winterhold;
    CommodityKind commodity              = CommodityKind::Grain;
    float         cargoUnits             = 0.0f;
    float         totalDistanceKm        = 28.0f;
    float         traveledKm             = 0.0f;
    float         baseSpeedKmh           = 4.5f;
    float         mountainPassExposure01 = 0.5f;
    float         banditRisk01           = 0.15f;
    float         banditDelayHours       = 0.0f;
    bool          snowbound              = false;
    bool          active                 = true;
};

struct SettlementTelemetry {
    double   totalGold           = 0.0;
    uint32_t totalCaravansActive = 0u;
    uint32_t snowboundCaravans   = 0u;
    uint32_t deliveriesCompleted = 0u;
    float    meanPriceIndex      = 1.0f;
    HoldId   mostScarceHold      = HoldId::Winterhold;
    float    simHoursElapsed     = 0.0f;
    float    timeOfDayHours      = 8.0f;
};

const char* GetHoldName(HoldId id);
const char* GetCommodityName(CommodityKind c);
float       GetCommodityBasePrice(CommodityKind c);

class RawSettlementSim {
public:
    explicit RawSettlementSim(uint32_t seed = 202652u);

    void Reset(uint32_t seed = 202652u);

    // Advances the 9-hold living economy & caravan network deterministically by `hours`.
    void StepHours(float hours, const SettlementWeatherMap& weather);

    // Safe read-only & mutable hold accessors (returns nullptr if out-of-range HoldId).
    const HoldState* GetHold(HoldId id) const;
    HoldState*       GetHoldMutable(HoldId id);

    const std::vector<RoadRoute>&    Routes() const noexcept { return routes_; }
    const std::vector<TradeCaravan>& Caravans() const noexcept { return caravans_; }

    // Computes exact total commodity quantity (all holds' stocks + all active caravans' cargo).
    double TotalWorldCommodityUnits(CommodityKind c) const;

    SettlementTelemetry ComputeWorldTelemetry() const;

private:
    struct PathInfo {
        bool  reachable              = false;
        float totalDistanceKm        = 0.0f;
        float maxPassExposure01      = 0.0f;
        float maxBanditRisk01        = 0.0f;
    };

    void     InitRoutes();
    void     InitHolds();
    void     RecomputeAllPrices();
    void     DispatchCaravans(const SettlementWeatherMap& weather);
    void     UpdateLivingSchedules(const SettlementWeatherMap& weather);
    PathInfo ComputeShortestRoadPath(HoldId from, HoldId to) const;
    uint32_t NextRng();

    std::array<HoldState, kHoldCount> holds_{};
    std::vector<RoadRoute>            routes_;
    std::vector<TradeCaravan>         caravans_;
    uint32_t                          nextCaravanId_       = 1u;
    uint32_t                          deliveriesCompleted_ = 0u;
    uint32_t                          rngState_            = 202652u;
    float                             simHoursElapsed_     = 0.0f;
    float                             timeOfDayHours_      = 8.0f;
    float                             dispatchAccumHours_  = 0.0f;
};

}  // namespace raw::worldgen
