#pragma once
// rt_core_fixes.h: Core Creation Engine OS, Memory, Timer, Scheduling & Cross-Title Engine Fixes.
// Addresses shared Creation Engine bottlenecks across Skyrim SE/AE/VR, Fallout 4/VR, Fallout 76, and Starfield (CE2):
//   1) Sub-millisecond Windows multimedia & kernel timer precision (0.5-1.0 ms; kills 15.6 ms Wait/Sleep micro-stutter)
//   2) Windows 11 EcoQoS / hybrid-core power throttling disable (keeps render + raw::tx + job threads on P-cores)
//   3) Process-wide Low-Fragmentation Heap (LFH) activation (cuts long-session cell-transition heap fragmentation)
//   4) Universal 8192-handle CRT stdio raise across host CRT + ucrtbase.dll + legacy MSVC CRTs (512-handle save fix)
//   5) Cross-title safe engine setting unlocks (lip-sync async load, multi-core Havok threads, 16x AF, FO4/Skyrim LOD)
//   6) Clean restoration on RAW_ModuleStop + exported C ABI RAW_EngineProfile().
// Original work, MIT.

#include <cstdint>
#include <string>
#include <vector>

#include "rt_callgraph.h"
#include "rt_image.h"

namespace raw::rt {

struct EngineProfileInfo {
    uint32_t titleId;               // raw::versions::EngineTitle
    uint32_t genId;                 // raw::versions::EngineGen
    uint32_t usesNativeDx12;        // 1 on Starfield CE2, 0 on DX11 CE1/1.5/1.8
    uint32_t isMultiplayerOnline;   // 1 on Fallout 76 (gameplay/VM hooks disabled)
    uint32_t timerPrecisionUs;      // achieved timer resolution in microseconds (e.g. 500 or 1000)
    uint32_t lfhHeapsEnabled;       // count of Win32/CRT heaps upgraded to LFH
    uint32_t maxStdioHandles;       // active _setmaxstdio handle ceiling (e.g. 8192)
    uint32_t ecoQosDisabled;        // 1 if Windows 11 PowerThrottling was disabled cleanly
    uint32_t coreSettingsApplied;   // count of cross-title engine settings unlocked
    uint32_t havokThreads;          // configured Havok physics worker count
    char     displayName[64];
    char     tablePrefix[32];
};

// Stage 1 (before version table load): OS timer, EcoQoS power throttling, LFH heaps, and 8192 stdio handles.
std::string CoreFixesInitProcess();

// Stage 2 (after version table load): cross-title Creation Engine setting unlocks (respects online & CE2 guards).
std::string CoreFixesApplyEngine(const Image& img, const std::vector<RefTarget>& refs);

// Restores timer resolution cleanly before live reload / shutdown.
void CoreFixesShutdown();

// JSON diagnostic summary for live telemetry.
std::string CoreFixesStatsJson();

}  // namespace raw::rt

extern "C" __declspec(dllexport) void RAW_EngineProfile(raw::rt::EngineProfileInfo* out);
