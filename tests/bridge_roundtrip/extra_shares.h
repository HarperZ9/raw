#pragma once
// On-demand sharing (D3D12Bridge::ShareAndRead): buffers that do not cross
// every frame must still read the same on D3D12. The fixtures follow what the
// game holds: a 16-bit depth array (shadow cascades), a float cube map
// (reflections), a D24S8 buffer of another size, and two-channel masks.
// Each read is compared with the bytes the D3D11 texture was made from, and a
// control compares against one changed byte. Original work, MIT.
#include "fixtures.h"

namespace fx
{
struct ShareCase
{
    const char* name;
    UINT w, h, layers, bytesPerPixel;
    DXGI_FORMAT format;
    UINT bind, misc;
};

template <class CheckFn>
void ExtraShares(ID3D11Device* dev, raw::bridge::D3D12Bridge& b, CheckFn&& check)
{
    const ShareCase cases[] = {
        {"R16 depth array, layer 1 (a shadow cascade)", 512, 512, 2, 2, DXGI_FORMAT_R16_TYPELESS, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0},
        {"RGBA16F cube map, face 4 (reflections)", 128, 128, 6, 8, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_TEXTURECUBE},
        {"RG16F (motion vectors)", 320, 180, 1, 4, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0},
        {"RG8 (mask)", 320, 180, 1, 2, DXGI_FORMAT_R8G8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0},
        {"R11G11B10F", 320, 180, 1, 4, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0},
        {"D24S8 at 512x512, as float depth", 512, 512, 1, 4, DXGI_FORMAT_R24G8_TYPELESS, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0},
    };
    for (const ShareCase& c : cases) {
        const UINT layer = c.layers - 1 - (c.layers > 2 ? 1 : 0);
        const size_t layerBytes = size_t(c.w) * c.h * c.bytesPerPixel;
        std::vector<uint8_t> all(layerBytes * c.layers);
        for (size_t i = 0; i < all.size(); ++i) all[i] = uint8_t((i * 2654435761u) >> 13);
        const bool d24 = c.format == DXGI_FORMAT_R24G8_TYPELESS;
        const bool halfFloat = c.format == DXGI_FORMAT_R16G16B16A16_FLOAT || c.format == DXGI_FORMAT_R16G16_FLOAT;
        if (halfFloat)   // keep every half finite: a copy may quiet a signalling NaN
            for (size_t i = 1; i < all.size(); i += 2) all[i] &= 0x3F;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = c.w; td.Height = c.h; td.MipLevels = 1; td.ArraySize = c.layers; td.Format = c.format;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = c.bind; td.MiscFlags = c.misc;
        std::vector<D3D11_SUBRESOURCE_DATA> init(c.layers);
        for (UINT l = 0; l < c.layers; ++l) init[l] = {all.data() + layerBytes * l, c.w * c.bytesPerPixel, 0};
        ComPtr<ID3D11Texture2D> tex;
        dev->CreateTexture2D(&td, init.data(), &tex);
        std::vector<uint8_t> got;
        UINT row = 0;
        DXGI_FORMAT crossed = DXGI_FORMAT_UNKNOWN;
        const bool read = tex && b.ShareAndRead(tex.Get(), layer, &got, &row, &crossed);
        const uint8_t* want = all.data() + layerBytes * layer;
        long off = 0, offControl = 0;
        if (read && d24) {
            const uint32_t* u = reinterpret_cast<const uint32_t*>(want);
            const float* f = reinterpret_cast<const float*>(got.data());
            for (size_t i = 0; i < size_t(c.w) * c.h; ++i) {
                const double v = double(u[i] & 0xFFFFFF) / 16777215.0;
                off += std::fabs(double(f[i]) - v) > 6.0e-8;
                offControl += std::fabs(double(f[i]) - (i == 7 ? v + 1.0e-3 : v)) > 6.0e-8;
            }
        } else if (read) {
            off = got.size() == layerBytes ? (memcmp(got.data(), want, layerBytes) != 0) : 1;
            std::vector<uint8_t> changed(want, want + layerBytes);
            changed[layerBytes / 2] ^= 1;
            offControl = got.size() == layerBytes ? (memcmp(got.data(), changed.data(), layerBytes) != 0) : 0;
        }
        char line[200];
        sprintf_s(line, "on-demand share: %s reads the same on D3D12 (crossed as format %d)", c.name, int(crossed));
        check(read && off == 0, line);
        sprintf_s(line, "CONTROL: %s against one changed value is reported", c.name);
        check(read && offControl != 0, line);
    }
}
} // namespace fx
