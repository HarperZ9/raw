#pragma once
// RawHydrologyGen.h: Real-Time Hydraulic & Thermal Terrain Erosion + D8 Watershed River Network
// Hydrology Engine (`raw::worldgen`).
// Standalone clean-room MIT C++20/23 framework with zero external/Skyrim/D3D12 header dependencies.
// Features:
//   - Particle-based hydraulic droplet erosion with bilinear elevation/gradient sampling,
//     momentum inertia, stream-power sediment capacity, smooth Gaussian brush footprint
//     (zero single-cell pinholes), deposition, and exponential evaporation
//   - Isotropic 8-neighbor thermal talus-angle scree relaxation (strict volume conservation)
//   - Exact D8 steepest-descent watershed drainage graph + Kahn topological upstream flow
//     accumulation, continuous river valley carving, riverbank wetness, and waterfall extraction
//   - Seamless TerrainChunkData integration updating normals, curvature, RiverPebbleSand splat1.z,
//     and wetness while keeping chunk boundaries watertight and 8-channel splats normalized
// Original work, MIT.

#include "RawTerrainGen.h"

#include <cstdint>
#include <vector>

namespace raw::worldgen {

struct HydraulicErosionConfig {
    uint32_t seed                   = 202643u;
    uint32_t dropletCount           = 4096u;
    uint32_t maxLifetime            = 64u;
    float    cellSizeU              = 1.0f;   // World-space spacing between adjacent grid samples
    float    inertia                = 0.25f;  // [0..0.95] velocity direction persistence
    float    minSlope               = 0.01f;  // Floor slope for sediment capacity on flats
    float    sedimentCapacityFactor = 4.0f;   // Multiplier on stream-power capacity
    float    erodeSpeed             = 0.30f;  // Fraction of surplus capacity eroded per step
    float    depositSpeed           = 0.30f;  // Fraction of excess sediment deposited per step
    float    evaporateSpeed         = 0.025f; // Per-step water loss fraction
    float    gravity                = 4.0f;   // Downhill droplet acceleration scale
    float    initialWater           = 1.0f;
    float    initialSpeed           = 1.0f;
    int      erosionRadius          = 2;      // Gaussian footprint half-width in cells (>=1)
    float    maxDeltaPerStep        = 0.28f;  // Max elevation cut per step as fraction of cellSizeU
};

struct HydraulicErosionStats {
    uint32_t dropletsSimulated    = 0u;
    uint64_t totalSteps           = 0ull;
    double   totalErodedVolume    = 0.0; // Total positive elevation removed across grid cells
    double   totalDepositedVolume = 0.0; // Total positive elevation deposited across grid cells
    double   netHeightChange      = 0.0; // sum(h_after) - sum(h_before)
    float    maxErosionDepth      = 0.0f;
    float    maxDepositHeight     = 0.0f;
    bool     allFinite            = true;
};

struct ThermalErosionConfig {
    uint32_t iterations    = 24u;
    float    cellSizeU     = 1.0f;
    float    talusAngleDeg = 36.0f; // Physical repose angle (granite/basalt scree ~ 36 deg)
    float    transferRate  = 0.42f; // Clamped (0..0.5] for unconditional numerical stability
    bool     use8Neighbors = true;  // Isotropic Moore 8-neighborhood (exact sqrt(2) diagonals)
};

struct ThermalErosionStats {
    uint32_t iterationsRun   = 0u;
    float    maxSlopeBefore  = 0.0f; // Max |dh|/dist before thermal relaxation
    float    maxSlopeAfter   = 0.0f; // Max |dh|/dist after thermal relaxation
    double   totalMassMoved  = 0.0;
    double   netHeightChange = 0.0;  // Strictly ~0.0 (mass-conserving redistribution)
    bool     allFinite       = true;
};

struct WatershedConfig {
    float cellSizeU            = 1.0f;
    float originX              = 0.0f;
    float originY              = 0.0f;
    float riverFlowThreshold   = 18.0f; // Minimum upstream contributing cells for active river channel
    float riverCarveDepthU     = 0.0f;  // <=0 auto-scales to 0.55 * cellSizeU; >0 uses explicit world depth
    float waterfallMinSlopeTan = 0.65f; // Minimum downhill slope tan(angle) (~33 deg) for waterfall
    float waterfallMinFlow     = 20.0f; // Minimum upstream flow accumulation for waterfall detection
    bool  carveRiverValleys    = true;
    bool  preserveBorderEdge   = false; // Smoothly taper valley carving to 0 on outer 2-ring border
};

struct WaterfallPoint {
    uint32_t gridX       = 0u;
    uint32_t gridY       = 0u;
    Vec3     worldPos{};               // Crest position (worldX, worldY, heightZ)
    Vec2     flowDir{1.0f, 0.0f};      // Unit horizontal downstream flow direction
    float    dropHeightU = 0.0f;       // Vertical drop to steepest downhill neighbor
    float    slopeTan    = 0.0f;       // dropHeightU / horizontal step distance
    float    flowAccum   = 0.0f;       // Upstream contributing cell count
};

struct WatershedResult {
    uint32_t                    width          = 0u;
    uint32_t                    height         = 0u;
    std::vector<int8_t>         d8Dir;         // -1 = outlet/pit, 0..7 = steepest downhill D8 neighbor
    std::vector<float>          flowAccum;     // Upstream drainage area in cells (>= 1.0)
    std::vector<float>          riverMask01;   // [0..1] continuous river channel intensity
    std::vector<float>          riverDepthU;   // Carved river incision depth (world units >= 0)
    std::vector<float>          wetness01;     // [0..1] channel + dilated riverbank wetness
    std::vector<WaterfallPoint> waterfalls;    // Detected waterfalls sorted by flow * drop descending
    float                       maxFlowAccum   = 0.0f;
    uint32_t                    riverCellCount = 0u;
    bool                        ok             = false;
};

struct HydrologyChunkConfig {
    bool                   enableHydraulic = true;
    bool                   enableThermal   = true;
    bool                   enableWatershed = true;
    HydraulicErosionConfig hydraulic{};
    ThermalErosionConfig   thermal{};
    WatershedConfig        watershed{};
    float                  bankSplatStrength = 0.72f; // Max blend weight toward RiverPebbleSand (splat1.z)
};

// 1. Particle-based hydraulic droplet erosion with smooth Gaussian brush footprint.
//    Optional outErosionMap / outDepositMap record cumulative per-cell erosion & deposition depths.
HydraulicErosionStats ApplyHydraulicErosion(std::vector<float>& heights,
                                            uint32_t width,
                                            uint32_t height,
                                            const HydraulicErosionConfig& cfg,
                                            std::vector<float>* outErosionMap = nullptr,
                                            std::vector<float>* outDepositMap = nullptr);

// 2. Mass-conserving thermal talus-angle scree relaxation across a heightfield grid.
ThermalErosionStats ApplyThermalErosion(std::vector<float>& heights,
                                        uint32_t width,
                                        uint32_t height,
                                        const ThermalErosionConfig& cfg);

// 3. Exact D8 steepest-descent watershed drainage, topological upstream flow accumulation,
//    continuous downhill river valley carving, riverbank wetness, and waterfall extraction.
WatershedResult ComputeWatershedHydrology(std::vector<float>& heights,
                                          uint32_t width,
                                          uint32_t height,
                                          const WatershedConfig& cfg);

// 4. Applies full hydraulic + thermal + D8 watershed hydrology to an existing TerrainChunkData,
//    preserving watertight border seams and updating normals, curvature, flow, erosion, wetness,
//    riverDepth, and strictly normalized 8-channel PBR splats (splat1.z = RiverPebbleSand).
bool ApplyHydrologyToTerrainChunk(TerrainChunkData& chunk,
                                  const HydrologyChunkConfig& cfg,
                                  WatershedResult* outWatershed = nullptr);

// 5. Optional RawTerrainGen::GenerateTerrainChunk hook (default OFF so existing deterministic
//    tests remain 100% bit-exact unchanged; enable via ScopedTerrainGenHydrologyErosion or setter).
void SetTerrainGenHydrologyErosionEnabled(bool enabled) noexcept;
bool IsTerrainGenHydrologyErosionEnabled() noexcept;

struct ScopedTerrainGenHydrologyErosion {
    bool previous = false;
    explicit ScopedTerrainGenHydrologyErosion(bool enable = true) noexcept
        : previous(IsTerrainGenHydrologyErosionEnabled()) {
        SetTerrainGenHydrologyErosionEnabled(enable);
    }
    ~ScopedTerrainGenHydrologyErosion() noexcept {
        SetTerrainGenHydrologyErosionEnabled(previous);
    }
    ScopedTerrainGenHydrologyErosion(const ScopedTerrainGenHydrologyErosion&) = delete;
    ScopedTerrainGenHydrologyErosion& operator=(const ScopedTerrainGenHydrologyErosion&) = delete;
};

// Internal hook registration symbol defined in RawTerrainGen.cpp (decouples link targets).
using TerrainHydrologyHookFn = void (*)(TerrainChunkData& chunk, const TerrainConfig& cfg);
void RegisterTerrainGenHydrologyHook(TerrainHydrologyHookFn hook, bool enabled) noexcept;
bool QueryTerrainGenHydrologyHookEnabled() noexcept;

}  // namespace raw::worldgen
