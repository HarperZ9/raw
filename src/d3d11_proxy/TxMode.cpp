// TxMode: see the header. Original work, MIT.
#include "TxMode.h"
#include "ProxyAPI.h"
#include "ProxyLog.h"
#include "Translate.h"
#include "../d3d12_bridge/HardwareProfile.h"

#include <psapi.h>
#include <string>

namespace SB::Proxy
{
namespace
{
using PFN_CreateDev = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
                                       ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
using PFN_CreateDevSc = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
                                         const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*,
                                         ID3D11DeviceContext**);

struct TxConfig {
    int mode = -1;
    bool debugLayer = false, wrap = false, renameRing = true, ringWriteBack = false, geometrySubdiv = false;
    char agilityPath[MAX_PATH] = {};
    UINT agilityVersion = 0;
    UINT workers = 0, segmentDraws = 256, segmentMin = 64;
};
TxConfig& C() { static TxConfig c; return c; }

void Load()
{
    if (C().mode >= 0) return;
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string ini(path);
    ini = ini.substr(0, ini.rfind('\\') + 1) + "d3d11_proxy.ini";
    const bool on = GetPrivateProfileIntA("Translate", "Enabled", 0, ini.c_str()) != 0;
    const bool native = GetPrivateProfileIntA("Translate", "Native", 0, ini.c_str()) != 0;
    C().mode = on ? (native ? 2 : 1) : 0;
    C().debugLayer = GetPrivateProfileIntA("Translate", "DebugLayer", 0, ini.c_str()) != 0;
    C().wrap = GetPrivateProfileIntA("Translate", "Wrap", 0, ini.c_str()) != 0;
    // [Translate] AgilityPath (the folder of the Agility SDK's D3D12Core.dll) and AgilityVersion (its SDK version).
    GetPrivateProfileStringA("Translate", "AgilityPath", "", C().agilityPath, MAX_PATH, ini.c_str());
    C().agilityVersion = GetPrivateProfileIntA("Translate", "AgilityVersion", 0, ini.c_str());
    // [Translate] RenameRing (default 1) and RingWriteBack (default 0): how buffer renames get their memory.
    C().renameRing = GetPrivateProfileIntA("Translate", "RenameRing", 1, ini.c_str()) != 0;
    C().ringWriteBack = GetPrivateProfileIntA("Translate", "RingWriteBack", 0, ini.c_str()) != 0;
    // [Translate] Workers (0=single-threaded, -1=auto from CPU core count, >0=explicit worker threads).
    const int rawWorkers = GetPrivateProfileIntA("Translate", "Workers", 0, ini.c_str());
    C().workers = (rawWorkers < 0) ? raw::bridge::RecommendTxWorkers() : static_cast<UINT>(rawWorkers);
    C().segmentDraws = GetPrivateProfileIntA("Translate", "SegmentDraws", 256, ini.c_str());
    C().segmentMin = GetPrivateProfileIntA("Translate", "SegmentMin", 64, ini.c_str());   // shorter runs stay on the game's thread
    // Opt-in only (default 0): Skyrim SE's position-only Depth Prepass does not match NORMAL-bearing Opaque layouts,
    // so enabling vertex subdivision on Skyrim SE causes DEPTH_EQUAL rejection holes across terrain, roofs, and weapons.
    C().geometrySubdiv = GetPrivateProfileIntA("Translate", "GeometrySubdiv", 0, ini.c_str()) != 0;
    if (C().mode) Log("[Tx] translate mode %s%s (workers=%u%s, subdiv=%d)", C().mode == 1 ? "on: the game runs on raw::tx (D3D12)" : "native: system D3D11 behind the harness wrapper",
                      C().debugLayer ? ", D3D12 debug layer on" : "", C().workers, rawWorkers < 0 ? " auto" : "", int(C().geometrySubdiv));
}

void TxLog(const char* line) { Log("[tx] %s", line); }

// First-chance faults inside this module or the game, with the module and offset, so a crash in
// the layer can be found in its map file. Logged only; the search goes on to the game's handlers.
LONG CALLBACK Fault(EXCEPTION_POINTERS* e)
{
    static volatile LONG logged = 0;
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW &&
        code != 0xC0000409 && code != EXCEPTION_INT_DIVIDE_BY_ZERO)
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&logged) > 12) return EXCEPTION_CONTINUE_SEARCH;
    void* at = e->ExceptionRecord->ExceptionAddress;
    HMODULE mod = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)at, &mod))
        GetModuleFileNameA(mod, name, MAX_PATH);
    const char* base = strrchr(name, '\\');
    Log("[Tx] fault 0x%08lX at %s+0x%llX (thread %lu), address read/written 0x%llX", code, base ? base + 1 : name,
        (unsigned long long)((uintptr_t)at - (uintptr_t)mod), GetCurrentThreadId(),
        e->ExceptionRecord->NumberParameters > 1 ? (unsigned long long)e->ExceptionRecord->ExceptionInformation[1] : 0ull);
    const CONTEXT* r = e->ContextRecord;
    Log("[Tx]   rax %llX rbx %llX rcx %llX rdx %llX rsi %llX rdi %llX rbp %llX rsp %llX", r->Rax, r->Rbx, r->Rcx, r->Rdx, r->Rsi, r->Rdi,
        r->Rbp, r->Rsp);
    Log("[Tx]   r8 %llX r9 %llX r10 %llX r11 %llX r12 %llX r13 %llX r14 %llX r15 %llX", r->R8, r->R9, r->R10, r->R11, r->R12, r->R13,
        r->R14, r->R15);
    // The code around the fault and before each caller's return address (the game's code is
    // decrypted only in memory), as hex for an offline disassembler.
    auto dump = [](const char* what, const uint8_t* p, int before, int after) {
        char hex[3 * 160 + 1] = {};
        int k = 0;
        for (int i = -before; i < after && k < 3 * 160; ++i) {
            if (IsBadReadPtr(p + i, 1)) { k += sprintf_s(hex + k, sizeof hex - k, "?? "); continue; }
            k += sprintf_s(hex + k, sizeof hex - k, "%02X ", p[i]);
        }
        Log("[Tx]   %s (from -%d): %s", what, before, hex);
    };
    dump("code", static_cast<const uint8_t*>(at), 96, 32);
    raw::tx::TraceDump([](const char* line) { Log("[Tx]   last calls into the layer: %s", line); });
    // A few return addresses from the stack, for the module offsets of the callers.
    void* frames[12] = {};
    const USHORT n = RtlCaptureStackBackTrace(0, 12, frames, nullptr);
    for (USHORT i = 0; i < n; ++i) {
        HMODULE m = nullptr;
        char mn[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)frames[i], &m))
            GetModuleFileNameA(m, mn, MAX_PATH);
        const char* b = strrchr(mn, '\\');
        Log("[Tx]   %u %s+0x%llX", i, b ? b + 1 : mn, (unsigned long long)((uintptr_t)frames[i] - (uintptr_t)m));
        if (i >= 4 && i <= 7 && b && _stricmp(b + 1, "SkyrimSE.exe") == 0) dump("before return", static_cast<const uint8_t*>(frames[i]), 64, 0);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

HRESULT MakeTx(IDXGIAdapter* adapter, ID3D11Device** dev, ID3D11DeviceContext** ctx, D3D_FEATURE_LEVEL* level)
{
    raw::tx::SetLogSink(&TxLog);
    static PVOID handler = AddVectoredExceptionHandler(0, &Fault);
    (void)handler;
    raw::tx::CreateParams p;
    p.debugLayer = C().debugLayer;
    p.agilityPath = C().agilityPath;
    p.agilityVersion = C().agilityVersion;
    p.renameRing = C().renameRing;
    p.ringWriteBack = C().ringWriteBack;
    p.workers = C().workers;
    p.segmentDraws = C().segmentDraws;
    p.segmentMin = C().segmentMin;
    p.geometrySubdiv = C().geometrySubdiv;
    DXGI_ADAPTER_DESC ad{};
    if (adapter && SUCCEEDED(adapter->GetDesc(&ad))) p.adapter = &ad.AdapterLuid;
    ID3D11Device* d = nullptr;
    ID3D11DeviceContext* c = nullptr;
    const HRESULT hr = raw::tx::CreateDevice(p, &d, &c);
    Log("[Tx] raw::tx::CreateDevice: 0x%08X (adapter %ls)", (unsigned)hr, adapter ? ad.Description : L"default");
    if (FAILED(hr)) return hr;
    if (level) *level = D3D_FEATURE_LEVEL_11_0;
    if (dev) *dev = d; else d->Release();
    if (ctx) *ctx = c; else c->Release();
    return hr;
}

HRESULT WINAPI TxCreateFn(IDXGIAdapter* adapter, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
                          const DXGI_SWAP_CHAIN_DESC* scDesc, IDXGISwapChain** sc, ID3D11Device** dev, D3D_FEATURE_LEVEL* level,
                          ID3D11DeviceContext** ctx)
{
    ID3D11Device* d = nullptr;
    ID3D11DeviceContext* c = nullptr;
    HRESULT hr = MakeTx(adapter, &d, &c, level);
    if (FAILED(hr)) return hr;
    if (scDesc && sc) {
        hr = raw::tx::CreateSwapChain(d, scDesc, sc);
        Log("[Tx] raw::tx::CreateSwapChain (wrapped proxy): 0x%08X", (unsigned)hr);
        if (FAILED(hr)) { c->Release(); d->Release(); return hr; }
    }
    if (dev) *dev = d; else d->Release();
    if (ctx) *ctx = c; else c->Release();
    return hr;
}
} // namespace

bool TxMode_Wrap() { Load(); return C().wrap; }
void* TxMode_CreateFn() { return reinterpret_cast<void*>(&TxCreateFn); }

int TxMode_Get()
{
    Load();
    return C().mode;
}

HRESULT TxMode_CreateDevice(IDXGIAdapter* adapter, D3D_DRIVER_TYPE type, HMODULE sw, UINT flags, const D3D_FEATURE_LEVEL* levels,
                            UINT nLevels, UINT sdk, ID3D11Device** dev, D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** ctx,
                            void* realCreate)
{
    Log("[Tx] D3D11CreateDevice (driver %d, flags 0x%X, %u levels)", (int)type, flags, nLevels);
    HRESULT hr;
    if (C().mode == 1) hr = MakeTx(adapter, dev, ctx, level);
    else hr = reinterpret_cast<PFN_CreateDev>(realCreate)(adapter, type, sw, flags, levels, nLevels, sdk, dev, level, ctx);
    if (SUCCEEDED(hr) && dev && *dev) {
        auto* pi = PG_GetProxyInterface();
        pi->device = *dev;
        if (ctx) pi->context = *ctx;
    }
    return hr;
}

HRESULT TxMode_CreateDeviceAndSwapChain(IDXGIAdapter* adapter, D3D_DRIVER_TYPE type, HMODULE sw, UINT flags,
                                        const D3D_FEATURE_LEVEL* levels, UINT nLevels, UINT sdk, const DXGI_SWAP_CHAIN_DESC* scDesc,
                                        IDXGISwapChain** sc, ID3D11Device** dev, D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** ctx,
                                        void* realCreate)
{
    if (scDesc)
        Log("[Tx] D3D11CreateDeviceAndSwapChain: %ux%u format %d, %u buffers, swap effect %d, flags 0x%X, windowed %d, samples %u",
            scDesc->BufferDesc.Width, scDesc->BufferDesc.Height, (int)scDesc->BufferDesc.Format, scDesc->BufferCount,
            (int)scDesc->SwapEffect, scDesc->Flags, scDesc->Windowed, scDesc->SampleDesc.Count);
    ID3D11Device* d = nullptr;
    ID3D11DeviceContext* c = nullptr;
    IDXGISwapChain* inner = nullptr;
    HRESULT hr;
    if (C().mode == 1) {
        hr = MakeTx(adapter, &d, &c, level);
        if (SUCCEEDED(hr) && scDesc && sc) {
            hr = raw::tx::CreateSwapChain(d, scDesc, &inner);
            Log("[Tx] raw::tx::CreateSwapChain: 0x%08X", (unsigned)hr);
        }
    } else {
        hr = reinterpret_cast<PFN_CreateDevSc>(realCreate)(adapter, type, sw, flags, levels, nLevels, sdk, scDesc, sc ? &inner : nullptr,
                                                         &d, level, &c);
    }
    if (FAILED(hr)) {
        if (inner) inner->Release();
        if (c) c->Release();
        if (d) d->Release();
        return hr;
    }
    auto* pi = PG_GetProxyInterface();
    pi->device = d;
    pi->context = c;
    pi->swapChain = inner;
    pi->backbufferFormat = scDesc ? scDesc->BufferDesc.Format : DXGI_FORMAT_UNKNOWN;
    if (sc) *sc = TxMode_WrapSwapChain(inner, d);
    if (sc && *sc != inner && inner) inner->AddRef();   // the wrapper holds one reference, the proxy interface another
    if (dev) *dev = d; else d->Release();
    if (ctx) *ctx = c; else c->Release();
    return hr;
}

} // namespace SB::Proxy
