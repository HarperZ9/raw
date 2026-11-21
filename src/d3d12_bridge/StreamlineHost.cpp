// StreamlineHost.cpp: see StreamlineHost.h. Original work, MIT.
#include "StreamlineHost.h"

#include <windows.h>
#include <filesystem>
#include <string>

#ifdef RAW_HAS_STREAMLINE
#include <sl.h>
#include <sl_core_api.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_reflex.h>
#include <sl_deepdvc.h>
#endif

namespace raw::bridge
{
namespace
{
#ifdef RAW_HAS_STREAMLINE
struct SlApi
{
    HMODULE dll = nullptr;
    PFun_slInit* init = nullptr;
    PFun_slShutdown* shutdown = nullptr;
    PFun_slSetD3DDevice* setD3DDevice = nullptr;
    PFun_slIsFeatureSupported* isSupported = nullptr;
    PFun_slGetNewFrameToken* getNewFrameToken = nullptr;
    PFun_slGetFeatureFunction* getFeatureFn = nullptr;
    PFun_slReflexSetOptions* reflexSetOptions = nullptr;
    PFun_slReflexSleep* reflexSleep = nullptr;
    bool ready = false;
};
SlApi& Api() { static SlApi a; return a; }
#endif
} // namespace

StreamlineHost& StreamlineHost::Get()
{
    static StreamlineHost inst;
    return inst;
}

bool StreamlineHost::Initialize(ID3D12Device* dev12, LUID adapterLuid, LogFn log)
{
    if (m_tried) return m_caps.available;
    m_tried = true;
#ifndef RAW_HAS_STREAMLINE
    (void)dev12; (void)adapterLuid; (void)log;
    return false;
#else
    if (!dev12) return false;
    auto& a = Api();
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::filesystem::path dir = std::filesystem::path(exe).parent_path();
    std::wstring dllPath = (dir / L"sl.interposer.dll").wstring();
    a.dll = LoadLibraryW(dllPath.c_str());
    if (!a.dll) a.dll = LoadLibraryW(L"sl.interposer.dll");
    if (!a.dll) {
        if (log) log("[Streamline] sl.interposer.dll not present beside SkyrimSE.exe (optional)");
        return false;
    }

    a.init = reinterpret_cast<PFun_slInit*>(GetProcAddress(a.dll, "slInit"));
    a.shutdown = reinterpret_cast<PFun_slShutdown*>(GetProcAddress(a.dll, "slShutdown"));
    a.setD3DDevice = reinterpret_cast<PFun_slSetD3DDevice*>(GetProcAddress(a.dll, "slSetD3DDevice"));
    a.isSupported = reinterpret_cast<PFun_slIsFeatureSupported*>(GetProcAddress(a.dll, "slIsFeatureSupported"));
    a.getNewFrameToken = reinterpret_cast<PFun_slGetNewFrameToken*>(GetProcAddress(a.dll, "slGetNewFrameToken"));
    a.getFeatureFn = reinterpret_cast<PFun_slGetFeatureFunction*>(GetProcAddress(a.dll, "slGetFeatureFunction"));
    if (!a.init || !a.setD3DDevice || !a.isSupported || !a.getNewFrameToken || !a.getFeatureFn) {
        if (log) log("[Streamline] sl.interposer.dll missing required exports");
        return false;
    }

    static const sl::Feature kFeats[] = {
        sl::kFeatureDLSS,
        sl::kFeatureDLSS_RR,
        sl::kFeatureReflex,
        sl::kFeatureDeepDVC
    };
    const std::wstring pluginDir = dir.wstring();
    const wchar_t* paths[] = {pluginDir.c_str()};

    sl::Preferences pref{};
    pref.showConsole = false;
    pref.logLevel = sl::LogLevel::eOff;
    pref.pathsToPlugins = paths;
    pref.numPathsToPlugins = 1;
    pref.featuresToLoad = kFeats;
    pref.numFeaturesToLoad = static_cast<uint32_t>(sizeof(kFeats) / sizeof(kFeats[0]));
    pref.flags = sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    pref.renderAPI = sl::RenderAPI::eD3D12;
    pref.engine = sl::EngineType::eCustom;
    pref.engineVersion = "RAW-DX12-SM69";
    pref.projectId = "a0f57b54-1daf-4934-90ae-c4035c19df04";

    sl::Result r = a.init(pref, sl::kSDKVersion);
    if (r != sl::Result::eOk) {
        if (log) log("[Streamline] slInit returned %d", static_cast<int>(r));
        return false;
    }

    r = a.setD3DDevice(dev12);
    if (r != sl::Result::eOk) {
        if (log) log("[Streamline] slSetD3DDevice returned %d", static_cast<int>(r));
        return false;
    }

    sl::AdapterInfo ai{};
    ai.deviceLUID = reinterpret_cast<uint8_t*>(&adapterLuid);
    ai.deviceLUIDSizeInBytes = sizeof(adapterLuid);

    m_caps.available = true;
    m_caps.dlss = (a.isSupported(sl::kFeatureDLSS, ai) == sl::Result::eOk);
    m_caps.dlssRR = (a.isSupported(sl::kFeatureDLSS_RR, ai) == sl::Result::eOk);
    m_caps.reflex = (a.isSupported(sl::kFeatureReflex, ai) == sl::Result::eOk);
    m_caps.deepDvc = (a.isSupported(sl::kFeatureDeepDVC, ai) == sl::Result::eOk);
    a.ready = true;

    if (m_caps.reflex) {
        void* fnOpt = nullptr;
        void* fnSleep = nullptr;
        a.getFeatureFn(sl::kFeatureReflex, "slReflexSetOptions", fnOpt);
        a.getFeatureFn(sl::kFeatureReflex, "slReflexSleep", fnSleep);
        a.reflexSetOptions = reinterpret_cast<PFun_slReflexSetOptions*>(fnOpt);
        a.reflexSleep = reinterpret_cast<PFun_slReflexSleep*>(fnSleep);
        if (a.reflexSetOptions) {
            sl::ReflexOptions ro{};
            ro.mode = sl::ReflexMode::eLowLatencyWithBoost;
            ro.frameLimitUs = 0;
            ro.useMarkersToOptimize = false;
            a.reflexSetOptions(ro);
        }
    }

    if (log) {
        log("[Streamline] NVIDIA Streamline v2.14 active on DX12: DLSS/DLAA=%s, RayReconstruction=%s, Reflex=%s, DeepDVC=%s",
            m_caps.dlss ? "ON" : "no", m_caps.dlssRR ? "ON" : "no",
            m_caps.reflex ? "ON (LowLatency+Boost)" : "no", m_caps.deepDvc ? "ON" : "no");
    }
    return true;
#endif
}

void StreamlineHost::ReflexBeginFrame(uint32_t frameIdx)
{
#ifdef RAW_HAS_STREAMLINE
    auto& a = Api();
    if (!a.ready || !m_caps.reflex || !a.reflexSleep || !a.getNewFrameToken) return;
    sl::FrameToken* tok = nullptr;
    if (a.getNewFrameToken(tok, &frameIdx) == sl::Result::eOk && tok) {
        a.reflexSleep(*tok);
    }
#else
    (void)frameIdx;
#endif
}

void StreamlineHost::Shutdown()
{
#ifdef RAW_HAS_STREAMLINE
    auto& a = Api();
    if (a.ready && a.shutdown) {
        a.shutdown();
        a.ready = false;
    }
#endif
    m_caps = {};
}

} // namespace raw::bridge
