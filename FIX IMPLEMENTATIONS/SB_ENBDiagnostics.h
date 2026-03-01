#pragma once
//═════════════════════════════════════════════════════════════════════════════
//  SB_ENBDiagnostics.h — ENBSetParameter Validation & Diagnostics
//
//  Wraps ENBSetParameter calls with return-value checking, per-parameter
//  success tracking, and diagnostic logging to identify dead-stripped
//  shader variables that silently reject data.
//
//  Usage:
//    1. Include this instead of calling ENBSetParameter directly
//    2. Call SB::Diag::PushAllParams() in the ENB frame callback
//    3. Call SB::Diag::LogReport() periodically (e.g., every 300 frames)
//
//  The diagnostic system will identify:
//    - Parameters successfully bound (data flowing to shaders)
//    - Parameters rejected by ENB (dead-stripped from compiled Effect)
//    - Parameters with mismatched names (typos, case sensitivity)
//    - Per-shader success rates
//
//  Author: Zain Dana Harper
//  Version: 2.0.0
//═════════════════════════════════════════════════════════════════════════════

#include <Windows.h>
#include <array>
#include <string>
#include <cstring>
#include <SKSE/SKSE.h>

namespace SB::Diag
{
    //─────────────────────────────────────────────────────────────────────────
    //  Configuration
    //─────────────────────────────────────────────────────────────────────────

    // Target shader files that include SkyrimBridge.fxh
    // Only non-obfuscated shaders that have the #include
    inline constexpr const char* kTargetShaders[] = {
        "enbeffectprepass.fx",
        "enbeffect.fx",
        "enblens.fx",
        "enbdepthoffield.fx",
        "enbsunsprite.fx",
        "enbbloom.fx",
        "enbadaptation.fx",
    };
    inline constexpr int kShaderCount = sizeof(kTargetShaders) / sizeof(kTargetShaders[0]);

    // ENB parameter category for SkyrimBridge data
    // Empty string = no category filter (matches any annotated or bare global)
    inline constexpr const char* kCategory = "";

    //─────────────────────────────────────────────────────────────────────────
    //  Parameter table entry — maps struct offset to HLSL variable name
    //─────────────────────────────────────────────────────────────────────────

    struct ParamEntry
    {
        size_t      offset;     // Byte offset into AllData struct
        const char* hlslName;   // Exact name as declared in SkyrimBridge.fxh
    };

    // Forward-declare — defined in the .cpp that has the AllData struct
    // Each entry is: { offsetof(AllData, field), "SB_FieldName" }
    extern const ParamEntry kParamTable[];
    extern const size_t     kParamCount;

    //─────────────────────────────────────────────────────────────────────────
    //  Per-parameter tracking state
    //─────────────────────────────────────────────────────────────────────────

    struct ParamStatus
    {
        int  successCount  = 0;   // Successful ENBSetParameter calls
        int  failCount     = 0;   // Failed ENBSetParameter calls
        bool everSucceeded = false;
        bool lastResult    = false;
    };

    struct ShaderStatus
    {
        int totalSuccess = 0;
        int totalFail    = 0;
        std::array<ParamStatus, 128> params;  // Max 128 params per shader
    };

    inline std::array<ShaderStatus, 8> g_shaderStats;  // Per-shader tracking
    inline int  g_framesSinceReport  = 0;
    inline int  g_reportInterval     = 300;  // Log every N frames
    inline bool g_firstFrameLogged   = false;
    inline int  g_totalFramesPushed  = 0;

    //─────────────────────────────────────────────────────────────────────────
    //  ENB SDK function pointer (set during Init)
    //─────────────────────────────────────────────────────────────────────────

    using FnENBSetParameter = BOOL(WINAPI*)(
        const char* filename,
        const char* category,
        const char* keyname,
        void*       value,
        int         size
    );

    inline FnENBSetParameter g_enbSetParam = nullptr;

    //─────────────────────────────────────────────────────────────────────────
    //  Init — Resolve ENB SDK and prepare diagnostics
    //─────────────────────────────────────────────────────────────────────────

    inline bool Init(FnENBSetParameter setParamFn)
    {
        g_enbSetParam = setParamFn;
        if (!g_enbSetParam) {
            SKSE::log::error("SB::Diag: ENBSetParameter function pointer is null");
            return false;
        }

        // Reset all stats
        for (auto& shader : g_shaderStats) {
            shader.totalSuccess = 0;
            shader.totalFail = 0;
            for (auto& p : shader.params) {
                p.successCount = 0;
                p.failCount = 0;
                p.everSucceeded = false;
                p.lastResult = false;
            }
        }
        g_firstFrameLogged = false;
        g_totalFramesPushed = 0;

        SKSE::log::info("SB::Diag: Initialized with {} params × {} shaders = {} calls/frame",
            kParamCount, kShaderCount, kParamCount * kShaderCount);
        return true;
    }

    //─────────────────────────────────────────────────────────────────────────
    //  PushAllParams — Push every parameter to every target shader with
    //                  return value tracking
    //─────────────────────────────────────────────────────────────────────────

    inline void PushAllParams(const void* dataBasePtr)
    {
        if (!g_enbSetParam || !dataBasePtr) return;

        for (int s = 0; s < kShaderCount; s++)
        {
            auto& ss = g_shaderStats[s];

            for (size_t p = 0; p < kParamCount; p++)
            {
                const auto& entry = kParamTable[p];

                // Pointer to this parameter's float4 data
                void* valuePtr = (void*)((const uint8_t*)dataBasePtr + entry.offset);

                BOOL result = g_enbSetParam(
                    kTargetShaders[s],
                    kCategory,
                    entry.hlslName,
                    valuePtr,
                    sizeof(float) * 4  // Always push float4 (16 bytes)
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

        // First-frame detailed report
        if (!g_firstFrameLogged && g_totalFramesPushed == 1) {
            LogFirstFrameReport();
            g_firstFrameLogged = true;
        }

        // Periodic summary
        if (g_framesSinceReport >= g_reportInterval) {
            LogPeriodicSummary();
            g_framesSinceReport = 0;
        }
    }

    //─────────────────────────────────────────────────────────────────────────
    //  First-frame report — detailed per-parameter per-shader success/fail
    //─────────────────────────────────────────────────────────────────────────

    inline void LogFirstFrameReport()
    {
        SKSE::log::info("╔═══════════════════════════════════════════════════╗");
        SKSE::log::info("║  SkyrimBridge — First Frame Diagnostic Report    ║");
        SKSE::log::info("╚═══════════════════════════════════════════════════╝");

        int totalOK = 0, totalFail = 0;

        for (int s = 0; s < kShaderCount; s++)
        {
            auto& ss = g_shaderStats[s];
            int shaderOK = 0, shaderFail = 0;

            for (size_t p = 0; p < kParamCount; p++) {
                if (ss.params[p].lastResult) shaderOK++;
                else shaderFail++;
            }

            SKSE::log::info("  {} — {} OK, {} FAILED (of {} params)",
                kTargetShaders[s], shaderOK, shaderFail, kParamCount);

            // Log individual failures for debugging
            if (shaderFail > 0 && shaderFail < (int)kParamCount) {
                // Mixed results — log the specific failures
                for (size_t p = 0; p < kParamCount; p++) {
                    if (!ss.params[p].lastResult) {
                        SKSE::log::warn("    FAIL: {} → {} (dead-stripped?)",
                            kTargetShaders[s], kParamTable[p].hlslName);
                    }
                }
            } else if (shaderFail == (int)kParamCount) {
                // ALL failed — shader probably doesn't include SkyrimBridge.fxh
                SKSE::log::error("    ALL PARAMS FAILED — shader likely missing "
                    "#include \"Helper/SkyrimBridge.fxh\" or is obfuscated");
            }

            totalOK += shaderOK;
            totalFail += shaderFail;
        }

        float pct = (totalOK + totalFail > 0)
            ? (100.0f * totalOK / (totalOK + totalFail)) : 0.0f;

        SKSE::log::info("  ────────────────────────────────────────────");
        SKSE::log::info("  TOTAL: {} OK / {} FAIL ({:.1f}% success rate)",
            totalOK, totalFail, pct);

        if (totalFail > 0 && totalOK > 0) {
            SKSE::log::info("  TIP: Failed params are likely dead-stripped by HLSL compiler.");
            SKSE::log::info("       Add SB_Retain(uv) call to pixel shaders or reference");
            SKSE::log::info("       the missing parameters in shader code.");
        } else if (totalOK == 0) {
            SKSE::log::error("  CRITICAL: Zero parameters bound! Check:");
            SKSE::log::error("    1. Is SkyrimBridge.fxh included in the .fx files?");
            SKSE::log::error("    2. Are shader filenames correct? (case-sensitive)");
            SKSE::log::error("    3. Is ENB loading and compiling the shaders?");
            SKSE::log::error("    4. Is the ENB SDK version compatible? (check GetSDKVersion)");
        }
    }

    //─────────────────────────────────────────────────────────────────────────
    //  Periodic summary — brief per-shader stats
    //─────────────────────────────────────────────────────────────────────────

    inline void LogPeriodicSummary()
    {
        SKSE::log::info("SB Diag [frame {}]: {} calls/frame", g_totalFramesPushed,
            kParamCount * kShaderCount);

        for (int s = 0; s < kShaderCount; s++)
        {
            auto& ss = g_shaderStats[s];
            // Count params that succeeded on most recent frame
            int live = 0;
            for (size_t p = 0; p < kParamCount; p++) {
                if (ss.params[p].lastResult) live++;
            }
            SKSE::log::info("  {} — {}/{} params live",
                kTargetShaders[s], live, kParamCount);
        }
    }

    //─────────────────────────────────────────────────────────────────────────
    //  GetParamStatus — Query whether a specific parameter is reaching
    //                   a specific shader (for runtime UI display)
    //─────────────────────────────────────────────────────────────────────────

    inline bool IsParamLive(int shaderIdx, size_t paramIdx)
    {
        if (shaderIdx < 0 || shaderIdx >= kShaderCount) return false;
        if (paramIdx >= kParamCount) return false;
        return g_shaderStats[shaderIdx].params[paramIdx].everSucceeded;
    }

    inline float GetSuccessRate(int shaderIdx)
    {
        if (shaderIdx < 0 || shaderIdx >= kShaderCount) return 0.0f;
        auto& ss = g_shaderStats[shaderIdx];
        int total = ss.totalSuccess + ss.totalFail;
        return total > 0 ? (float)ss.totalSuccess / total : 0.0f;
    }

} // namespace SB::Diag
