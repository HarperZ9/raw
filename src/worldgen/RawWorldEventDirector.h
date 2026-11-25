#pragma once
// RawWorldEventDirector.h: Emergent Weather, Caravan, Wildfire & Aurora Radiant World-Event Director (`raw::worldgen`).
// Pure portable C++23 with zero Windows or D3D12 headers.
// Fuses live snapshots of `RawSettlementSim`, `RawWorldEcosystem`, `RawWildlifeBoids`, and `RawWeatherSim`
// to spawn, escalate, auto-resolve, and expire organic regional world events across all 9 Skyrim Holds:
//   1. `BlizzardCaravanRescue`     (snowbound mountain-pass caravans; scarcity-scaled rescue gold)
//   2. `WildfireContainment`       (active wildfire cells >= 16; auto-resolves when fire == 0)
//   3. `CaravanBanditAmbush`       (high-value scarce routes; MeleeFlanker + RangedArcher squad hints)
//   4. `AuroraCelestialGathering`  (clear alpine night + geomagnetic Kp >= 5.5; tavern/merchant harmony bonus)
//   5. `SeasonalHarvestMarket`     (Summer/Autumn daylight grain/mead surplus festivals)
//   6. `PredatorMigrationAlert`    (startled wildlife > 15 or winter blizzard pressure toward settlements)
// Enforces strict concurrency cap (`maxConcurrentEvents`), per-kind anti-spam cooldown hysteresis,
// and exact lifecycle accounting (`totalSpawned == activeCount + totalResolved + totalExpired`).
// Original work, MIT.

#include "RawSettlementSim.h"
#include "RawWeatherSim.h"
#include "RawWildlifeBoids.h"
#include "RawWorldEcosystem.h"

#include <array>
#include <cstdint>
#include <vector>

namespace raw::worldgen {

enum class WorldEventKind : uint8_t {
    BlizzardCaravanRescue    = 0,
    WildfireContainment      = 1,
    CaravanBanditAmbush      = 2,
    AuroraCelestialGathering = 3,
    SeasonalHarvestMarket    = 4,
    PredatorMigrationAlert   = 5,
    Count                    = 6,
};

inline constexpr size_t kWorldEventKindCount = static_cast<size_t>(WorldEventKind::Count);

struct EventDirectorConfig {
    uint32_t maxConcurrentEvents = 6u;
    std::array<float, kWorldEventKindCount> cooldownHoursByKind = {
        16.0f, // BlizzardCaravanRescue
        12.0f, // WildfireContainment
        14.0f, // CaravanBanditAmbush
        20.0f, // AuroraCelestialGathering
        24.0f, // SeasonalHarvestMarket
        18.0f, // PredatorMigrationAlert
    };
    std::array<float, kWorldEventKindCount> durationHoursByKind = {
        18.0f, // BlizzardCaravanRescue
        24.0f, // WildfireContainment
        10.0f, // CaravanBanditAmbush
         8.0f, // AuroraCelestialGathering
        12.0f, // SeasonalHarvestMarket
        14.0f, // PredatorMigrationAlert
    };
};

struct WorldEventInputs {
    const RawSettlementSim*  settlement         = nullptr;
    const RawWorldEcosystem* ecosystem          = nullptr;
    const RawWildlifeBoids*  wildlife           = nullptr;
    const RawWeatherSim*     weatherSim         = nullptr;
    float                    timeOfDayHours     = 12.0f;
    SeasonId                 season             = SeasonId::Summer;
    float                    auroraKp           = 2.0f;  // Geomagnetic Kp index [0..9]
    float                    cloudCover01       = 0.25f; // [0..1]
    float                    blizzard01         = 0.0f;  // [0..1]
    HoldId                   ecosystemHoldId    = HoldId::Falkreath;
    HoldId                   wildlifeHoldId     = HoldId::Whiterun;
    bool                     useWeatherSimClock = false; // If true & weatherSim != nullptr, syncs clock/season/clouds
};

struct WorldEvent {
    uint32_t       id                   = 0u;
    WorldEventKind kind                 = WorldEventKind::BlizzardCaravanRescue;
    HoldId         holdId               = HoldId::Winterhold;
    CommodityKind  commodity            = CommodityKind::Grain;
    float          urgency01            = 0.5f;  // [0..1] escalates over time while unresolved
    float          rewardGold           = 0.0f;
    float          tavernHarmonyBonus01 = 0.0f;  // Harmony/morale bonus for Aurora / Harvest festivals
    uint32_t       suggestedFlankers    = 0u;    // Tactical MeleeFlanker count hint for ambushes
    uint32_t       suggestedArchers     = 0u;    // Tactical RangedArcher count hint for ambushes
    float          ageHours             = 0.0f;
    float          maxDurationHours     = 12.0f;
};

struct EventDirectorTelemetry {
    uint32_t activeCount    = 0u;
    uint32_t totalSpawned   = 0u;
    uint32_t totalResolved  = 0u;
    uint32_t totalExpired   = 0u;
    float    highestUrgency = 0.0f;
    std::array<uint32_t, kWorldEventKindCount> spawnedByKind{};
};

const char* GetWorldEventKindName(WorldEventKind kind);

class RawWorldEventDirector {
public:
    explicit RawWorldEventDirector(const EventDirectorConfig& cfg = {});

    void Reset(const EventDirectorConfig& cfg = {});

    // Evaluates all coupled world simulators, escalates/auto-resolves/expires active events,
    // and spawns eligible emergent events subject to cooldown hysteresis and concurrency cap.
    void StepHours(float hours, const WorldEventInputs& inputs);

    // Explicitly resolves an active event by ID (returns false if ID is unknown/inactive).
    bool ResolveEvent(uint32_t eventId);

    const std::vector<WorldEvent>& ActiveEvents() const noexcept { return active_; }
    const WorldEvent*              FindActiveByKind(WorldEventKind kind) const;
    float                          CooldownRemainingHours(WorldEventKind kind) const;
    const EventDirectorConfig&     Config() const noexcept { return cfg_; }

    EventDirectorTelemetry ComputeTelemetry() const;

private:
    bool HasActiveKind(WorldEventKind kind) const;
    bool CanSpawnKind(WorldEventKind kind) const;
    void SpawnEvent(WorldEvent ev);

    EventDirectorConfig                     cfg_{};
    std::vector<WorldEvent>                 active_;
    std::array<float, kWorldEventKindCount> cooldownRemaining_{};
    std::array<uint32_t, kWorldEventKindCount> spawnedByKind_{};
    uint32_t                                nextEventId_   = 1u;
    uint32_t                                totalSpawned_  = 0u;
    uint32_t                                totalResolved_ = 0u;
    uint32_t                                totalExpired_  = 0u;
};

}  // namespace raw::worldgen
