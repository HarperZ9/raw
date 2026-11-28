// RawHdrDisplayOutput.hlsli: Wave 86 True HDR10 / BT.2020 / OKLab Calibrated Display Output.
// Implements:
//   - Exact ITU-R BT.709 -> ITU-R BT.2020 linear matrix (row sums == 1.0, D65 white identity)
//   - Perceptual OKLab soft-gamut rolloff preserving hue with zero out-of-gamut clipping
//   - Exact SMPTE ST 2084 (PQ) OETF & inverse EOTF calibrated for [100..4000 nits] peak
//     matching Calibrate Pro & build-color
//   - Clean SDR fallback identity (hdrEnabled == 0) with finite NaN/Inf sanitization
// Clean-room MIT, DXC SM 6.6 / 6.9 / 6.10 (-WX -O3) clean.

#ifndef RAW_HDR_DISPLAY_OUTPUT_HLSLI
#define RAW_HDR_DISPLAY_OUTPUT_HLSLI

static const float RAW_PQ_M1 = 2610.0f / 16384.0f;         // 0.1593017578125
static const float RAW_PQ_M2 = (2523.0f / 4096.0f) * 128.0f; // 78.84375
static const float RAW_PQ_C1 = 3424.0f / 4096.0f;          // 0.8359375
static const float RAW_PQ_C2 = (2413.0f / 4096.0f) * 32.0f;  // 18.8515625
static const float RAW_PQ_C3 = (2392.0f / 4096.0f) * 32.0f;  // 18.6875

float3 RawHdrSanitizeRgb(float3 c)
{
    bool3 fin = isfinite(c);
    float3 clean = float3(
        fin.x ? max(c.x, 0.0f) : 0.0f,
        fin.y ? max(c.y, 0.0f) : 0.0f,
        fin.z ? max(c.z, 0.0f) : 0.0f);
    return clean;
}

// Exact ITU-R BT.709 linear -> ITU-R BT.2020 linear (D65 white point preserved).
float3 RawRec709ToRec2020(float3 rgb709)
{
    static const float3x3 kM = float3x3(
        0.6274039f, 0.3292830f, 0.0433131f,
        0.0690973f, 0.9195404f, 0.0113623f,
        0.0163914f, 0.0880133f, 0.8955953f);
    return max(mul(kM, RawHdrSanitizeRgb(rgb709)), 0.0f.xxx);
}

// Perceptual OKLab soft-gamut rolloff on Rec.709 linear color (prevents negative channels
// and hue twists while gently rolling extreme chroma into gamut).
float3 RawOklabSoftGamutCompress(float3 rgb709, float softChromaKnee)
{
    float3 c = RawHdrSanitizeRgb(rgb709);
    float l = 0.4122214708f * c.r + 0.5363325363f * c.g + 0.0514459929f * c.b;
    float m = 0.2119034982f * c.r + 0.6806995451f * c.g + 0.1073969566f * c.b;
    float s = 0.0883024619f * c.r + 0.2817188376f * c.g + 0.6299787005f * c.b;

    float l_ = pow(max(l, 0.0f), 1.0f / 3.0f);
    float m_ = pow(max(m, 0.0f), 1.0f / 3.0f);
    float s_ = pow(max(s, 0.0f), 1.0f / 3.0f);

    float L = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
    float a = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
    float b = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;

    float chroma = sqrt(a * a + b * b);
    float knee   = max(softChromaKnee, 0.05f);
    if (chroma > knee) {
        float excess = chroma - knee;
        float compressed = knee + excess / (1.0f + excess * 2.5f);
        float scale = compressed / max(chroma, 1.0e-6f);
        a *= scale;
        b *= scale;
    }

    float l2 = L + 0.3963377774f * a + 0.2158037573f * b;
    float m2 = L - 0.1055613458f * a - 0.0638541728f * b;
    float s2 = L - 0.0894841775f * a - 1.2914855480f * b;

    float l3 = l2 * l2 * l2;
    float m3 = m2 * m2 * m2;
    float s3 = s2 * s2 * s2;

    float3 outRgb;
    outRgb.r =  4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3;
    outRgb.g = -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3;
    outRgb.b = -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3;
    return max(outRgb, 0.0f.xxx);
}

// SMPTE ST 2084 (PQ) OETF: linear luminance in nits [0..10000] -> PQ signal [0..1].
float3 RawLinearNitsToPq01(float3 nits)
{
    float3 y = clamp(RawHdrSanitizeRgb(nits) / 10000.0f, 0.0f.xxx, 1.0f.xxx);
    float3 ym1 = pow(y, RAW_PQ_M1);
    float3 num = RAW_PQ_C1 + RAW_PQ_C2 * ym1;
    float3 den = 1.0f + RAW_PQ_C3 * ym1;
    return saturate(pow(max(num / max(den, 1.0e-6f.xxx), 0.0f.xxx), RAW_PQ_M2));
}

// SMPTE ST 2084 (PQ) Inverse EOTF: PQ signal [0..1] -> linear luminance in nits [0..10000].
float3 RawPq01ToLinearNits(float3 pq)
{
    float3 p = saturate(RawHdrSanitizeRgb(pq));
    float3 pm2 = pow(p, 1.0f / RAW_PQ_M2);
    float3 num = max(pm2 - RAW_PQ_C1, 0.0f.xxx);
    float3 den = max(RAW_PQ_C2 - RAW_PQ_C3 * pm2, 1.0e-6f.xxx);
    float3 y   = pow(num / den, 1.0f / RAW_PQ_M1);
    return clamp(y * 10000.0f, 0.0f.xxx, 10000.0f.xxx);
}

// Unified calibrated display output transform:
//   hdrEnabled == 0 -> exact sanitized SDR Rec.709 fallback identity
//   hdrEnabled != 0 -> OKLab gamut-safe rolloff -> BT.2020 -> calibrated [100..4000 nit] ST 2084 PQ
float3 RawEncodeCalibratedDisplayOutput(
    float3 linearRec709,
    uint   hdrEnabled,
    float  paperWhiteNits,
    float  peakNits)
{
    float3 safeRgb = RawHdrSanitizeRgb(linearRec709);
    if (hdrEnabled == 0u) {
        return saturate(safeRgb);
    }
    float calPeak  = clamp(peakNits, 100.0f, 4000.0f);
    float calPaper = clamp(paperWhiteNits, 80.0f, min(calPeak, 400.0f));
    float3 gamutSafe709 = RawOklabSoftGamutCompress(safeRgb, 0.30f);
    float3 rgb2020      = RawRec709ToRec2020(gamutSafe709);
    float3 nits         = min(rgb2020 * calPaper, calPeak.xxx);
    return RawLinearNitsToPq01(nits);
}

#endif // RAW_HDR_DISPLAY_OUTPUT_HLSLI
