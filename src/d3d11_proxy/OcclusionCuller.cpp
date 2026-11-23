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
#include "GBufferComplete.h"
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

void OcclusionCuller::OnPresent(ID3D11DeviceContext* /*ctx*/)
{
    if (!m_initialized || !m_enabled) return;

    // Periodic live CPU/GPU draw & triangle savings telemetry (every 300 frames).
    // Uses zero-copy DXBC shader-class-filtered NiAVObject::worldBound frustum & sub-pixel culling
    // (eliminated wasted 1920x1080 CPU depth staging CopyResource/Map every frame under raw::tx DX12).
    if (++m_frameCount % 300 == 0) {
        Log("[OcclusionCuller] frame %llu: hiz=%d (%ux%u) tested %u draws (%llu tris), culled %u draws (%llu tris: frustum=%u subpx=%u hiz=%u, %.1f%% tris saved)",
            static_cast<unsigned long long>(m_frameCount), m_hizReady ? 1 : 0, m_hizW, m_hizH,
            m_drawsTested, static_cast<unsigned long long>(m_trisTested),
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
    const bool hasBound = m_hasExactBound;
    m_hasExactBound = false;  // Consume once per draw so no draw ever inherits a stale bound
    if (!m_enabled || !hasBound || !GBuffer_CurrentDrawIsCullableProp()) return false;
    if (indexCount < 36) return false;

    const auto& sl = SceneLights_Frame();
    if (!sl.hasCamera) return false;

    // Exact Creation Engine NiAVObject::worldBound (center in world space -> camera-relative, authored radius).
    const float radius = m_exactBound[3];
    if (!(radius > 0.1f && radius < 20000.0f)) return false;
    const float curX = m_exactBound[0] - sl.posAdjust[0];
    const float curY = m_exactBound[1] - sl.posAdjust[1];
    const float curZ = m_exactBound[2] - sl.posAdjust[2];
    const float curDist = std::sqrt(curX*curX + curY*curY + curZ*curZ);

    const uint32_t tris = indexCount / 3;
    m_drawsTested++;
    m_trisTested += tris;

    // Never cull anything whose bounding sphere contains or approaches the camera.
    if (curDist <= radius * 1.25f + 64.0f) return false;

    // ── Stage 1: Exact Current-Frame Creation Engine worldBound Frustum Culling ──
    const float safeR = radius * 1.15f + 16.0f;
    const float cw0 = sl.worldToClip[3][0]*curX + sl.worldToClip[3][1]*curY + sl.worldToClip[3][2]*curZ + sl.worldToClip[3][3];
    if (cw0 < -safeR) {
        m_drawsCulled++; m_frustumCulled++; m_trisCulled += tris;
        return true;
    }
    if (cw0 > 1.0f) {
        const float cx0 = (sl.worldToClip[0][0]*curX + sl.worldToClip[0][1]*curY + sl.worldToClip[0][2]*curZ + sl.worldToClip[0][3]) / cw0;
        const float cy0 = (sl.worldToClip[1][0]*curX + sl.worldToClip[1][1]*curY + sl.worldToClip[1][2]*curZ + sl.worldToClip[1][3]) / cw0;
        const float padNdc = (safeR * 1.35f) / cw0;
        if (cx0 < -1.0f - padNdc || cx0 > 1.0f + padNdc || cy0 < -1.0f - padNdc || cy0 > 1.0f + padNdc) {
            m_drawsCulled++; m_frustumCulled++; m_trisCulled += tris;
            return true;
        }
        // ── Stage 2: Exact-Bound Distant Sub-Pixel Micro-Prop Culling ──
        // Only cull genuinely tiny distant props (<1.2 px diameter past 3,500u, authored radius < 48u).
        if (curDist > 3500.0f && radius < 48.0f && (radius / cw0) < 0.00060f) {
            m_drawsCulled++; m_subPxCulled++; m_trisCulled += tris;
            return true;
        }
        // ── Stage 3: Native DX12 Compute Hi-Z Occlusion Culling (240x135 GPU-reduced depth pyramid) ──
        // Conservative screen-space sphere projection against GPU Hi-Z (2 frames retired, exact m_hizW2C/m_hizCam).
        if (m_hizReady && m_hizHasCam && curDist > 320.0f && radius < 480.0f) {
            const float hrx = m_exactBound[0] - m_hizCam[0], hry = m_exactBound[1] - m_hizCam[1], hrz = m_exactBound[2] - m_hizCam[2];
            const float hDist = std::sqrt(hrx * hrx + hry * hry + hrz * hrz);
            if (hDist > radius + 120.0f) {
                const float* hm = &m_hizW2C[0][0];
                const float hcx = hm[0]*hrx + hm[1]*hry + hm[2]*hrz + hm[3];
                const float hcy = hm[4]*hrx + hm[5]*hry + hm[6]*hrz + hm[7];
                const float hcz = hm[8]*hrx + hm[9]*hry + hm[10]*hrz + hm[11];
                const float hcw = hm[12]*hrx + hm[13]*hry + hm[14]*hrz + hm[15];
                if (hcw > 40.0f) {
                    const float invW = 1.0f / hcw;
                    const float sx = hcx * invW * 0.5f + 0.5f;
                    const float sy = 0.5f - hcy * invW * 0.5f;
                    // Conservative 1.25x screen-rect expansion + near-face depth with safety bias prevents any popping.
                    const float rNdc = (radius * 1.25f) * invW;
                    const float nearScale = std::max((hDist - radius * 1.15f) / hDist, 0.1f);
                    const float hrxN = hrx * nearScale, hryN = hry * nearScale, hrzN = hrz * nearScale;
                    const float hczN = hm[8]*hrxN + hm[9]*hryN + hm[10]*hrzN + hm[11];
                    const float hcwN = hm[12]*hrxN + hm[13]*hryN + hm[14]*hrzN + hm[15];
                    if (hcwN > 15.0f && sx - rNdc >= 0.02f && sx + rNdc <= 0.98f && sy - rNdc >= 0.02f && sy + rNdc <= 0.98f) {
                        const float nearDepth = (hczN / hcwN) - 0.0020f;
                        if (nearDepth > 0.0f && nearDepth < 0.9999f && TestRect(sx - rNdc, sy - rNdc, sx + rNdc, sy + rNdc, nearDepth)) {
                            m_drawsCulled++; m_hiZCulled++; m_trisCulled += tris;
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

void OcclusionCuller::UpdateHiZFromGpu(const float* mip0, uint32_t w, uint32_t h, const float w2c[16], const float cam[3])
{
    if (!mip0 || w < 8 || h < 8 || !w2c || !cam) return;
    m_hizMipChain[0].width = w;
    m_hizMipChain[0].height = h;
    m_hizMipChain[0].data.assign(mip0, mip0 + size_t(w) * h);
    m_hizW = w;
    m_hizH = h;
    m_hizMips = 1;
    uint32_t prevW = w, prevH = h;
    for (uint32_t mip = 1; mip < kMaxHiZMips; ++mip) {
        const uint32_t curW = std::max(prevW / 2, 1u);
        const uint32_t curH = std::max(prevH / 2, 1u);
        auto& curMip = m_hizMipChain[mip];
        const auto& prevMip = m_hizMipChain[mip - 1];
        curMip.width = curW;
        curMip.height = curH;
        curMip.data.resize(size_t(curW) * curH);
        for (uint32_t y = 0; y < curH; ++y) {
            for (uint32_t x = 0; x < curW; ++x) {
                const uint32_t sx = x * 2, sy = y * 2;
                const float d00 = prevMip.data[sy * prevW + sx];
                const float d10 = (sx + 1 < prevW) ? prevMip.data[sy * prevW + sx + 1] : d00;
                const float d01 = (sy + 1 < prevH) ? prevMip.data[(sy + 1) * prevW + sx] : d00;
                const float d11 = (sx + 1 < prevW && sy + 1 < prevH) ? prevMip.data[(sy + 1) * prevW + sx + 1] : d00;
                curMip.data[y * curW + x] = std::max({d00, d10, d01, d11});
            }
        }
        ++m_hizMips;
        prevW = curW;
        prevH = curH;
        if (curW == 1 && curH == 1) break;
    }
    std::memcpy(m_hizW2C, w2c, sizeof(m_hizW2C));
    std::memcpy(m_hizCam, cam, sizeof(m_hizCam));
    m_hizHasCam = true;
    m_hizReady = true;
}

} // namespace SB::Proxy

extern "C" __declspec(dllexport) void PG_SetCurrentDrawBound(float cx, float cy, float cz, float radius)
{
    SB::Proxy::OcclusionCuller::Get().SetExactDrawBound(cx, cy, cz, radius);
}
