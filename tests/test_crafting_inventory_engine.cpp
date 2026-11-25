// test_crafting_inventory_engine.cpp: Verification Suite for `raw::worldgen::RawCraftingInventoryEngine`.
// Exports BOTH `int run_crafting_inventory_tests()` and
// `int raw::worldgen::RunCraftingInventoryTests(void (*checkFn)(bool, const char*) = nullptr)`
// plus `#ifdef RAW_CRAFTING_INVENTORY_TEST_STANDALONE` standalone `main()`.
// Original work, MIT.

#include "RawCraftingInventoryEngine.h"

#include <cmath>
#include <cstdio>

namespace raw::worldgen {

int RunCraftingInventoryTests(void (*checkFn)(bool, const char*)) {
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
    // Test 1: Planted-failure controls (bad IDs, non-equippable, incompatible alchemy, duplicate ing)
    // =========================================================================
    {
        RawCraftingInventoryEngine inv(300.0f);
        ItemDef ingA{};
        ingA.name = "Blue Mountain Flower";
        ingA.category = ItemCategory::Ingredient;
        ingA.ingredientEffects = {AlchemyEffect::RestoreHealth, AlchemyEffect::FortifyEnchanting,
                                  AlchemyEffect::None, AlchemyEffect::None};
        const uint32_t idA = inv.AddItem(ingA, 2u);

        ItemDef ingB{};
        ingB.name = "Fire Salts";
        ingB.category = ItemCategory::Ingredient;
        ingB.ingredientEffects = {AlchemyEffect::ResistFrost, AlchemyEffect::RestoreMagicka,
                                  AlchemyEffect::None, AlchemyEffect::None};
        const uint32_t idB = inv.AddItem(ingB, 2u);

        const bool badEquip   = !inv.EquipItem(idA) && !inv.EquipItem(9999u);
        const bool badTemper  = !inv.TemperItem(idA, 100.0f, true).succeeded;
        const bool dupIngFail = !inv.BrewPotion({idA, idA}, 50.0f).succeeded;
        const bool noMatchFail = !inv.BrewPotion({idA, idB}, 50.0f).succeeded;
        const bool itemsPreserved = (inv.FindSlot(idA)->quantity == 2u && inv.FindSlot(idB)->quantity == 2u);

        check(badEquip && badTemper && dupIngFail && noMatchFail && itemsPreserved,
              "crafting_inventory_planted_failure_controls");
    }

    // =========================================================================
    // Test 2: 50-Iteration Fortify-Alchemy <-> Fortify-Enchanting Anti-Exploit Convergence Proof!
    // =========================================================================
    {
        RawCraftingInventoryEngine inv(400.0f);

        // Equip 4 apparel pieces (Head, Hands, Ring, Amulet) that will be repeatedly re-enchanted:
        ItemDef head{};   head.name = "Circlet";   head.category = ItemCategory::ArmorHead;
        ItemDef hands{};  hands.name = "Gloves";   hands.category = ItemCategory::ArmorHands;
        ItemDef ring{};   ring.name = "Gold Ring"; ring.category = ItemCategory::Ring;
        ItemDef amulet{}; amulet.name = "Amulet";  amulet.category = ItemCategory::Amulet;
        const uint32_t gearIds[4] = {
            inv.AddItem(head, 1u), inv.AddItem(hands, 1u),
            inv.AddItem(ring, 1u), inv.AddItem(amulet, 1u)
        };
        for (uint32_t gid : gearIds) inv.EquipItem(gid);

        ItemDef snowberries{};
        snowberries.name = "Snowberries";
        snowberries.category = ItemCategory::Ingredient;
        snowberries.ingredientEffects = {AlchemyEffect::FortifyEnchanting, AlchemyEffect::ResistFire,
                                         AlchemyEffect::None, AlchemyEffect::None};
        const uint32_t idSnow = inv.AddItem(snowberries, 100u);

        ItemDef blueButterfly{};
        blueButterfly.name = "Blue Butterfly Wing";
        blueButterfly.category = ItemCategory::Ingredient;
        blueButterfly.ingredientEffects = {AlchemyEffect::FortifyEnchanting, AlchemyEffect::DamageHealth,
                                           AlchemyEffect::None, AlchemyEffect::None};
        const uint32_t idWing = inv.AddItem(blueButterfly, 100u);

        float prevPotionMag = 0.0f;
        float lastPotionMag = 0.0f;
        float lastGearAlch  = 0.0f;
        float iter50Delta   = 999.0f;
        bool  allFinite     = true;

        for (int iter = 0; iter < 50; ++iter) {
            // Step A: Brew Fortify Enchanting potion while wearing current Fortify Alchemy gear
            const AlchemyBrewResult brew = inv.BrewPotion({idSnow, idWing}, 100.0f, 0.0f);
            if (!brew.succeeded || brew.matchedEffects.empty()) {
                allFinite = false;
                break;
            }
            const float potMag = brew.matchedEffects.front().magnitude;
            iter50Delta   = std::abs(potMag - prevPotionMag);
            prevPotionMag = potMag;
            lastPotionMag = potMag;

            // Step B: Drink potion and re-enchant all 4 apparel pieces with Fortify Alchemy (Grand Soul Gems!)
            for (uint32_t gid : gearIds) {
                inv.EnchantItem(gid, SoulGemTier::Grand, AlchemyEffect::FortifyAlchemy, 100.0f, 1.0f, potMag);
            }
            lastGearAlch = inv.ComputeEquippedCombatStats().fortifyAlchemyPct;
            if (!std::isfinite(potMag) || !std::isfinite(lastGearAlch)) {
                allFinite = false;
                break;
            }
        }

        std::printf(
            "  [50-Iter Anti-Exploit Loop] finalPotionFortEnch=%.2f%% (<%.0f%%) finalGearFortAlch=%.2f%% (<%.0f%%) delta50=%.2e\n",
            lastPotionMag, kMaxFortifyEnchantPotionPct, lastGearAlch, kMaxFortifyAlchemyTotalPct, iter50Delta);

        check(allFinite &&
                  lastPotionMag > 15.0f && lastPotionMag < kMaxFortifyEnchantPotionPct &&
                  lastGearAlch > 30.0f && lastGearAlch < kMaxFortifyAlchemyTotalPct &&
                  iter50Delta < 1.0e-3f,
              "crafting_inventory_50_iter_anti_exploit_convergence");
    }

    // =========================================================================
    // Test 3: Smithing Tempering Ceiling, Soul-Gem Charge/Uses Tradeoff & Encumbrance + Combat Bridge
    // =========================================================================
    {
        RawCraftingInventoryEngine inv(200.0f);

        ItemDef swordA{};
        swordA.name = "Ebony Sword A"; swordA.category = ItemCategory::Weapon;
        swordA.material = MaterialTier::Ebony; swordA.weight = 16.0f;
        swordA.baseDamage = 40.0f; swordA.baseValueGold = 720.0f;
        const uint32_t idLow = inv.AddItem(swordA, 1u);

        ItemDef swordB = swordA;
        swordB.name = "Ebony Sword B";
        const uint32_t idHigh = inv.AddItem(swordB, 1u);

        const TemperResult tLow  = inv.TemperItem(idLow,  25.0f, false, 0.0f);
        const TemperResult tHigh = inv.TemperItem(idHigh, 100.0f, true, 50.0f);

        // Weapon enchanting charge vs magnitude slider tradeoff:
        const EnchantResult eManyUses  = inv.EnchantItem(idLow,  SoulGemTier::Grand, AlchemyEffect::ShockDamage, 80.0f, 0.20f);
        const EnchantResult eHighPower = inv.EnchantItem(idHigh, SoulGemTier::Grand, AlchemyEffect::ShockDamage, 80.0f, 0.95f);

        // Equip high sword + heavy armor & verify combat actor stat transfer + encumbrance tiers:
        ItemDef plate{};
        plate.name = "Ebony Plate"; plate.category = ItemCategory::ArmorChest;
        plate.material = MaterialTier::Ebony; plate.weight = 48.0f; plate.baseArmor = 160.0f;
        const uint32_t idPlate = inv.AddItem(plate, 1u);
        inv.EquipItem(idHigh);
        inv.EquipItem(idPlate);

        const EncumbranceReport encLight = inv.EvaluateEncumbrance();
        const EquippedCombatStats cstats = inv.ComputeEquippedCombatStats();

        CombatActor hero{};
        cstats.ApplyToCombatActor(hero);

        // Add heavy iron ingots to trigger Overencumbered tier:
        ItemDef ingot{};
        ingot.name = "Iron Ingot"; ingot.category = ItemCategory::Ingot; ingot.weight = 25.0f;
        inv.AddItem(ingot, 8u); // +200 weight -> > 200 carry capacity
        const EncumbranceReport encOver = inv.EvaluateEncumbrance();

        // Merchant barter pricing coupled with RawSettlementSim + RawFactionDiplomacy:
        RawSettlementSim settle(6001u);
        RawFactionDiplomacy dip;
        dip.AdjustHoldReputation(HoldId::Whiterun, 50.0f);
        const ItemDef& highRef = inv.FindSlot(idHigh)->item;
        const float buyPrice   = EvaluateBarterPrice(highRef, true,  60.0f, HoldId::Whiterun, &settle, &dip);
        const float sellPrice  = EvaluateBarterPrice(highRef, false, 60.0f, HoldId::Whiterun, &settle, &dip);

        std::printf(
            "  [Temper+Enchant+Encumbrance+Barter] temper %.1f<%.1f uses %u>%u mag %.1f<%.1f "
            "encSpeed %.2f->%.2f buy=%.0fg sell=%.0fg\n",
            tLow.effectiveStat, tHigh.effectiveStat, eManyUses.estimatedUses, eHighPower.estimatedUses,
            eManyUses.magnitude, eHighPower.magnitude, encLight.moveSpeedMult, encOver.moveSpeedMult,
            buyPrice, sellPrice);

        check(tLow.succeeded && tHigh.succeeded && tHigh.effectiveStat > tLow.effectiveStat &&
                  tHigh.effectiveStat < 85.0f &&
                  eManyUses.succeeded && eHighPower.succeeded &&
                  eManyUses.estimatedUses > eHighPower.estimatedUses &&
                  eHighPower.magnitude > eManyUses.magnitude &&
                  encLight.tier == EncumbranceTier::Light &&
                  encOver.tier == EncumbranceTier::Overencumbered &&
                  encOver.moveSpeedMult < encLight.moveSpeedMult &&
                  hero.baseAttackDamage > 70.0f && hero.attackElement == DamageElement::Shock &&
                  sellPrice < buyPrice && sellPrice > 0.0f,
              "crafting_inventory_temper_enchant_encumbrance_and_barter");
    }

    return failures;
}

}  // namespace raw::worldgen

int run_crafting_inventory_tests() {
    return raw::worldgen::RunCraftingInventoryTests(nullptr);
}

#ifdef RAW_CRAFTING_INVENTORY_TEST_STANDALONE
int main() {
    const int failures = run_crafting_inventory_tests();
    std::printf("RawCraftingInventoryEngine standalone test summary: %s (%d failures)\n",
                failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
#endif
