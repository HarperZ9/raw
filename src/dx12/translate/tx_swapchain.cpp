// A D3D12-owned flip-model swap chain behind IDXGISwapChain.
//
// A D3D11 application sees one back buffer whose contents it may keep using.
// Flip-model buffers rotate, so the application draws into a stable texture
// and Present copies (or resolves) it into the buffer that is about to flip.
#include "tx_context.h"

namespace raw::tx {

class SwapChain final : public IDXGISwapChain {
public:
    SwapChain(Device* d) : dev_(d) { dev_->AddRef(); }
    ~SwapChain() { ReleaseBuffers(); dev_->Release(); }
    HRESULT Init(const DXGI_SWAP_CHAIN_DESC& desc);

    STDMETHODIMP QueryInterface(REFIID r, void** o) override
    {
        if (!o) return E_POINTER;
        if (r == __uuidof(IUnknown) || r == __uuidof(IDXGIObject) || r == __uuidof(IDXGIDeviceSubObject) || r == __uuidof(IDXGISwapChain)) {
            *o = static_cast<IDXGISwapChain*>(this);
            AddRef();
            return S_OK;
        }
        *o = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++rc_; }
    STDMETHODIMP_(ULONG) Release() override { ULONG n = --rc_; if (!n) delete this; return n; }
    STDMETHODIMP SetPrivateData(REFGUID g, UINT s, const void* d) override { return priv_.Set(g, s, d); }
    STDMETHODIMP SetPrivateDataInterface(REFGUID g, const IUnknown* p) override { return priv_.SetIface(g, p); }
    STDMETHODIMP GetPrivateData(REFGUID g, UINT* s, void* d) override { return priv_.Get(g, s, d); }
    STDMETHODIMP GetParent(REFIID r, void** o) override { return sc_->GetParent(r, o); }
    STDMETHODIMP GetDevice(REFIID r, void** o) override { return dev_->QueryInterface(r, o); }
    STDMETHODIMP Present(UINT sync, UINT flags) override;
    STDMETHODIMP GetBuffer(UINT i, REFIID r, void** o) override
    {
        if (i != 0 || !front_) return DXGI_ERROR_INVALID_CALL;
        return front_->QueryInterface(r, o);
    }
    STDMETHODIMP SetFullscreenState(BOOL, IDXGIOutput*) override { return S_OK; }   // borderless only
    STDMETHODIMP GetFullscreenState(BOOL* f, IDXGIOutput** o) override
    {
        if (f) *f = FALSE;
        if (o) *o = nullptr;
        return S_OK;
    }
    STDMETHODIMP GetDesc(DXGI_SWAP_CHAIN_DESC* d) override { if (!d) return E_INVALIDARG; *d = desc_; return S_OK; }
    STDMETHODIMP ResizeBuffers(UINT count, UINT w, UINT h, DXGI_FORMAT f, UINT flags) override;
    STDMETHODIMP ResizeTarget(const DXGI_MODE_DESC* m) override { return sc_->ResizeTarget(m); }
    STDMETHODIMP GetContainingOutput(IDXGIOutput** o) override { return sc_->GetContainingOutput(o); }
    STDMETHODIMP GetFrameStatistics(DXGI_FRAME_STATISTICS* s) override { return sc_->GetFrameStatistics(s); }
    STDMETHODIMP GetLastPresentCount(UINT* c) override { return sc_->GetLastPresentCount(c); }

private:
    HRESULT AcquireBuffers();
    void ReleaseBuffers();
    Device* dev_;
    std::atomic<ULONG> rc_{1};
    PrivateStore priv_;
    DXGI_SWAP_CHAIN_DESC desc_{};
    ComPtr<IDXGISwapChain3> sc_;
    std::vector<std::shared_ptr<TrackedRes>> back_;
    ID3D11Texture2D* front_ = nullptr;   // what the application draws into
};

HRESULT SwapChain::Init(const DXGI_SWAP_CHAIN_DESC& desc)
{
    desc_ = desc;
    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = desc.BufferDesc.Width; d.Height = desc.BufferDesc.Height;
    d.Format = StripSrgb(desc.BufferDesc.Format);   // flip-model buffers cannot be sRGB; the stable texture is
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = desc.BufferCount < 2 ? 2 : desc.BufferCount;
    d.Scaling = DXGI_SCALING_STRETCH;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    d.Flags = desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    ComPtr<IDXGISwapChain1> sc1;
    HRESULT hr = dev_->factory->CreateSwapChainForHwnd(dev_->queue.Get(), desc.OutputWindow, &d, nullptr, nullptr, &sc1);
    if (FAILED(hr)) return hr;
    dev_->factory->MakeWindowAssociation(desc.OutputWindow, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(hr = sc1.As(&sc_))) return hr;
    sc_->GetDesc1(&d);
    desc_.BufferDesc.Width = d.Width;
    desc_.BufferDesc.Height = d.Height;
    return AcquireBuffers();
}

HRESULT SwapChain::AcquireBuffers()
{
    DXGI_SWAP_CHAIN_DESC1 d{};
    sc_->GetDesc1(&d);
    for (UINT i = 0; i < d.BufferCount; ++i) {
        auto tr = std::make_shared<TrackedRes>();
        HRESULT hr = sc_->GetBuffer(i, IID_PPV_ARGS(&tr->res));
        if (FAILED(hr)) return hr;
        tr->subs = 1;
        tr->global.assign(1, D3D12_RESOURCE_STATE_PRESENT);
        back_.push_back(std::move(tr));
    }
    D3D11_TEXTURE2D_DESC t{};
    t.Width = d.Width; t.Height = d.Height; t.MipLevels = 1; t.ArraySize = 1;
    t.Format = desc_.BufferDesc.Format;
    t.SampleDesc = desc_.SampleDesc.Count ? desc_.SampleDesc : DXGI_SAMPLE_DESC{1, 0};
    t.BindFlags = D3D11_BIND_RENDER_TARGET | (t.SampleDesc.Count > 1 ? 0 : D3D11_BIND_SHADER_RESOURCE);
    return dev_->CreateTexture2D(&t, nullptr, &front_);
}

void SwapChain::ReleaseBuffers()
{
    dev_->imm->Sync();
    if (front_) { front_->Release(); front_ = nullptr; }
    back_.clear();
    dev_->imm->Sync();     // the retire queue drops its references to the old buffers
    dev_->Reclaim();
}

STDMETHODIMP SwapChain::Present(UINT sync, UINT flags)
{
    Context* ctx = dev_->imm;
    ResCore* src = CoreOf(front_);
    const std::shared_ptr<TrackedRes>& dst = back_[sc_->GetCurrentBackBufferIndex()];
    Tracker& t = ctx->Rec().tracker;
    ID3D12GraphicsCommandList* cl = ctx->Cl();
    if (src->samples > 1) {
        t.Require(src->tr, 0, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        t.Require(dst, 0, D3D12_RESOURCE_STATE_RESOLVE_DEST);
        t.Flush(cl);
        cl->ResolveSubresource(dst->res.Get(), 0, src->Res(), 0, src->format);
    } else {
        t.Require(src->tr, 0, D3D12_RESOURCE_STATE_COPY_SOURCE);
        t.Require(dst, 0, D3D12_RESOURCE_STATE_COPY_DEST);
        t.Flush(cl);
        cl->CopyResource(dst->res.Get(), src->Res());
    }
    t.Require(dst, 0, D3D12_RESOURCE_STATE_PRESENT);
    t.Flush(cl);
    ++ctx->Rec().cmdCount;
    ctx->MarkDirty(D_SCAN);
    ctx->FlushInternal();
    HRESULT hr = sc_->Present(sync, flags);
    dev_->Reclaim();
    return hr;
}

STDMETHODIMP SwapChain::ResizeBuffers(UINT count, UINT w, UINT h, DXGI_FORMAT f, UINT flags)
{
    ReleaseBuffers();
    if (f != DXGI_FORMAT_UNKNOWN) desc_.BufferDesc.Format = f;
    HRESULT hr = sc_->ResizeBuffers(count ? (count < 2 ? 2 : count) : 0, w, h, StripSrgb(f), flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
    if (FAILED(hr)) return hr;
    DXGI_SWAP_CHAIN_DESC1 d{};
    sc_->GetDesc1(&d);
    desc_.BufferDesc.Width = d.Width;
    desc_.BufferDesc.Height = d.Height;
    return AcquireBuffers();
}

HRESULT CreateSwapChain(ID3D11Device* device, const DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** out)
{
    if (!device || !desc || !out) return E_INVALIDARG;
    auto* sc = new SwapChain(Dev(device));
    HRESULT hr = sc->Init(*desc);
    if (FAILED(hr)) { sc->Release(); return hr; }
    *out = sc;
    return S_OK;
}

}  // namespace raw::tx
