//=============================================================================
//  DebugRenderer.cpp -- 3D wireframe / line overlay for debug visualization
//
//  Accumulates line segments during the frame, renders them in a single
//  draw call at PrePresent (priority 800), then clears for the next frame.
//  Text labels are projected to screen space and drawn via ImGui overlay.
//=============================================================================

#include "DebugRenderer.h"
#include "BridgeData.h"
#include "RenderPipeline.h"
#include "Projection.h"
#include "ShaderLoader.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <imgui.h>

#include <cstring>
#include <cmath>
#include <algorithm>

namespace SB
{

using namespace DirectX;

// =============================================================================
//  Embedded HLSL shaders
// =============================================================================

static const char kDebugVS[] = R"HLSL(
cbuffer CB : register(b0)
{
    float4x4 ViewProj;
};

struct VS_IN
{
    float3 pos   : POSITION;
    uint   color : COLOR;
};

struct VS_OUT
{
    float4 pos   : SV_Position;
    float4 color : COLOR;
};

VS_OUT main(VS_IN i)
{
    VS_OUT o;
    o.pos = mul(float4(i.pos, 1.0), ViewProj);
    o.color = float4(
        ((i.color >>  0) & 0xFF) / 255.0,
        ((i.color >>  8) & 0xFF) / 255.0,
        ((i.color >> 16) & 0xFF) / 255.0,
        ((i.color >> 24) & 0xFF) / 255.0);
    return o;
}
)HLSL";


static const char kDebugPS[] = R"HLSL(
struct PS_IN
{
    float4 pos   : SV_Position;
    float4 color : COLOR;
};

float4 main(PS_IN i) : SV_Target
{
    return i.color;
}
)HLSL";


// =============================================================================
//  Initialize
// =============================================================================

bool DebugRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx)
{
    if (m_initialized) return true;
    if (!dev || !ctx) return false;

    m_device  = dev;
    m_context = ctx;

    if (!CreateShaders()) {
        SKSE::log::error("DebugRenderer: failed to create shaders");
        return false;
    }

    if (!CreateResources()) {
        SKSE::log::error("DebugRenderer: failed to create GPU resources");
        Shutdown();
        return false;
    }

    m_vertices.reserve(kMaxVertices);

    // Register as a PrePresent pass in RenderPipeline (priority 800)
    auto& pipeline = RenderPipeline::Get();
    if (pipeline.IsInitialized()) {
        pipeline.AddPass({
            .name     = "DebugRenderer",
            .stage    = PipelineStage::PrePresent,
            .priority = 800,
            .enabled  = true,
            .execute  = [](PassContext&) {
                DebugRenderer::Get().Flush();
            },
        });
    }

    m_initialized = true;
    SKSE::log::info("DebugRenderer: initialized (maxVerts={}, pipeline pass registered)", kMaxVertices);
    return true;
}


// =============================================================================
//  Shutdown
// =============================================================================

void DebugRenderer::Shutdown()
{
    auto SafeRelease = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };

    SafeRelease(m_vs);
    SafeRelease(m_ps);
    SafeRelease(m_inputLayout);
    SafeRelease(m_vertexBuffer);
    SafeRelease(m_constantBuffer);
    SafeRelease(m_depthTestOn);
    SafeRelease(m_depthTestOff);
    SafeRelease(m_blendState);
    SafeRelease(m_rasterizerState);

    m_vertices.clear();
    m_labels.clear();
    m_saved.Release();
    m_initialized = false;
    m_drawCalls   = 0;
}


// =============================================================================
//  CreateShaders -- compile embedded VS/PS via D3DCompile
// =============================================================================

bool DebugRenderer::CreateShaders()
{
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    // ---- Vertex Shader ------------------------------------------------------

    ID3DBlob* vsBlob = ShaderLoader::Compile("Debug_VS", kDebugVS, "main", "vs_5_0", flags);
    if (!vsBlob) {
        SKSE::log::error("DebugRenderer: VS compile failed");
        return false;
    }

    HRESULT hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(),
                                       vsBlob->GetBufferSize(),
                                       nullptr, &m_vs);
    if (FAILED(hr)) {
        SKSE::log::error("DebugRenderer: CreateVertexShader failed (0x{:X})",
                         static_cast<uint32_t>(hr));
        vsBlob->Release();
        return false;
    }

    // ---- Input Layout (matches DebugVertex) ---------------------------------

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UINT,   0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = m_device->CreateInputLayout(layout, 2,
                                      vsBlob->GetBufferPointer(),
                                      vsBlob->GetBufferSize(),
                                      &m_inputLayout);
    vsBlob->Release();
    if (FAILED(hr)) {
        SKSE::log::error("DebugRenderer: CreateInputLayout failed (0x{:X})",
                         static_cast<uint32_t>(hr));
        return false;
    }

    // ---- Pixel Shader -------------------------------------------------------

    ID3DBlob* psBlob = ShaderLoader::Compile("Debug_PS", kDebugPS, "main", "ps_5_0", flags);
    if (!psBlob) {
        SKSE::log::error("DebugRenderer: PS compile failed");
        return false;
    }

    hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(),
                                      psBlob->GetBufferSize(),
                                      nullptr, &m_ps);
    psBlob->Release();
    if (FAILED(hr)) {
        SKSE::log::error("DebugRenderer: CreatePixelShader failed (0x{:X})",
                         static_cast<uint32_t>(hr));
        return false;
    }

    return true;
}


// =============================================================================
//  CreateResources -- VB, CB, depth-stencil states, blend, rasterizer
// =============================================================================

bool DebugRenderer::CreateResources()
{
    // ---- Dynamic vertex buffer (pre-allocated for kMaxVertices) --------------

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth      = kMaxVertices * static_cast<uint32_t>(sizeof(DebugVertex));
    vbDesc.Usage           = D3D11_USAGE_DYNAMIC;
    vbDesc.BindFlags       = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = m_device->CreateBuffer(&vbDesc, nullptr, &m_vertexBuffer);
    if (FAILED(hr)) {
        SKSE::log::error("DebugRenderer: CreateBuffer (VB) failed (0x{:X})",
                         static_cast<uint32_t>(hr));
        return false;
    }

    // ---- Constant buffer (ViewProj 4x4 = 64 bytes) -------------------------

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth      = 64;  // sizeof(float4x4)
    cbDesc.Usage           = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;

    hr = m_device->CreateBuffer(&cbDesc, nullptr, &m_constantBuffer);
    if (FAILED(hr)) {
        SKSE::log::error("DebugRenderer: CreateBuffer (CB) failed (0x{:X})",
                         static_cast<uint32_t>(hr));
        return false;
    }

    // ---- Depth-stencil state: depth test ON (read-only) ---------------------

    D3D11_DEPTH_STENCIL_DESC dsOn = {};
    dsOn.DepthEnable    = TRUE;
    dsOn.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;   // Read-only
    dsOn.DepthFunc      = D3D11_COMPARISON_LESS_EQUAL;
    dsOn.StencilEnable  = FALSE;

    hr = m_device->CreateDepthStencilState(&dsOn, &m_depthTestOn);
    if (FAILED(hr)) {
        SKSE::log::error("DebugRenderer: CreateDepthStencilState (on) failed");
        return false;
    }

    // ---- Depth-stencil state: depth test OFF --------------------------------

    D3D11_DEPTH_STENCIL_DESC dsOff = {};
    dsOff.DepthEnable   = FALSE;
    dsOff.StencilEnable = FALSE;

    hr = m_device->CreateDepthStencilState(&dsOff, &m_depthTestOff);
    if (FAILED(hr)) {
        SKSE::log::error("DebugRenderer: CreateDepthStencilState (off) failed");
        return false;
    }

    // ---- Alpha blend state (src alpha blending for translucent lines) -------

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable  = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable           = TRUE;
    blendDesc.RenderTarget[0].SrcBlend               = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend              = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp                = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha          = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha         = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha           = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask  = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = m_device->CreateBlendState(&blendDesc, &m_blendState);
    if (FAILED(hr)) {
        SKSE::log::error("DebugRenderer: CreateBlendState failed");
        return false;
    }

    // ---- Rasterizer state: no culling, wireframe fill -------------------------

    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode              = D3D11_FILL_WIREFRAME;
    rsDesc.CullMode              = D3D11_CULL_NONE;
    rsDesc.DepthClipEnable       = TRUE;
    rsDesc.AntialiasedLineEnable = TRUE;

    hr = m_device->CreateRasterizerState(&rsDesc, &m_rasterizerState);
    if (FAILED(hr)) {
        SKSE::log::error("DebugRenderer: CreateRasterizerState failed");
        return false;
    }

    return true;
}


// =============================================================================
//  Drawing API -- XMFLOAT3 overloads
// =============================================================================

void DebugRenderer::DrawLine(const XMFLOAT3& a, const XMFLOAT3& b, uint32_t color)
{
    if (!m_enabled || m_vertices.size() + 2 > kMaxVertices)
        return;

    m_vertices.push_back({ a.x, a.y, a.z, color });
    m_vertices.push_back({ b.x, b.y, b.z, color });
}


void DebugRenderer::DrawTriangle(const XMFLOAT3& a, const XMFLOAT3& b,
                                  const XMFLOAT3& c, uint32_t color)
{
    DrawLine(a, b, color);
    DrawLine(b, c, color);
    DrawLine(c, a, color);
}


void DebugRenderer::DrawBox(const XMFLOAT3& center, const XMFLOAT3& extents, uint32_t color)
{
    // 8 corners from center +/- extents
    float cx = center.x, cy = center.y, cz = center.z;
    float ex = extents.x, ey = extents.y, ez = extents.z;

    XMFLOAT3 c0 = { cx - ex, cy - ey, cz - ez };  // ---
    XMFLOAT3 c1 = { cx + ex, cy - ey, cz - ez };  // +--
    XMFLOAT3 c2 = { cx + ex, cy + ey, cz - ez };  // ++-
    XMFLOAT3 c3 = { cx - ex, cy + ey, cz - ez };  // -+-
    XMFLOAT3 c4 = { cx - ex, cy - ey, cz + ez };  // --+
    XMFLOAT3 c5 = { cx + ex, cy - ey, cz + ez };  // +-+
    XMFLOAT3 c6 = { cx + ex, cy + ey, cz + ez };  // +++
    XMFLOAT3 c7 = { cx - ex, cy + ey, cz + ez };  // -++

    // Bottom face (Z-min): 0-1-2-3
    DrawLine(c0, c1, color);
    DrawLine(c1, c2, color);
    DrawLine(c2, c3, color);
    DrawLine(c3, c0, color);

    // Top face (Z-max): 4-5-6-7
    DrawLine(c4, c5, color);
    DrawLine(c5, c6, color);
    DrawLine(c6, c7, color);
    DrawLine(c7, c4, color);

    // Vertical edges
    DrawLine(c0, c4, color);
    DrawLine(c1, c5, color);
    DrawLine(c2, c6, color);
    DrawLine(c3, c7, color);
}


void DebugRenderer::DrawSphere(const XMFLOAT3& center, float radius, uint32_t color,
                                int segments)
{
    if (segments < 4) segments = 4;
    if (segments > 64) segments = 64;

    const float step = 6.2831853f / static_cast<float>(segments);  // 2*PI / N

    // XY circle (around Z axis)
    for (int i = 0; i < segments; ++i) {
        float a0 = step * static_cast<float>(i);
        float a1 = step * static_cast<float>(i + 1);
        XMFLOAT3 p0 = { center.x + radius * std::cos(a0),
                         center.y + radius * std::sin(a0),
                         center.z };
        XMFLOAT3 p1 = { center.x + radius * std::cos(a1),
                         center.y + radius * std::sin(a1),
                         center.z };
        DrawLine(p0, p1, color);
    }

    // XZ circle (around Y axis)
    for (int i = 0; i < segments; ++i) {
        float a0 = step * static_cast<float>(i);
        float a1 = step * static_cast<float>(i + 1);
        XMFLOAT3 p0 = { center.x + radius * std::cos(a0),
                         center.y,
                         center.z + radius * std::sin(a0) };
        XMFLOAT3 p1 = { center.x + radius * std::cos(a1),
                         center.y,
                         center.z + radius * std::sin(a1) };
        DrawLine(p0, p1, color);
    }

    // YZ circle (around X axis)
    for (int i = 0; i < segments; ++i) {
        float a0 = step * static_cast<float>(i);
        float a1 = step * static_cast<float>(i + 1);
        XMFLOAT3 p0 = { center.x,
                         center.y + radius * std::cos(a0),
                         center.z + radius * std::sin(a0) };
        XMFLOAT3 p1 = { center.x,
                         center.y + radius * std::cos(a1),
                         center.z + radius * std::sin(a1) };
        DrawLine(p0, p1, color);
    }
}


void DebugRenderer::DrawBone(const XMFLOAT3& parent, const XMFLOAT3& child, uint32_t color)
{
    // Main bone line
    DrawLine(parent, child, color);

    // Diamond/octahedron at the child joint for visibility.
    float dx = child.x - parent.x;
    float dy = child.y - parent.y;
    float dz = child.z - parent.z;
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);

    if (len < 1e-4f) return;

    float dirX = dx / len;
    float dirY = dy / len;
    float dirZ = dz / len;

    // Pick a non-parallel reference vector for cross product
    float refX = 0.f, refY = 0.f, refZ = 1.f;
    if (std::abs(dirZ) > 0.9f) {
        refX = 1.f; refY = 0.f; refZ = 0.f;
    }

    // Cross: perp1 = dir x ref
    float p1x = dirY * refZ - dirZ * refY;
    float p1y = dirZ * refX - dirX * refZ;
    float p1z = dirX * refY - dirY * refX;
    float p1len = std::sqrt(p1x * p1x + p1y * p1y + p1z * p1z);
    if (p1len > 1e-6f) { p1x /= p1len; p1y /= p1len; p1z /= p1len; }

    // Cross: perp2 = dir x perp1
    float p2x = dirY * p1z - dirZ * p1y;
    float p2y = dirZ * p1x - dirX * p1z;
    float p2z = dirX * p1y - dirY * p1x;

    // Diamond size = 10% of bone length, clamped
    float size = len * 0.1f;
    if (size < 0.5f) size = 0.5f;
    if (size > 5.0f) size = 5.0f;

    // Diamond midpoint (25% along bone from child toward parent)
    float midFrac = 0.25f;
    float midX = child.x - dirX * len * midFrac;
    float midY = child.y - dirY * len * midFrac;
    float midZ = child.z - dirZ * len * midFrac;

    // 4 diamond points
    XMFLOAT3 d0 = { midX + p1x * size, midY + p1y * size, midZ + p1z * size };
    XMFLOAT3 d1 = { midX - p1x * size, midY - p1y * size, midZ - p1z * size };
    XMFLOAT3 d2 = { midX + p2x * size, midY + p2y * size, midZ + p2z * size };
    XMFLOAT3 d3 = { midX - p2x * size, midY - p2y * size, midZ - p2z * size };

    // Connect diamond to child joint
    DrawLine(child, d0, color);
    DrawLine(child, d1, color);
    DrawLine(child, d2, color);
    DrawLine(child, d3, color);

    // Connect diamond points (ring)
    DrawLine(d0, d2, color);
    DrawLine(d2, d1, color);
    DrawLine(d1, d3, color);
    DrawLine(d3, d0, color);
}


// =============================================================================
//  Drawing API -- raw float* overloads (for NiPoint3 compat)
// =============================================================================

void DebugRenderer::DrawLine(const float* a, const float* b, uint32_t color)
{
    DrawLine(XMFLOAT3{ a[0], a[1], a[2] }, XMFLOAT3{ b[0], b[1], b[2] }, color);
}


void DebugRenderer::DrawTriangle(const float* v0, const float* v1, const float* v2,
                                  uint32_t color)
{
    DrawTriangle(XMFLOAT3{ v0[0], v0[1], v0[2] },
                 XMFLOAT3{ v1[0], v1[1], v1[2] },
                 XMFLOAT3{ v2[0], v2[1], v2[2] }, color);
}


void DebugRenderer::DrawBox(const float* mn, const float* mx, uint32_t color)
{
    // float* overload interprets as min/max AABB corners (legacy behavior)
    // Convert to center+extents for the XMFLOAT3 overload
    XMFLOAT3 center = {
        (mn[0] + mx[0]) * 0.5f,
        (mn[1] + mx[1]) * 0.5f,
        (mn[2] + mx[2]) * 0.5f
    };
    XMFLOAT3 extents = {
        (mx[0] - mn[0]) * 0.5f,
        (mx[1] - mn[1]) * 0.5f,
        (mx[2] - mn[2]) * 0.5f
    };
    DrawBox(center, extents, color);
}


void DebugRenderer::DrawSphere(const float* center, float radius, uint32_t color,
                                int segments)
{
    DrawSphere(XMFLOAT3{ center[0], center[1], center[2] }, radius, color, segments);
}


void DebugRenderer::DrawBone(const float* parent, const float* child, uint32_t color)
{
    DrawBone(XMFLOAT3{ parent[0], parent[1], parent[2] },
             XMFLOAT3{ child[0], child[1], child[2] }, color);
}


void DebugRenderer::DrawLabel(const float* worldPos, const char* text, uint32_t color)
{
    if (!m_enabled || !text || !text[0]) return;

    LabelEntry entry;
    entry.worldPos[0] = worldPos[0];
    entry.worldPos[1] = worldPos[1];
    entry.worldPos[2] = worldPos[2];
    entry.text  = text;
    entry.color = color;
    m_labels.push_back(std::move(entry));
}


// =============================================================================
//  UpdateViewProj -- fallback when SetViewProjection not called this frame
// =============================================================================

void DebugRenderer::UpdateViewProj()
{
    auto* niCamera = RE::Main::WorldRootCamera();
    if (!niCamera) {
        std::memset(m_viewProj, 0, sizeof(m_viewProj));
        m_viewProj[0] = m_viewProj[5] = m_viewProj[10] = m_viewProj[15] = 1.f;
        return;
    }

    // Get the world-to-camera matrix from NiCamera
    const auto& camRT  = niCamera->GetRuntimeData();
    const auto& camRT2 = niCamera->GetRuntimeData2();

    // NiCamera::worldToCam is a 4x4 row-major matrix -- use it as our View matrix
    XMMATRIX view;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            view.r[row].m128_f32[col] = camRT.worldToCam[row][col];

    // Build Projection from NiCamera's viewFrustum planes.
    // This matches how CameraTracker reads near/far.
    float l = camRT2.viewFrustum.fLeft;
    float r = camRT2.viewFrustum.fRight;
    float t = camRT2.viewFrustum.fTop;
    float b = camRT2.viewFrustum.fBottom;
    float n = camRT2.viewFrustum.fNear;
    float f = camRT2.viewFrustum.fFar;

    // Off-center perspective projection (handles asymmetric frustums)
    XMMATRIX proj;
    if (std::abs(r - l) > 1e-6f && std::abs(t - b) > 1e-6f && std::abs(f - n) > 1e-6f) {
        proj = XMMatrixPerspectiveOffCenterLH(l, r, b, t, n, f);
    } else {
        proj = XMMatrixIdentity();
    }

    // ViewProj = View * Proj
    XMMATRIX vp = XMMatrixMultiply(view, proj);

    // Store as row-major float[16] for cbuffer upload
    XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(m_viewProj), vp);
}


// =============================================================================
//  SetViewProjection
// =============================================================================

void DebugRenderer::SetViewProjection(const DirectX::XMFLOAT4X4& viewProj)
{
    std::memcpy(m_viewProj, &viewProj, 64);
    m_viewProjSet = true;
}


// =============================================================================
//  Flush -- upload vertices, draw all lines, render labels, clear
// =============================================================================

void DebugRenderer::Flush()
{
    if (!m_initialized || !m_enabled)
        return;

    if (m_vertices.empty() && m_labels.empty())
        return;

    m_drawCalls = 0;

    // Render 3D lines
    if (!m_vertices.empty())
        RenderLines();

    // Render screen-space labels via ImGui
    if (!m_labels.empty())
        RenderLabels();

    // Clear for next frame
    m_vertices.clear();
    m_labels.clear();
    m_viewProjSet = false;
}


// =============================================================================
//  RenderLines -- upload VB, bind state, draw line list
// =============================================================================

void DebugRenderer::RenderLines()
{
    if (m_vertices.empty() || !m_context) return;

    // If ViewProj wasn't set externally this frame, reconstruct from NiCamera
    if (!m_viewProjSet) {
        UpdateViewProj();
    }

    // ---- Save full pipeline state -------------------------------------------
    SavePipelineState();

    // ---- Upload vertices to dynamic VB (in batches if needed) ---------------
    uint32_t totalVerts = static_cast<uint32_t>(m_vertices.size());
    uint32_t offset = 0;

    while (offset < totalVerts) {
        uint32_t batchCount = (std::min)(totalVerts - offset, kMaxVertices);

        // Map and copy vertex data
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = m_context->Map(m_vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) {
            SKSE::log::warn("DebugRenderer: VB map failed (0x{:X})", static_cast<uint32_t>(hr));
            break;
        }
        std::memcpy(mapped.pData, &m_vertices[offset], batchCount * sizeof(DebugVertex));
        m_context->Unmap(m_vertexBuffer, 0);

        // ---- Upload ViewProj to constant buffer -----------------------------
        hr = m_context->Map(m_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) {
            SKSE::log::warn("DebugRenderer: CB map failed");
            break;
        }
        std::memcpy(mapped.pData, m_viewProj, 64);
        m_context->Unmap(m_constantBuffer, 0);

        // ---- Bind pipeline state --------------------------------------------

        // Input Assembler
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        m_context->IASetInputLayout(m_inputLayout);
        UINT stride = static_cast<UINT>(sizeof(DebugVertex));
        UINT vbOff  = 0;
        m_context->IASetVertexBuffers(0, 1, &m_vertexBuffer, &stride, &vbOff);

        // Vertex Shader
        m_context->VSSetShader(m_vs, nullptr, 0);
        m_context->VSSetConstantBuffers(0, 1, &m_constantBuffer);

        // Rasterizer
        m_context->RSSetState(m_rasterizerState);
        // Viewport: use whatever is currently bound (the backbuffer viewport)

        // Pixel Shader
        m_context->PSSetShader(m_ps, nullptr, 0);

        // Output Merger -- keep current RTV, set our depth/blend state
        // We do NOT rebind RTVs; we draw onto whatever is currently bound
        // (the backbuffer, set up by the pipeline before our pass)
        float blendFactor[4] = { 1.f, 1.f, 1.f, 1.f };
        m_context->OMSetBlendState(m_blendState, blendFactor, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(
            m_depthTest ? m_depthTestOn : m_depthTestOff, 0);

        // ---- Draw -----------------------------------------------------------
        m_context->Draw(batchCount, 0);
        ++m_drawCalls;

        offset += batchCount;
    }

    // ---- Restore pipeline state ---------------------------------------------
    RestorePipelineState();
}


// =============================================================================
//  RenderLabels -- project world positions to screen, draw text via ImGui
// =============================================================================

void DebugRenderer::RenderLabels()
{
    auto* drawList = ImGui::GetBackgroundDrawList();
    if (!drawList) return;

    auto* niCamera = RE::Main::WorldRootCamera();
    if (!niCamera) return;

    const auto& camRT  = niCamera->GetRuntimeData();
    const auto& camRT2 = niCamera->GetRuntimeData2();

    for (const auto& label : m_labels) {
        RE::NiPoint3 wp{ label.worldPos[0], label.worldPos[1], label.worldPos[2] };

        float sx = 0.f, sy = 0.f, sz = 0.f;
        bool ok = RE::NiCamera::WorldPtToScreenPt3(
            camRT.worldToCam, camRT2.port, wp, sx, sy, sz, 1e-5f);

        if (!ok || sz <= 0.0f)
            continue;

        // Screen UV [0,1] -> pixel coordinates
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        float px = sx * displaySize.x;
        float py = sy * displaySize.y;

        // Convert ABGR color to ImGui's ABGR (same byte order)
        ImU32 imColor = static_cast<ImU32>(label.color);

        drawList->AddText(ImVec2(px, py), imColor, label.text.c_str());
    }
}


// =============================================================================
//  SavePipelineState / RestorePipelineState
//  Mirrors the pattern from RenderPassManager.
// =============================================================================

void DebugRenderer::SavePipelineState()
{
    auto& s = m_saved;

    // Input Assembler
    m_context->IAGetPrimitiveTopology(&s.topology);
    m_context->IAGetInputLayout(&s.inputLayout);
    m_context->IAGetVertexBuffers(0, 4, s.vertexBuffers, s.vbStrides, s.vbOffsets);

    // Vertex Shader
    s.vsCICount = 256;
    m_context->VSGetShader(&s.vs, s.vsCI, &s.vsCICount);
    m_context->VSGetConstantBuffers(0, 4, s.vsCBs);

    // Rasterizer
    m_context->RSGetState(&s.rsState);
    s.viewportCount = 4;
    m_context->RSGetViewports(&s.viewportCount, s.viewports);

    // Pixel Shader
    s.psCICount = 256;
    m_context->PSGetShader(&s.ps, s.psCI, &s.psCICount);
    m_context->PSGetConstantBuffers(0, 4, s.psCBs);

    // Output Merger
    m_context->OMGetRenderTargets(8, s.rtvs, &s.dsv);
    m_context->OMGetBlendState(&s.blendState, s.blendFactor, &s.sampleMask);
    m_context->OMGetDepthStencilState(&s.depthStencilState, &s.stencilRef);

    s.saved = true;
}


void DebugRenderer::RestorePipelineState()
{
    auto& s = m_saved;
    if (!s.saved) return;

    // Input Assembler
    m_context->IASetPrimitiveTopology(s.topology);
    m_context->IASetInputLayout(s.inputLayout);
    m_context->IASetVertexBuffers(0, 4, s.vertexBuffers, s.vbStrides, s.vbOffsets);

    // Vertex Shader
    m_context->VSSetShader(s.vs, s.vsCI, s.vsCICount);
    m_context->VSSetConstantBuffers(0, 4, s.vsCBs);

    // Rasterizer
    m_context->RSSetState(s.rsState);
    if (s.viewportCount > 0)
        m_context->RSSetViewports(s.viewportCount, s.viewports);

    // Pixel Shader
    m_context->PSSetShader(s.ps, s.psCI, s.psCICount);
    m_context->PSSetConstantBuffers(0, 4, s.psCBs);

    // Output Merger
    m_context->OMSetRenderTargets(8, s.rtvs, s.dsv);
    m_context->OMSetBlendState(s.blendState, s.blendFactor, s.sampleMask);
    m_context->OMSetDepthStencilState(s.depthStencilState, s.stencilRef);

    // Release COM references
    s.Release();
}


// =============================================================================
//  SavedState::Release -- drop all COM refs acquired by Get* calls
// =============================================================================

void DebugRenderer::SavedState::Release()
{
    if (!saved) return;

    // IA
    if (inputLayout) inputLayout->Release();
    for (auto& vb : vertexBuffers) if (vb) vb->Release();

    // VS
    if (vs) vs->Release();
    for (UINT i = 0; i < vsCICount; i++) if (vsCI[i]) vsCI[i]->Release();
    for (auto& cb : vsCBs) if (cb) cb->Release();

    // RS
    if (rsState) rsState->Release();

    // PS
    if (ps) ps->Release();
    for (UINT i = 0; i < psCICount; i++) if (psCI[i]) psCI[i]->Release();
    for (auto& cb : psCBs) if (cb) cb->Release();

    // OM
    for (auto& rtv : rtvs) if (rtv) rtv->Release();
    if (dsv) dsv->Release();
    if (blendState) blendState->Release();
    if (depthStencilState) depthStencilState->Release();

    // Zero everything
    *this = {};
}

} // namespace SB
