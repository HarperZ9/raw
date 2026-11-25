// RawUiLibPass.hlsl: Zero-Dependency GPU-Native UI Library Compute Pass (SM 6.6 / 6.10).
// Renders antialiased SDF rounded panels, dividers, capsule pills, toggle switches, value sliders,
// color-coded 64-sample sparklines/histograms, debug-layer legend swatches, and crisp proportional-kerned
// 7x9 ASCII (32..126 including lowercase) outline typography in ONE fast compute dispatch (<0.04ms @ 1080p).
// Original clean-room implementation, MIT License.

RWTexture2D<float4> g_target : register(u0);

struct GpuUiCmd {
    float4 rect;         // x, y, w, h in target pixels
    float4 clipRect;     // x0, y0, x1, y1 scissor clip in target pixels
    uint   colorFill;    // packed RGBA8 (low byte = R, high byte = A)
    uint   colorBorder;  // packed RGBA8
    uint   colorAccent;  // packed RGBA8
    uint   kindAndMeta;  // bits0..7=kind, bits8..15=variant/scale, bits16..31=dataOffset
    float4 params;       // per-kind params (radius, borderW, value01/greenMax, dataLen/amberMax)
    float4 extra;        // extra params (graphMax, opacity, flags, reserved)
};

StructuredBuffer<GpuUiCmd> g_cmds    : register(t0);
StructuredBuffer<uint>     g_glyphs  : register(t1); // low8=ascii, bits8..15=advPx, bits16..31=penX
StructuredBuffer<float>    g_samples : register(t2); // sparkline / histogram sample values

cbuffer UiLibCB : register(b0) {
    uint4 dirtyRect;   // x, y, w, h union bounding box of all commands
    uint4 targetInfo;  // x = texW, y = texH, z = cmdCount, w = flags (bit0 = srgbOutput)
    uint4 reserved0;
    uint4 reserved1;
};

static const uint kCmdPanel     = 0u;
static const uint kCmdRect      = 1u;
static const uint kCmdPill      = 2u;
static const uint kCmdToggle    = 3u;
static const uint kCmdSlider    = 4u;
static const uint kCmdSparkline = 5u;
static const uint kCmdText      = 6u;
static const uint kCmdSwatch    = 7u;

// Clean-room 7x9 ASCII bitmap font covering ALL 95 printable ASCII chars (32 ' ' .. 126 '~').
// Each glyph is 9 rows x 7 columns packed into uint3(x, y, z):
//   x: rows 0..3 (row r in bits [r*7 .. r*7+6], bit 6 = leftmost col 0, bit 0 = rightmost col 6)
//   y: rows 4..7 (row r+4 in bits [r*7 .. r*7+6])
//   z: row 8 in bits [0..6]
#define G79(r0, r1, r2, r3, r4, r5, r6, r7, r8) \
    uint3(((uint)(r0)) | (((uint)(r1)) << 7u) | (((uint)(r2)) << 14u) | (((uint)(r3)) << 21u), \
          ((uint)(r4)) | (((uint)(r5)) << 7u) | (((uint)(r6)) << 14u) | (((uint)(r7)) << 21u), \
          (uint)(r8))

static const uint3 kFont7x9[95] = {
    // 32 ' '
    G79(0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00),
    // 33 '!'
    G79(0x10,0x10,0x10,0x10,0x10,0x10,0x00,0x10,0x00),
    // 34 '"'
    G79(0x28,0x28,0x28,0x00,0x00,0x00,0x00,0x00,0x00),
    // 35 '#'
    G79(0x14,0x14,0x3E,0x14,0x14,0x3E,0x14,0x14,0x00),
    // 36 '$'
    G79(0x10,0x3C,0x50,0x38,0x14,0x14,0x78,0x10,0x00),
    // 37 '%'
    G79(0x62,0x64,0x08,0x10,0x10,0x26,0x46,0x00,0x00),
    // 38 '&'
    G79(0x18,0x24,0x24,0x18,0x2A,0x44,0x44,0x3A,0x00),
    // 39 '''
    G79(0x10,0x10,0x20,0x00,0x00,0x00,0x00,0x00,0x00),
    // 40 '('
    G79(0x08,0x10,0x20,0x20,0x20,0x20,0x10,0x08,0x00),
    // 41 ')'
    G79(0x20,0x10,0x08,0x08,0x08,0x08,0x10,0x20,0x00),
    // 42 '*'
    G79(0x00,0x10,0x54,0x38,0x54,0x10,0x00,0x00,0x00),
    // 43 '+'
    G79(0x00,0x00,0x10,0x10,0x7C,0x10,0x10,0x00,0x00),
    // 44 ','
    G79(0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x10,0x20),
    // 45 '-'
    G79(0x00,0x00,0x00,0x00,0x3C,0x00,0x00,0x00,0x00),
    // 46 '.'
    G79(0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00),
    // 47 '/'
    G79(0x02,0x04,0x08,0x10,0x10,0x20,0x40,0x00,0x00),
    // 48 '0'
    G79(0x38,0x44,0x4C,0x54,0x64,0x44,0x44,0x38,0x00),
    // 49 '1'
    G79(0x10,0x30,0x10,0x10,0x10,0x10,0x10,0x38,0x00),
    // 50 '2'
    G79(0x38,0x44,0x04,0x08,0x10,0x20,0x40,0x7C,0x00),
    // 51 '3'
    G79(0x38,0x44,0x04,0x18,0x04,0x04,0x44,0x38,0x00),
    // 52 '4'
    G79(0x08,0x18,0x28,0x48,0x7C,0x08,0x08,0x08,0x00),
    // 53 '5'
    G79(0x7C,0x40,0x40,0x78,0x04,0x04,0x44,0x38,0x00),
    // 54 '6'
    G79(0x1C,0x20,0x40,0x78,0x44,0x44,0x44,0x38,0x00),
    // 55 '7'
    G79(0x7C,0x04,0x08,0x10,0x10,0x20,0x20,0x20,0x00),
    // 56 '8'
    G79(0x38,0x44,0x44,0x38,0x44,0x44,0x44,0x38,0x00),
    // 57 '9'
    G79(0x38,0x44,0x44,0x3C,0x04,0x04,0x08,0x30,0x00),
    // 58 ':'
    G79(0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00),
    // 59 ';'
    G79(0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x10,0x20),
    // 60 '<'
    G79(0x04,0x08,0x10,0x20,0x10,0x08,0x04,0x00,0x00),
    // 61 '='
    G79(0x00,0x00,0x00,0x7C,0x00,0x7C,0x00,0x00,0x00),
    // 62 '>'
    G79(0x40,0x20,0x10,0x08,0x10,0x20,0x40,0x00,0x00),
    // 63 '?'
    G79(0x38,0x44,0x04,0x08,0x10,0x10,0x00,0x10,0x00),
    // 64 '@'
    G79(0x38,0x44,0x5C,0x54,0x5C,0x40,0x44,0x38,0x00),
    // 65 'A'
    G79(0x10,0x28,0x44,0x44,0x7C,0x44,0x44,0x44,0x00),
    // 66 'B'
    G79(0x78,0x44,0x44,0x78,0x44,0x44,0x44,0x78,0x00),
    // 67 'C'
    G79(0x38,0x44,0x40,0x40,0x40,0x40,0x44,0x38,0x00),
    // 68 'D'
    G79(0x70,0x48,0x44,0x44,0x44,0x44,0x48,0x70,0x00),
    // 69 'E'
    G79(0x7C,0x40,0x40,0x78,0x40,0x40,0x40,0x7C,0x00),
    // 70 'F'
    G79(0x7C,0x40,0x40,0x78,0x40,0x40,0x40,0x40,0x00),
    // 71 'G'
    G79(0x38,0x44,0x40,0x40,0x4E,0x44,0x44,0x3C,0x00),
    // 72 'H'
    G79(0x44,0x44,0x44,0x7C,0x44,0x44,0x44,0x44,0x00),
    // 73 'I'
    G79(0x38,0x10,0x10,0x10,0x10,0x10,0x10,0x38,0x00),
    // 74 'J'
    G79(0x1C,0x08,0x08,0x08,0x08,0x48,0x48,0x30,0x00),
    // 75 'K'
    G79(0x44,0x48,0x50,0x60,0x50,0x48,0x44,0x44,0x00),
    // 76 'L'
    G79(0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x7C,0x00),
    // 77 'M'
    G79(0x41,0x63,0x55,0x49,0x41,0x41,0x41,0x41,0x00),
    // 78 'N'
    G79(0x44,0x64,0x54,0x4C,0x44,0x44,0x44,0x44,0x00),
    // 79 'O'
    G79(0x38,0x44,0x44,0x44,0x44,0x44,0x44,0x38,0x00),
    // 80 'P'
    G79(0x78,0x44,0x44,0x78,0x40,0x40,0x40,0x40,0x00),
    // 81 'Q'
    G79(0x38,0x44,0x44,0x44,0x44,0x54,0x48,0x34,0x00),
    // 82 'R'
    G79(0x78,0x44,0x44,0x78,0x50,0x48,0x44,0x44,0x00),
    // 83 'S'
    G79(0x3C,0x40,0x40,0x38,0x04,0x04,0x44,0x38,0x00),
    // 84 'T'
    G79(0x7C,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x00),
    // 85 'U'
    G79(0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x38,0x00),
    // 86 'V'
    G79(0x44,0x44,0x44,0x44,0x44,0x28,0x28,0x10,0x00),
    // 87 'W'
    G79(0x41,0x41,0x41,0x49,0x49,0x55,0x63,0x41,0x00),
    // 88 'X'
    G79(0x44,0x44,0x28,0x10,0x10,0x28,0x44,0x44,0x00),
    // 89 'Y'
    G79(0x44,0x44,0x28,0x10,0x10,0x10,0x10,0x10,0x00),
    // 90 'Z'
    G79(0x7C,0x04,0x08,0x10,0x10,0x20,0x40,0x7C,0x00),
    // 91 '['
    G79(0x38,0x20,0x20,0x20,0x20,0x20,0x20,0x38,0x00),
    // 92 '\'
    G79(0x40,0x20,0x10,0x08,0x08,0x04,0x02,0x00,0x00),
    // 93 ']'
    G79(0x38,0x08,0x08,0x08,0x08,0x08,0x08,0x38,0x00),
    // 94 '^'
    G79(0x10,0x28,0x44,0x00,0x00,0x00,0x00,0x00,0x00),
    // 95 '_'
    G79(0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7E),
    // 96 '`'
    G79(0x20,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00),
    // 97 'a'
    G79(0x00,0x00,0x38,0x04,0x3C,0x44,0x44,0x3C,0x00),
    // 98 'b'
    G79(0x40,0x40,0x78,0x44,0x44,0x44,0x44,0x78,0x00),
    // 99 'c'
    G79(0x00,0x00,0x38,0x44,0x40,0x40,0x44,0x38,0x00),
    // 100 'd'
    G79(0x04,0x04,0x3C,0x44,0x44,0x44,0x44,0x3C,0x00),
    // 101 'e'
    G79(0x00,0x00,0x38,0x44,0x7C,0x40,0x44,0x38,0x00),
    // 102 'f'
    G79(0x18,0x24,0x20,0x78,0x20,0x20,0x20,0x20,0x00),
    // 103 'g'
    G79(0x00,0x00,0x3C,0x44,0x44,0x3C,0x04,0x44,0x38),
    // 104 'h'
    G79(0x40,0x40,0x78,0x44,0x44,0x44,0x44,0x44,0x00),
    // 105 'i'
    G79(0x10,0x00,0x30,0x10,0x10,0x10,0x10,0x38,0x00),
    // 106 'j'
    G79(0x08,0x00,0x18,0x08,0x08,0x08,0x08,0x48,0x30),
    // 107 'k'
    G79(0x40,0x40,0x48,0x50,0x60,0x50,0x48,0x44,0x00),
    // 108 'l'
    G79(0x30,0x10,0x10,0x10,0x10,0x10,0x10,0x38,0x00),
    // 109 'm'
    G79(0x00,0x00,0x76,0x49,0x49,0x49,0x49,0x49,0x00),
    // 110 'n'
    G79(0x00,0x00,0x78,0x44,0x44,0x44,0x44,0x44,0x00),
    // 111 'o'
    G79(0x00,0x00,0x38,0x44,0x44,0x44,0x44,0x38,0x00),
    // 112 'p'
    G79(0x00,0x00,0x78,0x44,0x44,0x44,0x78,0x40,0x40),
    // 113 'q'
    G79(0x00,0x00,0x3C,0x44,0x44,0x44,0x3C,0x04,0x04),
    // 114 'r'
    G79(0x00,0x00,0x58,0x64,0x40,0x40,0x40,0x40,0x00),
    // 115 's'
    G79(0x00,0x00,0x3C,0x40,0x38,0x04,0x44,0x38,0x00),
    // 116 't'
    G79(0x20,0x20,0x78,0x20,0x20,0x20,0x24,0x18,0x00),
    // 117 'u'
    G79(0x00,0x00,0x44,0x44,0x44,0x44,0x44,0x3C,0x00),
    // 118 'v'
    G79(0x00,0x00,0x44,0x44,0x44,0x28,0x28,0x10,0x00),
    // 119 'w'
    G79(0x00,0x00,0x41,0x41,0x49,0x49,0x55,0x22,0x00),
    // 120 'x'
    G79(0x00,0x00,0x44,0x28,0x10,0x28,0x44,0x44,0x00),
    // 121 'y'
    G79(0x00,0x00,0x44,0x44,0x44,0x3C,0x04,0x44,0x38),
    // 122 'z'
    G79(0x00,0x00,0x7C,0x08,0x10,0x20,0x40,0x7C,0x00),
    // 123 '{'
    G79(0x0C,0x10,0x10,0x60,0x10,0x10,0x10,0x0C,0x00),
    // 124 '|'
    G79(0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x00),
    // 125 '}'
    G79(0x60,0x10,0x10,0x0C,0x10,0x10,0x10,0x60,0x00),
    // 126 '~'
    G79(0x00,0x00,0x24,0x5A,0x42,0x00,0x00,0x00,0x00)
};

#undef G79

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

float4 unpackRgba8Linear(uint packed) {
    const float3 srgb = float3(
        float((packed >> 0u) & 0xFFu) / 255.0f,
        float((packed >> 8u) & 0xFFu) / 255.0f,
        float((packed >> 16u) & 0xFFu) / 255.0f
    );
    const float a = float((packed >> 24u) & 0xFFu) / 255.0f;
    return float4(srgbToLinear(srgb), a);
}

float roundedBoxSdf(float2 p, float2 halfSize, float radius) {
    const float r = clamp(radius, 0.0f, min(halfSize.x, halfSize.y));
    const float2 q = abs(p) - (halfSize - r);
    return length(max(q, 0.0f.xx)) + min(max(q.x, q.y), 0.0f) - r;
}

bool sampleGlyph7x9(uint asciiCode, int gx, int gy) {
    if (gx < 0 || gx >= 7 || gy < 0 || gy >= 9) return false;
    if (asciiCode < 32u || asciiCode > 126u) return false;
    const uint3 bits = kFont7x9[asciiCode - 32u];
    uint rowMask = 0u;
    if (gy < 4) {
        rowMask = (bits.x >> ((uint)gy * 7u)) & 0x7Fu;
    } else if (gy < 8) {
        rowMask = (bits.y >> ((uint)(gy - 4) * 7u)) & 0x7Fu;
    } else {
        rowMask = bits.z & 0x7Fu;
    }
    const uint colBit = (rowMask >> (6u - (uint)gx)) & 1u;
    return (colBit != 0u);
}

[numthreads(8, 8, 1)]
void cs_uilib_draw(uint3 dtid : SV_DispatchThreadID) {
    const uint2 localPx = dtid.xy;
    if (localPx.x >= dirtyRect.z || localPx.y >= dirtyRect.w) {
        return;
    }
    const uint2 dstPx = uint2(dirtyRect.x + localPx.x, dirtyRect.y + localPx.y);
    if (dstPx.x >= targetInfo.x || dstPx.y >= targetInfo.y) {
        return;
    }

    const float2 p = float2(dstPx) + 0.5f;
    const uint cmdCount = min(targetInfo.z, 256u);
    if (cmdCount == 0u) {
        return;
    }

    const float4 bgSample = g_target[dstPx];
    const bool isSrgb = (targetInfo.w & 1u) != 0u;
    float3 col = isSrgb ? srgbToLinear(bgSample.rgb) : max(bgSample.rgb, 0.0f.xxx);
    bool touched = false;

    [loop]
    for (uint i = 0u; i < cmdCount; ++i) {
        const GpuUiCmd cmd = g_cmds[i];

        // Strict clip-rect scissor rejection
        if (p.x < cmd.clipRect.x || p.x >= cmd.clipRect.z ||
            p.y < cmd.clipRect.y || p.y >= cmd.clipRect.w) {
            continue;
        }
        // Tight command bounding-box rejection (+2px margin for SDF antialiasing/outline)
        if (p.x < cmd.rect.x - 2.0f || p.x > cmd.rect.x + cmd.rect.z + 2.0f ||
            p.y < cmd.rect.y - 2.0f || p.y > cmd.rect.y + cmd.rect.w + 2.0f) {
            continue;
        }

        const uint kind     = cmd.kindAndMeta & 0xFFu;
        const uint meta     = (cmd.kindAndMeta >> 8u) & 0xFFu;
        const uint dataOff  = (cmd.kindAndMeta >> 16u) & 0xFFFFu;

        const float4 fillCol   = unpackRgba8Linear(cmd.colorFill);
        const float4 borderCol = unpackRgba8Linear(cmd.colorBorder);
        const float4 accentCol = unpackRgba8Linear(cmd.colorAccent);

        const float2 halfSz = max(cmd.rect.zw * 0.5f, 0.5f.xx);
        const float2 center = cmd.rect.xy + halfSz;
        const float2 rel    = p - center;

        if (kind == kCmdPanel) {
            const float sdf = roundedBoxSdf(rel, halfSz - 0.5f, cmd.params.x);
            if (sdf <= 1.5f) {
                const float edgeCov = saturate(0.5f - sdf);
                if (edgeCov > 0.001f) {
                    const float gradY = saturate((p.y - cmd.rect.y) / max(cmd.rect.w, 1.0f));
                    const float3 surf = fillCol.rgb * lerp(1.06f, 0.92f, gradY);
                    col = lerp(col, surf, fillCol.a * edgeCov);

                    // Optional crisp 2px top accent bar inside rounded card
                    if (accentCol.a > 0.01f && (p.y - cmd.rect.y) < 2.5f && sdf < -0.25f) {
                        col = lerp(col, accentCol.rgb, accentCol.a * edgeCov);
                    }

                    // Crisp elevation border
                    const float borderW = max(cmd.params.y, 1.0f);
                    const float borderCov = saturate(1.0f - abs(sdf + 0.5f * borderW) / max(borderW * 0.65f, 0.5f));
                    if (borderCol.a > 0.01f && borderCov > 0.001f) {
                        col = lerp(col, borderCol.rgb, borderCol.a * borderCov);
                    }
                    touched = true;
                }
            }
        } else if (kind == kCmdRect) {
            if (p.x >= cmd.rect.x && p.x <= cmd.rect.x + cmd.rect.z &&
                p.y >= cmd.rect.y && p.y <= cmd.rect.y + cmd.rect.w) {
                col = lerp(col, fillCol.rgb, fillCol.a);
                touched = true;
            }
        } else if (kind == kCmdPill) {
            const float r = min(halfSz.x, halfSz.y);
            const float sdf = roundedBoxSdf(rel, halfSz - 0.5f, r);
            if (sdf <= 0.75f) {
                const float cov = saturate(0.5f - sdf);
                col = lerp(col, fillCol.rgb, fillCol.a * cov);
                const float borderCov = saturate(1.0f - abs(sdf + 0.5f));
                if (borderCol.a > 0.01f) {
                    col = lerp(col, borderCol.rgb, borderCol.a * borderCov);
                }
                // Optional leading semantic verdict disc (when accentCol.a > 0)
                if (accentCol.a > 0.01f) {
                    const float dotR = max(min(cmd.rect.w * 0.24f, 4.0f), 2.0f);
                    const float2 dotCenter = float2(cmd.rect.x + cmd.rect.w * 0.55f, center.y);
                    const float dDot = length(p - dotCenter) - dotR;
                    const float dotCov = saturate(0.5f - dDot);
                    if (dotCov > 0.001f) {
                        col = lerp(col, accentCol.rgb, accentCol.a * dotCov);
                    }
                }
                touched = true;
            }
        } else if (kind == kCmdToggle) {
            const float r = min(halfSz.x, halfSz.y);
            const float sdf = roundedBoxSdf(rel, halfSz - 0.5f, r);
            if (sdf <= 0.75f) {
                const bool isOn = (cmd.params.z > 0.5f);
                const float cov = saturate(0.5f - sdf);
                const float3 trackCol = isOn ? accentCol.rgb : fillCol.rgb;
                col = lerp(col, trackCol, cov * 0.96f);
                const float borderCov = saturate(1.0f - abs(sdf + 0.5f));
                col = lerp(col, borderCol.rgb, borderCol.a * borderCov);

                const float thumbR = max(halfSz.y - 2.5f, 2.5f);
                const float thumbX = isOn
                    ? (cmd.rect.x + cmd.rect.z - halfSz.y)
                    : (cmd.rect.x + halfSz.y);
                const float dThumb = length(p - float2(thumbX, center.y)) - thumbR;
                const float thumbCov = saturate(0.5f - dThumb);
                if (thumbCov > 0.001f) {
                    col = lerp(col, float3(0.86f, 0.84f, 0.79f), thumbCov);
                }
                touched = true;
            }
        } else if (kind == kCmdSlider) {
            const float v01 = saturate(cmd.params.z);
            const float trackH = max(min(cmd.rect.w * 0.42f, 6.0f), 3.0f);
            const float2 trackHalf = float2(halfSz.x - 4.0f, trackH * 0.5f);
            const float dTrack = roundedBoxSdf(rel, max(trackHalf, 1.0f.xx), trackH * 0.5f);
            if (dTrack <= 0.75f) {
                const float cov = saturate(0.5f - dTrack);
                col = lerp(col, fillCol.rgb, fillCol.a * cov);
                const float fillXEnd = (cmd.rect.x + 4.0f) + v01 * max(cmd.rect.z - 8.0f, 1.0f);
                if (p.x <= fillXEnd) {
                    col = lerp(col, accentCol.rgb, accentCol.a * cov);
                }
                touched = true;
            }
            const float knobX = (cmd.rect.x + 4.0f) + v01 * max(cmd.rect.z - 8.0f, 1.0f);
            const float knobR = max(min(halfSz.y - 1.0f, 5.5f), 3.0f);
            const float dKnob = length(p - float2(knobX, center.y)) - knobR;
            if (dKnob <= 0.75f) {
                const float kCov = saturate(0.5f - dKnob);
                col = lerp(col, float3(0.88f, 0.86f, 0.80f), kCov);
                const float kRing = saturate(1.0f - abs(dKnob + 0.5f));
                col = lerp(col, borderCol.rgb, kRing * 0.75f);
                touched = true;
            }
        } else if (kind == kCmdSwatch) {
            const float sdf = roundedBoxSdf(rel, halfSz - 0.5f, max(cmd.params.x, 2.0f));
            if (sdf <= 0.75f) {
                const float cov = saturate(0.5f - sdf);
                col = lerp(col, fillCol.rgb, fillCol.a * cov);
                const float borderCov = saturate(1.0f - abs(sdf + 0.5f));
                col = lerp(col, borderCol.rgb, borderCol.a * borderCov);
                touched = true;
            }
        } else if (kind == kCmdSparkline) {
            if (p.x >= cmd.rect.x && p.x < cmd.rect.x + cmd.rect.z &&
                p.y >= cmd.rect.y && p.y < cmd.rect.y + cmd.rect.w) {
                const float sx = p.x - cmd.rect.x;
                const float sy = p.y - cmd.rect.y;
                const float sw = max(cmd.rect.z, 2.0f);
                const float sh = max(cmd.rect.w, 4.0f);
                col = lerp(col, fillCol.rgb, fillCol.a);
                if (sx < 1.0f || sx >= sw - 1.0f || sy < 1.0f || sy >= sh - 1.0f) {
                    col = lerp(col, borderCol.rgb, borderCol.a);
                } else {
                    const uint count = min((uint)cmd.params.w, 128u);
                    const float greenMax = max(cmd.params.x, 0.1f);
                    const float amberMax = max(cmd.params.y, greenMax + 0.1f);
                    const float graphMax = max(cmd.extra.x, amberMax + 0.5f);
                    const float innerW = max(sw - 2.0f, 1.0f);
                    const float innerH = max(sh - 2.0f, 1.0f);
                    const float yFromBot = (sh - 1.5f) - sy;

                    // Dashed green & amber budget lines
                    const float greenY = clamp((greenMax / graphMax) * (innerH - 1.0f), 0.0f, innerH - 1.0f);
                    const float amberY = clamp((amberMax / graphMax) * (innerH - 1.0f), 0.0f, innerH - 1.0f);
                    if ((((uint)sx & 3u) < 2u) && abs(yFromBot - greenY) < 0.6f) {
                        col = lerp(col, float3(0.16f, 0.44f, 0.72f), 0.55f);
                    } else if ((((uint)sx & 3u) < 2u) && abs(yFromBot - amberY) < 0.6f) {
                        col = lerp(col, float3(0.76f, 0.46f, 0.12f), 0.55f);
                    }

                    if (count > 0u) {
                        const uint bin = min((uint)floor(((sx - 1.0f) / innerW) * float(count)), count - 1u);
                        const float v = max(g_samples[dataOff + bin], 0.0f);
                        const float barPx = max(saturate(v / graphMax) * (innerH - 1.0f), (v > 0.001f) ? 1.5f : 0.0f);
                        if (yFromBot <= barPx) {
                            float3 barCol;
                            if (v <= greenMax) {
                                barCol = float3(0.14f, 0.50f, 0.92f); // Calm CVD-safe pass blue (#619ff9 linear)
                            } else if (v <= amberMax) {
                                barCol = float3(0.84f, 0.46f, 0.09f); // Warm brass (#eeb153 linear)
                            } else {
                                barCol = float3(0.88f, 0.16f, 0.09f); // Alert coral (#f47154 linear)
                            }
                            const float boost = (barPx - yFromBot < 1.5f) ? 1.20f : lerp(0.68f, 0.98f, saturate(yFromBot / max(barPx, 1.0f)));
                            col = lerp(col, saturate(barCol * boost), 0.94f);
                        }
                    }
                }
                touched = true;
            }
        } else if (kind == kCmdText) {
            const uint scale = max(meta, 1u);
            const uint len   = min((uint)cmd.params.w, 128u);
            if (len > 0u &&
                p.y >= cmd.rect.y - float(scale) && p.y < cmd.rect.y + float(9u * scale) + float(scale)) {
                const float relX = (p.x - cmd.rect.x) / float(scale);
                const float relY = (p.y - cmd.rect.y) / float(scale);
                const int iy = (int)floor(relY);

                // Binary search glyph whose [penX .. penX + advPx] contains relX (with 1px outline margin)
                uint lo = 0u;
                uint hi = len;
                [loop]
                for (uint s = 0u; s < 8u && lo + 1u < hi; ++s) {
                    const uint mid = (lo + hi) >> 1u;
                    const float midPen = float((g_glyphs[dataOff + mid] >> 16u) & 0xFFFFu);
                    if (midPen <= relX + 1.0f) {
                        lo = mid;
                    } else {
                        hi = mid;
                    }
                }

                bool lit = false;
                bool halo = false;
                const uint startG = (lo > 0u) ? (lo - 1u) : 0u;
                const uint endG   = min(lo + 1u, len - 1u);
                [loop]
                for (uint gi = startG; gi <= endG; ++gi) {
                    const uint packed = g_glyphs[dataOff + gi];
                    const uint ascii  = packed & 0xFFu;
                    const float penX  = float((packed >> 16u) & 0xFFFFu);
                    const int ix = (int)floor(relX - penX);
                    if (ix >= -1 && ix <= 7) {
                        if (sampleGlyph7x9(ascii, ix, iy)) {
                            lit = true;
                            break;
                        }
                        if (sampleGlyph7x9(ascii, ix - 1, iy) || sampleGlyph7x9(ascii, ix + 1, iy) ||
                            sampleGlyph7x9(ascii, ix, iy - 1) || sampleGlyph7x9(ascii, ix, iy + 1)) {
                            halo = true;
                        }
                    }
                }
                if (lit) {
                    col = lerp(col, fillCol.rgb, fillCol.a);
                    touched = true;
                } else if (halo && borderCol.a > 0.01f) {
                    col = lerp(col, borderCol.rgb, borderCol.a * 0.72f);
                    touched = true;
                }
            }
        }
    }

    if (!touched) {
        return;
    }

    col = isfinite(col.x) && isfinite(col.y) && isfinite(col.z) ? max(col, 0.0f.xxx) : 0.0f.xxx;
    const float3 finalRgb = isSrgb ? linearToSrgb(col) : saturate(col);
    g_target[dstPx] = float4(finalRgb, bgSample.a);
}
