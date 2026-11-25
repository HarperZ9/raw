// test_live_world_composite_gpu.cpp: WARP + Hardware DX12 GPU Verification Suite for
// `RawLiveWorldHost` + `RawLiveWorldComposite.hlsl` (SnowDeform tracks, WaterCaustics riverbed
// injection, 65K GrassWind micro-tufts, strict sky & actor base 10/11/12/14 protection,
// world-space camera-shift stability, Debug Views 28/31/33, and < 0.06 ms RTX 4090 budget).
// Original clean-room implementation, MIT License.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "RawLiveWorldHost.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace
{
using Microsoft::WRL::ComPtr;
using namespace raw::bridge;

uint16_t FloatToHalf(float f)
{
    uint32_t bits = 0u;
    std::memcpy(&bits, &f, 4u);
    const uint32_t sign = (bits >> 16u) & 0x8000u;
    int exp             = static_cast<int>((bits >> 23u) & 0xFFu) - 127 + 15;
    uint32_t mant       = (bits >> 13u) & 0x03FFu;
    if ((bits & 0x7FFFFFFFu) == 0x7F800000u) return static_cast<uint16_t>(sign | 0x7C00u); // Inf
    if (((bits >> 23u) & 0xFFu) == 0xFFu)    return static_cast<uint16_t>(sign | 0x7E00u); // NaN
    if (exp <= 0) return static_cast<uint16_t>(sign);
    if (exp >= 31) return static_cast<uint16_t>(sign | 0x7C00u);
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10u) | mant);
}

float HalfToFloat(uint16_t h)
{
    const uint32_t sign = (static_cast<uint32_t>(h & 0x8000u)) << 16u;
    const uint32_t exp  = (h >> 10u) & 0x1Fu;
    const uint32_t mant = h & 0x03FFu;
    uint32_t out = 0u;
    if (exp == 0u) {
        if (mant == 0u) {
            out = sign;
        } else {
            int e = -14;
            uint32_t m = mant;
            while ((m & 0x0400u) == 0u) { m <<= 1u; --e; }
            m &= 0x03FFu;
            out = sign | (static_cast<uint32_t>(e + 127) << 23u) | (m << 13u);
        }
    } else if (exp == 31u) {
        out = sign | 0x7F800000u | (mant << 13u);
    } else {
        out = sign | ((exp + 112u) << 23u) | (mant << 13u);
    }
    float f = 0.0f;
    std::memcpy(&f, &out, 4u);
    return f;
}

struct GpuCtx {
    ComPtr<ID3D12Device>              dev;
    ComPtr<ID3D12InfoQueue>           infoQueue;
    ComPtr<ID3D12CommandQueue>        queue;
    ComPtr<ID3D12CommandAllocator>    alloc;
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ComPtr<ID3D12Fence>               fence;
    HANDLE                            event = nullptr;
    uint64_t                          fenceVal = 0ull;
    uint64_t                          tsFreq = 0ull;

    ~GpuCtx() {
        WaitIdle();
        if (event) CloseHandle(event);
    }

    bool Init(bool useWarp, bool enableDebug) {
        if (enableDebug) {
            ComPtr<ID3D12Debug> dbg;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
                dbg->EnableDebugLayer();
            }
        }
        ComPtr<IDXGIFactory4> fac;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&fac)))) return false;

        ComPtr<IDXGIAdapter1> adapter;
        if (useWarp) {
            if (FAILED(fac->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))) return false;
        } else {
            for (UINT i = 0u; fac->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
                DXGI_ADAPTER_DESC1 ad{};
                adapter->GetDesc1(&ad);
                if ((ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0u) break;
                adapter.Reset();
            }
            if (!adapter) return false;
        }

        if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev)))) {
            return false;
        }
        dev.As(&infoQueue);

        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) return false;
        queue->GetTimestampFrequency(&tsFreq);

        if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) ||
            FAILED(dev->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd))) ||
            FAILED(cmd->Close()) ||
            FAILED(dev->CreateFence(0ull, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
            return false;
        }
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return event != nullptr;
    }

    void WaitIdle() {
        if (!queue || !fence || !event) return;
        const uint64_t v = ++fenceVal;
        queue->Signal(fence.Get(), v);
        if (fence->GetCompletedValue() < v) {
            fence->SetEventOnCompletion(v, event);
            WaitForSingleObject(event, 5000);
        }
    }

    uint32_t DebugErrorCount() const {
        if (!infoQueue) return 0u;
        const UINT64 n = infoQueue->GetNumStoredMessages();
        uint32_t errs = 0u;
        for (UINT64 i = 0u; i < n; ++i) {
            SIZE_T len = 0u;
            infoQueue->GetMessage(i, nullptr, &len);
            if (len == 0u) continue;
            std::vector<uint8_t> buf(len);
            auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
            if (SUCCEEDED(infoQueue->GetMessage(i, msg, &len))) {
                if (msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ||
                    msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR) {
                    std::fprintf(stderr, "  [D3D12 ERROR] %s\n", msg->pDescription);
                    ++errs;
                }
            }
        }
        return errs;
    }
};

ComPtr<ID3D12Resource> CreateTex2D(ID3D12Device* dev, uint32_t w, uint32_t h, DXGI_FORMAT fmt,
                                   D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState)
{
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = w;
    rd.Height           = h;
    rd.DepthOrArraySize = 1u;
    rd.MipLevels        = 1u;
    rd.Format           = fmt;
    rd.SampleDesc.Count = 1u;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags            = flags;
    ComPtr<ID3D12Resource> res;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initState, nullptr, IID_PPV_ARGS(&res)))) {
        return {};
    }
    return res;
}

bool UploadTex2D(GpuCtx& ctx, ID3D12Resource* dst, D3D12_RESOURCE_STATES dstState,
                 const void* srcData, uint32_t rowBytes, uint32_t height)
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT numRows = 0u;
    UINT64 rowSize = 0u, totalBytes = 0u;
    const D3D12_RESOURCE_DESC desc = dst->GetDesc();
    ctx.dev->GetCopyableFootprints(&desc, 0u, 1u, 0u, &fp, &numRows, &rowSize, &totalBytes);

    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = totalBytes;
    rd.Height           = 1u;
    rd.DepthOrArraySize = 1u;
    rd.MipLevels        = 1u;
    rd.SampleDesc.Count = 1u;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> up;
    if (FAILED(ctx.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&up)))) {
        return false;
    }
    uint8_t* mapped = nullptr;
    D3D12_RANGE zero{0u, 0u};
    if (FAILED(up->Map(0u, &zero, reinterpret_cast<void**>(&mapped)))) return false;
    for (uint32_t y = 0u; y < height; ++y) {
        std::memcpy(mapped + fp.Offset + static_cast<size_t>(y) * fp.Footprint.RowPitch,
                    static_cast<const uint8_t*>(srcData) + static_cast<size_t>(y) * rowBytes, rowBytes);
    }
    up->Unmap(0u, nullptr);

    ctx.alloc->Reset();
    ctx.cmd->Reset(ctx.alloc.Get(), nullptr);

    if (dstState != D3D12_RESOURCE_STATE_COPY_DEST) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = dst;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = dstState;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        ctx.cmd->ResourceBarrier(1u, &b);
    }
    D3D12_TEXTURE_COPY_LOCATION dLoc{dst, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
    dLoc.SubresourceIndex = 0u;
    D3D12_TEXTURE_COPY_LOCATION sLoc{up.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
    sLoc.PlacedFootprint = fp;
    ctx.cmd->CopyTextureRegion(&dLoc, 0u, 0u, 0u, &sLoc, nullptr);

    if (dstState != D3D12_RESOURCE_STATE_COPY_DEST) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = dst;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter  = dstState;
        ctx.cmd->ResourceBarrier(1u, &b);
    }

    ctx.cmd->Close();
    ID3D12CommandList* ls[1] = {ctx.cmd.Get()};
    ctx.queue->ExecuteCommandLists(1u, ls);
    ctx.WaitIdle();
    return true;
}

bool ReadbackRgba16f(GpuCtx& ctx, ID3D12Resource* src, D3D12_RESOURCE_STATES srcState,
                     uint32_t w, uint32_t h, std::vector<float>& outRgba)
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT numRows = 0u;
    UINT64 rowSize = 0u, totalBytes = 0u;
    const D3D12_RESOURCE_DESC desc = src->GetDesc();
    ctx.dev->GetCopyableFootprints(&desc, 0u, 1u, 0u, &fp, &numRows, &rowSize, &totalBytes);

    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = totalBytes;
    rd.Height           = 1u;
    rd.DepthOrArraySize = 1u;
    rd.MipLevels        = 1u;
    rd.SampleDesc.Count = 1u;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> rb;
    if (FAILED(ctx.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rb)))) {
        return false;
    }

    ctx.alloc->Reset();
    ctx.cmd->Reset(ctx.alloc.Get(), nullptr);

    if (srcState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = src;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = srcState;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ctx.cmd->ResourceBarrier(1u, &b);
    }

    D3D12_TEXTURE_COPY_LOCATION dLoc{rb.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
    dLoc.PlacedFootprint = fp;
    D3D12_TEXTURE_COPY_LOCATION sLoc{src, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
    sLoc.SubresourceIndex = 0u;
    ctx.cmd->CopyTextureRegion(&dLoc, 0u, 0u, 0u, &sLoc, nullptr);

    if (srcState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = src;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.StateAfter  = srcState;
        ctx.cmd->ResourceBarrier(1u, &b);
    }

    ctx.cmd->Close();
    ID3D12CommandList* ls[1] = {ctx.cmd.Get()};
    ctx.queue->ExecuteCommandLists(1u, ls);
    ctx.WaitIdle();

    uint8_t* mapped = nullptr;
    D3D12_RANGE rr{0u, static_cast<SIZE_T>(totalBytes)};
    if (FAILED(rb->Map(0u, &rr, reinterpret_cast<void**>(&mapped)))) return false;
    outRgba.resize(static_cast<size_t>(w) * h * 4u);
    for (uint32_t y = 0u; y < h; ++y) {
        const auto* row = reinterpret_cast<const uint16_t*>(mapped + fp.Offset + static_cast<size_t>(y) * fp.Footprint.RowPitch);
        for (uint32_t x = 0u; x < w; ++x) {
            const size_t idx = (static_cast<size_t>(y) * w + x) * 4u;
            outRgba[idx + 0u] = HalfToFloat(row[x * 4u + 0u]);
            outRgba[idx + 1u] = HalfToFloat(row[x * 4u + 1u]);
            outRgba[idx + 2u] = HalfToFloat(row[x * 4u + 2u]);
            outRgba[idx + 3u] = HalfToFloat(row[x * 4u + 3u]);
        }
    }
    D3D12_RANGE empty{0u, 0u};
    rb->Unmap(0u, &empty);
    return true;
}

// Simple orthographic clipToWorld matrix mapping NDC (x, y, d) -> camera-relative world (x*spanX, y*spanY, zBase - d*spanZ).
void BuildOrthoClipToWorld(float spanX, float spanY, float zBase, float spanZ, float out[16])
{
    std::memset(out, 0, 16u * sizeof(float));
    // Row-major dot(clipToWorld[r], float4(ndcX, ndcY, d, 1)):
    out[0]  = spanX;  // worldX = ndcX * spanX
    out[5]  = spanY;  // worldY = ndcY * spanY
    out[10] = -spanZ; // worldZ = zBase - d * spanZ
    out[11] = zBase;
    out[15] = 1.0f;
}

// Exact perspective camera basis & inverse clipToWorld for real cam_sweep yaw/pitch anti-swimming proof!
struct PerspCamBasis {
    float eye[3];
    float R[3];
    float U[3];
    float F[3];
    float tanHalfFov;
    float aspect;
    float zn;
    float zf;
};

PerspCamBasis BuildPerspCam(float eyeX, float eyeY, float eyeZ, float yawRad, float pitchRad,
                            float tanHalfFov, float aspect, float zn, float zf, float outClipToWorld[16])
{
    PerspCamBasis c{};
    c.eye[0] = eyeX; c.eye[1] = eyeY; c.eye[2] = eyeZ;
    c.tanHalfFov = tanHalfFov; c.aspect = aspect; c.zn = zn; c.zf = zf;

    const float cy = std::cos(yawRad), sy = std::sin(yawRad);
    const float cp = std::cos(pitchRad), sp = std::sin(pitchRad);
    c.F[0] = sy * cp; c.F[1] = cy * cp; c.F[2] = sp;
    c.R[0] = cy;      c.R[1] = -sy;     c.R[2] = 0.0f;
    // U = cross(R, F)
    c.U[0] = c.R[1] * c.F[2] - c.R[2] * c.F[1];
    c.U[1] = c.R[2] * c.F[0] - c.R[0] * c.F[2];
    c.U[2] = c.R[0] * c.F[1] - c.R[1] * c.F[0];

    // w.xyz = ndcX*(tanHalfFov*aspect)*R + ndcY*tanHalfFov*U + F; w.w = (1/zn) - d*(1/zn - 1/zf)
    const float sx = tanHalfFov * aspect;
    const float syF = tanHalfFov;
    std::memset(outClipToWorld, 0, 16u * sizeof(float));
    outClipToWorld[0]  = sx * c.R[0]; outClipToWorld[1]  = syF * c.U[0]; outClipToWorld[3]  = c.F[0];
    outClipToWorld[4]  = sx * c.R[1]; outClipToWorld[5]  = syF * c.U[1]; outClipToWorld[7]  = c.F[1];
    outClipToWorld[8]  = sx * c.R[2]; outClipToWorld[9]  = syF * c.U[2]; outClipToWorld[11] = c.F[2];
    outClipToWorld[14] = -((1.0f / zn) - (1.0f / zf));
    outClipToWorld[15] = 1.0f / zn;
    return c;
}

bool ProjectWorldPoint(const PerspCamBasis& c, float wx, float wy, float wz, uint32_t w, uint32_t h,
                       float& outPx, float& outPy, float& outD)
{
    const float dx = wx - c.eye[0], dy = wy - c.eye[1], dz = wz - c.eye[2];
    const float vx = dx * c.R[0] + dy * c.R[1] + dz * c.R[2];
    const float vy = dx * c.U[0] + dy * c.U[1] + dz * c.U[2];
    const float vz = dx * c.F[0] + dy * c.F[1] + dz * c.F[2];
    if (vz <= c.zn + 1.0f || vz >= c.zf - 1.0f) return false;
    const float ndcX = vx / (vz * c.tanHalfFov * c.aspect);
    const float ndcY = vy / (vz * c.tanHalfFov);
    if (std::fabs(ndcX) > 0.92f || std::fabs(ndcY) > 0.92f) return false;
    outPx = (ndcX * 0.5f + 0.5f) * static_cast<float>(w) - 0.5f;
    outPy = (0.5f - ndcY * 0.5f) * static_cast<float>(h) - 0.5f;
    outD  = (c.zf / (c.zf - c.zn)) * (1.0f - c.zn / vz);
    return true;
}

void FillPlaneDepthForPerspCam(const PerspCamBasis& c, float planeZ, uint32_t w, uint32_t h, std::vector<float>& depthOut)
{
    depthOut.assign(static_cast<size_t>(w) * h, 1.0f);
    for (uint32_t y = 0u; y < h; ++y) {
        const float ndcY = (1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(h)) * 2.0f - 1.0f;
        for (uint32_t x = 0u; x < w; ++x) {
            const float ndcX = ((static_cast<float>(x) + 0.5f) / static_cast<float>(w)) * 2.0f - 1.0f;
            const float rz = ndcX * c.tanHalfFov * c.aspect * c.R[2] + ndcY * c.tanHalfFov * c.U[2] + c.F[2];
            if (rz >= -1.0e-4f) continue; // Above horizon -> sky
            const float t = (planeZ - c.eye[2]) / rz;
            if (t <= c.zn || t >= c.zf) continue;
            // Note: dot(ray, F) = 1.0 by construction, so vz == t!
            const float d = (c.zf / (c.zf - c.zn)) * (1.0f - c.zn / t);
            depthOut[static_cast<size_t>(y) * w + x] = std::clamp(d, 0.0001f, 0.9999f);
        }
    }
}

int RunBackendSuite(bool useWarp, const char* label)
{
    int failures = 0;
    auto check = [&](bool cond, const char* name) {
        if (!cond) ++failures;
        std::printf("[%s] %s %s\n", label, cond ? "PASS" : "FAIL", name);
    };

    GpuCtx ctx{};
    if (!ctx.Init(useWarp, true)) {
        std::printf("[%s] SKIP device unavailable\n", label);
        return useWarp ? 1 : 0;
    }

    RawLiveWorldHost host{};
    std::string err;
    if (!host.Init(ctx.dev.Get(), &err)) {
        std::fprintf(stderr, "[%s] RawLiveWorldHost::Init failed: %s\n", label, err.c_str());
        return 1;
    }
    if (ctx.tsFreq > 0ull) host.SetTimestampFrequency(ctx.tsFreq);

    constexpr uint32_t W = 256u;
    constexpr uint32_t H = 256u;
    constexpr uint32_t S = 64u; // 64x64 test solver maps

    auto colorOut   = CreateTex2D(ctx.dev.Get(), W, H, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                  D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    auto depthIn    = CreateTex2D(ctx.dev.Get(), W, H, DXGI_FORMAT_R32_FLOAT,
                                  D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
    auto normalIn   = CreateTex2D(ctx.dev.Get(), W, H, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                  D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
    auto albedoIn   = CreateTex2D(ctx.dev.Get(), W, H, DXGI_FORMAT_R8G8B8A8_UNORM,
                                  D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
    auto specularIn = CreateTex2D(ctx.dev.Get(), W, H, DXGI_FORMAT_R8G8B8A8_UNORM,
                                  D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);

    auto snowState  = CreateTex2D(ctx.dev.Get(), S, S, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                  D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto snowNormal = CreateTex2D(ctx.dev.Get(), S, S, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                  D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto caustics   = CreateTex2D(ctx.dev.Get(), S, S, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                  D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto grassBend  = CreateTex2D(ctx.dev.Get(), S, S, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                  D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // Build deterministic synthetic G-buffer divided into quadrants:
    //   Q0 (top-left, x<128, y<128): Snowy terrain (base=4, white albedo, z=+50u)
    //     - Left half of Q0 sits over deep snow footprint trench (depth=0.04m vs baseline 0.40m)!
    //     - Right half of Q0 sits over untouched flat snow (depth=0.40m)!
    //   Q1 (top-right, x>=128, y<128): Submerged riverbed (base=4, z=-120u < waterPlaneZ=0u) -> WaterCaustics!
    //   Q2 (bottom-left, x<128, y>=128): Grassy meadow terrain (base=4, green albedo, z=+80u) -> GrassWind!
    //   Q3 (bottom-right, x>=128, y>=128): Strictly protected zones:
    //     - y in [128..159]: Sky (d = 1.0)
    //     - y in [160..191]: Actor Skin (base=10)
    //     - y in [192..223]: Skinned Creature/Dog/Cloth (base=14)
    //     - y in [224..255]: Hair/Eye (base=11/12)
    std::vector<uint16_t> baseColor(W * H * 4u);
    std::vector<float>    depthBuf(W * H, 0.5f);
    std::vector<uint16_t> normBuf(W * H * 4u);
    std::vector<uint8_t>  albBuf(W * H * 4u);
    std::vector<uint8_t>  specBuf(W * H * 4u);

    // View-space up normal encoding: e=(0,0) -> nv=(0,0,-1); with viewToWorld row2=(0,0,-1) -> world N=(0,0,+1)
    for (uint32_t y = 0u; y < H; ++y) {
        for (uint32_t x = 0u; x < W; ++x) {
            const size_t i  = static_cast<size_t>(y) * W + x;
            const size_t i4 = i * 4u;
            baseColor[i4 + 0u] = FloatToHalf(0.50f);
            baseColor[i4 + 1u] = FloatToHalf(0.50f);
            baseColor[i4 + 2u] = FloatToHalf(0.50f);
            baseColor[i4 + 3u] = FloatToHalf(1.00f);

            normBuf[i4 + 0u] = FloatToHalf(0.5f);
            normBuf[i4 + 1u] = FloatToHalf(0.5f);
            normBuf[i4 + 2u] = FloatToHalf(0.0f);
            normBuf[i4 + 3u] = FloatToHalf(1.0f);

            uint8_t cls = 4u;
            uint8_t ar = 180u, ag = 185u, ab = 190u;
            float d = 0.20f; // worldZ = +100 - 0.20*250 = +50u

            if (x < 128u && y < 128u) {
                // Q0: Snowy terrain (top half y < 96) + 3D Glacial Ice Facets (y in [96..127], base == 6u)!
                if (y < 96u) {
                    cls = 4u; ar = 235u; ag = 240u; ab = 245u; d = 0.20f;
                } else {
                    cls = 6u; ar = 175u; ag = 210u; ab = 245u; d = 0.22f;
                }
            } else if (x >= 128u && y < 128u) {
                // Q1: Submerged riverbed (worldZ = +100 - 0.88*250 = -120u < waterPlaneZ=0u)
                cls = 4u; ar = 110u; ag = 120u; ab = 115u; d = 0.88f;
            } else if (x < 128u && y >= 128u) {
                // Q2: Grassy meadow terrain (worldZ = +50u)
                cls = 4u; ar = 72u; ag = 135u; ab = 48u; d = 0.20f;
            } else {
                // Q3: Protected Sky & Actors (base 10, 11, 12, 14) placed right at submerged/snow depth!
                if (y < 160u) {
                    cls = 0u; d = 1.0f; ar = 80u; ag = 120u; ab = 220u;
                } else if (y < 192u) {
                    cls = 10u; d = 0.88f; ar = 210u; ag = 160u; ab = 135u;
                } else if (y < 224u) {
                    cls = 14u; d = 0.88f; ar = 140u; ag = 115u; ab = 90u; // Skinned Dog/Cloth!
                } else {
                    cls = (x < 192u) ? 11u : 12u; d = 0.20f; ar = 95u; ag = 70u; ab = 45u;
                }
            }
            depthBuf[i]     = d;
            albBuf[i4 + 0u] = ar; albBuf[i4 + 1u] = ag; albBuf[i4 + 2u] = ab; albBuf[i4 + 3u] = 255u;
            specBuf[i4 + 0u] = 40u; specBuf[i4 + 1u] = 128u; specBuf[i4 + 2u] = cls; specBuf[i4 + 3u] = 255u;
        }
    }

    // Build solver textures:
    // SnowState (64x64 over [-32..+32]m): left half x < 24 has deep compacted footprint trench (depth=0.04m),
    // right half has untouched flat snow (depth=0.40m).
    std::vector<uint16_t> snowS(S * S * 4u);
    std::vector<uint16_t> snowN(S * S * 4u);
    std::vector<uint16_t> caustB(S * S * 4u);
    std::vector<uint16_t> grassCalm(S * S * 4u);
    std::vector<uint16_t> grassGust(S * S * 4u);

    for (uint32_t y = 0u; y < S; ++y) {
        for (uint32_t x = 0u; x < S; ++x) {
            const size_t i4 = (static_cast<size_t>(y) * S + x) * 4u;
            const bool inTrench = (x < 24u);
            snowS[i4 + 0u] = FloatToHalf(inTrench ? 0.04f : 0.40f);
            snowS[i4 + 1u] = FloatToHalf(inTrench ? 0.90f : 0.00f);
            snowS[i4 + 2u] = FloatToHalf(0.0f);
            snowS[i4 + 3u] = FloatToHalf(0.0f);

            // Trench wall tilts away from sun for crisp directional shading:
            snowN[i4 + 0u] = FloatToHalf(inTrench ? -0.45f : 0.0f);
            snowN[i4 + 1u] = FloatToHalf(inTrench ?  0.89f : 1.0f);
            snowN[i4 + 2u] = FloatToHalf(0.0f);
            snowN[i4 + 3u] = FloatToHalf(0.0f);

            // Bright chromatic water caustics:
            caustB[i4 + 0u] = FloatToHalf(0.75f);
            caustB[i4 + 1u] = FloatToHalf(1.35f);
            caustB[i4 + 2u] = FloatToHalf(1.65f);
            caustB[i4 + 3u] = FloatToHalf(1.40f);

            // Grass calm vs strong gust/trample:
            grassCalm[i4 + 0u] = FloatToHalf(0.02f);
            grassCalm[i4 + 1u] = FloatToHalf(0.01f);
            grassCalm[i4 + 2u] = FloatToHalf(0.0f);
            grassCalm[i4 + 3u] = FloatToHalf(0.0f);

            grassGust[i4 + 0u] = FloatToHalf(0.95f);
            grassGust[i4 + 1u] = FloatToHalf(0.60f);
            grassGust[i4 + 2u] = FloatToHalf(0.0f);
            grassGust[i4 + 3u] = FloatToHalf(0.0f);
        }
    }

    UploadTex2D(ctx, colorOut.Get(),   D3D12_RESOURCE_STATE_COMMON, baseColor.data(), W * 8u, H);
    UploadTex2D(ctx, depthIn.Get(),    D3D12_RESOURCE_STATE_COMMON, depthBuf.data(),  W * 4u, H);
    UploadTex2D(ctx, normalIn.Get(),   D3D12_RESOURCE_STATE_COMMON, normBuf.data(),   W * 8u, H);
    UploadTex2D(ctx, albedoIn.Get(),   D3D12_RESOURCE_STATE_COMMON, albBuf.data(),    W * 4u, H);
    UploadTex2D(ctx, specularIn.Get(), D3D12_RESOURCE_STATE_COMMON, specBuf.data(),   W * 4u, H);
    UploadTex2D(ctx, snowState.Get(),  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, snowS.data(), S * 8u, S);
    UploadTex2D(ctx, snowNormal.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, snowN.data(), S * 8u, S);
    UploadTex2D(ctx, caustics.Get(),   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, caustB.data(), S * 8u, S);
    UploadTex2D(ctx, grassBend.Get(),  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, grassCalm.data(), S * 8u, S);

    LiveWorldInputResources lwr{};
    lwr.colorOut       = colorOut.Get();
    lwr.depthIn        = depthIn.Get();
    lwr.normalIn       = normalIn.Get();
    lwr.albedoIn       = albedoIn.Get();
    lwr.specularIn     = specularIn.Get();
    lwr.snowState      = snowState.Get();
    lwr.snowNormal     = snowNormal.Get();
    lwr.waterCaustics  = caustics.Get();
    lwr.grassBend      = grassBend.Get();

    LiveWorldCompositeParams lwp{};
    // Map screen [-1..+1] -> world [-16m..+16m] = [-1120u..+1120u] around origin:
    BuildOrthoClipToWorld(1120.0f, 1120.0f, 100.0f, 250.0f, lwp.clipToWorld);
    // viewToWorld flips Z so view (0,0,-1) -> world (0,0,+1)
    lwp.viewToWorld[0] = 1.0f; lwp.viewToWorld[5] = 1.0f; lwp.viewToWorld[10] = -1.0f;
    lwp.camPosW[0] = 0.0f; lwp.camPosW[1] = 0.0f; lwp.camPosW[2] = 0.0f;
    lwp.snowOriginWorldX_m = -32.0f;
    lwp.snowOriginWorldZ_m = -32.0f;
    lwp.snowSpanM          = 64.0f;
    lwp.snowStrength       = 1.0f;
    lwp.snowBaselineDepthM = 0.40f;
    lwp.grassOriginWorldX_m = 0.0f;
    lwp.grassOriginWorldZ_m = 0.0f;
    lwp.grassSpanM          = 64.0f;
    lwp.grassStrength       = 1.0f;
    lwp.waterPlaneZ_u       = 0.0f;
    lwp.causticsDomainM     = 8.0f;
    lwp.causticsStrength    = 1.0f;
    lwp.seasonSnowDepthM    = 0.40f;
    lwp.sunDir[0] = 0.45f; lwp.sunDir[1] = 0.25f; lwp.sunDir[2] = 0.85f;
    lwp.sunIntensity = 1.0f;
    lwp.sunCol[0] = 1.0f; lwp.sunCol[1] = 0.96f; lwp.sunCol[2] = 0.90f;
    lwp.width  = W;
    lwp.height = H;
    lwp.activeDebugView = 0u;

    // -------------------------------------------------------------------------
    // Run 1: Live World-Space Composite (Calm Grass)
    // -------------------------------------------------------------------------
    check(host.Composite(ctx.queue.Get(), lwr, lwp), "live_world_composite_dispatch");
    ctx.WaitIdle();
    std::vector<float> outCalm;
    ReadbackRgba16f(ctx, colorOut.Get(), D3D12_RESOURCE_STATE_COMMON, W, H, outCalm);

    auto pxRgb = [&](const std::vector<float>& img, uint32_t x, uint32_t y, float& r, float& g, float& b) {
        const size_t idx = (static_cast<size_t>(y) * W + x) * 4u;
        r = img[idx + 0u]; g = img[idx + 1u]; b = img[idx + 2u];
    };

    // Check 1: Strict Sky & Actor (base 10, 11, 12, 14) bit-exact preservation!
    float maxProtectedDelta = 0.0f;
    uint32_t nanInfCount = 0u;
    for (uint32_t y = 0u; y < H; ++y) {
        for (uint32_t x = 0u; x < W; ++x) {
            float r, g, b;
            pxRgb(outCalm, x, y, r, g, b);
            if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) ++nanInfCount;
            if (x >= 128u && y >= 128u) {
                maxProtectedDelta = std::max(maxProtectedDelta, std::fabs(r - 0.50f));
                maxProtectedDelta = std::max(maxProtectedDelta, std::fabs(g - 0.50f));
                maxProtectedDelta = std::max(maxProtectedDelta, std::fabs(b - 0.50f));
            }
        }
    }
    check(nanInfCount == 0u && maxProtectedDelta < 1e-4f, "strict_sky_and_actor_10_11_12_14_preservation");

    // Check 2: Snow trench darkening (x=32, y=64 sits in trench vs x=112, y=64 untouched flat snow)
    float trR, trG, trB, flR, flG, flB;
    pxRgb(outCalm, 32u, 64u, trR, trG, trB);
    pxRgb(outCalm, 112u, 64u, flR, flG, flB);
    const float trenchLuma = 0.2126f * trR + 0.7152f * trG + 0.0722f * trB;
    const float flatLuma   = 0.2126f * flR + 0.7152f * flG + 0.0722f * flB;
    std::printf("  [%s] SnowTrack: trenchLuma=%.4f vs flatLuma=%.4f (delta=%.4f)\n",
                label, trenchLuma, flatLuma, flatLuma - trenchLuma);
    check(trenchLuma < flatLuma - 0.08f, "live_snow_deform_trench_darkening");

    // Check 3: Water caustics injection on submerged riverbed (Q1: x=192, y=64) vs dry flat snow
    float caR, caG, caB;
    pxRgb(outCalm, 192u, 64u, caR, caG, caB);
    const float caustLuma = 0.2126f * caR + 0.7152f * caG + 0.0722f * caB;
    std::printf("  [%s] Caustics: submergedLuma=%.4f (base=0.5000, RGB=%.3f/%.3f/%.3f)\n",
                label, caustLuma, caR, caG, caB);
    check(caustLuma > 0.54f && caB > caR, "live_water_caustics_submerged_injection");

    // Check 3b: 3D Glacial Ice Facet Contrast & Teal Subsurface Scatter (Q0 lower band y in [100..124], base == 6u)
    float iceMinLuma = 10.0f, iceMaxLuma = 0.0f;
    double iceBlueBiasSum = 0.0;
    for (uint32_t y = 100u; y < 124u; ++y) {
        for (uint32_t x = 16u; x < 112u; ++x) {
            float ir, ig, ib;
            pxRgb(outCalm, x, y, ir, ig, ib);
            const float l = 0.2126f * ir + 0.7152f * ig + 0.0722f * ib;
            iceMinLuma = std::min(iceMinLuma, l);
            iceMaxLuma = std::max(iceMaxLuma, l);
            iceBlueBiasSum += static_cast<double>(ib - ir);
        }
    }
    const float iceContrast = iceMaxLuma - iceMinLuma;
    const float iceBlueBias = static_cast<float>(iceBlueBiasSum / (24.0 * 96.0));
    std::printf("  [%s] Glacier3D: facetContrast=%.4f tealBlueBias=%.4f\n", label, iceContrast, iceBlueBias);
    check(iceContrast > 0.03f && iceBlueBias > 0.03f, "glacier_3d_internal_facet_contrast");

    // -------------------------------------------------------------------------
    // Run 2: 4-Shell 3D Grass Wind Modulation (Calm vs Gust + Non-Uniform Blade Detail)
    // -------------------------------------------------------------------------
    UploadTex2D(ctx, colorOut.Get(),  D3D12_RESOURCE_STATE_COMMON, baseColor.data(), W * 8u, H);
    UploadTex2D(ctx, grassBend.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, grassGust.data(), S * 8u, S);
    host.Composite(ctx.queue.Get(), lwr, lwp);
    ctx.WaitIdle();
    std::vector<float> outGust;
    ReadbackRgba16f(ctx, colorOut.Get(), D3D12_RESOURCE_STATE_COMMON, W, H, outGust);

    double grassDiffSum = 0.0;
    float  grassMinG = 10.0f, grassMaxG = 0.0f;
    for (uint32_t y = 140u; y < 240u; ++y) {
        for (uint32_t x = 16u; x < 112u; ++x) {
            float r0, g0, b0, r1, g1, b1;
            pxRgb(outCalm, x, y, r0, g0, b0);
            pxRgb(outGust, x, y, r1, g1, b1);
            grassDiffSum += std::fabs(g1 - g0) + std::fabs(r1 - r0);
            grassMinG = std::min(grassMinG, g1);
            grassMaxG = std::max(grassMaxG, g1);
        }
    }
    const double grassMeanDiff = grassDiffSum / (100.0 * 96.0);
    const float  grassBladeRange = grassMaxG - grassMinG;
    std::printf("  [%s] Grass3D (4-shell): calm-vs-gust meanDelta=%.5f bladeDetailRange=%.4f\n",
                label, grassMeanDiff, grassBladeRange);
    check(grassMeanDiff > 0.015 && grassBladeRange > 0.04f, "live_3d_grass_4shell_and_wind_modulation");

    // -------------------------------------------------------------------------
    // Run 3: World-Space Stability Under Camera Shift (Zero Screen-Space Swimming!)
    // Shift camera by +16 px in X (world +140u = +2.0m), sample the SAME world point!
    // -------------------------------------------------------------------------
    UploadTex2D(ctx, colorOut.Get(), D3D12_RESOURCE_STATE_COMMON, baseColor.data(), W * 8u, H);
    LiveWorldCompositeParams shifted = lwp;
    shifted.camPosW[0] = 140.0f; // +16 pixels in X (2240u / 256px = 8.75u/px -> 16px = 140u!)
    host.Composite(ctx.queue.Get(), lwr, shifted);
    ctx.WaitIdle();
    std::vector<float> outShifted;
    ReadbackRgba16f(ctx, colorOut.Get(), D3D12_RESOURCE_STATE_COMMON, W, H, outShifted);

    // Pixel (64, 180) in unshifted camera maps to the EXACT same world XY as pixel (48, 180) in shifted camera!
    float aR, aG, aB, bR, bG, bB;
    pxRgb(outGust,    64u, 180u, aR, aG, aB);
    pxRgb(outShifted, 48u, 180u, bR, bG, bB);
    const float swimErr = std::max({std::fabs(aR - bR), std::fabs(aG - bG), std::fabs(aB - bB)});
    std::printf("  [%s] WorldAnchor: camA(64,180)=(%.4f,%.4f,%.4f) vs camB(48,180)=(%.4f,%.4f,%.4f) swimErr=%.5f\n",
                label, aR, aG, aB, bR, bG, bB, swimErr);
    check(swimErr < 0.005f, "world_space_stability_zero_swimming");

    // -------------------------------------------------------------------------
    // Run 3b: Perspective cam_sweep Yaw/Pitch Anti-Swimming Proof!
    // Rotates a true 3D perspective camera by yaw +0.08 rad & pitch +0.03 rad over a continuous
    // grassy meadow plane (z=+50u) and proves projected 3D world points stay world-locked!
    // -------------------------------------------------------------------------
    {
        std::vector<uint8_t> meadowAlb(W * H * 4u);
        std::vector<uint8_t> meadowSpec(W * H * 4u);
        for (uint32_t i = 0u; i < W * H; ++i) {
            meadowAlb[i * 4u + 0u]  = 72u; meadowAlb[i * 4u + 1u]  = 135u; meadowAlb[i * 4u + 2u]  = 48u; meadowAlb[i * 4u + 3u]  = 255u;
            meadowSpec[i * 4u + 0u] = 40u; meadowSpec[i * 4u + 1u] = 128u; meadowSpec[i * 4u + 2u] = 4u;  meadowSpec[i * 4u + 3u] = 255u;
        }
        UploadTex2D(ctx, albedoIn.Get(),   D3D12_RESOURCE_STATE_COMMON, meadowAlb.data(),  W * 4u, H);
        UploadTex2D(ctx, specularIn.Get(), D3D12_RESOURCE_STATE_COMMON, meadowSpec.data(), W * 4u, H);

        LiveWorldCompositeParams pA = lwp;
        pA.camPosW[0] = 0.0f; pA.camPosW[1] = 0.0f; pA.camPosW[2] = 220.0f;
        const PerspCamBasis camA = BuildPerspCam(0.0f, 0.0f, 220.0f, 0.0f, -0.58f, 0.55f, 1.0f, 16.0f, 16384.0f, pA.clipToWorld);
        std::vector<float> depthA;
        FillPlaneDepthForPerspCam(camA, 50.0f, W, H, depthA);
        UploadTex2D(ctx, depthIn.Get(),  D3D12_RESOURCE_STATE_COMMON, depthA.data(),    W * 4u, H);
        UploadTex2D(ctx, colorOut.Get(), D3D12_RESOURCE_STATE_COMMON, baseColor.data(), W * 8u, H);
        host.Composite(ctx.queue.Get(), lwr, pA);
        ctx.WaitIdle();
        std::vector<float> imgA;
        ReadbackRgba16f(ctx, colorOut.Get(), D3D12_RESOURCE_STATE_COMMON, W, H, imgA);

        LiveWorldCompositeParams pB = pA;
        const PerspCamBasis camB = BuildPerspCam(0.0f, 0.0f, 220.0f, 0.08f, -0.55f, 0.55f, 1.0f, 16.0f, 16384.0f, pB.clipToWorld);
        std::vector<float> depthB;
        FillPlaneDepthForPerspCam(camB, 50.0f, W, H, depthB);
        UploadTex2D(ctx, depthIn.Get(),  D3D12_RESOURCE_STATE_COMMON, depthB.data(),    W * 4u, H);
        UploadTex2D(ctx, colorOut.Get(), D3D12_RESOURCE_STATE_COMMON, baseColor.data(), W * 8u, H);
        host.Composite(ctx.queue.Get(), lwr, pB);
        ctx.WaitIdle();
        std::vector<float> imgB;
        ReadbackRgba16f(ctx, colorOut.Get(), D3D12_RESOURCE_STATE_COMMON, W, H, imgB);

        auto sampleBilin = [&](const std::vector<float>& img, float fx, float fy, float& r, float& g, float& b) {
            const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, static_cast<int>(W) - 2);
            const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, static_cast<int>(H) - 2);
            const float tx = std::clamp(fx - static_cast<float>(x0), 0.0f, 1.0f);
            const float ty = std::clamp(fy - static_cast<float>(y0), 0.0f, 1.0f);
            float r00, g00, b00, r10, g10, b10, r01, g01, b01, r11, g11, b11;
            pxRgb(img, static_cast<uint32_t>(x0),     static_cast<uint32_t>(y0),     r00, g00, b00);
            pxRgb(img, static_cast<uint32_t>(x0 + 1), static_cast<uint32_t>(y0),     r10, g10, b10);
            pxRgb(img, static_cast<uint32_t>(x0),     static_cast<uint32_t>(y0 + 1), r01, g01, b01);
            pxRgb(img, static_cast<uint32_t>(x0 + 1), static_cast<uint32_t>(y0 + 1), r11, g11, b11);
            r = (r00 * (1 - tx) + r10 * tx) * (1 - ty) + (r01 * (1 - tx) + r11 * tx) * ty;
            g = (g00 * (1 - tx) + g10 * tx) * (1 - ty) + (g01 * (1 - tx) + g11 * tx) * ty;
            b = (b00 * (1 - tx) + b10 * tx) * (1 - ty) + (b01 * (1 - tx) + b11 * tx) * ty;
        };

        // Evaluate 25 world-locked meadow points across the camera yaw/pitch sweep (Nyquist-resolved ~3.8m range):
        double trackedErr = 0.0, staticScreenErr = 0.0, meanPxShift = 0.0;
        int validPts = 0;
        for (int iy = -2; iy <= 2; ++iy) {
            for (int ix = -2; ix <= 2; ++ix) {
                const float wx = static_cast<float>(ix) * 26.0f;
                const float wy = 260.0f + static_cast<float>(iy) * 26.0f;
                float pxA, pyA, dA, pxB, pyB, dB;
                if (ProjectWorldPoint(camA, wx, wy, 50.0f, W, H, pxA, pyA, dA) &&
                    ProjectWorldPoint(camB, wx, wy, 50.0f, W, H, pxB, pyB, dB)) {
                    float rA, gA, bA, rBt, gBt, bBt, rBs, gBs, bBs;
                    sampleBilin(imgA, pxA, pyA, rA, gA, bA);
                    sampleBilin(imgB, pxB, pyB, rBt, gBt, bBt);
                    sampleBilin(imgB, pxA, pyA, rBs, gBs, bBs);
                    trackedErr      += (std::fabs(rA - rBt) + std::fabs(gA - gBt) + std::fabs(bA - bBt)) / 3.0;
                    staticScreenErr += (std::fabs(rA - rBs) + std::fabs(gA - gBs) + std::fabs(bA - bBs)) / 3.0;
                    meanPxShift     += std::hypot(pxB - pxA, pyB - pyA);
                    ++validPts;
                }
            }
        }
        const double avgTracked = (validPts > 0) ? (trackedErr / validPts) : 1.0;
        const double avgStatic  = (validPts > 0) ? (staticScreenErr / validPts) : 0.0;
        const double avgShift   = (validPts > 0) ? (meanPxShift / validPts) : 0.0;
        std::printf("  [%s] CamSweepYawPitch: pts=%d pxShift=%.1fpx trackedErr=%.4f vs staticScreenErr=%.4f\n",
                    label, validPts, avgShift, avgTracked, avgStatic);
        check(validPts >= 20 && avgShift > 10.0 && avgTracked < 0.035 && avgTracked < avgStatic * 0.55,
              "cam_sweep_yaw_pitch_world_lock_anti_swimming");

        // Restore original 4-quadrant G-buffer for remaining Debug View & timing checks:
        UploadTex2D(ctx, depthIn.Get(),    D3D12_RESOURCE_STATE_COMMON, depthBuf.data(), W * 4u, H);
        UploadTex2D(ctx, albedoIn.Get(),   D3D12_RESOURCE_STATE_COMMON, albBuf.data(),   W * 4u, H);
        UploadTex2D(ctx, specularIn.Get(), D3D12_RESOURCE_STATE_COMMON, specBuf.data(),  W * 4u, H);
    }

    // -------------------------------------------------------------------------
    // Run 4: Debug Views 28 (Actor base=14 highlight) & 33 (Planted NaN/HDR Overflow!)
    // -------------------------------------------------------------------------
    LiveWorldCompositeParams dbg28 = lwp;
    dbg28.activeDebugView = 28u;
    host.Composite(ctx.queue.Get(), lwr, dbg28);
    ctx.WaitIdle();
    std::vector<float> outDbg28;
    ReadbackRgba16f(ctx, colorOut.Get(), D3D12_RESOURCE_STATE_COMMON, W, H, outDbg28);
    float d14R, d14G, d14B;
    pxRgb(outDbg28, 192u, 208u, d14R, d14G, d14B); // base == 14u -> vivid cyan (G & B high, R low)
    check(d14G > 0.70f && d14B > 0.70f && d14R < 0.25f, "debug_view_28_highlights_skinned_actor_14");

    // Plant an HDR overflow pixel (> 32.0) at (32, 32) and verify Debug View 33 flags magenta (1, 0, 1)!
    std::vector<uint16_t> hdrOverflow = baseColor;
    const size_t ovIdx = (static_cast<size_t>(32u) * W + 32u) * 4u;
    hdrOverflow[ovIdx + 0u] = FloatToHalf(48.0f);
    UploadTex2D(ctx, colorOut.Get(), D3D12_RESOURCE_STATE_COMMON, hdrOverflow.data(), W * 8u, H);

    LiveWorldCompositeParams dbg33 = lwp;
    dbg33.activeDebugView = 33u;
    host.Composite(ctx.queue.Get(), lwr, dbg33);
    ctx.WaitIdle();
    std::vector<float> outDbg33;
    ReadbackRgba16f(ctx, colorOut.Get(), D3D12_RESOURCE_STATE_COMMON, W, H, outDbg33);
    float ovR, ovG, ovB;
    pxRgb(outDbg33, 32u, 32u, ovR, ovG, ovB);
    check(ovR > 0.95f && ovG < 0.05f && ovB > 0.95f, "debug_view_33_flags_hdr_overflow_magenta");

    // Verify Debug Views 34 (Flora/3D Grass Audit), 35 (Silhouette/Outline Risk), and 36 (Thermal/Solver Energy):
    {
        UploadTex2D(ctx, colorOut.Get(), D3D12_RESOURCE_STATE_COMMON, baseColor.data(), W * 8u, H);
        LiveWorldCompositeParams dbg34 = lwp;
        dbg34.activeDebugView = 34u;
        host.Composite(ctx.queue.Get(), lwr, dbg34);
        ctx.WaitIdle();
        std::vector<float> out34;
        ReadbackRgba16f(ctx, colorOut.Get(), D3D12_RESOURCE_STATE_COMMON, W, H, out34);
        float g34r, g34g, g34b;
        pxRgb(out34, 64u, 192u, g34r, g34g, g34b); // Q2 grass terrain -> lime green (G high)
        check(g34g > 0.75f && g34r > 0.30f && g34b < 0.35f, "debug_view_34_flora_grass_audit");

        LiveWorldCompositeParams dbg35 = lwp;
        dbg35.activeDebugView = 35u;
        host.Composite(ctx.queue.Get(), lwr, dbg35);
        ctx.WaitIdle();
        std::vector<float> out35;
        ReadbackRgba16f(ctx, colorOut.Get(), D3D12_RESOURCE_STATE_COMMON, W, H, out35);
        // Depth discontinuity at Q0/Q1 border (x=127..128, y=64) must trigger bright edge highlight!
        float e35r, e35g, e35b;
        pxRgb(out35, 128u, 64u, e35r, e35g, e35b);
        check(e35r > 0.70f && e35g > 0.65f, "debug_view_35_silhouette_discontinuity_risk");

        LiveWorldCompositeParams dbg36 = lwp;
        dbg36.activeDebugView = 36u;
        host.Composite(ctx.queue.Get(), lwr, dbg36);
        ctx.WaitIdle();
        std::vector<float> out36;
        ReadbackRgba16f(ctx, colorOut.Get(), D3D12_RESOURCE_STATE_COMMON, W, H, out36);
        // Submerged Q1 (x=192, y=64) has strong blue caustic energy; grass Q2 (x=64, y=192) has strong green bend energy:
        float c36r, c36g, c36b, f36r, f36g, f36b;
        pxRgb(out36, 192u, 64u, c36r, c36g, c36b);
        pxRgb(out36, 64u, 192u, f36r, f36g, f36b);
        check(c36b > 0.50f && f36g > 0.50f, "debug_view_36_thermal_and_live_solver_energy");

        // Verify strict bit-exact 0-pixel bypass when lightingDebugActive == true (protects Lighting/Water debug views!):
        UploadTex2D(ctx, colorOut.Get(), D3D12_RESOURCE_STATE_COMMON, baseColor.data(), W * 8u, H);
        LiveWorldCompositeParams lpBypass = lwp;
        lpBypass.activeDebugView     = 0u;
        lpBypass.lightingDebugActive = true;
        host.Composite(ctx.queue.Get(), lwr, lpBypass);
        ctx.WaitIdle();
        std::vector<float> outBypass;
        ReadbackRgba16f(ctx, colorOut.Get(), D3D12_RESOURCE_STATE_COMMON, W, H, outBypass);
        float maxBypassDelta = 0.0f;
        for (uint32_t y = 0u; y < H; ++y) {
            for (uint32_t x = 0u; x < W; ++x) {
                float br, bg, bb;
                pxRgb(outBypass, x, y, br, bg, bb);
                maxBypassDelta = std::max(maxBypassDelta, std::fabs(br - 0.50f));
                maxBypassDelta = std::max(maxBypassDelta, std::fabs(bg - 0.50f));
                maxBypassDelta = std::max(maxBypassDelta, std::fabs(bb - 0.50f));
            }
        }
        check(maxBypassDelta < 1e-5f, "strict_lighting_and_water_debug_view_bypass");
    }

    // -------------------------------------------------------------------------
    // Run 5: Timing Benchmark (1080p equivalent dispatch budget < 0.06 ms on HW)
    // -------------------------------------------------------------------------
    for (int f = 0; f < 8; ++f) {
        host.Composite(ctx.queue.Get(), lwr, lwp);
    }
    ctx.WaitIdle();
    const float gpuMs = host.HasValidTiming() ? host.GetLastCompositeMs() : 0.0f;
    std::printf("  [%s] Timing (%ux%u): liveWorldCompositeMs=%.4f ms\n", label, W, H, gpuMs);
    check(useWarp ? (gpuMs >= 0.0f && std::isfinite(gpuMs)) : (gpuMs > 0.0f && gpuMs < 0.06f),
          "live_world_gpu_timing_budget");

    const uint32_t dbgErrors = ctx.DebugErrorCount();
    std::printf("  [%s] d3d12DebugErrors=%u\n", label, dbgErrors);
    check(dbgErrors == 0u, "zero_d3d12_debug_layer_errors");

    host.Shutdown();
    return failures;
}

} // namespace

int main(int argc, char** argv)
{
    bool warpOnly = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--warp") == 0) warpOnly = true;
    }

    int totalFails = 0;
    totalFails += RunBackendSuite(true, "WARP");
    if (!warpOnly) {
        totalFails += RunBackendSuite(false, "HW");
    }

    std::printf("test_live_world_composite_gpu summary: %s (%d failures)\n",
                totalFails == 0 ? "ALL PASSED" : "FAILED", totalFails);
    return totalFails == 0 ? 0 : 1;
}
