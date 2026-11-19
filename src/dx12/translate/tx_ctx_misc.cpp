// Clears, queries, submission and command lists.
#include "tx_context.h"

namespace raw::tx {

STDMETHODIMP_(void) Context::ClearRenderTargetView(ID3D11RenderTargetView* view, const FLOAT color[4])
{
    RunBreak(__func__);
    TX_TRACE("Context::ClearRenderTargetView");
    TRTV* v = static_cast<TRTV*>(view);
    if (!v) return;
    if (rec_.tracker.RequireRange(v->core->tr, v->range, v->core->mips, v->core->slices, D3D12_RESOURCE_STATE_RENDER_TARGET))
        dirty_ |= D_SCAN;
    rec_.tracker.Flush(Cl());
    Cl()->ClearRenderTargetView(v->h, color, 0, nullptr);
    ++rec_.cmdCount;
}

STDMETHODIMP_(void) Context::ClearDepthStencilView(ID3D11DepthStencilView* view, UINT flags, FLOAT depth, UINT8 stencil)
{
    RunBreak(__func__);
    TX_TRACE("Context::ClearDepthStencilView");
    TDSV* v = static_cast<TDSV*>(view);
    if (!v) return;
    ResCore& c = *v->core;
    SubRange r = v->range;
    r.all = false;
    UINT f = 0;
    if (flags & D3D11_CLEAR_DEPTH) { f |= D3D12_CLEAR_FLAG_DEPTH; r.planeMask = 1; }
    if ((flags & D3D11_CLEAR_STENCIL) && c.planes > 1) { f |= D3D12_CLEAR_FLAG_STENCIL; r.planeMask |= 2; }
    if (!f) return;
    if (!(flags & D3D11_CLEAR_DEPTH)) r.planeMask = 2;
    if (rec_.tracker.RequireRange(c.tr, r, c.mips, c.slices, D3D12_RESOURCE_STATE_DEPTH_WRITE)) dirty_ |= D_SCAN;
    rec_.tracker.Flush(Cl());
    Cl()->ClearDepthStencilView(v->h, static_cast<D3D12_CLEAR_FLAGS>(f), depth, stencil, 0, nullptr);
    ++rec_.cmdCount;
}

void Context::ClearUav(ID3D11UnorderedAccessView* view, const UINT* ui, const FLOAT* fl)
{
    RunBreak(__func__);
    TUAV* v = static_cast<TUAV*>(view);
    if (!v || !v->core->tr) return;
    ResCore& c = *v->core;
    if (rec_.tracker.RequireRange(c.tr, v->range, c.mips, c.slices, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)) dirty_ |= D_SCAN;
    rec_.tracker.Flush(Cl());
    uint32_t idx = AllocDescs(1);
    dev_->d3d->CopyDescriptorsSimple(1, CpuHandle(idx), v->h, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    if (ui) Cl()->ClearUnorderedAccessViewUint(GpuHandle(idx), v->h, c.Res(), ui, 0, nullptr);
    else Cl()->ClearUnorderedAccessViewFloat(GpuHandle(idx), v->h, c.Res(), fl, 0, nullptr);
    ++rec_.cmdCount;
}
STDMETHODIMP_(void) Context::ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView* v, const UINT c[4]) { ClearUav(v, c, nullptr); }
STDMETHODIMP_(void) Context::ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView* v, const FLOAT c[4]) { ClearUav(v, nullptr, c); }

// ---- queries ----

static bool HeapQuery(const TQuery* q, QueryHeapKind* heap, D3D12_QUERY_TYPE* type)
{
    switch (q->desc.Query) {
    case D3D11_QUERY_OCCLUSION: *heap = QH_Occlusion; *type = D3D12_QUERY_TYPE_OCCLUSION; return true;
    case D3D11_QUERY_OCCLUSION_PREDICATE: *heap = QH_Occlusion; *type = D3D12_QUERY_TYPE_BINARY_OCCLUSION; return true;
    case D3D11_QUERY_PIPELINE_STATISTICS: *heap = QH_Stats; *type = D3D12_QUERY_TYPE_PIPELINE_STATISTICS; return true;
    case D3D11_QUERY_TIMESTAMP: *heap = QH_Timestamp; *type = D3D12_QUERY_TYPE_TIMESTAMP; return true;
    default: return false;
    }
}

STDMETHODIMP_(void) Context::Begin(ID3D11Asynchronous* a)
{
    RunBreak(__func__);
    TX_TRACE("Context::Begin");
    TQuery* q = static_cast<TQuery*>(a);
    QueryHeapKind heap; D3D12_QUERY_TYPE type;
    if (!q || !HeapQuery(q, &heap, &type) || type == D3D12_QUERY_TYPE_TIMESTAMP) return;
    Cl()->BeginQuery(dev_->qh[heap].heap.Get(), type, q->slot);
    ++rec_.cmdCount;
    ++openQueries_;   // a D3D12 query begins and ends in one list: draws stay on this one until End
}

STDMETHODIMP_(void) Context::End(ID3D11Asynchronous* a)
{
    RunBreak(__func__);
    TX_TRACE("Context::End");
    TQuery* q = static_cast<TQuery*>(a);
    if (!q) return;
    QueryHeapKind heap; D3D12_QUERY_TYPE type;
    if (HeapQuery(q, &heap, &type)) {
        if (type != D3D12_QUERY_TYPE_TIMESTAMP && openQueries_) --openQueries_;
        QueryHeap& h = dev_->qh[heap];
        Cl()->EndQuery(h.heap.Get(), type, q->slot);
        Cl()->ResolveQueryData(h.heap.Get(), type, q->slot, 1, h.readback.Get(), UINT64(q->slot) * h.stride);
    }
    q->serial = ~0ull;   // known once the list holding it is submitted
    q->issued = true;
    q->IntAddRef();
    rec_.queries.push_back(q);
    ++rec_.cmdCount;
}

STDMETHODIMP Context::GetData(ID3D11Asynchronous* a, void* data, UINT size, UINT flags)
{
    TX_TRACE("Context::GetData");
    TQuery* q = static_cast<TQuery*>(a);
    if (!q) return E_INVALIDARG;
    if (deferred_) return DXGI_ERROR_INVALID_CALL;
    if (!q->issued) return S_FALSE;
    if (q->serial == ~0ull && !(flags & D3D11_ASYNC_GETDATA_DONOTFLUSH)) FlushInternal();
    uint64_t serial = q->serial;
    if (serial == ~0ull || dev_->Completed() < serial) return S_FALSE;
    if (!data) return S_OK;
    if (size < q->GetDataSize()) return E_INVALIDARG;
    QueryHeapKind heap; D3D12_QUERY_TYPE type;
    const uint8_t* src = HeapQuery(q, &heap, &type) ? dev_->qh[heap].mapped + size_t(q->slot) * dev_->qh[heap].stride : nullptr;
    switch (q->desc.Query) {
    case D3D11_QUERY_EVENT: *static_cast<BOOL*>(data) = TRUE; break;
    case D3D11_QUERY_OCCLUSION_PREDICATE: { UINT64 v; memcpy(&v, src, 8); *static_cast<BOOL*>(data) = v != 0; break; }
    case D3D11_QUERY_TIMESTAMP_DISJOINT: {
        auto* d = static_cast<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT*>(data);
        d->Disjoint = FALSE;
        dev_->queue->GetTimestampFrequency(&d->Frequency);
        break;
    }
    case D3D11_QUERY_PIPELINE_STATISTICS: memcpy(data, src, sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS)); break;   // same eleven counters, same order
    default: memcpy(data, src, 8); break;
    }
    return S_OK;
}

STDMETHODIMP_(void) Context::SetResourceMinLOD(ID3D11Resource*, FLOAT lod)
{
    TX_TRACE("Context::SetResourceMinLOD");
    if (lod != 0.0f) CountUnsupported(dev_, "SetResourceMinLOD");
}
STDMETHODIMP_(FLOAT) Context::GetResourceMinLOD(ID3D11Resource*) { return 0.0f; }

// ---- submission ----

uint64_t Context::FlushInternal()
{
    if (deferred_) return 0;
    holds_.Drain(1024);   // pins of objects the game released and nothing binds give their reference back
    RunBreak(__func__);
    if (!pending_.empty()) return SubmitPending();
    if (rec_.cmdCount == 0 && !dev_->UploadPending()) return dev_->LastSubmitted();
    rec_.cl->Close();
    Recording* r = &rec_;
    uint64_t serial = dev_->Submit(&r, 1, true);
    dev_->OpenRecording(rec_);
    BeginList();
    return serial;
}

void Context::Sync()
{
    if (deferred_) return;
    dev_->WaitFor(FlushInternal());
}

STDMETHODIMP_(void) Context::Flush() { FlushInternal(); }

TCommandList::~TCommandList()
{
    Retire r = std::move(rec.retire);
    for (auto& kv : renames) {
        if (kv.second.local && !kv.second.adopted) r.chunks.push_back(kv.second.chunk);
        kv.first->IntRelease();
    }
    rec.tracker.Keep(r.keep);
    for (TQuery* q : rec.queries) q->IntRelease();
    dev->RetireLater(std::move(r));
    dev->RecycleList(rec);
}

// The deferred context recorded a complete D3D12 command list on its own
// thread. Finishing closes it and hands it over; nothing is re-recorded.
STDMETHODIMP Context::FinishCommandList(BOOL restore, ID3D11CommandList** out)
{
    TX_TRACE("Context::FinishCommandList");
    if (!deferred_) return DXGI_ERROR_INVALID_CALL;
    rec_.cl->Close();
    auto* l = new TCommandList(dev_);
    if (descBase_ != ~0u) { rec_.retire.descPages.push_back(descBase_); descBase_ = ~0u; }
    l->rec = std::move(rec_);
    l->renames = std::move(renames_);
    renames_.clear();
    for (auto& kv : l->renames) {   // later lists of this context still see what it wrote
        if (!kv.second.local) continue;
        renames_[kv.first].chunk = kv.second.chunk;
        kv.first->IntAddRef();
    }
    rec_ = Recording{};
    rec_.tracker.faults = dev_->faults;
    dev_->OpenRecording(rec_);
    BeginList();
    if (restore) Repin(); else ResetState();
    if (out) *out = l; else l->Release();
    return S_OK;
}

STDMETHODIMP_(void) Context::ExecuteCommandList(ID3D11CommandList* list, BOOL restore)
{
    TX_TRACE("Context::ExecuteCommandList");
    if (deferred_ || !list) return;
    TCommandList* l = static_cast<TCommandList*>(list);
    if (rec_.cmdCount || stale_ || !pending_.empty()) FlushInternal();   // what the immediate context recorded runs first
    Recording* r = &l->rec;
    dev_->Submit(&r, 1, false);
    for (auto& kv : l->renames) {         // the buffers the list wrote now hold its contents
        Rename& e = kv.second;
        if (!e.local) continue;
        TBuffer* b = kv.first;
        if (b->core.chunk.cpu != e.chunk.cpu) {
            rec_.retire.chunks.push_back(b->core.chunk);
            b->core.chunk = e.chunk;
        }
        e.adopted = true;
    }
    dirty_ |= D_VB | D_IB;
    if (!restore) ResetState();
}

}  // namespace raw::tx
