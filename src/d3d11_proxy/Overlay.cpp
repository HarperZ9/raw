// Overlay: interactive 6-tab ENB-class in-game RAW DX12 Control Panel (toggled by End or Shift+Enter)
// and error banner, rendered via GDI into a 32-bit backbuffer-format texture before Present. Original work, MIT.
#include "Overlay.h"
#include "BridgeHookState.h"
#include "ProxyAPI.h"
#include "ProxyLog.h"
#include "Tonemap.h"
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

// 6-tab, 60-control interactive in-game RAW DX12 Control Panel.
bool g_menuOpen = false;
int g_tab = 0;
int g_sel = 0;
float g_menuSnow = -1.0f, g_menuRain = -1.0f;
bool g_prevToggle = false, g_prevTab = false, g_prevUp = false, g_prevDown = false, g_prevLeft = false, g_prevRight = false;

bool KeyEdge(bool now, bool& prev) { const bool f = now && !prev; prev = now; return f; }

int TabRowCount(int tab)
{
    static const int kCounts[6] = {12, 13, 7, 8, 12, 11};
    return kCounts[std::clamp(tab, 0, 5)];
}

std::string Fmt(const char* f, float v) { char b[64]; std::snprintf(b, sizeof b, f, v); return b; }
std::string FmtI(const char* f, int v) { char b[64]; std::snprintf(b, sizeof b, f, v); return b; }
std::string OnOff(bool b) { return b ? "ON" : "OFF"; }

std::string BuildMenuText()
{
    const auto& l = hook::Cfg().light;
    const auto tm = Tonemap_Get();
    static const char* kTabs[6] = {"1:RT & GI", "2:Sky/Cloud/Fog", "3:Weather/Season", "4:Water", "5:Skin/Shadow/AO", "6:Tonemap/Debug"};
    std::string out = "=== RAW DX12 STUDIO [End/Shift+Enter:Close | Tab:Page | Up/Dn:Sel | L/R:Tune | Shift+L/R:Fine] ===\n";
    for (int i = 0; i < 6; ++i) {
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
    } else {
        static const char* kLuts[4] = {"Neutral", "Nordic Film", "Kodak 2383", "Cinema"};
        const int li = std::clamp(int(tm.x[2] + 0.5f), 0, 3);
        row(0, "RAW Tonemap + FXAA", OnOff(tm.enabled));
        row(1, "Color Grade LUT", FmtI("%d (", li) + kLuts[li] + ")");
        row(2, "Display Output", tm.view >= 3 ? "HDR10 PQ Rec.2020" : "SDR Rec.709");
        row(3, "Exposure Bias (EV)", Fmt("%+.2f", tm.bias));
        row(4, "Exposure Key", Fmt("%.2f", tm.key));
        row(5, "Adaptation Speed", Fmt("%.2f", tm.adapt));
        row(6, "White Point (X1)", Fmt("%.2f", tm.x[1]));
        row(7, "Highlight Contrast", Fmt("%.2f", tm.x[3]));
        row(8, "Shadow Lift (X4)", Fmt("%.2f", tm.x[4]));
        row(9, "Interior Factor (X5)", Fmt("%.2f", tm.x[5]));
        row(10, "Debug View (0..23)", FmtI("%d", l.view));
    }
    out += "Web Studio, Presets & Live HLSL Editor: http://127.0.0.1:8844";
    return out;
}

bool PollMenu(std::string& outText)
{
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool enter = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    const bool endKey = (GetAsyncKeyState(VK_END) & 0x8000) != 0;
    bool changed = false;
    if (KeyEdge(endKey || (shift && enter), g_prevToggle)) { g_menuOpen = !g_menuOpen; changed = true; }
    if (!g_menuOpen) return false;
    if (KeyEdge((GetAsyncKeyState(VK_TAB) & 0x8000) != 0, g_prevTab)) {
        g_tab = (g_tab + (shift ? 5 : 1)) % 6;
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
        } else {
            switch (g_sel) {
            case 0: tm.enabled = !tm.enabled; tmDirty = true; break;
            case 1: tm.x[2] = float((std::clamp(int(tm.x[2] + 0.5f), 0, 3) + 4 + dir) % 4); tmDirty = true; break;
            case 2: tm.view = tm.view >= 3 ? 0 : 3; tmDirty = true; break;
            case 3: tm.bias = std::clamp(tm.bias + step * 0.10f, -2.0f, 2.0f); tmDirty = true; break;
            case 4: tm.key = std::clamp(tm.key + step * 0.02f, 0.15f, 0.85f); tmDirty = true; break;
            case 5: tm.adapt = std::clamp(tm.adapt + step * 0.05f, 0.0f, 1.0f); tmDirty = true; break;
            case 6: tm.x[1] = std::clamp(tm.x[1] + step * 0.25f, 1.5f, 10.0f); tmDirty = true; break;
            case 7: tm.x[3] = std::clamp(tm.x[3] + step * 0.05f, 0.80f, 2.00f); tmDirty = true; break;
            case 8: tm.x[4] = std::clamp(tm.x[4] + step * 0.05f, 0.25f, 1.00f); tmDirty = true; break;
            case 9: tm.x[5] = std::clamp(tm.x[5] + step * 0.05f, 0.0f, 1.50f); tmDirty = true; break;
            case 10: l.view = (l.view + 24 + dir) % 24; break;
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
    w = std::min<UINT>(maxW, isMenu ? 940 : 1400);
    h = UINT(lines.size() * lineH + 18);
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
    HGDIOBJ oldFont = SelectObject(dc, font);
    RECT all{0, 0, LONG(w), LONG(h)};
    HBRUSH bg = CreateSolidBrush(isMenu ? RGB(14, 19, 28) : RGB(90, 10, 10));
    FillRect(dc, &all, bg);
    SetBkMode(dc, TRANSPARENT);
    for (size_t i = 0; i < lines.size(); ++i) {
        if (isMenu && i <= 1) SetTextColor(dc, i == 0 ? RGB(66, 212, 196) : RGB(150, 210, 245));
        else if (isMenu && lines[i].rfind(">>", 0) == 0) SetTextColor(dc, RGB(248, 196, 86));
        else SetTextColor(dc, RGB(236, 234, 226));
        TextOutA(dc, 10, int(9 + i * lineH), lines[i].c_str(), int(lines[i].size()));
    }
    GdiFlush();
    px.assign(static_cast<uint8_t*>(bits), static_cast<uint8_t*>(bits) + size_t(w) * h * 4);
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(font);
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
