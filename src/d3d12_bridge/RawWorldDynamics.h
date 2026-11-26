#pragma once
// RawWorldDynamics.h: Wave 74 C++23 Analytical Mirror & Deterministic 100 Hz Replay Verifier for
// World-Space Coherent Gust Fields, Foliage Phase-Locked MV Compensation, and Energy-Conserving
// Wet-Leaf / Water Volumetric Cloud-Shadow Coupling. Original work, MIT.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "RawFramePacing.h"

namespace raw::bridge {

struct WorldWindGustSample {
    double swayVelX   = 0.0;
    double swayVelY   = 0.0;
    double gust01     = 0.0;
    double swayDeriv  = 0.0;
    bool   allFinite  = true;
};

struct WetLeafEnergySample {
    double albedoEffR        = 0.0;
    double albedoEffG        = 0.0;
    double albedoEffB        = 0.0;
    double fresnelWet        = 0.0;
    double directTransWeight = 0.0;
    double wetRoughness      = 0.78;
    double totalEnergySum    = 0.0;
    bool   energyConserved   = true;
    bool   allFinite         = true;
};

struct WaterCloudCouplingSample {
    double sunTransmittance      = 1.0;
    double roughScale            = 1.0;
    double causticTransmittance  = 1.0;
    double crestSssTransmittance = 1.0;
    double skyAmbientFactor      = 1.0;  // Strictly 1.0: cloud shadows attenuate direct sun, NOT sky ambient!
    bool   allFinite             = true;
};

inline double SanitizeFinite(double v, double fallbackVal) {
    return std::isfinite(v) ? v : fallbackVal;
}

inline double Clamp01(double v) {
    return (std::clamp)(v, 0.0, 1.0);
}

inline WorldWindGustSample EvalCoherentWindGust(
    double posX,
    double posY,
    double windX,
    double windY,
    double rainFall,
    double snowFall,
    double simTimeSec) {
    WorldWindGustSample out{};
    const double px = SanitizeFinite(posX, 0.0);
    const double py = SanitizeFinite(posY, 0.0);
    const double wx = SanitizeFinite(windX, 0.0);
    const double wy = SanitizeFinite(windY, 0.0);
    const double tSafe = SanitizeFinite(simTimeSec, 0.0);
    const double rain01 = Clamp01(SanitizeFinite(rainFall, 0.0));
    const double snow01 = Clamp01(SanitizeFinite(snowFall, 0.0));

    const double wLen = std::hypot(wx, wy);
    const double dirX = (wLen > 1.0e-3) ? (wx / wLen) : 0.8660254037844386;
    const double dirY = (wLen > 1.0e-3) ? (wy / wLen) : 0.5;
    const double perpX = -dirY;
    const double perpY = dirX;

    const double windNorm = Clamp01(wLen / 280.0 + rain01 * 0.35 + snow01 * 0.30);
    const double omega1 = 1.15 + 1.85 * windNorm;
    const double omega2 = 0.72 + 1.10 * windNorm;

    const double phi1 = (px * dirX + py * dirY) * 0.0024 - tSafe * omega1;
    const double phi2 = (px * perpX + py * perpY) * 0.0041 - tSafe * omega2 + 1.37;

    const double wave1 = 0.55 + 0.45 * std::sin(phi1);
    const double eddy2 = 0.65 + 0.35 * std::cos(phi2);
    const double ampScale = 0.20 + 0.80 * windNorm;

    out.gust01 = Clamp01(wave1 * eddy2 * ampScale);
    out.swayDeriv = (std::clamp)(std::cos(phi1) * eddy2 * ampScale * (omega1 * 0.32), -1.5, 1.5);
    out.swayVelX = dirX * out.swayDeriv;
    out.swayVelY = dirY * out.swayDeriv;
    out.allFinite = std::isfinite(out.swayVelX) && std::isfinite(out.swayVelY)
                 && std::isfinite(out.gust01) && std::isfinite(out.swayDeriv);
    return out;
}

inline WetLeafEnergySample EvalWetLeafRadiativeBudget(
    double albR,
    double albG,
    double albB,
    double wetness,
    double ndotv,
    double cloudTrans,
    double gust01,
    double plantedEnergyLeak = 0.0) {
    WetLeafEnergySample out{};
    const double rSafe = (std::clamp)(SanitizeFinite(albR, 0.0), 0.0, 0.92);
    const double gSafe = (std::clamp)(SanitizeFinite(albG, 0.0), 0.0, 0.92);
    const double bSafe = (std::clamp)(SanitizeFinite(albB, 0.0), 0.0, 0.92);
    const double w = Clamp01(SanitizeFinite(wetness, 0.0));
    const double nv = Clamp01(SanitizeFinite(ndotv, 0.5));
    const double ct = Clamp01(SanitizeFinite(cloudTrans, 1.0));
    const double g = Clamp01(SanitizeFinite(gust01, 0.0));

    const double f0Wet = 0.016 + (0.024 - 0.016) * w;
    const double oneMinusNv = 1.0 - nv;
    const double fSpec = Clamp01(f0Wet + (1.0 - f0Wet) * (oneMinusNv * oneMinusNv * oneMinusNv * oneMinusNv * oneMinusNv));
    const double remEnergy = (std::max)(1.0 - fSpec, 0.0);

    const double wetDarken = 1.0 + (0.86 - 1.0) * (w * 0.65);
    out.albedoEffR = (std::min)(rSafe * wetDarken, remEnergy);
    out.albedoEffG = (std::min)(gSafe * wetDarken, remEnergy);
    out.albedoEffB = (std::min)(bSafe * wetDarken, remEnergy);
    out.fresnelWet = fSpec;

    const double maxAlb = (std::max)(out.albedoEffR, (std::max)(out.albedoEffG, out.albedoEffB));
    const double transCap = (std::max)(remEnergy - maxAlb, 0.0);
    const double gustMod = 1.0 + 0.16 * g;
    const double desiredTrans = (0.24 + (0.15 - 0.24) * w) * ct * gustMod;
    out.directTransWeight = (std::min)(desiredTrans, transCap) + plantedEnergyLeak;
    out.wetRoughness = (std::clamp)((0.78 + (0.24 - 0.78) * w) + g * 0.05, 0.18, 0.94);

    out.totalEnergySum = maxAlb + out.fresnelWet + out.directTransWeight;
    out.allFinite = std::isfinite(out.albedoEffR) && std::isfinite(out.albedoEffG)
                 && std::isfinite(out.albedoEffB) && std::isfinite(out.fresnelWet)
                 && std::isfinite(out.directTransWeight) && std::isfinite(out.totalEnergySum);
    out.energyConserved = out.allFinite && (out.totalEnergySum <= 1.0 + 1.0e-9) && (out.directTransWeight >= -1.0e-9);
    return out;
}

inline WaterCloudCouplingSample EvalWaterCloudRoughnessCoupling(
    double cloudTrans,
    double gust01,
    double rainFall,
    double plantedAmbientCorruption = 0.0) {
    WaterCloudCouplingSample out{};
    const double ct = Clamp01(SanitizeFinite(cloudTrans, 1.0));
    const double g = Clamp01(SanitizeFinite(gust01, 0.0));
    const double r = Clamp01(SanitizeFinite(rainFall, 0.0));

    out.sunTransmittance = ct;
    out.roughScale = (std::clamp)(1.0 + (1.0 - ct) * 0.32 + g * 0.28 + r * 0.22, 1.0, 1.85);
    out.causticTransmittance = 0.30 + (1.0 - 0.30) * ct;
    out.crestSssTransmittance = 0.36 + (1.0 - 0.36) * ct;
    out.skyAmbientFactor = 1.0 - plantedAmbientCorruption;
    out.allFinite = std::isfinite(out.sunTransmittance) && std::isfinite(out.roughScale)
                 && std::isfinite(out.causticTransmittance) && std::isfinite(out.crestSssTransmittance)
                 && std::isfinite(out.skyAmbientFactor);
    return out;
}

// Deterministic 100 Hz world-dynamics replay check: proves bit-identical wind phase & gust evolution
// across variable display frame rates (e.g., 60 FPS vs 144 FPS) when driven by DeterministicFramePacer.
inline bool VerifyDeterministic100HzDynamicsReplay(
    const FramePacingPolicy& policy,
    double posX,
    double posY,
    double windX,
    double windY,
    double durationSec,
    double fpsA,
    double fpsB,
    double& outMaxAbsDiff) {
    DeterministicFramePacer pA(policy);
    DeterministicFramePacer pB(policy);
    double simTimeA = 0.0;
    double simTimeB = 0.0;
    const int framesA = static_cast<int>(std::llround(durationSec * fpsA));
    const int framesB = static_cast<int>(std::llround(durationSec * fpsB));
    for (int i = 0; i < framesA; ++i) {
        const FixedStepResult s = pA.Step(1.0 / fpsA, 2.0);
        simTimeA += static_cast<double>(s.substeps) * policy.fixedTickSec;
    }
    for (int i = 0; i < framesB; ++i) {
        const FixedStepResult s = pB.Step(1.0 / fpsB, 2.0);
        simTimeB += static_cast<double>(s.substeps) * policy.fixedTickSec;
    }
    const WorldWindGustSample gA = EvalCoherentWindGust(posX, posY, windX, windY, 0.4, 0.0, simTimeA);
    const WorldWindGustSample gB = EvalCoherentWindGust(posX, posY, windX, windY, 0.4, 0.0, simTimeB);
    outMaxAbsDiff = (std::max)(
        (std::max)(std::abs(gA.swayVelX - gB.swayVelX), std::abs(gA.swayVelY - gB.swayVelY)),
        (std::max)(std::abs(gA.gust01 - gB.gust01), std::abs(gA.swayDeriv - gB.swayDeriv)));
    return gA.allFinite && gB.allFinite && (outMaxAbsDiff <= 1.0e-9);
}

}  // namespace raw::bridge
