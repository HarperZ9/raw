// Tonemap: see Tonemap.h. Original work, MIT.
#include "Tonemap.h"
#include "TonemapPs.h"
#include "Overlay.h"
#include "ProxyLog.h"
#include "SceneLights.h"
#include "ShaderManager.h"

#include <d3dcompiler.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
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
constexpr UINT kCbBytes = 112;   // 28 floats (Tonemap.hlsl, cbuffer Raw: RA, RB, RC, RX, RO, RP, RQ)

using CompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

struct LutEntry { std::string name; std::wstring path; };

struct State
{
    TonemapSettings set;
    ID3D11PixelShader* ps = nullptr;
    std::vector<ID3D11PixelShader*> retired;   // earlier live shaders: kept alive, the game may still hold one bound
    ID3D11Buffer* cb = nullptr;
    ID3D11Buffer* saved = nullptr;
    ID3D11ShaderResourceView* savedLutSrv = nullptr;
    ID3D11Texture2D* lutTex = nullptr;
    ID3D11ShaderResourceView* lutSrv = nullptr;
    int loadedLutIdx = -1;
    std::vector<LutEntry> luts;
    bool lutsScanned = false;
    bool tried = false, swapped = false, logged = false;
    CompileFn compile = nullptr;
    std::wstring file;                 // the live source (empty: the built-in text only)
    fs::file_time_type fileTime{};
    uint64_t draws = 0;
    uint32_t builds = 0;
};
State& S() { static State s; return s; }

void ScanLutsUnlocked(State& s)
{
    s.luts.clear();
    s.lutsScanned = true;
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    fs::path dir = fs::path(exe).parent_path() / L"raw_luts";
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        auto ext = entry.path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (ext != L".png") continue;
        s.luts.push_back({entry.path().stem().string(), entry.path().wstring()});
    }
    std::sort(s.luts.begin(), s.luts.end(), [](const LutEntry& a, const LutEntry& b) { return a.name < b.name; });
    Log("[Tonemap] scanned %zu 3D PNG LUTs from raw_luts\\", s.luts.size());
}

void EnsureLutLoaded(State& s, ID3D11Device* dev, int idx)
{
    if (!s.lutsScanned) ScanLutsUnlocked(s);
    if (idx == s.loadedLutIdx) return;
    if (s.lutSrv) { s.lutSrv->Release(); s.lutSrv = nullptr; }
    if (s.lutTex) { s.lutTex->Release(); s.lutTex = nullptr; }
    s.loadedLutIdx = idx;
    if (idx <= 0 || idx > static_cast<int>(s.luts.size()) || !dev) return;

    const std::wstring& path = s.luts[idx - 1].path;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* fac = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac))) || !fac) return;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    bool ok = false;
    if (SUCCEEDED(fac->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) &&
        SUCCEEDED(fac->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        UINT w = 0, h = 0;
        conv->GetSize(&w, &h);
        if (w >= 256 && h >= 16) {
            std::vector<uint8_t> px(size_t(w) * h * 4);
            if (SUCCEEDED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(px.size()), px.data()))) {
                D3D11_TEXTURE2D_DESC td{};
                td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
                td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
                td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA init{px.data(), w * 4, 0};
                if (SUCCEEDED(dev->CreateTexture2D(&td, &init, &s.lutTex)) &&
                    SUCCEEDED(dev->CreateShaderResourceView(s.lutTex, nullptr, &s.lutSrv))) {
                    ok = true;
                    Log("[Tonemap] loaded 3D LUT #%d '%s' (%ux%u)", idx, s.luts[idx - 1].name.c_str(), w, h);
                }
            }
        }
    }
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    fac->Release();
    if (!ok) Log("[Tonemap] failed to load LUT #%d '%s'", idx, s.luts[idx - 1].name.c_str());
}

// The live file's text, or the built-in shader's when there is no file.
std::string Source(State& s, bool* fromFile)
{
    *fromFile = false;
    std::error_code ec;
    if (s.file.empty() || !fs::exists(s.file, ec)) {
        std::string built;
        for (size_t k = 0; kTonemapHlslChunks[k]; ++k) built += kTonemapHlslChunks[k];
        return built;
    }
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

TonemapSettings Tonemap_Get()
{
    return S().set;
}

uint32_t Tonemap_LutCount()
{
    State& s = S();
    if (!s.lutsScanned) ScanLutsUnlocked(s);
    return static_cast<uint32_t>(s.luts.size());
}

std::string Tonemap_LutName(int idx)
{
    State& s = S();
    if (!s.lutsScanned) ScanLutsUnlocked(s);
    if (idx <= 0 || idx > static_cast<int>(s.luts.size())) return "None (Built-In)";
    return s.luts[idx - 1].name;
}

void Tonemap_RescanLuts()
{
    State& s = S();
    s.loadedLutIdx = -1;
    ScanLutsUnlocked(s);
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
    if (s.set.lutIndex != s.loadedLutIdx) {
        ID3D11Device* dev = nullptr;
        real->GetDevice(&dev);
        if (dev) { EnsureLutLoaded(s, dev, s.set.lutIndex); dev->Release(); }
    }
    D3D11_MAPPED_SUBRESOURCE mp{};
    if (FAILED(real->Map(s.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mp))) return;
    const TonemapSettings& t = s.set;
    // The last value is the exposure factor for this frame's kind of cell: ToneX5 in an interior (a cell with no sun
    // cascades, from the decoded lighting), 1 outside. The game lights its interiors dark on purpose: the Bannered
    // Mare meters 0.094 against a clear night's 0.22 and still reads darker in the game's frame.
    const auto& sl = SceneLights_Frame();
    const bool interior = !sl.hasCascades;
    const float inside = interior && t.x[5] > 0.0f ? t.x[5] : 1.0f;
    const float lutStr = (s.lutSrv && t.lutIndex > 0) ? t.lutStrength : 0.0f;
    // Smoothed horizontal player movement speed (world units/frame) for optional running headbob & radial speed blur.
    static float s_emaSpeed = 0.0f;
    const float rawStep = std::sqrt(sl.cameraStep[0] * sl.cameraStep[0] + sl.cameraStep[1] * sl.cameraStep[1]);
    const float clampedStep = rawStep > 45.0f ? 0.0f : std::min(rawStep, 18.0f);   // ignore teleports/fast-travel
    s_emaSpeed = s_emaSpeed * 0.86f + clampedStep * 0.14f;
    const float v[28] = {float(t.mode), t.bias, t.minLum, t.maxLum, float(t.curve), t.key, t.white, float(t.view),
                         t.adapt, t.ref, t.x[0], t.x[1], t.x[2], t.x[3], t.x[4], inside,
                         lutStr, float(t.overlayMask), float(s.draws & 63u), s_emaSpeed,
                         t.bloomScale, t.sharpenStr, t.grainStr, t.vignetteStr,
                         t.caStr, t.radialBlurStr, t.headbobStr, t.lensStr};
    std::memcpy(mp.pData, v, sizeof v);
    real->Unmap(s.cb, 0);
    real->PSGetConstantBuffers(5, 1, &s.saved);
    real->PSSetConstantBuffers(5, 1, &s.cb);
    real->PSGetShaderResources(5, 1, &s.savedLutSrv);
    if (s.lutSrv) real->PSSetShaderResources(5, 1, &s.lutSrv);
    s.swapped = true;
    if (!s.logged) { s.logged = true; Log("[Tonemap] first tone map draw with RAW's shader"); }
}

void Tonemap_AfterDraw(ID3D11DeviceContext* real)
{
    State& s = S();
    if (!s.swapped) return;
    real->PSSetConstantBuffers(5, 1, &s.saved);
    if (s.saved) { s.saved->Release(); s.saved = nullptr; }
    real->PSSetShaderResources(5, 1, &s.savedLutSrv);
    if (s.savedLutSrv) { s.savedLutSrv->Release(); s.savedLutSrv = nullptr; }
    s.swapped = false;
}

} // namespace SB::Proxy
