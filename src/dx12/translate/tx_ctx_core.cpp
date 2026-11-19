// Context lifetime, buffer renaming and the shadow-state reset.
#include "tx_context.h"

namespace raw::tx {

extern const void* g_contextVtable;   // tx_host.cpp

Context::Context(Device* dev, bool deferred, bool replay)
    : replay_(replay), dev_(dev), deferred_(deferred), holds_(replay ? BindHolds::kNone : deferred ? BindHolds::kAtomic : BindHolds::kPlain)
{
    g_contextVtable = *reinterpret_cast<void* const*>(this);
    chunks_.Init(&dev->slab, !deferred && dev->renameRing);   // the immediate context renames through the ring
    rec_.tracker.faults = dev->faults;
    if (!deferred_) rec_.tracker.EnableSlots();   // the immediate context's list: hash-free state lookups
    dev_->OpenRecording(rec_);
    BeginList();
    if (deferred_ && !replay_) dev_->AddRef();
}

Context::~Context()
{
    if (seg_ || stale_) EndStream(__func__);
    if (!pending_.empty()) SubmitPending();
    ResetState();
    holds_.ReleaseAll();
    if (rec_.cl) rec_.cl->Close();
    Retire r = std::move(rec_.retire);
    if (descBase_ != ~0u) r.descPages.push_back(descBase_);
    for (auto& kv : renames_) {
        if (kv.second.local && !kv.second.adopted) r.chunks.push_back(kv.second.chunk);
        kv.first->IntRelease();
    }
    rec_.tracker.Keep(r.keep);
    for (TQuery* q : rec_.queries) q->IntRelease();
    dev_->RetireLater(std::move(r));
    dev_->RecycleList(rec_);
    chunks_.ReturnAll();
}

void Context::Shutdown()
{
    ResetState();
    FlushInternal();
    holds_.ReleaseAll();
}

STDMETHODIMP Context::QueryInterface(REFIID r, void** o)
{
    if (!o) return E_POINTER;
    if (r == __uuidof(IUnknown) || r == __uuidof(ID3D11DeviceChild) || r == __uuidof(ID3D11DeviceContext)) {
        *o = static_cast<ID3D11DeviceContext*>(this);
        AddRef();
        return S_OK;
    }
    LogIid("context", r);
    *o = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) Context::AddRef() { return deferred_ ? ++rc_ : dev_->AddRef(); }
STDMETHODIMP_(ULONG) Context::Release()
{
    if (!deferred_) return dev_->Release();
    ULONG n = --rc_;
    if (n == 0) {
        Device* d = dev_;
        delete this;
        d->Release();
    }
    return n;
}

void Context::BeginList()
{
    ID3D12DescriptorHeap* heaps[2] = {dev_->gpuHeap.Get(), dev_->samplerHeap.Get()};
    rec_.cl->SetDescriptorHeaps(2, heaps);
    dirty_ = D_ALL;
    root_ = croot_ = nullptr;
    pso_ = nullptr;
    memset(cbVa_, 0xFF, sizeof(cbVa_));
    memset(cbBuf_, 0, sizeof(cbBuf_));
    for (auto& k : sampKey_) k = ~0ull;
}

const Chunk* Context::Pin(TBuffer* b)
{
    if (replay_ && b->core.store == Store::Renamed) return &override_[b];   // the address the segment recorded
    if (!deferred_ || b->core.store != Store::Renamed) return &b->core.chunk;
    auto ins = renames_.try_emplace(b);
    if (ins.second) { ins.first->second.chunk = b->core.chunk; b->IntAddRef(); }
    return &ins.first->second.chunk;
}

void Context::RenameBuffer(TBuffer* b)
{
    Chunk nc = chunks_.AllocRename(b->core.bytes);
    if (deferred_) {
        auto ins = renames_.try_emplace(b);
        Rename& e = ins.first->second;
        if (ins.second) b->IntAddRef();
        else if (e.local) rec_.retire.chunks.push_back(e.chunk);
        e.chunk = nc;
        e.local = true;
    } else {
        rec_.retire.chunks.push_back(b->core.chunk);
        b->core.chunk = nc;
    }
    if (b->core.bind & D3D11_BIND_VERTEX_BUFFER) dirty_ |= D_VB;
    if (b->core.bind & D3D11_BIND_INDEX_BUFFER) dirty_ |= D_IB;
}

void Context::Repin()
{
    auto fix = [this](BufSlot& s) { if (s.buf) s.chunk = Pin(s.buf); };
    for (auto& st : s_.st) for (auto& c : st.cb) fix(c);
    for (auto& v : s_.vb) fix(v);
    fix(s_.ib);
}

void Context::ResetState()
{
    for (auto& st : s_.st) {
        if (st.sh.obj) st.sh.drop(holds_, st.sh.obj);
        for (auto& c : st.cb) holds_.Assign<TBuffer>(c.buf, nullptr);
        for (uint32_t i = 0; i < st.srvHi; ++i) holds_.Assign<TSRV>(st.srv[i], nullptr);
        for (auto& s : st.samp) holds_.Assign<TSampler>(s, nullptr);
    }
    holds_.Assign<TInputLayout>(s_.layout, nullptr);
    for (auto& v : s_.vb) holds_.Assign<TBuffer>(v.buf, nullptr);
    holds_.Assign<TBuffer>(s_.ib.buf, nullptr);
    for (auto& r : s_.rtv) holds_.Assign<TRTV>(r, nullptr);
    holds_.Assign<TDSV>(s_.dsv, nullptr);
    for (auto& u : s_.uav) holds_.Assign<TUAV>(u, nullptr);
    for (auto& u : s_.csUav) holds_.Assign<TUAV>(u, nullptr);
    holds_.Assign<TBlend>(s_.blend, nullptr);
    holds_.Assign<TDepthStencil>(s_.ds, nullptr);
    holds_.Assign<TRaster>(s_.rs, nullptr);
    s_ = State{};
    dirty_ = D_ALL;
}

STDMETHODIMP_(void) Context::ClearState()
{
    if (seg_ || stale_) EndStream(__func__);
    ResetState();
}

}  // namespace raw::tx
