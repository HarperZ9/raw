#include "D3D11Hook.h"
#include "DebugGUI.h"
#include "RmlD3D11.h"
#include "SB_ShaderDebug.h"
#include "LuminanceHistogram.h"
#include "TAAManager.h"
#include "SRVInjector.h"
#include "RenderPipeline.h"
#include "RenderPassManager.h"
#include "ComputeManager.h"
#include "MotionVectorGen.h"
#include "ShaderLoader.h"
#include "ShaderReload.h"
#include "TemporalSuperRes.h"
#include "FrameGenerator.h"
#include "VolumetricClouds.h"
#include "SSGIRenderer.h"
#include "MaterialClassifier.h"
#include "AtmosphereRenderer.h"
#include "ToneMapManager.h"
#include "ClusteredLighting.h"
#include "HiZPyramid.h"
#include "BootDiagnostics.h"
#include "GPUProfiler.h"
#include "FrameCapture.h"
#include "TextureDump.h"

// Frame update (defined in main.cpp) — runs trackers and data collection
extern void RunStandaloneFrameUpdate();

#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <atomic>
#include <Psapi.h>

#include "D3D11StateBackup.h"
#include "PhaseDispatcher.h"

// Forward declare the Win32 message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ── Proxy API (resolved at runtime from our d3d11.dll proxy) ─────────
namespace SB::Proxy
{
    // Must match src/d3d11_proxy/ProxyAPI.h layout EXACTLY.
    // RenderPhase is a uint8_t enum in the proxy.
    struct ProxyInterface
    {
        uint32_t version;
        ID3D11Device* device;
        ID3D11DeviceContext* context;
        IDXGISwapChain* swapChain;
        bool hdrCapable;
        bool hdrEnabled;
        DXGI_FORMAT backbufferFormat;
        uint32_t drawCallsThisFrame;
        uint32_t rtSwitchesThisFrame;
        uint32_t shaderChangesThisFrame;
        uint32_t frameCount;
        void (*RegisterPrePresent)(void(*)(ID3D11DeviceContext*, IDXGISwapChain*));
        void (*RegisterOnDraw)(void(*)(uint32_t, uint32_t));
        void (*RegisterOnRTChange)(void(*)(uint32_t, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*));
        void (*RegisterOnShaderBind)(void(*)(ID3D11PixelShader*, ID3D11VertexShader*));
        void (*RegisterOnResize)(void(*)(uint32_t, uint32_t, DXGI_FORMAT));
        void (*SetHDREnabled)(bool);
        float hdrMaxNits;
        float hdrPaperWhite;
        uint8_t currentPhase;         // RenderPhase enum
        const char* (*GetPhaseName)();
        void (*SetMaterialPipelineEnabled)(bool);
        bool materialPipelineActive;
        uint32_t materialPatchedCount;
        uint32_t materialCandidateCount;
        uint32_t materialClassifiedCount;
        bool deferredActive;
        ID3D11ShaderResourceView* gBufferAlbedo;
        ID3D11ShaderResourceView* gBufferNormals;
        ID3D11ShaderResourceView* gBufferMaterial;
        ID3D11DepthStencilView* gameDepthDSV;
        ID3D11ShaderResourceView* gameDepthSRV;

        // Optimization stats (must match ProxyAPI.h OptimizationStats)
        struct {
            uint32_t cbMapsIntercepted, cbUpdatesSkipped, cbUpdatesCommitted, cbTrackedBuffers;
            uint32_t srvCallsRedundant, srvCallsTotal;
            uint32_t blendCallsRedundant, blendCallsTotal;
            uint32_t dsCallsRedundant, dsCallsTotal;
            uint32_t rsCallsRedundant, rsCallsTotal;
            uint32_t occDrawsTested, occDrawsCulled;
        } optStats;

        // Pre-UI scene capture
        ID3D11ShaderResourceView* preUISceneSRV;
        ID3D11Texture2D*          preUISceneTex;
        bool                      preUISceneValid;

        // State cache invalidation
        void (*InvalidateStateCache)();

        // Phase change callback registration
        void (*RegisterOnPhaseChange)(void(*)(uint8_t, uint8_t));
    };
}

namespace D3D11Hook
{
    // ── State ────────────────────────────────────────────────────────────
    static bool s_initialized = false;
    static std::atomic<bool> s_guiVisible{false};
    static bool s_imguiInitialized = false;

    static ID3D11Device* s_device = nullptr;
    static ID3D11DeviceContext* s_context = nullptr;
    static IDXGISwapChain* s_swapChain = nullptr;
    static ID3D11RenderTargetView* s_renderTargetView = nullptr;

    static HWND s_gameWindow = nullptr;
    static WNDPROC s_originalWndProc = nullptr;

    // Proxy state
    static SB::Proxy::ProxyInterface* s_proxy = nullptr;
    static bool s_proxyMode = false;

    // Rendering kill switches for isolation:
    // F9 = disable PrePresent render passes (Bloom, DoF, Lens, Vignette, etc.)
    // F8 = disable compute passes + tracker updates (GTAO, SSR, SSGI, etc.)
    // F7 = disable mid-frame phase dispatch
    // Both F8+F9 = equivalent to master disable
    static bool s_disableRenderPasses = false;
    static bool s_disableComputePasses = false;

    // Original Present function pointer (stored from vtable) — legacy mode only
    using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    static PresentFn s_originalPresent = nullptr;
    static void** s_swapChainVTable = nullptr;

    // ── Cursor management ──────────────────────────────────────────────
    // Skyrim does TWO things that fight with ImGui mouse input:
    //   1. ClipCursor() — confines cursor to window center for mouselook
    //   2. SetCursorPos() — snaps cursor to screen center every frame
    // We must block BOTH when the debug GUI is open.
    static RECT s_savedClipRect{};
    static bool s_hasClipRect = false;
    static bool s_cursorForced = false;  // tracks whether we pumped ShowCursor(TRUE)

    // Hook SetCursorPos to prevent Skyrim from re-centering the cursor
    using SetCursorPosFn = BOOL(WINAPI*)(int, int);
    static SetCursorPosFn s_origSetCursorPos = nullptr;

    static BOOL WINAPI HookedSetCursorPos(int X, int Y)
    {
        // Block SetCursorPos while our GUI is active — prevents mouselook snap-back
        if (s_guiVisible)
            return TRUE;  // lie to Skyrim: "yes we moved the cursor" (we didn't)
        return s_origSetCursorPos(X, Y);
    }

    // Scan a single module's IAT for thunks pointing to targetAddr, redirect to hook
    static bool PatchModuleIATForAddr(uintptr_t moduleBase, void* targetAddr, void* hookAddr)
    {
        __try {
            auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(moduleBase);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
            auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(moduleBase + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

            auto& importDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
            if (importDir.VirtualAddress == 0) return false;

            auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(moduleBase + importDir.VirtualAddress);
            for (; imports->Name; ++imports) {
                auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(moduleBase + imports->FirstThunk);
                for (; thunk->u1.Function; ++thunk) {
                    if (reinterpret_cast<void*>(thunk->u1.Function) == targetAddr) {
                        DWORD oldProtect;
                        if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                            thunk->u1.Function = reinterpret_cast<uintptr_t>(hookAddr);
                            VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProtect, &oldProtect);
                            return true;
                        }
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    static void InstallCursorHook()
    {
        if (s_origSetCursorPos) return;  // already installed

        // Get the real SetCursorPos address from user32.dll
        HMODULE hUser32 = GetModuleHandleA("user32.dll");
        if (!hUser32) return;

        auto* realSetCursorPos = reinterpret_cast<SetCursorPosFn>(
            GetProcAddress(hUser32, "SetCursorPos"));
        if (!realSetCursorPos) return;

        s_origSetCursorPos = realSetCursorPos;

        // Scan ALL loaded modules' IATs for thunks pointing to the real SetCursorPos
        HANDLE hProcess = GetCurrentProcess();
        HMODULE hMods[512];
        DWORD cbNeeded;
        if (!EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded))
            return;

        DWORD numModules = cbNeeded / sizeof(HMODULE);
        if (numModules > static_cast<DWORD>(std::size(hMods)))
            numModules = static_cast<DWORD>(std::size(hMods));

        int patchCount = 0;
        for (DWORD i = 0; i < numModules; ++i) {
            if (!hMods[i]) continue;
            // Skip user32.dll itself — don't patch the source
            if (hMods[i] == hUser32) continue;
            if (PatchModuleIATForAddr(reinterpret_cast<uintptr_t>(hMods[i]),
                                       reinterpret_cast<void*>(realSetCursorPos),
                                       reinterpret_cast<void*>(&HookedSetCursorPos)))
                ++patchCount;
        }

        if (patchCount > 0) {
            SKSE::log::info("RAW: SetCursorPos IAT hook installed ({} module(s) patched)", patchCount);
        } else {
            SKSE::log::warn("RAW: SetCursorPos not found in any module IAT — cursor may snap during GUI");
        }
    }

    static void UnclipCursor()
    {
        // Always save the current clip rect (Skyrim may change it between calls)
        RECT currentRect;
        if (GetClipCursor(&currentRect)) {
            // Only save if Skyrim set a real clip (not our own nullptr unclip)
            if (currentRect.right - currentRect.left > 0 &&
                currentRect.bottom - currentRect.top > 0 &&
                (currentRect.left != 0 || currentRect.top != 0 ||
                 currentRect.right != GetSystemMetrics(SM_CXVIRTUALSCREEN) ||
                 currentRect.bottom != GetSystemMetrics(SM_CYVIRTUALSCREEN))) {
                s_savedClipRect = currentRect;
            }
            s_hasClipRect = true;
        }
        ClipCursor(nullptr);
    }

    static void RestoreClipCursor()
    {
        if (s_hasClipRect) {
            ClipCursor(&s_savedClipRect);
            s_hasClipRect = false;
        }
    }

    // ── Window Procedure Hook ────────────────────────────────────────────
    static LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        // Handle window focus changes (alt-tab)
        if (uMsg == WM_ACTIVATEAPP || uMsg == WM_ACTIVATE) {
            bool gaining = (uMsg == WM_ACTIVATEAPP) ? (wParam != 0) : (LOWORD(wParam) != WA_INACTIVE);
            if (s_imguiInitialized) {
                if (gaining && s_guiVisible) {
                    // Regaining focus with GUI open — reclaim cursor
                    ClipCursor(nullptr);
                    ImGui::GetIO().MouseDrawCursor = true;
                    // Clear all mouse button state to prevent stuck buttons
                    ImGui::GetIO().AddMouseButtonEvent(0, false);
                    ImGui::GetIO().AddMouseButtonEvent(1, false);
                    ImGui::GetIO().AddMouseButtonEvent(2, false);
                } else if (!gaining) {
                    // Losing focus — clear all input state to prevent stuck keys/buttons
                    ImGui::GetIO().AddMouseButtonEvent(0, false);
                    ImGui::GetIO().AddMouseButtonEvent(1, false);
                    ImGui::GetIO().AddMouseButtonEvent(2, false);
                    ImGui::GetIO().ClearInputKeys();
                    // Reset cursor force flag so it re-forces on next focus gain
                    s_cursorForced = false;
                }
            }
        }

        // F9: Disable PrePresent render passes (Bloom, DoF, Lens, Vignette, etc.)
        if (uMsg == WM_KEYDOWN && wParam == VK_F9) {
            s_disableRenderPasses = !s_disableRenderPasses;
            SKSE::log::info("RAW: Render passes {} (F9)",
                s_disableRenderPasses ? "DISABLED" : "ENABLED");
        }
        // F8: Disable compute passes + trackers (GTAO, SSR, SSGI, etc.)
        if (uMsg == WM_KEYDOWN && wParam == VK_F8) {
            s_disableComputePasses = !s_disableComputePasses;
            SKSE::log::info("RAW: Compute passes {} (F8)",
                s_disableComputePasses ? "DISABLED" : "ENABLED");
        }
        // F7: Disable mid-frame phase dispatch (PostGeometry, PostSky, PreUI)
        if (uMsg == WM_KEYDOWN && wParam == VK_F7) {
            auto& pd = SB::PhaseDispatcher::Get();
            pd.SetEnabled(!pd.IsEnabled());
            SKSE::log::info("RAW: Mid-frame dispatch {} (F7)",
                pd.IsEnabled() ? "ENABLED" : "DISABLED");
        }
        // F5: Dump all effect textures to disk for inspection
        if (uMsg == WM_KEYDOWN && wParam == VK_F5) {
            static uint32_t s_dumpFrame = 0;
            SB::TextureDump::DumpAllEffects(s_device, s_context, s_dumpFrame++);
        }
        // F10: Start frame capture (600 frames = ~10s at 60fps)
        if (uMsg == WM_KEYDOWN && wParam == VK_F10) {
            auto& cap = SB::FrameCapture::Get();
            if (cap.IsCapturing()) {
                cap.StopCapture();
                SKSE::log::info("RAW: Frame capture STOPPED manually (F10)");
            } else {
                cap.StartCapture(600);
                SKSE::log::info("RAW: Frame capture STARTED — 600 frames (F10)");
            }
        }
        // F11: Toggle GPU profiler
        if (uMsg == WM_KEYDOWN && wParam == VK_F11) {
            auto& prof = SB::GPUProfiler::Get();
            if (prof.IsInitialized()) {
                prof.SetEnabled(!prof.IsEnabled());
                SKSE::log::info("RAW: GPU Profiler {} (F11)",
                    prof.IsEnabled() ? "ENABLED" : "DISABLED");
            }
        }

        // F12: Hot-reload — recompile all shaders from disk
        if (uMsg == WM_KEYDOWN && wParam == VK_F12) {
            int count = SB::ReloadAllShaders();
            char msg[64];
            snprintf(msg, sizeof(msg), "RAW: %d shaders recompiled", count);
            RE::DebugNotification(msg);
        }

        // HOME: Toggle RmlUi HTML overlay
        if (uMsg == WM_KEYDOWN && wParam == VK_HOME) {
            bool vis = !RmlD3D11::IsVisible();
            RmlD3D11::SetVisible(vis);
            SKSE::log::info("RAW: RmlUi toggled {} (HOME)", vis ? "ON" : "OFF");
        }

        // Forward input to RmlUi when visible
        if (RmlD3D11::IsVisible() && RmlD3D11::IsInitialized()) {
            RmlD3D11::ProcessWindowMessage(hWnd, uMsg, wParam, lParam);
        }

        // INSERT: Toggle ImGui debug GUI
        if (uMsg == WM_KEYDOWN && wParam == VK_INSERT) {
            s_guiVisible = !s_guiVisible;
            SKSE::log::info("RAW: GUI toggled {} (INSERT)", s_guiVisible ? "ON" : "OFF");
            if (s_imguiInitialized) {
                ImGui::GetIO().MouseDrawCursor = s_guiVisible.load();
            }
            if (s_guiVisible) {
                UnclipCursor();
            } else {
                RestoreClipCursor();
                // Undo our forced ShowCursor(TRUE) calls
                if (s_cursorForced) {
                    int count = ShowCursor(FALSE);
                    while (count >= 0) count = ShowCursor(FALSE);
                    s_cursorForced = false;
                }
            }
        }

        // When GUI is visible: feed ALL input to ImGui, block game from seeing it
        if (s_guiVisible && s_imguiInitialized) {
            // Force cursor state every message when GUI is visible.
            // Skyrim constantly re-clips and hides the cursor — we must fight back
            // on every message, not just once per frame or on focus change.
            ClipCursor(nullptr);
            ImGui::GetIO().MouseDrawCursor = true;

            // Force the OS cursor visible (Skyrim hides it via ShowCursor(FALSE))
            // ShowCursor uses a counter — we only increment if currently hidden
            if (!s_cursorForced) {
                int count = ShowCursor(TRUE);
                while (count < 0) count = ShowCursor(TRUE);
                s_cursorForced = true;
            }

            // Always feed the message to ImGui (don't gate on return value)
            ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);

            // Handle WM_INPUT: parse mouse buttons for ImGui, block mouse from game,
            // but let keyboard raw input pass through for WASD movement.
            if (uMsg == WM_INPUT) {
                RAWINPUT raw{};
                UINT size = sizeof(raw);
                if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT,
                                     &raw, &size, sizeof(RAWINPUTHEADER)) != UINT(-1))
                {
                    if (raw.header.dwType == RIM_TYPEMOUSE) {
                        // Feed mouse buttons to ImGui (WM_LBUTTONDOWN may not arrive)
                        auto& io = ImGui::GetIO();
                        USHORT bf = raw.data.mouse.usButtonFlags;
                        if (bf & RI_MOUSE_LEFT_BUTTON_DOWN)    io.AddMouseButtonEvent(0, true);
                        if (bf & RI_MOUSE_LEFT_BUTTON_UP)      io.AddMouseButtonEvent(0, false);
                        if (bf & RI_MOUSE_RIGHT_BUTTON_DOWN)   io.AddMouseButtonEvent(1, true);
                        if (bf & RI_MOUSE_RIGHT_BUTTON_UP)     io.AddMouseButtonEvent(1, false);
                        if (bf & RI_MOUSE_MIDDLE_BUTTON_DOWN)  io.AddMouseButtonEvent(2, true);
                        if (bf & RI_MOUSE_MIDDLE_BUTTON_UP)    io.AddMouseButtonEvent(2, false);
                        if (bf & RI_MOUSE_WHEEL) {
                            float wd = static_cast<short>(raw.data.mouse.usButtonData)
                                     / static_cast<float>(WHEEL_DELTA);
                            io.AddMouseWheelEvent(0.0f, wd);
                        }
                        return 1;  // block mouse raw input from game
                    }
                    // Keyboard raw input: let it pass through to game for WASD
                }
            }

            // Block legacy mouse messages from the game
            if ((uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST) ||
                uMsg == WM_SETCURSOR)
            {
                return 1;
            }

            if (ImGui::GetIO().WantCaptureKeyboard &&
                ((uMsg >= WM_KEYFIRST && uMsg <= WM_KEYLAST) || uMsg == WM_CHAR))
            {
                return 1;
            }
        }

        return CallWindowProc(s_originalWndProc, hWnd, uMsg, wParam, lParam);
    }

    // ── Initialize ImGui ─────────────────────────────────────────────────
    static bool InitImGui()
    {
        if (s_imguiInitialized)
            return true;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;  // Don't save layout

        // Setup style
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 5.0f;
        style.FrameRounding = 3.0f;
        style.Alpha = 0.95f;

        // Initialize platform/renderer backends
        if (!ImGui_ImplWin32_Init(s_gameWindow)) {
            SKSE::log::error("RAW: ImGui_ImplWin32_Init failed");
            return false;
        }

        if (!ImGui_ImplDX11_Init(s_device, s_context)) {
            SKSE::log::error("RAW: ImGui_ImplDX11_Init failed");
            ImGui_ImplWin32_Shutdown();
            return false;
        }

        s_imguiInitialized = true;
        SKSE::log::info("RAW: ImGui initialized successfully");
        return true;
    }

    // ── Create Render Target ─────────────────────────────────────────────
    static bool CreateRenderTarget()
    {
        if (!s_swapChain) return false;

        ID3D11Texture2D* backBuffer = nullptr;
        HRESULT hr = s_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
        if (FAILED(hr)) {
            SKSE::log::error("RAW: Failed to get back buffer");
            return false;
        }

        hr = s_device->CreateRenderTargetView(backBuffer, nullptr, &s_renderTargetView);
        backBuffer->Release();

        if (FAILED(hr)) {
            SKSE::log::error("RAW: Failed to create render target view");
            return false;
        }

        return true;
    }

    // D3D11StateBackup is now in D3D11StateBackup.h (shared with PhaseDispatcher)
    using D3D11StateBackup = SB::D3D11StateBackup;

    // ── Overlay rendering (factored out for SEH compatibility) ─────────
    // This function contains C++ objects (ImGui), so it CANNOT use __try.
    // Instead, HookedPresent wraps the call in SEH.
    static int s_overlayErrorCount = 0;
    static bool s_overlayDisabled = false;

    // ── Pre-UI scene swap state ────────────────────────────────────────
    // When the proxy captures a pre-UI scene, we swap it into the backbuffer
    // before post-processing and restore UI pixels afterward.
    static ID3D11Texture2D*          s_uiSavedTex = nullptr;
    static ID3D11ShaderResourceView* s_uiSavedSRV = nullptr;

    // Save the current backbuffer (scene+UI) for later UI extraction
    static void SaveBackbufferWithUI(IDXGISwapChain* sc)
    {
        if (!s_device || !s_context || !sc) return;

        ID3D11Texture2D* backTex = nullptr;
        if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                    reinterpret_cast<void**>(&backTex))) || !backTex)
            return;

        // Create save texture on first use
        if (!s_uiSavedTex) {
            D3D11_TEXTURE2D_DESC desc;
            backTex->GetDesc(&desc);
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.CPUAccessFlags = 0;
            desc.MiscFlags = 0;
            if (SUCCEEDED(s_device->CreateTexture2D(&desc, nullptr, &s_uiSavedTex))) {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format = desc.Format;
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;
                s_device->CreateShaderResourceView(s_uiSavedTex, &srvDesc, &s_uiSavedSRV);
            }
        }

        if (s_uiSavedTex) {
            s_context->CopyResource(s_uiSavedTex, backTex);
        }
        backTex->Release();
    }

    // Restore pre-UI scene to backbuffer (so post-processing doesn't affect UI)
    static void RestorePreUIScene(IDXGISwapChain* sc)
    {
        if (!s_context || !sc || !s_proxy || !s_proxy->preUISceneTex) return;

        ID3D11Texture2D* backTex = nullptr;
        if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                       reinterpret_cast<void**>(&backTex))) && backTex) {
            s_context->CopyResource(backTex, s_proxy->preUISceneTex);
            backTex->Release();
        }
    }

    // After post-processing, composite UI pixels back on top.
    // UI pixels = where savedWithUI differs from preUIScene.
    static void CompositeUIBack(IDXGISwapChain* sc)
    {
        if (!s_context || !sc || !s_uiSavedTex || !s_proxy || !s_proxy->preUISceneTex)
            return;

        // Get the processed backbuffer
        ID3D11Texture2D* backTex = nullptr;
        if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                    reinterpret_cast<void**>(&backTex))) || !backTex)
            return;

        // We need to find pixels where the saved (scene+UI) differs from pre-UI scene.
        // Those are UI pixels — copy them from the saved texture to the processed backbuffer.
        // For now, use a simple CopyResource approach: we saved the scene+UI texture,
        // and we know which pixels are UI (they differ from preUI).
        //
        // Use a fullscreen pixel shader to do the composite:
        //   output = (savedWithUI != preUI) ? savedWithUI : processedScene
        //
        // This is registered as a one-time RenderPass below.
        backTex->Release();
    }

    static void DoOverlayWork(IDXGISwapChain* swapChain)
    {
        // Save ALL D3D11 state before we touch anything — restored at function exit
        D3D11StateBackup stateBackup;
        stateBackup.Save(s_context);

        // GPU Profiler: end previous frame's queries, collect results, start new frame
        {
            static bool s_profilerInitDone = false;
            auto& profiler = SB::GPUProfiler::Get();
            if (!s_profilerInitDone && s_device && s_context) {
                profiler.Initialize(s_device, s_context);
                s_profilerInitDone = true;
            }
            if (profiler.IsInitialized()) {
                profiler.EndFrame();
                profiler.BeginFrame();
            }
        }

        // Frame Capture: end previous frame, start new
        {
            auto& capture = SB::FrameCapture::Get();
            if (capture.IsCapturing()) {
                capture.EndFrame();

                static uint32_t s_captureFrameIdx = 0;
                capture.BeginFrame(s_captureFrameIdx++, 0.0f);

                // Proxy stats (ProxyDiagnostics removed)
            }
        }

        // Run tracker updates from the Present hook
        if (!s_disableComputePasses) {
            RunStandaloneFrameUpdate();
        }

        // Pre-UI scene swap DISABLED: the UI composite shader is not yet
        // implemented, so swapping in the pre-UI scene would present a frame
        // without Scaleform HUD and with potentially stale scene data.
        // Re-enable once CompositeUIBack() has a proper per-pixel CS pass.
        bool hasPreUI = false;

        if (!s_disableRenderPasses) {
        // ── Pre-UI scene swap: isolate post-processing from UI ──────────
        // If the proxy captured a pre-UI scene this frame, swap it in before
        // running any post-processing passes, then composite UI back after.
        if (hasPreUI) {
            // 1. Save current backbuffer (has scene+UI drawn by Scaleform)
            SaveBackbufferWithUI(swapChain);
            // 2. Restore pre-UI scene to backbuffer (clean scene, no HUD)
            RestorePreUIScene(swapChain);
        }

        // ── Fallback: if mid-frame PostGeometry never fired (legacy mode or
        // proxy phase detection failure), dispatch PostGeometry effects NOW
        // using present-time depth (one frame old but functional).
        {
            auto& pd = SB::PhaseDispatcher::Get();
            auto& pipeline = SB::RenderPipeline::Get();
            if (pipeline.IsInitialized() && pd.GetDispatchCount() == 0 &&
                pipeline.GetPassCount(SB::PipelineStage::PostGeometry) > 0)
            {
                static uint32_t s_fallbackLog = 0;
                if (s_fallbackLog++ < 5)
                    SKSE::log::warn("RAW: PostGeometry never fired — running effects at PrePresent (legacy fallback)");
                pipeline.ExecuteStage(SB::PipelineStage::PostGeometry, 0.0f, swapChain);
            }
        }

        // Execute PrePresent pipeline passes (compositor, debug overlay, etc.)
        {
            auto& pipeline = SB::RenderPipeline::Get();
            if (pipeline.IsInitialized())
                pipeline.ExecuteStage(SB::PipelineStage::PrePresent, 0.0f, swapChain);
        }

        } // end if (!s_disableRenderPasses)

        if (!s_disableComputePasses) {
        // GPU histogram: read-only (reads backbuffer, writes to own buffers)
        auto& histogram = SB::LuminanceHistogram::Get();
        if (histogram.IsInitialized() && histogram.IsEnabled()) {
            histogram.ReadBack(s_context);
            histogram.Dispatch(s_context, swapChain);
        }

        // TAA resolve: WRITES to backbuffer — only run when explicitly enabled
        auto& taa = SB::TAAManager::Get();
        if (taa.IsInitialized() && taa.IsEnabled()) {
            taa.Resolve(s_context, swapChain);
            SB::SRVInjector::Get().RegisterSRV(
                SB::TAAManager::kSRVSlot, taa.GetHistorySRV());
        }

        // ── Post-processing done — composite UI back on top ─────────────
        // For pixels where savedWithUI differs from preUIScene, those are UI.
        // Restore those pixels from the saved texture.
        if (hasPreUI && s_uiSavedTex) {
            ID3D11Texture2D* backTex = nullptr;
            if (SUCCEEDED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                           reinterpret_cast<void**>(&backTex))) && backTex) {
                // For each pixel: if savedUI != preUI → it's UI → overwrite with savedUI pixel
                // Simple approach: use CopySubresourceRegion with a stencil, or a CS pass.
                //
                // TEMPORARY SIMPLE APPROACH: Copy the entire saved UI texture back.
                // This overwrites the processed scene, but only for pixels that had UI.
                // We'll add a proper pixel-level composite CS later.
                //
                // For now, use a two-step workaround:
                // The processed backbuffer is correct for scene pixels.
                // We can't easily do a per-pixel copy without a shader, so we defer
                // the proper UI composite to when the ColorPipeline system lands
                // (which will include a UI-mask-aware composite pass).
                //
                // IMMEDIATE FIX: Run a compute shader that does the per-pixel compare.
                // Since ComputeManager may not be available here (it's in SKSE plugin),
                // just note that the pre-UI capture is working and scene effects
                // are applied to the clean scene. UI restoration is done below.
                backTex->Release();
            }

            // For now: re-copy the saved-with-UI pixels over the processed backbuffer
            // where they differ from the pre-UI scene.
            // Since we can't easily do per-pixel compare without a shader pass,
            // use the pre-UI scene capture to at least fix the HISTOGRAM and ADAPTATION
            // (which were metering UI brightness). The full per-pixel composite
            // requires a registered CS pass — see ColorPipeline for the final solution.
            //
            // PRACTICAL FIX: The post-processing passes (bloom, DoF, color) each get
            // a pre-UI scene copy via proxy. They should sample from preUISceneSRV
            // instead of backbuffer copy, making this composite unnecessary.
        }

        // Clear injected SRVs
        SB::SRVInjector::Get().ClearAll();

        } // end if (!s_disableComputePasses)

        // Sync game input freeze state
        UpdateInputFreeze();

        // First-time ImGui initialization
        if (!s_imguiInitialized && s_device && s_context && s_gameWindow) {
            if (CreateRenderTarget() && InitImGui()) {
                SKSE::log::info("RAW: ImGui initialized in Present hook");

                // Initialize RmlUi alongside ImGui
                DXGI_SWAP_CHAIN_DESC scDesc;
                if (SUCCEEDED(swapChain->GetDesc(&scDesc))) {
                    if (RmlD3D11::Init(s_device, s_context,
                                        scDesc.BufferDesc.Width,
                                        scDesc.BufferDesc.Height,
                                        s_gameWindow)) {
                        RmlD3D11::LoadDocument("Data/SKSE/Plugins/RAW/UI/panel.rml");
                        RmlD3D11::SetVisible(false);  // start hidden, toggle with HOME
                    }
                }
            }
        }

        // Render ImGui if visible
        if (s_guiVisible && s_imguiInitialized && s_renderTargetView) {
            // Ensure cursor is unclipped — Skyrim re-clips every frame
            ClipCursor(nullptr);

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();

            // Poll mouse buttons directly — WndProc messages (WM_LBUTTONDOWN, WM_INPUT)
            // may never reach us if another hook consumes them first.
            // GetAsyncKeyState reads hardware state, bypassing the message queue entirely.
            // ImGui deduplicates identical events, so this is safe even if WndProc also fires.
            {
                auto& io = ImGui::GetIO();
                io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
                io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
                io.AddMouseButtonEvent(2, (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
            }

            ImGui::NewFrame();

            SB::DebugGUI::Render();

            ImGui::Render();

            s_context->OMSetRenderTargets(1, &s_renderTargetView, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }

        // Render RmlUi overlay (HTML/CSS panels)
        if (RmlD3D11::IsInitialized() && RmlD3D11::IsVisible() && s_renderTargetView) {
            s_context->OMSetRenderTargets(1, &s_renderTargetView, nullptr);
            RmlD3D11::Update();
            RmlD3D11::Render();
        }

        // Render ShaderDebug overlay (independent of ImGui — has its own D3D11 pipeline)
        auto& shaderDbg = SB::Debug::ShaderDebug::Get();
        if (shaderDbg.IsInstalled()) {
            shaderDbg.ProcessInput();
            if (shaderDbg.IsOverlayVisible()) {
                if (!shaderDbg.IsOverlayInited())
                    shaderDbg.InitOverlayResources();
                shaderDbg.RenderOverlay();
            }
        }

        // Restore ALL D3D11 state — prevents corruption of Scaleform UI / next frame
        stateBackup.Restore(s_context);

        // Invalidate proxy's state cache — our render passes modified blend/DS/RS/SRV
        // state through the real context, which desyncs the proxy's redundancy cache.
        // Without this, the game's next-frame state calls get incorrectly skipped.
        if (s_proxy && s_proxy->InvalidateStateCache)
            s_proxy->InvalidateStateCache();
    }

    // ── Hooked Present ───────────────────────────────────────────────────
    // SEH wrapper: catches access violations / crashes in overlay code so
    // the game's Present always completes. Auto-disables overlay after
    // repeated crashes to prevent infinite crash loops.
    static HRESULT __stdcall HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
    {
        if (!s_overlayDisabled) {
            __try {
                DoOverlayWork(swapChain);
                s_overlayErrorCount = 0;  // reset on success
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                s_overlayErrorCount++;
                SB::BootDiag::LogError("HookedPresent", "ACCESS VIOLATION in DoOverlayWork");
                if (s_overlayErrorCount >= 3) {
                    s_overlayDisabled = true;
                    SB::BootDiag::LogError("HookedPresent", "OVERLAY DISABLED after 3 crashes");
                    SB::BootDiag::DumpReport();
                    SKSE::log::error("RAW: overlay crashed {} times — auto-disabled to protect game stability",
                        s_overlayErrorCount);
                }
            }
        }

        return s_originalPresent(swapChain, syncInterval, flags);
    }

    // ── Proxy PrePresent callback (called from d3d11.dll proxy's Present) ──
    static void ProxyPrePresentCallback(ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
    {
        // Update swap chain reference (in case of resize)
        s_swapChain = sc;
        s_context = ctx;

        if (!s_overlayDisabled) {
            __try {
                DoOverlayWork(sc);
                s_overlayErrorCount = 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                s_overlayErrorCount++;
                SB::BootDiag::LogError("ProxyPrePresent", "ACCESS VIOLATION in DoOverlayWork");
                if (s_overlayErrorCount >= 3) {
                    s_overlayDisabled = true;
                    SB::BootDiag::LogError("ProxyPrePresent", "OVERLAY DISABLED after 3 crashes");
                    SB::BootDiag::DumpReport();
                    SKSE::log::error("RAW: overlay crashed {} times — auto-disabled",
                        s_overlayErrorCount);
                }
            }
        }
    }

    // ── Try to connect to our d3d11.dll proxy ────────────────────────────
    static bool TryProxyInit()
    {
        HMODULE hD3D11 = GetModuleHandleA("d3d11.dll");
        if (!hD3D11) return false;

        using GetProxyFn = SB::Proxy::ProxyInterface* (*)();
        auto getProxy = reinterpret_cast<GetProxyFn>(
            GetProcAddress(hD3D11, "SB_GetProxyInterface"));

        if (!getProxy) {
            SKSE::log::info("RAW: d3d11.dll has no SB_GetProxyInterface — not our proxy");
            return false;
        }

        s_proxy = getProxy();
        if (!s_proxy || s_proxy->version < 1) {
            SKSE::log::error("RAW: Proxy interface invalid (version={})",
                s_proxy ? s_proxy->version : 0);
            return false;
        }

        // Get D3D11 objects from proxy (these are the REAL unwrapped objects)
        s_device    = s_proxy->device;
        s_context   = s_proxy->context;
        s_swapChain = s_proxy->swapChain;

        if (!s_device || !s_context || !s_swapChain) {
            SKSE::log::error("RAW: Proxy has null D3D11 objects — device={}, ctx={}, sc={}",
                (void*)s_device, (void*)s_context, (void*)s_swapChain);
            return false;
        }

        // Register our PrePresent callback with the proxy
        if (s_proxy->RegisterPrePresent)
            s_proxy->RegisterPrePresent(ProxyPrePresentCallback);

        // Register mid-frame phase dispatcher with the proxy
        if (s_proxy->RegisterOnPhaseChange) {
            s_proxy->RegisterOnPhaseChange([](uint8_t oldPhase, uint8_t newPhase) {
                SB::PhaseDispatcher::Get().OnPhaseChange(oldPhase, newPhase);
            });
        }

        s_proxyMode = true;
        SKSE::log::info("RAW: Connected to d3d11 proxy v{}", s_proxy->version);
        SKSE::log::info("  HDR capable: {}, HDR enabled: {}, format: {}",
            s_proxy->hdrCapable, s_proxy->hdrEnabled,
            static_cast<int>(s_proxy->backbufferFormat));
        SKSE::log::info("  Device: {}, Context: {}, SwapChain: {}",
            (void*)s_device, (void*)s_context, (void*)s_swapChain);

        return true;
    }

    // ── Hook the game's actual swap chain (legacy mode) ──────────────────
    static bool HookGameSwapChain()
    {
        // Get the renderer from Skyrim
        auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
        if (!renderer) {
            SKSE::log::error("RAW: BSGraphics::Renderer not available");
            return false;
        }

        auto& rendererData = renderer->data;
        s_swapChain = rendererData.renderWindows[0].swapChain;
        s_device = rendererData.forwarder;
        s_context = rendererData.context;
        s_gameWindow = reinterpret_cast<HWND>(rendererData.renderWindows[0].hWnd);

        if (!s_swapChain || !s_device || !s_context || !s_gameWindow) {
            SKSE::log::error("RAW: Null D3D11 objects from renderer");
            return false;
        }

        SKSE::log::info("RAW: Got game swap chain, device, and window (legacy mode)");

        // Hook the window procedure for input
        SetLastError(0);
        s_originalWndProc = (WNDPROC)SetWindowLongPtrA(s_gameWindow, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
        if (!s_originalWndProc && GetLastError() != 0) {
            SKSE::log::error("RAW: Failed to hook window procedure (error {})", GetLastError());
            return false;
        }
        if (!s_originalWndProc) {
            s_originalWndProc = DefWindowProcA;
            SKSE::log::warn("RAW: Previous WndProc was null — using DefWindowProc fallback");
        }

        InstallCursorHook();

        // Hook Present via vtable (legacy mode only — proxy mode uses callback)
        s_swapChainVTable = *reinterpret_cast<void***>(s_swapChain);
        s_originalPresent = reinterpret_cast<PresentFn>(s_swapChainVTable[8]);

        DWORD oldProtect;
        if (VirtualProtect(&s_swapChainVTable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            s_swapChainVTable[8] = reinterpret_cast<void*>(&HookedPresent);
            VirtualProtect(&s_swapChainVTable[8], sizeof(void*), oldProtect, &oldProtect);
            SKSE::log::info("RAW: Present hook installed via vtable (legacy)");
        } else {
            SKSE::log::error("RAW: VirtualProtect failed");
            return false;
        }

        return true;
    }

    // ── Game input freeze/thaw ──────────────────────────────────────────
    // When an overlay is active, suppress game input.
    // SAFE approach: only set ignoreKeyboardMouse flag.
    // ToggleControls is NOT thread-safe — calling it from Present (render thread)
    // while the game processes ButtonEvents on the main thread causes crashes.
    // The WndProc hook already blocks WM_* input messages from reaching the game.
    static bool s_inputFrozen = false;

    static void FreezeGameInput()
    {
        if (s_inputFrozen) return;
        auto* controlMap = RE::ControlMap::GetSingleton();
        if (!controlMap) return;

        controlMap->ignoreKeyboardMouse = true;
        s_inputFrozen = true;
    }

    static void ThawGameInput()
    {
        if (!s_inputFrozen) return;
        auto* controlMap = RE::ControlMap::GetSingleton();
        if (!controlMap) return;

        controlMap->ignoreKeyboardMouse = false;
        s_inputFrozen = false;
    }

    bool ShouldFreezeInput()
    {
        // Currently no overlay requires full input freeze.
        // The debug GUI lets keyboard (WASD) pass through.
        return false;
    }

    void UpdateInputFreeze()
    {
        if (ShouldFreezeInput()) {
            FreezeGameInput();
        } else {
            ThawGameInput();
        }
    }

    // ── Shared init for window hook (both proxy and legacy need this) ───
    static bool InitWindowHook()
    {
        // Get game window
        if (!s_gameWindow) {
            auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
            if (renderer)
                s_gameWindow = reinterpret_cast<HWND>(renderer->data.renderWindows[0].hWnd);
        }

        if (!s_gameWindow) {
            SKSE::log::error("RAW: Game window is null");
            return false;
        }

        // Hook the window procedure for input
        SetLastError(0);
        s_originalWndProc = (WNDPROC)SetWindowLongPtrA(s_gameWindow, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
        if (!s_originalWndProc && GetLastError() != 0) {
            SKSE::log::error("RAW: Failed to hook window procedure (error {})", GetLastError());
            return false;
        }
        if (!s_originalWndProc) {
            s_originalWndProc = DefWindowProcA;
            SKSE::log::warn("RAW: Previous WndProc was null — using DefWindowProc fallback");
        }

        InstallCursorHook();
        return true;
    }

    // ── Public Interface ─────────────────────────────────────────────────
    bool Init()
    {
        if (s_initialized)
            return true;

        // Try proxy mode first (our d3d11.dll loaded)
        if (TryProxyInit()) {
            SKSE::log::info("RAW: Using PROXY mode (d3d11.dll proxy active)");
            InitWindowHook();
            s_initialized = true;
            SKSE::log::info("RAW: D3D11 hook initialized (proxy) — press INSERT to toggle GUI");
            return true;
        }

        // Fall back to legacy vtable hook (vanilla d3d11)
        SKSE::log::info("RAW: No proxy detected — using LEGACY mode (vtable hook)");
        if (!HookGameSwapChain()) {
            SKSE::log::error("RAW: Failed to hook game swap chain");
            return false;
        }

        s_initialized = true;
        SKSE::log::info("RAW: D3D11 hook initialized (legacy) — press INSERT to toggle GUI");
        return true;
    }

    void Shutdown()
    {
        if (!s_initialized)
            return;

        ThawGameInput();

        // Shutdown GPU rendering systems (reverse init order)
        SB::MotionVectorGen::Get().Shutdown();
        // [DISABLED] SB::TemporalSuperRes::Get().Shutdown();
        // [DISABLED] SB::FrameGenerator::Get().Shutdown();
        // [DISABLED] SB::VolumetricClouds::Get().Shutdown();
        SB::SSGIRenderer::Get().Shutdown();
        SB::MaterialClassifier::Get().Shutdown();
        // [DISABLED] SB::AtmosphereRenderer::Get().Shutdown();
        SB::ToneMapManager::Get().Shutdown();
        SB::ClusteredLighting::Get().Shutdown();
        SB::TAAManager::Get().Shutdown();
        SB::HiZPyramid::Get().Shutdown();
        SB::LuminanceHistogram::Get().Shutdown();
        SB::RenderPipeline::Get().Shutdown();
        SB::RenderPassManager::Get().Shutdown();
        SB::ComputeManager::Get().Shutdown();

        // Restore original Present (legacy mode only — proxy mode doesn't modify vtable)
        if (!s_proxyMode && s_swapChainVTable && s_originalPresent) {
            DWORD oldProtect;
            if (VirtualProtect(&s_swapChainVTable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                s_swapChainVTable[8] = reinterpret_cast<void*>(s_originalPresent);
                VirtualProtect(&s_swapChainVTable[8], sizeof(void*), oldProtect, &oldProtect);
            }
        }

        if (s_imguiInitialized) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }

        if (s_renderTargetView) {
            s_renderTargetView->Release();
            s_renderTargetView = nullptr;
        }

        if (s_originalWndProc && s_gameWindow) {
            SetWindowLongPtrA(s_gameWindow, GWLP_WNDPROC, (LONG_PTR)s_originalWndProc);
        }

        s_initialized = false;
        s_imguiInitialized = false;
        s_proxyMode = false;
        s_proxy = nullptr;
    }

    void ToggleGUI()
    {
        s_guiVisible = !s_guiVisible;
        if (s_guiVisible) {
            UnclipCursor();
        } else {
            RestoreClipCursor();
        }
        if (s_imguiInitialized) {
            ImGui::GetIO().MouseDrawCursor = s_guiVisible.load();
        }
        SKSE::log::info("RAW: GUI toggled {}", s_guiVisible ? "ON" : "OFF");
    }

    bool IsGUIVisible()
    {
        return s_guiVisible;
    }

    void SetGUIVisible(bool a_visible)
    {
        if (a_visible && !s_guiVisible) {
            UnclipCursor();
        } else if (!a_visible && s_guiVisible) {
            RestoreClipCursor();
        }
        s_guiVisible = a_visible;
        if (s_imguiInitialized) {
            ImGui::GetIO().MouseDrawCursor = s_guiVisible.load();
        }
    }

    ID3D11Device* GetDevice() { return s_device; }
    ID3D11DeviceContext* GetContext() { return s_context; }
    IDXGISwapChain* GetSwapChain() { return s_swapChain; }

    bool IsProxyActive() { return s_proxyMode; }
    bool IsHDREnabled()  { return s_proxy ? s_proxy->hdrEnabled : false; }
    bool IsHDRCapable()  { return s_proxy ? s_proxy->hdrCapable : false; }

    bool IsMaterialPipelineActive() { return s_proxy ? s_proxy->materialPipelineActive : false; }
    void SetMaterialPipelineEnabled(bool enabled) {
        if (s_proxy && s_proxy->SetMaterialPipelineEnabled)
            s_proxy->SetMaterialPipelineEnabled(enabled);
    }
    uint32_t GetMaterialPatchedCount() { return s_proxy ? s_proxy->materialPatchedCount : 0; }
    uint32_t GetMaterialCandidateCount() { return s_proxy ? s_proxy->materialCandidateCount : 0; }
    uint32_t GetMaterialClassifiedCount() { return s_proxy ? s_proxy->materialClassifiedCount : 0; }

    ID3D11ShaderResourceView* GetGameDepthSRV()          { return s_proxy ? s_proxy->gameDepthSRV    : nullptr; }
    ID3D11ShaderResourceView* GetGBufferAlbedoSRV()      { return s_proxy ? s_proxy->gBufferAlbedo   : nullptr; }
    ID3D11ShaderResourceView* GetGBufferNormalsSRV()      { return s_proxy ? s_proxy->gBufferNormals  : nullptr; }
    ID3D11ShaderResourceView* GetGBufferMaterialSRV()    { return s_proxy ? s_proxy->gBufferMaterial : nullptr; }

    ID3D11ShaderResourceView* GetPreUISceneSRV()  { return (s_proxy && s_proxy->preUISceneValid) ? s_proxy->preUISceneSRV : nullptr; }
    ID3D11Texture2D*          GetPreUISceneTex()  { return (s_proxy && s_proxy->preUISceneValid) ? s_proxy->preUISceneTex : nullptr; }
    bool                      IsPreUISceneValid() { return s_proxy && s_proxy->preUISceneValid; }

    InvalidateCacheFn GetInvalidateCacheFn()
    {
        return (s_proxy && s_proxy->InvalidateStateCache)
               ? s_proxy->InvalidateStateCache : nullptr;
    }
}
