// Scene 14: the texture shapes a game frame uses beyond plain 2D targets. Layout of
// the 256x256 output (256ths):
//   (0,0)-(192,128)     six cube faces, 3 by 2, sampled with direction vectors from an
//                       RGBA16F cube map whose faces were rendered one RTV at a time
//   (192,0)-(256,128)   the two slices of an R16 depth array, sampled as Texture2DArray
//   (0,128)-(256,192)   four depths of an R16F 3D volume written by two compute passes
//   (0,192)-(224,224)   the seven mips of an R32F pyramid, each rendered from the last
//   (0,224)-(256,256)   five quads from one DrawInstancedIndirect
#include "volumes_cubes_arrays.h"
#include <algorithm>
#include <cstdlib>

namespace txtest {
namespace volumes {
namespace {
const float kVolDepths[4] = {1.5f / 16, 4.0f / 16, 6.5f / 16, 13.5f / 16};

// One full-viewport draw into the cell at (x, y), sized w by h, in 256ths.
void Cell(SceneCtx& c, const Res& r, ID3D11PixelShader* ps, ID3D11ShaderResourceView* srv,
          float param, float x, float y, float w, float h)
{
    const float sx = static_cast<float>(c.width) / 256.0f, sy = static_cast<float>(c.height) / 256.0f;
    const float p[4] = {param, 0, 0, 0};
    c.ctx->UpdateSubresource(r.cb.Get(), 0, nullptr, p, 0, 0);
    SetViewport(c.ctx, w * sx, h * sy, x * sx, y * sy);
    c.ctx->PSSetShader(ps, nullptr, 0);
    c.ctx->PSSetShaderResources(0, 1, &srv);
    c.ctx->Draw(3, 0);
}

void Composite(SceneCtx& c, const Res& r)
{
    c.ctx->ClearRenderTargetView(r.out.rtv.Get(), kClear);
    c.ctx->OMSetRenderTargets(1, r.out.rtv.GetAddressOf(), nullptr);
    c.ctx->OMSetDepthStencilState(nullptr, 0);
    c.ctx->IASetInputLayout(nullptr);
    c.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c.ctx->VSSetShader(r.fsVs.Get(), nullptr, 0);
    c.ctx->PSSetConstantBuffers(0, 1, r.cb.GetAddressOf());
    c.ctx->PSSetSamplers(0, 1, r.point.GetAddressOf());
    for (int f = 0; f < 6; ++f)
        Cell(c, r, r.psCube.Get(), r.cubeSrv.Get(), static_cast<float>(f),
             static_cast<float>(64 * (f % 3)), static_cast<float>(64 * (f / 3)), 64, 64);
    for (int s = 0; s < 2; ++s)
        Cell(c, r, r.psArr.Get(), r.arrSrv.Get(), static_cast<float>(s), 192, static_cast<float>(64 * s), 64, 64);
    for (UINT m = 0; m < kMips; ++m)
        Cell(c, r, r.psMip.Get(), r.mipSrvAll.Get(), static_cast<float>(m), static_cast<float>(32 * m), 192, 32, 32);
    c.ctx->PSSetSamplers(0, 1, r.linear.GetAddressOf());
    for (int k = 0; k < 4; ++k)
        Cell(c, r, r.psVol.Get(), r.volSrvB.Get(), kVolDepths[k], static_cast<float>(64 * k), 128, 64, 64);
}

void DrawIndirectQuads(SceneCtx& c, const Res& r)
{
    SetViewport(c.ctx, static_cast<float>(c.width), static_cast<float>(c.height));
    c.ctx->IASetInputLayout(r.layoutInst.Get());
    BindVB(c.ctx, r.vbInst.Get(), 2 * sizeof(float));
    c.ctx->VSSetShader(r.vsInst.Get(), nullptr, 0);
    c.ctx->PSSetShader(r.pipe.ps.Get(), nullptr, 0);
    c.ctx->DrawInstancedIndirect(r.args.Get(), 16);
}

// Pixel of the output for a position given in 256ths.
bool At(const SceneCtx& c, const Image& o, float x, float y, int r, int g, int b, int tol, const char* what)
{
    const uint32_t px = static_cast<uint32_t>(x * static_cast<float>(c.width) / 256.0f);
    const uint32_t py = static_cast<uint32_t>(y * static_cast<float>(c.height) / 256.0f);
    if (PxNear(o, px, py, r, g, b, tol)) return true;
    const uint8_t* p = Px(o, px, py);
    fprintf(stderr, "[%s] %s at (%u,%u): got %d,%d,%d want %d,%d,%d\n", kName, what, px, py, p[0], p[1],
            p[2], r, g, b);
    return false;
}

bool CheckCube(const SceneCtx& c, const Image& o)
{
    for (int f = 0; f < 6; ++f) {
        const float x = static_cast<float>(64 * (f % 3)) + 0.5f, y = static_cast<float>(64 * (f / 3)) + 0.5f;
        const float end = static_cast<float>(20 + 4 * f);   // marker length names the face
        const int b[3] = {kFaceBase[f][0] > 1 ? 204 : 51, kFaceBase[f][1] > 1 ? 204 : 51,
                          kFaceBase[f][2] > 1 ? 204 : 51};
        TX_REQ(kName, At(c, o, x + 6, y + 6, 255, 255, 255, 1, "cube marker"));
        TX_REQ(kName, At(c, o, x + end - 1, y + 6, 255, 255, 255, 1, "cube marker end"));
        TX_REQ(kName, At(c, o, x + end + 1, y + 6, b[0], b[1], b[2], 1, "cube past marker"));
        TX_REQ(kName, At(c, o, x + 14, y + 12, 255, 255, 255, 1, "cube marker over quad (depth)"));
        TX_REQ(kName, At(c, o, x + 30, y + 30, 0, 0, 0, 1, "cube black quad"));
        TX_REQ(kName, At(c, o, x + 57, y + 6, b[0], b[1], b[2], 1, "cube top right"));
        TX_REQ(kName, At(c, o, x + 6, y + 57, b[0], b[1], b[2], 1, "cube bottom left"));
    }
    return true;
}

bool CheckArrayAndIndirect(const SceneCtx& c, const Image& o)
{
    // Cells show every second texel: output (192 + k, row + j) is texel (2k+1, 2j+1).
    TX_REQ(kName, At(c, o, 192.5f + 16, 16.5f, 51, 51, 0, 1, "slice 0 near quad"));
    TX_REQ(kName, At(c, o, 192.5f + 32, 32.5f, 51, 51, 0, 1, "slice 0 overlap"));
    TX_REQ(kName, At(c, o, 192.5f + 48, 48.5f, 153, 153, 0, 1, "slice 0 far quad"));
    TX_REQ(kName, At(c, o, 192.5f + 2, 60.5f, 255, 255, 0, 1, "slice 0 clear"));
    TX_REQ(kName, At(c, o, 192.5f + 8, 64.5f + 32, 0, 204, 204, 1, "slice 1 far quad"));
    TX_REQ(kName, At(c, o, 192.5f + 32, 64.5f + 32, 0, 102, 102, 1, "slice 1 overlap"));
    TX_REQ(kName, At(c, o, 192.5f + 32, 64.5f + 8, 0, 102, 102, 1, "slice 1 near quad"));
    TX_REQ(kName, At(c, o, 192.5f + 2, 64.5f + 2, 0, 255, 255, 1, "slice 1 clear"));
    for (int i = 0; i < static_cast<int>(kInstances); ++i) {
        const float x = static_cast<float>(20 + 48 * i);
        TX_REQ(kName, At(c, o, x, 240, 51 * (i + 1), 255 - 51 * i, 153, 1, "indirect quad"));
        TX_REQ(kName, IsClear(o, static_cast<uint32_t>((x + 24) * static_cast<float>(c.width) / 256.0f),
                              240 * c.height / 256));
    }
    TX_REQ(kName, IsClear(o, 240 * c.width / 256, 208 * c.height / 256));
    return true;
}

float VolB(int x, int y, int z)
{
    const bool par = (((x >> 2) + (y >> 2) + (z >> 2)) & 1) != 0;
    return (par ? 0.5625f : 0.375f) + static_cast<float>(z) / 64.0f;
}

bool CheckVolume(const SceneCtx& c, const Image& o)
{
    // Cells magnify 2x with linear filtering; inside a 4-texel block the value is flat.
    const int z0[4] = {1, 3, 6, 13}, z1[4] = {1, 4, 6, 13};
    for (int k = 0; k < 4; ++k)
        for (int by = 0; by < 8; ++by)
            for (int bx = 0; bx < 8; ++bx) {
                const float v = 0.5f * (VolB(4 * bx + 1, 4 * by + 1, z0[k]) + VolB(4 * bx + 1, 4 * by + 1, z1[k]));
                const int e = static_cast<int>(v * 255.0f + 0.5f), h = static_cast<int>(v * 127.5f + 0.5f);
                const float x = static_cast<float>(64 * k + 8 * bx) + 3.5f, y = 128.0f + static_cast<float>(8 * by) + 4.5f;
                TX_REQ(kName, At(c, o, x, y, e, e, h, 2, "volume"));
            }
    return true;
}

bool CheckMips(const SceneCtx& c, const Image& o)
{
    std::vector<float> cur(kMipSize * kMipSize);
    for (UINT y = 0; y < kMipSize; ++y)
        for (UINT x = 0; x < kMipSize; ++x)
            cur[y * kMipSize + x] = static_cast<float>((x * x * 3 + y * 5 + (x ^ y) * 7) & 31) / 32.0f;
    for (UINT m = 0, size = kMipSize; m < kMips; ++m, size /= 2) {
        for (UINT py = 0; py < 32; ++py)
            for (UINT px = 0; px < 32; ++px) {
                const float v = cur[((2 * py + 1) * size / 64) * size + (2 * px + 1) * size / 64] * 0.9f;
                const int r = static_cast<int>(v * 255.0f + 0.5f), g = static_cast<int>((1 - v) * 255.0f + 0.5f);
                TX_REQ(kName, At(c, o, static_cast<float>(32 * m + px) + 0.5f, 192.5f + static_cast<float>(py), r, g, 64, 1, "mip"));
            }
        std::vector<float> next((size / 2) * (size / 2));
        for (UINT y = 0; y < size / 2; ++y)
            for (UINT x = 0; x < size / 2; ++x) {
                const float* s = &cur[(2 * y) * size + 2 * x];
                next[y * (size / 2) + x] = std::max(std::max(s[0], s[1]), std::max(s[size], s[size + 1]));
            }
        cur.swap(next);
    }
    return true;
}
}  // namespace
}  // namespace volumes

bool RunVolumesCubesArrays(SceneCtx& c, Image& out)
{
    using namespace volumes;
    c.ctx->ClearState();
    Res r;
    if (!MakeResources(c, r) || !MakeShaders(c, r)) return false;
    c.ctx->RSSetState(r.rs.Get());

    RenderCube(c, r);
    RenderDepthArray(c, r);
    RunVolumeCompute(c, r);
    BuildMipPyramid(c, r);
    Composite(c, r);
    DrawIndirectQuads(c, r);

    TX_REQ(kName, Readback(c, r.out.tex.Get(), out));
    c.ctx->ClearState();
    return CheckCube(c, out) && CheckArrayAndIndirect(c, out) && CheckVolume(c, out) && CheckMips(c, out);
}
}  // namespace txtest
