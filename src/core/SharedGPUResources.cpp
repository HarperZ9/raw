//=============================================================================
//  SharedGPUResources.cpp — Blue noise, linearized depth, vanilla params CB
//=============================================================================

#include "SharedGPUResources.h"
#include "HiZPyramid.h"
#include "SRVInjector.h"
#include "ImageSpaceTracker.h"
#include "SceneData.h"
#include "D3D11Hook.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>

namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════
//  Depth linearization compute shader
// ═══════════════════════════════════════════════════════════════════════════

static const char kLinearizeDepthCS[] = R"HLSL(
// Linearize depth buffer into view-space Z (R32_FLOAT).
// Reads from HiZ mip 0 (reversed-Z after CSCopy conversion), writes to linear depth.
// Input: reversed-Z (near=1, far=0). HiZ CSCopy converts Skyrim's standard depth.

Texture2D<float>    RawDepth    : register(t0);
RWTexture2D<float>  LinearDepth : register(u0);

cbuffer LinearizeParams : register(b0)
{
    uint2 Dims;
    float NearZ;
    float FarZ;
};

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= Dims.x || DTid.y >= Dims.y)
        return;

    float z = RawDepth[DTid.xy];

    // Skyrim reversed-Z: z=1 near, z=0 far
    // Sky pixels (z ≈ 0) → output FarZ (max distance)
    float viewZ = (z < 0.00001)
        ? FarZ
        : NearZ * FarZ / (NearZ + z * (FarZ - NearZ));

    LinearDepth[DTid.xy] = viewZ;
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  R2 quasi-random sequence — approaches blue noise properties
//  Reference: Roberts, "The Unreasonable Effectiveness of Quasirandom
//  Sequences" (2018).  Uses the generalized golden ratio (plastic constant)
//  for 2D: phi2 = 1.32471795724..., alpha1 = 1/phi2, alpha2 = 1/phi2^2.
// ═══════════════════════════════════════════════════════════════════════════

static constexpr float kPhi2    = 1.32471795724474602596f;
static constexpr float kAlpha1  = 1.0f / kPhi2;           // 0.7548776...
static constexpr float kAlpha2  = 1.0f / (kPhi2 * kPhi2); // 0.5698402...

static float Frac(float x)
{
    return x - std::floor(x);
}


// ═══════════════════════════════════════════════════════════════════════════
//  Initialize
// ═══════════════════════════════════════════════════════════════════════════

bool SharedGPUResources::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                     IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    m_device  = dev;
    m_context = ctx;

    // Get backbuffer dimensions
    ID3D11Texture2D* backTex = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                              reinterpret_cast<void**>(&backTex))))
        return false;
    D3D11_TEXTURE2D_DESC bbDesc;
    backTex->GetDesc(&bbDesc);
    backTex->Release();

    m_width  = bbDesc.Width;
    m_height = bbDesc.Height;

    if (!CreateBlueNoiseTexture(dev))       return false;
    if (!CompileLinearizeCS(dev))            return false;
    if (!CreateLinearDepthResources(dev, m_width, m_height)) return false;
    if (!CreateVanillaParamsCB(dev))         return false;

    m_initialized = true;
    SKSE::log::info("SharedGPUResources: initialized — blue noise at t{}, "
                    "linear depth at t{} ({}x{}), vanilla params at b{}",
                    kBlueNoiseSlot, kLinearDepthSlot, m_width, m_height,
                    kVanillaParamsCBSlot);
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Blue noise — 128x128 R8G8B8A8_UNORM, R2 quasi-random, 4 channels
// ═══════════════════════════════════════════════════════════════════════════

bool SharedGPUResources::CreateBlueNoiseTexture(ID3D11Device* dev)
{
    constexpr uint32_t kSize = 128;

    // Generate CPU-side pixel data
    std::vector<uint8_t> pixels(kSize * kSize * 4);

    for (uint32_t y = 0; y < kSize; ++y) {
        for (uint32_t x = 0; x < kSize; ++x) {
            uint32_t idx = (y * kSize + x) * 4;
            // 4 decorrelated channels using R2 with different seeds
            // Channel n uses seed offset n*17 for decorrelation
            pixels[idx + 0] = static_cast<uint8_t>(
                Frac(0.5f + kAlpha1 * (x + 0)  + kAlpha2 * (y + 0))  * 255.0f);
            pixels[idx + 1] = static_cast<uint8_t>(
                Frac(0.5f + kAlpha1 * (x + 17) + kAlpha2 * (y + 31)) * 255.0f);
            pixels[idx + 2] = static_cast<uint8_t>(
                Frac(0.5f + kAlpha1 * (x + 59) + kAlpha2 * (y + 73)) * 255.0f);
            pixels[idx + 3] = static_cast<uint8_t>(
                Frac(0.5f + kAlpha1 * (x + 97) + kAlpha2 * (y + 113)) * 255.0f);
        }
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width      = kSize;
    desc.Height     = kSize;
    desc.MipLevels  = 1;
    desc.ArraySize  = 1;
    desc.Format     = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc = {1, 0};
    desc.Usage      = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem     = pixels.data();
    initData.SysMemPitch = kSize * 4;

    HRESULT hr = dev->CreateTexture2D(&desc, &initData, &m_blueNoiseTex);
    if (FAILED(hr)) {
        SKSE::log::error("SharedGPUResources: blue noise texture creation failed (0x{:X})",
                         static_cast<uint32_t>(hr));
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels       = 1;
    hr = dev->CreateShaderResourceView(m_blueNoiseTex, &srvDesc, &m_blueNoiseSRV);
    if (FAILED(hr)) return false;

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Linearized depth — R32_FLOAT full-res, compute from HiZ mip 0
// ═══════════════════════════════════════════════════════════════════════════

bool SharedGPUResources::CompileLinearizeCS(ID3D11Device* dev)
{
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* blob = nullptr;
    ID3DBlob* err  = nullptr;
    HRESULT hr = D3DCompile(kLinearizeDepthCS, strlen(kLinearizeDepthCS),
                            "SB_LinearizeDepthCS", nullptr, nullptr,
                            "main", "cs_5_0", flags, 0, &blob, &err);
    if (FAILED(hr)) {
        if (err) {
            SKSE::log::error("SharedGPUResources: linearize CS compile failed: {}",
                             static_cast<const char*>(err->GetBufferPointer()));
            err->Release();
        }
        return false;
    }
    if (err) err->Release();

    hr = dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                   nullptr, &m_linearizeCS);
    blob->Release();
    return SUCCEEDED(hr);
}

bool SharedGPUResources::CreateLinearDepthResources(ID3D11Device* dev,
                                                      uint32_t w, uint32_t h)
{
    HRESULT hr;

    // Full-res R32_FLOAT texture
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width      = w;
    desc.Height     = h;
    desc.MipLevels  = 1;
    desc.ArraySize  = 1;
    desc.Format     = DXGI_FORMAT_R32_FLOAT;
    desc.SampleDesc = {1, 0};
    desc.Usage      = D3D11_USAGE_DEFAULT;
    desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    hr = dev->CreateTexture2D(&desc, nullptr, &m_linearDepthTex);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels       = 1;
    hr = dev->CreateShaderResourceView(m_linearDepthTex, &srvDesc, &m_linearDepthSRV);
    if (FAILED(hr)) return false;

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format             = DXGI_FORMAT_R32_FLOAT;
    uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    hr = dev->CreateUnorderedAccessView(m_linearDepthTex, &uavDesc, &m_linearDepthUAV);
    if (FAILED(hr)) return false;

    // Params CB (16 bytes: uint2 dims + float near + float far)
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth      = 16;
    cbDesc.Usage           = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
    hr = dev->CreateBuffer(&cbDesc, nullptr, &m_linearizeCB);
    return SUCCEEDED(hr);
}


// ═══════════════════════════════════════════════════════════════════════════
//  Vanilla params constant buffer
// ═══════════════════════════════════════════════════════════════════════════

bool SharedGPUResources::CreateVanillaParamsCB(ID3D11Device* dev)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth      = sizeof(VanillaParamsCBData);
    desc.Usage           = D3D11_USAGE_DYNAMIC;
    desc.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(dev->CreateBuffer(&desc, nullptr, &m_vanillaParamsCB));
}


// ═══════════════════════════════════════════════════════════════════════════
//  Per-frame update — linearize depth + upload vanilla params
// ═══════════════════════════════════════════════════════════════════════════

void SharedGPUResources::Update(ID3D11DeviceContext* ctx)
{
    if (!m_initialized) return;

    auto& scene = SceneMatrices::Get();
    if (!scene.IsValid()) {
        static int s_invalidLog = 0;
        if (s_invalidLog++ < 5) SKSE::log::warn("SharedGPUResources: SceneMatrices invalid — skipping linearize");
        return;
    }

    // ── 1. Linearize depth ──────────────────────────────────────────────
    // Read from HiZ pyramid mip 0 (raw reversed-Z depth built at PostGeometry:1)
    auto& hiz = HiZPyramid::Get();
    ID3D11ShaderResourceView* depthSRV = nullptr;
    const char* depthSource = "NONE";
    if (hiz.IsInitialized() && hiz.GetSRV()) {
        depthSRV = hiz.GetSRV();
        depthSource = "HiZ";
    } else {
        depthSRV = D3D11Hook::GetGameDepthSRV();
        depthSource = depthSRV ? "GameDepth" : "NONE";
    }

    // Diagnostic: log depth source and params periodically
    {
        static uint32_t s_diagFrame = 0;
        if (s_diagFrame++ % 300 == 0) {
            SKSE::log::info("SharedGPURes[f{}]: depthSRV={} src={} near={:.1f} far={:.1f} dims={}x{}",
                s_diagFrame, (void*)depthSRV, depthSource,
                scene.NearClip(), scene.FarClip(), m_width, m_height);
        }
    }

    if (depthSRV) {
        // Save CS state
        ID3D11ComputeShader* prevCS = nullptr;
        ID3D11ShaderResourceView* prevSRV = nullptr;
        ID3D11UnorderedAccessView* prevUAV = nullptr;
        ID3D11Buffer* prevCB = nullptr;
        ctx->CSGetShader(&prevCS, nullptr, nullptr);
        ctx->CSGetShaderResources(0, 1, &prevSRV);
        ctx->CSGetUnorderedAccessViews(0, 1, &prevUAV);
        ctx->CSGetConstantBuffers(0, 1, &prevCB);

        // Update params
        struct { uint32_t w, h; float nearZ, farZ; } params = {
            m_width, m_height, scene.NearClip(), scene.FarClip()
        };
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx->Map(m_linearizeCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &params, sizeof(params));
            ctx->Unmap(m_linearizeCB, 0);
        }

        // Dispatch
        ctx->CSSetShader(m_linearizeCS, nullptr, 0);
        ctx->CSSetShaderResources(0, 1, &depthSRV);
        ctx->CSSetUnorderedAccessViews(0, 1, &m_linearDepthUAV, nullptr);
        ctx->CSSetConstantBuffers(0, 1, &m_linearizeCB);
        ctx->Dispatch((m_width + 7) / 8, (m_height + 7) / 8, 1);

        // Cleanup
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ID3D11UnorderedAccessView* nullUAV = nullptr;
        ID3D11Buffer* nullCB = nullptr;
        ctx->CSSetShaderResources(0, 1, &nullSRV);
        ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
        ctx->CSSetConstantBuffers(0, 1, &nullCB);
        ctx->CSSetShader(nullptr, nullptr, 0);

        // Restore CS state
        ctx->CSSetShader(prevCS, nullptr, 0);
        ctx->CSSetShaderResources(0, 1, &prevSRV);
        ctx->CSSetUnorderedAccessViews(0, 1, &prevUAV, nullptr);
        ctx->CSSetConstantBuffers(0, 1, &prevCB);
        if (prevCS)  prevCS->Release();
        if (prevSRV) prevSRV->Release();
        if (prevUAV) prevUAV->Release();
        if (prevCB)  prevCB->Release();
    } else {
        static int s_noDepthLog = 0;
        if (s_noDepthLog++ < 10)
            SKSE::log::warn("SharedGPUResources: NO depth SRV — linearization skipped (HiZ init={} srv={}, gameDepth={})",
                hiz.IsInitialized(), (void*)hiz.GetSRV(), (void*)D3D11Hook::GetGameDepthSRV());
    }

    // ── 2. Upload vanilla params ────────────────────────────────────────
    {
        auto isData = ImageSpaceTracker::Update();

        VanillaParamsCBData cb = {};
        cb.eyeAdaptSpeed  = isData.HDR.x;
        cb.bloomScale     = isData.HDR.y;
        cb.bloomThreshold = isData.HDR.z;
        cb.sunlightScale  = isData.HDR.w;
        cb.saturation     = isData.Cinematic.x;
        cb.brightness     = isData.Cinematic.y;
        cb.contrast       = isData.Cinematic.z;
        cb.tintAmount     = isData.Cinematic.w;
        cb.tintR          = isData.CineTint.x;
        cb.tintG          = isData.CineTint.y;
        cb.tintB          = isData.CineTint.z;
        cb.dofStrength    = isData.DOF.x;
        cb.dofDistance     = isData.DOF.y;
        cb.dofRange       = isData.DOF.z;
        cb.imodActive     = isData.IMOD.x;
        cb.imodStrength   = isData.IMOD.y;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx->Map(m_vanillaParamsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx->Unmap(m_vanillaParamsCB, 0);
        }

        // Bind at PS b7 (available to all subsequent pixel shaders this frame)
        ctx->PSSetConstantBuffers(kVanillaParamsCBSlot, 1, &m_vanillaParamsCB);
    }
}


// ═══════════════════════════════════════════════════════════════════════════
//  Shutdown
// ═══════════════════════════════════════════════════════════════════════════

void SharedGPUResources::Shutdown()
{
    auto SafeRelease = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };

    SafeRelease(m_blueNoiseTex);
    SafeRelease(m_blueNoiseSRV);
    SafeRelease(m_linearDepthTex);
    SafeRelease(m_linearDepthSRV);
    SafeRelease(m_linearDepthUAV);
    SafeRelease(m_linearizeCS);
    SafeRelease(m_linearizeCB);
    SafeRelease(m_vanillaParamsCB);

    m_initialized = false;
}

} // namespace SB
