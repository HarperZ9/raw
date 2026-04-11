#pragma once
//=============================================================================
//  RenderInspector.h — Domain R: Runtime rendering engine inspector
//
//  Single-frame capture of the full D3D11 rendering pipeline:
//    - Draw calls with BSShader context (type, technique, material)
//    - Constant buffer contents (staging copy + readback)
//    - Shader bytecode (from DXBCPatcher's bytecodeStore)
//    - Scene graph dump (NiNode tree → BSShaderProperty → materials)
//
//  State machine: Idle → Armed (F12) → Capturing (next frame) → Writing → Idle
//  Zero overhead when Idle — all callbacks early-return.
//  Piggybacks on existing DXBCPatcher/MaterialTracker/D3D11Hook hooks.
//
//  Output: Data/SKSE/Plugins/SkyrimBridge/Captures/Frame_N_timestamp/
//    - draw_calls.json, summary.json
//    - scene_graph.json
//    - shaders/PS_HASH.dxbc + PS_HASH.asm
//
//  Author: Zain Dana Harper
//=============================================================================

#include <cstdint>
#include <vector>
#include <string>
#include <array>
#include <filesystem>
#include <atomic>

struct ID3D11DeviceContext;
struct ID3D11PixelShader;
struct ID3D11Buffer;

namespace RE { class BSShader; }

namespace SB
{
    enum class InspectorState : uint8_t
    {
        Idle,       // No capture in progress, zero overhead
        Armed,      // F12 pressed, will start capturing next BeginFrame
        Capturing,  // Recording draw calls this frame
        Writing     // Writing captured data to disk
    };

    class RenderInspector
    {
    public:
        static RenderInspector& Get();

        // ── State control ─────────────────────────────────────────────
        void Arm();             // F12 → Armed
        void BeginFrame();      // Top of OnENBFrame: Armed→Capturing
        void EndFrame();        // Bottom of OnENBFrame: Capturing→Writing→Idle

        bool IsCapturing() const { return m_state == InspectorState::Capturing; }
        InspectorState GetState() const { return m_state; }
        uint32_t GetLastCaptureDrawCount() const { return m_lastCaptureDrawCount; }

        // ── Callbacks from existing hooks (state-gated) ───────────────
        void OnDrawIndexed(ID3D11DeviceContext* ctx, uint32_t indexCount,
                           uint32_t startIndex, int32_t baseVertex);
        void OnPSSetShader(ID3D11PixelShader* ps);
        void OnPSSetConstantBuffers(ID3D11DeviceContext* ctx, uint32_t startSlot,
                                     uint32_t numBuffers, ID3D11Buffer* const* buffers);
        void OnBeginTechnique(RE::BSShader* shader, uint32_t technique);

    private:
        RenderInspector() = default;

        // ── Per-draw constant buffer snapshot ─────────────────────────
        struct CBBinding {
            uint32_t slot;
            uint32_t byteWidth;
            std::vector<uint8_t> contents;
        };

        // ── Per-draw SRV info ─────────────────────────────────────────
        struct SRVBinding {
            uint32_t slot;
            uint32_t format;  // DXGI_FORMAT as uint
            uint32_t width;
            uint32_t height;
        };

        // ── Per-draw-call record ──────────────────────────────────────
        struct DrawCallRecord {
            uint32_t drawIndex;
            uint32_t indexCount;
            // BSShader context
            uint8_t  shaderType;     // BSShader::Type
            uint32_t technique;
            uint8_t  materialType;   // from MaterialTracker
            // PS info
            ID3D11PixelShader* pixelShader;
            uint32_t bytecodeHash;
            // CB bindings
            std::vector<CBBinding> constantBuffers;
        };

        // ── Scene graph node ──────────────────────────────────────────
        struct SceneNodeRecord {
            std::string name;
            std::string typeName;
            float worldTranslate[3];
            float worldScale;
            // BSGeometry material data
            bool hasMaterial;
            std::string materialTypeName;
            float specularColor[3];
            float specularPower;
            float materialAlpha;
            std::array<std::string, 9> texturePaths; // max 9 texture slots
            uint64_t shaderFlags;
            // Children
            std::vector<SceneNodeRecord> children;
        };

        // ── Internal methods ──────────────────────────────────────────
        void CaptureCBContents(ID3D11DeviceContext* ctx, ID3D11Buffer* buffer, CBBinding& out);
        void CaptureSceneGraph();
        void WriteToDisk();
        void WriteDrawCallsJSON(const std::filesystem::path& dir);
        void WriteSummaryJSON(const std::filesystem::path& dir);
        void WriteShaderBytecode(const std::filesystem::path& dir);
        void WriteSceneGraphJSON(const std::filesystem::path& dir);

        // JSON helpers
        std::string CBContentsToHex(const std::vector<uint8_t>& data, uint32_t maxBytes = 256);
        void WriteNodeJSON(std::ofstream& out, const SceneNodeRecord& node, int indent);

        // ── State ─────────────────────────────────────────────────────
        InspectorState m_state = InspectorState::Idle;
        uint32_t m_frameNumber = 0;
        uint32_t m_lastCaptureDrawCount = 0;

        // Current PS tracking (updated by OnPSSetShader)
        ID3D11PixelShader* m_currentPS = nullptr;

        // Current BSShader context (updated by OnBeginTechnique)
        uint8_t  m_currentShaderType = 0;
        uint32_t m_currentTechnique = 0;

        // Currently bound CBs (updated by OnPSSetConstantBuffers)
        static constexpr uint32_t kMaxCBSlots = 16;
        ID3D11Buffer* m_boundCBs[kMaxCBSlots] = {};

        // Per-frame capture buffers
        std::vector<DrawCallRecord> m_drawCalls;
        SceneNodeRecord m_sceneRoot;
    };
}
