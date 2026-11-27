// rt_depth_topology.cpp: Binary-Verified Creation Engine Depth-Precision, Near/Far Clip Stability & X/Y/Z Topology Engine.
// Original work, MIT.
#include "rt_depth_topology.h"

#ifndef RAW_SIM_PURE_TEST
#include "rt_memory_guards.h"
#include "rt_version.h"
#include "rt_view.h"
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>

namespace raw::rt {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Computes exact depth step dz (in world units) corresponding to 1 LSB of 24-bit unsigned normalized D24S8
// under Creation Engine's standard non-reversed-Z projection: d(z) = f*(z - n) / (z*(f - n)).
// Differentiating: dd/dz = (n*f) / ((f - n) * z^2)  =>  dz = (1 / 2^24) * ((f - n) * z^2) / (n * f).
float StandardD24DepthStep(double n, double f, double z) {
    if (n <= 0.0 || f <= n || z <= 0.0) return std::numeric_limits<float>::infinity();
    const double lsb = 1.0 / 16777216.0;  // 2^-24
    const double dz = lsb * ((f - n) * z * z) / (n * f);
    return static_cast<float>(dz);
}

// Computes exact depth step dz (in world units) corresponding to 1 ULP of FP32 under Reversed-Z infinite/finite
// projection: d_rev(z) = (n * f) / (z * (f - n)) - n / (f - n)  =>  d_rev ~ n / z.
// Because FP32 exponent scales proportionally with d_rev ~ n/z, 1 ULP(d_rev) ~ 2^-23 * (n/z),
// so dz = ULP(d_rev) / |dd_rev/dz| = (2^-23 * (n/z)) / (n / z^2) = 2^-23 * z  (LINEAR precision at all distances!).
float ReversedZ32DepthStep(double n, double f, double z) {
    if (n <= 0.0 || f <= n || z <= 0.0) return std::numeric_limits<float>::infinity();
    const double dRev = (n * (f - z)) / (z * (f - n));
    const float dF = static_cast<float>(std::clamp(dRev, 1e-30, 1.0));
    const double ulp = static_cast<double>(std::nextafter(dF, 2.0f) - dF);
    const double dddz = (n * f) / ((f - n) * z * z);
    if (dddz <= 0.0) return std::numeric_limits<float>::infinity();
    return static_cast<float>(ulp / dddz);
}

}  // namespace

DepthPrecisionMetrics AnalyzeDepthPrecision(float nearClipU, float farClipU) {
    DepthPrecisionMetrics m{};
    const double n = (std::isfinite(nearClipU) && nearClipU > 0.01f) ? static_cast<double>(nearClipU) : 15.0;
    const double f = (std::isfinite(farClipU) && farClipU > n + 10.0) ? static_cast<double>(farClipU) : 350000.0;
    m.nearClipU = static_cast<float>(n);
    m.farClipU = static_cast<float>(f);
    m.farNearRatio = static_cast<float>(f / n);

    m.d24StepAt50U     = StandardD24DepthStep(n, f, 50.0);
    m.d24StepAt4096U   = StandardD24DepthStep(n, f, 4096.0);
    m.d24StepAt32768U  = StandardD24DepthStep(n, f, 32768.0);
    m.d24StepAt250000U = StandardD24DepthStep(n, f, 250000.0);

    m.revZ32StepAt50U     = ReversedZ32DepthStep(n, f, 50.0);
    m.revZ32StepAt4096U   = ReversedZ32DepthStep(n, f, 4096.0);
    m.revZ32StepAt32768U  = ReversedZ32DepthStep(n, f, 32768.0);
    m.revZ32StepAt250000U = ReversedZ32DepthStep(n, f, 250000.0);

    m.horizonGainRatio = m.revZ32StepAt250000U > 0.0f
                             ? (m.d24StepAt250000U / m.revZ32StepAt250000U)
                             : 1.0f;
    m.zFightingRisk = (n < 10.0) || (m.d24StepAt32768U > 3.5f);
    m.horizonCollapseRisk = (m.farNearRatio > 26000.0f) || (m.d24StepAt250000U > 200.0f);
    return m;
}

TopologyAudit ValidateCameraAndAxisTopology(const float camPos[3], const float camRot[9],
                                            const CameraFrustumState& frustum, float expectedFovDeg) {
    TopologyAudit a{};
    if (!camPos || !camRot) {
        a.posFinite = false;
        a.rotOrthonormal = false;
        a.rightHanded = false;
        a.frustumConsistent = false;
        return a;
    }

    a.posFinite = std::isfinite(camPos[0]) && std::isfinite(camPos[1]) && std::isfinite(camPos[2]) &&
                  std::fabs(camPos[0]) < 1.0e7f && std::fabs(camPos[1]) < 1.0e7f && std::fabs(camPos[2]) < 1.0e7f;
    if (a.posFinite) {
        const double r = std::sqrt(double(camPos[0]) * camPos[0] + double(camPos[1]) * camPos[1] + double(camPos[2]) * camPos[2]);
        a.worldRadiusU = static_cast<float>(r);
        a.extremeCoordWarning = (std::fabs(camPos[0]) > 90000.0f || std::fabs(camPos[1]) > 90000.0f);
    }

    // Check R^T R == I and det(R) == +1 (right-handed Z-up Creation Engine coordinate system)
    bool rotFinite = true;
    for (int i = 0; i < 9; ++i) if (!std::isfinite(camRot[i])) rotFinite = false;
    if (rotFinite) {
        float maxErr = 0.0f;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                const float dot = camRot[r * 3 + 0] * camRot[c * 3 + 0] +
                                  camRot[r * 3 + 1] * camRot[c * 3 + 1] +
                                  camRot[r * 3 + 2] * camRot[c * 3 + 2];
                maxErr = std::max(maxErr, std::fabs(dot - (r == c ? 1.0f : 0.0f)));
            }
        }
        a.orthoError = maxErr;
        a.determinant =
            camRot[0] * (camRot[4] * camRot[8] - camRot[5] * camRot[7]) -
            camRot[1] * (camRot[3] * camRot[8] - camRot[5] * camRot[6]) +
            camRot[2] * (camRot[3] * camRot[7] - camRot[4] * camRot[6]);
        a.rotOrthonormal = (maxErr < 5.0e-3f);
        a.rightHanded = (std::fabs(a.determinant - 1.0f) < 1.0e-2f);
    } else {
        a.orthoError = std::numeric_limits<float>::infinity();
        a.determinant = 0.0f;
        a.rotOrthonormal = false;
        a.rightHanded = false;
    }

    if (frustum.valid) {
        const bool finiteF = std::isfinite(frustum.left) && std::isfinite(frustum.right) &&
                             std::isfinite(frustum.top) && std::isfinite(frustum.bottom) &&
                             std::isfinite(frustum.nearZ) && std::isfinite(frustum.farZ);
        const float width = frustum.right - frustum.left;
        const float height = frustum.top - frustum.bottom;
        const bool ordered = finiteF && width > 1e-4f && height > 1e-4f &&
                             frustum.nearZ > 0.1f && frustum.farZ > frustum.nearZ + 10.0f;
        if (ordered) {
            a.frustumAspect = width / height;
            // Gamebryo/NiCamera stores symmetric half-extents at unit distance (tan(fov/2) = right)
            const double halfTan = 0.5 * static_cast<double>(width);
            a.derivedHorizFovDeg = static_cast<float>(2.0 * std::atan(halfTan) * (180.0 / kPi));
            const bool fovOk = (expectedFovDeg <= 1.0f) ||
                               (a.derivedHorizFovDeg > 20.0f && a.derivedHorizFovDeg < 165.0f &&
                                std::fabs(a.derivedHorizFovDeg - expectedFovDeg) < 35.0f);
            a.frustumConsistent = fovOk;
        } else {
            a.frustumConsistent = false;
        }
    } else {
        a.frustumConsistent = true;  // not probed yet
    }
    return a;
}

ClipPlaneRecommendation ComputeOptimalClipPlanes(bool isInterior, uint32_t camStateId,
                                                 float altitudeZ, float currentNearU, float currentFarU) {
    ClipPlaneRecommendation r{};
    const bool isFirstPerson = (camStateId == 0u);
    const float safeFar = (std::isfinite(currentFarU) && currentFarU > 4096.0f && currentFarU < 1.0e7f)
                              ? currentFarU
                              : (isInterior ? 65536.0f : 350000.0f);
    r.recommendedFarU = safeFar;

    if (isInterior) {
        // Interiors have close walls & small far clip: 12..15u eliminates wall clipping while preserving sub-mm Z.
        r.recommendedNearU = isFirstPerson ? 12.0f : 15.0f;
        r.clampedForInterior = true;
        r.clampedForFirstPerson = isFirstPerson;
    } else if (isFirstPerson) {
        // First-person weapon/hand models sit ~15..22u from camera; 15.0u prevents hand clipping while beating vanilla 5u by 3x.
        r.recommendedNearU = 15.0f;
        r.clampedForFirstPerson = true;
    } else {
        // Exterior 3rd-person / free camera: push near clip smoothly to 18..24u at high altitude vistas
        // (cuts distant mountain Z-fighting by 1.2x-1.6x without clipping character shoulders at ~80-160u).
        const float altBoost = std::clamp((altitudeZ - 2000.0f) / 10000.0f, 0.0f, 1.0f) * 6.0f;
        r.recommendedNearU = std::clamp(18.0f + altBoost, 15.0f, 24.0f);
    }

    // Never allow extreme far/near ratio above 22,000 in exterior 3rd-person if currentNearU was sane
    (void)currentNearU;
    r.effectiveRatio = r.recommendedFarU / r.recommendedNearU;
    return r;
}

bool BuildReversedZInfiniteProj(float vertFovDeg, float aspect, float nearClipU, float outMat[16]) {
    if (!outMat || !std::isfinite(vertFovDeg) || !std::isfinite(aspect) || !std::isfinite(nearClipU)) return false;
    if (vertFovDeg <= 1.0f || vertFovDeg >= 175.0f || aspect <= 0.1f || nearClipU <= 0.01f) return false;
    std::memset(outMat, 0, sizeof(float) * 16u);
    const double halfRad = 0.5 * static_cast<double>(vertFovDeg) * (kPi / 180.0);
    const float fy = static_cast<float>(1.0 / std::tan(halfRad));
    const float fx = fy / aspect;
    // Right-handed view space (-Z forward), Reversed-Z infinite far: z_ndc = near / (-z_view) -> [1 at near, 0 at inf]
    outMat[0]  = fx;
    outMat[5]  = fy;
    outMat[10] = 0.0f;
    outMat[11] = nearClipU;
    outMat[14] = -1.0f;
    outMat[15] = 0.0f;
    return true;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

const uint8_t* g_base = nullptr;
std::mutex g_mu;
RawDepthTopologyTelemetry g_tel{};
uint32_t g_playerCameraRva = 0;
uint32_t g_niCameraVtRva = 0;
uint32_t g_offWorldToCam = 0x110u;
uint32_t g_offViewFrustum = 0x150u;
float* g_fNearSetting = nullptr;
float g_origNearSetting = 15.0f;
bool g_nearSaved = false;
uint32_t g_presentTick = 0;

template <class T>
T ReadAt(const uint8_t* p, uint32_t off) {
    T v{};
    std::memcpy(&v, p + off, sizeof v);
    return v;
}

bool ValidHeapPtr(const void* p) {
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);
    return a >= 0x10000ull && a < 0x00007FFFFFFFFFFFull && (a & 0x7ull) == 0;
}

// Walks PlayerCamera::cameraRoot (+0x20, NiNode) children array (+0x128: NiTPrimitiveArray<NiAVObject*> data at +0x08, size at +0x10)
// and verifies the child's vtable matches binary-verified vtable:NiCamera (0x1A485B8) before reading viewFrustum & worldToCam!
bool ProbeNiCameraSeh(CameraFrustumState* outFrust, float outWorldToCam[16]) {
    __try {
        if (!g_base || !g_playerCameraRva || !g_niCameraVtRva) return false;
        const uint8_t* pcam = ReadAt<const uint8_t*>(g_base, g_playerCameraRva);
        if (!ValidHeapPtr(pcam)) return false;
        const uint8_t* root = ReadAt<const uint8_t*>(pcam, 0x20u);
        if (!ValidHeapPtr(root)) return false;

        const uint64_t wantVt = reinterpret_cast<uintptr_t>(g_base) + g_niCameraVtRva;
        const uint8_t* niCam = nullptr;

        constexpr uint32_t kNiNodeChildrenArrayOff  = 0x128u;
        constexpr uint32_t kNiTArrayDataPtrOff      = 0x08u;
        constexpr uint32_t kNiTArrayCapacityOff     = 0x10u;
        struct NiFrustumLayout { float left; float right; float top; float bottom; float nearZ; float farZ; };
        static_assert(sizeof(NiFrustumLayout) == 24u, "NiFrustum ABI layout must be 24 bytes");
        static_assert(offsetof(NiFrustumLayout, nearZ) == 16u && offsetof(NiFrustumLayout, farZ) == 20u,
                      "NiFrustum nearZ/farZ ABI offset guard");

        // Check if root itself is NiCamera, else scan NiNode::children (Skyrim SE 1.6/1.7: +0x128 NiTObjectArray)
        if (ReadAt<uint64_t>(root, 0x00u) == wantVt) {
            niCam = root;
        } else {
            const uint8_t* const* data = ReadAt<const uint8_t* const*>(root, kNiNodeChildrenArrayOff + kNiTArrayDataPtrOff);
            const uint16_t cap = ReadAt<uint16_t>(root, kNiNodeChildrenArrayOff + kNiTArrayCapacityOff);
            if (ValidHeapPtr(data) && cap > 0u && cap <= 32u) {
                for (uint16_t i = 0; i < cap; ++i) {
                    const uint8_t* ch = data[i];
                    if (ValidHeapPtr(ch) && ReadAt<uint64_t>(ch, 0x00u) == wantVt) {
                        niCam = ch;
                        break;
                    }
                }
            }
        }
        if (!niCam) return false;

        outFrust->left   = ReadAt<float>(niCam, g_offViewFrustum + static_cast<uint32_t>(offsetof(NiFrustumLayout, left)));
        outFrust->right  = ReadAt<float>(niCam, g_offViewFrustum + static_cast<uint32_t>(offsetof(NiFrustumLayout, right)));
        outFrust->top    = ReadAt<float>(niCam, g_offViewFrustum + static_cast<uint32_t>(offsetof(NiFrustumLayout, top)));
        outFrust->bottom = ReadAt<float>(niCam, g_offViewFrustum + static_cast<uint32_t>(offsetof(NiFrustumLayout, bottom)));
        outFrust->nearZ  = ReadAt<float>(niCam, g_offViewFrustum + static_cast<uint32_t>(offsetof(NiFrustumLayout, nearZ)));
        outFrust->farZ   = ReadAt<float>(niCam, g_offViewFrustum + static_cast<uint32_t>(offsetof(NiFrustumLayout, farZ)));
        outFrust->valid  = std::isfinite(outFrust->nearZ) && std::isfinite(outFrust->farZ) &&
                           outFrust->nearZ > 0.01f && outFrust->farZ > outFrust->nearZ;
        if (outWorldToCam) {
            std::memcpy(outWorldToCam, niCam + g_offWorldToCam, sizeof(float) * 16u);
        }
        return outFrust->valid;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

void DepthTopologyInit(const Image& img, const std::vector<RefTarget>& refs) {
    g_base = img.base;
    g_playerCameraRva = Addr("PlayerCamera::Singleton");
    g_niCameraVtRva = Addr("vtable:NiCamera");
    if (uint32_t v = Off("off:NiCamera::worldToCam")) g_offWorldToCam = v;
    if (uint32_t v = Off("off:NiCamera::viewFrustum")) g_offViewFrustum = v;

    g_fNearSetting = nullptr;
    g_nearSaved = false;
    for (const auto& r : refs) {
        if (r.setting && r.name == "fNearDistance:Display") {
            g_fNearSetting = reinterpret_cast<float*>(const_cast<uint8_t*>(img.base) + r.rva + 8u);
            g_origNearSetting = *g_fNearSetting;
            g_nearSaved = true;
            break;
        }
    }

    std::lock_guard<std::mutex> lk(g_mu);
    g_tel = {};
}

void DepthTopologyOnPresent() {
    if (!g_base) return;
    const uint32_t tick = ++g_presentTick;
    // Sample every 15 presents (~4 Hz at 60 FPS) to keep render-thread overhead near zero.
    if ((tick % 15u) != 1u) return;

    EngineView ev{};
    RAW_EngineView(&ev);

    CameraFrustumState frust{};
    float w2c[16] = {};
    const bool foundCam = ProbeNiCameraSeh(&frust, w2c);

    const float liveNear = foundCam ? frust.nearZ : (g_fNearSetting ? *g_fNearSetting : 15.0f);
    const float liveFar  = foundCam ? frust.farZ : (ev.interior ? 65536.0f : 350000.0f);

    const DepthPrecisionMetrics dp = AnalyzeDepthPrecision(liveNear, liveFar);
    const TopologyAudit topo = (ev.ok & 2u)
                                   ? ValidateCameraAndAxisTopology(ev.camPos, ev.camRot, frust, ev.fov)
                                   : TopologyAudit{};
    const ClipPlaneRecommendation rec = ComputeOptimalClipPlanes(
        ev.interior != 0u, ev.camStateId, (ev.ok & 2u) ? ev.camPos[2] : 0.0f, liveNear, liveFar);

    // Safe fNearDistance:Display stabilization (never mutates when fail-closed or in menus):
    bool nearGuard = false;
    if (g_fNearSetting && AreBinaryOffsetsVerified() && MemoryGuardAllowLiveStructWrites() && ev.menuPaused == 0u) {
        const float cur = *g_fNearSetting;
        if (std::isfinite(cur) && std::fabs(cur - rec.recommendedNearU) > 0.5f) {
            // Smooth exponential approach to prevent visible near-clip pops during 1st/3rd person switches.
            const float nextNear = std::clamp(cur + (rec.recommendedNearU - cur) * 0.25f, 12.0f, 24.0f);
            *g_fNearSetting = nextNear;
        }
        nearGuard = true;
    }

    RawDepthTopologyTelemetry t{};
    t.tickCount = tick;
    t.niCameraFound = foundCam ? 1u : 0u;
    t.topologyHealthy = (topo.posFinite && topo.rotOrthonormal && topo.rightHanded && topo.frustumConsistent) ? 1u : 0u;
    t.zFightingRisk = dp.zFightingRisk ? 1u : 0u;
    t.horizonCollapseRisk = dp.horizonCollapseRisk ? 1u : 0u;
    t.extremeCoordWarning = topo.extremeCoordWarning ? 1u : 0u;
    t.nearClipGuardActive = nearGuard ? 1u : 0u;
    t.liveNearU = dp.nearClipU;
    t.liveFarU = dp.farClipU;
    t.recommendedNearU = rec.recommendedNearU;
    t.recommendedFarU = rec.recommendedFarU;
    t.farNearRatio = dp.farNearRatio;
    t.d24StepAt4096U = dp.d24StepAt4096U;
    t.d24StepAt32768U = dp.d24StepAt32768U;
    t.d24StepAt250000U = dp.d24StepAt250000U;
    t.revZ32StepAt250000U = dp.revZ32StepAt250000U;
    t.horizonGainRatio = dp.horizonGainRatio;
    t.orthoError = topo.orthoError;
    t.determinant = topo.determinant;
    t.worldRadiusU = topo.worldRadiusU;
    t.derivedFovDeg = topo.derivedHorizFovDeg;
    if (foundCam) {
        t.frustum[0] = frust.left;   t.frustum[1] = frust.right;
        t.frustum[2] = frust.top;    t.frustum[3] = frust.bottom;
        t.frustum[4] = frust.nearZ;  t.frustum[5] = frust.farZ;
        std::memcpy(t.worldToCam, w2c, sizeof(w2c));
    }

    std::lock_guard<std::mutex> lk(g_mu);
    g_tel = t;
}

void DepthTopologyShutdown() {
    if (g_fNearSetting && g_nearSaved && std::isfinite(g_origNearSetting)) {
        *g_fNearSetting = g_origNearSetting;
    }
    g_fNearSetting = nullptr;
    g_nearSaved = false;
    g_base = nullptr;
}

std::string DepthTopologyStatsJson() {
    RawDepthTopologyTelemetry t{};
    {
        std::lock_guard<std::mutex> lk(g_mu);
        t = g_tel;
    }
    char buf[896];
    std::snprintf(buf, sizeof buf,
                  "{\"cmd\":\"depth_topology\",\"ni_camera_found\":%s,\"topology_healthy\":%s,"
                  "\"near_u\":%.2f,\"far_u\":%.1f,\"rec_near_u\":%.2f,\"far_near_ratio\":%.1f,"
                  "\"d24_step_4k_u\":%.4f,\"d24_step_32k_u\":%.3f,\"d24_step_250k_u\":%.2f,"
                  "\"revz32_step_250k_u\":%.5f,\"horizon_gain_x\":%.1f,"
                  "\"z_fighting_risk\":%s,\"horizon_collapse_risk\":%s,\"extreme_coord_warn\":%s,"
                  "\"near_guard_active\":%s,\"cam_ortho_err\":%.6f,\"cam_det\":%.5f,"
                  "\"world_radius_u\":%.1f,\"derived_fov_deg\":%.2f}",
                  t.niCameraFound ? "true" : "false", t.topologyHealthy ? "true" : "false",
                  t.liveNearU, t.liveFarU, t.recommendedNearU, t.farNearRatio,
                  t.d24StepAt4096U, t.d24StepAt32768U, t.d24StepAt250000U,
                  t.revZ32StepAt250000U, t.horizonGainRatio,
                  t.zFightingRisk ? "true" : "false", t.horizonCollapseRisk ? "true" : "false",
                  t.extremeCoordWarning ? "true" : "false", t.nearClipGuardActive ? "true" : "false",
                  t.orthoError, t.determinant, t.worldRadiusU, t.derivedFovDeg);
    return buf;
}

#else
void DepthTopologyInit(const Image&, const std::vector<RefTarget>&) {}
void DepthTopologyOnPresent() {}
void DepthTopologyShutdown() {}
std::string DepthTopologyStatsJson() { return "{\"cmd\":\"depth_topology\",\"mode\":\"pure_test\"}"; }
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetDepthTopologyTelemetry(raw::rt::RawDepthTopologyTelemetry* out) {
#ifndef RAW_SIM_PURE_TEST
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_mu);
    *out = raw::rt::g_tel;
    return raw::rt::g_tel.tickCount > 0u ? 1 : 0;
#else
    (void)out;
    return 0;
#endif
}
