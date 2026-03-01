#include "UIStateTracker.h"
#include <RE/Skyrim.h>

namespace SB::UIStateTracker
{
    UIStateData Update()
    {
        UIStateData data{};

        auto* ui = RE::UI::GetSingleton();
        if (!ui)
            return data;

        // ── Menu state ────────────────────────────────────────────────
        data.Menus.x = ui->IsShowingMenus() ? 1.f : 0.f;
        data.Menus.y = ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) ? 1.f : 0.f;
        data.Menus.z = ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME) ? 1.f : 0.f;
        data.Menus.w = ui->IsMenuOpen(RE::MapMenu::MENU_NAME) ? 1.f : 0.f;

        // ── HUD state ─────────────────────────────────────────────────
        data.HUD.x = ui->IsMenuOpen(RE::HUDMenu::MENU_NAME) ? 1.f : 0.f;

        // Crosshair visibility — tracked via HUD subtitles/crosshair
        // When menus are open, crosshair is typically hidden
        data.HUD.y = (!ui->IsShowingMenus() && data.HUD.x > 0.5f) ? 1.f : 0.f;

        // Cinematic mode: check for letterbox bars / kill cam
        auto* cam = RE::PlayerCamera::GetSingleton();
        if (cam) {
            bool isThirdPersonCinematic =
                (cam->currentState == cam->cameraStates[RE::CameraState::kTween]) ||
                (cam->currentState == cam->cameraStates[RE::CameraState::kVATS]) ||
                (cam->currentState == cam->cameraStates[RE::CameraState::kBleedout]);
            data.HUD.z = isThirdPersonCinematic ? 1.f : 0.f;
        }

        // Loading screen detection
        data.HUD.w = ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) ? 1.f : 0.f;

        // ── Detail menu state ─────────────────────────────────────────
        data.Detail.x = ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME) ? 1.f : 0.f;
        data.Detail.y = ui->IsMenuOpen(RE::BookMenu::MENU_NAME) ? 1.f : 0.f;
        data.Detail.z = ui->IsMenuOpen(RE::LockpickingMenu::MENU_NAME) ? 1.f : 0.f;
        data.Detail.w = ui->IsMenuOpen(RE::Console::MENU_NAME) ? 1.f : 0.f;

        return data;
    }
}
