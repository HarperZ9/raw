// RtScene: see the header. Original work, MIT.
#include "RtScene.h"
#include "RtSceneDetail.h"
#include "rt_shadow.h"   // kDxil_rt_shadow (dxc, cs_6_6)
#include "rt_shadow_debug.h"   // kDxil_rt_shadow_debug: the same with the debug views (RtDebug)
#ifdef RAW_HAS_SM69_DXIL
#include "rt_shadow_69.h"        // kDxil_rt_shadow_69 (dxc 1.9+, native cs_6_9 DXIL 1.9)
#include "rt_shadow_debug_69.h"  // kDxil_rt_shadow_debug_69 (dxc 1.9+, native cs_6_9)
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

namespace raw::bridge
{
using namespace rtdetail;

RtScene::~RtScene()
{
    if (m_fence && m_fence->GetCompletedValue() < m_fenceValue) {
        HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_fence->SetEventOnCompletion(m_fenceValue, e);
        WaitForSingleObject(e, 5000);
        CloseHandle(e);
    }
    for (Mesh& m : m_meshes) { if (m.vb) m.vb->Release(); if (m.ib) m.ib->Release(); }
    for (RtDraw& d : m_draws) ReleaseRtDraw(d);
}

bool RtScene::Buffer(ComPtr<ID3D12Resource>& r, uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES s, bool uav, const wchar_t* name)
{
    D3D12_HEAP_PROPERTIES hp{heap};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1; d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    if (FAILED(m_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, s, nullptr, IID_PPV_ARGS(&r)))) return false;
    r->SetName(name);
    return true;
}

bool RtScene::Texture(ComPtr<ID3D12Resource>& r, const wchar_t* name, DXGI_FORMAT format)
{
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = m_w; d.Height = m_h; d.DepthOrArraySize = 1; d.MipLevels = 1; d.SampleDesc.Count = 1;
    d.Format = format;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(m_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&r)))) return false;
    r->SetName(name);
    return true;
}

bool RtScene::Pipeline(std::string& err)
{
    D3D12_DESCRIPTOR_RANGE srv{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, 0};
    D3D12_DESCRIPTOR_RANGE uav{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0, 0, 0};   // its own table, set at descriptor 3: vis, lights, alpha coordinates
    D3D12_DESCRIPTOR_RANGE masks{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, UINT(-1), 0, 1, 0};   // u0 space1 onward, at descriptor 6
    D3D12_DESCRIPTOR_RANGE skyRaw{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3, 0, 0};          // u3, RtSky's
    D3D12_DESCRIPTOR_RANGE colour{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5, 0, 0};          // t5, the game's frame (RtSky's)
    D3D12_DESCRIPTOR_RANGE refl{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 4, 0, 0};            // u4, the reflection output
    // Off-screen hits (RtAlpha, RtCache): the face normals (u5), the colour copies (u0 space2), the
    // cache (u6..u8), the material records (t6) and the cache's constants (b2).
    D3D12_DESCRIPTOR_RANGE normals{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 5, 0, 0};
    D3D12_DESCRIPTOR_RANGE albedo{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, RtAlpha::kMaxAlbedo, 0, 2, 0};
    D3D12_DESCRIPTOR_RANGE cache{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, RtCache::Descriptors(), 6, 0, 0};
    // The reservoirs (u9 in, u10 out) and the motion vectors (t7), RtSky's (RtRestReuse).
    D3D12_DESCRIPTOR_RANGE resv[2] = {{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 9, 0, 0}, {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 7, 0, 2}};
    D3D12_ROOT_PARAMETER p[16]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; p[0].Descriptor = {0, 0};
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[1].Constants = {1, 0, 8};
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; p[2].Descriptor = {3, 0};
    p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[3].DescriptorTable = {1, &srv};
    p[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[4].DescriptorTable = {1, &uav};
    p[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; p[5].Descriptor = {4, 0};
    p[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[6].DescriptorTable = {1, &masks};
    p[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[7].DescriptorTable = {1, &skyRaw};
    p[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[8].DescriptorTable = {1, &colour};
    p[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[9].DescriptorTable = {1, &refl};
    p[10].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[10].DescriptorTable = {1, &normals};
    p[11].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[11].DescriptorTable = {1, &albedo};
    p[12].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[12].DescriptorTable = {1, &cache};
    p[13].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; p[13].Descriptor = {6, 0};
    p[14].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[14].Constants = {2, 0, 12};
    p[15].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[15].DescriptorTable = {2, resv};
    D3D12_FEATURE_DATA_D3D12_OPTIONS o0{};
    const bool tier3 = SUCCEEDED(m_dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o0, sizeof o0)) &&
                       o0.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3;
    D3D12_ROOT_SIGNATURE_DESC rs{16, p, 0, nullptr,
                                 tier3 ? D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
                                       : D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, errs;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errs))) {
        rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
        blob.Reset(); errs.Reset();
        if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errs))) {
            err = errs ? static_cast<const char*>(errs->GetBufferPointer()) : "root signature";
            return false;
        }
    }
    if (FAILED(m_dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
        if (rs.Flags != D3D12_ROOT_SIGNATURE_FLAG_NONE) {
            rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
            blob.Reset(); errs.Reset();
            if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errs)) ||
                FAILED(m_dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
                err = "CreateRootSignature"; return false;
            }
        } else { err = "CreateRootSignature"; return false; }
    }
    if (m_log && (rs.Flags & D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED))
        m_log("[RT] SM 6.6+ ResourceDescriptorHeap[] direct bindless heap indexing enabled on trace root signature");
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_root.Get();
#ifdef RAW_HAS_SM69_DXIL
    if (m_useSm69) {
        pd.CS = {kDxil_rt_shadow_69, sizeof kDxil_rt_shadow_69};
        ComPtr<ID3D12PipelineState> pso69, psoDbg69;
        if (SUCCEEDED(m_dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso69)))) {
            pd.CS = {kDxil_rt_shadow_debug_69, sizeof kDxil_rt_shadow_debug_69};
            if (SUCCEEDED(m_dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&psoDbg69)))) {
                m_pso = pso69;
                m_psoDebug = psoDbg69;
                if (m_log) m_log("[RT] native SM 6.9 DXIL 1.9 trace pipeline active (RtShadow cs_6_9)");
                return true;
            }
        }
        m_useSm69 = false;
        if (m_log) m_log("[RT] SM 6.9 trace pipeline refused by runtime; falling back to SM 6.6");
    }
#endif
    pd.CS = {kDxil_rt_shadow, sizeof kDxil_rt_shadow};
    if (FAILED(m_dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_pso)))) { err = "CreateComputePipelineState (rt_shadow)"; return false; }
    pd.CS = {kDxil_rt_shadow_debug, sizeof kDxil_rt_shadow_debug};
    if (FAILED(m_dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_psoDebug)))) { err = "CreateComputePipelineState (rt_shadow debug)"; return false; }
    return true;
}

bool RtScene::Create(ID3D12Device* dev, UINT width, UINT height, LogFn log, std::string& err)
{
    m_log = log; m_w = width; m_h = height;
    if (!dev || FAILED(dev->QueryInterface(IID_PPV_ARGS(&m_dev)))) { err = "no ID3D12Device5"; return false; }
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
    m_dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof o5);
    // The device's highest shader model and DXR tier, as the driver reports them (the system runtime
    // without the Agility SDK answers up to what it knows; the loop asks from the newest down).
    D3D12_FEATURE_DATA_SHADER_MODEL sm{};
    for (int v = 0x6a; v >= 0x60; --v) {
        sm.HighestShaderModel = static_cast<D3D_SHADER_MODEL>(v);
        if (SUCCEEDED(m_dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof sm))) break;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 o7{};
    m_dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &o7, sizeof o7);
#ifdef D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_BYTE_ALIGNMENT
    // Agility headers: DXR 1.2 brings opacity micromaps and shader execution reordering.
    D3D12_FEATURE_DATA_D3D12_OPTIONS22 o22{};
    const bool has22 = SUCCEEDED(m_dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS22, &o22, sizeof o22));
    if (m_log)
        m_log("[RT] DXR 1.2 features: opacity micromaps %s, shader execution reordering %s (reorders: %s)",
              o5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_2 ? "yes" : "no", o5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_2 ? "yes" : "no",
              has22 ? (o22.ShaderExecutionReorderingActuallyReorders ? "yes" : "no") : "unknown");
#endif
    if (m_log)
        m_log("[RT] device: shader model %d.%d, DXR tier %d.%d, mesh shader tier %d, sampler feedback tier %d", int(sm.HighestShaderModel) >> 4,
              int(sm.HighestShaderModel) & 15, int(o5.RaytracingTier) / 10, int(o5.RaytracingTier) % 10, int(o7.MeshShaderTier), int(o7.SamplerFeedbackTier));
    if (o5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1) { err = "DXR tier 1.1 (inline ray queries) not supported"; return false; }
    if (sm.HighestShaderModel < D3D_SHADER_MODEL_6_6) { err = "shader model 6.6 not supported"; return false; }
#ifdef RAW_HAS_SM69_DXIL
    m_useSm69 = (static_cast<unsigned>(sm.HighestShaderModel) >= 0x69u);
#endif
    const auto AS = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, UA = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    bool ok = Buffer(m_blasHeap, kBlasHeap, D3D12_HEAP_TYPE_DEFAULT, AS, true, L"rt blas heap") &&
              Buffer(m_scratch, kScratch, D3D12_HEAP_TYPE_DEFAULT, UA, true, L"rt scratch") &&
              Buffer(m_tlas, kTlas, D3D12_HEAP_TYPE_DEFAULT, AS, true, L"rt tlas") &&
              Buffer(m_tlasScratch, kTlas, D3D12_HEAP_TYPE_DEFAULT, UA, true, L"rt tlas scratch") &&
              Buffer(m_tsRead, 3 * 4 * 8, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, false, L"rt timestamps") &&
              Texture(m_vis, L"rt visibility") && Texture(m_lights, L"rt light index") &&
              Texture(m_refl, L"rt reflection", DXGI_FORMAT_R16G16B16A16_FLOAT);
    // Optional DX12 Hardware Async Compute Queue (enabled via RtAsyncCompute=1 once BLAS/TLAS builds are split
    // ahead of GeometryMain; back-to-back cross-queue fencing adds ~1.1 ms WDDM overhead when not overlapped).
    const bool wantAsync = GetPrivateProfileIntA("Lighting", "RtAsyncCompute", 0, "d3d11_proxy.ini") != 0;
    if (wantAsync) {
        D3D12_COMMAND_QUEUE_DESC cqd{};
        cqd.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        cqd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        if (SUCCEEDED(m_dev->CreateCommandQueue(&cqd, IID_PPV_ARGS(&m_asyncQueue))) &&
            SUCCEEDED(m_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_syncFence)))) {
            m_asyncCompute = true;
            m_asyncQueue->SetName(L"RAW Async Compute Queue");
            m_asyncQueue->GetTimestampFrequency(&m_freq);
            if (m_log) m_log("[RT] DX12 Async Compute Queue active (D3D12_COMMAND_LIST_TYPE_COMPUTE)");
        } else {
            m_asyncQueue.Reset(); m_syncFence.Reset(); m_asyncCompute = false;
        }
    }
    const D3D12_COMMAND_LIST_TYPE cmdType = m_asyncCompute ? D3D12_COMMAND_LIST_TYPE_COMPUTE : D3D12_COMMAND_LIST_TYPE_DIRECT;
    for (int k = 0; ok && k < 3; ++k)
        ok = Buffer(m_inst[k], kMaxInstances * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false, L"rt instances") &&
             Buffer(m_cb[k], kCb, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false, L"rt uniform") &&
             SUCCEEDED(m_dev->CreateCommandAllocator(cmdType, IID_PPV_ARGS(&m_alloc[k])));
    if (!ok) { err = "resources"; return false; }
    D3D12_QUERY_HEAP_DESC qd{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 12, 0};
    // 0..2 the G-buffer, 3 and 4 the outputs, then RtAlpha's: its coordinates (5), masks (6...), staging.
#ifdef RAW_HAS_OMM
    constexpr UINT kOmmDescriptors = RtOmm::Descriptors();
#else
    constexpr UINT kOmmDescriptors = 0;
#endif
    const UINT cacheAt = 5 + RtAlpha::Descriptors() + RtSky::Descriptors() + 1 + kOmmDescriptors;
    const UINT skinAt = cacheAt + RtCache::Descriptors();
    m_waterAt = skinAt + RtSkin::Descriptors();
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_waterAt + kWaterDescriptors,
                                  D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    if (FAILED(m_dev->CreateQueryHeap(&qd, IID_PPV_ARGS(&m_ts))) || FAILED(m_dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap))) ||
        FAILED(m_dev->CreateCommandList(0, cmdType, m_alloc[0].Get(), nullptr, IID_PPV_ARGS(&m_list))) ||
        FAILED(m_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) { err = "objects"; return false; }
    m_list->Close();
    if (!Pipeline(err)) return false;
    if (!m_alpha.Create(m_dev.Get(), m_heap.Get(), 5, err)) return false;
    std::string skyErr;
    m_skyReady = m_sky.Create(m_dev.Get(), m_heap.Get(), 5 + RtAlpha::Descriptors(), width, height, skyErr);
    if (!m_skyReady && m_log) m_log("[RT] sky visibility off: %s", skyErr.c_str());
    std::string cacheErr;
    m_cacheReady = m_cache.Create(m_dev.Get(), m_heap.Get(), cacheAt, cacheErr);
    if (m_log) m_log(m_cacheReady ? "[RT] radiance cache ready: %u slots" : "[RT] radiance cache off: %s",
                     m_cacheReady ? RtCache::kSlots : 0u, cacheErr.c_str());
    std::string skinErr;
    m_skinReady = m_skin.Create(m_dev.Get(), m_heap.Get(), skinAt, skinErr);
    if (!m_skinReady && m_log) m_log("[RT] skinned actors off: %s", skinErr.c_str());
#ifdef RAW_HAS_OMM
    // Opacity micromaps: DXR 1.2. Native SM 6.9 shaders support OMM directly without the PRE_SM69 flag;
    // SM 6.6 shaders require D3D12_PIPELINE_STATE_FLAG_RAYQUERY_ALLOW_OPACITY_MICROMAPS_ON_PRE_SM69_SHADERS.
    if (o5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_2) {
        std::string ommErr;
        if (!m_omm.Create(m_dev.Get(), m_heap.Get(), 5 + RtAlpha::Descriptors() + RtSky::Descriptors() + 1, 5, 6, ommErr)) {
            if (m_log) m_log("[RT] opacity micromaps off: %s", ommErr.c_str());
        } else if (m_useSm69) {
            m_psoFlags = 0;
            m_ommOn = true;
            if (m_log) m_log("[RT] opacity micromaps on (native SM 6.9 DXR 1.2): level %u, 4-state, baked from the alpha masks", RtOmm::kLevel);
        } else {
            const UINT flags = D3D12_PIPELINE_STATE_FLAG_RAYQUERY_ALLOW_OPACITY_MICROMAPS_ON_PRE_SM69_SHADERS;
            D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
            pd.pRootSignature = m_root.Get();
            pd.CS = {kDxil_rt_shadow, sizeof kDxil_rt_shadow};
            pd.Flags = D3D12_PIPELINE_STATE_FLAGS(flags);
            ComPtr<ID3D12PipelineState> pso;
            if (FAILED(m_dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)))) {
                if (m_log) m_log("[RT] opacity micromaps off: the runtime refused the pipeline flag (the system runtime, not the Agility SDK?)");
            } else {
                m_pso = pso;
                pd.CS = {kDxil_rt_shadow_debug, sizeof kDxil_rt_shadow_debug};
                if (SUCCEEDED(m_dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)))) m_psoDebug = pso;
                m_psoFlags = flags;
                m_ommOn = true;
                if (m_log) m_log("[RT] opacity micromaps on: level %u, 4-state, baked from the alpha masks", RtOmm::kLevel);
            }
        }
    }
#endif
    CreateWater();
    m_ready = true;
    if (m_log) m_log("[RT] scene ready on the layer's device: %ux%u, BLAS heap %llu MB, %u instances at most", width, height,
                     (unsigned long long)(kBlasHeap >> 20), kMaxInstances);
    return true;
}

void RtScene::Evict()
{
    // The BLAS heap is a bump allocator: when it fills, every mesh is dropped (after the GPU is done
    // with the frames in flight) and rebuilt over the next frames.
    if (m_fence->GetCompletedValue() < m_fenceValue) {
        HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_fence->SetEventOnCompletion(m_fenceValue, e);
        WaitForSingleObject(e, 2000);
        CloseHandle(e);
    }
    for (Mesh& m : m_meshes) { if (m.vb) m.vb->Release(); if (m.ib) m.ib->Release(); }
    m_meshes.clear(); m_meshOf.clear(); m_skinRecOf.clear(); m_build.clear(); m_lastKeys.clear(); m_lastMesh.clear();
    m_persistInst.clear();
    m_blasUsed = 0;
    m_alpha.Reset();
    m_cache.Reset();   // a new cell: the old cells' light means nothing here
#ifdef RAW_HAS_OMM
    m_omm.Reset();
#endif
    ++m_stats.resets;
}

namespace
{
double NowUs()
{
    static LARGE_INTEGER f{};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) * 1e6 / double(f.QuadPart);
}
} // namespace

void RtScene::SetDraws(std::vector<RtDraw>&& draws, std::vector<ID3D11Resource*>& toHand, ID3D12Resource* (*native)(ID3D11Resource*))
{
    const double t0 = NowUs();
    SetDrawsInner(std::move(draws), toHand, native);
    m_cpuSetUs += NowUs() - t0;
}

void RtScene::SetDrawsInner(std::vector<RtDraw>&& draws, std::vector<ID3D11Resource*>& toHand, ID3D12Resource* (*native)(ID3D11Resource*))
{
    const double r0 = NowUs();
    for (RtDraw& d : m_draws) ReleaseRtDraw(d);
    m_cpuReleaseUs += NowUs() - r0;
    m_draws = std::move(draws);
    m_drawMesh.assign(m_draws.size(), ~0u);
    // The loaded area from this frame's distant land (all its chunks carry the same); none: nothing is skipped.
    for (float& v : m_lodRect) v = 0.0f;
    for (const RtDraw& d : m_draws)
        if (d.lodLand) { std::memcpy(m_lodRect, d.lodRect, sizeof m_lodRect); break; }
    m_stageState.clear();   // staged again below, or nothing placed this frame
    m_build.clear();
    ++m_frame;
    m_stats.draws = uint32_t(m_draws.size());
    m_stats.skipped = 0;
    if (!Active()) return;
    // A full heap, or a cache grown far past the scene (buffers the game rewrites at new offsets every
    // frame add new keys each time): start again. Also after a gap of more than half a second without a
    // world frame (a loading screen or a menu): the game may have refilled its buffers with the next
    // cell's meshes, and a BLAS keyed by buffer and range would keep the old shapes.
    const uint64_t now = GetTickCount64();
    const bool gap = m_lastDraws && now - m_lastDraws > 500;
    m_lastDraws = now;
    if (m_blasUsed > kBlasHeap - (64ull << 20) || m_meshes.size() > 40000 || (gap && !m_meshes.empty()) || m_evictPending) Evict();
    m_evictPending = false;
    m_skin.SetCamera(m_tune.cameraPos);
    m_skin.SetCheck(m_tune.skinCull, m_tune.skinRatio, m_tune.skinMin, 120, m_log);
    const double s0 = NowUs();
    if (m_skinReady && m_tune.skin) { SkinRecords(toHand, native); m_skin.SetDraws(m_draws, toHand, native); }
    else { std::vector<RtDraw> none; m_skin.SetDraws(none, toHand, native); }
    m_cpuSkinUs += NowUs() - s0;
    std::vector<Key>& keys = m_keys;   // reused: no allocation per frame
    keys.assign(m_draws.size(), Key{});
    for (size_t di = 0; di < m_draws.size(); ++di) {
        RtDraw& d = m_draws[di];
        if (d.skinned) continue;   // RtSkin's, rebuilt every frame
        const Key k{uintptr_t(d.vb), uintptr_t(d.ib), uint64_t(d.vbOffset) << 32 | d.ibOffset,
                    uint64_t(d.startIndex) << 32 | uint32_t(d.baseVertex) ^ uint64_t(d.indexCount) << 12};
        keys[di] = k;
        if (m_tune.incremental && di < m_lastKeys.size() && m_lastKeys[di] == k && m_lastMesh[di] < m_meshes.size()) {
            // The same draw at the same place in the list as last frame: its mesh without a lookup.
            m_meshes[m_lastMesh[di]].seen = m_frame; m_drawMesh[di] = m_lastMesh[di]; continue;
        }
        auto it = m_meshOf.find(k);
        if (it != m_meshOf.end()) { m_meshes[it->second].seen = m_frame; m_drawMesh[di] = it->second; continue; }
        // Over this frame's budget: the mesh is tried again next frame.
        const bool wantMat = d.alphaKind ? m_tune.alpha : m_tune.materials && d.alphaSrv;
        if (m_build.size() >= m_tune.maxNewPerFrame || (wantMat && !m_alpha.Room())) { ++m_stats.skipped; continue; }
        D3D11_BUFFER_DESC vd{}, id{};
        d.vb->GetDesc(&vd); d.ib->GetDesc(&id);
        ID3D12Resource* v12 = native(d.vb);
        ID3D12Resource* i12 = native(d.ib);
        const uint64_t vStart = uint64_t(d.vbOffset) + int64_t(d.baseVertex) * d.stride;
        const uint32_t isz = d.ibFormat == DXGI_FORMAT_R16_UINT ? 2 : 4;
        const uint64_t iStart = uint64_t(d.ibOffset) + uint64_t(d.startIndex) * isz;
        const bool usable = v12 && i12 && vd.Usage != D3D11_USAGE_DYNAMIC && id.Usage != D3D11_USAGE_DYNAMIC && d.stride &&
                            vStart < vd.ByteWidth && iStart + uint64_t(d.indexCount) * isz <= id.ByteWidth && d.indexCount >= 3;
        Mesh m;
        m.seen = m_frame;
        if (!usable) { m.failed = true; ++m_stats.skipped; }
        else {
            m.vb = d.vb; m.ib = d.ib; m.vb->AddRef(); m.ib->AddRef();
            D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC& t = m.tri;
            t.VertexFormat = d.posFormat == DXGI_FORMAT_R16G16B16A16_FLOAT ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R32G32B32_FLOAT;
            t.VertexBuffer.StartAddress = v12->GetGPUVirtualAddress() + vStart + d.posOffset;
            t.VertexBuffer.StrideInBytes = d.stride;
            t.VertexCount = UINT((vd.ByteWidth - vStart) / d.stride);
            t.IndexFormat = d.ibFormat;
            t.IndexBuffer = i12->GetGPUVirtualAddress() + iStart;
            t.IndexCount = d.indexCount / 3 * 3;
            if (!d.alphaKind && wantMat) {
                // Opaque: its coordinates, normals and colour copy, for hits off screen; without them it
                // still joins (record 0: shaded grey off screen).
                m.alphaRecord = m_alpha.AddMesh(d, v12, i12, vStart, iStart, t.IndexCount / 3, false, toHand, native);
            }
            if (d.alphaKind) {
                // Alpha-tested: its coordinates and mask join RtAlpha, or the mesh stays out (a solid card).
                m.alpha = true;
                if (m_tune.alpha) m.alphaRecord = m_alpha.AddMesh(d, v12, i12, vStart, iStart, t.IndexCount / 3, true, toHand, native);
                if (!m.alphaRecord) { m.failed = true; ++m_stats.skipped; m.vb->Release(); m.ib->Release(); m.vb = m.ib = nullptr; }
#ifdef RAW_HAS_OMM
                else if (m_ommOn && m_tune.omm) {
                    uint32_t info[4];
                    m_alpha.Info(m.alphaRecord - 1, info);
                    m.omm = m_omm.Add(t.IndexCount / 3, info[0], info[1], info[2], info[3]);
                }
#endif
            }
            if (!m.failed) { toHand.push_back(d.vb); toHand.push_back(d.ib); }
        }
        m_drawMesh[di] = uint32_t(m_meshes.size());
        m_meshOf.emplace(k, uint32_t(m_meshes.size()));
        if (!m.failed) m_build.push_back(uint32_t(m_meshes.size()));
        m_meshes.push_back(m);
    }
    m_lastKeys.swap(keys);
    m_lastMesh = m_drawMesh;
    if (m_tune.incremental) StageDraws();
}

// Alpha-tested skinned draws (hair, fur, leaves): their coordinates and mask join RtAlpha once per mesh, as a rigid
// alpha mesh's do; the coordinates do not move with the bones, so the record holds while the BLAS is rebuilt each
// frame. A mesh with no room this frame waits (skinRecord ~0u: left out of this frame); one that cannot join stays out.
// The face normals RtAlpha would take from the bind pose are left out (the coordinates are passed as their own
// stream), so an off-screen hit on one faces the ray.
void RtScene::SkinRecords(std::vector<ID3D11Resource*>& toHand, ID3D12Resource* (*native)(ID3D11Resource*))
{
    for (RtDraw& d : m_draws) {
        if (!d.skinned || !d.alphaKind) continue;
        d.skinRecord = ~0u;
        if (!m_tune.alpha) continue;
        const Key k{uintptr_t(d.vb), uintptr_t(d.ib), uint64_t(d.vbOffset) << 32 | d.ibOffset,
                    uint64_t(d.startIndex) << 32 | uint32_t(d.baseVertex) ^ uint64_t(d.indexCount) << 12};
        auto it = m_skinRecOf.find(k);
        if (it != m_skinRecOf.end()) { d.skinRecord = it->second ? it->second : ~0u; continue; }
        if (!m_alpha.Room()) continue;
        D3D11_BUFFER_DESC vd{};
        d.vb->GetDesc(&vd);
        ID3D12Resource* v12 = native(d.vb);
        ID3D12Resource* i12 = native(d.ib);
        const int64_t vStart = int64_t(d.vbOffset) + int64_t(d.baseVertex) * d.stride;
        const uint32_t isz = d.ibFormat == DXGI_FORMAT_R16_UINT ? 2 : 4;
        const uint64_t iStart = uint64_t(d.ibOffset) + uint64_t(d.startIndex) * isz;
        uint32_t rec = 0;
        if (v12 && i12 && vStart >= 0 && uint64_t(vStart) < vd.ByteWidth) {
            RtDraw t = d;   // a view of d: the coordinates as their own stream (no bind-pose normals); no references taken
            if (!t.uvVb) { t.uvVb = d.vb; t.uvVbOffset = d.vbOffset; t.uvStride = d.stride; }
            rec = m_alpha.AddMesh(t, v12, i12, uint64_t(vStart), iStart, d.indexCount / 3, true, toHand, native);
        }
        m_skinRecOf.emplace(k, rec);
        d.skinRecord = rec ? rec : ~0u;
    }
}

bool RtScene::Submit(ID3D12CommandQueue* q)
{
    if (!m_recorded) return false;
    ID3D12CommandList* l = m_list.Get();
    if (m_asyncCompute && m_asyncQueue) {
        // GPU-side cross-queue handoff (zero CPU stall): wait for Direct queue GBuffer, run DXR on Async Compute, signal back.
        q->Signal(m_syncFence.Get(), ++m_syncValue);
        m_asyncQueue->Wait(m_syncFence.Get(), m_syncValue);
        m_asyncQueue->ExecuteCommandLists(1, &l);
        m_asyncQueue->Signal(m_fence.Get(), ++m_fenceValue);
        q->Wait(m_fence.Get(), m_fenceValue);
    } else {
        q->ExecuteCommandLists(1, &l);
        q->Signal(m_fence.Get(), ++m_fenceValue);
    }
    m_slotValue[m_frame % 3] = m_fenceValue;
    m_recorded = false;
    return true;
}

RtFrameStats RtScene::Stats() { return m_stats; }

void RtScene::SetCamera(const float step[3], uint32_t voters, const float posAdjust[4])
{
    // A step longer than 2000 units in one frame is a teleport or a cell change: the cache restarts
    // (the place itself stays continuous; nothing keys on its absolute value).
    // The per-frame constants' float4 40 is the camera's world position (Riverwood logged 17225,
    // -47204, -8); where it is set it is used as it is, otherwise the steps are summed.
    // Both kept: float4 40 and the summed steps of the rigid draws' world blocks (RtCamSteps=1 uses the
    // steps; in third person float4 40 may follow the player rather than the camera).
    const bool hasAdjust = posAdjust[0] != 0.0f || posAdjust[1] != 0.0f || posAdjust[2] != 0.0f;
    if (!m_stepInit && hasAdjust) { for (int k = 0; k < 3; ++k) m_stepPos[k] = posAdjust[k]; m_stepInit = true; }
    else if (voters) for (int k = 0; k < 3; ++k) m_stepPos[k] += step[k];
    if (m_tune.camSteps && m_stepInit) {
        for (int k = 0; k < 3; ++k) m_camPos[k] = m_stepPos[k];
    } else if (hasAdjust) {
        const double dx = posAdjust[0] - m_camPos[0], dy = posAdjust[1] - m_camPos[1], dz = posAdjust[2] - m_camPos[2];
        if (dx * dx + dy * dy + dz * dz > 2000.0 * 2000.0) m_cache.Reset();
        for (int k = 0; k < 3; ++k) m_camPos[k] = posAdjust[k];
    } else if (voters) {
        const double len = std::sqrt(double(step[0]) * step[0] + double(step[1]) * step[1] + double(step[2]) * step[2]);
        if (len > 2000.0) m_cache.Reset();
        else for (int k = 0; k < 3; ++k) m_camPos[k] += step[k];
    }
    for (int k = 0; k < 3; ++k) m_tune.cameraPos[k] = float(m_camPos[k]);
    if (m_log && (m_camLog++ % 300) == 0)
        m_log("[RT] camera: used %.1f %.1f %.1f; summed steps %.1f %.1f %.1f (step %.2f %.2f %.2f from %u draws); per-frame constants float4 40: %.1f %.1f %.1f %.3f",
              m_camPos[0], m_camPos[1], m_camPos[2], m_stepPos[0], m_stepPos[1], m_stepPos[2], step[0], step[1], step[2], voters,
              posAdjust[0], posAdjust[1], posAdjust[2], posAdjust[3]);
}

bool RtScene::Reload(const std::vector<uint8_t>& shadow, const std::vector<uint8_t>& sky, std::string& err)
{
    if (!m_ready) { err = "the ray-tracing scene is not running"; return false; }
    ComPtr<ID3D12PipelineState> pso;
    if (!shadow.empty()) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = m_root.Get();
        pd.CS = {shadow.data(), shadow.size()};
        pd.Flags = D3D12_PIPELINE_STATE_FLAGS(m_psoFlags);
        if (FAILED(m_dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)))) {
#ifdef RAW_HAS_OMM
            if (m_ommOn) {
                pd.Flags = m_psoFlags ? D3D12_PIPELINE_STATE_FLAG_NONE
                                      : D3D12_PIPELINE_STATE_FLAGS(D3D12_PIPELINE_STATE_FLAG_RAYQUERY_ALLOW_OPACITY_MICROMAPS_ON_PRE_SM69_SHADERS);
                if (FAILED(m_dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)))) { err = "the device refused the reloaded RtShadow.hlsl"; return false; }
            } else
#endif
            { err = "the device refused the reloaded RtShadow.hlsl"; return false; }
        }
    }
    if (m_fence->GetCompletedValue() < m_fenceValue) {
        HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_fence->SetEventOnCompletion(m_fenceValue, e);
        WaitForSingleObject(e, 2000);
        CloseHandle(e);
    }
    if (m_skyReady && !sky.empty() && !m_sky.Reload(sky.data(), sky.size(), err)) return false;
    if (pso) m_pso = pso;
    return true;
}

} // namespace raw::bridge
