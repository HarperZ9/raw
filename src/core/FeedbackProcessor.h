#pragma once
//=============================================================================
//  FeedbackProcessor.h — ENB feedback loop: read-back computed values
//
//  After ENB renders each frame, reads scene luminance from the backbuffer
//  and distributes it to the next frame's constant buffer as SB_Computed_*.
//
//  Data flow (1-frame delay):
//    Frame N:  OnENBFrame -> push SB_ params (game state + feedback from N-1)
//              ENB renders -> shaders use SB_ params
//              HookedPresent -> CollectFeedback() reads backbuffer 5x5 grid
//    Frame N+1: DistributeFeedback() merges results into AllData
//              ...cycle repeats...
//=============================================================================

#include "BridgeData.h"
#include <filesystem>
#include <fstream>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct IDXGISwapChain;
enum DXGI_FORMAT;

namespace SB
{
    // ── ENB readback slot: one parameter read via ENBGetParameter ─────────
    inline constexpr int kMaxReadbackSlots = 16;

    struct ENBReadbackSlot
    {
        char  shader[64]{};       // e.g., "enbeffect.fx"
        char  paramName[64]{};    // UIName annotation string
        int   dataSize = 0;       // 4 (float) or 16 (float4)
        float data[4]{};          // result storage (zeroed if invalid)
        bool  valid = false;      // true after successful ENBGetParameter call
        bool  loggedFailure = false; // prevents per-frame log spam
    };

    // ── Configurable feedback parameters ─────────────────────────────────
    struct FeedbackConfig
    {
        // EMA smoothing alphas
        float centerLumAlpha    = 0.05f;   // center luminance
        float sceneAvgAlpha     = 0.03f;   // scene average luminance
        float sceneColorAlpha   = 0.08f;   // scene average color

        // Temporal analysis
        float lumVelocityAlpha  = 0.10f;   // luminance velocity smoothing
        float colorShiftAlpha   = 0.10f;   // color shift smoothing
        float sceneCutThreshold = 0.30f;   // abs(lumDelta) > this = scene cut
        float stabilityWindow   = 300.0f;  // max samples for variance accumulator
    };

    class FeedbackProcessor
    {
    public:
        static FeedbackProcessor& Get();

        // One-time init: creates staging textures for backbuffer readback.
        // Call from D3D11Hook after device/swapchain are available.
        bool Initialize(ID3D11Device* device, IDXGISwapChain* swapChain);

        // Called from HookedPresent BEFORE ImGui/overlay rendering.
        // Reads center-pixel and 5x5 grid luminance from the post-processed backbuffer.
        void CollectFeedback(ID3D11DeviceContext* context, IDXGISwapChain* swapChain);

        // Called from OnENBFrame to merge last frame's feedback into AllData.
        void DistributeFeedback(AllData& data);

        // Load feedback configuration from INI file.
        void LoadConfig(const std::filesystem::path& configDir);

        // Read ENB parameters via ENBGetParameter (INI-configured slots).
        void ReadENBParameters();

        bool IsInitialized() const { return m_initialized; }

        // ENB readback slot accessors (for DebugGUI)
        int GetReadbackSlotCount() const { return m_readbackSlotCount; }
        const ENBReadbackSlot& GetReadbackSlot(int index) const { return m_readbackSlots[index]; }

    private:
        FeedbackProcessor() = default;

        // Parse [ENBReadback] section from INI
        void ParseReadbackConfig(std::ifstream& file);

        bool m_initialized = false;
        ID3D11Texture2D* m_stagingTexture = nullptr;       // 1x1 for center pixel
        ID3D11Texture2D* m_gridStagingTexture = nullptr;   // 25x1 for 5x5 grid sampling
        int m_backbufferFormat = 0;  // DXGI_FORMAT stored as int to avoid header
        uint32_t m_backbufferWidth = 0;
        uint32_t m_backbufferHeight = 0;

        // Double buffer: write to m_feedback[m_writeIndex],
        // read from m_feedback[1 - m_writeIndex]
        FeedbackData m_feedback[2]{};
        int m_writeIndex = 0;
        bool m_hasData = false;

        // ── EMA state ────────────────────────────────────────────────────
        float m_emaLuminance = 0.0f;
        float m_emaSceneAvgLum = 0.0f;
        float m_emaSceneAvgR = 0.0f;
        float m_emaSceneAvgG = 0.0f;
        float m_emaSceneAvgB = 0.0f;
        bool m_emaInitialized = false;

        // ── Temporal tracking state ──────────────────────────────────────
        float m_prevLuminance = 0.0f;
        float m_prevAvgR = 0.0f;
        float m_prevAvgG = 0.0f;
        float m_prevAvgB = 0.0f;
        float m_emaLumVelocity = 0.0f;
        float m_emaColorShift = 0.0f;

        // Welford's online variance (for stability score)
        float m_varianceM2 = 0.0f;
        float m_varianceMean = 0.0f;
        int   m_varianceCount = 0;

        // ── ENB readback slots ───────────────────────────────────────────
        ENBReadbackSlot m_readbackSlots[kMaxReadbackSlots]{};
        int m_readbackSlotCount = 0;

        // Configurable smoothing parameters
        FeedbackConfig m_config;
    };
}
