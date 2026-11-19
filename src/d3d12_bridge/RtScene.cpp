// RtScene: see the header. Original work, MIT.
#include "RtScene.h"
#include "rt_shadow.h"   // kDxil_rt_shadow (dxc, cs_6_6)

#include <algorithm>
#include <cstring>

namespace raw::bridge
{
namespace
{
constexpr uint32_t kMaxInstances = 16384;
constexpr uint64_t kBlasHeap = 768ull << 20, kScratch = 256ull << 20, kTlas = 32ull << 20, kCb = 64 * 1024;
constexpr uint64_t Align(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b)
{
    D3D12_RESOURCE_BARRIER x{};
    x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    x.Transition.pResource = r;
    x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    x.Transition.StateBefore = a;
    x.Transition.StateAfter = b;
    return x;
}
D3D12_RESOURCE_BARRIER Uav(ID3D12Resource* r = nullptr)
{
    D3D12_RESOURCE_BARRIER x{};
    x.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    x.UAV.pResource = r;
    return x;
}
} // namespace

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
    D3D12_ROOT_PARAMETER p[10]{};
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
    D3D12_ROOT_SIGNATURE_DESC rs{10, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, errs;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errs))) {
        err = errs ? static_cast<const char*>(errs->GetBufferPointer()) : "root signature";
        return false;
    }
    if (FAILED(m_dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_root)))) { err = "CreateRootSignature"; return false; }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_root.Get();
    pd.CS = {kDxil_rt_shadow, sizeof kDxil_rt_shadow};
    if (FAILED(m_dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_pso)))) { err = "CreateComputePipelineState (rt_shadow)"; return false; }
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
    const auto AS = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, UA = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    bool ok = Buffer(m_blasHeap, kBlasHeap, D3D12_HEAP_TYPE_DEFAULT, AS, true, L"rt blas heap") &&
              Buffer(m_scratch, kScratch, D3D12_HEAP_TYPE_DEFAULT, UA, true, L"rt scratch") &&
              Buffer(m_tlas, kTlas, D3D12_HEAP_TYPE_DEFAULT, AS, true, L"rt tlas") &&
              Buffer(m_tlasScratch, kTlas, D3D12_HEAP_TYPE_DEFAULT, UA, true, L"rt tlas scratch") &&
              Buffer(m_tsRead, 3 * 3 * 8, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, false, L"rt timestamps") &&
              Texture(m_vis, L"rt visibility") && Texture(m_lights, L"rt light index") &&
              Texture(m_refl, L"rt reflection", DXGI_FORMAT_R16G16B16A16_FLOAT);
    for (int k = 0; ok && k < 3; ++k)
        ok = Buffer(m_inst[k], kMaxInstances * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false, L"rt instances") &&
             Buffer(m_cb[k], kCb, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false, L"rt uniform") &&
             SUCCEEDED(m_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_alloc[k])));
    if (!ok) { err = "resources"; return false; }
    D3D12_QUERY_HEAP_DESC qd{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 9, 0};
    // 0..2 the G-buffer, 3 and 4 the outputs, then RtAlpha's: its coordinates (5), masks (6...), staging.
#ifdef RAW_HAS_OMM
    constexpr UINT kOmmDescriptors = RtOmm::Descriptors();
#else
    constexpr UINT kOmmDescriptors = 0;
#endif
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 5 + RtAlpha::Descriptors() + RtSky::Descriptors() + 1 + kOmmDescriptors,
                                  D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    if (FAILED(m_dev->CreateQueryHeap(&qd, IID_PPV_ARGS(&m_ts))) || FAILED(m_dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap))) ||
        FAILED(m_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc[0].Get(), nullptr, IID_PPV_ARGS(&m_list))) ||
        FAILED(m_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) { err = "objects"; return false; }
    m_list->Close();
    if (!Pipeline(err)) return false;
    if (!m_alpha.Create(m_dev.Get(), m_heap.Get(), 5, err)) return false;
    std::string skyErr;
    m_skyReady = m_sky.Create(m_dev.Get(), m_heap.Get(), 5 + RtAlpha::Descriptors(), width, height, skyErr);
    if (!m_skyReady && m_log) m_log("[RT] sky visibility off: %s", skyErr.c_str());
#ifdef RAW_HAS_OMM
    // Opacity micromaps: DXR 1.2, and a trace pipeline that lets its ray queries use them (the shader is
    // below shader model 6.9, so the pipeline flag does it). Either missing: the candidate loop alone.
    if (o5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_2) {
        std::string ommErr;
        const UINT flags = D3D12_PIPELINE_STATE_FLAG_RAYQUERY_ALLOW_OPACITY_MICROMAPS_ON_PRE_SM69_SHADERS;
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = m_root.Get();
        pd.CS = {kDxil_rt_shadow, sizeof kDxil_rt_shadow};
        pd.Flags = D3D12_PIPELINE_STATE_FLAGS(flags);
        ComPtr<ID3D12PipelineState> pso;
        if (!m_omm.Create(m_dev.Get(), m_heap.Get(), 5 + RtAlpha::Descriptors() + RtSky::Descriptors() + 1, 5, 6, ommErr)) {
            if (m_log) m_log("[RT] opacity micromaps off: %s", ommErr.c_str());
        } else if (FAILED(m_dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)))) {
            if (m_log) m_log("[RT] opacity micromaps off: the runtime refused the pipeline flag (the system runtime, not the Agility SDK?)");
        } else {
            m_pso = pso;
            m_psoFlags = flags;
            m_ommOn = true;
            if (m_log) m_log("[RT] opacity micromaps on: level %u, 4-state, baked from the alpha masks", RtOmm::kLevel);
        }
    }
#endif
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
    m_meshes.clear(); m_meshOf.clear(); m_build.clear();
    m_blasUsed = 0;
    m_alpha.Reset();
#ifdef RAW_HAS_OMM
    m_omm.Reset();
#endif
    ++m_stats.resets;
}

void RtScene::SetDraws(std::vector<RtDraw>&& draws, std::vector<ID3D11Resource*>& toHand, ID3D12Resource* (*native)(ID3D11Resource*))
{
    for (RtDraw& d : m_draws) ReleaseRtDraw(d);
    m_draws = std::move(draws);
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
    if (m_blasUsed > kBlasHeap - (64ull << 20) || m_meshes.size() > 40000 || (gap && !m_meshes.empty())) Evict();
    for (RtDraw& d : m_draws) {
        const Key k{uintptr_t(d.vb), uintptr_t(d.ib), uint64_t(d.vbOffset) << 32 | d.ibOffset,
                    uint64_t(d.startIndex) << 32 | uint32_t(d.baseVertex) ^ uint64_t(d.indexCount) << 12};
        auto it = m_meshOf.find(k);
        if (it != m_meshOf.end()) { m_meshes[it->second].seen = m_frame; continue; }
        // Over this frame's budget: the mesh is tried again next frame.
        if (m_build.size() >= m_tune.maxNewPerFrame || (d.alphaKind && m_tune.alpha && !m_alpha.Room())) { ++m_stats.skipped; continue; }
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
            if (d.alphaKind) {
                // Alpha-tested: its coordinates and mask join RtAlpha, or the mesh stays out (a solid card).
                if (m_tune.alpha) m.alphaRecord = m_alpha.AddMesh(d, v12, i12, vStart, iStart, t.IndexCount / 3, toHand, native);
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
        m_meshOf.emplace(k, uint32_t(m_meshes.size()));
        if (!m.failed) m_build.push_back(uint32_t(m_meshes.size()));
        m_meshes.push_back(m);
    }
}

bool RtScene::Record(const void* uniform, size_t bytes, ID3D12Resource* depth, ID3D12Resource* normal, ID3D12Resource* spec,
                     ID3D12Resource* motion, ID3D12Resource* colour)
{
    m_recorded = false;
    if (!Active() || !uniform || bytes > kCb) return false;
    const uint32_t slot = uint32_t(m_frame % 3);
    if (m_fence->GetCompletedValue() < m_slotValue[slot]) {   // the slot's last use must be done
        HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_fence->SetEventOnCompletion(m_slotValue[slot], e);
        WaitForSingleObject(e, 2000);
        CloseHandle(e);
    }
    // The newest retired slot's timings.
    for (uint32_t k = 0; k < 3; ++k) {
        if (m_slotValue[k] && m_fence->GetCompletedValue() >= m_slotValue[k] && m_freq) {
            uint64_t* t = nullptr;
            const D3D12_RANGE r{k * 24, k * 24 + 24};
            if (SUCCEEDED(m_tsRead->Map(0, &r, reinterpret_cast<void**>(&t)))) {
                const uint64_t* s = t + k * 3;
                if (s[2] > s[0]) { m_stats.buildMs = double(s[1] - s[0]) * 1e3 / double(m_freq); m_stats.traceMs = double(s[2] - s[1]) * 1e3 / double(m_freq); }
                const D3D12_RANGE none{0, 0};
                m_tsRead->Unmap(0, &none);
            }
        }
    }
    if (!m_freq) {
        ComPtr<ID3D12CommandQueue> q;   // the frequency comes from a queue; any direct queue of the device gives it
        D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT};
        if (SUCCEEDED(m_dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q)))) q->GetTimestampFrequency(&m_freq);
    }
    m_alloc[slot]->Reset();
    m_list->Reset(m_alloc[slot].Get(), nullptr);
    ID3D12GraphicsCommandList4* cl = m_list.Get();
    cl->EndQuery(m_ts.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 3);

    // Alpha-tested meshes first seen this frame: their coordinates and masks.
    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cl->SetDescriptorHeaps(1, heaps);
    bool prepared = m_alpha.Record(cl, slot);
#ifdef RAW_HAS_OMM
    if (m_ommOn) prepared = m_omm.Record(cl, m_tune.alphaRef) || prepared;   // after the masks, before the BLASes
#endif

    // BLAS builds for the new meshes, each from the game's own buffers.
    uint64_t scratchAt = 0;
    uint32_t built = 0;
    uint64_t tris = 0;
    for (uint32_t idx : m_build) {
        Mesh& m = m_meshes[idx];
        D3D12_RAYTRACING_GEOMETRY_DESC g{};
        g.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        g.Flags = m.alphaRecord ? D3D12_RAYTRACING_GEOMETRY_FLAG_NONE : D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        g.Triangles = m.tri;
#ifdef RAW_HAS_OMM
        D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC link{};
        if (m.omm) {
            link = m_omm.Linkage(m.omm);
            g.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES;
            g.OmmTriangles = {&m.tri, &link};
        }
#endif
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
        in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        in.NumDescs = 1; in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY; in.pGeometryDescs = &g;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO pre{};
        m_dev->GetRaytracingAccelerationStructurePrebuildInfo(&in, &pre);
        const uint64_t size = Align(pre.ResultDataMaxSizeInBytes, 256), scratch = Align(pre.ScratchDataSizeInBytes, 256);
        if (m_blasUsed + size > kBlasHeap || scratchAt + scratch > kScratch) { m.failed = true; continue; }
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC b{};
        b.Inputs = in;
        b.DestAccelerationStructureData = m_blasHeap->GetGPUVirtualAddress() + m_blasUsed;
        b.ScratchAccelerationStructureData = m_scratch->GetGPUVirtualAddress() + scratchAt;
        cl->BuildRaytracingAccelerationStructure(&b, 0, nullptr);
        m.blas = m_blasUsed; m.bytes = size; m.built = true;
        m_blasUsed += size; scratchAt += scratch;
        ++built; tris += m.tri.IndexCount / 3;
    }
    D3D12_RESOURCE_BARRIER u = Uav();
    if (built || prepared) cl->ResourceBarrier(1, &u);

    // TLAS: every draw whose mesh has a BLAS, placed by its world matrix.
    auto* inst = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(nullptr);
    const D3D12_RANGE none{0, 0};
    m_inst[slot]->Map(0, &none, reinterpret_cast<void**>(&inst));
    uint32_t n = 0, alphaN = 0;
    uint64_t sceneTris = 0;
    for (const RtDraw& d : m_draws) {
        if (n >= kMaxInstances) break;
        const Key k{uintptr_t(d.vb), uintptr_t(d.ib), uint64_t(d.vbOffset) << 32 | d.ibOffset,
                    uint64_t(d.startIndex) << 32 | uint32_t(d.baseVertex) ^ uint64_t(d.indexCount) << 12};
        auto it = m_meshOf.find(k);
        if (it == m_meshOf.end() || !m_meshes[it->second].built) continue;
        const Mesh& m = m_meshes[it->second];
        if (m.alphaRecord && !m_tune.alpha) continue;   // RtAlpha=0 live: the alpha-tested meshes leave the scene
        D3D12_RAYTRACING_INSTANCE_DESC& x = inst[n++];
        std::memcpy(x.Transform, d.world, sizeof x.Transform);
        x.InstanceID = m.alphaRecord; x.InstanceMask = 0xFF; x.InstanceContributionToHitGroupIndex = 0;
        x.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE |
                  (m.alphaRecord ? D3D12_RAYTRACING_INSTANCE_FLAG_NONE : D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE);
        alphaN += m.alphaRecord != 0;
        x.AccelerationStructure = m_blasHeap->GetGPUVirtualAddress() + m.blas;
        sceneTris += m.tri.IndexCount / 3;
    }
    m_inst[slot]->Unmap(0, nullptr);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC t{};
    t.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    t.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    t.Inputs.NumDescs = n; t.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    t.Inputs.InstanceDescs = m_inst[slot]->GetGPUVirtualAddress();
    t.DestAccelerationStructureData = m_tlas->GetGPUVirtualAddress();
    t.ScratchAccelerationStructureData = m_tlasScratch->GetGPUVirtualAddress();
    cl->BuildRaytracingAccelerationStructure(&t, 0, nullptr);
    D3D12_RESOURCE_BARRIER ut = Uav(m_tlas.Get());
    cl->ResourceBarrier(1, &ut);
    cl->EndQuery(m_ts.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 3 + 1);

    // The trace.
    void* cbp = nullptr;
    m_cb[slot]->Map(0, &none, &cbp);
    std::memcpy(cbp, uniform, bytes);
    m_cb[slot]->Unmap(0, nullptr);
    const UINT inc = m_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    ID3D12Resource* srvs[3] = {depth, normal, spec};
    const DXGI_FORMAT fmts[3] = {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM};
    for (int k = 0; k < 3; ++k) {
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = fmts[k]; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        m_dev->CreateShaderResourceView(srvs[k], &sd, {h.ptr + k * inc});
    }
    ID3D12Resource* outs[2] = {m_vis.Get(), m_lights.Get()};   // descriptor 5 (the alpha coordinates) is RtAlpha's
    for (int k = 0; k < 2; ++k) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = DXGI_FORMAT_R8G8B8A8_UNORM; ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_dev->CreateUnorderedAccessView(outs[k], nullptr, &ud, {h.ptr + (3 + k) * inc});
    }
    const UINT reflAt = 5 + RtAlpha::Descriptors() + RtSky::Descriptors();
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_dev->CreateUnorderedAccessView(m_refl.Get(), nullptr, &ud, {h.ptr + reflAt * inc});
    }
    D3D12_RESOURCE_BARRIER in2[3] = {Transition(outs[0], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                                     Transition(outs[1], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                                     Transition(m_refl.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
    cl->ResourceBarrier(3, in2);
    cl->SetComputeRootSignature(m_root.Get());
    cl->SetPipelineState(m_pso.Get());
    cl->SetComputeRootConstantBufferView(0, m_cb[slot]->GetGPUVirtualAddress());
    const bool sky = m_skyReady && m_tune.sky && colour;
    if (sky) m_sky.SetColour(colour);
    const float rc[8] = {m_tune.sunLength, m_tune.sourceRadius, m_tune.sunCone, float(m_frame & 0xFFFF), m_tune.alphaRef, m_tune.termDist,
                         sky ? m_tune.skyLength : 0.0f, m_tune.debug ? -1.0f : sky && m_tune.reflect ? m_tune.reflectLength : 0.0f};
    cl->SetComputeRoot32BitConstants(1, 8, rc, 0);
    cl->SetComputeRootShaderResourceView(2, m_tlas->GetGPUVirtualAddress());
    D3D12_GPU_DESCRIPTOR_HANDLE g = m_heap->GetGPUDescriptorHandleForHeapStart();
    cl->SetComputeRootDescriptorTable(3, g);
    cl->SetComputeRootDescriptorTable(4, {g.ptr + 3 * inc});
    cl->SetComputeRootShaderResourceView(5, m_alpha.RecordsAddress());
    cl->SetComputeRootDescriptorTable(6, {g.ptr + 6 * inc});
    cl->SetComputeRootDescriptorTable(7, {g.ptr + UINT64(m_skyReady ? m_sky.RawDescriptor() : 3) * inc});   // off: never written
    cl->SetComputeRootDescriptorTable(8, {g.ptr + UINT64(sky ? m_sky.ColourDescriptor() : 0) * inc});         // off: never read
    cl->SetComputeRootDescriptorTable(9, {g.ptr + UINT64(reflAt) * inc});
    cl->Dispatch((m_w + 7) / 8, (m_h + 7) / 8, 1);
    if (sky) m_sky.Record(cl, m_cb[slot]->GetGPUVirtualAddress(), motion, m_frame, 1.0f);
    else m_sky.Reset();
    D3D12_RESOURCE_BARRIER out2[3] = {Transition(outs[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
                                      Transition(outs[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
                                      Transition(m_refl.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON)};
    cl->ResourceBarrier(3, out2);
    cl->EndQuery(m_ts.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 3 + 2);
    cl->ResolveQueryData(m_ts.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 3, 3, m_tsRead.Get(), slot * 24);
    if (FAILED(cl->Close())) return false;
    m_stats.instances = n; m_stats.meshes = uint32_t(m_meshes.size()); m_stats.newMeshes = built;
    m_stats.newTriangles = tris; m_stats.triangles = sceneTris; m_stats.blasBytes = m_blasUsed;
    m_stats.alphaInstances = alphaN; m_stats.alphaMeshes = m_alpha.Meshes(); m_stats.masks = m_alpha.Masks();
    m_stats.noMask = m_alpha.NoMask(); m_stats.uvBytes = m_alpha.PoolBytes();
#ifdef RAW_HAS_OMM
    m_stats.ommArrays = m_omm.Arrays();
#endif
    m_recorded = true;
    return true;
}

bool RtScene::Submit(ID3D12CommandQueue* q)
{
    if (!m_recorded) return false;
    ID3D12CommandList* l = m_list.Get();
    q->ExecuteCommandLists(1, &l);
    q->Signal(m_fence.Get(), ++m_fenceValue);
    m_slotValue[m_frame % 3] = m_fenceValue;
    m_recorded = false;
    return true;
}

RtFrameStats RtScene::Stats() { return m_stats; }

bool RtScene::Reload(const std::vector<uint8_t>& shadow, const std::vector<uint8_t>& sky, std::string& err)
{
    if (!m_ready) { err = "the ray-tracing scene is not running"; return false; }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_root.Get();
    pd.CS = {shadow.data(), shadow.size()};
    pd.Flags = D3D12_PIPELINE_STATE_FLAGS(m_psoFlags);
    ComPtr<ID3D12PipelineState> pso;
    if (FAILED(m_dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)))) { err = "the device refused the reloaded RtShadow.hlsl"; return false; }
    if (m_fence->GetCompletedValue() < m_fenceValue) {
        HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_fence->SetEventOnCompletion(m_fenceValue, e);
        WaitForSingleObject(e, 2000);
        CloseHandle(e);
    }
    if (m_skyReady && !sky.empty() && !m_sky.Reload(sky.data(), sky.size(), err)) return false;
    m_pso = pso;
    return true;
}

} // namespace raw::bridge
