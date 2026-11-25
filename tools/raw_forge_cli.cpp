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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

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
        "  raw_forge_cli --esm <path> --cell <x,y> --out <dir>     Extract, validate & export real Tamriel ESM cell\n"
        "  raw_forge_cli --bsa-list <path> [--filter <substr>]     Inspect & list files in a v104/v105 .bsa archive\n"
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
    std::string validateAllDir;
    std::string esmPath;
    std::string esmOutDir;
    std::string bsaListPath;
    std::string bsaFilter;
    bool hasEsmCell     = false;
    int32_t esmCellX    = 0;
    int32_t esmCellY    = 0;
    uint32_t listLimit  = 50u;
    bool runValidateAll = false;
    bool writeGeometry  = true;
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
        } else if (arg == "--esm") {
            const char* v = needNext("--esm");
            if (!v) return 2;
            esmPath = v;
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
        } else if (arg == "--filter") {
            const char* v = needNext("--filter");
            if (!v) return 2;
            bsaFilter = v;
        } else if (arg == "--limit") {
            const char* v = needNext("--limit");
            if (!v) return 2;
            listLimit = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
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

    if (!runValidateAll && floraDir.empty() && dioramaDir.empty() && almanacDir.empty() &&
        materialsDir.empty() && esmPath.empty() && bsaListPath.empty()) {
        std::fprintf(stderr, "Error: specify at least one action (--flora, --diorama, --almanac, --materials, --esm, --bsa-list, or --validate-all).\n");
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
    }

    return allOk ? 0 : 1;
}
