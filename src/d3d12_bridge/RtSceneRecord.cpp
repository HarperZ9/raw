// RtScene's frame: the BLAS builds, the TLAS, the trace and the radiance cache's resolve (RtScene.h).
// Original work, MIT.
#include "RtScene.h"
#include "RtSceneDetail.h"

#include <cstring>

namespace raw::bridge
{
using namespace rtdetail;

void RtScene::WaitSlot(uint32_t slot)
{
    if (m_fence->GetCompletedValue() >= m_slotValue[slot]) return;   // the slot's last use must be done
    HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_fence->SetEventOnCompletion(m_slotValue[slot], e);
    WaitForSingleObject(e, 2000);
    CloseHandle(e);
}

void RtScene::ReadTimings()
{
    // The newest retired slot's timings: start, builds done, trace done, cache done.
    for (uint32_t k = 0; k < 3; ++k) {
        if (!m_slotValue[k] || m_fence->GetCompletedValue() < m_slotValue[k] || !m_freq) continue;
        uint64_t* t = nullptr;
        const D3D12_RANGE r{k * 32, k * 32 + 32};
        if (SUCCEEDED(m_tsRead->Map(0, &r, reinterpret_cast<void**>(&t)))) {
            const uint64_t* s = t + k * 4;
            const double ms = 1e3 / double(m_freq);
            if (s[2] > s[0]) { m_stats.buildMs = double(s[1] - s[0]) * ms; m_stats.traceMs = double(s[2] - s[1]) * ms; }
            m_stats.cacheMs = s[3] >= s[2] ? double(s[3] - s[2]) * ms : 0.0;
            const D3D12_RANGE none{0, 0};
            m_tsRead->Unmap(0, &none);
        }
        if (m_cacheReady) m_stats.cacheCells = m_cache.Cells(k);
    }
    if (!m_freq) {
        ComPtr<ID3D12CommandQueue> q;   // the frequency comes from a queue; any direct queue of the device gives it
        D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT};
        if (SUCCEEDED(m_dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q)))) q->GetTimestampFrequency(&m_freq);
    }
}

uint32_t RtScene::BuildBlases(ID3D12GraphicsCommandList4* cl, uint64_t& tris)
{
    // BLAS builds for the new meshes, each from the game's own buffers.
    uint64_t scratchAt = 0;
    uint32_t built = 0;
    for (uint32_t idx : m_build) {
        Mesh& m = m_meshes[idx];
        D3D12_RAYTRACING_GEOMETRY_DESC g{};
        g.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        g.Flags = m.alpha ? D3D12_RAYTRACING_GEOMETRY_FLAG_NONE : D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
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
    return built;
}

uint32_t RtScene::PlaceInstances(uint32_t slot, uint32_t& alphaN, uint64_t& sceneTris)
{
    // TLAS input: every draw whose mesh has a BLAS, placed by its world matrix. The instance ID is the
    // mesh's material record + 1 (0: none); only alpha-tested meshes are non-opaque.
    auto* inst = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(nullptr);
    const D3D12_RANGE none{0, 0};
    m_inst[slot]->Map(0, &none, reinterpret_cast<void**>(&inst));
    uint32_t n = 0;
    for (const RtDraw& d : m_draws) {
        if (n >= kMaxInstances) break;
        const Key k{uintptr_t(d.vb), uintptr_t(d.ib), uint64_t(d.vbOffset) << 32 | d.ibOffset,
                    uint64_t(d.startIndex) << 32 | uint32_t(d.baseVertex) ^ uint64_t(d.indexCount) << 12};
        auto it = m_meshOf.find(k);
        if (it == m_meshOf.end() || !m_meshes[it->second].built) continue;
        const Mesh& m = m_meshes[it->second];
        if (m.alpha && !m_tune.alpha) continue;   // RtAlpha=0 live: the alpha-tested meshes leave the scene
        D3D12_RAYTRACING_INSTANCE_DESC& x = inst[n++];
        std::memcpy(x.Transform, d.world, sizeof x.Transform);
        x.InstanceID = m.alphaRecord; x.InstanceMask = 0xFF; x.InstanceContributionToHitGroupIndex = 0;
        x.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE |
                  (m.alpha ? D3D12_RAYTRACING_INSTANCE_FLAG_NONE : D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE);
        alphaN += m.alpha;
        x.AccelerationStructure = m_blasHeap->GetGPUVirtualAddress() + m.blas;
        sceneTris += m.tri.IndexCount / 3;
    }
    m_inst[slot]->Unmap(0, nullptr);
    return n;
}

void RtScene::BindViews(ID3D12Resource* depth, ID3D12Resource* normal, ID3D12Resource* spec)
{
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
    D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_dev->CreateUnorderedAccessView(m_refl.Get(), nullptr, &ud, {h.ptr + (5 + RtAlpha::Descriptors() + RtSky::Descriptors()) * inc});
}

bool RtScene::Record(const void* uniform, size_t bytes, ID3D12Resource* depth, ID3D12Resource* normal, ID3D12Resource* spec,
                     ID3D12Resource* motion, ID3D12Resource* colour)
{
    m_recorded = false;
    if (!Active() || !uniform || bytes > kCb) return false;
    const uint32_t slot = uint32_t(m_frame % 3);
    WaitSlot(slot);
    ReadTimings();
    m_alloc[slot]->Reset();
    m_list->Reset(m_alloc[slot].Get(), nullptr);
    ID3D12GraphicsCommandList4* cl = m_list.Get();
    cl->EndQuery(m_ts.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 4);

    // Meshes first seen this frame: their coordinates, normals, masks and colour copies.
    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cl->SetDescriptorHeaps(1, heaps);
    bool prepared = m_alpha.Record(cl, slot);
#ifdef RAW_HAS_OMM
    if (m_ommOn) prepared = m_omm.Record(cl, m_tune.alphaRef) || prepared;   // after the masks, before the BLASes
#endif
    uint64_t tris = 0;
    const uint32_t built = BuildBlases(cl, tris);
    D3D12_RESOURCE_BARRIER u = Uav();
    if (built || prepared) cl->ResourceBarrier(1, &u);

    uint32_t alphaN = 0;
    uint64_t sceneTris = 0;
    const uint32_t n = PlaceInstances(slot, alphaN, sceneTris);
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
    cl->EndQuery(m_ts.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 4 + 1);

    // The trace.
    const D3D12_RANGE none{0, 0};
    void* cbp = nullptr;
    m_cb[slot]->Map(0, &none, &cbp);
    std::memcpy(cbp, uniform, bytes);
    m_cb[slot]->Unmap(0, nullptr);
    BindViews(depth, normal, spec);
    ID3D12Resource* outs[3] = {m_vis.Get(), m_lights.Get(), m_refl.Get()};
    D3D12_RESOURCE_BARRIER in3[3], out3[3];
    for (int k = 0; k < 3; ++k) {
        in3[k] = Transition(outs[k], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        out3[k] = Transition(outs[k], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    }
    cl->ResourceBarrier(3, in3);
    const bool sky = m_skyReady && m_tune.sky && colour;
    if (sky) m_sky.SetColour(colour);
    const bool cache = m_cacheReady && m_tune.cache && sky;
    RecordTrace(cl, slot, sky, cache);
    if (sky) m_sky.Record(cl, m_cb[slot]->GetGPUVirtualAddress(), motion, m_frame, 1.0f);
    else m_sky.Reset();
    cl->ResourceBarrier(3, out3);
    cl->EndQuery(m_ts.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 4 + 2);
    if (cache) m_cache.Record(cl, m_frame, slot);
    cl->EndQuery(m_ts.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 4 + 3);
    cl->ResolveQueryData(m_ts.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 4, 4, m_tsRead.Get(), slot * 32);
    if (FAILED(cl->Close())) return false;
    m_stats.instances = n; m_stats.meshes = uint32_t(m_meshes.size()); m_stats.newMeshes = built;
    m_stats.newTriangles = tris; m_stats.triangles = sceneTris; m_stats.blasBytes = m_blasUsed;
    m_stats.alphaInstances = alphaN; m_stats.alphaMeshes = m_alpha.AlphaMeshes(); m_stats.masks = m_alpha.Masks();
    m_stats.noMask = m_alpha.NoMask(); m_stats.uvBytes = m_alpha.PoolBytes();
    m_stats.matMeshes = m_alpha.Meshes(); m_stats.albedos = m_alpha.Albedos(); m_stats.noAlbedo = m_alpha.NoAlbedo();
    m_stats.albedoBytes = m_alpha.AlbedoBytes();
#ifdef RAW_HAS_OMM
    m_stats.ommArrays = m_omm.Arrays();
#endif
    m_recorded = true;
    return true;
}

void RtScene::RecordTrace(ID3D12GraphicsCommandList4* cl, uint32_t slot, bool sky, bool cache)
{
    const UINT inc = m_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const UINT reflAt = 5 + RtAlpha::Descriptors() + RtSky::Descriptors();
    cl->SetComputeRootSignature(m_root.Get());
    cl->SetPipelineState(m_pso.Get());
    cl->SetComputeRootConstantBufferView(0, m_cb[slot]->GetGPUVirtualAddress());
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
    cl->SetComputeRootDescriptorTable(10, {g.ptr + UINT64(5 + RtAlpha::NormalOffset()) * inc});
    cl->SetComputeRootDescriptorTable(11, {g.ptr + UINT64(5 + RtAlpha::AlbedoOffset()) * inc});
    cl->SetComputeRootDescriptorTable(12, {g.ptr + UINT64(m_cache.Descriptor()) * inc});
    cl->SetComputeRootShaderResourceView(13, m_alpha.MaterialsAddress());
    // b2: the camera's place, the finest cell, the table size, what runs (1 materials, 2 cache), the
    // debug view, the frame.
    const uint32_t giFlags = (m_tune.materials ? 1u : 0u) | (cache ? 2u : 0u);
    uint32_t gc[8];
    std::memcpy(gc, m_tune.cameraPos, 12);
    std::memcpy(gc + 3, &m_tune.cacheCell, 4);
    gc[4] = RtCache::kSlots; gc[5] = giFlags; gc[6] = uint32_t(m_tune.debugView); gc[7] = uint32_t(m_frame);
    cl->SetComputeRoot32BitConstants(14, 8, gc, 0);
    cl->Dispatch((m_w + 7) / 8, (m_h + 7) / 8, 1);
}

} // namespace raw::bridge
