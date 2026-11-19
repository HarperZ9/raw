// The output-merger, rasterizer and compute-UAV setters (the others are in tx_ctx_bind.cpp). They update the
// shadow state and dirty flags, and record into an open segment (tx_stream.h).
#include "tx_context.h"

namespace raw::tx {

void Context::SetTargets(UINT n, ID3D11RenderTargetView* const* rtv, ID3D11DepthStencilView* dsv, bool)
{
    if (n > 8) n = 8;
    uint32_t hi = 0;
    for (UINT i = 0; i < 8; ++i) {
        TRTV* v = (i < n && rtv) ? static_cast<TRTV*>(rtv[i]) : nullptr;
        holds_.Assign(s_.rtv[i], v);
        if (v) hi = i + 1;
    }
    s_.rtCount = hi;
    holds_.Assign(s_.dsv, static_cast<TDSV*>(dsv));
    dirty_ |= D_RT | D_PSO | D_SCAN | D_SC;
    for (uint32_t i = 0; i < hi; ++i)
        if (s_.rtv[i]) UnbindSrvsOf(s_.rtv[i]->core, s_.rtv[i]->range);
    const UINT ro = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;
    if (s_.dsv && (s_.dsv->desc.Flags & ro) == 0) UnbindSrvsOf(s_.dsv->core, s_.dsv->range);
}

STDMETHODIMP_(void) Context::OMSetRenderTargets(UINT n, ID3D11RenderTargetView* const* rtv, ID3D11DepthStencilView* dsv)
{
    TX_TRACE("Context::OMSetRenderTargets");
    if (seg_) {
        SHead(SO_RT, 0, 0, n);
        for (UINT i = 0; i < n && i < 8; ++i) SObj(rtv ? static_cast<TRTV*>(rtv[i]) : nullptr);
        SObj(static_cast<TDSV*>(dsv));
    }
    SetTargets(n, rtv, dsv, false);
    for (auto& u : s_.uav) holds_.Assign<TUAV>(u, nullptr);
    dirty_ |= D_UAV;
}

STDMETHODIMP_(void) Context::OMSetRenderTargetsAndUnorderedAccessViews(UINT nRtv, ID3D11RenderTargetView* const* rtv,
    ID3D11DepthStencilView* dsv, UINT uavStart, UINT nUav, ID3D11UnorderedAccessView* const* uav, const UINT*)
{
    TX_TRACE("Context::OMSetRenderTargetsAndUnorderedAccessViews");
    if (seg_) {
        SHead(SO_RTUAV, 0, uavStart, nRtv);
        seg_->words.push_back(nUav);
        if (nRtv != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) {
            for (UINT i = 0; i < nRtv && i < 8; ++i) SObj(rtv ? static_cast<TRTV*>(rtv[i]) : nullptr);
            SObj(static_cast<TDSV*>(dsv));
        }
        if (nUav != D3D11_KEEP_UNORDERED_ACCESS_VIEWS)
            for (UINT i = 0; i < nUav && uavStart + i < 8; ++i) SObj(uav ? static_cast<TUAV*>(uav[i]) : nullptr);
    }
    if (nRtv != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) SetTargets(nRtv, rtv, dsv, false);
    if (nUav == D3D11_KEEP_UNORDERED_ACCESS_VIEWS) return;
    for (auto& u : s_.uav) holds_.Assign<TUAV>(u, nullptr);
    for (UINT i = 0; i < nUav && uavStart + i < 8; ++i)
        holds_.Assign(s_.uav[uavStart + i], uav ? static_cast<TUAV*>(uav[i]) : nullptr);
    dirty_ |= D_UAV | D_SCAN;
}

STDMETHODIMP_(void) Context::OMSetBlendState(ID3D11BlendState* b, const FLOAT f[4], UINT mask)
{
    TX_TRACE("Context::OMSetBlendState");
    if (seg_) {
        SHead(SO_Blend, 0, 0, f ? 1 : 0);
        SObj(static_cast<TBlend*>(b));
        uint64_t w[2] = {};
        if (f) memcpy(w, f, 16);
        seg_->words.push_back(w[0]); seg_->words.push_back(w[1]); seg_->words.push_back(mask);
    }
    holds_.Assign(s_.blend, static_cast<TBlend*>(b));
    for (int i = 0; i < 4; ++i) s_.factor[i] = f ? f[i] : 1.0f;
    s_.sampleMask = mask;
    dirty_ |= D_PSO | D_BF;
}

STDMETHODIMP_(void) Context::OMSetDepthStencilState(ID3D11DepthStencilState* d, UINT ref)
{
    TX_TRACE("Context::OMSetDepthStencilState");
    if (seg_) { SHead(SO_DS, 0, 0, ref); SObj(static_cast<TDepthStencil*>(d)); }
    holds_.Assign(s_.ds, static_cast<TDepthStencil*>(d));
    s_.stencilRef = ref;
    dirty_ |= D_PSO | D_SREF;
}

STDMETHODIMP_(void) Context::RSSetState(ID3D11RasterizerState* r)
{
    TX_TRACE("Context::RSSetState");
    if (seg_) { SHead(SO_RS); SObj(static_cast<TRaster*>(r)); }
    holds_.Assign(s_.rs, static_cast<TRaster*>(r));
    dirty_ |= D_PSO | D_SC;
}

STDMETHODIMP_(void) Context::RSSetViewports(UINT n, const D3D11_VIEWPORT* v)
{
    TX_TRACE("Context::RSSetViewports");
    if (seg_) {
        const UINT c = n > 16 ? 16 : n;
        SHead(SO_VP, v ? 1 : 0, 0, c);
        if (v) { const size_t at = seg_->words.size(); seg_->words.resize(at + c * 3); memcpy(&seg_->words[at], v, c * sizeof(D3D11_VIEWPORT)); }
        else seg_->words.push_back(n);
    }
    s_.vpCount = n > 16 ? 16 : n;
    if (v) memcpy(s_.vp, v, s_.vpCount * sizeof(D3D11_VIEWPORT));
    dirty_ |= D_VP | D_SC;
}

STDMETHODIMP_(void) Context::RSSetScissorRects(UINT n, const D3D11_RECT* r)
{
    TX_TRACE("Context::RSSetScissorRects");
    if (seg_) {
        const UINT c = n > 16 ? 16 : n;
        SHead(SO_SC, r ? 1 : 0, 0, c);
        if (r) { const size_t at = seg_->words.size(); seg_->words.resize(at + c * 2); memcpy(&seg_->words[at], r, c * sizeof(D3D11_RECT)); }
        else seg_->words.push_back(n);
    }
    s_.scCount = n > 16 ? 16 : n;
    if (r) memcpy(s_.sc, r, s_.scCount * sizeof(D3D11_RECT));
    dirty_ |= D_SC;
}

STDMETHODIMP_(void) Context::CSSetUnorderedAccessViews(UINT start, UINT n, ID3D11UnorderedAccessView* const* u, const UINT*)
{
    TX_TRACE("Context::CSSetUnorderedAccessViews");
    if (seg_) {
        SHead(SO_CSUAV, 0, start, n);
        for (UINT i = 0; i < n; ++i) SObj(u ? static_cast<TUAV*>(u[i]) : nullptr);
    }
    for (UINT i = 0; i < n && start + i < 8; ++i)
        holds_.Assign(s_.csUav[start + i], u ? static_cast<TUAV*>(u[i]) : nullptr);
    dirty_ |= D_CSUAV;
}

STDMETHODIMP_(void) Context::SOSetTargets(UINT n, ID3D11Buffer* const* b, const UINT*)
{
    TX_TRACE("Context::SOSetTargets");
    for (UINT i = 0; i < n; ++i)
        if (b && b[i]) { CountUnsupported(dev_, "SOSetTargets (stream output)"); return; }
}

STDMETHODIMP_(void) Context::SetPredication(ID3D11Predicate* p, BOOL)
{
    TX_TRACE("Context::SetPredication");
    if (seg_ || stale_) EndStream(__func__);
    if (p) CountUnsupported(dev_, "SetPredication");
}

}  // namespace raw::tx
