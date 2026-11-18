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

// ---- queries ----

HRESULT Device::InitQueries()
{
    static const struct { D3D12_QUERY_HEAP_TYPE type; uint32_t count, stride; } k[QH_Count] = {
        {D3D12_QUERY_HEAP_TYPE_OCCLUSION, 16384, 8},
        {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 4096, 8},
        {D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS, 512, sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS)},
    };
    for (int i = 0; i < QH_Count; ++i) {
        D3D12_QUERY_HEAP_DESC hd{k[i].type, k[i].count, 0};
        HRESULT hr = d3d->CreateQueryHeap(&hd, IID_PPV_ARGS(&qh[i].heap));
        if (FAILED(hr)) return hr;
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_READBACK};
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = UINT64(k[i].count) * k[i].stride; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr = d3d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&qh[i].readback));
        if (FAILED(hr)) return hr;
        void* p = nullptr;
        qh[i].readback->Map(0, nullptr, &p);
        qh[i].mapped = static_cast<uint8_t*>(p);
        qh[i].stride = k[i].stride;
        for (uint32_t s = k[i].count; s-- > 0;) qh[i].free.push_back(s);
    }
    return S_OK;
}

static int HeapOf(D3D11_QUERY q)
{
    switch (q) {
    case D3D11_QUERY_OCCLUSION: case D3D11_QUERY_OCCLUSION_PREDICATE: return QH_Occlusion;
    case D3D11_QUERY_TIMESTAMP: return QH_Timestamp;
    case D3D11_QUERY_PIPELINE_STATISTICS: return QH_Stats;
    default: return -1;
    }
}

uint32_t Device::AllocQuerySlot(QueryHeapKind k)
{
    std::lock_guard<std::mutex> g(queryM);
    if (qh[k].free.empty()) return ~0u;
    uint32_t s = qh[k].free.back();
    qh[k].free.pop_back();
    return s;
}

void Device::FreeQuerySlot(QueryHeapKind k, uint32_t slot)
{
    std::lock_guard<std::mutex> g(queryM);
    qh[k].free.push_back(slot);
}

TQuery::~TQuery()
{
    int h = HeapOf(desc.Query);
    if (h >= 0 && slot != ~0u) dev->FreeQuerySlot(static_cast<QueryHeapKind>(h), slot);
}

STDMETHODIMP_(UINT) TQuery::GetDataSize()
{
    switch (desc.Query) {
    case D3D11_QUERY_EVENT: case D3D11_QUERY_OCCLUSION_PREDICATE: return sizeof(BOOL);
    case D3D11_QUERY_TIMESTAMP_DISJOINT: return sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT);
    case D3D11_QUERY_PIPELINE_STATISTICS: return sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS);
    default: return sizeof(UINT64);
    }
}

STDMETHODIMP Device::CreateQuery(const D3D11_QUERY_DESC* d, ID3D11Query** out)
{
    if (!d) return E_INVALIDARG;
    if (d->Query >= D3D11_QUERY_SO_STATISTICS) {
        CountUnsupported(this, "stream-output statistics queries");
        return E_NOTIMPL;
    }
    if (!out) return S_FALSE;
    auto* q = new TQuery(this);
    q->desc = *d;
    q->slot = ~0u;
    int h = HeapOf(d->Query);
    if (h >= 0) {
        q->slot = AllocQuerySlot(static_cast<QueryHeapKind>(h));
        if (q->slot == ~0u) { q->Release(); return E_OUTOFMEMORY; }
    }
    *out = q;
    return S_OK;
}

STDMETHODIMP Device::CreatePredicate(const D3D11_QUERY_DESC* d, ID3D11Predicate** out)
{
    if (!d || d->Query != D3D11_QUERY_OCCLUSION_PREDICATE) return E_INVALIDARG;
    ID3D11Query* q = nullptr;
    HRESULT hr = CreateQuery(d, out ? &q : nullptr);
    if (out && SUCCEEDED(hr)) *out = static_cast<TQuery*>(q);
    return hr;
}

}  // namespace raw::tx
