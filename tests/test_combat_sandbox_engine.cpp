// test_combat_sandbox_engine.cpp: Verification Suite for `raw::worldgen::RawCombatSandboxEngine`.
// Exports BOTH `int run_combat_sandbox_tests()` and
// `int raw::worldgen::RunCombatSandboxTests(void (*checkFn)(bool, const char*) = nullptr)`
// plus `#ifdef RAW_COMBAT_SANDBOX_TEST_STANDALONE` standalone `main()`.
// Original work, MIT.

#include "RawCombatSandboxEngine.h"

#include <chrono>
#include <cmath>
#include <cstdio>

namespace raw::worldgen {
namespace {

bool IsFinite3(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

TerrainCellHeightfield MakeFlatArena(uint64_t key) {
    TerrainCellHeightfield hf{};
    hf.cellKey     = key;
    hf.originX     = -2048.0f;
    hf.originY     = -2048.0f;
    hf.sizeX       = 4096.0f;
    hf.sizeY       = 4096.0f;
    hf.gridW       = 17u;
    hf.gridH       = 17u;
    hf.waterLevelZ = -14000.0f;
    hf.heights.assign(17u * 17u, 0.0f);
    return hf;
}

TriangleMeshCollider MakeStoneWall(Vec3 bmin, Vec3 bmax) {
    TriangleMeshCollider m{};
    m.instanceId  = 42u;
    m.materialTag = 1u;
    m.vertices = {
        {bmin.x, bmin.y, bmin.z}, {bmax.x, bmin.y, bmin.z},
        {bmax.x, bmax.y, bmin.z}, {bmin.x, bmax.y, bmin.z},
        {bmin.x, bmin.y, bmax.z}, {bmax.x, bmin.y, bmax.z},
        {bmax.x, bmax.y, bmax.z}, {bmin.x, bmax.y, bmax.z},
    };
    m.indices = {
        0, 2, 1,  0, 3, 2,  4, 5, 6,  4, 6, 7,
        0, 1, 5,  0, 5, 4,  3, 6, 2,  3, 7, 6,
        0, 4, 7,  0, 7, 3,  1, 2, 6,  1, 6, 5,
    };
    return m;
}

}  // namespace

int RunCombatSandboxTests(void (*checkFn)(bool, const char*)) {
    int failures = 0;
    auto check = [&](bool cond, const char* name) {
        if (!cond) {
            ++failures;
        }
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
        }
    };

    // =========================================================================
    // Test 1: Planted-failure controls (zero dt no-op, unknown actor ID safe)
    // =========================================================================
    {
        RawCombatSandboxEngine eng;
        CombatWeatherInput wx{};
        const CombatSandboxTelemetry emptyT = eng.Step(0.0f, wx);
        const bool badCmdsFalse = !eng.StartMeleeAttack(999u, 1u) &&
                                  !eng.RaiseBlock(999u) &&
                                  !eng.TriggerShout(999u) &&
                                  (eng.FireBallisticProjectile(999u, {100, 0, 0}) == 0u);
        check(emptyT.aliveAllies == 0u && emptyT.aliveHostiles == 0u && badCmdsFalse,
              "combat_sandbox_planted_failure_controls");
    }

    // =========================================================================
    // Test 2: 220ms Timed Parry (Negates Damage + Staggers Attacker) & Poise Break
    // =========================================================================
    {
        RawCombatSandboxEngine eng;
        CombatActor atk{};
        atk.faction      = CombatFaction::Hostiles;
        atk.position     = {0.0f, 0.0f, 0.0f};
        atk.facing       = {1.0f, 0.0f, 0.0f};
        atk.aiControlled = false;
        const uint32_t atkId = eng.SpawnActor(atk);

        CombatActor def{};
        def.faction      = CombatFaction::PlayerAllies;
        def.position     = {100.0f, 0.0f, 0.0f};
        def.facing       = {-1.0f, 0.0f, 0.0f}; // Facing attacker!
        def.aiControlled = false;
        const uint32_t defId = eng.SpawnActor(def);

        // Raise block and step 120ms (strictly inside 220ms timed-parry window!):
        eng.RaiseBlock(defId);
        CombatWeatherInput dry{};
        eng.Step(0.120f, dry);

        const float defHpBefore = eng.FindActor(defId)->hp;
        const HitResolution parryRes = eng.ResolveMeleeHit(
            *eng.FindActor(atkId), *eng.FindActor(defId), 45.0f, 50.0f, DamageElement::Physical, dry);

        const bool atkStaggeredOnParry = (eng.FindActor(atkId)->state == CombatActionState::Staggered);
        const bool parryOk = parryRes.parried && !parryRes.landed && parryRes.finalDamage == 0.0f &&
                             (eng.FindActor(defId)->hp == defHpBefore) && atkStaggeredOnParry;

        // Now test Poise Break on an unblocked defender (poise = 100):
        eng.FindActor(atkId)->state = CombatActionState::Idle;
        eng.FindActor(defId)->state = CombatActionState::Idle;
        eng.ResolveMeleeHit(*eng.FindActor(atkId), *eng.FindActor(defId), 30.0f, 60.0f, DamageElement::Physical, dry);
        const HitResolution breakRes = eng.ResolveMeleeHit(
            *eng.FindActor(atkId), *eng.FindActor(defId), 30.0f, 60.0f, DamageElement::Physical, dry);
        const bool poiseBreakOk = breakRes.landed && breakRes.poiseBroken &&
                                  (eng.FindActor(defId)->state == CombatActionState::Staggered);

        // Verify skeleton weapon hand pose evaluation is finite:
        const Vec3 handPos = RawCombatSandboxEngine::EvaluateActorWeaponHandPos(*eng.FindActor(atkId));

        std::printf("  [220ms Parry + PoiseBreak] parried=%d dmg=%.1f atkStaggered=%d poiseBroken=%d handZ=%.1f\n",
                    parryRes.parried ? 1 : 0, parryRes.finalDamage,
                    atkStaggeredOnParry ? 1 : 0, breakRes.poiseBroken ? 1 : 0, handPos.z);

        check(parryOk && poiseBreakOk && IsFinite3(handPos),
              "combat_sandbox_220ms_timed_parry_and_poise_break");
    }

    // =========================================================================
    // Test 3: Wet / Freezing Weather Elemental Synergy & Stamina Regen
    // =========================================================================
    {
        CombatWeatherInput dryWarm{};
        dryWarm.wetness01    = 0.0f;
        dryWarm.ambientTempC = 16.0f;

        CombatWeatherInput wetCold{};
        wetCold.wetness01    = 1.0f;
        wetCold.ambientTempC = -15.0f;

        const float shockDry  = EvaluateCombatElementalMult(DamageElement::Shock, dryWarm);
        const float shockWet  = EvaluateCombatElementalMult(DamageElement::Shock, wetCold);
        const float fireDry   = EvaluateCombatElementalMult(DamageElement::Fire, dryWarm);
        const float fireWet   = EvaluateCombatElementalMult(DamageElement::Fire, wetCold);
        const float frostDry  = EvaluateCombatElementalMult(DamageElement::Frost, dryWarm);
        const float frostWet  = EvaluateCombatElementalMult(DamageElement::Frost, wetCold);
        const float stamWarm  = EvaluateThermalStaminaRegenMult(dryWarm.ambientTempC);
        const float stamCold  = EvaluateThermalStaminaRegenMult(wetCold.ambientTempC);

        std::printf(
            "  [Weather Synergy] shock %.2f->%.2f fire %.2f->%.2f frost %.2f->%.2f stamRegen %.2f->%.2f\n",
            shockDry, shockWet, fireDry, fireWet, frostDry, frostWet, stamWarm, stamCold);

        check(shockWet > shockDry * 1.25f && fireWet < fireDry * 0.85f &&
                  frostWet > frostDry * 1.25f && stamCold < stamWarm * 0.85f,
              "combat_sandbox_weather_elemental_and_thermal_synergy");
    }

    // =========================================================================
    // Test 4: Ballistic Arrow Raycast -- Clear LOS Hit vs Stone Wall Obstacle Block
    // =========================================================================
    {
        // 4a. Clear LOS shot hits enemy cleanly:
        RawWorldPhysics openWorld;
        openWorld.AddTerrainHeightfield(MakeFlatArena(1ull));
        RawCombatSandboxEngine openEng;

        CombatActor archer{};
        archer.faction = CombatFaction::PlayerAllies;
        archer.role    = CombatRole::Archer;
        archer.position = {0.0f, 0.0f, 0.0f};
        archer.aiControlled = false;
        const uint32_t archerId = openEng.SpawnActor(archer);

        CombatActor dummy{};
        dummy.faction  = CombatFaction::Hostiles;
        dummy.position = {700.0f, 0.0f, 0.0f};
        dummy.aiControlled = false;
        const uint32_t dummyId = openEng.SpawnActor(dummy);

        openEng.FireBallisticProjectile(archerId, {700.0f, 0.0f, 0.0f}, 2400.0f, DamageElement::Physical);
        CombatWeatherInput calm{};
        calm.windSpeedMps = 0.0f;
        for (int f = 0; f < 40; ++f) {
            openEng.Step(1.0f / 60.0f, calm, &openWorld);
        }
        const bool clearShotHit = (openEng.FindActor(dummyId)->hp < 200.0f);

        // 4b. Solid stone wall between archer and target blocks the arrow cleanly!
        RawWorldPhysics walledWorld;
        walledWorld.AddTerrainHeightfield(MakeFlatArena(1ull));
        walledWorld.AddCellCollider(2ull, MakeStoneWall({320.0f, -200.0f, 0.0f}, {380.0f, 200.0f, 280.0f}));

        RawCombatSandboxEngine wallEng;
        const uint32_t wArcher = wallEng.SpawnActor(archer);
        const uint32_t wDummy  = wallEng.SpawnActor(dummy);
        wallEng.FireBallisticProjectile(wArcher, {700.0f, 0.0f, 0.0f}, 2400.0f, DamageElement::Physical);
        for (int f = 0; f < 40; ++f) {
            wallEng.Step(1.0f / 60.0f, calm, &walledWorld);
        }
        const bool wallBlocked = (wallEng.FindActor(wDummy)->hp == 200.0f) &&
                                 (wallEng.ComputeTelemetry().totalArrowBlocks >= 1u);

        std::printf("  [Ballistic Arrow LOS] clearShotHit=%d wallBlocked=%d(blocks=%u)\n",
                    clearShotHit ? 1 : 0, wallBlocked ? 1 : 0, wallEng.ComputeTelemetry().totalArrowBlocks);

        check(clearShotHit && wallBlocked, "combat_sandbox_ballistic_arrow_los_vs_obstacle_block");
    }

    // =========================================================================
    // Test 5: 16-Actor Melee + Shield + Archer + Battlemage Battle Sim & < 0.30 ms/tick Perf
    // =========================================================================
    {
        RawWorldPhysics arena;
        arena.AddTerrainHeightfield(MakeFlatArena(1ull));
        RawCombatSandboxEngine battle;

        // Spawn 8 PlayerAllies vs 8 Hostiles (mixed MeleeWarrior, ShieldBearer, Archer, Battlemage):
        for (int i = 0; i < 8; ++i) {
            CombatActor ally{};
            ally.faction       = CombatFaction::PlayerAllies;
            ally.role          = static_cast<CombatRole>(i % 4);
            ally.position      = {-320.0f - static_cast<float>((i % 4) >= 2 ? 220 : 0),
                                  -280.0f + static_cast<float>(i) * 80.0f, 0.0f};
            ally.facing        = {1.0f, 0.0f, 0.0f};
            ally.attackElement = (ally.role == CombatRole::Battlemage) ? DamageElement::Shock : DamageElement::Physical;
            battle.SpawnActor(ally);

            CombatActor foe{};
            foe.faction       = CombatFaction::Hostiles;
            foe.role          = static_cast<CombatRole>(i % 4);
            foe.position      = {320.0f + static_cast<float>((i % 4) >= 2 ? 220 : 0),
                                 -280.0f + static_cast<float>(i) * 80.0f, 0.0f};
            foe.facing        = {-1.0f, 0.0f, 0.0f};
            foe.attackElement = (foe.role == CombatRole::Battlemage) ? DamageElement::Frost : DamageElement::Physical;
            battle.SpawnActor(foe);
        }

        CombatWeatherInput rainBattle{};
        rainBattle.wetness01    = 0.75f;
        rainBattle.ambientTempC = 4.0f;
        rainBattle.windSpeedMps = 6.0f;

        constexpr int kTicks = 180; // 3.0 seconds @ 60 Hz
        const auto bt0 = std::chrono::steady_clock::now();
        for (int t = 0; t < kTicks; ++t) {
            battle.Step(1.0f / 60.0f, rainBattle, &arena);
        }
        const auto bt1 = std::chrono::steady_clock::now();
        const double meanTickMs =
            std::chrono::duration<double, std::milli>(bt1 - bt0).count() / static_cast<double>(kTicks);
        const CombatSandboxTelemetry finalTel = battle.ComputeTelemetry();

        bool allFinite = true;
        for (const CombatActor& a : battle.Actors()) {
            if (!IsFinite3(a.position) || !std::isfinite(a.hp) || a.hp < 0.0f ||
                !std::isfinite(a.stamina) || !std::isfinite(a.poise)) {
                allFinite = false;
            }
        }

        std::printf(
            "  [16-Actor Squad Battle 180 ticks] allies=%u hostiles=%u hits=%u parries=%u poiseBreaks=%u arrows=%u meanTick=%.3fms\n",
            finalTel.aliveAllies, finalTel.aliveHostiles, finalTel.totalHitsLanded,
            finalTel.totalParries, finalTel.totalPoiseBreaks, finalTel.totalArrowsFired, meanTickMs);

        check(allFinite && finalTel.totalHitsLanded >= 10u && finalTel.totalArrowsFired >= 4u &&
                  meanTickMs < 0.30,
              "combat_sandbox_16_actor_battle_and_perf");
    }

    return failures;
}

}  // namespace raw::worldgen

int run_combat_sandbox_tests() {
    return raw::worldgen::RunCombatSandboxTests(nullptr);
}

#ifdef RAW_COMBAT_SANDBOX_TEST_STANDALONE
int main() {
    const int failures = run_combat_sandbox_tests();
    std::printf("RawCombatSandboxEngine standalone test summary: %s (%d failures)\n",
                failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
#endif
