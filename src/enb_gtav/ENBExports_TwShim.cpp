//=============================================================================
//  ENBExports_TwShim.cpp — AntTweakBar Shim Exports (46 functions)
//
//  Phase 1: Functional stubs that don't crash. Return success where expected.
//  Phase 5: Will be backed by ImGui for actual UI rendering.
//=============================================================================

#include "ENBExports_TwShim.h"
#include <cstring>

// ---------------------------------------------------------------------------
//  Internal bar storage (simple linked list for now)
// ---------------------------------------------------------------------------
struct TwBarInternal
{
    char  name[256];
    int   id;
};

static TwBarInternal s_bars[64];
static int           s_barCount = 0;
static int           s_nextBarId = 1;
static const char*   s_lastError = nullptr;

extern "C" {

// === Lifecycle ===

__declspec(dllexport) int TwInit(TwGraphAPI graphAPI, void* device)
{
    // TODO Phase 5: Initialize ImGui with the D3D11 device
    (void)graphAPI;
    (void)device;
    return 1; // success
}

__declspec(dllexport) int TwTerminate()
{
    // TODO Phase 5: Shutdown ImGui
    s_barCount = 0;
    return 1;
}

__declspec(dllexport) int TwDraw()
{
    // TODO Phase 5: Call ImGui::Render() and draw all bars
    return 1;
}

// === Bar Management ===

__declspec(dllexport) TwBar* TwNewBar(const char* barName)
{
    if (!barName || s_barCount >= 64)
        return nullptr;

    TwBarInternal* bar = &s_bars[s_barCount];
    strncpy_s(bar->name, barName, 255);
    bar->id = s_nextBarId++;
    s_barCount++;

    return reinterpret_cast<TwBar*>(bar);
}

__declspec(dllexport) int TwDeleteBar(TwBar* bar)
{
    (void)bar;
    return 1;
}

__declspec(dllexport) int TwDeleteAllBars()
{
    s_barCount = 0;
    return 1;
}

// === Variable Binding ===

__declspec(dllexport) int TwAddVarRW(TwBar* bar, const char* name, TwType type,
                                      void* var, const char* def)
{
    // TODO Phase 5: Register variable for ImGui rendering
    (void)bar; (void)name; (void)type; (void)var; (void)def;
    return 1;
}

__declspec(dllexport) int TwAddVarRO(TwBar* bar, const char* name, TwType type,
                                      const void* var, const char* def)
{
    (void)bar; (void)name; (void)type; (void)var; (void)def;
    return 1;
}

__declspec(dllexport) int TwAddVarCB(TwBar* bar, const char* name, TwType type,
                                      TwSetVarCallback setCallback,
                                      TwGetVarCallback getCallback,
                                      void* clientData, const char* def)
{
    (void)bar; (void)name; (void)type;
    (void)setCallback; (void)getCallback; (void)clientData; (void)def;
    return 1;
}

__declspec(dllexport) int TwAddButton(TwBar* bar, const char* name,
                                       TwButtonCallback callback,
                                       void* clientData, const char* def)
{
    (void)bar; (void)name; (void)callback; (void)clientData; (void)def;
    return 1;
}

__declspec(dllexport) int TwAddSeparator(TwBar* bar, const char* name, const char* def)
{
    (void)bar; (void)name; (void)def;
    return 1;
}

__declspec(dllexport) int TwRemoveVar(TwBar* bar, const char* name)
{
    (void)bar; (void)name;
    return 1;
}

__declspec(dllexport) int TwRemoveAllVars(TwBar* bar)
{
    (void)bar;
    return 1;
}

// === Definition / Configuration ===

__declspec(dllexport) int TwDefine(const char* def)
{
    (void)def;
    return 1;
}

__declspec(dllexport) TwType TwDefineEnum(const char* name, const void* enumValues,
                                           unsigned int nbValues)
{
    (void)name; (void)enumValues; (void)nbValues;
    return TW_TYPE_INT32;
}

__declspec(dllexport) TwType TwDefineEnumFromString(const char* name, const char* enumString)
{
    (void)name; (void)enumString;
    return TW_TYPE_INT32;
}

__declspec(dllexport) TwType TwDefineStruct(const char* name, const void* structMembers,
                                             unsigned int nbMembers, size_t structSize,
                                             void* summaryCallback, void* clientData)
{
    (void)name; (void)structMembers; (void)nbMembers;
    (void)structSize; (void)summaryCallback; (void)clientData;
    return TW_TYPE_UNDEF;
}

// === Parameter Get/Set ===

__declspec(dllexport) int TwGetParam(TwBar* bar, const char* varName,
                                      const char* paramName, TwType paramValueType,
                                      unsigned int outValueMaxCount, void* outValues)
{
    (void)bar; (void)varName; (void)paramName;
    (void)paramValueType; (void)outValueMaxCount; (void)outValues;
    return 0;
}

__declspec(dllexport) int TwSetParam(TwBar* bar, const char* varName,
                                      const char* paramName, TwType paramValueType,
                                      unsigned int inValueCount, const void* inValues)
{
    (void)bar; (void)varName; (void)paramName;
    (void)paramValueType; (void)inValueCount; (void)inValues;
    return 1;
}

// === Bar Queries ===

__declspec(dllexport) TwBar* TwGetBarByIndex(int barIndex)
{
    if (barIndex < 0 || barIndex >= s_barCount)
        return nullptr;
    return reinterpret_cast<TwBar*>(&s_bars[barIndex]);
}

__declspec(dllexport) TwBar* TwGetBarByName(const char* barName)
{
    if (!barName) return nullptr;
    for (int i = 0; i < s_barCount; i++)
    {
        if (strcmp(s_bars[i].name, barName) == 0)
            return reinterpret_cast<TwBar*>(&s_bars[i]);
    }
    return nullptr;
}

__declspec(dllexport) int TwGetBarCount()
{
    return s_barCount;
}

__declspec(dllexport) const char* TwGetBarName(TwBar* bar)
{
    if (!bar) return nullptr;
    TwBarInternal* b = reinterpret_cast<TwBarInternal*>(bar);
    return b->name;
}

__declspec(dllexport) TwBar* TwGetBottomBar()
{
    return s_barCount > 0 ? reinterpret_cast<TwBar*>(&s_bars[s_barCount - 1]) : nullptr;
}

__declspec(dllexport) TwBar* TwGetTopBar()
{
    return s_barCount > 0 ? reinterpret_cast<TwBar*>(&s_bars[0]) : nullptr;
}

__declspec(dllexport) int TwSetBottomBar(TwBar* bar)
{
    (void)bar;
    return 1;
}

__declspec(dllexport) int TwSetTopBar(TwBar* bar)
{
    (void)bar;
    return 1;
}

__declspec(dllexport) int TwRefreshBar(TwBar* bar)
{
    (void)bar;
    return 1;
}

__declspec(dllexport) void TwSetBarFontSize(TwBar* bar, int fontSize)
{
    (void)bar; (void)fontSize;
}

// === Window Management ===

__declspec(dllexport) int TwGetCurrentWindow()
{
    return 0;
}

__declspec(dllexport) int TwSetCurrentWindow(int windowID)
{
    (void)windowID;
    return 1;
}

__declspec(dllexport) int TwWindowExists(int windowID)
{
    (void)windowID;
    return windowID == 0 ? 1 : 0;
}

__declspec(dllexport) int TwWindowSize(int width, int height)
{
    (void)width; (void)height;
    return 1;
}

// === Error Handling ===

__declspec(dllexport) const char* TwGetLastError()
{
    return s_lastError ? s_lastError : "";
}

__declspec(dllexport) void TwHandleErrors(void (*errorHandler)(const char* errorMessage))
{
    (void)errorHandler;
}

// === Input Events ===

__declspec(dllexport) int TwKeyPressed(int key, int modifiers)
{
    (void)key; (void)modifiers;
    return 0; // not handled
}

__declspec(dllexport) int TwKeyTest(int key, int modifiers)
{
    (void)key; (void)modifiers;
    return 0;
}

__declspec(dllexport) int TwMouseButton(int action, int button)
{
    (void)action; (void)button;
    return 0;
}

__declspec(dllexport) int TwMouseMotion(int mouseX, int mouseY)
{
    (void)mouseX; (void)mouseY;
    return 0;
}

__declspec(dllexport) int TwMouseWheel(int pos)
{
    (void)pos;
    return 0;
}

__declspec(dllexport) int TwEventWin(void* wnd, unsigned int msg,
                                      unsigned int wParam, int lParam)
{
    (void)wnd; (void)msg; (void)wParam; (void)lParam;
    return 0; // not handled
}

__declspec(dllexport) int TwEventWin32(void* wnd, unsigned int msg,
                                        unsigned int wParam, int lParam)
{
    // TODO Phase 5: Forward to ImGui Win32 input handler
    (void)wnd; (void)msg; (void)wParam; (void)lParam;
    return 0; // not handled
}

// === String Copy Utilities ===

__declspec(dllexport) void TwCopyCDStringToClientFunc(TwCopyCDStringToClient copyFunc)
{
    (void)copyFunc;
}

__declspec(dllexport) void TwCopyCDStringToLibrary(char** destPtr, const char* src)
{
    (void)destPtr; (void)src;
}

__declspec(dllexport) void TwCopyStdStringToClientFunc(void* copyFunc)
{
    (void)copyFunc;
}

__declspec(dllexport) void TwCopyStdStringToLibrary(void* destString, const char* src)
{
    (void)destString; (void)src;
}

} // extern "C"
