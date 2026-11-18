// Scene 9: a dynamic vertex buffer. Eight Map(WRITE_DISCARD)+Draw rounds, then
// Map(WRITE_NO_OVERWRITE) appends at growing offsets, each drawn with a start-vertex
// offset; then a dynamic 16-bit index buffer rewritten with DISCARD between two
// DrawIndexed calls that use a non-zero base vertex.
#include "../scene_util.h"
#include <cstring>

namespace txtest {
namespace {
const char* kName = "dynamic_vb";
const UINT kCapacity = 128;      // vertices
const UINT kIndexedBase = 64;    // first vertex of the indexed quads

const Rgba kPalette[8] = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {1, 1, 0, 1},
                          {1, 0, 1, 1}, {0, 1, 1, 1}, {1, 1, 1, 1}, {0, 0, 0, 1}};

struct Res {
    Target rt;
    ColorPipe pipe;
    ComPtr<ID3D11Buffer> vb, ib;
    ComPtr<ID3D11RasterizerState> rs;
    float w = 0, h = 0, sx = 1, sy = 1;
};

bool Write(SceneCtx& c, ID3D11Buffer* buf, D3D11_MAP mode, size_t offset, const void* data,
           size_t bytes)
{
    D3D11_MAPPED_SUBRESOURCE m{};
    TX_HR(kName, c.ctx->Map(buf, 0, mode, 0, &m));
    memcpy(static_cast<uint8_t*>(m.pData) + offset, data, bytes);
    c.ctx->Unmap(buf, 0);
    return true;
}

// Rectangle in 256ths of the target, as 6 vertices.
std::vector<VtxPC> Quad(const Res& r, float x0, float y0, float x1, float y1, Rgba col)
{
    std::vector<VtxPC> v;
    PushQuadPx(v, r.w, r.h, x0 * r.sx, y0 * r.sy, x1 * r.sx, y1 * r.sy, 0.5f, col);
    return v;
}

// Top band: the buffer is discarded and refilled from offset 0 for every quad.
bool DiscardRounds(SceneCtx& c, const Res& r)
{
    for (int i = 0; i < 8; ++i) {
        const float x = 8.0f + 30.0f * static_cast<float>(i);
        const float y1 = 40.0f + 4.0f * static_cast<float>(i);   // different geometry each time
        const auto v = Quad(r, x, 16, x + 24, y1, kPalette[i]);
        if (!Write(c, r.vb.Get(), D3D11_MAP_WRITE_DISCARD, 0, v.data(), v.size() * sizeof(VtxPC)))
            return false;
        c.ctx->Draw(6, 0);
    }
    return true;
}

// Middle band: appends after the 6 vertices the last discard left in use.
bool AppendRounds(SceneCtx& c, const Res& r)
{
    for (UINT j = 0; j < 6; ++j) {
        const float x = 16.0f + 38.0f * static_cast<float>(j);
        const auto v = Quad(r, x, 96, x + 30, 136, kPalette[(j + 3) % 8]);
        const UINT first = 6 + 6 * j;
        if (!Write(c, r.vb.Get(), D3D11_MAP_WRITE_NO_OVERWRITE, first * sizeof(VtxPC), v.data(),
                   v.size() * sizeof(VtxPC)))
            return false;
        c.ctx->Draw(6, first);
    }
    return true;
}

// Bottom band: two quads as 4 corners each (tl, tr, br, bl) at vertices 64..71.
bool IndexedRounds(SceneCtx& c, const Res& r)
{
    const float z = 0.5f;
    auto corners = [&](float x0, float y0, float x1, float y1, Rgba col, VtxPC* o) {
        o[0] = VertPx(r.w, r.h, x0 * r.sx, y0 * r.sy, z, col);
        o[1] = VertPx(r.w, r.h, x1 * r.sx, y0 * r.sy, z, col);
        o[2] = VertPx(r.w, r.h, x1 * r.sx, y1 * r.sy, z, col);
        o[3] = VertPx(r.w, r.h, x0 * r.sx, y1 * r.sy, z, col);
    };
    VtxPC v[8];
    corners(24, 176, 104, 232, kPalette[1], v);
    corners(152, 176, 232, 232, kPalette[4], v + 4);
    if (!Write(c, r.vb.Get(), D3D11_MAP_WRITE_NO_OVERWRITE, kIndexedBase * sizeof(VtxPC), v, sizeof(v)))
        return false;
    c.ctx->IASetIndexBuffer(r.ib.Get(), DXGI_FORMAT_R16_UINT, 0);
    const uint16_t whole[6] = {0, 1, 2, 0, 2, 3};
    if (!Write(c, r.ib.Get(), D3D11_MAP_WRITE_DISCARD, 0, whole, sizeof(whole))) return false;
    c.ctx->DrawIndexed(6, 0, static_cast<INT>(kIndexedBase));
    // Only the lower-left triangle of the second quad. A lost rewrite would draw
    // the upper-right one instead.
    const uint16_t half[6] = {0, 2, 3, 0, 1, 2};
    if (!Write(c, r.ib.Get(), D3D11_MAP_WRITE_DISCARD, 0, half, sizeof(half))) return false;
    c.ctx->DrawIndexed(3, 0, static_cast<INT>(kIndexedBase + 4));
    return true;
}

bool Check(const SceneCtx& c, const Image& out)
{
    auto px = [&](uint32_t x) { return x * c.width / 256; };
    auto py = [&](uint32_t y) { return y * c.height / 256; };
    auto is = [&](uint32_t x, uint32_t y, const Rgba& k) {
        return PxNear(out, px(x), py(y), static_cast<int>(k.r * 255), static_cast<int>(k.g * 255),
                      static_cast<int>(k.b * 255), 0);
    };
    TX_REQ(kName, IsClear(out, px(2), py(2)) && IsClear(out, px(128), py(70)));
    for (uint32_t i = 0; i < 8; ++i) {
        TX_REQ(kName, is(20 + 30 * i, 28, kPalette[i]));
        TX_REQ(kName, is(20 + 30 * i, 37 + 4 * i, kPalette[i]));         // just above the bottom edge
        TX_REQ(kName, IsClear(out, px(20 + 30 * i), py(43 + 4 * i)));    // just below it
    }
    for (uint32_t j = 0; j < 6; ++j) TX_REQ(kName, is(31 + 38 * j, 116, kPalette[(j + 3) % 8]));
    TX_REQ(kName, is(40, 190, kPalette[1]) && is(90, 220, kPalette[1]));   // whole first quad
    TX_REQ(kName, is(168, 220, kPalette[4]));                             // lower-left half
    TX_REQ(kName, IsClear(out, px(216), py(188)));                        // upper-right half
    return true;
}
}  // namespace

bool RunDynamicVb(SceneCtx& c, Image& out)
{
    c.ctx->ClearState();
    Res r;
    r.w = static_cast<float>(c.width); r.h = static_cast<float>(c.height);
    r.sx = r.w / 256.0f; r.sy = r.h / 256.0f;
    TX_REQ(kName, MakeTarget(c.dev, c.width, c.height, DXGI_FORMAT_R8G8B8A8_UNORM, r.rt));
    TX_REQ(kName, MakeColorPipe(c.dev, r.pipe));
    r.vb = MakeBuffer(c.dev, kCapacity * sizeof(VtxPC), D3D11_BIND_VERTEX_BUFFER, nullptr, D3D11_USAGE_DYNAMIC);
    r.ib = MakeBuffer(c.dev, 6 * sizeof(uint16_t), D3D11_BIND_INDEX_BUFFER, nullptr, D3D11_USAGE_DYNAMIC);
    r.rs = MakeRaster(c.dev, D3D11_CULL_NONE);
    TX_REQ(kName, r.vb && r.ib && r.rs);

    c.ctx->ClearRenderTargetView(r.rt.rtv.Get(), kClear);
    c.ctx->OMSetRenderTargets(1, r.rt.rtv.GetAddressOf(), nullptr);
    SetViewport(c.ctx, r.w, r.h);
    c.ctx->RSSetState(r.rs.Get());
    BindColorPipe(c.ctx, r.pipe);
    BindVB(c.ctx, r.vb.Get(), sizeof(VtxPC));
    if (!DiscardRounds(c, r) || !AppendRounds(c, r) || !IndexedRounds(c, r)) return false;

    TX_REQ(kName, Readback(c, r.rt.tex.Get(), out));
    c.ctx->ClearState();
    return Check(c, out);
}
}  // namespace txtest
