#include "D3D11Hook.h"
#include "DebugGUI.h"
#include "SB_ShaderDebug.h"

#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

// Forward declare the Win32 message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace D3D11Hook
{
    // ── State ────────────────────────────────────────────────────────────
    static bool s_initialized = false;
    static bool s_guiVisible = false;
    static bool s_imguiInitialized = false;

    static ID3D11Device* s_device = nullptr;
    static ID3D11DeviceContext* s_context = nullptr;
    static IDXGISwapChain* s_swapChain = nullptr;
    static ID3D11RenderTargetView* s_renderTargetView = nullptr;

    static HWND s_gameWindow = nullptr;
    static WNDPROC s_originalWndProc = nullptr;

    // Original Present function pointer (stored from vtable)
    using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    static PresentFn s_originalPresent = nullptr;
    static void** s_swapChainVTable = nullptr;

    // ── Window Procedure Hook ────────────────────────────────────────────
    static LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        // Toggle GUI with Insert key
        if (uMsg == WM_KEYDOWN && wParam == VK_INSERT) {
            s_guiVisible = !s_guiVisible;
            SKSE::log::info("SkyrimBridge: GUI toggled {}", s_guiVisible ? "ON" : "OFF");
        }

        // Let ImGui handle input when visible
        if (s_guiVisible && s_imguiInitialized) {
            if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) {
                return true;
            }

            // Block game input when GUI has focus
            ImGuiIO& io = ImGui::GetIO();
            if (io.WantCaptureMouse || io.WantCaptureKeyboard) {
                if (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST)
                    return true;
                if (uMsg >= WM_KEYFIRST && uMsg <= WM_KEYLAST)
                    return true;
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
            SKSE::log::error("SkyrimBridge: ImGui_ImplWin32_Init failed");
            return false;
        }

        if (!ImGui_ImplDX11_Init(s_device, s_context)) {
            SKSE::log::error("SkyrimBridge: ImGui_ImplDX11_Init failed");
            ImGui_ImplWin32_Shutdown();
            return false;
        }

        s_imguiInitialized = true;
        SKSE::log::info("SkyrimBridge: ImGui initialized successfully");
        return true;
    }

    // ── Create Render Target ─────────────────────────────────────────────
    static bool CreateRenderTarget()
    {
        if (!s_swapChain) return false;

        ID3D11Texture2D* backBuffer = nullptr;
        HRESULT hr = s_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
        if (FAILED(hr)) {
            SKSE::log::error("SkyrimBridge: Failed to get back buffer");
            return false;
        }

        hr = s_device->CreateRenderTargetView(backBuffer, nullptr, &s_renderTargetView);
        backBuffer->Release();

        if (FAILED(hr)) {
            SKSE::log::error("SkyrimBridge: Failed to create render target view");
            return false;
        }

        return true;
    }

    // ── Hooked Present ───────────────────────────────────────────────────
    static HRESULT __stdcall HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
    {
        // First-time ImGui initialization
        if (!s_imguiInitialized && s_device && s_context && s_gameWindow) {
            if (CreateRenderTarget() && InitImGui()) {
                SKSE::log::info("SkyrimBridge: ImGui initialized in Present hook");
            }
        }

        // Render ImGui if visible
        if (s_guiVisible && s_imguiInitialized && s_renderTargetView) {
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            // Render our debug GUI
            SB::DebugGUI::Render();

            ImGui::Render();

            s_context->OMSetRenderTargets(1, &s_renderTargetView, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
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

        return s_originalPresent(swapChain, syncInterval, flags);
    }

    // ── Hook the game's actual swap chain ────────────────────────────────
    static bool HookGameSwapChain()
    {
        // Get the renderer from Skyrim
        auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
        if (!renderer) {
            SKSE::log::error("SkyrimBridge: BSGraphics::Renderer not available");
            return false;
        }

        // Get the swap chain from the first render window
        auto& rendererData = renderer->data;
        s_swapChain = rendererData.renderWindows[0].swapChain;

        if (!s_swapChain) {
            SKSE::log::error("SkyrimBridge: SwapChain is null");
            return false;
        }

        // Get device from renderer (forwarder is the D3D11 device)
        s_device = rendererData.forwarder;
        if (!s_device) {
            SKSE::log::error("SkyrimBridge: D3D11 device is null");
            return false;
        }

        // Get context from renderer
        s_context = rendererData.context;
        if (!s_context) {
            SKSE::log::error("SkyrimBridge: D3D11 context is null");
            return false;
        }

        // Get game window from renderer window
        s_gameWindow = reinterpret_cast<HWND>(rendererData.renderWindows[0].hWnd);

        if (!s_gameWindow) {
            SKSE::log::error("SkyrimBridge: Game window is null");
            return false;
        }

        SKSE::log::info("SkyrimBridge: Got game swap chain, device, and window");

        // Hook the window procedure for input
        s_originalWndProc = (WNDPROC)SetWindowLongPtrA(s_gameWindow, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
        if (!s_originalWndProc) {
            SKSE::log::error("SkyrimBridge: Failed to hook window procedure");
            return false;
        }

        SKSE::log::info("SkyrimBridge: Window procedure hooked");

        // Get the swap chain's vtable and hook Present
        s_swapChainVTable = *reinterpret_cast<void***>(s_swapChain);
        s_originalPresent = reinterpret_cast<PresentFn>(s_swapChainVTable[8]);

        // Modify vtable entry for Present (index 8)
        DWORD oldProtect;
        if (VirtualProtect(&s_swapChainVTable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            s_swapChainVTable[8] = reinterpret_cast<void*>(&HookedPresent);
            VirtualProtect(&s_swapChainVTable[8], sizeof(void*), oldProtect, &oldProtect);
            SKSE::log::info("SkyrimBridge: Present hook installed via vtable");
        } else {
            SKSE::log::error("SkyrimBridge: VirtualProtect failed");
            return false;
        }

        return true;
    }

    // ── Public Interface ─────────────────────────────────────────────────
    bool Init()
    {
        if (s_initialized)
            return true;

        if (!HookGameSwapChain()) {
            SKSE::log::error("SkyrimBridge: Failed to hook game swap chain");
            return false;
        }

        s_initialized = true;
        SKSE::log::info("SkyrimBridge: D3D11 hook initialized - press INSERT to toggle GUI");
        return true;
    }

    void Shutdown()
    {
        if (!s_initialized)
            return;

        // Restore original Present
        if (s_swapChainVTable && s_originalPresent) {
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
    }

    void ToggleGUI()
    {
        s_guiVisible = !s_guiVisible;
        SKSE::log::info("SkyrimBridge: GUI toggled {}", s_guiVisible ? "ON" : "OFF");
    }

    bool IsGUIVisible()
    {
        return s_guiVisible;
    }

    void SetGUIVisible(bool a_visible)
    {
        s_guiVisible = a_visible;
    }

    ID3D11Device* GetDevice() { return s_device; }
    ID3D11DeviceContext* GetContext() { return s_context; }
    IDXGISwapChain* GetSwapChain() { return s_swapChain; }
}
