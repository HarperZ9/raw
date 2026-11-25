// test_adaptive_audio_director.cpp: Wave 51 Hysteresis-Smoothed Adaptive Acoustic &
// Soundscape Director Tests.
// Linkable into ce_runtime_check via raw::rt::RunAdaptiveAudioDirectorTests(checkFn),
// or buildable standalone with -DRAW_AUDIO_DIRECTOR_STANDALONE_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_adaptive_audio_director.h"
#include "../src/engine_rt/rt_live_config.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace raw::rt {

int RunAdaptiveAudioDirectorTests(void (*checkFn)(bool, const char*)) {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) ++failures;
        if (checkFn) {
            checkFn(ok, name);
        } else {
            std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        }
    };

    constexpr float kDt = 1.0f / 60.0f;

    // 1. Hysteresis anti-thrashing proof: brief 0.5s weather flicker does NOT switch ExploreDay -> StormBlizzard,
    //    whereas sustained storm past minHoldSeconds transitions cleanly!
    {
        AdaptiveAudioDirector dir;
        AudioDirectorInput sunny{};
        sunny.gameHour = 12.0f;

        AudioDirectorInput storm = sunny;
        storm.snow01       = 0.85f;
        storm.windBeaufort = 9.0f;

        // 0.5 s brief weather flicker right away (30 frames) -> MUST stay ExploreDay!
        for (int i = 0; i < 30; ++i) dir.Step(kDt, storm);
        const bool resistedEarlyFlicker =
            dir.State().activeMood == static_cast<uint8_t>(AudioMood::ExploreDay) &&
            dir.State().transitionCount == 0u;

        // Return to sunny for 5.0 s (past minHoldSeconds), then another brief 0.5 s flicker -> MUST STILL stay ExploreDay!
        for (int i = 0; i < 300; ++i) dir.Step(kDt, sunny);
        for (int i = 0; i < 30; ++i)  dir.Step(kDt, storm);
        const bool resistedMidFlicker =
            dir.State().activeMood == static_cast<uint8_t>(AudioMood::ExploreDay) &&
            dir.State().transitionCount == 0u;

        // Now sustain storm for 2.0 s -> MUST transition cleanly to StormBlizzard!
        for (int i = 0; i < 120; ++i) dir.Step(kDt, storm);
        const bool transitionedSustained =
            dir.State().activeMood == static_cast<uint8_t>(AudioMood::StormBlizzard) &&
            dir.State().transitionCount == 1u;

        check(resistedEarlyFlicker && resistedMidFlicker && transitionedSustained,
              "audio_director_hysteresis_blocks_flicker_and_commits_sustained_storm");
    }

    // 2. Priority interrupt proof: UnderwaterMuffle & CombatSkirmish override immediately (0s hold),
    //    and UnderwaterMuffle seals LPF <= 700 Hz!
    {
        AdaptiveAudioDirector dir;
        AudioDirectorInput combat{};
        combat.gameHour          = 12.0f;
        combat.combatIntensity01 = 0.85f;

        dir.Step(kDt, combat);
        const bool combatImmediate =
            dir.State().activeMood == static_cast<uint8_t>(AudioMood::CombatSkirmish) &&
            dir.State().interruptCount == 1u;

        AudioDirectorInput submerge = combat;
        submerge.underwater = true;
        dir.Step(kDt, submerge);
        const bool underwaterImmediate =
            dir.State().activeMood == static_cast<uint8_t>(AudioMood::UnderwaterMuffle) &&
            dir.State().lpfCutoffHz <= 700.0f &&
            dir.State().reverbWetMult >= 1.50f &&
            dir.State().interruptCount == 2u;

        check(combatImmediate && underwaterImmediate,
              "audio_director_priority_interrupts_combat_and_underwater_lpf_under_700hz");
    }

    // 3. Exact mood-weight conservation across 200 stepped transition frames (|sum - 1.0| < 1e-4, zero NaNs!)
    {
        AdaptiveAudioDirector dir;
        bool weightsValid = true;
        float maxSumErr = 0.0f;

        for (int frame = 0; frame < 200; ++frame) {
            AudioDirectorInput in{};
            if (frame < 40) {
                in.gameHour = 12.0f; // ExploreDay
            } else if (frame < 80) {
                in.gameHour = 12.0f; in.combatIntensity01 = 0.8f; // CombatSkirmish interrupt
            } else if (frame < 120) {
                in.dragonBossActive = true; // DragonBoss interrupt
            } else if (frame < 160) {
                in.underwater = true; // UnderwaterMuffle interrupt
            } else {
                in.gameHour = 23.0f; in.aurora01 = 0.9f; // Surface into night aurora
            }
            dir.Step(kDt, in);

            const auto& st = dir.State();
            float sum = 0.0f;
            for (int m = 0; m < 8; ++m) {
                if (!std::isfinite(st.moodWeights[m]) || st.moodWeights[m] < -1.0e-6f || st.moodWeights[m] > 1.00001f) {
                    weightsValid = false;
                }
                sum += st.moodWeights[m];
            }
            const float err = std::fabs(sum - 1.0f);
            if (!std::isfinite(err) || err >= 1.0e-4f || !std::isfinite(st.lpfCutoffHz) ||
                !std::isfinite(st.reverbWetMult) || !std::isfinite(st.dialogueDuckGain)) {
                weightsValid = false;
            }
            if (err > maxSumErr) maxSumErr = err;
        }

        if (!checkFn) {
            std::printf("[AudioDirector 200f] maxWeightSumErr=%.2e valid=%s\n",
                        double(maxSumErr), weightsValid ? "YES" : "NO");
        }
        check(weightsValid, "audio_director_200f_weight_sum_unity_and_zero_nan");
    }

    // 4. Dialogue/menu ducking (< 0.45), stinger pulse & auto-clear, RAW_GetAudioDirectorState C ABI,
    //    and [Audio] AdaptiveAudioDirector=0 disable gate!
    {
        const LiveEngineConfig prevCfg = GetLiveConfig();
        LiveEngineConfig onCfg = DefaultLiveEngineConfig();
        onCfg.adaptiveAudioDirector = true;
        SetLiveConfigSnapshot(onCfg);

        AudioDirectorInit();
        AudioDirectorTriggerStinger(AudioStinger::ParryDeflect, 0.30f);

        AudioDirectorInput dlg{};
        dlg.gameHour       = 12.0f;
        dlg.dialogueActive = true;

        // First step: stinger MUST be pending
        AudioDirectorStepDirect(kDt, dlg);
        RawAudioDirectorState st1{};
        const int ok1 = RAW_GetAudioDirectorState(&st1);
        const bool stingerActive = (ok1 == 1 && st1.stingerPending == static_cast<uint8_t>(AudioStinger::ParryDeflect));

        // Step 0.45 s more (27 frames): stinger MUST auto-clear AND dialogueDuckGain < 0.45!
        for (int i = 0; i < 27; ++i) AudioDirectorStepDirect(kDt, dlg);
        RawAudioDirectorState st2{};
        RAW_GetAudioDirectorState(&st2);
        const bool duckAndClearOk =
            st2.stingerPending == static_cast<uint8_t>(AudioStinger::None) &&
            st2.dialogueDuckGain < 0.45f &&
            st2.dialogueDuckGain >= 0.25f;

        check(stingerActive && duckAndClearOk, "audio_director_dialogue_ducking_and_stinger_autoclear");

        // Verify [Audio] AdaptiveAudioDirector INI round-trip & 0 disable gate freezes updates
        const std::string defIni = FormatDefaultLiveConfigIni(DefaultLiveEngineConfig());
        LiveEngineConfig parsedDef{};
        ParseLiveConfigIni(defIni, parsedDef);
        LiveEngineConfig parsedOff{};
        ParseLiveConfigIni("[Audio]\nAdaptiveAudioDirector=0\n", parsedOff);

        SetLiveConfigSnapshot(parsedOff);
        AudioDirectorInput boss{};
        boss.dragonBossActive = true;
        AudioDirectorStepDirect(kDt, boss); // MUST be ignored while AdaptiveAudioDirector=0!

        RawAudioDirectorState stOff{};
        RAW_GetAudioDirectorState(&stOff);

        const bool iniGateOk =
            defIni.find("AdaptiveAudioDirector=1") != std::string::npos &&
            parsedDef.adaptiveAudioDirector &&
            !parsedOff.adaptiveAudioDirector &&
            stOff.directorEnabled == 0u &&
            stOff.activeMood == static_cast<uint8_t>(AudioMood::ExploreDay); // DragonBoss was gated off!
        check(iniGateOk, "audio_director_ini_roundtrip_c_abi_and_disable_gate");

        AudioDirectorShutdown();
        SetLiveConfigSnapshot(prevCfg);
    }

    if (!checkFn) {
        std::printf("AdaptiveAudioDirectorTests completed: %s (failures=%d)\n",
                    failures == 0 ? "PASS" : "FAIL", failures);
    }
    return failures;
}

}  // namespace raw::rt

#ifdef RAW_AUDIO_DIRECTOR_STANDALONE_MAIN
int main() {
    const int failures = raw::rt::RunAdaptiveAudioDirectorTests(nullptr);
    return failures == 0 ? 0 : 1;
}
#endif
