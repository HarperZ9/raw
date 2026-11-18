// FrameLedger events of an armed frame: target binds, draws, clears, copies and
// dispatches. Original work, MIT.
#include "FrameLedgerImpl.h"
#include "CBDirtyTracker.h"

namespace SB::Proxy
{
using namespace ledger;

namespace
{
// The shadow copy of a constant buffer, as the game last wrote it.
void AppendCb(State& s, ID3D11Buffer* b)
{
    const uint8_t* p = b ? CBDirtyTracker::Get().GetShadowData(b) : nullptr;
    const uint16_t n = p ? static_cast<uint16_t>(CBDirtyTracker::Get().GetShadowSize(b)) : 0;
    fwrite(&n, 2, 1, s.drawCb);
    if (n) fwrite(p, 1, n, s.drawCb);
    s.drawCbBytes += 2 + n;
    if (b) b->Release();
}
} // namespace

void FrameLedgerHooks::OnTargets(ID3D11DeviceContext* ctx, UINT n, ID3D11RenderTargetView* const* rtvs,
                                 ID3D11DepthStencilView* dsv)
{
    State& s = S();
    CloseInterval(ctx);
    Interval iv;
    iv.seq = static_cast<int>(s.intervals.size());
    iv.phaseOpen = RenderPhaseDetector::Get().GetCurrentPhase();
    for (UINT k = 0; k < n && k < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++k) {
        iv.rt[k] = RefOf(rtvs ? rtvs[k] : nullptr);
        if (iv.rt[k].tex >= 0) iv.numRt = k + 1;
    }
    iv.ds = RefOf(dsv);
    s.intervals.push_back(std::move(iv));
}

void FrameLedgerHooks::OnShaders(uint64_t psHash, uint64_t vsHash, ID3D11PixelShader* ps)
{
    State& s = S();
    s.curPs = psHash;
    s.curVs = vsHash;
    if (psHash && ps) s.psSeen.emplace(psHash, ps);
}

void FrameLedgerHooks::OnDraw(ID3D11DeviceContext* ctx, const char* kind, UINT count, UINT instances)
{
    State& s = S();
    if (s.intervals.empty()) return;
    Interval& iv = s.intervals.back();
    if (iv.draws == 0) {
        iv.firstDraw = s.drawCount;
        UINT nvp = 1;
        D3D11_VIEWPORT vp{};
        ctx->RSGetViewports(&nvp, &vp);
        iv.vpW = vp.Width;
        iv.vpH = vp.Height;
    }
    ++iv.draws;
    bool known = false;
    for (uint64_t h : iv.ps) known |= h == s.curPs;
    if (!known) iv.ps.push_back(s.curPs);

    ID3D11BlendState* bs = nullptr;
    FLOAT factor[4];
    UINT mask = 0, stencilRef = 0;
    ctx->OMGetBlendState(&bs, factor, &mask);
    D3D11_RENDER_TARGET_BLEND_DESC b{};
    b.RenderTargetWriteMask = 0xF;
    if (bs) { D3D11_BLEND_DESC d{}; bs->GetDesc(&d); b = d.RenderTarget[0]; bs->Release(); }
    ID3D11DepthStencilState* dss = nullptr;
    ctx->OMGetDepthStencilState(&dss, &stencilRef);
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_LESS;
    if (dss) { dss->GetDesc(&dd); dss->Release(); }
    iv.blendDraws += b.BlendEnable ? 1 : 0;
    iv.depthWriteDraws += (dd.DepthEnable && dd.DepthWriteMask) ? 1 : 0;
    if (s.draws)
        fprintf(s.draws, "%u,%d,%s,%u,%u,%016llx,%016llx,%d,%d,%d,%d,%d,%d,%u,%d,%d,%d,%d,%u\n",
                s.drawCount, iv.seq, kind, count, instances, (unsigned long long)s.curPs,
                (unsigned long long)s.curVs, b.BlendEnable, b.SrcBlend, b.DestBlend, b.BlendOp, b.SrcBlendAlpha,
                b.DestBlendAlpha, b.RenderTargetWriteMask, dd.DepthEnable, dd.DepthWriteMask, dd.DepthFunc,
                dd.StencilEnable, stencilRef);
    // Per-draw constants: vertex slots 0,1,2,12 then pixel slots 0,1,2,12.
    if (s.drawCb && s.drawCbBytes < (512ull << 20)) {
        static const UINT kSlots[4] = {0, 1, 2, 12};
        fwrite(&s.drawCount, 4, 1, s.drawCb);
        s.drawCbBytes += 4;
        for (int stage = 0; stage < 2; ++stage)
            for (UINT slot : kSlots) {
                ID3D11Buffer* cb = nullptr;
                if (stage == 0) ctx->VSGetConstantBuffers(slot, 1, &cb); else ctx->PSGetConstantBuffers(slot, 1, &cb);
                AppendCb(s, cb);
            }
    }
    ++s.drawCount;
}

void FrameLedgerHooks::OnClearTarget(ID3D11RenderTargetView* rtv, const float c[4])
{
    State& s = S();
    const ViewRef v = RefOf(rtv);
    if (v.tex < 0) return;
    s.tex[v.tex].written = true;
    char buf[160];
    sprintf_s(buf, "{\"clear\":\"target\",\"tex\":%d,\"rgba\":[%g,%g,%g,%g]}", v.tex, c[0], c[1], c[2], c[3]);
    s.events.push_back({static_cast<int>(s.intervals.size()) - 1, buf});
}

void FrameLedgerHooks::OnClearDepth(ID3D11DepthStencilView* dsv, UINT flags, float depth, UINT8 stencil)
{
    State& s = S();
    const ViewRef v = RefOf(dsv);
    if (v.tex < 0) return;
    s.tex[v.tex].written = true;
    char buf[160];
    sprintf_s(buf, "{\"clear\":\"depth\",\"tex\":%d,\"slice\":%u,\"flags\":%u,\"depth\":%g,\"stencil\":%u}",
              v.tex, v.slice, flags, depth, stencil);
    s.events.push_back({static_cast<int>(s.intervals.size()) - 1, buf});
}

void FrameLedgerHooks::OnCopy(ID3D11DeviceContext*, ID3D11Resource* dst, ID3D11Resource* src, bool whole)
{
    State& s = S();
    const int d = Track(dst), r = Track(src);
    if (d < 0) return;
    s.tex[d].written = true;
    ++s.tex[d].copiesIn;
    if (r >= 0) ++s.tex[r].copiesOut;
    char buf[96];
    sprintf_s(buf, "{\"copy\":%d,\"from\":%d,\"whole\":%d}", d, r, whole ? 1 : 0);
    s.events.push_back({static_cast<int>(s.intervals.size()) - 1, buf});
}

void FrameLedgerHooks::OnDispatch(ID3D11DeviceContext* ctx, UINT x, UINT y, UINT z)
{
    State& s = S();
    ID3D11UnorderedAccessView* uavs[8]{};
    ctx->CSGetUnorderedAccessViews(0, 8, uavs);
    std::string list;
    for (auto* u : uavs) {
        if (!u) continue;
        const ViewRef v = RefOf(u);
        u->Release();
        if (v.tex < 0) continue;
        s.tex[v.tex].written = true;
        ++s.tex[v.tex].uavWrites;
        list += (list.empty() ? "" : ",") + std::to_string(v.tex);
    }
    char buf[160];
    sprintf_s(buf, "{\"dispatch\":[%u,%u,%u],\"uav\":[%s]}", x, y, z, list.c_str());
    s.events.push_back({static_cast<int>(s.intervals.size()) - 1, buf});
}

} // namespace SB::Proxy
