#pragma once
//=============================================================================
//  DebugRenderer -- 3D wireframe / line overlay for debug visualization
//
//  Accumulates line segments (world-space positions + per-vertex ABGR colors)
//  during the frame, then renders them all in one draw call at PrePresent.
//
//  Provides helpers for common primitives: lines, triangles, boxes, spheres,
//  and bone links (diamond/octahedron joint markers).
//
//  Usage:
//    auto& dr = SB::DebugRenderer::Get();
//    dr.SetViewProjection(vp);
//    dr.DrawLine(p0, p1, DebugRenderer::MakeColor(255, 0, 0));
//    dr.DrawBox(center, extents, DebugRenderer::MakeColor(0, 255, 0));
//    dr.DrawSphere(center, 50.f, DebugRenderer::MakeColor(0, 255, 255));
//    dr.Flush(ctx);
//=============================================================================

#include <d3d11.h>
#include <DirectXMath.h>
#include <cstdint>
#include <vector>
#include <string>

namespace SB
{

// ---- Vertex format ----------------------------------------------------------

struct DebugVertex
{
    float    x, y, z;   // World position
    uint32_t color;     // ABGR packed color
};


// ---- DebugRenderer ----------------------------------------------------------

class DebugRenderer
{
public:
    static DebugRenderer& Get()
    {
        static DebugRenderer instance;
        return instance;
    }

    // Initialize D3D11 resources (VS, PS, VB, input layout, blend/depth states)
    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx);
    bool IsInitialized() const { return m_initialized; }
    void Shutdown();

    // === Drawing API (accumulates vertices for current frame) ================

    void DrawLine(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, uint32_t color);
    void DrawTriangle(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b,
                      const DirectX::XMFLOAT3& c, uint32_t color);
    void DrawBox(const DirectX::XMFLOAT3& center, const DirectX::XMFLOAT3& extents, uint32_t color);
    void DrawSphere(const DirectX::XMFLOAT3& center, float radius, uint32_t color, int segments = 12);
    void DrawBone(const DirectX::XMFLOAT3& parent, const DirectX::XMFLOAT3& child, uint32_t color);

    // Convenience overloads accepting raw float[3] pointers (for NiPoint3, etc.)
    void DrawLine(const float* a, const float* b, uint32_t color);
    void DrawTriangle(const float* v0, const float* v1, const float* v2, uint32_t color);
    void DrawBox(const float* mn, const float* mx, uint32_t color);
    void DrawSphere(const float* center, float radius, uint32_t color, int segments = 12);
    void DrawBone(const float* parent, const float* child, uint32_t color);

    // Text label at world position (rendered via ImGui overlay, not 3D)
    void DrawLabel(const float* worldPos, const char* text, uint32_t color);

    // === Rendering (call during PrePresent, after game rendering) ============

    // Flush: uploads vertices to GPU, draws, clears buffer (uses stored context)
    void Flush();

    // === Settings ============================================================

    // Set the view-projection matrix (must be set each frame before Flush)
    void SetViewProjection(const DirectX::XMFLOAT4X4& viewProj);

    // Depth mode
    void SetDepthTestEnabled(bool enabled) { m_depthTest = enabled; }

    // Enable/disable all rendering
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const        { return m_enabled; }

    // === Stats ===============================================================

    uint32_t GetVertexCount()   const { return static_cast<uint32_t>(m_vertices.size()); }
    uint32_t GetDrawCallCount() const { return m_drawCalls; }
    uint32_t GetLineCount()     const { return static_cast<uint32_t>(m_vertices.size() / 2); }
    uint32_t GetLabelCount()    const { return static_cast<uint32_t>(m_labels.size()); }

    // === Color helpers (packs RGBA into uint32_t as ABGR) ====================

    static uint32_t MakeColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        return static_cast<uint32_t>(a) << 24 |
               static_cast<uint32_t>(b) << 16 |
               static_cast<uint32_t>(g) << 8  |
               static_cast<uint32_t>(r);
    }

    // Legacy alias
    static uint32_t ColorRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        return MakeColor(r, g, b, a);
    }

    // Named color constants (ABGR byte order)
    static constexpr uint32_t kRed     = 0xFF0000FF;  // A=FF B=00 G=00 R=FF
    static constexpr uint32_t kGreen   = 0xFF00FF00;  // A=FF B=00 G=FF R=00
    static constexpr uint32_t kBlue    = 0xFFFF0000;  // A=FF B=FF G=00 R=00
    static constexpr uint32_t kYellow  = 0xFF00FFFF;  // A=FF B=00 G=FF R=FF
    static constexpr uint32_t kCyan    = 0xFFFFFF00;  // A=FF B=FF G=FF R=00
    static constexpr uint32_t kMagenta = 0xFFFF00FF;  // A=FF B=FF G=00 R=FF
    static constexpr uint32_t kWhite   = 0xFFFFFFFF;
    static constexpr uint32_t kBlack   = 0xFF000000;

private:
    DebugRenderer() = default;

    // Embedded HLSL shaders (compiled at init time)
    bool CreateShaders();
    bool CreateResources();

    // ViewProj matrix reconstruction from NiCamera (fallback when not set externally)
    void UpdateViewProj();

    // Pipeline state save/restore
    void SavePipelineState();
    void RestorePipelineState();

    // Render 3D lines
    void RenderLines();

    // Render screen-space labels via ImGui
    void RenderLabels();

    // ---- State --------------------------------------------------------------

    bool m_initialized = false;
    bool m_enabled     = true;
    bool m_depthTest   = true;
    bool m_viewProjSet = false;  // true if SetViewProjection was called this frame

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Shaders
    ID3D11VertexShader*  m_vs          = nullptr;
    ID3D11PixelShader*   m_ps          = nullptr;
    ID3D11InputLayout*   m_inputLayout = nullptr;

    // GPU resources
    ID3D11Buffer*             m_vertexBuffer   = nullptr;
    ID3D11Buffer*             m_constantBuffer = nullptr;
    ID3D11DepthStencilState*  m_depthTestOn    = nullptr;
    ID3D11DepthStencilState*  m_depthTestOff   = nullptr;
    ID3D11BlendState*         m_blendState     = nullptr;
    ID3D11RasterizerState*    m_rasterizerState = nullptr;

    // Capacity
    static constexpr uint32_t kMaxVertices = 65536;

    // CPU-side vertex accumulator
    std::vector<DebugVertex> m_vertices;

    // ViewProj matrix (row-major float[16], 64 bytes -- matches cbuffer layout)
    float m_viewProj[16] = {};

    uint32_t m_drawCalls = 0;

    // Label accumulator
    struct LabelEntry
    {
        float       worldPos[3];
        std::string text;
        uint32_t    color;
    };
    std::vector<LabelEntry> m_labels;

    // Saved pipeline state (full D3D11 state machine)
    struct SavedState
    {
        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        ID3D11InputLayout*       inputLayout = nullptr;
        ID3D11Buffer*            vertexBuffers[4] = {};
        UINT                     vbStrides[4] = {};
        UINT                     vbOffsets[4] = {};

        ID3D11VertexShader*      vs = nullptr;
        ID3D11Buffer*            vsCBs[4] = {};
        ID3D11ClassInstance*     vsCI[256] = {};
        UINT                     vsCICount = 256;

        ID3D11RasterizerState*   rsState = nullptr;
        D3D11_VIEWPORT           viewports[4] = {};
        UINT                     viewportCount = 4;

        ID3D11PixelShader*       ps = nullptr;
        ID3D11ClassInstance*     psCI[256] = {};
        UINT                     psCICount = 256;
        ID3D11Buffer*            psCBs[4] = {};

        ID3D11RenderTargetView*  rtvs[8] = {};
        ID3D11DepthStencilView*  dsv = nullptr;
        ID3D11BlendState*        blendState = nullptr;
        float                    blendFactor[4] = {};
        UINT                     sampleMask = 0;
        ID3D11DepthStencilState* depthStencilState = nullptr;
        UINT                     stencilRef = 0;

        bool saved = false;

        void Release();
    };
    SavedState m_saved;
};

} // namespace SB
