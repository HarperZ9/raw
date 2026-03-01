#pragma once
//=============================================================================
//  ENBInterface.h — Runtime resolution of ENBSeries SDK functions
//
//  Based on the ENB SDK by Boris Vorontsov (enbdev.com) and
//  doodlum's enb-api wrapper (github.com/doodlum/enb-api).
//
//  ENB for Skyrim SE ships as d3d11.dll (DX11 wrapper). It exports
//  C functions that third-party plugins can call to read/write shader
//  parameters and register per-frame callbacks.
//=============================================================================

#include "BridgeData.h"
#include <cstdint>

namespace ENBInterface
{
    // ── ENB SDK type definitions ────────────────────────────────────────
    // These match Boris Vorontsov's enbseries SDK and doodlum's enb-api.
    // We define our own to avoid Windows.h conflicts with CommonLibSSE-NG.

    // Callback types for ENB
    enum class CallbackType : int
    {
        EndFrame = 0,       // After Present
        BeginFrame = 1,     // Before Present
        PreSave = 2,        // Before config/screenshot save
        PostLoad = 3,       // After config load
        OnInit = 4          // ENB initialization
    };

    // Callback invoked once per frame.
    //   a_callbackType indicates the phase of rendering
    using ENBCallbackFunction = void(__stdcall*)(int a_callbackType);

    // GUI rendering callback (for ImGui integration)
    using ENBGUICallback = void(__stdcall*)(void);

    // ── SDK function signatures ─────────────────────────────────────────
    // All ENB SDK exports use __stdcall calling convention.
    // Get/SetParameter use a flat (void* value, int size) interface.

    // Returns the SDK version number (e.g., 1001 for SDK v1.001).
    using _ENBGetSDKVersion = long(__stdcall*)();

    // Returns the ENBSeries binary version.
    using _ENBGetVersion = long(__stdcall*)();

    // Registers a callback invoked each frame.
    using _ENBSetCallbackFunction = void(__stdcall*)(ENBCallbackFunction a_func);

    // Gets a shader parameter value.
    //   a_filename:  Shader filename (e.g., "enbsunsprite.fx")
    //   a_category:  UI category in ENB editor, or empty string
    //   a_keyname:   Parameter name as declared in the shader
    //   a_value:     Pointer to output buffer
    //   a_size:      Size of the output buffer in bytes
    // Returns non-zero on success.
    using _ENBGetParameter = int(__stdcall*)(
        const char* a_filename,
        const char* a_category,
        const char* a_keyname,
        void*       a_value,
        int         a_size
    );

    // Sets a shader parameter value.
    //   Same signature as Get, but writes TO the shader.
    // Returns non-zero on success.
    using _ENBSetParameter = int(__stdcall*)(
        const char* a_filename,
        const char* a_category,
        const char* a_keyname,
        void*       a_value,
        int         a_size
    );

    // GUI-related functions (available in newer ENB SDK versions)
    // Returns true if ENB GUI is currently being rendered
    using _ENBIsEditorActive = int(__stdcall*)();

    // Register a callback for GUI rendering (ImGui context)
    using _ENBSetGUICallback = void(__stdcall*)(ENBGUICallback a_func);

    // Get ENB's D3D11 device (for custom rendering)
    using _ENBGetD3D11Device = void*(__stdcall*)();

    // ── Resolved function pointers ──────────────────────────────────────
    // These are populated by Init() and remain valid for the process lifetime.

    inline _ENBGetSDKVersion       GetSDKVersion       = nullptr;
    inline _ENBGetVersion          GetVersion          = nullptr;
    inline _ENBSetCallbackFunction SetCallbackFunction = nullptr;
    inline _ENBGetParameter        GetParameter        = nullptr;
    inline _ENBSetParameter        SetParameter        = nullptr;
    inline _ENBIsEditorActive      IsEditorActive      = nullptr;
    inline _ENBSetGUICallback      SetGUICallback      = nullptr;
    inline _ENBGetD3D11Device      GetD3D11Device      = nullptr;

    // ── Initialization ──────────────────────────────────────────────────

    // Resolves ENB SDK functions from d3d11.dll.
    // Must be called AFTER ENB has loaded (kPostLoad or kPostPostLoad).
    // Returns true if ENB was found and all functions were resolved.
    bool Init();

    // Returns true if Init() succeeded and ENB functions are available.
    bool IsLoaded();

    // Returns true if ENB GUI integration is supported (newer ENB versions)
    bool IsGUISupported();

    // ── SkyrimBridge data push ──────────────────────────────────────────

    // Pushes all parameters from AllData to all target shaders.
    // This iterates through kParamTable and calls SetParameter for each
    // parameter to each shader in kTargetShaders.
    void PushAllData(const SB::AllData& a_data);
}
