// RtScene: see the header. Original work, MIT.
#include "RtScene.h"
#include "rt_shadow.h"   // kDxil_rt_shadow (dxc, cs_6_5)

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
    for (RtDraw& d : m_draws) { if (d.vb) d.vb->Release(); if (d.ib) d.ib->Release(); }
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

bool RtScene::Texture(ComPtr<ID3D12Resource>& r, const wchar_t* name)
{
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = m_w; d.Height = m_h; d.DepthOrArraySize = 1; d.MipLevels = 1; d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(m_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&r)))) return false;
    r->SetName(name);
    return true;
}

bool RtScene::Pipeline(std::string& err)
{
    D3D12_DESCRIPTOR_RANGE srv{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, 0};
    D3D12_DESCRIPTOR_RANGE uav{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 0};   // its own table, set at descriptor 3
    D3D12_ROOT_PARAMETER p[5]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; p[0].Descriptor = {0, 0};
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[1].Constants = {1, 0, 4};
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; p[2].Descriptor = {3, 0};
    p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[3].DescriptorTable = {1, &srv};
    p[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[4].DescriptorTable = {1, &uav};
    D3D12_ROOT_SIGNATURE_DESC rs{5, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
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
    if (o5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1) { err = "DXR tier 1.1 (inline ray queries) not supported"; return false; }
    const auto AS = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, UA = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    bool ok = Buffer(m_blasHeap, kBlasHeap, D3D12_HEAP_TYPE_DEFAULT, AS, true, L"rt blas heap") &&
              Buffer(m_scratch, kScratch, D3D12_HEAP_TYPE_DEFAULT, UA, true, L"rt scratch") &&
              Buffer(m_tlas, kTlas, D3D12_HEAP_TYPE_DEFAULT, AS, true, L"rt tlas") &&
              Buffer(m_tlasScratch, kTlas, D3D12_HEAP_TYPE_DEFAULT, UA, true, L"rt tlas scratch") &&
              Buffer(m_tsRead, 3 * 3 * 8, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, false, L"rt timestamps") &&
              Texture(m_vis, L"rt visibility") && Texture(m_lights, L"rt light index");
    for (int k = 0; ok && k < 3; ++k)
        ok = Buffer(m_inst[k], kMaxInstances * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false, L"rt instances") &&
             Buffer(m_cb[k], kCb, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false, L"rt uniform") &&
             SUCCEEDED(m_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_alloc[k])));
    if (!ok) { err = "resources"; return false; }
    D3D12_QUERY_HEAP_DESC qd{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 9, 0};
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 5, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    if (FAILED(m_dev->CreateQueryHeap(&qd, IID_PPV_ARGS(&m_ts))) || FAILED(m_dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap))) ||
        FAILED(m_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc[0].Get(), nullptr, IID_PPV_ARGS(&m_list))) ||
        FAILED(m_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) { err = "objects"; return false; }
    m_list->Close();
    if (!Pipeline(err)) return false;
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
    ++m_stats.resets;
}

void RtScene::SetDraws(std::vector<RtDraw>&& draws, std::vector<ID3D11Resource*>& toHand, ID3D12Resource* (*native)(ID3D11Resource*))
{
    for (RtDraw& d : m_draws) { if (d.vb) d.vb->Release(); if (d.ib) d.ib->Release(); }
    m_draws = std::move(draws);
    m_build.clear();
    ++m_frame;
    m_stats.draws = uint32_t(m_draws.size());
    m_stats.skipped = 0;
    if (!Active()) return;
    // A full heap, or a cache grown far past the scene (buffers the game rewrites at new offsets every
    // frame add new keys each time): start again.
    if (m_blasUsed > kBlasHeap - (64ull << 20) || m_meshes.size() > 40000) Evict();
    for (RtDraw& d : m_draws) {
        const Key k{uintptr_t(d.vb), uintptr_t(d.ib), uint64_t(d.vbOffset) << 32 | d.ibOffset,
                    uint64_t(d.startIndex) << 32 | uint32_t(d.baseVertex) ^ uint64_t(d.indexCount) << 12};
        auto it = m_meshOf.find(k);
        if (it != m_meshOf.end()) { m_meshes[it->second].seen = m_frame; continue; }
        if (m_build.size() >= m_tune.maxNewPerFrame) { ++m_stats.skipped; continue; }
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
            toHand.push_back(d.vb);
            toHand.push_back(d.ib);
        }
        m_meshOf.emplace(k, uint32_t(m_meshes.size()));
        if (!m.failed) m_build.push_back(uint32_t(m_meshes.size()));
        m_meshes.push_back(m);
    }
}

bool RtScene::Record(const void* uniform, size_t bytes, ID3D12Resource* depth, ID3D12Resource* normal, ID3D12Resource* spec)
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

    // BLAS builds for the new meshes, each from the game's own buffers.
    uint64_t scratchAt = 0;
    uint32_t built = 0;
    uint64_t tris = 0;
    for (uint32_t idx : m_build) {
        Mesh& m = m_meshes[idx];
        D3D12_RAYTRACING_GEOMETRY_DESC g{};
        g.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        g.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        g.Triangles = m.tri;
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
    if (built) cl->ResourceBarrier(1, &u);

    // TLAS: every draw whose mesh has a BLAS, placed by its world matrix.
    auto* inst = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(nullptr);
    const D3D12_RANGE none{0, 0};
    m_inst[slot]->Map(0, &none, reinterpret_cast<void**>(&inst));
    uint32_t n = 0;
    uint64_t sceneTris = 0;
    for (const RtDraw& d : m_draws) {
        if (n >= kMaxInstances) break;
        const Key k{uintptr_t(d.vb), uintptr_t(d.ib), uint64_t(d.vbOffset) << 32 | d.ibOffset,
                    uint64_t(d.startIndex) << 32 | uint32_t(d.baseVertex) ^ uint64_t(d.indexCount) << 12};
        auto it = m_meshOf.find(k);
        if (it == m_meshOf.end() || !m_meshes[it->second].built) continue;
        const Mesh& m = m_meshes[it->second];
        D3D12_RAYTRACING_INSTANCE_DESC& x = inst[n++];
        std::memcpy(x.Transform, d.world, sizeof x.Transform);
        x.InstanceID = 0; x.InstanceMask = 0xFF; x.InstanceContributionToHitGroupIndex = 0;
        x.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE | D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
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
    ID3D12Resource* outs[2] = {m_vis.Get(), m_lights.Get()};
    for (int k = 0; k < 2; ++k) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = DXGI_FORMAT_R8G8B8A8_UNORM; ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_dev->CreateUnorderedAccessView(outs[k], nullptr, &ud, {h.ptr + (3 + k) * inc});
    }
    D3D12_RESOURCE_BARRIER in2[2] = {Transition(outs[0], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                                     Transition(outs[1], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
    cl->ResourceBarrier(2, in2);
    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetComputeRootSignature(m_root.Get());
    cl->SetPipelineState(m_pso.Get());
    cl->SetComputeRootConstantBufferView(0, m_cb[slot]->GetGPUVirtualAddress());
    const float rc[4] = {m_tune.sunLength, m_tune.sourceRadius, m_tune.sunCone, float(m_frame & 0xFFFF)};
    cl->SetComputeRoot32BitConstants(1, 4, rc, 0);
    cl->SetComputeRootShaderResourceView(2, m_tlas->GetGPUVirtualAddress());
    D3D12_GPU_DESCRIPTOR_HANDLE g = m_heap->GetGPUDescriptorHandleForHeapStart();
    cl->SetComputeRootDescriptorTable(3, g);
    cl->SetComputeRootDescriptorTable(4, {g.ptr + 3 * inc});
    cl->Dispatch((m_w + 7) / 8, (m_h + 7) / 8, 1);
    D3D12_RESOURCE_BARRIER out2[2] = {Transition(outs[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
                                      Transition(outs[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON)};
    cl->ResourceBarrier(2, out2);
    cl->EndQuery(m_ts.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 3 + 2);
    cl->ResolveQueryData(m_ts.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 3, 3, m_tsRead.Get(), slot * 24);
    if (FAILED(cl->Close())) return false;
    m_stats.instances = n; m_stats.meshes = uint32_t(m_meshes.size()); m_stats.newMeshes = built;
    m_stats.newTriangles = tris; m_stats.triangles = sceneTris; m_stats.blasBytes = m_blasUsed;
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

} // namespace raw::bridge
