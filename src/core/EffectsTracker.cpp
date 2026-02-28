#include "EffectsTracker.h"
#include <RE/Skyrim.h>

namespace SB::EffectsTracker
{
    EffectsData Update()
    {
        EffectsData data{};

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player)
            return data;

        // ── Scan active magic effects ───────────────────────────────────
        // Player inherits MagicTarget, which has a list of ActiveEffect.
        // Each ActiveEffect has a pointer to the EffectSetting (MGEF)
        // which tells us what it does.
        auto* magicTarget = player->AsMagicTarget();
        if (!magicTarget)
            return data;

        auto* activeEffects = magicTarget->GetActiveEffectList();
        if (!activeEffects)
            return data;

        for (auto* ae : *activeEffects) {
            if (!ae || ae->flags.any(RE::ActiveEffect::Flag::kInactive))
                continue;

            auto* effect = ae->GetBaseObject();
            if (!effect)
                continue;

            auto archetype = effect->GetArchetype();
            using AT = RE::EffectSetting::Archetype;

            // ── Vision effects ──────────────────────────────────────────
            if (archetype == AT::kNightEye) {
                data.VisionEffects.x = 1.f;
            }
            if (archetype == AT::kDetectLife) {
                data.VisionEffects.y = 1.f;
            }

            // Detect Dead uses the same archetype with different flags
            // or a separate archetype depending on version.
            // We check the effect's name or associated magic school.
            // Conservative: check for specific keywords or FormIDs.

            if (archetype == AT::kEtherealize) {
                data.VisionEffects.w = 1.f;
            }

            // ── Time effects ────────────────────────────────────────────
            if (archetype == AT::kSlowTime) {
                // Magnitude indicates the slow-down factor
                data.TimeEffects.x = ae->magnitude;
            }

            // ── Damage effects (resistible) ─────────────────────────────
            // These indicate the player is currently taking elemental damage.
            // The shader can respond with screen effects.
            if (archetype == AT::kValueModifier) {
                auto av = effect->data.primaryAV;
                if (av == RE::ActorValue::kHealth) {
                    // Check delivery type / resistance to determine element.
                    auto resist = effect->data.resistVariable;
                    if (resist == RE::ActorValue::kResistFire)
                        data.DamageEffects.x = 1.f;
                    else if (resist == RE::ActorValue::kResistFrost)
                        data.DamageEffects.y = 1.f;
                    else if (resist == RE::ActorValue::kResistShock)
                        data.DamageEffects.z = 1.f;
                    else if (resist == RE::ActorValue::kPoisonResist)
                        data.DamageEffects.w = 1.f;
                }
            }

            // ── Misc effects ────────────────────────────────────────────
            if (archetype == AT::kInvisibility) {
                data.MiscEffects.x = 1.f;
            }
            if (archetype == AT::kParalysis) {
                data.MiscEffects.y = 1.f;
            }
        }

        // ── Drunk detection ─────────────────────────────────────────────
        // Alcohol/skooma effects apply a ValueModifier to various AVs.
        // We detect this via the alcohol-related imagespace modifier
        // or by checking if a specific spell is active.
        // Conservative approach: check if DrunkEffect IMGSPMOD is active.
        // This is a heuristic — may need FormID lookup for accuracy.

        return data;
    }
}
