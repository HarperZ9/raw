// The translated context: ID3D11DeviceContext recorded as a D3D12 command list.
//
// The immediate context and every deferred context each own one open D3D12
// command list. D3D11 calls update a shadow of the D3D11 pipeline state; a draw
// turns the dirty parts into D3D12 state. A deferred context records on its
// own thread, and FinishCommandList hands the closed D3D12 list over as it is.
#pragma once
#include "tx_device.h"

namespace raw::tx {

enum StageIndex { S_VS, S_HS, S_DS, S_GS, S_PS, S_CS, S_COUNT };

enum Dirty : uint32_t {
    D_PSO = 1, D_RT = 2, D_VP = 4, D_SC = 8, D_BF = 0x10, D_SREF = 0x20, D_TOPO = 0x40,
    D_VB = 0x80, D_IB = 0x100, D_UAV = 0x400, D_CSUAV = 0x800,
    D_SRV0 = 0x1000,     // << stage
    D_SAMP0 = 0x40000,   // << stage
    // State scans at the next draw (ScanGraphics): which bindings may need a transition. A vertex or index
    // buffer, or one stage's shader resources, rescans only that part; anything that can move a resource out
    // of a state the list already holds (copies, clears, targets, compute, a new list) sets D_SCAN, all parts.
    D_SCAN_IA = 0x200,
    D_SCAN_OM = 0x1000000,
    D_SCAN_SRV0 = 0x2000000,   // << graphics stage (VS..PS)
    D_SCAN = D_SCAN_IA | D_SCAN_OM | (D_SCAN_SRV0 * 0x1F),
    D_ALL = 0xFFFFFFFFu,
};

struct ShaderSlot {
    ShaderData* sd = nullptr;
    void* obj = nullptr;
    void (*ref)(void*, int) = nullptr;
};
struct BufSlot {
    TBuffer* buf = nullptr;
    const Chunk* chunk = nullptr;   // where the buffer's current memory is described
    UINT stride = 0, offset = 0;
};
struct StageState {
    ShaderSlot sh;
    BufSlot cb[14];
    TSRV* srv[128] = {};
    TSampler* samp[16] = {};
    uint32_t srvHi = 0;
};
struct State {
    StageState st[S_COUNT];
    TInputLayout* layout = nullptr;
    BufSlot vb[32];
    uint32_t vbHi = 0;
    BufSlot ib;
    DXGI_FORMAT ibFmt = DXGI_FORMAT_UNKNOWN;
    D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    TRTV* rtv[8] = {};
    uint32_t rtCount = 0;
    TDSV* dsv = nullptr;
    TUAV* uav[8] = {};
    TUAV* csUav[8] = {};
    TBlend* blend = nullptr;
    float factor[4] = {1, 1, 1, 1};
    UINT sampleMask = 0xFFFFFFFFu;
    TDepthStencil* ds = nullptr;
    UINT stencilRef = 0;
    TRaster* rs = nullptr;
    D3D11_VIEWPORT vp[16] = {};
    UINT vpCount = 0;
    D3D11_RECT sc[16] = {};
    UINT scCount = 0;
};

// A buffer a deferred context touched: its own copy of the buffer's memory.
struct Rename {
    Chunk chunk;
    bool local = false;     // written in this context
    bool adopted = false;   // handed to the buffer at ExecuteCommandList
};

struct TCommandList final : Child<ID3D11CommandList> {
    Recording rec;
    std::unordered_map<TBuffer*, Rename> renames;
    explicit TCommandList(Device* d) : Child<ID3D11CommandList>(d) {}
    ~TCommandList() override;
    STDMETHODIMP_(UINT) GetContextFlags() override { return 0; }
};

struct PsoEntry { ID3D12PipelineState* pso; RootSig* root; };

#define TX_STAGE_DECL(P, I)                                                                         \
    STDMETHODIMP_(void) P##SetConstantBuffers(UINT, UINT, ID3D11Buffer* const*) override;           \
    STDMETHODIMP_(void) P##SetShaderResources(UINT, UINT, ID3D11ShaderResourceView* const*) override; \
    STDMETHODIMP_(void) P##SetSamplers(UINT, UINT, ID3D11SamplerState* const*) override;            \
    STDMETHODIMP_(void) P##SetShader(I*, ID3D11ClassInstance* const*, UINT) override;               \
    STDMETHODIMP_(void) P##GetConstantBuffers(UINT, UINT, ID3D11Buffer**) override;                 \
    STDMETHODIMP_(void) P##GetShaderResources(UINT, UINT, ID3D11ShaderResourceView**) override;     \
    STDMETHODIMP_(void) P##GetSamplers(UINT, UINT, ID3D11SamplerState**) override;                  \
    STDMETHODIMP_(void) P##GetShader(I**, ID3D11ClassInstance**, UINT*) override;

class Context final : public ID3D11DeviceContext {
public:
    Context(Device* dev, bool deferred);
    ~Context();

    // IUnknown, ID3D11DeviceChild
    STDMETHODIMP QueryInterface(REFIID r, void** o) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP_(void) GetDevice(ID3D11Device** o) override { *o = dev_; dev_->AddRef(); }
    STDMETHODIMP GetPrivateData(REFGUID g, UINT* s, void* d) override { return priv_.Get(g, s, d); }
    STDMETHODIMP SetPrivateData(REFGUID g, UINT s, const void* d) override { return priv_.Set(g, s, d); }
    STDMETHODIMP SetPrivateDataInterface(REFGUID g, const IUnknown* p) override { return priv_.SetIface(g, p); }

    TX_STAGE_DECL(VS, ID3D11VertexShader)
    TX_STAGE_DECL(HS, ID3D11HullShader)
    TX_STAGE_DECL(DS, ID3D11DomainShader)
    TX_STAGE_DECL(GS, ID3D11GeometryShader)
    TX_STAGE_DECL(PS, ID3D11PixelShader)
    TX_STAGE_DECL(CS, ID3D11ComputeShader)

    // input assembler, output merger, rasterizer (tx_ctx_bind.cpp, tx_ctx_get.cpp)
    STDMETHODIMP_(void) IASetInputLayout(ID3D11InputLayout*) override;
    STDMETHODIMP_(void) IASetVertexBuffers(UINT, UINT, ID3D11Buffer* const*, const UINT*, const UINT*) override;
    STDMETHODIMP_(void) IASetIndexBuffer(ID3D11Buffer*, DXGI_FORMAT, UINT) override;
    STDMETHODIMP_(void) IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY) override;
    STDMETHODIMP_(void) OMSetRenderTargets(UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*) override;
    STDMETHODIMP_(void) OMSetRenderTargetsAndUnorderedAccessViews(UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*) override;
    STDMETHODIMP_(void) OMSetBlendState(ID3D11BlendState*, const FLOAT[4], UINT) override;
    STDMETHODIMP_(void) OMSetDepthStencilState(ID3D11DepthStencilState*, UINT) override;
    STDMETHODIMP_(void) SOSetTargets(UINT, ID3D11Buffer* const*, const UINT*) override;
    STDMETHODIMP_(void) RSSetState(ID3D11RasterizerState*) override;
    STDMETHODIMP_(void) RSSetViewports(UINT, const D3D11_VIEWPORT*) override;
    STDMETHODIMP_(void) RSSetScissorRects(UINT, const D3D11_RECT*) override;
    STDMETHODIMP_(void) CSSetUnorderedAccessViews(UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*) override;
    STDMETHODIMP_(void) IAGetInputLayout(ID3D11InputLayout**) override;
    STDMETHODIMP_(void) IAGetVertexBuffers(UINT, UINT, ID3D11Buffer**, UINT*, UINT*) override;
    STDMETHODIMP_(void) IAGetIndexBuffer(ID3D11Buffer**, DXGI_FORMAT*, UINT*) override;
    STDMETHODIMP_(void) IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY*) override;
    STDMETHODIMP_(void) OMGetRenderTargets(UINT, ID3D11RenderTargetView**, ID3D11DepthStencilView**) override;
    STDMETHODIMP_(void) OMGetRenderTargetsAndUnorderedAccessViews(UINT, ID3D11RenderTargetView**, ID3D11DepthStencilView**, UINT, UINT, ID3D11UnorderedAccessView**) override;
    STDMETHODIMP_(void) OMGetBlendState(ID3D11BlendState**, FLOAT[4], UINT*) override;
    STDMETHODIMP_(void) OMGetDepthStencilState(ID3D11DepthStencilState**, UINT*) override;
    STDMETHODIMP_(void) SOGetTargets(UINT, ID3D11Buffer**) override;
    STDMETHODIMP_(void) RSGetState(ID3D11RasterizerState**) override;
    STDMETHODIMP_(void) RSGetViewports(UINT*, D3D11_VIEWPORT*) override;
    STDMETHODIMP_(void) RSGetScissorRects(UINT*, D3D11_RECT*) override;
    STDMETHODIMP_(void) CSGetUnorderedAccessViews(UINT, UINT, ID3D11UnorderedAccessView**) override;
    STDMETHODIMP_(void) GetPredication(ID3D11Predicate**, BOOL*) override;
    STDMETHODIMP_(void) SetPredication(ID3D11Predicate*, BOOL) override;

    // draws and compute (tx_ctx_draw.cpp, tx_ctx_compute.cpp)
    STDMETHODIMP_(void) DrawIndexed(UINT, UINT, INT) override;
    STDMETHODIMP_(void) Draw(UINT, UINT) override;
    STDMETHODIMP_(void) DrawIndexedInstanced(UINT, UINT, UINT, INT, UINT) override;
    STDMETHODIMP_(void) DrawInstanced(UINT, UINT, UINT, UINT) override;
    STDMETHODIMP_(void) DrawAuto() override;
    STDMETHODIMP_(void) DrawIndexedInstancedIndirect(ID3D11Buffer*, UINT) override;
    STDMETHODIMP_(void) DrawInstancedIndirect(ID3D11Buffer*, UINT) override;
    STDMETHODIMP_(void) Dispatch(UINT, UINT, UINT) override;
    STDMETHODIMP_(void) DispatchIndirect(ID3D11Buffer*, UINT) override;

    // data (tx_ctx_data.cpp, tx_ctx_copy.cpp)
    STDMETHODIMP Map(ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*) override;
    STDMETHODIMP_(void) Unmap(ID3D11Resource*, UINT) override;
    STDMETHODIMP_(void) UpdateSubresource(ID3D11Resource*, UINT, const D3D11_BOX*, const void*, UINT, UINT) override;
    STDMETHODIMP_(void) CopySubresourceRegion(ID3D11Resource*, UINT, UINT, UINT, UINT, ID3D11Resource*, UINT, const D3D11_BOX*) override;
    STDMETHODIMP_(void) CopyResource(ID3D11Resource*, ID3D11Resource*) override;
    STDMETHODIMP_(void) CopyStructureCount(ID3D11Buffer*, UINT, ID3D11UnorderedAccessView*) override;
    STDMETHODIMP_(void) ResolveSubresource(ID3D11Resource*, UINT, ID3D11Resource*, UINT, DXGI_FORMAT) override;
    STDMETHODIMP_(void) GenerateMips(ID3D11ShaderResourceView*) override;

    // clears, queries, lists (tx_ctx_misc.cpp)
    STDMETHODIMP_(void) ClearRenderTargetView(ID3D11RenderTargetView*, const FLOAT[4]) override;
    STDMETHODIMP_(void) ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView*, const UINT[4]) override;
    STDMETHODIMP_(void) ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView*, const FLOAT[4]) override;
    STDMETHODIMP_(void) ClearDepthStencilView(ID3D11DepthStencilView*, UINT, FLOAT, UINT8) override;
    STDMETHODIMP_(void) Begin(ID3D11Asynchronous*) override;
    STDMETHODIMP_(void) End(ID3D11Asynchronous*) override;
    STDMETHODIMP GetData(ID3D11Asynchronous*, void*, UINT, UINT) override;
    STDMETHODIMP_(void) SetResourceMinLOD(ID3D11Resource*, FLOAT) override;
    STDMETHODIMP_(FLOAT) GetResourceMinLOD(ID3D11Resource*) override;
    STDMETHODIMP_(void) ExecuteCommandList(ID3D11CommandList*, BOOL) override;
    STDMETHODIMP FinishCommandList(BOOL, ID3D11CommandList**) override;
    STDMETHODIMP_(void) ClearState() override;
    STDMETHODIMP_(void) Flush() override;
    STDMETHODIMP_(D3D11_DEVICE_CONTEXT_TYPE) GetType() override
    {
        return deferred_ ? D3D11_DEVICE_CONTEXT_DEFERRED : D3D11_DEVICE_CONTEXT_IMMEDIATE;
    }
    STDMETHODIMP_(UINT) GetContextFlags() override { return 0; }

    // ---- internals ----
    void PeekCB(UINT stage, UINT start, UINT n, ID3D11Buffer** out) const
    {
        for (UINT i = 0; i < n; ++i)
            out[i] = (stage < S_COUNT && start + i < 14) ? s_.st[stage].cb[start + i].buf : nullptr;
    }
    uint64_t FlushInternal();                 // submits the open list; returns its serial
    void Sync();                              // flush and wait for the GPU
    void Shutdown();
    ID3D12GraphicsCommandList* Cl() { return rec_.cl.Get(); }
    Recording& Rec() { return rec_; }
    ChunkCache& Chunks() { return chunks_; }
    void MarkDirty(uint32_t d) { dirty_ |= d; }
    uint32_t AllocDescs(uint32_t n);          // index into the shader-visible heap
    D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(uint32_t index) const;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(uint32_t index) const;

private:
    void BeginList();
    void ResetState();
    void Repin();
    const Chunk* Pin(TBuffer* b);
    void RenameBuffer(TBuffer* b);
    template <class T> void SetShader(int stage, T* s);
    template <class I> void GetShader(int stage, I** out);
    void SetCB(int stage, UINT start, UINT n, ID3D11Buffer* const* b);
    void SetSRV(int stage, UINT start, UINT n, ID3D11ShaderResourceView* const* v);
    void SetSamp(int stage, UINT start, UINT n, ID3D11SamplerState* const* s);
    void SetTargets(UINT n, ID3D11RenderTargetView* const* rtv, ID3D11DepthStencilView* dsv, bool keepRtv);
    bool TargetConflict(const TSRV* v) const;
    void UnbindSrvsOf(const ResCore* core, const SubRange& range);

    bool PrepareDraw(bool indexed);
    bool ResolvePipeline();
    void ScanGraphics(const RootSig* root);
    void ApplyTables(const RootSig* root, int firstStage, int lastStage, bool compute);
    void ApplyIA(bool indexed);
    void ApplyDynamic();
    bool PrepareDispatch();
    void Indirect(ID3D11Buffer* args, UINT offset, int kind);

    void WriteTexture(ResCore& core, UINT sub, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch);
    void CopyBuffer(ResCore& dst, uint64_t dstOff, ResCore& src, uint64_t srcOff, uint64_t n);
    void CopyTexture(ResCore& dst, UINT dsub, UINT x, UINT y, UINT z, ResCore& src, UINT ssub, const D3D11_BOX* box);
    void StagingCpuCopy(ResCore& dst, UINT dsub, UINT x, UINT y, UINT z, ResCore& src, UINT ssub, const D3D11_BOX* box);
    void ClearUav(ID3D11UnorderedAccessView* v, const UINT* ui, const FLOAT* fl);

    Device* dev_;
    bool deferred_;
    std::atomic<ULONG> rc_{1};
    PrivateStore priv_;
    Recording rec_;
    ChunkCache chunks_;
    uint32_t descBase_ = ~0u, descUsed_ = 0;
    State s_;
    uint32_t dirty_ = D_ALL;
    RootSig* root_ = nullptr;       // graphics root signature set on the list
    RootSig* croot_ = nullptr;      // compute root signature set on the list
    ID3D12PipelineState* pso_ = nullptr;
    PsoEntry cur_{};                // pipeline the D3D11 state resolves to
    PsoKey lastKey_{};              // the key cur_ was resolved from
    D3D12_GPU_VIRTUAL_ADDRESS cbVa_[S_COUNT][14];
    const TBuffer* cbBuf_[S_COUNT][14];
    uint64_t sampKey_[S_COUNT];
    D3D12_CPU_DESCRIPTOR_HANDLE nullSrv_[12] = {}, nullUav_[12] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE NullSrvFor(const ShaderData* sd, uint32_t slot);
    D3D12_CPU_DESCRIPTOR_HANDLE NullUavFor(const ShaderData* sd, uint32_t slot);
    std::unordered_map<PsoKey, PsoEntry, PsoKeyHash> psoCache_;
    std::unordered_map<TBuffer*, Rename> renames_;
    friend class Device;
    friend class SwapChain;
};

inline Context* Ctx(ID3D11DeviceContext* c) { return static_cast<Context*>(c); }

// Uploads `data` into a region of a committed texture through `rec`.
void UploadRegion(Device* dev, Recording& rec, ChunkCache& chunks, ResCore& core, UINT sub,
                  UINT x, UINT y, UINT z, UINT w, UINT h, UINT d,
                  const void* data, UINT rowPitch, UINT depthPitch);

}  // namespace raw::tx
