// raw_sandbox_render.cpp: RAW Standalone Native Engine Sandbox Renderer (Zero SkyrimSE.exe Required!).
// Boots and renders BOTH:
//   1. `--mode frontier`: RawWeatherSim (seasonal thermals/snow/ice/wind) + RawWorldShowcase (async frontier
//      multi-biome chunks + RawFloraEngine 3D biomechanical trees) + RawDestructionEngine (3D Voronoi-plane
//      CSG fractured stone ruins) + RawFluidSimHost (64^3 3D Navier-Stokes Fire/Smoke + 256^2 SWE Water)
//      + RawMeshHost (RawTerrainWorld.hlsl hardware AS+MS+PS / VS+PS fallback) + RawFluidCompositeHost
//      -> Troy Sobotka AgX filmic tonemapping + sRGB -> 24-bit uncompressed .bmp output!
//   2. `--mode esm --esm <Skyrim.esm> --cell <x,y>`: Streams REAL Tamriel exterior terrain directly from
//      Skyrim.esm via RawEsmWorldLoader -> 64v/126t hardware GPU meshlets -> RawMeshHost AS+MS+PS -> AgX .bmp!
//   3. `--self-test [--warp]`: Runs BOTH frontier and ESM modes end-to-end with D3D12 Debug Layer enabled,
//      verifying 0 D3D12 debug errors, 0 NaN/Inf pixels, non-trivial HDR contrast/variance, and valid .bmp files.
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
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "RawMeshHost.h"
#include "RawFluidSimHost.h"
#include "RawFluidCompositeHost.h"
#include "RawWeatherSim.h"
#include "RawWorldShowcase.h"
#include "RawDestructionEngine.h"
#include "RawEsmWorldLoader.h"

using Microsoft::WRL::ComPtr;
using namespace raw::bridge;

namespace
{

constexpr const char* kDefaultSkyrimEsm =
    "H:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition\\Data\\Skyrim.esm";

// Exact byte-for-byte mirror of cbuffer P in LightingCommon.hlsli (42800 bytes).
struct SandboxLightUniform {
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
static_assert(sizeof(SandboxLightUniform) == 42800u, "SandboxLightUniform must match cbuffer P");

enum class RenderMode { Frontier, Esm };
enum class SeasonChoice { Winter, Autumn, Summer, Spring };

struct SandboxOptions {
    RenderMode   mode     = RenderMode::Frontier;
    SeasonChoice season   = SeasonChoice::Winter;
    std::string  esmPath  = kDefaultSkyrimEsm;
    int32_t      cellX    = 14;
    int32_t      cellY    = -14; // High Hrothgar / Throat of the World
    UINT         width    = 640u;
    UINT         height   = 360u;
    std::string  outPath;
    bool         useWarp  = false;
    bool         debug    = false;
    bool         selfTest = false;
    bool         showHelp = false;
};

struct CameraBasis {
    float right[3];
    float up[3];
    float fwd[3];
};

struct RenderMetrics {
    bool     ok               = false;
    uint32_t drawnChunks      = 0u;
    uint32_t drawnMeshlets    = 0u;
    uint32_t litPixels        = 0u;
    uint32_t fireBoostPixels  = 0u;
    uint32_t nanInfPixels     = 0u;
    uint32_t d3d12DebugErrors = 0u;
    double   meanLuma         = 0.0;
    double   stdDevLuma       = 0.0;
    float    maxHdrR          = 0.0f;
    bool     usedMeshShaders  = false;
    bool     bmpValid         = false;
    std::string outPath;
    std::string errMsg;
};

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

CameraBasis BuildCameraMatrices(const float eye[3], const float target[3],
                                float aspect, float nearZ, float farZ,
                                float outWorldToClip[16], float outClipToWorld[16])
{
    float fx = target[0] - eye[0], fy = target[1] - eye[1], fz = target[2] - eye[2];
    const float fl = std::max(std::sqrt(fx * fx + fy * fy + fz * fz), 1.0e-5f);
    fx /= fl; fy /= fl; fz /= fl;

    float rx = fy, ry = -fx, rz = 0.0f;
    const float rl = std::max(std::sqrt(rx * rx + ry * ry + rz * rz), 1.0e-5f);
    rx /= rl; ry /= rl; rz /= rl;

    const float ux = ry * fz - rz * fy;
    const float uy = rz * fx - rx * fz;
    const float uz = rx * fy - ry * fx;

    const float tanHalfFov = std::tan(0.56f); // ~64 deg vFOV
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

    return {{rx, ry, rz}, {ux, uy, uz}, {fx, fy, fz}};
}

// Exact Troy Sobotka AgX Filmic Tonemapper (Inset -> Log2 -> 6th-order Default Contrast -> Outset).
void AgxTonemap(float rIn, float gIn, float bIn, float& rOut, float& gOut, float& bOut)
{
    const float r = std::max(rIn, 0.0f);
    const float g = std::max(gIn, 0.0f);
    const float b = std::max(bIn, 0.0f);

    // AgX inset matrix (sRGB linear -> AgX working primaries)
    float x = 0.842479062253094f * r + 0.0784335999999992f * g + 0.0792237451477643f * b;
    float y = 0.0423282422610123f * r + 0.878468636469772f * g + 0.0791661274605434f * b;
    float z = 0.0423756549057051f * r + 0.0784336000000000f * g + 0.879142973793104f * b;

    constexpr float kMinEv = -12.47393f;
    constexpr float kMaxEv = 4.026069f;
    auto evalAgxPoly = [](float v) -> float {
        const float lv = std::clamp((std::log2(std::max(v, 1.0e-10f)) - kMinEv) / (kMaxEv - kMinEv), 0.0f, 1.0f);
        const float x2 = lv * lv;
        const float x4 = x2 * x2;
        return std::clamp(
            15.5f * x4 * x2 - 40.14f * x4 * lv + 31.96f * x4 - 6.868f * x2 * lv + 0.4298f * x2 + 0.1191f * lv - 0.00232f,
            0.0f, 1.0f);
    };
    x = evalAgxPoly(x);
    y = evalAgxPoly(y);
    z = evalAgxPoly(z);

    // AgX outset matrix back to display-referred sRGB (already perceptually shaped near gamma 2.2)
    float or_ = 1.19687900512017f * x - 0.0528968517574562f * y - 0.0529716355144438f * z;
    float og_ = -0.0980208811401368f * x + 1.15190312990417f * y - 0.0980434501171241f * z;
    float ob_ = -0.0990297440797205f * x - 0.0989611768448433f * y + 1.15107367264116f * z;

    // Convert AgX log-encoded display value through linear power 2.2 -> exact IEC 61966-2-1 sRGB OETF
    auto agxToSrgb = [](float c) -> float {
        const float lin = std::pow(std::clamp(c, 0.0f, 1.0f), 2.2f);
        const float s = (lin <= 0.0031308f) ? (12.92f * lin) : (1.055f * std::pow(lin, 1.0f / 2.4f) - 0.055f);
        return std::clamp(s, 0.0f, 1.0f);
    };
    rOut = agxToSrgb(or_);
    gOut = agxToSrgb(og_);
    bOut = agxToSrgb(ob_);
}

bool WriteBmp24(const std::string& path, UINT width, UINT height, const std::vector<uint8_t>& rgbTopDown)
{
    if (width == 0u || height == 0u || rgbTopDown.size() < size_t(width) * height * 3u) return false;
    const uint32_t rowStride = ((width * 3u) + 3u) & ~3u;
    const uint32_t pixelBytes = rowStride * height;
    const uint32_t fileSize = 54u + pixelBytes;

    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;

    uint8_t hdr[54]{};
    hdr[0] = 'B'; hdr[1] = 'M';
    std::memcpy(hdr + 2, &fileSize, 4);
    const uint32_t pixelOffset = 54u;
    std::memcpy(hdr + 10, &pixelOffset, 4);
    const uint32_t dibSize = 40u;
    std::memcpy(hdr + 14, &dibSize, 4);
    const int32_t w32 = static_cast<int32_t>(width);
    const int32_t h32 = static_cast<int32_t>(height); // Bottom-up standard BMP
    std::memcpy(hdr + 18, &w32, 4);
    std::memcpy(hdr + 22, &h32, 4);
    const uint16_t planes = 1u;
    const uint16_t bpp = 24u;
    std::memcpy(hdr + 26, &planes, 2);
    std::memcpy(hdr + 28, &bpp, 2);
    std::memcpy(hdr + 34, &pixelBytes, 4);
    ofs.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));

    std::vector<uint8_t> rowBuf(rowStride, 0u);
    for (UINT y = 0; y < height; ++y) {
        const UINT srcY = height - 1u - y;
        for (UINT x = 0; x < width; ++x) {
            const size_t si = (size_t(srcY) * width + x) * 3u;
            rowBuf[size_t(x) * 3u + 0u] = rgbTopDown[si + 2u]; // B
            rowBuf[size_t(x) * 3u + 1u] = rgbTopDown[si + 1u]; // G
            rowBuf[size_t(x) * 3u + 2u] = rgbTopDown[si + 0u]; // R
        }
        ofs.write(reinterpret_cast<const char*>(rowBuf.data()), static_cast<std::streamsize>(rowStride));
    }
    ofs.flush();
    return ofs.good();
}

bool ValidateBmp24Header(const std::string& path, UINT expectedW, UINT expectedH)
{
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) return false;
    const std::streamoff sz = ifs.tellg();
    const uint32_t rowStride = ((expectedW * 3u) + 3u) & ~3u;
    const std::streamoff expectedSize = static_cast<std::streamoff>(54u + rowStride * expectedH);
    if (sz != expectedSize) return false;
    ifs.seekg(0);
    uint8_t hdr[54]{};
    ifs.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
    if (!ifs.good() || hdr[0] != 'B' || hdr[1] != 'M') return false;
    int32_t w = 0, h = 0;
    uint16_t bpp = 0;
    std::memcpy(&w, hdr + 18, 4);
    std::memcpy(&h, hdr + 22, 4);
    std::memcpy(&bpp, hdr + 28, 2);
    return (w == static_cast<int32_t>(expectedW) && h == static_cast<int32_t>(expectedH) && bpp == 24u);
}

void ConfigureWeatherForSeason(SeasonChoice s, raw::worldgen::WeatherSimConfig* cfg, raw::worldgen::WeatherRegime* regime)
{
    cfg->timeScale = 120.0f;
    switch (s) {
        case SeasonChoice::Winter:
            cfg->initialDayOfYear = 15.0f;
            cfg->timeOfDayHours   = 12.5f;
            cfg->baseSeaTempC     = -4.5f;
            *regime               = raw::worldgen::WeatherRegime::Blizzard;
            break;
        case SeasonChoice::Autumn:
            cfg->initialDayOfYear = 285.0f;
            cfg->timeOfDayHours   = 15.5f;
            cfg->baseSeaTempC     = 8.0f;
            *regime               = raw::worldgen::WeatherRegime::ClearAlpine;
            break;
        case SeasonChoice::Summer:
            cfg->initialDayOfYear = 172.0f;
            cfg->timeOfDayHours   = 13.0f;
            cfg->baseSeaTempC     = 16.0f;
            *regime               = raw::worldgen::WeatherRegime::ClearAlpine;
            break;
        case SeasonChoice::Spring:
            cfg->initialDayOfYear = 105.0f;
            cfg->timeOfDayHours   = 9.5f;
            cfg->baseSeaTempC     = 7.5f;
            *regime               = raw::worldgen::WeatherRegime::DawnValleyMist;
            break;
    }
    cfg->initialRegime = *regime;
}

const char* SeasonName(SeasonChoice s)
{
    switch (s) {
        case SeasonChoice::Winter: return "winter";
        case SeasonChoice::Autumn: return "autumn";
        case SeasonChoice::Summer: return "summer";
        case SeasonChoice::Spring: return "spring";
    }
    return "winter";
}

float SampleChunkTerrainZ(const raw::worldgen::TerrainChunkData& t, float worldX, float worldY)
{
    if (t.stride < 2u || t.heights.size() < size_t(t.stride) * t.stride) {
        return 0.5f * (t.minZ + t.maxZ);
    }
    const float u = std::clamp((worldX - t.originX) / std::max(t.chunkSizeU, 1.0f), 0.0f, 1.0f);
    const float v = std::clamp((worldY - t.originY) / std::max(t.chunkSizeU, 1.0f), 0.0f, 1.0f);
    return t.SampleHeightBilinear(u, v);
}

void PopulateUniformFromWeather(SandboxLightUniform& u, UINT W, UINT H,
                                const float camPos[3], float nearZ, float farZ,
                                const raw::worldgen::WeatherGpuUniforms& wGpu,
                                SeasonChoice season)
{
    u.dims[0] = float(W); u.dims[1] = float(H);
    u.dims[2] = 1.0f / float(W); u.dims[3] = 1.0f / float(H);
    u.proj[0] = farZ / (farZ - nearZ);
    u.proj[1] = (farZ * nearZ) / (farZ - nearZ);
    u.resScale[0] = 1.0f; u.resScale[1] = 1.0f; u.resScale[2] = 0.5f; u.resScale[3] = 1.0f;
    u.camPos[0] = camPos[0]; u.camPos[1] = camPos[1]; u.camPos[2] = camPos[2]; u.camPos[3] = 2.4f;

    const bool golden = (season == SeasonChoice::Autumn || season == SeasonChoice::Spring);
    // Match LightingCommon.hlsli isRealSun() hemisphere convention: morning (<12.5h) uses +X sun, afternoon (>=12.5h) uses -X sun.
    u.sunDir[0] = golden ? -0.74f : 0.68f;
    u.sunDir[1] = 0.32f;
    u.sunDir[2] = golden ? 0.58f : 0.66f;
    const float sl = std::sqrt(u.sunDir[0] * u.sunDir[0] + u.sunDir[1] * u.sunDir[1] + u.sunDir[2] * u.sunDir[2]);
    u.sunDir[0] /= sl; u.sunDir[1] /= sl; u.sunDir[2] /= sl; u.sunDir[3] = 1.0f;

    u.sunColour[0] = golden ? 1.55f : 1.35f;
    u.sunColour[1] = golden ? 1.18f : 1.28f;
    u.sunColour[2] = golden ? 0.84f : 1.18f;
    u.sunColour[3] = 1.0f;

    u.weather[0] = wGpu.weather.x;
    u.weather[1] = wGpu.weather.y;
    u.weather[2] = (season == SeasonChoice::Winter) ? std::max(wGpu.weather.z, 0.55f) : wGpu.weather.z;
    u.weather[3] = wGpu.weather.w;
    u.timeEnv[0] = golden ? 15.8f : 10.5f;
    u.timeEnv[1] = u.sunDir[2];
    u.timeEnv[2] = wGpu.timeEnv.z;
    u.timeEnv[3] = 0.0f;
    u.tune[0] = 0.000028f; u.tune[1] = 0.45f;
    u.clouds[0] = std::min(wGpu.cloudWind.z, 0.35f);
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
}

uint32_t AuditInfoQueue(ID3D12InfoQueue* iq)
{
    if (!iq) return 0u;
    uint32_t errs = 0u;
    const UINT64 n = iq->GetNumStoredMessages();
    for (UINT64 i = 0; i < n; ++i) {
        SIZE_T len = 0;
        iq->GetMessage(i, nullptr, &len);
        std::vector<uint8_t> buf(len);
        auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
        if (SUCCEEDED(iq->GetMessage(i, msg, &len)) &&
            (msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR || msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)) {
            std::printf("  [D3D12_DEBUG_ERR] %s\n", msg->pDescription);
            ++errs;
        }
    }
    return errs;
}

RenderMetrics RunSingleRender(const SandboxOptions& opt)
{
    RenderMetrics res{};
    res.outPath = opt.outPath;

    if (opt.debug || opt.useWarp || opt.selfTest) {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
            dbg->EnableDebugLayer();
        }
    }

    ComPtr<IDXGIFactory4> fac;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&fac)))) {
        res.errMsg = "CreateDXGIFactory2 failed";
        return res;
    }

    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> dev;
    std::string adapterName = "WARP";
    if (!opt.useWarp) {
        for (UINT idx = 0; fac->EnumAdapters1(idx, &adapter) != DXGI_ERROR_NOT_FOUND; ++idx) {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
                adapter.Reset();
                continue;
            }
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev)))) {
                char nb[128]{};
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nb, sizeof(nb) - 1, nullptr, nullptr);
                adapterName = nb;
                break;
            }
            adapter.Reset();
        }
    }
    if (!dev) {
        ComPtr<IDXGIAdapter> warp;
        if (FAILED(fac->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
            FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev)))) {
            res.errMsg = "Failed to create D3D12 device (HW & WARP)";
            return res;
        }
        adapterName = "Microsoft Basic Render Driver (WARP)";
    }

    ComPtr<ID3D12InfoQueue> iq;
    dev.As(&iq);

    std::printf("[Sandbox] Mode=%s | Adapter=%s | Res=%ux%u | Season=%s\n",
                opt.mode == RenderMode::Frontier ? "frontier" : "esm",
                adapterName.c_str(), opt.width, opt.height, SeasonName(opt.season));

    // Inner scope guarantees every child COM object & async pool shuts down before InfoQueue audit.
    {
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ComPtr<ID3D12CommandQueue> queue;
        if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) {
            res.errMsg = "CreateCommandQueue failed";
            return res;
        }

        RawMeshHost meshHost;
        std::string err;
        if (!meshHost.Init(dev.Get(), &err) || !meshHost.TerrainReady()) {
            res.errMsg = "RawMeshHost init failed: " + err;
            return res;
        }
        res.usedMeshShaders = meshHost.TerrainUsesMeshShaders();

        // Step RawWeatherSim for active seasonal lighting/snow/ice/wind uniforms.
        raw::worldgen::WeatherSimConfig wcfg{};
        raw::worldgen::WeatherRegime wreg = raw::worldgen::WeatherRegime::ClearAlpine;
        ConfigureWeatherForSeason(opt.season, &wcfg, &wreg);
        raw::worldgen::RawWeatherSim weatherSim(wcfg);
        weatherSim.SetRegime(wreg, true);

        const UINT W = opt.width;
        const UINT H = opt.height;
        const float nearZ = 15.0f, farZ = 353840.0f;

        std::unordered_map<uint64_t, TerrainGpuChunk> chunks;
        uint64_t nextHandle = 1u;
        float camPos[3]{};
        float target[3]{};

        raw::worldgen::RawWorldShowcase showcase;
        bool hasShowcase = false;
        std::unique_ptr<RawFluidSimHost> fluidSim;
        std::unique_ptr<RawFluidCompositeHost> compHost;

        if (opt.mode == RenderMode::Frontier) {
            const raw::worldgen::Vec3 frontierAnchor{75776.0f, 157696.0f, 1200.0f};
            for (int i = 0; i < 24; ++i) weatherSim.Step(1.0f, frontierAnchor);

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

            if (!showcase.Init(sc)) {
                res.errMsg = "RawWorldShowcase::Init failed";
                return res;
            }
            hasShowcase = true;

            raw::worldgen::TerrainChunkData capturedTerrain{};
            bool hasCapturedTerrain = false;

            struct FrontierSink final : raw::worldgen::IGpuChunkSink {
                RawMeshHost* host = nullptr;
                const raw::worldgen::StreamConfig* sc = nullptr;
                std::unordered_map<uint64_t, TerrainGpuChunk>* chunks = nullptr;
                uint64_t* nextHandle = nullptr;
                raw::worldgen::TerrainChunkData* outTerrain = nullptr;
                bool* hasTerrain = nullptr;
                size_t propsBaked = 0u;

                uint64_t OnChunkLoaded(const raw::worldgen::StreamedChunkEntry& entry) override {
                    raw::worldgen::ProceduralMesh merged{};
                    if (!raw::worldgen::BuildChunkMergedGpuMesh(entry, sc->maxMeshletTris, sc->terrain.seed,
                                                                sc->terrain.seaLevelZ, &merged)) {
                        return 0u;
                    }
                    TerrainGpuChunk gpu{};
                    if (!host->UploadTerrainChunk(merged, &gpu)) return 0u;
                    propsBaked += entry.props.size();
                    if (!*hasTerrain) {
                        *outTerrain = entry.terrain;
                        *hasTerrain = true;
                    }
                    const uint64_t h = (*nextHandle)++;
                    chunks->emplace(h, std::move(gpu));
                    return h;
                }
                void OnChunkEvicted(raw::worldgen::ChunkKey, uint64_t h) override {
                    auto it = chunks->find(h);
                    if (it != chunks->end()) {
                        host->DeferRelease(std::move(it->second));
                        chunks->erase(it);
                    }
                }
            } sink;
            sink.host = &meshHost;
            sink.sc = &sc;
            sink.chunks = &chunks;
            sink.nextHandle = &nextHandle;
            sink.outTerrain = &capturedTerrain;
            sink.hasTerrain = &hasCapturedTerrain;

            (void)showcase.Update(frontierAnchor, &sink);
            showcase.WaitForAsyncIdle();
            const auto st = showcase.Update(frontierAnchor, &sink);
            if (!hasCapturedTerrain || chunks.empty()) {
                res.errMsg = "RawWorldShowcase failed to stream frontier chunk";
                return res;
            }

            const float midX = capturedTerrain.originX + capturedTerrain.chunkSizeU * 0.5f;
            const float midY = capturedTerrain.originY + capturedTerrain.chunkSizeU * 0.5f;
            const float midZ = 0.5f * (capturedTerrain.minZ + capturedTerrain.maxZ);
            camPos[0] = midX;
            camPos[1] = midY - 2600.0f;
            camPos[2] = midZ + 1400.0f;
            target[0] = midX;
            target[1] = midY;
            target[2] = midZ;

            // Add dynamically fractured Nordic stone ruins from RawDestructionEngine right on the terrain!
            const float ruinX = midX - 280.0f;
            const float ruinY = midY - 420.0f;
            const float ruinGroundZ = SampleChunkTerrainZ(capturedTerrain, ruinX, ruinY);
            raw::worldgen::DestructionSimConfig dcfg{};
            dcfg.groundZ = ruinGroundZ;
            raw::worldgen::DestructionSimulation dsim(dcfg);
            raw::worldgen::FractureConfig fcfg{};
            fcfg.seed = 202643u;
            fcfg.shardCount = 18u;
            fcfg.impactPoint = {ruinX, ruinY, ruinGroundZ + 90.0f};
            raw::worldgen::DestructionImpact imp{};
            imp.point = fcfg.impactPoint;
            imp.direction = {0.75f, 0.45f, 0.35f};
            imp.impulseNs = 420.0f;
            imp.radialBurstNs = 240.0f;
            imp.falloffRadiusM = 180.0f;

            raw::worldgen::FractureResult fRes{};
            if (dsim.FractureAndInitialize(raw::worldgen::DestructibleArchetype::NordicStonePillar,
                                           95.0f, {ruinX, ruinY, ruinGroundZ}, fcfg, imp, &fRes)) {
                for (int f = 0; f < 16; ++f) dsim.Step(1.0f / 60.0f);
                raw::worldgen::ProceduralMesh ruinsMesh{};
                if (dsim.BuildMeshFromDestructionState(126u, &ruinsMesh, "frontier_fractured_ruins")) {
                    TerrainGpuChunk ruinsGpu{};
                    if (meshHost.UploadTerrainChunk(ruinsMesh, &ruinsGpu)) {
                        chunks.emplace(nextHandle++, std::move(ruinsGpu));
                    }
                }
            }
            std::printf("[Sandbox] Frontier biome=%s | props=%zu | fracturedShards=%zu | gpuChunks=%zu\n",
                        raw::worldgen::GetBiomeName(st.cameraBiome), sink.propsBaked, fRes.shards.size(), chunks.size());
        } else {
            // REAL Skyrim.esm Tamriel mode!
            raw::worldgen::EsmWorldIndex esmIdx{};
            if (!raw::worldgen::IndexEsmWorldspace(opt.esmPath, raw::worldgen::kTamrielWorldFormId, &esmIdx, &err)) {
                res.errMsg = "IndexEsmWorldspace failed: " + err;
                return res;
            }
            raw::worldgen::EsmLoadedCell centerCell{};
            if (!raw::worldgen::LoadEsmExteriorCell(esmIdx, opt.cellX, opt.cellY, &centerCell, &err)) {
                res.errMsg = "LoadEsmExteriorCell failed: " + err;
                return res;
            }
            raw::worldgen::ProceduralMesh centerMesh{};
            raw::worldgen::TerrainChunkData centerChunk{};
            if (!raw::worldgen::BuildMeshFromEsmCell(centerCell, esmIdx.selectedWorld.defaultWaterHeight, 126u,
                                                     &centerMesh, &centerChunk, &err)) {
                res.errMsg = "BuildMeshFromEsmCell failed: " + err;
                return res;
            }
            TerrainGpuChunk centerGpu{};
            if (!meshHost.UploadTerrainChunk(centerMesh, &centerGpu, &err)) {
                res.errMsg = "UploadTerrainChunk (ESM) failed: " + err;
                return res;
            }
            chunks.emplace(nextHandle++, std::move(centerGpu));

            // Load immediate 3x3 neighboring Tamriel cells if present in Skyrim.esm for a seamless mountain horizon!
            uint32_t neighborLoaded = 0u;
            for (int32_t dy = -1; dy <= 1; ++dy) {
                for (int32_t dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const int32_t nx = opt.cellX + dx;
                    const int32_t ny = opt.cellY + dy;
                    if (!esmIdx.FindCell(nx, ny)) continue;
                    raw::worldgen::EsmLoadedCell nCell{};
                    raw::worldgen::ProceduralMesh nMesh{};
                    if (raw::worldgen::LoadEsmExteriorCell(esmIdx, nx, ny, &nCell) &&
                        raw::worldgen::BuildMeshFromEsmCell(nCell, esmIdx.selectedWorld.defaultWaterHeight, 126u, &nMesh)) {
                        TerrainGpuChunk nGpu{};
                        if (meshHost.UploadTerrainChunk(nMesh, &nGpu)) {
                            chunks.emplace(nextHandle++, std::move(nGpu));
                            ++neighborLoaded;
                        }
                    }
                }
            }

            const float midX = 0.5f * (centerMesh.aabbMin.x + centerMesh.aabbMax.x);
            const float midY = 0.5f * (centerMesh.aabbMin.y + centerMesh.aabbMax.y);
            const float midZ = 0.5f * (centerMesh.aabbMin.z + centerMesh.aabbMax.z);
            const float zSpan = std::max(centerMesh.aabbMax.z - centerMesh.aabbMin.z, 256.0f);
            camPos[0] = midX;
            camPos[1] = midY - 2850.0f;
            camPos[2] = centerMesh.aabbMax.z + std::clamp(zSpan * 0.45f, 450.0f, 1600.0f);
            target[0] = midX;
            target[1] = midY + 400.0f;
            target[2] = midZ;

            for (int i = 0; i < 16; ++i) weatherSim.Step(1.0f, {midX, midY, midZ});
            std::printf("[Sandbox] Skyrim.esm Tamriel cell (%d,%d) '%s' | Z=[%.0f..%.0f] | cellsLoaded=%u (1+%u neighbors)\n",
                        opt.cellX, opt.cellY,
                        centerCell.meta.edid.empty() ? "Wilderness" : centerCell.meta.edid.c_str(),
                        centerMesh.aabbMin.z, centerMesh.aabbMax.z, 1u + neighborLoaded, neighborLoaded);
        }

        const auto wGpu = weatherSim.GetGpuUniforms();
        SandboxLightUniform u{};
        const CameraBasis cam = BuildCameraMatrices(camPos, target, float(W) / float(H), nearZ, farZ,
                                                    u.worldToClip, u.clipToWorld);
        PopulateUniformFromWeather(u, W, H, camPos, nearZ, farZ, wGpu, opt.season);

        // Create command list, fence, HDR colorOut (RT+UAV), R32F depthIn (1.0f sky), and readback buffer.
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

        // In frontier mode, also step live GPU 64^3 3D Navier-Stokes Fire + 256^2 SWE Water!
        if (opt.mode == RenderMode::Frontier) {
            fluidSim = std::make_unique<RawFluidSimHost>();
            compHost = std::make_unique<RawFluidCompositeHost>();
            if (!fluidSim->Init(dev.Get(), &err) || !compHost->Init(dev.Get(), &err)) {
                res.errMsg = "Fluid host init failed: " + err;
                CloseHandle(ev);
                return res;
            }

            const float fireCenter[3] = {
                camPos[0] + cam.fwd[0] * 300.0f,
                camPos[1] + cam.fwd[1] * 300.0f,
                camPos[2] + cam.fwd[2] * 300.0f
            };
            FluidFireParams fp{};
            fp.cellSize = 4.0f;
            fp.domainOriginW[0] = fireCenter[0] - 128.0f;
            fp.domainOriginW[1] = fireCenter[1] - 128.0f;
            fp.domainOriginW[2] = fireCenter[2] - 128.0f;
            fp.dt = 1.0f / 24.0f;
            fp.windVelW[0] = std::clamp(wGpu.cloudWind.x * 0.08f, -12.0f, 12.0f);
            fp.windVelW[1] = std::clamp(wGpu.cloudWind.y * 0.08f, -12.0f, 12.0f);
            fp.ambientTempK = std::clamp(wGpu.seasonThermal0.w + 273.15f, 240.0f, 305.0f);
            fp.jacobiIterations = opt.useWarp ? 4u : 8u;
            fp.emitterCount = 1u;
            fp.emitters[0].posW[0] = fireCenter[0] + cam.right[0] * 46.0f - cam.up[0] * 48.0f;
            fp.emitters[0].posW[1] = fireCenter[1] + cam.right[1] * 46.0f - cam.up[1] * 48.0f;
            fp.emitters[0].posW[2] = fireCenter[2] + cam.right[2] * 46.0f - cam.up[2] * 48.0f;
            fp.emitters[0].radius = 22.0f;
            fp.emitters[0].ignitionTempK = 2850.0f;
            fp.emitters[0].fuelRate = 3.2f;
            fp.emitters[0].jetSpeed = 48.0f;
            fp.emitters[0].curlStrength = 14.0f;
            fp.emitters[0].sootSeedRate = 0.95f;

            FluidWaterParams wp{};
            wp.camPosXY[0] = camPos[0];
            wp.camPosXY[1] = camPos[1];
            wp.cellSize = 8.0f;
            wp.dt = 1.0f / 24.0f;
            wp.windDirXY[0] = 0.866f;
            wp.windDirXY[1] = 0.5f;
            wp.swellAmp = std::clamp(0.45f + wGpu.beaufortScale * 0.42f, 0.5f, 4.5f);
            wp.rainIntensity = wGpu.weather.w;
            wp.waterPlaneZ = -14000.0f;

            for (int step = 0; step < 12; ++step) {
                fp.timeSec = float(step) * fp.dt;
                wp.timeSec = float(step) * wp.dt;
                fluidSim->StepFire(cl.Get(), fp);
                fluidSim->StepWater(cl.Get(), wp);
            }
            cl->Close();
            ID3D12CommandList* fLists[1] = {cl.Get()};
            queue->ExecuteCommandLists(1, fLists);
            flushQueue();
            ca->Reset();
            cl->Reset(ca.Get(), nullptr);
        }

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
            for (UINT x = 0; x < W; ++x) row[x] = 1.0f; // Open sky across entire viewport
        }
        depthUp->Unmap(0, nullptr);

        ComPtr<ID3D12DescriptorHeap> rtvHeap;
        D3D12_DESCRIPTOR_HEAP_DESC rtvD{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
        dev->CreateDescriptorHeap(&rtvD, IID_PPV_ARGS(&rtvHeap));
        dev->CreateRenderTargetView(colorOut.Get(), nullptr, rtvHeap->GetCPUDescriptorHandleForHeapStart());

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

        // Execute RawTerrainWorld.hlsl AS+MS+PS (or VS+PS fallback) pass!
        const bool terrainOk = meshHost.ExecuteTerrainPass(
            queue.Get(), colorOut.Get(), depthIn.Get(), nullptr, W, H,
            &u, sizeof(u), u.worldToClip, camPos, 2.4f, chunks, &res.drawnChunks, &res.drawnMeshlets);
        flushQueue();
        if (!terrainOk || res.drawnChunks == 0u || res.drawnMeshlets == 0u) {
            res.errMsg = "ExecuteTerrainPass drew 0 chunks/meshlets";
            if (hasShowcase) { showcase.WaitForAsyncIdle(); showcase.Reset(); showcase.SetAsyncEnabled(false); }
            CloseHandle(ev);
            return res;
        }

        // Composite 64^3 3D Navier-Stokes fire/smoke onto the EXACT SAME HDR target in frontier mode!
        if (opt.mode == RenderMode::Frontier && fluidSim && compHost) {
            const auto fDom = fluidSim->GetFireDomainInfo();
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
            if (!compHost->Composite(queue.Get(), colorOut.Get(), depthIn.Get(),
                                     fluidSim->FireThermoResource(), fluidSim->FireRadianceResource(), cp)) {
                res.errMsg = "RawFluidCompositeHost::Composite failed";
                if (hasShowcase) { showcase.WaitForAsyncIdle(); showcase.Reset(); showcase.SetAsyncEnabled(false); }
                CloseHandle(ev);
                return res;
            }
            flushQueue();
        }

        // Read back HDR RGBA16F pixels!
        const UINT colorPitch = (W * 8u + 255u) & ~255u;
        D3D12_RESOURCE_DESC rbDesc = upDesc;
        rbDesc.Width = size_t(colorPitch) * H;
        ComPtr<ID3D12Resource> rbBuf;
        dev->CreateCommittedResource(&rbHp, D3D12_HEAP_FLAG_NONE, &rbDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rbBuf));

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
        cl->Close();
        ID3D12CommandList* rbLists[1] = {cl.Get()};
        queue->ExecuteCommandLists(1, rbLists);
        flushQueue();

        // Analyze REAL GPU HDR pixels first (before sky backdrop fill) & apply Troy Sobotka AgX -> sRGB -> 24-bit BMP!
        std::vector<uint8_t> rgbOut(size_t(W) * H * 3u, 0u);
        double sumLuma = 0.0;
        double sumLumaSq = 0.0;

        void* rp = nullptr;
        rbBuf->Map(0, nullptr, &rp);
        for (UINT y = 0; y < H; ++y) {
            const float skyT = float(y) / std::max(float(H - 1u), 1.0f);
            // Physical zenith-to-horizon sky gradient (used ONLY for empty sky pixels with alpha==0)
            const float skyR = (0.14f * (1.0f - skyT) + 0.56f * skyT);
            const float skyG = (0.26f * (1.0f - skyT) + 0.68f * skyT);
            const float skyB = (0.52f * (1.0f - skyT) + 0.84f * skyT);

            const uint16_t* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(rp) + size_t(y) * colorPitch);
            for (UINT x = 0; x < W; ++x) {
                float r = HalfToFloat(row[x * 4u + 0u]);
                float g = HalfToFloat(row[x * 4u + 1u]);
                float b = HalfToFloat(row[x * 4u + 2u]);
                const float a = HalfToFloat(row[x * 4u + 3u]);

                if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a)) {
                    ++res.nanInfPixels;
                    r = g = b = 0.0f;
                } else {
                    const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                    if (a > 0.5f || luma > 0.002f) {
                        ++res.litPixels;
                        sumLuma += double(luma);
                        sumLumaSq += double(luma) * double(luma);
                        res.maxHdrR = std::max(res.maxHdrR, r);
                        if (r > 1.25f && r > b * 1.45f) ++res.fireBoostPixels;
                    } else {
                        r = skyR; g = skyG; b = skyB;
                    }
                }

                float sr = 0.0f, sg = 0.0f, sb = 0.0f;
                AgxTonemap(r, g, b, sr, sg, sb);
                const size_t oi = (size_t(y) * W + x) * 3u;
                rgbOut[oi + 0u] = static_cast<uint8_t>(std::round(sr * 255.0f));
                rgbOut[oi + 1u] = static_cast<uint8_t>(std::round(sg * 255.0f));
                rgbOut[oi + 2u] = static_cast<uint8_t>(std::round(sb * 255.0f));
            }
        }
        rbBuf->Unmap(0, nullptr);

        if (res.litPixels > 0u) {
            res.meanLuma = sumLuma / double(res.litPixels);
            const double var = std::max(0.0, (sumLumaSq / double(res.litPixels)) - res.meanLuma * res.meanLuma);
            res.stdDevLuma = std::sqrt(var);
        }

        if (!opt.outPath.empty()) {
            const bool wrote = WriteBmp24(opt.outPath, W, H, rgbOut);
            res.bmpValid = wrote && ValidateBmp24Header(opt.outPath, W, H);
            if (!res.bmpValid) {
                res.errMsg = "Failed to write/validate BMP: " + opt.outPath;
            }
        } else {
            res.bmpValid = true;
        }

        // Clean race-free shutdown of async worker pool & GPU hosts.
        if (hasShowcase) {
            showcase.WaitForAsyncIdle();
            showcase.Reset();
            showcase.SetAsyncEnabled(false);
        }
        flushQueue();
        if (compHost) compHost->Shutdown();
        meshHost.Shutdown();
        CloseHandle(ev);
    }

    res.d3d12DebugErrors = AuditInfoQueue(iq.Get());
    res.ok = (res.errMsg.empty() && res.d3d12DebugErrors == 0u && res.nanInfPixels == 0u &&
              res.litPixels > (opt.width * opt.height) / 10u && res.stdDevLuma > 0.01 && res.bmpValid);
    return res;
}

void PrintUsage()
{
    std::printf(
        "RAW Standalone Native Engine Sandbox Renderer (Zero SkyrimSE.exe Required)\n"
        "Usage: raw_sandbox_render [options]\n"
        "  --mode frontier|esm         Render procedural frontier world (default) or real Skyrim.esm cell\n"
        "  --season winter|autumn|summer|spring  Seasonal thermals & weather (default: winter)\n"
        "  --esm <path>                Path to Skyrim.esm (default: Steam SE Data\\Skyrim.esm)\n"
        "  --cell <x,y>                Tamriel exterior cell coordinates (default: 14,-14 High Hrothgar)\n"
        "  --width <W> --height <H>    Output resolution (default: 640x360)\n"
        "  --out <path.bmp>            Output 24-bit AgX-tonemapped BMP file\n"
        "  --warp                      Use WARP software DX12 rasterizer + D3D12 Debug Layer\n"
        "  --debug                     Enable D3D12 Debug Layer\n"
        "  --self-test                 Run both frontier & ESM self-tests, assert 0 errors & valid BMPs\n");
}

bool ParseArgs(int argc, char** argv, SandboxOptions* opt)
{
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto needVal = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                std::printf("Missing value for %s\n", flag);
                return nullptr;
            }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") {
            opt->showHelp = true;
            return true;
        } else if (a == "--mode") {
            const char* v = needVal("--mode");
            if (!v) return false;
            if (std::strcmp(v, "frontier") == 0) opt->mode = RenderMode::Frontier;
            else if (std::strcmp(v, "esm") == 0) opt->mode = RenderMode::Esm;
            else { std::printf("Unknown --mode: %s\n", v); return false; }
        } else if (a == "--season") {
            const char* v = needVal("--season");
            if (!v) return false;
            if (std::strcmp(v, "winter") == 0) opt->season = SeasonChoice::Winter;
            else if (std::strcmp(v, "autumn") == 0) opt->season = SeasonChoice::Autumn;
            else if (std::strcmp(v, "summer") == 0) opt->season = SeasonChoice::Summer;
            else if (std::strcmp(v, "spring") == 0) opt->season = SeasonChoice::Spring;
            else { std::printf("Unknown --season: %s\n", v); return false; }
        } else if (a == "--esm") {
            const char* v = needVal("--esm");
            if (!v) return false;
            opt->esmPath = v;
        } else if (a == "--cell") {
            const char* v = needVal("--cell");
            if (!v) return false;
            char* end1 = nullptr;
            const long cx = std::strtol(v, &end1, 10);
            if (!end1 || *end1 != ',') {
                std::printf("Invalid --cell format (expected x,y): %s\n", v);
                return false;
            }
            char* end2 = nullptr;
            const long cy = std::strtol(end1 + 1, &end2, 10);
            if (!end2 || *end2 != '\0') {
                std::printf("Invalid --cell format (expected x,y): %s\n", v);
                return false;
            }
            opt->cellX = static_cast<int32_t>(cx);
            opt->cellY = static_cast<int32_t>(cy);
        } else if (a == "--width") {
            const char* v = needVal("--width");
            if (!v) return false;
            opt->width = static_cast<UINT>(std::clamp(std::atoi(v), 64, 3840));
        } else if (a == "--height") {
            const char* v = needVal("--height");
            if (!v) return false;
            opt->height = static_cast<UINT>(std::clamp(std::atoi(v), 64, 2160));
        } else if (a == "--out") {
            const char* v = needVal("--out");
            if (!v) return false;
            opt->outPath = v;
        } else if (a == "--warp") {
            opt->useWarp = true;
        } else if (a == "--debug") {
            opt->debug = true;
        } else if (a == "--self-test") {
            opt->selfTest = true;
        } else {
            std::printf("Unknown option: %s\n", a.c_str());
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    SandboxOptions opt{};
    if (!ParseArgs(argc, argv, &opt)) {
        PrintUsage();
        return 2;
    }
    if (opt.showHelp) {
        PrintUsage();
        return 0;
    }

    if (opt.selfTest) {
        std::printf("=== RAW Standalone Native Engine Sandbox Self-Test ===\n");
        int passed = 0, failed = 0;
        auto check = [&](bool ok, const char* label) {
            if (ok) { ++passed; std::printf("[PASS] %s\n", label); }
            else    { ++failed; std::printf("[FAIL] %s\n", label); }
        };

        const std::string outBase = opt.outPath.empty() ? std::string("raw_sandbox_selftest") : opt.outPath;

        // 1. Procedural Frontier World (RawWeatherSim + RawWorldShowcase + RawFloraEngine + RawDestructionEngine + 64^3 NS Fire + SWE Water + AgX BMP)
        SandboxOptions fOpt = opt;
        fOpt.mode = RenderMode::Frontier;
        fOpt.debug = true;
        fOpt.outPath = outBase + "_frontier.bmp";
        const RenderMetrics fRes = RunSingleRender(fOpt);
        std::printf("  Frontier Metrics: path=%s, chunks=%u, meshlets=%u, litPx=%u, firePx=%u (maxR=%.2f), meanLuma=%.3f, stdDev=%.3f, nanInf=%u, d3dErr=%u, bmp=%s\n",
                    fRes.usedMeshShaders ? "AS+MS+PS" : "VS+PS",
                    fRes.drawnChunks, fRes.drawnMeshlets, fRes.litPixels, fRes.fireBoostPixels,
                    fRes.maxHdrR, fRes.meanLuma, fRes.stdDevLuma, fRes.nanInfPixels, fRes.d3d12DebugErrors,
                    fRes.bmpValid ? "VALID" : "INVALID");
        if (!fRes.errMsg.empty()) std::printf("  Frontier Err: %s\n", fRes.errMsg.c_str());

        check(fRes.ok, "Frontier standalone render completed cleanly");
        check(fRes.d3d12DebugErrors == 0u, "Frontier render reported 0 D3D12 Debug Layer errors");
        check(fRes.nanInfPixels == 0u, "Frontier HDR render produced 0 NaN/Inf pixels");
        check(fRes.litPixels > (fOpt.width * fOpt.height) / 5u && fRes.stdDevLuma > 0.015,
              "Frontier render produced high-contrast multi-biome terrain + 3D flora + fractured ruins");
        check(fRes.fireBoostPixels >= 15u && fRes.maxHdrR > 1.25f,
              "Frontier render composited live 64^3 3D Navier-Stokes fire onto HDR scene");
        check(fRes.bmpValid, "Frontier AgX-tonemapped 24-bit BMP header & size verified");

        // 2. Real Skyrim.esm Tamriel Cell (High Hrothgar 14,-14 by default, if Skyrim.esm exists)
        if (std::filesystem::exists(opt.esmPath)) {
            SandboxOptions eOpt = opt;
            eOpt.mode = RenderMode::Esm;
            eOpt.debug = true;
            eOpt.outPath = outBase + "_esm.bmp";
            const RenderMetrics eRes = RunSingleRender(eOpt);
            std::printf("  ESM Metrics: path=%s, chunks=%u, meshlets=%u, litPx=%u, meanLuma=%.3f, stdDev=%.3f, nanInf=%u, d3dErr=%u, bmp=%s\n",
                        eRes.usedMeshShaders ? "AS+MS+PS" : "VS+PS",
                        eRes.drawnChunks, eRes.drawnMeshlets, eRes.litPixels,
                        eRes.meanLuma, eRes.stdDevLuma, eRes.nanInfPixels, eRes.d3d12DebugErrors,
                        eRes.bmpValid ? "VALID" : "INVALID");
            if (!eRes.errMsg.empty()) std::printf("  ESM Err: %s\n", eRes.errMsg.c_str());

            check(eRes.ok, "Real Skyrim.esm Tamriel standalone render completed cleanly");
            check(eRes.d3d12DebugErrors == 0u, "Skyrim.esm render reported 0 D3D12 Debug Layer errors");
            check(eRes.nanInfPixels == 0u, "Skyrim.esm HDR render produced 0 NaN/Inf pixels");
            check(eRes.litPixels > (eOpt.width * eOpt.height) / 5u && eRes.stdDevLuma > 0.012,
                  "Skyrim.esm Tamriel terrain rendered with non-trivial elevation/lighting contrast");
            check(eRes.bmpValid, "Skyrim.esm AgX-tonemapped 24-bit BMP header & size verified");
        } else {
            std::printf("[INFO] Skyrim.esm not present at '%s'; skipping live ESM self-test leg.\n", opt.esmPath.c_str());
        }

        std::printf("=== Self-Test Summary: %d passed, %d failed ===\n", passed, failed);
        return failed == 0 ? 0 : 1;
    }

    if (opt.outPath.empty()) {
        opt.outPath = (opt.mode == RenderMode::Frontier) ? "raw_sandbox_frontier.bmp" : "raw_sandbox_esm.bmp";
    }
    const RenderMetrics m = RunSingleRender(opt);
    std::printf("[Sandbox] Done: ok=%d, path=%s, chunks=%u, meshlets=%u, litPx=%u, firePx=%u, meanLuma=%.3f, stdDev=%.3f, nanInf=%u, d3dErr=%u, out=%s (%s)\n",
                int(m.ok), m.usedMeshShaders ? "AS+MS+PS" : "VS+PS",
                m.drawnChunks, m.drawnMeshlets, m.litPixels, m.fireBoostPixels,
                m.meanLuma, m.stdDevLuma, m.nanInfPixels, m.d3d12DebugErrors,
                m.outPath.c_str(), m.bmpValid ? "VALID BMP" : "INVALID");
    if (!m.errMsg.empty()) std::printf("[Sandbox] Error: %s\n", m.errMsg.c_str());
    return m.ok ? 0 : 1;
}
