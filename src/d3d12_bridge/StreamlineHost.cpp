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

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

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
    PFun_slSetConstants* setConstants = nullptr;
    PFun_slSetTagForFrame* setTagForFrame = nullptr;
    PFun_slEvaluateFeature* evaluateFeature = nullptr;
    PFun_slDLSSSetOptions* dlssSetOptions = nullptr;
    PFun_slDLSSDSetOptions* dlssdSetOptions = nullptr;
    PFun_slDeepDVCSetOptions* deepDvcSetOptions = nullptr;
    PFun_slReflexSetOptions* reflexSetOptions = nullptr;
    PFun_slReflexSleep* reflexSleep = nullptr;
    ID3D12Device* dev12 = nullptr;
    ComPtr<ID3D12CommandAllocator> alloc[3];
    ComPtr<ID3D12GraphicsCommandList> cl;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEv = nullptr;
    uint64_t fenceVal = 0;
    uint64_t slotVal[3] = {};
    uint32_t optW = 0, optH = 0;
    int optMode = 0;
    bool dlssFailed = false;
    bool dlssdFailed = false;
    bool deepDvcFailed = false;
    bool dlssLogged = false;
    bool dlssdLogged = false;
    bool deepDvcLogged = false;
    bool ready = false;
};
SlApi& Api() { static SlApi a; return a; }

D3D12_RESOURCE_BARRIER Barrier(ID3D12Resource* r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    return b;
}
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
    a.setConstants = reinterpret_cast<PFun_slSetConstants*>(GetProcAddress(a.dll, "slSetConstants"));
    a.setTagForFrame = reinterpret_cast<PFun_slSetTagForFrame*>(GetProcAddress(a.dll, "slSetTagForFrame"));
    a.evaluateFeature = reinterpret_cast<PFun_slEvaluateFeature*>(GetProcAddress(a.dll, "slEvaluateFeature"));
    if (!a.init || !a.setD3DDevice || !a.isSupported || !a.getNewFrameToken || !a.getFeatureFn ||
        !a.setConstants || !a.setTagForFrame || !a.evaluateFeature) {
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
    a.dev12 = dev12;
    a.ready = true;

    if (m_caps.dlss || m_caps.dlssRR) {
        if (m_caps.dlss) {
            void* fnDlssOpt = nullptr;
            a.getFeatureFn(sl::kFeatureDLSS, "slDLSSSetOptions", fnDlssOpt);
            a.dlssSetOptions = reinterpret_cast<PFun_slDLSSSetOptions*>(fnDlssOpt);
        }
        if (m_caps.dlssRR) {
            void* fnRrOpt = nullptr;
            a.getFeatureFn(sl::kFeatureDLSS_RR, "slDLSSDSetOptions", fnRrOpt);
            a.dlssdSetOptions = reinterpret_cast<PFun_slDLSSDSetOptions*>(fnRrOpt);
        }
        if (m_caps.deepDvc) {
            void* fnDvc = nullptr;
            a.getFeatureFn(sl::kFeatureDeepDVC, "slDeepDVCSetOptions", fnDvc);
            a.deepDvcSetOptions = reinterpret_cast<PFun_slDeepDVCSetOptions*>(fnDvc);
        }
        bool cmdOk = SUCCEEDED(dev12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&a.fence)));
        for (int k = 0; cmdOk && k < 3; ++k)
            cmdOk = SUCCEEDED(dev12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&a.alloc[k])));
        if (cmdOk) cmdOk = SUCCEEDED(dev12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, a.alloc[0].Get(), nullptr, IID_PPV_ARGS(&a.cl)));
        if (cmdOk) { a.cl->Close(); a.fenceEv = CreateEventW(nullptr, FALSE, FALSE, nullptr); }
        else { a.dlssFailed = true; a.dlssdFailed = true; }
    }

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
    if (!a.ready || !m_reflexOn || !m_caps.reflex || !a.reflexSleep || !a.getNewFrameToken) return;
    sl::FrameToken* tok = nullptr;
    if (a.getNewFrameToken(tok, &frameIdx) == sl::Result::eOk && tok) {
        a.reflexSleep(*tok);
    }
#else
    (void)frameIdx;
#endif
}

bool StreamlineHost::Evaluate(ID3D12CommandQueue* queue, ID3D12Resource* colorIn, ID3D12Resource* colorOut,
                              ID3D12Resource* depth, ID3D12Resource* mvec,
                              ID3D12Resource* albedo, ID3D12Resource* normal, ID3D12Resource* spec,
                              uint32_t width, uint32_t height, uint32_t frameIdx, const EvalCamera& cam, LogFn log)
{
#ifndef RAW_HAS_STREAMLINE
    (void)queue; (void)colorIn; (void)colorOut; (void)depth; (void)mvec; (void)albedo; (void)normal; (void)spec;
    (void)width; (void)height; (void)frameIdx; (void)cam; (void)log;
    return false;
#else
    auto& a = Api();
    if (!a.ready || m_mode <= 0 || !a.cl || !queue || !colorIn || !colorOut || !depth || !mvec)
        return false;

    // Skyrim SE's m_normalIn is R8G8B8A8_UNORM stereographic octahedral view-space normals, NOT linear unpacked
    // world-space XYZ normals (which sl::DLSSDNormalRoughnessMode::eUnpacked requires). Feeding octahedral normals
    // to DLSS-RR causes surface-edge shimmering during movement & cursor placement. Route mode 2 cleanly to
    // DLAA Transformer Preset K (which uses exact colorIn + depth + mvec with zero normal-format mismatch)
    // until a dedicated linear world-space normal+roughness unpack pass is wired for DLSS-RR.
    const bool useRR = false;
    (void)albedo; (void)normal; (void)spec;
    if (!m_caps.dlss || a.dlssFailed || !a.dlssSetOptions) return false;

    const sl::ViewportHandle vp(0);
    const int activeMode = useRR ? 2 : m_mode;
    if (a.optW != width || a.optH != height || a.optMode != activeMode) {
        if (useRR) {
            sl::DLSSDOptions opt{};
            opt.mode = sl::DLSSMode::eDLAA;
            opt.outputWidth = width;
            opt.outputHeight = height;
            opt.colorBuffersHDR = sl::Boolean::eTrue;
            opt.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::eUnpacked;
            opt.dlaaPreset = sl::DLSSDPreset::ePresetF;
            opt.qualityPreset = sl::DLSSDPreset::ePresetF;
            const sl::Result r = a.dlssdSetOptions(vp, opt);
            if (r != sl::Result::eOk) {
                a.dlssdFailed = true;
                if (log) log("[Streamline] slDLSSDSetOptions(PresetF) failed: %d (falling back to DLAA)", static_cast<int>(r));
                return false;
            }
        } else {
            sl::DLSSOptions opt{};
            opt.mode = (m_mode == 3) ? sl::DLSSMode::eMaxQuality
                     : (m_mode == 4) ? sl::DLSSMode::eBalanced
                     : (m_mode == 5) ? sl::DLSSMode::eMaxPerformance
                                     : sl::DLSSMode::eDLAA;
            opt.outputWidth = width;
            opt.outputHeight = height;
            opt.colorBuffersHDR = sl::Boolean::eTrue;
            opt.dlaaPreset = sl::DLSSPreset::ePresetK;
            opt.qualityPreset = sl::DLSSPreset::ePresetK;
            opt.balancedPreset = sl::DLSSPreset::ePresetK;
            opt.performancePreset = sl::DLSSPreset::ePresetM;
            const sl::Result r = a.dlssSetOptions(vp, opt);
            if (r != sl::Result::eOk) {
                a.dlssFailed = true;
                if (log) log("[Streamline] slDLSSSetOptions(mode %d) failed: %d", m_mode, static_cast<int>(r));
                return false;
            }
        }
        a.optW = width; a.optH = height; a.optMode = activeMode;
    }

    sl::FrameToken* tok = nullptr;
    if (a.getNewFrameToken(tok, &frameIdx) != sl::Result::eOk || !tok) return false;

    sl::Constants c{};
    c.jitterOffset = {0.0f, 0.0f};
    c.mvecScale = {1.0f, 1.0f};
    c.cameraAspectRatio = cam.valid && cam.aspect > 0.1f ? cam.aspect : (height ? float(width) / float(height) : 16.0f / 9.0f);
    c.cameraNear = cam.valid && cam.nearZ > 0.1f ? cam.nearZ : 15.0f;
    c.cameraFar = cam.valid && cam.farZ > c.cameraNear ? cam.farZ : 400000.0f;
    c.cameraFOV = cam.valid && cam.fovY > 0.1f ? cam.fovY : 1.134464f;
    c.depthInverted = sl::Boolean::eFalse;
    c.cameraMotionIncluded = sl::Boolean::eTrue;
    c.motionVectors3D = sl::Boolean::eFalse;
    c.motionVectorsJittered = sl::Boolean::eFalse;
    static float s_prevPos[3] = {};
    static bool s_hasPrevPos = false;
    bool cameraCut = (frameIdx <= 2u);
    if (cam.valid) {
        c.cameraPos = {cam.pos[0], cam.pos[1], cam.pos[2]};
        c.cameraRight = {cam.viewToWorld[0], cam.viewToWorld[4], cam.viewToWorld[8]};
        c.cameraUp    = {cam.viewToWorld[1], cam.viewToWorld[5], cam.viewToWorld[9]};
        c.cameraFwd   = {cam.viewToWorld[2], cam.viewToWorld[6], cam.viewToWorld[10]};
        for (int r = 0; r < 4; ++r) {
            c.cameraViewToClip[r] = {cam.proj[r * 4 + 0], cam.proj[r * 4 + 1], cam.proj[r * 4 + 2], cam.proj[r * 4 + 3]};
        }
        if (s_hasPrevPos) {
            const float dx = cam.pos[0] - s_prevPos[0];
            const float dy = cam.pos[1] - s_prevPos[1];
            const float dz = cam.pos[2] - s_prevPos[2];
            if (dx * dx + dy * dy + dz * dz > 500.0f * 500.0f) cameraCut = true;
        }
        s_prevPos[0] = cam.pos[0]; s_prevPos[1] = cam.pos[1]; s_prevPos[2] = cam.pos[2];
        s_hasPrevPos = true;
    }
    c.reset = cameraCut ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    a.setConstants(c, *tok, vp);

    const uint32_t slot = frameIdx % 3u;
    if (a.slotVal[slot] && a.fence->GetCompletedValue() < a.slotVal[slot]) {
        a.fence->SetEventOnCompletion(a.slotVal[slot], a.fenceEv);
        WaitForSingleObject(a.fenceEv, 100);
    }
    if (FAILED(a.alloc[slot]->Reset()) || FAILED(a.cl->Reset(a.alloc[slot].Get(), nullptr))) return false;

    D3D12_RESOURCE_BARRIER inB[7];
    UINT nb = 0;
    inB[nb++] = Barrier(colorIn,  D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    inB[nb++] = Barrier(colorOut, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    inB[nb++] = Barrier(depth,    D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    inB[nb++] = Barrier(mvec,     D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (useRR) {
        inB[nb++] = Barrier(albedo, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        inB[nb++] = Barrier(normal, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        inB[nb++] = Barrier(spec,   D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    a.cl->ResourceBarrier(nb, inB);

    const sl::Extent ext{0, 0, width, height};
    sl::Resource rIn(sl::ResourceType::eTex2d, colorIn, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Resource rOut(sl::ResourceType::eTex2d, colorOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    sl::Resource rDepth(sl::ResourceType::eTex2d, depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Resource rMvec(sl::ResourceType::eTex2d, mvec, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Resource rAlb(sl::ResourceType::eTex2d, albedo, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Resource rNrm(sl::ResourceType::eTex2d, normal, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Resource rSpec(sl::ResourceType::eTex2d, spec, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::ResourceTag tags[8] = {
        {&rIn,    sl::kBufferTypeScalingInputColor,  sl::ResourceLifecycle::eValidUntilEvaluate, &ext},
        {&rOut,   sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &ext},
        {&rDepth, sl::kBufferTypeDepth,              sl::ResourceLifecycle::eValidUntilEvaluate, &ext},
        {&rMvec,  sl::kBufferTypeMotionVectors,      sl::ResourceLifecycle::eValidUntilEvaluate, &ext},
        {&rAlb,   sl::kBufferTypeAlbedo,             sl::ResourceLifecycle::eValidUntilEvaluate, &ext},
        {&rSpec,  sl::kBufferTypeSpecularAlbedo,     sl::ResourceLifecycle::eValidUntilEvaluate, &ext},
        {&rNrm,   sl::kBufferTypeNormals,            sl::ResourceLifecycle::eValidUntilEvaluate, &ext},
        {&rSpec,  sl::kBufferTypeRoughness,          sl::ResourceLifecycle::eValidUntilEvaluate, &ext}
    };
    a.setTagForFrame(*tok, vp, tags, useRR ? 8u : 4u, a.cl.Get());

    const sl::BaseStructure* inputs[] = {&vp};
    const sl::Feature feat = useRR ? sl::kFeatureDLSS_RR : sl::kFeatureDLSS;
    const sl::Result er = a.evaluateFeature(feat, *tok, inputs, 1, a.cl.Get());

    // Watermarked sl.deepdvc.dll evaluation is bypassed; m_deepDvcOn now drives RAW's own
    // 100% watermark-free GPU-native Oklab Neural Vibrance & Local Contrast pass in Tonemap.hlsl.
    (void)m_deepDvcOn;

    D3D12_RESOURCE_BARRIER outB[7];
    for (UINT i = 0; i < nb; ++i) {
        outB[i] = inB[i];
        std::swap(outB[i].Transition.StateBefore, outB[i].Transition.StateAfter);
    }
    a.cl->ResourceBarrier(nb, outB);
    a.cl->Close();

    if (er != sl::Result::eOk) {
        if (useRR) a.dlssdFailed = true; else a.dlssFailed = true;
        if (log) log("[Streamline] slEvaluateFeature(%s) failed: %d", useRR ? "DLSS-RR" : "DLAA", static_cast<int>(er));
        return false;
    }

    ID3D12CommandList* lists[] = {a.cl.Get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(a.fence.Get(), ++a.fenceVal);
    a.slotVal[slot] = a.fenceVal;
    if (useRR && !a.dlssdLogged) {
        a.dlssdLogged = true;
        if (log) log("[Streamline] NVIDIA DLSS Ray Reconstruction (Preset F) active live at %ux%u HDR16F", width, height);
    } else if (!useRR && !a.dlssLogged) {
        a.dlssLogged = true;
        if (log) log("[Streamline] NVIDIA DLAA Transformer (Preset K) active live at %ux%u HDR16F", width, height);
    }
    return true;
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
