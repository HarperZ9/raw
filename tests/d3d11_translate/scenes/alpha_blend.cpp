// Scene 4: blending. Opaque backgrounds, six SRC_ALPHA/INV_SRC_ALPHA quads, one
// additive, one multiplicative, one driven by the blend factor, one with a
// RED|BLUE write mask. Alpha factors differ from the colour factors.
#include "../scene_util.h"

namespace txtest {
namespace {
const char* kName = "alpha_blend";

struct Step {
    ComPtr<ID3D11BlendState> blend;
    UINT start = 0, count = 0;
    float factor[4] = {1, 1, 1, 1};
};

struct Builder {
    ID3D11Device* dev;
    float w, h;
    std::vector<VtxPC> verts;
    std::vector<Step> steps;
    bool ok = true;

    void Begin(const BlendSpec& spec)
    {
        Step s;
        s.blend = MakeBlend(dev, spec);
        s.start = static_cast<UINT>(verts.size());
        ok = ok && s.blend;
        steps.push_back(s);
    }
    // Rectangle in 256ths of the target.
    void Quad(float x0, float y0, float x1, float y1, Rgba c)
    {
        const float sx = w / 256.0f, sy = h / 256.0f;
        PushQuadPx(verts, w, h, x0 * sx, y0 * sy, x1 * sx, y1 * sy, 0.5f, c);
        steps.back().count += 6;
    }
};

void BuildSteps(Builder& b)
{
    const D3D11_BLEND one = D3D11_BLEND_ONE, zero = D3D11_BLEND_ZERO;
    const UINT8 all = D3D11_COLOR_WRITE_ENABLE_ALL;
    b.Begin({false, one, zero, one, zero, all});
    b.Quad(0, 16, 128, 240, {0, 0, 1, 1});
    b.Quad(128, 16, 256, 240, {0.2f, 0.6f, 0.2f, 1});

    b.Begin({true, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, one, D3D11_BLEND_INV_SRC_ALPHA, all});
    const Rgba tint[6] = {{1, 0, 0, 0.25f}, {1, 1, 0, 0.4f},    {0, 1, 1, 0.5f},
                          {1, 0, 1, 0.6f},  {1, 1, 1, 0.35f},   {0, 0, 0, 0.7f}};
    for (int i = 0; i < 6; ++i) {
        const float x = 16.0f + 30.0f * static_cast<float>(i), y = 40.0f + 12.0f * static_cast<float>(i);
        b.Quad(x, y, x + 70, y + 60, tint[i]);
    }

    b.Begin({true, one, one, zero, one, all});                       // additive
    b.Quad(150, 168, 236, 200, {0.5f, 0.25f, 0, 1});

    b.Begin({true, D3D11_BLEND_DEST_COLOR, zero, one, zero, all});   // multiplicative
    b.Quad(20, 168, 100, 224, {0.5f, 1, 0.5f, 1});

    b.Begin({true, D3D11_BLEND_BLEND_FACTOR, D3D11_BLEND_INV_BLEND_FACTOR, D3D11_BLEND_BLEND_FACTOR, zero, all});
    b.steps.back().factor[0] = 0.25f;
    b.steps.back().factor[1] = 0.5f;
    b.steps.back().factor[2] = 0.75f;
    b.Quad(108, 176, 144, 232, {1, 1, 0, 1});

    b.Begin({false, one, zero, one, zero, D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_BLUE});
    b.Quad(170, 208, 240, 236, {1, 1, 1, 1});
}

bool Check(const SceneCtx& c, const Image& out)
{
    auto at = [&](uint32_t x, uint32_t y, int r, int g, int b) {
        return PxNear(out, x * c.width / 256, y * c.height / 256, r, g, b, 1);
    };
    TX_REQ(kName, IsClear(out, 2, 2));
    TX_REQ(kName, at(4, 100, 0, 0, 255) && at(250, 100, 51, 153, 51));   // backgrounds
    // src (1,0,0,a=0.25) over blue: 0.25*src + 0.75*dst. Swapped factors give (191,0,64).
    TX_REQ(kName, at(24, 44, 64, 0, 191));
    TX_REQ(kName, at(200, 184, 178, 217, 51) || at(200, 184, 179, 217, 51));   // dst + src
    TX_REQ(kName, at(60, 200, 0, 0, 128));                                     // src * dst
    TX_REQ(kName, at(116, 200, 64, 128, 64));                                  // blend factor
    TX_REQ(kName, at(200, 220, 255, 153, 255));                                // green masked off
    return true;
}
}  // namespace

bool RunAlphaBlend(SceneCtx& c, Image& out)
{
    c.ctx->ClearState();
    Target rt;
    ColorPipe pipe;
    TX_REQ(kName, MakeTarget(c.dev, c.width, c.height, DXGI_FORMAT_R8G8B8A8_UNORM, rt));
    TX_REQ(kName, MakeColorPipe(c.dev, pipe));
    Builder b{c.dev, static_cast<float>(c.width), static_cast<float>(c.height)};
    BuildSteps(b);
    auto vb = MakeBuffer(c.dev, static_cast<UINT>(b.verts.size() * sizeof(VtxPC)), D3D11_BIND_VERTEX_BUFFER, b.verts.data());
    auto rs = MakeRaster(c.dev, D3D11_CULL_NONE);
    TX_REQ(kName, b.ok && vb && rs);

    c.ctx->ClearRenderTargetView(rt.rtv.Get(), kClear);
    c.ctx->OMSetRenderTargets(1, rt.rtv.GetAddressOf(), nullptr);
    SetViewport(c.ctx, b.w, b.h);
    c.ctx->RSSetState(rs.Get());
    BindColorPipe(c.ctx, pipe);
    BindVB(c.ctx, vb.Get(), sizeof(VtxPC));
    for (const Step& s : b.steps) {
        c.ctx->OMSetBlendState(s.blend.Get(), s.factor, 0xFFFFFFFFu);
        c.ctx->Draw(s.count, s.start);
    }

    TX_REQ(kName, Readback(c, rt.tex.Get(), out));
    c.ctx->ClearState();
    return Check(c, out);
}
}  // namespace txtest
