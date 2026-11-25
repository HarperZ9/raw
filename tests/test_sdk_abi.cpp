// test_sdk_abi.cpp: Wave 55 Verification Suite for Public C11/C++20 SDK (`raw_engine_sdk.h`) &
// 17-Subsystem Runtime Snapshot Bridge (`rt_sdk_bridge`).
// Compiles both as a standalone binary (RAW_SDK_ABI_STANDALONE_MAIN) and as part of ce_runtime_check.
// Original work, MIT.

#include "rt_sdk_bridge.h"

#include "rt_adaptive_audio_director.h"
#include "rt_engine_health.h"
#include "rt_frame_governor.h"
#include "rt_magic_physics.h"
#include "rt_npc_behavior.h"
#include "rt_telemetry_recorder.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace raw::rt {

namespace {

inline bool IsFiniteSnapshot(const RawUnifiedEngineSnapshot& s) noexcept {
    return std::isfinite(s.frameP95Ms) &&
           std::isfinite(s.frameMeanMs) &&
           std::isfinite(s.renderScale) &&
           std::isfinite(s.volumetricQuality01) &&
           std::isfinite(s.shadowDistanceScale) &&
           std::isfinite(s.floraLodScale) &&
           std::isfinite(s.giRayBudget01) &&
           std::isfinite(s.surfaceTempC) &&
           std::isfinite(s.snowpackM) &&
           std::isfinite(s.windBeaufort) &&
           std::isfinite(s.shelterPressure01) &&
           std::isfinite(s.torchDeployment01) &&
           std::isfinite(s.guardAlertnessMult) &&
           std::isfinite(s.fireDamageMult) &&
           std::isfinite(s.frostDamageMult) &&
           std::isfinite(s.shockDamageMult) &&
           std::isfinite(s.audioLpfCutoffHz) &&
           std::isfinite(s.audioReverbWetMult) &&
           std::isfinite(s.audioDuckGain) &&
           std::isfinite(s.craftingAsymptote) &&
           std::isfinite(s.aiViewConeDeg) &&
           std::isfinite(s.crimeAlarmRadiusU) &&
           std::isfinite(s.meleeDeathForceCap) &&
           std::isfinite(s.sneakMaxDistanceU);
}

uint32_t PopCount(uint32_t v) noexcept {
    uint32_t c = 0u;
    while (v != 0u) {
        v &= (v - 1u);
        ++c;
    }
    return c;
}

}  // namespace

int RunSdkAbiTests(void (*checkFn)(bool, const char*) = nullptr) {
    int localFail = 0;
    auto Check = [&](bool cond, const char* msg) {
        if (checkFn) {
            checkFn(cond, msg);
        } else {
            std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", msg);
        }
        if (!cond) ++localFail;
    };

    // -------------------------------------------------------------------------
    // Check 1: Planted-Failure ABI Guards (nullptr, undersized structSize=4, exact sizeof)
    // -------------------------------------------------------------------------
    {
        const int rcNull = RAW_GetUnifiedEngineSnapshot(nullptr);

        RawUnifiedEngineSnapshot undersized{};
        undersized.structSize = 4u;
        const int rcSmall = RAW_GetUnifiedEngineSnapshot(&undersized);

        RawUnifiedEngineSnapshot zeroSize{};
        zeroSize.structSize = 0u;
        const int rcZero = RAW_GetUnifiedEngineSnapshot(&zeroSize);

        RawUnifiedEngineSnapshot valid{};
        valid.structSize = static_cast<uint32_t>(sizeof(RawUnifiedEngineSnapshot));
        const int rcValid = RAW_GetUnifiedEngineSnapshot(&valid);

        // Verify function pointer typedefs match exact C ABI signatures
        PFN_RAW_GetSdkVersion            pfnVer  = &RAW_GetSdkVersion;
        PFN_RAW_GetUnifiedEngineSnapshot pfnSnap = &RAW_GetUnifiedEngineSnapshot;
        RawUnifiedEngineSnapshot viaFnPtr{};
        viaFnPtr.structSize = static_cast<uint32_t>(sizeof(RawUnifiedEngineSnapshot));
        const int rcFnPtr = pfnSnap(&viaFnPtr);

        std::printf("[SdkAbi] Check1 Guards: null=%d small4=%d zero=%d valid=%d fnPtr=%d sizeof=%zu\n",
                    rcNull, rcSmall, rcZero, rcValid, rcFnPtr, sizeof(RawUnifiedEngineSnapshot));

        Check(rcNull == 0 && rcSmall == 0 && rcZero == 0 &&
              rcValid == 1 && rcFnPtr == 1 &&
              pfnVer() == RAW_ENGINE_SDK_VERSION &&
              valid.structSize == static_cast<uint32_t>(sizeof(RawUnifiedEngineSnapshot)) &&
              valid.sdkVersion == RAW_ENGINE_SDK_VERSION,
              "SdkAbi planted-failure guards: nullptr & undersized structSize=4/0 rejected, exact sizeof accepted");
    }

    // -------------------------------------------------------------------------
    // Check 2: Version + Non-Zero Active Subsystem Mask + Live Populated Finite Fields
    // -------------------------------------------------------------------------
    {
        // Initialize live core runtime subsystems so their live telemetry flows through the bridge
        (void)EngineHealthInit();
        (void)FrameGovernorInit();
        (void)FlightRecorderInit("");
        (void)AudioDirectorInit();
        (void)NpcBehaviorInit();

        // Feed realistic telemetry into subsystems to verify live bridge propagation
        FrameGovernorOnPresent(16.6);
        FlightFrameSample sample{};
        sample.frameIndex = 1ull;
        sample.frameMs    = 14.5f;
        FlightRecorderRecordDirect(sample);

        MagicEnvInput menv{};
        menv.rainIntensity01 = 0.85f;
        menv.wetness01       = 0.90f;
        menv.thunderstorm01  = 0.80f;
        MagicPhysicsUpdateEnvironment(menv);

        NpcBehaviorEnvInput nenv{};
        nenv.gameHour     = 23.0f;
        nenv.season       = WorldSeason::Winter;
        nenv.surfaceTempC = -12.0f;
        nenv.windBeaufort = 8.0f;
        nenv.snow01       = 0.90f;
        NpcBehaviorUpdateEnv(nenv);

        RawUnifiedEngineSnapshot snap{};
        snap.structSize = static_cast<uint32_t>(sizeof(RawUnifiedEngineSnapshot));
        const int rc = RAW_GetUnifiedEngineSnapshot(&snap);
        const uint32_t activeCount = PopCount(snap.activeSubsystemMask);

        std::printf("[SdkAbi] Check2 LiveSnapshot: ver=0x%08X mask=0x%05X (%u/17) gmstMods=%u health=%d p95=%.2fms render=%.2f shelter=%.2f shock=%.2fx\n",
                    snap.sdkVersion, snap.activeSubsystemMask, activeCount,
                    snap.verifiedGmstModules, snap.healthScore,
                    double(snap.frameP95Ms), double(snap.renderScale),
                    double(snap.shelterPressure01), double(snap.shockDamageMult));

        Check(rc == 1 &&
              RAW_GetSdkVersion() == RAW_ENGINE_SDK_VERSION &&
              snap.sdkVersion == RAW_ENGINE_SDK_VERSION &&
              snap.activeSubsystemMask != 0u &&
              (snap.activeSubsystemMask & RAW_SUBSYS_ENGINE_HEALTH) != 0u &&
              (snap.activeSubsystemMask & RAW_SUBSYS_FRAME_GOVERNOR) != 0u &&
              (snap.activeSubsystemMask & RAW_SUBSYS_TELEMETRY_RECORDER) != 0u &&
              (snap.activeSubsystemMask & RAW_SUBSYS_AUDIO_DIRECTOR) != 0u &&
              (snap.activeSubsystemMask & RAW_SUBSYS_NPC_BEHAVIOR) != 0u &&
              (snap.activeSubsystemMask & RAW_SUBSYS_MAGIC_PHYSICS) != 0u &&
              snap.verifiedGmstModules >= 8u &&
              IsFiniteSnapshot(snap) &&
              snap.healthScore >= 0 && snap.healthScore <= 100 &&
              snap.renderScale >= 0.65f && snap.renderScale <= 1.0f &&
              snap.shockDamageMult > 1.20f &&
              snap.shelterPressure01 > 0.50f &&
              snap.totalRecordedFrames >= 1ull,
              "SdkAbi live snapshot: version matches 0x00010000, activeSubsystemMask populated, all fields finite & live");

        // Clean up initialized test subsystems
        NpcBehaviorShutdown();
        AudioDirectorShutdown();
        FlightRecorderShutdown();
        FrameGovernorShutdown();
        EngineHealthShutdown();
    }

    // -------------------------------------------------------------------------
    // Check 3: Disk JSON Snapshot Export & Temp File Cleanup
    // -------------------------------------------------------------------------
    {
        std::error_code ec;
        const std::filesystem::path tmpDir =
            std::filesystem::temp_directory_path(ec) / "raw_sdk_abi_test_w55";
        const std::filesystem::path jsonPath = tmpDir / "unified_sdk_snapshot.json";
        std::filesystem::remove_all(tmpDir, ec);

        const std::string initMsg = SdkBridgeInit(tmpDir.string());
        const bool exported       = SdkBridgeExportSnapshotJson(jsonPath.string().c_str());

        std::string content;
        if (exported) {
            std::ifstream in(jsonPath, std::ios::binary);
            std::ostringstream ss;
            ss << in.rdbuf();
            content = ss.str();
        }

        // Also test clean-stop flush via SdkBridgeShutdown()
        std::filesystem::remove(jsonPath, ec);
        SdkBridgeShutdown();
        const bool shutdownFlushed = std::filesystem::exists(jsonPath, ec) &&
                                     std::filesystem::file_size(jsonPath, ec) > 100u;

        // Clean up temp directory completely
        std::filesystem::remove_all(tmpDir, ec);
        const bool cleanedUp = !std::filesystem::exists(tmpDir, ec);

        const bool jsonWellFormed =
            !content.empty() &&
            content.front() == '{' &&
            content.find("\"sdkVersion\"") != std::string::npos &&
            content.find("\"activeSubsystemMask\"") != std::string::npos &&
            content.find("\"health\"") != std::string::npos &&
            content.find("\"framePacing\"") != std::string::npos &&
            content.find("\"world\"") != std::string::npos &&
            content.find("\"magic\"") != std::string::npos &&
            content.find("\"audio\"") != std::string::npos &&
            content.find("\"savesAndMods\"") != std::string::npos &&
            content.find("\"gmstRuntime\"") != std::string::npos;

        std::printf("[SdkAbi] Check3 JsonExport: init='%s' bytes=%zu shutdownFlush=%u cleaned=%u\n",
                    initMsg.c_str(), content.size(), shutdownFlushed ? 1u : 0u, cleanedUp ? 1u : 0u);

        Check(exported && jsonWellFormed && shutdownFlushed && cleanedUp,
              "SdkAbi disk JSON export: valid JSON with sdkVersion & all subsystem blocks, shutdown flush & cleanup OK");
    }

    return localFail;
}

}  // namespace raw::rt

#ifdef RAW_SDK_ABI_STANDALONE_MAIN
int main() {
    std::printf("=== RAW Wave 55: Unified Public C ABI SDK & 17-Subsystem Bridge Suite ===\n");
    const int fail = raw::rt::RunSdkAbiTests(nullptr);
    std::printf("=== RESULT: %s (failures=%d) ===\n", fail == 0 ? "ALL PASS" : "FAIL", fail);
    return fail == 0 ? 0 : 1;
}
#endif
