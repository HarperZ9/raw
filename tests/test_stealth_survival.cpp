// test_stealth_survival.cpp: Wave 40 verification suite for live weather-masked stealth
// (SneakDetectionCalc 0x41CD90), thermal body-warmth / hearth recovery survival,
// 3D acoustic environment classification, and real SkyrimSE.exe 1.7.104.0 Zydis verification!
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_STEALTH_SURVIVAL_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_stealth_survival.h"
#include "../src/engine_rt/rt_zydis_offsets.h"

#include <Zydis/Zydis.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace raw::rt {

namespace {

bool VerifyCleanPrologue14B(const uint8_t* peBuf, size_t peSize, uint32_t rva, size_t* outLen = nullptr) {
    if (!peBuf || peSize < 0x400) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(peBuf);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(peBuf + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    uint32_t fileOff = 0;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        const uint32_t va = sec->VirtualAddress;
        const uint32_t sz = std::max<uint32_t>(sec->Misc.VirtualSize, sec->SizeOfRawData);
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

int RunStealthSurvivalTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s stealth_survival::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    // 1. Heavy rain & blizzard mask footsteps (-12%..-35%), lowering exterior detection score:
    {
        StealthDetectInput in{};
        in.baseScore = 24; in.soundScore = 40.0f; in.visualScore = 10.0f;
        in.distanceU = 500.0f; in.isExterior = true; in.isMoving = true; in.hasLOS = true;

        StealthEnvSnapshot rainEnv{};
        rainEnv.rain01 = 0.90f; rainEnv.storm01 = 0.85f; rainEnv.windSpeedMs = 10.0f;
        const auto res = EvaluateWeatherMaskedStealth(in, rainEnv);
        const bool ok = (res.soundMaskMult <= 0.88f && res.soundMaskMult >= 0.65f) &&
                        (res.adjustedSound < in.soundScore * 0.88f) &&
                        (res.adjustedScore < in.baseScore);
        check(ok, "rain_and_storm_footstep_masking");
    }

    // 2. Dry crisp sub-zero snow increases crunch audibility (+8%..+14%):
    {
        StealthDetectInput in{};
        in.baseScore = 15; in.soundScore = 40.0f; in.visualScore = 10.0f;
        in.distanceU = 350.0f; in.isExterior = true; in.isMoving = true; in.hasLOS = true;

        StealthEnvSnapshot crispSnow{};
        crispSnow.ambientTempC = -11.0f; crispSnow.snowDepthU = 14.0f;
        crispSnow.rain01 = 0.0f; crispSnow.storm01 = 0.0f; crispSnow.windSpeedMs = 2.0f;
        const auto res = EvaluateWeatherMaskedStealth(in, crispSnow);
        const bool ok = (res.soundMaskMult >= 1.08f && res.soundMaskMult <= 1.14f) &&
                        (res.adjustedSound > in.soundScore * 1.07f) &&
                        (res.adjustedScore > in.baseScore);
        check(ok, "crisp_subzero_snow_crunch_amplification");
    }

    // 3. Dense fog, blizzard whiteout & dark moonless night reduce long-range visual detection:
    {
        StealthDetectInput farIn{};
        farIn.baseScore = 30; farIn.soundScore = 5.0f; farIn.visualScore = 45.0f;
        farIn.distanceU = 1100.0f; farIn.isExterior = true; farIn.hasLOS = true;

        StealthEnvSnapshot clearDay{};
        clearDay.fog01 = 0.0f; clearDay.snow01 = 0.0f; clearDay.nightDarkness01 = 0.0f;

        StealthEnvSnapshot fogNight{};
        fogNight.fog01 = 0.85f; fogNight.snow01 = 0.65f; fogNight.nightDarkness01 = 0.90f;

        const auto rClear = EvaluateWeatherMaskedStealth(farIn, clearDay);
        const auto rFog   = EvaluateWeatherMaskedStealth(farIn, fogNight);
        const bool ok = (rFog.visualOcclMult < 0.65f) &&
                        (rFog.adjustedVisual < rClear.adjustedVisual * 0.65f) &&
                        (rFog.adjustedScore < rClear.adjustedScore - 10);
        check(ok, "fog_blizzard_night_visual_occlusion");
    }

    // 4. Wind direction propagation asymmetry: downwind observer detects earlier than upwind observer:
    {
        StealthEnvSnapshot windy{};
        windy.windSpeedMs = 15.0f; windy.windDirX = 1.0f; windy.windDirY = 0.0f;

        StealthDetectInput downwind{};
        downwind.baseScore = 10; downwind.soundScore = 20.0f; downwind.visualScore = 10.0f;
        downwind.distanceU = 450.0f; downwind.isExterior = true; downwind.obsToTgtDotWind = +1.0f;

        StealthDetectInput upwind = downwind;
        upwind.obsToTgtDotWind = -1.0f;

        const auto rDown = EvaluateWeatherMaskedStealth(downwind, windy);
        const auto rUp   = EvaluateWeatherMaskedStealth(upwind, windy);
        const bool ok = (rDown.windBiasDelta > 3.0f) && (rUp.windBiasDelta < -3.0f) &&
                        (rDown.adjustedScore > rUp.adjustedScore + 6);
        check(ok, "wind_direction_downwind_vs_upwind_asymmetry");
    }

    // 5. Interior shelter invariance: exterior weather/wind never distorts interior sneak detection:
    {
        StealthDetectInput interior{};
        interior.baseScore = 22; interior.soundScore = 30.0f; interior.visualScore = 20.0f;
        interior.isExterior = false; interior.obsToTgtDotWind = +1.0f;
        StealthEnvSnapshot blizzard{};
        blizzard.storm01 = 1.0f; blizzard.snow01 = 1.0f; blizzard.fog01 = 1.0f; blizzard.windSpeedMs = 25.0f;
        const auto res = EvaluateWeatherMaskedStealth(interior, blizzard);
        check(res.adjustedScore == 22 && res.soundMaskMult == 1.0f && res.visualOcclMult == 1.0f && res.windBiasDelta == 0.0f,
              "interior_sheltered_from_weather_stealth");
    }

    // 6. Thermal survival: Arctic icy-water blizzard chills to Freezing; hearth warms rapidly to WarmAndReady!
    {
        ThermalSurvivalState st{};
        st.coreWarmth = 75.0f; st.wetness01 = 0.0f;

        ThermalSurvivalInput arcticWater{};
        arcticWater.ambientTempC = -14.0f; arcticWater.windSpeedMs = 16.0f;
        arcticWater.snow01 = 0.9f; arcticWater.altitudeZ = 1400.0f;
        arcticWater.inWater = true; arcticWater.dtSec = 1.0f;
        for (int i = 0; i < 30; ++i) st = StepThermalSurvival(st, arcticWater);
        const bool frozeOk = (st.coreWarmth < 28.0f) && (st.wetness01 > 0.95f) &&
                             (st.effectiveWindChillC < -20.0f) && (st.staminaRegenMult <= 0.72f);

        ThermalSurvivalInput hearthIn{};
        hearthIn.ambientTempC = -12.0f; hearthIn.windSpeedMs = 12.0f;
        hearthIn.nearHearth = true; hearthIn.hearthProximity01 = 0.95f; hearthIn.dtSec = 1.0f;
        for (int i = 0; i < 15; ++i) st = StepThermalSurvival(st, hearthIn);
        const bool warmedOk = (st.coreWarmth >= 90.0f) && (st.wetness01 < 0.05f) &&
                              (st.comfortTier == ThermalComfortTier::WarmAndReady) &&
                              (st.staminaRegenMult >= 1.14f) && (st.combatStaminaMult > 0.80f);
        check(frozeOk && warmedOk, "thermal_blizzard_water_chill_and_hearth_recovery");
    }

    // 7. 3D Acoustic environment classification across all 5 environments:
    {
        AcousticClassificationInput pineIn{};
        pineIn.inInterior = false; pineIn.snowCover01 = 0.85f; pineIn.worldY = -14000.0f; pineIn.temperatureC = -4.0f;
        const auto pine = ClassifyAcousticEnvironment(pineIn);

        AcousticClassificationInput cryptIn{};
        cryptIn.inInterior = true; cryptIn.enclosureRadiusU = 1100.0f; cryptIn.temperatureC = 8.0f;
        const auto crypt = ClassifyAcousticEnvironment(cryptIn);

        AcousticClassificationInput iceIn{};
        iceIn.inInterior = true; iceIn.isIceCavern = true; iceIn.enclosureRadiusU = 1200.0f; iceIn.temperatureC = -8.0f;
        const auto ice = ClassifyAcousticEnvironment(iceIn);

        AcousticClassificationInput tundraIn{};
        tundraIn.inInterior = false; tundraIn.snowCover01 = 0.0f; tundraIn.windSpeedMs = 14.0f; tundraIn.worldY = 4000.0f;
        const auto tundra = ClassifyAcousticEnvironment(tundraIn);

        AcousticClassificationInput waterIn{};
        waterIn.underwater = true;
        const auto water = ClassifyAcousticEnvironment(waterIn);

        const bool ok =
            pine.kind   == AcousticEnvironmentKind::SnowDampedPineForest &&
            crypt.kind  == AcousticEnvironmentKind::NordicStoneCrypt &&
            ice.kind    == AcousticEnvironmentKind::GlacierIceCavern &&
            tundra.kind == AcousticEnvironmentKind::OpenWindyTundra &&
            water.kind  == AcousticEnvironmentKind::Underwater &&
            (ice.rt60Sec > crypt.rt60Sec) &&
            (crypt.rt60Sec > 1.5f) &&
            (pine.rt60Sec < 0.30f) &&
            (pine.hfAirAbsorbDbM > tundra.hfAirAbsorbDbM) &&
            (water.lowPassHz < 1000.0f);
        check(ok, "acoustic_5_environments_rt60_and_hf_absorption");
    }

    // 8. Real SkyrimSE.exe 1.7.104.0 Zydis verification: SneakDetectionCalc (0x41CD90) 16B prologue + caller 0x6A3972 + Actor::actorStateFlags (+0xC8)!
    {
        std::ifstream f(R"(H:\SteamLibrary\steamapps\common\Skyrim Special Edition\SkyrimSE.exe)", std::ios::binary);
        if (f) {
            std::vector<uint8_t> pe((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            size_t plen = 0;
            const bool cleanPrologue = VerifyCleanPrologue14B(pe.data(), pe.size(), 0x41CD90u, &plen) && (plen == 16u);

            // Map PE image to VA layout to verify Actor::actorStateFlags (+0xC8) in 0x41CD90 AND caller E8 at 0x6A3972:
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
            // Verify call instruction at 0x6A3972 targets 0x41CD90 (E8 19 94 D7 FF):
            const uint8_t* callSite = mapped.data() + 0x6A3972u;
            int32_t rel32 = 0;
            std::memcpy(&rel32, callSite + 1, 4);
            const uint32_t callTgt = static_cast<uint32_t>(static_cast<int64_t>(0x6A3972u) + 5 + rel32);
            const bool callOk = (callSite[0] == 0xE8u && callTgt == 0x41CD90u);

            // Verify Actor::actorStateFlags (+0xC8) rule in rt_version.cpp against 0x41CD90:
            const OffsetVerifyRule kRule[] = {
                {"Actor::actorStateFlags", 0x41CD90u, 0xC8u, 4, false},
            };
            const auto vr = VerifyCriticalOffsets(img, kRule, 1);
            check(cleanPrologue && callOk && !vr.failClosed && vr.passed == 1u,
                  "real_skyrim17104_sneak_0x41CD90_prologue_caller_and_zydis_rule");
        } else {
            check(true, "real_skyrim17104_sneak_0x41CD90_skipped_no_exe");
        }
    }

    return fail;
}

}  // namespace raw::rt

#ifndef RAW_STEALTH_SURVIVAL_NO_MAIN
int main() {
    const int fail = raw::rt::RunStealthSurvivalTests();
    std::printf("stealth_survival: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
