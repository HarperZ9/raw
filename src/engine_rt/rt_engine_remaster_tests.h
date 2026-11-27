#pragma once
// rt_engine_remaster_tests.h: Self-contained known-answer + planted-failure + live SkyrimSE.exe 1.7.104
// binary verification test suite for RAW's Creation Engine remaster modules:
//   - rt_zydis_offsets.h/.cpp (fail-closed startup binary offset verification)
//   - rt_terrain_lod.h/.cpp   (floating-origin stabilizer, LOD4/8/16/32 bands, cell-border height/water audit)
//   - rt_depth_topology.h/.cpp (D24S8 vs Reversed-Z precision, clip-plane guard, Z-up right-handed topology)
//   - rt_memory_guards.h/.cpp (cell spinlock/state/flag/sentinel/pointer/grid-race/shader-flag/ScrapHeap guards)
//
// Wire into tests/ce_runtime_check/main.cpp with 2 lines:
//   #include "../../src/engine_rt/rt_engine_remaster_tests.h"
//   raw::rt::RunEngineRemasterTests(Check);
// Original work, MIT.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

#include "rt_depth_topology.h"
#include "rt_memory_guards.h"
#include "rt_terrain_lod.h"
#include "rt_zydis_offsets.h"

namespace raw::rt {

template <typename CheckFn>
inline void RunEngineRemasterTests(CheckFn&& Check) {
    // 1. Zydis Fail-Closed Binary Offset Verifier (synthetic machine code + planted mismatch)
    {
        // Build a synthetic image buffer at RVA 0x1000..0x2000 containing two real x64 functions:
        //   0x1000: movss xmm0, [rcx + 0x214]; ret  (F3 0F 10 81 14 02 00 00 C3)
        //   0x1040: movzx edx, word ptr [rcx + 0x40]; movss xmm1, [rcx + 0x78]; mov rcx, [rcx + 0x128]; ret
        std::vector<uint8_t> fakeImg(0x2000, 0xCC);
        const uint8_t fn1[] = {0xF3, 0x0F, 0x10, 0x81, 0x14, 0x02, 0x00, 0x00, 0xC3};
        const uint8_t fn2[] = {
            0x0F, 0xB7, 0x51, 0x40,                   // movzx edx, word ptr [rcx + 0x40]
            0xF3, 0x0F, 0x10, 0x49, 0x78,             // movss xmm1, dword ptr [rcx + 0x78]
            0x48, 0x8B, 0x89, 0x28, 0x01, 0x00, 0x00, // mov rcx, qword ptr [rcx + 0x128]
            0xC3
        };
        constexpr size_t kSyntheticFn1Offset = 0x1000u;
        constexpr size_t kSyntheticFn2Offset = 0x1040u;
        static_assert(kSyntheticFn2Offset > kSyntheticFn1Offset, "Synthetic Zydis test function layout guard");
        std::memcpy(fakeImg.data() + kSyntheticFn1Offset, fn1, sizeof fn1);
        std::memcpy(fakeImg.data() + kSyntheticFn2Offset, fn2, sizeof fn2);

        Image img{};
        img.base = fakeImg.data();
        img.sizeOfImage = static_cast<uint32_t>(fakeImg.size());
        img.textBegin = 0x1000;
        img.textEnd = 0x2000;

        const OffsetVerifyRule goodRules[] = {
            {"ws_water",   0x1000, 0x214, 4, true},
            {"cell_flags", 0x1040, 0x40,  2, false},
            {"cell_water", 0x1040, 0x78,  4, true},
            {"cell_ws",    0x1040, 0x128, 8, false},
        };
        const auto vGood = VerifyCriticalOffsets(img, goodRules, 4);
        Check(vGood.checked == 4 && vGood.passed == 4 && vGood.failed == 0 && !vGood.failClosed,
              "zydis_startup_verify_synthetic_pass");

        // Planted failure: wrong offset 0x70 instead of 0x78 MUST trip failClosed!
        const OffsetVerifyRule badRules[] = {
            {"ws_water",   0x1000, 0x214, 4, true},
            {"cell_wrong", 0x1040, 0x70,  4, true},
        };
        const auto vBad = VerifyCriticalOffsets(img, badRules, 2);
        Check(vBad.checked == 2 && vBad.passed == 1 && vBad.failed == 1 && vBad.failClosed,
              "zydis_startup_verify_planted_mismatch_fail_closed");
    }

    // 2. Real SkyrimSE.exe 1.7.104.0 PE Binary Zydis Verification (runs when live binary is present)
    {
        const std::string exePath = "H:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition\\SkyrimSE.exe";
        if (std::filesystem::exists(exePath)) {
            std::ifstream f(exePath, std::ios::binary);
            std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (raw.size() > 0x1000 && raw[0] == 'M' && raw[1] == 'Z') {
                uint32_t lfanew = 0;
                std::memcpy(&lfanew, raw.data() + 0x3C, 4);
                if (lfanew + 0x108 < raw.size()) {
                    uint16_t numSec = 0, optSize = 0;
                    uint32_t ts = 0, sizeOfImage = 0;
                    std::memcpy(&numSec, raw.data() + lfanew + 6, 2);
                    std::memcpy(&ts, raw.data() + lfanew + 8, 4);
                    std::memcpy(&optSize, raw.data() + lfanew + 20, 2);
                    std::memcpy(&sizeOfImage, raw.data() + lfanew + 24 + 56, 4);
                    // Only run 1.7.104.0 specific RVA verification when exact PE signature matches
                    if (ts == 0x6A8C7046u && sizeOfImage == 0x3929000u) {
                        std::vector<uint8_t> mapped(sizeOfImage, 0);
                        uint32_t textBegin = 0, textEnd = 0;
                        const size_t secBase = lfanew + 24u + optSize;
                        for (uint16_t i = 0; i < numSec; ++i) {
                            const size_t sh = secBase + size_t(i) * 40u;
                            if (sh + 40u > raw.size()) break;
                            char name[9]{};
                            std::memcpy(name, raw.data() + sh, 8);
                            uint32_t vsz = 0, va = 0, rsz = 0, roff = 0;
                            std::memcpy(&vsz, raw.data() + sh + 8, 4);
                            std::memcpy(&va, raw.data() + sh + 12, 4);
                            std::memcpy(&rsz, raw.data() + sh + 16, 4);
                            std::memcpy(&roff, raw.data() + sh + 20, 4);
                            if (std::strcmp(name, ".text") == 0 && textBegin == 0) {
                                textBegin = va;
                                textEnd = va + vsz;
                            }
                            if (roff < raw.size() && va < mapped.size()) {
                                const size_t cp = std::min<size_t>({rsz, raw.size() - roff, mapped.size() - va});
                                std::memcpy(mapped.data() + va, raw.data() + roff, cp);
                            }
                        }
                        Image liveImg{};
                        liveImg.base = mapped.data();
                        liveImg.sizeOfImage = sizeOfImage;
                        liveImg.textBegin = textBegin;
                        liveImg.textEnd = textEnd;

                        const OffsetVerifyRule liveRules[] = {
                            {"TESObjectCELL::cellFlags",          0x2BEE00u, 0x40u,   2, false},
                            {"TESObjectCELL::waterHeight",        0x2BEE00u, 0x78u,   4, true},
                            {"TESObjectCELL::worldSpace",         0x2BEE00u, 0x128u,  8, false},
                            {"TESObjectCELL::exteriorData",       0x2BC620u, 0x68u,   8, false},
                            {"TESWorldSpace::parentWorld",        0x30DE30u, 0x158u,  8, false},
                            {"TESWorldSpace::parentUseFlags",     0x30DE30u, 0xA2u,   1, false},
                            {"TESWorldSpace::defaultLandHeight",  0x30DE30u, 0x210u,  4, true},
                            {"TESWorldSpace::defaultWaterHeight", 0x30DE80u, 0x214u,  4, true},
                            {"TESObjectLAND::loadedData",         0x2AD160u, 0x40u,   8, false},
                            {"LoadedLandData::cellX",             0x2AD160u, 0x49B8u, 4, false},
                            {"LoadedLandData::cellY",             0x2AD1C0u, 0x49BCu, 4, false},
                            {"LoadedLandData::heights",           0x2B3870u, 0x20u,   4, true},
                            {"LoadedLandData::baseHeight",        0x2B3870u, 0x49C0u, 4, true},
                            {"LoadedLandData::minHeight",         0x2B0850u, 0x4888u, 4, true},
                            {"LoadedLandData::maxHeight",         0x2B0850u, 0x488Cu, 4, true},
                            {"GridCellArray::length",             0x226C60u, 0x10u,   4, false},
                            {"NiCamera::frustumNear",             0xEF1310u, 0x160u,  4, true},
                            {"NiCamera::frustumFar",              0xEF1310u, 0x164u,  4, true},
                            {"ScrapHeap::baseAddress",            0xCDF4C0u, 0x48u,   8, false},
                            {"ScrapHeap::endAddress",             0xCDF4C0u, 0x50u,   8, false},
                            {"TES::interiorCell",                 0x5160C0u, 0xC0u,   8, false},
                            {"PlayerCamera::freeState",           0x5160C0u, 0xD0u,   8, false},
                            {"PlayerCamera::currentState",        0x5160C0u, 0x28u,   8, false},
                            {"BGSTerrainChunk::landMesh",         0x5117B0u, 0x08u,   8, false},
                            {"BGSTerrainChunk::waterMesh",        0x5117B0u, 0x10u,   8, false},
                        };
                        const auto res = VerifyCriticalOffsets(liveImg, liveRules, sizeof(liveRules) / sizeof(liveRules[0]));
                        std::printf("INFO real SkyrimSE.exe 1.7.104 Zydis verify: passed=%u/%u failClosed=%d\n",
                                    res.passed, res.checked, int(res.failClosed));
                        Check(res.checked == 25 && res.passed == 25 && !res.failClosed,
                              "real_skyrim17104_zydis_25_offsets_verified");

                        // Verify Wave 38 tfc LOD4 TVDT branch site (0x516201 -> 0x516246) & BGSTerrainManager::Update prologue
                        const uint32_t winStart = 0x5161F0u;
                        const uint32_t winLen   = 0x80u;
                        const auto tfcChk = VerifyTfcTvdtBranchSite(
                            mapped.data() + winStart, winLen, 0x516201u - winStart, 0x516246u - winStart);
                        static constexpr uint8_t kExpectedUpdatePrologue[14] = {
                            0x40, 0x53, 0x55, 0x57, 0x48, 0x83, 0xEC, 0x50,
                            0x80, 0x39, 0x00, 0x49, 0x8B, 0xD8
                        };
                        const bool prologueOk = std::memcmp(mapped.data() + 0x5160C0u, kExpectedUpdatePrologue, 14) == 0;
                        std::printf("INFO real SkyrimSE.exe 1.7.104 tfc TVDT site: valid=%d orig=74 %02X patch=74 %02X prologue=%d\n",
                                    int(tfcChk.valid), tfcChk.origRel8, tfcChk.patchedRel8, int(prologueOk));
                        Check(tfcChk.valid && tfcChk.origRel8 == 0x4Au && tfcChk.patchedRel8 == 0x43u &&
                              prologueOk,
                              "real_skyrim17104_tfc_tvdt_branch_and_prologue_verified");

                        // Planted mismatch: corrupting the je displacement MUST fail closed!
                        std::vector<uint8_t> tampered(mapped.data() + winStart, mapped.data() + winStart + winLen);
                        tampered[0x516202u - winStart] = 0x48u;
                        const auto tfcBad = VerifyTfcTvdtBranchSite(
                            tampered.data(), tampered.size(), 0x516201u - winStart, 0x516246u - winStart);
                        Check(!tfcBad.valid, "real_skyrim17104_tfc_tvdt_planted_corrupt_fail_closed");

                        // Planted mismatch against real SkyrimSE.exe: claiming waterHeight is +0x70 MUST fail closed!
                        const OffsetVerifyRule plantedLive[] = {
                            {"TESObjectCELL::waterHeight_planted_wrong", 0x2BEE00u, 0x70u, 4, true},
                        };
                        const auto badLive = VerifyCriticalOffsets(liveImg, plantedLive, 1);
                        Check(badLive.failClosed && badLive.failed == 1,
                              "real_skyrim17104_zydis_planted_wrong_offset_fail_closed");
                    }
                }
            }
        }
    }

    // 3. Floating-Origin Precision Stabilizer at Winterhold (107126, 132705, 8900)
    {
        FloatingOriginState fo{};
        const float winterhold[3] = {107126.0f, 132705.0f, 8900.0f};
        UpdateFloatingOrigin(fo, winterhold, 2048.0f);
        Check(fo.initialized && fo.originCellX == 26 && fo.originCellY == 32 &&
              std::fabs(fo.localCamPos[0]) < 2048.0f && std::fabs(fo.localCamPos[1]) < 2048.0f &&
              fo.rawWorldUlpU >= 0.015f && fo.stabilizedUlpU <= 0.0005f && fo.precisionGain >= 32.0f,
              "terrain_floating_origin_winterhold_precision");

        // Hysteresis: small step does not recenter; >2048u step recenters cleanly to next cell boundary
        const float nearStep[3] = {107250.0f, 132800.0f, 8920.0f};
        UpdateFloatingOrigin(fo, nearStep, 2048.0f);
        const uint32_t rc1 = fo.recenterCount;
        const float farStep[3] = {112000.0f, 136000.0f, 8920.0f};
        UpdateFloatingOrigin(fo, farStep, 2048.0f);
        Check(rc1 == 0u && fo.recenterCount == 1u && fo.originCellX == 27 && fo.originCellY == 33,
              "terrain_floating_origin_cell_snap_hysteresis");
    }

    // 4. LOD4/8/16/32 Seamless Transition Band Solver & 37-Slot Pool Cap Detector
    {
        LodTransitionInput okIn{};
        okIn.uGridsToLoad = 5;
        okIn.cachedGridsToLoad = 5;
        okIn.blockLevel0Dist = 75000.0f;
        okIn.blockLevel1Dist = 140000.0f;
        okIn.blockMaxDist = 250000.0f;
        okIn.splitDistMult = 1.75f;
        const auto okBands = ComputeSeamlessLodBands(okIn);
        Check(!okBands.hasBandGap && !okBands.hasBandInversion && !okBands.gridsCacheDesync &&
              !okBands.landPoolOverflowRisk &&
              okBands.lod4StartU < okBands.lod8StartU &&
              okBands.lod8StartU < okBands.lod16StartU &&
              okBands.lod16StartU < okBands.lod32StartU,
              "terrain_lod_bands_monotonic_healthy");

        // Planted anomalies: uGridsToLoad=7 (>37 pool cap!), inverted distances, band gap, cache desync
        LodTransitionInput badIn{};
        badIn.uGridsToLoad = 7;
        badIn.cachedGridsToLoad = 5;
        badIn.blockLevel0Dist = 8000.0f;   // < 7*0.5*4096 = 14336 -> gap!
        badIn.blockLevel1Dist = 6000.0f;   // < level0 -> inversion!
        badIn.blockMaxDist = 5000.0f;
        const auto badBands = ComputeSeamlessLodBands(badIn);
        Check(badBands.landPoolOverflowRisk && badBands.hasBandGap && badBands.hasBandInversion &&
              badBands.gridsCacheDesync &&
              badBands.lod4StartU < badBands.lod8StartU &&
              badBands.lod8StartU < badBands.lod16StartU &&
              badBands.lod16StartU < badBands.lod32StartU,
              "terrain_lod_bands_planted_anomalies_detected_and_clamped");
    }

    // 5. Cell-Border Height, Skirt & Water Consistency Auditor + Save-Safe Repairer
    {
        std::vector<CellTerrainSample> grid(4);
        // (0,0): NaN baseHeight (must repair), East edge = 100u, sea water = -14000u
        grid[0] = {0x1001u, 0, 0, kCellFlagHasWater, kCellStateAttached, 0u, true, true,
                   -100.0f, 300.0f, std::numeric_limits<float>::quiet_NaN(), -14000.0f,
                   {100.0f, 100.0f, 100.0f, 100.0f}, {true, true, true, true}, 0u};
        // (1,0): minHeight > maxHeight inversion (must swap-repair), West edge = 250u (150u tear vs (0,0)!)
        grid[1] = {0x1002u, 1, 0, kCellFlagHasWater, kCellStateAttached, 0u, true, true,
                   450.0f, 120.0f, 0.0f, -14000.0f,
                   {250.0f, 250.0f, 250.0f, 250.0f}, {true, true, true, true}, 0u};
        // (0,1): water step -13920u vs -14000u (80u sea-level discontinuity!), locked spinLock=1 with NaN base (must NOT repair!)
        grid[2] = {0x1003u, 0, 1, kCellFlagHasWater, kCellStateAttached, 1u, true, true,
                   -50.0f, 200.0f, std::numeric_limits<float>::quiet_NaN(), -13920.0f,
                   {100.0f, 100.0f, 100.0f, 100.0f}, {true, true, true, true}, 0u};
        // (1,1): valid worldspace water inherit sentinel 2147483648.0f (must be preserved untouched!)
        grid[3] = {0x1004u, 1, 1, kCellFlagHasWater, kCellStateAttached, 0u, true, true,
                   -200.0f, 200.0f, 0.0f, kCellWaterInheritSentinel,
                   {250.0f, 250.0f, 250.0f, 250.0f}, {true, true, true, true}, 0u};

        const auto audit = AuditAndRepairCellGrid(grid, -2048.0f, -14000.0f, 64.0f, true);
        Check(audit.cellsInspected == 4 && audit.attachedCells == 4 && audit.landLoadedCells == 4 &&
              audit.nanOrInfAnomalies == 2 && audit.minMaxInversions == 1 &&
              audit.anomaliesRepaired == 2 &&   // (0,0) baseHeight + (1,0) min/max swap; (0,1) skipped due to spinLock!
              audit.heightSeamTears >= 1 && audit.waterBorderSteps == 2 &&  // (0,0)-(0,1) and (0,1)-(1,1) both step 80u!
              std::isfinite(grid[0].baseHeight) && grid[1].minHeight <= grid[1].maxHeight &&
              !std::isfinite(grid[2].baseHeight) &&   // locked cell stayed untouched!
              grid[3].waterHeight == kCellWaterInheritSentinel,
              "terrain_cell_border_audit_repair_and_lock_safety");
    }

    // 6. Depth Precision, Reversed-Z Projection & X/Y/Z Axis Topology Validator
    {
        const auto dpVanilla = AnalyzeDepthPrecision(5.0f, 350000.0f);
        const auto dpRemast  = AnalyzeDepthPrecision(20.0f, 350000.0f);
        Check(dpVanilla.zFightingRisk && dpVanilla.horizonCollapseRisk &&
              dpVanilla.d24StepAt32768U > dpRemast.d24StepAt32768U * 3.5f &&
              dpVanilla.horizonGainRatio > 500.0f &&
              dpRemast.revZ32StepAt250000U < 0.05f,
              "depth_d24_vs_reversed_z_precision_proof");

        float proj[16] = {};
        const bool pOk = BuildReversedZInfiniteProj(65.0f, 16.0f / 9.0f, 15.0f, proj);
        // Project view point (0, 0, -15) -> clipZ = proj[10]*(-15) + proj[11]*1 = 15, clipW = proj[14]*(-15) = 15 -> ndcZ = 1.0
        const float ndcNear = (proj[10] * -15.0f + proj[11]) / (proj[14] * -15.0f);
        const float ndcFar  = (proj[10] * -100000.0f + proj[11]) / (proj[14] * -100000.0f);
        const bool pBad = BuildReversedZInfiniteProj(200.0f, 1.77f, 15.0f, proj);  // planted invalid FOV
        Check(pOk && std::fabs(ndcNear - 1.0f) < 1e-5f && ndcFar > 0.0001f && ndcFar < 0.0002f && !pBad,
              "depth_reversed_z_infinite_proj_matrix");

        const float eyePos[3] = {107126.0f, 132705.0f, 8900.0f};
        const float idRot[9]  = {1, 0, 0,  0, 1, 0,  0, 0, 1};
        CameraFrustumState goodFrust{-0.84f, 0.84f, 0.4725f, -0.4725f, 15.0f, 350000.0f, true};
        const auto topoOk = ValidateCameraAndAxisTopology(eyePos, idRot, goodFrust, 80.0f);
        Check(topoOk.posFinite && topoOk.rotOrthonormal && topoOk.rightHanded &&
              topoOk.frustumConsistent && topoOk.extremeCoordWarning,
              "topology_right_handed_camera_and_winterhold_flag");

        // Planted failure: mirrored left-handed rotation (det = -1) + inverted frustum (near > far)
        const float mirrorRot[9] = {-1, 0, 0,  0, 1, 0,  0, 0, 1};
        CameraFrustumState badFrust{0.5f, -0.5f, 0.4f, -0.4f, 500.0f, 100.0f, true};
        const auto topoBad = ValidateCameraAndAxisTopology(eyePos, mirrorRot, badFrust, 80.0f);
        Check(!topoBad.rightHanded && !topoBad.frustumConsistent,
              "topology_planted_mirror_and_inverted_frustum_reject");

        const auto clip1p = ComputeOptimalClipPlanes(false, 0u, 8000.0f, 5.0f, 350000.0f);
        const auto clip3p = ComputeOptimalClipPlanes(false, 9u, 8000.0f, 5.0f, 350000.0f);
        const auto clipInt = ComputeOptimalClipPlanes(true, 0u, 200.0f, 5.0f, 65536.0f);
        Check(clip1p.clampedForFirstPerson && clip1p.recommendedNearU == 15.0f &&
              clip3p.recommendedNearU > 20.0f && clip3p.recommendedNearU <= 24.0f &&
              clipInt.clampedForInterior && clipInt.recommendedNearU == 12.0f,
              "depth_adaptive_near_clip_stabilizer");
    }

    // 7. Memory, Allocator, Race & Hidden-Flag Invariant Guards
    {
        CellWriteRequest goodReq{};
        goodReq.cellFlags = kCellFlagHasWater;
        goodReq.proposedCellFlags = kCellFlagHasWater;
        goodReq.cellState = kCellStateAttached;
        goodReq.spinLockCount = 0;
        goodReq.writeOffset = 0x78u;
        goodReq.currentWaterHeight = -14000.0f;
        goodReq.proposedWaterHeight = -13997.5f;
        goodReq.binaryOffsetsVerified = true;
        Check(ValidateCellFlagWrite(goodReq).allowed, "mem_guard_valid_cell_water_write_allow");

        auto failReq = goodReq; failReq.binaryOffsetsVerified = false;
        const auto rFailClosed = ValidateCellFlagWrite(failReq);
        auto raceReq = goodReq; raceReq.cellState = 5u;  // Detaching
        const auto rRace = ValidateCellFlagWrite(raceReq);
        auto lockReq = goodReq; lockReq.spinLockCount = 1u;
        const auto rLock = ValidateCellFlagWrite(lockReq);
        auto flipReq = goodReq; flipReq.proposedCellFlags |= kCellFlagIsInterior;
        const auto rFlip = ValidateCellFlagWrite(flipReq);
        auto ptrReq  = goodReq; ptrReq.writeOffset = 0x70u;  // TESObjectLAND*!
        const auto rPtr  = ValidateCellFlagWrite(ptrReq);
        auto sentReq = goodReq; sentReq.currentWaterHeight = kCellWaterInheritSentinel;
        const auto rSent = ValidateCellFlagWrite(sentReq);
        Check(!rFailClosed.allowed && rFailClosed.reason == GuardRejectReason::OffsetsNotVerified &&
              !rRace.allowed       && rRace.reason       == GuardRejectReason::CellNotAttached &&
              !rLock.allowed       && rLock.reason       == GuardRejectReason::CellSpinLockHeld &&
              !rFlip.allowed       && rFlip.reason       == GuardRejectReason::InteriorFlagFlipForbidden &&
              !rPtr.allowed        && rPtr.reason        == GuardRejectReason::ProtectedPointerOffset &&
              !rSent.allowed       && rSent.reason       == GuardRejectReason::WaterInheritSentinelProtected,
              "mem_guard_planted_cell_flag_and_race_rejects");

        // GridCellArray resize transition & 37-slot pool cap guard
        const auto gOk   = ValidateGridResizeTransition({5, 5, 0, 3, 5, 10, 20});
        const auto gEven = ValidateGridResizeTransition({5, 5, 0, 4, 5, 10, 20});
        const auto gBusy = ValidateGridResizeTransition({5, 5, 25, 3, 5, 10, 20});
        const auto gPool = ValidateGridResizeTransition({5, 7, 0, 7, 5, 10, 20});
        const auto gDesy = ValidateGridResizeTransition({5, 5, 0, 5, 7, 10, 20});
        Check(gOk.allowed &&
              !gEven.allowed && gEven.reason == GuardRejectReason::GridLengthInvalid &&
              !gBusy.allowed && gBusy.reason == GuardRejectReason::GridNotEmpty &&
              !gPool.allowed && gPool.reason == GuardRejectReason::LandPoolOverflow &&
              !gDesy.allowed && gDesy.reason == GuardRejectReason::GridCacheDesync,
              "mem_guard_grid_resize_and_pool_cap_checks");

        // BSShaderProperty 64-bit hidden flag & ScrapHeap bounds audits
        const auto shOk  = AuditShaderPropertyFlags(kShaderFlag1ZBufferTest | kShaderFlag2ZBufferWrite, true, true);
        const auto shBad = AuditShaderPropertyFlags(kShaderFlag2ZBufferWrite, false, true);
        const auto scOk  = AnalyzeScrapHeapBounds(0x000001A000000000ull, 0x000001A004000000ull, 128u, 0x000001A000100000ull);
        const auto scBad = AnalyzeScrapHeapBounds(0x000001A004000000ull, 0x000001A000000000ull, 128u, 0x000001A000100000ull);
        Check(shOk.valid && !shBad.valid && shBad.zWriteWithoutZTest && shBad.nullMaterialWithPasses &&
              scOk.valid && scOk.ptrContained && !scBad.valid,
              "mem_guard_shader_flags_and_scrapheap_bounds");
    }

    // 8. Wave 38 tfc Free-Camera LOD4 Void-Guard: Synthetic Zydis Site Verifier + Refresh Policy State Machine
    {
        // Build exact synthetic 0x5161F6..0x51624D machine code buffer (offset 0 = 0x5161F6, branch=11, bypassSet=80):
        //   +0x00: 48 8B 81 D0 00 00 00    mov rax, [rcx + 0xD0]
        //   +0x07: 48 39 41 28             cmp [rcx + 0x28], rax
        //   +0x0B: 74 4A                   je  +0x57 (0x0D + 0x4A = 0x57 = bypassSet + 7)
        //   +0x50: C6 05 F4 D0 CE 02 01    mov byte ptr [rip + 0x2CED0F4], 1
        std::vector<uint8_t> synth(0x60, 0x90);
        const uint8_t head[] = {0x48, 0x8B, 0x81, 0xD0, 0x00, 0x00, 0x00, 0x48, 0x39, 0x41, 0x28, 0x74, 0x4A};
        const uint8_t set1[] = {0xC6, 0x05, 0xF4, 0xD0, 0xCE, 0x02, 0x01};
        std::memcpy(synth.data(), head, sizeof head);
        std::memcpy(synth.data() + 0x50, set1, sizeof set1);
        const auto okSite = VerifyTfcTvdtBranchSite(synth.data(), synth.size(), 0x0Bu, 0x50u);
        Check(okSite.valid && okSite.origRel8 == 0x4Au && okSite.patchedRel8 == 0x43u &&
              okSite.origTargetOff == 0x57u && okSite.patchTargetOff == 0x50u,
              "tfc_tvdt_branch_zydis_synthetic_pass");

        // Planted failure: wrong freeState displacement (0xC8 instead of 0xD0) MUST reject fail-closed!
        auto badDisp = synth;
        badDisp[3] = 0xC8u;
        // Planted failure: bypassSet immediate 0 instead of 1 MUST reject fail-closed!
        auto badImm = synth;
        badImm[0x56] = 0x00u;
        Check(!VerifyTfcTvdtBranchSite(badDisp.data(), badDisp.size(), 0x0Bu, 0x50u).valid &&
              !VerifyTfcTvdtBranchSite(badImm.data(), badImm.size(), 0x0Bu, 0x50u).valid,
              "tfc_tvdt_branch_zydis_planted_rejects");

        // Refresh policy state machine: entry, stationary no-op, travel throttle, menu/interior gate, exit, NaN safety
        TfcLodRefreshState st{};
        const float p0[3] = {10000.0f, 20000.0f, 500.0f};
        const float pNear[3] = {11000.0f, 20500.0f, 600.0f};
        const float pFar[3] = {16000.0f, 20000.0f, 800.0f};
        const float pNan[3] = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};

        const auto dNormal = EvaluateTfcLodRefreshPolicy(false, true, false, p0, st, 4096.0f, 8u);
        const auto dEnter  = EvaluateTfcLodRefreshPolicy(true,  true, false, p0, st, 4096.0f, 8u);
        const auto dNear   = EvaluateTfcLodRefreshPolicy(true,  true, false, pNear, st, 4096.0f, 8u);
        const auto dEarly  = EvaluateTfcLodRefreshPolicy(true,  true, false, pFar, st, 4096.0f, 8u);  // only 2 frames since entry (<8)
        for (int i = 0; i < 6; ++i) EvaluateTfcLodRefreshPolicy(true, true, false, pNear, st, 4096.0f, 8u);
        const auto dTravel = EvaluateTfcLodRefreshPolicy(true,  true, false, pFar, st, 4096.0f, 8u);  // >=8 frames & >4096u -> refresh!
        const auto dMenu   = EvaluateTfcLodRefreshPolicy(true,  true, true,  p0, st, 4096.0f, 8u);    // paused menu -> never force
        const auto dInt    = EvaluateTfcLodRefreshPolicy(true,  false, false, p0, st, 4096.0f, 8u);   // interior -> never force
        const auto dNan    = EvaluateTfcLodRefreshPolicy(true,  true, false, pNan, st, 4096.0f, 8u);  // NaN pos -> safe no-op
        const auto dExit   = EvaluateTfcLodRefreshPolicy(false, true, false, p0, st, 4096.0f, 8u);    // exit -> restore refresh

        Check(!dNormal.forceFullUpdate &&
              dEnter.forceFullUpdate && dEnter.isEntry &&
              !dNear.forceFullUpdate && !dEarly.forceFullUpdate &&
              dTravel.forceFullUpdate && dTravel.isTravel &&
              !dMenu.forceFullUpdate && !dInt.forceFullUpdate && !dNan.forceFullUpdate &&
              dExit.forceFullUpdate && dExit.isExit &&
              st.entryRefreshes == 1u && st.travelRefreshes == 1u && st.exitRefreshes == 1u,
              "tfc_lod_refresh_policy_state_machine");
    }
}

}  // namespace raw::rt
