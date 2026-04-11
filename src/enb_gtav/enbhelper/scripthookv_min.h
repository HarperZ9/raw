#pragma once
//=============================================================================
//  scripthookv_min.h — Minimal ScriptHookV API declarations
//
//  Only the 6 functions imported by enbhelper.dll.
//  The real ScriptHookV.dll must be present in the game directory.
//  These are C++ mangled imports resolved by the linker against ScriptHookV.lib
//  or by GetProcAddress at runtime.
//=============================================================================

#include <Windows.h>
#include <cstdint>

// ScriptHookV function signatures (from ScriptHookV SDK main.h)
// These are the C++ exported functions from ScriptHookV.dll

// Register a script thread. The callback runs each frame in ScriptHookV's fiber.
// void scriptRegister(HMODULE module, void(*LP_SCRIPT_MAIN)());
typedef void(*LP_SCRIPT_MAIN)();

extern "C" {
    // From ScriptHookV.dll — resolved at load time
    __declspec(dllimport) void scriptRegister(HMODULE module, LP_SCRIPT_MAIN scriptMain);
    __declspec(dllimport) void scriptUnregister(HMODULE module);
    __declspec(dllimport) void scriptWait(DWORD time);
    __declspec(dllimport) void nativeInit(uint64_t hash);
    __declspec(dllimport) void nativePush64(uint64_t val);
    __declspec(dllimport) uint64_t* nativeCall();
}
