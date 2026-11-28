// rt_core_fixes.cpp: Core Creation Engine OS, Memory, Timer, Scheduling & Cross-Title Engine Fixes.
// Original work, MIT.
#include "rt_core_fixes.h"
#include "rt_control.h"
#include "rt_version.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace raw::rt {
namespace {

EngineProfileInfo g_info{};
bool g_timerActive = false;

uint32_t RaiseStdioAllCrts(int want) {
    int best = _setmaxstdio(want);
    static const char* kCrts[] = {
        "ucrtbase.dll",
        "msvcr110.dll",
        "msvcr120.dll",
        "vcruntime140.dll",
    };
    using SetMaxStdioFn = int(__cdecl*)(int);
    for (const char* dll : kCrts) {
        HMODULE m = GetModuleHandleA(dll);
        if (!m) continue;
        if (auto fn = reinterpret_cast<SetMaxStdioFn>(GetProcAddress(m, "_setmaxstdio"))) {
            const int r = fn(want);
            if (r > best) best = r;
        }
    }
    return best > 0 ? uint32_t(best) : 512u;
}

uint32_t EnableLowFragmentationHeaps() {
    const DWORD count = GetProcessHeaps(0, nullptr);
    if (count == 0) return 0;
    std::vector<HANDLE> heaps(count);
    const DWORD got = GetProcessHeaps(count, heaps.data());
    uint32_t enabled = 0;
    ULONG lfh = 2;  // HEAP_LFH
    for (DWORD i = 0; i < got; ++i) {
        if (heaps[i] && HeapSetInformation(heaps[i], HeapCompatibilityInformation, &lfh, sizeof(lfh))) {
            ++enabled;
        }
    }
    return enabled;
}

bool DisableWindowsPowerThrottling() {
    // PROCESS_POWER_THROTTLING_EXECUTION_SPEED (0x1) | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION (0x4)
    PROCESS_POWER_THROTTLING_STATE pts{};
    pts.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    pts.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | 0x4u;
    pts.StateMask = 0;  // explicitly disable EcoQoS throttling & preserve high-res timer across focus changes
    return SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &pts, sizeof(pts)) != 0;
}

uint32_t EnableHighResTimer() {
    uint32_t achievedUs = 15625u;
    if (timeBeginPeriod(1) == TIMERR_NOERROR) {
        g_timerActive = true;
        achievedUs = 1000u;
    }
    // Optional 0.5 ms kernel timer resolution via ntdll!NtSetTimerResolution (best-effort, non-fatal if unavailable).
    if (HMODULE ntdll = GetModuleHandleA("ntdll.dll")) {
        using NtSetTimerResolutionFn = LONG(NTAPI*)(ULONG, BOOLEAN, PULONG);
        if (auto fn = reinterpret_cast<NtSetTimerResolutionFn>(GetProcAddress(ntdll, "NtSetTimerResolution"))) {
            ULONG actual = 0;
            if (fn(5000u, TRUE, &actual) == 0 && actual > 0) {
                achievedUs = actual / 10u;   // 100ns units -> microseconds
            }
        }
    }
    return achievedUs;
}

}  // namespace

std::string CoreFixesInitProcess() {
    g_info = {};
    const auto prof = raw::versions::DetectEngineProfile();
    g_info.titleId = static_cast<uint32_t>(prof.title);
    g_info.genId = static_cast<uint32_t>(prof.gen);
    g_info.usesNativeDx12 = prof.usesNativeDx12 ? 1u : 0u;
    g_info.isMultiplayerOnline = prof.isMultiplayerOnline ? 1u : 0u;
    std::snprintf(g_info.displayName, sizeof(g_info.displayName), "%s", prof.displayName);
    std::snprintf(g_info.tablePrefix, sizeof(g_info.tablePrefix), "%s", prof.tablePrefix);

    g_info.maxStdioHandles = RaiseStdioAllCrts(8192);
    g_info.lfhHeapsEnabled = EnableLowFragmentationHeaps();
    g_info.ecoQosDisabled = DisableWindowsPowerThrottling() ? 1u : 0u;
    g_info.timerPrecisionUs = EnableHighResTimer();

    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "core-fixes(os): host='%s' (gen=%u dx12=%u online=%u) stdio=%u lfh_heaps=%u ecoqos_off=%u timer=%uus",
                  g_info.displayName, g_info.genId, g_info.usesNativeDx12, g_info.isMultiplayerOnline,
                  g_info.maxStdioHandles, g_info.lfhHeapsEnabled, g_info.ecoQosDisabled, g_info.timerPrecisionUs);
    return buf;
}

std::string CoreFixesApplyEngine(const Image& img, const std::vector<RefTarget>& refs) {
    const auto& prof = ActiveProfile();
    g_info.titleId = static_cast<uint32_t>(prof.title);
    g_info.genId = static_cast<uint32_t>(prof.gen);
    g_info.usesNativeDx12 = prof.usesNativeDx12 ? 1u : 0u;
    g_info.isMultiplayerOnline = prof.isMultiplayerOnline ? 1u : 0u;
    std::snprintf(g_info.displayName, sizeof(g_info.displayName), "%s", prof.displayName);
    std::snprintf(g_info.tablePrefix, sizeof(g_info.tablePrefix), "%s", prof.tablePrefix);

    if (prof.isMultiplayerOnline) {
        return "core-fixes(engine): online multiplayer guard active (Fallout 76) - engine memory settings left untouched";
    }

    struct KV { const char* k; const char* v; };
    uint32_t applied = 0;

    // 1. Scale Havok physics worker count to modern multi-core CPUs (clamped 4..8 to avoid Havok lock contention).
    const unsigned hwCores = std::max(4u, std::thread::hardware_concurrency());
    const unsigned physApprox = std::clamp(hwCores / 2u, 4u, 8u);
    g_info.havokThreads = physApprox;
    char havokThreadsStr[16];
    std::snprintf(havokThreadsStr, sizeof(havokThreadsStr), "%u", physApprox);

    if (prof.gen == raw::versions::EngineGen::CE1_DX11 || prof.gen == raw::versions::EngineGen::CE1_5_FO4_DX11) {
        // Shared CE1 (Skyrim SE/AE/VR) + CE1.5 (Fallout 4/VR) core unlocks:
        // - bBackgroundLoadLipFiles: eliminates dialogue hitch when NPCs begin speaking
        // - iMaxAnisotropy=16: crisp grazing textures across all terrain & architecture
        // - iNumThreads:Havok: scales physics solver beyond 2011 2-core defaults
        // - bForceUpdateDiffuseOnly=0: prevents dropped normal/specular refreshes on dynamic surfaces
        const KV kSharedCe1[] = {
            {"bBackgroundLoadLipFiles:General", "1"},
            {"iNumThreads:Havok", havokThreadsStr},
            {"iMaxAnisotropy:Display", "16"},
            {"bForceUpdateDiffuseOnly:Display", "0"},
            {"iPresentInterval:Display", "0"},
        };
        for (const auto& kv : kSharedCe1) {
            if (SetSetting(img, refs, kv.k, kv.v)) ++applied;
        }
    }

    if (prof.gen == raw::versions::EngineGen::CE1_DX11) {
        // Wave 35 Structural Creation Engine 1 (Skyrim SE/AE/VR) Shadow Stability & Particle Unlocks:
        // Respects EngineFixes.dll guard on grass/tree/terrain LOD distances so modded 32 GB lists never OOM at save load!
        const bool hasEngineFixes = GetModuleHandleA("EngineFixes.dll") != nullptr;
        static const KV kCe1Safe[] = {
            {"fSunUpdateThreshold:Display", "0.002"},
            {"fSunShadowUpdateTime:Display", "0.0"},
            {"iMaxDesired:Particles", "6000"},
        };
        for (const auto& kv : kCe1Safe) {
            if (SetSetting(img, refs, kv.k, kv.v)) ++applied;
        }
        if (!hasEngineFixes) {
            // Keep TerrainManager LOD distances untouched so SkyrimPrefs.ini / modded LOD grids never shrink or pop holes!
            static const KV kCe1Lod[] = {
                {"fTreesMidLODSwitchDist:Display", "10000000.0"},
                {"uiMaxSkinnedTreesToRender:Trees", "200"},
                {"fGrassStartFadeDistance:Grass", "9500.0"},
                {"fGrassMaxStartFadeDistance:Grass", "11000.0"},
            };
            for (const auto& kv : kCe1Lod) {
                if (SetSetting(img, refs, kv.k, kv.v)) ++applied;
            }
        }
    } else if (prof.gen == raw::versions::EngineGen::CE1_5_FO4_DX11) {
        // Fallout 4 / FO4VR safe core upgrades (strictly preserves PreVis/Precombines - never disables bUseCombinedObjects!):
        static const KV kFo4[] = {
            {"fDirShadowDistance:Display", "20000.0"},
            {"fShadowDistance:Display", "20000.0"},
            {"fShadowBiasScale:Display", "0.22"},
            {"fSunUpdateThreshold:Display", "0.0001"},
            {"fSunShadowUpdateTime:Display", "0.0"},
            {"bNvGodraysEnable:Display", "0"},
            {"fMeshLODLevel1FadeDist:Display", "16000.0"},
            {"fMeshLODLevel2FadeDist:Display", "12000.0"},
            {"fLODFadeOutMultObjects:LOD", "20.0"},
            {"fLODFadeOutMultItems:LOD", "15.0"},
            {"fLODFadeOutMultActors:LOD", "20.0"},
            {"iMaxDesired:Particles", "4096"},
            {"uMaxDecals:Decals", "1000"},
            {"iMaxDecalsPerFrame:Display", "256"},
        };
        for (const auto& kv : kFo4) {
            if (SetSetting(img, refs, kv.k, kv.v)) ++applied;
        }
    } else if (prof.gen == raw::versions::EngineGen::CE2_Starfield_DX12) {
        // Starfield CE2: native DX12 + Havok 2022 already handles high FPS; only unlock present interval safely.
        if (SetSetting(img, refs, "iPresentInterval:Display", "0")) ++applied;
    }

    g_info.coreSettingsApplied = applied;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "core-fixes(engine): %u cross-title settings applied for %s (havok_threads=%u)",
                  applied, prof.displayName, g_info.havokThreads);
    return buf;
}

void CoreFixesShutdown() {
    if (g_timerActive) {
        timeEndPeriod(1);
        g_timerActive = false;
    }
}

std::string CoreFixesStatsJson() {
    char buf[384];
    std::snprintf(buf, sizeof(buf),
                  "{\"title\":\"%s\",\"title_id\":%u,\"gen_id\":%u,\"native_dx12\":%s,\"online_guard\":%s,"
                  "\"timer_us\":%u,\"lfh_heaps\":%u,\"stdio_max\":%u,\"ecoqos_off\":%s,\"havok_threads\":%u,\"core_settings\":%u}",
                  g_info.displayName, g_info.titleId, g_info.genId,
                  g_info.usesNativeDx12 ? "true" : "false", g_info.isMultiplayerOnline ? "true" : "false",
                  g_info.timerPrecisionUs, g_info.lfhHeapsEnabled, g_info.maxStdioHandles,
                  g_info.ecoQosDisabled ? "true" : "false", g_info.havokThreads, g_info.coreSettingsApplied);
    return buf;
}

}  // namespace raw::rt

extern "C" __declspec(dllexport) void RAW_EngineProfile(raw::rt::EngineProfileInfo* out) {
    if (out) *out = raw::rt::g_info;
}
