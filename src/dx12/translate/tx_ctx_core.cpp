// Context lifetime, buffer renaming and the shadow-state reset.
#include "tx_context.h"

namespace raw::tx {

Context::Context(Device* dev, bool deferred) : dev_(dev), deferred_(deferred)
{
    chunks_.Init(&dev->slab);
    rec_.tracker.faults = dev->faults;
    dev_->OpenRecording(rec_);
    BeginList();
    if (deferred_) dev_->AddRef();
}

Context::~Context()
{
    ResetState();
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
}

STDMETHODIMP Context::QueryInterface(REFIID r, void** o)
{
    if (!o) return E_POINTER;
    if (r == __uuidof(IUnknown) || r == __uuidof(ID3D11DeviceChild) || r == __uuidof(ID3D11DeviceContext)) {
        *o = static_cast<ID3D11DeviceContext*>(this);
        AddRef();
        return S_OK;
    }
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
    if (!deferred_ || b->core.store != Store::Renamed) return &b->core.chunk;
    auto ins = renames_.try_emplace(b);
    if (ins.second) { ins.first->second.chunk = b->core.chunk; b->IntAddRef(); }
    return &ins.first->second.chunk;
}

void Context::RenameBuffer(TBuffer* b)
{
    Chunk nc = chunks_.Alloc(b->core.bytes);
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
        if (st.sh.obj) st.sh.ref(st.sh.obj, -1);
        for (auto& c : st.cb) IntAssign<TBuffer>(c.buf, nullptr);
        for (uint32_t i = 0; i < st.srvHi; ++i) IntAssign<TSRV>(st.srv[i], nullptr);
        for (auto& s : st.samp) IntAssign<TSampler>(s, nullptr);
    }
    IntAssign<TInputLayout>(s_.layout, nullptr);
    for (auto& v : s_.vb) IntAssign<TBuffer>(v.buf, nullptr);
    IntAssign<TBuffer>(s_.ib.buf, nullptr);
    for (auto& r : s_.rtv) IntAssign<TRTV>(r, nullptr);
    IntAssign<TDSV>(s_.dsv, nullptr);
    for (auto& u : s_.uav) IntAssign<TUAV>(u, nullptr);
    for (auto& u : s_.csUav) IntAssign<TUAV>(u, nullptr);
    IntAssign<TBlend>(s_.blend, nullptr);
    IntAssign<TDepthStencil>(s_.ds, nullptr);
    IntAssign<TRaster>(s_.rs, nullptr);
    s_ = State{};
    dirty_ = D_ALL;
}

STDMETHODIMP_(void) Context::ClearState() { ResetState(); }

}  // namespace raw::tx
