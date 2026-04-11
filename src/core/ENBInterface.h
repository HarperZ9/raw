#pragma once
//=============================================================================
//  ENBInterface.h — STUB (ENB support removed)
//
//  Type definitions preserved for backward compatibility with backend systems
//  that haven't been fully cleaned up yet. All functions are inline no-ops.
//=============================================================================

#include "BridgeData.h"
#include <cstdint>
#include <cstring>

namespace ENBInterface
{
    // ── ENB SDK type definitions (preserved for compile compatibility) ────

    enum class CallbackType : int
    {
        EndFrame = 1,
        BeginFrame = 2,
        PreSave = 3,
        PostLoad = 4,
        OnInit = 5,
        OnExit = 6,
        PreReset = 7,
        PostReset = 8
    };

    enum class ENBParameterType : long
    {
        ENBParam_NONE       = 0,
        ENBParam_FLOAT      = 1,
        ENBParam_INT        = 2,
        ENBParam_HEX        = 3,
        ENBParam_BOOL       = 4,
        ENBParam_COLOR3     = 5,
        ENBParam_COLOR4     = 6,
        ENBParam_VECTOR3    = 7,
        ENBParam_FORCEDWORD = 0x7fffffff
    };

    struct ENBParameter
    {
        unsigned char    Data[16];
        unsigned long    Size;
        ENBParameterType Type;

        ENBParameter()
            : Size(0), Type(ENBParameterType::ENBParam_NONE)
        {
            for (int k = 0; k < 16; k++) Data[k] = 0;
        }
    };

    using ENBCallbackFunction = void(__stdcall*)(int a_callbackType);

    using _ENBGetSDKVersion       = long(__stdcall*)();
    using _ENBGetVersion          = long(__stdcall*)();
    using _ENBSetCallbackFunction = void(__stdcall*)(ENBCallbackFunction a_func);
    using _ENBGetParameter        = int(__stdcall*)(const char*, const char*, const char*, ENBParameter*);
    using _ENBSetParameter        = int(__stdcall*)(const char*, const char*, const char*, ENBParameter*);

    enum class ENBStateType : long { IsEditorActive = 1 };
    using _ENBGetState = long(__stdcall*)(ENBStateType);

    // ── All function pointers are null (ENB not supported) ──────────────
    inline _ENBGetSDKVersion       GetSDKVersion       = nullptr;
    inline _ENBGetVersion          GetVersion          = nullptr;
    inline _ENBSetCallbackFunction SetCallbackFunction = nullptr;
    inline _ENBGetParameter        GetParameter        = nullptr;
    inline _ENBSetParameter        SetParameter        = nullptr;
    inline _ENBGetState            GetState            = nullptr;

    // ── Stub functions (always return false/empty) ──────────────────────
    inline bool Init()                { return false; }
    inline bool IsLoaded()            { return false; }
    inline bool IsGUISupported()      { return false; }
    inline bool IsEditorOpen()        { return false; }
    inline bool IsEffectsWindowOpen() { return false; }

    struct PushStats
    {
        int dirtyParams = 0, totalParams = 0, setParamCalls = 0;
        int setParamSuccess = 0, setParamFail = 0;
        std::size_t pushCount = 0;
        bool firstPushDone = false;
    };

    inline const PushStats& GetPushStats()
    {
        static PushStats s;
        return s;
    }

    inline void PushAllData(const SB::AllData&) {}
}
