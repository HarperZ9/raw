// Engine settings by name and the shadow atlas size at startup (RAW engine runtime). Original work, MIT.
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "rt_control.h"
#include "rt_encounter_balance.h"
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
    const auto& prof = ActiveProfile();
    if (!prof.hasHavok60HzBug || prof.isMultiplayerOnline) {
        return "havok fix: skipped (not needed on this Creation Engine generation or online client)";
    }
    g_havok.fMaxTime = reinterpret_cast<float*>(FindSettingPtr(img, refs, "fMaxTime:HAVOK"));
    g_havok.fMaxTimeComplex = reinterpret_cast<float*>(FindSettingPtr(img, refs, "fMaxTimeComplex:HAVOK"));
    g_havok.uMaxSteps = reinterpret_cast<uint32_t*>(FindSettingPtr(img, refs, "uMaxNumPhysicsStepsPerUpdate:HAVOK"));
    g_havok.uMaxStepsComplex = reinterpret_cast<uint32_t*>(FindSettingPtr(img, refs, "uMaxNumPhysicsStepsPerUpdateComplex:HAVOK"));
    g_havok.bLockFps = reinterpret_cast<uint8_t*>(FindSettingPtr(img, refs, "bLockFramerate:Display"));
    if (!g_havok.fMaxTime) return "havok fix: fMaxTime:HAVOK not found in version table";
    if (g_havok.bLockFps) *g_havok.bLockFps = 0;
    // Raise sub-step ceilings to 6 so 144-360 Hz gameplay stays deterministic with zero clutter/water explosions:
    if (g_havok.uMaxSteps && *g_havok.uMaxSteps < 6u) *g_havok.uMaxSteps = 6u;
    if (g_havok.uMaxStepsComplex && *g_havok.uMaxStepsComplex < 6u) *g_havok.uMaxStepsComplex = 6u;
    QueryPerformanceFrequency(&g_havok.freq);
    QueryPerformanceCounter(&g_havok.lastQpc);
    g_havok.emaDt = 1.0f / 60.0f;
    g_havok.enabled = true;
    char out[224];
    std::snprintf(out, sizeof out, "havok fix: active 30-360Hz (%s, fMaxTime=%p complex=%p lockFps=%p steps=%p)",
                  prof.displayName, g_havok.fMaxTime, g_havok.fMaxTimeComplex, g_havok.bLockFps, g_havok.uMaxSteps);
    return out;
}

void HavokFixOnPresent() {
    if (!g_havok.enabled || !g_havok.fMaxTime || g_havok.freq.QuadPart <= 0) return;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const double rawDt = double(now.QuadPart - g_havok.lastQpc.QuadPart) / double(g_havok.freq.QuadPart);
    g_havok.lastQpc = now;
    if (rawDt <= 0.0004 || rawDt > 0.25) return;  // ignore loading screens / paused spikes
    // Smooth 24 Hz to 360 Hz adaptive Havok step (0.002778s .. 0.041667s) across Skyrim SE & Fallout 4:
    const float clampedDt = rawDt < 0.002778 ? 0.002778f : (rawDt > 0.041667 ? 0.041667f : float(rawDt));
    g_havok.emaDt = g_havok.emaDt * 0.65f + clampedDt * 0.35f;
    const float mt = g_havok.emaDt < 0.002778f ? 0.002778f : (g_havok.emaDt > 0.041667f ? 0.041667f : g_havok.emaDt);
    *g_havok.fMaxTime = mt;
    if (g_havok.fMaxTimeComplex) {
        const float mtc = mt * 2.0f;
        *g_havok.fMaxTimeComplex = mtc < 0.005556f ? 0.005556f : (mtc > 0.066667f ? 0.066667f : mtc);
    }
}

std::string ApplyEngineTuning(const Image& img, const std::vector<RefTarget>& refs) {
    const auto& prof = ActiveProfile();
    if (prof.isMultiplayerOnline || prof.gen != raw::versions::EngineGen::CE1_DX11) {
        return "engine tuning: Skyrim CE1 table skipped for " + std::string(prof.displayName) + " (handled by CoreFixesApplyEngine)";
    }
    struct KV { const char* k; const char* v; };
    static const KV kTuning[] = {
        {"fNearDistance:Display", "15.0"},
        {"fShadowBiasScale:Display", "0.18"},
        // Eliminate 2011 stepped sun-shadow ticking: smooth per-frame sun & shadow progression (0.0s update interval).
        {"fSunUpdateThreshold:Display", "0.0001"},
        {"fSunShadowUpdateTime:Display", "0.0"},
        // Eliminate 2011 flickering-light culling (default 1024u turned torches/hearths off 15m away -> 16384u),
        // full-res shadow mask (iShadowMaskQuarter=4), crisp un-smeared shadow mask (iBlurDeferredShadowMask=1),
        // and disable VSync lock so NVIDIA Reflex LowLatency+Boost owns frame pacing cleanly.
        {"fFlickeringLightDistance:Display", "16384.0"},
        {"iShadowMaskQuarter:Display", "4"},
        {"iBlurDeferredShadowMask:Display", "1"},
        {"iPresentInterval:Display", "0"},
        // Uncap 2011 skinned tree wind-sway limit (default 20 froze distant pines like statues -> 256 swaying trees).
        {"uMaxSkinnedTreesToRender:Trees", "256"},
        {"bEnableTreeAnimations:Trees", "1"},
        {"fUpdateBudget:Trees", "3.0"},
        // Keep vanilla Skyrim water reflection buffer dimensions intact (overriding iWaterReflectWidth/Height
        // or forcing bForceHighDetailReflections=1 at runtime misaligns TESWaterSystem reflection planes!).
        {"bForceHighDetailReflections:Water", "0"},
        // Disable redundant 2011 Skyrim rendering passes that RAW's native DX12 + SM 6.9 + DXR 1.2 render graph
        // now owns 100% (eliminates wasted upside-down water reflection camera pass, 2011 SSR, 2011 SAO, 2011 VL & DoF).
        {"bUseWaterReflections:Water", "0"},
        {"bScreenSpaceReflectionEnabled:Display", "0"},
        {"bSAOEnable:Display", "0"},
        {"bVolumetricLightingEnable:Display", "0"},
        {"bDoDepthOfField:Imagespace", "0"},
        // Upgrade Creation Engine's native TESWaterSystem simulation (60 FPS wave updates instead of 2011 12-30 FPS,
        // hi-res water & displacements) and eliminate 2011 blurry 3D World-Map depth blur directly in MapMenu settings.
        {"bUseWaterHiRes:Water", "1"},
        {"bUseWaterDisplacements:Water", "1"},
        {"uSurfaceFPS:Water", "60"},
        {"bWorldMapNoSkyDepthBlur:MapMenu", "1"},
        {"fWorldMapMaximumDepthBlur:MapMenu", "0.0"},
        // Zero Skyrim's 2011 2D Sun billboard & glare so RAW's single physical HDR solar disc in LightFog.hlsl
        // is the ONLY sun in the sky (eliminates double-sun coordinate drift & night-time glare leaks!).
        {"fSunBaseSize:Weather", "0.0"},
        {"fSunGlareSize:Weather", "0.0"},
        {"fSunGlareMultiplier:Weather", "0.0"},
        {"uiNumActorsAllowedToMorph:FaceGen", "64"},
        {"fLODFadeOutMultActors:LOD", "15.0"},
        {"fLODFadeOutMultObjects:LOD", "15.0"},
        {"fLODFadeOutMultItems:LOD", "15.0"},
        {"fSpecularLODStartFade:Display", "4000.0"},
        {"fShadowLODStartFade:Display", "2000.0"},
        {"fMeshLODLevel1FadeDist:Display", "8192.0"},
        {"fMeshLODLevel2FadeDist:Display", "16384.0"},
        {"fGrassStartFadeDistance:Grass", "11000.0"},
        {"fGrassMaxStartFadeDistance:Grass", "14000.0"},
        {"fGrassWindMagnitudeMin:Grass", "0.45"},
        {"fGrassWindMagnitudeMax:Grass", "1.65"},
        {"iMaxGrassTypesPerTexure:Grass", "15"},
        {"fLightLODStartFade:Display", "18000.0"},
        {"fTreeLoadDistance:Trees", "95000.0"},
        {"fTreesMidLODSwitchDist:Display", "14000.0"},
        {"fInteriorShadowDistance:Display", "4500.0"},
        // Uncap 2011 particle system (750 -> 4096) & decal limits so spells, fires, embers & combat stay dense.
        {"iMaxDesired:Particles", "4096"},
        {"uMaxDecals:Decals", "1000"},
        {"uMaxSkinDecals:Decals", "250"},
        {"uMaxSkinDecalPerActor:Decals", "60"},
        {"bDecals:Decals", "1"},
        {"bSkinnedDecals:Decals", "1"},
        {"bAllowDecalsOnAlpha:Decals", "1"},
        {"iMaxDecalsPerFrame:Display", "256"},
        {"iMaxSkinDecalsPerFrame:Display", "64"},
        {"fDecalLOD0:Display", "4000.0"},
        {"fDecalLOD1:Display", "5000.0"},
        {"fDecalLOD2:Display", "6500.0"},
        // NOTE: Do NOT force bUseThreaded*, bBackgroundLoadVMData, bBackgroundCellLoads, bPreemptivelyUnloadCells,
        // bEnableFileCaching, iNumHWThreads=16, or runtime TerrainManager overrides: in Creation Engine they race
        // BGSSaveLoadManager (causing save corruption) and race the LOD quadtree.
        {"bShadowsOnGrass:Display", "1"},
        {"bTreesReceiveShadows:Display", "1"},
        {"bDrawLandShadows:Display", "1"},
        {"fDecalLifetime:Display", "900.0"},
        // Safe Havok clutter/explosion impulse damping + IK (NOTE: do NOT override fTimePerSubStep/iMinNumSubSteps/
        // bUseCharRBExtrapolation/bUseConstraintProjector/bRagdollFeedback: at 144-360 FPS they fight HavokFixOnPresent()
        // and cause 2.7x substep dilation and ankle-height 1st-person camera sink as seen in 22.59.17.38.mp4!):
        {"fVisibleNavmeshMoveDist:Combat", "16000.0"},
        {"iNumberActorsInCombatPlayer:Combat", "32"},
        {"fActiveEffectConditionUpdateInterval:Gameplay", "0.25"},
        {"fExplosionMaxImpulse", "240.0"},
        {"fGameplayImpulseMultBiped", "0.62"},
        {"fGameplayImpulseMultClutter", "0.55"},
        {"fGameplayImpulseMultProp", "0.65"},
        {"fVelocityDamping:RagdollAnim", "0.14"},
        {"bFootIK:RagdollAnim", "1"},
        {"bLookIK:RagdollAnim", "1"},
        // Native Engine-Level Tactical Combat AI, Stagger & Responsive ARPG Hit-Feel (verified 1.7.104.0 GMSTs):
        // Replaces sluggish 2011 passive enemy AI with decisive attacking, reactive blocking/bashing,
        // sharper movement anticipation, and weightier weapon stagger across daggers, 1H, 2H & bows.
        {"fCombatAttackChanceMin", "0.45"},
        {"fCombatAttackChanceMax", "0.95"},
        {"fCombatBashChanceMin", "0.28"},
        {"fCombatBashChanceMax", "0.72"},
        {"fCombatBashTargetBlockingMult", "1.65"},
        {"fCombatBlockChanceMin", "0.35"},
        {"fCombatBlockChanceMax", "0.82"},
        {"fCombatBlockTimeMin", "0.45"},
        {"fCombatBlockTimeMid", "1.05"},
        {"fCombatBlockTimeMax", "1.85"},
        {"fCombatAttackAnimationDrivenDelayTime", "0.08"},
        {"fCombatAttackPlayerAnticipateMult", "1.35"},
        // Binary-verified SkyrimSE 1.7.104.0 weapon stagger AR thresholds (0x28C140; defaults 5/10/15/5):
        {"fStaggerDaggerAR", "5.5"},
        {"fStagger1WeapAR", "11.5"},
        {"fStagger2WeapAR", "17.5"},
        {"fStaggerBowAR", "6.0"},
        // Non-spongy lethal difficulty curves (verified 0x20AA578..0x20AA680): both player and enemies stay decisive
        // on Expert/Master/Legendary instead of vanilla 0.25x bullet-sponge player damage!
        {"fDiffMultHPToPCVE", "0.50"},
        {"fDiffMultHPToPCE",  "0.75"},
        {"fDiffMultHPToPCN",  "1.00"},
        {"fDiffMultHPToPCH",  "1.35"},
        {"fDiffMultHPToPCVH", "1.75"},
        {"fDiffMultHPToPCL",  "2.25"},
        {"fDiffMultHPByPCVE", "1.50"},
        {"fDiffMultHPByPCE",  "1.25"},
        {"fDiffMultHPByPCN",  "1.00"},
        {"fDiffMultHPByPCH",  "1.15"},
        {"fDiffMultHPByPCVH", "1.35"},
        {"fDiffMultHPByPCL",  "1.60"},
        {"f1PArrowTiltUpAngle:Combat", "0.7"},
        {"f3PArrowTiltUpAngle:Combat", "0.7"},
        {"f1PBoltTiltUpAngle:Combat", "0.7"},
        {"fCollisionRecoverySpeed:Camera", "4.0"},
        {"fCollisionRecoveryMinDist:Camera", "10.0"},
        // Modern 3rd-person, combat & horse over-the-shoulder camera (leaves 1st-person spring constants untouched!):
        {"fOverShoulderPosX:Camera", "24.0"},
        {"fOverShoulderPosZ:Camera", "-4.0"},
        {"fOverShoulderCombatPosX:Camera", "28.0"},
        {"fOverShoulderCombatPosZ:Camera", "-2.0"},
        {"fOverShoulderCombatAddY:Camera", "-20.0"},
        {"fOverShoulderHorsePosX:Camera", "32.0"},
        {"fOverShoulderHorsePosZ:Camera", "10.0"},
        {"fOverShoulderHorseAddY:Camera", "-40.0"},
        {"fShoulderDollySpeed:Camera", "4.5"},
        {"fChaseCameraSpeed:Camera", "10.0"},
        {"fMouseWheelZoomSpeed:Camera", "14.0"},
        {"fMouseWheelZoomIncrement:Camera", "0.08"},
        {"fVanityModeMinDist:Camera", "95.0"},
        {"fVanityModeMaxDist:Camera", "680.0"},
        {"bDisableAutoVanityMode:Camera", "1"},
        {"f1st3rdSwitchDelay:Camera", "0.05"},
        {"fActorFadeOutLimit:Camera", "28.0"},
        // Realistic light-and-sound stealth detection (replaces RAID / Realistic AI Detection):
        {"fSneakLightMult", "0.42"},
        {"fSneakLightExteriorMult", "0.38"},
        {"fSneakSoundsMult", "1.25"},
        {"fSneakSoundLosMult", "0.45"},
        {"fSneakRunningMult", "1.35"},
        {"fSneakDistanceAttenuationExponent", "1.55"},
        // Binary-verified 0x41CEE6: xmm10 += inCombat * fSneakCombatMod (default -0.4; negative = harder to re-hide)
        {"fSneakCombatMod", "-0.55"},
        // Zero magnetic auto-aim (true ballistic archery, armor mitigation & resource GMSTs applied cleanly
        // via binary-verified ApplyEncounterBalanceGmsts below — fixes legacy fArrowSpeedMult/fArrowGravityMult units!):
        {"fAutoAimMaxDegrees", "0.0"},
        {"fAutoAimMaxDegrees3rdPerson", "0.0"},
        {"fAutoAimMaxDegreesMelee", "0.0"},
        // Precision hit-cone, flanking/circling tactical AI & responsive combat pacing (replaces Mortal Enemies / Wildcat):
        {"fCombatHitConeAngle", "22.0"},
        {"fMeleeSweepViewAngleMult", "0.65"},
        {"fCombatFlankingDistanceMin", "130.0"},
        {"fCombatFlankingDistanceMax", "380.0"},
        {"fCombatCircleChanceMin", "0.35"},
        {"fCombatCircleChanceMax", "0.72"},
        {"fCombatStrafeChanceMin", "0.30"},
        {"fCombatStrafeChanceMax", "0.65"},
        {"fCombatFallbackChanceMin", "0.18"},
        {"fCombatFallbackChanceMax", "0.48"},
        {"fPowerAttackCoolDownTime", "0.35"},
        // Binary-verified 0x7F0E08: HitData::Populate clamps percentBlocked vs fPlayerMaxResistance (0x20A9AE8, def 85.0);
        // legacy fBlockMax (0x20A71A0) has 0 .text readers.
        {"fPlayerMaxResistance", "85.0"},
        {"fBlockWeaponBase", "0.32"},
        // Binary-verified 0x4214C0: sprint drain = dt * (w*mult+base) * fSprintStaminaDrainMult (default 15.0/sec)
        {"fSprintStaminaDrainMult", "14.5"},
        // Grounded economy progression & snappy interface transitions (replaces Trade & Barter / Faster Fade):
        {"fBarterMin", "1.35"},
        {"fBarterMax", "3.60"},
        {"fActivatePickRadius:Interface", "12.0"},
        {"fFadeToBlackFadeSeconds:Interface", "0.35"},
        {"fMinSecondsForLoadFadeIn:Interface", "0.25"},
        {"fAIInteriorHeadTrackMult", "1.0"},
        {"fAIMaxHeadTrackDistanceFromPC", "1200.0"},
        {"fAIBestHeadTrackDistance", "600.0"},
        {"fDialogueHeadPitchExaggeration:Audio", "1.15"},
        {"fDialogueHeadRollExaggeration:Audio", "0.85"},
        {"fDialogueHeadYawExaggeration:Audio", "1.10"},
        {"bUsePrecipitationOcclusion:Display", "1"},
        {"bEnableParallaxOcclusion:Display", "1"},
        {"fUpdateBudgetMS:Papyrus", "2.4"},
        {"fExtraTaskletBudgetMS:Papyrus", "2.4"},
        {"fPostLoadUpdateTimeMS:Papyrus", "1000.0"},
    };
    const bool hasEngineFixes = GetModuleHandleA("EngineFixes.dll") != nullptr;
    size_t applied = 0;
    for (const auto& kv : kTuning) {
        // On heavily modded SKSE lists (EngineFixes.dll active), skip aggressive LOD/tree/grass distance inflation
        // so 32 GB RAM systems never hit 31.5+ GB OOM during save load; keep all rendering, physics & combat fixes!
        if (hasEngineFixes && (
                std::strncmp(kv.k, "fLODFadeOutMult", 15) == 0 ||
                std::strcmp(kv.k, "fTreeLoadDistance:Trees") == 0 ||
                std::strcmp(kv.k, "uMaxSkinnedTreesToRender:Trees") == 0 ||
                std::strcmp(kv.k, "fGrassStartFadeDistance:Grass") == 0 ||
                std::strcmp(kv.k, "fGrassMaxStartFadeDistance:Grass") == 0)) {
            continue;
        }
        if (SetSetting(img, refs, kv.k, kv.v)) ++applied;
    }
    const std::string balMsg = ApplyEncounterBalanceGmsts(img, refs);
    char out[320];
    std::snprintf(out, sizeof out, "engine tuning: %zu of %zu safe architectural fixes applied (save-safe) | %s",
                  applied, sizeof(kTuning) / sizeof(kTuning[0]), balMsg.c_str());
    return out;
}

namespace {
struct DynamicGmstBindings {
    bool bound = false;
    float* fBarterMin = nullptr;
    float* fBarterMax = nullptr;
    float* fSprintStaminaDrainMult = nullptr;
    float* fArrowGravityMult = nullptr;
    float* fJumpFallHeightMult = nullptr;
    float baseBarterMin = 1.35f;
    float baseBarterMax = 3.60f;
    float baseSprintDrain = 14.5f;
    float baseArrowGravity = 0.0016f;
    float baseJumpFall = 1.0f;
} g_dynGmsts;
}  // namespace

std::string WorldSimBindDynamicSettings(const Image& img, const std::vector<RefTarget>& refs) {
    g_dynGmsts = {};
    const auto& prof = ActiveProfile();
    if (prof.isMultiplayerOnline || prof.gen != raw::versions::EngineGen::CE1_DX11) {
        return "worldsim gmst feedback: skipped (non-CE1 or online profile)";
    }
    g_dynGmsts.fBarterMin = reinterpret_cast<float*>(FindSettingPtr(img, refs, "fBarterMin"));
    g_dynGmsts.fBarterMax = reinterpret_cast<float*>(FindSettingPtr(img, refs, "fBarterMax"));
    g_dynGmsts.fSprintStaminaDrainMult = reinterpret_cast<float*>(FindSettingPtr(img, refs, "fSprintStaminaDrainMult"));
    g_dynGmsts.fArrowGravityMult = reinterpret_cast<float*>(FindSettingPtr(img, refs, "fArrowGravityMult"));
    g_dynGmsts.fJumpFallHeightMult = reinterpret_cast<float*>(FindSettingPtr(img, refs, "fJumpFallHeightMult"));
    if (g_dynGmsts.fBarterMin) g_dynGmsts.baseBarterMin = *g_dynGmsts.fBarterMin;
    if (g_dynGmsts.fBarterMax) g_dynGmsts.baseBarterMax = *g_dynGmsts.fBarterMax;
    if (g_dynGmsts.fSprintStaminaDrainMult) g_dynGmsts.baseSprintDrain = *g_dynGmsts.fSprintStaminaDrainMult;
    if (g_dynGmsts.fArrowGravityMult) g_dynGmsts.baseArrowGravity = *g_dynGmsts.fArrowGravityMult;
    if (g_dynGmsts.fJumpFallHeightMult) g_dynGmsts.baseJumpFall = *g_dynGmsts.fJumpFallHeightMult;
    g_dynGmsts.bound = (g_dynGmsts.fBarterMin || g_dynGmsts.fBarterMax ||
                        g_dynGmsts.fSprintStaminaDrainMult || g_dynGmsts.fArrowGravityMult || g_dynGmsts.fJumpFallHeightMult);
    char out[224];
    std::snprintf(out, sizeof out, "worldsim gmst feedback: bound=%d (barterMin=%p barterMax=%p sprint=%p arrowGrav=%p)",
                  g_dynGmsts.bound ? 1 : 0, g_dynGmsts.fBarterMin, g_dynGmsts.fBarterMax,
                  g_dynGmsts.fSprintStaminaDrainMult, g_dynGmsts.fArrowGravityMult);
    return out;
}

void WorldSimApplyLiveGmsts(float barterMin, float barterMax, float sprintStaminaDrainMult) {
    if (!g_dynGmsts.bound) return;
    if (g_dynGmsts.fBarterMin && std::fabs(*g_dynGmsts.fBarterMin - barterMin) > 0.005f) {
        *g_dynGmsts.fBarterMin = barterMin;
    }
    if (g_dynGmsts.fBarterMax && std::fabs(*g_dynGmsts.fBarterMax - barterMax) > 0.005f) {
        *g_dynGmsts.fBarterMax = barterMax;
    }
    if (g_dynGmsts.fSprintStaminaDrainMult && std::fabs(*g_dynGmsts.fSprintStaminaDrainMult - sprintStaminaDrainMult) > 0.005f) {
        *g_dynGmsts.fSprintStaminaDrainMult = sprintStaminaDrainMult;
    }
}

void WorldSimApplyLivePlanetaryGravity(float gravityMps2) {
    if (!g_dynGmsts.bound || !std::isfinite(gravityMps2)) return;
    const float gRatio = std::clamp(gravityMps2 / 9.80665f, 0.75f, 1.25f);
    if (g_dynGmsts.fArrowGravityMult) {
        const float target = g_dynGmsts.baseArrowGravity * gRatio;
        if (std::fabs(*g_dynGmsts.fArrowGravityMult - target) > 0.002f) *g_dynGmsts.fArrowGravityMult = target;
    }
    if (g_dynGmsts.fJumpFallHeightMult) {
        const float target = g_dynGmsts.baseJumpFall * gRatio;
        if (std::fabs(*g_dynGmsts.fJumpFallHeightMult - target) > 0.002f) *g_dynGmsts.fJumpFallHeightMult = target;
    }
}

void WorldSimRestoreDynamicSettings() {
    RestoreEncounterBalanceGmsts();
    if (!g_dynGmsts.bound) return;
    if (g_dynGmsts.fBarterMin) *g_dynGmsts.fBarterMin = g_dynGmsts.baseBarterMin;
    if (g_dynGmsts.fBarterMax) *g_dynGmsts.fBarterMax = g_dynGmsts.baseBarterMax;
    if (g_dynGmsts.fSprintStaminaDrainMult) *g_dynGmsts.fSprintStaminaDrainMult = g_dynGmsts.baseSprintDrain;
    if (g_dynGmsts.fArrowGravityMult) *g_dynGmsts.fArrowGravityMult = g_dynGmsts.baseArrowGravity;
    if (g_dynGmsts.fJumpFallHeightMult) *g_dynGmsts.fJumpFallHeightMult = g_dynGmsts.baseJumpFall;
    g_dynGmsts.bound = false;
}

}  // namespace raw::rt
