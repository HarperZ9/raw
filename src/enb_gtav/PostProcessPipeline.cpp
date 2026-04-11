//=============================================================================
//  PostProcessPipeline.cpp — ENB Post-Processing Pipeline
//=============================================================================

#include "PostProcessPipeline.h"
#include "RenderTargetPool.h"
#include "ConfigManager.h"
#include "ENBState.h"
#include "ENBLog.h"
#include <cstdio>
#include <cstring>
#include <cmath>

PostProcessPipeline g_Pipeline;

// Route pipeline logs through the file logger
#define PipeLog(...) g_Log.Log(__VA_ARGS__)

// ═══════════════════════════════════════════════════════════════════════════
//  Fullscreen triangle vertex (3 vertices cover entire screen)
// ═══════════════════════════════════════════════════════════════════════════

struct QuadVertex
{
    float pos[3];
    float uv[2];
};

static const QuadVertex kFullscreenTri[] = {
    // Full-screen triangle (oversized, clipped by rasterizer)
    { {-1.0f,  1.0f, 0.0f}, {0.0f, 0.0f} },
    { { 3.0f,  1.0f, 0.0f}, {2.0f, 0.0f} },
    { {-1.0f, -3.0f, 0.0f}, {0.0f, 2.0f} },
};

// ENB shaders use a quad (4 vertices), not a triangle.
// Match the original VS_Draw input layout: POSITION + TEXCOORD0
static const QuadVertex kFullscreenQuad[] = {
    { {-1.0f,  1.0f, 0.0f}, {0.0f, 0.0f} },  // top-left
    { { 1.0f,  1.0f, 0.0f}, {1.0f, 0.0f} },  // top-right
    { {-1.0f, -1.0f, 0.0f}, {0.0f, 1.0f} },  // bottom-left
    { { 1.0f, -1.0f, 0.0f}, {1.0f, 1.0f} },  // bottom-right
};

// ═══════════════════════════════════════════════════════════════════════════
//  Initialize
// ═══════════════════════════════════════════════════════════════════════════

bool PostProcessPipeline::Initialize(ID3D11Device* device, const char* gameDir)
{
    strncpy_s(m_gameDir, gameDir, MAX_PATH - 1);
    QueryPerformanceFrequency(&m_frequency);
    QueryPerformanceCounter(&m_startTime);

    // Initialize shader compiler
    if (!g_Compiler.Initialize())
        return false;

    // Create fullscreen quad vertex buffer
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(kFullscreenQuad);
        desc.Usage     = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = kFullscreenQuad;

        device->CreateBuffer(&desc, &init, &m_quadVB);
    }

    // Create common constant buffer (ENBCommonCB)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(ENBCommonCB);
        desc.Usage     = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        device->CreateBuffer(&desc, nullptr, &m_commonCB);
    }

    // Create samplers matching ENB's Sampler0 (point) and Sampler1 (linear)
    {
        D3D11_SAMPLER_DESC desc = {};
        desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxLOD   = D3D11_FLOAT32_MAX;

        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        device->CreateSamplerState(&desc, &m_samplerPoint);

        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        device->CreateSamplerState(&desc, &m_samplerLinear);
    }

    // Blend state: disabled (opaque)
    {
        D3D11_BLEND_DESC desc = {};
        desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        device->CreateBlendState(&desc, &m_blendDisabled);
    }

    // Depth stencil: disabled
    {
        D3D11_DEPTH_STENCIL_DESC desc = {};
        desc.DepthEnable = FALSE;
        device->CreateDepthStencilState(&desc, &m_dsDisabled);
    }

    // Rasterizer: no culling, solid fill
    {
        D3D11_RASTERIZER_DESC desc = {};
        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = D3D11_CULL_NONE;
        device->CreateRasterizerState(&desc, &m_rsNoCull);
    }

    // Initialize render target pool
    g_RTPool.Initialize(device, g_ENB.screenWidth, g_ENB.screenHeight);

    // Load shader effect files
    static const char* shaderFiles[] = {
        "enbeffectprepass.fx",
        "enbadaptation.fx",
        "enbbloom.fx",
        "enblens.fx",
        "enbeffect.fx",
        "enbeffectpostpass.fx",
        "enblightsprite.fx",
    };

    for (const char* sf : shaderFiles)
    {
        char path[MAX_PATH];
        snprintf(path, MAX_PATH, "%s\\%s", gameDir, sf);

        LoadedEffect effect;
        effect.filename = sf;

        if (LoadEffect(device, path, effect))
        {
            effect.selectedTechnique = g_Config.GetShaderTechnique(sf);
            m_effects.push_back(std::move(effect));
        }
    }

    PipeLog("[ENB] Post-processing pipeline: %zu effects loaded\n", m_effects.size());
    return true;
}

void PostProcessPipeline::Shutdown()
{
    for (auto& effect : m_effects)
    {
        for (auto& tech : effect.techniques)
        {
            for (auto& pass : tech.passes)
            {
                if (pass.vs)     pass.vs->Release();
                if (pass.ps)     pass.ps->Release();
                if (pass.vsBlob) pass.vsBlob->Release();
                if (pass.psBlob) pass.psBlob->Release();
            }
        }
        if (effect.cbuffer) effect.cbuffer->Release();
    }
    m_effects.clear();

    if (m_commonCB)      { m_commonCB->Release();      m_commonCB = nullptr; }
    if (m_quadVB)        { m_quadVB->Release();         m_quadVB = nullptr; }
    if (m_quadLayout)    { m_quadLayout->Release();     m_quadLayout = nullptr; }
    if (m_samplerPoint)  { m_samplerPoint->Release();   m_samplerPoint = nullptr; }
    if (m_samplerLinear) { m_samplerLinear->Release();  m_samplerLinear = nullptr; }
    if (m_blendDisabled) { m_blendDisabled->Release();  m_blendDisabled = nullptr; }
    if (m_dsDisabled)    { m_dsDisabled->Release();     m_dsDisabled = nullptr; }
    if (m_rsNoCull)      { m_rsNoCull->Release();       m_rsNoCull = nullptr; }

    g_RTPool.Shutdown();
    g_Compiler.Shutdown();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Load and compile a single effect file
// ═══════════════════════════════════════════════════════════════════════════

bool PostProcessPipeline::LoadEffect(ID3D11Device* device, const char* path, LoadedEffect& effect)
{
    // Read file
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        PipeLog("[ENB] Shader file not found: %s\n", path);
        return false;
    }

    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE)
    {
        CloseHandle(hFile);
        return false;
    }

    effect.source.resize(fileSize);
    DWORD bytesRead = 0;
    ReadFile(hFile, effect.source.data(), fileSize, &bytesRead, nullptr);
    CloseHandle(hFile);

    if (bytesRead != fileSize)
        return false;

    // Parse techniques and variables
    g_Compiler.ParseEffectFile(path, effect.source.c_str(), effect.source.size(),
                                effect.techniques, effect.variables);

    // Compile all shader passes
    return CompileEffect(device, effect);
}

bool PostProcessPipeline::CompileEffect(ID3D11Device* device, LoadedEffect& effect)
{
    // Build defines based on enbseries.ini settings
    std::vector<D3D_SHADER_MACRO> defines;

    if (g_Config.GetEffectToggles().useProceduralCorrection)
        defines.push_back({"E_CC_PROCEDURAL", "1"});

    defines.push_back({nullptr, nullptr}); // terminator

    bool anyCompiled = false;

    for (auto& tech : effect.techniques)
    {
        for (auto& pass : tech.passes)
        {
            // Compile vertex shader
            std::string errors;
            if (g_Compiler.CompileShader(
                    effect.source.c_str(), effect.source.size(),
                    pass.vsEntryPoint.c_str(), pass.vsProfile.c_str(),
                    effect.filename.c_str(), defines.data(),
                    &pass.vsBlob, &errors))
            {
                g_Compiler.CreateVertexShader(device, pass.vsBlob, &pass.vs);

                // Create input layout from first VS (same for all ENB shaders)
                if (!m_quadLayout && pass.vsBlob)
                {
                    D3D11_INPUT_ELEMENT_DESC layout[] = {
                        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                          D3D11_INPUT_PER_VERTEX_DATA, 0 },
                        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
                          D3D11_INPUT_PER_VERTEX_DATA, 0 },
                    };
                    device->CreateInputLayout(layout, 2,
                                              pass.vsBlob->GetBufferPointer(),
                                              pass.vsBlob->GetBufferSize(),
                                              &m_quadLayout);
                }
            }

            // Compile pixel shader
            if (g_Compiler.CompileShader(
                    effect.source.c_str(), effect.source.size(),
                    pass.psEntryPoint.c_str(), pass.psProfile.c_str(),
                    effect.filename.c_str(), defines.data(),
                    &pass.psBlob, &errors))
            {
                g_Compiler.CreatePixelShader(device, pass.psBlob, &pass.ps);
                anyCompiled = true;
            }
        }
    }

    effect.loaded = anyCompiled;

    // Register shader variables in ConfigManager for SDK access
    for (auto& var : effect.variables)
    {
        ENBParameterType ptype = ENBParam_FLOAT;
        if (var.type == EffectVariable::Float3) ptype = ENBParam_COLOR3;
        else if (var.type == EffectVariable::Float4) ptype = ENBParam_COLOR4;
        else if (var.type == EffectVariable::Int) ptype = ENBParam_INT;
        else if (var.type == EffectVariable::Bool) ptype = ENBParam_BOOL;

        g_Config.RegisterShaderVariable(
            effect.filename.c_str(),
            var.uiName.empty() ? var.name.c_str() : var.uiName.c_str(),
            ptype, &var.data);
    }

    return anyCompiled;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Update common uniform buffer
// ═══════════════════════════════════════════════════════════════════════════

void PostProcessPipeline::UpdateCommonCB(ID3D11DeviceContext* ctx)
{
    // Compute timing
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double elapsed = static_cast<double>(now.QuadPart - m_startTime.QuadPart) /
                     static_cast<double>(m_frequency.QuadPart);

    // Cyclic timer: 0..1 over 16777216ms (~4.6 hours)
    float cyclic = static_cast<float>(fmod(elapsed * 1000.0, 16777216.0) / 16777216.0);

    static LARGE_INTEGER lastFrame = now;
    double frameDelta = static_cast<double>(now.QuadPart - lastFrame.QuadPart) /
                        static_cast<double>(m_frequency.QuadPart);
    lastFrame = now;

    m_frameTime = static_cast<float>(frameDelta);
    if (m_frameTime > 0.0f)
        m_avgFPS = m_avgFPS * 0.95f + (1.0f / m_frameTime) * 0.05f;

    float W = static_cast<float>(g_ENB.screenWidth);
    float H = static_cast<float>(g_ENB.screenHeight);
    float aspect = (H > 0.0f) ? W / H : 1.0f;

    ENBCommonCB cb = {};
    cb.Timer[0]     = cyclic;
    cb.Timer[1]     = m_avgFPS;
    cb.Timer[2]     = 0.0f;
    cb.Timer[3]     = m_frameTime;
    cb.ScreenSize[0] = W;
    cb.ScreenSize[1] = (W > 0.0f) ? 1.0f / W : 0.0f;
    cb.ScreenSize[2] = aspect;
    cb.ScreenSize[3] = (aspect > 0.0f) ? 1.0f / aspect : 0.0f;
    cb.AdaptiveQuality = 0.0f; // TODO: adaptive quality

    cb.Weather[0] = static_cast<float>(g_ENB.currentWeather);
    cb.Weather[1] = static_cast<float>(g_ENB.outgoingWeather);
    cb.Weather[2] = g_ENB.weatherTransition;
    cb.Weather[3] = g_ENB.timeOfDay;

    cb.TimeOfDay1[0] = g_ENB.todFactorDawn;
    cb.TimeOfDay1[1] = g_ENB.todFactorSunrise;
    cb.TimeOfDay1[2] = g_ENB.todFactorDay;
    cb.TimeOfDay1[3] = g_ENB.todFactorSunset;
    cb.TimeOfDay2[0] = g_ENB.todFactorDusk;
    cb.TimeOfDay2[1] = g_ENB.todFactorNight;

    cb.ENightDayFactor = g_ENB.nightDayFactor;
    cb.EInteriorFactor = g_ENB.interiorFactor;

    // tempF1-F3 (keyboard debug vars, default 1.0)
    for (int i = 0; i < 4; i++) { cb.tempF1[i] = 1.0f; cb.tempF2[i] = 1.0f; }
    cb.tempF3[0] = 1.0f; cb.tempF3[1] = 1.0f;

    // tempInfo1: cursor + editor state
    cb.tempInfo1[2] = g_ENB.editorActive ? 1.0f : 0.0f;

    // Map and update
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(ctx->Map(m_commonCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, &cb, sizeof(cb));
        ctx->Unmap(m_commonCB, 0);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Draw fullscreen quad
// ═══════════════════════════════════════════════════════════════════════════

void PostProcessPipeline::DrawFullscreenQuad(ID3D11DeviceContext* ctx)
{
    UINT stride = sizeof(QuadVertex);
    UINT offset = 0;
    ctx->IASetVertexBuffers(0, 1, &m_quadVB, &stride, &offset);
    ctx->IASetInputLayout(m_quadLayout);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->Draw(4, 0);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Bind textures for a shader pass
// ═══════════════════════════════════════════════════════════════════════════

void PostProcessPipeline::BindTextures(ID3D11DeviceContext* ctx, const LoadedEffect& effect,
                                        ID3D11ShaderResourceView* colorSRV,
                                        ID3D11ShaderResourceView* depthSRV)
{
    // ENB shaders use named textures bound to consecutive slots:
    // The binding depends on the shader type. For enbeffect.fx:
    //   t0 = TextureColor, t1 = TextureBloom, t2 = TextureLens,
    //   t3 = TextureDepth, t4 = TextureAdaptation, t5 = TextureAperture
    // For enbbloom.fx:
    //   t0 = TextureDownsampled, t1 = TextureColor, etc.
    //
    // We bind by convention — the .fx files declare textures in order,
    // and the D3DX11 Effects framework binds them in declaration order.

    // Bind standard textures to known slots
    ID3D11ShaderResourceView* srvs[8] = {};
    srvs[0] = colorSRV;                                    // TextureColor / TextureDownsampled
    srvs[1] = g_RTPool.GetSRV("RenderTarget1024");         // TextureBloom (or secondary)
    srvs[2] = nullptr;                                      // TextureLens
    srvs[3] = depthSRV;                                     // TextureDepth
    srvs[4] = g_RTPool.GetSRV("TexturePrevious");          // TextureAdaptation
    srvs[5] = nullptr;                                      // TextureAperture

    // Bind render targets as additional texture slots for multi-pass
    srvs[6] = g_RTPool.GetSRV("RenderTarget512");
    srvs[7] = g_RTPool.GetSRV("RenderTarget256");

    ctx->PSSetShaderResources(0, 8, srvs);

    // Bind samplers: s0 = point, s1 = linear
    ID3D11SamplerState* samplers[] = { m_samplerPoint, m_samplerLinear };
    ctx->PSSetSamplers(0, 2, samplers);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Execute a single technique
// ═══════════════════════════════════════════════════════════════════════════

void PostProcessPipeline::ExecuteTechnique(ID3D11DeviceContext* ctx, LoadedEffect& effect,
                                            int techIndex,
                                            ID3D11RenderTargetView* defaultRTV,
                                            ID3D11ShaderResourceView* colorSRV,
                                            ID3D11ShaderResourceView* depthSRV)
{
    if (techIndex < 0 || techIndex >= static_cast<int>(effect.techniques.size()))
        return;

    auto& tech = effect.techniques[techIndex];

    // Set render target (from annotation or default backbuffer)
    ID3D11RenderTargetView* rtv = defaultRTV;
    D3D11_VIEWPORT vp = {};
    vp.Width    = static_cast<float>(g_ENB.screenWidth);
    vp.Height   = static_cast<float>(g_ENB.screenHeight);
    vp.MaxDepth = 1.0f;

    if (!tech.renderTarget.empty())
    {
        ID3D11RenderTargetView* namedRTV = g_RTPool.GetRTV(tech.renderTarget);
        if (namedRTV)
        {
            rtv = namedRTV;
            auto* entry = g_RTPool.Get(tech.renderTarget);
            if (entry)
            {
                vp.Width  = static_cast<float>(entry->width);
                vp.Height = static_cast<float>(entry->height);
            }
        }
    }

    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->RSSetViewports(1, &vp);

    // Execute each pass
    for (auto& pass : tech.passes)
    {
        if (!pass.vs || !pass.ps) continue;

        ctx->VSSetShader(pass.vs, nullptr, 0);
        ctx->PSSetShader(pass.ps, nullptr, 0);

        // TODO: Bind $Globals cbuffer with correct layout matching the shader.
        // For now, don't bind our common CB (wrong layout) — shader uses defaults.
        // ctx->VSSetConstantBuffers(0, 1, &m_commonCB);
        // ctx->PSSetConstantBuffers(0, 1, &m_commonCB);

        BindTextures(ctx, effect, colorSRV, depthSRV);
        DrawFullscreenQuad(ctx);
    }

    // Unbind render target SRV to avoid D3D11 warnings
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx->PSSetShaderResources(0, 1, &nullSRV);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Execute full pipeline
// ═══════════════════════════════════════════════════════════════════════════

void PostProcessPipeline::Execute(ID3D11Device* device, ID3D11DeviceContext* ctx,
                                   ID3D11RenderTargetView* backbufferRTV,
                                   ID3D11ShaderResourceView* backbufferSRV,
                                   ID3D11ShaderResourceView* depthSRV)
{
    if (!g_ENB.useEffect) return;
    if (m_effects.empty()) return;

    // Save current D3D11 state (we'll restore after)
    // TODO: Full state save/restore for production

    // Update common uniform buffer
    UpdateCommonCB(ctx);

    // Set shared render state
    ctx->OMSetBlendState(m_blendDisabled, nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(m_dsDisabled, 0);
    ctx->RSSetState(m_rsNoCull);

    // Null out geometry shader (game might have one bound)
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);

    // Execute each loaded effect in order
    for (auto& effect : m_effects)
    {
        if (!effect.loaded) continue;

        // Check if this effect is enabled
        const auto& toggles = g_Config.GetEffectToggles();
        bool enabled = true;

        // For initial testing: only run enbeffect.fx (direct tone mapping).
        // Multi-pass effects (bloom, lens, adaptation) need proper RT ping-pong.
        if (effect.filename == "enbeffect.fx")
            enabled = true;  // Always run the main tone mapper
        else if (effect.filename == "enbbloom.fx")
            enabled = toggles.enableBloom;
        else if (effect.filename == "enblens.fx")
            enabled = toggles.enableLens;
        else if (effect.filename == "enbadaptation.fx")
            enabled = toggles.enableAdaptation;
        else if (effect.filename == "enbeffectpostpass.fx")
            enabled = toggles.enablePostPassShader;
        else if (effect.filename == "enblightsprite.fx")
            enabled = toggles.enableSprites;
        else if (effect.filename == "enbeffectprepass.fx")
            enabled = false;  // Skip prepass for now (needs depth + mask textures)
        else
            enabled = false;  // Skip unknown effects

        if (!enabled) continue;

        // Find the selected technique group
        int selectedGroup = effect.selectedTechnique;

        // Execute all techniques in the selected group
        for (int i = 0; i < static_cast<int>(effect.techniques.size()); i++)
        {
            auto& tech = effect.techniques[i];
            if (tech.groupIndex == selectedGroup)
            {
                ExecuteTechnique(ctx, effect, i, backbufferRTV, backbufferSRV, depthSRV);
            }
        }
    }

    // Restore backbuffer as render target
    D3D11_VIEWPORT vp = {};
    vp.Width    = static_cast<float>(g_ENB.screenWidth);
    vp.Height   = static_cast<float>(g_ENB.screenHeight);
    vp.MaxDepth = 1.0f;
    ctx->OMSetRenderTargets(1, &backbufferRTV, nullptr);
    ctx->RSSetViewports(1, &vp);
}

void PostProcessPipeline::ReloadShaders(ID3D11Device* device)
{
    PipeLog("[ENB] Reloading all shaders...\n");
    for (auto& effect : m_effects)
    {
        // Release old shaders
        for (auto& tech : effect.techniques)
        {
            for (auto& pass : tech.passes)
            {
                if (pass.vs) { pass.vs->Release(); pass.vs = nullptr; }
                if (pass.ps) { pass.ps->Release(); pass.ps = nullptr; }
                if (pass.vsBlob) { pass.vsBlob->Release(); pass.vsBlob = nullptr; }
                if (pass.psBlob) { pass.psBlob->Release(); pass.psBlob = nullptr; }
            }
        }
        effect.techniques.clear();
        effect.variables.clear();

        // Re-read source
        char path[MAX_PATH];
        snprintf(path, MAX_PATH, "%s\\%s", m_gameDir, effect.filename.c_str());
        LoadEffect(device, path, effect);
    }
}

LoadedEffect* PostProcessPipeline::GetEffect(const char* filename)
{
    for (auto& e : m_effects)
    {
        if (_stricmp(e.filename.c_str(), filename) == 0)
            return &e;
    }
    return nullptr;
}
