// RawInputEngine.cpp: Unified Keyboard, Mouse, and XInput Gamepad Controller Engine (Zero SKSE).
// Original work, MIT.
#include "RawInputEngine.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace raw::ui {
namespace {

// Minimal XInput ABI structs (avoids requiring xinput.lib link dependency).
struct XInputGamepadAbi {
    uint16_t wButtons;
    uint8_t  bLeftTrigger;
    uint8_t  bRightTrigger;
    int16_t  sThumbLX;
    int16_t  sThumbLY;
    int16_t  sThumbRX;
    int16_t  sThumbRY;
};

struct XInputStateAbi {
    uint32_t         dwPacketNumber;
    XInputGamepadAbi Gamepad;
};

using XInputGetStateFn = uint32_t(WINAPI*)(uint32_t, XInputStateAbi*);

constexpr uint16_t kPadDpadUp     = 0x0001u;
constexpr uint16_t kPadDpadDown   = 0x0002u;
constexpr uint16_t kPadDpadLeft   = 0x0004u;
constexpr uint16_t kPadDpadRight  = 0x0008u;
constexpr uint16_t kPadStart      = 0x0010u;
constexpr uint16_t kPadBack       = 0x0020u;
constexpr uint16_t kPadRThumb     = 0x0080u;
constexpr uint16_t kPadLB         = 0x0100u;
constexpr uint16_t kPadRB         = 0x0200u;
constexpr uint16_t kPadA          = 0x1000u;
constexpr uint16_t kPadB          = 0x2000u;
constexpr uint16_t kPadX          = 0x4000u;
constexpr uint16_t kPadY          = 0x8000u;

inline bool KeyDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

}  // namespace

void FilterAnalogStick(int16_t rawX, int16_t rawY, float deadzone, float& outX, float& outY) {
    const float fx = std::clamp(float(rawX) / 32767.0f, -1.0f, 1.0f);
    const float fy = std::clamp(float(rawY) / 32767.0f, -1.0f, 1.0f);
    const float mag = std::sqrt(fx * fx + fy * fy);
    const float dz = std::clamp(deadzone, 0.01f, 0.85f);
    if (mag <= dz) {
        outX = 0.0f;
        outY = 0.0f;
        return;
    }
    const float norm = std::clamp((mag - dz) / (1.0f - dz), 0.0f, 1.0f);
    // Smooth quadratic-blended curve gives micro-precision near center and full velocity at rim.
    const float shaped = norm * (0.45f + 0.55f * norm);
    const float scale = shaped / mag;
    outX = fx * scale;
    outY = fy * scale;
}

bool RawInputEngine::RepeatKey::Update(bool down, float dtSec, bool allowRepeat) {
    if (!down) {
        prev = false;
        heldSec = 0.0f;
        nextFireSec = 0.0f;
        return false;
    }
    if (!prev) {
        prev = true;
        heldSec = 0.0f;
        nextFireSec = 0.34f;  // initial repeat delay
        return true;
    }
    if (!allowRepeat) return false;
    heldSec += std::clamp(dtSec, 0.0f, 0.1f);
    if (heldSec >= nextFireSec) {
        nextFireSec = heldSec + 0.055f;  // smooth 18 Hz continuous scrub
        return true;
    }
    return false;
}

RawInputEngine::RawInputEngine() = default;

void RawInputEngine::EnsureXInputLoaded() {
    if (m_xinputChecked) return;
    m_xinputChecked = true;
    static const char* kDlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
    for (const char* name : kDlls) {
        HMODULE m = LoadLibraryA(name);
        if (m) {
            if (auto fn = GetProcAddress(m, "XInputGetState")) {
                m_xinputDll = m;
                m_xinputGetState = reinterpret_cast<void*>(fn);
                return;
            }
            FreeLibrary(m);
        }
    }
}

void RawInputEngine::OnMouseWheel(int32_t deltaNotches) {
    m_wheelAccum += deltaNotches;
    m_activeDevice = InputDevice::KeyboardMouse;
}

void RawInputEngine::OnMouseMove(int32_t x, int32_t y) {
    if (x != m_mouseX || y != m_mouseY) {
        m_mouseX = x;
        m_mouseY = y;
        m_activeDevice = InputDevice::KeyboardMouse;
    }
}

void RawInputEngine::OnMouseButton(int button, bool down) {
    if (button == 0) m_mouseLeftDown = down;
    else if (button == 1) m_mouseRightDown = down;
    if (down) m_activeDevice = InputDevice::KeyboardMouse;
}

const char* RawInputEngine::FooterHint() const {
    if (m_activeDevice == InputDevice::Gamepad) {
        return "LB/RB: Domain | D-Pad/Stick: Tune | LT: Fine | A: Toggle | Y: Save | X: Load | RS: Theme | LB+RB+Start: Close";
    }
    return "Click/Drag or Arrows: Tune | Tab/PgUp/PgDn: Domain | Shift: Fine | S: Save | L: Load | P: Export | T: Theme | End: Close";
}

InputNavFrame RawInputEngine::Poll(void* hwndVoid, bool uiCapturing, float dtSec) {
    EnsureXInputLoaded();
    InputNavFrame f{};
    const float dt = std::clamp(dtSec, 0.001f, 0.1f);

    // 1. Poll XInput gamepad slots 0..3
    XInputStateAbi pad{};
    bool padOk = false;
    int32_t padSlot = -1;
    if (auto getState = reinterpret_cast<XInputGetStateFn>(m_xinputGetState)) {
        for (uint32_t slot = 0; slot < 4; ++slot) {
            if (getState(slot, &pad) == ERROR_SUCCESS) {
                padOk = true;
                padSlot = int32_t(slot);
                break;
            }
        }
    }
    m_gamepadConnected = padOk;
    m_gamepadSlot = padSlot;
    f.gamepadConnected = padOk;
    f.activeGamepadSlot = padSlot;

    uint16_t btns = 0;
    float lt = 0.0f, rt = 0.0f;
    if (padOk) {
        btns = pad.Gamepad.wButtons;
        lt = float(pad.Gamepad.bLeftTrigger) / 255.0f;
        rt = float(pad.Gamepad.bRightTrigger) / 255.0f;
        FilterAnalogStick(pad.Gamepad.sThumbLX, pad.Gamepad.sThumbLY, 0.22f, f.stickLX, f.stickLY);
        FilterAnalogStick(pad.Gamepad.sThumbRX, pad.Gamepad.sThumbRY, 0.22f, f.stickRX, f.stickRY);
        if (btns != 0 || std::fabs(f.stickLX) > 0.15f || std::fabs(f.stickLY) > 0.15f || lt > 0.25f || rt > 0.25f) {
            m_activeDevice = InputDevice::Gamepad;
        }
    }

    // 2. Sync cursor coordinates with game window client rect
    HWND hwnd = static_cast<HWND>(hwndVoid);
    if (hwnd) {
        POINT pt{};
        if (GetCursorPos(&pt) && ScreenToClient(hwnd, &pt)) {
            if (uiCapturing && (pt.x != m_mouseX || pt.y != m_mouseY)) {
                m_mouseX = pt.x;
                m_mouseY = pt.y;
                m_activeDevice = InputDevice::KeyboardMouse;
            }
        }
    }
    const bool lmbNow = m_mouseLeftDown || KeyDown(VK_LBUTTON);
    const bool rmbNow = m_mouseRightDown || KeyDown(VK_RBUTTON);
    f.mouseX = m_mouseX;
    f.mouseY = m_mouseY;
    f.mouseDown = lmbNow;
    f.mousePressed = lmbNow && !m_mouseLeftPrev;
    f.mouseReleased = !lmbNow && m_mouseLeftPrev;
    f.mouseRightPressed = rmbNow && !m_mouseRightPrev;
    m_mouseLeftPrev = lmbNow;
    m_mouseRightPrev = rmbNow;
    f.wheelDelta = m_wheelAccum;
    m_wheelAccum = 0;

    // 3. Keyboard modifiers & device detection
    const bool shift = KeyDown(VK_SHIFT);
    const bool ctrl  = KeyDown(VK_CONTROL);
    const bool kbNav = KeyDown(VK_END) || KeyDown(VK_UP) || KeyDown(VK_DOWN) || KeyDown(VK_LEFT) || KeyDown(VK_RIGHT) ||
                       KeyDown(VK_TAB) || KeyDown(VK_PRIOR) || KeyDown(VK_NEXT);
    if (kbNav) m_activeDevice = InputDevice::KeyboardMouse;
    f.activeDevice = m_activeDevice;

    f.fineMod   = shift || (lt > 0.35f);
    f.coarseMod = ctrl  || (rt > 0.35f);

    // 4. Unified UI navigation actions (Keyboard + Gamepad)
    const bool padToggle = ((btns & (kPadLB | kPadRB | kPadStart)) == (kPadLB | kPadRB | kPadStart));
    f.toggleStudio = m_kToggle.Update(KeyDown(VK_END) || padToggle, dt, false);

    if (uiCapturing) {
        f.back         = m_kBack.Update(KeyDown(VK_ESCAPE) || ((btns & kPadB) != 0), dt, false);
        f.confirm      = m_kConfirm.Update(KeyDown(VK_RETURN) || KeyDown(VK_SPACE) || ((btns & kPadA) != 0), dt, false);
        const bool kbNext = (KeyDown(VK_TAB) && !shift) || KeyDown(VK_NEXT) || KeyDown(VK_OEM_6);
        const bool kbPrev = (KeyDown(VK_TAB) && shift)  || KeyDown(VK_PRIOR) || KeyDown(VK_OEM_4);
        f.nextTab      = m_kNextTab.Update(kbNext || ((btns & kPadRB) != 0), dt, true);
        f.prevTab      = m_kPrevTab.Update(kbPrev || ((btns & kPadLB) != 0), dt, true);
        f.up           = m_kUp.Update(KeyDown(VK_UP) || ((btns & kPadDpadUp) != 0) || f.stickLY > 0.55f, dt, true);
        f.down         = m_kDown.Update(KeyDown(VK_DOWN) || ((btns & kPadDpadDown) != 0) || f.stickLY < -0.55f, dt, true);
        f.left         = m_kLeft.Update(KeyDown(VK_LEFT) || ((btns & kPadDpadLeft) != 0) || f.stickLX < -0.55f, dt, true);
        f.right        = m_kRight.Update(KeyDown(VK_RIGHT) || ((btns & kPadDpadRight) != 0) || f.stickLX > 0.55f, dt, true);
        f.save         = m_kSave.Update(KeyDown('S') || ((btns & kPadY) != 0), dt, false);
        f.load         = m_kLoad.Update(KeyDown('L') || ((btns & kPadX) != 0), dt, false);
        f.exportRecipe = m_kExport.Update(KeyDown('P') || ((btns & kPadBack) != 0), dt, false);
        f.toggleTheme  = m_kTheme.Update(KeyDown('T') || ((btns & kPadRThumb) != 0), dt, false);
    }

    // 5. Gameplay combat/action signals (exposed to raw_engine.dll even when menu is closed)
    const bool blockNow = (!uiCapturing) && (rmbNow || lt > 0.40f);
    const bool dodgeNow = (!uiCapturing) && (KeyDown(VK_LMENU) || ((btns & kPadB) != 0));
    const bool heavyNow = (!uiCapturing) && (rt > 0.65f);
    f.combatBlockHeld = blockNow;
    f.combatBlockEdge = m_kBlock.Update(blockNow, dt, false);
    f.combatDodgeEdge = m_kDodge.Update(dodgeNow, dt, false);
    f.combatHeavyEdge = m_kHeavy.Update(heavyNow, dt, false);
    f.quickWheelHeld  = (!uiCapturing) && (KeyDown('Q') || ((btns & kPadDpadDown) != 0));

    return f;
}

}  // namespace raw::ui
