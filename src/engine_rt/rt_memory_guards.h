#pragma once
// rt_memory_guards.h: Low-Level Creation Engine Memory, Allocator, Cell-Race & Hidden-Flag Invariant Guard.
// Protects Skyrim SE 1.7.104 against "house of cards" cross-coupled mutations:
//   1) Fail-closed master gate backed by Zydis binary offset verification (rt_zydis_offsets / rt_version)
//   2) TESObjectCELL / TESWorldSpace hidden-flag & spinlock/cellState race validator before ANY engine write
//   3) GridCellArray / uGridsToLoad / BGSTerrainManager (0x320358C) / LoadedLandData 37-slot pool race guard
//   4) BSShaderProperty 64-bit hidden flag (SF1|SF2 @ +0x38) & ScrapHeap (+0x48/+0x50/+0x84) integrity audits
//   5) Live telemetry exported via RAW_GetMemoryGuardTelemetry() and rt_control.cpp (`memory_guards`).
// Original work, MIT.

#include <cstdint>
#include <string>

#include "rt_image.h"

namespace raw::rt {

// Binary-verified TESObjectCELL::cellFlags (+0x40) bitmask constants (SkyrimSE 1.7.104):
constexpr uint16_t kCellFlagIsInterior       = 0x0001u;
constexpr uint16_t kCellFlagHasWater         = 0x0002u;
constexpr uint16_t kCellFlagCantFastTravel   = 0x0004u;
constexpr uint16_t kCellFlagNoLodWater       = 0x0010u;
constexpr uint16_t kCellFlagPublicArea       = 0x0020u;
constexpr uint16_t kCellFlagShowSky          = 0x0080u;
constexpr uint16_t kCellFlagUseSkyLighting   = 0x0100u;

// Binary-verified TESObjectCELL::cellState (+0x44) values:
constexpr uint8_t  kCellStateAttached        = 7u;

// Binary-verified BSShaderProperty::flags (+0x38) key bits (SF1 low 32 | SF2 high 32):
constexpr uint64_t kShaderFlag1ZBufferTest   = (1ull << 15);  // 0x8000 verified @ 0x151CA80
constexpr uint64_t kShaderFlag1Skinned       = (1ull << 2);   // 0x0004 verified @ 0x151CA80
constexpr uint64_t kShaderFlag1ModelNormals  = (1ull << 12);  // 0x1000 verified @ 0x151A0B0
constexpr uint64_t kShaderFlag2ZBufferWrite  = (1ull << 32);  // SF2 bit 0
constexpr uint64_t kShaderFlag2DoubleSided   = (1ull << 36);  // SF2 bit 4

// Binary-verified BSPointerHandleManager::Entry (0x21A4280) active reference handle cap constants:
constexpr uint32_t kRefHandleCapacity        = 1048576u;      // 2^20 = 1,048,576 hard CE ref-handle limit
constexpr uint32_t kRefHandleInUseBit        = 0x04000000u;   // bit 26 verified @ 0x1764F0..0x17652E
constexpr uint32_t kRefHandleEntryStride     = 16u;           // 16 bytes per BSPointerHandleManager::Entry

enum class RefHandleHealthLevel : uint8_t {
    Healthy  = 0,  // < 50%
    Elevated = 1,  // 50% .. < 70%
    Warning  = 2,  // 70% .. < 85%
    Critical = 3,  // >= 85% (imminent 1M CE ref-cap crash risk)
};

struct RefHandleSaturationReport {
    uint32_t             sampledSlots     = 0;
    uint32_t             sampledInUse     = 0;
    uint32_t             estimatedActive  = 0;
    uint32_t             capacity         = kRefHandleCapacity;
    float                saturationPct    = 0.0f;
    RefHandleHealthLevel level            = RefHandleHealthLevel::Healthy;
    bool                 warningTriggered = false;
};

enum class GuardRejectReason : uint32_t {
    Ok = 0,
    OffsetsNotVerified,
    CellNotAttached,
    CellSpinLockHeld,
    InteriorFlagFlipForbidden,
    NoWaterFlagSet,
    WaterInheritSentinelProtected,
    WaterHeightOutOfRange,
    ProtectedPointerOffset,
    GridLengthInvalid,
    GridNotEmpty,
    GridCacheDesync,
    LandPoolOverflow,
    ShaderFlagConflict,
    ScrapHeapCorruptBounds,
};

struct CellWriteRequest {
    uint16_t cellFlags          = 0;
    uint16_t proposedCellFlags  = 0;
    uint8_t  cellState          = 0;
    uint32_t spinLockCount      = 0;
    uint32_t writeOffset        = 0;
    float    currentWaterHeight = 0.0f;
    float    proposedWaterHeight = 0.0f;
    bool     binaryOffsetsVerified = true;
};

struct GuardVerdict {
    bool              allowed = false;
    GuardRejectReason reason  = GuardRejectReason::Ok;
    const char*       detail  = "ok";
};

struct GridTransitionState {
    uint32_t currentLength      = 5u;
    uint32_t capacity           = 5u;
    uint32_t loadedCells        = 0u;
    uint32_t proposedLength     = 5u;
    uint32_t terrainCachedGrids = 5u;
    uint32_t landPoolActive     = 0u;
    uint32_t landPoolFree       = 0u;
};

struct ShaderPropertyAudit {
    bool     valid              = true;
    bool     zWriteWithoutZTest = false;
    bool     nullMaterialWithPasses = false;
    uint64_t flags              = 0;
};

struct ScrapHeapAudit {
    bool     valid              = false;
    uint64_t spanBytes          = 0;
    bool     ptrContained       = false;
    GuardRejectReason reason    = GuardRejectReason::Ok;
};

struct RawMemoryGuardTelemetry {
    uint32_t tickCount          = 0;
    uint8_t  offsetsVerified    = 0;
    uint8_t  writesLocked       = 1;           // fail-closed default until startup verification passes
    uint8_t  memManagerPresent  = 0;
    uint8_t  gridsCacheDesync   = 0;
    uint8_t  landPoolPressure   = 0;
    uint8_t  activeCellLocked   = 0;
    uint8_t  activeCellState    = 0;
    uint16_t activeCellFlags    = 0;
    uint32_t landPoolFree       = 0;
    uint32_t landPoolActive     = 0;
    uint32_t terrainCachedGrids = 0;
    uint32_t liveGridLength     = 0;
    uint32_t blockedWrites      = 0;
    uint32_t passedChecks       = 0;
    uint64_t workingSetMB       = 0;
    uint64_t availPhysMB        = 0;
    // Wave 45 BSPointerHandleManager 1,048,576 ref-cap saturation watchdog:
    uint32_t refHandlesEstimated = 0;
    float    refHandleSatPct     = 0.0f;
    uint8_t  refHandleWarnLevel  = 0;   // RefHandleHealthLevel (0=Healthy, 1=Elevated, 2=Warning, 3=Critical)
};

// Pure deterministic invariant guards:
GuardVerdict ValidateCellFlagWrite(const CellWriteRequest& req);
GuardVerdict ValidateGridResizeTransition(const GridTransitionState& st);
ShaderPropertyAudit AuditShaderPropertyFlags(uint64_t flags, bool hasMaterial, bool hasRenderPasses);
ScrapHeapAudit AnalyzeScrapHeapBounds(uint64_t baseAddress, uint64_t endAddress,
                                      uint32_t activeAllocs, uint64_t testPtr = 0);
// SEH-guarded strided BSPointerHandleManager (0x21A4280) active-handle saturation sampler:
RefHandleSaturationReport SampleRefHandleSaturation(const void* handleEntriesBase,
                                                    uint32_t totalCapacity = kRefHandleCapacity,
                                                    uint32_t strideEntries = 256u);

// Live runtime hooks & master write gate:
void MemoryGuardsInit(const Image& img);
void MemoryGuardsOnPresent();
void MemoryGuardsShutdown();
bool MemoryGuardAllowLiveStructWrites();
GuardVerdict MemoryGuardCheckCellWrite(const CellWriteRequest& req);
std::string MemoryGuardsStatsJson();

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetMemoryGuardTelemetry(raw::rt::RawMemoryGuardTelemetry* out);
