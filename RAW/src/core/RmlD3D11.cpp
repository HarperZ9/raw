//=============================================================================
//  RmlD3D11.cpp — RmlUi D3D11 integration
//
//  Minimal D3D11 render backend for RmlUi. Renders geometry as textured
//  triangles with alpha blending. Supports solid colors and texture fills.
//
//  Copyright (c) 2026 Zain D. Harper. All rights reserved.
//=============================================================================

#include "RmlD3D11.h"
#include <RmlUi/Core.h>
#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <filesystem>

namespace RmlD3D11
{

// ══════════════════════════════════════════════════════════════════════════
//  Embedded shaders for RmlUi geometry
// ══════════════════════════════════════════════════════════════════════════

static const char kRmlVS[] = R"(
cbuffer CB : register(b0) { float4x4 Transform; };
struct VS_IN  { float2 pos : POSITION; float4 col : COLOR; float2 uv : TEXCOORD; };
struct VS_OUT { float4 pos : SV_Position; float4 col : COLOR; float2 uv : TEXCOORD; };
VS_OUT main(VS_IN i) {
    VS_OUT o;
    o.pos = mul(float4(i.pos, 0, 1), Transform);
    o.col = i.col;
    o.uv  = i.uv;
    return o;
}
)";

static const char kRmlPS[] = R"(
Texture2D tex : register(t0);
SamplerState samp : register(s0);
struct PS_IN { float4 pos : SV_Position; float4 col : COLOR; float2 uv : TEXCOORD; };
float4 main(PS_IN i) : SV_Target {
    return i.col * tex.Sample(samp, i.uv);
}
)";

// ══════════════════════════════════════════════════════════════════════════
//  D3D11 Render Interface for RmlUi
// ══════════════════════════════════════════════════════════════════════════

class D3D11RenderInterface : public Rml::RenderInterface
{
public:
    bool Init(ID3D11Device* dev, ID3D11DeviceContext* ctx, uint32_t w, uint32_t h);
    void Shutdown();
    void SetDimensions(uint32_t w, uint32_t h);

    // Rml::RenderInterface overrides
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                 Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry,
                        Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions,
                                    const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                        Rml::Vector2i source_dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;

    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

private:
    struct CompiledGeo {
        ID3D11Buffer* vb = nullptr;
        ID3D11Buffer* ib = nullptr;
        uint32_t indexCount = 0;
    };

    struct TextureData {
        ID3D11Texture2D* tex = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
    };

    void UpdateTransform(float tx, float ty);

    ID3D11Device*        m_dev = nullptr;
    ID3D11DeviceContext* m_ctx = nullptr;
    uint32_t m_width = 0, m_height = 0;

    ID3D11VertexShader*   m_vs = nullptr;
    ID3D11PixelShader*    m_ps = nullptr;
    ID3D11InputLayout*    m_layout = nullptr;
    ID3D11Buffer*         m_cb = nullptr;
    ID3D11BlendState*     m_blendState = nullptr;
    ID3D11RasterizerState* m_rastState = nullptr;
    ID3D11DepthStencilState* m_dsState = nullptr;
    ID3D11SamplerState*   m_sampler = nullptr;
    TextureData           m_whiteTexture;  // 1x1 white for untextured geometry

    Rml::CompiledGeometryHandle m_nextGeoHandle = 1;
    std::unordered_map<Rml::CompiledGeometryHandle, CompiledGeo> m_geometry;

    Rml::TextureHandle m_nextTexHandle = 1;
    std::unordered_map<Rml::TextureHandle, TextureData> m_textures;

    bool m_scissorEnabled = false;
};

// ══════════════════════════════════════════════════════════════════════════
//  System Interface (file I/O, logging, timing)
// ══════════════════════════════════════════════════════════════════════════

class D3D11SystemInterface : public Rml::SystemInterface
{
public:
    double GetElapsedTime() override {
        static auto start = std::chrono::high_resolution_clock::now();
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double>(now - start).count();
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        switch (type) {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:
            SKSE::log::error("RmlUi: {}", message);
            break;
        case Rml::Log::LT_WARNING:
            SKSE::log::warn("RmlUi: {}", message);
            break;
        default:
            SKSE::log::info("RmlUi: {}", message);
            break;
        }
        return true;
    }
};

// ══════════════════════════════════════════════════════════════════════════
//  Module state
// ══════════════════════════════════════════════════════════════════════════

static D3D11RenderInterface s_renderInterface;
static D3D11SystemInterface s_systemInterface;
static Rml::Context* s_context = nullptr;
static bool s_initialized = false;
static bool s_visible = true;

// ══════════════════════════════════════════════════════════════════════════
//  Public API
// ══════════════════════════════════════════════════════════════════════════

bool Init(ID3D11Device* device, ID3D11DeviceContext* context,
          uint32_t width, uint32_t height, HWND hwnd)
{
    if (s_initialized) return true;

    if (!s_renderInterface.Init(device, context, width, height))
        return false;

    Rml::SetSystemInterface(&s_systemInterface);
    Rml::SetRenderInterface(&s_renderInterface);
    Rml::Initialise();

    // Load fonts — try the UI folder first, fall back to system fonts
    auto uiDir = std::filesystem::path("Data/SKSE/Plugins/RAW/UI");
    bool fontLoaded = false;
    {
        auto regular = uiDir / "LatoLatin-Regular.ttf";
        auto bold    = uiDir / "LatoLatin-Bold.ttf";
        auto mono    = uiDir / "CascadiaMono.ttf";

        std::error_code ec;
        if (std::filesystem::exists(regular, ec)) {
            fontLoaded = Rml::LoadFontFace(regular.string(), true);
            SKSE::log::info("RmlD3D11: loaded font '{}'  ok={}", regular.string(), fontLoaded);
        }
        if (std::filesystem::exists(bold, ec)) {
            Rml::LoadFontFace(bold.string());
        }
        if (std::filesystem::exists(mono, ec)) {
            Rml::LoadFontFace(mono.string());
        }
    }
    if (!fontLoaded) {
        SKSE::log::warn("RmlD3D11: no fonts loaded — text will not render");
    }

    s_context = Rml::CreateContext("main", Rml::Vector2i(width, height));
    if (!s_context) {
        SKSE::log::error("RmlD3D11: failed to create context");
        Rml::Shutdown();
        return false;
    }

    s_initialized = true;
    SKSE::log::info("RmlD3D11: initialized ({}x{})", width, height);
    return true;
}

void Shutdown()
{
    if (!s_initialized) return;
    if (s_context) {
        Rml::RemoveContext("main");
        s_context = nullptr;
    }
    Rml::Shutdown();
    s_renderInterface.Shutdown();
    s_initialized = false;
}

bool LoadDocument(const char* path)
{
    if (!s_context) return false;
    auto* doc = s_context->LoadDocument(path);
    if (!doc) {
        SKSE::log::error("RmlD3D11: failed to load '{}'", path);
        return false;
    }
    doc->Show();
    SKSE::log::info("RmlD3D11: loaded '{}'", path);
    return true;
}

void Update()
{
    if (!s_initialized || !s_visible || !s_context) return;
    s_context->Update();
}

void Render()
{
    if (!s_initialized || !s_visible || !s_context) return;
    s_context->Render();
}

bool ProcessWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!s_initialized || !s_visible || !s_context) return false;

    switch (msg) {
    case WM_MOUSEMOVE: {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        s_context->ProcessMouseMove(x, y, 0);
        return true;
    }
    case WM_LBUTTONDOWN:
        s_context->ProcessMouseButtonDown(0, 0);
        return true;
    case WM_LBUTTONUP:
        s_context->ProcessMouseButtonUp(0, 0);
        return true;
    case WM_RBUTTONDOWN:
        s_context->ProcessMouseButtonDown(1, 0);
        return true;
    case WM_RBUTTONUP:
        s_context->ProcessMouseButtonUp(1, 0);
        return true;
    case WM_MOUSEWHEEL: {
        float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
        s_context->ProcessMouseWheel(Rml::Vector2f(0, -delta), 0);
        return true;
    }
    case WM_KEYDOWN:
    case WM_KEYUP: {
        // Basic key mapping — extend as needed
        auto key = static_cast<Rml::Input::KeyIdentifier>(wParam);
        if (msg == WM_KEYDOWN)
            s_context->ProcessKeyDown(key, 0);
        else
            s_context->ProcessKeyUp(key, 0);
        return true;
    }
    case WM_CHAR:
        s_context->ProcessTextInput(static_cast<Rml::Character>(wParam));
        return true;
    }
    return false;
}

void OnResize(uint32_t width, uint32_t height)
{
    if (!s_initialized) return;
    s_renderInterface.SetDimensions(width, height);
    if (s_context)
        s_context->SetDimensions(Rml::Vector2i(width, height));
}

bool IsInitialized() { return s_initialized; }
bool IsVisible()     { return s_visible; }
void SetVisible(bool v) { s_visible = v; }

// ══════════════════════════════════════════════════════════════════════════
//  D3D11RenderInterface implementation
// ══════════════════════════════════════════════════════════════════════════

bool D3D11RenderInterface::Init(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                 uint32_t w, uint32_t h)
{
    m_dev = dev;
    m_ctx = ctx;
    m_width = w;
    m_height = h;

    // Compile shaders
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errBlob = nullptr;

    HRESULT hr = D3DCompile(kRmlVS, strlen(kRmlVS), "RmlVS", nullptr, nullptr,
                             "main", "vs_5_0", 0, 0, &vsBlob, &errBlob);
    if (FAILED(hr)) {
        SKSE::log::error("RmlD3D11: VS compile failed");
        if (errBlob) errBlob->Release();
        return false;
    }

    hr = D3DCompile(kRmlPS, strlen(kRmlPS), "RmlPS", nullptr, nullptr,
                     "main", "ps_5_0", 0, 0, &psBlob, &errBlob);
    if (FAILED(hr)) {
        SKSE::log::error("RmlD3D11: PS compile failed");
        vsBlob->Release();
        if (errBlob) errBlob->Release();
        return false;
    }

    dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                            nullptr, &m_vs);
    dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                           nullptr, &m_ps);

    // Input layout matching Rml::Vertex
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, offsetof(Rml::Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,      0, offsetof(Rml::Vertex, colour),   D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, offsetof(Rml::Vertex, tex_coord), D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    dev->CreateInputLayout(layout, 3, vsBlob->GetBufferPointer(),
                           vsBlob->GetBufferSize(), &m_layout);
    vsBlob->Release();
    psBlob->Release();

    // Constant buffer (4x4 ortho matrix)
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = 64;
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    dev->CreateBuffer(&cbDesc, nullptr, &m_cb);

    // Blend state (standard alpha blending)
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev->CreateBlendState(&blendDesc, &m_blendState);

    // Rasterizer (no culling, scissor enabled)
    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_NONE;
    rastDesc.ScissorEnable = TRUE;
    dev->CreateRasterizerState(&rastDesc, &m_rastState);

    // Depth stencil (disabled)
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = FALSE;
    dev->CreateDepthStencilState(&dsDesc, &m_dsState);

    // Sampler
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    dev->CreateSamplerState(&sampDesc, &m_sampler);

    // 1x1 white texture for untextured geometry
    {
        uint32_t white = 0xFFFFFFFF;
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = 1; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc = {1, 0};
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd = {&white, 4, 0};
        dev->CreateTexture2D(&td, &sd, &m_whiteTexture.tex);
        dev->CreateShaderResourceView(m_whiteTexture.tex, nullptr, &m_whiteTexture.srv);
    }

    return true;
}

void D3D11RenderInterface::Shutdown()
{
    for (auto& [h, g] : m_geometry) {
        if (g.vb) g.vb->Release();
        if (g.ib) g.ib->Release();
    }
    m_geometry.clear();
    for (auto& [h, t] : m_textures) {
        if (t.srv) t.srv->Release();
        if (t.tex) t.tex->Release();
    }
    m_textures.clear();

    if (m_whiteTexture.srv) m_whiteTexture.srv->Release();
    if (m_whiteTexture.tex) m_whiteTexture.tex->Release();
    if (m_vs) m_vs->Release();
    if (m_ps) m_ps->Release();
    if (m_layout) m_layout->Release();
    if (m_cb) m_cb->Release();
    if (m_blendState) m_blendState->Release();
    if (m_rastState) m_rastState->Release();
    if (m_dsState) m_dsState->Release();
    if (m_sampler) m_sampler->Release();
}

void D3D11RenderInterface::SetDimensions(uint32_t w, uint32_t h)
{
    m_width = w;
    m_height = h;
}

void D3D11RenderInterface::UpdateTransform(float tx, float ty)
{
    // Orthographic projection: pixel coords → clip coords
    float L = tx, T = ty;
    float R = static_cast<float>(m_width), B = static_cast<float>(m_height);
    float mvp[16] = {
        2.0f / (R - L),       0.0f,                 0.0f, 0.0f,
        0.0f,                 2.0f / (T - B),       0.0f, 0.0f,
        0.0f,                 0.0f,                 0.5f, 0.0f,
        (L + R) / (L - R) + tx * 2.0f / (R - L),
        (T + B) / (B - T) + ty * 2.0f / (T - B),
        0.5f, 1.0f
    };

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_ctx->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, mvp, sizeof(mvp));
        m_ctx->Unmap(m_cb, 0);
    }
}

Rml::CompiledGeometryHandle D3D11RenderInterface::CompileGeometry(
    Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
    CompiledGeo geo;
    geo.indexCount = static_cast<uint32_t>(indices.size());

    D3D11_BUFFER_DESC vbd = {};
    vbd.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Rml::Vertex));
    vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vsd = {vertices.data(), 0, 0};
    m_dev->CreateBuffer(&vbd, &vsd, &geo.vb);

    D3D11_BUFFER_DESC ibd = {};
    ibd.ByteWidth = static_cast<UINT>(indices.size() * sizeof(int));
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA isd = {indices.data(), 0, 0};
    m_dev->CreateBuffer(&ibd, &isd, &geo.ib);

    auto handle = m_nextGeoHandle++;
    m_geometry[handle] = geo;
    return handle;
}

void D3D11RenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry,
                                           Rml::Vector2f translation,
                                           Rml::TextureHandle texture)
{
    auto it = m_geometry.find(geometry);
    if (it == m_geometry.end()) return;
    auto& geo = it->second;

    // Update ortho transform with translation
    // Build a proper ortho matrix
    float L = 0, R = static_cast<float>(m_width);
    float T = 0, B = static_cast<float>(m_height);
    float mvp[16] = {
        2.0f / (R - L),  0.0f,            0.0f, 0.0f,
        0.0f,            2.0f / (T - B),  0.0f, 0.0f,
        0.0f,            0.0f,            0.5f, 0.0f,
        -1.0f + translation.x * 2.0f / (R - L),
         1.0f + translation.y * 2.0f / (T - B),
        0.5f, 1.0f
    };
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_ctx->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, mvp, sizeof(mvp));
        m_ctx->Unmap(m_cb, 0);
    }

    // Set pipeline state
    UINT stride = sizeof(Rml::Vertex), offset = 0;
    m_ctx->IASetVertexBuffers(0, 1, &geo.vb, &stride, &offset);
    m_ctx->IASetIndexBuffer(geo.ib, DXGI_FORMAT_R32_UINT, 0);
    m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_ctx->IASetInputLayout(m_layout);
    m_ctx->VSSetShader(m_vs, nullptr, 0);
    m_ctx->PSSetShader(m_ps, nullptr, 0);
    m_ctx->VSSetConstantBuffers(0, 1, &m_cb);

    // Texture
    ID3D11ShaderResourceView* srv = m_whiteTexture.srv;
    if (texture) {
        auto tit = m_textures.find(texture);
        if (tit != m_textures.end() && tit->second.srv)
            srv = tit->second.srv;
    }
    m_ctx->PSSetShaderResources(0, 1, &srv);
    m_ctx->PSSetSamplers(0, 1, &m_sampler);

    float blendFactor[4] = {0, 0, 0, 0};
    m_ctx->OMSetBlendState(m_blendState, blendFactor, 0xFFFFFFFF);
    m_ctx->OMSetDepthStencilState(m_dsState, 0);
    m_ctx->RSSetState(m_rastState);

    m_ctx->DrawIndexed(geo.indexCount, 0, 0);
}

void D3D11RenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
    auto it = m_geometry.find(geometry);
    if (it == m_geometry.end()) return;
    if (it->second.vb) it->second.vb->Release();
    if (it->second.ib) it->second.ib->Release();
    m_geometry.erase(it);
}

Rml::TextureHandle D3D11RenderInterface::LoadTexture(Rml::Vector2i& dims,
                                                      const Rml::String& source)
{
    // For now, return 0 (no file-based textures)
    // TODO: implement stb_image loading
    return 0;
}

Rml::TextureHandle D3D11RenderInterface::GenerateTexture(
    Rml::Span<const Rml::byte> source, Rml::Vector2i dims)
{
    TextureData td;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = dims.x;
    desc.Height = dims.y;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc = {1, 0};
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = source.data();
    sd.SysMemPitch = dims.x * 4;

    HRESULT hr = m_dev->CreateTexture2D(&desc, &sd, &td.tex);
    if (FAILED(hr)) return 0;

    hr = m_dev->CreateShaderResourceView(td.tex, nullptr, &td.srv);
    if (FAILED(hr)) { td.tex->Release(); return 0; }

    auto handle = m_nextTexHandle++;
    m_textures[handle] = td;
    return handle;
}

void D3D11RenderInterface::ReleaseTexture(Rml::TextureHandle texture)
{
    auto it = m_textures.find(texture);
    if (it == m_textures.end()) return;
    if (it->second.srv) it->second.srv->Release();
    if (it->second.tex) it->second.tex->Release();
    m_textures.erase(it);
}

void D3D11RenderInterface::EnableScissorRegion(bool enable)
{
    m_scissorEnabled = enable;
    if (!enable) {
        D3D11_RECT rect = {0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
        m_ctx->RSSetScissorRects(1, &rect);
    }
}

void D3D11RenderInterface::SetScissorRegion(Rml::Rectanglei region)
{
    D3D11_RECT rect;
    rect.left   = region.Left();
    rect.top    = region.Top();
    rect.right  = region.Right();
    rect.bottom = region.Bottom();
    m_ctx->RSSetScissorRects(1, &rect);
}

} // namespace RmlD3D11
