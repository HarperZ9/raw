#pragma once
//=============================================================================
//  ENBExports_TwShim.h — AntTweakBar API Compatibility Shim
//
//  Provides the 46 Tw* exported functions that ENB originally exposed.
//  Internally backed by ImGui (Phase 5). For now, functional stubs that
//  allow plugins to call these without crashing.
//=============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// Opaque bar handle (matches AntTweakBar's TwBar*)
typedef struct TwBar TwBar;

// AntTweakBar type enum (matches original)
enum TwType
{
    TW_TYPE_UNDEF   = 0,
    TW_TYPE_BOOLCPP = 1,
    TW_TYPE_BOOL8   = 2,
    TW_TYPE_BOOL16  = 3,
    TW_TYPE_BOOL32  = 4,
    TW_TYPE_CHAR    = 5,
    TW_TYPE_INT8    = 6,
    TW_TYPE_UINT8   = 7,
    TW_TYPE_INT16   = 8,
    TW_TYPE_UINT16  = 9,
    TW_TYPE_INT32   = 10,
    TW_TYPE_UINT32  = 11,
    TW_TYPE_FLOAT   = 12,
    TW_TYPE_DOUBLE  = 13,
    TW_TYPE_COLOR32 = 14,
    TW_TYPE_COLOR3F = 15,
    TW_TYPE_COLOR4F = 16,
    TW_TYPE_CDSTRING= 17,
    TW_TYPE_STDSTRING = 0x2fff0000,
    TW_TYPE_QUAT4F  = 19,
    TW_TYPE_QUAT4D  = 20,
    TW_TYPE_DIR3F   = 21,
    TW_TYPE_DIR3D   = 22,
};

// AntTweakBar graphics API enum
enum TwGraphAPI
{
    TW_OPENGL       = 1,
    TW_DIRECT3D9    = 2,
    TW_DIRECT3D10   = 3,
    TW_DIRECT3D11   = 4,
    TW_OPENGL_CORE  = 5,
};

// Callback types for TwAddVarCB
typedef void (*TwSetVarCallback)(const void* value, void* clientData);
typedef void (*TwGetVarCallback)(void* value, void* clientData);
typedef void (*TwButtonCallback)(void* clientData);
typedef void (*TwCopyCDStringToClient)(char** destPtr, const char* src);
