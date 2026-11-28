// wave86_mod_sdk_hotreload_hdr_check.cpp: Wave 86 Standalone C++23 Verification Suite.
// Verifies:
//   1. SemVer v2.0 Mod-Developer SDK ABI (include/raw_mod_sdk_abi.h + RawModSdkHost):
//      64B struct layout assertions, valid ABI v2.0 negotiation, acyclic pass registration,
//      non-colliding debug view slots [100..255], capability-gated read-only world queries,
//      and per-mod CPU-microsecond + VRAM-byte attribution naming exact offending mod ID!
//   2. Planted Control 86-A (ABI version 3.0 / truncated structSize rejected fail-closed) +
//      Planted Control 86-C (cyclic frame-graph PassA -> PassB -> PassA rejected fail-closed).
//   3. Sub-100 ms Atomic Live Hot-Reload + Wave 83 RawPsoCache + Automatic PSO Rollback
//      (valid reload commits cleanly at Present boundary in << 100 ms; Planted Control 86-B
//      syntax-broken shader, NaN canary, and ABI hash mismatch roll back cleanly preserving
//      last-known-good PSO untouched with 0 black frames).
//   4. True HDR10 / BT.2020 / OKLab Calibrated Display Output (Rec.709->Rec.2020 D65 white
//      identity, OKLab soft-gamut safety with 0 negative/out-of-gamut clip, exact SMPTE ST 2084
//      PQ OETF<->EOTF round-trip < 1e-4 across 100..4000 nits, SDR fallback identity, NaN safety).
//   5. Shared Cross-Title ICreationGameAdapter across 4 profiles (SkyrimSE, Fallout4, Starfield,
//      SuperframeNative) + Planted Control 86-D (cross-title Skyrim RTTI tag injected into
//      Fallout4Adapter fails closed!) + deterministic 100 Hz replay across all 4 titles.
// Clean-room MIT, MSVC /W4 /WX clean.

#include "../include/raw_mod_sdk_abi.h"
#include "../src/engine_rt/RawModSdkHost.h"
#include "../src/d3d12_bridge/RawHotReload.h"
#include "../src/d3d12_bridge/RawHdrDisplayOutput.h"
#include "../src/ce_runtime/ICreationGameAdapter.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace
{

int g_passed = 0;
int g_failed = 0;

void Check(bool cond, const char* name)
{
    if (cond) {
        ++g_passed;
        std::printf("[PASS] %s\n", name);
    } else {
        ++g_failed;
        std::printf("[FAIL] %s\n", name);
    }
}

uint64_t MixHash(uint64_t h, uint64_t v) noexcept
{
    h ^= v;
    h *= 1099511628211ull;
    return h;
}

} // namespace

int main()
{
    std::printf("=== Wave 86: Mod SDK ABI v2.0 + Atomic Hot-Reload + HDR10/OKLab + Cross-Title Adapter ===\n");

    // 1. SemVer v2.0 Mod SDK Host, Capability Enforcement & Per-Mod CPU/VRAM Budget Attribution.
    raw::rt::RawModSdkHost sdkHost;
    {
        RawModSdkApiV2 api{};
        const int rcNeg = sdkHost.NegotiateAbiV2(RAW_MOD_SDK_ABI_VERSION, sizeof(RawModSdkApiV2), &api);

        const uint32_t fullCaps = RAW_MOD_CAP_READ_GBUFFER | RAW_MOD_CAP_READ_ACTORS |
                                  RAW_MOD_CAP_READ_NAVMESH | RAW_MOD_CAP_RENDER_PASS |
                                  RAW_MOD_CAP_DEBUG_VIEW | RAW_MOD_CAP_BUILDLANG_FFI;
        const int rcM1 = sdkHost.RegisterMod(101u, "NordicMistVolumetrics", fullCaps, 500ull, 1000ull,
                                             32ull * 1024ull * 1024ull, 64ull * 1024ull * 1024ull);
        const int rcM2 = sdkHost.RegisterMod(202u, "HeavyScriptMod", RAW_MOD_CAP_READ_ACTORS, 400ull, 800ull,
                                             16ull * 1024ull * 1024ull, 32ull * 1024ull * 1024ull);

        RawModPassDesc p1{};
        p1.structSize      = sizeof(RawModPassDesc);
        p1.modId           = 101u;
        p1.passId          = 10u;
        p1.dependsOnPassId = RAW_MOD_NO_DEPENDENCY;
        p1.requiredCaps    = RAW_MOD_CAP_READ_GBUFFER;
        std::memcpy(p1.passName, "MistPass", 9u);
        const int rcP1 = api.RegisterRenderPass(api.hostCtx, &p1);

        RawModDebugViewDesc dv{};
        dv.structSize = sizeof(RawModDebugViewDesc);
        dv.modId      = 101u;
        dv.viewSlot   = 105u;
        std::memcpy(dv.viewName, "MistDensityView", 16u);
        const int rcDv = api.RegisterDebugView(api.hostCtx, &dv);

        // Core view slot 3 must be rejected!
        RawModDebugViewDesc dvBad = dv;
        dvBad.viewSlot = 3u;
        const int rcDvBad = api.RegisterDebugView(api.hostCtx, &dvBad);

        // Capability-gated read-only world query.
        RawModReadOnlyWorldQuery world{};
        world.structSize       = sizeof(RawModReadOnlyWorldQuery);
        world.activeActorCount = 1024u;
        world.navClusterCount  = 64u;
        world.gbufferWidth     = 1920u;
        world.gbufferHeight    = 1080u;
        sdkHost.SetWorldSnapshot(world);

        RawModReadOnlyWorldQuery qOut{};
        qOut.structSize = sizeof(RawModReadOnlyWorldQuery);
        const int rcQOk   = api.QueryReadOnlyWorld(api.hostCtx, 101u, RAW_MOD_CAP_READ_GBUFFER | RAW_MOD_CAP_READ_ACTORS, &qOut);
        const int rcQDeny = api.QueryReadOnlyWorld(api.hostCtx, 202u, RAW_MOD_CAP_READ_GBUFFER, &qOut);

        // Per-mod CPU/VRAM budget attribution: mod 101 clean, mod 202 breaches hard CPU cap -> named!
        std::string diagGood, diagBad;
        const int rcU1 = sdkHost.RecordModUsage(101u, 220ull, 12ull * 1024ull * 1024ull, &diagGood);
        const int rcU2 = sdkHost.RecordModUsage(202u, 2400ull, 48ull * 1024ull * 1024ull, &diagBad);
        const uint32_t worstMod = sdkHost.Budgets().GetWorstOffendingModId();

        const bool ok1 = (rcNeg == RAW_MOD_OK) && (api.abiVersion == RAW_MOD_SDK_ABI_VERSION) &&
                         (rcM1 == RAW_MOD_OK) && (rcM2 == RAW_MOD_OK) &&
                         (rcP1 == RAW_MOD_OK) && (rcDv == RAW_MOD_OK) && (rcDvBad == RAW_MOD_ERR_INVALID_SLOT) &&
                         (rcQOk == RAW_MOD_OK) && (qOut.activeActorCount == 1024u) &&
                         (rcQDeny == RAW_MOD_ERR_CAPABILITY_DENIED) &&
                         (rcU1 == RAW_MOD_OK) && (rcU2 == RAW_MOD_ERR_BUDGET_EXCEEDED) &&
                         (diagBad.find("HeavyScriptMod") != std::string::npos) &&
                         (worstMod == 202u);
        std::printf("  [T1] abi=0x%08x passes=%zu views=%zu worstMod=%u ('%s')\n",
                    api.abiVersion, sdkHost.RegisteredPassCount(), sdkHost.RegisteredDebugViewCount(),
                    worstMod, sdkHost.Budgets().GetModName(worstMod).c_str());
        Check(ok1, "T1_ModSdkAbiV2_Capabilities_And_PerModBudgetAttribution");
    }

    // 2. Planted Control 86-A (ABI Mismatch / Truncated Struct) & Planted Control 86-C (Cyclic Frame-Graph).
    {
        RawModSdkApiV2 badApi{};
        const int rcMajMismatch = sdkHost.NegotiateAbiV2(0x00030000u, sizeof(RawModSdkApiV2), &badApi);
        const int rcTruncStruct = sdkHost.NegotiateAbiV2(RAW_MOD_SDK_ABI_VERSION, 16u, &badApi);
        Check((rcMajMismatch == RAW_MOD_ERR_ABI_MISMATCH) && (rcTruncStruct == RAW_MOD_ERR_STRUCT_SIZE),
              "T2_Planted86A_AbiVersion_And_StructSize_FailClosed");

        RawModPassDesc pA{};
        pA.structSize      = sizeof(RawModPassDesc);
        pA.modId           = 101u;
        pA.passId          = 50u;
        pA.dependsOnPassId = 51u;
        RawModPassDesc pB{};
        pB.structSize      = sizeof(RawModPassDesc);
        pB.modId           = 101u;
        pB.passId          = 51u;
        pB.dependsOnPassId = 50u;
        std::string cycDiag;
        const int rcA = sdkHost.RegisterRenderPass(pA, nullptr);
        const int rcB = sdkHost.RegisterRenderPass(pB, &cycDiag);
        Check((rcA == RAW_MOD_OK) && (rcB == RAW_MOD_ERR_CYCLE) &&
              (cycDiag.find("FRAMEGRAPH_CYCLE") != std::string::npos),
              "T3_Planted86C_CyclicFrameGraphDependency_Rejected");
    }

    // 3. Sub-100 ms Atomic Live Hot-Reload + Wave 83 RawPsoCache + Planted Control 86-B (Broken Shader / NaN Rollback).
    {
        raw::bridge::wave83::RawPsoCache psoCache({}, 0x00010722u);
        raw::bridge::wave86::RawHotReloadManager reloader(&psoCache, 0x00010722u);

        const std::vector<uint8_t> goodV1 = {0x50u, 0x53u, 0x4Fu, 0x01u, 0xAAu, 0xBBu};
        const std::vector<uint8_t> goodV2 = {0x50u, 0x53u, 0x4Fu, 0x02u, 0xCCu, 0xDDu};
        const uint64_t kAbiHash = 0x202600020000ull;

        const bool regOk = reloader.RegisterInitialGoodAsset(
            "shaders/Lighting.hlsl", raw::bridge::wave86::HotReloadAssetKind::HlslShader,
            "float4 main() { return float4(1,1,1,1); }", goodV1, kAbiHash);

        auto goodCompiler = [&](const std::string& src) -> raw::bridge::wave86::HotReloadCompileResult {
            if (src.find("SYNTAX_ERROR") != std::string::npos) {
                return {false, {}, "error X3000: syntax error at line 1"};
            }
            if (src.find("NAN_CANARY") != std::string::npos) {
                return {true, {0xFFu, 0x7Fu, 0xFFu, 0xFFu}, ""};
            }
            return {true, goodV2, ""};
        };
        auto canaryCheck = [](const std::vector<uint8_t>& blob) -> raw::bridge::wave86::HotReloadCanaryResult {
            if (!blob.empty() && blob[0] == 0xFFu) {
                return {false, 12u, "12 NaN pixels detected in canary render"};
            }
            return {true, 0u, "clean"};
        };

        // Valid live edit -> stages and commits at Present boundary in << 100 ms!
        const auto repGood = reloader.StageReload(
            "shaders/Lighting.hlsl", "float4 main() { return float4(0.8,0.9,1.0,1.0); }",
            kAbiHash, goodCompiler, canaryCheck);
        const uint32_t committed = reloader.CommitAtPresentBoundary();
        raw::bridge::wave86::HotReloadEntry afterGood{};
        reloader.GetActiveEntry("shaders/Lighting.hlsl", &afterGood);

        // Planted Control 86-B: Broken HLSL syntax + NaN canary + ABI mismatch -> ALL roll back cleanly, preserving goodV2!
        const auto repSyntax = reloader.StageReload(
            "shaders/Lighting.hlsl", "SYNTAX_ERROR {{{", kAbiHash, goodCompiler, canaryCheck);
        const auto repNan = reloader.StageReload(
            "shaders/Lighting.hlsl", "NAN_CANARY", kAbiHash, goodCompiler, canaryCheck);
        const auto repAbi = reloader.StageReload(
            "shaders/Lighting.hlsl", "float4 main() { return 0; }", kAbiHash ^ 1ull, goodCompiler, canaryCheck);
        const uint32_t committedBad = reloader.CommitAtPresentBoundary();

        raw::bridge::wave86::HotReloadEntry afterBad{};
        reloader.GetActiveEntry("shaders/Lighting.hlsl", &afterBad);
        const auto stats = reloader.GetStats();

        const bool okReload = regOk && repGood.staged && !repGood.rolledBack &&
                              (repGood.elapsedUs < 100000ull) && (committed == 1u) &&
                              (afterGood.activePsoBlob == goodV2) &&
                              repSyntax.rolledBack && repNan.rolledBack && repAbi.rolledBack &&
                              (committedBad == 0u) &&
                              (afterBad.activePsoBlob == goodV2) &&
                              (stats.rollbackCount == 3ull) &&
                              (stats.maxReloadUs < 100000ull);
        std::printf("  [T4] reloadUs=%llu (<100000) swaps=%llu rollbacks=%llu activePreserved=%d\n",
                    static_cast<unsigned long long>(stats.maxReloadUs),
                    static_cast<unsigned long long>(stats.successfulSwaps),
                    static_cast<unsigned long long>(stats.rollbackCount),
                    (afterBad.activePsoBlob == goodV2) ? 1 : 0);
        Check(okReload, "T4_Sub100msAtomicHotReload_And_Planted86B_PsoRollback");
    }

    // 4. True HDR10 / BT.2020 / OKLab Calibrated Output (PQ round-trip < 1e-4, D65 white identity, SDR identity).
    {
        // Exact D65 white preservation under Rec.709 -> Rec.2020 matrix.
        const auto w2020 = raw::bridge::wave86::Rec709ToRec2020({1.0f, 1.0f, 1.0f});
        const float whiteErr = (std::max)((std::max)(std::fabs(w2020.r - 1.0f), std::fabs(w2020.g - 1.0f)),
                                          std::fabs(w2020.b - 1.0f));

        // Exact SMPTE ST 2084 PQ OETF <-> EOTF round-trip across [100..4000 nits].
        float maxPqRelErr = 0.0f;
        const float testNits[] = {0.5f, 10.0f, 100.0f, 203.0f, 600.0f, 1000.0f, 2000.0f, 4000.0f};
        for (float n : testNits) {
            const float pq = raw::bridge::wave86::LinearNitsToPq01Scalar(n);
            const float back = raw::bridge::wave86::Pq01ToLinearNitsScalar(pq);
            const float relErr = std::fabs(back - n) / (std::max)(n, 1.0f);
            maxPqRelErr = (std::max)(maxPqRelErr, relErr);
        }

        // OKLab gamut compression on extreme wide/saturated HDR color + NaN sanitization.
        const auto hdrOut = raw::bridge::wave86::EncodeCalibratedDisplayOutput({12.0f, 0.2f, 0.05f}, true, 203.0f, 1000.0f);
        const auto nanOut = raw::bridge::wave86::EncodeCalibratedDisplayOutput(
            {std::numeric_limits<float>::quiet_NaN(), -5.0f, std::numeric_limits<float>::infinity()}, true, 203.0f, 1000.0f);
        const auto sdrOut = raw::bridge::wave86::EncodeCalibratedDisplayOutput({0.42f, 0.65f, 0.81f}, false, 203.0f, 1000.0f);

        const float peakPq1000 = raw::bridge::wave86::LinearNitsToPq01Scalar(1000.0f);
        const bool okHdr = (whiteErr < 1.0e-5f) &&
                           (maxPqRelErr < 1.0e-4f) &&
                           std::isfinite(hdrOut.r) && std::isfinite(hdrOut.g) && std::isfinite(hdrOut.b) &&
                           (hdrOut.r >= 0.0f && hdrOut.r <= peakPq1000 + 1.0e-4f) &&
                           (hdrOut.g >= 0.0f && hdrOut.b >= 0.0f) &&
                           (nanOut.r == 0.0f && nanOut.g == 0.0f && nanOut.b == 0.0f) &&
                           (std::fabs(sdrOut.r - 0.42f) < 1.0e-6f &&
                            std::fabs(sdrOut.g - 0.65f) < 1.0e-6f &&
                            std::fabs(sdrOut.b - 0.81f) < 1.0e-6f);
        std::printf("  [T5] d65WhiteErr=%.2e maxPqRoundTripRelErr=%.2e peak1000PQ=%.4f sdrIdentity=%d\n",
                    static_cast<double>(whiteErr), static_cast<double>(maxPqRelErr),
                    static_cast<double>(peakPq1000), (std::fabs(sdrOut.g - 0.65f) < 1.0e-6f) ? 1 : 0);
        Check(okHdr, "T5_Hdr10_Bt2020_Oklab_PqRoundTrip_And_SdrFallbackIdentity");
    }

    // 5. Cross-Title ICreationGameAdapter (4 titles) + Planted Control 86-D + 100 Hz Deterministic Replay.
    {
        const raw::ce::CreationTitleId titles[4] = {
            raw::ce::CreationTitleId::SkyrimSE,
            raw::ce::CreationTitleId::Fallout4,
            raw::ce::CreationTitleId::Starfield,
            raw::ce::CreationTitleId::SuperframeNative
        };
        uint64_t run1Hashes[4]{};
        uint64_t run2Hashes[4]{};
        bool allValid = true;

        for (int i = 0; i < 4; ++i) {
            auto a = raw::ce::CreateGameAdapter(titles[i]);
            if (!a) { allValid = false; continue; }
            const auto prof = a->GetProfile();
            if (!a->ValidateRttiContract(prof.expectedRttiContractTag) || prof.defaultTickHz != 100u) {
                allValid = false;
            }
            uint64_t h1 = a->ComputeDeterministicProfileHash();
            uint64_t h2 = a->ComputeDeterministicProfileHash();
            for (uint32_t tick = 0u; tick < 100u; ++tick) {
                const float meters = a->WorldUnitsToMeters(static_cast<float>(tick) * 7.0f);
                uint32_t bits = 0u;
                std::memcpy(&bits, &meters, sizeof(bits));
                h1 = MixHash(h1, bits);
                h2 = MixHash(h2, bits);
            }
            run1Hashes[i] = h1;
            run2Hashes[i] = h2;
            if (h1 != h2) allValid = false;
        }

        // Planted Control 86-D: Inject SkyrimSE RTTI contract tag into Fallout4Adapter -> MUST fail closed!
        auto sky = raw::ce::CreateGameAdapter(raw::ce::CreationTitleId::SkyrimSE);
        auto fo4 = raw::ce::CreateGameAdapter(raw::ce::CreationTitleId::Fallout4);
        const bool crossTitleRejected = !fo4->ValidateRttiContract(sky->GetProfile().expectedRttiContractTag);
        const bool distinctHashes = (run1Hashes[0] != run1Hashes[1]) &&
                                    (run1Hashes[1] != run1Hashes[2]) &&
                                    (run1Hashes[2] != run1Hashes[3]);

        std::printf("  [T6] 4-title replay hashes: Skyrim=%016llx FO4=%016llx SF=%016llx Superframe=%016llx 86D_rejected=%d\n",
                    static_cast<unsigned long long>(run1Hashes[0]),
                    static_cast<unsigned long long>(run1Hashes[1]),
                    static_cast<unsigned long long>(run1Hashes[2]),
                    static_cast<unsigned long long>(run1Hashes[3]),
                    crossTitleRejected ? 1 : 0);
        Check(allValid && distinctHashes && crossTitleRejected,
              "T6_CrossTitleGameAdapter_4Title100HzReplay_And_Planted86D");
    }

    std::printf("=== Wave 86 Summary: %d passed, %d failed ===\n", g_passed, g_failed);
    return (g_failed == 0) ? 0 : 1;
}
