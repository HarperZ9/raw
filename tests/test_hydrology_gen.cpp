// test_hydrology_gen.cpp: Real-Time Hydraulic & Thermal Erosion + D8 Watershed Hydrology Tests.
// Linkable into ce_runtime_check via raw::worldgen::RunHydrologyGenTests(checkFn),
// or buildable standalone with -DRAW_HYDROLOGY_STANDALONE_MAIN.
// Original work, MIT.

#include "../src/pipeline/RawAssetValidator.h"
#include "../src/worldgen/RawHydrologyGen.h"
#include "../src/worldgen/RawMeshPipeline.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace raw::worldgen {
namespace {

std::vector<float> MakeAlpineRidgeHeightfield(uint32_t w, uint32_t h) {
    std::vector<float> grid(size_t(w) * h, 0.0f);
    for (uint32_t y = 0u; y < h; ++y) {
        const float v = float(y) / float(h - 1u);
        for (uint32_t x = 0u; x < w; ++x) {
            const float u = float(x) / float(w - 1u);
            // Tilted alpine ridge with a central converging V-valley draining toward y = 0
            const float valleyV = std::fabs(u - 0.5f) * 28.0f;
            const float slopeY  = v * 22.0f;
            const float micro   = 2.2f * std::sin(u * 15.0f) * std::cos(v * 11.0f);
            grid[size_t(y) * w + x] = 10.0f + valleyV + slopeY + micro;
        }
    }
    return grid;
}

std::vector<float> MakeSteppedEscarpmentHeightfield(uint32_t w, uint32_t h) {
    // Upper tilted plateau (y >= h/2) funnels water toward x = w/2, then plunges over a sheer
    // escarpment cliff at y = h/2 into a lower gently sloped basin (y < h/2).
    std::vector<float> grid(size_t(w) * h, 0.0f);
    const uint32_t cliffY = h / 2u;
    for (uint32_t y = 0u; y < h; ++y) {
        const float v = float(y) / float(h - 1u);
        for (uint32_t x = 0u; x < w; ++x) {
            const float u = float(x) / float(w - 1u);
            const float valleyV = std::fabs(u - 0.5f) * 14.0f;
            const float baseSlope = v * 12.0f;
            const float cliffStep = (y >= cliffY) ? 18.0f : 0.0f;
            grid[size_t(y) * w + x] = 5.0f + valleyV + baseSlope + cliffStep;
        }
    }
    return grid;
}

}  // namespace

int RunHydrologyGenTests(void (*checkFn)(bool, const char*)) {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) ++failures;
        if (checkFn) {
            checkFn(ok, name);
        } else {
            std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        }
    };

    // 1. Particle-based hydraulic droplet erosion: finiteness, active erosion/deposition, bounded sediment transfer
    {
        constexpr uint32_t W = 65u, H = 65u;
        std::vector<float> grid = MakeAlpineRidgeHeightfield(W, H);
        const std::vector<float> orig = grid;

        HydraulicErosionConfig hcfg{};
        hcfg.seed                   = 202643u;
        hcfg.dropletCount           = 1800u;
        hcfg.maxLifetime            = 64u;
        hcfg.cellSizeU              = 1.0f;
        hcfg.sedimentCapacityFactor = 2.4f;
        hcfg.erodeSpeed             = 0.22f;
        hcfg.erosionRadius          = 2;

        std::vector<float> erodeMap, depositMap;
        const HydraulicErosionStats st = ApplyHydraulicErosion(grid, W, H, hcfg, &erodeMap, &depositMap);

        float maxDelta = 0.0f;
        bool finiteGrid = st.allFinite;
        for (size_t i = 0; i < grid.size(); ++i) {
            if (!std::isfinite(grid[i])) finiteGrid = false;
            maxDelta = std::max(maxDelta, std::fabs(grid[i] - orig[i]));
        }

        // Droplets can never deposit more sediment than they eroded, and net grid volume change
        // equals (deposited - eroded) within floating-point accumulation precision!
        const double expectedNet = st.totalDepositedVolume - st.totalErodedVolume;
        const bool massBalanceOk =
            st.totalErodedVolume > 10.0 &&
            st.totalDepositedVolume > 0.5 &&
            st.totalDepositedVolume <= st.totalErodedVolume + 1.0e-2 &&
            std::fabs(st.netHeightChange - expectedNet) < 0.25 &&
            maxDelta > 0.05f && maxDelta < 25.0f;

        if (!checkFn) {
            std::printf("[Hydraulic] droplets=%u steps=%llu eroded=%.2f deposited=%.2f net=%.2f maxDelta=%.3f\n",
                        st.dropletsSimulated, static_cast<unsigned long long>(st.totalSteps),
                        st.totalErodedVolume, st.totalDepositedVolume, st.netHeightChange, double(maxDelta));
        }
        check(finiteGrid,    "hydrology_hydraulic_erosion_zero_nan_inf");
        check(massBalanceOk, "hydrology_hydraulic_erosion_bounded_sediment_transfer");
    }

    // 2. Thermal talus-angle erosion: strictly lowers max cliff slope while conserving total terrain mass
    {
        constexpr uint32_t W = 49u, H = 49u;
        std::vector<float> cliff = MakeSteppedEscarpmentHeightfield(W, H);

        ThermalErosionConfig tcfg{};
        tcfg.iterations    = 40u;
        tcfg.cellSizeU     = 1.0f;
        tcfg.talusAngleDeg = 36.0f; // tan(36 deg) = 0.7265
        tcfg.transferRate  = 0.42f;
        tcfg.use8Neighbors = true;

        const ThermalErosionStats st = ApplyThermalErosion(cliff, W, H, tcfg);

        if (!checkFn) {
            std::printf("[Thermal] iters=%u maxSlope: %.3f -> %.3f moved=%.2f net=%.6f\n",
                        st.iterationsRun, double(st.maxSlopeBefore), double(st.maxSlopeAfter),
                        st.totalMassMoved, st.netHeightChange);
        }
        check(st.allFinite, "hydrology_thermal_erosion_zero_nan_inf");
        check(st.maxSlopeBefore > 10.0f && st.maxSlopeAfter < st.maxSlopeBefore * 0.5f,
              "hydrology_thermal_erosion_strictly_reduces_cliff_slope");
        check(std::fabs(st.netHeightChange) < 1.0e-2 && st.totalMassMoved > 50.0,
              "hydrology_thermal_erosion_conserves_total_mass");
    }

    // 3. D8 watershed flow accumulation & continuous downhill river carving on synthetic alpine valley
    {
        constexpr uint32_t W = 65u, H = 65u;
        std::vector<float> valley = MakeAlpineRidgeHeightfield(W, H);
        const std::vector<float> preCarve = valley;

        WatershedConfig wcfg{};
        wcfg.cellSizeU          = 1.0f;
        wcfg.riverFlowThreshold = 20.0f;
        wcfg.riverCarveDepthU   = 1.6f;
        wcfg.carveRiverValleys  = true;
        wcfg.preserveBorderEdge = false;

        const WatershedResult ws = ComputeWatershedHydrology(valley, W, H, wcfg);

        // Verify strictly increasing flow along every D8 edge and continuous downhill river carving
        constexpr int kDx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
        constexpr int kDy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
        bool d8MonotoneOk = ws.ok && (ws.flowAccum.size() == size_t(W) * H);
        uint32_t carvedCells = 0u;
        for (uint32_t y = 0u; y < H && d8MonotoneOk; ++y) {
            for (uint32_t x = 0u; x < W; ++x) {
                const size_t c = size_t(y) * W + x;
                if (!std::isfinite(ws.flowAccum[c]) || ws.flowAccum[c] < 1.0f) {
                    d8MonotoneOk = false;
                    break;
                }
                if (valley[c] < preCarve[c] - 0.1f) ++carvedCells;
                const int8_t dir = ws.d8Dir[c];
                if (dir >= 0) {
                    const size_t nb = size_t(int(y) + kDy[dir]) * W + size_t(int(x) + kDx[dir]);
                    if (ws.flowAccum[nb] < ws.flowAccum[c] + 1.0f - 1.0e-3f) {
                        d8MonotoneOk = false;
                        break;
                    }
                    if (preCarve[nb] >= preCarve[c]) {
                        d8MonotoneOk = false;
                        break;
                    }
                }
            }
        }

        if (!checkFn) {
            std::printf("[Watershed] maxFlow=%.1f riverCells=%u carvedCells=%u\n",
                        double(ws.maxFlowAccum), ws.riverCellCount, carvedCells);
        }
        check(d8MonotoneOk, "hydrology_d8_watershed_strictly_downhill_monotonic_flow");
        check(ws.maxFlowAccum >= 100.0f && ws.riverCellCount >= 20u && carvedCells >= 20u,
              "hydrology_d8_watershed_carves_continuous_river_channel");
    }

    // 4. Waterfall detection on a stepped alpine escarpment
    {
        constexpr uint32_t W = 65u, H = 65u;
        std::vector<float> escarpment = MakeSteppedEscarpmentHeightfield(W, H);

        WatershedConfig wcfg{};
        wcfg.cellSizeU            = 1.0f;
        wcfg.riverFlowThreshold   = 18.0f;
        wcfg.waterfallMinSlopeTan = 0.65f;
        wcfg.waterfallMinFlow     = 20.0f;
        wcfg.carveRiverValleys    = true;

        const WatershedResult ws = ComputeWatershedHydrology(escarpment, W, H, wcfg);
        bool wfAtCliffLip = false;
        if (!ws.waterfalls.empty()) {
            const WaterfallPoint& topWf = ws.waterfalls.front();
            // Cliff step sits at y == H / 2 == 32, valley channel sits near x == W / 2 == 32
            wfAtCliffLip =
                (topWf.gridY == H / 2u) &&
                (topWf.gridX >= W / 2u - 3u && topWf.gridX <= W / 2u + 3u) &&
                (topWf.dropHeightU >= 10.0f) &&
                (topWf.slopeTan >= wcfg.waterfallMinSlopeTan) &&
                (topWf.flowAccum >= wcfg.waterfallMinFlow);
        }

        if (!checkFn && !ws.waterfalls.empty()) {
            const auto& wf = ws.waterfalls.front();
            std::printf("[Waterfall] detected=%zu top=(%u,%u) drop=%.2f slopeTan=%.2f flow=%.1f\n",
                        ws.waterfalls.size(), wf.gridX, wf.gridY,
                        double(wf.dropHeightU), double(wf.slopeTan), double(wf.flowAccum));
        }
        check(!ws.waterfalls.empty() && wfAtCliffLip, "hydrology_waterfall_detected_at_stepped_escarpment");
    }

    // 5. Bit-exact deterministic seed reproducibility across Hydraulic + Thermal + D8 Watershed
    {
        constexpr uint32_t W = 49u, H = 49u;
        std::vector<float> gA = MakeAlpineRidgeHeightfield(W, H);
        std::vector<float> gB = gA;

        HydraulicErosionConfig hc{};
        hc.seed         = 998877u;
        hc.dropletCount = 1200u;
        ThermalErosionConfig tc{};
        tc.iterations   = 12u;
        WatershedConfig wc{};
        wc.riverFlowThreshold = 14.0f;

        ApplyHydraulicErosion(gA, W, H, hc);
        ApplyThermalErosion(gA, W, H, tc);
        const WatershedResult wsA = ComputeWatershedHydrology(gA, W, H, wc);

        ApplyHydraulicErosion(gB, W, H, hc);
        ApplyThermalErosion(gB, W, H, tc);
        const WatershedResult wsB = ComputeWatershedHydrology(gB, W, H, wc);

        const bool bitExact = (gA == gB) && (wsA.flowAccum == wsB.flowAccum) &&
                              (wsA.d8Dir == wsB.d8Dir) && (wsA.waterfalls.size() == wsB.waterfalls.size());
        check(bitExact, "hydrology_bit_exact_deterministic_reproducibility");
    }

    // 6. Seamless RawTerrainGen integration hook + watertight seams + ValidateProceduralMeshStrict (0 defects!)
    {
        TerrainConfig tcfg{};
        tcfg.seed           = 202643u;
        tcfg.border.enabled = false; // Full frontier terrain relief

        TerrainChunkData chunkBase{};
        TerrainChunkData chunkHydro{};
        const bool okBase = GenerateTerrainChunk(12, -7, 0u, tcfg, StitchNone, &chunkBase);
        bool okHydro = false;
        {
            ScopedTerrainGenHydrologyErosion enableGuard(true);
            okHydro = GenerateTerrainChunk(12, -7, 0u, tcfg, StitchNone, &chunkHydro);
        }
        // Verify guard cleanly restored default OFF state
        const bool restoredOff = !IsTerrainGenHydrologyErosionEnabled();

        bool bordersExact = okBase && okHydro && (chunkBase.heights.size() == chunkHydro.heights.size());
        bool interiorChanged = false;
        bool splatBankUpdated = false;
        const uint32_t S = chunkHydro.stride;

        if (bordersExact) {
            for (uint32_t iy = 0u; iy < S; ++iy) {
                for (uint32_t ix = 0u; ix < S; ++ix) {
                    const size_t vi = size_t(iy) * S + ix;
                    const bool isBorder = (ix == 0u || iy == 0u || ix + 1u == S || iy + 1u == S);
                    if (isBorder) {
                        if (std::fabs(chunkBase.heights[vi] - chunkHydro.heights[vi]) > 1.0e-5f) {
                            bordersExact = false;
                        }
                    } else {
                        if (std::fabs(chunkBase.heights[vi] - chunkHydro.heights[vi]) > 0.05f) {
                            interiorChanged = true;
                        }
                        if (chunkHydro.splat1[vi].z > 0.05f && chunkHydro.wetness[vi] > 0.1f) {
                            splatBankUpdated = true;
                        }
                    }
                }
            }
        }

        ProceduralMesh chunkMesh{};
        raw::pipeline::ValidationReport rep{};
        const bool meshBuilt = okHydro && BuildTerrainChunkMesh(chunkHydro, 126u, &chunkMesh);
        const bool meshValid = meshBuilt && raw::pipeline::ValidateProceduralMeshStrict(chunkMesh, &rep);

        if (!checkFn) {
            std::printf("[TerrainChunkHook] meshlets=%u tris=%u validatorErrors=%u bankSplat=%s\n",
                        rep.meshletCount, rep.triangleCount, rep.TotalErrors(),
                        splatBankUpdated ? "YES" : "NO");
        }

        check(restoredOff, "hydrology_terrain_hook_scoped_restore_default_off");
        check(bordersExact && interiorChanged && splatBankUpdated,
              "hydrology_terrain_chunk_watertight_borders_and_riverbank_splat");
        check(meshValid && rep.TotalErrors() == 0u,
              "hydrology_terrain_chunk_gpu_meshlets_strict_zero_defects");
    }

    if (!checkFn) {
        std::printf("HydrologyGenTests completed: %s (failures=%d)\n",
                    failures == 0 ? "PASS" : "FAIL", failures);
    }
    return failures;
}

}  // namespace raw::worldgen

#ifdef RAW_HYDROLOGY_STANDALONE_MAIN
int main() {
    const int failures = raw::worldgen::RunHydrologyGenTests(nullptr);
    return failures == 0 ? 0 : 1;
}
#endif
