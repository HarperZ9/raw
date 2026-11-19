// Root signatures generated from shader bindings, and pipeline state objects
// built from the D3D11 state blocks and cached by the full state key.
#include "tx_context.h"
#include "MipGen_VS.h"
#include "MipGen_PS.h"

namespace raw::tx {

RootSig* Device::GetRootSig(const StageBind sb[5], bool compute)
{
    uint64_t key = Fnv(sb, sizeof(StageBind) * 5, compute ? 0x9E3779B97F4A7C15ull : 1469598103934665603ull);
    std::lock_guard<std::mutex> g(cacheM_);
    auto it = rootSigs_.find(key);
    if (it != rootSigs_.end()) return it->second.get();

    auto rs = std::make_unique<RootSig>();
    rs->compute = compute;
    rs->id = uint32_t(rootSigs_.size()) + 1;
    memcpy(rs->sb, sb, sizeof(rs->sb));
    static const D3D12_SHADER_VISIBILITY vis[5] = {
        D3D12_SHADER_VISIBILITY_VERTEX, D3D12_SHADER_VISIBILITY_HULL, D3D12_SHADER_VISIBILITY_DOMAIN,
        D3D12_SHADER_VISIBILITY_GEOMETRY, D3D12_SHADER_VISIBILITY_PIXEL};
    D3D12_ROOT_PARAMETER params[64];
    D3D12_DESCRIPTOR_RANGE ranges[16];
    UINT np = 0, nr = 0, cost = 0;
    auto table = [&](D3D12_DESCRIPTOR_RANGE_TYPE type, UINT count, D3D12_SHADER_VISIBILITY v) {
        ranges[nr] = D3D12_DESCRIPTOR_RANGE{type, count, 0, 0, 0};
        params[np].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[np].DescriptorTable = {1, &ranges[nr]};
        params[np].ShaderVisibility = v;
        ++nr; ++cost;
        return int8_t(np++);
    };
    for (int st = 0; st < (compute ? 1 : 5); ++st) {
        D3D12_SHADER_VISIBILITY v = compute ? D3D12_SHADER_VISIBILITY_ALL : vis[st];
        auto& p = rs->st[st];
        if (sb[st].srvCount) p.srv = table(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, sb[st].srvCount, v);
        if (sb[st].sampCount) p.samp = table(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, sb[st].sampCount, v);
        if (sb[st].uavCount && (compute || st == 4)) p.uav = table(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, sb[st].uavCount, v);
    }
    for (int st = 0; st < 5; ++st) {
        for (int slot = 0; slot < 14; ++slot) {
            rs->st[st].cb[slot] = -1;
            if (st >= (compute ? 1 : 5) || !(sb[st].cbMask & (1u << slot))) continue;
            if (cost + 2 > 64) { Log("root signature full: constant buffer b%d of stage %d dropped", slot, st); continue; }
            params[np].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[np].Descriptor = {UINT(slot), 0};
            params[np].ShaderVisibility = compute ? D3D12_SHADER_VISIBILITY_ALL : vis[st];
            rs->st[st].cb[slot] = int8_t(np++);
            rs->st[st].cbBits |= uint16_t(1u << slot);
            cost += 2;
        }
    }
    D3D12_ROOT_SIGNATURE_DESC d{};
    d.NumParameters = np;
    d.pParameters = params;
    d.Flags = compute ? D3D12_ROOT_SIGNATURE_FLAG_NONE : D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> blob, err;
    HRESULT hr = TxD3D12SerializeRootSignature(&d, &blob, &err);
    if (SUCCEEDED(hr)) hr = d3d->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rs->rs));
    if (FAILED(hr)) {
        Log("root signature failed 0x%08X %s", hr, err ? static_cast<const char*>(err->GetBufferPointer()) : "");
        return nullptr;
    }
    ++nRootSig;
    RootSig* out = rs.get();
    rootSigs_.emplace(key, std::move(rs));
    return out;
}

static void FillBlend(D3D12_BLEND_DESC& o, const D3D11_BLEND_DESC& b, uint32_t faults)
{
    o.AlphaToCoverageEnable = b.AlphaToCoverageEnable;
    o.IndependentBlendEnable = b.IndependentBlendEnable;
    for (int i = 0; i < 8; ++i) {
        const auto& s = b.RenderTarget[i];
        auto& t = o.RenderTarget[i];
        t.BlendEnable = s.BlendEnable;
        // D3D11_BLEND, D3D11_BLEND_OP and the write mask keep their values in D3D12.
        t.SrcBlend = static_cast<D3D12_BLEND>(s.SrcBlend);
        t.DestBlend = static_cast<D3D12_BLEND>(s.DestBlend);
        t.BlendOp = static_cast<D3D12_BLEND_OP>(s.BlendOp);
        t.SrcBlendAlpha = static_cast<D3D12_BLEND>(s.SrcBlendAlpha);
        t.DestBlendAlpha = static_cast<D3D12_BLEND>(s.DestBlendAlpha);
        t.BlendOpAlpha = static_cast<D3D12_BLEND_OP>(s.BlendOpAlpha);
        t.LogicOp = D3D12_LOGIC_OP_NOOP;
        t.RenderTargetWriteMask = s.RenderTargetWriteMask;
        if (faults & FaultBlendSwap) { std::swap(t.SrcBlend, t.DestBlend); std::swap(t.SrcBlendAlpha, t.DestBlendAlpha); }
    }
}

static void FillRaster(D3D12_RASTERIZER_DESC& o, const D3D11_RASTERIZER_DESC& r)
{
    o.FillMode = static_cast<D3D12_FILL_MODE>(r.FillMode);
    o.CullMode = static_cast<D3D12_CULL_MODE>(r.CullMode);
    o.FrontCounterClockwise = r.FrontCounterClockwise;
    o.DepthBias = r.DepthBias; o.DepthBiasClamp = r.DepthBiasClamp; o.SlopeScaledDepthBias = r.SlopeScaledDepthBias;
    o.DepthClipEnable = r.DepthClipEnable;
    o.MultisampleEnable = r.MultisampleEnable;
    o.AntialiasedLineEnable = r.AntialiasedLineEnable;
}

static D3D12_DEPTH_STENCILOP_DESC Op(const D3D11_DEPTH_STENCILOP_DESC& s)
{
    return {static_cast<D3D12_STENCIL_OP>(s.StencilFailOp), static_cast<D3D12_STENCIL_OP>(s.StencilDepthFailOp),
            static_cast<D3D12_STENCIL_OP>(s.StencilPassOp), static_cast<D3D12_COMPARISON_FUNC>(s.StencilFunc)};
}

static void FillDepth(D3D12_DEPTH_STENCIL_DESC& o, const D3D11_DEPTH_STENCIL_DESC& s, bool hasDsv)
{
    o.DepthEnable = hasDsv && s.DepthEnable;
    o.DepthWriteMask = static_cast<D3D12_DEPTH_WRITE_MASK>(s.DepthWriteMask);
    o.DepthFunc = static_cast<D3D12_COMPARISON_FUNC>(s.DepthFunc);
    o.StencilEnable = hasDsv && s.StencilEnable;
    o.StencilReadMask = s.StencilReadMask; o.StencilWriteMask = s.StencilWriteMask;
    o.FrontFace = Op(s.FrontFace); o.BackFace = Op(s.BackFace);
}

ID3D12PipelineState* Device::GetPso(const PsoKey& key, ShaderData* const sh[5], RootSig* rs)
{
    {
        std::shared_lock<std::shared_mutex> g(psoM_);
        auto it = psos_.find(key);
        if (it != psos_.end()) return it->second.Get();
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = rs->rs.Get();
    D3D12_SHADER_BYTECODE* code[5] = {&d.VS, &d.HS, &d.DS, &d.GS, &d.PS};
    for (int i = 0; i < 5; ++i)
        if (sh[i]) *code[i] = {sh[i]->code.data(), sh[i]->code.size()};
    FillBlend(d.BlendState, BlendDesc(key.blend), faults);
    d.SampleMask = key.sampleMask;
    FillRaster(d.RasterizerState, RasterDesc(key.rs));
    FillDepth(d.DepthStencilState, DepthDesc(key.ds), key.dsFmt != 0);
    if (key.layout) {
        const LayoutData& l = *layouts_[key.layout];
        d.InputLayout = {l.elems.data(), UINT(l.elems.size())};
    }
    d.IBStripCutValue = static_cast<D3D12_INDEX_BUFFER_STRIP_CUT_VALUE>(key.stripCut);
    d.PrimitiveTopologyType = static_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(key.topoType);
    d.NumRenderTargets = key.rtCount;
    for (int i = 0; i < 8; ++i) d.RTVFormats[i] = static_cast<DXGI_FORMAT>(key.rtFmt[i]);
    d.DSVFormat = static_cast<DXGI_FORMAT>(key.dsFmt);
    d.SampleDesc = {key.samples ? key.samples : 1u, key.quality};
    ComPtr<ID3D12PipelineState> pso;
    HRESULT hr = d3d->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso));
    if (FAILED(hr)) Log("pipeline state failed 0x%08X (vs %u ps %u layout %u)", hr, key.sh[0], key.sh[4], key.layout);
    ++nPso;
    std::unique_lock<std::shared_mutex> g(psoM_);
    auto ins = psos_.emplace(key, pso);
    return ins.first->second.Get();
}

ID3D12PipelineState* Device::GetComputePso(ShaderData* cs, RootSig* rs)
{
    {
        std::shared_lock<std::shared_mutex> g(psoM_);
        auto it = csPsos_.find(cs->id);
        if (it != csPsos_.end()) return it->second.Get();
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC d{};
    d.pRootSignature = rs->rs.Get();
    d.CS = {cs->code.data(), cs->code.size()};
    ComPtr<ID3D12PipelineState> pso;
    HRESULT hr = d3d->CreateComputePipelineState(&d, IID_PPV_ARGS(&pso));
    if (FAILED(hr)) Log("compute pipeline state failed 0x%08X (cs %u)", hr, cs->id);
    ++nPso;
    std::unique_lock<std::shared_mutex> g(psoM_);
    auto ins = csPsos_.emplace(cs->id, pso);
    return ins.first->second.Get();
}

// GenerateMips has no D3D12 counterpart: each level is drawn from the level
// above with a full-screen triangle and a linear sampler.
ID3D12PipelineState* Device::GetMipPso(DXGI_FORMAT fmt)
{
    if (!mipRootSig) {
        StageBind sb[5]{};
        sb[4].srvCount = 1; sb[4].sampCount = 1;
        mipRootSig = GetRootSig(sb, false);
    }
    {
        std::shared_lock<std::shared_mutex> g(psoM_);
        auto it = mipPsos_.find(fmt);
        if (it != mipPsos_.end()) return it->second.Get();
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = mipRootSig->rs.Get();
    d.VS = {g_MipGenVS, sizeof(g_MipGenVS)};
    d.PS = {g_MipGenPS, sizeof(g_MipGenPS)};
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.SampleMask = 0xFFFFFFFFu;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = fmt;
    d.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pso;
    if (FAILED(d3d->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso)))) Log("mip pipeline state failed for format %d", int(fmt));
    std::unique_lock<std::shared_mutex> g(psoM_);
    auto ins = mipPsos_.emplace(fmt, pso);
    return ins.first->second.Get();
}

}  // namespace raw::tx
