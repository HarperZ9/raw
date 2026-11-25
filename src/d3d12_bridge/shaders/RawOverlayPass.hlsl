// RawOverlayPass.hlsl: Zero-Dependency <0.02ms GPU Compute Telemetry & Sparkline HUD Overlay (SM 6.6 / 6.10)
// Original clean-room implementation, MIT License.
//
// Draws a crisp dark-slate translucent telemetry card directly onto a RWTexture2D<float4> display target
// with an embedded 5x7 ASCII bitmap font LUT (ASCII 32..95 + '|') and a 64-sample color-coded GPU
// frame-time sparkline graph. Zero external font textures, zero ImGui dependencies, zero out-of-card writes.

RWTexture2D<float4> g_target : register(u0);

cbuffer OverlayCB : register(b0) {
    uint4  panelRect;       // x = panelX, y = panelY, z = panelW, w = panelH
    uint4  targetAndLayout; // x = texW, y = texH, z = lineCount (0..8), w = glyphScale (1..2)
    uint4  sparkLayout;     // x = sparkCount (0..64), y = sparkStartY, z = sparkHeight, w = flags (bit0=srgb)
    float4 sparkRange;      // x = greenMaxMs, y = amberMaxMs, z = graphMaxMs, w = opacity
    uint4  packedChars[128];// 8 lines x 64 chars (1 uint/char: low 8 bits = ASCII, bits 8..15 = colorTag)
    float4 sparkSamples[16];// 64 frame-time samples in milliseconds (4 per float4)
    float4 lineColors[8];   // Default per-line RGBA colors
    uint4  _pad[4];         // Exact 2560-byte 256B-aligned CB
};

// Clean-room 5x7 ASCII bitmap font table covering ASCII 32 (' ') through 95 ('_').
// Each glyph is 7 rows x 5 columns packed into uint2(lo, hi):
//   lo holds rows 0..5 (row r in bits [r*5 .. r*5+4], bit 4 = leftmost column 0, bit 0 = rightmost column 4)
//   hi holds row 6 in bits [0..4]
#define GLYPH5X7(r0, r1, r2, r3, r4, r5, r6) \
    uint2(((uint)(r0)) | (((uint)(r1)) << 5u) | (((uint)(r2)) << 10u) | \
          (((uint)(r3)) << 15u) | (((uint)(r4)) << 20u) | (((uint)(r5)) << 25u), (uint)(r6))

static const uint2 kFont5x7[64] = {
    // 32 ' '
    GLYPH5X7(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
    // 33 '!'
    GLYPH5X7(0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04),
    // 34 '"'
    GLYPH5X7(0x0A, 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00),
    // 35 '#'
    GLYPH5X7(0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A),
    // 36 '$'
    GLYPH5X7(0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04),
    // 37 '%'
    GLYPH5X7(0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03),
    // 38 '&'
    GLYPH5X7(0x08, 0x14, 0x14, 0x08, 0x15, 0x12, 0x0D),
    // 39 '''
    GLYPH5X7(0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00),
    // 40 '('
    GLYPH5X7(0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02),
    // 41 ')'
    GLYPH5X7(0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08),
    // 42 '*'
    GLYPH5X7(0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00),
    // 43 '+'
    GLYPH5X7(0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00),
    // 44 ','
    GLYPH5X7(0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08),
    // 45 '-'
    GLYPH5X7(0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00),
    // 46 '.'
    GLYPH5X7(0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C),
    // 47 '/'
    GLYPH5X7(0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10),
    // 48 '0'
    GLYPH5X7(0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E),
    // 49 '1'
    GLYPH5X7(0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E),
    // 50 '2'
    GLYPH5X7(0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F),
    // 51 '3'
    GLYPH5X7(0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E),
    // 52 '4'
    GLYPH5X7(0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02),
    // 53 '5'
    GLYPH5X7(0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E),
    // 54 '6'
    GLYPH5X7(0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E),
    // 55 '7'
    GLYPH5X7(0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08),
    // 56 '8'
    GLYPH5X7(0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E),
    // 57 '9'
    GLYPH5X7(0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C),
    // 58 ':'
    GLYPH5X7(0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00),
    // 59 ';'
    GLYPH5X7(0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08),
    // 60 '<'
    GLYPH5X7(0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02),
    // 61 '='
    GLYPH5X7(0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00),
    // 62 '>'
    GLYPH5X7(0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08),
    // 63 '?'
    GLYPH5X7(0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04),
    // 64 '@'
    GLYPH5X7(0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E),
    // 65 'A'
    GLYPH5X7(0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11),
    // 66 'B'
    GLYPH5X7(0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E),
    // 67 'C'
    GLYPH5X7(0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E),
    // 68 'D'
    GLYPH5X7(0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C),
    // 69 'E'
    GLYPH5X7(0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F),
    // 70 'F'
    GLYPH5X7(0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10),
    // 71 'G'
    GLYPH5X7(0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F),
    // 72 'H'
    GLYPH5X7(0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11),
    // 73 'I'
    GLYPH5X7(0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E),
    // 74 'J'
    GLYPH5X7(0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C),
    // 75 'K'
    GLYPH5X7(0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11),
    // 76 'L'
    GLYPH5X7(0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F),
    // 77 'M'
    GLYPH5X7(0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11),
    // 78 'N'
    GLYPH5X7(0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11),
    // 79 'O'
    GLYPH5X7(0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E),
    // 80 'P'
    GLYPH5X7(0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10),
    // 81 'Q'
    GLYPH5X7(0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D),
    // 82 'R'
    GLYPH5X7(0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11),
    // 83 'S'
    GLYPH5X7(0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E),
    // 84 'T'
    GLYPH5X7(0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04),
    // 85 'U'
    GLYPH5X7(0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E),
    // 86 'V'
    GLYPH5X7(0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04),
    // 87 'W'
    GLYPH5X7(0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11),
    // 88 'X'
    GLYPH5X7(0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11),
    // 89 'Y'
    GLYPH5X7(0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04),
    // 90 'Z'
    GLYPH5X7(0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F),
    // 91 '['
    GLYPH5X7(0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E),
    // 92 '\'
    GLYPH5X7(0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01),
    // 93 ']'
    GLYPH5X7(0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E),
    // 94 '^'
    GLYPH5X7(0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00),
    // 95 '_'
    GLYPH5X7(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F)
};

#undef GLYPH5X7

uint readPackedChar(uint lineIdx, uint colIdx) {
    const uint flatIdx = lineIdx * 64u + colIdx;
    const uint4 vec = packedChars[flatIdx >> 2u];
    const uint lane = flatIdx & 3u;
    if (lane == 0u) return vec.x;
    if (lane == 1u) return vec.y;
    if (lane == 2u) return vec.z;
    return vec.w;
}

float readSparkSample(uint idx) {
    const float4 vec = sparkSamples[idx >> 2u];
    const uint lane = idx & 3u;
    if (lane == 0u) return vec.x;
    if (lane == 1u) return vec.y;
    if (lane == 2u) return vec.z;
    return vec.w;
}

bool sampleGlyphPixel(uint asciiCode, uint gx, uint gy) {
    if (gx >= 5u || gy >= 7u) return false;
    // Handle vertical pipe '|' (ASCII 124) explicitly for clean telemetry separators
    if (asciiCode == 124u) {
        return (gx == 2u);
    }
    // Fold lowercase a..z to uppercase A..Z
    if (asciiCode >= 97u && asciiCode <= 122u) {
        asciiCode -= 32u;
    }
    if (asciiCode < 32u || asciiCode > 95u) {
        return false;
    }
    const uint2 bits = kFont5x7[asciiCode - 32u];
    const uint rowMask = (gy < 6u) ? ((bits.x >> (gy * 5u)) & 0x1Fu) : (bits.y & 0x1Fu);
    const uint colBit = (rowMask >> (4u - gx)) & 1u;
    return (colBit != 0u);
}

float3 resolveTagColor(uint tag, float3 lineDefault) {
    if (tag == 1u) return float3(0.46f, 0.88f, 1.00f); // Ice cyan accent
    if (tag == 2u) return float3(0.32f, 0.94f, 0.56f); // Emerald OK
    if (tag == 3u) return float3(1.00f, 0.78f, 0.28f); // Warm amber
    if (tag == 4u) return float3(1.00f, 0.34f, 0.36f); // Crimson alert
    if (tag == 5u) return float3(0.66f, 0.74f, 0.84f); // Muted slate
    return lineDefault;
}

float3 linearToSrgb(float3 c) {
    c = saturate(c);
    const float3 lo = c * 12.92f;
    const float3 hi = 1.055f * pow(max(c, 1.0e-6f.xxx), (1.0f / 2.4f).xxx) - 0.055f;
    return lerp(lo, hi, step(0.0031308f.xxx, c));
}

float3 srgbToLinear(float3 c) {
    c = saturate(c);
    const float3 lo = c / 12.92f;
    const float3 hi = pow(max((c + 0.055f) / 1.055f, 1.0e-6f.xxx), 2.4f.xxx);
    return lerp(lo, hi, step(0.04045f.xxx, c));
}

float roundedBoxSdf(float2 p, float2 halfSize, float radius) {
    const float2 q = abs(p) - (halfSize - radius);
    return length(max(q, 0.0f.xx)) + min(max(q.x, q.y), 0.0f) - radius;
}

[numthreads(8, 8, 1)]
void cs_overlay_draw(uint3 dtid : SV_DispatchThreadID) {
    const uint2 localPx = dtid.xy;
    const uint  panelW  = panelRect.z;
    const uint  panelH  = panelRect.w;
    if (localPx.x >= panelW || localPx.y >= panelH) {
        return;
    }

    const uint2 dstPx = uint2(panelRect.x + localPx.x, panelRect.y + localPx.y);
    if (dstPx.x >= targetAndLayout.x || dstPx.y >= targetAndLayout.y) {
        return;
    }

    // Rounded glass card SDF (r = 6px) — pixels strictly outside rounded corners stay untouched
    const float2 halfSz = float2(panelW, panelH) * 0.5f;
    const float2 centered = (float2(localPx) + 0.5f) - halfSz;
    const float  sdf = roundedBoxSdf(centered, halfSz - 0.5f, 6.0f);
    if (sdf > 0.5f) {
        return;
    }

    const float4 bgSample = g_target[dstPx];
    const bool   isSrgb   = (sparkLayout.w & 1u) != 0u;
    const float3 sceneCol = isSrgb ? srgbToLinear(bgSample.rgb) : max(bgSample.rgb, 0.0f.xxx);

    // 1) Dark-slate frosted glass panel background + crisp 1px border + top accent stripe
    const float  edgeCov    = saturate(0.5f - sdf);
    const float  borderMask = saturate(1.0f - abs(sdf + 0.5f));
    const float  gradY      = saturate(float(localPx.y) / max(float(panelH), 1.0f));
    const float3 glassBase  = lerp(float3(0.032f, 0.045f, 0.068f), float3(0.018f, 0.025f, 0.038f), gradY);
    const float3 borderCol  = float3(0.24f, 0.42f, 0.58f);

    const float  panelAlpha = saturate(sparkRange.w) * edgeCov;
    float3 outCol = lerp(sceneCol, glassBase, panelAlpha);

    // Subtle 2px top header accent highlight bar
    if (localPx.y < 2u && localPx.x >= 4u && localPx.x + 4u < panelW) {
        outCol = lerp(outCol, float3(0.26f, 0.78f, 0.96f), 0.85f * edgeCov);
    }

    // Crisp 1px perimeter border
    outCol = lerp(outCol, borderCol, borderMask * 0.65f);

    // 2) Embedded 5x7 ASCII Bitmap Text Grid (up to 8 lines x 64 chars)
    const uint scale     = clamp(targetAndLayout.w, 1u, 2u);
    const uint lineCount = min(targetAndLayout.z, 8u);
    const uint padX      = 10u * scale;
    const uint padY      = 9u * scale;
    const uint cellW     = 6u * scale;  // 5px glyph + 1px spacing
    const uint cellH     = 10u * scale; // 7px glyph + 3px leading

    if (lineCount > 0u && localPx.x >= padX && localPx.y >= padY) {
        const uint relX = localPx.x - padX;
        const uint relY = localPx.y - padY;
        const uint lineIdx = relY / cellH;
        const uint colIdx  = relX / cellW;
        if (lineIdx < lineCount && colIdx < 64u) {
            const uint inCellX = (relX % cellW) / scale;
            const uint inCellY = (relY % cellH) / scale;
            const uint packed  = readPackedChar(lineIdx, colIdx);
            const uint ascii   = packed & 0xFFu;
            const uint tag     = (packed >> 8u) & 0xFFu;

            bool lit = sampleGlyphPixel(ascii, inCellX, inCellY);
            if (lit) {
                const float3 txtCol = resolveTagColor(tag, lineColors[lineIdx].rgb);
                outCol = lerp(outCol, txtCol, 0.98f);
            } else if (inCellX > 0u && inCellY > 0u) {
                // Subtle 1px drop-shadow behind active glyphs for WCAG legibility
                if (sampleGlyphPixel(ascii, inCellX - 1u, inCellY - 1u)) {
                    outCol *= 0.42f;
                }
            }
        }
    }

    // 3) Live 64-Sample Color-Coded GPU Frame-Time Sparkline Bar Graph
    const uint sparkCount = min(sparkLayout.x, 64u);
    const uint sparkY0    = sparkLayout.y;
    const uint sparkH     = sparkLayout.z;
    const uint sparkX0    = padX;
    const uint sparkW     = (panelW > padX * 2u) ? (panelW - padX * 2u) : 0u;

    if (sparkCount > 0u && sparkH >= 8u && sparkW >= 64u &&
        localPx.x >= sparkX0 && localPx.x < (sparkX0 + sparkW) &&
        localPx.y >= sparkY0 && localPx.y < (sparkY0 + sparkH)) {

        const uint sx = localPx.x - sparkX0;
        const uint sy = localPx.y - sparkY0;

        // Recessed graph well background
        outCol = lerp(outCol, float3(0.010f, 0.014f, 0.022f), 0.72f);

        // Subtle 1px graph frame
        if (sx == 0u || sx + 1u == sparkW || sy == 0u || sy + 1u == sparkH) {
            outCol = lerp(outCol, float3(0.18f, 0.30f, 0.42f), 0.55f);
        } else {
            const float graphMaxMs = max(sparkRange.z, 1.0f);
            const float greenMaxMs = max(sparkRange.x, 0.1f);
            const float amberMaxMs = max(sparkRange.y, greenMaxMs + 0.1f);

            const float innerH = float(sparkH - 2u);
            const float yFromBottom = float((sparkH - 2u) - sy); // 0 at bottom, innerH-1 at top

            // Reference dashed threshold lines at greenMaxMs (8.33ms) and amberMaxMs (16.67ms)
            const uint greenRow = (uint)clamp(round((greenMaxMs / graphMaxMs) * (innerH - 1.0f)), 0.0f, innerH - 1.0f);
            const uint amberRow = (uint)clamp(round((amberMaxMs / graphMaxMs) * (innerH - 1.0f)), 0.0f, innerH - 1.0f);
            const uint rowFromBot = (sparkH - 2u) - sy;
            if (((sx & 3u) < 2u) && rowFromBot == greenRow) {
                outCol = lerp(outCol, float3(0.16f, 0.56f, 0.34f), 0.48f);
            } else if (((sx & 3u) < 2u) && rowFromBot == amberRow) {
                outCol = lerp(outCol, float3(0.76f, 0.52f, 0.14f), 0.48f);
            }

            // Map pixel X to one of the 64 sparkline bins
            const uint innerW = sparkW - 2u;
            const uint binIdx = min(((sx - 1u) * 64u) / max(innerW, 1u), 63u);
            const uint binNext = min((sx * 64u) / max(innerW, 1u), 63u);
            const bool isGap = (innerW >= 128u) && (binIdx != binNext);

            if (!isGap && binIdx < sparkCount) {
                const float ms = max(readSparkSample(binIdx), 0.0f);
                const float normH = saturate(ms / graphMaxMs);
                const float barPx = max(normH * (innerH - 1.0f), (ms > 0.001f) ? 1.5f : 0.0f);

                if (yFromBottom <= barPx) {
                    float3 barCol;
                    if (ms <= greenMaxMs) {
                        barCol = float3(0.14f, 0.86f, 0.44f); // Crisp emerald (< 8.3ms)
                    } else if (ms <= amberMaxMs) {
                        barCol = float3(0.98f, 0.72f, 0.16f); // Warm amber (8.3..16.6ms)
                    } else {
                        barCol = float3(0.98f, 0.24f, 0.26f); // Alert crimson (> 16.6ms)
                    }

                    // Subtle vertical depth shading + crisp bright top cap
                    const float capBoost = (barPx - yFromBottom < 1.5f) ? 1.22f : lerp(0.62f, 0.96f, saturate(yFromBottom / max(barPx, 1.0f)));
                    outCol = lerp(outCol, saturate(barCol * capBoost), 0.94f);
                }
            }
        }
    }

    const float3 finalRgb = isSrgb ? linearToSrgb(outCol) : saturate(outCol);
    g_target[dstPx] = float4(finalRgb, bgSample.a);
}
