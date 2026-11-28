// rt_economy_ledger.cpp: Persistent 9-Hold Living Skyrim Economy Ledger & Dynamic NPC Weather/Night Ecosystem Heuristics.
// Original work, MIT.
#include "rt_economy_ledger.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

#ifndef RAW_SIM_PURE_TEST
#include <windows.h>
#endif

namespace raw::rt {
namespace {

constexpr uint32_t kEconomySchemaVersion = 1u;

struct HoldGeoProfile {
    const char* name;
    const char* capital;
    float       anchorX;
    float       anchorY;
    float       arcticExposure;
    // Baseline daily production & demand for [FoodGrain, TimberFuel, OreMetal, FursClothing, AlchemySupplies]
    float       prod[kCommodityCount];
    float       dem[kCommodityCount];
};

// Anchors calibrated to Tamriel exterior cell coordinates (1 cell = 4096u) for all 9 Skyrim Holds.
constexpr HoldGeoProfile kHoldProfiles[kSkyrimHoldCount] = {
    // 0: Whiterun — central breadbasket plains (Whiterun 20480,-8192; Riverwood 20480,-45056; Rorikstead -86016,-8192)
    {"Whiterun",   "Whiterun",    16000.0f,  -10000.0f, 0.22f,
     {24.0f, 13.0f, 11.0f, 12.0f, 12.0f},
     {14.0f, 13.0f, 14.0f, 12.0f, 13.0f}},
    // 1: Haafingar — Solitude (-69632,102400), Dragon Bridge (-102400,65536): maritime imperial capital
    {"Haafingar",  "Solitude",   -76000.0f,   94000.0f, 0.62f,
     {13.0f, 13.0f, 11.0f, 17.0f, 16.0f},
     {16.0f, 15.0f, 14.0f, 14.0f, 15.0f}},
    // 2: Eastmarch — Windhelm (135168,32768), Kynesgrove (126976,-24576): smithing, mines, cold tundra
    {"Eastmarch",  "Windhelm",   130000.0f,   24000.0f, 0.74f,
     {10.5f, 15.0f, 19.0f, 14.0f, 11.0f},
     {15.5f, 16.5f, 14.0f, 15.0f, 12.5f}},
    // 3: TheRift — Riften (172032,-94208), Ivarstead (73728,-77824): autumn birch woods, honey & alchemy
    {"TheRift",    "Riften",     150000.0f,  -86000.0f, 0.14f,
     {15.0f, 18.0f, 10.0f, 13.0f, 22.0f},
     {14.0f, 13.0f, 13.5f, 12.0f, 13.5f}},
    // 4: TheReach — Markarth (-176128,0), Karthwasten (-147456,36864): silver & ore mining powerhouse, rocky soil
    {"TheReach",   "Markarth",  -158000.0f,    4000.0f, 0.30f,
     { 8.5f,  9.5f, 25.0f, 11.0f, 13.0f},
     {15.5f, 14.0f, 13.5f, 12.5f, 13.0f}},
    // 5: Falkreath — Falkreath (-32768,-90112), Helgen (16384,-90112): deep pine logging & hunting
    {"Falkreath",  "Falkreath",  -24000.0f,  -88000.0f, 0.16f,
     {13.5f, 24.0f, 10.0f, 18.0f, 15.0f},
     {12.5f, 12.0f, 12.5f, 12.0f, 12.5f}},
    // 6: Winterhold — Winterhold (114688,102400): glacier college, arctic pelts/alchemy, acute food & fuel scarcity
    {"Winterhold", "Winterhold", 114000.0f,  102000.0f, 1.00f,
     { 5.5f,  6.5f,  9.0f, 17.5f, 20.0f},
     {16.0f, 18.5f, 11.5f, 15.5f, 13.0f}},
    // 7: ThePale — Dawnstar (24576,106496): arctic port, furs/fish & quicksilver mines, winter exposure
    {"ThePale",    "Dawnstar",    24000.0f,   96000.0f, 0.94f,
     { 7.5f,  8.0f, 17.5f, 19.0f, 11.0f},
     {15.0f, 17.5f, 13.0f, 14.5f, 12.5f}},
    // 8: Hjaalmarch — Morthal (-61440,45056): northern salt marsh, alchemy herbs & timber
    {"Hjaalmarch", "Morthal",    -60000.0f,   42000.0f, 0.64f,
     { 9.5f, 16.5f,  8.5f, 13.5f, 19.5f},
     {13.5f, 14.5f, 12.5f, 13.5f, 12.5f}},
};

struct RouteSpec {
    SkyrimHold from;
    SkyrimHold to;
    float      baseCapacity;
    bool       isArcticPass;
    bool       isSeaRoute;
};

constexpr RouteSpec kRouteSpecs[kCaravanRouteCount] = {
    {SkyrimHold::Whiterun,   SkyrimHold::Falkreath,  15.0f, false, false},
    {SkyrimHold::Whiterun,   SkyrimHold::TheReach,   14.0f, false, false},
    {SkyrimHold::Whiterun,   SkyrimHold::Hjaalmarch, 13.0f, false, false},
    {SkyrimHold::Whiterun,   SkyrimHold::ThePale,    12.5f, true,  false},
    {SkyrimHold::Whiterun,   SkyrimHold::Eastmarch,  14.0f, true,  false},
    {SkyrimHold::Whiterun,   SkyrimHold::TheRift,    13.5f, false, false},
    {SkyrimHold::Haafingar,  SkyrimHold::Hjaalmarch, 14.0f, false, false},
    {SkyrimHold::Haafingar,  SkyrimHold::TheReach,   12.0f, false, false},
    {SkyrimHold::Haafingar,  SkyrimHold::ThePale,    13.0f, true,  true },
    {SkyrimHold::ThePale,    SkyrimHold::Winterhold, 11.0f, true,  true },
    {SkyrimHold::ThePale,    SkyrimHold::Eastmarch,  11.0f, true,  false},
    {SkyrimHold::Winterhold, SkyrimHold::Eastmarch,  11.5f, true,  false},
    {SkyrimHold::Eastmarch,  SkyrimHold::TheRift,    13.5f, false, false},
    {SkyrimHold::Falkreath,  SkyrimHold::TheRift,    12.5f, false, false},
};

constexpr uint32_t kMonthStartDoy[12] = {
    0u, 31u, 59u, 90u, 120u, 151u, 181u, 212u, 243u, 273u, 304u, 334u
};

// Returns [0..1] winter severity peaking around Morning Star / Evening Star (days 335..55).
float SeasonalWinter01(uint32_t dayOfYear) {
    const float d = static_cast<float>(dayOfYear % 365u);
    // Cosine wave peaking at day 10 (mid-January / Morning Star), trough at day 192 (mid-July / Sun's Height)
    const float rad = (d - 10.0f) * (6.28318530718f / 365.0f);
    const float raw = 0.5f + 0.5f * std::cos(rad);
    return std::clamp((raw - 0.32f) / 0.68f, 0.0f, 1.0f);
}

// Returns [0..1] autumn harvest abundance peaking around Last Seed / Hearthfire (days 215..270, peak ~day 240).
float SeasonalHarvest01(uint32_t dayOfYear) {
    const float d = static_cast<float>(dayOfYear % 365u);
    const float dist = std::fabs(d - 240.0f);
    return std::clamp(1.0f - dist / 42.0f, 0.0f, 1.0f);
}

// Minimal strict JSON tokenizer for fail-closed persistence (no external dependencies).
class StrictJsonParser {
public:
    explicit StrictJsonParser(const std::string& src) : s_(src) {}

    void SkipWs() {
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++pos_;
            else break;
        }
    }

    bool Eof() {
        SkipWs();
        return pos_ >= s_.size();
    }

    bool Consume(char expected) {
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool Peek(char c) {
        SkipWs();
        return pos_ < s_.size() && s_[pos_] == c;
    }

    bool ParseString(std::string& out) {
        SkipWs();
        if (pos_ >= s_.size() || s_[pos_] != '"') return false;
        ++pos_;
        out.clear();
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= s_.size()) return false;
                const char e = s_[pos_++];
                switch (e) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    default:   return false;
                }
            } else if (static_cast<unsigned char>(c) < 0x20u) {
                return false;
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool ParseDouble(double& out) {
        SkipWs();
        if (pos_ >= s_.size()) return false;
        const char* start = s_.c_str() + pos_;
        char* endPtr = nullptr;
        const double v = std::strtod(start, &endPtr);
        if (endPtr == start || !std::isfinite(v)) return false;
        pos_ = static_cast<size_t>(endPtr - s_.c_str());
        out = v;
        return true;
    }

    bool ParseUint(uint32_t& out) {
        double v = 0.0;
        if (!ParseDouble(v) || v < 0.0 || v > 4294967295.0) return false;
        const double r = std::floor(v);
        if (std::fabs(v - r) > 1e-6) return false;
        out = static_cast<uint32_t>(r);
        return true;
    }

    bool ParseFloat(float& out) {
        double v = 0.0;
        if (!ParseDouble(v)) return false;
        out = static_cast<float>(v);
        return std::isfinite(out);
    }

    bool SkipValue() {
        SkipWs();
        if (pos_ >= s_.size()) return false;
        const char c = s_[pos_];
        if (c == '"') {
            std::string tmp;
            return ParseString(tmp);
        }
        if (c == '{') {
            ++pos_;
            if (Consume('}')) return true;
            for (;;) {
                std::string k;
                if (!ParseString(k) || !Consume(':') || !SkipValue()) return false;
                if (Consume('}')) return true;
                if (!Consume(',')) return false;
            }
        }
        if (c == '[') {
            ++pos_;
            if (Consume(']')) return true;
            for (;;) {
                if (!SkipValue()) return false;
                if (Consume(']')) return true;
                if (!Consume(',')) return false;
            }
        }
        if (s_.compare(pos_, 4u, "true") == 0)  { pos_ += 4u; return true; }
        if (s_.compare(pos_, 5u, "false") == 0) { pos_ += 5u; return true; }
        if (s_.compare(pos_, 4u, "null") == 0)  { pos_ += 4u; return true; }
        double num = 0.0;
        return ParseDouble(num);
    }

private:
    const std::string& s_;
    size_t pos_ = 0u;
};

}  // namespace

const char* SkyrimHoldName(SkyrimHold hold) {
    const size_t idx = static_cast<size_t>(hold);
    return (idx < kSkyrimHoldCount) ? kHoldProfiles[idx].name : "Whiterun";
}

const char* SkyrimHoldCapital(SkyrimHold hold) {
    const size_t idx = static_cast<size_t>(hold);
    return (idx < kSkyrimHoldCount) ? kHoldProfiles[idx].capital : "Whiterun";
}

const char* CommodityName(CommodityKind kind) {
    switch (kind) {
        case CommodityKind::FoodGrain:       return "FoodGrain";
        case CommodityKind::TimberFuel:      return "TimberFuel";
        case CommodityKind::OreMetal:        return "OreMetal";
        case CommodityKind::FursClothing:    return "FursClothing";
        case CommodityKind::AlchemySupplies: return "AlchemySupplies";
        default:                             return "FoodGrain";
    }
}

SkyrimHold ClassifySkyrimHold(float worldX, float worldY) {
    if (!std::isfinite(worldX) || !std::isfinite(worldY)) {
        return SkyrimHold::Whiterun;
    }
    size_t bestIdx = 0u;
    float bestDist2 = 1.0e30f;
    for (size_t i = 0; i < kSkyrimHoldCount; ++i) {
        const float dx = worldX - kHoldProfiles[i].anchorX;
        const float dy = worldY - kHoldProfiles[i].anchorY;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestDist2) {
            bestDist2 = d2;
            bestIdx = i;
        }
    }
    return static_cast<SkyrimHold>(bestIdx);
}

uint32_t ComputeTamrielDayOfYear(float month0_11, float day1_31) {
    const int m = std::isfinite(month0_11)
        ? std::clamp(static_cast<int>(std::floor(month0_11)), 0, 11)
        : 7;
    const int d = std::isfinite(day1_31)
        ? std::clamp(static_cast<int>(std::floor(day1_31)) - 1, 0, 30)
        : 16;
    return (kMonthStartDoy[m] + static_cast<uint32_t>(d)) % 365u;
}

NpcEcosystemDecision EvaluateNpcShelterTorchHeuristic(const NpcEcosystemInput& in) {
    NpcEcosystemDecision out{};
    const float rain  = std::clamp(std::isfinite(in.rain01) ? in.rain01 : 0.0f, 0.0f, 1.0f);
    const float snow  = std::clamp(std::isfinite(in.snow01) ? in.snow01 : 0.0f, 0.0f, 1.0f);
    const float storm = std::clamp(std::isfinite(in.storm01) ? in.storm01 : std::max(rain, snow), 0.0f, 1.0f);
    const float snowD = std::clamp(std::isfinite(in.snowDepthU) ? in.snowDepthU : 0.0f, 0.0f, 48.0f);
    const float wind  = std::clamp(std::isfinite(in.windSpeedMs) ? in.windSpeedMs : 0.0f, 0.0f, 40.0f);
    const float tempC = std::isfinite(in.ambientTempC) ? in.ambientTempC : 10.0f;
    const float hour  = std::isfinite(in.gameHour) ? std::fmod(std::max(in.gameHour, 0.0f), 24.0f) : 12.0f;
    const float arctic = kHoldProfiles[std::min(static_cast<size_t>(in.hold), kSkyrimHoldCount - 1u)].arcticExposure;
    const float disrupt = std::clamp(std::isfinite(in.caravanDisruption01) ? in.caravanDisruption01 : 0.0f, 0.0f, 1.0f);

    // Night darkness envelope: peaks 21:00..04:30, transitions across dusk (19:00..21:00) & dawn (04:30..06:30).
    float night01 = 0.0f;
    if (hour >= 21.0f || hour < 4.5f) {
        night01 = 1.0f;
    } else if (hour >= 19.0f) {
        night01 = (hour - 19.0f) / 2.0f;
    } else if (hour < 6.5f) {
        night01 = (6.5f - hour) / 2.0f;
    }

    // Wind-chill + blizzard + downpour shelter urgency (suppressed indoors):
    const float coldStress = std::clamp((4.0f - tempC) / 24.0f, 0.0f, 1.0f);
    const float windStress = std::clamp((wind - 7.0f) / 18.0f, 0.0f, 1.0f);
    const float precipStress = std::max(rain * 0.92f, snow * (0.70f + 0.30f * std::clamp(snowD / 24.0f, 0.0f, 1.0f)));
    if (in.inInterior) {
        out.shelterUrgency01 = 0.0f;
    } else {
        out.shelterUrgency01 = std::clamp(
            precipStress * 0.58f + storm * 0.24f + coldStress * (0.20f + 0.14f * arctic) + windStress * 0.18f,
            0.0f, 1.0f);
    }

    // Torch need: high at night or in heavy overcast/blizzard gloom outdoors; low in daylight or right beside a hearth.
    const float gloom = in.inInterior ? 0.0f : (storm * 0.35f + snow * 0.18f);
    out.torchNeed01 = std::clamp(
        (in.inInterior ? 0.05f : (night01 * 0.88f + gloom)) * (in.nearFire ? 0.25f : 1.0f),
        0.0f, 1.0f);

    // Guard / NPC alertness rises sharply in combat, night storms, or hold caravan crisis.
    const float crowdBoost = std::clamp(static_cast<float>(in.nearbyActors) / 20.0f, 0.0f, 0.15f);
    out.alertness01 = in.inCombat
        ? 1.0f
        : std::clamp(0.12f + night01 * 0.22f + storm * 0.24f + disrupt * 0.28f + crowdBoost, 0.0f, 0.95f);

    out.shouldSeekShelter = (!in.inInterior) && (!in.nearFire) && (out.shelterUrgency01 >= 0.55f);
    out.shouldWarmAtHearth = in.nearFire && ((coldStress >= 0.25f) || (out.shelterUrgency01 >= 0.42f) || (snow > 0.25f));
    out.shouldCarryTorch  = (!in.inInterior) && (!out.shouldSeekShelter) && (!out.shouldWarmAtHearth) && (out.torchNeed01 >= 0.50f);

    if (in.inCombat) {
        out.directive = WorldNpcDirective::AlertPatrol;
    } else if (out.shouldWarmAtHearth) {
        out.directive = WorldNpcDirective::WarmAtBrazier;
    } else if (out.shouldSeekShelter) {
        out.directive = WorldNpcDirective::SeekShelter;
    } else if (out.shouldCarryTorch) {
        out.directive = WorldNpcDirective::EquipTorch;
    } else {
        out.directive = WorldNpcDirective::NormalRoutine;
    }
    return out;
}

EconomyLedger::EconomyLedger() {
    ResetToBaseline();
}

void EconomyLedger::ResetToBaseline() {
    totalDaysSimulated_     = 0u;
    totalPlayerTrades_      = 0u;
    lastDayOfYear_          = 228u;
    lastCalendarDaysPassed_ = -1.0f;

    for (size_t hi = 0; hi < kSkyrimHoldCount; ++hi) {
        HoldEconomyState& h = holds_[hi];
        h.hold            = static_cast<SkyrimHold>(hi);
        h.arcticExposure  = kHoldProfiles[hi].arcticExposure;
        h.localStorm01    = 0.0f;
        h.localSnowDepthU = 0.0f;
        h.scarcity01      = 0.0f;
        h.disruption01    = 0.0f;
        h.playerTrades    = 0u;
        for (size_t ci = 0; ci < kCommodityCount; ++ci) {
            auto& m = h.markets[ci];
            m.baseProduction = kHoldProfiles[hi].prod[ci];
            m.baseDemand     = kHoldProfiles[hi].dem[ci];
            // Seed initial stock slightly reflecting each hold's local comparative specialty
            const float ratio = m.baseProduction / std::max(m.baseDemand, 1.0f);
            m.stock           = std::clamp(100.0f * (0.82f + 0.18f * ratio), 65.0f, 145.0f);
            m.playerSurplus   = 0.0f;
            m.priceIndex      = 1.0f;
        }
        RecomputeHoldMetrics(h, lastDayOfYear_);
    }

    for (size_t ri = 0; ri < kCaravanRouteCount; ++ri) {
        auto& r = routes_[ri];
        r.from          = kRouteSpecs[ri].from;
        r.to            = kRouteSpecs[ri].to;
        r.baseCapacity  = kRouteSpecs[ri].baseCapacity;
        r.disruption01  = 0.0f;
        r.lastFlowUnits = 0.0f;
        r.isArcticPass  = kRouteSpecs[ri].isArcticPass;
        r.isSeaRoute    = kRouteSpecs[ri].isSeaRoute;
    }
}

void EconomyLedger::RecomputeHoldMetrics(HoldEconomyState& h, uint32_t dayOfYear) {
    const float winter01  = SeasonalWinter01(dayOfYear);
    const float harvest01 = SeasonalHarvest01(dayOfYear);

    float scarcitySum = 0.0f;
    for (size_t ci = 0; ci < kCommodityCount; ++ci) {
        auto& m = h.markets[ci];
        m.stock = std::clamp(m.stock, 12.0f, 320.0f);
        m.playerSurplus = std::clamp(m.playerSurplus, -120.0f, 240.0f);

        // Effective supply includes player-dumped surplus (depressing local price when flooded)
        const float effectiveSupply = std::max(12.0f, m.stock + m.playerSurplus * 0.55f);
        const float supplyRatio = 100.0f / effectiveSupply;

        float seasonalPriceShift = 0.0f;
        if (ci == static_cast<size_t>(CommodityKind::FoodGrain)) {
            seasonalPriceShift += winter01 * (0.14f + 0.24f * h.arcticExposure);
            seasonalPriceShift -= harvest01 * (0.12f + 0.14f * std::clamp((m.baseProduction - 12.0f) / 12.0f, 0.0f, 1.0f));
        } else if (ci == static_cast<size_t>(CommodityKind::TimberFuel)) {
            seasonalPriceShift += winter01 * (0.16f + 0.26f * h.arcticExposure);
        } else if (ci == static_cast<size_t>(CommodityKind::FursClothing)) {
            seasonalPriceShift += winter01 * (0.08f + 0.10f * h.arcticExposure);
        }

        const float rawIndex = 0.45f + 0.55f * supplyRatio + seasonalPriceShift + h.disruption01 * 0.14f;
        m.priceIndex = std::clamp(rawIndex, 0.58f, 1.95f);

        const float commScarcity = std::clamp((m.priceIndex - 0.88f) / 0.85f, 0.0f, 1.0f);
        scarcitySum += commScarcity;
    }
    h.scarcity01 = std::clamp(scarcitySum / static_cast<float>(kCommodityCount), 0.0f, 1.0f);
}

void EconomyLedger::StepDay(const EconomyDayStepInput& in) {
    const uint32_t doy     = in.dayOfYear % 365u;
    const float winter01   = SeasonalWinter01(doy);
    const float harvest01  = SeasonalHarvest01(doy);
    const float storm      = std::clamp(std::isfinite(in.storm01) ? in.storm01 : 0.0f, 0.0f, 1.0f);
    const float snowD      = std::clamp(std::isfinite(in.snowDepthU) ? in.snowDepthU : 0.0f, 0.0f, 48.0f);
    const float rain       = std::clamp(std::isfinite(in.rain01) ? in.rain01 : 0.0f, 0.0f, 1.0f);
    const float snowNorm   = std::clamp(snowD / 32.0f, 0.0f, 1.0f);

    lastDayOfYear_ = doy;

    // 1. Update per-hold weather exposure, seasonal production, and local consumption.
    for (size_t hi = 0; hi < kSkyrimHoldCount; ++hi) {
        HoldEconomyState& h = holds_[hi];
        const bool isFocus = (h.hold == in.focusHold);
        const float weatherWeight = in.applyStormGlobally
            ? (0.65f + 0.35f * h.arcticExposure)
            : (isFocus ? 1.0f : (0.25f + 0.35f * h.arcticExposure));

        h.localStorm01    = std::clamp(storm * weatherWeight, 0.0f, 1.0f);
        h.localSnowDepthU = std::clamp(snowD * (0.45f + 0.55f * h.arcticExposure), 0.0f, 48.0f);

        for (size_t ci = 0; ci < kCommodityCount; ++ci) {
            auto& m = h.markets[ci];
            float prod = m.baseProduction;
            float dem  = m.baseDemand;

            if (ci == static_cast<size_t>(CommodityKind::FoodGrain)) {
                // Autumn harvest surges farm production (especially Whiterun); winter cuts crops & raises arctic rations.
                prod *= (1.0f + harvest01 * 0.55f - winter01 * (0.28f + 0.32f * h.arcticExposure));
                dem  *= (1.0f + winter01 * (0.18f + 0.34f * h.arcticExposure));
            } else if (ci == static_cast<size_t>(CommodityKind::TimberFuel)) {
                // Winter freezes logging & surges firewood heating demand in arctic holds.
                prod *= (1.0f - winter01 * 0.18f * h.arcticExposure - h.localStorm01 * 0.15f);
                dem  *= (1.0f + winter01 * (0.25f + 0.45f * h.arcticExposure));
            } else if (ci == static_cast<size_t>(CommodityKind::OreMetal)) {
                // Severe storms slow surface ore hauling slightly.
                prod *= (1.0f - h.localStorm01 * 0.10f);
            } else if (ci == static_cast<size_t>(CommodityKind::FursClothing)) {
                dem  *= (1.0f + winter01 * (0.16f + 0.20f * h.arcticExposure));
            } else if (ci == static_cast<size_t>(CommodityKind::AlchemySupplies)) {
                prod *= (1.0f + harvest01 * 0.18f - winter01 * 0.16f * h.arcticExposure);
            }

            // Soft mean-reverting inventory damping prevents runaway saturation/collapse while preserving strong regional spread.
            const float meanPull = (100.0f - m.stock) * 0.045f;
            m.stock = std::clamp(m.stock + (prod - dem) * 0.65f + meanPull, 12.0f, 320.0f);

            // Local merchant absorption naturally decays a fraction of player surplus each day even before caravans.
            m.playerSurplus *= 0.88f;
            if (std::fabs(m.playerSurplus) < 0.15f) m.playerSurplus = 0.0f;
        }
    }

    // 2. Step all 14 inter-hold caravan routes: compute storm/blizzard disruption and flow goods from surplus -> shortage.
    std::array<float, kSkyrimHoldCount> holdDisruptSum{};
    std::array<uint32_t, kSkyrimHoldCount> holdRouteDeg{};

    for (auto& r : routes_) {
        const size_t ia = static_cast<size_t>(r.from);
        const size_t ib = static_cast<size_t>(r.to);
        HoldEconomyState& ha = holds_[ia];
        HoldEconomyState& hb = holds_[ib];

        const float routeStorm = std::max(ha.localStorm01, hb.localStorm01);
        const float routeArctic = 0.5f * (ha.arcticExposure + hb.arcticExposure);
        const float blizzardPenalty = (r.isArcticPass ? 0.36f : 0.24f) * routeStorm * (0.5f + 0.5f * snowNorm);
        const float seaGalePenalty  = r.isSeaRoute ? (routeStorm * 0.30f + winter01 * 0.14f) : 0.0f;
        const float rainMudPenalty  = (!r.isSeaRoute) ? (rain * 0.22f) : 0.0f;
        const float winterPassPen   = r.isArcticPass ? (winter01 * 0.16f * routeArctic) : 0.0f;

        r.disruption01 = std::clamp(
            routeStorm * 0.56f + blizzardPenalty + seaGalePenalty + rainMudPenalty + winterPassPen,
            0.0f, 0.92f);

        holdDisruptSum[ia] += r.disruption01;
        holdDisruptSum[ib] += r.disruption01;
        holdRouteDeg[ia]   += 1u;
        holdRouteDeg[ib]   += 1u;

        const float effCap = r.baseCapacity * (1.0f - r.disruption01);
        float movedTotal = 0.0f;

        for (size_t ci = 0; ci < kCommodityCount; ++ci) {
            auto& ma = ha.markets[ci];
            auto& mb = hb.markets[ci];

            // Effective inventory differential drives arbitrage caravan flow (surplus -> shortage)
            const float effA = ma.stock + ma.playerSurplus * 0.65f;
            const float effB = mb.stock + mb.playerSurplus * 0.65f;
            const float diff = effA - effB;
            if (std::fabs(diff) > 3.0f && effCap > 0.25f) {
                const float scheduledMove = std::clamp(std::fabs(diff) * 0.24f, 0.0f, r.baseCapacity);
                const float rawMove = scheduledMove * (1.0f - r.disruption01);
                if (diff > 0.0f) {
                    // Export from A -> B (caravans actively export playerSurplus first!)
                    const float exportSurp = std::min(std::max(ma.playerSurplus, 0.0f), rawMove * 0.70f);
                    ma.playerSurplus -= exportSurp;
                    const float fromStock = rawMove - exportSurp;
                    ma.stock = std::clamp(ma.stock - fromStock, 12.0f, 320.0f);
                    mb.stock = std::clamp(mb.stock + rawMove * 0.92f, 12.0f, 320.0f);
                } else {
                    // Export from B -> A
                    const float exportSurp = std::min(std::max(mb.playerSurplus, 0.0f), rawMove * 0.70f);
                    mb.playerSurplus -= exportSurp;
                    const float fromStock = rawMove - exportSurp;
                    mb.stock = std::clamp(mb.stock - fromStock, 12.0f, 320.0f);
                    ma.stock = std::clamp(ma.stock + rawMove * 0.92f, 12.0f, 320.0f);
                }
                movedTotal += rawMove;
            }
        }
        r.lastFlowUnits = movedTotal;
    }

    // 3. Finalize per-hold caravan disruption averages & recompute live commodity prices.
    for (size_t hi = 0; hi < kSkyrimHoldCount; ++hi) {
        holds_[hi].disruption01 = (holdRouteDeg[hi] > 0u)
            ? std::clamp(holdDisruptSum[hi] / static_cast<float>(holdRouteDeg[hi]), 0.0f, 1.0f)
            : 0.0f;
        RecomputeHoldMetrics(holds_[hi], doy);
    }

    ++totalDaysSimulated_;
}

void EconomyLedger::RecordPlayerTrade(SkyrimHold hold, CommodityKind commodity, float quantityUnits, bool playerSoldToMerchant) {
    const size_t hi = std::min(static_cast<size_t>(hold), kSkyrimHoldCount - 1u);
    const size_t ci = std::min(static_cast<size_t>(commodity), kCommodityCount - 1u);
    if (!std::isfinite(quantityUnits) || quantityUnits <= 0.0f) return;

    const float q = std::clamp(quantityUnits, 0.5f, 160.0f);
    HoldEconomyState& h = holds_[hi];
    HoldCommodityMarket& m = h.markets[ci];
    if (playerSoldToMerchant) {
        // Flooding a hold with goods (e.g. iron daggers) creates local glut & depresses sell returns.
        m.playerSurplus = std::clamp(m.playerSurplus + q, -120.0f, 240.0f);
        m.stock         = std::clamp(m.stock + q * 0.45f, 12.0f, 320.0f);
    } else {
        // Buying out a merchant tightens local supply.
        m.playerSurplus = std::clamp(m.playerSurplus - q * 0.40f, -120.0f, 240.0f);
        m.stock         = std::clamp(m.stock - q * 0.65f, 12.0f, 320.0f);
    }
    ++h.playerTrades;
    ++totalPlayerTrades_;
    RecomputeHoldMetrics(h, lastDayOfYear_);
}

void EconomyLedger::RecordPlayerBarterSession(SkyrimHold hold, float intensityUnits) {
    const float q = std::clamp(std::isfinite(intensityUnits) ? intensityUnits : 10.0f, 1.0f, 60.0f);
    const size_t hi = std::min(static_cast<size_t>(hold), kSkyrimHoldCount - 1u);
    HoldEconomyState& h = holds_[hi];
    // Distribute session trade impact across local commodities (weighted toward OreMetal/FoodGrain/Furs)
    static constexpr float kWeights[kCommodityCount] = {0.22f, 0.14f, 0.30f, 0.18f, 0.16f};
    for (size_t ci = 0; ci < kCommodityCount; ++ci) {
        h.markets[ci].playerSurplus = std::clamp(h.markets[ci].playerSurplus + q * kWeights[ci], -120.0f, 240.0f);
        h.markets[ci].stock         = std::clamp(h.markets[ci].stock + q * kWeights[ci] * 0.25f, 12.0f, 320.0f);
    }
    ++h.playerTrades;
    ++totalPlayerTrades_;
    RecomputeHoldMetrics(h, lastDayOfYear_);
}

HoldBarterMultipliers EconomyLedger::QueryHoldBarterMultipliers(SkyrimHold hold,
                                                                float liveStorm01,
                                                                float liveSnowDepthU,
                                                                uint32_t dayOfYear) const {
    const size_t hi = std::min(static_cast<size_t>(hold), kSkyrimHoldCount - 1u);
    const HoldEconomyState& h = holds_[hi];
    const uint32_t doy = dayOfYear % 365u;
    const float winter01  = SeasonalWinter01(doy);
    const float harvest01 = SeasonalHarvest01(doy);
    const float storm     = std::clamp(std::isfinite(liveStorm01) ? liveStorm01 : 0.0f, 0.0f, 1.0f);
    const float snowNorm  = std::clamp((std::isfinite(liveSnowDepthU) ? liveSnowDepthU : 0.0f) / 36.0f, 0.0f, 1.0f);

    float avgPrice = 0.0f;
    float maxSurplus = 0.0f;
    float maxPrice = -1.0f;
    float minPrice = 100.0f;
    CommodityKind shortKind = CommodityKind::FoodGrain;
    CommodityKind surpKind  = CommodityKind::FoodGrain;
    float totalPositiveSurplus = 0.0f;

    for (size_t ci = 0; ci < kCommodityCount; ++ci) {
        const auto& m = h.markets[ci];
        avgPrice += m.priceIndex;
        if (m.priceIndex > maxPrice) { maxPrice = m.priceIndex; shortKind = static_cast<CommodityKind>(ci); }
        if (m.priceIndex < minPrice) { minPrice = m.priceIndex; surpKind  = static_cast<CommodityKind>(ci); }
        if (m.playerSurplus > 0.0f) {
            totalPositiveSurplus += m.playerSurplus;
            maxSurplus = std::max(maxSurplus, m.playerSurplus);
        }
    }
    avgPrice /= static_cast<float>(kCommodityCount);

    // Combine stored multi-day caravan disruption with live instantaneous storm impact
    const float liveDisrupt = std::clamp(
        std::max(h.disruption01, storm * (0.48f + 0.34f * h.arcticExposure) + snowNorm * 0.20f * h.arcticExposure),
        0.0f, 0.95f);

    const float seasonalMod = std::clamp(
        1.0f + winter01 * (0.10f + 0.22f * h.arcticExposure) - harvest01 * (0.10f * (1.0f - h.arcticExposure)),
        0.82f, 1.45f);

    const float stormPremium = storm * (0.10f + 0.12f * h.arcticExposure) + liveDisrupt * 0.16f;
    const float scarcity = std::clamp(
        h.scarcity01 * 0.65f + liveDisrupt * 0.25f + storm * 0.12f + winter01 * 0.10f * h.arcticExposure
        - harvest01 * 0.08f * (1.0f - h.arcticExposure),
        0.0f, 1.0f);

    // Player market-flooding penalty depresses merchant sell payouts smoothly until caravans export surplus!
    const float oversupplyFrac = std::clamp((totalPositiveSurplus * 0.0045f) + (maxSurplus * 0.0055f), 0.0f, 0.42f);

    HoldBarterMultipliers out{};
    out.hold                = h.hold;
    out.scarcity01          = scarcity;
    out.caravanDisruption01 = liveDisrupt;
    out.seasonalModifier    = seasonalMod;
    out.dominantShortage    = shortKind;
    out.dominantSurplus     = surpKind;

    // Buy price multiplier (what the player pays): rises with hold scarcity, winter arctic squeeze & storm disruption.
    out.buyPriceMult = std::clamp(avgPrice * (0.84f + 0.16f * seasonalMod) + stormPremium - oversupplyFrac * 0.25f,
                                  0.78f, 1.85f);

    // Sell price multiplier (what merchants pay the player): rises slightly in shortage, drops sharply when player floods market!
    out.sellPriceMult = std::clamp(0.96f + scarcity * 0.18f - oversupplyFrac * 0.72f,
                                   0.58f, 1.22f);

    // Bounded Skyrim GMST targets wired live into fBarterMin/Max/BuyMin/SellMax:
    out.barterMin     = std::clamp(1.32f * out.buyPriceMult, 1.10f, 2.35f);
    out.barterMax     = std::clamp(3.30f * (0.68f + 0.32f * out.buyPriceMult), 2.70f, 4.65f);
    out.barterBuyMin  = std::clamp(1.05f + scarcity * 0.14f, 1.05f, 1.24f);
    out.barterSellMax = std::clamp(0.95f * std::clamp(out.sellPriceMult, 0.68f, 1.0f), 0.65f, 0.95f);

    if (scarcity < 0.22f)      out.marketTier = 0u;  // Surplus / Harvest Abundance
    else if (scarcity < 0.48f) out.marketTier = 1u;  // Balanced
    else if (scarcity < 0.74f) out.marketTier = 2u;  // Tight / Winter Squeeze
    else                       out.marketTier = 3u;  // Crisis / Blizzard Blockade
    return out;
}

const HoldEconomyState& EconomyLedger::GetHold(SkyrimHold hold) const {
    return holds_[std::min(static_cast<size_t>(hold), kSkyrimHoldCount - 1u)];
}

const CaravanRouteState& EconomyLedger::GetRoute(size_t index) const {
    return routes_[std::min(index, kCaravanRouteCount - 1u)];
}

std::string EconomyLedger::SerializeToJson() const {
    std::ostringstream os;
    char num[64];
    auto fmtF = [&](float v) -> const char* {
        const float safe = std::isfinite(v) ? v : 0.0f;
        std::snprintf(num, sizeof num, "%.4f", safe);
        return num;
    };

    os << "{\n";
    os << "  \"schemaVersion\": " << kEconomySchemaVersion << ",\n";
    os << "  \"totalDaysSimulated\": " << totalDaysSimulated_ << ",\n";
    os << "  \"totalPlayerTrades\": " << totalPlayerTrades_ << ",\n";
    os << "  \"lastDayOfYear\": " << lastDayOfYear_ << ",\n";
    os << "  \"lastCalendarDaysPassed\": " << fmtF(lastCalendarDaysPassed_) << ",\n";
    os << "  \"holds\": [\n";
    for (size_t hi = 0; hi < kSkyrimHoldCount; ++hi) {
        const auto& h = holds_[hi];
        os << "    {\n";
        os << "      \"id\": " << hi << ",\n";
        os << "      \"name\": \"" << SkyrimHoldName(h.hold) << "\",\n";
        os << "      \"localStorm01\": " << fmtF(h.localStorm01) << ",\n";
        os << "      \"localSnowDepthU\": " << fmtF(h.localSnowDepthU) << ",\n";
        os << "      \"disruption01\": " << fmtF(h.disruption01) << ",\n";
        os << "      \"playerTrades\": " << h.playerTrades << ",\n";
        os << "      \"markets\": [\n";
        for (size_t ci = 0; ci < kCommodityCount; ++ci) {
            const auto& m = h.markets[ci];
            os << "        {\"stock\": " << fmtF(m.stock)
               << ", \"playerSurplus\": " << fmtF(m.playerSurplus)
               << ", \"priceIndex\": " << fmtF(m.priceIndex) << "}"
               << (ci + 1u < kCommodityCount ? "," : "") << "\n";
        }
        os << "      ]\n";
        os << "    }" << (hi + 1u < kSkyrimHoldCount ? "," : "") << "\n";
    }
    os << "  ],\n";
    os << "  \"routes\": [\n";
    for (size_t ri = 0; ri < kCaravanRouteCount; ++ri) {
        const auto& r = routes_[ri];
        os << "    {\"disruption01\": " << fmtF(r.disruption01)
           << ", \"lastFlowUnits\": " << fmtF(r.lastFlowUnits) << "}"
           << (ri + 1u < kCaravanRouteCount ? "," : "") << "\n";
    }
    os << "  ]\n";
    os << "}\n";
    return os.str();
}

bool EconomyLedger::DeserializeFromJson(const std::string& jsonText) {
    if (jsonText.empty() || jsonText.size() > 512u * 1024u) return false;

    // Parse into a temporary candidate ledger so any error fails closed without mutating *this!
    EconomyLedger cand;
    cand.ResetToBaseline();

    StrictJsonParser p(jsonText);
    if (!p.Consume('{')) return false;

    bool gotSchema = false;
    bool gotHolds  = false;
    bool gotRoutes = false;

    if (!p.Peek('}')) {
        for (;;) {
            std::string key;
            if (!p.ParseString(key) || !p.Consume(':')) return false;

            if (key == "schemaVersion") {
                uint32_t ver = 0u;
                if (!p.ParseUint(ver) || ver != kEconomySchemaVersion) return false;
                gotSchema = true;
            } else if (key == "totalDaysSimulated") {
                if (!p.ParseUint(cand.totalDaysSimulated_)) return false;
            } else if (key == "totalPlayerTrades") {
                if (!p.ParseUint(cand.totalPlayerTrades_)) return false;
            } else if (key == "lastDayOfYear") {
                uint32_t doy = 0u;
                if (!p.ParseUint(doy) || doy >= 365u) return false;
                cand.lastDayOfYear_ = doy;
            } else if (key == "lastCalendarDaysPassed") {
                float d = 0.0f;
                if (!p.ParseFloat(d) || d < -1.0f || d > 1.0e7f) return false;
                cand.lastCalendarDaysPassed_ = d;
            } else if (key == "holds") {
                if (!p.Consume('[')) return false;
                size_t holdCount = 0u;
                if (!p.Peek(']')) {
                    for (;;) {
                        if (holdCount >= kSkyrimHoldCount) return false;
                        if (!p.Consume('{')) return false;
                        HoldEconomyState& h = cand.holds_[holdCount];
                        bool gotMarkets = false;
                        if (!p.Peek('}')) {
                            for (;;) {
                                std::string hk;
                                if (!p.ParseString(hk) || !p.Consume(':')) return false;
                                if (hk == "id") {
                                    uint32_t id = 0u;
                                    if (!p.ParseUint(id) || id != holdCount) return false;
                                } else if (hk == "name") {
                                    std::string nm;
                                    if (!p.ParseString(nm)) return false;
                                } else if (hk == "localStorm01") {
                                    float v = 0.0f;
                                    if (!p.ParseFloat(v) || v < 0.0f || v > 1.0f) return false;
                                    h.localStorm01 = v;
                                } else if (hk == "localSnowDepthU") {
                                    float v = 0.0f;
                                    if (!p.ParseFloat(v) || v < 0.0f || v > 64.0f) return false;
                                    h.localSnowDepthU = v;
                                } else if (hk == "disruption01") {
                                    float v = 0.0f;
                                    if (!p.ParseFloat(v) || v < 0.0f || v > 1.0f) return false;
                                    h.disruption01 = v;
                                } else if (hk == "playerTrades") {
                                    if (!p.ParseUint(h.playerTrades)) return false;
                                } else if (hk == "markets") {
                                    if (!p.Consume('[')) return false;
                                    size_t mCount = 0u;
                                    if (!p.Peek(']')) {
                                        for (;;) {
                                            if (mCount >= kCommodityCount) return false;
                                            if (!p.Consume('{')) return false;
                                            auto& m = h.markets[mCount];
                                            bool gotStock = false;
                                            if (!p.Peek('}')) {
                                                for (;;) {
                                                    std::string mk;
                                                    if (!p.ParseString(mk) || !p.Consume(':')) return false;
                                                    if (mk == "stock") {
                                                        float v = 0.0f;
                                                        if (!p.ParseFloat(v) || v < 5.0f || v > 400.0f) return false;
                                                        m.stock = std::clamp(v, 12.0f, 320.0f);
                                                        gotStock = true;
                                                    } else if (mk == "playerSurplus") {
                                                        float v = 0.0f;
                                                        if (!p.ParseFloat(v) || v < -200.0f || v > 300.0f) return false;
                                                        m.playerSurplus = std::clamp(v, -120.0f, 240.0f);
                                                    } else if (mk == "priceIndex") {
                                                        float v = 0.0f;
                                                        if (!p.ParseFloat(v) || v < 0.2f || v > 4.0f) return false;
                                                        m.priceIndex = std::clamp(v, 0.58f, 1.95f);
                                                    } else {
                                                        if (!p.SkipValue()) return false;
                                                    }
                                                    if (p.Consume('}')) break;
                                                    if (!p.Consume(',')) return false;
                                                }
                                            } else {
                                                p.Consume('}');
                                            }
                                            if (!gotStock) return false;
                                            ++mCount;
                                            if (p.Consume(']')) break;
                                            if (!p.Consume(',')) return false;
                                        }
                                    } else {
                                        return false;
                                    }
                                    if (mCount != kCommodityCount) return false;
                                    gotMarkets = true;
                                } else {
                                    if (!p.SkipValue()) return false;
                                }
                                if (p.Consume('}')) break;
                                if (!p.Consume(',')) return false;
                            }
                        } else {
                            return false;
                        }
                        if (!gotMarkets) return false;
                        ++holdCount;
                        if (p.Consume(']')) break;
                        if (!p.Consume(',')) return false;
                    }
                } else {
                    return false;
                }
                if (holdCount != kSkyrimHoldCount) return false;
                gotHolds = true;
            } else if (key == "routes") {
                if (!p.Consume('[')) return false;
                size_t rCount = 0u;
                if (!p.Peek(']')) {
                    for (;;) {
                        if (rCount >= kCaravanRouteCount) return false;
                        if (!p.Consume('{')) return false;
                        auto& r = cand.routes_[rCount];
                        if (!p.Peek('}')) {
                            for (;;) {
                                std::string rk;
                                if (!p.ParseString(rk) || !p.Consume(':')) return false;
                                if (rk == "disruption01") {
                                    float v = 0.0f;
                                    if (!p.ParseFloat(v) || v < 0.0f || v > 1.0f) return false;
                                    r.disruption01 = v;
                                } else if (rk == "lastFlowUnits") {
                                    float v = 0.0f;
                                    if (!p.ParseFloat(v) || v < 0.0f || v > 10000.0f) return false;
                                    r.lastFlowUnits = v;
                                } else {
                                    if (!p.SkipValue()) return false;
                                }
                                if (p.Consume('}')) break;
                                if (!p.Consume(',')) return false;
                            }
                        } else {
                            p.Consume('}');
                        }
                        ++rCount;
                        if (p.Consume(']')) break;
                        if (!p.Consume(',')) return false;
                    }
                } else {
                    return false;
                }
                if (rCount != kCaravanRouteCount) return false;
                gotRoutes = true;
            } else {
                if (!p.SkipValue()) return false;
            }

            if (p.Consume('}')) break;
            if (!p.Consume(',')) return false;
        }
    } else {
        return false;
    }

    if (!p.Eof() || !gotSchema || !gotHolds || !gotRoutes) return false;

    for (auto& h : cand.holds_) {
        cand.RecomputeHoldMetrics(h, cand.lastDayOfYear_);
    }
    *this = cand;
    return true;
}

bool EconomyLedger::SaveToJson(const std::string& filePath) const {
    if (filePath.empty()) return false;
    try {
        const std::filesystem::path target(filePath);
        if (target.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(target.parent_path(), ec);
        }
        const std::string tmpPath = filePath + ".tmp";
        const std::string payload = SerializeToJson();
        {
            std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            out.flush();
            if (!out.good()) return false;
        }
#ifndef RAW_SIM_PURE_TEST
        if (MoveFileExA(tmpPath.c_str(), filePath.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }
#endif
        std::error_code ec;
        std::filesystem::rename(tmpPath, target, ec);
        if (ec) {
            std::filesystem::copy_file(tmpPath, target, std::filesystem::copy_options::overwrite_existing, ec);
            std::error_code ecRm;
            std::filesystem::remove(tmpPath, ecRm);
            return !ec;
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool EconomyLedger::LoadFromJson(const std::string& filePath) {
    if (filePath.empty()) return false;
    try {
        std::ifstream in(filePath, std::ios::binary);
        if (!in) return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        return DeserializeFromJson(ss.str());
    } catch (...) {
        return false;
    }
}

#ifndef RAW_SIM_PURE_TEST
namespace {

struct LiveEconomyRuntime {
    std::mutex             mu;
    EconomyLedger          ledger;
    std::string            jsonPath;
    SkyrimHold             lastExteriorHold = SkyrimHold::Whiterun;
    EconomyLedgerTelemetry tel{};
    bool                   initialized = false;
    bool                   dirty       = false;
    uint32_t               ticksSinceSave = 0u;
} g_econ;

}  // namespace

std::string EconomyLedgerInit(const std::string& jsonPath) {
    std::lock_guard<std::mutex> lk(g_econ.mu);
    g_econ.jsonPath = jsonPath;
    g_econ.ledger.ResetToBaseline();
    const bool loaded = !jsonPath.empty() && g_econ.ledger.LoadFromJson(jsonPath);
    g_econ.initialized = true;
    g_econ.dirty = !loaded;
    g_econ.ticksSinceSave = 0u;
    g_econ.tel = {};
    g_econ.tel.initialized       = 1u;
    g_econ.tel.activeHold        = static_cast<uint8_t>(g_econ.lastExteriorHold);
    g_econ.tel.daysSimulated     = g_econ.ledger.TotalDaysSimulated();
    g_econ.tel.dayOfYear         = g_econ.ledger.LastDayOfYear();
    g_econ.tel.totalPlayerTrades = g_econ.ledger.TotalPlayerTrades();

    if (!loaded && !jsonPath.empty()) {
        if (g_econ.ledger.SaveToJson(jsonPath)) {
            g_econ.dirty = false;
            ++g_econ.tel.jsonSaves;
        }
    }

    char buf[256];
    std::snprintf(buf, sizeof buf,
                  "[EconomyLedger] 9-hold living economy %s (%s) days=%u trades=%u",
                  loaded ? "restored from disk" : "initialized baseline",
                  jsonPath.c_str(),
                  g_econ.ledger.TotalDaysSimulated(),
                  g_econ.ledger.TotalPlayerTrades());
    return buf;
}

void EconomyLedgerShutdown() {
    std::lock_guard<std::mutex> lk(g_econ.mu);
    if (!g_econ.initialized) return;
    if (!g_econ.jsonPath.empty()) {
        if (g_econ.ledger.SaveToJson(g_econ.jsonPath)) {
            ++g_econ.tel.jsonSaves;
        }
    }
    g_econ.dirty = false;
    g_econ.initialized = false;
    g_econ.tel.initialized = 0u;
}

HoldBarterMultipliers EconomyLedgerOnWorldTick(const float playerPos[3],
                                               bool inInterior,
                                               float daysPassed,
                                               float gameMonth0_11,
                                               float gameDay1_31,
                                               float storm01,
                                               float rain01,
                                               float snowDepthU,
                                               const NpcEcosystemDecision& npcDecision) {
    std::lock_guard<std::mutex> lk(g_econ.mu);
    if (!g_econ.initialized) {
        g_econ.ledger.ResetToBaseline();
        g_econ.initialized = true;
        g_econ.tel.initialized = 1u;
    }

    if (!inInterior && playerPos) {
        g_econ.lastExteriorHold = ClassifySkyrimHold(playerPos[0], playerPos[1]);
    }
    const SkyrimHold activeHold = g_econ.lastExteriorHold;
    const uint32_t doy = ComputeTamrielDayOfYear(gameMonth0_11, gameDay1_31);

    // Step simulated days whenever Skyrim's Calendar::daysPassed advances across integer day boundaries.
    if (std::isfinite(daysPassed) && daysPassed >= 0.0f) {
        const float prevDays = g_econ.ledger.LastCalendarDaysPassed();
        if (prevDays < 0.0f) {
            g_econ.ledger.SetLastCalendarDaysPassed(daysPassed);
        } else if (daysPassed < prevDays - 0.5f) {
            // Player loaded an earlier save; resync baseline calendar anchor cleanly without jumping backward.
            g_econ.ledger.SetLastCalendarDaysPassed(daysPassed);
        } else {
            const int prevInt = static_cast<int>(std::floor(prevDays));
            const int curInt  = static_cast<int>(std::floor(daysPassed));
            if (curInt > prevInt) {
                const int steps = std::min(curInt - prevInt, 30);
                for (int s = 0; s < steps; ++s) {
                    EconomyDayStepInput stepIn{};
                    stepIn.dayOfYear          = (doy + 365u - static_cast<uint32_t>(steps - 1 - s)) % 365u;
                    stepIn.storm01            = storm01;
                    stepIn.rain01             = rain01;
                    stepIn.snowDepthU         = snowDepthU;
                    stepIn.focusHold          = activeHold;
                    stepIn.applyStormGlobally = true;
                    g_econ.ledger.StepDay(stepIn);
                }
                g_econ.ledger.SetLastCalendarDaysPassed(daysPassed);
                g_econ.dirty = true;
            }
        }
    }

    const HoldBarterMultipliers quote = g_econ.ledger.QueryHoldBarterMultipliers(activeHold, storm01, snowDepthU, doy);

    g_econ.tel.activeHold          = static_cast<uint8_t>(activeHold);
    g_econ.tel.marketTier          = quote.marketTier;
    g_econ.tel.npcDirective        = static_cast<uint8_t>(npcDecision.directive);
    g_econ.tel.daysSimulated       = g_econ.ledger.TotalDaysSimulated();
    g_econ.tel.dayOfYear           = doy;
    g_econ.tel.totalPlayerTrades   = g_econ.ledger.TotalPlayerTrades();
    g_econ.tel.buyPriceMult        = quote.buyPriceMult;
    g_econ.tel.sellPriceMult       = quote.sellPriceMult;
    g_econ.tel.scarcity01          = quote.scarcity01;
    g_econ.tel.caravanDisruption01 = quote.caravanDisruption01;
    g_econ.tel.npcShelterUrgency01 = npcDecision.shelterUrgency01;
    g_econ.tel.npcTorchNeed01      = npcDecision.torchNeed01;
    g_econ.tel.npcAlertness01      = npcDecision.alertness01;

    // Persist atomically every ~180 sim ticks (~6 seconds at 30 Hz) when dirty (day rolled or trades completed).
    if (g_econ.dirty && !g_econ.jsonPath.empty() && (++g_econ.ticksSinceSave >= 180u)) {
        if (g_econ.ledger.SaveToJson(g_econ.jsonPath)) {
            g_econ.dirty = false;
            g_econ.ticksSinceSave = 0u;
            ++g_econ.tel.jsonSaves;
        }
    }
    return quote;
}

HoldBarterMultipliers EconomyLedgerQueryLiveBarter(const float playerPos[3],
                                                   bool inInterior,
                                                   float storm01,
                                                   float snowDepthU,
                                                   float gameMonth0_11,
                                                   float gameDay1_31) {
    std::lock_guard<std::mutex> lk(g_econ.mu);
    if (!g_econ.initialized) {
        g_econ.ledger.ResetToBaseline();
        g_econ.initialized = true;
        g_econ.tel.initialized = 1u;
    }
    if (!inInterior && playerPos) {
        g_econ.lastExteriorHold = ClassifySkyrimHold(playerPos[0], playerPos[1]);
    }
    const uint32_t doy = ComputeTamrielDayOfYear(gameMonth0_11, gameDay1_31);
    const HoldBarterMultipliers q = g_econ.ledger.QueryHoldBarterMultipliers(
        g_econ.lastExteriorHold, storm01, snowDepthU, doy);
    ++g_econ.tel.barterQueries;
    g_econ.tel.activeHold          = static_cast<uint8_t>(q.hold);
    g_econ.tel.marketTier          = q.marketTier;
    g_econ.tel.buyPriceMult        = q.buyPriceMult;
    g_econ.tel.sellPriceMult       = q.sellPriceMult;
    g_econ.tel.scarcity01          = q.scarcity01;
    g_econ.tel.caravanDisruption01 = q.caravanDisruption01;
    return q;
}

void EconomyLedgerRecordLiveBarterSession(const float playerPos[3], bool inInterior, float intensityUnits) {
    std::lock_guard<std::mutex> lk(g_econ.mu);
    if (!g_econ.initialized) {
        g_econ.ledger.ResetToBaseline();
        g_econ.initialized = true;
        g_econ.tel.initialized = 1u;
    }
    if (!inInterior && playerPos) {
        g_econ.lastExteriorHold = ClassifySkyrimHold(playerPos[0], playerPos[1]);
    }
    g_econ.ledger.RecordPlayerBarterSession(g_econ.lastExteriorHold, intensityUnits);
    g_econ.tel.totalPlayerTrades = g_econ.ledger.TotalPlayerTrades();
    g_econ.dirty = true;
}

bool EconomyLedgerGetTelemetry(EconomyLedgerTelemetry* out) {
    if (!out) return false;
    std::lock_guard<std::mutex> lk(g_econ.mu);
    *out = g_econ.tel;
    return true;
}

bool EconomyLedgerSaveNow() {
    std::lock_guard<std::mutex> lk(g_econ.mu);
    if (!g_econ.initialized || g_econ.jsonPath.empty()) return false;
    if (g_econ.ledger.SaveToJson(g_econ.jsonPath)) {
        g_econ.dirty = false;
        g_econ.ticksSinceSave = 0u;
        ++g_econ.tel.jsonSaves;
        return true;
    }
    return false;
}
#endif

}  // namespace raw::rt
