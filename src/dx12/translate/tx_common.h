// Shared includes, COM base and small helpers for the translation layer.
#pragma once
#include "Translate.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace raw::tx {
using Microsoft::WRL::ComPtr;
class Device;
class Context;

void Log(const char* fmt, ...);
void LogIid(const char* where, REFIID r);
// The last calls into the layer, for a fault report (TraceDump in Translate.h).
extern const char* volatile g_trace[64];
extern std::atomic<uint32_t> g_traceAt;
#define TX_TRACE(name) (g_trace[g_traceAt.fetch_add(1, std::memory_order_relaxed) & 63u] = (name))   // an interface request the layer cannot answer
void DevAddRef(Device* d);
void DevRelease(Device* d);
ID3D11Device* DevAsD3D11(Device* d);
void CountUnsupported(Device* d, const char* what);

inline uint64_t Fnv(const void* p, size_t n, uint64_t h = 1469598103934665603ull)
{
    auto* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

// SetPrivateData storage shared by every object.
class PrivateStore {
public:
    HRESULT Get(REFGUID g, UINT* size, void* data);
    HRESULT Set(REFGUID g, UINT size, const void* data);
    HRESULT SetIface(REFGUID g, const IUnknown* p);
private:
    struct Item { GUID g; std::vector<uint8_t> bytes; ComPtr<IUnknown> obj; };
    std::mutex m_;
    std::vector<Item> items_;
};

// Base of every device child. Two counts, as in D3D11: `rc` is the
// application's, `irc` is held by contexts and views. The object dies when
// both are zero; it holds a device reference only while the application does.
template <class I>
struct Child : I {
    std::atomic<ULONG> rc{1};
    std::atomic<ULONG> irc{0};
    Device* dev;
    PrivateStore priv;

    explicit Child(Device* d) : dev(d) { DevAddRef(d); }
    virtual ~Child() = default;
    virtual bool QIExtra(REFIID, void**) { return false; }

    void IntAddRef() { ++irc; }
    void IntRelease() { if (--irc == 0 && rc == 0) delete this; }

    STDMETHODIMP QueryInterface(REFIID r, void** o) override
    {
        if (!o) return E_POINTER;
        if (r == __uuidof(IUnknown) || r == __uuidof(ID3D11DeviceChild) || r == __uuidof(I)) {
            *o = static_cast<I*>(this); AddRef(); return S_OK;
        }
        if (QIExtra(r, o)) { AddRef(); return S_OK; }
        *o = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override
    {
        ULONG n = ++rc;
        if (n == 1) DevAddRef(dev);
        return n;
    }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG n = --rc;
        if (n == 0) {
            Device* d = dev;
            if (irc == 0) delete this;
            DevRelease(d);
        }
        return n;
    }
    STDMETHODIMP_(void) GetDevice(ID3D11Device** o) override
    {
        *o = DevAsD3D11(dev);
        (*o)->AddRef();
    }
    STDMETHODIMP GetPrivateData(REFGUID g, UINT* s, void* d) override { return priv.Get(g, s, d); }
    STDMETHODIMP SetPrivateData(REFGUID g, UINT s, const void* d) override { return priv.Set(g, s, d); }
    STDMETHODIMP SetPrivateDataInterface(REFGUID g, const IUnknown* p) override { return priv.SetIface(g, p); }
};

// Holds an internal reference to a child.
template <class T>
inline void IntAssign(T*& slot, T* v)
{
    if (slot == v) return;
    if (v) v->IntAddRef();
    if (slot) slot->IntRelease();
    slot = v;
}

}  // namespace raw::tx
