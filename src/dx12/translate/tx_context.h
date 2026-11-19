// The translated context: ID3D11DeviceContext recorded as a D3D12 command list.
//
// The immediate context and every deferred context each own one open D3D12
// command list. D3D11 calls update a shadow of the D3D11 pipeline state; a draw
// turns the dirty parts into D3D12 state. A deferred context records on its
// own thread, and FinishCommandList hands the closed D3D12 list over as it is.
#pragma once
#include "tx_device.h"
#include "tx_holds.h"
#include "tx_stream.h"

#include "tx_state.h"

namespace raw::tx {

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
    // replay: a worker's context for segments of the immediate context (tx_stream.h); holds no references.
    Context(Device* dev, bool deferred, bool replay = false);
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
    const State& Bound() const { return s_; }
    BindHolds& Holds() { return holds_; }

    // ---- parallel translation (tx_stream.cpp, tx_stream_play.cpp) ----
    void EnableStream(StreamPool* p, uint32_t segmentDraws, uint32_t minDraws, bool predict)
    {
        pool_ = p;
        predict_ = predict;
        segDraws_ = segmentDraws ? segmentDraws : 256;
        minDraws_ = minDraws > segDraws_ ? segDraws_ : minDraws;
    }
    void EndStream(const char* why = "flush");  // the segments stop; the immediate context's list carries on
    void BeginReplay(const Segment& s);
    void ReplayChunk(TBuffer* b, uint64_t va);
    void SetReplayVa(TBuffer* b, uint64_t va) { if (b && va) override_[b].va = va; }
    void EndReplay(Segment& s);
    void StreamStatsLog(uint64_t frames);     // segments, streamed draws and waits per frame, then reset   // the shadow state, for the host's per-draw reads
    uint64_t FlushInternal();                 // submits the open list; returns its serial
    void Sync();                              // flush and wait for the GPU
    void Shutdown();
    ID3D12GraphicsCommandList* Cl() { if (seg_ || stale_) EndStream("Cl"); return rec_.cl.Get(); }
    Recording& Rec() { if (seg_ || stale_) EndStream("Rec"); return rec_; }   // a host recording: the segments end first
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

    // ---- parallel translation ----
    template <class T> void SHold(Segment& s, T* o)
    {
        if (!o) return;
        holds_.Add(o);
        s.held.push_back(&o->imm);
    }
    template <class T> void SObj(T* o) { seg_->words.push_back(reinterpret_cast<uint64_t>(o)); SHold(*seg_, o); }
    void SHead(StreamOp op, uint32_t stage = 0, uint32_t start = 0, uint32_t n = 0)
    {
        seg_->words.push_back(uint64_t(op) | uint64_t(stage) << 8 | uint64_t(start) << 16 | uint64_t(n) << 32);
    }
    void SBuf(TBuffer* b);                     // a buffer and its GPU address when renamed
    void SRes(ResCore* c)                      // a resource, held
    {
        seg_->words.push_back(reinterpret_cast<uint64_t>(c->self));
        c->hold(holds_, c->self);
        seg_->held.push_back(c->imm);
    }
    // A copy between two committed resources needs nothing from the application's thread: it is recorded
    // while draws are, and does not end the run. True: recorded (or there is no run to keep).
    bool StreamCopy(ResCore* d, ResCore* s, bool& recorded);
    bool StreamDraw();                         // true: this draw goes into a segment
    void SegDrawn()
    {
        ++seg_->draws;
        if (seg_->lazy && seg_->draws >= minDraws_) Commit();
        if (seg_->draws >= segDraws_) CutSegment();
    }
    // A call that is not a binding or a draw ends the run of draws: the run's length is noted for the next
    // frame's prediction (a run whose place in the frame was short last frame goes straight to the list).
    void RunBreak(const char* why)
    {
        if (!pool_) return;
        if (runDraws_) { if (runIndex_ < 4096) { if (runIndex_ >= lastRuns_.size()) lastRuns_.resize(runIndex_ + 1, 0); lastRuns_[runIndex_] = runDraws_; } ++runIndex_; }
        runDraws_ = 0;
        runMode_ = 0;
        if (seg_ || stale_) EndStream(why);
    }
    void BeginSegment();
    void Commit();                             // a lazy segment splits the list and goes to the workers
    void InlineReplay(Segment* s);             // a short run replayed on this context, into its own list
    void CutSegment();
    uint64_t SubmitPending();
    void DropSegment(Segment* s);

    StreamPool* pool_ = nullptr;               // the immediate context with workers
    uint32_t segDraws_ = 256, minDraws_ = 64;
    uint32_t openQueries_ = 0;                 // begun and not ended: no segments meanwhile
    std::vector<uint32_t> lastRuns_;           // draws in each run of the last frames, by its place in the frame
    uint32_t runIndex_ = 0, runDraws_ = 0;
    uint8_t runMode_ = 0;
    bool predict_ = true;                      // this run: 0 not decided, 1 recorded, 2 straight to the list
    std::vector<Segment*> freeSegs_;           // reused: no allocation per segment
  public:
    void FrameRuns() { RunBreak("Present"); runIndex_ = 0; }   // the swap chain, at every Present
  private:
    Segment* seg_ = nullptr;                   // being recorded
    bool stale_ = false;                       // the open list's D3D12 state is not the shadow's (segments ran)
    struct Pending { std::unique_ptr<Recording> rec; Segment* seg = nullptr; };
    std::vector<Pending> pending_;             // closed, in call order, not yet submitted
    StreamStats sstats_;
    bool replay_ = false;
    std::unordered_map<const TBuffer*, Chunk> override_;   // replay: renamed buffers' GPU addresses

    Device* dev_;
    bool deferred_;
    std::atomic<ULONG> rc_{1};
    PrivateStore priv_;
    BindHolds holds_;               // the bindings' internal references (tx_holds.h)
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
