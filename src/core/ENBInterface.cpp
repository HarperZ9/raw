#include "ENBInterface.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <cstring>

// We need to access Windows API but CommonLibSSE-NG wraps it.
// Use forward declarations with extern "C" to call the raw API.
extern "C" {
    __declspec(dllimport) void* __stdcall GetModuleHandleW(const wchar_t*);
    __declspec(dllimport) void* __stdcall GetProcAddress(void*, const char*);
}

namespace ENBInterface
{
    static bool g_loaded = false;
    static bool g_guiSupported = false;

    bool Init()
    {
        // ENB for Skyrim SE ships as d3d11.dll (DX11 wrapper).
        // Some setups use a proxy chain where the actual ENB is enbseries.dll.
        void* enbModule = GetModuleHandleW(L"d3d11.dll");
        if (!enbModule) {
            enbModule = GetModuleHandleW(L"d3d11_enb.dll");
        }
        if (!enbModule) {
            SKSE::log::info("SkyrimBridge: d3d11.dll not found (ENB not installed?)");
            return false;
        }

        // Check if this d3d11.dll is actually ENB (it exports ENBGetSDKVersion)
        GetSDKVersion = reinterpret_cast<_ENBGetSDKVersion>(
            GetProcAddress(enbModule, "ENBGetSDKVersion"));

        if (!GetSDKVersion) {
            SKSE::log::info("SkyrimBridge: d3d11.dll is not ENBSeries (no SDK exports)");
            return false;
        }

        long sdkVer = GetSDKVersion();
        SKSE::log::info("SkyrimBridge: ENB SDK version {}", sdkVer);

        if (sdkVer < 1000) {
            SKSE::log::warn("SkyrimBridge: ENB SDK version {} too old (need >= 1000)", sdkVer);
            return false;
        }

        // Resolve core functions
        GetVersion = reinterpret_cast<_ENBGetVersion>(
            GetProcAddress(enbModule, "ENBGetVersion"));

        SetCallbackFunction = reinterpret_cast<_ENBSetCallbackFunction>(
            GetProcAddress(enbModule, "ENBSetCallbackFunction"));

        GetParameter = reinterpret_cast<_ENBGetParameter>(
            GetProcAddress(enbModule, "ENBGetParameter"));

        SetParameter = reinterpret_cast<_ENBSetParameter>(
            GetProcAddress(enbModule, "ENBSetParameter"));

        // Resolve optional GUI functions (may not be available in all ENB versions)
        IsEditorActive = reinterpret_cast<_ENBIsEditorActive>(
            GetProcAddress(enbModule, "ENBIsEditorActive"));

        SetGUICallback = reinterpret_cast<_ENBSetGUICallback>(
            GetProcAddress(enbModule, "ENBSetGUICallback"));

        GetD3D11Device = reinterpret_cast<_ENBGetD3D11Device>(
            GetProcAddress(enbModule, "ENBGetD3D11Device"));

        if (!SetCallbackFunction || !SetParameter) {
            SKSE::log::error("SkyrimBridge: Failed to resolve ENB SDK functions");
            return false;
        }

        if (GetVersion) {
            SKSE::log::info("SkyrimBridge: ENBSeries binary version {}", GetVersion());
        }

        // Check GUI support
        g_guiSupported = (IsEditorActive != nullptr);
        SKSE::log::info("SkyrimBridge: GUI support: {}", g_guiSupported ? "yes" : "no");

        g_loaded = true;
        return true;
    }

    bool IsGUISupported()
    {
        return g_guiSupported;
    }

    bool IsLoaded()
    {
        return g_loaded;
    }

    void PushAllData(const SB::AllData& a_data)
    {
        if (!SetParameter)
            return;

        static bool s_firstPush = true;
        static std::size_t s_pushCount = 0;

        // Get pointer to raw bytes of AllData
        const auto* rawData = reinterpret_cast<const char*>(&a_data);

        int totalSuccess = 0;
        int totalFail = 0;

        // Iterate through all parameters and push to all target shaders
        for (std::size_t i = 0; i < SB::kParamCount; ++i) {
            const auto& entry = SB::kParamTable[i];
            // Pointer to this parameter's float4 data (16 bytes)
            void* paramData = const_cast<void*>(
                static_cast<const void*>(rawData + entry.offset));

            // Push to each target shader — every SB param is a float4 (16 bytes)
            for (const auto* shader : SB::kTargetShaders) {
                int result = SetParameter(
                    shader,
                    "",             // empty category
                    entry.name,
                    paramData,
                    16              // sizeof(float) * 4
                );

                if (s_firstPush) {
                    if (result)
                        ++totalSuccess;
                    else
                        ++totalFail;
                }
            }
        }

        if (s_firstPush) {
            // Log the sentinel value to confirm data is populated
            SKSE::log::info("SkyrimBridge: first push — SB_Render_Frame.x = {:.1f}",
                a_data.render.FrameInfo.x);
            SKSE::log::info("SkyrimBridge: first push — {} succeeded, {} failed out of {} total calls",
                totalSuccess, totalFail,
                SB::kParamCount * std::size(SB::kTargetShaders));

            // Log per-shader breakdown on first push
            if (totalFail > 0) {
                for (const auto* shader : SB::kTargetShaders) {
                    int shaderOk = 0, shaderFail = 0;
                    for (std::size_t i = 0; i < SB::kParamCount; ++i) {
                        const auto& entry = SB::kParamTable[i];
                        void* pd = const_cast<void*>(
                            static_cast<const void*>(rawData + entry.offset));

                        int r = SetParameter(shader, "", entry.name, pd, 16);
                        if (r) ++shaderOk; else ++shaderFail;
                    }
                    SKSE::log::info("  {} — {} ok, {} fail", shader, shaderOk, shaderFail);
                }
            }

            s_firstPush = false;
        }

        ++s_pushCount;
    }
}
