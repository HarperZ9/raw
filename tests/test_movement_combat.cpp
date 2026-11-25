// test_movement_combat.cpp: Wave 43 verification suite for Tactile Directional Dodge-Step
// (260ms i-frame evasion, 18 stamina gate, 550ms cooldown, zero-damage hit evasion),
// Smooth Critically-Damped Combat/Sprint Camera FOV Breath (+4.2/+2.6/0.0 deg + dynamic baseline preservation),
// and real SkyrimSE.exe 1.7.104.0 Zydis verification of PlayerCamera::worldFOV (+0x13C) & firstPersonFOV (+0x140)!
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_MOVEMENT_COMBAT_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_movement_combat.h"
#include "../src/engine_rt/rt_zydis_offsets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace raw::rt {

int RunMovementCombatTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s movement_combat::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. Dodge trigger (Alt + direction & WASD double-tap) and stamina gating (< 18 rejected):
    {
        DodgeControllerState st{};
        // Insufficient stamina (17.9 < 18.0) MUST reject cleanly
        DodgeTriggerInput lowStam{};
        lowStam.nowMs          = 1000ull;
        lowStam.currentStamina = 17.9f;
        lowStam.altEdge        = true;
        lowStam.wasdHeld[2]    = true;  // Left
        const auto rLow = StepDodgeController(st, lowStam);
        const bool lowNotEvading = !st.evadingNow;

        // Sufficient stamina (18.0) triggers Left dodge cleanly with 18.0 cost
        DodgeTriggerInput okIn{};
        okIn.nowMs          = 1100ull;
        okIn.currentStamina = 18.0f;
        okIn.altEdge        = true;
        okIn.wasdHeld[2]    = true;
        const auto rOk = StepDodgeController(st, okIn);

        // Quick double-tap D (Right) after cooldown expires triggers Right dodge
        DodgeTriggerInput tap1{};
        tap1.nowMs          = 1700ull;
        tap1.currentStamina = 50.0f;
        tap1.wasdEdge[3]    = true;
        const auto rTap1 = StepDodgeController(st, tap1);

        DodgeTriggerInput tap2{};
        tap2.nowMs          = 1880ull;  // 180ms <= 260ms double-tap window
        tap2.currentStamina = 50.0f;
        tap2.wasdEdge[3]    = true;
        const auto rTap2 = StepDodgeController(st, tap2);

        const bool ok =
            (!rLow.triggered && rLow.rejectedStamina && lowNotEvading && st.dodgesRejectedStamina == 1u) &&
            (rOk.triggered && !rOk.rejectedStamina && std::fabs(rOk.staminaCost - 18.0f) < 1e-4f && rOk.direction == DodgeDirection::Left) &&
            (!rTap1.triggered) &&
            (rTap2.triggered && rTap2.direction == DodgeDirection::Right && st.dodgesTriggered == 2u);
        check(ok, "dodge_trigger_and_stamina_gate");
    }

    // 2. Exact 260ms wall-clock i-frame window open/expiry & 550ms cooldown enforcement:
    {
        DodgeControllerState st{};
        DodgeTriggerInput trig{};
        trig.nowMs          = 5000ull;
        trig.currentStamina = 80.0f;
        trig.altEdge        = true;
        const auto r0 = StepDodgeController(st, trig);

        const bool activeAt0   = IsDodgeEvadingAt(st, 5000ull);
        const bool activeAt259 = IsDodgeEvadingAt(st, 5259ull);
        const bool expired260  = !IsDodgeEvadingAt(st, 5260ull);

        // Attempt re-dodge at +549ms -> MUST be rejected by 550ms cooldown!
        DodgeTriggerInput early{};
        early.nowMs          = 5549ull;
        early.currentStamina = 80.0f;
        early.altEdge        = true;
        const auto rEarly = StepDodgeController(st, early);

        // Attempt re-dodge at +550ms -> MUST succeed!
        DodgeTriggerInput ready{};
        ready.nowMs          = 5550ull;
        ready.currentStamina = 80.0f;
        ready.altEdge        = true;
        const auto rReady = StepDodgeController(st, ready);

        check(r0.triggered && activeAt0 && activeAt259 && expired260 &&
              !rEarly.triggered && rEarly.rejectedCooldown &&
              rReady.triggered && st.dodgesTriggered == 2u,
              "dodge_260ms_iframe_and_550ms_cooldown");
    }

    // 3. Zero-damage incoming hit evasion during active 260ms i-frames + crisp 35ms evasion feel pulse:
    {
        DodgeControllerState st{};
        DodgeTriggerInput trig{};
        trig.nowMs          = 10000ull;
        trig.currentStamina = 60.0f;
        trig.altEdge        = true;
        StepDodgeController(st, trig);

        float totalDmg = 74.5f, physDmg = 62.0f, pctBlk = 0.0f;
        const bool evaded = TryEvadeIncomingHit(st, 10120ull, &totalDmg, &physDmg, &pctBlk);
        const bool feelOnAt10140  = IsDodgeEvasionFeelActive(st, 10140ull);
        const bool feelOffAt10156 = !IsDodgeEvasionFeelActive(st, 10156ull);

        // After i-frame expiry (10265ms), incoming hit MUST NOT be evaded!
        float lateTotal = 45.0f, latePhys = 45.0f, lateBlk = 0.25f;
        const bool evadedLate = TryEvadeIncomingHit(st, 10265ull, &lateTotal, &latePhys, &lateBlk);

        const bool ok =
            evaded &&
            std::fabs(totalDmg) < 1e-5f &&
            std::fabs(physDmg) < 1e-5f &&
            std::fabs(pctBlk - 1.0f) < 1e-5f &&
            st.evadedHits == 1u &&
            feelOnAt10140 && feelOffAt10156 &&
            !evadedLate && std::fabs(lateTotal - 45.0f) < 1e-4f && std::fabs(lateBlk - 0.25f) < 1e-4f;
        check(ok, "zero_damage_hit_evasion_and_35ms_feel");
    }

    // 4. Critically-damped FOV spring convergence (+4.2 sprint, +2.6 combat/dodge, 0.0 idle, zero overshoot, menu restore):
    {
        FovSpringState fs{};
        float liveFov = 80.0f;
        bool noOvershoot = true;

        // Sprint for 60 frames (~1.0s): must converge smoothly to +4.2 deg without overshooting!
        FovStepOutput sprintOut{};
        for (int i = 0; i < 60; ++i) {
            FovStepInput in{};
            in.liveReadFovDeg = liveFov;
            in.dtSec          = 1.0f / 60.0f;
            in.sprinting      = true;
            sprintOut = StepFovBreathSpring(fs, in);
            if (sprintOut.needWrite) liveFov = sprintOut.desiredWorldFovDeg;
            if (sprintOut.appliedOffsetDeg > kFovSprintOffsetDeg + 0.02f) noOvershoot = false;
        }
        const bool sprintConv = std::fabs(sprintOut.appliedOffsetDeg - 4.2f) < 0.05f &&
                                std::fabs(liveFov - 84.2f) < 0.05f;

        // Transition to weapon-drawn/dodge combat for 60 frames: converges to +2.6 deg!
        FovStepOutput combatOut{};
        for (int i = 0; i < 60; ++i) {
            FovStepInput in{};
            in.liveReadFovDeg        = liveFov;
            in.dtSec                 = 1.0f / 60.0f;
            in.inCombatOrWeaponDrawn = true;
            combatOut = StepFovBreathSpring(fs, in);
            if (combatOut.needWrite) liveFov = combatOut.desiredWorldFovDeg;
        }
        const bool combatConv = std::fabs(combatOut.appliedOffsetDeg - 2.6f) < 0.05f &&
                                std::fabs(liveFov - 82.6f) < 0.05f;

        // Menu pause immediately restores exact baseline (80.0 deg)!
        FovStepInput pauseIn{};
        pauseIn.liveReadFovDeg = liveFov;
        pauseIn.menuPaused     = true;
        const auto pauseOut = StepFovBreathSpring(fs, pauseIn);
        if (pauseOut.needWrite) liveFov = pauseOut.desiredWorldFovDeg;
        const bool pauseRestored = pauseOut.needWrite && std::fabs(liveFov - 80.0f) < 1e-4f &&
                                   std::fabs(pauseOut.appliedOffsetDeg) < 1e-4f;

        check(noOvershoot && sprintConv && combatConv && pauseRestored,
              "fov_spring_convergence_and_menu_restore");
    }

    // 5. Dynamic baseline FOV preservation when user changes FOV mid-session (e.g. console `fov 95`):
    {
        FovSpringState fs{};
        float liveFov = 75.0f;

        // Run a few sprint frames starting from user baseline 75.0
        for (int i = 0; i < 20; ++i) {
            FovStepInput in{};
            in.liveReadFovDeg = liveFov;
            in.dtSec          = 1.0f / 60.0f;
            in.sprinting      = true;
            const auto o = StepFovBreathSpring(fs, in);
            if (o.needWrite) liveFov = o.desiredWorldFovDeg;
        }
        const float preChangeBase = fs.baselineFovDeg;

        // User opens console and sets `fov 95`: liveReadFovDeg jumps externally to 95.0!
        liveFov = 95.0f;
        FovStepOutput resyncOut{};
        for (int i = 0; i < 60; ++i) {
            FovStepInput in{};
            in.liveReadFovDeg = liveFov;
            in.dtSec          = 1.0f / 60.0f;
            in.sprinting      = true;
            resyncOut = StepFovBreathSpring(fs, in);
            if (resyncOut.needWrite) liveFov = resyncOut.desiredWorldFovDeg;
        }

        // Stop sprinting -> MUST settle smoothly back to NEW baseline 95.0 (NEVER clobber back to 75.0!)
        for (int i = 0; i < 80; ++i) {
            FovStepInput in{};
            in.liveReadFovDeg = liveFov;
            in.dtSec          = 1.0f / 60.0f;
            const auto o = StepFovBreathSpring(fs, in);
            if (o.needWrite) liveFov = o.desiredWorldFovDeg;
        }

        const bool ok =
            std::fabs(preChangeBase - 75.0f) < 1e-3f &&
            fs.baselineResyncCount >= 1u &&
            std::fabs(fs.baselineFovDeg - 95.0f) < 1e-3f &&
            std::fabs(resyncOut.desiredWorldFovDeg - 99.2f) < 0.08f &&
            std::fabs(liveFov - 95.0f) < 0.02f;
        check(ok, "fov_baseline_preservation_on_console_change");
    }

    // 6. Real SkyrimSE.exe 1.7.104.0 Zydis verification: PlayerCamera::Ctor (0x8F79F0) ->
    //    PlayerCamera::Singleton (0x31A5478), worldFOV (+0x13C), firstPersonFOV (+0x140) + planted fail-closed control!
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
                if (img.textBegin == 0 && std::strncmp(reinterpret_cast<const char*>(sec->Name), ".text", 5) == 0) {
                    img.textBegin = va;
                    img.textEnd   = va + std::max<uint32_t>(sec->Misc.VirtualSize, rawSz);
                }
            }

            const bool siteOk = VerifyPlayerCameraFovCodeSite(img, 0x8F79F0u, 0x31A5478u, 0x13Cu, 0x140u);
            // Planted fail-closed controls: wrong offset or wrong singleton RVA MUST fail!
            const bool badOffReject  = !VerifyPlayerCameraFovCodeSite(img, 0x8F79F0u, 0x31A5478u, 0x144u, 0x140u);
            const bool badSingReject = !VerifyPlayerCameraFovCodeSite(img, 0x8F79F0u, 0x31A5480u, 0x13Cu, 0x140u);

            const OffsetVerifyRule kRules[] = {
                {"PlayerCamera::worldFOV",       0x8F79F0u, 0x13Cu, 4, true},
                {"PlayerCamera::firstPersonFOV", 0x8F79F0u, 0x140u, 4, true},
            };
            const auto vr = VerifyCriticalOffsets(img, kRules, 2);
            check(siteOk && badOffReject && badSingReject && !vr.failClosed && vr.passed == 2u,
                  "real_skyrim17104_playercamera_fov_zydis_verified");
        } else {
            check(true, "real_skyrim17104_playercamera_fov_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

#ifndef RAW_MOVEMENT_COMBAT_NO_MAIN
int main() {
    const int fail = raw::rt::RunMovementCombatTests();
    std::printf("movement_combat: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
