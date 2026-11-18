// d3d12.dll and dxgi.dll entry points, resolved at run time. A host DLL that
// links this library keeps its import table as it was.
#include "tx_device.h"
#include <cstdarg>
#include <cstdio>

namespace raw::tx {

static FARPROC Entry(const wchar_t* dll, const char* name)
{
    HMODULE m = LoadLibraryW(dll);
    return m ? GetProcAddress(m, name) : nullptr;
}

HRESULT TxD3D12CreateDevice(IUnknown* adapter, D3D_FEATURE_LEVEL fl, REFIID riid, void** out)
{
    using Fn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    static Fn fn = reinterpret_cast<Fn>(Entry(L"d3d12.dll", "D3D12CreateDevice"));
    return fn ? fn(adapter, fl, riid, out) : E_NOINTERFACE;
}

HRESULT TxD3D12GetDebugInterface(REFIID riid, void** out)
{
    using Fn = HRESULT(WINAPI*)(REFIID, void**);
    static Fn fn = reinterpret_cast<Fn>(Entry(L"d3d12.dll", "D3D12GetDebugInterface"));
    return fn ? fn(riid, out) : E_NOINTERFACE;
}

HRESULT TxD3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC* d, ID3DBlob** blob, ID3DBlob** err)
{
    using Fn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
    static Fn fn = reinterpret_cast<Fn>(Entry(L"d3d12.dll", "D3D12SerializeRootSignature"));
    return fn ? fn(d, D3D_ROOT_SIGNATURE_VERSION_1, blob, err) : E_NOINTERFACE;
}

HRESULT TxCreateDXGIFactory2(UINT flags, REFIID riid, void** out)
{
    using Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
    static Fn fn = reinterpret_cast<Fn>(Entry(L"dxgi.dll", "CreateDXGIFactory2"));
    return fn ? fn(flags, riid, out) : E_NOINTERFACE;
}

void Log(const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    strcat_s(buf, "\n");
    OutputDebugStringA(buf);
    fputs("[tx] ", stderr);
    fputs(buf, stderr);
}

HRESULT PrivateStore::Get(REFGUID g, UINT* size, void* data)
{
    if (!size) return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(m_);
    for (auto& it : items_) {
        if (it.g != g) continue;
        UINT need = it.obj ? UINT(sizeof(IUnknown*)) : UINT(it.bytes.size());
        if (!data) { *size = need; return S_OK; }
        if (*size < need) { *size = need; return DXGI_ERROR_MORE_DATA; }
        if (it.obj) { it.obj->AddRef(); *static_cast<IUnknown**>(data) = it.obj.Get(); }
        else memcpy(data, it.bytes.data(), need);
        *size = need;
        return S_OK;
    }
    *size = 0;
    return DXGI_ERROR_NOT_FOUND;
}

HRESULT PrivateStore::Set(REFGUID g, UINT size, const void* data)
{
    std::lock_guard<std::mutex> lock(m_);
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].g != g) continue;
        items_.erase(items_.begin() + i);
        break;
    }
    if (!data) return S_OK;
    Item it;
    it.g = g;
    it.bytes.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
    items_.push_back(std::move(it));
    return S_OK;
}

HRESULT PrivateStore::SetIface(REFGUID g, const IUnknown* p)
{
    std::lock_guard<std::mutex> lock(m_);
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].g != g) continue;
        items_.erase(items_.begin() + i);
        break;
    }
    if (!p) return S_OK;
    Item it;
    it.g = g;
    it.obj = const_cast<IUnknown*>(p);
    items_.push_back(std::move(it));
    return S_OK;
}

}  // namespace raw::tx
