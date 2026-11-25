// RawStudioUi.cpp: Anti-Aliased SDF Vector + ClearType Studio UI (DESIGN-RULES.md + colour-tokens.json).
// Original work, MIT.
#include "RawStudioUi.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace raw::ui {
namespace {

WorldSimTuning g_worldSim{};

// Layout constants (landmarks keep their place across visits per DESIGN-RULES.md).
constexpr int kHeaderH    = 52;
constexpr int kFooterH    = 34;
constexpr int kRailW      = 206;
constexpr int kInspectW   = 312;
constexpr int kRowH       = 30;
constexpr int kRowPadY    = 12;
constexpr int kSliderW    = 124;
constexpr int kSliderPadR = 16;

struct Canvas {
    uint8_t* bgra = nullptr;
    int w = 0, h = 0;

    inline void BlendPx(int x, int y, Rgb8 c, float alpha) {
        if (x < 0 || y < 0 || x >= w || y >= h || alpha <= 0.002f) return;
        const float a = std::clamp(alpha * (float(c.a) / 255.0f), 0.0f, 1.0f);
        uint8_t* p = bgra + (size_t(y) * size_t(w) + size_t(x)) * 4u;
        const float inv = 1.0f - a;
        p[0] = uint8_t(std::clamp(float(c.b) * a + float(p[0]) * inv, 0.0f, 255.0f));
        p[1] = uint8_t(std::clamp(float(c.g) * a + float(p[1]) * inv, 0.0f, 255.0f));
        p[2] = uint8_t(std::clamp(float(c.r) * a + float(p[2]) * inv, 0.0f, 255.0f));
        p[3] = uint8_t(std::clamp(255.0f * (a + (float(p[3]) / 255.0f) * inv), 0.0f, 255.0f));
    }

    void FillRectSolid(int x0, int y0, int x1, int y1, Rgb8 c) {
        x0 = std::max(x0, 0); y0 = std::max(y0, 0);
        x1 = std::min(x1, w); y1 = std::min(y1, h);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) BlendPx(x, y, c, 1.0f);
        }
    }

    // Signed-distance anti-aliased rounded rectangle with optional 1px hairline stroke.
    void RoundedRect(int x0, int y0, int x1, int y1, float rad, Rgb8 fill, Rgb8 stroke, bool hasStroke) {
        const float cx = 0.5f * float(x0 + x1);
        const float cy = 0.5f * float(y0 + y1);
        const float hx = std::max(0.5f * float(x1 - x0) - rad, 0.0f);
        const float hy = std::max(0.5f * float(y1 - y0) - rad, 0.0f);
        const int ix0 = std::max(x0 - 1, 0), iy0 = std::max(y0 - 1, 0);
        const int ix1 = std::min(x1 + 1, w), iy1 = std::min(y1 + 1, h);
        for (int y = iy0; y < iy1; ++y) {
            const float py = std::fabs(float(y) + 0.5f - cy) - hy;
            for (int x = ix0; x < ix1; ++x) {
                const float px = std::fabs(float(x) + 0.5f - cx) - hx;
                const float qx = std::max(px, 0.0f), qy = std::max(py, 0.0f);
                const float sd = std::sqrt(qx * qx + qy * qy) + std::min(std::max(px, py), 0.0f) - rad;
                const float fillA = std::clamp(0.5f - sd, 0.0f, 1.0f);
                if (fillA > 0.0f) BlendPx(x, y, fill, fillA);
                if (hasStroke) {
                    const float strokeA = std::clamp(1.0f - std::fabs(sd + 0.5f), 0.0f, 1.0f);
                    if (strokeA > 0.0f) BlendPx(x, y, stroke, strokeA);
                }
            }
        }
    }

    // Anti-aliased CVD verdict shape glyph (filled disc, half disc, hollow ring, cross).
    void DrawVerdictGlyph(float cx, float cy, float r, VerdictGlyph g, Rgb8 col) {
        const int x0 = std::max(int(cx - r - 2.0f), 0), y0 = std::max(int(cy - r - 2.0f), 0);
        const int x1 = std::min(int(cx + r + 3.0f), w), y1 = std::min(int(cy + r + 3.0f), h);
        for (int y = y0; y < y1; ++y) {
            const float dy = (float(y) + 0.5f) - cy;
            for (int x = x0; x < x1; ++x) {
                const float dx = (float(x) + 0.5f) - cx;
                const float d = std::sqrt(dx * dx + dy * dy);
                if (g == VerdictGlyph::FilledDisc) {
                    BlendPx(x, y, col, std::clamp(r + 0.5f - d, 0.0f, 1.0f));
                } else if (g == VerdictGlyph::HollowRing) {
                    const float ringA = std::clamp(1.15f - std::fabs(d - (r - 0.6f)), 0.0f, 1.0f);
                    BlendPx(x, y, col, ringA);
                } else if (g == VerdictGlyph::HalfDisc) {
                    const float ringA = std::clamp(1.1f - std::fabs(d - (r - 0.6f)), 0.0f, 1.0f);
                    const float halfA = (dx <= 0.2f) ? std::clamp(r + 0.4f - d, 0.0f, 1.0f) : 0.0f;
                    BlendPx(x, y, col, std::max(ringA, halfA));
                } else {
                    const float d1 = std::fabs(dx - dy) * 0.7071f;
                    const float d2 = std::fabs(dx + dy) * 0.7071f;
                    const float boxA = std::clamp(r + 0.4f - std::max(std::fabs(dx), std::fabs(dy)), 0.0f, 1.0f);
                    const float xA = std::max(std::clamp(1.1f - d1, 0.0f, 1.0f), std::clamp(1.1f - d2, 0.0f, 1.0f)) * boxA;
                    BlendPx(x, y, col, xA);
                }
            }
        }
    }

    // Anti-aliased Wu/SDF line segment for tone curves, aperture iris blades, and cursor.
    void DrawLineAA(float x0, float y0, float x1, float y1, float thick, Rgb8 col) {
        const float minX = std::min(x0, x1) - thick - 1.5f;
        const float maxX = std::max(x0, x1) + thick + 1.5f;
        const float minY = std::min(y0, y1) - thick - 1.5f;
        const float maxY = std::max(y0, y1) + thick + 1.5f;
        const float vx = x1 - x0, vy = y1 - y0;
        const float len2 = std::max(vx * vx + vy * vy, 1.0e-5f);
        const int ix0 = std::max(int(minX), 0), iy0 = std::max(int(minY), 0);
        const int ix1 = std::min(int(maxX) + 1, w), iy1 = std::min(int(maxY) + 1, h);
        for (int y = iy0; y < iy1; ++y) {
            const float py = float(y) + 0.5f - y0;
            for (int x = ix0; x < ix1; ++x) {
                const float px = float(x) + 0.5f - x0;
                const float t = std::clamp((px * vx + py * vy) / len2, 0.0f, 1.0f);
                const float dx = px - vx * t, dy = py - vy * t;
                const float dist = std::sqrt(dx * dx + dy * dy);
                const float a = std::clamp(thick * 0.5f + 0.6f - dist, 0.0f, 1.0f);
                if (a > 0.0f) BlendPx(x, y, col, a);
            }
        }
    }

    // Crisp anti-aliased Studio vector cursor (visible even when Skyrim hides the Win32 cursor).
    void DrawCursor(int mx, int my, Rgb8 fill, Rgb8 outline) {
        static const float kPts[7][2] = {
            {0, 0}, {0, 17}, {4.5f, 13.0f}, {8.2f, 19.5f}, {10.5f, 18.2f}, {6.8f, 11.8f}, {12.5f, 11.8f}
        };
        for (int i = 0; i < 7; ++i) {
            const int j = (i + 1) % 7;
            DrawLineAA(float(mx) + kPts[i][0], float(my) + kPts[i][1],
                       float(mx) + kPts[j][0], float(my) + kPts[j][1], 2.4f, outline);
        }
        for (int i = 0; i < 7; ++i) {
            const int j = (i + 1) % 7;
            DrawLineAA(float(mx) + kPts[i][0], float(my) + kPts[i][1],
                       float(mx) + kPts[j][0], float(my) + kPts[j][1], 1.2f, fill);
        }
    }
};

struct LayoutRects {
    int panelW = 1140, panelH = 660;
    int listX0 = kRailW, listX1 = 828;
    int listY0 = kHeaderH, listY1 = 626;
    int inspX0 = 828, inspX1 = 1140;
    int wheelCx = 984, wheelCy = 150, wheelRad = 54;
    int btnThemeX0 = 0, btnThemeX1 = 0;
    int btnMotionX0 = 0, btnMotionX1 = 0;
    int btnSaveX0 = 0, btnSaveX1 = 0;
    int btnExportX0 = 0, btnExportX1 = 0;
    int btnY0 = 12, btnY1 = 38;
};

LayoutRects ComputeLayout(uint32_t maxW, uint32_t maxH) {
    LayoutRects r{};
    r.panelW = int(std::min<uint32_t>(maxW, 1180u));
    r.panelH = int(std::min<uint32_t>(maxH, 680u));
    const bool showInsp = (r.panelW >= 920);
    r.listX0 = kRailW;
    r.inspX0 = showInsp ? (r.panelW - kInspectW) : r.panelW;
    r.inspX1 = r.panelW;
    r.listX1 = r.inspX0;
    r.listY0 = kHeaderH;
    r.listY1 = r.panelH - kFooterH;
    r.wheelCx = r.inspX0 + (r.inspX1 - r.inspX0) / 2;
    r.wheelCy = kHeaderH + 92;
    r.wheelRad = 54;

    int rx = r.panelW - 14;
    r.btnExportX1 = rx; r.btnExportX0 = rx - 68; rx -= 76;
    r.btnSaveX1   = rx; r.btnSaveX0   = rx - 58; rx -= 66;
    r.btnMotionX1 = rx; r.btnMotionX0 = rx - 76; rx -= 84;
    r.btnThemeX1  = rx; r.btnThemeX0  = rx - 66;
    return r;
}

}  // namespace

WorldSimTuning& GetWorldSimTuning() {
    return g_worldSim;
}

const char* RawStudioUi::DomainName(int idx) {
    static const char* kNames[kDomainCount] = {
        "Pipeline & RT",
        "Geometry & Mesh",
        "Sky & Atmosphere",
        "Weather & Snow",
        "Water & Fluids",
        "Characters & IK",
        "World AI & Combat",
        "Tonemap & Grade",
        "Camera & Optics",
        "Presets & Engine",
    };
    return kNames[std::clamp(idx, 0, kDomainCount - 1)];
}

const char* RawStudioUi::DomainCode(int idx) {
    static const char* kCodes[kDomainCount] = {
        "01", "02", "03", "04", "05", "06", "07", "08", "09", "10"
    };
    return kCodes[std::clamp(idx, 0, kDomainCount - 1)];
}

StudioHitResult RawStudioUi::HitTest(int32_t panelOriginX, int32_t panelOriginY,
                                     uint32_t panelW, uint32_t panelH,
                                     const InputNavFrame& input,
                                     int rowCount, int scrollOffset) const {
    StudioHitResult hit{};
    const LayoutRects L = ComputeLayout(panelW, panelH);
    const int lx = input.mouseX - panelOriginX;
    const int ly = input.mouseY - panelOriginY;
    if (lx < 0 || ly < 0 || lx >= L.panelW || ly >= L.panelH) return hit;

    // Header buttons
    if (ly >= L.btnY0 && ly <= L.btnY1 && input.mousePressed) {
        if (lx >= L.btnThemeX0  && lx <= L.btnThemeX1)  hit.toggleThemeClicked = true;
        if (lx >= L.btnMotionX0 && lx <= L.btnMotionX1) hit.toggleMotionClicked = true;
        if (lx >= L.btnSaveX0   && lx <= L.btnSaveX1)   hit.saveClicked = true;
        if (lx >= L.btnExportX0 && lx <= L.btnExportX1) hit.exportClicked = true;
    }

    // Left domain rail
    if (lx >= 8 && lx < kRailW - 8 && ly >= kHeaderH + 12) {
        const int tabIdx = (ly - (kHeaderH + 12)) / 36;
        if (tabIdx >= 0 && tabIdx < kDomainCount) {
            hit.hoveredTab = tabIdx;
            if (input.mousePressed) hit.clickedTab = tabIdx;
        }
    }

    // Center interactive parameter list & slider hit zones
    if (lx >= L.listX0 + 8 && lx < L.listX1 - 8 && ly >= L.listY0 + kRowPadY && ly < L.listY1 - 6) {
        const int visIdx = (ly - (L.listY0 + kRowPadY)) / kRowH;
        const int rowIdx = visIdx + scrollOffset;
        if (rowIdx >= 0 && rowIdx < rowCount) {
            hit.hoveredRow = rowIdx;
            const int sx1 = L.listX1 - kSliderPadR;
            const int sx0 = sx1 - kSliderW;
            if (input.mouseDown && lx >= sx0 - 6 && lx <= sx1 + 6) {
                hit.sliderDragged = true;
                hit.clickedRow = rowIdx;
                hit.sliderNorm = std::clamp(float(lx - sx0) / float(kSliderW), 0.0f, 1.0f);
            } else if (input.mousePressed) {
                hit.clickedRow = rowIdx;
            }
        }
    }

    // Right inspector interactive Oklab color wheel
    if (L.inspX0 < L.inspX1 && input.mouseDown) {
        const float dx = float(lx - L.wheelCx);
        const float dy = float(ly - L.wheelCy);
        const float r = std::sqrt(dx * dx + dy * dy);
        if (r <= float(L.wheelRad + 6)) {
            hit.wheelClicked = true;
            const float wbNorm = std::clamp(dx / (float(L.wheelRad) * 0.85f), -1.0f, 1.0f);
            const float satNorm = std::clamp(0.5f - dy / (float(L.wheelRad) * 1.3f), 0.0f, 1.0f);
            hit.wheelTempK = std::clamp(6500.0f + wbNorm * 3000.0f, 3500.0f, 9500.0f);
            hit.wheelSat = std::clamp(satNorm * 2.0f, 0.0f, 2.0f);
        }
    }

    return hit;
}

bool RawStudioUi::RenderStudio(uint32_t maxW, uint32_t maxH,
                               int activeTab, int selectedRow, int hoveredRow, int hoveredTab, int scrollOffset,
                               const std::vector<StudioRow>& rows,
                               const StudioTelemetry& tel,
                               const WorldSimTuning& sim,
                               const InputNavFrame& input,
                               int32_t panelOriginX, int32_t panelOriginY,
                               const char* footerHint,
                               std::vector<uint8_t>& outBgra, uint32_t& outW, uint32_t& outH) const {
    const LayoutRects L = ComputeLayout(maxW, maxH);
    const ThemePalette P = sim.darkTheme ? DarkTheme() : LightTheme();
    outW = uint32_t(L.panelW);
    outH = uint32_t(L.panelH);

    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = L.panelW;
    bi.bmiHeader.biHeight = -L.panelH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) { DeleteDC(dc); return false; }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);

    Canvas cv{static_cast<uint8_t*>(bits), L.panelW, L.panelH};
    std::memset(bits, 0, size_t(L.panelW) * size_t(L.panelH) * 4u);

    // 1. Calm architectural frame + hairline border + subtle ochre platform accent bar
    cv.RoundedRect(0, 0, L.panelW, L.panelH, 10.0f, P.ground, P.borderStrong, true);
    cv.FillRectSolid(1, 1, L.panelW - 1, kHeaderH, P.surface);
    cv.FillRectSolid(1, 1, L.panelW - 1, 3, P.accentLead);
    cv.FillRectSolid(1, kHeaderH, L.panelW - 1, kHeaderH + 1, P.border);
    cv.FillRectSolid(1, L.panelH - kFooterH, L.panelW - 1, L.panelH - 1, P.surface);
    cv.FillRectSolid(1, L.panelH - kFooterH, L.panelW - 1, L.panelH - kFooterH + 1, P.border);

    // Left rail and right inspector vertical hairlines
    cv.FillRectSolid(kRailW, kHeaderH + 1, kRailW + 1, L.panelH - kFooterH, P.border);
    if (L.inspX0 < L.inspX1) {
        cv.FillRectSolid(L.inspX0, kHeaderH + 1, L.inspX0 + 1, L.panelH - kFooterH, P.border);
        cv.FillRectSolid(L.inspX0 + 1, kHeaderH + 1, L.panelW - 1, L.panelH - kFooterH, P.surface);
    }

    // Header pill buttons
    auto drawBtn = [&](int x0, int x1, bool active) {
        cv.RoundedRect(x0, L.btnY0, x1, L.btnY1, 5.0f, active ? P.pillOnBg : P.surfaceAlt, active ? P.accentPass : P.border, true);
    };
    drawBtn(L.btnThemeX0, L.btnThemeX1, !sim.darkTheme);
    drawBtn(L.btnMotionX0, L.btnMotionX1, sim.reducedMotion);
    drawBtn(L.btnSaveX0, L.btnSaveX1, false);
    drawBtn(L.btnExportX0, L.btnExportX1, false);

    // 2. Left navigation rail (10 fixed-position domains)
    for (int i = 0; i < kDomainCount; ++i) {
        const int y0 = kHeaderH + 12 + i * 36;
        const int y1 = y0 + 30;
        const bool isAct = (i == activeTab);
        const bool isHov = (i == hoveredTab);
        if (isAct) {
            cv.RoundedRect(10, y0, kRailW - 10, y1, 6.0f, P.surfaceSelected, P.borderStrong, true);
            cv.FillRectSolid(10, y0 + 5, 13, y1 - 5, P.accentLead);
        } else if (isHov) {
            cv.RoundedRect(10, y0, kRailW - 10, y1, 6.0f, P.surfaceHover, P.border, false);
        }
        cv.DrawVerdictGlyph(24.0f, float(y0 + y1) * 0.5f, 3.6f,
                            isAct ? VerdictGlyph::FilledDisc : VerdictGlyph::HollowRing,
                            isAct ? P.accentPass : P.accentNull);
    }

    // 3. Center parameter rows (rounded hover/selection cards + CVD pills + AA sliders)
    const int maxVisibleRows = std::max(1, (L.listY1 - L.listY0 - kRowPadY - 8) / kRowH);
    const int endRow = std::min<int>(int(rows.size()), scrollOffset + maxVisibleRows);
    const int sx1 = L.listX1 - kSliderPadR;
    const int sx0 = sx1 - kSliderW;

    for (int r = scrollOffset; r < endRow; ++r) {
        const int vis = r - scrollOffset;
        const int y0 = L.listY0 + kRowPadY + vis * kRowH;
        const int y1 = y0 + kRowH - 3;
        const bool isSel = (r == selectedRow);
        const bool isHov = (r == hoveredRow);
        const auto& row = rows[size_t(r)];

        if (isSel) {
            cv.RoundedRect(L.listX0 + 10, y0, L.listX1 - 10, y1, 5.0f, P.surfaceSelected, P.borderStrong, true);
            cv.FillRectSolid(L.listX0 + 10, y0 + 4, L.listX0 + 13, y1 - 4, P.accentLead);
        } else if (isHov) {
            cv.RoundedRect(L.listX0 + 10, y0, L.listX1 - 10, y1, 5.0f, P.surfaceHover, P.border, false);
        } else if ((vis & 1) == 1) {
            cv.FillRectSolid(L.listX0 + 12, y0, L.listX1 - 12, y1, P.surfaceAlt);
        }

        const float midY = 0.5f * float(y0 + y1);
        if (row.kind == RowKind::Toggle) {
            const int px0 = sx1 - 62;
            cv.RoundedRect(px0, y0 + 4, sx1, y1 - 4, 9.0f,
                           row.boolVal ? P.pillOnBg : P.pillOffBg,
                           row.boolVal ? P.accentPass : P.border, true);
            cv.DrawVerdictGlyph(float(px0 + 12), midY, 3.6f,
                                row.boolVal ? VerdictGlyph::FilledDisc : VerdictGlyph::HollowRing,
                                row.boolVal ? P.accentPass : P.accentNull);
        } else if (row.kind == RowKind::Slider) {
            const int sy0 = int(midY) - 2;
            cv.RoundedRect(sx0, sy0, sx1, sy0 + 5, 2.5f, P.sliderTrack, P.border, false);
            const float norm = std::clamp(row.normVal, 0.0f, 1.0f);
            const int fillX = sx0 + int(norm * float(kSliderW));
            if (fillX > sx0 + 2) {
                cv.RoundedRect(sx0, sy0, fillX, sy0 + 5, 2.5f, isSel ? P.accentLead : P.accentPass, P.border, false);
            }
            cv.DrawVerdictGlyph(float(std::clamp(fillX, sx0 + 4, sx1 - 4)), midY, 5.0f,
                                VerdictGlyph::FilledDisc, isSel ? P.accentLead : P.inkPrimary);
        } else {
            cv.DrawVerdictGlyph(float(L.listX0 + 22), midY, 3.2f, row.glyph,
                                row.activeAccent ? P.accentLead : P.accentPass);
        }
    }

    // 4. Right live visual instrument bay
    if (L.inspX0 < L.inspX1) {
        // 4a. Interactive Oklab Color Wheel (smooth circular boundary)
        const int rW = L.wheelRad;
        for (int dy = -rW - 1; dy <= rW + 1; ++dy) {
            for (int dx = -rW - 1; dx <= rW + 1; ++dx) {
                const float dist = std::sqrt(float(dx * dx + dy * dy));
                const float a = std::clamp(float(rW) + 0.5f - dist, 0.0f, 1.0f);
                if (a <= 0.0f) continue;
                const float rn = std::min(dist / float(rW), 1.0f);
                const float ang = std::atan2(float(dy), float(dx));
                const float hDeg = (ang < 0.0f ? ang + 6.2831853f : ang) * (180.0f / 3.14159265f);
                const float c = rn, x = c * (1.0f - std::fabs(std::fmod(hDeg / 60.0f, 2.0f) - 1.0f));
                const float m = 1.0f - c * 0.50f;
                float rr = 0, gg = 0, bb = 0;
                if (hDeg < 60) { rr = c; gg = x; } else if (hDeg < 120) { rr = x; gg = c; }
                else if (hDeg < 180) { gg = c; bb = x; } else if (hDeg < 240) { gg = x; bb = c; }
                else if (hDeg < 300) { rr = x; bb = c; } else { rr = c; bb = x; }
                Rgb8 wc{uint8_t(std::clamp((rr + m * 0.35f) * 235.0f, 0.0f, 255.0f)),
                        uint8_t(std::clamp((gg + m * 0.35f) * 235.0f, 0.0f, 255.0f)),
                        uint8_t(std::clamp((bb + m * 0.35f) * 235.0f, 0.0f, 255.0f)), 255};
                cv.BlendPx(L.wheelCx + dx, L.wheelCy + dy, wc, a);
            }
        }
        cv.DrawVerdictGlyph(float(L.wheelCx), float(L.wheelCy), float(rW + 1), VerdictGlyph::HollowRing, P.borderStrong);
        const float wbNorm = std::clamp((tel.colorTempK - 6500.0f) / 3000.0f, -1.0f, 1.0f);
        const float satNorm = std::clamp(tel.oklabSat * 0.5f, 0.0f, 1.0f);
        const float dotX = float(L.wheelCx) + wbNorm * float(rW) * 0.85f;
        const float dotY = float(L.wheelCy) - (satNorm - 0.5f) * float(rW) * 1.3f;
        cv.DrawVerdictGlyph(dotX, dotY, 5.2f, VerdictGlyph::FilledDisc, P.ground);
        cv.DrawVerdictGlyph(dotX, dotY, 3.6f, VerdictGlyph::FilledDisc, P.accentLead);

        // 4b. Mechanical Aperture Iris (anti-aliased blades)
        const int iCy = L.wheelCy + 132;
        const float openT = std::clamp(1.0f - (std::log2(std::max(tel.dofFStop, 0.95f)) / 4.0f), 0.18f, 0.94f);
        const float iRad = 38.0f * openT;
        cv.DrawVerdictGlyph(float(L.wheelCx), float(iCy), 42.0f, VerdictGlyph::HollowRing, P.borderStrong);
        const int sh = std::clamp(tel.dofBokehShape, 0, 6);
        const int nb = (sh == 0) ? 5 : (sh == 1) ? 6 : (sh == 2) ? 7 : (sh == 3) ? 9 : 24;
        const float rx = (sh == 5) ? iRad * 0.62f : iRad;
        const float ry = (sh == 5) ? iRad * 1.18f : iRad;
        for (int k = 0; k < nb; ++k) {
            const float a0 = float(k) * (6.2831853f / float(nb)) - 1.5707963f;
            const float a1 = float(k + 1) * (6.2831853f / float(nb)) - 1.5707963f;
            cv.DrawLineAA(float(L.wheelCx) + std::cos(a0) * rx, float(iCy) + std::sin(a0) * ry,
                          float(L.wheelCx) + std::cos(a1) * rx, float(iCy) + std::sin(a1) * ry,
                          1.8f, P.accentLead);
        }

        // 4c. Live HDR Tone Response Curve Graph
        const int gx0 = L.inspX0 + 18, gy0 = iCy + 76, gw = kInspectW - 36, gh = 86;
        cv.RoundedRect(gx0, gy0, gx0 + gw, gy0 + gh, 6.0f, P.surfaceAlt, P.border, true);
        // Subtle 25/50/75 grid hairlines
        for (int g = 1; g <= 3; ++g) {
            cv.FillRectSolid(gx0 + (gw * g) / 4, gy0 + 2, gx0 + (gw * g) / 4 + 1, gy0 + gh - 2, P.border);
            cv.FillRectSolid(gx0 + 2, gy0 + (gh * g) / 4, gx0 + gw - 2, gy0 + (gh * g) / 4 + 1, P.border);
        }
        const float expGain = std::pow(2.0f, tel.exposureBias);
        const float knee = std::max(tel.highlightKnee, 0.25f);
        float prevX = float(gx0), prevY = float(gy0 + gh - 2);
        for (int xi = 0; xi <= gw; xi += 3) {
            const float xIn = (float(xi) / float(gw)) * 2.8f * expGain;
            const float yOut = std::clamp((xIn / (xIn + knee)) + tel.shadowLift, 0.0f, 1.0f);
            const float curX = float(gx0 + xi);
            const float curY = float(gy0 + gh - 3) - yOut * float(gh - 6);
            if (xi > 0) cv.DrawLineAA(prevX, prevY, curX, curY, 2.0f, P.accentPass);
            prevX = curX; prevY = curY;
        }

        // 4d. Live Telemetry & World Simulation Status Card
        const int cy0 = gy0 + gh + 18;
        const int cy1 = L.panelH - kFooterH - 12;
        if (cy1 > cy0 + 48) {
            cv.RoundedRect(gx0, cy0, gx0 + gw, cy1, 6.0f, P.surfaceAlt, P.border, true);
            cv.DrawVerdictGlyph(float(gx0 + 14), float(cy0 + 18), 4.0f,
                                tel.bridgeReady ? VerdictGlyph::FilledDisc : VerdictGlyph::Cross,
                                tel.bridgeReady ? P.accentPass : P.accentFail);
        }
    }

    // 5. Crisp ClearType typography pass (Hanken Grotesk fallback -> Segoe UI + Consolas)
    GdiFlush();
    SetBkMode(dc, TRANSPARENT);
    HFONT fTitle = CreateFontA(16, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
    HFONT fUi    = CreateFontA(15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
    HFONT fSmall = CreateFontA(13, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
    HFONT fMono  = CreateFontA(14, 0, 0, 0, FW_MEDIUM, 0, 0, 0, ANSI_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");
    HGDIOBJ oldF = SelectObject(dc, fTitle);

    auto txt = [&](int x, int y, Rgb8 col, HFONT fnt, const char* s) {
        SelectObject(dc, fnt);
        SetTextColor(dc, col.ToColorRef());
        TextOutA(dc, x, y, s, int(std::strlen(s)));
    };

    // Header copy
    txt(16, 15, P.inkPrimary, fTitle, "THE PLAYGROUND  |  RAW DX12 STUDIO");
    char perfBuf[160];
    std::snprintf(perfBuf, sizeof perfBuf, "%.1f FPS  %.1f ms  |  %s  |  %s%s",
                  tel.fps, tel.frameMs,
                  tel.sm69Active ? "SM 6.9 DXIL 1.9" : "SM 6.6",
                  input.activeDevice == InputDevice::Gamepad ? "PAD" : "KBM",
                  tel.statusMessage.empty() ? "" : tel.statusMessage.c_str());
    txt(318, 17, P.inkSecondary, fMono, perfBuf);

    txt(L.btnThemeX0 + 12,  L.btnY0 + 6, P.inkPrimary, fSmall, sim.darkTheme ? "LIGHT" : "DARK");
    txt(L.btnMotionX0 + 10, L.btnY0 + 6, P.inkPrimary, fSmall, sim.reducedMotion ? "STILL" : "MOTION");
    txt(L.btnSaveX0 + 13,   L.btnY0 + 6, P.inkPrimary, fSmall, "SAVE");
    txt(L.btnExportX0 + 11, L.btnY0 + 6, P.inkPrimary, fSmall, "EXPORT");

    // Left domain rail labels
    for (int i = 0; i < kDomainCount; ++i) {
        const int y0 = kHeaderH + 12 + i * 36;
        const bool isAct = (i == activeTab);
        txt(36, y0 + 7, isAct ? P.accentLead : P.inkMuted, fMono, DomainCode(i));
        txt(58, y0 + 7, isAct ? P.inkPrimary : P.inkSecondary, fUi, DomainName(i));
    }

    // Center row labels & readouts
    for (int r = scrollOffset; r < endRow; ++r) {
        const int vis = r - scrollOffset;
        const int y0 = L.listY0 + kRowPadY + vis * kRowH;
        const bool isSel = (r == selectedRow);
        const auto& row = rows[size_t(r)];
        txt(L.listX0 + 34, y0 + 6, isSel ? P.accentLead : P.inkPrimary, fUi, row.label.c_str());
        if (row.kind == RowKind::Toggle) {
            txt(sx1 - 40, y0 + 6, row.boolVal ? P.accentPass : P.inkMuted, fMono, row.boolVal ? "ON" : "OFF");
        } else if (row.kind == RowKind::Slider) {
            txt(sx0 - 78, y0 + 6, isSel ? P.inkPrimary : P.inkSecondary, fMono, row.valueText.c_str());
        } else {
            const int vX = std::max(L.listX0 + 230, sx1 - int(row.valueText.size()) * 8 - 8);
            txt(vX, y0 + 6, isSel ? P.accentLead : P.inkSecondary, fMono, row.valueText.c_str());
        }
    }

    // Right inspector typography
    if (L.inspX0 < L.inspX1) {
        txt(L.inspX0 + 18, kHeaderH + 14, P.inkMuted, fSmall, "OKLAB GRADE WHEEL (CLICK)");
        char wbStr[48];
        std::snprintf(wbStr, sizeof wbStr, "%.0f K  |  SAT %.2fx", tel.colorTempK, tel.oklabSat);
        txt(L.inspX0 + 18, L.wheelCy + L.wheelRad + 8, P.inkSecondary, fMono, wbStr);

        const int iCy = L.wheelCy + 132;
        char irisStr[48];
        std::snprintf(irisStr, sizeof irisStr, "APERTURE  %.0fmm f/%.1f", tel.dofFocalMm, tel.dofFStop);
        txt(L.inspX0 + 18, iCy - 56, P.inkMuted, fSmall, irisStr);

        const int gy0 = iCy + 76;
        txt(L.inspX0 + 18, gy0 - 18, P.inkMuted, fSmall, "HDR TONE RESPONSE");

        const int cy0 = gy0 + 86 + 18;
        if (L.panelH - kFooterH - 12 > cy0 + 48) {
            txt(L.inspX0 + 42, cy0 + 11, tel.bridgeReady ? P.inkPrimary : P.accentFail, fUi,
                tel.bridgeReady ? "DX12 BRIDGE VERIFIED" : "BRIDGE INACTIVE");
            char t1[64], t2[64], t3[64];
            std::snprintf(t1, sizeof t1, "Subdiv: %dx  Snow: %.1fu", sim.geomSubdivLevel, tel.snowDepthAvgU);
            std::snprintf(t2, sizeof t2, "NavMesh: %d  AI: %d  IK: %d", tel.activeNavNodes, tel.activeAiAgents, tel.activeIkActors);
            std::snprintf(t3, sizeof t3, "Zero-SKSE Runtime Active");
            txt(L.inspX0 + 28, cy0 + 34, P.inkSecondary, fMono, t1);
            txt(L.inspX0 + 28, cy0 + 52, P.inkSecondary, fMono, t2);
            txt(L.inspX0 + 28, cy0 + 70, P.accentPass, fSmall, t3);
        }
    }

    // Footer control legend
    txt(16, L.panelH - kFooterH + 10, P.inkMuted, fMono, footerHint ? footerHint : "");

    GdiFlush();

    // 6. Draw anti-aliased mouse cursor on top when KeyboardMouse mode is active
    if (input.activeDevice == InputDevice::KeyboardMouse) {
        const int mx = input.mouseX - panelOriginX;
        const int my = input.mouseY - panelOriginY;
        if (mx >= 0 && my >= 0 && mx < L.panelW && my < L.panelH) {
            cv.DrawCursor(mx, my, P.inkPrimary, P.ground);
        }
    }

    outBgra.assign(static_cast<uint8_t*>(bits), static_cast<uint8_t*>(bits) + size_t(L.panelW) * size_t(L.panelH) * 4u);

    SelectObject(dc, oldF);
    SelectObject(dc, oldBmp);
    DeleteObject(fTitle); DeleteObject(fUi); DeleteObject(fSmall); DeleteObject(fMono);
    DeleteObject(bmp);
    DeleteDC(dc);
    return true;
}

bool RawStudioUi::RenderBanner(const std::string& text, uint32_t maxW, bool darkTheme,
                               std::vector<uint8_t>& outBgra, uint32_t& outW, uint32_t& outH) const {
    const ThemePalette P = darkTheme ? DarkTheme() : LightTheme();
    const int w = int(std::min<uint32_t>(maxW, 920u));
    const int h = 44;
    outW = uint32_t(w); outH = uint32_t(h);

    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) { DeleteDC(dc); return false; }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);

    Canvas cv{static_cast<uint8_t*>(bits), w, h};
    std::memset(bits, 0, size_t(w) * size_t(h) * 4u);
    cv.RoundedRect(0, 0, w, h, 8.0f, P.ground, P.accentFail, true);
    cv.DrawVerdictGlyph(20.0f, 22.0f, 5.0f, VerdictGlyph::Cross, P.accentFail);

    GdiFlush();
    SetBkMode(dc, TRANSPARENT);
    HFONT fUi = CreateFontA(15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
    HGDIOBJ oldF = SelectObject(dc, fUi);
    SetTextColor(dc, P.inkPrimary.ToColorRef());
    TextOutA(dc, 38, 13, text.c_str(), int(std::min<size_t>(text.size(), 110u)));
    GdiFlush();

    outBgra.assign(static_cast<uint8_t*>(bits), static_cast<uint8_t*>(bits) + size_t(w) * size_t(h) * 4u);
    SelectObject(dc, oldF);
    SelectObject(dc, oldBmp);
    DeleteObject(fUi);
    DeleteObject(bmp);
    DeleteDC(dc);
    return true;
}

}  // namespace raw::ui
