#pragma once
//=============================================================================
//  MaterialClassifier — Per-pixel material ID classification buffer
//
//  During the game's geometry pass, writes a material ID to an auxiliary
//  render target based on which pixel shader hash is active. The resulting
//  classification buffer enables material-aware post-processing:
//    - Subsurface scattering (skin, snow, foliage)
//    - Wet surface darkening (stone, wood, ground during rain)
//    - Per-material motion blur and sharpening
//    - Material-aware GI and reflections
//
//  Material IDs:
//    0  = Unknown / unclassified
//    1  = Skin (character)
//    2  = Metal (armor, weapons, dwarven)
//    3  = Stone (architecture, cliffs, roads)
//    4  = Foliage (trees, grass, shrubs)
//    5  = Water (rivers, ocean, puddles)
//    6  = Snow (snow surfaces, frost)
//    7  = Glass (windows, ice, crystal)
//    8  = Fabric (clothing, banners, tents)
//    9  = Wood (structures, furniture)
//    10 = Terrain (landscape, ground)
//    11 = Emissive (fire, magic effects, glowing)
//    12 = Sky (atmosphere, clouds)
//    13-255 = Reserved for mod-added materials
//
//  The shader hash → material ID table is built by:
//    1. Automatic BSShader type classification (BSLightingShader subtypes)
//    2. Texture name heuristics (diffuse texture path keywords)
//    3. Manual overrides from INI file
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <string>

namespace SB
{

enum MaterialID : uint8_t
{
    MAT_UNKNOWN  = 0,
    MAT_SKIN     = 1,
    MAT_METAL    = 2,
    MAT_STONE    = 3,
    MAT_FOLIAGE  = 4,
    MAT_WATER    = 5,
    MAT_SNOW     = 6,
    MAT_GLASS    = 7,
    MAT_FABRIC   = 8,
    MAT_WOOD     = 9,
    MAT_TERRAIN  = 10,
    MAT_EMISSIVE = 11,
    MAT_SKY      = 12,
};

class MaterialClassifier
{
public:
    static MaterialClassifier& Get()
    {
        static MaterialClassifier inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                    uint32_t width, uint32_t height);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // ── Classification table ────────────────────────────────────────────

    // Register a shader hash → material ID mapping
    void RegisterShaderMaterial(uint64_t psHash, MaterialID mat);

    // Auto-classify a shader based on its bytecode features
    // (called from ShaderManager when a new PS is created)
    void AutoClassify(uint64_t psHash, const void* bytecode, size_t length);

    // Get material ID for a pixel shader hash (0 = unknown)
    MaterialID GetMaterial(uint64_t psHash) const;

    // ── Per-frame operations ────────────────────────────────────────────

    // Called at the start of the geometry pass to clear the material buffer
    void BeginFrame(ID3D11DeviceContext* ctx);

    // Called when a draw call happens — stamps current material ID
    // (The proxy's WrappedContext calls this from Draw* hooks)
    void OnDraw(uint64_t currentPSHash);

    // ── Resource access ─────────────────────────────────────────────────

    ID3D11ShaderResourceView*   GetMaterialSRV() const { return m_materialSRV; }
    ID3D11RenderTargetView*     GetMaterialRTV() const { return m_materialRTV; }
    ID3D11Texture2D*            GetMaterialTexture() const { return m_materialTex; }

    static constexpr uint32_t kMaterialSRVSlot = 25;  // t25

    // Stats
    uint32_t GetClassifiedShaderCount() const;
    uint32_t GetUnclassifiedDrawCount() const { return m_unclassifiedDraws; }

private:
    MaterialClassifier() = default;

    bool m_initialized = false;
    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Material ID render target (R8_UINT, same res as backbuffer)
    ID3D11Texture2D*            m_materialTex = nullptr;
    ID3D11RenderTargetView*     m_materialRTV = nullptr;
    ID3D11ShaderResourceView*   m_materialSRV = nullptr;

    // Shader hash → material ID classification table
    mutable std::mutex m_tableMutex;
    std::unordered_map<uint64_t, MaterialID> m_shaderTable;

    // Per-frame stats
    uint32_t m_unclassifiedDraws = 0;

    // Material stamping pixel shader (writes constant material ID to RT)
    ID3D11PixelShader* m_stampPS = nullptr;
    ID3D11Buffer*      m_stampCB = nullptr;

    bool CreateMaterialRT(uint32_t width, uint32_t height);
    bool CreateStampShader();
};

} // namespace SB
