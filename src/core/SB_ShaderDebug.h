#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  Playground — Shader Compilation Diagnostics (SB_ShaderDebug)
//
//  Hooks D3DCompile to intercept all ENB/game shader compilation.
//  On failure, captures the error blob, parses it into structured records
//  (file, line, column, error code, message), writes a formatted log file,
//  and renders a persistent in-game overlay showing all errors.
//
//  Usage (integrated with D3D11Hook):
//    1. Call SB::Debug::ShaderDebug::Get().Install(device, context, swapChain)
//       after D3D11 device creation (in your SKSE plugin init).
//    2. Errors are captured automatically via D3DCompile IAT hooks.
//    3. Call ProcessInput() and RenderOverlay() from D3D11Hook's Present.
//    4. Press the configurable hotkey (default: F10) to toggle the overlay.
//    5. Errors are written to Data/SKSE/Plugins/Playground_ShaderErrors.log
//
//  Requirements:
//    - d3dcompiler_47.dll must be loadable (ships with ENB and Windows 10+)
//    - D3D11 device access (for overlay rendering)
//    - SKSE for logging (optional, falls back to OutputDebugString)
//
//  Author: Zain Dana Harper
//  License: Same as Playground
// ═══════════════════════════════════════════════════════════════════════════

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <filesystem>
#include <unordered_set>

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

namespace SB::Debug
{
    // ═════════════════════════════════════════════════════════════════════
    //  Structured shader error record
    // ═════════════════════════════════════════════════════════════════════

    enum class Severity : uint8_t
    {
        Error   = 0,   // Compilation failed
        Warning = 1,   // Compiled with warnings
        Info    = 2    // Informational (e.g., "shader compiled successfully after retry")
    };

    struct ShaderError
    {
        // Parsed from D3DCompile error blob
        std::string     filename;       // Source file (.fx / .fxh), may be empty for memory shaders
        int             line     = -1;  // 1-based line number, -1 if unknown
        int             column   = -1;  // 1-based column, -1 if unknown
        std::string     errorCode;      // e.g., "X3004", "X3018", "X4000"
        std::string     message;        // Human-readable error description

        Severity        severity = Severity::Error;

        // Context: what ENB was trying to compile
        std::string     shaderProfile;  // e.g., "ps_5_0", "vs_5_0"
        std::string     entryPoint;     // e.g., "PS_Draw", "VS_Main"
        std::string     sourceHint;     // Inferred source .fx file (from ENB naming conventions)

        // Timing
        std::chrono::system_clock::time_point timestamp;

        // Raw error blob text (for full context)
        std::string     rawBlobText;
    };

    // ═════════════════════════════════════════════════════════════════════
    //  Compilation attempt record (groups errors per compilation call)
    // ═════════════════════════════════════════════════════════════════════

    struct CompilationAttempt
    {
        std::string     sourceFile;     // Best-guess source filename
        std::string     entryPoint;
        std::string     profile;
        bool            succeeded = false;
        std::string     rawErrorBlob;   // Full text from ID3DBlob

        std::vector<ShaderError> errors;   // Parsed individual errors
        std::vector<ShaderError> warnings; // Parsed individual warnings

        std::chrono::system_clock::time_point timestamp;
        double          compileTimeMs = 0.0; // How long the compilation took

        // Source snippet: the lines around each error for context
        struct SourceSnippet
        {
            int         startLine;
            int         endLine;
            std::vector<std::string> lines;
            int         errorLine; // Which line in this snippet is the error
        };
        std::vector<SourceSnippet> snippets;
    };

    // ═════════════════════════════════════════════════════════════════════
    //  Overlay configuration
    // ═════════════════════════════════════════════════════════════════════

    struct OverlayConfig
    {
        // Hotkey
        int     toggleKey       = VK_F10;        // Key to toggle overlay visibility
        int     clearKey        = VK_F11;        // Key to clear all errors
        int     scrollUpKey     = VK_PRIOR;      // Page Up
        int     scrollDownKey   = VK_NEXT;       // Page Down

        // Appearance
        float   bgAlpha         = 0.88f;         // Background opacity
        float   panelX          = 0.01f;         // Left edge (0-1 screen fraction)
        float   panelY          = 0.01f;         // Top edge
        float   panelW          = 0.98f;         // Width
        float   panelH          = 0.55f;         // Max height (scrollable if more errors)
        float   fontSize        = 12.0f;         // In pixels (approximate)

        // Colors (RGBA float)
        float   colorBg[4]          = { 0.08f, 0.08f, 0.12f, 0.88f };
        float   colorHeaderBg[4]    = { 0.60f, 0.15f, 0.15f, 0.95f };
        float   colorError[4]       = { 1.00f, 0.35f, 0.30f, 1.00f };
        float   colorWarning[4]     = { 1.00f, 0.85f, 0.30f, 1.00f };
        float   colorInfo[4]        = { 0.50f, 0.80f, 1.00f, 1.00f };
        float   colorFilename[4]    = { 0.40f, 0.90f, 0.60f, 1.00f };
        float   colorLineNum[4]     = { 0.85f, 0.85f, 0.50f, 1.00f };
        float   colorCode[4]        = { 0.90f, 0.55f, 1.00f, 1.00f };
        float   colorText[4]        = { 0.90f, 0.90f, 0.90f, 1.00f };
        float   colorSourceLine[4]  = { 0.70f, 0.70f, 0.70f, 1.00f };
        float   colorSourceErr[4]   = { 1.00f, 0.30f, 0.30f, 0.25f }; // Error line bg highlight

        // Behavior
        bool    autoShow        = true;          // Auto-show on first error
        bool    persistLog      = true;          // Write log file
        int     maxErrors       = 200;           // Discard oldest past this
        bool    showSourceSnippets = true;        // Show code context around errors
        int     snippetRadius   = 3;             // Lines above/below error to show
    };

    // ═════════════════════════════════════════════════════════════════════
    //  ShaderDebug — singleton manager
    // ═════════════════════════════════════════════════════════════════════

    class ShaderDebug
    {
    public:
        static ShaderDebug& Get()
        {
            static ShaderDebug inst;
            return inst;
        }

        // ─── Lifecycle ──────────────────────────────────────────────────

        // Phase 1: Install D3DCompile IAT hooks as early as possible.
        // Call from SKSEPlugin_Load — no D3D11 device needed.
        // This captures all shader compilations including ENB's early ones.
        void InstallHooksEarly();

        // Phase 2: Provide D3D11 resources for overlay rendering.
        // Call after D3D11 device is available (kDataLoaded).
        void Install(ID3D11Device* device, ID3D11DeviceContext* context,
                     IDXGISwapChain* swapChain);

        void Shutdown();
        bool IsInstalled() const { return m_installed; }
        bool AreHooksInstalled() const { return m_hooksInstalled; }

        // ─── Error queries ──────────────────────────────────────────────

        bool HasErrors() const;
        bool HasWarnings() const;
        size_t ErrorCount() const;
        size_t WarningCount() const;
        size_t TotalAttempts() const;

        const std::vector<CompilationAttempt>& GetAttempts() const { return m_attempts; }

        // Get only failed compilations
        std::vector<const CompilationAttempt*> GetFailedAttempts() const;

        // Clear all recorded errors/warnings
        void ClearAll();

        // Get cached source for a compilation (by source filename)
        std::string GetCachedSource(const std::string& sourceFile) const;

        // ─── Overlay control ────────────────────────────────────────────

        void SetOverlayVisible(bool visible);
        void ToggleOverlay();
        bool IsOverlayVisible() const { return m_overlayVisible; }

        void ScrollUp();
        void ScrollDown();
        void ScrollToTop();
        void ScrollToBottom();

        // ─── Configuration ──────────────────────────────────────────────

        OverlayConfig& Config() { return m_config; }
        const OverlayConfig& Config() const { return m_config; }

        // ─── Log file ───────────────────────────────────────────────────

        // Force write current errors to log file (normally auto-written on each error)
        void FlushLog();

        // Get the log file path
        const std::filesystem::path& LogPath() const { return m_logPath; }

        // ─── Shader source capture to disk ───────────────────────────────

        void SetCaptureEnabled(bool enabled) { m_captureEnabled = enabled; }
        bool IsCaptureEnabled() const { return m_captureEnabled; }
        void SetCapturePath(const std::filesystem::path& dir) { m_capturePath = dir; }
        const std::filesystem::path& CapturePath() const { return m_capturePath; }
        int  CapturedCount() const { return m_capturedCount; }

        // ─── Overlay rendering (called from D3D11Hook::HookedPresent) ──

        void InitOverlayResources();
        void RenderOverlay();
        void ProcessInput();
        bool IsOverlayInited() const { return m_overlayInited; }

    private:
        ShaderDebug() = default;
        ~ShaderDebug() = default;

        // ─── D3DCompile hook ────────────────────────────────────────────

        using fnD3DCompile = HRESULT(WINAPI*)(
            LPCVOID pSrcData, SIZE_T SrcDataSize,
            LPCSTR pSourceName, const D3D_SHADER_MACRO* pDefines,
            ID3DInclude* pInclude, LPCSTR pEntrypoint,
            LPCSTR pTarget, UINT Flags1, UINT Flags2,
            ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs);

        static fnD3DCompile s_origD3DCompile;

        static HRESULT WINAPI HookD3DCompile(
            LPCVOID pSrcData, SIZE_T SrcDataSize,
            LPCSTR pSourceName, const D3D_SHADER_MACRO* pDefines,
            ID3DInclude* pInclude, LPCSTR pEntrypoint,
            LPCSTR pTarget, UINT Flags1, UINT Flags2,
            ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs);

        using fnD3DCompile2 = HRESULT(WINAPI*)(
            LPCVOID pSrcData, SIZE_T SrcDataSize,
            LPCSTR pSourceName, const D3D_SHADER_MACRO* pDefines,
            ID3DInclude* pInclude, LPCSTR pEntrypoint,
            LPCSTR pTarget, UINT Flags1, UINT Flags2,
            UINT SecondaryDataFlags, LPCVOID pSecondaryData,
            SIZE_T SecondaryDataSize,
            ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs);

        static fnD3DCompile2 s_origD3DCompile2;

        static HRESULT WINAPI HookD3DCompile2(
            LPCVOID pSrcData, SIZE_T SrcDataSize,
            LPCSTR pSourceName, const D3D_SHADER_MACRO* pDefines,
            ID3DInclude* pInclude, LPCSTR pEntrypoint,
            LPCSTR pTarget, UINT Flags1, UINT Flags2,
            UINT SecondaryDataFlags, LPCVOID pSecondaryData,
            SIZE_T SecondaryDataSize,
            ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs);

        // ─── Error parsing ──────────────────────────────────────────────

        // Parse D3DCompile error blob into structured ShaderError records
        void ParseErrorBlob(const std::string& blobText,
                            const std::string& sourceName,
                            const std::string& entryPoint,
                            const std::string& profile,
                            CompilationAttempt& outAttempt);

        // Parse a single error line from the blob
        // Format: "filename(line,col-col): error XNNNN: message"
        // or:     "(line,col): error XNNNN: message"
        // or:     "error XNNNN: message"
        ShaderError ParseSingleError(const std::string& line,
                                     const std::string& defaultFilename);

        // Try to extract source code snippet around error line
        CompilationAttempt::SourceSnippet ExtractSnippet(
            const std::string& sourceCode,
            int errorLine, int radius);

        // Infer the source .fx file from ENB naming conventions
        std::string InferSourceFile(const std::string& sourceName,
                                    const std::string& entryPoint);

        // ─── Log writing ────────────────────────────────────────────────

        void WriteLogEntry(const CompilationAttempt& attempt);
        void WriteLogHeader();
        void EnsureLogDirectory();

        // ─── Record compilation (called by hooks) ─────────────────────

        void RecordCompilation(HRESULT hr, LPCVOID pSrcData, SIZE_T srcSize,
                               LPCSTR pSourceName, LPCSTR pEntrypoint,
                               LPCSTR pTarget, ID3DBlob** ppErrorMsgs,
                               double elapsedMs);

        // Write captured shader source + DXBC to disk
        void CaptureShaderToDisk(LPCSTR pSourceName, LPCSTR pEntrypoint,
                                 LPCSTR pTarget, LPCVOID pSrcData,
                                 SIZE_T srcSize, ID3DBlob* pCode);

        // ─── Overlay rendering (internal helpers) ────────────────────

        void RenderErrorPanel();

        // Simple bitmap font rendering (self-contained, no external deps)
        void InitFontAtlas();
        void DrawText(float x, float y, const std::string& text,
                      const float color[4], float scale = 1.0f);
        void DrawRect(float x, float y, float w, float h, const float color[4]);

        // ─── Input handling (internal) ───────────────────────────────

        bool IsKeyPressed(int vk);
        bool WasKeyJustPressed(int vk);

        // ─── State ──────────────────────────────────────────────────────

        bool m_installed      = false;
        bool m_hooksInstalled = false;

        // D3D11 resources
        ID3D11Device*              m_device  = nullptr;
        ID3D11DeviceContext*       m_context = nullptr;
        IDXGISwapChain*            m_swapChain = nullptr;

        // Overlay GPU resources
        Microsoft::WRL::ComPtr<ID3D11VertexShader>      m_overlayVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>       m_overlayPS;
        Microsoft::WRL::ComPtr<ID3D11Buffer>            m_overlayVB;
        Microsoft::WRL::ComPtr<ID3D11Buffer>            m_overlayCB;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>       m_overlayLayout;
        Microsoft::WRL::ComPtr<ID3D11BlendState>        m_overlayBlend;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_overlayRaster;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_overlayDepthState;
        Microsoft::WRL::ComPtr<ID3D11SamplerState>      m_overlaySampler;

        // Font atlas
        Microsoft::WRL::ComPtr<ID3D11Texture2D>          m_fontTex;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_fontSRV;
        int m_fontAtlasW = 0;
        int m_fontAtlasH = 0;
        int m_glyphW     = 0;
        int m_glyphH     = 0;
        static constexpr int kFontCharsPerRow = 16;
        static constexpr int kFontFirstChar   = 32;  // ASCII space
        static constexpr int kFontLastChar    = 126;  // ASCII tilde

        // Error storage
        std::vector<CompilationAttempt> m_attempts;
        mutable std::mutex              m_errorMtx;

        // Counters
        size_t m_totalErrors   = 0;
        size_t m_totalWarnings = 0;
        size_t m_totalSuccess  = 0;

        // Source code cache (for snippet extraction)
        // Key: filename, Value: file contents (read on first error in that file)
        std::unordered_map<std::string, std::string> m_sourceCache;
        mutable std::mutex m_sourceCacheMtx;

        // Overlay state
        bool    m_overlayVisible   = false;
        int     m_scrollOffset     = 0;
        int     m_maxVisibleLines  = 0;  // Computed from panel height / font size
        bool    m_overlayInited    = false;

        // Input state
        bool    m_keyStates[256]   = {};
        bool    m_prevKeyStates[256] = {};

        // Configuration
        OverlayConfig m_config;

        // Log file
        std::filesystem::path m_logPath;
        bool                  m_logHeaderWritten = false;

        // Shader source capture
        bool                  m_captureEnabled = true;
        std::filesystem::path m_capturePath;
        int                   m_capturedCount  = 0;
        std::unordered_set<uint64_t> m_capturedHashes;  // Avoid duplicate captures

        // Frame counter (for overlay animation / timing)
        uint64_t m_frameCount = 0;

        // Tracked shader source files (for hot-reload detection)
        struct FileWatch
        {
            std::filesystem::path path;
            std::filesystem::file_time_type lastWrite;
        };
        std::vector<FileWatch> m_watchedFiles;
    };

} // namespace SB::Debug
