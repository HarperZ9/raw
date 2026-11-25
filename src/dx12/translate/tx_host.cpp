// Host access: a renderer on the layer's own D3D12 device and queue (RAW's bridge passes) works on
// the layer's resources between two stretches of the application's D3D11 calls. PrepareForHost
// moves the resources to one state at the end of the work recorded so far and submits it; the
// host's lists, submitted to the same queue after that, find them in that state and leave them in
// it, which is where the layer's tracker expects them next. No fence crosses: the queue orders
// everything. Original work, MIT.
#include "tx_context.h"

namespace raw::tx {

const void* g_contextVtable = nullptr;   // set by the first Context (tx_ctx_core.cpp)

bool IsLayerContext(ID3D11DeviceContext* c)
{
    return c && g_contextVtable && *reinterpret_cast<void* const*>(c) == g_contextVtable;
}

void PeekConstantBuffers(ID3D11DeviceContext* c, UINT stage, UINT start, UINT n, ID3D11Buffer** out)
{
    static_cast<Context*>(c)->PeekCB(stage, start, n, out);
}

ID3D11InputLayout* PeekInputLayout(ID3D11DeviceContext* c) { return Ctx(c)->Bound().layout; }

ID3D11PixelShader* PeekPixelShader(ID3D11DeviceContext* c)
{
    return static_cast<TShader<ID3D11PixelShader>*>(Ctx(c)->Bound().st[S_PS].sh.obj);
}

const D3D11_BLEND_DESC* PeekBlendDesc(ID3D11DeviceContext* c)
{
    const TBlend* b = Ctx(c)->Bound().blend;
    return b ? &b->desc : nullptr;
}

ID3D11Buffer* PeekVertexBuffer(ID3D11DeviceContext* c, UINT slot, UINT* stride, UINT* offset)
{
    const BufSlot* s = slot < 32 ? &Ctx(c)->Bound().vb[slot] : nullptr;
    if (stride) *stride = s ? s->stride : 0;
    if (offset) *offset = s ? s->offset : 0;
    return s ? s->buf : nullptr;
}

ID3D11Buffer* PeekIndexBuffer(ID3D11DeviceContext* c, DXGI_FORMAT* format, UINT* offset)
{
    const State& s = Ctx(c)->Bound();
    if (format) *format = s.ibFmt;
    if (offset) *offset = s.ib.offset;
    return s.ib.buf;
}

ID3D11ShaderResourceView* PeekShaderResource(ID3D11DeviceContext* c, UINT stage, UINT slot, D3D11_SRV_DIMENSION* dim)
{
    TSRV* v = stage < S_COUNT && slot < 128 ? Ctx(c)->Bound().st[stage].srv[slot] : nullptr;
    if (dim) *dim = v ? v->desc.ViewDimension : D3D11_SRV_DIMENSION_UNKNOWN;
    return v;
}

template <class T>
static void Hold(T* o, int delta)
{
    Context* c = o ? o->dev->imm : nullptr;
    if (!c) return;
    if (delta > 0) c->Holds().Add(o); else c->Holds().Drop(o);
}

void HoldBuffer(ID3D11Buffer* b, int delta) { Hold(static_cast<TBuffer*>(b), delta); }
void HoldView(ID3D11ShaderResourceView* v, int delta) { Hold(static_cast<TSRV*>(v), delta); }

void SetDrawWaterHint(ID3D11DeviceContext* c, bool isWater)
{
    if (IsLayerContext(c)) Ctx(c)->SetDrawWaterHint(isWater);
}

ID3D12Resource* NativeResource(ID3D11Resource* r)
{
    ResCore* c = r ? CoreOf(r) : nullptr;
    return c ? c->Res() : nullptr;
}

HRESULT PrepareForHost(ID3D11DeviceContext* context, ID3D11Resource* const* res, UINT n, D3D12_RESOURCE_STATES state)
{
    TX_TRACE("Host::PrepareForHost");
    auto* ctx = static_cast<Context*>(context);
    if (!ctx) return E_INVALIDARG;
    Tracker& t = ctx->Rec().tracker;
    for (UINT i = 0; i < n; ++i) {
        ResCore* c = res[i] ? CoreOf(res[i]) : nullptr;
        if (!c || !c->tr) return E_INVALIDARG;
        t.RequireAll(c->tr, state);
    }
    t.Flush(ctx->Cl());
    ++ctx->Rec().cmdCount;
    ctx->MarkDirty(D_SCAN);   // the list closes: every binding is set again on the next one
    ctx->FlushInternal();
    return S_OK;
}

bool PeekSubdivMesh(ID3D11DeviceContext* context, UINT indexCount, UINT startIndex, INT baseVertex,
                    ID3D12Resource** outVb, ID3D12Resource** outIb, UINT* outIndexCount, UINT* outVertCount)
{
    if (!context || !outVb || !outIb || !outIndexCount || !outVertCount) return false;
    auto* ctx = static_cast<Context*>(context);
    ID3D11Device* d11 = nullptr;
    ctx->GetDevice(&d11);
    if (!d11) return false;
    Device* dev = Dev(d11);
    const bool enabled = dev->geometrySubdiv && dev->subdiv.Ready();
    const uint32_t epoch = dev->subdivEpoch.load(std::memory_order_relaxed);
    d11->Release();
    if (!enabled) return false;

    const State& s = ctx->Bound();
    if (!s.layout || !s.layout->id || !s.vb[0].buf || !s.ib.buf) return false;
    if (s.vb[0].buf->core.store != Store::Committed || s.ib.buf->core.store != Store::Committed) return false;
    if (!s.vb[0].buf->core.tr || !s.ib.buf->core.tr) return false;

    SubdivKey key{};
    key.vbTr = s.vb[0].buf->core.tr.get();
    key.ibTr = s.ib.buf->core.tr.get();
    key.vbOffset = s.vb[0].offset;
    key.ibOffset = s.ib.offset;
    key.startIndex = startIndex;
    key.indexCount = indexCount;
    key.baseVertex = baseVertex;
    key.stride = s.vb[0].stride;
    key.ibFmt = static_cast<uint32_t>(s.ibFmt);
    key.epoch = epoch;

    auto hit = dev->subdiv.Find(key);
    if (!hit || !hit->vbTr || !hit->ibTr || !hit->vbTr->res || !hit->ibTr->res) return false;
    *outVb = hit->vbTr->res.Get();
    *outIb = hit->ibTr->res.Get();
    (*outVb)->AddRef();
    (*outIb)->AddRef();
    *outIndexCount = hit->outIndexCount;
    *outVertCount = hit->vbView.StrideInBytes ? (hit->vbView.SizeInBytes / hit->vbView.StrideInBytes) : 0u;
    return *outVertCount > 0u && *outIndexCount >= 3u;
}

}  // namespace raw::tx
