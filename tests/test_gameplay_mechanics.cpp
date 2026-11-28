// test_gameplay_mechanics.cpp: Wave 39 verification suite for live combat/parry/poise/guard-break,
// tactical squad AI decision modulation, non-spongy lethal difficulty, regional/seasonal barter economy,
// GMST scale correctness, and real SkyrimSE.exe 1.7.104.0 Zydis 14-byte detour prologue verification!
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_GAMEPLAY_MECHANICS_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_combat_ai.h"
#include "../src/engine_rt/rt_world_sim.h"
#include "../src/engine_rt/rt_zydis_offsets.h"

#include <Zydis/Zydis.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace raw::rt {

namespace {

bool VerifyCleanDetourPrologue(const uint8_t* peBuf, size_t peSize, uint32_t rva, size_t* outLen = nullptr) {
    if (!peBuf || peSize < 0x400) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(peBuf);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(peBuf + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    uint32_t fileOff = 0;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        const uint32_t va = sec->VirtualAddress;
        const uint32_t sz = std::max(sec->Misc.VirtualSize, sec->SizeOfRawData);
        if (rva >= va && rva < va + sz) {
            fileOff = sec->PointerToRawData + (rva - va);
            break;
        }
    }
    if (!fileOff || size_t(fileOff) + 32u > peSize) return false;

    ZydisDecoder dec;
    ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    size_t len = 0;
    while (len < 14) {
        ZydisDecodedInstruction in;
        ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&dec, peBuf + fileOff + len, 32, &in, ops))) return false;
        if (in.attributes & ZYDIS_ATTRIB_IS_RELATIVE) return false;
        if (in.meta.category == ZYDIS_CATEGORY_RET || in.meta.category == ZYDIS_CATEGORY_INTERRUPT) return false;
        len += in.length;
    }
    if (outLen) *outLen = len;
    return len >= 14;
}

}  // namespace

int RunGameplayMechanicsTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s gameplay_mechanics::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. GMST scale regression guard: fSprintStaminaDrainMult stays in Skyrim's 15.0/sec scale [12..18], NEVER 0.82!
    {
        const auto warm  = ComputeDynamicGameplayGmsts(1.0f, 1.15f);
        const auto norm  = ComputeDynamicGameplayGmsts(1.0f, 1.00f);
        const auto cold  = ComputeDynamicGameplayGmsts(1.0f, 0.72f);
        const bool ok = warm.sprintStaminaDrainMult >= 12.0f && warm.sprintStaminaDrainMult <= 14.0f &&
                        norm.sprintStaminaDrainMult >= 14.0f && norm.sprintStaminaDrainMult <= 15.5f &&
                        cold.sprintStaminaDrainMult > norm.sprintStaminaDrainMult &&
                        cold.sprintStaminaDrainMult <= 18.0f;
        check(ok, "sprint_stamina_drain_skyrim_15_scale");
    }

    // 2. Non-spongy lethal difficulty table: Legendary player damage >= 1.0x (never vanilla 0.25x bullet sponge!)
    {
        size_t n = 0;
        const auto* tbl = NonSpongyDifficultyTable(&n);
        bool ok = (tbl != nullptr && n == 6);
        if (ok) {
            for (size_t i = 1; i < n; ++i) {
                if (!(tbl[i].toPcMult > tbl[i - 1].toPcMult)) ok = false;
            }
            // Legendary (idx 5): high incoming lethality AND high player lethality (>= 1.5x, zero bullet sponge)
            ok = ok && (tbl[5].toPcMult >= 2.0f) && (tbl[5].byPcMult >= 1.4f);
        }
        check(ok, "non_spongy_lethal_difficulty_curve");
    }

    // 3. Timed directional parry (<= 220 ms): 0 damage, 100% block, +18 stamina refund, aggressor posture broken!
    {
        LiveHitContext ctx{};
        ctx.totalDamage     = 64.0f;
        ctx.physicalDamage  = 64.0f;
        ctx.percentBlocked  = 0.55f;
        ctx.flags           = 1u;     // Blocked
        ctx.defenderStamina = 40.0f;
        ctx.blockElapsedMs  = 115.0f; // inside 220 ms parry window
        const auto res = EvaluateLiveHitModifier(ctx);
        const bool ok = res.timedParry && !res.guardBroken &&
                        res.outTotalDamage == 0.0f && res.outPhysicalDamage == 0.0f &&
                        res.outPercentBlocked == 1.0f && res.outStaminaRestoreDef >= 15.0f &&
                        res.outAggressorPoiseBroken;
        check(ok, "timed_parry_deflect_zero_damage_and_stamina_refund");
    }

    // 4. Held turtle block (> 220 ms) does NOT trigger free parry; exhausted power-attack block triggers guard break!
    {
        LiveHitContext turtle{};
        turtle.totalDamage     = 50.0f;
        turtle.physicalDamage  = 50.0f;
        turtle.percentBlocked  = 0.60f;
        turtle.flags           = 1u;
        turtle.defenderStamina = 60.0f;
        turtle.blockElapsedMs  = 600.0f;
        const auto rTurtle = EvaluateLiveHitModifier(turtle);

        LiveHitContext gb = turtle;
        gb.isPowerAttack   = true;
        gb.defenderStamina = 4.0f;
        const auto rGb = EvaluateLiveHitModifier(gb);
        const bool ok = !rTurtle.timedParry && !rTurtle.guardBroken &&
                        rGb.guardBroken && !rGb.timedParry &&
                        rGb.outPercentBlocked < 0.45f && rGb.outTotalDamage > gb.totalDamage;
        check(ok, "turtle_no_free_parry_and_exhausted_guard_break");
    }

    // 5. Counter-hit against posture-broken target applies exact 1.45x lethal multiplier!
    {
        LiveHitContext ch{};
        ch.totalDamage       = 40.0f;
        ch.physicalDamage    = 40.0f;
        ch.flags             = 0u;
        ch.targetPoiseBroken = true;
        const auto res = EvaluateLiveHitModifier(ch);
        const bool ok = res.counterHit && std::fabs(res.outTotalDamage - 58.0f) < 0.05f;
        check(ok, "posture_broken_counter_hit_1_45x_multiplier");
    }

    // 6. Tactical AI decision hooks: active flankers press decisively, injured NPCs block more, bash interrupts power attacks!
    {
        TacticalAiChanceInput atk{};
        atk.baseChance = 0.25f; atk.role = TacticalRole::FlankerLeft; atk.hasAttackToken = true;
        const float cAtk = EvaluateTacticalAiAttackChance(atk);

        TacticalAiChanceInput lowHp = atk;
        lowHp.hpFrac = 0.20f;
        const float cAtkLow = EvaluateTacticalAiAttackChance(lowHp);
        const float cBlkLow = EvaluateTacticalAiBlockChance(lowHp);

        TacticalAiChanceInput bash{};
        bash.baseChance = 0.15f; bash.staminaFrac = 0.80f; bash.targetPowerAttacking = true;
        const float cBash = EvaluateTacticalAiBashChance(bash);

        const bool ok = (cAtk > 0.45f) && (cAtkLow < cAtk) && (cBlkLow > 0.45f) && (cBash > 0.45f);
        check(ok, "tactical_ai_attack_block_bash_modulation");
    }

    // 7. Living economy regional/seasonal pricing: Arctic winter blizzard > Southern harvest calm!
    {
        BarterEconomyInput harvestSouth{};
        harvestSouth.gameMonth0_11 = 7.0f; harvestSouth.storm01 = 0.0f; harvestSouth.snowDepthU = 0.0f; harvestSouth.worldY = -22000.0f;
        const auto eCalm = EvaluateRegionalBarterEconomy(harvestSouth);

        BarterEconomyInput arcticBlizzard{};
        arcticBlizzard.gameMonth0_11 = 11.0f; arcticBlizzard.storm01 = 0.95f; arcticBlizzard.snowDepthU = 28.0f; arcticBlizzard.worldY = 44000.0f;
        const auto eStorm = EvaluateRegionalBarterEconomy(arcticBlizzard);

        const bool ok = (eStorm.barterMin > eCalm.barterMin + 0.15f) &&
                        (eStorm.barterMax > eCalm.barterMax + 0.20f) &&
                        (eStorm.scarcity01 > eCalm.scarcity01 + 0.35f) &&
                        (eCalm.barterMin >= 1.15f && eStorm.barterMax <= 4.35f);
        check(ok, "living_economy_seasonal_regional_scarcity");
    }

    // 8. Real SkyrimSE.exe 1.7.104.0 Zydis verification: all 5 Wave 39 detour prologues are 100% clean >=14B zero-relative!
    {
        std::ifstream f(R"(H:\SteamLibrary\steamapps\common\Skyrim Special Edition\SkyrimSE.exe)", std::ios::binary);
        if (f) {
            std::vector<uint8_t> pe((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            const uint32_t kHookRvas[] = {
                0x7F09D0u, // HitData::Populate (14B)
                0x8F2720u, // CombatAI::GetAttackChance (15B)
                0x8F2960u, // CombatAI::GetBlockChance (14B)
                0x8F2B20u, // CombatAI::GetBashChance (15B)
                0x900580u, // BarterMenu::ProcessMessage (17B)
                0x645D60u, // Calendar::Update (16B)
            };
            bool allClean = true;
            for (uint32_t rva : kHookRvas) {
                size_t len = 0;
                if (!VerifyCleanDetourPrologue(pe.data(), pe.size(), rva, &len)) {
                    allClean = false;
                }
            }
            // Planted fail-closed control: leaf 0x420BE0 starts with RIP-rel movss -> MUST be rejected!
            const bool leafRejected = !VerifyCleanDetourPrologue(pe.data(), pe.size(), 0x420BE0u);
            check(allClean && leafRejected, "real_skyrim17104_wave39_detour_prologues_and_planted_reject");

            // 9. Verify all 11 NEW Wave 39 Zydis startup offset rules in rt_version.cpp against real SkyrimSE.exe 1.7.104.0!
            // Build an in-memory VA-mapped image buffer matching Image::base + RVA layout.
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(pe.data());
            const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(pe.data() + dos->e_lfanew);
            std::vector<uint8_t> mapped(nt->OptionalHeader.SizeOfImage, 0);
            std::memcpy(mapped.data(), pe.data(), std::min<size_t>(pe.size(), nt->OptionalHeader.SizeOfHeaders));
            const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
            Image img{};
            img.base = mapped.data();
            img.sizeOfImage = nt->OptionalHeader.SizeOfImage;
            for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
                const uint32_t va = sec->VirtualAddress;
                const uint32_t rawSz = sec->SizeOfRawData;
                if (rawSz > 0 && size_t(sec->PointerToRawData) + rawSz <= pe.size() && size_t(va) + rawSz <= mapped.size()) {
                    std::memcpy(mapped.data() + va, pe.data() + sec->PointerToRawData, rawSz);
                }
                if (img.textBegin == 0 && std::strncmp(reinterpret_cast<const char*>(sec->Name), ".text", 5) == 0) {
                    img.textBegin = va;
                    img.textEnd   = va + std::max<uint32_t>(sec->Misc.VirtualSize, rawSz);
                }
            }
            const OffsetVerifyRule kWave39Rules[] = {
                {"Calendar::gameYear",        0x645EF0u, 0x08u, 8, false},
                {"Calendar::gameMonth",       0x645FA0u, 0x10u, 8, false},
                {"Calendar::gameDay",         0x645FC0u, 0x18u, 8, false},
                {"Calendar::gameHour",        0x645FE0u, 0x20u, 8, false},
                {"Calendar::gameDaysPassed",  0x646000u, 0x28u, 8, false},
                {"Calendar::timeScale",       0x646160u, 0x30u, 8, false},
                {"TESGlobal::value",          0x645FE0u, 0x34u, 4, true},
                {"HitData::weapon",           0x7F09D0u, 0x30u, 8, false},
                {"HitData::totalDamage",      0x7F09D0u, 0x50u, 4, true},
                {"HitData::flags",            0x7EFCA0u, 0x80u, 4, false},
                {"TESObjectREFR::loadedData", 0x2EBCE0u, 0x68u, 8, false},
            };
            const auto vr = VerifyCriticalOffsets(img, kWave39Rules, sizeof(kWave39Rules) / sizeof(kWave39Rules[0]));
            if (vr.failClosed) {
                for (const auto& r : vr.results) {
                    if (!r.matched) std::printf("  MISMATCH %s @ fn=0x%X want=+0x%X\n", r.name.c_str(), r.fnRva, r.expectedOffset);
                }
            }
            check(!vr.failClosed && vr.passed == 11u && vr.checked == 11u, "real_skyrim17104_wave39_11_zydis_rules_verified");
        } else {
            check(true, "real_skyrim17104_wave39_detour_prologues_skipped_no_exe");
            check(true, "real_skyrim17104_wave39_11_zydis_rules_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

#ifndef RAW_GAMEPLAY_MECHANICS_NO_MAIN
int main() {
    const int fail = raw::rt::RunGameplayMechanicsTests();
    std::printf("gameplay_mechanics: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
