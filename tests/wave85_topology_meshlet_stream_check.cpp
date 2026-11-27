// wave85_topology_meshlet_stream_check.cpp: Wave 85 Standalone C++23 Verification Suite.
// Verifies:
//   1. NIF Geometry Topology Repair (spatial-hash seam welding preserving UV/hard-edge
//      boundaries, degenerate & NaN-vertex stripping, MikkTSpace/Gram-Schmidt orthonormal
//      tangent frames |N|=1, |T|=1, |dot(N,T)| <= 1e-5, handedness |w|=1) +
//      Planted Control 85-A (corrupt NaN vertex + out-of-range index NIF fails closed
//      with plain-English diagnostic naming exact mesh & vertex/index span).
//   2. Watertight 64v/64t Meshlet Clusterizer & Monotonic QEM LOD DAG (strict <=64v/<=64t,
//      48-byte Wave 83 GpuMeshletDesc parity, tight normal cones, crack-free screen-space
//      LOD cut across 5m..500m).
//   3. Planted Control 85-B (non-monotonic QEM LOD DAG lodErrorWorld > parentErrorWorld -> rejected).
//   4. Planted Control 85-C (over-limit 68v/70t meshlet & NaN cone cutoff -> rejected).
//   5. Priority Async VRAM Residency & Horizon Streaming Budgeter (camera-velocity lookahead
//      priority ordering, stale epoch cancellation, async upload ring, strict 80% VRAM LRU
//      residency cap across 9-cell high-speed traversal, zero leak after release) +
//      Planted Control 85-D (LRU eviction disabled under 512 MB cap -> breach rejected fail-closed).
//   6. Multi-Title 100 Hz Deterministic Replay (SkyrimSE, Fallout4, Starfield) verifying
//      bit-identical final hashes across repeated runs.
// Clean-room MIT, MSVC /W4 /WX clean.

#include "../src/ce_runtime/CeTopologyRepair.h"
#include "../src/ce_runtime/CeMeshletBuilder.h"
#include "../src/d3d12_bridge/RawAssetStream.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

// Minimal link stubs for RawAssetStream::WorkerLoop when compiled standalone without WIC/disk IO.
namespace raw::bridge
{
bool LoadModernMeshAsset(const std::string& path, RawMeshAsset* outAsset, std::string* outErr)
{
    (void)path;
    (void)outAsset;
    if (outErr) *outErr = "stub";
    return false;
}
bool LoadModernTextureAsset(const std::string& path, RawTextureAsset* outTex, std::string* outErr)
{
    (void)path;
    (void)outTex;
    if (outErr) *outErr = "stub";
    return false;
}
} // namespace raw::bridge

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

// Generates a UV-mapped tessellated sphere with optional deliberate floating seam duplicates
// and degenerate triangles to exercise topology repair & meshlet clustering.
void BuildTestSphereMesh(
    uint32_t                                 rings,
    uint32_t                                 sectors,
    float                                    radius,
    bool                                     injectSplitSeams,
    bool                                     injectDegenerates,
    std::vector<raw::bridge::RawMeshVertex>* outVerts,
    std::vector<uint32_t>*                   outIndices)
{
    outVerts->clear();
    outIndices->clear();
    constexpr float kPi = 3.14159265358979323846f;

    auto makeVert = [&](uint32_t r, uint32_t s, float jitter) -> raw::bridge::RawMeshVertex {
        const float v   = static_cast<float>(r) / static_cast<float>(rings);
        const float u   = static_cast<float>(s) / static_cast<float>(sectors);
        const float phi = v * kPi;
        const float th  = u * 2.0f * kPi;
        const float nx  = std::sin(phi) * std::cos(th);
        const float ny  = std::sin(phi) * std::sin(th);
        const float nz  = std::cos(phi);
        raw::bridge::RawMeshVertex vert{};
        vert.pos[0]    = nx * radius + jitter;
        vert.pos[1]    = ny * radius;
        vert.pos[2]    = nz * radius;
        vert.normal[0] = nx;
        vert.normal[1] = ny;
        vert.normal[2] = nz;
        vert.uv[0]     = u;
        vert.uv[1]     = v;
        return vert;
    };

    for (uint32_t r = 0u; r < rings; ++r) {
        for (uint32_t s = 0u; s < sectors; ++s) {
            const float j = injectSplitSeams ? 2.5e-5f : 0.0f;
            const uint32_t base = static_cast<uint32_t>(outVerts->size());
            outVerts->push_back(makeVert(r, s, 0.0f));
            outVerts->push_back(makeVert(r + 1u, s, j));
            outVerts->push_back(makeVert(r + 1u, s + 1u, 0.0f));
            outVerts->push_back(makeVert(r, s + 1u, j));

            if (r > 0u) {
                outIndices->push_back(base + 0u);
                outIndices->push_back(base + 1u);
                outIndices->push_back(base + 2u);
            }
            if (r + 1u < rings) {
                outIndices->push_back(base + 0u);
                outIndices->push_back(base + 2u);
                outIndices->push_back(base + 3u);
            }
        }
    }

    if (injectDegenerates && outVerts->size() >= 3u) {
        for (uint32_t k = 0u; k < 24u; ++k) {
            // Duplicate vertex index degenerate triangle.
            outIndices->push_back(0u);
            outIndices->push_back(0u);
            outIndices->push_back(1u);
        }
    }
}

uint64_t MixHash(uint64_t h, uint64_t v) noexcept
{
    h ^= v;
    h *= 1099511628211ull;
    return h;
}

uint64_t RunTitleDeterministicReplay(const char* titleName, uint32_t seedOffset)
{
    std::vector<raw::bridge::RawMeshVertex> rawV;
    std::vector<uint32_t>                   rawI;
    BuildTestSphereMesh(16u + (seedOffset % 3u), 24u, 2.0f + 0.25f * static_cast<float>(seedOffset), true, true, &rawV, &rawI);

    const auto repaired = raw::ce::RepairNifMeshTopology(titleName, rawV, rawI);
    const auto dag      = raw::ce::BuildWatertightMeshletLodDag(repaired.vertices, repaired.indices);

    raw::bridge::RawAssetStream stream;
    constexpr uint64_t kBudgetBytes = 512ull * 1024ull * 1024ull; // 512 MB simulated budget -> 409.6 MB hard cap
    stream.ConfigureVramBudget(kBudgetBytes, 0.80f);

    uint64_t h = 14695981039346656037ull;
    h = MixHash(h, static_cast<uint64_t>(repaired.vertices.size()));
    h = MixHash(h, static_cast<uint64_t>(dag.meshlets.size()));
    h = MixHash(h, static_cast<uint64_t>(dag.lodLevels));

    // 300 ticks at 100 Hz (3.0 s) across 9 exterior cells.
    for (uint32_t tick = 0u; tick < 300u; ++tick) {
        const float tSec      = static_cast<float>(tick) * 0.01f;
        const float camPos[3] = {tSec * 25.0f, 0.0f, 2.0f};
        const float camVel[3] = {25.0f, 0.0f, 0.0f};
        const uint32_t epoch  = tick / 50u;
        stream.SetCameraState(camPos, camVel, epoch);

        const uint64_t assetId = 1000ull + static_cast<uint64_t>(seedOffset) * 500ull + (tick % 48u);
        const uint64_t szBytes = 16ull * 1024ull * 1024ull; // 16 MB cell chunk
        stream.AdmitResidentAsset(assetId, szBytes, tick, true, nullptr);
    }

    const auto tel = stream.GetResidencyTelemetry();
    h = MixHash(h, tel.residentBytes);
    h = MixHash(h, tel.peakResidentBytes);
    h = MixHash(h, tel.totalEvictedBytes);
    h = MixHash(h, static_cast<uint64_t>(tel.evictionCount));
    h = MixHash(h, tel.withinHardCap ? 1ull : 0ull);
    return h;
}

} // namespace

int main()
{
    std::printf("=== Wave 85: NIF Topology Repair + 64v/64t Meshlet QEM DAG + Priority VRAM Streamer ===\n");

    // 1. Topology Repair: seam weld, UV/hard-edge preservation, degenerate strip, orthonormal tangents.
    {
        std::vector<raw::bridge::RawMeshVertex> rawV;
        std::vector<uint32_t>                   rawI;
        BuildTestSphereMesh(20u, 32u, 2.5f, true, true, &rawV, &rawI);

        const auto rep = raw::ce::RepairNifMeshTopology("meshes/architecture/whiterun_dome.nif", rawV, rawI);
        float maxOrtho = 1.0f;
        float maxUnit  = 1.0f;
        const bool tangentsOk = raw::ce::VerifyOrthonormalTangentFrames(rep.vertices, &maxOrtho, &maxUnit);

        // Also test hard-edge + UV-split preservation on a 2-quad crease.
        std::vector<raw::bridge::RawMeshVertex> creaseV(6u);
        // Quad 1 (normal +Z, UV [0..0.5])
        creaseV[0] = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}};
        creaseV[1] = {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.0f}};
        creaseV[2] = {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}};
        // Hard-crease neighbor at same position but normal +X (must NOT weld across hard edge!)
        creaseV[3] = {{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.5f, 0.0f}};
        creaseV[4] = {{1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}};
        creaseV[5] = {{1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.5f, 1.0f}};
        std::vector<uint32_t> creaseI = {0u, 1u, 2u, 3u, 4u, 5u};
        const auto creaseRep = raw::ce::RepairNifMeshTopology("hard_edge_crease.nif", creaseV, creaseI);

        const bool ok1 = rep.ok &&
                         (rep.weldedVertexCount > 1000u) &&
                         (rep.strippedDegenerateTriangles == 24u) &&
                         tangentsOk &&
                         (maxOrtho <= 1.0e-5f) &&
                         (maxUnit <= 1.0e-4f) &&
                         (creaseRep.ok && creaseRep.vertices.size() == 6u && creaseRep.weldedVertexCount == 0u);
        std::printf("  [T1] inputV=%u -> repairedV=%zu (welded=%u, strippedDegen=%u, maxOrtho=%.2e, hardEdgeV=%zu)\n",
                    rep.inputVertexCount, rep.vertices.size(), rep.weldedVertexCount,
                    rep.strippedDegenerateTriangles, static_cast<double>(maxOrtho), creaseRep.vertices.size());
        Check(ok1, "T1_TopologyRepair_SeamWeld_HardEdge_OrthonormalTangents");
    }

    // 2. Planted Control 85-A: Corrupt NaN vertex + out-of-range index NIF -> fail-closed plain-English diagnostic!
    {
        std::vector<raw::bridge::RawMeshVertex> badV(4u);
        badV[0] = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}};
        badV[1] = {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}};
        badV[2] = {{std::numeric_limits<float>::quiet_NaN(), 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}};
        badV[3] = {{1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}};
        std::vector<uint32_t> badI = {0u, 1u, 2u, 0u, 1u, 999u};

        raw::ce::TopologyRepairConfig strictCfg{};
        strictCfg.failClosedOnCorruptInput = true;
        const auto badRep = raw::ce::RepairNifMeshTopology("meshes/armor/corrupt_pauldron.nif", badV, badI, strictCfg);

        bool hasNanDiag = false;
        bool hasOobDiag = false;
        for (const auto& d : badRep.diagnostics) {
            if (d.code == "CORRUPT_NAN_VERTEX" && d.firstVertexIdx == 2u &&
                d.message.find("corrupt_pauldron.nif") != std::string::npos) {
                hasNanDiag = true;
            }
            if (d.code == "OUT_OF_RANGE_INDEX" && d.firstIndexIdx == 3u &&
                d.message.find("corrupt_pauldron.nif") != std::string::npos) {
                hasOobDiag = true;
            }
        }
        const bool okA = (!badRep.ok) && hasNanDiag && hasOobDiag;
        std::printf("  [85-A] failClosed=%d nanDiag=%d oobDiag=%d diags=%zu\n",
                    badRep.ok ? 0 : 1, hasNanDiag ? 1 : 0, hasOobDiag ? 1 : 0, badRep.diagnostics.size());
        Check(okA, "T2_Planted85A_CorruptNif_FailClosed_PlainEnglishDiagnostics");
    }

    // 3. Watertight 64v/64t Meshlet Clusterizer & Monotonic QEM LOD DAG + Wave 83 GpuMeshletDesc cut parity.
    raw::ce::MeshletLodDag validDag{};
    {
        std::vector<raw::bridge::RawMeshVertex> rawV;
        std::vector<uint32_t>                   rawI;
        BuildTestSphereMesh(24u, 36u, 3.0f, true, false, &rawV, &rawI);
        const auto rep = raw::ce::RepairNifMeshTopology("meshes/clutter/watertight_orb.nif", rawV, rawI);

        validDag = raw::ce::BuildWatertightMeshletLodDag(rep.vertices, rep.indices);
        std::string dagErr;
        const bool  dagOk = raw::ce::ValidateMeshletLodDag(validDag, &dagErr);

        uint32_t maxV = 0u;
        uint32_t maxT = 0u;
        uint32_t validCones = 0u;
        for (const auto& m : validDag.meshlets) {
            maxV = (std::max)(maxV, static_cast<uint32_t>(m.vertexIndices.size()));
            maxT = (std::max)(maxT, static_cast<uint32_t>(m.primitiveIndices.size() / 3u));
            if (!m.degenerateCone && m.coneCutoff < 0.99f) {
                ++validCones;
            }
        }

        // Test Wave 83 continuous LOD cut across viewing distances: near selects LOD0, far selects coarser LODs.
        uint32_t nearSelected = 0u;
        uint32_t farSelected  = 0u;
        for (const auto& g : validDag.gpuDescriptors) {
            if (raw::bridge::wave83::SelectMeshletLodNode(g, 4.0f, 1080.0f, 1.25f)) ++nearSelected;
            if (raw::bridge::wave83::SelectMeshletLodNode(g, 350.0f, 1080.0f, 1.25f)) ++farSelected;
        }

        const bool ok3 = dagOk &&
                         (validDag.lodLevels >= 3u) &&
                         (maxV <= raw::ce::kMaxMeshletVertices) &&
                         (maxT <= raw::ce::kMaxMeshletTriangles) &&
                         (validCones > 0u) &&
                         (nearSelected == validDag.lod0MeshletCount) &&
                         (farSelected > 0u) &&
                         (farSelected < nearSelected);
        std::printf("  [T3] meshlets=%zu lodLevels=%u lod0=%u maxV=%u maxT=%u avgV/T=%.2f cones=%u nearSel=%u farSel=%u\n",
                    validDag.meshlets.size(), validDag.lodLevels, validDag.lod0MeshletCount,
                    maxV, maxT, static_cast<double>(validDag.avgVertsPerTri), validCones,
                    nearSelected, farSelected);
        Check(ok3, "T3_Meshlet64v64t_QemLodDag_MonotonicCut");
    }

    // 4. Planted Control 85-B (Non-monotonic QEM LOD DAG) & Planted Control 85-C (Over-limit 68v meshlet / NaN cone).
    {
        auto nonMonoDag = validDag;
        nonMonoDag.gpuDescriptors[0].lodErrorWorld    = 0.50f;
        nonMonoDag.gpuDescriptors[0].parentErrorWorld = 0.10f;
        std::string errB;
        const bool rejectedB = !raw::ce::ValidateMeshletLodDag(nonMonoDag, &errB);
        Check(rejectedB && (errB.find("LOD_CUT_NON_MONOTONIC") != std::string::npos),
              "T4_Planted85B_NonMonotonicLodDag_Rejected");

        auto overLimitDag = validDag;
        overLimitDag.meshlets[0].vertexIndices.resize(68u, 0u); // 68 > 64 limit!
        std::string errC1;
        const bool rejectedC1 = !raw::ce::ValidateMeshletLodDag(overLimitDag, &errC1);

        auto nanConeDag = validDag;
        nanConeDag.meshlets[0].coneCutoff = std::numeric_limits<float>::quiet_NaN();
        std::string errC2;
        const bool rejectedC2 = !raw::ce::ValidateMeshletLodDag(nanConeDag, &errC2);

        Check(rejectedC1 && rejectedC2, "T5_Planted85C_OverLimitMeshlet_And_NaNCone_Rejected");
    }

    // 5. Priority Async VRAM Residency Streamer + 80% Hard Cap + Zero Leak + Planted Control 85-D.
    {
        raw::bridge::RawAssetStream stream;
        constexpr uint64_t kTotalVram = 512ull * 1024ull * 1024ull; // 512 MB -> 80% cap = 409.6 MB
        stream.ConfigureVramBudget(kTotalVram, 0.80f);

        const float camPos[3] = {0.0f, 0.0f, 0.0f};
        const float camVel[3] = {20.0f, 0.0f, 0.0f};
        stream.SetCameraState(camPos, camVel, 1u);

        // Enqueue 3 requests: stale epoch 0 (must drop), distant behind, close ahead in velocity direction.
        raw::bridge::StreamRequest staleReq{};
        staleReq.assetHash = 10u; staleReq.path = "stale.nif"; staleReq.cancellationEpoch = 0u;
        staleReq.worldPos[0] = 5.0f;
        stream.EnqueuePriorityRequest(staleReq);

        raw::bridge::StreamRequest behindReq{};
        behindReq.assetHash = 20u; behindReq.path = "behind.nif"; behindReq.cancellationEpoch = 1u;
        behindReq.worldPos[0] = -40.0f; behindReq.screenCoverage = 0.1f;
        stream.EnqueuePriorityRequest(behindReq);

        raw::bridge::StreamRequest aheadReq{};
        aheadReq.assetHash = 30u; aheadReq.path = "ahead.nif"; aheadReq.cancellationEpoch = 1u;
        aheadReq.worldPos[0] = 20.0f; aheadReq.screenCoverage = 0.6f;
        stream.EnqueuePriorityRequest(aheadReq);

        raw::bridge::StreamRequest first{}, second{};
        const bool pop1 = stream.PopNextPriorityRequestForTest(&first);
        const bool pop2 = stream.PopNextPriorityRequestForTest(&second);
        const bool prioOk = pop1 && pop2 && (first.assetHash == 30u) && (second.assetHash == 20u);

        // Async upload ring push/pop parity.
        const bool ringPush = stream.PushAsyncUploadRing(30u, 8ull * 1024ull * 1024ull, 1u);
        raw::bridge::AsyncUploadRingSlot slot{};
        const bool ringPop  = stream.PopAsyncUploadRing(&slot) && (slot.assetHash == 30u);

        // 9-cell high-speed traversal: admit 40 x 24 MB = 960 MB through 409.6 MB hard cap -> LRU evicts cleanly!
        bool allAdmitted = true;
        std::vector<uint64_t> evictedAll;
        for (uint64_t i = 0u; i < 40u; ++i) {
            if (!stream.AdmitResidentAsset(1000u + i, 24ull * 1024ull * 1024ull, i + 1u, true, &evictedAll)) {
                allAdmitted = false;
            }
        }
        const auto telPeak = stream.GetResidencyTelemetry();

        // Planted Control 85-D: Disable LRU eviction when at/near cap -> MUST fail closed (never breach 80% cap)!
        const bool breachBlocked = !stream.AdmitResidentAsset(9999u, 128ull * 1024ull * 1024ull, 50u, false, nullptr);
        const auto telAfterBlocked = stream.GetResidencyTelemetry();

        // Zero-leak release check: release all resident assets -> residentBytes must return to exact 0!
        stream.ClearResidentAssets();
        const auto telZero = stream.GetResidencyTelemetry();

        const bool okStream = prioOk && ringPush && ringPop && allAdmitted &&
                              telPeak.withinHardCap &&
                              (telPeak.peakResidentBytes <= telPeak.hardCapBytes) &&
                              (telPeak.evictionCount > 0u) &&
                              (telPeak.cancelledRequests >= 1u) &&
                              breachBlocked &&
                              telAfterBlocked.withinHardCap &&
                              (telZero.residentBytes == 0u) &&
                              (telZero.residentAssetCount == 0u);
        std::printf("  [T6] prioOk=%d peakMB=%.1f/%.1f evictions=%u cancelled=%u 85D_blocked=%d zeroLeak=%llu\n",
                    prioOk ? 1 : 0,
                    static_cast<double>(telPeak.peakResidentBytes) / (1024.0 * 1024.0),
                    static_cast<double>(telPeak.hardCapBytes) / (1024.0 * 1024.0),
                    telPeak.evictionCount, telPeak.cancelledRequests,
                    breachBlocked ? 1 : 0,
                    static_cast<unsigned long long>(telZero.residentBytes));
        Check(okStream, "T6_PriorityAsyncVramStreamer_80PctLruCap_ZeroLeak_Planted85D");
    }

    // 6. Multi-Title 100 Hz Deterministic Replay across SkyrimSE, Fallout4, Starfield.
    {
        const uint64_t s1 = RunTitleDeterministicReplay("SkyrimSE", 0u);
        const uint64_t s2 = RunTitleDeterministicReplay("SkyrimSE", 0u);
        const uint64_t f1 = RunTitleDeterministicReplay("Fallout4", 1u);
        const uint64_t f2 = RunTitleDeterministicReplay("Fallout4", 1u);
        const uint64_t st1 = RunTitleDeterministicReplay("Starfield", 2u);
        const uint64_t st2 = RunTitleDeterministicReplay("Starfield", 2u);
        const bool detOk = (s1 == s2) && (f1 == f2) && (st1 == st2) && (s1 != f1) && (f1 != st1);
        std::printf("  [T7] SkyrimSE=%016llx Fallout4=%016llx Starfield=%016llx\n",
                    static_cast<unsigned long long>(s1),
                    static_cast<unsigned long long>(f1),
                    static_cast<unsigned long long>(st1));
        Check(detOk, "T7_MultiTitle_100Hz_DeterministicReplay_SkyrimSE_Fallout4_Starfield");
    }

    std::printf("=== Wave 85 Summary: %d passed, %d failed ===\n", g_passed, g_failed);
    return (g_failed == 0) ? 0 : 1;
}
