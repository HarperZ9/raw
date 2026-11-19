// RtScene's water pass (RtWater.hlsl, docs/RAYTRACING.md "Water"): once a frame, after the game composites
// its water into the HDR colour, on the ray-tracing scene the frame's shadows used. Its own command list,
// allocators, constants and timestamps, so it never waits on or reuses what the frame's trace holds; the
// root signature, the descriptor heap (21 descriptors of its own at the end) and the TLAS are the scene's.
// Original work, MIT.
#include "RtScene.h"
#include "RtSceneDetail.h"

#include <cstring>

#include "rt_water.h"
#include "rt_sss.h"

namespace raw::bridge
{
using namespace rtdetail;

namespace
{
constexpr UINT kWaterSlot = 7;   // descriptors a frame in flight: depth, layer, albedo, out, out, coordinates, colour
}

void RtScene::CreateWater()
{
    bool ok = true;
    for (int k = 0; ok && k < 3; ++k)
        ok = Buffer(m_wCb[k], kCb, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false, L"rt water uniform") &&
             SUCCEEDED(m_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_wAlloc[k])));
    ok = ok && Buffer(m_wTsRead, 3 * 2 * 8, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, false, L"rt water timestamps");
    D3D12_QUERY_HEAP_DESC qd{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 6, 0};
    ok = ok && SUCCEEDED(m_dev->CreateQueryHeap(&qd, IID_PPV_ARGS(&m_wTs))) &&
         SUCCEEDED(m_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_wAlloc[0].Get(), nullptr, IID_PPV_ARGS(&m_wList))) &&
         SUCCEEDED(m_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_wFence)));
    if (ok) m_wList->Close();
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_root.Get();
    pd.CS = {kDxil_rt_water, sizeof kDxil_rt_water};
    pd.Flags = D3D12_PIPELINE_STATE_FLAGS(m_psoFlags);   // opacity micromaps where the trace has them
    ok = ok && SUCCEEDED(m_dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_waterPso)));
    if (m_log) m_log(ok ? "[RT] water pass ready (reflection, glint, caustics)" : "[RT] water pass off: its objects or pipeline were refused");
    if (!ok) { m_waterPso.Reset(); return; }
    // The scattering's own small signature: b0 the lighting uniform, b1 four constants, t0..t2, u0.
    D3D12_DESCRIPTOR_RANGE srv{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, 0}, uav{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 3};
    D3D12_ROOT_PARAMETER p[4]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; p[0].Descriptor = {0, 0};
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[1].Constants = {1, 0, 4};
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[2].DescriptorTable = {1, &srv};
    uav.OffsetInDescriptorsFromTableStart = 0;
    p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[3].DescriptorTable = {1, &uav};
    D3D12_ROOT_SIGNATURE_DESC rs{4, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, errs;
    D3D12_COMPUTE_PIPELINE_STATE_DESC sd{};
    sd.CS = {kDxil_rt_sss, sizeof kDxil_rt_sss};
    const bool sssOk = SUCCEEDED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errs)) &&
                       SUCCEEDED(m_dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_sssRoot))) &&
                       (sd.pRootSignature = m_sssRoot.Get(), SUCCEEDED(m_dev->CreateComputePipelineState(&sd, IID_PPV_ARGS(&m_sssPso))));
    if (!sssOk) m_sssPso.Reset();
    if (m_log) m_log(sssOk ? "[RT] skin scattering pass ready" : "[RT] skin scattering off: its pipeline was refused");
}

bool RtScene::RecordWater(const void* uniform, size_t bytes, ID3D12Resource* depth, ID3D12Resource* layer, ID3D12Resource* albedo,
                          ID3D12Resource* spec, ID3D12Resource* colourIn, ID3D12Resource* colourOut, bool water, float sssWidth,
                          float sssStrength, bool* resultInColourIn)
{
    m_wRecorded = false;
    *resultInColourIn = false;
    const bool sss = m_sssPso && sssStrength > 0.0f && spec;
    water = water && m_tlasFrame == m_frame;
    if (!Active() || !m_waterPso || !uniform || bytes > kCb || (!water && !sss) || !depth || !layer || !albedo || !colourIn || !colourOut)
        return false;
    const uint32_t slot = uint32_t(m_frame % 3);
    if (m_wFence->GetCompletedValue() < m_wSlotValue[slot]) {
        HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_wFence->SetEventOnCompletion(m_wSlotValue[slot], e);
        WaitForSingleObject(e, 2000);
        CloseHandle(e);
    }
    if (m_wSlotValue[slot] && m_freq) {   // the slot's last frame is done: its GPU time
        const D3D12_RANGE r{slot * 16u, slot * 16u + 16u};
        uint64_t* t = nullptr;
        if (SUCCEEDED(m_wTsRead->Map(0, &r, reinterpret_cast<void**>(&t)))) {
            const double ms = double(t[slot * 2 + 1] - t[slot * 2]) * 1000.0 / double(m_freq);
            if (ms >= 0.0 && ms < 100.0) { m_stats.waterMs = ms; ++m_stats.waterRuns; }
            const D3D12_RANGE none{0, 0};
            m_wTsRead->Unmap(0, &none);
        }
    }
    const D3D12_RANGE none{0, 0};
    void* cbp = nullptr;
    if (FAILED(m_wCb[slot]->Map(0, &none, &cbp))) return false;
    std::memcpy(cbp, uniform, bytes);
    m_wCb[slot]->Unmap(0, nullptr);

    // This slot's descriptors.
    const UINT inc = m_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const UINT at = m_waterAt + slot * kWaterSlot;
    const D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    auto cpu = [&](UINT k) { return D3D12_CPU_DESCRIPTOR_HANDLE{h.ptr + SIZE_T(at + k) * inc}; };
    auto srv = [&](ID3D12Resource* r, DXGI_FORMAT f, UINT k) {
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = f; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        m_dev->CreateShaderResourceView(r, &sd, cpu(k));
    };
    srv(depth, DXGI_FORMAT_R32_FLOAT, 0);
    srv(layer, DXGI_FORMAT_R32G32B32A32_FLOAT, 1);
    srv(albedo, DXGI_FORMAT_R8G8B8A8_UNORM, 2);
    srv(colourIn, DXGI_FORMAT_R16G16B16A16_FLOAT, 6);
    D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_dev->CreateUnorderedAccessView(colourOut, nullptr, &ud, cpu(3));
    m_dev->CreateUnorderedAccessView(colourOut, nullptr, &ud, cpu(4));
    D3D12_UNORDERED_ACCESS_VIEW_DESC cd{};
    cd.Format = DXGI_FORMAT_R32_TYPELESS; cd.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    cd.Buffer.NumElements = UINT(RtAlpha::kPool / 4); cd.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    m_dev->CreateUnorderedAccessView(m_alpha.Pool(), nullptr, &cd, cpu(5));

    m_wAlloc[slot]->Reset();
    m_wList->Reset(m_wAlloc[slot].Get(), nullptr);
    ID3D12GraphicsCommandList4* cl = m_wList.Get();
    cl->EndQuery(m_wTs.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 2);
    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cl->SetDescriptorHeaps(1, heaps);
    D3D12_RESOURCE_BARRIER in = Transition(colourOut, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cl->ResourceBarrier(1, &in);
    const D3D12_GPU_DESCRIPTOR_HANDLE g = m_heap->GetGPUDescriptorHandleForHeapStart();
    auto gpu = [&](UINT k) { return D3D12_GPU_DESCRIPTOR_HANDLE{g.ptr + UINT64(k) * inc}; };
    if (water) {
    cl->SetComputeRootSignature(m_root.Get());
    cl->SetPipelineState(m_waterPso.Get());
    cl->SetComputeRootConstantBufferView(0, m_wCb[slot]->GetGPUVirtualAddress());
    const float rc[8] = {m_tune.sunLength, m_tune.sourceRadius, m_tune.sunCone, float(m_frame & 0xFFFF), m_tune.alphaRef, m_tune.termDist,
                         0.0f, m_tune.reflect ? m_tune.reflectLength * 2.5f : 0.0f};
    cl->SetComputeRoot32BitConstants(1, 8, rc, 0);
    cl->SetComputeRootShaderResourceView(2, m_tlas->GetGPUVirtualAddress());
    cl->SetComputeRootDescriptorTable(3, gpu(at));
    cl->SetComputeRootDescriptorTable(4, gpu(at + 3));
    cl->SetComputeRootShaderResourceView(5, m_alpha.RecordsAddress());
    cl->SetComputeRootDescriptorTable(6, gpu(6));
    cl->SetComputeRootDescriptorTable(7, gpu(m_skyReady ? m_sky.RawDescriptor() : 3));   // never written by this kernel
    cl->SetComputeRootDescriptorTable(8, gpu(at + 6));
    cl->SetComputeRootDescriptorTable(9, gpu(5 + RtAlpha::Descriptors() + RtSky::Descriptors()));
    cl->SetComputeRootDescriptorTable(10, gpu(5 + RtAlpha::NormalOffset()));
    cl->SetComputeRootDescriptorTable(11, gpu(5 + RtAlpha::AlbedoOffset()));
    cl->SetComputeRootDescriptorTable(12, gpu(m_cache.Descriptor()));
    cl->SetComputeRootShaderResourceView(13, m_alpha.MaterialsAddress());
    const uint32_t giFlags = (m_tune.materials ? 1u : 0u) | (m_cacheReady && m_tune.cache ? 2u : 0u);
    uint32_t gc[8];
    std::memcpy(gc, m_tune.cameraPos, 12);
    std::memcpy(gc + 3, &m_tune.cacheCell, 4);
    gc[4] = RtCache::kSlots; gc[5] = giFlags; gc[6] = 0; gc[7] = uint32_t(m_frame);
    cl->SetComputeRoot32BitConstants(14, 8, gc, 0);
    cl->Dispatch((m_w + 7) / 8, (m_h + 7) / 8, 1);
    }
    if (sss) {
        // In: the water's result (or the frame, without water); out: the other texture.
        ID3D12Resource* src = water ? colourOut : colourIn;
        ID3D12Resource* dst = water ? colourIn : colourOut;
        const UINT sa = m_waterAt + 3 * kWaterSlot + slot * 4;
        auto scpu = [&](UINT k) { return D3D12_CPU_DESCRIPTOR_HANDLE{h.ptr + SIZE_T(sa + k) * inc}; };
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Format = DXGI_FORMAT_R32_FLOAT; m_dev->CreateShaderResourceView(depth, &sd, scpu(0));
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; m_dev->CreateShaderResourceView(spec, &sd, scpu(1));
        sd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; m_dev->CreateShaderResourceView(src, &sd, scpu(2));
        m_dev->CreateUnorderedAccessView(dst, nullptr, &ud, scpu(3));
        D3D12_RESOURCE_BARRIER b[2];
        int nb = 0;
        if (water) {
            b[nb++] = Transition(colourOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            b[nb++] = Transition(colourIn, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        if (nb) cl->ResourceBarrier(nb, b);
        cl->SetComputeRootSignature(m_sssRoot.Get());
        cl->SetPipelineState(m_sssPso.Get());
        cl->SetComputeRootConstantBufferView(0, m_wCb[slot]->GetGPUVirtualAddress());
        const float sc[4] = {sssWidth, sssStrength, float(m_frame & 0xFFFF), 0.0f};
        cl->SetComputeRoot32BitConstants(1, 4, sc, 0);
        cl->SetComputeRootDescriptorTable(2, gpu(sa));
        cl->SetComputeRootDescriptorTable(3, gpu(sa + 3));
        cl->Dispatch((m_w + 7) / 8, (m_h + 7) / 8, 1);
        if (water) {
            b[0] = Transition(colourOut, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
            b[1] = Transition(colourIn, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
            cl->ResourceBarrier(2, b);
        }
        *resultInColourIn = water;
    }
    if (!(sss && water)) {
        D3D12_RESOURCE_BARRIER out = Transition(colourOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
        cl->ResourceBarrier(1, &out);
    }
    cl->EndQuery(m_wTs.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 2 + 1);
    cl->ResolveQueryData(m_wTs.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 2, 2, m_wTsRead.Get(), slot * 16);
    if (FAILED(cl->Close())) return false;
    m_wRecorded = true;
    return true;
}

bool RtScene::SubmitWater(ID3D12CommandQueue* q)
{
    if (!m_wRecorded) return false;
    if (!m_freq) q->GetTimestampFrequency(&m_freq);
    ID3D12CommandList* l = m_wList.Get();
    q->ExecuteCommandLists(1, &l);
    q->Signal(m_wFence.Get(), ++m_wFenceValue);
    m_wSlotValue[m_frame % 3] = m_wFenceValue;
    m_wRecorded = false;
    return true;
}

} // namespace raw::bridge
