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

#include "rt_normal_unpack.h"
#include "rt_hiz_downsample.h"
#include <cstring>
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
    ComPtr<ID3D12RootSignature> unpackRs;
    ComPtr<ID3D12PipelineState> unpackPso;
    ComPtr<ID3D12DescriptorHeap> unpackHeap;
    ComPtr<ID3D12Resource> normalRough;
    ComPtr<ID3D12RootSignature> hizRs;
    ComPtr<ID3D12PipelineState> hizPso;
    ComPtr<ID3D12DescriptorHeap> hizHeap;
    ComPtr<ID3D12Resource> hizGpu;
    ComPtr<ID3D12Resource> hizRead[3];
    uint32_t hizW = 0, hizH = 0;
    float hizW2C[3][16] = {};
    float hizCam[3][3] = {};
    bool hizValid[3] = {};
    uint32_t nrW = 0, nrH = 0;
    UINT descInc = 0;
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
        // Build SM 6.9 DX12 compute PSO for unpacking Skyrim SE's octahedral R8G8B8A8 normals + gloss
        // into a linear RGBA16F world-space normal + roughness buffer for DLSS Ray Reconstruction (ePacked).
        if (cmdOk && m_caps.dlssRR) {
            D3D12_DESCRIPTOR_RANGE ranges[2]{};
            ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
            ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 2};
            D3D12_ROOT_PARAMETER rp[2]{};
            rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            rp[0].Constants = {0, 0, 16};
            rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rp[1].DescriptorTable = {2, ranges};
            rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            D3D12_ROOT_SIGNATURE_DESC rsd{2, rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
            ComPtr<ID3DBlob> sigBlob, errBlob;
            if (SUCCEEDED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob)) &&
                SUCCEEDED(dev12->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&a.unpackRs)))) {
                D3D12_COMPUTE_PIPELINE_STATE_DESC cpd{};
                cpd.pRootSignature = a.unpackRs.Get();
                cpd.CS = {kDxil_normal_unpack, sizeof(kDxil_normal_unpack)};
                if (SUCCEEDED(dev12->CreateComputePipelineState(&cpd, IID_PPV_ARGS(&a.unpackPso)))) {
                    D3D12_DESCRIPTOR_HEAP_DESC hd{};
                    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                    hd.NumDescriptors = 3;
                    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                    if (SUCCEEDED(dev12->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&a.unpackHeap)))) {
                        a.descInc = dev12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                    }
                }
            }
        }
        // Build SM 6.9 Native DX12 Compute Hi-Z 8x8 MAX-depth reduction PSO (1920x1080 -> 240x135, 129 KB).
        if (cmdOk) {
            D3D12_DESCRIPTOR_RANGE hranges[2]{};
            hranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
            hranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 1};
            D3D12_ROOT_PARAMETER hrp[2]{};
            hrp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            hrp[0].Constants = {0, 0, 4};
            hrp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            hrp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            hrp[1].DescriptorTable = {2, hranges};
            hrp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            D3D12_ROOT_SIGNATURE_DESC hrsd{2, hrp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
            ComPtr<ID3DBlob> hsig, herr;
            if (SUCCEEDED(D3D12SerializeRootSignature(&hrsd, D3D_ROOT_SIGNATURE_VERSION_1, &hsig, &herr)) &&
                SUCCEEDED(dev12->CreateRootSignature(0, hsig->GetBufferPointer(), hsig->GetBufferSize(), IID_PPV_ARGS(&a.hizRs)))) {
                D3D12_COMPUTE_PIPELINE_STATE_DESC hcpd{};
                hcpd.pRootSignature = a.hizRs.Get();
                hcpd.CS = {kDxil_hiz_downsample, sizeof(kDxil_hiz_downsample)};
                if (SUCCEEDED(dev12->CreateComputePipelineState(&hcpd, IID_PPV_ARGS(&a.hizPso)))) {
                    D3D12_DESCRIPTOR_HEAP_DESC hhd{};
                    hhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                    hhd.NumDescriptors = 2;
                    hhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                    if (SUCCEEDED(dev12->CreateDescriptorHeap(&hhd, IID_PPV_ARGS(&a.hizHeap)))) {
                        if (!a.descInc) a.descInc = dev12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                    }
                }
            }
        }
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

    // True NVIDIA DLSS Ray Reconstruction (Preset F) with SM 6.9 linear world-space normal+roughness unpack:
    // NormalUnpack.hlsl decodes Skyrim's octahedral R8G8B8A8 view-space normals + Spec.y gloss into RGBA16F
    // (RGB = linear world-space normal, A = linear roughness) matching sl::DLSSDNormalRoughnessMode::ePacked.
    if (m_mode == 2 && a.unpackPso && a.unpackHeap && (a.nrW != width || a.nrH != height || !a.normalRough)) {
        a.normalRough.Reset();
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = width; rd.Height = height; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rd.SampleDesc = {1, 0};
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (SUCCEEDED(a.dev12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                       D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&a.normalRough)))) {
            a.nrW = width; a.nrH = height;
        }
    }
    const bool useRR = (m_mode == 2 && m_caps.dlssRR && !a.dlssdFailed && a.dlssdSetOptions &&
                        a.unpackPso && a.unpackHeap && a.normalRough && albedo && normal && spec && cam.valid);
    if (!useRR && (!m_caps.dlss || a.dlssFailed || !a.dlssSetOptions)) return false;

    const sl::ViewportHandle vp(0);
    const int activeMode = useRR ? 2 : m_mode;
    if (a.optW != width || a.optH != height || a.optMode != activeMode) {
        if (useRR) {
            sl::DLSSDOptions opt{};
            opt.mode = sl::DLSSMode::eDLAA;
            opt.outputWidth = width;
            opt.outputHeight = height;
            opt.colorBuffersHDR = sl::Boolean::eTrue;
            opt.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
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
    c.jitterOffset = {
        cam.jitterNdc[0] * float(width) * 0.5f,
        -cam.jitterNdc[1] * float(height) * 0.5f
    };
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
    // Harvest retired Native DX12 Compute Hi-Z depth buffer (2 frames old, 100% GPU-retired, zero stall).
    if (m_hizCb && a.hizValid[slot] && a.hizRead[slot] && a.hizW && a.hizH) {
        const D3D12_RANGE rr{0, size_t(a.hizW) * a.hizH * 4};
        void* hp = nullptr;
        if (SUCCEEDED(a.hizRead[slot]->Map(0, &rr, &hp)) && hp) {
            m_hizCb(static_cast<const float*>(hp), a.hizW, a.hizH, a.hizW2C[slot], a.hizCam[slot]);
            const D3D12_RANGE wr{0, 0};
            a.hizRead[slot]->Unmap(0, &wr);
        }
        a.hizValid[slot] = false;
    }
    if (FAILED(a.alloc[slot]->Reset()) || FAILED(a.cl->Reset(a.alloc[slot].Get(), nullptr))) return false;

    D3D12_RESOURCE_BARRIER inB[8];
    UINT nb = 0;
    inB[nb++] = Barrier(colorIn,  D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    inB[nb++] = Barrier(colorOut, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    inB[nb++] = Barrier(depth,    D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    inB[nb++] = Barrier(mvec,     D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (useRR) {
        inB[nb++] = Barrier(albedo, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        inB[nb++] = Barrier(normal, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        inB[nb++] = Barrier(spec,   D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        inB[nb++] = Barrier(a.normalRough.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    a.cl->ResourceBarrier(nb, inB);

    if (useRR) {
        // Dispatch SM 6.9 NormalUnpack.hlsl -> linear RGBA16F world-space normal + roughness for DLSS-RR ePacked.
        D3D12_CPU_DESCRIPTOR_HANDLE h0 = a.unpackHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        srv.Format = normal->GetDesc().Format;
        a.dev12->CreateShaderResourceView(normal, &srv, h0);
        D3D12_CPU_DESCRIPTOR_HANDLE h1{h0.ptr + a.descInc};
        srv.Format = spec->GetDesc().Format;
        a.dev12->CreateShaderResourceView(spec, &srv, h1);
        D3D12_CPU_DESCRIPTOR_HANDLE h2{h0.ptr + 2u * a.descInc};
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        a.dev12->CreateUnorderedAccessView(a.normalRough.Get(), nullptr, &uav, h2);

        ID3D12DescriptorHeap* heaps[1] = {a.unpackHeap.Get()};
        a.cl->SetDescriptorHeaps(1, heaps);
        a.cl->SetComputeRootSignature(a.unpackRs.Get());
        a.cl->SetPipelineState(a.unpackPso.Get());
        struct { float v2w[12]; uint32_t w, h, p0, p1; } cb{};
        std::memcpy(cb.v2w, cam.viewToWorld, sizeof(cb.v2w));
        cb.w = width; cb.h = height;
        a.cl->SetComputeRoot32BitConstants(0, 16, &cb, 0);
        a.cl->SetComputeRootDescriptorTable(1, a.unpackHeap->GetGPUDescriptorHandleForHeapStart());
        a.cl->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
        D3D12_RESOURCE_BARRIER nrToSrv = Barrier(a.normalRough.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        a.cl->ResourceBarrier(1, &nrToSrv);
        inB[nb - 1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    // Dispatch SM 6.9 Native DX12 Compute Hi-Z 8x8 MAX-depth reduction (1920x1080 -> 240x135, 129 KB) into 3-slot ring.
    if (m_hizCb && a.hizPso && a.hizHeap && cam.valid) {
        const uint32_t hw = (width + 7u) / 8u, hh = (height + 7u) / 8u;
        const uint64_t bytes = uint64_t(hw) * hh * 4ull;
        if (a.hizW != hw || a.hizH != hh || !a.hizGpu) {
            a.hizGpu.Reset();
            for (auto& r : a.hizRead) r.Reset();
            D3D12_HEAP_PROPERTIES hpDef{D3D12_HEAP_TYPE_DEFAULT};
            D3D12_HEAP_PROPERTIES hpRb{D3D12_HEAP_TYPE_READBACK};
            D3D12_RESOURCE_DESC bd{};
            bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bd.Width = bytes; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
            bd.SampleDesc = {1, 0}; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            bd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            bool hok = SUCCEEDED(a.dev12->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &bd,
                                                                  D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&a.hizGpu)));
            bd.Flags = D3D12_RESOURCE_FLAG_NONE;
            for (int k = 0; hok && k < 3; ++k)
                hok = SUCCEEDED(a.dev12->CreateCommittedResource(&hpRb, D3D12_HEAP_FLAG_NONE, &bd,
                                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&a.hizRead[k])));
            if (hok) { a.hizW = hw; a.hizH = hh; for (bool& v : a.hizValid) v = false; }
            else { a.hizGpu.Reset(); a.hizW = a.hizH = 0; }
        }
        if (a.hizGpu && a.hizRead[slot]) {
            D3D12_CPU_DESCRIPTOR_HANDLE hh0 = a.hizHeap->GetCPUDescriptorHandleForHeapStart();
            D3D12_SHADER_RESOURCE_VIEW_DESC dsrv{};
            dsrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            dsrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            dsrv.Texture2D.MipLevels = 1;
            dsrv.Format = DXGI_FORMAT_R32_FLOAT;
            a.dev12->CreateShaderResourceView(depth, &dsrv, hh0);
            D3D12_UNORDERED_ACCESS_VIEW_DESC huav{};
            huav.Format = DXGI_FORMAT_UNKNOWN;
            huav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            huav.Buffer.NumElements = hw * hh;
            huav.Buffer.StructureByteStride = 4;
            a.dev12->CreateUnorderedAccessView(a.hizGpu.Get(), nullptr, &huav, {hh0.ptr + a.descInc});

            D3D12_RESOURCE_BARRIER hb0 = Barrier(a.hizGpu.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            a.cl->ResourceBarrier(1, &hb0);
            ID3D12DescriptorHeap* hheaps[1] = {a.hizHeap.Get()};
            a.cl->SetDescriptorHeaps(1, hheaps);
            a.cl->SetComputeRootSignature(a.hizRs.Get());
            a.cl->SetPipelineState(a.hizPso.Get());
            const uint32_t hcb[4] = {width, height, hw, hh};
            a.cl->SetComputeRoot32BitConstants(0, 4, hcb, 0);
            a.cl->SetComputeRootDescriptorTable(1, a.hizHeap->GetGPUDescriptorHandleForHeapStart());
            a.cl->Dispatch((hw + 7u) / 8u, (hh + 7u) / 8u, 1u);
            D3D12_RESOURCE_BARRIER hb1 = Barrier(a.hizGpu.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            a.cl->ResourceBarrier(1, &hb1);
            a.cl->CopyBufferRegion(a.hizRead[slot].Get(), 0, a.hizGpu.Get(), 0, bytes);
            D3D12_RESOURCE_BARRIER hb2 = Barrier(a.hizGpu.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
            a.cl->ResourceBarrier(1, &hb2);
            std::memcpy(a.hizW2C[slot], cam.worldToClip, sizeof(a.hizW2C[slot]));
            std::memcpy(a.hizCam[slot], cam.pos, sizeof(a.hizCam[slot]));
            a.hizValid[slot] = true;
        }
    }

    const sl::Extent ext{0, 0, width, height};
    sl::Resource rIn(sl::ResourceType::eTex2d, colorIn, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Resource rOut(sl::ResourceType::eTex2d, colorOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    sl::Resource rDepth(sl::ResourceType::eTex2d, depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Resource rMvec(sl::ResourceType::eTex2d, mvec, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Resource rAlb(sl::ResourceType::eTex2d, albedo, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Resource rNrm(sl::ResourceType::eTex2d, useRR ? a.normalRough.Get() : normal, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Resource rSpec(sl::ResourceType::eTex2d, spec, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::ResourceTag tags[7] = {
        {&rIn,    sl::kBufferTypeScalingInputColor,  sl::ResourceLifecycle::eValidUntilEvaluate, &ext},
        {&rOut,   sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &ext},
        {&rDepth, sl::kBufferTypeDepth,              sl::ResourceLifecycle::eValidUntilEvaluate, &ext},
        {&rMvec,  sl::kBufferTypeMotionVectors,      sl::ResourceLifecycle::eValidUntilEvaluate, &ext},
        {&rAlb,   sl::kBufferTypeAlbedo,             sl::ResourceLifecycle::eValidUntilEvaluate, &ext},
        {&rSpec,  sl::kBufferTypeSpecularAlbedo,     sl::ResourceLifecycle::eValidUntilEvaluate, &ext},
        {&rNrm,   sl::kBufferTypeNormalRoughness,    sl::ResourceLifecycle::eValidUntilEvaluate, &ext}
    };
    a.setTagForFrame(*tok, vp, tags, useRR ? 7u : 4u, a.cl.Get());

    const sl::BaseStructure* inputs[] = {&vp};
    const sl::Feature feat = useRR ? sl::kFeatureDLSS_RR : sl::kFeatureDLSS;
    const sl::Result er = a.evaluateFeature(feat, *tok, inputs, 1, a.cl.Get());

    // Watermarked sl.deepdvc.dll evaluation is bypassed; m_deepDvcOn now drives RAW's own
    // 100% watermark-free GPU-native Oklab Neural Vibrance & Local Contrast pass in Tonemap.hlsl.
    (void)m_deepDvcOn;

    D3D12_RESOURCE_BARRIER outB[8];
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
