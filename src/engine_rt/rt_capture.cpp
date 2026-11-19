// Backbuffer to BMP for engine-control proofs (RAW engine runtime). Original work, MIT.
#include "rt_capture.h"

#include <cstdio>
#include <vector>

namespace raw::rt {

bool CaptureBackbuffer(ID3D11DeviceContext* ctx, IDXGISwapChain* sc, const std::string& path) {
    if (!ctx || !sc) return false;
    ID3D11Texture2D* back = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) || !back) return false;
    D3D11_TEXTURE2D_DESC d;
    back->GetDesc(&d);
    const bool bgra = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    const bool rgba = d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    const bool r10 = d.Format == DXGI_FORMAT_R10G10B10A2_UNORM;
    if (!(bgra || rgba || r10) || d.SampleDesc.Count != 1) { back->Release(); return false; }
    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    D3D11_TEXTURE2D_DESC s = d;
    s.Usage = D3D11_USAGE_STAGING; s.BindFlags = 0; s.CPUAccessFlags = D3D11_CPU_ACCESS_READ; s.MiscFlags = 0;
    s.MipLevels = 1; s.ArraySize = 1;
    ID3D11Texture2D* staging = nullptr;
    bool ok = false;
    if (SUCCEEDED(dev->CreateTexture2D(&s, nullptr, &staging))) {
        ctx->CopyResource(staging, back);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
            const uint32_t w = d.Width, h = d.Height, row = (w * 3 + 3) & ~3u;
            std::vector<uint8_t> px(size_t(row) * h);
            for (uint32_t y = 0; y < h; ++y) {
                const auto* src = static_cast<const uint8_t*>(m.pData) + size_t(m.RowPitch) * y;
                uint8_t* dst = px.data() + size_t(row) * (h - 1 - y);  // bottom-up rows
                for (uint32_t x = 0; x < w; ++x) {
                    uint8_t r, g, b;
                    if (r10) {
                        const uint32_t v = reinterpret_cast<const uint32_t*>(src)[x];
                        r = uint8_t((v & 1023) >> 2); g = uint8_t(((v >> 10) & 1023) >> 2); b = uint8_t(((v >> 20) & 1023) >> 2);
                    } else {
                        const uint8_t* p = src + 4 * x;
                        r = bgra ? p[2] : p[0]; g = p[1]; b = bgra ? p[0] : p[2];
                    }
                    dst[3 * x] = b; dst[3 * x + 1] = g; dst[3 * x + 2] = r;
                }
            }
            ctx->Unmap(staging, 0);
            if (FILE* f = std::fopen(path.c_str(), "wb")) {
                const uint32_t size = 54 + uint32_t(px.size());
                uint8_t hdr[54] = {'B', 'M'};
                auto put = [&](int at, uint32_t v) { for (int i = 0; i < 4; ++i) hdr[at + i] = uint8_t(v >> (8 * i)); };
                put(2, size); put(10, 54); put(14, 40); put(18, w); put(22, h);
                hdr[26] = 1; hdr[28] = 24; put(34, uint32_t(px.size()));
                std::fwrite(hdr, 1, 54, f);
                std::fwrite(px.data(), 1, px.size(), f);
                std::fclose(f);
                ok = true;
            }
        }
        staging->Release();
    }
    dev->Release();
    back->Release();
    return ok;
}

}  // namespace raw::rt
