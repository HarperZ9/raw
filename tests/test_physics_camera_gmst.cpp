// test_physics_camera_gmst.cpp: Wave 51 verification suite for Binary-Verified 1.7.104.0 Havok Ragdoll
// Impulse, Shield Block, Combat Regen, Bash & Mounted Camera GMSTs (rt_physics_camera_gmst.h/.cpp) verified
// directly against the real SkyrimSE.exe 1.7.104.0 PE image!
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_PHYSICS_CAMERA_GMST_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_image.h"
#include "../src/engine_rt/rt_physics_camera_gmst.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include <windows.h>

namespace raw::rt {

int RunPhysicsCameraGmstTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s physics_camera_gmst::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. Table completeness, category coverage & dead 0-reader legacy GMST hygiene:
    {
        size_t n = 0;
        const auto* tbl = PhysicsCameraGmstTable(&n);
        size_t liveCount = 0, deadCount = 0;
        bool hasRagdoll = false, hasPhysics = false, hasBlock = false, hasBash = false, hasRegen = false, hasCam = false;
        bool deadBlockMaxSkipped = false, deadArrowMassSkipped = false, deadHpRegenSkipped = false;
        bool valid = (tbl != nullptr && n >= 20u);
        for (size_t i = 0; i < n; ++i) {
            if (!tbl[i].name || !tbl[i].category || tbl[i].expectedRva == 0u ||
                !std::isfinite(tbl[i].exeDefault) || !std::isfinite(tbl[i].balancedValue)) {
                valid = false;
            }
            if (tbl[i].hasLiveReader) {
                ++liveCount;
                if (std::strcmp(tbl[i].category, "Ragdoll") == 0) hasRagdoll = true;
                if (std::strcmp(tbl[i].category, "Physics") == 0) hasPhysics = true;
                if (std::strcmp(tbl[i].category, "Block") == 0)   hasBlock = true;
                if (std::strcmp(tbl[i].category, "Bash") == 0)    hasBash = true;
                if (std::strcmp(tbl[i].category, "Regen") == 0)   hasRegen = true;
                if (std::strcmp(tbl[i].category, "Camera") == 0)  hasCam = true;
            } else {
                ++deadCount;
                if (std::strcmp(tbl[i].name, "fBlockMax") == 0)                  deadBlockMaxSkipped = true;
                if (std::strcmp(tbl[i].name, "fArrowFakeMass") == 0)             deadArrowMassSkipped = true;
                if (std::strcmp(tbl[i].name, "fCombatHealthRegenRateMult") == 0) deadHpRegenSkipped = true;
            }
        }
        check(valid && liveCount == 20u && deadCount == 6u &&
              hasRagdoll && hasPhysics && hasBlock && hasBash && hasRegen && hasCam &&
              deadBlockMaxSkipped && deadArrowMassSkipped && deadHpRegenSkipped,
              "table_completeness_and_dead_legacy_gmst_hygiene");
    }

    // 2. Ragdoll momentum bounding (arrow << warhammer, strictly capped) + monotonic Shield > Weapon > BareHand block + C ABI:
    {
        RagdollImpulseInput arrow{};
        arrow.projectileMassKg = 0.04f;
        arrow.impactSpeedMps   = 65.0f;
        arrow.damageAmount     = 45.0f;
        arrow.isRanged         = true;

        RagdollImpulseInput warhammer{};
        warhammer.projectileMassKg = 3.5f;
        warhammer.impactSpeedMps   = 14.0f;
        warhammer.damageAmount     = 120.0f;
        warhammer.isRanged         = false;

        RagdollImpulseInput giantClubExtreme{};
        giantClubExtreme.projectileMassKg = 45.0f;
        giantClubExtreme.impactSpeedMps   = 35.0f;
        giantClubExtreme.damageAmount     = 10000.0f;
        giantClubExtreme.isRanged         = false;

        const auto rArrow   = EvaluateRagdollDeathImpulse(arrow);
        const auto rHammer  = EvaluateRagdollDeathImpulse(warhammer);
        const auto rExtreme = EvaluateRagdollDeathImpulse(giantClubExtreme);

        const bool ragdollOk = (rArrow.cappedImpulseN > 1.0f && rArrow.cappedImpulseN < 12.0f) &&
                               (rHammer.cappedImpulseN > rArrow.cappedImpulseN * 3.0f) &&
                               (rHammer.cappedImpulseN <= 48.0f) &&
                               (rExtreme.cappedImpulseN <= 48.0f && rExtreme.wasClamped) &&
                               (rExtreme.launchVelocityMps < 1.0f) &&
                               std::isfinite(rExtreme.cappedImpulseN);

        BlockMitigationInput bBare{BlockGearType::BareHand, 50.0f, 0.0f, false, false, 0.85f};
        BlockMitigationInput bWeap{BlockGearType::Weapon,   50.0f, 0.0f, false, false, 0.85f};
        BlockMitigationInput bShld{BlockGearType::Shield,   50.0f, 36.0f, false, false, 0.85f};
        BlockMitigationInput bParry{BlockGearType::Shield,  80.0f, 45.0f, true,  false, 0.85f};
        BlockMitigationInput bPowAtk{BlockGearType::Shield, 50.0f, 36.0f, false, true,  0.85f};

        const auto mBare   = EvaluateBlockMitigation(bBare);
        const auto mWeap   = EvaluateBlockMitigation(bWeap);
        const auto mShld   = EvaluateBlockMitigation(bShld);
        const auto mParry  = EvaluateBlockMitigation(bParry);
        const auto mPowAtk = EvaluateBlockMitigation(bPowAtk);

        const bool blockOk = (mShld.mitigationFrac > mWeap.mitigationFrac) &&
                             (mWeap.mitigationFrac > mBare.mitigationFrac) &&
                             (mParry.mitigationFrac > mShld.mitigationFrac) &&
                             (mParry.mitigationFrac <= 0.85f + 1e-5f) &&
                             (mParry.staminaCostMult < mShld.staminaCostMult) &&
                             (mPowAtk.mitigationFrac < mShld.mitigationFrac);

        RawPhysicsCameraState st{};
        const int rcOk   = RAW_GetPhysicsCameraState(&st);
        const int rcNull = RAW_GetPhysicsCameraState(nullptr);
        const bool abiOk = (rcOk == 1) && (rcNull == 0) &&
                           (std::fabs(st.meleeDeathForceCap - 48.0f) < 1e-4f) &&
                           (std::fabs(st.rangedDeathForceCap - 22.0f) < 1e-4f) &&
                           (std::fabs(st.mountedMaxLookDownDeg - 58.0f) < 1e-4f);
        check(ragdollOk && blockOk && abiOk, "ragdoll_momentum_block_mitigation_and_c_abi");
    }

    // 3. Real SkyrimSE.exe 1.7.104.0 PE verification of ALL 26 Physics/Camera GMST RVAs, names & defaults + planted control!
    {
        std::ifstream f(R"(H:\SteamLibrary\steamapps\common\Skyrim Special Edition\SkyrimSE.exe)", std::ios::binary);
        if (f) {
            std::vector<uint8_t> pe((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(pe.data());
            const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(pe.data() + dos->e_lfanew);
            std::vector<uint8_t> mapped(nt->OptionalHeader.SizeOfImage, uint8_t{0});
            std::memcpy(mapped.data(), pe.data(), std::min<size_t>(pe.size(), nt->OptionalHeader.SizeOfHeaders));
            const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
            Image img{};
            img.base        = mapped.data();
            img.sizeOfImage = nt->OptionalHeader.SizeOfImage;
            for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
                const uint32_t va    = sec->VirtualAddress;
                const uint32_t rawSz = sec->SizeOfRawData;
                if (rawSz > 0 && size_t(sec->PointerToRawData) + rawSz <= pe.size() && size_t(va) + rawSz <= mapped.size()) {
                    std::memcpy(mapped.data() + va, pe.data() + sec->PointerToRawData, rawSz);
                }
            }

            uint32_t verifiedCount = 0u;
            const bool realOk = VerifyPhysicsCameraGmstTable(img, &verifiedCount);

            // Planted fail-closed control: corrupt fDeathForceForceMax (0x20A7518+8) -> MUST reject!
            const float badVal = 5000.0f;
            std::memcpy(mapped.data() + 0x20A7518u + 8u, &badVal, 4u);
            const bool plantedReject = !VerifyPhysicsCameraGmstTable(img);

            size_t totalSpecs = 0;
            PhysicsCameraGmstTable(&totalSpecs);
            check(realOk && verifiedCount == totalSpecs && plantedReject,
                  "real_skyrim17104_physics_camera_gmsts_verified");
        } else {
            check(true, "real_skyrim17104_physics_camera_gmsts_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

#ifndef RAW_PHYSICS_CAMERA_GMST_NO_MAIN
int main() {
    const int fail = raw::rt::RunPhysicsCameraGmstTests();
    std::printf("physics_camera_gmst: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
