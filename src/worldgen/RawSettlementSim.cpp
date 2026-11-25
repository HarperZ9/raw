// RawSettlementSim.cpp: Clean-Room 9-Hold Living Settlement, Weather-Driven Trade Caravan & Supply/Demand
// Economy Simulation. Original work, MIT. Zero Windows / D3D12 headers.

#include "RawSettlementSim.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace raw::worldgen {
namespace {

inline float ClampF(float v, float lo, float hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

inline float Clamp01F(float v) noexcept {
    return ClampF(v, 0.0f, 1.0f);
}

constexpr std::array<const char*, kHoldCount> kHoldNames = {
    "Whiterun", "Solitude", "Windhelm", "Riften", "Markarth",
    "Falkreath", "Morthal", "Dawnstar", "Winterhold"
};

constexpr std::array<const char*, kCommodityCount> kCommodityNames = {
    "Grain", "Timber", "IronOre", "Pelts", "Mead", "SoulGems"
};

constexpr std::array<float, kCommodityCount> kCommodityBasePrices = {
    12.0f, 16.0f, 24.0f, 18.0f, 22.0f, 65.0f
};

}  // namespace

const char* GetHoldName(HoldId id) {
    const size_t idx = static_cast<size_t>(id);
    return (idx < kHoldCount) ? kHoldNames[idx] : "UnknownHold";
}

const char* GetCommodityName(CommodityKind c) {
    const size_t idx = static_cast<size_t>(c);
    return (idx < kCommodityCount) ? kCommodityNames[idx] : "UnknownCommodity";
}

float GetCommodityBasePrice(CommodityKind c) {
    const size_t idx = static_cast<size_t>(c);
    return (idx < kCommodityCount) ? kCommodityBasePrices[idx] : 10.0f;
}

RawSettlementSim::RawSettlementSim(uint32_t seed) {
    Reset(seed);
}

uint32_t RawSettlementSim::NextRng() {
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 17;
    rngState_ ^= rngState_ << 5;
    return rngState_;
}

void RawSettlementSim::InitRoutes() {
    routes_ = {
        {HoldId::Whiterun,  HoldId::Falkreath,  22.0f, 0.25f, 0.14f},
        {HoldId::Whiterun,  HoldId::Morthal,    26.0f, 0.35f, 0.16f},
        {HoldId::Whiterun,  HoldId::Dawnstar,   32.0f, 0.65f, 0.20f},
        {HoldId::Whiterun,  HoldId::Windhelm,   38.0f, 0.72f, 0.22f},
        {HoldId::Whiterun,  HoldId::Riften,     42.0f, 0.55f, 0.24f},
        {HoldId::Whiterun,  HoldId::Markarth,   44.0f, 0.48f, 0.26f},
        {HoldId::Solitude,  HoldId::Morthal,    20.0f, 0.25f, 0.10f},
        {HoldId::Solitude,  HoldId::Markarth,   46.0f, 0.52f, 0.22f},
        {HoldId::Morthal,   HoldId::Dawnstar,   24.0f, 0.58f, 0.18f},
        {HoldId::Dawnstar,  HoldId::Winterhold, 30.0f, 0.92f, 0.20f},
        {HoldId::Windhelm,  HoldId::Winterhold, 28.0f, 0.95f, 0.18f},
        {HoldId::Windhelm,  HoldId::Riften,     36.0f, 0.68f, 0.24f},
        {HoldId::Falkreath, HoldId::Markarth,   34.0f, 0.70f, 0.28f},
        {HoldId::Falkreath, HoldId::Riften,     38.0f, 0.50f, 0.25f},
        {HoldId::Dawnstar,  HoldId::Windhelm,   34.0f, 0.80f, 0.20f},
    };
}

void RawSettlementSim::InitHolds() {
    // Commodity order: {Grain, Timber, IronOre, Pelts, Mead, SoulGems}
    struct HoldPreset {
        float treasury;
        std::array<float, kCommodityCount> initStock;
        std::array<float, kCommodityCount> target;
        std::array<float, kCommodityCount> prod;
        std::array<float, kCommodityCount> cons;
    };

    constexpr std::array<HoldPreset, kHoldCount> kPresets = {{
        // 0: Whiterun — central breadbasket & Honningbrew mead
        {9000.0f, {260, 120, 110, 130, 190,  45}, {140, 120, 120, 110, 120,  55},
                  {4.6f, 1.0f, 0.9f, 1.4f, 2.8f, 0.2f}, {1.6f, 1.2f, 1.1f, 1.0f, 1.2f, 0.4f}},
        // 1: Solitude — capital port, high gold, strong demand
        {12000.0f,{130, 110, 120, 100, 140,  75}, {160, 130, 130, 110, 130,  70},
                  {1.4f, 1.1f, 1.0f, 0.9f, 1.8f, 0.6f}, {2.0f, 1.3f, 1.2f, 1.1f, 1.5f, 0.5f}},
        // 2: Windhelm — cold eastern port, pelts & timber hub
        {8500.0f, {120, 160, 130, 180, 110,  50}, {130, 120, 120, 120, 110,  60},
                  {1.2f, 2.0f, 1.4f, 2.4f, 1.0f, 0.3f}, {1.6f, 1.2f, 1.1f, 1.0f, 1.2f, 0.4f}},
        // 3: Riften — Black-Briar mead, pelts & autumn timber
        {7500.0f, {115, 170,  90, 175, 220,  55}, {120, 120, 110, 110, 120,  55},
                  {1.3f, 2.1f, 0.7f, 2.2f, 3.2f, 0.4f}, {1.5f, 1.2f, 1.1f, 1.0f, 1.2f, 0.4f}},
        // 4: Markarth — Reach iron/silver mining powerhouse, grain deficit
        {9500.0f, { 85,  90, 260, 100,  95,  60}, {130, 110, 130, 100, 105,  60},
                  {0.7f, 0.7f, 4.4f, 0.9f, 0.8f, 0.5f}, {1.7f, 1.2f, 1.4f, 1.0f, 1.1f, 0.4f}},
        // 5: Falkreath — deep old-growth timber & hunting pelts
        {5500.0f, {110, 260,  85, 210, 100,  40}, {110, 120, 100, 110, 100,  45},
                  {1.2f, 4.2f, 0.6f, 2.8f, 0.9f, 0.2f}, {1.2f, 1.1f, 0.9f, 1.0f, 1.0f, 0.3f}},
        // 6: Morthal — northern marsh alchemy/pelts/gems, food & iron deficit
        {4500.0f, { 85, 125,  75, 165,  85,  95}, {105, 100,  95, 100,  95,  55},
                  {0.8f, 1.4f, 0.5f, 2.0f, 0.7f, 1.2f}, {1.2f, 1.0f, 0.9f, 0.9f, 0.9f, 0.3f}},
        // 7: Dawnstar — northern mining & pelts port, grain deficit
        {5500.0f, { 80,  95, 210, 175,  90,  55}, {110, 105, 110, 100, 100,  50},
                  {0.6f, 0.8f, 3.2f, 2.2f, 0.7f, 0.4f}, {1.4f, 1.1f, 1.1f, 0.9f, 1.0f, 0.3f}},
        // 8: Winterhold — College soul-gem hub, severe grain & timber scarcity!
        {6000.0f, { 68,  65,  80, 110,  75, 220}, {115, 110,  95, 100,  95,  90},
                  {0.25f,0.35f,0.6f, 1.1f, 0.5f, 3.4f}, {1.85f,1.45f,0.9f, 1.0f, 0.9f, 0.7f}},
    }};

    for (size_t i = 0u; i < kHoldCount; ++i) {
        HoldState& h = holds_[i];
        h.id           = static_cast<HoldId>(i);
        h.name         = kHoldNames[i];
        h.treasuryGold = kPresets[i].treasury;
        h.stock        = kPresets[i].initStock;
        h.targetStock  = kPresets[i].target;
        h.prodPerHour  = kPresets[i].prod;
        h.consPerHour  = kPresets[i].cons;
        h.marketOpen   = true;
        h.activeGuards = 12u;
        h.tavernOccupancy01 = 0.30f;
    }
    RecomputeAllPrices();
}

void RawSettlementSim::Reset(uint32_t seed) {
    rngState_            = (seed == 0u) ? 202652u : seed;
    caravans_.clear();
    nextCaravanId_       = 1u;
    deliveriesCompleted_ = 0u;
    simHoursElapsed_     = 0.0f;
    timeOfDayHours_      = 8.0f;
    dispatchAccumHours_  = 0.0f;
    InitRoutes();
    InitHolds();
}

const HoldState* RawSettlementSim::GetHold(HoldId id) const {
    const size_t idx = static_cast<size_t>(id);
    return (idx < kHoldCount) ? &holds_[idx] : nullptr;
}

HoldState* RawSettlementSim::GetHoldMutable(HoldId id) {
    const size_t idx = static_cast<size_t>(id);
    return (idx < kHoldCount) ? &holds_[idx] : nullptr;
}

void RawSettlementSim::RecomputeAllPrices() {
    for (HoldState& h : holds_) {
        for (size_t c = 0u; c < kCommodityCount; ++c) {
            const float ratio = h.targetStock[c] / std::max(h.stock[c], 1.0f);
            h.price[c] = kCommodityBasePrices[c] * ClampF(ratio, 0.55f, 2.85f);
        }
    }
}

RawSettlementSim::PathInfo RawSettlementSim::ComputeShortestRoadPath(HoldId from, HoldId to) const {
    PathInfo info{};
    const size_t src = static_cast<size_t>(from);
    const size_t dst = static_cast<size_t>(to);
    if (src >= kHoldCount || dst >= kHoldCount) return info;
    if (src == dst) {
        info.reachable = true;
        return info;
    }

    constexpr float kInf = 1.0e20f;
    std::array<float, kHoldCount> dist;
    std::array<float, kHoldCount> maxPass;
    std::array<float, kHoldCount> maxBandit;
    std::array<bool,  kHoldCount> visited;
    dist.fill(kInf);
    maxPass.fill(0.0f);
    maxBandit.fill(0.0f);
    visited.fill(false);

    dist[src] = 0.0f;
    for (size_t iter = 0u; iter < kHoldCount; ++iter) {
        size_t u = kHoldCount;
        float bestD = kInf;
        for (size_t i = 0u; i < kHoldCount; ++i) {
            if (!visited[i] && dist[i] < bestD) {
                bestD = dist[i];
                u = i;
            }
        }
        if (u == kHoldCount || u == dst) break;
        visited[u] = true;

        for (const RoadRoute& r : routes_) {
            const size_t a = static_cast<size_t>(r.a);
            const size_t b = static_cast<size_t>(r.b);
            size_t v = kHoldCount;
            if (a == u)      v = b;
            else if (b == u) v = a;
            else             continue;

            const float candD = dist[u] + r.distanceKm;
            if (candD < dist[v]) {
                dist[v]      = candD;
                maxPass[v]   = std::max(maxPass[u], r.mountainPassExposure01);
                maxBandit[v] = std::max(maxBandit[u], r.banditRisk01);
            }
        }
    }

    if (dist[dst] < kInf * 0.5f) {
        info.reachable         = true;
        info.totalDistanceKm   = dist[dst];
        info.maxPassExposure01 = maxPass[dst];
        info.maxBanditRisk01   = maxBandit[dst];
    }
    return info;
}

void RawSettlementSim::DispatchCaravans(const SettlementWeatherMap& weather) {
    // Cap concurrent active caravans across Tamriel to keep trade cadence realistic:
    constexpr size_t kMaxActiveCaravans = 18u;
    if (caravans_.size() >= kMaxActiveCaravans) {
        return;
    }

    // For each commodity, match highest-surplus origin with highest-scarcity (price) destination:
    for (size_t c = 0u; c < kCommodityCount && caravans_.size() < kMaxActiveCaravans; ++c) {
        size_t bestSrc = kHoldCount;
        float  bestSurplusRatio = 1.08f; // Require genuine surplus (> 108% target)
        size_t bestDst = kHoldCount;
        float  bestPriceRatio = 1.05f;   // Require scarcity demand

        for (size_t i = 0u; i < kHoldCount; ++i) {
            const float sr = holds_[i].stock[c] / std::max(1.0f, holds_[i].targetStock[c]);
            if (sr > bestSurplusRatio) {
                bestSurplusRatio = sr;
                bestSrc = i;
            }
            const float pr = holds_[i].price[c] / kCommodityBasePrices[c];
            if (pr > bestPriceRatio) {
                bestPriceRatio = pr;
                bestDst = i;
            }
        }

        if (bestSrc == kHoldCount || bestDst == kHoldCount || bestSrc == bestDst) {
            continue;
        }
        if (holds_[bestDst].price[c] < holds_[bestSrc].price[c] * 1.08f) {
            continue;
        }

        // Avoid duplicating an identical active caravan route+commodity if one already exists:
        bool alreadyEnRoute = false;
        for (const TradeCaravan& tc : caravans_) {
            if (tc.active &&
                tc.originHold == static_cast<HoldId>(bestSrc) &&
                tc.destHold == static_cast<HoldId>(bestDst) &&
                tc.commodity == static_cast<CommodityKind>(c)) {
                alreadyEnRoute = true;
                break;
            }
        }
        if (alreadyEnRoute) continue;

        const float availSurplus = std::max(0.0f, holds_[bestSrc].stock[c] - holds_[bestSrc].targetStock[c] * 0.85f);
        const float cargoQty     = std::min(38.0f, availSurplus * 0.55f);
        if (cargoQty < 6.0f) continue;

        const PathInfo path = ComputeShortestRoadPath(static_cast<HoldId>(bestSrc), static_cast<HoldId>(bestDst));
        if (!path.reachable) continue;

        // Exact conservation: deduct cargo from origin hold stock upon loading caravan!
        holds_[bestSrc].stock[c] -= cargoQty;

        TradeCaravan tc{};
        tc.id                     = nextCaravanId_++;
        tc.originHold             = static_cast<HoldId>(bestSrc);
        tc.destHold               = static_cast<HoldId>(bestDst);
        tc.commodity              = static_cast<CommodityKind>(c);
        tc.cargoUnits             = cargoQty;
        tc.totalDistanceKm        = std::max(8.0f, path.totalDistanceKm);
        tc.traveledKm             = 0.0f;
        tc.baseSpeedKmh           = 4.6f;
        tc.mountainPassExposure01 = path.maxPassExposure01;
        tc.banditRisk01           = path.maxBanditRisk01;
        tc.active                 = true;

        // Check immediate blizzard blockade status on departure:
        const float routeBliz = std::max(weather.byHold[bestSrc].blizzard01,
                                         weather.byHold[bestDst].blizzard01);
        tc.snowbound = (routeBliz > 0.60f);

        caravans_.push_back(tc);
    }
}

void RawSettlementSim::UpdateLivingSchedules(const SettlementWeatherMap& weather) {
    const float daylight = ClampF(std::isfinite(weather.seasonDaylightHours) ? weather.seasonDaylightHours : 12.0f,
                                  6.0f, 18.0f);
    const float sunriseH = 12.0f - daylight * 0.5f;
    const float sunsetH  = 12.0f + daylight * 0.5f;
    const bool  isDay    = (timeOfDayHours_ >= sunriseH && timeOfDayHours_ <= sunsetH);

    for (size_t i = 0u; i < kHoldCount; ++i) {
        HoldState& h = holds_[i];
        const HoldWeather& hw = weather.byHold[i];
        const float bliz = Clamp01F(hw.blizzard01);
        const float rain = Clamp01F(hw.rain01);

        // Outdoor markets close at night or during severe blizzards:
        h.marketOpen = isDay && (bliz < 0.72f);

        // Guard patrol count scales up slightly at night and during high wealth / low visibility:
        const uint32_t baseGuards = (h.id == HoldId::Solitude || h.id == HoldId::Whiterun || h.id == HoldId::Windhelm)
            ? 18u : 12u;
        h.activeGuards = isDay ? baseGuards : (baseGuards + 4u);
        if (bliz > 0.65f) {
            h.activeGuards = std::max(6u, h.activeGuards - 3u); // Patrols rotate indoors in whiteout
        }

        // Tavern occupancy spikes at night and during cold rain/blizzards (shelter pressure!):
        const float nightBoost   = isDay ? 0.0f : 0.42f;
        const float weatherBoost = bliz * 0.48f + rain * 0.18f;
        h.tavernOccupancy01      = Clamp01F(0.22f + nightBoost + weatherBoost);
    }
}

void RawSettlementSim::StepHours(float hours, const SettlementWeatherMap& weather) {
    if (!std::isfinite(hours) || hours <= 0.0f) {
        return;
    }

    const int   subSteps = std::clamp(static_cast<int>(std::ceil(hours / 1.0f)), 1, 1024);
    const float subDt    = hours / static_cast<float>(subSteps);

    for (int s = 0; s < subSteps; ++s) {
        simHoursElapsed_    += subDt;
        timeOfDayHours_      = std::fmod(timeOfDayHours_ + subDt, 24.0f);
        dispatchAccumHours_ += subDt;

        // 1. Local hold production & consumption (can be disabled to test exact transit conservation):
        if (weather.enableProductionConsumption) {
            for (size_t i = 0u; i < kHoldCount; ++i) {
                HoldState& h = holds_[i];
                const float bliz = Clamp01F(weather.byHold[i].blizzard01);
                // Severe blizzards slow outdoor farming/logging/mining slightly:
                const float prodFactor = 1.0f - 0.35f * bliz;
                for (size_t c = 0u; c < kCommodityCount; ++c) {
                    const float maxWarehouse = h.targetStock[c] * 3.5f;
                    const float prod = h.prodPerHour[c] * prodFactor * subDt;
                    const float cons = h.consPerHour[c] * subDt;
                    h.stock[c] = ClampF(h.stock[c] + prod - cons, 0.0f, maxWarehouse);
                }
            }
        }

        RecomputeAllPrices();

        // 2. Dispatch autonomous trade caravans periodically (every 2 simulated hours):
        if (dispatchAccumHours_ >= 2.0f) {
            dispatchAccumHours_ -= 2.0f;
            DispatchCaravans(weather);
            RecomputeAllPrices();
        }

        // 3. Advance active caravans along road network (handling blizzard stalls & bandit delays):
        for (TradeCaravan& tc : caravans_) {
            if (!tc.active) continue;

            const size_t srcIdx = static_cast<size_t>(tc.originHold);
            const size_t dstIdx = static_cast<size_t>(tc.destHold);
            const float routeBliz = std::max(Clamp01F(weather.byHold[srcIdx].blizzard01),
                                             Clamp01F(weather.byHold[dstIdx].blizzard01));
            const float routeRain = std::max(Clamp01F(weather.byHold[srcIdx].rain01),
                                             Clamp01F(weather.byHold[dstIdx].rain01));

            // Blizzard snowbound blockade on routes where blizzard01 > 0.6:
            if (routeBliz > 0.60f) {
                tc.snowbound = true;
                // Caravans shelter in place during severe blizzards (nearly 0 progress across mountain passes):
                const float crawlFactor = (tc.mountainPassExposure01 > 0.55f) ? 0.0f : 0.08f;
                tc.traveledKm += tc.baseSpeedKmh * crawlFactor * subDt;
                continue;
            }
            tc.snowbound = false;

            // Optional bandit ambush delay on remote routes (delays arrival without destroying cargo):
            if (tc.banditDelayHours > 0.0f) {
                tc.banditDelayHours = std::max(0.0f, tc.banditDelayHours - subDt);
                continue;
            }
            if (weather.enableBanditAmbushes && tc.banditRisk01 > 0.15f) {
                const float roll = static_cast<float>(NextRng() & 0xFFFFu) / 65536.0f;
                if (roll < tc.banditRisk01 * 0.015f * subDt) {
                    tc.banditDelayHours = 2.5f;
                    continue;
                }
            }

            const float speedKmh = tc.baseSpeedKmh * (1.0f - 0.22f * routeRain);
            tc.traveledKm += speedKmh * subDt;

            if (tc.traveledKm >= tc.totalDistanceKm) {
                // Exact commodity & gold delivery transfer!
                const size_t cIdx = static_cast<size_t>(tc.commodity);
                holds_[dstIdx].stock[cIdx] += tc.cargoUnits;

                const float payment = std::min(holds_[dstIdx].treasuryGold * 0.35f,
                                               tc.cargoUnits * holds_[dstIdx].price[cIdx]);
                holds_[dstIdx].treasuryGold -= payment;
                holds_[srcIdx].treasuryGold += payment;

                tc.cargoUnits = 0.0f;
                tc.active     = false;
                ++deliveriesCompleted_;
            }
        }

        // Compact completed caravans:
        caravans_.erase(
            std::remove_if(caravans_.begin(), caravans_.end(), [](const TradeCaravan& tc) {
                return !tc.active;
            }),
            caravans_.end());

        RecomputeAllPrices();
        UpdateLivingSchedules(weather);
    }
}

double RawSettlementSim::TotalWorldCommodityUnits(CommodityKind c) const {
    const size_t cIdx = static_cast<size_t>(c);
    if (cIdx >= kCommodityCount) return 0.0;
    double total = 0.0;
    for (const HoldState& h : holds_) {
        total += static_cast<double>(h.stock[cIdx]);
    }
    for (const TradeCaravan& tc : caravans_) {
        if (tc.active && tc.commodity == c) {
            total += static_cast<double>(tc.cargoUnits);
        }
    }
    return total;
}

SettlementTelemetry RawSettlementSim::ComputeWorldTelemetry() const {
    SettlementTelemetry t{};
    t.simHoursElapsed     = simHoursElapsed_;
    t.timeOfDayHours      = timeOfDayHours_;
    t.deliveriesCompleted = deliveriesCompleted_;

    double sumPriceRatio = 0.0;
    float  worstHoldIdx  = -1.0f;
    HoldId scarceHold    = HoldId::Winterhold;

    for (size_t i = 0u; i < kHoldCount; ++i) {
        const HoldState& h = holds_[i];
        t.totalGold += static_cast<double>(h.treasuryGold);
        float holdPriceSum = 0.0f;
        for (size_t c = 0u; c < kCommodityCount; ++c) {
            const float pr = h.price[c] / kCommodityBasePrices[c];
            holdPriceSum  += pr;
            sumPriceRatio += static_cast<double>(pr);
        }
        const float holdMean = holdPriceSum / static_cast<float>(kCommodityCount);
        if (holdMean > worstHoldIdx) {
            worstHoldIdx = holdMean;
            scarceHold   = h.id;
        }
    }

    for (const TradeCaravan& tc : caravans_) {
        if (tc.active) {
            ++t.totalCaravansActive;
            if (tc.snowbound) {
                ++t.snowboundCaravans;
            }
        }
    }

    t.meanPriceIndex = static_cast<float>(sumPriceRatio / static_cast<double>(kHoldCount * kCommodityCount));
    t.mostScarceHold = scarceHold;
    return t;
}

}  // namespace raw::worldgen
