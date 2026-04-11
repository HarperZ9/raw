#pragma once
//=============================================================================
//  LUTManager — Texture3D LUT injection for shader passes
//
//  Loads 8×8 tiled PNG atlas files (512×512 = 64³ volume) from disk,
//  creates ID3D11Texture3D resources, and binds the active LUT as SRV
//  at register(t18) with a trilinear sampler at register(s2).
//=============================================================================

#include <d3d11.h>
#include <string>
#include <vector>
#include <filesystem>

namespace SB
{
    struct LUTEntry
    {
        std::string name;           // Filename without extension
        ID3D11Texture3D* texture = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
    };

    class LUTManager
    {
    public:
        static LUTManager& Get()
        {
            static LUTManager instance;
            return instance;
        }

        bool Initialize(ID3D11Device* a_device, const std::filesystem::path& lutDir);
        void Shutdown();

        /// Get the currently active LUT SRV for shader binding (t18)
        ID3D11ShaderResourceView* GetActiveSRV() const;

        /// Get the trilinear sampler (s2)
        ID3D11SamplerState* GetSampler() const { return m_sampler; }

        // LUT management
        int GetLUTCount() const { return static_cast<int>(m_luts.size()); }
        const std::string& GetLUTName(int index) const { return m_luts[index].name; }
        int GetActiveIndex() const { return m_activeIndex; }
        void SetActiveIndex(int index);

        static constexpr uint32_t kSRVSlot = 18;
        static constexpr uint32_t kSamplerSlot = 2;

        bool IsInitialized() const { return m_initialized; }
        bool IsEnabled() const { return m_enabled && !m_luts.empty(); }
        void SetEnabled(bool a_enabled) { m_enabled = a_enabled; }

    private:
        LUTManager() = default;

        /// Load a single 8×8 tiled PNG atlas and convert to 64³ Texture3D
        bool LoadAtlasPNG(ID3D11Device* a_device, const std::filesystem::path& path);

        /// Decode PNG to RGBA pixels via stb_image
        static bool DecodePNG(const std::filesystem::path& path,
                              std::vector<uint8_t>& pixels, uint32_t& w, uint32_t& h);

        /// Rearrange 8×8 tiled atlas (512×512 RGBA) into 64×64×64 volume
        static bool AtlasToVolume(const uint8_t* atlas, uint32_t atlasW, uint32_t atlasH,
                                  std::vector<uint8_t>& volume);

        bool m_initialized = false;
        bool m_enabled = true;
        int m_activeIndex = 0;

        std::vector<LUTEntry> m_luts;
        ID3D11SamplerState* m_sampler = nullptr;
    };

} // namespace SB
