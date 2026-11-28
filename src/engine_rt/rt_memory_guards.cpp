// rt_memory_guards.cpp: Low-Level Creation Engine Memory, Allocator, Cell-Race & Hidden-Flag Invariant Guard.
// Original work, MIT.
#include "rt_memory_guards.h"
#include "rt_terrain_lod.h"

#include <windows.h>
#ifndef RAW_SIM_PURE_TEST
#include "rt_forensics.h"
#include "rt_version.h"
#include <psapi.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace raw::rt {

GuardVerdict ValidateCellFlagWrite(const CellWriteRequest& req) {
    if (!req.binaryOffsetsVerified) {
        return {false, GuardRejectReason::OffsetsNotVerified, "binary offsets not verified (fail-closed)"};
    }
    if (req.cellState != kCellStateAttached) {
        return {false, GuardRejectReason::CellNotAttached, "TESObjectCELL cellState != 7 (load/unload race guard)"};
    }
    if (req.spinLockCount != 0u) {
        return {false, GuardRejectReason::CellSpinLockHeld, "TESObjectCELL spinLock (+0x120) currently held"};
    }
    // Never permit overwriting pointer fields: exteriorData (+0x68), cellLand (+0x70), worldSpace (+0x128), loadedData (+0x130)
    if (req.writeOffset == 0x68u || req.writeOffset == 0x70u ||
        req.writeOffset == 0x128u || req.writeOffset == 0x130u) {
        return {false, GuardRejectReason::ProtectedPointerOffset, "protected TESObjectCELL pointer offset"};
    }
    // Never permit flipping IsInterior (bit 0) or NoLODWater (bit 4) on a live loaded cell
    if (((req.cellFlags ^ req.proposedCellFlags) & (kCellFlagIsInterior | kCellFlagNoLodWater)) != 0u) {
        return {false, GuardRejectReason::InteriorFlagFlipForbidden, "forbidden mutation of IsInterior/NoLODWater cellFlag"};
    }
    if (req.writeOffset == 0x78u) {
        if ((req.cellFlags & kCellFlagIsInterior) != 0u || (req.cellFlags & kCellFlagHasWater) == 0u) {
            return {false, GuardRejectReason::NoWaterFlagSet, "waterHeight write requires exterior cell with HasWater bit"};
        }
        if (!std::isfinite(req.currentWaterHeight) || req.currentWaterHeight >= kCellWaterInheritSentinel * 0.99f) {
            return {false, GuardRejectReason::WaterInheritSentinelProtected, "cell inherits TESWorldSpace::defaultWaterHeight sentinel"};
        }
        if (!std::isfinite(req.proposedWaterHeight) ||
            req.proposedWaterHeight <= -50000.0f || req.proposedWaterHeight >= 50000.0f) {
            return {false, GuardRejectReason::WaterHeightOutOfRange, "proposed waterHeight out of safe world bounds"};
        }
    }
    return {true, GuardRejectReason::Ok, "ok"};
}

GuardVerdict ValidateGridResizeTransition(const GridTransitionState& st) {
    if (st.proposedLength < 3u || (st.proposedLength & 1u) == 0u || st.proposedLength > st.capacity) {
        return {false, GuardRejectReason::GridLengthInvalid, "grid length must be odd, >= 3, and <= allocated capacity"};
    }
    if (st.loadedCells != 0u && st.proposedLength != st.currentLength) {
        return {false, GuardRejectReason::GridNotEmpty, "GridCellArray non-empty (resizing causes OOB crash @ +0x1A2E7E)"};
    }
    if (st.proposedLength * st.proposedLength > kLoadedLandPoolCap) {
        return {false, GuardRejectReason::LandPoolOverflow, "proposed uGridsToLoad^2 exceeds LoadedLandData 37-slot pool cap"};
    }
    if (st.terrainCachedGrids != 0u && st.terrainCachedGrids != st.currentLength) {
        return {false, GuardRejectReason::GridCacheDesync, "BGSTerrainManager cached uGridsToLoad (0x320358C) desynchronized"};
    }
    return {true, GuardRejectReason::Ok, "ok"};
}

ShaderPropertyAudit AuditShaderPropertyFlags(uint64_t flags, bool hasMaterial, bool hasRenderPasses) {
    ShaderPropertyAudit out{};
    out.flags = flags;
    const bool zTest = (flags & kShaderFlag1ZBufferTest) != 0ull;
    const bool zWrite = (flags & kShaderFlag2ZBufferWrite) != 0ull;
    out.zWriteWithoutZTest = zWrite && !zTest;
    out.nullMaterialWithPasses = hasRenderPasses && !hasMaterial;
    out.valid = !out.zWriteWithoutZTest && !out.nullMaterialWithPasses;
    return out;
}

ScrapHeapAudit AnalyzeScrapHeapBounds(uint64_t baseAddress, uint64_t endAddress,
                                      uint32_t activeAllocs, uint64_t testPtr) {
    ScrapHeapAudit out{};
    if (baseAddress < 0x10000ull || endAddress <= baseAddress || endAddress >= 0x00007FFFFFFFFFFFull) {
        out.valid = false;
        out.reason = GuardRejectReason::ScrapHeapCorruptBounds;
        return out;
    }
    const uint64_t span = endAddress - baseAddress;
    // ScrapHeap max commit in Creation Engine is <= 256 MB; spans > 512 MB indicate corrupt base/end pointers.
    if (span > (512ull << 20) || activeAllocs > 2000000u) {
        out.valid = false;
        out.spanBytes = span;
        out.reason = GuardRejectReason::ScrapHeapCorruptBounds;
        return out;
    }
    out.valid = true;
    out.spanBytes = span;
    out.ptrContained = (testPtr != 0ull) ? (testPtr >= baseAddress && testPtr <= endAddress) : true;
    out.reason = GuardRejectReason::Ok;
    return out;
}

static bool SehSampleRefHandleBuckets(const uint8_t* entriesBase,
                                      uint32_t totalCapacity,
                                      uint32_t strideEntries,
                                      uint32_t* outSampled,
                                      uint32_t* outInUse) noexcept {
    if (!entriesBase || totalCapacity == 0u || strideEntries == 0u || !outSampled || !outInUse) {
        return false;
    }
    uint32_t sampled = 0u;
    uint32_t inUse = 0u;
    __try {
        for (uint32_t i = 0u; i < totalCapacity; i += strideEntries) {
            const uint32_t bits = *reinterpret_cast<const uint32_t*>(
                entriesBase + static_cast<size_t>(i) * kRefHandleEntryStride);
            ++sampled;
            if ((bits & kRefHandleInUseBit) != 0u) {
                ++inUse;
            }
        }
        *outSampled = sampled;
        *outInUse   = inUse;
        return sampled > 0u;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outSampled = 0u;
        *outInUse   = 0u;
        return false;
    }
}

RefHandleSaturationReport SampleRefHandleSaturation(const void* handleEntriesBase,
                                                    uint32_t totalCapacity,
                                                    uint32_t strideEntries) {
    RefHandleSaturationReport r{};
    r.capacity = totalCapacity;
    if (!handleEntriesBase || totalCapacity == 0u || strideEntries == 0u) {
        return r;
    }
    uint32_t sampled = 0u, inUse = 0u;
    if (!SehSampleRefHandleBuckets(static_cast<const uint8_t*>(handleEntriesBase),
                                   totalCapacity, strideEntries, &sampled, &inUse) || sampled == 0u) {
        return r;
    }
    r.sampledSlots    = sampled;
    r.sampledInUse    = inUse;
    const double frac = static_cast<double>(inUse) / static_cast<double>(sampled);
    r.estimatedActive = static_cast<uint32_t>(std::lround(frac * static_cast<double>(totalCapacity)));
    r.saturationPct   = static_cast<float>(std::clamp(frac * 100.0, 0.0, 100.0));
    if (r.saturationPct >= 85.0f)      r.level = RefHandleHealthLevel::Critical;
    else if (r.saturationPct >= 70.0f) r.level = RefHandleHealthLevel::Warning;
    else if (r.saturationPct >= 50.0f) r.level = RefHandleHealthLevel::Elevated;
    else                               r.level = RefHandleHealthLevel::Healthy;
    r.warningTriggered = (r.level >= RefHandleHealthLevel::Warning);
    return r;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

const uint8_t* g_base = nullptr;
std::mutex g_mu;
RawMemoryGuardTelemetry g_tel{};
std::atomic<uint32_t> g_blockedWrites{0};
std::atomic<uint32_t> g_passedChecks{0};
uint32_t g_presentTick = 0;

constexpr uint32_t kCeTesGridCellsOffset = 0x78u;
constexpr uint32_t kCeCellFlagsOffset    = 0x40u;
constexpr uint32_t kCeCellStateOffset    = 0x44u;
constexpr uint32_t kCeCellSpinLockOffset = 0x120u;
static_assert(kCeCellStateOffset == kCeCellFlagsOffset + 4u && kCeCellSpinLockOffset > kCeCellStateOffset,
              "TESObjectCELL SkyrimSE 1.7.104.0 field layout guard");

uint32_t kMemMgrRva = 0;
uint32_t kPlayerRva = 0;
uint32_t kTesRva = 0;
uint32_t kOffTesGrid = kCeTesGridCellsOffset;
uint32_t kOffCellFlags = kCeCellFlagsOffset;
uint32_t kOffCellState = kCeCellStateOffset;
uint32_t kOffCellLock = kCeCellSpinLockOffset;
uint32_t kGlobPoolFree = 0;
uint32_t kGlobPoolActive = 0;
uint32_t kGlobCachedGrids = 0;
uint32_t kHandleEntriesRva = 0;
uint32_t g_lastRefEst = 0;
float    g_lastRefPct = 0.0f;
uint8_t  g_lastRefLvl = 0;

template <class T>
T ReadAt(const uint8_t* p, uint32_t off) {
    T v{};
    std::memcpy(&v, p + off, sizeof v);
    return v;
}

bool ValidHeapPtr(const void* p) {
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);
    return a >= 0x10000ull && a < 0x00007FFFFFFFFFFFull && (a & 0x7ull) == 0;
}

void ProbeMemoryStateSeh(RawMemoryGuardTelemetry* out) {
    __try {
        if (!g_base) return;
        if (kMemMgrRva) {
            // MemoryManager::Singleton sits inline in .data at 0x369A810 (non-zero initialized flag/vtable/heaps)
            const uint64_t q0 = ReadAt<uint64_t>(g_base, kMemMgrRva);
            const uint64_t q1 = ReadAt<uint64_t>(g_base, kMemMgrRva + 8u);
            out->memManagerPresent = (q0 != 0ull || q1 != 0ull) ? 1u : 0u;
        }
        if (kGlobPoolFree) out->landPoolFree = ReadAt<uint32_t>(g_base, kGlobPoolFree);
        if (kGlobPoolActive) out->landPoolActive = ReadAt<uint32_t>(g_base, kGlobPoolActive);
        if (kGlobCachedGrids) out->terrainCachedGrids = ReadAt<uint32_t>(g_base, kGlobCachedGrids);
        out->landPoolPressure = (out->landPoolFree + out->landPoolActive >= kLoadedLandPoolCap) ? 1u : 0u;

        if (kTesRva) {
            const uint8_t* tes = ReadAt<const uint8_t*>(g_base, kTesRva);
            if (ValidHeapPtr(tes)) {
                const uint8_t* arr = ReadAt<const uint8_t*>(tes, kOffTesGrid);
                if (ValidHeapPtr(arr)) {
                    out->liveGridLength = ReadAt<uint32_t>(arr, 0x10u);
                    out->gridsCacheDesync = (out->terrainCachedGrids != 0u &&
                                             out->liveGridLength != 0u &&
                                             out->terrainCachedGrids != out->liveGridLength) ? 1u : 0u;
                }
            }
        }
        if (kPlayerRva) {
            const uint8_t* pc = ReadAt<const uint8_t*>(g_base, kPlayerRva);
            if (ValidHeapPtr(pc)) {
                const uint8_t* cell = ReadAt<const uint8_t*>(pc, 0x60u);
                if (ValidHeapPtr(cell) && cell[0x1A] == 60u) {
                    out->activeCellFlags = ReadAt<uint16_t>(cell, kOffCellFlags);
                    out->activeCellState = ReadAt<uint8_t>(cell, kOffCellState);
                    out->activeCellLocked = (ReadAt<uint32_t>(cell, kOffCellLock + 4u) != 0u) ? 1u : 0u;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

}  // namespace

void MemoryGuardsInit(const Image& img) {
    g_base = img.base;
    kMemMgrRva       = Addr("MemoryManager::Singleton");
    kPlayerRva       = Addr("PlayerCharacter::Singleton");
    kTesRva          = Addr("TES::Singleton");
    kGlobPoolFree    = Addr("global:LoadedLandPoolCount");
    kGlobPoolActive  = Addr("global:LoadedLandPoolActive");
    kGlobCachedGrids = Addr("global:TerrainCachedGridsToLoad");
    kHandleEntriesRva = Addr("BSPointerHandleManager::Entry::GetHandleEntries");
    if (uint32_t v = Off("off:TES::gridCells"))           kOffTesGrid = v;
    if (uint32_t v = Off("off:TESObjectCELL::cellFlags")) kOffCellFlags = v;
    if (uint32_t v = Off("off:TESObjectCELL::cellState")) kOffCellState = v;
    if (uint32_t v = Off("off:TESObjectCELL::spinLock"))  kOffCellLock = v;

    std::lock_guard<std::mutex> lk(g_mu);
    g_tel = {};
    const bool ok = AreBinaryOffsetsVerified();
    g_tel.offsetsVerified = ok ? 1u : 0u;
    g_tel.writesLocked = ok ? 0u : 1u;
}

void MemoryGuardsOnPresent() {
    if (!g_base) return;
    const uint32_t tick = ++g_presentTick;
    if ((tick % 60u) != 1u) return;

    RawMemoryGuardTelemetry next{};
    next.tickCount = tick;
    const bool ok = AreBinaryOffsetsVerified();
    next.offsetsVerified = ok ? 1u : 0u;
    next.writesLocked = ok ? 0u : 1u;
    next.blockedWrites = g_blockedWrites.load(std::memory_order_relaxed);
    next.passedChecks = g_passedChecks.load(std::memory_order_relaxed);

    ProbeMemoryStateSeh(&next);

    // Sample BSPointerHandleManager 1M ref-cap saturation every ~5 seconds (every 5th 60-frame guard tick):
    if (kHandleEntriesRva && ((tick / 60u) % 5u) == 0u) {
        const auto sat = SampleRefHandleSaturation(g_base + kHandleEntriesRva, kRefHandleCapacity, 256u);
        g_lastRefEst = sat.estimatedActive;
        g_lastRefPct = sat.saturationPct;
        g_lastRefLvl = static_cast<uint8_t>(sat.level);
        if (sat.warningTriggered) {
            ForensicsRecord("ref_handle_cap_warning", sat.estimatedActive, static_cast<uint64_t>(sat.level));
        }
    }
    next.refHandlesEstimated = g_lastRefEst;
    next.refHandleSatPct     = g_lastRefPct;
    next.refHandleWarnLevel  = g_lastRefLvl;

    MEMORYSTATUSEX ms{}; ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) next.availPhysMB = ms.ullAvailPhys >> 20;
    PROCESS_MEMORY_COUNTERS_EX pmc{}; pmc.cb = sizeof pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc)) {
        next.workingSetMB = pmc.WorkingSetSize >> 20;
    }

    if (next.gridsCacheDesync) {
        ForensicsRecord("grids_cache_desync", next.liveGridLength, next.terrainCachedGrids);
    }

    std::lock_guard<std::mutex> lk(g_mu);
    g_tel = next;
}

void MemoryGuardsShutdown() {
    g_base = nullptr;
}

bool MemoryGuardAllowLiveStructWrites() {
    return AreBinaryOffsetsVerified();
}

GuardVerdict MemoryGuardCheckCellWrite(const CellWriteRequest& req) {
    CellWriteRequest copy = req;
    copy.binaryOffsetsVerified = copy.binaryOffsetsVerified && AreBinaryOffsetsVerified();
    const GuardVerdict v = ValidateCellFlagWrite(copy);
    if (v.allowed) {
        g_passedChecks.fetch_add(1u, std::memory_order_relaxed);
    } else {
        g_blockedWrites.fetch_add(1u, std::memory_order_relaxed);
        ForensicsRecord("mem_guard_block", static_cast<uint64_t>(v.reason), copy.writeOffset);
    }
    return v;
}

std::string MemoryGuardsStatsJson() {
    RawMemoryGuardTelemetry t{};
    {
        std::lock_guard<std::mutex> lk(g_mu);
        t = g_tel;
    }
    char buf[768];
    std::snprintf(buf, sizeof buf,
                  "{\"cmd\":\"memory_guards\",\"offsets_verified\":%s,\"writes_locked\":%s,"
                  "\"mem_mgr_present\":%s,\"live_grid_len\":%u,\"terrain_cached_grids\":%u,"
                  "\"grids_desync\":%s,\"land_pool\":[%u,%u,37],\"land_pool_pressure\":%s,"
                  "\"active_cell_state\":%u,\"active_cell_flags\":\"0x%04X\",\"active_cell_locked\":%s,"
                  "\"passed_checks\":%u,\"blocked_writes\":%u,\"working_set_mb\":%llu,\"avail_phys_mb\":%llu,"
                  "\"ref_handles_est\":%u,\"ref_handle_sat_pct\":%.1f,\"ref_handle_level\":%u}",
                  t.offsetsVerified ? "true" : "false", t.writesLocked ? "true" : "false",
                  t.memManagerPresent ? "true" : "false", t.liveGridLength, t.terrainCachedGrids,
                  t.gridsCacheDesync ? "true" : "false", t.landPoolFree, t.landPoolActive,
                  t.landPoolPressure ? "true" : "false", t.activeCellState, t.activeCellFlags,
                  t.activeCellLocked ? "true" : "false", t.passedChecks, t.blockedWrites,
                  static_cast<unsigned long long>(t.workingSetMB),
                  static_cast<unsigned long long>(t.availPhysMB),
                  t.refHandlesEstimated, t.refHandleSatPct, t.refHandleWarnLevel);
    return buf;
}

#else
void MemoryGuardsInit(const Image&) {}
void MemoryGuardsOnPresent() {}
void MemoryGuardsShutdown() {}
bool MemoryGuardAllowLiveStructWrites() { return false; }
GuardVerdict MemoryGuardCheckCellWrite(const CellWriteRequest& req) { return ValidateCellFlagWrite(req); }
std::string MemoryGuardsStatsJson() { return "{\"cmd\":\"memory_guards\",\"mode\":\"pure_test\"}"; }
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetMemoryGuardTelemetry(raw::rt::RawMemoryGuardTelemetry* out) {
#ifndef RAW_SIM_PURE_TEST
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_mu);
    *out = raw::rt::g_tel;
    return raw::rt::g_tel.tickCount > 0u ? 1 : 0;
#else
    (void)out;
    return 0;
#endif
}
