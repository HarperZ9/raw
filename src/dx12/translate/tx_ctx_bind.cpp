// The D3D11 state setters. They only update the shadow state and dirty flags;
// D3D12 state is written at the next draw.
#include "tx_context.h"

namespace raw::tx {

template <class T>
void Context::SetShader(int stage, T* s)
{
    ShaderSlot& slot = s_.st[stage].sh;
    if (slot.obj == s) return;
    if (slot.obj) slot.ref(slot.obj, -1);
    slot = ShaderSlot{};
    if (s) {
        s->IntAddRef();
        slot.obj = s;
        slot.sd = s->sd;
        slot.ref = [](void* p, int delta) {
            if (delta > 0) static_cast<T*>(p)->IntAddRef(); else static_cast<T*>(p)->IntRelease();
        };
    }
    dirty_ |= D_PSO;
}

void Context::SetCB(int stage, UINT start, UINT n, ID3D11Buffer* const* b)
{
    for (UINT i = 0; i < n && start + i < 14; ++i) {
        BufSlot& s = s_.st[stage].cb[start + i];
        TBuffer* nb = b ? static_cast<TBuffer*>(b[i]) : nullptr;
        IntAssign(s.buf, nb);
        s.chunk = nb ? Pin(nb) : nullptr;
    }
}

static bool Overlap(const SubRange& a, const SubRange& b)
{
    if (!(a.planeMask & b.planeMask)) return false;
    if (a.all || b.all) return true;
    return a.mip0 < b.mip0 + b.mipN && b.mip0 < a.mip0 + a.mipN &&
           a.slice0 < b.slice0 + b.sliceN && b.slice0 < a.slice0 + a.sliceN;
}

// D3D11 rule: a resource bound for writing cannot also be bound as a shader
// input; the input side is bound as null.
bool Context::TargetConflict(const TSRV* v) const
{
    for (uint32_t i = 0; i < s_.rtCount; ++i)
        if (s_.rtv[i] && s_.rtv[i]->core == v->core && Overlap(s_.rtv[i]->range, v->range)) return true;
    const UINT ro = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;
    if (s_.dsv && s_.dsv->core == v->core && (s_.dsv->desc.Flags & ro) == 0) return true;
    return false;
}

void Context::UnbindSrvsOf(const ResCore* core, const SubRange& range)
{
    for (int st = 0; st < S_COUNT; ++st) {
        StageState& s = s_.st[st];
        for (uint32_t i = 0; i < s.srvHi; ++i) {
            if (!s.srv[i] || s.srv[i]->core != core || !Overlap(range, s.srv[i]->range)) continue;
            IntAssign<TSRV>(s.srv[i], nullptr);
            dirty_ |= D_SRV0 << st;
        }
    }
}

void Context::SetSRV(int stage, UINT start, UINT n, ID3D11ShaderResourceView* const* v)
{
    StageState& s = s_.st[stage];
    for (UINT i = 0; i < n && start + i < 128; ++i) {
        TSRV* nv = v ? static_cast<TSRV*>(v[i]) : nullptr;
        if (nv && stage != S_CS && TargetConflict(nv)) nv = nullptr;
        if (s.srv[start + i] == nv) continue;
        IntAssign(s.srv[start + i], nv);
        if (nv && start + i + 1 > s.srvHi) s.srvHi = start + i + 1;
        dirty_ |= (D_SRV0 << stage) | D_SCAN;
    }
}

void Context::SetSamp(int stage, UINT start, UINT n, ID3D11SamplerState* const* p)
{
    for (UINT i = 0; i < n && start + i < 16; ++i) {
        TSampler* ns = p ? static_cast<TSampler*>(p[i]) : nullptr;
        if (s_.st[stage].samp[start + i] == ns) continue;
        IntAssign(s_.st[stage].samp[start + i], ns);
        dirty_ |= D_SAMP0 << stage;
    }
}

#define TX_STAGE_SET(P, I, S)                                                                                  \
    STDMETHODIMP_(void) Context::P##SetConstantBuffers(UINT a, UINT n, ID3D11Buffer* const* b) { SetCB(S, a, n, b); } \
    STDMETHODIMP_(void) Context::P##SetShaderResources(UINT a, UINT n, ID3D11ShaderResourceView* const* v) { SetSRV(S, a, n, v); } \
    STDMETHODIMP_(void) Context::P##SetSamplers(UINT a, UINT n, ID3D11SamplerState* const* s) { SetSamp(S, a, n, s); } \
    STDMETHODIMP_(void) Context::P##SetShader(I* s, ID3D11ClassInstance* const*, UINT) { SetShader(S, static_cast<TShader<I>*>(s)); }
TX_STAGE_SET(VS, ID3D11VertexShader, S_VS)
TX_STAGE_SET(HS, ID3D11HullShader, S_HS)
TX_STAGE_SET(DS, ID3D11DomainShader, S_DS)
TX_STAGE_SET(GS, ID3D11GeometryShader, S_GS)
TX_STAGE_SET(PS, ID3D11PixelShader, S_PS)
TX_STAGE_SET(CS, ID3D11ComputeShader, S_CS)

STDMETHODIMP_(void) Context::IASetInputLayout(ID3D11InputLayout* l)
{
    IntAssign(s_.layout, static_cast<TInputLayout*>(l));
    dirty_ |= D_PSO;
}

STDMETHODIMP_(void) Context::IASetVertexBuffers(UINT start, UINT n, ID3D11Buffer* const* b, const UINT* strides, const UINT* offsets)
{
    for (UINT i = 0; i < n && start + i < 32; ++i) {
        BufSlot& s = s_.vb[start + i];
        TBuffer* nb = b ? static_cast<TBuffer*>(b[i]) : nullptr;
        IntAssign(s.buf, nb);
        s.chunk = nb ? Pin(nb) : nullptr;
        s.stride = strides ? strides[i] : 0;
        s.offset = offsets ? offsets[i] : 0;
        if (nb && start + i + 1 > s_.vbHi) s_.vbHi = start + i + 1;
    }
    dirty_ |= D_VB | D_SCAN;
}

STDMETHODIMP_(void) Context::IASetIndexBuffer(ID3D11Buffer* b, DXGI_FORMAT f, UINT offset)
{
    TBuffer* nb = static_cast<TBuffer*>(b);
    IntAssign(s_.ib.buf, nb);
    s_.ib.chunk = nb ? Pin(nb) : nullptr;
    s_.ib.offset = offset;
    if (s_.ibFmt != f) dirty_ |= D_PSO;
    s_.ibFmt = f;
    dirty_ |= D_IB | D_SCAN;
}

STDMETHODIMP_(void) Context::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY t)
{
    if (s_.topo == t) return;
    s_.topo = t;
    dirty_ |= D_TOPO | D_PSO;
}

void Context::SetTargets(UINT n, ID3D11RenderTargetView* const* rtv, ID3D11DepthStencilView* dsv, bool)
{
    if (n > 8) n = 8;
    uint32_t hi = 0;
    for (UINT i = 0; i < 8; ++i) {
        TRTV* v = (i < n && rtv) ? static_cast<TRTV*>(rtv[i]) : nullptr;
        IntAssign(s_.rtv[i], v);
        if (v) hi = i + 1;
    }
    s_.rtCount = hi;
    IntAssign(s_.dsv, static_cast<TDSV*>(dsv));
    dirty_ |= D_RT | D_PSO | D_SCAN | D_SC;
    for (uint32_t i = 0; i < hi; ++i)
        if (s_.rtv[i]) UnbindSrvsOf(s_.rtv[i]->core, s_.rtv[i]->range);
    const UINT ro = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;
    if (s_.dsv && (s_.dsv->desc.Flags & ro) == 0) UnbindSrvsOf(s_.dsv->core, s_.dsv->range);
}

STDMETHODIMP_(void) Context::OMSetRenderTargets(UINT n, ID3D11RenderTargetView* const* rtv, ID3D11DepthStencilView* dsv)
{
    SetTargets(n, rtv, dsv, false);
    for (auto& u : s_.uav) IntAssign<TUAV>(u, nullptr);
    dirty_ |= D_UAV;
}

STDMETHODIMP_(void) Context::OMSetRenderTargetsAndUnorderedAccessViews(UINT nRtv, ID3D11RenderTargetView* const* rtv,
    ID3D11DepthStencilView* dsv, UINT uavStart, UINT nUav, ID3D11UnorderedAccessView* const* uav, const UINT*)
{
    if (nRtv != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) SetTargets(nRtv, rtv, dsv, false);
    if (nUav == D3D11_KEEP_UNORDERED_ACCESS_VIEWS) return;
    for (auto& u : s_.uav) IntAssign<TUAV>(u, nullptr);
    for (UINT i = 0; i < nUav && uavStart + i < 8; ++i)
        IntAssign(s_.uav[uavStart + i], uav ? static_cast<TUAV*>(uav[i]) : nullptr);
    dirty_ |= D_UAV | D_SCAN;
}

STDMETHODIMP_(void) Context::OMSetBlendState(ID3D11BlendState* b, const FLOAT f[4], UINT mask)
{
    IntAssign(s_.blend, static_cast<TBlend*>(b));
    for (int i = 0; i < 4; ++i) s_.factor[i] = f ? f[i] : 1.0f;
    s_.sampleMask = mask;
    dirty_ |= D_PSO | D_BF;
}

STDMETHODIMP_(void) Context::OMSetDepthStencilState(ID3D11DepthStencilState* d, UINT ref)
{
    IntAssign(s_.ds, static_cast<TDepthStencil*>(d));
    s_.stencilRef = ref;
    dirty_ |= D_PSO | D_SREF;
}

STDMETHODIMP_(void) Context::RSSetState(ID3D11RasterizerState* r)
{
    IntAssign(s_.rs, static_cast<TRaster*>(r));
    dirty_ |= D_PSO | D_SC;
}

STDMETHODIMP_(void) Context::RSSetViewports(UINT n, const D3D11_VIEWPORT* v)
{
    s_.vpCount = n > 16 ? 16 : n;
    if (v) memcpy(s_.vp, v, s_.vpCount * sizeof(D3D11_VIEWPORT));
    dirty_ |= D_VP | D_SC;
}

STDMETHODIMP_(void) Context::RSSetScissorRects(UINT n, const D3D11_RECT* r)
{
    s_.scCount = n > 16 ? 16 : n;
    if (r) memcpy(s_.sc, r, s_.scCount * sizeof(D3D11_RECT));
    dirty_ |= D_SC;
}

STDMETHODIMP_(void) Context::CSSetUnorderedAccessViews(UINT start, UINT n, ID3D11UnorderedAccessView* const* u, const UINT*)
{
    for (UINT i = 0; i < n && start + i < 8; ++i)
        IntAssign(s_.csUav[start + i], u ? static_cast<TUAV*>(u[i]) : nullptr);
    dirty_ |= D_CSUAV;
}

STDMETHODIMP_(void) Context::SOSetTargets(UINT n, ID3D11Buffer* const* b, const UINT*)
{
    for (UINT i = 0; i < n; ++i)
        if (b && b[i]) { CountUnsupported(dev_, "SOSetTargets (stream output)"); return; }
}

STDMETHODIMP_(void) Context::SetPredication(ID3D11Predicate* p, BOOL)
{
    if (p) CountUnsupported(dev_, "SetPredication");
}

}  // namespace raw::tx
