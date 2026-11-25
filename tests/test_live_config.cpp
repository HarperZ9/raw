// test_live_config.cpp: Wave 44 verification suite for Live Hot-Reloading Configuration (raw_engine.ini)
// & F10 In-Game Runtime Diagnostics Dump (rt_live_config.h/.cpp).
// Compiles standalone (default) OR links into ce_runtime_check with -DRAW_LIVE_CONFIG_NO_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_live_config.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <windows.h>

namespace raw::rt {

int RunLiveConfigTests(void (*checkFn)(bool, const char*) = nullptr) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s live_config::%s\n", cond ? "PASS" : "FAIL", name);
        }
        if (cond) ++pass; else ++fail;
    };

    const std::filesystem::path tmpDir = std::filesystem::temp_directory_path();
    const std::filesystem::path iniPath = tmpDir / "raw_w44_test_engine.ini";
    const std::filesystem::path dumpPath = tmpDir / "raw_w44_live_status_dump.txt";
    std::error_code ec;
    std::filesystem::remove(iniPath, ec);
    std::filesystem::remove(dumpPath, ec);

    // 1. Default INI auto-generation & faithful round-trip parsing (preserving user edits when present):
    {
        const bool created = EnsureDefaultLiveConfigFile(iniPath.string());
        const bool exists  = std::filesystem::exists(iniPath, ec);
        LiveEngineConfig loaded{};
        const bool parsed  = LoadLiveConfigFromFile(iniPath.string(), loaded);
        const LiveEngineConfig def = DefaultLiveEngineConfig();

        const bool defaultsMatch =
            std::fabs(loaded.parryWindowMs - def.parryWindowMs) < 1e-3f &&
            std::fabs(loaded.dodgeIFrameMs - def.dodgeIFrameMs) < 1e-3f &&
            std::fabs(loaded.dodgeStaminaCost - def.dodgeStaminaCost) < 1e-3f &&
            loaded.hitstopEnabled == def.hitstopEnabled &&
            std::fabs(loaded.hitstopScale - def.hitstopScale) < 1e-3f &&
            loaded.nonSpongyDifficulty == def.nonSpongyDifficulty &&
            loaded.tacticalSquadAi == def.tacticalSquadAi &&
            loaded.statusDumpHotkeyVK == 121u &&
            loaded.hotReloadIntervalFrames == 60u;

        // Append a custom comment and confirm EnsureDefaultLiveConfigFile does NOT overwrite existing file!
        {
            std::ofstream app(iniPath, std::ios::binary | std::ios::app);
            app << "; user custom marker\n";
        }
        EnsureDefaultLiveConfigFile(iniPath.string());
        std::ifstream in(iniPath, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        const bool preserved = (ss.str().find("; user custom marker") != std::string::npos);

        check(created && exists && parsed && defaultsMatch && preserved,
              "default_ini_autogeneration_and_preservation");
    }

    // 2. Parse overrides & strict safe clamping on out-of-range / NaN / corrupt values:
    {
        const std::string wildIni =
            "[Combat]\n"
            "ParryWindowMs = 99999 ; out of range high -> clamp 450\n"
            "DodgeIFrameMs = -50   ; negative -> clamp 80\n"
            "DodgeStaminaCost = NaN ; corrupt -> fallback default 18\n"
            "DodgeIFramesEnabled = false\n"
            "HitstopEnabled = 0\n"
            "HitstopScale = 7.5    ; clamp 2.0\n"
            "NonSpongyDifficulty = off\n"
            "TacticalSquadAI = no\n"
            "[Physics]\n"
            "SkeletonXpbdEnabled = 1\n"
            "XpbdWindScale = 2.25\n"
            "FovBreathEnabled = 0\n"
            "FovBreathScale = -4.0 ; clamp 0.0\n"
            "[World]\n"
            "TfcLodVoidGuard = 1\n"
            "LivingEconomyEnabled = 0\n"
            "WeatherStealthEnabled = true\n"
            "ThermalSurvivalEnabled = bogus_value ; keep default true\n"
            "[Diagnostics]\n"
            "StatusDumpHotkeyVK = 999 ; clamp 254 (0xFE)\n"
            "HotReloadIntervalFrames = 2 ; clamp 10\n";

        LiveEngineConfig c{};
        const bool parsed = ParseLiveConfigIni(wildIni, c);
        const bool ok = parsed &&
            std::fabs(c.parryWindowMs - 450.0f) < 1e-3f &&
            std::fabs(c.dodgeIFrameMs - 80.0f) < 1e-3f &&
            std::fabs(c.dodgeStaminaCost - 18.0f) < 1e-3f &&
            !c.dodgeIFramesEnabled &&
            !c.hitstopEnabled &&
            std::fabs(c.hitstopScale - 2.0f) < 1e-3f &&
            !c.nonSpongyDifficulty &&
            !c.tacticalSquadAi &&
            c.skeletonXpbdEnabled &&
            std::fabs(c.xpbdWindScale - 2.25f) < 1e-3f &&
            !c.fovBreathEnabled &&
            std::fabs(c.fovBreathScale - 0.0f) < 1e-3f &&
            c.tfcLodVoidGuard &&
            !c.livingEconomyEnabled &&
            c.weatherStealthEnabled &&
            c.thermalSurvivalEnabled &&
            c.statusDumpHotkeyVK == 0xFEu &&
            c.hotReloadIntervalFrames == 10u;
        check(ok, "parse_overrides_and_corrupt_value_safe_clamping");
    }

    // 3. Hot-reload mtime/size change detection (sub-microsecond unchanged path + reload on edit):
    {
        std::filesystem::remove(iniPath, ec);
        LiveConfigHotReloader hr;
        hr.Init(iniPath.string(), true);

        // Unchanged file must NOT trigger reload
        const bool unchanged1 = !hr.PollReload(true);
        const uint32_t count0 = hr.ReloadCount();

        // Mutate file contents and bump mtime explicitly by +2 seconds so NTFS mtime changes deterministically
        {
            std::ofstream out(iniPath, std::ios::binary | std::ios::trunc);
            out << "[Combat]\nParryWindowMs=310\nHitstopScale=1.4\n[Diagnostics]\nHotReloadIntervalFrames=15\n";
        }
        HANDLE hFile = CreateFileA(iniPath.string().c_str(), FILE_WRITE_ATTRIBUTES,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            FILETIME ft{};
            GetSystemTimeAsFileTime(&ft);
            ULARGE_INTEGER uli{};
            uli.LowPart  = ft.dwLowDateTime;
            uli.HighPart = ft.dwHighDateTime;
            uli.QuadPart += 30000000ull;  // +3 seconds
            ft.dwLowDateTime  = uli.LowPart;
            ft.dwHighDateTime = uli.HighPart;
            SetFileTime(hFile, nullptr, nullptr, &ft);
            CloseHandle(hFile);
        }

        const bool reloaded = hr.PollReload(true);
        const bool unchangedAfter = !hr.PollReload(true);
        const bool ok = unchanged1 && count0 == 0u && reloaded && hr.ReloadCount() == 1u && unchangedAfter &&
                        std::fabs(hr.Config().parryWindowMs - 310.0f) < 1e-3f &&
                        std::fabs(hr.Config().hitstopScale - 1.4f) < 1e-3f &&
                        hr.Config().hotReloadIntervalFrames == 15u;
        check(ok, "hot_reload_mtime_change_detection");
    }

    // 4. F10 Runtime Diagnostics Dump completeness verification:
    {
        LiveStatusDumpData d{};
        d.config              = DefaultLiveEngineConfig();
        d.config.parryWindowMs = 240.0f;
        d.configReloads       = 2u;
        d.iniPath             = iniPath.string();
        d.runtimeVersion      = "1.7.104.0";
        d.zydisPassed         = 41u;
        d.zydisChecked        = 41u;
        d.zydisOk             = true;
        d.skeletonHookActive  = true;
        d.skeletonActors      = 12u;
        d.skeletonIkBones     = 48u;
        d.skeletonXpbdBones   = 96u;
        d.gameplayHooksMask   = 0x1Fu;
        d.timedParries        = 7u;
        d.dodgesTriggered     = 9u;
        d.evadedHits          = 4u;
        d.hitstopVerified     = true;
        d.hitstopTriggers     = 11u;
        d.activeHoldName      = "Winterhold";
        d.economyBuyMult      = 1.42f;
        d.economySellMult     = 0.88f;
        d.sneakHookActive     = true;
        d.coreWarmth          = 64.5f;
        d.acousticRt60Sec     = 0.85f;
        d.calYear             = 201.0f;
        d.calMonth            = 11.0f;
        d.calDay              = 14.0f;
        d.calHour             = 22.5f;

        const bool written = WriteLiveStatusDumpToFile(dumpPath.string(), d);
        std::ifstream in(dumpPath, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string txt = ss.str();

        const bool hasAllSections =
            (txt.find("Zydis Binary Verification: 41/41 (OK - VERIFIED)") != std::string::npos) &&
            (txt.find("[Active Configuration]") != std::string::npos) &&
            (txt.find("ParryWindowMs=240") != std::string::npos) &&
            (txt.find("[Skeleton IK & 240Hz XPBD Soft-Body]") != std::string::npos) &&
            (txt.find("ActiveActors=12") != std::string::npos) &&
            (txt.find("[Combat, Parry, Dodge i-Frames, Hitstop & Camera FOV]") != std::string::npos) &&
            (txt.find("TimedParries=7") != std::string::npos) &&
            (txt.find("EvadedHits=4") != std::string::npos) &&
            (txt.find("[9-Hold Living Skyrim Economy & NPC Ecosystem]") != std::string::npos) &&
            (txt.find("ActiveHold=Winterhold") != std::string::npos) &&
            (txt.find("[Weather Stealth, Thermal Survival & 3D Acoustics]") != std::string::npos) &&
            (txt.find("SabineEyringRT60=0.85s") != std::string::npos) &&
            (txt.find("[Live Skyrim Calendar]") != std::string::npos) &&
            (txt.find("4E 201") != std::string::npos);

        check(written && hasAllSections, "f10_status_dump_completeness");
    }

    std::filesystem::remove(iniPath, ec);
    std::filesystem::remove(dumpPath, ec);
    return fail;
}

}  // namespace raw::rt

#ifndef RAW_LIVE_CONFIG_NO_MAIN
int main() {
    const int fail = raw::rt::RunLiveConfigTests();
    std::printf("live_config: %s (%d failures)\n", fail == 0 ? "ALL PASS" : "FAILED", fail);
    return fail == 0 ? 0 : 1;
}
#endif
