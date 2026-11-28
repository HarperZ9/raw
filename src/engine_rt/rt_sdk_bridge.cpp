// rt_sdk_bridge.cpp: Wave 55 17-Subsystem Unified Public C ABI Aggregator & JSON Snapshot Exporter (`raw::rt`).
// Original work, MIT.

#include "rt_sdk_bridge.h"

#include "rt_adaptive_audio_director.h"
#include "rt_ai_packages.h"
#include "rt_alchemy_enchant_gmst.h"
#include "rt_engine_health.h"
#include "rt_frame_governor.h"
#include "rt_locomotion_melee_gmst.h"
#include "rt_magic_physics.h"
#include "rt_memory_guards.h"
#include "rt_mod_load_audit.h"
#include "rt_npc_behavior.h"
#include "rt_physics_camera_gmst.h"
#include "rt_progression_economy.h"
#include "rt_save_papyrus_guard.h"
#include "rt_stealth_social_gmst.h"
#include "rt_telemetry_recorder.h"
#include "rt_world_rules_gmst.h"
#include "rt_world_sim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace raw::rt {
namespace {

inline float SafeFinite(float v, float fallback) noexcept {
    return std::isfinite(v) ? v : fallback;
}

struct SdkBridgeRuntime {
    std::mutex  mu;
    bool        initialized = false;
    std::string outDir;
} g_sdk;

bool AtomicWriteJsonFile(const std::string& outPath, const std::string& content) {
    if (outPath.empty()) return false;
    try {
        const std::filesystem::path dst(outPath);
        if (dst.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(dst.parent_path(), ec);
        }
        const std::string tmpPath = outPath + ".tmp";
        {
            std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!out.good()) return false;
        }
        std::error_code ec;
        std::filesystem::rename(tmpPath, dst, ec);
        if (ec) {
            std::filesystem::remove(dst, ec);
            ec.clear();
            std::filesystem::rename(tmpPath, dst, ec);
            if (ec) {
                std::ofstream direct(outPath, std::ios::binary | std::ios::trunc);
                if (!direct) return false;
                direct.write(content.data(), static_cast<std::streamsize>(content.size()));
                std::filesystem::remove(tmpPath, ec);
                return direct.good();
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

uint32_t PopCount32(uint32_t v) noexcept {
    uint32_t c = 0u;
    while (v != 0u) {
        v &= (v - 1u);
        ++c;
    }
    return c;
}

}  // namespace

std::string SdkBridgeInit(const std::string& outDir) {
    {
        std::lock_guard<std::mutex> lk(g_sdk.mu);
        g_sdk.initialized = true;
        g_sdk.outDir      = outDir;
    }
    RawUnifiedEngineSnapshot snap{};
    snap.structSize = static_cast<uint32_t>(sizeof(RawUnifiedEngineSnapshot));
    RAW_GetUnifiedEngineSnapshot(&snap);

    char buf[224];
    std::snprintf(buf, sizeof(buf),
                  "[SdkBridge] initialized (SDK v%u.%u.%u [0x%08X], structSize=%u, activeSubsystems=%u/17 [0x%05X], gmstModules=%u)",
                  RAW_ENGINE_SDK_VERSION_MAJOR, RAW_ENGINE_SDK_VERSION_MINOR, RAW_ENGINE_SDK_VERSION_PATCH,
                  snap.sdkVersion, snap.structSize,
                  PopCount32(snap.activeSubsystemMask), snap.activeSubsystemMask,
                  snap.verifiedGmstModules);
    return buf;
}

std::string SdkBridgeFormatSnapshotJson(const RawUnifiedEngineSnapshot& s) {
    std::ostringstream os;
    char buf[512];

    os << "{\n";
    std::snprintf(buf, sizeof(buf),
                  "  \"sdkVersion\": %u,\n"
                  "  \"sdkVersionHex\": \"0x%08X\",\n"
                  "  \"sdkVersionString\": \"%u.%u.%u\",\n"
                  "  \"structSize\": %u,\n"
                  "  \"activeSubsystemMask\": %u,\n"
                  "  \"activeSubsystemMaskHex\": \"0x%05X\",\n"
                  "  \"activeSubsystemCount\": %u,\n"
                  "  \"verifiedGmstModules\": %u,\n",
                  s.sdkVersion, s.sdkVersion,
                  RAW_ENGINE_SDK_VERSION_MAJOR, RAW_ENGINE_SDK_VERSION_MINOR, RAW_ENGINE_SDK_VERSION_PATCH,
                  s.structSize, s.activeSubsystemMask, s.activeSubsystemMask,
                  PopCount32(s.activeSubsystemMask), s.verifiedGmstModules);
    os << buf;

    std::snprintf(buf, sizeof(buf),
                  "  \"health\": {\n"
                  "    \"score\": %d,\n"
                  "    \"grade\": %d,\n"
                  "    \"activeHandles\": %u,\n"
                  "    \"workingSetMB\": %u\n"
                  "  },\n",
                  s.healthScore, s.healthGrade, s.activeHandles, s.workingSetMB);
    os << buf;

    std::snprintf(buf, sizeof(buf),
                  "  \"framePacing\": {\n"
                  "    \"frameP95Ms\": %.3f,\n"
                  "    \"frameMeanMs\": %.3f,\n"
                  "    \"hitchCount50ms\": %u,\n"
                  "    \"totalRecordedFrames\": %llu,\n"
                  "    \"governorRegime\": %u,\n"
                  "    \"renderScale\": %.4f,\n"
                  "    \"volumetricQuality01\": %.4f,\n"
                  "    \"shadowDistanceScale\": %.4f,\n"
                  "    \"floraLodScale\": %.4f,\n"
                  "    \"giRayBudget01\": %.4f\n"
                  "  },\n",
                  double(SafeFinite(s.frameP95Ms, 0.0f)),
                  double(SafeFinite(s.frameMeanMs, 0.0f)),
                  s.hitchCount50ms,
                  static_cast<unsigned long long>(s.totalRecordedFrames),
                  s.governorRegime,
                  double(SafeFinite(s.renderScale, 1.0f)),
                  double(SafeFinite(s.volumetricQuality01, 1.0f)),
                  double(SafeFinite(s.shadowDistanceScale, 1.0f)),
                  double(SafeFinite(s.floraLodScale, 1.0f)),
                  double(SafeFinite(s.giRayBudget01, 1.0f)));
    os << buf;

    std::snprintf(buf, sizeof(buf),
                  "  \"world\": {\n"
                  "    \"surfaceTempC\": %.2f,\n"
                  "    \"snowpackM\": %.4f,\n"
                  "    \"windBeaufort\": %.2f,\n"
                  "    \"shelterPressure01\": %.4f,\n"
                  "    \"torchDeployment01\": %.4f,\n"
                  "    \"guardAlertnessMult\": %.4f\n"
                  "  },\n",
                  double(SafeFinite(s.surfaceTempC, 12.0f)),
                  double(SafeFinite(s.snowpackM, 0.0f)),
                  double(SafeFinite(s.windBeaufort, 2.0f)),
                  double(SafeFinite(s.shelterPressure01, 0.0f)),
                  double(SafeFinite(s.torchDeployment01, 0.0f)),
                  double(SafeFinite(s.guardAlertnessMult, 1.0f)));
    os << buf;

    std::snprintf(buf, sizeof(buf),
                  "  \"magic\": {\n"
                  "    \"fireDamageMult\": %.4f,\n"
                  "    \"frostDamageMult\": %.4f,\n"
                  "    \"shockDamageMult\": %.4f,\n"
                  "    \"steamBurst\": %u,\n"
                  "    \"waterArc\": %u\n"
                  "  },\n",
                  double(SafeFinite(s.fireDamageMult, 1.0f)),
                  double(SafeFinite(s.frostDamageMult, 1.0f)),
                  double(SafeFinite(s.shockDamageMult, 1.0f)),
                  s.magicSteamBurst, s.magicWaterArc);
    os << buf;

    std::snprintf(buf, sizeof(buf),
                  "  \"audio\": {\n"
                  "    \"mood\": %d,\n"
                  "    \"lpfCutoffHz\": %.1f,\n"
                  "    \"reverbWetMult\": %.4f,\n"
                  "    \"duckGain\": %.4f,\n"
                  "    \"transitions\": %u\n"
                  "  },\n",
                  s.audioMood,
                  double(SafeFinite(s.audioLpfCutoffHz, 18000.0f)),
                  double(SafeFinite(s.audioReverbWetMult, 1.0f)),
                  double(SafeFinite(s.audioDuckGain, 1.0f)),
                  s.audioTransitions);
    os << buf;

    std::snprintf(buf, sizeof(buf),
                  "  \"savesAndMods\": {\n"
                  "    \"saveMaxStrings\": %u,\n"
                  "    \"worstSaveHealth\": %u,\n"
                  "    \"modPluginCount\": %u,\n"
                  "    \"modIssueCount\": %u,\n"
                  "    \"modAuditSeverity\": %u\n"
                  "  },\n",
                  s.saveMaxStrings, s.worstSaveHealth,
                  s.modPluginCount, s.modIssueCount, s.modAuditSeverity);
    os << buf;

    std::snprintf(buf, sizeof(buf),
                  "  \"gmstRuntime\": {\n"
                  "    \"verifiedModules\": %u,\n"
                  "    \"totalVerified\": %u,\n"
                  "    \"totalApplied\": %u,\n"
                  "    \"craftingAsymptote\": %.4f,\n"
                  "    \"aiViewConeDeg\": %.2f,\n"
                  "    \"crimeAlarmRadiusU\": %.2f,\n"
                  "    \"meleeDeathForceCap\": %.2f,\n"
                  "    \"sneakMaxDistanceU\": %.2f\n"
                  "  }\n",
                  s.verifiedGmstModules, s.gmstTotalVerified, s.gmstTotalApplied,
                  double(SafeFinite(s.craftingAsymptote, 2.25f)),
                  double(SafeFinite(s.aiViewConeDeg, 168.0f)),
                  double(SafeFinite(s.crimeAlarmRadiusU, 2400.0f)),
                  double(SafeFinite(s.meleeDeathForceCap, 48.0f)),
                  double(SafeFinite(s.sneakMaxDistanceU, 1850.0f)));
    os << buf;
    os << "}\n";
    return os.str();
}

bool SdkBridgeExportSnapshotJson(const char* path) {
    std::string targetPath;
    if (path && path[0] != '\0') {
        targetPath = path;
    } else {
        std::lock_guard<std::mutex> lk(g_sdk.mu);
        if (g_sdk.outDir.empty()) return false;
        targetPath = g_sdk.outDir + "\\unified_sdk_snapshot.json";
    }

    RawUnifiedEngineSnapshot snap{};
    snap.structSize = static_cast<uint32_t>(sizeof(RawUnifiedEngineSnapshot));
    if (!RAW_GetUnifiedEngineSnapshot(&snap)) return false;

    const std::string json = SdkBridgeFormatSnapshotJson(snap);
    return AtomicWriteJsonFile(targetPath, json);
}

void SdkBridgeShutdown() {
    std::string flushPath;
    {
        std::lock_guard<std::mutex> lk(g_sdk.mu);
        if (g_sdk.initialized && !g_sdk.outDir.empty()) {
            flushPath = g_sdk.outDir + "\\unified_sdk_snapshot.json";
        }
        g_sdk.initialized = false;
        g_sdk.outDir.clear();
    }
    if (!flushPath.empty()) {
        (void)SdkBridgeExportSnapshotJson(flushPath.c_str());
    }
}

}  // namespace raw::rt

extern "C" RAW_SDK_API uint32_t RAW_GetSdkVersion(void) {
    return RAW_ENGINE_SDK_VERSION;
}

extern "C" RAW_SDK_API int RAW_GetUnifiedEngineSnapshot(RawUnifiedEngineSnapshot* out) {
    if (!out) return 0;
    if (out->structSize < static_cast<uint32_t>(sizeof(RawUnifiedEngineSnapshot))) {
        return 0;
    }

    const uint32_t callerSize = out->structSize;
    std::memset(out, 0, sizeof(RawUnifiedEngineSnapshot));

    // Safe canonical defaults before populating live subsystem responses.
    out->structSize          = callerSize;
    out->sdkVersion          = RAW_ENGINE_SDK_VERSION;
    out->healthScore         = 100;
    out->healthGrade         = 0;
    out->frameP95Ms          = 16.6667f;
    out->frameMeanMs         = 16.6667f;
    out->renderScale         = 1.0f;
    out->volumetricQuality01 = 1.0f;
    out->shadowDistanceScale = 1.0f;
    out->floraLodScale       = 1.0f;
    out->giRayBudget01       = 1.0f;
    out->surfaceTempC        = 12.0f;
    out->windBeaufort        = 2.0f;
    out->guardAlertnessMult  = 1.0f;
    out->fireDamageMult      = 1.0f;
    out->frostDamageMult     = 1.0f;
    out->shockDamageMult     = 1.0f;
    out->audioLpfCutoffHz    = 18000.0f;
    out->audioReverbWetMult  = 0.85f;
    out->audioDuckGain       = 1.0f;
    out->craftingAsymptote   = 2.25f;
    out->aiViewConeDeg       = 168.0f;
    out->crimeAlarmRadiusU   = 2400.0f;
    out->meleeDeathForceCap  = 48.0f;
    out->sneakMaxDistanceU   = 1850.0f;

    uint32_t mask        = 0u;
    uint32_t gmstModules = 0u;
    uint32_t gmstVerSum  = 0u;
    uint32_t gmstAppSum  = 0u;

    // 1. rt_engine_health
    {
        raw::rt::RawEngineHealthReport rep{};
        if (RAW_GetEngineHealthReport(&rep)) {
            mask |= RAW_SUBSYS_ENGINE_HEALTH;
            out->healthScore = static_cast<int32_t>(rep.compositeScore);
            out->healthGrade = static_cast<int32_t>(rep.grade);
        }
    }

    // 2. rt_frame_governor
    {
        raw::rt::RawFrameGovernorState fg{};
        if (RAW_GetFrameGovernorState(&fg)) {
            mask |= RAW_SUBSYS_FRAME_GOVERNOR;
            out->renderScale         = raw::rt::SafeFinite(fg.renderScale, 1.0f);
            out->volumetricQuality01 = raw::rt::SafeFinite(fg.volumetricQuality01, 1.0f);
            out->shadowDistanceScale = raw::rt::SafeFinite(fg.shadowDistanceScale, 1.0f);
            out->floraLodScale       = raw::rt::SafeFinite(fg.floraLodScale, 1.0f);
            out->giRayBudget01       = raw::rt::SafeFinite(fg.giRayBudget01, 1.0f);
            out->governorRegime      = static_cast<uint32_t>(fg.regime);
        }
    }

    // 3. rt_telemetry_recorder
    {
        raw::rt::RawFlightRecorderStats fr{};
        if (RAW_GetFlightRecorderStats(&fr)) {
            mask |= RAW_SUBSYS_TELEMETRY_RECORDER;
            if (fr.ringFillCount > 0u) {
                out->frameP95Ms  = raw::rt::SafeFinite(fr.p95FrameMs, out->frameP95Ms);
                out->frameMeanMs = raw::rt::SafeFinite(fr.meanFrameMs, out->frameMeanMs);
            }
            out->hitchCount50ms      = std::max(out->hitchCount50ms, fr.hitchFramesOver50ms);
            out->totalRecordedFrames = fr.totalRecordedFrames;
        }
    }

    // 4. rt_adaptive_audio_director
    {
        raw::rt::RawAudioDirectorState ad{};
        if (RAW_GetAudioDirectorState(&ad)) {
            mask |= RAW_SUBSYS_AUDIO_DIRECTOR;
            out->audioMood          = static_cast<int32_t>(ad.activeMood);
            out->audioLpfCutoffHz   = raw::rt::SafeFinite(ad.lpfCutoffHz, 18000.0f);
            out->audioReverbWetMult = raw::rt::SafeFinite(ad.reverbWetMult, 0.85f);
            out->audioDuckGain      = raw::rt::SafeFinite(ad.dialogueDuckGain, 1.0f);
            out->audioTransitions   = ad.transitionCount;
        }
    }

    // 5. rt_npc_behavior
    {
        raw::rt::RawNpcBehaviorState nb{};
        if (RAW_GetNpcBehaviorState(&nb)) {
            mask |= RAW_SUBSYS_NPC_BEHAVIOR;
            out->shelterPressure01  = raw::rt::SafeFinite(nb.shelterPressure01, 0.0f);
            out->torchDeployment01  = raw::rt::SafeFinite(nb.torchDeploymentRatio, 0.0f);
            out->guardAlertnessMult = raw::rt::SafeFinite(nb.guardAlertnessMult, 1.0f);
        }
    }

    // 6. rt_save_papyrus_guard
    {
        raw::rt::RawSavePapyrusHealth sp{};
        if (RAW_GetSavePapyrusHealth(&sp)) {
            mask |= RAW_SUBSYS_SAVE_PAPYRUS_GUARD;
            out->saveMaxStrings  = sp.worstStringCount;
            out->worstSaveHealth = static_cast<uint32_t>(sp.worstSaveHealth);
            out->hitchCount50ms  = std::max(out->hitchCount50ms, sp.hitchCount);
            if (sp.totalFrames > 0ull && sp.p95FrameMs > 0.0f && (mask & RAW_SUBSYS_TELEMETRY_RECORDER) == 0u) {
                out->frameP95Ms = raw::rt::SafeFinite(sp.p95FrameMs, out->frameP95Ms);
            }
        }
    }

    // 7. rt_mod_load_audit
    {
        raw::rt::RawModAuditSummary ma{};
        if (RAW_GetModAuditSummary(&ma)) {
            mask |= RAW_SUBSYS_MOD_LOAD_AUDIT;
            out->modPluginCount   = static_cast<uint32_t>(ma.totalPlugins);
            out->modAuditSeverity = static_cast<uint32_t>(ma.severity);
            out->modIssueCount    = static_cast<uint32_t>(ma.missingMasterCount) +
                                    static_cast<uint32_t>(ma.outOfOrderMasterCount) +
                                    static_cast<uint32_t>(ma.corruptHeaderCount) +
                                    static_cast<uint32_t>(ma.oversizedEslCount) +
                                    static_cast<uint32_t>(ma.fullSlotOverflow) +
                                    static_cast<uint32_t>(ma.lightSlotOverflow);
        }
    }

    // 8. rt_magic_physics (GMST module #1)
    {
        raw::rt::RawMagicPhysicsState mp{};
        if (RAW_GetMagicPhysicsState(&mp)) {
            mask |= RAW_SUBSYS_MAGIC_PHYSICS;
            ++gmstModules;
            gmstVerSum += static_cast<uint32_t>(mp.gmstsVerified);
            gmstAppSum += static_cast<uint32_t>(mp.gmstsAppliedCount);
            out->fireDamageMult  = raw::rt::SafeFinite(mp.synergy.fireDamageMult, 1.0f);
            out->frostDamageMult = raw::rt::SafeFinite(mp.synergy.frostDamageMult, 1.0f);
            out->shockDamageMult = raw::rt::SafeFinite(mp.synergy.shockDamageMult, 1.0f);
            out->magicSteamBurst = static_cast<uint32_t>(mp.steamBurstFlag != 0u || mp.synergy.steamBurstFlag != 0u ? 1u : 0u);
            out->magicWaterArc   = static_cast<uint32_t>(mp.waterArcFlag != 0u || mp.synergy.waterArcConductFlag != 0u ? 1u : 0u);
        }
    }

    // 9. rt_progression_economy (GMST module #2)
    {
        raw::rt::RawProgressionEconomyState pe{};
        if (RAW_GetProgressionEconomyState(&pe)) {
            mask |= RAW_SUBSYS_PROGRESSION_ECONOMY;
            ++gmstModules;
            gmstVerSum += static_cast<uint32_t>(pe.gmstsVerified);
            gmstAppSum += static_cast<uint32_t>(pe.gmstsAppliedCount);
            out->craftingAsymptote = raw::rt::SafeFinite(pe.craftingAsymptoteCap, 2.25f);
        }
    }

    // 10. rt_ai_packages (GMST module #3)
    {
        raw::rt::RawAiPackagesState ap{};
        if (RAW_GetAiPackagesState(&ap)) {
            mask |= RAW_SUBSYS_AI_PACKAGES;
            ++gmstModules;
            gmstVerSum += static_cast<uint32_t>(ap.gmstsVerified);
            gmstAppSum += static_cast<uint32_t>(ap.gmstsAppliedCount);
            out->aiViewConeDeg = raw::rt::SafeFinite(ap.effectiveViewConeDeg, 168.0f);
        }
    }

    // 11. rt_world_rules_gmst (GMST module #4)
    {
        raw::rt::RawWorldRulesState wr{};
        if (RAW_GetWorldRulesState(&wr)) {
            mask |= RAW_SUBSYS_WORLD_RULES_GMST;
            ++gmstModules;
            gmstVerSum += static_cast<uint32_t>(wr.gmstsVerified);
            gmstAppSum += static_cast<uint32_t>(wr.gmstsAppliedCount);
            out->crimeAlarmRadiusU = raw::rt::SafeFinite(wr.effectiveWitnessRadius, 2400.0f);
        }
    }

    // 12. rt_physics_camera_gmst (GMST module #5)
    {
        raw::rt::RawPhysicsCameraState pc{};
        if (RAW_GetPhysicsCameraState(&pc)) {
            mask |= RAW_SUBSYS_PHYSICS_CAMERA_GMST;
            ++gmstModules;
            gmstVerSum += static_cast<uint32_t>(pc.gmstsVerified);
            gmstAppSum += static_cast<uint32_t>(pc.gmstsAppliedCount);
            out->meleeDeathForceCap = raw::rt::SafeFinite(pc.meleeDeathForceCap, 48.0f);
        }
    }

    // 13. rt_stealth_social_gmst (GMST module #6)
    {
        raw::rt::RawStealthSocialState ss{};
        if (RAW_GetStealthSocialState(&ss)) {
            mask |= RAW_SUBSYS_STEALTH_SOCIAL_GMST;
            ++gmstModules;
            gmstVerSum += static_cast<uint32_t>(ss.gmstsVerified);
            gmstAppSum += static_cast<uint32_t>(ss.gmstsAppliedCount);
            out->sneakMaxDistanceU = raw::rt::SafeFinite(ss.sneakMaxDistanceU, 1850.0f);
        }
    }

    // 14. rt_locomotion_melee_gmst (GMST module #7)
    {
        raw::rt::RawLocomotionMeleeState lm{};
        if (RAW_GetLocomotionMeleeState(&lm)) {
            mask |= RAW_SUBSYS_LOCOMOTION_MELEE_GMST;
            ++gmstModules;
            gmstVerSum += static_cast<uint32_t>(lm.gmstsVerified);
            gmstAppSum += static_cast<uint32_t>(lm.gmstsAppliedCount);
        }
    }

    // 15. rt_alchemy_enchant_gmst (GMST module #8)
    {
        raw::rt::RawAlchemyEnchantState ae{};
        if (RAW_GetAlchemyEnchantState(&ae)) {
            mask |= RAW_SUBSYS_ALCHEMY_ENCHANT_GMST;
            ++gmstModules;
            gmstVerSum += static_cast<uint32_t>(ae.gmstsVerified);
            gmstAppSum += static_cast<uint32_t>(ae.gmstsAppliedCount);
        }
    }

    // 16. rt_world_sim
    {
        raw::rt::RawWorldSimTelemetry ws{};
        if (RAW_GetWorldSimTelemetry(&ws)) {
            mask |= RAW_SUBSYS_WORLD_SIM;
            out->surfaceTempC = raw::rt::SafeFinite(ws.ambientTempC, 12.0f);
            // Convert Skyrim units (70 u ~= 1 meter) to physical snowpack meters
            out->snowpackM    = raw::rt::SafeFinite(std::max(0.0f, ws.snowDepthU) * (1.0f / 70.0f), 0.0f);
            out->windBeaufort = raw::rt::SafeFinite(ws.beaufortScale, 2.0f);
        }
    }

    // 17. rt_memory_guards
    {
        raw::rt::RawMemoryGuardTelemetry mg{};
        if (RAW_GetMemoryGuardTelemetry(&mg)) {
            mask |= RAW_SUBSYS_MEMORY_GUARDS;
            out->activeHandles = mg.refHandlesEstimated;
            out->workingSetMB  = static_cast<uint32_t>(std::min<uint64_t>(mg.workingSetMB, 0xFFFFFFFFull));
        }
    }

    out->activeSubsystemMask = mask;
    out->verifiedGmstModules = gmstModules;
    out->gmstTotalVerified   = gmstVerSum;
    out->gmstTotalApplied    = gmstAppSum;
    return 1;
}
