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

}  // namespace raw::tx
