// Tonemap: see Tonemap.h. Original work, MIT.
#include "Tonemap.h"
#include "TonemapPs.h"
#include "Overlay.h"
#include "ProxyLog.h"
#include "SceneLights.h"
#include "ShaderManager.h"

#include <d3dcompiler.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace SB::Proxy
{
namespace
{
namespace fs = std::filesystem;
// The game's tone map pixel shader (ps_99d22a3d198a3b69 in the frame captures: the full-screen draw from
// the HDR scene into the 8-bit target, after the luminance chain and before the temporal resolve).
constexpr uint64_t kTonemapHash = 0x99d22a3d198a3b69ull;
constexpr UINT kCbBytes = 64;   // 16 floats (Tonemap.hlsl, cbuffer Raw)

using CompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

struct State
{
    TonemapSettings set;
    ID3D11PixelShader* ps = nullptr;
    std::vector<ID3D11PixelShader*> retired;   // earlier live shaders: kept alive, the game may still hold one bound
    ID3D11Buffer* cb = nullptr;
    ID3D11Buffer* saved = nullptr;
    bool tried = false, swapped = false, logged = false;
    CompileFn compile = nullptr;
    std::wstring file;                 // the live source (empty: the built-in text only)
    fs::file_time_type fileTime{};
    uint64_t draws = 0;
    uint32_t builds = 0;
};
State& S() { static State s; return s; }

// The live file's text, or the built-in shader's when there is no file.
std::string Source(State& s, bool* fromFile)
{
    *fromFile = false;
    std::error_code ec;
    if (s.file.empty() || !fs::exists(s.file, ec)) return kTonemapHlsl;
    std::ifstream in(s.file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    s.fileTime = fs::last_write_time(s.file, ec);
    *fromFile = true;
    return ss.str();
}

// Compiles the current source; on success the new shader replaces the game's from its next bind. A failure keeps the
// shader in use and shows the compiler's text in the overlay.
bool Build(State& s, ID3D11Device* dev)
{
    bool fromFile = false;
    const std::string text = Source(s, &fromFile);
    ID3DBlob* code = nullptr;
    ID3DBlob* errs = nullptr;
    const HRESULT hr = s.compile(text.data(), text.size(), "Tonemap.hlsl", nullptr, nullptr, "main", "ps_5_0",
                                 D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errs);
    if (FAILED(hr) || !code) {
        const std::string e = errs ? static_cast<const char*>(errs->GetBufferPointer()) : "compile failed";
        Log("[Tonemap] compile failed, the shader in use stays: %s", e.c_str());
        Overlay_Set("RAW tone map: Tonemap.hlsl did not compile; the last good one stays.\n" + e);
        if (errs) errs->Release();
        return false;
    }
    if (errs) errs->Release();
    ID3D11PixelShader* ps = nullptr;
    const HRESULT h2 = dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &ps);
    code->Release();
    if (FAILED(h2) || !ps) { Log("[Tonemap] shader creation failed"); return false; }
    if (s.ps) s.retired.push_back(s.ps);
    s.ps = ps;
    if (s.set.enabled) ShaderManager::Get().RegisterPSReplacement(kTonemapHash, s.ps);
    if (s.builds++ > 0) Overlay_Set("");
    Log("[Tonemap] RAW's tone map %s (%s): exposure mode %d, bias %.2f, curve %d", s.builds == 1 ? "replaces the game's" : "reloaded",
        fromFile ? "from the shader folder" : "built in", s.set.mode, s.set.bias, s.set.curve);
    return true;
}
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

void Tonemap_SetSource(const std::wstring& file)
{
    S().file = file;
    Log("[Tonemap] live source: %ls", file.c_str());
}

void Tonemap_OnDevice(ID3D11Device* dev)
{
    State& s = S();
    if (s.tried || !dev || !s.set.enabled) return;
    s.tried = true;
    HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
    s.compile = m ? reinterpret_cast<CompileFn>(GetProcAddress(m, "D3DCompile")) : nullptr;
    if (!s.compile) { Log("[Tonemap] d3dcompiler_47.dll not found: the game's tone map stays"); return; }
    D3D11_BUFFER_DESC bd{kCbBytes, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
    if (FAILED(dev->CreateBuffer(&bd, nullptr, &s.cb))) { Log("[Tonemap] buffer creation failed"); return; }
    Build(s, dev);
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
    bool ours = ps == s.ps;
    for (ID3D11PixelShader* r : s.retired) ours = ours || r == ps;
    if (!ours) return;   // not the tone map draw
    // The live source: a new write time recompiles (one tone map draw a frame: every 30 is about twice a second).
    if (s.compile && !s.file.empty() && ++s.draws % 30 == 0) {
        std::error_code ec;
        const auto t = fs::last_write_time(s.file, ec);
        if (!ec && t != s.fileTime) {
            s.fileTime = t;
            ID3D11Device* dev = nullptr;
            real->GetDevice(&dev);
            if (dev) { Build(s, dev); dev->Release(); }
        }
    }
    if (ps != s.ps) real->PSSetShader(s.ps, nullptr, 0);   // bound before a reload: the current one draws
    D3D11_MAPPED_SUBRESOURCE mp{};
    if (FAILED(real->Map(s.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mp))) return;
    const TonemapSettings& t = s.set;
    // The last value is the exposure factor for this frame's kind of cell: ToneX5 in an interior (a cell with no sun
    // cascades, from the decoded lighting), 1 outside. The game lights its interiors dark on purpose: the Bannered
    // Mare meters 0.094 against a clear night's 0.22 and still reads darker in the game's frame.
    const bool interior = !SceneLights_Frame().hasCascades;
    const float inside = interior && t.x[5] > 0.0f ? t.x[5] : 1.0f;
    const float v[16] = {float(t.mode), t.bias, t.minLum, t.maxLum, float(t.curve), t.key, t.white, float(t.view),
                         t.adapt, t.ref, t.x[0], t.x[1], t.x[2], t.x[3], t.x[4], inside};
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
