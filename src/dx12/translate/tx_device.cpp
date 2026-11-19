// Device creation, lifetime and capability queries.
#include "tx_context.h"

namespace raw::tx {

void DevAddRef(Device* d) { d->AddRef(); }
void DevRelease(Device* d) { d->Release(); }
ID3D11Device* DevAsD3D11(Device* d) { return d; }
void CountUnsupported(Device* d, const char* what)
{
    if (d->nUnsupported++ < 16) Log("unsupported: %s", what);
}

static HRESULT PickAdapter(IDXGIFactory4* f, const CreateParams& p, IDXGIAdapter1** out)
{
    if (p.warp) return f->EnumWarpAdapter(IID_PPV_ARGS(out));
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> a;
        if (f->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        if (p.adapter) {
            if (memcmp(&d.AdapterLuid, p.adapter, sizeof(LUID)) != 0) continue;
        } else if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }
        *out = a.Detach();
        return S_OK;
    }
    return DXGI_ERROR_NOT_FOUND;
}

// A device from the Agility SDK's D3D12Core.dll in `path` (absolute), with its own debug layer when asked.
static HRESULT AgilityDevice(const CreateParams& p, IDXGIAdapter1* adapter, ComPtr<ID3D12Device>& out)
{
    ComPtr<ID3D12SDKConfiguration1> cfg;
    HRESULT hr = TxD3D12GetInterface(CLSID_D3D12SDKConfiguration, IID_PPV_ARGS(&cfg));
    if (FAILED(hr)) { Log("Agility SDK: no ID3D12SDKConfiguration1 (0x%08X)", unsigned(hr)); return hr; }
    ComPtr<ID3D12DeviceFactory> factory;
    if (FAILED(hr = cfg->CreateDeviceFactory(p.agilityVersion, p.agilityPath, IID_PPV_ARGS(&factory)))) {
        Log("Agility SDK %u at %s: CreateDeviceFactory 0x%08X", p.agilityVersion, p.agilityPath, unsigned(hr));
        return hr;
    }
    if (p.debugLayer) {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(factory->GetConfigurationInterface(CLSID_D3D12Debug, IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer();
        else Log("Agility SDK: debug layer not available");
    }
    if (FAILED(hr = factory->CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&out)))) {
        Log("Agility SDK: CreateDevice 0x%08X", unsigned(hr));
        return hr;
    }
    Log("Agility SDK %u: the device runs on %s\\D3D12Core.dll", p.agilityVersion, p.agilityPath);
    return S_OK;
}

HRESULT Device::Init(const CreateParams& p)
{
    faults = p.faults;
    renameRing = p.renameRing;
    const bool agility = p.agilityPath && p.agilityPath[0] && p.agilityVersion;
    if (p.debugLayer && !agility) {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(TxD3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer();
        else Log("D3D12 debug layer not available");
    }
    HRESULT hr = TxCreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return hr;
    if (FAILED(hr = PickAdapter(factory.Get(), p, &adapter))) return hr;
    // The Agility SDK when configured; the system runtime when it is not, or when it fails.
    if (!agility || FAILED(AgilityDevice(p, adapter.Get(), d3d))) {
        if (agility && p.debugLayer) {
            ComPtr<ID3D12Debug> dbg;
            if (SUCCEEDED(TxD3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer();
        }
        if (FAILED(hr = TxD3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d)))) return hr;
    }
    if (p.debugLayer && SUCCEEDED(d3d.As(&info))) info->SetMessageCountLimit(4096);

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(hr = d3d->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) return hr;
    if (FAILED(hr = d3d->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return hr;

    slab.Init(d3d.Get(), p.ringWriteBack);
    Log("buffer renames: %s", !renameRing ? "slab chunks" : p.ringWriteBack ? "ring, write-back pages" : "ring, upload pages");
    initChunks_.Init(&slab);
    cpuSrv.Init(d3d.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    cpuRtv.Init(d3d.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    cpuDsv.Init(d3d.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    cpuSamp.Init(d3d.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    if (FAILED(hr = InitHeaps())) return hr;
    if (FAILED(hr = InitQueries())) return hr;

    std::vector<Chunk> z;
    slab.Refill(Slab::ClassOf(65536), z, 1);
    if (z.empty()) return E_OUTOFMEMORY;
    zeroCb = z[0];
    memset(zeroCb.cpu, 0, zeroCb.size);

    InitDefaults();

    OpenRecording(init_);
    imm = new Context(this, false);
    if (p.workers) {
        pool = new StreamPool(this, p.workers > 16 ? 16 : p.workers);
        imm->EnableStream(pool, p.segmentDraws, p.segmentMin);
        Log("translation workers: %u", pool->Workers());
    }
    return S_OK;
}

Device::~Device()
{
    if (imm) {
        imm->Shutdown();   // submits what the workers closed
        delete pool;
        pool = nullptr;
        delete imm;
    }
    if (queue && fence) {
        uint64_t s = ++lastSubmitted_;
        queue->Signal(fence.Get(), s);
        WaitFor(s);
    }
    {
        std::lock_guard<std::mutex> g(submitM_);
        ReclaimLocked(true);
    }
    initChunks_.ReturnAll();
    init_ = Recording{};
    psos_.clear(); csPsos_.clear(); mipPsos_.clear(); rootSigs_.clear();
    for (auto& h : qh) { h.readback.Reset(); h.heap.Reset(); }
    cpuSrv.Shutdown(); cpuRtv.Shutdown(); cpuDsv.Shutdown(); cpuSamp.Shutdown();
    slab.Shutdown();
    DrainInfoQueue();
}

STDMETHODIMP Device::QueryInterface(REFIID r, void** o)
{
    if (!o) return E_POINTER;
    if (r == __uuidof(IUnknown) || r == __uuidof(ID3D11Device)) {
        *o = static_cast<ID3D11Device*>(this);
        AddRef();
        return S_OK;
    }
    if (r == __uuidof(IDXGIObject) || r == __uuidof(IDXGIDevice) || r == __uuidof(IDXGIDevice1) || r == __uuidof(IDXGIDevice2)) {
        dxgi.dev = this;
        *o = static_cast<IDXGIDevice2*>(&dxgi);
        AddRef();
        return S_OK;
    }
    LogIid("device", r);
    *o = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) Device::Release()
{
    ULONG n = --rc_;
    if (n == 0) delete this;
    return n;
}

STDMETHODIMP_(void) Device::GetImmediateContext(ID3D11DeviceContext** o)
{
    TX_TRACE("Device::GetImmediateContext");
    *o = imm;
    imm->AddRef();
}

void Device::DrainInfoQueue()
{
    if (!info) return;
    UINT64 n = info->GetNumStoredMessages();
    std::vector<uint8_t> buf;
    for (UINT64 i = 0; i < n; ++i) {
        SIZE_T len = 0;
        info->GetMessage(i, nullptr, &len);
        buf.resize(len);
        auto* m = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
        if (FAILED(info->GetMessage(i, m, &len))) continue;
        if (m->Severity == D3D12_MESSAGE_SEVERITY_ERROR || m->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
            if (nDebugErrors++ < 8) Log("D3D12 debug layer: %s", m->pDescription);
        }
    }
    info->ClearStoredMessages();
}

STDMETHODIMP Device::CheckFormatSupport(DXGI_FORMAT f, UINT* out)
{
    TX_TRACE("Device::CheckFormatSupport");
    if (!out) return E_INVALIDARG;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT fs{f};
    HRESULT hr = d3d->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof(fs));
    *out = SUCCEEDED(hr) ? UINT(fs.Support1) : 0;   // D3D12_FORMAT_SUPPORT1 keeps the D3D11 bit values
    return hr;
}

STDMETHODIMP Device::CheckMultisampleQualityLevels(DXGI_FORMAT f, UINT samples, UINT* out)
{
    TX_TRACE("Device::CheckMultisampleQualityLevels");
    if (!out) return E_INVALIDARG;
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q{};
    q.Format = f; q.SampleCount = samples;
    HRESULT hr = d3d->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof(q));
    *out = SUCCEEDED(hr) ? q.NumQualityLevels : 0;
    return S_OK;
}

STDMETHODIMP Device::CheckFeatureSupport(D3D11_FEATURE feature, void* data, UINT size)
{
    TX_TRACE("Device::CheckFeatureSupport");
    if (!data) return E_INVALIDARG;
    if (feature == D3D11_FEATURE_THREADING && size == sizeof(D3D11_FEATURE_DATA_THREADING)) {
        auto* t = static_cast<D3D11_FEATURE_DATA_THREADING*>(data);
        t->DriverConcurrentCreates = TRUE;
        t->DriverCommandLists = TRUE;
        return S_OK;
    }
    if (feature == D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS &&
        size == sizeof(D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS)) {
        static_cast<D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS*>(data)
            ->ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x = TRUE;
        return S_OK;
    }
    if (feature == D3D11_FEATURE_DOUBLES && size == sizeof(D3D11_FEATURE_DATA_DOUBLES)) {
        D3D12_FEATURE_DATA_D3D12_OPTIONS o{};
        d3d->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof(o));
        static_cast<D3D11_FEATURE_DATA_DOUBLES*>(data)->DoublePrecisionFloatShaderOps = o.DoublePrecisionFloatShaderOps;
        return S_OK;
    }
    CountUnsupported(this, "CheckFeatureSupport (this feature)");
    return E_INVALIDARG;
}

STDMETHODIMP_(void) Device::CheckCounterInfo(D3D11_COUNTER_INFO* i)
{
    TX_TRACE("Device::CheckCounterInfo");
    if (i) *i = D3D11_COUNTER_INFO{};
}
STDMETHODIMP Device::CheckCounter(const D3D11_COUNTER_DESC*, D3D11_COUNTER_TYPE*, UINT*, LPSTR, UINT*, LPSTR, UINT*, LPSTR, UINT*)
{
    TX_TRACE("Device::CheckCounter");
    return DXGI_ERROR_UNSUPPORTED;
}
STDMETHODIMP Device::CreateCounter(const D3D11_COUNTER_DESC*, ID3D11Counter**) { return DXGI_ERROR_UNSUPPORTED; }
STDMETHODIMP Device::CreateClassLinkage(ID3D11ClassLinkage**)
{
    TX_TRACE("Device::CreateClassLinkage");
    CountUnsupported(this, "CreateClassLinkage");
    return E_NOTIMPL;
}
STDMETHODIMP Device::OpenSharedResource(HANDLE, REFIID, void**)
{
    TX_TRACE("Device::OpenSharedResource");
    CountUnsupported(this, "OpenSharedResource");
    return E_NOTIMPL;
}

STDMETHODIMP Device::CreateDeferredContext(UINT, ID3D11DeviceContext** out)
{
    TX_TRACE("Device::CreateDeferredContext");
    if (!out) return S_FALSE;
    *out = new Context(this, true);
    return S_OK;
}

// ---- public entry points ----

HRESULT CreateDevice(const CreateParams& params, ID3D11Device** device, ID3D11DeviceContext** ctx)
{
    if (!device) return E_POINTER;
    auto* d = new Device();
    HRESULT hr = d->Init(params);
    if (FAILED(hr)) { delete d; return hr; }
    *device = d;
    if (ctx) d->GetImmediateContext(ctx);
    return S_OK;
}

Stats GetStats(ID3D11Device* device)
{
    Device* d = Dev(device);
    d->DrainInfoQueue();
    Stats s;
    s.debugErrors = d->nDebugErrors; s.psoCreated = d->nPso; s.rootSignatures = d->nRootSig;
    s.submissions = d->nSubmit; s.barriers = d->nBarriers; s.draws = d->nDraws;
    s.unsupportedCalls = d->nUnsupported;
    return s;
}

void GetNative(ID3D11Device* device, void** d3d12Device, void** d3d12Queue)
{
    if (d3d12Device) *d3d12Device = Dev(device)->d3d.Get();
    if (d3d12Queue) *d3d12Queue = Dev(device)->queue.Get();
}

}  // namespace raw::tx
