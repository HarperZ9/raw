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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

void PrintUsage() {
    std::printf(
        "RAW Forge Studio Pipeline CLI (Clean-Room MIT)\n"
        "Usage:\n"
        "  raw_forge_cli --flora <dir>        Bake & validate 5 species x 4 seasons x 3 LODs\n"
        "  raw_forge_cli --diorama <dir>      Bake & validate 6 frontier biomes + hero props\n"
        "  raw_forge_cli --almanac <dir>      Bake 365-day orbital climate & phenology almanac\n"
        "  raw_forge_cli --materials <dir>    Validate & export physical optical material catalog\n"
        "  raw_forge_cli --validate-all [dir] Run all 4 pipelines + quality-gate validation\n"
        "Options:\n"
        "  --seed <N>         Deterministic RNG seed (default: 202607)\n"
        "  --latitude <deg>   Orbital latitude in degrees North (default: 60.0)\n"
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

    if (!runValidateAll && floraDir.empty() && dioramaDir.empty() && almanacDir.empty() && materialsDir.empty()) {
        std::fprintf(stderr, "Error: specify at least one action (--flora, --diorama, --almanac, --materials, or --validate-all).\n");
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
