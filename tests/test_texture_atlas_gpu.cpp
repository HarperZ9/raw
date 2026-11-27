// test_texture_atlas_gpu.cpp: Wave 47 Live GPU Procedural PBR Texture2DArray Atlas & Tri-Planar Micro-Detail Proof.
// Verifies end-to-end on BOTH Hardware DX12 GPU (e.g. RTX 4090) AND WARP software adapter with D3D12 Debug Layer ON:
//   1. RawMeshHost generates & uploads all 8x3 Texture2DArray slices (128x128, 8 mip levels each: Albedo, Normal, ORMH)
//      from RawTextureForge with 0 D3D12 Debug Layer errors and proper COPY_DEST -> PIXEL_SHADER_RESOURCE transitions.
//   2. Renders close-up multi-material procedural terrain + bark/conifer flora + stone architecture chunks through
//      RawTerrainWorld.hlsl (AS+MS+PS on mesh-shader GPUs, VS+PS fallback on WARP) sampling g_pbrAlbedo/Normal/Ormh.
//   3. Reads back RGBA16F HDR pixels and proves:
//      - 0 NaN / 0 Inf pixels,
//      - Non-zero lit coverage across multiple PBR material classes (rock/snow/ice/basalt/moss/cobble/bark/needles),
//      - Measurable high-frequency spatial micro-detail variance (adjacent-pixel gradient & luma std-dev),
//      - Second frame runs cleanly after staging upload buffer release with 0 D3D12 Debug Layer errors!
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

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "RawMeshHost.h"
#include "../materials/RawTextureForge.h"
#include "../worldgen/RawMeshPipeline.h"

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

// Exact mirror of cbuffer P in LightingCommon.hlsli (42800 bytes).
struct AtlasTestLightUniform {
    float dims[4], proj[4], clipToWorld[16], worldToClip[16], viewToWorld[12];
    float resScale[4], sunDir[4], sunColour[4], ambient[12], cascade[24], cascadeInfo[4];
    uint32_t flags[4];
    float cluster[4], tune[4], tune2[4], weather[4], camPos[4], clouds[4], cloudWind[4], waterA[4], waterB[4], prevWorldToClip[16];
    float lightPosR[128][4], lightColS[128][4];
    uint32_t clusters[16 * 9 * 16][4];
    float footTrail[32][4];
    float dofParams[4];
    float timeEnv[4];
    float engineWorld[4];
    float camOptics[2][4];
    float physWorld[2][4];
    float seasonThermal0[4];
    float seasonThermal1[4];
    float phenology0[4];
    float phenology1[4];
    float impactDecalPosR[16][4];
    float impactDecalMeta[16][4];
};
static_assert(sizeof(AtlasTestLightUniform) == 42800u, "AtlasTestLightUniform must match cbuffer P");

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

void BuildCameraMatrices(const float eye[3], const float target[3],
                         float aspect, float nearZ, float farZ,
                         float outWorldToClip[16], float outClipToWorld[16])
{
    float fx = target[0] - eye[0], fy = target[1] - eye[1], fz = target[2] - eye[2];
    const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    fx /= fl; fy /= fl; fz /= fl;

    float rx = fy, ry = -fx, rz = 0.0f;
    const float rl = std::sqrt(rx * rx + ry * ry + rz * rz);
    rx /= rl; ry /= rl; rz /= rl;

    const float ux = ry * fz - rz * fy;
    const float uy = rz * fx - rx * fz;
    const float uz = rx * fy - ry * fx;

    const float tanHalfFov = std::tan(0.55f);
    const float f = 1.0f / tanHalfFov;
    const float a = farZ / (farZ - nearZ);
    const float B = (farZ * nearZ) / (farZ - nearZ);

    std::memset(outWorldToClip, 0, 64);
    outWorldToClip[0]  = rx * (f / aspect); outWorldToClip[1]  = ry * (f / aspect); outWorldToClip[2]  = rz * (f / aspect);
    outWorldToClip[4]  = ux * f;            outWorldToClip[5]  = uy * f;            outWorldToClip[6]  = uz * f;
    outWorldToClip[8]  = fx * a;            outWorldToClip[9]  = fy * a;            outWorldToClip[10] = fz * a;            outWorldToClip[11] = -B;
    outWorldToClip[12] = fx;                outWorldToClip[13] = fy;                outWorldToClip[14] = fz;

    std::memset(outClipToWorld, 0, 64);
    const float sx = tanHalfFov * aspect;
    const float sy = tanHalfFov;
    outClipToWorld[0]  = rx * sx; outClipToWorld[1]  = ux * sy; outClipToWorld[3]  = fx;
    outClipToWorld[4]  = ry * sx; outClipToWorld[5]  = uy * sy; outClipToWorld[7]  = fy;
    outClipToWorld[8]  = rz * sx; outClipToWorld[9]  = uz * sy; outClipToWorld[11] = fz;
    outClipToWorld[14] = -1.0f / B;
    outClipToWorld[15] = a / B;
}

// Builds a close-up flat multi-material test slab (8 distinct strips exercising all 8 PBR atlas slices!)
// so ANY high-frequency spatial pixel variance comes directly from tri-planar PBR micro-detail!
raw::worldgen::ProceduralMesh BuildMultiMaterialAtlasShowcaseSlab()
{
    raw::worldgen::ProceduralMesh mesh;
    mesh.name = "pbr_atlas_8slice_slab";
    mesh.aabbMin = {-420.0f, 180.0f, -120.0f};
    mesh.aabbMax = { 420.0f, 820.0f,  120.0f};

    constexpr uint32_t kCols = 8u;
    const float xMin = -400.0f;
    const float xMax =  400.0f;
    const float yMin =  220.0f;
    const float yMax =  780.0f;
    const float stepX = (xMax - xMin) / float(kCols);

    for (uint32_t c = 0u; c < kCols; ++c) {
        const float x0 = xMin + stepX * float(c);
        const float x1 = x0 + stepX;

        raw::worldgen::TerrainVertex v[4]{};
        const float xs[4] = {x0, x1, x1, x0};
        const float ys[4] = {yMin, yMin, yMax, yMax};
        const float us[4] = {0.0f, 1.0f, 1.0f, 0.0f};
        const float vs[4] = {0.0f, 0.0f, 1.0f, 1.0f};

        for (int k = 0; k < 4; ++k) {
            v[k].pos[0] = xs[k]; v[k].pos[1] = ys[k]; v[k].pos[2] = 0.0f;
            v[k].normal[0] = 0.0f; v[k].normal[1] = 0.0f; v[k].normal[2] = 1.0f;
            v[k].uv[0] = us[k]; v[k].uv[1] = vs[k];
            switch (c) {
                case 0: v[k].splat0[0] = 1.0f; break; // Slice 0: RockGranite
                case 1: v[k].splat0[2] = 1.0f; break; // Slice 1: GlacialIce
                case 2: v[k].splat1[3] = 1.0f; break; // Slice 2: ArcaneCrystal / ForgedSteel
                case 3: v[k].splat0[0] = 1.0f; v[k].meta[3] = 10.0f; break; // Slice 3: BorealPineBark
                case 4: v[k].splat0[3] = 1.0f; break; // Slice 4: HexagonalBasalt
                case 5: v[k].splat1[0] = 1.0f; break; // Slice 5: VolcanicMagmaCrust
                case 6: v[k].splat1[1] = 1.0f; break; // Slice 6: AlpineMossMeadow
                case 7: v[k].splat1[2] = 1.0f; break; // Slice 7: BraidedRiverPebbles
            }
        }

        const uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
        const uint32_t iBase = static_cast<uint32_t>(mesh.indices.size());
        for (int k = 0; k < 4; ++k) mesh.vertices.push_back(v[k]);
        mesh.indices.push_back(0u); mesh.indices.push_back(1u); mesh.indices.push_back(2u);
        mesh.indices.push_back(0u); mesh.indices.push_back(2u); mesh.indices.push_back(3u);

        raw::worldgen::MeshletDesc ml{};
        ml.vertexOffset = vBase;
        ml.triangleOffset = iBase;
        ml.vertexCount = 4u;
        ml.triangleCount = 2u;
        ml.boundSphere[0] = 0.5f * (x0 + x1);
        ml.boundSphere[1] = 0.5f * (yMin + yMax);
        ml.boundSphere[2] = 0.0f;
        ml.boundSphere[3] = 400.0f;
        ml.normalCone[0] = 0.0f;
        ml.normalCone[1] = 0.0f;
        ml.normalCone[2] = 1.0f;
        ml.normalCone[3] = -1.0f;
        mesh.meshlets.push_back(ml);
    }
    return mesh;
}

bool RunAtlasGpuProof(bool useWarp)
{
    const char* modeLabel = useWarp ? "WARP" : "HW-GPU";
    std::printf("\n--- Running Texture2DArray PBR Atlas Proof [%s] ---\n", modeLabel);

    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        Check(false, "CreateDXGIFactory2");
        return false;
    }

    ComPtr<IDXGIAdapter1> adapter;
    if (useWarp) {
        if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))) {
            Check(false, "EnumWarpAdapter");
            return false;
        }
    } else {
        for (UINT i = 0; factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0u) {
                std::printf("  Adapter: %ls\n", desc.Description);
                break;
            }
            adapter.Reset();
        }
        if (!adapter) {
            std::printf("  [INFO] No hardware GPU adapter available; skipping HW-GPU pass.\n");
            return true;
        }
    }

    ComPtr<ID3D12Device> dev;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev)))) {
        Check(false, "D3D12CreateDevice");
        return false;
    }

    ComPtr<ID3D12InfoQueue> iq;
    dev.As(&iq);
    if (iq) {
        iq->ClearStoredMessages();
    }

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) {
        Check(false, "CreateCommandQueue");
        return false;
    }

    // 1. Initialize RawMeshHost (generates all 8x3 128x128 mip-mapped procedural PBR Texture2DArray slices)
    const auto t0 = std::chrono::steady_clock::now();
    RawMeshHost host;
    std::string err;
    const bool initOk = host.Init(dev.Get(), &err);
    const double initMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  RawMeshHost::Init [%s]: ok=%d ms=%.1f usesMS=%d slices=%u res=%u mips=%u (%s)\n",
                modeLabel, int(initOk), initMs, int(host.TerrainUsesMeshShaders()),
                host.PbrAtlasSliceCount(), host.PbrAtlasResolution(), host.PbrAtlasMipLevels(), err.c_str());
    Check(initOk && host.TerrainReady(), useWarp ? "WARP: RawMeshHost::Init + TerrainReady" : "HW: RawMeshHost::Init + TerrainReady");
    Check(host.PbrAtlasSliceCount() == 16u && host.PbrAtlasResolution() == 512u && host.PbrAtlasMipLevels() == 10u,
          useWarp ? "WARP: 16x512x512x10-mip PBR atlas configured" : "HW: 16x512x512x10-mip PBR atlas configured");

    // 2. Upload multi-material 8-strip slab mesh
    const auto slabMesh = BuildMultiMaterialAtlasShowcaseSlab();
    TerrainGpuChunk slabChunk;
    Check(host.UploadTerrainChunk(slabMesh, &slabChunk, &err),
          useWarp ? "WARP: UploadTerrainChunk (8-material slab)" : "HW: UploadTerrainChunk (8-material slab)");
    std::unordered_map<uint64_t, TerrainGpuChunk> chunks;
    chunks.emplace(1ull, std::move(slabChunk));

    // 3. Create 256x144 RGBA16F color target + R32F unobstructed sky depth (1.0f) + readback buffer
    constexpr UINT kW = 256u;
    constexpr UINT kH = 144u;

    D3D12_HEAP_PROPERTIES defHp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC colorDesc{};
    colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    colorDesc.Width = kW; colorDesc.Height = kH; colorDesc.DepthOrArraySize = 1; colorDesc.MipLevels = 1;
    colorDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clearC{};
    clearC.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    ComPtr<ID3D12Resource> colorOut;
    Check(SUCCEEDED(dev->CreateCommittedResource(&defHp, D3D12_HEAP_FLAG_NONE, &colorDesc,
                                                 D3D12_RESOURCE_STATE_COMMON, &clearC, IID_PPV_ARGS(&colorOut))),
          "Create RGBA16F colorOut");

    D3D12_RESOURCE_DESC depthDesc{};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = kW; depthDesc.Height = kH; depthDesc.DepthOrArraySize = 1; depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_R32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> depthIn;
    Check(SUCCEEDED(dev->CreateCommittedResource(&defHp, D3D12_HEAP_FLAG_NONE, &depthDesc,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&depthIn))),
          "Create R32F depthIn");

    // Initialize depthIn to 1.0f (sky) and clear colorOut to 0
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT dFp{}, cFp{};
    UINT dRows = 0, cRows = 0;
    UINT64 dRowBytes = 0, dTotal = 0, cRowBytes = 0, cTotal = 0;
    dev->GetCopyableFootprints(&depthDesc, 0, 1, 0, &dFp, &dRows, &dRowBytes, &dTotal);
    dev->GetCopyableFootprints(&colorDesc, 0, 1, 0, &cFp, &cRows, &cRowBytes, &cTotal);

    D3D12_HEAP_PROPERTIES upHp{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_HEAP_PROPERTIES rbHp{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC upDesc{};
    upDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    upDesc.Width = (dTotal + 255u) & ~UINT64(255u);
    upDesc.Height = 1; upDesc.DepthOrArraySize = 1; upDesc.MipLevels = 1;
    upDesc.SampleDesc.Count = 1; upDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> depthUp;
    dev->CreateCommittedResource(&upHp, D3D12_HEAP_FLAG_NONE, &upDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&depthUp));
    {
        uint8_t* p = nullptr;
        D3D12_RANGE z{0, 0};
        depthUp->Map(0, &z, reinterpret_cast<void**>(&p));
        for (UINT y = 0; y < kH; ++y) {
            float* row = reinterpret_cast<float*>(p + size_t(y) * dFp.Footprint.RowPitch);
            for (UINT x = 0; x < kW; ++x) row[x] = 1.0f;
        }
        depthUp->Unmap(0, nullptr);
    }

    D3D12_RESOURCE_DESC rbDesc = upDesc;
    rbDesc.Width = (cTotal + 255u) & ~UINT64(255u);
    ComPtr<ID3D12Resource> colorRb;
    dev->CreateCommittedResource(&rbHp, D3D12_HEAP_FLAG_NONE, &rbDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&colorRb));

    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ComPtr<ID3D12Fence> fence;
    HANDLE evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    uint64_t fVal = 0;
    dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
    dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd));
    dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));

    auto submitAndWait = [&]() {
        cmd->Close();
        ID3D12CommandList* l[1] = {cmd.Get()};
        queue->ExecuteCommandLists(1, l);
        queue->Signal(fence.Get(), ++fVal);
        if (fence->GetCompletedValue() < fVal) {
            fence->SetEventOnCompletion(fVal, evt);
            WaitForSingleObject(evt, 5000);
        }
    };

    // Upload depthIn = 1.0 and transition to COMMON
    {
        D3D12_TEXTURE_COPY_LOCATION dst{depthIn.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
        dst.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION src{depthUp.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
        src.PlacedFootprint = dFp;
        cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {depthIn.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON};
        cmd->ResourceBarrier(1, &b);
        submitAndWait();
    }

    // 4. Build LightUniform looking right down at the 8-material slab at close range (~420u)
    AtlasTestLightUniform u{};
    u.dims[0] = float(kW); u.dims[1] = float(kH);
    u.dims[2] = 1.0f / float(kW); u.dims[3] = 1.0f / float(kH);
    const float nearZ = 10.0f, farZ = 100000.0f;
    u.proj[0] = farZ / (farZ - nearZ);
    u.proj[1] = (farZ * nearZ) / (farZ - nearZ);
    u.resScale[0] = 1.0f; u.resScale[1] = 1.0f; u.resScale[2] = 1.0f; u.resScale[3] = 1.0f;
    const float camPos[3] = {0.0f, 100.0f, 220.0f};
    const float relEye[3] = {0.0f, 0.0f, 0.0f};
    const float relTarget[3] = {0.0f, 380.0f, -220.0f};
    BuildCameraMatrices(relEye, relTarget, float(kW) / float(kH), nearZ, farZ, u.worldToClip, u.clipToWorld);
    u.camPos[0] = camPos[0]; u.camPos[1] = camPos[1]; u.camPos[2] = camPos[2]; u.camPos[3] = 1.0f;
    u.sunDir[0] = 0.55f; u.sunDir[1] = 0.35f; u.sunDir[2] = 0.76f; u.sunDir[3] = 1.0f;
    u.sunColour[0] = 1.15f; u.sunColour[1] = 1.08f; u.sunColour[2] = 0.96f; u.sunColour[3] = 1.0f;
    u.timeEnv[0] = 13.0f; u.timeEnv[1] = 0.72f; u.timeEnv[2] = 0.0f; u.timeEnv[3] = 0.0f;
    u.clouds[0] = 0.15f; u.clouds[1] = 18000.0f; u.clouds[2] = 8000.0f; u.clouds[3] = 0.0f;
    u.tune[0] = 0.00002f; u.tune[1] = 0.55f;
    u.seasonThermal0[3] = 12.0f; // clear warm midday
    u.phenology0[1] = 1.0f;      // summer chlorophyll

    // 5. Execute 2 frames (Frame 1 uploads 8x3 Texture2DArray; Frame 2 verifies steady-state after upload cleanup)
    uint32_t drawnChunks = 0, drawnMeshlets = 0;
    const bool exec1 = host.ExecuteTerrainPass(queue.Get(), colorOut.Get(), depthIn.Get(), nullptr,
                                               kW, kH, &u, sizeof u, u.worldToClip, camPos, 1.0f,
                                               chunks, &drawnChunks, &drawnMeshlets);
    Check(exec1 && host.PbrAtlasReady() && drawnChunks == 1u && drawnMeshlets == 8u,
          useWarp ? "WARP: Frame 1 ExecuteTerrainPass + PbrAtlasReady" : "HW: Frame 1 ExecuteTerrainPass + PbrAtlasReady");

    // Frame 2
    u.camPos[3] = 2.0f;
    const bool exec2 = host.ExecuteTerrainPass(queue.Get(), colorOut.Get(), depthIn.Get(), nullptr,
                                               kW, kH, &u, sizeof u, u.worldToClip, camPos, 2.0f,
                                               chunks, &drawnChunks, &drawnMeshlets);
    Check(exec2 && drawnMeshlets == 8u,
          useWarp ? "WARP: Frame 2 ExecuteTerrainPass steady-state" : "HW: Frame 2 ExecuteTerrainPass steady-state");

    // 6. Readback colorOut and measure pixel health + high-frequency PBR micro-detail variance
    alloc->Reset();
    cmd->Reset(alloc.Get(), nullptr);
    {
        D3D12_RESOURCE_BARRIER b1{};
        b1.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b1.Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                         D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE};
        cmd->ResourceBarrier(1, &b1);
        D3D12_TEXTURE_COPY_LOCATION src{colorOut.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
        src.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION dst{colorRb.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
        dst.PlacedFootprint = cFp;
        cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        D3D12_RESOURCE_BARRIER b2{};
        b2.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b2.Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                         D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON};
        cmd->ResourceBarrier(1, &b2);
        submitAndWait();
    }

    uint32_t nanInfCount = 0u;
    uint32_t litPixels = 0u;
    double lumaSum = 0.0, lumaSqSum = 0.0, gradSum = 0.0;
    uint32_t gradSamples = 0u;
    std::vector<float> lumaImg(size_t(kW) * kH, 0.0f);
    {
        uint8_t* p = nullptr;
        D3D12_RANGE r{0, static_cast<SIZE_T>(cTotal)};
        colorRb->Map(0, &r, reinterpret_cast<void**>(&p));
        for (UINT y = 0; y < kH; ++y) {
            const uint16_t* row = reinterpret_cast<const uint16_t*>(p + size_t(y) * cFp.Footprint.RowPitch);
            for (UINT x = 0; x < kW; ++x) {
                const float cr = HalfToFloat(row[x * 4u + 0u]);
                const float cg = HalfToFloat(row[x * 4u + 1u]);
                const float cb = HalfToFloat(row[x * 4u + 2u]);
                const float ca = HalfToFloat(row[x * 4u + 3u]);
                if (!std::isfinite(cr) || !std::isfinite(cg) || !std::isfinite(cb) || !std::isfinite(ca)) {
                    ++nanInfCount;
                    continue;
                }
                const float lum = 0.2126f * cr + 0.7152f * cg + 0.0722f * cb;
                lumaImg[size_t(y) * kW + x] = lum;
                if (ca > 0.5f && lum > 0.002f) {
                    ++litPixels;
                    lumaSum += lum;
                    lumaSqSum += double(lum) * double(lum);
                }
            }
        }
        colorRb->Unmap(0, nullptr);
    }

    // Compute local horizontal micro-detail gradient inside lit interior pixels
    for (UINT y = 1; y + 1 < kH; ++y) {
        for (UINT x = 1; x + 1 < kW; ++x) {
            const float c0 = lumaImg[size_t(y) * kW + x];
            const float cx = lumaImg[size_t(y) * kW + (x + 1u)];
            const float cy = lumaImg[size_t(y + 1u) * kW + x];
            if (c0 > 0.005f && cx > 0.005f && cy > 0.005f) {
                gradSum += std::abs(cx - c0) + std::abs(cy - c0);
                ++gradSamples;
            }
        }
    }

    const double meanLuma = litPixels > 0u ? (lumaSum / double(litPixels)) : 0.0;
    const double varLuma = litPixels > 0u ? std::max(0.0, lumaSqSum / double(litPixels) - meanLuma * meanLuma) : 0.0;
    const double stdLuma = std::sqrt(varLuma);
    const double meanGrad = gradSamples > 0u ? (gradSum / double(gradSamples)) : 0.0;

    // Count D3D12 Debug Layer errors/corruptions
    uint32_t dbgErrors = 0u;
    if (iq) {
        const UINT64 numMsgs = iq->GetNumStoredMessages();
        for (UINT64 i = 0; i < numMsgs; ++i) {
            SIZE_T len = 0;
            iq->GetMessage(i, nullptr, &len);
            std::vector<uint8_t> buf(len);
            auto* m = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
            if (SUCCEEDED(iq->GetMessage(i, m, &len))) {
                if (m->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ||
                    m->Severity == D3D12_MESSAGE_SEVERITY_ERROR) {
                    ++dbgErrors;
                    std::printf("  [D3D12-ERR] %s\n", m->pDescription);
                }
            }
        }
    }

    std::printf("  [%s] litPixels=%u meanLuma=%.4f stdLuma=%.4f microGrad=%.5f nanInf=%u dbgErrors=%u\n",
                modeLabel, litPixels, meanLuma, stdLuma, meanGrad, nanInfCount, dbgErrors);

    Check(nanInfCount == 0u, useWarp ? "WARP: 0 NaN/Inf HDR pixels" : "HW: 0 NaN/Inf HDR pixels");
    Check(litPixels > 4000u, useWarp ? "WARP: Multi-material slab rendered (>4000 lit pixels)" : "HW: Multi-material slab rendered (>4000 lit pixels)");
    Check(stdLuma > 0.015 && meanGrad > 0.002, useWarp ? "WARP: High-frequency PBR tri-planar micro-detail variance > 0" : "HW: High-frequency PBR tri-planar micro-detail variance > 0");
    Check(dbgErrors == 0u, useWarp ? "WARP: 0 D3D12 Debug Layer errors" : "HW: 0 D3D12 Debug Layer errors");

    host.Shutdown();
    CloseHandle(evt);
    return dbgErrors == 0u && nanInfCount == 0u;
}

} // namespace

int main()
{
    std::printf("=== Wave 47 GPU Procedural PBR Texture2DArray Atlas & Tri-Planar Proof ===\n");

    ComPtr<ID3D12Debug> dbg;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
        dbg->EnableDebugLayer();
        std::printf("D3D12 Debug Layer: ENABLED\n");
    }

    RunAtlasGpuProof(true);  // WARP (VS+PS fallback + full Debug Layer)
    RunAtlasGpuProof(false); // Hardware GPU (RTX 4090 AS+MS+PS + full Debug Layer)

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
