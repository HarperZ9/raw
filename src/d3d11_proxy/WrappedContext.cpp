#include "WrappedContextImpl.h"

namespace SB::Proxy
{

// Coherence membrane (Tier 2.1 full): serialize blend/raster/depth/sampler descs on
// first sight of each distinct state object, so the LLM can query HOW state is
// interpreted (blend op, cull, depth-compare, sampler filter) by reading the artifact
// instead of guessing. Deduped by pointer (null keyed by obj literal). char(34)/char(10).
static std::unordered_set<const void*> g_seenState;

void EmitStateLine(const char* obj, const void* ptr,
                          std::initializer_list<std::pair<const char*, int>> kv)
{
    const void* key = ptr ? ptr : static_cast<const void*>(obj);
    if (g_seenState.count(key)) return;
    g_seenState.insert(key);
    try {
        std::error_code ec;
        std::filesystem::create_directories("Data/SKSE/Plugins/RAW/live", ec);
        std::ofstream out("Data/SKSE/Plugins/RAW/live/pipeline_state.jsonl", std::ios::app);
        if (!out.is_open()) return;
        const char Q = static_cast<char>(34);
        out << '{' << Q << "kind" << Q << ':' << Q << "state" << Q
            << ',' << Q << "obj" << Q << ':' << Q << obj << Q
            << ',' << Q << "ptr" << Q << ':' << Q << "0x" << std::hex
            << reinterpret_cast<uintptr_t>(ptr) << std::dec << Q;
        for (const auto& pr : kv)
            out << ',' << Q << pr.first << Q << ':' << pr.second;
        out << '}' << static_cast<char>(10);
    } catch (...) {}
}

WrappedContext* WrappedContext::s_instance = nullptr;

// ============================================================================
//  Constructor / Destructor
// ============================================================================

WrappedContext::WrappedContext(ID3D11DeviceContext* real)
    : m_real(real)
{
    s_instance = this;
    Log("[WrappedContext] Created wrapper %p around real context %p", this, real);
}

WrappedContext::~WrappedContext()
{
    Log("[WrappedContext] Destroyed wrapper %p", this);
}

// ============================================================================
//  Frame stats
// ============================================================================

void WrappedContext::ResetFrameStats()
{
    static bool s_blInit = [](){
        if (PG_DiagBindingLedger()) { BindingLedger::Get().Initialize(nullptr); BindingLedger::Get().SetEnabled(true); }
        return true; }();
    BindingLedger::Get().BeginFrame();
    m_drawCalls     = 0;
    m_rtSwitches    = 0;
    m_shaderChanges = 0;

    // Reset state cache redundancy counters
    m_redundantSRV   = 0;
    m_redundantBlend = 0;
    m_redundantDS    = 0;
    m_redundantRS    = 0;
    m_totalSRV       = 0;
    m_totalBlend     = 0;
    m_totalDS        = 0;
    m_totalRS        = 0;
}

void WrappedContext::ResetStateCache()
{
    m_cachedBlendState = nullptr;
    m_cachedBlendFactor[0] = m_cachedBlendFactor[1] = 1.f;
    m_cachedBlendFactor[2] = m_cachedBlendFactor[3] = 1.f;
    m_cachedSampleMask = 0xFFFFFFFF;
    m_cachedDSState    = nullptr;
    m_cachedStencilRef = 0;
    m_cachedRSState    = nullptr;
    std::memset(m_cachedPSSRVs, 0, sizeof(m_cachedPSSRVs));
    std::memset(m_cachedPSSamplers, 0, sizeof(m_cachedPSSamplers));
}

// ============================================================================
//  IUnknown
// ============================================================================

HRESULT STDMETHODCALLTYPE WrappedContext::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;

    if (riid == __uuidof(ID3D11DeviceContext) || riid == __uuidof(IUnknown) ||
        riid == __uuidof(ID3D11DeviceChild))
    {
        *ppv = this;
        AddRef();
        return S_OK;
    }

    // Block higher context versions — returning the real context would break
    // the COM identity chain (callers can call GetImmediateContext1/2/3 on the
    // real device, completely bypassing our wrapper).
    if (riid == __uuidof(ID3D11DeviceContext1) ||
        riid == __uuidof(ID3D11DeviceContext2) ||
        riid == __uuidof(ID3D11DeviceContext3) ||
        riid == __uuidof(ID3D11DeviceContext4))
    {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    // ID3D11Multithread is safe to forward — it only has Get/SetMultithreadProtected,
    // no methods that return device/context pointers.
    return m_real->QueryInterface(riid, ppv);
}

ULONG STDMETHODCALLTYPE WrappedContext::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

ULONG STDMETHODCALLTYPE WrappedContext::Release()
{
    ULONG ref = InterlockedDecrement(&m_refCount);
    if (ref == 0)
        delete this;
    return ref;
}

// ============================================================================
//  ID3D11DeviceChild
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::GetDevice(ID3D11Device** ppDevice)
{
    if (!ppDevice) return;
    if (m_wrappedDevice) {
        m_wrappedDevice->AddRef();
        *ppDevice = m_wrappedDevice;
    } else {
        m_real->GetDevice(ppDevice);
    }
}

HRESULT STDMETHODCALLTYPE WrappedContext::GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData)
{
    return m_real->GetPrivateData(guid, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedContext::SetPrivateData(REFGUID guid, UINT DataSize, const void* pData)
{
    return m_real->SetPrivateData(guid, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedContext::SetPrivateDataInterface(REFGUID guid, const IUnknown* pData)
{
    return m_real->SetPrivateDataInterface(guid, pData);
}

// ============================================================================
//  Global state (forwarded)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::ClearState()
{
    if (PG_IsSafeMode()) { m_real->ClearState(); return; }

    ResetStateCache();
    m_currentPS = nullptr;
    m_currentVS = nullptr;
    CBDirtyTracker::Get().OnClearState();
    OcclusionCuller::Get().OnClearState();
    m_real->ClearState();
}

void STDMETHODCALLTYPE WrappedContext::Flush()
{
    m_real->Flush();
}

D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE WrappedContext::GetType()
{
    return m_real->GetType();
}

UINT STDMETHODCALLTYPE WrappedContext::GetContextFlags()
{
    return m_real->GetContextFlags();
}

HRESULT STDMETHODCALLTYPE WrappedContext::FinishCommandList(BOOL RestoreDeferredContextState, ID3D11CommandList** ppCL)
{
    return m_real->FinishCommandList(RestoreDeferredContextState, ppCL);
}

} // namespace SB::Proxy
