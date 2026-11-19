// BridgeHook configuration: [D3D12Bridge] in d3d11_proxy.ini. The keys are listed at the
// top of BridgeHook.cpp. Original work, MIT.
#include "HotReload.h"
#include "BridgeHookState.h"
#include "ProxyLog.h"
#include <cstdlib>
#include <cstring>

namespace SB::Proxy::hook
{
void LoadConfig()
{
    Config& c = Cfg();
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string ini(path);
    ini = ini.substr(0, ini.rfind('\\') + 1) + "d3d11_proxy.ini";
    auto i = [&](const char* k, int d) { return GetPrivateProfileIntA("D3D12Bridge", k, d, ini.c_str()); };
    auto f = [&](const char* k, float d) {
        char buf[64]{};
        GetPrivateProfileStringA("D3D12Bridge", k, "", buf, sizeof(buf), ini.c_str());
        return buf[0] ? std::strtof(buf, nullptr) : d;
    };
    char text[MAX_PATH]{};
    GetPrivateProfileStringA("D3D12Bridge", "Pass", "haze", text, MAX_PATH, ini.c_str());
    c.enginePass = _stricmp(text, "engine") == 0;
    c.lighting = _stricmp(text, "lighting") == 0;
    GetPrivateProfileStringA("D3D12Bridge", "CaptureDir", "", text, MAX_PATH, ini.c_str());
    c.captureDir = text;
    c.enabled = i("Enabled", 0) != 0;
    c.fallbackAtPresent = i("FallbackAtPresent", 1) != 0;
    c.debugLayer = i("DebugLayer", 0) != 0;
    c.timingLogEvery = i("TimingLogEvery", 600);
    c.captureFrame = i("CaptureFrame", 0);
    c.captureAfter = i("CaptureAfter", 0);
    c.testStallAt = i("TestStallAtBridgeFrame", 0);
    c.retireTimeoutMs = i("RetireTimeoutMs", 15000);
    auto& k = c.k;
    k.mode = static_cast<float>(i("Mode", 1));
    k.marker = static_cast<float>(i("Marker", c.enginePass ? 0 : 1));
    k.density = f("Density", k.density);
    k.fogColor[0] = f("FogR", k.fogColor[0]);
    k.fogColor[1] = f("FogG", k.fogColor[1]);
    k.fogColor[2] = f("FogB", k.fogColor[2]);
    k.maxOpacity = f("MaxOpacity", k.maxOpacity);
    k.nearZ = f("Near", k.nearZ);
    k.farZ = f("Far", k.farZ);
    c.tune.radius = f("AoRadius", 35.0f);
    c.tune.slices = static_cast<uint32_t>(i("AoSlices", 0));
    c.tune.steps = static_cast<uint32_t>(i("AoSteps", 0));
    c.tune.strength = f("AoStrength", 1.0f);
    GetPrivateProfileStringA("D3D12Bridge", "AoMode", "reference", text, MAX_PATH, ini.c_str());
    c.realtimeAo = c.enginePass && _stricmp(text, "realtime") == 0;
    c.tune.noise = i("AoNoise", 1) != 0;
    c.tune.temporal = i("AoTemporal", 1) != 0;
    c.tune.levels = i("AoLevels", 1) != 0;
    c.tune.maxHistory = f("AoHistory", 32.0f);
    c.tune.depthTolerance = f("AoDepthTolerance", 0.1f);
    c.tune.motionScale[0] = f("AoMotionScaleX", 1.0f);
    c.tune.motionScale[1] = f("AoMotionScaleY", 1.0f);
    g_bridgeHookActive = c.enabled;
    if (c.lighting) LoadLightingConfig(ini);
    if (c.lighting) HotReload_LoadConfig(ini);
    Log("[D3D12Bridge] config: enabled=%d pass=%s mode=%.0f radius=%g slices=%u steps=%u strength=%g",
        c.enabled, c.lighting ? "lighting" : !c.enginePass ? "haze" : c.realtimeAo ? "engine, real-time AO" : "engine", k.mode, c.tune.radius, c.tune.slices, c.tune.steps,
        c.tune.strength);
}
} // namespace SB::Proxy::hook
