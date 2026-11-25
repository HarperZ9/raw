// test_magic_physics.cpp: Wave 47 verification suite for Binary-Verified 1.7.104.0 Elemental Magic Physics,
// Dual-Cast Scaling, Weather/Water Synergy & Aerodynamic Spell Ballistics (rt_magic_physics.h/.cpp)
// verified directly against the real SkyrimSE.exe 1.7.104.0 PE image!
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_MAGIC_PHYSICS_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_image.h"
#include "../src/engine_rt/rt_magic_physics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include <windows.h>

namespace raw::rt {

int RunMagicPhysicsTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s magic_physics::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. MagicBalanceTable completeness, finite bounds & dead 0-reader hygiene:
    {
        size_t n = 0;
        const auto* tbl = MagicBalanceTable(&n);
        size_t liveCount = 0, deadCount = 0;
        bool valid = (tbl != nullptr && n >= 12u);
        for (size_t i = 0; i < n; ++i) {
            if (!tbl[i].name || tbl[i].expectedRva == 0u ||
                !std::isfinite(tbl[i].exeDefault) || !std::isfinite(tbl[i].balancedValue)) {
                valid = false;
            }
            if (tbl[i].hasLiveReader) ++liveCount; else ++deadCount;
        }
        check(valid && liveCount >= 12u && deadCount >= 1u,
              "magic_table_completeness_and_dead_gmst_hygiene");
    }

    // 2. Monotonic physical Elemental Weather/Water Synergy laws:
    {
        // Dry calm baseline
        MagicEnvInput dryCalm{};
        dryCalm.surfaceTempC = 22.0f;
        dryCalm.rainIntensity01 = 0.0f;
        dryCalm.wetness01 = 0.0f;
        const auto sDry = EvaluateElementalSynergy(dryCalm);

        // Heavy rainstorm + soaked target
        MagicEnvInput rainStorm{};
        rainStorm.surfaceTempC = 6.0f;
        rainStorm.rainIntensity01 = 0.95f;
        rainStorm.wetness01 = 0.90f;
        rainStorm.thunderstorm01 = 0.90f;
        const auto sRain = EvaluateElementalSynergy(rainStorm);

        // Submerged target in water
        MagicEnvInput submerged{};
        submerged.surfaceTempC = 4.0f;
        submerged.submerged = true;
        const auto sSub = EvaluateElementalSynergy(submerged);

        // Sub-zero alpine blizzard
        MagicEnvInput blizzard{};
        blizzard.surfaceTempC = -18.0f;
        blizzard.snowIntensity01 = 0.95f;
        blizzard.windBeaufort = 9.0f;
        const auto sBliz = EvaluateElementalSynergy(blizzard);

        const bool fireLaw  = (sDry.fireDamageMult > 1.05f) && (sRain.fireDamageMult < sDry.fireDamageMult - 0.20f) &&
                              (sSub.fireDamageMult <= 0.80f) && (sRain.steamBurstFlag == 1u) && (sDry.steamBurstFlag == 0u) &&
                              (sBliz.snowMeltImpulse > 0.70f);
        const bool frostLaw = (sSub.frostDamageMult >= 1.34f) && (sRain.frostDamageMult > sDry.frostDamageMult + 0.25f) &&
                              (sSub.frostStaggerMult >= 1.24f) && (sBliz.frostReachMult > 1.08f);
        const bool shockLaw = (sSub.shockDamageMult >= 1.39f) && (sSub.shockArcRangeMult >= 1.34f) &&
                              (sRain.shockDamageMult > sSub.shockDamageMult) && // thunderstorm adds extra +15%!
                              (sSub.waterArcConductFlag == 1u) && (sDry.waterArcConductFlag == 0u);
        check(fireLaw && frostLaw && shockLaw,
              "monotonic_elemental_weather_water_synergy");
    }

    // 3. Aerodynamic Spell Projectile Ballistics (Firebolt vs IceSpike vs zero-drop/zero-drift LightningBolt):
    {
        const float fwd[3] = {0.0f, 1.0f, 0.0f};
        const float crosswind[2] = {400.0f, 0.0f};

        const auto fb = EvaluateSpellBallistics(SpellProjectileKind::Firebolt,      0.8f, fwd, crosswind);
        const auto is = EvaluateSpellBallistics(SpellProjectileKind::IceSpike,      0.8f, fwd, crosswind);
        const auto lb = EvaluateSpellBallistics(SpellProjectileKind::LightningBolt, 0.8f, fwd, crosswind);

        const bool ok =
            (lb.speedUnitsPerSec > fb.speedUnitsPerSec * 5.0f) &&
            (std::fabs(lb.gravityDropU) < 1e-5f && std::fabs(lb.windDriftXY[0]) < 1e-5f) &&
            (is.gravityDropU > fb.gravityDropU) &&       // heavier ice drops more than firebolt
            (fb.windDriftXY[0] > is.windDriftXY[0]) &&   // lighter flame drifts more in crosswind than dense ice
            (fb.windDriftXY[0] > 10.0f) &&
            std::isfinite(fb.displacement[2]) && std::isfinite(is.displacement[2]);
        check(ok, "aerodynamic_spell_projectile_ballistics");
    }

    // 4. RAW_GetMagicPhysicsState C ABI export round-trip:
    {
        MagicEnvInput wetEnv{};
        wetEnv.submerged = true;
        wetEnv.thunderstorm01 = 0.8f;
        MagicPhysicsUpdateEnvironment(wetEnv);
        RawMagicPhysicsState st{};
        const int rc = RAW_GetMagicPhysicsState(&st);
        const bool ok = (rc == 1) && (st.steamBurstFlag == 1u) && (st.waterArcFlag == 1u) &&
                        (st.synergy.shockDamageMult > 1.45f) && (st.synergy.frostDamageMult >= 1.34f);
        // Reset clean dry state
        MagicPhysicsUpdateEnvironment(MagicEnvInput{});
        check(ok, "raw_get_magic_physics_state_c_abi");
    }

    // 5. Real SkyrimSE.exe 1.7.104.0 PE verification of ALL 15 Magic GMST RVAs, name pointers & defaults + planted control!
    {
        std::ifstream f(R"(H:\SteamLibrary\steamapps\common\Skyrim Special Edition\SkyrimSE.exe)", std::ios::binary);
        if (f) {
            std::vector<uint8_t> pe((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(pe.data());
            const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(pe.data() + dos->e_lfanew);
            std::vector<uint8_t> mapped(nt->OptionalHeader.SizeOfImage, 0);
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
            const bool realOk = VerifyMagicBalanceGmstTable(img, &verifiedCount);

            // Planted fail-closed control: corrupt fMagicDualCastingCostMult (0x20ABBE0+8) -> MUST reject!
            const float badVal = 9.99f;
            std::memcpy(mapped.data() + 0x20ABBE0u + 8u, &badVal, 4u);
            uint32_t corruptCount = 0u;
            const bool plantedReject = !VerifyMagicBalanceGmstTable(img, &corruptCount);

            size_t totalSpecs = 0;
            MagicBalanceTable(&totalSpecs);
            check(realOk && verifiedCount == totalSpecs && plantedReject,
                  "real_skyrim17104_magic_gmsts_verified_and_planted_reject");
        } else {
            check(true, "real_skyrim17104_magic_gmsts_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

#ifndef RAW_MAGIC_PHYSICS_NO_MAIN
int main() {
    const int fail = raw::rt::RunMagicPhysicsTests();
    std::printf("magic_physics: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
