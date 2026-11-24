// Overlay: interactive 6-tab ENB-class in-game RAW DX12 Control Panel (toggled by End or Shift+Enter)
// and error banner, rendered via GDI into a 32-bit backbuffer-format texture before Present. Original work, MIT.
#include "Overlay.h"
#include "HotReload.h"
#include <cmath>
#include "BridgeHookState.h"
#include "ProxyAPI.h"
#include "ProxyLog.h"
#include "Tonemap.h"
#include "StreamlineHost.h"
#include "WeatherDrive.h"

#include <d3d11.h>
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <vector>

namespace SB::Proxy
{
namespace
{
std::mutex g_m;
std::string g_text;
bool g_dirty = false;
ID3D11Texture2D* g_tex = nullptr;
UINT g_w = 0, g_h = 0;
DXGI_FORMAT g_fmt = DXGI_FORMAT_UNKNOWN;

// 7-tab, 76-control interactive in-game RAW DX12 Control Panel + live FPS, 46 Elder 3D LUTs, 8 stackable overlays & User Preset Save/Load.
bool g_menuOpen = false;
int g_tab = 0;
int g_sel = 0;
int g_hwTier = 4;      // 0=Low, 1=Medium, 2=High, 3=Ultra, 4=Overdrive
int g_presetSlot = 1;  // User preset slot 1..5 (raw_presets/slot_N.ini)
float g_menuSnow = -1.0f, g_menuRain = -1.0f;
bool g_prevToggle = false, g_prevTab = false, g_prevPgUp = false, g_prevPgDn = false, g_prevSave = false, g_prevLoad = false, g_prevUp = false, g_prevDown = false, g_prevLeft = false, g_prevRight = false;
HWND g_hookHwnd = nullptr;
WNDPROC g_origWndProc = nullptr;

// Subclass Skyrim's game window so opening the RAW overlay freezes camera mouse movement and prevents
// Tab / Arrows / S / L / PgUp / PgDn from leaking into Skyrim's own Inventory/Character/Wait menus.
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_menuOpen) {
        switch (msg) {
        case WM_INPUT:
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        case WM_MOUSEWHEEL:  case WM_MOUSEHWHEEL:
            return 0;
        case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_CHAR:
            if (wp == VK_TAB || wp == VK_UP || wp == VK_DOWN || wp == VK_LEFT || wp == VK_RIGHT ||
                wp == VK_PRIOR || wp == VK_NEXT || wp == 'S' || wp == 's' || wp == 'L' || wp == 'l' ||
                wp == VK_RETURN || wp == VK_END)
                return 0;
            break;
        }
    }
    return g_origWndProc ? CallWindowProcW(g_origWndProc, hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

void EnsureWndProcHook()
{
    if (g_hookHwnd) return;
    HWND fg = GetForegroundWindow();
    if (!fg) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid != GetCurrentProcessId()) return;
    g_origWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(fg, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(OverlayWndProc)));
    if (g_origWndProc) g_hookHwnd = fg;
}
LARGE_INTEGER g_qpcFreq{}, g_qpcLast{};
float g_emaMs = 11.0f;
uint32_t g_savedFrames = 0;
std::string g_statusTag;

std::string ExeDir()
{
    char exe[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string p(exe);
    const size_t sl = p.rfind('\\');
    return sl == std::string::npos ? std::string() : p.substr(0, sl + 1);
}

void SavePresetFile(const std::string& path, const char* tag)
{
    const auto& l = hook::Cfg().light;
    const auto tm = Tonemap_Get();
    bool ok = true;
    auto wF = [&](const char* k, float v) { char b[32]; std::snprintf(b, sizeof b, "%.4g", v); if (!WritePrivateProfileStringA("Lighting", k, b, path.c_str())) ok = false; };
    auto wI = [&](const char* k, int v) { char b[32]; std::snprintf(b, sizeof b, "%d", v); if (!WritePrivateProfileStringA("Lighting", k, b, path.c_str())) ok = false; };
    wI("Mode", l.mode); wI("Quality", g_hwTier);
    wI("SoftShadows", l.softShadows ? 1 : 0); wI("ContactShadows", l.contactShadows ? 1 : 0);
    wI("Season", l.season); wI("View", l.view);
    wI("RtShadows", l.rtShadows ? 1 : 0); wI("RtSky", l.rtSky ? 1 : 0); wI("RtReflect", l.rtReflect ? 1 : 0);
    wI("RtOmm", l.rtOmm ? 1 : 0); wI("RtFar", l.rtFar ? 1 : 0); wI("RtSkin", l.rtSkin ? 1 : 0); wI("RtRest", l.rtRest ? 1 : 0);
    wF("RtBounce", l.rtBounce); wF("RtBounceInterior", l.rtBounceInterior); wF("RtSkyStrength", l.rtSkyStrength); wF("RtSkyLength", l.rtSkyLength);
    wF("RtTermDist", l.rtTermDist);
    wI("Clouds", l.clouds ? 1 : 0); wF("CloudCover", l.cloudCover); wF("CloudDensity", l.cloudDensity); wF("CloudShadow", l.cloudShadow);
    wI("VolumetricFog", l.volumetricFog ? 1 : 0); wI("FogReplacesGame", l.fogReplacesGame ? 1 : 0); wF("FogSunScale", l.fogSunScale);
    wI("Water", l.water ? 1 : 0); wF("WaterReflect", l.waterReflect); wF("WaterCaustics", l.waterCaustics); wF("WaterRefract", l.waterRefract);
    wF("Sss", l.sss); wF("AoStrength", l.aoStrength);
    wI("Tonemap", tm.enabled ? 1 : 0); wI("ToneCurve", tm.curve); wF("ExposureBias", tm.bias); wF("ExposureKey", tm.key); wF("ExposureAdapt", tm.adapt);
    wF("ToneX1", tm.x[1]); wF("ToneX2", tm.x[2]); wF("ToneX3", tm.x[3]); wF("ToneX4", tm.x[4]); wF("ToneX5", tm.x[5]);
    wI("ToneView", tm.view); wI("LutIndex", tm.lutIndex); wF("LutStrength", tm.lutStrength); wI("OverlayMask", static_cast<int>(tm.overlayMask));
    wF("BloomScale", tm.bloomScale); wF("SharpenStr", tm.sharpenStr); wF("GrainStr", tm.grainStr); wF("VignetteStr", tm.vignetteStr);
    wF("CaStr", tm.caStr); wF("RadialBlurStr", tm.radialBlurStr); wF("HeadbobStr", tm.headbobStr); wF("LensStr", tm.lensStr);
    wF("ColorTempK", tm.colorTempK); wF("OklabSat", tm.oklabSat); wF("ShadowLift", tm.shadowLift); wF("HighlightKnee", tm.highlightKnee);
    wI("Rawlss5", l.rawlss5 ? 1 : 0);
    wI("Dof", l.dof ? 1 : 0); wF("DofMaxDist", l.dofMaxDist); wF("DofRadius", l.dofRadius); wF("DofOrbBoost", l.dofOrbBoost);
    wI("DofSensor", l.dofSensor); wF("DofFocalMm", l.dofFocalMm); wF("DofFStop", l.dofFStop); wI("DofBokehShape", l.dofBokehShape);
    wF("DofNearScale", l.dofNearScale); wF("DofFarScale", l.dofFarScale); wF("DofNearBleed", l.dofNearBleed); wF("DofFocusSpeed", l.dofFocusSpeed);
    wI("WeatherLens", l.weatherLens ? 1 : 0);
    wI("DlssMode", raw::bridge::StreamlineHost::Get().GetMode());
    wI("Reflex", raw::bridge::StreamlineHost::Get().GetReflex() ? 1 : 0);
    wI("DeepDvc", raw::bridge::StreamlineHost::Get().GetDeepDvc() ? 1 : 0);
    // Flush Windows INI write cache immediately and sync HotReload's timestamp so HotReload_OnPresent NEVER reloads over our save!
    WritePrivateProfileStringA(nullptr, nullptr, nullptr, path.c_str());
    HotReload_SyncIniTime();
    g_statusTag = ok ? tag : " [SAVE FAILED]";
    g_savedFrames = 180;
}

void LoadPresetSlot(int slot)
{
    const std::string path = ExeDir() + "raw_presets\\slot_" + std::to_string(slot) + ".ini";
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        g_statusTag = " [SLOT " + std::to_string(slot) + " EMPTY]";
        g_savedFrames = 150;
        return;
    }
    auto rI = [&](const char* k, int d) { return static_cast<int>(GetPrivateProfileIntA("Lighting", k, d, path.c_str())); };
    auto rF = [&](const char* k, float d) { char b[64]{}; GetPrivateProfileStringA("Lighting", k, "", b, sizeof b, path.c_str()); return b[0] ? std::strtof(b, nullptr) : d; };
    auto& l = hook::Cfg().light;
    l.mode = rI("Mode", l.mode);
    g_hwTier = std::clamp(rI("Quality", g_hwTier), 0, 4);
    l.softShadows = rI("SoftShadows", l.softShadows ? 1 : 0) != 0;
    l.contactShadows = rI("ContactShadows", l.contactShadows ? 1 : 0) != 0;
    l.season = rI("Season", l.season); l.view = rI("View", l.view);
    l.rtShadows = rI("RtShadows", l.rtShadows) != 0; l.rtSky = rI("RtSky", l.rtSky) != 0; l.rtReflect = rI("RtReflect", l.rtReflect) != 0;
    l.rtOmm = rI("RtOmm", l.rtOmm) != 0; l.rtFar = rI("RtFar", l.rtFar) != 0; l.rtSkin = rI("RtSkin", l.rtSkin) != 0; l.rtRest = rI("RtRest", l.rtRest) != 0;
    l.rtBounce = rF("RtBounce", l.rtBounce); l.rtBounceInterior = rF("RtBounceInterior", l.rtBounceInterior);
    l.rtSkyStrength = rF("RtSkyStrength", l.rtSkyStrength); l.rtSkyLength = rF("RtSkyLength", l.rtSkyLength);
    l.rtTermDist = rF("RtTermDist", l.rtTermDist);
    l.clouds = rI("Clouds", l.clouds) != 0; l.cloudCover = rF("CloudCover", l.cloudCover); l.cloudDensity = rF("CloudDensity", l.cloudDensity); l.cloudShadow = rF("CloudShadow", l.cloudShadow);
    l.volumetricFog = rI("VolumetricFog", l.volumetricFog) != 0; l.fogReplacesGame = rI("FogReplacesGame", l.fogReplacesGame) != 0; l.fogSunScale = rF("FogSunScale", l.fogSunScale);
    l.water = rI("Water", l.water) != 0; l.waterReflect = rF("WaterReflect", l.waterReflect); l.waterCaustics = rF("WaterCaustics", l.waterCaustics); l.waterRefract = rF("WaterRefract", l.waterRefract);
    l.sss = rF("Sss", l.sss); l.aoStrength = rF("AoStrength", l.aoStrength);
    auto tm = Tonemap_Get();
    tm.enabled = rI("Tonemap", tm.enabled) != 0; tm.curve = rI("ToneCurve", tm.curve); tm.bias = rF("ExposureBias", tm.bias); tm.key = rF("ExposureKey", tm.key); tm.adapt = rF("ExposureAdapt", tm.adapt);
    tm.x[1] = rF("ToneX1", tm.x[1]); tm.x[2] = rF("ToneX2", tm.x[2]); tm.x[3] = rF("ToneX3", tm.x[3]); tm.x[4] = rF("ToneX4", tm.x[4]); tm.x[5] = rF("ToneX5", tm.x[5]);
    tm.view = rI("ToneView", tm.view); tm.lutIndex = rI("LutIndex", tm.lutIndex); tm.lutStrength = rF("LutStrength", tm.lutStrength);
    tm.overlayMask = static_cast<uint32_t>(rI("OverlayMask", static_cast<int>(tm.overlayMask)));
    tm.bloomScale = rF("BloomScale", tm.bloomScale); tm.sharpenStr = rF("SharpenStr", tm.sharpenStr);
    tm.grainStr = rF("GrainStr", tm.grainStr); tm.vignetteStr = rF("VignetteStr", tm.vignetteStr);
    tm.caStr = rF("CaStr", tm.caStr); tm.radialBlurStr = rF("RadialBlurStr", tm.radialBlurStr);
    tm.headbobStr = rF("HeadbobStr", tm.headbobStr); tm.lensStr = rF("LensStr", tm.lensStr);
    tm.colorTempK = rF("ColorTempK", tm.colorTempK); tm.oklabSat = rF("OklabSat", tm.oklabSat);
    tm.shadowLift = rF("ShadowLift", tm.shadowLift); tm.highlightKnee = rF("HighlightKnee", tm.highlightKnee);
    l.rawlss5 = rI("Rawlss5", l.rawlss5 ? 1 : 0) != 0;
    l.dof = rI("Dof", l.dof ? 1 : 0) != 0; l.dofMaxDist = rF("DofMaxDist", l.dofMaxDist);
    l.dofRadius = rF("DofRadius", l.dofRadius); l.dofOrbBoost = rF("DofOrbBoost", l.dofOrbBoost);
    l.dofSensor = rI("DofSensor", l.dofSensor); l.dofFocalMm = rF("DofFocalMm", l.dofFocalMm);
    l.dofFStop = rF("DofFStop", l.dofFStop); l.dofBokehShape = rI("DofBokehShape", l.dofBokehShape);
    l.dofNearScale = rF("DofNearScale", l.dofNearScale); l.dofFarScale = rF("DofFarScale", l.dofFarScale);
    l.dofNearBleed = rF("DofNearBleed", l.dofNearBleed); l.dofFocusSpeed = rF("DofFocusSpeed", l.dofFocusSpeed);
    l.weatherLens = rI("WeatherLens", l.weatherLens ? 1 : 0) != 0;
    Tonemap_Set(tm);
    raw::bridge::StreamlineHost::Get().SetMode(rI("DlssMode", raw::bridge::StreamlineHost::Get().GetMode()));
    raw::bridge::StreamlineHost::Get().SetReflex(rI("Reflex", raw::bridge::StreamlineHost::Get().GetReflex() ? 1 : 0) != 0);
    raw::bridge::StreamlineHost::Get().SetDeepDvc(rI("DeepDvc", raw::bridge::StreamlineHost::Get().GetDeepDvc() ? 1 : 0) != 0);
    hook::LightingApplyTuning();
    g_statusTag = " [LOADED SLOT " + std::to_string(slot) + "]";
    g_savedFrames = 180;
}

void ApplyHardwareTier(int tier)
{
    auto& l = hook::Cfg().light;
    if (tier == 0) { l.rtShadows = false; l.rtSky = false; l.rtReflect = false; l.clouds = false; l.volumetricFog = false; l.water = false; l.sss = 0.0f; }
    else if (tier == 1) { l.rtShadows = true; l.rtSky = false; l.rtReflect = false; l.clouds = true; l.volumetricFog = true; l.water = true; l.sss = 0.5f; }
    else if (tier == 2) { l.rtShadows = true; l.rtSky = true; l.rtReflect = false; l.clouds = true; l.volumetricFog = true; l.water = true; l.sss = 0.8f; }
    else if (tier == 3) { l.rtShadows = true; l.rtSky = true; l.rtReflect = true; l.rtFar = false; l.clouds = true; l.volumetricFog = true; l.water = true; l.sss = 1.0f; }
    else { l.rtShadows = true; l.rtSky = true; l.rtReflect = true; l.rtFar = true; l.rtSkin = true; l.rtOmm = true; l.clouds = true; l.volumetricFog = true; l.water = true; l.sss = 1.0f; }
}

void SaveMenuToIni()
{
    const std::string dir = ExeDir();
    if (dir.empty()) return;
    if (g_tab == 6) {
        CreateDirectoryA((dir + "raw_presets").c_str(), nullptr);
        SavePresetFile(dir + "raw_presets\\slot_" + std::to_string(g_presetSlot) + ".ini",
                       (" [SAVED SLOT " + std::to_string(g_presetSlot) + "]").c_str());
    } else {
        SavePresetFile(dir + "d3d11_proxy.ini", " [SAVED INI]");
    }
}

bool KeyEdge(bool now, bool& prev) { const bool f = now && !prev; prev = now; return f; }

void ExportCameraRecipe()
{
    const std::string dir = ExeDir();
    if (dir.empty()) return;
    const std::string capDir = dir + "raw_captures";
    CreateDirectoryA(capDir.c_str(), nullptr);
    SYSTEMTIME st{}; GetLocalTime(&st);
    char fname[128];
    std::snprintf(fname, sizeof fname, "\\RAW_%04u%02u%02u_%02u%02u%02u_camera.ini",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    SavePresetFile(capDir + fname, " [CAMERA EXPORTED]");
}

int TabRowCount(int tab)
{
    static const int kCounts[8] = {14, 13, 7, 8, 12, 11, 20, 26};
    return kCounts[std::clamp(tab, 0, 7)];
}

std::string Fmt(const char* f, float v) { char b[64]; std::snprintf(b, sizeof b, f, v); return b; }
std::string FmtI(const char* f, int v) { char b[64]; std::snprintf(b, sizeof b, f, v); return b; }
std::string OnOff(bool b) { return b ? "ON" : "OFF"; }

std::string BuildMenuText()
{
    const auto& l = hook::Cfg().light;
    const auto tm = Tonemap_Get();
    static const char* kTabs[8] = {"1:RT", "2:Sky/Fog", "3:Weather", "4:Water", "5:Skin/AO", "6:Tonemap", "7:LUTs/OV", "8:Camera/DoF"};
    char hdr[224];
    std::snprintf(hdr, sizeof hdr, "=== RAW DX12 STUDIO | %.1f FPS (%.1f ms) | [End:Close | PgUp/PgDn:Tab | Arrows:Tune | S:Save | L:Load | P:Export]%s ===\n",
                  g_emaMs > 0.1f ? 1000.0f / g_emaMs : 0.0f, g_emaMs, g_savedFrames ? g_statusTag.c_str() : "");
    std::string out = hdr;
    const bool bridgeOk = hook::Bridge() && hook::Bridge()->Ready();
    if (!bridgeOk) {
        out += "!! STATUS: DX12 BRIDGE INACTIVE (only Tonemap runs) - check d3d11_proxy.log & ensure Skyrim TAA=1, MSAA=0 !!\n";
    } else {
        char st[128];
        std::snprintf(st, sizeof st, "STATUS: DX12 BRIDGE ACTIVE | Mode=%d | RT=%s | RAWLSS=%s | Tier=%d\n",
                      l.mode, l.rtShadows ? "ON" : "OFF", l.rawlss5 ? "ON" : "OFF", g_hwTier);
        out += st;
    }
    for (int i = 0; i < 8; ++i) {
        out += (i == g_tab ? "[" : " ");
        out += kTabs[i];
        out += (i == g_tab ? "] " : "  ");
    }
    out += "\n";
    auto row = [&](int idx, const char* name, const std::string& val) {
        char line[160];
        std::snprintf(line, sizeof line, "%s %-22s : %s\n", g_sel == idx ? ">>" : "  ", name, val.c_str());
        out += line;
    };
    if (g_tab == 0) {
        row(0, "RT Shadows", OnOff(l.rtShadows));
        row(1, "RT Sky / RTAO", OnOff(l.rtSky));
        row(2, "RT Reflections", OnOff(l.rtReflect));
        row(3, "RT Opacity Micromaps", OnOff(l.rtOmm));
        row(4, "RT Distant Far", OnOff(l.rtFar));
        row(5, "RT Skinned Actors", OnOff(l.rtSkin));
        row(6, "RT ReSTIR Lights", OnOff(l.rtRest));
        row(7, "RT GI Bounce (Ext)", Fmt("%.2f", l.rtBounce));
        row(8, "RT GI Bounce (Int)", Fmt("%.2f", l.rtBounceInterior));
        row(9, "RT Sky Strength", Fmt("%.2f", l.rtSkyStrength));
        row(10, "RT Sky Ray Length", Fmt("%.0f", l.rtSkyLength));
        row(11, "RT Terminator Bias", Fmt("%.2f", l.rtTermDist));
        row(12, "RAWLSS 5.5 3D-Guided", OnOff(l.rawlss5));
        static const char* kDbgNames[] = {"0:Final", "1:Normals", "2:SunShadow", "12:MatClass", "20:4Quad", "21:Heatmap", "22:BloomAudit", "23:WorldField", "24:Continuity"};
        static const int kDbgVals[] = {0, 1, 2, 12, 20, 21, 22, 23, 24};
        int dbgIdx = 0;
        for (int di = 0; di < 9; ++di) if (kDbgVals[di] == l.view) { dbgIdx = di; break; }
        row(13, "Pipeline Debug View", kDbgNames[dbgIdx]);
    } else if (g_tab == 1) {
        row(0, "Volumetric Clouds", OnOff(l.clouds));
        row(1, "Cloud Cover", Fmt("%.2f", l.cloudCover));
        row(2, "Cloud Density", Fmt("%.2f", l.cloudDensity));
        row(3, "Cloud Floor Height", Fmt("%.0f", l.cloudBase));
        row(4, "Cloud Thickness", Fmt("%.0f", l.cloudThickness));
        row(5, "Cloud Shadow Str", Fmt("%.2f", l.cloudShadow));
        row(6, "Cloud Wind X", Fmt("%.0f", l.cloudWindX));
        row(7, "Cloud Wind Y", Fmt("%.0f", l.cloudWindY));
        row(8, "Volumetric Fog", OnOff(l.volumetricFog));
        row(9, "Fog Replaces Vanilla", OnOff(l.fogReplacesGame));
        row(10, "Fog Density", Fmt("%.5f", l.fogDensity));
        row(11, "Fog Anisotropy (g)", Fmt("%.2f", l.fogAnisotropy));
        row(12, "Fog Sun Shaft Scale", Fmt("%.2f", l.fogSunScale));
    } else if (g_tab == 2) {
        static const char* kSeasons[5] = {"Auto", "Spring", "Summer", "Autumn", "Winter"};
        const int si = std::clamp(l.season, 0, 4);
        row(0, "Season", FmtI("%d (", si) + kSeasons[si] + ")");
        row(1, "Force Snow", g_menuSnow < 0.0f ? "Auto" : Fmt("%.0f%%", g_menuSnow * 100.0f));
        row(2, "Force Rain", g_menuRain < 0.0f ? "Auto" : Fmt("%.0f%%", g_menuRain * 100.0f));
        row(3, "Surface Wetness", Fmt("%.2f", l.wetness));
        row(4, "Surface Puddles", Fmt("%.2f", l.puddles));
        row(5, "Surface Snow Cover", Fmt("%.2f", l.snow));
        row(6, "Active Rain Ripples", Fmt("%.2f", l.rain));
    } else if (g_tab == 3) {
        row(0, "RT Water Enabled", OnOff(l.water));
        row(1, "Water Reflections", Fmt("%.2f", l.waterReflect));
        row(2, "Water Caustics", Fmt("%.2f", l.waterCaustics));
        row(3, "Water Absorption", Fmt("%.4f", l.waterAbsorb));
        row(4, "Water Sun Glint", Fmt("%.2f", l.waterGlint));
        row(5, "Caustic Cell Size", Fmt("%.0f", l.waterCausticCell));
        row(6, "Water Refraction", Fmt("%.2f", l.waterRefract));
        row(7, "Water Debug View", FmtI("%d", l.waterView));
    } else if (g_tab == 4) {
        row(0, "Skin SSS Strength", Fmt("%.2f", l.sss));
        row(1, "Skin SSS Width", Fmt("%.2f", l.sssWidth));
        row(2, "Ambient Occlusion", OnOff(l.ao));
        row(3, "AO Strength", Fmt("%.2f", l.aoStrength));
        row(4, "Contact Shadows", OnOff(l.contactShadows));
        row(5, "Contact Shadow Len", Fmt("%.1f", l.contactLength));
        row(6, "Soft Point Shadows", OnOff(l.softShadows));
        row(7, "Soft Shadow Range", Fmt("%.0f", l.softRange));
        row(8, "GGX Specular", OnOff(l.ggxSpecular));
        row(9, "RAW Shadow Cascades", OnOff(l.cascades));
        row(10, "Distant LOD Lighting", OnOff(l.distantLod));
        row(11, "EnvMap / CharLight", OnOff(l.envMap) + " / " + OnOff(l.charLight));
    } else if (g_tab == 5) {
        static const char* kLuts[14] = {"Neutral", "Nordic Film", "Kodak 2383", "Cinema", "Vision3 500T", "Fuji Velvia 50", "Tarkovsky", "LomoChrome", "Oil Impasto", "Ink Hatch", "Pixel Bayer", "Watercolor", "Sony PVM CRT", "NTSC VHS Tape"};
        const int li = std::clamp(int(tm.x[2] + 0.5f), 0, 13);
        row(0, "RAW Tonemap + FXAA", OnOff(tm.enabled));
        row(1, "Built-In Grade", FmtI("%d (", li) + kLuts[li] + ")");
        row(2, "Display Output", tm.view >= 3 ? "HDR10 PQ Rec.2020" : "SDR Rec.709");
        row(3, "Exposure Bias (EV)", Fmt("%+.2f", tm.bias));
        row(4, "Exposure Key", Fmt("%.2f", tm.key));
        row(5, "Adaptation Speed", Fmt("%.2f", tm.adapt));
        row(6, "White Point (X1)", Fmt("%.2f", tm.x[1]));
        row(7, "Highlight Contrast", Fmt("%.2f", tm.x[3]));
        row(8, "Shadow Lift (X4)", Fmt("%.2f", tm.x[4]));
        row(9, "Interior Factor (X5)", Fmt("%.2f", tm.x[5]));
        row(10, "Debug View (0..27)", FmtI("%d", l.view));
    } else if (g_tab == 6) {
        static const char* kTiers[5] = {"Low (Raster)", "Medium (RT Sun)", "High (RT GI)", "Ultra (Full RT)", "Overdrive (Max)"};
        static const char* kCurves[9] = {"0:Game", "1:ACES Fitted", "2:ReinhardW", "3:RAW Hybrid", "4:AgX WideGamut", "5:GranTurismo", "6:Lottes", "7:Hable", "8:ACES 2.0 CAM"};
        static const char* kDlss[6] = {"0:Off (FXAA+CAS)", "1:DLAA Transformer K", "2:DLSS RayRecon F", "3:DLSS Quality K", "4:DLSS Balanced K", "5:DLSS Perf M"};
        const uint32_t lutTotal = Tonemap_LutCount();
        row(0, "Hardware Tier Preset", kTiers[std::clamp(g_hwTier, 0, 4)]);
        row(1, "NVIDIA Streamline", kDlss[std::clamp(raw::bridge::StreamlineHost::Get().GetMode(), 0, 5)]);
        row(2, "Color Space / Curve", kCurves[std::clamp(tm.curve, 0, 8)]);
        row(3, "Elder 3D PNG LUT", FmtI("%d/", tm.lutIndex) + FmtI("%d ", int(lutTotal)) + Tonemap_LutName(tm.lutIndex));
        row(4, "3D LUT Blend Strength", Fmt("%.0f%%", tm.lutStrength * 100.0f));
        row(5, "OV1: Film Emulsion", OnOff((tm.overlayMask & 1u) != 0u));
        row(6, "OV2: Halation+Anamorp", OnOff((tm.overlayMask & 2u) != 0u));
        row(7, "OV3: Elder Lens Ghost", OnOff((tm.overlayMask & 4u) != 0u));
        row(8, "OV4: Soft Diffusion", OnOff((tm.overlayMask & 8u) != 0u));
        row(9, "OV5: Bleach Bypass", OnOff((tm.overlayMask & 16u) != 0u));
        row(10, "OV6: Optical Vignette", OnOff((tm.overlayMask & 32u) != 0u));
        row(11, "OV7: 35mm Film Grain", OnOff((tm.overlayMask & 64u) != 0u));
        row(12, "OV8: Cinema Borders", OnOff((tm.overlayMask & 128u) != 0u));
        row(13, "OV9: Sharpen+Smooth", OnOff((tm.overlayMask & 256u) != 0u));
        row(14, "OV10: Radial SpeedBlur", OnOff((tm.overlayMask & 512u) != 0u));
        row(15, "OV11: Chromatic Aberr", OnOff((tm.overlayMask & 1024u) != 0u));
        row(16, "OV12: Run Headbob/Sway", OnOff((tm.overlayMask & 2048u) != 0u));
        row(17, "NVIDIA Reflex Latency", OnOff(raw::bridge::StreamlineHost::Get().GetReflex()));
        row(18, "RAW GPU Neural Vibrance", OnOff(raw::bridge::StreamlineHost::Get().GetDeepDvc()));
        row(19, "User Preset Slot", FmtI("Slot %d  [S=Save | L=Load]", g_presetSlot));
    } else {
        static const char* kSensors[4] = {"APS-C (24mm)", "35mm Full-Frame", "Medium Format 645 (44mm)", "Medium Format 6x7 (70mm)"};
        static const char* kShapes[7]  = {"5-Blade Pentagon", "6-Blade Hexagon", "7-Blade Cinema", "9-Blade Zeiss", "Circular Ideal", "Anamorphic 2x", "Swirly Petzval"};
        row(0, "Bokeh DoF Enabled", OnOff(l.dof));
        row(1, "Camera Sensor Format", kSensors[std::clamp(l.dofSensor, 0, 3)]);
        row(2, "Lens Focal Length", Fmt("%.0f mm", l.dofFocalMm));
        row(3, "Aperture f-Stop", Fmt("f/%.2f", l.dofFStop));
        row(4, "Bokeh Blade Shape", kShapes[std::clamp(l.dofBokehShape, 0, 6)]);
        row(5, "Near-Plane Blur", Fmt("%.2fx", l.dofNearScale));
        row(6, "Far-Plane Blur", Fmt("%.2fx", l.dofFarScale));
        row(7, "Near Foreground Bleed", Fmt("%.2fx", l.dofNearBleed));
        row(8, "Bokeh Max Radius", Fmt("%.1f px", l.dofRadius));
        row(9, "Bokeh Specular Orbs", Fmt("%.2fx", l.dofOrbBoost));
        row(10, "Focus Pull Speed", Fmt("%.2f", l.dofFocusSpeed));
        row(11, "DoF Master Gain", Fmt("%.0f u", l.dofMaxDist));
        row(12, "Rain/Frost Lens FX", OnOff(l.weatherLens));
        row(13, "Spectral Bloom Scale", Fmt("%.2fx", tm.bloomScale));
        row(14, "RCAS Sharpen Strength", Fmt("%.2fx", tm.sharpenStr));
        row(15, "35mm Grain Strength", Fmt("%.2fx", tm.grainStr));
        row(16, "Optical Vignette Str", Fmt("%.2fx", tm.vignetteStr));
        row(17, "Chromatic Aberr Str", Fmt("%.2fx", tm.caStr));
        row(18, "Radial SpeedBlur Str", Fmt("%.2fx", tm.radialBlurStr));
        row(19, "Run Headbob Strength", Fmt("%.2fx", tm.headbobStr));
        row(20, "Lens & Halation Str", Fmt("%.2fx", tm.lensStr));
        row(21, "White Balance (K)", Fmt("%.0f K", tm.colorTempK));
        row(22, "Oklab Saturation", Fmt("%.2fx", tm.oklabSat));
        row(23, "Oklab Shadow Lift", Fmt("%+.3f", tm.shadowLift));
        row(24, "HDR Highlight Knee", Fmt("%.2f", tm.highlightKnee));
        static const char* kCamModes[6] = {"0:Cinema Prime", "1:Tilt-Shift Miniature", "2:ARPG Isometric Diorama", "3:Anamorphic 2.39:1", "4:Petzval Portrait Swirl", "5:Vertical Tilt-Rise"};
        row(25, "Camera Perspective", kCamModes[std::clamp(l.cameraMode, 0, 5)]);
    }
    out += "Web Studio, Presets & Live HLSL Editor: http://127.0.0.1:8844";
    return out;
}

bool PollMenu(std::string& outText)
{
    if (!g_qpcFreq.QuadPart) { QueryPerformanceFrequency(&g_qpcFreq); QueryPerformanceCounter(&g_qpcLast); }
    else {
        LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
        const float ms = float(double(now.QuadPart - g_qpcLast.QuadPart) * 1000.0 / double(g_qpcFreq.QuadPart));
        g_qpcLast = now;
        if (ms > 0.5f && ms < 250.0f) g_emaMs = g_emaMs * 0.90f + ms * 0.10f;
    }
    if (g_savedFrames > 0) --g_savedFrames;
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool enter = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    const bool endKey = (GetAsyncKeyState(VK_END) & 0x8000) != 0;
    bool changed = false;
    EnsureWndProcHook();
    // Single clean toggle key (End): removes redundant Shift+Enter menu collision.
    if (KeyEdge(endKey, g_prevToggle)) { g_menuOpen = !g_menuOpen; changed = true; }
    if (!g_menuOpen) return false;
    if (KeyEdge((GetAsyncKeyState('S') & 0x8000) != 0, g_prevSave)) { SaveMenuToIni(); changed = true; }
    if (KeyEdge((GetAsyncKeyState('L') & 0x8000) != 0, g_prevLoad)) { LoadPresetSlot(g_presetSlot); changed = true; }
    static bool g_prevExport = false;
    if (KeyEdge((GetAsyncKeyState('P') & 0x8000) != 0, g_prevExport)) { ExportCameraRecipe(); changed = true; }
    const bool nextTab = KeyEdge((GetAsyncKeyState(VK_TAB) & 0x8000) != 0, g_prevTab) && !shift;
    const bool prevTab = KeyEdge((GetAsyncKeyState(VK_PRIOR) & 0x8000) != 0 || (GetAsyncKeyState(VK_OEM_4) & 0x8000) != 0, g_prevPgUp);
    const bool nextTab2 = KeyEdge((GetAsyncKeyState(VK_NEXT) & 0x8000) != 0 || (GetAsyncKeyState(VK_OEM_6) & 0x8000) != 0, g_prevPgDn);
    if (nextTab || nextTab2) {
        g_tab = (g_tab + 1) % 8;
        g_sel = std::min(g_sel, TabRowCount(g_tab) - 1);
        changed = true;
    } else if (prevTab) {
        g_tab = (g_tab + 7) % 8;
        g_sel = std::min(g_sel, TabRowCount(g_tab) - 1);
        changed = true;
    }
    const int rows = TabRowCount(g_tab);
    if (KeyEdge((GetAsyncKeyState(VK_UP) & 0x8000) != 0, g_prevUp)) { g_sel = (g_sel + rows - 1) % rows; changed = true; }
    if (KeyEdge((GetAsyncKeyState(VK_DOWN) & 0x8000) != 0, g_prevDown)) { g_sel = (g_sel + 1) % rows; changed = true; }
    int dir = 0;
    if (KeyEdge((GetAsyncKeyState(VK_LEFT) & 0x8000) != 0, g_prevLeft)) dir = -1;
    if (KeyEdge((GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0, g_prevRight)) dir = 1;
    if (dir != 0) {
        const float fine = shift ? 0.25f : 1.0f;
        const float step = float(dir) * fine;
        auto& l = hook::Cfg().light;
        auto tm = Tonemap_Get();
        bool tmDirty = false;
        if (g_tab == 0) {
            switch (g_sel) {
            case 0: l.rtShadows = !l.rtShadows; break;
            case 1: l.rtSky = !l.rtSky; break;
            case 2: l.rtReflect = !l.rtReflect; break;
            case 3: l.rtOmm = !l.rtOmm; break;
            case 4: l.rtFar = !l.rtFar; break;
            case 5: l.rtSkin = !l.rtSkin; break;
            case 6: l.rtRest = !l.rtRest; break;
            case 7: l.rtBounce = std::clamp(l.rtBounce + step * 0.10f, 0.0f, 3.0f); break;
            case 8: l.rtBounceInterior = std::clamp(l.rtBounceInterior + step * 0.10f, 0.0f, 3.0f); break;
            case 9: l.rtSkyStrength = std::clamp(l.rtSkyStrength + step * 0.05f, 0.0f, 2.0f); break;
            case 10: l.rtSkyLength = std::clamp(l.rtSkyLength + step * 150.0f, 100.0f, 4000.0f); break;
            case 11: l.rtTermDist = std::clamp(l.rtTermDist + step * 0.50f, 0.0f, 12.0f); break;
            case 12: l.rawlss5 = !l.rawlss5; break;
            case 13: {
                static const int kDbgVals[9] = {0, 1, 2, 12, 20, 21, 22, 23, 24};
                int di = 0;
                for (int k = 0; k < 9; ++k) if (kDbgVals[k] == l.view) { di = k; break; }
                l.view = kDbgVals[(di + 9 + dir) % 9];
                break;
            }
            }
        } else if (g_tab == 1) {
            switch (g_sel) {
            case 0: l.clouds = !l.clouds; break;
            case 1: l.cloudCover = std::clamp(l.cloudCover + step * 0.05f, 0.0f, 1.0f); break;
            case 2: l.cloudDensity = std::clamp(l.cloudDensity + step * 0.10f, 0.1f, 2.5f); break;
            case 3: l.cloudBase = std::clamp(l.cloudBase + step * 1000.0f, 4000.0f, 40000.0f); break;
            case 4: l.cloudThickness = std::clamp(l.cloudThickness + step * 500.0f, 1000.0f, 20000.0f); break;
            case 5: l.cloudShadow = std::clamp(l.cloudShadow + step * 0.05f, 0.0f, 1.0f); break;
            case 6: l.cloudWindX = std::clamp(l.cloudWindX + step * 40.0f, -600.0f, 600.0f); break;
            case 7: l.cloudWindY = std::clamp(l.cloudWindY + step * 40.0f, -600.0f, 600.0f); break;
            case 8: l.volumetricFog = !l.volumetricFog; break;
            case 9: l.fogReplacesGame = !l.fogReplacesGame; break;
            case 10: l.fogDensity = std::clamp(l.fogDensity + step * 0.00005f, 0.0f, 0.002f); break;
            case 11: l.fogAnisotropy = std::clamp(l.fogAnisotropy + step * 0.05f, 0.0f, 0.90f); break;
            case 12: l.fogSunScale = std::clamp(l.fogSunScale + step * 0.20f, 0.0f, 5.0f); break;
            }
        } else if (g_tab == 2) {
            switch (g_sel) {
            case 0: l.season = (l.season + 5 + dir) % 5; break;
            case 1: g_menuSnow = g_menuSnow < 0.0f ? (dir > 0 ? 0.5f : 1.0f) : (g_menuSnow + dir * 0.25f < 0.0f ? -1.0f : std::min(1.0f, g_menuSnow + dir * 0.25f));
                    WeatherDrive_SetTest(g_menuRain, g_menuSnow); break;
            case 2: g_menuRain = g_menuRain < 0.0f ? (dir > 0 ? 0.5f : 1.0f) : (g_menuRain + dir * 0.25f < 0.0f ? -1.0f : std::min(1.0f, g_menuRain + dir * 0.25f));
                    WeatherDrive_SetTest(g_menuRain, g_menuSnow); break;
            case 3: l.wetness = std::clamp(l.wetness + step * 0.10f, 0.0f, 1.0f); break;
            case 4: l.puddles = std::clamp(l.puddles + step * 0.10f, 0.0f, 1.0f); break;
            case 5: l.snow = std::clamp(l.snow + step * 0.10f, 0.0f, 1.0f); break;
            case 6: l.rain = std::clamp(l.rain + step * 0.10f, 0.0f, 1.0f); break;
            }
        } else if (g_tab == 3) {
            switch (g_sel) {
            case 0: l.water = !l.water; break;
            case 1: l.waterReflect = std::clamp(l.waterReflect + step * 0.10f, 0.0f, 1.5f); break;
            case 2: l.waterCaustics = std::clamp(l.waterCaustics + step * 0.10f, 0.0f, 3.0f); break;
            case 3: l.waterAbsorb = std::clamp(l.waterAbsorb + step * 0.002f, 0.001f, 0.05f); break;
            case 4: l.waterGlint = std::clamp(l.waterGlint + step * 0.10f, 0.0f, 3.0f); break;
            case 5: l.waterCausticCell = std::clamp(l.waterCausticCell + step * 10.0f, 20.0f, 200.0f); break;
            case 6: l.waterRefract = std::clamp(l.waterRefract + step * 0.05f, 0.0f, 1.0f); break;
            case 7: l.waterView = (l.waterView + 5 + dir) % 5; break;
            }
        } else if (g_tab == 4) {
            switch (g_sel) {
            case 0: l.sss = std::clamp(l.sss + step * 0.10f, 0.0f, 1.5f); break;
            case 1: l.sssWidth = std::clamp(l.sssWidth + step * 0.10f, 0.1f, 2.0f); break;
            case 2: l.ao = !l.ao; break;
            case 3: l.aoStrength = std::clamp(l.aoStrength + step * 0.10f, 0.0f, 2.0f); break;
            case 4: l.contactShadows = !l.contactShadows; break;
            case 5: l.contactLength = std::clamp(l.contactLength + step * 2.0f, 2.0f, 64.0f); break;
            case 6: l.softShadows = !l.softShadows; break;
            case 7: l.softRange = std::clamp(l.softRange + step * 20.0f, 20.0f, 400.0f); break;
            case 8: l.ggxSpecular = !l.ggxSpecular; break;
            case 9: l.cascades = !l.cascades; break;
            case 10: l.distantLod = !l.distantLod; break;
            case 11: l.envMap = !l.envMap; break;
            }
        } else if (g_tab == 5) {
            switch (g_sel) {
            case 0: tm.enabled = !tm.enabled; tmDirty = true; break;
            case 1: tm.x[2] = float((std::clamp(int(tm.x[2] + 0.5f), 0, 13) + 14 + dir) % 14); tmDirty = true; break;
            case 2: tm.view = tm.view >= 3 ? 0 : 3; tmDirty = true; break;
            case 3: tm.bias = std::clamp(tm.bias + step * 0.10f, -2.0f, 2.0f); tmDirty = true; break;
            case 4: tm.key = std::clamp(tm.key + step * 0.02f, 0.15f, 0.85f); tmDirty = true; break;
            case 5: tm.adapt = std::clamp(tm.adapt + step * 0.05f, 0.0f, 1.0f); tmDirty = true; break;
            case 6: tm.x[1] = std::clamp(tm.x[1] + step * 0.25f, 1.5f, 10.0f); tmDirty = true; break;
            case 7: tm.x[3] = std::clamp(tm.x[3] + step * 0.05f, 0.80f, 2.00f); tmDirty = true; break;
            case 8: tm.x[4] = std::clamp(tm.x[4] + step * 0.05f, 0.25f, 1.00f); tmDirty = true; break;
            case 9: tm.x[5] = std::clamp(tm.x[5] + step * 0.05f, 0.0f, 1.50f); tmDirty = true; break;
            case 10: l.view = (l.view + 28 + dir) % 28; break;
            }
        } else if (g_tab == 6) {
            const int lutN = int(Tonemap_LutCount()) + 1;
            switch (g_sel) {
            case 0: g_hwTier = (g_hwTier + 5 + dir) % 5; ApplyHardwareTier(g_hwTier); break;
            case 1: raw::bridge::StreamlineHost::Get().SetMode((raw::bridge::StreamlineHost::Get().GetMode() + 6 + dir) % 6); break;
            case 2: tm.curve = (tm.curve + 9 + dir) % 9; tmDirty = true; break;
            case 3: tm.lutIndex = (tm.lutIndex + lutN + dir) % lutN; tmDirty = true; break;
            case 4: tm.lutStrength = std::clamp(tm.lutStrength + step * 0.05f, 0.0f, 1.0f); tmDirty = true; break;
            case 5: tm.overlayMask ^= 1u; tmDirty = true; break;
            case 6: tm.overlayMask ^= 2u; tmDirty = true; break;
            case 7: tm.overlayMask ^= 4u; tmDirty = true; break;
            case 8: tm.overlayMask ^= 8u; tmDirty = true; break;
            case 9: tm.overlayMask ^= 16u; tmDirty = true; break;
            case 10: tm.overlayMask ^= 32u; tmDirty = true; break;
            case 11: tm.overlayMask ^= 64u; tmDirty = true; break;
            case 12: tm.overlayMask ^= 128u; tmDirty = true; break;
            case 13: tm.overlayMask ^= 256u; tmDirty = true; break;
            case 14: tm.overlayMask ^= 512u; tmDirty = true; break;
            case 15: tm.overlayMask ^= 1024u; tmDirty = true; break;
            case 16: tm.overlayMask ^= 2048u; tmDirty = true; break;
            case 17: raw::bridge::StreamlineHost::Get().SetReflex(!raw::bridge::StreamlineHost::Get().GetReflex()); break;
            case 18: raw::bridge::StreamlineHost::Get().SetDeepDvc(!raw::bridge::StreamlineHost::Get().GetDeepDvc()); break;
            case 19: g_presetSlot = ((g_presetSlot - 1 + 5 + dir) % 5) + 1; break;
            }
        } else {
            switch (g_sel) {
            case 0: l.dof = !l.dof; break;
            case 1: l.dofSensor = (l.dofSensor + 4 + dir) % 4; break;
            case 2: l.dofFocalMm = std::clamp(l.dofFocalMm + step * 5.0f, 24.0f, 200.0f); break;
            case 3: l.dofFStop = std::clamp(l.dofFStop + step * 0.2f, 0.95f, 16.0f); break;
            case 4: l.dofBokehShape = (l.dofBokehShape + 7 + dir) % 7; break;
            case 5: l.dofNearScale = std::clamp(l.dofNearScale + step * 0.10f, 0.0f, 2.5f); break;
            case 6: l.dofFarScale = std::clamp(l.dofFarScale + step * 0.10f, 0.0f, 2.5f); break;
            case 7: l.dofNearBleed = std::clamp(l.dofNearBleed + step * 0.10f, 0.0f, 2.0f); break;
            case 8: l.dofRadius = std::clamp(l.dofRadius + step * 0.5f, 2.0f, 28.0f); break;
            case 9: l.dofOrbBoost = std::clamp(l.dofOrbBoost + step * 0.15f, 0.0f, 5.0f); break;
            case 10: l.dofFocusSpeed = std::clamp(l.dofFocusSpeed + step * 0.01f, 0.02f, 0.30f); break;
            case 11: l.dofMaxDist = std::clamp(l.dofMaxDist + step * 60.0f, 150.0f, 2500.0f); break;
            case 12: l.weatherLens = !l.weatherLens; break;
            case 13: tm.bloomScale = std::clamp(tm.bloomScale + step * 0.10f, 0.0f, 2.5f); tmDirty = true; break;
            case 14: tm.sharpenStr = std::clamp(tm.sharpenStr + step * 0.10f, 0.0f, 2.0f); tmDirty = true; break;
            case 15: tm.grainStr = std::clamp(tm.grainStr + step * 0.10f, 0.0f, 2.5f); tmDirty = true; break;
            case 16: tm.vignetteStr = std::clamp(tm.vignetteStr + step * 0.10f, 0.0f, 2.0f); tmDirty = true; break;
            case 17: tm.caStr = std::clamp(tm.caStr + step * 0.10f, 0.0f, 2.5f); tmDirty = true; break;
            case 18: tm.radialBlurStr = std::clamp(tm.radialBlurStr + step * 0.10f, 0.0f, 2.5f); tmDirty = true; break;
            case 19: tm.headbobStr = std::clamp(tm.headbobStr + step * 0.10f, 0.0f, 2.5f); tmDirty = true; break;
            case 20: tm.lensStr = std::clamp(tm.lensStr + step * 0.10f, 0.0f, 2.5f); tmDirty = true; break;
            case 21: tm.colorTempK = std::clamp(tm.colorTempK + step * 150.0f, 3500.0f, 9500.0f); tmDirty = true; break;
            case 22: tm.oklabSat = std::clamp(tm.oklabSat + step * 0.05f, 0.0f, 2.0f); tmDirty = true; break;
            case 23: tm.shadowLift = std::clamp(tm.shadowLift + step * 0.01f, -0.15f, 0.15f); tmDirty = true; break;
            case 24: tm.highlightKnee = std::clamp(tm.highlightKnee + step * 0.04f, 0.35f, 1.40f); tmDirty = true; break;
            case 25: l.cameraMode = (l.cameraMode + 6 + dir) % 6; break;
            }
        }
        if (tmDirty) Tonemap_Set(tm);
        hook::LightingApplyTuning();
        changed = true;
    }
    outText = BuildMenuText();
    return changed;
}

bool Render(const std::string& text, UINT maxW, bool isMenu, std::vector<uint8_t>& px, UINT& w, UINT& h)
{
    std::vector<std::string> lines;
    for (size_t at = 0; at <= text.size() && lines.size() < 40;) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        lines.push_back(text.substr(at, std::min<size_t>(end - at, 150)));
        at = end + 1;
    }
    const int lineH = 19;
    const LONG listW = isMenu ? 1040 : 1400;
    w = std::min<UINT>(maxW, isMenu ? 1310 : 1400);
    h = UINT(std::max<size_t>(lines.size(), isMenu ? 27 : lines.size()) * lineH + 26);
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = LONG(w);
    bi.bmiHeader.biHeight = -LONG(h);
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) { DeleteDC(dc); return false; }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    HFONT font = CreateFontA(16, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");
    HFONT hdrFont = CreateFontA(17, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    RECT all{0, 0, LONG(w), LONG(h)};
    HBRUSH bg = CreateSolidBrush(isMenu ? RGB(13, 17, 25) : RGB(90, 10, 10));
    FillRect(dc, &all, bg);
    if (isMenu) {
        // 2027 Architectural Glassmorphic HUD chrome: luminous cyan top accent, obsidian header band & precision steel border.
        HBRUSH borderBr = CreateSolidBrush(RGB(44, 62, 84));
        FrameRect(dc, &all, borderBr);
        DeleteObject(borderBr);
        RECT hdrRect{1, 4, LONG(w) - 1, LONG(14 + 2 * lineH)};
        HBRUSH hdrBr = CreateSolidBrush(RGB(9, 13, 20));
        FillRect(dc, &hdrRect, hdrBr);
        DeleteObject(hdrBr);
        RECT accentTop{0, 0, LONG(w), 3};
        HBRUSH accBr = CreateSolidBrush(RGB(66, 212, 196));
        FillRect(dc, &accentTop, accBr);
        DeleteObject(accBr);
    }
    SetBkMode(dc, TRANSPARENT);
    HBRUSH zebraBr = CreateSolidBrush(RGB(18, 24, 35));
    HBRUSH selBgBr = CreateSolidBrush(RGB(34, 43, 58));
    HBRUSH selBarBr = CreateSolidBrush(RGB(248, 196, 86));
    HBRUSH onPillBr = CreateSolidBrush(RGB(18, 72, 58));
    HBRUSH offPillBr = CreateSolidBrush(RGB(38, 43, 54));
    for (size_t i = 0; i < lines.size(); ++i) {
        const int y0 = int(12 + i * lineH);
        const bool isSel = isMenu && lines[i].rfind(">>", 0) == 0;
        if (isMenu && i >= 2) {
            RECT rowRect{2, y0 - 2, listW - 4, y0 + lineH - 2};
            if (isSel) {
                FillRect(dc, &rowRect, selBgBr);
                RECT barRect{2, y0 - 2, 6, y0 + lineH - 2};
                FillRect(dc, &barRect, selBarBr);
            } else if ((i & 1u) == 1u) {
                FillRect(dc, &rowRect, zebraBr);
            }
            // Render crisp color-coded ON / OFF status pill badges on right side of boolean rows:
            const size_t posOn = lines[i].rfind(": ON");
            const size_t posOff = lines[i].rfind(": OFF");
            if (posOn != std::string::npos && posOn + 4 == lines[i].size()) {
                RECT pill{listW - 68, y0, listW - 14, y0 + lineH - 4};
                FillRect(dc, &pill, onPillBr);
                SetTextColor(dc, RGB(88, 240, 196));
                TextOutA(dc, listW - 52, y0, "ON", 2);
            } else if (posOff != std::string::npos && posOff + 5 == lines[i].size()) {
                RECT pill{listW - 68, y0, listW - 14, y0 + lineH - 4};
                FillRect(dc, &pill, offPillBr);
                SetTextColor(dc, RGB(148, 160, 178));
                TextOutA(dc, listW - 56, y0, "OFF", 3);
            } else {
                // Draw a sleek 110px horizontal progress slider bar on numeric/percentage rows:
                const size_t colPos = lines[i].rfind(": ");
                if (colPos != std::string::npos && colPos + 2 < lines[i].size()) {
                    char* endP = nullptr;
                    const float val = std::strtof(lines[i].c_str() + colPos + 2, &endP);
                    if (endP != lines[i].c_str() + colPos + 2) {
                        const bool isPct = (*endP == '%');
                        const float norm = isPct ? std::clamp(val / 100.0f, 0.0f, 1.0f)
                                         : (val < 0.0f ? std::clamp((val + 2.0f) / 4.0f, 0.0f, 1.0f)
                                                       : (val <= 2.5f ? std::clamp(val / 2.5f, 0.0f, 1.0f)
                                                                      : (val <= 30.0f ? std::clamp(val / 30.0f, 0.0f, 1.0f)
                                                                                      : std::clamp(val / 10000.0f, 0.0f, 1.0f))));
                        const LONG sx0 = listW - 128, sx1 = listW - 14;
                        const LONG sy = y0 + (lineH / 2) - 3;
                        RECT trk{sx0, sy, sx1, sy + 5};
                        FillRect(dc, &trk, offPillBr);
                        const LONG fillX = sx0 + LONG((sx1 - sx0) * norm);
                        RECT fl{sx0, sy, std::max(sx0 + 2, fillX), sy + 5};
                        FillRect(dc, &fl, isSel ? selBarBr : onPillBr);
                        RECT knob{std::clamp(fillX - 2, sx0, sx1 - 4), sy - 2, std::clamp(fillX + 2, sx0 + 4, sx1), sy + 7};
                        FillRect(dc, &knob, selBarBr);
                    }
                }
            }
        }
        SelectObject(dc, (isMenu && i == 0) ? hdrFont : font);
        if (isMenu && i <= 1) SetTextColor(dc, i == 0 ? RGB(72, 224, 208) : RGB(154, 208, 245));
        else if (isSel) SetTextColor(dc, RGB(252, 204, 96));
        else SetTextColor(dc, RGB(234, 236, 242));
        TextOutA(dc, 14, y0, lines[i].c_str(), int(lines[i].size()));
    }
    // Right-Side Live Visual Studio Widget Bay (Color Wheel, Mechanical Aperture Iris & Live Tone Curve Graph):
    if (isMenu && LONG(w) >= listW + 220) {
        const auto& l = hook::Cfg().light;
        const auto tm = Tonemap_Get();
        const LONG px0 = listW + 10, pw = LONG(w) - px0 - 12;
        const LONG cx = px0 + pw / 2;
        RECT bay{px0, 56, px0 + pw, LONG(h) - 14};
        HBRUSH bayBr = CreateSolidBrush(RGB(10, 14, 21));
        FillRect(dc, &bay, bayBr);
        DeleteObject(bayBr);
        HBRUSH sepBr = CreateSolidBrush(RGB(40, 56, 76));
        FrameRect(dc, &bay, sepBr);
        DeleteObject(sepBr);
        SelectObject(dc, font);
        // 1) Live Oklab Color Wheel & White-Balance / Saturation Crosshair:
        SetTextColor(dc, RGB(72, 224, 208));
        TextOutA(dc, px0 + 12, 64, "OKLAB COLOR WHEEL", 17);
        const int wCy = 152, wRad = 58;
        for (int dy = -wRad; dy <= wRad; ++dy) {
            for (int dx = -wRad; dx <= wRad; ++dx) {
                const float r = std::sqrt(float(dx * dx + dy * dy)) / float(wRad);
                if (r > 1.0f) continue;
                const float ang = std::atan2(float(dy), float(dx));
                const float hDeg = (ang < 0.0f ? ang + 6.2831853f : ang) * (180.0f / 3.14159265f);
                const float c = r, x = c * (1.0f - std::fabs(std::fmod(hDeg / 60.0f, 2.0f) - 1.0f)), m = 1.0f - c * 0.55f;
                float rr = 0, gg = 0, bb = 0;
                if (hDeg < 60) { rr = c; gg = x; } else if (hDeg < 120) { rr = x; gg = c; }
                else if (hDeg < 180) { gg = c; bb = x; } else if (hDeg < 240) { gg = x; bb = c; }
                else if (hDeg < 300) { rr = x; bb = c; } else { rr = c; bb = x; }
                SetPixelV(dc, cx + dx, wCy + dy, RGB(int(std::clamp((rr + m * 0.35f) * 230.0f, 0.0f, 255.0f)),
                                                     int(std::clamp((gg + m * 0.35f) * 230.0f, 0.0f, 255.0f)),
                                                     int(std::clamp((bb + m * 0.35f) * 230.0f, 0.0f, 255.0f))));
            }
        }
        const float wbNorm = std::clamp((tm.colorTempK - 6500.0f) / 3000.0f, -1.0f, 1.0f);
        const float satNorm = std::clamp(tm.oklabSat * 0.5f, 0.0f, 1.0f);
        const int dotX = cx + int(wbNorm * wRad * 0.85f);
        const int dotY = wCy - int((satNorm - 0.5f) * wRad * 1.3f);
        RECT dotR{dotX - 4, dotY - 4, dotX + 5, dotY + 5};
        FillRect(dc, &dotR, selBarBr);
        // 2) Live Mechanical Aperture Iris Diagram (5/6/7/9-blade, Circle, Anamorphic 2x, Petzval):
        SetTextColor(dc, RGB(72, 224, 208));
        char irisLbl[48];
        std::snprintf(irisLbl, sizeof irisLbl, "IRIS: %.0fmm f/%.1f", l.dofFocalMm, l.dofFStop);
        TextOutA(dc, px0 + 12, 224, irisLbl, int(std::strlen(irisLbl)));
        const int iCy = 296;
        const float openT = std::clamp(1.0f - (std::log2(std::max(l.dofFStop, 0.95f)) / 4.0f), 0.16f, 0.96f);
        const float iRad = 46.0f * openT;
        HPEN irisPen = CreatePen(PS_SOLID, 2, RGB(248, 196, 86));
        HGDIOBJ oldPen = SelectObject(dc, irisPen);
        HBRUSH irisFill = CreateSolidBrush(RGB(22, 34, 48));
        HGDIOBJ oldBr = SelectObject(dc, irisFill);
        const int sh = std::clamp(l.dofBokehShape, 0, 6);
        if (sh <= 3) {
            const int nb = (sh == 0) ? 5 : (sh == 1) ? 6 : (sh == 2) ? 7 : 9;
            POINT pts[10];
            for (int k = 0; k < nb; ++k) {
                const float a = float(k) * (6.2831853f / float(nb)) - 1.5707963f;
                pts[k].x = cx + LONG(std::cos(a) * iRad);
                pts[k].y = iCy + LONG(std::sin(a) * iRad);
            }
            Polygon(dc, pts, nb);
        } else {
            const float rx = (sh == 5) ? iRad * 0.62f : iRad;
            const float ry = (sh == 5) ? iRad * 1.18f : iRad;
            Ellipse(dc, cx - int(rx), iCy - int(ry), cx + int(rx), iCy + int(ry));
        }
        // 3) Live HDR Tone & Exposure Response Curve Graph:
        SetTextColor(dc, RGB(72, 224, 208));
        TextOutA(dc, px0 + 12, 362, "HDR TONE CURVE", 14);
        const int gx0 = px0 + 14, gy0 = 386, gw = pw - 28, gh = 92;
        RECT gBox{gx0, gy0, gx0 + gw, gy0 + gh};
        FillRect(dc, &gBox, zebraBr);
        HPEN curvePen = CreatePen(PS_SOLID, 2, RGB(88, 240, 196));
        SelectObject(dc, curvePen);
        const float expGain = std::pow(2.0f, tm.bias);
        for (int xi = 0; xi <= gw; xi += 2) {
            const float xIn = (float(xi) / float(gw)) * 2.5f * expGain;
            const float knee = std::max(tm.highlightKnee, 0.25f);
            const float yOut = std::clamp((xIn / (xIn + knee)) + tm.shadowLift, 0.0f, 1.0f);
            const int py = gy0 + gh - int(yOut * float(gh - 4)) - 2;
            if (xi == 0) MoveToEx(dc, gx0 + xi, py, nullptr);
            else LineTo(dc, gx0 + xi, py);
        }
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBr);
        DeleteObject(irisPen);
        DeleteObject(curvePen);
        DeleteObject(irisFill);
        char statusLine[48];
        std::snprintf(statusLine, sizeof statusLine, "RAWDLSS 5.0: %s", l.rawlss5 ? "ACTIVE" : "OFF");
        SetTextColor(dc, l.rawlss5 ? RGB(88, 240, 196) : RGB(148, 160, 178));
        TextOutA(dc, px0 + 14, gy0 + gh + 10, statusLine, int(std::strlen(statusLine)));
    }
    DeleteObject(zebraBr);
    DeleteObject(selBgBr);
    DeleteObject(selBarBr);
    DeleteObject(onPillBr);
    DeleteObject(offPillBr);
    GdiFlush();
    px.assign(static_cast<uint8_t*>(bits), static_cast<uint8_t*>(bits) + size_t(w) * h * 4);
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(font);
    DeleteObject(hdrFont);
    DeleteObject(bg);
    DeleteObject(bmp);
    DeleteDC(dc);
    return true;
}
} // namespace

void Overlay_Set(const std::string& text)
{
    std::lock_guard<std::mutex> g(g_m);
    if (text == g_text) return;
    g_text = text;
    g_dirty = true;
}

extern "C" __declspec(dllexport) int PG_IsMenuOpen()
{
    return g_menuOpen ? 1 : 0;
}

void Overlay_Draw()
{
    std::string text;
    bool dirty = false;
    bool isMenu = false;
    {
        std::lock_guard<std::mutex> g(g_m);
        text = g_text;
        dirty = g_dirty;
        g_dirty = false;
    }
    if (text.empty()) {
        static std::string s_lastMenu;
        std::string menuText;
        PollMenu(menuText);
        if (!g_menuOpen) return;
        isMenu = true;
        text = menuText;
        if (text != s_lastMenu) { s_lastMenu = text; dirty = true; }
    }
    if (text.empty()) return;
    auto* pi = PG_GetProxyInterface();
    if (!pi || !pi->device || !pi->context || !pi->swapChain) return;
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(pi->swapChain->GetBuffer(0, IID_PPV_ARGS(&bb))) || !bb) return;
    D3D11_TEXTURE2D_DESC bd{};
    bb->GetDesc(&bd);
    const bool bgra = bd.Format == DXGI_FORMAT_B8G8R8A8_UNORM || bd.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    const bool rgba = bd.Format == DXGI_FORMAT_R8G8B8A8_UNORM || bd.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    if ((!bgra && !rgba) || bd.SampleDesc.Count != 1) { bb->Release(); return; }
    if (dirty || !g_tex || g_fmt != bd.Format) {
        std::vector<uint8_t> px;
        UINT w = 0, h = 0;
        if (Render(text, bd.Width - 32, isMenu, px, w, h)) {
            if (rgba)
                for (size_t i = 0; i < px.size(); i += 4) std::swap(px[i], px[i + 2]);   // GDI writes B, G, R
            for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
            if (g_tex) { g_tex->Release(); g_tex = nullptr; }
            D3D11_TEXTURE2D_DESC d{};
            d.Width = w; d.Height = std::min(h, bd.Height - 32); d.MipLevels = 1; d.ArraySize = 1; d.Format = bd.Format;
            d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
            D3D11_SUBRESOURCE_DATA init{px.data(), w * 4, 0};
            if (SUCCEEDED(pi->device->CreateTexture2D(&d, &init, &g_tex))) { g_w = d.Width; g_h = d.Height; g_fmt = bd.Format; }
            else Log("[Overlay] texture %ux%u failed", w, h);
        }
    }
    if (g_tex) pi->context->CopySubresourceRegion(bb, 0, 16, 16, 0, g_tex, 0, nullptr);
    bb->Release();
}

} // namespace SB::Proxy
