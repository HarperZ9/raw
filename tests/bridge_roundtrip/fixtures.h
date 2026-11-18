#pragma once
// Fixtures for the D3D12 bridge round-trip test: known colour and depth
// patterns, a CPU reference of the haze, and a pixel comparator.
#include "D3D12Bridge.h"
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <vector>

namespace fx
{
using Microsoft::WRL::ComPtr;

inline uint32_t ColourAt(UINT x, UINT y)
{
    uint32_t r = (x * 7 + y * 3) & 255, g = ((x * 5) ^ (y * 11)) & 255, b = (x + y * 2) & 255;
    return r | (g << 8) | (b << 16) | (255u << 24);
}

inline float DepthBand(UINT x, UINT w)
{
    const float bands[4] = {0.0f, 0.5f, 0.9f, 0.999f};
    return bands[(x * 4) / w];
}

inline uint32_t DepthWordAt(UINT x, UINT y, UINT w)
{
    uint32_t d24 = static_cast<uint32_t>(std::lround(DepthBand(x, w) * 16777215.0));
    return d24 | (((x * 13 + y) & 255u) << 24);   // stencil byte is noise the pass ignores
}

// CPU reference of DepthFog.hlsl mode 1 for one channel, in [0,255].
inline float HazeRef(float c255, float fog255, float d, const raw::bridge::PassConstants& k)
{
    float z = (k.nearZ * k.farZ) / (k.farZ - d * (k.farZ - k.nearZ));
    float f = (1.0f - std::exp(-k.density * z)) * k.maxOpacity;
    return c255 + (fog255 - c255) * f;
}

inline std::vector<uint32_t> Expected(UINT w, UINT h, const raw::bridge::PassConstants& k,
                                      bool uniformDepth, float uniformD)
{
    std::vector<uint32_t> out(w * h);
    for (UINT y = 0; y < h; ++y)
        for (UINT x = 0; x < w; ++x) {
            uint32_t c = ColourAt(x, y);
            if (k.mode == 0.0f) { out[y * w + x] = c; continue; }
            float d = uniformDepth ? uniformD
                                   : static_cast<float>(DepthWordAt(x, y, w) & 0xFFFFFF) / 16777215.0f;
            uint32_t px = 255u << 24;
            for (int ch = 0; ch < 3; ++ch) {
                float v = HazeRef(static_cast<float>((c >> (8 * ch)) & 255), k.fogColor[ch] * 255.0f, d, k);
                px |= static_cast<uint32_t>(std::lround(std::fmin(std::fmax(v, 0.0f), 255.0f))) << (8 * ch);
            }
            out[y * w + x] = px;
        }
    return out;
}

struct Diff { uint32_t mismatches = 0; int maxDelta = 0; };

inline Diff Compare(const std::vector<uint32_t>& got, const std::vector<uint32_t>& want, int tol)
{
    Diff d;
    for (size_t i = 0; i < got.size(); ++i) {
        int worst = 0;
        for (int ch = 0; ch < 4; ++ch) {
            int a = (got[i] >> (8 * ch)) & 255, b = (want[i] >> (8 * ch)) & 255;
            worst = std::max(worst, std::abs(a - b));
        }
        d.maxDelta = std::max(d.maxDelta, worst);
        if (worst > tol) ++d.mismatches;
    }
    return d;
}

inline ComPtr<ID3D11Texture2D> MakeTex(ID3D11Device* dev, UINT w, UINT h, DXGI_FORMAT f,
                                       UINT bind, const void* init, UINT pitch)
{
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = f; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = bind;
    D3D11_SUBRESOURCE_DATA sd{init, pitch, 0};
    ComPtr<ID3D11Texture2D> t;
    dev->CreateTexture2D(&td, init ? &sd : nullptr, &t);
    return t;
}

inline ComPtr<ID3D11ShaderResourceView> MakeSrv(ID3D11Device* dev, ID3D11Texture2D* t, DXGI_FORMAT f)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC d{};
    d.Format = f; d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; d.Texture2D.MipLevels = 1;
    ComPtr<ID3D11ShaderResourceView> v;
    dev->CreateShaderResourceView(t, &d, &v);
    return v;
}

inline std::vector<uint32_t> ReadBack(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                      ID3D11Texture2D* src, UINT w, UINT h)
{
    D3D11_TEXTURE2D_DESC td{};
    src->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> st;
    std::vector<uint32_t> px(w * h);
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &st))) return {};
    ctx->CopyResource(st.Get(), src);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(st.Get(), 0, D3D11_MAP_READ, 0, &m))) return {};
    for (UINT y = 0; y < h; ++y)
        memcpy(&px[y * w], static_cast<uint8_t*>(m.pData) + y * m.RowPitch, w * 4);
    ctx->Unmap(st.Get(), 0);
    return px;
}
} // namespace fx
