#pragma once
// RawInputEngine.h: Unified Keyboard, Mouse, and XInput Gamepad Controller Engine (Zero SKSE).
// Features:
//   - Dynamic runtime XInput 1.4/1.3/9.1.0 loading (zero hard link dependencies)
//   - Circular radial stick deadzone + smooth quadratic response curve + repeat timers
//   - Auto-switching input device glyphs (KeyboardMouse vs Gamepad)
//   - Full mouse pointer tracking, hit-test click/drag, wheel scroll, and window-message isolation
//   - Exposes action-combat signals (timed parry block, dodge step, quick-menu wheel) to raw_engine.dll
// Original work, MIT.

#include <cstdint>

namespace raw::ui {

enum class InputDevice : uint8_t {
    KeyboardMouse = 0,
    Gamepad       = 1,
};

struct InputNavFrame {
    // High-level UI navigation actions (edge-triggered or auto-repeated)
    bool toggleStudio = false;   // End or Gamepad (LB + RB + Start)
    bool back         = false;   // Esc or Gamepad B
    bool confirm      = false;   // Enter/Space or Gamepad A
    bool nextTab      = false;   // Tab / PgDn / ] or Gamepad RB
    bool prevTab      = false;   // Shift+Tab / PgUp / [ or Gamepad LB
    bool up           = false;   // Up / W or D-Pad Up / L-Stick Up
    bool down         = false;   // Down / S or D-Pad Down / L-Stick Down
    bool left         = false;   // Left / A or D-Pad Left / L-Stick Left
    bool right        = false;   // Right / D or D-Pad Right / L-Stick Right
    bool save         = false;   // Ctrl+S / S or Gamepad Y
    bool load         = false;   // Ctrl+L / L or Gamepad X
    bool exportRecipe = false;   // P or Gamepad Back/View
    bool toggleTheme  = false;   // T or Gamepad RS click
    bool fineMod      = false;   // Shift or Gamepad LT held
    bool coarseMod    = false;   // Ctrl or Gamepad RT held

    // Mouse state in client-window pixels
    int32_t mouseX    = 0;
    int32_t mouseY    = 0;
    bool mouseDown    = false;
    bool mousePressed = false;   // rising edge
    bool mouseReleased = false;  // falling edge
    bool mouseRightPressed = false;
    int32_t wheelDelta = 0;      // notches (+1 up, -1 down)

    // Analog stick normalized [-1..+1] after radial deadzone
    float stickLX = 0.0f;
    float stickLY = 0.0f;
    float stickRX = 0.0f;
    float stickRY = 0.0f;

    // Active device & gamepad connection telemetry
    InputDevice activeDevice = InputDevice::KeyboardMouse;
    bool gamepadConnected = false;
    int32_t activeGamepadSlot = -1;

    // Gameplay combat/action signals (valid even when Studio menu is closed)
    bool combatBlockHeld     = false;  // RMB or Gamepad LT
    bool combatBlockEdge     = false;  // precision parry trigger
    bool combatDodgeEdge     = false;  // Left Alt / Space-tap or Gamepad B
    bool combatHeavyEdge     = false;  // Hold LMB or Gamepad RT
    bool quickWheelHeld      = false;  // Q or Gamepad D-Pad Down
};

// Pure math helper (testable headlessly without Win32/XInput hardware):
// Applies circular radial deadzone and quadratic response curve to raw [-32768..32767] stick axes.
void FilterAnalogStick(int16_t rawX, int16_t rawY, float deadzone, float& outX, float& outY);

class RawInputEngine {
public:
    RawInputEngine();

    // Polls keyboard, mouse, and XInput gamepad. dtSec drives smooth key/stick repeat timers.
    InputNavFrame Poll(void* hwnd, bool uiCapturing, float dtSec);

    // Feed WM_MOUSEWHEEL / WM_MOUSEMOVE / button state from window hook for low-latency precision.
    void OnMouseWheel(int32_t deltaNotches);
    void OnMouseMove(int32_t x, int32_t y);
    void OnMouseButton(int button, bool down);

    InputDevice ActiveDevice() const { return m_activeDevice; }
    bool GamepadConnected() const { return m_gamepadConnected; }

    // Human-readable control hint string automatically matched to the active device (no em dashes).
    const char* FooterHint() const;

private:
    struct RepeatKey {
        bool  prev = false;
        float heldSec = 0.0f;
        float nextFireSec = 0.0f;
        bool  Update(bool down, float dtSec, bool allowRepeat);
    };

    void EnsureXInputLoaded();

    void*   m_xinputDll = nullptr;
    void*   m_xinputGetState = nullptr;
    bool    m_xinputChecked = false;

    InputDevice m_activeDevice = InputDevice::KeyboardMouse;
    bool    m_gamepadConnected = false;
    int32_t m_gamepadSlot = -1;

    int32_t m_mouseX = 640;
    int32_t m_mouseY = 360;
    bool    m_mouseLeftDown = false;
    bool    m_mouseLeftPrev = false;
    bool    m_mouseRightDown = false;
    bool    m_mouseRightPrev = false;
    int32_t m_wheelAccum = 0;

    RepeatKey m_kToggle, m_kBack, m_kConfirm, m_kNextTab, m_kPrevTab;
    RepeatKey m_kUp, m_kDown, m_kLeft, m_kRight;
    RepeatKey m_kSave, m_kLoad, m_kExport, m_kTheme;
    RepeatKey m_kBlock, m_kDodge, m_kHeavy;
};

}  // namespace raw::ui
