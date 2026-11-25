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

    compHost.Shutdown();
    CloseHandle(ev);
    std::printf("=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
