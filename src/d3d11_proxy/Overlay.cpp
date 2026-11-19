// Overlay: see the header. The text is laid out by GDI into a 32-bit bitmap once per change,
// uploaded into a texture of the backbuffer's format, and copied over the top-left of the frame
// before Present: no shader, so it shows even when the bridge's own pipelines failed to build.
// Original work, MIT.
#include "Overlay.h"
#include "ProxyAPI.h"
#include "ProxyLog.h"

#include <d3d11.h>
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

// White text on dark red, Consolas 16 px, at most 40 lines of 150 characters.
bool Render(const std::string& text, UINT maxW, std::vector<uint8_t>& px, UINT& w, UINT& h)
{
    std::vector<std::string> lines;
    for (size_t at = 0; at <= text.size() && lines.size() < 40;) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        lines.push_back(text.substr(at, std::min<size_t>(end - at, 150)));
        at = end + 1;
    }
    const int lineH = 18;
    w = std::min<UINT>(maxW, 1400);
    h = UINT(lines.size() * lineH + 16);
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
    HFONT font = CreateFontA(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");
    HGDIOBJ oldFont = SelectObject(dc, font);
    RECT all{0, 0, LONG(w), LONG(h)};
    HBRUSH bg = CreateSolidBrush(RGB(90, 10, 10));
    FillRect(dc, &all, bg);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    for (size_t i = 0; i < lines.size(); ++i) TextOutA(dc, 8, int(8 + i * lineH), lines[i].c_str(), int(lines[i].size()));
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
    bool dirty;
    {
        std::lock_guard<std::mutex> g(g_m);
        text = g_text;
        dirty = g_dirty;
        g_dirty = false;
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
        if (Render(text, bd.Width - 32, px, w, h)) {
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
