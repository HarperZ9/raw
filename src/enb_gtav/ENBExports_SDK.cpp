//=============================================================================
//  ENBExports_SDK.cpp — ENB SDK API Exports (8 functions)
//
//  These are the public SDK functions that plugins use. ABI must match
//  enbseries.h exactly. Behavior reconstructed from disassembly.
//=============================================================================

#include "ENBState.h"
#include "ConfigManager.h"
#include <cstring>

extern "C" {

// ---------------------------------------------------------------------------
//  ENBGetSDKVersion — Returns SDK version (1000-based: 1002 = v1.002)
// ---------------------------------------------------------------------------
__declspec(dllexport) long ENBGetSDKVersion()
{
    return ENB_SDK_VERSION;  // 1002
}

// ---------------------------------------------------------------------------
//  ENBGetVersion — Returns ENB version (492 = v0.492)
// ---------------------------------------------------------------------------
__declspec(dllexport) long ENBGetVersion()
{
    return ENB_VERSION;  // 492
}

// ---------------------------------------------------------------------------
//  ENBGetGameIdentifier — Returns unique game ID
// ---------------------------------------------------------------------------
__declspec(dllexport) long ENBGetGameIdentifier()
{
    return ENB_GAME_ID_GTA5;  // 0x10000021
}

// ---------------------------------------------------------------------------
//  ENBSetCallbackFunction — Register a callback function
//
//  Original disassembly (RVA 0x75940):
//    test rcx, rcx       ; null check
//    mov edx, [count]    ; current count
//    cmp edx, 0x3ff      ; max 1023
//    lea r8, [array]     ; callback array
//    mov [r8+rdx*8], rcx ; store
//    inc edx             ; count++
// ---------------------------------------------------------------------------
__declspec(dllexport) void ENBSetCallbackFunction(ENBCallbackFunction func)
{
    if (!func)
        return;

    if (g_ENB.callbackCount >= ENBGlobalState::kMaxCallbacks)
        return;

    g_ENB.callbacks[g_ENB.callbackCount] = func;
    g_ENB.callbackCount++;
}

// ---------------------------------------------------------------------------
//  ENBGetParameter — Read a parameter value
//
//  filename = INI file name (e.g. "enbseries.ini"), or NULL for shader vars
//  category = section name (e.g. "GLOBAL") or shader filename (e.g. "ENBEFFECT.FX")
//  keyname  = parameter name (e.g. "UseEffect" or "CC: Gamma")
//  outparam = receives the parameter value
//
//  Returns FALSE if category is NULL, or parameter not found.
//
//  Original disassembly (RVA 0x757F0):
//    test rdx, rdx       ; null-check category
//    jne continue
//    xor eax, eax        ; return FALSE
//    ret
// ---------------------------------------------------------------------------
__declspec(dllexport) BOOL ENBGetParameter(char* filename, char* category,
                                            char* keyname, ENBParameter* outparam)
{
    if (!category)
        return FALSE;

    if (!keyname || !outparam)
        return FALSE;

    return g_Config.GetParameter(filename, category, keyname, outparam);
}

// ---------------------------------------------------------------------------
//  ENBSetParameter — Write a parameter value
//
//  Same arguments as ENBGetParameter.
//  Returns FALSE if called outside of a callback (thread safety).
//
//  Original disassembly (RVA 0x758E0):
//    mov eax, [lock_flag]  ; check if inside callback
//    cmp eax, 1
//    jne fail              ; not inside callback -> return FALSE
// ---------------------------------------------------------------------------
__declspec(dllexport) BOOL ENBSetParameter(char* filename, char* category,
                                            char* keyname, ENBParameter* inparam)
{
    // Must be inside a callback to set parameters
    if (g_ENB.insideCallback != 1)
        return FALSE;

    if (!category || !keyname || !inparam)
        return FALSE;

    return g_Config.SetParameter(filename, category, keyname, inparam);
}

// ---------------------------------------------------------------------------
//  ENBGetRenderInfo — Returns pointer to static ENBRenderInfo struct
//
//  Original disassembly (RVA 0x75A90):
//    Reads device/context/swapchain/screensize from globals
//    Checks all are non-null + screensize non-zero
//    Populates static struct, returns pointer (or NULL if not ready)
// ---------------------------------------------------------------------------
__declspec(dllexport) ENBRenderInfo* ENBGetRenderInfo()
{
    if (!g_ENB.realDevice || !g_ENB.realContext || !g_ENB.realSwapChain)
        return nullptr;

    if (g_ENB.screenWidth == 0 || g_ENB.screenHeight == 0)
        return nullptr;

    if (!g_ENB.initialized)
        return nullptr;

    g_ENB.UpdateRenderInfo();
    return &g_ENB.renderInfo;
}

// ---------------------------------------------------------------------------
//  ENBGetState — Query various ENB state values
//
//  Original disassembly (RVA 0x75B10):
//    cmp ecx, 1          ; switch on state enum
//    reads from various globals based on state type
// ---------------------------------------------------------------------------
__declspec(dllexport) long ENBGetState(ENBStateType state)
{
    long result = 0;

    switch (state)
    {
    case ENBState_IsEditorActive:
        result = g_ENB.editorActive ? 1 : 0;
        break;
    case ENBState_IsEffectsWndActive:
        result = g_ENB.effectsWndActive ? 1 : 0;
        break;
    case ENBState_CursorPosX:
        result = g_ENB.cursorPosX;
        break;
    case ENBState_CursorPosY:
        result = g_ENB.cursorPosY;
        break;
    case ENBState_MouseLeft:
        result = g_ENB.mouseLeft ? 1 : 0;
        break;
    case ENBState_MouseRight:
        result = g_ENB.mouseRight ? 1 : 0;
        break;
    case ENBState_MouseMiddle:
        result = g_ENB.mouseMiddle ? 1 : 0;
        break;
    case ENBState_ulWeatherCurrent:
        result = static_cast<long>(g_ENB.currentWeather);
        break;
    case ENBState_ulWeatherOutgoing:
        result = static_cast<long>(g_ENB.outgoingWeather);
        break;
    case ENBState_fWeatherTransition:
        { float f = g_ENB.weatherTransition; memcpy(&result, &f, 4); }
        break;
    case ENBState_fTimeOfDay:
        { float f = g_ENB.timeOfDay; memcpy(&result, &f, 4); }
        break;
    case ENBState_fTODFactorDawn:
        { float f = g_ENB.todFactorDawn; memcpy(&result, &f, 4); }
        break;
    case ENBState_fTODFactorSunrise:
        { float f = g_ENB.todFactorSunrise; memcpy(&result, &f, 4); }
        break;
    case ENBState_fTODFactorDay:
        { float f = g_ENB.todFactorDay; memcpy(&result, &f, 4); }
        break;
    case ENBState_fTODFactorSunset:
        { float f = g_ENB.todFactorSunset; memcpy(&result, &f, 4); }
        break;
    case ENBState_fTODFactorDusk:
        { float f = g_ENB.todFactorDusk; memcpy(&result, &f, 4); }
        break;
    case ENBState_fTODFactorNight:
        { float f = g_ENB.todFactorNight; memcpy(&result, &f, 4); }
        break;
    case ENBState_fTODFactorInteriorDay:   // UNUSED in GTA5
    case ENBState_fTODFactorInteriorNight: // UNUSED in GTA5
        result = 0;
        break;
    case ENBState_fNightDayFactor:
        { float f = g_ENB.nightDayFactor; memcpy(&result, &f, 4); }
        break;
    case ENBState_fInteriorFactor:
        { float f = g_ENB.interiorFactor; memcpy(&result, &f, 4); }
        break;
    case ENBState_ulWorldSpaceID: // UNUSED in GTA5
    case ENBState_ulLocationID:   // UNUSED in GTA5
        result = 0;
        break;
    default:
        result = 0;
        break;
    }

    return result;
}

} // extern "C"
