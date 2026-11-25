// RawCraftingInventoryEngine.cpp: Clean-Room Anti-Exploit Alchemy, Smithing Tempering, Soul-Gem
// Enchanting, Inventory/Encumbrance & Merchant Barter Engine.
// Original work, MIT. Zero Windows / D3D12 / proprietary SDK dependencies.

#include "RawCraftingInventoryEngine.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace raw::worldgen {
namespace {

inline float ClampF(float v, float lo, float hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

inline float Clamp01F(float v) noexcept {
    return ClampF(v, 0.0f, 1.0f);
}

inline float SafeF(float v, float fallback = 0.0f) noexcept {
    return std::isfinite(v) ? v : fallback;
}

bool CategoryToEquipSlot(ItemCategory cat, EquipSlot& outSlot) noexcept {
    switch (cat) {
        case ItemCategory::Weapon:     outSlot = EquipSlot::Weapon; return true;
        case ItemCategory::Shield:     outSlot = EquipSlot::Shield; return true;
        case ItemCategory::ArmorHead:  outSlot = EquipSlot::Head;   return true;
        case ItemCategory::ArmorChest: outSlot = EquipSlot::Chest;  return true;
        case ItemCategory::ArmorHands: outSlot = EquipSlot::Hands;  return true;
        case ItemCategory::ArmorFeet:  outSlot = EquipSlot::Feet;   return true;
        case ItemCategory::Ring:       outSlot = EquipSlot::Ring;   return true;
        case ItemCategory::Amulet:     outSlot = EquipSlot::Amulet; return true;
        default: return false;
    }
}

float MaterialTierMultiplier(MaterialTier tier) noexcept {
    switch (tier) {
        case MaterialTier::Iron:       return 0.90f;
        case MaterialTier::Steel:      return 1.00f;
        case MaterialTier::Dwarven:    return 1.12f;
        case MaterialTier::Orcish:     return 1.22f;
        case MaterialTier::Ebony:      return 1.38f;
        case MaterialTier::Daedric:    return 1.50f;
        case MaterialTier::Dragonbone: return 1.58f;
        default:                       return 1.00f;
    }
}

bool IsMetaCraftingFortify(AlchemyEffect eff) noexcept {
    return eff == AlchemyEffect::FortifyAlchemy ||
           eff == AlchemyEffect::FortifySmithing ||
           eff == AlchemyEffect::FortifyEnchanting;
}

}  // namespace

// ============================================================================
// Pure Anti-Exploit Crafting & Barter Helpers
// ============================================================================

float CompressCraftingFortifyPct(float rawPct, float asymptoticCapPct) noexcept {
    const float r   = std::max(0.0f, SafeF(rawPct, 0.0f));
    const float cap = std::max(1.0f, SafeF(asymptoticCapPct, 40.0f));
    return cap * std::tanh(r / cap);
}

float GetSoulGemChargeCapacity(SoulGemTier tier) noexcept {
    switch (tier) {
        case SoulGemTier::Petty:   return 250.0f;
        case SoulGemTier::Lesser:  return 500.0f;
        case SoulGemTier::Common:  return 1000.0f;
        case SoulGemTier::Greater: return 2000.0f;
        case SoulGemTier::Grand:   return 3000.0f;
        default:                   return 500.0f;
    }
}

float EvaluateBarterPrice(const ItemDef& item, bool playerBuying, float speechSkill,
                          HoldId hold,
                          const RawSettlementSim* settlementOpt,
                          const RawFactionDiplomacy* diplomacyOpt) noexcept {
    const float baseVal = std::max(1.0f, SafeF(item.baseValueGold, 10.0f) +
                                   item.temperBonus * 8.0f + item.enchantMagnitude * 6.0f);
    const float speech  = ClampF(SafeF(speechSkill, 15.0f), 0.0f, 100.0f);

    // Regional scarcity factor from RawSettlementSim (defaults to 1.0):
    float scarcityFactor = 1.0f;
    if (settlementOpt != nullptr) {
        if (const HoldState* hs = settlementOpt->GetHold(hold)) {
            const float grainRatio = hs->price[0] / std::max(1.0f, GetCommodityBasePrice(CommodityKind::Grain));
            const float ironRatio  = hs->price[2] / std::max(1.0f, GetCommodityBasePrice(CommodityKind::IronOre));
            scarcityFactor = ClampF(0.5f * (grainRatio + ironRatio), 0.75f, 1.85f);
        }
    }

    // Hold reputation merchant multiplier from RawFactionDiplomacy (0.82..1.35):
    float dipMult = 1.0f;
    if (diplomacyOpt != nullptr) {
        if (const HoldStanding* st = diplomacyOpt->GetHoldStanding(hold)) {
            dipMult = ClampF(st->merchantPriceMult, 0.82f, 1.35f);
        }
    }

    // Grounded anti-exploit barter spread: sellPrice is ALWAYS strictly < buyPrice!
    if (playerBuying) {
        const float speechDisc = 1.0f - 0.30f * (speech / 100.0f); // 1.00 -> 0.70
        const float buyMult    = std::max(1.08f, 1.85f * speechDisc * dipMult);
        return std::round(baseVal * scarcityFactor * buyMult);
    } else {
        const float speechBoost = 0.42f + 0.36f * (speech / 100.0f); // 0.42 -> 0.78
        const float repBonus    = 1.0f / std::max(0.82f, dipMult);
        const float sellMult    = ClampF(speechBoost * repBonus, 0.25f, 0.88f);
        return std::max(1.0f, std::round(baseVal * scarcityFactor * sellMult));
    }
}

void EquippedCombatStats::ApplyToCombatActor(CombatActor& actor) const {
    actor.baseAttackDamage = std::max(5.0f, weaponDamage + elementalBonusDamage);
    actor.armorRating      = std::max(0.0f, totalArmorRating);
    actor.attackElement    = weaponElement;
    actor.moveSpeedU       = std::max(60.0f, 250.0f * moveSpeedMult);
}

// ============================================================================
// RawCraftingInventoryEngine Implementation
// ============================================================================

RawCraftingInventoryEngine::RawCraftingInventoryEngine(float carryCapacity) {
    Clear(carryCapacity);
}

void RawCraftingInventoryEngine::Clear(float carryCapacity) {
    carryCapacity_ = std::max(50.0f, SafeF(carryCapacity, 300.0f));
    slots_.clear();
    loadout_.equippedItemIds.fill(0u);
    nextItemId_ = 1u;
}

uint32_t RawCraftingInventoryEngine::AddItem(ItemDef def, uint32_t quantity) {
    if (quantity == 0u) return 0u;
    def.id            = nextItemId_++;
    def.weight        = std::max(0.0f, SafeF(def.weight, 0.0f));
    def.baseValueGold = std::max(0.0f, SafeF(def.baseValueGold, 0.0f));
    def.baseDamage    = std::max(0.0f, SafeF(def.baseDamage, 0.0f));
    def.baseArmor     = std::max(0.0f, SafeF(def.baseArmor, 0.0f));
    slots_.push_back({std::move(def), quantity});
    return slots_.back().item.id;
}

bool RawCraftingInventoryEngine::RemoveItem(uint32_t itemId, uint32_t quantity) {
    if (itemId == 0u || quantity == 0u) return false;
    for (auto it = slots_.begin(); it != slots_.end(); ++it) {
        if (it->item.id == itemId) {
            if (it->quantity < quantity) return false;
            it->quantity -= quantity;
            if (it->quantity == 0u) {
                for (uint32_t& eqId : loadout_.equippedItemIds) {
                    if (eqId == itemId) eqId = 0u;
                }
                slots_.erase(it);
            }
            return true;
        }
    }
    return false;
}

InventorySlot* RawCraftingInventoryEngine::FindSlot(uint32_t itemId) {
    for (InventorySlot& s : slots_) {
        if (s.item.id == itemId) return &s;
    }
    return nullptr;
}

const InventorySlot* RawCraftingInventoryEngine::FindSlot(uint32_t itemId) const {
    for (const InventorySlot& s : slots_) {
        if (s.item.id == itemId) return &s;
    }
    return nullptr;
}

bool RawCraftingInventoryEngine::EquipItem(uint32_t itemId) {
    const InventorySlot* s = FindSlot(itemId);
    if (!s || s->quantity == 0u) return false;
    EquipSlot slot{};
    if (!CategoryToEquipSlot(s->item.category, slot)) {
        return false; // Potions/ingots/ingredients cannot be equipped into gear slots
    }
    loadout_.equippedItemIds[static_cast<size_t>(slot)] = itemId;
    return true;
}

bool RawCraftingInventoryEngine::UnequipSlot(EquipSlot slot) {
    const size_t idx = static_cast<size_t>(slot);
    if (idx >= kEquipSlotCount) return false;
    loadout_.equippedItemIds[idx] = 0u;
    return true;
}

EncumbranceReport RawCraftingInventoryEngine::EvaluateEncumbrance() const {
    EncumbranceReport r{};
    r.carryCapacity = carryCapacity_;
    for (const InventorySlot& s : slots_) {
        r.totalWeight += s.item.weight * static_cast<float>(s.quantity);
    }
    r.loadRatio = r.totalWeight / std::max(1.0f, r.carryCapacity);
    if (r.loadRatio > 1.00f) {
        r.tier          = EncumbranceTier::Overencumbered;
        r.moveSpeedMult = 0.42f;
    } else if (r.loadRatio >= 0.85f) {
        r.tier          = EncumbranceTier::Heavy;
        r.moveSpeedMult = 0.84f;
    } else if (r.loadRatio >= 0.50f) {
        r.tier          = EncumbranceTier::Normal;
        r.moveSpeedMult = 0.94f;
    } else {
        r.tier          = EncumbranceTier::Light;
        r.moveSpeedMult = 1.00f;
    }
    return r;
}

EquippedCombatStats RawCraftingInventoryEngine::ComputeEquippedCombatStats() const {
    EquippedCombatStats st{};
    const EncumbranceReport enc = EvaluateEncumbrance();
    st.moveSpeedMult = enc.moveSpeedMult;

    float rawFortifyAlch   = 0.0f;
    float rawFortifySmith  = 0.0f;
    float rawFortifyEnch   = 0.0f;

    for (size_t i = 0u; i < kEquipSlotCount; ++i) {
        const uint32_t eqId = loadout_.equippedItemIds[i];
        if (eqId == 0u) continue;
        const InventorySlot* slot = FindSlot(eqId);
        if (!slot) continue;
        const ItemDef& it = slot->item;

        if (it.category == ItemCategory::Weapon) {
            st.weaponDamage = it.baseDamage + it.temperBonus;
            if (it.enchantEffect == AlchemyEffect::FireDamage) {
                st.weaponElement        = DamageElement::Fire;
                st.elementalBonusDamage = it.enchantMagnitude;
            } else if (it.enchantEffect == AlchemyEffect::FrostDamage) {
                st.weaponElement        = DamageElement::Frost;
                st.elementalBonusDamage = it.enchantMagnitude;
            } else if (it.enchantEffect == AlchemyEffect::ShockDamage) {
                st.weaponElement        = DamageElement::Shock;
                st.elementalBonusDamage = it.enchantMagnitude;
            }
        } else {
            st.totalArmorRating += it.baseArmor + it.temperBonus;
            if (it.enchantEffect == AlchemyEffect::FortifyAlchemy) {
                rawFortifyAlch += it.enchantMagnitude;
            } else if (it.enchantEffect == AlchemyEffect::FortifySmithing) {
                rawFortifySmith += it.enchantMagnitude;
            } else if (it.enchantEffect == AlchemyEffect::FortifyEnchanting) {
                rawFortifyEnch += it.enchantMagnitude;
            }
        }
    }

    st.physicalMitigation01 = EvaluateArmorMitigation(st.totalArmorRating);
    st.fortifyAlchemyPct    = CompressCraftingFortifyPct(rawFortifyAlch,  kMaxFortifyAlchemyTotalPct);
    st.fortifySmithingPct   = CompressCraftingFortifyPct(rawFortifySmith, kMaxFortifySmithingTotalPct);
    st.fortifyEnchantingPct = CompressCraftingFortifyPct(rawFortifyEnch,  kMaxFortifyEnchantPotionPct);
    return st;
}

// ============================================================================
// Anti-Exploit Smithing Tempering
// ============================================================================

TemperResult RawCraftingInventoryEngine::TemperItem(uint32_t itemId, float smithingSkill,
                                                    bool hasMaterialPerk,
                                                    float activeFortifySmithingPct) {
    TemperResult res{};
    InventorySlot* slot = FindSlot(itemId);
    if (!slot) return res;

    ItemDef& it = slot->item;
    const bool isWeapon = (it.category == ItemCategory::Weapon);
    const bool isArmor  = (it.category == ItemCategory::Shield ||
                           it.category == ItemCategory::ArmorHead ||
                           it.category == ItemCategory::ArmorChest ||
                           it.category == ItemCategory::ArmorHands ||
                           it.category == ItemCategory::ArmorFeet);
    if (!isWeapon && !isArmor) {
        return res;
    }

    const float baseStat = isWeapon ? it.baseDamage : it.baseArmor;
    if (baseStat <= 0.0f) return res;

    const float skill    = ClampF(SafeF(smithingSkill, 15.0f), 0.0f, 100.0f);
    const float gearFort = ComputeEquippedCombatStats().fortifySmithingPct;
    const float totalFort = CompressCraftingFortifyPct(gearFort + std::max(0.0f, SafeF(activeFortifySmithingPct)),
                                                       kMaxFortifySmithingTotalPct);

    const float effSkill = skill * (1.0f + totalFort / 100.0f);
    const float perkMult = hasMaterialPerk ? 1.35f : 1.00f;
    const float matScale = MaterialTierMultiplier(it.material);

    // Smooth hyperbolic-tangent ceiling strictly below +95% of base stat * material scale:
    const float maxTemperCeiling = baseStat * (0.78f + 0.14f * Clamp01F((matScale - 0.9f) / 0.7f));
    const float newBonus = maxTemperCeiling * std::tanh((effSkill * perkMult) / 115.0f);

    res.succeeded      = true;
    res.oldTemperBonus = it.temperBonus;
    it.temperBonus     = std::max(it.temperBonus, newBonus);
    res.newTemperBonus = it.temperBonus;
    res.effectiveStat  = baseStat + it.temperBonus;
    return res;
}

// ============================================================================
// 4-Effect Alchemy Brewing (Exact Matching + Bounded Meta-Buff Cap)
// ============================================================================

AlchemyBrewResult RawCraftingInventoryEngine::BrewPotion(const std::vector<uint32_t>& ingredientItemIds,
                                                         float alchemySkill,
                                                         float activeFortifyAlchemyPct) {
    AlchemyBrewResult res{};
    if (ingredientItemIds.size() < 2u || ingredientItemIds.size() > 3u) {
        return res;
    }

    // Verify all IDs exist, are distinct Ingredient items, and have quantity >= 1:
    std::unordered_set<uint32_t> seenIds;
    std::vector<InventorySlot*> ingSlots;
    ingSlots.reserve(ingredientItemIds.size());
    for (uint32_t id : ingredientItemIds) {
        if (!seenIds.insert(id).second) return res; // Duplicate ingredient rejected
        InventorySlot* s = FindSlot(id);
        if (!s || s->quantity == 0u || s->item.category != ItemCategory::Ingredient) {
            return res;
        }
        ingSlots.push_back(s);
    }

    // Count how many distinct ingredients share each AlchemyEffect:
    std::array<uint32_t, 16> effectCounts{};
    for (const InventorySlot* s : ingSlots) {
        std::unordered_set<uint8_t> perIngUnique;
        for (AlchemyEffect eff : s->item.ingredientEffects) {
            const uint8_t code = static_cast<uint8_t>(eff);
            if (eff != AlchemyEffect::None && code < effectCounts.size() && perIngUnique.insert(code).second) {
                ++effectCounts[code];
            }
        }
    }

    const float skill     = ClampF(SafeF(alchemySkill, 15.0f), 0.0f, 100.0f);
    const float gearAlch  = ComputeEquippedCombatStats().fortifyAlchemyPct;
    const float totalAlch = CompressCraftingFortifyPct(gearAlch + std::max(0.0f, SafeF(activeFortifyAlchemyPct)),
                                                       kMaxFortifyAlchemyTotalPct);
    const float skillMult = (1.0f + 0.015f * skill) * (1.0f + totalAlch / 100.0f);

    for (size_t code = 1u; code < effectCounts.size(); ++code) {
        if (effectCounts[code] < 2u) continue;
        const AlchemyEffect eff = static_cast<AlchemyEffect>(code);
        BrewedEffect be{};
        be.effect = eff;
        if (eff == AlchemyEffect::FortifyEnchanting) {
            // Strictly bounded by kMaxFortifyEnchantPotionPct (40%) -> impossible to diverge!
            const float raw = 10.0f * skillMult;
            be.magnitude    = CompressCraftingFortifyPct(raw, kMaxFortifyEnchantPotionPct);
            be.durationS    = 30.0f;
        } else if (eff == AlchemyEffect::FortifySmithing) {
            const float raw = 18.0f * skillMult;
            be.magnitude    = CompressCraftingFortifyPct(raw, kMaxFortifySmithingTotalPct);
            be.durationS    = 30.0f;
        } else if (eff == AlchemyEffect::FortifyAlchemy) {
            // Fortify Alchemy potions explicitly blocked from self-recursion via strict 20% cap:
            be.magnitude    = CompressCraftingFortifyPct(8.0f * skillMult, 20.0f);
            be.durationS    = 30.0f;
        } else {
            be.magnitude    = std::min(220.0f, 25.0f * skillMult);
            be.durationS    = (eff == AlchemyEffect::RestoreHealth ||
                               eff == AlchemyEffect::RestoreStamina ||
                               eff == AlchemyEffect::RestoreMagicka) ? 0.0f : 60.0f;
        }
        res.matchedEffects.push_back(be);
    }

    if (res.matchedEffects.empty()) {
        return res; // Incompatible ingredients fail cleanly without consuming items
    }

    // Consume 1 unit of each ingredient and add brewed potion to inventory:
    for (uint32_t id : ingredientItemIds) {
        RemoveItem(id, 1u);
    }

    ItemDef potion{};
    potion.name             = "Brewed Potion";
    potion.category         = ItemCategory::Potion;
    potion.weight           = 0.5f;
    potion.enchantEffect    = res.matchedEffects.front().effect;
    potion.enchantMagnitude = res.matchedEffects.front().magnitude;
    float totalVal = 0.0f;
    for (const BrewedEffect& be : res.matchedEffects) {
        totalVal += be.magnitude * 2.5f + 15.0f;
    }
    potion.baseValueGold = std::round(totalVal);
    res.potionValueG     = potion.baseValueGold;
    res.potionItemId     = AddItem(std::move(potion), 1u);
    res.succeeded        = (res.potionItemId != 0u);
    return res;
}

// ============================================================================
// Anti-Exploit Soul-Gem Enchanting
// ============================================================================

EnchantResult RawCraftingInventoryEngine::EnchantItem(uint32_t targetItemId, SoulGemTier soulTier,
                                                      AlchemyEffect effect, float enchantSkill,
                                                      float magnitudeSlider01,
                                                      float activeFortifyEnchantPct) {
    EnchantResult res{};
    if (effect == AlchemyEffect::None) return res;
    InventorySlot* slot = FindSlot(targetItemId);
    if (!slot) return res;

    EquipSlot eqSlot{};
    if (!CategoryToEquipSlot(slot->item.category, eqSlot)) {
        return res; // Only equippable weapons/armor/jewelry can be enchanted
    }

    const float skill    = ClampF(SafeF(enchantSkill, 15.0f), 0.0f, 100.0f);
    const float slider   = ClampF(SafeF(magnitudeSlider01, 0.5f), 0.15f, 1.0f);
    const float fortEnch = CompressCraftingFortifyPct(std::max(0.0f, SafeF(activeFortifyEnchantPct)),
                                                      kMaxFortifyEnchantPotionPct);
    const float maxCharge = GetSoulGemChargeCapacity(soulTier);
    const float soulScale = ClampF(maxCharge / 3000.0f, 0.15f, 1.0f);
    const float skillMult = (0.75f + 0.0125f * skill) * (1.0f + fortEnch / 100.0f);

    ItemDef& it = slot->item;
    it.enchantEffect = effect;

    if (it.category == ItemCategory::Weapon) {
        const float rawMag     = 32.0f * soulScale * (0.45f + 0.55f * slider) * skillMult;
        const float chargeUse  = Lerp(16.0f, 110.0f, slider);
        it.enchantMagnitude     = std::min(75.0f, rawMag);
        it.enchantChargeMax     = maxCharge;
        it.enchantChargeCurrent = maxCharge;
        it.enchantChargePerUse  = chargeUse;
        res.maxCharge           = maxCharge;
        res.chargePerUse        = chargeUse;
        res.estimatedUses       = static_cast<uint32_t>(std::floor(maxCharge / std::max(1.0f, chargeUse)));
    } else if (IsMetaCraftingFortify(effect)) {
        // Strictly bounded single-piece Fortify ceiling (< 22% per piece) -> mathematically kills loop exploit!
        const float rawPiece   = 11.5f * soulScale * skillMult;
        it.enchantMagnitude    = CompressCraftingFortifyPct(rawPiece, kMaxSinglePieceFortifyPct);
        it.enchantChargeMax    = 0.0f;
        it.enchantChargePerUse = 0.0f;
    } else {
        it.enchantMagnitude    = std::min(60.0f, 24.0f * soulScale * skillMult);
    }

    res.magnitude = it.enchantMagnitude;
    res.succeeded = true;
    return res;
}

}  // namespace raw::worldgen
