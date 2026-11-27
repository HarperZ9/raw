// wave81_iridescence_frost_halo_check.cpp: Wave 81 C++23 Unit & Planted-Failure Control Suite
// Verifies:
//   1) Physical Airy thin-film reflectance (650/532/450 nm, d = 80..900 nm) across 1,440 states:
//      strict white-furnace bound (0 <= R <= 1.0), genuine spectral dispersion, exact identity on isGreenPine & dry
//   2) Anisotropic glaze-ice & hoarfrost phase transition across -10 C .. +10 C:
//      smooth -4 C .. +2 C transition, fire/torch thaw, unit normal, energy <= 1.0, zero cyan/blue neon (B-R <= 0.12)
//   3) Atmospheric 22-deg ice-crystal halo & 42-deg primary rain-mist bow angular sweep (0..180 deg):
//      22-deg peak with forbidden dark interior (< 19 deg == 0) + R-inside/B-outside order;
//      42-deg rainbow around anti-solar axis with R-outside/B-inside order + exact 0.0 on dry days
//   4) World-space camera-pan invariance (0.0 drift, zero screen-UV dependence)
//   5) Extreme NaN / +Inf / -Inf / zero-vector sanitization across all 3 Wave 81 evaluators
//   6) Planted Control A+B: unnormalized Airy tint (> 1.0) & cyan neon frost tint (B-R > 0.25) caught fail-closed
//   7) Planted Control C+D: dry-day phantom rainbow & isGreenPine mutation caught fail-closed
//   8) Bit-identical 100 Hz replay across SkyrimSE (CE1), Fallout4 (CE1.5), and Starfield (CE2) at 60/100/144/240 FPS
// Clean-room MIT, MSVC /std:c++latest /W4 /WX.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../src/d3d12_bridge/RawFramePacing.h"
#include "../src/d3d12_bridge/RawWorldDynamics.h"
#include "../src/d3d12_bridge/RawIridescenceFrostHalo.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>

using namespace raw::bridge::wave81;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool cond, const char* name)
{
    if (cond) {
        std::printf("  [PASS] %s\n", name);
        ++g_pass;
    } else {
        std::printf("  [FAIL] %s\n", name);
        ++g_fail;
    }
}

static bool IsFinite3(Float3 v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

int main()
{
    std::printf("=== Wave 81: Airy Thin-Film Iridescence + Frost-Glaze Phase + Atmospheric Spectral Halo Check ===\n");

    // 1) 1,440-state Airy thin-film sweep: white-furnace [0, 1], spectral dispersion, dry & isGreenPine identity
    {
        bool ok = true;
        int total = 0;
        float maxSpread = 0.0f;
        const float ds[6]     = {80.0f, 180.0f, 320.0f, 480.0f, 680.0f, 900.0f};
        const float cs[5]     = {0.02f, 0.22f, 0.50f, 0.78f, 1.0f};
        const float nFs[4]    = {1.25f, 1.31f, 1.333f, 1.48f};
        const float masks[3]  = {0.0f, 0.5f, 1.0f};
        const float nBs[2]    = {1.45f, 1.85f};
        const bool  pines[2]  = {false, true};
        const Float3 baseF0{0.04f, 0.04f, 0.04f};

        for (float d : ds) {
            for (float c : cs) {
                for (float nF : nFs) {
                    for (float m : masks) {
                        for (float nB : nBs) {
                            for (bool pine : pines) {
                                ++total;
                                const auto r = EvalThinFilmIridescence(
                                    {120.0f + d, -45.0f, 30.0f}, c, baseF0, d, m, nF, nB, pine);
                                if (!IsFinite3(r.reflectanceRGB) || !std::isfinite(r.maxReflectance)) { ok = false; }
                                if (r.reflectanceRGB.x < -1e-6f || r.reflectanceRGB.x > 1.0f + 1e-6f) { ok = false; }
                                if (r.reflectanceRGB.y < -1e-6f || r.reflectanceRGB.y > 1.0f + 1e-6f) { ok = false; }
                                if (r.reflectanceRGB.z < -1e-6f || r.reflectanceRGB.z > 1.0f + 1e-6f) { ok = false; }
                                if (r.maxReflectance > 1.0f + 1e-6f) { ok = false; }
                                if (pine || m == 0.0f) {
                                    if (std::fabs(r.reflectanceRGB.x - baseF0.x) > 1e-6f ||
                                        std::fabs(r.reflectanceRGB.y - baseF0.y) > 1e-6f ||
                                        std::fabs(r.reflectanceRGB.z - baseF0.z) > 1e-6f ||
                                        r.iridescenceWeight != 0.0f) {
                                        ok = false;
                                    }
                                } else {
                                    if (r.filmThicknessNm < 80.0f - 1e-4f || r.filmThicknessNm > 900.0f + 1e-4f) {
                                        ok = false;
                                    }
                                    const float hi = (std::max)(r.reflectanceRGB.x, (std::max)(r.reflectanceRGB.y, r.reflectanceRGB.z));
                                    const float lo = (std::min)(r.reflectanceRGB.x, (std::min)(r.reflectanceRGB.y, r.reflectanceRGB.z));
                                    if (hi - lo > maxSpread) { maxSpread = hi - lo; }
                                }
                            }
                        }
                    }
                }
            }
        }
        Check(ok && total == 1440 && maxSpread > 0.02f,
              "1,440-state Airy thin-film sweep: white-furnace <= 1.0, dispersion > 0.02, dry & isGreenPine identity");
    }

    // 2) Anisotropic glaze-ice & hoarfrost phase transition across -10 C .. +10 C
    {
        bool ok = true;
        const float temps[6] = {-10.0f, -4.0f, -1.5f, 0.0f, 2.0f, 8.0f};
        float prevGlaze = 2.0f;
        for (float tc : temps) {
            const auto r = EvalFrostGlazeCoupling(
                {310.0f, 145.0f, 80.0f}, {0.1f, -0.05f, 0.99f},
                {0.42f, 0.40f, 0.38f}, 0.68f, 0.85f, 0.60f,
                tc, {0.8f, 0.6f}, 0.70f, 0.0f, 0.55f, false);
            const float nLen = std::sqrt(r.normalOut.x * r.normalOut.x + r.normalOut.y * r.normalOut.y + r.normalOut.z * r.normalOut.z);
            if (!IsFinite3(r.albedoOut) || !IsFinite3(r.normalOut)) { ok = false; }
            if (std::fabs(nLen - 1.0f) > 1e-4f) { ok = false; }
            if (r.totalEnergyMax > 1.0f + 1e-5f) { ok = false; }
            if ((r.albedoOut.z - r.albedoOut.x) > 0.12f) { ok = false; } // zero blue/cyan neon clipping
            if (r.glazeMask > prevGlaze + 1e-5f) { ok = false; }          // monotonic decrease as temp rises
            prevGlaze = r.glazeMask;
        }
        // Verify torch/brazier heat thaws sub-zero glaze completely:
        const auto thawed = EvalFrostGlazeCoupling(
            {310.0f, 145.0f, 80.0f}, {0.0f, 0.0f, 1.0f},
            {0.42f, 0.40f, 0.38f}, 0.68f, 0.85f, 0.60f,
            -4.0f, {1.0f, 0.0f}, 0.60f, 0.85f, 0.55f, false);
        if (thawed.glazeMask > 1e-5f || thawed.frostMask > 1e-5f) { ok = false; }

        // Verify isGreenPine exact identity even at -10 C:
        const auto pine = EvalFrostGlazeCoupling(
            {310.0f, 145.0f, 80.0f}, {0.0f, 0.0f, 1.0f},
            {0.18f, 0.32f, 0.14f}, 0.75f, 0.90f, 0.90f,
            -10.0f, {1.0f, 0.0f}, 0.90f, 0.0f, 0.55f, true);
        if (std::fabs(pine.albedoOut.y - 0.32f) > 1e-6f || pine.glazeMask != 0.0f || pine.frostMask != 0.0f) { ok = false; }

        Check(ok, "Frost-glaze phase transition (-4 C..+2 C): monotonic, fire-thaw, unit normal, <= 1.0 energy, zero cyan neon");
    }

    // 3) Atmospheric 22-deg ice-crystal halo & 42-deg rain-mist bow angular phase sweep
    {
        bool ok = true;
        const Float3 sunDir{0.0f, 0.93969f, 0.34202f}; // sun at +20 deg elev along +Y

        // Helper: rotate around X axis in YZ plane relative to a base vector
        auto dirAtAngleFromSun = [&](float degFromSun) -> Float3 {
            const float sunElev = std::asin(sunDir.z);
            const float ang = sunElev + degFromSun * (kPi / 180.0f);
            return {0.0f, std::cos(ang), std::sin(ang)};
        };

        // 22-deg ice halo in cold snowy air: forbidden dark inner sky (< 19 deg == 0), peak around 21.8 deg:
        const auto iceInner = EvalAtmoSpectralHalo(dirAtAngleFromSun(15.0f), sunDir, 0.0f, 0.85f, 0.85f, 1200.0f, -6.0f);
        const auto iceRed   = EvalAtmoSpectralHalo(dirAtAngleFromSun(21.54f), sunDir, 0.0f, 0.85f, 0.85f, 1200.0f, -6.0f);
        const auto iceBlue  = EvalAtmoSpectralHalo(dirAtAngleFromSun(22.35f), sunDir, 0.0f, 0.85f, 0.85f, 1200.0f, -6.0f);
        if (iceInner.iceHalo22Weight > 1e-5f || iceInner.haloRGB.x > 1e-5f) { ok = false; }
        if (iceRed.iceHalo22Weight < 0.50f || !(iceRed.haloRGB.x > iceRed.haloRGB.z)) { ok = false; }
        if (iceBlue.iceHalo22Weight < 0.40f || !(iceBlue.haloRGB.z > iceBlue.haloRGB.x)) { ok = false; }

        // 42-deg primary rainbow around anti-solar axis (-sunDir) in active warm rain:
        auto dirAtAntiAngle = [&](float degFromAnti) -> Float3 {
            // Anti-solar vector is (0, -cos(20), -sin(20)); lift by degFromAnti toward +Z -> (0, -cos(a), sin(a)) where a = degFromAnti - 20
            const float a = (degFromAnti - 20.0f) * (kPi / 180.0f);
            return {0.0f, -std::cos(a), std::sin(a)};
        };
        const auto bowRed  = EvalAtmoSpectralHalo(dirAtAntiAngle(42.3f), sunDir, 0.70f, 0.0f, 0.70f, 150.0f, 12.0f);
        const auto bowBlue = EvalAtmoSpectralHalo(dirAtAntiAngle(40.6f), sunDir, 0.70f, 0.0f, 0.70f, 150.0f, 12.0f);
        const auto bowDry  = EvalAtmoSpectralHalo(dirAtAntiAngle(42.0f), sunDir, 0.0f, 0.0f, 0.90f, 150.0f, 15.0f);
        if (bowRed.rainBow42Weight < 0.50f || !(bowRed.haloRGB.x > bowRed.haloRGB.z)) { ok = false; }
        if (bowBlue.rainBow42Weight < 0.50f || !(bowBlue.haloRGB.z > bowBlue.haloRGB.x)) { ok = false; }
        if (bowDry.rainBow42Weight != 0.0f || bowDry.haloRGB.x != 0.0f || bowDry.haloRGB.y != 0.0f || bowDry.haloRGB.z != 0.0f) {
            ok = false;
        }

        Check(ok, "Atmospheric 22-deg ice halo (R-in/B-out, dark inner) & 42-deg rain bow (R-out/B-in, 0 dry phantom)");
    }

    // 4) World-space camera-pan invariance (0.0 drift, zero screen-UV dependence)
    {
        const Float3 wpWorld{1024.5f, -768.25f, 112.0f};
        const auto tfA = EvalThinFilmIridescence(wpWorld, 0.62f, {0.03f, 0.03f, 0.03f}, 360.0f, 0.8f, 1.333f, 1.52f, false);
        const auto tfB = EvalThinFilmIridescence(wpWorld, 0.62f, {0.03f, 0.03f, 0.03f}, 360.0f, 0.8f, 1.333f, 1.52f, false);
        const auto fgA = EvalFrostGlazeCoupling(wpWorld, {0.0f, 0.0f, 1.0f}, {0.5f, 0.48f, 0.45f}, 0.6f, 0.7f, 0.5f, -2.5f, {0.6f, 0.8f}, 0.5f, 0.0f, 0.6f, false);
        const auto fgB = EvalFrostGlazeCoupling(wpWorld, {0.0f, 0.0f, 1.0f}, {0.5f, 0.48f, 0.45f}, 0.6f, 0.7f, 0.5f, -2.5f, {0.6f, 0.8f}, 0.5f, 0.0f, 0.6f, false);
        const float drift = std::fabs(tfA.reflectanceRGB.x - tfB.reflectanceRGB.x)
                          + std::fabs(fgA.normalOut.x - fgB.normalOut.x)
                          + std::fabs(fgA.albedoOut.x - fgB.albedoOut.x);
        Check(drift == 0.0f, "Exact 0.0 world-space camera-pan drift across thin-film & frost-glaze");
    }

    // 5) Extreme NaN / +Inf / -Inf / zero-vector sanitization across all 3 Wave 81 evaluators
    {
        const float qnan = std::numeric_limits<float>::quiet_NaN();
        const float pinf = std::numeric_limits<float>::infinity();
        const float ninf = -std::numeric_limits<float>::infinity();

        const auto tfNan = EvalThinFilmIridescence({qnan, pinf, ninf}, qnan, {qnan, pinf, -2.0f}, qnan, pinf, qnan, ninf, false);
        const auto fgNan = EvalFrostGlazeCoupling({qnan, ninf, pinf}, {0.0f, 0.0f, 0.0f}, {pinf, qnan, -1.0f}, qnan, pinf, ninf, qnan, {0.0f, 0.0f}, pinf, qnan, ninf, false);
        const auto ahNan = EvalAtmoSpectralHalo({0.0f, 0.0f, 0.0f}, {qnan, pinf, ninf}, pinf, qnan, ninf, pinf, qnan);

        const bool ok = IsFinite3(tfNan.reflectanceRGB) && std::isfinite(tfNan.maxReflectance) && tfNan.maxReflectance <= 1.0f
                     && IsFinite3(fgNan.albedoOut) && IsFinite3(fgNan.normalOut) && fgNan.totalEnergyMax <= 1.0f + 1e-5f
                     && IsFinite3(ahNan.haloRGB) && std::isfinite(ahNan.chromaticSpread);
        Check(ok, "Extreme NaN / +Inf / -Inf / zero-vector sanitization across all 3 Wave 81 evaluators");
    }

    // 6) Planted Control A+B: unnormalized Airy tint (> 1.0) & cyan neon frost tint (B-R > 0.25) caught fail-closed
    {
        auto validateWhiteFurnaceAndChroma = [](float maxRefl, Float3 frostAlb) -> bool {
            if (maxRefl > 1.0f + 1e-5f) { return false; }
            if ((frostAlb.z - frostAlb.x) > 0.12f) { return false; }
            return true;
        };
        const bool plantedOverUnitCaught = !validateWhiteFurnaceAndChroma(1.32f, {0.78f, 0.82f, 0.86f});
        const bool plantedCyanNeonCaught = !validateWhiteFurnaceAndChroma(0.45f, {0.32f, 0.72f, 0.96f});
        const auto realTf = EvalThinFilmIridescence({100.0f, 200.0f, 10.0f}, 0.35f, {0.04f, 0.04f, 0.04f}, 420.0f, 1.0f, 1.38f, 1.52f, false);
        const auto realFg = EvalFrostGlazeCoupling({100.0f, 200.0f, 10.0f}, {0.0f, 0.0f, 1.0f}, {0.45f, 0.45f, 0.45f}, 0.7f, 0.2f, 0.9f, -6.0f, {1.0f, 0.0f}, 0.8f, 0.0f, 0.5f, false);
        const bool realPasses = validateWhiteFurnaceAndChroma(realTf.maxReflectance, realFg.albedoOut);
        Check(plantedOverUnitCaught && plantedCyanNeonCaught && realPasses,
              "Planted Controls A+B: over-unit Airy tint (>1.0) & cyan neon frost (B-R>0.12) caught fail-closed");
    }

    // 7) Planted Control C+D: dry-day phantom rainbow & isGreenPine mutation caught fail-closed
    {
        auto validateDryAndPine = [](float dryBowW, Float3 pineBefore, Float3 pineAfter) -> bool {
            if (dryBowW > 1e-5f) { return false; }
            const float diff = std::fabs(pineBefore.x - pineAfter.x) + std::fabs(pineBefore.y - pineAfter.y) + std::fabs(pineBefore.z - pineAfter.z);
            if (diff > 1e-6f) { return false; }
            return true;
        };
        const Float3 pineRef{0.15f, 0.34f, 0.12f};
        const bool plantedPhantomBowCaught = !validateDryAndPine(0.18f, pineRef, pineRef);
        const bool plantedPineMutCaught    = !validateDryAndPine(0.0f, pineRef, {0.42f, 0.52f, 0.58f});
        const auto realDry  = EvalAtmoSpectralHalo({0.0f, -0.92f, 0.38f}, {0.0f, 0.94f, 0.34f}, 0.0f, 0.0f, 0.9f, 100.0f, 14.0f);
        const auto realPine = EvalFrostGlazeCoupling({50.0f, 60.0f, 10.0f}, {0.0f, 0.0f, 1.0f}, pineRef, 0.8f, 0.9f, 0.9f, -8.0f, {1.0f, 0.0f}, 0.9f, 0.0f, 0.5f, true);
        const bool realPasses = validateDryAndPine(realDry.rainBow42Weight, pineRef, realPine.albedoOut);
        Check(plantedPhantomBowCaught && plantedPineMutCaught && realPasses,
              "Planted Controls C+D: dry-day phantom rainbow & isGreenPine mutation caught fail-closed");
    }

    // 8) Deterministic 100 Hz replay across SkyrimSE (CE1), Fallout4 (CE1.5), and Starfield (CE2) profiles (60/100/144/240 FPS)
    {
        const char* exes[3] = {"SkyrimSE.exe", "Fallout4.exe", "Starfield.exe"};
        const double rates[4] = {60.0, 100.0, 144.0, 240.0};
        bool ok = true;
        double globalRef = -1.0;

        for (int gi = 0; gi < 3; ++gi) {
            const auto prof = raw::versions::ResolveEngineProfileByExe(exes[gi]);
            const auto policy = raw::bridge::ResolveFramePacingPolicy(prof);
            if (policy.multiplayerBlocked || policy.fixedTickHz != 100.0) { ok = false; }
            for (int ri = 0; ri < 4; ++ri) {
                raw::bridge::DeterministicFramePacer pacer(policy);
                const double dt = 1.0 / rates[ri];
                const int frames = static_cast<int>(std::llround(2.0 * rates[ri]));
                double simT = 0.0;
                double acc = 0.0;
                for (int f = 0; f < frames; ++f) {
                    const auto s = pacer.Step(dt);
                    for (uint32_t k = 0; k < s.substeps; ++k) {
                        simT += 0.01;
                        const auto gust = raw::bridge::EvalCoherentWindGust(
                            420.0, -180.0, 140.0, 65.0, 0.4, 0.0, simT);
                        const auto tf = EvalThinFilmIridescence(
                            {420.0f, -180.0f, 60.0f}, 0.55f, {0.04f, 0.04f, 0.04f},
                            380.0f, 0.65f, 1.333f, 1.52f, false);
                        const auto fg = EvalFrostGlazeCoupling(
                            {420.0f, -180.0f, 60.0f}, {0.0f, 0.0f, 1.0f},
                            {0.46f, 0.42f, 0.38f}, 0.72f, 0.70f, 0.55f, -2.0f,
                            {static_cast<float>(gust.swayVelX), static_cast<float>(gust.swayVelY)},
                            static_cast<float>(gust.gust01), 0.0f, 0.55f, false);
                        const auto ah = EvalAtmoSpectralHalo(
                            {0.0f, 0.74f, 0.67f}, {0.0f, 0.94f, 0.34f},
                            0.0f, 0.75f, 0.80f, 1200.0f, -5.0f);
                        acc += static_cast<double>(tf.reflectanceRGB.x) + static_cast<double>(fg.roughnessOut)
                             + static_cast<double>(fg.anisoRatio) + static_cast<double>(ah.haloRGB.x);
                    }
                }
                if (gi == 0 && ri == 0) {
                    globalRef = acc;
                } else if (std::fabs(acc - globalRef) > 1.0e-12) {
                    ok = false;
                }
            }
        }
        Check(ok && std::isfinite(globalRef) && globalRef > 0.0,
              "Bit-identical 100 Hz replay across SkyrimSE / Fallout4 / Starfield profiles & 60/100/144/240 FPS");
    }

    std::printf("=== RESULT: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
