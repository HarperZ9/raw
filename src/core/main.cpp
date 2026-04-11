//=============================================================================
//  RAW — Rendering platform for Creation Engine
//
//  GPU shader systems, compute infrastructure, D3D11 proxy with full
//  pipeline control, and developer tools for modders.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <ShlObj.h>
#include <dxgi.h>

#include "BridgeData.h"
#include "DebugGUI.h"
#include "D3D11Hook.h"

// Kept tracker
#include "ImageSpaceTracker.h"

// Shader compilation diagnostics
#include "SB_ShaderDebug.h"

// Backend enhancement: shader bytecode cache
#include "ShaderCache.h"
#include "ShaderLoader.h"
#include "ShaderReload.h"
#include "ConfigManager.h"

// Clustered forward+ lighting (Light Limit Fix replacement)
#include "ClusteredLighting.h"

// Rendering pipeline: shader systems + motion vector generation
#include "ToneMapManager.h"
// [DISABLED] #include "AtmosphereRenderer.h"
#include "MaterialClassifier.h"
#include "GTAORenderer.h"
#include "ContactShadowRenderer.h"
#include "SkylightingRenderer.h"
#include "SSRRenderer.h"
// [DISABLED] #include "SDSMCascades.h"
#include "SSGIRenderer.h"
// [DISABLED] #include "VolumetricClouds.h"
// [DISABLED] #include "FrameGenerator.h"
// [DISABLED] #include "TemporalSuperRes.h"
#include "MotionVectorGen.h"
#include "WeatherParameterManager.h"

// CS replacement renderers
// [DISABLED] #include "GrassLightingRenderer.h"
// [DISABLED] #include "TreeLODLightingRenderer.h"
// [DISABLED] #include "WaterBlendingRenderer.h"
// [DISABLED] #include "DynamicCubemapRenderer.h"

// New rendering pipeline: Tier 1-4 systems
#include "BloomRenderer.h"
#include "ColorPipeline.h"
#include "DenoiseManager.h"
// [DISABLED] #include "DoFRenderer.h"
// [DISABLED] #include "LensRenderer.h"
// [DISABLED] #include "UnderwaterRenderer.h"

// New compute systems (Tier 5)
// [DISABLED] #include "VolumetricLightingRenderer.h"
// [DISABLED] #include "SubsurfaceScatteringRenderer.h"
// [DISABLED] #include "IndirectSpecularRenderer.h"
// [DISABLED] #include "ScreenSpaceDecalRenderer.h"
// [DISABLED] #include "ParticleLightingRenderer.h"

// Scene data bridge (reconstructed matrices for GPU rendering systems)
#include "SceneData.h"
#include "TextureDump.h"

// GPU compute infrastructure + effects
#include "ComputeManager.h"
#include "SRVInjector.h"
#include "LuminanceHistogram.h"
#include "HiZPyramid.h"
#include "SharedGPUResources.h"
#include "LUTManager.h"
#include "TAAManager.h"
#include "RenderPassManager.h"
#include "RenderPipeline.h"
#include "PhaseDispatcher.h"
#include "SceneCompositor.h"
#include "DebugRenderer.h"
#include "NavMeshVisualizer.h"
#include "SkeletonVisualizer.h"

// Scene composition observer (BeginTechnique hook + BSShaderManager state)
#include "SceneObserver.h"

// Unified diagnostics system
#include "BootDiagnostics.h"

// GPU profiling + frame capture
#include "GPUProfiler.h"
#include "FrameCapture.h"

// DXBC bytecode patching
#include "DXBCPatcher.h"

// ── Game readiness flag ─────────────────────────────────────────────────────
static std::atomic<bool> s_gameReady{false};
static uint32_t s_frameCount = 0;

// ── NaN/Inf sanitization ───────────────────────────────────────────────────
static void SanitizeFloat4(SB::Float4& v)
{
    if (!std::isfinite(v.x)) v.x = 0.0f;
    if (!std::isfinite(v.y)) v.y = 0.0f;
    if (!std::isfinite(v.z)) v.z = 0.0f;
    if (!std::isfinite(v.w)) v.w = 0.0f;
}

static void SanitizeAllData(SB::AllData& data)
{
    auto* raw = reinterpret_cast<char*>(&data);
    for (std::size_t i = 0; i < SB::kParamCount; ++i) {
        auto& vec = *reinterpret_cast<SB::Float4*>(raw + SB::kParamTable[i].offset);
        SanitizeFloat4(vec);
    }
}

// ── Timing ──────────────────────────────────────────────────────────────────
static std::chrono::high_resolution_clock::time_point s_lastFrame;
static bool s_hasLastFrame = false;

static float GetDeltaTime()
{
    auto now = std::chrono::high_resolution_clock::now();
    float dt = 0.016f;  // default 60fps

    if (s_hasLastFrame) {
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            now - s_lastFrame);
        dt = elapsed.count() / 1'000'000.0f;
        if (dt < 0.0001f) dt = 0.0001f;
        if (dt > 0.5f) dt = 0.5f;
    }

    s_lastFrame = now;
    s_hasLastFrame = true;
    return dt;
}

// ── SEH-safe wrapper for frame update ────────────────────────────────────
static int s_frameAVCount = 0;
static bool s_frameUpdateDisabled = false;
static bool s_startupDiagsDone = false;

static void DoFrameUpdate();

// ── Frame update (called from Present hook) ─────────────────────────────
void RunStandaloneFrameUpdate()
{
    if (!s_gameReady.load(std::memory_order_acquire))
        return;
    if (s_frameUpdateDisabled)
        return;

    __try {
        DoFrameUpdate();
        s_frameAVCount = 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        s_frameAVCount++;
        SB::BootDiag::LogError("StandaloneFrameUpdate",
            s_frameAVCount <= 5 ? "ACCESS VIOLATION caught by SEH" : "AV (disabled)");
        SKSE::log::error("RAW: standalone frame update AV #{}", s_frameAVCount);
        if (s_frameAVCount >= 5) {
            s_frameUpdateDisabled = true;
            SB::BootDiag::LogError("StandaloneFrameUpdate", "DISABLED after 5 AVs — no more data will flow");
            SB::BootDiag::DumpReport();
            SKSE::log::error("RAW: too many AVs — frame update disabled");
        }
    }
}

static void DoFrameUpdate()
{
    SB::BootDiag::BeginFrame(s_frameCount);

    float dt = GetDeltaTime();

    // ── Weather-reactive parameter update ───────────────────────────────
    if (SB::WeatherParameterManager::Get().IsInitialized())
        SB::WeatherParameterManager::Get().Update(dt);

    // ── Collect game state (only ImageSpaceTracker remains) ─────────────
    SB::AllData data{};

    try {
        data.imageSpace = SB::ImageSpaceTracker::Update();
    } catch (...) {
        SKSE::log::error("RAW: ImageSpaceTracker threw");
    }

    // SceneObserver: material counting + shader state
    if (SB::SceneObserver::Get().IsInstalled()) {
        try {
            data.scene = SB::SceneObserver::Get().Update();
        } catch (...) {
            SKSE::log::error("RAW: SceneObserver threw");
        }
    }

    // Sanitize all float4s BEFORE building matrices or dispatching GPU passes.
    SanitizeAllData(data);

    // Reconstruct full 4x4 matrices from camera/celestial data for GPU rendering systems
    SB::SceneMatrices::Get().Update(data);

    // Boot diagnostics: log key state before pipeline dispatch
    if (SB::BootDiag::IsActive()) {
        auto& sm = SB::SceneMatrices::Get();
        SB::BootDiag::LogGuard("SceneMatrices", "IsValid", sm.IsValid());
        SB::BootDiag::LogGuard("DepthSRV", "GetGameDepthSRV", D3D11Hook::GetGameDepthSRV() != nullptr);
        SB::BootDiag::LogGuard("GBufferNormals", "GetGBufferNormalsSRV", D3D11Hook::GetGBufferNormalsSRV() != nullptr);
        SB::BootDiag::LogGuard("GBufferMaterial", "GetGBufferMaterialSRV", D3D11Hook::GetGBufferMaterialSRV() != nullptr);
        SB::BootDiag::LogGuard("MaterialClassifier", "IsInitialized",
            SB::MaterialClassifier::Get().IsInitialized());
    }

    // Update debug GUI with current data
    SB::DebugGUI::SetData(data);

    // [DISABLED] Atmosphere: update LUTs when sun position changes
    // [DISABLED] if (SB::AtmosphereRenderer::Get().IsInitialized()) {
    // [DISABLED]     float sunZenithCos = data.celestial.SunDirection.y;
    // [DISABLED]     float sunAzimuth = std::atan2(data.celestial.SunDirection.x, data.celestial.SunDirection.z);
    // [DISABLED]     SB::AtmosphereRenderer::Get().UpdateLUTs(sunZenithCos, sunAzimuth);
    // [DISABLED] }

    // Material classifier: begin frame (clear buffer)
    if (SB::MaterialClassifier::Get().IsInitialized()) {
        SB::MaterialClassifier::Get().BeginFrame(D3D11Hook::GetContext());
    }

    // Clustered lighting: collect lights from scene graph
    if (SB::ClusteredLighting::Get().IsInitialized() && SB::ClusteredLighting::Get().IsEnabled()) {
        try {
            SB::ClusteredLighting::Get().CollectLights();
        } catch (...) {
            SKSE::log::error("RAW: ClusteredLighting::CollectLights threw");
        }
    }

    ++s_frameCount;

    // Feed TAA jitter to TAAManager for the resolve pass
    if (SB::TAAManager::Get().IsInitialized())
        SB::TAAManager::Get().SetJitter(data.render.Jitter.x, data.render.Jitter.y);

    SB::BootDiag::EndFrame();

    // Auto-reload shaders that changed on disk (~once per second)
    SB::ShaderAutoReloadTick();

    // GPU compute: build Hi-Z pyramid + linearize depth at Present time.
    // Present has COMPLETE depth (all geometry drawn). PostGeometry depth was
    // incomplete for complex scenes (phase detector fires too early).
    // One frame of depth latency is invisible at 60fps.
    {
        auto* ctx = D3D11Hook::GetContext();
        if (ctx) {
            if (SB::HiZPyramid::Get().IsInitialized() && SB::HiZPyramid::Get().IsEnabled())
                SB::HiZPyramid::Get().BuildPyramid(ctx);
            if (SB::SharedGPUResources::Get().IsInitialized())
                SB::SharedGPUResources::Get().Update(ctx);

            // Motion vectors: depth reprojection using current/previous camera matrices
            if (SB::MotionVectorGen::Get().IsInitialized()) {
                auto& sm = SB::SceneMatrices::Get();
                auto* depthSRV = SB::HiZPyramid::Get().IsInitialized()
                    ? SB::HiZPyramid::Get().GetSRV() : D3D11Hook::GetGameDepthSRV();
                if (sm.IsValid() && depthSRV)
                    SB::MotionVectorGen::Get().Dispatch(ctx, depthSRV, sm.InvViewProjMatrix(), sm.PrevViewProjMatrix());
            }
        }
    }

    // [DISABLED] SDSM: analyze depth distribution and optimize shadow cascade distance
    // [DISABLED] if (SB::SDSMCascades::Get().IsInitialized() && SB::SDSMCascades::Get().IsEnabled()) {
    // [DISABLED]     auto* ctx = D3D11Hook::GetContext();
    // [DISABLED]     if (ctx) SB::SDSMCascades::Get().Update(ctx);
    // [DISABLED] }

    // GPU compute: clustered lighting dispatch
    if (SB::ClusteredLighting::Get().IsInitialized() && SB::ClusteredLighting::Get().IsEnabled()) {
        auto* ctx = D3D11Hook::GetContext();
        if (ctx) {
            DirectX::XMFLOAT4X4 viewMat, projMat;
            DirectX::XMStoreFloat4x4(&viewMat, DirectX::XMMatrixIdentity());
            DirectX::XMStoreFloat4x4(&projMat, DirectX::XMMatrixIdentity());

            if (auto* niCamera = RE::Main::WorldRootCamera()) {
                const auto& camRT  = niCamera->GetRuntimeData();
                const auto& camRT2 = niCamera->GetRuntimeData2();

                DirectX::XMMATRIX view;
                for (int row = 0; row < 4; ++row)
                    for (int col = 0; col < 4; ++col)
                        view.r[row].m128_f32[col] = camRT.worldToCam[row][col];
                DirectX::XMStoreFloat4x4(&viewMat, view);

                float l = camRT2.viewFrustum.fLeft;
                float r = camRT2.viewFrustum.fRight;
                float t = camRT2.viewFrustum.fTop;
                float b = camRT2.viewFrustum.fBottom;
                float n = camRT2.viewFrustum.fNear;
                float f = camRT2.viewFrustum.fFar;
                if (std::abs(r - l) > 1e-6f && std::abs(t - b) > 1e-6f && std::abs(f - n) > 1e-6f)
                    DirectX::XMStoreFloat4x4(&projMat, DirectX::XMMatrixPerspectiveOffCenterLH(l, r, b, t, n, f));
            }

            float nearZ = data.camera.Params.y;
            float farZ  = data.camera.Params.z;
            if (nearZ < 0.01f) nearZ = 1.0f;
            if (farZ < nearZ)  farZ = 10000.0f;

            uint32_t screenW = static_cast<uint32_t>(data.render.FrameInfo.z);
            uint32_t screenH = static_cast<uint32_t>(data.render.FrameInfo.w);
            if (screenW == 0) screenW = 1920;
            if (screenH == 0) screenH = 1080;

            SB::ClusteredLighting::Get().Dispatch(
                ctx, viewMat, projMat,
                nearZ, farZ, screenW, screenH);
        }
    }

    // Inject compute output SRVs for rendering systems
    SB::SRVInjector::Get().InjectAll();

    // Debug visualizers (self-guard with IsEnabled check)
    SB::NavMeshVisualizer::Get().Update();
    SB::SkeletonVisualizer::Get().Update();

    // ── Periodic health report ─────────────────────────────────────────────
    if (s_frameCount == 600 || (s_frameCount > 0 && (s_frameCount % 18000) == 0)) {
        SKSE::log::info("RAW: health report at frame {}", s_frameCount);
    }

    // ── On-screen startup diagnostics ─────────────────────────────────────
    if (!s_startupDiagsDone && s_frameCount == 60) {
        s_startupDiagsDone = true;

        char buf[256];

        // ShaderCache stats
        auto& sc = SB::ShaderCache::Get();
        snprintf(buf, sizeof(buf), "PG: ShaderCache %u hits, %u misses, %u stored",
            sc.GetHitCount(), sc.GetMissCount(), sc.GetStoreCount());
        RE::DebugNotification(buf);

        // Pipeline status
        auto& pipeline = SB::RenderPipeline::Get();
        snprintf(buf, sizeof(buf), "PG: Pipeline %s, %u passes registered",
            pipeline.IsInitialized() ? "OK" : "FAILED",
            pipeline.GetPassCount());
        RE::DebugNotification(buf);
    }
}

// ── Notification helper ─────────────────────────────────────────────────────
static void ShowNotification(const char* a_message)
{
    RE::DebugNotification(a_message);
}

// ── SKSE message handler ────────────────────────────────────────────────────
static void OnMessage(SKSE::MessagingInterface::Message* a_msg)
{
    SKSE::log::info("RAW: received message type {}", a_msg->type);

    switch (a_msg->type) {
    case SKSE::MessagingInterface::kPostLoad:
        break;

    case SKSE::MessagingInterface::kPostPostLoad:
        // Initialize debug GUI
        SB::DebugGUI::Init();
        SKSE::log::info("RAW: debug GUI initialized");
        break;

    case SKSE::MessagingInterface::kDataLoaded:
        SKSE::log::info("RAW: game data loaded — initializing subsystems");

        // Boot diagnostics: start recording everything
        SB::BootDiag::Init();

        // Initialize shader bytecode cache + external shader loader
        {
            auto cacheDir = std::filesystem::path("Data/SKSE/Plugins/RAW/ShaderCache");
            SB::ShaderCache::Get().Initialize(cacheDir);

            auto shaderDir = std::filesystem::path("Data/SKSE/Plugins/RAW/Shaders");
            SB::ShaderLoader::SetShaderDir(shaderDir);
            SKSE::log::info("RAW: ShaderLoader active — external HLSL from {}", shaderDir.string());

            // Texture dump output directory (F5 capture)
            SB::TextureDump::SetOutputDir(
                std::filesystem::path("Data/SKSE/Plugins/RAW/Captures"));
        }

        // Initialize D3D11 hook for ImGui overlay
        if (D3D11Hook::Init()) {
            SB::BootDiag::LogInit("D3D11Hook", true, D3D11Hook::IsProxyActive() ? "proxy mode" : "legacy mode");
            SKSE::log::info("RAW: D3D11 hook initialized — press INSERT to toggle debug GUI");

            auto* dev = D3D11Hook::GetDevice();
            auto* ctx = D3D11Hook::GetContext();
            auto* sc  = D3D11Hook::GetSwapChain();

            SB::BootDiag::LogInfo("D3D11Hook", dev ? "Device OK" : "Device NULL");
            SB::BootDiag::LogInfo("D3D11Hook", ctx ? "Context OK" : "Context NULL");
            SB::BootDiag::LogInfo("D3D11Hook", sc ? "SwapChain OK" : "SwapChain NULL");

            // Install ShaderDebug compilation diagnostics
            if (dev && ctx && sc) {
                SB::Debug::ShaderDebug::Get().Install(dev, ctx, sc);
                SKSE::log::info("RAW: ShaderDebug installed — F10 toggle, F11 clear");
            }

            // GPU compute infrastructure + SRV injection
            if (dev && ctx && sc) {
                SB::ComputeManager::Get().Initialize(dev, ctx);
                SB::SRVInjector::Get().Initialize(ctx);
                SB::RenderPassManager::Get().Initialize(dev, ctx);

                // LuminanceHistogram
                if (SB::LuminanceHistogram::Get().Initialize(dev, sc)) {
                    SB::SRVInjector::Get().RegisterSRV(
                        SB::LuminanceHistogram::kSRVSlot,
                        SB::LuminanceHistogram::Get().GetHistogramSRV());
                    SKSE::log::info("RAW: LuminanceHistogram active at t{}",
                        SB::LuminanceHistogram::kSRVSlot);
                }

                // RenderPipeline
                SB::RenderPipeline::Get().Initialize(dev, ctx, sc);

                // HiZPyramid
                if (SB::HiZPyramid::Get().Initialize(dev, sc)) {
                    SB::SRVInjector::Get().RegisterSRV(
                        SB::HiZPyramid::kSRVSlot,
                        SB::HiZPyramid::Get().GetSRV());

                    // HiZ builds ONLY at Present time (line ~264) where depth is complete.
                    // PostGeometry HiZ removed — phase detector fires before all
                    // geometry is drawn, producing incomplete depth.

                    SKSE::log::info("RAW: HiZPyramid active at t{} ({}x{}, {} mips)",
                        SB::HiZPyramid::kSRVSlot,
                        SB::HiZPyramid::Get().GetWidth(),
                        SB::HiZPyramid::Get().GetHeight(),
                        SB::HiZPyramid::Get().GetMipCount());
                }

                // SharedGPUResources
                if (SB::SharedGPUResources::Get().Initialize(dev, ctx, sc)) {
                    SB::SRVInjector::Get().RegisterSRV(
                        SB::SharedGPUResources::kBlueNoiseSlot,
                        SB::SharedGPUResources::Get().GetBlueNoiseSRV());
                    SB::SRVInjector::Get().RegisterSRV(
                        SB::SharedGPUResources::kLinearDepthSlot,
                        SB::SharedGPUResources::Get().GetLinearDepthSRV());

                    // SharedGPUResources builds ONLY at Present time (after HiZ).
                }

                // LUTManager
                {
                    auto lutDir = std::filesystem::path("Data/SKSE/Plugins/RAW/LUTs");
                    if (SB::LUTManager::Get().Initialize(dev, lutDir)) {
                        if (SB::LUTManager::Get().IsEnabled()) {
                            SB::SRVInjector::Get().RegisterSRV(
                                SB::LUTManager::kSRVSlot,
                                SB::LUTManager::Get().GetActiveSRV());
                            SB::SRVInjector::Get().RegisterSampler(
                                SB::LUTManager::kSamplerSlot,
                                SB::LUTManager::Get().GetSampler());
                            SKSE::log::info("RAW: LUTManager active at t{}/s{} ({} LUTs loaded)",
                                SB::LUTManager::kSRVSlot, SB::LUTManager::kSamplerSlot,
                                SB::LUTManager::Get().GetLUTCount());
                        }
                    }
                }

                // TAAManager
                if (SB::TAAManager::Get().Initialize(dev, sc)) {
                    SB::SRVInjector::Get().RegisterSRV(
                        SB::TAAManager::kSRVSlot,
                        SB::TAAManager::Get().GetHistorySRV());
                    SB::SRVInjector::Get().RegisterSampler(
                        SB::TAAManager::kSamplerSlot,
                        SB::TAAManager::Get().GetSampler());
                    SKSE::log::info("RAW: TAAManager active at t{}/s{} ({}x{})",
                        SB::TAAManager::kSRVSlot, SB::TAAManager::kSamplerSlot,
                        SB::TAAManager::Get().GetWidth(),
                        SB::TAAManager::Get().GetHeight());
                }

                // PhaseDispatcher
                {
                    auto invalidateCache = D3D11Hook::GetInvalidateCacheFn();
                    bool ok = SB::PhaseDispatcher::Get().Initialize(ctx, invalidateCache);
                    SB::BootDiag::LogInit("PhaseDispatcher", ok);
                    if (ok) SKSE::log::info("RAW: PhaseDispatcher active (mid-frame dispatch)");
                }

                // ClusteredLighting
                { bool ok = SB::ClusteredLighting::Get().Initialize(dev, ctx);
                  SB::BootDiag::LogInit("ClusteredLighting", ok);
                  if (ok) SKSE::log::info("RAW: ClusteredLighting active");
                }

                // ── Rendering pipeline: shader systems ──────────────────────
                { bool ok = SB::ToneMapManager::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("ToneMapManager", ok);
                }

                // [DISABLED] { bool ok = SB::AtmosphereRenderer::Get().Initialize(dev, ctx, sc);
                // [DISABLED]   SB::BootDiag::LogInit("AtmosphereRenderer", ok);
                // [DISABLED] }

                {
                    DXGI_SWAP_CHAIN_DESC scDesc;
                    if (SUCCEEDED(sc->GetDesc(&scDesc))) {
                        bool ok = SB::MaterialClassifier::Get().Initialize(
                                dev, ctx, scDesc.BufferDesc.Width, scDesc.BufferDesc.Height);
                        SB::BootDiag::LogInit("MaterialClassifier", ok);
                    }
                }

                { bool ok = SB::GTAORenderer::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("GTAORenderer", ok);
                }

                { bool ok = SB::ContactShadowRenderer::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("ContactShadowRenderer", ok);
                }

                { bool ok = SB::SkylightingRenderer::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("SkylightingRenderer", ok);
                }

                // [DISABLED] { bool ok = SB::SDSMCascades::Get().Initialize(dev, ctx, sc);
                // [DISABLED]   SB::BootDiag::LogInit("SDSMCascades", ok);
                // [DISABLED] }

                { bool ok = SB::SSRRenderer::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("SSRRenderer", ok);
                }

                { bool ok = SB::SSGIRenderer::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("SSGIRenderer", ok);
                }

                // Motion vector generation (needed by temporal passes)
                { DXGI_SWAP_CHAIN_DESC scDesc;
                  uint32_t w = 1920, h = 1080;
                  if (SUCCEEDED(sc->GetDesc(&scDesc))) { w = scDesc.BufferDesc.Width; h = scDesc.BufferDesc.Height; }
                  bool ok = SB::MotionVectorGen::Get().Initialize(dev, ctx, w, h);
                  SB::BootDiag::LogInit("MotionVectorGen", ok);
                }

                // Weather-reactive parameter interpolation
                SB::WeatherParameterManager::Get().Initialize();

                // Heavy deferred systems: skip init, lazy-init on enable
                SKSE::log::info("RAW: Skipping init for heavy deferred systems — will lazy-init on enable");
                SB::BootDiag::LogInit("DeferredHeavySystems", true, "deferred — 13 systems skipped");

                // DenoiseManager
                { bool ok = SB::DenoiseManager::Get().Initialize(dev, ctx);
                  SB::BootDiag::LogInit("DenoiseManager", ok);
                }

                // ColorPipeline
                { bool ok = SB::ColorPipeline::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("ColorPipeline", ok);
                }

                // BloomRenderer
                { bool ok = SB::BloomRenderer::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("BloomRenderer", ok);
                }

                // [DISABLED] DoFRenderer
                // [DISABLED] { bool ok = SB::DoFRenderer::Get().Initialize(dev, ctx, sc);
                // [DISABLED]   SB::BootDiag::LogInit("DoFRenderer", ok);
                // [DISABLED] }

                // [DISABLED] LensRenderer
                // [DISABLED] { bool ok = SB::LensRenderer::Get().Initialize(dev, ctx, sc);
                // [DISABLED]   SB::BootDiag::LogInit("LensRenderer", ok);
                // [DISABLED] }

                // [DISABLED] UnderwaterRenderer
                // [DISABLED] { bool ok = SB::UnderwaterRenderer::Get().Initialize(dev, ctx, sc);
                // [DISABLED]   SB::BootDiag::LogInit("UnderwaterRenderer", ok);
                // [DISABLED] }

                // SceneCompositor
                { bool ok = SB::SceneCompositor::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("SceneCompositor", ok, "default disabled");
                }

                // Debug visualization (line renderer for navmesh, skeleton overlays)
                { bool ok = SB::DebugRenderer::Get().Initialize(dev, ctx);
                  SB::BootDiag::LogInit("DebugRenderer", ok);
                  if (ok) SKSE::log::info("RAW: DebugRenderer active");
                }
            }

        } else {
            SKSE::log::warn("RAW: D3D11 hook failed — debug GUI unavailable");
        }

        // Install scene composition observer (BeginTechnique hook)
        if (SB::SceneObserver::Get().Install()) {
            SKSE::log::info("RAW: SceneObserver active — material counting + shader state");
        }

        // Log initial game state for debugging
        if (auto* sky = RE::Sky::GetSingleton()) {
            SKSE::log::info("RAW: Sky OK — weather={}, masser={}, secunda={}",
                sky->currentWeather ? "yes" : "null",
                sky->masser ? "yes" : "null",
                sky->secunda ? "yes" : "null");
        }

        // Load persistent settings and apply to all initialized systems
        {
            auto configPath = std::filesystem::path("Data/SKSE/Plugins/RAW/RAW.ini");
            SB::ConfigManager::Get().Initialize(configPath);
            SB::ConfigManager::Get().ApplyToSystems();
        }

        // All subsystems initialized — NOW enable the per-frame callback.
        s_gameReady.store(true, std::memory_order_release);
        SKSE::log::info("RAW: all subsystems ready");

        // ── Startup state dump for diagnostics ──────────────────────
        SKSE::log::info("RAW Pipeline State:");
        SKSE::log::info("  DepthSRV: {}",
            D3D11Hook::GetGameDepthSRV() ? "active" : "null");
        SKSE::log::info("  HiZ: {} | LinearDepth: {} | BlueNoise: {} | MotionVec: {}",
            SB::HiZPyramid::Get().IsInitialized() ? "OK" : "--",
            SB::SharedGPUResources::Get().IsInitialized() ? "OK" : "--",
            SB::SharedGPUResources::Get().GetBlueNoiseSRV() ? "OK" : "null",
            SB::MotionVectorGen::Get().IsInitialized() ? "OK" : "--");
        SKSE::log::info("  GTAO: {} | CS: {} | Sky: {} | SSR: {} | SSGI: {}",
            SB::GTAORenderer::Get().IsInitialized() ? "OK" : "--",
            SB::ContactShadowRenderer::Get().IsInitialized() ? "OK" : "--",
            SB::SkylightingRenderer::Get().IsInitialized() ? "OK" : "--",
            SB::SSRRenderer::Get().IsInitialized() ? "OK" : "--",
            SB::SSGIRenderer::Get().IsInitialized() ? "OK" : "--");
        SKSE::log::info("  Compositor: {} | ColorPipeline: {} | Bloom: {} | Weather: {}",
            SB::SceneCompositor::Get().IsInitialized() ? "OK" : "--",
            SB::ColorPipeline::Get().IsInitialized() ? "OK" : "--",
            SB::BloomRenderer::Get().IsInitialized() ? "OK" : "--",
            SB::WeatherParameterManager::Get().IsInitialized() ? "OK" : "--");

        ShowNotification("RAW v1.0.0 - INSERT for debug GUI");
        break;

    case SKSE::MessagingInterface::kNewGame:
    case SKSE::MessagingInterface::kPostLoadGame:
        if (s_frameUpdateDisabled) {
            s_frameUpdateDisabled = false;
            s_frameAVCount = 0;
            SKSE::log::info("RAW: game world loaded — frame updates re-enabled");
        }
        s_frameCount = 0;
        s_startupDiagsDone = false;
        SKSE::log::info("RAW: entered game world");
        ShowNotification("RAW active");
        break;
    }
}

// ── SKSE plugin entry point ─────────────────────────────────────────────────
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);

    // Logging
    std::filesystem::path logPath;

    if (auto skseLogDir = SKSE::log::log_directory(); skseLogDir) {
        logPath = *skseLogDir / "RAW.log";
    } else {
        wchar_t* documentsPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documentsPath))) {
            logPath = std::filesystem::path(documentsPath) / "My Games" / "Skyrim Special Edition" / "SKSE" / "RAW.log";
            CoTaskMemFree(documentsPath);
        }
    }

    if (!logPath.empty()) {
        std::filesystem::create_directories(logPath.parent_path());

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            logPath.string(), true);
        auto logger = std::make_shared<spdlog::logger>("RAW", std::move(sink));
        logger->set_level(spdlog::level::info);
        logger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));

        SKSE::log::info("RAW v3.0.0 loaded — {} parameters defined",
            SB::kParamCount);
        SKSE::log::info("Log path: {}", logPath.string());
    }

    // Install D3DCompile hooks ASAP
    SB::Debug::ShaderDebug::Get().InstallHooksEarly();

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        SKSE::log::critical("RAW: no SKSE messaging interface");
        return false;
    }
    messaging->RegisterListener(OnMessage);

    return true;
}
