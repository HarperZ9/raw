#pragma once
// RawHdrDisplayOutput.h: Wave 86 Exact C++23 Mirror of RawHdrDisplayOutput.hlsli.
// Implements ITU-R BT.709 -> BT.2020 linear matrix, OKLab perceptual soft-gamut rolloff,
// SMPTE ST 2084 PQ OETF/EOTF (100..4000 nits matching Calibrate Pro & build-color),
// and SDR fallback identity. Clean-room MIT, MSVC /W4 /WX clean.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace raw::bridge::wave86
{

inline constexpr double kPqM1 = 2610.0 / 16384.0;
inline constexpr double kPqM2 = (2523.0 / 4096.0) * 128.0;
inline constexpr double kPqC1 = 3424.0 / 4096.0;
inline constexpr double kPqC2 = (2413.0 / 4096.0) * 32.0;
inline constexpr double kPqC3 = (2392.0 / 4096.0) * 32.0;

struct Rgb3
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

inline Rgb3 SanitizeRgb(const Rgb3& c) noexcept
{
    return {
        std::isfinite(c.r) ? (std::max)(c.r, 0.0f) : 0.0f,
        std::isfinite(c.g) ? (std::max)(c.g, 0.0f) : 0.0f,
        std::isfinite(c.b) ? (std::max)(c.b, 0.0f) : 0.0f
    };
}

inline Rgb3 Rec709ToRec2020(const Rgb3& rgb709) noexcept
{
    const Rgb3 c = SanitizeRgb(rgb709);
    const float r = 0.6274039f * c.r + 0.3292830f * c.g + 0.0433131f * c.b;
    const float g = 0.0690973f * c.r + 0.9195404f * c.g + 0.0113623f * c.b;
    const float b = 0.0163914f * c.r + 0.0880133f * c.g + 0.8955953f * c.b;
    return {(std::max)(r, 0.0f), (std::max)(g, 0.0f), (std::max)(b, 0.0f)};
}

inline Rgb3 OklabSoftGamutCompress(const Rgb3& rgb709, float softChromaKnee = 0.30f) noexcept
{
    const Rgb3 c = SanitizeRgb(rgb709);
    const float l = 0.4122214708f * c.r + 0.5363325363f * c.g + 0.0514459929f * c.b;
    const float m = 0.2119034982f * c.r + 0.6806995451f * c.g + 0.1073969566f * c.b;
    const float s = 0.0883024619f * c.r + 0.2817188376f * c.g + 0.6299787005f * c.b;

    const float l_ = std::cbrt((std::max)(l, 0.0f));
    const float m_ = std::cbrt((std::max)(m, 0.0f));
    const float s_ = std::cbrt((std::max)(s, 0.0f));

    const float L = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
    float       a = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
    float       b = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;

    const float chroma = std::sqrt(a * a + b * b);
    const float knee   = (std::max)(softChromaKnee, 0.05f);
    if (chroma > knee) {
        const float excess     = chroma - knee;
        const float compressed = knee + excess / (1.0f + excess * 2.5f);
        const float scale      = compressed / (std::max)(chroma, 1.0e-6f);
        a *= scale;
        b *= scale;
    }

    const float l2 = L + 0.3963377774f * a + 0.2158037573f * b;
    const float m2 = L - 0.1055613458f * a - 0.0638541728f * b;
    const float s2 = L - 0.0894841775f * a - 1.2914855480f * b;

    const float l3 = l2 * l2 * l2;
    const float m3 = m2 * m2 * m2;
    const float s3 = s2 * s2 * s2;

    const float outR =  4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3;
    const float outG = -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3;
    const float outB = -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3;
    return {(std::max)(outR, 0.0f), (std::max)(outG, 0.0f), (std::max)(outB, 0.0f)};
}

inline float LinearNitsToPq01Scalar(float nits) noexcept
{
    if (!std::isfinite(nits) || nits <= 0.0f) return 0.0f;
    const double y   = (std::clamp)(static_cast<double>(nits) / 10000.0, 0.0, 1.0);
    const double ym1 = std::pow(y, kPqM1);
    const double num = kPqC1 + kPqC2 * ym1;
    const double den = 1.0 + kPqC3 * ym1;
    const double pq  = std::pow((std::max)(num / (std::max)(den, 1.0e-12), 0.0), kPqM2);
    return static_cast<float>((std::clamp)(pq, 0.0, 1.0));
}

inline float Pq01ToLinearNitsScalar(float pq) noexcept
{
    if (!std::isfinite(pq) || pq <= 0.0f) return 0.0f;
    const double p   = (std::clamp)(static_cast<double>(pq), 0.0, 1.0);
    const double pm2 = std::pow(p, 1.0 / kPqM2);
    const double num = (std::max)(pm2 - kPqC1, 0.0);
    const double den = (std::max)(kPqC2 - kPqC3 * pm2, 1.0e-12);
    const double y   = std::pow(num / den, 1.0 / kPqM1);
    return static_cast<float>((std::clamp)(y * 10000.0, 0.0, 10000.0));
}

inline Rgb3 LinearNitsToPq01(const Rgb3& nits) noexcept
{
    return {
        LinearNitsToPq01Scalar(nits.r),
        LinearNitsToPq01Scalar(nits.g),
        LinearNitsToPq01Scalar(nits.b)
    };
}

inline Rgb3 Pq01ToLinearNits(const Rgb3& pq) noexcept
{
    return {
        Pq01ToLinearNitsScalar(pq.r),
        Pq01ToLinearNitsScalar(pq.g),
        Pq01ToLinearNitsScalar(pq.b)
    };
}

inline Rgb3 EncodeCalibratedDisplayOutput(
    const Rgb3& linearRec709,
    bool        hdrEnabled,
    float       paperWhiteNits = 203.0f,
    float       peakNits       = 1000.0f) noexcept
{
    const Rgb3 safe = SanitizeRgb(linearRec709);
    if (!hdrEnabled) {
        return {
            (std::clamp)(safe.r, 0.0f, 1.0f),
            (std::clamp)(safe.g, 0.0f, 1.0f),
            (std::clamp)(safe.b, 0.0f, 1.0f)
        };
    }
    const float calPeak  = (std::clamp)(peakNits, 100.0f, 4000.0f);
    const float calPaper = (std::clamp)(paperWhiteNits, 80.0f, (std::min)(calPeak, 400.0f));
    const Rgb3  gamut709 = OklabSoftGamutCompress(safe, 0.30f);
    const Rgb3  rgb2020  = Rec709ToRec2020(gamut709);
    const Rgb3  nits{
        (std::min)(rgb2020.r * calPaper, calPeak),
        (std::min)(rgb2020.g * calPaper, calPeak),
        (std::min)(rgb2020.b * calPaper, calPeak)
    };
    return LinearNitsToPq01(nits);
}

} // namespace raw::bridge::wave86
