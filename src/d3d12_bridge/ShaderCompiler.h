#pragma once
// ShaderCompiler: HLSL to DXIL at run time, for hot reload (docs/HOT_RELOAD.md). It loads
// dxcompiler.dll on first use (from the path given, else beside the game, else the Windows SDK)
// and compiles with the same flags the build uses for the bridge's passes, so a reloaded shader
// is the shader the next build would embed. Original work, MIT.
#include <cstdint>
#include <string>
#include <vector>

namespace raw::bridge
{
struct CompileResult
{
    bool ok = false;
    std::vector<uint8_t> dxil;
    std::string errors;   // the compiler's text, warnings included
    double ms = 0;
};

// path: the .hlsl file; its folder is the include folder. target: "ps_6_0", "vs_6_0", "cs_6_5".
CompileResult CompileHlsl(const std::wstring& path, const wchar_t* entry, const wchar_t* target, const std::wstring& dxcDll = L"");
}
