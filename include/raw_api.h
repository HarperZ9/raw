#pragma once
// raw_api.h: RAW-API v1 — Public Extensibility C ABI for Rendering Advancement Workshop (R.A.W.)
// Part of THE PLAYGROUND (THEPLAYGRND). Allows SKSE plugins, weather authors, material packs,
// and custom DX12 render passes to query live RAW state and inject native DX12 / DXR 1.2 data.
// Original work, MIT.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RAW_API_VERSION 1u

typedef struct RAW_Capabilities {
    uint32_t apiVersion;
    uint32_t shaderModelMajor;   // e.g. 6
    uint32_t shaderModelMinor;   // e.g. 6 or 9
    uint32_t dxrTier;            // 10 = DXR 1.0, 11 = DXR 1.1, 12 = DXR 1.2
    uint32_t meshShaderTier;     // 0 = none, 10 = Tier 1
    uint32_t vramMB;
    uint32_t qualityTier;        // 0=Perf, 1=Bal, 2=High, 3=Ultra, 4=Overdrive
    uint32_t featuresMask;       // bit 0: DXR, bit 1: RAWLSS 5.5, bit 2: TruePBR RMAOS, bit 3: RAW-STRATA, bit 4: 3D Waves
} RAW_Capabilities;

typedef struct RAW_AtmosphereState {
    float gameHour;              // 0..24
    float sunDir[3];
    float sunColor[3];
    float cloudCoverage;         // 0..1
    float rainIntensity;         // 0..1
    float snowIntensity;         // 0..1
    float windXY[2];
    uint32_t isExterior;
} RAW_AtmosphereState;

typedef struct RAW_CameraOptics {
    float sensorFormat;          // 0=APS-C, 1=35mm, 2=MF645, 3=MF6x7
    float focalMm;               // 24..200
    float fStop;                 // 0.95..16
    float focusDistance;         // world units
    float jitterNdc[2];          // exact per-frame sub-pixel projection jitter
} RAW_CameraOptics;

typedef struct RAW_ApiV1 {
    uint32_t structSize;
    uint32_t apiVersion;
    int (*GetCapabilities)(RAW_Capabilities* outCaps);
    int (*GetAtmosphere)(RAW_AtmosphereState* outAtmo);
    int (*GetCameraOptics)(RAW_CameraOptics* outOptics);
    const char* (*GetVersionString)(void);
} RAW_ApiV1;

// Exported by d3d11.dll: returns non-null RAW_ApiV1* when requestedVersion <= RAW_API_VERSION.
typedef const RAW_ApiV1* (*PFN_RAW_GetApi)(uint32_t requestedVersion);

#ifdef __cplusplus
}
#endif
