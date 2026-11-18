// RecDevice: forwards every ID3D11Device method. Created objects get an id;
// shaders and input layouts also get a copy of their creation data.
#include "rec_api.h"
#include "rec_util.h"
#include "rec_wrappers.h"

namespace raw::rec
{
RecDevice::RecDevice(ID3D11Device* real, ID3D11DeviceContext* realCtx)
    : m_real(real), m_realCtx(realCtx), m_rec(real, realCtx),
      m_ctx(new RecContext(this, realCtx, &m_rec))
{
    m_real->AddRef();
    m_realCtx->AddRef();
    m_shared.store(2);      // one reference on the device wrapper, one on the context wrapper
    Recorder* rec = &m_rec;
    m_real->SetPrivateData(kGuidRecorder, sizeof(rec), &rec);
}

RecDevice::~RecDevice()
{
    m_real->SetPrivateData(kGuidRecorder, 0, nullptr);
    delete m_ctx;
    m_realCtx->Release();
    m_real->Release();
}

ULONG RecDevice::AddShared() { return static_cast<ULONG>(++m_shared); }
ULONG RecDevice::ReleaseShared()
{
    const long left = --m_shared;
    if (left == 0) delete this;
    return static_cast<ULONG>(left);
}

HRESULT RecDevice::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11Device)) {
        *ppv = static_cast<ID3D11Device*>(this);
        AddRef();
        return S_OK;
    }
    return m_real->QueryInterface(riid, ppv);   // newer interfaces are not wrapped
}
ULONG RecDevice::AddRef() { AddShared(); return ++m_refs; }
ULONG RecDevice::Release()
{
    const ULONG r = --m_refs;
    ReleaseShared();
    return r;
}

template <class T>
HRESULT RecDevice::DoneShader(HRESULT hr, T** out, Kind k, const void* code, SIZE_T bytes)
{
    if (SUCCEEDED(hr) && out && *out && code) {
        const u8* b = static_cast<const u8*>(code);
        AttachBlob(*out, kGuidBytecode, std::vector<u8>(b, b + bytes));
    }
    return Done(hr, out, k);
}

HRESULT RecDevice::CreateBuffer(const D3D11_BUFFER_DESC* d, const D3D11_SUBRESOURCE_DATA* init, ID3D11Buffer** out)
{ return Done(m_real->CreateBuffer(d, init, out), out, K_BUFFER); }
HRESULT RecDevice::CreateTexture1D(const D3D11_TEXTURE1D_DESC* d, const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture1D** out)
{ return Done(m_real->CreateTexture1D(d, init, out), out, K_TEX1D); }
HRESULT RecDevice::CreateTexture2D(const D3D11_TEXTURE2D_DESC* d, const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture2D** out)
{ return Done(m_real->CreateTexture2D(d, init, out), out, K_TEX2D); }
HRESULT RecDevice::CreateTexture3D(const D3D11_TEXTURE3D_DESC* d, const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture3D** out)
{ return Done(m_real->CreateTexture3D(d, init, out), out, K_TEX3D); }
HRESULT RecDevice::CreateShaderResourceView(ID3D11Resource* r, const D3D11_SHADER_RESOURCE_VIEW_DESC* d, ID3D11ShaderResourceView** out)
{ return Done(m_real->CreateShaderResourceView(r, d, out), out, K_SRV); }
HRESULT RecDevice::CreateUnorderedAccessView(ID3D11Resource* r, const D3D11_UNORDERED_ACCESS_VIEW_DESC* d, ID3D11UnorderedAccessView** out)
{ return Done(m_real->CreateUnorderedAccessView(r, d, out), out, K_UAV); }
HRESULT RecDevice::CreateRenderTargetView(ID3D11Resource* r, const D3D11_RENDER_TARGET_VIEW_DESC* d, ID3D11RenderTargetView** out)
{ return Done(m_real->CreateRenderTargetView(r, d, out), out, K_RTV); }
HRESULT RecDevice::CreateDepthStencilView(ID3D11Resource* r, const D3D11_DEPTH_STENCIL_VIEW_DESC* d, ID3D11DepthStencilView** out)
{ return Done(m_real->CreateDepthStencilView(r, d, out), out, K_DSV); }

HRESULT RecDevice::CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* e, UINT n, const void* sig, SIZE_T sigBytes, ID3D11InputLayout** out)
{
    const HRESULT hr = m_real->CreateInputLayout(e, n, sig, sigBytes, out);
    if (SUCCEEDED(hr) && out && *out && e && sig)
        AttachBlob(*out, kGuidLayout, PackLayout(e, n, sig, sigBytes));
    return Done(hr, out, K_INPUT_LAYOUT);
}

HRESULT RecDevice::CreateVertexShader(const void* c, SIZE_T n, ID3D11ClassLinkage* l, ID3D11VertexShader** out)
{ return DoneShader(m_real->CreateVertexShader(c, n, l, out), out, K_VS, c, n); }
HRESULT RecDevice::CreateGeometryShader(const void* c, SIZE_T n, ID3D11ClassLinkage* l, ID3D11GeometryShader** out)
{ return DoneShader(m_real->CreateGeometryShader(c, n, l, out), out, K_GS, c, n); }
HRESULT RecDevice::CreatePixelShader(const void* c, SIZE_T n, ID3D11ClassLinkage* l, ID3D11PixelShader** out)
{ return DoneShader(m_real->CreatePixelShader(c, n, l, out), out, K_PS, c, n); }
HRESULT RecDevice::CreateHullShader(const void* c, SIZE_T n, ID3D11ClassLinkage* l, ID3D11HullShader** out)
{ return DoneShader(m_real->CreateHullShader(c, n, l, out), out, K_HS, c, n); }
HRESULT RecDevice::CreateDomainShader(const void* c, SIZE_T n, ID3D11ClassLinkage* l, ID3D11DomainShader** out)
{ return DoneShader(m_real->CreateDomainShader(c, n, l, out), out, K_DS, c, n); }
HRESULT RecDevice::CreateComputeShader(const void* c, SIZE_T n, ID3D11ClassLinkage* l, ID3D11ComputeShader** out)
{ return DoneShader(m_real->CreateComputeShader(c, n, l, out), out, K_CS, c, n); }

HRESULT RecDevice::CreateGeometryShaderWithStreamOutput(const void* c, SIZE_T n, const D3D11_SO_DECLARATION_ENTRY* decl, UINT numEntries, const UINT* strides, UINT numStrides, UINT rasterizedStream, ID3D11ClassLinkage* l, ID3D11GeometryShader** out)
{
    const HRESULT hr = m_real->CreateGeometryShaderWithStreamOutput(c, n, decl, numEntries, strides, numStrides, rasterizedStream, l, out);
    if (SUCCEEDED(hr) && out && *out && (decl || numEntries == 0))
        AttachBlob(*out, kGuidStreamOut, PackStreamOut(decl, numEntries, strides, numStrides, rasterizedStream));
    return DoneShader(hr, out, K_GS, c, n);
}

HRESULT RecDevice::CreateClassLinkage(ID3D11ClassLinkage** out) { return m_real->CreateClassLinkage(out); }
HRESULT RecDevice::CreateBlendState(const D3D11_BLEND_DESC* d, ID3D11BlendState** out)
{ return Done(m_real->CreateBlendState(d, out), out, K_BLEND); }
HRESULT RecDevice::CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC* d, ID3D11DepthStencilState** out)
{ return Done(m_real->CreateDepthStencilState(d, out), out, K_DEPTH_STENCIL); }
HRESULT RecDevice::CreateRasterizerState(const D3D11_RASTERIZER_DESC* d, ID3D11RasterizerState** out)
{ return Done(m_real->CreateRasterizerState(d, out), out, K_RASTERIZER); }
HRESULT RecDevice::CreateSamplerState(const D3D11_SAMPLER_DESC* d, ID3D11SamplerState** out)
{ return Done(m_real->CreateSamplerState(d, out), out, K_SAMPLER); }
HRESULT RecDevice::CreateQuery(const D3D11_QUERY_DESC* d, ID3D11Query** out)
{ return Done(m_real->CreateQuery(d, out), out, K_QUERY); }
HRESULT RecDevice::CreatePredicate(const D3D11_QUERY_DESC* d, ID3D11Predicate** out)
{ return Done(m_real->CreatePredicate(d, out), out, K_PREDICATE); }
HRESULT RecDevice::CreateCounter(const D3D11_COUNTER_DESC* d, ID3D11Counter** out)
{ return Done(m_real->CreateCounter(d, out), out, K_OPAQUE); }

// Deferred contexts are returned unwrapped: their calls are not recorded.
HRESULT RecDevice::CreateDeferredContext(UINT flags, ID3D11DeviceContext** out) { return m_real->CreateDeferredContext(flags, out); }
HRESULT RecDevice::OpenSharedResource(HANDLE h, REFIID riid, void** out) { return m_real->OpenSharedResource(h, riid, out); }
HRESULT RecDevice::CheckFormatSupport(DXGI_FORMAT f, UINT* s) { return m_real->CheckFormatSupport(f, s); }
HRESULT RecDevice::CheckMultisampleQualityLevels(DXGI_FORMAT f, UINT n, UINT* q) { return m_real->CheckMultisampleQualityLevels(f, n, q); }
void RecDevice::CheckCounterInfo(D3D11_COUNTER_INFO* i) { m_real->CheckCounterInfo(i); }
HRESULT RecDevice::CheckCounter(const D3D11_COUNTER_DESC* d, D3D11_COUNTER_TYPE* t, UINT* a, LPSTR n, UINT* nl, LPSTR u, UINT* ul, LPSTR ds, UINT* dl)
{ return m_real->CheckCounter(d, t, a, n, nl, u, ul, ds, dl); }
HRESULT RecDevice::CheckFeatureSupport(D3D11_FEATURE f, void* d, UINT n) { return m_real->CheckFeatureSupport(f, d, n); }
HRESULT RecDevice::GetPrivateData(REFGUID g, UINT* n, void* d) { return m_real->GetPrivateData(g, n, d); }
HRESULT RecDevice::SetPrivateData(REFGUID g, UINT n, const void* d) { return m_real->SetPrivateData(g, n, d); }
HRESULT RecDevice::SetPrivateDataInterface(REFGUID g, const IUnknown* u) { return m_real->SetPrivateDataInterface(g, u); }
D3D_FEATURE_LEVEL RecDevice::GetFeatureLevel() { return m_real->GetFeatureLevel(); }
UINT RecDevice::GetCreationFlags() { return m_real->GetCreationFlags(); }
HRESULT RecDevice::GetDeviceRemovedReason() { return m_real->GetDeviceRemovedReason(); }
HRESULT RecDevice::SetExceptionMode(UINT f) { return m_real->SetExceptionMode(f); }
UINT RecDevice::GetExceptionMode() { return m_real->GetExceptionMode(); }

void RecDevice::GetImmediateContext(ID3D11DeviceContext** out)
{
    if (!out) return;
    m_ctx->AddRef();
    *out = m_ctx;
}

HRESULT Install(ID3D11Device* real, ID3D11DeviceContext* realCtx, ID3D11Device** outDev,
                ID3D11DeviceContext** outCtx)
{
    if (!real || !realCtx || !outDev || !outCtx) return E_INVALIDARG;
    RecDevice* dev = new RecDevice(real, realCtx);
    *outDev = dev;
    *outCtx = dev->Context();
    return S_OK;
}

Recorder* GetRecorder(ID3D11Device* device)
{
    Recorder* rec = nullptr;
    UINT size = sizeof(rec);
    if (!device || FAILED(device->GetPrivateData(kGuidRecorder, &size, &rec)) || size != sizeof(rec))
        return nullptr;
    return rec;
}
} // namespace raw::rec
