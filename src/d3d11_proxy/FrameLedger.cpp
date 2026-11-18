// FrameLedger tracking: intervals, draws, clears, copies and dispatches of one
// armed frame. Files are written by FrameLedgerDump.cpp. Original work, MIT.
#include "FrameLedgerImpl.h"
#include "CBDirtyTracker.h"
#include "ProxyAPI.h"
#include "ProxyLog.h"
#include <direct.h>

namespace SB::Proxy
{
using namespace ledger;

namespace ledger
{
State& S()
{
    static State s;
    return s;
}

int Track(ID3D11Resource* res)
{
    if (!res) return -1;
    State& s = S();
    auto it = s.byRes.find(res);
    if (it != s.byRes.end()) return it->second;
    TexRec t;
    res->GetType(&t.dim);
    if (t.dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        D3D11_TEXTURE2D_DESC d{};
        static_cast<ID3D11Texture2D*>(res)->GetDesc(&d);
        t.w = d.Width; t.h = d.Height; t.array = d.ArraySize; t.mips = d.MipLevels;
        t.fmt = d.Format; t.samples = d.SampleDesc.Count; t.bind = d.BindFlags; t.misc = d.MiscFlags;
    } else if (t.dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D) {
        D3D11_TEXTURE3D_DESC d{};
        static_cast<ID3D11Texture3D*>(res)->GetDesc(&d);
        t.w = d.Width; t.h = d.Height; t.depth = d.Depth; t.mips = d.MipLevels;
        t.fmt = d.Format; t.bind = d.BindFlags; t.misc = d.MiscFlags;
    } else {
        return -1;
    }
    res->AddRef();
    t.res = res;
    t.id = static_cast<int>(s.tex.size());
    s.byRes[res] = t.id;
    s.tex.push_back(t);
    return t.id;
}

ViewRef RefOf(ID3D11View* view)
{
    ViewRef r;
    if (!view) return r;
    ID3D11Resource* res = nullptr;
    view->GetResource(&res);
    r.tex = Track(res);
    if (res) res->Release();
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    ID3D11UnorderedAccessView* uav = nullptr;
    if (SUCCEEDED(view->QueryInterface(&rtv)) && rtv) {
        D3D11_RENDER_TARGET_VIEW_DESC d{};
        rtv->GetDesc(&d);
        r.fmt = d.Format;
        if (d.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D) r.mip = d.Texture2D.MipSlice;
        if (d.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DARRAY) {
            r.mip = d.Texture2DArray.MipSlice; r.slice = d.Texture2DArray.FirstArraySlice;
            r.slices = d.Texture2DArray.ArraySize;
        }
        rtv->Release();
    } else if (SUCCEEDED(view->QueryInterface(&dsv)) && dsv) {
        D3D11_DEPTH_STENCIL_VIEW_DESC d{};
        dsv->GetDesc(&d);
        r.fmt = d.Format; r.flags = d.Flags;
        if (d.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2D) r.mip = d.Texture2D.MipSlice;
        if (d.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2DARRAY) {
            r.mip = d.Texture2DArray.MipSlice; r.slice = d.Texture2DArray.FirstArraySlice;
            r.slices = d.Texture2DArray.ArraySize;
        }
        dsv->Release();
    } else if (SUCCEEDED(view->QueryInterface(&uav)) && uav) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC d{};
        uav->GetDesc(&d);
        r.fmt = d.Format;
        uav->Release();
    }
    return r;
}
} // namespace ledger

namespace
{
void LoadConfig()
{
    State& s = S();
    s.loaded = true;
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string ini(path);
    ini = ini.substr(0, ini.rfind('\\') + 1) + "d3d11_proxy.ini";
    auto i = [&](const char* k, int d) { return GetPrivateProfileIntA("FrameLedger", k, d, ini.c_str()); };
    s.enabled = i("Enabled", 0) != 0;
    s.after = i("AfterGameplayFrames", 600);
    s.minDraws = i("MinDraws", 500);
    s.captures = i("Captures", 1);
    s.every = i("Every", 600);
    s.history = i("History", 1) != 0;
    char out[MAX_PATH]{};
    GetPrivateProfileStringA("FrameLedger", "OutDir", "", out, MAX_PATH, ini.c_str());
    s.outRoot = out;
    if (s.enabled && s.outRoot.empty()) {
        Log("[FrameLedger] Enabled=1 needs an absolute OutDir; ledger stays off");
        s.enabled = false;
    }
    Log("[FrameLedger] config: enabled=%d after=%d minDraws=%d captures=%d every=%d history=%d out=%s",
        s.enabled, s.after, s.minDraws, s.captures, s.every, s.history, s.outRoot.c_str());
}

bool FullSize(const TexRec& t)
{
    const State& s = S();
    return t.dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D && t.array == 1 && t.w == s.bbW && t.h == s.bbH;
}

void CloseInterval(ID3D11DeviceContext* ctx)
{
    State& s = S();
    if (s.intervals.empty()) return;
    Interval& iv = s.intervals.back();
    iv.phaseClose = RenderPhaseDetector::Get().GetCurrentPhase();
    if (iv.draws == 0) return;
    auto touch = [&](const ViewRef& v, bool depth) {
        if (v.tex < 0) return;
        TexRec& t = s.tex[v.tex];
        t.written = true;
        t.draws += iv.draws;
        ++(depth ? t.dsIntervals : t.rtIntervals);
        if (s.history && FullSize(t)) {
            char name[48];
            sprintf_s(name, "t%02d_i%03d", t.id, iv.seq);
            if (DumpTexture(ctx, v.tex, name)) iv.history.push_back(t.id);
        }
    };
    for (UINT k = 0; k < iv.numRt; ++k) touch(iv.rt[k], false);
    // A read-only depth view cannot change the depth, so it needs no copy.
    if (iv.ds.tex >= 0 && !(iv.ds.flags & D3D11_DSV_READ_ONLY_DEPTH)) touch(iv.ds, true);
}

void Arm(ID3D11DeviceContext* ctx)
{
    State& s = S();
    if (auto* pi = PG_GetProxyInterface(); pi && pi->swapChain) {
        DXGI_SWAP_CHAIN_DESC d{};
        pi->swapChain->GetDesc(&d);
        s.bbW = d.BufferDesc.Width;
        s.bbH = d.BufferDesc.Height;
    }
    char sub[16];
    sprintf_s(sub, "\\%03d", s.captured);
    s.dir = s.outRoot + sub;
    _mkdir(s.outRoot.c_str());
    _mkdir(s.dir.c_str());
    fopen_s(&s.draws, (s.dir + "\\draws.csv").c_str(), "w");
    fopen_s(&s.drawCb, (s.dir + "\\drawcb.bin").c_str(), "wb");
    if (s.draws)
        fprintf(s.draws, "draw,interval,kind,count,instances,ps,vs,blend,src,dst,op,srcA,dstA,mask,"
                         "depth,depthWrite,depthFunc,stencil,stencilRef\n");
    s.drawCount = 0;
    s.drawCbBytes = 0;
    g_frameLedgerArmed = true;
    Log("[FrameLedger] armed capture %d into %s (backbuffer %ux%u)", s.captured, s.dir.c_str(), s.bbW, s.bbH);
    // The frame starts with whatever is bound now.
    ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* dsv = nullptr;
    ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
    FrameLedgerHooks::OnTargets(ctx, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, dsv);
    for (auto* r : rtvs) if (r) r->Release();
    if (dsv) dsv->Release();
}

void Finish(ID3D11DeviceContext* ctx)
{
    State& s = S();
    CloseInterval(ctx);
    g_frameLedgerArmed = false;
    int dumped = 0;
    for (TexRec& t : s.tex) {
        if (!t.written) continue;
        char name[32];
        sprintf_s(name, "t%02d_final", t.id);
        dumped += DumpTexture(ctx, t.id, name, true) ? 1 : 0;
    }
    DumpShaders();
    WriteManifest();
    if (s.draws) fclose(s.draws);
    if (s.drawCb) fclose(s.drawCb);
    s.draws = s.drawCb = nullptr;
    Log("[FrameLedger] capture %d done: %zu textures tracked, %d dumped, %zu intervals, %u draws",
        s.captured, s.tex.size(), dumped, s.intervals.size(), s.drawCount);
    for (TexRec& t : s.tex) {
        if (t.staging) t.staging->Release();
        if (t.res) t.res->Release();
    }
    s.tex.clear(); s.byRes.clear(); s.intervals.clear(); s.events.clear(); s.psSeen.clear();
    ++s.captured;
}

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

void FrameLedgerHooks::OnPresent(ID3D11DeviceContext* ctx, uint32_t drawsThisFrame)
{
    State& s = S();
    if (!s.loaded) LoadConfig();
    if (g_frameLedgerArmed) Finish(ctx);
    if (!s.enabled || s.captured >= s.captures || !ctx) return;
    s.gameplay = drawsThisFrame > static_cast<uint32_t>(s.minDraws) ? s.gameplay + 1 : 0;
    if (s.gameplay == s.after + s.captured * s.every) Arm(ctx);
}

void FrameLedger_OnPresent()
{
    auto* pi = PG_GetProxyInterface();
    if (PG_IsSafeMode() || !pi) return;
    FrameLedgerHooks::OnPresent(pi->context, RenderPhaseDetector::Get().GetTotalDrawsThisFrame());
}

} // namespace SB::Proxy
