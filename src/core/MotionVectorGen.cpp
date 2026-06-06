//=============================================================================
//  MotionVectorGen.cpp — Depth-reprojection motion vector generation
//
//  Compute shader reads depth buffer + InvViewProj + PrevViewProj to produce
//  per-pixel screen-space motion vectors (R16G16_FLOAT).
//
//  Algorithm per pixel:
//    1. Load depth, reconstruct NDC position
//    2. Unproject to world space via InvViewProj
//    3. Reproject to previous frame via PrevViewProj
//    4. Motion = currentUV - previousUV
//=============================================================================

#include "MotionVectorGen.h"
#include "ShaderLoader.h"
#include "ComputeManager.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>
#include "GPUResource.h"

namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL compute shader
// ═══════════════════════════════════════════════════════════════════════════

static const char kMotionVectorCS[] = R"(
// Screen-space motion vector generation via depth reprojection
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Per pixel:
//   1. Load reversed-Z depth, reconstruct NDC position
//   2. Unproject to world space via InvViewProj
//   3. Reproject to previous frame via PrevViewProj
//   4. Motion vector = currentUV - previousUV
//
// Output: R16G16_FLOAT motion vectors in UV space.
// Sky pixels (reversed-Z depth < 0.00001) output zero motion.

cbuffer MotionVectorCB : register(b0)
{
    row_major float4x4 InvViewProj;
    row_major float4x4 PrevViewProj;
    uint2    Dimensions;
    float2   RcpDimensions;
}

Texture2D<float>     DepthBuffer   : register(t0);
RWTexture2D<float2>  MotionVectors : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= Dimensions))
        return;

    int2   pixelCoord = int2(dtid.xy);
    float2 uv = (float2(pixelCoord) + 0.5) * RcpDimensions;

    // Load reversed-Z depth (near=1, far=0)
    float depth = DepthBuffer.Load(int3(pixelCoord, 0));

    // Sky pixels: output zero motion (no meaningful reprojection)
    if (depth < 0.00001)
    {
        MotionVectors[dtid.xy] = float2(0.0, 0.0);
        return;
    }

    // Reconstruct clip-space position from UV + depth
    // UV -> NDC: x = uv.x * 2 - 1, y = (1 - uv.y) * 2 - 1
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float4 clipPos = float4(ndc, depth, 1.0);

    // Unproject to world space
    float4 worldPos = mul(clipPos, InvViewProj);
    worldPos /= worldPos.w;

    // Reproject to previous frame
    float4 prevClip = mul(worldPos, PrevViewProj);
    float2 prevNDC  = prevClip.xy / prevClip.w;

    // NDC -> UV: x = ndc.x * 0.5 + 0.5, y = 0.5 - ndc.y * 0.5
    float2 prevUV = float2(prevNDC.x * 0.5 + 0.5, 0.5 - prevNDC.y * 0.5);

    // Motion vector: current UV minus previous UV
    // Positive X = object moved right, positive Y = object moved down
    float2 motion = uv - prevUV;

    MotionVectors[dtid.xy] = motion;
}
)";


// ═══════════════════════════════════════════════════════════════════════════
//  Constant buffer layout (must match HLSL)
// ═══════════════════════════════════════════════════════════════════════════

struct alignas(16) MotionVectorCBData
{
    float    invViewProj[16];   // row_major float4x4 (64 bytes)
    float    prevViewProj[16];  // row_major float4x4 (64 bytes)
    uint32_t dimensions[2];     // uint2               (8 bytes)
    float    rcpDimensions[2];  // float2              (8 bytes)
};
static_assert(sizeof(MotionVectorCBData) == 144, "MotionVectorCBData must be 144 bytes");


// ═══════════════════════════════════════════════════════════════════════════
//  Initialization
// ═══════════════════════════════════════════════════════════════════════════

bool MotionVectorGen::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                  uint32_t width, uint32_t height)
{
    if (m_initialized) return true;
    if (!dev || !ctx || width == 0 || height == 0) return false;

    m_device  = dev;
    m_context = ctx;

    if (!CompileCS(dev)) {
        SKSE::log::error("MotionVectorGen: failed to compile compute shader");
        return false;
    }

    if (!CreateResources(dev, width, height)) {
        SKSE::log::error("MotionVectorGen: failed to create resources");
        Shutdown();
        return false;
    }

    m_initialized = true;
    SKSE::log::info("MotionVectorGen: initialized ({}x{}, R16G16_FLOAT)", width, height);
    return true;
}

void MotionVectorGen::Shutdown()
{
    ReleaseResources();
    if (m_cs) { m_cs->Release(); m_cs = nullptr; }
    m_initialized = false;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Compile compute shader
// ═══════════════════════════════════════════════════════════════════════════

bool MotionVectorGen::CompileCS(ID3D11Device* dev)
{
    ID3DBlob* blob = ShaderLoader::Compile("MotionVector_Main", kMotionVectorCS, "CSMain", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3);
    if (!blob) {
        SKSE::log::error("MotionVectorGen CS compile failed");
        return false;
    }

    HRESULT csHr = dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                             nullptr, &m_cs);
    blob->Release();
    return SUCCEEDED(csHr);
}


// ═══════════════════════════════════════════════════════════════════════════
//  Create / release GPU resources
// ═══════════════════════════════════════════════════════════════════════════

bool MotionVectorGen::CreateResources(ID3D11Device* dev, uint32_t w, uint32_t h)
{
    m_width  = w;
    m_height = h;

    // Motion vector texture: R16G16_FLOAT
    if (!CreateGPUTexture(dev, w, h, DXGI_FORMAT_R16G16_FLOAT,
                          &m_motionTex, &m_motionSRV, &m_motionUAV, "motionVec"))
        return false;

    // Constant buffer
    if (!CreateCB(dev, sizeof(MotionVectorCBData), &m_cb)) return false;

    return true;
}

void MotionVectorGen::ReleaseResources()
{
    if (m_cb)        { m_cb->Release();        m_cb = nullptr; }
    if (m_motionUAV) { m_motionUAV->Release(); m_motionUAV = nullptr; }
    if (m_motionSRV) { m_motionSRV->Release(); m_motionSRV = nullptr; }
    if (m_motionTex) { m_motionTex->Release(); m_motionTex = nullptr; }
    m_width = m_height = 0;
}

void MotionVectorGen::Resize(ID3D11Device* dev, uint32_t width, uint32_t height)
{
    if (!m_initialized || (width == m_width && height == m_height))
        return;
    ReleaseResources();
    if (CreateResources(dev, width, height))
        SKSE::log::info("MotionVectorGen: resized to {}x{}", width, height);
    else
        SKSE::log::error("MotionVectorGen: resize failed for {}x{}", width, height);
}


// ═══════════════════════════════════════════════════════════════════════════
//  Dispatch — generate motion vectors for current frame
// ═══════════════════════════════════════════════════════════════════════════

void MotionVectorGen::Dispatch(ID3D11DeviceContext* ctx,
                                ID3D11ShaderResourceView* depthSRV,
                                const float* invViewProj,
                                const float* prevViewProj)
{
    if (!m_initialized || !ctx || !depthSRV || !invViewProj || !prevViewProj)
        return;

    auto& cm = ComputeManager::Get();
    cm.SaveCSState();

    // Update constant buffer
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = ctx->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            auto* data = static_cast<MotionVectorCBData*>(mapped.pData);
            std::memcpy(data->invViewProj,  invViewProj,  16 * sizeof(float));
            std::memcpy(data->prevViewProj, prevViewProj, 16 * sizeof(float));
            data->dimensions[0]    = m_width;
            data->dimensions[1]    = m_height;
            data->rcpDimensions[0] = 1.0f / static_cast<float>(m_width);
            data->rcpDimensions[1] = 1.0f / static_cast<float>(m_height);
            ctx->Unmap(m_cb, 0);
        }
    }

    // Bind
    ctx->CSSetShader(m_cs, nullptr, 0);

    ID3D11ShaderResourceView* srvs[1] = { depthSRV };
    ctx->CSSetShaderResources(0, 1, srvs);

    ID3D11UnorderedAccessView* uavs[1] = { m_motionUAV };
    ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

    ID3D11Buffer* cbs[1] = { m_cb };
    ctx->CSSetConstantBuffers(0, 1, cbs);

    // Dispatch (8x8 thread groups)
    UINT groupsX = (m_width  + 7) / 8;
    UINT groupsY = (m_height + 7) / 8;
    ctx->Dispatch(groupsX, groupsY, 1);

    // Unbind
    ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
    ctx->CSSetShaderResources(0, 1, nullSRV);
    ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
    ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

    cm.RestoreCSState();
}

} // namespace SB
