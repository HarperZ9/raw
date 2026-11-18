// Scene 12: queries. An occluder is drawn with depth, then three OCCLUSION queries
// (hidden, visible, half hidden), one OCCLUSION_PREDICATE, and a TIMESTAMP_DISJOINT
// bracket with two TIMESTAMP queries. Query draws write no colour. Indicator quads
// along the bottom are green where the result equals the expected value, red otherwise.
// values: [hidden samples, visible samples, half-hidden samples, predicate].
#include "../scene_util.h"

namespace txtest {
namespace {
const char* kName = "occlusion_query";

struct Rect { uint32_t x0, y0, x1, y1; float z; };   // in 256ths of the target
const Rect kOccluder = {64, 64, 192, 192, 0.5f};
const Rect kHidden = {80, 80, 120, 120, 0.8f};
const Rect kVisible = {8, 8, 40, 28, 0.3f};
const Rect kHalf = {160, 100, 224, 140, 0.8f};       // left half is behind the occluder

struct Res {
    Target rt;
    Depth depth;
    ColorPipe pipe;
    ComPtr<ID3D11Buffer> vb;
    ComPtr<ID3D11RasterizerState> rs;
    ComPtr<ID3D11DepthStencilState> dsWrite, dsTest, dsOff;
    ComPtr<ID3D11BlendState> noColor;
    ComPtr<ID3D11Query> occ[3], pred, disjoint, ts[2];
};

struct Results {
    UINT64 samples[3] = {0, 0, 0};
    BOOL any = FALSE;
};

// Whole-pixel edges, so a rectangle covers exactly (x1-x0)*(y1-y0) pixel centres.
uint32_t PxX(const SceneCtx& c, uint32_t v) { return v * c.width / 256; }
uint32_t PxY(const SceneCtx& c, uint32_t v) { return v * c.height / 256; }

void PushRect(const SceneCtx& c, std::vector<VtxPC>& v, const Rect& r, Rgba col)
{
    PushQuadPx(v, static_cast<float>(c.width), static_cast<float>(c.height),
               static_cast<float>(PxX(c, r.x0)), static_cast<float>(PxY(c, r.y0)),
               static_cast<float>(PxX(c, r.x1)), static_cast<float>(PxY(c, r.y1)), r.z, col);
}

uint64_t Area(const SceneCtx& c, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    return static_cast<uint64_t>(PxX(c, x1) - PxX(c, x0)) * (PxY(c, y1) - PxY(c, y0));
}

bool MakeQueries(SceneCtx& c, Res& r)
{
    auto make = [&](D3D11_QUERY type, ComPtr<ID3D11Query>& q) {
        const D3D11_QUERY_DESC qd = {type, 0};
        return SUCCEEDED(c.dev->CreateQuery(&qd, &q));
    };
    for (auto& q : r.occ) TX_REQ(kName, make(D3D11_QUERY_OCCLUSION, q));
    for (auto& q : r.ts) TX_REQ(kName, make(D3D11_QUERY_TIMESTAMP, q));
    TX_REQ(kName, make(D3D11_QUERY_OCCLUSION_PREDICATE, r.pred));
    TX_REQ(kName, make(D3D11_QUERY_TIMESTAMP_DISJOINT, r.disjoint));
    return true;
}

bool Setup(SceneCtx& c, Res& r)
{
    TX_REQ(kName, MakeTarget(c.dev, c.width, c.height, DXGI_FORMAT_R8G8B8A8_UNORM, r.rt));
    TX_REQ(kName, MakeDepth(c.dev, c.width, c.height, r.depth));
    TX_REQ(kName, MakeColorPipe(c.dev, r.pipe));
    std::vector<VtxPC> v;
    PushRect(c, v, kOccluder, {0.5f, 0.5f, 0.5f, 1});
    for (const Rect* q : {&kHidden, &kVisible, &kHalf}) PushRect(c, v, *q, {1, 0, 1, 1});
    r.vb = MakeBuffer(c.dev, static_cast<UINT>(v.size() * sizeof(VtxPC)), D3D11_BIND_VERTEX_BUFFER, v.data());
    r.rs = MakeRaster(c.dev, D3D11_CULL_NONE);
    r.dsWrite = MakeDepthState(c.dev, true, true, D3D11_COMPARISON_LESS);
    r.dsTest = MakeDepthState(c.dev, true, false, D3D11_COMPARISON_LESS);
    r.dsOff = MakeDepthState(c.dev, false, false, D3D11_COMPARISON_ALWAYS);
    BlendSpec mask0;
    mask0.mask = 0;
    r.noColor = MakeBlend(c.dev, mask0);
    TX_REQ(kName, r.vb && r.rs && r.dsWrite && r.dsTest && r.dsOff && r.noColor);
    return MakeQueries(c, r);
}

bool RunQueries(SceneCtx& c, Res& r, Results& res)
{
    c.ctx->Begin(r.disjoint.Get());
    c.ctx->End(r.ts[0].Get());
    c.ctx->OMSetDepthStencilState(r.dsWrite.Get(), 0);
    c.ctx->Draw(6, 0);                                   // occluder, with colour
    c.ctx->OMSetDepthStencilState(r.dsTest.Get(), 0);
    c.ctx->OMSetBlendState(r.noColor.Get(), nullptr, 0xFFFFFFFFu);
    for (UINT i = 0; i < 3; ++i) {
        c.ctx->Begin(r.occ[i].Get());
        c.ctx->Draw(6, 6 + 6 * i);
        c.ctx->End(r.occ[i].Get());
    }
    c.ctx->Begin(r.pred.Get());
    c.ctx->Draw(6, 12);                                  // the visible quad again
    c.ctx->End(r.pred.Get());
    c.ctx->End(r.ts[1].Get());
    c.ctx->End(r.disjoint.Get());
    c.ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);

    for (UINT i = 0; i < 3; ++i)
        TX_REQ(kName, WaitQuery(c.ctx, r.occ[i].Get(), &res.samples[i], sizeof(UINT64)));
    TX_REQ(kName, WaitQuery(c.ctx, r.pred.Get(), &res.any, sizeof(BOOL)));
    UINT64 t[2] = {0, 0};
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
    TX_REQ(kName, WaitQuery(c.ctx, r.ts[0].Get(), &t[0], sizeof(UINT64)));
    TX_REQ(kName, WaitQuery(c.ctx, r.ts[1].Get(), &t[1], sizeof(UINT64)));
    TX_REQ(kName, WaitQuery(c.ctx, r.disjoint.Get(), &dj, sizeof(dj)));
    TX_REQ(kName, dj.Frequency != 0);
    TX_REQ(kName, dj.Disjoint || t[1] >= t[0]);
    return true;
}

// Indicator k sits at x 16+60k .. 56+60k, y 216 .. 240.
bool DrawIndicators(SceneCtx& c, const Res& r, const bool good[4])
{
    std::vector<VtxPC> v;
    for (uint32_t k = 0; k < 4; ++k) {
        const Rect q = {16 + 60 * k, 216, 56 + 60 * k, 240, 0.5f};
        PushRect(c, v, q, good[k] ? Rgba{0, 1, 0, 1} : Rgba{1, 0, 0, 1});
    }
    auto vb = MakeBuffer(c.dev, static_cast<UINT>(v.size() * sizeof(VtxPC)), D3D11_BIND_VERTEX_BUFFER, v.data());
    TX_REQ(kName, vb);
    c.ctx->OMSetDepthStencilState(r.dsOff.Get(), 0);
    BindVB(c.ctx, vb.Get(), sizeof(VtxPC));
    c.ctx->Draw(static_cast<UINT>(v.size()), 0);
    return true;
}

bool Check(const SceneCtx& c, const Image& out, const bool good[4])
{
    TX_REQ(kName, good[0]);   // hidden quad: 0 samples
    TX_REQ(kName, good[1]);   // visible quad: its exact pixel count
    TX_REQ(kName, good[2]);   // half-hidden quad: the visible half's pixel count
    TX_REQ(kName, good[3]);   // predicate: TRUE
    TX_REQ(kName, PxNear(out, c.width / 2, c.height / 2, 128, 128, 128, 1));       // occluder
    TX_REQ(kName, IsClear(out, PxX(c, 24), PxY(c, 18)));                           // no colour from query draws
    TX_REQ(kName, IsClear(out, PxX(c, 210), PxY(c, 120)));
    for (uint32_t k = 0; k < 4; ++k)
        TX_REQ(kName, PxNear(out, PxX(c, 36 + 60 * k), PxY(c, 228), 0, 255, 0, 0));
    return true;
}
}  // namespace

bool RunOcclusionQuery(SceneCtx& c, Image& out)
{
    c.ctx->ClearState();
    Res r;
    if (!Setup(c, r)) return false;
    c.ctx->ClearRenderTargetView(r.rt.rtv.Get(), kClear);
    c.ctx->ClearDepthStencilView(r.depth.dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    c.ctx->OMSetRenderTargets(1, r.rt.rtv.GetAddressOf(), r.depth.dsv.Get());
    SetViewport(c.ctx, static_cast<float>(c.width), static_cast<float>(c.height));
    c.ctx->RSSetState(r.rs.Get());
    BindColorPipe(c.ctx, r.pipe);
    BindVB(c.ctx, r.vb.Get(), sizeof(VtxPC));

    Results res;
    if (!RunQueries(c, r, res)) return false;
    for (UINT64 s : res.samples) c.values.push_back(s);
    c.values.push_back(res.any ? 1u : 0u);
    const bool good[4] = {
        res.samples[0] == 0,
        res.samples[1] == Area(c, kVisible.x0, kVisible.y0, kVisible.x1, kVisible.y1),
        res.samples[2] == Area(c, kOccluder.x1, kHalf.y0, kHalf.x1, kHalf.y1),
        res.any != FALSE,
    };
    if (!DrawIndicators(c, r, good)) return false;

    TX_REQ(kName, Readback(c, r.rt.tex.Get(), out));
    c.ctx->ClearState();
    if (!good[1] || !good[2])
        fprintf(stderr, "[%s] samples: hidden=%llu visible=%llu half=%llu\n", kName,
                res.samples[0], res.samples[1], res.samples[2]);
    return Check(c, out, good);
}
}  // namespace txtest
