// test_engine_health.cpp: Wave 53 Unified 0-100 Engine Health Scoreboard & Actionable Remediation Adviser Tests.
// Linkable into ce_runtime_check via raw::rt::RunEngineHealthTests(checkFn),
// or buildable standalone with -DRAW_ENGINE_HEALTH_STANDALONE_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_engine_health.h"
#include "../src/engine_rt/rt_live_config.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace raw::rt {
namespace {

bool HasAdvisoryContaining(const RawEngineHealthReport& rep, const char* substr) {
    for (uint8_t i = 0u; i < rep.advisoryCount && i < 4u; ++i) {
        if (std::strstr(rep.advisories[i], substr) != nullptr) return true;
    }
    return false;
}

}  // namespace

int RunEngineHealthTests(void (*checkFn)(bool, const char*)) {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) ++failures;
        if (checkFn) {
            checkFn(ok, name);
        } else {
            std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        }
    };

    // 1. Clean nominal system -> score == 100, Optimal, 0 advisories
    {
        EngineHealthInputs nominal{};
        nominal.zydisVerified        = true;
        nominal.activeHandles        = 42000u;
        nominal.maxSaveStringCount   = 31216u;
        nominal.fullPluginCount      = 15u;
        nominal.p95FrameMs           = 16.6f;
        nominal.hitchesOver50ms      = 0u;
        nominal.crashForensicsActive = true;
        nominal.flightRecorderActive = true;

        const RawEngineHealthReport rep = EvaluateEngineHealth(nominal);
        const bool ok =
            rep.compositeScore == 100u &&
            rep.grade == static_cast<uint8_t>(HealthGrade::Optimal) &&
            rep.advisoryCount == 0u;
        check(ok, "engine_health_nominal_system_score_100_optimal_zero_advisories");
    }

    // 2. Real-world elevated save string-table (58,429/65,535 like Autosave2) -> Healthy/Degraded + SAVE_STRINGS_HIGH
    {
        EngineHealthInputs elevatedSave{};
        elevatedSave.zydisVerified        = true;
        elevatedSave.activeHandles        = 80000u;
        elevatedSave.maxSaveStringCount   = 58429u;
        elevatedSave.worstSaveHealth      = 1u; // Warning
        elevatedSave.fullPluginCount      = 15u;
        elevatedSave.p95FrameMs           = 16.6f;
        elevatedSave.crashForensicsActive = true;
        elevatedSave.flightRecorderActive = true;

        const RawEngineHealthReport rep = EvaluateEngineHealth(elevatedSave);
        if (!checkFn) {
            std::printf("%s\n", EngineHealthFormatSummary(rep).c_str());
        }
        const bool ok =
            rep.compositeScore >= 45u && rep.compositeScore < 85u &&
            (rep.grade == static_cast<uint8_t>(HealthGrade::Healthy) ||
             rep.grade == static_cast<uint8_t>(HealthGrade::Degraded)) &&
            rep.advisoryCount >= 1u &&
            HasAdvisoryContaining(rep, "SAVE_STRINGS_HIGH") &&
            HasAdvisoryContaining(rep, "58429/65535");
        check(ok, "engine_health_58429_save_strings_emits_save_strings_high_advisory");
    }

    // 3. Missing master + ref-handle cap > 90% + severe frame hitches -> Critical (score < 45) with exact advisories!
    {
        EngineHealthInputs crit{};
        crit.zydisVerified        = true;
        crit.activeHandles        = 965000u; // 92.0% of 1,048,576!
        crit.handleCapacity       = 1048576u;
        crit.maxSaveStringCount   = 32000u;
        crit.missingMasters       = 2u;
        crit.fullPluginCount      = 90u;
        crit.p95FrameMs           = 58.0f;
        crit.hitchesOver50ms      = 14u;
        crit.crashForensicsActive = true;
        crit.flightRecorderActive = true;

        const RawEngineHealthReport rep = EvaluateEngineHealth(crit);
        if (!checkFn) {
            std::printf("%s\n", EngineHealthFormatSummary(rep).c_str());
        }
        const bool ok =
            rep.compositeScore < 45u &&
            rep.grade == static_cast<uint8_t>(HealthGrade::Critical) &&
            rep.advisoryCount >= 3u &&
            HasAdvisoryContaining(rep, "MOD_MISSING_MASTER") &&
            HasAdvisoryContaining(rep, "REF_HANDLES_CRITICAL") &&
            HasAdvisoryContaining(rep, "FRAME_HITCHES");
        check(ok, "engine_health_multi_failure_critical_under_45_with_prioritized_advisories");
    }

    // 4. RAW_GetEngineHealthReport C ABI, F10 summary formatting, and [Diagnostics] EngineHealthReport=0 gate!
    {
        const LiveEngineConfig prevCfg = GetLiveConfig();
        LiveEngineConfig onCfg = DefaultLiveEngineConfig();
        onCfg.engineHealthReport = true;
        SetLiveConfigSnapshot(onCfg);

        EngineHealthInit();
        EngineHealthInputs warnIn{};
        warnIn.maxSaveStringCount = 58429u;
        warnIn.worstSaveHealth    = 1u;
        EngineHealthUpdateDirect(warnIn);

        RawEngineHealthReport liveRep{};
        const int got = RAW_GetEngineHealthReport(&liveRep);
        const std::string summary = EngineHealthFormatSummary(liveRep);

        LiveStatusDumpData dump{};
        dump.config              = onCfg;
        dump.healthScore         = liveRep.compositeScore;
        dump.healthGrade         = liveRep.grade;
        dump.healthAdvisoryCount = liveRep.advisoryCount;
        for (uint8_t i = 0u; i < liveRep.advisoryCount && i < 4u; ++i) {
            dump.healthAdvisories[i] = liveRep.advisories[i];
        }
        const std::string f10Text = FormatLiveStatusDump(dump);

        const bool abiAndDumpOk =
            got == 1 &&
            liveRep.reportEnabled == 1u &&
            liveRep.compositeScore == 84u &&
            summary.find("[EngineHealth] score=84/100 (Healthy)") != std::string::npos &&
            f10Text.find("[Engine Health Scoreboard]") != std::string::npos &&
            f10Text.find("CompositeScore=84/100 (Healthy)") != std::string::npos &&
            f10Text.find("SAVE_STRINGS_HIGH") != std::string::npos;
        check(abiAndDumpOk, "engine_health_c_abi_and_f10_scoreboard_formatting");

        // Verify [Diagnostics] EngineHealthReport=0 INI gate freezes updates cleanly
        const std::string defIni = FormatDefaultLiveConfigIni(DefaultLiveEngineConfig());
        LiveEngineConfig parsedDef{};
        ParseLiveConfigIni(defIni, parsedDef);
        LiveEngineConfig parsedOff{};
        ParseLiveConfigIni("[Diagnostics]\nEngineHealthReport=0\n", parsedOff);

        SetLiveConfigSnapshot(parsedOff);
        EngineHealthInputs cleanIn{};
        EngineHealthUpdateDirect(cleanIn); // MUST be ignored while EngineHealthReport=0!

        RawEngineHealthReport offRep{};
        RAW_GetEngineHealthReport(&offRep);
        const bool iniOffOk =
            defIni.find("EngineHealthReport=1") != std::string::npos &&
            parsedDef.engineHealthReport &&
            !parsedOff.engineHealthReport &&
            offRep.reportEnabled == 0u &&
            offRep.compositeScore == 84u; // Clean update was gated off!
        check(iniOffOk, "engine_health_diagnostics_ini_roundtrip_and_disable_gate");

        EngineHealthShutdown();
        SetLiveConfigSnapshot(prevCfg);
    }

    if (!checkFn) {
        std::printf("EngineHealthTests completed: %s (failures=%d)\n",
                    failures == 0 ? "PASS" : "FAIL", failures);
    }
    return failures;
}

}  // namespace raw::rt

#ifdef RAW_ENGINE_HEALTH_STANDALONE_MAIN
int main() {
    const int failures = raw::rt::RunEngineHealthTests(nullptr);
    return failures == 0 ? 0 : 1;
}
#endif
