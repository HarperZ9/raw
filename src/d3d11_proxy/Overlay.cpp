// Overlay.cpp: Design-Canon 10-Domain RAW DX12 Studio UI + Unified Mouse/Keyboard/XInput Controller Stack.
// Zero SKSE dependency. Uses RawDesignTokens (DESIGN-RULES.md + colour-tokens.json), RawInputEngine, and RawStudioUi.
// Original work, MIT.
#include "Overlay.h"
#include "HotReload.h"
#include "BridgeHookState.h"
#include "ProxyAPI.h"
#include "ProxyLog.h"
#include "Tonemap.h"
#include "StreamlineHost.h"
#include "WeatherDrive.h"
#include "../ui/RawDesignTokens.h"
#include "../ui/RawInputEngine.h"
#include "../ui/RawStudioUi.h"

#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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

bool g_menuOpen = false;
int  g_tab = 0;
int  g_sel = 0;
int  g_scroll = 0;
int  g_hwTier = 4;
int  g_presetSlot = 1;
float g_menuSnow = -1.0f, g_menuRain = -1.0f;

HWND g_hookHwnd = nullptr;
WNDPROC g_origWndProc = nullptr;
raw::ui::RawInputEngine g_input;
raw::ui::RawStudioUi    g_studio;
raw::ui::InputNavFrame  g_lastInput{};

LARGE_INTEGER g_qpcFreq{}, g_qpcLast{};
float g_emaMs = 11.0f;
uint32_t g_savedFrames = 0;
std::string g_statusTag;

// Window subclass captures mouse/keyboard cleanly when RAW Studio is open and feeds wheel/coords to RawInputEngine.
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_menuOpen) {
        switch (msg) {
        case WM_MOUSEMOVE:
            g_input.OnMouseMove(int32_t(int16_t(LOWORD(lp))), int32_t(int16_t(HIWORD(lp))));
            return 0;
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
            g_input.OnMouseButton(0, true);
            return 0;
        case WM_LBUTTONUP:
            g_input.OnMouseButton(0, false);
            return 0;
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
            g_input.OnMouseButton(1, true);
            return 0;
        case WM_RBUTTONUP:
            g_input.OnMouseButton(1, false);
            return 0;
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wp);
            g_input.OnMouseWheel(delta > 0 ? 1 : (delta < 0 ? -1 : 0));
            return 0;
        }
        case WM_INPUT:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MOUSEHWHEEL:
            return 0;
        case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_CHAR:
            if (wp == VK_TAB || wp == VK_UP || wp == VK_DOWN || wp == VK_LEFT || wp == VK_RIGHT ||
                wp == VK_PRIOR || wp == VK_NEXT || wp == 'S' || wp == 's' || wp == 'L' || wp == 'l' ||
                wp == 'P' || wp == 'p' || wp == 'T' || wp == 't' || wp == VK_ESCAPE ||
                wp == VK_RETURN || wp == VK_SPACE || wp == VK_END)
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
    const auto& sim = raw::ui::GetWorldSimTuning();
    bool ok = true;
    auto wF = [&](const char* sec, const char* k, float v) { char b[32]; std::snprintf(b, sizeof b, "%.4g", v); if (!WritePrivateProfileStringA(sec, k, b, path.c_str())) ok = false; };
    auto wI = [&](const char* sec, const char* k, int v) { char b[32]; std::snprintf(b, sizeof b, "%d", v); if (!WritePrivateProfileStringA(sec, k, b, path.c_str())) ok = false; };
    wI("Lighting", "Mode", l.mode); wI("Lighting", "Quality", g_hwTier);
    wI("Lighting", "RtShadows", l.rtShadows ? 1 : 0); wI("Lighting", "RtSky", l.rtSky ? 1 : 0);
    wI("Lighting", "RtReflect", l.rtReflect ? 1 : 0); wI("Lighting", "RtOmm", l.rtOmm ? 1 : 0);
    wI("Lighting", "RtFar", l.rtFar ? 1 : 0); wI("Lighting", "RtSkin", l.rtSkin ? 1 : 0); wI("Lighting", "RtRest", l.rtRest ? 1 : 0);
    wF("Lighting", "RtBounce", l.rtBounce); wF("Lighting", "RtBounceInterior", l.rtBounceInterior);
    wF("Lighting", "RtSkyStrength", l.rtSkyStrength); wF("Lighting", "RtSkyLength", l.rtSkyLength);
    wI("Lighting", "Clouds", l.clouds ? 1 : 0); wF("Lighting", "CloudCover", l.cloudCover);
    wF("Lighting", "CloudDensity", l.cloudDensity); wF("Lighting", "CloudShadow", l.cloudShadow);
    wI("Lighting", "VolumetricFog", l.volumetricFog ? 1 : 0); wI("Lighting", "FogReplacesGame", l.fogReplacesGame ? 1 : 0);
    wF("Lighting", "FogSunScale", l.fogSunScale); wI("Lighting", "Season", l.season);
    wI("Lighting", "Water", l.water ? 1 : 0); wF("Lighting", "WaterReflect", l.waterReflect);
    wF("Lighting", "WaterCaustics", l.waterCaustics); wF("Lighting", "WaterRefract", l.waterRefract);
    wF("Lighting", "Sss", l.sss); wF("Lighting", "AoStrength", l.aoStrength);
    wI("Lighting", "Tonemap", tm.enabled ? 1 : 0); wI("Lighting", "ToneCurve", tm.curve);
    wF("Lighting", "ExposureBias", tm.bias); wF("Lighting", "ExposureKey", tm.key);
    wI("Lighting", "LutIndex", tm.lutIndex); wF("Lighting", "LutStrength", tm.lutStrength);
    wI("Lighting", "OverlayMask", int(tm.overlayMask)); wF("Lighting", "ColorTempK", tm.colorTempK);
    wF("Lighting", "OklabSat", tm.oklabSat); wF("Lighting", "ShadowLift", tm.shadowLift); wF("Lighting", "HighlightKnee", tm.highlightKnee);
    wI("Lighting", "Rawlss5", l.rawlss5 ? 1 : 0); wI("Lighting", "Dof", l.dof ? 1 : 0);
    wF("Lighting", "DofFStop", l.dofFStop); wF("Lighting", "DofFocalMm", l.dofFocalMm); wF("Lighting", "DofRadius", l.dofRadius);
    // WorldSim & Geometry Uplift section
    wI("WorldSim", "GeomSubdivLevel", sim.geomSubdivLevel); wF("WorldSim", "GeomCurvature", sim.geomCurvature);
    wF("WorldSim", "GeomDisplaceScale", sim.geomDisplaceScale); wI("WorldSim", "SnowVolumetric", sim.snowVolumetric ? 1 : 0);
    wF("WorldSim", "SnowMaxDepthU", sim.snowMaxDepthU); wI("WorldSim", "IkFoot", sim.ikFootEnabled ? 1 : 0);
    wI("WorldSim", "CombatAi", sim.combatAiEnabled ? 1 : 0); wF("WorldSim", "ParryWindowMs", sim.parryWindowMs);
    wF("WorldSim", "HitStopMs", sim.hitStopMs); wI("WorldSim", "DarkTheme", sim.darkTheme ? 1 : 0);
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
    auto rI = [&](const char* sec, const char* k, int d) { return int(GetPrivateProfileIntA(sec, k, d, path.c_str())); };
    auto rF = [&](const char* sec, const char* k, float d) { char b[64]{}; GetPrivateProfileStringA(sec, k, "", b, sizeof b, path.c_str()); return b[0] ? std::strtof(b, nullptr) : d; };
    auto& l = hook::Cfg().light;
    auto tm = Tonemap_Get();
    auto& sim = raw::ui::GetWorldSimTuning();
    l.mode = rI("Lighting", "Mode", l.mode); g_hwTier = std::clamp(rI("Lighting", "Quality", g_hwTier), 0, 4);
    l.rtShadows = rI("Lighting", "RtShadows", l.rtShadows) != 0; l.rtSky = rI("Lighting", "RtSky", l.rtSky) != 0;
    l.rtReflect = rI("Lighting", "RtReflect", l.rtReflect) != 0; l.rtOmm = rI("Lighting", "RtOmm", l.rtOmm) != 0;
    l.rtBounce = rF("Lighting", "RtBounce", l.rtBounce); l.rtBounceInterior = rF("Lighting", "RtBounceInterior", l.rtBounceInterior);
    l.clouds = rI("Lighting", "Clouds", l.clouds) != 0; l.cloudCover = rF("Lighting", "CloudCover", l.cloudCover);
    l.volumetricFog = rI("Lighting", "VolumetricFog", l.volumetricFog) != 0; l.water = rI("Lighting", "Water", l.water) != 0;
    l.dof = rI("Lighting", "Dof", l.dof) != 0; l.dofFStop = rF("Lighting", "DofFStop", l.dofFStop);
    tm.curve = rI("Lighting", "ToneCurve", tm.curve); tm.bias = rF("Lighting", "ExposureBias", tm.bias);
    tm.colorTempK = rF("Lighting", "ColorTempK", tm.colorTempK); tm.oklabSat = rF("Lighting", "OklabSat", tm.oklabSat);
    sim.geomSubdivLevel = std::clamp(rI("WorldSim", "GeomSubdivLevel", sim.geomSubdivLevel), 0, 2);
    sim.geomCurvature = std::clamp(rF("WorldSim", "GeomCurvature", sim.geomCurvature), 0.0f, 1.25f);
    sim.snowVolumetric = rI("WorldSim", "SnowVolumetric", sim.snowVolumetric) != 0;
    sim.snowMaxDepthU = std::clamp(rF("WorldSim", "SnowMaxDepthU", sim.snowMaxDepthU), 4.0f, 48.0f);
    sim.combatAiEnabled = rI("WorldSim", "CombatAi", sim.combatAiEnabled) != 0;
    Tonemap_Set(tm);
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

void SaveCurrentConfig()
{
    const std::string dir = ExeDir();
    if (dir.empty()) return;
    if (g_tab == 9) {
        CreateDirectoryA((dir + "raw_presets").c_str(), nullptr);
        SavePresetFile(dir + "raw_presets\\slot_" + std::to_string(g_presetSlot) + ".ini",
                       (" [SAVED SLOT " + std::to_string(g_presetSlot) + "]").c_str());
    } else {
        SavePresetFile(dir + "d3d11_proxy.ini", " [SAVED INI]");
    }
}

void ExportCameraRecipe()
{
    const std::string dir = ExeDir();
    if (dir.empty()) return;
    const std::string capDir = dir + "raw_captures";
    CreateDirectoryA(capDir.c_str(), nullptr);
    SYSTEMTIME st{}; GetLocalTime(&st);
    char fname[128];
    std::snprintf(fname, sizeof fname, "\\RAW_%04u%02u%02u_%02u%02u%02u_recipe.ini",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    SavePresetFile(capDir + fname, " [RECIPE EXPORTED]");
}

std::string Fmt(const char* f, float v) { char b[64]; std::snprintf(b, sizeof b, f, v); return b; }
std::string FmtI(const char* f, int v) { char b[64]; std::snprintf(b, sizeof b, f, v); return b; }

void AddToggle(std::vector<raw::ui::StudioRow>& out, const char* lbl, bool v) {
    raw::ui::StudioRow r; r.label = lbl; r.kind = raw::ui::RowKind::Toggle; r.boolVal = v;
    r.glyph = v ? raw::ui::VerdictGlyph::FilledDisc : raw::ui::VerdictGlyph::HollowRing;
    out.push_back(std::move(r));
}
void AddSlider(std::vector<raw::ui::StudioRow>& out, const char* lbl, float v, float lo, float hi, const char* fmt) {
    raw::ui::StudioRow r; r.label = lbl; r.kind = raw::ui::RowKind::Slider;
    r.valueText = Fmt(fmt, v);
    r.normVal = (hi > lo) ? std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
    out.push_back(std::move(r));
}
void AddCycle(std::vector<raw::ui::StudioRow>& out, const char* lbl, const std::string& val, bool accent = false) {
    raw::ui::StudioRow r; r.label = lbl; r.kind = raw::ui::RowKind::Cycle; r.valueText = val;
    r.glyph = accent ? raw::ui::VerdictGlyph::HalfDisc : raw::ui::VerdictGlyph::FilledDisc;
    r.activeAccent = accent;
    out.push_back(std::move(r));
}

std::vector<raw::ui::StudioRow> BuildRows(int tab)
{
    std::vector<raw::ui::StudioRow> rows;
    const auto& l = hook::Cfg().light;
    const auto tm = Tonemap_Get();
    const auto& sim = raw::ui::GetWorldSimTuning();

    if (tab == 0) {
        AddToggle(rows, "DXR 1.2 Ray-Traced Shadows", l.rtShadows);
        AddToggle(rows, "RT Sky Visibility & RTAO", l.rtSky);
        AddToggle(rows, "RT Glossy Reflections", l.rtReflect);
        AddToggle(rows, "DXR 1.2 Opacity Micromaps", l.rtOmm);
        AddToggle(rows, "RT Distant Terrain Far-Trace", l.rtFar);
        AddToggle(rows, "RT Skinned Actors BLAS", l.rtSkin);
        AddToggle(rows, "ReSTIR Visibility Reuse", l.rtRest);
        AddSlider(rows, "RT GI Bounce (Exterior)", l.rtBounce, 0.0f, 3.0f, "%.2f");
        AddSlider(rows, "RT GI Bounce (Interior)", l.rtBounceInterior, 0.0f, 3.0f, "%.2f");
        AddSlider(rows, "RT Sky Strength", l.rtSkyStrength, 0.0f, 2.0f, "%.2f");
        AddSlider(rows, "RT Sky Ray Length", l.rtSkyLength, 100.0f, 4000.0f, "%.0f u");
        AddToggle(rows, "RAWLSS 7.0 Split-Manifold", l.rawlss5);
        static const char* kDbgNames[] = {"Final", "Normals", "SunShadow", "MatClass", "4-Quad", "Heatmap", "BloomAudit", "WorldField", "Continuity"};
        static const int kDbgVals[] = {0, 1, 2, 12, 20, 21, 22, 23, 24};
        int di = 0; for (int k = 0; k < 9; ++k) if (kDbgVals[k] == l.view) { di = k; break; }
        AddCycle(rows, "Diagnostic View", kDbgNames[di], di != 0);
    } else if (tab == 1) {
        static const char* kSub[3] = {"Off (Stock)", "2x Curved PN-Tri (4x)", "Adaptive 4x->16x"};
        AddCycle(rows, "Runtime GPU Subdivision", kSub[std::clamp(sim.geomSubdivLevel, 0, 2)], sim.geomSubdivLevel > 0);
        AddSlider(rows, "PN-Triangle Curvature", sim.geomCurvature, 0.0f, 1.25f, "%.2f");
        AddSlider(rows, "World Micro-Displacement", sim.geomDisplaceScale, 0.0f, 2.0f, "%.2fx");
        AddToggle(rows, "Distant LOD Relighting", l.distantLod);
        AddToggle(rows, "Meshlet Wireframe Audit", sim.geomWireframe);
        AddToggle(rows, "EnvMap Conductor Split", l.envMap);
        AddToggle(rows, "GGX Energy-Conserving", l.ggxSpecular);
        AddCycle(rows, "Asset Hot-Swap (.glb)", "raw_assets\\overrides\\", true);
    } else if (tab == 2) {
        AddToggle(rows, "3D Volumetric Clouds", l.clouds);
        AddSlider(rows, "Cloud Cover", l.cloudCover, 0.0f, 1.0f, "%.2f");
        AddSlider(rows, "Cloud Density", l.cloudDensity, 0.1f, 2.5f, "%.2f");
        AddSlider(rows, "Cloud Shadow Strength", l.cloudShadow, 0.0f, 1.0f, "%.2f");
        AddSlider(rows, "Jet-Stream Wind X", l.cloudWindX, -600.0f, 600.0f, "%.0f");
        AddSlider(rows, "Jet-Stream Wind Y", l.cloudWindY, -600.0f, 600.0f, "%.0f");
        AddToggle(rows, "3D Froxel Volumetric Fog", l.volumetricFog);
        AddToggle(rows, "Fog Replaces Vanilla", l.fogReplacesGame);
        AddSlider(rows, "Froxel Fog Density", l.fogDensity, 0.0f, 0.0005f, "%.5f");
        AddSlider(rows, "Mie Phase Anisotropy (g)", l.fogAnisotropy, 0.0f, 0.90f, "%.2f");
        AddSlider(rows, "Volumetric Sun Shafts", l.fogSunScale, 0.0f, 3.0f, "%.2f");
    } else if (tab == 3) {
        static const char* kSeasons[5] = {"Auto", "Spring", "Summer", "Autumn", "Winter"};
        AddCycle(rows, "World Season", kSeasons[std::clamp(l.season, 0, 4)], l.season != 0);
        AddToggle(rows, "3D Volumetric Snowpack", sim.snowVolumetric);
        AddSlider(rows, "Snow Buildup Max Depth", sim.snowMaxDepthU, 4.0f, 48.0f, "%.0f u");
        AddSlider(rows, "Snow Accumulation Rate", sim.snowAccumRate, 0.0f, 3.0f, "%.2fx");
        AddSlider(rows, "Avalanche Repose Slope", sim.snowAvalancheAng, 0.20f, 0.75f, "%.2f");
        AddCycle(rows, "Force Snow Override", g_menuSnow < 0.0f ? "Auto" : Fmt("%.0f%%", g_menuSnow * 100.0f), g_menuSnow >= 0.0f);
        AddCycle(rows, "Force Rain Override", g_menuRain < 0.0f ? "Auto" : Fmt("%.0f%%", g_menuRain * 100.0f), g_menuRain >= 0.0f);
        AddSlider(rows, "Surface Wetness", l.wetness, 0.0f, 1.0f, "%.2f");
        AddSlider(rows, "Surface Puddles", l.puddles, 0.0f, 1.0f, "%.2f");
        AddSlider(rows, "Surface Snow Cover", l.snow, 0.0f, 1.0f, "%.2f");
        AddSlider(rows, "Active Rain Ripples", l.rain, 0.0f, 1.0f, "%.2f");
    } else if (tab == 4) {
        AddToggle(rows, "DXR 1.2 Physical Water", l.water);
        AddSlider(rows, "Water Reflections", l.waterReflect, 0.0f, 1.5f, "%.2f");
        AddSlider(rows, "Underwater Caustics", l.waterCaustics, 0.0f, 3.0f, "%.2f");
        AddSlider(rows, "Beer-Lambert Absorption", l.waterAbsorb, 0.001f, 0.04f, "%.4f");
        AddSlider(rows, "Wave Sun Glint", l.waterGlint, 0.0f, 3.0f, "%.2f");
        AddSlider(rows, "Snell Refraction Blend", l.waterRefract, 0.0f, 1.0f, "%.2f");
        AddCycle(rows, "Water Diagnostic View", FmtI("View %d", l.waterView), l.waterView != 0);
    } else if (tab == 5) {
        AddSlider(rows, "Skin Subsurface Scatter", l.sss, 0.0f, 1.5f, "%.2f");
        AddSlider(rows, "Skin SSS Radius", l.sssWidth, 0.1f, 2.0f, "%.2f");
        AddToggle(rows, "2-Bone Terrain Foot IK", sim.ikFootEnabled);
        AddToggle(rows, "Spine & Head Gaze IK", sim.ikGazeEnabled);
        AddToggle(rows, "240 Hz XPBD Softbody", sim.softbodyEnabled);
        AddSlider(rows, "XPBD Spring Stiffness", sim.physicsStiffness, 0.4f, 2.0f, "%.2fx");
        AddToggle(rows, "Ambient Occlusion", l.ao);
        AddSlider(rows, "AO Strength", l.aoStrength, 0.0f, 2.0f, "%.2f");
        AddToggle(rows, "Screen Contact Shadows", l.contactShadows);
        AddToggle(rows, "Soft Point Shadows", l.softShadows);
    } else if (tab == 6) {
        AddToggle(rows, "Custom RawNavGraph Pathing", sim.navCustomEnabled);
        AddToggle(rows, "Squad Tactical Combat AI", sim.combatAiEnabled);
        AddToggle(rows, "Precision Timed Parry", sim.timedParryEnabled);
        AddSlider(rows, "Timed Parry Window", sim.parryWindowMs, 80.0f, 280.0f, "%.0f ms");
        AddSlider(rows, "Melee Impact Hit-Stop", sim.hitStopMs, 0.0f, 90.0f, "%.0f ms");
        AddSlider(rows, "Actor Poise / Stagger", sim.poiseScale, 0.5f, 2.0f, "%.2fx");
        AddToggle(rows, "Weather-Reactive World AI", sim.livingWorldAi);
        AddCycle(rows, "Runtime Autonomy", "Zero SKSE / Native raw_engine.dll", true);
    } else if (tab == 7) {
        static const char* kCurves[9] = {"Game", "ACES Fitted", "ReinhardW", "RAW Hybrid", "AgX WideGamut", "GranTurismo", "Lottes", "Hable", "ACES 2.0 CAM"};
        static const char* kLuts[14] = {"Neutral", "Nordic Film", "Kodak 2383", "Cinema", "Vision3 500T", "Fuji Velvia", "Tarkovsky", "LomoChrome", "Oil Impasto", "Ink Hatch", "Pixel Bayer", "Watercolor", "Sony PVM", "NTSC VHS"};
        AddToggle(rows, "RAW Tonemap & FXAA", tm.enabled);
        AddCycle(rows, "Display Transform", kCurves[std::clamp(tm.curve, 0, 8)], true);
        AddCycle(rows, "Film Stock Grade", kLuts[std::clamp(int(tm.x[2] + 0.5f), 0, 13)]);
        AddSlider(rows, "Exposure Bias (EV)", tm.bias, -2.0f, 2.0f, "%+.2f");
        AddSlider(rows, "White Balance (Kelvin)", tm.colorTempK, 3500.0f, 9500.0f, "%.0f K");
        AddSlider(rows, "Oklab Saturation", tm.oklabSat, 0.0f, 2.0f, "%.2fx");
        AddSlider(rows, "Shadow Lift", tm.shadowLift, -0.15f, 0.15f, "%+.3f");
        AddSlider(rows, "Highlight Knee", tm.highlightKnee, 0.35f, 1.40f, "%.2f");
        AddCycle(rows, "3D PNG LUT", FmtI("%d/", tm.lutIndex) + FmtI("%d ", int(Tonemap_LutCount())) + Tonemap_LutName(tm.lutIndex));
        AddSlider(rows, "3D LUT Strength", tm.lutStrength, 0.0f, 1.0f, "%.2f");
        AddToggle(rows, "OV1 Film Emulsion", (tm.overlayMask & 1u) != 0u);
        AddToggle(rows, "OV2 Halation & Streak", (tm.overlayMask & 2u) != 0u);
        AddToggle(rows, "OV3 Lens Ghosts", (tm.overlayMask & 4u) != 0u);
        AddToggle(rows, "OV6 Optical Vignette", (tm.overlayMask & 32u) != 0u);
        AddToggle(rows, "OV7 35mm Film Grain", (tm.overlayMask & 64u) != 0u);
        AddToggle(rows, "OV9 RCAS Sharpen", (tm.overlayMask & 256u) != 0u);
    } else if (tab == 8) {
        static const char* kSensors[4] = {"APS-C 24mm", "35mm Full-Frame", "MF 645 44mm", "MF 6x7 70mm"};
        static const char* kShapes[7]  = {"5-Blade", "6-Blade", "7-Blade", "9-Blade", "Circular", "Anamorphic 2x", "Petzval"};
        static const char* kCamModes[6] = {"Cinema Prime", "Tilt-Shift", "ARPG Isometric", "Anamorphic 2.39", "Petzval Swirl", "Vertical Rise"};
        AddToggle(rows, "Physical Thin-Lens DoF", l.dof);
        AddCycle(rows, "Sensor Format", kSensors[std::clamp(l.dofSensor, 0, 3)]);
        AddSlider(rows, "Focal Length", l.dofFocalMm, 24.0f, 200.0f, "%.0f mm");
        AddSlider(rows, "Aperture f-Stop", l.dofFStop, 1.4f, 16.0f, "f/%.1f");
        AddCycle(rows, "Aperture Blade Shape", kShapes[std::clamp(l.dofBokehShape, 0, 6)]);
        AddSlider(rows, "Bokeh Max Radius", l.dofRadius, 2.0f, 18.0f, "%.1f px");
        AddSlider(rows, "Far-Plane Scale", l.dofFarScale, 0.0f, 1.5f, "%.2fx");
        AddSlider(rows, "Near-Plane Scale", l.dofNearScale, 0.0f, 1.5f, "%.2fx");
        AddSlider(rows, "Focus Pull Speed", l.dofFocusSpeed, 0.02f, 0.30f, "%.2f");
        AddCycle(rows, "Camera Perspective", kCamModes[std::clamp(l.cameraMode, 0, 5)], l.cameraMode != 0);
        AddToggle(rows, "Weather Lens Optics", l.weatherLens);
    } else {
        static const char* kTiers[5] = {"Low (Raster)", "Medium (RT Sun)", "High (RT GI)", "Ultra (Full RT)", "Overdrive (Max)"};
        static const char* kDlss[6] = {"Off (FXAA+CAS)", "DLAA Transformer", "DLSS RayRecon", "DLSS Quality", "DLSS Balanced", "DLSS Perf"};
        AddCycle(rows, "Hardware Tier Preset", kTiers[std::clamp(g_hwTier, 0, 4)], true);
        AddCycle(rows, "NVIDIA Streamline", kDlss[std::clamp(raw::bridge::StreamlineHost::Get().GetMode(), 0, 5)]);
        AddToggle(rows, "NVIDIA Reflex Low-Latency", raw::bridge::StreamlineHost::Get().GetReflex());
        AddToggle(rows, "RAW GPU Neural Vibrance", raw::bridge::StreamlineHost::Get().GetDeepDvc());
        AddCycle(rows, "User Preset Slot", FmtI("Slot %d  (S=Save | L=Load)", g_presetSlot), true);
        AddToggle(rows, "Studio Dark Ground", sim.darkTheme);
        AddToggle(rows, "Reduced Motion Mode", sim.reducedMotion);
    }
    return rows;
}

void ApplyRowEdit(int tab, int row, int dir, bool hasDirectNorm, float directNorm, float fineMult)
{
    auto& l = hook::Cfg().light;
    auto tm = Tonemap_Get();
    auto& sim = raw::ui::GetWorldSimTuning();
    bool tmDirty = false;
    const float step = float(dir) * fineMult;

    auto setF = [&](float& v, float lo, float hi, float delta) {
        v = hasDirectNorm ? std::clamp(lo + directNorm * (hi - lo), lo, hi)
                          : std::clamp(v + step * delta, lo, hi);
    };

    if (tab == 0) {
        switch (row) {
        case 0: l.rtShadows = !l.rtShadows; break;
        case 1: l.rtSky = !l.rtSky; break;
        case 2: l.rtReflect = !l.rtReflect; break;
        case 3: l.rtOmm = !l.rtOmm; break;
        case 4: l.rtFar = !l.rtFar; break;
        case 5: l.rtSkin = !l.rtSkin; break;
        case 6: l.rtRest = !l.rtRest; break;
        case 7: setF(l.rtBounce, 0.0f, 3.0f, 0.10f); break;
        case 8: setF(l.rtBounceInterior, 0.0f, 3.0f, 0.10f); break;
        case 9: setF(l.rtSkyStrength, 0.0f, 2.0f, 0.05f); break;
        case 10: setF(l.rtSkyLength, 100.0f, 4000.0f, 150.0f); break;
        case 11: l.rawlss5 = !l.rawlss5; break;
        case 12: {
            static const int kDbgVals[9] = {0, 1, 2, 12, 20, 21, 22, 23, 24};
            int di = 0; for (int k = 0; k < 9; ++k) if (kDbgVals[k] == l.view) { di = k; break; }
            l.view = kDbgVals[(di + 9 + (dir ? dir : 1)) % 9];
            break;
        }
        }
    } else if (tab == 1) {
        switch (row) {
        case 0: sim.geomSubdivLevel = (sim.geomSubdivLevel + 3 + (dir ? dir : 1)) % 3; break;
        case 1: setF(sim.geomCurvature, 0.0f, 1.25f, 0.05f); break;
        case 2: setF(sim.geomDisplaceScale, 0.0f, 2.0f, 0.10f); break;
        case 3: l.distantLod = !l.distantLod; break;
        case 4: sim.geomWireframe = !sim.geomWireframe; l.view = sim.geomWireframe ? 22 : 0; break;
        case 5: l.envMap = !l.envMap; break;
        case 6: l.ggxSpecular = !l.ggxSpecular; break;
        }
    } else if (tab == 2) {
        switch (row) {
        case 0: l.clouds = !l.clouds; break;
        case 1: setF(l.cloudCover, 0.0f, 1.0f, 0.05f); break;
        case 2: setF(l.cloudDensity, 0.1f, 2.5f, 0.10f); break;
        case 3: setF(l.cloudShadow, 0.0f, 1.0f, 0.05f); break;
        case 4: setF(l.cloudWindX, -600.0f, 600.0f, 40.0f); break;
        case 5: setF(l.cloudWindY, -600.0f, 600.0f, 40.0f); break;
        case 6: l.volumetricFog = !l.volumetricFog; break;
        case 7: l.fogReplacesGame = !l.fogReplacesGame; break;
        case 8: setF(l.fogDensity, 0.0f, 0.0005f, 0.00002f); break;
        case 9: setF(l.fogAnisotropy, 0.0f, 0.90f, 0.05f); break;
        case 10: setF(l.fogSunScale, 0.0f, 3.0f, 0.15f); break;
        }
    } else if (tab == 3) {
        const int d = dir ? dir : 1;
        switch (row) {
        case 0: l.season = (l.season + 5 + d) % 5; break;
        case 1: sim.snowVolumetric = !sim.snowVolumetric; break;
        case 2: setF(sim.snowMaxDepthU, 4.0f, 48.0f, 2.0f); break;
        case 3: setF(sim.snowAccumRate, 0.0f, 3.0f, 0.15f); break;
        case 4: setF(sim.snowAvalancheAng, 0.20f, 0.75f, 0.04f); break;
        case 5: g_menuSnow = g_menuSnow < 0.0f ? (d > 0 ? 0.5f : 1.0f) : (g_menuSnow + d * 0.25f < 0.0f ? -1.0f : std::min(1.0f, g_menuSnow + d * 0.25f));
                WeatherDrive_SetTest(g_menuRain, g_menuSnow); break;
        case 6: g_menuRain = g_menuRain < 0.0f ? (d > 0 ? 0.5f : 1.0f) : (g_menuRain + d * 0.25f < 0.0f ? -1.0f : std::min(1.0f, g_menuRain + d * 0.25f));
                WeatherDrive_SetTest(g_menuRain, g_menuSnow); break;
        case 7: setF(l.wetness, 0.0f, 1.0f, 0.08f); break;
        case 8: setF(l.puddles, 0.0f, 1.0f, 0.08f); break;
        case 9: setF(l.snow, 0.0f, 1.0f, 0.08f); break;
        case 10: setF(l.rain, 0.0f, 1.0f, 0.08f); break;
        }
    } else if (tab == 4) {
        switch (row) {
        case 0: l.water = !l.water; break;
        case 1: setF(l.waterReflect, 0.0f, 1.5f, 0.08f); break;
        case 2: setF(l.waterCaustics, 0.0f, 3.0f, 0.10f); break;
        case 3: setF(l.waterAbsorb, 0.001f, 0.04f, 0.002f); break;
        case 4: setF(l.waterGlint, 0.0f, 3.0f, 0.10f); break;
        case 5: setF(l.waterRefract, 0.0f, 1.0f, 0.05f); break;
        case 6: l.waterView = (l.waterView + 6 + (dir ? dir : 1)) % 6; break;
        }
    } else if (tab == 5) {
        switch (row) {
        case 0: setF(l.sss, 0.0f, 1.5f, 0.10f); break;
        case 1: setF(l.sssWidth, 0.1f, 2.0f, 0.10f); break;
        case 2: sim.ikFootEnabled = !sim.ikFootEnabled; break;
        case 3: sim.ikGazeEnabled = !sim.ikGazeEnabled; break;
        case 4: sim.softbodyEnabled = !sim.softbodyEnabled; break;
        case 5: setF(sim.physicsStiffness, 0.4f, 2.0f, 0.10f); break;
        case 6: l.ao = !l.ao; break;
        case 7: setF(l.aoStrength, 0.0f, 2.0f, 0.08f); break;
        case 8: l.contactShadows = !l.contactShadows; break;
        case 9: l.softShadows = !l.softShadows; break;
        }
    } else if (tab == 6) {
        switch (row) {
        case 0: sim.navCustomEnabled = !sim.navCustomEnabled; break;
        case 1: sim.combatAiEnabled = !sim.combatAiEnabled; break;
        case 2: sim.timedParryEnabled = !sim.timedParryEnabled; break;
        case 3: setF(sim.parryWindowMs, 80.0f, 280.0f, 10.0f); break;
        case 4: setF(sim.hitStopMs, 0.0f, 90.0f, 5.0f); break;
        case 5: setF(sim.poiseScale, 0.5f, 2.0f, 0.10f); break;
        case 6: sim.livingWorldAi = !sim.livingWorldAi; break;
        }
    } else if (tab == 7) {
        const int d = dir ? dir : 1;
        const int lutN = int(Tonemap_LutCount()) + 1;
        switch (row) {
        case 0: tm.enabled = !tm.enabled; tmDirty = true; break;
        case 1: tm.curve = (tm.curve + 9 + d) % 9; tmDirty = true; break;
        case 2: tm.x[2] = float((std::clamp(int(tm.x[2] + 0.5f), 0, 13) + 14 + d) % 14); tmDirty = true; break;
        case 3: setF(tm.bias, -2.0f, 2.0f, 0.10f); tmDirty = true; break;
        case 4: setF(tm.colorTempK, 3500.0f, 9500.0f, 150.0f); tmDirty = true; break;
        case 5: setF(tm.oklabSat, 0.0f, 2.0f, 0.05f); tmDirty = true; break;
        case 6: setF(tm.shadowLift, -0.15f, 0.15f, 0.01f); tmDirty = true; break;
        case 7: setF(tm.highlightKnee, 0.35f, 1.40f, 0.04f); tmDirty = true; break;
        case 8: tm.lutIndex = (tm.lutIndex + lutN + d) % lutN; tmDirty = true; break;
        case 9: setF(tm.lutStrength, 0.0f, 1.0f, 0.05f); tmDirty = true; break;
        case 10: tm.overlayMask ^= 1u; tmDirty = true; break;
        case 11: tm.overlayMask ^= 2u; tmDirty = true; break;
        case 12: tm.overlayMask ^= 4u; tmDirty = true; break;
        case 13: tm.overlayMask ^= 32u; tmDirty = true; break;
        case 14: tm.overlayMask ^= 64u; tmDirty = true; break;
        case 15: tm.overlayMask ^= 256u; tmDirty = true; break;
        }
    } else if (tab == 8) {
        const int d = dir ? dir : 1;
        switch (row) {
        case 0: l.dof = !l.dof; break;
        case 1: l.dofSensor = (l.dofSensor + 4 + d) % 4; break;
        case 2: setF(l.dofFocalMm, 24.0f, 200.0f, 5.0f); break;
        case 3: setF(l.dofFStop, 1.4f, 16.0f, 0.2f); break;
        case 4: l.dofBokehShape = (l.dofBokehShape + 7 + d) % 7; break;
        case 5: setF(l.dofRadius, 2.0f, 18.0f, 0.5f); break;
        case 6: setF(l.dofFarScale, 0.0f, 1.5f, 0.08f); break;
        case 7: setF(l.dofNearScale, 0.0f, 1.5f, 0.08f); break;
        case 8: setF(l.dofFocusSpeed, 0.02f, 0.30f, 0.02f); break;
        case 9: l.cameraMode = (l.cameraMode + 6 + d) % 6; break;
        case 10: l.weatherLens = !l.weatherLens; break;
        }
    } else {
        const int d = dir ? dir : 1;
        switch (row) {
        case 0: g_hwTier = (g_hwTier + 5 + d) % 5; ApplyHardwareTier(g_hwTier); break;
        case 1: raw::bridge::StreamlineHost::Get().SetMode((raw::bridge::StreamlineHost::Get().GetMode() + 6 + d) % 6); break;
        case 2: raw::bridge::StreamlineHost::Get().SetReflex(!raw::bridge::StreamlineHost::Get().GetReflex()); break;
        case 3: raw::bridge::StreamlineHost::Get().SetDeepDvc(!raw::bridge::StreamlineHost::Get().GetDeepDvc()); break;
        case 4: g_presetSlot = ((g_presetSlot - 1 + 5 + d) % 5) + 1; break;
        case 5: sim.darkTheme = !sim.darkTheme; break;
        case 6: sim.reducedMotion = !sim.reducedMotion; break;
        }
    }
    if (tmDirty) Tonemap_Set(tm);
    hook::LightingApplyTuning();
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

extern "C" __declspec(dllexport) raw::ui::WorldSimTuning* PG_GetWorldSimTuning()
{
    return &raw::ui::GetWorldSimTuning();
}

extern "C" __declspec(dllexport) void PG_GetCombatInput(uint32_t* outFlags)
{
    if (!outFlags) return;
    uint32_t f = 0;
    if (g_lastInput.combatBlockHeld) f |= 1u;
    if (g_lastInput.combatBlockEdge) f |= 2u;
    if (g_lastInput.combatDodgeEdge) f |= 4u;
    if (g_lastInput.combatHeavyEdge) f |= 8u;
    if (g_lastInput.quickWheelHeld)  f |= 16u;
    *outFlags = f;
}

void Overlay_Draw()
{
    // Frame-time EMA update
    float dtSec = 0.016f;
    if (!g_qpcFreq.QuadPart) {
        QueryPerformanceFrequency(&g_qpcFreq);
        QueryPerformanceCounter(&g_qpcLast);
    } else {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const float ms = float(double(now.QuadPart - g_qpcLast.QuadPart) * 1000.0 / double(g_qpcFreq.QuadPart));
        g_qpcLast = now;
        if (ms > 0.5f && ms < 250.0f) {
            g_emaMs = g_emaMs * 0.90f + ms * 0.10f;
            dtSec = ms * 0.001f;
        }
    }
    if (g_savedFrames > 0) --g_savedFrames;

    EnsureWndProcHook();
    g_lastInput = g_input.Poll(g_hookHwnd, g_menuOpen, dtSec);
    if (g_lastInput.toggleStudio) { g_menuOpen = !g_menuOpen; }
    if (g_menuOpen && g_lastInput.back) { g_menuOpen = false; }

    std::string bannerText;
    bool bannerDirty = false;
    {
        std::lock_guard<std::mutex> g(g_m);
        bannerText = g_text;
        bannerDirty = g_dirty;
        g_dirty = false;
    }

    const bool drawStudio = bannerText.empty() && g_menuOpen;
    if (!drawStudio && bannerText.empty()) return;

    auto* pi = PG_GetProxyInterface();
    if (!pi || !pi->device || !pi->context || !pi->swapChain) return;
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(pi->swapChain->GetBuffer(0, IID_PPV_ARGS(&bb))) || !bb) return;
    D3D11_TEXTURE2D_DESC bd{};
    bb->GetDesc(&bd);
    const bool bgra = bd.Format == DXGI_FORMAT_B8G8R8A8_UNORM || bd.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    const bool rgba = bd.Format == DXGI_FORMAT_R8G8B8A8_UNORM || bd.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    if ((!bgra && !rgba) || bd.SampleDesc.Count != 1) { bb->Release(); return; }

    const uint32_t maxW = bd.Width > 32 ? bd.Width - 32 : bd.Width;
    const uint32_t maxH = bd.Height > 32 ? bd.Height - 32 : bd.Height;
    const int32_t panelX = 16, panelY = 16;

    std::vector<uint8_t> px;
    uint32_t w = 0, h = 0;
    bool needUpload = false;

    if (drawStudio) {
        auto rows = BuildRows(g_tab);
        const int nRows = int(rows.size());
        auto& sim = raw::ui::GetWorldSimTuning();

        // Hit-test mouse input against stable landmarks
        const auto hit = g_studio.HitTest(panelX, panelY, maxW, maxH, g_lastInput, nRows, g_scroll);
        if (hit.toggleThemeClicked || g_lastInput.toggleTheme) sim.darkTheme = !sim.darkTheme;
        if (hit.toggleMotionClicked) sim.reducedMotion = !sim.reducedMotion;
        if (hit.saveClicked || g_lastInput.save) SaveCurrentConfig();
        if (g_lastInput.load) LoadPresetSlot(g_presetSlot);
        if (hit.exportClicked || g_lastInput.exportRecipe) ExportCameraRecipe();

        if (hit.clickedTab >= 0) { g_tab = hit.clickedTab; g_sel = 0; g_scroll = 0; }
        if (g_lastInput.nextTab) { g_tab = (g_tab + 1) % raw::ui::RawStudioUi::kDomainCount; g_sel = 0; g_scroll = 0; }
        if (g_lastInput.prevTab) { g_tab = (g_tab + raw::ui::RawStudioUi::kDomainCount - 1) % raw::ui::RawStudioUi::kDomainCount; g_sel = 0; g_scroll = 0; }

        rows = BuildRows(g_tab);
        const int curRows = int(rows.size());
        g_sel = std::clamp(g_sel, 0, std::max(curRows - 1, 0));

        if (g_lastInput.wheelDelta != 0) {
            g_scroll = std::clamp(g_scroll - g_lastInput.wheelDelta, 0, std::max(0, curRows - 14));
        }
        if (g_lastInput.up && curRows > 0)   g_sel = (g_sel + curRows - 1) % curRows;
        if (g_lastInput.down && curRows > 0) g_sel = (g_sel + 1) % curRows;

        const float fine = g_lastInput.fineMod ? 0.25f : (g_lastInput.coarseMod ? 2.5f : 1.0f);
        if (hit.sliderDragged && hit.clickedRow >= 0 && hit.clickedRow < curRows) {
            g_sel = hit.clickedRow;
            if (rows[size_t(g_sel)].kind == raw::ui::RowKind::Slider) {
                ApplyRowEdit(g_tab, g_sel, 0, true, hit.sliderNorm, 1.0f);
            }
        } else if (hit.clickedRow >= 0 && hit.clickedRow < curRows) {
            g_sel = hit.clickedRow;
            ApplyRowEdit(g_tab, g_sel, 1, false, 0.0f, fine);
        } else if (g_lastInput.left) {
            ApplyRowEdit(g_tab, g_sel, -1, false, 0.0f, fine);
        } else if (g_lastInput.right || g_lastInput.confirm) {
            ApplyRowEdit(g_tab, g_sel, 1, false, 0.0f, fine);
        }
        if (hit.wheelClicked) {
            auto tm = Tonemap_Get();
            tm.colorTempK = hit.wheelTempK;
            tm.oklabSat = hit.wheelSat;
            Tonemap_Set(tm);
        }

        // Refresh rows after edits and build live telemetry snapshot
        rows = BuildRows(g_tab);
        const auto& l = hook::Cfg().light;
        const auto tm = Tonemap_Get();
        raw::ui::StudioTelemetry tel{};
        tel.fps = g_emaMs > 0.1f ? 1000.0f / g_emaMs : 0.0f;
        tel.frameMs = g_emaMs;
        tel.bridgeReady = hook::Bridge() && hook::Bridge()->Ready();
        tel.sm69Active = true;
        tel.rawlssActive = l.rawlss5;
        tel.rtActive = l.rtShadows;
        tel.hwTier = g_hwTier;
        tel.statusMessage = g_savedFrames ? g_statusTag : std::string();
        tel.colorTempK = tm.colorTempK;
        tel.oklabSat = tm.oklabSat;
        tel.dofFocalMm = l.dofFocalMm;
        tel.dofFStop = l.dofFStop;
        tel.dofBokehShape = l.dofBokehShape;
        tel.exposureBias = tm.bias;
        tel.highlightKnee = tm.highlightKnee;
        tel.shadowLift = tm.shadowLift;
        tel.snowDepthAvgU = l.snow * sim.snowMaxDepthU;

        needUpload = g_studio.RenderStudio(maxW, maxH, g_tab, g_sel, hit.hoveredRow, hit.hoveredTab, g_scroll,
                                           rows, tel, sim, g_lastInput, panelX, panelY, g_input.FooterHint(), px, w, h);
    } else if (bannerDirty || !g_tex || g_fmt != bd.Format) {
        needUpload = g_studio.RenderBanner(bannerText, maxW, raw::ui::GetWorldSimTuning().darkTheme, px, w, h);
    }

    if (needUpload && !px.empty()) {
        if (rgba) {
            for (size_t i = 0; i < px.size(); i += 4) std::swap(px[i], px[i + 2]);
        }
        if (g_tex && (g_w != w || g_h != h || g_fmt != bd.Format)) {
            g_tex->Release(); g_tex = nullptr;
        }
        if (!g_tex) {
            D3D11_TEXTURE2D_DESC d{};
            d.Width = w; d.Height = std::min(h, maxH); d.MipLevels = 1; d.ArraySize = 1; d.Format = bd.Format;
            d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
            D3D11_SUBRESOURCE_DATA init{px.data(), w * 4, 0};
            if (SUCCEEDED(pi->device->CreateTexture2D(&d, &init, &g_tex))) { g_w = d.Width; g_h = d.Height; g_fmt = bd.Format; }
            else Log("[Overlay] studio texture %ux%u failed", w, h);
        } else {
            D3D11_BOX box{0, 0, 0, g_w, g_h, 1};
            pi->context->UpdateSubresource(g_tex, 0, &box, px.data(), w * 4, 0);
        }
    }
    if (g_tex) pi->context->CopySubresourceRegion(bb, 0, UINT(panelX), UINT(panelY), 0, g_tex, 0, nullptr);
    bb->Release();
}

} // namespace SB::Proxy
