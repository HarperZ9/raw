#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../vendor/stb_image.h"

#include "LUTManager.h"

#include <d3d11.h>
#include <cstring>
#include <algorithm>

#include <SKSE/SKSE.h>

namespace SB
{
    // ── Initialization ────────────────────────────────────────────────────

    bool LUTManager::Initialize(ID3D11Device* a_device, const std::filesystem::path& lutDir)
    {
        if (m_initialized) return true;
        if (!a_device) return false;

        // Create trilinear sampler at s2
        D3D11_SAMPLER_DESC sampDesc{};
        sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

        HRESULT hr = a_device->CreateSamplerState(&sampDesc, &m_sampler);
        if (FAILED(hr)) {
            SKSE::log::error("LUTManager: failed to create trilinear sampler");
            return false;
        }

        // Scan LUT directory for PNG atlases
        std::error_code ec;
        if (!std::filesystem::exists(lutDir, ec)) {
            std::filesystem::create_directories(lutDir, ec);
            SKSE::log::info("LUTManager: created LUT directory: {}", lutDir.string());
        }

        for (auto& entry : std::filesystem::directory_iterator(lutDir, ec)) {
            if (entry.path().extension() == ".png") {
                if (LoadAtlasPNG(a_device, entry.path())) {
                    SKSE::log::info("LUTManager: loaded LUT '{}'", entry.path().stem().string());
                } else {
                    SKSE::log::warn("LUTManager: failed to load '{}'", entry.path().string());
                }
            }
        }

        m_initialized = true;
        SKSE::log::info("LUTManager: initialized — {} LUTs loaded, SRV at t{}, sampler at s{}",
            m_luts.size(), kSRVSlot, kSamplerSlot);
        return true;
    }

    void LUTManager::Shutdown()
    {
        for (auto& lut : m_luts) {
            if (lut.srv) lut.srv->Release();
            if (lut.texture) lut.texture->Release();
        }
        m_luts.clear();

        if (m_sampler) {
            m_sampler->Release();
            m_sampler = nullptr;
        }

        m_initialized = false;
    }

    // ── LUT access ────────────────────────────────────────────────────────

    ID3D11ShaderResourceView* LUTManager::GetActiveSRV() const
    {
        if (m_luts.empty() || m_activeIndex < 0 ||
            m_activeIndex >= static_cast<int>(m_luts.size()))
            return nullptr;
        return m_luts[m_activeIndex].srv;
    }

    void LUTManager::SetActiveIndex(int index)
    {
        if (index >= 0 && index < static_cast<int>(m_luts.size()))
            m_activeIndex = index;
    }

    // ── PNG decoding ──────────────────────────────────────────────────────

    bool LUTManager::DecodePNG(const std::filesystem::path& path,
                                std::vector<uint8_t>& pixels, uint32_t& w, uint32_t& h)
    {
        int iw, ih, channels;
        uint8_t* data = stbi_load(path.string().c_str(), &iw, &ih, &channels, 4);
        if (!data) {
            SKSE::log::error("LUTManager: stbi_load failed for '{}'", path.string());
            return false;
        }

        w = static_cast<uint32_t>(iw);
        h = static_cast<uint32_t>(ih);
        pixels.assign(data, data + w * h * 4);
        stbi_image_free(data);
        return true;
    }

    // ── Atlas → Volume conversion ─────────────────────────────────────────

    bool LUTManager::AtlasToVolume(const uint8_t* atlas, uint32_t atlasW, uint32_t atlasH,
                                    std::vector<uint8_t>& volume)
    {
        constexpr int kLUTSize = 64;
        constexpr int kTilesPerRow = 8;
        constexpr int kBPP = 4;

        if (atlasW != kLUTSize * kTilesPerRow || atlasH != kLUTSize * kTilesPerRow) {
            SKSE::log::error("LUTManager: expected 512x512 atlas, got {}x{}", atlasW, atlasH);
            return false;
        }

        volume.resize(kLUTSize * kLUTSize * kLUTSize * kBPP);

        for (int z = 0; z < kLUTSize; z++) {
            int tileCol = z % kTilesPerRow;
            int tileRow = z / kTilesPerRow;
            int tileX = tileCol * kLUTSize;
            int tileY = tileRow * kLUTSize;

            for (int y = 0; y < kLUTSize; y++) {
                for (int x = 0; x < kLUTSize; x++) {
                    int srcIdx = ((tileY + y) * atlasW + (tileX + x)) * kBPP;
                    int dstIdx = (z * kLUTSize * kLUTSize + y * kLUTSize + x) * kBPP;
                    std::memcpy(&volume[dstIdx], &atlas[srcIdx], kBPP);
                }
            }
        }
        return true;
    }

    // ── Texture3D creation ────────────────────────────────────────────────

    bool LUTManager::LoadAtlasPNG(ID3D11Device* a_device, const std::filesystem::path& path)
    {
        std::vector<uint8_t> pixels;
        uint32_t w, h;
        if (!DecodePNG(path, pixels, w, h)) return false;

        std::vector<uint8_t> volume;
        if (!AtlasToVolume(pixels.data(), w, h, volume)) return false;

        constexpr int kLUTSize = 64;

        D3D11_TEXTURE3D_DESC desc{};
        desc.Width = kLUTSize;
        desc.Height = kLUTSize;
        desc.Depth = kLUTSize;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initData{};
        initData.pSysMem = volume.data();
        initData.SysMemPitch = kLUTSize * 4;               // row pitch
        initData.SysMemSlicePitch = kLUTSize * kLUTSize * 4; // slice pitch

        LUTEntry entry;
        entry.name = path.stem().string();

        HRESULT hr = a_device->CreateTexture3D(&desc, &initData, &entry.texture);
        if (FAILED(hr)) {
            SKSE::log::error("LUTManager: CreateTexture3D failed (hr=0x{:X})",
                static_cast<unsigned>(hr));
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MipLevels = 1;
        srvDesc.Texture3D.MostDetailedMip = 0;

        hr = a_device->CreateShaderResourceView(entry.texture, &srvDesc, &entry.srv);
        if (FAILED(hr)) {
            entry.texture->Release();
            SKSE::log::error("LUTManager: CreateSRV failed (hr=0x{:X})",
                static_cast<unsigned>(hr));
            return false;
        }

        m_luts.push_back(std::move(entry));
        return true;
    }

} // namespace SB
