// Shaders, state objects, input layouts, samplers and queries.
#include "tx_context.h"
#include "tx_reflect.h"
#include <cfloat>

namespace raw::tx {

ShaderData* Device::InternShader(const void* code, SIZE_T size)
{
    uint64_t h = Fnv(code, size);
    std::lock_guard<std::mutex> g(cacheM_);
    auto it = shaders_.find(h);
    if (it != shaders_.end()) return it->second.get();
    auto sd = std::make_unique<ShaderData>();
    sd->code.assign(static_cast<const uint8_t*>(code), static_cast<const uint8_t*>(code) + size);
    sd->hash = h;
    sd->id = uint32_t(shaders_.size()) + 1;
    if (!ReflectDxbc(code, size, ReflectSource::Auto, *sd)) Log("shader %016llx: no binding data found in the bytecode", h);
    ShaderData* p = sd.get();
    shaders_.emplace(h, std::move(sd));
    return p;
}

template <class I>
HRESULT Device::MakeShader(const void* code, SIZE_T size, I** out)
{
    if (!code || size < 32) return E_INVALIDARG;
    if (!out) return S_FALSE;
    auto* s = new TShader<I>(this);
    s->sd = InternShader(code, size);
    *out = s;
    return S_OK;
}

STDMETHODIMP Device::CreateVertexShader(const void* c, SIZE_T n, ID3D11ClassLinkage*, ID3D11VertexShader** o) { return MakeShader(c, n, o); }
STDMETHODIMP Device::CreateGeometryShader(const void* c, SIZE_T n, ID3D11ClassLinkage*, ID3D11GeometryShader** o) { return MakeShader(c, n, o); }
STDMETHODIMP Device::CreatePixelShader(const void* c, SIZE_T n, ID3D11ClassLinkage*, ID3D11PixelShader** o) { return MakeShader(c, n, o); }
STDMETHODIMP Device::CreateHullShader(const void* c, SIZE_T n, ID3D11ClassLinkage*, ID3D11HullShader** o) { return MakeShader(c, n, o); }
STDMETHODIMP Device::CreateDomainShader(const void* c, SIZE_T n, ID3D11ClassLinkage*, ID3D11DomainShader** o) { return MakeShader(c, n, o); }
STDMETHODIMP Device::CreateComputeShader(const void* c, SIZE_T n, ID3D11ClassLinkage*, ID3D11ComputeShader** o) { return MakeShader(c, n, o); }
STDMETHODIMP Device::CreateGeometryShaderWithStreamOutput(const void*, SIZE_T, const D3D11_SO_DECLARATION_ENTRY*, UINT, const UINT*, UINT, UINT, ID3D11ClassLinkage*, ID3D11GeometryShader**)
{
    TX_TRACE("Device::CreateGeometryShaderWithStreamOutput");
    CountUnsupported(this, "CreateGeometryShaderWithStreamOutput");
    return E_NOTIMPL;
}

template <class D>
uint32_t Device::InternDesc(std::vector<D>& table, const D& d)
{
    std::lock_guard<std::mutex> g(cacheM_);
    for (size_t i = 0; i < table.size(); ++i)
        if (memcmp(&table[i], &d, sizeof(D)) == 0) return uint32_t(i);
    table.push_back(d);
    return uint32_t(table.size() - 1);
}

// Descriptions are rebuilt field by field so padding bytes never differ.
static D3D11_BLEND_DESC Normalize(const D3D11_BLEND_DESC& in)
{
    D3D11_BLEND_DESC d;
    memset(&d, 0, sizeof(d));
    d.AlphaToCoverageEnable = in.AlphaToCoverageEnable ? TRUE : FALSE;
    d.IndependentBlendEnable = in.IndependentBlendEnable ? TRUE : FALSE;
    for (int i = 0; i < 8; ++i) {
        const auto& s = in.RenderTarget[d.IndependentBlendEnable ? i : 0];
        auto& t = d.RenderTarget[i];
        t.BlendEnable = s.BlendEnable ? TRUE : FALSE;
        t.SrcBlend = s.SrcBlend; t.DestBlend = s.DestBlend; t.BlendOp = s.BlendOp;
        t.SrcBlendAlpha = s.SrcBlendAlpha; t.DestBlendAlpha = s.DestBlendAlpha; t.BlendOpAlpha = s.BlendOpAlpha;
        t.RenderTargetWriteMask = s.RenderTargetWriteMask;
    }
    return d;
}

static D3D11_DEPTH_STENCIL_DESC Normalize(const D3D11_DEPTH_STENCIL_DESC& in)
{
    D3D11_DEPTH_STENCIL_DESC d;
    memset(&d, 0, sizeof(d));
    d.DepthEnable = in.DepthEnable ? TRUE : FALSE;
    d.DepthWriteMask = in.DepthWriteMask; d.DepthFunc = in.DepthFunc;
    d.StencilEnable = in.StencilEnable ? TRUE : FALSE;
    d.StencilReadMask = in.StencilReadMask; d.StencilWriteMask = in.StencilWriteMask;
    d.FrontFace = in.FrontFace; d.BackFace = in.BackFace;
    return d;
}

void Device::InitDefaults()
{
    D3D11_BLEND_DESC b{};
    b.RenderTarget[0].SrcBlend = b.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    b.RenderTarget[0].DestBlend = b.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    b.RenderTarget[0].BlendOp = b.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    b.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    InternDesc(blendDescs_, Normalize(b));

    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE; ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; ds.DepthFunc = D3D11_COMPARISON_LESS;
    ds.StencilReadMask = ds.StencilWriteMask = 0xFF;
    ds.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS};
    ds.BackFace = ds.FrontFace;
    InternDesc(dsDescs_, Normalize(ds));

    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode = D3D11_FILL_SOLID; rs.CullMode = D3D11_CULL_BACK; rs.DepthClipEnable = TRUE;
    InternDesc(rsDescs_, rs);

    D3D11_SAMPLER_DESC sd{};   // the D3D11 default sampler: id 0
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxAnisotropy = 1;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 1.0f;
    sd.MinLOD = -FLT_MAX; sd.MaxLOD = FLT_MAX;
    mipSampler_ = InternSampler(sd);
    layouts_.push_back(nullptr);   // layout id 0: none
}

STDMETHODIMP Device::CreateBlendState(const D3D11_BLEND_DESC* d, ID3D11BlendState** out)
{
    TX_TRACE("Device::CreateBlendState");
    if (!d) return E_INVALIDARG;
    if (!out) return S_FALSE;
    auto* s = new TBlend(this);
    s->desc = *d;
    s->id = InternDesc(blendDescs_, Normalize(*d));
    *out = s;
    return S_OK;
}

STDMETHODIMP Device::CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC* d, ID3D11DepthStencilState** out)
{
    TX_TRACE("Device::CreateDepthStencilState");
    if (!d) return E_INVALIDARG;
    if (!out) return S_FALSE;
    auto* s = new TDepthStencil(this);
    s->desc = *d;
    s->id = InternDesc(dsDescs_, Normalize(*d));
    *out = s;
    return S_OK;
}

STDMETHODIMP Device::CreateRasterizerState(const D3D11_RASTERIZER_DESC* d, ID3D11RasterizerState** out)
{
    TX_TRACE("Device::CreateRasterizerState");
    if (!d) return E_INVALIDARG;
    if (!out) return S_FALSE;
    auto* s = new TRaster(this);
    s->desc = *d;
    D3D11_RASTERIZER_DESC n = *d;
    n.FrontCounterClockwise = n.FrontCounterClockwise ? TRUE : FALSE;
    n.DepthClipEnable = n.DepthClipEnable ? TRUE : FALSE; n.ScissorEnable = n.ScissorEnable ? TRUE : FALSE;
    n.MultisampleEnable = n.MultisampleEnable ? TRUE : FALSE; n.AntialiasedLineEnable = n.AntialiasedLineEnable ? TRUE : FALSE;
    s->id = InternDesc(rsDescs_, n);
    *out = s;
    return S_OK;
}

uint32_t Device::InternSampler(const D3D11_SAMPLER_DESC& d)
{
    std::lock_guard<std::mutex> g(cacheM_);
    for (size_t i = 0; i < sampDescs_.size(); ++i)
        if (memcmp(&sampDescs_[i], &d, sizeof(d)) == 0) return uint32_t(i);
    D3D12_SAMPLER_DESC s{};
    s.Filter = static_cast<D3D12_FILTER>(d.Filter);                       // same encoding in both APIs
    s.AddressU = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(d.AddressU);
    s.AddressV = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(d.AddressV);
    s.AddressW = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(d.AddressW);
    s.MipLODBias = d.MipLODBias;
    s.MaxAnisotropy = d.MaxAnisotropy < 1 ? 1 : d.MaxAnisotropy > 16 ? 16 : d.MaxAnisotropy;
    s.ComparisonFunc = d.ComparisonFunc ? static_cast<D3D12_COMPARISON_FUNC>(d.ComparisonFunc) : D3D12_COMPARISON_FUNC_NEVER;
    memcpy(s.BorderColor, d.BorderColor, sizeof(s.BorderColor));
    s.MinLOD = d.MinLOD; s.MaxLOD = d.MaxLOD;
    D3D12_CPU_DESCRIPTOR_HANDLE h = cpuSamp.Alloc();
    d3d->CreateSampler(&s, h);
    sampDescs_.push_back(d);
    sampHandles_.push_back(h);
    return uint32_t(sampDescs_.size() - 1);
}

STDMETHODIMP Device::CreateSamplerState(const D3D11_SAMPLER_DESC* d, ID3D11SamplerState** out)
{
    TX_TRACE("Device::CreateSamplerState");
    if (!d) return E_INVALIDARG;
    if (!out) return S_FALSE;
    auto* s = new TSampler(this);
    s->desc = *d;
    s->id = InternSampler(*d);
    *out = s;
    return S_OK;
}

STDMETHODIMP Device::CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* e, UINT n, const void*, SIZE_T, ID3D11InputLayout** out)
{
    TX_TRACE("Device::CreateInputLayout");
    if (n && !e) return E_INVALIDARG;
    if (!out) return S_FALSE;
    uint64_t h = 1469598103934665603ull ^ n;
    for (UINT i = 0; i < n; ++i) {
        h = Fnv(e[i].SemanticName, strlen(e[i].SemanticName), h);
        h = Fnv(&e[i].SemanticIndex, sizeof(D3D11_INPUT_ELEMENT_DESC) - offsetof(D3D11_INPUT_ELEMENT_DESC, SemanticIndex), h);
    }
    auto* l = new TInputLayout(this);
    std::lock_guard<std::mutex> g(cacheM_);
    auto it = layoutIds_.find(h);
    if (it != layoutIds_.end()) { l->id = it->second; *out = l; return S_OK; }
    auto data = std::make_unique<LayoutData>();
    data->names.resize(n);
    data->elems.resize(n);
    for (UINT i = 0; i < n; ++i) {
        data->names[i] = e[i].SemanticName;
        D3D12_INPUT_ELEMENT_DESC& t = data->elems[i];
        t.SemanticName = data->names[i].c_str();
        t.SemanticIndex = e[i].SemanticIndex; t.Format = e[i].Format; t.InputSlot = e[i].InputSlot;
        t.AlignedByteOffset = e[i].AlignedByteOffset;   // D3D11_APPEND_ALIGNED_ELEMENT has the D3D12 value
        t.InputSlotClass = static_cast<D3D12_INPUT_CLASSIFICATION>(e[i].InputSlotClass);
        t.InstanceDataStepRate = e[i].InstanceDataStepRate;
    }
    l->id = uint32_t(layouts_.size());
    layouts_.push_back(std::move(data));
    layoutIds_.emplace(h, l->id);
    *out = l;
    return S_OK;
}

}  // namespace raw::tx
