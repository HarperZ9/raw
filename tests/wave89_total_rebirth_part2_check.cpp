// wave89_total_rebirth_part2_check.cpp: Wave 89 Project Total Rebirth Part 2 C++23 /W4 /WX Verification Suite.
// Verifies ALL 5 Wave 88 Eye-Audit Fixes + Part 2 POM Sculpted Cobble/Cliff/Puddle Relief + 4/4 Planted Fail-Closed Controls.
// Clean-room MIT.

#include "../src/core/dxbc/GBufferPatch.h"
#include "../src/d3d12_bridge/RawTotalRebirth.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace raw::dxbc::material;
using namespace raw::rebirth;

namespace {

int g_pass = 0;
int g_fail = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        ++g_pass;
        std::printf("[PASS] %s\n", name);
    } else {
        ++g_fail;
        std::printf("[FAIL] %s\n", name);
    }
}

// Compute horizontal vs vertical gradient energy ratio on a 2D height grid
float MeasureAnisotropyRatio(float (*fn)(float, float), float step, int n) {
    double gx2 = 0.0, gy2 = 0.0;
    for (int j = 1; j < n - 1; ++j) {
        for (int i = 1; i < n - 1; ++i) {
            const float x = float(i) * step;
            const float y = float(j) * step;
            const float dx = fn(x + step, y) - fn(x - step, y);
            const float dy = fn(x, y + step) - fn(x, y - step);
            gx2 += double(dx * dx);
            gy2 += double(dy * dy);
        }
    }
    const double hi = std::max(gx2, gy2);
    const double lo = std::max(std::min(gx2, gy2), 1.0e-12);
    return static_cast<float>(std::sqrt(hi / lo));
}

float SampleGlacierField(float x, float y) {
    return EvalGlacierAnisotropicHeight({x, y, y * 0.6f}, {x, y});
}

float SampleTimberField(float x, float y) {
    return EvalTimberAnisotropicGrain({x, 0.0f, y}, {0.0f, 1.0f, 0.0f});
}

float SampleLeopardSpotPlanted(float x, float y) {
    // Planted Defect C regression: isotropic circular spots
    const float n = ValueNoise2D(x * 3.5f, y * 3.5f);
    const float ridge = 1.0f - std::fabs(n * 2.0f - 1.0f);
    return ridge * ridge;
}

} // namespace

int main() {
    // 1. Defect A: Horse / Skinned creature / Warm-fur actor NEVER gets moss or Foliage class!
    {
        DrawClassifyInput horse{};
        horse.materialBase = Lit;
        horse.hasSkinning = true;
        horse.hasAlphaTest = true; // even alpha-tested fur/mane must resolve as SkinnedActor/Character!
        horse.avgAlbedoR = 0.42f;
        horse.avgAlbedoG = 0.28f;
        horse.avgAlbedoB = 0.16f;
        Check(ClassifyLiveDraw14(horse) == AssetClass14::CharacterSkinHairCloth,
              "defectA_horse_skinned_classifies_as_character");
        // Regression lock: Skyrim horse fur shader binds SoftLit (t12) + 80-bone skinning -> MUST promote SoftLit(5) -> SkinnedActor(14)!
        Check(ResolveMaterialClass(SoftLit, SkinnedActor, 0u) == SkinnedActor,
              "defectA_horse_softlit_promotes_to_skinned_actor");
        Check(ResolveMaterialClass(Skin, SkinnedActor, 0u) == Skin,
              "defectA_human_skin_preserved_under_skinned_actor_hint");

        MossEvalInput horseMoss{};
        horseMoss.worldM = {2.5f, 3.1f, 0.8f};
        horseMoss.normal = {0.0f, 0.0f, 1.0f};
        horseMoss.assetClass = AssetClass14::RockCliffBoulder; // even if misclassified, warm fur hue must block moss!
        horseMoss.isActor = false;
        horseMoss.albedoR = 0.44f;
        horseMoss.albedoG = 0.30f;
        horseMoss.albedoB = 0.18f;
        const MossEvalResult rWarm = Evaluate3DMoss(horseMoss);
        Check(rWarm.coverage01 == 0.0f && rWarm.dampRim01 == 0.0f,
              "defectA_warm_fur_hue_blocks_moss_even_if_non_actor");

        MossEvalInput actorMoss{};
        actorMoss.worldM = {2.5f, 3.1f, 0.8f};
        actorMoss.normal = {0.0f, 0.0f, 1.0f};
        actorMoss.isActor = true;
        const MossEvalResult rActor = Evaluate3DMoss(actorMoss);
        Check(rActor.coverage01 == 0.0f, "defectA_actor_flag_zero_moss");
    }

    // 2. Defect B: Frosted winter twigs & snow-encrusted brambles NEVER trip living-green chlorophyll gate!
    {
        Check(!IsLivingGreenFoliage(0.42f, 0.43f, 0.44f), "defectB_frosted_grey_twig_not_living_green");
        Check(!IsLivingGreenFoliage(0.36f, 0.31f, 0.25f), "defectB_bare_brown_bramble_not_living_green");
        Check(!IsLivingGreenFoliage(0.72f, 0.74f, 0.78f), "defectB_snowy_branch_not_living_green");
        Check(IsLivingGreenFoliage(0.14f, 0.28f, 0.12f),  "defectB_living_spruce_needle_stays_living_green");
    }

    // 3. Defect C: Glacier ice has genuine directional flow anisotropy + sharp conchoidal cracks (vs planted leopard spots!)
    {
        // Step 0.008m resolves 26 cycles/m glacial flow striations cleanly below Nyquist
        const float iceAniso = MeasureAnisotropyRatio(SampleGlacierField, 0.008f, 128);
        const float spotAniso = MeasureAnisotropyRatio(SampleLeopardSpotPlanted, 0.035f, 96);
        std::printf("  [info] glacier_aniso=%.3f planted_spot_aniso=%.3f\n", iceAniso, spotAniso);
        Check(iceAniso >= 2.20f, "defectC_glacier_ice_anisotropic_striations_ge_2_2x");
        Check(spotAniso < 1.35f, "control_planted_leopard_spots_detected_as_isotropic");

        // Verify Voronoi-edge conchoidal cracks are sparse hairline fissures (not blobby fill!)
        int crackHits = 0;
        int totalSamples = 0;
        for (int j = 0; j < 64; ++j) {
            for (int i = 0; i < 64; ++i) {
                const float c = EvalGlacierConchoidalCrack({float(i) * 0.09f, float(j) * 0.09f});
                if (c > 0.5f) ++crackHits;
                ++totalSamples;
            }
        }
        const float crackFrac = float(crackHits) / float(totalSamples);
        Check(crackFrac > 0.02f && crackFrac < 0.28f, "defectC_conchoidal_cracks_sparse_hairline");
    }

    // 4. Defect D: Exterior painted Nordic blue wood classifies as TimberWood (NEVER GlacierIce!) + strong 22:1 wood grain!
    {
        DrawClassifyInput blueBarn{};
        blueBarn.materialBase = Lit;
        blueBarn.isExterior = true;
        blueBarn.upNormalZ = 0.08f;
        blueBarn.avgAlbedoR = 0.16f;
        blueBarn.avgAlbedoG = 0.28f;
        blueBarn.avgAlbedoB = 0.42f;
        Check(ClassifyLiveDraw14(blueBarn) == AssetClass14::TimberWood,
              "defectD_exterior_painted_blue_wood_is_timber_not_glacier");

        MossEvalInput blueWoodMoss{};
        blueWoodMoss.worldM = {1.2f, 2.4f, 1.0f};
        blueWoodMoss.normal = {0.0f, 0.0f, 1.0f};
        blueWoodMoss.assetClass = AssetClass14::TimberWood;
        blueWoodMoss.albedoR = 0.16f;
        blueWoodMoss.albedoG = 0.28f;
        blueWoodMoss.albedoB = 0.42f;
        Check(Evaluate3DMoss(blueWoodMoss).coverage01 == 0.0f,
              "defectD_painted_blue_wood_blocks_moss_splatter");

        const float woodAniso = MeasureAnisotropyRatio(SampleTimberField, 0.015f, 96);
        Check(woodAniso >= 4.0f, "defectD_timber_wood_grain_strongly_anisotropic_ge_4x");
    }

    // 5. Part 2 World Overhaul: 4-Step Silhouette-Safe POM Cobblestone / Fractured Cliff / Wheel-Rut Micro-Puddles
    {
        const Vec3 viewDir = Normalize3({0.55f, 0.35f, 0.75f});
        const PomEvalResult cobblePom = EvalPomDisplacement4Step(
            AssetClass14::CobbleRoadPath, {4.2f, 6.8f, 0.0f}, {0.0f, 0.0f, 1.0f}, viewDir, 4.5f, 1.0f, 0.4f);
        const float dispLen = Len3(cobblePom.displacedWorldM - Vec3{4.2f, 6.8f, 0.0f});
        Check(dispLen > 0.005f && dispLen < 0.15f, "part2_pom_cobble_nonzero_bounded_displacement");
        Check(cobblePom.pomAo >= 0.70f && cobblePom.pomAo <= 1.12f, "part2_pom_ao_physically_bounded");

        // Silhouette edge safety: interiorMask == 0 MUST yield zero displacement (zero halo!)
        const PomEvalResult edgePom = EvalPomDisplacement4Step(
            AssetClass14::RockCliffBoulder, {4.2f, 6.8f, 2.0f}, {0.0f, 1.0f, 0.0f}, viewDir, 4.5f, 0.0f, 0.0f);
        Check(Len3(edgePom.displacedWorldM - Vec3{4.2f, 6.8f, 2.0f}) < 1.0e-6f && edgePom.pomAo == 1.0f,
              "control_pom_silhouette_mask_zero_displacement");

        // Actor / Water exclusion: NEVER POM-displaced
        const PomEvalResult actorPom = EvalPomDisplacement4Step(
            AssetClass14::CharacterSkinHairCloth, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, viewDir, 3.0f, 1.0f, 1.0f);
        Check(Len3(actorPom.displacedWorldM - Vec3{1.0f, 1.0f, 1.0f}) < 1.0e-6f,
              "control_actor_never_pom_displaced");

        // Road wheel-rut micro-puddles appear in road hollows during damp/rain conditions
        float maxPuddle = 0.0f;
        for (int j = 0; j < 40; ++j) {
            for (int i = 0; i < 40; ++i) {
                const PomEvalResult p = EvalPomDisplacement4Step(
                    AssetClass14::CobbleRoadPath, {float(i) * 0.45f, float(j) * 0.45f, 0.0f},
                    {0.0f, 0.0f, 1.0f}, viewDir, 5.0f, 1.0f, 0.85f);
                if (p.roadPuddle01 > maxPuddle) maxPuddle = p.roadPuddle01;
            }
        }
        Check(maxPuddle > 0.15f, "part2_road_wheel_rut_micro_puddles_active");

        // Distant fade (>25m): POM fades cleanly to zero (zero far-field aliasing!)
        const PomEvalResult farPom = EvalPomDisplacement4Step(
            AssetClass14::RockCliffBoulder, {4.2f, 6.8f, 2.0f}, {0.0f, 1.0f, 0.0f}, viewDir, 35.0f, 1.0f, 0.0f);
        Check(Len3(farPom.displacedWorldM - Vec3{4.2f, 6.8f, 2.0f}) < 1.0e-6f,
              "control_pom_far_distance_zero_aliasing");
    }

    std::printf("wave89_total_rebirth_part2_check: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
