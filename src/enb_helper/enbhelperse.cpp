//=============================================================================
//  enbhelperse.dll — ENB Helper for Skyrim SE
//
//  Minimal SKSE plugin that exports C functions ENB expects from enbhelperse.dll.
//  ENB's d3d11.dll finds this via GetModuleHandle after SKSE loads it.
//
//  Based on doodlum's ENB Helper SE reference implementation.
//  Part of SkyrimBridge v3.
//=============================================================================

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <ShlObj.h>
#include <atomic>
#include <filesystem>

static std::atomic<bool> s_loaded{false};

// ── Exported functions for ENB ───────────────────────────────────────────────

extern "C" __declspec(dllexport) bool IsLoaded()
{
    return s_loaded.load(std::memory_order_acquire);
}

extern "C" __declspec(dllexport) bool GetTime(float& time)
{
    if (auto* sky = RE::Sky::GetSingleton()) {
        time = sky->currentGameHour;
        return true;
    }
    return false;
}

static bool IsValidInterior(RE::PlayerCharacter* player)
{
    return player && player->parentCell &&
           player->parentCell->IsInteriorCell();
}

extern "C" __declspec(dllexport) bool GetWeatherTransition(float& t)
{
    auto* sky = RE::Sky::GetSingleton();
    if (!sky) return false;

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (IsValidInterior(player)) {
        t = sky->lightingTransition == 0.0f ? 1.0f : sky->lightingTransition;
    } else {
        t = sky->currentWeatherPct;
    }
    return true;
}

extern "C" __declspec(dllexport) bool GetCurrentWeather(unsigned long& id)
{
    auto* sky = RE::Sky::GetSingleton();
    if (!sky) return false;

    if (sky->currentWeather) {
        id = sky->currentWeather->GetFormID();
        return true;
    }
    return false;
}

extern "C" __declspec(dllexport) bool GetOutgoingWeather(unsigned long& id)
{
    auto* sky = RE::Sky::GetSingleton();
    if (!sky) return false;

    if (sky->lastWeather) {
        id = sky->lastWeather->GetFormID();
        return true;
    }
    return false;
}

extern "C" __declspec(dllexport) bool GetCurrentLocationID(unsigned long& id)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (player) {
        auto* loc = player->GetCurrentLocation();
        if (loc) {
            id = loc->GetFormID();
            return true;
        }
    }
    return false;
}

extern "C" __declspec(dllexport) bool GetWorldSpaceID(unsigned long& id)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (player && player->GetWorldspace()) {
        id = player->GetWorldspace()->GetFormID();
        return true;
    }
    return false;
}

extern "C" __declspec(dllexport) bool GetSkyMode(unsigned long& mode)
{
    auto* sky = RE::Sky::GetSingleton();
    if (sky) {
        mode = sky->mode.underlying();
        return true;
    }
    return false;
}

static int GetClassification(RE::TESWeather* weather)
{
    using Flags = RE::TESWeather::WeatherDataFlag;
    auto flags = weather->data.flags;

    if ((flags & Flags::kPleasant) != Flags::kNone) return 0;
    if ((flags & Flags::kCloudy)   != Flags::kNone) return 1;
    if ((flags & Flags::kRainy)    != Flags::kNone) return 2;
    if ((flags & Flags::kSnow)     != Flags::kNone) return 3;
    return -1;
}

extern "C" __declspec(dllexport) bool GetCurrentWeatherClassification(int& c)
{
    auto* sky = RE::Sky::GetSingleton();
    if (sky && sky->currentWeather) {
        c = GetClassification(sky->currentWeather);
        return true;
    }
    return false;
}

extern "C" __declspec(dllexport) bool GetOutgoingWeatherClassification(int& c)
{
    auto* sky = RE::Sky::GetSingleton();
    if (sky && sky->lastWeather) {
        c = GetClassification(sky->lastWeather);
        return true;
    }
    return false;
}

extern "C" __declspec(dllexport) bool GetPlayerCameraTransformMatrices(
    RE::NiTransform& m_local, RE::NiTransform& m_world, RE::NiTransform& m_oldworld)
{
    auto* cam = RE::PlayerCamera::GetSingleton();
    if (cam && cam->cameraRoot) {
        auto* node = cam->cameraRoot.get();
        std::memcpy(&m_local, &node->local, sizeof(RE::NiTransform));
        std::memcpy(&m_world, &node->world, sizeof(RE::NiTransform));
        std::memcpy(&m_oldworld, &node->previousWorld, sizeof(RE::NiTransform));
        return true;
    }
    return false;
}

// ── Additional exports ENB expects from enbhelperse.dll ─────────────────────
// ENB's d3d11.dll resolves these via GetProcAddress. Without them, ENB logs
// error messages: "ENBHELPERSE.DLL failed to work" or "not installed".
// Verified by string extraction from ENB v504 d3d11.dll binary.

extern "C" __declspec(dllexport) bool HelperPluginStarted()
{
    // ENB checks this to confirm the helper is alive and initialized.
    return s_loaded.load(std::memory_order_acquire);
}

extern "C" __declspec(dllexport) bool InitProxyFunctions()
{
    // ENB calls this when proxy functions need initialization.
    // For our implementation, initialization happens in SKSEPluginLoad.
    return s_loaded.load(std::memory_order_acquire);
}

extern "C" __declspec(dllexport) bool SelectableObjects()
{
    // ENB uses this for the object selection system (ShowObjectsWindow).
    // Returns false — we don't support ENB's object selection.
    return false;
}

// ── SKSE Plugin Entry Point ──────────────────────────────────────────────────

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);

    // Logging
    std::filesystem::path logPath;
    if (auto dir = SKSE::log::log_directory(); dir) {
        logPath = *dir / "enbhelperse.log";
    }
    if (!logPath.empty()) {
        std::filesystem::create_directories(logPath.parent_path());
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
        auto logger = std::make_shared<spdlog::logger>("enbhelperse", std::move(sink));
        logger->set_level(spdlog::level::info);
        logger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));
        SKSE::log::info("enbhelperse v1.0 (SkyrimBridge) loaded");
    }

    s_loaded.store(true, std::memory_order_release);
    return true;
}
