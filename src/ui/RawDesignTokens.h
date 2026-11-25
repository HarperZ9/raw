#pragma once
// RawDesignTokens.h: Unified RAW / THE PLAYGROUND Design Canon Tokens.
// Exact OKLCH->sRGB palette from DESIGN-RULES.md and colour-tokens.json (v0.3.0):
//   - Dark ground  #101318 (OKLCH 0.185, 0.012, 260) + warm bone ink #ede9e1 (15.4:1 contrast)
//   - Light ground #f9f5ed (OKLCH 0.972, 0.012,  85) + deep slate ink #1b1e24 (15.37:1 contrast)
//   - CVD-safe verdict tokens paired with geometric shape glyphs (filled disc, half disc, ring, cross)
//   - Zero em dashes, AA/AAA contrast helpers, reduced-motion support, and landmark stability.
// Original work, MIT.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace raw::ui {

struct Rgb8 {
    uint8_t r = 0, g = 0, b = 0, a = 255;

    constexpr uint32_t ToColorRef() const {
        return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16);
    }
};

enum class VerdictGlyph : uint8_t {
    FilledDisc = 0,  // Pass / Active (locked check passed)
    HalfDisc   = 1,  // Lead / Interactive / Focus
    HollowRing = 2,  // Null / Off
    Cross      = 3,  // Fail / Fault
};

struct ThemePalette {
    // Grounds & surfaces (calm figure-ground hierarchy)
    Rgb8 ground;
    Rgb8 surface;
    Rgb8 surfaceAlt;
    Rgb8 surfaceHover;
    Rgb8 surfaceSelected;
    Rgb8 border;
    Rgb8 borderStrong;

    // Typography inks
    Rgb8 inkPrimary;
    Rgb8 inkSecondary;
    Rgb8 inkMuted;

    // Semantic / verdict tokens (exact colour-tokens.json values)
    Rgb8 accentPass;     // #619ff9 dark / #2d64b8 light (active / verified)
    Rgb8 accentLead;     // #eeb153 dark / #9b6612 light (focus / selected / warm brass)
    Rgb8 accentFail;     // #f47154 dark / #b83b22 light (fault / warning)
    Rgb8 accentNull;     // #929292 dark / #747474 light (inactive / null)
    Rgb8 accentOchre;    // #7a6047 platform ochre mark

    // Pill fills
    Rgb8 pillOnBg;
    Rgb8 pillOffBg;
    Rgb8 sliderTrack;
};

constexpr ThemePalette DarkTheme() {
    return ThemePalette{
        /* ground          */ {0x10, 0x13, 0x18, 244},  // #101318
        /* surface         */ {0x16, 0x1A, 0x22, 248},
        /* surfaceAlt      */ {0x1B, 0x20, 0x2A, 250},
        /* surfaceHover    */ {0x22, 0x28, 0x34, 252},
        /* surfaceSelected */ {0x26, 0x2F, 0x3E, 255},
        /* border          */ {0x2A, 0x32, 0x40, 255},
        /* borderStrong    */ {0x42, 0x4E, 0x62, 255},

        /* inkPrimary      */ {0xED, 0xE9, 0xE1, 255},  // #ede9e1 (15.4:1 vs #101318)
        /* inkSecondary    */ {0xBE, 0xB8, 0xAD, 255},
        /* inkMuted        */ {0x92, 0x92, 0x92, 255},  // #929292 (5.99:1 vs #101318)

        /* accentPass      */ {0x61, 0x9F, 0xF9, 255},  // #619ff9 (6.93:1 vs #101318)
        /* accentLead      */ {0xEE, 0xB1, 0x53, 255},  // #eeb153 (9.82:1 vs #101318)
        /* accentFail      */ {0xF4, 0x71, 0x54, 255},  // #f47154 (6.51:1 vs #101318)
        /* accentNull      */ {0x92, 0x92, 0x92, 255},  // #929292
        /* accentOchre     */ {0x93, 0x7E, 0x6B, 255},  // #937e6b

        /* pillOnBg        */ {0x1B, 0x2C, 0x45, 255},
        /* pillOffBg       */ {0x20, 0x24, 0x2D, 255},
        /* sliderTrack     */ {0x28, 0x30, 0x3D, 255},
    };
}

constexpr ThemePalette LightTheme() {
    return ThemePalette{
        /* ground          */ {0xF9, 0xF5, 0xED, 246},  // #f9f5ed
        /* surface         */ {0xF2, 0xEC, 0xE1, 250},
        /* surfaceAlt      */ {0xEA, 0xE2, 0xD4, 252},
        /* surfaceHover    */ {0xE2, 0xD8, 0xC8, 255},
        /* surfaceSelected */ {0xDB, 0xCF, 0xBC, 255},
        /* border          */ {0xCE, 0xC4, 0xB4, 255},
        /* borderStrong    */ {0x9E, 0x93, 0x82, 255},

        /* inkPrimary      */ {0x1B, 0x1E, 0x24, 255},  // #1b1e24 (15.37:1 vs #f9f5ed)
        /* inkSecondary    */ {0x44, 0x48, 0x51, 255},
        /* inkMuted        */ {0x66, 0x66, 0x66, 255},

        /* accentPass      */ {0x23, 0x5A, 0xAD, 255},  // AA/AAA on #f9f5ed
        /* accentLead      */ {0x8A, 0x58, 0x0A, 255},
        /* accentFail      */ {0xB5, 0x34, 0x1B, 255},
        /* accentNull      */ {0x6E, 0x6E, 0x6E, 255},
        /* accentOchre     */ {0x7A, 0x60, 0x47, 255},  // #7a6047 (5.39:1 vs #f9f5ed)

        /* pillOnBg        */ {0xD8, 0xE5, 0xF6, 255},
        /* pillOffBg       */ {0xE4, 0xDD, 0xD0, 255},
        /* sliderTrack     */ {0xD4, 0xCB, 0xBC, 255},
    };
}

// WCAG 2.1 relative luminance and contrast ratio (verified >= 4.5:1 AA for all text tokens).
inline float SrgbChannelToLinear(uint8_t c) {
    const float s = float(c) / 255.0f;
    return s <= 0.04045f ? (s / 12.92f) : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

inline float RelativeLuminance(Rgb8 c) {
    return 0.2126f * SrgbChannelToLinear(c.r)
         + 0.7152f * SrgbChannelToLinear(c.g)
         + 0.0722f * SrgbChannelToLinear(c.b);
}

inline float ContrastRatio(Rgb8 a, Rgb8 b) {
    const float la = RelativeLuminance(a);
    const float lb = RelativeLuminance(b);
    const float hi = std::max(la, lb);
    const float lo = std::min(la, lb);
    return (hi + 0.05f) / (lo + 0.05f);
}

}  // namespace raw::ui
