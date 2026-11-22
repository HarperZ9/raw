//=============================================================================
//  OcclusionCuller.cpp — CPU-side GPU occlusion culling
//
//  Per-frame flow:
//    Present:    Copy depth → staging[writeIdx], Map staging[readIdx] → CPU Hi-Z
//    DrawIndexed: Extract world pos from VS CB slot 2, project bounding sphere,
//                 test against CPU Hi-Z, skip if fully behind closest depth
//=============================================================================

#include "OcclusionCuller.h"
#include "CBDirtyTracker.h"
#include "ProxyAPI.h"
#include "ProxyLog.h"
#include "RenderPhaseDetector.h"
#include "SceneLights.h"

#include <cstring>
#include <cmath>
#include <algorithm>

namespace SB::Proxy
{

// ═══════════════════════════════════════════════════════════════════════════
//  Initialize
// ═══════════════════════════════════════════════════════════════════════════

bool OcclusionCuller::Initialize(ID3D11Device* device, ID3D11DeviceContext* ctx)
{
    if (m_initialized) return true;
    if (!device || !ctx) return false;

    m_device  = device;
    m_context = ctx;

    // We defer staging texture creation until the first Present,
    // when we know the depth buffer dimensions.
    m_initialized = true;
    return true;
}

void OcclusionCuller::Shutdown()
{
    for (auto& stg : m_depthStaging) {
        if (stg) { stg->Release(); stg = nullptr; }
    }
    m_initialized = false;
    m_hizReady = false;
}


// ═══════════════════════════════════════════════════════════════════════════
//  OnPresent — Depth readback + CPU Hi-Z build
// ═══════════════════════════════════════════════════════════════════════════

void OcclusionCuller::OnPresent(ID3D11DeviceContext* ctx)
{
    if (!m_initialized || !m_enabled) return;

    // Periodic live CPU/GPU draw & triangle savings telemetry (every 300 frames).
    if (++m_frameCount % 300 == 0) {
        Log("[OcclusionCuller] frame %llu: hiz=%d tested %u draws (%llu tris), culled %u draws (%llu tris: frustum=%u subpx=%u hiz=%u, %.1f%% tris saved)",
            static_cast<unsigned long long>(m_frameCount), m_hizReady ? 1 : 0, m_drawsTested, static_cast<unsigned long long>(m_trisTested),
            m_drawsCulled, static_cast<unsigned long long>(m_trisCulled), m_frustumCulled, m_subPxCulled, m_hiZCulled,
            m_trisTested ? (100.0 * double(m_trisCulled) / double(m_trisTested)) : 0.0);
    }

    // Reset per-frame stats
    m_drawsTested   = 0;
    m_drawsCulled   = 0;
    m_frustumCulled = 0;
    m_subPxCulled   = 0;
    m_hiZCulled     = 0;
    m_trisTested    = 0;
    m_trisCulled    = 0;

    // ── Get the game's depth buffer ─────────────────────────────────────
    auto& rpd = RenderPhaseDetector::Get();
    ID3D11DepthStencilView* mainDSV = rpd.GetMainDSV();
    if (!mainDSV) {
        if (auto* pi = PG_GetProxyInterface()) mainDSV = pi->gameDepthDSV;
    }
    if (!mainDSV) return;

    ID3D11Resource* dsResource = nullptr;
    mainDSV->GetResource(&dsResource);
    if (!dsResource) return;

    ID3D11Texture2D* depthTex = nullptr;
    HRESULT hr = dsResource->QueryInterface(__uuidof(ID3D11Texture2D),
                                             reinterpret_cast<void**>(&depthTex));
    dsResource->Release();
    if (FAILED(hr) || !depthTex) return;

    D3D11_TEXTURE2D_DESC depthDesc;
    depthTex->GetDesc(&depthDesc);

    m_screenW = depthDesc.Width;
    m_screenH = depthDesc.Height;

    // ── Create staging textures matching the game's depth format (R24G8_TYPELESS or R32_TYPELESS/R32_FLOAT) ─
    const bool supportedFmt = (depthDesc.Format == DXGI_FORMAT_R24G8_TYPELESS ||
                               depthDesc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
                               depthDesc.Format == DXGI_FORMAT_R32_TYPELESS ||
                               depthDesc.Format == DXGI_FORMAT_R32_FLOAT ||
                               depthDesc.Format == DXGI_FORMAT_D32_FLOAT);
    if (!supportedFmt) {
        depthTex->Release();
        return;
    }

    if (!m_depthStaging[0] || m_depthW != depthDesc.Width || m_depthH != depthDesc.Height || m_stagingFmt != depthDesc.Format)
    {
        for (auto& stg : m_depthStaging) {
            if (stg) { stg->Release(); stg = nullptr; }
        }

        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width      = depthDesc.Width;
        stagingDesc.Height     = depthDesc.Height;
        stagingDesc.MipLevels  = 1;
        stagingDesc.ArraySize  = 1;
        stagingDesc.Format     = depthDesc.Format;
        stagingDesc.SampleDesc = {1, 0};
        stagingDesc.Usage      = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        for (int i = 0; i < 2; i++) {
            hr = m_device->CreateTexture2D(&stagingDesc, nullptr, &m_depthStaging[i]);
            if (FAILED(hr)) {
                depthTex->Release();
                return;
            }
        }

        m_depthW = depthDesc.Width;
        m_depthH = depthDesc.Height;
        m_stagingFmt = depthDesc.Format;
        m_hizReady = false;
        m_hizHasCam = false;
        m_stageHasCam[0] = m_stageHasCam[1] = false;
    }

    // ── Copy depth to staging[writeIdx] and record its exact camera matrix ──
    int writeIdx = m_readbackIdx;
    int readIdx  = 1 - m_readbackIdx;

    ctx->CopyResource(m_depthStaging[writeIdx], depthTex);
    depthTex->Release();
    {
        const auto& sl = SceneLights_Frame();
        m_stageHasCam[writeIdx] = sl.hasCamera;
        if (sl.hasCamera) {
            std::memcpy(m_stageW2C[writeIdx], sl.worldToClip, sizeof(m_stageW2C[writeIdx]));
            std::memcpy(m_stageCam[writeIdx], sl.posAdjust, sizeof(m_stageCam[writeIdx]));
        }
    }

    // ── Read back staging[readIdx] from previous frame ──────────────────
    if (m_depthStaging[readIdx] && m_hizReady && m_stageHasCam[readIdx])
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = ctx->Map(m_depthStaging[readIdx], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (SUCCEEDED(hr))
        {
            BuildCPUHiZ(static_cast<const uint8_t*>(mapped.pData),
                        m_depthW, m_depthH, mapped.RowPitch, m_stagingFmt);
            std::memcpy(m_hizW2C, m_stageW2C[readIdx], sizeof(m_hizW2C));
            std::memcpy(m_hizCam, m_stageCam[readIdx], sizeof(m_hizCam));
            m_hizHasCam = true;
            ctx->Unmap(m_depthStaging[readIdx], 0);
        }
    }
    else if (!m_hizReady)
    {
        m_hizReady = true;
    }

    // Swap double buffer
    m_readbackIdx = readIdx;
}


// ═══════════════════════════════════════════════════════════════════════════
//  BuildCPUHiZ — Construct a Standard-Z (0=near, 1=far) MAX Hi-Z pyramid
// ═══════════════════════════════════════════════════════════════════════════

void OcclusionCuller::BuildCPUHiZ(const uint8_t* rawData, uint32_t width, uint32_t height, uint32_t rowPitchBytes, DXGI_FORMAT fmt)
{
    constexpr uint32_t kSubsample = 4;
    const uint32_t mip0W = (width + kSubsample - 1) / kSubsample;
    const uint32_t mip0H = (height + kSubsample - 1) / kSubsample;
    const bool isD24 = (fmt == DXGI_FORMAT_R24G8_TYPELESS || fmt == DXGI_FORMAT_D24_UNORM_S8_UINT);

    auto& mip0 = m_hizMipChain[0];
    mip0.width  = mip0W;
    mip0.height = mip0H;
    mip0.data.resize(mip0W * mip0H);

    // Standard-Z: 0=near, 1=far. Conservative Hi-Z stores MAX (farthest depth in tile).
    constexpr float kInv24 = 1.0f / 16777215.0f;
    for (uint32_t y = 0; y < mip0H; y++) {
        const uint32_t srcY0 = y * kSubsample;
        const uint32_t srcY1 = std::min(srcY0 + kSubsample, height);
        for (uint32_t x = 0; x < mip0W; x++) {
            float maxZ = 0.0f;
            const uint32_t srcX0 = x * kSubsample;
            const uint32_t srcX1 = std::min(srcX0 + kSubsample, width);
            for (uint32_t sy = srcY0; sy < srcY1; sy++) {
                const uint8_t* row = rawData + size_t(sy) * rowPitchBytes;
                if (isD24) {
                    const uint32_t* p32 = reinterpret_cast<const uint32_t*>(row);
                    for (uint32_t sx = srcX0; sx < srcX1; sx++) {
                        const float d = float(p32[sx] & 0x00FFFFFFu) * kInv24;
                        if (d > maxZ) maxZ = d;
                    }
                } else {
                    const float* pf = reinterpret_cast<const float*>(row);
                    for (uint32_t sx = srcX0; sx < srcX1; sx++) {
                        if (pf[sx] > maxZ) maxZ = pf[sx];
                    }
                }
            }
            mip0.data[y * mip0W + x] = maxZ;
        }
    }

    // Build remaining mip levels (2x2 MAX downsample)
    m_hizW    = mip0W;
    m_hizH    = mip0H;
    m_hizMips = 1;

    uint32_t prevW = mip0W;
    uint32_t prevH = mip0H;

    for (uint32_t mip = 1; mip < kMaxHiZMips; mip++) {
        uint32_t curW = std::max(prevW / 2, 1u);
        uint32_t curH = std::max(prevH / 2, 1u);

        auto& curMip  = m_hizMipChain[mip];
        auto& prevMip = m_hizMipChain[mip - 1];

        curMip.width  = curW;
        curMip.height = curH;
        curMip.data.resize(curW * curH);

        for (uint32_t y = 0; y < curH; y++) {
            for (uint32_t x = 0; x < curW; x++) {
                uint32_t sx = x * 2;
                uint32_t sy = y * 2;
                float d00 = prevMip.data[sy * prevW + sx];
                float d10 = (sx + 1 < prevW) ? prevMip.data[sy * prevW + sx + 1] : d00;
                float d01 = (sy + 1 < prevH) ? prevMip.data[(sy + 1) * prevW + sx] : d00;
                float d11 = (sx + 1 < prevW && sy + 1 < prevH) ?
                            prevMip.data[(sy + 1) * prevW + sx + 1] : d00;

                curMip.data[y * curW + x] = std::max({d00, d10, d01, d11});
            }
        }

        m_hizMips++;
        prevW = curW;
        prevH = curH;

        if (curW == 1 && curH == 1) break;
    }
}


// ═══════════════════════════════════════════════════════════════════════════
//  TestRect — Test a screen-space rect against the CPU Hi-Z
// ═══════════════════════════════════════════════════════════════════════════

bool OcclusionCuller::TestRect(float minX, float minY, float maxX, float maxY, float nearDepth) const
{
    if (m_hizMips == 0) return false;  // No Hi-Z available, don't cull

    // Clamp to [0,1]
    minX = std::max(minX, 0.0f);
    minY = std::max(minY, 0.0f);
    maxX = std::min(maxX, 1.0f);
    maxY = std::min(maxY, 1.0f);

    if (minX >= maxX || minY >= maxY) return false;

    // Choose mip level where the rect covers approximately 2x2 to 4x4 texels
    float rectW = (maxX - minX) * m_hizW;
    float rectH = (maxY - minY) * m_hizH;
    float maxDim = std::max(rectW, rectH);

    // Mip level = log2(maxDim / 2)  (we want the rect to span ~2 texels)
    int mipLevel = 0;
    if (maxDim > 2.0f) {
        mipLevel = static_cast<int>(std::log2(maxDim / 2.0f));
    }
    mipLevel = std::max(0, std::min(mipLevel, static_cast<int>(m_hizMips) - 1));

    auto& mip = m_hizMipChain[mipLevel];

    // Convert UV rect to texel coordinates at this mip
    int x0 = static_cast<int>(minX * mip.width);
    int y0 = static_cast<int>(minY * mip.height);
    int x1 = static_cast<int>(std::ceil(maxX * mip.width));
    int y1 = static_cast<int>(std::ceil(maxY * mip.height));

    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, static_cast<int>(mip.width));
    y1 = std::min(y1, static_cast<int>(mip.height));

    // Check all covered texels: if ALL have depth > nearDepth (reversed-Z:
    // the Hi-Z stores max=closest, if the object's near point is farther than
    // the closest surface at every covered texel, the object is fully behind).
    //
    // Reversed-Z: z=1 near, z=0 far.
    // Object's nearDepth is the closest point of the bounding sphere (in depth).
    // Hi-Z stores MAX (closest surface).
    // Occluded if: nearDepth < min_of_hiz (object's nearest point is farther
    // than the farthest "closest surface" across the rect)
    //
    // Actually in reversed-Z: higher Z = closer. So:
    // - nearDepth = object's closest depth (highest Z value)
    // - Hi-Z MAX = closest surface (highest Z value)
    // Object is occluded if: nearDepth < ALL Hi-Z values in the rect
    //   i.e., the object's closest point is FARTHER than every blocker
    //   i.e., nearDepth < min(Hi-Z values in rect)

    // Standard-Z (0=near, 1=far): Hi-Z stores MAX (farthest depth in tile).
    // Object is conservatively guaranteed occluded if its nearest bounding-sphere depth
    // is strictly farther than the farthest blocker across every covered texel.
    float maxHiZ = 0.0f;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            const float hizVal = mip.data[y * mip.width + x];
            if (hizVal > maxHiZ) maxHiZ = hizVal;
        }
    }
    return nearDepth > (maxHiZ + 0.0025f);
}


// ═══════════════════════════════════════════════════════════════════════════
//  VS CB tracking
// ═══════════════════════════════════════════════════════════════════════════

void OcclusionCuller::OnVSSetConstantBuffers(UINT startSlot, UINT numBuffers,
                                              ID3D11Buffer* const* ppCBs)
{
    for (UINT i = 0; i < numBuffers; i++) {
        UINT slot = startSlot + i;
        if (slot < kMaxVSCBSlots) {
            m_boundVSCBs[slot] = ppCBs ? ppCBs[i] : nullptr;
        }
    }
}

void OcclusionCuller::OnClearState()
{
    std::memset(m_boundVSCBs, 0, sizeof(m_boundVSCBs));
}


// ═══════════════════════════════════════════════════════════════════════════
//  ExtractWorldPosition — Read world translation from VS CB slot 2
// ═══════════════════════════════════════════════════════════════════════════

float OcclusionCuller::EstimateBoundingRadius(uint32_t indexCount, float scale) const
{
    const uint32_t triCount = indexCount / 3;
    float base = 60.0f;
    if (triCount < 60)        base = 55.0f;
    else if (triCount < 250)  base = 120.0f;
    else if (triCount < 1000) base = 240.0f;
    else if (triCount < 3500) base = 460.0f;
    else                      return 0.0f;  // Never cull huge terrain/building super-meshes
    return base * std::clamp(scale, 0.5f, 4.0f);
}

bool OcclusionCuller::ShouldCull(uint32_t indexCount)
{
    if (!m_enabled || !SceneLights_Opaque()) return false;
    if (indexCount < 36 || indexCount > 10500) return false;

    const float* wm = SceneLights_DrawWorld();
    if (!wm) return false;
    const auto& sl = SceneLights_Frame();
    if (!sl.hasCamera) return false;

    const float sx = std::sqrt(wm[0]*wm[0] + wm[1]*wm[1] + wm[2]*wm[2]);
    const float radius = EstimateBoundingRadius(indexCount, sx);
    if (radius <= 0.0f) return false;

    const uint32_t tris = indexCount / 3;
    m_drawsTested++;
    m_trisTested += tris;

    // ── Stage 1: Exact Current-Frame Ultra-Conservative Frustum Culling ──
    // Skyrim terrain chunks (4096u) and building walls can have off-center pivots with <3000 tris;
    // restrict culling to genuinely small/distant props (indexCount <= 1200, dist > 2600u) with a 1800u
    // conservative bounding guard so NO terrain, building, cliff, or off-center pivot can EVER pop out.
    const float curX = wm[3], curY = wm[7], curZ = wm[11];
    const float curDist = std::sqrt(curX*curX + curY*curY + curZ*curZ);
    if (indexCount <= 1200 && curDist > 2600.0f) {
        const float safeR = std::max(radius * 2.5f, 1800.0f);
        const float cw0 = sl.worldToClip[3][0]*curX + sl.worldToClip[3][1]*curY + sl.worldToClip[3][2]*curZ + sl.worldToClip[3][3];
        if (cw0 < -safeR) {
            m_drawsCulled++; m_frustumCulled++; m_trisCulled += tris;
            return true;
        }
        if (cw0 > 1.0f) {
            const float cx0 = (sl.worldToClip[0][0]*curX + sl.worldToClip[0][1]*curY + sl.worldToClip[0][2]*curZ + sl.worldToClip[0][3]) / cw0;
            const float cy0 = (sl.worldToClip[1][0]*curX + sl.worldToClip[1][1]*curY + sl.worldToClip[1][2]*curZ + sl.worldToClip[1][3]) / cw0;
            const float padNdc = std::clamp(safeR / cw0, 0.25f, 1.50f);
            if (cx0 < -1.0f - padNdc || cx0 > 1.0f + padNdc || cy0 < -1.0f - padNdc || cy0 > 1.0f + padNdc) {
                m_drawsCulled++; m_frustumCulled++; m_trisCulled += tris;
                return true;
            }
            // ── Stage 2: Sub-Pixel Distant Micro-Prop Culling ──
            if (curDist > 5500.0f && indexCount <= 300 && (radius / cw0) < 0.00045f) {
                m_drawsCulled++; m_subPxCulled++; m_trisCulled += tris;
                return true;
            }
        }
    }

    // ── Stage 3: Standard-Z Temporal-Reprojected CPU Hi-Z Occlusion Culling ──
    if (!m_hizReady || !m_hizHasCam || m_hizMips == 0) return false;

    // Re-center object position from current camera-relative space into the Hi-Z frame's camera space.
    const float px = curX + (sl.posAdjust[0] - m_hizCam[0]);
    const float py = curY + (sl.posAdjust[1] - m_hizCam[1]);
    const float pz = curZ + (sl.posAdjust[2] - m_hizCam[2]);
    const float dist = std::sqrt(px*px + py*py + pz*pz);
    // Conservative near guard: never cull nearby props, NPCs, weapons, or foreground foliage.
    if (dist < radius * 2.4f + 160.0f) return false;

    // Project center and nearest point on bounding sphere using the exact Hi-Z frame's worldToClip matrix.
    const float invD = 1.0f / dist;
    const float nx = px - px * invD * radius;
    const float ny = py - py * invD * radius;
    const float nz = pz - pz * invD * radius;

    auto proj = [&](float x, float y, float z, float& ndcX, float& ndcY, float& ndcZ) -> bool {
        const float cw = m_hizW2C[3][0]*x + m_hizW2C[3][1]*y + m_hizW2C[3][2]*z + m_hizW2C[3][3];
        if (cw <= 1.0f) return false;
        const float cx = m_hizW2C[0][0]*x + m_hizW2C[0][1]*y + m_hizW2C[0][2]*z + m_hizW2C[0][3];
        const float cy = m_hizW2C[1][0]*x + m_hizW2C[1][1]*y + m_hizW2C[1][2]*z + m_hizW2C[1][3];
        const float cz = m_hizW2C[2][0]*x + m_hizW2C[2][1]*y + m_hizW2C[2][2]*z + m_hizW2C[2][3];
        const float iw = 1.0f / cw;
        ndcX = cx * iw; ndcY = cy * iw; ndcZ = cz * iw;
        return true;
    };

    float cx, cy, cz, nxNdc, nyNdc, nearZ;
    if (!proj(px, py, pz, cx, cy, cz) || !proj(nx, ny, nz, nxNdc, nyNdc, nearZ)) return false;
    if (nearZ <= 0.0f || nearZ >= 1.0f) return false;
    // Skip objects straddling the frustum edge (newly visible during camera turns).
    if (cx < -0.95f || cx > 0.95f || cy < -0.95f || cy > 0.95f) return false;

    const float u = cx * 0.5f + 0.5f;
    const float v = 0.5f - cy * 0.5f;
    const float halfU = std::clamp((radius * invD) * 0.95f + 1.5f / float(std::max(m_hizW, 1u)), 0.005f, 0.35f);
    const float halfV = std::clamp((radius * invD) * 1.45f + 1.5f / float(std::max(m_hizH, 1u)), 0.005f, 0.35f);

    if (TestRect(u - halfU, v - halfV, u + halfU, v + halfV, nearZ)) {
        m_drawsCulled++; m_hiZCulled++; m_trisCulled += tris;
        return true;
    }
    return false;
}

} // namespace SB::Proxy
