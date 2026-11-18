// Scene 1: one triangle with per-vertex colours on a non-black clear.
#include "../scene_util.h"

namespace txtest {
static const char* kName = "triangle";

bool RunTriangle(SceneCtx& c, Image& out)
{
    c.ctx->ClearState();
    Target rt;
    ColorPipe pipe;
    TX_REQ(kName, MakeTarget(c.dev, c.width, c.height, DXGI_FORMAT_R8G8B8A8_UNORM, rt));
    TX_REQ(kName, MakeColorPipe(c.dev, pipe));
    const VtxPC v[3] = {
        {0.0f, 0.8f, 0.5f, 1, 0, 0, 1},
        {0.8f, -0.8f, 0.5f, 0, 1, 0, 1},
        {-0.8f, -0.8f, 0.5f, 0, 0, 1, 1},
    };
    auto vb = MakeBuffer(c.dev, sizeof(v), D3D11_BIND_VERTEX_BUFFER, v);
    auto rs = MakeRaster(c.dev, D3D11_CULL_BACK);
    TX_REQ(kName, vb && rs);

    c.ctx->ClearRenderTargetView(rt.rtv.Get(), kClear);
    c.ctx->OMSetRenderTargets(1, rt.rtv.GetAddressOf(), nullptr);
    SetViewport(c.ctx, static_cast<float>(c.width), static_cast<float>(c.height));
    c.ctx->RSSetState(rs.Get());
    BindColorPipe(c.ctx, pipe);
    BindVB(c.ctx, vb.Get(), sizeof(VtxPC));
    c.ctx->Draw(3, 0);

    TX_REQ(kName, Readback(c, rt.tex.Get(), out));
    c.ctx->ClearState();

    const uint8_t* mid = Px(out, c.width / 2, c.height / 2);
    TX_REQ(kName, IsClear(out, 2, 2) && IsClear(out, c.width - 3, 2));
    TX_REQ(kName, !IsClear(out, c.width / 2, c.height / 2));
    TX_REQ(kName, mid[0] > 40 && mid[1] > 40 && mid[2] > 40 && mid[3] == 255);
    // Near the bottom-left vertex the colour is mostly blue.
    const uint8_t* bl = Px(out, c.width * 14 / 100, c.height * 88 / 100);
    TX_REQ(kName, bl[2] > 200 && bl[0] < 40);
    return true;
}
}  // namespace txtest
