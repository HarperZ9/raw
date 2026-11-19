// Shared includes, COM base and small helpers for the translation layer.
#pragma once
#include "Translate.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <atomic>
#include <bit>
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
// A plain load and store, no locked instruction: a lock prefix on every call made each Unmap wait for the
// write-combined bytes the caller had just copied into upload memory to drain. Calls from two threads at
// once may overwrite one entry; the record is for a fault report.
#define TX_TRACE(name)                                                                    do {                                                                                      const uint32_t tx_at_ = g_traceAt.load(std::memory_order_relaxed);                    g_traceAt.store(tx_at_ + 1, std::memory_order_relaxed);                               g_trace[tx_at_ & 63u] = (name);                                                   } while (0)
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

// A fast hash for the per-draw keys (pipeline keys, sampler tables): eight bytes per step with one
// multiply, against FNV's one byte per multiply. Not for anything stored across runs.
inline uint64_t HashBytes(const void* p, size_t n, uint64_t h = 0x9E3779B97F4A7C15ull)
{
    auto* b = static_cast<const uint8_t*>(p);
    for (; n >= 8; n -= 8, b += 8) {
        uint64_t v;
        memcpy(&v, b, 8);
        h = (h ^ v) * 0xFF51AFD7ED558CCDull;
        h ^= h >> 32;
    }
    if (n) {
        uint64_t v = 0;
        memcpy(&v, b, n);
        h = (h ^ v) * 0xFF51AFD7ED558CCDull;
    }
    h ^= h >> 29;
    h *= 0xC4CEB9FE1A85EC53ull;
    return h ^ (h >> 32);
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
