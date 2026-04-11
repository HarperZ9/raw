#pragma once
//=============================================================================
//  ENBState.h — ENBSeries Global State
//
//  Central singleton holding all ENB runtime state: D3D11 objects, callbacks,
//  render info, weather/time state, editor state, and configuration.
//  Thread-safe where needed (atomic operations for Present-path data).
//=============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <cstdint>

// ---------------------------------------------------------------------------
//  SDK types — must match enbseries.h exactly for ABI compatibility
// ---------------------------------------------------------------------------

enum ENBParameterType : long
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

inline long ENBParameterTypeToSize(ENBParameterType type)
{
    switch (type) {
    case ENBParam_FLOAT:   return 4;
    case ENBParam_INT:     return 4;
    case ENBParam_HEX:     return 4;
    case ENBParam_BOOL:    return 4;
    case ENBParam_COLOR3:  return 12;
    case ENBParam_COLOR4:  return 16;
    case ENBParam_VECTOR3: return 12;
    default:               return 0;
    }
}

struct ENBParameter
{
    unsigned char    Data[16];
    unsigned long    Size;
    ENBParameterType Type;

    ENBParameter()
    {
        for (int k = 0; k < 16; k++) Data[k] = 0;
        Size = 0;
        Type = ENBParam_NONE;
    }
};

enum ENBCallbackType : long
{
    ENBCallback_EndFrame    = 1,
    ENBCallback_BeginFrame  = 2,
    ENBCallback_PreSave     = 3,
    ENBCallback_PostLoad    = 4,
    ENBCallback_OnInit      = 5,
    ENBCallback_OnExit      = 6,
    ENBCallback_PreReset    = 7,
    ENBCallback_PostReset   = 8,
    ENBCallback_FORCEDWORD  = 0x7fffffff
};

enum ENBStateType : long
{
    ENBState_IsEditorActive         = 1,
    ENBState_IsEffectsWndActive     = 2,
    ENBState_CursorPosX             = 3,
    ENBState_CursorPosY             = 4,
    ENBState_MouseLeft              = 5,
    ENBState_MouseRight             = 6,
    ENBState_MouseMiddle            = 7,
    ENBState_ulWeatherCurrent       = 8,
    ENBState_ulWeatherOutgoing      = 9,
    ENBState_fWeatherTransition     = 10,
    ENBState_fTimeOfDay             = 11,
    ENBState_fTODFactorDawn         = 12,
    ENBState_fTODFactorSunrise      = 13,
    ENBState_fTODFactorDay          = 14,
    ENBState_fTODFactorSunset       = 15,
    ENBState_fTODFactorDusk         = 16,
    ENBState_fTODFactorNight        = 17,
    ENBState_fTODFactorInteriorDay  = 18,
    ENBState_fTODFactorInteriorNight= 19,
    ENBState_fNightDayFactor        = 20,
    ENBState_fInteriorFactor        = 21,
    ENBState_ulWorldSpaceID         = 22,
    ENBState_ulLocationID           = 23,
    ENBState_FORCEDWORD             = 0x7fffffff
};

typedef void (WINAPI* ENBCallbackFunction)(ENBCallbackType calltype);

struct ENBRenderInfo
{
    void*  d3d11device;
    void*  d3d11devicecontext;
    void*  dxgiswapchain;
    DWORD  ScreenSizeX;
    DWORD  ScreenSizeY;

    ENBRenderInfo()
        : d3d11device(nullptr)
        , d3d11devicecontext(nullptr)
        , dxgiswapchain(nullptr)
        , ScreenSizeX(0)
        , ScreenSizeY(0)
    {}
};

// ---------------------------------------------------------------------------
//  SDK constants
// ---------------------------------------------------------------------------

static constexpr long ENB_SDK_VERSION   = 1002;  // v1.002
static constexpr long ENB_VERSION       = 492;   // v0.492
static constexpr long ENB_GAME_ID_GTA5  = 0x10000021;

// ---------------------------------------------------------------------------
//  Global ENB State
// ---------------------------------------------------------------------------

struct ENBGlobalState
{
    // ── D3D11 Objects ──────────────────────────────────────────────────
    ID3D11Device*        realDevice       = nullptr;
    ID3D11DeviceContext* realContext      = nullptr;
    IDXGISwapChain*      realSwapChain   = nullptr;
    ID3D11Device*        wrappedDevice    = nullptr;
    ID3D11DeviceContext* wrappedContext   = nullptr;
    IDXGISwapChain*      wrappedSwapChain = nullptr;
    HWND                 gameWindow       = nullptr;
    DWORD                screenWidth      = 0;
    DWORD                screenHeight     = 0;

    // ── Callback System ────────────────────────────────────────────────
    static constexpr DWORD kMaxCallbacks = 1023;
    ENBCallbackFunction  callbacks[kMaxCallbacks] = {};
    DWORD                callbackCount = 0;

    // ── Render Info (returned by ENBGetRenderInfo) ─────────────────────
    ENBRenderInfo        renderInfo;
    bool                 initialized = false;

    // ── Editor / UI State ──────────────────────────────────────────────
    bool  editorActive      = false;
    bool  effectsWndActive  = false;
    LONG  cursorPosX        = 0;
    LONG  cursorPosY        = 0;
    bool  mouseLeft         = false;
    bool  mouseRight        = false;
    bool  mouseMiddle       = false;
    bool  useEffect         = true;

    // ── Weather / Time State (from ENBHelper) ──────────────────────────
    DWORD currentWeather     = 0;
    DWORD outgoingWeather    = 0;
    float weatherTransition  = 0.0f;
    float timeOfDay          = 12.0f;
    float todFactorDawn      = 0.0f;
    float todFactorSunrise   = 0.0f;
    float todFactorDay       = 1.0f;
    float todFactorSunset    = 0.0f;
    float todFactorDusk      = 0.0f;
    float todFactorNight     = 0.0f;
    float nightDayFactor     = 1.0f;
    float interiorFactor     = 0.0f;
    float cameraPosition[3]  = {0.0f, 0.0f, 0.0f};

    // ── Thread Safety ──────────────────────────────────────────────────
    volatile LONG insideCallback  = 0;  // 1 = inside callback (SetParameter allowed)
    volatile LONG presentRefCount = 0;  // atomic present tracking
    CRITICAL_SECTION cs;

    // ── Raw D3D mode (for external tools) ──────────────────────────────
    volatile LONG rawD3DMode = 0;

    // ── Frame State ────────────────────────────────────────────────────
    UINT64 frameCount = 0;

    // ── Lifecycle ──────────────────────────────────────────────────────
    void Init()
    {
        InitializeCriticalSectionAndSpinCount(&cs, 4000);
    }

    void Shutdown()
    {
        DeleteCriticalSection(&cs);
    }

    void InvokeCallbacks(ENBCallbackType type)
    {
        for (DWORD i = 0; i < callbackCount; i++)
        {
            if (callbacks[i])
            {
                __try { callbacks[i](type); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
    }

    void UpdateRenderInfo()
    {
        renderInfo.d3d11device        = wrappedDevice;
        renderInfo.d3d11devicecontext = wrappedContext;
        renderInfo.dxgiswapchain      = wrappedSwapChain;
        renderInfo.ScreenSizeX        = screenWidth;
        renderInfo.ScreenSizeY        = screenHeight;
    }
};

// Single global instance
extern ENBGlobalState g_ENB;
