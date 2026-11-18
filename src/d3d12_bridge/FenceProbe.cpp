// FenceProbe: logs whether a D3D11 device can signal monitored fences, and
// what the device and context look like at that moment. Used in the proxy
// right after device creation and again at the first Present, to find where
// ID3D11DeviceContext4::Signal starts failing. Original work, MIT.
#include "D3D12Bridge.h"
#include <wrl/client.h>

namespace raw::bridge
{
using Microsoft::WRL::ComPtr;

static HRESULT TrySignal(ID3D11Device5* d5, ID3D11DeviceContext4* c4, D3D11_FENCE_FLAG flag,
                         uint64_t* completed)
{
    ComPtr<ID3D11Fence> f;
    HRESULT hr = d5->CreateFence(0, flag, IID_PPV_ARGS(&f));
    if (FAILED(hr)) return hr;
    hr = c4->Signal(f.Get(), 1);
    c4->Flush();
    for (int i = 0; i < 200 && f->GetCompletedValue() < 1; ++i) Sleep(1);
    *completed = f->GetCompletedValue();
    return hr;
}

bool ProbeFenceSupport(ID3D11Device* dev, ID3D11DeviceContext* ctx, LogFn log, const char* where)
{
    if (!dev || !log) return false;
    ComPtr<ID3D11DeviceContext> own;
    dev->GetImmediateContext(&own);
    ComPtr<ID3D11Device5> d5;
    ComPtr<ID3D11DeviceContext4> c4;
    HRESULT hd = dev->QueryInterface(IID_PPV_ARGS(&d5));
    HRESULT hc = own->QueryInterface(IID_PPV_ARGS(&c4));
    D3D11_FEATURE_DATA_THREADING th{};
    dev->CheckFeatureSupport(D3D11_FEATURE_THREADING, &th, sizeof(th));
    log("[FenceProbe] %s: tid=%lu dev=%p ctx(given)=%p ctx(own)=%p flags=0x%X fl=0x%X "
        "QI Device5=0x%08X Context4=0x%08X driverCommandLists=%d",
        where, GetCurrentThreadId(), (void*)dev, (void*)ctx, (void*)own.Get(),
        dev->GetCreationFlags(), (unsigned)dev->GetFeatureLevel(), (unsigned)hd, (unsigned)hc,
        (int)th.DriverCommandLists);
    if (FAILED(hd) || FAILED(hc)) return false;
    uint64_t cA = 0, cB = 0;
    HRESULT a = TrySignal(d5.Get(), c4.Get(), D3D11_FENCE_FLAG_NONE, &cA);
    HRESULT b = TrySignal(d5.Get(), c4.Get(), D3D11_FENCE_FLAG_SHARED, &cB);
    log("[FenceProbe] %s: local fence Signal=0x%08X completed=%llu; shared fence Signal=0x%08X completed=%llu",
        where, (unsigned)a, (unsigned long long)cA, (unsigned)b, (unsigned long long)cB);
    return SUCCEEDED(a) && SUCCEEDED(b) && cA == 1 && cB == 1;
}

} // namespace raw::bridge
