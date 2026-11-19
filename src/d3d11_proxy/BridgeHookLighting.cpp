// BridgeHook, lighting form: the engine lights the frame at the end of the opaque pass
// (docs/LIGHTING.md). Keys of [Lighting] in d3d11_proxy.ini:
//   Mode=1              0 the game's lighting, 1 ours matched to the game, 2 ours improved; F10 cycles
//   SoftShadows=1 GgxSpecular=1 VolumetricFog=1 ContactShadows=1 Ao=1   the improvements (Mode=2)
//   Cascades=1          the sun's shadow from the decoded cascades (0: the game's shadow mask)
//   View=0              1 world normals, 2 sun shadow, 3 ambient, 8 weather (wet, puddles, snow), 9 test control: the top-left quarter only
//   FogDensity FogAnisotropy FogLightScale FogSunScale FogMaxDistance SoftRange ContactLength AoStrength
//   ShadowMapSize=4096  one cascade's side (the game's iShadowMapResolution)
//   CaptureAfter=0      world frames before the capture set: each step holds Hold frames, then its
//   Hold=45             backbuffer and the match statistics go to CaptureDir (Scene names the files)
// Needs [D3D12Bridge] Enabled=1 Pass=lighting and [GBuffer] Enabled=1. Original work, MIT.
#include "BridgeHookState.h"
#include "GBufferComplete.h"
#include "ProxyAPI.h"
#include "ProxyLog.h"
#include "SceneLights.h"
#include "GameComposite.h"
#include "HotReload.h"
#include "SceneGeometry.h"
#include "TxMode.h"
#include "Translate.h"
#include "Overlay.h"
#include "WeatherDrive.h"
#include "Tonemap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <direct.h>
#include <vector>

namespace SB::Proxy::hook
{
using raw::bridge::LightingTuning;
using raw::bridge::Shared;

namespace
{
int s_failures = 0;
UINT s_shadowSide = 4096;
bool s_handoverFlush = true;
bool s_weatherDriven = true;

// The surfaces the weather has built up, into the pass's tuning each frame (WeatherDrive.h; a copy of
// the tuning, so an ini reload in between is not lost).
void DriveWeather()
{
    static LARGE_INTEGER last{}, freq{};
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    const double dt = last.QuadPart ? double(now.QuadPart - last.QuadPart) / double(freq.QuadPart) : 0.0;
    last = now;
    WeatherSurfaces w;
    if (!s_weatherDriven || !WeatherDrive_Step(dt, w) || !Bridge()) return;
    LightingTuning t = Cfg().light;
    t.wetness = w.wetness; t.puddles = w.puddles; t.snow = w.snow; t.rain = w.rain; t.weatherCloud = w.cloud;
    raw::bridge::LightingPassTune(Bridge()->ActivePass(), t);
    static uint32_t logged = 0;
    if (logged++ % 240 == 0) Log("[Weather] surfaces: wetness %.3f, puddles %.3f, snow %.3f", w.wetness, w.puddles, w.snow);
}

} // namespace

void LightingApplyTuning()
{
    const LightingTuning& t = Cfg().light;
    if (Bridge()) raw::bridge::LightingPassTune(Bridge()->ActivePass(), t);
    if (Bridge()) {
        raw::bridge::RtTuning rt;
        rt.enabled = t.mode == 2 && t.rtShadows && TxMode_Get() == 1;
        rt.alpha = t.rtAlpha;
        rt.alphaRef = t.rtAlphaRef;
        rt.termDist = t.rtTermDist;
        rt.sky = t.rtSky;
        rt.skyLength = t.rtSkyLength;
        rt.reflect = t.rtReflect;
        rt.debug = t.rtDebug != 0;
        rt.debugView = t.rtDebug;
        rt.omm = t.rtOmm;
        rt.materials = t.rtMaterials;
        rt.matSide = uint32_t(t.rtMatSide);
        rt.cache = t.rtCache;
        rt.cacheCell = t.rtCacheCell;
        rt.skin = t.rtSkin;
        rt.skinCull = t.rtSkinCull; rt.skinRatio = t.rtSkinRatio; rt.skinMin = t.rtSkinMin;
        rt.incremental = t.rtIncremental;
        rt.camSteps = t.rtCamSteps;
        Bridge()->TuneRt(rt);
    }
    g_sceneGeometryActive = Cfg().enabled && t.rtShadows && TxMode_Get() == 1;
    SceneGeometry_SetAlpha(t.rtAlpha);
    SceneGeometry_SetMaterials(t.rtMaterials);
    SceneGeometry_SetSkin(t.rtSkin, t.rtSkinBones);
    SceneGeometry_SetActors(t.rtActors);
    SceneGeometry_SetSkinFilter(t.rtSkinFilter);
    GameComposite_Set({t.mode == 2 && t.ao, t.mode == 2 && t.volumetricFog && t.fogReplacesGame});
}

void LoadLightingConfig(const std::string& ini)
{
    LightingTuning& t = Cfg().light;
    auto i = [&](const char* k, int d) { return GetPrivateProfileIntA("Lighting", k, d, ini.c_str()); };
    auto f = [&](const char* k, float d) {
        char buf[64]{};
        GetPrivateProfileStringA("Lighting", k, "", buf, sizeof(buf), ini.c_str());
        return buf[0] ? std::strtof(buf, nullptr) : d;
    };
    t.mode = i("Mode", 1);
    t.softShadows = i("SoftShadows", 1) != 0; t.ggxSpecular = i("GgxSpecular", 1) != 0;
    t.volumetricFog = i("VolumetricFog", 1) != 0; t.contactShadows = i("ContactShadows", 1) != 0;
    t.rtShadows = i("RtShadows", 1) != 0;   // on raw::tx only (docs/RAYTRACING.md)
    t.rtAlpha = i("RtAlpha", 1) != 0;
    t.rtAlphaRef = f("RtAlphaRef", t.rtAlphaRef);
    t.rtTermDist = f("RtTermDist", t.rtTermDist);
    t.rtSky = i("RtSky", 1) != 0;
    t.rtSkyStrength = f("RtSkyStrength", t.rtSkyStrength);
    t.rtSkyLength = f("RtSkyLength", t.rtSkyLength);
    t.rtBounce = f("RtBounce", t.rtBounce);
    t.rtBounceInterior = f("RtBounceInterior", t.rtBounceInterior);
    t.rtReflect = i("RtReflect", 1) != 0;
    t.rtDebug = i("RtDebug", 0);
    t.rtOmm = i("RtOmm", 1) != 0;
    t.rtMaterials = i("RtMaterials", 1) != 0;
    t.rtMatSide = i("RtMatSide", t.rtMatSide);
    t.rtCache = i("RtCache", 1) != 0;
    t.rtCacheCell = f("RtCacheCell", t.rtCacheCell);
    t.rtSkin = i("RtSkin", 1) != 0;
    t.rtSkinCull = i("RtSkinCull", 1) != 0; t.rtSkinRatio = f("RtSkinRatio", t.rtSkinRatio); t.rtSkinMin = f("RtSkinMin", t.rtSkinMin);
    t.wetness = f("Wetness", t.wetness);
    t.snow = f("Snow", t.snow);
    // The game's weather drives Wetness, Puddles and Snow once the engine module reports it
    // (WeatherDrive.h); WeatherRain and WeatherSnow stand in for that report in tests.
    s_weatherDriven = i("WeatherDrive", 1) != 0;
    SceneLights_SetEngineLights(i("EngineLights", 1) != 0);   // raw_engine.dll's light list (SceneLights.cpp)
    WeatherDrive_SetTest(f("WeatherRain", -1.0f), f("WeatherSnow", -1.0f));
    WeatherTimes wt;
    wt.soakSeconds = f("WeatherSoakSeconds", wt.soakSeconds); wt.drySeconds = f("WeatherDrySeconds", wt.drySeconds);
    wt.puddleSeconds = f("WeatherPuddleSeconds", wt.puddleSeconds); wt.snowSeconds = f("WeatherSnowSeconds", wt.snowSeconds);
    wt.meltSeconds = f("WeatherMeltSeconds", wt.meltSeconds); wt.drainSeconds = f("WeatherDrainSeconds", wt.drainSeconds);
    WeatherDrive_SetTimes(wt);
    t.puddles = f("Puddles", t.puddles);
    t.rain = f("Rain", t.rain);
    t.clouds = i("Clouds", 1) != 0; t.cloudCover = f("CloudCover", t.cloudCover);
    t.cloudBase = f("CloudBase", t.cloudBase); t.cloudThickness = f("CloudThickness", t.cloudThickness);
    t.cloudShadow = f("CloudShadow", t.cloudShadow); t.cloudDensity = f("CloudDensity", t.cloudDensity);
    t.cloudWindX = f("CloudWindX", t.cloudWindX); t.cloudWindY = f("CloudWindY", t.cloudWindY);
    t.cloudHistory = f("CloudHistory", t.cloudHistory);
    {   // RAW's tone map and exposure (Tonemap.h); keys in [Lighting] so a live edit reaches them
        TonemapSettings tm;
        tm.enabled = i("Tonemap", 0) != 0; tm.mode = i("Exposure", tm.mode); tm.bias = f("ExposureBias", tm.bias);
        tm.minLum = f("ExposureMinLum", tm.minLum); tm.maxLum = f("ExposureMaxLum", tm.maxLum); tm.key = f("ExposureKey", tm.key);
        tm.curve = i("ToneCurve", tm.curve); tm.white = f("ToneWhite", tm.white); tm.view = i("ToneView", 0);
        Tonemap_Set(tm);
    }
    t.rtSkinBones = i("RtSkinBones", 10);
    t.rtActors = i("RtActors", 1) != 0;
    t.rtIncremental = i("RtIncremental", 1) != 0;
    t.rtSkinFilter = i("RtSkinFilter", 0);
    t.rtCamSteps = i("RtCamSteps", 0) != 0;
    t.ao = i("Ao", 1) != 0; t.fogReplacesGame = i("FogReplacesGame", 0) != 0; t.cascades = i("Cascades", 0) != 0; t.view = i("View", 0);
    // Cascades=0: the sun's shadow is the game's own filtered mask (match dE76 2.23 against 2.53 with
    // our cascade lookup, which also drew hard twig streaks on the ground).
    t.fogDensity = f("FogDensity", t.fogDensity); t.fogAnisotropy = f("FogAnisotropy", t.fogAnisotropy);
    t.fogLightScale = f("FogLightScale", t.fogLightScale); t.fogSunScale = f("FogSunScale", t.fogSunScale);
    t.fogMaxDistance = f("FogMaxDistance", t.fogMaxDistance); t.softRange = f("SoftRange", t.softRange);
    t.contactLength = f("ContactLength", t.contactLength); t.aoStrength = f("AoStrength", t.aoStrength);
    Cfg().lightCaptureAfter = i("CaptureAfter", 0);
    Cfg().lightHold = i("Hold", 45);
    s_shadowSide = static_cast<UINT>(i("ShadowMapSize", 4096));
    s_handoverFlush = i("HandoverFlush", 1) != 0;   // docs/LIGHTING.md: the indoor cost
    Cfg().lightDump = i("Dump", 0) != 0;
    Cfg().harness = i("Harness", 0) != 0;
    char text[MAX_PATH]{};
    GetPrivateProfileStringA("Lighting", "CaptureDir", "", text, MAX_PATH, ini.c_str());
    Cfg().lightCaptureDir = text;
    GetPrivateProfileStringA("Lighting", "Scene", "scene", text, MAX_PATH, ini.c_str());
    Cfg().lightScene = text;
    g_sceneLightsActive = Cfg().enabled;
    LightingMakeSteps();
    Log("[Lighting] config: mode %d, soft %d ggx %d fog %d contact %d ao %d cascades %d, capture after %d", t.mode, t.softShadows,
        t.ggxSpecular, t.volumetricFog, t.contactShadows, t.ao, t.cascades, Cfg().lightCaptureAfter);
}

bool LightingEnsure()
{
    raw::bridge::D3D12Bridge* b = Bridge();
    if (!b || Cfg().light.mode <= 0) return false;
    auto* pi = PG_GetProxyInterface();
    ID3D11Texture2D* bb = nullptr;
    if (!pi || !pi->device || !pi->context || !pi->swapChain || FAILED(pi->swapChain->GetBuffer(0, IID_PPV_ARGS(&bb))) || !bb) return false;
    D3D11_TEXTURE2D_DESC d{};
    bb->GetDesc(&d);
    bb->Release();
    if (b->Ready()) {
        if (b->Config().width == d.Width && b->Config().height == d.Height) return b->Config().lightingPass;
        // The swap chain was resized: every shared target is the old size, so the bridge is made again.
        Log("[Lighting] backbuffer now %ux%u (bridge %ux%u): making the bridge again", d.Width, d.Height, b->Config().width, b->Config().height);
        b->Shutdown();
    }
    if (s_failures >= 3 || !GBuffer_Textures().albedo) return false;
    LightingLogGeometry(pi->swapChain, d);
    raw::bridge::BridgeConfig c;
    c.width = d.Width; c.height = d.Height;
    c.colorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    c.normalFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    c.motionFormat = DXGI_FORMAT_R16G16_FLOAT;
    c.debugLayer = Cfg().debugLayer;
    c.passRetireMs = static_cast<DWORD>(Cfg().retireTimeoutMs);
    c.lightingPass = true;
    c.shadowMapSize = s_shadowSide;
    c.handoverFlush = s_handoverFlush;
    if (TxMode_Get() == 1) {
        // The game runs on raw::tx: the passes run on the layer's own device and queue.
        void* d12 = nullptr;
        void* q12 = nullptr;
        raw::tx::GetNative(pi->device, &d12, &q12);
        c.layer.device = static_cast<ID3D12Device*>(d12);
        c.layer.queue = static_cast<ID3D12CommandQueue*>(q12);
        c.layer.native = &raw::tx::NativeResource;
        c.layer.prepare = &raw::tx::PrepareForHost;
    }
    if (!b->Initialize(pi->device, pi->context, c, &Log)) { ++s_failures; return false; }
    LightingApplyTuning();
    return true;
}

bool LightingAtOpaque(ID3D11DeviceContext* real, ID3D11Resource* sceneHdr)
{
    raw::bridge::D3D12Bridge* b = Bridge();
    if (!b || !b->Ready() || !sceneHdr) return false;
    SceneLights_SetCamera(real);
    const GBufferTextures g = GBuffer_Textures();
    ID3D11Texture2D* mask = SceneLights_ShadowMask();
    b->CaptureSurface(g.albedo, g.specular, mask ? mask : g.albedo, g.emissive);
    if (ID3D11Texture2D* cascades = SceneLights_Cascades()) b->CaptureShadows(cascades);
    raw::bridge::SceneLighting l = SceneLights_Frame();
    if (!mask)
        for (uint32_t k = 0; k < l.lightCount; ++k) l.lights[k].shadowChannel = -1;
    b->SetLighting(l);
    b->SetRtDraws(SceneGeometry_Take());
    raw::bridge::PassConstants k = Cfg().k;
    return b->Execute(sceneHdr, sceneHdr, k);
}

void LightingOnPresent(bool worldFrame)
{
    if (GetAsyncKeyState(VK_F10) & 1) {
        Cfg().light.mode = (Cfg().light.mode + 1) % 3;
        LightingApplyTuning();
        Log("[Lighting] F10: mode %d", Cfg().light.mode);
    }
    HotReload_OnPresent();
    DriveWeather();
    Overlay_Draw();
    SceneGeometry_Drop();   // draws of a frame that never reached the bridge (it was off, or not yet made)
    LightingScheduleOnPresent(worldFrame);
}

} // namespace SB::Proxy::hook
