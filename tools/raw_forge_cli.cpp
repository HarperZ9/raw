// raw_forge_cli.cpp: Standalone C++23 Studio Authoring, Baking & Validation CLI (`raw::pipeline`).
// Drives RAW's clean-room Biomechanical Flora, 6-Biome WorldGen Diorama, 365-Day Orbital Climate Almanac,
// Physical Optical Material Registry, and DX12 AS+MS Hardware Meshlet Asset Validator.
//
// Usage:
//   raw_forge_cli --flora <dir>        Bake & validate 5 species x 4 seasons x 3 LODs (.glb/.obj + manifest)
//   raw_forge_cli --diorama <dir>      Bake & validate 6 frontier biomes + hero props (.glb/.obj + manifest)
//   raw_forge_cli --almanac <dir>      Simulate & export 365-day orbital climate/phenology CSV + JSON
//   raw_forge_cli --materials <dir>    Validate & export physical optical material JSON + C++ header
//   raw_forge_cli --validate-all [dir] Run all 4 pipelines + quality gates end-to-end
//   Options: --seed <N> (default 202607), --latitude <deg> (default 60.0), --no-write
//
// Exit code: 0 on 100% validation pass, 1 on any pipeline/validation defect, 2 on bad CLI arguments.
// Original work, MIT.

#include "../src/pipeline/RawForgePipeline.h"
#include "../src/worldgen/RawBsaArchive.h"
#include "../src/worldgen/RawEsmModelResolver.h"
#include "../src/worldgen/RawEsmNpcWeatherLoader.h"
#include "../src/worldgen/RawNavMeshAi.h"
#include "../src/worldgen/RawSettlementSim.h"
#include "../src/worldgen/RawTamrielWorldStreamer.h"
#include "../src/worldgen/RawWildlifeBoids.h"
#include "../src/worldgen/RawWorldEcosystem.h"
#include "../src/worldgen/RawWorldEventDirector.h"
#include "../src/worldgen/RawWorldPhysics.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

constexpr const char* kDefaultSkyrimEsm =
    "H:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition\\Data\\Skyrim.esm";

bool ParseSignedInt(const char* s, int32_t* outVal) {
    if (!s || !*s) return false;
    char* end = nullptr;
    const long v = std::strtol(s, &end, 10);
    if (!end || *end != '\0') return false;
    *outVal = static_cast<int32_t>(v);
    return true;
}

std::string ResolveSiblingMeshesBsa(const std::string& esmPath, const std::string& explicitBsa) {
    if (!explicitBsa.empty()) return explicitBsa;
    namespace fs = std::filesystem;
    const fs::path p(esmPath);
    const fs::path candidate = p.parent_path() / "Skyrim - Meshes0.bsa";
    return candidate.string();
}

const char* WeatherRegimeName(raw::worldgen::WeatherRegime r) {
    using raw::worldgen::WeatherRegime;
    switch (r) {
        case WeatherRegime::ClearAlpine:         return "ClearAlpine";
        case WeatherRegime::DawnValleyMist:      return "DawnValleyMist";
        case WeatherRegime::MountainFogCloudCap: return "MountainFogCloudCap";
        case WeatherRegime::Overcast:            return "Overcast";
        case WeatherRegime::CoastalSeaFog:       return "CoastalSeaFog";
        case WeatherRegime::Rainstorm:           return "Rainstorm";
        case WeatherRegime::Thunderstorm:        return "Thunderstorm";
        case WeatherRegime::Blizzard:            return "Blizzard";
        case WeatherRegime::ArcticClearAurora:   return "ArcticClearAurora";
        default:                                 return "Unknown";
    }
}

const char* PrecipPhaseName(raw::worldgen::PrecipPhase p) {
    using raw::worldgen::PrecipPhase;
    switch (p) {
        case PrecipPhase::None:              return "None";
        case PrecipPhase::Rain:              return "Rain";
        case PrecipPhase::Sleet:             return "Sleet";
        case PrecipPhase::WetSnow:           return "WetSnow";
        case PrecipPhase::DryPowderBlizzard: return "DryPowderBlizzard";
        default:                             return "Unknown";
    }
}

bool IsFinite3(const raw::worldgen::Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool RunEsmMetaAudit(const std::string& esmPath) {
    raw::worldgen::EsmWorldMetaIndex meta{};
    std::string err;
    if (!raw::worldgen::LoadEsmWorldMeta(esmPath, &meta, &err)) {
        std::fprintf(stderr, "[FAIL] esm-meta: LoadEsmWorldMeta('%s') failed: %s\n", esmPath.c_str(), err.c_str());
        return false;
    }

    std::printf(
        "[INFO] ESM World Metadata (%s)\n"
        "  Parse time : %.2f ms | Localized: %s | Compressed records inflated: %u\n"
        "  Counts     : WTHR=%zu  CLMT=%zu  NPC_=%zu  REGN=%zu  QUST=%zu\n",
        meta.esmPath.c_str(),
        meta.loadTimeMs,
        meta.isLocalized ? "yes" : "no",
        meta.compressedSeen,
        meta.weathers.size(),
        meta.climates.size(),
        meta.npcs.size(),
        meta.regions.size(),
        meta.quests.size());

    static const char* kCanonicalWeathers[5] = {
        "SkyrimClear",
        "SkyrimCloudy",
        "SkyrimFog",
        "SkyrimStormRain",
        "SkyrimStormSnow",
    };

    uint32_t mappedOk = 0u;
    std::printf("  Canonical WTHR -> RawWeatherSim Physical Mapping (13:00 local):\n");
    for (const char* edid : kCanonicalWeathers) {
        const auto* w = meta.FindWeatherByEdid(edid);
        if (!w) {
            std::fprintf(stderr, "  [MISS] WTHR '%s' not found in ESM\n", edid);
            continue;
        }
        const auto p = raw::worldgen::MapEsmWeatherToPhysicalParams(*w, 13.0f);
        std::printf(
            "    %-16s (0x%08X) -> %-19s | precip=%-17s | Bft=%.1f (%.1f m/s) | cloud=%.2f | rain=%.2f snow=%.2f | fog=%.2f | thund=%.2f\n",
            w->editorId.c_str(),
            w->formId,
            WeatherRegimeName(p.suggestedRegime),
            PrecipPhaseName(p.precipPhase),
            p.beaufortWindScale,
            p.windSpeedMps,
            p.cloudCover01,
            p.rainRate01,
            p.snowRate01,
            p.fogDensity,
            p.thunderIntensity01);
        ++mappedOk;
    }

    const bool ok = !meta.weathers.empty() && !meta.climates.empty() &&
                    !meta.npcs.empty() && !meta.regions.empty() && !meta.quests.empty() &&
                    (mappedOk == 5u);
    std::printf("[%s] esm-meta: WTHR=%zu CLMT=%zu NPC_=%zu REGN=%zu QUST=%zu mapped=%u/5 (%.2f ms)\n",
                ok ? "PASS" : "FAIL",
                meta.weathers.size(), meta.climates.size(), meta.npcs.size(),
                meta.regions.size(), meta.quests.size(), mappedOk, meta.loadTimeMs);
    return ok;
}

bool RunPhysicsProbeAudit(const std::string& esmPath, const std::string& explicitBsa,
                          int32_t cellX, int32_t cellY, uint32_t maxModels) {
    const std::string bsaPath = ResolveSiblingMeshesBsa(esmPath, explicitBsa);
    const auto t0 = std::chrono::steady_clock::now();

    raw::worldgen::ReconstructedCellScene scene{};
    std::string err;
    if (!raw::worldgen::ReconstructTamrielCellScene(esmPath, bsaPath, cellX, cellY, maxModels, &scene, &err)) {
        std::fprintf(stderr, "[FAIL] physics-probe: ReconstructTamrielCellScene(%d,%d) failed: %s\n",
                     cellX, cellY, err.c_str());
        return false;
    }

    raw::worldgen::ProceduralMesh bakedProps{};
    const bool hasProps = raw::worldgen::BakeReconstructedCellPropsToWorldMesh(scene, 126u, &bakedProps, &err);

    raw::worldgen::RawWorldPhysics phys{};
    const uint64_t cellKey = raw::worldgen::PackCellKey(cellX, cellY);
    const bool okHf = phys.AddTerrainHeightfield(
        raw::worldgen::MakeHeightfieldFromTerrainChunk(scene.terrainChunk, scene.waterHeightZ, 1u));
    bool okBvh = true;
    if (hasProps && !bakedProps.vertices.empty() && !bakedProps.indices.empty()) {
        okBvh = phys.AddCellCollider(cellKey, bakedProps, 1001u, 2u);
    }

    const auto stats = phys.GetStats();
    const float originX  = scene.terrainChunk.originX;
    const float originY  = scene.terrainChunk.originY;
    const float cellSize = scene.terrainChunk.chunkSizeU;

    // 8x8 = 64 downward rays across the cell
    constexpr uint32_t kGridN = 8u;
    constexpr uint32_t kTotalRays = kGridN * kGridN;
    uint32_t totalHits   = 0u;
    uint32_t propHits    = 0u;
    uint32_t terrainHits = 0u;
    bool allHitsValid    = true;

    for (uint32_t gy = 0u; gy < kGridN; ++gy) {
        for (uint32_t gx = 0u; gx < kGridN; ++gx) {
            const float rx = originX + (static_cast<float>(gx) + 0.5f) * (cellSize / static_cast<float>(kGridN));
            const float ry = originY + (static_cast<float>(gy) + 0.5f) * (cellSize / static_cast<float>(kGridN));
            const auto h = phys.RayCast({rx, ry, 12000.0f}, {0.0f, 0.0f, -1.0f}, 36000.0f);
            if (h.hit) {
                ++totalHits;
                if (h.hitTerrain) ++terrainHits;
                else              ++propHits;
                const raw::worldgen::Vec3 p{h.point[0], h.point[1], h.point[2]};
                const raw::worldgen::Vec3 n{h.normal[0], h.normal[1], h.normal[2]};
                const float nLen = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                if (!IsFinite3(p) || !IsFinite3(n) || std::fabs(nLen - 1.0f) > 2.0e-2f || h.distance <= 0.0f) {
                    allHitsValid = false;
                }
            }
        }
    }

    // 60-step (60Hz) grounded capsule walk test starting near cell center
    const float spawnX = originX + cellSize * 0.5f;
    const float spawnY = originY + cellSize * 0.5f;
    float spawnTerrainZ = 0.0f;
    const bool sampledSpawn = phys.SampleTerrainHeight(spawnX, spawnY, &spawnTerrainZ, nullptr);

    raw::worldgen::CharacterBody hero{};
    hero.position = {spawnX, spawnY, spawnTerrainZ + 48.0f};
    raw::worldgen::CharacterInput walkIn{};
    walkIn.wishDirXY = {0.70710678f, 0.70710678f};
    walkIn.maxSpeed  = 240.0f;

    constexpr uint32_t kWalkSteps = 60u;
    uint32_t groundedSteps = 0u;
    bool allFinite         = true;
    bool neverTunneled     = true;

    for (uint32_t s = 0u; s < kWalkSteps; ++s) {
        phys.StepCharacterCapsule(hero, walkIn, 1.0f / 60.0f);
        if (!IsFinite3(hero.position) || !IsFinite3(hero.velocity)) {
            allFinite = false;
            break;
        }
        float curZ = 0.0f;
        if (phys.SampleTerrainHeight(hero.position.x, hero.position.y, &curZ, nullptr)) {
            if (hero.position.z < curZ - 1.0f) {
                neverTunneled = false;
            }
        }
        if (hero.grounded) ++groundedSteps;
    }

    const float dx = hero.position.x - spawnX;
    const float dy = hero.position.y - spawnY;
    const float walkedDist = std::sqrt(dx * dx + dy * dy);
    const double elapsedMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();

    const bool hitRateOk = (totalHits == kTotalRays) && allHitsValid;
    const bool walkOk    = sampledSpawn && allFinite && neverTunneled &&
                           (groundedSteps >= 20u || hero.inWater) && (walkedDist > 30.0f);
    const bool ok        = okHf && okBvh && hitRateOk && walkOk;

    std::printf(
        "[%s] physics-probe (%d,%d): tris=%u bvhNodes=%u hfs=%u | rays=%u/%u (prop=%u terrain=%u, valid=%s) | "
        "walk=%u/%u grounded (dist=%.1fu, finalZ=%.1f, finite=%s, noTunnel=%s) | %.1f ms\n",
        ok ? "PASS" : "FAIL",
        cellX, cellY,
        stats.totalTriangles, stats.totalBvhNodes, stats.heightfieldCount,
        totalHits, kTotalRays, propHits, terrainHits, allHitsValid ? "yes" : "no",
        groundedSteps, kWalkSteps, walkedDist, hero.position.z,
        allFinite ? "yes" : "no", neverTunneled ? "yes" : "no",
        elapsedMs);
    return ok;
}

bool RunTamrielStreamAudit(const std::string& esmPath, const std::string& explicitBsa) {
    const std::string bsaPath = ResolveSiblingMeshesBsa(esmPath, explicitBsa);
    const auto tInit0 = std::chrono::steady_clock::now();

    raw::worldgen::TamrielStreamConfig cfg{};
    cfg.radiusCells            = 1;
    cfg.maxCellsPerUpdate      = 9u;
    cfg.maxUniqueModelsPerCell = 32u;
    cfg.vramBudgetBytes        = 128ull * 1024ull * 1024ull;
    cfg.modelCacheBudgetBytes  = 64ull * 1024ull * 1024ull;
    cfg.bakeCellProps          = true;

    raw::worldgen::RawTamrielWorldStreamer streamer{};
    std::string err;
    if (!streamer.Init(esmPath, bsaPath, cfg, &err)) {
        std::fprintf(stderr, "[FAIL] tamriel-stream: Init('%s') failed: %s\n", esmPath.c_str(), err.c_str());
        return false;
    }
    const double initMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - tInit0).count();

    struct Landmark {
        const char* name;
        int32_t     cellX;
        int32_t     cellY;
    };
    static const Landmark kLandmarks[3] = {
        {"Riverwood",     4, -12},
        {"Whiterun",      5,  -3},
        {"High Hrothgar", 14, -14},
    };

    std::printf("[INFO] Tamriel Multi-Cell Streamer Audit (indexed %zu cells, %zu models in %.1f ms)\n",
                streamer.WorldIndex().cells.size(),
                streamer.ModelIndex().byFormId.size(),
                initMs);

    bool allLandmarksOk = true;
    raw::worldgen::TamrielStreamStats lastSt{};
    for (const auto& lm : kLandmarks) {
        const raw::worldgen::Vec3 pos{
            (static_cast<float>(lm.cellX) + 0.5f) * 4096.0f,
            (static_cast<float>(lm.cellY) + 0.5f) * 4096.0f,
            0.0f
        };
        const auto t0 = std::chrono::steady_clock::now();
        lastSt = streamer.StreamRingComplete(pos, nullptr);
        const double stepMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();

        const uint64_t totalLookups = lastSt.cacheHits + lastSt.cacheMisses;
        const double hitRatePct = (totalLookups > 0ull)
            ? (static_cast<double>(lastSt.cacheHits) * 100.0 / static_cast<double>(totalLookups))
            : 0.0;
        const double vramMB = static_cast<double>(lastSt.usedVramBytes) / (1024.0 * 1024.0);
        const double budgMB = static_cast<double>(lastSt.vramBudgetBytes) / (1024.0 * 1024.0);

        std::printf(
            "  %-14s (%3d,%3d): activeCells=%u | terrainMsh=%u propMsh=%u propInst=%u | "
            "VRAM=%.2f/%.0f MB | nifCache=%u | LRU hit=%.1f%% (%llu/%llu) | %.1f ms\n",
            lm.name, lm.cellX, lm.cellY,
            lastSt.activeCells,
            lastSt.totalTerrainMeshlets,
            lastSt.totalPropMeshlets,
            lastSt.totalPropInstances,
            vramMB, budgMB,
            lastSt.cachedNifModels,
            hitRatePct,
            static_cast<unsigned long long>(lastSt.cacheHits),
            static_cast<unsigned long long>(totalLookups),
            stepMs);

        if (lastSt.activeCells < 9u || lastSt.usedVramBytes > lastSt.vramBudgetBytes) {
            allLandmarksOk = false;
        }
    }

    const uint64_t finalLookups = lastSt.cacheHits + lastSt.cacheMisses;
    const double finalHitPct = (finalLookups > 0ull)
        ? (static_cast<double>(lastSt.cacheHits) * 100.0 / static_cast<double>(finalLookups))
        : 0.0;
    const bool ok = allLandmarksOk && (lastSt.cacheHits > 0ull) && (lastSt.totalTerrainMeshlets > 0u);
    std::printf(
        "[%s] tamriel-stream: 3 landmarks streamed | finalActiveCells=%u propMeshlets=%u | "
        "VRAM=%.2f MB | cumulative LRU hit=%.1f%% (hits=%llu miss=%llu)\n",
        ok ? "PASS" : "FAIL",
        lastSt.activeCells,
        lastSt.totalPropMeshlets,
        static_cast<double>(lastSt.usedVramBytes) / (1024.0 * 1024.0),
        finalHitPct,
        static_cast<unsigned long long>(lastSt.cacheHits),
        static_cast<unsigned long long>(lastSt.cacheMisses));
    return ok;
}

bool RunNavMeshAudit(const std::string& esmPath, const std::string& explicitBsa,
                     int32_t cellX, int32_t cellY, uint32_t maxModels) {
    const std::string bsaPath = ResolveSiblingMeshesBsa(esmPath, explicitBsa);
    const auto t0 = std::chrono::steady_clock::now();

    raw::worldgen::ReconstructedCellScene scene{};
    std::string err;
    if (!raw::worldgen::ReconstructTamrielCellScene(esmPath, bsaPath, cellX, cellY, maxModels, &scene, &err)) {
        std::fprintf(stderr, "[FAIL] navmesh-audit: ReconstructTamrielCellScene(%d,%d) failed: %s\n",
                     cellX, cellY, err.c_str());
        return false;
    }

    raw::worldgen::ProceduralMesh bakedProps{};
    const bool hasProps = raw::worldgen::BakeReconstructedCellPropsToWorldMesh(scene, 126u, &bakedProps, &err);

    raw::worldgen::RawWorldPhysics phys{};
    const uint64_t cellKey = raw::worldgen::PackCellKey(cellX, cellY);
    const bool okHf = phys.AddTerrainHeightfield(
        raw::worldgen::MakeHeightfieldFromTerrainChunk(scene.terrainChunk, scene.waterHeightZ, 1u));
    bool okBvh = true;
    if (hasProps && !bakedProps.vertices.empty() && !bakedProps.indices.empty()) {
        okBvh = phys.AddCellCollider(cellKey, bakedProps, 1001u, 2u);
    }

    raw::worldgen::RawNavMesh nav{};
    raw::worldgen::NavBakeConfig bcfg{};
    bcfg.originX   = scene.terrainChunk.originX;
    bcfg.originY   = scene.terrainChunk.originY;
    bcfg.sizeX     = scene.terrainChunk.chunkSizeU;
    bcfg.sizeY     = scene.terrainChunk.chunkSizeU;
    bcfg.cellSizeU = 64.0f;
    const bool okBake = okHf && okBvh && nav.BakeFromPhysics(phys, bcfg);
    const auto st = nav.GetStats();

    // Pick two well-separated connected walkable ground cells across the village for cross-cell A* + funnel smoothing
    raw::worldgen::Vec3 pathStart{};
    raw::worldgen::Vec3 pathGoal{};
    bool pickedStart = false;
    bool pickedGoal  = false;
    const int32_t gw = static_cast<int32_t>(st.gridW);
    const int32_t gh = static_cast<int32_t>(st.gridH);

    // Sample walkable anchors across a 6x6 grid covering the cell so rivers/cliffs never stall path selection:
    std::vector<raw::worldgen::Vec3> anchors;
    for (int32_t iy = 0; iy < 6; ++iy) {
        const int32_t gyBase = 4 + (iy * (gh - 9)) / 5;
        for (int32_t ix = 0; ix < 6; ++ix) {
            const int32_t gxBase = 4 + (ix * (gw - 9)) / 5;
            const auto* c = nav.GetCell(gxBase, gyBase);
            if (c && c->tag == raw::worldgen::NavCellTag::Walkable && c->passableNeighborMask != 0u) {
                anchors.push_back(nav.GridToWorldCenter(gxBase, gyBase));
            }
        }
    }

    // Pick the longest connected cross-village A* path (> 1000u) between well-separated walkable anchors:
    float bestLenU = 0.0f;
    for (size_t i = 0u; i < anchors.size(); ++i) {
        for (size_t j = anchors.size(); j-- > i + 1u;) {
            const float dx = anchors[j].x - anchors[i].x;
            const float dy = anchors[j].y - anchors[i].y;
            const float euclid = std::sqrt(dx * dx + dy * dy);
            if (euclid <= std::max(1000.0f, bestLenU * 0.85f)) continue;
            const auto probe = nav.FindPath(anchors[i], anchors[j], &phys);
            if (probe.found && probe.totalLengthU > bestLenU) {
                bestLenU    = probe.totalLengthU;
                pathStart   = anchors[i];
                pathGoal    = anchors[j];
                pickedStart = true;
                pickedGoal  = true;
            }
        }
        if (bestLenU > 2200.0f) break;
    }

    raw::worldgen::NavPathResult path{};
    if (pickedStart && pickedGoal) {
        path = nav.FindPath(pathStart, pathGoal, &phys);
    }
    bool allWpFinite = path.found && (path.waypoints.size() >= 2u);
    for (const auto& wp : path.waypoints) {
        if (!IsFinite3(wp.position)) allWpFinite = false;
    }

    // Spawn 4-agent tactical squad (2 MeleeFlanker, 1 RangedArcher, 1 Spellcaster) around a central walkable threat
    raw::worldgen::Vec3 threatPos = pathStart;
    int32_t tGx = 0, tGy = 0;
    const raw::worldgen::Vec3 midPt{
        bcfg.originX + bcfg.sizeX * 0.5f,
        bcfg.originY + bcfg.sizeY * 0.5f,
        0.0f
    };
    if (nav.FindNearestWalkableCell(midPt, 16, &tGx, &tGy)) {
        threatPos = nav.GridToWorldCenter(tGx, tGy);
    }

    raw::worldgen::ThreatTarget threat{};
    threat.position = threatPos;
    threat.facing   = {1.0f, 0.0f, 0.0f};

    auto spawnNear = [&](float dx, float dy) -> raw::worldgen::Vec3 {
        raw::worldgen::Vec3 p{threatPos.x + dx, threatPos.y + dy, threatPos.z};
        float tz = threatPos.z;
        if (phys.SampleTerrainHeight(p.x, p.y, &tz, nullptr)) {
            p.z = tz;
        }
        return p;
    };

    std::vector<raw::worldgen::SquadAgent> squad(4u);
    squad[0].id        = 1u;
    squad[0].archetype = raw::worldgen::TacticalArchetype::MeleeFlanker;
    squad[0].position  = spawnNear(240.0f, -90.0f);
    squad[1].id        = 2u;
    squad[1].archetype = raw::worldgen::TacticalArchetype::MeleeFlanker;
    squad[1].position  = spawnNear(220.0f, 110.0f);
    squad[2].id        = 3u;
    squad[2].archetype = raw::worldgen::TacticalArchetype::RangedArcher;
    squad[2].position  = spawnNear(-680.0f, -120.0f);
    squad[3].id        = 4u;
    squad[3].archetype = raw::worldgen::TacticalArchetype::Spellcaster;
    squad[3].position  = spawnNear(-640.0f, 180.0f);

    std::vector<raw::worldgen::TacticalSteeringOutput> steerOut;
    bool squadAllFinite = true;
    constexpr uint32_t kSquadSteps = 30u;
    for (uint32_t s = 0u; s < kSquadSteps; ++s) {
        steerOut = raw::worldgen::StepSquadTactics(squad, threat, phys, &nav, 1.0f / 60.0f);
        for (const auto& ag : squad) {
            if (!IsFinite3(ag.position) || !IsFinite3(ag.velocity)) {
                squadAllFinite = false;
            }
        }
    }

    auto angleToThreatDeg = [&](const raw::worldgen::Vec3& p) -> float {
        return std::atan2(p.y - threat.position.y, p.x - threat.position.x) * 57.2957795f;
    };
    float flankDeltaDeg = std::fabs(angleToThreatDeg(squad[1].position) - angleToThreatDeg(squad[0].position));
    if (flankDeltaDeg > 180.0f) flankDeltaDeg = 360.0f - flankDeltaDeg;

    auto distXY = [&](const raw::worldgen::Vec3& p) -> float {
        const float dx = p.x - threat.position.x;
        const float dy = p.y - threat.position.y;
        return std::sqrt(dx * dx + dy * dy);
    };
    const float archerDist = distXY(squad[2].position);
    const float casterDist = distXY(squad[3].position);
    const bool rangedStandoffOk = (archerDist >= 350.0f && archerDist <= 1350.0f) &&
                                  (casterDist >= 350.0f && casterDist <= 1350.0f);
    const bool flankSpreadOk = (flankDeltaDeg > 20.0f);
    const bool steerValid = (steerOut.size() == 4u);
    const bool anyRangedLos = steerValid && (steerOut[2].hasLineOfSight || steerOut[3].hasLineOfSight ||
                                             steerOut[2].repositioningForLos || steerOut[3].repositioningForLos);

    const double totalMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();

    const bool ok = okBake && (st.walkableCells > 100u) && (st.totalLinks > 200u) &&
                    pickedStart && pickedGoal && path.found && allWpFinite &&
                    squadAllFinite && steerValid && flankSpreadOk && rangedStandoffOk && anyRangedLos;

    std::printf(
        "[INFO] NavMesh Audit (%d,%d): grid=%ux%u bake=%.2f ms | walkable=%u steep=%u water=%u blocked=%u links=%u\n"
        "  A* Funnel Path : raw=%u -> smooth=%zu waypoints | len=%.1fu | expanded=%u | search=%.3f ms\n"
        "  4-Agent Squad  : 30 steps | flankSpread=%.1f deg | archer=%.0fu (LOS=%d) | caster=%.0fu (LOS=%d) | finite=%s\n"
        "[%s] navmesh-audit (%d,%d): bake=%.2f ms, path=%u->%zu wps (%.1fu), squad=4 agents PASS (total=%.1f ms)\n",
        cellX, cellY, st.gridW, st.gridH, st.bakeTimeMs,
        st.walkableCells, st.steepCells, st.shallowWaterCells, st.blockedCells, st.totalLinks,
        path.rawGridNodes, path.waypoints.size(), path.totalLengthU, path.expandedNodes, path.searchTimeMs,
        flankDeltaDeg,
        archerDist, steerValid && steerOut[2].hasLineOfSight ? 1 : 0,
        casterDist, steerValid && steerOut[3].hasLineOfSight ? 1 : 0,
        squadAllFinite ? "yes" : "no",
        ok ? "PASS" : "FAIL",
        cellX, cellY, st.bakeTimeMs, path.rawGridNodes, path.waypoints.size(), path.totalLengthU, totalMs);
    return ok;
}

bool RunEcosystemSimAudit() {
    const auto t0 = std::chrono::steady_clock::now();

    // 1. Downwind & Upslope Wildfire Spread (eastReach > westReach)
    int32_t maxEastReach = 0;
    int32_t maxWestReach = 0;
    float   fireBurnedM2 = 0.0f;
    float   firePeakTempC = 0.0f;
    bool    fireOk = false;
    {
        raw::worldgen::EcoGridConfig cfg{};
        cfg.gridW     = 64u;
        cfg.gridH     = 64u;
        cfg.cellSizeM = 2.0f;
        raw::worldgen::RawWorldEcosystem eco(cfg);
        for (uint32_t y = 0u; y < 64u; ++y) {
            for (uint32_t x = 0u; x < 64u; ++x) {
                auto* c = eco.CellAt(x, y);
                c->elevationM = static_cast<float>(x) * 0.45f;
                c->fuelLoad01 = 0.85f;
                c->moisture01 = 0.12f;
            }
        }
        const uint32_t ox = 22u, oy = 32u;
        const auto pos = eco.CellCenterWorldM(ox, oy);
        const uint32_t ign = eco.IgniteAt(pos.x, pos.y, 3.0f, 550.0f);

        raw::worldgen::EcoWeatherInput gale{};
        gale.ambientTempC      = 24.0f;
        gale.windDirX          = 1.0f;
        gale.windDirY          = 0.0f;
        gale.windBeaufort      = 7.0f;
        gale.solarInsolation01 = 0.85f;
        for (int s = 0; s < 60; ++s) {
            eco.Step(0.25f, gale);
        }
        for (uint32_t y = 0u; y < 64u; ++y) {
            for (uint32_t x = 0u; x < 64u; ++x) {
                const auto* c = eco.CellAt(x, y);
                if (c->char01 > 0.10f || c->fireIntensity01 > 0.10f) {
                    const int32_t dx = static_cast<int32_t>(x) - static_cast<int32_t>(ox);
                    if (dx > maxEastReach) maxEastReach = dx;
                    if (-dx > maxWestReach) maxWestReach = -dx;
                }
            }
        }
        const auto tel = eco.ComputeTelemetry();
        fireBurnedM2  = tel.burnedAreaM2;
        firePeakTempC = tel.peakTempC;
        fireOk = (ign >= 1u) && (maxEastReach > maxWestReach) && (maxEastReach >= 6) &&
                 (fireBurnedM2 > 60.0f) && (firePeakTempC > 400.0f);
    }

    // 2. Heavy Rain Wildfire Suppression (activeFire -> 0, steamFlux > 0)
    uint32_t rainFireBefore = 0u;
    uint32_t rainFireAfter  = 0u;
    float    rainPeakSteam  = 0.0f;
    bool     rainOk         = false;
    {
        raw::worldgen::EcoGridConfig cfg{};
        cfg.gridW     = 32u;
        cfg.gridH     = 32u;
        cfg.cellSizeM = 2.0f;
        raw::worldgen::RawWorldEcosystem eco(cfg);
        const auto ctr = eco.CellCenterWorldM(16u, 16u);
        eco.IgniteAt(ctr.x, ctr.y, 5.0f, 600.0f);

        raw::worldgen::EcoWeatherInput dry{};
        dry.ambientTempC = 20.0f;
        dry.windBeaufort = 3.0f;
        for (int s = 0; s < 10; ++s) eco.Step(0.25f, dry);
        rainFireBefore = eco.ComputeTelemetry().activeFireCells;

        raw::worldgen::EcoWeatherInput storm{};
        storm.ambientTempC      = 9.0f;
        storm.windBeaufort      = 4.0f;
        storm.rainRate01        = 0.90f;
        storm.solarInsolation01 = 0.10f;
        for (int s = 0; s < 40; ++s) {
            eco.Step(0.25f, storm);
            rainPeakSteam = std::max(rainPeakSteam, eco.ComputeTelemetry().steamFlux);
        }
        rainFireAfter = eco.ComputeTelemetry().activeFireCells;
        rainOk = (rainFireBefore >= 4u) && (rainFireAfter == 0u) && (rainPeakSteam > 1.0f);
    }

    // 3. Snowmelt D8 Downhill Runoff (exact water mass conservation) + Sub-Zero -8 C Night Black-Ice Refreeze
    float    snowVolBefore    = 0.0f;
    float    snowVolAfter     = 0.0f;
    float    downhillWaterM3  = 0.0f;
    float    waterMassErrRel  = 1.0f;
    float    peakBlackIce     = 0.0f;
    uint32_t blackIceCells    = 0u;
    bool     hydroIceOk       = false;
    {
        raw::worldgen::EcoGridConfig cfg{};
        cfg.gridW     = 32u;
        cfg.gridH     = 32u;
        cfg.cellSizeM = 2.0f;
        raw::worldgen::RawWorldEcosystem eco(cfg);
        for (uint32_t y = 0u; y < 32u; ++y) {
            for (uint32_t x = 0u; x < 32u; ++x) {
                auto* c = eco.CellAt(x, y);
                c->elevationM   = static_cast<float>(x) * 0.9f;
                c->surfaceTempC = -1.0f;
                c->fuelLoad01   = 0.70f;
                c->moisture01   = 0.18f;
                if (x >= 18u && x <= 26u && y >= 10u && y <= 22u) {
                    c->snowDepthM = 0.35f;
                }
            }
        }
        const auto initTel = eco.ComputeTelemetry();
        snowVolBefore = initTel.totalSnowVolumeM3;
        const float initWaterMassM3 = initTel.totalWaterMassM3;

        const auto brushPos = eco.CellCenterWorldM(28u, 16u);
        eco.IgniteAt(brushPos.x, brushPos.y, 4.0f, 600.0f);

        raw::worldgen::EcoWeatherInput springMelt{};
        springMelt.ambientTempC      = 9.0f;
        springMelt.windDirX          = -1.0f;
        springMelt.windBeaufort      = 4.0f;
        springMelt.solarInsolation01 = 0.80f;
        for (int s = 0; s < 60; ++s) eco.Step(0.25f, springMelt);

        const auto meltTel = eco.ComputeTelemetry();
        snowVolAfter = meltTel.totalSnowVolumeM3;
        waterMassErrRel = std::fabs(meltTel.totalWaterMassM3 - initWaterMassM3) /
                          std::max(1.0e-4f, initWaterMassM3);
        for (uint32_t y = 8u; y <= 24u; ++y) {
            for (uint32_t x = 0u; x < 18u; ++x) {
                downhillWaterM3 += eco.CellAt(x, y)->meltWaterM * (cfg.cellSizeM * cfg.cellSizeM);
            }
        }

        raw::worldgen::EcoWeatherInput freezeNight{};
        freezeNight.ambientTempC      = -8.0f;
        freezeNight.windBeaufort      = 2.0f;
        freezeNight.solarInsolation01 = 0.0f;
        for (int s = 0; s < 120; ++s) eco.Step(0.25f, freezeNight);

        for (uint32_t y = 8u; y <= 24u; ++y) {
            for (uint32_t x = 0u; x < 18u; ++x) {
                peakBlackIce = std::max(peakBlackIce, eco.CellAt(x, y)->blackIce01);
            }
        }
        blackIceCells = eco.ComputeTelemetry().blackIceCells;
        hydroIceOk = (snowVolAfter < snowVolBefore * 0.90f) && (downhillWaterM3 > 0.5f) &&
                     (waterMassErrRel < 1.0e-4f) && (peakBlackIce > 0.50f) && (blackIceCells >= 10u);
    }

    // 4. 90-Day Spring Biomass Regrowth (fuel recovery & char decay)
    float fuelBefore = 0.0f, fuelAfter = 0.0f;
    float charBefore = 0.0f, charAfter = 0.0f;
    bool  regrowthOk = false;
    {
        raw::worldgen::EcoGridConfig cfg{};
        cfg.gridW     = 16u;
        cfg.gridH     = 16u;
        cfg.cellSizeM = 2.0f;
        raw::worldgen::RawWorldEcosystem eco(cfg);
        for (uint32_t y = 0u; y < 16u; ++y) {
            for (uint32_t x = 0u; x < 16u; ++x) {
                auto* c = eco.CellAt(x, y);
                c->fuelLoad01 = 0.05f;
                c->char01     = 0.95f;
                c->moisture01 = 0.05f;
            }
        }
        fuelBefore = eco.CellAt(8u, 8u)->fuelLoad01;
        charBefore = eco.CellAt(8u, 8u)->char01;
        eco.AdvanceSeasonDays(90.0f);
        fuelAfter = eco.CellAt(8u, 8u)->fuelLoad01;
        charAfter = eco.CellAt(8u, 8u)->char01;
        regrowthOk = (fuelAfter > fuelBefore + 0.45f) && (charAfter < charBefore * 0.25f);
    }

    const double elapsedMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    const bool ok = fireOk && rainOk && hydroIceOk && regrowthOk;

    std::printf(
        "[INFO] Ecosystem Coupled Simulation Audit (%.2f ms):\n"
        "  1. Wildfire Spread     : eastReach=%d > westReach=%d cells | burned=%.1f m2 | peakTemp=%.1f C [%s]\n"
        "  2. Rain Suppression    : activeFire %u -> %u | peakSteamFlux=%.1f [%s]\n"
        "  3. Snowmelt->BlackIce  : snow %.1f->%.1f m3 | downhill=%.2f m3 | massErr=%.2e | -8C blackIce=%.2f (%u cells) [%s]\n"
        "  4. 90-Day Regrowth     : fuel %.2f -> %.2f | char %.2f -> %.2f [%s]\n"
        "[%s] ecosystem-sim: wildfire/rain/snowmelt-D8/black-ice/regrowth audit %s (%.2f ms)\n",
        elapsedMs,
        maxEastReach, maxWestReach, fireBurnedM2, firePeakTempC, fireOk ? "PASS" : "FAIL",
        rainFireBefore, rainFireAfter, rainPeakSteam, rainOk ? "PASS" : "FAIL",
        snowVolBefore, snowVolAfter, downhillWaterM3, waterMassErrRel, peakBlackIce, blackIceCells, hydroIceOk ? "PASS" : "FAIL",
        fuelBefore, fuelAfter, charBefore, charAfter, regrowthOk ? "PASS" : "FAIL",
        ok ? "PASS" : "FAIL", ok ? "PASS" : "FAIL", elapsedMs);
    return ok;
}

bool RunWildlifeSimAudit() {
    const auto t0 = std::chrono::steady_clock::now();

    auto makeFlatHf = [](uint64_t key, float ox, float oy, float sx, float sy, float baseZ, float waterZ) {
        raw::worldgen::TerrainCellHeightfield hf{};
        hf.cellKey     = key;
        hf.originX     = ox;
        hf.originY     = oy;
        hf.sizeX       = sx;
        hf.sizeY       = sy;
        hf.gridW       = 17u;
        hf.gridH       = 17u;
        hf.waterLevelZ = waterZ;
        hf.heights.assign(17u * 17u, baseZ);
        return hf;
    };

    // 1. AlpineHawk thermal soaring vs blizzard roosting
    float hawkStartZ = 0.0f, hawkThermalZ = 0.0f, hawkRoostZ = 0.0f;
    uint32_t hawkRoostCount = 0u;
    bool hawkOk = false;
    {
        raw::worldgen::RawWorldPhysics phys;
        phys.AddTerrainHeightfield(makeFlatHf(1ull, -4096.0f, -4096.0f, 8192.0f, 8192.0f, 0.0f, -500.0f));
        raw::worldgen::RawWildlifeBoids hawks;
        hawks.SpawnGuild(raw::worldgen::FaunaGuild::AlpineHawk, 16u, {0.0f, 0.0f, 750.0f}, 220.0f, 42u);
        auto meanZ = [&]() {
            double s = 0.0;
            for (const auto& a : hawks.Agents()) s += a.position.z;
            return static_cast<float>(s / static_cast<double>(hawks.Agents().size()));
        };
        hawkStartZ = meanZ();
        raw::worldgen::WildlifeEnvInput sunny{};
        sunny.solarInsolation01 = 1.0f;
        sunny.windBeaufort      = 2.0f;
        for (int f = 0; f < 180; ++f) hawks.Step(1.0f / 60.0f, sunny, &phys);
        hawkThermalZ = meanZ();

        raw::worldgen::WildlifeEnvInput bliz{};
        bliz.solarInsolation01 = 0.0f;
        bliz.snowIntensity01   = 0.95f;
        bliz.windBeaufort      = 10.0f;
        raw::worldgen::WildlifeTelemetry tel{};
        for (int f = 0; f < 360; ++f) tel = hawks.Step(1.0f / 60.0f, bliz, &phys);
        hawkRoostZ     = meanZ();
        hawkRoostCount = tel.roostingCount;
        hawkOk = (hawkThermalZ > hawkStartZ + 120.0f) && (hawkRoostZ < hawkThermalZ - 350.0f) && (hawkRoostCount >= 12u);
    }

    // 2. PineCrowFlock explosive acoustic flush (EmitStimulus(LoudNoise))
    float crowPreZ = 0.0f, crowPostZ = 0.0f, crowPreSpd = 0.0f, crowPostSpd = 0.0f;
    uint32_t crowStartled = 0u;
    bool crowOk = false;
    {
        raw::worldgen::RawWorldPhysics phys;
        phys.AddTerrainHeightfield(makeFlatHf(1ull, -4096.0f, -4096.0f, 8192.0f, 8192.0f, 0.0f, -500.0f));
        raw::worldgen::RawWildlifeBoids crows;
        crows.SpawnGuild(raw::worldgen::FaunaGuild::PineCrowFlock, 24u, {0.0f, 0.0f, 280.0f}, 140.0f, 77u);
        auto meanZ = [&]() {
            double s = 0.0;
            for (const auto& a : crows.Agents()) s += a.position.z;
            return static_cast<float>(s / static_cast<double>(crows.Agents().size()));
        };
        raw::worldgen::WildlifeEnvInput calm{};
        calm.solarInsolation01 = 0.6f;
        raw::worldgen::WildlifeTelemetry preTel{};
        for (int f = 0; f < 30; ++f) preTel = crows.Step(1.0f / 60.0f, calm, &phys);
        crowPreZ   = meanZ();
        crowPreSpd = preTel.meanSpeed;

        crows.EmitStimulus({0.0f, 0.0f, 250.0f}, 1200.0f, raw::worldgen::StimulusKind::LoudNoise, 1.0f, 2.5f);
        raw::worldgen::WildlifeTelemetry postTel{};
        for (int f = 0; f < 45; ++f) postTel = crows.Step(1.0f / 60.0f, calm, &phys);
        crowPostZ    = meanZ();
        crowPostSpd  = postTel.meanSpeed;
        crowStartled = postTel.startledCount;
        crowOk = (crowStartled == 24u) && (crowPostZ > crowPreZ + 120.0f) && (crowPostSpd > crowPreSpd * 1.25f);
    }

    // 3. TundraDeerHerd grounded upwind evasion from live RawWorldEcosystem wildfire
    float deerBefore = 0.0f, deerAfter = 0.0f;
    bool deerGrounded = true;
    bool deerOk = false;
    {
        raw::worldgen::RawWorldPhysics phys;
        phys.AddTerrainHeightfield(makeFlatHf(1ull, -4096.0f, -4096.0f, 8192.0f, 8192.0f, 120.0f, -500.0f));
        raw::worldgen::EcoGridConfig ecfg{};
        ecfg.gridW = 32u; ecfg.gridH = 32u; ecfg.cellSizeM = 2.0f;
        raw::worldgen::RawWorldEcosystem eco(ecfg);
        const auto fireM = eco.CellCenterWorldM(16u, 16u);
        eco.IgniteAt(fireM.x, fireM.y, 6.0f, 600.0f);
        const raw::worldgen::Vec3 fireW{fireM.x * 70.0f, fireM.y * 70.0f, 120.0f};

        raw::worldgen::RawWildlifeBoids deer;
        deer.SpawnGuild(raw::worldgen::FaunaGuild::TundraDeerHerd, 16u, {fireW.x + 450.0f, fireW.y + 100.0f, 120.0f}, 90.0f, 99u);
        auto meanDist = [&]() {
            double s = 0.0;
            for (const auto& a : deer.Agents()) {
                const float dx = a.position.x - fireW.x;
                const float dy = a.position.y - fireW.y;
                s += std::sqrt(dx * dx + dy * dy);
            }
            return static_cast<float>(s / static_cast<double>(deer.Agents().size()));
        };
        deerBefore = meanDist();
        raw::worldgen::WildlifeEnvInput env{};
        env.ecoUnitsPerMeter = 70.0f;
        env.windDirXY        = {1.0f, 0.0f};
        for (int f = 0; f < 120; ++f) {
            deer.Step(1.0f / 60.0f, env, &phys, &eco);
            for (const auto& a : deer.Agents()) {
                if (std::fabs(a.position.z - 120.0f) > 1.0e-2f || !IsFinite3(a.position)) deerGrounded = false;
            }
        }
        deerAfter = meanDist();
        deerOk = deerGrounded && (deerAfter > deerBefore + 150.0f);
    }

    // 4. RiverSalmonSchool depth confinement + 512-agent O(N) < 1.0 ms/frame perf check
    double swarmMeanMs = 0.0;
    uint32_t swarmCount = 0u;
    bool salmonConfined = true;
    bool swarmFinite    = true;
    bool swarmOk        = false;
    {
        raw::worldgen::RawWorldPhysics phys;
        phys.AddTerrainHeightfield(makeFlatHf(1ull, -8192.0f, -8192.0f, 16384.0f, 16384.0f, -200.0f, 0.0f));
        raw::worldgen::RawWildlifeBoids swarm;
        swarm.SpawnGuild(raw::worldgen::FaunaGuild::AlpineHawk,        64u,  {0.0f, 0.0f, 900.0f},    800.0f, 101u);
        swarm.SpawnGuild(raw::worldgen::FaunaGuild::PineCrowFlock,     160u, {300.0f, 200.0f, 260.0f}, 500.0f, 102u);
        swarm.SpawnGuild(raw::worldgen::FaunaGuild::TundraDeerHerd,    128u, {-400.0f, 300.0f, -200.0f}, 600.0f, 103u);
        swarm.SpawnGuild(raw::worldgen::FaunaGuild::RiverSalmonSchool, 160u, {0.0f, 0.0f, -90.0f},    350.0f, 104u);
        swarmCount = static_cast<uint32_t>(swarm.Agents().size());

        raw::worldgen::WildlifeEnvInput env{};
        env.defaultWaterLevelZ = 0.0f;
        env.riverCurrentXY     = {1.0f, 0.0f};
        double sumMs = 0.0;
        constexpr int kFrames = 40;
        for (int f = 0; f < kFrames; ++f) {
            const auto tel = swarm.Step(1.0f / 60.0f, env, &phys);
            if (f >= 5) sumMs += tel.stepMs;
            for (const auto& a : swarm.Agents()) {
                if (!IsFinite3(a.position) || !IsFinite3(a.velocity)) swarmFinite = false;
                if (a.guild == raw::worldgen::FaunaGuild::RiverSalmonSchool) {
                    if (a.position.z < -190.0f || a.position.z > -10.0f) salmonConfined = false;
                }
            }
        }
        swarmMeanMs = sumMs / static_cast<double>(kFrames - 5);
        swarmOk = (swarmCount == 512u) && salmonConfined && swarmFinite && (swarmMeanMs < 1.0);
    }

    const double totalMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    const bool ok = hawkOk && crowOk && deerOk && swarmOk;

    std::printf(
        "[INFO] Wildlife 3D Spatial-Hash Boids Audit (%.2f ms):\n"
        "  1. AlpineHawk Thermal/Roost : Z %.1f -> %.1f -> %.1f (roosting=%u/16) [%s]\n"
        "  2. PineCrow Acoustic Flush  : Z %.1f -> %.1f | spd %.1f -> %.1f | startled=%u/24 [%s]\n"
        "  3. TundraDeer Fire Evasion  : dist %.1fu -> %.1fu | grounded=%s [%s]\n"
        "  4. 512-Agent Swarm & Salmon : agents=%u | meanStep=%.3f ms (<1.0ms) | salmonBox=%s | finite=%s [%s]\n"
        "[%s] wildlife-sim: 4 guilds / 512 agents O(N) spatial-hash audit %s (%.2f ms)\n",
        totalMs,
        hawkStartZ, hawkThermalZ, hawkRoostZ, hawkRoostCount, hawkOk ? "PASS" : "FAIL",
        crowPreZ, crowPostZ, crowPreSpd, crowPostSpd, crowStartled, crowOk ? "PASS" : "FAIL",
        deerBefore, deerAfter, deerGrounded ? "yes" : "no", deerOk ? "PASS" : "FAIL",
        swarmCount, swarmMeanMs, salmonConfined ? "yes" : "no", swarmFinite ? "yes" : "no", swarmOk ? "PASS" : "FAIL",
        ok ? "PASS" : "FAIL", ok ? "PASS" : "FAIL", totalMs);
    return ok;
}

bool RunSettlementSimAudit() {
    const auto t0 = std::chrono::steady_clock::now();

    // 1. Exact Transit Commodity & Gold Conservation across 200 hours (< 1e-4 relative error)
    double maxCommRelErr = 1.0;
    double goldRelErr    = 1.0;
    uint32_t consDeliv   = 0u;
    bool consOk          = false;
    {
        raw::worldgen::RawSettlementSim sim(2026u);
        std::array<double, raw::worldgen::kCommodityCount> initTotals{};
        for (size_t c = 0u; c < raw::worldgen::kCommodityCount; ++c) {
            initTotals[c] = sim.TotalWorldCommodityUnits(static_cast<raw::worldgen::CommodityKind>(c));
        }
        const double initGold = sim.ComputeWorldTelemetry().totalGold;

        raw::worldgen::SettlementWeatherMap frozenWx{};
        frozenWx.enableProductionConsumption = false;
        frozenWx.enableBanditAmbushes        = false;
        sim.StepHours(200.0f, frozenWx);

        maxCommRelErr = 0.0;
        for (size_t c = 0u; c < raw::worldgen::kCommodityCount; ++c) {
            const double now = sim.TotalWorldCommodityUnits(static_cast<raw::worldgen::CommodityKind>(c));
            const double err = std::fabs(now - initTotals[c]) / std::max(1.0, initTotals[c]);
            if (err > maxCommRelErr) maxCommRelErr = err;
        }
        const auto tel = sim.ComputeWorldTelemetry();
        consDeliv  = tel.deliveriesCompleted;
        goldRelErr = std::fabs(tel.totalGold - initGold) / std::max(1.0, initGold);
        consOk = (consDeliv >= 5u) && (maxCommRelErr < 1.0e-4) && (goldRelErr < 1.0e-4);
    }

    // 2. 72-Hour Windhelm/Winterhold/Dawnstar Blizzard Blockade -> Snowbound Caravans & Grain Price Spike
    uint32_t peakSnowbound = 0u;
    float clearGrainPrice  = 0.0f;
    float blizGrainPrice   = 0.0f;
    float clearTavern      = 0.0f;
    float blizTavern       = 0.0f;
    bool blockadeOk        = false;
    {
        raw::worldgen::RawSettlementSim clearSim(3030u);
        raw::worldgen::RawSettlementSim blizSim(3030u);

        raw::worldgen::SettlementWeatherMap clearWx{};
        clearWx.enableProductionConsumption = true;
        clearWx.enableBanditAmbushes        = false;

        raw::worldgen::SettlementWeatherMap blizWx = clearWx;
        blizWx.byHold[static_cast<size_t>(raw::worldgen::HoldId::Windhelm)].blizzard01   = 0.92f;
        blizWx.byHold[static_cast<size_t>(raw::worldgen::HoldId::Winterhold)].blizzard01 = 0.95f;
        blizWx.byHold[static_cast<size_t>(raw::worldgen::HoldId::Dawnstar)].blizzard01   = 0.88f;

        for (int h = 0; h < 72; ++h) {
            clearSim.StepHours(1.0f, clearWx);
            blizSim.StepHours(1.0f, blizWx);
            const uint32_t sb = blizSim.ComputeWorldTelemetry().snowboundCaravans;
            if (sb > peakSnowbound) peakSnowbound = sb;
        }
        clearGrainPrice = clearSim.GetHold(raw::worldgen::HoldId::Winterhold)->price[static_cast<size_t>(raw::worldgen::CommodityKind::Grain)];
        blizGrainPrice  = blizSim.GetHold(raw::worldgen::HoldId::Winterhold)->price[static_cast<size_t>(raw::worldgen::CommodityKind::Grain)];
        clearTavern     = clearSim.GetHold(raw::worldgen::HoldId::Winterhold)->tavernOccupancy01;
        blizTavern      = blizSim.GetHold(raw::worldgen::HoldId::Winterhold)->tavernOccupancy01;
        blockadeOk = (peakSnowbound > 0u) && (blizGrainPrice > clearGrainPrice * 1.05f) && (blizTavern > clearTavern);
    }

    // 3. 720-Hour (30-Day) 9-Hold Regional Economy Stability (deliveries > 10, zero NaN/negatives)
    uint32_t sim30Deliv = 0u;
    double   sim30Gold  = 0.0;
    float    sim30Price = 0.0f;
    const char* scarceHoldName = "Unknown";
    bool stable30Ok = false;
    {
        raw::worldgen::RawSettlementSim sim(4040u);
        raw::worldgen::SettlementWeatherMap wx{};
        wx.seasonDaylightHours = 11.0f;

        bool allValid = true;
        for (int day = 0; day < 30; ++day) {
            wx.byHold[static_cast<size_t>(raw::worldgen::HoldId::Winterhold)].blizzard01 = (day % 7 == 0) ? 0.75f : 0.15f;
            wx.byHold[static_cast<size_t>(raw::worldgen::HoldId::Riften)].rain01         = (day % 5 == 0) ? 0.60f : 0.10f;
            sim.StepHours(24.0f, wx);

            for (size_t i = 0u; i < raw::worldgen::kHoldCount; ++i) {
                const auto* h = sim.GetHold(static_cast<raw::worldgen::HoldId>(i));
                if (!h || !std::isfinite(h->treasuryGold) || h->treasuryGold < 0.0f) allValid = false;
                for (size_t c = 0u; c < raw::worldgen::kCommodityCount; ++c) {
                    if (!std::isfinite(h->stock[c]) || h->stock[c] < 0.0f ||
                        !std::isfinite(h->price[c]) || h->price[c] <= 0.0f) {
                        allValid = false;
                    }
                }
            }
        }
        const auto tel = sim.ComputeWorldTelemetry();
        sim30Deliv     = tel.deliveriesCompleted;
        sim30Gold      = tel.totalGold;
        sim30Price     = tel.meanPriceIndex;
        scarceHoldName = raw::worldgen::GetHoldName(tel.mostScarceHold);
        stable30Ok     = allValid && (sim30Deliv > 10u) && std::isfinite(sim30Price) &&
                         (sim30Price > 0.4f) && (sim30Price < 3.0f);
    }

    const double totalMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    const bool ok = consOk && blockadeOk && stable30Ok;

    std::printf(
        "[INFO] 9-Hold Living Settlement & Caravan Economy Audit (%.2f ms):\n"
        "  1. Transit Conservation (200h) : deliveries=%u | maxCommErr=%.2e | goldErr=%.2e [%s]\n"
        "  2. Blizzard Blockade (72h)     : peakSnowbound=%u | Winterhold grain %.2fg -> %.2fg | tavern %.2f -> %.2f [%s]\n"
        "  3. 30-Day 9-Hold Stability     : deliveries=%u | totalGold=%.0f | meanPriceIdx=%.2f | scarce=%s [%s]\n"
        "[%s] settlement-sim: 9-hold caravan economy & blizzard blockade audit %s (%.2f ms)\n",
        totalMs,
        consDeliv, maxCommRelErr, goldRelErr, consOk ? "PASS" : "FAIL",
        peakSnowbound, clearGrainPrice, blizGrainPrice, clearTavern, blizTavern, blockadeOk ? "PASS" : "FAIL",
        sim30Deliv, sim30Gold, sim30Price, scarceHoldName, stable30Ok ? "PASS" : "FAIL",
        ok ? "PASS" : "FAIL", ok ? "PASS" : "FAIL", totalMs);
    return ok;
}

bool RunEventsSimAudit() {
    const auto t0 = std::chrono::steady_clock::now();

    // 1. Blizzard Caravan Rescue Spawn & Cooldown Anti-Spam (0 duplicates while active or in cooldown)
    uint32_t rescueSpawnsActive = 0u;
    uint32_t rescueSpawnsCooldown = 0u;
    float    rescueRewardGold = 0.0f;
    float    cooldownLeftH = 0.0f;
    bool     rescueOk = false;
    {
        raw::worldgen::RawSettlementSim settle(202u);
        raw::worldgen::SettlementWeatherMap blizWx{};
        blizWx.byHold[static_cast<size_t>(raw::worldgen::HoldId::Windhelm)].blizzard01   = 0.92f;
        blizWx.byHold[static_cast<size_t>(raw::worldgen::HoldId::Winterhold)].blizzard01 = 0.95f;
        blizWx.byHold[static_cast<size_t>(raw::worldgen::HoldId::Dawnstar)].blizzard01   = 0.90f;
        settle.StepHours(12.0f, blizWx);

        raw::worldgen::RawWorldEventDirector dir{};
        raw::worldgen::WorldEventInputs in{};
        in.settlement     = &settle;
        in.timeOfDayHours = 12.0f;
        in.blizzard01     = 0.90f;

        dir.StepHours(1.0f, in);
        const auto* rescueEv = dir.FindActiveByKind(raw::worldgen::WorldEventKind::BlizzardCaravanRescue);
        const bool spawned   = (rescueEv != nullptr) && (rescueEv->rewardGold > 100.0f);
        const uint32_t rid   = rescueEv ? rescueEv->id : 0u;
        rescueRewardGold     = rescueEv ? rescueEv->rewardGold : 0.0f;

        for (int h = 0; h < 6; ++h) {
            settle.StepHours(1.0f, blizWx);
            dir.StepHours(1.0f, in);
        }
        rescueSpawnsActive = dir.ComputeTelemetry().spawnedByKind[static_cast<size_t>(raw::worldgen::WorldEventKind::BlizzardCaravanRescue)];

        const bool resolved = dir.ResolveEvent(rid);
        for (int h = 0; h < 6; ++h) {
            dir.StepHours(1.0f, in);
        }
        rescueSpawnsCooldown = dir.ComputeTelemetry().spawnedByKind[static_cast<size_t>(raw::worldgen::WorldEventKind::BlizzardCaravanRescue)];
        cooldownLeftH        = dir.CooldownRemainingHours(raw::worldgen::WorldEventKind::BlizzardCaravanRescue);
        rescueOk = spawned && resolved && (rescueSpawnsActive == 1u) && (rescueSpawnsCooldown == 1u);
    }

    // 2. Wildfire Containment Spawn & Heavy-Rain Auto-Resolution
    uint32_t fireAfterRain = 999u;
    uint32_t fireResolved  = 0u;
    bool     wildfireOk    = false;
    {
        raw::worldgen::EcoGridConfig ecfg{};
        ecfg.gridW = 32u; ecfg.gridH = 32u; ecfg.cellSizeM = 2.0f;
        raw::worldgen::RawWorldEcosystem eco(ecfg);
        const auto ctr = eco.CellCenterWorldM(16u, 16u);
        eco.IgniteAt(ctr.x, ctr.y, 7.0f, 650.0f);

        raw::worldgen::RawWorldEventDirector dir{};
        raw::worldgen::WorldEventInputs in{};
        in.ecosystem       = &eco;
        in.ecosystemHoldId = raw::worldgen::HoldId::Falkreath;
        in.timeOfDayHours  = 14.0f;

        dir.StepHours(1.0f, in);
        const auto* fireEv   = dir.FindActiveByKind(raw::worldgen::WorldEventKind::WildfireContainment);
        const bool spawned   = (fireEv != nullptr) && (fireEv->holdId == raw::worldgen::HoldId::Falkreath);

        raw::worldgen::EcoWeatherInput storm{};
        storm.ambientTempC = 8.0f;
        storm.rainRate01   = 0.95f;
        for (int s = 0; s < 50; ++s) eco.Step(0.25f, storm);

        dir.StepHours(1.0f, in);
        fireAfterRain = eco.ComputeTelemetry().activeFireCells;
        fireResolved  = dir.ComputeTelemetry().totalResolved;
        wildfireOk = spawned && (fireAfterRain == 0u) &&
                     (dir.FindActiveByKind(raw::worldgen::WorldEventKind::WildfireContainment) == nullptr) &&
                     (fireResolved >= 1u);
    }

    // 3. 720-Hour (30-Day) Coupled 9-Hold Emergent Event Run (exact lifecycle accounting & >= 4 distinct kinds)
    uint32_t totalSpawned  = 0u;
    uint32_t activeCount   = 0u;
    uint32_t totalResolved = 0u;
    uint32_t totalExpired  = 0u;
    uint32_t distinctKinds = 0u;
    bool     acctExact     = true;
    bool     coupled30Ok   = false;
    {
        raw::worldgen::RawSettlementSim settle(404u);
        raw::worldgen::RawWildlifeBoids boids;
        boids.SpawnGuild(raw::worldgen::FaunaGuild::PineCrowFlock, 24u, {0.0f, 0.0f, 300.0f}, 120.0f, 55u);
        boids.EmitStimulus({0.0f, 0.0f, 260.0f}, 1200.0f, raw::worldgen::StimulusKind::LoudNoise, 1.0f, 3.0f);
        raw::worldgen::WildlifeEnvInput wEnv{};
        boids.Step(0.05f, wEnv);

        raw::worldgen::RawWorldEventDirector dir{};
        raw::worldgen::SettlementWeatherMap wx{};
        bool allFinite = true;
        bool auroraHit = false;
        bool harvestHit = false;

        for (int h = 0; h < 720; ++h) {
            const int day = h / 24;
            const float tod = static_cast<float>(h % 24);
            const bool stormDay = (day % 6 == 2);
            wx.byHold[static_cast<size_t>(raw::worldgen::HoldId::Windhelm)].blizzard01   = stormDay ? 0.88f : 0.05f;
            wx.byHold[static_cast<size_t>(raw::worldgen::HoldId::Winterhold)].blizzard01 = stormDay ? 0.92f : 0.05f;
            settle.StepHours(1.0f, wx);

            raw::worldgen::WorldEventInputs in{};
            in.settlement     = &settle;
            in.wildlife       = (day == 0) ? &boids : nullptr;
            in.timeOfDayHours = tod;
            in.season         = (day < 15) ? raw::worldgen::SeasonId::Autumn : raw::worldgen::SeasonId::Winter;
            in.auroraKp       = (!stormDay && (day % 4 == 0)) ? 7.2f : 2.0f;
            in.cloudCover01   = stormDay ? 0.90f : 0.12f;
            in.blizzard01     = stormDay ? 0.85f : 0.05f;
            dir.StepHours(1.0f, in);

            if (dir.FindActiveByKind(raw::worldgen::WorldEventKind::AuroraCelestialGathering) != nullptr) auroraHit = true;
            if (dir.FindActiveByKind(raw::worldgen::WorldEventKind::SeasonalHarvestMarket) != nullptr) harvestHit = true;

            const auto tel = dir.ComputeTelemetry();
            if (tel.totalSpawned != tel.activeCount + tel.totalResolved + tel.totalExpired ||
                tel.activeCount > dir.Config().maxConcurrentEvents ||
                !std::isfinite(tel.highestUrgency)) {
                acctExact = false;
            }
            for (const auto& e : dir.ActiveEvents()) {
                if (!std::isfinite(e.urgency01) || !std::isfinite(e.rewardGold) || !std::isfinite(e.ageHours)) {
                    allFinite = false;
                }
            }
        }
        const auto finalTel = dir.ComputeTelemetry();
        totalSpawned  = finalTel.totalSpawned;
        activeCount   = finalTel.activeCount;
        totalResolved = finalTel.totalResolved;
        totalExpired  = finalTel.totalExpired;
        for (size_t k = 0u; k < raw::worldgen::kWorldEventKindCount; ++k) {
            if (finalTel.spawnedByKind[k] > 0u) ++distinctKinds;
        }
        coupled30Ok = acctExact && allFinite && auroraHit && harvestHit && (distinctKinds >= 4u) && (totalSpawned >= 15u);
    }

    const double totalMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    const bool ok = rescueOk && wildfireOk && coupled30Ok;

    std::printf(
        "[INFO] Emergent Radiant World-Event Director Audit (%.2f ms):\n"
        "  1. Blizzard Rescue Anti-Spam : reward=%.0fg | spawnsActive=%u spawnsCooldown=%u (cdLeft=%.1fh) [%s]\n"
        "  2. Wildfire Rain Auto-Resolve: fireAfterRain=%u | totalResolved=%u [%s]\n"
        "  3. 30-Day Coupled 9-Hold Run : spawned=%u (act=%u + res=%u + exp=%u) | kinds=%u/6 | exactAcct=%s [%s]\n"
        "[%s] events-sim: coupled radiant world-event director audit %s (%.2f ms)\n",
        totalMs,
        rescueRewardGold, rescueSpawnsActive, rescueSpawnsCooldown, cooldownLeftH, rescueOk ? "PASS" : "FAIL",
        fireAfterRain, fireResolved, wildfireOk ? "PASS" : "FAIL",
        totalSpawned, activeCount, totalResolved, totalExpired, distinctKinds, acctExact ? "yes" : "no", coupled30Ok ? "PASS" : "FAIL",
        ok ? "PASS" : "FAIL", ok ? "PASS" : "FAIL", totalMs);
    return ok;
}

bool ParseCellXY(const std::string& s, int32_t* outX, int32_t* outY) {
    const size_t comma = s.find(',');
    if (comma == std::string::npos || comma == 0u || comma + 1u >= s.size()) return false;
    const std::string sx = s.substr(0u, comma);
    const std::string sy = s.substr(comma + 1u);
    char* endX = nullptr;
    char* endY = nullptr;
    const long x = std::strtol(sx.c_str(), &endX, 10);
    const long y = std::strtol(sy.c_str(), &endY, 10);
    if (!endX || *endX != '\0' || !endY || *endY != '\0') return false;
    *outX = static_cast<int32_t>(x);
    *outY = static_cast<int32_t>(y);
    return true;
}

void PrintUsage() {
    std::printf(
        "RAW Forge Studio Pipeline CLI (Clean-Room MIT)\n"
        "Usage:\n"
        "  raw_forge_cli --flora <dir>                             Bake & validate 5 species x 4 seasons x 3 LODs\n"
        "  raw_forge_cli --diorama <dir>                           Bake & validate 6 frontier biomes + hero props\n"
        "  raw_forge_cli --almanac <dir>                           Bake 365-day orbital climate & phenology almanac\n"
        "  raw_forge_cli --materials <dir>                         Validate & export physical optical material catalog\n"
        "  raw_forge_cli --textures <dir> [--tex-size 256|512]     Generate 8 seamless PBR texture sets (.dds + .bmp)\n"
        "  raw_forge_cli --esm <path> --cell <x,y> --out <dir>     Extract, validate & export real Tamriel ESM cell\n"
        "  raw_forge_cli --esm-scene <esm> --bsa <bsa> --cell <x,y> --out <d> Reconstruct full cell (terrain + NIF props)\n"
        "  raw_forge_cli --bsa-list <path> [--filter <substr>]     Inspect & list files in a v104/v105 .bsa archive\n"
        "  raw_forge_cli --nif <path> --out <dir>                  Parse, validate & export a Skyrim SE .nif mesh\n"
        "  raw_forge_cli --bsa-nif <bsa> --entry <vpath> --out <d> Extract .nif from .bsa, validate & export .glb/.obj\n"
        "  raw_forge_cli --dds <path> --out <dir>                  Decode .dds (BC1..BC7) & run PBR/Toksvig analysis\n"
        "  raw_forge_cli --bsa-dds <bsa> --entry <vpath> --out <d> Extract .dds from .bsa, decode & export PBR JSON/BMP\n"
        "  raw_forge_cli --esm-meta [Skyrim.esm]                   Audit WTHR/CLMT/NPC_/REGN/QUST + RawWeatherSim maps\n"
        "  raw_forge_cli --physics-probe [Skyrim.esm] [cx cy]      SAH BVH + 64-ray scan + 60-step capsule walk audit\n"
        "  raw_forge_cli --tamriel-stream [Skyrim.esm]             3-landmark Tamriel multi-cell streaming & LRU audit\n"
        "  raw_forge_cli --navmesh-audit [Skyrim.esm] [cx cy]      NavMesh bake + A* funnel + 4-agent squad AI audit\n"
        "  raw_forge_cli --ecosystem-sim                           Coupled wildfire/rain/snowmelt-D8/black-ice/regrowth audit\n"
        "  raw_forge_cli --wildlife-sim                            512-agent O(N) wildlife boids (hawk/crow/deer/salmon) audit\n"
        "  raw_forge_cli --settlement-sim                          9-hold living settlement, caravan & blizzard blockade audit\n"
        "  raw_forge_cli --events-sim                              Emergent 9-hold radiant world-event director 30-day audit\n"
        "  raw_forge_cli --validate-all [dir]                      Run all 4 procedural pipelines + quality gates\n"
        "Options:\n"
        "  --seed <N>         Deterministic RNG seed (default: 202607)\n"
        "  --latitude <deg>   Orbital latitude in degrees North (default: 60.0)\n"
        "  --limit <N>        Max lines printed by --bsa-list (default: 50, 0 = all)\n"
        "  --no-write         Skip writing .glb/.obj blobs (still validates all geometry & writes manifests)\n"
        "  -h, --help         Show this help\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc <= 1) {
        PrintUsage();
        return 2;
    }

    std::string floraDir;
    std::string dioramaDir;
    std::string almanacDir;
    std::string materialsDir;
    std::string texturesDir;
    uint32_t    texSize = 256u;
    std::string validateAllDir;
    std::string esmPath;
    std::string esmScenePath;
    std::string esmSceneBsa;
    uint32_t    maxModels = 64u;
    std::string esmOutDir;
    std::string bsaListPath;
    std::string bsaFilter;
    std::string nifPath;
    std::string bsaNifArchive;
    std::string bsaNifEntry;
    std::string ddsPath;
    std::string bsaDdsArchive;
    bool hasEsmCell     = false;
    int32_t esmCellX    = 0;
    int32_t esmCellY    = 0;
    uint32_t listLimit  = 50u;
    bool runValidateAll     = false;
    bool runEsmMeta         = false;
    std::string esmMetaPath = kDefaultSkyrimEsm;
    bool runPhysicsProbe    = false;
    std::string physEsmPath = kDefaultSkyrimEsm;
    int32_t physCellX       = 4;
    int32_t physCellY       = -12;
    bool hasPhysCellArg     = false;
    bool runTamrielStream   = false;
    std::string streamEsmPath = kDefaultSkyrimEsm;
    bool runNavMeshAudit    = false;
    std::string navEsmPath  = kDefaultSkyrimEsm;
    int32_t navCellX        = 4;
    int32_t navCellY        = -12;
    bool hasNavCellArg      = false;
    bool runEcosystemSim    = false;
    bool runWildlifeSim     = false;
    bool runSettlementSim   = false;
    bool runEventsSim       = false;
    bool writeGeometry      = true;
    uint32_t seed       = 202607u;
    float latitudeDeg   = 60.0f;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto needNext = [&](const char* opt) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Error: option %s requires a value.\n", opt);
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            PrintUsage();
            return 0;
        } else if (arg == "--flora") {
            const char* v = needNext("--flora");
            if (!v) return 2;
            floraDir = v;
        } else if (arg == "--diorama") {
            const char* v = needNext("--diorama");
            if (!v) return 2;
            dioramaDir = v;
        } else if (arg == "--almanac") {
            const char* v = needNext("--almanac");
            if (!v) return 2;
            almanacDir = v;
        } else if (arg == "--materials") {
            const char* v = needNext("--materials");
            if (!v) return 2;
            materialsDir = v;
        } else if (arg == "--textures") {
            const char* v = needNext("--textures");
            if (!v) return 2;
            texturesDir = v;
        } else if (arg == "--tex-size") {
            const char* v = needNext("--tex-size");
            if (!v) return 2;
            texSize = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
        } else if (arg == "--esm") {
            const char* v = needNext("--esm");
            if (!v) return 2;
            esmPath = v;
        } else if (arg == "--esm-scene") {
            const char* v = needNext("--esm-scene");
            if (!v) return 2;
            esmScenePath = v;
        } else if (arg == "--bsa") {
            const char* v = needNext("--bsa");
            if (!v) return 2;
            esmSceneBsa = v;
        } else if (arg == "--max-models") {
            const char* v = needNext("--max-models");
            if (!v) return 2;
            maxModels = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
        } else if (arg == "--cell") {
            const char* v = needNext("--cell");
            if (!v || !ParseCellXY(v, &esmCellX, &esmCellY)) {
                std::fprintf(stderr, "Error: --cell expects integer pair <x,y> (e.g. 4,-3).\n");
                return 2;
            }
            hasEsmCell = true;
        } else if (arg == "--out") {
            const char* v = needNext("--out");
            if (!v) return 2;
            esmOutDir = v;
        } else if (arg == "--bsa-list") {
            const char* v = needNext("--bsa-list");
            if (!v) return 2;
            bsaListPath = v;
        } else if (arg == "--nif") {
            const char* v = needNext("--nif");
            if (!v) return 2;
            nifPath = v;
        } else if (arg == "--bsa-nif") {
            const char* v = needNext("--bsa-nif");
            if (!v) return 2;
            bsaNifArchive = v;
        } else if (arg == "--dds") {
            const char* v = needNext("--dds");
            if (!v) return 2;
            ddsPath = v;
        } else if (arg == "--bsa-dds") {
            const char* v = needNext("--bsa-dds");
            if (!v) return 2;
            bsaDdsArchive = v;
        } else if (arg == "--entry") {
            const char* v = needNext("--entry");
            if (!v) return 2;
            bsaNifEntry = v;
        } else if (arg == "--filter") {
            const char* v = needNext("--filter");
            if (!v) return 2;
            bsaFilter = v;
        } else if (arg == "--limit") {
            const char* v = needNext("--limit");
            if (!v) return 2;
            listLimit = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
        } else if (arg == "--esm-meta") {
            runEsmMeta = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                esmMetaPath = argv[++i];
            }
        } else if (arg == "--physics-probe") {
            runPhysicsProbe = true;
            if (i + 1 < argc) {
                int32_t tmpX = 0, tmpY = 0;
                if (i + 2 < argc && ParseSignedInt(argv[i + 1], &tmpX) && ParseSignedInt(argv[i + 2], &tmpY)) {
                    physCellX = tmpX;
                    physCellY = tmpY;
                    hasPhysCellArg = true;
                    i += 2;
                } else if (argv[i + 1][0] != '-') {
                    physEsmPath = argv[++i];
                    if (i + 2 < argc && ParseSignedInt(argv[i + 1], &tmpX) && ParseSignedInt(argv[i + 2], &tmpY)) {
                        physCellX = tmpX;
                        physCellY = tmpY;
                        hasPhysCellArg = true;
                        i += 2;
                    }
                }
            }
        } else if (arg == "--tamriel-stream") {
            runTamrielStream = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                streamEsmPath = argv[++i];
            }
        } else if (arg == "--navmesh-audit") {
            runNavMeshAudit = true;
            if (i + 1 < argc) {
                int32_t tmpX = 0, tmpY = 0;
                if (i + 2 < argc && ParseSignedInt(argv[i + 1], &tmpX) && ParseSignedInt(argv[i + 2], &tmpY)) {
                    navCellX = tmpX;
                    navCellY = tmpY;
                    hasNavCellArg = true;
                    i += 2;
                } else if (argv[i + 1][0] != '-') {
                    navEsmPath = argv[++i];
                    if (i + 2 < argc && ParseSignedInt(argv[i + 1], &tmpX) && ParseSignedInt(argv[i + 2], &tmpY)) {
                        navCellX = tmpX;
                        navCellY = tmpY;
                        hasNavCellArg = true;
                        i += 2;
                    }
                }
            }
        } else if (arg == "--ecosystem-sim") {
            runEcosystemSim = true;
        } else if (arg == "--wildlife-sim") {
            runWildlifeSim = true;
        } else if (arg == "--settlement-sim") {
            runSettlementSim = true;
        } else if (arg == "--events-sim") {
            runEventsSim = true;
        } else if (arg == "--validate-all") {
            runValidateAll = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                validateAllDir = argv[++i];
            } else {
                validateAllDir = "raw_forge_out";
            }
        } else if (arg == "--seed") {
            const char* v = needNext("--seed");
            if (!v) return 2;
            seed = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
        } else if (arg == "--latitude") {
            const char* v = needNext("--latitude");
            if (!v) return 2;
            latitudeDeg = std::strtof(v, nullptr);
        } else if (arg == "--no-write") {
            writeGeometry = false;
        } else {
            std::fprintf(stderr, "Error: unrecognized option '%s'.\n", arg.c_str());
            PrintUsage();
            return 2;
        }
    }

    if (!runValidateAll && !runEsmMeta && !runPhysicsProbe && !runTamrielStream && !runNavMeshAudit &&
        !runEcosystemSim && !runWildlifeSim && !runSettlementSim && !runEventsSim &&
        floraDir.empty() && dioramaDir.empty() && almanacDir.empty() &&
        materialsDir.empty() && texturesDir.empty() && esmPath.empty() && esmScenePath.empty() &&
        bsaListPath.empty() && nifPath.empty() && bsaNifArchive.empty() && ddsPath.empty() && bsaDdsArchive.empty()) {
        std::fprintf(stderr, "Error: specify at least one action (--flora, --diorama, --almanac, --materials, --textures, --esm, --esm-scene, --bsa-list, --nif, --bsa-nif, --dds, --bsa-dds, --esm-meta, --physics-probe, --tamriel-stream, --navmesh-audit, --ecosystem-sim, --wildlife-sim, --settlement-sim, --events-sim, or --validate-all).\n");
        PrintUsage();
        return 2;
    }

    const raw::pipeline::ValidatorPolicy policy{};
    bool allOk = true;

    auto runStep = [&](const char* label, const raw::pipeline::BakeResult& r) {
        std::printf("[%s] %s | files=%u | manifest=%s\n",
                    r.ok ? "PASS" : "FAIL",
                    r.summaryMessage.c_str(),
                    r.filesWritten,
                    r.manifestPath.c_str());
        if (!r.ok) {
            allOk = false;
            std::fprintf(stderr, "  %s reported %u validation failure(s).\n", label, r.validationFailures);
        }
    };

    if (!bsaListPath.empty()) {
        raw::worldgen::RawBsaArchive bsa;
        std::string err;
        if (!bsa.Open(bsaListPath, &err)) {
            std::fprintf(stderr, "[FAIL] BSA open failed: %s\n", err.c_str());
            allOk = false;
        } else {
            const auto st = bsa.GetArchiveStats();
            const auto files = bsa.ListFiles(bsaFilter, false);
            std::printf("[PASS] BSA v%u (%s): folders=%u files=%u (comp=%u, raw=%u, embedNames=%s) matched=%zu\n",
                        st.version, st.path.c_str(), st.folderCount, st.fileCount,
                        st.compressedFiles, st.uncompressedFiles,
                        st.embedsFileNames ? "yes" : "no", files.size());
            const size_t showN = (listLimit == 0u || listLimit > files.size()) ? files.size() : size_t(listLimit);
            for (size_t k = 0u; k < showN; ++k) {
                std::printf("  %s\n", files[k].c_str());
            }
            if (showN < files.size()) {
                std::printf("  ... (%zu more entries; pass --limit 0 to show all)\n", files.size() - showN);
            }
        }
    }

    if (!esmPath.empty()) {
        if (!hasEsmCell) {
            std::fprintf(stderr, "Error: --esm requires --cell <x,y>.\n");
            return 2;
        }
        const std::string outDir = esmOutDir.empty() ? "raw_forge_esm_out" : esmOutDir;
        runStep("esm-cell", raw::pipeline::BakeEsmCellAsset(
            esmPath, esmCellX, esmCellY, outDir, 0x0000003Cu, policy, writeGeometry));
    }

    if (!esmScenePath.empty()) {
        if (!hasEsmCell || esmSceneBsa.empty()) {
            std::fprintf(stderr, "Error: --esm-scene requires --bsa <Meshes0.bsa> and --cell <x,y>.\n");
            return 2;
        }
        const std::string outDir = esmOutDir.empty() ? "raw_forge_scene_out" : esmOutDir;
        runStep("esm-scene", raw::pipeline::BakeTamrielCellSceneBundle(
            esmScenePath, esmSceneBsa, esmCellX, esmCellY, outDir, maxModels, policy, writeGeometry));
    }

    if (!nifPath.empty()) {
        const std::string outDir = esmOutDir.empty() ? "raw_forge_nif_out" : esmOutDir;
        runStep("nif", raw::pipeline::BakeNifFileAsset(nifPath, outDir, policy, writeGeometry));
    }

    if (!bsaNifArchive.empty()) {
        if (bsaNifEntry.empty()) {
            std::fprintf(stderr, "Error: --bsa-nif requires --entry <virtualPath>.\n");
            return 2;
        }
        const std::string outDir = esmOutDir.empty() ? "raw_forge_nif_out" : esmOutDir;
        runStep("bsa-nif", raw::pipeline::BakeBsaNifAsset(bsaNifArchive, bsaNifEntry, outDir, policy, writeGeometry));
    }

    if (!ddsPath.empty()) {
        const std::string outDir = esmOutDir.empty() ? "raw_forge_dds_out" : esmOutDir;
        runStep("dds", raw::pipeline::InspectDdsFileAsset(ddsPath, outDir, writeGeometry));
    }

    if (!bsaDdsArchive.empty()) {
        if (bsaNifEntry.empty()) {
            std::fprintf(stderr, "Error: --bsa-dds requires --entry <virtualPath>.\n");
            return 2;
        }
        const std::string outDir = esmOutDir.empty() ? "raw_forge_dds_out" : esmOutDir;
        runStep("bsa-dds", raw::pipeline::InspectBsaDdsAsset(bsaDdsArchive, bsaNifEntry, outDir, writeGeometry));
    }

    if (runEsmMeta) {
        if (!RunEsmMetaAudit(esmMetaPath)) allOk = false;
    }

    if (runPhysicsProbe) {
        const int32_t cx = (!hasPhysCellArg && hasEsmCell) ? esmCellX : physCellX;
        const int32_t cy = (!hasPhysCellArg && hasEsmCell) ? esmCellY : physCellY;
        if (!RunPhysicsProbeAudit(physEsmPath, esmSceneBsa, cx, cy, maxModels)) allOk = false;
    }

    if (runTamrielStream) {
        if (!RunTamrielStreamAudit(streamEsmPath, esmSceneBsa)) allOk = false;
    }

    if (runNavMeshAudit) {
        const int32_t cx = (!hasNavCellArg && hasEsmCell) ? esmCellX : navCellX;
        const int32_t cy = (!hasNavCellArg && hasEsmCell) ? esmCellY : navCellY;
        if (!RunNavMeshAudit(navEsmPath, esmSceneBsa, cx, cy, maxModels)) allOk = false;
    }

    if (runEcosystemSim) {
        if (!RunEcosystemSimAudit()) allOk = false;
    }

    if (runWildlifeSim) {
        if (!RunWildlifeSimAudit()) allOk = false;
    }

    if (runSettlementSim) {
        if (!RunSettlementSimAudit()) allOk = false;
    }

    if (runEventsSim) {
        if (!RunEventsSimAudit()) allOk = false;
    }

    if (runValidateAll) {
        runStep("validate-all", raw::pipeline::RunFullPipelineValidation(
            validateAllDir, seed, latitudeDeg, policy, writeGeometry));
    } else {
        if (!floraDir.empty()) {
            runStep("flora", raw::pipeline::BakeSeasonalFloraCatalog(floraDir, seed, policy, writeGeometry));
        }
        if (!dioramaDir.empty()) {
            runStep("diorama", raw::pipeline::BakeWorldShowcaseDiorama(dioramaDir, seed, policy, writeGeometry));
        }
        if (!almanacDir.empty()) {
            runStep("almanac", raw::pipeline::BakeAnnualClimateAlmanac(almanacDir, seed, latitudeDeg));
        }
        if (!materialsDir.empty()) {
            runStep("materials", raw::pipeline::BakeMaterialCatalogManifest(materialsDir));
        }
        if (!texturesDir.empty()) {
            runStep("textures", raw::pipeline::BakeProceduralTextureLibrary(texturesDir, texSize, seed, writeGeometry));
        }
    }

    return allOk ? 0 : 1;
}
