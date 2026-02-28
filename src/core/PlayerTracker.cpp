#include "PlayerTracker.h"
#include <RE/Skyrim.h>
#include <cmath>

namespace SB::PlayerTracker
{
    // Persistent: previous position for speed calculation
    static RE::NiPoint3 s_prevPos{};
    static bool         s_hasPrevPos = false;

    PlayerData Update()
    {
        PlayerData data{};

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player)
            return data;

        // Cast to Actor for access to ActorValueOwner and ActorState methods
        RE::Actor* actor = static_cast<RE::Actor*>(player);

        // ── Position ────────────────────────────────────────────────────
        auto pos = player->GetPosition();
        data.Position.x = pos.x;
        data.Position.y = pos.y;
        data.Position.z = pos.z;

        // Altitude above water/sea level
        float waterZ = 0.f;
        if (auto* cell = player->GetParentCell()) {
            waterZ = cell->GetExteriorWaterHeight();
        }
        data.Position.w = pos.z - waterZ;

        // ── Vitals ──────────────────────────────────────────────────────
        using AV = RE::ActorValue;

        // Access actor values through ActorValueOwner interface
        auto* avOwner = actor->AsActorValueOwner();
        if (avOwner) {
            float maxHP = avOwner->GetPermanentActorValue(AV::kHealth);
            float maxSP = avOwner->GetPermanentActorValue(AV::kStamina);
            float maxMP = avOwner->GetPermanentActorValue(AV::kMagicka);

            data.Vitals.x = (maxHP > 0.f) ? avOwner->GetActorValue(AV::kHealth)  / maxHP : 0.f;
            data.Vitals.y = (maxSP > 0.f) ? avOwner->GetActorValue(AV::kStamina) / maxSP : 0.f;
            data.Vitals.z = (maxMP > 0.f) ? avOwner->GetActorValue(AV::kMagicka) / maxMP : 0.f;
        }
        data.Vitals.w = static_cast<float>(player->GetLevel());

        // ── Movement ────────────────────────────────────────────────────
        // Speed: distance from previous frame position.
        // Note: this is raw displacement per frame. The shader should
        // divide by deltaTime (SB_Render_Frame.y) for units/second.
        if (s_hasPrevPos) {
            float dx = pos.x - s_prevPos.x;
            float dy = pos.y - s_prevPos.y;
            float dz = pos.z - s_prevPos.z;
            data.Movement.x = std::sqrt(dx*dx + dy*dy + dz*dz);
        }
        s_prevPos = pos;
        s_hasPrevPos = true;

        // Access state flags through ActorState interface
        auto* actorState = actor->AsActorState();
        if (actorState) {
            data.Movement.y = actorState->IsSprinting() ? 1.f : 0.f;
            data.Movement.z = actorState->IsSwimming()  ? 1.f : 0.f;
        }

        // Mounted check
        bool isRiding = player->IsOnMount();
        data.Movement.w = isRiding ? 1.f : 0.f;

        // ── Combat ──────────────────────────────────────────────────────
        data.Combat.x = actor->IsInCombat()  ? 1.f : 0.f;
        if (actorState) {
            data.Combat.y = actorState->IsBleedingOut() ? 1.f : 0.f;
            data.Combat.w = actorState->IsWeaponDrawn() ? 1.f : 0.f;
        }
        // Kill move detection: check camera state
        auto* cam = RE::PlayerCamera::GetSingleton();
        if (cam) {
            bool isKillCam = (cam->currentState ==
                cam->cameraStates[RE::CameraState::kVATS]);
            data.Combat.z = isKillCam ? 1.f : 0.f;
        }

        // ── Water ───────────────────────────────────────────────────────
        // Player's head Z vs water surface Z determines submersion.
        float headZ = pos.z + 128.0f;  // approximate head offset
        bool underwater = (headZ < waterZ && waterZ > -1e10f);

        data.Water.x = underwater ? 1.f : 0.f;
        data.Water.y = waterZ;
        data.Water.z = underwater ? (waterZ - headZ) : 0.f;  // submersion depth

        // Wading: feet in water but head above
        bool wading = (!underwater && pos.z < waterZ && waterZ > -1e10f);
        data.Water.w = wading ? 1.f : 0.f;

        return data;
    }
}
