//=============================================================================
//  test_shaders.cpp — Verify all ENB .fx files compile with D3DCompile
//
//  Standalone console test. Loads each .fx file from the original ENB
//  distribution, parses techniques, and compiles all VS/PS entry points.
//  Uses D3DCompile from the system d3dcompiler DLL (no game needed).
//=============================================================================

#include <Windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

// D3DCompile function pointer
typedef HRESULT(WINAPI* PFN_D3DCompile)(
    LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
    LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

static PFN_D3DCompile g_d3dCompile = nullptr;

struct ShaderEntry
{
    const char* file;
    const char* vsEntry;
    const char* psEntry;
    const char* vsProfile;
    const char* psProfile;
};

// Read file into string
static std::string ReadFile(const char* path)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "";
    DWORD sz = GetFileSize(h, nullptr);
    std::string data(sz, '\0');
    DWORD read = 0;
    ReadFile(h, data.data(), sz, &read, nullptr);
    CloseHandle(h);
    return data;
}

static bool CompileEntry(const char* source, size_t len, const char* file,
                          const char* entry, const char* profile,
                          const D3D_SHADER_MACRO* defines)
{
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;

    HRESULT hr = g_d3dCompile(source, len, file, defines, nullptr,
                               entry, profile,
                               D3DCOMPILE_OPTIMIZATION_LEVEL3 |
                               D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY,
                               0, &code, &errors);

    if (FAILED(hr))
    {
        printf("    FAIL %s::%s (%s): 0x%08X\n", file, entry, profile, hr);
        if (errors)
        {
            printf("      %s\n", static_cast<const char*>(errors->GetBufferPointer()));
            errors->Release();
        }
        return false;
    }

    DWORD codeSize = code ? static_cast<DWORD>(code->GetBufferSize()) : 0;
    printf("    PASS %s::%s (%s) -> %u bytes\n", file, entry, profile, codeSize);

    if (code) code->Release();
    if (errors) errors->Release();
    return true;
}

int main(int argc, char* argv[])
{
    printf("======================================\n");
    printf("  ENB Shader Compilation Test\n");
    printf("======================================\n\n");

    // Find shader directory
    const char* shaderDir = nullptr;
    char defaultDir[MAX_PATH];

    if (argc > 1)
    {
        shaderDir = argv[1];
    }
    else
    {
        // Look relative to exe for the original ENB shader files
        GetModuleFileNameA(nullptr, defaultDir, MAX_PATH);
        char* slash = strrchr(defaultDir, '\\');
        if (slash) *slash = '\0';

        // Try several paths
        static const char* searchPaths[] = {
            "..\\..\\..\\..\\..\\ENB Binary Dump\\GTAV\\extracted\\WrapperVersion",
            "..\\..\\ENB Binary Dump\\GTAV\\extracted\\WrapperVersion",
            ".",
        };

        for (const char* sp : searchPaths)
        {
            char testPath[MAX_PATH];
            snprintf(testPath, MAX_PATH, "%s\\%s\\enbeffect.fx", defaultDir, sp);
            DWORD attr = GetFileAttributesA(testPath);
            if (attr != INVALID_FILE_ATTRIBUTES)
            {
                snprintf(defaultDir + strlen(defaultDir), MAX_PATH - strlen(defaultDir),
                         "\\%s", sp);
                shaderDir = defaultDir;
                break;
            }
        }
    }

    if (!shaderDir)
    {
        printf("ERROR: Could not find shader directory.\n");
        printf("Usage: test_shaders.exe [path_to_shader_dir]\n");
        return 1;
    }

    printf("Shader directory: %s\n\n", shaderDir);

    // Load D3DCompiler
    static const char* compilerDLLs[] = {
        "d3dcompiler_47.dll", "d3dcompiler_46e.dll", "d3dcompiler_43.dll",
    };

    HMODULE compDLL = nullptr;
    for (const char* dll : compilerDLLs)
    {
        compDLL = LoadLibraryA(dll);
        if (compDLL)
        {
            g_d3dCompile = reinterpret_cast<PFN_D3DCompile>(GetProcAddress(compDLL, "D3DCompile"));
            if (g_d3dCompile)
            {
                printf("Compiler: %s\n\n", dll);
                break;
            }
            FreeLibrary(compDLL);
            compDLL = nullptr;
        }
    }

    if (!g_d3dCompile)
    {
        printf("ERROR: No shader compiler DLL found.\n");
        return 1;
    }

    // Define shaders to test: file, entry points
    struct TestShader
    {
        const char* file;
        std::vector<std::pair<std::string, std::string>> entries; // (entry, profile)
    };

    TestShader shaders[] = {
        { "enbeffect.fx", {
            {"VS_Draw", "vs_5_0"}, {"PS_Draw", "ps_5_0"}, {"PS_DrawOriginal", "ps_5_0"}
        }},
        { "enbbloom.fx", {
            {"VS_Quad", "vs_5_0"}, {"PS_BloomPostPass", "ps_5_0"}
        }},
        { "enblens.fx", {
            {"VS_Quad", "vs_5_0"}, {"PS_LensMix", "ps_5_0"}, {"PS_MixSkyrimLens", "ps_5_0"}
        }},
        { "enbadaptation.fx", {
            {"PS_Downsample", "ps_5_0"}, {"PS_Adaptation", "ps_5_0"}
        }},
        { "enbeffectprepass.fx", {
            {"VS_PostProcess", "vs_5_0"}, {"PS_DrawEdge1", "ps_5_0"}, {"PS_DrawEdge2", "ps_5_0"}
        }},
        { "enbeffectpostpass.fx", {
            {"VS_PostProcess", "vs_5_0"}, {"PS_Sharp", "ps_5_0"}, {"PS_Blur", "ps_5_0"}
        }},
        { "enblightsprite.fx", {
            {"VS_Draw", "vs_4_0"}, {"PS_Draw", "ps_4_0"},
            {"VS_DrawGlow", "vs_4_0"}, {"PS_DrawGlow", "ps_4_0"}
        }},
    };

    int totalTests = 0, passed = 0;

    // Defines for conditional compilation
    D3D_SHADER_MACRO defines[] = {
        { "E_CC_PROCEDURAL", "1" },
        { "POSTPROCESS", "2" },
        { nullptr, nullptr }
    };

    for (auto& shader : shaders)
    {
        char path[MAX_PATH];
        snprintf(path, MAX_PATH, "%s\\%s", shaderDir, shader.file);

        std::string source = ReadFile(path);
        if (source.empty())
        {
            printf("[%s] NOT FOUND: %s\n", shader.file, path);
            continue;
        }

        printf("[%s] (%zu bytes)\n", shader.file, source.size());

        for (auto& [entry, profile] : shader.entries)
        {
            totalTests++;
            if (CompileEntry(source.c_str(), source.size(), shader.file,
                              entry.c_str(), profile.c_str(), defines))
                passed++;
        }
        printf("\n");
    }

    printf("======================================\n");
    printf("  Results: %d/%d shaders compiled\n", passed, totalTests);
    printf("======================================\n");

    if (compDLL) FreeLibrary(compDLL);

    return (passed == totalTests) ? 0 : 1;
}
