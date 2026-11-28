// RawWorldEventDirector.cpp: Emergent Weather, Caravan, Wildfire & Aurora Radiant World-Event Director.
// Original work, MIT. Zero Windows / D3D12 headers.

#include "RawWorldEventDirector.h"

#include <algorithm>
#include <cmath>

namespace raw::worldgen {
namespace {

inline float ClampF(float v, float lo, float hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

inline float Clamp01F(float v) noexcept {
    return ClampF(v, 0.0f, 1.0f);
}

constexpr std::array<const char*, kWorldEventKindCount> kEventKindNames = {
    "BlizzardCaravanRescue",
    "WildfireContainment",
    "CaravanBanditAmbush",
    "AuroraCelestialGathering",
    "SeasonalHarvestMarket",
    "PredatorMigrationAlert",
};

}  // namespace

const char* GetWorldEventKindName(WorldEventKind kind) {
    const size_t idx = static_cast<size_t>(kind);
    return (idx < kWorldEventKindCount) ? kEventKindNames[idx] : "UnknownEvent";
}

RawWorldEventDirector::RawWorldEventDirector(const EventDirectorConfig& cfg) {
    Reset(cfg);
}

void RawWorldEventDirector::Reset(const EventDirectorConfig& cfg) {
    cfg_ = cfg;
    active_.clear();
    cooldownRemaining_.fill(0.0f);
    spawnedByKind_.fill(0u);
    nextEventId_   = 1u;
    totalSpawned_  = 0u;
    totalResolved_ = 0u;
    totalExpired_  = 0u;
}

bool RawWorldEventDirector::HasActiveKind(WorldEventKind kind) const {
    for (const WorldEvent& e : active_) {
        if (e.kind == kind) return true;
    }
    return false;
}

bool RawWorldEventDirector::CanSpawnKind(WorldEventKind kind) const {
    const size_t idx = static_cast<size_t>(kind);
    if (idx >= kWorldEventKindCount) return false;
    if (active_.size() >= cfg_.maxConcurrentEvents) return false;
    if (HasActiveKind(kind)) return false;
    if (cooldownRemaining_[idx] > 1.0e-4f) return false;
    return true;
}

const WorldEvent* RawWorldEventDirector::FindActiveByKind(WorldEventKind kind) const {
    for (const WorldEvent& e : active_) {
        if (e.kind == kind) return &e;
    }
    return nullptr;
}

float RawWorldEventDirector::CooldownRemainingHours(WorldEventKind kind) const {
    const size_t idx = static_cast<size_t>(kind);
    return (idx < kWorldEventKindCount) ? cooldownRemaining_[idx] : 0.0f;
}

void RawWorldEventDirector::SpawnEvent(WorldEvent ev) {
    const size_t idx = static_cast<size_t>(ev.kind);
    if (idx >= kWorldEventKindCount || active_.size() >= cfg_.maxConcurrentEvents) {
        return;
    }
    ev.id               = nextEventId_++;
    ev.ageHours         = 0.0f;
    ev.maxDurationHours = std::max(1.0f, cfg_.durationHoursByKind[idx]);
    ev.urgency01        = Clamp01F(std::isfinite(ev.urgency01) ? ev.urgency01 : 0.5f);
    ev.rewardGold       = std::max(0.0f, std::isfinite(ev.rewardGold) ? ev.rewardGold : 0.0f);
    active_.push_back(ev);
    cooldownRemaining_[idx] = std::max(0.0f, cfg_.cooldownHoursByKind[idx]);
    ++totalSpawned_;
    ++spawnedByKind_[idx];
}

bool RawWorldEventDirector::ResolveEvent(uint32_t eventId) {
    if (eventId == 0u) return false;
    for (auto it = active_.begin(); it != active_.end(); ++it) {
        if (it->id == eventId) {
            const size_t idx = static_cast<size_t>(it->kind);
            if (idx < kWorldEventKindCount) {
                cooldownRemaining_[idx] = std::max(cooldownRemaining_[idx], cfg_.cooldownHoursByKind[idx]);
            }
            active_.erase(it);
            ++totalResolved_;
            return true;
        }
    }
    return false;
}

void RawWorldEventDirector::StepHours(float hours, const WorldEventInputs& inputs) {
    if (!std::isfinite(hours) || hours <= 0.0f) {
        return;
    }

    // 1. Resolve effective environmental snapshot (syncing from RawWeatherSim when requested):
    float    todHours   = std::isfinite(inputs.timeOfDayHours) ? inputs.timeOfDayHours : 12.0f;
    SeasonId season     = inputs.season;
    float    cloudCover = Clamp01F(std::isfinite(inputs.cloudCover01) ? inputs.cloudCover01 : 0.25f);
    float    blizzard01 = Clamp01F(std::isfinite(inputs.blizzard01) ? inputs.blizzard01 : 0.0f);
    float    auroraKp   = ClampF(std::isfinite(inputs.auroraKp) ? inputs.auroraKp : 2.0f, 0.0f, 9.0f);

    if (inputs.useWeatherSimClock && inputs.weatherSim != nullptr) {
        const CalendarState& cal      = inputs.weatherSim->GetCalendarState();
        const WeatherGpuUniforms& gpu = inputs.weatherSim->GetGpuUniforms();
        todHours   = cal.timeOfDayHours;
        season     = cal.season;
        cloudCover = Clamp01F(gpu.cloudWind.z);
        blizzard01 = Clamp01F(gpu.weather.z);
        auroraKp   = std::max(auroraKp, gpu.auroraIntensity01 * 9.0f);
    }
    todHours = std::fmod(std::fmod(todHours, 24.0f) + 24.0f, 24.0f);
    const bool isNight    = (todHours < 5.5f || todHours >= 20.5f);
    const bool isDaylight = (todHours >= 8.0f && todHours <= 17.0f);

    // 2. Sample live telemetry from coupled simulators:
    const bool hasEco = (inputs.ecosystem != nullptr);
    const EcoFieldTelemetry ecoTel = hasEco ? inputs.ecosystem->ComputeTelemetry() : EcoFieldTelemetry{};
    const bool hasWild = (inputs.wildlife != nullptr);
    const WildlifeTelemetry wildTel = hasWild ? inputs.wildlife->GetLastTelemetry() : WildlifeTelemetry{};

    // 3. Tick cooldowns ONLY for kinds without an active event (prevents duplicate spam & enforces quiet period):
    for (size_t k = 0u; k < kWorldEventKindCount; ++k) {
        if (!HasActiveKind(static_cast<WorldEventKind>(k)) && cooldownRemaining_[k] > 0.0f) {
            cooldownRemaining_[k] = std::max(0.0f, cooldownRemaining_[k] - hours);
        }
    }

    // 4. Advance active events: escalate urgency, auto-resolve cleared emergencies, and expire timed-out events:
    for (auto it = active_.begin(); it != active_.end(); ) {
        it->ageHours += hours;

        // Auto-resolve WildfireContainment cleanly once fire is extinguished!
        if (it->kind == WorldEventKind::WildfireContainment && hasEco && ecoTel.activeFireCells == 0u) {
            const size_t idx = static_cast<size_t>(it->kind);
            cooldownRemaining_[idx] = std::max(cooldownRemaining_[idx], cfg_.cooldownHoursByKind[idx]);
            it = active_.erase(it);
            ++totalResolved_;
            continue;
        }

        // Escalate urgency gradually for unresolved emergencies:
        if (it->kind == WorldEventKind::BlizzardCaravanRescue ||
            it->kind == WorldEventKind::WildfireContainment ||
            it->kind == WorldEventKind::CaravanBanditAmbush) {
            it->urgency01 = Clamp01F(it->urgency01 + 0.025f * hours);
        }

        if (it->ageHours >= it->maxDurationHours) {
            const size_t idx = static_cast<size_t>(it->kind);
            cooldownRemaining_[idx] = std::max(cooldownRemaining_[idx], cfg_.cooldownHoursByKind[idx]);
            it = active_.erase(it);
            ++totalExpired_;
            continue;
        }
        ++it;
    }

    // 5. Evaluate emergent triggers across all 6 event kinds (respecting cap & cooldowns):

    // 5a. BlizzardCaravanRescue: snowbound caravan on a mountain pass!
    if (inputs.settlement != nullptr && CanSpawnKind(WorldEventKind::BlizzardCaravanRescue)) {
        const TradeCaravan* worstSnowbound = nullptr;
        float bestScarcityPrice = 0.0f;
        for (const TradeCaravan& tc : inputs.settlement->Caravans()) {
            if (!tc.active || !tc.snowbound) continue;
            const HoldState* dst = inputs.settlement->GetHold(tc.destHold);
            const float priceRatio = dst
                ? (dst->price[static_cast<size_t>(tc.commodity)] /
                   std::max(1.0f, GetCommodityBasePrice(tc.commodity)))
                : 1.0f;
            if (!worstSnowbound || priceRatio > bestScarcityPrice) {
                worstSnowbound    = &tc;
                bestScarcityPrice = priceRatio;
            }
        }
        if (worstSnowbound != nullptr) {
            WorldEvent ev{};
            ev.kind       = WorldEventKind::BlizzardCaravanRescue;
            ev.holdId     = worstSnowbound->destHold;
            ev.commodity  = worstSnowbound->commodity;
            ev.urgency01  = Clamp01F(0.65f + 0.15f * bestScarcityPrice);
            ev.rewardGold = std::round(150.0f * std::max(1.0f, bestScarcityPrice) + worstSnowbound->cargoUnits * 4.0f);
            SpawnEvent(ev);
        }
    }

    // 5b. WildfireContainment: active wildfire cells >= 16!
    if (hasEco && ecoTel.activeFireCells >= 16u && CanSpawnKind(WorldEventKind::WildfireContainment)) {
        WorldEvent ev{};
        ev.kind       = WorldEventKind::WildfireContainment;
        ev.holdId     = inputs.ecosystemHoldId;
        ev.commodity  = CommodityKind::Timber;
        ev.urgency01  = Clamp01F(0.70f + static_cast<float>(ecoTel.activeFireCells) / 250.0f);
        ev.rewardGold = std::round(220.0f + static_cast<float>(ecoTel.activeFireCells) * 3.0f);
        SpawnEvent(ev);
    }

    // 5c. CaravanBanditAmbush: high-value scarce trade route with banditRisk > 0.25!
    if (inputs.settlement != nullptr && CanSpawnKind(WorldEventKind::CaravanBanditAmbush)) {
        const TradeCaravan* target = nullptr;
        float highestCargoValue = 0.0f;
        for (const TradeCaravan& tc : inputs.settlement->Caravans()) {
            if (!tc.active || tc.snowbound) continue;
            if (tc.banditRisk01 < 0.25f) continue;
            const HoldState* dst = inputs.settlement->GetHold(tc.destHold);
            const float unitPrice = dst ? dst->price[static_cast<size_t>(tc.commodity)]
                                        : GetCommodityBasePrice(tc.commodity);
            const float cargoVal = tc.cargoUnits * unitPrice;
            if (cargoVal >= 180.0f && cargoVal > highestCargoValue) {
                highestCargoValue = cargoVal;
                target = &tc;
            }
        }
        if (target != nullptr) {
            WorldEvent ev{};
            ev.kind              = WorldEventKind::CaravanBanditAmbush;
            ev.holdId            = target->destHold;
            ev.commodity         = target->commodity;
            ev.urgency01         = Clamp01F(0.60f + target->banditRisk01 * 0.5f);
            ev.rewardGold        = std::round(120.0f + highestCargoValue * 0.25f);
            ev.suggestedFlankers = 2u;
            ev.suggestedArchers  = 2u;
            SpawnEvent(ev);
        }
    }

    // 5d. AuroraCelestialGathering: clear alpine night + geomagnetic auroraKp >= 5.5!
    if (isNight && cloudCover < 0.35f && blizzard01 < 0.25f && auroraKp >= 5.5f &&
        CanSpawnKind(WorldEventKind::AuroraCelestialGathering)) {
        WorldEvent ev{};
        ev.kind                 = WorldEventKind::AuroraCelestialGathering;
        ev.holdId               = HoldId::Winterhold;
        ev.commodity            = CommodityKind::SoulGems;
        ev.urgency01            = 0.30f;
        ev.tavernHarmonyBonus01 = Clamp01F(0.15f + (auroraKp - 5.5f) * 0.06f);
        SpawnEvent(ev);
    }

    // 5e. SeasonalHarvestMarket: Summer/Autumn daylight surplus grain or mead festival!
    if (inputs.settlement != nullptr && isDaylight &&
        (season == SeasonId::Summer || season == SeasonId::Autumn) &&
        CanSpawnKind(WorldEventKind::SeasonalHarvestMarket)) {
        HoldId bestHold = HoldId::Whiterun;
        CommodityKind bestComm = CommodityKind::Grain;
        float bestRatio = 1.15f;
        bool foundSurplus = false;

        for (size_t i = 0u; i < kHoldCount; ++i) {
            const HoldState* h = inputs.settlement->GetHold(static_cast<HoldId>(i));
            if (!h || !h->marketOpen) continue;
            for (CommodityKind ck : {CommodityKind::Grain, CommodityKind::Mead}) {
                const size_t cIdx = static_cast<size_t>(ck);
                const float r = h->stock[cIdx] / std::max(1.0f, h->targetStock[cIdx]);
                if (r > bestRatio) {
                    bestRatio    = r;
                    bestHold     = h->id;
                    bestComm     = ck;
                    foundSurplus = true;
                }
            }
        }
        if (foundSurplus) {
            WorldEvent ev{};
            ev.kind                 = WorldEventKind::SeasonalHarvestMarket;
            ev.holdId               = bestHold;
            ev.commodity            = bestComm;
            ev.urgency01            = 0.25f;
            ev.tavernHarmonyBonus01 = 0.20f;
            SpawnEvent(ev);
        }
    }

    // 5f. PredatorMigrationAlert: startled wildlife > 15 OR winter blizzard pushing fauna toward holds!
    const bool wildlifeDisturbed = (hasWild && wildTel.startledCount > 15u);
    const bool winterPush        = (season == SeasonId::Winter && blizzard01 > 0.55f);
    if ((wildlifeDisturbed || winterPush) && CanSpawnKind(WorldEventKind::PredatorMigrationAlert)) {
        WorldEvent ev{};
        ev.kind              = WorldEventKind::PredatorMigrationAlert;
        ev.holdId            = inputs.wildlifeHoldId;
        ev.commodity         = CommodityKind::Pelts;
        ev.urgency01         = wildlifeDisturbed ? 0.62f : 0.50f;
        ev.rewardGold        = 160.0f;
        ev.suggestedFlankers = 3u;
        SpawnEvent(ev);
    }
}

EventDirectorTelemetry RawWorldEventDirector::ComputeTelemetry() const {
    EventDirectorTelemetry t{};
    t.activeCount   = static_cast<uint32_t>(active_.size());
    t.totalSpawned  = totalSpawned_;
    t.totalResolved = totalResolved_;
    t.totalExpired  = totalExpired_;
    t.spawnedByKind = spawnedByKind_;
    for (const WorldEvent& e : active_) {
        if (e.urgency01 > t.highestUrgency) {
            t.highestUrgency = e.urgency01;
        }
    }
    return t;
}

}  // namespace raw::worldgen
