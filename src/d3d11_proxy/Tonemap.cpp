// Tonemap: see Tonemap.h. Original work, MIT.
#include "Tonemap.h"
#include "TonemapPs.h"
#include "ProxyLog.h"
#include "ShaderManager.h"

#include <d3dcompiler.h>
#include <cstring>

namespace SB::Proxy
{
namespace
{
// The game's tone map pixel shader (ps_99d22a3d198a3b69 in the frame captures: the full-screen draw from
// the HDR scene into the 8-bit target, after the luminance chain and before the temporal resolve).
constexpr uint64_t kTonemapHash = 0x99d22a3d198a3b69ull;

struct State
{
    TonemapSettings set;
    ID3D11PixelShader* ps = nullptr;
    ID3D11Buffer* cb = nullptr;
    ID3D11Buffer* saved = nullptr;
    bool tried = false, swapped = false, logged = false;
};
State& S() { static State s; return s; }

using CompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
} // namespace

void Tonemap_Set(const TonemapSettings& t)
{
    State& s = S();
    s.set = t;
    if (s.ps) {
        if (t.enabled) ShaderManager::Get().RegisterPSReplacement(kTonemapHash, s.ps);
        else ShaderManager::Get().RemovePSReplacement(kTonemapHash);
    }
}

void Tonemap_OnDevice(ID3D11Device* dev)
{
    State& s = S();
    if (s.tried || !dev || !s.set.enabled) return;
    s.tried = true;
    HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
    auto compile = m ? reinterpret_cast<CompileFn>(GetProcAddress(m, "D3DCompile")) : nullptr;
    if (!compile) { Log("[Tonemap] d3dcompiler_47.dll not found: the game's tone map stays"); return; }
    ID3DBlob* code = nullptr;
    ID3DBlob* errs = nullptr;
    const HRESULT hr = compile(kTonemapHlsl, std::strlen(kTonemapHlsl), "raw_tonemap", nullptr, nullptr, "main", "ps_5_0",
                               D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errs);
    if (FAILED(hr) || !code) {
        Log("[Tonemap] compile failed: %s", errs ? static_cast<const char*>(errs->GetBufferPointer()) : "?");
        if (errs) errs->Release();
        return;
    }
    if (errs) errs->Release();
    const HRESULT h2 = dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &s.ps);
    code->Release();
    D3D11_BUFFER_DESC bd{32, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
    if (FAILED(h2) || FAILED(dev->CreateBuffer(&bd, nullptr, &s.cb))) { Log("[Tonemap] shader or buffer creation failed"); return; }
    ShaderManager::Get().RegisterPSReplacement(kTonemapHash, s.ps);
    Log("[Tonemap] RAW's tone map replaces the game's (ps %016llx): exposure mode %d, bias %.2f, curve %d", (unsigned long long)kTonemapHash,
        s.set.mode, s.set.bias, s.set.curve);
}

void Tonemap_BeforeDraw(ID3D11DeviceContext* real)
{
    State& s = S();
    if (!s.set.enabled) return;
    if (!s.tried) {
        ID3D11Device* dev = nullptr;
        real->GetDevice(&dev);
        Tonemap_OnDevice(dev);   // the replacement binds from the game's next PSSetShader on
        if (dev) dev->Release();
    }
    if (!s.ps) return;
    ID3D11PixelShader* ps = nullptr;
    real->PSGetShader(&ps, nullptr, nullptr);
    if (ps) ps->Release();
    if (ps != s.ps) return;
    D3D11_MAPPED_SUBRESOURCE mp{};
    if (FAILED(real->Map(s.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mp))) return;
    const TonemapSettings& t = s.set;
    const float v[8] = {float(t.mode), t.bias, t.minLum, t.maxLum, float(t.curve), t.key, t.white, float(t.view)};
    std::memcpy(mp.pData, v, sizeof v);
    real->Unmap(s.cb, 0);
    real->PSGetConstantBuffers(5, 1, &s.saved);
    real->PSSetConstantBuffers(5, 1, &s.cb);
    s.swapped = true;
    if (!s.logged) { s.logged = true; Log("[Tonemap] first tone map draw with RAW's shader"); }
}

void Tonemap_AfterDraw(ID3D11DeviceContext* real)
{
    State& s = S();
    if (!s.swapped) return;
    real->PSSetConstantBuffers(5, 1, &s.saved);
    if (s.saved) { s.saved->Release(); s.saved = nullptr; }
    s.swapped = false;
}

} // namespace SB::Proxy
