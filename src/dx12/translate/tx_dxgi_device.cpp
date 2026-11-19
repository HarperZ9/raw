// The DXGI side of the layer's device. The system runtime's D3D11 device is also an IDXGIDevice;
// games ask for it for the adapter (video memory) and for the frame latency. It shares the
// device's reference count and private data. Original work, MIT.
#include "tx_context.h"

namespace raw::tx {

using P = Device::DxgiPart;

STDMETHODIMP P::QueryInterface(REFIID r, void** o) { return dev->QueryInterface(r, o); }
STDMETHODIMP_(ULONG) P::AddRef() { return dev->AddRef(); }
STDMETHODIMP_(ULONG) P::Release() { return dev->Release(); }
STDMETHODIMP P::SetPrivateData(REFGUID g, UINT s, const void* d) { return dev->SetPrivateData(g, s, d); }
STDMETHODIMP P::SetPrivateDataInterface(REFGUID g, const IUnknown* p) { return dev->SetPrivateDataInterface(g, p); }
STDMETHODIMP P::GetPrivateData(REFGUID g, UINT* s, void* d) { return dev->GetPrivateData(g, s, d); }
STDMETHODIMP P::GetParent(REFIID r, void** o)
{
    TX_TRACE("DxgiDevice::GetParent");
    return dev->adapter ? dev->adapter->QueryInterface(r, o) : E_FAIL;
}
STDMETHODIMP P::GetAdapter(IDXGIAdapter** a)
{
    TX_TRACE("DxgiDevice::GetAdapter");
    if (!a) return E_INVALIDARG;
    return dev->adapter ? dev->adapter->QueryInterface(IID_PPV_ARGS(a)) : E_FAIL;
}
STDMETHODIMP P::CreateSurface(const DXGI_SURFACE_DESC*, UINT, DXGI_USAGE, const DXGI_SHARED_RESOURCE*, IDXGISurface**)
{
    CountUnsupported(dev, "IDXGIDevice::CreateSurface");
    return E_NOTIMPL;
}
STDMETHODIMP P::QueryResourceResidency(IUnknown* const*, DXGI_RESIDENCY* s, UINT n)
{
    for (UINT i = 0; s && i < n; ++i) s[i] = DXGI_RESIDENCY_FULLY_RESIDENT;
    return S_OK;
}
STDMETHODIMP P::SetGPUThreadPriority(INT p)
{
    if (p < -7 || p > 7) return E_INVALIDARG;
    priority = p;
    return S_OK;
}
STDMETHODIMP P::GetGPUThreadPriority(INT* p) { if (!p) return E_INVALIDARG; *p = priority; return S_OK; }
// Kept and answered back; the layer's swap chains apply it at their next Present (tx_swapchain.cpp).
STDMETHODIMP P::SetMaximumFrameLatency(UINT n)
{
    TX_TRACE("DxgiDevice::SetMaximumFrameLatency");
    if (n > 16) return E_INVALIDARG;
    latency = n ? n : 3;
    return S_OK;
}
STDMETHODIMP P::GetMaximumFrameLatency(UINT* n) { if (!n) return E_INVALIDARG; *n = latency; return S_OK; }
STDMETHODIMP P::OfferResources(UINT, IDXGIResource* const*, DXGI_OFFER_RESOURCE_PRIORITY) { return S_OK; }
STDMETHODIMP P::ReclaimResources(UINT n, IDXGIResource* const*, BOOL* discarded)
{
    for (UINT i = 0; discarded && i < n; ++i) discarded[i] = FALSE;
    return S_OK;
}
// Sets the event once the GPU has finished all work submitted so far.
STDMETHODIMP P::EnqueueSetEvent(HANDLE e)
{
    TX_TRACE("DxgiDevice::EnqueueSetEvent");
    if (!e) return E_INVALIDARG;
    dev->imm->Flush();
    const uint64_t v = dev->LastSubmitted();
    return dev->fence->SetEventOnCompletion(v, e);
}

}  // namespace raw::tx
