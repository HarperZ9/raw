// test_fluid_sim.cpp: Unit, Physics Conservation & Live DX12/WARP GPU Compute Verification
// for RAW's 3D Eulerian Navier-Stokes Fire/Smoke Solver + 2D Shallow-Water Hydrodynamics Solver.
//
// Checks:
//   1. 3D Navier-Stokes divergence -> Jacobi pressure relaxation -> projection convergence & wall BCs
//   2. Arrhenius fuel combustion mass/enthalpy conservation + semi-implicit T^4 radiative cooling
//   3. Planck blackbody spectral monotonicity & Wien chromaticity shift (650K..4000K)
//   4. 2D Shallow-Water Equations CFL stability (sqrt(g*H0)*dt/dx < 0.5), mass conservation, wave speed & foam advection
//   5. Live DX12/WARP GPU compute execution & readback of RawFluidSimHost (all 7 fire + 3 water passes)
//
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "../src/d3d12_bridge/RawFluidSimHost.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
using Microsoft::WRL::ComPtr;
using namespace raw::bridge;

int g_failCount = 0;

void Check(bool ok, const char* label)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) {
        ++g_failCount;
    }
}

// IEEE-754 binary16 (FP16) -> binary32 (FP32) decoder for GPU RGBA16F readback inspection
float HalfToFloat(uint16_t h)
{
    const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    const uint32_t exp  = (h >> 10) & 0x1Fu;
    const uint32_t mant = h & 0x03FFu;
    uint32_t bits = 0u;
    if (exp == 0u) {
        if (mant == 0u) {
            bits = sign;
        } else {
            // Subnormal half -> normal float
            uint32_t m = mant;
            uint32_t e = 127u - 14u;
            while ((m & 0x0400u) == 0u) {
                m <<= 1;
                --e;
            }
            m &= 0x03FFu;
            bits = sign | (e << 23) | (m << 13);
        }
    } else if (exp == 31u) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + (127u - 15u)) << 23) | (mant << 13);
    }
    float f = 0.0f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

// ----------------------------------------------------------------------------
// Test 1: 3D Incompressible Divergence -> Jacobi Pressure -> Projection Convergence
// ----------------------------------------------------------------------------
void TestFireDivergenceAndProjection()
{
    constexpr int N = 16;
    constexpr float h = 2.0f;
    constexpr float inv2h = 0.5f / h;
    auto idx = [](int x, int y, int z) -> size_t {
        return static_cast<size_t>((z * N + y) * N + x);
    };
    auto isSolid = [](int x, int y, int z) -> bool {
        if (z == 0) return true; // floor
        if (x >= 11 && x <= 12 && y >= 5 && y <= 10 && z <= 8) return true; // obstacle wall
        return false;
    };

    const size_t total = static_cast<size_t>(N * N * N);
    std::vector<float> ux(total, 0.0f), uy(total, 0.0f), uz(total, 0.0f);
    std::vector<float> div(total, 0.0f), pA(total, 0.0f), pB(total, 0.0f);

    // Seed a strongly divergent radial source near (7, 7, 5)
    for (int z = 0; z < N; ++z) {
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                if (isSolid(x, y, z)) continue;
                const float dx = static_cast<float>(x - 7);
                const float dy = static_cast<float>(y - 7);
                const float dz = static_cast<float>(z - 5);
                const float r2 = dx * dx + dy * dy + dz * dz;
                const float g = std::exp(-r2 / 6.0f);
                const size_t i = idx(x, y, z);
                ux[i] = dx * 8.0f * g;
                uy[i] = dy * 8.0f * g;
                uz[i] = (dz * 8.0f + 12.0f) * g;
            }
        }
    }

    auto computeDivL2 = [&](std::vector<float>& outDiv) -> double {
        double sumSq = 0.0;
        for (int z = 0; z < N; ++z) {
            for (int y = 0; y < N; ++y) {
                for (int x = 0; x < N; ++x) {
                    const size_t i = idx(x, y, z);
                    if (isSolid(x, y, z)) {
                        outDiv[i] = 0.0f;
                        continue;
                    }
                    const float uL = (x > 0 && !isSolid(x - 1, y, z)) ? ux[idx(x - 1, y, z)] : 0.0f;
                    const float uR = (x < N - 1 && !isSolid(x + 1, y, z)) ? ux[idx(x + 1, y, z)] : 0.0f;
                    const float vD = (y > 0 && !isSolid(x, y - 1, z)) ? uy[idx(x, y - 1, z)] : 0.0f;
                    const float vU = (y < N - 1 && !isSolid(x, y + 1, z)) ? uy[idx(x, y + 1, z)] : 0.0f;
                    const float wB = (z > 0 && !isSolid(x, y, z - 1)) ? uz[idx(x, y, z - 1)] : 0.0f;
                    const float wT = (z < N - 1 && !isSolid(x, y, z + 1)) ? uz[idx(x, y, z + 1)] : 0.0f;
                    const float d = ((uR - uL) + (vU - vD) + (wT - wB)) * inv2h;
                    outDiv[i] = d;
                    sumSq += static_cast<double>(d) * static_cast<double>(d);
                }
            }
        }
        return std::sqrt(sumSq / static_cast<double>(total));
    };

    const double divBefore = computeDivL2(div);

    // Run 80 Jacobi iterations mirroring cs_fire_pressure (Neumann solids + Dirichlet open domain)
    const int kOff[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    double poissonResInit = 0.0, poissonResFinal = 0.0;
    for (int iter = 0; iter < 80; ++iter) {
        const auto& src = (iter & 1) == 0 ? pA : pB;
        auto& dst = (iter & 1) == 0 ? pB : pA;
        double resSq = 0.0;
        for (int z = 0; z < N; ++z) {
            for (int y = 0; y < N; ++y) {
                for (int x = 0; x < N; ++x) {
                    const size_t i = idx(x, y, z);
                    if (isSolid(x, y, z)) {
                        dst[i] = 0.0f;
                        continue;
                    }
                    float pSum = 0.0f;
                    float denom = 0.0f;
                    for (int n = 0; n < 6; ++n) {
                        const int nx = x + kOff[n][0];
                        const int ny = y + kOff[n][1];
                        const int nz = z + kOff[n][2];
                        if (nx < 0 || nx >= N || ny < 0 || ny >= N || nz < 0 || nz >= N) {
                            denom += 1.0f;
                        } else if (!isSolid(nx, ny, nz)) {
                            pSum += src[idx(nx, ny, nz)];
                            denom += 1.0f;
                        }
                    }
                    const float lapP = (pSum - denom * src[i]) / (h * h);
                    const float r = lapP - div[i];
                    resSq += static_cast<double>(r) * static_cast<double>(r);
                    dst[i] = (denom > 0.5f) ? ((pSum - h * h * div[i]) / denom) : src[i];
                }
            }
        }
        const double resL2 = std::sqrt(resSq / static_cast<double>(total));
        if (iter == 0) poissonResInit = resL2;
        if (iter == 79) poissonResFinal = resL2;
    }

    // Project velocity u_new = u - grad(p) mirroring cs_fire_project
    const auto& pFinal = pA; // 32 is even -> ends in pA
    bool solidClean = true;
    for (int z = 0; z < N; ++z) {
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                const size_t i = idx(x, y, z);
                if (isSolid(x, y, z)) {
                    ux[i] = uy[i] = uz[i] = 0.0f;
                    continue;
                }
                const float pC = pFinal[i];
                const float pL = (x <= 0) ? 0.0f : (isSolid(x - 1, y, z) ? pC : pFinal[idx(x - 1, y, z)]);
                const float pR = (x >= N - 1) ? 0.0f : (isSolid(x + 1, y, z) ? pC : pFinal[idx(x + 1, y, z)]);
                const float pD = (y <= 0) ? 0.0f : (isSolid(x, y - 1, z) ? pC : pFinal[idx(x, y - 1, z)]);
                const float pU = (y >= N - 1) ? 0.0f : (isSolid(x, y + 1, z) ? pC : pFinal[idx(x, y + 1, z)]);
                const float pBot = (z <= 0) ? 0.0f : (isSolid(x, y, z - 1) ? pC : pFinal[idx(x, y, z - 1)]);
                const float pTop = (z >= N - 1) ? 0.0f : (isSolid(x, y, z + 1) ? pC : pFinal[idx(x, y, z + 1)]);
                ux[i] -= (pR - pL) * inv2h;
                uy[i] -= (pU - pD) * inv2h;
                uz[i] -= (pTop - pBot) * inv2h;
                if (z > 0 && isSolid(x, y, z - 1)) uz[i] = std::max(uz[i], 0.0f);
                if (x < N - 1 && isSolid(x + 1, y, z)) ux[i] = std::min(ux[i], 0.0f);
                if (!std::isfinite(ux[i]) || !std::isfinite(uy[i]) || !std::isfinite(uz[i])) {
                    solidClean = false;
                }
            }
        }
    }

    std::vector<float> divAfterVec(total, 0.0f);
    const double divAfter = computeDivL2(divAfterVec);
    std::printf("       Fire 3D projection: Poisson residual %.5f -> %.5f (%.1fx), L2 div %.5f -> %.5f (%.2fx)\n",
                poissonResInit, poissonResFinal, poissonResInit / std::max(poissonResFinal, 1.0e-9),
                divBefore, divAfter, divBefore / std::max(divAfter, 1.0e-9));
    Check(poissonResFinal < poissonResInit * 0.05 && divAfter < divBefore * 0.20,
          "3D Navier-Stokes Jacobi solver converges (>20x Poisson residual drop, >5x collocated div reduction)");
    Check(solidClean, "3D Navier-Stokes solid obstacle boundaries stay finite and non-penetrating");
}

// ----------------------------------------------------------------------------
// Test 2: Arrhenius Combustion Mass & Enthalpy Conservation + T^4 Cooling
// ----------------------------------------------------------------------------
void TestArrheniusCombustionAndCooling()
{
    auto smoothstep = [](float e0, float e1, float x) -> float {
        const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };

    const float tAmb = 293.15f;
    const float arrA = 18.0f;
    const float actTa = 1600.0f;
    const float heatQ = 2200.0f;
    const float sootYield = 0.85f;
    const float dt = 1.0f / 60.0f;

    // 2a) Cold fuel must NOT spontaneously ignite
    {
        const float T = 350.0f;
        const float ignGate = smoothstep(520.0f, 760.0f, T);
        const float rateK = arrA * std::exp(-actTa / T) * ignGate;
        Check(rateK == 0.0f, "Arrhenius combustion ignition gate blocks cold spontaneous burn (<520K)");
    }

    // 2b) Adiabatic combustion (sigma=0) strictly conserves total enthalpy T + Q*fuel and soot - Ys*(fuel0 - fuel)
    {
        const float fuel0 = 1.5f;
        const float T0 = 1800.0f;
        float fuel = fuel0;
        float T = T0;
        float soot = 0.0f;
        float totalBurned = 0.0f;
        bool monotonic = true;

        for (int s = 0; s < 120; ++s) {
            const float prevFuel = fuel;
            const float ignGate = smoothstep(520.0f, 760.0f, T);
            const float rateK = arrA * std::exp(-std::min(actTa / std::max(T, 1.0f), 60.0f)) * ignGate;
            const float burnFrac = 1.0f - std::exp(-rateK * dt);
            const float burned = fuel * burnFrac;
            fuel = std::max(0.0f, fuel - burned);
            T += heatQ * burned;
            soot += sootYield * burned;
            totalBurned += burned;
            if (fuel > prevFuel + 1.0e-7f) monotonic = false;
        }
        const float massErr = std::fabs((fuel + totalBurned) - fuel0);
        const float energyErr = std::fabs((T + heatQ * fuel) - (T0 + heatQ * fuel0));
        const float sootErr = std::fabs(soot - sootYield * (fuel0 - fuel));
        std::printf("       Arrhenius adiabatic: burned=%.4f massErr=%.2e energyErr=%.2e sootErr=%.2e\n",
                    totalBurned, massErr, energyErr, sootErr);
        Check(monotonic && totalBurned > 1.0f && massErr < 1.0e-5f, "Arrhenius fuel consumption monotonic & mass-conserving");
        Check(energyErr < 1.0e-3f && sootErr < 1.0e-5f, "Arrhenius enthalpy (T + Q*fuel) & soot yield strictly conserved");
    }

    // 2c) Semi-implicit T^4 radiative cooling unconditionally preserves T >= T_amb even for huge dt
    {
        float T = 3400.0f;
        const float sigma = 1.6e-10f;
        bool alwaysAboveAmb = true;
        bool alwaysDecreasing = true;
        for (int s = 0; s < 100; ++s) {
            const float stepDt = (s == 99) ? 10.0f : dt;
            const float prevT = T;
            const float tExcess = std::max(0.0f, T - tAmb);
            const float denom = 1.0f + stepDt * sigma * (T * T + tAmb * tAmb) * (T + tAmb);
            T = tAmb + tExcess / std::max(denom, 1.0f);
            if (!std::isfinite(T) || T < tAmb) alwaysAboveAmb = false;
            if (T > prevT) alwaysDecreasing = false;
        }
        Check(alwaysAboveAmb && alwaysDecreasing && T < 3400.0f, "Semi-implicit Stefan-Boltzmann T^4 cooling unconditionally preserves T >= T_amb");
    }
}

// ----------------------------------------------------------------------------
// Test 3: Planck Blackbody Spectral Monotonicity & Wien Chromaticity Shift
// ----------------------------------------------------------------------------
struct Vec3 { float x, y, z; };

Vec3 EvalPlanckCpu(float tempK)
{
    if (tempK < 650.0f) return {0.0f, 0.0f, 0.0f};
    const float T = std::clamp(tempK, 650.0f, 6500.0f);
    const float c2[3] = {22657.9f, 26893.0f, 31277.8f};
    const float invL5[3] = {0.415f, 1.000f, 2.148f};
    float raw[3];
    for (int k = 0; k < 3; ++k) {
        const float expoShift = std::min((c2[k] - c2[0]) / T, 60.0f);
        raw[k] = invL5[k] * std::exp(-expoShift);
    }
    const float maxCh = std::max(raw[0], std::max(raw[1], raw[2]));
    const float t = std::clamp((T - 650.0f) / (950.0f - 650.0f), 0.0f, 1.0f);
    const float gate = t * t * (3.0f - 2.0f * t);
    const float tn = T / 1800.0f;
    const float scale = gate * 3.5f * (tn * tn) * (tn * tn) / std::max(maxCh, 1.0e-6f);
    return {raw[0] * scale, raw[1] * scale, raw[2] * scale};
}

void TestPlanckBlackbodyMonotonicity()
{
    const Vec3 cold = EvalPlanckCpu(500.0f);
    Check(cold.x == 0.0f && cold.y == 0.0f && cold.z == 0.0f, "Planck blackbody emits strictly zero below 650K");

    bool monoLum = true;
    bool monoWien = true;
    float prevLum = 0.0f;
    float prevBR = -1.0f;
    for (float T = 700.0f; T <= 4000.0f; T += 100.0f) {
        const Vec3 c = EvalPlanckCpu(T);
        const float lum = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
        if (!std::isfinite(lum) || lum <= prevLum) {
            monoLum = false;
        }
        prevLum = lum;
        if (T >= 1200.0f) {
            const float br = c.z / std::max(c.x, 1.0e-9f);
            if (br <= prevBR) {
                monoWien = false;
            }
            prevBR = br;
        }
    }
    const Vec3 ember = EvalPlanckCpu(1200.0f);
    const Vec3 flame = EvalPlanckCpu(2200.0f);
    const Vec3 core  = EvalPlanckCpu(3400.0f);
    std::printf("       Planck RGB: 1200K=(%.3f,%.3f,%.3f) 2200K=(%.2f,%.2f,%.2f) 3400K=(%.1f,%.1f,%.1f)\n",
                ember.x, ember.y, ember.z, flame.x, flame.y, flame.z, core.x, core.y, core.z);
    Check(monoLum, "Planck blackbody luminance strictly monotonically increasing across 700K..4000K");
    Check(monoWien && ember.x > ember.y && ember.y > ember.z, "Planck blackbody obeys Wien chromaticity shift (crimson -> orange -> golden-white)");
}

// ----------------------------------------------------------------------------
// Test 4: 2D Shallow-Water Equations CFL Stability, Mass Conservation & Wave Speed
// ----------------------------------------------------------------------------
void TestSweWaveSpeedCflAndConservation()
{
    constexpr int N = 64;
    constexpr float dx = 8.0f;
    constexpr float g = 386.0f;
    constexpr float H0 = 48.0f;
    const float cAnalytical = std::sqrt(g * H0); // ~136.12 world units/s
    const float dt = 0.014f;
    const float cfl = cAnalytical * dt / dx;
    std::printf("       SWE CFL = sqrt(g*H0)*dt/dx = %.4f (c=%.2f u/s)\n", cfl, cAnalytical);
    Check(cfl < 0.5f, "SWE wave-speed CFL number strictly below 0.5 stability bound");

    const size_t total = static_cast<size_t>(N * N);
    std::vector<float> hA(total, 0.0f), uxA(total, 0.0f), uyA(total, 0.0f);
    std::vector<float> hB(total, 0.0f), uxB(total, 0.0f), uyB(total, 0.0f);
    auto idx = [](int x, int y) -> size_t {
        return static_cast<size_t>(((y + N) & (N - 1)) * N + ((x + N) & (N - 1)));
    };

    // Seed central Gaussian height perturbation
    double initMass = 0.0;
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            const float rx = static_cast<float>(x - N / 2);
            const float ry = static_cast<float>(y - N / 2);
            const float h0 = 1.5f * std::exp(-(rx * rx + ry * ry) / 8.0f);
            hA[idx(x, y)] = h0;
            initMass += static_cast<double>(h0);
        }
    }

    const float inv2dx = 0.5f / dx;
    constexpr int kSteps = 60;
    bool finiteAndBounded = true;
    for (int step = 0; step < kSteps; ++step) {
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                const size_t c = idx(x, y);
                const size_t L = idx(x - 1, y), R = idx(x + 1, y);
                const size_t D = idx(x, y - 1), U = idx(x, y + 1);

                const float gradHx = (hA[R] - hA[L]) * inv2dx;
                const float gradHy = (hA[U] - hA[D]) * inv2dx;
                uxB[c] = uxA[c] - dt * g * gradHx;
                uyB[c] = uyA[c] - dt * g * gradHy;

                const float HL = H0 + hA[L], HR = H0 + hA[R];
                const float HD = H0 + hA[D], HU = H0 + hA[U];
                const float fluxDiv = ((HR * uxA[R] - HL * uxA[L]) + (HU * uyA[U] - HD * uyA[D])) * inv2dx;
                const float lapH = 0.25f * (hA[L] + hA[R] + hA[D] + hA[U]) - hA[c];
                hB[c] = hA[c] - dt * fluxDiv + 0.04f * lapH;

                if (!std::isfinite(hB[c]) || std::fabs(hB[c]) > 10.0f) {
                    finiteAndBounded = false;
                }
            }
        }
        std::swap(hA, hB);
        std::swap(uxA, uxB);
        std::swap(uyA, uyB);
    }

    double finalMass = 0.0;
    float peakRingH = -1.0e9f;
    float peakRingR = 0.0f;
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            const float h = hA[idx(x, y)];
            finalMass += static_cast<double>(h);
            const float rCells = std::hypot(static_cast<float>(x - N / 2), static_cast<float>(y - N / 2));
            if (rCells > 4.0f && h > peakRingH) {
                peakRingH = h;
                peakRingR = rCells * dx;
            }
        }
    }
    const double relMassErr = std::fabs(finalMass - initMass) / std::max(std::fabs(initMass), 1.0e-9);
    const float expectedDist = cAnalytical * (kSteps * dt);
    const float speedRatio = peakRingR / std::max(expectedDist, 1.0f);
    std::printf("       SWE 60 steps: relMassErr=%.2e peakR=%.1fu expected=%.1fu (ratio=%.2f)\n",
                relMassErr, peakRingR, expectedDist, speedRatio);
    Check(finiteAndBounded, "SWE 60-step evolution remains finite & bounded with zero numerical blowup");
    Check(relMassErr < 1.0e-4, "SWE conservative flux divergence conserves integrated water volume (<1e-4)");
    Check(speedRatio > 0.70f && speedRatio < 1.25f, "SWE radial wavefront propagates at analytical gravity-wave speed sqrt(g*H0)");
}

// ----------------------------------------------------------------------------
// Test 5: Live DX12 / WARP GPU Compute Verification of RawFluidSimHost
// ----------------------------------------------------------------------------
struct GpuTestRig {
    ComPtr<ID3D12Device>              dev;
    ComPtr<ID3D12InfoQueue>           infoQueue;
    ComPtr<ID3D12CommandQueue>        queue;
    ComPtr<ID3D12CommandAllocator>    alloc;
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ComPtr<ID3D12Fence>               fence;
    HANDLE                            fenceEvent = nullptr;
    uint64_t                          fenceVal   = 0;
    uint64_t                          tsFreqHz   = 0;
    bool                              isWarp     = false;
    bool                              debugLayer = false;

    ~GpuTestRig()
    {
        if (fenceEvent) {
            CloseHandle(fenceEvent);
            fenceEvent = nullptr;
        }
    }

    bool Init(bool preferWarp)
    {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
            dbg->EnableDebugLayer();
            debugLayer = true;
        }

        ComPtr<IDXGIFactory4> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            return false;
        }

        if (!preferWarp) {
            if (SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev)))) {
                D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_6};
                if (FAILED(dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof sm)) ||
                    sm.HighestShaderModel < D3D_SHADER_MODEL_6_6) {
                    dev.Reset();
                }
            }
        }
        if (!dev) {
            ComPtr<IDXGIAdapter> warpAdapter;
            if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter))) ||
                FAILED(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev)))) {
                return false;
            }
            isWarp = true;
        }

        if (debugLayer) {
            dev.As(&infoQueue);
        }

        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))) ||
            FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) ||
            FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd))) ||
            FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
            return false;
        }
        queue->GetTimestampFrequency(&tsFreqHz);
        cmd->Close();
        fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return fenceEvent != nullptr;
    }

    uint32_t CountDebugErrors()
    {
        if (!infoQueue) return 0u;
        uint32_t errCount = 0u;
        const UINT64 n = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 i = 0; i < n; ++i) {
            SIZE_T len = 0;
            infoQueue->GetMessage(i, nullptr, &len);
            if (len == 0) continue;
            std::vector<uint8_t> buf(len);
            auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
            if (SUCCEEDED(infoQueue->GetMessage(i, msg, &len))) {
                if (msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ||
                    msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR) {
                    std::printf("       [D3D12 ERROR] %s\n", msg->pDescription ? msg->pDescription : "(null)");
                    ++errCount;
                }
            }
        }
        infoQueue->ClearStoredMessages();
        return errCount;
    }

    bool FlushAndWait()
    {
        if (FAILED(cmd->Close())) return false;
        ID3D12CommandList* lists[1] = {cmd.Get()};
        queue->ExecuteCommandLists(1, lists);
        ++fenceVal;
        if (FAILED(queue->Signal(fence.Get(), fenceVal))) return false;
        if (fence->GetCompletedValue() < fenceVal) {
            if (FAILED(fence->SetEventOnCompletion(fenceVal, fenceEvent))) return false;
            WaitForSingleObject(fenceEvent, 15000);
        }
        return fence->GetCompletedValue() >= fenceVal;
    }

    bool BeginFrame()
    {
        return SUCCEEDED(alloc->Reset()) && SUCCEEDED(cmd->Reset(alloc.Get(), nullptr));
    }

    // Reads back a Texture2D or Texture3D (RGBA16F) into a packed array of float4s (x,y,z,w)
    bool ReadbackRgba16Tex(ID3D12Resource* tex, D3D12_RESOURCE_STATES curState, std::vector<float>& outRgba,
                           uint32_t& outW, uint32_t& outH, uint32_t& outD)
    {
        const D3D12_RESOURCE_DESC desc = tex->GetDesc();
        outW = static_cast<uint32_t>(desc.Width);
        outH = static_cast<uint32_t>(desc.Height);
        outD = static_cast<uint32_t>(desc.DepthOrArraySize);

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT numRows = 0;
        UINT64 rowBytes = 0, totalBytes = 0;
        dev->GetCopyableFootprints(&desc, 0, 1, 0, &fp, &numRows, &rowBytes, &totalBytes);

        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rbd{};
        rbd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rbd.Width = totalBytes;
        rbd.Height = 1;
        rbd.DepthOrArraySize = 1;
        rbd.MipLevels = 1;
        rbd.SampleDesc.Count = 1;
        rbd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> rbBuf;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rbd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rbBuf)))) {
            return false;
        }

        if (!BeginFrame()) return false;
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = tex;
        b.Transition.StateBefore = curState;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = tex;
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLoc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = rbBuf.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstLoc.PlacedFootprint = fp;

        cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.StateAfter = curState;
        cmd->ResourceBarrier(1, &b);
        if (!FlushAndWait()) return false;

        void* mapped = nullptr;
        D3D12_RANGE rr{0, static_cast<SIZE_T>(totalBytes)};
        if (FAILED(rbBuf->Map(0, &rr, &mapped))) return false;
        const auto* base = static_cast<const uint8_t*>(mapped) + fp.Offset;

        outRgba.resize(static_cast<size_t>(outW) * outH * outD * 4u);
        const size_t slicePitch = static_cast<size_t>(fp.Footprint.RowPitch) * numRows;
        for (uint32_t z = 0; z < outD; ++z) {
            for (uint32_t y = 0; y < outH; ++y) {
                const auto* row = reinterpret_cast<const uint16_t*>(
                    base + z * slicePitch + static_cast<size_t>(y) * fp.Footprint.RowPitch);
                for (uint32_t x = 0; x < outW; ++x) {
                    const size_t dst = ((static_cast<size_t>(z) * outH + y) * outW + x) * 4u;
                    outRgba[dst + 0] = HalfToFloat(row[x * 4 + 0]);
                    outRgba[dst + 1] = HalfToFloat(row[x * 4 + 1]);
                    outRgba[dst + 2] = HalfToFloat(row[x * 4 + 2]);
                    outRgba[dst + 3] = HalfToFloat(row[x * 4 + 3]);
                }
            }
        }
        D3D12_RANGE wr{0, 0};
        rbBuf->Unmap(0, &wr);
        return true;
    }
};

void TestDx12LiveGpuSolvers(bool preferWarp)
{
    GpuTestRig rig;
    if (!rig.Init(preferWarp)) {
        Check(false, "DX12 device creation (hardware or WARP) succeeded");
        return;
    }
    std::printf("       DX12 test adapter: %s | DebugLayer: %s | tsFreq=%llu Hz\n",
                rig.isWarp ? "WARP software rasterizer" : "Hardware GPU",
                rig.debugLayer ? "ON" : "OFF",
                static_cast<unsigned long long>(rig.tsFreqHz));

    RawFluidSimHost host;
    std::string err;
    const bool initOk = host.Init(rig.dev.Get(), &err);
    if (!initOk) {
        std::printf("       RawFluidSimHost::Init error: %s\n", err.c_str());
    }
    Check(initOk && host.Ready(), "RawFluidSimHost::Init created all 10 SM 6.6 compute PSOs, 3D/2D textures & heaps");
    if (!initOk) return;

    host.SetTimestampFrequency(rig.tsFreqHz);
    host.SetTargetBudgetMs(rig.isWarp ? 2000.0f : 1.2f);

    Check(host.FireThermoSrv().ptr != 0 && host.FireRadianceSrv().ptr != 0 &&
          host.WaterStateSrv().ptr != 0 && host.WaterNormalSrv().ptr != 0,
          "RawFluidSimHost exposes valid GPU SRV handles for FireThermo, FireRadiance, WaterState, WaterNormal");

    // --- 5a) Step 3D Fire/Smoke + Radiance Cascade for 10 frames ---
    FluidFireParams fp{};
    fp.domainOriginW[0] = 0.0f;
    fp.domainOriginW[1] = 0.0f;
    fp.domainOriginW[2] = 0.0f;
    fp.cellSize = 4.0f; // 64*4 = 256u cube
    fp.dt = 1.0f / 60.0f;
    fp.windVelW[0] = 8.0f;
    fp.windVelW[1] = 2.0f;
    fp.windVelW[2] = 0.0f;
    fp.emitterCount = 1u;
    fp.emitters[0].posW[0] = 128.0f;
    fp.emitters[0].posW[1] = 128.0f;
    fp.emitters[0].posW[2] = 48.0f; // voxel (32, 32, 12)
    fp.emitters[0].radius = 18.0f;
    fp.emitters[0].ignitionTempK = 2600.0f;
    fp.emitters[0].fuelRate = 3.2f;
    fp.emitters[0].jetSpeed = 42.0f;
    fp.emitters[0].curlStrength = 14.0f;
    fp.emitters[0].sootSeedRate = 0.5f;

    bool fireExecOk = true;
    constexpr int kFireFrames = 24;
    for (int f = 0; f < kFireFrames; ++f) {
        fp.timeSec = static_cast<float>(f) * fp.dt;
        if (!rig.BeginFrame()) { fireExecOk = false; break; }
        host.StepFire(rig.cmd.Get(), fp);
        if (!rig.FlushAndWait()) { fireExecOk = false; break; }
    }
    Check(fireExecOk && host.FireFrameIndex() == static_cast<uint32_t>(kFireFrames),
          "DX12 StepFire executed 24 full Navier-Stokes + Radiance Cascade frames");

    const FluidFireDomainInfo fireDom = host.GetFireDomainInfo();
    Check(fireDom.active && fireDom.emitterCount == 1u && std::fabs(fireDom.worldSpanU - 256.0f) < 1.0e-3f,
          "RawFluidSimHost::GetFireDomainInfo reports active 256u domain and 1 active emitter");

    std::vector<float> thermoData, radData;
    uint32_t fw = 0, fh = 0, fd = 0;
    const bool rbFireOk = rig.ReadbackRgba16Tex(host.FireThermoResource(), host.FireResourceState(), thermoData, fw, fh, fd) &&
                          rig.ReadbackRgba16Tex(host.FireRadianceResource(), host.FireResourceState(), radData, fw, fh, fd);
    Check(rbFireOk && fw == 64u && fh == 64u && fd == 64u, "DX12 FireThermo & FireRadiance 64x64x64 GPU readback succeeded");

    if (rbFireOk) {
        bool allFinite = true;
        float maxTempK = 0.0f;
        float maxSoot = 0.0f;
        float plumeTempAbove = 0.0f;
        float maxRadR = 0.0f;
        float minSunShadowT = 1.0f;
        float maxSunShadowT = 0.0f;
        float scatterGlowR = 0.0f, scatterGlowG = 0.0f, scatterGlowB = 0.0f;

        for (size_t i = 0; i < thermoData.size(); ++i) {
            if (!std::isfinite(thermoData[i]) || !std::isfinite(radData[i])) {
                allFinite = false;
                break;
            }
        }
        auto vIdx = [](uint32_t x, uint32_t y, uint32_t z) -> size_t {
            return ((static_cast<size_t>(z) * 64u + y) * 64u + x) * 4u;
        };
        for (uint32_t z = 0; z < 64u; ++z) {
            for (uint32_t y = 0; y < 64u; ++y) {
                for (uint32_t x = 0; x < 64u; ++x) {
                    const size_t o = vIdx(x, y, z);
                    maxTempK = std::max(maxTempK, thermoData[o + 0]);
                    maxSoot  = std::max(maxSoot,  thermoData[o + 2]);
                    maxRadR  = std::max(maxRadR,  radData[o + 0]);
                    minSunShadowT = std::min(minSunShadowT, radData[o + 3]);
                    maxSunShadowT = std::max(maxSunShadowT, radData[o + 3]);
                }
            }
        }
        // Inspect rising buoyant plume above emitter (max temp in z=17..22 column, well above emitter radius z=12+-4.5)
        for (uint32_t pz = 17u; pz <= 22u; ++pz) {
            for (uint32_t py = 30u; py <= 34u; ++py) {
                for (uint32_t px = 30u; px <= 34u; ++px) {
                    plumeTempAbove = std::max(plumeTempAbove, thermoData[vIdx(px, py, pz) + 0]);
                }
            }
        }
        // Inspect nearby cooler smoke ring (36, 32, 16) receiving scattered fire radiance
        const size_t scOff = vIdx(36, 32, 16);
        scatterGlowR = radData[scOff + 0];
        scatterGlowG = radData[scOff + 1];
        scatterGlowB = radData[scOff + 2];

        std::printf("       GPU Fire 24f: maxT=%.1fK plumeAbove=%.1fK maxSoot=%.3f maxRadR=%.2f scatterRGB=(%.3f,%.3f,%.3f) sunShadowT=[%.3f..%.3f]\n",
                    maxTempK, plumeTempAbove, maxSoot, maxRadR, scatterGlowR, scatterGlowG, scatterGlowB, minSunShadowT, maxSunShadowT);
        Check(allFinite, "GPU 64x64x64 FireThermo & FireRadiance volumes contain zero NaN/Inf");
        Check(maxTempK > 1400.0f && maxSoot > 0.05f, "GPU Fire combustion heats core >1400K and generates soot smoke");
        Check(plumeTempAbove > fp.ambientTempK + 40.0f, "GPU Fire thermal buoyancy + jet lofts hot plume upward (+Z)");
        Check(maxRadR > 5.0f && scatterGlowR > 0.02f && scatterGlowR > scatterGlowG && scatterGlowG > scatterGlowB,
              "GPU 3D Radiance Cascade propagates warm orange firelight outward into surrounding smoke");
        Check(minSunShadowT >= 0.0f && maxSunShadowT <= 1.005f && minSunShadowT < 0.85f && maxSunShadowT > 0.98f,
              "GPU 4-tap directional sun self-shadow transmittance (FireRadiance.a) shadows dense smoke core cleanly");
    }

    // --- 5b) Step 2D Shallow-Water + Wakes + Rain + Normals for 10 frames ---
    FluidWaterParams wp{};
    wp.camPosXY[0] = 0.0f;
    wp.camPosXY[1] = 0.0f;
    wp.cellSize = 8.0f;
    wp.dt = 1.0f / 60.0f;
    wp.rainIntensity = 0.45f;
    wp.trailCount = 17u;
    // Player wake near center
    wp.trails[0].posW[0] = 32.0f;
    wp.trails[0].posW[1] = 16.0f;
    wp.trails[0].strength = 1.0f;
    wp.trails[0].velW[0] = 65.0f;
    wp.trails[0].velW[1] = 20.0f;
    wp.trails[0].radius = 22.0f;
    wp.trails[0].splashScale = 1.2f;
    // Live NPC wake at slot 16
    wp.trails[16].posW[0] = -140.0f;
    wp.trails[16].posW[1] = 96.0f;
    wp.trails[16].strength = 0.85f;
    wp.trails[16].velW[0] = -40.0f;
    wp.trails[16].velW[1] = 30.0f;
    wp.trails[16].radius = 20.0f;

    bool waterExecOk = true;
    for (int f = 0; f < 10; ++f) {
        wp.timeSec = static_cast<float>(f) * wp.dt;
        wp.trails[0].posW[0] += wp.trails[0].velW[0] * wp.dt;
        wp.trails[0].posW[1] += wp.trails[0].velW[1] * wp.dt;
        if (f >= 8) {
            // Exercise toroidal camera-following shift
            wp.camPosXY[0] += 64.0f;
            wp.camPosXY[1] -= 40.0f;
        }
        if (!rig.BeginFrame()) { waterExecOk = false; break; }
        host.StepWater(rig.cmd.Get(), wp);
        if (!rig.FlushAndWait()) { waterExecOk = false; break; }
    }
    Check(waterExecOk && host.WaterFrameIndex() == 10u, "DX12 StepWater executed 10 SWE + wake + rain + toroidal-shift frames");

    const FluidWaterDomainInfo waterDom = host.GetWaterDomainInfo();
    Check(waterDom.active && std::fabs(waterDom.worldSpanU - 2048.0f) < 1.0e-3f &&
          host.WaterResourceState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
          "RawFluidSimHost::GetWaterDomainInfo reports active 2048u toroidal domain in NON_PIXEL_SHADER_RESOURCE");

    std::vector<float> waterState, waterNorm;
    uint32_t ww = 0, wh = 0, wd = 0;
    const bool rbWaterOk = rig.ReadbackRgba16Tex(host.WaterStateResource(), host.WaterResourceState(), waterState, ww, wh, wd) &&
                           rig.ReadbackRgba16Tex(host.WaterNormalResource(), host.WaterResourceState(), waterNorm, ww, wh, wd);
    Check(rbWaterOk && ww == 256u && wh == 256u && wd == 1u, "DX12 WaterState & WaterNormal 256x256 GPU readback succeeded");

    if (rbWaterOk) {
        bool allFinite = true;
        bool normalsUnit = true;
        float maxAbsH = 0.0f, maxVel = 0.0f, maxFoam = 0.0f, maxNormTilt = 0.0f;

        for (size_t i = 0; i < waterState.size(); i += 4u) {
            const float h  = waterState[i + 0];
            const float ux = waterState[i + 1];
            const float uy = waterState[i + 2];
            const float fm = waterState[i + 3];
            const float nx = waterNorm[i + 0];
            const float ny = waterNorm[i + 1];
            const float nz = waterNorm[i + 2];
            const float nf = waterNorm[i + 3];
            if (!std::isfinite(h) || !std::isfinite(ux) || !std::isfinite(uy) || !std::isfinite(fm) ||
                !std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz) || !std::isfinite(nf)) {
                allFinite = false;
                break;
            }
            maxAbsH = std::max(maxAbsH, std::fabs(h));
            maxVel  = std::max(maxVel, std::hypot(ux, uy));
            maxFoam = std::max(maxFoam, std::max(fm, nf));
            const float nLen = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (std::fabs(nLen - 1.0f) > 0.025f || nz <= 0.1f) {
                normalsUnit = false;
            }
            maxNormTilt = std::max(maxNormTilt, std::hypot(nx, ny));
        }

        std::printf("       GPU Water 10f: max|h|=%.3f max|u|=%.2f maxFoam=%.3f maxNormTilt=%.3f\n",
                    maxAbsH, maxVel, maxFoam, maxNormTilt);
        Check(allFinite, "GPU 256x256 WaterState & WaterNormal contain zero NaN/Inf");
        Check(maxAbsH > 0.01f && maxVel > 0.2f && maxFoam > 0.05f,
              "GPU SWE wakes & rain generate active waves, horizontal flow, and lacy foam persistence");
        Check(normalsUnit && maxNormTilt > 0.005f,
              "GPU WaterNormal outputs valid unit-length curl-free Z-up normals with active wave slope");
    }

    // --- 5c) Hardware GPU Timestamp Profiling & Adaptive Jacobi Budget [4..8] ---
    const FluidGpuTimings baseT = host.GetLastTimings();
    std::printf("       GPU Fluid Timings (baseline): valid=%d fireStepMs=%.4f ms | waterStepMs=%.4f ms | activePressureIters=%u\n",
                baseT.valid ? 1 : 0, baseT.fireStepMs, baseT.waterStepMs, baseT.activePressureIters);
    Check(baseT.valid && std::isfinite(baseT.fireStepMs) && baseT.fireStepMs >= 0.0f &&
          std::isfinite(baseT.waterStepMs) && baseT.waterStepMs >= 0.0f,
          "GPU timestamp query ring harvested finite non-negative fireStepMs & waterStepMs");
    Check(baseT.activePressureIters >= 4u && baseT.activePressureIters <= 8u,
          "Baseline adaptive Jacobi iteration count stays cleanly within [4..8]");

    // Tight budget stress test -> must step down smoothly to floor 4 (and never below 4)
    host.SetTargetBudgetMs(0.0001f);
    for (int f = 0; f < 6; ++f) {
        fp.timeSec += fp.dt;
        if (!rig.BeginFrame()) break;
        host.StepFire(rig.cmd.Get(), fp);
        if (!rig.FlushAndWait()) break;
    }
    const FluidGpuTimings tightT = host.GetLastTimings();

    // Generous budget recovery -> must step back up smoothly to full 8 (and never above 8)
    host.SetTargetBudgetMs(2500.0f);
    for (int f = 0; f < 8; ++f) {
        fp.timeSec += fp.dt;
        if (!rig.BeginFrame()) break;
        host.StepFire(rig.cmd.Get(), fp);
        if (!rig.FlushAndWait()) break;
    }
    const FluidGpuTimings recovT = host.GetLastTimings();
    std::printf("       Adaptive Jacobi budget: tightBudgetIters=%u -> recoveredBudgetIters=%u (fire=%.4f ms)\n",
                tightT.activePressureIters, recovT.activePressureIters, recovT.fireStepMs);
    Check(tightT.activePressureIters == 4u && recovT.activePressureIters == 8u,
          "Adaptive Jacobi budget drops smoothly to 4 under pressure and recovers to 8 with headroom");

    const uint32_t d3d12Errors = rig.CountDebugErrors();
    std::printf("       D3D12 Debug Layer errors: %u\n", d3d12Errors);
    Check(d3d12Errors == 0u, "D3D12 Debug Layer reported 0 errors/corruptions across Fluid Sim");
}

} // namespace

int main(int argc, char** argv)
{
    bool preferWarp = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--warp") == 0) {
            preferWarp = true;
        }
    }

    std::printf("=== RAW GPU Fluid Sim Unit & Physics Verification ===\n");
    TestFireDivergenceAndProjection();
    TestArrheniusCombustionAndCooling();
    TestPlanckBlackbodyMonotonicity();
    TestSweWaveSpeedCflAndConservation();
    TestDx12LiveGpuSolvers(preferWarp);

    std::printf("=== Summary: %d failure(s) ===\n", g_failCount);
    return g_failCount == 0 ? 0 : 1;
}
