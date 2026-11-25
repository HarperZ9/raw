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
#include "../src/worldgen/RawTamrielWorldStreamer.h"
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

    if (!runValidateAll && !runEsmMeta && !runPhysicsProbe && !runTamrielStream &&
        floraDir.empty() && dioramaDir.empty() && almanacDir.empty() &&
        materialsDir.empty() && texturesDir.empty() && esmPath.empty() && esmScenePath.empty() &&
        bsaListPath.empty() && nifPath.empty() && bsaNifArchive.empty() && ddsPath.empty() && bsaDdsArchive.empty()) {
        std::fprintf(stderr, "Error: specify at least one action (--flora, --diorama, --almanac, --materials, --textures, --esm, --esm-scene, --bsa-list, --nif, --bsa-nif, --dds, --bsa-dds, --esm-meta, --physics-probe, --tamriel-stream, or --validate-all).\n");
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
