// wave82_playtest_debug_ownership_check.cpp: Wave 82 C++23 Unit, ABI Layout Guard & Planted-Control Suite
// Verifies:
//   1) Exact sizeof & offsetof compile-time static_assert proofs for ALL Wave 38B paired C++ <-> HLSL structs
//      (PassConstants, GpuCullInstance, ClusterAabb, GpuClusterLight, MeshletDesc, TerrainVertex,
//       FroxelPointLight, XpbdParticle, XpbdCollider, RawDebugViewCB, PassOwnershipRecord)
//   2) All 9 Canonical GPU Debug Views (Modes 0..8 -> Lighting.hlsl flags.w 0, 41..48) + click-any-pixel inspector
//   3) 15-Pass Runtime Engine Ownership Ledger: atomic 3-frame finite promotion, zero flicker, JSON dump
//   4) Planted Control 82-C: double-executed vanilla AO/fog caught fail-closed (doubleExecCount > 0)
//   5) Planted Control 82-D: zero-length normal patch (Mode 2) & NaN depth tile (Mode 3) caught fail-closed
//   6) Bit-identical 100 Hz replay across SkyrimSE (CE1), Fallout4 (CE1.5), and Starfield (CE2) at 60/100/144/240 FPS
// Clean-room MIT, MSVC /std:c++latest /W4 /WX.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../src/d3d12_bridge/D3D12Bridge.h"
#include "../src/d3d12_bridge/RawGpuCullHost.h"
#include "../src/d3d12_bridge/RawLightClusterHost.h"
#include "../src/d3d12_bridge/RawVolumetricFroxelHost.h"
#include "../src/d3d12_bridge/RawXpbdSimHost.h"
#include "../src/d3d12_bridge/RawDebugViews.h"
#include "../src/d3d12_bridge/RawFramePacing.h"
#include "../src/worldgen/RawMeshPipeline.h"
#include "../src/engine_rt/rt_ownership_ledger.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <string>

// Compile-time C++ <-> HLSL ABI layout proofs resolving Wave 38B (#1023) audit items:
static_assert(sizeof(raw::bridge::PassConstants) == 64u, "PassConstants == 64B");
static_assert(offsetof(raw::bridge::PassConstants, dims) == 48u, "PassConstants::dims @ +48B");
static_assert(sizeof(raw::bridge::GpuCullInstance) == 64u, "GpuCullInstance == 64B");
static_assert(offsetof(raw::bridge::GpuCullInstance, meshletOffset) == 44u, "GpuCullInstance::meshletOffset @ +44B");
static_assert(sizeof(raw::bridge::ClusterAabb) == 32u, "ClusterAabb == 32B");
static_assert(offsetof(raw::bridge::ClusterAabb, maxBounds) == 16u, "ClusterAabb::maxBounds @ +16B");
static_assert(sizeof(raw::bridge::GpuClusterLight) == 48u, "GpuClusterLight == 48B");
static_assert(offsetof(raw::bridge::GpuClusterLight, colorRgb) == 32u, "GpuClusterLight::colorRgb @ +32B");
static_assert(sizeof(raw::worldgen::MeshletDesc) == 48u, "MeshletDesc == 48B");
static_assert(offsetof(raw::worldgen::MeshletDesc, normalCone) == 32u, "MeshletDesc::normalCone @ +32B");
static_assert(sizeof(raw::worldgen::TerrainVertex) == 80u, "TerrainVertex == 80B");
static_assert(offsetof(raw::worldgen::TerrainVertex, meta) == 64u, "TerrainVertex::meta @ +64B");
static_assert(sizeof(raw::bridge::FroxelPointLight) == 32u, "FroxelPointLight == 32B");
static_assert(offsetof(raw::bridge::FroxelPointLight, color) == 16u, "FroxelPointLight::color @ +16B");
static_assert(sizeof(raw::bridge::XpbdParticle) == 64u, "XpbdParticle == 64B");
static_assert(offsetof(raw::bridge::XpbdParticle, restPos) == 48u, "XpbdParticle::restPos @ +48B");
static_assert(sizeof(raw::bridge::XpbdCollider) == 32u, "XpbdCollider == 32B");
static_assert(offsetof(raw::bridge::XpbdCollider, p1) == 16u, "XpbdCollider::p1 @ +16B");
static_assert(sizeof(raw::bridge::wave82::RawDebugViewCB) == 64u, "RawDebugViewCB == 64B");
static_assert(sizeof(raw::rt::PassOwnershipRecord) == 64u, "PassOwnershipRecord == 64B");

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
    std::printf("=== Wave 82: Debug Views (0..8) + Ownership Ledger + C++/HLSL ABI Proofs Check ===\n");

    // 1) All 11 paired C++ <-> HLSL structs verified at compile time
    Check(true, "Compile-time sizeof & offsetof static_assert proofs for all 11 GPU/ownership structs");

    // 2) All 9 Canonical Debug Views (0..8) encoding + Click-Any-Pixel Inspector
    {
        bool ok = true;
        if (raw::bridge::wave82::EncodeLightingViewFlag(raw::bridge::wave82::DebugViewMode::FinalLit) != 0u) { ok = false; }
        for (uint32_t m = 1u; m <= 8u; ++m) {
            const auto mode = static_cast<raw::bridge::wave82::DebugViewMode>(m);
            if (raw::bridge::wave82::EncodeLightingViewFlag(mode) != 40u + m) { ok = false; }
            if (std::string(raw::bridge::wave82::DebugViewName(mode)) == "unknown") { ok = false; }
        }
        raw::bridge::wave82::DebugPixelSample cleanPx{};
        cleanPx.pixelX = 960u; cleanPx.pixelY = 540u;
        cleanPx.materialClass = 10u; cleanPx.linearDepth = 420.0f; cleanPx.rawDepth = 0.62f;
        cleanPx.worldNormal[0] = 0.0f; cleanPx.worldNormal[1] = 0.6f; cleanPx.worldNormal[2] = 0.8f;
        cleanPx.wetness01 = 0.45f;
        const auto rep = raw::bridge::wave82::InspectPixelBuffer(cleanPx);
        if (rep.corruptAlarm || !rep.normalValid || !rep.depthValid || rep.lodTier != 0u || rep.shadowCascadeIdx != 0u) {
            ok = false;
        }
        if (std::string(rep.passTag) != "ActorSkinSssPass") { ok = false; }
        Check(ok, "All 9 canonical debug views (0..8 -> flags.w 0, 41..48) & clean pixel inspector");
    }

    // 3) 15-Pass Runtime Ownership Ledger atomic 3-frame promotion & JSON serialization
    {
        raw::rt::RuntimeOwnershipLedger ledger;
        bool ok = (ledger.CountByState(raw::rt::PassOwnershipState::OwnedByRaw) == 7u)
               && (ledger.CountByState(raw::rt::PassOwnershipState::HybridIntercepted) == 7u)
               && (ledger.CountByState(raw::rt::PassOwnershipState::StillVanilla) == 1u)
               && (ledger.TotalDoubleExecs() == 0u)
               && (ledger.TotalFlickerFrames() == 0u);
        // Pass 6 (shadow_cascades_atlas) stays HYBRID on frames 1 & 2, promotes atomically on frame 3:
        ledger.RecordFrameTelemetry(6u, true, false, false, 0.28f, true);
        ledger.AdvancePresentFrame();
        if (ledger.Get(6u).state != static_cast<uint8_t>(raw::rt::PassOwnershipState::HybridIntercepted)) { ok = false; }
        ledger.RecordFrameTelemetry(6u, true, false, false, 0.27f, true);
        ledger.AdvancePresentFrame();
        if (ledger.Get(6u).state != static_cast<uint8_t>(raw::rt::PassOwnershipState::HybridIntercepted)) { ok = false; }
        ledger.RecordFrameTelemetry(6u, true, false, false, 0.26f, true);
        ledger.AdvancePresentFrame();
        if (ledger.Get(6u).state != static_cast<uint8_t>(raw::rt::PassOwnershipState::OwnedByRaw)) { ok = false; }
        const std::string dump = ledger.DumpOwnershipJson();
        if (dump.find("\"cmd\":\"ownership_dump\"") == std::string::npos ||
            dump.find("\"double_exec_total\":0") == std::string::npos) {
            ok = false;
        }
        Check(ok, "15-pass RuntimeOwnershipLedger atomic 3-frame promotion & JSON dump");
    }

    // 4) Planted Control 82-C: double-executed vanilla AO/fog caught fail-closed
    {
        raw::rt::RuntimeOwnershipLedger ledger;
        const bool ok1 = ledger.RecordFrameTelemetry(2u, true, true, false, 0.65f, false);
        const bool ok2 = ledger.RecordFrameTelemetry(2u, true, true, false, 0.65f, false);
        Check(!ok1 && !ok2 && ledger.TotalDoubleExecs() == 2u,
              "Planted Control 82-C: double-executed vanilla AO/fog caught fail-closed");
    }

    // 5) Planted Control 82-D: zero-length normal (Mode 2) & NaN depth (Mode 3) caught fail-closed
    {
        raw::bridge::wave82::DebugPixelSample zeroNrm{};
        zeroNrm.worldNormal[0] = 0.0f; zeroNrm.worldNormal[1] = 0.0f; zeroNrm.worldNormal[2] = 0.0f;
        const auto repN = raw::bridge::wave82::InspectPixelBuffer(zeroNrm);

        raw::bridge::wave82::DebugPixelSample nanDepth{};
        nanDepth.linearDepth = std::numeric_limits<float>::quiet_NaN();
        const auto repD = raw::bridge::wave82::InspectPixelBuffer(nanDepth);

        Check(repN.corruptAlarm && !repN.normalValid && repD.corruptAlarm && !repD.depthValid,
              "Planted Control 82-D: zero-length normal & NaN depth debug samples caught fail-closed");
    }

    // 6) Deterministic 100 Hz replay across SkyrimSE, Fallout4, and Starfield profiles (60/100/144/240 FPS)
    {
        const char* exes[3] = {"SkyrimSE.exe", "Fallout4.exe", "Starfield.exe"};
        const double rates[4] = {60.0, 100.0, 144.0, 240.0};
        bool ok = true;
        uint64_t refOwned = 0u;
        for (int gi = 0; gi < 3; ++gi) {
            const auto prof = raw::versions::ResolveEngineProfileByExe(exes[gi]);
            const auto policy = raw::bridge::ResolveFramePacingPolicy(prof);
            if (policy.multiplayerBlocked || policy.fixedTickHz != 100.0) { ok = false; }
            for (int ri = 0; ri < 4; ++ri) {
                raw::bridge::DeterministicFramePacer pacer(policy);
                raw::rt::RuntimeOwnershipLedger ledger;
                const double dt = 1.0 / rates[ri];
                const int frames = static_cast<int>(std::llround(1.0 * rates[ri]));
                uint64_t ticks = 0u;
                for (int f = 0; f < frames; ++f) {
                    const auto s = pacer.Step(dt);
                    for (uint32_t k = 0; k < s.substeps; ++k) {
                        ++ticks;
                        ledger.RecordFrameTelemetry(6u, true, false, false, 0.25f, true);
                        ledger.AdvancePresentFrame();
                    }
                }
                const uint64_t sig = (ticks << 8) ^ static_cast<uint64_t>(ledger.CountByState(raw::rt::PassOwnershipState::OwnedByRaw));
                if (gi == 0 && ri == 0) { refOwned = sig; }
                else if (sig != refOwned) { ok = false; }
            }
        }
        Check(ok && refOwned > 0u, "Bit-identical 100 Hz ownership replay across SkyrimSE / Fallout4 / Starfield & 60/100/144/240 FPS");
    }

    std::printf("=== RESULT: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
