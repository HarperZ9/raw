#pragma once
// RawWorldSerializer.h: Clean-Room Bit-Exact IEEE 802.3 CRC-32 Binary + JSON Persistent World State
// Serializer (`raw::worldgen`). Pure portable C++23 with zero Windows, D3D12, or external dependencies.
// Captures, tamper-checks, persists, and exports the complete 5-pillar living Tamriel simulation:
//   1. `RawWeatherSim`         (orbital day/hour, snowpack, surface temp, ice thickness, wind, regime)
//   2. `RawSettlementSim`      (9-hold treasuries, 6-commodity stocks & prices, active caravans, deliveries)
//   3. `RawWorldEventDirector` (active radiant events, per-kind cooldowns, spawn/resolve/expire counters)
//   4. `RawFactionDiplomacy`   (9-hold reputations, bounties, guard postures & 6-faction standings)
//   5. `RawWorldEcosystem`     (active fire cells, burned area, snow/meltwater volume, black-ice cells).
// Binary container: magic "RAWWORLD" (8B) | version (u32=1) | payloadBytes (u32) | crc32 (u32) | payload.
// Original work, MIT.

#include "RawFactionDiplomacy.h"
#include "RawSettlementSim.h"
#include "RawWeatherSim.h"
#include "RawWorldEcosystem.h"
#include "RawWorldEventDirector.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::worldgen {

inline constexpr uint32_t kRawWorldBinaryVersion = 1u;

struct WeatherSnapshot {
    float         dayOfYear             = 172.0f;
    float         timeOfDayHours        = 8.0f;
    float         timeScale             = 20.0f;
    float         snowDepthM            = 0.0f;
    float         surfaceTempC          = 10.0f;
    float         iceThicknessM         = 0.0f;
    float         beaufortWind          = 1.0f;
    float         cloudCover01          = 0.15f;
    float         barometricPressureHpa = 1013.25f;
    WeatherRegime activeRegime          = WeatherRegime::ClearAlpine;
    SeasonId      season                = SeasonId::Summer;
};

struct HoldSnapshot {
    float                              treasuryGold      = 0.0f;
    std::array<float, kCommodityCount> stock{};
    std::array<float, kCommodityCount> price{};
    bool                               marketOpen        = true;
    uint32_t                           activeGuards      = 12u;
    float                              tavernOccupancy01 = 0.25f;
};

struct SettlementSnapshot {
    float                                simHoursElapsed     = 0.0f;
    float                                timeOfDayHours      = 8.0f;
    uint32_t                             deliveriesCompleted = 0u;
    std::array<HoldSnapshot, kHoldCount> holds{};
    std::vector<TradeCaravan>            caravans;
};

struct EventDirectorSnapshot {
    uint32_t                                   totalSpawned  = 0u;
    uint32_t                                   totalResolved = 0u;
    uint32_t                                   totalExpired  = 0u;
    std::array<float, kWorldEventKindCount>    cooldownRemaining{};
    std::array<uint32_t, kWorldEventKindCount> spawnedByKind{};
    std::vector<WorldEvent>                    activeEvents;
};

struct DiplomacySnapshot {
    std::array<HoldStanding, kHoldCount>       holds{};
    std::array<FactionStanding, kFactionCount> factions{};
};

struct EcosystemSnapshot {
    uint32_t activeFireCells   = 0u;
    uint32_t blackIceCells     = 0u;
    float    burnedAreaM2      = 0.0f;
    float    totalSnowVolumeM3 = 0.0f;
    float    totalMeltRunoffM3 = 0.0f;
    float    totalWaterMassM3  = 0.0f;
    float    peakTempC         = 0.0f;
    float    steamFlux         = 0.0f;
};

struct WorldSnapshotState {
    WeatherSnapshot       weather{};
    SettlementSnapshot    settlement{};
    EventDirectorSnapshot events{};
    DiplomacySnapshot     diplomacy{};
    EcosystemSnapshot     ecosystem{};
};

class RawWorldSerializer {
public:
    // Clean-room IEEE 802.3 CRC-32 checksum (poly 0xEDB88320).
    static uint32_t ComputeCrc32(const uint8_t* data, size_t size) noexcept;

    // Captures a complete deterministic snapshot from any non-null world simulation systems.
    static WorldSnapshotState CaptureFromSystems(const RawWeatherSim*         weather,
                                                 const RawSettlementSim*      settlement,
                                                 const RawWorldEventDirector* events,
                                                 const RawFactionDiplomacy*   diplomacy,
                                                 const RawWorldEcosystem*     ecosystem);

    // Restores captured snapshot fields cleanly back into live systems using public APIs.
    static void RestoreIntoSystems(const WorldSnapshotState& snap,
                                   RawWeatherSim*            weather,
                                   RawSettlementSim*         settlement,
                                   RawWorldEventDirector*    events,
                                   RawFactionDiplomacy*      diplomacy);

    // Bit-exact binary serialization with "RAWWORLD" v1 header & IEEE 802.3 CRC-32 integrity check.
    static bool SerializeWorldBinary(const WorldSnapshotState& state,
                                     std::vector<uint8_t>& outBytes);

    static bool DeserializeWorldBinary(const uint8_t* data, size_t size,
                                       WorldSnapshotState& outState,
                                       std::string* outErr = nullptr);

    // Disk persistence helpers:
    static bool SaveWorldToFile(const WorldSnapshotState& state,
                                const std::string& filePath,
                                std::string* outErr = nullptr);

    static bool LoadWorldFromFile(const std::string& filePath,
                                  WorldSnapshotState& outState,
                                  std::string* outErr = nullptr);

    // Human-readable deterministic JSON world state report:
    static std::string SerializeWorldJson(const WorldSnapshotState& state);
};

}  // namespace raw::worldgen
