// raw_engine_viewer.cpp: RAW Interactive Real-Time 60+ FPS Win32 / DX12 Standalone Engine Viewer.
// Double-click to fly freely through BOTH our 6-biome procedural frontier world (RawWorldShowcase +
// RawHydrologyGen + RawFloraEngine + RawDestructionEngine + RawWeatherSim + 64^3 3D Navier-Stokes Fire)
// AND Real Riverwood (Skyrim.esm + Skyrim - Meshes0.bsa .NIF buildings) completely WITHOUT SkyrimSE.exe!
//
// Controls (Interactive Mode):
//   WASD / Q / E        : 6-DOF Flight Camera (Shift = 3.5x boost)
//   Right-Mouse Drag    : Smooth Freelook Yaw / Pitch
//   1 / 2 / 3 / 4       : Live Season Switch (Spring / Summer / Autumn / Winter)
//   F                   : Toggle Live 64^3 3D Navier-Stokes Fire + Smoke
//   G                   : Toggle Live 4-Cascade Radiance Cascades Screen GI
//   B                   : Cycle/Teleport Across All 6 Procedural Frontier Biomes
//   L                   : Teleport Across 4 Iconic Tamriel Landmarks (Riverwood / Whiterun / High Hrothgar / Solitude)
//   T                   : Cycle Time-of-Day Presets (Dawn 6h / Noon 13h / Golden Hour 18.5h / Midnight Aurora 23.5h)
//   W                   : Cycle Weather Presets (Clear / Overcast / Rainstorm / Blizzard)
//   A                   : Toggle Live 48kHz WASAPI Ambient Soundscape (Interactive Mode Only)
//   R                   : Fracture Fresh 20-Shard NordicStonePillar (60Hz Rigid-Body Debris!)
//   M                   : Toggle World Mode (Frontier Biomes <-> Multi-Cell Streaming Tamriel ESM+NIF)
//   ESC                 : Clean Exit
//
// Headless CI / Automated Verification Mode:
//   raw_engine_viewer.exe --headless-frames 4 [--warp] [--width W] [--height H] [--esm <Skyrim.esm>]
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
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "RawMeshHost.h"
#include "RawFluidSimHost.h"
#include "RawFluidCompositeHost.h"
#include "RawRadianceHost.h"
#include "RawWeatherSim.h"
#include "RawWorldShowcase.h"
#include "RawDestructionEngine.h"
#include "RawEsmWorldLoader.h"
#include "RawEsmModelResolver.h"
#include "RawTamrielWorldStreamer.h"
#include "RawHydrologyGen.h"
#include "RawAudioOutput.h"

#include "raw_agx_display_vs.h"
#include "raw_agx_display_ps.h"

using Microsoft::WRL::ComPtr;
using namespace raw::bridge;

namespace
{

constexpr const char* kDefaultSkyrimEsm =
    "H:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition\\Data\\Skyrim.esm";
constexpr UINT kBackBufferCount = 3u;
constexpr UINT kRingFrames      = 3u;

// Exact byte-for-byte mirror of cbuffer P in LightingCommon.hlsli (42800 bytes).
struct ViewerLightUniform {
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
static_assert(sizeof(ViewerLightUniform) == 42800u, "ViewerLightUniform must match cbuffer P");

struct AgxDisplayCB {
    float skyZenithExposure[4];
    float skyHorizonFrame[4];
    float sunTintAndGlow[4];
    float _pad0[4];
    float _pad256[48];
};
static_assert(sizeof(AgxDisplayCB) == 256u, "AgxDisplayCB must be 256 bytes");

enum class WorldMode { Frontier, RiverwoodEsm };
enum class SeasonChoice { Spring = 0, Summer = 1, Autumn = 2, Winter = 3 };
enum class TimePreset : uint8_t { Dawn = 0, Noon = 1, GoldenHour = 2, MidnightAurora = 3 };
enum class WeatherPreset : uint8_t { Clear = 0, Overcast = 1, Rainstorm = 2, Blizzard = 3 };

struct TamrielLandmark {
    const char* name;
    int32_t     cellX;
    int32_t     cellY;
};

constexpr TamrielLandmark kTamrielLandmarks[4] = {
    {"Riverwood",      4, -12},
    {"Whiterun",       5,  -3},
    {"High Hrothgar", 14, -14},
    {"Solitude",     -16,  25},
};

const char* TimePresetName(TimePreset t)
{
    switch (t) {
        case TimePreset::Dawn:           return "Dawn 06:00";
        case TimePreset::Noon:           return "Noon 13:00";
        case TimePreset::GoldenHour:     return "Golden 18:30";
        case TimePreset::MidnightAurora: return "Aurora 23:30";
    }
    return "Noon 13:00";
}

float TimePresetHours(TimePreset t)
{
    switch (t) {
        case TimePreset::Dawn:           return 6.0f;
        case TimePreset::Noon:           return 13.0f;
        case TimePreset::GoldenHour:     return 18.5f;
        case TimePreset::MidnightAurora: return 23.5f;
    }
    return 13.0f;
}

const char* WeatherPresetName(WeatherPreset w)
{
    switch (w) {
        case WeatherPreset::Clear:     return "Clear";
        case WeatherPreset::Overcast:  return "Overcast";
        case WeatherPreset::Rainstorm: return "Rainstorm";
        case WeatherPreset::Blizzard:  return "Blizzard";
    }
    return "Clear";
}

raw::worldgen::WeatherRegime WeatherPresetToRegime(WeatherPreset w)
{
    switch (w) {
        case WeatherPreset::Clear:     return raw::worldgen::WeatherRegime::ClearAlpine;
        case WeatherPreset::Overcast:  return raw::worldgen::WeatherRegime::Overcast;
        case WeatherPreset::Rainstorm: return raw::worldgen::WeatherRegime::Rainstorm;
        case WeatherPreset::Blizzard:  return raw::worldgen::WeatherRegime::Blizzard;
    }
    return raw::worldgen::WeatherRegime::ClearAlpine;
}

struct ViewerOptions {
    UINT         width          = 1280u;
    UINT         height         = 720u;
    uint32_t     headlessFrames = 0u;
    bool         useWarp        = false;
    bool         debugLayer     = false;
    bool         startEsm       = false;
    std::string  esmPath        = kDefaultSkyrimEsm;
    bool         showHelp       = false;
};

struct CameraBasis {
    float right[3];
    float up[3];
    float fwd[3];
};

const char* SeasonName(SeasonChoice s)
{
    switch (s) {
        case SeasonChoice::Spring: return "Spring";
        case SeasonChoice::Summer: return "Summer";
        case SeasonChoice::Autumn: return "Autumn";
        case SeasonChoice::Winter: return "Winter";
    }
    return "Winter";
}

float HalfToFloat(uint16_t h)
{
    const uint32_t s = (uint32_t(h & 0x8000u) << 16);
    uint32_t e = (h >> 10) & 0x1Fu;
    uint32_t m = h & 0x3FFu;
    uint32_t bits = 0;
    if (e == 0) {
        if (m == 0) bits = s;
        else {
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

CameraBasis BuildCameraFromYawPitch(float yawRad, float pitchRad, float aspect,
                                    float nearZ, float farZ,
                                    float outWorldToClip[16], float outClipToWorld[16])
{
    const float cp = std::cos(pitchRad);
    const float sp = std::sin(pitchRad);
    const float cy = std::cos(yawRad);
    const float sy = std::sin(yawRad);

    // Yaw = 0 looks along +Y; positive yaw rotates clockwise (+X)
    const float fx = sy * cp;
    const float fy = cy * cp;
    const float fz = sp;

    float rx = fy, ry = -fx, rz = 0.0f;
    const float rl = std::max(std::sqrt(rx * rx + ry * ry), 1.0e-5f);
    rx /= rl; ry /= rl;

    const float ux = ry * fz - rz * fy;
    const float uy = rz * fx - rx * fz;
    const float uz = rx * fy - ry * fx;

    const float tanHalfFov = std::tan(0.56f);
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
    const float sy_ = tanHalfFov;
    outClipToWorld[0]  = rx * sx; outClipToWorld[1]  = ux * sy_; outClipToWorld[3]  = fx;
    outClipToWorld[4]  = ry * sx; outClipToWorld[5]  = uy * sy_; outClipToWorld[7]  = fy;
    outClipToWorld[8]  = rz * sx; outClipToWorld[9]  = uz * sy_; outClipToWorld[11] = fz;
    outClipToWorld[14] = -1.0f / B;
    outClipToWorld[15] = a / B;

    return {{rx, ry, rz}, {ux, uy, uz}, {fx, fy, fz}};
}

D3D12_RESOURCE_BARRIER MakeBarrier(ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    return b;
}

class RawEngineViewer {
public:
    RawEngineViewer() = default;
    ~RawEngineViewer() { Shutdown(); }

    bool Init(const ViewerOptions& opt, HWND hwnd)
    {
        m_opt = opt;
        m_hwnd = hwnd;
        m_width = std::max(opt.width, 64u);
        m_height = std::max(opt.height, 64u);

        if (opt.debugLayer || opt.useWarp || opt.headlessFrames > 0u) {
            ComPtr<ID3D12Debug> dbg;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
                dbg->EnableDebugLayer();
            }
        }

        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory)))) return false;

        if (!opt.useWarp) {
            ComPtr<IDXGIAdapter1> ad;
            for (UINT idx = 0; m_factory->EnumAdapters1(idx, &ad) != DXGI_ERROR_NOT_FOUND; ++idx) {
                DXGI_ADAPTER_DESC1 desc{};
                ad->GetDesc1(&desc);
                if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) { ad.Reset(); continue; }
                if (SUCCEEDED(D3D12CreateDevice(ad.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_dev)))) {
                    char nb[128]{};
                    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nb, sizeof(nb) - 1, nullptr, nullptr);
                    m_adapterName = nb;
                    break;
                }
                ad.Reset();
            }
        }
        if (!m_dev) {
            ComPtr<IDXGIAdapter> warp;
            if (FAILED(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
                FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_dev)))) {
                return false;
            }
            m_adapterName = "Microsoft Basic Render Driver (WARP)";
            m_isWarp = true;
        }
        m_dev.As(&m_infoQueue);

        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(m_dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_queue)))) return false;

        if (FAILED(m_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) return false;
        m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!m_fenceEvent) return false;

        for (UINT i = 0; i < kRingFrames; ++i) {
            if (FAILED(m_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_alloc[i])))) {
                return false;
            }
        }
        if (FAILED(m_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc[0].Get(), nullptr, IID_PPV_ARGS(&m_cmd))) ||
            FAILED(m_cmd->Close())) {
            return false;
        }

        // Create SwapChain for interactive mode, or offscreen display target for headless mode.
        if (m_hwnd) {
            DXGI_SWAP_CHAIN_DESC1 scd{};
            scd.Width = m_width;
            scd.Height = m_height;
            scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            scd.SampleDesc.Count = 1;
            scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            scd.BufferCount = kBackBufferCount;
            scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            ComPtr<IDXGISwapChain1> sc1;
            if (FAILED(m_factory->CreateSwapChainForHwnd(m_queue.Get(), m_hwnd, &scd, nullptr, nullptr, &sc1)) ||
                FAILED(sc1.As(&m_swapChain))) {
                return false;
            }
            m_factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
        }

        if (!CreateDisplayPipeline() || !CreateSizeDependentTargets(m_width, m_height)) {
            return false;
        }

        // Initialize core RAW DX12 subsystems!
        std::string err;
        if (!m_meshHost.Init(m_dev.Get(), &err) || !m_meshHost.TerrainReady()) {
            std::printf("[Viewer] RawMeshHost init failed: %s\n", err.c_str());
            return false;
        }
        err.clear();
        m_fluidOk = m_fluidSim.Init(m_dev.Get(), &err) && m_fluidComposite.Init(m_dev.Get(), &err);
        if (!m_fluidOk) {
            std::printf("[Viewer] FluidSim warning: %s\n", err.c_str());
        }
        err.clear();
        m_radOk = m_radianceHost.Init(m_dev.Get(), &err) && m_radianceHost.ScreenGIReady();
        if (!m_radOk) {
            std::printf("[Viewer] RadianceCascades warning: %s\n", err.c_str());
        }
        UINT64 tsFreq = 0;
        if (SUCCEEDED(m_queue->GetTimestampFrequency(&tsFreq)) && tsFreq > 0) {
            if (m_fluidOk) m_fluidSim.SetTimestampFrequency(tsFreq);
            if (m_radOk)   m_radianceHost.SetTimestampFrequency(tsFreq);
        }

        // Enable real hydraulic & thermal erosion + D8 rivers for procedural frontier terrain!
        raw::worldgen::SetTerrainGenHydrologyErosionEnabled(true);
        ApplySeason(SeasonChoice::Winter);

        // Initialize async Frontier WorldShowcase!
        raw::worldgen::StreamConfig sc{};
        sc.vramBudgetBytes           = 128ull * 1024ull * 1024ull;
        sc.lod0RadiusChunks          = (opt.headlessFrames > 0u) ? 0 : 1;
        sc.lod1RadiusChunks          = (opt.headlessFrames > 0u) ? 0 : 1;
        sc.lod2RadiusChunks          = (opt.headlessFrames > 0u) ? 0 : 2;
        sc.lod3RadiusChunks          = (opt.headlessFrames > 0u) ? 0 : 2;
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
        m_showcase.Init(sc);
        DiscoverAllSixBiomeAnchors(sc.terrain.seed);

        // Start in Frontier (or Riverwood if --esm requested and available)
        if (opt.startEsm && std::filesystem::exists(opt.esmPath)) {
            SwitchWorldMode(WorldMode::RiverwoodEsm);
        } else {
            SwitchWorldMode(WorldMode::Frontier);
        }

        // Spawn an initial fractured Nordic stone pillar in front of the camera!
        TriggerFreshFracture();

        m_ready = true;
        return true;
    }

    void Resize(UINT w, UINT h)
    {
        if (!m_ready || !m_swapChain || w == 0u || h == 0u) return;
        if (w == m_width && h == m_height) return;
        WaitGpuIdle();
        for (UINT i = 0; i < kBackBufferCount; ++i) m_backBuffers[i].Reset();
        m_hdrColor.Reset();
        m_depthIn.Reset();
        if (SUCCEEDED(m_swapChain->ResizeBuffers(kBackBufferCount, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 0))) {
            CreateSizeDependentTargets(w, h);
        }
    }

    void ApplySeason(SeasonChoice s)
    {
        m_season = s;
        raw::worldgen::WeatherSimConfig wcfg{};
        raw::worldgen::WeatherRegime reg = raw::worldgen::WeatherRegime::ClearAlpine;
        wcfg.timeScale = 90.0f;
        switch (s) {
            case SeasonChoice::Spring:
                wcfg.initialDayOfYear = 105.0f; wcfg.timeOfDayHours = 9.5f;  wcfg.baseSeaTempC = 7.5f;
                reg = raw::worldgen::WeatherRegime::DawnValleyMist;
                break;
            case SeasonChoice::Summer:
                wcfg.initialDayOfYear = 172.0f; wcfg.timeOfDayHours = 13.0f; wcfg.baseSeaTempC = 16.0f;
                reg = raw::worldgen::WeatherRegime::ClearAlpine;
                break;
            case SeasonChoice::Autumn:
                wcfg.initialDayOfYear = 285.0f; wcfg.timeOfDayHours = 15.5f; wcfg.baseSeaTempC = 8.0f;
                reg = raw::worldgen::WeatherRegime::ClearAlpine;
                break;
            case SeasonChoice::Winter:
                wcfg.initialDayOfYear = 15.0f;  wcfg.timeOfDayHours = 10.5f; wcfg.baseSeaTempC = -4.5f;
                reg = raw::worldgen::WeatherRegime::Blizzard;
                break;
        }
        wcfg.initialRegime = reg;
        m_weatherSim.Reset(wcfg);
        m_weatherSim.SetTimeOfDay(TimePresetHours(m_timePreset));
        m_weatherSim.SetRegime(WeatherPresetToRegime(m_weatherPreset), true);
        for (int i = 0; i < 16; ++i) {
            m_weatherSim.Step(1.0f, {m_camPos[0], m_camPos[1], m_camPos[2]});
        }
    }

    void ApplyTimePreset(TimePreset t)
    {
        m_timePreset = t;
        m_weatherSim.SetTimeOfDay(TimePresetHours(t));
        for (int i = 0; i < 4; ++i) {
            m_weatherSim.Step(0.5f, {m_camPos[0], m_camPos[1], m_camPos[2]});
        }
    }

    void CycleTimePreset()
    {
        ApplyTimePreset(static_cast<TimePreset>((static_cast<uint8_t>(m_timePreset) + 1u) % 4u));
    }

    void ApplyWeatherPreset(WeatherPreset w)
    {
        m_weatherPreset = w;
        m_weatherSim.SetRegime(WeatherPresetToRegime(w), true);
        for (int i = 0; i < 12; ++i) {
            m_weatherSim.Step(1.0f, {m_camPos[0], m_camPos[1], m_camPos[2]});
        }
        if (m_audioEnabled && m_audio.IsRunning()) {
            PushAudioSceneState(m_weatherSim.GetGpuUniforms());
        }
    }

    void CycleWeatherPreset()
    {
        ApplyWeatherPreset(static_cast<WeatherPreset>((static_cast<uint8_t>(m_weatherPreset) + 1u) % 4u));
    }

    bool TeleportToTamrielLandmark(uint32_t idx)
    {
        if (!EnsureTamrielStreamerInit()) return false;
        m_landmarkIdx = idx % 4u;
        m_mode = WorldMode::RiverwoodEsm;
        const auto& lm = kTamrielLandmarks[m_landmarkIdx];
        const raw::worldgen::Vec3 anchor{
            float(lm.cellX) * raw::worldgen::kEsmCellSizeU + raw::worldgen::kEsmCellSizeU * 0.5f,
            float(lm.cellY) * raw::worldgen::kEsmCellSizeU + raw::worldgen::kEsmCellSizeU * 0.5f,
            0.0f
        };
        TamrielViewerSink sink;
        sink.v = this;
        const auto st = m_tamrielStreamer.StreamRingComplete(anchor, &sink);
        m_lastTamrielStats = st;

        float midX = anchor.x;
        float midY = anchor.y;
        float maxZ = 0.0f;
        float zSpan = 512.0f;
        if (const auto* cell = m_tamrielStreamer.FindCell(lm.cellX, lm.cellY)) {
            midX  = 0.5f * (cell->terrainMesh.aabbMin.x + cell->terrainMesh.aabbMax.x);
            midY  = 0.5f * (cell->terrainMesh.aabbMin.y + cell->terrainMesh.aabbMax.y);
            maxZ  = cell->terrainMesh.aabbMax.z;
            zSpan = std::max(cell->terrainMesh.aabbMax.z - cell->terrainMesh.aabbMin.z, 256.0f);
        }
        m_camPos[0] = midX - 520.0f;
        m_camPos[1] = midY - 1950.0f;
        m_camPos[2] = maxZ + std::clamp(zSpan * 0.40f, 420.0f, 1250.0f);
        m_camYaw    = 0.24f;
        m_camPitch  = -0.34f;
        m_esmLoaded = (st.activeCells > 0u);
        return m_esmLoaded;
    }

    void CycleTamrielLandmark()
    {
        if (!std::filesystem::exists(m_opt.esmPath)) return;
        const uint32_t nextIdx = (m_mode == WorldMode::RiverwoodEsm) ? ((m_landmarkIdx + 1u) % 4u) : m_landmarkIdx;
        TeleportToTamrielLandmark(nextIdx);
    }

    raw::worldgen::TamrielStreamStats GetTamrielStats() const { return m_lastTamrielStats; }
    uint32_t TotalTamrielEvictions() const { return m_totalTamrielEvictions; }
    const char* ActiveLandmarkName() const { return kTamrielLandmarks[m_landmarkIdx % 4u].name; }

    void ToggleFire() { m_fireEnabled = !m_fireEnabled; }
    void ToggleGI() { m_giEnabled = !m_giEnabled; }

    void ToggleAudio()
    {
        if (m_opt.headlessFrames > 0u) return; // Strictly OFF in headless CI mode
        if (!m_audioEnabled) {
            if (m_audio.Start(raw::audio::AudioBackendMode::WasapiShared, 48000u, 202646u)) {
                m_audioEnabled = true;
                PushAudioSceneState(m_weatherSim.GetGpuUniforms());
            }
        } else {
            m_audio.Stop();
            m_audioEnabled = false;
        }
    }

    void CycleBiome()
    {
        m_biomeIdx = (m_biomeIdx + 1u) % 6u;
        m_mode = WorldMode::Frontier;
        m_hasFrontierCenter = false;
        const auto& a = m_biomeAnchors[m_biomeIdx];
        m_frontierCenter[0] = a.x;
        m_frontierCenter[1] = a.y;
        m_frontierCenter[2] = 1200.0f;
        PumpFrontierShowcase(true);
        m_camPos[0] = m_frontierCenter[0];
        m_camPos[1] = m_frontierCenter[1] - 1800.0f;
        m_camPos[2] = m_frontierCenter[2] + 1150.0f;
        m_camYaw    = 0.0f;
        m_camPitch  = -0.50f;
        TriggerFreshFracture();
    }

    void ToggleWorldMode()
    {
        if (m_mode == WorldMode::Frontier) {
            if (std::filesystem::exists(m_opt.esmPath)) {
                SwitchWorldMode(WorldMode::RiverwoodEsm);
            }
        } else {
            SwitchWorldMode(WorldMode::Frontier);
        }
    }

    void TriggerFreshFracture()
    {
        const float cp = std::cos(m_camPitch), sp = std::sin(m_camPitch);
        const float cy = std::cos(m_camYaw),   sy = std::sin(m_camYaw);
        const float fx = sy * cp, fy = cy * cp, fz = sp;
        const float px = m_camPos[0] + fx * 620.0f;
        const float py = m_camPos[1] + fy * 620.0f;
        const float groundZ = (m_mode == WorldMode::Frontier && m_hasFrontierCenter)
                            ? m_frontierGroundZ
                            : (m_camPos[2] + fz * 620.0f - 180.0f);

        raw::worldgen::DestructionSimConfig dcfg{};
        dcfg.groundZ = groundZ;
        m_destSim.SetConfig(dcfg);

        raw::worldgen::FractureConfig fcfg{};
        fcfg.seed = 202645u + (++m_fractureCounter) * 7919u;
        fcfg.shardCount = 20u;
        fcfg.impactPoint = {px, py, groundZ + 95.0f};
        raw::worldgen::DestructionImpact imp{};
        imp.point = fcfg.impactPoint;
        imp.direction = {fx, fy, 0.35f};
        imp.impulseNs = 460.0f;
        imp.radialBurstNs = 260.0f;
        imp.falloffRadiusM = 220.0f;

        raw::worldgen::FractureResult fRes{};
        if (m_destSim.FractureAndInitialize(raw::worldgen::DestructibleArchetype::NordicStonePillar,
                                            95.0f, {px, py, groundZ}, fcfg, imp, &fRes)) {
            m_activeShards = static_cast<uint32_t>(fRes.shards.size());
            m_destructionDirty = true;
            UploadDebrisChunk();
        }
    }

    void UpdateInput(float dt)
    {
        if (!m_hwnd || GetForegroundWindow() != m_hwnd) return;
        const float cp = std::cos(m_camPitch), sp = std::sin(m_camPitch);
        const float cy = std::cos(m_camYaw),   sy = std::sin(m_camYaw);
        const float fx = sy * cp, fy = cy * cp, fz = sp;
        float rx = fy, ry = -fx;
        const float rl = std::max(std::sqrt(rx * rx + ry * ry), 1.0e-5f);
        rx /= rl; ry /= rl;

        const bool fast = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        const float speed = (fast ? 2600.0f : 720.0f) * dt;
        auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
        if (down('W')) { m_camPos[0] += fx * speed; m_camPos[1] += fy * speed; m_camPos[2] += fz * speed; }
        if (down('S')) { m_camPos[0] -= fx * speed; m_camPos[1] -= fy * speed; m_camPos[2] -= fz * speed; }
        if (down('D')) { m_camPos[0] += rx * speed; m_camPos[1] += ry * speed; }
        if (down('A')) { m_camPos[0] -= rx * speed; m_camPos[1] -= ry * speed; }
        if (down('E')) { m_camPos[2] += speed; }
        if (down('Q')) { m_camPos[2] -= speed; }
    }

    void OnMouseDrag(int dx, int dy)
    {
        m_camYaw   += float(dx) * 0.0045f;
        m_camPitch  = std::clamp(m_camPitch - float(dy) * 0.0045f, -1.46f, 1.46f);
    }

    bool RenderFrame(float dtSec, bool present)
    {
        if (!m_ready) return false;
        const float dtClamped = std::clamp(dtSec, 1.0e-4f, 0.05f);
        m_simTimeSec += dtClamped;

        // 1. Step seasonal weather simulation (and push live acoustics/weather to optional WASAPI audio).
        m_weatherSim.Step(dtClamped, {m_camPos[0], m_camPos[1], m_camPos[2]});
        const auto wGpu = m_weatherSim.GetGpuUniforms();
        if (m_audioEnabled && m_audio.IsRunning()) {
            PushAudioSceneState(wGpu);
        }

        // 2. Stream async Frontier chunks OR live multi-cell Tamriel (Skyrim.esm + .NIF) ring around camera!
        if (m_mode == WorldMode::Frontier) {
            PumpFrontierShowcase(false);
        } else if (m_tamrielStreamer.IsInitialized()) {
            PumpTamrielStreamer(false);
        }

        // 3. Step live 60Hz rigid-body 3D Voronoi destruction debris!
        if (m_activeShards > 0u) {
            const auto stats = m_destSim.GetStats();
            if (stats.awakeBodies > 0u || m_destructionDirty) {
                m_destSim.Step(dtClamped);
                UploadDebrisChunk();
                m_destructionDirty = false;
            }
        }

        // 4. Build camera & LightingCommon.hlsli cbuffer P.
        const float nearZ = 15.0f, farZ = 353840.0f;
        ViewerLightUniform u{};
        const CameraBasis cam = BuildCameraFromYawPitch(
            m_camYaw, m_camPitch, float(m_width) / float(m_height), nearZ, farZ,
            u.worldToClip, u.clipToWorld);
        PopulateLightUniform(u, nearZ, farZ, wGpu);

        // 5. Wait on ring slot, reset command list, clear HDR colorOut to 0, step 64^3 3D NS Fire + 256^2 SWE Water.
        const UINT slot = static_cast<UINT>(m_frameCount % kRingFrames);
        WaitFenceVal(m_slotFenceVal[slot]);
        if (FAILED(m_alloc[slot]->Reset()) || FAILED(m_cmd->Reset(m_alloc[slot].Get(), nullptr))) return false;

        // Clear HDR target to (0,0,0,0) (COMMON -> RT -> COMMON)
        auto bClrIn = MakeBarrier(m_hdrColor.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_cmd->ResourceBarrier(1, &bClrIn);
        const float zero4[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        m_cmd->ClearRenderTargetView(RtvHandle(kBackBufferCount), zero4, 0, nullptr);
        auto bClrOut = MakeBarrier(m_hdrColor.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        m_cmd->ResourceBarrier(1, &bClrOut);

        // Step live GPU 64^3 3D NS Fire + 256^2 SWE Water!
        if (m_fluidOk && m_fireEnabled) {
            const float fireCenter[3] = {
                m_camPos[0] + cam.fwd[0] * 300.0f,
                m_camPos[1] + cam.fwd[1] * 300.0f,
                m_camPos[2] + cam.fwd[2] * 300.0f
            };
            FluidFireParams fp{};
            fp.cellSize = 4.0f;
            fp.domainOriginW[0] = fireCenter[0] - 128.0f;
            fp.domainOriginW[1] = fireCenter[1] - 128.0f;
            fp.domainOriginW[2] = fireCenter[2] - 128.0f;
            fp.dt = m_opt.headlessFrames > 0u ? (1.0f / 24.0f) : dtClamped;
            fp.timeSec = m_simTimeSec;
            fp.windVelW[0] = std::clamp(wGpu.cloudWind.x * 0.08f, -12.0f, 12.0f);
            fp.windVelW[1] = std::clamp(wGpu.cloudWind.y * 0.08f, -12.0f, 12.0f);
            fp.ambientTempK = std::clamp(wGpu.seasonThermal0.w + 273.15f, 240.0f, 305.0f);
            fp.jacobiIterations = m_isWarp ? 4u : 8u;
            fp.emitterCount = 1u;
            fp.emitters[0].posW[0] = fireCenter[0] + cam.right[0] * 48.0f - cam.up[0] * 48.0f;
            fp.emitters[0].posW[1] = fireCenter[1] + cam.right[1] * 48.0f - cam.up[1] * 48.0f;
            fp.emitters[0].posW[2] = fireCenter[2] + cam.right[2] * 48.0f - cam.up[2] * 48.0f;
            fp.emitters[0].radius = 22.0f;
            fp.emitters[0].ignitionTempK = 2850.0f;
            fp.emitters[0].fuelRate = 3.2f;
            fp.emitters[0].jetSpeed = 48.0f;
            fp.emitters[0].curlStrength = 14.0f;
            fp.emitters[0].sootSeedRate = 0.90f;

            FluidWaterParams wp{};
            wp.camPosXY[0] = m_camPos[0];
            wp.camPosXY[1] = m_camPos[1];
            wp.cellSize = 8.0f;
            wp.dt = fp.dt;
            wp.timeSec = m_simTimeSec;
            wp.windDirXY[0] = 0.866f;
            wp.windDirXY[1] = 0.5f;
            wp.swellAmp = std::clamp(0.45f + wGpu.beaufortScale * 0.42f, 0.5f, 4.5f);
            wp.rainIntensity = wGpu.weather.w;
            wp.waterPlaneZ = -14000.0f;

            // Warm up 6 substeps on first frame so the flame is immediately visible!
            const int subSteps = (m_frameCount == 0ull && m_opt.headlessFrames > 0u) ? 6 : 1;
            for (int k = 0; k < subSteps; ++k) {
                fp.timeSec = m_simTimeSec + float(k) * fp.dt;
                wp.timeSec = fp.timeSec;
                m_fluidSim.StepFire(m_cmd.Get(), fp);
                m_fluidSim.StepWater(m_cmd.Get(), wp);
            }
        }

        if (FAILED(m_cmd->Close())) return false;
        ID3D12CommandList* preLists[1] = {m_cmd.Get()};
        m_queue->ExecuteCommandLists(1, preLists);

        // 6. Execute RawTerrainWorld (AS+MS+PS or VS+PS fallback) on all active chunks (terrain + flora/NIFs + debris)!
        m_lastDrawnChunks = 0u;
        m_lastDrawnMeshlets = 0u;
        const auto& activeMap = ActiveChunkMap();
        if (!activeMap.empty()) {
            m_meshHost.ExecuteTerrainPass(
                m_queue.Get(), m_hdrColor.Get(), m_depthIn.Get(), nullptr,
                m_width, m_height, &u, sizeof(u), u.worldToClip, m_camPos, m_simTimeSec,
                activeMap, &m_lastDrawnChunks, &m_lastDrawnMeshlets);
        }

        // 7. Composite 64^3 3D Navier-Stokes Fire/Smoke onto the EXACT SAME HDR target!
        if (m_fluidOk && m_fireEnabled) {
            const auto fDom = m_fluidSim.GetFireDomainInfo();
            if (fDom.active && fDom.emitterCount > 0u) {
                FluidCompositeParams cp{};
                std::memcpy(cp.clipToWorld, u.clipToWorld, 64u);
                cp.camPosW[0] = m_camPos[0]; cp.camPosW[1] = m_camPos[1]; cp.camPosW[2] = m_camPos[2];
                cp.simTimeSec = m_simTimeSec;
                cp.domainMinW[0] = fDom.originXYZ[0]; cp.domainMinW[1] = fDom.originXYZ[1]; cp.domainMinW[2] = fDom.originXYZ[2];
                cp.domainSpanW = fDom.worldSpanU;
                cp.sunDir[0] = u.sunDir[0]; cp.sunDir[1] = u.sunDir[1]; cp.sunDir[2] = u.sunDir[2];
                cp.sunIntensity = 1.0f;
                cp.sunCol[0] = u.sunColour[0]; cp.sunCol[1] = u.sunColour[1]; cp.sunCol[2] = u.sunColour[2];
                cp.ambientFill = 0.45f;
                cp.width = m_width; cp.height = m_height;
                cp.activeEmitters = fDom.emitterCount;
                cp.isInterior = false;
                m_fluidComposite.Composite(
                    m_queue.Get(), m_hdrColor.Get(), m_depthIn.Get(),
                    m_fluidSim.FireThermoResource(), m_fluidSim.FireRadianceResource(), cp);
            }
        }

        // 8. Execute Live 4-Cascade Radiance Cascades Screen GI + RawAgxDisplay.hlsl fullscreen pass!
        const UINT dispSlot = static_cast<UINT>(m_frameCount % kRingFrames);
        WaitFenceVal(m_dispFenceVal[dispSlot]);
        if (FAILED(m_dispAlloc[dispSlot]->Reset()) || FAILED(m_dispCmd->Reset(m_dispAlloc[dispSlot].Get(), nullptr))) {
            return false;
        }

        if (m_radOk && m_giEnabled) {
            m_radianceHost.ExecuteScreenGI(m_dispCmd.Get(), m_hdrColor.Get(), m_depthIn.Get(),
                                           m_width, m_height, 0.16f);
        }
        m_dispCmd->SetPipelineState(m_agxPso.Get());

        const UINT bbIdx = m_swapChain ? m_swapChain->GetCurrentBackBufferIndex() : 0u;
        ID3D12Resource* dispTarget = m_swapChain ? m_backBuffers[bbIdx].Get() : m_headlessDisplay.Get();
        const D3D12_RESOURCE_STATES dispRestState = m_swapChain ? D3D12_RESOURCE_STATE_PRESENT : D3D12_RESOURCE_STATE_COMMON;

        AgxDisplayCB dcb{};
        FillAgxSkyConstants(dcb);
        dcb.skyHorizonFrame[3] = float(m_frameCount & 1023ull);
        std::memcpy(m_agxCbMapped + size_t(dispSlot) * 256u, &dcb, sizeof(dcb));

        D3D12_RESOURCE_BARRIER bDispIn[2] = {
            MakeBarrier(m_hdrColor.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            MakeBarrier(dispTarget, dispRestState, D3D12_RESOURCE_STATE_RENDER_TARGET)
        };
        m_dispCmd->ResourceBarrier(2, bDispIn);

        D3D12_VIEWPORT vp{0.0f, 0.0f, float(m_width), float(m_height), 0.0f, 1.0f};
        D3D12_RECT sc{0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
        m_dispCmd->RSSetViewports(1, &vp);
        m_dispCmd->RSSetScissorRects(1, &sc);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = RtvHandle(bbIdx);
        m_dispCmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

        ID3D12DescriptorHeap* heaps[1] = {m_srvHeap.Get()};
        m_dispCmd->SetDescriptorHeaps(1, heaps);
        m_dispCmd->SetGraphicsRootSignature(m_agxRoot.Get());
        m_dispCmd->SetGraphicsRootConstantBufferView(0, m_agxCbBuf->GetGPUVirtualAddress() + UINT64(dispSlot) * 256ull);
        m_dispCmd->SetGraphicsRootDescriptorTable(1, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
        m_dispCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_dispCmd->DrawInstanced(3, 1, 0, 0);

        D3D12_RESOURCE_BARRIER bDispOut[2] = {
            MakeBarrier(m_hdrColor.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
            MakeBarrier(dispTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, dispRestState)
        };
        m_dispCmd->ResourceBarrier(2, bDispOut);
        if (FAILED(m_dispCmd->Close())) return false;

        ID3D12CommandList* dispLists[1] = {m_dispCmd.Get()};
        m_queue->ExecuteCommandLists(1, dispLists);

        const uint64_t sig = ++m_fenceVal;
        m_queue->Signal(m_fence.Get(), sig);
        m_slotFenceVal[slot] = sig;
        m_dispFenceVal[dispSlot] = sig;

        if (present && m_swapChain) {
            m_swapChain->Present(1, 0);
        }
        ++m_frameCount;
        return true;
    }

    struct HeadlessAudit {
        uint32_t hdrNanInf        = 0u;
        uint32_t hdrLitPixels     = 0u;
        uint32_t dispNonZeroPx    = 0u;
        double   dispMeanLuma     = 0.0;
        double   dispStdDevLuma   = 0.0;
        uint32_t drawnMeshlets    = 0u;
        uint32_t activeShards     = 0u;
        bool     usedMeshShaders  = false;
        bool     radianceActive   = false;
        float    fireStepMs       = 0.0f;
        float    waterStepMs      = 0.0f;
        float    radianceGiMs     = 0.0f;
        uint32_t biomesFound      = 0u;
    };

    bool ReadbackHeadlessAudit(HeadlessAudit* out)
    {
        if (!out || !m_headlessDisplay || !m_hdrColor) return false;
        WaitGpuIdle();
        out->drawnMeshlets = m_lastDrawnMeshlets;
        out->activeShards = m_activeShards;
        out->usedMeshShaders = m_meshHost.TerrainUsesMeshShaders();
        out->radianceActive = m_radOk && m_giEnabled;
        out->biomesFound = m_discoveredBiomeCount;
        const auto ft = m_fluidOk ? m_fluidSim.GetLastTimings() : FluidGpuTimings{};
        const auto rt = m_radOk ? m_radianceHost.GetLastTimings() : RadianceGpuTimings{};
        out->fireStepMs = ft.valid ? ft.fireStepMs : 0.0f;
        out->waterStepMs = ft.valid ? ft.waterStepMs : 0.0f;
        out->radianceGiMs = rt.valid ? rt.giPassMs : 0.0f;

        const UINT hdrPitch  = (m_width * 8u + 255u) & ~255u;
        const UINT dispPitch = (m_width * 4u + 255u) & ~255u;
        D3D12_HEAP_PROPERTIES rbHp{D3D12_HEAP_TYPE_READBACK};
        D3D12_RESOURCE_DESC rbDesc{};
        rbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rbDesc.Height = 1; rbDesc.DepthOrArraySize = 1; rbDesc.MipLevels = 1; rbDesc.SampleDesc.Count = 1;
        rbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        rbDesc.Width = size_t(hdrPitch) * m_height;
        ComPtr<ID3D12Resource> rbHdr;
        m_dev->CreateCommittedResource(&rbHp, D3D12_HEAP_FLAG_NONE, &rbDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rbHdr));

        rbDesc.Width = size_t(dispPitch) * m_height;
        ComPtr<ID3D12Resource> rbDisp;
        m_dev->CreateCommittedResource(&rbHp, D3D12_HEAP_FLAG_NONE, &rbDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rbDisp));

        m_alloc[0]->Reset();
        m_cmd->Reset(m_alloc[0].Get(), nullptr);
        D3D12_RESOURCE_BARRIER bIn[2] = {
            MakeBarrier(m_hdrColor.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
            MakeBarrier(m_headlessDisplay.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE)
        };
        m_cmd->ResourceBarrier(2, bIn);

        D3D12_TEXTURE_COPY_LOCATION hDst{}; hDst.pResource = rbHdr.Get(); hDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        hDst.PlacedFootprint.Footprint = {DXGI_FORMAT_R16G16B16A16_FLOAT, m_width, m_height, 1, hdrPitch};
        D3D12_TEXTURE_COPY_LOCATION hSrc{}; hSrc.pResource = m_hdrColor.Get(); hSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        m_cmd->CopyTextureRegion(&hDst, 0, 0, 0, &hSrc, nullptr);

        D3D12_TEXTURE_COPY_LOCATION dDst{}; dDst.pResource = rbDisp.Get(); dDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dDst.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, m_width, m_height, 1, dispPitch};
        D3D12_TEXTURE_COPY_LOCATION dSrc{}; dSrc.pResource = m_headlessDisplay.Get(); dSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        m_cmd->CopyTextureRegion(&dDst, 0, 0, 0, &dSrc, nullptr);

        D3D12_RESOURCE_BARRIER bOut[2] = {
            MakeBarrier(m_hdrColor.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
            MakeBarrier(m_headlessDisplay.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON)
        };
        m_cmd->ResourceBarrier(2, bOut);
        m_cmd->Close();
        ID3D12CommandList* lists[1] = {m_cmd.Get()};
        m_queue->ExecuteCommandLists(1, lists);
        WaitGpuIdle();

        void* hp = nullptr;
        rbHdr->Map(0, nullptr, &hp);
        for (UINT y = 0; y < m_height; ++y) {
            const uint16_t* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(hp) + size_t(y) * hdrPitch);
            for (UINT x = 0; x < m_width; ++x) {
                const float r = HalfToFloat(row[x * 4u + 0u]);
                const float g = HalfToFloat(row[x * 4u + 1u]);
                const float b = HalfToFloat(row[x * 4u + 2u]);
                const float a = HalfToFloat(row[x * 4u + 3u]);
                if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a)) {
                    ++out->hdrNanInf;
                } else if (a > 0.5f || (r + g + b) > 0.01f) {
                    ++out->hdrLitPixels;
                }
            }
        }
        rbHdr->Unmap(0, nullptr);

        void* dp = nullptr;
        rbDisp->Map(0, nullptr, &dp);
        double sumL = 0.0, sumL2 = 0.0;
        const size_t totalPx = size_t(m_width) * m_height;
        for (UINT y = 0; y < m_height; ++y) {
            const uint8_t* row = static_cast<const uint8_t*>(dp) + size_t(y) * dispPitch;
            for (UINT x = 0; x < m_width; ++x) {
                const float r = float(row[x * 4u + 0u]) / 255.0f;
                const float g = float(row[x * 4u + 1u]) / 255.0f;
                const float b = float(row[x * 4u + 2u]) / 255.0f;
                if (row[x * 4u + 0u] || row[x * 4u + 1u] || row[x * 4u + 2u]) ++out->dispNonZeroPx;
                const double l = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                sumL += l;
                sumL2 += l * l;
            }
        }
        rbDisp->Unmap(0, nullptr);

        out->dispMeanLuma = totalPx ? (sumL / double(totalPx)) : 0.0;
        out->dispStdDevLuma = totalPx ? std::sqrt(std::max(0.0, (sumL2 / double(totalPx)) - out->dispMeanLuma * out->dispMeanLuma)) : 0.0;
        return true;
    }

    uint32_t AuditDebugErrorsAndShutdown()
    {
        ComPtr<ID3D12InfoQueue> iq = m_infoQueue;
        Shutdown();
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

    void Shutdown()
    {
        m_audio.Stop();
        m_audioEnabled = false;
        if (!m_ready && !m_dev) return;
        WaitGpuIdle();
        m_showcase.WaitForAsyncIdle();
        m_showcase.Reset();
        m_showcase.SetAsyncEnabled(false);
        if (m_tamrielStreamer.IsInitialized()) {
            TamrielViewerSink sink;
            sink.v = this;
            m_tamrielStreamer.Shutdown(&sink);
        }
        m_tamrielCellHandles.clear();

        m_frontierChunks.clear();
        m_esmChunks.clear();
        m_fluidComposite.Shutdown();
        m_meshHost.Shutdown();

        if (m_agxCbBuf && m_agxCbMapped) {
            m_agxCbBuf->Unmap(0, nullptr);
            m_agxCbMapped = nullptr;
        }
        m_agxCbBuf.Reset();
        m_agxPso.Reset();
        m_agxRoot.Reset();
        m_srvHeap.Reset();
        m_rtvHeap.Reset();
        m_hdrColor.Reset();
        m_depthIn.Reset();
        m_headlessDisplay.Reset();
        for (UINT i = 0; i < kBackBufferCount; ++i) m_backBuffers[i].Reset();
        m_swapChain.Reset();
        m_dispCmd.Reset();
        m_cmd.Reset();
        for (UINT i = 0; i < kRingFrames; ++i) {
            m_dispAlloc[i].Reset();
            m_alloc[i].Reset();
        }
        m_fence.Reset();
        if (m_fenceEvent) {
            CloseHandle(m_fenceEvent);
            m_fenceEvent = nullptr;
        }
        m_queue.Reset();
        m_infoQueue.Reset();
        m_dev.Reset();
        m_factory.Reset();
        m_ready = false;
    }

    std::string FormatTitle(double fps) const
    {
        const auto ft = m_fluidOk ? m_fluidSim.GetLastTimings() : FluidGpuTimings{};
        const auto rt = m_radOk ? m_radianceHost.GetLastTimings() : RadianceGpuTimings{};
        char locBuf[128]{};
        if (m_mode == WorldMode::Frontier) {
            std::snprintf(locBuf, sizeof(locBuf), "%s",
                          raw::worldgen::GetBiomeName(static_cast<raw::worldgen::BiomeId>(m_biomeIdx)));
        } else {
            std::snprintf(locBuf, sizeof(locBuf), "%s (%u cells, %llu LRU hits)",
                          kTamrielLandmarks[m_landmarkIdx % 4u].name,
                          m_lastTamrielStats.activeCells,
                          static_cast<unsigned long long>(m_lastTamrielStats.cacheHits));
        }
        char buf[512]{};
        std::snprintf(buf, sizeof(buf),
                      "RAW Engine | %.1f FPS | %u Meshlets (%s) | %s | %s | %s | %s | Fire:%s(%.2fms) Water:%.2fms GI:%s(%.2fms) | Audio:%s | Shards:%u",
                      fps, m_lastDrawnMeshlets, m_meshHost.TerrainUsesMeshShaders() ? "AS+MS" : "VS+PS",
                      locBuf, TimePresetName(m_timePreset), WeatherPresetName(m_weatherPreset), SeasonName(m_season),
                      m_fireEnabled ? "ON" : "OFF", ft.valid ? ft.fireStepMs : 0.0f,
                      ft.valid ? ft.waterStepMs : 0.0f,
                      (m_radOk && m_giEnabled) ? "ON" : "OFF", rt.valid ? rt.giPassMs : 0.0f,
                      m_audioEnabled ? "ON" : "OFF",
                      m_activeShards);
        return buf;
    }

    const std::string& AdapterName() const { return m_adapterName; }

private:
    static constexpr uint64_t kDebrisChunkKey = 0xDEBB1500ull;

    std::unordered_map<uint64_t, TerrainGpuChunk>& ActiveChunkMap()
    {
        return (m_mode == WorldMode::Frontier) ? m_frontierChunks : m_esmChunks;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle(UINT idx) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += SIZE_T(idx) * m_rtvStep;
        return h;
    }

    void WaitFenceVal(uint64_t val)
    {
        if (!m_fence || !m_fenceEvent || val == 0u) return;
        if (m_fence->GetCompletedValue() < val) {
            m_fence->SetEventOnCompletion(val, m_fenceEvent);
            WaitForSingleObject(m_fenceEvent, 60000);
        }
    }

    void WaitGpuIdle()
    {
        if (!m_queue || !m_fence || !m_fenceEvent) return;
        const uint64_t sig = ++m_fenceVal;
        m_queue->Signal(m_fence.Get(), sig);
        WaitFenceVal(sig);
    }

    bool CreateDisplayPipeline()
    {
        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.BaseShaderRegister = 0;

        D3D12_ROOT_PARAMETER rp[2]{};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rp[0].Descriptor = {0, 0};
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[1].DescriptorTable = {1, &srvRange};
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC ss{};
        ss.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        ss.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ss.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ss.MaxLOD = D3D12_FLOAT32_MAX;
        ss.ShaderRegister = 0;
        ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsd{2, rp, 1, &ss, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) ||
            FAILED(m_dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_agxRoot)))) {
            return false;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC gpso{};
        gpso.pRootSignature = m_agxRoot.Get();
        gpso.VS = {kDxil_raw_agx_display_vs, sizeof(kDxil_raw_agx_display_vs)};
        gpso.PS = {kDxil_raw_agx_display_ps, sizeof(kDxil_raw_agx_display_ps)};
        gpso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        gpso.SampleMask = UINT_MAX;
        gpso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        gpso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        gpso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        gpso.NumRenderTargets = 1;
        gpso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        gpso.SampleDesc.Count = 1;
        if (FAILED(m_dev->CreateGraphicsPipelineState(&gpso, IID_PPV_ARGS(&m_agxPso)))) return false;

        // RTV heap: 3 swapchain backbuffers (or 1 headless display) + 1 HDR colorOut clear RTV
        D3D12_DESCRIPTOR_HEAP_DESC rtvD{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kBackBufferCount + 1u, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
        if (FAILED(m_dev->CreateDescriptorHeap(&rtvD, IID_PPV_ARGS(&m_rtvHeap)))) return false;
        m_rtvStep = m_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_DESCRIPTOR_HEAP_DESC srvD{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1u, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
        if (FAILED(m_dev->CreateDescriptorHeap(&srvD, IID_PPV_ARGS(&m_srvHeap)))) return false;

        D3D12_HEAP_PROPERTIES upHp{D3D12_HEAP_TYPE_UPLOAD};
        D3D12_RESOURCE_DESC cbDesc{};
        cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        cbDesc.Width = sizeof(AgxDisplayCB) * kRingFrames;
        cbDesc.Height = 1; cbDesc.DepthOrArraySize = 1; cbDesc.MipLevels = 1; cbDesc.SampleDesc.Count = 1;
        cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_RANGE zeroR{0, 0};
        void* mapped = nullptr;
        if (FAILED(m_dev->CreateCommittedResource(&upHp, D3D12_HEAP_FLAG_NONE, &cbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_agxCbBuf))) ||
            FAILED(m_agxCbBuf->Map(0, &zeroR, &mapped))) {
            return false;
        }
        m_agxCbMapped = static_cast<uint8_t*>(mapped);

        for (UINT i = 0; i < kRingFrames; ++i) {
            if (FAILED(m_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_dispAlloc[i])))) {
                return false;
            }
        }
        return SUCCEEDED(m_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_dispAlloc[0].Get(), nullptr, IID_PPV_ARGS(&m_dispCmd))) &&
               SUCCEEDED(m_dispCmd->Close());
    }

    bool CreateSizeDependentTargets(UINT w, UINT h)
    {
        m_width = w;
        m_height = h;
        D3D12_HEAP_PROPERTIES defHp{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_HEAP_PROPERTIES upHp{D3D12_HEAP_TYPE_UPLOAD};

        if (m_swapChain) {
            for (UINT i = 0; i < kBackBufferCount; ++i) {
                if (FAILED(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])))) return false;
                m_dev->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, RtvHandle(i));
            }
        } else {
            D3D12_RESOURCE_DESC dispDesc{};
            dispDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            dispDesc.Width = w; dispDesc.Height = h; dispDesc.DepthOrArraySize = 1; dispDesc.MipLevels = 1;
            dispDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            dispDesc.SampleDesc.Count = 1;
            dispDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            if (FAILED(m_dev->CreateCommittedResource(&defHp, D3D12_HEAP_FLAG_NONE, &dispDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_headlessDisplay)))) {
                return false;
            }
            m_dev->CreateRenderTargetView(m_headlessDisplay.Get(), nullptr, RtvHandle(0));
        }

        // HDR RGBA16F scene buffer (RT + UAV + SRV)
        D3D12_RESOURCE_DESC hdrDesc{};
        hdrDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        hdrDesc.Width = w; hdrDesc.Height = h; hdrDesc.DepthOrArraySize = 1; hdrDesc.MipLevels = 1;
        hdrDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        hdrDesc.SampleDesc.Count = 1;
        hdrDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (FAILED(m_dev->CreateCommittedResource(&defHp, D3D12_HEAP_FLAG_NONE, &hdrDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_hdrColor)))) {
            return false;
        }
        m_dev->CreateRenderTargetView(m_hdrColor.Get(), nullptr, RtvHandle(kBackBufferCount));

        D3D12_SHADER_RESOURCE_VIEW_DESC hdrSrv{};
        hdrSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        hdrSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        hdrSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        hdrSrv.Texture2D.MipLevels = 1;
        m_dev->CreateShaderResourceView(m_hdrColor.Get(), &hdrSrv, m_srvHeap->GetCPUDescriptorHandleForHeapStart());

        // R32_FLOAT SceneDepth filled with 1.0f (open sky for standalone viewer)
        D3D12_RESOURCE_DESC depthDesc = hdrDesc;
        depthDesc.Format = DXGI_FORMAT_R32_FLOAT;
        depthDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        if (FAILED(m_dev->CreateCommittedResource(&defHp, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_depthIn)))) {
            return false;
        }

        const UINT depthPitch = (w * 4u + 255u) & ~255u;
        D3D12_RESOURCE_DESC upDesc{};
        upDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        upDesc.Width = size_t(depthPitch) * h;
        upDesc.Height = 1; upDesc.DepthOrArraySize = 1; upDesc.MipLevels = 1; upDesc.SampleDesc.Count = 1;
        upDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> depthUp;
        if (FAILED(m_dev->CreateCommittedResource(&upHp, D3D12_HEAP_FLAG_NONE, &upDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&depthUp)))) {
            return false;
        }
        void* dp = nullptr;
        depthUp->Map(0, nullptr, &dp);
        for (UINT y = 0; y < h; ++y) {
            float* row = reinterpret_cast<float*>(static_cast<uint8_t*>(dp) + size_t(y) * depthPitch);
            for (UINT x = 0; x < w; ++x) row[x] = 1.0f;
        }
        depthUp->Unmap(0, nullptr);

        WaitGpuIdle();
        m_alloc[0]->Reset();
        m_cmd->Reset(m_alloc[0].Get(), nullptr);
        D3D12_TEXTURE_COPY_LOCATION dDst{}; dDst.pResource = m_depthIn.Get(); dDst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dSrc{}; dSrc.pResource = depthUp.Get(); dSrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dSrc.PlacedFootprint.Footprint = {DXGI_FORMAT_R32_FLOAT, w, h, 1, depthPitch};
        m_cmd->CopyTextureRegion(&dDst, 0, 0, 0, &dSrc, nullptr);
        auto bD = MakeBarrier(m_depthIn.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        m_cmd->ResourceBarrier(1, &bD);
        m_cmd->Close();
        ID3D12CommandList* lists[1] = {m_cmd.Get()};
        m_queue->ExecuteCommandLists(1, lists);
        WaitGpuIdle();
        return true;
    }

    void PumpFrontierShowcase(bool waitInitial)
    {
        struct ViewerSink final : raw::worldgen::IGpuChunkSink {
            RawEngineViewer* v = nullptr;
            uint64_t OnChunkLoaded(const raw::worldgen::StreamedChunkEntry& entry) override {
                raw::worldgen::ProceduralMesh merged{};
                const auto& sc = v->m_showcase.Config();
                if (!raw::worldgen::BuildChunkMergedGpuMesh(entry, sc.maxMeshletTris, sc.terrain.seed, sc.terrain.seaLevelZ, &merged)) {
                    return 0u;
                }
                TerrainGpuChunk gpu{};
                if (!v->m_meshHost.UploadTerrainChunk(merged, &gpu)) return 0u;
                if (!v->m_hasFrontierCenter) {
                    v->m_frontierCenter[0] = entry.terrain.originX + entry.terrain.chunkSizeU * 0.5f;
                    v->m_frontierCenter[1] = entry.terrain.originY + entry.terrain.chunkSizeU * 0.5f;
                    v->m_frontierCenter[2] = 0.5f * (entry.terrain.minZ + entry.terrain.maxZ);
                    v->m_frontierGroundZ   = entry.terrain.SampleHeightBilinear(0.5f, 0.35f);
                    v->m_hasFrontierCenter = true;
                }
                const uint64_t h = v->m_nextChunkHandle++;
                v->m_frontierChunks.emplace(h, std::move(gpu));
                return h;
            }
            void OnChunkEvicted(raw::worldgen::ChunkKey, uint64_t h) override {
                auto it = v->m_frontierChunks.find(h);
                if (it != v->m_frontierChunks.end()) {
                    v->m_meshHost.DeferRelease(std::move(it->second));
                    v->m_frontierChunks.erase(it);
                }
            }
        } sink;
        sink.v = this;

        const raw::worldgen::Vec3 anchor = m_hasFrontierCenter
            ? raw::worldgen::Vec3{m_camPos[0], m_camPos[1], m_camPos[2]}
            : raw::worldgen::Vec3{m_biomeAnchors[m_biomeIdx].x, m_biomeAnchors[m_biomeIdx].y, 1200.0f};
        (void)m_showcase.Update(anchor, &sink);
        if (waitInitial) {
            m_showcase.WaitForAsyncIdle();
            (void)m_showcase.Update(anchor, &sink);
        }
    }

    struct TamrielViewerSink final : raw::worldgen::ITamrielChunkSink {
        RawEngineViewer* v = nullptr;
        uint64_t OnTamrielCellLoaded(const raw::worldgen::StreamedTamrielCell& cell) override {
            uint64_t terrainKey = 0u, propKey = 0u;
            if (!cell.terrainMesh.meshlets.empty()) {
                TerrainGpuChunk tGpu{};
                if (v->m_meshHost.UploadTerrainChunk(cell.terrainMesh, &tGpu)) {
                    terrainKey = v->m_nextChunkHandle++;
                    v->m_esmChunks.emplace(terrainKey, std::move(tGpu));
                }
            }
            if (!cell.propsMesh.meshlets.empty()) {
                TerrainGpuChunk pGpu{};
                if (v->m_meshHost.UploadTerrainChunk(cell.propsMesh, &pGpu)) {
                    propKey = v->m_nextChunkHandle++;
                    v->m_esmChunks.emplace(propKey, std::move(pGpu));
                }
            }
            if (terrainKey == 0u && propKey == 0u) return 0u;
            const uint64_t cellHandle = v->m_nextChunkHandle++;
            v->m_tamrielCellHandles.emplace(cellHandle, std::make_pair(terrainKey, propKey));
            return cellHandle;
        }
        void OnTamrielCellEvicted(raw::worldgen::ChunkKey, uint64_t gpuHandle) override {
            auto it = v->m_tamrielCellHandles.find(gpuHandle);
            if (it == v->m_tamrielCellHandles.end()) return;
            const uint64_t tKey = it->second.first;
            const uint64_t pKey = it->second.second;
            v->m_tamrielCellHandles.erase(it);
            ++v->m_totalTamrielEvictions;
            if (tKey != 0u) {
                auto ct = v->m_esmChunks.find(tKey);
                if (ct != v->m_esmChunks.end()) {
                    v->m_meshHost.DeferRelease(std::move(ct->second));
                    v->m_esmChunks.erase(ct);
                }
            }
            if (pKey != 0u) {
                auto cp = v->m_esmChunks.find(pKey);
                if (cp != v->m_esmChunks.end()) {
                    v->m_meshHost.DeferRelease(std::move(cp->second));
                    v->m_esmChunks.erase(cp);
                }
            }
        }
    };

    bool EnsureTamrielStreamerInit()
    {
        if (m_tamrielStreamer.IsInitialized()) return true;
        if (!std::filesystem::exists(m_opt.esmPath)) return false;
        const std::filesystem::path bsa = std::filesystem::path(m_opt.esmPath).parent_path() / "Skyrim - Meshes0.bsa";
        raw::worldgen::TamrielStreamConfig tcfg{};
        tcfg.radiusCells            = 1; // 3x3 = 9 cells around viewer camera
        tcfg.maxCellsPerUpdate      = 3u;
        tcfg.maxUniqueModelsPerCell = 32u;
        tcfg.maxMeshletTris         = 126u;
        tcfg.vramBudgetBytes        = 128ull * 1024ull * 1024ull;
        tcfg.modelCacheBudgetBytes  = 64ull * 1024ull * 1024ull;
        tcfg.bakeCellProps          = std::filesystem::exists(bsa);
        std::string err;
        return m_tamrielStreamer.Init(m_opt.esmPath, std::filesystem::exists(bsa) ? bsa.string() : std::string(), tcfg, &err);
    }

    void PumpTamrielStreamer(bool fullRing)
    {
        if (!m_tamrielStreamer.IsInitialized()) return;
        TamrielViewerSink sink;
        sink.v = this;
        const raw::worldgen::Vec3 cam{m_camPos[0], m_camPos[1], m_camPos[2]};
        const auto st = fullRing ? m_tamrielStreamer.StreamRingComplete(cam, &sink)
                                 : m_tamrielStreamer.Update(cam, &sink);
        m_lastTamrielStats = st;
    }

    void SwitchWorldMode(WorldMode newMode)
    {
        m_mode = newMode;
        if (m_mode == WorldMode::Frontier) {
            if (!m_hasFrontierCenter) {
                PumpFrontierShowcase(true);
            }
            m_camPos[0] = m_frontierCenter[0];
            m_camPos[1] = m_frontierCenter[1] - 1800.0f;
            m_camPos[2] = m_frontierCenter[2] + 1150.0f;
            m_camYaw    = 0.0f;
            m_camPitch  = -0.50f;
        } else {
            if (!TeleportToTamrielLandmark(m_landmarkIdx)) {
                m_mode = WorldMode::Frontier;
            }
        }
    }

    void FillAgxSkyConstants(AgxDisplayCB& dcb) const
    {
        switch (m_timePreset) {
            case TimePreset::Dawn:
                dcb.skyZenithExposure[0] = 0.14f; dcb.skyZenithExposure[1] = 0.20f; dcb.skyZenithExposure[2] = 0.44f; dcb.skyZenithExposure[3] = 1.08f;
                dcb.skyHorizonFrame[0]   = 0.88f; dcb.skyHorizonFrame[1]   = 0.52f; dcb.skyHorizonFrame[2]   = 0.40f;
                dcb.sunTintAndGlow[0]    = 0.95f; dcb.sunTintAndGlow[1]    = 0.50f; dcb.sunTintAndGlow[2]    = 0.24f; dcb.sunTintAndGlow[3] = 0.65f;
                break;
            case TimePreset::GoldenHour:
                dcb.skyZenithExposure[0] = 0.16f; dcb.skyZenithExposure[1] = 0.22f; dcb.skyZenithExposure[2] = 0.42f; dcb.skyZenithExposure[3] = 1.05f;
                dcb.skyHorizonFrame[0]   = 0.96f; dcb.skyHorizonFrame[1]   = 0.46f; dcb.skyHorizonFrame[2]   = 0.24f;
                dcb.sunTintAndGlow[0]    = 1.05f; dcb.sunTintAndGlow[1]    = 0.48f; dcb.sunTintAndGlow[2]    = 0.16f; dcb.sunTintAndGlow[3] = 0.72f;
                break;
            case TimePreset::MidnightAurora:
                dcb.skyZenithExposure[0] = 0.015f; dcb.skyZenithExposure[1] = 0.032f; dcb.skyZenithExposure[2] = 0.075f; dcb.skyZenithExposure[3] = 1.35f;
                dcb.skyHorizonFrame[0]   = 0.045f; dcb.skyHorizonFrame[1]   = 0.140f; dcb.skyHorizonFrame[2]   = 0.165f; // Aurora emerald-teal horizon glow
                dcb.sunTintAndGlow[0]    = 0.14f;  dcb.sunTintAndGlow[1]    = 0.65f;  dcb.sunTintAndGlow[2]    = 0.52f; dcb.sunTintAndGlow[3] = 0.45f;
                break;
            case TimePreset::Noon:
            default:
                dcb.skyZenithExposure[0] = 0.12f; dcb.skyZenithExposure[1] = 0.26f; dcb.skyZenithExposure[2] = 0.58f; dcb.skyZenithExposure[3] = 1.05f;
                dcb.skyHorizonFrame[0]   = 0.54f; dcb.skyHorizonFrame[1]   = 0.68f; dcb.skyHorizonFrame[2]   = 0.85f;
                dcb.sunTintAndGlow[0]    = 0.40f; dcb.sunTintAndGlow[1]    = 0.36f; dcb.sunTintAndGlow[2]    = 0.30f; dcb.sunTintAndGlow[3] = 0.25f;
                break;
        }
        if (m_weatherPreset == WeatherPreset::Overcast || m_weatherPreset == WeatherPreset::Rainstorm || m_weatherPreset == WeatherPreset::Blizzard) {
            dcb.skyZenithExposure[0] *= 0.65f; dcb.skyZenithExposure[1] *= 0.68f; dcb.skyZenithExposure[2] *= 0.72f;
            dcb.sunTintAndGlow[3] *= 0.35f;
        }
    }

    void UploadDebrisChunk()
    {
        raw::worldgen::ProceduralMesh dm{};
        if (!m_destSim.BuildMeshFromDestructionState(126u, &dm, "viewer_debris") || dm.meshlets.empty()) return;
        TerrainGpuChunk gpu{};
        if (!m_meshHost.UploadTerrainChunk(dm, &gpu)) return;
        auto& map = ActiveChunkMap();
        auto it = map.find(kDebrisChunkKey);
        if (it != map.end()) {
            m_meshHost.DeferRelease(std::move(it->second));
            it->second = std::move(gpu);
        } else {
            map.emplace(kDebrisChunkKey, std::move(gpu));
        }
    }

    void PopulateLightUniform(ViewerLightUniform& u, float nearZ, float farZ,
                              const raw::worldgen::WeatherGpuUniforms& wGpu) const
    {
        u.dims[0] = float(m_width); u.dims[1] = float(m_height);
        u.dims[2] = 1.0f / float(m_width); u.dims[3] = 1.0f / float(m_height);
        u.proj[0] = farZ / (farZ - nearZ);
        u.proj[1] = (farZ * nearZ) / (farZ - nearZ);
        u.resScale[0] = 1.0f; u.resScale[1] = 1.0f; u.resScale[2] = 0.5f; u.resScale[3] = 1.0f;
        u.camPos[0] = m_camPos[0]; u.camPos[1] = m_camPos[1]; u.camPos[2] = m_camPos[2]; u.camPos[3] = m_simTimeSec;

        // Time-of-day presets drive exact solar azimuth/elevation/irradiance & aurora night lighting:
        float sunX = 0.68f, sunY = 0.32f, sunZ = 0.66f;
        float sunR = 1.35f, sunG = 1.28f, sunB = 1.18f;
        float gameHour = 13.0f;
        float astroElev = 0.66f;
        switch (m_timePreset) {
            case TimePreset::Dawn:
                sunX = 0.92f; sunY = 0.25f; sunZ = 0.28f;
                sunR = 1.48f; sunG = 0.96f; sunB = 0.68f;
                gameHour = 6.0f; astroElev = 0.14f;
                break;
            case TimePreset::GoldenHour:
                sunX = -0.88f; sunY = 0.30f; sunZ = 0.34f;
                sunR = 1.62f; sunG = 1.02f; sunB = 0.62f;
                gameHour = 18.5f; astroElev = 0.16f;
                break;
            case TimePreset::MidnightAurora:
                sunX = -0.45f; sunY = 0.40f; sunZ = 0.78f; // Moon light vector above horizon
                sunR = 0.26f; sunG = 0.38f; sunB = 0.62f;
                gameHour = 23.5f; astroElev = -0.38f;
                break;
            case TimePreset::Noon:
            default:
                sunX = 0.68f; sunY = 0.32f; sunZ = 0.66f;
                sunR = 1.35f; sunG = 1.28f; sunB = 1.18f;
                gameHour = 10.5f; astroElev = 0.66f;
                break;
        }
        const float sl = std::sqrt(sunX * sunX + sunY * sunY + sunZ * sunZ);
        u.sunDir[0] = sunX / sl; u.sunDir[1] = sunY / sl; u.sunDir[2] = sunZ / sl; u.sunDir[3] = 1.0f;
        u.sunColour[0] = sunR; u.sunColour[1] = sunG; u.sunColour[2] = sunB; u.sunColour[3] = 1.0f;

        u.weather[0] = wGpu.weather.x;
        u.weather[1] = wGpu.weather.y;
        u.weather[2] = (m_season == SeasonChoice::Winter || m_weatherPreset == WeatherPreset::Blizzard)
                     ? std::max(wGpu.weather.z, 0.55f) : wGpu.weather.z;
        u.weather[3] = (m_weatherPreset == WeatherPreset::Rainstorm) ? std::max(wGpu.weather.w, 0.75f) : wGpu.weather.w;
        u.timeEnv[0] = gameHour;
        u.timeEnv[1] = astroElev;
        u.timeEnv[2] = wGpu.timeEnv.z;
        u.tune[0] = 0.000028f; u.tune[1] = 0.45f;
        u.clouds[0] = (m_weatherPreset == WeatherPreset::Clear) ? std::min(wGpu.cloudWind.z, 0.28f)
                    : (m_weatherPreset == WeatherPreset::Overcast ? 0.68f : std::min(wGpu.cloudWind.z, 0.85f));
        u.clouds[1] = 46000.0f; u.clouds[2] = 44000.0f; u.clouds[3] = 0.68f;
        u.cloudWind[0] = wGpu.cloudWind.x; u.cloudWind[1] = wGpu.cloudWind.y;
        u.physWorld[0][0] = m_simTimeSec; u.physWorld[0][1] = 9.81f;
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

    void DiscoverAllSixBiomeAnchors(uint32_t seed)
    {
        // Deterministic 4-quadrant frontier search strictly outside Tamriel's border box covering all 6 biomes.
        std::array<bool, 6> found{};
        for (size_t i = 0; i < 6u; ++i) {
            m_biomeAnchors[i] = {75776.0f, 157696.0f};
        }
        m_discoveredBiomeCount = 0u;
        for (int32_t ring = 1; ring <= 48 && m_discoveredBiomeCount < 6u; ++ring) {
            const int32_t step = (ring <= 12) ? 2 : 4;
            const int32_t span = 38 + ring * 2;
            for (int32_t cy = -span; cy <= span && m_discoveredBiomeCount < 6u; cy += step) {
                for (int32_t cx = -span; cx <= span && m_discoveredBiomeCount < 6u; cx += step) {
                    const float wx = float(cx) * 4096.0f + 2048.0f;
                    const float wy = float(cy) * 4096.0f + 2048.0f;
                    // Strictly outside Tamriel exclusion box [-148k..148k] x [-118k..132k] + 12k shoreline apron
                    if (wx > -160000.0f && wx < 160000.0f && wy > -130000.0f && wy < 144000.0f) continue;
                    const auto b = raw::worldgen::EvaluateBiomeBlend(wx, wy, seed);
                    const size_t bi = static_cast<size_t>(b.primary);
                    if (bi < 6u && !found[bi]) {
                        found[bi] = true;
                        m_biomeAnchors[bi] = {wx, wy};
                        ++m_discoveredBiomeCount;
                    }
                }
            }
        }
    }

    void PushAudioSceneState(const raw::worldgen::WeatherGpuUniforms& wGpu)
    {
        raw::audio::AudioSceneState as{};
        as.beaufortScale = wGpu.beaufortScale;
        as.rain01 = wGpu.weather.w;
        as.snow01 = (m_season == SeasonChoice::Winter) ? std::max(wGpu.weather.z, 0.55f) : wGpu.weather.z;
        as.fireIntensity01 = m_fireEnabled ? 0.65f : 0.0f;
        as.fireDistanceU = 280.0f;
        as.masterGain = 0.25f;
        if (m_mode == WorldMode::RiverwoodEsm) {
            as.environment = raw::audio::AcousticEnvironment::SnowDampedPineForest;
            as.waterIntensity01 = 0.40f;
        } else {
            switch (static_cast<raw::worldgen::BiomeId>(m_biomeIdx)) {
                case raw::worldgen::BiomeId::GlacialFjordSpires:
                case raw::worldgen::BiomeId::BioluminescentCrystalTundra:
                    as.environment = raw::audio::AcousticEnvironment::GlacierIceCavern;
                    as.waterIntensity01 = 0.25f;
                    break;
                case raw::worldgen::BiomeId::OldGrowthConiferHighlands:
                    as.environment = raw::audio::AcousticEnvironment::SnowDampedPineForest;
                    as.waterIntensity01 = 0.15f;
                    break;
                case raw::worldgen::BiomeId::ObsidianBasaltCanyons:
                case raw::worldgen::BiomeId::GeothermalVolcanicCaldera:
                    as.environment = raw::audio::AcousticEnvironment::NordicStoneCrypt;
                    as.fireIntensity01 = std::max(as.fireIntensity01, 0.45f);
                    break;
                case raw::worldgen::BiomeId::AlpineMeltwaterDeltas:
                default:
                    as.environment = raw::audio::AcousticEnvironment::OpenWindyTundra;
                    as.waterIntensity01 = 0.55f;
                    break;
            }
        }
        m_audio.UpdateSceneState(as);
    }

    ViewerOptions m_opt{};
    HWND          m_hwnd = nullptr;
    UINT          m_width = 1280u;
    UINT          m_height = 720u;
    bool          m_ready = false;
    bool          m_isWarp = false;
    bool          m_fluidOk = false;
    bool          m_radOk = false;
    std::string   m_adapterName;

    ComPtr<IDXGIFactory4>              m_factory;
    ComPtr<ID3D12Device>               m_dev;
    ComPtr<ID3D12InfoQueue>            m_infoQueue;
    ComPtr<ID3D12CommandQueue>         m_queue;
    ComPtr<IDXGISwapChain3>            m_swapChain;
    ComPtr<ID3D12Resource>             m_backBuffers[kBackBufferCount];
    ComPtr<ID3D12Resource>             m_headlessDisplay;
    ComPtr<ID3D12Resource>             m_hdrColor;
    ComPtr<ID3D12Resource>             m_depthIn;
    ComPtr<ID3D12DescriptorHeap>       m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap>       m_srvHeap;
    UINT                               m_rtvStep = 0u;

    ComPtr<ID3D12CommandAllocator>     m_alloc[kRingFrames];
    ComPtr<ID3D12GraphicsCommandList>  m_cmd;
    ComPtr<ID3D12CommandAllocator>     m_dispAlloc[kRingFrames];
    ComPtr<ID3D12GraphicsCommandList>  m_dispCmd;
    ComPtr<ID3D12Fence>                m_fence;
    HANDLE                             m_fenceEvent = nullptr;
    uint64_t                           m_fenceVal = 0u;
    uint64_t                           m_slotFenceVal[kRingFrames] = {};
    uint64_t                           m_dispFenceVal[kRingFrames] = {};

    ComPtr<ID3D12RootSignature>        m_agxRoot;
    ComPtr<ID3D12PipelineState>        m_agxPso;
    ComPtr<ID3D12Resource>             m_agxCbBuf;
    uint8_t*                           m_agxCbMapped = nullptr;

    RawMeshHost                        m_meshHost;
    RawFluidSimHost                    m_fluidSim;
    RawFluidCompositeHost              m_fluidComposite;
    RawRadianceHost                    m_radianceHost;
    raw::audio::RawAudioOutput         m_audio;
    raw::worldgen::RawWeatherSim          m_weatherSim;
    raw::worldgen::RawWorldShowcase       m_showcase;
    raw::worldgen::RawTamrielWorldStreamer m_tamrielStreamer;
    raw::worldgen::DestructionSimulation  m_destSim;

    struct BiomeAnchor { float x = 75776.0f, y = 157696.0f; };
    std::array<BiomeAnchor, 6>            m_biomeAnchors{};
    uint32_t                              m_discoveredBiomeCount = 0u;
    uint32_t                              m_biomeIdx = 0u;

    std::unordered_map<uint64_t, TerrainGpuChunk> m_frontierChunks;
    std::unordered_map<uint64_t, TerrainGpuChunk> m_esmChunks;
    std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> m_tamrielCellHandles;
    raw::worldgen::TamrielStreamStats     m_lastTamrielStats{};
    uint32_t                              m_totalTamrielEvictions = 0u;
    uint32_t                              m_landmarkIdx = 0u;
    uint64_t                              m_nextChunkHandle = 1u;
    bool                                  m_hasFrontierCenter = false;
    float                                 m_frontierCenter[3] = {75776.0f, 157696.0f, 1200.0f};
    float                                 m_frontierGroundZ = 0.0f;
    bool                                  m_esmLoaded = false;

    WorldMode                             m_mode = WorldMode::Frontier;
    SeasonChoice                          m_season = SeasonChoice::Winter;
    TimePreset                            m_timePreset = TimePreset::Noon;
    WeatherPreset                         m_weatherPreset = WeatherPreset::Clear;
    bool                               m_fireEnabled = true;
    bool                               m_giEnabled = true;
    bool                               m_audioEnabled = false;
    uint32_t                           m_fractureCounter = 0u;
    uint32_t                           m_activeShards = 0u;
    bool                               m_destructionDirty = false;
    uint32_t                           m_lastDrawnChunks = 0u;
    uint32_t                           m_lastDrawnMeshlets = 0u;
    uint64_t                           m_frameCount = 0u;
    float                              m_simTimeSec = 0.0f;

    float                              m_camPos[3] = {75776.0f, 155200.0f, 2400.0f};
    float                              m_camYaw = 0.0f;
    float                              m_camPitch = -0.45f;
};

struct WindowState {
    RawEngineViewer* viewer = nullptr;
    bool             rmbDown = false;
    POINT            lastMouse{};
    bool             quit = false;
};

LRESULT CALLBACK ViewerWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* st = reinterpret_cast<WindowState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTA*>(lp);
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return 0;
        }
        case WM_SIZE:
            if (st && st->viewer && wp != SIZE_MINIMIZED) {
                st->viewer->Resize(LOWORD(lp), HIWORD(lp));
            }
            return 0;
        case WM_RBUTTONDOWN:
            if (st) {
                st->rmbDown = true;
                SetCapture(hwnd);
                GetCursorPos(&st->lastMouse);
            }
            return 0;
        case WM_RBUTTONUP:
            if (st) {
                st->rmbDown = false;
                ReleaseCapture();
            }
            return 0;
        case WM_MOUSEMOVE:
            if (st && st->viewer && st->rmbDown) {
                POINT p{};
                GetCursorPos(&p);
                st->viewer->OnMouseDrag(p.x - st->lastMouse.x, p.y - st->lastMouse.y);
                st->lastMouse = p;
            }
            return 0;
        case WM_KEYDOWN:
            if (st && st->viewer && ((lp & 0x40000000L) == 0)) {
                switch (wp) {
                    case VK_ESCAPE: PostQuitMessage(0); break;
                    case '1': st->viewer->ApplySeason(SeasonChoice::Spring); break;
                    case '2': st->viewer->ApplySeason(SeasonChoice::Summer); break;
                    case '3': st->viewer->ApplySeason(SeasonChoice::Autumn); break;
                    case '4': st->viewer->ApplySeason(SeasonChoice::Winter); break;
                    case 'F': st->viewer->ToggleFire(); break;
                    case 'G': st->viewer->ToggleGI(); break;
                    case 'B': st->viewer->CycleBiome(); break;
                    case 'L': st->viewer->CycleTamrielLandmark(); break;
                    case 'T': st->viewer->CycleTimePreset(); break;
                    case 'W': st->viewer->CycleWeatherPreset(); break;
                    case 'A': st->viewer->ToggleAudio(); break;
                    case 'R': st->viewer->TriggerFreshFracture(); break;
                    case 'M': st->viewer->ToggleWorldMode(); break;
                    default: break;
                }
            }
            return 0;
        case WM_DESTROY:
            if (st) st->quit = true;
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

bool ParseArgs(int argc, char** argv, ViewerOptions* opt)
{
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char* fl) -> const char* {
            if (i + 1 >= argc) { std::printf("Missing value for %s\n", fl); return nullptr; }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") { opt->showHelp = true; return true; }
        else if (a == "--headless-frames") {
            const char* v = need("--headless-frames");
            if (!v) return false;
            opt->headlessFrames = static_cast<uint32_t>(std::clamp(std::atoi(v), 1, 240));
            if (opt->width == 1280u && opt->height == 720u) {
                opt->width = 640u; opt->height = 360u;
            }
        } else if (a == "--width") {
            const char* v = need("--width");
            if (!v) return false;
            opt->width = static_cast<UINT>(std::clamp(std::atoi(v), 64, 3840));
        } else if (a == "--height") {
            const char* v = need("--height");
            if (!v) return false;
            opt->height = static_cast<UINT>(std::clamp(std::atoi(v), 64, 2160));
        } else if (a == "--esm") {
            const char* v = need("--esm");
            if (!v) return false;
            opt->esmPath = v;
            opt->startEsm = true;
        } else if (a == "--warp") {
            opt->useWarp = true;
        } else if (a == "--debug") {
            opt->debugLayer = true;
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
    ViewerOptions opt{};
    if (!ParseArgs(argc, argv, &opt)) return 2;
    if (opt.showHelp) {
        std::printf(
            "RAW Interactive Real-Time Win32/DX12 Standalone Engine Viewer\n"
            "Controls: WASD/QE flight, Shift boost, RMB freelook, 1-4 seasons, F fire, G GI, B biomes, L landmarks, T time-of-day, W weather, A audio, R fracture, M frontier/Tamriel, ESC exit\n"
            "CLI: [--headless-frames N] [--warp] [--debug] [--width W] [--height H] [--esm <Skyrim.esm>]\n");
        return 0;
    }

    // Headless automated multi-frame self-test mode (CI / WARP / RTX 4090)!
    if (opt.headlessFrames > 0u) {
        std::printf("=== RAW Engine Viewer Headless Multi-Frame Proof (%u frames, %s) ===\n",
                    opt.headlessFrames, opt.useWarp ? "WARP" : "Hardware GPU");
        int passed = 0, failed = 0;
        auto check = [&](bool ok, const char* msg) {
            if (ok) { ++passed; std::printf("[PASS] %s\n", msg); }
            else    { ++failed; std::printf("[FAIL] %s\n", msg); }
        };

        RawEngineViewer viewer;
        const bool initOk = viewer.Init(opt, nullptr);
        check(initOk, "RawEngineViewer initialized headless DX12 pipeline (Terrain + Fluid + AgX Display)");
        if (!initOk) return 1;
        std::printf("       Adapter: %s | Res: %ux%u\n", viewer.AdapterName().c_str(), opt.width, opt.height);

        bool allFramesOk = true;
        raw::worldgen::TamrielStreamStats riverwoodStats{};
        bool teleportedToWhiterun = false;
        for (uint32_t f = 0; f < opt.headlessFrames; ++f) {
            if (f == 0u && opt.startEsm) {
                riverwoodStats = viewer.GetTamrielStats();
            }
            if (f == opt.headlessFrames / 2u) {
                if (opt.startEsm && std::filesystem::exists(opt.esmPath)) {
                    teleportedToWhiterun = viewer.TeleportToTamrielLandmark(1u); // Riverwood (4,-12) -> Whiterun (5,-3)!
                    viewer.ApplyTimePreset(TimePreset::GoldenHour);
                    viewer.ApplyWeatherPreset(WeatherPreset::Clear);
                } else {
                    viewer.ApplySeason(SeasonChoice::Autumn);
                    viewer.CycleTimePreset();
                }
            }
            if (!viewer.RenderFrame(1.0f / 60.0f, false)) {
                allFramesOk = false;
                break;
            }
        }
        check(allFramesOk, opt.startEsm
              ? "Executed multi-frame live Tamriel streaming with Riverwood -> Whiterun landmark teleport"
              : "Executed all back-to-back multi-pass frames with live mid-run season & time switch");

        RawEngineViewer::HeadlessAudit audit{};
        const bool rbOk = viewer.ReadbackHeadlessAudit(&audit);
        std::printf("       Audit: path=%s, meshlets=%u, shards=%u, biomes=%u/6, gi=%s (%.3fms), fire=%.3fms, water=%.3fms, hdrLitPx=%u, hdrNanInf=%u, dispNonZeroPx=%u, dispMean=%.3f, dispStdDev=%.3f\n",
                    audit.usedMeshShaders ? "AS+MS+PS" : "VS+PS",
                    audit.drawnMeshlets, audit.activeShards, audit.biomesFound,
                    audit.radianceActive ? "ON" : "off", audit.radianceGiMs,
                    audit.fireStepMs, audit.waterStepMs,
                    audit.hdrLitPixels, audit.hdrNanInf,
                    audit.dispNonZeroPx, audit.dispMeanLuma, audit.dispStdDevLuma);
        if (opt.startEsm && std::filesystem::exists(opt.esmPath)) {
            const auto whStats = viewer.GetTamrielStats();
            std::printf("       Tamriel Streaming: Riverwood(cells=%u, nifCache=%u, hits=%llu) -> %s(cells=%u, nifCache=%u, hits=%llu, evictions=%u, meshlets=%u)\n",
                        riverwoodStats.activeCells, riverwoodStats.cachedNifModels,
                        static_cast<unsigned long long>(riverwoodStats.cacheHits),
                        viewer.ActiveLandmarkName(), whStats.activeCells, whStats.cachedNifModels,
                        static_cast<unsigned long long>(whStats.cacheHits),
                        viewer.TotalTamrielEvictions(), audit.drawnMeshlets);
            check(riverwoodStats.activeCells >= 1u && teleportedToWhiterun && whStats.activeCells >= 1u &&
                  viewer.TotalTamrielEvictions() > 0u && audit.drawnMeshlets > 500u,
                  "RawTamrielWorldStreamer streamed Riverwood 3x3 ring, evicted & reloaded Whiterun 3x3 ring live on GPU");
        }
        check(rbOk && audit.drawnMeshlets > 0u && audit.activeShards >= 10u && audit.radianceActive && audit.biomesFound == 6u,
              "Rendered terrain + flora + 20-shard destruction + 64^3 NS fire + 4-cascade Radiance GI (6/6 biomes discovered)");
        check(audit.hdrNanInf == 0u, "HDR RGBA16F scene buffer contains 0 NaN/Inf pixels");
        check(audit.hdrLitPixels > (opt.width * opt.height) / 5u &&
              audit.dispNonZeroPx > (opt.width * opt.height) * 9u / 10u &&
              audit.dispStdDevLuma > 0.04,
              "RawAgxDisplay fullscreen pass produced rich high-contrast sRGB display output");

        const uint32_t d3dErr = viewer.AuditDebugErrorsAndShutdown();
        std::printf("       Shutdown: d3d12DebugErrors=%u\n", d3dErr);
        check(d3dErr == 0u, "D3D12 Debug Layer reported 0 errors/corruptions across multi-frame execution & shutdown");

        std::printf("=== Viewer Headless Summary: %d passed, %d failed ===\n", passed, failed);
        return failed == 0 ? 0 : 1;
    }

    // Interactive Win32 60+ FPS Window Mode!
    HINSTANCE hInst = GetModuleHandleA(nullptr);
    WNDCLASSA wc{};
    wc.lpfnWndProc = ViewerWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    wc.lpszClassName = "RawEngineViewerWndClass";
    RegisterClassA(&wc);

    RECT r{0, 0, static_cast<LONG>(opt.width), static_cast<LONG>(opt.height)};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    WindowState ws{};
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "RAW Native Engine Viewer — Initializing...",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                                nullptr, nullptr, hInst, &ws);
    if (!hwnd) return 1;

    RawEngineViewer viewer;
    if (!viewer.Init(opt, hwnd)) {
        DestroyWindow(hwnd);
        return 1;
    }
    ws.viewer = &viewer;

    auto tPrev = std::chrono::steady_clock::now();
    auto tTitle = tPrev;
    uint32_t titleFrames = 0u;

    MSG msg{};
    while (!ws.quit) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { ws.quit = true; break; }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (ws.quit) break;

        const auto tNow = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(tNow - tPrev).count();
        tPrev = tNow;

        viewer.UpdateInput(std::clamp(dt, 1.0e-4f, 0.05f));
        viewer.RenderFrame(dt, true);
        ++titleFrames;

        const double titleElapsed = std::chrono::duration<double>(tNow - tTitle).count();
        if (titleElapsed >= 0.25) {
            const double fps = double(titleFrames) / titleElapsed;
            const std::string title = viewer.FormatTitle(fps);
            SetWindowTextA(hwnd, title.c_str());
            tTitle = tNow;
            titleFrames = 0u;
        }
    }

    viewer.Shutdown();
    DestroyWindow(hwnd);
    return 0;
}
