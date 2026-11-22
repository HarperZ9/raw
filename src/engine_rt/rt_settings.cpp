// Engine settings by name and the shadow atlas size at startup (RAW engine runtime). Original work, MIT.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "rt_control.h"
#include "rt_version.h"

namespace raw::rt {

bool SetSetting(const Image& img, const std::vector<RefTarget>& refs, const std::string& name, const std::string& value,
                std::string* before) {
    const RefTarget* r = nullptr;
    for (const auto& x : refs)
        if (x.setting && x.name == name) { r = &x; break; }
    if (!r || name.empty()) return false;
    auto* field = const_cast<uint8_t*>(img.base) + r->rva + 8;
    char old[64] = "";
    const char t = name[0];
    if (t == 'f') {
        float o; std::memcpy(&o, field, 4); std::snprintf(old, sizeof old, "%g", o);
        const float v = std::strtof(value.c_str(), nullptr); std::memcpy(field, &v, 4);
    } else if (t == 'i') {
        int32_t o; std::memcpy(&o, field, 4); std::snprintf(old, sizeof old, "%d", o);
        const int32_t v = std::atoi(value.c_str()); std::memcpy(field, &v, 4);
    } else if (t == 'u') {
        uint32_t o; std::memcpy(&o, field, 4); std::snprintf(old, sizeof old, "%u", o);
        const uint32_t v = uint32_t(std::strtoul(value.c_str(), nullptr, 10)); std::memcpy(field, &v, 4);
    } else if (t == 'b') {
        std::snprintf(old, sizeof old, "%d", int(field[0]));
        field[0] = (value == "1" || value == "true") ? 1 : 0;
    } else {
        return false;  // strings and colours are not written here
    }
    if (before) *before = old;
    return true;
}

size_t ApplyStartupSettings(const Image& img, const std::vector<RefTarget>& refs, const std::string& ini,
                            const std::string& logPath) {
    char buf[16384] = {};
    const DWORD n = GetPrivateProfileSectionA("EngineSet", buf, sizeof buf, ini.c_str());
    size_t applied = 0;
    FILE* log = std::fopen(logPath.c_str(), "a");
    for (const char* p = buf; p < buf + n && *p; p += std::strlen(p) + 1) {
        std::string kv(p);
        const size_t eq = kv.rfind('=');
        if (eq == std::string::npos) continue;
        std::string before;
        const bool ok = SetSetting(img, refs, kv.substr(0, eq), kv.substr(eq + 1), &before);
        applied += ok;
        if (log) std::fprintf(log, "startup set %s (was %s): %s\n", kv.c_str(), before.c_str(), ok ? "ok" : "not found");
    }
    if (log) std::fclose(log);
    return applied;
}

std::string ResizeShadowAtlas(const Image& img, const std::vector<RefTarget>& refs, uint32_t size) {
    // sub_14F00D0 copies iShadowMapResolution into a global at startup, before this module loads, and the
    // renderer sizes the shadow atlas from that global when it creates its targets (after this module
    // starts). Write both, the global only when it still holds the setting's value.
    const uint32_t kShadowSizeGlobal = Addr("global:ShadowMapSize");  // the renderer's copy of iShadowMapResolution
    if (!kShadowSizeGlobal) return "shadow atlas: no global:ShadowMapSize for this version; left alone";
    auto* setting = [&]() -> uint32_t* {
        for (const auto& x : refs)
            if (x.name == "iShadowMapResolution:Display") return reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(img.base) + x.rva + 8);
        return nullptr;
    }();
    auto* global = reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(img.base) + kShadowSizeGlobal);
    char out[192];
    if (!setting) return "shadow atlas: iShadowMapResolution not in the refs";
    const uint32_t s0 = *setting, g0 = *global;
    if (g0 != s0) {
        std::snprintf(out, sizeof out, "shadow atlas: global 0x%X holds %u, setting %u; left alone", kShadowSizeGlobal, g0, s0);
        return out;
    }
    *setting = size;
    *global = size;
    std::snprintf(out, sizeof out, "shadow atlas: setting and global 0x%X %u -> %u", kShadowSizeGlobal, s0, size);
    return out;
}

namespace {
struct HavokState {
    bool enabled = false;
    float* fMaxTime = nullptr;
    float* fMaxTimeComplex = nullptr;
    uint32_t* uMaxSteps = nullptr;
    uint32_t* uMaxStepsComplex = nullptr;
    uint8_t* bLockFps = nullptr;
    LARGE_INTEGER freq{};
    LARGE_INTEGER lastQpc{};
    float emaDt = 1.0f / 60.0f;
} g_havok;

void* FindSettingPtr(const Image& img, const std::vector<RefTarget>& refs, const char* name) {
    for (const auto& x : refs)
        if (x.setting && x.name == name) return const_cast<uint8_t*>(img.base) + x.rva + 8;
    return nullptr;
}
}  // namespace

std::string HavokFixInit(const Image& img, const std::vector<RefTarget>& refs) {
    g_havok = {};
    g_havok.fMaxTime = reinterpret_cast<float*>(FindSettingPtr(img, refs, "fMaxTime:HAVOK"));
    g_havok.fMaxTimeComplex = reinterpret_cast<float*>(FindSettingPtr(img, refs, "fMaxTimeComplex:HAVOK"));
    g_havok.uMaxSteps = reinterpret_cast<uint32_t*>(FindSettingPtr(img, refs, "uMaxNumPhysicsStepsPerUpdate:HAVOK"));
    g_havok.uMaxStepsComplex = reinterpret_cast<uint32_t*>(FindSettingPtr(img, refs, "uMaxNumPhysicsStepsPerUpdateComplex:HAVOK"));
    g_havok.bLockFps = reinterpret_cast<uint8_t*>(FindSettingPtr(img, refs, "bLockFramerate:Display"));
    if (!g_havok.fMaxTime) return "havok fix: fMaxTime:HAVOK not found in version table";
    if (g_havok.bLockFps) *g_havok.bLockFps = 0;
    if (g_havok.uMaxSteps && *g_havok.uMaxSteps < 4u) *g_havok.uMaxSteps = 4u;
    if (g_havok.uMaxStepsComplex && *g_havok.uMaxStepsComplex < 4u) *g_havok.uMaxStepsComplex = 4u;
    QueryPerformanceFrequency(&g_havok.freq);
    QueryPerformanceCounter(&g_havok.lastQpc);
    g_havok.emaDt = 1.0f / 60.0f;
    g_havok.enabled = true;
    char out[192];
    std::snprintf(out, sizeof out, "havok fix: active (fMaxTime=%p complex=%p lockFps=%p steps=%p)",
                  g_havok.fMaxTime, g_havok.fMaxTimeComplex, g_havok.bLockFps, g_havok.uMaxSteps);
    return out;
}

void HavokFixOnPresent() {
    if (!g_havok.enabled || !g_havok.fMaxTime || g_havok.freq.QuadPart <= 0) return;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const double rawDt = double(now.QuadPart - g_havok.lastQpc.QuadPart) / double(g_havok.freq.QuadPart);
    g_havok.lastQpc = now;
    if (rawDt <= 0.0005 || rawDt > 0.25) return;  // ignore loading screens / paused spikes
    const float clampedDt = rawDt < 0.003333 ? 0.003333f : (rawDt > 0.041667 ? 0.041667f : float(rawDt));
    g_havok.emaDt = g_havok.emaDt * 0.65f + clampedDt * 0.35f;
    const float mt = g_havok.emaDt < 0.003333f ? 0.003333f : (g_havok.emaDt > 0.041667f ? 0.041667f : g_havok.emaDt);
    *g_havok.fMaxTime = mt;
    if (g_havok.fMaxTimeComplex) {
        const float mtc = mt * 2.0f;
        *g_havok.fMaxTimeComplex = mtc < 0.006667f ? 0.006667f : (mtc > 0.066667f ? 0.066667f : mtc);
    }
}

std::string ApplyEngineTuning(const Image& img, const std::vector<RefTarget>& refs) {
    struct KV { const char* k; const char* v; };
    static const KV kTuning[] = {
        {"fNearDistance:Display", "22.0"},
        {"fShadowBiasScale:Display", "0.18"},
        {"fSunUpdateThreshold:Display", "0.0025"},
        {"fSunShadowUpdateTime:Display", "0.25"},
        {"fSunBaseSize:Weather", "160.0"},
        {"fSunGlareSize:Weather", "280.0"},
        {"fSunGlareMultiplier:Weather", "0.35"},
        {"bMultiThreadBoneUpdate:Animation", "1"},
        {"bUseThreadedParticleSystem:General", "1"},
        {"bUseThreadedTempEffects:General", "1"},
        {"bUseThreadedMorpher:General", "1"},
        {"bUseMultiThreadedFaceGen:BackgroundLoad", "1"},
        {"bLoadBackgroundFaceGen:BackgroundLoad", "1"},
        {"uiNumActorsAllowedToMorph:FaceGen", "64"},
        {"fLODFadeOutMultActors:LOD", "15.0"},
        {"fGrassStartFadeDistance:Grass", "11000.0"},
        {"fLightLODStartFade:Display", "6500.0"},
        {"fTreesMidLODSwitchDist:Display", "14000.0"},
        {"fInteriorShadowDistance:Display", "4500.0"},
        {"fSplitDistanceMult:TerrainManager", "1.5"},
        {"fBlockLevel0Distance:TerrainManager", "45000.0"},
        {"fBlockLevel1Distance:TerrainManager", "90000.0"},
        {"bShadowsOnGrass:Display", "1"},
        {"iMaxGrassTypesPerTexure:Grass", "15"},
        {"iMaxDecalsPerFrame:Display", "256"},
        {"uMaxDecals:Decals", "1000"},
        {"fDecalLifetime:Display", "900.0"},
        {"iMaxSkinDecalsPerFrame:Display", "64"},
        {"uMaxSkinDecals:Decals", "256"},
        {"uMaxSkinDecalPerActor:Decals", "32"},
        // Binary-level Havok physics, 120Hz sub-stepped solver, anti-clutter-explosion & realistic ragdoll/IK overhaul:
        {"fTimePerSubStep:HAVOK", "0.0083333"},
        {"iMinNumSubSteps:HAVOK", "2"},
        {"bFindContactPointsOnAdd:HAVOK", "1"},
        {"bUseConstraintProjector:HAVOK", "1"},
        {"bUseCharRBExtrapolation:HAVOK", "1"},
        {"fInAirFallingCharGravityMult:Havok", "1.15"},
        {"fExplosionMaxImpulse", "240.0"},
        {"fGameplayImpulseMultBiped", "0.62"},
        {"fGameplayImpulseMultClutter", "0.55"},
        {"fGameplayImpulseMultProp", "0.65"},
        {"fVelocityDamping:RagdollAnim", "0.14"},
        {"bFootIK:RagdollAnim", "1"},
        {"bLookIK:RagdollAnim", "1"},
        {"bPoseMatching:RagdollAnim", "1"},
        {"bRagdollFeedback:RagdollAnim", "1"},
        // Engine-level file caching, water reflection parity, archery aiming & smooth camera collision recovery:
        {"bEnableFileCaching:General", "1"},
        {"bReflectLODLand:Water", "1"},
        {"bReflectLODObjects:Water", "1"},
        {"bReflectLODTrees:Water", "1"},
        {"bReflectSky:Water", "1"},
        {"f1PArrowTiltUpAngle:Combat", "0.7"},
        {"f3PArrowTiltUpAngle:Combat", "0.7"},
        {"fCollisionRecoverySpeed:Camera", "4.0"},
        {"fCollisionRecoveryMinDist:Camera", "10.0"},
        {"fAIInteriorHeadTrackMult", "1.0"},
        {"fAIMaxHeadTrackDistanceFromPC", "1200.0"},
        {"fAIBestHeadTrackDistance", "600.0"},
        {"fDialogueHeadPitchExaggeration:Audio", "1.15"},
        {"bUseMultiThreadedTrees:BackgroundLoad", "1"},
        {"bBackgroundLoadLipFiles:BackgroundLoad", "1"},
        {"bBackgroundCellLoads:BackgroundLoad", "1"},
        {"bBackgroundLoadVMData:General", "1"},
        {"bBackgroundInitializeGeometryDecals:Decals", "1"},
        {"bBackgroundNavmeshUpdate:Pathfinding", "1"},
        {"bBackgroundPathing:Pathfinding", "1"},
        {"iPreloadSizeLimit:General", "268435456"},
        {"bPreemptivelyUnloadCells:General", "1"},
        {"bEnableBoundingVolumeOcclusion:General", "0"},
        {"bPreCullActors:General", "0"},
        {"bUsePrecipitationOcclusion:Display", "1"},
        {"bEnableParallaxOcclusion:Display", "1"},
        // Eliminate 2011 Creation Engine LOD/specular/shadow throttling so full-detail geometry & PBR specular
        // stay crisp across entire valleys, cities, and mountains (zero close-range specular/mesh/tree pop-in).
        {"fSpecularLODStartFade:Display", "4000.0"},
        {"fShadowLODStartFade:Display", "2000.0"},
        {"fMeshLODLevel1FadeDist:Display", "9999999.0"},
        {"fMeshLODLevel2FadeDist:Display", "9999999.0"},
        {"fMeshLODLevel1FadeTreeDistance:Display", "9999999.0"},
        {"fMeshLODLevel2FadeTreeDistance:Display", "9999999.0"},
        {"bTreesReceiveShadows:Display", "1"},
        {"bDrawLandShadows:Display", "1"},
        {"fBlockMaximumDistance:TerrainManager", "250000.0"},
        {"fSkyCellRefFadeDistance:General", "300000.0"},
        {"fGrassStartFadeDistance:Grass", "9500.0"},
        {"fLightLODStartFade:Display", "5500.0"},
        {"fBlockLevel0Distance:TerrainManager", "45000.0"},
        {"fTreeLoadDistance:TerrainManager", "85000.0"},
        {"iMaxDesired:Particles", "6000"},
        {"uParticlesCount:BudgetCaps", "6000"},
        {"uParticlesCountInterior:BudgetCaps", "6000"},
        {"fUpdateBudgetMS:Papyrus", "2.4"},
        {"fExtraTaskletBudgetMS:Papyrus", "2.4"},
        {"fPostLoadUpdateTimeMS:Papyrus", "1000.0"},
    };
    size_t applied = 0;
    for (const auto& kv : kTuning) {
        if (SetSetting(img, refs, kv.k, kv.v)) ++applied;
    }
    // Scale Creation Engine worker thread pool to modern multi-core CPUs (clamped 4..16).
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const uint32_t hwThreads = si.dwNumberOfProcessors < 4 ? 4u : (si.dwNumberOfProcessors > 16 ? 16u : si.dwNumberOfProcessors);
    char thBuf[16];
    std::snprintf(thBuf, sizeof thBuf, "%u", hwThreads);
    const bool thOk = SetSetting(img, refs, "iNumHWThreads:General", thBuf);
    if (thOk) ++applied;
    char out[160];
    std::snprintf(out, sizeof out, "engine tuning: %zu of %zu architectural fixes applied (iNumHWThreads=%u)",
                  applied, sizeof(kTuning) / sizeof(kTuning[0]) + 1, hwThreads);
    return out;
}

}  // namespace raw::rt
