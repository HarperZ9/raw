#pragma once
// ShaderCompiler: Unified HLSL + GLSL -> DXIL / SPIR-V runtime compiler (docs/HOT_RELOAD.md).
// Supports:
//   1) Direct HLSL SM 6.6 / 6.9 -> DXIL (D3D12) and SPIR-V (Vulkan / The Playground / spirv-val).
//   2) Full GLSL 450/460 -> SPIR-V -> HLSL SM 6.6 -> DXIL via Khronos glslangValidator + spirv-cross
//      when Vulkan SDK tools are installed, with an automatic zero-dependency clean-room
//      TranspileGlslToHlsl() fallback so end users never need the Vulkan SDK installed.
// Original work, MIT.
#include <cstdint>
#include <string>
#include <vector>

namespace raw::bridge
{
enum class ShaderOutput
{
    Dxil = 0,
    Spirv = 1,
};

struct CompileResult
{
    bool ok = false;
    std::vector<uint8_t> dxil;   // DXIL bytes when ShaderOutput::Dxil, or SPIR-V bytes when ShaderOutput::Spirv
    std::string errors;          // compiler / validator diagnostics (warnings included)
    std::string backend;         // e.g. "dxc-hlsl", "glsl-builtin->dxc", "glsl-khronos-spirv->dxc", "glslang-spirv"
    double ms = 0;
};

struct GlslTranspileResult
{
    bool ok = false;
    std::string hlsl;
    std::string errors;
};

// Zero-dependency clean-room GLSL 450/460 -> HLSL 2021 transpiler (compute & fragment stages).
GlslTranspileResult TranspileGlslToHlsl(const std::string& glslSource, const char* entry = "main",
                                        const char* stage = "cs");

// Returns true when glslangValidator.exe and spirv-cross.exe are available on PATH or in C:\VulkanSDK.
bool HasVulkanGlslToolchain();

// Compiles in-memory HLSL to signed DXIL or Vulkan 1.3 SPIR-V.
CompileResult CompileHlslFromMemory(const std::string& hlslSource, const std::wstring& virtualName,
                                    const wchar_t* entry, const wchar_t* target,
                                    const std::wstring& dxcDll = L"", const wchar_t* define = nullptr,
                                    ShaderOutput output = ShaderOutput::Dxil,
                                    const std::wstring& includeDir = L"");

// Compiles in-memory GLSL 450/460 to signed DXIL (via Khronos SPIR-V cross-compile or built-in transpiler)
// or directly to Vulkan 1.3 SPIR-V.
CompileResult CompileGlslFromMemory(const std::string& glslSource, const std::wstring& virtualName,
                                    const wchar_t* entry, const wchar_t* target,
                                    const std::wstring& dxcDll = L"", const wchar_t* define = nullptr,
                                    ShaderOutput output = ShaderOutput::Dxil,
                                    bool preferBuiltinTranspiler = false);

// path: the .hlsl file; its folder is the include folder. target: "ps_6_6", "vs_6_6", "cs_6_6".
// define: optional preprocessor macro (e.g. L"FROXEL_INT=1").
CompileResult CompileHlsl(const std::wstring& path, const wchar_t* entry, const wchar_t* target,
                          const std::wstring& dxcDll = L"", const wchar_t* define = nullptr,
                          ShaderOutput output = ShaderOutput::Dxil);

// path: a .glsl / .comp / .frag / .vert file -> compiled to DXIL or SPIR-V.
CompileResult CompileGlsl(const std::wstring& path, const wchar_t* entry, const wchar_t* target,
                          const std::wstring& dxcDll = L"", const wchar_t* define = nullptr,
                          ShaderOutput output = ShaderOutput::Dxil);

// Auto-dispatches by file extension (.glsl/.comp/.frag/.vert vs .hlsl/.hlsli).
CompileResult CompileShaderAuto(const std::wstring& path, const wchar_t* entry, const wchar_t* target,
                                const std::wstring& dxcDll = L"", const wchar_t* define = nullptr,
                                ShaderOutput output = ShaderOutput::Dxil);
}
