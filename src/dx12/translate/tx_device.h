// The translated device: ID3D11Device on a D3D12 device and direct queue.
#pragma once
#include "tx_objects.h"
#include <deque>
#include <shared_mutex>
#include <string>

namespace raw::tx {

// One command list being recorded, with everything that must outlive it.
struct Recording {
    ComPtr<ID3D12GraphicsCommandList> cl;
    ComPtr<ID3D12CommandAllocator> alloc;
    Tracker tracker;
    Retire retire;                      // chunks and descriptor pages given up while recording
    std::vector<TQuery*> queries;       // ended in this list; internal references
    uint32_t cmdCount = 0;
    uint32_t draws = 0;
};

struct RootSig {
    ComPtr<ID3D12RootSignature> rs;
    uint32_t id = 0;
    bool compute = false;
    StageBind sb[5];
    struct Params {
        int8_t cb[14];
        int8_t srv = -1, samp = -1, uav = -1;
        uint16_t cbBits = 0;   // the slots with a cb parameter, so a draw visits only those
    } st[5];
};

struct PsoKey {
    uint32_t sh[5];
    uint32_t layout, blend, ds, rs, sampleMask;
    uint16_t rtFmt[8];
    uint16_t dsFmt;
    uint8_t topoType, rtCount, samples, quality, stripCut, pad[3];
    bool operator==(const PsoKey& o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
};
struct PsoKeyHash {
    size_t operator()(const PsoKey& k) const { return static_cast<size_t>(HashBytes(&k, sizeof(k))); }
};

struct LayoutData {
    std::vector<D3D12_INPUT_ELEMENT_DESC> elems;
    std::vector<std::string> names;
};

enum QueryHeapKind { QH_Occlusion, QH_Timestamp, QH_Stats, QH_Count };
struct QueryHeap {
    ComPtr<ID3D12QueryHeap> heap;
    ComPtr<ID3D12Resource> readback;
    uint8_t* mapped = nullptr;
    uint32_t stride = 8;
    std::vector<uint32_t> free;
    bool exhaustedLogged = false;
};

class Device final : public ID3D11Device {
public:
    Device() = default;
    ~Device();
    HRESULT Init(const CreateParams& p);

    // ---- IUnknown / ID3D11Device (tx_device.cpp, tx_create_*.cpp) ----
    STDMETHODIMP QueryInterface(REFIID r, void** o) override;
    STDMETHODIMP_(ULONG) AddRef() override { return ++rc_; }
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP CreateBuffer(const D3D11_BUFFER_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Buffer**) override;
    STDMETHODIMP CreateTexture1D(const D3D11_TEXTURE1D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture1D**) override;
    STDMETHODIMP CreateTexture2D(const D3D11_TEXTURE2D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**) override;
    STDMETHODIMP CreateTexture3D(const D3D11_TEXTURE3D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture3D**) override;
    STDMETHODIMP CreateShaderResourceView(ID3D11Resource*, const D3D11_SHADER_RESOURCE_VIEW_DESC*, ID3D11ShaderResourceView**) override;
    STDMETHODIMP CreateUnorderedAccessView(ID3D11Resource*, const D3D11_UNORDERED_ACCESS_VIEW_DESC*, ID3D11UnorderedAccessView**) override;
    STDMETHODIMP CreateRenderTargetView(ID3D11Resource*, const D3D11_RENDER_TARGET_VIEW_DESC*, ID3D11RenderTargetView**) override;
    STDMETHODIMP CreateDepthStencilView(ID3D11Resource*, const D3D11_DEPTH_STENCIL_VIEW_DESC*, ID3D11DepthStencilView**) override;
    STDMETHODIMP CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC*, UINT, const void*, SIZE_T, ID3D11InputLayout**) override;
    STDMETHODIMP CreateVertexShader(const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11VertexShader**) override;
    STDMETHODIMP CreateGeometryShader(const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11GeometryShader**) override;
    STDMETHODIMP CreateGeometryShaderWithStreamOutput(const void*, SIZE_T, const D3D11_SO_DECLARATION_ENTRY*, UINT, const UINT*, UINT, UINT, ID3D11ClassLinkage*, ID3D11GeometryShader**) override;
    STDMETHODIMP CreatePixelShader(const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11PixelShader**) override;
    STDMETHODIMP CreateHullShader(const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11HullShader**) override;
    STDMETHODIMP CreateDomainShader(const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11DomainShader**) override;
    STDMETHODIMP CreateComputeShader(const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11ComputeShader**) override;
    STDMETHODIMP CreateClassLinkage(ID3D11ClassLinkage**) override;
    STDMETHODIMP CreateBlendState(const D3D11_BLEND_DESC*, ID3D11BlendState**) override;
    STDMETHODIMP CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC*, ID3D11DepthStencilState**) override;
    STDMETHODIMP CreateRasterizerState(const D3D11_RASTERIZER_DESC*, ID3D11RasterizerState**) override;
    STDMETHODIMP CreateSamplerState(const D3D11_SAMPLER_DESC*, ID3D11SamplerState**) override;
    STDMETHODIMP CreateQuery(const D3D11_QUERY_DESC*, ID3D11Query**) override;
    STDMETHODIMP CreatePredicate(const D3D11_QUERY_DESC*, ID3D11Predicate**) override;
    STDMETHODIMP CreateCounter(const D3D11_COUNTER_DESC*, ID3D11Counter**) override;
    STDMETHODIMP CreateDeferredContext(UINT, ID3D11DeviceContext**) override;
    STDMETHODIMP OpenSharedResource(HANDLE, REFIID, void**) override;
    STDMETHODIMP CheckFormatSupport(DXGI_FORMAT, UINT*) override;
    STDMETHODIMP CheckMultisampleQualityLevels(DXGI_FORMAT, UINT, UINT*) override;
    STDMETHODIMP_(void) CheckCounterInfo(D3D11_COUNTER_INFO*) override;
    STDMETHODIMP CheckCounter(const D3D11_COUNTER_DESC*, D3D11_COUNTER_TYPE*, UINT*, LPSTR, UINT*, LPSTR, UINT*, LPSTR, UINT*) override;
    STDMETHODIMP CheckFeatureSupport(D3D11_FEATURE, void*, UINT) override;
    STDMETHODIMP GetPrivateData(REFGUID g, UINT* s, void* d) override { return priv_.Get(g, s, d); }
    STDMETHODIMP SetPrivateData(REFGUID g, UINT s, const void* d) override { return priv_.Set(g, s, d); }
    STDMETHODIMP SetPrivateDataInterface(REFGUID g, const IUnknown* p) override { return priv_.SetIface(g, p); }
    STDMETHODIMP_(D3D_FEATURE_LEVEL) GetFeatureLevel() override { return D3D_FEATURE_LEVEL_11_0; }
    STDMETHODIMP_(UINT) GetCreationFlags() override { return 0; }
    STDMETHODIMP GetDeviceRemovedReason() override { return d3d->GetDeviceRemovedReason(); }
    STDMETHODIMP_(void) GetImmediateContext(ID3D11DeviceContext**) override;
    STDMETHODIMP SetExceptionMode(UINT) override { return S_OK; }
    STDMETHODIMP_(UINT) GetExceptionMode() override { return 0; }

    // ---- IDXGIDevice2 (tx_dxgi_device.cpp): a part of the same object, as the system runtime's device is ----
    class DxgiPart final : public IDXGIDevice2 {
    public:
        Device* dev = nullptr;
        UINT latency = 3;
        INT priority = 0;
        STDMETHODIMP QueryInterface(REFIID r, void** o) override;
        STDMETHODIMP_(ULONG) AddRef() override;
        STDMETHODIMP_(ULONG) Release() override;
        STDMETHODIMP SetPrivateData(REFGUID g, UINT s, const void* d) override;
        STDMETHODIMP SetPrivateDataInterface(REFGUID g, const IUnknown* p) override;
        STDMETHODIMP GetPrivateData(REFGUID g, UINT* s, void* d) override;
        STDMETHODIMP GetParent(REFIID r, void** o) override;
        STDMETHODIMP GetAdapter(IDXGIAdapter** a) override;
        STDMETHODIMP CreateSurface(const DXGI_SURFACE_DESC*, UINT, DXGI_USAGE, const DXGI_SHARED_RESOURCE*, IDXGISurface**) override;
        STDMETHODIMP QueryResourceResidency(IUnknown* const*, DXGI_RESIDENCY*, UINT) override;
        STDMETHODIMP SetGPUThreadPriority(INT p) override;
        STDMETHODIMP GetGPUThreadPriority(INT* p) override;
        STDMETHODIMP SetMaximumFrameLatency(UINT n) override;
        STDMETHODIMP GetMaximumFrameLatency(UINT* n) override;
        STDMETHODIMP OfferResources(UINT, IDXGIResource* const*, DXGI_OFFER_RESOURCE_PRIORITY) override;
        STDMETHODIMP ReclaimResources(UINT, IDXGIResource* const*, BOOL*) override;
        STDMETHODIMP EnqueueSetEvent(HANDLE e) override;
    };
    DxgiPart dxgi;

    // ---- D3D12 objects ----
    ComPtr<ID3D12Device> d3d;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12InfoQueue> info;
    HANDLE fenceEvent = nullptr;
    uint32_t faults = 0;
    bool renameRing = true;
    Context* imm = nullptr;
    StreamPool* pool = nullptr;   // translation workers (tx_stream.h), when CreateParams::workers

    // ---- counters ----
    std::atomic<uint64_t> nDebugErrors{0}, nPso{0}, nRootSig{0}, nSubmit{0}, nBarriers{0}, nDraws{0}, nUnsupported{0};
    void DrainInfoQueue();

    // ---- allocators ----
    Slab slab;
    CpuDescs cpuSrv, cpuRtv, cpuDsv, cpuSamp;
    static constexpr uint32_t kDescPage = 1024;
    ComPtr<ID3D12DescriptorHeap> gpuHeap, samplerHeap;
    UINT srvInc = 0, sampInc = 0;
    uint32_t AllocDescPage();                       // base index; ~0u on failure
    D3D12_GPU_DESCRIPTOR_HANDLE SamplerTable(const uint32_t* ids, uint32_t n);
    D3D12_CPU_DESCRIPTOR_HANDLE NullSrv(uint8_t dim);
    D3D12_CPU_DESCRIPTOR_HANDLE NullUav(uint8_t dim);
    Chunk zeroCb;                                   // bound where a shader reads an unbound slot

    // ---- submission ----
    // Runs the pending upload list, then each recording behind its state
    // fix-ups. Recordings must be closed. `consume` hands the recording's
    // retired memory, allocator and queries to the device.
    uint64_t Submit(Recording** recs, int n, bool consume);
    uint64_t LastSubmitted() const { return lastSubmitted_.load(); }
    uint64_t Completed() { return fence->GetCompletedValue(); }
    void WaitFor(uint64_t serial);
    void Reclaim();
    void RetireLater(Retire&& r);                   // stamped with the next submission
    ComPtr<ID3D12CommandAllocator> GetAllocator();
    void OpenRecording(Recording& r);
    void RecycleList(Recording& r);                 // a finished list's D3D12 objects
    ComPtr<ID3D12CommandSignature> sigDraw, sigDrawIndexed, sigDispatch;
    D3D12_CPU_DESCRIPTOR_HANDLE nullRtv{};
    bool UploadPending() const { return initCmds_ != 0; }

    // ---- uploads at creation time (any thread) ----
    void InitBufferData(ResCore& core, const void* data);
    void InitTextureData(ResCore& core, const D3D11_SUBRESOURCE_DATA* data);

    // ---- caches ----
    ShaderData* InternShader(const void* code, SIZE_T size);
    const D3D11_BLEND_DESC& BlendDesc(uint32_t id) { return blendDescs_[id]; }
    const D3D11_DEPTH_STENCIL_DESC& DepthDesc(uint32_t id) { return dsDescs_[id]; }
    const D3D11_RASTERIZER_DESC& RasterDesc(uint32_t id) { return rsDescs_[id]; }
    uint32_t MipSampler() const { return mipSampler_; }
    RootSig* GetRootSig(const StageBind sb[5], bool compute);
    ID3D12PipelineState* GetPso(const PsoKey& key, ShaderData* const sh[5], RootSig* rs);
    ID3D12PipelineState* GetComputePso(ShaderData* cs, RootSig* rs);
    ID3D12PipelineState* GetMipPso(DXGI_FORMAT fmt);
    RootSig* mipRootSig = nullptr;

    // ---- queries ----
    QueryHeap qh[QH_Count];
    std::mutex queryM;
    uint32_t AllocQuerySlot(QueryHeapKind k);
    void FreeQuerySlot(QueryHeapKind k, uint32_t slot);

    uint32_t PlaneCount(DXGI_FORMAT f);

private:
    template <class I> HRESULT MakeShader(const void* code, SIZE_T size, I** out);
    template <class D> uint32_t InternDesc(std::vector<D>& table, const D& d);
    uint32_t InternSampler(const D3D11_SAMPLER_DESC& d);
    HRESULT InitHeaps();
    HRESULT InitQueries();
    void InitDefaults();
    uint32_t mipSampler_ = 0;
    ID3D12GraphicsCommandList* FixupList(size_t index);

    std::atomic<ULONG> rc_{1};
    PrivateStore priv_;

    std::mutex submitM_;
    std::mutex poolM_;
    std::vector<ID3D12CommandList*> execLists_;
    std::vector<Recording*> submitRecs_;
    std::vector<ComPtr<ID3D12CommandAllocator>> fixAllocs_;
    void ReclaimLocked(bool force);
    void RecycleAllocator(ComPtr<ID3D12CommandAllocator> a, uint64_t serial);
    std::atomic<uint64_t> lastSubmitted_{0};
    std::deque<Retire> retireQ_;
    // Emptied retire vectors, capacity kept (under submitM_): a list's retire set grows to about ten
    // thousand chunks a frame, and growing a fresh vector each time took new pages from the system.
    std::vector<std::vector<Chunk>> chunkVecs_;
    std::vector<std::vector<std::shared_ptr<TrackedRes>>> keepVecs_;
    struct PooledAlloc { ComPtr<ID3D12CommandAllocator> a; uint64_t serial; };
    std::vector<PooledAlloc> allocPool_;
    std::vector<ComPtr<ID3D12GraphicsCommandList>> fixLists_;
    std::vector<ComPtr<ID3D12GraphicsCommandList>> listPool_;
    std::vector<D3D12_RESOURCE_BARRIER> fixups_;
    std::vector<uint32_t> freePages_;

    std::mutex initM_;
    Recording init_;
    std::atomic<uint32_t> initCmds_{0};
    ChunkCache initChunks_;

    std::mutex cacheM_;
    std::unordered_map<uint64_t, std::unique_ptr<ShaderData>> shaders_;
    std::vector<D3D11_BLEND_DESC> blendDescs_;
    std::vector<D3D11_DEPTH_STENCIL_DESC> dsDescs_;
    std::vector<D3D11_RASTERIZER_DESC> rsDescs_;
    std::vector<D3D11_SAMPLER_DESC> sampDescs_;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> sampHandles_;
    std::vector<std::unique_ptr<LayoutData>> layouts_;
    std::unordered_map<uint64_t, uint32_t> layoutIds_;
    std::unordered_map<uint64_t, std::unique_ptr<RootSig>> rootSigs_;
    std::unordered_map<uint64_t, D3D12_GPU_DESCRIPTOR_HANDLE> sampTables_;
    uint32_t sampUsed_ = 0;
    std::unordered_map<uint32_t, D3D12_CPU_DESCRIPTOR_HANDLE> nullSrv_, nullUav_;
    std::unordered_map<uint32_t, uint32_t> planes_;

    std::shared_mutex psoM_;
    std::unordered_map<PsoKey, ComPtr<ID3D12PipelineState>, PsoKeyHash> psos_;
    std::unordered_map<uint32_t, ComPtr<ID3D12PipelineState>> csPsos_, mipPsos_;

    // Native DX12 persistent disk PSO cache (raw_pso_cache.bin beside SkyrimSE.exe).
public:
    void SavePsoCache();
private:
    void InitPsoCache();
    std::mutex psoLibM_;
    ComPtr<ID3D12PipelineLibrary1> psoLib_;
    std::vector<uint8_t> psoLibBlob_;
    std::wstring psoCachePath_;
    bool psoLibDirty_ = false;
    uint32_t psoLibHits_ = 0, psoLibMisses_ = 0;

    friend struct DeviceAccess;
};

inline Device* Dev(ID3D11Device* d) { return static_cast<Device*>(d); }

// Entry points of d3d12.dll and dxgi.dll, resolved at run time so a host DLL
// keeps its import table unchanged (tx_imports.cpp).
HRESULT TxD3D12CreateDevice(IUnknown* adapter, D3D_FEATURE_LEVEL fl, REFIID riid, void** out);
HRESULT TxD3D12GetDebugInterface(REFIID riid, void** out);
HRESULT TxD3D12GetInterface(REFCLSID clsid, REFIID riid, void** out);
HRESULT TxD3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC* d, ID3DBlob** blob, ID3DBlob** err);
HRESULT TxCreateDXGIFactory2(UINT flags, REFIID riid, void** out);

// Format helpers (tx_format.cpp).
DXGI_FORMAT StripSrgb(DXGI_FORMAT f);
bool IsStencilPlaneView(DXGI_FORMAT f);
bool IsDepthFormat(DXGI_FORMAT f);
DXGI_FORMAT FootprintFormat(DXGI_FORMAT f);

}  // namespace raw::tx
