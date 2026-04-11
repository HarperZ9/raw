//=============================================================================
//  InputManager.cpp — Keyboard/Mouse Hook Implementation
//=============================================================================

#include "InputManager.h"
#include "ConfigManager.h"
#include "ENBState.h"
#include <cstdio>

InputManager  g_Input;
InputManager* InputManager::s_instance = nullptr;

bool InputManager::Initialize(HINSTANCE hInstance)
{
    s_instance = this;

    // Read key bindings from enblocal.ini config
    const auto& cfg = g_Config.GetLocalConfig();
    m_keyCombination = cfg.keyCombination;
    m_keyUseEffect   = cfg.keyUseEffect;
    m_keyEditor      = cfg.keyEditor;
    m_keyScreenshot  = cfg.keyScreenshot;
    m_keyFPSLimit    = cfg.keyFPSLimit;
    m_keyShowFPS     = cfg.keyShowFPS;

    // Install low-level keyboard hook
    m_keyboardHook = SetWindowsHookExA(WH_KEYBOARD_LL, KeyboardHookProc, hInstance, 0);
    if (!m_keyboardHook)
    {
        OutputDebugStringA("[ENB] WARNING: Failed to install keyboard hook\n");
        // Non-fatal — we can still poll with GetAsyncKeyState
    }

    return true;
}

void InputManager::Shutdown()
{
    if (m_keyboardHook)
    {
        UnhookWindowsHookEx(m_keyboardHook);
        m_keyboardHook = nullptr;
    }
    if (m_mouseHook)
    {
        UnhookWindowsHookEx(m_mouseHook);
        m_mouseHook = nullptr;
    }
    s_instance = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Hook callbacks
// ═══════════════════════════════════════════════════════════════════════════

LRESULT CALLBACK InputManager::KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && s_instance)
    {
        KBDLLHOOKSTRUCT* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
            s_instance->ProcessKeyDown(kb->vkCode);
        else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
            s_instance->ProcessKeyUp(kb->vkCode);
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK InputManager::MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    // Forward to UI system if editor is active
    if (nCode >= 0 && s_instance && g_ENB.editorActive)
    {
        // TODO Phase 5b: Forward to ImGui
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Key processing
// ═══════════════════════════════════════════════════════════════════════════

void InputManager::ProcessKeyDown(int vkCode)
{
    // Check if combination key is held
    m_comboHeld = (GetAsyncKeyState(m_keyCombination) & 0x8000) != 0;

    if (!m_comboHeld)
    {
        // Handle PageUp/PageDown for temp vars when number keys are held
        for (int i = 0; i < 10; i++)
        {
            int numKey = (i == 0) ? '0' : ('0' + i); // keys 1-9, 0
            if (i >= 1 && i <= 9) numKey = '0' + i;
            else numKey = '0';

            if (GetAsyncKeyState(numKey) & 0x8000)
            {
                if (vkCode == VK_PRIOR) // PageUp
                    tempVars[i] += 0.01f;
                else if (vkCode == VK_NEXT) // PageDown
                    tempVars[i] -= 0.01f;
            }
        }
        return;
    }

    // Combination key is held — check for ENB hotkeys
    if (vkCode == m_keyUseEffect)
        m_effectToggled = true;
    else if (vkCode == m_keyEditor)
        m_editorToggled = true;
    else if (vkCode == m_keyScreenshot)
        m_screenshotQueued = true;
}

void InputManager::ProcessKeyUp(int vkCode)
{
    if (vkCode == m_keyCombination)
        m_comboHeld = false;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Per-frame update (poll-based fallback + process toggle flags)
// ═══════════════════════════════════════════════════════════════════════════

void InputManager::Update()
{
    // Process toggle flags
    if (m_effectToggled)
    {
        g_ENB.useEffect = !g_ENB.useEffect;
        OutputDebugStringA(g_ENB.useEffect ? "[ENB] Effects ON\n" : "[ENB] Effects OFF\n");
        m_effectToggled = false;
    }

    if (m_editorToggled)
    {
        g_ENB.editorActive = !g_ENB.editorActive;
        OutputDebugStringA(g_ENB.editorActive ? "[ENB] Editor OPEN\n" : "[ENB] Editor CLOSED\n");
        m_editorToggled = false;
    }

    // Update mouse state
    g_ENB.mouseLeft   = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    g_ENB.mouseRight  = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    g_ENB.mouseMiddle = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

    // Update cursor position
    POINT cursorPos;
    if (GetCursorPos(&cursorPos))
    {
        if (g_ENB.gameWindow)
            ScreenToClient(g_ENB.gameWindow, &cursorPos);
        g_ENB.cursorPosX = cursorPos.x;
        g_ENB.cursorPosY = cursorPos.y;
    }
}
