// D3D12Bridge setup: device on the game's adapter, shared textures, shared
// fence, descriptor heaps and timestamp queries. Original work, MIT.
#include "BridgeImpl.h"
#include "DepthCopy_CS.h"   // g_DepthCopyCS (fxc, cs_5_0)

namespace raw::bridge
{

static void NoLog(const char*, ...) {}

bool D3D12Bridge::Impl::CreateDevice12(ID3D11Device* dev)
{
    ComPtr<IDXGIDevice> dxgiDev;
    ComPtr<IDXGIAdapter> adapter11;
    DXGI_ADAPTER_DESC ad{};
    if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&dxgiDev))) ||
        FAILED(dxgiDev->GetAdapter(&adapter11)) || FAILED(adapter11->GetDesc(&ad))) {
        log("[D3D12Bridge] cannot read the D3D11 adapter");
        return false;
    }

    // Loaded at runtime: the proxy must not import d3d12.dll or dxgi.dll.
    d3d12Dll = LoadLibraryW(L"d3d12.dll");
    HMODULE dxgiDll = LoadLibraryW(L"dxgi.dll");
    if (!d3d12Dll || !dxgiDll) { log("[D3D12Bridge] d3d12.dll or dxgi.dll missing"); return false; }
    auto createDevice = reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(
        GetProcAddress(d3d12Dll, "D3D12CreateDevice"));
    auto getDebug = reinterpret_cast<PFN_D3D12_GET_DEBUG_INTERFACE>(
        GetProcAddress(d3d12Dll, "D3D12GetDebugInterface"));
    using PFN_Factory2 = HRESULT(WINAPI*)(UINT, REFIID, void**);
    auto createFactory = reinterpret_cast<PFN_Factory2>(GetProcAddress(dxgiDll, "CreateDXGIFactory2"));
    if (!createDevice || !createFactory) { log("[D3D12Bridge] entry points missing"); return false; }

    if (cfg.debugLayer && getDebug) {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(getDebug(IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer();
    }

    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter1> adapter12;
    if (FAILED(createFactory(0, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->EnumAdapterByLuid(ad.AdapterLuid, IID_PPV_ARGS(&adapter12)))) {
        log("[D3D12Bridge] no DXGI adapter with LUID %08X:%08X",
            ad.AdapterLuid.HighPart, ad.AdapterLuid.LowPart);
        return false;
    }
    HRESULT hr = createDevice(adapter12.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev12));
    if (FAILED(hr)) { log("[D3D12Bridge] D3D12CreateDevice failed 0x%08X", (unsigned)hr); return false; }

    char name[128]{};
    WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name, sizeof(name) - 1, nullptr, nullptr);
    log("[D3D12Bridge] D3D12 device on '%s' (LUID %08X:%08X, same adapter as D3D11)",
        name, ad.AdapterLuid.HighPart, ad.AdapterLuid.LowPart);

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(dev12->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) return false;
    queue->GetTimestampFrequency(&d12Freq);
    for (auto& s : d12)
        if (FAILED(dev12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s.alloc))))
            return false;
    if (FAILED(dev12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, d12[0].alloc.Get(),
                                        nullptr, IID_PPV_ARGS(&cl))))
        return false;
    cl->Close();

    // One fence, created on D3D12, opened on D3D11.
    HANDLE h = nullptr;
    if (FAILED(dev12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence12))) ||
        FAILED(dev12->CreateSharedHandle(fence12.Get(), nullptr, GENERIC_ALL, nullptr, &h))) {
        log("[D3D12Bridge] shared fence creation failed");
        return false;
    }
    hr = dev11->OpenSharedFence(h, IID_PPV_ARGS(&fence11));
    CloseHandle(h);
    if (FAILED(hr)) { log("[D3D12Bridge] OpenSharedFence failed 0x%08X", (unsigned)hr); return false; }
    cpuEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return cpuEvent != nullptr;
}

bool D3D12Bridge::Impl::CreateShared(SharedTex& t, DXGI_FORMAT fmt,
                                     D3D12_RESOURCE_FLAGS flags, const char* name)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = cfg.width;
    rd.Height = cfg.height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = fmt;
    rd.SampleDesc.Count = 1;
    rd.Flags = flags;
    HRESULT hr = dev12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&t.res12));
    if (FAILED(hr)) { log("[D3D12Bridge] %s: CreateCommittedResource 0x%08X", name, (unsigned)hr); return false; }

    HANDLE h = nullptr;
    hr = dev12->CreateSharedHandle(t.res12.Get(), nullptr, GENERIC_ALL, nullptr, &h);
    if (FAILED(hr)) { log("[D3D12Bridge] %s: CreateSharedHandle 0x%08X", name, (unsigned)hr); return false; }
    hr = dev11->OpenSharedResource1(h, IID_PPV_ARGS(&t.tex11));
    CloseHandle(h);
    if (FAILED(hr)) { log("[D3D12Bridge] %s: OpenSharedResource1 0x%08X", name, (unsigned)hr); return false; }
    return true;
}

bool D3D12Bridge::Impl::CreateViews()
{
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = 2;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev12->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&srvHeap)))) return false;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = 1;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(dev12->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtvHeap)))) return false;

    UINT inc = dev12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = srvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    sd.Format = cfg.colorFormat;
    dev12->CreateShaderResourceView(colorIn.res12.Get(), &sd, cpu);
    cpu.ptr += inc;
    sd.Format = kSharedDepthFormat;
    dev12->CreateShaderResourceView(depthIn.res12.Get(), &sd, cpu);
    dev12->CreateRenderTargetView(colorOut.res12.Get(), nullptr,
                                  rtvHeap->GetCPUDescriptorHandleForHeapStart());
    return true;
}

bool D3D12Bridge::Impl::CreateQueries()
{
    D3D12_QUERY_HEAP_DESC qh{};
    qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qh.Count = 2 * kRing;
    if (FAILED(dev12->CreateQueryHeap(&qh, IID_PPV_ARGS(&tsHeap)))) return false;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = sizeof(uint64_t) * 2 * kRing;
    rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(dev12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tsReadback))))
        return false;

    D3D11_QUERY_DESC dq{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    D3D11_QUERY_DESC tq{D3D11_QUERY_TIMESTAMP, 0};
    for (auto& s : d11) {
        if (FAILED(dev11->CreateQuery(&dq, &s.disjoint))) return false;
        for (auto& t : s.ts)
            if (FAILED(dev11->CreateQuery(&tq, &t))) return false;
    }
    return true;
}

// D3D11 opened these D3D12 textures only when they were render-target
// capable; a no-flag texture and a depth-stencil texture both returned
// E_INVALIDARG from OpenSharedResource1 (RTX 4090, 2026-11-17). So depth
// crosses as R32_FLOAT written by a D3D11 compute pass. Simultaneous access
// lets both queues use the textures with the shared fence as the only ordering.
static constexpr D3D12_RESOURCE_FLAGS kSharedColour =
    D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
static constexpr D3D12_RESOURCE_FLAGS kSharedDepth =
    kSharedColour | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

bool D3D12Bridge::Impl::CreateDepthCopy()
{
    HRESULT hr = dev11->CreateComputeShader(g_DepthCopyCS, sizeof(g_DepthCopyCS), nullptr, &depthCS);
    if (FAILED(hr)) { log("[D3D12Bridge] depth copy shader 0x%08X", (unsigned)hr); return false; }
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = kSharedDepthFormat;
    ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    hr = dev11->CreateUnorderedAccessView(depthIn.tex11.Get(), &ud, &depthUAV);
    if (FAILED(hr)) { log("[D3D12Bridge] depth UAV 0x%08X", (unsigned)hr); return false; }
    return true;
}

D3D12Bridge::D3D12Bridge() : m(std::make_unique<Impl>()) {}
D3D12Bridge::~D3D12Bridge() { Shutdown(); }

bool D3D12Bridge::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                             const BridgeConfig& cfg, LogFn log)
{
    Shutdown();
    m->cfg = cfg;
    m->log = log ? log : NoLog;
    if (!dev || !ctx || !cfg.width || !cfg.height) return false;
    // Always work on the device's own immediate context. A process can hold
    // several D3D11 devices (SKSE plugins create their own); a context from
    // another device makes Signal fail with E_INVALIDARG.
    ComPtr<ID3D11DeviceContext> own;
    dev->GetImmediateContext(&own);
    if (own.Get() != ctx)
        m->log("[D3D12Bridge] the context passed in belongs to another device; using the device's own");
    HRESULT hrDev = dev->QueryInterface(IID_PPV_ARGS(&m->dev11));
    HRESULT hrCtx = own->QueryInterface(IID_PPV_ARGS(&m->ctx11));
    if (FAILED(hrDev) || FAILED(hrCtx)) {
        m->log("[D3D12Bridge] needs ID3D11Device5 (0x%08X) and ID3D11DeviceContext4 (0x%08X)",
               (unsigned)hrDev, (unsigned)hrCtx);
        return false;
    }
    bool ok = m->CreateDevice12(dev)
        && m->CreateShared(m->colorIn, cfg.colorFormat, kSharedColour, "colour-in")
        && m->CreateShared(m->depthIn, kSharedDepthFormat, kSharedDepth, "depth-in")
        && m->CreateShared(m->colorOut, cfg.colorFormat, kSharedColour, "colour-out")
        && m->CreateViews() && m->CreateDepthCopy() && m->CreateQueries();
    if (ok) {
        m->pass = MakeDepthFogPass();
        ok = m->pass->Create(m->dev12.Get(), cfg.colorFormat, m->log);
    }
    if (!ok) { m->log("[D3D12Bridge] initialisation failed; bridge stays off"); Shutdown(); return false; }
    if (!m->SelfTest()) {
        m->log("[D3D12Bridge] fence self-test failed; bridge stays off");
        Shutdown();
        return false;
    }
    m->ready = true;
    m->log("[D3D12Bridge] ready: %ux%u colour fmt %d, depth as R32F, pass '%s'",
           cfg.width, cfg.height, (int)cfg.colorFormat, m->pass->Name());
    return true;
}

void D3D12Bridge::Shutdown()
{
    if (!m) return;
    if (m->ready) {
        m->CpuWait(m->lastD12, m->cfg.cpuWaitTimeout);
        if (m->ctx11) m->ctx11->Flush();
    }
    LogFn log = m->log;
    if (m->cpuEvent) CloseHandle(m->cpuEvent);
    *m = Impl{};
    m->log = log;
}

bool D3D12Bridge::Ready() const { return m->ready; }
const BridgeConfig& D3D12Bridge::Config() const { return m->cfg; }
bool D3D12Bridge::Matches(UINT w, UINT h, DXGI_FORMAT c) const
{
    return m->ready && m->cfg.width == w && m->cfg.height == h && m->cfg.colorFormat == c;
}

} // namespace raw::bridge
