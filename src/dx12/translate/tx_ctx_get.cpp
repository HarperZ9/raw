// The D3D11 state getters: read back from the shadow state.
#include "tx_context.h"

namespace raw::tx {

template <class T>
static T* Ref(T* p)
{
    if (p) p->AddRef();
    return p;
}

template <class I>
void Context::GetShader(int stage, I** out)
{
    if (!out) return;
    *out = static_cast<TShader<I>*>(s_.st[stage].sh.obj);
    if (*out) (*out)->AddRef();
}

#define TX_STAGE_GET(P, I, S)                                                                        \
    STDMETHODIMP_(void) Context::P##GetConstantBuffers(UINT a, UINT n, ID3D11Buffer** o)             \
    { for (UINT i = 0; i < n; ++i) o[i] = a + i < 14 ? Ref(s_.st[S].cb[a + i].buf) : nullptr; }      \
    STDMETHODIMP_(void) Context::P##GetShaderResources(UINT a, UINT n, ID3D11ShaderResourceView** o) \
    { for (UINT i = 0; i < n; ++i) o[i] = a + i < 128 ? Ref(s_.st[S].srv[a + i]) : nullptr; }        \
    STDMETHODIMP_(void) Context::P##GetSamplers(UINT a, UINT n, ID3D11SamplerState** o)              \
    { for (UINT i = 0; i < n; ++i) o[i] = a + i < 16 ? Ref(s_.st[S].samp[a + i]) : nullptr; }        \
    STDMETHODIMP_(void) Context::P##GetShader(I** o, ID3D11ClassInstance**, UINT* n)                 \
    { GetShader(S, o); if (n) *n = 0; }
TX_STAGE_GET(VS, ID3D11VertexShader, S_VS)
TX_STAGE_GET(HS, ID3D11HullShader, S_HS)
TX_STAGE_GET(DS, ID3D11DomainShader, S_DS)
TX_STAGE_GET(GS, ID3D11GeometryShader, S_GS)
TX_STAGE_GET(PS, ID3D11PixelShader, S_PS)
TX_STAGE_GET(CS, ID3D11ComputeShader, S_CS)

STDMETHODIMP_(void) Context::IAGetInputLayout(ID3D11InputLayout** o) { if (o) *o = Ref(s_.layout); }

STDMETHODIMP_(void) Context::IAGetVertexBuffers(UINT start, UINT n, ID3D11Buffer** b, UINT* strides, UINT* offsets)
{
    TX_TRACE("Context::IAGetVertexBuffers");
    for (UINT i = 0; i < n; ++i) {
        const BufSlot* s = start + i < 32 ? &s_.vb[start + i] : nullptr;
        if (b) b[i] = s ? Ref(s->buf) : nullptr;
        if (strides) strides[i] = s ? s->stride : 0;
        if (offsets) offsets[i] = s ? s->offset : 0;
    }
}

STDMETHODIMP_(void) Context::IAGetIndexBuffer(ID3D11Buffer** b, DXGI_FORMAT* f, UINT* offset)
{
    TX_TRACE("Context::IAGetIndexBuffer");
    if (b) *b = Ref(s_.ib.buf);
    if (f) *f = s_.ibFmt;
    if (offset) *offset = s_.ib.offset;
}

STDMETHODIMP_(void) Context::IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY* t) { if (t) *t = s_.topo; }

STDMETHODIMP_(void) Context::OMGetRenderTargets(UINT n, ID3D11RenderTargetView** rtv, ID3D11DepthStencilView** dsv)
{
    TX_TRACE("Context::OMGetRenderTargets");
    if (rtv) for (UINT i = 0; i < n; ++i) rtv[i] = i < 8 ? Ref(s_.rtv[i]) : nullptr;
    if (dsv) *dsv = Ref(s_.dsv);
}

STDMETHODIMP_(void) Context::OMGetRenderTargetsAndUnorderedAccessViews(UINT n, ID3D11RenderTargetView** rtv,
    ID3D11DepthStencilView** dsv, UINT uavStart, UINT nUav, ID3D11UnorderedAccessView** uav)
{
    TX_TRACE("Context::OMGetRenderTargetsAndUnorderedAccessViews");
    OMGetRenderTargets(n, rtv, dsv);
    if (uav) for (UINT i = 0; i < nUav; ++i) uav[i] = uavStart + i < 8 ? Ref(s_.uav[uavStart + i]) : nullptr;
}

STDMETHODIMP_(void) Context::OMGetBlendState(ID3D11BlendState** b, FLOAT f[4], UINT* mask)
{
    TX_TRACE("Context::OMGetBlendState");
    if (b) *b = Ref(s_.blend);
    if (f) memcpy(f, s_.factor, sizeof(s_.factor));
    if (mask) *mask = s_.sampleMask;
}

STDMETHODIMP_(void) Context::OMGetDepthStencilState(ID3D11DepthStencilState** d, UINT* ref)
{
    TX_TRACE("Context::OMGetDepthStencilState");
    if (d) *d = Ref(s_.ds);
    if (ref) *ref = s_.stencilRef;
}

STDMETHODIMP_(void) Context::SOGetTargets(UINT n, ID3D11Buffer** b)
{
    TX_TRACE("Context::SOGetTargets");
    if (b) for (UINT i = 0; i < n; ++i) b[i] = nullptr;
}

STDMETHODIMP_(void) Context::RSGetState(ID3D11RasterizerState** r) { if (r) *r = Ref(s_.rs); }

STDMETHODIMP_(void) Context::RSGetViewports(UINT* n, D3D11_VIEWPORT* v)
{
    TX_TRACE("Context::RSGetViewports");
    if (!n) return;
    if (v) {
        UINT c = *n < s_.vpCount ? *n : s_.vpCount;
        memcpy(v, s_.vp, c * sizeof(D3D11_VIEWPORT));
        for (UINT i = c; i < *n; ++i) v[i] = D3D11_VIEWPORT{};
    }
    *n = s_.vpCount;
}

STDMETHODIMP_(void) Context::RSGetScissorRects(UINT* n, D3D11_RECT* r)
{
    TX_TRACE("Context::RSGetScissorRects");
    if (!n) return;
    if (r) {
        UINT c = *n < s_.scCount ? *n : s_.scCount;
        memcpy(r, s_.sc, c * sizeof(D3D11_RECT));
        for (UINT i = c; i < *n; ++i) r[i] = D3D11_RECT{};
    }
    *n = s_.scCount;
}

STDMETHODIMP_(void) Context::CSGetUnorderedAccessViews(UINT start, UINT n, ID3D11UnorderedAccessView** u)
{
    TX_TRACE("Context::CSGetUnorderedAccessViews");
    if (u) for (UINT i = 0; i < n; ++i) u[i] = start + i < 8 ? Ref(s_.csUav[start + i]) : nullptr;
}

STDMETHODIMP_(void) Context::GetPredication(ID3D11Predicate** p, BOOL* v)
{
    TX_TRACE("Context::GetPredication");
    if (p) *p = nullptr;
    if (v) *v = FALSE;
}

}  // namespace raw::tx
