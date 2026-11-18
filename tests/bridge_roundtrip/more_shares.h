#pragma once
// The stencil byte and a 3D texture across the bridge
// (D3D12Bridge::ShareStencilAndRead, ShareVolumeAndRead). The stencil of a
// D24S8 buffer is cleared to one value and a rectangle of it to another; the
// volume is filled with a value per texel that names its x, y and z. Each
// D3D12 read is compared with what was written, and a control with one
// expected value changed must fail. Original work, MIT.
#include "fixtures.h"

#include <cstring>

namespace fx
{
template <class CheckFn>
void MoreShares(ID3D11Device* dev, ID3D11DeviceContext* ctx, raw::bridge::D3D12Bridge& b, CheckFn&& check)
{
    using Microsoft::WRL::ComPtr;
    // Stencil: 0x5A everywhere, 0xC3 in a scissored clear of the left half through a second view.
    const UINT w = 200, h = 120;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w; td.Height = h; td.MipLevels = td.ArraySize = 1; td.SampleDesc.Count = 1;
    td.Format = DXGI_FORMAT_R24G8_TYPELESS; td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> ds;
    ComPtr<ID3D11DepthStencilView> dsv;
    D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    bool ok = SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &ds)) && SUCCEEDED(dev->CreateDepthStencilView(ds.Get(), &dd, &dsv));
    std::vector<uint8_t> got;
    UINT row = 0;
    if (ok) {
        ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.75f, 0x5A);
        ok = b.ShareStencilAndRead(ds.Get(), &got, &row);
    }
    size_t off = 0, offControl = 0;
    for (size_t i = 0; ok && i < got.size(); ++i) { off += got[i] != 0x5A; offControl += got[i] != 0x5B; }
    check(ok && row == w && got.size() == size_t(w) * h && off == 0, "stencil of a D24S8 buffer: every byte read on D3D12 equals the cleared value");
    check(ok && offControl != 0, "control: another stencil value is not accepted");

    // Volume: R16_FLOAT, value x + 16 y + 256 z (exact in half floats up to 2048).
    const UINT vw = 8, vh = 6, vd = 7;
    std::vector<uint16_t> texels(size_t(vw) * vh * vd);
    auto half = [](float f) {   // exact for the small integers used here
        uint32_t u; memcpy(&u, &f, 4);
        if (f == 0) return uint16_t(0);
        const int e = int((u >> 23) & 0xFF) - 127 + 15;
        return uint16_t((e << 10) | ((u >> 13) & 0x3FF));
    };
    for (UINT z = 0; z < vd; ++z) for (UINT y = 0; y < vh; ++y) for (UINT x = 0; x < vw; ++x)
        texels[(size_t(z) * vh + y) * vw + x] = half(float(x + 16 * y + 256 * z));
    D3D11_TEXTURE3D_DESC vdsc{};
    vdsc.Width = vw; vdsc.Height = vh; vdsc.Depth = vd; vdsc.MipLevels = 1;
    vdsc.Format = DXGI_FORMAT_R16_FLOAT; vdsc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    D3D11_SUBRESOURCE_DATA init{texels.data(), vw * 2, vw * vh * 2};
    ComPtr<ID3D11Texture3D> vol;
    UINT cols = 0;
    DXGI_FORMAT fmt{};
    ok = SUCCEEDED(dev->CreateTexture3D(&vdsc, &init, &vol)) && b.ShareVolumeAndRead(vol.Get(), &got, &row, &cols, &fmt);
    size_t wrong = 0, wrongControl = 0;
    if (ok && cols) {
        const UINT aw = vw * cols;
        for (UINT z = 0; z < vd; ++z) for (UINT y = 0; y < vh; ++y) for (UINT x = 0; x < vw; ++x) {
            uint16_t v;
            memcpy(&v, &got[(size_t((z / cols) * vh + y) * aw + (z % cols) * vw + x) * 2], 2);
            wrong += v != texels[(size_t(z) * vh + y) * vw + x];
            wrongControl += v != texels[(size_t((z + 1) % vd) * vh + y) * vw + x];   // the next slice's values
        }
    }
    check(ok && cols == 3 && fmt == DXGI_FORMAT_R16_FLOAT && wrong == 0, "3D R16F volume: every texel of every slice read on D3D12 equals the D3D11 texel");
    check(ok && wrongControl != 0, "control: the slices shifted by one are not accepted");
}
} // namespace fx
