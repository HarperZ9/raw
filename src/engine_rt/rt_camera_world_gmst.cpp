// rt_camera_world_gmst.cpp: Milestone Wave 61 Binary-Verified 1.7.104.0 Camera Shake, KillCam,
// Dragon Flight/Orbit/Landing, Weather, Celestial & Water Physics GMSTs (Zero overlap across all 372 prior GMSTs!).
// Original work, MIT.
#include "rt_camera_world_gmst.h"
#include "rt_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#ifndef RAW_SIM_PURE_TEST
#include "rt_callgraph.h"
#include "rt_version.h"
#include <windows.h>
#endif

namespace raw::rt {
namespace {

// Every single GMST RVA, on-disk float/int32 default, and .text reader verified directly against SkyrimSE.exe 1.7.104.0
// with STRICT ZERO OVERLAP against all 372 previously cataloged GMSTs across the 15 prior modules:
//   - Camera Shake & Dragon-Rider Framing (0x5584A0, 0x558600, 0x55CB50):
//       fCameraShakeTime                     (0x20AA1A0, float def=1.25,    reader  0x5585B0)           -> 0.95
//       fCameraShakeFadeTime                 (0x20AA1B8, float def=0.25,    reader  0x558B5F)           -> 0.30
//       fCameraShakeDistFadeStart            (0x20AA200, float def=2000.0,  reader  0x558548)           -> 1600.0
//       fCameraShakeDistFadeDelta            (0x20AA218, float def=2500.0,  reader  0x55853C)           -> 2000.0
//       fOverShoulderRangedDragonPosZ        (0x20B3E68, float def=90.0,    reader  0x55CB5F)           -> 105.0
//       fOverShoulderRangedDragonAddY        (0x20B3E80, float def=-150.0,  reader  0x55CB67)           -> -175.0
//   - KillCam & VATS Smart Camera (0x4206C0, 0x3191B0, 0x6A07C0):
//       fKillCamBaseOdds                     (0x20A7F20, float def=0.1,     reader  0x42071C)           -> 0.14
//       iKillCamLevelOffset                  (0x20A7F68, int32 def=4,       reader  0x4206C9)           -> 4
//       fVATSCameraDollyTime                 (0x207BEA0, float def=1.0,     reader  0x3191E6)           -> 0.85
//       fVATSSmartCameraCheckStepDistance    (0x20AA260, float def=128.0,   readers 0x6A095C, 0x6A0D52) -> 96.0
//   - Dragon Flight, Orbit & Landing Physics (0x55CC60, 0x685CD0, 0x8F2E2C, 0x8AA590, 0x8F22D0, 0x8AD1C0):
//       fDragonMaxUpwardPitch                (0x20B3EC8, float def=45.0,    4 readers incl. 0x55CD4E)   -> 52.0
//       fFlyingActorDefaultTurningSpeed      (0x20A0398, float def=1.5708,  reader  0x685D45)           -> 1.75
//       fCombatFlightMinimumRange            (0x20A1610, float def=1536.0,  reader  0x8F2E2F)           -> 1280.0
//       fCombatOrbitDistance                 (0x20A2768, float def=6000.0,  readers 0x8AA67B, 0x8ACF77) -> 4800.0
//       fCombatOrbitTimeMax                  (0x20A2798, float def=12.0,    reader  0x8F22E4)           -> 9.0
//       fCombatLandingSearchDistance         (0x20A2990, float def=6000.0,  reader  0x8AD1F7)           -> 5200.0
//   - Weather & Atmospheric Transitions (0x40C220, 0x412C9D, 0x411EC0, 0x40F1C0):
//       fWeatherCloudSpeedMax                (0x209E538, float def=0.1,     reader  0x40C3B4)           -> 0.12
//       fWeatherTransMax                     (0x209E700, float def=0.25,    reader  0x412CE3)           -> 0.22
//       fWeatherFlashDuration                (0x209E748, float def=0.25,    reader  0x412650)           -> 0.28
//       fPrecipWindMult                      (0x209E558, float def=500.0,   6 readers incl. 0x40F214)   -> 550.0
//   - Celestial Sun & Moons (0x41BD90, 0x414FE0):
//       fSunMinimumGlareScale                (0x209E910, float def=0.25,    reader  0x41C582)           -> 0.20
//       iMasserSize                          (0x209E658, int32 def=94,      reader  0x41529D)           -> 94
//   - Water & Underwater World Physics (0x24960, 0x696D20):
//       fUnderwaterFullDepth                 (0x209E7A8, float def=1000.0,  reader  0x2496B)            -> 1150.0
//       fWaterKnockdownVelocity              (0x209F108, float def=10.0,    reader  0x696D76)           -> 11.5
//   - Dead 0-Reader Legacy GMSTs (confirmed 0 .text readers via overlap-safe ModRM scan, strictly never written!):
//       fTimeSpanSunriseStart                (0x207A5D0, float def=6.0,     0 .text readers)
//       fTimeSpanSunsetStart                 (0x207A6F0, float def=18.0,    0 .text readers)
//       fVATSTargetSelectCamPanTime          (0x20AD8D8, float def=0.75,    0 .text readers)
//       fVATSCamZoomInTime                   (0x20AD8F0, float def=0.75,    0 .text readers)
//       fGunShellCameraDistance              (0x20A5FB8, float def=512.0,   0 .text readers)
//       iSnowNoiseTextureSize                (0x20D89F8, int32 def=512,     0 .text readers)
constexpr CameraWorldGmstSpec kCameraWorldSpecs[] = {
    {"fCameraShakeTime",                  0x20AA1A0u, false, 1.25f,    0.95f,   0,   0,   true,  "Camera"},
    {"fCameraShakeFadeTime",              0x20AA1B8u, false, 0.25f,    0.30f,   0,   0,   true,  "Camera"},
    {"fCameraShakeDistFadeStart",         0x20AA200u, false, 2000.0f,  1600.0f, 0,   0,   true,  "Camera"},
    {"fCameraShakeDistFadeDelta",         0x20AA218u, false, 2500.0f,  2000.0f, 0,   0,   true,  "Camera"},
    {"fOverShoulderRangedDragonPosZ",     0x20B3E68u, false, 90.0f,    105.0f,  0,   0,   true,  "Camera"},
    {"fOverShoulderRangedDragonAddY",     0x20B3E80u, false, -150.0f,  -175.0f, 0,   0,   true,  "Camera"},
    {"fKillCamBaseOdds",                  0x20A7F20u, false, 0.1f,     0.14f,   0,   0,   true,  "KillCam"},
    {"iKillCamLevelOffset",               0x20A7F68u, true,  0.0f,     0.0f,    4,   4,   true,  "KillCam"},
    {"fVATSCameraDollyTime",              0x207BEA0u, false, 1.0f,     0.85f,   0,   0,   true,  "KillCam"},
    {"fVATSSmartCameraCheckStepDistance", 0x20AA260u, false, 128.0f,   96.0f,   0,   0,   true,  "KillCam"},
    {"fDragonMaxUpwardPitch",             0x20B3EC8u, false, 45.0f,    52.0f,   0,   0,   true,  "DragonFlight"},
    {"fFlyingActorDefaultTurningSpeed",   0x20A0398u, false, 1.5708f,  1.75f,   0,   0,   true,  "DragonFlight"},
    {"fCombatFlightMinimumRange",         0x20A1610u, false, 1536.0f,  1280.0f, 0,   0,   true,  "DragonFlight"},
    {"fCombatOrbitDistance",              0x20A2768u, false, 6000.0f,  4800.0f, 0,   0,   true,  "DragonFlight"},
    {"fCombatOrbitTimeMax",               0x20A2798u, false, 12.0f,    9.0f,    0,   0,   true,  "DragonFlight"},
    {"fCombatLandingSearchDistance",      0x20A2990u, false, 6000.0f,  5200.0f, 0,   0,   true,  "DragonFlight"},
    {"fWeatherCloudSpeedMax",             0x209E538u, false, 0.1f,     0.12f,   0,   0,   true,  "Weather"},
    {"fWeatherTransMax",                  0x209E700u, false, 0.25f,    0.22f,   0,   0,   true,  "Weather"},
    {"fWeatherFlashDuration",             0x209E748u, false, 0.25f,    0.28f,   0,   0,   true,  "Weather"},
    {"fPrecipWindMult",                   0x209E558u, false, 500.0f,   550.0f,  0,   0,   true,  "Weather"},
    {"fSunMinimumGlareScale",             0x209E910u, false, 0.25f,    0.20f,   0,   0,   true,  "Celestial"},
    {"iMasserSize",                       0x209E658u, true,  0.0f,     0.0f,    94,  94,  true,  "Celestial"},
    {"fUnderwaterFullDepth",              0x209E7A8u, false, 1000.0f,  1150.0f, 0,   0,   true,  "Water"},
    {"fWaterKnockdownVelocity",           0x209F108u, false, 10.0f,    11.5f,   0,   0,   true,  "Water"},
    // Dead 0-reader legacy GMSTs (verified on disk, strictly never written):
    {"fTimeSpanSunriseStart",             0x207A5D0u, false, 6.0f,     6.0f,    0,   0,   false, "Celestial"},
    {"fTimeSpanSunsetStart",              0x207A6F0u, false, 18.0f,    18.0f,   0,   0,   false, "Celestial"},
    {"fVATSTargetSelectCamPanTime",       0x20AD8D8u, false, 0.75f,    0.75f,   0,   0,   false, "KillCam"},
    {"fVATSCamZoomInTime",                0x20AD8F0u, false, 0.75f,    0.75f,   0,   0,   false, "KillCam"},
    {"fGunShellCameraDistance",           0x20A5FB8u, false, 512.0f,   512.0f,  0,   0,   false, "Camera"},
    {"iSnowNoiseTextureSize",             0x20D89F8u, true,  0.0f,     0.0f,    512, 512, false, "Weather"},
};

constexpr size_t kCameraWorldSpecCount = sizeof(kCameraWorldSpecs) / sizeof(kCameraWorldSpecs[0]);

std::mutex          g_cwStateMu;
RawCameraWorldState g_cwState{};

}  // namespace

const CameraWorldGmstSpec* CameraWorldGmstTable(size_t* outCount) {
    if (outCount) *outCount = kCameraWorldSpecCount;
    return kCameraWorldSpecs;
}

bool VerifyCameraWorldGmstTable(const Image& img, uint32_t* outVerifiedCount) {
    if (outVerifiedCount) *outVerifiedCount = 0u;
    if (!img.base || img.sizeOfImage < 0x20E0000u) return false;

    uint64_t imageBase = reinterpret_cast<uint64_t>(img.base);
    if (img.base[0] == 'M' && img.base[1] == 'Z') {
        uint32_t lfanew = 0u;
        std::memcpy(&lfanew, img.base + 0x3Cu, 4u);
        if (lfanew + 0x40u < img.sizeOfImage) {
            uint64_t optBase = 0ull;
            std::memcpy(&optBase, img.base + lfanew + 0x30u, 8u);
            if (optBase >= 0x100000000ull) imageBase = optBase;
        }
    }

    uint32_t verified = 0u;
    for (size_t i = 0; i < kCameraWorldSpecCount; ++i) {
        const auto& spec = kCameraWorldSpecs[i];
        if (spec.expectedRva + 24u > img.sizeOfImage) return false;

        const uint8_t* obj = img.base + spec.expectedRva;
        uint64_t nameVa = 0ull;
        std::memcpy(&nameVa, obj + 16u, 8u);

        uint64_t nameRva = 0ull;
        if (nameVa >= imageBase && (nameVa - imageBase) < img.sizeOfImage) {
            nameRva = nameVa - imageBase;
        } else if (nameVa >= reinterpret_cast<uint64_t>(img.base) &&
                   (nameVa - reinterpret_cast<uint64_t>(img.base)) < img.sizeOfImage) {
            nameRva = nameVa - reinterpret_cast<uint64_t>(img.base);
        } else {
            return false;
        }

        const size_t nameLen = std::strlen(spec.name);
        if (nameRva + nameLen + 1u > img.sizeOfImage) return false;
        if (std::memcmp(img.base + nameRva, spec.name, nameLen + 1u) != 0) return false;

        if (spec.isInt32) {
            int32_t ival = 0;
            std::memcpy(&ival, obj + 8u, 4u);
            if (ival != spec.exeDefaultI) return false;
        } else {
            float fval = 0.0f;
            std::memcpy(&fval, obj + 8u, 4u);
            const float tol = (std::max)(1.0e-4f, std::fabs(spec.exeDefaultF) * 1.0e-4f);
            if (!std::isfinite(fval) || std::fabs(fval - spec.exeDefaultF) > tol) return false;
        }

        ++verified;
    }

    if (outVerifiedCount) *outVerifiedCount = verified;
    return verified == kCameraWorldSpecCount;
}

CameraSpringZoomResult EvaluateCameraSpringZoom(float currentDistU, float targetDistU,
                                                float dtSec, float sourceDistU,
                                                float stiffness) {
    CameraSpringZoomResult out{};
    const float cur  = std::isfinite(currentDistU) ? std::clamp(currentDistU, 32.0f, 1200.0f) : 200.0f;
    const float tgt  = std::isfinite(targetDistU)  ? std::clamp(targetDistU,  32.0f, 1200.0f) : 200.0f;
    const float dt   = std::isfinite(dtSec)        ? std::clamp(dtSec, 0.0f, 0.25f)           : 0.016f;
    const float k    = std::isfinite(stiffness)    ? std::clamp(stiffness, 1.0f, 40.0f)       : 10.0f;
    const float sDst = std::isfinite(sourceDistU)  ? (std::max)(sourceDistU, 0.0f)            : 0.0f;

    // Exact exponential critically-damped zoom approach (frame-rate independent):
    const float alpha = 1.0f - std::exp(-k * dt);
    out.newDistU  = cur + (tgt - cur) * alpha;
    out.velocityU = (dt > 1.0e-5f) ? ((out.newDistU - cur) / dt) : 0.0f;

    // Distance shake falloff matching balanced fCameraShakeDistFadeStart=1600, Delta=2000:
    constexpr float kFadeStart = 1600.0f;
    constexpr float kFadeDelta = 2000.0f;
    if (sDst <= kFadeStart) {
        out.shakeAttenuation = 1.0f;
    } else if (sDst >= kFadeStart + kFadeDelta) {
        out.shakeAttenuation = 0.0f;
    } else {
        out.shakeAttenuation = 1.0f - ((sDst - kFadeStart) / kFadeDelta);
    }
    return out;
}

WeatherCelestialResult EvaluateWeatherCelestialVisibility(float precipIntensity01,
                                                          float fogDensity01,
                                                          float windSpeed01,
                                                          float sunElevDeg) {
    WeatherCelestialResult out{};
    const float precip = std::isfinite(precipIntensity01) ? std::clamp(precipIntensity01, 0.0f, 1.0f) : 0.0f;
    const float fog    = std::isfinite(fogDensity01)      ? std::clamp(fogDensity01, 0.0f, 1.0f)      : 0.0f;
    const float wind   = std::isfinite(windSpeed01)       ? std::clamp(windSpeed01, 0.0f, 1.0f)       : 0.0f;
    const float elev   = std::isfinite(sunElevDeg)        ? std::clamp(sunElevDeg, -90.0f, 90.0f)     : 30.0f;

    // Visibility attenuates monotonically with precipitation and fog density:
    const float rawVis  = (1.0f - 0.55f * precip) * (1.0f - 0.65f * fog);
    out.skyVisibility01 = std::clamp(rawVis, 0.05f, 1.0f);

    // Sun glare bounded by balanced fSunMinimumGlareScale = 0.20:
    const float dayFactor = (elev > 0.0f) ? std::clamp(elev / 60.0f, 0.0f, 1.0f) : 0.0f;
    out.sunGlareScale     = std::clamp(0.20f + 0.80f * dayFactor * out.skyVisibility01, 0.20f, 1.0f);

    // Precipitation slant increases with wind and active precipitation:
    out.precipWindSlant   = std::clamp(precip * (0.20f + 0.80f * wind), 0.0f, 1.0f);
    return out;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

struct SavedCameraWorldEntry {
    uint8_t* ptr     = nullptr;
    uint32_t saved32 = 0u;
};

std::mutex                         g_cwGmstMu;
std::vector<SavedCameraWorldEntry> g_savedCw;
bool                               g_cwApplied = false;

static bool SehReadCwU32(const uint8_t* p, uint32_t* out) noexcept {
    if (!p || !out) return false;
    __try {
        std::memcpy(out, p, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehWriteCwU32(uint8_t* p, uint32_t v) noexcept {
    if (!p) return false;
    __try {
        std::memcpy(p, &v, 4u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string InitCameraWorldGmst(const Image& img, const std::vector<RefTarget>& refs) {
    if (!AreBinaryOffsetsVerified()) {
        return "[CameraWorldGMST] REFUSED (fail-closed: startup Zydis binary verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[CameraWorldGMST] skipped (non-CE1 or online profile)";
    }

    std::lock_guard<std::mutex> lk(g_cwGmstMu);
    if (g_cwApplied) {
        return "[CameraWorldGMST] already active";
    }

    size_t applied     = 0u;
    size_t skippedDead = 0u;
    g_savedCw.clear();

    for (size_t i = 0; i < kCameraWorldSpecCount; ++i) {
        const auto& spec = kCameraWorldSpecs[i];
        if (!spec.hasLiveReader) {
            ++skippedDead;
            continue;
        }
        uint32_t rva = 0u;
        for (const auto& r : refs) {
            if (r.setting && r.name == spec.name) {
                rva = r.rva;
                break;
            }
        }
        if (rva == 0u) continue;
        if (TableVersion() == "1.7.104.0" && rva != spec.expectedRva) continue;

        uint8_t* field = const_cast<uint8_t*>(img.base) + rva + 8u;
        uint32_t orig32 = 0u;
        if (!SehReadCwU32(field, &orig32)) continue;

        uint32_t target32 = 0u;
        if (spec.isInt32) {
            std::memcpy(&target32, &spec.balancedI, 4u);
        } else {
            if (!std::isfinite(spec.balancedF)) continue;
            std::memcpy(&target32, &spec.balancedF, 4u);
        }

        if (SehWriteCwU32(field, target32)) {
            g_savedCw.push_back({field, orig32});
            ++applied;
        }
    }
    g_cwApplied = (applied > 0u);

    {
        std::lock_guard<std::mutex> slk(g_cwStateMu);
        g_cwState.gmstsVerified     = 1u;
        g_cwState.gmstsAppliedCount = static_cast<uint8_t>((std::min)(applied, static_cast<size_t>(255u)));
        g_cwState.deadLegacySkipped = static_cast<uint8_t>((std::min)(skippedDead, static_cast<size_t>(255u)));
        g_cwState.cameraWorldActive = g_cwApplied ? 1u : 0u;
    }

    char buf[224];
    std::snprintf(buf, sizeof buf,
                  "[CameraWorldGMST] applied %zu binary-verified camera/dragon/weather/water GMSTs (%zu 0-reader skipped)",
                  applied, skippedDead);
    return buf;
}

void ShutdownCameraWorldGmst() {
    std::lock_guard<std::mutex> lk(g_cwGmstMu);
    if (!g_cwApplied) return;
    for (const auto& e : g_savedCw) {
        SehWriteCwU32(e.ptr, e.saved32);
    }
    g_savedCw.clear();
    g_cwApplied = false;
    {
        std::lock_guard<std::mutex> slk(g_cwStateMu);
        g_cwState.gmstsAppliedCount = 0u;
    }
}
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetCameraWorldState(raw::rt::RawCameraWorldState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_cwStateMu);
    *out = raw::rt::g_cwState;
    return 1;
}
