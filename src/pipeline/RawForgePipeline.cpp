// RawForgePipeline.cpp: Unified Studio Authoring, Baking & Validation Pipeline (`raw::pipeline`).
// Orchestrates RawFloraEngine, RawWorldShowcase, RawMeshPipeline, RawWeatherSim, RawMaterialLib,
// and RawAssetValidator into deterministic batch bakers with JSON/CSV/C++/glTF/OBJ deliverables.
// Original work, MIT.

#include "RawForgePipeline.h"

#include "../materials/RawMaterialLib.h"
#include "../worldgen/RawBsaArchive.h"
#include "../worldgen/RawEsmWorldLoader.h"
#include "../worldgen/RawFloraEngine.h"
#include "../worldgen/RawNifLoader.h"
#include "../worldgen/RawWeatherSim.h"
#include "../worldgen/RawWorldShowcase.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace raw::pipeline {
namespace {

bool EnsureDir(const std::string& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return !ec || std::filesystem::exists(dir, ec);
}

bool WriteTextFile(const std::string& path, const std::string& content) {
    if (path.empty()) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    return f.good();
}

std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4u);
    for (char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

std::string SanitizeId(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    return out;
}

const char* SeasonPresetName(raw::worldgen::FloraSeasonPreset p) {
    switch (p) {
        case raw::worldgen::FloraSeasonPreset::SpringBudBreak: return "Spring";
        case raw::worldgen::FloraSeasonPreset::SummerLush:     return "Summer";
        case raw::worldgen::FloraSeasonPreset::PeakAutumn:     return "Autumn";
        case raw::worldgen::FloraSeasonPreset::DeepWinter:     return "Winter";
    }
    return "Summer";
}

void AccumulateReport(const ValidationReport& rep, BakeResult& res) {
    ++res.assetsValidated;
    if (!rep.passed) ++res.validationFailures;
    res.totalGpuBytes  += rep.estimatedGpuBytes;
    res.totalMeshlets  += rep.meshletCount;
    res.totalTriangles += rep.triangleCount;
    res.reports.push_back(rep);
}

}  // namespace

BakeResult BakeSeasonalFloraCatalog(const std::string& outDir,
                                    uint32_t seed,
                                    const ValidatorPolicy& policy,
                                    bool writeGeometryFiles) {
    using namespace raw::worldgen;
    BakeResult res{};
    if (!EnsureDir(outDir)) {
        res.summaryMessage = "Failed to create flora output directory: " + outDir;
        return res;
    }

    constexpr FloraSeasonPreset kSeasons[4] = {
        FloraSeasonPreset::SpringBudBreak,
        FloraSeasonPreset::SummerLush,
        FloraSeasonPreset::PeakAutumn,
        FloraSeasonPreset::DeepWinter,
    };

    std::ostringstream manifest;
    manifest << std::fixed << std::setprecision(4);
    manifest << "{\n  \"pipeline\": \"RAW Seasonal Flora Catalog\",\n"
             << "  \"seed\": " << seed << ",\n"
             << "  \"speciesCount\": 5,\n"
             << "  \"seasonsCount\": 4,\n"
             << "  \"lodLevels\": 3,\n"
             << "  \"assets\": [\n";

    bool firstEntry = true;
    for (uint32_t spIdx = 0u; spIdx < static_cast<uint32_t>(FloraSpecies::Count); ++spIdx) {
        const auto sp = static_cast<FloraSpecies>(spIdx);
        const std::string spName = GetFloraSpeciesName(sp);

        for (uint32_t seaIdx = 0u; seaIdx < 4u; ++seaIdx) {
            const FloraSeasonPreset preset = kSeasons[seaIdx];
            const std::string seaName = SeasonPresetName(preset);

            for (uint32_t lod = 0u; lod < 3u; ++lod) {
                FloraGrowthParams params = MakeDefaultFloraParams(sp, seed + spIdx * 97u, lod);
                ApplyFloraSeasonPreset(preset, &params);

                FloraAsset asset{};
                if (!GenerateFloraAsset(params, &asset)) {
                    ++res.validationFailures;
                    continue;
                }
                ++res.assetsGenerated;

                const std::string baseName = spName + "_" + seaName + "_lod" + std::to_string(lod);
                asset.mesh.name = baseName;

                const ValidationReport rep = ValidateProceduralMesh(asset.mesh, policy);
                AccumulateReport(rep, res);

                std::string glbRel = baseName + ".glb";
                std::string objRel = baseName + ".obj";
                if (writeGeometryFiles) {
                    const std::string glbPath = outDir + "/" + glbRel;
                    const std::string objPath = outDir + "/" + objRel;
                    if (ExportFloraToGlb(asset, glbPath, true)) ++res.filesWritten;
                    else ++res.validationFailures;
                    if (ExportFloraToObj(asset, objPath)) ++res.filesWritten;
                    else ++res.validationFailures;
                }

                if (!firstEntry) manifest << ",\n";
                firstEntry = false;
                manifest << "    {"
                         << "\"name\":\"" << baseName << "\","
                         << "\"species\":\"" << spName << "\","
                         << "\"season\":\"" << seaName << "\","
                         << "\"lod\":" << lod << ","
                         << "\"glb\":\"" << (writeGeometryFiles ? glbRel : "") << "\","
                         << "\"obj\":\"" << (writeGeometryFiles ? objRel : "") << "\","
                         << "\"passed\":" << (rep.passed ? "true" : "false") << ","
                         << "\"vertices\":" << rep.vertexCount << ","
                         << "\"triangles\":" << rep.triangleCount << ","
                         << "\"meshlets\":" << rep.meshletCount << ","
                         << "\"gpuBytes\":" << rep.estimatedGpuBytes << ","
                         << "\"actualHeightU\":" << asset.stats.actualHeightU << ","
                         << "\"meanCanopyZ\":" << asset.stats.meanCanopyZ << ","
                         << "\"foliageClusters\":" << asset.stats.foliageClusterCount << ","
                         << "\"abscisedClusters\":" << asset.stats.abscisedClusterCount << ","
                         << "\"prunedStubs\":" << asset.stats.prunedStubCount << ","
                         << "\"pipeMonotonic\":" << (asset.stats.pipeModelMonotonic ? "true" : "false")
                         << "}";
            }
        }
    }

    manifest << "\n  ],\n"
             << "  \"totalAssets\": " << res.assetsGenerated << ",\n"
             << "  \"validationFailures\": " << res.validationFailures << ",\n"
             << "  \"totalMeshlets\": " << res.totalMeshlets << ",\n"
             << "  \"totalTriangles\": " << res.totalTriangles << ",\n"
             << "  \"totalGpuBytes\": " << res.totalGpuBytes << "\n"
             << "}\n";

    res.manifestPath = outDir + "/flora_manifest.json";
    if (WriteTextFile(res.manifestPath, manifest.str())) {
        ++res.filesWritten;
    } else {
        ++res.validationFailures;
    }

    res.ok = (res.assetsGenerated == 60u && res.validationFailures == 0u);
    res.summaryMessage = "FloraCatalog: " + std::to_string(res.assetsGenerated) + " assets (" +
                         std::to_string(res.totalMeshlets) + " meshlets, " +
                         std::to_string(res.totalTriangles) + " tris, " +
                         std::to_string(res.validationFailures) + " failures)";
    return res;
}

BakeResult BakeWorldShowcaseDiorama(const std::string& outDir,
                                    uint32_t seed,
                                    const ValidatorPolicy& policy,
                                    bool writeGeometryFiles) {
    using namespace raw::worldgen;
    BakeResult res{};
    if (!EnsureDir(outDir)) {
        res.summaryMessage = "Failed to create diorama output directory: " + outDir;
        return res;
    }

    TerrainConfig tcfg{};
    tcfg.seed = seed;
    tcfg.border.enabled = false;

    constexpr float kAngles[6] = {
        +1.5707963f, // 0: Glacial Fjord Spires
        -0.5235988f, // 1: Geothermal Volcanic Caldera
        +2.6179939f, // 2: Old-Growth Conifer Highlands
        -1.5707963f, // 3: Obsidian Basalt Canyons
        +0.5235988f, // 4: Bioluminescent Crystal Tundra
        -2.6179939f, // 5: Alpine Meltwater Deltas
    };
    constexpr float kFrontierR = 220000.0f;

    std::ostringstream manifest;
    manifest << std::fixed << std::setprecision(4);
    manifest << "{\n  \"pipeline\": \"RAW 6-Biome World Showcase Diorama\",\n"
             << "  \"seed\": " << seed << ",\n"
             << "  \"biomes\": [\n";

    for (uint32_t i = 0u; i < 6u; ++i) {
        const auto biomeId = static_cast<BiomeId>(i);
        const std::string biomeName = GetBiomeName(biomeId);
        const std::string biomeSlug = "biome" + std::to_string(i) + "_" + SanitizeId(biomeName);

        const float wx = std::cos(kAngles[i]) * kFrontierR;
        const float wy = std::sin(kAngles[i]) * kFrontierR;
        const int32_t cx = int32_t(std::floor(wx / tcfg.chunkSizeU));
        const int32_t cy = int32_t(std::floor(wy / tcfg.chunkSizeU));

        TerrainChunkData chunk{};
        ProceduralMesh terrainMesh{};
        if (!GenerateBiomeTerrainChunk(cx, cy, 1u, tcfg, StitchNone, &chunk) ||
            !BuildTerrainChunkMesh(chunk, 126u, &terrainMesh)) {
            ++res.validationFailures;
            continue;
        }
        terrainMesh.name = biomeSlug + "_terrain";
        ++res.assetsGenerated;
        const ValidationReport terrainRep = ValidateProceduralMesh(terrainMesh, policy);
        AccumulateReport(terrainRep, res);

        // Generate signature geological hero prop per biome
        ProceduralMesh geoProp{};
        bool geoOk = false;
        switch (i) {
            case 0u: geoOk = BuildGlacialIceArch(seed + 11u, 340.0f, 210.0f, 42.0f, 126u, &geoProp); break;
            case 1u: geoOk = BuildVolcanicVent(seed + 22u, 110.0f, 150.0f, 126u, &geoProp); break;
            case 2u: geoOk = BuildNordicSnowDrift(seed + 33u, 220.0f, 140.0f, 44.0f, 126u, &geoProp); break;
            case 3u: geoOk = BuildHexagonalBasaltCluster(seed + 44u, 11u, 125.0f, 210.0f, 126u, &geoProp); break;
            case 4u: geoOk = BuildCrystalMonolith(seed + 55u, 80.0f, 240.0f, 6u, 126u, &geoProp); break;
            case 5u:
            default: geoOk = BuildAlpineBoulder(seed + 66u, 96.0f, 0.35f, 5u, 126u, &geoProp); break;
        }
        ValidationReport geoRep{};
        if (geoOk) {
            geoProp.name = biomeSlug + "_hero_prop";
            ++res.assetsGenerated;
            geoRep = ValidateProceduralMesh(geoProp, policy);
            AccumulateReport(geoRep, res);
        } else {
            ++res.validationFailures;
        }

        // Generate biome-matched biomechanical hero tree via RawFloraEngine!
        PropInstance treeProp{};
        treeProp.kind     = PropKind::ConiferTree;
        treeProp.biome    = biomeId;
        treeProp.upNormal = chunk.SampleNormalBilinear(0.5f, 0.5f);
        treeProp.scale    = 1.0f;
        treeProp.seed     = seed + 700u + i * 31u;

        ProceduralMesh floraHero{};
        ValidationReport floraRep{};
        const float biomeSnow = (i == 0u || i == 4u) ? 0.72f : ((i == 2u) ? 0.30f : 0.10f);
        if (BuildBiomechanicalTreeForProp(treeProp, 1u, biomeSnow, 126u, &floraHero)) {
            floraHero.name = biomeSlug + "_hero_flora";
            ++res.assetsGenerated;
            floraRep = ValidateProceduralMesh(floraHero, policy);
            AccumulateReport(floraRep, res);
        } else {
            ++res.validationFailures;
        }

        // Deterministic Poisson prop placement count for manifest metadata
        std::vector<PropInstance> props;
        GenerateChunkProps(chunk, seed + i * 19u, 0.0f, &props);

        if (writeGeometryFiles) {
            auto exportPair = [&](const ProceduralMesh& m) {
                if (ExportMeshToGlbFile(m, outDir + "/" + m.name + ".glb", true)) ++res.filesWritten;
                else ++res.validationFailures;
                if (ExportMeshToObjFile(m, outDir + "/" + m.name + ".obj")) ++res.filesWritten;
                else ++res.validationFailures;
            };
            exportPair(terrainMesh);
            if (geoOk) exportPair(geoProp);
            if (!floraHero.meshlets.empty()) exportPair(floraHero);
        }

        if (i > 0u) manifest << ",\n";
        manifest << "    {"
                 << "\"biomeId\":" << i << ","
                 << "\"biomeName\":\"" << JsonEscape(biomeName) << "\","
                 << "\"chunk\":[" << cx << "," << cy << "],"
                 << "\"poissonPropCount\":" << props.size() << ","
                 << "\"terrainPassed\":" << (terrainRep.passed ? "true" : "false") << ","
                 << "\"terrainMeshlets\":" << terrainRep.meshletCount << ","
                 << "\"heroPropPassed\":" << (geoRep.passed ? "true" : "false") << ","
                 << "\"heroFloraPassed\":" << (floraRep.passed ? "true" : "false")
                 << "}";
    }

    manifest << "\n  ],\n"
             << "  \"totalAssets\": " << res.assetsGenerated << ",\n"
             << "  \"validationFailures\": " << res.validationFailures << ",\n"
             << "  \"totalMeshlets\": " << res.totalMeshlets << ",\n"
             << "  \"totalTriangles\": " << res.totalTriangles << ",\n"
             << "  \"totalGpuBytes\": " << res.totalGpuBytes << "\n"
             << "}\n";

    res.manifestPath = outDir + "/world_manifest.json";
    if (WriteTextFile(res.manifestPath, manifest.str())) {
        ++res.filesWritten;
    } else {
        ++res.validationFailures;
    }

    res.ok = (res.assetsGenerated == 18u && res.validationFailures == 0u);
    res.summaryMessage = "WorldDiorama: 6 biomes / " + std::to_string(res.assetsGenerated) + " meshes (" +
                         std::to_string(res.totalMeshlets) + " meshlets, " +
                         std::to_string(res.validationFailures) + " failures)";
    return res;
}

BakeResult BakeAnnualClimateAlmanac(const std::string& outDir, uint32_t seed, float latitudeDeg) {
    using namespace raw::worldgen;
    BakeResult res{};
    if (!EnsureDir(outDir)) {
        res.summaryMessage = "Failed to create almanac output directory: " + outDir;
        return res;
    }

    WeatherSimConfig cfg{};
    cfg.seed             = seed;
    cfg.latitudeDeg      = Clamp(latitudeDeg, 15.0f, 75.0f);
    cfg.initialDayOfYear = 0.0f; // Start Jan 1 (deep winter)
    cfg.timeOfDayHours   = 5.5f;
    cfg.timeScale        = 20.0f;
    cfg.baseSeaTempC     = 6.5f;
    cfg.initialRegime    = WeatherRegime::Blizzard;

    RawWeatherSim sim(cfg);
    // Representative Nordic foothill station (~320m elevation above sea level)
    const Vec3 stationPos{0.0f, 0.0f, cfg.seaLevelZ + 320.0f * cfg.unitsPerMeter};

    std::ostringstream csv;
    csv << std::fixed << std::setprecision(4);
    csv << "dayOfYear,season,solarDeclDeg,dayLengthHr,middayTempC,dawnInversion01,middayInversion01,"
        << "snowDepthM,snowAlbedo,iceThicknessM,budBreak01,chlorophyll01,"
        << "autumnCarotenoid01,autumnAnthocyanin01,leafAbscission01,canopySnowLoadKgM2\n";

    struct SeasonStats {
        double dayLenSum = 0.0, tempSum = 0.0, snowSum = 0.0, iceSum = 0.0;
        double chloroSum = 0.0, carotSum = 0.0, anthoSum = 0.0, abscSum = 0.0;
        uint32_t days = 0u;
    };
    SeasonStats seasonStats[4]{};

    float maxSnowM = 0.0f, maxIceM = 0.0f;
    float minTempC = 1.0e9f, maxTempC = -1.0e9f;
    float minDayLen = 24.0f, maxDayLen = 0.0f;

    for (uint32_t day = 0u; day < 365u; ++day) {
        const uint32_t h = HashU32(seed ^ (day * 0x9E3779B9u));
        const float r = HashTo01(h);

        // Seasonally plausible synoptic weather regime sequence across the 365-day year
        WeatherRegime reg = WeatherRegime::ClearAlpine;
        if (day < 65u || day >= 330u) {
            reg = (r < 0.45f) ? WeatherRegime::Blizzard
                : ((r < 0.78f) ? WeatherRegime::ArcticClearAurora : WeatherRegime::Overcast);
        } else if (day < 150u) {
            reg = (r < 0.35f) ? WeatherRegime::DawnValleyMist
                : ((r < 0.65f) ? WeatherRegime::Rainstorm : WeatherRegime::ClearAlpine);
        } else if (day < 245u) {
            reg = (r < 0.55f) ? WeatherRegime::ClearAlpine
                : ((r < 0.78f) ? WeatherRegime::Thunderstorm : WeatherRegime::MountainFogCloudCap);
        } else {
            reg = (r < 0.38f) ? WeatherRegime::Overcast
                : ((r < 0.72f) ? WeatherRegime::Rainstorm : WeatherRegime::DawnValleyMist);
        }
        sim.SetRegime(reg, true);

        // Morning dawn sample (05:30) captures nocturnal thermal inversion before solar burn-off
        sim.SetTimeOfDay(5.5f);
        sim.Step(360.0f, 40.0f, float(day) + 5.5f / 24.0f, stationPos); // 4 game-hours integration
        const float dawnInversion = sim.GetSurfaceThermalState().dispersion.inversionStrength01;

        // Midday sample (13:30) captures peak solar insolation, inversion burn-off & thermal melt
        sim.SetTimeOfDay(13.5f);
        sim.Step(720.0f, 40.0f, float(day) + 13.5f / 24.0f, stationPos); // 8 game-hours integration

        const CalendarState& cal = sim.GetCalendarState();
        const SurfaceThermalState& th = sim.GetSurfaceThermalState();
        const PhenologyState& ph = th.phenology;
        const float declDeg = cal.solarDeclinationRad * (180.0f / 3.14159265f);

        csv << day << "," << RawWeatherSim::GetSeasonName(cal.season) << ","
            << declDeg << "," << cal.dayLengthHours << "," << th.surfaceTempC << ","
            << dawnInversion << "," << th.dispersion.inversionStrength01 << ","
            << th.snow.depthM << "," << th.snow.albedo << "," << th.ice.iceThicknessM << ","
            << ph.budBreak01 << "," << ph.chlorophyll01 << ","
            << ph.autumnCarotenoid01 << "," << ph.autumnAnthocyanin01 << ","
            << ph.leafAbscission01 << "," << ph.canopySnowLoadKgM2 << "\n";

        const uint32_t sIdx = std::min<uint32_t>(static_cast<uint32_t>(cal.season), 3u);
        SeasonStats& ss = seasonStats[sIdx];
        ss.dayLenSum += cal.dayLengthHours;
        ss.tempSum   += th.surfaceTempC;
        ss.snowSum   += th.snow.depthM;
        ss.iceSum    += th.ice.iceThicknessM;
        ss.chloroSum += ph.chlorophyll01;
        ss.carotSum  += ph.autumnCarotenoid01;
        ss.anthoSum  += ph.autumnAnthocyanin01;
        ss.abscSum   += ph.leafAbscission01;
        ++ss.days;

        maxSnowM  = std::max(maxSnowM, th.snow.depthM);
        maxIceM   = std::max(maxIceM, th.ice.iceThicknessM);
        minTempC  = std::min(minTempC, th.surfaceTempC);
        maxTempC  = std::max(maxTempC, th.surfaceTempC);
        minDayLen = std::min(minDayLen, cal.dayLengthHours);
        maxDayLen = std::max(maxDayLen, cal.dayLengthHours);
        ++res.assetsGenerated;
    }

    const std::string csvPath = outDir + "/climate_almanac.csv";
    if (WriteTextFile(csvPath, csv.str())) ++res.filesWritten;
    else ++res.validationFailures;

    std::ostringstream js;
    js << std::fixed << std::setprecision(4);
    js << "{\n  \"pipeline\": \"RAW 365-Day Orbital Climate & Phenology Almanac\",\n"
       << "  \"seed\": " << seed << ",\n"
       << "  \"latitudeDeg\": " << cfg.latitudeDeg << ",\n"
       << "  \"daysSimulated\": 365,\n"
       << "  \"annualExtremes\": {"
       << "\"minDayLengthHr\":" << minDayLen << ","
       << "\"maxDayLengthHr\":" << maxDayLen << ","
       << "\"minTempC\":" << minTempC << ","
       << "\"maxTempC\":" << maxTempC << ","
       << "\"maxSnowDepthM\":" << maxSnowM << ","
       << "\"maxIceThicknessM\":" << maxIceM
       << "},\n  \"seasons\": [\n";

    for (uint32_t s = 0u; s < 4u; ++s) {
        const auto sid = static_cast<SeasonId>(s);
        const SeasonStats& ss = seasonStats[s];
        const double inv = ss.days > 0u ? (1.0 / double(ss.days)) : 0.0;
        if (s > 0u) js << ",\n";
        js << "    {"
           << "\"season\":\"" << RawWeatherSim::GetSeasonName(sid) << "\","
           << "\"days\":" << ss.days << ","
           << "\"meanDayLengthHr\":" << (ss.dayLenSum * inv) << ","
           << "\"meanTempC\":" << (ss.tempSum * inv) << ","
           << "\"meanSnowDepthM\":" << (ss.snowSum * inv) << ","
           << "\"meanIceThicknessM\":" << (ss.iceSum * inv) << ","
           << "\"meanChlorophyll01\":" << (ss.chloroSum * inv) << ","
           << "\"meanCarotenoid01\":" << (ss.carotSum * inv) << ","
           << "\"meanAnthocyanin01\":" << (ss.anthoSum * inv) << ","
           << "\"meanAbscission01\":" << (ss.abscSum * inv)
           << "}";
    }
    js << "\n  ]\n}\n";

    res.manifestPath = outDir + "/climate_summary.json";
    if (WriteTextFile(res.manifestPath, js.str())) ++res.filesWritten;
    else ++res.validationFailures;

    // Sanity-validate orbital & thermal physics invariants directly
    const bool physicsOk = (maxDayLen > minDayLen + 4.0f) && (maxTempC > minTempC + 5.0f) && (maxSnowM > 0.02f);
    if (!physicsOk) ++res.validationFailures;

    res.assetsValidated = res.assetsGenerated;
    res.ok = (res.assetsGenerated == 365u && res.validationFailures == 0u);
    res.summaryMessage = "ClimateAlmanac: 365 days at lat=" + std::to_string(int(cfg.latitudeDeg)) +
                         "deg (maxSnow=" + std::to_string(maxSnowM) + "m, maxIce=" + std::to_string(maxIceM) + "m)";
    return res;
}

BakeResult BakeMaterialCatalogManifest(const std::string& outDir) {
    using namespace raw::materials;
    BakeResult res{};
    if (!EnsureDir(outDir)) {
        res.summaryMessage = "Failed to create materials output directory: " + outDir;
        return res;
    }

    const MaterialRegistry& reg = DefaultMaterialRegistry();
    const auto& allMats = reg.All();
    const auto gpuRecords = reg.BuildGpuBuffer();

    std::ostringstream js;
    js << std::fixed << std::setprecision(5);
    js << "{\n  \"pipeline\": \"RAW Physical Optical Material Catalog\",\n"
       << "  \"materialCount\": " << allMats.size() << ",\n"
       << "  \"gpuRecordStrideBytes\": " << sizeof(GpuMaterialRecord) << ",\n"
       << "  \"materials\": [\n";

    std::ostringstream hdr;
    hdr << "#pragma once\n"
        << "// Auto-generated by raw::pipeline::BakeMaterialCatalogManifest (Clean-Room MIT).\n"
        << "// Standalone physically-measured optical table for external DCC/engine tooling.\n"
        << "#include <cstdint>\n\n"
        << "namespace raw::materials::baked {\n\n"
        << "struct OpticalTableRow {\n"
        << "    uint16_t    id;\n"
        << "    const char* name;\n"
        << "    const char* family;\n"
        << "    float       etaRGB[3];\n"
        << "    float       kRGB[3];\n"
        << "    float       absorbSigma[3];\n"
        << "    float       f0RGB[3];\n"
        << "    float       roughness;\n"
        << "    float       metallic;\n"
        << "    float       cauchyB;\n"
        << "    float       blackbodyTempK;\n"
        << "    float       thinFilmNm;\n"
        << "};\n\n"
        << "inline constexpr OpticalTableRow kCanonicalOpticalTable[] = {\n";
    hdr << std::fixed << std::setprecision(5);

    for (size_t i = 0; i < allMats.size(); ++i) {
        const PhysicalMaterial& m = allMats[i];
        ++res.assetsGenerated;
        ++res.assetsValidated;
        std::string reason;
        if (!ValidateMaterialPhysics(m, &reason)) {
            ++res.validationFailures;
        }
        const Vec3 f0 = ComputeNormalIncidenceF0(m);
        const std::string famStr(FamilyName(m.family));

        if (i > 0u) js << ",\n";
        js << "    {"
           << "\"id\":" << static_cast<uint16_t>(m.id) << ","
           << "\"name\":\"" << JsonEscape(m.name) << "\","
           << "\"family\":\"" << JsonEscape(famStr) << "\","
           << "\"etaRGB\":[" << m.etaRGB.x << "," << m.etaRGB.y << "," << m.etaRGB.z << "],"
           << "\"kRGB\":[" << m.kRGB.x << "," << m.kRGB.y << "," << m.kRGB.z << "],"
           << "\"absorbSigma\":[" << m.absorbSigma.x << "," << m.absorbSigma.y << "," << m.absorbSigma.z << "],"
           << "\"f0RGB\":[" << f0.x << "," << f0.y << "," << f0.z << "],"
           << "\"baseIor540\":" << m.baseIor540 << ","
           << "\"cauchyB\":" << m.cauchyB << ","
           << "\"roughness\":" << m.roughness << ","
           << "\"metallic\":" << m.metallic << ","
           << "\"sssMeanFreePathM\":" << m.sssMeanFreePathM << ","
           << "\"blackbodyTempK\":" << m.blackbodyTempK << ","
           << "\"thinFilmNm\":" << m.thinFilmNm
           << "}";

        hdr << "    {" << static_cast<uint16_t>(m.id) << "u, \"" << JsonEscape(m.name) << "\", \""
            << JsonEscape(famStr) << "\", {" << m.etaRGB.x << "f, " << m.etaRGB.y << "f, " << m.etaRGB.z << "f}, {"
            << m.kRGB.x << "f, " << m.kRGB.y << "f, " << m.kRGB.z << "f}, {"
            << m.absorbSigma.x << "f, " << m.absorbSigma.y << "f, " << m.absorbSigma.z << "f}, {"
            << f0.x << "f, " << f0.y << "f, " << f0.z << "f}, "
            << m.roughness << "f, " << m.metallic << "f, " << m.cauchyB << "f, "
            << m.blackbodyTempK << "f, " << m.thinFilmNm << "f},\n";
    }

    js << "\n  ]\n}\n";
    hdr << "};\n\n}  // namespace raw::materials::baked\n";

    res.manifestPath = outDir + "/materials_manifest.json";
    if (WriteTextFile(res.manifestPath, js.str())) ++res.filesWritten;
    else ++res.validationFailures;

    if (WriteTextFile(outDir + "/raw_materials_catalog.h", hdr.str())) ++res.filesWritten;
    else ++res.validationFailures;

    res.totalGpuBytes = uint64_t(gpuRecords.size()) * sizeof(GpuMaterialRecord);
    res.ok = (!allMats.empty() && res.validationFailures == 0u);
    res.summaryMessage = "MaterialCatalog: " + std::to_string(allMats.size()) + " physical materials validated (" +
                         std::to_string(res.totalGpuBytes) + " GPU bytes)";
    return res;
}

BakeResult BakeEsmCellAsset(const std::string& esmPath,
                            int32_t cellX,
                            int32_t cellY,
                            const std::string& outDir,
                            uint32_t targetWorldFormId,
                            const ValidatorPolicy& policy,
                            bool writeGeometryFiles) {
    using namespace raw::worldgen;
    BakeResult res{};
    if (!EnsureDir(outDir)) {
        res.summaryMessage = "Failed to create ESM cell output directory: " + outDir;
        return res;
    }

    EsmWorldIndex index{};
    std::string err;
    if (!IndexEsmWorldspace(esmPath, targetWorldFormId, &index, &err)) {
        res.validationFailures = 1u;
        res.summaryMessage = "ESM index failed: " + err;
        return res;
    }

    EsmLoadedCell cell{};
    if (!LoadEsmExteriorCell(index, cellX, cellY, &cell, &err)) {
        res.validationFailures = 1u;
        res.summaryMessage = "ESM cell load failed (" + std::to_string(cellX) + "," + std::to_string(cellY) + "): " + err;
        return res;
    }

    ProceduralMesh mesh{};
    TerrainChunkData chunk{};
    if (!BuildMeshFromEsmCell(cell, index.selectedWorld.defaultWaterHeight, 126u, &mesh, &chunk, &err)) {
        res.validationFailures = 1u;
        res.summaryMessage = "ESM mesh build failed: " + err;
        return res;
    }

    const std::string baseName = "esm_cell_" + std::to_string(cellX) + "_" + std::to_string(cellY);
    mesh.name = baseName;
    ++res.assetsGenerated;

    ValidationReport rep{};
    const bool valid = ValidateProceduralMeshStrict(mesh, &rep, policy);
    AccumulateReport(rep, res);

    const std::string glbRel = baseName + ".glb";
    const std::string objRel = baseName + ".obj";
    if (writeGeometryFiles && valid) {
        if (ExportMeshToGlbFile(mesh, outDir + "/" + glbRel, true)) ++res.filesWritten;
        else ++res.validationFailures;
        if (ExportMeshToObjFile(mesh, outDir + "/" + objRel)) ++res.filesWritten;
        else ++res.validationFailures;
    }

    std::ostringstream js;
    js << std::fixed << std::setprecision(4);
    js << "{\n  \"pipeline\": \"RAW Clean-Room ESM Cell Asset Bake\",\n"
       << "  \"esmPath\": \"" << JsonEscape(esmPath) << "\",\n"
       << "  \"worldspace\": \"" << JsonEscape(index.selectedWorld.edid) << "\",\n"
       << "  \"cell\": [" << cellX << ", " << cellY << "],\n"
       << "  \"cellFormId\": " << cell.meta.cellFormId << ",\n"
       << "  \"cellEdid\": \"" << JsonEscape(cell.meta.edid) << "\",\n"
       << "  \"minHeightZ\": " << cell.land.minHeightZ << ",\n"
       << "  \"maxHeightZ\": " << cell.land.maxHeightZ << ",\n"
       << "  \"meanHeightZ\": " << cell.land.meanHeightZ << ",\n"
       << "  \"waterHeightZ\": " << (cell.meta.hasWaterHeight ? cell.meta.waterHeight : index.selectedWorld.defaultWaterHeight) << ",\n"
       << "  \"refrPlacements\": " << cell.placements.size() << ",\n"
       << "  \"props\": " << cell.props.size() << ",\n"
       << "  \"glb\": \"" << (writeGeometryFiles ? glbRel : "") << "\",\n"
       << "  \"obj\": \"" << (writeGeometryFiles ? objRel : "") << "\",\n"
       << "  \"validation\": " << rep.ToJson() << "\n"
       << "}\n";

    res.manifestPath = outDir + "/" + baseName + "_manifest.json";
    if (WriteTextFile(res.manifestPath, js.str())) ++res.filesWritten;
    else ++res.validationFailures;

    res.ok = valid && (res.validationFailures == 0u);
    res.summaryMessage = "EsmCell(" + std::to_string(cellX) + "," + std::to_string(cellY) +
                         ", edid=" + (cell.meta.edid.empty() ? "Wilderness" : cell.meta.edid) +
                         "): " + std::to_string(rep.meshletCount) + " meshlets, " +
                         std::to_string(rep.triangleCount) + " tris, " +
                         std::to_string(cell.placements.size()) + " REFRs";
    return res;
}

namespace {

BakeResult BakeParsedNifScene(const raw::worldgen::NifScene& scene,
                              const std::string& sourceLabel,
                              const std::string& outDir,
                              const ValidatorPolicy& policy,
                              bool writeGeometryFiles) {
    using namespace raw::worldgen;
    BakeResult res{};
    if (!EnsureDir(outDir)) {
        res.summaryMessage = "Failed to create NIF output directory: " + outDir;
        return res;
    }

    ProceduralMesh mesh{};
    std::string err;
    if (!BuildProceduralMeshFromNif(scene, 126u, &mesh, &err)) {
        res.validationFailures = 1u;
        res.summaryMessage = "NIF mesh build failed (" + sourceLabel + "): " + err;
        return res;
    }

    const std::filesystem::path stemPath(sourceLabel);
    const std::string baseName = "nif_" + SanitizeId(stemPath.stem().string().empty() ? "asset" : stemPath.stem().string());
    mesh.name = baseName;
    ++res.assetsGenerated;

    ValidationReport rep{};
    const bool valid = ValidateProceduralMeshStrict(mesh, &rep, policy);
    AccumulateReport(rep, res);

    const std::string glbRel = baseName + ".glb";
    const std::string objRel = baseName + ".obj";
    if (writeGeometryFiles && valid) {
        if (ExportMeshToGlbFile(mesh, outDir + "/" + glbRel, true)) ++res.filesWritten;
        else ++res.validationFailures;
        if (ExportMeshToObjFile(mesh, outDir + "/" + objRel)) ++res.filesWritten;
        else ++res.validationFailures;
    }

    std::ostringstream js;
    js << std::fixed << std::setprecision(4);
    js << "{\n  \"pipeline\": \"RAW Clean-Room Skyrim SE NIF Asset Bake\",\n"
       << "  \"source\": \"" << JsonEscape(sourceLabel) << "\",\n"
       << "  \"header\": \"" << JsonEscape(scene.header.headerString) << "\",\n"
       << "  \"userVersion\": " << scene.header.userVersion << ",\n"
       << "  \"userVersion2\": " << scene.header.userVersion2 << ",\n"
       << "  \"numBlocks\": " << scene.header.numBlocks << ",\n"
       << "  \"nodeCount\": " << scene.nodes.size() << ",\n"
       << "  \"triShapeCount\": " << scene.shapes.size() << ",\n"
       << "  \"glb\": \"" << (writeGeometryFiles ? glbRel : "") << "\",\n"
       << "  \"obj\": \"" << (writeGeometryFiles ? objRel : "") << "\",\n"
       << "  \"shapes\": [\n";
    for (size_t i = 0u; i < scene.shapes.size(); ++i) {
        const auto& s = scene.shapes[i];
        if (i > 0u) js << ",\n";
        js << "    {"
           << "\"name\":\"" << JsonEscape(s.name) << "\","
           << "\"type\":\"" << JsonEscape(s.blockType) << "\","
           << "\"vertices\":" << s.vertices.size() << ","
           << "\"triangles\":" << (s.indices.size() / 3u) << ","
           << "\"fullPrecision\":" << (s.fullPrecisionPos ? "true" : "false") << ","
           << "\"diffuse\":\"" << JsonEscape(s.textureSet.diffusePath) << "\","
           << "\"normal\":\"" << JsonEscape(s.textureSet.normalPath) << "\","
           << "\"specular\":\"" << JsonEscape(s.textureSet.specularPath) << "\""
           << "}";
    }
    js << "\n  ],\n  \"validation\": " << rep.ToJson() << "\n}\n";

    res.manifestPath = outDir + "/" + baseName + "_manifest.json";
    if (WriteTextFile(res.manifestPath, js.str())) ++res.filesWritten;
    else ++res.validationFailures;

    res.ok = valid && (res.validationFailures == 0u);
    res.summaryMessage = "NifAsset(" + baseName + "): " + std::to_string(scene.shapes.size()) +
                         " shapes, " + std::to_string(rep.meshletCount) + " meshlets, " +
                         std::to_string(rep.triangleCount) + " tris";
    return res;
}

}  // namespace

BakeResult BakeNifFileAsset(const std::string& nifPath,
                            const std::string& outDir,
                            const ValidatorPolicy& policy,
                            bool writeGeometryFiles) {
    raw::worldgen::NifScene scene{};
    std::string err;
    if (!raw::worldgen::ParseNifFile(nifPath, &scene, &err)) {
        BakeResult res{};
        res.validationFailures = 1u;
        res.summaryMessage = "NIF parse failed: " + err;
        return res;
    }
    return BakeParsedNifScene(scene, nifPath, outDir, policy, writeGeometryFiles);
}

BakeResult BakeBsaNifAsset(const std::string& bsaPath,
                           const std::string& virtualNifPath,
                           const std::string& outDir,
                           const ValidatorPolicy& policy,
                           bool writeGeometryFiles) {
    raw::worldgen::RawBsaArchive bsa;
    std::string err;
    if (!bsa.Open(bsaPath, &err)) {
        BakeResult res{};
        res.validationFailures = 1u;
        res.summaryMessage = "BSA open failed: " + err;
        return res;
    }
    std::vector<uint8_t> nifBytes;
    if (!bsa.ExtractFile(virtualNifPath, &nifBytes, &err)) {
        BakeResult res{};
        res.validationFailures = 1u;
        res.summaryMessage = "BSA extract failed: " + err;
        return res;
    }
    raw::worldgen::NifScene scene{};
    if (!raw::worldgen::ParseNifBytes(nifBytes.data(), nifBytes.size(), virtualNifPath, &scene, &err)) {
        BakeResult res{};
        res.validationFailures = 1u;
        res.summaryMessage = "NIF parse failed: " + err;
        return res;
    }
    return BakeParsedNifScene(scene, virtualNifPath, outDir, policy, writeGeometryFiles);
}

BakeResult RunFullPipelineValidation(const std::string& outDir,
                                     uint32_t seed,
                                     float latitudeDeg,
                                     const ValidatorPolicy& policy,
                                     bool writeGeometryFiles) {
    BakeResult total{};
    if (!EnsureDir(outDir)) {
        total.summaryMessage = "Failed to create root pipeline output directory: " + outDir;
        return total;
    }

    const BakeResult flora = BakeSeasonalFloraCatalog(outDir + "/flora", seed, policy, writeGeometryFiles);
    const BakeResult world = BakeWorldShowcaseDiorama(outDir + "/diorama", seed, policy, writeGeometryFiles);
    const BakeResult clim  = BakeAnnualClimateAlmanac(outDir + "/almanac", seed, latitudeDeg);
    const BakeResult mats  = BakeMaterialCatalogManifest(outDir + "/materials");

    auto merge = [&](const BakeResult& sub) {
        total.assetsGenerated    += sub.assetsGenerated;
        total.assetsValidated    += sub.assetsValidated;
        total.validationFailures += sub.validationFailures;
        total.filesWritten       += sub.filesWritten;
        total.totalGpuBytes      += sub.totalGpuBytes;
        total.totalMeshlets      += sub.totalMeshlets;
        total.totalTriangles     += sub.totalTriangles;
        total.reports.insert(total.reports.end(), sub.reports.begin(), sub.reports.end());
    };
    merge(flora);
    merge(world);
    merge(clim);
    merge(mats);

    total.manifestPath = outDir + "/pipeline_validation_report.json";
    if (WriteTextFile(total.manifestPath, SerializeValidationBatchJson(total.reports))) {
        ++total.filesWritten;
    } else {
        ++total.validationFailures;
    }

    total.ok = flora.ok && world.ok && clim.ok && mats.ok && (total.validationFailures == 0u);
    total.summaryMessage = flora.summaryMessage + " | " + world.summaryMessage + " | " +
                           clim.summaryMessage + " | " + mats.summaryMessage;
    return total;
}

}  // namespace raw::pipeline
