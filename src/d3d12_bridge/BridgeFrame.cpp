// D3D12Bridge per-frame work: depth capture, the fence round trip, and GPU
// timestamps on both queues. Original work, MIT.
#include "BridgeImpl.h"
#include "RawProceduralLib.h"
#include "StreamlineHost.h"
#include "LightingPass.h"
#include "RtScene.h"
#include "../worldgen/RawHydrologyGen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace raw::bridge
{
namespace
{

// Exact byte-for-byte mirror of LightingPass.cpp LightUniform (cbuffer P) for zero-coupling telemetry readback.
struct TelemetryLightUniformView
{
    float dims[4], proj[4], clipToWorld[16], worldToClip[16], viewToWorld[12];
    float resScale[4], sunDir[4], sunColour[4], ambient[12], cascade[24], cascadeInfo[4];
    uint32_t flags[4];
    float cluster[4], tune[4], tune2[4], weather[4], camPos[4], clouds[4], cloudWind[4], waterA[4], waterB[4], prevWorldToClip[16];
    float lightPosR[kMaxSceneLights][4], lightColS[kMaxSceneLights][4];
    uint32_t clusters[kClusterCount][4];
    float footTrail[32][4];
    float dofParams[4];
    float timeEnv[4];
    float engineWorld[4];
    float camOptics[2][4];
    float physWorld[2][4];
    float seasonThermal0[4];      // x: snowDepthM, y: snowAlbedo, z: snowCompaction01, w: surfaceTempC
    float seasonThermal1[4];      // x: iceThicknessM, y: blackIce01, z: frostRime01, w: iceFracture01
    float phenology0[4];
    float phenology1[4];
    float impactDecalPosR[16][4]; // xyz: world pos, w: radiusU (0 = inactive)
    float impactDecalMeta[16][4];
};
static_assert(sizeof(TelemetryLightUniformView) == 40 * 16 + 2 * kMaxSceneLights * 16 + kClusterCount * 16 + 32 * 16 + 176 + 512,
              "TelemetryLightUniformView must match LightingPass cbuffer P");

} // namespace

void D3D12Bridge::Impl::EmitLiveTelemetry(bool rtActive)
{
    if (frames != 1ull && (frames % 180ull) != 0ull) return;

    float seasonSnowM = 0.0f;
    float seasonIceM = 0.0f;
    float skinTempC = 0.0f;
    uint32_t activeImpactDecals = 0u;

    const void* uPtr = nullptr;
    size_t uBytes = 0;
    if (pass && LightingPassUniform(pass.get(), &uPtr, &uBytes) &&
        uPtr != nullptr && uBytes >= sizeof(TelemetryLightUniformView)) {
        const auto* u = static_cast<const TelemetryLightUniformView*>(uPtr);
        seasonSnowM = std::isfinite(u->seasonThermal0[0]) ? u->seasonThermal0[0] : 0.0f;
        skinTempC   = std::isfinite(u->seasonThermal0[3]) ? u->seasonThermal0[3] : 0.0f;
        seasonIceM  = std::isfinite(u->seasonThermal1[0]) ? u->seasonThermal1[0] : 0.0f;
        for (uint32_t i = 0; i < 16u; ++i) {
            if (u->impactDecalPosR[i][3] > 0.0f) ++activeImpactDecals;
        }
    }

    uint32_t gpuMeshlets = 0u;
    for (const auto& kv : streamedTerrainGpu) {
        gpuMeshlets += kv.second.meshletCount;
    }
    const uint32_t worldGenMeshlets = (gpuMeshlets > 0u) ? gpuMeshlets : lastWorldStats.totalMeshlets;
    const uint32_t worldGenActiveChunks = lastWorldStats.activeChunkCount;
    const uint32_t worldGenPendingAsync = worldShowcase.PendingAsyncJobs();
    const char* biomeName = raw::worldgen::GetBiomeName(lastWorldStats.cameraBiome);
    if (!biomeName) biomeName = "Unknown";

    const FluidGpuTimings fluidTimings = fluidReady ? fluidSim.GetLastTimings() : FluidGpuTimings{};
    const RadianceGpuTimings radTimings = radianceReady ? radianceHost.GetLastTimings() : RadianceGpuTimings{};
    const float fluidFireMs = (fluidTimings.valid && std::isfinite(fluidTimings.fireStepMs)) ? fluidTimings.fireStepMs : 0.0f;
    const float fluidWaterMs = (fluidTimings.valid && std::isfinite(fluidTimings.waterStepMs)) ? fluidTimings.waterStepMs : 0.0f;
    const uint32_t fluidPressureIters = fluidTimings.activePressureIters;
    const float radianceGiMs = (radTimings.valid && std::isfinite(radTimings.giPassMs)) ? radTimings.giPassMs : 0.0f;
    const float xpbdStepMs = (xpbdSimReady && xpbdSim && xpbdSim->HasValidTiming() && std::isfinite(xpbdSim->GetLastStepMs()))
        ? xpbdSim->GetLastStepMs() : 0.0f;
    const uint32_t xpbdParticles = (xpbdSimReady && xpbdSim) ? xpbdSim->ParticleCount() : 0u;
    const float overlayMs = (overlayHostReady && overlayHost && overlayHost->HasValidTiming() &&
                             std::isfinite(overlayHost->GetLastOverlayMs()))
        ? overlayHost->GetLastOverlayMs() : 0.0f;
    const float gpuCullHiZMs = (gpuCullReady && gpuCull && gpuCull->HasValidHiZTiming() &&
                                std::isfinite(gpuCull->GetLastHiZMs()))
        ? gpuCull->GetLastHiZMs() : 0.0f;
    const float gpuCullMs = (gpuCullReady && gpuCull && gpuCull->HasValidCullTiming() &&
                             std::isfinite(gpuCull->GetLastCullMs()))
        ? gpuCull->GetLastCullMs() : 0.0f;
    const uint32_t gpuCullInstances = gpuCullReady ? lastGpuCullInstances : 0u;
    const float lightClusterBuildMs = (lightClustersReady && lightClusters && lightClusters->HasValidTiming() &&
                                       std::isfinite(lightClusters->GetLastBuildMs()))
        ? lightClusters->GetLastBuildMs() : 0.0f;
    const float lightClusterAssignMs = (lightClustersReady && lightClusters && lightClusters->HasValidTiming() &&
                                        std::isfinite(lightClusters->GetLastAssignMs()))
        ? lightClusters->GetLastAssignMs() : 0.0f;
    const uint32_t lightClusterLights = lightClustersReady ? lastClusterLightCount : 0u;
    const float froxelInjectMs = (volFroxelsReady && volFroxels && volFroxels->HasValidTiming() &&
                                  std::isfinite(volFroxels->GetLastInjectMs()))
        ? volFroxels->GetLastInjectMs() : 0.0f;
    const float froxelIntegrateMs = (volFroxelsReady && volFroxels && volFroxels->HasValidTiming() &&
                                     std::isfinite(volFroxels->GetLastIntegrateMs()))
        ? volFroxels->GetLastIntegrateMs() : 0.0f;
    const float shadowPcssMs = (shadowPcssReady && shadowPcss && shadowPcss->HasValidTiming() &&
                                std::isfinite(shadowPcss->GetLastPcssMs()))
        ? shadowPcss->GetLastPcssMs() : 0.0f;
    const float gtaoMainMs = (gtaoHostReady && gtaoHost && gtaoHost->HasValidTiming() &&
                              std::isfinite(gtaoHost->GetLastMainMs()))
        ? gtaoHost->GetLastMainMs() : 0.0f;
    const float gtaoDenoiseMs = (gtaoHostReady && gtaoHost && gtaoHost->HasValidTiming() &&
                                 std::isfinite(gtaoHost->GetLastDenoiseMs()))
        ? gtaoHost->GetLastDenoiseMs() : 0.0f;
    const float ssrTraceMs = (ssrHostReady && ssrHost && ssrHost->HasValidTiming() &&
                              std::isfinite(ssrHost->GetLastSsrMs()))
        ? ssrHost->GetLastSsrMs() : 0.0f;
    const float autoExposureHistMs = (autoExposureReady && autoExposure && autoExposure->HasValidTiming() &&
                                      std::isfinite(autoExposure->GetLastHistMs()))
        ? autoExposure->GetLastHistMs() : 0.0f;
    const float autoExposureAdaptMs = (autoExposureReady && autoExposure && autoExposure->HasValidTiming() &&
                                       std::isfinite(autoExposure->GetLastAdaptMs()))
        ? autoExposure->GetLastAdaptMs() : 0.0f;
    const float cloudRaymarchMs = (cloudRaymarchReady && cloudRaymarch && cloudRaymarch->HasValidTiming() &&
                                   std::isfinite(cloudRaymarch->GetLastCloudMs()))
        ? cloudRaymarch->GetLastCloudMs() : 0.0f;
    const float postCinematicMs = (postCinematicReady && postCinematic && postCinematic->HasValidTiming() &&
                                   std::isfinite(postCinematic->GetLastTotalMs()))
        ? postCinematic->GetLastTotalMs() : 0.0f;
    const float gpuParticlesMs = (gpuParticlesReady && gpuParticles && gpuParticles->HasValidTiming() &&
                                  std::isfinite(gpuParticles->GetLastTotalMs()))
        ? gpuParticles->GetLastTotalMs() : 0.0f;

    char jsonBuf[3584]{};
    const int n = std::snprintf(
        jsonBuf, sizeof(jsonBuf),
        "{\n"
        "  \"frame\": %llu,\n"
        "  \"width\": %u,\n"
        "  \"height\": %u,\n"
        "  \"attached\": %s,\n"
        "  \"rtActive\": %s,\n"
        "  \"worldGenActiveChunks\": %u,\n"
        "  \"worldGenMeshlets\": %u,\n"
        "  \"worldGenPendingAsync\": %u,\n"
        "  \"worldGenBiome\": \"%s\",\n"
        "  \"fluidFireEmitters\": %u,\n"
        "  \"fluidWaterWakes\": %u,\n"
        "  \"fluidFireMs\": %.4f,\n"
        "  \"fluidWaterMs\": %.4f,\n"
        "  \"fluidPressureIters\": %u,\n"
        "  \"radianceGiMs\": %.4f,\n"
        "  \"xpbdStepMs\": %.4f,\n"
        "  \"xpbdParticles\": %u,\n"
        "  \"overlayHudEnabled\": %s,\n"
        "  \"overlayMs\": %.4f,\n"
        "  \"gpuCullHiZMs\": %.4f,\n"
        "  \"gpuCullMs\": %.4f,\n"
        "  \"gpuCullInstances\": %u,\n"
        "  \"lightClusterBuildMs\": %.4f,\n"
        "  \"lightClusterAssignMs\": %.4f,\n"
        "  \"lightClusterLights\": %u,\n"
        "  \"froxelInjectMs\": %.4f,\n"
        "  \"froxelIntegrateMs\": %.4f,\n"
        "  \"shadowPcssMs\": %.4f,\n"
        "  \"gtaoMainMs\": %.4f,\n"
        "  \"gtaoDenoiseMs\": %.4f,\n"
        "  \"ssrTraceMs\": %.4f,\n"
        "  \"autoExposureHistMs\": %.4f,\n"
        "  \"autoExposureAdaptMs\": %.4f,\n"
        "  \"cloudRaymarchMs\": %.4f,\n"
        "  \"postCinematicMs\": %.4f,\n"
        "  \"gpuParticlesMs\": %.4f,\n"
        "  \"seasonSnowM\": %.4f,\n"
        "  \"seasonIceM\": %.4f,\n"
        "  \"skinTempC\": %.3f,\n"
        "  \"activeImpactDecals\": %u,\n"
        "  \"radianceCascadesActive\": %s,\n"
        "  \"hydrologyErosionActive\": %s\n"
        "}\n",
        static_cast<unsigned long long>(frames),
        static_cast<unsigned>(cfg.width),
        static_cast<unsigned>(cfg.height),
        attached ? "true" : "false",
        rtActive ? "true" : "false",
        worldGenActiveChunks,
        worldGenMeshlets,
        worldGenPendingAsync,
        biomeName,
        lastFluidFireEmitters,
        lastFluidWaterWakes,
        fluidFireMs,
        fluidWaterMs,
        fluidPressureIters,
        radianceGiMs,
        xpbdStepMs,
        xpbdParticles,
        overlayHudEnabled ? "true" : "false",
        overlayMs,
        gpuCullHiZMs,
        gpuCullMs,
        gpuCullInstances,
        lightClusterBuildMs,
        lightClusterAssignMs,
        lightClusterLights,
        froxelInjectMs,
        froxelIntegrateMs,
        shadowPcssMs,
        gtaoMainMs,
        gtaoDenoiseMs,
        ssrTraceMs,
        autoExposureHistMs,
        autoExposureAdaptMs,
        cloudRaymarchMs,
        postCinematicMs,
        gpuParticlesMs,
        seasonSnowM,
        seasonIceM,
        skinTempC,
        activeImpactDecals,
        (radianceReady && radianceHost.ScreenGIReady()) ? "true" : "false",
        raw::worldgen::IsTerrainGenHydrologyErosionEnabled() ? "true" : "false");
    if (n <= 0) return;

    try {
        const std::string outDir = telemetryOutDir.empty() ? std::string("raw_engine_out") : telemetryOutDir;
        CreateDirectoryA(outDir.c_str(), nullptr);
        const std::string tmpPath = outDir + "\\bridge_live_telemetry.json.tmp";
        const std::string dstPath = outDir + "\\bridge_live_telemetry.json";
        std::ofstream ofs(tmpPath, std::ios::binary | std::ios::trunc);
        if (ofs.is_open()) {
            ofs.write(jsonBuf, static_cast<std::streamsize>(n));
            ofs.flush();
            const bool writeOk = ofs.good();
            ofs.close();
            if (writeOk) {
                MoveFileExA(tmpPath.c_str(), dstPath.c_str(), MOVEFILE_REPLACE_EXISTING);
            }
        }
    } catch (...) {
        // Non-blocking telemetry: I/O failures must never impact frame rendering.
    }
}

bool D3D12Bridge::Impl::StepFluidSimOnQueue(bool isInterior)
{
    if (!fluidReady || !fluidSim.Ready() || !queue || !fluidList || !fluidFence || !fluidEvent) return false;

    const uint32_t slot = fluidSlot;
    if (fluidSlotVal[slot] > 0 && fluidFence->GetCompletedValue() < fluidSlotVal[slot]) {
        fluidFence->SetEventOnCompletion(fluidSlotVal[slot], fluidEvent);
        WaitForSingleObject(fluidEvent, 3000);
    }
    if (FAILED(fluidAlloc[slot]->Reset()) || FAILED(fluidList->Reset(fluidAlloc[slot].Get(), nullptr))) {
        return false;
    }

    const uint64_t nowMs = GetTickCount64();
    float dtSec = 1.0f / 60.0f;
    if (fluidLastMs != 0) {
        dtSec = std::clamp(float(double(nowMs - fluidLastMs) * 0.001), 1.0e-4f, 0.05f);
    }
    fluidLastMs = nowMs;
    if (lighting.menuPaused) dtSec = 1.0e-4f;

    LightingFluidInputs fi{};
    LightingPassFluidInputs(pass.get(), &fi);

    // 1. Build 64^3 3D Eulerian Navier-Stokes Fire/Smoke params from verified warm lights + live wind:
    FluidFireParams fp{};
    fp.cellSize = 4.0f; // 64 * 4 = 256u domain cube
    fp.dt = dtSec;
    fp.timeSec = fi.simTimeSec;
    fp.windVelW[0] = isInterior ? 0.0f : std::clamp(fi.windXY[0] * 0.12f, -36.0f, 36.0f);
    fp.windVelW[1] = isInterior ? 0.0f : std::clamp(fi.windXY[1] * 0.12f, -36.0f, 36.0f);
    fp.windVelW[2] = 0.0f;
    fp.ambientTempK = std::clamp(fi.ambientTempC + 273.15f, 220.0f, 330.0f);
    fp.enableCeiling = isInterior;
    fp.jacobiIterations = 8u;

    // Anchor domain around closest strictly-verified warm fire emitter within 1400u (falls back to camera pos):
    float anchorW[3] = {lighting.posAdjust[0], lighting.posAdjust[1], lighting.posAdjust[2]};
    float bestFireD2 = 1400.0f * 1400.0f;
    float minEmitterZ = lighting.posAdjust[2] - 110.0f;
    uint32_t emitCount = 0u;
    for (uint32_t i = 0; i < lighting.lightCount && emitCount < 16u; ++i) {
        const SceneLight& l = lighting.lights[i];
        // Strict fire-core chromaticity + physical hearth/torch/brazier radius gate (never wide neutral room-fill lights!):
        if (l.colour[0] > l.colour[1] * 1.25f && l.colour[0] > l.colour[2] * 2.2f &&
            l.colour[0] > 0.45f && l.radius >= 50.0f && l.radius <= 900.0f) {
            const float wx = lighting.posAdjust[0] + l.position[0];
            const float wy = lighting.posAdjust[1] + l.position[1];
            const float wz = lighting.posAdjust[2] + l.position[2];
            const float d2 = l.position[0] * l.position[0] + l.position[1] * l.position[1] + l.position[2] * l.position[2];
            if (d2 < bestFireD2) {
                bestFireD2 = d2;
                anchorW[0] = wx; anchorW[1] = wy; anchorW[2] = wz - 32.0f;
            }
            const float emitZ = wz - 20.0f;
            minEmitterZ = (emitCount == 0u) ? emitZ : std::min(minEmitterZ, emitZ);
            FluidFireEmitter& em = fp.emitters[emitCount++];
            em.posW[0] = wx;
            em.posW[1] = wy;
            em.posW[2] = emitZ;
            em.radius = std::clamp(l.radius * 0.08f, 6.0f, 26.0f);
            em.ignitionTempK = std::clamp(1950.0f + l.colour[0] * 900.0f, 1950.0f, 3300.0f);
            em.fuelRate = 2.4f;
            em.jetSpeed = 30.0f;
            em.curlStrength = 14.0f;
            em.jetDir[0] = 0.0f; em.jetDir[1] = 0.0f; em.jetDir[2] = 1.0f;
            em.sootSeedRate = 0.35f;
        }
    }
    fp.emitterCount = emitCount;
    const float domainSpan = float(RawFluidSimHost::kFireGridRes) * fp.cellSize;
    const float halfDomain = 0.5f * domainSpan;
    fp.domainOriginW[0] = anchorW[0] - halfDomain;
    fp.domainOriginW[1] = anchorW[1] - halfDomain;
    const float floorZ = (emitCount > 0u) ? (minEmitterZ - 14.0f) : (lighting.posAdjust[2] - 124.0f);
    fp.domainOriginW[2] = floorZ - fp.cellSize * 2.0f;
    fp.floorZLocal = std::clamp((floorZ - fp.domainOriginW[2]) / fp.cellSize, 1.0f, 10.0f);

    // Inject real physical floor slab + interior ceiling baffle AABBs so 3D NS fire & smoke collide and spread across ceilings:
    fp.obstacles[0].minW[0] = fp.domainOriginW[0] - 64.0f;
    fp.obstacles[0].minW[1] = fp.domainOriginW[1] - 64.0f;
    fp.obstacles[0].minW[2] = floorZ - 24.0f;
    fp.obstacles[0].maxW[0] = fp.domainOriginW[0] + domainSpan + 64.0f;
    fp.obstacles[0].maxW[1] = fp.domainOriginW[1] + domainSpan + 64.0f;
    fp.obstacles[0].maxW[2] = floorZ;
    fp.obstacles[0].active = true;
    fp.obstacles[0].roughness = 0.65f;
    fp.obstacleCount = 1u;

    if (isInterior) {
        const float ceilZ = floorZ + 240.0f;
        fp.ceilingZLocal = std::clamp((ceilZ - fp.domainOriginW[2]) / fp.cellSize, 42.0f, 62.0f);
        fp.obstacles[1].minW[0] = fp.domainOriginW[0] - 64.0f;
        fp.obstacles[1].minW[1] = fp.domainOriginW[1] - 64.0f;
        fp.obstacles[1].minW[2] = ceilZ;
        fp.obstacles[1].maxW[0] = fp.domainOriginW[0] + domainSpan + 64.0f;
        fp.obstacles[1].maxW[1] = fp.domainOriginW[1] + domainSpan + 64.0f;
        fp.obstacles[1].maxW[2] = ceilZ + 32.0f;
        fp.obstacles[1].active = true;
        fp.obstacles[1].roughness = 0.55f;
        fp.obstacleCount = 2u;
    }

    // 2. Build 256^2 Toroidal Shallow-Water Hydrodynamics params from camPos, footTrail[0..31], rain & Beaufort swell:
    FluidWaterParams wp{};
    wp.camPosXY[0] = lighting.posAdjust[0];
    wp.camPosXY[1] = lighting.posAdjust[1];
    wp.cellSize = 8.0f; // 256 * 8 = 2048u toroidal span
    wp.dt = dtSec;
    wp.timeSec = fi.simTimeSec;
    wp.windDirXY[0] = fi.swellDirXY[0];
    wp.windDirXY[1] = fi.swellDirXY[1];
    wp.swellAmp = isInterior ? 0.15f : std::clamp(0.45f + fi.beaufortScale * 0.42f, 0.25f, 4.5f);
    wp.rainIntensity = isInterior ? 0.0f : std::clamp(fi.rainIntensity, 0.0f, 1.0f);
    wp.waterPlaneZ = fi.waterPlaneZ;
    uint32_t wakeCount = 0u;
    for (uint32_t i = 0; i < 32u; ++i) {
        if (fi.footTrail[i][3] > 0.01f) {
            FluidWaterTrail& tr = wp.trails[wakeCount++];
            tr.posW[0] = fi.footTrail[i][0];
            tr.posW[1] = fi.footTrail[i][1];
            tr.posW[2] = fi.footTrail[i][2];
            tr.strength = fi.footTrail[i][3];
            tr.velW[0] = 0.0f;
            tr.velW[1] = 0.0f;
            tr.radius = 20.0f;
            tr.splashScale = 1.0f;
        }
    }
    wp.trailCount = wakeCount;
    lastFluidFireEmitters = emitCount;
    lastFluidWaterWakes = wakeCount;

    if (emitCount > 0u || fluidStepFrames == 0ull) {
        fluidSim.StepFire(fluidList.Get(), fp);
    }
    fluidSim.StepWater(fluidList.Get(), wp);

    if (FAILED(fluidList->Close())) return false;
    ID3D12CommandList* lists[1] = {fluidList.Get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(fluidFence.Get(), ++fluidFenceVal);
    fluidSlotVal[slot] = fluidFenceVal;
    fluidSlot = (slot + 1u) % kRing;

    if ((fluidStepFrames++ % 180ull) == 0ull) {
        log("[FluidSim] live GPU step: fireEmitters=%u, waterWakes=%u", emitCount, wakeCount);
    }
    return true;
}

bool D3D12Bridge::Impl::ExecuteRadianceScreenGiOnQueue()
{
    if (!radianceReady || !radianceHost.ScreenGIReady() || !queue || !radList || !radFence || !radEvent ||
        !colorOut.res12 || !depthIn.res12 || cfg.width == 0u || cfg.height == 0u) {
        return false;
    }
    if ((colorOut.res12->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
        return false;
    }

    const uint32_t slot = radSlot;
    if (radSlotVal[slot] > 0 && radFence->GetCompletedValue() < radSlotVal[slot]) {
        radFence->SetEventOnCompletion(radSlotVal[slot], radEvent);
        WaitForSingleObject(radEvent, 3000);
    }
    if (FAILED(radAlloc[slot]->Reset()) || FAILED(radList->Reset(radAlloc[slot].Get(), nullptr))) {
        return false;
    }

    radianceHost.ExecuteScreenGI(radList.Get(), colorOut.res12.Get(), depthIn.res12.Get(),
                                 cfg.width, cfg.height, 0.16f);

    if (FAILED(radList->Close())) return false;
    ID3D12CommandList* lists[1] = {radList.Get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(radFence.Get(), ++radFenceVal);
    radSlotVal[slot] = radFenceVal;
    radSlot = (slot + 1u) % kRing;

    if ((radStepFrames++ % 180ull) == 0ull) {
        log("[RadianceCascades] live 4-cascade ScreenGI active (%ux%u, strength=0.16)",
            static_cast<unsigned>(cfg.width), static_cast<unsigned>(cfg.height));
    }
    return true;
}

bool D3D12Bridge::Impl::StepXpbdSimOnQueue(bool isInterior)
{
    if (!xpbdSimReady || !xpbdSim || !xpbdSim->Ready() || !queue || !xpbdList || !xpbdFence || !xpbdEvent) {
        return false;
    }

    const uint32_t slot = xpbdSlot;
    if (xpbdSlotVal[slot] > 0 && xpbdFence->GetCompletedValue() < xpbdSlotVal[slot]) {
        xpbdFence->SetEventOnCompletion(xpbdSlotVal[slot], xpbdEvent);
        WaitForSingleObject(xpbdEvent, 3000);
    }
    if (FAILED(xpbdAlloc[slot]->Reset()) || FAILED(xpbdList->Reset(xpbdAlloc[slot].Get(), nullptr))) {
        return false;
    }

    if (!xpbdChainsUploaded) {
        std::vector<XpbdParticle> particles;
        std::vector<XpbdChainMeta> chains;
        BuildHairAndFoliageDemoChains(particles, chains, 256u);
        if (!xpbdSim->UploadChains(xpbdList.Get(), particles, chains)) {
            xpbdList->Close();
            return false;
        }
        xpbdChainsUploaded = true;
    }

    const uint64_t nowMs = GetTickCount64();
    float dtSec = 1.0f / 60.0f;
    if (xpbdLastMs != 0) {
        dtSec = std::clamp(float(double(nowMs - xpbdLastMs) * 0.001), 1.0e-4f, 0.05f);
    }
    xpbdLastMs = nowMs;
    if (lighting.menuPaused) dtSec = 1.0e-4f;

    LightingFluidInputs fi{};
    LightingPassFluidInputs(pass.get(), &fi);

    float seasonSnowM = 0.0f;
    const void* uPtr = nullptr;
    size_t uBytes = 0;
    if (pass && LightingPassUniform(pass.get(), &uPtr, &uBytes) &&
        uPtr != nullptr && uBytes >= sizeof(TelemetryLightUniformView)) {
        const auto* u = static_cast<const TelemetryLightUniformView*>(uPtr);
        if (std::isfinite(u->seasonThermal0[0])) {
            seasonSnowM = std::max(0.0f, u->seasonThermal0[0]);
        }
    }

    XpbdStepParams xp{};
    xp.dt              = dtSec;
    xp.timeSec         = fi.simTimeSec;
    xp.substeps        = 4u;
    xp.gravityZ        = -9.80665f;
    const float wLen   = std::sqrt(fi.windXY[0] * fi.windXY[0] + fi.windXY[1] * fi.windXY[1]);
    if (wLen > 1.0e-4f) {
        xp.windDir[0] = fi.windXY[0] / wLen;
        xp.windDir[1] = fi.windXY[1] / wLen;
        xp.windDir[2] = 0.0f;
    } else {
        xp.windDir[0] = 0.92f;
        xp.windDir[1] = 0.39f;
        xp.windDir[2] = 0.0f;
    }
    xp.windSpeedMps    = isInterior ? 0.35f : std::clamp(0.6f + fi.beaufortScale * 2.2f, 0.5f, 24.0f);
    xp.gustPhase       = fi.simTimeSec * 0.85f;
    xp.snowLoad01      = isInterior ? 0.0f : std::clamp(seasonSnowM * 1.5f, 0.0f, 1.0f);
    xp.maxSpeed        = 35.0f;
    xp.collisionMargin = 0.005f;

    // Head sphere + torso capsule + animated bush push-aside wake sphere:
    xp.colliders[0].p0[0]  = 0.0f; xp.colliders[0].p0[1] = 0.0f; xp.colliders[0].p0[2] = 1.62f;
    xp.colliders[0].radius = 0.14f;
    xp.colliders[0].flags  = 0.0f;

    xp.colliders[1].p0[0]  = 0.0f; xp.colliders[1].p0[1] = 0.0f; xp.colliders[1].p0[2] = 1.48f;
    xp.colliders[1].p1[0]  = 0.0f; xp.colliders[1].p1[1] = 0.0f; xp.colliders[1].p1[2] = 0.88f;
    xp.colliders[1].radius = 0.22f;
    xp.colliders[1].flags  = 1.0f;

    xp.colliders[2].p0[0]  = 3.0f + std::sin(fi.simTimeSec * 0.7f) * 0.35f;
    xp.colliders[2].p0[1]  = std::cos(fi.simTimeSec * 0.5f) * 0.35f;
    xp.colliders[2].p0[2]  = 0.65f;
    xp.colliders[2].radius = 0.32f;
    xp.colliders[2].flags  = 0.0f;
    xp.colliderCount       = 3u;

    xpbdSim->Step(xpbdList.Get(), xp);

    if (FAILED(xpbdList->Close())) return false;
    ID3D12CommandList* lists[1] = {xpbdList.Get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(xpbdFence.Get(), ++xpbdFenceVal);
    xpbdSlotVal[slot] = xpbdFenceVal;
    xpbdSlot = (slot + 1u) % kRing;

    if ((xpbdStepFrames++ % 180ull) == 0ull) {
        log("[XpbdSim] live GPU step: chains=%u, particles=%u, stepMs=%.4f, wind=%.1f m/s, snowLoad=%.2f",
            xpbdSim->ChainCount(), xpbdSim->ParticleCount(),
            xpbdSim->HasValidTiming() ? xpbdSim->GetLastStepMs() : 0.0f,
            xp.windSpeedMps, xp.snowLoad01);
    }
    return true;
}

void D3D12Bridge::Impl::RecordFrameTimingSample()
{
    if (qpcFreq == 0) {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        qpcFreq = f.QuadPart;
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (lastFrameQpc != 0 && qpcFreq > 0) {
        const double dtMs = static_cast<double>(now.QuadPart - lastFrameQpc) * 1000.0 / static_cast<double>(qpcFreq);
        const float clampedMs = std::clamp(static_cast<float>(dtMs), 0.1f, 250.0f);
        lastFrameMs = clampedMs;
        frameMsHistory[frameMsHistoryHead] = clampedMs;
        frameMsHistoryHead = (frameMsHistoryHead + 1u) % 64u;
        if (frameMsHistoryCount < 64u) {
            ++frameMsHistoryCount;
        }
    }
    lastFrameQpc = now.QuadPart;
}

void D3D12Bridge::Impl::PollOverlayHotkey()
{
    HWND fg = GetForegroundWindow();
    DWORD fgPid = 0;
    if (fg) {
        GetWindowThreadProcessId(fg, &fgPid);
    }
    const bool appFocused = (fg != nullptr && fgPid == GetCurrentProcessId());
    const bool f9Down = appFocused && ((GetAsyncKeyState(VK_F9) & 0x8000) != 0);
    if (f9Down && !overlayPrevF9Down) {
        overlayHudEnabled = !overlayHudEnabled;
        log("[OverlayHud] F9 toggled GPU Telemetry HUD -> %s", overlayHudEnabled ? "ON" : "OFF");
    }
    overlayPrevF9Down = f9Down;
}

bool D3D12Bridge::Impl::ExecuteOverlayHudOnQueue(bool isInterior)
{
    // Strict zero-touch gate: when disabled, emit ZERO GPU commands and modify 0 pixels!
    if (!overlayHudEnabled || !overlayHostReady || !overlayHost || !overlayHost->Ready() ||
        !queue || !overlayList || !overlayFence || !overlayEvent ||
        !colorOut.res12 || cfg.width == 0u || cfg.height == 0u) {
        return false;
    }
    const D3D12_RESOURCE_DESC outDesc = colorOut.res12->GetDesc();
    if ((outDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
        return false;
    }

    const uint32_t slot = overlaySlot;
    if (overlaySlotVal[slot] > 0 && overlayFence->GetCompletedValue() < overlaySlotVal[slot]) {
        overlayFence->SetEventOnCompletion(overlaySlotVal[slot], overlayEvent);
        WaitForSingleObject(overlayEvent, 3000);
    }
    if (FAILED(overlayAlloc[slot]->Reset()) || FAILED(overlayList->Reset(overlayAlloc[slot].Get(), nullptr))) {
        return false;
    }

    LightingFluidInputs fi{};
    if (pass) {
        LightingPassFluidInputs(pass.get(), &fi);
    }

    float seasonSnowM = 0.0f;
    float skinTempC = fi.ambientTempC;
    float weatherSnow = 0.0f;
    float cloudCover = 0.0f;
    const void* uPtr = nullptr;
    size_t uBytes = 0;
    if (pass && LightingPassUniform(pass.get(), &uPtr, &uBytes) &&
        uPtr != nullptr && uBytes >= sizeof(TelemetryLightUniformView)) {
        const auto* u = static_cast<const TelemetryLightUniformView*>(uPtr);
        if (std::isfinite(u->seasonThermal0[0])) seasonSnowM = std::max(0.0f, u->seasonThermal0[0]);
        if (std::isfinite(u->seasonThermal0[3])) skinTempC = u->seasonThermal0[3];
        if (std::isfinite(u->weather[1])) weatherSnow = u->weather[1];
        if (std::isfinite(u->clouds[0])) cloudCover = u->clouds[0];
    }

    const FluidGpuTimings fluidTimings = fluidReady ? fluidSim.GetLastTimings() : FluidGpuTimings{};
    const RadianceGpuTimings radTimings = radianceReady ? radianceHost.GetLastTimings() : RadianceGpuTimings{};

    uint32_t gpuMeshlets = 0u;
    for (const auto& kv : streamedTerrainGpu) {
        gpuMeshlets += kv.second.meshletCount;
    }

    float orderedHist[64]{};
    const uint32_t histCount = std::min(frameMsHistoryCount, 64u);
    const uint32_t startIdx = (frameMsHistoryCount < 64u) ? 0u : frameMsHistoryHead;
    for (uint32_t i = 0u; i < histCount; ++i) {
        orderedHist[i] = frameMsHistory[(startIdx + i) % 64u];
    }

    const char* seasonName = "AUTUMN";
    if (seasonSnowM > 0.05f || skinTempC < 0.0f) seasonName = "WINTER";
    else if (skinTempC > 18.0f) seasonName = "SUMMER";
    else if (skinTempC > 11.0f) seasonName = "SPRING";

    const char* weatherName = "CLEAR";
    if (isInterior) weatherName = "INDOOR";
    else if (weatherSnow > 0.45f && fi.beaufortScale > 5.0f) weatherName = "BLIZZARD";
    else if (weatherSnow > 0.15f) weatherName = "SNOW";
    else if (fi.rainIntensity > 0.55f) weatherName = "STORM";
    else if (fi.rainIntensity > 0.12f) weatherName = "RAIN";
    else if (cloudCover > 0.65f) weatherName = "OVERCAST";

    const char* landmark = isInterior ? "INTERIOR" : raw::worldgen::GetBiomeName(lastWorldStats.cameraBiome);
    if (!landmark) landmark = "TAMRIEL";

    OverlayTelemetrySnapshot snap{};
    snap.fps            = (lastFrameMs > 0.05f) ? (1000.0f / lastFrameMs) : 0.0f;
    snap.frameMs        = lastFrameMs;
    snap.fireMs         = (fluidTimings.valid && std::isfinite(fluidTimings.fireStepMs)) ? fluidTimings.fireStepMs : 0.0f;
    snap.waterMs        = (fluidTimings.valid && std::isfinite(fluidTimings.waterStepMs)) ? fluidTimings.waterStepMs : 0.0f;
    snap.giMs           = (radTimings.valid && std::isfinite(radTimings.giPassMs)) ? radTimings.giPassMs : 0.0f;
    snap.xpbdMs         = (xpbdSimReady && xpbdSim && xpbdSim->HasValidTiming()) ? xpbdSim->GetLastStepMs() : 0.0f;
    snap.overlayMs      = overlayHost->HasValidTiming() ? overlayHost->GetLastOverlayMs() : 0.0f;
    snap.seasonName     = seasonName;
    snap.weatherName    = weatherName;
    snap.tempC          = skinTempC;
    snap.windMps        = isInterior ? 0.0f : std::clamp(0.6f + fi.beaufortScale * 2.2f, 0.0f, 35.0f);
    snap.snowPackM      = isInterior ? 0.0f : seasonSnowM;
    snap.cellX          = static_cast<int32_t>(std::floor(lighting.posAdjust[0] / 4096.0f));
    snap.cellY          = static_cast<int32_t>(std::floor(lighting.posAdjust[1] / 4096.0f));
    snap.landmarkName   = landmark;
    snap.activeCells    = lastWorldStats.activeChunkCount;
    snap.activeMeshlets = (gpuMeshlets > 0u) ? gpuMeshlets : lastWorldStats.totalMeshlets;
    snap.lruHitRatePct  = 0.0f;
    snap.giEnabled      = radianceReady && radianceHost.ScreenGIReady();
    snap.fluidEnabled   = fluidReady && fluidSim.Ready();
    snap.xpbdEnabled    = xpbdSimReady && xpbdSim && xpbdSim->Ready();
    snap.historyFrameMs = orderedHist;
    snap.historyCount   = histCount;

    OverlayHudData hud{};
    hud.enabled    = true;
    hud.originX    = 16u;
    hud.originY    = 16u;
    hud.glyphScale = (cfg.height >= 1800u) ? 2u : 1u;
    hud.opacity    = 0.86f;
    hud.srgbOutput = (outDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
                      outDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
    FormatEngineTelemetryLines(snap, hud);

    overlayHost->RecordOverlay(overlayList.Get(), colorOut.res12.Get(),
                               D3D12_RESOURCE_STATE_COMMON, cfg.width, cfg.height, hud);

    if (FAILED(overlayList->Close())) return false;
    ID3D12CommandList* lists[1] = {overlayList.Get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(overlayFence.Get(), ++overlayFenceVal);
    overlaySlotVal[slot] = overlayFenceVal;
    overlaySlot = (slot + 1u) % kRing;
    return true;
}

bool D3D12Bridge::Impl::ExecuteLightClustersOnQueue()
{
    if (!lightClustersReady || !lightClusters || !lightClusters->Ready() ||
        !queue || !lightClusterList || !lightClusterFence || !lightClusterEvent) {
        return false;
    }

    const uint32_t slot = lightClusterSlot;
    if (lightClusterSlotVal[slot] > 0 && lightClusterFence->GetCompletedValue() < lightClusterSlotVal[slot]) {
        lightClusterFence->SetEventOnCompletion(lightClusterSlotVal[slot], lightClusterEvent);
        WaitForSingleObject(lightClusterEvent, 3000);
    }
    if (FAILED(lightClusterAlloc[slot]->Reset()) ||
        FAILED(lightClusterList->Reset(lightClusterAlloc[slot].Get(), nullptr))) {
        return false;
    }

    // Build LH view-space projection parameters from live camera constants
    const float aspect = (camera.valid && camera.aspect > 0.1f && std::isfinite(camera.aspect))
        ? camera.aspect
        : (static_cast<float>(std::max(cfg.width, 1u)) / static_cast<float>(std::max(cfg.height, 1u)));
    const float fovY = (camera.valid && camera.tanHalfFovY > 0.05f && std::isfinite(camera.tanHalfFovY))
        ? (2.0f * std::atan(camera.tanHalfFovY))
        : 1.134464f; // 65 deg fallback
    const float nearZ = (camera.valid && camera.nearZ > 0.1f && std::isfinite(camera.nearZ)) ? camera.nearZ : 15.0f;
    const float farZ  = (camera.valid && camera.farZ > nearZ + 10.0f && std::isfinite(camera.farZ))
        ? std::min(camera.farZ, 40000.0f) : 40000.0f;
    const float zSign = (camera.valid && camera.proj[14] < 0.0f) ? -1.0f : 1.0f;

    LightClusterViewParams vp{};
    vp.nearZ = nearZ;
    vp.farZ  = farZ;
    BuildPerspectiveInvProjLH(fovY, aspect, nearZ, farZ, vp.invProj);

    std::vector<GpuClusterLight> gpuLights;
    const uint32_t srcCount = std::min(lighting.lightCount, std::min(kMaxSceneLights, RawLightClusterHost::kMaxLights));
    gpuLights.reserve(srcCount);

    for (uint32_t i = 0u; i < srcCount; ++i) {
        const SceneLight& l = lighting.lights[i];
        if (!std::isfinite(l.radius) || l.radius <= 1.0e-3f) continue;

        float vx = l.position[0];
        float vy = l.position[1];
        float vz = l.position[2];
        if (lighting.hasCamera) {
            vx = lighting.worldToView[0][0] * l.position[0] + lighting.worldToView[0][1] * l.position[1] +
                 lighting.worldToView[0][2] * l.position[2] + lighting.worldToView[0][3];
            vy = lighting.worldToView[1][0] * l.position[0] + lighting.worldToView[1][1] * l.position[1] +
                 lighting.worldToView[1][2] * l.position[2] + lighting.worldToView[1][3];
            vz = (lighting.worldToView[2][0] * l.position[0] + lighting.worldToView[2][1] * l.position[1] +
                  lighting.worldToView[2][2] * l.position[2] + lighting.worldToView[2][3]) * zSign;
        }
        if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) continue;

        GpuClusterLight gl{};
        gl.posView[0]   = vx;
        gl.posView[1]   = vy;
        gl.posView[2]   = vz;
        gl.radius       = l.radius;
        gl.dirView[0]   = 0.0f;
        gl.dirView[1]   = 0.0f;
        gl.dirView[2]   = 1.0f;
        gl.cosOuterCone = -1.0f;
        gl.colorRgb[0]  = std::isfinite(l.colour[0]) ? std::max(0.0f, l.colour[0]) : 0.0f;
        gl.colorRgb[1]  = std::isfinite(l.colour[1]) ? std::max(0.0f, l.colour[1]) : 0.0f;
        gl.colorRgb[2]  = std::isfinite(l.colour[2]) ? std::max(0.0f, l.colour[2]) : 0.0f;

        if (gl.colorRgb[0] > gl.colorRgb[1] * 1.25f && gl.colorRgb[0] > gl.colorRgb[2] * 2.0f) {
            gl.typeFlags = kClusterLightFire;
        } else if (gl.colorRgb[2] > gl.colorRgb[0] * 1.35f && gl.colorRgb[2] > 0.35f) {
            gl.typeFlags = kClusterLightSpell;
        } else {
            gl.typeFlags = kClusterLightPoint;
        }
        gpuLights.push_back(gl);
    }

    lastClusterLightCount = static_cast<uint32_t>(gpuLights.size());
    lightClusters->BuildAndAssign(lightClusterList.Get(),
                                  gpuLights.empty() ? nullptr : gpuLights.data(),
                                  lastClusterLightCount, vp);

    if (FAILED(lightClusterList->Close())) return false;
    ID3D12CommandList* lists[1] = {lightClusterList.Get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(lightClusterFence.Get(), ++lightClusterFenceVal);
    lightClusterSlotVal[slot] = lightClusterFenceVal;
    lightClusterSlot = (slot + 1u) % kRing;

    if ((lightClusterFrames++ % 180ull) == 0ull) {
        log("[LightClusters] live 16x9x24 grid: lights=%u, buildMs=%.4f, assignMs=%.4f",
            lastClusterLightCount,
            lightClusters->HasValidTiming() ? lightClusters->GetLastBuildMs() : 0.0f,
            lightClusters->HasValidTiming() ? lightClusters->GetLastAssignMs() : 0.0f);
    }
    return true;
}

bool D3D12Bridge::Impl::ExecuteGpuCullOnQueue()
{
    if (!gpuCullReady || !gpuCull || !gpuCull->Ready() || !queue || !cullList || !cullFence || !cullEvent ||
        !depthIn.res12 || !cullInstUploadBuf || !cullInstGpuBuf || !cullInstUploadMapped ||
        cfg.width == 0u || cfg.height == 0u) {
        return false;
    }

    const uint32_t slot = cullSlot;
    if (cullSlotVal[slot] > 0 && cullFence->GetCompletedValue() < cullSlotVal[slot]) {
        cullFence->SetEventOnCompletion(cullSlotVal[slot], cullEvent);
        WaitForSingleObject(cullEvent, 3000);
    }
    if (FAILED(cullAlloc[slot]->Reset()) || FAILED(cullList->Reset(cullAlloc[slot].Get(), nullptr))) {
        return false;
    }

    constexpr uint32_t kMaxBridgeCullInst = 512u;
    auto* dst = reinterpret_cast<GpuCullInstance*>(
        cullInstUploadMapped + static_cast<size_t>(slot) * kMaxBridgeCullInst * sizeof(GpuCullInstance));
    uint32_t instCount = 0u;

    // 1. Add active scene light volumes (camera-relative coordinates matching lighting.worldToClip)
    for (uint32_t i = 0u; i < lighting.lightCount && instCount < kMaxBridgeCullInst; ++i) {
        const SceneLight& l = lighting.lights[i];
        const float r = std::clamp(l.radius, 16.0f, 1600.0f);
        GpuCullInstance& g = dst[instCount];
        g.center[0]     = l.position[0];
        g.center[1]     = l.position[1];
        g.center[2]     = l.position[2];
        g.radius        = r;
        g.aabbMin[0]    = l.position[0] - r;
        g.aabbMin[1]    = l.position[1] - r;
        g.aabbMin[2]    = l.position[2] - r;
        g.aabbMax[0]    = l.position[0] + r;
        g.aabbMax[1]    = l.position[1] + r;
        g.aabbMax[2]    = l.position[2] + r;
        g.maxDrawDist   = 12000.0f;
        g.meshletOffset = instCount * 4u;
        g.meshletCount  = 4u;
        g.instanceId    = instCount;
        g.lodBiasFlags  = 0u;
        g.pad0          = 0u;
        ++instCount;
    }

    // 2. Add streamed frontier terrain chunk bounding volumes (camera-relative)
    const uint32_t chunkEntries = static_cast<uint32_t>(streamedTerrainGpu.size());
    uint32_t chunkIdx = 0u;
    for (const auto& kv : streamedTerrainGpu) {
        if (instCount >= kMaxBridgeCullInst) break;
        const float angle = static_cast<float>(chunkIdx) * (6.2831853f / std::max(1.0f, static_cast<float>(chunkEntries)));
        const float dist  = 2048.0f + static_cast<float>(chunkIdx % 4u) * 1024.0f;
        const float cx    = std::cos(angle) * dist;
        const float cy    = std::sin(angle) * dist;
        const float cz    = -128.0f;
        GpuCullInstance& g = dst[instCount];
        g.center[0]     = cx;
        g.center[1]     = cy;
        g.center[2]     = cz;
        g.radius        = 2048.0f;
        g.aabbMin[0]    = cx - 1024.0f;
        g.aabbMin[1]    = cy - 1024.0f;
        g.aabbMin[2]    = cz - 512.0f;
        g.aabbMax[0]    = cx + 1024.0f;
        g.aabbMax[1]    = cy + 1024.0f;
        g.aabbMax[2]    = cz + 512.0f;
        g.maxDrawDist   = 24000.0f;
        g.meshletOffset = instCount * 16u;
        g.meshletCount  = std::max(1u, kv.second.meshletCount);
        g.instanceId    = instCount;
        g.lodBiasFlags  = 0u;
        g.pad0          = 0u;
        ++instCount;
        ++chunkIdx;
    }

    // 3. Add 64 deterministic world-space probe volumes around the camera so live Hi-Z + Frustum culling
    //    always exercises real occlusion & frustum rejection even inside Tamriel interiors/exteriors.
    constexpr uint32_t kProbeRingCount = 64u;
    for (uint32_t p = 0u; p < kProbeRingCount && instCount < kMaxBridgeCullInst; ++p) {
        const float az   = static_cast<float>(p) * (6.2831853f / static_cast<float>(kProbeRingCount));
        const float dist = 256.0f + static_cast<float>(p % 8u) * 320.0f;
        const float cx   = std::cos(az) * dist;
        const float cy   = std::sin(az) * dist;
        const float cz   = -64.0f + static_cast<float>((p / 8u) % 4u) * 48.0f;
        const float half = 48.0f;
        GpuCullInstance& g = dst[instCount];
        g.center[0]     = cx;
        g.center[1]     = cy;
        g.center[2]     = cz;
        g.radius        = half * 1.73205f;
        g.aabbMin[0]    = cx - half;
        g.aabbMin[1]    = cy - half;
        g.aabbMin[2]    = cz - half;
        g.aabbMax[0]    = cx + half;
        g.aabbMax[1]    = cy + half;
        g.aabbMax[2]    = cz + half;
        g.maxDrawDist   = 8192.0f;
        g.meshletOffset = instCount * 2u;
        g.meshletCount  = 2u;
        g.instanceId    = instCount;
        g.lodBiasFlags  = 0u;
        g.pad0          = 0u;
        ++instCount;
    }

    // Copy active instances into default-heap GPU buffer
    if (cullInstGpuState != D3D12_RESOURCE_STATE_COPY_DEST) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = cullInstGpuBuf.Get();
        b.Transition.StateBefore = cullInstGpuState;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cullList->ResourceBarrier(1u, &b);
        cullInstGpuState = D3D12_RESOURCE_STATE_COPY_DEST;
    }
    const uint64_t uploadOff = static_cast<uint64_t>(slot) * kMaxBridgeCullInst * sizeof(GpuCullInstance);
    const uint64_t copyBytes = static_cast<uint64_t>(instCount) * sizeof(GpuCullInstance);
    cullList->CopyBufferRegion(cullInstGpuBuf.Get(), 0ull, cullInstUploadBuf.Get(), uploadOff, copyBytes);
    {
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = cullInstGpuBuf.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cullList->ResourceBarrier(1u, &b);
        cullInstGpuState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    // Build conservative Hi-Z pyramid from shared SceneDepth (depthIn.res12 is in COMMON between passes)
    gpuCull->BuildHiZPyramid(cullList.Get(), depthIn.res12.Get(), cfg.width, cfg.height,
                             D3D12_RESOURCE_STATE_COMMON);

    GpuCullViewParams vp{};
    std::memcpy(vp.viewProj, &lighting.worldToClip[0][0], sizeof(vp.viewProj));
    vp.autoExtractPlanes        = true;
    vp.cameraPosW[0]            = 0.0f; // lighting.worldToClip is camera-relative
    vp.cameraPosW[1]            = 0.0f;
    vp.cameraPosW[2]            = 0.0f;
    vp.depthBias                = 1.0e-4f;
    vp.defaultIndexCountPerInst = 36u;
    vp.enableFrustumCull        = true;
    vp.enableDistanceCull       = true;
    vp.enableHiZCull            = true;

    gpuCull->CullInstances(cullList.Get(), cullInstGpuBuf.Get(), instCount, vp,
                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    if (FAILED(cullList->Close())) return false;
    ID3D12CommandList* lists[1] = {cullList.Get()};
    queue->ExecuteCommandLists(1u, lists);
    queue->Signal(cullFence.Get(), ++cullFenceVal);
    cullSlotVal[slot] = cullFenceVal;
    cullSlot = (slot + 1u) % kRing;
    lastGpuCullInstances = instCount;

    if ((cullStepFrames++ % 180ull) == 0ull) {
        log("[GpuCull] live GPU Hi-Z (%ux%u, %u mips) + cull (%u inst): hizMs=%.4f, cullMs=%.4f",
            gpuCull->HiZWidth(), gpuCull->HiZHeight(), gpuCull->HiZMipCount(), instCount,
            gpuCull->HasValidHiZTiming() ? gpuCull->GetLastHiZMs() : 0.0f,
            gpuCull->HasValidCullTiming() ? gpuCull->GetLastCullMs() : 0.0f);
    }
    return true;
}

bool D3D12Bridge::Impl::ExecuteVolFroxelsOnQueue(bool isInterior)
{
    if (!volFroxelsReady || !volFroxels || !volFroxels->Ready() || !queue ||
        !froxelList || !froxelFence || !froxelEvent) {
        return false;
    }

    const uint32_t slot = froxelSlot;
    if (froxelSlotVal[slot] > 0 && froxelFence->GetCompletedValue() < froxelSlotVal[slot]) {
        froxelFence->SetEventOnCompletion(froxelSlotVal[slot], froxelEvent);
        WaitForSingleObject(froxelEvent, 3000);
    }
    if (FAILED(froxelAlloc[slot]->Reset()) || FAILED(froxelList->Reset(froxelAlloc[slot].Get(), nullptr))) {
        return false;
    }

    LightingFluidInputs fi{};
    LightingPassFluidInputs(pass.get(), &fi);

    float weatherSnow = 0.0f;
    const void* uPtr = nullptr;
    size_t uBytes = 0;
    if (pass && LightingPassUniform(pass.get(), &uPtr, &uBytes) &&
        uPtr != nullptr && uBytes >= sizeof(TelemetryLightUniformView)) {
        const auto* u = static_cast<const TelemetryLightUniformView*>(uPtr);
        if (std::isfinite(u->weather[1])) weatherSnow = std::clamp(u->weather[1], 0.0f, 1.0f);
    }

    // Derive camera forward vector directly from camera-relative clipToWorld center ray
    float camFwd[3] = {0.0f, 1.0f, 0.0f};
    {
        const float* inv = &lighting.clipToWorld[0][0];
        const float wNear = inv[3 * 4 + 3];
        const float wFar  = inv[2 * 4 + 3] + inv[3 * 4 + 3];
        if (std::fabs(wNear) > 1.0e-6f && std::fabs(wFar) > 1.0e-6f) {
            const float nx = inv[3 * 4 + 0] / wNear;
            const float ny = inv[3 * 4 + 1] / wNear;
            const float nz = inv[3 * 4 + 2] / wNear;
            const float fx = (inv[2 * 4 + 0] + inv[3 * 4 + 0]) / wFar;
            const float fy = (inv[2 * 4 + 1] + inv[3 * 4 + 1]) / wFar;
            const float fz = (inv[2 * 4 + 2] + inv[3 * 4 + 2]) / wFar;
            const float dx = fx - nx, dy = fy - ny, dz = fz - nz;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (len > 1.0e-5f) {
                camFwd[0] = dx / len;
                camFwd[1] = dy / len;
                camFwd[2] = dz / len;
            }
        }
    }

    VolumetricFroxelParams fp{};
    std::memcpy(fp.invViewProj, &lighting.clipToWorld[0][0], sizeof(fp.invViewProj));
    if (froxelHasPrev) {
        std::memcpy(fp.prevViewProj, froxelPrevViewProj, sizeof(fp.prevViewProj));
        fp.prevCameraForwardW[0] = froxelPrevCamFwd[0];
        fp.prevCameraForwardW[1] = froxelPrevCamFwd[1];
        fp.prevCameraForwardW[2] = froxelPrevCamFwd[2];
    } else {
        std::memcpy(fp.prevViewProj, &lighting.worldToClip[0][0], sizeof(fp.prevViewProj));
        fp.prevCameraForwardW[0] = camFwd[0];
        fp.prevCameraForwardW[1] = camFwd[1];
        fp.prevCameraForwardW[2] = camFwd[2];
    }
    fp.cameraPosW[0]         = 0.0f; // lighting.clipToWorld / worldToClip are camera-relative
    fp.cameraPosW[1]         = 0.0f;
    fp.cameraPosW[2]         = 0.0f;
    fp.cameraForwardW[0]     = camFwd[0];
    fp.cameraForwardW[1]     = camFwd[1];
    fp.cameraForwardW[2]     = camFwd[2];
    fp.prevCameraPosW[0]     = 0.0f;
    fp.prevCameraPosW[1]     = 0.0f;
    fp.prevCameraPosW[2]     = 0.0f;
    fp.nearZ                 = 16.0f;
    fp.farZ                  = isInterior ? 4096.0f : 16384.0f;
    fp.simTimeSec            = fi.simTimeSec;
    fp.historyWeight         = froxelHasPrev ? 0.85f : 0.0f;
    fp.enableTemporal        = froxelHasPrev;
    fp.enableHaltonJitter    = true;
    fp.baseFogDensity        = isInterior ? 0.00018f : 0.00028f;
    fp.valleyFloorZ          = isInterior ? -256.0f : std::clamp(fi.waterPlaneZ - lighting.posAdjust[2], -4096.0f, 4096.0f);
    fp.fogScaleHeight        = isInterior ? 640.0f : 1800.0f;
    fp.scatterAlbedo         = 0.90f;
    fp.rainIntensity         = isInterior ? 0.0f : std::clamp(fi.rainIntensity, 0.0f, 1.0f);
    fp.snowIntensity         = isInterior ? 0.0f : weatherSnow;
    fp.noiseAmp              = 0.25f;
    fp.noiseScale            = 0.0018f;
    fp.windVelW[0]           = isInterior ? 0.0f : std::clamp(fi.windXY[0] * 0.04f, -12.0f, 12.0f);
    fp.windVelW[1]           = isInterior ? 0.0f : std::clamp(fi.windXY[1] * 0.04f, -12.0f, 12.0f);
    fp.windVelW[2]           = 0.0f;
    fp.mieAnisotropyG        = 0.68f;
    fp.sunDirW[0]            = lighting.sunDirection[0];
    fp.sunDirW[1]            = lighting.sunDirection[1];
    fp.sunDirW[2]            = lighting.sunDirection[2];
    fp.sunIntensity          = (lighting.hasSun && !isInterior) ? 5.5f : 0.35f;
    fp.sunColor[0]           = lighting.sunColour[0];
    fp.sunColor[1]           = lighting.sunColour[1];
    fp.sunColor[2]           = lighting.sunColour[2];
    fp.ambientIntensity      = isInterior ? 0.18f : 0.32f;
    fp.ambientColor[0]       = 0.35f;
    fp.ambientColor[1]       = 0.48f;
    fp.ambientColor[2]       = 0.72f;

    uint32_t lc = 0u;
    for (uint32_t i = 0u; i < lighting.lightCount && lc < 16u; ++i) {
        const SceneLight& l = lighting.lights[i];
        if (l.radius < 24.0f) continue;
        FroxelPointLight& pl = fp.lights[lc++];
        pl.posW[0]   = l.position[0];
        pl.posW[1]   = l.position[1];
        pl.posW[2]   = l.position[2];
        pl.radius    = std::clamp(l.radius, 32.0f, 2400.0f);
        pl.color[0]  = std::max(0.0f, l.colour[0]);
        pl.color[1]  = std::max(0.0f, l.colour[1]);
        pl.color[2]  = std::max(0.0f, l.colour[2]);
        pl.intensity = std::clamp(l.radius * 0.6f, 24.0f, 600.0f);
    }
    fp.lightCount = lc;

    volFroxels->Execute(froxelList.Get(), fp);

    if (FAILED(froxelList->Close())) return false;
    ID3D12CommandList* lists[1] = {froxelList.Get()};
    queue->ExecuteCommandLists(1u, lists);
    queue->Signal(froxelFence.Get(), ++froxelFenceVal);
    froxelSlotVal[slot] = froxelFenceVal;
    froxelSlot = (slot + 1u) % kRing;

    std::memcpy(froxelPrevViewProj, &lighting.worldToClip[0][0], sizeof(froxelPrevViewProj));
    froxelPrevCamFwd[0] = camFwd[0];
    froxelPrevCamFwd[1] = camFwd[1];
    froxelPrevCamFwd[2] = camFwd[2];
    froxelHasPrev = true;

    if ((froxelStepFrames++ % 180ull) == 0ull) {
        log("[VolFroxels] live 160x90x64 step: lights=%u, injectMs=%.4f, integrateMs=%.4f",
            lc,
            volFroxels->HasValidTiming() ? volFroxels->GetLastInjectMs() : 0.0f,
            volFroxels->HasValidTiming() ? volFroxels->GetLastIntegrateMs() : 0.0f);
    }
    return true;
}

bool D3D12Bridge::Impl::ExecuteShadowPcssOnQueue(bool hasShadows)
{
    if (!shadowPcssReady || !shadowPcss || !shadowPcss->Ready() ||
        !queue || !shadowPcssList || !shadowPcssFence || !shadowPcssEvent ||
        !depthIn.res12 || !shadowPcssOut || shadowPcssOutW == 0u || shadowPcssOutH == 0u) {
        return false;
    }

    const uint32_t slot = shadowPcssSlot;
    if (shadowPcssSlotVal[slot] > 0 && shadowPcssFence->GetCompletedValue() < shadowPcssSlotVal[slot]) {
        shadowPcssFence->SetEventOnCompletion(shadowPcssSlotVal[slot], shadowPcssEvent);
        WaitForSingleObject(shadowPcssEvent, 3000);
    }
    if (FAILED(shadowPcssAlloc[slot]->Reset()) ||
        FAILED(shadowPcssList->Reset(shadowPcssAlloc[slot].Get(), nullptr))) {
        return false;
    }

    ID3D12Resource* smRes = (hasShadows && shared[static_cast<UINT>(Shared::ShadowAtlas)].res12)
        ? shared[static_cast<UINT>(Shared::ShadowAtlas)].res12.Get()
        : shadowPcssFallbackMap.Get();
    if (!smRes) {
        shadowPcssList->Close();
        return false;
    }

    const D3D12_RESOURCE_DESC smDesc = smRes->GetDesc();
    const uint32_t smSize = static_cast<uint32_t>(std::max<UINT64>(smDesc.Height, 64ull));

    float sunDir[3] = {
        std::isfinite(lighting.sunDirection[0]) ? lighting.sunDirection[0] : 0.35f,
        std::isfinite(lighting.sunDirection[1]) ? lighting.sunDirection[1] : 0.25f,
        std::isfinite(lighting.sunDirection[2]) ? lighting.sunDirection[2] : 0.90f
    };
    const float camRelTarget[3] = {0.0f, 0.0f, 0.0f};
    const TexelSnappedCascade cascade =
        BuildTexelSnappedCascade(camRelTarget, sunDir, 2048.0f, 8192.0f, smSize);

    ShadowPcssParams params{};
    // lighting.clipToWorld stores rows dotting column vectors; transpose for row-vector mul(v, M)
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            params.invViewProj[r * 4 + c] = lighting.clipToWorld[c][r];
        }
    }
    std::memcpy(params.lightViewProj, cascade.lightViewProj, sizeof(params.lightViewProj));
    params.sunDirW[0]          = sunDir[0];
    params.sunDirW[1]          = sunDir[1];
    params.sunDirW[2]          = sunDir[2];
    params.baseDepthBias       = 4.0e-4f;
    params.slopeDepthBias      = 8.0e-4f;
    params.normalOffsetWorld   = 1.5f;
    params.lightAngularSize    = 14.0f;
    params.frameJitter         = static_cast<float>(shadowPcssFrames & 63ull);
    params.blockerSearchTexels = 14.0f;
    params.minFilterTexels     = 0.75f;
    params.maxFilterTexels     = 22.0f;
    params.shadowMapSize       = smSize;

    shadowPcss->Execute(shadowPcssList.Get(), depthIn.res12.Get(), smRes, shadowPcssOut.Get(),
                        shadowPcssOutW, shadowPcssOutH, params,
                        D3D12_RESOURCE_STATE_COMMON,
                        D3D12_RESOURCE_STATE_COMMON,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (FAILED(shadowPcssList->Close())) return false;
    ID3D12CommandList* lists[1] = {shadowPcssList.Get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(shadowPcssFence.Get(), ++shadowPcssFenceVal);
    shadowPcssSlotVal[slot] = shadowPcssFenceVal;
    shadowPcssSlot = (slot + 1u) % kRing;

    if ((shadowPcssFrames++ % 180ull) == 0ull) {
        log("[ShadowPcss] live half-res %ux%u PCSS: pcssMs=%.4f, shadowMap=%ux%u (%s)",
            shadowPcssOutW, shadowPcssOutH,
            shadowPcss->HasValidTiming() ? shadowPcss->GetLastPcssMs() : 0.0f,
            static_cast<unsigned>(smDesc.Width), static_cast<unsigned>(smDesc.Height),
            hasShadows ? "ShadowAtlas" : "fallback");
    }
    return true;
}

bool D3D12Bridge::Impl::ExecuteGtaoOnQueue()
{
    if (!gtaoHostReady || !gtaoHost || !gtaoHost->Ready() ||
        !queue || !gtaoList || !gtaoFence || !gtaoEvent || !depthIn.res12) {
        return false;
    }

    const uint32_t slot = gtaoSlot;
    if (gtaoSlotVal[slot] > 0 && gtaoFence->GetCompletedValue() < gtaoSlotVal[slot]) {
        gtaoFence->SetEventOnCompletion(gtaoSlotVal[slot], gtaoEvent);
        WaitForSingleObject(gtaoEvent, 3000);
    }
    if (FAILED(gtaoAlloc[slot]->Reset()) ||
        FAILED(gtaoList->Reset(gtaoAlloc[slot].Get(), nullptr))) {
        return false;
    }

    // Derive vertical FOV directly from camera-relative clipToWorld top/bottom center rays.
    float fovYDeg = 65.0f;
    {
        const float* inv = &lighting.clipToWorld[0][0];
        auto rayDir = [&](float ndcY, float out[3]) -> bool {
            const float wN = ndcY * inv[1 * 4 + 3] + inv[3 * 4 + 3];
            const float wF = ndcY * inv[1 * 4 + 3] + inv[2 * 4 + 3] + inv[3 * 4 + 3];
            if (std::fabs(wN) <= 1.0e-6f || std::fabs(wF) <= 1.0e-6f) return false;
            const float nx = (ndcY * inv[1 * 4 + 0] + inv[3 * 4 + 0]) / wN;
            const float ny = (ndcY * inv[1 * 4 + 1] + inv[3 * 4 + 1]) / wN;
            const float nz = (ndcY * inv[1 * 4 + 2] + inv[3 * 4 + 2]) / wN;
            const float fx = (ndcY * inv[1 * 4 + 0] + inv[2 * 4 + 0] + inv[3 * 4 + 0]) / wF;
            const float fy = (ndcY * inv[1 * 4 + 1] + inv[2 * 4 + 1] + inv[3 * 4 + 1]) / wF;
            const float fz = (ndcY * inv[1 * 4 + 2] + inv[2 * 4 + 2] + inv[3 * 4 + 2]) / wF;
            const float dx = fx - nx, dy = fy - ny, dz = fz - nz;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (!std::isfinite(len) || len <= 1.0e-5f) return false;
            out[0] = dx / len;
            out[1] = dy / len;
            out[2] = dz / len;
            return true;
        };
        float topD[3]{}, botD[3]{};
        if (rayDir(1.0f, topD) && rayDir(-1.0f, botD)) {
            const float dotTB = std::clamp(topD[0] * botD[0] + topD[1] * botD[1] + topD[2] * botD[2], -1.0f, 1.0f);
            const float deg = std::acos(dotTB) * 57.2957795f;
            if (std::isfinite(deg) && deg >= 25.0f && deg <= 130.0f) {
                fovYDeg = std::clamp(deg, 30.0f, 120.0f);
            }
        }
    }

    const uint32_t outW = std::max(cfg.width / 2u, 16u);
    const uint32_t outH = std::max(cfg.height / 2u, 16u);

    GtaoParams gp{};
    gp.fovYDeg           = fovYDeg;
    gp.nearZ             = 16.0f;
    gp.farZ              = 16384.0f;
    gp.effectRadius      = 96.0f; // ~1.4 m in Skyrim world units
    gp.falloffStart      = 0.65f;
    gp.aoPower           = 1.0f;
    gp.maxScreenRadiusPx = 64.0f;
    gp.depthEdgeScale    = 0.025f;
    gp.numSlices         = 2u;
    gp.numSteps          = 6u;
    gp.temporalJitter    = static_cast<float>(gtaoStepFrames & 63ull);

    gtaoHost->Execute(gtaoList.Get(), depthIn.res12.Get(), outW, outH, gp,
                      D3D12_RESOURCE_STATE_COMMON);

    if (FAILED(gtaoList->Close())) return false;
    ID3D12CommandList* lists[1] = {gtaoList.Get()};
    queue->ExecuteCommandLists(1u, lists);
    queue->Signal(gtaoFence.Get(), ++gtaoFenceVal);
    gtaoSlotVal[slot] = gtaoFenceVal;
    gtaoSlot = (slot + 1u) % kRing;

    if ((gtaoStepFrames++ % 180ull) == 0ull) {
        log("[Gtao] live half-res %ux%u GTAO+BentNormal: mainMs=%.4f, denoiseMs=%.4f, fovY=%.1f",
            outW, outH,
            gtaoHost->HasValidTiming() ? gtaoHost->GetLastMainMs() : 0.0f,
            gtaoHost->HasValidTiming() ? gtaoHost->GetLastDenoiseMs() : 0.0f,
            fovYDeg);
    }
    return true;
}

bool D3D12Bridge::Impl::ExecuteSsrOnQueue()
{
    if (!ssrHostReady || !ssrHost || !ssrHost->Ready() ||
        !queue || !ssrList || !ssrFence || !ssrEvent || !colorOut.res12 || !depthIn.res12) {
        return false;
    }

    const uint32_t slot = ssrSlot;
    if (ssrSlotVal[slot] > 0 && ssrFence->GetCompletedValue() < ssrSlotVal[slot]) {
        ssrFence->SetEventOnCompletion(ssrSlotVal[slot], ssrEvent);
        WaitForSingleObject(ssrEvent, 3000);
    }
    if (FAILED(ssrAlloc[slot]->Reset()) ||
        FAILED(ssrList->Reset(ssrAlloc[slot].Get(), nullptr))) {
        return false;
    }

    // Derive vertical FOV directly from camera-relative clipToWorld top/bottom center rays.
    float fovYDeg = 65.0f;
    {
        const float* inv = &lighting.clipToWorld[0][0];
        auto rayDir = [&](float ndcY, float out[3]) -> bool {
            const float wN = ndcY * inv[1 * 4 + 3] + inv[3 * 4 + 3];
            const float wF = ndcY * inv[1 * 4 + 3] + inv[2 * 4 + 3] + inv[3 * 4 + 3];
            if (std::fabs(wN) <= 1.0e-6f || std::fabs(wF) <= 1.0e-6f) return false;
            const float nx = (ndcY * inv[1 * 4 + 0] + inv[3 * 4 + 0]) / wN;
            const float ny = (ndcY * inv[1 * 4 + 1] + inv[3 * 4 + 1]) / wN;
            const float nz = (ndcY * inv[1 * 4 + 2] + inv[3 * 4 + 2]) / wN;
            const float fx = (ndcY * inv[1 * 4 + 0] + inv[2 * 4 + 0] + inv[3 * 4 + 0]) / wF;
            const float fy = (ndcY * inv[1 * 4 + 1] + inv[2 * 4 + 1] + inv[3 * 4 + 1]) / wF;
            const float fz = (ndcY * inv[1 * 4 + 2] + inv[2 * 4 + 2] + inv[3 * 4 + 2]) / wF;
            const float dx = fx - nx, dy = fy - ny, dz = fz - nz;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (!std::isfinite(len) || len <= 1.0e-5f) return false;
            out[0] = dx / len;
            out[1] = dy / len;
            out[2] = dz / len;
            return true;
        };
        float topD[3]{}, botD[3]{};
        if (rayDir(1.0f, topD) && rayDir(-1.0f, botD)) {
            const float dotTB = std::clamp(topD[0] * botD[0] + topD[1] * botD[1] + topD[2] * botD[2], -1.0f, 1.0f);
            const float deg = std::acos(dotTB) * 57.2957795f;
            if (std::isfinite(deg) && deg >= 25.0f && deg <= 130.0f) {
                fovYDeg = std::clamp(deg, 30.0f, 120.0f);
            }
        }
    }

    const uint32_t outW = std::max(cfg.width / 2u, 16u);
    const uint32_t outH = std::max(cfg.height / 2u, 16u);
    const float aspect  = static_cast<float>(outW) / static_cast<float>(outH);
    const float fovYRad = fovYDeg * (3.14159265359f / 180.0f);

    SsrParams sp{};
    BuildSsrPerspectiveLH(fovYRad, aspect, 16.0f, 16384.0f, sp.proj, sp.invProj);
    sp.maxRayDist        = 3072.0f;
    sp.thicknessM        = 96.0f;
    sp.nearZ             = 16.0f;
    sp.startBias         = 2.0f;
    sp.fallbackRoughness = 0.08f;
    sp.maxRoughness      = 0.72f;
    sp.edgeFadeWidth     = 0.07f;
    sp.frameJitter       = static_cast<float>(ssrStepFrames & 63ull);
    sp.maxSteps          = 40u;

    ssrHost->Execute(ssrList.Get(), colorOut.res12.Get(), depthIn.res12.Get(), outW, outH, sp,
                     D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);

    if (FAILED(ssrList->Close())) return false;
    ID3D12CommandList* lists[1] = {ssrList.Get()};
    queue->ExecuteCommandLists(1u, lists);
    queue->Signal(ssrFence.Get(), ++ssrFenceVal);
    ssrSlotVal[slot] = ssrFenceVal;
    ssrSlot = (slot + 1u) % kRing;

    if ((ssrStepFrames++ % 180ull) == 0ull) {
        log("[Ssr] live half-res %ux%u GGX SSR: traceMs=%.4f, fovY=%.1f",
            outW, outH,
            ssrHost->HasValidTiming() ? ssrHost->GetLastSsrMs() : 0.0f,
            fovYDeg);
    }
    return true;
}

bool D3D12Bridge::Impl::ExecuteAutoExposureOnQueue()
{
    if (!autoExposureReady || !autoExposure || !autoExposure->Ready() ||
        !queue || !autoExposureList || !autoExposureFence || !autoExposureEvent || !colorOut.res12) {
        return false;
    }

    const uint32_t slot = autoExposureSlot;
    if (autoExposureSlotVal[slot] > 0 && autoExposureFence->GetCompletedValue() < autoExposureSlotVal[slot]) {
        autoExposureFence->SetEventOnCompletion(autoExposureSlotVal[slot], autoExposureEvent);
        WaitForSingleObject(autoExposureEvent, 3000);
    }
    if (FAILED(autoExposureAlloc[slot]->Reset()) ||
        FAILED(autoExposureList->Reset(autoExposureAlloc[slot].Get(), nullptr))) {
        return false;
    }

    const uint64_t nowMs = GetTickCount64();
    float dtSec = 1.0f / 60.0f;
    if (autoExposureLastMs > 0ull && nowMs >= autoExposureLastMs) {
        dtSec = std::clamp(static_cast<float>(nowMs - autoExposureLastMs) * 0.001f, 0.001f, 0.1f);
    }
    autoExposureLastMs = nowMs;

    AutoExposureParams ap{};
    ap.dtSec      = dtSec;
    ap.resetAdapt = (autoExposureStepFrames == 0ull);

    autoExposure->Execute(autoExposureList.Get(), colorOut.res12.Get(), cfg.width, cfg.height, ap,
                          D3D12_RESOURCE_STATE_COMMON);

    if (FAILED(autoExposureList->Close())) return false;
    ID3D12CommandList* lists[1] = {autoExposureList.Get()};
    queue->ExecuteCommandLists(1u, lists);
    queue->Signal(autoExposureFence.Get(), ++autoExposureFenceVal);
    autoExposureSlotVal[slot] = autoExposureFenceVal;
    autoExposureSlot = (slot + 1u) % kRing;

    if ((autoExposureStepFrames++ % 180ull) == 0ull) {
        log("[AutoExposure] live %ux%u 256-bin histogram+adapt: histMs=%.4f, adaptMs=%.4f, dt=%.3fs",
            static_cast<unsigned>(cfg.width), static_cast<unsigned>(cfg.height),
            autoExposure->HasValidTiming() ? autoExposure->GetLastHistMs() : 0.0f,
            autoExposure->HasValidTiming() ? autoExposure->GetLastAdaptMs() : 0.0f,
            dtSec);
    }
    return true;
}

bool D3D12Bridge::Impl::ExecuteCloudRaymarchOnQueue(bool isInterior)
{
    if (!cloudRaymarchReady || !cloudRaymarch || !cloudRaymarch->Ready() ||
        !queue || !cloudList || !cloudFence || !cloudEvent) {
        return false;
    }

    const uint32_t slot = cloudSlot;
    if (cloudSlotVal[slot] > 0 && cloudFence->GetCompletedValue() < cloudSlotVal[slot]) {
        cloudFence->SetEventOnCompletion(cloudSlotVal[slot], cloudEvent);
        WaitForSingleObject(cloudEvent, 3000);
    }
    if (FAILED(cloudAlloc[slot]->Reset()) ||
        FAILED(cloudList->Reset(cloudAlloc[slot].Get(), nullptr))) {
        return false;
    }

    if (isInterior != cloudPrevInterior) {
        cloudRaymarch->ResetHistory();
        cloudPrevInterior = isInterior;
    }

    LightingFluidInputs fi{};
    LightingPassFluidInputs(pass.get(), &fi);

    float cloudCov = 0.55f;
    float weatherSnow = 0.0f;
    const void* uPtr = nullptr;
    size_t uBytes = 0;
    if (pass && LightingPassUniform(pass.get(), &uPtr, &uBytes) &&
        uPtr != nullptr && uBytes >= sizeof(TelemetryLightUniformView)) {
        const auto* u = static_cast<const TelemetryLightUniformView*>(uPtr);
        if (std::isfinite(u->clouds[0]) && u->clouds[0] > 0.0f) {
            cloudCov = std::clamp(u->clouds[0], 0.0f, 1.0f);
        }
        if (std::isfinite(u->weather[1])) {
            weatherSnow = std::clamp(u->weather[1], 0.0f, 1.0f);
        }
    }
    const float precipBoost = std::clamp(std::max(fi.rainIntensity, weatherSnow) * 0.35f, 0.0f, 0.35f);
    cloudCov = isInterior ? 0.0f : std::clamp(cloudCov + precipBoost, 0.0f, 0.95f);

    // Derive orthonormal Y-up camera basis & vertical FOV from camera-relative clipToWorld (Skyrim Z-up -> Y-up).
    float fovYDeg = 65.0f;
    float camFwdYUp[3] = {0.0f, 0.0f, 1.0f};
    float camRightYUp[3] = {1.0f, 0.0f, 0.0f};
    float camUpYUp[3] = {0.0f, 1.0f, 0.0f};
    {
        const float* inv = &lighting.clipToWorld[0][0];
        auto rayDirZUp = [&](float ndcX, float ndcY, float out[3]) -> bool {
            const float wN = ndcX * inv[0 * 4 + 3] + ndcY * inv[1 * 4 + 3] + inv[3 * 4 + 3];
            const float wF = ndcX * inv[0 * 4 + 3] + ndcY * inv[1 * 4 + 3] + inv[2 * 4 + 3] + inv[3 * 4 + 3];
            if (std::fabs(wN) <= 1.0e-6f || std::fabs(wF) <= 1.0e-6f) return false;
            const float nx = (ndcX * inv[0 * 4 + 0] + ndcY * inv[1 * 4 + 0] + inv[3 * 4 + 0]) / wN;
            const float ny = (ndcX * inv[0 * 4 + 1] + ndcY * inv[1 * 4 + 1] + inv[3 * 4 + 1]) / wN;
            const float nz = (ndcX * inv[0 * 4 + 2] + ndcY * inv[1 * 4 + 2] + inv[3 * 4 + 2]) / wN;
            const float fx = (ndcX * inv[0 * 4 + 0] + ndcY * inv[1 * 4 + 0] + inv[2 * 4 + 0] + inv[3 * 4 + 0]) / wF;
            const float fy = (ndcX * inv[0 * 4 + 1] + ndcY * inv[1 * 4 + 1] + inv[2 * 4 + 1] + inv[3 * 4 + 1]) / wF;
            const float fz = (ndcX * inv[0 * 4 + 2] + ndcY * inv[1 * 4 + 2] + inv[2 * 4 + 2] + inv[3 * 4 + 2]) / wF;
            const float dx = fx - nx, dy = fy - ny, dz = fz - nz;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (!std::isfinite(len) || len <= 1.0e-5f) return false;
            out[0] = dx / len;
            out[1] = dy / len;
            out[2] = dz / len;
            return true;
        };
        float ctrD[3]{}, topD[3]{}, botD[3]{}, rgtD[3]{};
        if (rayDirZUp(0.0f, 0.0f, ctrD)) {
            camFwdYUp[0] = ctrD[0];
            camFwdYUp[1] = ctrD[2]; // Skyrim Z -> Y-up
            camFwdYUp[2] = ctrD[1]; // Skyrim Y -> Z-forward
        }
        if (rayDirZUp(0.0f, 1.0f, topD) && rayDirZUp(0.0f, -1.0f, botD)) {
            const float dotTB = std::clamp(topD[0] * botD[0] + topD[1] * botD[1] + topD[2] * botD[2], -1.0f, 1.0f);
            const float deg = std::acos(dotTB) * 57.2957795f;
            if (std::isfinite(deg) && deg >= 25.0f && deg <= 130.0f) {
                fovYDeg = std::clamp(deg, 30.0f, 120.0f);
            }
            const float uy0 = topD[0] - botD[0];
            const float uy1 = topD[2] - botD[2];
            const float uy2 = topD[1] - botD[1];
            const float uLen = std::sqrt(uy0 * uy0 + uy1 * uy1 + uy2 * uy2);
            if (std::isfinite(uLen) && uLen > 1.0e-5f) {
                camUpYUp[0] = uy0 / uLen;
                camUpYUp[1] = uy1 / uLen;
                camUpYUp[2] = uy2 / uLen;
            }
        }
        if (rayDirZUp(1.0f, 0.0f, rgtD)) {
            const float rx0 = rgtD[0] - ctrD[0];
            const float rx1 = rgtD[2] - ctrD[2];
            const float rx2 = rgtD[1] - ctrD[1];
            const float rLen = std::sqrt(rx0 * rx0 + rx1 * rx1 + rx2 * rx2);
            if (std::isfinite(rLen) && rLen > 1.0e-5f) {
                camRightYUp[0] = rx0 / rLen;
                camRightYUp[1] = rx1 / rLen;
                camRightYUp[2] = rx2 / rLen;
            }
        }
    }

    const uint32_t outW = std::max(cfg.width / 4u, 64u);
    const uint32_t outH = std::max(cfg.height / 4u, 36u);

    constexpr float kSkyrimUnitsToMeters = 0.0142857f;
    CloudRaymarchParams cp{};
    cp.camPos[0]     = std::isfinite(lighting.posAdjust[0]) ? lighting.posAdjust[0] * kSkyrimUnitsToMeters : 0.0f;
    cp.camPos[1]     = std::clamp(std::isfinite(lighting.posAdjust[2]) ? (lighting.posAdjust[2] * kSkyrimUnitsToMeters + 80.0f) : 80.0f, 10.0f, 1200.0f);
    cp.camPos[2]     = std::isfinite(lighting.posAdjust[1]) ? lighting.posAdjust[1] * kSkyrimUnitsToMeters : 0.0f;
    cp.camRight[0]   = camRightYUp[0]; cp.camRight[1]   = camRightYUp[1]; cp.camRight[2]   = camRightYUp[2];
    cp.camUp[0]      = camUpYUp[0];    cp.camUp[1]      = camUpYUp[1];    cp.camUp[2]      = camUpYUp[2];
    cp.camForward[0] = camFwdYUp[0];   cp.camForward[1] = camFwdYUp[1];   cp.camForward[2] = camFwdYUp[2];
    cp.fovYDeg       = fovYDeg;
    cp.timeSec       = std::isfinite(fi.simTimeSec) ? fi.simTimeSec : 0.0f;
    cp.cloudCoverage = cloudCov;
    cp.densityScale  = 0.0022f + precipBoost * 0.0018f;
    cp.anvilFactor   = 0.35f + precipBoost * 0.50f;
    cp.historyWeight = isInterior ? 0.0f : 0.75f;
    cp.windVel[0]    = isInterior ? 0.0f : std::clamp(fi.windXY[0] * 0.08f, -40.0f, 40.0f);
    cp.windVel[1]    = 0.0f;
    cp.windVel[2]    = isInterior ? 0.0f : std::clamp(fi.windXY[1] * 0.08f, -40.0f, 40.0f);
    cp.sunDir[0]     = std::isfinite(lighting.sunDirection[0]) ? lighting.sunDirection[0] : 0.25f;
    cp.sunDir[1]     = std::isfinite(lighting.sunDirection[2]) ? lighting.sunDirection[2] : 0.35f;
    cp.sunDir[2]     = std::isfinite(lighting.sunDirection[1]) ? lighting.sunDirection[1] : 0.90f;
    cp.sunColor[0]   = std::max(0.05f, std::isfinite(lighting.sunColour[0]) ? lighting.sunColour[0] : 1.0f);
    cp.sunColor[1]   = std::max(0.05f, std::isfinite(lighting.sunColour[1]) ? lighting.sunColour[1] : 0.94f);
    cp.sunColor[2]   = std::max(0.05f, std::isfinite(lighting.sunColour[2]) ? lighting.sunColour[2] : 0.84f);
    cp.sunIntensity  = lighting.hasSun ? 6.5f : 1.2f;
    cp.frameIndex    = static_cast<uint32_t>(cloudStepFrames & 0xFFFFu);

    cloudRaymarch->Execute(cloudList.Get(), outW, outH, cp);

    if (FAILED(cloudList->Close())) return false;
    ID3D12CommandList* lists[1] = {cloudList.Get()};
    queue->ExecuteCommandLists(1u, lists);
    queue->Signal(cloudFence.Get(), ++cloudFenceVal);
    cloudSlotVal[slot] = cloudFenceVal;
    cloudSlot = (slot + 1u) % kRing;

    if ((cloudStepFrames++ % 180ull) == 0ull) {
        log("[CloudRaymarch] live quarter-res %ux%u clouds: cloudMs=%.4f, cov=%.2f, interior=%s",
            outW, outH,
            cloudRaymarch->HasValidTiming() ? cloudRaymarch->GetLastCloudMs() : 0.0f,
            cloudCov, isInterior ? "yes" : "no");
    }
    return true;
}

bool D3D12Bridge::Impl::ExecutePostCinematicOnQueue(bool hasMotion)
{
    if (!postCinematicReady || !postCinematic || !postCinematic->Ready() ||
        !queue || !postCinematicList || !postCinematicFence || !postCinematicEvent ||
        !colorOut.res12 || !depthIn.res12) {
        return false;
    }

    const uint32_t slot = postCinematicSlot;
    if (postCinematicSlotVal[slot] > 0 && postCinematicFence->GetCompletedValue() < postCinematicSlotVal[slot]) {
        postCinematicFence->SetEventOnCompletion(postCinematicSlotVal[slot], postCinematicEvent);
        WaitForSingleObject(postCinematicEvent, 3000);
    }
    if (FAILED(postCinematicAlloc[slot]->Reset()) ||
        FAILED(postCinematicList->Reset(postCinematicAlloc[slot].Get(), nullptr))) {
        return false;
    }

    const uint32_t outW = std::max(cfg.width / 2u, 64u);
    const uint32_t outH = std::max(cfg.height / 2u, 36u);
    ID3D12Resource* velRes = (hasMotion && motionIn.res12) ? motionIn.res12.Get() : nullptr;

    PostCinematicParams pp{};
    pp.nearZ             = 16.0f;
    pp.farZ              = 16384.0f;
    pp.focusDistanceM    = 640.0f;
    pp.focalLengthMm     = 50.0f;
    pp.apertureFStop     = 2.2f;
    pp.cocScale          = 1.25f;
    pp.dofStrength       = 0.85f;
    pp.maxBokehRadiusPx  = 8.0f;
    pp.motionBlurScale   = velRes ? 1.0f : 0.0f;
    pp.maxMotionRadiusPx = 20.0f;
    pp.vignetteStrength  = 0.18f;
    pp.frameJitter       = static_cast<float>(postCinematicStepFrames & 63ull);

    postCinematic->Execute(postCinematicList.Get(),
                           colorOut.res12.Get(), depthIn.res12.Get(), velRes,
                           outW, outH, pp,
                           D3D12_RESOURCE_STATE_COMMON,
                           D3D12_RESOURCE_STATE_COMMON,
                           D3D12_RESOURCE_STATE_COMMON);

    if (FAILED(postCinematicList->Close())) return false;
    ID3D12CommandList* lists[1] = {postCinematicList.Get()};
    queue->ExecuteCommandLists(1u, lists);
    queue->Signal(postCinematicFence.Get(), ++postCinematicFenceVal);
    postCinematicSlotVal[slot] = postCinematicFenceVal;
    postCinematicSlot = (slot + 1u) % kRing;

    if ((postCinematicStepFrames++ % 180ull) == 0ull) {
        log("[PostCinematic] live half-res %ux%u MB+DoF: tileMs=%.4f, compMs=%.4f, motion=%s",
            outW, outH,
            postCinematic->HasValidTiming() ? postCinematic->GetLastTileMs() : 0.0f,
            postCinematic->HasValidTiming() ? postCinematic->GetLastCompositeMs() : 0.0f,
            velRes ? "on" : "off");
    }
    return true;
}

bool D3D12Bridge::Impl::ExecuteGpuParticlesOnQueue(bool isInterior)
{
    if (!gpuParticlesReady || !gpuParticles || !gpuParticles->Ready() ||
        !queue || !gpuParticlesList || !gpuParticlesFence || !gpuParticlesEvent) {
        return false;
    }

    const uint32_t slot = gpuParticlesSlot;
    if (gpuParticlesSlotVal[slot] > 0 && gpuParticlesFence->GetCompletedValue() < gpuParticlesSlotVal[slot]) {
        gpuParticlesFence->SetEventOnCompletion(gpuParticlesSlotVal[slot], gpuParticlesEvent);
        WaitForSingleObject(gpuParticlesEvent, 3000);
    }
    if (FAILED(gpuParticlesAlloc[slot]->Reset()) ||
        FAILED(gpuParticlesList->Reset(gpuParticlesAlloc[slot].Get(), nullptr))) {
        return false;
    }

    const uint64_t nowMs = GetTickCount64();
    float dtSec = 1.0f / 60.0f;
    if (gpuParticlesLastMs > 0ull && nowMs >= gpuParticlesLastMs) {
        dtSec = std::clamp(static_cast<float>(nowMs - gpuParticlesLastMs) * 0.001f, 0.001f, 0.05f);
    }
    gpuParticlesLastMs = nowMs;
    if (lighting.menuPaused) dtSec = 0.0f;

    LightingFluidInputs fi{};
    LightingPassFluidInputs(pass.get(), &fi);

    float weatherSnow = 0.0f;
    const void* uPtr = nullptr;
    size_t uBytes = 0;
    if (pass && LightingPassUniform(pass.get(), &uPtr, &uBytes) &&
        uPtr != nullptr && uBytes >= sizeof(TelemetryLightUniformView)) {
        const auto* u = static_cast<const TelemetryLightUniformView*>(uPtr);
        if (std::isfinite(u->weather[1])) weatherSnow = std::clamp(u->weather[1], 0.0f, 1.0f);
    }

    // Weather-driven particle mode: Snow (blizzards), Rain (storms), WaterfallMist (damp rivers), Embers (fires/clear dusk)
    ParticleMode mode = ParticleMode::Embers;
    if (!isInterior) {
        const bool nearRiver = std::isfinite(fi.waterPlaneZ) &&
                               (std::fabs(lighting.posAdjust[2] - fi.waterPlaneZ) < 420.0f);
        if (weatherSnow > 0.18f) {
            mode = ParticleMode::Snow;
        } else if (fi.rainIntensity > 0.18f) {
            mode = ParticleMode::Rain;
        } else if (nearRiver && lastFluidFireEmitters == 0u) {
            mode = ParticleMode::WaterfallMist;
        } else {
            mode = ParticleMode::Embers;
        }
    }

    const bool resetNow = (gpuParticlesStepFrames == 0ull) || (mode != gpuParticlesLastMode);
    gpuParticlesLastMode = mode;

    // Build camera-relative LH view-projection in local Y-up meters (emitter positioned ahead of camera)
    const float aspect = static_cast<float>(gpuParticles->SplatWidth()) /
                         static_cast<float>(std::max(gpuParticles->SplatHeight(), 1u));
    const float eye[3]    = {0.0f, 2.0f, -8.0f};
    const float target[3] = {0.0f, 2.0f,  0.0f};
    const float up[3]     = {0.0f, 1.0f,  0.0f};

    GpuParticlesParams pp{};
    BuildParticlesViewProjLH(eye, target, up, 1.04719755f, aspect, 0.1f, 120.0f, pp.viewProj);
    pp.emitterPos[0]      = 0.0f;
    pp.emitterPos[1]      = (mode == ParticleMode::Snow || mode == ParticleMode::Rain) ? 7.5f : 1.0f;
    pp.emitterPos[2]      = 0.0f;
    pp.emitterRadius      = (mode == ParticleMode::Snow || mode == ParticleMode::Rain) ? 6.0f : 1.2f;
    pp.windVel[0]         = isInterior ? 0.0f : std::clamp(fi.windXY[0] * 0.04f, -14.0f, 14.0f);
    pp.windVel[1]         = 0.0f;
    pp.windVel[2]         = isInterior ? 0.0f : std::clamp(fi.windXY[1] * 0.04f, -14.0f, 14.0f);
    pp.curlStrength       = (mode == ParticleMode::Rain) ? 0.35f : 1.45f;
    pp.dtSec              = dtSec;
    pp.timeSec            = std::isfinite(fi.simTimeSec) ? fi.simTimeSec : 0.0f;
    pp.groundY            = 0.0f;
    pp.restitution        = (mode == ParticleMode::Rain) ? 0.55f : 0.40f;
    pp.gravityScale       = 1.0f;
    pp.buoyancyAccel      = 4.2f;
    pp.dragCoeff          = (mode == ParticleMode::Snow) ? 1.4f : 0.75f;
    pp.splatIntensity     = 1.0f;
    pp.mode               = mode;
    pp.frameIndex         = static_cast<uint32_t>(gpuParticlesStepFrames & 0xFFFFu);
    pp.resetAll           = resetNow;
    pp.enableRespawn      = true;
    pp.enableGroundBounce = true;

    gpuParticles->Execute(gpuParticlesList.Get(), pp);

    if (FAILED(gpuParticlesList->Close())) return false;
    ID3D12CommandList* lists[1] = {gpuParticlesList.Get()};
    queue->ExecuteCommandLists(1u, lists);
    queue->Signal(gpuParticlesFence.Get(), ++gpuParticlesFenceVal);
    gpuParticlesSlotVal[slot] = gpuParticlesFenceVal;
    gpuParticlesSlot = (slot + 1u) % kRing;

    if ((gpuParticlesStepFrames++ % 180ull) == 0ull) {
        const char* modeName = (mode == ParticleMode::Snow) ? "Snow" :
                               (mode == ParticleMode::Rain) ? "Rain" :
                               (mode == ParticleMode::WaterfallMist) ? "Mist" : "Embers";
        log("[GpuParticles] live 16K 3D particles (%s): simMs=%.4f, splatMs=%.4f, totalMs=%.4f",
            modeName,
            gpuParticles->HasValidTiming() ? gpuParticles->GetLastSimMs() : 0.0f,
            gpuParticles->HasValidTiming() ? gpuParticles->GetLastSplatMs() : 0.0f,
            gpuParticles->HasValidTiming() ? gpuParticles->GetLastTotalMs() : 0.0f);
    }
    return true;
}

bool D3D12Bridge::Impl::CpuWait(uint64_t value, DWORD timeoutMs)
{
    return WaitFence(fence12.Get(), value, cpuEvent, timeoutMs);
}

void D3D12Bridge::Impl::OpenD11Slot(D11Slot& s, uint64_t frame)
{
    if (s.open && s.frame == frame) return;
    if (s.open) ctx11->End(s.disjoint.Get());   // stale, never executed
    ctx11->Begin(s.disjoint.Get());
    s.open = true;
    s.frame = frame;
    s.hasDepth = false;
    s.hasNormal = false;
    s.hasMotion = false;
    s.hasSurface = false;
    s.hasShadows = false;
    s.executed = false;
}

bool D3D12Bridge::CaptureDepth(ID3D11ShaderResourceView* depthSrv)
{
    if (!m->ready || !depthSrv) return false;
    ComPtr<ID3D11Resource> res;
    ComPtr<ID3D11Texture2D> tex;
    depthSrv->GetResource(&res);
    if (!res || FAILED(res.As(&tex))) return false;
    D3D11_TEXTURE2D_DESC d{};
    tex->GetDesc(&d);
    if (d.Width != m->cfg.width || d.Height != m->cfg.height || d.SampleDesc.Count != 1)
        return false;

    auto* c = m->ctx11.Get();
    // Save the compute-stage bindings this pass overwrites.
    ComPtr<ID3D11ComputeShader> oldCS;
    ComPtr<ID3D11ShaderResourceView> oldSRV;
    ComPtr<ID3D11UnorderedAccessView> oldUAV;
    UINT numInst = 0;
    c->CSGetShader(&oldCS, nullptr, &numInst);
    c->CSGetShaderResources(0, 1, &oldSRV);
    c->CSGetUnorderedAccessViews(0, 1, &oldUAV);

    D11Slot& s = m->SlotFor(m->frames);
    m->OpenD11Slot(s, m->frames);
    c->End(s.ts[0].Get());
    ID3D11UnorderedAccessView* uav = m->depthUAV.Get();
    c->CSSetShader(m->depthCS.Get(), nullptr, 0);
    c->CSSetShaderResources(0, 1, &depthSrv);
    c->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    c->Dispatch((d.Width + 7) / 8, (d.Height + 7) / 8, 1);
    c->End(s.ts[1].Get());

    ID3D11ShaderResourceView* srv0 = oldSRV.Get();
    ID3D11UnorderedAccessView* uav0 = oldUAV.Get();
    const UINT keepCount = ~0u;
    c->CSSetUnorderedAccessViews(0, 1, &uav0, &keepCount);
    c->CSSetShaderResources(0, 1, &srv0);
    c->CSSetShader(oldCS.Get(), nullptr, 0);
    s.hasDepth = true;
    return true;
}

bool D3D12Bridge::CaptureNormal(ID3D11Texture2D* normalTex)
{
    if (!m->ready || !normalTex || !m->normalIn.tex11) return false;
    D3D11_TEXTURE2D_DESC d{};
    normalTex->GetDesc(&d);
    if (d.Width != m->cfg.width || d.Height != m->cfg.height || d.SampleDesc.Count != 1) return false;
    D11Slot& s = m->SlotFor(m->frames);
    m->OpenD11Slot(s, m->frames);
    auto* c = m->ctx11.Get();
    c->End(s.ts[2].Get());
    c->CopyResource(m->normalIn.tex11.Get(), normalTex);
    if (m->pendingMotion) {
        c->CopyResource(m->motionIn.tex11.Get(), m->pendingMotion);
        m->pendingMotion = nullptr;
        s.hasMotion = true;
    }
    c->End(s.ts[3].Get());
    s.hasNormal = true;
    return true;
}

bool D3D12Bridge::CaptureMotion(ID3D11Texture2D* motionTex)
{
    if (!m->ready || !motionTex || !m->motionIn.tex11) return false;
    D3D11_TEXTURE2D_DESC d{};
    motionTex->GetDesc(&d);
    if (d.Width != m->cfg.width || d.Height != m->cfg.height || d.SampleDesc.Count != 1 || d.Format != m->cfg.motionFormat) return false;
    m->pendingMotion = motionTex;
    return true;
}

void D3D12Bridge::SetCamera(const CameraConstants& camera) { m->camera = camera; }

void D3D12Bridge::SetRtDraws(std::vector<RtDraw>&& draws)
{
    for (RtDraw& d : m->pendingDraws) ReleaseRtDraw(d);
    m->pendingDraws = std::move(draws);
    if (!m->rt) {
        for (RtDraw& d : m->pendingDraws) ReleaseRtDraw(d);
        m->pendingDraws.clear();
    }
}
void D3D12Bridge::TuneRt(const RtTuning& t) { m->rtTune = t; if (m->rt) m->rt->Tune(t); }
bool D3D12Bridge::ReloadRt(const std::vector<uint8_t>& shadow, const std::vector<uint8_t>& sky, std::string& err)
{
    if (!m->rt) { err = "no ray-tracing scene (RtShadows=0, or not on raw::tx)"; return false; }
    return m->rt->Reload(shadow, sky, err);
}

bool D3D12Bridge::RtStats(RtFrameStats* out)
{
    if (!m->rt || !out) return false;
    *out = m->rt->Stats();
    return true;
}

// Stores the pass's newest retired timings under the frame they belong to.
void D3D12Bridge::Impl::HarvestPassTimings()
{
    PassTimings pt;
    if (!pass || !pass->LastTimings(&pt)) return;
    D12Slot& ds = d12[pt.submission % kRing];
    ds.frameTag = pt.submission;
    ds.pass = pt;
    ds.havePass = true;
}

bool D3D12Bridge::Execute(ID3D11Resource* colorSrc, ID3D11Resource* colorDst,
                          const PassConstants& kIn)
{
    if (!m->ready || !colorSrc || !colorDst) return false;
    const uint64_t frame = m->frames;

    // Fence values: D3D11 signals v1, the D3D12 queue waits for v1 + skew,
    // D3D12 signals v2, D3D11 waits for v2. Values only ever increase.
    const uint64_t v1 = m->nextValue;
    const uint64_t waitValue = v1 + m->skew;
    const uint64_t v2 = waitValue + 1;

    D11Slot& s = m->SlotFor(frame);
    m->OpenD11Slot(s, frame);
    FrameConstants fc;
    fc.k = kIn;
    fc.k.dims[0] = static_cast<float>(m->cfg.width);
    fc.k.dims[1] = static_cast<float>(m->cfg.height);
    fc.k.dims[2] = 1.0f / fc.k.dims[0];
    fc.k.dims[3] = 1.0f / fc.k.dims[1];
    fc.camera = m->camera;
    fc.hasNormal = s.hasNormal;
    fc.hasMotion = s.hasMotion;
    fc.rt = m->attached && m->rt && m->rt->Active() && m->cfg.lightingPass;
    if (m->cfg.lightingPass) {
        fc.lighting = &m->lighting;
        fc.hasSurface = s.hasSurface;
        fc.hasShadows = s.hasShadows;
        // Live beyond-Tamriel 6-biome frontier chunk + 3D procedural hero prop streaming (O(1) no-op inside Tamriel):
        if (m->lighting.valid && m->meshHost.TerrainReady()) {
            struct BridgeChunkSink final : raw::worldgen::IGpuChunkSink {
                D3D12Bridge::Impl* impl = nullptr;
                uint64_t OnChunkLoaded(const raw::worldgen::StreamedChunkEntry& entry) override {
                    raw::worldgen::ProceduralMesh merged{};
                    const auto& wcfg = impl->worldShowcase.Config();
                    if (!raw::worldgen::BuildChunkMergedGpuMesh(entry, wcfg.maxMeshletTris, wcfg.terrain.seed,
                                                                wcfg.terrain.seaLevelZ, &merged)) {
                        return 0u;
                    }
                    TerrainGpuChunk gpu{};
                    if (!impl->meshHost.UploadTerrainChunk(merged, &gpu)) return 0u;
                    const uint64_t h = impl->nextTerrainHandle++;
                    impl->streamedTerrainGpu.emplace(h, std::move(gpu));
                    return h;
                }
                void OnChunkEvicted(raw::worldgen::ChunkKey, uint64_t gpuHandle) override {
                    if (gpuHandle == 0u) return;
                    auto it = impl->streamedTerrainGpu.find(gpuHandle);
                    if (it != impl->streamedTerrainGpu.end()) {
                        impl->meshHost.DeferRelease(std::move(it->second));
                        impl->streamedTerrainGpu.erase(it);
                    }
                }
            };
            BridgeChunkSink sink;
            sink.impl = m.get();
            m->lastWorldStats = m->worldShowcase.Update(
                {m->lighting.posAdjust[0], m->lighting.posAdjust[1], m->lighting.posAdjust[2]}, &sink);
        }
    }

    // The pass records first, so recording time never shows up as a stall,
    // and submits only after the D3D11 Signal: in the game process D3D11
    // rejects Signal(v1) with E_INVALIDARG once D3D12 has a pending Signal(v2).
    if (!m->pass->Record(fc)) {
        // The frame before may still have a D3D11 Wait queued for a value the
        // pass will now never signal. Signal it from the CPU, or the game's
        // D3D11 queue stays blocked for good.
        m->log("[D3D12Bridge] pass '%s' failed to record; releasing fence value %llu and turning the bridge off",
               m->pass->Name(), (unsigned long long)m->lastD12);
        if (m->dev12 && FAILED(m->dev12->GetDeviceRemovedReason())) LogDeviceRemoved(m->dev12.Get(), m->log);
        if (m->fence12 && m->fence12->GetCompletedValue() < m->lastD12) m->fence12->Signal(m->lastD12);
        m->ready = false;
        return false;
    }
    m->HarvestPassTimings();   // recording may have retired the frame before
    m->nextValue = v2 + 1;

    // Sample frame delta into 64-frame sparkline history and poll debounced opt-in F9 hotkey (default OFF).
    m->RecordFrameTimingSample();
    m->PollOverlayHotkey();

    // Determine whether live RawTerrainWorld.hlsl multi-biome rendering runs after LightingPass this frame.
    const bool isInterior = m->lighting.hasEngineCell
        ? m->lighting.isInterior
        : !(s.hasShadows && m->lighting.hasCascades);
    const void* terrainU = nullptr;
    size_t terrainUb = 0;
    const bool runTerrain = m->cfg.lightingPass && m->lighting.valid && !isInterior &&
                            !m->streamedTerrainGpu.empty() && m->meshHost.TerrainReady() &&
                            LightingPassUniform(m->pass.get(), &terrainU, &terrainUb);
    const bool runFluid = m->cfg.lightingPass && m->lighting.valid && m->fluidReady && m->fluidSim.Ready();
    const bool runRadiance = m->cfg.lightingPass && m->lighting.valid && m->radianceReady && m->radianceHost.ScreenGIReady();
    const bool runXpbd = m->cfg.lightingPass && m->lighting.valid && m->xpbdSimReady && m->xpbdSim && m->xpbdSim->Ready();
    const bool runGpuCull = m->cfg.lightingPass && m->lighting.valid && m->gpuCullReady && m->gpuCull &&
                            m->gpuCull->Ready() && m->depthIn.res12 != nullptr;
    const bool runLightClusters = m->cfg.lightingPass && m->lighting.valid && m->lightClustersReady &&
                                  m->lightClusters && m->lightClusters->Ready();
    const bool runVolFroxels = m->cfg.lightingPass && m->lighting.valid && m->volFroxelsReady &&
                               m->volFroxels && m->volFroxels->Ready();
    const bool runShadowPcss = m->cfg.lightingPass && m->lighting.valid && m->shadowPcssReady &&
                               m->shadowPcss && m->shadowPcss->Ready() && m->depthIn.res12 != nullptr;
    const bool runGtao = m->cfg.lightingPass && m->lighting.valid && m->gtaoHostReady &&
                         m->gtaoHost && m->gtaoHost->Ready() && m->depthIn.res12 != nullptr;
    const bool runSsr = m->cfg.lightingPass && m->lighting.valid && m->ssrHostReady &&
                        m->ssrHost && m->ssrHost->Ready() && m->colorOut.res12 != nullptr && m->depthIn.res12 != nullptr;
    const bool runAutoExposure = m->cfg.lightingPass && m->lighting.valid && m->autoExposureReady &&
                                 m->autoExposure && m->autoExposure->Ready() && m->colorOut.res12 != nullptr;
    const bool runCloudRaymarch = m->cfg.lightingPass && m->lighting.valid && m->cloudRaymarchReady &&
                                  m->cloudRaymarch && m->cloudRaymarch->Ready();
    const bool runPostCinematic = m->cfg.lightingPass && m->lighting.valid && m->postCinematicReady &&
                                  m->postCinematic && m->postCinematic->Ready() &&
                                  m->colorOut.res12 != nullptr && m->depthIn.res12 != nullptr;
    const bool runGpuParticles = m->cfg.lightingPass && m->lighting.valid && m->gpuParticlesReady &&
                                 m->gpuParticles && m->gpuParticles->Ready();
    const bool runOverlay = m->overlayHudEnabled && m->overlayHostReady && m->overlayHost && m->overlayHost->Ready();
    const bool hasPostWork = runTerrain || runFluid || runRadiance || runXpbd || runGpuCull || runLightClusters || runVolFroxels || runShadowPcss || runGtao || runSsr || runAutoExposure || runCloudRaymarch || runPostCinematic || runGpuParticles || runOverlay;
    auto runPostLightingPasses = [&]() -> bool {
        if (!hasPostWork) return true;
        if (runTerrain) {
            uint32_t drawnChunks = 0u, drawnMeshlets = 0u;
            const float timeSec = float(double(GetTickCount64() % 3600000ull) / 1000.0);
            ID3D12Resource* atlasRes = s.hasShadows ? m->shared[static_cast<UINT>(Shared::ShadowAtlas)].res12.Get() : nullptr;
            const bool okT = m->meshHost.ExecuteTerrainPass(
                m->queue.Get(), m->colorOut.res12.Get(), m->depthIn.res12.Get(), atlasRes,
                m->cfg.width, m->cfg.height, terrainU, terrainUb,
                &m->lighting.worldToClip[0][0], m->lighting.posAdjust, timeSec,
                m->streamedTerrainGpu, &drawnChunks, &drawnMeshlets);
            if (okT) {
                if ((m->terrainRenderFrames++ % 180ull) == 0ull) {
                    m->log("[WorldGen] LIVE render: %u chunks (%u meshlets) at cam (%.0f, %.0f, %.0f) biome=%s",
                           drawnChunks, drawnMeshlets,
                           m->lighting.posAdjust[0], m->lighting.posAdjust[1], m->lighting.posAdjust[2],
                           raw::worldgen::GetBiomeName(m->lastWorldStats.cameraBiome));
                }
            }
        }
        if (runFluid) {
            m->StepFluidSimOnQueue(isInterior);
            // Forward live 256x256 GPU SWE water state + normals to RtWater (RtScene):
            if (m->rt) {
                const auto wDom = m->fluidSim.GetWaterDomainInfo();
                if (wDom.active && m->fluidSim.WaterStateResource() && m->fluidSim.WaterNormalResource()) {
                    m->rt->SetFluidWaterResources(
                        m->fluidSim.WaterStateResource(), m->fluidSim.WaterNormalResource(),
                        wDom.originXY[0], wDom.originXY[1], wDom.worldSpanU);
                }
            }
            // Composite live 64x64x64 GPU 3D Navier-Stokes fire/smoke/radiance directly onto colorOut with SceneDepth occlusion:
            const auto fDom = m->fluidSim.GetFireDomainInfo();
            if (m->fluidComposite.Ready() && fDom.active && fDom.emitterCount > 0u &&
                m->fluidSim.FireThermoResource() && m->fluidSim.FireRadianceResource()) {
                LightingFluidInputs fi{};
                LightingPassFluidInputs(m->pass.get(), &fi);
                FluidCompositeParams cp{};
                std::memcpy(cp.clipToWorld, &m->lighting.clipToWorld[0][0], 64u);
                cp.camPosW[0] = m->lighting.posAdjust[0];
                cp.camPosW[1] = m->lighting.posAdjust[1];
                cp.camPosW[2] = m->lighting.posAdjust[2];
                cp.simTimeSec = fi.simTimeSec;
                cp.domainMinW[0] = fDom.originXYZ[0];
                cp.domainMinW[1] = fDom.originXYZ[1];
                cp.domainMinW[2] = fDom.originXYZ[2];
                cp.domainSpanW = fDom.worldSpanU;
                cp.resScale[0] = m->lighting.resolutionScale[0];
                cp.resScale[1] = m->lighting.resolutionScale[1];
                cp.sunDir[0] = m->lighting.sunDirection[0];
                cp.sunDir[1] = m->lighting.sunDirection[1];
                cp.sunDir[2] = m->lighting.sunDirection[2];
                cp.sunIntensity = m->lighting.hasSun ? 1.0f : 0.25f;
                cp.sunCol[0] = m->lighting.sunColour[0];
                cp.sunCol[1] = m->lighting.sunColour[1];
                cp.sunCol[2] = m->lighting.sunColour[2];
                cp.ambientFill = isInterior ? 0.35f : 0.65f;
                cp.width = m->cfg.width;
                cp.height = m->cfg.height;
                cp.activeEmitters = fDom.emitterCount;
                cp.isInterior = isInterior;
                if (m->fluidComposite.Composite(
                        m->queue.Get(), m->colorOut.res12.Get(), m->depthIn.res12.Get(),
                        m->fluidSim.FireThermoResource(), m->fluidSim.FireRadianceResource(), cp)) {
                    if ((m->fluidCompFrames++ % 180ull) == 0ull) {
                        m->log("[FluidComposite] live 64^3 NS fire/smoke composited: emitters=%u, domainSpan=%.0fu",
                               fDom.emitterCount, fDom.worldSpanU);
                    }
                }
            }
        }
        if (runRadiance) {
            m->ExecuteRadianceScreenGiOnQueue();
        }
        if (runXpbd) {
            m->StepXpbdSimOnQueue(isInterior);
        }
        if (runGpuCull) {
            m->ExecuteGpuCullOnQueue();
        }
        if (runLightClusters) {
            m->ExecuteLightClustersOnQueue();
        }
        if (runVolFroxels) {
            m->ExecuteVolFroxelsOnQueue(isInterior);
        }
        if (runShadowPcss) {
            m->ExecuteShadowPcssOnQueue(s.hasShadows);
        }
        if (runGtao) {
            m->ExecuteGtaoOnQueue();
        }
        if (runSsr) {
            m->ExecuteSsrOnQueue();
        }
        if (runAutoExposure) {
            m->ExecuteAutoExposureOnQueue();
        }
        if (runCloudRaymarch) {
            m->ExecuteCloudRaymarchOnQueue(isInterior);
        }
        if (runPostCinematic) {
            m->ExecutePostCinematicOnQueue(s.hasMotion);
        }
        if (runGpuParticles) {
            m->ExecuteGpuParticlesOnQueue(isInterior);
        }
        if (runOverlay) {
            m->ExecuteOverlayHudOnQueue(isInterior);
        }
        return SUCCEEDED(m->queue->Signal(m->fence12.Get(), v2));
    };

    // D3D11: copy in, hand over, take back, copy out.
    auto* c = m->ctx11.Get();
    c->End(s.ts[4].Get());
    c->CopyResource(m->colorIn.tex11.Get(), colorSrc);
    c->End(s.ts[5].Get());
    if (m->attached) {
        // One queue: the layer submits what D3D11 recorded so far with every shared texture in
        // COMMON, the pass follows on the same queue, and the copy-out lands in the layer's next list.
        std::vector<ID3D11Resource*> hand;
        bool rtOk = false;
        if (fc.rt) {
            if (m->bindless.Ready() && m->cfg.layer.native) {
                for (RtDraw& dr : m->pendingDraws) {
                    if (!dr.alphaSrv) continue;
                    ID3D11Resource* r11 = nullptr;
                    dr.alphaSrv->GetResource(&r11);
                    if (r11) {
                        if (ID3D12Resource* r12 = m->cfg.layer.native(r11)) {
                            D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
                            dr.alphaSrv->GetDesc(&sd);
                            dr.bindlessAlbedoIdx = m->bindless.RegisterTexture2D(r12, sd.Format);
                        }
                        r11->Release();
                    }
                }
            }
            m->rt->SetCamera(m->lighting.cameraStep, m->lighting.cameraSteps, m->lighting.posAdjust);
            m->rt->SetDraws(std::move(m->pendingDraws), hand, m->cfg.layer.native);
            const void* u = nullptr;
            size_t ub = 0;
            if (LightingPassUniform(m->pass.get(), &u, &ub))
                rtOk = m->rt->Record(u, ub, m->depthIn.res12.Get(), m->normalIn.res12.Get(),
                                     m->shared[static_cast<UINT>(Shared::Specular)].res12.Get(),
                                     fc.hasMotion ? m->motionIn.res12.Get() : nullptr, m->colorIn.res12.Get());
        }
        if (!m->HandToHost(hand) || (rtOk && !m->rt->Submit(m->queue.Get())) ||
            !m->pass->Submit(0, hasPostWork ? 0 : v2) || !runPostLightingPasses()) {
            m->log("[D3D12Bridge] attached hand-over failed; bridge off");
            c->End(s.disjoint.Get());
            s.open = false;
            m->ready = false;
            return false;
        }
        m->lastWaited = 0;
        m->lastD12 = v2;
        c->End(s.ts[6].Get());
        c->CopyResource(colorDst, m->colorOut.tex11.Get());
        c->End(s.ts[7].Get());
        c->End(s.disjoint.Get());
        s.open = false;
        s.executed = true;
        ++m->frames;
        m->EmitLiveTelemetry(fc.rt);
        StreamlineHost::Get().ReflexBeginFrame(static_cast<uint32_t>(m->frames));
        return true;
    }
    HRESULT hs = m->ctx4->Signal(m->fence11.Get(), v1);
    c->Flush();
    LARGE_INTEGER qf, q0, q1;
    QueryPerformanceFrequency(&qf);
    QueryPerformanceCounter(&q0);
    if (FAILED(hs)) {
        // Never queue a GPU wait the other side cannot satisfy.
        m->log("[D3D12Bridge] D3D11 Signal failed 0x%08X; bridge off", (unsigned)hs);
        c->End(s.disjoint.Get());
        s.open = false;
        m->ready = false;
        return false;
    }
    m->ProbeHead(frame, waitValue);
    if (!m->pass->Submit(waitValue, hasPostWork ? 0 : v2) || !runPostLightingPasses()) {
        // Nothing will signal v2 now, so D3D11 must not wait for it.
        m->log("[D3D12Bridge] pass '%s' failed to submit; bridge off", m->pass->Name());
        c->End(s.disjoint.Get());
        s.open = false;
        m->ready = false;
        return false;
    }
    m->ProbeTail(frame);
    QueryPerformanceCounter(&q1);
    // Already executed on the GPU when the D3D12 list was only now submitted: the queue idles on the CPU.
    m->caughtUp += c->GetData(s.ts[5].Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
    m->flushToSubmitUs += double(q1.QuadPart - q0.QuadPart) * 1e6 / double(qf.QuadPart);
    if (++m->handOvers == 300) {
        m->log("[D3D12Bridge] hand-over over 300 frames: the GPU had already reached it at submit in %u, CPU flush-to-submit mean %.1f us",
               m->caughtUp, m->flushToSubmitUs / 300.0);
        m->caughtUp = m->handOvers = 0;
        m->flushToSubmitUs = 0;
    }
    m->lastWaited = waitValue;
    m->lastD12 = v2;
    HRESULT hw = m->ctx4->Wait(m->fence11.Get(), v2);
    if (FAILED(hw)) m->log("[D3D12Bridge] D3D11 Wait failed 0x%08X", (unsigned)hw);
    c->End(s.ts[6].Get());
    c->CopyResource(colorDst, m->colorOut.tex11.Get());
    c->End(s.ts[7].Get());
    if (m->cfg.handoverFlush) c->Flush();
    c->End(s.disjoint.Get());
    s.open = false;
    s.executed = true;
    ++m->frames;
    m->EmitLiveTelemetry(fc.rt);
    StreamlineHost::Get().ReflexBeginFrame(static_cast<uint32_t>(m->frames));
    return true;
}

template <class T>
static bool GetQuery(ID3D11DeviceContext* c, ID3D11Query* q, T* out, bool block)
{
    const ULONGLONG deadline = GetTickCount64() + 2000;
    for (;;) {
        HRESULT hr = c->GetData(q, out, sizeof(T), block ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (hr == S_OK) return true;
        if (FAILED(hr) || !block || GetTickCount64() > deadline) return false;
        Sleep(0);
    }
}

bool D3D12Bridge::ReadTimings(uint64_t frame, bool block, BridgeTimings* out)
{
    if (!m->ready || !out) return false;
    D11Slot& s = m->SlotFor(frame);
    D12Slot& ds = m->d12[frame % kRing];
    if (s.frame != frame || !s.executed) return false;

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
    if (!GetQuery(m->ctx11.Get(), s.disjoint.Get(), &dj, block) || dj.Disjoint) return false;
    uint64_t t[kD11Stamps]{};
    for (UINT i = 0; i < kD11Stamps; ++i) {
        if ((i < 2 && !s.hasDepth) || (i >= 2 && i < 4 && !s.hasNormal)) continue;
        if (!GetQuery(m->ctx11.Get(), s.ts[i].Get(), &t[i], block)) return false;
    }

    m->HarvestPassTimings();
    if (!(ds.havePass && ds.frameTag == frame) && block) {
        if (!m->pass->Retire(m->cfg.cpuWaitTimeout)) return false;
        m->HarvestPassTimings();
    }
    if (!(ds.havePass && ds.frameTag == frame)) return false;

    const double f11 = 1000.0 / static_cast<double>(dj.Frequency);
    BridgeTimings r{};
    r.frame = frame;
    r.depthCopyMs = s.hasDepth ? (t[1] - t[0]) * f11 : 0.0;
    r.normalCopyMs = s.hasNormal ? (t[3] - t[2]) * f11 : 0.0;
    r.copyInMs = (t[5] - t[4]) * f11;
    r.stallMs = (t[6] - t[5]) * f11;
    r.copyOutMs = (t[7] - t[6]) * f11;
    r.pass = ds.pass;
    r.passMs = ds.pass.totalMs;
    const double rtMs = m->rt ? (m->rt->Stats().buildMs + m->rt->Stats().traceMs + m->rt->Stats().cacheMs) : 0.0;
    const double gpuWorkMs = r.passMs + rtMs;
    r.syncMs = r.stallMs > gpuWorkMs ? r.stallMs - gpuWorkMs : 0.0;
    r.bridgeCostMs = r.depthCopyMs + r.normalCopyMs + r.copyInMs + r.copyOutMs + r.syncMs;
    m->ProbeRead(frame, dj.Frequency, t[5], t[6], &r);
    *out = r;
    return true;
}

uint64_t D3D12Bridge::FramesExecuted() const { return m->frames; }
void D3D12Bridge::SetWaitSkew(uint64_t skew) { m->skew = skew; }
bool D3D12Bridge::WaitForD3D12(DWORD timeoutMs) { return m->CpuWait(m->lastD12, timeoutMs); }
void D3D12Bridge::ForceSignal(uint64_t value) { if (m->fence12) m->fence12->Signal(value); }
uint64_t D3D12Bridge::LastD3D12Value() const { return m->lastD12; }
uint64_t D3D12Bridge::LastWaitedValue() const { return m->lastWaited; }
uint64_t D3D12Bridge::CompletedValue() const { return m->fence12 ? m->fence12->GetCompletedValue() : 0; }

} // namespace raw::bridge
