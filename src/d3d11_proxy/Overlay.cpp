// Overlay: see the header. The text is laid out by GDI into a 32-bit bitmap once per change,
// uploaded into a texture of the backbuffer's format, and copied over the top-left of the frame
// before Present: no shader, so it shows even when the bridge's own pipelines failed to build.
// Original work, MIT.
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

// Interactive in-game RAW DX12 Menu (toggled by End or Shift+Enter, tuned with Arrow keys).
bool g_menuOpen = false;
int g_menuSel = 0;
float g_menuSnow = -1.0f, g_menuRain = -1.0f;
bool g_prevToggle = false, g_prevUp = false, g_prevDown = false, g_prevLeft = false, g_prevRight = false;

bool KeyEdge(bool now, bool& prev)
{
    const bool fired = now && !prev;
    prev = now;
    return fired;
}

std::string BuildMenuText()
{
    const auto& l = hook::Cfg().light;
    static const char* kSeasons[5] = {"Auto", "Spring", "Summer", "Autumn", "Winter"};
    const int sIdx = std::clamp(l.season, 0, 4);
    char buf[2048];
    const auto row = [&](int idx, const char* name, const char* val) {
        char line[128];
        std::snprintf(line, sizeof line, "%s %-20s : %s\n", g_menuSel == idx ? ">>" : "  ", name, val);
        return std::string(line);
    };
    std::string out = "=== RAW DX12 IN-GAME MENU [End / Shift+Enter: Close | Up/Down: Select | Left/Right: Tune] ===\n";
    std::snprintf(buf, sizeof buf, "%d (%s)", sIdx, kSeasons[sIdx]); out += row(0, "Season", buf);
    std::snprintf(buf, sizeof buf, "%d", l.view); out += row(1, "Debug View (0..21)", buf);
    out += row(2, "RT Shadows", l.rtShadows ? "ON" : "OFF");
    out += row(3, "RT Sky / RTAO", l.rtSky ? "ON" : "OFF");
    out += row(4, "RT Reflections", l.rtReflect ? "ON" : "OFF");
    out += row(5, "Volumetric Clouds", l.clouds ? "ON" : "OFF");
    out += row(6, "Volumetric Fog", l.volumetricFog ? "ON" : "OFF");
    std::snprintf(buf, sizeof buf, "%.2f", l.cloudCover); out += row(7, "Cloud Cover", buf);
    std::snprintf(buf, sizeof buf, "%.2f", l.cloudDensity); out += row(8, "Cloud Density", buf);
    std::snprintf(buf, sizeof buf, "%.2f", l.rtBounce); out += row(9, "RT GI Bounce", buf);
    std::snprintf(buf, sizeof buf, "%.2f", l.rtBounceInterior); out += row(10, "RT GI Interior", buf);
    std::snprintf(buf, sizeof buf, "%.2f", l.rtSkyStrength); out += row(11, "RT Sky Strength", buf);
    if (g_menuSnow < 0.0f) std::snprintf(buf, sizeof buf, "Auto"); else std::snprintf(buf, sizeof buf, "%.0f%%", g_menuSnow * 100.0f);
    out += row(12, "Force Snow", buf);
    if (g_menuRain < 0.0f) std::snprintf(buf, sizeof buf, "Auto"); else std::snprintf(buf, sizeof buf, "%.0f%%", g_menuRain * 100.0f);
    out += row(13, "Force Rain", buf);
    const auto tm = Tonemap_Get();
    static const char* kLuts[4] = {"Neutral", "Nordic Film", "Kodak 2383", "Cinema"};
    const int lutIdx = std::clamp(int(tm.x[2] + 0.5f), 0, 3);
    std::snprintf(buf, sizeof buf, "%d (%s)", lutIdx, kLuts[lutIdx]); out += row(14, "Color Grade LUT", buf);
    std::snprintf(buf, sizeof buf, "%s", tm.view >= 3 ? "HDR10 PQ Rec.2020" : "SDR Rec.709"); out += row(15, "Display Output", buf);
    std::snprintf(buf, sizeof buf, "%.2f", tm.key); out += row(16, "Exposure Key", buf);
    std::snprintf(buf, sizeof buf, "%.2f", tm.x[1]); out += row(17, "White Point (X1)", buf);
    std::snprintf(buf, sizeof buf, "%.2f", tm.x[3]); out += row(18, "Highlight Contrast", buf);
    std::snprintf(buf, sizeof buf, "%.2f", tm.x[4]); out += row(19, "Shadow Lift (X4)", buf);
    out += "Full Web Studio & HLSL Editor: http://127.0.0.1:8844";
    return out;
}

bool PollMenu(std::string& outText)
{
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool enter = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    const bool endKey = (GetAsyncKeyState(VK_END) & 0x8000) != 0;
    bool changed = false;
    if (KeyEdge(endKey || (shift && enter), g_prevToggle)) {
        g_menuOpen = !g_menuOpen;
        changed = true;
    }
    if (!g_menuOpen) return false;
    constexpr int kRows = 20;
    if (KeyEdge((GetAsyncKeyState(VK_UP) & 0x8000) != 0, g_prevUp)) { g_menuSel = (g_menuSel + kRows - 1) % kRows; changed = true; }
    if (KeyEdge((GetAsyncKeyState(VK_DOWN) & 0x8000) != 0, g_prevDown)) { g_menuSel = (g_menuSel + 1) % kRows; changed = true; }
    int dir = 0;
    if (KeyEdge((GetAsyncKeyState(VK_LEFT) & 0x8000) != 0, g_prevLeft)) dir = -1;
    if (KeyEdge((GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0, g_prevRight)) dir = 1;
    if (dir != 0) {
        auto& l = hook::Cfg().light;
        auto tm = Tonemap_Get();
        bool tmDirty = false;
        switch (g_menuSel) {
        case 0: l.season = (l.season + 5 + dir) % 5; break;
        case 1: l.view = (l.view + 22 + dir) % 22; break;
        case 2: l.rtShadows = !l.rtShadows; break;
        case 3: l.rtSky = !l.rtSky; break;
        case 4: l.rtReflect = !l.rtReflect; break;
        case 5: l.clouds = !l.clouds; break;
        case 6: l.volumetricFog = !l.volumetricFog; break;
        case 7: l.cloudCover = std::clamp(l.cloudCover + dir * 0.05f, 0.0f, 1.0f); break;
        case 8: l.cloudDensity = std::clamp(l.cloudDensity + dir * 0.10f, 0.2f, 2.0f); break;
        case 9: l.rtBounce = std::clamp(l.rtBounce + dir * 0.10f, 0.0f, 2.5f); break;
        case 10: l.rtBounceInterior = std::clamp(l.rtBounceInterior + dir * 0.10f, 0.0f, 2.5f); break;
        case 11: l.rtSkyStrength = std::clamp(l.rtSkyStrength + dir * 0.05f, 0.0f, 1.5f); break;
        case 12: g_menuSnow = g_menuSnow < 0.0f ? (dir > 0 ? 0.5f : 1.0f) : (g_menuSnow + dir * 0.25f < 0.0f ? -1.0f : std::min(1.0f, g_menuSnow + dir * 0.25f));
                 WeatherDrive_SetTest(g_menuRain, g_menuSnow); break;
        case 13: g_menuRain = g_menuRain < 0.0f ? (dir > 0 ? 0.5f : 1.0f) : (g_menuRain + dir * 0.25f < 0.0f ? -1.0f : std::min(1.0f, g_menuRain + dir * 0.25f));
                 WeatherDrive_SetTest(g_menuRain, g_menuSnow); break;
        case 14: tm.x[2] = float((std::clamp(int(tm.x[2] + 0.5f), 0, 3) + 4 + dir) % 4); tmDirty = true; break;
        case 15: tm.view = tm.view >= 3 ? 0 : 3; tmDirty = true; break;
        case 16: tm.key = std::clamp(tm.key + dir * 0.02f, 0.18f, 0.75f); tmDirty = true; break;
        case 17: tm.x[1] = std::clamp(tm.x[1] + dir * 0.25f, 1.5f, 8.0f); tmDirty = true; break;
        case 18: tm.x[3] = std::clamp(tm.x[3] + dir * 0.05f, 0.80f, 1.90f); tmDirty = true; break;
        case 19: tm.x[4] = std::clamp(tm.x[4] + dir * 0.05f, 0.30f, 1.00f); tmDirty = true; break;
        }
        if (tmDirty) Tonemap_Set(tm);
        hook::LightingApplyTuning();
        changed = true;
    }
    outText = BuildMenuText();
    return changed;
}

// Consolas 16 px, dark slate panel for interactive menu or dark red for error banners.
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
    w = std::min<UINT>(maxW, isMenu ? 860 : 1400);
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
    HBRUSH bg = CreateSolidBrush(isMenu ? RGB(16, 22, 32) : RGB(90, 10, 10));
    FillRect(dc, &all, bg);
    SetBkMode(dc, TRANSPARENT);
    for (size_t i = 0; i < lines.size(); ++i) {
        if (isMenu && i == 0) SetTextColor(dc, RGB(66, 212, 196));
        else if (isMenu && lines[i].rfind(">>", 0) == 0) SetTextColor(dc, RGB(245, 190, 88));
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
