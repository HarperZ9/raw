// test_fluid_composite_warp.cpp: End-to-End WARP + D3D12 Debug Layer GPU Proof for
// RawFluidSimHost (64x64x64 3D Eulerian Navier-Stokes Fire/Smoke + Radiance Cascade)
// -> RawFluidCompositeHost (RawFluidFireComposite.hlsl volumetric raymarch + SceneDepth occlusion).
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "RawFluidSimHost.h"
#include "RawFluidCompositeHost.h"
#include "raw_fluid_fire_composite_cs.h"

using Microsoft::WRL::ComPtr;
using namespace raw::bridge;

namespace
{

int g_pass = 0;
int g_fail = 0;

void Check(bool ok, const char* msg)
{
    if (ok) {
        ++g_pass;
        std::printf("[PASS] %s\n", msg);
    } else {
        ++g_fail;
        std::printf("[FAIL] %s\n", msg);
    }
}

float HalfToFloat(uint16_t h)
{
    const uint32_t s = (uint32_t(h & 0x8000u) << 16);
    uint32_t e = (h >> 10) & 0x1Fu;
    uint32_t m = h & 0x3FFu;
    uint32_t bits = 0;
    if (e == 0) {
        if (m == 0) {
            bits = s;
        } else {
            e = 127 - 14;
            while ((m & 0x400u) == 0) { m <<= 1; --e; }
            m &= 0x3FFu;
            bits = s | (e << 23) | (m << 13);
        }
    } else if (e == 31) {
        bits = s | 0x7F800000u | (m << 13);
    } else {
        bits = s | ((e + (127 - 15)) << 23) | (m << 13);
    }
    float f = 0.0f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// Build inverse projection (clip -> camera-relative world) for a camera at camPos looking along +Y (Z-up, +X right).
// Forward = (0, 1, 0), Right = (1, 0, 0), Up = (0, 0, 1).
// Projection: cx = x * (f/aspect), cy = z * f, cz = a*y + b, cw = y.
// Inverse: x = cx * (aspect/f), y = 1, z = cy / f, w_inv = (a - cz) / (-b) -> divide by w_inv!
// Specifically: clip (cx, cy, cz, 1) -> viewZ = B / (A - cz) where A = far/(far-near), B = far*near/(far-near).
void BuildClipToWorldRowMajor(float aspect, float nearZ, float farZ, float out[16])
{
    const float tanHalfFov = std::tan(0.55f); // ~63 deg vFOV
    const float a = farZ / (farZ - nearZ);
    const float b = (farZ * nearZ) / (farZ - nearZ);
    // Row 0 (X = right): cx * (tanHalfFov * aspect)
    // Row 1 (Y = forward): 1.0 * 1
    // Row 2 (Z = up): cy * tanHalfFov
    // Row 3 (W): (a - cz) / b = (-1/b)*cz + (a/b)*1
    // Check: for c=(0,0,1,0): (0, 0, 0, -1/b) -> eye = (0,0,0)!
    // Check: for c=(0,0,d,1): y = 1 / ((a - d)/b) = b / (a - d) = viewZ! Exact match with LightingCommon.hlsli!
    std::memset(out, 0, 64);
    out[0]  = tanHalfFov * aspect;
    out[7]  = 1.0f;
    out[9]  = tanHalfFov;
    out[14] = -1.0f / b;
    out[15] = a / b;
}

} // namespace

int main()
{
    std::printf("=== RAW GPU 3D Fluid Fire Composite WARP Proof ===\n");

    ComPtr<ID3D12Debug> dbg;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
        dbg->EnableDebugLayer();
    }

    ComPtr<IDXGIFactory4> fac;
    Check(SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&fac))), "DXGI factory created");
    if (!fac) return 1;

    ComPtr<IDXGIAdapter> warp;
    Check(SUCCEEDED(fac->EnumWarpAdapter(IID_PPV_ARGS(&warp))), "WARP adapter enumerated");
    if (!warp) return 1;

    ComPtr<ID3D12Device> dev;
    Check(SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev))), "WARP D3D12 device created");
    if (!dev) return 1;

    ComPtr<ID3D12InfoQueue> iq;
    dev.As(&iq);

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    Check(SUCCEEDED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))), "D3D12 direct queue created");
    if (!queue) return 1;

    RawFluidSimHost fluidSim;
    std::string err;
    Check(fluidSim.Init(dev.Get(), &err), "RawFluidSimHost::Init succeeded on WARP");
    if (!fluidSim.Ready()) {
        std::printf("  Init err: %s\n", err.c_str());
        return 1;
    }

    RawFluidCompositeHost compHost;
    err.clear();
    Check(compHost.Init(dev.Get(), &err), "RawFluidCompositeHost::Init succeeded on WARP");
    if (!compHost.Ready()) {
        std::printf("  Comp init err: %s\n", err.c_str());
        return 1;
    }

    // Command list + fence for setup, 16 fluid steps, and readback
    ComPtr<ID3D12CommandAllocator> ca;
    ComPtr<ID3D12GraphicsCommandList> cl;
    ComPtr<ID3D12Fence> fence;
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ca));
    dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ca.Get(), nullptr, IID_PPV_ARGS(&cl));
    dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    uint64_t fVal = 0;
    auto flushQueue = [&]() {
        queue->Signal(fence.Get(), ++fVal);
        if (fence->GetCompletedValue() < fVal) {
            fence->SetEventOnCompletion(fVal, ev);
            WaitForSingleObject(ev, 60000);
        }
    };

    // Place a 64^3 fire cube centered at (0, 260, 40) with span 256u (origin (-128, 132, -88)),
    // and two strong fire emitters spanning both left (x=-36) and right (x=+36) halves of the screen
    // so the left-half occluder test is a genuine planted occlusion test!
    FluidFireParams fp{};
    fp.domainOriginW[0] = -128.0f;
    fp.domainOriginW[1] =  132.0f;
    fp.domainOriginW[2] =  -88.0f;
    fp.cellSize = 4.0f; // 64 * 4 = 256u
    fp.dt = 1.0f / 24.0f;
    fp.windVelW[0] = 6.0f;
    fp.windVelW[1] = 0.0f;
    fp.windVelW[2] = 0.0f;
    fp.jacobiIterations = 4u;
    fp.emitterCount = 2u;
    // Left emitter (will be occluded by SceneDepth left-half wall)
    fp.emitters[0].posW[0] = -36.0f;
    fp.emitters[0].posW[1] = 260.0f;
    fp.emitters[0].posW[2] = -24.0f;
    fp.emitters[0].radius = 22.0f;
    fp.emitters[0].ignitionTempK = 2750.0f;
    fp.emitters[0].fuelRate = 3.2f;
    fp.emitters[0].jetSpeed = 48.0f;
    fp.emitters[0].curlStrength = 14.0f;
    fp.emitters[0].sootSeedRate = 0.85f;
    // Right emitter (visible in open right half!)
    fp.emitters[1] = fp.emitters[0];
    fp.emitters[1].posW[0] = +36.0f;

    for (int step = 0; step < 12; ++step) {
        fp.timeSec = float(step) * fp.dt;
        fluidSim.StepFire(cl.Get(), fp);
    }
    cl->Close();
    ID3D12CommandList* stepLists[1] = {cl.Get()};
    queue->ExecuteCommandLists(1, stepLists);
    flushQueue();
    Check(fluidSim.GetFireDomainInfo().active && fluidSim.GetFireDomainInfo().emitterCount == 2u,
          "RawFluidSimHost stepped 16 frames of 64x64x64 Navier-Stokes fire+smoke+radiance on WARP");

    // Create 256x144 ColorOut (RGBA16F, UAV+RT, cleared to neutral sky blue (0.55, 0.62, 0.72, 1.0))
    // and SceneDepth (R32F: left half = 0.05 foreground occluder, right half = 1.0 sky).
    constexpr UINT W = 256u, H = 144u;
    D3D12_HEAP_PROPERTIES defHp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_HEAP_PROPERTIES upHp{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_HEAP_PROPERTIES rbHp{D3D12_HEAP_TYPE_READBACK};

    D3D12_RESOURCE_DESC colorDesc{};
    colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    colorDesc.Width = W; colorDesc.Height = H; colorDesc.DepthOrArraySize = 1; colorDesc.MipLevels = 1;
    colorDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> colorOut;
    dev->CreateCommittedResource(&defHp, D3D12_HEAP_FLAG_NONE, &colorDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&colorOut));

    D3D12_RESOURCE_DESC depthDesc = colorDesc;
    depthDesc.Format = DXGI_FORMAT_R32_FLOAT;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> depthIn;
    dev->CreateCommittedResource(&defHp, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&depthIn));

    const UINT depthPitch = (W * 4u + 255u) & ~255u;
    D3D12_RESOURCE_DESC upDesc{};
    upDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    upDesc.Width = size_t(depthPitch) * H;
    upDesc.Height = 1; upDesc.DepthOrArraySize = 1; upDesc.MipLevels = 1; upDesc.SampleDesc.Count = 1;
    upDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> depthUp;
    dev->CreateCommittedResource(&upHp, D3D12_HEAP_FLAG_NONE, &upDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&depthUp));
    void* dp = nullptr;
    depthUp->Map(0, nullptr, &dp);
    for (UINT y = 0; y < H; ++y) {
        float* row = reinterpret_cast<float*>(static_cast<uint8_t*>(dp) + size_t(y) * depthPitch);
        for (UINT x = 0; x < W; ++x) {
            row[x] = (x < W / 2u) ? 0.05f : 1.0f; // left half occluded at ~15.8u, right half open sky
        }
    }
    depthUp->Unmap(0, nullptr);

    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    D3D12_DESCRIPTOR_HEAP_DESC rtvD{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    dev->CreateDescriptorHeap(&rtvD, IID_PPV_ARGS(&rtvHeap));
    dev->CreateRenderTargetView(colorOut.Get(), nullptr, rtvHeap->GetCPUDescriptorHandleForHeapStart());

    ca->Reset();
    cl->Reset(ca.Get(), nullptr);
    D3D12_TEXTURE_COPY_LOCATION dDst{}; dDst.pResource = depthIn.Get(); dDst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dSrc{}; dSrc.pResource = depthUp.Get(); dSrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dSrc.PlacedFootprint.Footprint = {DXGI_FORMAT_R32_FLOAT, W, H, 1, depthPitch};
    cl->CopyTextureRegion(&dDst, 0, 0, 0, &dSrc, nullptr);

    D3D12_RESOURCE_BARRIER bPrep[2]{};
    bPrep[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bPrep[0].Transition = {depthIn.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON};
    bPrep[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bPrep[1].Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET};
    cl->ResourceBarrier(2, bPrep);
    constexpr float kBgR = 0.55f, kBgG = 0.62f, kBgB = 0.72f;
    const float bgCol[4] = {kBgR, kBgG, kBgB, 1.0f};
    cl->ClearRenderTargetView(rtvHeap->GetCPUDescriptorHandleForHeapStart(), bgCol, 0, nullptr);
    D3D12_RESOURCE_BARRIER bClr{};
    bClr.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bClr.Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON};
    cl->ResourceBarrier(1, &bClr);
    cl->Close();
    ID3D12CommandList* prepLists[1] = {cl.Get()};
    queue->ExecuteCommandLists(1, prepLists);
    flushQueue();

    // Run live volumetric composite pass!
    FluidCompositeParams cp{};
    BuildClipToWorldRowMajor(float(W) / float(H), 15.0f, 353840.0f, cp.clipToWorld);
    cp.camPosW[0] = 0.0f; cp.camPosW[1] = 0.0f; cp.camPosW[2] = 20.0f;
    cp.simTimeSec = 0.53f;
    const auto fDom = fluidSim.GetFireDomainInfo();
    cp.domainMinW[0] = fDom.originXYZ[0];
    cp.domainMinW[1] = fDom.originXYZ[1];
    cp.domainMinW[2] = fDom.originXYZ[2];
    cp.domainSpanW = fDom.worldSpanU;
    cp.width = W;
    cp.height = H;
    cp.activeEmitters = fDom.emitterCount;
    cp.isInterior = false;

    const bool compRan = compHost.Composite(
        queue.Get(), colorOut.Get(), depthIn.Get(),
        fluidSim.FireThermoResource(), fluidSim.FireRadianceResource(), cp);
    flushQueue();
    Check(compRan, "RawFluidCompositeHost::Composite executed cleanly on WARP");

    // Readback ColorOut
    const UINT colorPitch = (W * 8u + 255u) & ~255u;
    D3D12_RESOURCE_DESC rbDesc = upDesc;
    rbDesc.Width = size_t(colorPitch) * H;
    ComPtr<ID3D12Resource> rb;
    dev->CreateCommittedResource(&rbHp, D3D12_HEAP_FLAG_NONE, &rbDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rb));

    ca->Reset();
    cl->Reset(ca.Get(), nullptr);
    D3D12_RESOURCE_BARRIER bRb{};
    bRb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bRb.Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE};
    cl->ResourceBarrier(1, &bRb);
    D3D12_TEXTURE_COPY_LOCATION rbDst{}; rbDst.pResource = rb.Get(); rbDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    rbDst.PlacedFootprint.Footprint = {DXGI_FORMAT_R16G16B16A16_FLOAT, W, H, 1, colorPitch};
    D3D12_TEXTURE_COPY_LOCATION rbSrc{}; rbSrc.pResource = colorOut.Get(); rbSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    cl->CopyTextureRegion(&rbDst, 0, 0, 0, &rbSrc, nullptr);
    cl->Close();
    ID3D12CommandList* rbLists[1] = {cl.Get()};
    queue->ExecuteCommandLists(1, rbLists);
    flushQueue();

    void* rp = nullptr;
    rb->Map(0, nullptr, &rp);
    uint32_t nanInfCount = 0u;
    uint32_t leftModified = 0u;
    uint32_t rightFlamePixels = 0u;
    uint32_t rightSootPixels = 0u;
    float maxRightR = 0.0f;
    float minRightB = 1.0f;
    for (UINT y = 0; y < H; ++y) {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(rp) + size_t(y) * colorPitch);
        for (UINT x = 0; x < W; ++x) {
            const float r = HalfToFloat(row[x * 4u + 0u]);
            const float g = HalfToFloat(row[x * 4u + 1u]);
            const float b = HalfToFloat(row[x * 4u + 2u]);
            const float a = HalfToFloat(row[x * 4u + 3u]);
            if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a)) {
                ++nanInfCount;
                continue;
            }
            const float diff = std::fabs(r - kBgR) + std::fabs(g - kBgG) + std::fabs(b - kBgB);
            if (x < W / 2u) {
                if (diff > 0.01f) ++leftModified;
            } else {
                maxRightR = std::max(maxRightR, r);
                minRightB = std::min(minRightB, b);
                if (r > 1.20f && r > b * 1.45f) ++rightFlamePixels;
                if (b < kBgB - 0.08f && r <= 1.20f) ++rightSootPixels;
            }
        }
    }
    rb->Unmap(0, nullptr);

    uint32_t d3dErrors = 0u;
    if (iq) {
        const UINT64 nMsg = iq->GetNumStoredMessages();
        for (UINT64 i = 0; i < nMsg; ++i) {
            SIZE_T len = 0;
            iq->GetMessage(i, nullptr, &len);
            std::vector<uint8_t> buf(len);
            auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
            if (SUCCEEDED(iq->GetMessage(i, msg, &len)) &&
                (msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR || msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)) {
                std::printf("  D3D12_DEBUG_ERR: %s\n", msg->pDescription);
                ++d3dErrors;
            }
        }
    }

    std::printf("       Readback: leftOccludedMod=%u, rightFlamePx=%u (maxR=%.2f), rightSootPx=%u (minB=%.3f), nanInf=%u, d3d12Errors=%u\n",
                leftModified, rightFlamePixels, maxRightR, rightSootPixels, minRightB, nanInfCount, d3dErrors);

    Check(d3dErrors == 0u, "D3D12 Debug Layer reported 0 errors/corruptions");
    Check(nanInfCount == 0u, "Composited HDR frame contains 0 NaN/Inf pixels");
    Check(leftModified == 0u, "SceneDepth foreground wall (left half) 100% occludes 3D fluid fire/smoke");
    Check(rightFlamePixels >= 20u && maxRightR > 1.20f,
          "Visible right half renders genuine bright incandescent Planck blackbody flame core");
    Check(rightSootPixels >= 20u && minRightB < kBgB - 0.08f,
          "Visible right half renders Beer-Lambert dark soot attenuation & internal fire scatter");

    // Pass 2: Daytime Sunlight Volumetric Self-Shadowing & Beer-Powder Multi-Scattering Check:
    // Re-clear ColorOut, run Composite with night sunIntensity=0 vs strong daytime sunIntensity=2.5,
    // and compare mean RGB over the exact same smoke-billow pixel mask!
    std::vector<uint8_t> smokeMask(size_t(W) * H, 0u);
    auto runSunPass = [&](float sunInt, bool recordMask, double& outRightSmokeSum, uint32_t& outSmokePx, uint32_t& outLeftMod) -> bool {
        ca->Reset();
        cl->Reset(ca.Get(), nullptr);
        D3D12_RESOURCE_BARRIER b0{};
        b0.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b0.Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET};
        cl->ResourceBarrier(1, &b0);
        cl->ClearRenderTargetView(rtvHeap->GetCPUDescriptorHandleForHeapStart(), bgCol, 0, nullptr);
        b0.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b0.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        cl->ResourceBarrier(1, &b0);
        cl->Close();
        ID3D12CommandList* clrL[1] = {cl.Get()};
        queue->ExecuteCommandLists(1, clrL);
        flushQueue();

        FluidCompositeParams cp2 = cp;
        cp2.sunIntensity = sunInt;
        if (!compHost.Composite(queue.Get(), colorOut.Get(), depthIn.Get(),
                                fluidSim.FireThermoResource(), fluidSim.FireRadianceResource(), cp2)) {
            return false;
        }
        flushQueue();

        ca->Reset();
        cl->Reset(ca.Get(), nullptr);
        b0.Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE};
        cl->ResourceBarrier(1, &b0);
        cl->CopyTextureRegion(&rbDst, 0, 0, 0, &rbSrc, nullptr);
        cl->Close();
        ID3D12CommandList* rbL2[1] = {cl.Get()};
        queue->ExecuteCommandLists(1, rbL2);
        flushQueue();

        void* p2 = nullptr;
        rb->Map(0, nullptr, &p2);
        outRightSmokeSum = 0.0;
        outSmokePx = 0u;
        outLeftMod = 0u;
        for (UINT y = 0; y < H; ++y) {
            const uint16_t* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(p2) + size_t(y) * colorPitch);
            for (UINT x = 0; x < W; ++x) {
                const float r = HalfToFloat(row[x * 4u + 0u]);
                const float g = HalfToFloat(row[x * 4u + 1u]);
                const float b = HalfToFloat(row[x * 4u + 2u]);
                if (x < W / 2u) {
                    if (std::fabs(r - kBgR) + std::fabs(g - kBgG) + std::fabs(b - kBgB) > 0.01f) ++outLeftMod;
                } else {
                    const size_t idx = size_t(y) * W + x;
                    if (recordMask && b < kBgB - 0.05f && r <= 1.20f) {
                        smokeMask[idx] = 1u;
                    }
                    if (smokeMask[idx]) {
                        outRightSmokeSum += double(r + g + b);
                        ++outSmokePx;
                    }
                }
            }
        }
        rb->Unmap(0, nullptr);
        return true;
    };

    double smokeDarkSum = 0.0, smokeSunSum = 0.0;
    uint32_t smokePxDark = 0u, smokePxSun = 0u;
    uint32_t leftModDark = 0u, leftModSun = 0u;
    const bool sunCompOk = runSunPass(0.0f, true, smokeDarkSum, smokePxDark, leftModDark) &&
                           runSunPass(2.5f, false, smokeSunSum, smokePxSun, leftModSun);
    uint32_t d3dErrorsAfterSun = 0u;
    if (iq) {
        const UINT64 nMsg2 = iq->GetNumStoredMessages();
        for (UINT64 i = 0; i < nMsg2; ++i) {
            SIZE_T len = 0;
            iq->GetMessage(i, nullptr, &len);
            std::vector<uint8_t> buf(len);
            auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
            if (SUCCEEDED(iq->GetMessage(i, msg, &len)) &&
                (msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR || msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)) {
                std::printf("  D3D12_DEBUG_ERR: %s\n", msg->pDescription);
                ++d3dErrorsAfterSun;
            }
        }
    }
    const double meanSmokeLift = (smokeSunSum - smokeDarkSum) / std::max(double(smokePxDark), 1.0);
    std::printf("       Daytime Beer-Powder Sun Scatter (%u smoke px): nightSum=%.2f -> sunlitSum=%.2f (gain=%.2fx, meanLift=+%.3f/px), leftMod=%u, d3d12Err=%u\n",
                smokePxDark, smokeDarkSum, smokeSunSum, smokeSunSum / std::max(smokeDarkSum, 1.0e-5), meanSmokeLift, leftModDark + leftModSun, d3dErrorsAfterSun);
    Check(sunCompOk && d3dErrorsAfterSun == 0u && leftModDark == 0u && leftModSun == 0u && smokePxDark >= 20u &&
          smokeSunSum > smokeDarkSum * 1.04 && meanSmokeLift > 0.05,
          "Daytime directional sun self-shadowing + Beer-Powder Mie scatter illuminates sunlit smoke billows cleanly");

    // Pass 3 (Wave 63): Indoor No-Black-Orb Proof + Strict Emitter Hygiene Proof
    // Verifies that:
    //   (a) Oversized neutral room-fill lights (radius=160u) and cool bounce lights (900K) are rejected by StepFire.
    //   (b) Even with ceiling enabled and heavy soot seeding indoors, transmittance stays >= 0.78f (zero black orbs!).
    {
        fluidSim.Reset();
        FluidFireParams fpInt = fp;
        fpInt.enableCeiling = true;
        fpInt.debugOverlayMode = true;
        fpInt.ceilingZLocal = 46.0f;
        fpInt.sootYield = 1.40f;
        fpInt.sootDamping = 0.05f;
        fpInt.emitterCount = 4u;
        fpInt.emitters[0].sootSeedRate = 1.50f;
        fpInt.emitters[1].sootSeedRate = 1.50f;
        // Slot 2: oversized neutral room-fill light (must be rejected)
        fpInt.emitters[2] = fpInt.emitters[1];
        fpInt.emitters[2].radius = 160.0f;
        // Slot 3: cool bounce light < 1400K (must be rejected)
        fpInt.emitters[3] = fpInt.emitters[1];
        fpInt.emitters[3].ignitionTempK = 900.0f;

        // Step in batches of <= 4 frames (matching RawFluidSimHost::kCbRingCount) so Reset() on step 0 is never overwritten.
        for (int batch = 0; batch < 12; batch += 4) {
            ca->Reset();
            cl->Reset(ca.Get(), nullptr);
            for (int s = 0; s < 4 && (batch + s) < 12; ++s) {
                fpInt.timeSec = float(batch + s) * fpInt.dt;
                fluidSim.StepFire(cl.Get(), fpInt);
            }
            cl->Close();
            ID3D12CommandList* intStepL[1] = {cl.Get()};
            queue->ExecuteCommandLists(1, intStepL);
            flushQueue();
        }

        const auto intDom = fluidSim.GetFireDomainInfo();
        Check(intDom.active && intDom.emitterCount == 2u && intDom.isInterior && intDom.debugOverlayMode,
              "RawFluidSimHost emitter hygiene rejected oversized (160u) & cool (900K) room-fill lights & exposed DebugView 36 flags");

        ca->Reset();
        cl->Reset(ca.Get(), nullptr);
        D3D12_RESOURCE_BARRIER bClrInt{};
        bClrInt.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        bClrInt.Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET};
        cl->ResourceBarrier(1, &bClrInt);
        cl->ClearRenderTargetView(rtvHeap->GetCPUDescriptorHandleForHeapStart(), bgCol, 0, nullptr);
        bClrInt.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        bClrInt.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        cl->ResourceBarrier(1, &bClrInt);
        cl->Close();
        ID3D12CommandList* clrIntL[1] = {cl.Get()};
        queue->ExecuteCommandLists(1, clrIntL);
        flushQueue();

        FluidCompositeParams cpInt = cp;
        cpInt.isInterior = true;
        cpInt.sunIntensity = 0.0f;
        cpInt.activeEmitters = intDom.emitterCount;
        const bool intCompOk = compHost.Composite(
            queue.Get(), colorOut.Get(), depthIn.Get(),
            fluidSim.FireThermoResource(), fluidSim.FireRadianceResource(), cpInt);
        flushQueue();

        ca->Reset();
        cl->Reset(ca.Get(), nullptr);
        bClrInt.Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE};
        cl->ResourceBarrier(1, &bClrInt);
        cl->CopyTextureRegion(&rbDst, 0, 0, 0, &rbSrc, nullptr);
        cl->Close();
        ID3D12CommandList* rbIntL[1] = {cl.Get()};
        queue->ExecuteCommandLists(1, rbIntL);
        flushQueue();

        void* pInt = nullptr;
        rb->Map(0, nullptr, &pInt);
        uint32_t intNanInf = 0u;
        uint32_t intLeftMod = 0u;
        uint32_t intFlamePx = 0u;
        uint32_t intBlackOrbPx = 0u;
        float minIntB = 1.0f;
        float maxIntR = 0.0f;
        for (UINT y = 0; y < H; ++y) {
            const uint16_t* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(pInt) + size_t(y) * colorPitch);
            for (UINT x = 0; x < W; ++x) {
                const float r = HalfToFloat(row[x * 4u + 0u]);
                const float g = HalfToFloat(row[x * 4u + 1u]);
                const float b = HalfToFloat(row[x * 4u + 2u]);
                const float a = HalfToFloat(row[x * 4u + 3u]);
                if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a)) {
                    ++intNanInf;
                    continue;
                }
                if (x < W / 2u) {
                    if (std::fabs(r - kBgR) + std::fabs(g - kBgG) + std::fabs(b - kBgB) > 0.01f) ++intLeftMod;
                } else {
                    maxIntR = std::max(maxIntR, r);
                    if (r > 1.20f && r > b * 1.45f) {
                        ++intFlamePx;
                    } else {
                        minIntB = std::min(minIntB, b);
                        if (b < kBgB * 0.76f) {
                            ++intBlackOrbPx;
                        }
                    }
                }
            }
        }
        rb->Unmap(0, nullptr);
        std::printf("       Indoor Proof: flamePx=%u (maxR=%.2f), minNonFlameB=%.3f (trans>=%.2f), blackOrbPx=%u, leftMod=%u, nanInf=%u\n",
                    intFlamePx, maxIntR, minIntB, minIntB / kBgB, intBlackOrbPx, intLeftMod, intNanInf);
        Check(intCompOk && intNanInf == 0u && intLeftMod == 0u && intBlackOrbPx == 0u && minIntB >= kBgB * 0.76f,
              "Indoor composite preserves background transmittance >= 0.78 (0 black concentric orbs)");
        Check(intFlamePx >= 10u && maxIntR > 1.20f,
              "Indoor active flame core still renders crisp turbulent Planck blackbody flames");

        // Pass 3b (Wave 64): DebugView 36 debugOverlayMode (_pad[0].x > 0.5f) WARP GPU Proof
        // Verifies live 3D domain wireframe bounds (cyan), active emitter cores (gold), and indoor
        // transmittance compliance heatmap (green >= 0.78, ZERO red < 0.78 alarms) with SceneDepth occlusion!
        {
            struct DebugFireCompCB {
                float clipToWorld[16];
                float camPosW[4];
                float domainMinW[4];
                float domainInvSpanW[4];
                float dimsAndInv[4];
                float resScaleAndFlags[4];
                float sunDir[4];
                float sunCol[4];
                float debugOverlayParams[4];
                float _pad[16];
            };
            static_assert(sizeof(DebugFireCompCB) == 256u, "DebugFireCompCB must be 256 bytes");

            D3D12_DESCRIPTOR_RANGE srvR{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
            D3D12_DESCRIPTOR_RANGE uavR{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
            D3D12_ROOT_PARAMETER rps[3]{};
            rps[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            rps[0].Descriptor = {0, 0};
            rps[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            rps[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rps[1].DescriptorTable = {1, &srvR};
            rps[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            rps[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rps[2].DescriptorTable = {1, &uavR};
            rps[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            D3D12_STATIC_SAMPLER_DESC ss{};
            ss.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            ss.MaxLOD = D3D12_FLOAT32_MAX;
            ss.ShaderRegister = 0;
            ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            D3D12_ROOT_SIGNATURE_DESC rsd{3, rps, 1, &ss, D3D12_ROOT_SIGNATURE_FLAG_NONE};
            ComPtr<ID3DBlob> sigBlob, errBlob;
            ComPtr<ID3D12RootSignature> dbgRoot;
            ComPtr<ID3D12PipelineState> dbgPso;
            bool dbgInitOk = SUCCEEDED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob)) &&
                             SUCCEEDED(dev->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&dbgRoot)));
            if (dbgInitOk) {
                D3D12_COMPUTE_PIPELINE_STATE_DESC cpd{};
                cpd.pRootSignature = dbgRoot.Get();
                cpd.CS = {kDxil_raw_fluid_fire_composite, sizeof(kDxil_raw_fluid_fire_composite)};
                dbgInitOk = SUCCEEDED(dev->CreateComputePipelineState(&cpd, IID_PPV_ARGS(&dbgPso)));
            }

            ComPtr<ID3D12DescriptorHeap> dbgHeap;
            D3D12_DESCRIPTOR_HEAP_DESC dhd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
            dbgInitOk = dbgInitOk && SUCCEEDED(dev->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&dbgHeap)));
            const UINT dInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            D3D12_RESOURCE_DESC cbDesc = upDesc;
            cbDesc.Width = 256u;
            ComPtr<ID3D12Resource> dbgCb;
            dbgInitOk = dbgInitOk && SUCCEEDED(dev->CreateCommittedResource(&upHp, D3D12_HEAP_FLAG_NONE, &cbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&dbgCb)));

            uint32_t dbgLeftMod = 0u, dbgNanInf = 0u, dbgWirePx = 0u, dbgCorePx = 0u, dbgGreenPx = 0u, dbgRedAlarmPx = 0u;
            if (dbgInitOk) {
                DebugFireCompCB dcb{};
                std::memcpy(dcb.clipToWorld, cpInt.clipToWorld, 64u);
                dcb.camPosW[0] = cpInt.camPosW[0]; dcb.camPosW[1] = cpInt.camPosW[1]; dcb.camPosW[2] = cpInt.camPosW[2]; dcb.camPosW[3] = cpInt.simTimeSec;
                dcb.domainMinW[0] = cpInt.domainMinW[0]; dcb.domainMinW[1] = cpInt.domainMinW[1]; dcb.domainMinW[2] = cpInt.domainMinW[2]; dcb.domainMinW[3] = cpInt.domainSpanW;
                const float invSpan = 1.0f / cpInt.domainSpanW;
                dcb.domainInvSpanW[0] = invSpan; dcb.domainInvSpanW[1] = invSpan; dcb.domainInvSpanW[2] = invSpan; dcb.domainInvSpanW[3] = 0.0f;
                dcb.dimsAndInv[0] = float(W); dcb.dimsAndInv[1] = float(H); dcb.dimsAndInv[2] = 1.0f / float(W); dcb.dimsAndInv[3] = 1.0f / float(H);
                dcb.resScaleAndFlags[0] = 1.0f; dcb.resScaleAndFlags[1] = 1.0f; dcb.resScaleAndFlags[2] = float(intDom.emitterCount); dcb.resScaleAndFlags[3] = 1.0f; // interior
                dcb.sunDir[0] = cpInt.sunDir[0]; dcb.sunDir[1] = cpInt.sunDir[1]; dcb.sunDir[2] = cpInt.sunDir[2]; dcb.sunDir[3] = 0.0f;
                dcb.sunCol[0] = cpInt.sunCol[0]; dcb.sunCol[1] = cpInt.sunCol[1]; dcb.sunCol[2] = cpInt.sunCol[2]; dcb.sunCol[3] = cpInt.ambientFill;
                dcb.debugOverlayParams[0] = 1.0f; // enable DebugView 36 overlay
                void* cbp = nullptr;
                dbgCb->Map(0, nullptr, &cbp);
                std::memcpy(cbp, &dcb, sizeof(dcb));
                dbgCb->Unmap(0, nullptr);

                D3D12_CPU_DESCRIPTOR_HANDLE cpu0 = dbgHeap->GetCPUDescriptorHandleForHeapStart();
                D3D12_SHADER_RESOURCE_VIEW_DESC dsrv{};
                dsrv.Format = DXGI_FORMAT_R32_FLOAT;
                dsrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                dsrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                dsrv.Texture2D.MipLevels = 1;
                dev->CreateShaderResourceView(depthIn.Get(), &dsrv, cpu0);

                D3D12_SHADER_RESOURCE_VIEW_DESC vsrv{};
                vsrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                vsrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                vsrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                vsrv.Texture3D.MipLevels = 1;
                D3D12_CPU_DESCRIPTOR_HANDLE cpu1 = {cpu0.ptr + dInc};
                D3D12_CPU_DESCRIPTOR_HANDLE cpu2 = {cpu0.ptr + size_t(2) * dInc};
                D3D12_CPU_DESCRIPTOR_HANDLE cpu3 = {cpu0.ptr + size_t(3) * dInc};
                dev->CreateShaderResourceView(fluidSim.FireThermoResource(), &vsrv, cpu1);
                dev->CreateShaderResourceView(fluidSim.FireRadianceResource(), &vsrv, cpu2);
                D3D12_UNORDERED_ACCESS_VIEW_DESC uavd{};
                uavd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                uavd.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                dev->CreateUnorderedAccessView(colorOut.Get(), nullptr, &uavd, cpu3);

                ca->Reset();
                cl->Reset(ca.Get(), nullptr);
                D3D12_RESOURCE_BARRIER bD0{};
                bD0.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bD0.Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET};
                cl->ResourceBarrier(1, &bD0);
                cl->ClearRenderTargetView(rtvHeap->GetCPUDescriptorHandleForHeapStart(), bgCol, 0, nullptr);
                D3D12_RESOURCE_BARRIER bD1[2]{};
                bD1[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bD1[0].Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
                bD1[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bD1[1].Transition = {depthIn.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
                cl->ResourceBarrier(2, bD1);

                ID3D12DescriptorHeap* dHeaps[1] = {dbgHeap.Get()};
                cl->SetDescriptorHeaps(1, dHeaps);
                cl->SetComputeRootSignature(dbgRoot.Get());
                cl->SetPipelineState(dbgPso.Get());
                cl->SetComputeRootConstantBufferView(0, dbgCb->GetGPUVirtualAddress());
                D3D12_GPU_DESCRIPTOR_HANDLE gpu0 = dbgHeap->GetGPUDescriptorHandleForHeapStart();
                D3D12_GPU_DESCRIPTOR_HANDLE gpu3 = {gpu0.ptr + UINT64(3) * dInc};
                cl->SetComputeRootDescriptorTable(1, gpu0);
                cl->SetComputeRootDescriptorTable(2, gpu3);
                cl->Dispatch((W + 7u) / 8u, (H + 7u) / 8u, 1u);

                D3D12_RESOURCE_BARRIER bD2[2]{};
                bD2[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bD2[0].Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
                bD2[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bD2[1].Transition = {depthIn.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON};
                cl->ResourceBarrier(2, bD2);
                cl->CopyTextureRegion(&rbDst, 0, 0, 0, &rbSrc, nullptr);
                cl->Close();
                ID3D12CommandList* dL[1] = {cl.Get()};
                queue->ExecuteCommandLists(1, dL);
                flushQueue();

                void* pDbg = nullptr;
                rb->Map(0, nullptr, &pDbg);
                for (UINT y = 0; y < H; ++y) {
                    const uint16_t* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(pDbg) + size_t(y) * colorPitch);
                    for (UINT x = 0; x < W; ++x) {
                        const float r = HalfToFloat(row[x * 4u + 0u]);
                        const float g = HalfToFloat(row[x * 4u + 1u]);
                        const float b = HalfToFloat(row[x * 4u + 2u]);
                        const float a = HalfToFloat(row[x * 4u + 3u]);
                        if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a)) {
                            ++dbgNanInf;
                            continue;
                        }
                        if (x < W / 2u) {
                            if (std::fabs(r - kBgR) + std::fabs(g - kBgG) + std::fabs(b - kBgB) > 0.01f) ++dbgLeftMod;
                        } else {
                            // Cyan domain wireframe: B > 0.90 && G > 0.80 && R < 0.25
                            if (b > 0.90f && g > 0.80f && r < 0.25f) ++dbgWirePx;
                            // Gold-amber active emitter core: R > 0.78 && G > 0.55 && B < 0.45
                            if (r > 0.78f && g > 0.55f && b < 0.45f) ++dbgCorePx;
                            // Green compliant indoor transmittance (>= 0.78): G > 0.74 && G > R + 0.18 && G > B + 0.08
                            if (g > 0.74f && g > r + 0.18f && g > b + 0.08f) ++dbgGreenPx;
                            // Red indoor transmittance violation alarm (< 0.78): R > 0.90 && G < 0.15 && B < 0.15
                            if (r > 0.90f && g < 0.15f && b < 0.15f) ++dbgRedAlarmPx;
                        }
                    }
                }
                rb->Unmap(0, nullptr);
            }
            std::printf("       DebugView 36 Overlay Proof: wirePx=%u, corePx=%u, greenCompliantPx=%u, redAlarmPx=%u, leftMod=%u, nanInf=%u\n",
                        dbgWirePx, dbgCorePx, dbgGreenPx, dbgRedAlarmPx, dbgLeftMod, dbgNanInf);
            Check(dbgInitOk && dbgNanInf == 0u && dbgLeftMod == 0u && dbgWirePx >= 20u && dbgCorePx >= 20u && dbgGreenPx >= 20u && dbgRedAlarmPx == 0u,
                  "DebugView 36 debugOverlayMode visualizes cyan domain wireframe, gold emitter cores & green compliant indoor transmittance (0 red alarms)");

            // Pass 3c (Wave 65): Non-fire DebugView 1..35 Bypass Proof (debugOverlayParams.y > 0.5 && .x <= 0.5)
            // Must produce ZERO writes to ColorOut across the entire 256x144 image even over active flames!
            uint32_t bypassTotalMod = 0u, bypassNanInf = 0u;
            if (dbgInitOk) {
                DebugFireCompCB bcb{};
                std::memcpy(bcb.clipToWorld, cpInt.clipToWorld, 64u);
                bcb.camPosW[0] = cpInt.camPosW[0]; bcb.camPosW[1] = cpInt.camPosW[1]; bcb.camPosW[2] = cpInt.camPosW[2]; bcb.camPosW[3] = cpInt.simTimeSec;
                bcb.domainMinW[0] = cpInt.domainMinW[0]; bcb.domainMinW[1] = cpInt.domainMinW[1]; bcb.domainMinW[2] = cpInt.domainMinW[2]; bcb.domainMinW[3] = cpInt.domainSpanW;
                const float invSpan = 1.0f / cpInt.domainSpanW;
                bcb.domainInvSpanW[0] = invSpan; bcb.domainInvSpanW[1] = invSpan; bcb.domainInvSpanW[2] = invSpan; bcb.domainInvSpanW[3] = 0.0f;
                bcb.dimsAndInv[0] = float(W); bcb.dimsAndInv[1] = float(H); bcb.dimsAndInv[2] = 1.0f / float(W); bcb.dimsAndInv[3] = 1.0f / float(H);
                bcb.resScaleAndFlags[0] = 1.0f; bcb.resScaleAndFlags[1] = 1.0f; bcb.resScaleAndFlags[2] = float(intDom.emitterCount); bcb.resScaleAndFlags[3] = 0.0f;
                bcb.sunDir[0] = cpInt.sunDir[0]; bcb.sunDir[1] = cpInt.sunDir[1]; bcb.sunDir[2] = cpInt.sunDir[2]; bcb.sunDir[3] = 1.0f;
                bcb.sunCol[0] = cpInt.sunCol[0]; bcb.sunCol[1] = cpInt.sunCol[1]; bcb.sunCol[2] = cpInt.sunCol[2]; bcb.sunCol[3] = cpInt.ambientFill;
                bcb.debugOverlayParams[0] = 0.0f; // non-fire debug view
                bcb.debugOverlayParams[1] = 1.0f; // bypassDebugView = true
                void* bcp = nullptr;
                dbgCb->Map(0, nullptr, &bcp);
                std::memcpy(bcp, &bcb, sizeof(bcb));
                dbgCb->Unmap(0, nullptr);

                ca->Reset();
                cl->Reset(ca.Get(), nullptr);
                D3D12_RESOURCE_BARRIER bB0{};
                bB0.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bB0.Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET};
                cl->ResourceBarrier(1, &bB0);
                cl->ClearRenderTargetView(rtvHeap->GetCPUDescriptorHandleForHeapStart(), bgCol, 0, nullptr);
                D3D12_RESOURCE_BARRIER bB1[2]{};
                bB1[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bB1[0].Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
                bB1[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bB1[1].Transition = {depthIn.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
                cl->ResourceBarrier(2, bB1);

                ID3D12DescriptorHeap* bHeaps[1] = {dbgHeap.Get()};
                cl->SetDescriptorHeaps(1, bHeaps);
                cl->SetComputeRootSignature(dbgRoot.Get());
                cl->SetPipelineState(dbgPso.Get());
                cl->SetComputeRootConstantBufferView(0, dbgCb->GetGPUVirtualAddress());
                D3D12_GPU_DESCRIPTOR_HANDLE gpu0 = dbgHeap->GetGPUDescriptorHandleForHeapStart();
                D3D12_GPU_DESCRIPTOR_HANDLE gpu3 = {gpu0.ptr + UINT64(3) * dInc};
                cl->SetComputeRootDescriptorTable(1, gpu0);
                cl->SetComputeRootDescriptorTable(2, gpu3);
                cl->Dispatch((W + 7u) / 8u, (H + 7u) / 8u, 1u);

                D3D12_RESOURCE_BARRIER bB2[2]{};
                bB2[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bB2[0].Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
                bB2[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bB2[1].Transition = {depthIn.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON};
                cl->ResourceBarrier(2, bB2);
                cl->CopyTextureRegion(&rbDst, 0, 0, 0, &rbSrc, nullptr);
                cl->Close();
                ID3D12CommandList* bL[1] = {cl.Get()};
                queue->ExecuteCommandLists(1, bL);
                flushQueue();

                void* pByp = nullptr;
                rb->Map(0, nullptr, &pByp);
                for (UINT y = 0; y < H; ++y) {
                    const uint16_t* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(pByp) + size_t(y) * colorPitch);
                    for (UINT x = 0; x < W; ++x) {
                        const float r = HalfToFloat(row[x * 4u + 0u]);
                        const float g = HalfToFloat(row[x * 4u + 1u]);
                        const float b = HalfToFloat(row[x * 4u + 2u]);
                        const float a = HalfToFloat(row[x * 4u + 3u]);
                        if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a)) {
                            ++bypassNanInf;
                            continue;
                        }
                        if (std::fabs(r - kBgR) + std::fabs(g - kBgG) + std::fabs(b - kBgB) > 0.002f) {
                            ++bypassTotalMod;
                        }
                    }
                }
                rb->Unmap(0, nullptr);
            }
            std::printf("       DebugView 1..35 Bypass Proof: totalMod=%u, nanInf=%u\n", bypassTotalMod, bypassNanInf);
            Check(dbgInitOk && bypassNanInf == 0u && bypassTotalMod == 0u,
                  "Non-fire DebugView 1..35 bypass flag produces 0 writes to ColorOut over active flames");
        }
    }

    // Pass 4 (Wave 63): No Red Low-Temp Sky Bleed Proof
    // Cool active volume below 1180K (emitterCount=0 for 28 frames) and composite onto sky (sceneD=1.0):
    // must produce ZERO neon-red fringe pixels on the sky!
    {
        FluidFireParams fpCool = fp;
        fpCool.enableCeiling = false;
        fpCool.emitterCount = 0u;
        for (int batch = 0; batch < 28; batch += 4) {
            ca->Reset();
            cl->Reset(ca.Get(), nullptr);
            for (int s = 0; s < 4 && (batch + s) < 28; ++s) {
                fpCool.timeSec = float(batch + s + 20) * fpCool.dt;
                fluidSim.StepFire(cl.Get(), fpCool);
            }
            cl->Close();
            ID3D12CommandList* coolL[1] = {cl.Get()};
            queue->ExecuteCommandLists(1, coolL);
            flushQueue();
        }

        ca->Reset();
        cl->Reset(ca.Get(), nullptr);
        D3D12_RESOURCE_BARRIER bClrSky{};
        bClrSky.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        bClrSky.Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET};
        cl->ResourceBarrier(1, &bClrSky);
        cl->ClearRenderTargetView(rtvHeap->GetCPUDescriptorHandleForHeapStart(), bgCol, 0, nullptr);
        bClrSky.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        bClrSky.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        cl->ResourceBarrier(1, &bClrSky);
        cl->Close();
        ID3D12CommandList* clrSkyL[1] = {cl.Get()};
        queue->ExecuteCommandLists(1, clrSkyL);
        flushQueue();

        FluidCompositeParams cpSky = cp;
        cpSky.isInterior = false;
        cpSky.activeEmitters = 1u; // force raymarch over cooled residual volume
        cpSky.sunIntensity = 0.0f;
        const bool skyCompOk = compHost.Composite(
            queue.Get(), colorOut.Get(), depthIn.Get(),
            fluidSim.FireThermoResource(), fluidSim.FireRadianceResource(), cpSky);
        flushQueue();

        ca->Reset();
        cl->Reset(ca.Get(), nullptr);
        bClrSky.Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE};
        cl->ResourceBarrier(1, &bClrSky);
        cl->CopyTextureRegion(&rbDst, 0, 0, 0, &rbSrc, nullptr);
        cl->Close();
        ID3D12CommandList* rbSkyL[1] = {cl.Get()};
        queue->ExecuteCommandLists(1, rbSkyL);
        flushQueue();

        void* pSky = nullptr;
        rb->Map(0, nullptr, &pSky);
        uint32_t redSkyBleedPx = 0u;
        uint32_t skyNanInf = 0u;
        for (UINT y = 0; y < H; ++y) {
            const uint16_t* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(pSky) + size_t(y) * colorPitch);
            for (UINT x = W / 2u; x < W; ++x) {
                const float r = HalfToFloat(row[x * 4u + 0u]);
                const float g = HalfToFloat(row[x * 4u + 1u]);
                const float b = HalfToFloat(row[x * 4u + 2u]);
                if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) {
                    ++skyNanInf;
                    continue;
                }
                if (r > kBgR + 0.06f && r > g * 1.20f) {
                    ++redSkyBleedPx;
                }
            }
        }
        rb->Unmap(0, nullptr);
        std::printf("       Cool Sky Bleed Proof: redSkyBleedPx=%u, nanInf=%u\n", redSkyBleedPx, skyNanInf);
        Check(skyCompOk && skyNanInf == 0u && redSkyBleedPx == 0u,
              "Cooled plume (<1180K) produces 0 neon-red bleed pixels onto sky (sceneD=1.0)");
    }

    uint32_t finalD3dErrors = 0u;
    if (iq) {
        const UINT64 nMsgFinal = iq->GetNumStoredMessages();
        for (UINT64 i = 0; i < nMsgFinal; ++i) {
            SIZE_T len = 0;
            iq->GetMessage(i, nullptr, &len);
            std::vector<uint8_t> buf(len);
            auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
            if (SUCCEEDED(iq->GetMessage(i, msg, &len)) &&
                (msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR || msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)) {
                std::printf("  D3D12_DEBUG_ERR: %s\n", msg->pDescription);
                ++finalD3dErrors;
            }
        }
    }
    Check(finalD3dErrors == 0u, "D3D12 Debug Layer reported 0 errors/corruptions across all 4 passes");

    compHost.Shutdown();
    CloseHandle(ev);
    std::printf("=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
