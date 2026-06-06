//=============================================================================
//  MaterialClassifier.cpp — Per-pixel material classification buffer
//=============================================================================

#include "MaterialClassifier.h"
#include "ShaderLoader.h"
#include "SRVInjector.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>

namespace SB
{

// ── Material stamp pixel shader ───────────────────────────────────────────
// Writes a constant material ID to the classification RT.
// Bound as an additional MRT output alongside the game's normal RTs.
static const char kStampPS[] = R"HLSL(
cbuffer StampCB : register(b7)
{
    uint MaterialID;
    uint pad0, pad1, pad2;
};

float4 main() : SV_Target
{
    return float4(float(MaterialID) / 255.0, 0, 0, 1);
}
)HLSL";

// ── Initialize ────────────────────────────────────────────────────────────

bool MaterialClassifier::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                     uint32_t width, uint32_t height)
{
    if (m_initialized) return true;
    m_device = dev;
    m_context = ctx;

    if (!CreateMaterialRT(width, height)) {
        SKSE::log::error("MaterialClassifier: failed to create material RT");
        return false;
    }

    if (!CreateStampShader()) {
        SKSE::log::error("MaterialClassifier: failed to create stamp shader");
        return false;
    }

    // Register SRV for injection
    SRVInjector::Get().RegisterSRV(kMaterialSRVSlot, m_materialSRV);

    // Seed classification table with known BSShader types
    // These hashes will be populated at runtime as shaders are created.
    // The auto-classification system examines bytecode signatures.

    m_initialized = true;
    SKSE::log::info("MaterialClassifier: initialized ({}x{} R8_UINT at t{})",
        width, height, kMaterialSRVSlot);
    return true;
}

void MaterialClassifier::Shutdown()
{
    auto SafeRelease = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    SafeRelease(m_materialTex);
    SafeRelease(m_materialRTV);
    SafeRelease(m_materialSRV);
    SafeRelease(m_stampPS);
    SafeRelease(m_stampCB);
    m_initialized = false;
}

bool MaterialClassifier::CreateMaterialRT(uint32_t width, uint32_t height)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM;  // 0-255 material IDs, normalized
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &m_materialTex)))
        return false;

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = desc.Format;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    if (FAILED(m_device->CreateRenderTargetView(m_materialTex, &rtvDesc, &m_materialRTV)))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    if (FAILED(m_device->CreateShaderResourceView(m_materialTex, &srvDesc, &m_materialSRV)))
        return false;

    return true;
}

bool MaterialClassifier::CreateStampShader()
{
    ID3DBlob* blob = ShaderLoader::Compile("Material_Stamp", kStampPS, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3);
    if (!blob) {
        SKSE::log::error("MaterialClassifier stamp PS compile failed");
        return false;
    }

    HRESULT hr = m_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                     nullptr, &m_stampPS);
    blob->Release();
    if (FAILED(hr)) return false;

    // Create stamp CB
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = 16;
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(m_device->CreateBuffer(&cbDesc, nullptr, &m_stampCB));
}

// ── Classification table ──────────────────────────────────────────────────

void MaterialClassifier::RegisterShaderMaterial(uint64_t psHash, MaterialID mat)
{
    std::lock_guard lock(m_tableMutex);
    m_shaderTable[psHash] = mat;
}

void MaterialClassifier::AutoClassify(uint64_t psHash, const void* bytecode, size_t length)
{
    // Examine DXBC bytecode for signatures that indicate material type.
    // BSLightingShader subtypes have distinctive constant buffer layouts:
    //   - Skin: SubSurfaceRolloff CB member
    //   - Metal: SpecularColor with high values
    //   - Foliage: TreeParams CB
    //   - Terrain: LandscapeTexture* bindings
    //
    // For now, use bytecode length as a rough heuristic (to be refined):
    // BSLightingShader variants have characteristic sizes.
    // This will be replaced with proper DXBC reflection.

    if (!bytecode || length == 0) return;

    // Check if already classified
    {
        std::lock_guard lock(m_tableMutex);
        if (m_shaderTable.count(psHash)) return;
    }

    // Default: don't classify unknown shaders
    // Classification will be populated by:
    // 1. Runtime analysis of CB layouts via DXBC reflection
    // 2. Manual hash → material mapping from config INI
    // 3. SceneObserver BSShader type hooks (SetupMaterial callbacks)
}

MaterialID MaterialClassifier::GetMaterial(uint64_t psHash) const
{
    std::lock_guard lock(m_tableMutex);
    auto it = m_shaderTable.find(psHash);
    return (it != m_shaderTable.end()) ? it->second : MAT_UNKNOWN;
}

// ── Per-frame operations ──────────────────────────────────────────────────

void MaterialClassifier::BeginFrame(ID3D11DeviceContext* ctx)
{
    if (!m_initialized) return;
    m_unclassifiedDraws = 0;

    // Clear material RT to 0 (unknown)
    float clearColor[4] = { 0, 0, 0, 0 };
    ctx->ClearRenderTargetView(m_materialRTV, clearColor);
}

void MaterialClassifier::OnDraw(uint64_t currentPSHash)
{
    if (!m_initialized) return;

    MaterialID mat = GetMaterial(currentPSHash);
    if (mat == MAT_UNKNOWN) {
        m_unclassifiedDraws++;
    }

    // The actual material ID stamping happens in the proxy's WrappedContext.
    // When OMSetRenderTargets is called for the main geometry pass, the
    // proxy appends our material RTV as an additional MRT output.
    // The game's pixel shader writes to SV_Target0 (color), and our
    // stamp PS (or a modified version of the game's PS) writes to
    // SV_Target1 (material ID).
    //
    // Alternative approach: Use a fullscreen classify pass that reads
    // depth + stencil to assign materials based on rendering order.
    // This is simpler but less accurate.
}

uint32_t MaterialClassifier::GetClassifiedShaderCount() const
{
    std::lock_guard lock(m_tableMutex);
    return static_cast<uint32_t>(m_shaderTable.size());
}

} // namespace SB
