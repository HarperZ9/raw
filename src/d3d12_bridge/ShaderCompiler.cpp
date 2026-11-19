// ShaderCompiler: see the header. Original work, MIT.
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
#include <chrono>

namespace raw::bridge
{
using Microsoft::WRL::ComPtr;

namespace
{
DxcCreateInstanceProc Load(const std::wstring& given)
{
    static DxcCreateInstanceProc fn = nullptr;
    if (fn) return fn;
    HMODULE m = nullptr;
    // The altered search path lets dxcompiler find dxil.dll beside it, which signs the output.
    if (!given.empty()) m = LoadLibraryExW(given.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m) m = LoadLibraryW(L"dxcompiler.dll");
    if (!m) m = LoadLibraryExW(L"C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.26100.0\\x64\\dxcompiler.dll", nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (m) fn = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(m, "DxcCreateInstance"));
    return fn;
}
} // namespace

CompileResult CompileHlsl(const std::wstring& path, const wchar_t* entry, const wchar_t* target, const std::wstring& dxcDll)
{
    CompileResult r;
    const auto t0 = std::chrono::steady_clock::now();
    DxcCreateInstanceProc create = Load(dxcDll);
    if (!create) { r.errors = "dxcompiler.dll not found (set [HotReload] Dxc= to its path)"; return r; }
    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcCompiler3> compiler;
    if (FAILED(create(CLSID_DxcUtils, IID_PPV_ARGS(&utils))) || FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))) {
        r.errors = "DxcCreateInstance failed";
        return r;
    }
    ComPtr<IDxcBlobEncoding> src;
    if (FAILED(utils->LoadFile(path.c_str(), nullptr, &src))) { r.errors = "cannot read the shader file"; return r; }
    ComPtr<IDxcIncludeHandler> inc;
    utils->CreateDefaultIncludeHandler(&inc);
    const std::wstring dir = path.substr(0, path.find_last_of(L"\\/"));
    // The build's flags (CMakeLists.txt RAW_DXC_FLAGS): HLSL 2021, IEEE strictness, O3, no debug or reflection.
    const wchar_t* args[] = {path.c_str(), L"-E", entry, L"-T", target, L"-HV", L"2021", L"-Gis", L"-O3",
                             L"-Qstrip_debug", L"-Qstrip_reflect", L"-I", dir.c_str()};
    DxcBuffer buf{src->GetBufferPointer(), src->GetBufferSize(), DXC_CP_ACP};
    ComPtr<IDxcResult> res;
    if (FAILED(compiler->Compile(&buf, args, UINT32(sizeof args / sizeof args[0]), inc.Get(), IID_PPV_ARGS(&res)))) {
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

} // namespace raw::bridge
