#pragma once
//=============================================================================
//  PostProcessPipeline.h — ENB Post-Processing Shader Pipeline
//
//  Loads, compiles, and executes the 7 ENB .fx shader files in the correct
//  order during API_BeforePresent. Manages the fullscreen quad, common
//  uniform buffer, and per-shader texture bindings.
//=============================================================================

#include <Windows.h>
#include <d3d11.h>
#include <string>
#include <vector>
#include "EffectCompiler.h"

// ---------------------------------------------------------------------------
//  Common ENB uniform buffer (shared by all .fx shaders, 256-byte aligned)
// ---------------------------------------------------------------------------
struct alignas(16) ENBCommonCB
{
    float Timer[4];           // x=cyclic 0..1, y=avgFPS, z=0, w=frameTime
    float ScreenSize[4];      // x=W, y=1/W, z=aspect, w=1/aspect
    float AdaptiveQuality;    // 0=full, 1=lowest
    float _pad0[3];
    float Weather[4];         // x=currentID, y=outgoingID, z=transition, w=time0-24
    float TimeOfDay1[4];      // x=dawn, y=sunrise, z=day, w=sunset
    float TimeOfDay2[4];      // x=dusk, y=night, z=0, w=0
    float ENightDayFactor;
    float EInteriorFactor;
    float _pad1[2];
    float tempF1[4];          // keyboard debug vars 0-3
    float tempF2[4];          // keyboard debug vars 4-7
    float tempF3[4];          // keyboard debug vars 8-9
    float tempInfo1[4];       // xy=cursor, z=editorActive, w=mouseButtons
    float tempInfo2[4];       // xy=lastLeftClick, zw=lastRightClick
};

// ---------------------------------------------------------------------------
//  Loaded effect (one per .fx file)
// ---------------------------------------------------------------------------
struct LoadedEffect
{
    std::string                  filename;
    std::string                  source;         // raw .fx source text
    std::vector<EffectTechnique> techniques;
    std::vector<EffectVariable>  variables;
    ID3D11Buffer*                cbuffer = nullptr;  // per-shader constant buffer
    int                          selectedTechnique = 0;
    bool                         loaded = false;
};

// ---------------------------------------------------------------------------
//  Pipeline
// ---------------------------------------------------------------------------
class PostProcessPipeline
{
public:
    bool Initialize(ID3D11Device* device, const char* gameDir);
    void Shutdown();

    // Execute the full post-processing chain
    // Called in API_BeforePresent with the current backbuffer
    void Execute(ID3D11Device* device, ID3D11DeviceContext* ctx,
                 ID3D11RenderTargetView* backbufferRTV,
                 ID3D11ShaderResourceView* backbufferSRV,
                 ID3D11ShaderResourceView* depthSRV);

    // Reload all shaders (e.g., after user edits)
    void ReloadShaders(ID3D11Device* device);

    // Access loaded effects (for editor UI)
    LoadedEffect* GetEffect(const char* filename);
    const std::vector<LoadedEffect>& GetEffects() const { return m_effects; }

private:
    bool LoadEffect(ID3D11Device* device, const char* path, LoadedEffect& effect);
    bool CompileEffect(ID3D11Device* device, LoadedEffect& effect);

    void UpdateCommonCB(ID3D11DeviceContext* ctx);
    void DrawFullscreenQuad(ID3D11DeviceContext* ctx);

    // Bind named textures for a shader (TextureColor, TextureBloom, etc.)
    void BindTextures(ID3D11DeviceContext* ctx, const LoadedEffect& effect,
                      ID3D11ShaderResourceView* colorSRV,
                      ID3D11ShaderResourceView* depthSRV);

    // Execute a single technique
    void ExecuteTechnique(ID3D11DeviceContext* ctx, LoadedEffect& effect,
                          int techIndex,
                          ID3D11RenderTargetView* defaultRTV,
                          ID3D11ShaderResourceView* colorSRV,
                          ID3D11ShaderResourceView* depthSRV);

    char m_gameDir[MAX_PATH] = {};

    // Loaded effects (one per .fx file)
    std::vector<LoadedEffect> m_effects;

    // Common resources
    ID3D11Buffer*          m_commonCB     = nullptr;   // ENBCommonCB
    ID3D11Buffer*          m_quadVB       = nullptr;   // fullscreen triangle
    ID3D11InputLayout*     m_quadLayout   = nullptr;
    ID3D11SamplerState*    m_samplerPoint = nullptr;   // Sampler0
    ID3D11SamplerState*    m_samplerLinear = nullptr;  // Sampler1
    ID3D11BlendState*      m_blendDisabled = nullptr;
    ID3D11DepthStencilState* m_dsDisabled = nullptr;
    ID3D11RasterizerState* m_rsNoCull     = nullptr;

    // Performance timing
    LARGE_INTEGER m_startTime = {};
    LARGE_INTEGER m_frequency = {};
    float         m_avgFPS    = 60.0f;
    float         m_frameTime = 0.016f;
};

extern PostProcessPipeline g_Pipeline;
