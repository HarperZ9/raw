#pragma once
// RawCraftingInventoryEngine.h: Clean-Room Anti-Exploit Alchemy, Smithing Tempering, Soul-Gem
// Enchanting, Inventory/Encumbrance & Merchant Barter Engine (`raw::worldgen`).
// Pure portable C++23 with zero Windows, D3D12, or proprietary SDK dependencies.
// Eliminates Bethesda's infinite Fortify-Restoration / Alchemy-Enchanting feedback loops via
// strictly contractive hyperbolic-tangent ceilings, while coupling directly with:
//   - `RawCombatSandboxEngine` (`ComputeEquippedCombatStats` -> live weapon/armor/mitigation stats)
//   - `RawSettlementSim`       (regional scarcity price index in merchant barter)
//   - `RawFactionDiplomacy`    (hold reputation merchant discount/surcharge).
// Original work, MIT.

#include "RawCombatSandboxEngine.h"
#include "RawFactionDiplomacy.h"
#include "RawSettlementSim.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::worldgen {

inline constexpr float kMaxFortifyAlchemyTotalPct    = 65.0f;
inline constexpr float kMaxFortifySmithingTotalPct   = 65.0f;
inline constexpr float kMaxFortifyEnchantPotionPct   = 40.0f;
inline constexpr float kMaxSinglePieceFortifyPct     = 22.0f;

enum class ItemCategory : uint8_t {
    Weapon     = 0,
    Shield     = 1,
    ArmorHead  = 2,
    ArmorChest = 3,
    ArmorHands = 4,
    ArmorFeet  = 5,
    Ring       = 6,
    Amulet     = 7,
    Potion     = 8,
    SoulGem    = 9,
    Ingot      = 10,
    Ingredient = 11,
};

enum class EquipSlot : uint8_t {
    Weapon = 0,
    Shield = 1,
    Head   = 2,
    Chest  = 3,
    Hands  = 4,
    Feet   = 5,
    Ring   = 6,
    Amulet = 7,
    Count  = 8,
};

inline constexpr size_t kEquipSlotCount = static_cast<size_t>(EquipSlot::Count);

enum class MaterialTier : uint8_t {
    Iron       = 0,
    Steel      = 1,
    Dwarven    = 2,
    Orcish     = 3,
    Ebony      = 4,
    Daedric    = 5,
    Dragonbone = 6,
};

enum class SoulGemTier : uint8_t {
    Petty   = 0,
    Lesser  = 1,
    Common  = 2,
    Greater = 3,
    Grand   = 4,
};

enum class AlchemyEffect : uint8_t {
    None              = 0,
    RestoreHealth     = 1,
    RestoreStamina    = 2,
    RestoreMagicka    = 3,
    FortifySmithing   = 4,
    FortifyEnchanting = 5,
    FortifyAlchemy    = 6,
    ResistFrost       = 7,
    ResistFire        = 8,
    DamageHealth      = 9,
    Slow              = 10,
    FireDamage        = 11,
    FrostDamage       = 12,
    ShockDamage       = 13,
};

enum class EncumbranceTier : uint8_t {
    Light          = 0,
    Normal         = 1,
    Heavy          = 2,
    Overencumbered = 3,
};

struct ItemDef {
    uint32_t                       id                   = 0u;
    std::string                    name;
    ItemCategory                   category             = ItemCategory::Weapon;
    MaterialTier                   material             = MaterialTier::Steel;
    SoulGemTier                    soulTier             = SoulGemTier::Common;
    float                          weight               = 1.0f;
    float                          baseValueGold        = 50.0f;
    float                          baseDamage           = 0.0f;
    float                          baseArmor            = 0.0f;
    float                          temperBonus          = 0.0f;
    AlchemyEffect                  enchantEffect        = AlchemyEffect::None;
    float                          enchantMagnitude     = 0.0f;
    float                          enchantChargeMax     = 0.0f;
    float                          enchantChargeCurrent = 0.0f;
    float                          enchantChargePerUse  = 0.0f;
    std::array<AlchemyEffect, 4>   ingredientEffects    = {
        AlchemyEffect::None, AlchemyEffect::None, AlchemyEffect::None, AlchemyEffect::None
    };
};

struct InventorySlot {
    ItemDef  item{};
    uint32_t quantity = 1u;
};

struct EquipmentLoadout {
    std::array<uint32_t, kEquipSlotCount> equippedItemIds{};
};

struct EncumbranceReport {
    float           totalWeight   = 0.0f;
    float           carryCapacity = 300.0f;
    float           loadRatio     = 0.0f;
    EncumbranceTier tier          = EncumbranceTier::Light;
    float           moveSpeedMult = 1.0f;
};

struct TemperResult {
    bool  succeeded        = false;
    float oldTemperBonus   = 0.0f;
    float newTemperBonus   = 0.0f;
    float effectiveStat    = 0.0f;
};

struct BrewedEffect {
    AlchemyEffect effect    = AlchemyEffect::None;
    float         magnitude = 0.0f;
    float         durationS = 0.0f;
};

struct AlchemyBrewResult {
    bool                      succeeded     = false;
    uint32_t                  potionItemId  = 0u;
    std::vector<BrewedEffect> matchedEffects;
    float                     potionValueG  = 0.0f;
};

struct EnchantResult {
    bool     succeeded      = false;
    float    magnitude      = 0.0f;
    float    maxCharge      = 0.0f;
    float    chargePerUse   = 0.0f;
    uint32_t estimatedUses  = 0u;
};

struct EquippedCombatStats {
    float         weaponDamage         = 8.0f;
    float         totalArmorRating     = 0.0f;
    float         physicalMitigation01 = 0.0f;
    float         elementalBonusDamage = 0.0f;
    DamageElement weaponElement        = DamageElement::Physical;
    float         moveSpeedMult        = 1.0f;
    float         fortifyAlchemyPct    = 0.0f;
    float         fortifySmithingPct   = 0.0f;
    float         fortifyEnchantingPct = 0.0f;

    void ApplyToCombatActor(CombatActor& actor) const;
};

// Pure anti-exploit crafting & barter physics helpers:
float CompressCraftingFortifyPct(float rawPct, float asymptoticCapPct) noexcept;
float GetSoulGemChargeCapacity(SoulGemTier tier) noexcept;
float EvaluateBarterPrice(const ItemDef& item, bool playerBuying, float speechSkill,
                          HoldId hold,
                          const RawSettlementSim* settlementOpt = nullptr,
                          const RawFactionDiplomacy* diplomacyOpt = nullptr) noexcept;

class RawCraftingInventoryEngine {
public:
    explicit RawCraftingInventoryEngine(float carryCapacity = 300.0f);

    void Clear(float carryCapacity = 300.0f);

    uint32_t             AddItem(ItemDef def, uint32_t quantity = 1u);
    bool                 RemoveItem(uint32_t itemId, uint32_t quantity = 1u);
    InventorySlot*       FindSlot(uint32_t itemId);
    const InventorySlot* FindSlot(uint32_t itemId) const;

    bool EquipItem(uint32_t itemId);
    bool UnequipSlot(EquipSlot slot);

    EncumbranceReport   EvaluateEncumbrance() const;
    EquippedCombatStats ComputeEquippedCombatStats() const;

    // Anti-exploit Smithing tempering (hyperbolic-tangent ceiling + material perk scaling):
    TemperResult TemperItem(uint32_t itemId, float smithingSkill, bool hasMaterialPerk,
                            float activeFortifySmithingPct = 0.0f);

    // 4-Effect Alchemy brewing (exact effect matching across 2 or 3 distinct ingredients, bounded cap):
    AlchemyBrewResult BrewPotion(const std::vector<uint32_t>& ingredientItemIds,
                                 float alchemySkill,
                                 float activeFortifyAlchemyPct = 0.0f);

    // Soul-Gem Enchanting (magnitude vs uses slider, strictly bounded anti-loop Fortify ceiling):
    EnchantResult EnchantItem(uint32_t targetItemId, SoulGemTier soulTier,
                              AlchemyEffect effect, float enchantSkill,
                              float magnitudeSlider01 = 0.5f,
                              float activeFortifyEnchantPct = 0.0f);

    const std::vector<InventorySlot>& Slots() const noexcept { return slots_; }
    const EquipmentLoadout&           Loadout() const noexcept { return loadout_; }

private:
    float                      carryCapacity_ = 300.0f;
    std::vector<InventorySlot> slots_;
    EquipmentLoadout           loadout_{};
    uint32_t                   nextItemId_    = 1u;
};

}  // namespace raw::worldgen
