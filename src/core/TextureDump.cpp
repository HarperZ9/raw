#include "TextureDump.h"
#include "GTAORenderer.h"
#include "ContactShadowRenderer.h"
#include "SkylightingRenderer.h"
#include "SSRRenderer.h"
#include "SSGIRenderer.h"
#include "HiZPyramid.h"
#include "SharedGPUResources.h"
#include "D3D11Hook.h"
#include <SKSE/SKSE.h>
#include <fstream>
#include <vector>
#include <cstring>
#include <algorithm>

namespace SB
{

static std::filesystem::path s_outputDir;

void TextureDump::SetOutputDir(const std::filesystem::path& dir)
{
    s_outputDir = dir;
    std::error_code ec;
    std::filesystem::create_directories(s_outputDir, ec);
}

bool TextureDump::SaveSRV(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                           ID3D11ShaderResourceView* srv,
                           const std::string& name, uint32_t frameIndex)
{
    if (!dev || !ctx || !srv) return false;

    // Get the underlying texture resource
    ID3D11Resource* res = nullptr;
    srv->GetResource(&res);
    if (!res) return false;

    ID3D11Texture2D* tex = nullptr;
    res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex));
    res->Release();
    if (!tex) return false;

    char filename[256];
    snprintf(filename, sizeof(filename), "frame%04u_%s.bmp", frameIndex, name.c_str());
    auto filepath = (s_outputDir / filename).string();

    bool ok = SaveTexture2D(dev, ctx, tex, filepath);
    tex->Release();

    if (ok) SKSE::log::info("TextureDump: saved {}", filepath);
    return ok;
}

bool TextureDump::SaveTexture2D(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                 ID3D11Texture2D* tex, const std::string& filepath)
{
    D3D11_TEXTURE2D_DESC desc;
    tex->GetDesc(&desc);

    // Create staging texture for CPU readback
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage          = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags      = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags      = 0;
    stagingDesc.MipLevels      = 1;
    stagingDesc.ArraySize       = 1;
    stagingDesc.SampleDesc     = { 1, 0 };

    ID3D11Texture2D* staging = nullptr;
    if (FAILED(dev->CreateTexture2D(&stagingDesc, nullptr, &staging)))
        return false;

    // Copy GPU → staging (use subresource 0, mip 0)
    ctx->CopySubresourceRegion(staging, 0, 0, 0, 0, tex, 0, nullptr);

    // Map staging for CPU read
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        staging->Release();
        return false;
    }

    // Convert to RGBA8 regardless of source format
    uint32_t w = desc.Width;
    uint32_t h = desc.Height;
    std::vector<uint8_t> rgba(w * h * 4);

    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);

    switch (desc.Format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        for (uint32_t y = 0; y < h; y++)
            std::memcpy(&rgba[y * w * 4], src + y * mapped.RowPitch, w * 4);
        break;

    case DXGI_FORMAT_R32_FLOAT: {
        // Scalar float → grayscale (auto-range: find min/max for contrast)
        float minV = 1e30f, maxV = -1e30f;
        for (uint32_t y = 0; y < h; y++) {
            const float* row = reinterpret_cast<const float*>(src + y * mapped.RowPitch);
            for (uint32_t x = 0; x < w; x++) {
                float v = row[x];
                if (v > 0.0f && v < 1e20f) { minV = (std::min)(minV, v); maxV = (std::max)(maxV, v); }
            }
        }
        float range = (maxV - minV > 1e-6f) ? (maxV - minV) : 1.0f;
        for (uint32_t y = 0; y < h; y++) {
            const float* row = reinterpret_cast<const float*>(src + y * mapped.RowPitch);
            for (uint32_t x = 0; x < w; x++) {
                float v = (row[x] - minV) / range;
                uint8_t b = static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
                rgba[(y * w + x) * 4 + 0] = b;
                rgba[(y * w + x) * 4 + 1] = b;
                rgba[(y * w + x) * 4 + 2] = b;
                rgba[(y * w + x) * 4 + 3] = 255;
            }
        }
        break;
    }

    case DXGI_FORMAT_R16_FLOAT: {
        // Half-float scalar → grayscale
        for (uint32_t y = 0; y < h; y++) {
            const uint16_t* row = reinterpret_cast<const uint16_t*>(src + y * mapped.RowPitch);
            for (uint32_t x = 0; x < w; x++) {
                // Quick half→float: good enough for visualization
                uint16_t hf = row[x];
                uint32_t sign = (hf >> 15) & 1;
                uint32_t exp  = (hf >> 10) & 0x1F;
                uint32_t mant = hf & 0x3FF;
                float v = 0.0f;
                if (exp == 0) v = (mant / 1024.0f) * (1.0f / 16384.0f);
                else if (exp < 31) v = (1.0f + mant / 1024.0f) * powf(2.0f, (float)exp - 15.0f);
                if (sign) v = -v;
                uint8_t b = static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
                rgba[(y * w + x) * 4 + 0] = b;
                rgba[(y * w + x) * 4 + 1] = b;
                rgba[(y * w + x) * 4 + 2] = b;
                rgba[(y * w + x) * 4 + 3] = 255;
            }
        }
        break;
    }

    case DXGI_FORMAT_R16G16B16A16_FLOAT: {
        // RGBA half-float → RGBA8 (tone-mapped)
        for (uint32_t y = 0; y < h; y++) {
            const uint16_t* row = reinterpret_cast<const uint16_t*>(src + y * mapped.RowPitch);
            for (uint32_t x = 0; x < w; x++) {
                for (int c = 0; c < 4; c++) {
                    uint16_t hf = row[x * 4 + c];
                    uint32_t exp  = (hf >> 10) & 0x1F;
                    uint32_t mant = hf & 0x3FF;
                    float v = 0.0f;
                    if (exp == 0) v = (mant / 1024.0f) * (1.0f / 16384.0f);
                    else if (exp < 31) v = (1.0f + mant / 1024.0f) * powf(2.0f, (float)exp - 15.0f);
                    if (hf & 0x8000) v = -v;
                    // Simple Reinhard for HDR → [0,1]
                    v = v / (1.0f + v);
                    rgba[(y * w + x) * 4 + c] = static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
                }
            }
        }
        break;
    }

    case DXGI_FORMAT_R8_UNORM: {
        // Single-channel 8-bit (contact shadows, material IDs)
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t* row = src + y * mapped.RowPitch;
            for (uint32_t x = 0; x < w; x++) {
                uint8_t v = row[x];
                rgba[(y * w + x) * 4 + 0] = v;
                rgba[(y * w + x) * 4 + 1] = v;
                rgba[(y * w + x) * 4 + 2] = v;
                rgba[(y * w + x) * 4 + 3] = 255;
            }
        }
        break;
    }

    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_R24G8_TYPELESS: {
        // 24-bit depth (game depth buffer via DepthIntercept)
        // Read as R32 and extract upper 24 bits as normalized float
        for (uint32_t y = 0; y < h; y++) {
            const uint32_t* row = reinterpret_cast<const uint32_t*>(src + y * mapped.RowPitch);
            for (uint32_t x = 0; x < w; x++) {
                float v = static_cast<float>(row[x] >> 8) / 16777215.0f; // 24-bit normalized
                uint8_t b = static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
                rgba[(y * w + x) * 4 + 0] = b;
                rgba[(y * w + x) * 4 + 1] = b;
                rgba[(y * w + x) * 4 + 2] = b;
                rgba[(y * w + x) * 4 + 3] = 255;
            }
        }
        break;
    }

    case DXGI_FORMAT_R10G10B10A2_UNORM: {
        // 10-bit per channel (backbuffer, some render targets)
        for (uint32_t y = 0; y < h; y++) {
            const uint32_t* row = reinterpret_cast<const uint32_t*>(src + y * mapped.RowPitch);
            for (uint32_t x = 0; x < w; x++) {
                uint32_t p = row[x];
                rgba[(y * w + x) * 4 + 0] = static_cast<uint8_t>((p & 0x3FF) >> 2);
                rgba[(y * w + x) * 4 + 1] = static_cast<uint8_t>(((p >> 10) & 0x3FF) >> 2);
                rgba[(y * w + x) * 4 + 2] = static_cast<uint8_t>(((p >> 20) & 0x3FF) >> 2);
                rgba[(y * w + x) * 4 + 3] = 255;
            }
        }
        break;
    }

    case DXGI_FORMAT_R11G11B10_FLOAT: {
        // Shared-exponent HDR (specular, some render targets)
        for (uint32_t y = 0; y < h; y++) {
            const uint32_t* row = reinterpret_cast<const uint32_t*>(src + y * mapped.RowPitch);
            for (uint32_t x = 0; x < w; x++) {
                uint32_t p = row[x];
                // Simplified decode — extract mantissa+exponent per channel
                // Good enough for visualization
                float r = static_cast<float>(p & 0x7FF) / 2047.0f;
                float g = static_cast<float>((p >> 11) & 0x7FF) / 2047.0f;
                float b = static_cast<float>((p >> 22) & 0x3FF) / 1023.0f;
                rgba[(y * w + x) * 4 + 0] = static_cast<uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
                rgba[(y * w + x) * 4 + 1] = static_cast<uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
                rgba[(y * w + x) * 4 + 2] = static_cast<uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
                rgba[(y * w + x) * 4 + 3] = 255;
            }
        }
        break;
    }

    default:
        // Unknown format — fill with magenta to make it obvious
        for (uint32_t i = 0; i < w * h; i++) {
            rgba[i * 4 + 0] = 255; rgba[i * 4 + 1] = 0;
            rgba[i * 4 + 2] = 255; rgba[i * 4 + 3] = 255;
        }
        SKSE::log::warn("TextureDump: unsupported format {} — writing magenta",
                        static_cast<int>(desc.Format));
        break;
    }

    ctx->Unmap(staging, 0);
    staging->Release();

    return WriteBMP(filepath, rgba.data(), w, h, w * 4);
}

bool TextureDump::WriteBMP(const std::string& filepath,
                            const uint8_t* pixels, uint32_t w, uint32_t h,
                            uint32_t rowPitch)
{
    // BMP header (54 bytes)
    uint32_t dataSize = w * h * 4;
    uint32_t fileSize = 54 + dataSize;

    uint8_t header[54] = {};
    header[0] = 'B'; header[1] = 'M';
    std::memcpy(&header[2], &fileSize, 4);
    uint32_t dataOffset = 54;
    std::memcpy(&header[10], &dataOffset, 4);
    uint32_t dibSize = 40;
    std::memcpy(&header[14], &dibSize, 4);
    std::memcpy(&header[18], &w, 4);
    int32_t negH = -static_cast<int32_t>(h); // top-down
    std::memcpy(&header[22], &negH, 4);
    uint16_t planes = 1; std::memcpy(&header[26], &planes, 2);
    uint16_t bpp = 32; std::memcpy(&header[28], &bpp, 2);
    std::memcpy(&header[34], &dataSize, 4);

    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    file.write(reinterpret_cast<const char*>(header), 54);

    // BMP expects BGRA, our data is RGBA — swap R and B
    std::vector<uint8_t> bgra(w * 4);
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t* row = pixels + y * rowPitch;
        for (uint32_t x = 0; x < w; x++) {
            bgra[x * 4 + 0] = row[x * 4 + 2]; // B
            bgra[x * 4 + 1] = row[x * 4 + 1]; // G
            bgra[x * 4 + 2] = row[x * 4 + 0]; // R
            bgra[x * 4 + 3] = row[x * 4 + 3]; // A
        }
        file.write(reinterpret_cast<const char*>(bgra.data()), w * 4);
    }

    return file.good();
}

void TextureDump::DumpAllEffects(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                  uint32_t frameIndex)
{
    if (s_outputDir.empty()) {
        SKSE::log::warn("TextureDump: output directory not set");
        return;
    }

    SKSE::log::info("TextureDump: === Frame {} capture begin ===", frameIndex);

    // Depth
    auto* depthSRV = D3D11Hook::GetGameDepthSRV();
    if (depthSRV) SaveSRV(dev, ctx, depthSRV, "depth_raw", frameIndex);

    auto* linearSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    if (linearSRV) SaveSRV(dev, ctx, linearSRV, "depth_linear", frameIndex);

    auto* hizSRV = HiZPyramid::Get().GetSRV();
    if (hizSRV) SaveSRV(dev, ctx, hizSRV, "depth_hiz", frameIndex);

    // Effects
    auto* aoSRV = GTAORenderer::Get().GetOutputSRV();
    if (aoSRV) SaveSRV(dev, ctx, aoSRV, "GTAO_output", frameIndex);

    auto* csSRV = ContactShadowRenderer::Get().GetShadowSRV();
    if (csSRV) SaveSRV(dev, ctx, csSRV, "ContactShadow_output", frameIndex);

    auto* skySRV = SkylightingRenderer::Get().GetSkylightSRV();
    if (skySRV) SaveSRV(dev, ctx, skySRV, "Skylighting_output", frameIndex);

    auto* ssrSRV = SSRRenderer::Get().GetReflectionSRV();
    if (ssrSRV) SaveSRV(dev, ctx, ssrSRV, "SSR_output", frameIndex);

    auto* giSRV = SSGIRenderer::Get().GetGISRV();
    if (giSRV) SaveSRV(dev, ctx, giSRV, "SSGI_output", frameIndex);

    SKSE::log::info("TextureDump: === Frame {} capture end ===", frameIndex);
}

} // namespace SB
