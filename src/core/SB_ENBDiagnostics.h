#pragma once
//=============================================================================
//  SB_ENBDiagnostics.h — ENBSetParameter Validation & Diagnostics
//
//  Wraps ENBSetParameter calls with return-value checking, per-parameter
//  success tracking, and diagnostic logging to identify dead-stripped
//  shader variables that silently reject data.
//
//  Integrated with ENBInterface.h — uses the same resolved function
//  pointers and BridgeData param table. No duplicate definitions.
//
//  Usage:
//    1. Call SB::Diag::Init() after ENBInterface::Init() succeeds
//    2. Call SB::Diag::PushAllParams() instead of ENBInterface::PushAllData()
//    3. Diagnostic reports are logged automatically
//
//  Author: Zain Dana Harper
//  Version: 2.1.0 (adapted for ENBInterface integration)
//=============================================================================

#include "BridgeData.h"
#include "ENBInterface.h"
#include <SKSE/SKSE.h>
#include <array>
#include <cstring>

namespace SB::Diag
{
    //-------------------------------------------------------------------------
    //  Per-parameter tracking state
    //-------------------------------------------------------------------------

    struct ParamStatus
    {
        int  successCount  = 0;
        int  failCount     = 0;
        bool everSucceeded = false;
        bool lastResult    = false;
    };

    struct ShaderStatus
    {
        int totalSuccess = 0;
        int totalFail    = 0;
        std::array<ParamStatus, 128> params;  // Max 128 params per shader
    };

    // Max 12 shaders (kTargetShaders currently has 9)
    inline std::array<ShaderStatus, 12> g_shaderStats;
    inline int  g_framesSinceReport  = 0;
    inline int  g_reportInterval     = 300;
    inline bool g_firstFrameLogged   = false;
    inline int  g_totalFramesPushed  = 0;
    inline bool g_initialized        = false;

    //-------------------------------------------------------------------------
    //  Forward declarations for logging functions
    //-------------------------------------------------------------------------
    inline void LogFirstFrameReport();
    inline void LogPeriodicSummary();

    //-------------------------------------------------------------------------
    //  Init — Prepare diagnostics (call after ENBInterface::Init)
    //-------------------------------------------------------------------------

    inline bool Init()
    {
        if (!ENBInterface::SetParameter) {
            SKSE::log::error("SB::Diag: ENBSetParameter not resolved");
            return false;
        }

        // Reset all stats
        for (auto& shader : g_shaderStats) {
            shader.totalSuccess = 0;
            shader.totalFail = 0;
            for (auto& p : shader.params) {
                p = ParamStatus{};
            }
        }
        g_firstFrameLogged = false;
        g_totalFramesPushed = 0;
        g_initialized = true;

        int shaderCount = static_cast<int>(std::size(SB::kTargetShaders));
        SKSE::log::info("SB::Diag: Initialized with {} params x {} shaders = {} calls/frame",
            SB::kParamCount, shaderCount, SB::kParamCount * shaderCount);
        return true;
    }

    //-------------------------------------------------------------------------
    //  PushAllParams — Push every parameter with return value tracking
    //-------------------------------------------------------------------------

    inline void PushAllParams(const SB::AllData& a_data)
    {
        if (!g_initialized || !ENBInterface::SetParameter)
            return;

        const auto* rawData = reinterpret_cast<const char*>(&a_data);
        int shaderCount = static_cast<int>(std::size(SB::kTargetShaders));

        for (int s = 0; s < shaderCount; s++)
        {
            auto& ss = g_shaderStats[s];

            for (std::size_t p = 0; p < SB::kParamCount; p++)
            {
                const auto& entry = SB::kParamTable[p];
                void* valuePtr = const_cast<void*>(
                    static_cast<const void*>(rawData + entry.offset));

                int result = ENBInterface::SetParameter(
                    SB::kTargetShaders[s],
                    "",
                    entry.name,
                    valuePtr,
                    16  // sizeof(float) * 4
                );

                auto& ps = ss.params[p];
                if (result) {
                    ps.successCount++;
                    ps.lastResult = true;
                    ps.everSucceeded = true;
                    ss.totalSuccess++;
                } else {
                    ps.failCount++;
                    ps.lastResult = false;
                    ss.totalFail++;
                }
            }
        }

        g_totalFramesPushed++;
        g_framesSinceReport++;

        if (!g_firstFrameLogged && g_totalFramesPushed == 1) {
            LogFirstFrameReport();
            g_firstFrameLogged = true;
        }

        if (g_framesSinceReport >= g_reportInterval) {
            LogPeriodicSummary();
            g_framesSinceReport = 0;
        }
    }

    //-------------------------------------------------------------------------
    //  First-frame report
    //-------------------------------------------------------------------------

    inline void LogFirstFrameReport()
    {
        int shaderCount = static_cast<int>(std::size(SB::kTargetShaders));

        SKSE::log::info("======================================================");
        SKSE::log::info("  SkyrimBridge - First Frame Diagnostic Report");
        SKSE::log::info("======================================================");

        int totalOK = 0, totalFail = 0;

        for (int s = 0; s < shaderCount; s++)
        {
            auto& ss = g_shaderStats[s];
            int shaderOK = 0, shaderFail = 0;

            for (std::size_t p = 0; p < SB::kParamCount; p++) {
                if (ss.params[p].lastResult) shaderOK++;
                else shaderFail++;
            }

            SKSE::log::info("  {} - {} OK, {} FAILED (of {} params)",
                SB::kTargetShaders[s], shaderOK, shaderFail, SB::kParamCount);

            if (shaderFail > 0 && shaderFail < static_cast<int>(SB::kParamCount)) {
                for (std::size_t p = 0; p < SB::kParamCount; p++) {
                    if (!ss.params[p].lastResult) {
                        SKSE::log::warn("    FAIL: {} -> {} (dead-stripped?)",
                            SB::kTargetShaders[s], SB::kParamTable[p].name);
                    }
                }
            } else if (shaderFail == static_cast<int>(SB::kParamCount)) {
                SKSE::log::error("    ALL PARAMS FAILED - shader likely missing "
                    "#include \"Helper/SkyrimBridge.fxh\" or SB_Retain() call");
            }

            totalOK += shaderOK;
            totalFail += shaderFail;
        }

        float pct = (totalOK + totalFail > 0)
            ? (100.0f * totalOK / (totalOK + totalFail)) : 0.0f;

        SKSE::log::info("  --------------------------------------------------");
        SKSE::log::info("  TOTAL: {} OK / {} FAIL ({:.1f}% success rate)",
            totalOK, totalFail, pct);

        if (totalFail > 0 && totalOK > 0) {
            SKSE::log::info("  TIP: Failed params are likely dead-stripped by HLSL compiler.");
            SKSE::log::info("       Add SB_Retain(uv) call to pixel shaders.");
        } else if (totalOK == 0) {
            SKSE::log::error("  CRITICAL: Zero parameters bound! Check:");
            SKSE::log::error("    1. Is SkyrimBridge.fxh included in the .fx files?");
            SKSE::log::error("    2. Are shader filenames correct? (case-sensitive)");
            SKSE::log::error("    3. Is ENB loading and compiling the shaders?");
            SKSE::log::error("    4. Is SB_Retain(uv) called in at least one PS?");
        }
    }

    //-------------------------------------------------------------------------
    //  Periodic summary
    //-------------------------------------------------------------------------

    inline void LogPeriodicSummary()
    {
        int shaderCount = static_cast<int>(std::size(SB::kTargetShaders));

        SKSE::log::info("SB Diag [frame {}]: {} calls/frame", g_totalFramesPushed,
            SB::kParamCount * shaderCount);

        for (int s = 0; s < shaderCount; s++)
        {
            auto& ss = g_shaderStats[s];
            int live = 0;
            for (std::size_t p = 0; p < SB::kParamCount; p++) {
                if (ss.params[p].lastResult) live++;
            }
            SKSE::log::info("  {} - {}/{} params live",
                SB::kTargetShaders[s], live, SB::kParamCount);
        }
    }

    //-------------------------------------------------------------------------
    //  Runtime query API
    //-------------------------------------------------------------------------

    inline bool IsParamLive(int shaderIdx, std::size_t paramIdx)
    {
        int shaderCount = static_cast<int>(std::size(SB::kTargetShaders));
        if (shaderIdx < 0 || shaderIdx >= shaderCount) return false;
        if (paramIdx >= SB::kParamCount) return false;
        return g_shaderStats[shaderIdx].params[paramIdx].everSucceeded;
    }

    inline float GetSuccessRate(int shaderIdx)
    {
        int shaderCount = static_cast<int>(std::size(SB::kTargetShaders));
        if (shaderIdx < 0 || shaderIdx >= shaderCount) return 0.0f;
        auto& ss = g_shaderStats[shaderIdx];
        int total = ss.totalSuccess + ss.totalFail;
        return total > 0 ? static_cast<float>(ss.totalSuccess) / total : 0.0f;
    }

} // namespace SB::Diag
