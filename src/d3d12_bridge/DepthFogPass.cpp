// DepthFogPass: the first pass on RAW's D3D12 device. A full-screen triangle
// that reads shared colour and depth and writes the shared output.
// Shaders are compiled by fxc at build time (DepthFog_VS.h, DepthFog_PS.h).
// Original work, MIT.
#include "BridgeImpl.h"

#include "DepthFog_VS.h"   // g_DepthFogVS
#include "DepthFog_PS.h"   // g_DepthFogPS

namespace raw::bridge
{
namespace
{
constexpr UINT kSlots = 4;   // submissions in flight: one allocator and two timestamps each

void Transition(ID3D12GraphicsCommandList* cl, ID3D12Resource* r,
                D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    cl->ResourceBarrier(1, &b);
}

class DepthFogPass final : public IBridgePass
{
public:
    const char* Name() const override { return "depth-fog"; }
    ~DepthFogPass() override { if (m_event) CloseHandle(m_event); }

    bool Create(const HostResources& host) override
    {
        m_host = host;
        return CreatePipeline(host.device, host.format[static_cast<UINT>(Shared::ColorOut)], host.log)
            && CreateFrameObjects();
    }

    bool Record(const FrameConstants& frame) override
    {
        const UINT slot = static_cast<UINT>(m_submissions % kSlots);
        if (!WaitFence(m_host.fence, m_slotValue[slot], m_event, 2000)) {
            m_host.log("[DepthFogPass] slot %u did not retire in 2000 ms", slot);
            return false;
        }
        auto* cl = m_list.Get();
        m_alloc[slot]->Reset();
        cl->Reset(m_alloc[slot].Get(), nullptr);
        ID3D12Resource* out = m_host.tex[static_cast<UINT>(Shared::ColorOut)];
        cl->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2 * slot);
        Transition(cl, out, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        ID3D12DescriptorHeap* heaps[] = {m_srvHeap.Get()};
        cl->SetDescriptorHeaps(1, heaps);
        Draw(cl, frame.k);
        Transition(cl, out, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        cl->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2 * slot + 1);
        cl->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2 * slot, 2,
                             m_tsReadback.Get(), sizeof(uint64_t) * 2 * slot);
        return SUCCEEDED(cl->Close());
    }

    bool Submit(uint64_t waitValue, uint64_t signalValue) override
    {
        const UINT slot = static_cast<UINT>(m_submissions % kSlots);
        ID3D12CommandList* lists[] = {m_list.Get()};
        if (FAILED(m_host.queue->Wait(m_host.fence, waitValue))) return false;
        m_host.queue->ExecuteCommandLists(1, lists);
        if (FAILED(m_host.queue->Signal(m_host.fence, signalValue))) return false;
        m_slotValue[slot] = signalValue;
        m_slotSubmission[slot] = m_submissions++;
        m_lastValue = signalValue;
        return true;
    }

    bool Retire(DWORD timeoutMs) override { return WaitFence(m_host.fence, m_lastValue, m_event, timeoutMs); }

    bool LastTimings(PassTimings* out) override
    {
        // The newest slot whose submission the GPU has finished.
        const uint64_t done = m_host.fence->GetCompletedValue();
        int best = -1;
        for (UINT k = 0; k < kSlots; ++k)
            if (m_slotValue[k] && m_slotValue[k] <= done && (best < 0 || m_slotSubmission[k] > m_slotSubmission[best]))
                best = static_cast<int>(k);
        if (best < 0) return false;
        D3D12_RANGE rr{sizeof(uint64_t) * 2 * best, sizeof(uint64_t) * 2 * (best + 1)};
        void* p = nullptr;
        if (FAILED(m_tsReadback->Map(0, &rr, &p))) return false;
        const uint64_t* ts = reinterpret_cast<const uint64_t*>(static_cast<uint8_t*>(p) + rr.Begin);
        const uint64_t a = ts[0], b = ts[1];
        D3D12_RANGE none{0, 0};
        m_tsReadback->Unmap(0, &none);
        *out = {};
        out->submission = m_slotSubmission[best];
        out->totalMs = (b - a) * 1000.0 / static_cast<double>(m_freq);
        out->stageMs[0] = out->totalMs;
        out->stageName[0] = "haze";
        return true;
    }

private:
    // The list, one allocator per slot, views of the shared textures and the
    // timestamp heap: everything the bridge itself used to own for its pass.
    bool CreateFrameObjects()
    {
        ID3D12Device* dev = m_host.device;
        m_host.queue->GetTimestampFrequency(&m_freq);
        for (auto& a : m_alloc)
            if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&a)))) return false;
        if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc[0].Get(), nullptr,
                                          IID_PPV_ARGS(&m_list))))
            return false;
        m_list->Close();
        m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);

        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = 2;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_srvHeap)))) return false;
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.NumDescriptors = 1;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_rtvHeap)))) return false;
        const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels = 1;
        sd.Format = m_host.format[static_cast<UINT>(Shared::ColorIn)];
        dev->CreateShaderResourceView(m_host.tex[static_cast<UINT>(Shared::ColorIn)], &sd, cpu);
        cpu.ptr += inc;
        sd.Format = m_host.format[static_cast<UINT>(Shared::Depth)];
        dev->CreateShaderResourceView(m_host.tex[static_cast<UINT>(Shared::Depth)], &sd, cpu);
        dev->CreateRenderTargetView(m_host.tex[static_cast<UINT>(Shared::ColorOut)], nullptr,
                                    m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        D3D12_QUERY_HEAP_DESC qh{};
        qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qh.Count = 2 * kSlots;
        if (FAILED(dev->CreateQueryHeap(&qh, IID_PPV_ARGS(&m_tsHeap)))) return false;
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = sizeof(uint64_t) * 2 * kSlots;
        rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return m_event && SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_tsReadback)));
    }

    bool CreatePipeline(ID3D12Device* dev, DXGI_FORMAT outFormat, LogFn log)
    {
        HMODULE d3d12 = GetModuleHandleW(L"d3d12.dll");
        auto serialize = d3d12 ? reinterpret_cast<PFN_D3D12_SERIALIZE_ROOT_SIGNATURE>(
            GetProcAddress(d3d12, "D3D12SerializeRootSignature")) : nullptr;
        if (!serialize) { log("[DepthFogPass] D3D12SerializeRootSignature missing"); return false; }

        // Root: 16 constants at b0, one table with t0 (colour) and t1 (depth).
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 2;
        range.BaseShaderRegister = 0;
        range.OffsetInDescriptorsFromTableStart = 0;
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.Num32BitValues = 16;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &range;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 2;
        rsd.pParameters = params;

        ComPtr<ID3DBlob> blob, err;
        HRESULT hr = serialize(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
        if (FAILED(hr)) {
            log("[DepthFogPass] root signature: %s",
                err ? static_cast<const char*>(err->GetBufferPointer()) : "unknown error");
            return false;
        }
        hr = dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                      IID_PPV_ARGS(&m_rs));
        if (FAILED(hr)) { log("[DepthFogPass] CreateRootSignature 0x%08X", (unsigned)hr); return false; }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = m_rs.Get();
        pd.VS = {g_DepthFogVS, sizeof(g_DepthFogVS)};
        pd.PS = {g_DepthFogPS, sizeof(g_DepthFogPS)};
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0] = outFormat;
        pd.SampleDesc.Count = 1;
        hr = dev->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&m_pso));
        if (FAILED(hr)) { log("[DepthFogPass] CreateGraphicsPipelineState 0x%08X", (unsigned)hr); return false; }
        return true;
    }

    void Draw(ID3D12GraphicsCommandList* cl, const PassConstants& k)
    {
        D3D12_VIEWPORT vp{0, 0, static_cast<float>(m_host.width), static_cast<float>(m_host.height), 0, 1};
        D3D12_RECT sc{0, 0, static_cast<LONG>(m_host.width), static_cast<LONG>(m_host.height)};
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        cl->SetPipelineState(m_pso.Get());
        cl->SetGraphicsRootSignature(m_rs.Get());
        cl->SetGraphicsRoot32BitConstants(0, 16, &k, 0);
        cl->SetGraphicsRootDescriptorTable(1, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
        cl->RSSetViewports(1, &vp);
        cl->RSSetScissorRects(1, &sc);
        cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cl->DrawInstanced(3, 1, 0, 0);
    }

    HostResources m_host;
    ComPtr<ID3D12CommandAllocator>    m_alloc[kSlots];
    ComPtr<ID3D12GraphicsCommandList> m_list;
    ComPtr<ID3D12DescriptorHeap>      m_srvHeap, m_rtvHeap;
    ComPtr<ID3D12QueryHeap>           m_tsHeap;
    ComPtr<ID3D12Resource>            m_tsReadback;
    HANDLE   m_event = nullptr;
    uint64_t m_freq = 1, m_submissions = 0, m_lastValue = 0;
    uint64_t m_slotValue[kSlots] = {}, m_slotSubmission[kSlots] = {};
    ComPtr<ID3D12RootSignature> m_rs;
    ComPtr<ID3D12PipelineState> m_pso;
};
} // namespace

std::unique_ptr<IBridgePass> MakeDepthFogPass() { return std::make_unique<DepthFogPass>(); }
#ifndef RAW_ENGINE
std::unique_ptr<IBridgePass> MakeEnginePass() { return nullptr; }
bool EnginePassTune(IBridgePass*, const EngineTuning&) { return false; }
bool EnginePassReadVisibility(IBridgePass*, std::vector<float>*) { return false; }
std::unique_ptr<IBridgePass> MakeRealtimeAoPass() { return nullptr; }
bool RealtimeAoTune(IBridgePass*, const EngineTuning&) { return false; }
bool RealtimeAoReadVisibility(IBridgePass*, std::vector<float>*) { return false; }
#endif

} // namespace raw::bridge
