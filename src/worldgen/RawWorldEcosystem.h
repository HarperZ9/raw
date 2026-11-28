#pragma once
// RawWorldEcosystem.h: Coupled Cellular Wildfire, Thermal Snowmelt Runoff, Black-Ice & Seasonal Regrowth
// Field (`raw::worldgen`). Pure portable C++23 with zero Windows, D3D12, or proprietary SDK dependencies.
// Unifies:
//   - Rothermel-inspired directional wildfire propagation (Beaufort wind + terrain slope preheating, fuel burnout, char)
//   - Rain & moisture wildfire suppression + fire-on-snow / rain-on-fire steam flux emission
//   - Exact mass-conserving D8 downhill snowmelt runoff (snowWaterEq + meltWater)
//   - Sub-zero (< -1.5 C) standing meltwater black-ice refreezing
//   - Multi-day seasonal biomass regrowth & char scar recovery (`AdvanceSeasonDays`)
//   - Lightweight `EcoFieldTelemetry` for shader/bridge/HUD consumption.
// Original work, MIT.

#include "RawTerrainGen.h"

#include <cstdint>
#include <vector>

namespace raw::worldgen {

struct EcoGridConfig {
    uint32_t gridW                 = 64u;
    uint32_t gridH                 = 64u;
    float    originX               = 0.0f;  // World meters
    float    originY               = 0.0f;  // World meters
    float    cellSizeM             = 2.0f;  // Meters per cell (64x64 @ 2m = 128m field)
    float    snowWaterEquivDensity = 0.30f; // Snow-water-equivalent density ratio (0.30 = 300 kg/m^3)
};

struct EcoCell {
    float elevationM      = 0.0f;
    float fuelLoad01      = 0.65f; // Dry grass / brush / conifer needles [0..1]
    float moisture01      = 0.20f; // Surface & soil moisture [0..1]
    float snowDepthM      = 0.0f;  // Snowpack depth (meters)
    float meltWaterM      = 0.0f;  // Standing / flowing liquid meltwater depth (meters)
    float blackIce01      = 0.0f;  // Slick sub-zero refrozen black-ice sheen [0..1]
    float fireIntensity01 = 0.0f;  // Active flaming combustion intensity [0..1]
    float surfaceTempC    = 12.0f; // Cell surface temperature (Celsius)
    float char01          = 0.0f;  // Burned / charred scar intensity [0..1]
    bool  isRiverWater    = false; // Permanent open river/lake barrier
};

struct EcoWeatherInput {
    float ambientTempC      = 14.0f;
    float windDirX          = 1.0f; // Normalized internally
    float windDirY          = 0.0f;
    float windBeaufort      = 2.0f; // [0..12] Beaufort scale
    float rainRate01        = 0.0f; // [0..1]
    float snowRate01        = 0.0f; // [0..1]
    float solarInsolation01 = 0.6f; // [0..1]
};

struct EcoFieldTelemetry {
    uint32_t activeFireCells   = 0u;
    float    burnedAreaM2      = 0.0f;
    float    totalSnowVolumeM3 = 0.0f;
    float    totalMeltRunoffM3 = 0.0f;
    float    totalWaterMassM3  = 0.0f; // snowVolume * SWE + meltWaterVolume
    uint32_t blackIceCells     = 0u;
    float    peakTempC         = 0.0f;
    float    steamFlux         = 0.0f; // Steam emitted during most recent Step()
};

class RawWorldEcosystem {
public:
    RawWorldEcosystem();
    explicit RawWorldEcosystem(const EcoGridConfig& cfg);

    void Reset(const EcoGridConfig& cfg = {});

    // Optional terrain-chunk initializer: resamples elevation, wetness, and snow from a TerrainChunkData.
    void InitFromTerrainChunk(const TerrainChunkData& chunk, float unitsPerMeter = 70.0f,
                              float waterLevelZ = -14000.0f);

    // Ignites eligible cells (fuel > 0.1, moisture < 0.65, !isRiverWater, thin/no snow) inside radiusM.
    // Returns number of cells ignited.
    uint32_t IgniteAt(float worldX, float worldY, float radiusM, float heatImpulseC = 480.0f);

    // Advances coupled wildfire propagation, thermal snowmelt, D8 downhill runoff, and black-ice freezing.
    void Step(float dtSec, const EcoWeatherInput& wx);

    // Simulates multi-day spring/summer ecological recovery (biomass fuel regrowth & char scar decay).
    void AdvanceSeasonDays(float days);

    // Grid accessors:
    uint32_t Width()  const noexcept { return cfg_.gridW; }
    uint32_t Height() const noexcept { return cfg_.gridH; }
    const EcoGridConfig& Config() const noexcept { return cfg_; }

    EcoCell*       CellAt(uint32_t gx, uint32_t gy);
    const EcoCell* CellAt(uint32_t gx, uint32_t gy) const;
    bool           WorldToCell(float worldX, float worldY, uint32_t* outGx, uint32_t* outGy) const;
    Vec2           CellCenterWorldM(uint32_t gx, uint32_t gy) const;

    EcoFieldTelemetry ComputeTelemetry() const;

private:
    void StepSubstep(float subDt, const EcoWeatherInput& wx, float windDx, float windDy, float windMps);

    EcoGridConfig        cfg_{};
    std::vector<EcoCell> cells_;
    std::vector<EcoCell> scratch_;
    std::vector<float>   waterDelta_;
    float                lastSteamFlux_ = 0.0f;
};

}  // namespace raw::worldgen
