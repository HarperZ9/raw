// Readback and image tools for the D3D11 test scenes.
#include "scene_util.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace txtest {

bool Readback(SceneCtx& c, ID3D11Texture2D* rgba8Tex, Image& out)
{
    if (!rgba8Tex) return false;
    D3D11_TEXTURE2D_DESC td{};
    rgba8Tex->GetDesc(&td);
    if (td.Format != DXGI_FORMAT_R8G8B8A8_UNORM || td.SampleDesc.Count != 1) {
        fprintf(stderr, "[scene_util] Readback wants a single-sample R8G8B8A8_UNORM texture\n");
        return false;
    }
    td.MipLevels = 1;
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> st;
    HRESULT hr = c.dev->CreateTexture2D(&td, nullptr, &st);
    if (FAILED(hr)) return Fail("readback", "CreateTexture2D(staging)", hr);
    if (c.onReadback) c.onReadback(c.user, rgba8Tex);
    c.ctx->CopyResource(st.Get(), rgba8Tex);
    D3D11_MAPPED_SUBRESOURCE m{};
    hr = c.ctx->Map(st.Get(), 0, D3D11_MAP_READ, 0, &m);
    if (FAILED(hr)) return Fail("readback", "Map(READ)", hr);
    out.width = td.Width;
    out.height = td.Height;
    out.rgba.resize(static_cast<size_t>(td.Width) * td.Height * 4);
    for (UINT y = 0; y < td.Height; ++y)
        memcpy(&out.rgba[static_cast<size_t>(y) * td.Width * 4],
               static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch,
               static_cast<size_t>(td.Width) * 4);
    c.ctx->Unmap(st.Get(), 0);
    return true;
}

#pragma pack(push, 1)
struct BmpHeader {
    uint16_t type = 0x4D42;
    uint32_t fileSize = 0;
    uint32_t reserved = 0;
    uint32_t dataOffset = 54;
    uint32_t infoSize = 40;
    int32_t width = 0;
    int32_t height = 0;   // negative: top row first
    uint16_t planes = 1;
    uint16_t bpp = 32;
    uint32_t compression = 0;
    uint32_t imageSize = 0;
    int32_t ppmX = 2835, ppmY = 2835;
    uint32_t clrUsed = 0, clrImportant = 0;
};
#pragma pack(pop)

bool SaveBMP(const char* path, const Image& img)
{
    const size_t n = static_cast<size_t>(img.width) * img.height;
    if (img.rgba.size() != n * 4 || n == 0) return false;
    std::vector<uint8_t> bgra(n * 4);
    for (size_t i = 0; i < n; ++i) {
        bgra[i * 4 + 0] = img.rgba[i * 4 + 2];
        bgra[i * 4 + 1] = img.rgba[i * 4 + 1];
        bgra[i * 4 + 2] = img.rgba[i * 4 + 0];
        bgra[i * 4 + 3] = img.rgba[i * 4 + 3];
    }
    BmpHeader h;
    h.width = static_cast<int32_t>(img.width);
    h.height = -static_cast<int32_t>(img.height);
    h.imageSize = static_cast<uint32_t>(n * 4);
    h.fileSize = h.dataOffset + h.imageSize;
    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || !f) {
        fprintf(stderr, "[scene_util] SaveBMP: cannot open %s\n", path);
        return false;
    }
    const bool ok = fwrite(&h, sizeof(h), 1, f) == 1 && fwrite(bgra.data(), 1, bgra.size(), f) == bgra.size();
    fclose(f);
    if (!ok) fprintf(stderr, "[scene_util] SaveBMP: short write to %s\n", path);
    return ok;
}

uint64_t HashImage(const Image& img)
{
    uint64_t h = 14695981039346656037ull;
    auto mix = [&h](uint8_t b) { h ^= b; h *= 1099511628211ull; };
    for (int i = 0; i < 4; ++i) mix(static_cast<uint8_t>(img.width >> (8 * i)));
    for (int i = 0; i < 4; ++i) mix(static_cast<uint8_t>(img.height >> (8 * i)));
    for (uint8_t b : img.rgba) mix(b);
    return h;
}

int MaxDiff(const Image& a, const Image& b)
{
    if (a.width != b.width || a.height != b.height || a.rgba.size() != b.rgba.size()) return -1;
    int worst = 0;
    for (size_t i = 0; i < a.rgba.size(); ++i)
        worst = std::max(worst, std::abs(static_cast<int>(a.rgba[i]) - static_cast<int>(b.rgba[i])));
    return worst;
}

const uint8_t* Px(const Image& img, uint32_t x, uint32_t y)
{
    static const uint8_t zero[4] = {0, 0, 0, 0};
    if (x >= img.width || y >= img.height) return zero;
    return &img.rgba[(static_cast<size_t>(y) * img.width + x) * 4];
}

bool PxNear(const Image& img, uint32_t x, uint32_t y, int r, int g, int b, int tol)
{
    if (x >= img.width || y >= img.height) return false;
    const uint8_t* p = Px(img, x, y);
    return std::abs(p[0] - r) <= tol && std::abs(p[1] - g) <= tol && std::abs(p[2] - b) <= tol;
}

bool IsClear(const Image& img, uint32_t x, uint32_t y)
{
    return PxNear(img, x, y, 51, 102, 153, 0);
}

uint32_t CountNotClear(const Image& img)
{
    uint32_t n = 0;
    for (uint32_t y = 0; y < img.height; ++y)
        for (uint32_t x = 0; x < img.width; ++x)
            if (!IsClear(img, x, y)) ++n;
    return n;
}

}  // namespace txtest
