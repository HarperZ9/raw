#pragma once
// RawMaterialsTests.h: Self-Contained Known-Answer & Planted-Failure Verification Suite
// for raw::materials (RawMaterialLib + RawAssetForge).
// Wire into ce_runtime_check/main.cpp in 1 line: raw::materials::RunRawMaterialsTests(Check);
// Original work, MIT.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "RawAssetForge.h"
#include "RawMaterialLib.h"

namespace raw::materials {

template <typename CheckFn>
inline void RunRawMaterialsTests(CheckFn&& check) {
    const auto& reg = DefaultMaterialRegistry();

    // 1. Registry completeness across all 8 core families + 64-byte GPU record packing round-trip + JSON export
    {
        bool allFamiliesPopulated = true;
        for (uint8_t f = 0; f < static_cast<uint8_t>(MaterialFamily::Count); ++f) {
            if (reg.ListByFamily(static_cast<MaterialFamily>(f)).empty()) {
                allFamiliesPopulated = false;
                break;
            }
        }
        bool allValidPhysics = true;
        for (const auto& m : reg.All()) {
            if (!ValidateMaterialPhysics(m)) {
                allValidPhysics = false;
                break;
            }
        }
        const auto gpuBuf = reg.BuildGpuBuffer();
        const auto* ice = reg.FindById(MaterialId::GlacialIce);
        const auto* gold = reg.FindByName("GoldMeasured");
        bool packRoundTrip = false;
        if (ice && gold) {
            const GpuMaterialRecord rIce = PackGpuMaterialRecord(*ice);
            const GpuMaterialRecord rGold = PackGpuMaterialRecord(*gold);
            packRoundTrip =
                (sizeof(GpuMaterialRecord) == 64) &&
                (UnpackGpuMaterialFamily(rIce) == MaterialFamily::FrostGlacial) &&
                (UnpackGpuMaterialIdLo(rIce) == static_cast<uint16_t>(MaterialId::GlacialIce)) &&
                (std::fabs(UnpackGpuSssMfpMeters(rIce) - ice->sssMeanFreePathM) < 0.005f) &&
                (UnpackGpuMaterialFamily(rGold) == MaterialFamily::ForgedMetal) &&
                (rGold.metallic > 0.99f);
        }
        const std::string json = reg.ExportAllToJson();
        const bool jsonOk =
            json.find("\"schema\":\"raw.materials.v1\"") != std::string::npos &&
            json.find("\"GlacialIce\"") != std::string::npos &&
            json.find("\"DiamondCut\"") != std::string::npos &&
            json.find("\"DaedricObsidianMetal\"") != std::string::npos;

        check(allFamiliesPopulated && allValidPhysics && reg.All().size() >= 24u &&
              gpuBuf.size() == reg.All().size() && packRoundTrip && jsonOk,
              "raw_materials_registry_8families_gpu64_json");
    }

    // 2. Known-answer Dielectric Fresnel & strict [0, 1] bounds across angles and media
    {
        const float fGlassN = EvaluateDielectricFresnel(1.0f, 1.5f);    // exact ((0.5/2.5)^2) = 0.04
        const float fIceN   = EvaluateDielectricFresnel(1.0f, 1.31f);   // ~0.01799
        const float fDiamN  = EvaluateDielectricFresnel(1.0f, 2.42f);   // ~0.1724
        const float fGraze  = EvaluateDielectricFresnel(0.0f, 1.52f);   // exact 1.0
        bool boundsMonotone = true;
        float prevF = 1.01f;
        for (int i = 0; i <= 20; ++i) {
            const float c = static_cast<float>(i) / 20.0f;
            const float f = EvaluateDielectricFresnel(c, 1.52f);
            if (!(f >= 0.0f && f <= 1.0f && f <= prevF + 1e-5f)) {
                boundsMonotone = false;
                break;
            }
            prevF = f;
        }
        check(std::fabs(fGlassN - 0.04f) < 1e-4f &&
              std::fabs(fIceN - 0.0180f) < 5e-4f &&
              std::fabs(fDiamN - 0.1724f) < 5e-4f &&
              std::fabs(fGraze - 1.0f) < 1e-4f &&
              boundsMonotone,
              "raw_materials_dielectric_fresnel_known_answers");
    }

    // 3. Complex-IOR Conductor Fresnel energy conservation & spectral signature (Gold, Silver, all 8 metals)
    {
        const auto metals = reg.ListByFamily(MaterialFamily::ForgedMetal);
        bool allMetalsOk = (metals.size() >= 8u);
        for (const auto* m : metals) {
            for (int i = 0; i <= 10; ++i) {
                const float c = static_cast<float>(i) / 10.0f;
                const Vec3 f = EvaluateConductorFresnelRGB(c, m->etaRGB, m->kRGB);
                if (!(f.x >= 0.0f && f.x <= 1.0f &&
                      f.y >= 0.0f && f.y <= 1.0f &&
                      f.z >= 0.0f && f.z <= 1.0f)) {
                    allMetalsOk = false;
                }
            }
            const Vec3 fg = EvaluateConductorFresnelRGB(0.0f, m->etaRGB, m->kRGB);
            if (fg.x < 0.99f || fg.y < 0.99f || fg.z < 0.99f) allMetalsOk = false;
        }
        const auto* gold = reg.FindById(MaterialId::GoldMeasured);
        const auto* silver = reg.FindById(MaterialId::SilverPolished);
        const Vec3 fGold = gold ? EvaluateConductorFresnelRGB(1.0f, gold->etaRGB, gold->kRGB) : Vec3{};
        const Vec3 fSilver = silver ? EvaluateConductorFresnelRGB(1.0f, silver->etaRGB, silver->kRGB) : Vec3{};
        check(allMetalsOk &&
              (fGold.x > fGold.y && fGold.y > fGold.z && fGold.x > 0.90f) &&
              (fSilver.x > 0.90f && fSilver.y > 0.90f && fSilver.z > 0.90f),
              "raw_materials_conductor_fresnel_energy_conservation");
    }

    // 4. Cauchy RGB dispersion monotonicity (n_blue > n_green > n_red >= 1, diamond > sapphire > crown glass)
    {
        const auto* glass = reg.FindById(MaterialId::CrownGlass);
        const auto* saph  = reg.FindById(MaterialId::SapphireCrystal);
        const auto* diam  = reg.FindById(MaterialId::DiamondCut);
        bool cauchyOk = (glass && saph && diam);
        if (cauchyOk) {
            const Vec3 nG = EvaluateCauchyRGB(glass->baseIor540, glass->cauchyB);
            const Vec3 nS = EvaluateCauchyRGB(saph->baseIor540,  saph->cauchyB);
            const Vec3 nD = EvaluateCauchyRGB(diam->baseIor540,  diam->cauchyB);
            const float spreadG = nG.z - nG.x;
            const float spreadS = nS.z - nS.x;
            const float spreadD = nD.z - nD.x;
            cauchyOk =
                (nG.z > nG.y && nG.y > nG.x && std::fabs(nG.y - 1.5190f) < 1e-4f) &&
                (nS.z > nS.y && nS.y > nS.x && std::fabs(nS.y - 1.7711f) < 1e-4f) &&
                (nD.z > nD.y && nD.y > nD.x && std::fabs(nD.y - 2.4240f) < 1e-4f) &&
                (spreadD > spreadS && spreadS > spreadG && spreadG > 0.005f);
        }
        check(cauchyOk, "raw_materials_cauchy_dispersion_monotonicity");
    }

    // 5. Planck blackbody monotonicity across 1000K..3400K + Beer-Lambert known answer
    {
        bool bbMono = true;
        float prevRad = -1.0f;
        float prevBlueOverRed = -1.0f;
        for (int tK = 1000; tK <= 3400; tK += 200) {
            float rad = 0.0f;
            const Vec3 rgb = EvaluatePlanckBlackbodyRGB(static_cast<float>(tK), &rad);
            const float bOverR = rgb.z / std::max(rgb.x, 1.0e-5f);
            if (!(rad > prevRad && bOverR >= prevBlueOverRed - 1e-5f &&
                  rgb.x >= 0.0f && rgb.x <= 1.0f && rgb.y >= 0.0f && rgb.y <= 1.0f && rgb.z >= 0.0f && rgb.z <= 1.0f)) {
                bbMono = false;
                break;
            }
            prevRad = rad;
            prevBlueOverRed = bOverR;
        }
        const Vec3 bb1200 = EvaluatePlanckBlackbodyRGB(1200.0f);
        const Vec3 bl0 = EvaluateBeerLambert({0.5f, 0.25f, 0.1f}, 0.0f);
        const Vec3 bl1 = EvaluateBeerLambert({1.0f, 0.5f, 0.25f}, 1.0f);
        const bool blOk =
            (std::fabs(bl0.x - 1.0f) < 1e-5f) &&
            (std::fabs(bl1.x - std::exp(-1.0f)) < 1e-4f) &&
            (bl1.z > bl1.y && bl1.y > bl1.x);

        check(bbMono && (bb1200.x > bb1200.y && bb1200.y > bb1200.z) && blOk,
              "raw_materials_planck_blackbody_and_beer_lambert");
    }

    // 6. Procedural PBR Asset Forge across all 8 archetypes: non-degenerate, unit normals, seamless wrap, emissive & PPM/RAW IO
    {
        bool allForgeOk = true;
        for (uint8_t a = 0; a < static_cast<uint8_t>(ForgeArchetype::Count); ++a) {
            ProceduralPbrSet set{};
            const auto arch = static_cast<ForgeArchetype>(a);
            if (!SynthesizePbrSet(arch, 64u, 64u, 2026u + a * 17u, &set) ||
                set.albedoRgba8.size() != 64u * 64u * 4u ||
                set.normalRgba8.size() != 64u * 64u * 4u ||
                set.rmaohRgba8.size() != 64u * 64u * 4u ||
                set.emissiveRgba8.size() != 64u * 64u * 4u) {
                allForgeOk = false;
                break;
            }
            // Verify non-degenerate albedo/height variation and unit-length tangent normals
            uint8_t minH = 255u, maxH = 0u;
            uint8_t maxEm = 0u;
            for (size_t i = 0; i < set.normalRgba8.size(); i += 4u) {
                const float nx = static_cast<float>(set.normalRgba8[i + 0]) * (2.0f / 255.0f) - 1.0f;
                const float ny = static_cast<float>(set.normalRgba8[i + 1]) * (2.0f / 255.0f) - 1.0f;
                const float nz = static_cast<float>(set.normalRgba8[i + 2]) * (2.0f / 255.0f) - 1.0f;
                const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (std::fabs(len - 1.0f) > 0.08f || nz <= 0.0f) {
                    allForgeOk = false;
                    break;
                }
                minH = std::min(minH, set.rmaohRgba8[i + 3]);
                maxH = std::max(maxH, set.rmaohRgba8[i + 3]);
                maxEm = std::max(maxEm, set.emissiveRgba8[i + 3]);
            }
            if (!allForgeOk || static_cast<int>(maxH - minH) < 20) {
                allForgeOk = false;
                break;
            }
            // Active emissive on MagmaHeat & ArcaneMagic; zero emissive on passive BasaltStone & AlpineSnow
            if ((arch == ForgeArchetype::MagmaHeat || arch == ForgeArchetype::ArcaneMagic) && maxEm < 40u) {
                allForgeOk = false;
                break;
            }
            if ((arch == ForgeArchetype::BasaltStone || arch == ForgeArchetype::AlpineSnow) && maxEm != 0u) {
                allForgeOk = false;
                break;
            }
            // Seamless torus tiling check: wrap seams (x=0|W-1 and y=0|H-1) must not exceed interior 1px step bounds
            double seamX = 0.0, seamY = 0.0;
            double maxColDelta = 0.0, maxRowDelta = 0.0;
            for (uint32_t y = 0; y < 64u; ++y) {
                const size_t iL = (static_cast<size_t>(y) * 64u + 0u) * 4u + 3u;
                const size_t iR = (static_cast<size_t>(y) * 64u + 63u) * 4u + 3u;
                seamX += std::abs(static_cast<int>(set.rmaohRgba8[iL]) - static_cast<int>(set.rmaohRgba8[iR]));
            }
            for (uint32_t x = 0; x < 64u; ++x) {
                const size_t iT = (0u * 64u + static_cast<size_t>(x)) * 4u + 3u;
                const size_t iB = (63u * 64u + static_cast<size_t>(x)) * 4u + 3u;
                seamY += std::abs(static_cast<int>(set.rmaohRgba8[iT]) - static_cast<int>(set.rmaohRgba8[iB]));
            }
            for (uint32_t x = 0; x < 63u; ++x) {
                double colD = 0.0;
                for (uint32_t y = 0; y < 64u; ++y) {
                    const size_t i0 = (static_cast<size_t>(y) * 64u + x) * 4u + 3u;
                    const size_t i1 = (static_cast<size_t>(y) * 64u + x + 1u) * 4u + 3u;
                    colD += std::abs(static_cast<int>(set.rmaohRgba8[i1]) - static_cast<int>(set.rmaohRgba8[i0]));
                }
                maxColDelta = std::max(maxColDelta, colD / 64.0);
            }
            for (uint32_t y = 0; y < 63u; ++y) {
                double rowD = 0.0;
                for (uint32_t x = 0; x < 64u; ++x) {
                    const size_t i0 = (static_cast<size_t>(y) * 64u + x) * 4u + 3u;
                    const size_t i1 = (static_cast<size_t>(y + 1u) * 64u + x) * 4u + 3u;
                    rowD += std::abs(static_cast<int>(set.rmaohRgba8[i1]) - static_cast<int>(set.rmaohRgba8[i0]));
                }
                maxRowDelta = std::max(maxRowDelta, rowD / 64.0);
            }
            seamX /= 64.0;
            seamY /= 64.0;
            if (seamX > maxColDelta * 1.35 + 4.0 || seamY > maxRowDelta * 1.35 + 4.0) {
                allForgeOk = false;
                break;
            }
        }

        // Verify .ppm & .raw export round-trip in system temp dir and clean up
        bool ioOk = false;
        ProceduralPbrSet magmaSet{};
        if (SynthesizePbrSet(ForgeArchetype::MagmaHeat, 32u, 32u, 77u, &magmaSet)) {
            const auto tmpDir = std::filesystem::temp_directory_path() / "raw_materials_forge_check";
            std::string ioErr;
            if (ExportPbrSetToDirectory(magmaSet, tmpDir.string(), "magma_test", &ioErr)) {
                const auto ppmPath = tmpDir / "magma_test_albedo.ppm";
                const auto rawPath = tmpDir / "magma_test_rmaoh.raw";
                std::ifstream pf(ppmPath, std::ios::binary);
                char hdr[3] = {};
                pf.read(hdr, 2);
                std::error_code ec;
                const auto rawSz = std::filesystem::file_size(rawPath, ec);
                ioOk = (hdr[0] == 'P' && hdr[1] == '6' && !ec && rawSz == 32u * 32u * 4u);
            }
            std::error_code ecClean;
            std::filesystem::remove_all(tmpDir, ecClean);
        }

        check(allForgeOk && ioOk, "raw_materials_asset_forge_8archetypes_seamless_io");
    }

    // 7. Planted-failure controls (MUST reject invalid physics, anomalous Cauchy dispersion, NaN, and bad forge dimensions)
    {
        PhysicalMaterial badEnergy = *reg.FindById(MaterialId::CrownGlass);
        badEnergy.albedo = {1.35f, 0.5f, 0.5f};  // planted energy conservation violation
        const bool rejectEnergy = !ValidateMaterialPhysics(badEnergy);

        PhysicalMaterial badIor = *reg.FindById(MaterialId::GlacialIce);
        badIor.baseIor540 = 0.65f;               // planted unphysical dielectric IOR < 1
        badIor.etaRGB = {0.65f, 0.65f, 0.65f};
        const bool rejectIor = !ValidateMaterialPhysics(badIor);

        PhysicalMaterial badCauchy = *reg.FindById(MaterialId::DiamondCut);
        badCauchy.cauchyB = -0.025f;             // planted wrong-sign anomalous Cauchy dispersion
        badCauchy.etaRGB = EvaluateCauchyRGB(badCauchy.baseIor540, badCauchy.cauchyB);
        const bool rejectCauchy = !ValidateMaterialPhysics(badCauchy) && (badCauchy.etaRGB.z < badCauchy.etaRGB.x);

        PhysicalMaterial badNan = *reg.FindById(MaterialId::NordicSteel);
        badNan.roughness = std::numeric_limits<float>::quiet_NaN();
        const bool rejectNan = !ValidateMaterialPhysics(badNan);

        ProceduralPbrSet badSet{};
        const bool rejectSmallForge = !SynthesizePbrSet(ForgeArchetype::BasaltStone, 4u, 4u, 1u, &badSet);
        const bool rejectNullForge  = !SynthesizePbrSet(ForgeArchetype::BasaltStone, 64u, 64u, 1u, nullptr);

        check(rejectEnergy && rejectIor && rejectCauchy && rejectNan && rejectSmallForge && rejectNullForge,
              "raw_materials_planted_failure_controls");
    }
}

}  // namespace raw::materials
