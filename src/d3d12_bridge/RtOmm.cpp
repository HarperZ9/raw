// RtOmm: see the header. Original work, MIT.
#include "RtOmm.h"
#include "OmmIndex.h"
#include "rt_omm_bake.h"   // kDxil_rt_omm_bake (dxc, cs_6_6)

#include <cstring>

namespace raw::bridge
{
using Microsoft::WRL::ComPtr;

namespace
{
constexpr uint64_t Align(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }
}

bool RtOmm::Create(ID3D12Device5* dev, ID3D12DescriptorHeap* heap, uint32_t base, uint32_t pool, uint32_t maskBase, std::string& err)
{
    m_dev = dev; m_heap = heap; m_base = base; m_pool = pool; m_maskBase = maskBase;
    m_inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto buffer = [&](ComPtr<ID3D12Resource>& r, uint64_t bytes, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES s, bool uav, const wchar_t* name) {
        D3D12_HEAP_PROPERTIES hp{type};
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = bytes; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
        d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        d.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, s, nullptr, IID_PPV_ARGS(&r)))) return false;
        r->SetName(name);
        return true;
    };
    const uint32_t cells = 1u << (2 * kLevel);
    if (!buffer(m_input, kInput, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, L"rt omm bits") ||
        !buffer(m_descs, uint64_t(kMaxTriangles) * 8, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, L"rt omm descs") ||
        !buffer(m_arrayHeap, kHeap, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, true, L"rt omm arrays") ||
        !buffer(m_scratch, kScratch, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, L"rt omm scratch") ||
        !buffer(m_iota, uint64_t(kMaxTriangles) * 4, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false, L"rt omm linkage") ||
        !buffer(m_cells, cells * 4ull, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false, L"rt omm order")) {
        err = "omm resources"; return false;
    }
    uint32_t* p = nullptr;
    const D3D12_RANGE none{0, 0};
    if (FAILED(m_iota->Map(0, &none, reinterpret_cast<void**>(&p)))) { err = "omm linkage map"; return false; }
    for (uint32_t i = 0; i < kMaxTriangles; ++i) p[i] = i;
    m_iota->Unmap(0, nullptr);
    // Cells: micromap index -> grid cell (the inverse of OmmTable.h's cell -> index).
    if (FAILED(m_cells->Map(0, &none, reinterpret_cast<void**>(&p)))) { err = "omm order map"; return false; }
    for (uint32_t c = 0; c < cells; ++c) p[raw::omm::IndexOfCell(kLevel, c)] = c;
    m_cells->Unmap(0, nullptr);
    const D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
    ID3D12Resource* raws[2] = {m_input.Get(), m_descs.Get()};
    const UINT counts[2] = {UINT(kInput / 4), kMaxTriangles * 2};
    for (int k = 0; k < 2; ++k) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = counts[k]; ud.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        dev->CreateUnorderedAccessView(raws[k], nullptr, &ud, {cpu.ptr + SIZE_T(base + k) * m_inc});
    }
    // Root: constants (b0), the order (t0), the coordinates (u0), the mask (u1), bits and descriptors (u2, u3).
    D3D12_DESCRIPTOR_RANGE r0{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0}, r1{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0, 0},
                           r2{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 2, 0, 0};
    D3D12_ROOT_PARAMETER rp[5]{};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp[0].Constants = {0, 0, 8};
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; rp[1].Descriptor = {0, 0};
    rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[2].DescriptorTable = {1, &r0};
    rp[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[3].DescriptorTable = {1, &r1};
    rp[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[4].DescriptorTable = {1, &r2};
    D3D12_ROOT_SIGNATURE_DESC rs{5, rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, errs;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errs)) ||
        FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
        err = "omm root signature"; return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_root.Get();
    pd.CS = {kDxil_rt_omm_bake, sizeof kDxil_rt_omm_bake};
    if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_pso)))) { err = "omm bake pipeline"; return false; }
    m_ready = true;
    return true;
}

void RtOmm::Reset()
{
    m_jobs.clear();
    m_wordsUsed = m_descsUsed = m_arrays = 0;
    m_heapUsed = 0;
}

uint64_t RtOmm::Add(uint32_t triangles, uint32_t uvBase, uint32_t mask, uint32_t maskW, uint32_t maskH)
{
    if (!m_ready || !triangles) return 0;
    const uint64_t words = uint64_t(triangles) * kWordsPerTriangle;
    if (uint64_t(m_wordsUsed) + words > kInput / 4 || uint64_t(m_descsUsed) + triangles > kMaxTriangles || triangles > kMaxTriangles) return 0;
    D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY hist{triangles, kLevel, D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_4_STATE};
    D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC ad{};
    ad.NumOmmHistogramEntries = 1; ad.pOmmHistogram = &hist;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
    in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_OPACITY_MICROMAP_ARRAY;
    in.NumDescs = 1; in.pOpacityMicromapArrayDesc = &ad;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO pre{};
    m_dev->GetRaytracingAccelerationStructurePrebuildInfo(&in, &pre);
    const uint64_t bytes = Align(pre.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_BYTE_ALIGNMENT);
    if (!bytes || m_heapUsed + bytes > kHeap || pre.ScratchDataSizeInBytes > kScratch / 8) return 0;
    Job j{triangles, uvBase, mask, maskW, maskH, m_wordsUsed, m_descsUsed, m_arrayHeap->GetGPUVirtualAddress() + m_heapUsed, bytes};
    m_jobs.push_back(j);
    m_wordsUsed += uint32_t(words);
    m_descsUsed += triangles;
    m_heapUsed += bytes;
    ++m_arrays;
    return j.array;
}

bool RtOmm::Record(ID3D12GraphicsCommandList4* cl, float alphaRef)
{
    if (m_jobs.empty()) return false;
    const D3D12_GPU_DESCRIPTOR_HANDLE gpu = m_heap->GetGPUDescriptorHandleForHeapStart();
    D3D12_RESOURCE_BARRIER b[2]{};
    b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;   // RtAlpha's coordinates and masks are written
    cl->ResourceBarrier(1, b);
    cl->SetComputeRootSignature(m_root.Get());
    cl->SetPipelineState(m_pso.Get());
    cl->SetComputeRootShaderResourceView(1, m_cells->GetGPUVirtualAddress());
    cl->SetComputeRootDescriptorTable(2, {gpu.ptr + UINT64(m_pool) * m_inc});
    cl->SetComputeRootDescriptorTable(4, {gpu.ptr + UINT64(m_base) * m_inc});
    for (const Job& j : m_jobs) {
        const uint32_t c[8] = {j.triangles, j.uvBase, j.maskW, j.maskH, j.word, j.desc, uint32_t(alphaRef * 1000.0f + 0.5f), kLevel};
        cl->SetComputeRoot32BitConstants(0, 8, c, 0);
        cl->SetComputeRootDescriptorTable(3, {gpu.ptr + UINT64(m_maskBase + j.mask) * m_inc});
        const uint64_t threads = uint64_t(j.triangles) * kWordsPerTriangle;
        cl->Dispatch(UINT((threads + 63) / 64), 1, 1);
    }
    // The bits and descriptors become build inputs.
    for (int k = 0; k < 2; ++k) {
        b[k].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b[k].Transition = {k ? m_descs.Get() : m_input.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    }
    cl->ResourceBarrier(2, b);
    uint64_t scratchAt = 0;
    for (const Job& j : m_jobs) {
        D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY hist{j.triangles, kLevel, D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_4_STATE};
        D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC ad{};
        ad.NumOmmHistogramEntries = 1; ad.pOmmHistogram = &hist;
        ad.InputBuffer = m_input->GetGPUVirtualAddress();   // the descriptors' ByteOffset counts from here
        ad.PerOmmDescs = {m_descs->GetGPUVirtualAddress() + uint64_t(j.desc) * 8, 8};
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
        in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_OPACITY_MICROMAP_ARRAY;
        in.NumDescs = 1; in.pOpacityMicromapArrayDesc = &ad;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO pre{};
        m_dev->GetRaytracingAccelerationStructurePrebuildInfo(&in, &pre);
        const uint64_t scratch = Align(pre.ScratchDataSizeInBytes, 256);
        if (scratchAt + scratch > kScratch) {   // reuse the scratch from the start after a barrier
            D3D12_RESOURCE_BARRIER u{};
            u.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            cl->ResourceBarrier(1, &u);
            scratchAt = 0;
        }
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC d{};
        d.Inputs = in;
        d.DestAccelerationStructureData = j.array;
        d.ScratchAccelerationStructureData = m_scratch->GetGPUVirtualAddress() + scratchAt;
        cl->BuildRaytracingAccelerationStructure(&d, 0, nullptr);
        scratchAt += scratch;
    }
    for (int k = 0; k < 2; ++k) std::swap(b[k].Transition.StateBefore, b[k].Transition.StateAfter);
    D3D12_RESOURCE_BARRIER all[3] = {b[0], b[1], {}};
    all[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;   // the arrays are built before the BLASes that link them
    cl->ResourceBarrier(3, all);
    m_jobs.clear();
    return true;
}

D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC RtOmm::Linkage(uint64_t array) const
{
    D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC l{};
    l.OpacityMicromapIndexBuffer = {m_iota->GetGPUVirtualAddress(), 4};
    l.OpacityMicromapIndexFormat = DXGI_FORMAT_R32_UINT;
    l.OpacityMicromapBaseLocation = 0;
    l.OpacityMicromapArray = array;
    return l;
}

} // namespace raw::bridge
