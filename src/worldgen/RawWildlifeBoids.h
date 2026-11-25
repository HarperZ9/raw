#pragma once
// RawWildlifeBoids.h: Clean-Room O(N) 3D Spatial-Hash Wildlife Boids & Ecological Fauna Engine (`raw::worldgen`).
// Pure portable C++23 with zero Windows, D3D12, or proprietary SDK dependencies.
// Simulates 4 distinct Tamriel fauna guilds with live weather, physics, acoustic, and wildfire coupling:
//   1. `AlpineHawk`        — Mountain thermal updraft soaring (600..2200u AGL), banked turns, storm/blizzard roosting
//   2. `PineCrowFlock`     — O(N) 3D spatial-hash Reynolds canopy flocking + explosive acoustic/wildfire upward flush
//   3. `TundraDeerHerd`    — Ground-locked heightfield grazing herds + upwind wildfire/predator evasion
//   4. `RiverSalmonSchool` — Depth-confined aquatic schooling ([terrainZ + 12u .. waterLevelZ - 15u]) against river current.
// Original work, MIT.

#include "RawWorldEcosystem.h"
#include "RawWorldPhysics.h"

#include <array>
#include <cstdint>
#include <vector>

namespace raw::worldgen {

enum class FaunaGuild : uint8_t {
    AlpineHawk        = 0,
    PineCrowFlock     = 1,
    TundraDeerHerd    = 2,
    RiverSalmonSchool = 3,
    Count             = 4,
};

enum class StimulusKind : uint8_t {
    LoudNoise = 0,
    Wildfire  = 1,
    Predator  = 2,
};

struct WildlifeStimulus {
    Vec3         position{};
    float        radiusU    = 800.0f;
    StimulusKind kind       = StimulusKind::LoudNoise;
    float        strength01 = 1.0f;
    float        ttlSec     = 2.0f;
};

struct WildlifeEnvInput {
    float solarInsolation01  = 0.75f;  // [0..1] drives solar thermal updrafts for AlpineHawk
    float rainIntensity01    = 0.0f;   // [0..1]
    float snowIntensity01    = 0.0f;   // [0..1]
    float windBeaufort       = 2.0f;   // [0..12]
    Vec2  windDirXY{1.0f, 0.0f};       // Horizontal wind direction
    Vec2  riverCurrentXY{1.0f, 0.0f};  // Downstream river flow direction (salmon swim upstream against it)
    float defaultWaterLevelZ = 0.0f;   // Fallback water surface Z when heightfield absent
    float ecoUnitsPerMeter   = 70.0f;  // Scale converting RawWorldEcosystem meters -> world units
};

struct WildlifeAgent {
    uint32_t   id               = 0u;
    FaunaGuild guild            = FaunaGuild::AlpineHawk;
    Vec3       position{};
    Vec3       velocity{};
    Vec3       homeAnchor{};
    float      bankAngleRad     = 0.0f;
    float      startledTimerSec = 0.0f;
    float      phaseRad         = 0.0f;
    bool       roosting         = false;
    bool       startled         = false;
};

struct WildlifeTelemetry {
    uint32_t activeAgents  = 0u;
    uint32_t roostingCount = 0u;
    uint32_t startledCount = 0u;
    float    meanSpeed     = 0.0f;
    double   stepMs        = 0.0;
};

class RawWildlifeBoids {
public:
    static constexpr uint32_t kMaxTotalAgents = 4096u;
    static constexpr uint32_t kHashBuckets    = 2048u;

    void Clear();

    // Deterministically spawns `count` agents of `guild` around `centerPos` (safely clamped to kMaxTotalAgents).
    // Returns actual number of agents added.
    uint32_t SpawnGuild(FaunaGuild guild, uint32_t count, Vec3 centerPos,
                        float spreadRadiusU, uint32_t seed = 2026u);

    // Emits a transient acoustic, wildfire, or predator stimulus affecting nearby fauna.
    void EmitStimulus(Vec3 pos, float radiusU, StimulusKind kind,
                      float strength01 = 1.0f, float durationSec = 2.0f);

    // Advances all wildlife guilds in O(N) via 3D spatial hash with optional physics & ecosystem coupling.
    WildlifeTelemetry Step(float dtSec,
                           const WildlifeEnvInput& env,
                           const RawWorldPhysics* physics = nullptr,
                           const RawWorldEcosystem* eco = nullptr);

    const std::vector<WildlifeAgent>& Agents() const noexcept { return agents_; }
    std::vector<WildlifeAgent>&       AgentsMutable() noexcept { return agents_; }
    WildlifeTelemetry                 GetLastTelemetry() const noexcept { return lastTelemetry_; }

private:
    static uint32_t HashCell3I(int32_t cx, int32_t cy, int32_t cz) noexcept;
    void            RebuildSpatialHash(float invCellSize);

    std::vector<WildlifeAgent>          agents_;
    std::vector<WildlifeStimulus>       stimuli_;
    std::array<int32_t, kHashBuckets>   bucketHead_{};
    std::vector<int32_t>                nextInBucket_;
    std::vector<Vec3>                   steerAccel_;
    uint32_t                            nextId_ = 1u;
    WildlifeTelemetry                   lastTelemetry_{};
};

}  // namespace raw::worldgen
