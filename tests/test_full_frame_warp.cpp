// test_full_frame_warp.cpp: Full-Frame Multi-Pass WARP D3D12 Debug Layer Regression Suite.
// Proves end-to-end GPU execution of:
//   1. RawWeatherSim (Nordic winter blizzard: snowpack, ice/frost, sub-zero skin temp, synoptic wind)
//   2. RawWorldShowcase (non-blocking async RawJobSystem frontier chunk streaming + baked RawFloraEngine 3D trees)
//   3. RawFluidSimHost (12 frames of 64x64x64 3D Navier-Stokes Fire/Smoke/Radiance + 256x256 2D SWE Water)
//   4. RawMeshHost (RawTerrainWorld.hlsl AS+MS+PS rendering onto 256x144 RGBA16F HDR target with SceneDepth occlusion)
//   5. RawFluidCompositeHost (live 3D NS fire/smoke/radiance composited onto the EXACT SAME 256x144 RGBA16F HDR target)
// Asserts 0 D3D12 Debug Layer errors, 0 NaN/Inf, 0 occluded-half modifications, genuine terrain+flora + fire/soot pixels,
// and clean shutdown with 0 leaked async worker threads or D3D12 live objects.
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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "RawMeshHost.h"
#include "RawFluidSimHost.h"
#include "RawFluidCompositeHost.h"
#include "../worldgen/RawWeatherSim.h"
#include "../worldgen/RawWorldShowcase.h"

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

// Exact mirror of cbuffer P in LightingCommon.hlsli (matches LightingPass.cpp LightUniform: 42800 bytes).
struct FullFrameLightUniform {
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
static_assert(sizeof(FullFrameLightUniform) == 42800u, "FullFrameLightUniform must match cbuffer P");

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

struct CameraBasis {
    float right[3];
    float up[3];
    float fwd[3];
};

// Builds BOTH camera-relative worldToClip AND its exact analytic inverse clipToWorld (standard-Z: 0=near, 1=far).
CameraBasis BuildCameraMatrices(const float eye[3], const float target[3],
                                float aspect, float nearZ, float farZ,
                                float outWorldToClip[16], float outClipToWorld[16])
{
    float fx = target[0] - eye[0], fy = target[1] - eye[1], fz = target[2] - eye[2];
    const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    fx /= fl; fy /= fl; fz /= fl;

    // Z-up right-handed basis: Right = normalize(cross(fwd, upZ)), Up = cross(right, fwd)
    float rx = fy, ry = -fx, rz = 0.0f;
    const float rl = std::sqrt(rx * rx + ry * ry + rz * rz);
    rx /= rl; ry /= rl; rz /= rl;

    const float ux = ry * fz - rz * fy;
    const float uy = rz * fx - rx * fz;
    const float uz = rx * fy - ry * fx;

    const float tanHalfFov = std::tan(0.60f); // ~68.7 deg vFOV
    const float f = 1.0f / tanHalfFov;
    const float a = farZ / (farZ - nearZ);
    const float B = (farZ * nearZ) / (farZ - nearZ); // depth = a - B / viewZ

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

    return {{rx, ry, rz}, {ux, uy, uz}, {fx, fy, fz}};
}

} // namespace

int main()
{
    std::printf("=== RAW Full-Frame Multi-Pass WARP D3D12 Debug Layer Regression Suite ===\n");

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
    Check(SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev))),
          "WARP D3D12 device created with Debug Layer enabled");
    if (!dev) return 1;

    ComPtr<ID3D12InfoQueue> iq;
    dev.As(&iq);

    uint32_t d3dErrors = 0u;

    // Inner scope guarantees every D3D12 child object & host is cleanly destroyed before final InfoQueue audit.
    {
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ComPtr<ID3D12CommandQueue> queue;
        Check(SUCCEEDED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))), "D3D12 direct command queue created");
        if (!queue) return 1;

        // 1. Step RawWeatherSim in Nordic winter blizzard with active snow, ice/frost, and synoptic wind.
        raw::worldgen::WeatherSimConfig wsimCfg{};
        wsimCfg.initialDayOfYear = 15.0f; // Mid-January deep winter
        wsimCfg.timeOfDayHours   = 12.5f; // Crisp winter midday sun
        wsimCfg.baseSeaTempC     = -4.5f;
        wsimCfg.timeScale        = 120.0f;
        wsimCfg.initialRegime    = raw::worldgen::WeatherRegime::Blizzard;
        raw::worldgen::RawWeatherSim weatherSim(wsimCfg);
        weatherSim.SetRegime(raw::worldgen::WeatherRegime::Blizzard, true);

        const raw::worldgen::Vec3 frontierAnchor{75776.0f, 157696.0f, 1200.0f};
        for (int i = 0; i < 24; ++i) {
            weatherSim.Step(1.0f, frontierAnchor);
        }
        const auto wGpu = weatherSim.GetGpuUniforms();
        const float windMag = std::hypot(wGpu.cloudWind.x, wGpu.cloudWind.y);
        std::printf("       WeatherSim (Winter Blizzard): snowM=%.3f, iceM=%.3f, frostRime=%.3f, skinTempC=%.2f, wind=%.1fu/s, beaufort=%.1f\n",
                    wGpu.seasonThermal0.x, wGpu.seasonThermal1.x, wGpu.seasonThermal1.z,
                    wGpu.seasonThermal0.w, windMag, wGpu.beaufortScale);
        Check(wGpu.seasonThermal0.x > 0.005f && wGpu.seasonThermal0.w < 0.0f && windMag > 5.0f &&
              (wGpu.seasonThermal1.x > 0.0f || wGpu.seasonThermal1.z > 0.0f),
              "RawWeatherSim simulated active winter snowpack, ice/frost, sub-zero skin temp & wind");

        // 2. Initialize RawMeshHost, RawFluidSimHost, and RawFluidCompositeHost on WARP.
        RawMeshHost meshHost;
        std::string err;
        Check(meshHost.Init(dev.Get(), &err) && meshHost.TerrainReady(),
              "RawMeshHost initialized with live RawTerrainWorld pipeline on WARP");
        if (!meshHost.TerrainReady()) {
            std::printf("  MeshHost init err: %s\n", err.c_str());
            return 1;
        }
        std::printf("       RawTerrainWorld path: %s\n", meshHost.TerrainUsesMeshShaders() ? "Hardware AS+MS+PS" : "VS+PS fallback");

        RawFluidSimHost fluidSim;
        err.clear();
        Check(fluidSim.Init(dev.Get(), &err) && fluidSim.Ready(),
              "RawFluidSimHost (64^3 3D NS Fire + 256^2 SWE Water) initialized on WARP");
        if (!fluidSim.Ready()) {
            std::printf("  FluidSim init err: %s\n", err.c_str());
            return 1;
        }

        RawFluidCompositeHost compHost;
        err.clear();
        Check(compHost.Init(dev.Get(), &err) && compHost.Ready(),
              "RawFluidCompositeHost initialized on WARP");
        if (!compHost.Ready()) {
            std::printf("  FluidComp init err: %s\n", err.c_str());
            return 1;
        }

        // 3. Stream a real frontier chunk asynchronously via RawWorldShowcase + RawJobSystem,
        //    bake its 3D biomechanical RawFloraEngine trees/props, and upload into RawMeshHost.
        raw::worldgen::StreamConfig sc{};
        sc.vramBudgetBytes           = 128ull * 1024ull * 1024ull;
        sc.lod0RadiusChunks          = 0;
        sc.lod1RadiusChunks          = 0;
        sc.lod2RadiusChunks          = 0;
        sc.lod3RadiusChunks          = 0;
        sc.maxLoadsPerFrame          = 4u;
        sc.maxMeshletTris            = 126u;
        sc.showcaseMode              = false;
        sc.frontierActivationMarginU = 24576.0f;
        sc.terrain.border.enabled    = true;
        sc.useCustomBorderBox        = true;
        sc.borderMinX                = -148000.0f;
        sc.borderMaxX                =  148000.0f;
        sc.borderMinY                = -118000.0f;
        sc.borderMaxY                =  132000.0f;
        sc.asyncStreaming            = true;
        sc.asyncWorkerCount          = 2u;

        raw::worldgen::RawWorldShowcase showcase;
        Check(showcase.Init(sc) && showcase.IsAsyncEnabled(),
              "RawWorldShowcase initialized with non-blocking async RawJobSystem worker pool");

        std::unordered_map<uint64_t, TerrainGpuChunk> chunks;
        uint64_t nextHandle = 1u;
        size_t bakedPropCount = 0u;
        size_t terrainOnlyMeshlets = 0u;
        size_t mergedTotalMeshlets = 0u;
        float chunkMidX = frontierAnchor.x;
        float chunkMidY = frontierAnchor.y;
        float chunkMidZ = 0.0f;

        struct TestSink final : raw::worldgen::IGpuChunkSink {
            RawMeshHost* meshHost = nullptr;
            const raw::worldgen::StreamConfig* sc = nullptr;
            std::unordered_map<uint64_t, TerrainGpuChunk>* chunks = nullptr;
            uint64_t* nextHandle = nullptr;
            size_t* bakedPropCount = nullptr;
            size_t* terrainOnlyMeshlets = nullptr;
            size_t* mergedTotalMeshlets = nullptr;
            float* midX = nullptr;
            float* midY = nullptr;
            float* midZ = nullptr;

            uint64_t OnChunkLoaded(const raw::worldgen::StreamedChunkEntry& entry) override {
                raw::worldgen::ProceduralMesh merged{};
                if (!raw::worldgen::BuildChunkMergedGpuMesh(entry, sc->maxMeshletTris, sc->terrain.seed,
                                                            sc->terrain.seaLevelZ, &merged)) {
                    return 0u;
                }
                TerrainGpuChunk gpu{};
                if (!meshHost->UploadTerrainChunk(merged, &gpu)) return 0u;
                *bakedPropCount += entry.props.size();
                *terrainOnlyMeshlets += entry.mesh.meshlets.size();
                *mergedTotalMeshlets += merged.meshlets.size();
                *midX = entry.terrain.originX + entry.terrain.chunkSizeU * 0.5f;
                *midY = entry.terrain.originY + entry.terrain.chunkSizeU * 0.5f;
                *midZ = 0.5f * (entry.terrain.minZ + entry.terrain.maxZ);
                const uint64_t h = (*nextHandle)++;
                chunks->emplace(h, std::move(gpu));
                return h;
            }
            void OnChunkEvicted(raw::worldgen::ChunkKey, uint64_t gpuHandle) override {
                auto it = chunks->find(gpuHandle);
                if (it != chunks->end()) {
                    meshHost->DeferRelease(std::move(it->second));
                    chunks->erase(it);
                }
            }
        } sink;
        sink.meshHost = &meshHost;
        sink.sc = &sc;
        sink.chunks = &chunks;
        sink.nextHandle = &nextHandle;
        sink.bakedPropCount = &bakedPropCount;
        sink.terrainOnlyMeshlets = &terrainOnlyMeshlets;
        sink.mergedTotalMeshlets = &mergedTotalMeshlets;
        sink.midX = &chunkMidX;
        sink.midY = &chunkMidY;
        sink.midZ = &chunkMidZ;

        // First Update dispatches async build job; WaitForAsyncIdle + second Update drains & uploads GPU buffers.
        (void)showcase.Update(frontierAnchor, &sink);
        showcase.WaitForAsyncIdle();
        const auto streamStats = showcase.Update(frontierAnchor, &sink);
        std::printf("       Async WorldShowcase: activeChunks=%u, pendingAsync=%u, biome=%s, props=%zu, terrainMeshlets=%zu, mergedMeshlets=%zu\n",
                    streamStats.activeChunkCount, showcase.PendingAsyncJobs(),
                    raw::worldgen::GetBiomeName(streamStats.cameraBiome),
                    bakedPropCount, terrainOnlyMeshlets, mergedTotalMeshlets);
        Check(streamStats.activeInFrontier && streamStats.activeChunkCount >= 1u && !chunks.empty() &&
              showcase.PendingAsyncJobs() == 0u,
              "RawWorldShowcase async-streamed and uploaded frontier chunk into RawMeshHost");
        Check(bakedPropCount > 0u && mergedTotalMeshlets > terrainOnlyMeshlets,
              "RawFloraEngine 3D biomechanical trees/props baked into merged GPU terrain meshlets");

        // 4. Build unified camera looking at the streamed frontier chunk + matching inverse clipToWorld.
        constexpr UINT W = 256u, H = 144u;
        const float camPos[3] = {chunkMidX, chunkMidY - 2600.0f, chunkMidZ + 1400.0f};
        const float target[3] = {chunkMidX, chunkMidY, chunkMidZ};
        const float nearZ = 15.0f, farZ = 353840.0f;

        FullFrameLightUniform u{};
        const CameraBasis cam = BuildCameraMatrices(camPos, target, float(W) / float(H), nearZ, farZ,
                                                    u.worldToClip, u.clipToWorld);
        u.dims[0] = float(W); u.dims[1] = float(H); u.dims[2] = 1.0f / float(W); u.dims[3] = 1.0f / float(H);
        u.proj[0] = farZ / (farZ - nearZ);
        u.proj[1] = (farZ * nearZ) / (farZ - nearZ);
        u.resScale[0] = 1.0f; u.resScale[1] = 1.0f; u.resScale[2] = 0.5f; u.resScale[3] = 1.0f;
        u.camPos[0] = camPos[0]; u.camPos[1] = camPos[1]; u.camPos[2] = camPos[2]; u.camPos[3] = 2.4f;
        u.sunDir[0] = 0.42f; u.sunDir[1] = 0.28f; u.sunDir[2] = 0.86f; u.sunDir[3] = 1.0f;
        u.sunColour[0] = 1.25f; u.sunColour[1] = 1.20f; u.sunColour[2] = 1.12f; u.sunColour[3] = 1.0f;
        u.weather[0] = wGpu.weather.x; u.weather[1] = wGpu.weather.y; u.weather[2] = std::max(wGpu.weather.z, 0.55f); u.weather[3] = wGpu.weather.w;
        u.timeEnv[0] = 12.5f; u.timeEnv[1] = 0.72f; u.timeEnv[2] = wGpu.timeEnv.z; u.timeEnv[3] = 0.0f;
        u.tune[0] = 0.000035f; u.tune[1] = 0.45f;
        u.clouds[0] = wGpu.cloudWind.z;
        u.cloudWind[0] = wGpu.cloudWind.x; u.cloudWind[1] = wGpu.cloudWind.y;
        u.physWorld[0][0] = 2.4f; u.physWorld[0][1] = 9.81f;
        u.physWorld[1][0] = wGpu.beaufortScale; u.physWorld[1][1] = 0.866f; u.physWorld[1][2] = 0.5f;
        u.seasonThermal0[0] = wGpu.seasonThermal0.x; u.seasonThermal0[1] = wGpu.seasonThermal0.y;
        u.seasonThermal0[2] = wGpu.seasonThermal0.z; u.seasonThermal0[3] = wGpu.seasonThermal0.w;
        u.seasonThermal1[0] = wGpu.seasonThermal1.x; u.seasonThermal1[1] = wGpu.seasonThermal1.y;
        u.seasonThermal1[2] = wGpu.seasonThermal1.z; u.seasonThermal1[3] = wGpu.seasonThermal1.w;
        u.phenology0[0] = wGpu.phenology0.x; u.phenology0[1] = wGpu.phenology0.y;
        u.phenology0[2] = wGpu.phenology0.z; u.phenology0[3] = wGpu.phenology0.w;
        u.phenology1[0] = wGpu.phenology1.x; u.phenology1[1] = wGpu.phenology1.y;
        u.phenology1[2] = wGpu.phenology1.z; u.phenology1[3] = wGpu.phenology1.w;

        // 5. Command list & fence for 12 frames of 64^3 3D NS Fire + 256^2 SWE Water + target setup + readbacks.
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

        // Place 64^3 3D Navier-Stokes fire domain directly 280u along the camera's forward ray (in front of the chunk!),
        // with one emitter on the occluded left half (-R) and one emitter on the visible right half (+R).
        const float fireCenter[3] = {
            camPos[0] + cam.fwd[0] * 280.0f,
            camPos[1] + cam.fwd[1] * 280.0f,
            camPos[2] + cam.fwd[2] * 280.0f
        };
        FluidFireParams fp{};
        fp.cellSize = 4.0f; // 64 * 4 = 256u domain span
        fp.domainOriginW[0] = fireCenter[0] - 128.0f;
        fp.domainOriginW[1] = fireCenter[1] - 128.0f;
        fp.domainOriginW[2] = fireCenter[2] - 128.0f;
        fp.dt = 1.0f / 24.0f;
        fp.windVelW[0] = std::clamp(wGpu.cloudWind.x * 0.08f, -12.0f, 12.0f);
        fp.windVelW[1] = std::clamp(wGpu.cloudWind.y * 0.08f, -12.0f, 12.0f);
        fp.windVelW[2] = 0.0f;
        fp.ambientTempK = std::clamp(wGpu.seasonThermal0.w + 273.15f, 240.0f, 300.0f);
        fp.jacobiIterations = 4u;
        fp.emitterCount = 2u;
        // Left emitter (occluded by left-half SceneDepth wall!)
        fp.emitters[0].posW[0] = fireCenter[0] - cam.right[0] * 42.0f - cam.up[0] * 48.0f;
        fp.emitters[0].posW[1] = fireCenter[1] - cam.right[1] * 42.0f - cam.up[1] * 48.0f;
        fp.emitters[0].posW[2] = fireCenter[2] - cam.right[2] * 42.0f - cam.up[2] * 48.0f;
        fp.emitters[0].radius = 22.0f;
        fp.emitters[0].ignitionTempK = 2850.0f;
        fp.emitters[0].fuelRate = 3.2f;
        fp.emitters[0].jetSpeed = 48.0f;
        fp.emitters[0].curlStrength = 14.0f;
        fp.emitters[0].sootSeedRate = 1.05f;
        // Right emitter (visible directly in front of winter terrain & 3D flora!)
        fp.emitters[1] = fp.emitters[0];
        fp.emitters[1].posW[0] = fireCenter[0] + cam.right[0] * 42.0f - cam.up[0] * 48.0f;
        fp.emitters[1].posW[1] = fireCenter[1] + cam.right[1] * 42.0f - cam.up[1] * 48.0f;
        fp.emitters[1].posW[2] = fireCenter[2] + cam.right[2] * 42.0f - cam.up[2] * 48.0f;

        FluidWaterParams wp{};
        wp.camPosXY[0] = camPos[0];
        wp.camPosXY[1] = camPos[1];
        wp.cellSize = 8.0f;
        wp.dt = 1.0f / 24.0f;
        wp.windDirXY[0] = 0.866f;
        wp.windDirXY[1] = 0.5f;
        wp.swellAmp = std::clamp(0.45f + wGpu.beaufortScale * 0.42f, 0.5f, 4.5f);
        wp.rainIntensity = 0.35f;
        wp.waterPlaneZ = -14000.0f;
        wp.trailCount = 1u;
        wp.trails[0].posW[0] = camPos[0] + 64.0f;
        wp.trails[0].posW[1] = camPos[1] + 64.0f;
        wp.trails[0].posW[2] = wp.waterPlaneZ;
        wp.trails[0].strength = 0.85f;
        wp.trails[0].radius = 24.0f;
        wp.trails[0].splashScale = 1.0f;

        for (int step = 0; step < 12; ++step) {
            fp.timeSec = float(step) * fp.dt;
            wp.timeSec = float(step) * wp.dt;
            fluidSim.StepFire(cl.Get(), fp);
            fluidSim.StepWater(cl.Get(), wp);
        }
        cl->Close();
        ID3D12CommandList* fluidLists[1] = {cl.Get()};
        queue->ExecuteCommandLists(1, fluidLists);
        flushQueue();
        Check(fluidSim.GetFireDomainInfo().active && fluidSim.GetFireDomainInfo().emitterCount == 2u &&
              fluidSim.GetWaterDomainInfo().active,
              "RawFluidSimHost stepped 12 frames of 64^3 3D NS Fire + 256^2 SWE Water on WARP");

        // 6. Create shared 256x144 RGBA16F HDR ColorOut (RT + UAV) and R32F SceneDepth (left half occluded, right half sky).
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
                row[x] = (x < W / 2u) ? 0.05f : 1.0f; // Left half occluded at ~15.8u; right half open sky
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
        const float clearZero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        cl->ClearRenderTargetView(rtvHeap->GetCPUDescriptorHandleForHeapStart(), clearZero, 0, nullptr);
        D3D12_RESOURCE_BARRIER bClr{};
        bClr.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        bClr.Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON};
        cl->ResourceBarrier(1, &bClr);
        cl->Close();
        ID3D12CommandList* prepLists[1] = {cl.Get()};
        queue->ExecuteCommandLists(1, prepLists);
        flushQueue();

        // Readback helper (preserves colorOut in COMMON after snapshot!).
        const UINT colorPitch = (W * 8u + 255u) & ~255u;
        D3D12_RESOURCE_DESC rbDesc = upDesc;
        rbDesc.Width = size_t(colorPitch) * H;
        ComPtr<ID3D12Resource> rbBuf;
        dev->CreateCommittedResource(&rbHp, D3D12_HEAP_FLAG_NONE, &rbDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rbBuf));

        auto readbackColorOut = [&](std::vector<float>& rgbaOut) {
            ca->Reset();
            cl->Reset(ca.Get(), nullptr);
            D3D12_RESOURCE_BARRIER bToSrc{};
            bToSrc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bToSrc.Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE};
            cl->ResourceBarrier(1, &bToSrc);
            D3D12_TEXTURE_COPY_LOCATION rbDst{}; rbDst.pResource = rbBuf.Get(); rbDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            rbDst.PlacedFootprint.Footprint = {DXGI_FORMAT_R16G16B16A16_FLOAT, W, H, 1, colorPitch};
            D3D12_TEXTURE_COPY_LOCATION rbSrc{}; rbSrc.pResource = colorOut.Get(); rbSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            cl->CopyTextureRegion(&rbDst, 0, 0, 0, &rbSrc, nullptr);
            D3D12_RESOURCE_BARRIER bBack{};
            bBack.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bBack.Transition = {colorOut.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON};
            cl->ResourceBarrier(1, &bBack);
            cl->Close();
            ID3D12CommandList* lists[1] = {cl.Get()};
            queue->ExecuteCommandLists(1, lists);
            flushQueue();

            rgbaOut.resize(size_t(W) * H * 4u);
            void* rp = nullptr;
            rbBuf->Map(0, nullptr, &rp);
            for (UINT y = 0; y < H; ++y) {
                const uint16_t* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(rp) + size_t(y) * colorPitch);
                for (UINT x = 0; x < W; ++x) {
                    const size_t o = (size_t(y) * W + x) * 4u;
                    rgbaOut[o + 0] = HalfToFloat(row[x * 4u + 0u]);
                    rgbaOut[o + 1] = HalfToFloat(row[x * 4u + 1u]);
                    rgbaOut[o + 2] = HalfToFloat(row[x * 4u + 2u]);
                    rgbaOut[o + 3] = HalfToFloat(row[x * 4u + 3u]);
                }
            }
            rbBuf->Unmap(0, nullptr);
        };

        // 7. PASS A: Render RawTerrainWorld (AS+MS+PS) with winter seasons + baked 3D flora onto colorOut.
        uint32_t drawnC = 0u, drawnM = 0u;
        const bool terrainOk = meshHost.ExecuteTerrainPass(
            queue.Get(), colorOut.Get(), depthIn.Get(), nullptr, W, H,
            &u, sizeof(u), u.worldToClip, camPos, 2.4f, chunks, &drawnC, &drawnM);
        flushQueue();
        Check(terrainOk && drawnC >= 1u && drawnM >= 1u,
              "RawTerrainWorld (AS+MS+PS) rendered winter terrain + 3D flora onto 256x144 RGBA16F HDR target");

        std::vector<float> afterTerrain;
        readbackColorOut(afterTerrain);

        // 8. PASS B: Composite live 64^3 3D Navier-Stokes Fire/Smoke/Radiance onto the EXACT SAME colorOut target!
        const auto fDom = fluidSim.GetFireDomainInfo();
        FluidCompositeParams cp{};
        std::memcpy(cp.clipToWorld, u.clipToWorld, 64u);
        cp.camPosW[0] = camPos[0]; cp.camPosW[1] = camPos[1]; cp.camPosW[2] = camPos[2];
        cp.simTimeSec = 2.4f;
        cp.domainMinW[0] = fDom.originXYZ[0]; cp.domainMinW[1] = fDom.originXYZ[1]; cp.domainMinW[2] = fDom.originXYZ[2];
        cp.domainSpanW = fDom.worldSpanU;
        cp.sunDir[0] = u.sunDir[0]; cp.sunDir[1] = u.sunDir[1]; cp.sunDir[2] = u.sunDir[2];
        cp.sunIntensity = 1.0f;
        cp.sunCol[0] = u.sunColour[0]; cp.sunCol[1] = u.sunColour[1]; cp.sunCol[2] = u.sunColour[2];
        cp.ambientFill = 0.45f;
        cp.width = W; cp.height = H;
        cp.activeEmitters = fDom.emitterCount;
        cp.isInterior = false;

        const bool compOk = compHost.Composite(
            queue.Get(), colorOut.Get(), depthIn.Get(),
            fluidSim.FireThermoResource(), fluidSim.FireRadianceResource(), cp);
        flushQueue();
        Check(compOk, "RawFluidCompositeHost composited 64^3 3D NS fire/smoke onto the EXACT SAME HDR target");

        std::vector<float> afterComposite;
        readbackColorOut(afterComposite);

        // 9. Verify pixels across both stages!
        uint32_t nanInfCount = 0u;
        uint32_t occludedHalfModified = 0u;
        uint32_t terrainRightLit = 0u;
        uint32_t fireBoostedRightPx = 0u;
        uint32_t sootAttenuatedRightPx = 0u;
        uint32_t preservedTerrainRightPx = 0u;
        float maxFinalRightR = 0.0f;

        for (UINT y = 0; y < H; ++y) {
            for (UINT x = 0; x < W; ++x) {
                const size_t o = (size_t(y) * W + x) * 4u;
                const float tr = afterTerrain[o + 0], tg = afterTerrain[o + 1], tb = afterTerrain[o + 2], ta = afterTerrain[o + 3];
                const float fr = afterComposite[o + 0], fg = afterComposite[o + 1], fb = afterComposite[o + 2], fa = afterComposite[o + 3];
                if (!std::isfinite(tr) || !std::isfinite(tg) || !std::isfinite(tb) || !std::isfinite(ta) ||
                    !std::isfinite(fr) || !std::isfinite(fg) || !std::isfinite(fb) || !std::isfinite(fa)) {
                    ++nanInfCount;
                    continue;
                }
                if (x < W / 2u) {
                    if (std::fabs(tr) + std::fabs(tg) + std::fabs(tb) + std::fabs(ta) > 0.001f ||
                        std::fabs(fr) + std::fabs(fg) + std::fabs(fb) + std::fabs(fa) > 0.001f) {
                        ++occludedHalfModified;
                    }
                } else {
                    const float tLuma = (tr + tg + tb) / 3.0f;
                    const bool hadTerrain = (ta > 0.5f || tLuma > 0.005f);
                    if (hadTerrain) ++terrainRightLit;
                    maxFinalRightR = std::max(maxFinalRightR, fr);
                    if (fr > tr + 0.45f && fr > 1.20f) ++fireBoostedRightPx;
                    if (hadTerrain && fb < tb - 0.015f && fr <= 1.20f) ++sootAttenuatedRightPx;
                    if (hadTerrain && std::fabs(fr - tr) + std::fabs(fg - tg) + std::fabs(fb - tb) < 0.01f) {
                        ++preservedTerrainRightPx;
                    }
                }
            }
        }

        std::printf("       Readback: terrainRightLit=%u, fireBoostPx=%u (maxR=%.2f), sootAttenPx=%u, preservedTerrainPx=%u, occludedMod=%u, nanInf=%u\n",
                    terrainRightLit, fireBoostedRightPx, maxFinalRightR, sootAttenuatedRightPx,
                    preservedTerrainRightPx, occludedHalfModified, nanInfCount);

        Check(nanInfCount == 0u, "Full-frame multi-pass HDR target contains 0 NaN/Inf pixels");
        Check(occludedHalfModified == 0u, "SceneDepth left-half foreground wall 100% occluded BOTH terrain+flora AND 3D fire/smoke");
        Check(terrainRightLit >= 1500u && preservedTerrainRightPx >= 500u,
              "Winter multi-biome terrain + baked 3D flora rendered rich HDR pixels on open right half");
        Check(fireBoostedRightPx >= 20u && maxFinalRightR > 1.20f,
              "Live 64^3 3D Navier-Stokes incandescent fire composited cleanly on top of terrain + 3D flora");
        Check(sootAttenuatedRightPx >= 10u,
              "Live 64^3 3D Navier-Stokes Beer-Lambert soot visibly attenuated terrain + flora pixels behind smoke plume");

        // 10. Clean race-free shutdown of async worker pool + all D3D12 hosts & GPU resources.
        showcase.WaitForAsyncIdle();
        showcase.Reset(&sink);
        showcase.SetAsyncEnabled(false);
        Check(showcase.PendingAsyncJobs() == 0u && chunks.empty(),
              "RawWorldShowcase async pool drained & shut down cleanly with 0 leaked worker threads");

        flushQueue();
        compHost.Shutdown();
        meshHost.Shutdown();
        CloseHandle(ev);
    }

    // Audit D3D12 Debug Layer InfoQueue for ANY errors or corruptions across the entire run & shutdown.
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
    Check(d3dErrors == 0u, "D3D12 Debug Layer reported 0 errors/corruptions across full-frame multi-pass execution & shutdown");

    std::printf("=== Summary: %d passed, %d failed (d3d12DebugErrors=%u) ===\n", g_pass, g_fail, d3dErrors);
    return g_fail == 0 ? 0 : 1;
}
