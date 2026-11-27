// rt_terrain_lod.cpp: Binary-Verified Creation Engine Terrain, LOD4/8/16/32 Transition & Floating-Origin Stabilizer.
// Original work, MIT.
#include "rt_terrain_lod.h"

#include <Zydis/Zydis.h>

#ifndef RAW_SIM_PURE_TEST
#include "rt_detour.h"
#include "rt_forensics.h"
#include "rt_memory_guards.h"
#include "rt_patch.h"
#include "rt_version.h"
#include "rt_view.h"
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace raw::rt {

// Wave 82 ABI layout guards (SkyrimSE 1.7.104.0 verified PlayerCamera & TES::gridCells offsets):
static constexpr uint32_t kCe17104OffCamCurrentState = 0x28u;
static constexpr uint32_t kCe17104OffCamFreeState    = 0xD0u;
static constexpr uint32_t kCe17104OffTesGridCells    = 0x78u;
static_assert(kCe17104OffCamCurrentState < kCe17104OffCamFreeState, "PlayerCamera ABI ordering");
static_assert(kCe17104OffTesGridCells == 0x78u, "TES::gridCells verified ABI offset");

float ComputeFp32Ulp(float v) {
    if (!std::isfinite(v)) return std::numeric_limits<float>::infinity();
    const float av = std::fabs(v);
    if (av < 1.0f) return std::nextafter(1.0f, 2.0f) - 1.0f;
    return std::nextafter(av, std::numeric_limits<float>::infinity()) - av;
}

void UpdateFloatingOrigin(FloatingOriginState& state, const float worldPos[3], float recenterThresholdU) {
    if (!worldPos) return;
    if (!std::isfinite(worldPos[0]) || !std::isfinite(worldPos[1]) || !std::isfinite(worldPos[2])) return;
    const double wx = static_cast<double>(worldPos[0]);
    const double wy = static_cast<double>(worldPos[1]);
    const double wz = static_cast<double>(worldPos[2]);
    const double thresh = static_cast<double>(std::max(512.0f, recenterThresholdU));

    const double dx = wx - state.originWorld[0];
    const double dy = wy - state.originWorld[1];
    const double dz = wz - state.originWorld[2];
    const double distXY = std::sqrt(dx * dx + dy * dy);

    if (!state.initialized || distXY > thresh || std::fabs(dz) > thresh) {
        // Snap XY anchor strictly to Creation Engine 4096u cell boundaries so terrain quads stay bit-aligned.
        state.originCellX = static_cast<int32_t>(std::floor(wx / static_cast<double>(kCellWorldSizeU) + 0.5));
        state.originCellY = static_cast<int32_t>(std::floor(wy / static_cast<double>(kCellWorldSizeU) + 0.5));
        state.originWorld[0] = static_cast<double>(state.originCellX) * static_cast<double>(kCellWorldSizeU);
        state.originWorld[1] = static_cast<double>(state.originCellY) * static_cast<double>(kCellWorldSizeU);
        state.originWorld[2] = std::floor(wz / 1024.0 + 0.5) * 1024.0;
        if (state.initialized) ++state.recenterCount;
        state.initialized = true;
    }

    state.localCamPos[0] = static_cast<float>(wx - state.originWorld[0]);
    state.localCamPos[1] = static_cast<float>(wy - state.originWorld[1]);
    state.localCamPos[2] = static_cast<float>(wz - state.originWorld[2]);

    const float rawMaxCoord = std::max({std::fabs(worldPos[0]), std::fabs(worldPos[1]), std::fabs(worldPos[2]), 1.0f});
    const float locMaxCoord = std::max({std::fabs(state.localCamPos[0]), std::fabs(state.localCamPos[1]),
                                        std::fabs(state.localCamPos[2]), 1.0f});
    state.rawWorldUlpU = ComputeFp32Ulp(rawMaxCoord);
    state.stabilizedUlpU = ComputeFp32Ulp(locMaxCoord);
    state.precisionGain = state.stabilizedUlpU > 0.0f ? (state.rawWorldUlpU / state.stabilizedUlpU) : 1.0f;
}

LodTransitionBands ComputeSeamlessLodBands(const LodTransitionInput& in) {
    LodTransitionBands out{};
    const uint32_t grids = (in.uGridsToLoad >= 3u && (in.uGridsToLoad & 1u) == 1u) ? in.uGridsToLoad : 5u;
    out.activeGridRadiusU = (static_cast<float>(grids) * 0.5f) * kCellWorldSizeU;
    out.landPoolOverflowRisk = (grids * grids) > kLoadedLandPoolCap;
    out.gridsCacheDesync = (in.cachedGridsToLoad != 0u && in.cachedGridsToLoad != grids);

    const float mult = std::isfinite(in.splitDistMult) ? std::clamp(in.splitDistMult, 0.5f, 4.0f) : 1.0f;
    const float rawL0 = (std::isfinite(in.blockLevel0Dist) && in.blockLevel0Dist > 0.0f) ? in.blockLevel0Dist : 75000.0f;
    const float rawL1 = (std::isfinite(in.blockLevel1Dist) && in.blockLevel1Dist > 0.0f) ? in.blockLevel1Dist : 140000.0f;
    const float rawMax = (std::isfinite(in.blockMaxDist) && in.blockMaxDist > 0.0f) ? in.blockMaxDist : 250000.0f;

    if (rawL0 < out.activeGridRadiusU) out.hasBandGap = true;
    if (rawL1 <= rawL0 || rawMax <= rawL1) out.hasBandInversion = true;

    // Enforce monotonic, seamless LOD4 -> LOD8 -> LOD16 -> LOD32 progression with zero pop/hole gaps.
    out.lod4StartU  = out.activeGridRadiusU;
    out.lod8StartU  = std::max(out.lod4StartU + kCellWorldSizeU * 2.0f, rawL0 * std::min(mult, 1.5f));
    out.lod16StartU = std::max(out.lod8StartU * 1.45f, rawL1);
    out.lod32StartU = std::max(out.lod16StartU * 1.35f, rawMax);

    out.lod4BlendWidthU  = std::clamp(out.activeGridRadiusU * 0.18f, 1024.0f, 4096.0f);
    out.lod8BlendWidthU  = std::clamp((out.lod8StartU - out.lod4StartU) * 0.12f, 2048.0f, 8192.0f);
    out.lod16BlendWidthU = std::clamp((out.lod16StartU - out.lod8StartU) * 0.12f, 4096.0f, 16384.0f);
    // Skirt depth scales conservatively with LOD band to hide micro-cracks without piercing underwater caves.
    out.skirtClampDepthU = std::clamp(256.0f + (mult - 1.0f) * 192.0f, 192.0f, 640.0f);
    return out;
}

namespace {

bool IsReasonableWorldZ(float z) {
    return std::isfinite(z) && z > -65536.0f && z < 65536.0f;
}

bool IsWaterInheritSentinel(float w) {
    return std::isfinite(w) && w >= kCellWaterInheritSentinel * 0.99f;
}

uint64_t CellKey(int32_t x, int32_t y) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(y);
}

}  // namespace

TerrainBorderAudit AuditAndRepairCellGrid(std::vector<CellTerrainSample>& cells,
                                          float defaultLandHeight, float defaultWaterHeight,
                                          float seamToleranceU, bool allowRepair) {
    TerrainBorderAudit audit{};
    const float safeDefLand = IsReasonableWorldZ(defaultLandHeight) ? defaultLandHeight : -2048.0f;
    const float safeDefWater = IsReasonableWorldZ(defaultWaterHeight) ? defaultWaterHeight : -14000.0f;
    const float tol = std::max(1.0f, seamToleranceU);

    std::unordered_map<uint64_t, size_t> indexByCoord;
    indexByCoord.reserve(cells.size());

    for (size_t i = 0; i < cells.size(); ++i) {
        auto& c = cells[i];
        ++audit.cellsInspected;
        if (c.cellState == 7u) ++audit.attachedCells;
        if (c.hasLand && c.landLoaded) ++audit.landLoadedCells;
        indexByCoord[CellKey(c.cellX, c.cellY)] = i;

        const bool safeToTouch = allowRepair && c.cellState == 7u && c.spinLockCount == 0u;

        // 1. Validate LoadedLandData height bounds and NaN/Inf/sentinel corruption
        if (c.hasLand && c.landLoaded) {
            if (c.nanHeightVerts > 0u) audit.nanOrInfAnomalies += c.nanHeightVerts;

            const bool baseBad = !IsReasonableWorldZ(c.baseHeight);
            if (baseBad) {
                ++audit.nanOrInfAnomalies;
                if (safeToTouch) {
                    c.baseHeight = safeDefLand;
                    ++audit.anomaliesRepaired;
                }
            }

            const bool minBad = !IsReasonableWorldZ(c.minHeight);
            const bool maxBad = !IsReasonableWorldZ(c.maxHeight);
            if (minBad || maxBad) {
                ++audit.nanOrInfAnomalies;
                if (safeToTouch) {
                    if (minBad) c.minHeight = c.baseHeight - 512.0f;
                    if (maxBad) c.maxHeight = c.baseHeight + 512.0f;
                    ++audit.anomaliesRepaired;
                }
            } else if (c.minHeight > c.maxHeight) {
                ++audit.minMaxInversions;
                if (safeToTouch) {
                    std::swap(c.minHeight, c.maxHeight);
                    ++audit.anomaliesRepaired;
                }
            }
        }

        // 2. Validate exterior cell waterHeight (never overwrite the 2147483648.0f inheritance sentinel!)
        const bool isInterior = (c.cellFlags & 1u) != 0u;
        const bool hasWater = (c.cellFlags & 2u) != 0u;
        if (!isInterior && hasWater && !IsWaterInheritSentinel(c.waterHeight)) {
            if (!IsReasonableWorldZ(c.waterHeight)) {
                ++audit.waterAnomalies;
                if (safeToTouch) {
                    c.waterHeight = safeDefWater;
                    ++audit.anomaliesRepaired;
                }
            }
        }
    }

    // 3. Pairwise cell-border consistency check (East neighbor +X and North neighbor +Y to avoid double-counting)
    for (size_t i = 0; i < cells.size(); ++i) {
        const auto& a = cells[i];
        if ((a.cellFlags & 1u) != 0u || a.cellState != 7u) continue;

        auto checkPair = [&](int32_t nx, int32_t ny, int edgeA, int edgeB) {
            const auto it = indexByCoord.find(CellKey(nx, ny));
            if (it == indexByCoord.end()) return;
            const auto& b = cells[it->second];
            if ((b.cellFlags & 1u) != 0u || b.cellState != 7u) return;

            if (a.hasLand && a.landLoaded && b.hasLand && b.landLoaded &&
                a.edgeHeightValid[edgeA] && b.edgeHeightValid[edgeB]) {
                const float delta = std::fabs(a.edgeHeight[edgeA] - b.edgeHeight[edgeB]);
                if (std::isfinite(delta)) {
                    audit.maxBorderHeightDeltaU = std::max(audit.maxBorderHeightDeltaU, delta);
                    if (delta > tol) ++audit.heightSeamTears;
                }
            }

            if ((a.cellFlags & 2u) != 0u && (b.cellFlags & 2u) != 0u) {
                const float wa = IsWaterInheritSentinel(a.waterHeight) ? safeDefWater : a.waterHeight;
                const float wb = IsWaterInheritSentinel(b.waterHeight) ? safeDefWater : b.waterHeight;
                if (IsReasonableWorldZ(wa) && IsReasonableWorldZ(wb)) {
                    const float wDelta = std::fabs(wa - wb);
                    audit.maxBorderWaterDeltaU = std::max(audit.maxBorderWaterDeltaU, wDelta);
                    // Only flag sea-level mismatches (> 16u step when both cells are near sea level)
                    if (std::fabs(wa - safeDefWater) < 512.0f && std::fabs(wb - safeDefWater) < 512.0f && wDelta > 16.0f) {
                        ++audit.waterBorderSteps;
                    }
                }
            }
        };

        checkPair(a.cellX + 1, a.cellY, 2, 3);  // A East vs B West
        checkPair(a.cellX, a.cellY + 1, 0, 1);  // A North vs B South
    }

    return audit;
}

TfcTvdtPatchCheck VerifyTfcTvdtBranchSite(const uint8_t* code, size_t maxBytes,
                                          uint32_t branchOff, uint32_t bypassSetOff) {
    TfcTvdtPatchCheck out{};
    if (!code || branchOff < 11u || bypassSetOff <= branchOff + 2u) return out;
    if (static_cast<size_t>(bypassSetOff) + 7u > maxBytes) return out;
    const int32_t patchDelta = static_cast<int32_t>(bypassSetOff) - static_cast<int32_t>(branchOff + 2u);
    if (patchDelta <= 0 || patchDelta > 127) return out;

    ZydisDecoder dec;
    ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    ZydisDecodedInstruction insn{};
    ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT]{};

    // 1) [branchOff - 11]: mov rax, qword ptr [rcx + 0xD0] (loads PlayerCamera::cameraStates[3] == FreeCameraState)
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&dec, code + branchOff - 11u, maxBytes - (branchOff - 11u), &insn, ops))) {
        return out;
    }
    if (insn.length != 7u || insn.mnemonic != ZYDIS_MNEMONIC_MOV || insn.operand_count_visible < 2u) return out;
    if (ops[0].type != ZYDIS_OPERAND_TYPE_REGISTER || ops[0].reg.value != ZYDIS_REGISTER_RAX) return out;
    if (ops[1].type != ZYDIS_OPERAND_TYPE_MEMORY || ops[1].mem.base != ZYDIS_REGISTER_RCX ||
        ops[1].mem.index != ZYDIS_REGISTER_NONE || ops[1].size != 64u ||
        !ops[1].mem.disp.has_displacement || ops[1].mem.disp.value != 0xD0) {
        return out;
    }

    // 2) [branchOff - 4]: cmp qword ptr [rcx + 0x28], rax (compares PlayerCamera::currentState == freeState)
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&dec, code + branchOff - 4u, maxBytes - (branchOff - 4u), &insn, ops))) {
        return out;
    }
    if (insn.length != 4u || insn.mnemonic != ZYDIS_MNEMONIC_CMP || insn.operand_count_visible < 2u) return out;
    if (ops[0].type != ZYDIS_OPERAND_TYPE_MEMORY || ops[0].mem.base != ZYDIS_REGISTER_RCX ||
        ops[0].mem.index != ZYDIS_REGISTER_NONE || ops[0].size != 64u ||
        !ops[0].mem.disp.has_displacement || ops[0].mem.disp.value != 0x28) {
        return out;
    }
    if (ops[1].type != ZYDIS_OPERAND_TYPE_REGISTER || ops[1].reg.value != ZYDIS_REGISTER_RAX) return out;

    // 3) [branchOff]: je rel8 (74 XX) skipping past bypassSetOff (+7) instead of jumping into it
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&dec, code + branchOff, maxBytes - branchOff, &insn, ops))) {
        return out;
    }
    if (insn.length != 2u || insn.mnemonic != ZYDIS_MNEMONIC_JZ || code[branchOff] != 0x74u) return out;
    const uint8_t origRel8 = code[branchOff + 1u];
    const int32_t origTarget = static_cast<int32_t>(branchOff + 2u) + static_cast<int32_t>(static_cast<int8_t>(origRel8));
    if (origTarget != static_cast<int32_t>(bypassSetOff + 7u)) return out;

    // 4) [bypassSetOff]: mov byte ptr [rip + disp32], 1 (sets global:TerrainBypassTVDT = 1)
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&dec, code + bypassSetOff, maxBytes - bypassSetOff, &insn, ops))) {
        return out;
    }
    if (insn.length != 7u || insn.mnemonic != ZYDIS_MNEMONIC_MOV || insn.operand_count_visible < 2u) return out;
    if (ops[0].type != ZYDIS_OPERAND_TYPE_MEMORY || ops[0].mem.base != ZYDIS_REGISTER_RIP || ops[0].size != 8u) return out;
    if (ops[1].type != ZYDIS_OPERAND_TYPE_IMMEDIATE || ops[1].imm.value.u != 1ull) return out;

    out.valid = true;
    out.origRel8 = origRel8;
    out.patchedRel8 = static_cast<uint8_t>(patchDelta);
    out.origTargetOff = static_cast<uint32_t>(origTarget);
    out.patchTargetOff = bypassSetOff;
    return out;
}

TfcLodRefreshDecision EvaluateTfcLodRefreshPolicy(bool isFreeCam, bool isExterior, bool inMenu,
                                                  const float camPos[3], TfcLodRefreshState& state,
                                                  float travelThresholdU, uint32_t minFrameInterval) {
    TfcLodRefreshDecision dec{};
    const bool posOk = camPos != nullptr &&
                       std::isfinite(camPos[0]) && std::isfinite(camPos[1]) && std::isfinite(camPos[2]);
    const float thresh = std::max(512.0f, travelThresholdU);
    const uint32_t minGap = std::max(1u, minFrameInterval);

    if (!isExterior || inMenu) {
        state.wasFreeCam = isFreeCam;
        return dec;
    }

    if (isFreeCam) {
        ++state.framesInTfc;
        ++state.framesSinceRefresh;
        if (!state.wasFreeCam) {
            // Immediate full-tree refresh on tfc entry: instantly un-culls every LOD4/8 chunk that was TVDT-culled!
            dec.forceFullUpdate = true;
            dec.isEntry = true;
            ++state.entryRefreshes;
            state.framesSinceRefresh = 0u;
            if (posOk) {
                state.lastRefreshPos[0] = camPos[0];
                state.lastRefreshPos[1] = camPos[1];
                state.lastRefreshPos[2] = camPos[2];
                state.hasAnchor = true;
            }
        } else if (posOk && state.framesSinceRefresh >= minGap) {
            if (!state.hasAnchor) {
                state.lastRefreshPos[0] = camPos[0];
                state.lastRefreshPos[1] = camPos[1];
                state.lastRefreshPos[2] = camPos[2];
                state.hasAnchor = true;
            } else {
                const float dx = camPos[0] - state.lastRefreshPos[0];
                const float dy = camPos[1] - state.lastRefreshPos[1];
                const float dz = camPos[2] - state.lastRefreshPos[2];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (dist >= thresh) {
                    dec.forceFullUpdate = true;
                    dec.isTravel = true;
                    ++state.travelRefreshes;
                    state.framesSinceRefresh = 0u;
                    state.lastRefreshPos[0] = camPos[0];
                    state.lastRefreshPos[1] = camPos[1];
                    state.lastRefreshPos[2] = camPos[2];
                }
            }
        }
    } else if (state.wasFreeCam) {
        // Immediate single full-tree refresh on tfc exit to restore normal player-cell LOD state cleanly.
        dec.forceFullUpdate = true;
        dec.isExit = true;
        ++state.exitRefreshes;
        state.framesSinceRefresh = 0u;
        state.hasAnchor = false;
    }

    state.wasFreeCam = isFreeCam;
    return dec;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

const uint8_t* g_base = nullptr;
std::mutex g_mu;
RawTerrainLodTelemetry g_tel{};
FloatingOriginState g_origin{};
uint32_t g_presentTick = 0;

// Binary-verified RVAs & offsets loaded from version table at TerrainLodInit:
uint32_t kTesSingleton = 0;
uint32_t kOffTesGridCells = 0x78u;
uint32_t kOffCellFlags = 0x40u;
uint32_t kOffCellState = 0x44u;
uint32_t kOffCellExtData = 0x68u;
uint32_t kOffCellLand = 0x70u;
uint32_t kOffCellWater = 0x78u;
uint32_t kOffCellSpinLock = 0x120u;
uint32_t kOffCellWorldSpace = 0x128u;
uint32_t kOffLandDataFlags = 0x28u;
uint32_t kOffLandLoadedData = 0x40u;
uint32_t kOffLoadedHeights = 0x20u;
uint32_t kOffLoadedMinH = 0x4888u;
uint32_t kOffLoadedMaxH = 0x488Cu;
uint32_t kOffLoadedCellX = 0x49B8u;
uint32_t kOffLoadedCellY = 0x49BCu;
uint32_t kOffLoadedBaseH = 0x49C0u;
uint32_t kOffWsDefLand = 0x210u;
uint32_t kOffWsDefWater = 0x214u;
uint32_t kOffWsParent = 0x158u;
uint32_t kOffWsParentFlags = 0xA2u;
uint32_t kGlobLodDist0Sq = 0;
uint32_t kGlobLodDist1Sq = 0;
uint32_t kGlobLodMaxSq = 0;
uint32_t kGlobTreeDistSq = 0;
uint32_t kGlobSplitMult = 0;
uint32_t kGlobLockedLod = 0;
uint32_t kGlobCachedGrids = 0;
uint32_t kGlobLandPoolFree = 0;
uint32_t kGlobLandPoolActive = 0;

// Wave 38 binary-verified tfc LOD4 void-guard RVAs & state:
uint32_t kPlayerCameraSingleton = 0;
uint32_t kUiSingleton = 0;
uint32_t kFnTerrainMgrUpdate = 0;
uint32_t kSiteTfcTvdtBranch = 0;
uint32_t kSiteTfcTvdtBypassSet = 0;
uint32_t kGlobBypassTvdt = 0;
uint32_t kOffTesInteriorCell = 0xC0u;
uint32_t kOffCamCurrentState = 0x28u;
uint32_t kOffCamFreeState = 0xD0u;

using TerrainMgrUpdateFn = uint64_t (*)(void* mgr, const float* pos, uint32_t* flags);
TerrainMgrUpdateFn g_origTerrainMgrUpdate = nullptr;
bool g_tfcTvdtPatchActive = false;
bool g_tfcUpdateDetourActive = false;
std::string g_initStatus;
TfcLodRefreshState g_tfcRefreshState{};
std::atomic<uint32_t> g_tfcFullRefreshCount{0};
std::atomic<uint8_t> g_tfcLiveActive{0};
std::atomic<uint8_t> g_tfcBypassLive{0};

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

bool VerifyTfcBranchSeh(const uint8_t* base, uint32_t branchRva, uint32_t bypassSetRva, TfcTvdtPatchCheck* out) {
    __try {
        if (!base || branchRva < 16u || bypassSetRva <= branchRva + 2u) return false;
        const uint32_t winStart = branchRva - 16u;
        const uint32_t winEnd = bypassSetRva + 16u;
        const size_t winLen = static_cast<size_t>(winEnd - winStart);
        volatile uint8_t p0 = base[winStart];
        volatile uint8_t p1 = base[winEnd - 1u];
        (void)p0; (void)p1;
        *out = VerifyTfcTvdtBranchSite(base + winStart, winLen, branchRva - winStart, bypassSetRva - winStart);
        return out->valid;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool VerifyUpdatePrologueSeh(const uint8_t* base, uint32_t fnRva) {
    // Verified SkyrimSE 1.7.104 BGSTerrainManager::Update (0x5160C0) exact 14-byte clean prologue:
    //   push rbx; push rbp; push rdi; sub rsp, 0x50; cmp byte ptr [rcx], 0; mov rbx, r8
    static constexpr uint8_t kExpected[14] = {
        0x40, 0x53, 0x55, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x80, 0x39, 0x00, 0x49, 0x8B, 0xD8
    };
    __try {
        if (!base || !fnRva) return false;
        return std::memcmp(base + fnRva, kExpected, sizeof kExpected) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void ProbeTfcHookContextSeh(const float* pos, bool* outFreeCam, bool* outExterior, bool* outMenu, float outPos[3]) {
    *outFreeCam = false;
    *outExterior = false;
    *outMenu = false;
    outPos[0] = outPos[1] = outPos[2] = 0.0f;
    __try {
        if (!g_base) return;
        if (kTesSingleton) {
            const uint8_t* tes = ReadAt<const uint8_t*>(g_base, kTesSingleton);
            if (ValidHeapPtr(tes)) {
                const uint8_t* interiorCell = ReadAt<const uint8_t*>(tes, kOffTesInteriorCell);
                *outExterior = (interiorCell == nullptr);
            }
        }
        if (kPlayerCameraSingleton) {
            const uint8_t* pcam = ReadAt<const uint8_t*>(g_base, kPlayerCameraSingleton);
            if (ValidHeapPtr(pcam)) {
                const uint8_t* curState = ReadAt<const uint8_t*>(pcam, kOffCamCurrentState);
                const uint8_t* freeState = ReadAt<const uint8_t*>(pcam, kOffCamFreeState);
                *outFreeCam = (curState != nullptr && freeState != nullptr && curState == freeState);
            }
        }
        if (kUiSingleton) {
            const uint8_t* ui = ReadAt<const uint8_t*>(g_base, kUiSingleton);
            if (ValidHeapPtr(ui)) {
                const uint32_t numPauses = ReadAt<uint32_t>(ui, 0x160u);
                *outMenu = (numPauses > 0u && numPauses < 64u);
            }
        }
        if (pos) {
            outPos[0] = pos[0];
            outPos[1] = pos[1];
            outPos[2] = pos[2];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outFreeCam = false;
        *outExterior = false;
    }
}

uint8_t ReadBypassTvdtByteSeh() {
    __try {
        if (!g_base || !kGlobBypassTvdt) return 0u;
        return g_base[kGlobBypassTvdt];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0u;
    }
}

uint64_t HookTerrainManagerUpdate(void* mgr, const float* pos, uint32_t* flags) {
    if (!g_origTerrainMgrUpdate) return 0ull;
    bool isFreeCam = false, isExterior = false, inMenu = false;
    float camPos[3] = {0.0f, 0.0f, 0.0f};
    ProbeTfcHookContextSeh(pos, &isFreeCam, &isExterior, &inMenu, camPos);
    g_tfcLiveActive.store(isFreeCam ? 1u : 0u, std::memory_order_relaxed);

    const TfcLodRefreshDecision dec = EvaluateTfcLodRefreshPolicy(
        isFreeCam, isExterior, inMenu, pos ? camPos : nullptr, g_tfcRefreshState, 4096.0f, 8u);

    uint64_t ret = 0ull;
    if (dec.forceFullUpdate && flags != nullptr && ((*flags) & 1u) == 0u) {
        uint32_t boostedFlags = (*flags) | 1u;
        g_tfcFullRefreshCount.fetch_add(1u, std::memory_order_relaxed);
        ret = g_origTerrainMgrUpdate(mgr, pos, &boostedFlags);
    } else {
        ret = g_origTerrainMgrUpdate(mgr, pos, flags);
    }

    g_tfcBypassLive.store(ReadBypassTvdtByteSeh(), std::memory_order_relaxed);
    return ret;
}

float ResolveWorldDefaultFloat(const uint8_t* ws, uint32_t fieldOff, float fallback) {
    for (int depth = 0; depth < 4 && ValidHeapPtr(ws); ++depth) {
        const uint8_t* parent = ReadAt<const uint8_t*>(ws, kOffWsParent);
        const uint16_t pflags = ReadAt<uint16_t>(ws, kOffWsParentFlags);
        if (ValidHeapPtr(parent) && (pflags & 1u) != 0u) {
            ws = parent;
            continue;
        }
        const float v = ReadAt<float>(ws, fieldOff);
        return IsReasonableWorldZ(v) ? v : fallback;
    }
    return fallback;
}

struct LiveTerrainScan {
    bool ok = false;
    uint32_t gridLen = 0;
    int32_t centerX = 0, centerY = 0;
    uint32_t cellsLoaded = 0;
    uint32_t poolFree = 0, poolActive = 0;
    uint32_t repairedLive = 0;
    float defLandH = -2048.0f;
    float defWaterH = -14000.0f;
    LodTransitionInput lodIn{};
    std::vector<CellTerrainSample> samples;
};

void ProbeLiveGridSeh(LiveTerrainScan* out, bool allowLiveRepair) {
    __try {
        if (!g_base || !kTesSingleton) return;
        const uint8_t* tes = ReadAt<const uint8_t*>(g_base, kTesSingleton);
        if (!ValidHeapPtr(tes)) return;
        const uint8_t* arr = ReadAt<const uint8_t*>(tes, kOffTesGridCells);
        if (!ValidHeapPtr(arr)) return;

        out->centerX = ReadAt<int32_t>(arr, 0x08u);
        out->centerY = ReadAt<int32_t>(arr, 0x0Cu);
        out->gridLen = ReadAt<uint32_t>(arr, 0x10u);
        const uint8_t* const* cellsPtr = ReadAt<const uint8_t* const*>(arr, 0x18u);
        if (out->gridLen < 3u || out->gridLen > 15u || !ValidHeapPtr(cellsPtr)) return;

        // Read BGSTerrainManager cached LOD globals (binary-verified 0x3203570..0x3203590)
        out->lodIn.uGridsToLoad = out->gridLen;
        if (kGlobLodDist0Sq) {
            const float sq = ReadAt<float>(g_base, kGlobLodDist0Sq);
            if (std::isfinite(sq) && sq > 0.0f) out->lodIn.blockLevel0Dist = std::sqrt(sq);
        }
        if (kGlobLodDist1Sq) {
            const float sq = ReadAt<float>(g_base, kGlobLodDist1Sq);
            if (std::isfinite(sq) && sq > 0.0f) out->lodIn.blockLevel1Dist = std::sqrt(sq);
        }
        if (kGlobLodMaxSq) {
            const float sq = ReadAt<float>(g_base, kGlobLodMaxSq);
            if (std::isfinite(sq) && sq > 0.0f) out->lodIn.blockMaxDist = std::sqrt(sq);
        }
        if (kGlobTreeDistSq) {
            const float sq = ReadAt<float>(g_base, kGlobTreeDistSq);
            if (std::isfinite(sq) && sq > 0.0f) out->lodIn.treeLoadDist = std::sqrt(sq);
        }
        if (kGlobSplitMult) {
            const float m = ReadAt<float>(g_base, kGlobSplitMult);
            if (std::isfinite(m) && m > 0.0f) out->lodIn.splitDistMult = m;
        }
        if (kGlobLockedLod) out->lodIn.lockedTerrainLod = ReadAt<uint32_t>(g_base, kGlobLockedLod);
        if (kGlobCachedGrids) out->lodIn.cachedGridsToLoad = ReadAt<uint32_t>(g_base, kGlobCachedGrids);
        if (kGlobLandPoolFree) out->poolFree = ReadAt<uint32_t>(g_base, kGlobLandPoolFree);
        if (kGlobLandPoolActive) out->poolActive = ReadAt<uint32_t>(g_base, kGlobLandPoolActive);

        const uint32_t total = out->gridLen * out->gridLen;
        out->samples.reserve(total);
        bool gotWsDefaults = false;

        for (uint32_t i = 0; i < total; ++i) {
            const uint8_t* cell = cellsPtr[i];
            if (!ValidHeapPtr(cell)) continue;
            if (cell[0x1A] != 60u) continue;  // TESObjectCELL formType == 60 (0x3C)
            ++out->cellsLoaded;

            CellTerrainSample s{};
            s.formId = ReadAt<uint32_t>(cell, 0x14u);
            s.cellFlags = ReadAt<uint16_t>(cell, kOffCellFlags);
            s.cellState = ReadAt<uint8_t>(cell, kOffCellState);
            s.spinLockCount = ReadAt<uint32_t>(cell, kOffCellSpinLock + 4u);
            s.waterHeight = ReadAt<float>(cell, kOffCellWater);

            if ((s.cellFlags & 1u) == 0u) {
                const uint8_t* extData = ReadAt<const uint8_t*>(cell, kOffCellExtData);
                if (ValidHeapPtr(extData)) {
                    s.cellX = ReadAt<int32_t>(extData, 0x00u);
                    s.cellY = ReadAt<int32_t>(extData, 0x04u);
                }
                if (!gotWsDefaults) {
                    const uint8_t* ws = ReadAt<const uint8_t*>(cell, kOffCellWorldSpace);
                    if (ValidHeapPtr(ws) && ws[0x1A] == 71u) {
                        out->defLandH = ResolveWorldDefaultFloat(ws, kOffWsDefLand, -2048.0f);
                        out->defWaterH = ResolveWorldDefaultFloat(ws, kOffWsDefWater, -14000.0f);
                        gotWsDefaults = true;
                    }
                }

                // Inspect TESObjectLAND (+0x70) ONLY when cell is attached (state==7) and unlocked (spinLock==0)
                if (s.cellState == 7u && s.spinLockCount == 0u) {
                    const uint8_t* land = ReadAt<const uint8_t*>(cell, kOffCellLand);
                    if (ValidHeapPtr(land) && land[0x1A] == 72u) {  // TESObjectLAND formType == 72 (0x48)
                        s.hasLand = true;
                        uint8_t* ld = const_cast<uint8_t*>(ReadAt<const uint8_t*>(land, kOffLandLoadedData));
                        const uint32_t lflags = ReadAt<uint32_t>(land, kOffLandDataFlags);
                        if (ValidHeapPtr(ld) && (lflags & 1u) != 0u) {
                            s.landLoaded = true;
                            s.minHeight = ReadAt<float>(ld, kOffLoadedMinH);
                            s.maxHeight = ReadAt<float>(ld, kOffLoadedMaxH);
                            s.baseHeight = ReadAt<float>(ld, kOffLoadedBaseH);
                            const int32_t lx = ReadAt<int32_t>(ld, kOffLoadedCellX);
                            const int32_t ly = ReadAt<int32_t>(ld, kOffLoadedCellY);
                            if (lx != 0 || ly != 0) { s.cellX = lx; s.cellY = ly; }

                            // Sample border vertices across the 4 quads (17x17 = 289 floats each, base +0x20):
                            // Quad 0=SW, 1=SE, 2=NW, 3=NE; compute mean North/South/East/West border height & detect NaNs.
                            const float* hBase = reinterpret_cast<const float*>(ld + kOffLoadedHeights);
                            double edgeSum[4] = {};
                            uint32_t edgeCnt[4] = {};
                            for (int q = 0; q < 4; ++q) {
                                const float* qh = hBase + q * 289;
                                for (int v = 0; v < 289; ++v) {
                                    const float hv = qh[v];
                                    if (!std::isfinite(hv)) {
                                        ++s.nanHeightVerts;
                                        if (allowLiveRepair && MemoryGuardAllowLiveStructWrites()) {
                                            float zero = 0.0f;
                                            std::memcpy(const_cast<float*>(qh + v), &zero, sizeof(float));
                                            ++out->repairedLive;
                                        }
                                        continue;
                                    }
                                    const int row = v / 17;
                                    const int col = v % 17;
                                    const float worldZ = hv + (IsReasonableWorldZ(s.baseHeight) ? s.baseHeight : out->defLandH);
                                    if ((q == 2 || q == 3) && row == 16) { edgeSum[0] += worldZ; ++edgeCnt[0]; }  // North
                                    if ((q == 0 || q == 1) && row == 0)  { edgeSum[1] += worldZ; ++edgeCnt[1]; }  // South
                                    if ((q == 1 || q == 3) && col == 16) { edgeSum[2] += worldZ; ++edgeCnt[2]; }  // East
                                    if ((q == 0 || q == 2) && col == 0)  { edgeSum[3] += worldZ; ++edgeCnt[3]; }  // West
                                }
                            }
                            for (int e = 0; e < 4; ++e) {
                                if (edgeCnt[e] > 0u) {
                                    s.edgeHeight[e] = static_cast<float>(edgeSum[e] / static_cast<double>(edgeCnt[e]));
                                    s.edgeHeightValid[e] = true;
                                }
                            }
                            // Repair non-finite or inverted runtime minHeight/maxHeight in LoadedLandData (transient, save-safe)
                            if (allowLiveRepair && MemoryGuardAllowLiveStructWrites() &&
                                IsReasonableWorldZ(s.minHeight) && IsReasonableWorldZ(s.maxHeight) &&
                                s.minHeight > s.maxHeight) {
                                float fixedMin = s.maxHeight, fixedMax = s.minHeight;
                                std::memcpy(ld + kOffLoadedMinH, &fixedMin, sizeof(float));
                                std::memcpy(ld + kOffLoadedMaxH, &fixedMax, sizeof(float));
                                s.minHeight = fixedMin;
                                s.maxHeight = fixedMax;
                                ++out->repairedLive;
                            }
                        }
                    }
                }
            }
            out->samples.push_back(s);
        }
        out->ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out->ok = false;
    }
}

}  // namespace

void TerrainLodInit(const Image& img) {
    g_base = img.base;
    kTesSingleton = Addr("TES::Singleton");
    if (uint32_t v = Off("off:TES::gridCells"))           kOffTesGridCells = v;
    if (uint32_t v = Off("off:TESObjectCELL::cellFlags")) kOffCellFlags = v;
    if (uint32_t v = Off("off:TESObjectCELL::cellState")) kOffCellState = v;
    if (uint32_t v = Off("off:TESObjectCELL::exteriorData")) kOffCellExtData = v;
    if (uint32_t v = Off("off:TESObjectCELL::cellLand"))  kOffCellLand = v;
    if (uint32_t v = Off("off:TESObjectCELL::waterHeight")) kOffCellWater = v;
    if (uint32_t v = Off("off:TESObjectCELL::spinLock"))  kOffCellSpinLock = v;
    if (uint32_t v = Off("off:TESObjectCELL::worldSpace")) kOffCellWorldSpace = v;
    if (uint32_t v = Off("off:TESObjectLAND::dataFlags")) kOffLandDataFlags = v;
    if (uint32_t v = Off("off:TESObjectLAND::loadedData")) kOffLandLoadedData = v;
    if (uint32_t v = Off("off:LoadedLandData::heights"))  kOffLoadedHeights = v;
    if (uint32_t v = Off("off:LoadedLandData::minHeight")) kOffLoadedMinH = v;
    if (uint32_t v = Off("off:LoadedLandData::maxHeight")) kOffLoadedMaxH = v;
    if (uint32_t v = Off("off:LoadedLandData::cellX"))    kOffLoadedCellX = v;
    if (uint32_t v = Off("off:LoadedLandData::cellY"))    kOffLoadedCellY = v;
    if (uint32_t v = Off("off:LoadedLandData::baseHeight")) kOffLoadedBaseH = v;
    if (uint32_t v = Off("off:TESWorldSpace::defaultLandHeight"))  kOffWsDefLand = v;
    if (uint32_t v = Off("off:TESWorldSpace::defaultWaterHeight")) kOffWsDefWater = v;
    if (uint32_t v = Off("off:TESWorldSpace::parentWorld"))        kOffWsParent = v;
    if (uint32_t v = Off("off:TESWorldSpace::parentUseFlags"))     kOffWsParentFlags = v;
    kGlobLodDist0Sq     = Addr("global:TerrainLodDist0Sq");
    kGlobLodDist1Sq     = Addr("global:TerrainLodDist1Sq");
    kGlobLodMaxSq       = Addr("global:TerrainLodMaxDistSq");
    kGlobTreeDistSq     = Addr("global:TerrainTreeLoadDistSq");
    kGlobSplitMult      = Addr("global:TerrainSplitDistMult");
    kGlobLockedLod      = Addr("global:TerrainLockedLod");
    kGlobCachedGrids    = Addr("global:TerrainCachedGridsToLoad");
    kGlobLandPoolFree   = Addr("global:LoadedLandPoolCount");
    kGlobLandPoolActive = Addr("global:LoadedLandPoolActive");

    kPlayerCameraSingleton = Addr("PlayerCamera::Singleton");
    kUiSingleton           = Addr("UI::Singleton");
    kFnTerrainMgrUpdate    = Addr("fn:BGSTerrainManager::Update");
    kSiteTfcTvdtBranch     = Addr("site:TerrainManager::TfcTvdtBranch");
    kSiteTfcTvdtBypassSet  = Addr("site:TerrainManager::TfcTvdtBypassSet");
    kGlobBypassTvdt        = Addr("global:TerrainBypassTVDT");
    if (uint32_t v = Off("off:TES::interiorCell"))        kOffTesInteriorCell = v;
    if (uint32_t v = Off("off:PlayerCamera::currentState")) kOffCamCurrentState = v;
    if (uint32_t v = Off("off:PlayerCamera::freeState"))  kOffCamFreeState = v;

    g_tfcTvdtPatchActive = false;
    g_tfcUpdateDetourActive = false;
    g_origTerrainMgrUpdate = nullptr;
    g_tfcRefreshState = {};
    g_tfcFullRefreshCount.store(0u, std::memory_order_relaxed);
    g_tfcLiveActive.store(0u, std::memory_order_relaxed);
    g_tfcBypassLive.store(0u, std::memory_order_relaxed);

    // Wave 38: Fail-closed binary-verified tfc LOD4 void-guard installation:
    //   1) Redirects 0x516201 (je 0x51624D -> je 0x516246) so FreeCameraState sets global:TerrainBypassTVDT=1
    //      instead of skipping it and culling LOD4 terrain via PlayerCharacter->parentCell's TVDT bitmask.
    //   2) Installs Zydis-checked detour on BGSTerrainManager::Update (0x5160C0) to promote incremental
    //      1-node/frame updates to immediate full-quadtree refresh (flags |= 1) on tfc entry, exit, and travel.
    std::string branchDiag = "disabled(fail-closed)";
    std::string detourDiag = "disabled(fail-closed)";
    if (AreBinaryOffsetsVerified() && MemoryGuardAllowLiveStructWrites() && img.base) {
        if (kSiteTfcTvdtBranch && kSiteTfcTvdtBypassSet &&
            img.InText(kSiteTfcTvdtBranch) && img.InText(kSiteTfcTvdtBypassSet)) {
            TfcTvdtPatchCheck chk{};
            if (VerifyTfcBranchSeh(img.base, kSiteTfcTvdtBranch, kSiteTfcTvdtBypassSet, &chk)) {
                const uint8_t patchBytes[2] = {0x74u, chk.patchedRel8};
                uint8_t* patchAddr = const_cast<uint8_t*>(img.base) + kSiteTfcTvdtBranch;
                if (PatchWrite(patchAddr, patchBytes, sizeof patchBytes)) {
                    g_tfcTvdtPatchActive = true;
                    char b[96];
                    std::snprintf(b, sizeof b, "0x%X(74 %02X->74 %02X)",
                                  kSiteTfcTvdtBranch, chk.origRel8, chk.patchedRel8);
                    branchDiag = b;
                } else {
                    branchDiag = "refused(PatchWrite failed)";
                }
            } else {
                branchDiag = "refused(Zydis/byte mismatch - fail-closed)";
            }
        } else {
            branchDiag = "skipped(site absent in version table)";
        }

        if (kFnTerrainMgrUpdate && img.InText(kFnTerrainMgrUpdate)) {
            if (VerifyUpdatePrologueSeh(img.base, kFnTerrainMgrUpdate)) {
                const std::string res = InstallDetour(
                    img, kFnTerrainMgrUpdate,
                    reinterpret_cast<void*>(&HookTerrainManagerUpdate),
                    reinterpret_cast<void**>(&g_origTerrainMgrUpdate));
                if (res.rfind("ok", 0) == 0 && g_origTerrainMgrUpdate != nullptr) {
                    g_tfcUpdateDetourActive = true;
                }
                detourDiag = res;
            } else {
                detourDiag = "refused(prologue bytes mismatch - fail-closed)";
            }
        } else {
            detourDiag = "skipped(fn absent in version table)";
        }
    }

    {
        char msg[320];
        std::snprintf(msg, sizeof msg,
                      "[TerrainLOD] tfc LOD4 void-guard %s: tvdt_branch_patch=%s update_detour=%s",
                      (g_tfcTvdtPatchActive || g_tfcUpdateDetourActive) ? "active" : "inactive",
                      branchDiag.c_str(), detourDiag.c_str());
        g_initStatus = msg;
    }

    std::lock_guard<std::mutex> lk(g_mu);
    g_tel = {};
    g_origin = {};
    g_tel.writesEnabled = AreBinaryOffsetsVerified() ? 1u : 0u;
    g_tel.tfcTvdtPatchActive = g_tfcTvdtPatchActive ? 1u : 0u;
    g_tel.tfcUpdateDetourActive = g_tfcUpdateDetourActive ? 1u : 0u;
}

std::string TerrainLodInitStatus() {
    return g_initStatus;
}

void TerrainLodOnPresent() {
    if (!g_base) return;
    // Update floating origin every frame from EngineView; run full 5x5 cell-grid height/LOD scan every 60 frames.
    EngineView ev{};
    RAW_EngineView(&ev);

    FloatingOriginState locOrigin;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (ev.ok & 2u) UpdateFloatingOrigin(g_origin, ev.camPos);
        locOrigin = g_origin;
    }

    const uint32_t tick = ++g_presentTick;
    if ((tick % 60u) != 1u) {
        std::lock_guard<std::mutex> lk(g_mu);
        std::memcpy(g_tel.originWorld, locOrigin.originWorld, sizeof(g_tel.originWorld));
        std::memcpy(g_tel.localCamPos, locOrigin.localCamPos, sizeof(g_tel.localCamPos));
        g_tel.rawWorldUlpU = locOrigin.rawWorldUlpU;
        g_tel.stabilizedUlpU = locOrigin.stabilizedUlpU;
        g_tel.precisionGain = locOrigin.precisionGain;
        g_tel.tfcActive = g_tfcLiveActive.load(std::memory_order_relaxed);
        g_tel.bypassTvdtLive = g_tfcBypassLive.load(std::memory_order_relaxed);
        g_tel.tfcFullRefreshes = g_tfcFullRefreshCount.load(std::memory_order_relaxed);
        return;
    }

    LiveTerrainScan scan{};
    const bool writesAllowed = AreBinaryOffsetsVerified() && MemoryGuardAllowLiveStructWrites();
    ProbeLiveGridSeh(&scan, writesAllowed);

    RawTerrainLodTelemetry next{};
    next.tickCount = tick;
    next.writesEnabled = writesAllowed ? 1u : 0u;
    next.tfcTvdtPatchActive = g_tfcTvdtPatchActive ? 1u : 0u;
    next.tfcUpdateDetourActive = g_tfcUpdateDetourActive ? 1u : 0u;
    next.tfcActive = g_tfcLiveActive.load(std::memory_order_relaxed);
    next.bypassTvdtLive = g_tfcBypassLive.load(std::memory_order_relaxed);
    next.tfcFullRefreshes = g_tfcFullRefreshCount.load(std::memory_order_relaxed);
    std::memcpy(next.originWorld, locOrigin.originWorld, sizeof(next.originWorld));
    std::memcpy(next.localCamPos, locOrigin.localCamPos, sizeof(next.localCamPos));
    next.rawWorldUlpU = locOrigin.rawWorldUlpU;
    next.stabilizedUlpU = locOrigin.stabilizedUlpU;
    next.precisionGain = locOrigin.precisionGain;

    if (scan.ok) {
        const LodTransitionBands bands = ComputeSeamlessLodBands(scan.lodIn);
        const TerrainBorderAudit audit = AuditAndRepairCellGrid(scan.samples, scan.defLandH, scan.defWaterH, 64.0f, false);
        next.gridLength = scan.gridLen;
        next.gridCenterX = scan.centerX;
        next.gridCenterY = scan.centerY;
        next.cellsLoaded = scan.cellsLoaded;
        next.attachedCells = audit.attachedCells;
        next.landLoadedCells = audit.landLoadedCells;
        next.landPoolFree = scan.poolFree;
        next.landPoolActive = scan.poolActive;
        next.anomaliesDetected = audit.nanOrInfAnomalies + audit.minMaxInversions + audit.waterAnomalies;
        next.anomaliesRepaired = scan.repairedLive;
        next.heightSeamTears = audit.heightSeamTears;
        next.waterBorderSteps = audit.waterBorderSteps;
        next.defaultLandHeight = scan.defLandH;
        next.defaultWaterHeight = scan.defWaterH;
        next.maxBorderHeightDeltaU = audit.maxBorderHeightDeltaU;
        next.maxBorderWaterDeltaU = audit.maxBorderWaterDeltaU;
        next.lodBandStarts[0] = bands.lod4StartU;
        next.lodBandStarts[1] = bands.lod8StartU;
        next.lodBandStarts[2] = bands.lod16StartU;
        next.lodBandStarts[3] = bands.lod32StartU;
        next.hasBandAnomaly = (bands.hasBandGap || bands.hasBandInversion) ? 1u : 0u;
        next.gridsCacheDesync = bands.gridsCacheDesync ? 1u : 0u;
        next.landPoolOverflowRisk = bands.landPoolOverflowRisk ? 1u : 0u;
        if (scan.repairedLive > 0u) {
            ForensicsRecord("terrain_lod_repair", scan.repairedLive, next.anomaliesDetected);
        }
    }

    std::lock_guard<std::mutex> lk(g_mu);
    g_tel = next;
}

void TerrainLodShutdown() {
    g_tfcTvdtPatchActive = false;
    g_tfcUpdateDetourActive = false;
    g_origTerrainMgrUpdate = nullptr;
    g_base = nullptr;
}

std::string TerrainLodStatsJson() {
    RawTerrainLodTelemetry t{};
    {
        std::lock_guard<std::mutex> lk(g_mu);
        t = g_tel;
    }
    char buf[1280];
    std::snprintf(buf, sizeof buf,
                  "{\"cmd\":\"terrain_lod\",\"grid_length\":%u,\"grid_center\":[%d,%d],\"cells_loaded\":%u,"
                  "\"attached_cells\":%u,\"land_loaded\":%u,\"land_pool\":[%u,%u,37],"
                  "\"default_land_z\":%.1f,\"default_water_z\":%.1f,"
                  "\"anomalies\":%u,\"repaired\":%u,\"height_seam_tears\":%u,\"water_border_steps\":%u,"
                  "\"max_height_delta_u\":%.2f,\"max_water_delta_u\":%.2f,"
                  "\"origin_world\":[%.0f,%.0f,%.0f],\"local_cam\":[%.2f,%.2f,%.2f],"
                  "\"raw_ulp_u\":%.6f,\"stabilized_ulp_u\":%.6f,\"precision_gain\":%.1f,"
                  "\"lod_bands\":[%.0f,%.0f,%.0f,%.0f],\"band_anomaly\":%s,\"grids_desync\":%s,"
                  "\"pool_overflow_risk\":%s,\"writes_enabled\":%s,"
                  "\"tfc_tvdt_patch\":%s,\"tfc_update_detour\":%s,\"tfc_active\":%s,"
                  "\"bypass_tvdt_live\":%u,\"tfc_full_refreshes\":%u}",
                  t.gridLength, t.gridCenterX, t.gridCenterY, t.cellsLoaded,
                  t.attachedCells, t.landLoadedCells, t.landPoolFree, t.landPoolActive,
                  t.defaultLandHeight, t.defaultWaterHeight,
                  t.anomaliesDetected, t.anomaliesRepaired, t.heightSeamTears, t.waterBorderSteps,
                  t.maxBorderHeightDeltaU, t.maxBorderWaterDeltaU,
                  t.originWorld[0], t.originWorld[1], t.originWorld[2],
                  t.localCamPos[0], t.localCamPos[1], t.localCamPos[2],
                  t.rawWorldUlpU, t.stabilizedUlpU, t.precisionGain,
                  t.lodBandStarts[0], t.lodBandStarts[1], t.lodBandStarts[2], t.lodBandStarts[3],
                  t.hasBandAnomaly ? "true" : "false", t.gridsCacheDesync ? "true" : "false",
                  t.landPoolOverflowRisk ? "true" : "false", t.writesEnabled ? "true" : "false",
                  t.tfcTvdtPatchActive ? "true" : "false", t.tfcUpdateDetourActive ? "true" : "false",
                  t.tfcActive ? "true" : "false", t.bypassTvdtLive, t.tfcFullRefreshes);
    return buf;
}

#else
void TerrainLodInit(const Image&) {}
std::string TerrainLodInitStatus() { return "[TerrainLOD] pure_test"; }
void TerrainLodOnPresent() {}
void TerrainLodShutdown() {}
std::string TerrainLodStatsJson() { return "{\"cmd\":\"terrain_lod\",\"mode\":\"pure_test\"}"; }
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetTerrainLodTelemetry(raw::rt::RawTerrainLodTelemetry* out) {
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
