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
    ~SwapChain() { ReleaseBuffers(); if (waitable_) CloseHandle(waitable_); dev_->Release(); }
    HRESULT Init(const DXGI_SWAP_CHAIN_DESC& desc);

    STDMETHODIMP QueryInterface(REFIID r, void** o) override
    {
        if (!o) return E_POINTER;
        if (r == __uuidof(IUnknown) || r == __uuidof(IDXGIObject) || r == __uuidof(IDXGIDeviceSubObject) || r == __uuidof(IDXGISwapChain)) {
            *o = static_cast<IDXGISwapChain*>(this);
            AddRef();
            return S_OK;
        }
        LogIid("swap chain", r);
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
    HANDLE waitable_ = nullptr;          // frame latency: Present waits here, as D3D11's Present blocks
    UINT latency_ = 0;                   // the value applied to the chain
    bool tearing_ = false;               // the chain allows tearing presents (sync 0 runs uncapped)
    uint64_t presents_ = 0;
    uint64_t frameSerial_[8] = {};       // the submission that ended each of the last 8 frames
    void ApplyLatency();
    UINT ChainFlags() const;
};

HRESULT SwapChain::Init(const DXGI_SWAP_CHAIN_DESC& desc)
{
    desc_ = desc;
    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = desc.BufferDesc.Width; d.Height = desc.BufferDesc.Height;
    d.Format = StripSrgb(desc.BufferDesc.Format);   // flip-model buffers cannot be sRGB; the stable texture is
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    // Three buffers at least: with two, a sync-0 present waits for the queued buffer to leave the screen,
    // which holds the frame rate to the display's refresh even with tearing unavailable.
    d.BufferCount = desc.BufferCount < 3 ? 3 : desc.BufferCount;
    d.Scaling = DXGI_SCALING_STRETCH;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    ComPtr<IDXGIFactory5> f5;
    BOOL tear = FALSE;
    if (SUCCEEDED(dev_->factory.As(&f5)) &&
        SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tear, sizeof(tear))))
        tearing_ = tear != FALSE;
    d.Flags = ChainFlags();
    ComPtr<IDXGISwapChain1> sc1;
    HRESULT hr = dev_->factory->CreateSwapChainForHwnd(dev_->queue.Get(), desc.OutputWindow, &d, nullptr, nullptr, &sc1);
    if (FAILED(hr)) return hr;
    dev_->factory->MakeWindowAssociation(desc.OutputWindow, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(hr = sc1.As(&sc_))) return hr;
    sc_->GetDesc1(&d);
    desc_.BufferDesc.Width = d.Width;
    desc_.BufferDesc.Height = d.Height;
    waitable_ = sc_->GetFrameLatencyWaitableObject();
    ApplyLatency();
    Log("swap chain %ux%u, %u buffers, tearing %s", d.Width, d.Height, d.BufferCount, tearing_ ? "on" : "unavailable");
    return AcquireBuffers();
}

// Tearing only on this chain: the display mode and the system's settings stay as they are.
UINT SwapChain::ChainFlags() const
{
    return (tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u) | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
}

// The device's maximum frame latency (IDXGIDevice1::SetMaximumFrameLatency, 3 unless the application
// sets it) on the chain. A waitable chain defaults to 1, so it is always set.
void SwapChain::ApplyLatency()
{
    const UINT n = dev_->dxgi.latency ? dev_->dxgi.latency : 3;
    if (n != latency_ && SUCCEEDED(sc_->SetMaximumFrameLatency(n))) latency_ = n;
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

void MapStatsLog(uint64_t frames);   // tx_ctx_data.cpp

STDMETHODIMP SwapChain::Present(UINT sync, UINT flags)
{
    TX_TRACE("SwapChain::Present");
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
    ctx->FrameRuns();
    const uint64_t serial = ctx->FlushInternal();
    // Sync 0 is a tearing present when the chain allows it; the flag is invalid with any other interval.
    if (sync == 0 && tearing_ && !(flags & DXGI_PRESENT_TEST)) flags |= DXGI_PRESENT_ALLOW_TEARING;
    HRESULT hr = sc_->Present(sync, flags);
    if (flags & DXGI_PRESENT_TEST) return hr;
    const uint64_t frame = presents_++;
    frameSerial_[frame % 8] = serial;
    if (presents_ % 600 == 0) { MapStatsLog(600); ctx->StreamStatsLog(600); }
    dev_->Reclaim();
    // At most `latency` frames queued: the CPU waits for the oldest to leave the queue, as the system
    // runtime does inside Present.
    ApplyLatency();
    if (sync != 0) {
        if (waitable_) WaitForSingleObjectEx(waitable_, 1000, TRUE);
    } else {
        // With sync 0 the frame-latency object still signals only at the display's refresh (it held every
        // frame-time measurement at 8.33 ms on a 120 Hz display). The bound comes from the queue's own fence
        // instead: the CPU waits for the frame `latency` presents back to finish on the GPU, never for a vblank.
        const uint64_t n = latency_ ? (latency_ < 8 ? latency_ : 7) : 3;
        if (frame >= n) dev_->WaitFor(frameSerial_[(frame - n) % 8]);
    }
    return hr;
}

STDMETHODIMP SwapChain::ResizeBuffers(UINT count, UINT w, UINT h, DXGI_FORMAT f, UINT flags)
{
    TX_TRACE("SwapChain::ResizeBuffers");
    ReleaseBuffers();
    if (f != DXGI_FORMAT_UNKNOWN) desc_.BufferDesc.Format = f;
    // The chain's flags stay those it was made with (tearing cannot be switched by a resize).
    (void)flags;
    HRESULT hr = sc_->ResizeBuffers(count ? (count < 3 ? 3 : count) : 0, w, h, StripSrgb(f), ChainFlags());
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
