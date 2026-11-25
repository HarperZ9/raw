// RawApi.cpp: RAW-API v1 implementation exported by d3d11.dll.
// Exposes live DX12/DXR capabilities, world atmosphere, camera optics, and version string to
// external SKSE plugins, weather authors, and custom render passes via RAW_GetApi(RAW_API_VERSION).
// Original work, MIT.
#include "../../include/raw_api.h"
#include "BridgeHookState.h"
#include "HotReload.h"

#include <cstring>

namespace SB::Proxy
{
namespace
{
int Api_GetCapabilities(RAW_Capabilities* outCaps)
{
    if (!outCaps) return 0;
    std::memset(outCaps, 0, sizeof(*outCaps));
    outCaps->apiVersion = RAW_API_VERSION;
    outCaps->shaderModelMajor = 6;
    outCaps->shaderModelMinor = 6;
    outCaps->dxrTier = 12;
    outCaps->meshShaderTier = 10;
    outCaps->qualityTier = static_cast<uint32_t>(hook::Cfg().light.mode);
    // bit 0: DXR, bit 1: RAWLSS 5.5, bit 2: TruePBR RMAOS, bit 3: RAW-STRATA, bit 4: 3D Ray-Marched Waves,
    // bit 5: RAW-LUMEN Bindless65K, bit 6: RAW-MESH (as_6_6/ms_6_6), bit 7: AsyncCompute, bit 8: ExecuteIndirect
    outCaps->featuresMask = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4)
                          | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8);
    return 1;
}

int Api_GetAtmosphere(RAW_AtmosphereState* outAtmo)
{
    if (!outAtmo) return 0;
    std::memset(outAtmo, 0, sizeof(*outAtmo));
    const auto& t = hook::Cfg().light;
    outAtmo->gameHour = t.gameHour;
    outAtmo->cloudCoverage = t.cloudCover;
    outAtmo->rainIntensity = t.rain;
    outAtmo->snowIntensity = t.snow;
    outAtmo->windXY[0] = t.cloudWindX;
    outAtmo->windXY[1] = t.cloudWindY;
    outAtmo->isExterior = 1u;
    return 1;
}

int Api_GetCameraOptics(RAW_CameraOptics* outOptics)
{
    if (!outOptics) return 0;
    std::memset(outOptics, 0, sizeof(*outOptics));
    const auto& t = hook::Cfg().light;
    outOptics->sensorFormat = static_cast<float>(t.dofSensor);
    outOptics->focalMm = t.dofFocalMm;
    outOptics->fStop = t.dofFStop;
    outOptics->focusDistance = t.dofMaxDist;
    return 1;
}

const char* Api_GetVersionString()
{
    return "RAW 2.0 (THE PLAYGROUND / DX12 + DXR 1.2 + TxSubdiv 4x + Dual HLSL/GLSL + RAWLSS 7.0 + Zero-SKSE WorldSim)";
}

const RAW_ApiV1 kApiV1 = {
    sizeof(RAW_ApiV1),
    RAW_API_VERSION,
    &Api_GetCapabilities,
    &Api_GetAtmosphere,
    &Api_GetCameraOptics,
    &Api_GetVersionString,
};
} // namespace
} // namespace SB::Proxy

extern "C" __declspec(dllexport) const RAW_ApiV1* RAW_GetApi(uint32_t requestedVersion)
{
    if (requestedVersion == 0 || requestedVersion > RAW_API_VERSION) return nullptr;
    return &SB::Proxy::kApiV1;
}

extern "C" __declspec(dllexport) int RAW_SubmitLiveShader(const char* passName, const char* sourceUtf8, int isGlsl)
{
    if (!passName || !sourceUtf8) return 0;
    return SB::Proxy::HotReload_SubmitLiveShader(passName, sourceUtf8, isGlsl != 0) ? 1 : 0;
}

extern "C" __declspec(dllexport) int RAW_GetLiveShaderStatus(char* outBuf, uint32_t cap)
{
    if (!outBuf || cap == 0) return 0;
    const std::string s = SB::Proxy::HotReload_LiveStatus();
    const size_t n = (s.size() < size_t(cap - 1)) ? s.size() : size_t(cap - 1);
    std::memcpy(outBuf, s.data(), n);
    outBuf[n] = '\0';
    return int(n);
}
