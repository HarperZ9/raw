// raw_engine_viewer.cpp: RAW Interactive Real-Time 60+ FPS Win32 / DX12 Standalone Engine Viewer.
// Double-click to fly freely through BOTH our 6-biome procedural frontier world (RawWorldShowcase +
// RawHydrologyGen + RawFloraEngine + RawDestructionEngine + RawWeatherSim + 64^3 3D Navier-Stokes Fire)
// AND Real Riverwood (Skyrim.esm + Skyrim - Meshes0.bsa .NIF buildings) completely WITHOUT SkyrimSE.exe!
//
// Controls (Interactive Mode):
//   WASD / Q / E        : 6-DOF Flight Camera (Walk Mode: WASD move, Space jump, Shift sprint)
//   Right-Mouse Drag    : Smooth Freelook Yaw / Pitch
//   P                   : Toggle Playable Grounded Walk Mode <-> Free-Fly Camera (RawWorldPhysics!)
//   C / Left-Click      : Fire Real Physics Raycast Against .NIF Buildings & Terrain (+ Destruction Burst!)
//   N                   : Compute Live 3D NavMesh Octile A* Funnel-Smoothed Path (RawNavMeshAi!)
//   I                   : Ignite Live Directional Wildfire / Snowmelt / Black-Ice Field (RawWorldEcosystem!)
//   V                   : Emit Loud Acoustic Shockwave Startling Wildlife Boids Skyward (RawWildlifeBoids!)
//   E                   : Advance 9-Hold Economy +6h & Cycle Active Hold Market Readout (RawSettlementSim!)
//   O                   : Resolve Highest-Urgency Emergent World Event or Poll +4h (RawWorldEventDirector!)
//   K                   : Cycle 9-Hold & 6-Faction Reputation / Bounty / Guard Posture Readout (RawFactionDiplomacy!)
//   D                   : Cycle Procedural 3D Dungeon Explorer (Nordic Barrow -> Ice Cavern -> Dwemer Ruin -> Overworld)
//   X                   : Toggle/Cycle Live Combat Skirmish Arena (220ms Timed-Parry, Melee, Shout, Arrows!)
//   Q                   : Radiant Quest/Dialogue Journal (Generate, Persuade/Bribe/Intimidate & Advance!)
//   F5 / F8             : CRC-32 Persistent World Quicksave (.bin + .json) / Quickload (RawWorldSerializer!)
//   F9 / H              : Toggle Live GPU Telemetry HUD & 64-Sample Frame-Time Sparkline (RawOverlayHost!)
//   F12                 : Capture Instant 24-Bit .bmp Screenshot (raw_viewer_shot_<N>.bmp)
//   1 / 2 / 3 / 4       : Live Season Switch (Spring / Summer / Autumn / Winter)
//   F                   : Cycle Live Anti-Exploit Forge/Alchemy/Enchanting (RawCraftingInventoryEngine!)
//   U                   : Toggle Live 64^3 3D Navier-Stokes Fire + Smoke
//   G                   : Toggle Live 4-Cascade Radiance Cascades Screen GI
//   B                   : Cycle/Teleport Across All 6 Procedural Frontier Biomes
//   L                   : Teleport Across 4 Iconic Tamriel Landmarks (Riverwood / Whiterun / High Hrothgar / Solitude)
//   T                   : Cycle Time-of-Day Presets (Dawn 6h / Noon 13h / Golden Hour 18.5h / Midnight Aurora 23.5h)
//   W                   : Cycle Weather Presets (Frontier: Clear/Overcast/Rain/Blizzard | Tamriel: Real Skyrim.esm WTHR!)
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
#include "RawXpbdSimHost.h"
#include "RawOverlayHost.h"
#include "RawGpuCullHost.h"
#include "RawLightClusterHost.h"
#include "RawVolumetricFroxelHost.h"
#include "RawShadowPcssHost.h"
#include "RawGtaoHost.h"
#include "RawSsrHost.h"
#include "RawAutoExposureHost.h"
#include "RawCloudRaymarchHost.h"
#include "RawPostCinematicHost.h"
#include "RawGpuParticlesHost.h"
#include "RawTsrUpscaleHost.h"
#include "RawWaterCausticsHost.h"
#include "RawWeatherSim.h"
#include "RawWorldShowcase.h"
#include "RawDestructionEngine.h"
#include "RawEsmWorldLoader.h"
#include "RawEsmModelResolver.h"
#include "RawTamrielWorldStreamer.h"
#include "RawEsmNpcWeatherLoader.h"
#include "RawWorldPhysics.h"
#include "RawNavMeshAi.h"
#include "RawWorldEcosystem.h"
#include "RawWildlifeBoids.h"
#include "RawSettlementSim.h"
#include "RawWorldEventDirector.h"
#include "RawFactionDiplomacy.h"
#include "RawWorldSerializer.h"
#include "RawDungeonGen.h"
#include "RawSkeletonAnimEngine.h"
#include "RawCombatSandboxEngine.h"
#include "RawQuestStoryEngine.h"
#include "RawCraftingInventoryEngine.h"
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

enum class WorldMode { Frontier, RiverwoodEsm, Dungeon };
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
        err.clear();
        m_xpbdOk = m_xpbdSim.Init(m_dev.Get(), {}, &err);
        if (!m_xpbdOk) {
            std::printf("[Viewer] RawXpbdSimHost warning: %s\n", err.c_str());
        }
        err.clear();
        m_overlayOk = m_overlayHost.Init(m_dev.Get(), &err);
        if (!m_overlayOk) {
            std::printf("[Viewer] RawOverlayHost warning: %s\n", err.c_str());
        }
        err.clear();
        GpuCullDesc cdesc{};
        cdesc.maxInstances     = kMaxCullInstances;
        cdesc.initialHiZWidth  = m_width;
        cdesc.initialHiZHeight = m_height;
        m_gpuCullOk = m_gpuCull.Init(m_dev.Get(), cdesc, &err) && CreateGpuCullInstanceBuffers();
        if (!m_gpuCullOk) {
            std::printf("[Viewer] RawGpuCullHost warning: %s\n", err.c_str());
        }
        err.clear();
        m_lightClustersOk = m_lightClusters.Init(m_dev.Get(), {}, &err);
        if (!m_lightClustersOk) {
            std::printf("[Viewer] RawLightClusterHost warning: %s\n", err.c_str());
        }
        err.clear();
        m_volFroxelsOk = m_volFroxels.Init(m_dev.Get(), {}, &err);
        if (!m_volFroxelsOk) {
            std::printf("[Viewer] RawVolumetricFroxelHost warning: %s\n", err.c_str());
        }
        err.clear();
        m_shadowPcssOk = m_shadowPcss.Init(m_dev.Get(), {}, &err) && CreateShadowMapProxy();
        if (!m_shadowPcssOk) {
            std::printf("[Viewer] RawShadowPcssHost warning: %s\n", err.c_str());
        }
        err.clear();
        GtaoDesc gtd{};
        gtd.initialWidth  = std::max(1u, m_width / 2u);
        gtd.initialHeight = std::max(1u, m_height / 2u);
        m_gtaoOk = m_gtao.Init(m_dev.Get(), gtd, &err);
        if (!m_gtaoOk) {
            std::printf("[Viewer] RawGtaoHost warning: %s\n", err.c_str());
        }
        err.clear();
        SsrDesc ssrd{};
        ssrd.initialWidth  = std::max(1u, m_width / 2u);
        ssrd.initialHeight = std::max(1u, m_height / 2u);
        m_ssrOk = m_ssr.Init(m_dev.Get(), ssrd, &err);
        if (!m_ssrOk) {
            std::printf("[Viewer] RawSsrHost warning: %s\n", err.c_str());
        }
        err.clear();
        m_autoExposureOk = m_autoExposure.Init(m_dev.Get(), {}, &err);
        if (!m_autoExposureOk) {
            std::printf("[Viewer] RawAutoExposureHost warning: %s\n", err.c_str());
        }
        err.clear();
        CloudRaymarchDesc crd{};
        crd.initialWidth  = std::max(1u, m_width / 2u);
        crd.initialHeight = std::max(1u, m_height / 2u);
        m_cloudRaymarchOk = m_cloudRaymarch.Init(m_dev.Get(), crd, &err);
        if (!m_cloudRaymarchOk) {
            std::printf("[Viewer] RawCloudRaymarchHost warning: %s\n", err.c_str());
        }
        err.clear();
        PostCinematicDesc pcd{};
        pcd.initialWidth  = std::max(1u, m_width / 2u);
        pcd.initialHeight = std::max(1u, m_height / 2u);
        m_postCinematicOk = m_postCinematic.Init(m_dev.Get(), pcd, &err);
        if (!m_postCinematicOk) {
            std::printf("[Viewer] RawPostCinematicHost warning: %s\n", err.c_str());
        }
        err.clear();
        GpuParticlesDesc gpdesc{};
        gpdesc.particleCount = RawGpuParticlesHost::kMaxParticles;
        gpdesc.splatWidth    = 480u;
        gpdesc.splatHeight   = 270u;
        m_gpuParticlesOk = m_gpuParticles.Init(m_dev.Get(), gpdesc, &err);
        if (!m_gpuParticlesOk) {
            std::printf("[Viewer] RawGpuParticlesHost warning: %s\n", err.c_str());
        }
        err.clear();
        TsrUpscaleDesc tsrD{};
        tsrD.initialInputWidth   = m_width;
        tsrD.initialInputHeight  = m_height;
        tsrD.initialOutputWidth  = std::min(m_width * 2u, 1920u);
        tsrD.initialOutputHeight = std::min(m_height * 2u, 1080u);
        m_tsrUpscaleOk = m_tsrUpscale.Init(m_dev.Get(), tsrD, &err);
        if (!m_tsrUpscaleOk) {
            std::printf("[Viewer] RawTsrUpscaleHost warning: %s\n", err.c_str());
        }
        err.clear();
        WaterCausticsDesc wcd{};
        wcd.initialWidth  = 512u;
        wcd.initialHeight = 512u;
        m_waterCausticsOk = m_waterCaustics.Init(m_dev.Get(), wcd, &err);
        if (!m_waterCausticsOk) {
            std::printf("[Viewer] RawWaterCausticsHost warning: %s\n", err.c_str());
        }
        UINT64 tsFreq = 0;
        if (SUCCEEDED(m_queue->GetTimestampFrequency(&tsFreq)) && tsFreq > 0) {
            if (m_fluidOk)         m_fluidSim.SetTimestampFrequency(tsFreq);
            if (m_radOk)           m_radianceHost.SetTimestampFrequency(tsFreq);
            if (m_xpbdOk)          m_xpbdSim.SetTimestampFrequency(tsFreq);
            if (m_overlayOk)       m_overlayHost.SetTimestampFrequency(tsFreq);
            if (m_gpuCullOk)       m_gpuCull.SetTimestampFrequency(tsFreq);
            if (m_lightClustersOk) m_lightClusters.SetTimestampFrequency(tsFreq);
            if (m_volFroxelsOk)    m_volFroxels.SetTimestampFrequency(tsFreq);
            if (m_shadowPcssOk)    m_shadowPcss.SetTimestampFrequency(tsFreq);
            if (m_gtaoOk)          m_gtao.SetTimestampFrequency(tsFreq);
            if (m_ssrOk)           m_ssr.SetTimestampFrequency(tsFreq);
            if (m_autoExposureOk)  m_autoExposure.SetTimestampFrequency(tsFreq);
            if (m_cloudRaymarchOk) m_cloudRaymarch.SetTimestampFrequency(tsFreq);
            if (m_postCinematicOk) m_postCinematic.SetTimestampFrequency(tsFreq);
            if (m_gpuParticlesOk)  m_gpuParticles.SetTimestampFrequency(tsFreq);
            if (m_tsrUpscaleOk)    m_tsrUpscale.SetTimestampFrequency(tsFreq);
            if (m_waterCausticsOk) m_waterCaustics.SetTimestampFrequency(tsFreq);
        }
        InitStarterCraftingLoadout();
        m_settlementSim.Reset(202653u);
        m_settlementTelemetry   = m_settlementSim.ComputeWorldTelemetry();
        m_initialSettlementGold = m_settlementTelemetry.totalGold;
        m_eventDirector.Reset();
        m_eventTelemetry        = m_eventDirector.ComputeTelemetry();
        m_diplomacy.Reset();
        m_diplomacyTelemetry    = m_diplomacy.ComputeTelemetry();

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
        m_displayLdr.Reset();
        m_hdrColor.Reset();
        m_depthIn.Reset();
        m_shadowOutTex.Reset();
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
        if (m_mode == WorldMode::RiverwoodEsm && !m_esmWeatherCycle.empty()) {
            m_esmWeatherIdx = (m_esmWeatherIdx + 1u) % static_cast<uint32_t>(m_esmWeatherCycle.size());
            ApplyActiveEsmWeather();
            return;
        }
        ApplyWeatherPreset(static_cast<WeatherPreset>((static_cast<uint8_t>(m_weatherPreset) + 1u) % 4u));
    }

    void ToggleHud() { m_hudEnabled = !m_hudEnabled; }

    const char* ActiveWeatherDisplayName() const
    {
        if (m_mode == WorldMode::RiverwoodEsm && !m_activeEsmWeatherEdid.empty()) {
            return m_activeEsmWeatherEdid.c_str();
        }
        return WeatherPresetName(m_weatherPreset);
    }

    size_t LoadedEsmWeatherCount() const { return m_esmWorldMeta.weathers.size(); }

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
        if (m_esmLoaded) {
            m_navMeshDirty = true;
            m_hasPrevFroxelView = false;
            m_volFroxels.ResetHistory();
            m_tsrUpscale.ResetHistory();
            BakeLocalNavMesh(float(lm.cellX) * raw::worldgen::kEsmCellSizeU,
                             float(lm.cellY) * raw::worldgen::kEsmCellSizeU,
                             true);
            float ecoGroundZ = 0.0f;
            float ecoWaterZ  = -14000.0f;
            if (const auto* cell = m_tamrielStreamer.FindCell(lm.cellX, lm.cellY)) {
                m_ecosystem.InitFromTerrainChunk(cell->terrainChunk, 70.0f, cell->waterHeightZ);
                m_ecoInitialized = true;
                m_ecoTelemetry   = m_ecosystem.ComputeTelemetry();
                ecoGroundZ       = 0.5f * (cell->terrainMesh.aabbMin.z + cell->terrainMesh.aabbMax.z);
                ecoWaterZ        = cell->waterHeightZ;
            }
            SpawnWildlifeAround(midX, midY, ecoGroundZ, ecoWaterZ);
        }
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

    void ToggleWalkMode()
    {
        m_walkMode = !m_walkMode;
        if (m_walkMode) {
            auto& phys = ActivePhysics();
            float groundZ = m_camPos[2] - 120.0f;
            const auto downHit = phys.RayCast({m_camPos[0], m_camPos[1], m_camPos[2] + 64.0f},
                                              {0.0f, 0.0f, -1.0f}, 6000.0f);
            if (downHit.hit) {
                groundZ = downHit.point[2];
            } else {
                float hz = 0.0f;
                if (phys.SampleTerrainHeight(m_camPos[0], m_camPos[1], &hz)) {
                    groundZ = hz;
                }
            }
            m_playerBody = raw::worldgen::CharacterBody{};
            m_playerBody.position = {m_camPos[0], m_camPos[1], groundZ + 6.0f};
            m_playerBody.velocity = {0.0f, 0.0f, 0.0f};
            m_camPos[2] = m_playerBody.position.z + 120.0f;
        }
    }

    void FireRaycastStrike()
    {
        const float cp = std::cos(m_camPitch), sp = std::sin(m_camPitch);
        const float cy = std::cos(m_camYaw),   sy = std::sin(m_camYaw);
        const raw::worldgen::Vec3 origin{m_camPos[0], m_camPos[1], m_camPos[2]};
        const raw::worldgen::Vec3 dir{sy * cp, cy * cp, sp};
        const auto hit = ActivePhysics().RayCast(origin, dir, 8000.0f);
        m_lastRayHit = hit;
        if (hit.hit) {
            std::printf("[Physics] RayCast HIT: dist=%.1fu mat=%u (%s) at (%.0f, %.0f, %.0f)\n",
                        hit.distance, hit.materialTag, hit.hitTerrain ? "terrain" : "NIF/BVH",
                        hit.point[0], hit.point[1], hit.point[2]);
            TriggerFractureAt(hit.point[0], hit.point[1], hit.point[2]);
        } else {
            std::printf("[Physics] RayCast MISS (8000u)\n");
        }
    }

    struct PhysicsWalkProbe {
        bool  rayHit          = false;
        float rayDist         = 0.0f;
        bool  walkGrounded    = false;
        bool  walkAllFinite   = true;
        float finalFeetZ      = 0.0f;
        uint32_t hfCount      = 0u;
        uint32_t bvhTriangles = 0u;
    };

    PhysicsWalkProbe RunPhysicsWalkSelfTest()
    {
        PhysicsWalkProbe p{};
        const auto st = m_worldPhysics.GetStats();
        p.hfCount = st.heightfieldCount;
        p.bvhTriangles = st.totalTriangles;

        const auto& lm = kTamrielLandmarks[m_landmarkIdx % 4u];
        const float testX = float(lm.cellX) * raw::worldgen::kEsmCellSizeU + raw::worldgen::kEsmCellSizeU * 0.5f;
        const float testY = float(lm.cellY) * raw::worldgen::kEsmCellSizeU + raw::worldgen::kEsmCellSizeU * 0.5f;
        const auto downHit = m_worldPhysics.RayCast({testX, testY, 8000.0f}, {0.0f, 0.0f, -1.0f}, 20000.0f);
        p.rayHit = downHit.hit;
        p.rayDist = downHit.distance;

        raw::worldgen::CharacterBody body{};
        const float startZ = downHit.hit ? (downHit.point[2] + 32.0f) : 200.0f;
        body.position = {testX, testY, startZ};
        raw::worldgen::CharacterInput cin{};
        cin.wishDirXY = {0.707f, 0.707f};
        cin.maxSpeed = 280.0f;
        for (int s = 0; s < 24; ++s) {
            m_worldPhysics.StepCharacterCapsule(body, cin, 1.0f / 60.0f);
            if (!std::isfinite(body.position.x) || !std::isfinite(body.position.y) || !std::isfinite(body.position.z) ||
                !std::isfinite(body.velocity.x) || !std::isfinite(body.velocity.y) || !std::isfinite(body.velocity.z)) {
                p.walkAllFinite = false;
            }
        }
        p.walkGrounded = body.grounded;
        p.finalFeetZ = body.position.z;
        return p;
    }

    bool BakeLocalNavMesh(float originOrCenterX, float originOrCenterY, bool isOriginCorner = false)
    {
        if (m_worldPhysics.GetStats().heightfieldCount == 0u) return false;
        raw::worldgen::NavBakeConfig cfg{};
        cfg.sizeX     = 4096.0f;
        cfg.sizeY     = 4096.0f;
        cfg.cellSizeU = 64.0f;
        cfg.originX   = isOriginCorner ? originOrCenterX : (originOrCenterX - cfg.sizeX * 0.5f);
        cfg.originY   = isOriginCorner ? originOrCenterY : (originOrCenterY - cfg.sizeY * 0.5f);
        if (!m_navMesh.BakeFromPhysics(m_worldPhysics, cfg)) return false;
        m_navMeshDirty = false;
        ++m_navMeshBakeCount;
        InitTacticalSquadAround(cfg.originX + cfg.sizeX * 0.5f, cfg.originY + cfg.sizeY * 0.5f);
        return true;
    }

    void ComputeLiveNavPath()
    {
        if (m_mode == WorldMode::Dungeon) {
            if (!m_navMesh.IsBaked()) return;
            const raw::worldgen::Vec3 startW{m_camPos[0], m_camPos[1], m_camPos[2] - 120.0f};
            const raw::worldgen::Vec3 goalW{m_activeDungeon.bossPos[0], m_activeDungeon.bossPos[1], m_activeDungeon.bossPos[2]};
            auto res = m_navMesh.FindPath(startW, goalW, &m_dungeonPhysics, 32768u);
            if (!res.found) {
                const raw::worldgen::Vec3 entW{m_activeDungeon.entrancePos[0], m_activeDungeon.entrancePos[1], m_activeDungeon.entrancePos[2]};
                res = m_navMesh.FindPath(entW, goalW, &m_dungeonPhysics, 32768u);
            }
            m_lastNavPath = res;
            const auto st = m_navMesh.GetStats();
            std::printf("[Dungeon NavMesh] Entrance->Boss A* Path: found=%d waypoints=%zu (raw=%u) len=%.1fu search=%.3fms | walkable=%u\n",
                        int(res.found), res.waypoints.size(), res.rawGridNodes, res.totalLengthU, res.searchTimeMs, st.walkableCells);
            return;
        }
        if (!m_navMesh.IsBaked()) {
            if (m_mode == WorldMode::RiverwoodEsm) {
                const auto& lm = kTamrielLandmarks[m_landmarkIdx % 4u];
                BakeLocalNavMesh(float(lm.cellX) * raw::worldgen::kEsmCellSizeU,
                                 float(lm.cellY) * raw::worldgen::kEsmCellSizeU, true);
            } else {
                BakeLocalNavMesh(m_camPos[0], m_camPos[1], false);
            }
        }
        if (!m_navMesh.IsBaked()) return;

        const auto& cfg = m_navMesh.Config();
        const int32_t gw = static_cast<int32_t>(m_navMesh.GetStats().gridW);
        const int32_t gh = static_cast<int32_t>(m_navMesh.GetStats().gridH);
        const float cy = std::cos(m_camYaw), sy = std::sin(m_camYaw);

        // Pick walkable start near camera (fallback to SW quadrant of baked 64x64 tile)
        raw::worldgen::Vec3 startW{m_camPos[0], m_camPos[1], m_camPos[2]};
        int32_t sgx = 0, sgy = 0;
        bool hasStart = m_navMesh.FindNearestWalkableCell(startW, 12, &sgx, &sgy);
        if (hasStart) {
            startW = m_navMesh.GridToWorldCenter(sgx, sgy);
        } else {
            for (int32_t gy = 4; gy < gh / 2 && !hasStart; ++gy) {
                for (int32_t gx = 4; gx < gw / 2 && !hasStart; ++gx) {
                    const auto* c = m_navMesh.GetCell(gx, gy);
                    if (c && c->tag == raw::worldgen::NavCellTag::Walkable && c->passableNeighborMask != 0u) {
                        startW = m_navMesh.GridToWorldCenter(gx, gy);
                        hasStart = true;
                    }
                }
            }
        }

        // Pick a well-separated walkable goal ahead of camera (fallback to opposite NE quadrant)
        raw::worldgen::Vec3 goalW{
            std::clamp(startW.x + sy * 1800.0f, cfg.originX + 192.0f, cfg.originX + cfg.sizeX - 192.0f),
            std::clamp(startW.y + cy * 1800.0f, cfg.originY + 192.0f, cfg.originY + cfg.sizeY - 192.0f),
            startW.z
        };
        int32_t ggx = 0, ggy = 0;
        raw::worldgen::NavPathResult res{};
        if (hasStart && m_navMesh.FindNearestWalkableCell(goalW, 14, &ggx, &ggy)) {
            const raw::worldgen::Vec3 cand = m_navMesh.GridToWorldCenter(ggx, ggy);
            res = m_navMesh.FindPath(startW, cand, &m_worldPhysics);
        }
        if (hasStart && (!res.found || res.waypoints.size() < 2u || res.totalLengthU < 300.0f)) {
            for (int32_t gy = gh - 6; gy >= gh / 2 && !res.found; --gy) {
                for (int32_t gx = gw - 6; gx >= gw / 2 && !res.found; --gx) {
                    const auto* c = m_navMesh.GetCell(gx, gy);
                    if (c && c->tag == raw::worldgen::NavCellTag::Walkable && c->passableNeighborMask != 0u) {
                        const raw::worldgen::Vec3 cand = m_navMesh.GridToWorldCenter(gx, gy);
                        const auto probe = m_navMesh.FindPath(startW, cand, &m_worldPhysics);
                        if (probe.found && probe.waypoints.size() >= 2u && probe.totalLengthU > 600.0f) {
                            res = probe;
                        }
                    }
                }
            }
        }
        m_lastNavPath = res;
        const auto st = m_navMesh.GetStats();
        std::printf("[NavMesh] A* Path: found=%d waypoints=%zu (raw=%u) len=%.1fu search=%.3fms | walkable=%u bake=%.2fms\n",
                    int(res.found), res.waypoints.size(), res.rawGridNodes, res.totalLengthU, res.searchTimeMs,
                    st.walkableCells, st.bakeTimeMs);
    }

    struct NavMeshSquadProbe {
        bool     baked          = false;
        uint32_t walkableCells  = 0u;
        double   bakeMs         = 0.0;
        bool     pathFound      = false;
        size_t   pathWaypoints  = 0u;
        float    pathLengthU    = 0.0f;
        double   pathSearchMs   = 0.0;
        bool     squadAllFinite = true;
        size_t   squadSize      = 0u;
    };

    NavMeshSquadProbe RunNavMeshSquadSelfTest()
    {
        NavMeshSquadProbe p{};
        const auto& lm = kTamrielLandmarks[m_landmarkIdx % 4u];
        const float ox = float(lm.cellX) * raw::worldgen::kEsmCellSizeU;
        const float oy = float(lm.cellY) * raw::worldgen::kEsmCellSizeU;
        p.baked = m_navMesh.IsBaked() || BakeLocalNavMesh(ox, oy, true);
        const auto st = m_navMesh.GetStats();
        p.walkableCells = st.walkableCells;
        p.bakeMs        = st.bakeTimeMs;

        // Compute cross-village A* path across Riverwood (4, -12)
        const int32_t gw = static_cast<int32_t>(st.gridW);
        const int32_t gh = static_cast<int32_t>(st.gridH);
        raw::worldgen::Vec3 startW{}, goalW{};
        bool pickedStart = false;
        for (int32_t gy = 6; gy < std::min(26, gh) && !pickedStart; ++gy) {
            for (int32_t gx = 6; gx < std::min(28, gw) && !pickedStart; ++gx) {
                const auto* c = m_navMesh.GetCell(gx, gy);
                if (c && c->tag == raw::worldgen::NavCellTag::Walkable && c->passableNeighborMask != 0u) {
                    startW = m_navMesh.GridToWorldCenter(gx, gy);
                    pickedStart = true;
                }
            }
        }
        raw::worldgen::NavPathResult bestPath{};
        if (pickedStart) {
            for (int32_t gy = std::max(0, gh - 8); gy >= gh / 2 && !bestPath.found; --gy) {
                for (int32_t gx = std::max(0, gw - 8); gx >= gw / 2 && !bestPath.found; --gx) {
                    const auto* c = m_navMesh.GetCell(gx, gy);
                    if (c && c->tag == raw::worldgen::NavCellTag::Walkable && c->passableNeighborMask != 0u) {
                        goalW = m_navMesh.GridToWorldCenter(gx, gy);
                        const auto probe = m_navMesh.FindPath(startW, goalW, &m_worldPhysics);
                        if (probe.found && probe.waypoints.size() >= 2u && probe.totalLengthU > 800.0f) {
                            bestPath = probe;
                        }
                    }
                }
            }
        }
        m_lastNavPath  = bestPath;
        p.pathFound    = bestPath.found;
        p.pathWaypoints = bestPath.waypoints.size();
        p.pathLengthU  = bestPath.totalLengthU;
        p.pathSearchMs = bestPath.searchTimeMs;

        // Run 10 steps of StepSquadTactics on the 4-agent tactical squad with zero NaNs
        if (m_tacticalSquad.size() != 4u) {
            InitTacticalSquadAround(ox + 2048.0f, oy + 2048.0f);
        }
        p.squadSize = m_tacticalSquad.size();
        raw::worldgen::ThreatTarget threat{};
        threat.position = pickedStart ? startW : raw::worldgen::Vec3{ox + 2048.0f, oy + 2048.0f, 0.0f};
        threat.facing   = {0.0f, 1.0f, 0.0f};
        for (int s = 0; s < 10; ++s) {
            const auto out = raw::worldgen::StepSquadTactics(
                m_tacticalSquad, threat, m_worldPhysics, m_navMesh.IsBaked() ? &m_navMesh : nullptr, 1.0f / 60.0f);
            for (const auto& a : m_tacticalSquad) {
                if (!std::isfinite(a.position.x) || !std::isfinite(a.position.y) || !std::isfinite(a.position.z) ||
                    !std::isfinite(a.velocity.x) || !std::isfinite(a.velocity.y) || !std::isfinite(a.velocity.z)) {
                    p.squadAllFinite = false;
                }
            }
            for (const auto& o : out) {
                if (!std::isfinite(o.desiredPosition.x) || !std::isfinite(o.desiredPosition.y) ||
                    !std::isfinite(o.desiredPosition.z) || !std::isfinite(o.desiredSpeedU)) {
                    p.squadAllFinite = false;
                }
            }
        }
        return p;
    }

    bool CaptureScreenshotBmp(std::string* outFilename = nullptr)
    {
        if (!m_ready || !m_displayLdr || m_width == 0u || m_height == 0u) return false;
        WaitGpuIdle();

        const UINT dispPitch = (m_width * 4u + 255u) & ~255u;
        D3D12_HEAP_PROPERTIES rbHp{D3D12_HEAP_TYPE_READBACK};
        D3D12_RESOURCE_DESC rbDesc{};
        rbDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rbDesc.Width            = static_cast<UINT64>(dispPitch) * m_height;
        rbDesc.Height           = 1u;
        rbDesc.DepthOrArraySize = 1u;
        rbDesc.MipLevels        = 1u;
        rbDesc.SampleDesc.Count = 1u;
        rbDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> rbDisp;
        if (FAILED(m_dev->CreateCommittedResource(&rbHp, D3D12_HEAP_FLAG_NONE, &rbDesc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rbDisp)))) {
            return false;
        }

        if (FAILED(m_alloc[0]->Reset()) || FAILED(m_cmd->Reset(m_alloc[0].Get(), nullptr))) return false;
        auto bIn = MakeBarrier(m_displayLdr.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        m_cmd->ResourceBarrier(1u, &bIn);
        D3D12_TEXTURE_COPY_LOCATION dDst{};
        dDst.pResource = rbDisp.Get();
        dDst.Type      = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dDst.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, m_width, m_height, 1u, dispPitch};
        D3D12_TEXTURE_COPY_LOCATION dSrc{};
        dSrc.pResource = m_displayLdr.Get();
        dSrc.Type      = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        m_cmd->CopyTextureRegion(&dDst, 0u, 0u, 0u, &dSrc, nullptr);
        auto bOut = MakeBarrier(m_displayLdr.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        m_cmd->ResourceBarrier(1u, &bOut);
        if (FAILED(m_cmd->Close())) return false;

        ID3D12CommandList* lists[1] = {m_cmd.Get()};
        m_queue->ExecuteCommandLists(1u, lists);
        WaitGpuIdle();

        void* mapped = nullptr;
        if (FAILED(rbDisp->Map(0u, nullptr, &mapped)) || !mapped) return false;

        const uint32_t rowStride = ((m_width * 3u) + 3u) & ~3u;
        const uint32_t imgBytes  = rowStride * m_height;
        const uint32_t fileBytes = 54u + imgBytes;

        uint8_t hdr[54]{};
        hdr[0] = 'B'; hdr[1] = 'M';
        auto putU32 = [&](size_t off, uint32_t v) {
            hdr[off + 0] = static_cast<uint8_t>(v & 0xFFu);
            hdr[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
            hdr[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
            hdr[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
        };
        auto putU16 = [&](size_t off, uint16_t v) {
            hdr[off + 0] = static_cast<uint8_t>(v & 0xFFu);
            hdr[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
        };
        putU32(2u, fileBytes);
        putU32(10u, 54u);
        putU32(14u, 40u);
        putU32(18u, m_width);
        putU32(22u, m_height);
        putU16(26u, 1u);
        putU16(28u, 24u);
        putU32(34u, imgBytes);

        char fname[64]{};
        std::snprintf(fname, sizeof(fname), "raw_viewer_shot_%u.bmp", ++m_screenshotCounter);
        FILE* fp = nullptr;
        bool ok = false;
        if (fopen_s(&fp, fname, "wb") == 0 && fp) {
            ok = (std::fwrite(hdr, 1u, sizeof(hdr), fp) == sizeof(hdr));
            std::vector<uint8_t> rowBuf(rowStride, 0u);
            for (UINT y = 0u; ok && y < m_height; ++y) {
                const UINT srcY = m_height - 1u - y; // BMP bottom-up scanline order
                const uint8_t* srcRow = static_cast<const uint8_t*>(mapped) + static_cast<size_t>(srcY) * dispPitch;
                for (UINT x = 0u; x < m_width; ++x) {
                    rowBuf[x * 3u + 0u] = srcRow[x * 4u + 2u]; // B
                    rowBuf[x * 3u + 1u] = srcRow[x * 4u + 1u]; // G
                    rowBuf[x * 3u + 2u] = srcRow[x * 4u + 0u]; // R
                }
                ok = (std::fwrite(rowBuf.data(), 1u, rowStride, fp) == rowStride);
            }
            std::fclose(fp);
        }
        rbDisp->Unmap(0u, nullptr);

        if (ok) {
            m_lastShotName = fname;
            if (outFilename) *outFilename = fname;
            std::printf("[Viewer] Saved 24-bit screenshot: %s (%ux%u, %u bytes)\n", fname, m_width, m_height, fileBytes);
        }
        return ok;
    }

    static raw::worldgen::EcoWeatherInput BuildEcoWeatherInput(const raw::worldgen::WeatherGpuUniforms& wGpu,
                                                               SeasonChoice season, WeatherPreset weather)
    {
        raw::worldgen::EcoWeatherInput ew{};
        ew.ambientTempC      = wGpu.seasonThermal0.w;
        ew.windDirX          = wGpu.cloudWind.x;
        ew.windDirY          = wGpu.cloudWind.y;
        ew.windBeaufort      = std::clamp(wGpu.beaufortScale, 0.0f, 12.0f);
        ew.rainRate01        = (weather == WeatherPreset::Rainstorm) ? std::max(wGpu.weather.w, 0.75f) : wGpu.weather.w;
        ew.snowRate01        = (season == SeasonChoice::Winter || weather == WeatherPreset::Blizzard)
                             ? std::max(wGpu.weather.z, 0.35f) : wGpu.weather.z;
        ew.solarInsolation01 = std::clamp(0.45f + 0.45f * (1.0f - std::min(wGpu.cloudWind.z, 1.0f)), 0.05f, 1.0f);
        return ew;
    }

    uint32_t IgniteWildfireAtLookHit()
    {
        const float cp = std::cos(m_camPitch), sp = std::sin(m_camPitch);
        const float cy = std::cos(m_camYaw),   sy = std::sin(m_camYaw);
        const raw::worldgen::Vec3 origin{m_camPos[0], m_camPos[1], m_camPos[2]};
        const raw::worldgen::Vec3 dir{sy * cp, cy * cp, sp};
        const auto hit = m_worldPhysics.RayCast(origin, dir, 8000.0f);
        const float targetXu = hit.hit ? hit.point[0] : (m_camPos[0] + dir.x * 650.0f);
        const float targetYu = hit.hit ? hit.point[1] : (m_camPos[1] + dir.y * 650.0f);

        const auto& cfg = m_ecosystem.Config();
        const float spanX = static_cast<float>(cfg.gridW) * cfg.cellSizeM;
        const float spanY = static_cast<float>(cfg.gridH) * cfg.cellSizeM;
        const float wxM = std::clamp(targetXu / 70.0f, cfg.originX + cfg.cellSizeM * 3.0f,
                                     cfg.originX + spanX - cfg.cellSizeM * 3.0f);
        const float wyM = std::clamp(targetYu / 70.0f, cfg.originY + cfg.cellSizeM * 3.0f,
                                     cfg.originY + spanY - cfg.cellSizeM * 3.0f);

        uint32_t ignited = m_ecosystem.IgniteAt(wxM, wyM, 8.0f, 680.0f);
        if (ignited == 0u) {
            // If exact look-hit is under deep snow/moisture, flash-dry a compact 4m ignition patch (dragon-fire/spell impact)
            uint32_t gx0 = cfg.gridW / 2u, gy0 = cfg.gridH / 2u;
            (void)m_ecosystem.WorldToCell(wxM, wyM, &gx0, &gy0);
            for (int32_t dy = -3; dy <= 3; ++dy) {
                for (int32_t dx = -3; dx <= 3; ++dx) {
                    const int32_t cx = std::clamp(static_cast<int32_t>(gx0) + dx, 0, static_cast<int32_t>(cfg.gridW) - 1);
                    const int32_t cy_ = std::clamp(static_cast<int32_t>(gy0) + dy, 0, static_cast<int32_t>(cfg.gridH) - 1);
                    if (auto* cell = m_ecosystem.CellAt(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy_))) {
                        if (!cell->isRiverWater) {
                            cell->snowDepthM = std::min(cell->snowDepthM, 0.05f);
                            cell->moisture01 = std::min(cell->moisture01, 0.25f);
                            cell->fuelLoad01 = std::max(cell->fuelLoad01, 0.65f);
                        }
                    }
                }
            }
            ignited = m_ecosystem.IgniteAt(wxM, wyM, 8.0f, 680.0f);
        }
        m_ecoTelemetry = m_ecosystem.ComputeTelemetry();
        std::printf("[Ecosystem] Wildfire ignited at (%.1fm, %.1fm): +%u cells | activeFire=%u snow=%.1fm3 ice=%u\n",
                    wxM, wyM, ignited, m_ecoTelemetry.activeFireCells,
                    m_ecoTelemetry.totalSnowVolumeM3, m_ecoTelemetry.blackIceCells);
        return ignited;
    }

    struct EcosystemProbe {
        uint32_t ignitedCells    = 0u;
        uint32_t activeFireCells = 0u;
        float    burnedAreaM2    = 0.0f;
        float    snowVolumeM3    = 0.0f;
        float    meltRunoffM3    = 0.0f;
        uint32_t blackIceCells   = 0u;
        float    peakTempC       = 0.0f;
        bool     allFinite       = true;
    };

    EcosystemProbe RunEcosystemSelfTest()
    {
        EcosystemProbe p{};
        const auto& cfg = m_ecosystem.Config();
        const float cxM = cfg.originX + static_cast<float>(cfg.gridW) * cfg.cellSizeM * 0.5f;
        const float cyM = cfg.originY + static_cast<float>(cfg.gridH) * cfg.cellSizeM * 0.5f;

        // Pick a non-river dry patch near center so ignition is guaranteed deterministic across biomes/cells
        uint32_t gx = cfg.gridW / 2u, gy = cfg.gridH / 2u;
        bool foundDry = false;
        for (uint32_t y = 8u; y + 8u < cfg.gridH && !foundDry; ++y) {
            for (uint32_t x = 8u; x + 8u < cfg.gridW && !foundDry; ++x) {
                const auto* c = m_ecosystem.CellAt(x, y);
                if (c && !c->isRiverWater && c->fuelLoad01 > 0.15f && c->moisture01 < 0.60f && c->snowDepthM <= 0.12f) {
                    gx = x; gy = y; foundDry = true;
                }
            }
        }
        if (!foundDry) {
            for (int32_t dy = -3; dy <= 3; ++dy) {
                for (int32_t dx = -3; dx <= 3; ++dx) {
                    const uint32_t x = static_cast<uint32_t>(std::clamp(static_cast<int32_t>(gx) + dx, 1, static_cast<int32_t>(cfg.gridW) - 2));
                    const uint32_t y = static_cast<uint32_t>(std::clamp(static_cast<int32_t>(gy) + dy, 1, static_cast<int32_t>(cfg.gridH) - 2));
                    if (auto* c = m_ecosystem.CellAt(x, y)) {
                        c->isRiverWater = false;
                        c->snowDepthM   = 0.02f;
                        c->moisture01   = 0.18f;
                        c->fuelLoad01   = 0.78f;
                    }
                }
            }
        }
        const auto cc = m_ecosystem.CellCenterWorldM(gx, gy);
        const float ixM = foundDry ? cc.x : cxM;
        const float iyM = foundDry ? cc.y : cyM;
        p.ignitedCells = m_ecosystem.IgniteAt(ixM, iyM, 6.0f, 650.0f);

        raw::worldgen::EcoWeatherInput ew{};
        ew.ambientTempC      = 12.0f;
        ew.windDirX          = 0.866f;
        ew.windDirY          = 0.5f;
        ew.windBeaufort      = 5.0f;
        ew.rainRate01        = 0.0f;
        ew.snowRate01        = 0.0f;
        ew.solarInsolation01 = 0.75f;
        for (int s = 0; s < 10; ++s) {
            m_ecosystem.Step(0.4f, ew);
        }
        const auto t = m_ecosystem.ComputeTelemetry();
        m_ecoTelemetry    = t;
        p.activeFireCells = t.activeFireCells;
        p.burnedAreaM2    = t.burnedAreaM2;
        p.snowVolumeM3    = t.totalSnowVolumeM3;
        p.meltRunoffM3    = t.totalMeltRunoffM3;
        p.blackIceCells   = t.blackIceCells;
        p.peakTempC       = t.peakTempC;
        p.allFinite       = std::isfinite(t.burnedAreaM2) && std::isfinite(t.totalSnowVolumeM3) &&
                            std::isfinite(t.totalMeltRunoffM3) && std::isfinite(t.totalWaterMassM3) &&
                            std::isfinite(t.peakTempC) && std::isfinite(t.steamFlux);
        return p;
    }

    static raw::worldgen::WildlifeEnvInput BuildWildlifeEnvInput(const raw::worldgen::WeatherGpuUniforms& wGpu,
                                                                 SeasonChoice season, WeatherPreset weather,
                                                                 float waterLevelZ)
    {
        raw::worldgen::WildlifeEnvInput env{};
        env.solarInsolation01  = std::clamp(0.55f + 0.40f * (1.0f - std::min(wGpu.cloudWind.z, 1.0f)), 0.05f, 1.0f);
        env.rainIntensity01    = (weather == WeatherPreset::Rainstorm) ? std::max(wGpu.weather.w, 0.75f) : wGpu.weather.w;
        env.snowIntensity01    = (season == SeasonChoice::Winter || weather == WeatherPreset::Blizzard)
                               ? std::max(wGpu.weather.z, 0.35f) : wGpu.weather.z;
        env.windBeaufort       = std::clamp(wGpu.beaufortScale, 0.0f, 12.0f);
        env.windDirXY          = {wGpu.cloudWind.x, wGpu.cloudWind.y};
        env.riverCurrentXY     = {0.866f, 0.5f};
        env.defaultWaterLevelZ = waterLevelZ;
        env.ecoUnitsPerMeter   = 70.0f;
        return env;
    }

    void SpawnWildlifeAround(float cx, float cy, float groundZ, float waterZ)
    {
        m_wildlife.Clear();
        const float safeWaterZ = (waterZ > -10000.0f) ? waterZ : (groundZ - 120.0f);
        m_wildlife.SpawnGuild(raw::worldgen::FaunaGuild::AlpineHawk,        16u, {cx, cy, groundZ + 1100.0f}, 1200.0f, 5201u);
        m_wildlife.SpawnGuild(raw::worldgen::FaunaGuild::PineCrowFlock,     32u, {cx, cy, groundZ + 280.0f},   650.0f, 5202u);
        m_wildlife.SpawnGuild(raw::worldgen::FaunaGuild::TundraDeerHerd,    40u, {cx, cy, groundZ + 12.0f},    900.0f, 5203u);
        m_wildlife.SpawnGuild(raw::worldgen::FaunaGuild::RiverSalmonSchool, 40u, {cx, cy, safeWaterZ - 35.0f}, 500.0f, 5204u);
        m_wildlifeWaterZ      = safeWaterZ;
        m_wildlifeInitialized = true;
        m_wildlifeTelemetry   = m_wildlife.GetLastTelemetry();
        m_wildlifeTelemetry.activeAgents = static_cast<uint32_t>(m_wildlife.Agents().size());
    }

    void TriggerWildlifeStartle()
    {
        if (!m_wildlifeInitialized) {
            SpawnWildlifeAround(m_camPos[0], m_camPos[1], m_camPos[2] - 250.0f, m_camPos[2] - 400.0f);
        }
        m_wildlife.EmitStimulus({m_camPos[0], m_camPos[1], m_camPos[2]}, 2500.0f,
                                raw::worldgen::StimulusKind::LoudNoise, 1.0f, 3.0f);
        const auto wGpu = m_weatherSim.GetGpuUniforms();
        m_wildlifeTelemetry = m_wildlife.Step(
            1.0f / 60.0f, BuildWildlifeEnvInput(wGpu, m_season, m_weatherPreset, m_wildlifeWaterZ),
            &m_worldPhysics, &m_ecosystem);
        std::printf("[Wildlife] Acoustic shockwave at (%.0f, %.0f, %.0f) r=2500u | agents=%u roost=%u startle=%u\n",
                    m_camPos[0], m_camPos[1], m_camPos[2],
                    m_wildlifeTelemetry.activeAgents, m_wildlifeTelemetry.roostingCount,
                    m_wildlifeTelemetry.startledCount);
    }

    struct WildlifeProbe {
        uint32_t activeAgents  = 0u;
        uint32_t roostingCount = 0u;
        uint32_t startledCount = 0u;
        float    meanSpeed     = 0.0f;
        double   stepMs        = 0.0;
        bool     allFinite     = true;
    };

    WildlifeProbe RunWildlifeSelfTest()
    {
        WildlifeProbe p{};
        if (!m_wildlifeInitialized || m_wildlife.Agents().size() != 128u) {
            SpawnWildlifeAround(m_camPos[0], m_camPos[1], m_camPos[2] - 300.0f, m_camPos[2] - 450.0f);
        }
        // Emit loud acoustic shockwave right at the PineCrowFlock centroid (r=2500u) and verify startle flush!
        raw::worldgen::Vec3 crowCenter{m_camPos[0], m_camPos[1], m_camPos[2]};
        uint32_t crowCnt = 0u;
        raw::worldgen::Vec3 acc{0.0f, 0.0f, 0.0f};
        for (const auto& ag : m_wildlife.Agents()) {
            if (ag.guild == raw::worldgen::FaunaGuild::PineCrowFlock) {
                acc.x += ag.position.x; acc.y += ag.position.y; acc.z += ag.position.z;
                ++crowCnt;
            }
        }
        if (crowCnt > 0u) {
            crowCenter = {acc.x / float(crowCnt), acc.y / float(crowCnt), acc.z / float(crowCnt)};
        }
        m_wildlife.EmitStimulus(crowCenter, 2500.0f, raw::worldgen::StimulusKind::LoudNoise, 1.0f, 3.0f);

        const auto wGpu = m_weatherSim.GetGpuUniforms();
        const auto env  = BuildWildlifeEnvInput(wGpu, m_season, m_weatherPreset, m_wildlifeWaterZ);
        raw::worldgen::WildlifeTelemetry tel{};
        for (int s = 0; s < 10; ++s) {
            tel = m_wildlife.Step(1.0f / 60.0f, env, &m_worldPhysics, &m_ecosystem);
        }
        m_wildlifeTelemetry = tel;
        p.activeAgents  = tel.activeAgents;
        p.roostingCount = tel.roostingCount;
        p.startledCount = tel.startledCount;
        p.meanSpeed     = tel.meanSpeed;
        p.stepMs        = tel.stepMs;
        p.allFinite     = std::isfinite(tel.meanSpeed) && std::isfinite(tel.stepMs);
        for (const auto& ag : m_wildlife.Agents()) {
            if (!std::isfinite(ag.position.x) || !std::isfinite(ag.position.y) || !std::isfinite(ag.position.z) ||
                !std::isfinite(ag.velocity.x) || !std::isfinite(ag.velocity.y) || !std::isfinite(ag.velocity.z) ||
                !std::isfinite(ag.bankAngleRad)) {
                p.allFinite = false;
            }
        }
        return p;
    }

    static raw::worldgen::SettlementWeatherMap BuildSettlementWeatherMap(
        const raw::worldgen::WeatherGpuUniforms& wGpu, SeasonChoice season, WeatherPreset weather)
    {
        raw::worldgen::SettlementWeatherMap wm{};
        wm.seasonDaylightHours = (season == SeasonChoice::Summer) ? 16.0f
                               : (season == SeasonChoice::Winter ? 8.0f : 12.0f);
        wm.enableProductionConsumption = true;
        wm.enableBanditAmbushes        = true;
        const float baseBlizz = (weather == WeatherPreset::Blizzard) ? 0.85f
                              : (season == SeasonChoice::Winter ? std::max(wGpu.weather.z * 0.75f, 0.25f) : wGpu.weather.z * 0.4f);
        const float baseRain  = (weather == WeatherPreset::Rainstorm) ? 0.85f : wGpu.weather.w;
        for (size_t i = 0u; i < raw::worldgen::kHoldCount; ++i) {
            auto& hw = wm.byHold[i];
            const auto hid = static_cast<raw::worldgen::HoldId>(i);
            const bool northern = (hid == raw::worldgen::HoldId::Winterhold ||
                                   hid == raw::worldgen::HoldId::Windhelm ||
                                   hid == raw::worldgen::HoldId::Dawnstar);
            hw.blizzard01 = std::clamp(baseBlizz + (northern ? 0.18f : 0.0f), 0.0f, 1.0f);
            hw.rain01     = std::clamp(baseRain, 0.0f, 1.0f);
            hw.tempC      = wGpu.seasonThermal0.w - (northern ? 5.5f : 0.0f);
        }
        return wm;
    }

    void AdvanceEconomyAndCycleHold()
    {
        const auto wGpu = m_weatherSim.GetGpuUniforms();
        const auto wm   = BuildSettlementWeatherMap(wGpu, m_season, m_weatherPreset);
        m_settlementSim.StepHours(6.0f, wm);
        m_settlementTelemetry = m_settlementSim.ComputeWorldTelemetry();
        m_activeHoldIdx = (m_activeHoldIdx + 1u) % static_cast<uint32_t>(raw::worldgen::kHoldCount);
        const auto hid = static_cast<raw::worldgen::HoldId>(m_activeHoldIdx);
        const auto* hs = m_settlementSim.GetHold(hid);
        const float grainPrice = hs ? hs->price[static_cast<size_t>(raw::worldgen::CommodityKind::Grain)] : 0.0f;
        std::printf("[Economy] +6h | Hold=%s grain=%.1fg | caravans=%u (snowbound=%u) | scarce=%s | gold=%.0f\n",
                    raw::worldgen::GetHoldName(hid), grainPrice,
                    m_settlementTelemetry.totalCaravansActive, m_settlementTelemetry.snowboundCaravans,
                    raw::worldgen::GetHoldName(m_settlementTelemetry.mostScarceHold),
                    m_settlementTelemetry.totalGold);
    }

    struct SettlementProbe {
        double   initialGold         = 0.0;
        double   finalGold           = 0.0;
        uint32_t activeCaravans      = 0u;
        uint32_t snowboundCaravans   = 0u;
        uint32_t deliveriesCompleted = 0u;
        float    meanPriceIndex      = 0.0f;
        const char* scarceHoldName   = "";
        bool     goldConserved       = false;
        bool     allFinite           = true;
    };

    SettlementProbe RunSettlementSelfTest()
    {
        SettlementProbe p{};
        const auto before = m_settlementSim.ComputeWorldTelemetry();
        p.initialGold = before.totalGold;

        const auto wGpu = m_weatherSim.GetGpuUniforms();
        const auto wm   = BuildSettlementWeatherMap(wGpu, m_season, m_weatherPreset);
        m_settlementSim.StepHours(24.0f, wm);
        const auto after = m_settlementSim.ComputeWorldTelemetry();
        m_settlementTelemetry = after;

        p.finalGold           = after.totalGold;
        p.activeCaravans      = after.totalCaravansActive;
        p.snowboundCaravans   = after.snowboundCaravans;
        p.deliveriesCompleted = after.deliveriesCompleted;
        p.meanPriceIndex      = after.meanPriceIndex;
        p.scarceHoldName      = raw::worldgen::GetHoldName(after.mostScarceHold);
        p.goldConserved       = std::abs(p.finalGold - p.initialGold) < 1.0e-2 && p.initialGold > 0.0;
        p.allFinite           = std::isfinite(after.totalGold) && std::isfinite(after.meanPriceIndex) &&
                                std::isfinite(after.simHoursElapsed) && std::isfinite(after.timeOfDayHours);
        for (size_t i = 0u; i < raw::worldgen::kHoldCount; ++i) {
            if (const auto* hs = m_settlementSim.GetHold(static_cast<raw::worldgen::HoldId>(i))) {
                if (!std::isfinite(hs->treasuryGold) || !std::isfinite(hs->tavernOccupancy01)) {
                    p.allFinite = false;
                }
                for (size_t c = 0u; c < raw::worldgen::kCommodityCount; ++c) {
                    if (!std::isfinite(hs->stock[c]) || !std::isfinite(hs->price[c])) {
                        p.allFinite = false;
                    }
                }
            }
        }
        return p;
    }

    raw::worldgen::WorldEventInputs BuildWorldEventInputs(const raw::worldgen::WeatherGpuUniforms& wGpu) const
    {
        raw::worldgen::WorldEventInputs ei{};
        ei.settlement         = &m_settlementSim;
        ei.ecosystem          = &m_ecosystem;
        ei.wildlife           = &m_wildlife;
        ei.weatherSim         = &m_weatherSim;
        ei.timeOfDayHours     = TimePresetHours(m_timePreset);
        ei.season             = static_cast<raw::worldgen::SeasonId>(static_cast<uint8_t>(m_season));
        ei.auroraKp           = (m_timePreset == TimePreset::MidnightAurora) ? 6.5f : 2.5f;
        ei.cloudCover01       = std::clamp(wGpu.cloudWind.z, 0.0f, 1.0f);
        ei.blizzard01         = (m_weatherPreset == WeatherPreset::Blizzard) ? 0.85f : wGpu.weather.z;
        ei.ecosystemHoldId    = raw::worldgen::HoldId::Falkreath;
        ei.wildlifeHoldId     = raw::worldgen::HoldId::Whiterun;
        ei.useWeatherSimClock = false;
        return ei;
    }

    void ResolveOrPollWorldEvent()
    {
        const auto wGpu = m_weatherSim.GetGpuUniforms();
        const auto& active = m_eventDirector.ActiveEvents();
        if (!active.empty()) {
            const raw::worldgen::WorldEvent* top = &active[0];
            for (const auto& ev : active) {
                if (ev.urgency01 > top->urgency01) top = &ev;
            }
            const uint32_t id = top->id;
            const auto evKind = top->kind;
            const auto evHold = top->holdId;
            const char* kindName = raw::worldgen::GetWorldEventKindName(evKind);
            const char* holdName = raw::worldgen::GetHoldName(evHold);
            m_eventDirector.ResolveEvent(id);
            m_diplomacy.ApplyEventReward(evKind, evHold);
            m_eventTelemetry     = m_eventDirector.ComputeTelemetry();
            m_diplomacyTelemetry = m_diplomacy.ComputeTelemetry();
            const auto* hs = m_diplomacy.GetHoldStanding(evHold);
            std::printf("[WorldEvent] Resolved #%u %s (%s) -> HoldRep=%+.1f PriceMult=%.3f | active=%u resolved=%u\n",
                        id, kindName, holdName,
                        hs ? hs->reputation : 0.0f, hs ? hs->merchantPriceMult : 1.0f,
                        m_eventTelemetry.activeCount, m_eventTelemetry.totalResolved);
        } else {
            m_eventDirector.StepHours(4.0f, BuildWorldEventInputs(wGpu));
            m_eventTelemetry = m_eventDirector.ComputeTelemetry();
            const char* topName = m_eventDirector.ActiveEvents().empty()
                                ? "None"
                                : raw::worldgen::GetWorldEventKindName(m_eventDirector.ActiveEvents()[0].kind);
            std::printf("[WorldEvent] Polled +4h | active=%u top=%s spawned=%u resolved=%u expired=%u\n",
                        m_eventTelemetry.activeCount, topName, m_eventTelemetry.totalSpawned,
                        m_eventTelemetry.totalResolved, m_eventTelemetry.totalExpired);
        }
    }

    struct WorldEventProbe {
        uint32_t activeCount       = 0u;
        uint32_t totalSpawned      = 0u;
        uint32_t totalResolved     = 0u;
        uint32_t totalExpired      = 0u;
        const char* topKindName    = "None";
        const char* topHoldName    = "Whiterun";
        bool     accountingExact   = false;
        bool     allFinite         = true;
    };

    WorldEventProbe RunWorldEventSelfTest()
    {
        WorldEventProbe p{};
        const auto wGpu = m_weatherSim.GetGpuUniforms();
        const auto ei   = BuildWorldEventInputs(wGpu);
        m_eventDirector.StepHours(2.0f, ei);
        // Resolve the highest-urgency active event once to exercise full Spawn -> Resolve lifecycle
        if (!m_eventDirector.ActiveEvents().empty()) {
            const raw::worldgen::WorldEvent* top = &m_eventDirector.ActiveEvents()[0];
            for (const auto& ev : m_eventDirector.ActiveEvents()) {
                if (ev.urgency01 > top->urgency01) top = &ev;
            }
            m_eventDirector.ResolveEvent(top->id);
        }
        m_eventDirector.StepHours(1.0f, ei);
        const auto tel = m_eventDirector.ComputeTelemetry();
        m_eventTelemetry  = tel;
        p.activeCount     = tel.activeCount;
        p.totalSpawned    = tel.totalSpawned;
        p.totalResolved   = tel.totalResolved;
        p.totalExpired    = tel.totalExpired;
        p.accountingExact = (tel.totalSpawned == (tel.activeCount + tel.totalResolved + tel.totalExpired));
        p.allFinite       = std::isfinite(tel.highestUrgency);
        if (!m_eventDirector.ActiveEvents().empty()) {
            const raw::worldgen::WorldEvent* top = &m_eventDirector.ActiveEvents()[0];
            for (const auto& ev : m_eventDirector.ActiveEvents()) {
                if (ev.urgency01 > top->urgency01) top = &ev;
                if (!std::isfinite(ev.urgency01) || !std::isfinite(ev.rewardGold) || !std::isfinite(ev.ageHours)) {
                    p.allFinite = false;
                }
            }
            p.topKindName = raw::worldgen::GetWorldEventKindName(top->kind);
            p.topHoldName = raw::worldgen::GetHoldName(top->holdId);
        }
        return p;
    }

    void CycleDiplomacyInspect()
    {
        m_diplomacyHoldIdx = (m_diplomacyHoldIdx + 1u) % static_cast<uint32_t>(raw::worldgen::kHoldCount);
        m_diplomacyFactionIdx = (m_diplomacyFactionIdx + 1u) % static_cast<uint32_t>(raw::worldgen::kFactionCount);
        const auto hid = static_cast<raw::worldgen::HoldId>(m_diplomacyHoldIdx);
        const auto fid = static_cast<raw::worldgen::FactionId>(m_diplomacyFactionIdx);
        const auto* hs = m_diplomacy.GetHoldStanding(hid);
        const auto* fs = m_diplomacy.GetFactionStanding(fid);
        std::printf("[Diplomacy] Hold=%s rep=%+.1f bounty=%.0fg posture=%s priceMult=%.3f | Faction=%s rep=%+.1f\n",
                    raw::worldgen::GetHoldName(hid),
                    hs ? hs->reputation : 0.0f, hs ? hs->bountyGold : 0.0f,
                    hs ? raw::worldgen::GetGuardPostureName(hs->guardPosture) : "Peaceful",
                    hs ? hs->merchantPriceMult : 1.0f,
                    raw::worldgen::GetFactionName(fid),
                    fs ? fs->reputation : 0.0f);
    }

    struct DiplomacyProbe {
        float holdRepAfterReward   = 0.0f;
        float merchantPriceMult    = 1.0f;
        float imperialRep          = 0.0f;
        float stormcloakRep        = 0.0f;
        bool  allBounded           = true;
        bool  allFinite            = true;
    };

    DiplomacyProbe RunDiplomacySelfTest()
    {
        DiplomacyProbe p{};
        m_diplomacy.ApplyEventReward(raw::worldgen::WorldEventKind::WildfireContainment, raw::worldgen::HoldId::Whiterun);
        m_diplomacy.ApplyEventReward(raw::worldgen::WorldEventKind::BlizzardCaravanRescue, raw::worldgen::HoldId::Whiterun);
        m_diplomacy.AdjustFactionReputation(raw::worldgen::FactionId::ImperialLegion, 40.0f);
        m_diplomacyTelemetry = m_diplomacy.ComputeTelemetry();

        if (const auto* wh = m_diplomacy.GetHoldStanding(raw::worldgen::HoldId::Whiterun)) {
            p.holdRepAfterReward = wh->reputation;
            p.merchantPriceMult  = wh->merchantPriceMult;
        }
        if (const auto* imp = m_diplomacy.GetFactionStanding(raw::worldgen::FactionId::ImperialLegion)) {
            p.imperialRep = imp->reputation;
        }
        if (const auto* sc = m_diplomacy.GetFactionStanding(raw::worldgen::FactionId::Stormcloaks)) {
            p.stormcloakRep = sc->reputation;
        }
        for (size_t i = 0u; i < raw::worldgen::kHoldCount; ++i) {
            if (const auto* hs = m_diplomacy.GetHoldStanding(static_cast<raw::worldgen::HoldId>(i))) {
                if (!std::isfinite(hs->reputation) || !std::isfinite(hs->bountyGold) || !std::isfinite(hs->merchantPriceMult)) {
                    p.allFinite = false;
                }
                if (hs->reputation < -100.0f || hs->reputation > 100.0f) p.allBounded = false;
            }
        }
        for (size_t i = 0u; i < raw::worldgen::kFactionCount; ++i) {
            if (const auto* fs = m_diplomacy.GetFactionStanding(static_cast<raw::worldgen::FactionId>(i))) {
                if (!std::isfinite(fs->reputation)) p.allFinite = false;
                if (fs->reputation < -100.0f || fs->reputation > 100.0f) p.allBounded = false;
            }
        }
        return p;
    }

    bool QuicksaveWorld()
    {
        const auto snap = raw::worldgen::RawWorldSerializer::CaptureFromSystems(
            &m_weatherSim, &m_settlementSim, &m_eventDirector, &m_diplomacy, &m_ecosystem);
        std::string err;
        const bool binOk = raw::worldgen::RawWorldSerializer::SaveWorldToFile(snap, "raw_world_quicksave.bin", &err);
        const std::string json = raw::worldgen::RawWorldSerializer::SerializeWorldJson(snap);
        bool jsonOk = false;
        FILE* fp = nullptr;
        if (fopen_s(&fp, "raw_world_quicksave.json", "wb") == 0 && fp) {
            jsonOk = (std::fwrite(json.data(), 1u, json.size(), fp) == json.size());
            std::fclose(fp);
        }
        if (binOk && jsonOk) {
            m_lastSaveStatus = "F5 SAVED";
            std::printf("[Serializer] F5 Quicksave OK: raw_world_quicksave.bin + raw_world_quicksave.json (%zu B json)\n", json.size());
            return true;
        }
        m_lastSaveStatus = "F5 ERR";
        std::printf("[Serializer] F5 Quicksave failed: %s\n", err.c_str());
        return false;
    }

    bool QuickloadWorld()
    {
        raw::worldgen::WorldSnapshotState snap{};
        std::string err;
        if (!raw::worldgen::RawWorldSerializer::LoadWorldFromFile("raw_world_quicksave.bin", snap, &err)) {
            m_lastSaveStatus = "F8 ERR";
            std::printf("[Serializer] F8 Quickload failed: %s\n", err.c_str());
            return false;
        }
        raw::worldgen::RawWorldSerializer::RestoreIntoSystems(
            snap, &m_weatherSim, &m_settlementSim, &m_eventDirector, &m_diplomacy);
        m_settlementTelemetry = m_settlementSim.ComputeWorldTelemetry();
        m_eventTelemetry      = m_eventDirector.ComputeTelemetry();
        m_diplomacyTelemetry  = m_diplomacy.ComputeTelemetry();
        m_lastSaveStatus = "F8 LOADED";
        std::printf("[Serializer] F8 Quickload OK (CRC-32 verified): caravans=%u events=%u meanRep=%+.1f\n",
                    m_settlementTelemetry.totalCaravansActive, m_eventTelemetry.activeCount,
                    m_diplomacyTelemetry.meanHoldReputation);
        return true;
    }

    struct SerializerProbe {
        size_t binaryBytes     = 0u;
        size_t jsonBytes       = 0u;
        bool   roundTripOk     = false;
        bool   bitExact        = false;
        bool   tamperRejected  = false;
        bool   jsonValid       = false;
    };

    SerializerProbe RunWorldSerializerSelfTest()
    {
        SerializerProbe p{};
        const auto snap1 = raw::worldgen::RawWorldSerializer::CaptureFromSystems(
            &m_weatherSim, &m_settlementSim, &m_eventDirector, &m_diplomacy, &m_ecosystem);
        std::vector<uint8_t> bin1;
        if (!raw::worldgen::RawWorldSerializer::SerializeWorldBinary(snap1, bin1)) {
            return p;
        }
        p.binaryBytes = bin1.size();

        // Planted 1-bit CRC-32 tamper control
        std::vector<uint8_t> tampered = bin1;
        if (!tampered.empty()) {
            tampered.back() ^= 0x01u;
            raw::worldgen::WorldSnapshotState badSnap{};
            p.tamperRejected = !raw::worldgen::RawWorldSerializer::DeserializeWorldBinary(
                tampered.data(), tampered.size(), badSnap);
        }

        // Exact round-trip + re-serialization bit-exact check + live restore check
        raw::worldgen::WorldSnapshotState snap2{};
        std::string err;
        p.roundTripOk = raw::worldgen::RawWorldSerializer::DeserializeWorldBinary(
            bin1.data(), bin1.size(), snap2, &err);
        if (p.roundTripOk) {
            std::vector<uint8_t> bin2;
            if (raw::worldgen::RawWorldSerializer::SerializeWorldBinary(snap2, bin2)) {
                p.bitExact = (bin1.size() == bin2.size() && std::memcmp(bin1.data(), bin2.data(), bin1.size()) == 0);
            }
            raw::worldgen::RawWorldSerializer::RestoreIntoSystems(
                snap2, &m_weatherSim, &m_settlementSim, &m_eventDirector, &m_diplomacy);
            m_settlementTelemetry = m_settlementSim.ComputeWorldTelemetry();
            m_eventTelemetry      = m_eventDirector.ComputeTelemetry();
            m_diplomacyTelemetry  = m_diplomacy.ComputeTelemetry();
        }

        const std::string json = raw::worldgen::RawWorldSerializer::SerializeWorldJson(snap2);
        p.jsonBytes = json.size();
        p.jsonValid = (json.find("\"weather\"") != std::string::npos &&
                       json.find("\"settlement\"") != std::string::npos &&
                       json.find("\"diplomacy\"") != std::string::npos &&
                       json.find("\"ecosystem\"") != std::string::npos);
        return p;
    }

    bool EnterProceduralDungeon(raw::worldgen::DungeonArchetype arch, uint32_t seed = 202657u)
    {
        if (m_mode != WorldMode::Dungeon) {
            m_preDungeonMode    = m_mode;
            m_savedOverCam[0]   = m_camPos[0];
            m_savedOverCam[1]   = m_camPos[1];
            m_savedOverCam[2]   = m_camPos[2];
            m_savedOverYaw      = m_camYaw;
            m_savedOverPitch    = m_camPitch;
        }
        raw::worldgen::DungeonGenConfig dcfg{};
        dcfg.archetype      = arch;
        dcfg.seed           = seed + static_cast<uint32_t>(arch) * 977u;
        dcfg.roomCount      = 9u;
        dcfg.tileSizeU      = 128.0f;
        dcfg.ceilingHeightU = 384.0f;
        dcfg.maxMeshletTris = 126u;

        raw::worldgen::GeneratedDungeon dg{};
        if (!raw::worldgen::RawDungeonGen::GenerateDungeon(dcfg, dg) || dg.mesh.meshlets.empty()) {
            return false;
        }
        TerrainGpuChunk gpu{};
        if (!m_meshHost.UploadTerrainChunk(dg.mesh, &gpu)) {
            return false;
        }
        for (auto& kv : m_dungeonChunks) {
            m_meshHost.DeferRelease(std::move(kv.second));
        }
        m_dungeonChunks.clear();
        m_dungeonChunks.emplace(kDungeonChunkKey, std::move(gpu));

        m_dungeonPhysics.Clear();
        m_dungeonPhysics.AddCellCollider(raw::worldgen::PackCellKey(0, 0), dg.mesh, 7001u, 2u);

        m_activeDungeon = std::move(dg);
        m_mode          = WorldMode::Dungeon;
        m_hasPrevFroxelView = false;
        m_volFroxels.ResetHistory();
        m_cloudRaymarch.ResetHistory();
        m_tsrUpscale.ResetHistory();

        // Bake interior 3D NavMesh strictly below vaulted ceilings & above dungeon floors
        raw::worldgen::NavBakeConfig ncfg{};
        ncfg.originX        = m_activeDungeon.worldBoundsMin.x - 64.0f;
        ncfg.originY        = m_activeDungeon.worldBoundsMin.y - 64.0f;
        ncfg.sizeX          = (m_activeDungeon.worldBoundsMax.x - m_activeDungeon.worldBoundsMin.x) + 128.0f;
        ncfg.sizeY          = (m_activeDungeon.worldBoundsMax.y - m_activeDungeon.worldBoundsMin.y) + 128.0f;
        ncfg.cellSizeU      = 64.0f;
        ncfg.rayStartZ      = m_activeDungeon.recommendedNavRayStartZ;
        ncfg.rayMaxDist     = 2000.0f;
        ncfg.agentHeadroomU = 130.0f;
        m_navMesh.BakeFromPhysics(m_dungeonPhysics, ncfg);
        m_navMeshDirty = false;
        ++m_navMeshBakeCount;

        // Place camera at dungeon entrance facing toward first corridor / boss chamber
        m_camPos[0] = m_activeDungeon.entrancePos[0];
        m_camPos[1] = m_activeDungeon.entrancePos[1];
        m_camPos[2] = m_activeDungeon.entrancePos[2] + 120.0f;
        const float dx = m_activeDungeon.bossPos[0] - m_activeDungeon.entrancePos[0];
        const float dy = m_activeDungeon.bossPos[1] - m_activeDungeon.entrancePos[1];
        m_camYaw   = std::atan2(dx, dy);
        m_camPitch = -0.10f;
        if (m_walkMode) {
            m_playerBody = raw::worldgen::CharacterBody{};
            m_playerBody.position = {m_activeDungeon.entrancePos[0], m_activeDungeon.entrancePos[1], m_activeDungeon.entrancePos[2] + 6.0f};
        }
        InitTacticalSquadAround(m_activeDungeon.entrancePos[0], m_activeDungeon.entrancePos[1]);
        ComputeLiveNavPath();
        std::printf("[Dungeon] Entered %s: rooms=%zu (bfs=%u) corridors=%zu meshlets=%zu lights=%zu navWalk=%u\n",
                    raw::worldgen::GetDungeonArchetypeName(m_activeDungeon.archetype),
                    m_activeDungeon.rooms.size(), m_activeDungeon.bfsReachableRooms,
                    m_activeDungeon.corridors.size(), m_activeDungeon.mesh.meshlets.size(),
                    m_activeDungeon.lights.size(), m_navMesh.GetStats().walkableCells);
        return true;
    }

    void ExitDungeonToOverworld()
    {
        if (m_mode != WorldMode::Dungeon) return;
        m_mode      = m_preDungeonMode;
        m_camPos[0] = m_savedOverCam[0];
        m_camPos[1] = m_savedOverCam[1];
        m_camPos[2] = m_savedOverCam[2];
        m_camYaw    = m_savedOverYaw;
        m_camPitch  = m_savedOverPitch;
        m_navMeshDirty      = true;
        m_hasPrevFroxelView = false;
        m_volFroxels.ResetHistory();
        m_cloudRaymarch.ResetHistory();
        m_tsrUpscale.ResetHistory();
        for (const auto& q : m_questStory.Quests()) {
            if (q.lifecycle == raw::worldgen::QuestLifecycle::Active &&
                q.archetype == raw::worldgen::QuestArchetype::ClearDungeon) {
                (void)AdvanceQuestHappyPath(q.id);
                break;
            }
        }
        std::printf("[Dungeon] Returned to Overworld (%s)\n",
                    m_mode == WorldMode::RiverwoodEsm ? ActiveLandmarkName() : "Frontier");
    }

    void CycleDungeonExplorer()
    {
        if (m_mode != WorldMode::Dungeon) {
            m_dungeonCycleIdx = 0u;
            EnterProceduralDungeon(raw::worldgen::DungeonArchetype::NordicBarrow);
        } else if (m_dungeonCycleIdx + 1u < static_cast<uint32_t>(raw::worldgen::DungeonArchetype::Count)) {
            ++m_dungeonCycleIdx;
            EnterProceduralDungeon(static_cast<raw::worldgen::DungeonArchetype>(m_dungeonCycleIdx));
        } else {
            m_dungeonCycleIdx = 0u;
            ExitDungeonToOverworld();
        }
    }

    struct DungeonExplorerProbe {
        bool     barrowOk        = false;
        bool     iceCavernOk     = false;
        bool     dwemerRuinOk    = false;
        bool     returnedOk      = false;
        uint32_t navWalkable     = 0u;
        size_t   astarWaypoints  = 0u;
        float    astarLengthU    = 0.0f;
        uint32_t totalMeshlets   = 0u;
        uint32_t totalLights     = 0u;
        bool     walkGrounded    = false;
    };

    DungeonExplorerProbe RunDungeonExplorerSelfTest()
    {
        DungeonExplorerProbe p{};
        const WorldMode origMode = m_mode;

        // 1. Nordic Barrow: generate, upload GPU meshlets, bake interior NavMesh, Entrance->Boss A* + Walk capsule
        p.barrowOk = EnterProceduralDungeon(raw::worldgen::DungeonArchetype::NordicBarrow, 202657u);
        if (p.barrowOk) {
            p.navWalkable    = m_navMesh.GetStats().walkableCells;
            p.astarWaypoints = m_lastNavPath.found ? m_lastNavPath.waypoints.size() : 0u;
            p.astarLengthU   = m_lastNavPath.totalLengthU;
            p.totalMeshlets += static_cast<uint32_t>(m_activeDungeon.mesh.meshlets.size());
            p.totalLights   += static_cast<uint32_t>(m_activeDungeon.lights.size());

            raw::worldgen::CharacterBody body{};
            body.position = {m_activeDungeon.entrancePos[0], m_activeDungeon.entrancePos[1], m_activeDungeon.entrancePos[2] + 24.0f};
            raw::worldgen::CharacterInput cin{};
            cin.wishDirXY = {std::sin(m_camYaw), std::cos(m_camYaw)};
            cin.maxSpeed  = 240.0f;
            for (int s = 0; s < 20; ++s) {
                m_dungeonPhysics.StepCharacterCapsule(body, cin, 1.0f / 60.0f);
            }
            p.walkGrounded = body.grounded;
        }
        // 2. Glacial Ice Cavern
        p.iceCavernOk = EnterProceduralDungeon(raw::worldgen::DungeonArchetype::GlacialIceCavern, 202657u);
        if (p.iceCavernOk) {
            p.totalMeshlets += static_cast<uint32_t>(m_activeDungeon.mesh.meshlets.size());
            p.totalLights   += static_cast<uint32_t>(m_activeDungeon.lights.size());
        }
        // 3. Basalt Dwemer Ruin + render 1 live GPU frame inside the dungeon!
        p.dwemerRuinOk = EnterProceduralDungeon(raw::worldgen::DungeonArchetype::BasaltDwemerRuin, 202657u);
        if (p.dwemerRuinOk) {
            p.totalMeshlets += static_cast<uint32_t>(m_activeDungeon.mesh.meshlets.size());
            p.totalLights   += static_cast<uint32_t>(m_activeDungeon.lights.size());
            (void)RenderFrame(1.0f / 60.0f, false);
        }
        // 4. Return cleanly to original Overworld mode!
        ExitDungeonToOverworld();
        p.returnedOk = (m_mode == origMode);
        return p;
    }

    void UpdateSkeletonRigsAndFootIk(float dtSec)
    {
        (void)dtSec;
        auto& phys = ActivePhysics();
        const auto pst = phys.GetStats();
        const raw::worldgen::RawWorldPhysics* physOpt =
            (pst.heightfieldCount > 0u || pst.totalTriangles > 0u) ? &phys : nullptr;

        // 1. Player 24-bone procedural rig with live Terrain Foot-IK & XPBD attachment anchors
        raw::worldgen::Vec3 playerRoot{m_camPos[0], m_camPos[1], m_camPos[2] - 120.0f};
        float playerSpeedXY = 0.0f;
        if (m_walkMode) {
            playerRoot    = m_playerBody.position;
            playerSpeedXY = std::hypot(m_playerBody.velocity.x, m_playerBody.velocity.y);
        } else if (physOpt) {
            const auto dh = physOpt->RayCast({m_camPos[0], m_camPos[1], m_camPos[2] + 64.0f},
                                             {0.0f, 0.0f, -1.0f}, 20000.0f);
            if (dh.hit) {
                playerRoot.z = dh.point[2];
            } else {
                float hz = 0.0f;
                if (physOpt->SampleTerrainHeight(m_camPos[0], m_camPos[1], &hz)) playerRoot.z = hz;
            }
        }
        raw::worldgen::AnimBlendParams pbp{};
        pbp.phaseSec = m_simTimeSec;
        if (playerSpeedXY > 300.0f) {
            pbp.primaryClip   = raw::worldgen::AnimClipId::Walk;
            pbp.secondaryClip = raw::worldgen::AnimClipId::Run;
            pbp.blendAlpha    = std::clamp((playerSpeedXY - 280.0f) / 240.0f, 0.0f, 1.0f);
        } else {
            pbp.primaryClip   = raw::worldgen::AnimClipId::Idle;
            pbp.secondaryClip = raw::worldgen::AnimClipId::Walk;
            pbp.blendAlpha    = std::clamp(playerSpeedXY / 260.0f, 0.0f, 1.0f);
        }
        // Couple live RawCombatSandboxEngine combat action states into RawSkeletonAnimEngine -> RawXpbdSimHost!
        if (m_combatActive && m_combatPlayerId != 0u) {
            if (const auto* pa = m_combatSandbox.FindActor(m_combatPlayerId)) {
                if (pa->state == raw::worldgen::CombatActionState::Windup ||
                    pa->state == raw::worldgen::CombatActionState::ActiveSwing) {
                    pbp.upperBodyClip           = raw::worldgen::AnimClipId::MeleeSwing;
                    pbp.upperBodyAdditiveWeight = 1.0f;
                    pbp.upperBodyPhaseSec       = pa->animPhaseSec;
                } else if (pa->state == raw::worldgen::CombatActionState::BowDrawing ||
                           pa->state == raw::worldgen::CombatActionState::Casting) {
                    pbp.upperBodyClip           = raw::worldgen::AnimClipId::BowDraw;
                    pbp.upperBodyAdditiveWeight = 1.0f;
                    pbp.upperBodyPhaseSec       = pa->animPhaseSec;
                } else if (pa->state == raw::worldgen::CombatActionState::Blocking) {
                    pbp.upperBodyClip           = raw::worldgen::AnimClipId::MeleeSwing;
                    pbp.upperBodyAdditiveWeight = 0.50f;
                    pbp.upperBodyPhaseSec       = 0.25f;
                }
            }
        }
        raw::worldgen::RawSkeletonAnimEngine::EvaluateCharacterRig(
            playerRoot, m_camYaw, pbp, physOpt, m_playerPose, &m_playerXpbdAnchors);
        m_playerRootPos = playerRoot;
        m_playerRigEvaluated = true;

        // 2. NavMesh Tactical Squad 24-bone procedural rigs with live Terrain Foot-IK
        m_squadPoses.resize(m_tacticalSquad.size());
        m_squadAnchors.resize(m_tacticalSquad.size());
        for (size_t i = 0u; i < m_tacticalSquad.size(); ++i) {
            const auto& ag = m_tacticalSquad[i];
            const float spd = std::hypot(ag.velocity.x, ag.velocity.y);
            const float yaw = (spd > 1.0f) ? std::atan2(ag.velocity.x, ag.velocity.y)
                                           : std::atan2(ag.facing.x, ag.facing.y);
            raw::worldgen::AnimBlendParams sbp{};
            sbp.phaseSec                = m_simTimeSec + static_cast<float>(i) * 0.37f;
            sbp.primaryClip             = (spd > 180.0f) ? raw::worldgen::AnimClipId::Run : raw::worldgen::AnimClipId::Walk;
            sbp.secondaryClip           = raw::worldgen::AnimClipId::Idle;
            sbp.blendAlpha              = (spd > 15.0f) ? 0.0f : 1.0f;
            sbp.upperBodyClip           = (ag.archetype == raw::worldgen::TacticalArchetype::MeleeFlanker)
                                        ? raw::worldgen::AnimClipId::MeleeSwing
                                        : raw::worldgen::AnimClipId::BowDraw;
            sbp.upperBodyAdditiveWeight = 0.65f;
            sbp.upperBodyPhaseSec       = m_simTimeSec * 1.15f + static_cast<float>(i) * 0.5f;
            if (m_combatActive && (i + 1u) < m_combatSandbox.Actors().size()) {
                const auto& ca = m_combatSandbox.Actors()[i + 1u];
                if (ca.state == raw::worldgen::CombatActionState::Windup ||
                    ca.state == raw::worldgen::CombatActionState::ActiveSwing) {
                    sbp.primaryClip             = raw::worldgen::AnimClipId::Run;
                    sbp.upperBodyClip           = raw::worldgen::AnimClipId::MeleeSwing;
                    sbp.upperBodyAdditiveWeight = 1.0f;
                    sbp.upperBodyPhaseSec       = ca.animPhaseSec;
                } else if (ca.state == raw::worldgen::CombatActionState::BowDrawing ||
                           ca.state == raw::worldgen::CombatActionState::Casting) {
                    sbp.upperBodyClip           = raw::worldgen::AnimClipId::BowDraw;
                    sbp.upperBodyAdditiveWeight = 1.0f;
                    sbp.upperBodyPhaseSec       = ca.animPhaseSec;
                }
            }
            raw::worldgen::RawSkeletonAnimEngine::EvaluateCharacterRig(
                ag.position, yaw, sbp, physOpt, m_squadPoses[i], &m_squadAnchors[i]);
        }
    }

    static raw::worldgen::CombatWeatherInput BuildCombatWeatherInput(const raw::worldgen::WeatherGpuUniforms& wGpu,
                                                                     WeatherPreset weather)
    {
        raw::worldgen::CombatWeatherInput cwx{};
        cwx.wetness01    = (weather == WeatherPreset::Rainstorm) ? std::max(wGpu.weather.w, 0.75f) : wGpu.weather.w;
        cwx.ambientTempC = wGpu.seasonThermal0.w;
        cwx.windSpeedMps = std::clamp(wGpu.beaufortScale * 2.2f, 0.0f, 35.0f);
        const float wl   = std::hypot(wGpu.cloudWind.x, wGpu.cloudWind.y);
        cwx.windDirXY    = (wl > 1.0e-4f) ? raw::worldgen::Vec2{wGpu.cloudWind.x / wl, wGpu.cloudWind.y / wl}
                                          : raw::worldgen::Vec2{1.0f, 0.0f};
        return cwx;
    }

    void SpawnCombatSkirmishAround(float cx, float cy, float groundZ)
    {
        m_combatSandbox.Clear();
        auto& phys = ActivePhysics();
        auto groundAt = [&](float wx, float wy) -> float {
            float hz = groundZ;
            if (phys.SampleTerrainHeight(wx, wy, &hz)) return hz;
            const auto dh = phys.RayCast({wx, wy, groundZ + 600.0f}, {0.0f, 0.0f, -1.0f}, 2000.0f);
            return dh.hit ? dh.point[2] : groundZ;
        };

        // Player Hero (index 0, manual combat actions + 220ms timed-parry)
        raw::worldgen::CombatActor player{};
        player.faction       = raw::worldgen::CombatFaction::PlayerAllies;
        player.role          = raw::worldgen::CombatRole::ShieldBearer;
        player.position      = {cx, cy, groundAt(cx, cy)};
        player.facing        = {std::sin(m_camYaw), std::cos(m_camYaw), 0.0f};
        player.hp            = 320.0f;
        player.maxHp         = 320.0f;
        player.armorRating   = 220.0f;
        player.aiControlled  = false;
        m_equippedStats.ApplyToCombatActor(player);
        m_combatPlayerId     = m_combatSandbox.SpawnActor(player);

        static constexpr raw::worldgen::CombatRole kRoles[4] = {
            raw::worldgen::CombatRole::MeleeWarrior,
            raw::worldgen::CombatRole::ShieldBearer,
            raw::worldgen::CombatRole::Archer,
            raw::worldgen::CombatRole::Battlemage,
        };
        // 3 Allied Vanguard + 4 Hostile Raiders forming an active 4v4 skirmish line
        for (uint32_t i = 0u; i < 3u; ++i) {
            raw::worldgen::CombatActor ally{};
            const float ox = cx + (float(i) - 1.0f) * 140.0f;
            const float oy = cy - 90.0f;
            ally.faction       = raw::worldgen::CombatFaction::PlayerAllies;
            ally.role          = kRoles[(i + 1u) % 4u];
            ally.position      = {ox, oy, groundAt(ox, oy)};
            ally.facing        = {0.0f, 1.0f, 0.0f};
            ally.attackElement = (ally.role == raw::worldgen::CombatRole::Battlemage)
                               ? raw::worldgen::DamageElement::Shock : raw::worldgen::DamageElement::Physical;
            m_combatSandbox.SpawnActor(ally);
        }
        for (uint32_t i = 0u; i < 4u; ++i) {
            raw::worldgen::CombatActor foe{};
            const float ox = cx + (float(i) - 1.5f) * 130.0f;
            const float oy = cy + 165.0f + float(i % 2u) * 85.0f;
            foe.faction       = raw::worldgen::CombatFaction::Hostiles;
            foe.role          = kRoles[i];
            foe.position      = {ox, oy, groundAt(ox, oy)};
            foe.facing        = {0.0f, -1.0f, 0.0f};
            foe.attackElement = (foe.role == raw::worldgen::CombatRole::Battlemage)
                              ? raw::worldgen::DamageElement::Frost : raw::worldgen::DamageElement::Physical;
            m_combatSandbox.SpawnActor(foe);
        }
        m_combatActive      = true;
        m_combatActionCycle = 0u;
        m_combatTelemetry   = m_combatSandbox.ComputeTelemetry();
    }

    void ToggleOrTriggerCombatSkirmish()
    {
        auto& phys = ActivePhysics();
        const auto pst = phys.GetStats();
        const raw::worldgen::RawWorldPhysics* physOpt =
            (pst.heightfieldCount > 0u || pst.totalTriangles > 0u) ? &phys : nullptr;
        const auto cwx = BuildCombatWeatherInput(m_weatherSim.GetGpuUniforms(), m_weatherPreset);

        if (!m_combatActive) {
            float gz = m_walkMode ? m_playerBody.position.z : (m_camPos[2] - 120.0f);
            if (!m_walkMode && physOpt) {
                (void)physOpt->SampleTerrainHeight(m_camPos[0], m_camPos[1], &gz);
            }
            SpawnCombatSkirmishAround(m_camPos[0], m_camPos[1], gz);
            m_combatSandbox.RaiseBlock(m_combatPlayerId);
            m_combatTelemetry = m_combatSandbox.Step(1.0f / 60.0f, cwx, physOpt, m_navMesh.IsBaked() ? &m_navMesh : nullptr);
            std::printf("[Combat] Skirmish STARTED (4v4, 220ms Timed-Parry Ready): allies=%u hostiles=%u\n",
                        m_combatTelemetry.aliveAllies, m_combatTelemetry.aliveHostiles);
            return;
        }

        const uint32_t act = m_combatActionCycle++ % 4u;
        uint32_t nearestHostile = 0u;
        raw::worldgen::Vec3 hostilePos{m_camPos[0], m_camPos[1] + 220.0f, m_camPos[2] - 120.0f};
        for (const auto& a : m_combatSandbox.Actors()) {
            if (a.faction == raw::worldgen::CombatFaction::Hostiles && a.state != raw::worldgen::CombatActionState::Dead) {
                nearestHostile = a.id;
                hostilePos     = a.position;
                break;
            }
        }
        if (act == 0u) {
            m_combatSandbox.RaiseBlock(m_combatPlayerId);
            if (nearestHostile != 0u) m_combatSandbox.StartMeleeAttack(m_combatPlayerId, nearestHostile, true);
            std::printf("[Combat] Player Timed-Parry + Power Attack against actor #%u\n", nearestHostile);
        } else if (act == 1u) {
            m_combatSandbox.TriggerShout(m_combatPlayerId, 12.0f);
            std::printf("[Combat] Player UNRELENTING FORCE Shout triggered!\n");
        } else if (act == 2u) {
            const uint32_t pid = m_combatSandbox.FireBallisticProjectile(
                m_combatPlayerId, hostilePos, 2200.0f, raw::worldgen::DamageElement::Shock);
            std::printf("[Combat] Player Ballistic Arrow Volley fired (proj #%u)\n", pid);
        } else {
            m_combatActive = false;
            m_combatSandbox.Clear();
            m_combatPlayerId = 0u;
            m_combatTelemetry = {};
            std::printf("[Combat] Skirmish ENDED cleanly.\n");
            return;
        }
        m_combatTelemetry = m_combatSandbox.Step(1.0f / 60.0f, cwx, physOpt, m_navMesh.IsBaked() ? &m_navMesh : nullptr);
    }

    struct CombatSkirmishProbe {
        uint32_t aliveAllies         = 0u;
        uint32_t aliveHostiles       = 0u;
        uint32_t hitsLanded          = 0u;
        uint32_t parries             = 0u;
        uint32_t poiseBreaks         = 0u;
        uint32_t arrowsFired         = 0u;
        bool     timedParryTriggered = false;
        bool     skeletonCoupled     = false;
        bool     allFinite           = true;
    };

    CombatSkirmishProbe RunCombatSkirmishSelfTest()
    {
        CombatSkirmishProbe p{};
        auto& phys = ActivePhysics();
        const auto pst = phys.GetStats();
        const raw::worldgen::RawWorldPhysics* physOpt =
            (pst.heightfieldCount > 0u || pst.totalTriangles > 0u) ? &phys : nullptr;
        const auto cwx = BuildCombatWeatherInput(m_weatherSim.GetGpuUniforms(), m_weatherPreset);

        float gz = m_camPos[2] - 120.0f;
        if (physOpt) (void)physOpt->SampleTerrainHeight(m_camPos[0], m_camPos[1], &gz);
        SpawnCombatSkirmishAround(m_camPos[0], m_camPos[1], gz);

        // 1. Deterministic 220ms frontal timed-parry test + melee hit + arrow volley + shout
        m_combatSandbox.RaiseBlock(m_combatPlayerId);
        uint32_t firstHostileId = 0u;
        for (const auto& a : m_combatSandbox.Actors()) {
            if (a.faction == raw::worldgen::CombatFaction::Hostiles) { firstHostileId = a.id; break; }
        }
        auto* pa = m_combatSandbox.FindActor(m_combatPlayerId);
        auto* ha = m_combatSandbox.FindActor(firstHostileId);
        if (pa && ha) {
            ha->position = {pa->position.x, pa->position.y + 85.0f, pa->position.z};
            ha->facing   = {0.0f, -1.0f, 0.0f};
            pa->facing   = {0.0f,  1.0f, 0.0f};
            pa->blockAgeSec = 0.110f; // Inside 220ms timed-parry window!
            const auto parryRes = m_combatSandbox.ResolveMeleeHit(*ha, *pa, 40.0f, 45.0f,
                                                                  raw::worldgen::DamageElement::Physical, cwx);
            p.timedParryTriggered = parryRes.parried && (parryRes.finalDamage == 0.0f);
            // Fire ballistic arrow volley from player toward hostile archer
            (void)m_combatSandbox.FireBallisticProjectile(
                m_combatPlayerId, {pa->position.x + 120.0f, pa->position.y + 240.0f, pa->position.z + 60.0f},
                2200.0f, raw::worldgen::DamageElement::Shock);
            // Trigger player melee swing so RawSkeletonAnimEngine evaluates MeleeSwing upper-body blend!
            (void)m_combatSandbox.StartMeleeAttack(m_combatPlayerId, firstHostileId, true);
            UpdateSkeletonRigsAndFootIk(1.0f / 60.0f);
            p.skeletonCoupled = m_playerRigEvaluated &&
                                std::isfinite(m_playerXpbdAnchors.headCenter.x) &&
                                std::isfinite(m_playerXpbdAnchors.capeShoulderLeft.z);
            (void)m_combatSandbox.TriggerShout(m_combatPlayerId, 18.0f);
        }

        // 2. Step 30 combat frames coupled to live ActivePhysics() + NavMesh + Weather!
        for (int s = 0; s < 30; ++s) {
            m_combatTelemetry = m_combatSandbox.Step(1.0f / 60.0f, cwx, physOpt,
                                                     m_navMesh.IsBaked() ? &m_navMesh : nullptr);
        }
        for (const auto& a : m_combatSandbox.Actors()) {
            if (!std::isfinite(a.position.x) || !std::isfinite(a.position.y) || !std::isfinite(a.position.z) ||
                !std::isfinite(a.hp) || !std::isfinite(a.stamina) || !std::isfinite(a.poise)) {
                p.allFinite = false;
            }
        }
        p.aliveAllies   = m_combatTelemetry.aliveAllies;
        p.aliveHostiles = m_combatTelemetry.aliveHostiles;
        p.hitsLanded    = m_combatTelemetry.totalHitsLanded;
        p.parries       = m_combatTelemetry.totalParries;
        p.poiseBreaks   = m_combatTelemetry.totalPoiseBreaks;
        p.arrowsFired   = m_combatTelemetry.totalArrowsFired;

        // Leave skirmish cleanly active for subsequent frames so RenderFrame exercises live Combat -> Skeleton -> XPBD!
        return p;
    }

    bool AdvanceQuestHappyPath(uint32_t questId)
    {
        const auto* q = m_questStory.FindQuest(questId);
        if (!q || q->lifecycle != raw::worldgen::QuestLifecycle::Active) return false;
        for (const auto& node : q->stages) {
            if (node.stageId == q->currentStageId) {
                for (uint32_t nextId : node.allowedNextStages) {
                    bool isFail = false;
                    for (const auto& cand : q->stages) {
                        if (cand.stageId == nextId && cand.isTerminalFailure) {
                            isFail = true;
                            break;
                        }
                    }
                    if (!isFail) {
                        const bool ok = m_questStory.AdvanceQuestStage(questId, nextId, &m_diplomacy);
                        if (ok) {
                            m_questTelemetry     = m_questStory.ComputeTelemetry();
                            m_diplomacyTelemetry = m_diplomacy.ComputeTelemetry();
                        }
                        return ok;
                    }
                }
                break;
            }
        }
        return false;
    }

    uint32_t GenerateLiveRadiantQuests(uint32_t maxNew = 3u)
    {
        raw::worldgen::RadiantQuestInputs in{};
        in.settlement = &m_settlementSim;
        in.diplomacy  = &m_diplomacy;
        in.events     = &m_eventDirector;
        in.dungeonOpt = !m_activeDungeon.rooms.empty() ? &m_activeDungeon : nullptr;
        in.seed       = 202660u + m_questSeedCounter++;
        const uint32_t added = m_questStory.GenerateRadiantQuests(in, maxNew);
        m_questTelemetry     = m_questStory.ComputeTelemetry();
        return added;
    }

    void CycleRadiantQuestJournal()
    {
        uint32_t activeId = 0u;
        for (const auto& q : m_questStory.Quests()) {
            if (q.lifecycle == raw::worldgen::QuestLifecycle::Active) {
                activeId = q.id;
                break;
            }
        }
        if (activeId == 0u) {
            const uint32_t added = GenerateLiveRadiantQuests(3u);
            std::printf("[Quest] Generated %u radiant quests (active=%u completed=%u gold=%.0fg)\n",
                        added, m_questTelemetry.activeCount, m_questTelemetry.completedCount,
                        m_questTelemetry.totalGoldAwarded);
            return;
        }
        static constexpr raw::worldgen::DialogueApproach kApproaches[3] = {
            raw::worldgen::DialogueApproach::Persuade,
            raw::worldgen::DialogueApproach::Bribe,
            raw::worldgen::DialogueApproach::Intimidate,
        };
        const auto app = kApproaches[m_questDialogueCycle++ % 3u];
        raw::worldgen::DialogueCheckInput dIn{};
        dIn.approach          = app;
        dIn.speechSkill       = 78.0f;
        dIn.intimidationLevel = 42.0f;
        dIn.availableGold     = 1200.0f;
        dIn.difficultyDC      = 45.0f;
        if (const auto* q = m_questStory.FindQuest(activeId)) {
            if (const auto* st = m_diplomacy.GetHoldStanding(q->holdId)) {
                dIn.holdReputation = st->reputation;
            }
        }
        const auto dRes = m_questStory.EvaluateDialogueBranch(activeId, dIn, &m_diplomacy);
        const bool adv  = AdvanceQuestHappyPath(activeId);
        m_questTelemetry     = m_questStory.ComputeTelemetry();
        m_diplomacyTelemetry = m_diplomacy.ComputeTelemetry();
        std::printf("[Quest] #%u %s=%d (p=%.2f, +%.0fg) adv=%d | active=%u completed=%u totalGold=%.0fg\n",
                    activeId, raw::worldgen::GetDialogueApproachName(app), int(dRes.succeeded),
                    static_cast<double>(dRes.passProbability), static_cast<double>(dRes.bonusRewardGold),
                    int(adv), m_questTelemetry.activeCount, m_questTelemetry.completedCount,
                    m_questTelemetry.totalGoldAwarded);
    }

    struct QuestStoryProbe {
        uint32_t generatedCount   = 0u;
        uint32_t activeCount      = 0u;
        uint32_t completedCount   = 0u;
        bool     dialoguePassed   = false;
        bool     stageCompleted   = false;
        bool     journalJsonValid = false;
        double   totalGoldAwarded = 0.0;
    };

    QuestStoryProbe RunQuestStorySelfTest()
    {
        QuestStoryProbe p{};
        m_questStory.Clear();
        p.generatedCount = GenerateLiveRadiantQuests(3u);
        uint32_t qid = 0u;
        for (const auto& q : m_questStory.Quests()) {
            if (q.lifecycle == raw::worldgen::QuestLifecycle::Active) {
                qid = q.id;
                break;
            }
        }
        if (qid != 0u) {
            raw::worldgen::DialogueCheckInput dIn{};
            dIn.approach          = raw::worldgen::DialogueApproach::Persuade;
            dIn.speechSkill       = 82.0f;
            dIn.intimidationLevel = 45.0f;
            dIn.availableGold     = 1000.0f;
            dIn.holdReputation    = 20.0f;
            dIn.difficultyDC      = 40.0f;
            const auto dRes = m_questStory.EvaluateDialogueBranch(qid, dIn, &m_diplomacy);
            p.dialoguePassed = dRes.succeeded && std::isfinite(dRes.passProbability);

            // Step quest cleanly through its verified DAG stages until terminal completion
            for (int step = 0; step < 6; ++step) {
                const auto* q = m_questStory.FindQuest(qid);
                if (!q || q->lifecycle == raw::worldgen::QuestLifecycle::Completed) break;
                if (!AdvanceQuestHappyPath(qid)) break;
            }
            if (const auto* q = m_questStory.FindQuest(qid)) {
                p.stageCompleted = (q->lifecycle == raw::worldgen::QuestLifecycle::Completed);
            }
        }
        m_questTelemetry     = m_questStory.ComputeTelemetry();
        m_diplomacyTelemetry = m_diplomacy.ComputeTelemetry();
        p.activeCount        = m_questTelemetry.activeCount;
        p.completedCount     = m_questTelemetry.completedCount;
        p.totalGoldAwarded   = m_questTelemetry.totalGoldAwarded;

        const std::string j = m_questStory.ExportJournalJson();
        p.journalJsonValid = (j.find("\"quests\"") != std::string::npos &&
                              j.find("\"activeCount\"") != std::string::npos &&
                              j.find("\xE2\x80\x94") == std::string::npos);
        return p;
    }

    void RefreshCraftingTelemetryAndApplyToHero()
    {
        m_encumbrance   = m_craftingEngine.EvaluateEncumbrance();
        m_equippedStats = m_craftingEngine.ComputeEquippedCombatStats();
        if (m_combatActive && m_combatPlayerId != 0u) {
            if (auto* hero = m_combatSandbox.FindActor(m_combatPlayerId)) {
                m_equippedStats.ApplyToCombatActor(*hero);
            }
        }
    }

    void InitStarterCraftingLoadout()
    {
        m_craftingEngine.Clear(300.0f);

        raw::worldgen::ItemDef sword{};
        sword.name          = "Nordic Greatsword";
        sword.category      = raw::worldgen::ItemCategory::Weapon;
        sword.material      = raw::worldgen::MaterialTier::Steel;
        sword.weight        = 16.0f;
        sword.baseValueGold = 420.0f;
        sword.baseDamage    = 34.0f;
        m_forgeSwordId      = m_craftingEngine.AddItem(sword, 1u);

        raw::worldgen::ItemDef cuirass{};
        cuirass.name          = "Nordic Carved Cuirass";
        cuirass.category      = raw::worldgen::ItemCategory::ArmorChest;
        cuirass.material      = raw::worldgen::MaterialTier::Steel;
        cuirass.weight        = 28.0f;
        cuirass.baseValueGold = 650.0f;
        cuirass.baseArmor     = 115.0f;
        m_forgeArmorId        = m_craftingEngine.AddItem(cuirass, 1u);

        raw::worldgen::ItemDef helm{};
        helm.name          = "Nordic Steel Helm";
        helm.category      = raw::worldgen::ItemCategory::ArmorHead;
        helm.material      = raw::worldgen::MaterialTier::Steel;
        helm.weight        = 7.0f;
        helm.baseValueGold = 180.0f;
        helm.baseArmor     = 45.0f;
        const uint32_t helmId = m_craftingEngine.AddItem(helm, 1u);

        raw::worldgen::ItemDef ring{};
        ring.name          = "Silver Signet Ring";
        ring.category      = raw::worldgen::ItemCategory::Ring;
        ring.material      = raw::worldgen::MaterialTier::Steel;
        ring.weight        = 0.5f;
        ring.baseValueGold = 210.0f;
        m_forgeRingId      = m_craftingEngine.AddItem(ring, 1u);

        raw::worldgen::ItemDef ingA{};
        ingA.name              = "Snowberries";
        ingA.category          = raw::worldgen::ItemCategory::Ingredient;
        ingA.weight            = 0.2f;
        ingA.baseValueGold     = 12.0f;
        ingA.ingredientEffects = {
            raw::worldgen::AlchemyEffect::ResistFrost,
            raw::worldgen::AlchemyEffect::FortifyEnchanting,
            raw::worldgen::AlchemyEffect::ResistFire,
            raw::worldgen::AlchemyEffect::None
        };
        m_forgeIngAId = m_craftingEngine.AddItem(ingA, 8u);

        raw::worldgen::ItemDef ingB{};
        ingB.name              = "Blue Butterfly Wing";
        ingB.category          = raw::worldgen::ItemCategory::Ingredient;
        ingB.weight            = 0.1f;
        ingB.baseValueGold     = 15.0f;
        ingB.ingredientEffects = {
            raw::worldgen::AlchemyEffect::FortifyEnchanting,
            raw::worldgen::AlchemyEffect::DamageHealth,
            raw::worldgen::AlchemyEffect::None,
            raw::worldgen::AlchemyEffect::None
        };
        m_forgeIngBId = m_craftingEngine.AddItem(ingB, 8u);

        (void)m_craftingEngine.EquipItem(m_forgeSwordId);
        (void)m_craftingEngine.EquipItem(m_forgeArmorId);
        (void)m_craftingEngine.EquipItem(helmId);
        (void)m_craftingEngine.EquipItem(m_forgeRingId);
        m_forgeCycleIdx = 0u;
        RefreshCraftingTelemetryAndApplyToHero();
    }

    void CycleCraftingForgeAction()
    {
        const uint32_t step = m_forgeCycleIdx++ % 4u;
        if (step == 0u) {
            const auto tr = m_craftingEngine.TemperItem(m_forgeSwordId, 85.0f, true, m_equippedStats.fortifySmithingPct);
            RefreshCraftingTelemetryAndApplyToHero();
            std::printf("[Forge] Tempered Nordic Greatsword: ok=%d bonus=%.1f->%.1f effDmg=%.1f\n",
                        int(tr.succeeded), static_cast<double>(tr.oldTemperBonus),
                        static_cast<double>(tr.newTemperBonus), static_cast<double>(tr.effectiveStat));
        } else if (step == 1u) {
            const auto br = m_craftingEngine.BrewPotion({m_forgeIngAId, m_forgeIngBId}, 82.0f, m_equippedStats.fortifyAlchemyPct);
            RefreshCraftingTelemetryAndApplyToHero();
            std::printf("[Forge] Brewed 2-Ingredient Potion: ok=%d effects=%zu val=%.0fg\n",
                        int(br.succeeded), br.matchedEffects.size(), static_cast<double>(br.potionValueG));
        } else if (step == 2u) {
            const auto erW = m_craftingEngine.EnchantItem(
                m_forgeSwordId, raw::worldgen::SoulGemTier::Grand,
                raw::worldgen::AlchemyEffect::ShockDamage, 85.0f, 0.65f, 24.0f);
            const auto erR = m_craftingEngine.EnchantItem(
                m_forgeRingId, raw::worldgen::SoulGemTier::Grand,
                raw::worldgen::AlchemyEffect::FortifyAlchemy, 85.0f, 0.65f, 24.0f);
            RefreshCraftingTelemetryAndApplyToHero();
            std::printf("[Forge] Grand Soul Enchant: swordMag=%.1f (%u uses) ringAlch=%.1f%%\n",
                        static_cast<double>(erW.magnitude), erW.estimatedUses, static_cast<double>(erR.magnitude));
        } else {
            RefreshCraftingTelemetryAndApplyToHero();
            const auto* swSlot = m_craftingEngine.FindSlot(m_forgeSwordId);
            const auto hid = static_cast<raw::worldgen::HoldId>(m_activeHoldIdx % static_cast<uint32_t>(raw::worldgen::kHoldCount));
            const float buyG  = swSlot ? raw::worldgen::EvaluateBarterPrice(swSlot->item, true,  65.0f, hid, &m_settlementSim, &m_diplomacy) : 0.0f;
            const float sellG = swSlot ? raw::worldgen::EvaluateBarterPrice(swSlot->item, false, 65.0f, hid, &m_settlementSim, &m_diplomacy) : 0.0f;
            std::printf("[Forge] Loadout Sync -> Hero: dmg=%.1f arm=%.1f enc=%.2fx barter(buy=%.0fg sell=%.0fg)\n",
                        static_cast<double>(m_equippedStats.weaponDamage),
                        static_cast<double>(m_equippedStats.totalArmorRating),
                        static_cast<double>(m_encumbrance.moveSpeedMult),
                        static_cast<double>(buyG), static_cast<double>(sellG));
        }
    }

    struct CraftingForgeProbe {
        bool  temperOk          = false;
        bool  brewOk            = false;
        bool  enchantOk         = false;
        bool  antiExploitBounded = false;
        bool  heroCoupled       = false;
        bool  barterSpreadValid = false;
        float finalWeaponDmg    = 0.0f;
        float finalArmorRating  = 0.0f;
        float convergedAlchPct  = 0.0f;
        float convergedEnchPct  = 0.0f;
        bool  allFinite         = true;
    };

    CraftingForgeProbe RunCraftingForgeSelfTest()
    {
        CraftingForgeProbe p{};
        InitStarterCraftingLoadout();

        // 1. Temper Nordic Greatsword, brew 2-ingredient potion, Grand Soul enchant sword + ring
        const auto tr  = m_craftingEngine.TemperItem(m_forgeSwordId, 88.0f, true, 18.0f);
        const auto br  = m_craftingEngine.BrewPotion({m_forgeIngAId, m_forgeIngBId}, 85.0f, 15.0f);
        const auto erW = m_craftingEngine.EnchantItem(
            m_forgeSwordId, raw::worldgen::SoulGemTier::Grand,
            raw::worldgen::AlchemyEffect::ShockDamage, 88.0f, 0.65f, 25.0f);
        const auto erR = m_craftingEngine.EnchantItem(
            m_forgeRingId, raw::worldgen::SoulGemTier::Grand,
            raw::worldgen::AlchemyEffect::FortifyAlchemy, 88.0f, 0.65f, 25.0f);
        p.temperOk  = tr.succeeded && tr.newTemperBonus > 0.0f;
        p.brewOk    = br.succeeded && !br.matchedEffects.empty();
        p.enchantOk = erW.succeeded && erW.estimatedUses > 0u && erR.succeeded;

        // 2. 50-iteration Banach anti-exploit Fortify Alchemy <-> Fortify Enchanting convergence check
        float gearAlchPct = 0.0f;
        float potEnchPct  = 0.0f;
        float prevAlchPct = -1.0f;
        for (int iter = 0; iter < 50; ++iter) {
            prevAlchPct = gearAlchPct;
            potEnchPct  = raw::worldgen::CompressCraftingFortifyPct(
                18.0f + gearAlchPct * 0.55f, raw::worldgen::kMaxFortifyEnchantPotionPct);
            const float singlePiece = raw::worldgen::CompressCraftingFortifyPct(
                14.0f + potEnchPct * 0.45f, raw::worldgen::kMaxSinglePieceFortifyPct);
            gearAlchPct = raw::worldgen::CompressCraftingFortifyPct(
                singlePiece * 4.0f, raw::worldgen::kMaxFortifyAlchemyTotalPct);
        }
        p.convergedAlchPct   = gearAlchPct;
        p.convergedEnchPct   = potEnchPct;
        p.antiExploitBounded = std::isfinite(gearAlchPct) && std::isfinite(potEnchPct) &&
                               gearAlchPct < raw::worldgen::kMaxFortifyAlchemyTotalPct &&
                               potEnchPct  < raw::worldgen::kMaxFortifyEnchantPotionPct &&
                               std::abs(gearAlchPct - prevAlchPct) < 1.0e-3f;

        // 3. Apply live forged/enchanted equipment stats directly onto RawCombatSandboxEngine's player hero!
        RefreshCraftingTelemetryAndApplyToHero();
        p.finalWeaponDmg   = m_equippedStats.weaponDamage;
        p.finalArmorRating = m_equippedStats.totalArmorRating;
        if (m_combatActive && m_combatPlayerId != 0u) {
            if (const auto* hero = m_combatSandbox.FindActor(m_combatPlayerId)) {
                p.heroCoupled = (hero->baseAttackDamage > 34.0f && hero->armorRating >= 150.0f &&
                                 hero->attackElement == raw::worldgen::DamageElement::Shock);
            }
        }
        if (const auto* swSlot = m_craftingEngine.FindSlot(m_forgeSwordId)) {
            const float buyG  = raw::worldgen::EvaluateBarterPrice(
                swSlot->item, true,  65.0f, raw::worldgen::HoldId::Whiterun, &m_settlementSim, &m_diplomacy);
            const float sellG = raw::worldgen::EvaluateBarterPrice(
                swSlot->item, false, 65.0f, raw::worldgen::HoldId::Whiterun, &m_settlementSim, &m_diplomacy);
            p.barterSpreadValid = std::isfinite(buyG) && std::isfinite(sellG) && sellG > 0.0f && sellG < buyG;
        }
        p.allFinite = std::isfinite(p.finalWeaponDmg) && std::isfinite(p.finalArmorRating) &&
                      std::isfinite(m_encumbrance.moveSpeedMult);
        return p;
    }

    struct SkeletonFootIkProbe {
        uint32_t evaluatedRigs     = 0u;
        float    maxQuatNormErr    = 0.0f;
        float    pelvisDropZ       = 0.0f;
        bool     footPlanted       = false;
        bool     xpbdAnchorsFinite = true;
        bool     coupledToXpbd     = false;
    };

    SkeletonFootIkProbe RunSkeletonFootIkSelfTest()
    {
        SkeletonFootIkProbe p{};
        UpdateSkeletonRigsAndFootIk(1.0f / 60.0f);
        p.evaluatedRigs = 1u + static_cast<uint32_t>(m_squadPoses.size());
        p.pelvisDropZ   = m_playerPose.pelvisDropZ;
        p.footPlanted   = m_playerPose.leftFootPlanted || m_playerPose.rightFootPlanted;

        auto checkPose = [&](const raw::worldgen::SkeletonPose& pose) {
            for (const auto& bt : pose.world) {
                const float err = std::abs(raw::worldgen::QuatNorm(bt.rot) - 1.0f);
                p.maxQuatNormErr = std::max(p.maxQuatNormErr, err);
                if (!std::isfinite(bt.pos.x) || !std::isfinite(bt.pos.y) || !std::isfinite(bt.pos.z) ||
                    !std::isfinite(bt.rot.x) || !std::isfinite(bt.rot.y) || !std::isfinite(bt.rot.z) || !std::isfinite(bt.rot.w)) {
                    p.xpbdAnchorsFinite = false;
                }
            }
        };
        checkPose(m_playerPose);
        for (const auto& sp : m_squadPoses) checkPose(sp);

        auto fin3 = [](const raw::worldgen::Vec3& v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        };
        if (!fin3(m_playerXpbdAnchors.hairRootPos) || !fin3(m_playerXpbdAnchors.capeShoulderLeft) ||
            !fin3(m_playerXpbdAnchors.capeShoulderRight) || !fin3(m_playerXpbdAnchors.torsoCapsuleA) ||
            !fin3(m_playerXpbdAnchors.torsoCapsuleB) || !fin3(m_playerXpbdAnchors.headCenter)) {
            p.xpbdAnchorsFinite = false;
        }
        p.coupledToXpbd = m_playerRigEvaluated && m_xpbdOk && p.xpbdAnchorsFinite;
        return p;
    }

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
        m_navMeshDirty      = true;
        m_ecoInitialized    = false;
        m_wildlifeInitialized = false;
        m_hasPrevFroxelView = false;
        m_volFroxels.ResetHistory();
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

    void TriggerFractureAt(float px, float py, float groundZ)
    {
        const float cp = std::cos(m_camPitch);
        const float cy = std::cos(m_camYaw), sy = std::sin(m_camYaw);
        const float fx = sy * cp, fy = cy * cp;

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

    void TriggerFreshFracture()
    {
        const float cp = std::cos(m_camPitch), sp = std::sin(m_camPitch);
        const float cy = std::cos(m_camYaw),   sy = std::sin(m_camYaw);
        const float fx = sy * cp, fy = cy * cp, fz = sp;
        const float px = m_camPos[0] + fx * 620.0f;
        const float py = m_camPos[1] + fy * 620.0f;
        float groundZ = (m_mode == WorldMode::Frontier && m_hasFrontierCenter)
                      ? m_frontierGroundZ
                      : (m_camPos[2] + fz * 620.0f - 180.0f);
        float sampledZ = 0.0f;
        if (m_worldPhysics.SampleTerrainHeight(px, py, &sampledZ)) {
            groundZ = sampledZ;
        }
        TriggerFractureAt(px, py, groundZ);
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
        auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };

        if (m_walkMode) {
            // Playable Grounded Capsule Character Controller (RawWorldPhysics)!
            float wx = 0.0f, wy = 0.0f;
            if (down('W')) { wx += sy; wy += cy; }
            if (down('S')) { wx -= sy; wy -= cy; }
            if (down('D')) { wx += rx; wy += ry; }
            if (down('A')) { wx -= rx; wy -= ry; }
            const float wl = std::hypot(wx, wy);
            if (wl > 1.0e-4f) { wx /= wl; wy /= wl; }

            raw::worldgen::CharacterInput cin{};
            cin.wishDirXY    = {wx, wy};
            cin.maxSpeed     = fast ? 540.0f : 280.0f;
            cin.jumpPressed  = down(VK_SPACE);
            cin.swimVertical = (down('E') ? 1.0f : 0.0f) - (down('Q') ? 1.0f : 0.0f);
            ActivePhysics().StepCharacterCapsule(m_playerBody, cin, dt);
            m_camPos[0] = m_playerBody.position.x;
            m_camPos[1] = m_playerBody.position.y;
            m_camPos[2] = m_playerBody.position.z + 120.0f; // Eye height +120u above capsule feet
        } else {
            const float speed = (fast ? 2600.0f : 720.0f) * dt;
            if (down('W')) { m_camPos[0] += fx * speed; m_camPos[1] += fy * speed; m_camPos[2] += fz * speed; }
            if (down('S')) { m_camPos[0] -= fx * speed; m_camPos[1] -= fy * speed; m_camPos[2] -= fz * speed; }
            if (down('D')) { m_camPos[0] += rx * speed; m_camPos[1] += ry * speed; }
            if (down('A')) { m_camPos[0] -= rx * speed; m_camPos[1] -= ry * speed; }
            if (down('E')) { m_camPos[2] += speed; }
            if (down('Q')) { m_camPos[2] -= speed; }
        }
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
        m_lastFrameMs = dtClamped * 1000.0f;
        m_frameMsRing[m_frameMsRingPos % 64u] = m_lastFrameMs;
        ++m_frameMsRingPos;
        if (m_frameMsCount < 64u) ++m_frameMsCount;

        // 1. Step seasonal weather simulation, cellular ecosystem field & 3D wildlife boids!
        m_weatherSim.Step(dtClamped, {m_camPos[0], m_camPos[1], m_camPos[2]});
        const auto wGpu = m_weatherSim.GetGpuUniforms();
        m_ecosystem.Step(dtClamped, BuildEcoWeatherInput(wGpu, m_season, m_weatherPreset));
        m_ecoTelemetry = m_ecosystem.ComputeTelemetry();
        if (m_wildlifeInitialized) {
            m_wildlifeTelemetry = m_wildlife.Step(
                dtClamped, BuildWildlifeEnvInput(wGpu, m_season, m_weatherPreset, m_wildlifeWaterZ),
                &m_worldPhysics, &m_ecosystem);
        }
        m_settlementSim.StepHours(dtClamped * (90.0f / 3600.0f),
                                  BuildSettlementWeatherMap(wGpu, m_season, m_weatherPreset));
        m_settlementTelemetry = m_settlementSim.ComputeWorldTelemetry();
        m_eventDirector.StepHours(dtClamped * (90.0f / 3600.0f), BuildWorldEventInputs(wGpu));
        m_eventTelemetry = m_eventDirector.ComputeTelemetry();
        if (m_audioEnabled && m_audio.IsRunning()) {
            PushAudioSceneState(wGpu);
        }

        // 2. Stream async Frontier chunks OR live multi-cell Tamriel (Skyrim.esm + .NIF) ring around camera!
        if (m_mode == WorldMode::Frontier) {
            PumpFrontierShowcase(false);
        } else if (m_mode == WorldMode::RiverwoodEsm && m_tamrielStreamer.IsInitialized()) {
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

        // 3b. Lazily bake 64x64 local 3D NavMesh on newly loaded terrain/NIF physics & step 4-agent Tactical Squad AI!
        if (m_navMeshDirty && m_mode != WorldMode::Dungeon && m_worldPhysics.GetStats().heightfieldCount > 0u) {
            if (m_mode == WorldMode::RiverwoodEsm) {
                const auto& lm = kTamrielLandmarks[m_landmarkIdx % 4u];
                BakeLocalNavMesh(float(lm.cellX) * raw::worldgen::kEsmCellSizeU,
                                 float(lm.cellY) * raw::worldgen::kEsmCellSizeU, true);
            } else {
                BakeLocalNavMesh(m_frontierCenter[0], m_frontierCenter[1], false);
            }
        }
        auto& activePhys = ActivePhysics();
        const auto activePhysStats = activePhys.GetStats();
        if (!m_tacticalSquad.empty() && (activePhysStats.heightfieldCount > 0u || activePhysStats.totalTriangles > 0u)) {
            raw::worldgen::ThreatTarget threat{};
            float groundZ = m_camPos[2] - 120.0f;
            if (m_walkMode) {
                threat.position = m_playerBody.position;
            } else {
                float hz = 0.0f;
                if (activePhys.SampleTerrainHeight(m_camPos[0], m_camPos[1], &hz)) groundZ = hz;
                threat.position = {m_camPos[0], m_camPos[1], groundZ};
            }
            threat.facing = {std::sin(m_camYaw), std::cos(m_camYaw), 0.0f};
            m_lastSquadOut = raw::worldgen::StepSquadTactics(
                m_tacticalSquad, threat, activePhys, m_navMesh.IsBaked() ? &m_navMesh : nullptr, dtClamped);
        }
        if (m_combatActive) {
            const raw::worldgen::RawWorldPhysics* physOpt =
                (activePhysStats.heightfieldCount > 0u || activePhysStats.totalTriangles > 0u) ? &activePhys : nullptr;
            m_combatTelemetry = m_combatSandbox.Step(
                dtClamped, BuildCombatWeatherInput(wGpu, m_weatherPreset), physOpt,
                m_navMesh.IsBaked() ? &m_navMesh : nullptr);
            if (m_combatTelemetry.aliveHostiles == 0u && m_combatTelemetry.aliveAllies > 0u) {
                for (const auto& q : m_questStory.Quests()) {
                    if (q.lifecycle == raw::worldgen::QuestLifecycle::Active &&
                        (q.archetype == raw::worldgen::QuestArchetype::BountyHunt ||
                         q.archetype == raw::worldgen::QuestArchetype::EscortCaravan)) {
                        (void)AdvanceQuestHappyPath(q.id);
                        break;
                    }
                }
            }
        }
        UpdateSkeletonRigsAndFootIk(dtClamped);

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

        // Step live GPU XPBD Soft-Body / Hair / Bush-Bone solver (256 chains / 4,096 particles) driven by Beaufort wind & player wake sphere!
        if (m_xpbdOk) {
            if (!m_xpbdUploaded) {
                std::vector<XpbdParticle> parts;
                std::vector<XpbdChainMeta> chains;
                BuildHairAndFoliageDemoChains(parts, chains, 256u);
                m_xpbdUploaded = m_xpbdSim.UploadChains(m_cmd.Get(), parts, chains);
            }
            if (m_xpbdUploaded) {
                XpbdStepParams xp{};
                xp.dt = dtClamped;
                xp.timeSec = m_simTimeSec;
                xp.substeps = 4u;
                const float wLen = std::hypot(wGpu.cloudWind.x, wGpu.cloudWind.y);
                if (wLen > 1.0e-3f) {
                    xp.windDir[0] = wGpu.cloudWind.x / wLen;
                    xp.windDir[1] = wGpu.cloudWind.y / wLen;
                    xp.windDir[2] = 0.0f;
                }
                xp.windSpeedMps = std::clamp(wGpu.beaufortScale * 2.2f, 0.5f, 28.0f);
                xp.gustPhase = m_simTimeSec * 1.4f;
                xp.snowLoad01 = std::clamp(wGpu.seasonThermal0.x, 0.0f, 1.0f);
                // Couple live 24-bone Terrain Foot-IK animated head/torso/shoulder XPBD anchors directly into cloth/hair!
                if (m_playerRigEvaluated) {
                    auto toLocalM = [&](const raw::worldgen::Vec3& wPos, float outM[3]) {
                        outM[0] = (wPos.x - m_playerRootPos.x) / 70.0f;
                        outM[1] = (wPos.y - m_playerRootPos.y) / 70.0f;
                        outM[2] = std::max(0.05f, (wPos.z - m_playerRootPos.z) / 70.0f);
                    };
                    xp.colliderCount = 4u;
                    // 0: Animated head sphere collider
                    toLocalM(m_playerXpbdAnchors.headCenter, xp.colliders[0].p0);
                    xp.colliders[0].radius = 0.14f;
                    xp.colliders[0].flags  = 0.0f; // sphere
                    // 1: Animated torso capsule collider
                    toLocalM(m_playerXpbdAnchors.torsoCapsuleA, xp.colliders[1].p0);
                    toLocalM(m_playerXpbdAnchors.torsoCapsuleB, xp.colliders[1].p1);
                    xp.colliders[1].radius = 0.22f;
                    xp.colliders[1].flags  = 1.0f; // capsule
                    // 2 & 3: Animated left & right cape shoulder sphere colliders
                    toLocalM(m_playerXpbdAnchors.capeShoulderLeft, xp.colliders[2].p0);
                    xp.colliders[2].radius = 0.12f;
                    xp.colliders[2].flags  = 0.0f;
                    toLocalM(m_playerXpbdAnchors.capeShoulderRight, xp.colliders[3].p0);
                    xp.colliders[3].radius = 0.12f;
                    xp.colliders[3].flags  = 0.0f;
                } else {
                    xp.colliderCount = 1u;
                    xp.colliders[0].p0[0] = std::fmod(m_camPos[0] / 70.0f, 8.0f);
                    xp.colliders[0].p0[1] = std::fmod(m_camPos[1] / 70.0f, 8.0f);
                    xp.colliders[0].p0[2] = 0.85f;
                    xp.colliders[0].radius = 0.65f;
                    xp.colliders[0].flags = 0.0f; // sphere
                }
                m_xpbdSim.Step(m_cmd.Get(), xp);
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

        // 8. Execute Live GPU Hi-Z + Instance Culler + 4-Cascade Radiance Cascades Screen GI + RawAgxDisplay + RawOverlayHost HUD + Copy to SwapChain!
        const UINT dispSlot = static_cast<UINT>(m_frameCount % kRingFrames);
        WaitFenceVal(m_dispFenceVal[dispSlot]);
        if (FAILED(m_dispAlloc[dispSlot]->Reset()) || FAILED(m_dispCmd->Reset(m_dispAlloc[dispSlot].Get(), nullptr))) {
            return false;
        }

        if (m_gpuCullOk && m_cullInstBuf && m_cullInstUpload && m_cullInstMapped) {
            std::vector<GpuCullInstance> insts;
            insts.reserve(std::min<size_t>(activeMap.size(), kMaxCullInstances));
            uint32_t idCounter = 0u;
            uint32_t meshletOff = 0u;
            for (const auto& kv : activeMap) {
                if (insts.size() >= kMaxCullInstances) break;
                const auto& ch = kv.second;
                const float r = std::max(ch.boundSphere[3], 64.0f);
                GpuCullInstance gi{};
                gi.center[0]     = ch.boundSphere[0];
                gi.center[1]     = ch.boundSphere[1];
                gi.center[2]     = ch.boundSphere[2];
                gi.radius        = r;
                gi.aabbMin[0]    = ch.boundSphere[0] - r;
                gi.aabbMin[1]    = ch.boundSphere[1] - r;
                gi.aabbMin[2]    = ch.boundSphere[2] - r;
                gi.maxDrawDist   = 250000.0f;
                gi.aabbMax[0]    = ch.boundSphere[0] + r;
                gi.aabbMax[1]    = ch.boundSphere[1] + r;
                gi.aabbMax[2]    = ch.boundSphere[2] + r;
                gi.meshletOffset = meshletOff;
                gi.meshletCount  = std::max(1u, ch.meshletCount);
                gi.instanceId    = idCounter++;
                gi.lodBiasFlags  = 0u;
                meshletOff      += gi.meshletCount;
                insts.push_back(gi);
            }
            const uint32_t instCount = static_cast<uint32_t>(insts.size());
            if (instCount > 0u) {
                const uint64_t slotByteOff = static_cast<uint64_t>(dispSlot) * kMaxCullInstances * sizeof(GpuCullInstance);
                std::memcpy(m_cullInstMapped + slotByteOff, insts.data(), instCount * sizeof(GpuCullInstance));
                auto bCullCopyIn = MakeBarrier(m_cullInstBuf.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
                m_dispCmd->ResourceBarrier(1u, &bCullCopyIn);
                m_dispCmd->CopyBufferRegion(m_cullInstBuf.Get(), 0ull, m_cullInstUpload.Get(), slotByteOff,
                                            static_cast<uint64_t>(instCount) * sizeof(GpuCullInstance));
                auto bCullCopyOut = MakeBarrier(m_cullInstBuf.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                m_dispCmd->ResourceBarrier(1u, &bCullCopyOut);

                m_gpuCull.BuildHiZPyramid(m_dispCmd.Get(), m_depthIn.Get(), m_width, m_height,
                                          D3D12_RESOURCE_STATE_COMMON);

                GpuCullViewParams cvp{};
                BuildWorldRowVectorViewProj(u.worldToClip, m_camPos, cvp.viewProj);
                cvp.autoExtractPlanes        = true;
                cvp.cameraPosW[0]            = m_camPos[0];
                cvp.cameraPosW[1]            = m_camPos[1];
                cvp.cameraPosW[2]            = m_camPos[2];
                cvp.depthBias                = 1.0e-4f;
                cvp.defaultIndexCountPerInst = 36u;
                cvp.enableFrustumCull        = true;
                cvp.enableDistanceCull       = true;
                cvp.enableHiZCull            = true;
                m_gpuCull.CullInstances(m_dispCmd.Get(), m_cullInstBuf.Get(), instCount, cvp,
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

                auto bCullDone = MakeBarrier(m_cullInstBuf.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                             D3D12_RESOURCE_STATE_COMMON);
                m_dispCmd->ResourceBarrier(1u, &bCullDone);
                m_lastCullInstCount = instCount;
            }
        }

        // Live 16x9x24 (3,456-Cluster) 3D Froxel Dynamic Point/Spot/Fire/Spell Light Assignment (RawLightClusterHost)!
        if (m_lightClustersOk) {
            std::vector<GpuClusterLight> clusterLights;
            clusterLights.reserve(128u);
            auto worldToView = [&](float wx, float wy, float wz, float outV[3]) {
                const float dx = wx - m_camPos[0];
                const float dy = wy - m_camPos[1];
                const float dz = wz - m_camPos[2];
                outV[0] = dx * cam.right[0] + dy * cam.right[1] + dz * cam.right[2];
                outV[1] = dx * cam.up[0]    + dy * cam.up[1]    + dz * cam.up[2];
                outV[2] = dx * cam.fwd[0]   + dy * cam.fwd[1]   + dz * cam.fwd[2];
            };

            // 1. Player forward lantern spotlight
            {
                GpuClusterLight spot{};
                spot.posView[0]   = 0.0f;
                spot.posView[1]   = -12.0f;
                spot.posView[2]   = 24.0f;
                spot.radius       = 1400.0f;
                spot.dirView[0]   = 0.0f;
                spot.dirView[1]   = 0.0f;
                spot.dirView[2]   = 1.0f;
                spot.cosOuterCone = 0.78f;
                spot.colorRgb[0]  = 2.6f; spot.colorRgb[1] = 2.1f; spot.colorRgb[2] = 1.4f;
                spot.typeFlags    = kClusterLightSpotBit | kClusterLightFire;
                clusterLights.push_back(spot);
            }
            // 2. Live 64^3 Navier-Stokes fire emitter dynamic point light
            if (m_fireEnabled) {
                const float flicker = 0.88f + 0.12f * std::sin(m_simTimeSec * 14.0f);
                GpuClusterLight fl{};
                worldToView(m_camPos[0] + cam.fwd[0] * 300.0f + cam.right[0] * 48.0f - cam.up[0] * 48.0f,
                            m_camPos[1] + cam.fwd[1] * 300.0f + cam.right[1] * 48.0f - cam.up[1] * 48.0f,
                            m_camPos[2] + cam.fwd[2] * 300.0f + cam.right[2] * 48.0f - cam.up[2] * 48.0f,
                            fl.posView);
                fl.radius      = 720.0f;
                fl.colorRgb[0] = 4.2f * flicker; fl.colorRgb[1] = 1.85f * flicker; fl.colorRgb[2] = 0.55f * flicker;
                fl.typeFlags   = kClusterLightFire;
                clusterLights.push_back(fl);
            }
            // 3. Tactical squad torches & spellcaster arcane orbs
            for (const auto& ag : m_tacticalSquad) {
                GpuClusterLight sl{};
                worldToView(ag.position.x, ag.position.y, ag.position.z + 96.0f, sl.posView);
                sl.radius = 520.0f;
                if (ag.archetype == raw::worldgen::TacticalArchetype::Spellcaster) {
                    sl.colorRgb[0] = 0.7f; sl.colorRgb[1] = 1.9f; sl.colorRgb[2] = 3.8f;
                    sl.typeFlags   = kClusterLightSpell;
                } else {
                    sl.colorRgb[0] = 2.4f; sl.colorRgb[1] = 1.3f; sl.colorRgb[2] = 0.5f;
                    sl.typeFlags   = kClusterLightFire;
                }
                clusterLights.push_back(sl);
            }
            // 4. Village/Frontier ambient hearth & braziers ring (24 deterministic local lights)
            for (uint32_t li = 0u; li < 24u; ++li) {
                const float ang = static_cast<float>(li) * (6.2831853f / 24.0f) + m_simTimeSec * 0.04f;
                const float rad = 380.0f + static_cast<float>(li % 4u) * 320.0f;
                const float wx  = m_camPos[0] + std::cos(ang) * rad;
                const float wy  = m_camPos[1] + std::sin(ang) * rad;
                float wz = m_camPos[2] - 120.0f;
                (void)m_worldPhysics.SampleTerrainHeight(wx, wy, &wz);
                GpuClusterLight bl{};
                worldToView(wx, wy, wz + 90.0f, bl.posView);
                bl.radius      = 420.0f;
                bl.colorRgb[0] = (li % 3u == 0u) ? 0.9f : 2.8f;
                bl.colorRgb[1] = (li % 3u == 0u) ? 2.1f : 1.5f;
                bl.colorRgb[2] = (li % 3u == 0u) ? 3.2f : 0.6f;
                bl.typeFlags   = (li % 3u == 0u) ? kClusterLightSpell : kClusterLightFire;
                clusterLights.push_back(bl);
            }
            // 5. Active cellular wildfire lights directly from RawWorldEcosystem (or dungeon sconces/braziers!)
            if (m_mode == WorldMode::Dungeon) {
                for (const auto& dl : m_activeDungeon.lights) {
                    GpuClusterLight dcl{};
                    worldToView(dl.pos[0], dl.pos[1], dl.pos[2], dcl.posView);
                    dcl.radius      = dl.radiusU;
                    dcl.colorRgb[0] = dl.color[0] * dl.intensity * 2.5f;
                    dcl.colorRgb[1] = dl.color[1] * dl.intensity * 2.5f;
                    dcl.colorRgb[2] = dl.color[2] * dl.intensity * 2.5f;
                    dcl.typeFlags   = (dl.kind == raw::worldgen::DungeonLightKind::BossAltarCrystal)
                                    ? kClusterLightSpell : kClusterLightFire;
                    clusterLights.push_back(dcl);
                }
            } else if (m_ecoTelemetry.activeFireCells > 0u) {
                uint32_t ecoAdded = 0u;
                const uint32_t ew = m_ecosystem.Width();
                const uint32_t eh = m_ecosystem.Height();
                for (uint32_t gy = 0u; gy < eh && ecoAdded < 64u; gy += 2u) {
                    for (uint32_t gx = 0u; gx < ew && ecoAdded < 64u; gx += 2u) {
                        const auto* cell = m_ecosystem.CellAt(gx, gy);
                        if (cell && cell->fireIntensity01 > 0.15f) {
                            const auto cc = m_ecosystem.CellCenterWorldM(gx, gy);
                            GpuClusterLight el{};
                            worldToView(cc.x * 70.0f, cc.y * 70.0f, cell->elevationM * 70.0f + 48.0f, el.posView);
                            el.radius      = 340.0f + 220.0f * cell->fireIntensity01;
                            el.colorRgb[0] = 4.5f * cell->fireIntensity01;
                            el.colorRgb[1] = 1.6f * cell->fireIntensity01;
                            el.colorRgb[2] = 0.35f * cell->fireIntensity01;
                            el.typeFlags   = kClusterLightFire;
                            clusterLights.push_back(el);
                            ++ecoAdded;
                        }
                    }
                }
            }

            LightClusterViewParams lcView{};
            lcView.nearZ = nearZ;
            lcView.farZ  = 16000.0f;
            BuildPerspectiveInvProjLH(1.12f, float(m_width) / float(m_height), lcView.nearZ, lcView.farZ, lcView.invProj);
            m_lightClusters.BuildAndAssign(m_dispCmd.Get(), clusterLights.data(),
                                           static_cast<uint32_t>(clusterLights.size()), lcView);
            m_lastClusterLightCount = static_cast<uint32_t>(clusterLights.size());
        }

        // Live 160x90x64 3D Volumetric Froxel Fog, Mie Scattering & Temporal Reprojection (RawVolumetricFroxelHost)!
        if (m_volFroxelsOk) {
            float currWorldViewProj[16]{};
            BuildWorldRowVectorViewProj(u.worldToClip, m_camPos, currWorldViewProj);
            VolumetricFroxelParams fp{};
            BuildWorldRowVectorInvViewProj(u.clipToWorld, m_camPos, fp.invViewProj);
            if (m_hasPrevFroxelView) {
                std::memcpy(fp.prevViewProj, m_prevWorldToClip, sizeof(fp.prevViewProj));
                fp.prevCameraPosW[0]     = m_prevCamPos[0];
                fp.prevCameraPosW[1]     = m_prevCamPos[1];
                fp.prevCameraPosW[2]     = m_prevCamPos[2];
                fp.prevCameraForwardW[0] = m_prevCamFwd[0];
                fp.prevCameraForwardW[1] = m_prevCamFwd[1];
                fp.prevCameraForwardW[2] = m_prevCamFwd[2];
                fp.historyWeight         = 0.85f;
                fp.enableTemporal        = true;
            } else {
                std::memcpy(fp.prevViewProj, currWorldViewProj, sizeof(fp.prevViewProj));
                fp.prevCameraPosW[0]     = m_camPos[0];
                fp.prevCameraPosW[1]     = m_camPos[1];
                fp.prevCameraPosW[2]     = m_camPos[2];
                fp.prevCameraForwardW[0] = cam.fwd[0];
                fp.prevCameraForwardW[1] = cam.fwd[1];
                fp.prevCameraForwardW[2] = cam.fwd[2];
                fp.historyWeight         = 0.0f;
                fp.enableTemporal        = false;
            }
            fp.cameraPosW[0]     = m_camPos[0]; fp.cameraPosW[1] = m_camPos[1]; fp.cameraPosW[2] = m_camPos[2];
            fp.cameraForwardW[0] = cam.fwd[0];  fp.cameraForwardW[1] = cam.fwd[1]; fp.cameraForwardW[2] = cam.fwd[2];
            fp.nearZ             = nearZ;
            fp.farZ              = 16000.0f;
            fp.simTimeSec        = m_simTimeSec;
            fp.enableHaltonJitter = true;

            float valleyZ = m_camPos[2] - 600.0f;
            (void)m_worldPhysics.SampleTerrainHeight(m_camPos[0], m_camPos[1], &valleyZ);
            fp.baseFogDensity    = 0.00018f;
            fp.valleyFloorZ      = valleyZ;
            fp.fogScaleHeight    = 2800.0f;
            fp.scatterAlbedo     = 0.90f;
            fp.rainIntensity     = u.weather[3];
            fp.snowIntensity     = u.weather[2];
            fp.noiseAmp          = 0.28f;
            fp.noiseScale        = 0.0006f;
            fp.windVelW[0]       = wGpu.cloudWind.x;
            fp.windVelW[1]       = wGpu.cloudWind.y;
            fp.windVelW[2]       = 0.0f;
            fp.mieAnisotropyG    = 0.68f;
            fp.sunDirW[0]        = u.sunDir[0]; fp.sunDirW[1] = u.sunDir[1]; fp.sunDirW[2] = u.sunDir[2];
            fp.sunIntensity      = 5.5f;
            fp.sunColor[0]       = u.sunColour[0]; fp.sunColor[1] = u.sunColour[1]; fp.sunColor[2] = u.sunColour[2];
            fp.ambientIntensity  = 0.35f;
            fp.ambientColor[0]   = 0.35f; fp.ambientColor[1] = 0.48f; fp.ambientColor[2] = 0.72f;

            uint32_t fLightCount = 0u;
            if (m_fireEnabled && fLightCount < 16u) {
                auto& fl = fp.lights[fLightCount++];
                fl.posW[0]   = m_camPos[0] + cam.fwd[0] * 300.0f + cam.right[0] * 48.0f - cam.up[0] * 48.0f;
                fl.posW[1]   = m_camPos[1] + cam.fwd[1] * 300.0f + cam.right[1] * 48.0f - cam.up[1] * 48.0f;
                fl.posW[2]   = m_camPos[2] + cam.fwd[2] * 300.0f + cam.right[2] * 48.0f - cam.up[2] * 48.0f;
                fl.radius    = 720.0f;
                fl.color[0]  = 1.0f; fl.color[1] = 0.55f; fl.color[2] = 0.18f;
                fl.intensity = 4.2f;
            }
            if (m_ecoTelemetry.activeFireCells > 0u) {
                const uint32_t ew = m_ecosystem.Width();
                const uint32_t eh = m_ecosystem.Height();
                for (uint32_t gy = 0u; gy < eh && fLightCount < 16u; gy += 4u) {
                    for (uint32_t gx = 0u; gx < ew && fLightCount < 16u; gx += 4u) {
                        const auto* cell = m_ecosystem.CellAt(gx, gy);
                        if (cell && cell->fireIntensity01 > 0.2f) {
                            const auto cc = m_ecosystem.CellCenterWorldM(gx, gy);
                            auto& el = fp.lights[fLightCount++];
                            el.posW[0]   = cc.x * 70.0f;
                            el.posW[1]   = cc.y * 70.0f;
                            el.posW[2]   = cell->elevationM * 70.0f + 56.0f;
                            el.radius    = 480.0f;
                            el.color[0]  = 1.0f; el.color[1] = 0.42f; el.color[2] = 0.10f;
                            el.intensity = 3.5f * cell->fireIntensity01;
                        }
                    }
                }
            }
            fp.lightCount = fLightCount;
            m_volFroxels.Execute(m_dispCmd.Get(), fp);

            std::memcpy(m_prevWorldToClip, currWorldViewProj, sizeof(m_prevWorldToClip));
            m_prevCamPos[0] = m_camPos[0]; m_prevCamPos[1] = m_camPos[1]; m_prevCamPos[2] = m_camPos[2];
            m_prevCamFwd[0] = cam.fwd[0];  m_prevCamFwd[1] = cam.fwd[1];  m_prevCamFwd[2] = cam.fwd[2];
            m_hasPrevFroxelView = true;
        }

        // Live GPU Variable-Penumbra PCSS Contact-Hardening Shadows + Texel-Snapped Cascade (RawShadowPcssHost)!
        if (m_shadowPcssOk && m_shadowMapProxy && m_shadowOutTex) {
            const float targetW[3] = {
                m_camPos[0] + cam.fwd[0] * 400.0f,
                m_camPos[1] + cam.fwd[1] * 400.0f,
                m_camPos[2] + cam.fwd[2] * 400.0f
            };
            const auto casc = BuildTexelSnappedCascade(targetW, u.sunDir, 2400.0f, 8000.0f, kShadowProxySize);
            ShadowPcssParams sp{};
            BuildWorldRowVectorInvViewProj(u.clipToWorld, m_camPos, sp.invViewProj);
            std::memcpy(sp.lightViewProj, casc.lightViewProj, sizeof(sp.lightViewProj));
            sp.sunDirW[0]          = u.sunDir[0]; sp.sunDirW[1] = u.sunDir[1]; sp.sunDirW[2] = u.sunDir[2];
            sp.baseDepthBias       = 4.0e-4f;
            sp.slopeDepthBias      = 8.0e-4f;
            sp.normalOffsetWorld   = 1.5f;
            sp.lightAngularSize    = 18.0f;
            sp.frameJitter         = static_cast<float>(m_frameCount & 63ull);
            sp.blockerSearchTexels = 12.0f;
            sp.minFilterTexels     = 0.75f;
            sp.maxFilterTexels     = 20.0f;
            sp.shadowMapSize       = kShadowProxySize;
            const uint32_t halfW   = std::max(1u, m_width / 2u);
            const uint32_t halfH   = std::max(1u, m_height / 2u);
            m_shadowPcssRan = m_shadowPcss.Execute(
                m_dispCmd.Get(), m_depthIn.Get(), m_shadowMapProxy.Get(), m_shadowOutTex.Get(),
                halfW, halfH, sp,
                D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_COMMON);
        }

        // Live GPU Jimenez GTAO + Bent Normals + Bilateral Denoise (RawGtaoHost — 10th Live GPU Solver!)
        if (m_gtaoOk) {
            GtaoParams gp{};
            gp.fovYDeg           = 1.12f * (180.0f / 3.14159265f);
            gp.nearZ             = nearZ;
            gp.farZ              = farZ;
            gp.effectRadius      = 120.0f;
            gp.falloffStart      = 0.65f;
            gp.aoPower           = 1.0f;
            gp.maxScreenRadiusPx = 48.0f;
            gp.depthEdgeScale    = 0.025f;
            gp.numSlices         = 2u;
            gp.numSteps          = 6u;
            gp.temporalJitter    = static_cast<float>(m_frameCount & 63ull);
            const uint32_t halfW = std::max(1u, m_width / 2u);
            const uint32_t halfH = std::max(1u, m_height / 2u);
            m_gtaoRan = m_gtao.Execute(m_dispCmd.Get(), m_depthIn.Get(), halfW, halfH, gp,
                                       D3D12_RESOURCE_STATE_COMMON);
        }

        // Live GPU GGX Stochastic Screen-Space Reflections (RawSsrHost — 11th Live GPU Solver!)
        if (m_ssrOk) {
            SsrParams sp{};
            BuildSsrPerspectiveLH(1.12f, float(m_width) / float(m_height), nearZ, farZ, sp.proj, sp.invProj);
            sp.maxRayDist        = 2800.0f;
            sp.thicknessM        = 65.0f;
            sp.nearZ             = nearZ;
            sp.startBias         = 2.0f;
            sp.fallbackRoughness = 0.08f;
            sp.maxRoughness      = 0.72f;
            sp.edgeFadeWidth     = 0.07f;
            sp.frameJitter       = static_cast<float>(m_frameCount & 63ull);
            sp.maxSteps          = 32u;
            const uint32_t halfW = std::max(1u, m_width / 2u);
            const uint32_t halfH = std::max(1u, m_height / 2u);
            m_ssrRan = m_ssr.Execute(m_dispCmd.Get(), m_hdrColor.Get(), m_depthIn.Get(), halfW, halfH, sp,
                                     D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);
        }

        // Live GPU 256-Bin Log-Luminance Histogram & Eye Adaptation (RawAutoExposureHost — 12th Live GPU Solver!)
        if (m_autoExposureOk) {
            AutoExposureParams aep{};
            aep.dtSec          = dtClamped;
            aep.minLogLum      = -10.0f;
            aep.maxLogLum      =  14.0f;
            aep.keyValue       =   0.18f;
            aep.tauBrightSec   =   0.35f;
            aep.tauDarkSec     =   1.40f;
            aep.lowPercentile  =   0.15f;
            aep.highPercentile =   0.95f;
            aep.resetAdapt     = (m_frameCount == 0ull);
            m_autoExposureRan  = m_autoExposure.Execute(m_dispCmd.Get(), m_hdrColor.Get(), m_width, m_height, aep,
                                                        D3D12_RESOURCE_STATE_COMMON);
        }

        // Live GPU 3D Volumetric Cloud Raymarcher + Temporal Reprojection (RawCloudRaymarchHost - 13th Live GPU Solver!)
        if (m_cloudRaymarchOk) {
            CloudRaymarchParams crp{};
            // Convert viewer Z-up Skyrim units (70u = 1m) cleanly to Y-up meters for spherical cloud shell
            crp.camPos[0]       = m_camPos[0] / 70.0f;
            crp.camPos[1]       = std::max(50.0f, m_camPos[2] / 70.0f + 150.0f);
            crp.camPos[2]       = m_camPos[1] / 70.0f;
            crp.camRight[0]     = cam.right[0]; crp.camRight[1]   = cam.right[2]; crp.camRight[2]   = cam.right[1];
            crp.camUp[0]        = cam.up[0];    crp.camUp[1]      = cam.up[2];    crp.camUp[2]      = cam.up[1];
            crp.camForward[0]   = cam.fwd[0];   crp.camForward[1] = cam.fwd[2];   crp.camForward[2] = cam.fwd[1];
            crp.fovYDeg         = 1.12f * (180.0f / 3.14159265f);
            crp.timeSec         = m_simTimeSec;
            crp.cloudCoverage   = std::clamp(u.clouds[0], 0.15f, 0.92f);
            crp.densityScale    = 0.0022f;
            crp.anvilFactor     = 0.35f;
            crp.historyWeight   = 0.75f;
            crp.windVel[0]      = wGpu.cloudWind.x * 0.4f;
            crp.windVel[1]      = 0.0f;
            crp.windVel[2]      = wGpu.cloudWind.y * 0.4f;
            crp.sunDir[0]       = u.sunDir[0]; crp.sunDir[1] = u.sunDir[2]; crp.sunDir[2] = u.sunDir[1];
            crp.sunColor[0]     = u.sunColour[0]; crp.sunColor[1] = u.sunColour[1]; crp.sunColor[2] = u.sunColour[2];
            crp.sunIntensity    = (m_timePreset == TimePreset::MidnightAurora) ? 1.4f : 6.5f;
            crp.frameIndex      = static_cast<uint32_t>(m_frameCount & 0xFFFFFFFFull);
            const uint32_t halfW = std::max(1u, m_width / 2u);
            const uint32_t halfH = std::max(1u, m_height / 2u);
            m_cloudRaymarchRan  = m_cloudRaymarch.Execute(m_dispCmd.Get(), halfW, halfH, crp);
        }

        // Live GPU Cinematic Post-Process: Hexagonal Bokeh DoF + Motion Blur + Vignette (RawPostCinematicHost - 14th Live GPU Solver!)
        if (m_postCinematicOk) {
            PostCinematicParams pcp{};
            pcp.nearZ             = nearZ;
            pcp.farZ              = farZ;
            pcp.focusDistanceM    = m_lastRayHit.hit ? std::clamp(m_lastRayHit.distance, 60.0f, 8000.0f) : 950.0f;
            pcp.focalLengthMm     = 50.0f;
            pcp.apertureFStop     = 2.2f;
            pcp.cocScale          = 1.25f;
            pcp.dofStrength       = 0.85f;
            pcp.maxBokehRadiusPx  = 8.0f;
            pcp.motionBlurScale   = 0.85f;
            pcp.maxMotionRadiusPx = 18.0f;
            pcp.vignetteStrength  = 0.16f;
            pcp.frameJitter       = static_cast<float>(m_frameCount & 63ull);
            const uint32_t halfW  = std::max(1u, m_width / 2u);
            const uint32_t halfH  = std::max(1u, m_height / 2u);
            m_postCinematicRan    = m_postCinematic.Execute(
                m_dispCmd.Get(), m_hdrColor.Get(), m_depthIn.Get(), nullptr, halfW, halfH, pcp,
                D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);
        }

        // Live GPU 16,384-Particle 3D Physics & Screen-Space Splat (RawGpuParticlesHost - 15th Live GPU Solver!)
        if (m_gpuParticlesOk) {
            GpuParticlesParams gpp{};
            const float eyeM[3]    = {m_camPos[0] / 70.0f, m_camPos[2] / 70.0f, m_camPos[1] / 70.0f};
            const float targetM[3] = {
                eyeM[0] + cam.fwd[0] * 10.0f,
                eyeM[1] + cam.fwd[2] * 10.0f,
                eyeM[2] + cam.fwd[1] * 10.0f
            };
            const float upM[3] = {cam.up[0], cam.up[2], cam.up[1]};
            BuildParticlesViewProjLH(eyeM, targetM, upM, 1.12f,
                                     float(m_width) / float(std::max(1u, m_height)),
                                     0.2f, 250.0f, gpp.viewProj);
            gpp.emitterPos[0] = eyeM[0] + cam.fwd[0] * 4.5f;
            gpp.emitterPos[1] = eyeM[1] + cam.fwd[2] * 4.5f + 0.8f;
            gpp.emitterPos[2] = eyeM[2] + cam.fwd[1] * 4.5f;
            gpp.emitterRadius = 2.4f;
            gpp.windVel[0]    = std::clamp(wGpu.cloudWind.x * 0.12f, -12.0f, 12.0f);
            gpp.windVel[1]    = 0.0f;
            gpp.windVel[2]    = std::clamp(wGpu.cloudWind.y * 0.12f, -12.0f, 12.0f);
            gpp.curlStrength  = 1.4f;
            gpp.dtSec         = dtClamped;
            gpp.timeSec       = m_simTimeSec;
            float groundU     = m_camPos[2] - 200.0f;
            (void)activePhys.SampleTerrainHeight(m_camPos[0], m_camPos[1], &groundU);
            gpp.groundY       = groundU / 70.0f;
            gpp.restitution   = 0.62f;
            gpp.frameIndex    = static_cast<uint32_t>(m_frameCount & 0xFFFFFFFFull);
            gpp.resetAll      = (m_frameCount == 0ull);
            if (m_weatherPreset == WeatherPreset::Blizzard || u.weather[2] > 0.35f) {
                gpp.mode = ParticleMode::Snow;
            } else if (m_weatherPreset == WeatherPreset::Rainstorm || u.weather[3] > 0.25f) {
                gpp.mode = ParticleMode::Rain;
            } else if (m_mode == WorldMode::Dungeon || (m_season == SeasonChoice::Spring && !m_fireEnabled)) {
                gpp.mode = ParticleMode::WaterfallMist;
            } else {
                gpp.mode = ParticleMode::Embers;
            }
            m_gpuParticlesRan = m_gpuParticles.Execute(m_dispCmd.Get(), gpp);
        }

        // Live GPU RAW-TSR Temporal Super-Resolution + RCAS Sharpening (RawTsrUpscaleHost - 16th Live GPU Solver!)
        if (m_tsrUpscaleOk) {
            TsrUpscaleParams tp{};
            ComputeHaltonJitter(static_cast<uint32_t>(m_frameCount & 0xFFFFFFFFull), tp.jitterX, tp.jitterY);
            tp.nearZ         = nearZ;
            tp.farZ          = farZ;
            tp.historyWeight = 0.88f;
            tp.varianceGamma = 1.15f;
            tp.rcasSharpness = 0.65f;
            tp.resetHistory  = (m_frameCount == 0ull);
            const uint32_t tsrOutW = std::min(m_width * 2u, 1920u);
            const uint32_t tsrOutH = std::min(m_height * 2u, 1080u);
            m_tsrUpscaleRan = m_tsrUpscale.Execute(
                m_dispCmd.Get(), m_hdrColor.Get(), m_depthIn.Get(), nullptr, nullptr,
                m_width, m_height, tsrOutW, tsrOutH, tp,
                D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);
        }

        // Live GPU Physical Water Caustics & Underwater Volumetric God-Rays (RawWaterCausticsHost - 17th Live GPU Solver!)
        if (m_waterCausticsOk) {
            WaterCausticsParams wcp{};
            wcp.timeSec         = m_simTimeSec;
            const float sx      = u.sunDir[0];
            const float sy      = std::max(0.10f, u.sunDir[2]);
            const float sz      = u.sunDir[1];
            const float sl      = std::max(1.0e-4f, std::sqrt(sx * sx + sy * sy + sz * sz));
            wcp.sunDir[0]       = sx / sl; wcp.sunDir[1] = sy / sl; wcp.sunDir[2] = sz / sl;
            wcp.sunIntensity    = (m_timePreset == TimePreset::MidnightAurora) ? 0.35f : 1.0f;
            wcp.sunColor[0]     = u.sunColour[0]; wcp.sunColor[1] = u.sunColour[1]; wcp.sunColor[2] = u.sunColour[2];
            const float waterZ  = (m_wildlifeWaterZ > -10000.0f) ? m_wildlifeWaterZ : (m_camPos[2] - 240.0f);
            wcp.bedDepthM       = std::clamp(std::abs(m_camPos[2] - waterZ) / 70.0f, 1.4f, 8.0f);
            wcp.waveAmp         = std::clamp(0.09f + wGpu.beaufortScale * 0.025f, 0.09f, 0.34f);
            wcp.domainSizeM     = 8.0f;
            wcp.dispersionScale = 1.0f;
            wcp.mieG            = 0.78f;
            wcp.camPos[0]       = m_camPos[0] / 70.0f;
            wcp.camPos[1]       = -std::min(wcp.bedDepthM * 0.75f, 3.0f);
            wcp.camPos[2]       = m_camPos[1] / 70.0f;
            wcp.waterSurfaceY   = 0.0f;
            wcp.camRight[0]     = cam.right[0]; wcp.camRight[1]   = cam.right[2]; wcp.camRight[2]   = cam.right[1];
            wcp.camUp[0]        = cam.up[0];    wcp.camUp[1]      = cam.up[2];    wcp.camUp[2]      = cam.up[1];
            wcp.camForward[0]   = cam.fwd[0];   wcp.camForward[1] = cam.fwd[2];   wcp.camForward[2] = cam.fwd[1];
            wcp.fovYDeg         = 1.12f * (180.0f / 3.14159265f);
            wcp.maxRayDistM     = 24.0f;
            wcp.shaftIntensity  = 2.2f;
            wcp.frameIndex      = static_cast<uint32_t>(m_frameCount & 0xFFFFFFFFull);
            m_waterCausticsRan  = m_waterCaustics.Execute(m_dispCmd.Get(), 512u, 512u, wcp);
        }

        if (m_radOk && m_giEnabled) {
            m_radianceHost.ExecuteScreenGI(m_dispCmd.Get(), m_hdrColor.Get(), m_depthIn.Get(),
                                           m_width, m_height, 0.16f);
        }
        m_dispCmd->SetPipelineState(m_agxPso.Get());

        AgxDisplayCB dcb{};
        FillAgxSkyConstants(dcb);
        dcb.skyHorizonFrame[3] = float(m_frameCount & 1023ull);
        std::memcpy(m_agxCbMapped + size_t(dispSlot) * 256u, &dcb, sizeof(dcb));

        D3D12_RESOURCE_BARRIER bDispIn[2] = {
            MakeBarrier(m_hdrColor.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            MakeBarrier(m_displayLdr.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET)
        };
        m_dispCmd->ResourceBarrier(2, bDispIn);

        D3D12_VIEWPORT vp{0.0f, 0.0f, float(m_width), float(m_height), 0.0f, 1.0f};
        D3D12_RECT sc{0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
        m_dispCmd->RSSetViewports(1, &vp);
        m_dispCmd->RSSetScissorRects(1, &sc);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = RtvHandle(kBackBufferCount + 1u);
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
            MakeBarrier(m_displayLdr.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON)
        };
        m_dispCmd->ResourceBarrier(2, bDispOut);

        // Live GPU Compute Telemetry & 64-Sample Sparkline HUD (RawOverlayHost, F9 / H toggleable)!
        if (m_overlayOk && m_hudEnabled) {
            OverlayHudData hud{};
            BuildOverlayHud(wGpu, hud);
            m_lastHudRect = ComputeOverlayPanelRect(hud, m_width, m_height);
            m_overlayHost.RecordOverlay(m_dispCmd.Get(), m_displayLdr.Get(), D3D12_RESOURCE_STATE_COMMON,
                                        m_width, m_height, hud);
        }

        // In interactive SwapChain mode, copy m_displayLdr -> current SwapChain backbuffer cleanly!
        if (m_swapChain) {
            const UINT bbIdx = m_swapChain->GetCurrentBackBufferIndex();
            ID3D12Resource* bb = m_backBuffers[bbIdx].Get();
            D3D12_RESOURCE_BARRIER bCopyIn[2] = {
                MakeBarrier(m_displayLdr.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
                MakeBarrier(bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST)
            };
            m_dispCmd->ResourceBarrier(2, bCopyIn);
            m_dispCmd->CopyResource(bb, m_displayLdr.Get());
            D3D12_RESOURCE_BARRIER bCopyOut[2] = {
                MakeBarrier(m_displayLdr.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
                MakeBarrier(bb, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT)
            };
            m_dispCmd->ResourceBarrier(2, bCopyOut);
        }

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
        bool     xpbdActive       = false;
        float    xpbdStepMs       = 0.0f;
        uint32_t xpbdParticles    = 0u;
        bool     overlayActive    = false;
        float    overlayMs        = 0.0f;
        uint32_t hudNonZeroPx     = 0u;
        bool     gpuCullActive        = false;
        float    gpuHiZMs             = 0.0f;
        float    gpuCullMs            = 0.0f;
        uint32_t gpuHiZMips           = 0u;
        uint32_t gpuCullInstances     = 0u;
        bool     lightClustersActive  = false;
        float    lightClusterBuildMs  = 0.0f;
        float    lightClusterAssignMs = 0.0f;
        uint32_t lightClusterCount    = 0u;
        bool     volFroxelsActive     = false;
        float    froxelInjectMs       = 0.0f;
        float    froxelIntegrateMs    = 0.0f;
        bool     shadowPcssActive     = false;
        float    shadowPcssMs         = 0.0f;
        bool     gtaoActive           = false;
        float    gtaoMainMs           = 0.0f;
        float    gtaoDenoiseMs        = 0.0f;
        bool     ssrActive            = false;
        float    ssrMs                = 0.0f;
        bool     autoExposureActive   = false;
        float    autoExpHistMs        = 0.0f;
        float    autoExpAdaptMs       = 0.0f;
        bool     cloudRaymarchActive  = false;
        float    cloudRaymarchMs      = 0.0f;
        bool     postCinematicActive  = false;
        float    postCinematicMs      = 0.0f;
        bool     gpuParticlesActive   = false;
        float    gpuParticlesMs       = 0.0f;
        uint32_t gpuParticleCount     = 0u;
        bool     tsrUpscaleActive     = false;
        float    tsrUpscaleMs         = 0.0f;
        uint32_t tsrOutWidth          = 0u;
        uint32_t tsrOutHeight         = 0u;
        bool     waterCausticsActive  = false;
        float    waterCausticsMs      = 0.0f;
        uint32_t biomesFound          = 0u;
    };

    bool ReadbackHeadlessAudit(HeadlessAudit* out)
    {
        if (!out || !m_displayLdr || !m_hdrColor) return false;
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
        out->xpbdActive = m_xpbdOk && m_xpbdUploaded;
        out->xpbdStepMs = m_xpbdSim.HasValidTiming() ? m_xpbdSim.GetLastStepMs() : 0.0f;
        out->xpbdParticles = m_xpbdSim.ParticleCount();
        out->overlayActive = m_overlayOk && m_hudEnabled;
        out->overlayMs = m_overlayHost.HasValidTiming() ? m_overlayHost.GetLastOverlayMs() : 0.0f;
        out->gpuCullActive = m_gpuCullOk && m_gpuCull.Ready() && m_lastCullInstCount > 0u;
        out->gpuHiZMs = m_gpuCull.HasValidHiZTiming() ? m_gpuCull.GetLastHiZMs() : 0.0f;
        out->gpuCullMs = m_gpuCull.HasValidCullTiming() ? m_gpuCull.GetLastCullMs() : 0.0f;
        out->gpuHiZMips = m_gpuCull.HiZMipCount();
        out->gpuCullInstances = m_lastCullInstCount;
        out->lightClustersActive = m_lightClustersOk && m_lightClusters.Ready() && m_lastClusterLightCount > 0u;
        out->lightClusterBuildMs = m_lightClusters.HasValidTiming() ? m_lightClusters.GetLastBuildMs() : 0.0f;
        out->lightClusterAssignMs = m_lightClusters.HasValidTiming() ? m_lightClusters.GetLastAssignMs() : 0.0f;
        out->lightClusterCount = m_lastClusterLightCount;
        out->volFroxelsActive = m_volFroxelsOk && m_volFroxels.Ready() && m_hasPrevFroxelView;
        out->froxelInjectMs = m_volFroxels.HasValidTiming() ? m_volFroxels.GetLastInjectMs() : 0.0f;
        out->froxelIntegrateMs = m_volFroxels.HasValidTiming() ? m_volFroxels.GetLastIntegrateMs() : 0.0f;
        out->shadowPcssActive = m_shadowPcssOk && m_shadowPcss.Ready() && m_shadowPcssRan;
        out->shadowPcssMs = m_shadowPcss.HasValidTiming() ? m_shadowPcss.GetLastPcssMs() : 0.0f;
        out->gtaoActive = m_gtaoOk && m_gtao.Ready() && m_gtaoRan;
        out->gtaoMainMs = m_gtao.HasValidTiming() ? m_gtao.GetLastMainMs() : 0.0f;
        out->gtaoDenoiseMs = m_gtao.HasValidTiming() ? m_gtao.GetLastDenoiseMs() : 0.0f;
        out->ssrActive = m_ssrOk && m_ssr.Ready() && m_ssrRan;
        out->ssrMs = m_ssr.HasValidTiming() ? m_ssr.GetLastSsrMs() : 0.0f;
        out->autoExposureActive = m_autoExposureOk && m_autoExposure.Ready() && m_autoExposureRan;
        out->autoExpHistMs = m_autoExposure.HasValidTiming() ? m_autoExposure.GetLastHistMs() : 0.0f;
        out->autoExpAdaptMs = m_autoExposure.HasValidTiming() ? m_autoExposure.GetLastAdaptMs() : 0.0f;
        out->cloudRaymarchActive = m_cloudRaymarchOk && m_cloudRaymarch.Ready() && m_cloudRaymarchRan;
        out->cloudRaymarchMs = m_cloudRaymarch.HasValidTiming() ? m_cloudRaymarch.GetLastCloudMs() : 0.0f;
        out->postCinematicActive = m_postCinematicOk && m_postCinematic.Ready() && m_postCinematicRan;
        out->postCinematicMs = m_postCinematic.HasValidTiming() ? m_postCinematic.GetLastTotalMs() : 0.0f;
        out->gpuParticlesActive = m_gpuParticlesOk && m_gpuParticles.Ready() && m_gpuParticlesRan;
        out->gpuParticlesMs = m_gpuParticles.HasValidTiming() ? m_gpuParticles.GetLastTotalMs() : 0.0f;
        out->gpuParticleCount = m_gpuParticles.ParticleCount();
        out->tsrUpscaleActive = m_tsrUpscaleOk && m_tsrUpscale.Ready() && m_tsrUpscaleRan;
        out->tsrUpscaleMs = m_tsrUpscale.HasValidTiming() ? m_tsrUpscale.GetLastTotalMs() : 0.0f;
        out->tsrOutWidth = m_tsrUpscale.OutputWidth();
        out->tsrOutHeight = m_tsrUpscale.OutputHeight();
        out->waterCausticsActive = m_waterCausticsOk && m_waterCaustics.Ready() && m_waterCausticsRan;
        out->waterCausticsMs = m_waterCaustics.HasValidTiming() ? m_waterCaustics.GetLastTotalMs() : 0.0f;

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
            MakeBarrier(m_displayLdr.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE)
        };
        m_cmd->ResourceBarrier(2, bIn);

        D3D12_TEXTURE_COPY_LOCATION hDst{}; hDst.pResource = rbHdr.Get(); hDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        hDst.PlacedFootprint.Footprint = {DXGI_FORMAT_R16G16B16A16_FLOAT, m_width, m_height, 1, hdrPitch};
        D3D12_TEXTURE_COPY_LOCATION hSrc{}; hSrc.pResource = m_hdrColor.Get(); hSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        m_cmd->CopyTextureRegion(&hDst, 0, 0, 0, &hSrc, nullptr);

        D3D12_TEXTURE_COPY_LOCATION dDst{}; dDst.pResource = rbDisp.Get(); dDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dDst.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, m_width, m_height, 1, dispPitch};
        D3D12_TEXTURE_COPY_LOCATION dSrc{}; dSrc.pResource = m_displayLdr.Get(); dSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        m_cmd->CopyTextureRegion(&dDst, 0, 0, 0, &dSrc, nullptr);

        D3D12_RESOURCE_BARRIER bOut[2] = {
            MakeBarrier(m_hdrColor.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
            MakeBarrier(m_displayLdr.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON)
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
                if (row[x * 4u + 0u] || row[x * 4u + 1u] || row[x * 4u + 2u]) {
                    ++out->dispNonZeroPx;
                    if (x >= m_lastHudRect.x && x < m_lastHudRect.x + m_lastHudRect.w &&
                        y >= m_lastHudRect.y && y < m_lastHudRect.y + m_lastHudRect.h) {
                        ++out->hudNonZeroPx;
                    }
                }
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
        m_navMesh.Clear();
        m_tacticalSquad.clear();
        m_lastSquadOut.clear();
        m_dungeonPhysics.Clear();
        m_worldPhysics.Clear();

        m_frontierChunks.clear();
        m_esmChunks.clear();
        m_dungeonChunks.clear();
        if (m_cullInstUpload && m_cullInstMapped) {
            m_cullInstUpload->Unmap(0u, nullptr);
            m_cullInstMapped = nullptr;
        }
        m_cullInstUpload.Reset();
        m_cullInstBuf.Reset();
        m_waterCaustics.Shutdown();
        m_craftingEngine.Clear();
        m_tsrUpscale.Shutdown();
        m_questStory.Clear();
        m_gpuParticles.Shutdown();
        m_combatSandbox.Clear();
        m_combatActive = false;
        m_postCinematic.Shutdown();
        m_squadPoses.clear();
        m_squadAnchors.clear();
        m_playerRigEvaluated = false;
        m_cloudRaymarch.Shutdown();
        m_autoExposure.Shutdown();
        m_ssr.Shutdown();
        m_gtao.Shutdown();
        m_shadowPcss.Shutdown();
        m_shadowOutTex.Reset();
        m_shadowMapProxy.Reset();
        m_volFroxels.Shutdown();
        m_lightClusters.Shutdown();
        m_gpuCull.Shutdown();
        m_wildlife.Clear();
        m_eventDirector.Reset();
        m_diplomacy.Reset();
        m_overlayHost.Shutdown();
        m_xpbdSim.Shutdown();
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
        m_displayLdr.Reset();
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
        } else if (m_mode == WorldMode::Dungeon) {
            std::snprintf(locBuf, sizeof(locBuf), "Dungeon:%s (%zu rms, %zu L)",
                          raw::worldgen::GetDungeonArchetypeName(m_activeDungeon.archetype),
                          m_activeDungeon.rooms.size(), m_activeDungeon.lights.size());
        } else {
            std::snprintf(locBuf, sizeof(locBuf), "%s (%u cells, %llu LRU hits)",
                          kTamrielLandmarks[m_landmarkIdx % 4u].name,
                          m_lastTamrielStats.activeCells,
                          static_cast<unsigned long long>(m_lastTamrielStats.cacheHits));
        }
        char hitBuf[64]{};
        if (m_lastRayHit.hit) {
            std::snprintf(hitBuf, sizeof(hitBuf), " | Hit:%.0fu(%s)",
                          m_lastRayHit.distance, m_lastRayHit.hitTerrain ? "Terrain" : "NIF");
        }
        char navBuf[80]{};
        if (m_lastNavPath.found && !m_lastNavPath.waypoints.empty()) {
            std::snprintf(navBuf, sizeof(navBuf), " | Nav:%zuwp(%.0fu,%.2fms)",
                          m_lastNavPath.waypoints.size(), m_lastNavPath.totalLengthU, m_lastNavPath.searchTimeMs);
        } else if (m_navMesh.IsBaked()) {
            std::snprintf(navBuf, sizeof(navBuf), " | Nav:%uw", m_navMesh.GetStats().walkableCells);
        }
        const float cullTotalMs = (m_gpuCull.HasValidHiZTiming() ? m_gpuCull.GetLastHiZMs() : 0.0f) +
                                  (m_gpuCull.HasValidCullTiming() ? m_gpuCull.GetLastCullMs() : 0.0f);
        const float lclustTotalMs = m_lightClusters.HasValidTiming()
                                  ? (m_lightClusters.GetLastBuildMs() + m_lightClusters.GetLastAssignMs())
                                  : 0.0f;
        const float froxTotalMs   = m_volFroxels.HasValidTiming()
                                  ? (m_volFroxels.GetLastInjectMs() + m_volFroxels.GetLastIntegrateMs())
                                  : 0.0f;
        const float pcssMs        = m_shadowPcss.HasValidTiming() ? m_shadowPcss.GetLastPcssMs() : 0.0f;
        const float gtaoMs        = m_gtao.HasValidTiming() ? (m_gtao.GetLastMainMs() + m_gtao.GetLastDenoiseMs()) : 0.0f;
        const float ssrMs         = m_ssr.HasValidTiming() ? m_ssr.GetLastSsrMs() : 0.0f;
        const float aeMs          = m_autoExposure.HasValidTiming() ? (m_autoExposure.GetLastHistMs() + m_autoExposure.GetLastAdaptMs()) : 0.0f;
        const float cloudMs       = m_cloudRaymarch.HasValidTiming() ? m_cloudRaymarch.GetLastCloudMs() : 0.0f;
        const float postMs        = m_postCinematic.HasValidTiming() ? m_postCinematic.GetLastTotalMs() : 0.0f;
        const float prtMs         = m_gpuParticles.HasValidTiming() ? m_gpuParticles.GetLastTotalMs() : 0.0f;
        const float tsrMs         = m_tsrUpscale.HasValidTiming() ? m_tsrUpscale.GetLastTotalMs() : 0.0f;
        const float cauMs         = m_waterCaustics.HasValidTiming() ? m_waterCaustics.GetLastTotalMs() : 0.0f;
        const auto activeHid      = static_cast<raw::worldgen::HoldId>(m_activeHoldIdx % static_cast<uint32_t>(raw::worldgen::kHoldCount));
        const auto* activeHold    = m_settlementSim.GetHold(activeHid);
        const float holdGrain     = activeHold ? activeHold->price[static_cast<size_t>(raw::worldgen::CommodityKind::Grain)] : 0.0f;
        const auto dipHid         = static_cast<raw::worldgen::HoldId>(m_diplomacyHoldIdx % static_cast<uint32_t>(raw::worldgen::kHoldCount));
        const auto* dipStand      = m_diplomacy.GetHoldStanding(dipHid);
        char dipBuf[96]{};
        std::snprintf(dipBuf, sizeof(dipBuf), " | Dip:%s(%+.0f,%.0fg,%s,x%.2f)",
                      raw::worldgen::GetHoldName(dipHid),
                      dipStand ? static_cast<double>(dipStand->reputation) : 0.0,
                      dipStand ? static_cast<double>(dipStand->bountyGold) : 0.0,
                      dipStand ? raw::worldgen::GetGuardPostureName(dipStand->guardPosture) : "Peaceful",
                      dipStand ? static_cast<double>(dipStand->merchantPriceMult) : 1.0);
        char saveBuf[32]{};
        if (!m_lastSaveStatus.empty()) {
            std::snprintf(saveBuf, sizeof(saveBuf), " | [%s]", m_lastSaveStatus.c_str());
        }
        char evBuf[96]{};
        if (!m_eventDirector.ActiveEvents().empty()) {
            const auto& ev0 = m_eventDirector.ActiveEvents()[0];
            std::snprintf(evBuf, sizeof(evBuf), " | Ev:%u(%s@%s)",
                          m_eventTelemetry.activeCount,
                          raw::worldgen::GetWorldEventKindName(ev0.kind),
                          raw::worldgen::GetHoldName(ev0.holdId));
        } else {
            std::snprintf(evBuf, sizeof(evBuf), " | Ev:0");
        }
        char buf[1536]{};
        std::snprintf(buf, sizeof(buf),
                      "RAW Engine [%s] | %.1f FPS | %u Meshlets (%s) | %s | %s | %s | Fire:%.2fms Water:%.2fms GI:%.2fms XPBD:%.3fms Cull:%.3fms LClust:%.3fms Frox:%.3fms PCSS:%.3fms GTAO:%.3fms SSR:%.3fms AE:%.3fms Cloud:%.3fms POST:%.3fms PRT:%.3fms TSR:%.3fms CAU:%.3fms HUD:%.3fms | IK:%.1fu Cbt:%u/%u(P:%u) Qst:%uA/%uC(%.0fg) Frg:%.0f/%.0f | Eco:fire=%u snow=%.0fm3 ice=%u | Fauna:%u roost=%u startle=%u | %s:%.1fg Car:%u(Snw:%u) Scarce:%s%s%s%s%s%s | Shards:%u",
                      m_walkMode ? "WALK" : "FLY",
                      fps, m_lastDrawnMeshlets, m_meshHost.TerrainUsesMeshShaders() ? "AS+MS" : "VS+PS",
                      locBuf, TimePresetName(m_timePreset), ActiveWeatherDisplayName(),
                      ft.valid ? ft.fireStepMs : 0.0f,
                      ft.valid ? ft.waterStepMs : 0.0f,
                      rt.valid ? rt.giPassMs : 0.0f,
                      m_xpbdSim.HasValidTiming() ? m_xpbdSim.GetLastStepMs() : 0.0f,
                      cullTotalMs,
                      lclustTotalMs,
                      froxTotalMs,
                      pcssMs,
                      gtaoMs,
                      ssrMs,
                      aeMs,
                      cloudMs,
                      postMs,
                      prtMs,
                      tsrMs,
                      cauMs,
                      m_overlayHost.HasValidTiming() ? m_overlayHost.GetLastOverlayMs() : 0.0f,
                      static_cast<double>(m_playerPose.pelvisDropZ),
                      m_combatTelemetry.aliveAllies,
                      m_combatTelemetry.aliveHostiles,
                      m_combatTelemetry.totalParries,
                      m_questTelemetry.activeCount,
                      m_questTelemetry.completedCount,
                      m_questTelemetry.totalGoldAwarded,
                      static_cast<double>(m_equippedStats.weaponDamage),
                      static_cast<double>(m_equippedStats.totalArmorRating),
                      m_ecoTelemetry.activeFireCells,
                      static_cast<double>(m_ecoTelemetry.totalSnowVolumeM3),
                      m_ecoTelemetry.blackIceCells,
                      m_wildlifeTelemetry.activeAgents,
                      m_wildlifeTelemetry.roostingCount,
                      m_wildlifeTelemetry.startledCount,
                      raw::worldgen::GetHoldName(activeHid),
                      static_cast<double>(holdGrain),
                      m_settlementTelemetry.totalCaravansActive,
                      m_settlementTelemetry.snowboundCaravans,
                      raw::worldgen::GetHoldName(m_settlementTelemetry.mostScarceHold),
                      dipBuf, evBuf, saveBuf, hitBuf, navBuf,
                      m_activeShards);
        return buf;
    }

    const std::string& AdapterName() const { return m_adapterName; }

private:
    static constexpr uint64_t kDebrisChunkKey  = 0xDEBB1500ull;
    static constexpr uint64_t kDungeonChunkKey = 0xD006E001ull;

    std::unordered_map<uint64_t, TerrainGpuChunk>& ActiveChunkMap()
    {
        if (m_mode == WorldMode::Dungeon) return m_dungeonChunks;
        return (m_mode == WorldMode::Frontier) ? m_frontierChunks : m_esmChunks;
    }

    raw::worldgen::RawWorldPhysics& ActivePhysics()
    {
        return (m_mode == WorldMode::Dungeon) ? m_dungeonPhysics : m_worldPhysics;
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

        // RTV heap: 3 swapchain backbuffers + 1 HDR colorOut clear RTV + 1 UAV-capable displayLdr RTV
        D3D12_DESCRIPTOR_HEAP_DESC rtvD{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kBackBufferCount + 2u, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
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
        }
        // UAV + RT capable R8G8B8A8_UNORM intermediate display target (receives RawAgxDisplay + RawOverlayHost HUD!)
        D3D12_RESOURCE_DESC dispDesc{};
        dispDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        dispDesc.Width = w; dispDesc.Height = h; dispDesc.DepthOrArraySize = 1; dispDesc.MipLevels = 1;
        dispDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dispDesc.SampleDesc.Count = 1;
        dispDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (FAILED(m_dev->CreateCommittedResource(&defHp, D3D12_HEAP_FLAG_NONE, &dispDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_displayLdr)))) {
            return false;
        }
        m_dev->CreateRenderTargetView(m_displayLdr.Get(), nullptr, RtvHandle(kBackBufferCount + 1u));

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

        // Owned half-res R16G16_FLOAT PCSS shadow visibility + penumbra output UAV (W/2 x H/2)
        D3D12_RESOURCE_DESC pcssOutDesc{};
        pcssOutDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        pcssOutDesc.Width            = std::max(1u, w / 2u);
        pcssOutDesc.Height           = std::max(1u, h / 2u);
        pcssOutDesc.DepthOrArraySize = 1u;
        pcssOutDesc.MipLevels        = 1u;
        pcssOutDesc.Format           = DXGI_FORMAT_R16G16_FLOAT;
        pcssOutDesc.SampleDesc.Count = 1u;
        pcssOutDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (FAILED(m_dev->CreateCommittedResource(&defHp, D3D12_HEAP_FLAG_NONE, &pcssOutDesc,
                                                  D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_shadowOutTex)))) {
            return false;
        }

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
                auto hf = raw::worldgen::MakeHeightfieldFromTerrainChunk(entry.terrain, sc.terrain.seaLevelZ, 3u);
                hf.cellKey = raw::worldgen::PackCellKey(entry.key.x, entry.key.y);
                v->m_worldPhysics.AddTerrainHeightfield(hf);
                if (!v->m_navMesh.IsBaked()) v->m_navMeshDirty = true;
                if (!v->m_ecoInitialized) {
                    v->m_ecosystem.InitFromTerrainChunk(entry.terrain, 70.0f, sc.terrain.seaLevelZ);
                    v->m_ecoInitialized = true;
                    v->m_ecoTelemetry   = v->m_ecosystem.ComputeTelemetry();
                }
                if (!v->m_wildlifeInitialized) {
                    v->SpawnWildlifeAround(v->m_frontierCenter[0], v->m_frontierCenter[1],
                                           v->m_frontierGroundZ, sc.terrain.seaLevelZ);
                }
                return h;
            }
            void OnChunkEvicted(raw::worldgen::ChunkKey key, uint64_t h) override {
                v->m_worldPhysics.RemoveCellCollider(raw::worldgen::PackCellKey(key.x, key.y));
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

            // Register streamed Tamriel terrain heightfield + baked .NIF prop BVH into RawWorldPhysics!
            const uint64_t physKey = raw::worldgen::PackCellKey(cell.key.x, cell.key.y);
            auto hf = raw::worldgen::MakeHeightfieldFromTerrainChunk(cell.terrainChunk, cell.waterHeightZ, 1u);
            hf.cellKey = physKey;
            v->m_worldPhysics.AddTerrainHeightfield(hf);
            if (!cell.propsMesh.meshlets.empty()) {
                v->m_worldPhysics.AddCellCollider(physKey, cell.propsMesh, cell.cellFormId, 2u);
            }
            return cellHandle;
        }
        void OnTamrielCellEvicted(raw::worldgen::ChunkKey key, uint64_t gpuHandle) override {
            v->m_worldPhysics.RemoveCellCollider(raw::worldgen::PackCellKey(key.x, key.y));
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
        const bool ok = m_tamrielStreamer.Init(m_opt.esmPath, std::filesystem::exists(bsa) ? bsa.string() : std::string(), tcfg, &err);
        if (ok && !m_esmMetaLoaded) {
            std::string metaErr;
            if (raw::worldgen::LoadEsmWorldMeta(m_opt.esmPath, &m_esmWorldMeta, &metaErr)) {
                m_esmMetaLoaded = true;
                m_esmWeatherCycle.clear();
                static constexpr const char* kPreferredWeathers[5] = {
                    "SkyrimClear", "SkyrimCloudy", "SkyrimFog", "SkyrimStormRain", "SkyrimStormSnow"
                };
                for (const char* edid : kPreferredWeathers) {
                    if (const auto* w = m_esmWorldMeta.FindWeatherByEdid(edid)) {
                        m_esmWeatherCycle.push_back(w);
                    }
                }
                if (m_esmWeatherCycle.empty()) {
                    const size_t lim = std::min<size_t>(m_esmWorldMeta.weathers.size(), 5u);
                    for (size_t i = 0; i < lim; ++i) m_esmWeatherCycle.push_back(&m_esmWorldMeta.weathers[i]);
                }
                if (!m_esmWeatherCycle.empty()) {
                    m_esmWeatherIdx = 0u;
                    ApplyActiveEsmWeather();
                }
            }
        }
        return ok;
    }

    void ApplyActiveEsmWeather()
    {
        if (m_esmWeatherCycle.empty()) return;
        const auto* w = m_esmWeatherCycle[m_esmWeatherIdx % m_esmWeatherCycle.size()];
        if (!w) return;
        m_activeEsmWeatherEdid = w->editorId;
        m_activeEsmWeatherParams = raw::worldgen::ApplyEsmWeatherToSim(*w, m_weatherSim, TimePresetHours(m_timePreset), true);
        for (int i = 0; i < 8; ++i) {
            m_weatherSim.Step(1.0f, {m_camPos[0], m_camPos[1], m_camPos[2]});
        }
        if (m_audioEnabled && m_audio.IsRunning()) {
            PushAudioSceneState(m_weatherSim.GetGpuUniforms());
        }
    }

    void BuildOverlayHud(const raw::worldgen::WeatherGpuUniforms& wGpu, OverlayHudData& outHud) const
    {
        float histLinear[64]{};
        const uint32_t count = m_frameMsCount;
        const uint32_t start = (m_frameMsRingPos >= count) ? (m_frameMsRingPos - count) : 0u;
        for (uint32_t i = 0; i < count; ++i) {
            histLinear[i] = m_frameMsRing[(start + i) % 64u];
        }

        const auto ft = m_fluidOk ? m_fluidSim.GetLastTimings() : FluidGpuTimings{};
        const auto rt = m_radOk ? m_radianceHost.GetLastTimings() : RadianceGpuTimings{};

        char locLabel[64]{};
        if (m_mode == WorldMode::RiverwoodEsm) {
            std::snprintf(locLabel, sizeof(locLabel), "%s [%s]",
                          kTamrielLandmarks[m_landmarkIdx % 4u].name, m_walkMode ? "WALK" : "FLY");
        } else if (m_mode == WorldMode::Dungeon) {
            std::snprintf(locLabel, sizeof(locLabel), "DNG:%s [%s]",
                          raw::worldgen::GetDungeonArchetypeName(m_activeDungeon.archetype),
                          m_walkMode ? "WALK" : "FLY");
        } else {
            std::snprintf(locLabel, sizeof(locLabel), "%s [%s]",
                          raw::worldgen::GetBiomeName(static_cast<raw::worldgen::BiomeId>(m_biomeIdx)),
                          m_walkMode ? "WALK" : "FLY");
        }

        const uint64_t totalCacheLookups = m_lastTamrielStats.cacheHits + m_lastTamrielStats.cacheMisses;
        const float lruPct = (totalCacheLookups > 0ull)
                           ? (100.0f * float(m_lastTamrielStats.cacheHits) / float(totalCacheLookups))
                           : 0.0f;

        OverlayTelemetrySnapshot snap{};
        snap.frameMs        = m_lastFrameMs;
        snap.fps            = (m_lastFrameMs > 0.01f) ? (1000.0f / m_lastFrameMs) : 0.0f;
        snap.fireMs         = ft.valid ? ft.fireStepMs : 0.0f;
        snap.waterMs        = ft.valid ? ft.waterStepMs : 0.0f;
        snap.giMs           = rt.valid ? rt.giPassMs : 0.0f;
        snap.xpbdMs         = m_xpbdSim.HasValidTiming() ? m_xpbdSim.GetLastStepMs() : 0.0f;
        snap.overlayMs      = m_overlayHost.HasValidTiming() ? m_overlayHost.GetLastOverlayMs() : 0.0f;
        snap.seasonName     = SeasonName(m_season);
        snap.weatherName    = ActiveWeatherDisplayName();
        snap.tempC          = wGpu.seasonThermal0.w;
        snap.windMps        = wGpu.beaufortScale * 2.2f;
        snap.snowPackM      = wGpu.seasonThermal0.x;
        snap.cellX          = (m_mode == WorldMode::RiverwoodEsm) ? m_lastTamrielStats.camCellX : static_cast<int32_t>(std::floor(m_camPos[0] / 4096.0f));
        snap.cellY          = (m_mode == WorldMode::RiverwoodEsm) ? m_lastTamrielStats.camCellY : static_cast<int32_t>(std::floor(m_camPos[1] / 4096.0f));
        snap.landmarkName   = locLabel;
        snap.activeCells    = (m_mode == WorldMode::RiverwoodEsm) ? m_lastTamrielStats.activeCells : static_cast<uint32_t>(m_frontierChunks.size());
        snap.activeMeshlets = m_lastDrawnMeshlets;
        snap.lruHitRatePct  = lruPct;
        snap.giEnabled      = m_radOk && m_giEnabled;
        snap.fluidEnabled   = m_fluidOk && m_fireEnabled;
        snap.xpbdEnabled    = m_xpbdOk && m_xpbdUploaded;
        snap.historyFrameMs = histLinear;
        snap.historyCount   = count;

        FormatEngineTelemetryLines(snap, outHud);
        // Append Wave 50-52 live GPU Hi-Z Cull + 3D Light Clusters + Volumetric Froxels on lines 2-3, Ecosystem + Fauna on line 6, NavMesh on line 7!
        const float hizMs    = m_gpuCull.HasValidHiZTiming() ? m_gpuCull.GetLastHiZMs() : 0.0f;
        const float cullMs   = m_gpuCull.HasValidCullTiming() ? m_gpuCull.GetLastCullMs() : 0.0f;
        const float lclustMs = m_lightClusters.HasValidTiming()
                             ? (m_lightClusters.GetLastBuildMs() + m_lightClusters.GetLastAssignMs())
                             : 0.0f;
        const float froxMs   = m_volFroxels.HasValidTiming()
                             ? (m_volFroxels.GetLastInjectMs() + m_volFroxels.GetLastIntegrateMs())
                             : 0.0f;
        const float pcssMs   = m_shadowPcss.HasValidTiming() ? m_shadowPcss.GetLastPcssMs() : 0.0f;
        const float gtaoMs   = m_gtao.HasValidTiming() ? (m_gtao.GetLastMainMs() + m_gtao.GetLastDenoiseMs()) : 0.0f;
        const float ssrMs    = m_ssr.HasValidTiming() ? m_ssr.GetLastSsrMs() : 0.0f;
        const float aeMs     = m_autoExposure.HasValidTiming() ? (m_autoExposure.GetLastHistMs() + m_autoExposure.GetLastAdaptMs()) : 0.0f;
        const float cloudMs  = m_cloudRaymarch.HasValidTiming() ? m_cloudRaymarch.GetLastCloudMs() : 0.0f;
        const float postMs   = m_postCinematic.HasValidTiming() ? m_postCinematic.GetLastTotalMs() : 0.0f;
        const float prtMs    = m_gpuParticles.HasValidTiming() ? m_gpuParticles.GetLastTotalMs() : 0.0f;
        const float tsrMs    = m_tsrUpscale.HasValidTiming() ? m_tsrUpscale.GetLastTotalMs() : 0.0f;
        const float cauMs    = m_waterCaustics.HasValidTiming() ? m_waterCaustics.GetLastTotalMs() : 0.0f;
        std::memset(outHud.lines[2], 0, sizeof(outHud.lines[2]));
        std::snprintf(outHud.lines[2], sizeof(outHud.lines[2]), "GPU F:%.2f W:%.2f GI:%.2f XP:%.2f FX:%.2f PS:%.2f",
                      static_cast<double>(snap.fireMs), static_cast<double>(snap.waterMs),
                      static_cast<double>(snap.giMs), static_cast<double>(snap.xpbdMs),
                      static_cast<double>(froxMs), static_cast<double>(pcssMs));
        std::memset(outHud.lines[3], 0, sizeof(outHud.lines[3]));
        std::snprintf(outHud.lines[3], sizeof(outHud.lines[3]), "CD:%.2f PS:%.2f PT:%.2f TS:%.2f CAU:%.3f LC:%.2f",
                      static_cast<double>(cloudMs),
                      static_cast<double>(postMs),
                      static_cast<double>(prtMs),
                      static_cast<double>(tsrMs),
                      static_cast<double>(cauMs),
                      static_cast<double>(lclustMs + hizMs + cullMs + gtaoMs + ssrMs + aeMs));

        std::memset(outHud.lines[6], 0, sizeof(outHud.lines[6]));
        std::snprintf(outHud.lines[6], sizeof(outHud.lines[6]), "FRG D:%.0f A:%.0f E:%.2fx AL:%.0f%% | EV:%u F:%u",
                      static_cast<double>(m_equippedStats.weaponDamage),
                      static_cast<double>(m_equippedStats.totalArmorRating),
                      static_cast<double>(m_encumbrance.moveSpeedMult),
                      static_cast<double>(m_equippedStats.fortifyAlchemyPct),
                      m_eventTelemetry.activeCount,
                      m_ecoTelemetry.activeFireCells);
        outHud.lineColorTag[6] = (m_ecoTelemetry.activeFireCells > 0u || m_eventTelemetry.activeCount > 0u)
                               ? kOverlayColorAmber : kOverlayColorMuted;

        const auto dipHid    = static_cast<raw::worldgen::HoldId>(m_diplomacyHoldIdx % static_cast<uint32_t>(raw::worldgen::kHoldCount));
        const auto* dipSt    = m_diplomacy.GetHoldStanding(dipHid);
        const raw::worldgen::QuestInstance* activeQ = nullptr;
        for (const auto& q : m_questStory.Quests()) {
            if (q.lifecycle == raw::worldgen::QuestLifecycle::Active) { activeQ = &q; break; }
        }
        std::memset(outHud.lines[7], 0, sizeof(outHud.lines[7]));
        if (activeQ) {
            std::snprintf(outHud.lines[7], sizeof(outHud.lines[7]), "QST %uA/%uC %.18s S%u | %.0fg",
                          m_questTelemetry.activeCount,
                          m_questTelemetry.completedCount,
                          activeQ->title.c_str(),
                          activeQ->currentStageId,
                          m_questTelemetry.totalGoldAwarded);
        } else {
            std::snprintf(outHud.lines[7], sizeof(outHud.lines[7]), "DIP %s %+.0f X%.2f | Q:%uC %.0fg",
                          raw::worldgen::GetHoldName(dipHid),
                          dipSt ? static_cast<double>(dipSt->reputation) : 0.0,
                          dipSt ? static_cast<double>(dipSt->merchantPriceMult) : 1.0,
                          m_questTelemetry.completedCount,
                          m_questTelemetry.totalGoldAwarded);
        }
        outHud.lineColorTag[7] = kOverlayColorCyan;
        outHud.lineCount       = 8u;
        outHud.enabled    = m_hudEnabled;
        outHud.srgbOutput = true; // m_displayLdr holds sRGB-encoded AgX output in R8G8B8A8_UNORM
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

    bool CreateGpuCullInstanceBuffers()
    {
        const UINT64 instBytes = static_cast<UINT64>(kMaxCullInstances) * sizeof(GpuCullInstance);
        D3D12_HEAP_PROPERTIES defHp{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = (instBytes + 255ull) & ~255ull;
        rd.Height           = 1u;
        rd.DepthOrArraySize = 1u;
        rd.MipLevels        = 1u;
        rd.SampleDesc.Count = 1u;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(m_dev->CreateCommittedResource(&defHp, D3D12_HEAP_FLAG_NONE, &rd,
                                                  D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_cullInstBuf)))) {
            return false;
        }
        D3D12_HEAP_PROPERTIES upHp{D3D12_HEAP_TYPE_UPLOAD};
        rd.Width = static_cast<UINT64>(kRingFrames) * instBytes;
        if (FAILED(m_dev->CreateCommittedResource(&upHp, D3D12_HEAP_FLAG_NONE, &rd,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_cullInstUpload)))) {
            return false;
        }
        const D3D12_RANGE zeroR{0u, 0u};
        void* mapped = nullptr;
        if (FAILED(m_cullInstUpload->Map(0u, &zeroR, &mapped)) || !mapped) return false;
        m_cullInstMapped = static_cast<uint8_t*>(mapped);
        return true;
    }

    bool CreateShadowMapProxy()
    {
        D3D12_HEAP_PROPERTIES defHp{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_HEAP_PROPERTIES upHp{D3D12_HEAP_TYPE_UPLOAD};
        D3D12_RESOURCE_DESC sd{};
        sd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        sd.Width            = kShadowProxySize;
        sd.Height           = kShadowProxySize;
        sd.DepthOrArraySize = 1u;
        sd.MipLevels        = 1u;
        sd.Format           = DXGI_FORMAT_R32_FLOAT;
        sd.SampleDesc.Count = 1u;
        if (FAILED(m_dev->CreateCommittedResource(&defHp, D3D12_HEAP_FLAG_NONE, &sd,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_shadowMapProxy)))) {
            return false;
        }
        const UINT pitch = (kShadowProxySize * 4u + 255u) & ~255u;
        D3D12_RESOURCE_DESC ud{};
        ud.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        ud.Width            = static_cast<UINT64>(pitch) * kShadowProxySize;
        ud.Height           = 1u;
        ud.DepthOrArraySize = 1u;
        ud.MipLevels        = 1u;
        ud.SampleDesc.Count = 1u;
        ud.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> upBuf;
        if (FAILED(m_dev->CreateCommittedResource(&upHp, D3D12_HEAP_FLAG_NONE, &ud,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upBuf)))) {
            return false;
        }
        void* ptr = nullptr;
        if (FAILED(upBuf->Map(0u, nullptr, &ptr)) || !ptr) return false;
        for (UINT y = 0u; y < kShadowProxySize; ++y) {
            float* row = reinterpret_cast<float*>(static_cast<uint8_t*>(ptr) + static_cast<size_t>(y) * pitch);
            const float fy = static_cast<float>(y) / static_cast<float>(kShadowProxySize);
            for (UINT x = 0u; x < kShadowProxySize; ++x) {
                const float fx = static_cast<float>(x) / static_cast<float>(kShadowProxySize);
                const float ridge = 0.55f + 0.18f * std::sin(fx * 9.4247f) * std::cos(fy * 6.2832f);
                row[x] = std::clamp(ridge, 0.25f, 0.95f);
            }
        }
        upBuf->Unmap(0u, nullptr);

        WaitGpuIdle();
        if (FAILED(m_alloc[0]->Reset()) || FAILED(m_cmd->Reset(m_alloc[0].Get(), nullptr))) return false;
        D3D12_TEXTURE_COPY_LOCATION dDst{};
        dDst.pResource = m_shadowMapProxy.Get();
        dDst.Type      = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dSrc{};
        dSrc.pResource = upBuf.Get();
        dSrc.Type      = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dSrc.PlacedFootprint.Footprint = {DXGI_FORMAT_R32_FLOAT, kShadowProxySize, kShadowProxySize, 1u, pitch};
        m_cmd->CopyTextureRegion(&dDst, 0u, 0u, 0u, &dSrc, nullptr);
        auto bOut = MakeBarrier(m_shadowMapProxy.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        m_cmd->ResourceBarrier(1u, &bOut);
        if (FAILED(m_cmd->Close())) return false;
        ID3D12CommandList* lists[1] = {m_cmd.Get()};
        m_queue->ExecuteCommandLists(1u, lists);
        WaitGpuIdle();
        return true;
    }

    static void BuildWorldRowVectorViewProj(const float camRelColMajor[16], const float camPos[3], float outRowVec[16])
    {
        // Convert camera-relative column-vector matrix M (clip = M * float4(p - cam, 1))
        // into absolute world-space row-vector matrix V (clip = float4(p, 1) * V) for RawGpuCullHost / Froxels.
        for (int r = 0; r < 4; ++r) {
            const float m0 = camRelColMajor[r * 4 + 0];
            const float m1 = camRelColMajor[r * 4 + 1];
            const float m2 = camRelColMajor[r * 4 + 2];
            const float m3 = camRelColMajor[r * 4 + 3];
            outRowVec[0 * 4 + r] = m0;
            outRowVec[1 * 4 + r] = m1;
            outRowVec[2 * 4 + r] = m2;
            outRowVec[3 * 4 + r] = m3 - (m0 * camPos[0] + m1 * camPos[1] + m2 * camPos[2]);
        }
    }

    static void BuildWorldRowVectorInvViewProj(const float camRelInvColMajor[16], const float camPos[3], float outRowVecInv[16])
    {
        // Convert camera-relative column-vector invViewProj M (world = cam + (M * clip).xyz / (M * clip).w)
        // into absolute world-space row-vector invViewProj IV (worldH = float4(clip, 1) * IV).
        for (int c = 0; c < 4; ++c) {
            const float m0 = camRelInvColMajor[0 * 4 + c];
            const float m1 = camRelInvColMajor[1 * 4 + c];
            const float m2 = camRelInvColMajor[2 * 4 + c];
            const float m3 = camRelInvColMajor[3 * 4 + c];
            outRowVecInv[c * 4 + 0] = m0 + camPos[0] * m3;
            outRowVecInv[c * 4 + 1] = m1 + camPos[1] * m3;
            outRowVecInv[c * 4 + 2] = m2 + camPos[2] * m3;
            outRowVecInv[c * 4 + 3] = m3;
        }
    }

    void InitTacticalSquadAround(float centerX, float centerY)
    {
        m_tacticalSquad.clear();
        m_tacticalSquad.reserve(4u);
        static constexpr raw::worldgen::TacticalArchetype kArchetypes[4] = {
            raw::worldgen::TacticalArchetype::MeleeFlanker,
            raw::worldgen::TacticalArchetype::MeleeFlanker,
            raw::worldgen::TacticalArchetype::RangedArcher,
            raw::worldgen::TacticalArchetype::Spellcaster,
        };
        static constexpr float kOffsets[4][2] = {
            { 280.0f,  160.0f},
            {-260.0f,  180.0f},
            { 640.0f, -240.0f},
            {-640.0f, -220.0f},
        };
        for (uint32_t i = 0u; i < 4u; ++i) {
            raw::worldgen::SquadAgent a{};
            a.id        = i + 1u;
            a.archetype = kArchetypes[i];
            float sx    = centerX + kOffsets[i][0];
            float sy    = centerY + kOffsets[i][1];
            float sz    = 0.0f;
            int32_t gx = 0, gy = 0;
            if (m_navMesh.IsBaked() && m_navMesh.FindNearestWalkableCell({sx, sy, 0.0f}, 12, &gx, &gy)) {
                a.position = m_navMesh.GridToWorldCenter(gx, gy);
            } else {
                ActivePhysics().SampleTerrainHeight(sx, sy, &sz);
                a.position = {sx, sy, sz};
            }
            a.hpRatio = 1.0f;
            m_tacticalSquad.push_back(a);
        }
    }

    static constexpr uint32_t kMaxCullInstances = 1024u;
    static constexpr uint32_t kShadowProxySize  = 512u;

    ViewerOptions m_opt{};
    HWND          m_hwnd = nullptr;
    UINT          m_width = 1280u;
    UINT          m_height = 720u;
    bool          m_ready = false;
    bool          m_isWarp = false;
    bool          m_fluidOk = false;
    bool          m_radOk = false;
    bool          m_xpbdOk = false;
    bool          m_xpbdUploaded = false;
    bool          m_overlayOk = false;
    bool          m_gpuCullOk = false;
    bool          m_lightClustersOk = false;
    bool          m_volFroxelsOk = false;
    bool          m_shadowPcssOk = false;
    bool          m_shadowPcssRan = false;
    bool          m_gtaoOk = false;
    bool          m_gtaoRan = false;
    bool          m_ssrOk = false;
    bool          m_ssrRan = false;
    bool          m_autoExposureOk = false;
    bool          m_autoExposureRan = false;
    bool          m_cloudRaymarchOk = false;
    bool          m_cloudRaymarchRan = false;
    bool          m_postCinematicOk = false;
    bool          m_postCinematicRan = false;
    bool          m_gpuParticlesOk = false;
    bool          m_gpuParticlesRan = false;
    bool          m_tsrUpscaleOk = false;
    bool          m_tsrUpscaleRan = false;
    bool          m_waterCausticsOk = false;
    bool          m_waterCausticsRan = false;
    std::string   m_adapterName;

    ComPtr<IDXGIFactory4>              m_factory;
    ComPtr<ID3D12Device>               m_dev;
    ComPtr<ID3D12InfoQueue>            m_infoQueue;
    ComPtr<ID3D12CommandQueue>         m_queue;
    ComPtr<IDXGISwapChain3>            m_swapChain;
    ComPtr<ID3D12Resource>             m_backBuffers[kBackBufferCount];
    ComPtr<ID3D12Resource>             m_displayLdr;
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
    RawXpbdSimHost                     m_xpbdSim;
    RawOverlayHost                     m_overlayHost;
    RawGpuCullHost                     m_gpuCull;
    RawLightClusterHost                m_lightClusters;
    uint32_t                           m_lastClusterLightCount = 0u;
    RawVolumetricFroxelHost            m_volFroxels;
    RawShadowPcssHost                  m_shadowPcss;
    RawGtaoHost                        m_gtao;
    RawSsrHost                         m_ssr;
    RawAutoExposureHost                m_autoExposure;
    RawCloudRaymarchHost               m_cloudRaymarch;
    RawPostCinematicHost               m_postCinematic;
    RawGpuParticlesHost                m_gpuParticles;
    RawTsrUpscaleHost                  m_tsrUpscale;
    RawWaterCausticsHost               m_waterCaustics;
    raw::worldgen::RawCraftingInventoryEngine m_craftingEngine;
    raw::worldgen::EquippedCombatStats m_equippedStats{};
    raw::worldgen::EncumbranceReport   m_encumbrance{};
    uint32_t                           m_forgeSwordId = 0u;
    uint32_t                           m_forgeArmorId = 0u;
    uint32_t                           m_forgeRingId  = 0u;
    uint32_t                           m_forgeIngAId  = 0u;
    uint32_t                           m_forgeIngBId  = 0u;
    uint32_t                           m_forgeCycleIdx = 0u;
    raw::worldgen::RawQuestStoryEngine m_questStory;
    raw::worldgen::QuestDirectorTelemetry m_questTelemetry{};
    uint32_t                           m_questSeedCounter = 0u;
    uint32_t                           m_questDialogueCycle = 0u;
    raw::worldgen::RawCombatSandboxEngine m_combatSandbox;
    raw::worldgen::CombatSandboxTelemetry m_combatTelemetry{};
    bool                               m_combatActive = false;
    uint32_t                           m_combatPlayerId = 0u;
    uint32_t                           m_combatActionCycle = 0u;
    raw::worldgen::SkeletonPose        m_playerPose{};
    raw::worldgen::XpbdAttachmentAnchors m_playerXpbdAnchors{};
    raw::worldgen::Vec3                m_playerRootPos{};
    bool                               m_playerRigEvaluated = false;
    std::vector<raw::worldgen::SkeletonPose> m_squadPoses;
    std::vector<raw::worldgen::XpbdAttachmentAnchors> m_squadAnchors;
    ComPtr<ID3D12Resource>             m_shadowOutTex;
    ComPtr<ID3D12Resource>             m_shadowMapProxy;
    float                              m_prevWorldToClip[16] = {};
    float                              m_prevCamPos[3] = {};
    float                              m_prevCamFwd[3] = {0.0f, 1.0f, 0.0f};
    bool                               m_hasPrevFroxelView = false;
    ComPtr<ID3D12Resource>             m_cullInstBuf;
    ComPtr<ID3D12Resource>             m_cullInstUpload;
    uint8_t*                           m_cullInstMapped = nullptr;
    uint32_t                           m_lastCullInstCount = 0u;
    raw::audio::RawAudioOutput         m_audio;
    raw::worldgen::RawWeatherSim          m_weatherSim;
    raw::worldgen::RawWorldShowcase       m_showcase;
    raw::worldgen::RawTamrielWorldStreamer m_tamrielStreamer;
    raw::worldgen::EsmWorldMetaIndex      m_esmWorldMeta;
    bool                                  m_esmMetaLoaded = false;
    std::vector<const raw::worldgen::EsmWeatherRecord*> m_esmWeatherCycle;
    uint32_t                              m_esmWeatherIdx = 0u;
    std::string                           m_activeEsmWeatherEdid;
    raw::worldgen::EsmPhysicalWeatherParams m_activeEsmWeatherParams{};
    raw::worldgen::RawWorldPhysics        m_worldPhysics;
    raw::worldgen::RawNavMesh             m_navMesh;
    raw::worldgen::RawWorldEcosystem      m_ecosystem;
    raw::worldgen::EcoFieldTelemetry      m_ecoTelemetry{};
    bool                                  m_ecoInitialized = false;
    raw::worldgen::RawWildlifeBoids       m_wildlife;
    raw::worldgen::WildlifeTelemetry      m_wildlifeTelemetry{};
    bool                                  m_wildlifeInitialized = false;
    float                                 m_wildlifeWaterZ = -14000.0f;
    raw::worldgen::RawSettlementSim       m_settlementSim;
    raw::worldgen::SettlementTelemetry    m_settlementTelemetry{};
    double                                m_initialSettlementGold = 0.0;
    uint32_t                              m_activeHoldIdx = 0u;
    raw::worldgen::RawWorldEventDirector  m_eventDirector;
    raw::worldgen::EventDirectorTelemetry m_eventTelemetry{};
    raw::worldgen::RawFactionDiplomacy    m_diplomacy;
    raw::worldgen::DiplomacyTelemetry     m_diplomacyTelemetry{};
    uint32_t                              m_diplomacyHoldIdx = 0u;
    uint32_t                              m_diplomacyFactionIdx = 0u;
    std::string                           m_lastSaveStatus;
    raw::worldgen::RawWorldPhysics        m_dungeonPhysics;
    raw::worldgen::GeneratedDungeon       m_activeDungeon{};
    std::unordered_map<uint64_t, TerrainGpuChunk> m_dungeonChunks;
    WorldMode                             m_preDungeonMode = WorldMode::Frontier;
    uint32_t                              m_dungeonCycleIdx = 0u;
    float                                 m_savedOverCam[3] = {};
    float                                 m_savedOverYaw = 0.0f;
    float                                 m_savedOverPitch = -0.45f;
    std::vector<raw::worldgen::SquadAgent> m_tacticalSquad;
    std::vector<raw::worldgen::TacticalSteeringOutput> m_lastSquadOut;
    raw::worldgen::NavPathResult          m_lastNavPath{};
    bool                                  m_navMeshDirty = false;
    uint64_t                              m_navMeshBakeCount = 0u;
    uint32_t                              m_screenshotCounter = 0u;
    std::string                           m_lastShotName;
    raw::worldgen::CharacterBody          m_playerBody{};
    raw::worldgen::RayHit                 m_lastRayHit{};
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
    bool                               m_walkMode = false;
    bool                               m_hudEnabled = true;
    OverlayPanelRect                   m_lastHudRect{};
    float                              m_frameMsRing[64] = {};
    uint32_t                           m_frameMsRingPos = 0u;
    uint32_t                           m_frameMsCount = 0u;
    float                              m_lastFrameMs = 16.667f;
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
        case WM_LBUTTONDOWN:
            if (st && st->viewer) {
                st->viewer->FireRaycastStrike();
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
                    case 'P': st->viewer->ToggleWalkMode(); break;
                    case 'C': st->viewer->FireRaycastStrike(); break;
                    case 'N': st->viewer->ComputeLiveNavPath(); break;
                    case 'I': st->viewer->IgniteWildfireAtLookHit(); break;
                    case 'V': st->viewer->TriggerWildlifeStartle(); break;
                    case 'E': st->viewer->AdvanceEconomyAndCycleHold(); break;
                    case 'O': st->viewer->ResolveOrPollWorldEvent(); break;
                    case 'K': st->viewer->CycleDiplomacyInspect(); break;
                    case 'D': st->viewer->CycleDungeonExplorer(); break;
                    case 'X': st->viewer->ToggleOrTriggerCombatSkirmish(); break;
                    case 'Q': st->viewer->CycleRadiantQuestJournal(); break;
                    case VK_F5: st->viewer->QuicksaveWorld(); break;
                    case VK_F8: st->viewer->QuickloadWorld(); break;
                    case 'H':
                    case VK_F9: st->viewer->ToggleHud(); break;
                    case VK_F12: st->viewer->CaptureScreenshotBmp(); break;
                    case 'F': st->viewer->CycleCraftingForgeAction(); break;
                    case 'U': st->viewer->ToggleFire(); break;
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
            "Controls: WASD/QE flight (P=Walk/Fly, Space=Jump), Shift boost, RMB freelook, C/LMB raycast+fracture, N NavMesh A* path, I ignite wildfire, V startle wildlife, E economy +6h/cycle hold, O resolve/poll world event, K cycle hold/faction diplomacy, D cycle 3D dungeon explorer, X toggle/cycle combat skirmish+parry, Q radiant quest/dialogue journal, F forge/alchemy/enchant, F5/F8 quicksave/quickload, F9/H HUD, F12 BMP screenshot, 1-4 seasons, U fire, G GI, B biomes, L landmarks, T time, W weather/WTHR, A audio, R fracture, M frontier/Tamriel, ESC exit\n"
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
        RawEngineViewer::PhysicsWalkProbe riverwoodPhys{};
        RawEngineViewer::NavMeshSquadProbe riverwoodNav{};
        RawEngineViewer::EcosystemProbe ecoProbe{};
        RawEngineViewer::WildlifeProbe wildlifeProbe{};
        RawEngineViewer::SettlementProbe settlementProbe{};
        RawEngineViewer::WorldEventProbe eventProbe{};
        RawEngineViewer::DiplomacyProbe diplomacyProbe{};
        RawEngineViewer::SerializerProbe serializerProbe{};
        RawEngineViewer::DungeonExplorerProbe dungeonProbe{};
        RawEngineViewer::SkeletonFootIkProbe skelProbe{};
        RawEngineViewer::CombatSkirmishProbe combatProbe{};
        RawEngineViewer::QuestStoryProbe questProbe{};
        RawEngineViewer::CraftingForgeProbe forgeProbe{};
        bool teleportedToWhiterun = false;
        for (uint32_t f = 0; f < opt.headlessFrames; ++f) {
            if (f == 0u) {
                ecoProbe        = viewer.RunEcosystemSelfTest();
                wildlifeProbe   = viewer.RunWildlifeSelfTest();
                settlementProbe = viewer.RunSettlementSelfTest();
                eventProbe      = viewer.RunWorldEventSelfTest();
                diplomacyProbe  = viewer.RunDiplomacySelfTest();
                serializerProbe = viewer.RunWorldSerializerSelfTest();
                if (opt.startEsm) {
                    riverwoodStats = viewer.GetTamrielStats();
                    riverwoodPhys = viewer.RunPhysicsWalkSelfTest();
                    riverwoodNav  = viewer.RunNavMeshSquadSelfTest();
                }
                dungeonProbe    = viewer.RunDungeonExplorerSelfTest();
                skelProbe       = viewer.RunSkeletonFootIkSelfTest();
                combatProbe     = viewer.RunCombatSkirmishSelfTest();
                questProbe      = viewer.RunQuestStorySelfTest();
                forgeProbe      = viewer.RunCraftingForgeSelfTest();
            }
            if (f == opt.headlessFrames / 2u) {
                if (opt.startEsm && std::filesystem::exists(opt.esmPath)) {
                    teleportedToWhiterun = viewer.TeleportToTamrielLandmark(1u); // Riverwood (4,-12) -> Whiterun (5,-3)!
                    viewer.ApplyTimePreset(TimePreset::GoldenHour);
                    viewer.CycleWeatherPreset(); // Cycle authentic Skyrim.esm WTHR record!
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
        std::printf("       Audit: path=%s, meshlets=%u, shards=%u, biomes=%u/6, gi=%s (%.3fms), fire=%.3fms, water=%.3fms, xpbd=%s (%u p, %.3fms), cull=%s (%u insts, %u mips, hiz=%.4fms, cull=%.4fms), lclust=%s (%u L, build=%.4fms, assign=%.4fms), frox=%s (inj=%.4fms, int=%.4fms), pcss=%s (%.4fms), gtao=%s (main=%.4fms, den=%.4fms), ssr=%s (%.4fms), ae=%s (hist=%.4fms, adapt=%.4fms), cloud=%s (%.4fms), post=%s (%.4fms), prt=%s (%u p, %.4fms), tsr=%s (%ux%u, %.4fms), cau=%s (%.4fms), hud=%s (%.4fms, hudPx=%u), hdrLitPx=%u, hdrNanInf=%u, dispNonZeroPx=%u, dispMean=%.3f, dispStdDev=%.3f\n",
                    audit.usedMeshShaders ? "AS+MS+PS" : "VS+PS",
                    audit.drawnMeshlets, audit.activeShards, audit.biomesFound,
                    audit.radianceActive ? "ON" : "off", audit.radianceGiMs,
                    audit.fireStepMs, audit.waterStepMs,
                    audit.xpbdActive ? "ON" : "off", audit.xpbdParticles, audit.xpbdStepMs,
                    audit.gpuCullActive ? "ON" : "off", audit.gpuCullInstances, audit.gpuHiZMips, audit.gpuHiZMs, audit.gpuCullMs,
                    audit.lightClustersActive ? "ON" : "off", audit.lightClusterCount, audit.lightClusterBuildMs, audit.lightClusterAssignMs,
                    audit.volFroxelsActive ? "ON" : "off", audit.froxelInjectMs, audit.froxelIntegrateMs,
                    audit.shadowPcssActive ? "ON" : "off", audit.shadowPcssMs,
                    audit.gtaoActive ? "ON" : "off", audit.gtaoMainMs, audit.gtaoDenoiseMs,
                    audit.ssrActive ? "ON" : "off", audit.ssrMs,
                    audit.autoExposureActive ? "ON" : "off", audit.autoExpHistMs, audit.autoExpAdaptMs,
                    audit.cloudRaymarchActive ? "ON" : "off", audit.cloudRaymarchMs,
                    audit.postCinematicActive ? "ON" : "off", audit.postCinematicMs,
                    audit.gpuParticlesActive ? "ON" : "off", audit.gpuParticleCount, audit.gpuParticlesMs,
                    audit.tsrUpscaleActive ? "ON" : "off", audit.tsrOutWidth, audit.tsrOutHeight, audit.tsrUpscaleMs,
                    audit.waterCausticsActive ? "ON" : "off", audit.waterCausticsMs,
                    audit.overlayActive ? "ON" : "off", audit.overlayMs, audit.hudNonZeroPx,
                    audit.hdrLitPixels, audit.hdrNanInf,
                    audit.dispNonZeroPx, audit.dispMeanLuma, audit.dispStdDevLuma);
        std::printf("       Skeleton Foot-IK: rigs=%u maxQuatErr=%.2e pelvisDrop=%.2fu footPlant=%d xpbdAnchorsFinite=%d coupled=%d\n",
                    skelProbe.evaluatedRigs, static_cast<double>(skelProbe.maxQuatNormErr),
                    static_cast<double>(skelProbe.pelvisDropZ), int(skelProbe.footPlanted),
                    int(skelProbe.xpbdAnchorsFinite), int(skelProbe.coupledToXpbd));
        std::printf("       Combat Skirmish: allies=%u hostiles=%u hits=%u parries=%u poiseBrk=%u arrows=%u timedParry=%d skelCoupled=%d finite=%d\n",
                    combatProbe.aliveAllies, combatProbe.aliveHostiles, combatProbe.hitsLanded,
                    combatProbe.parries, combatProbe.poiseBreaks, combatProbe.arrowsFired,
                    int(combatProbe.timedParryTriggered), int(combatProbe.skeletonCoupled), int(combatProbe.allFinite));
        std::printf("       Quest Story: generated=%u active=%u completed=%u dialogue=%d stageDone=%d gold=%.0fg jsonValid=%d\n",
                    questProbe.generatedCount, questProbe.activeCount, questProbe.completedCount,
                    int(questProbe.dialoguePassed), int(questProbe.stageCompleted),
                    questProbe.totalGoldAwarded, int(questProbe.journalJsonValid));
        std::printf("       Crafting Forge: temper=%d brew=%d enchant=%d bounded=%d (alch=%.1f%% ench=%.1f%%) heroCoupled=%d (dmg=%.1f arm=%.1f) barter=%d finite=%d\n",
                    int(forgeProbe.temperOk), int(forgeProbe.brewOk), int(forgeProbe.enchantOk),
                    int(forgeProbe.antiExploitBounded), static_cast<double>(forgeProbe.convergedAlchPct),
                    static_cast<double>(forgeProbe.convergedEnchPct), int(forgeProbe.heroCoupled),
                    static_cast<double>(forgeProbe.finalWeaponDmg), static_cast<double>(forgeProbe.finalArmorRating),
                    int(forgeProbe.barterSpreadValid), int(forgeProbe.allFinite));
        std::printf("       Ecosystem: ignited=%u activeFire=%u burned=%.1fm2 snow=%.1fm3 runoff=%.2fm3 ice=%u peakTemp=%.1fC finite=%d\n",
                    ecoProbe.ignitedCells, ecoProbe.activeFireCells, ecoProbe.burnedAreaM2,
                    ecoProbe.snowVolumeM3, ecoProbe.meltRunoffM3, ecoProbe.blackIceCells,
                    ecoProbe.peakTempC, int(ecoProbe.allFinite));
        std::printf("       Wildlife: agents=%u roost=%u startle=%u meanSpeed=%.1fu/s step=%.3fms finite=%d\n",
                    wildlifeProbe.activeAgents, wildlifeProbe.roostingCount, wildlifeProbe.startledCount,
                    wildlifeProbe.meanSpeed, wildlifeProbe.stepMs, int(wildlifeProbe.allFinite));
        std::printf("       Settlement Economy (24h): gold=%.0f->%.0f (conserved=%d) caravans=%u (snowbound=%u, delivered=%u) priceIdx=%.3f scarce=%s finite=%d\n",
                    settlementProbe.initialGold, settlementProbe.finalGold, int(settlementProbe.goldConserved),
                    settlementProbe.activeCaravans, settlementProbe.snowboundCaravans, settlementProbe.deliveriesCompleted,
                    settlementProbe.meanPriceIndex, settlementProbe.scarceHoldName, int(settlementProbe.allFinite));
        std::printf("       World Events: active=%u spawned=%u resolved=%u expired=%u top=%s@%s exact=%d finite=%d\n",
                    eventProbe.activeCount, eventProbe.totalSpawned, eventProbe.totalResolved, eventProbe.totalExpired,
                    eventProbe.topKindName, eventProbe.topHoldName, int(eventProbe.accountingExact), int(eventProbe.allFinite));
        std::printf("       Faction Diplomacy: WhiterunRep=%+.1f priceMult=%.3f | Imperial=%+.1f Stormcloak=%+.1f bounded=%d finite=%d\n",
                    diplomacyProbe.holdRepAfterReward, diplomacyProbe.merchantPriceMult,
                    diplomacyProbe.imperialRep, diplomacyProbe.stormcloakRep,
                    int(diplomacyProbe.allBounded), int(diplomacyProbe.allFinite));
        std::printf("       World Serializer: bin=%zuB json=%zuB roundTrip=%d bitExact=%d crcTamperRejected=%d jsonValid=%d\n",
                    serializerProbe.binaryBytes, serializerProbe.jsonBytes,
                    int(serializerProbe.roundTripOk), int(serializerProbe.bitExact),
                    int(serializerProbe.tamperRejected), int(serializerProbe.jsonValid));
        std::printf("       Dungeon Explorer: barrow=%d ice=%d dwemer=%d returned=%d | totalMeshlets=%u totalLights=%u | navWalk=%u astarWp=%zu len=%.1fu walkGrounded=%d\n",
                    int(dungeonProbe.barrowOk), int(dungeonProbe.iceCavernOk), int(dungeonProbe.dwemerRuinOk),
                    int(dungeonProbe.returnedOk), dungeonProbe.totalMeshlets, dungeonProbe.totalLights,
                    dungeonProbe.navWalkable, dungeonProbe.astarWaypoints, dungeonProbe.astarLengthU, int(dungeonProbe.walkGrounded));
        check(audit.xpbdActive && audit.xpbdParticles == 4096u && std::isfinite(audit.xpbdStepMs) && audit.xpbdStepMs >= 0.0f,
              "Live GPU XPBD soft-body/hair/bush solver (4,096 particles / 256 chains) stepped cleanly");
        check(audit.gpuCullActive && audit.gpuHiZMips > 1u &&
              std::isfinite(audit.gpuHiZMs) && audit.gpuHiZMs >= 0.0f &&
              std::isfinite(audit.gpuCullMs) && audit.gpuCullMs >= 0.0f,
              "Live GPU Hi-Z Pyramid + Instance Culler (RawGpuCullHost) ran cleanly with finite timings");
        check(audit.lightClustersActive && audit.lightClusterCount > 0u &&
              std::isfinite(audit.lightClusterBuildMs) && audit.lightClusterBuildMs >= 0.0f &&
              std::isfinite(audit.lightClusterAssignMs) && audit.lightClusterAssignMs >= 0.0f,
              "Live GPU 16x9x24 3D Clustered Light Grid (RawLightClusterHost) built & assigned cleanly");
        check(audit.volFroxelsActive &&
              std::isfinite(audit.froxelInjectMs) && audit.froxelInjectMs >= 0.0f &&
              std::isfinite(audit.froxelIntegrateMs) && audit.froxelIntegrateMs >= 0.0f,
              "Live GPU 160x90x64 3D Volumetric Froxels (RawVolumetricFroxelHost) executed cleanly with finite timings");
        check(audit.shadowPcssActive && std::isfinite(audit.shadowPcssMs) && audit.shadowPcssMs >= 0.0f,
              "Live GPU Variable-Penumbra PCSS Contact-Hardening Shadows (RawShadowPcssHost) executed cleanly");
        check(audit.gtaoActive && std::isfinite(audit.gtaoMainMs) && audit.gtaoMainMs >= 0.0f &&
              std::isfinite(audit.gtaoDenoiseMs) && audit.gtaoDenoiseMs >= 0.0f,
              "Live GPU Jimenez GTAO + Bent Normals (RawGtaoHost - 10th GPU Solver) executed cleanly");
        check(audit.ssrActive && std::isfinite(audit.ssrMs) && audit.ssrMs >= 0.0f,
              "Live GPU GGX Stochastic SSR (RawSsrHost - 11th GPU Solver) executed cleanly");
        check(audit.autoExposureActive && std::isfinite(audit.autoExpHistMs) && audit.autoExpHistMs >= 0.0f &&
              std::isfinite(audit.autoExpAdaptMs) && audit.autoExpAdaptMs >= 0.0f,
              "Live GPU 256-Bin Auto-Exposure (RawAutoExposureHost - 12th GPU Solver) executed cleanly");
        check(audit.cloudRaymarchActive && std::isfinite(audit.cloudRaymarchMs) && audit.cloudRaymarchMs >= 0.0f,
              "Live GPU 3D Volumetric Cloud Raymarcher (RawCloudRaymarchHost - 13th GPU Solver) executed cleanly");
        check(audit.postCinematicActive && std::isfinite(audit.postCinematicMs) && audit.postCinematicMs >= 0.0f,
              "Live GPU Cinematic Post-Process DoF+MotionBlur+Vignette (RawPostCinematicHost - 14th GPU Solver) executed cleanly");
        check(audit.gpuParticlesActive && audit.gpuParticleCount == 16384u &&
              std::isfinite(audit.gpuParticlesMs) && audit.gpuParticlesMs >= 0.0f,
              "Live GPU 16,384-Particle 3D Physics & Splat Engine (RawGpuParticlesHost - 15th GPU Solver) executed cleanly");
        check(audit.tsrUpscaleActive && audit.tsrOutWidth >= opt.width && audit.tsrOutHeight >= opt.height &&
              std::isfinite(audit.tsrUpscaleMs) && audit.tsrUpscaleMs >= 0.0f,
              "Live GPU RAW-TSR Temporal Super-Resolution + RCAS (RawTsrUpscaleHost - 16th GPU Solver) executed cleanly");
        check(audit.waterCausticsActive && std::isfinite(audit.waterCausticsMs) && audit.waterCausticsMs >= 0.0f,
              "Live GPU Physical Water Caustics & Underwater God-Rays (RawWaterCausticsHost - 17th GPU Solver) executed cleanly");
        check(skelProbe.evaluatedRigs >= 1u && skelProbe.maxQuatNormErr < 1.0e-4f &&
              skelProbe.footPlanted && skelProbe.xpbdAnchorsFinite && skelProbe.coupledToXpbd,
              "Live 24-Bone Procedural Skeleton Animation + Terrain Foot-IK + XPBD Anchor Coupling passed cleanly");
        check(combatProbe.timedParryTriggered && combatProbe.arrowsFired > 0u &&
              combatProbe.hitsLanded > 0u && combatProbe.skeletonCoupled && combatProbe.allFinite,
              "Live Combat Skirmish Arena (RawCombatSandboxEngine) 220ms Timed-Parry + Ballistic Arrows + Skeleton->XPBD coupling passed");
        check(questProbe.generatedCount >= 1u && questProbe.dialoguePassed && questProbe.stageCompleted &&
              questProbe.completedCount >= 1u && questProbe.totalGoldAwarded > 0.0 && questProbe.journalJsonValid,
              "Live Radiant Quest & Branching Dialogue Engine (RawQuestStoryEngine) generated, persuaded, completed & exported JSON");
        check(forgeProbe.temperOk && forgeProbe.brewOk && forgeProbe.enchantOk &&
              forgeProbe.antiExploitBounded && forgeProbe.heroCoupled && forgeProbe.barterSpreadValid && forgeProbe.allFinite,
              "Live Anti-Exploit Crafting/Alchemy/Enchanting Forge (RawCraftingInventoryEngine) converged & coupled to Combat Hero");
        check(dungeonProbe.barrowOk && dungeonProbe.iceCavernOk && dungeonProbe.dwemerRuinOk && dungeonProbe.returnedOk &&
              dungeonProbe.navWalkable > 100u && dungeonProbe.astarWaypoints >= 2u && dungeonProbe.walkGrounded,
              "Interactive Procedural 3D Dungeon Explorer cycled all 3 archetypes + interior NavMesh A* + Walk Mode & returned cleanly");
        check(ecoProbe.ignitedCells > 0u && ecoProbe.activeFireCells > 0u && ecoProbe.allFinite,
              "Live Cellular Ecosystem (RawWorldEcosystem) ignited wildfire, stepped 10x with active fire & finite telemetry");
        check(wildlifeProbe.activeAgents == 128u && wildlifeProbe.startledCount > 0u && wildlifeProbe.allFinite,
              "Live 3D Wildlife Boids (RawWildlifeBoids, 128 agents) flushed skyward on LoudNoise stimulus with zero NaNs");
        check(settlementProbe.goldConserved && settlementProbe.activeCaravans > 0u && settlementProbe.allFinite,
              "Live 9-Hold Caravan & Economy Sim (RawSettlementSim) stepped 24h with exact gold conservation & zero NaNs");
        check(eventProbe.totalSpawned > 0u && eventProbe.accountingExact && eventProbe.allFinite,
              "Live Emergent World-Event Director (RawWorldEventDirector) spawned/resolved events with exact lifecycle accounting");
        check(diplomacyProbe.merchantPriceMult < 1.0f && diplomacyProbe.imperialRep > 0.0f &&
              diplomacyProbe.stormcloakRep < 0.0f && diplomacyProbe.allBounded && diplomacyProbe.allFinite,
              "Live 9-Hold & 6-Faction Diplomacy (RawFactionDiplomacy) rewarded events, discounted prices & clamped spillover");
        check(serializerProbe.roundTripOk && serializerProbe.bitExact &&
              serializerProbe.tamperRejected && serializerProbe.jsonValid,
              "CRC-32 Persistent World Serializer (RawWorldSerializer) passed bit-exact 5-pillar round-trip & tamper rejection");
        check(audit.overlayActive && std::isfinite(audit.overlayMs) && audit.overlayMs >= 0.0f && audit.hudNonZeroPx > 500u,
              "Live GPU Telemetry & Sparkline HUD (RawOverlayHost) rendered cleanly onto LDR display buffer");
        if (opt.startEsm && std::filesystem::exists(opt.esmPath)) {
            const auto whStats = viewer.GetTamrielStats();
            std::printf("       Tamriel Streaming: Riverwood(cells=%u, nifCache=%u, hits=%llu) -> %s(cells=%u, nifCache=%u, hits=%llu, evictions=%u, meshlets=%u) | ESM WTHR=%zu (active=%s)\n",
                        riverwoodStats.activeCells, riverwoodStats.cachedNifModels,
                        static_cast<unsigned long long>(riverwoodStats.cacheHits),
                        viewer.ActiveLandmarkName(), whStats.activeCells, whStats.cachedNifModels,
                        static_cast<unsigned long long>(whStats.cacheHits),
                        viewer.TotalTamrielEvictions(), audit.drawnMeshlets,
                        viewer.LoadedEsmWeatherCount(), viewer.ActiveWeatherDisplayName());
            std::printf("       Riverwood Walk Physics: hfs=%u, bvhTris=%u, rayHit=%d (dist=%.1fu), walkGrounded=%d, feetZ=%.1f, finite=%d\n",
                        riverwoodPhys.hfCount, riverwoodPhys.bvhTriangles, int(riverwoodPhys.rayHit), riverwoodPhys.rayDist,
                        int(riverwoodPhys.walkGrounded), riverwoodPhys.finalFeetZ, int(riverwoodPhys.walkAllFinite));
            std::printf("       Riverwood NavMesh+Squad: baked=%d walkable=%u bake=%.2fms | A* found=%d wp=%zu len=%.1fu search=%.3fms | squad=%zu finite=%d\n",
                        int(riverwoodNav.baked), riverwoodNav.walkableCells, riverwoodNav.bakeMs,
                        int(riverwoodNav.pathFound), riverwoodNav.pathWaypoints, riverwoodNav.pathLengthU, riverwoodNav.pathSearchMs,
                        riverwoodNav.squadSize, int(riverwoodNav.squadAllFinite));
            check(riverwoodStats.activeCells >= 1u && teleportedToWhiterun && whStats.activeCells >= 1u &&
                  viewer.TotalTamrielEvictions() > 0u && audit.drawnMeshlets > 500u,
                  "RawTamrielWorldStreamer streamed Riverwood 3x3 ring, evicted & reloaded Whiterun 3x3 ring live on GPU");
            check(viewer.LoadedEsmWeatherCount() >= 50u,
                  "RawEsmNpcWeatherLoader loaded >= 50 authentic Skyrim.esm WTHR records and cycled live");
            check(riverwoodPhys.rayHit && riverwoodPhys.walkGrounded && riverwoodPhys.walkAllFinite,
                  "RawWorldPhysics RayCast + 24-step grounded capsule walk succeeded over real Riverwood terrain & NIF BVH");
            check(riverwoodNav.baked && riverwoodNav.walkableCells > 1000u &&
                  riverwoodNav.pathFound && riverwoodNav.pathWaypoints >= 2u &&
                  riverwoodNav.squadSize == 4u && riverwoodNav.squadAllFinite,
                  "RawNavMeshAi baked Riverwood 64x64 NavMesh (>1000 walkable), found cross-village A* path & stepped 4-agent squad cleanly");
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
