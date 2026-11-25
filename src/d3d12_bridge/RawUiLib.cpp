// RawUiLib.cpp: Clean-Room Zero-ImGui DX12 GPU-Native UI Library & Debug-Layer Legend System.
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawUiLib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#if __has_include("raw_uilib_cs.h")
#define RAW_UILIB_HAS_EMBEDDED_DXIL 1
#include "raw_uilib_cs.h"
#else
#define RAW_UILIB_HAS_EMBEDDED_DXIL 0
#endif

namespace raw::ui
{
namespace
{
using Microsoft::WRL::ComPtr;

// GPU constant buffer matching RawUiLibPass.hlsl byte-for-byte (256-byte aligned).
struct alignas(256) GpuUiLibCB {
    uint32_t dirtyRect[4];  // x, y, w, h
    uint32_t targetInfo[4]; // texW, texH, cmdCount, flags (bit0 = srgbOutput)
    uint32_t reserved0[4];
    uint32_t reserved1[4];
    uint32_t _pad[48];
};
static_assert(sizeof(GpuUiLibCB) == 256u, "GpuUiLibCB must be 256 bytes");

bool CreateUploadOrReadbackBuffer(ID3D12Device* dev, uint64_t bytes, D3D12_HEAP_TYPE heapType,
                                  D3D12_RESOURCE_STATES initState, ComPtr<ID3D12Resource>& out)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = heapType;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (bytes + 255ull) & ~255ull;
    rd.Height = 1u;
    rd.DepthOrArraySize = 1u;
    rd.MipLevels = 1u;
    rd.SampleDesc.Count = 1u;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initState,
                                                  nullptr, IID_PPV_ARGS(&out)));
}

DXGI_FORMAT ResolveUavFormat(DXGI_FORMAT fmt)
{
    switch (fmt) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    default:
        return fmt;
    }
}

uint32_t PackKindAndMeta(UiCmdKind kind, uint32_t meta, uint32_t dataOff) noexcept
{
    return (static_cast<uint32_t>(kind) & 0xFFu) |
           ((meta & 0xFFu) << 8u) |
           ((dataOff & 0xFFFFu) << 16u);
}

// Clean-room proportional advance width in pixels (at scale 1) for ASCII 32..126.
uint32_t BaseGlyphAdvance(unsigned char ch) noexcept
{
    switch (ch) {
    case ' ': return 4u;
    case '!': case '\'': case '.': case ',': case ':': case ';': case '|': return 4u;
    case 'i': case 'l': case 'I': case 'j': case 't': case '(': case ')': case '[': case ']': return 5u;
    case 'f': case 'r': case '1': case '-': case '`': return 6u;
    case 'M': case 'W': case 'm': case 'w': case '%': case '@': return 8u;
    default: return 7u;
    }
}

} // namespace

uint32_t GetGlyphAdvancePx(char ch, uint32_t scale) noexcept
{
    const uint32_t s = std::clamp(scale, 1u, 3u);
    const unsigned char u = static_cast<unsigned char>(ch);
    if (u < 32u || u > 126u) return 4u * s;
    return BaseGlyphAdvance(u) * s;
}

uint32_t MeasureTextWidthPx(const char* text, uint32_t scale) noexcept
{
    if (!text) return 0u;
    const uint32_t s = std::clamp(scale, 1u, 3u);
    uint32_t w = 0u;
    for (const char* p = text; *p != '\0'; ++p) {
        w += GetGlyphAdvancePx(*p, s);
    }
    return w;
}

void UiDrawList::Clear()
{
    m_cmds.clear();
    m_glyphs.clear();
    m_samples.clear();
    m_clipStack.clear();
}

void UiDrawList::PushClipRect(float x, float y, float w, float h)
{
    const float x0 = std::isfinite(x) ? x : 0.0f;
    const float y0 = std::isfinite(y) ? y : 0.0f;
    const float x1 = x0 + std::max(0.0f, std::isfinite(w) ? w : 0.0f);
    const float y1 = y0 + std::max(0.0f, std::isfinite(h) ? h : 0.0f);
    if (m_clipStack.empty()) {
        m_clipStack.push_back({x0, y0, x1, y1});
    } else {
        const UiRect& prev = m_clipStack.back();
        const float ix0 = std::max(prev.x, x0);
        const float iy0 = std::max(prev.y, y0);
        const float ix1 = std::max(ix0, std::min(prev.w, x1));
        const float iy1 = std::max(iy0, std::min(prev.h, y1));
        m_clipStack.push_back({ix0, iy0, ix1, iy1});
    }
}

void UiDrawList::PopClipRect()
{
    if (!m_clipStack.empty()) {
        m_clipStack.pop_back();
    }
}

UiRect UiDrawList::CurrentClip() const noexcept
{
    if (m_clipStack.empty()) {
        return {0.0f, 0.0f, 16384.0f, 16384.0f};
    }
    return m_clipStack.back();
}

void UiDrawList::PushCommand(GpuUiCmd cmd)
{
    if (m_cmds.size() >= kMaxCommands) return;
    if (!std::isfinite(cmd.rect[0]) || !std::isfinite(cmd.rect[1]) ||
        !std::isfinite(cmd.rect[2]) || !std::isfinite(cmd.rect[3])) {
        return;
    }
    if (cmd.rect[2] <= 0.0f || cmd.rect[3] <= 0.0f) return;

    const UiRect clip = CurrentClip();
    cmd.clipRect[0] = clip.x;
    cmd.clipRect[1] = clip.y;
    cmd.clipRect[2] = clip.w;
    cmd.clipRect[3] = clip.h;

    if (cmd.clipRect[2] <= cmd.clipRect[0] || cmd.clipRect[3] <= cmd.clipRect[1]) {
        return;
    }
    m_cmds.push_back(cmd);
}

void UiDrawList::AddPanel(float x, float y, float w, float h,
                          float cornerRadius,
                          uint32_t fillRgba,
                          uint32_t borderRgba,
                          uint32_t topAccentRgba,
                          float borderW)
{
    GpuUiCmd cmd{};
    cmd.rect[0]     = x;
    cmd.rect[1]     = y;
    cmd.rect[2]     = w;
    cmd.rect[3]     = h;
    cmd.colorFill   = fillRgba;
    cmd.colorBorder = borderRgba;
    cmd.colorAccent = topAccentRgba;
    cmd.kindAndMeta = PackKindAndMeta(UiCmdKind::Panel, 0u, 0u);
    cmd.params[0]   = std::max(0.0f, cornerRadius);
    cmd.params[1]   = std::clamp(borderW, 0.5f, 4.0f);
    PushCommand(cmd);
}

void UiDrawList::AddRect(float x, float y, float w, float h, uint32_t fillRgba)
{
    GpuUiCmd cmd{};
    cmd.rect[0]     = x;
    cmd.rect[1]     = y;
    cmd.rect[2]     = w;
    cmd.rect[3]     = h;
    cmd.colorFill   = fillRgba;
    cmd.kindAndMeta = PackKindAndMeta(UiCmdKind::Rect, 0u, 0u);
    PushCommand(cmd);
}

void UiDrawList::AddDivider(float x, float y, float w, uint32_t colorRgba)
{
    AddRect(x, y, w, 1.0f, colorRgba);
}

void UiDrawList::AddText(float x, float y, const char* text,
                         uint32_t colorRgba,
                         uint32_t scale,
                         uint32_t outlineRgba)
{
    if (!text || text[0] == '\0' || m_cmds.size() >= kMaxCommands) return;
    const uint32_t s = std::clamp(scale, 1u, 3u);
    const uint32_t startOff = static_cast<uint32_t>(m_glyphs.size());
    uint32_t penX = 0u;
    uint32_t count = 0u;

    for (const char* p = text; *p != '\0' && count < 128u; ++p) {
        if (m_glyphs.size() >= kMaxGlyphs) break;
        unsigned char u = static_cast<unsigned char>(*p);
        if (u < 32u || u > 126u) u = '?';
        const uint32_t adv = BaseGlyphAdvance(u);
        const uint32_t packed = static_cast<uint32_t>(u) |
                                ((adv & 0xFFu) << 8u) |
                                ((penX & 0xFFFFu) << 16u);
        m_glyphs.push_back(packed);
        penX += adv;
        ++count;
    }
    if (count == 0u) return;

    GpuUiCmd cmd{};
    cmd.rect[0]     = x;
    cmd.rect[1]     = y;
    cmd.rect[2]     = static_cast<float>(penX * s);
    cmd.rect[3]     = static_cast<float>(9u * s);
    cmd.colorFill   = colorRgba;
    cmd.colorBorder = outlineRgba;
    cmd.kindAndMeta = PackKindAndMeta(UiCmdKind::Text, s, startOff);
    cmd.params[3]   = static_cast<float>(count);
    PushCommand(cmd);
}

void UiDrawList::AddSectionHeader(float x, float y, float w, const char* title,
                                  uint32_t accentRgba, uint32_t scale,
                                  bool collapsible, bool expanded)
{
    const uint32_t s = std::clamp(scale, 1u, 3u);
    const float sf = static_cast<float>(s);

    // Crisp uppercase section caption per RAW Design Canon
    char upper[128]{};
    if (title) {
        size_t i = 0u;
        for (; i + 1u < sizeof(upper) && title[i] != '\0'; ++i) {
            const char ch = title[i];
            upper[i] = (ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - 'a' + 'A') : ch;
        }
        upper[i] = '\0';
    }

    if (collapsible) {
        GpuUiCmd chev{};
        chev.rect[0]     = x;
        chev.rect[1]     = y + 0.5f * sf;
        chev.rect[2]     = 8.0f * sf;
        chev.rect[3]     = 8.0f * sf;
        chev.colorFill   = accentRgba;
        chev.kindAndMeta = PackKindAndMeta(UiCmdKind::Disc, expanded ? 1u : 2u, 0u);
        chev.params[0]   = 1.15f * sf;
        PushCommand(chev);
        AddText(x + 11.0f * sf, y, upper, tokens::kInkPrimary, s);
    } else {
        AddRect(x, y + 1.0f, 3.0f * sf, 7.0f * sf, accentRgba);
        AddText(x + 7.0f * sf, y, upper, tokens::kInkPrimary, s);
    }
    AddDivider(x, y + static_cast<float>(11u * s), w, tokens::kBorderStrong);
}

void UiDrawList::AddTabBar(float x, float y, float w,
                           const char* const* labels, uint32_t tabCount, uint32_t activeIndex,
                           uint32_t accentRgba, uint32_t scale)
{
    if (!labels || tabCount == 0u || w <= 4.0f) return;
    const uint32_t s = std::clamp(scale, 1u, 3u);
    const float sf = static_cast<float>(s);
    const uint32_t clampedCount = std::min(tabCount, 8u);
    const uint32_t clampedActive = (activeIndex < clampedCount) ? activeIndex : 0u;
    const float barH = 18.0f * sf;
    const float tabW = w / static_cast<float>(clampedCount);

    // Subtle tab strip surface
    AddRect(x, y, w, barH, tokens::kSurface);

    for (uint32_t i = 0u; i < clampedCount; ++i) {
        const float tx = x + static_cast<float>(i) * tabW;
        const bool isActive = (i == clampedActive);
        if (isActive) {
            AddRect(tx, y, tabW, barH - 2.0f * sf, tokens::kPillOnBg);
            AddRect(tx + 2.0f * sf, y + barH - 2.0f * sf, std::max(2.0f, tabW - 4.0f * sf), 2.0f * sf, accentRgba);
        }
        const char* lbl = (labels[i] && labels[i][0] != '\0') ? labels[i] : "-";
        const float tw = static_cast<float>(MeasureTextWidthPx(lbl, s));
        const float lx = tx + std::max(2.0f * sf, (tabW - tw) * 0.5f);
        AddText(lx, y + 4.0f * sf, lbl, isActive ? tokens::kInkPrimary : tokens::kInkMuted, s, 0u);
    }

    // Bottom hairline divider across full tab strip
    AddDivider(x, y + barH, w, tokens::kBorderStrong);
}

void UiDrawList::AddBudgetBar(float x, float y, float w, float h,
                              const UiBudgetSegment* segments, uint32_t segmentCount,
                              float budgetTargetMs, float maxScaleMs, uint32_t scale)
{
    if (w <= 6.0f || h <= 4.0f) return;
    const uint32_t s = std::clamp(scale, 1u, 3u);
    const float sf = static_cast<float>(s);
    const float safeMax = (std::isfinite(maxScaleMs) && maxScaleMs > 0.05f) ? maxScaleMs : 20.0f;

    // Rounded dark graph-well container
    AddPanel(x, y, w, h, 3.0f * sf, tokens::kGraphWell, tokens::kBorderStrong, 0u, 1.0f);

    const float pad = 2.0f;
    const float innerX = x + pad;
    const float innerY = y + pad;
    const float innerW = std::max(1.0f, w - pad * 2.0f);
    const float innerH = std::max(1.0f, h - pad * 2.0f);

    PushClipRect(innerX, innerY, innerW, innerH);
    if (segments && segmentCount > 0u) {
        const uint32_t count = std::min(segmentCount, 8u);
        float cursorX = innerX;
        for (uint32_t i = 0u; i < count; ++i) {
            const float v = (std::isfinite(segments[i].valueMs) && segments[i].valueMs > 0.0f)
                                ? segments[i].valueMs : 0.0f;
            if (v <= 0.0f) continue;
            const float segW = (v / safeMax) * innerW;
            if (segW < 0.5f) continue;
            const float drawW = std::min(segW, (innerX + innerW) - cursorX);
            if (drawW <= 0.0f) break;
            AddRect(cursorX, innerY, drawW, innerH, segments[i].colorRgba);
            cursorX += drawW;
            if (cursorX >= innerX + innerW) break;
        }
    }
    PopClipRect();

    // Vertical target frame-budget marker line
    if (std::isfinite(budgetTargetMs) && budgetTargetMs > 0.0f && budgetTargetMs <= safeMax) {
        const float frac = std::clamp(budgetTargetMs / safeMax, 0.0f, 1.0f);
        const float mx = innerX + frac * innerW - 1.0f;
        AddRect(mx, y + 1.0f, 2.0f, std::max(1.0f, h - 2.0f), tokens::kInkPrimary);
    }
}

void UiDrawList::AddVerdictDot(float x, float y, float diameter,
                               uint32_t fillRgba, uint32_t borderRgba)
{
    GpuUiCmd dot{};
    dot.rect[0]     = x;
    dot.rect[1]     = y;
    dot.rect[2]     = std::max(4.0f, diameter);
    dot.rect[3]     = std::max(4.0f, diameter);
    dot.colorFill   = fillRgba;
    dot.colorBorder = borderRgba;
    dot.kindAndMeta = PackKindAndMeta(UiCmdKind::Disc, 0u, 0u);
    PushCommand(dot);
}

void UiDrawList::AddEngineVarRow(float x, float y, float rowWidth,
                                 const char* key, const char* valueText,
                                 uint32_t verdictRgba, uint32_t scale)
{
    const uint32_t s = std::clamp(scale, 1u, 3u);
    const float sf = static_cast<float>(s);
    const float dotD = 7.0f * sf;

    AddVerdictDot(x, y + 1.0f * sf, dotD, verdictRgba, tokens::kBorderStrong);
    if (key && key[0] != '\0') {
        AddText(x + dotD + 5.0f * sf, y, key, tokens::kInkSecondary, s);
    }
    if (valueText && valueText[0] != '\0') {
        const float vw = static_cast<float>(MeasureTextWidthPx(valueText, s));
        AddText(x + std::max(dotD + 8.0f * sf, rowWidth - vw), y, valueText, tokens::kInkPrimary, s);
    }
}

void UiDrawList::AddEngineVarFloat(float x, float y, float rowWidth,
                                   const char* key, float value, const char* unit,
                                   uint32_t verdictRgba, uint32_t scale)
{
    char buf[48]{};
    const float safe = std::isfinite(value) ? value : 0.0f;
    if (unit && unit[0] != '\0') {
        std::snprintf(buf, sizeof(buf), "%.2f %s", static_cast<double>(safe), unit);
    } else {
        std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(safe));
    }
    AddEngineVarRow(x, y, rowWidth, key, buf, verdictRgba, scale);
}

void UiDrawList::AddEngineVarBool(float x, float y, float rowWidth,
                                  const char* key, bool value, uint32_t scale)
{
    AddEngineVarRow(x, y, rowWidth, key, value ? "ON" : "OFF",
                    value ? tokens::kAccentPass : tokens::kAccentNull, scale);
}

void UiDrawList::AddPill(float x, float y, const char* label,
                         uint32_t bgRgba, uint32_t textRgba,
                         uint32_t borderRgba, uint32_t dotRgba,
                         uint32_t scale)
{
    const uint32_t s = std::clamp(scale, 1u, 3u);
    const bool hasDot = ((dotRgba >> 24u) & 0xFFu) > 0u;
    const uint32_t textW = MeasureTextWidthPx(label, s);
    const float padX = 7.0f * static_cast<float>(s);
    const float dotPad = hasDot ? (11.0f * static_cast<float>(s)) : 0.0f;
    const float h = static_cast<float>(15u * s);
    const float w = padX * 2.0f + dotPad + static_cast<float>(textW);

    GpuUiCmd pill{};
    pill.rect[0]     = x;
    pill.rect[1]     = y;
    pill.rect[2]     = w;
    pill.rect[3]     = h;
    pill.colorFill   = bgRgba;
    pill.colorBorder = borderRgba;
    pill.colorAccent = dotRgba;
    pill.kindAndMeta = PackKindAndMeta(UiCmdKind::Pill, 0u, 0u);
    PushCommand(pill);

    AddText(x + padX + dotPad, y + 3.0f * static_cast<float>(s), label, textRgba, s, 0u);
}

void UiDrawList::AddToggle(float x, float y, float rowWidth, const char* label, bool enabled,
                           uint32_t onAccentRgba, uint32_t scale)
{
    const uint32_t s = std::clamp(scale, 1u, 3u);
    const float sf = static_cast<float>(s);
    const float swW = 26.0f * sf;
    const float swH = 13.0f * sf;

    AddText(x, y + 2.0f * sf, label, enabled ? tokens::kInkPrimary : tokens::kInkMuted, s);

    GpuUiCmd sw{};
    sw.rect[0]     = x + std::max(0.0f, rowWidth - swW);
    sw.rect[1]     = y;
    sw.rect[2]     = swW;
    sw.rect[3]     = swH;
    sw.colorFill   = tokens::kSliderTrack;
    sw.colorBorder = tokens::kBorderStrong;
    sw.colorAccent = onAccentRgba;
    sw.kindAndMeta = PackKindAndMeta(UiCmdKind::Toggle, 0u, 0u);
    sw.params[2]   = enabled ? 1.0f : 0.0f;
    PushCommand(sw);
}

void UiDrawList::AddSlider(float x, float y, float rowWidth, const char* label,
                           float value01, const char* valueText,
                           uint32_t fillRgba, uint32_t scale)
{
    const uint32_t s = std::clamp(scale, 1u, 3u);
    const float sf = static_cast<float>(s);
    const float clamped = std::clamp(std::isfinite(value01) ? value01 : 0.0f, 0.0f, 1.0f);

    if (label && label[0] != '\0') {
        AddText(x, y, label, tokens::kInkSecondary, s);
    }
    if (valueText && valueText[0] != '\0') {
        const float vw = static_cast<float>(MeasureTextWidthPx(valueText, s));
        AddText(x + std::max(0.0f, rowWidth - vw), y, valueText, tokens::kInkPrimary, s);
    }

    GpuUiCmd sl{};
    sl.rect[0]     = x;
    sl.rect[1]     = y + 11.0f * sf;
    sl.rect[2]     = std::max(24.0f * sf, rowWidth);
    sl.rect[3]     = 11.0f * sf;
    sl.colorFill   = tokens::kSliderTrack;
    sl.colorBorder = tokens::kBorderStrong;
    sl.colorAccent = fillRgba;
    sl.kindAndMeta = PackKindAndMeta(UiCmdKind::Slider, 0u, 0u);
    sl.params[2]   = clamped;
    PushCommand(sl);
}

void UiDrawList::AddSparkline(float x, float y, float w, float h,
                              const float* samples, uint32_t count,
                              float greenMax, float amberMax, float graphMax)
{
    if (!samples || count == 0u || m_cmds.size() >= kMaxCommands) return;
    const uint32_t startOff = static_cast<uint32_t>(m_samples.size());
    const uint32_t copyN = std::min<uint32_t>(count, std::min<uint32_t>(128u, kMaxSamples - startOff));
    if (copyN == 0u) return;

    for (uint32_t i = 0u; i < copyN; ++i) {
        const float v = samples[i];
        m_samples.push_back((std::isfinite(v) && v > 0.0f) ? v : 0.0f);
    }

    GpuUiCmd sp{};
    sp.rect[0]     = x;
    sp.rect[1]     = y;
    sp.rect[2]     = w;
    sp.rect[3]     = h;
    sp.colorFill   = tokens::kGraphWell;
    sp.colorBorder = tokens::kBorder;
    sp.kindAndMeta = PackKindAndMeta(UiCmdKind::Sparkline, 0u, startOff);
    sp.params[0]   = std::max(0.1f, greenMax);
    sp.params[1]   = std::max(sp.params[0] + 0.1f, amberMax);
    sp.params[3]   = static_cast<float>(copyN);
    sp.extra[0]    = std::max(sp.params[1] + 0.5f, graphMax);
    PushCommand(sp);
}

void UiDrawList::AddLegendSwatch(float x, float y, uint32_t swatchRgba, const char* label,
                                 const char* badgeText, uint32_t scale)
{
    const uint32_t s = std::clamp(scale, 1u, 3u);
    const float sf = static_cast<float>(s);
    const float box = 10.0f * sf;

    GpuUiCmd sw{};
    sw.rect[0]     = x;
    sw.rect[1]     = y;
    sw.rect[2]     = box;
    sw.rect[3]     = box;
    sw.colorFill   = swatchRgba;
    sw.colorBorder = tokens::kBorderStrong;
    sw.kindAndMeta = PackKindAndMeta(UiCmdKind::Swatch, 0u, 0u);
    sw.params[0]   = 2.0f * sf;
    PushCommand(sw);

    float tx = x + box + 6.0f * sf;
    if (badgeText && badgeText[0] != '\0') {
        AddText(tx, y + 1.0f * sf, badgeText, tokens::kAccentLead, s);
        tx += static_cast<float>(MeasureTextWidthPx(badgeText, s)) + 5.0f * sf;
    }
    if (label && label[0] != '\0') {
        AddText(tx, y + 1.0f * sf, label, tokens::kInkPrimary, s);
    }
}

UiPixelRect UiDrawList::ComputeDirtyRect(uint32_t targetWidth, uint32_t targetHeight) const
{
    UiPixelRect r{};
    if (m_cmds.empty() || targetWidth == 0u || targetHeight == 0u) return r;

    float minX = static_cast<float>(targetWidth);
    float minY = static_cast<float>(targetHeight);
    float maxX = 0.0f;
    float maxY = 0.0f;

    for (const auto& c : m_cmds) {
        const float cx0 = std::max(c.rect[0] - 2.0f, c.clipRect[0]);
        const float cy0 = std::max(c.rect[1] - 2.0f, c.clipRect[1]);
        const float cx1 = std::min(c.rect[0] + c.rect[2] + 2.0f, c.clipRect[2]);
        const float cy1 = std::min(c.rect[1] + c.rect[3] + 2.0f, c.clipRect[3]);
        if (cx1 <= cx0 || cy1 <= cy0) continue;
        minX = std::min(minX, cx0);
        minY = std::min(minY, cy0);
        maxX = std::max(maxX, cx1);
        maxY = std::max(maxY, cy1);
    }
    if (maxX <= minX || maxY <= minY) return r;

    const uint32_t x0 = static_cast<uint32_t>(std::clamp(std::floor(minX), 0.0f, static_cast<float>(targetWidth)));
    const uint32_t y0 = static_cast<uint32_t>(std::clamp(std::floor(minY), 0.0f, static_cast<float>(targetHeight)));
    const uint32_t x1 = static_cast<uint32_t>(std::clamp(std::ceil(maxX), 0.0f, static_cast<float>(targetWidth)));
    const uint32_t y1 = static_cast<uint32_t>(std::clamp(std::ceil(maxY), 0.0f, static_cast<float>(targetHeight)));
    if (x1 <= x0 || y1 <= y0) return r;
    r.x = x0;
    r.y = y0;
    r.w = x1 - x0;
    r.h = y1 - y0;
    return r;
}

DebugViewDescriptor GetDebugViewDescriptor(uint32_t debugViewId)
{
    DebugViewDescriptor d{};
    d.viewId = debugViewId;
    switch (debugViewId) {
    case 0u:
        d.shortName  = "0:Final";
        d.title      = "Final Lit Scene";
        d.purpose    = "PBR + RT Shadows + Atmosphere + Live World";
        d.accentRgba = tokens::kAccentPass;
        d.legend = {
            {tokens::kAccentPass,    "Direct + IBL + RT",    "PBR"},
            {tokens::kAccentEmerald, "Live World Solvers",   "GPU"},
            {tokens::kAccentLead,    "Atmosphere & Clouds",  "SKY"},
        };
        break;
    case 1u:
        d.shortName  = "1:Normals";
        d.title      = "World-Space Normals";
        d.purpose    = "RGB mapped from [-1,+1] -> [0,1]";
        d.accentRgba = PackRgba8(0x7C, 0xB8, 0xFF);
        d.legend = {
            {PackRgba8(0xE8, 0x55, 0x55), "+X East Normal",  "R"},
            {PackRgba8(0x55, 0xE0, 0x7A), "+Y North Normal", "G"},
            {PackRgba8(0x61, 0x9F, 0xF9), "+Z Up Normal",    "B"},
        };
        break;
    case 2u:
        d.shortName  = "2:SunShadow";
        d.title      = "Sun Shadow Visibility";
        d.purpose    = "RT & cascade sun occlusion term";
        d.accentRgba = tokens::kAccentLead;
        d.legend = {
            {PackRgba8(0xF2, 0xEE, 0xE2), "Direct Sun Lit",    "1.0"},
            {PackRgba8(0x20, 0x26, 0x34), "Occluded Shadow",   "0.0"},
        };
        break;
    case 3u:
        d.shortName  = "3:Ambient";
        d.title      = "Ambient + Sky + GI Bounce";
        d.purpose    = "Hemisphere ambient plus indirect bounce";
        d.accentRgba = PackRgba8(0x7A, 0xC4, 0xF8);
        d.legend = {
            {PackRgba8(0x7A, 0xC4, 0xF8), "Cool Sky Ambient",  "SKY"},
            {PackRgba8(0xC8, 0x98, 0x62), "Warm Bounce Fill",  "GI"},
        };
        break;
    case 4u:
        d.shortName  = "4:SkyVis";
        d.title      = "Sky Hemisphere Visibility";
        d.purpose    = "RT sky-cone openness & RTAO factor";
        d.accentRgba = PackRgba8(0x90, 0xD2, 0xFF);
        d.legend = {
            {PackRgba8(0xEC, 0xF2, 0xFC), "Open Sky Dome",     "OPEN"},
            {PackRgba8(0x1E, 0x24, 0x30), "Deep Crevice Occ",  "OCC"},
        };
        break;
    case 5u:
        d.shortName  = "5:GIBounce";
        d.title      = "Multi-Bounce GI Irradiance";
        d.purpose    = "SVGF filtered indirect diffuse light";
        d.accentRgba = tokens::kAccentLead;
        d.legend = {
            {PackRgba8(0xEE, 0xB1, 0x53), "Warm Indirect GI",  "GI+"},
            {PackRgba8(0x3C, 0x76, 0xB8), "Cool Sky Bounce",   "SKY"},
        };
        break;
    case 6u:
        d.shortName  = "6:RtRefl";
        d.title      = "Ray-Traced Reflections";
        d.purpose    = "Primary reflection ray hits & fallback";
        d.accentRgba = PackRgba8(0x52, 0xD4, 0xF2);
        d.legend = {
            {PackRgba8(0x52, 0xD4, 0xF2), "RT Specular Hit",   "HIT"},
            {PackRgba8(0x36, 0x4E, 0x72), "EnvCube Fallback",  "ENV"},
        };
        break;
    case 7u:
        d.shortName  = "7:RtDebug";
        d.title      = "RT Reflection / Primary Buffer";
        d.purpose    = "Raw RtRefl buffer before composite";
        d.accentRgba = PackRgba8(0x64, 0xD2, 0xE8);
        d.legend = {
            {PackRgba8(0x64, 0xD2, 0xE8), "RtRefl RGB Sample", "RT"},
            {PackRgba8(0x1C, 0x22, 0x2E), "No RT Reflection",  "MISS"},
        };
        break;
    case 8u:
        d.shortName  = "8:Weather";
        d.title      = "Weather Wetness / Puddle / Snow";
        d.purpose    = "Surface wetness, rain puddles & snow";
        d.accentRgba = PackRgba8(0x64, 0xD2, 0xE8);
        d.legend = {
            {PackRgba8(0xF0, 0x5A, 0x4A), "Rain Wetness",      "R"},
            {PackRgba8(0x46, 0xDC, 0x72), "Standing Puddles",  "G"},
            {PackRgba8(0x5A, 0xA8, 0xFF), "Snow Accumulation", "B"},
        };
        break;
    case 9u:
        d.shortName  = "9:SoftMap";
        d.title      = "Emissive & SoftMap Mask";
        d.purpose    = "Emissive glow & character light slot";
        d.accentRgba = tokens::kAccentLead;
        d.legend = {
            {PackRgba8(0xFA, 0xC8, 0x58), "Emissive / Glow",   "EM"},
            {PackRgba8(0x56, 0x98, 0xF8), "EnvMap / Spec",     "SP"},
        };
        break;
    case 10u:
        d.shortName  = "10:SkyMask";
        d.title      = "Sky & Unlit Depth Mask";
        d.purpose    = "Highlights sky pixels (depth >= 1.0)";
        d.accentRgba = PackRgba8(0xF0, 0x48, 0xD8);
        d.legend = {
            {PackRgba8(0xFF, 0x28, 0xE0), "Sky / Empty Depth", "SKY"},
            {PackRgba8(0x68, 0x74, 0x86), "Solid World Geo",   "GEO"},
        };
        break;
    case 11u:
        d.shortName  = "11:CloudShd";
        d.title      = "Volumetric Cloud Shadow";
        d.purpose    = "Projected 3D cloud transmittance";
        d.accentRgba = PackRgba8(0xA8, 0xC6, 0xE8);
        d.legend = {
            {PackRgba8(0xEC, 0xF0, 0xF8), "Clear Sun Beam",    "1.0"},
            {PackRgba8(0x28, 0x34, 0x48), "Cloud Umbra",       "SHD"},
        };
        break;
    case 12u:
        d.shortName  = "12:Classes";
        d.title      = "Legacy G-Buffer Classes";
        d.purpose    = "Core material class tags from Spec.z";
        d.accentRgba = tokens::kAccentLead;
        d.legend = {
            {PackRgba8(0xF0, 0x4E, 0x46), "Actor Skin",        "RED"},
            {PackRgba8(0x3C, 0xD4, 0x62), "Foliage / Tree",    "GRN"},
            {PackRgba8(0x98, 0x6A, 0x38), "Terrain / Ground",  "BRN"},
            {PackRgba8(0x3C, 0x78, 0xF4), "Parallax Masonry",  "BLU"},
        };
        break;
    case 13u:
        d.shortName  = "13:Irrad";
        d.title      = "Total Received Irradiance";
        d.purpose    = "Direct diffuse + ambient irradiance";
        d.accentRgba = tokens::kAccentLead;
        d.legend = {
            {PackRgba8(0xFA, 0xE2, 0xA8), "High Irradiance",   "LIT"},
            {PackRgba8(0x28, 0x34, 0x4C), "Shadowed Ambient",  "AMB"},
        };
        break;
    case 14u:
        d.shortName  = "14:RestVis";
        d.title      = "Untraced Lights RT Share";
        d.purpose    = "ReSTIR / untraced point light visibility";
        d.accentRgba = tokens::kAccentPass;
        d.legend = {
            {PackRgba8(0xE8, 0xEE, 0xF6), "Visible Local Light", "VIS"},
            {PackRgba8(0x24, 0x2C, 0x3C), "Occluded Local",      "OCC"},
        };
        break;
    case 15u:
        d.shortName  = "15:LodAlbedo";
        d.title      = "Distant LOD & Surface Albedo";
        d.purpose    = "Raw G-Buffer Albedo including LOD";
        d.accentRgba = tokens::kAccentEmerald;
        d.legend = {
            {PackRgba8(0xC8, 0xC2, 0xB4), "Surface Albedo",    "ALB"},
            {PackRgba8(0x46, 0xB8, 0xB8), "Distant LOD (7)",   "LOD"},
        };
        break;
    case 16u:
        d.shortName  = "16:Albedo";
        d.title      = "Base Diffuse Albedo";
        d.purpose    = "Unlit surface reflectance before lighting";
        d.accentRgba = tokens::kAccentLead;
        d.legend = {
            {PackRgba8(0xDE, 0xD6, 0xC8), "Diffuse Reflectance", "ALB"},
            {PackRgba8(0x4A, 0x70, 0x42), "Conifer & Moss Base", "FLR"},
        };
        break;
    case 17u:
        d.shortName  = "17:GameRatio";
        d.title      = "Game Frame vs RAW Diffuse Ratio";
        d.purpose    = "0.25 grey = exact exposure parity";
        d.accentRgba = tokens::kAccentNull;
        d.legend = {
            {PackRgba8(0x44, 0x44, 0x44), "0.25 Exact Parity", "EQ"},
            {PackRgba8(0xEC, 0xEC, 0xEC), "Game Brighter",     "HI"},
        };
        break;
    case 18u:
        d.shortName  = "18:EnvRefl";
        d.title      = "Class-2 Environment Reflection";
        d.purpose    = "Armor, metal & leather cubemap term";
        d.accentRgba = PackRgba8(0xF2, 0xC6, 0x44);
        d.legend = {
            {PackRgba8(0xF2, 0xC6, 0x44), "Metallic EnvMap",   "C2"},
            {PackRgba8(0x20, 0x24, 0x30), "Non-Env Surface",   "OFF"},
        };
        break;
    case 19u:
        d.shortName  = "19:CharLight";
        d.title      = "Character Fill Light";
        d.purpose    = "Audits flat camera-attached actor fill";
        d.accentRgba = PackRgba8(0xE8, 0xB4, 0x92);
        d.legend = {
            {PackRgba8(0xEC, 0xE8, 0xE0), "Active Char Light", "CHR"},
            {PackRgba8(0x1E, 0x22, 0x2C), "No Char Fill",      "0.0"},
        };
        break;
    case 20u:
        d.shortName  = "20:QuadAudit";
        d.title      = "4-Quadrant Live Diagnostic";
        d.purpose    = "TL Final | TR Norm+Env | BL RT | BR Heat";
        d.accentRgba = tokens::kAccentPass;
        d.legend = {
            {tokens::kAccentPass,         "TL Final / BL RT",  "L"},
            {tokens::kAccentLead,         "TR Norm / BR Heat", "R"},
        };
        break;
    case 21u:
        d.shortName  = "21:LumaHeat";
        d.title      = "Luminance & Clipping Heatmap";
        d.purpose    = "Flags crushed blacks & blown highlights";
        d.accentRgba = PackRgba8(0xFF, 0x34, 0xC4);
        d.legend = {
            {PackRgba8(0x10, 0xD8, 0xF8), "Crushed < 0.015",   "LOW"},
            {PackRgba8(0xEE, 0xC8, 0x48), "Balanced Midtones", "MID"},
            {PackRgba8(0xFF, 0x18, 0xCC), "Blown > 0.95",      "HI"},
        };
        break;
    case 22u:
        d.shortName  = "22:Wireframe";
        d.title      = "Mesh Wireframe & Silhouette Facets";
        d.purpose    = "Normal & depth edge discontinuities";
        d.accentRgba = PackRgba8(0x26, 0xF2, 0xD8);
        d.legend = {
            {PackRgba8(0x26, 0xF2, 0xD8), "Facet / Edge Wire", "EDG"},
            {PackRgba8(0x28, 0x30, 0x3E), "Dimmed Albedo",     "GEO"},
        };
        break;
    case 23u:
        d.shortName  = "23:CloudScat";
        d.title      = "Cloud Shadow & Spectral Scatter";
        d.purpose    = "R=cloudShadow, G=ambient, B=direct";
        d.accentRgba = PackRgba8(0x88, 0xC2, 0xF8);
        d.legend = {
            {PackRgba8(0xEE, 0x58, 0x4A), "Cloud Shadow",      "R"},
            {PackRgba8(0x4C, 0xDC, 0x76), "Ambient Term",      "G"},
            {PackRgba8(0x5A, 0x9E, 0xFA), "Direct Diffuse",    "B"},
        };
        break;
    case 24u:
        d.shortName  = "24:Roughness";
        d.title      = "PBR Microfacet Roughness";
        d.purpose    = "Dark=mirror/wet, Bright=matte stone/snow";
        d.accentRgba = PackRgba8(0xB8, 0xC8, 0xDC);
        d.legend = {
            {PackRgba8(0x24, 0x2C, 0x38), "Smooth / Mirror",   "0.0"},
            {PackRgba8(0xE4, 0xE8, 0xF0), "Rough Matte",       "1.0"},
        };
        break;
    case 25u:
        d.shortName  = "25:Metallic";
        d.title      = "Metallic / Clearcoat / Dielectric";
        d.purpose    = "R=conductor, G=wet coat, B=dielectric";
        d.accentRgba = PackRgba8(0xF4, 0x78, 0x52);
        d.legend = {
            {PackRgba8(0xF0, 0x52, 0x46), "Conductor Metal",   "R"},
            {PackRgba8(0x44, 0xDC, 0x74), "Wet / Clearcoat",   "G"},
            {PackRgba8(0x52, 0x92, 0xF8), "Dielectric Base",   "B"},
        };
        break;
    case 26u:
        d.shortName  = "26:Parallax";
        d.title      = "Parallax Height Map Inspector";
        d.purpose    = "Cyan=_p.dds height, Gold=macro relief";
        d.accentRgba = PackRgba8(0x26, 0xE6, 0xFF);
        d.legend = {
            {PackRgba8(0x26, 0xE6, 0xFF), "Authored _p.dds",   "P6"},
            {PackRgba8(0xFF, 0xC6, 0x46), "Macro Relief",      "REL"},
        };
        break;
    case 27u:
        d.shortName  = "27:GiContrast";
        d.title      = "SVGF GI Bounce & AO Contrast";
        d.purpose    = "R=bounce luma, G=skyVis, B=AO term";
        d.accentRgba = tokens::kAccentEmerald;
        d.legend = {
            {PackRgba8(0xF0, 0x62, 0x48), "GI Bounce Luma",    "R"},
            {PackRgba8(0x48, 0xDC, 0x78), "Sky Visibility",    "G"},
            {PackRgba8(0x58, 0x9C, 0xF8), "Ambient Occlusion", "B"},
        };
        break;
    case 28u:
        d.shortName  = "28:Class+A14";
        d.title      = "Full Class ID + Actor14 Map";
        d.purpose    = "Verifies zero moss/rock on creatures/NPCs";
        d.accentRgba = tokens::kAccentFail;
        d.legend = {
            {PackRgba8(0xFF, 0x52, 0x10), "Skinned Actor/Fur/Cloth", "14"},
            {PackRgba8(0xFF, 0x42, 0x3D), "Skin / Hair / Eye",       "10-12"},
            {PackRgba8(0x20, 0xD2, 0x3E), "Foliage / Trees",         "05"},
            {PackRgba8(0x8F, 0x61, 0x2E), "Terrain / Landscape",     "04"},
            {PackRgba8(0x94, 0x9A, 0xA4), "Static Stone / Arch",     "01"},
        };
        break;
    case 29u:
        d.shortName  = "29:LinDepth";
        d.title      = "Linear Depth & Discontinuities";
        d.purpose    = "Inspects silhouette edges & sky depth";
        d.accentRgba = PackRgba8(0x58, 0xD0, 0xE8);
        d.legend = {
            {PackRgba8(0x3A, 0x68, 0xE8), "Near Foreground",      "NEAR"},
            {PackRgba8(0xD8, 0x92, 0x5A), "Mid / Far Landscape",  "FAR"},
            {PackRgba8(0x26, 0xFF, 0xF2), "Silhouette Edge Step", "EDGE"},
        };
        break;
    case 30u:
        d.shortName  = "30:ShadowStab";
        d.title      = "Shadow Stability & RT Light Slots";
        d.purpose    = "R=sunShadow, G=RT top-3 slots, B=ReSTIR";
        d.accentRgba = tokens::kAccentLead;
        d.legend = {
            {PackRgba8(0xF0, 0x58, 0x48), "Sun Shadow Vis",       "R"},
            {PackRgba8(0x48, 0xDC, 0x72), "RT Point-Light Slots", "G"},
            {PackRgba8(0x58, 0x9C, 0xFA), "ReSTIR Visibility",    "B"},
        };
        break;
    case 31u:
        d.shortName  = "31:MossSnowIce";
        d.title      = "Moss / Snow / Glacier Mask";
        d.purpose    = "Magenta alarm if moss leaks onto actors";
        d.accentRgba = tokens::kAccentEmerald;
        d.legend = {
            {PackRgba8(0xF2, 0x56, 0x48), "Snow Overhang / Pack", "R"},
            {PackRgba8(0x3E, 0xD2, 0x66), "Exterior Moss Mask",   "G"},
            {PackRgba8(0x4C, 0x98, 0xF8), "Glacial Ice Facets",   "B"},
            {PackRgba8(0xFF, 0x10, 0xE8), "ALARM: Actor Leak",    "ERR"},
        };
        break;
    case 32u:
        d.shortName  = "32:SkinSSS/AO";
        d.title      = "Actor Skin / SSS & Contact-AO";
        d.purpose    = "Audits blotchy faces & outline halos";
        d.accentRgba = PackRgba8(0xF2, 0x9C, 0x7A);
        d.legend = {
            {PackRgba8(0xF2, 0x6C, 0x5A), "Actor / Skin SSS",     "R"},
            {PackRgba8(0x4C, 0xD8, 0x7A), "Soft Contact AO",      "G"},
            {PackRgba8(0x61, 0x9F, 0xF9), "Specular Rim Wrap",    "B"},
        };
        break;
    case 33u:
        d.shortName  = "33:NaN/HDR";
        d.title      = "NaN / Inf / HDR Energy Heatmap";
        d.purpose    = "Magenta flash = NaN/Inf or HDR > 32";
        d.accentRgba = PackRgba8(0xFF, 0x2E, 0xD0);
        d.legend = {
            {PackRgba8(0x22, 0x98, 0x58), "Safe Nominal [0..4]",  "OK"},
            {PackRgba8(0xEE, 0xB1, 0x53), "Bright HDR [4..32]",   "HI"},
            {PackRgba8(0xFF, 0x20, 0xD4), "NaN / Inf / >32 BUG",  "ERR"},
        };
        break;
    case 34u:
        d.shortName  = "34:FloraTrees";
        d.title      = "Flora, Bark, Pine & Foliage Audit";
        d.purpose    = "Flags crushed-black bark & bleached needles";
        d.accentRgba = tokens::kAccentEmerald;
        d.legend = {
            {PackRgba8(0x2E, 0xC8, 0x5A), "Foliage Canopy (b=5)", "FLR"},
            {PackRgba8(0x8E, 0xE2, 0x3A), "Pine Sapling (b=1)",   "PIN"},
            {PackRgba8(0xC8, 0x84, 0x38), "Bark Trunk Timber",    "BRK"},
            {PackRgba8(0xF4, 0x32, 0x2E), "ALARM: Crushed <0.015","BLK"},
            {PackRgba8(0xFF, 0x20, 0xD8), "ALARM: Bleached Chalk","CHK"},
        };
        break;
    case 35u:
        d.shortName  = "35:MotionEdge";
        d.title      = "Motion Vector & Silhouette Audit";
        d.purpose    = "R/G=signed MV (0.5=0), B=MV divergence";
        d.accentRgba = PackRgba8(0x52, 0xD8, 0xF8);
        d.legend = {
            {PackRgba8(0xF4, 0x5A, 0x4A), "Signed Motion X (0.5=0)", "R"},
            {PackRgba8(0x4A, 0xE2, 0x82), "Signed Motion Y (0.5=0)", "G"},
            {PackRgba8(0x58, 0xA8, 0xFF), "MV Divergence / Outline", "B"},
            {PackRgba8(0x80, 0x80, 0x10), "Static / Sky (0.5,0.5)",  "0"},
        };
        break;
    case 36u:
        d.shortName  = "36:FireFroxel";
        d.title      = "Thermal Fire, Froxel & Transmittance";
        d.purpose    = "Indoor transmittance compliance >= 0.78";
        d.accentRgba = tokens::kAccentFail;
        d.legend = {
            {PackRgba8(0xFF, 0x52, 0x2A), "Fire Heat / Emission", "R"},
            {PackRgba8(0x46, 0xDC, 0x7A), "Froxel Ext (1-T)",     "G"},
            {PackRgba8(0x58, 0xA6, 0xFF), "Volumetric Sun Shaft", "B"},
            {PackRgba8(0xF4, 0x2A, 0x3A), "ALARM: Indoor T<0.78", "FOG"},
        };
        break;
    default:
        d.shortName  = "CustomView";
        d.title      = "Custom Debug Visualization";
        d.purpose    = "Active shader diagnostic view";
        d.accentRgba = tokens::kAccentLead;
        d.legend = {
            {tokens::kAccentLead, "Diagnostic Output", "DBG"},
        };
        break;
    }
    return d;
}

DebugViewDescriptor GetWaterDebugViewDescriptor(uint32_t waterDebugViewId)
{
    DebugViewDescriptor d{};
    d.viewId = waterDebugViewId;
    switch (waterDebugViewId) {
    case 1u:
        d.shortName  = "W1:Refl";
        d.title      = "Water Surface Reflections";
        d.purpose    = "RT & sky cubemap mirror reflectance";
        d.accentRgba = PackRgba8(0x52, 0xD2, 0xF8);
        d.legend = {
            {PackRgba8(0x64, 0xD2, 0xFA), "Sky / RT Reflection",  "RFL"},
            {PackRgba8(0x28, 0x42, 0x62), "Grazing Horizon",      "HRZ"},
        };
        break;
    case 2u:
        d.shortName  = "W2:Caustics";
        d.title      = "Underwater Refracted Caustics";
        d.purpose    = "Refracted sunlit caustic filaments (4x)";
        d.accentRgba = PackRgba8(0x6C, 0xF0, 0xE0);
        d.legend = {
            {PackRgba8(0x7C, 0xFA, 0xE8), "Focused Caustic Web",  "CAU"},
            {PackRgba8(0x18, 0x36, 0x44), "Deep Attenuated Bed",  "DEP"},
        };
        break;
    case 3u:
        d.shortName  = "W3:Fresnel";
        d.title      = "Schlick Fresnel & View Grazing";
        d.purpose    = "R=Fresnel F, G=NdotV, B=1.0";
        d.accentRgba = PackRgba8(0x90, 0xB8, 0xFF);
        d.legend = {
            {PackRgba8(0xF4, 0x58, 0x58), "Fresnel Factor (F)",   "R"},
            {PackRgba8(0x4C, 0xE0, 0x7A), "View Dot Normal",      "G"},
        };
        break;
    case 4u:
        d.shortName  = "W4:Depth";
        d.title      = "Optical Water Bed & Depth";
        d.purpose    = "Red=no bed, Green=deep, Blue=shallow";
        d.accentRgba = PackRgba8(0x46, 0xC8, 0xF2);
        d.legend = {
            {PackRgba8(0xF4, 0x36, 0x32), "No Bed Behind Water",  "RED"},
            {PackRgba8(0x36, 0xD8, 0x64), "Deep River / Lake",    "GRN"},
            {PackRgba8(0x46, 0x8E, 0xFF), "Shallow Shore Bed",    "BLU"},
        };
        break;
    case 5u:
        d.shortName  = "W5:Normal";
        d.title      = "Wave Normal & Fresnel Bands";
        d.purpose    = "Red F>0.25, Green F>0.06, Blue F<=0.06";
        d.accentRgba = PackRgba8(0x64, 0xE0, 0x98);
        d.legend = {
            {PackRgba8(0xF4, 0x38, 0x34), "Grazing Band F>0.25",  "HI"},
            {PackRgba8(0x3E, 0xD8, 0x66), "Mid Band F>0.06",      "MID"},
            {PackRgba8(0x48, 0x88, 0xFF), "Steep Normal F<=0.06", "LO"},
        };
        break;
    case 6u:
        d.shortName  = "W6:Body";
        d.title      = "Water Body Absorption & Scatter";
        d.purpose    = "Refracted riverbed + Beer-Lambert volume";
        d.accentRgba = PackRgba8(0x38, 0xB8, 0xB2);
        d.legend = {
            {PackRgba8(0x38, 0xB8, 0xB2), "Refracted Volume",     "VOL"},
            {PackRgba8(0x16, 0x34, 0x42), "Deep Extinction",      "ABS"},
        };
        break;
    case 7u:
        d.shortName  = "W7:FlowDiag";
        d.title      = "River Flow, Depth & Shore Audit";
        d.purpose    = "R=flow, G=depth/120, B=shore, Mag=seam";
        d.accentRgba = PackRgba8(0xFF, 0x3C, 0xC8);
        d.legend = {
            {PackRgba8(0xF4, 0x4C, 0x42), "River Flow Velocity",  "R"},
            {PackRgba8(0x3E, 0xDC, 0x72), "Optical Depth / 120",  "G"},
            {PackRgba8(0x4C, 0x96, 0xFF), "Shore Alpha Feather",  "B"},
            {PackRgba8(0xFF, 0x1E, 0xDC), "ALARM: Shallow Seam",  "ERR"},
        };
        break;
    default:
        d.shortName  = "W0:Final";
        d.title      = "Final Physical Water Surface";
        d.purpose    = "Normal water rendering (debug off)";
        d.accentRgba = tokens::kAccentPass;
        d.legend = {
            {tokens::kAccentPass, "Physical Water", "WTR"},
        };
        break;
    }
    return d;
}

void FormatDebugLayerHud(const DebugLayerHudSnapshot& snap, UiDrawList& outList)
{
    outList.Clear();
    if (!snap.hudEnabled && !snap.drawerOpen) return;

    const uint32_t s = std::clamp(snap.scale, 1u, 2u);
    const float sf = static_cast<float>(s);
    const bool waterMode = (snap.activeWaterDebugView > 0u);
    const DebugViewDescriptor view = waterMode
        ? GetWaterDebugViewDescriptor(snap.activeWaterDebugView)
        : GetDebugViewDescriptor(snap.activeDebugView);

    const float x0 = static_cast<float>(snap.originX);
    const float y0 = static_cast<float>(snap.originY);
    const float pad = 10.0f * sf;
    const float panelW = (snap.drawerOpen ? 344.0f : 268.0f) * sf;
    const float innerW = panelW - pad * 2.0f;

    const float tabBarH   = snap.drawerOpen ? (24.0f * sf) : 0.0f;
    const float legendH   = snap.drawerOpen ? (static_cast<float>(view.legend.size()) * 14.0f * sf + 20.0f * sf) : 0.0f;
    const float togglesH  = snap.drawerOpen ? (4.0f * 15.0f * sf + 18.0f * sf) : 0.0f;
    const float budgetH   = snap.drawerOpen ? (16.0f * sf + 14.0f * sf + 3.0f * 14.0f * sf + 18.0f * sf) : 0.0f;
    const float sparkH    = (snap.historyFrameMs && snap.historyCount > 0u) ? (40.0f * sf) : 0.0f;
    const float headerH   = 44.0f * sf;
    const float footerH   = 20.0f * sf;
    const float panelH    = pad * 2.0f + headerH + tabBarH + legendH + togglesH + budgetH + sparkH + footerH;

    // 1. Calm dark ink panel with crisp elevation border and active-view semantic accent top bar
    outList.AddPanel(x0, y0, panelW, panelH, 6.0f * sf,
                     tokens::kGround, tokens::kBorderStrong, view.accentRgba, 1.0f);

    float cy = y0 + pad;
    const float cx = x0 + pad;

    // 2. Title + Active DebugView Pill
    outList.AddText(cx, cy + 2.0f * sf, waterMode ? "RAW Water Debug" : "RAW Debug Layers", tokens::kInkPrimary, s);

    const float pillW = static_cast<float>(MeasureTextWidthPx(view.shortName, s)) + 25.0f * sf;
    outList.AddPill(cx + std::max(0.0f, innerW - pillW), cy - 1.0f * sf, view.shortName,
                    tokens::kPillOnBg, tokens::kInkPrimary, tokens::kBorderStrong, view.accentRgba, s);
    cy += 18.0f * sf;

    char perfBuf[80]{};
    const float safeFps = (std::isfinite(snap.fps) && snap.fps >= 0.0f) ? snap.fps : 0.0f;
    const float safeMs  = (std::isfinite(snap.frameMs) && snap.frameMs >= 0.0f) ? snap.frameMs : 0.0f;
    const float safeUi  = (std::isfinite(snap.uiMs) && snap.uiMs >= 0.0f) ? snap.uiMs : 0.0f;
    std::snprintf(perfBuf, sizeof(perfBuf), "%.1f FPS | %.2f ms | UI %.3f ms | %s",
                  static_cast<double>(safeFps), static_cast<double>(safeMs),
                  static_cast<double>(safeUi), snap.isInterior ? "Interior" : "Exterior");
    const uint32_t paceCol = (safeMs <= 8.333f) ? tokens::kAccentPass
                           : ((safeMs <= 16.667f) ? tokens::kAccentLead : tokens::kAccentFail);
    outList.AddText(cx, cy, perfBuf, paceCol, s);
    cy += 13.0f * sf;

    outList.AddText(cx, cy, view.title, tokens::kInkSecondary, s);
    cy += 13.0f * sf;

    if (snap.drawerOpen) {
        // 3. Compact Tab Bar
        static const char* const kTabs[3] = {"Layers", "Perf", "Vars"};
        outList.AddTabBar(cx, cy, innerW, kTabs, 3u, snap.activeTabIndex % 3u, view.accentRgba, s);
        cy += 24.0f * sf;

        // 4. Collapsible Color Legend Swatches for active Lighting or Water DebugView
        outList.AddSectionHeader(cx, cy, innerW, "Legend", view.accentRgba, s, true, true);
        cy += 16.0f * sf;
        for (const auto& sw : view.legend) {
            outList.AddLegendSwatch(cx, cy, sw.rgba, sw.label, sw.code, s);
            cy += 14.0f * sf;
        }
        cy += 4.0f * sf;

        // 5. Live World / Solver Toggle Switches
        outList.AddSectionHeader(cx, cy, innerW, "Live GPU Systems", tokens::kAccentPass, s, true, true);
        cy += 16.0f * sf;
        outList.AddToggle(cx, cy, innerW, "RT Shadows & GI",    snap.rtActive,         tokens::kAccentPass, s);    cy += 15.0f * sf;
        outList.AddToggle(cx, cy, innerW, "Fluid Fire & Water", snap.fluidActive,      tokens::kAccentLead, s);    cy += 15.0f * sf;
        outList.AddToggle(cx, cy, innerW, "Snow Deformation",   snap.snowDeformActive, tokens::kAccentPass, s);    cy += 15.0f * sf;
        outList.AddToggle(cx, cy, innerW, "Dynamic Grass Wind", snap.grassWindActive,  tokens::kAccentEmerald, s); cy += 15.0f * sf;
        cy += 2.0f * sf;

        // 6. Stacked GPU Pass Budget Bar + Live Engine Var Rows
        outList.AddSectionHeader(cx, cy, innerW, "GPU Pass Budget", tokens::kAccentLead, s, true, true);
        cy += 16.0f * sf;

        const float safeLight = (std::isfinite(snap.lightingMs) && snap.lightingMs >= 0.0f) ? snap.lightingMs : 0.0f;
        const float safeFluid = ((std::isfinite(snap.waterMs) && snap.waterMs >= 0.0f) ? snap.waterMs : 0.0f) +
                                ((std::isfinite(snap.fireMs) && snap.fireMs >= 0.0f) ? snap.fireMs : 0.0f);
        const float safeGiCl  = ((std::isfinite(snap.giMs) && snap.giMs >= 0.0f) ? snap.giMs : 0.0f) +
                                ((std::isfinite(snap.cloudsMs) && snap.cloudsMs >= 0.0f) ? snap.cloudsMs : 0.0f);
        const float safePost  = ((std::isfinite(snap.postMs) && snap.postMs >= 0.0f) ? snap.postMs : 0.0f) + safeUi;
        const float totalGpu  = safeLight + safeFluid + safeGiCl + safePost;

        const UiBudgetSegment segs[4] = {
            {"Lighting",    safeLight, tokens::kAccentPass},
            {"Water+Fire",  safeFluid, tokens::kAccentLead},
            {"GI+Clouds",   safeGiCl,  tokens::kAccentEmerald},
            {"Post+UI",     safePost,  tokens::kAccentFail},
        };
        outList.AddBudgetBar(cx, cy, innerW, 12.0f * sf, segs, 4u, 2.0f, 3.0f, s);
        cy += 16.0f * sf;

        const uint32_t gpuVerdict = (totalGpu <= 2.0f) ? tokens::kAccentPass
                                  : ((totalGpu <= 3.0f) ? tokens::kAccentLead : tokens::kAccentFail);
        outList.AddEngineVarFloat(cx, cy, innerW, "RAW GPU Total", totalGpu, "ms", gpuVerdict, s); cy += 14.0f * sf;
        outList.AddEngineVarFloat(cx, cy, innerW, "Lighting + Fluid", safeLight + safeFluid, "ms", tokens::kAccentLead, s); cy += 14.0f * sf;
        outList.AddEngineVarRow(cx, cy, innerW, "Active Layer", view.shortName, view.accentRgba, s); cy += 14.0f * sf;
        cy += 2.0f * sf;
    }

    // 7. Optional 64-sample Frame-Time Sparkline
    if (snap.historyFrameMs && snap.historyCount > 0u) {
        outList.AddSparkline(cx, cy, innerW, 34.0f * sf,
                             snap.historyFrameMs, snap.historyCount,
                             8.333f, 16.667f, 25.0f);
        cy += 40.0f * sf;
    }

    // 8. Hotkey hint strip
    outList.AddDivider(cx, cy, innerW, tokens::kBorder);
    cy += 5.0f * sf;
    outList.AddText(cx, cy, "F9 HUD | F10 Drawer | F11 View (0..36 / W1..W7)", tokens::kInkMuted, s);
}

RawUiLibHost::~RawUiLibHost()
{
    Shutdown();
}

void RawUiLibHost::Shutdown()
{
    if (m_cbBuf && m_cbMapped) { m_cbBuf->Unmap(0, nullptr); m_cbMapped = nullptr; }
    if (m_cmdBuf && m_cmdMapped) { m_cmdBuf->Unmap(0, nullptr); m_cmdMapped = nullptr; }
    if (m_glyphBuf && m_glyphMapped) { m_glyphBuf->Unmap(0, nullptr); m_glyphMapped = nullptr; }
    if (m_sampleBuf && m_sampleMapped) { m_sampleBuf->Unmap(0, nullptr); m_sampleMapped = nullptr; }
    m_tsReadback.Reset();
    m_tsHeap.Reset();
    m_sampleBuf.Reset();
    m_glyphBuf.Reset();
    m_cmdBuf.Reset();
    m_cbBuf.Reset();
    m_pso.Reset();
    m_root.Reset();
    m_heap.Reset();
    m_dev = nullptr;
    m_ready = false;
    m_frameIdx = 0u;
    m_lastUiMs = 0.0f;
    m_hasValidTiming = false;
}

bool RawUiLibHost::Init(ID3D12Device* dev, std::string* outErr, D3D12_SHADER_BYTECODE customShader)
{
    Shutdown();
    if (!dev) {
        if (outErr) *outErr = "RawUiLibHost: null ID3D12Device";
        return false;
    }

    D3D12_SHADER_BYTECODE cs = customShader;
    if (!cs.pShaderBytecode || cs.BytecodeLength == 0) {
#if RAW_UILIB_HAS_EMBEDDED_DXIL
        cs = {kDxil_raw_uilib, sizeof kDxil_raw_uilib};
#else
        if (outErr) *outErr = "RawUiLibHost: no embedded raw_uilib_cs.h and no customShader provided";
        return false;
#endif
    }

    m_dev = dev;

    // Descriptor heap: 4 slots per ring frame (t0 cmds, t1 glyphs, t2 samples, u0 target) -> 16 total
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kRingCount * 4u;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawUiLibHost: CreateDescriptorHeap failed";
        return false;
    }
    m_descInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Root signature: rp[0]=CBV b0, rp[1]=SRV table t0..t2, rp[2]=UAV table u0
    {
        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 3u;
        srvRange.BaseShaderRegister = 0u;

        D3D12_DESCRIPTOR_RANGE uavRange{};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1u;
        uavRange.BaseShaderRegister = 0u;

        D3D12_ROOT_PARAMETER rp[3]{};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rp[0].Descriptor = {0u, 0u};
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[1].DescriptorTable = {1u, &srvRange};
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[2].DescriptorTable = {1u, &uavRange};
        rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd{3u, rp, 0u, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) ||
            FAILED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
            if (outErr) *outErr = "RawUiLibHost: CreateRootSignature failed";
            return false;
        }
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_root.Get();
    pd.CS = cs;
    if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_pso)))) {
        if (outErr) *outErr = "RawUiLibHost: CreateComputePipelineState failed";
        return false;
    }

    const uint64_t cbBytes     = static_cast<uint64_t>(kRingCount) * sizeof(GpuUiLibCB);
    const uint64_t cmdBytes    = static_cast<uint64_t>(kRingCount) * UiDrawList::kMaxCommands * sizeof(GpuUiCmd);
    const uint64_t glyphBytes  = static_cast<uint64_t>(kRingCount) * UiDrawList::kMaxGlyphs * sizeof(uint32_t);
    const uint64_t sampleBytes = static_cast<uint64_t>(kRingCount) * UiDrawList::kMaxSamples * sizeof(float);

    if (!CreateUploadOrReadbackBuffer(dev, cbBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, m_cbBuf) ||
        !CreateUploadOrReadbackBuffer(dev, cmdBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, m_cmdBuf) ||
        !CreateUploadOrReadbackBuffer(dev, glyphBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, m_glyphBuf) ||
        !CreateUploadOrReadbackBuffer(dev, sampleBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, m_sampleBuf)) {
        if (outErr) *outErr = "RawUiLibHost: upload ring buffer allocation failed";
        return false;
    }

    D3D12_RANGE rr{0, 0};
    void* p0 = nullptr; void* p1 = nullptr; void* p2 = nullptr; void* p3 = nullptr;
    if (FAILED(m_cbBuf->Map(0, &rr, &p0)) ||
        FAILED(m_cmdBuf->Map(0, &rr, &p1)) ||
        FAILED(m_glyphBuf->Map(0, &rr, &p2)) ||
        FAILED(m_sampleBuf->Map(0, &rr, &p3))) {
        if (outErr) *outErr = "RawUiLibHost: failed to map upload rings";
        return false;
    }
    m_cbMapped     = static_cast<uint8_t*>(p0);
    m_cmdMapped    = static_cast<uint8_t*>(p1);
    m_glyphMapped  = static_cast<uint8_t*>(p2);
    m_sampleMapped = static_cast<uint8_t*>(p3);
    std::memset(m_cbMapped, 0, static_cast<size_t>(cbBytes));
    std::memset(m_cmdMapped, 0, static_cast<size_t>(cmdBytes));
    std::memset(m_glyphMapped, 0, static_cast<size_t>(glyphBytes));
    std::memset(m_sampleMapped, 0, static_cast<size_t>(sampleBytes));

    // Pre-create static SRVs (t0 cmds, t1 glyphs, t2 samples) for each of the 4 ring slots
    for (uint32_t r = 0u; r < kRingCount; ++r) {
        const uint32_t base = r * 4u;

        D3D12_SHADER_RESOURCE_VIEW_DESC srv0{};
        srv0.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv0.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv0.Format = DXGI_FORMAT_UNKNOWN;
        srv0.Buffer.FirstElement = static_cast<UINT64>(r) * UiDrawList::kMaxCommands;
        srv0.Buffer.NumElements = UiDrawList::kMaxCommands;
        srv0.Buffer.StructureByteStride = sizeof(GpuUiCmd);
        dev->CreateShaderResourceView(m_cmdBuf.Get(), &srv0, CpuAt(base + 0u));

        D3D12_SHADER_RESOURCE_VIEW_DESC srv1{};
        srv1.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv1.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv1.Format = DXGI_FORMAT_UNKNOWN;
        srv1.Buffer.FirstElement = static_cast<UINT64>(r) * UiDrawList::kMaxGlyphs;
        srv1.Buffer.NumElements = UiDrawList::kMaxGlyphs;
        srv1.Buffer.StructureByteStride = sizeof(uint32_t);
        dev->CreateShaderResourceView(m_glyphBuf.Get(), &srv1, CpuAt(base + 1u));

        D3D12_SHADER_RESOURCE_VIEW_DESC srv2{};
        srv2.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv2.Format = DXGI_FORMAT_UNKNOWN;
        srv2.Buffer.FirstElement = static_cast<UINT64>(r) * UiDrawList::kMaxSamples;
        srv2.Buffer.NumElements = UiDrawList::kMaxSamples;
        srv2.Buffer.StructureByteStride = sizeof(float);
        dev->CreateShaderResourceView(m_sampleBuf.Get(), &srv2, CpuAt(base + 2u));
    }

    // 4-frame GPU timestamp query heap + readback buffer (2 queries/frame)
    {
        constexpr uint32_t kTotalQueries = kRingCount * 2u;
        D3D12_QUERY_HEAP_DESC qd{};
        qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qd.Count = kTotalQueries;
        if (FAILED(dev->CreateQueryHeap(&qd, IID_PPV_ARGS(&m_tsHeap))) ||
            !CreateUploadOrReadbackBuffer(dev, static_cast<uint64_t>(kTotalQueries) * sizeof(uint64_t),
                                          D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, m_tsReadback)) {
            m_tsHeap.Reset();
            m_tsReadback.Reset();
        }
    }

    m_ready = true;
    return true;
}

bool RawUiLibHost::HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const
{
    if (!m_tsReadback || m_tsFreqHz == 0u) return false;
    const SIZE_T byteOff = static_cast<SIZE_T>(queryStartIdx) * sizeof(uint64_t);
    const D3D12_RANGE rr{byteOff, byteOff + 2u * sizeof(uint64_t)};
    void* ptr = nullptr;
    if (FAILED(m_tsReadback->Map(0, &rr, &ptr))) return false;
    const auto* ticks = reinterpret_cast<const uint64_t*>(static_cast<const uint8_t*>(ptr) + byteOff);
    const uint64_t t0 = ticks[0];
    const uint64_t t1 = ticks[1];
    const D3D12_RANGE wr{0, 0};
    m_tsReadback->Unmap(0, &wr);
    if (t1 < t0) return false;
    const double ms = static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(m_tsFreqHz);
    if (!std::isfinite(ms) || ms < 0.0 || ms > 5000.0) return false;
    outMs = static_cast<float>(ms);
    return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE RawUiLibHost::CpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawUiLibHost::GpuAt(uint32_t slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * m_descInc;
    return h;
}

void RawUiLibHost::RecordDrawList(ID3D12GraphicsCommandList* cmd,
                                  ID3D12Resource* targetTex,
                                  D3D12_RESOURCE_STATES currentState,
                                  uint32_t width,
                                  uint32_t height,
                                  const UiDrawList& drawList,
                                  bool srgbOutput)
{
    // Strict zero-touch guarantee when empty or invalid
    if (!m_ready || !cmd || !targetTex || !m_cbMapped || drawList.Empty() || width == 0u || height == 0u) {
        return;
    }

    const D3D12_RESOURCE_DESC texDesc = targetTex->GetDesc();
    const uint32_t safeW = std::min(width, static_cast<uint32_t>(texDesc.Width));
    const uint32_t safeH = std::min(height, texDesc.Height);
    if (safeW == 0u || safeH == 0u) return;

    const UiPixelRect dirty = drawList.ComputeDirtyRect(safeW, safeH);
    if (dirty.w == 0u || dirty.h == 0u) return;

    // Harvest 3-frames-old retired timestamp slot (non-blocking)
    if (m_frameIdx >= (kRingCount - 1u)) {
        const uint32_t retired = (m_frameIdx - (kRingCount - 1u)) % kRingCount;
        float ms = 0.0f;
        if (HarvestTimestampPair(retired * 2u, ms)) {
            m_lastUiMs = ms;
            m_hasValidTiming = true;
        }
    }

    const uint32_t ring = m_frameIdx % kRingCount;
    const uint32_t baseSlot = ring * 4u;

    const auto& cmds = drawList.Commands();
    const auto& glyphs = drawList.Glyphs();
    const auto& samples = drawList.Samples();
    const uint32_t cmdCount = static_cast<uint32_t>(std::min<size_t>(cmds.size(), UiDrawList::kMaxCommands));

    if (cmdCount > 0u) {
        std::memcpy(m_cmdMapped + static_cast<size_t>(ring) * UiDrawList::kMaxCommands * sizeof(GpuUiCmd),
                    cmds.data(), static_cast<size_t>(cmdCount) * sizeof(GpuUiCmd));
    }
    if (!glyphs.empty()) {
        const uint32_t gCount = static_cast<uint32_t>(std::min<size_t>(glyphs.size(), UiDrawList::kMaxGlyphs));
        std::memcpy(m_glyphMapped + static_cast<size_t>(ring) * UiDrawList::kMaxGlyphs * sizeof(uint32_t),
                    glyphs.data(), static_cast<size_t>(gCount) * sizeof(uint32_t));
    }
    if (!samples.empty()) {
        const uint32_t sCount = static_cast<uint32_t>(std::min<size_t>(samples.size(), UiDrawList::kMaxSamples));
        std::memcpy(m_sampleMapped + static_cast<size_t>(ring) * UiDrawList::kMaxSamples * sizeof(float),
                    samples.data(), static_cast<size_t>(sCount) * sizeof(float));
    }

    GpuUiLibCB cb{};
    cb.dirtyRect[0]  = dirty.x;
    cb.dirtyRect[1]  = dirty.y;
    cb.dirtyRect[2]  = dirty.w;
    cb.dirtyRect[3]  = dirty.h;
    cb.targetInfo[0] = safeW;
    cb.targetInfo[1] = safeH;
    cb.targetInfo[2] = cmdCount;
    cb.targetInfo[3] = srgbOutput ? 1u : 0u;

    const size_t cbOff = static_cast<size_t>(ring) * sizeof(GpuUiLibCB);
    std::memcpy(m_cbMapped + cbOff, &cb, sizeof(cb));
    const D3D12_GPU_VIRTUAL_ADDRESS cbGpu = m_cbBuf->GetGPUVirtualAddress() + cbOff;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = ResolveUavFormat(texDesc.Format);
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0u;
    uavDesc.Texture2D.PlaneSlice = 0u;
    m_dev->CreateUnorderedAccessView(targetTex, nullptr, &uavDesc, CpuAt(baseSlot + 3u));

    if (currentState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = targetTex;
        b.Transition.StateBefore = currentState;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);
    }

    if (m_tsHeap) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 2u + 0u);
    }

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetComputeRootSignature(m_root.Get());
    cmd->SetPipelineState(m_pso.Get());
    cmd->SetComputeRootConstantBufferView(0u, cbGpu);
    cmd->SetComputeRootDescriptorTable(1u, GpuAt(baseSlot + 0u));
    cmd->SetComputeRootDescriptorTable(2u, GpuAt(baseSlot + 3u));

    const uint32_t gx = (dirty.w + 7u) / 8u;
    const uint32_t gy = (dirty.h + 7u) / 8u;
    cmd->Dispatch(gx, gy, 1u);

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 2u + 1u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 2u + 0u, 2u,
                              m_tsReadback.Get(), static_cast<UINT64>(ring * 2u) * sizeof(uint64_t));
    }

    if (currentState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = targetTex;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        b.Transition.StateAfter = currentState;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);
    } else {
        D3D12_RESOURCE_BARRIER uavB{};
        uavB.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavB.UAV.pResource = targetTex;
        cmd->ResourceBarrier(1, &uavB);
    }

    ++m_frameIdx;
}

} // namespace raw::ui
