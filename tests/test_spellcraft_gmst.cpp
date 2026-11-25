// test_spellcraft_gmst.cpp: Milestone Wave 60 verification suite for Binary-Verified 1.7.104.0 Dual-Cast,
// Spell Ballistics, Conjuration/Illusion/Ward AI, Explosion Clutter & Light-Sensor GMSTs (rt_spellcraft_gmst.h/.cpp)
// verified directly against the real SkyrimSE.exe 1.7.104.0 PE image (372 total unique cataloged GMSTs!).
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_SPELLCRAFT_GMST_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_image.h"
#include "../src/engine_rt/rt_spellcraft_gmst.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <windows.h>

namespace raw::rt {

int RunSpellcraftGmstTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s spellcraft_gmst::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. Table completeness (24 live + 6 dead = 30), uniqueness, category coverage & dead-legacy hygiene:
    {
        size_t n = 0;
        const auto* tbl = SpellcraftGmstTable(&n);
        size_t liveCount = 0, deadCount = 0;
        bool hasDual = false, hasSpell = false, hasWard = false, hasConj = false, hasIll = false, hasAlt = false, hasExp = false, hasLight = false;
        bool deadStaffSneak = false, deadCastDet = false, deadStaffMin = false;
        std::unordered_set<std::string> seenNames;
        std::unordered_set<uint32_t>    seenRvas;
        bool valid = (tbl != nullptr && n >= 24u);
        for (size_t i = 0; i < n; ++i) {
            if (!tbl[i].name || !tbl[i].category || tbl[i].expectedRva == 0u) valid = false;
            if (!tbl[i].isInt32 && (!std::isfinite(tbl[i].exeDefaultF) || !std::isfinite(tbl[i].balancedF))) valid = false;
            if (!seenNames.insert(tbl[i].name).second) valid = false;
            if (!seenRvas.insert(tbl[i].expectedRva).second) valid = false;
            if (tbl[i].hasLiveReader) {
                ++liveCount;
                if (std::strcmp(tbl[i].category, "DualCast") == 0)    hasDual = true;
                if (std::strcmp(tbl[i].category, "Spellcraft") == 0)  hasSpell = true;
                if (std::strcmp(tbl[i].category, "Ward") == 0)        hasWard = true;
                if (std::strcmp(tbl[i].category, "Conjuration") == 0) hasConj = true;
                if (std::strcmp(tbl[i].category, "Illusion") == 0)    hasIll = true;
                if (std::strcmp(tbl[i].category, "Alteration") == 0)  hasAlt = true;
                if (std::strcmp(tbl[i].category, "Explosion") == 0)   hasExp = true;
                if (std::strcmp(tbl[i].category, "Light") == 0)       hasLight = true;
            } else {
                ++deadCount;
                if (std::strcmp(tbl[i].name, "fCombatSneakStaffMult") == 0)             deadStaffSneak = true;
                if (std::strcmp(tbl[i].name, "fSpellCastingDetectionMod") == 0)         deadCastDet = true;
                if (std::strcmp(tbl[i].name, "fCombatInventoryMinEquipTimeStaff") == 0) deadStaffMin = true;
            }
        }
        check(valid && liveCount == 24u && deadCount == 6u &&
              hasDual && hasSpell && hasWard && hasConj && hasIll && hasAlt && hasExp && hasLight &&
              deadStaffSneak && deadCastDet && deadStaffMin,
              "table_completeness_uniqueness_and_dead_legacy_gmst_hygiene");
    }

    // 2. Dual-cast scaling, directional ward absorption/shatter & C ABI export:
    {
        const auto singleLow  = EvaluateDualCastEfficiency(50.0f, 40.0f, 15.0f, false);
        const auto singleHigh = EvaluateDualCastEfficiency(50.0f, 40.0f, 100.0f, false);
        const auto dualHigh   = EvaluateDualCastEfficiency(50.0f, 40.0f, 100.0f, true);
        const bool dualOk = (singleHigh.magickaCost < singleLow.magickaCost) &&
                            (dualHigh.finalMagnitude > singleHigh.finalMagnitude * 2.1f) &&
                            (dualHigh.magickaCost > singleHigh.magickaCost * 2.3f);

        const auto wBlock = EvaluateWardAbsorption(60.0f, 40.0f, 20.0f);
        const auto wBreak = EvaluateWardAbsorption(60.0f, 95.0f, 20.0f);
        const auto wFlank = EvaluateWardAbsorption(60.0f, 40.0f, 110.0f);
        const bool wardOk = (wBlock.insideCone && !wBlock.wardBroken && wBlock.bleedThroughDmg == 0.0f) &&
                            (wBreak.insideCone && wBreak.wardBroken && std::fabs(wBreak.bleedThroughDmg - 35.0f) < 1e-4f) &&
                            (!wFlank.insideCone && wFlank.absorbedDamage == 0.0f && wFlank.bleedThroughDmg == 40.0f);

        RawSpellcraftState st{};
        const int rcOk   = RAW_GetSpellcraftState(&st);
        const int rcNull = RAW_GetSpellcraftState(nullptr);
        const bool abiOk = (rcOk == 1) && (rcNull == 0) &&
                           (std::fabs(st.dualCastAiChance - 0.48f) < 1e-4f) &&
                           (std::fabs(st.magicExplosionClutterMul - 4.0f) < 1e-4f) &&
                           (st.interiorLightMod == 40);
        check(dualOk && wardOk && abiOk, "dual_cast_ward_absorption_and_c_abi");
    }

    // 3. Real SkyrimSE.exe 1.7.104.0 PE verification of ALL 30 Spellcraft GMST RVAs, names & defaults + planted controls!
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
            const bool realOk = VerifySpellcraftGmstTable(img, &verifiedCount);

            // Planted fail-closed control #1: corrupt float fCombatMagicDualCastChance (0x20A2AC8+8) -> MUST reject!
            float savedDual = 0.0f;
            std::memcpy(&savedDual, mapped.data() + 0x20A2AC8u + 8u, 4u);
            const float badFloat = 99.0f;
            std::memcpy(mapped.data() + 0x20A2AC8u + 8u, &badFloat, 4u);
            const bool rejectFloat = !VerifySpellcraftGmstTable(img);
            std::memcpy(mapped.data() + 0x20A2AC8u + 8u, &savedDual, 4u);

            // Planted fail-closed control #2: corrupt int32 iLightLevelInteriorMod (0x209EC40+8) -> MUST reject!
            const int32_t badInt = 999;
            std::memcpy(mapped.data() + 0x209EC40u + 8u, &badInt, 4u);
            const bool rejectInt = !VerifySpellcraftGmstTable(img);

            size_t totalSpecs = 0;
            SpellcraftGmstTable(&totalSpecs);
            check(realOk && verifiedCount == totalSpecs && rejectFloat && rejectInt,
                  "real_skyrim17104_spellcraft_gmsts_verified");
        } else {
            check(true, "real_skyrim17104_spellcraft_gmsts_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

int run_spellcraft_gmst_tests() {
    return raw::rt::RunSpellcraftGmstTests(nullptr);
}

#ifndef RAW_SPELLCRAFT_GMST_NO_MAIN
int main() {
    const int fail = run_spellcraft_gmst_tests();
    std::printf("spellcraft_gmst: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
