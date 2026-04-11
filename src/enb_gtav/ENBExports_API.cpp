//=============================================================================
//  ENBExports_API.cpp — ENB Internal API Exports (9 functions)
//
//  These are the internal API functions used by ENB itself and advanced
//  integrations (e.g., FiveM, AltV). Behavior from disassembly at RVAs
//  0x600A0 - 0x60249.
//=============================================================================

#include "ENBState.h"
#include "ENBHelperBridge.h"
#include "TimeOfDaySystem.h"
#include "WeatherSystem.h"
#include "PostProcessPipeline.h"
#include "InputManager.h"
#include "ScreenshotCapture.h"
#include "ENBParamsCompute.h"
#include "FPSLimiter.h"

extern "C" {

// ---------------------------------------------------------------------------
//  API_SetRawD3DMode — Set raw D3D mode flag (atomic)
//
//  Original: xchg [rawD3DMode], ecx; mov eax, 1; xchg [flag2], eax; ret
// ---------------------------------------------------------------------------
__declspec(dllexport) void API_SetRawD3DMode(int mode)
{
    InterlockedExchange(&g_ENB.rawD3DMode, mode);
}

// ---------------------------------------------------------------------------
//  API_GetD3D11Device — Return real + wrapped device pointers
//
//  Original (RVA 0x600C0):
//    rcx = output for real device (from wrapper+0x28)
//    rdx = output for wrapped device
// ---------------------------------------------------------------------------
__declspec(dllexport) int API_GetD3D11Device(void** outReal, void** outWrapped)
{
    if (!g_ENB.wrappedDevice)
        return 0;

    if (outReal)
        *outReal = g_ENB.realDevice;
    if (outWrapped)
        *outWrapped = g_ENB.wrappedDevice;

    return 1;
}

// ---------------------------------------------------------------------------
//  API_GetD3D11DeviceContext — Return real + wrapped context pointers
//
//  Original (RVA 0x600F0):
//    rcx = output for real context (from wrapper+0x6C20)
//    rdx = output for wrapped context
// ---------------------------------------------------------------------------
__declspec(dllexport) int API_GetD3D11DeviceContext(void** outReal, void** outWrapped)
{
    if (!g_ENB.wrappedContext)
        return 0;

    if (outReal)
        *outReal = g_ENB.realContext;
    if (outWrapped)
        *outWrapped = g_ENB.wrappedContext;

    return 1;
}

// ---------------------------------------------------------------------------
//  API_GetDXGISwapChain — Return real + wrapped swapchain pointers
//
//  Original (RVA 0x60130):
//    rcx = output for real swapchain (from wrapper+0x28)
//    rdx = output for wrapped swapchain
// ---------------------------------------------------------------------------
__declspec(dllexport) int API_GetDXGISwapChain(void** outReal, void** outWrapped)
{
    if (!g_ENB.wrappedSwapChain)
        return 0;

    if (outReal)
        *outReal = g_ENB.realSwapChain;
    if (outWrapped)
        *outWrapped = g_ENB.wrappedSwapChain;

    return 1;
}

// ---------------------------------------------------------------------------
//  API_BeforePresent — Called before IDXGISwapChain::Present
//
//  Original (RVA 0x60160):
//    lock add [refcount], 1
//    lock xadd [refcount2], eax  ; atomic decrement
//    if last ref: call pre-present handler (shader pipeline + UI)
//
//  This is where the ENB post-processing pipeline runs.
// ---------------------------------------------------------------------------
__declspec(dllexport) void API_BeforePresent()
{
    InterlockedIncrement(&g_ENB.presentRefCount);
    LONG prev = InterlockedDecrement(&g_ENB.presentRefCount);

    if (prev == 0 && g_ENB.wrappedDevice)
    {
        // Update game state from enbhelper.dll
        g_Helper.UpdateGameState();

        // Feed helper data into global ENB state
        g_ENB.timeOfDay          = g_Helper.time;
        g_ENB.currentWeather     = static_cast<DWORD>(g_Helper.currentWeather);
        g_ENB.outgoingWeather    = static_cast<DWORD>(g_Helper.outgoingWeather);
        g_ENB.weatherTransition  = g_Helper.weatherTransition;
        g_ENB.interiorFactor     = g_Helper.isInterior ? 1.0f : 0.0f;
        g_ENB.cameraPosition[0]  = g_Helper.cameraPosition[0];
        g_ENB.cameraPosition[1]  = g_Helper.cameraPosition[1];
        g_ENB.cameraPosition[2]  = g_Helper.cameraPosition[2];

        // Compute time-of-day weights
        TimeOfDayWeights todWeights = g_TOD.Compute(g_ENB.timeOfDay);
        g_ENB.todFactorDawn    = todWeights.dawn;
        g_ENB.todFactorSunrise = todWeights.sunrise;
        g_ENB.todFactorDay     = todWeights.day;
        g_ENB.todFactorSunset  = todWeights.sunset;
        g_ENB.todFactorDusk    = todWeights.dusk;
        g_ENB.todFactorNight   = todWeights.night;
        g_ENB.nightDayFactor   = g_TOD.ComputeNightDayFactor(g_ENB.timeOfDay);

        // Update weather system
        g_Weather.Update(g_ENB.currentWeather, g_ENB.outgoingWeather,
                         g_ENB.weatherTransition);

        // Allow ENBSetParameter during callbacks
        InterlockedExchange(&g_ENB.insideCallback, 1);

        // Invoke EndFrame callbacks
        g_ENB.InvokeCallbacks(ENBCallback_EndFrame);

        // Compute per-frame ENB shader parameters (bloom/lens amounts, adaptation)
        g_ENBParams.Update(g_FPSLimiter.GetFrameTimeMs() * 0.001f);

        // Phase 5: Process input (toggles, hotkeys, debug vars)
        g_Input.Update();

        // Phase 5: Screenshot capture
        if (g_Input.IsScreenshotQueued())
        {
            char gameDir[MAX_PATH];
            GetModuleFileNameA(nullptr, gameDir, MAX_PATH);
            char* s = strrchr(gameDir, '\\');
            if (s) *s = '\0';
            g_Screenshot.Capture(g_ENB.realDevice, g_ENB.realContext,
                                 g_ENB.realSwapChain, gameDir);
            g_Input.ClearScreenshotQueue();
        }

        // Phase 4: Run post-processing shader pipeline
        // TODO: Capture backbuffer SRV and depth SRV from game's Present
        // Once the wrapped swapchain intercepts Present(), we'll have
        // the backbuffer RTV/SRV and can call:
        // g_Pipeline.Execute(g_ENB.realDevice, g_ENB.realContext,
        //                    backbufferRTV, backbufferSRV, depthSRV);

        // TODO Phase 5b: ImGui/TwShim render here

        InterlockedExchange(&g_ENB.insideCallback, 0);
    }
}

// ---------------------------------------------------------------------------
//  API_AfterPresent — Called after IDXGISwapChain::Present
//
//  Original (RVA 0x601A0): Same atomic pattern, calls post-present handler
// ---------------------------------------------------------------------------
__declspec(dllexport) void API_AfterPresent()
{
    InterlockedIncrement(&g_ENB.presentRefCount);
    LONG prev = InterlockedDecrement(&g_ENB.presentRefCount);

    if (prev == 0 && g_ENB.wrappedDevice)
    {
        InterlockedExchange(&g_ENB.insideCallback, 1);

        // Invoke BeginFrame callbacks
        g_ENB.InvokeCallbacks(ENBCallback_BeginFrame);

        InterlockedExchange(&g_ENB.insideCallback, 0);

        g_ENB.frameCount++;

        // FPS limiter (busy-wait to hit target framerate)
        g_FPSLimiter.Wait();
    }
}

// ---------------------------------------------------------------------------
//  API_SetWindow — Store game window handle and dimensions
//
//  Original (RVA 0x601E0):
//    mov [screenWidth], edx
//    mov [screenHeight], r8d
//    mov [gameWindow], rcx
// ---------------------------------------------------------------------------
__declspec(dllexport) void API_SetWindow(HWND hwnd, DWORD width, DWORD height)
{
    g_ENB.gameWindow    = hwnd;
    g_ENB.screenWidth   = width;
    g_ENB.screenHeight  = height;
}

// ---------------------------------------------------------------------------
//  API_BeforeDisplayModeChange — Called before display mode change
//
//  Original (RVA 0x60210): Calls PreReset callbacks to release resources
// ---------------------------------------------------------------------------
__declspec(dllexport) void API_BeforeDisplayModeChange()
{
    if (g_ENB.wrappedDevice)
    {
        g_ENB.InvokeCallbacks(ENBCallback_PreReset);
    }
}

// ---------------------------------------------------------------------------
//  API_AfterDisplayModeChange — Called after display mode change
//
//  Original (RVA 0x60230): Calls PostReset callbacks to recreate resources
// ---------------------------------------------------------------------------
__declspec(dllexport) void API_AfterDisplayModeChange()
{
    if (g_ENB.wrappedDevice)
    {
        g_ENB.InvokeCallbacks(ENBCallback_PostReset);
    }
}

} // extern "C"
