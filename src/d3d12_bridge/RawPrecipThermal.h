#pragma once
// RawPrecipThermal.h: Wave 76 C++23 Analytical Mirror & Deterministic 100 Hz Verifier for
// World-Space Thermal/Wind Precipitation Accumulation (Snow Drift + Torch/Brazier Melt)
// and Energy-Conserving Cloud-Crepuscular Shafts + Sky-to-Water Multiple-Scattering Coupling.
// Original work, MIT.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "RawFramePacing.h"
#include "RawWorldDynamics.h"

namespace raw::bridge {

struct ThermalPrecipSample {
    double snowAccum01    = 0.0;
    double meltCore01     = 0.0;
    double wetSlush01     = 0.0;
    double meltRoughness  = 0.84;
    double albedoScale    = 1.0;
    double concavity      = 0.0;
    bool   allFinite      = true;
    bool   energyBounded  = true;
};

inline double PtSan(double v, double fb) {
    return std::isfinite(v) ? v : fb;
}

inline double PtClamp01(double v) {
    return (std::clamp)(v, 0.0, 1.0);
}

inline double PtSmoothstep(double edge0, double edge1, double x) {
    const double t = PtClamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0 - 2.0 * t);
}

inline double EvalPtWorldHeight(double wpX, double wpY) {
    const double qx = PtSan(wpX, 0.0) * 0.014;
    const double qy = PtSan(wpY, 0.0) * 0.014;
    return 0.55 * std::sin(qx) * std::cos(qy)
         + 0.30 * std::cos(qx * 2.13 + 1.7) * std::sin(qy * 1.91 - 0.8)
         + 0.15 * std::sin((qx + qy) * 3.17 + 0.5);
}

inline double EvalPtLaplacianConcavity(double wpX, double wpY) {
    const double h = 18.0;
    const double hc = EvalPtWorldHeight(wpX, wpY);
    const double lap = (EvalPtWorldHeight(wpX + h, wpY)
                      + EvalPtWorldHeight(wpX - h, wpY)
                      + EvalPtWorldHeight(wpX, wpY + h)
                      + EvalPtWorldHeight(wpX, wpY - h)
                      - 4.0 * hc) / (h * h * 0.0004);
    return PtClamp01((std::max)(PtSan(lap, 0.0), 0.0) * 0.65);
}

inline double EvalTorchThermalFlux(double distU, double radiusU = 220.0, double heatStrength = 1.0) {
    const double d = PtSan(distU, radiusU);
    const double r = PtSan(radiusU, 220.0);
    const double h = PtClamp01(PtSan(heatStrength, 1.0));
    if (r <= 1.0 || d < 0.0 || d >= r) {
        return 0.0;
    }
    const double u = PtClamp01(1.0 - d / r);
    return (std::clamp)(u * u * 1.65 * h, 0.0, 1.5);
}

// Evaluates world-space snow accumulation & torch/brazier thermal melt strictly in world coordinates.
// Optional planted-control switches:
//   - disableThermalForPlantedControl: zeroes thermal melt (Control A)
//   - plantedScreenRadialBias > 0: injects screen-border vignette corruption (Control B)
inline ThermalPrecipSample EvalWorldThermalSnowAccum(
    double wpX,
    double wpY,
    double nX,
    double nY,
    double nZ,
    double windX,
    double windY,
    double gust01,
    double baseSnow01,
    double tempC,
    double fireHeat01,
    double skyVis,
    double screenUvX = 0.5,
    bool   disableThermalForPlantedControl = false,
    double plantedScreenRadialBias = 0.0) {
    ThermalPrecipSample o{};
    const double nx = PtSan(nX, 0.0);
    const double ny = PtSan(nY, 0.0);
    const double nz = PtSan(nZ, 1.0);
    const double nLen = std::sqrt(nx * nx + ny * ny + nz * nz);
    const double unx = (nLen > 1.0e-6) ? (nx / nLen) : 0.0;
    const double uny = (nLen > 1.0e-6) ? (ny / nLen) : 0.0;
    const double unz = (nLen > 1.0e-6) ? (nz / nLen) : 1.0;

    const double wx = PtSan(windX, 0.0);
    const double wy = PtSan(windY, 0.0);
    const double wLen = std::hypot(wx, wy);
    const double wdX = (wLen > 1.0e-3) ? (wx / wLen) : 0.8660254037844386;
    const double wdY = (wLen > 1.0e-3) ? (wy / wLen) : 0.5;

    const double g01 = PtClamp01(PtSan(gust01, 0.0));
    const double sIn = PtClamp01(PtSan(baseSnow01, 0.0));
    const double tC = (std::clamp)(PtSan(tempC, -2.0), -45.0, 45.0);
    const double fH = disableThermalForPlantedControl ? 0.0 : (std::clamp)(PtSan(fireHeat01, 0.0), 0.0, 1.5);
    const double sv = PtClamp01(PtSan(skyVis, 1.0));

    o.concavity = EvalPtLaplacianConcavity(wpX, wpY);
    const double leeward = PtClamp01((unx * wdX + uny * wdY) * 0.5 + 0.5);
    const double upSlope = PtSmoothstep(0.32, 0.80, unz);
    const double windScour = PtClamp01((1.0 - leeward) * g01 * 0.35);
    const double coldFactor = PtSmoothstep(3.0, -4.0, tC);
    const double driftBoost = 0.76 + 0.28 * o.concavity + 0.16 * leeward * g01 - windScour;
    double snowPreMelt = PtClamp01(sIn * upSlope * driftBoost * (0.45 + 0.55 * coldFactor) * (0.35 + 0.65 * sv));

    if (plantedScreenRadialBias > 0.0) {
        const double edgeDist = (std::min)(screenUvX, 1.0 - screenUvX);
        const double edgeVig = PtClamp01(edgeDist / 0.10);
        snowPreMelt *= (1.0 - plantedScreenRadialBias) + plantedScreenRadialBias * edgeVig;
    }

    const double meltCore = PtSmoothstep(0.14, 0.68, fH);
    const double slushCollar = PtClamp01(PtSmoothstep(0.04, 0.24, fH) * (1.0 - PtSmoothstep(0.46, 0.86, fH)));
    const double wetMelt = PtClamp01((std::max)(meltCore * 0.85, slushCollar * 0.78) * (std::max)(snowPreMelt, sIn * 0.55));

    o.snowAccum01 = PtClamp01(snowPreMelt * (1.0 - meltCore));
    o.meltCore01 = meltCore;
    o.wetSlush01 = wetMelt;
    o.meltRoughness = (std::clamp)(0.84 + (0.14 - 0.84) * wetMelt, 0.12, 0.92);
    o.albedoScale = (std::clamp)(1.0 + (0.64 - 1.0) * wetMelt, 0.58, 1.0);
    o.allFinite = std::isfinite(o.snowAccum01) && std::isfinite(o.meltCore01)
               && std::isfinite(o.wetSlush01) && std::isfinite(o.meltRoughness)
               && std::isfinite(o.albedoScale) && std::isfinite(o.concavity);
    o.energyBounded = o.allFinite && (o.snowAccum01 >= 0.0 && o.snowAccum01 <= 1.0)
                   && (o.wetSlush01 >= 0.0 && o.wetSlush01 <= 1.0)
                   && (o.albedoScale >= 0.58 && o.albedoScale <= 1.0);
    return o;
}

inline double EvalCrepuscularPhase(double cosTheta, double opticalDepth, double cloudTrans, double singleScatAlbedo = 0.96) {
    const double mu = (std::clamp)(PtSan(cosTheta, 0.0), -1.0, 1.0);
    const double tau = (std::clamp)(PtSan(opticalDepth, 0.0), 0.0, 12.0);
    const double ct = PtClamp01(PtSan(cloudTrans, 1.0));
    constexpr double kPi = 3.14159265358979323846;
    const double g1 = 0.72, g2 = 0.22;
    const double d1 = (std::max)(1.0 + g1 * g1 - 2.0 * g1 * mu, 0.04);
    const double d2 = (std::max)(1.0 + g2 * g2 - 2.0 * g2 * mu, 0.04);
    const double ph1 = (1.0 - g1 * g1) / (4.0 * kPi * std::pow(d1, 1.5));
    const double ph2 = (1.0 - g2 * g2) / (4.0 * kPi * std::pow(d2, 1.5));
    const double multiW = PtClamp01(1.0 - std::exp(-tau * 0.65));
    const double phase = (ph1 * (0.35 + 0.65 * ct)) * (1.0 - multiW * 0.55) + ph2 * (multiW * 0.55);
    return (std::clamp)(phase * singleScatAlbedo, 0.0, 2.8 * (std::max)(singleScatAlbedo, 1.0));
}

inline double EvalSkyWaterMultiScatter1D(double skyRefl, double waterBody, double fresnelF, double cloudTrans, double ndotv, double gust01) {
    const double skyS = (std::clamp)(PtSan(skyRefl, 0.0), 0.0, 12.0);
    const double bodS = (std::clamp)(PtSan(waterBody, 0.0), 0.0, 12.0);
    const double f0 = PtClamp01(PtSan(fresnelF, 0.02));
    const double ct = PtClamp01(PtSan(cloudTrans, 1.0));
    const double nv = PtClamp01(PtSan(ndotv, 0.5));
    const double g01 = PtClamp01(PtSan(gust01, 0.0));
    const double graze = (1.0 - nv) * (1.0 - nv);
    const double multiBounce = PtClamp01(graze * (0.06 + 0.10 * g01) * (1.0 - 0.28 * ct));
    const double fEff = (std::clamp)(f0 + (1.0 - f0) * multiBounce, 0.0, 0.98);
    return bodS + (skyS - bodS) * fEff;
}

inline bool VerifyDeterministic100HzThermalReplay(
    const FramePacingPolicy& policy,
    double wpX,
    double wpY,
    double windX,
    double windY,
    double torchDistU,
    double durationSec,
    double fpsA,
    double fpsB,
    double& outMaxDiff) {
    DeterministicFramePacer pA(policy);
    DeterministicFramePacer pB(policy);
    double simTimeA = 0.0, simTimeB = 0.0;
    const int nA = static_cast<int>(std::llround(durationSec * fpsA));
    const int nB = static_cast<int>(std::llround(durationSec * fpsB));
    for (int i = 0; i < nA; ++i) {
        simTimeA += static_cast<double>(pA.Step(1.0 / fpsA, 2.0).substeps) * policy.fixedTickSec;
    }
    for (int i = 0; i < nB; ++i) {
        simTimeB += static_cast<double>(pB.Step(1.0 / fpsB, 2.0).substeps) * policy.fixedTickSec;
    }
    const WorldWindGustSample gA = EvalCoherentWindGust(wpX, wpY, windX, windY, 0.0, 0.75, simTimeA);
    const WorldWindGustSample gB = EvalCoherentWindGust(wpX, wpY, windX, windY, 0.0, 0.75, simTimeB);
    const double heat = EvalTorchThermalFlux(torchDistU, 220.0, 1.0);
    const ThermalPrecipSample sA = EvalWorldThermalSnowAccum(wpX, wpY, 0.1, 0.0, 0.99, windX, windY, gA.gust01, 0.85, -4.0, heat, 1.0);
    const ThermalPrecipSample sB = EvalWorldThermalSnowAccum(wpX, wpY, 0.1, 0.0, 0.99, windX, windY, gB.gust01, 0.85, -4.0, heat, 1.0);
    outMaxDiff = (std::max)(std::abs(sA.snowAccum01 - sB.snowAccum01), std::abs(sA.wetSlush01 - sB.wetSlush01));
    return sA.allFinite && sB.allFinite && (outMaxDiff <= 1.0e-9);
}

}  // namespace raw::bridge
