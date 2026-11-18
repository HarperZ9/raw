// Scene 10: thin rotated triangles into a 4x MSAA colour target with an MSAA depth
// buffer (MultisampleEnable on), then ResolveSubresource into the single-sample output.
#include "../scene_util.h"
#include <cmath>
#include <cstdlib>

namespace txtest {
namespace {
const char* kName = "msaa_resolve";
const int kTris = 12;
const float kAxis = 110.0f, kHalfBase = 10.0f, kProbe = 40.0f;   // in 256ths of the target
const int kPalette[3][3] = {{255, 51, 51}, {51, 255, 51}, {255, 255, 51}};

float Angle(int i) { return 0.1222f + 0.5235988f * static_cast<float>(i); }   // 7 deg + 30 deg steps

std::vector<VtxPC> MakeTris(float w, float h)
{
    std::vector<VtxPC> v;
    const float sx = w / 256.0f, sy = h / 256.0f, cx = 128.0f, cy = 128.0f;
    for (int i = 0; i < kTris; ++i) {
        const float dx = std::cos(Angle(i)), dy = std::sin(Angle(i));
        const float z = 0.1f + 0.05f * static_cast<float>(i);
        const int* k = kPalette[i % 3];
        const Rgba col = {static_cast<float>(k[0]) / 255.0f, static_cast<float>(k[1]) / 255.0f,
                          static_cast<float>(k[2]) / 255.0f, 1.0f};
        v.push_back(VertPx(w, h, (cx + dx * kAxis) * sx, (cy + dy * kAxis) * sy, z, col));
        v.push_back(VertPx(w, h, (cx - dy * kHalfBase) * sx, (cy + dx * kHalfBase) * sy, z, col));
        v.push_back(VertPx(w, h, (cx + dy * kHalfBase) * sx, (cy - dx * kHalfBase) * sy, z, col));
    }
    return v;
}

bool IsPalette(const uint8_t* p)
{
    for (const auto& k : kPalette)
        if (std::abs(p[0] - k[0]) <= 1 && std::abs(p[1] - k[1]) <= 1 && std::abs(p[2] - k[2]) <= 1)
            return true;
    return false;
}

bool Check(const SceneCtx& c, const Image& out)
{
    TX_REQ(kName, IsClear(out, 2, 2) && IsClear(out, c.width - 3, c.height - 3));
    const float sx = static_cast<float>(c.width) / 256.0f, sy = static_cast<float>(c.height) / 256.0f;
    for (int i = 0; i < kTris; ++i) {
        const uint32_t x = static_cast<uint32_t>((128.0f + std::cos(Angle(i)) * kProbe) * sx);
        const uint32_t y = static_cast<uint32_t>((128.0f + std::sin(Angle(i)) * kProbe) * sy);
        const int* k = kPalette[i % 3];
        TX_REQ(kName, PxNear(out, x, y, k[0], k[1], k[2]));
    }
    // Resolved edges: pixels that are neither the clear colour nor a triangle colour.
    uint32_t mixed = 0;
    for (uint32_t y = 0; y < out.height; ++y)
        for (uint32_t x = 0; x < out.width; ++x)
            if (!IsClear(out, x, y) && !IsPalette(Px(out, x, y))) ++mixed;
    if (mixed < 200) {
        fprintf(stderr, "[%s] only %u blended edge pixels; the resolve did not average samples\n",
                kName, mixed);
        return false;
    }
    return true;
}

struct Res {
    Target resolved;
    ColorPipe pipe;
    ComPtr<ID3D11Texture2D> msTex, msDepth;
    ComPtr<ID3D11RenderTargetView> msRtv;
    ComPtr<ID3D11DepthStencilView> msDsv;
    ComPtr<ID3D11Buffer> vb;
    ComPtr<ID3D11RasterizerState> rs;
    ComPtr<ID3D11DepthStencilState> ds;
    UINT vertCount = 0;
};

bool Setup(SceneCtx& c, Res& r, DXGI_FORMAT fmt)
{
    UINT levels = 0;
    TX_HR(kName, c.dev->CheckMultisampleQualityLevels(fmt, 4, &levels));
    TX_REQ(kName, levels > 0);
    TX_REQ(kName, MakeTarget(c.dev, c.width, c.height, fmt, r.resolved));
    TX_REQ(kName, MakeColorPipe(c.dev, r.pipe));
    TexDesc cd;
    cd.w = c.width; cd.h = c.height; cd.fmt = fmt; cd.samples = 4; cd.bind = D3D11_BIND_RENDER_TARGET;
    TexDesc dd = cd;
    dd.fmt = DXGI_FORMAT_D24_UNORM_S8_UINT; dd.bind = D3D11_BIND_DEPTH_STENCIL;
    r.msTex = MakeTex(c.dev, cd);
    r.msDepth = MakeTex(c.dev, dd);
    r.msRtv = MakeRTV(c.dev, r.msTex.Get());
    r.msDsv = MakeDSV(c.dev, r.msDepth.Get());
    const std::vector<VtxPC> v = MakeTris(static_cast<float>(c.width), static_cast<float>(c.height));
    r.vertCount = static_cast<UINT>(v.size());
    r.vb = MakeBuffer(c.dev, r.vertCount * sizeof(VtxPC), D3D11_BIND_VERTEX_BUFFER, v.data());
    r.rs = MakeRaster(c.dev, D3D11_CULL_NONE, 0, 0.0f, false, true);
    r.ds = MakeDepthState(c.dev, true, true, D3D11_COMPARISON_LESS);
    TX_REQ(kName, r.msTex && r.msDepth && r.msRtv && r.msDsv && r.vb && r.rs && r.ds);
    return true;
}
}  // namespace

bool RunMsaaResolve(SceneCtx& c, Image& out)
{
    c.ctx->ClearState();
    const DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    Res r;
    if (!Setup(c, r, fmt)) return false;
    c.ctx->ClearRenderTargetView(r.msRtv.Get(), kClear);
    c.ctx->ClearDepthStencilView(r.msDsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    c.ctx->OMSetRenderTargets(1, r.msRtv.GetAddressOf(), r.msDsv.Get());
    c.ctx->OMSetDepthStencilState(r.ds.Get(), 0);
    SetViewport(c.ctx, static_cast<float>(c.width), static_cast<float>(c.height));
    c.ctx->RSSetState(r.rs.Get());
    BindColorPipe(c.ctx, r.pipe);
    BindVB(c.ctx, r.vb.Get(), sizeof(VtxPC));
    c.ctx->Draw(r.vertCount, 0);
    c.ctx->OMSetRenderTargets(0, nullptr, nullptr);
    c.ctx->ResolveSubresource(r.resolved.tex.Get(), 0, r.msTex.Get(), 0, fmt);

    TX_REQ(kName, Readback(c, r.resolved.tex.Get(), out));
    c.ctx->ClearState();
    return Check(c, out);
}
}  // namespace txtest
