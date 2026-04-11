//=============================================================================
//  Playground — Rendering platform for Creation Engine
//
//  Complete rendering toolset: 24 game-state tracker domains, 7 GPU shader
//  systems, compute infrastructure, D3D11 proxy with full pipeline control,
//  and developer tools for modders.
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
#include "CelestialTracker.h"
#include "AtmosphereTracker.h"
#include "FogTracker.h"
#include "WeatherTracker.h"
#include "PlayerTracker.h"
#include "CameraTracker.h"
#include "InteriorTracker.h"
#include "ShadowTracker.h"
#include "EffectsTracker.h"
#include "RenderTracker.h"

// v2 expansion trackers (domains 11-17)
#include "ImageSpaceTracker.h"
#include "LightTracker.h"
#include "ActorValueTracker.h"
#include "CrosshairTracker.h"
#include "EquipmentTracker.h"
#include "QuestTracker.h"
#include "UIStateTracker.h"

// Weather parameter computer
#include "WeatherParameterComputer.h"

// Shared memory bridge (inter-process data sharing)
#include "SharedMemoryBridge.h"

// Shader compilation diagnostics
#include "SB_ShaderDebug.h"

// Feedback loop: GPU read-back → next frame's data
#include "FeedbackProcessor.h"

// Domain A: Write game state back via CommonLibSSE (INI-driven rules)
#include "WriteBackProcessor.h"

// Backend enhancement: shader bytecode cache
#include "ShaderCache.h"

// Annotation pre-processor
#include "ShaderPreProcessor.h"

// Extern binding injection
#include "ExternBindingProcessor.h"

// Weather separation engine (per-weather per-ToD parameter overrides)
#include "WeatherSeparationEngine.h"

// Parameter binding engine (conditional param-to-param binding)
#include "ParameterBindingEngine.h"


// Weather Editor: real-time weather record editing + presets
#include "WeatherEditor.h"

// Clustered forward+ lighting (Light Limit Fix replacement)
#include "ClusteredLighting.h"

// Rendering pipeline: 9 shader systems + motion vector generation
#include "ToneMapManager.h"
#include "AtmosphereRenderer.h"
#include "MaterialClassifier.h"
#include "GTAORenderer.h"
#include "ContactShadowRenderer.h"
#include "SkylightingRenderer.h"
#include "SSRRenderer.h"
#include "SDSMCascades.h"
#include "SSGIRenderer.h"
#include "VolumetricClouds.h"
#include "FrameGenerator.h"
#include "TemporalSuperRes.h"
#include "MotionVectorGen.h"

// CS replacement renderers
#include "GrassLightingRenderer.h"
#include "TreeLODLightingRenderer.h"
#include "WaterBlendingRenderer.h"
#include "DynamicCubemapRenderer.h"

// New rendering pipeline: Tier 1-4 systems
#include "BloomRenderer.h"
#include "ColorPipeline.h"
#include "DenoiseManager.h"
#include "DoFRenderer.h"
#include "LensRenderer.h"
#include "UnderwaterRenderer.h"

// New compute systems (Tier 5)
#include "VolumetricLightingRenderer.h"
#include "SubsurfaceScatteringRenderer.h"
#include "IndirectSpecularRenderer.h"
#include "ScreenSpaceDecalRenderer.h"
#include "ParticleLightingRenderer.h"

// Scene data bridge (reconstructed matrices for GPU rendering systems)
#include "SceneData.h"

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
#include "PipelineTest.h"
#include "SceneCompositor.h"

// EditorID cache (replaces NativeEditorID Fix)
#include "EditorIDCache.h"

// Compatibility detection (NativeEditorID Fix, po3_Tweaks, enbParmLink)
#include "CompatDetect.h"

// v3 expansion trackers (domains 19-22)
#include "RegionTracker.h"
#include "AudioTracker.h"
#include "NPCDetectTracker.h"
#include "PerfMonitor.h"

// Scene composition observer (BeginTechnique hook + BSShaderManager state)
#include "SceneObserver.h"

// Engine-level binary patches (BSSpinLock threshold, etc.)
#include "EngineFixes.h"

// Papyrus script bridge (native functions for mod authors)
#include "PapyrusBridge.h"

// Feature lifecycle management
#include "FeatureManager.h"

// Debug visualization overlays
#include "DebugRenderer.h"
#include "NavMeshVisualizer.h"
#include "SkeletonVisualizer.h"

// Unified diagnostics system
#include "SystemHealth.h"
#include "CompatibilityProbe.h"
#include "ProxyDiagnostics.h"
#include "BootDiagnostics.h"

// ── Game readiness flag ─────────────────────────────────────────────────────
// ENB callbacks fire during D3D initialization, before game singletons exist.
// We must not access any RE:: singletons until kDataLoaded fires.
// Atomic because kDataLoaded fires on the main thread but the ENB callback
// may fire on the render thread.
static std::atomic<bool> s_gameReady{false};
static uint32_t s_frameCount = 0;

// ── NaN/Inf sanitization ───────────────────────────────────────────────────
// Prevents corrupt floats from propagating to ENB shaders.
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

// ── Self-healing tracker health system ─────────────────────────────────────
// Tracks per-tracker error counts. Auto-disables trackers that crash
// repeatedly, then retries them periodically to allow recovery.
struct TrackerHealth
{
    int  consecutiveErrors = 0;
    int  totalErrors       = 0;
    bool disabled          = false;
    uint32_t disabledAtFrame = 0;

    static constexpr int      kDisableThreshold = 5;    // consecutive errors to disable
    static constexpr uint32_t kRetryInterval    = 300;   // frames before retry (~5 sec at 60fps)

    bool ShouldRun(uint32_t frame) const {
        if (!disabled) return true;
        return (frame - disabledAtFrame) >= kRetryInterval;
    }

    void OnSuccess(const char* label) {
        if (disabled) {
            disabled = false;
            SKSE::log::info("Playground: {} recovered after {} total errors", label, totalErrors);
        }
        consecutiveErrors = 0;
    }

    void OnError(uint32_t frame, const char* label) {
        consecutiveErrors++;
        totalErrors++;
        if (consecutiveErrors >= kDisableThreshold && !disabled) {
            disabled = true;
            disabledAtFrame = frame;
            SKSE::log::warn("Playground: {} auto-disabled after {} consecutive errors "
                "(will retry in {} frames)", label, kDisableThreshold, kRetryInterval);
        }
    }
};

enum TrackerID : int
{
    kTrkCelestial, kTrkAtmosphere, kTrkFog, kTrkWeather,
    kTrkPlayer, kTrkCamera, kTrkInterior, kTrkShadow,
    kTrkEffects, kTrkRender,
    kTrkImageSpace, kTrkLights, kTrkActorValues, kTrkCrosshair,
    kTrkEquipment, kTrkQuest, kTrkUIState,
    kTrkRegion, kTrkAudio, kTrkNPCDetect, kTrkPerfMonitor, kTrkScene,
    kTrkCount
};

static TrackerHealth s_trackerHealth[kTrkCount];

// ── SystemHealth IDs ─────────────────────────────────────────────────────────
// Registered at kDataLoaded; used for per-frame heartbeat + error reporting.
static uint32_t s_shID_trackers[kTrkCount] = {};   // one per tracker
static uint32_t s_shID_shaderCache    = 0;
static uint32_t s_shID_feedbackProc   = 0;
static uint32_t s_shID_writeBack      = 0;
static uint32_t s_shID_sharedMemory   = 0;
static uint32_t s_shID_weatherParam   = 0;
static uint32_t s_shID_weatherSep     = 0;
static uint32_t s_shID_paramBinding   = 0;
static uint32_t s_shID_externBinding  = 0;
static uint32_t s_shID_shaderPreProc  = 0;
static uint32_t s_shID_weatherEditor  = 0;
static uint32_t s_shID_sceneObserver  = 0;
static uint32_t s_shID_engineFixes    = 0;
static uint32_t s_shID_papyrus        = 0;
static uint32_t s_shID_editorIDCache  = 0;
static uint32_t s_shID_compatDetect   = 0;
static uint32_t s_shID_d3d11Hook      = 0;
static uint32_t s_shID_shaderDebug    = 0;
static uint32_t s_shID_computeMgr     = 0;
static uint32_t s_shID_srvInjector    = 0;
static uint32_t s_shID_lumHistogram   = 0;
static uint32_t s_shID_hiZPyramid     = 0;
static uint32_t s_shID_lutManager     = 0;
static uint32_t s_shID_taaMgr         = 0;
static uint32_t s_shID_renderPipeline = 0;
static uint32_t s_shID_toneMap        = 0;
static uint32_t s_shID_atmosphere     = 0;
static uint32_t s_shID_matClassifier  = 0;
static uint32_t s_shID_gtao           = 0;
static uint32_t s_shID_contactShadow  = 0;
static uint32_t s_shID_skylighting    = 0;
static uint32_t s_shID_ssr            = 0;
static uint32_t s_shID_sdsm           = 0;
static uint32_t s_shID_ssgi           = 0;
static uint32_t s_shID_volClouds      = 0;
static uint32_t s_shID_frameGen       = 0;
static uint32_t s_shID_temporalSR     = 0;
static uint32_t s_shID_motionVec      = 0;
static uint32_t s_shID_clusteredLight = 0;
static uint32_t s_shID_grassLight    = 0;
static uint32_t s_shID_treeLodLight  = 0;
static uint32_t s_shID_waterBlend    = 0;
static uint32_t s_shID_dynCubemap    = 0;
static uint32_t s_shID_debugRenderer  = 0;
static uint32_t s_shID_bloom         = 0;
static uint32_t s_shID_colorPipeline = 0;
static uint32_t s_shID_denoise       = 0;
static uint32_t s_shID_dof           = 0;
static uint32_t s_shID_lens          = 0;
static uint32_t s_shID_underwater    = 0;
static uint32_t s_shID_perfMonitor    = 0;
static uint32_t s_shID_proxyDiag      = 0;
static bool s_healthRegistered = false;

static void RegisterAllSystemHealth()
{
    if (s_healthRegistered) return;
    s_healthRegistered = true;

    auto& sh = SB::SystemHealth::Get();

    // Trackers
    static const char* kTrackerNames[kTrkCount] = {
        "Celestial", "Atmosphere", "Fog", "Weather",
        "Player", "Camera", "Interior", "Shadow",
        "Effects", "Render",
        "ImageSpace", "Lights", "ActorValues", "Crosshair",
        "Equipment", "Quest", "UIState",
        "Region", "Audio", "NPCDetect", "PerfMonitor", "Scene"
    };
    for (int i = 0; i < kTrkCount; ++i)
        s_shID_trackers[i] = sh.Register(kTrackerNames[i], SB::SystemCategory::Tracker);

    // Backend
    s_shID_shaderCache   = sh.Register("ShaderCache",           SB::SystemCategory::Backend);
    s_shID_feedbackProc  = sh.Register("FeedbackProcessor",     SB::SystemCategory::Backend);
    s_shID_writeBack     = sh.Register("WriteBackProcessor",    SB::SystemCategory::Backend);
    s_shID_sharedMemory  = sh.Register("SharedMemoryBridge",    SB::SystemCategory::Backend);
    s_shID_weatherParam  = sh.Register("WeatherParamComputer",  SB::SystemCategory::Backend);
    s_shID_weatherSep    = sh.Register("WeatherSeparation",     SB::SystemCategory::Backend);
    s_shID_paramBinding  = sh.Register("ParameterBinding",      SB::SystemCategory::Backend);
    s_shID_externBinding = sh.Register("ExternBinding",         SB::SystemCategory::Backend);
    s_shID_shaderPreProc = sh.Register("ShaderPreProcessor",    SB::SystemCategory::Backend);
    s_shID_weatherEditor = sh.Register("WeatherEditor",         SB::SystemCategory::Backend);

    // Integration
    s_shID_sceneObserver = sh.Register("SceneObserver",         SB::SystemCategory::Integration);
    s_shID_engineFixes   = sh.Register("EngineFixes",           SB::SystemCategory::Integration);
    s_shID_papyrus       = sh.Register("PapyrusBridge",         SB::SystemCategory::Integration);
    s_shID_editorIDCache = sh.Register("EditorIDCache",         SB::SystemCategory::Integration);
    s_shID_compatDetect  = sh.Register("CompatDetect",          SB::SystemCategory::Integration);

    // Pipeline
    s_shID_d3d11Hook     = sh.Register("D3D11Hook",             SB::SystemCategory::Pipeline);
    s_shID_shaderDebug   = sh.Register("ShaderDebug",           SB::SystemCategory::Pipeline);
    s_shID_computeMgr    = sh.Register("ComputeManager",        SB::SystemCategory::Pipeline);
    s_shID_srvInjector   = sh.Register("SRVInjector",           SB::SystemCategory::Pipeline);
    s_shID_renderPipeline= sh.Register("RenderPipeline",        SB::SystemCategory::Pipeline);

    // Compute
    s_shID_lumHistogram  = sh.Register("LuminanceHistogram",    SB::SystemCategory::Compute);
    s_shID_hiZPyramid    = sh.Register("HiZPyramid",           SB::SystemCategory::Compute);
    s_shID_lutManager    = sh.Register("LUTManager",            SB::SystemCategory::Compute);
    s_shID_taaMgr        = sh.Register("TAAManager",            SB::SystemCategory::Compute);

    // Rendering
    s_shID_toneMap       = sh.Register("ToneMapManager",        SB::SystemCategory::Rendering);
    s_shID_atmosphere    = sh.Register("AtmosphereRenderer",    SB::SystemCategory::Rendering);
    s_shID_matClassifier = sh.Register("MaterialClassifier",    SB::SystemCategory::Rendering);
    s_shID_gtao          = sh.Register("GTAORenderer",          SB::SystemCategory::Rendering);
    s_shID_contactShadow = sh.Register("ContactShadowRenderer", SB::SystemCategory::Rendering);
    s_shID_skylighting   = sh.Register("SkylightingRenderer",   SB::SystemCategory::Rendering);
    s_shID_ssr           = sh.Register("SSRRenderer",           SB::SystemCategory::Rendering);
    s_shID_sdsm          = sh.Register("SDSMCascades",          SB::SystemCategory::Rendering);
    s_shID_ssgi          = sh.Register("SSGIRenderer",          SB::SystemCategory::Rendering);
    s_shID_volClouds     = sh.Register("VolumetricClouds",      SB::SystemCategory::Rendering);
    s_shID_frameGen      = sh.Register("FrameGenerator",        SB::SystemCategory::Rendering);
    s_shID_temporalSR    = sh.Register("TemporalSuperRes",      SB::SystemCategory::Rendering);
    s_shID_motionVec     = sh.Register("MotionVectorGen",       SB::SystemCategory::Rendering);
    s_shID_clusteredLight= sh.Register("ClusteredLighting",     SB::SystemCategory::Rendering);
    s_shID_grassLight    = sh.Register("GrassLightingRenderer",SB::SystemCategory::Rendering);
    s_shID_treeLodLight  = sh.Register("TreeLODLightingRenderer",SB::SystemCategory::Rendering);
    s_shID_waterBlend    = sh.Register("WaterBlendingRenderer",SB::SystemCategory::Rendering);
    s_shID_dynCubemap    = sh.Register("DynamicCubemapRenderer",SB::SystemCategory::Rendering);

    // New pipeline systems (Tier 1-4)
    s_shID_bloom         = sh.Register("BloomRenderer",         SB::SystemCategory::Rendering);
    s_shID_colorPipeline = sh.Register("ColorPipeline",         SB::SystemCategory::Rendering);
    s_shID_denoise       = sh.Register("DenoiseManager",        SB::SystemCategory::Rendering);
    s_shID_dof           = sh.Register("DoFRenderer",           SB::SystemCategory::Rendering);
    s_shID_lens          = sh.Register("LensRenderer",          SB::SystemCategory::Rendering);
    s_shID_underwater    = sh.Register("UnderwaterRenderer",    SB::SystemCategory::Rendering);

    // Debug
    s_shID_debugRenderer = sh.Register("DebugRenderer",         SB::SystemCategory::Debug);
    s_shID_perfMonitor   = sh.Register("PerfMonitor",           SB::SystemCategory::Debug);

    // Proxy
    s_shID_proxyDiag     = sh.Register("ProxyDiagnostics",      SB::SystemCategory::Proxy);
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
        // Clamp to sane range
        if (dt < 0.0001f) dt = 0.0001f;
        if (dt > 0.5f) dt = 0.5f;
    }

    s_lastFrame = now;
    s_hasLastFrame = true;
    return dt;
}

// ── SEH-safe wrapper for frame update ────────────────────────────────────────
// Access violations from bad CommonLibSSE casts at the main menu (where
// PlayerCharacter, Sky weather, etc. may not exist) are SEH exceptions,
// NOT C++ exceptions. try/catch won't catch them. This wrapper catches
// them and auto-disables the entire frame update until the game world loads.
static int s_frameAVCount = 0;
static bool s_frameUpdateDisabled = false;
static bool s_startupDiagsDone = false;

static void DoFrameUpdate();

// ── Frame update (called from Present hook) ─────────────────────────────────
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
        SKSE::log::error("Playground: standalone frame update AV #{}", s_frameAVCount);
        if (s_frameAVCount >= 5) {
            s_frameUpdateDisabled = true;
            SB::BootDiag::LogError("StandaloneFrameUpdate", "DISABLED after 5 AVs — no more data will flow");
            SB::BootDiag::DumpReport();  // Force dump NOW before we lose the chance
            SKSE::log::error("Playground: too many AVs — frame update disabled");
        }
    }
}

static void DoFrameUpdate()
{
    SB::BootDiag::BeginFrame(s_frameCount);

    float dt = GetDeltaTime();

    // GPU timing: begin frame measurement
    if (SB::PerfMonitor::Get().IsInitialized())
        SB::PerfMonitor::Get().BeginFrame();

    // ── Loading screen detection ───────────────────────────────────────────
    // During loading screens, singletons are valid but cell-dependent data
    // (lights, NPCs, regions, crosshair) may point to stale/transitional state.
    // Skip expensive cell-iterating trackers to avoid wasted work and edge cases.
    bool isLoading = false;
    if (auto* ui = RE::UI::GetSingleton())
        isLoading = ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);

    // ── Collect all game state ─────────────────────────────────────────────
    // Self-healing: each tracker is wrapped in try/catch with health tracking.
    // After kDisableThreshold consecutive errors a tracker is auto-disabled,
    // then retried every kRetryInterval frames. Recovery is logged.
    SB::AllData data{};

    #define SB_SAFE_UPDATE(field, expr, id, label)                              \
        if (s_trackerHealth[id].ShouldRun(s_frameCount)) {                      \
            try {                                                               \
                field = expr;                                                   \
                s_trackerHealth[id].OnSuccess(label);                           \
            }                                                                   \
            catch (const std::exception& e) {                                   \
                s_trackerHealth[id].OnError(s_frameCount, label);               \
                SKSE::log::error("Playground: {} threw: {}", label, e.what());\
            }                                                                   \
            catch (...) {                                                       \
                s_trackerHealth[id].OnError(s_frameCount, label);               \
                SKSE::log::error("Playground: {} threw unknown", label);      \
            }                                                                   \
        }

    // Phase 1: Core trackers (domains 1-10) — always run
    SB_SAFE_UPDATE(data.celestial,  SB::CelestialTracker::Update(),     kTrkCelestial,  "CelestialTracker");
    SB_SAFE_UPDATE(data.atmosphere, SB::AtmosphereTracker::Update(),    kTrkAtmosphere, "AtmosphereTracker");
    SB_SAFE_UPDATE(data.fog,        SB::FogTracker::Update(),           kTrkFog,        "FogTracker");
    SB_SAFE_UPDATE(data.weather,    SB::WeatherTracker::Update(dt),     kTrkWeather,    "WeatherTracker");
    SB_SAFE_UPDATE(data.player,     SB::PlayerTracker::Update(),        kTrkPlayer,     "PlayerTracker");
    SB_SAFE_UPDATE(data.camera,     SB::CameraTracker::Update(),        kTrkCamera,     "CameraTracker");
    SB_SAFE_UPDATE(data.interior,   SB::InteriorTracker::Update(),      kTrkInterior,   "InteriorTracker");
    SB_SAFE_UPDATE(data.shadow,     SB::ShadowTracker::Update(),        kTrkShadow,     "ShadowTracker");
    SB_SAFE_UPDATE(data.effects,    SB::EffectsTracker::Update(),       kTrkEffects,    "EffectsTracker");
    SB_SAFE_UPDATE(data.render,     SB::RenderTracker::Update(dt),      kTrkRender,     "RenderTracker");

    // v2 expansion trackers (domains 11-17)
    SB_SAFE_UPDATE(data.imageSpace,  SB::ImageSpaceTracker::Update(),   kTrkImageSpace, "ImageSpaceTracker");
    SB_SAFE_UPDATE(data.uiState,     SB::UIStateTracker::Update(),      kTrkUIState,    "UIStateTracker");
    SB_SAFE_UPDATE(data.equipment,   SB::EquipmentTracker::Update(),    kTrkEquipment,  "EquipmentTracker");

    // Cell-dependent trackers — skip during loading screens
    if (!isLoading) {
        SB_SAFE_UPDATE(data.lights,      SB::LightTracker::Update(),        kTrkLights,     "LightTracker");
        SB_SAFE_UPDATE(data.actorValues, SB::ActorValueTracker::Update(),   kTrkActorValues,"ActorValueTracker");
        SB_SAFE_UPDATE(data.crosshair,   SB::CrosshairTracker::Update(),    kTrkCrosshair,  "CrosshairTracker");
        SB_SAFE_UPDATE(data.quest,       SB::QuestTracker::Update(),        kTrkQuest,      "QuestTracker");
        SB_SAFE_UPDATE(data.region,      SB::RegionTracker::Update(),       kTrkRegion,     "RegionTracker");
        SB_SAFE_UPDATE(data.npcDetect,   SB::NPCDetectTracker::Update(),    kTrkNPCDetect,  "NPCDetectTracker");
    }

    // Atmosphere: update LUTs when sun position changes
    if (SB::AtmosphereRenderer::Get().IsInitialized()) {
        float sunZenithCos = data.celestial.SunDirection.y;  // Y = up component = cos(zenith)
        float sunAzimuth = std::atan2(data.celestial.SunDirection.x, data.celestial.SunDirection.z);
        SB::AtmosphereRenderer::Get().UpdateLUTs(sunZenithCos, sunAzimuth);
    }

    // Material classifier: begin frame (clear buffer)
    if (SB::MaterialClassifier::Get().IsInitialized()) {
        SB::MaterialClassifier::Get().BeginFrame(D3D11Hook::GetContext());
    }

    // Clustered lighting: collect lights from scene graph
    if (SB::ClusteredLighting::Get().IsInitialized() && SB::ClusteredLighting::Get().IsEnabled()) {
        try {
            SB::ClusteredLighting::Get().CollectLights();
        } catch (...) {
            SKSE::log::error("Playground: ClusteredLighting::CollectLights threw");
        }
    }

    // v3 expansion trackers (domains 19-22) — always run
    SB_SAFE_UPDATE(data.audio,       SB::AudioTracker::Update(),        kTrkAudio,      "AudioTracker");
    SB_SAFE_UPDATE(data.perf,        SB::PerfMonitor::Get().GetData(),  kTrkPerfMonitor,"PerfMonitor");
    if (SB::SceneObserver::Get().IsInstalled())
        SB_SAFE_UPDATE(data.scene,   SB::SceneObserver::Get().Update(), kTrkScene,      "SceneObserver");

    #undef SB_SAFE_UPDATE

    // ── Post-tracker pipeline (also protected) ────────────────────────────
    // Merge feedback from previous frame (1-frame delay GPU read-back)
    try {
        SB::FeedbackProcessor::Get().DistributeFeedback(data);
    } catch (...) {
        SKSE::log::error("Playground: FeedbackProcessor threw");
    }

    // Domain A: Write game state back (FOV, fog, lights, etc.)
    try {
        SB::WriteBackProcessor::Get().Execute(data);
    } catch (...) {
        SKSE::log::error("Playground: WriteBackProcessor threw");
    }

    // Sanitize all float4s BEFORE building matrices or dispatching GPU passes.
    // This prevents NaN/Inf from camera/celestial trackers from propagating into
    // projection matrices and compute shader constant buffers.
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

    // Update debug GUI with current data + tracker health
    SB::DebugGUI::SetData(data);
    {
        static const char* kTrackerNames[kTrkCount] = {
            "Celestial", "Atmosphere", "Fog", "Weather",
            "Player", "Camera", "Interior", "Shadow",
            "Effects", "Render",
            "ImageSpace", "Lights", "ActorValues", "Crosshair",
            "Equipment", "Quest", "UIState",
            "Region", "Audio", "NPCDetect", "PerfMonitor", "Scene"
        };
        SB::DebugGUI::TrackerHealthInfo info[kTrkCount];
        for (int i = 0; i < kTrkCount; ++i) {
            info[i].name = kTrackerNames[i];
            info[i].consecutiveErrors = s_trackerHealth[i].consecutiveErrors;
            info[i].totalErrors = s_trackerHealth[i].totalErrors;
            info[i].disabled = s_trackerHealth[i].disabled;
        }
        SB::DebugGUI::SetTrackerHealth(info, kTrkCount);
    }

    // Feed shader pre-processor stats to DebugGUI
    {
        auto& pp = SB::ShaderPreProcessor::Get();
        auto& db = pp.GetDatabase();
        auto& eb = SB::ExternBindingProcessor::Get();
        auto& ws = SB::WeatherSeparationEngine::Get();
        SB::DebugGUI::PreProcessorStats ppStats;
        ppStats.processCount      = pp.GetProcessCount();
        ppStats.cacheHits         = pp.GetCacheHits();
        ppStats.parameterCount    = db.GetParameterCount();
        ppStats.separatedCount    = db.GetSeparatedCount();
        ppStats.shaderCount       = db.GetShaderCount();
        ppStats.externBindingCount = eb.GetBindingCount();
        ppStats.externPushCount   = eb.GetPushCount();
        ppStats.weatherSepCount   = ws.GetSeparatedParamCount();
        ppStats.weatherINICount   = ws.GetLoadedWeatherCount();
        SB::DebugGUI::SetPreProcessorStats(ppStats);
    }

    // ── Per-frame diagnostics heartbeat ─────────────────────────────────
    {
        auto& sh = SB::SystemHealth::Get();

        // Tracker heartbeats + sync with TrackerHealth
        for (int i = 0; i < kTrkCount; ++i) {
            if (!s_trackerHealth[i].disabled) {
                sh.Heartbeat(s_shID_trackers[i]);
                if (s_trackerHealth[i].totalErrors > 0)
                    sh.ReportWarning(s_shID_trackers[i], "has errors");
            } else {
                sh.SetEnabled(s_shID_trackers[i], false);
            }
        }

        // Heartbeat always-running systems
        sh.Heartbeat(s_shID_feedbackProc);
        sh.Heartbeat(s_shID_writeBack);
        sh.Heartbeat(s_shID_weatherParam);

        // Update proxy diagnostics snapshot
        SB::ProxyDiagnostics::Get().Update();
        if (SB::ProxyDiagnostics::Get().IsConnected())
            sh.Heartbeat(s_shID_proxyDiag);

        // Evaluate all system health (recompute green/yellow/red)
        sh.EvaluateAll();
    }

    // Write to shared memory (inter-process data sharing)
    try {
        SB::SharedMemoryBridge::Get().WriteFrame(data, dt, s_frameCount);
    } catch (...) {
        SKSE::log::error("Playground: SharedMemoryBridge::WriteFrame threw");
    }
    ++s_frameCount;

    // Update Papyrus data cache for script consumers
    try {
        SB::PapyrusBridge::UpdateCache(data);
    } catch (...) {
        SKSE::log::error("Playground: PapyrusBridge::UpdateCache threw");
    }

    // GPU timing: end frame measurement
    if (SB::PerfMonitor::Get().IsInitialized())
        SB::PerfMonitor::Get().EndFrame();

    // Feed TAA jitter from RenderTracker to TAAManager for the resolve pass
    if (SB::TAAManager::Get().IsInitialized())
        SB::TAAManager::Get().SetJitter(data.render.Jitter.x, data.render.Jitter.y);

    // ── Debug visualization updates ─────────────────────────────────────────
    // Submit debug geometry to DebugRenderer (flushed at PrePresent by pipeline)
    if (SB::DebugRenderer::Get().IsInitialized() && SB::DebugRenderer::Get().IsEnabled()) {
        try {
            SB::NavMeshVisualizer::Get().Update();
        } catch (...) {
            SKSE::log::error("Playground: NavMeshVisualizer::Update threw");
        }
        try {
            SB::SkeletonVisualizer::Get().Update();
        } catch (...) {
            SKSE::log::error("Playground: SkeletonVisualizer::Update threw");
        }
    }

    SB::BootDiag::EndFrame();

    // GPU compute: build Hi-Z pyramid (needs game depth)
    if (SB::HiZPyramid::Get().IsInitialized() && SB::HiZPyramid::Get().IsEnabled()) {
        auto* ctx = D3D11Hook::GetContext();
        if (ctx) SB::HiZPyramid::Get().BuildPyramid(ctx);
    }

    // SDSM: analyze depth distribution and optimize shadow cascade distance
    if (SB::SDSMCascades::Get().IsInitialized() && SB::SDSMCascades::Get().IsEnabled()) {
        auto* ctx = D3D11Hook::GetContext();
        if (ctx) SB::SDSMCascades::Get().Update(ctx);
    }

    // GPU compute: clustered lighting dispatch (uses camera matrices from this frame)
    if (SB::ClusteredLighting::Get().IsInitialized() && SB::ClusteredLighting::Get().IsEnabled()) {
        auto* ctx = D3D11Hook::GetContext();
        if (ctx) {
            // Build view/proj from NiCamera (same approach as DebugRenderer::UpdateViewProj)
            DirectX::XMFLOAT4X4 viewMat, projMat;
            DirectX::XMStoreFloat4x4(&viewMat, DirectX::XMMatrixIdentity());
            DirectX::XMStoreFloat4x4(&projMat, DirectX::XMMatrixIdentity());

            if (auto* niCamera = RE::Main::WorldRootCamera()) {
                const auto& camRT  = niCamera->GetRuntimeData();
                const auto& camRT2 = niCamera->GetRuntimeData2();

                // NiCamera::worldToCam → View matrix
                DirectX::XMMATRIX view;
                for (int row = 0; row < 4; ++row)
                    for (int col = 0; col < 4; ++col)
                        view.r[row].m128_f32[col] = camRT.worldToCam[row][col];
                DirectX::XMStoreFloat4x4(&viewMat, view);

                // NiCamera viewFrustum planes → Projection matrix
                float l = camRT2.viewFrustum.fLeft;
                float r = camRT2.viewFrustum.fRight;
                float t = camRT2.viewFrustum.fTop;
                float b = camRT2.viewFrustum.fBottom;
                float n = camRT2.viewFrustum.fNear;
                float f = camRT2.viewFrustum.fFar;
                if (std::abs(r - l) > 1e-6f && std::abs(t - b) > 1e-6f && std::abs(f - n) > 1e-6f)
                    DirectX::XMStoreFloat4x4(&projMat, DirectX::XMMatrixPerspectiveOffCenterLH(l, r, b, t, n, f));
            }

            float nearZ = data.camera.Params.y;   // near plane from CameraTracker
            float farZ  = data.camera.Params.z;   // far plane
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

    // Weather Editor: detect weather changes, auto-load presets, auto-apply edits
    try {
        SB::WeatherEditor::Get().Update();
    } catch (...) {
        SKSE::log::error("Playground: WeatherEditor::Update threw");
    }

    // ── Periodic health report ─────────────────────────────────────────────
    // Log tracker health status at frame 600, then every 18000 frames (~5 min)
    if (s_frameCount == 600 || (s_frameCount > 0 && (s_frameCount % 18000) == 0)) {
        int disabledCount = 0;
        int totalErrors = 0;
        for (int i = 0; i < kTrkCount; ++i) {
            if (s_trackerHealth[i].disabled) disabledCount++;
            totalErrors += s_trackerHealth[i].totalErrors;
        }
        if (totalErrors > 0 || disabledCount > 0) {
            SKSE::log::info("Playground: health report at frame {} — {} trackers disabled, {} total errors",
                s_frameCount, disabledCount, totalErrors);
        }
    }

    // ── On-screen startup diagnostics ─────────────────────────────────────
    // Fire once ~1 second after game world loads (frame 60).
    if (!s_startupDiagsDone && s_frameCount == 60) {
        s_startupDiagsDone = true;

        char buf[256];

        // 1) ShaderCache stats
        auto& sc = SB::ShaderCache::Get();
        snprintf(buf, sizeof(buf), "PG: ShaderCache %u hits, %u misses, %u stored",
            sc.GetHitCount(), sc.GetMissCount(), sc.GetStoreCount());
        RE::DebugNotification(buf);

        // 2) Tracker health
        int disabledCount = 0;
        int errorTrackers = 0;
        for (int i = 0; i < kTrkCount; ++i) {
            if (s_trackerHealth[i].disabled) disabledCount++;
            if (s_trackerHealth[i].totalErrors > 0) errorTrackers++;
        }
        snprintf(buf, sizeof(buf), "PG: %d/%d trackers OK (%d disabled, %d with errors)",
            kTrkCount - disabledCount, kTrkCount, disabledCount, errorTrackers);
        RE::DebugNotification(buf);

        // 3) EditorID cache
        snprintf(buf, sizeof(buf), "PG: EditorIDCache=%zu entries",
            SB::EditorIDCache::Get().Size());
        RE::DebugNotification(buf);

        // 4) Pipeline status
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
    SKSE::log::info("Playground: received message type {}", a_msg->type);

    switch (a_msg->type) {
    case SKSE::MessagingInterface::kPostLoad:
        // Install EditorID cache hooks BEFORE data loading begins
        SB::EditorIDCache::Get().Install();
        break;

    case SKSE::MessagingInterface::kPostPostLoad:
        // Initialize debug GUI
        SB::DebugGUI::Init();
        SKSE::log::info("Playground: debug GUI initialized");
        break;

    case SKSE::MessagingInterface::kDataLoaded:
        // NOTE: s_gameReady is set at the END of this block, after all subsystems
        // are initialized. This prevents the frame update from running trackers while
        // D3D11Hook, FeedbackProcessor, SceneObserver, etc. are still initializing.
        SKSE::log::info("Playground: game data loaded — initializing subsystems");

        // Boot diagnostics: start recording everything
        SB::BootDiag::Init();

        // Register all systems with SystemHealth (must be first)
        RegisterAllSystemHealth();

        // Initialize shared memory bridge (inter-process data sharing)
        if (SB::SharedMemoryBridge::Get().Initialize()) {
            SKSE::log::info("Playground: shared memory bridge active");
        }

        // Domain A: Initialize write-back processor (no D3D11 dependency)
        {
            auto configDir = std::filesystem::path("Data/SKSE/Plugins/Playground");
            SB::WriteBackProcessor::Get().LoadConfig(configDir);
            SKSE::log::info("Playground: WriteBackProcessor initialized ({} rules, {} enabled)",
                SB::WriteBackProcessor::Get().GetRuleCount(),
                SB::WriteBackProcessor::Get().GetEnabledRuleCount());
        }

        // Initialize shader bytecode cache (no D3D11 dependency, must precede ShaderDebug)
        {
            auto cacheDir = std::filesystem::path("Data/SKSE/Plugins/Playground/ShaderCache");
            SB::ShaderCache::Get().Initialize(cacheDir);
        }

        // Initialize D3D11 hook for ImGui overlay
        if (D3D11Hook::Init()) {
            SB::BootDiag::LogInit("D3D11Hook", true, D3D11Hook::IsProxyActive() ? "proxy mode" : "legacy mode");
            SKSE::log::info("Playground: D3D11 hook initialized — press INSERT to toggle debug GUI");

            auto* dev = D3D11Hook::GetDevice();
            auto* ctx = D3D11Hook::GetContext();
            auto* sc  = D3D11Hook::GetSwapChain();

            SB::BootDiag::LogInfo("D3D11Hook", dev ? "Device OK" : "Device NULL");
            SB::BootDiag::LogInfo("D3D11Hook", ctx ? "Context OK" : "Context NULL");
            SB::BootDiag::LogInfo("D3D11Hook", sc ? "SwapChain OK" : "SwapChain NULL");

            // Initialize feedback processor (GPU read-back for luminance/scene analysis)
            if (dev && sc) {
                if (SB::FeedbackProcessor::Get().Initialize(dev, sc)) {
                    auto configDir = std::filesystem::path("Data/SKSE/Plugins/Playground");
                    SB::FeedbackProcessor::Get().LoadConfig(configDir);
                    SKSE::log::info("Playground: FeedbackProcessor active — center + grid luminance read-back");
                }
            }

            // Install ShaderDebug compilation diagnostics
            // IAT-hooks D3DCompile/D3DCompile2 to intercept all shader compilation
            if (dev && ctx && sc) {
                SB::Debug::ShaderDebug::Get().Install(dev, ctx, sc);
                SKSE::log::info("Playground: ShaderDebug installed — F10 toggle, F11 clear");
            }

            // GPU compute infrastructure + SRV injection
            if (dev && ctx && sc) {
                // ComputeManager: shader compilation + dispatch infrastructure
                SB::ComputeManager::Get().Initialize(dev, ctx);

                // SRVInjector: binds compute output textures at t17+ for rendering systems
                SB::SRVInjector::Get().Initialize(ctx);

                // RenderPassManager: fullscreen VS+PS+Draw pipeline for custom passes
                SB::RenderPassManager::Get().Initialize(dev, ctx);

                // LuminanceHistogram: GPU-parallel 256-bin histogram at t17
                if (SB::LuminanceHistogram::Get().Initialize(dev, sc)) {
                    SB::SRVInjector::Get().RegisterSRV(
                        SB::LuminanceHistogram::kSRVSlot,
                        SB::LuminanceHistogram::Get().GetHistogramSRV());
                    SKSE::log::info("Playground: LuminanceHistogram active at t{}",
                        SB::LuminanceHistogram::kSRVSlot);
                }

                // RenderPipeline: pass orchestration framework
                // MUST init before HiZ/SharedGPUResources so their PostGeometry
                // passes can register via pipeline.AddPass().
                SB::RenderPipeline::Get().Initialize(dev, ctx, sc);

                // HiZPyramid: hierarchical depth buffer at t19
                if (SB::HiZPyramid::Get().Initialize(dev, sc)) {
                    SB::SRVInjector::Get().RegisterSRV(
                        SB::HiZPyramid::kSRVSlot,
                        SB::HiZPyramid::Get().GetSRV());

                    // Register HiZ build as PostGeometry:1 so it runs BEFORE
                    // effects during mid-frame dispatch.  Effects can then use
                    // fresh HiZ data instead of falling back to raw depth.
                    auto& pipeline = SB::RenderPipeline::Get();
                    if (pipeline.IsInitialized()) {
                        pipeline.AddPass({
                            .name     = "HiZPyramid",
                            .stage    = SB::PipelineStage::PostGeometry,
                            .priority = 1,
                            .enabled  = true,
                            .execute  = [](SB::PassContext& ctx) {
                                auto& hiz = SB::HiZPyramid::Get();
                                if (hiz.IsInitialized() && hiz.IsEnabled())
                                    hiz.BuildPyramid(ctx.context);
                            },
                        });
                    }

                    SKSE::log::info("Playground: HiZPyramid active at t{} ({}x{}, {} mips)",
                        SB::HiZPyramid::kSRVSlot,
                        SB::HiZPyramid::Get().GetWidth(),
                        SB::HiZPyramid::Get().GetHeight(),
                        SB::HiZPyramid::Get().GetMipCount());
                }

                // SharedGPUResources: blue noise (t30), linearized depth (t31),
                // vanilla params CB (b7).  Must init after HiZ (reads HiZ mip 0).
                if (SB::SharedGPUResources::Get().Initialize(dev, ctx, sc)) {
                    SB::SRVInjector::Get().RegisterSRV(
                        SB::SharedGPUResources::kBlueNoiseSlot,
                        SB::SharedGPUResources::Get().GetBlueNoiseSRV());
                    SB::SRVInjector::Get().RegisterSRV(
                        SB::SharedGPUResources::kLinearDepthSlot,
                        SB::SharedGPUResources::Get().GetLinearDepthSRV());

                    // Register as PostGeometry:2 (after HiZ at :1, before effects at :15+)
                    auto& pipeline = SB::RenderPipeline::Get();
                    if (pipeline.IsInitialized()) {
                        pipeline.AddPass({
                            .name     = "SharedGPUResources",
                            .stage    = SB::PipelineStage::PostGeometry,
                            .priority = 2,
                            .enabled  = true,
                            .execute  = [](SB::PassContext& ctx) {
                                SB::SharedGPUResources::Get().Update(ctx.context);
                            },
                        });
                    }
                }

                // LUTManager: 3D film LUT injection at t18 + sampler s2
                {
                    auto lutDir = std::filesystem::path("Data/SKSE/Plugins/Playground/LUTs");
                    if (SB::LUTManager::Get().Initialize(dev, lutDir)) {
                        if (SB::LUTManager::Get().IsEnabled()) {
                            SB::SRVInjector::Get().RegisterSRV(
                                SB::LUTManager::kSRVSlot,
                                SB::LUTManager::Get().GetActiveSRV());
                            SB::SRVInjector::Get().RegisterSampler(
                                SB::LUTManager::kSamplerSlot,
                                SB::LUTManager::Get().GetSampler());
                            SKSE::log::info("Playground: LUTManager active at t{}/s{} ({} LUTs loaded)",
                                SB::LUTManager::kSRVSlot, SB::LUTManager::kSamplerSlot,
                                SB::LUTManager::Get().GetLUTCount());
                        }
                    }
                }

                // TAAManager: compute-based TAA resolve with persistent history at t22
                if (SB::TAAManager::Get().Initialize(dev, sc)) {
                    SB::SRVInjector::Get().RegisterSRV(
                        SB::TAAManager::kSRVSlot,
                        SB::TAAManager::Get().GetHistorySRV());
                    SB::SRVInjector::Get().RegisterSampler(
                        SB::TAAManager::kSamplerSlot,
                        SB::TAAManager::Get().GetSampler());
                    SKSE::log::info("Playground: TAAManager active at t{}/s{} ({}x{})",
                        SB::TAAManager::kSRVSlot, SB::TAAManager::kSamplerSlot,
                        SB::TAAManager::Get().GetWidth(),
                        SB::TAAManager::Get().GetHeight());
                }

                // PhaseDispatcher: mid-frame effect dispatch via proxy phase callbacks
                {
                    auto invalidateCache = D3D11Hook::GetInvalidateCacheFn();
                    bool ok = SB::PhaseDispatcher::Get().Initialize(ctx, invalidateCache);
                    SB::BootDiag::LogInit("PhaseDispatcher", ok);
                    if (ok) SKSE::log::info("Playground: PhaseDispatcher active (mid-frame dispatch)");
                }

                // PipelineTest: experimental fullscreen passes (vignette + film grain)
                SB::PipelineTest::Initialize();

                // ── Debug visualization overlays ────────────────────────────
                if (SB::DebugRenderer::Get().Initialize(dev, ctx)) {
                    SKSE::log::info("Playground: DebugRenderer active");
                }
                // NavMesh and Skeleton visualizers use DebugRenderer — no D3D init needed
                SKSE::log::info("Playground: NavMeshVisualizer ready (toggle via debug GUI)");
                SKSE::log::info("Playground: SkeletonVisualizer ready (toggle via debug GUI)");

                // ClusteredLighting: GPU forward+ lighting (Light Limit Fix replacement)
                { bool ok = SB::ClusteredLighting::Get().Initialize(dev, ctx);
                  SB::BootDiag::LogInit("ClusteredLighting", ok);
                  if (ok) SKSE::log::info("Playground: ClusteredLighting active");
                }

                // ── Rendering pipeline: 7 shader systems ──────────────────────

                // Feature 1: HDR tone mapping (autoexposure + AgX/ACES/PQ)
                { bool ok = SB::ToneMapManager::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("ToneMapManager", ok);
                  if (ok) SKSE::log::info("Playground: ToneMapManager active (curve={}, HDR={})",
                      static_cast<int>(SB::ToneMapManager::Get().GetToneCurve()),
                      SB::ToneMapManager::Get().IsHDROutput());
                }

                // Feature 2: Physically-based atmosphere (Rayleigh+Mie LUTs + celestials)
                { bool ok = SB::AtmosphereRenderer::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("AtmosphereRenderer", ok);
                  if (ok) SKSE::log::info("Playground: AtmosphereRenderer active (t{}/t{}/t{})",
                      SB::AtmosphereRenderer::kTransmittanceLUTSlot,
                      SB::AtmosphereRenderer::kScatteringLUTSlot,
                      SB::AtmosphereRenderer::kCelestialSRVSlot);
                }

                // Feature 3: Material classification buffer
                {
                    DXGI_SWAP_CHAIN_DESC scDesc;
                    if (SUCCEEDED(sc->GetDesc(&scDesc))) {
                        bool ok = SB::MaterialClassifier::Get().Initialize(
                                dev, ctx, scDesc.BufferDesc.Width, scDesc.BufferDesc.Height);
                        SB::BootDiag::LogInit("MaterialClassifier", ok);
                        if (ok) SKSE::log::info("Playground: MaterialClassifier active at t{}",
                                SB::MaterialClassifier::kMaterialSRVSlot);
                    }
                }

                // Feature 4a: Ground Truth Ambient Occlusion
                { bool ok = SB::GTAORenderer::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("GTAORenderer", ok);
                  if (ok) SKSE::log::info("Playground: GTAORenderer active at t{}",
                      SB::GTAORenderer::kSRVSlot);
                }

                // Feature 4a2: Screen-Space Contact Shadows (CS replacement)
                { bool ok = SB::ContactShadowRenderer::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("ContactShadowRenderer", ok);
                  if (ok) SKSE::log::info("Playground: ContactShadowRenderer active at t{}",
                      SB::ContactShadowRenderer::kSRVSlot);
                }

                // Feature 4a3: Skylighting (CS replacement)
                { bool ok = SB::SkylightingRenderer::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("SkylightingRenderer", ok);
                  if (ok) SKSE::log::info("Playground: SkylightingRenderer active at t{}",
                      SB::SkylightingRenderer::kSRVSlot);
                }

                // Feature 4b: SDSM cascade optimizer (depth histogram → shadow distance)
                { bool ok = SB::SDSMCascades::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("SDSMCascades", ok);
                  if (ok) SKSE::log::info("Playground: SDSMCascades active ({} bins)", 256u);
                }

                // Feature 4c: Screen-space reflections (Hi-Z ray march)
                { bool ok = SB::SSRRenderer::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("SSRRenderer", ok);
                  if (ok) SKSE::log::info("Playground: SSRRenderer active at t{}",
                      SB::SSRRenderer::kSRVSlot);
                }

                // Feature 4c: Screen-space global illumination
                { bool ok = SB::SSGIRenderer::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("SSGIRenderer", ok);
                  if (ok) SKSE::log::info("Playground: SSGIRenderer active");
                }

                // ── Default-disabled heavy systems: skip Init() to avoid blocking kDataLoaded ──
                // VolumetricClouds was hanging — D3DCompile with OPTIMIZATION_LEVEL3
                // on complex Worley+Perlin noise shaders blocks the main thread for minutes.
                SKSE::log::info("Playground: Skipping init for heavy deferred systems "
                    "(VolumetricClouds, FrameGenerator, TemporalSuperRes, MotionVectorGen, "
                    "GrassLighting, TreeLOD, WaterBlending, DynamicCubemaps, "
                    "VolumetricLighting, SubsurfaceScattering, IndirectSpecular, "
                    "ScreenSpaceDecals, ParticleLighting) "
                    "— will lazy-init on enable");
                SB::BootDiag::LogInit("DeferredHeavySystems", true, "deferred — 13 systems skipped");

                // ── New rendering pipeline: Tier 1-4 systems ─────────────────

                // DenoiseManager: shared denoising infrastructure (bilateral, à-trous, temporal)
                // Must init BEFORE systems that use it (GTAO, SSR, SSGI, ContactShadows)
                { bool ok = SB::DenoiseManager::Get().Initialize(dev, ctx);
                  SB::BootDiag::LogInit("DenoiseManager", ok);
                  if (ok) SKSE::log::info("Playground: DenoiseManager active");
                }

                // Tier 1: Core post-processing (most visible, no dependencies)

                // ColorPipeline: 12-stage Film+Grade color science (replaces ToneMapManager)
                { bool ok = SB::ColorPipeline::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("ColorPipeline", ok);
                  if (ok) SKSE::log::info("Playground: ColorPipeline active (12-stage film+grade)");
                }

                // BloomRenderer: FFT convolution + Jimenez downsample/upsample + GMM PSF
                { bool ok = SB::BloomRenderer::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("BloomRenderer", ok);
                  if (ok) SKSE::log::info("Playground: BloomRenderer active");
                }

                // Tier 2: Cinematic effects

                // DoFRenderer: Tile-classified CoC + N-gon bokeh + cat-eye + anamorphic
                { bool ok = SB::DoFRenderer::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("DoFRenderer", ok);
                  if (ok) SKSE::log::info("Playground: DoFRenderer active");
                }

                // LensRenderer: ABCD ghosts + spectral CA + Brown-Conrady + starburst
                { bool ok = SB::LensRenderer::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("LensRenderer", ok);
                  if (ok) SKSE::log::info("Playground: LensRenderer active");
                }

                // Tier 4: Situational

                // UnderwaterRenderer: Beer-Lambert absorption + caustics + Snell's window
                { bool ok = SB::UnderwaterRenderer::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("UnderwaterRenderer", ok);
                  if (ok) SKSE::log::info("Playground: UnderwaterRenderer active");
                }

                // Scene compositor: reads all compute outputs, composites onto backbuffer.
                // This is a simple fullscreen PS — safe to init here (no heavy shader compile).
                // MUST init AFTER all rendering systems that produce SRVs.
                { bool ok = SB::SceneCompositor::Get().Initialize(dev, ctx, sc);
                  SB::BootDiag::LogInit("SceneCompositor", ok, "default disabled");
                  if (ok) SKSE::log::info("Playground: SceneCompositor active (proxy-only composite)");
                }
            }

        } else {
            SKSE::log::warn("Playground: D3D11 hook failed — debug GUI unavailable");
        }

        // Phase 2: Initialize weather parameter computer
        // Looks for WeatherParams.ini and WeatherClasses.ini in the config directory.
        // MO2 virtualizes Data/, so this path resolves through the VFS.
        {
            auto configDir = std::filesystem::path("Data/SKSE/Plugins/Playground");
            SB::WeatherParameterComputer::Get().Initialize(configDir);
            SKSE::log::info("Playground: WeatherParameterComputer initialized (config: {})",
                configDir.string());
        }

        // Weather separation engine (per-weather per-ToD overrides from annotations)
        {
            auto configDir = std::filesystem::path("Data/SKSE/Plugins/Playground");
            SB::WeatherSeparationEngine::Get().Initialize(configDir);
        }

        // Weather Editor: real-time weather/lighting record editing + presets
        {
            auto presetDir = std::filesystem::path("Data/SKSE/Plugins/Playground/WeatherPresets");
            SB::WeatherEditor::Get().SetPresetDir(presetDir);
            SKSE::log::info("Playground: WeatherEditor ready — presets at {}",
                presetDir.string());
        }

        // Initialize PerfMonitor (GPU timing + performance governor)
        {
            auto* pmDev = D3D11Hook::GetDevice();
            auto* pmCtx = D3D11Hook::GetContext();
            if (pmDev && pmCtx) {
                if (SB::PerfMonitor::Get().Initialize(pmDev, pmCtx)) {
                    SKSE::log::info("Playground: PerfMonitor active — GPU timing + quality governor");
                }
            }
        }

        // Install scene composition observer (BeginTechnique hook)
        if (SB::SceneObserver::Get().Install()) {
            SKSE::log::info("Playground: SceneObserver active — material counting + shader state");
        }

        // Apply engine-level binary patches (BSSpinLock threshold reduction, etc.)
        {
            auto patches = SB::EngineFixes::Get().Install();
            if (patches > 0) {
                SKSE::log::info("Playground: {} engine patches applied", patches);
            }
        }

        // Register Papyrus native functions
        SB::PapyrusBridge::Register();

        // Report EditorID cache statistics
        SKSE::log::info("Playground: EditorID cache populated — {} editor IDs cached",
            SB::EditorIDCache::Get().Size());

        // Log initial game state for debugging
        if (auto* sky = RE::Sky::GetSingleton()) {
            SKSE::log::info("Playground: Sky OK — weather={}, masser={}, secunda={}",
                sky->currentWeather ? "yes" : "null",
                sky->masser ? "yes" : "null",
                sky->secunda ? "yes" : "null");
        }

        // Show compatibility notification if overlapping plugins detected
        {
            auto& compat = SB::CompatDetect::Get();
            auto note = compat.GetNotificationText();
            if (!note.empty()) {
                ShowNotification(note.c_str());
                SKSE::log::info("Playground: {}", note);
            }
        }

        // ── Report init state to SystemHealth ─────────────────────────────
        {
            auto& sh = SB::SystemHealth::Get();

            // Trackers: all considered initialized (they run lazily)
            for (int i = 0; i < kTrkCount; ++i)
                sh.SetInitialized(s_shID_trackers[i], true);

            // Backend
            sh.SetInitialized(s_shID_shaderCache,   SB::ShaderCache::Get().IsEnabled());
            sh.SetInitialized(s_shID_feedbackProc,   SB::FeedbackProcessor::Get().IsInitialized());
            sh.SetInitialized(s_shID_writeBack,      true);  // always succeeds (config-based)
            sh.SetInitialized(s_shID_sharedMemory,   SB::SharedMemoryBridge::Get().IsActive());
            sh.SetInitialized(s_shID_weatherParam,   true);
            sh.SetInitialized(s_shID_weatherSep,     true);
            sh.SetInitialized(s_shID_paramBinding,   true);
            sh.SetInitialized(s_shID_externBinding,  true);
            sh.SetInitialized(s_shID_shaderPreProc,  true);
            sh.SetInitialized(s_shID_weatherEditor,  true);

            // Integration
            sh.SetInitialized(s_shID_sceneObserver,  SB::SceneObserver::Get().IsInstalled());
            sh.SetInitialized(s_shID_engineFixes,    true);
            sh.SetInitialized(s_shID_papyrus,        true);
            sh.SetInitialized(s_shID_editorIDCache,  SB::EditorIDCache::Get().Size() > 0);
            sh.SetInitialized(s_shID_compatDetect,   true);

            // Pipeline
            sh.SetInitialized(s_shID_d3d11Hook,      D3D11Hook::GetDevice() != nullptr);
            sh.SetInitialized(s_shID_shaderDebug,     SB::Debug::ShaderDebug::Get().IsInstalled());
            sh.SetInitialized(s_shID_computeMgr,      SB::ComputeManager::Get().IsInitialized());
            sh.SetInitialized(s_shID_srvInjector,     true);
            sh.SetInitialized(s_shID_renderPipeline,  SB::RenderPipeline::Get().IsInitialized());

            // Compute
            sh.SetInitialized(s_shID_lumHistogram,   SB::LuminanceHistogram::Get().IsInitialized());
            sh.SetInitialized(s_shID_hiZPyramid,     SB::HiZPyramid::Get().IsInitialized());
            sh.SetInitialized(s_shID_lutManager,     SB::LUTManager::Get().IsEnabled());
            sh.SetInitialized(s_shID_taaMgr,         SB::TAAManager::Get().IsInitialized());

            // Rendering
            sh.SetInitialized(s_shID_toneMap,        SB::ToneMapManager::Get().IsInitialized());
            sh.SetInitialized(s_shID_atmosphere,     SB::AtmosphereRenderer::Get().IsInitialized());
            sh.SetInitialized(s_shID_matClassifier,  SB::MaterialClassifier::Get().IsInitialized());
            sh.SetInitialized(s_shID_gtao,           SB::GTAORenderer::Get().IsInitialized());
            sh.SetInitialized(s_shID_contactShadow,  SB::ContactShadowRenderer::Get().IsInitialized());
            sh.SetInitialized(s_shID_skylighting,    SB::SkylightingRenderer::Get().IsInitialized());
            sh.SetInitialized(s_shID_ssr,            SB::SSRRenderer::Get().IsInitialized());
            sh.SetInitialized(s_shID_sdsm,           SB::SDSMCascades::Get().IsInitialized());
            sh.SetInitialized(s_shID_ssgi,           SB::SSGIRenderer::Get().IsInitialized());
            sh.SetInitialized(s_shID_volClouds,      SB::VolumetricClouds::Get().IsInitialized());
            sh.SetInitialized(s_shID_frameGen,       SB::FrameGenerator::Get().IsInitialized());
            sh.SetInitialized(s_shID_temporalSR,     SB::TemporalSuperRes::Get().IsInitialized());
            sh.SetInitialized(s_shID_motionVec,      SB::MotionVectorGen::Get().IsInitialized());
            sh.SetInitialized(s_shID_clusteredLight,  SB::ClusteredLighting::Get().IsInitialized());
            sh.SetInitialized(s_shID_grassLight,      SB::GrassLightingRenderer::Get().IsInitialized());
            sh.SetInitialized(s_shID_treeLodLight,    SB::TreeLODLightingRenderer::Get().IsInitialized());
            sh.SetInitialized(s_shID_waterBlend,      SB::WaterBlendingRenderer::Get().IsInitialized());
            sh.SetInitialized(s_shID_dynCubemap,      SB::DynamicCubemapRenderer::Get().IsInitialized());

            // Debug
            sh.SetInitialized(s_shID_debugRenderer,  SB::DebugRenderer::Get().IsInitialized());
            sh.SetInitialized(s_shID_perfMonitor,    SB::PerfMonitor::Get().IsInitialized());

            // ProxyDiagnostics: connect to proxy
            bool proxyOk = SB::ProxyDiagnostics::Get().Connect();
            sh.SetInitialized(s_shID_proxyDiag, proxyOk);

            // Run initial compatibility probe
            SB::CompatibilityProbe::Get().RunProbe();
            auto probeSummary = SB::CompatibilityProbe::Get().GetSummary();
            SKSE::log::info("Playground: {}", probeSummary);

            // Initial health evaluation
            sh.EvaluateAll();
            SKSE::log::info("Playground: SystemHealth — {} green, {} yellow, {} red of {} systems",
                sh.GetGreenCount(), sh.GetYellowCount(), sh.GetRedCount(), sh.GetSystemCount());
        }

        // All subsystems initialized — NOW enable the per-frame callback.
        // This must be the last thing before break to prevent frame updates from
        // accessing uninitialized subsystems (D3D11Hook, FeedbackProcessor,
        // SceneObserver, ShaderDebug, etc.)
        s_gameReady.store(true, std::memory_order_release);
        SKSE::log::info("Playground: all subsystems ready");

        ShowNotification("Playground v1.0.0 - INSERT for debug GUI");
        break;

    case SKSE::MessagingInterface::kNewGame:
    case SKSE::MessagingInterface::kPostLoadGame:
        // Re-enable frame updates if they were disabled due to main-menu crashes.
        // The game world is now loaded — PlayerCharacter, Sky, etc. are valid.
        if (s_frameUpdateDisabled) {
            s_frameUpdateDisabled = false;
            s_frameAVCount = 0;
            SKSE::log::info("Playground: game world loaded — frame updates re-enabled");
        }
        // Reset startup diagnostics so they fire again for this game session
        s_frameCount = 0;
        s_startupDiagsDone = false;
        SKSE::log::info("Playground: entered game world");
        ShowNotification("Playground active");
        break;
    }
}

// ── SKSE plugin entry point ─────────────────────────────────────────────────
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);

    // Logging - try SKSE log directory first, fall back to Documents
    std::filesystem::path logPath;

    if (auto skseLogDir = SKSE::log::log_directory(); skseLogDir) {
        logPath = *skseLogDir / "Playground.log";
    } else {
        // Fallback: use user's Documents folder
        wchar_t* documentsPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documentsPath))) {
            logPath = std::filesystem::path(documentsPath) / "My Games" / "Skyrim Special Edition" / "SKSE" / "Playground.log";
            CoTaskMemFree(documentsPath);
        }
    }

    if (!logPath.empty()) {
        // Ensure parent directory exists
        std::filesystem::create_directories(logPath.parent_path());

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            logPath.string(), true);
        auto logger = std::make_shared<spdlog::logger>("Playground", std::move(sink));
        logger->set_level(spdlog::level::info);
        logger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));

        SKSE::log::info("Playground v3.0.0 loaded — {} parameters defined",
            SB::kParamCount);
        SKSE::log::info("Log path: {}", logPath.string());
    }

    // Install D3DCompile hooks ASAP — before ENB compiles its shaders.
    // This must happen in SKSEPlugin_Load, not kDataLoaded, because ENB
    // compiles shaders during early D3D11 initialization.
    SB::Debug::ShaderDebug::Get().InstallHooksEarly();

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        SKSE::log::critical("Playground: no SKSE messaging interface");
        return false;
    }
    messaging->RegisterListener(OnMessage);

    return true;
}
