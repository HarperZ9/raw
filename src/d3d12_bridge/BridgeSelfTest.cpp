// D3D12Bridge fence self-test: proves the D3D11 -> D3D12 -> CPU handshake
// with CPU waits only, before any D3D11 GPU wait is queued. Original work, MIT.
#include "BridgeImpl.h"

namespace raw::bridge
{

static void LogModuleOf(LogFn log, const char* what, const void* addr)
{
    HMODULE mod = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<const char*>(addr), &mod))
        GetModuleFileNameA(mod, name, MAX_PATH);
    log("[D3D12Bridge] %s at %p in %s", what, addr, name);
}

// Second route for the fence: created on D3D11, opened on D3D12.
bool D3D12Bridge::Impl::FenceFromD3D11()
{
    ComPtr<ID3D11Fence> f11;
    ComPtr<ID3D12Fence> f12;
    HANDLE h = nullptr;
    HRESULT hr = dev11->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&f11));
    if (SUCCEEDED(hr)) hr = f11->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &h);
    if (SUCCEEDED(hr)) { hr = dev12->OpenSharedHandle(h, IID_PPV_ARGS(&f12)); CloseHandle(h); }
    log("[D3D12Bridge] fence created on D3D11 and opened on D3D12: 0x%08X", (unsigned)hr);
    if (FAILED(hr)) return false;
    fence11 = f11;
    fence12 = f12;
    nextValue = 1;
    return true;
}

bool D3D12Bridge::Impl::SelfTest()
{
    // Who answers the D3D11 fence calls: a hooked vtable shows up here.
    void** vtbl = *reinterpret_cast<void***>(ctx11.Get());
    LogModuleOf(log, "ID3D11DeviceContext4::Signal", vtbl[157]);
    LogModuleOf(log, "ID3D11DeviceContext4::Wait", vtbl[158]);
    if (SelfTestOnce()) return true;
    return FenceFromD3D11() && SelfTestOnce();
}

// One attempt: D3D11 signals a, the D3D12 queue waits for a and signals b,
// the CPU waits for b. A failure here cannot stall the game's queue.
bool D3D12Bridge::Impl::SelfTestOnce()
{
    log("[D3D12Bridge] device creation flags 0x%X, feature level 0x%X",
        dev11->GetCreationFlags(), (unsigned)dev11->GetFeatureLevel());
    const uint64_t a = nextValue, b = a + 1;
    // D3D11 signals before the D3D12 queue holds a pending Signal(b): in the
    // game process D3D11 rejected Signal(a) with E_INVALIDARG while a higher
    // value was already pending from D3D12.
    HRESULT hs = ctx11->Signal(fence11.Get(), a);
    ctx11->Flush();
    queue->Wait(fence12.Get(), a);
    queue->Signal(fence12.Get(), b);
    bool d11 = SUCCEEDED(hs) && CpuWait(a, 1000);
    bool d12 = d11 && CpuWait(b, 1000);
    log("[D3D12Bridge] self-test: D3D11 Signal 0x%08X, D3D11->fence %s, D3D12 round trip %s",
        (unsigned)hs, d11 ? "seen" : "NOT seen", d12 ? "ok" : "FAILED");
    if (!d12) { fence12->Signal(b); return false; }
    nextValue = b + 1;
    return true;
}

} // namespace raw::bridge
