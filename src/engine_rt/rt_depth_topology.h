#pragma once
// rt_depth_topology.h: Binary-Verified Creation Engine Depth-Precision, Near/Far Clip Stability & X/Y/Z Topology Engine.
// Addresses Creation Engine's 24-bit D24S8 non-reversed-Z depth precision collapse (NiCamera::UpdateWorldData RVA 0xEF1310),
// near-clip Z-fighting, distant horizon collapse, and right-handed Z-up camera/axis/frustum integrity.
// Exposes live verified camera/projection/depth/topology telemetry via RAW_GetDepthTopologyTelemetry() & rt_control.cpp.
// Original work, MIT.

#include <cstdint>
#include <string>
#include <vector>

#include "rt_callgraph.h"
#include "rt_image.h"

namespace raw::rt {

struct DepthPrecisionMetrics {
    float nearClipU                = 15.0f;
    float farClipU                 = 350000.0f;
    float farNearRatio             = 23333.3f;
    // Standard 24-bit D24S8 non-reversed-Z depth step (units per 1 LSB of 24-bit depth) at 4 ranges:
    float d24StepAt50U             = 0.0f;
    float d24StepAt4096U           = 0.0f;
    float d24StepAt32768U          = 0.0f;
    float d24StepAt250000U         = 0.0f;
    // Equivalent Reversed-Z FP32 depth step at the same 4 ranges (proves RAW DX12 reversed-Z advantage):
    float revZ32StepAt50U          = 0.0f;
    float revZ32StepAt4096U        = 0.0f;
    float revZ32StepAt32768U       = 0.0f;
    float revZ32StepAt250000U      = 0.0f;
    float horizonGainRatio         = 1.0f;          // d24StepAt250000U / revZ32StepAt250000U
    bool  zFightingRisk            = false;         // true when d24StepAt32768U > 2.5u or near < 8u
    bool  horizonCollapseRisk      = false;         // true when far/near > 28000 or d24StepAt250000U > 128u
};

struct CameraFrustumState {
    float left   = -0.5f;
    float right  =  0.5f;
    float top    =  0.3f;
    float bottom = -0.3f;
    float nearZ  = 15.0f;
    float farZ   = 350000.0f;
    bool  valid  = false;
};

struct TopologyAudit {
    float orthoError               = 0.0f;          // max |R^T R - I|
    float determinant              = 1.0f;          // det(R), must be +1.0 for right-handed Z-up CE
    float worldRadiusU             = 0.0f;          // distance from world origin (0,0,0)
    float frustumAspect            = 1.7778f;
    float derivedHorizFovDeg       = 80.0f;
    bool  posFinite                = true;
    bool  rotOrthonormal           = true;
    bool  rightHanded              = true;
    bool  frustumConsistent        = true;
    bool  extremeCoordWarning      = false;         // true beyond +-90,000u (e.g. Winterhold 107k, 132k)
};

struct ClipPlaneRecommendation {
    float recommendedNearU         = 15.0f;
    float recommendedFarU          = 350000.0f;
    float effectiveRatio           = 23333.3f;
    bool  clampedForFirstPerson    = false;
    bool  clampedForInterior       = false;
};

struct RawDepthTopologyTelemetry {
    uint32_t tickCount             = 0;
    uint8_t  niCameraFound         = 0;
    uint8_t  topologyHealthy       = 0;
    uint8_t  zFightingRisk         = 0;
    uint8_t  horizonCollapseRisk   = 0;
    uint8_t  extremeCoordWarning   = 0;
    uint8_t  nearClipGuardActive   = 0;
    float    liveNearU             = 15.0f;
    float    liveFarU              = 350000.0f;
    float    recommendedNearU      = 15.0f;
    float    recommendedFarU       = 350000.0f;
    float    farNearRatio          = 23333.3f;
    float    d24StepAt4096U        = 0.0f;
    float    d24StepAt32768U       = 0.0f;
    float    d24StepAt250000U      = 0.0f;
    float    revZ32StepAt250000U   = 0.0f;
    float    horizonGainRatio      = 1.0f;
    float    orthoError            = 0.0f;
    float    determinant           = 1.0f;
    float    worldRadiusU          = 0.0f;
    float    derivedFovDeg         = 80.0f;
    float    frustum[6]            = {};            // l, r, t, b, n, f
    float    worldToCam[16]        = {};            // NiCamera +0x110 (4x4)
};

// Pure deterministic kernels:
DepthPrecisionMetrics AnalyzeDepthPrecision(float nearClipU, float farClipU);
TopologyAudit ValidateCameraAndAxisTopology(const float camPos[3], const float camRot[9],
                                            const CameraFrustumState& frustum, float expectedFovDeg);
ClipPlaneRecommendation ComputeOptimalClipPlanes(bool isInterior, uint32_t camStateId,
                                                 float altitudeZ, float currentNearU, float currentFarU);
// Builds an infinite-far Reversed-Z D32_FLOAT projection matrix (near -> 1.0, inf -> 0.0) in row-major order.
bool BuildReversedZInfiniteProj(float vertFovDeg, float aspect, float nearClipU, float outMat[16]);

// Live runtime hooks:
void DepthTopologyInit(const Image& img, const std::vector<RefTarget>& refs);
void DepthTopologyOnPresent();
void DepthTopologyShutdown();
std::string DepthTopologyStatsJson();

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetDepthTopologyTelemetry(raw::rt::RawDepthTopologyTelemetry* out);
