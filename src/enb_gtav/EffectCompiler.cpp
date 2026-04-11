//=============================================================================
//  EffectCompiler.cpp — HLSL .fx Parser and Compiler
//=============================================================================

#include "EffectCompiler.h"
#include "ENBLog.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <regex>

EffectCompiler g_Compiler;

#define CompLog(...) g_Log.Log(__VA_ARGS__)

// (Original local CompLog replaced by g_Log macro above)
#if 0 // Dead code preserved for reference
static void CompLog_dead(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
}
#endif // dead code

// ═══════════════════════════════════════════════════════════════════════════
//  Initialize — load D3DCompiler DLL
// ═══════════════════════════════════════════════════════════════════════════

bool EffectCompiler::Initialize()
{
    if (m_d3dCompile) return true;

    // Search order matching original ENB (from string evidence)
    static const char* compilerDLLs[] = {
        "d3dcompiler_47.dll",
        "d3dcompiler_46e.dll",
        "d3dcompiler_46.dll",
        "d3dcompiler_43.dll",
    };

    for (const char* dll : compilerDLLs)
    {
        m_compilerDLL = LoadLibraryA(dll);
        if (m_compilerDLL)
        {
            m_d3dCompile = reinterpret_cast<PFN_D3DCompile>(
                GetProcAddress(m_compilerDLL, "D3DCompile"));
            if (m_d3dCompile)
            {
                CompLog("[ENB] Shader compiler loaded: %s\n", dll);
                return true;
            }
            FreeLibrary(m_compilerDLL);
            m_compilerDLL = nullptr;
        }
    }

    CompLog("[ENB] FATAL: No shader compiler DLL found\n");
    return false;
}

void EffectCompiler::Shutdown()
{
    if (m_compilerDLL)
    {
        FreeLibrary(m_compilerDLL);
        m_compilerDLL = nullptr;
        m_d3dCompile = nullptr;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Annotation extraction helpers
// ═══════════════════════════════════════════════════════════════════════════

std::string EffectCompiler::ExtractAnnotation(const char* block, const char* name)
{
    // Find: string Name="value"
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "string %s", name);

    const char* pos = strstr(block, pattern);
    if (!pos) return "";

    const char* eq = strchr(pos, '=');
    if (!eq) return "";

    const char* q1 = strchr(eq, '"');
    if (!q1) return "";
    q1++;

    const char* q2 = strchr(q1, '"');
    if (!q2) return "";

    return std::string(q1, q2);
}

float EffectCompiler::ExtractAnnotationFloat(const char* block, const char* name, float def)
{
    // Find: float Name=value
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "float %s", name);

    const char* pos = strstr(block, pattern);
    if (!pos) return def;

    const char* eq = strchr(pos, '=');
    if (!eq) return def;

    return static_cast<float>(atof(eq + 1));
}

int EffectCompiler::ExtractAnnotationInt(const char* block, const char* name, int def)
{
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "int %s", name);

    const char* pos = strstr(block, pattern);
    if (!pos) return def;

    const char* eq = strchr(pos, '=');
    if (!eq) return def;

    return atoi(eq + 1);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Parse technique11 blocks
// ═══════════════════════════════════════════════════════════════════════════

void EffectCompiler::ParseTechniques(const char* source, std::vector<EffectTechnique>& out)
{
    const char* pos = source;
    int currentGroup = -1;

    while ((pos = strstr(pos, "technique11 ")) != nullptr)
    {
        pos += 12; // skip "technique11 "

        // Extract technique name
        const char* nameStart = pos;
        while (*pos && !isspace(*pos) && *pos != '<' && *pos != '{') pos++;
        std::string techName(nameStart, pos);

        EffectTechnique tech;
        tech.name = techName;

        // Skip whitespace
        while (*pos && isspace(*pos)) pos++;

        // Parse annotations if present: <string UIName="..."; string RenderTarget="...">
        if (*pos == '<')
        {
            const char* annotStart = pos + 1;
            const char* annotEnd = strchr(annotStart, '>');
            if (annotEnd)
            {
                std::string annotBlock(annotStart, annotEnd);
                tech.uiName = ExtractAnnotation(annotBlock.c_str(), "UIName");
                tech.renderTarget = ExtractAnnotation(annotBlock.c_str(), "RenderTarget");
                pos = annotEnd + 1;
            }
        }

        // If this technique has a UIName, it starts a new group
        if (!tech.uiName.empty())
        {
            currentGroup++;
        }
        tech.groupIndex = currentGroup;

        // Find the technique body { ... }
        const char* braceStart = strchr(pos, '{');
        if (!braceStart) break;

        // Find matching closing brace (handle nested braces)
        int depth = 1;
        const char* scan = braceStart + 1;
        while (*scan && depth > 0)
        {
            if (*scan == '{') depth++;
            else if (*scan == '}') depth--;
            scan++;
        }
        std::string body(braceStart + 1, scan - 1);

        // Parse passes within the technique body
        const char* passPos = body.c_str();
        while ((passPos = strstr(passPos, "pass ")) != nullptr)
        {
            EffectPass pass;

            // Find SetVertexShader(CompileShader(vs_X_X, EntryPoint()));
            const char* vsStart = strstr(passPos, "CompileShader(");
            if (vsStart && strstr(passPos, "SetVertexShader") && vsStart < strstr(passPos, "SetPixelShader"))
            {
                vsStart += 14; // skip "CompileShader("
                // Extract profile: vs_5_0 or vs_4_0
                while (*vsStart && isspace(*vsStart)) vsStart++;
                const char* profEnd = strchr(vsStart, ',');
                if (profEnd)
                {
                    pass.vsProfile = std::string(vsStart, profEnd);
                    // Trim
                    while (!pass.vsProfile.empty() && isspace(pass.vsProfile.back()))
                        pass.vsProfile.pop_back();

                    // Extract entry point name
                    const char* ep = profEnd + 1;
                    while (*ep && isspace(*ep)) ep++;
                    const char* epEnd = strchr(ep, '(');
                    if (epEnd)
                    {
                        pass.vsEntryPoint = std::string(ep, epEnd);
                        while (!pass.vsEntryPoint.empty() && isspace(pass.vsEntryPoint.back()))
                            pass.vsEntryPoint.pop_back();
                    }
                }
            }

            // Find SetPixelShader(CompileShader(ps_X_X, EntryPoint()));
            const char* psSearch = strstr(passPos, "SetPixelShader");
            if (psSearch)
            {
                const char* psStart = strstr(psSearch, "CompileShader(");
                if (psStart)
                {
                    psStart += 14;
                    while (*psStart && isspace(*psStart)) psStart++;
                    const char* profEnd2 = strchr(psStart, ',');
                    if (profEnd2)
                    {
                        pass.psProfile = std::string(psStart, profEnd2);
                        while (!pass.psProfile.empty() && isspace(pass.psProfile.back()))
                            pass.psProfile.pop_back();

                        const char* ep2 = profEnd2 + 1;
                        while (*ep2 && isspace(*ep2)) ep2++;
                        const char* epEnd2 = strchr(ep2, '(');
                        if (epEnd2)
                        {
                            pass.psEntryPoint = std::string(ep2, epEnd2);
                            while (!pass.psEntryPoint.empty() && isspace(pass.psEntryPoint.back()))
                                pass.psEntryPoint.pop_back();
                        }
                    }
                }
            }

            if (!pass.vsEntryPoint.empty() && !pass.psEntryPoint.empty())
                tech.passes.push_back(pass);

            // Advance past this pass
            const char* nextBrace = strchr(passPos, '}');
            if (nextBrace) passPos = nextBrace + 1;
            else break;
        }

        if (!tech.passes.empty())
            out.push_back(tech);

        pos = scan;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Parse variable declarations with annotations
// ═══════════════════════════════════════════════════════════════════════════

void EffectCompiler::ParseVariables(const char* source, std::vector<EffectVariable>& out)
{
    // Match patterns like:
    //   float  VarName < annotations > = {value};
    //   float3 VarName < annotations > = {v1, v2, v3};
    //   int    VarName < annotations > = {value};
    // Only variables with < > annotation blocks are UI-exposed parameters.

    const char* pos = source;

    while (pos && *pos)
    {
        // Find annotation block start '<'
        const char* annotOpen = strchr(pos, '<');
        if (!annotOpen) break;

        // Find the variable declaration before it (scan backward for type + name)
        // Look for float/float3/float4/int/bool before the '<'
        const char* lineStart = annotOpen;
        while (lineStart > source && *(lineStart - 1) != '\n' && *(lineStart - 1) != ';')
            lineStart--;
        while (*lineStart && isspace(*lineStart)) lineStart++;

        // Skip if inside a comment block
        // Simple check: if "/*" appears between lineStart and annotOpen without "*/"
        // we skip (this is a crude but sufficient check for ENB shaders)

        std::string declLine(lineStart, annotOpen);

        // Parse type and name from declLine
        EffectVariable var;
        char typeBuf[32] = {}, nameBuf[128] = {};

        if (sscanf(declLine.c_str(), "%31s %127[^ \t\r\n<]", typeBuf, nameBuf) == 2)
        {
            std::string typeStr = typeBuf;
            var.name = nameBuf;

            if (typeStr == "float")       { var.type = EffectVariable::Float;  var.dataSize = 4; }
            else if (typeStr == "float3") { var.type = EffectVariable::Float3; var.dataSize = 12; }
            else if (typeStr == "float4") { var.type = EffectVariable::Float4; var.dataSize = 16; }
            else if (typeStr == "int")    { var.type = EffectVariable::Int;    var.dataSize = 4; }
            else if (typeStr == "bool")   { var.type = EffectVariable::Bool;   var.dataSize = 4; }
            else {
                pos = annotOpen + 1;
                continue; // Skip Texture2D, SamplerState, string, etc.
            }

            // Find annotation block end '>'
            const char* annotClose = strchr(annotOpen, '>');
            if (!annotClose) { pos = annotOpen + 1; continue; }

            std::string annotBlock(annotOpen + 1, annotClose);

            // Extract annotations
            var.uiName    = ExtractAnnotation(annotBlock.c_str(), "UIName");
            var.uiWidget  = ExtractAnnotation(annotBlock.c_str(), "UIWidget");
            var.uiList    = ExtractAnnotation(annotBlock.c_str(), "UIList");
            var.resourceName = ExtractAnnotation(annotBlock.c_str(), "ResourceName");
            var.uiMin     = ExtractAnnotationFloat(annotBlock.c_str(), "UIMin", 0.0f);
            var.uiMax     = ExtractAnnotationFloat(annotBlock.c_str(), "UIMax", 1.0f);
            var.uiMinInt  = ExtractAnnotationInt(annotBlock.c_str(), "UIMin", 0);
            var.uiMaxInt  = ExtractAnnotationInt(annotBlock.c_str(), "UIMax", 1);
            var.uiHidden  = (ExtractAnnotationInt(annotBlock.c_str(), "UIHidden", 0) != 0);

            // Find default value: = {value} or = value after '>'
            const char* eqSign = strchr(annotClose, '=');
            if (eqSign)
            {
                const char* valStart = eqSign + 1;
                while (*valStart && isspace(*valStart)) valStart++;

                if (*valStart == '{') valStart++; // skip opening brace

                if (var.type == EffectVariable::Float)
                {
                    var.data.fVal[0] = static_cast<float>(atof(valStart));
                }
                else if (var.type == EffectVariable::Float3)
                {
                    sscanf(valStart, "%f, %f, %f", &var.data.fVal[0], &var.data.fVal[1], &var.data.fVal[2]);
                }
                else if (var.type == EffectVariable::Float4)
                {
                    sscanf(valStart, "%f, %f, %f, %f", &var.data.fVal[0], &var.data.fVal[1], &var.data.fVal[2], &var.data.fVal[3]);
                }
                else if (var.type == EffectVariable::Int)
                {
                    var.data.iVal = atoi(valStart);
                }
                else if (var.type == EffectVariable::Bool)
                {
                    var.data.bVal = (atoi(valStart) != 0) ? TRUE : FALSE;
                }
            }

            if (!var.uiName.empty())
                out.push_back(var);
        }

        pos = annotOpen + 1;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  ParseEffectFile — main entry point
// ═══════════════════════════════════════════════════════════════════════════

bool EffectCompiler::ParseEffectFile(const char* filePath, const char* source, size_t sourceLen,
                                      std::vector<EffectTechnique>& outTechniques,
                                      std::vector<EffectVariable>& outVariables)
{
    ParseTechniques(source, outTechniques);
    ParseVariables(source, outVariables);

    CompLog("[ENB] Parsed %s: %zu techniques, %zu variables\n",
            filePath, outTechniques.size(), outVariables.size());
    return !outTechniques.empty();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Compile a single shader entry point
// ═══════════════════════════════════════════════════════════════════════════

bool EffectCompiler::CompileShader(const char* source, size_t sourceLen,
                                    const char* entryPoint, const char* profile,
                                    const char* filePath,
                                    const D3D_SHADER_MACRO* defines,
                                    ID3DBlob** outBlob, std::string* outErrors)
{
    if (!m_d3dCompile) return false;

    ID3DBlob* errorBlob = nullptr;
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY;

    HRESULT hr = m_d3dCompile(
        source, sourceLen, filePath,
        defines, nullptr,  // no includes for now
        entryPoint, profile,
        flags, 0,
        outBlob, &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob && outErrors)
        {
            *outErrors = static_cast<const char*>(errorBlob->GetBufferPointer());
        }
        CompLog("[ENB] Shader compile FAILED: %s::%s (%s)\n", filePath, entryPoint, profile);
        if (errorBlob)
        {
            CompLog("[ENB]   %s\n", static_cast<const char*>(errorBlob->GetBufferPointer()));
            errorBlob->Release();
        }
        return false;
    }

    if (errorBlob)
    {
        // Warnings
        CompLog("[ENB] Shader warnings for %s::%s: %s\n",
                filePath, entryPoint,
                static_cast<const char*>(errorBlob->GetBufferPointer()));
        errorBlob->Release();
    }

    return true;
}

bool EffectCompiler::CreateVertexShader(ID3D11Device* device, ID3DBlob* blob, ID3D11VertexShader** outVS)
{
    HRESULT hr = device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, outVS);
    return SUCCEEDED(hr);
}

bool EffectCompiler::CreatePixelShader(ID3D11Device* device, ID3DBlob* blob, ID3D11PixelShader** outPS)
{
    HRESULT hr = device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, outPS);
    return SUCCEEDED(hr);
}
