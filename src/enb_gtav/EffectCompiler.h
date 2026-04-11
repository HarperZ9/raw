#pragma once
//=============================================================================
//  EffectCompiler.h — HLSL .fx File Parser and Compiler
//
//  Parses ENB .fx files to extract technique11 blocks, compiles individual
//  VS/PS entry points with D3DCompile, and extracts variable annotations
//  via ID3D11ShaderReflection.
//
//  Replaces the legacy D3DX11 Effects framework with a lightweight custom
//  implementation that gives full control over the compilation pipeline.
//=============================================================================

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <string>
#include <vector>

// Forward declarations
struct EffectTechnique;
struct EffectVariable;

// ---------------------------------------------------------------------------
//  Parsed technique pass
// ---------------------------------------------------------------------------
struct EffectPass
{
    std::string vsEntryPoint;   // e.g., "VS_Draw"
    std::string psEntryPoint;   // e.g., "PS_Draw"
    std::string vsProfile;      // e.g., "vs_5_0"
    std::string psProfile;      // e.g., "ps_5_0"

    ID3D11VertexShader* vs      = nullptr;
    ID3D11PixelShader*  ps      = nullptr;
    ID3DBlob*           vsBlob  = nullptr;
    ID3DBlob*           psBlob  = nullptr;
};

// ---------------------------------------------------------------------------
//  Parsed technique
// ---------------------------------------------------------------------------
struct EffectTechnique
{
    std::string name;           // e.g., "Draw", "Draw1", "MultiPassLens"
    std::string uiName;         // from annotation: string UIName="..."
    std::string renderTarget;   // from annotation: string RenderTarget="..."
    std::vector<EffectPass> passes;
    int groupIndex = -1;        // which UI group this belongs to (-1 = independent)
};

// ---------------------------------------------------------------------------
//  Parsed variable with annotations
// ---------------------------------------------------------------------------
struct EffectVariable
{
    std::string name;
    std::string uiName;
    std::string uiWidget;       // "spinner", "color", "vector", "quality", "dropdown"
    std::string uiList;         // for dropdown: "x1, x2, x3"
    std::string resourceName;   // for textures: "file.bmp"

    enum VarType { Float, Float3, Float4, Int, Bool, Texture2D, String } type = Float;
    float       uiMin = 0.0f;
    float       uiMax = 1.0f;
    int         uiMinInt = 0;
    int         uiMaxInt = 1;
    bool        uiHidden = false;  // UIHidden=1 annotation

    // Current value storage (up to float4)
    union {
        float  fVal[4];
        int    iVal;
        BOOL   bVal;
    } data = {};
    int         dataSize = 4;      // bytes

    // Offset in the constant buffer
    UINT        cbOffset = 0;
};

// ---------------------------------------------------------------------------
//  Compiler: handles D3DCompile loading and shader compilation
// ---------------------------------------------------------------------------
class EffectCompiler
{
public:
    bool Initialize();
    void Shutdown();

    // Parse an .fx file and extract techniques + variables
    bool ParseEffectFile(const char* filePath, const char* source, size_t sourceLen,
                         std::vector<EffectTechnique>& outTechniques,
                         std::vector<EffectVariable>& outVariables);

    // Compile a single shader entry point from source
    bool CompileShader(const char* source, size_t sourceLen,
                       const char* entryPoint, const char* profile,
                       const char* filePath,   // for error messages
                       const D3D_SHADER_MACRO* defines,
                       ID3DBlob** outBlob, std::string* outErrors);

    // Create VS/PS from compiled blob
    bool CreateVertexShader(ID3D11Device* device, ID3DBlob* blob, ID3D11VertexShader** outVS);
    bool CreatePixelShader(ID3D11Device* device, ID3DBlob* blob, ID3D11PixelShader** outPS);

private:
    // D3DCompile function pointer (loaded dynamically)
    using PFN_D3DCompile = HRESULT(WINAPI*)(
        LPCVOID pSrcData, SIZE_T SrcDataSize, LPCSTR pSourceName,
        const D3D_SHADER_MACRO* pDefines, ID3DInclude* pInclude,
        LPCSTR pEntrypoint, LPCSTR pTarget, UINT Flags1, UINT Flags2,
        ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs);

    HMODULE         m_compilerDLL = nullptr;
    PFN_D3DCompile  m_d3dCompile  = nullptr;

    // Parse technique11 blocks from source text
    void ParseTechniques(const char* source, std::vector<EffectTechnique>& out);

    // Parse variable declarations with annotations
    void ParseVariables(const char* source, std::vector<EffectVariable>& out);

    // Extract annotation value: string UIName="value"
    static std::string ExtractAnnotation(const char* annotBlock, const char* name);
    static int         ExtractAnnotationInt(const char* annotBlock, const char* name, int def);
    static float       ExtractAnnotationFloat(const char* annotBlock, const char* name, float def);
};

extern EffectCompiler g_Compiler;
