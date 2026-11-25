#pragma once
// rt_terrain_lod.h: Binary-Verified Creation Engine Terrain, LOD4/8/16/32 Transition & Floating-Origin Stabilizer.
// Directly inspects loaded GridCellArray (NxN cells), TESObjectLAND::LoadedLandData, TESWorldSpace defaults,
// and BGSTerrainManager cached LOD bands (SkyrimSE.exe 1.7.104 binary-verified offsets, SEH-hardened).
// Features:
//   1) Cell-snapped double-precision floating-origin stabilizer (eliminates FP32 jitter at Winterhold/far coords)
//   2) LOD4/8/16/32 transition band & uGridsToLoad / 37-slot LoadedLandData pool integrity solver
//   3) Cell-border heightmap, skirt & water-level consistency auditor + safe runtime NaN/sentinel repair (save-safe)
//   4) Live telemetry exported via RAW_GetTerrainLodTelemetry() and rt_control.cpp (`terrain_lod`).
// Original work, MIT.

#include <cstdint>
#include <string>
#include <vector>

#include "rt_image.h"

namespace raw::rt {

constexpr float kCellWorldSizeU = 4096.0f;
constexpr float kCellWaterInheritSentinel = 2147483648.0f;  // 0x4F000000 verified @ SkyrimSE 1.7.104 RVA 0x2BEE1F
constexpr uint32_t kLoadedLandPoolCap = 37u;                // 0x25 verified @ SkyrimSE 1.7.104 RVA 0x2B4BC3

struct FloatingOriginState {
    double   originWorld[3]    = {0.0, 0.0, 0.0};  // cell-snapped double-precision anchor
    int32_t  originCellX       = 0;
    int32_t  originCellY       = 0;
    float    localCamPos[3]    = {0.0f, 0.0f, 0.0f};
    float    rawWorldUlpU      = 0.0f;             // FP32 ULP at raw world coordinates
    float    stabilizedUlpU    = 0.0f;             // FP32 ULP after floating-origin subtraction
    float    precisionGain     = 1.0f;             // rawWorldUlpU / stabilizedUlpU
    uint32_t recenterCount     = 0;
    bool     initialized       = false;
};

struct LodTransitionInput {
    uint32_t uGridsToLoad      = 5u;
    float    blockLevel0Dist   = 75000.0f;         // LOD4 -> LOD8 transition
    float    blockLevel1Dist   = 140000.0f;        // LOD8 -> LOD16 transition
    float    blockMaxDist      = 250000.0f;        // LOD16 -> LOD32 / far clip
    float    splitDistMult     = 1.75f;
    float    treeLoadDist      = 150000.0f;
    uint32_t cachedGridsToLoad = 5u;               // BGSTerrainManager cached 0x320358C
    uint32_t lockedTerrainLod  = 32u;              // MapMenu uLockedTerrainLOD 0x3203584
};

struct LodTransitionBands {
    float    activeGridRadiusU = 10240.0f;         // (uGridsToLoad / 2.0) * 4096
    float    lod4StartU        = 10240.0f;
    float    lod8StartU        = 75000.0f;
    float    lod16StartU       = 140000.0f;
    float    lod32StartU       = 250000.0f;
    float    lod4BlendWidthU   = 2048.0f;
    float    lod8BlendWidthU   = 4096.0f;
    float    lod16BlendWidthU  = 8192.0f;
    float    skirtClampDepthU  = 512.0f;
    bool     hasBandGap        = false;
    bool     hasBandInversion  = false;
    bool     gridsCacheDesync  = false;
    bool     landPoolOverflowRisk = false;         // true if uGridsToLoad^2 > 37
};

struct CellTerrainSample {
    uint32_t formId            = 0;
    int32_t  cellX             = 0;
    int32_t  cellY             = 0;
    uint16_t cellFlags         = 0;
    uint8_t  cellState         = 0;
    uint32_t spinLockCount     = 0;
    bool     hasLand           = false;
    bool     landLoaded        = false;
    float    minHeight         = 0.0f;
    float    maxHeight         = 0.0f;
    float    baseHeight        = 0.0f;
    float    waterHeight       = kCellWaterInheritSentinel;
    // Edge mean heights along the 4 cell boundaries for neighbor seam checking: 0=North(+Y), 1=South(-Y), 2=East(+X), 3=West(-X)
    float    edgeHeight[4]     = {0.0f, 0.0f, 0.0f, 0.0f};
    bool     edgeHeightValid[4] = {false, false, false, false};
    uint32_t nanHeightVerts    = 0;
};

struct TerrainBorderAudit {
    uint32_t cellsInspected       = 0;
    uint32_t attachedCells        = 0;
    uint32_t landLoadedCells      = 0;
    uint32_t nanOrInfAnomalies    = 0;
    uint32_t minMaxInversions     = 0;
    uint32_t waterAnomalies       = 0;
    uint32_t waterBorderSteps     = 0;
    uint32_t heightSeamTears      = 0;
    uint32_t anomaliesRepaired    = 0;
    float    maxBorderHeightDeltaU = 0.0f;
    float    maxBorderWaterDeltaU  = 0.0f;
};

struct RawTerrainLodTelemetry {
    uint32_t tickCount            = 0;
    uint32_t gridLength           = 0;
    int32_t  gridCenterX          = 0;
    int32_t  gridCenterY          = 0;
    uint32_t cellsLoaded          = 0;
    uint32_t attachedCells        = 0;
    uint32_t landLoadedCells      = 0;
    uint32_t landPoolFree         = 0;
    uint32_t landPoolActive       = 0;
    uint32_t anomaliesDetected    = 0;
    uint32_t anomaliesRepaired    = 0;
    uint32_t heightSeamTears      = 0;
    uint32_t waterBorderSteps     = 0;
    float    defaultLandHeight    = -2048.0f;
    float    defaultWaterHeight   = -14000.0f;
    float    maxBorderHeightDeltaU = 0.0f;
    float    maxBorderWaterDeltaU  = 0.0f;
    double   originWorld[3]       = {0.0, 0.0, 0.0};
    float    localCamPos[3]       = {0.0f, 0.0f, 0.0f};
    float    rawWorldUlpU         = 0.0f;
    float    stabilizedUlpU       = 0.0f;
    float    precisionGain        = 1.0f;
    float    lodBandStarts[4]     = {10240.0f, 75000.0f, 140000.0f, 250000.0f};
    uint8_t  hasBandAnomaly       = 0;
    uint8_t  gridsCacheDesync     = 0;
    uint8_t  landPoolOverflowRisk = 0;
    uint8_t  writesEnabled        = 0;             // 1 only when Zydis binary offset verification passed
};

// Pure deterministic kernels (directly unit-tested in ce_runtime_check and executed live in raw_engine.dll):
float ComputeFp32Ulp(float v);
void UpdateFloatingOrigin(FloatingOriginState& state, const float worldPos[3], float recenterThresholdU = 2048.0f);
LodTransitionBands ComputeSeamlessLodBands(const LodTransitionInput& in);
TerrainBorderAudit AuditAndRepairCellGrid(std::vector<CellTerrainSample>& cells,
                                          float defaultLandHeight, float defaultWaterHeight,
                                          float seamToleranceU = 64.0f, bool allowRepair = true);

// Live runtime hooks:
void TerrainLodInit(const Image& img);
void TerrainLodOnPresent();
void TerrainLodShutdown();
std::string TerrainLodStatsJson();

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetTerrainLodTelemetry(raw::rt::RawTerrainLodTelemetry* out);
