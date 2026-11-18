// Scene 13: the shape of a deferred game frame. A depth prepass with stencil writes
// into an R24G8 texture, a CopyResource of that depth texture, an opaque pass into
// four targets at once (RGBA16F, RG16F, RGBA8, RG8) at depth EQUAL, a stencil-tested
// pass into R11G11B10F that reads depth as a texture under a read-only DSV, a late
// draw into R10G10B10A2 against the depth copy, and a composite that shows each
// target in its own tile (4 columns, 2 rows):
//   0 HDR colour  1 motion  2 normals  3 mask  /  4 stencil-lit  5 late  6 depth  7 mix
#include "gbuffer_frame.h"

namespace txtest {
namespace gbuffer {
namespace {
void DepthPrepass(SceneCtx& c, const Res& r)
{
    c.ctx->ClearDepthStencilView(r.dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    c.ctx->OMSetRenderTargets(0, nullptr, r.dsv.Get());
    c.ctx->PSSetShader(nullptr, nullptr, 0);
    c.ctx->OMSetDepthStencilState(r.dsPrepass.Get(), 3);
    c.ctx->Draw(6, kFirstB);
    c.ctx->OMSetDepthStencilState(r.dsPrepass.Get(), 1);
    c.ctx->Draw(kOthersCount, kFirstOthers);
    // The copy is taken with the source unbound.
    c.ctx->OMSetRenderTargets(0, nullptr, nullptr);
    c.ctx->CopyResource(r.depthCopy.Get(), r.depthTex.Get());
}

void OpaquePass(SceneCtx& c, const Res& r)
{
    const float zero[4] = {0, 0, 0, 0}, nrm[4] = {0.4f, 0.4f, 0, 0}, msk[4] = {0, 0.6f, 0, 0};
    c.ctx->ClearRenderTargetView(r.hdr.rtv.Get(), zero);
    c.ctx->ClearRenderTargetView(r.motion.rtv.Get(), zero);
    c.ctx->ClearRenderTargetView(r.normal.rtv.Get(), nrm);
    c.ctx->ClearRenderTargetView(r.mask.rtv.Get(), msk);
    ID3D11RenderTargetView* rtvs[4] = {r.hdr.rtv.Get(), r.motion.rtv.Get(), r.normal.rtv.Get(),
                                       r.mask.rtv.Get()};
    c.ctx->OMSetRenderTargets(4, rtvs, r.dsv.Get());
    c.ctx->OMSetDepthStencilState(r.dsOpaque.Get(), 0);
    c.ctx->OMSetBlendState(r.blendMrt.Get(), nullptr, 0xFFFFFFFFu);
    c.ctx->PSSetShader(r.psOpaque.Get(), nullptr, 0);
    c.ctx->PSSetConstantBuffers(0, 1, r.cb.GetAddressOf());
    c.ctx->Draw(kOpaqueCount, 0);
    c.ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
}

// Full screen where stencil equals 3; depth is the read-only DSV and a texture at once.
void StencilLitPass(SceneCtx& c, const Res& r)
{
    const float base[4] = {0.125f, 0.125f, 0.25f, 1};
    c.ctx->ClearRenderTargetView(r.lit.rtv.Get(), base);
    c.ctx->OMSetRenderTargets(1, r.lit.rtv.GetAddressOf(), r.dsvReadOnly.Get());
    c.ctx->OMSetDepthStencilState(r.dsLit.Get(), 3);
    c.ctx->IASetInputLayout(nullptr);
    c.ctx->VSSetShader(r.fsVs.Get(), nullptr, 0);
    c.ctx->PSSetShader(r.psLit.Get(), nullptr, 0);
    c.ctx->PSSetShaderResources(6, 1, r.depthSrv.GetAddressOf());
    c.ctx->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    c.ctx->PSSetShaderResources(6, 1, &none);
}

void LatePass(SceneCtx& c, const Res& r)
{
    const float zero[4] = {0, 0, 0, 0};
    c.ctx->ClearRenderTargetView(r.late.rtv.Get(), zero);
    c.ctx->OMSetRenderTargets(1, r.late.rtv.GetAddressOf(), r.dsvCopy.Get());
    c.ctx->OMSetDepthStencilState(r.dsLate.Get(), 0);
    BindColorPipe(c.ctx, r.pipe);
    c.ctx->Draw(6, kFirstLate);
}

void Composite(SceneCtx& c, const Res& r)
{
    c.ctx->ClearRenderTargetView(r.out.rtv.Get(), kClear);
    c.ctx->OMSetRenderTargets(1, r.out.rtv.GetAddressOf(), nullptr);
    c.ctx->OMSetDepthStencilState(nullptr, 0);
    c.ctx->IASetInputLayout(nullptr);
    c.ctx->VSSetShader(r.fsVs.Get(), nullptr, 0);
    c.ctx->PSSetShader(r.psComposite.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srvs[7] = {r.hdr.srv.Get(), r.motion.srv.Get(), r.normal.srv.Get(),
                                         r.mask.srv.Get(), r.lit.srv.Get(), r.late.srv.Get(),
                                         r.depthSrv.Get()};
    c.ctx->PSSetShaderResources(0, 7, srvs);
    c.ctx->PSSetSamplers(0, 1, r.point.GetAddressOf());
    c.ctx->Draw(3, 0);
}

// The output pixel of `tile` that shows source texel (sx, sy), given in 256ths.
bool TileNear(const Image& o, int tile, int sx, int sy, int r, int g, int b, int tol = 1)
{
    const float tw = static_cast<float>(o.width) / 4.0f, th = static_cast<float>(o.height) / 2.0f;
    const float fx = static_cast<float>(tile % 4) + (static_cast<float>(sx) + 0.5f) / 256.0f;
    const float fy = static_cast<float>(tile / 4) + (static_cast<float>(sy) + 0.5f) / 256.0f;
    const uint32_t x = static_cast<uint32_t>(fx * tw), y = static_cast<uint32_t>(fy * th);
    if (PxNear(o, x, y, r, g, b, tol)) return true;
    const uint8_t* p = Px(o, x, y);
    fprintf(stderr, "[%s] tile %d texel (%d,%d): got %d,%d,%d want %d,%d,%d\n", kName, tile, sx, sy,
            p[0], p[1], p[2], r, g, b);
    return false;
}

bool CheckGbuffer(const Image& o)
{
    // Texels: A (50,49), B (162,161), C (50,201), background (10,251).
    TX_REQ(kName, TileNear(o, 0, 50, 49, 255, 51, 51));      // HDR colour, halved
    TX_REQ(kName, TileNear(o, 0, 162, 161, 51, 204, 102));
    TX_REQ(kName, TileNear(o, 0, 10, 251, 0, 0, 0));
    TX_REQ(kName, TileNear(o, 1, 50, 49, 71, 133, 0));       // motion: signed values
    TX_REQ(kName, TileNear(o, 1, 162, 161, 116, 89, 0));
    TX_REQ(kName, TileNear(o, 1, 10, 251, 102, 102, 0));
    TX_REQ(kName, TileNear(o, 2, 50, 49, 0, 204, 204));      // normals
    TX_REQ(kName, TileNear(o, 2, 162, 161, 204, 51, 153));
    TX_REQ(kName, TileNear(o, 2, 10, 251, 102, 102, 0));
    TX_REQ(kName, TileNear(o, 3, 50, 49, 255, 153, 0));      // mask: red written, green kept
    TX_REQ(kName, TileNear(o, 3, 10, 251, 0, 153, 0));
    return true;
}

bool CheckDepthUsers(const Image& o)
{
    TX_REQ(kName, TileNear(o, 4, 162, 161, 64, 159, 64));    // stencil 3: tinted by depth 0.25
    TX_REQ(kName, TileNear(o, 4, 50, 49, 16, 16, 32));       // stencil 1: untouched
    TX_REQ(kName, TileNear(o, 4, 10, 251, 16, 16, 32));      // stencil 0: untouched
    TX_REQ(kName, TileNear(o, 5, 234, 49, 51, 153, 204));    // late object over the background
    TX_REQ(kName, TileNear(o, 5, 134, 49, 51, 153, 204));    // in front of A in the depth copy
    TX_REQ(kName, TileNear(o, 5, 162, 113, 0, 0, 0));        // behind B in the depth copy
    TX_REQ(kName, TileNear(o, 5, 10, 251, 0, 0, 0));
    TX_REQ(kName, TileNear(o, 6, 50, 49, 153, 153, 153));    // depth as a texture
    TX_REQ(kName, TileNear(o, 6, 162, 161, 64, 64, 64));
    TX_REQ(kName, TileNear(o, 6, 50, 201, 204, 204, 204));
    TX_REQ(kName, TileNear(o, 6, 10, 251, 255, 255, 255));
    TX_REQ(kName, TileNear(o, 7, 162, 161, 57, 182, 83, 2)); // mix of three targets
    TX_REQ(kName, TileNear(o, 7, 234, 49, 34, 85, 118, 2));
    return true;
}
}  // namespace
}  // namespace gbuffer

bool RunGbufferFrame(SceneCtx& c, Image& out)
{
    using namespace gbuffer;
    c.ctx->ClearState();
    Res r;
    if (!Setup(c, r)) return false;
    SetViewport(c.ctx, static_cast<float>(c.width), static_cast<float>(c.height));
    c.ctx->RSSetState(r.rs.Get());
    BindColorPipe(c.ctx, r.pipe);
    BindVB(c.ctx, r.vb.Get(), sizeof(VtxPC));

    DepthPrepass(c, r);
    OpaquePass(c, r);
    StencilLitPass(c, r);
    LatePass(c, r);
    Composite(c, r);

    TX_REQ(kName, Readback(c, r.out.tex.Get(), out));
    c.ctx->ClearState();
    return CheckGbuffer(out) && CheckDepthUsers(out);
}
}  // namespace txtest
