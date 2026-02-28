//=============================================================================
//  SkyrimBridge — Comprehensive game state → ENB shader pipeline
//
//  Reads every useful piece of Skyrim engine state via CommonLibSSE-NG
//  and pushes it to ENB shaders as external parameters, enabling
//  native-quality screenspace effects driven by ground-truth 3D data.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <chrono>
#include <filesystem>
#include <ShlObj.h>

#include "BridgeData.h"
#include "ENBInterface.h"
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

// ── Game readiness flag ─────────────────────────────────────────────────────
// ENB callbacks fire during D3D initialization, before game singletons exist.
// We must not access any RE:: singletons until kDataLoaded fires.
static bool s_gameReady = false;

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

// ── Per-frame callback from ENB ─────────────────────────────────────────────
void __stdcall OnENBFrame(int a_callbackType)
{
    // Only process on BeginFrame (before Present)
    if (a_callbackType != 1)
        return;

    // Don't access game data until Skyrim has fully initialized
    if (!s_gameReady)
        return;

    float dt = GetDeltaTime();

    // Collect all game state
    SB::AllData data{};

    data.celestial  = SB::CelestialTracker::Update();
    data.atmosphere = SB::AtmosphereTracker::Update();
    data.fog        = SB::FogTracker::Update();
    data.weather    = SB::WeatherTracker::Update(dt);
    data.player     = SB::PlayerTracker::Update();
    data.camera     = SB::CameraTracker::Update();
    data.interior   = SB::InteriorTracker::Update();
    data.shadow     = SB::ShadowTracker::Update();
    data.effects    = SB::EffectsTracker::Update();
    data.render     = SB::RenderTracker::Update(dt);

    // Update debug GUI with current data
    SB::DebugGUI::SetData(data);

    // Push to ENB if enabled
    if (SB::DebugGUI::IsDataPushEnabled()) {
        ENBInterface::PushAllData(data);
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
    SKSE::log::info("SkyrimBridge: received message type {}", a_msg->type);

    switch (a_msg->type) {
    case SKSE::MessagingInterface::kPostLoad:
        if (ENBInterface::Init()) {
            SKSE::log::info("SkyrimBridge: ENB SDK connected");
        } else {
            SKSE::log::info("SkyrimBridge: ENB not found — plugin inactive");
        }
        break;

    case SKSE::MessagingInterface::kPostPostLoad:
        if (ENBInterface::IsLoaded() && ENBInterface::SetCallbackFunction) {
            ENBInterface::SetCallbackFunction(OnENBFrame);
            SKSE::log::info("SkyrimBridge: registered per-frame callback"
                " ({} parameters x {} shaders = {} calls/frame)",
                SB::kParamCount,
                std::size(SB::kTargetShaders),
                SB::kParamCount * std::size(SB::kTargetShaders));

            // Initialize debug GUI
            SB::DebugGUI::Init();
            SKSE::log::info("SkyrimBridge: debug GUI initialized (ENB editor support: {})",
                ENBInterface::IsGUISupported() ? "yes" : "no");
        }
        break;

    case SKSE::MessagingInterface::kDataLoaded:
        // Game singletons are now safe to access
        s_gameReady = true;
        SKSE::log::info("SkyrimBridge: game data loaded — enabling ENB data push");

        // Initialize D3D11 hook for ImGui overlay
        if (D3D11Hook::Init()) {
            SKSE::log::info("SkyrimBridge: D3D11 hook initialized — press INSERT to toggle debug GUI");
        } else {
            SKSE::log::warn("SkyrimBridge: D3D11 hook failed — debug GUI unavailable");
        }

        // Log initial game state for debugging
        if (auto* sky = RE::Sky::GetSingleton()) {
            SKSE::log::info("SkyrimBridge: Sky OK — weather={}, masser={}, secunda={}",
                sky->currentWeather ? "yes" : "null",
                sky->masser ? "yes" : "null",
                sky->secunda ? "yes" : "null");
        }

        // Show in-game notification when data is loaded
        if (ENBInterface::IsLoaded()) {
            ShowNotification("SkyrimBridge v1.0.0 - ENB connected (INSERT for debug GUI)");
            SKSE::log::info("SkyrimBridge: displayed startup notification");
        } else {
            ShowNotification("SkyrimBridge v1.0.0 - No ENB (INSERT for debug GUI)");
        }
        break;

    case SKSE::MessagingInterface::kNewGame:
    case SKSE::MessagingInterface::kPostLoadGame:
        // Show notification when entering the game world
        SKSE::log::info("SkyrimBridge: entered game world (ENB={})",
            ENBInterface::IsLoaded() ? "yes" : "no");
        if (ENBInterface::IsLoaded()) {
            ShowNotification("SkyrimBridge active - pushing data to ENB");
        } else {
            ShowNotification("SkyrimBridge loaded - no ENB detected");
        }
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
        logPath = *skseLogDir / "SkyrimBridge.log";
    } else {
        // Fallback: use user's Documents folder
        wchar_t* documentsPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documentsPath))) {
            logPath = std::filesystem::path(documentsPath) / "My Games" / "Skyrim Special Edition" / "SKSE" / "SkyrimBridge.log";
            CoTaskMemFree(documentsPath);
        }
    }

    if (!logPath.empty()) {
        // Ensure parent directory exists
        std::filesystem::create_directories(logPath.parent_path());

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            logPath.string(), true);
        auto logger = std::make_shared<spdlog::logger>("SkyrimBridge", std::move(sink));
        logger->set_level(spdlog::level::info);
        logger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));

        SKSE::log::info("SkyrimBridge v1.0.0 loaded — {} parameters defined",
            SB::kParamCount);
        SKSE::log::info("Log path: {}", logPath.string());
    }

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        SKSE::log::critical("SkyrimBridge: no SKSE messaging interface");
        return false;
    }
    messaging->RegisterListener(OnMessage);

    return true;
}
