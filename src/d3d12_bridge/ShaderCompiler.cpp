// ShaderCompiler: unified HLSL + GLSL -> DXIL / SPIR-V runtime compiler. Original work, MIT.
#include "ShaderCompiler.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>
#include <objidl.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace raw::bridge
{
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace
{
DxcCreateInstanceProc LoadDxc(const std::wstring& given)
{
    static DxcCreateInstanceProc fn = nullptr;
    if (fn) return fn;
    HMODULE m = nullptr;
    if (!given.empty()) m = LoadLibraryExW(given.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m) m = LoadLibraryW(L"dxcompiler.dll");
    if (!m) m = LoadLibraryExW(L"H:\\dxc-1.9.2609-2026-11-21\\bin\\x64\\dxcompiler.dll", nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m) m = LoadLibraryExW(L"C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.26100.0\\x64\\dxcompiler.dll", nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (m) fn = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(m, "DxcCreateInstance"));
    return fn;
}

std::wstring FindToolExe(const wchar_t* exeName)
{
    wchar_t found[MAX_PATH]{};
    if (SearchPathW(nullptr, exeName, nullptr, MAX_PATH, found, nullptr) > 0) return found;
    std::error_code ec;
    if (fs::exists(L"C:\\VulkanSDK", ec)) {
        std::vector<fs::path> sdks;
        for (const auto& e : fs::directory_iterator(L"C:\\VulkanSDK", ec)) {
            if (e.is_directory(ec)) sdks.push_back(e.path());
        }
        std::sort(sdks.begin(), sdks.end(), std::greater<>());
        for (const auto& sdk : sdks) {
            fs::path p = sdk / L"Bin" / exeName;
            if (fs::exists(p, ec)) return p.wstring();
        }
    }
    return L"";
}

bool RunHiddenProcess(const std::wstring& cmdLine, std::string& outLog, DWORD& exitCode)
{
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi{};
    std::wstring mutCmd = cmdLine;
    const BOOL ok = CreateProcessW(nullptr, mutCmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);
    if (!ok) {
        CloseHandle(hRead);
        return false;
    }

    char buf[1024];
    DWORD n = 0;
    while (ReadFile(hRead, buf, sizeof(buf), &n, nullptr) && n > 0) {
        outLog.append(buf, n);
    }
    WaitForSingleObject(pi.hProcess, 15000);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);
    return true;
}

std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(std::max(n, 0)), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string StagePrefix(const wchar_t* target)
{
    if (!target || !target[0] || !target[1]) return "cs";
    std::string p;
    p.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(target[0] & 0xFF))));
    p.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(target[1] & 0xFF))));
    return p;
}

const wchar_t* GlslangStageOf(const wchar_t* target)
{
    const std::string p = StagePrefix(target);
    if (p == "vs") return L"vert";
    if (p == "ps") return L"frag";
    if (p == "gs") return L"geom";
    return L"comp";
}

bool ReadFileBytes(const fs::path& p, std::vector<uint8_t>& out)
{
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in.is_open()) return false;
    const auto sz = in.tellg();
    if (sz <= 0) return false;
    out.resize(size_t(sz));
    in.seekg(0);
    return bool(in.read(reinterpret_cast<char*>(out.data()), sz));
}

bool ReadFileText(const std::wstring& p, std::string& out)
{
    std::ifstream in(fs::path(p), std::ios::binary);
    if (!in.is_open()) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool IsGlslExtension(const std::wstring& path)
{
    std::wstring ext = fs::path(path).extension().wstring();
    for (wchar_t& c : ext) c = static_cast<wchar_t>(std::towlower(c));
    return ext == L".glsl" || ext == L".comp" || ext == L".frag" || ext == L".vert";
}
} // namespace

bool HasVulkanGlslToolchain()
{
    return !FindToolExe(L"glslangValidator.exe").empty() && !FindToolExe(L"spirv-cross.exe").empty();
}

GlslTranspileResult TranspileGlslToHlsl(const std::string& glslSource, const char* entry, const char* stage)
{
    GlslTranspileResult res;
    if (glslSource.empty()) {
        res.errors = "empty GLSL source";
        return res;
    }

    int lx = 8, ly = 8, lz = 1;
    const std::regex reLocal(R"(layout\s*\(([^)]*local_size[^)]*)\)\s*in\s*;)");
    std::smatch m;
    if (std::regex_search(glslSource, m, reLocal)) {
        const std::string args = m[1].str();
        std::smatch d;
        if (std::regex_search(args, d, std::regex(R"(local_size_x\s*=\s*(\d+))"))) lx = std::max(1, std::stoi(d[1].str()));
        if (std::regex_search(args, d, std::regex(R"(local_size_y\s*=\s*(\d+))"))) ly = std::max(1, std::stoi(d[1].str()));
        if (std::regex_search(args, d, std::regex(R"(local_size_z\s*=\s*(\d+))"))) lz = std::max(1, std::stoi(d[1].str()));
    }

    std::string body = glslSource;
    // Strip #version / #extension lines and local_size layout declarations.
    body = std::regex_replace(body, std::regex(R"((^|\r?\n)\s*#\s*(version|extension)[^\r\n]*)"), "$1");
    body = std::regex_replace(body, reLocal, "");

    // Translate simple SSBO buffer blocks: layout(std430, binding = N) [readonly|writeonly] buffer Name { T arr[]; };
    body = std::regex_replace(
        body,
        std::regex(R"(layout\s*\([^)]*binding\s*=\s*(\d+)[^)]*\)\s*(readonly\s+)?buffer\s+\w+\s*\{\s*([\w]+)\s+(\w+)\s*\[\s*\]\s*;\s*\}\s*;)"),
        "RWStructuredBuffer<$3> $4 : register(u$1);");
    // Translate uniform UBO blocks: layout(..., binding = N) uniform Name { ... };
    body = std::regex_replace(
        body,
        std::regex(R"(layout\s*\([^)]*binding\s*=\s*(\d+)[^)]*\)\s*uniform\s+(\w+)\s*\{)"),
        "cbuffer $2 : register(b$1) {");

    // Translate built-in invocation / fragment variables.
    body = std::regex_replace(body, std::regex(R"(\bgl_GlobalInvocationID\b)"), "_raw_gid");
    body = std::regex_replace(body, std::regex(R"(\bgl_LocalInvocationID\b)"), "_raw_gtid");
    body = std::regex_replace(body, std::regex(R"(\bgl_WorkGroupID\b)"), "_raw_wid");
    body = std::regex_replace(body, std::regex(R"(\bgl_FragCoord\b)"), "_raw_fragcoord");

    const std::string ep = (entry && *entry) ? std::string(entry) : std::string("main");
    const std::string st = (stage && *stage) ? std::string(stage) : std::string("cs");

    if (st == "cs") {
        const std::string repl = "[numthreads(" + std::to_string(lx) + ", " + std::to_string(ly) + ", " +
                                 std::to_string(lz) + ")]\nvoid " + ep +
                                 "(uint3 _raw_gid : SV_DispatchThreadID, uint3 _raw_gtid : SV_GroupThreadID, uint3 _raw_wid : SV_GroupID)";
        body = std::regex_replace(body, std::regex(R"(\bvoid\s+main\s*\(\s*(void)?\s*\))"), repl);
    } else if (st == "ps" || st == "fs") {
        // Support `layout(location = 0) out vec4 fragColor;` -> local variable returned as SV_Target0.
        std::string outVar;
        std::smatch om;
        const std::regex reOut(R"(layout\s*\([^)]*location\s*=\s*0[^)]*\)\s*out\s+vec4\s+(\w+)\s*;)");
        if (std::regex_search(body, om, reOut)) {
            outVar = om[1].str();
            body = std::regex_replace(body, reOut, "");
        }
        if (!outVar.empty()) {
            body = std::regex_replace(body, std::regex(R"(\bvoid\s+main\s*\(\s*(void)?\s*\)\s*\{)"),
                                      "float4 " + ep + "(float4 _raw_fragcoord : SV_Position) : SV_Target0 {\n    float4 " + outVar + " = float4(0,0,0,1);");
            const size_t lastBrace = body.rfind('}');
            if (lastBrace != std::string::npos) {
                body.insert(lastBrace, "\n    return " + outVar + ";\n");
            }
        } else {
            body = std::regex_replace(body, std::regex(R"(\bvoid\s+main\s*\(\s*(void)?\s*\))"),
                                      "float4 " + ep + "(float4 _raw_fragcoord : SV_Position) : SV_Target0");
        }
    }

    static const char* kPrelude = R"HLSL(// Auto-transpiled by RAW clean-room GLSL -> HLSL 2021 front-end
typedef float2 vec2; typedef float3 vec3; typedef float4 vec4;
typedef int2 ivec2; typedef int3 ivec3; typedef int4 ivec4;
typedef uint2 uvec2; typedef uint3 uvec3; typedef uint4 uvec4;
typedef bool2 bvec2; typedef bool3 bvec3; typedef bool4 bvec4;
typedef float2x2 mat2; typedef float3x3 mat3; typedef float4x4 mat4;
float  mix(float a, float b, float t) { return lerp(a, b, t); }
float2 mix(float2 a, float2 b, float t) { return lerp(a, b, t); }
float3 mix(float3 a, float3 b, float t) { return lerp(a, b, t); }
float4 mix(float4 a, float4 b, float t) { return lerp(a, b, t); }
float2 mix(float2 a, float2 b, float2 t) { return lerp(a, b, t); }
float3 mix(float3 a, float3 b, float3 t) { return lerp(a, b, t); }
float4 mix(float4 a, float4 b, float4 t) { return lerp(a, b, t); }
float  fract(float x) { return frac(x); }
float2 fract(float2 x) { return frac(x); }
float3 fract(float3 x) { return frac(x); }
float4 fract(float4 x) { return frac(x); }
float  mod(float x, float y) { return x - y * floor(x / y); }
float2 mod(float2 x, float2 y) { return x - y * floor(x / y); }
float3 mod(float3 x, float3 y) { return x - y * floor(x / y); }
float4 mod(float4 x, float4 y) { return x - y * floor(x / y); }
float  inversesqrt(float x) { return rsqrt(x); }
float2 inversesqrt(float2 x) { return rsqrt(x); }
float3 inversesqrt(float3 x) { return rsqrt(x); }
float4 inversesqrt(float4 x) { return rsqrt(x); }
)HLSL";

    res.hlsl = std::string(kPrelude) + "\n" + body;
    res.ok = true;
    return res;
}

CompileResult CompileHlslFromMemory(const std::string& hlslSource, const std::wstring& virtualName,
                                    const wchar_t* entry, const wchar_t* target,
                                    const std::wstring& dxcDll, const wchar_t* define,
                                    ShaderOutput output, const std::wstring& includeDir)
{
    CompileResult r;
    const auto t0 = std::chrono::steady_clock::now();
    DxcCreateInstanceProc create = LoadDxc(dxcDll);
    if (!create) { r.errors = "dxcompiler.dll not found (set [HotReload] Dxc= to its path)"; return r; }
    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcCompiler3> compiler;
    if (FAILED(create(CLSID_DxcUtils, IID_PPV_ARGS(&utils))) || FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))) {
        r.errors = "DxcCreateInstance failed";
        return r;
    }
    ComPtr<IDxcIncludeHandler> inc;
    utils->CreateDefaultIncludeHandler(&inc);
    std::vector<const wchar_t*> args = {virtualName.c_str(), L"-E", entry, L"-T", target, L"-HV", L"2021", L"-Gis", L"-O3",
                                        L"-enable-16bit-types"};
    if (output == ShaderOutput::Spirv) {
        args.push_back(L"-spirv");
        args.push_back(L"-fspv-target-env=vulkan1.3");
        r.backend = "dxc-hlsl->spirv";
    } else {
        args.push_back(L"-Qstrip_debug");
        args.push_back(L"-Qstrip_reflect");
        r.backend = "dxc-hlsl->dxil";
    }
    if (!includeDir.empty()) { args.push_back(L"-I"); args.push_back(includeDir.c_str()); }
    if (define && *define) { args.push_back(L"-D"); args.push_back(define); }

    DxcBuffer buf{hlslSource.data(), hlslSource.size(), DXC_CP_UTF8};
    ComPtr<IDxcResult> res;
    if (FAILED(compiler->Compile(&buf, args.data(), UINT32(args.size()), inc.Get(), IID_PPV_ARGS(&res)))) {
        r.errors = "Compile call failed";
        return r;
    }
    ComPtr<IDxcBlobUtf8> errs;
    res->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errs), nullptr);
    if (errs && errs->GetStringLength()) r.errors.assign(errs->GetStringPointer(), errs->GetStringLength());
    HRESULT status = E_FAIL;
    res->GetStatus(&status);
    ComPtr<IDxcBlob> obj;
    if (SUCCEEDED(status)) res->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&obj), nullptr);
    if (obj && obj->GetBufferSize()) {
        const auto* p = static_cast<const uint8_t*>(obj->GetBufferPointer());
        r.dxil.assign(p, p + obj->GetBufferSize());
        r.ok = true;
    }
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

CompileResult CompileGlslFromMemory(const std::string& glslSource, const std::wstring& virtualName,
                                    const wchar_t* entry, const wchar_t* target,
                                    const std::wstring& dxcDll, const wchar_t* define,
                                    ShaderOutput output, bool preferBuiltinTranspiler)
{
    CompileResult r;
    const auto t0 = std::chrono::steady_clock::now();
    const std::wstring glslang = FindToolExe(L"glslangValidator.exe");
    const std::wstring spvCross = FindToolExe(L"spirv-cross.exe");

    // Path 1: Full Khronos GLSL 450/460 -> SPIR-V (-> spirv-val -> spirv-cross HLSL SM 6.6 -> DXC DXIL)
    if (!preferBuiltinTranspiler && !glslang.empty() && (output == ShaderOutput::Spirv || !spvCross.empty())) {
        static std::atomic<uint64_t> s_seq{1};
        const uint64_t id = s_seq.fetch_add(1, std::memory_order_relaxed);
        const fs::path tmpDir = fs::temp_directory_path() / L"raw_glsl_cache";
        std::error_code ec;
        fs::create_directories(tmpDir, ec);
        const std::wstring stageExt = GlslangStageOf(target);
        const fs::path inPath = tmpDir / (L"s_" + std::to_wstring(id) + L"." + stageExt);
        const fs::path spvPath = tmpDir / (L"s_" + std::to_wstring(id) + L".spv");
        const fs::path hlslPath = tmpDir / (L"s_" + std::to_wstring(id) + L".hlsl");

        {
            std::ofstream out(inPath, std::ios::binary | std::ios::trunc);
            out.write(glslSource.data(), std::streamsize(glslSource.size()));
        }
        std::wstring cmd = L"\"" + glslang + L"\" -V --target-env vulkan1.3 -S " + stageExt;
        if (define && *define) cmd += L" -D" + std::wstring(define);
        cmd += L" -o \"" + spvPath.wstring() + L"\" \"" + inPath.wstring() + L"\"";

        std::string log;
        DWORD code = 1;
        const bool ran = RunHiddenProcess(cmd, log, code);
        if (!ran || code != 0) {
            r.errors = log.empty() ? "glslangValidator failed" : log;
            r.backend = "glsl-khronos-spirv";
            fs::remove(inPath, ec);
            fs::remove(spvPath, ec);
            r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            return r;
        }

        if (output == ShaderOutput::Spirv) {
            r.ok = ReadFileBytes(spvPath, r.dxil);
            r.errors = log;
            r.backend = "glslang-spirv";
            fs::remove(inPath, ec);
            fs::remove(spvPath, ec);
            r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            return r;
        }

        // Cross-compile validated SPIR-V -> HLSL SM 6.6 via spirv-cross, then compile to signed retail DXIL via DXC!
        std::wstring crossCmd = L"\"" + spvCross + L"\" \"" + spvPath.wstring() +
                                L"\" --hlsl --shader-model 66 --output \"" + hlslPath.wstring() + L"\"";
        std::string crossLog;
        DWORD crossCode = 1;
        const bool crossRan = RunHiddenProcess(crossCmd, crossLog, crossCode);
        std::string crossHlsl;
        const bool haveHlsl = crossRan && crossCode == 0 && ReadFileText(hlslPath.wstring(), crossHlsl);
        fs::remove(inPath, ec);
        fs::remove(spvPath, ec);
        fs::remove(hlslPath, ec);
        if (!haveHlsl) {
            r.errors = crossLog.empty() ? "spirv-cross failed" : crossLog;
            r.backend = "glsl-khronos-spirv-cross";
            r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            return r;
        }
        r = CompileHlslFromMemory(crossHlsl, virtualName, L"main", target, dxcDll, nullptr, ShaderOutput::Dxil);
        r.backend = "glsl-khronos-spirv->dxc";
        r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return r;
    }

    // Path 2: Zero-external-dependency clean-room GLSL -> HLSL 2021 transpiler -> DXC (DXIL or SPIR-V)
    const std::string epUtf8 = WideToUtf8(entry ? entry : L"main");
    const std::string stPref = StagePrefix(target);
    const auto tr = TranspileGlslToHlsl(glslSource, epUtf8.c_str(), stPref.c_str());
    if (!tr.ok) {
        r.errors = tr.errors;
        r.backend = "glsl-builtin";
        return r;
    }
    r = CompileHlslFromMemory(tr.hlsl, virtualName, entry ? entry : L"main", target, dxcDll, define, output);
    r.backend = (output == ShaderOutput::Spirv) ? "glsl-builtin->dxc-spirv" : "glsl-builtin->dxc-dxil";
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

CompileResult CompileHlsl(const std::wstring& path, const wchar_t* entry, const wchar_t* target,
                          const std::wstring& dxcDll, const wchar_t* define, ShaderOutput output)
{
    std::string src;
    if (!ReadFileText(path, src)) {
        CompileResult r;
        r.errors = "cannot read the shader file";
        return r;
    }
    const size_t sl = path.find_last_of(L"\\/");
    const std::wstring dir = (sl != std::wstring::npos) ? path.substr(0, sl) : L"";
    return CompileHlslFromMemory(src, path, entry, target, dxcDll, define, output, dir);
}

CompileResult CompileGlsl(const std::wstring& path, const wchar_t* entry, const wchar_t* target,
                          const std::wstring& dxcDll, const wchar_t* define, ShaderOutput output)
{
    std::string src;
    if (!ReadFileText(path, src)) {
        CompileResult r;
        r.errors = "cannot read the GLSL shader file";
        return r;
    }
    return CompileGlslFromMemory(src, path, entry, target, dxcDll, define, output, false);
}

CompileResult CompileShaderAuto(const std::wstring& path, const wchar_t* entry, const wchar_t* target,
                                const std::wstring& dxcDll, const wchar_t* define, ShaderOutput output)
{
    if (IsGlslExtension(path)) return CompileGlsl(path, entry, target, dxcDll, define, output);
    return CompileHlsl(path, entry, target, dxcDll, define, output);
}

} // namespace raw::bridge
