#pragma once
// rt_camera_world_gmst.h: Milestone Wave 61 Binary-Verified 1.7.104.0 Camera Shake, KillCam,
// Dragon Flight/Orbit/Landing, Weather, Celestial & Water Physics GMSTs (402 Total Unique Verified GMSTs!).
// 100% binary-verified RVAs, .rdata name pointers & on-disk defaults (float + int32);
// fail-closed, SEH-guarded, cleanly reversible.
// Original work, MIT.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::rt {
struct Image;
struct RefTarget;

struct CameraWorldGmstSpec {
    const char* name;
    uint32_t    expectedRva;     // Setting object RVA in SkyrimSE.exe 1.7.104.0 (value at +0x08)
    bool        isInt32;         // true = i* int32 GMST, false = f* float GMST
    float       exeDefaultF;     // Exact on-disk float default (when !isInt32)
    float       balancedF;       // 2027 balanced float target (when !isInt32)
    int32_t     exeDefaultI;     // Exact on-disk int32 default (when isInt32)
    int32_t     balancedI;       // 2027 balanced int32 target (when isInt32)
    bool        hasLiveReader;   // Proven by overlap-safe .text RIP-relative scan (never writes 0-reader GMSTs!)
    const char* category;        // "Camera" | "KillCam" | "DragonFlight" | "Weather" | "Celestial" | "Water"
};

const CameraWorldGmstSpec* CameraWorldGmstTable(size_t* outCount = nullptr);

// Direct PE-image verification of every GMST RVA, NUL-terminated name pointer (+0x10), and on-disk default (+0x08).
// Fail-closed: returns false if ANY entry mismatches!
bool VerifyCameraWorldGmstTable(const Image& img, uint32_t* outVerifiedCount = nullptr);

// =================================================================================================
// First-Principles Camera Spring-Zoom & Atmospheric Celestial Visibility Evaluators
// =================================================================================================
struct CameraSpringZoomResult {
    float newDistU         = 200.0f;
    float velocityU        = 0.0f;
    float shakeAttenuation = 1.0f;   // [0..1] distance-based shake falloff
};

CameraSpringZoomResult EvaluateCameraSpringZoom(float currentDistU, float targetDistU,
                                                float dtSec, float sourceDistU = 0.0f,
                                                float stiffness = 10.0f);

struct WeatherCelestialResult {
    float skyVisibility01   = 1.0f;  // [0.05..1] combined sun/moon/aurora visibility
    float sunGlareScale     = 0.20f; // [0.20..1] bounded glare factor
    float precipWindSlant   = 0.0f;  // [0..1] normalized rain/snow wind slant
};

WeatherCelestialResult EvaluateWeatherCelestialVisibility(float precipIntensity01,
                                                          float fogDensity01,
                                                          float windSpeed01,
                                                          float sunElevDeg);

// =================================================================================================
// Live Runtime State & C ABI Export (RAW_GetCameraWorldState)
// =================================================================================================
struct RawCameraWorldState {
    uint8_t gmstsVerified          = 0u;
    uint8_t gmstsAppliedCount      = 0u;
    uint8_t deadLegacySkipped      = 0u;
    uint8_t cameraWorldActive      = 1u;
    float   cameraShakeTimeSec     = 0.95f;
    float   dragonOrbitDistU       = 4800.0f;
    float   dragonMaxPitchDeg      = 52.0f;
    float   underwaterFullDepthU   = 1150.0f;
    int32_t killCamLevelOffset     = 4;
    int32_t masserAngularSize      = 94;
};

#ifndef RAW_SIM_PURE_TEST
std::string InitCameraWorldGmst(const Image& img, const std::vector<RefTarget>& refs);
void        ShutdownCameraWorldGmst();
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetCameraWorldState(raw::rt::RawCameraWorldState* out);
