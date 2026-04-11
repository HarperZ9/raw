#pragma once
//=============================================================================
//  InputManager.h — Keyboard/Mouse Hook for ENB
//
//  Uses SetWindowsHookExA to intercept keyboard and mouse input.
//  Handles ENB hotkeys (toggle effect, open editor, screenshot)
//  and passes input to the UI system when the editor is active.
//=============================================================================

#include <Windows.h>

class InputManager
{
public:
    bool Initialize(HINSTANCE hInstance);
    void Shutdown();

    // Process input state each frame (called from API_BeforePresent)
    void Update();

    // Query state
    bool IsEditorToggled()    const { return m_editorToggled; }
    bool IsEffectToggled()    const { return m_effectToggled; }
    bool IsScreenshotQueued() const { return m_screenshotQueued; }

    // Clear one-shot flags after processing
    void ClearEditorToggle()    { m_editorToggled = false; }
    void ClearEffectToggle()    { m_effectToggled = false; }
    void ClearScreenshotQueue() { m_screenshotQueued = false; }

    // Debug temp variables (keyboard keys 1-8 + PgUp/PgDn)
    float tempVars[10] = {1,1,1,1,1,1,1,1,1,1};

private:
    static LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);

    void ProcessKeyDown(int vkCode);
    void ProcessKeyUp(int vkCode);

    HHOOK m_keyboardHook = nullptr;
    HHOOK m_mouseHook    = nullptr;

    // Key config (from enblocal.ini)
    int m_keyCombination = 16;  // VK_SHIFT
    int m_keyUseEffect   = 123; // VK_F12
    int m_keyEditor      = 13;  // VK_RETURN
    int m_keyScreenshot  = 44;  // VK_SNAPSHOT
    int m_keyFPSLimit    = 36;  // VK_HOME
    int m_keyShowFPS     = 106; // VK_MULTIPLY

    // One-shot toggle flags
    bool m_editorToggled    = false;
    bool m_effectToggled    = false;
    bool m_screenshotQueued = false;

    // Key states
    bool m_comboHeld = false;

    // Singleton access for hook callbacks
    static InputManager* s_instance;
};

extern InputManager g_Input;
