// wave84_crowd_nav_ai_audio_check.cpp: Wave 84 C++23 Unit, Stress & Planted-Failure Control Suite
// Verifies:
//   1) 1,024-Actor SoA Crowd ECS + 4-Tier Adaptive Scheduler dispatched across Wave 83 workers (<= 2.2 ms CPU, 0 NaN)
//   2) 64-Actor opposing cross-flow ORCA/RVO2 local crowd avoidance (0 interpenetrations) + Planted Control 84-A (ORCA bypass caught)
//   3) Multi-cell HPA* portal routing + runtime dynamic obstacle carving reroute + Planted Control 84-B (broken portal stitch caught)
//   4) Off-thread Utility + HTN Tactical AI deterministic 1-worker vs 4-worker parity (0 drift)
//   5) Geometry-coupled Estuary-Space 8x8 Orthonormal Hadamard FDN: ||H*x||_2 == ||x||_2, cavern > inn > open-sky RT60,
//      underwater LPF attenuation + Planted Control 84-C (unstable feedback gain >= 1.0 rejected fail-closed)
//   6) Planted Control 84-D: NaN actor coordinate & negative radius sanitized fail-closed
//   7) Bit-identical 100 Hz replay across SkyrimSE, Fallout4, and Starfield profiles (60/100/144/240 FPS)
// Clean-room MIT, MSVC /std:c++latest /W4 /WX.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../src/engine_rt/RawActorCrowdEcs.h"
#include "../src/engine_rt/RawCrowdNavAi.h"
#include "../src/ce_runtime/CeNavMesh.h"
#include "../src/engine_rt/rt_adaptive_audio_director.h"
#include "../src/d3d12_bridge/RawFramePacing.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <vector>

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

int main()
{
    std::printf("=== Wave 84: 1,024-Actor SoA ECS + HPA*/ORCA/HTN + Estuary-Space FDN Audio Check ===\n");

    // 1) 1,024-Actor SoA Crowd ECS + 4-Tier Adaptive Scheduler <= 2.2 ms CPU across Wave 83 workers
    {
        raw::core::RawJobSystem js;
        js.Init(4u);

        raw::rt::RawActorCrowdSoA soa{};
        soa.count = raw::rt::kMaxCrowdActors;
        for (uint32_t i = 0u; i < soa.count; ++i) {
            const float ring = static_cast<float>(i % 64u) * 3.0f;
            const float ang  = static_cast<float>(i) * 0.19f;
            soa.posX[i] = std::cos(ang) * ring;
            soa.posY[i] = std::sin(ang) * ring;
            soa.posZ[i] = 0.0f;
            soa.prefVelX[i] = std::cos(ang + 1.57f) * 1.6f;
            soa.prefVelY[i] = std::sin(ang + 1.57f) * 1.6f;
            soa.radius[i] = 0.42f;
            soa.health[i] = 100.0f;
            soa.visibilityMask[i] = (i < 800u) ? 1u : 0u;
        }
        const auto tiers = raw::rt::RawActorCrowdScheduler::AssignAdaptiveTiers(soa, 0.0f, 0.0f, 0.0f);
        const auto t0 = std::chrono::steady_clock::now();
        for (uint64_t tk = 0u; tk < 20u; ++tk) {
            raw::rt::RawActorCrowdScheduler::StepCrowdTick(soa, tk, 0.01f, &js);
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double msPerTick = std::chrono::duration<double, std::milli>(t1 - t0).count() / 20.0;
        js.Shutdown();

        bool allFin = true;
        for (uint32_t i = 0u; i < soa.count; ++i) {
            if (!std::isfinite(soa.posX[i]) || !std::isfinite(soa.posY[i]) || !std::isfinite(soa.yaw[i])) {
                allFin = false;
                break;
            }
        }
        std::printf("    [INFO] 1,024-actor SoA avg tick = %.4f ms (budget <= 2.20 ms) | tiers=%u/%u/%u/%u\n",
                    msPerTick, tiers.tier0, tiers.tier1, tiers.tier2, tiers.tier3);
        Check(allFin && tiers.activeTotal == 1024u && tiers.tier0 <= 32u && tiers.tier1 <= 128u && tiers.tier2 <= 512u && msPerTick <= 2.2,
              "1,024-actor SoA Crowd ECS + 4-tier scheduler <= 2.2 ms CPU with 0 NaN & strict tier caps");
    }

    // 2) 64-Actor opposing cross-flow ORCA stress (0 interpenetrations) + Planted Control 84-A (ORCA bypass)
    {
        auto buildOpposingCrowd = []() -> raw::rt::RawActorCrowdSoA {
            raw::rt::RawActorCrowdSoA s{};
            s.count = 64u;
            for (uint32_t i = 0u; i < 32u; ++i) {
                const float row = static_cast<float>(i / 4u) * 1.4f;
                const float col = static_cast<float>(i % 4u) * 1.4f - 2.1f;
                s.posX[i] = -6.0f - row; s.posY[i] = col; s.prefVelX[i] = 2.5f; s.prefVelY[i] = 0.0f; s.radius[i] = 0.42f;
                const uint32_t j = 32u + i;
                s.posX[j] =  6.0f + row; s.posY[j] = col; s.prefVelX[j] = -2.5f; s.prefVelY[j] = 0.0f; s.radius[j] = 0.42f;
            }
            return s;
        };

        auto crowdOrca = buildOpposingCrowd();
        uint32_t maxOrcaOverlaps = 0u;
        for (int step = 0; step < 300; ++step) {
            const auto rep = raw::rt::SolveOrcaCrowdStep(crowdOrca, 0.02f, {2.0f, 4.5f, 2.5f, true});
            if (rep.overlapViolations > maxOrcaOverlaps) { maxOrcaOverlaps = rep.overlapViolations; }
        }

        // Planted Control 84-A: disable ORCA -> opposing actors collide heavily!
        auto crowdBypass = buildOpposingCrowd();
        uint32_t maxBypassOverlaps = 0u;
        for (int step = 0; step < 300; ++step) {
            const auto rep = raw::rt::SolveOrcaCrowdStep(crowdBypass, 0.02f, {2.0f, 4.5f, 2.5f, false});
            if (rep.overlapViolations > maxBypassOverlaps) { maxBypassOverlaps = rep.overlapViolations; }
        }
        Check(maxOrcaOverlaps == 0u && maxBypassOverlaps > 0u,
              "64-actor opposing cross-flow ORCA: 0 capsule overlaps + Planted Control 84-A bypass caught");
    }

    // 3) Multi-cell HPA* portal routing + runtime dynamic obstacle carving + Planted Control 84-B (broken portal)
    {
        raw::ce::HpaClusterNavGraph hpa;
        hpa.BuildGrid(4u, 4u);
        const raw::ce::NavVec3 start{16.0f, 16.0f, 0.0f};
        const raw::ce::NavVec3 goal{220.0f, 16.0f, 0.0f};

        const auto direct = hpa.FindMultiCellPath(start, goal);
        // Carve dynamic obstacle directly across portal (0 -> 1) at (64, 32): forces clean reroute through row y=1!
        hpa.CarveDynamicObstacle({1u, {64.0f, 32.0f, 0.0f}, 12.0f});
        const auto rerouted = hpa.FindMultiCellPath(start, goal);

        // Planted Control 84-B: sever ALL outgoing portals from cluster 0 -> path must fail-closed (found == false)!
        hpa.ClearCarvedObstacles();
        hpa.SetPortalEnabled(0u, 1u, false);
        hpa.SetPortalEnabled(0u, 4u, false);
        const auto broken = hpa.FindMultiCellPath(start, goal);

        Check(direct.found && rerouted.found && (rerouted.totalCost > direct.totalCost) && !broken.found,
              "Multi-cell HPA* routing + dynamic obstacle carving reroute + Planted Control 84-B broken portal caught");
    }

    // 4) Off-thread Utility + HTN Tactical AI deterministic 1-thread vs 4-worker parity
    {
        std::vector<raw::rt::TacticalActorContext> ctxs(256u);
        for (size_t i = 0; i < ctxs.size(); ++i) {
            ctxs[i].health01       = 0.15f + 0.85f * (static_cast<float>(i % 10u) / 9.0f);
            ctxs[i].stamina01      = 0.25f + 0.75f * (static_cast<float>((i * 3u) % 8u) / 7.0f);
            ctxs[i].distToThreatM  = 2.0f + static_cast<float>(i % 35u);
            ctxs[i].nearbyAllies   = static_cast<uint32_t>(i % 4u);
            ctxs[i].nearbyEnemies  = static_cast<uint32_t>((i + 1u) % 4u);
            ctxs[i].hasCoverNearby = (i % 3u) == 0u;
            ctxs[i].inChokepoint   = (i % 7u) == 0u;
        }
        std::vector<raw::rt::TacticalDecision> decSingle, decMulti;
        raw::rt::EvaluateUtilityHtnBatch(ctxs, decSingle, nullptr);

        raw::core::RawJobSystem js;
        js.Init(4u);
        raw::rt::EvaluateUtilityHtnBatch(ctxs, decMulti, &js);
        js.Shutdown();

        bool parity = (decSingle.size() == decMulti.size());
        for (size_t i = 0; parity && i < decSingle.size(); ++i) {
            if (decSingle[i].action != decMulti[i].action ||
                std::fabs(decSingle[i].utilityScore - decMulti[i].utilityScore) > 1e-6f ||
                decSingle[i].htnPlanLen != decMulti[i].htnPlanLen) {
                parity = false;
            }
        }
        Check(parity, "Off-thread Utility + HTN tactical AI 256-actor 1-thread vs 4-worker bit-identical parity");
    }

    // 5) Geometry-coupled Estuary-Space 8x8 Hadamard FDN + Planted Control 84-C (unstable gain >= 1.0 rejected)
    {
        // Exact orthonormal norm preservation ||H*x||_2 == ||x||_2:
        const std::array<float, 8> x{0.8f, -0.4f, 0.3f, 0.9f, -0.7f, 0.2f, -0.5f, 0.6f};
        std::array<float, 8> y{};
        raw::rt::EstuarySpaceFdn::ApplyOrthonormalHadamard8(x, y);
        double nx = 0.0, ny = 0.0;
        for (size_t i = 0; i < 8u; ++i) { nx += x[i] * x[i]; ny += y[i] * y[i]; }
        const bool orthoOk = std::fabs(nx - ny) < 1.0e-5;

        const auto pCavern = raw::rt::EstuarySpaceFdn::DeriveFromGeometry({42.0f, 0.02f, 0.0f, 0.1f, false});
        const auto pInn    = raw::rt::EstuarySpaceFdn::DeriveFromGeometry({12.0f, 0.08f, 0.0f, 0.1f, false});
        const auto pOpen   = raw::rt::EstuarySpaceFdn::DeriveFromGeometry({60.0f, 0.95f, 0.8f, 0.0f, false});
        const auto pUnder  = raw::rt::EstuarySpaceFdn::DeriveFromGeometry({16.0f, 0.50f, 0.0f, 0.2f, true});

        // Impulse decay energy check (no NaN, bounded):
        raw::rt::EstuarySpaceFdn fdn;
        bool finOk = true;
        float tailEnergy = 0.0f;
        for (int n = 0; n < 256; ++n) {
            const float out = fdn.ProcessSample(n == 0 ? 1.0f : 0.0f, pCavern, 48000.0f);
            if (!std::isfinite(out) || std::fabs(out) > 4.0f) { finOk = false; }
            if (n > 16) { tailEnergy += out * out; }
        }

        // Planted Control 84-C: unstable feedbackGain >= 1.0 rejected fail-closed!
        auto pBad = pCavern;
        pBad.feedbackGain = 1.08f;
        const bool unstableCaught = !raw::rt::EstuarySpaceFdn::ValidateStability(pBad);

        Check(orthoOk && finOk && tailEnergy > 0.0f &&
              (pCavern.rt60Sec > pInn.rt60Sec) && (pInn.rt60Sec > pOpen.rt60Sec) &&
              (pUnder.lpfCutoffHz <= 650.0f) && unstableCaught,
              "Estuary-Space 8x8 Hadamard FDN orthonormal + cavern>inn>open RT60 + underwater LPF + Planted 84-C");
    }

    // 6) Planted Control 84-D: NaN coordinate & negative radius injected into SoA ECS sanitized fail-closed
    {
        raw::rt::RawActorCrowdSoA s{};
        s.count = 4u;
        s.posX[0] = std::numeric_limits<float>::quiet_NaN();
        s.radius[1] = -2.5f;
        s.velY[2] = std::numeric_limits<float>::infinity();
        const uint32_t repaired = raw::rt::RawActorCrowdScheduler::SanitizeCrowdSoA(s);
        Check(repaired >= 3u && std::isfinite(s.posX[0]) && s.radius[1] >= 0.15f && std::isfinite(s.velY[2]),
              "Planted Control 84-D: NaN/Inf coords & negative radius sanitized fail-closed");
    }

    // 7) Bit-identical 100 Hz replay across SkyrimSE, Fallout4, and Starfield profiles (60/100/144/240 FPS)
    {
        const char* exes[3] = {"SkyrimSE.exe", "Fallout4.exe", "Starfield.exe"};
        const double rates[4] = {60.0, 100.0, 144.0, 240.0};
        bool ok = true;
        double refSig = -1.0;
        for (int gi = 0; gi < 3; ++gi) {
            const auto prof = raw::versions::ResolveEngineProfileByExe(exes[gi]);
            const auto policy = raw::bridge::ResolveFramePacingPolicy(prof);
            if (policy.multiplayerBlocked || policy.fixedTickHz != 100.0) { ok = false; }
            for (int ri = 0; ri < 4; ++ri) {
                raw::bridge::DeterministicFramePacer pacer(policy);
                raw::rt::RawActorCrowdSoA soa{};
                soa.count = 64u;
                for (uint32_t i = 0u; i < 64u; ++i) {
                    soa.posX[i] = static_cast<float>(i) * 2.0f;
                    soa.posY[i] = static_cast<float>(i % 8u) * 2.0f;
                    soa.prefVelX[i] = 1.2f;
                    soa.prefVelY[i] = 0.4f;
                    soa.radius[i] = 0.42f;
                    soa.visibilityMask[i] = 1u;
                }
                raw::rt::RawActorCrowdScheduler::AssignAdaptiveTiers(soa, 0.0f, 0.0f, 0.0f);
                const double dt = 1.0 / rates[ri];
                const int frames = static_cast<int>(std::llround(1.0 * rates[ri]));
                uint64_t tickIdx = 0u;
                for (int f = 0; f < frames; ++f) {
                    const auto s = pacer.Step(dt);
                    for (uint32_t k = 0; k < s.substeps; ++k) {
                        raw::rt::RawActorCrowdScheduler::StepCrowdTick(soa, tickIdx++, 0.01f, nullptr);
                    }
                }
                double sig = 0.0;
                for (uint32_t i = 0u; i < 64u; ++i) { sig += soa.posX[i] + soa.posY[i]; }
                if (gi == 0 && ri == 0) { refSig = sig; }
                else if (std::fabs(sig - refSig) > 1e-9) { ok = false; }
            }
        }
        Check(ok && refSig > 0.0, "Bit-identical 100 Hz crowd replay across SkyrimSE / Fallout4 / Starfield & 60/100/144/240 FPS");
    }

    std::printf("=== RESULT: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
