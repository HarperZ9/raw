// test_combat_impact.cpp: Wave 41 verification suite for Tactile Combat Hitstop
// (BSTimer::SetGlobalTimeMultiplier 0xCE3970 / g_globalTimeMultiplier 0x20CCAC8),
// 32-slot 3D World-Space Impact Decal Ring Buffer (RAW_GetImpactDecals C ABI),
// and real SkyrimSE.exe 1.7.104.0 Zydis verification!
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_COMBAT_IMPACT_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_combat_impact.h"
#include "../src/engine_rt/rt_zydis_offsets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace raw::rt {

int RunCombatImpactTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s combat_impact::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. Hitstop profiles match exact tactile targets (Parry 0.18x/65ms, GuardBreak 0.28x/55ms, Kill 0.35x/80ms):
    {
        const auto p = GetHitstopProfile(HitstopEventKind::TimedParry);
        const auto g = GetHitstopProfile(HitstopEventKind::HeavyGuardBreak);
        const auto k = GetHitstopProfile(HitstopEventKind::LethalKillBlow);
        const bool ok = std::fabs(p.timeScale - 0.18f) < 1e-4f && std::fabs(p.durationMs - 65.0f) < 1e-4f &&
                        std::fabs(g.timeScale - 0.28f) < 1e-4f && std::fabs(g.durationMs - 55.0f) < 1e-4f &&
                        std::fabs(k.timeScale - 0.35f) < 1e-4f && std::fabs(k.durationMs - 80.0f) < 1e-4f;
        check(ok, "hitstop_exact_parry_guardbreak_kill_profiles");
    }

    // 2. Wall-clock hitstop smooth decay strictly returns to 1.0f within <= 95 ms:
    {
        HitstopState st{};
        const bool trig = TriggerHitstop(st, HitstopEventKind::TimedParry, 1000ull, 1.0f);
        bool needW = false;
        float mult = 1.0f;
        StepHitstop(st, 1030ull, false, &needW, &mult);
        const float midScale = st.appliedTimeScale;
        StepHitstop(st, 1071ull, false, &needW, &mult);
        const float easeScale = st.appliedTimeScale;
        StepHitstop(st, 1095ull, false, &needW, &mult);
        const bool ok = trig &&
                        std::fabs(midScale - 0.18f) < 0.01f &&
                        (easeScale > 0.18f && easeScale < 1.0f) &&
                        !st.active && needW && std::fabs(mult - 1.0f) < 1e-5f;
        check(ok, "hitstop_smooth_wallclock_decay_within_95ms");
    }

    // 3. Menu pause immediately cancels hitstop back to 1.0f:
    {
        HitstopState st{};
        TriggerHitstop(st, HitstopEventKind::LethalKillBlow, 2000ull, 1.0f);
        bool needW = false;
        float mult = 0.0f;
        StepHitstop(st, 2015ull, true /*menuPaused*/, &needW, &mult);
        check(!st.active && needW && std::fabs(mult - 1.0f) < 1e-5f && st.canceledByPauseCount == 1u,
              "hitstop_instant_cancel_on_menu_pause");
    }

    // 4. Slow-time shout / mod preservation: never overwrites non-1.0 external globalTimeMultiplier!
    {
        HitstopState st{};
        const bool trigSlow = TriggerHitstop(st, HitstopEventKind::TimedParry, 3000ull, 0.30f);
        const bool trigFast = TriggerHitstop(st, HitstopEventKind::HeavyGuardBreak, 3050ull, 1.50f);
        check(!trigSlow && !trigFast && !st.active && st.skippedSlowTimeCount == 2u,
              "hitstop_preserves_user_slow_time_shout");
    }

    // 5. 32-slot 3D Impact Decal Ring Buffer: push, wraparound, newest-first C-ABI 8-float packing & expiration!
    {
        ImpactDecalRing ring{};
        for (uint32_t i = 0; i < 40u; ++i) {
            const float pos[3] = {100.0f + float(i) * 10.0f, -200.0f, 50.0f};
            ring.Push(pos, 42.0f, static_cast<ImpactDecalKind>(i % 5u),
                      10000ull + uint64_t(i) * 1000ull, float(i) * 0.1f, 1.0f, 30.0f);
        }
        float packed[16 * 8]{};
        // At t = 55000 ms: newest push was i=39 at 49000 ms (age 6s < 30s), i=16..39 (24 decals) are alive -> capped at maxCount=16
        const uint32_t n = ring.PackForGpu(packed, 16u, 55000ull);
        const bool newestIs39 = (n == 16u) &&
                                std::fabs(packed[0] - 490.0f) < 1e-3f &&
                                std::fabs(packed[3] - 42.0f) < 1e-3f &&
                                std::fabs(packed[4] - float(39u % 5u)) < 1e-3f &&
                                std::fabs(packed[5] - 6.0f) < 1e-3f &&
                                (packed[7] > 0.9f);
        // At t = 200000 ms: all 30s decals must be expired -> 0 returned!
        const uint32_t nExpired = ring.PackForGpu(packed, 16u, 200000ull);
        check(ring.totalPushed == 40u && newestIs39 && nExpired == 0u,
              "impact_decal_32slot_ring_wrap_pack_and_expire");
    }

    // 6. Real SkyrimSE.exe 1.7.104.0 Zydis verification: BSTimer::SetGlobalTimeMultiplier (0xCE3970)
    //    writes g_globalTimeMultiplier (0x20CCAC8) & target (0x20CCACC) + startup rules (+0x18, +0x3A)!
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
            const bool siteOk = VerifyGlobalTimeMultiplierCodeSite(img, 0xCE3970u, 0x20CCAC8u, 0x20CCACCu);
            // Planted fail-closed control: wrong global RVA MUST fail!
            const bool plantedReject = !VerifyGlobalTimeMultiplierCodeSite(img, 0xCE3970u, 0x20CCAD0u, 0x20CCACCu);

            const OffsetVerifyRule kRules[] = {
                {"BSTimer::delta",               0xCE3970u, 0x18u, 4, true},
                {"BSTimer::useGlobalTimeTarget", 0xCE3970u, 0x3Au, 1, false},
            };
            const auto vr = VerifyCriticalOffsets(img, kRules, 2);
            check(siteOk && plantedReject && !vr.failClosed && vr.passed == 2u,
                  "real_skyrim17104_global_time_multiplier_zydis_verified");
        } else {
            check(true, "real_skyrim17104_global_time_multiplier_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

#ifndef RAW_COMBAT_IMPACT_NO_MAIN
int main() {
    const int fail = raw::rt::RunCombatImpactTests();
    std::printf("combat_impact: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
