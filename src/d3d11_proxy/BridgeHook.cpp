//=============================================================================
//  BridgeHook: wires raw::bridge::D3D12Bridge into the live frame.
//
//  End of the opaque pass (GeometryMain -> next phase): copy the game's depth
//  into the shared depth texture.
//  Entry to the UI phase: copy the scene, run the D3D12 pass, copy the result
//  back into the backbuffer before Scaleform draws the HUD.
//  If a frame never reaches the UI phase (menus, loading), the pass runs at
//  Present instead, when FallbackAtPresent=1.
//
//  d3d11_proxy.ini:
//    [D3D12Bridge]
//    Enabled=0            1 turns the bridge on (default off)
//    Mode=1               0 identity, 1 depth haze, 2 linear-depth view
//    Density=0.00012      haze per game unit
//    FogR/FogG/FogB       haze colour, 0..1
//    MaxOpacity=0.85
//    Near=15  Far=353840  projection planes for depth linearisation
//    Marker=1             four swatches top-left that rotate each frame
//    FallbackAtPresent=1
//    DebugLayer=0
//    TimingLogEvery=600   frames between timing lines in d3d11_proxy.log
//
//  Original work, MIT.
//=============================================================================
#include "BridgeHook.h"
#include "D3D12Bridge.h"
#include "DepthIntercept.h"
#include "ProxyAPI.h"
#include "ProxyLog.h"
#include "RenderPhaseDetector.h"
#include "WrappedContext.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace SB::Proxy
{
namespace
{
struct HookConfig
{
    bool enabled = false, fallbackAtPresent = true, debugLayer = false;
    int  timingLogEvery = 600;
    int  captureFrame = 0;     // writes d3d12_bridge_capture.bmp after this frame (0 = never)
    raw::bridge::PassConstants k;
};

HookConfig               s_cfg;
raw::bridge::D3D12Bridge* s_bridge = nullptr;   // leaked at exit on purpose (no work under loader lock)
bool     s_started = false, s_ranThisFrame = false;
int      s_initFailures = 0;
uint64_t s_frame = 0, s_atUI = 0, s_atPresent = 0, s_depthCaptures = 0;
raw::bridge::BridgeTimings s_sum{};
uint32_t s_samples = 0;

float IniFloat(const char* key, float def, const std::string& ini)
{
    char buf[64]{};
    GetPrivateProfileStringA("D3D12Bridge", key, "", buf, sizeof(buf), ini.c_str());
    return buf[0] ? std::strtof(buf, nullptr) : def;
}

void LoadHookConfig()
{
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string ini(path);
    ini = ini.substr(0, ini.rfind('\\') + 1) + "d3d11_proxy.ini";
    auto i = [&](const char* k, int d) { return GetPrivateProfileIntA("D3D12Bridge", k, d, ini.c_str()); };
    s_cfg.enabled = i("Enabled", 0) != 0;
    s_cfg.fallbackAtPresent = i("FallbackAtPresent", 1) != 0;
    s_cfg.debugLayer = i("DebugLayer", 0) != 0;
    s_cfg.timingLogEvery = i("TimingLogEvery", 600);
    s_cfg.captureFrame = i("CaptureFrame", 0);
    auto& k = s_cfg.k;
    k.mode = static_cast<float>(i("Mode", 1));
    k.marker = static_cast<float>(i("Marker", 1));
    k.density = IniFloat("Density", k.density, ini);
    k.fogColor[0] = IniFloat("FogR", k.fogColor[0], ini);
    k.fogColor[1] = IniFloat("FogG", k.fogColor[1], ini);
    k.fogColor[2] = IniFloat("FogB", k.fogColor[2], ini);
    k.maxOpacity = IniFloat("MaxOpacity", k.maxOpacity, ini);
    k.nearZ = IniFloat("Near", k.nearZ, ini);
    k.farZ = IniFloat("Far", k.farZ, ini);
    Log("[D3D12Bridge] config: enabled=%d mode=%.0f density=%g near=%g far=%g marker=%.0f",
        s_cfg.enabled, k.mode, k.density, k.nearZ, k.farZ, k.marker);
}

bool EnsureBridge(ID3D11Texture2D* backbuffer)
{
    D3D11_TEXTURE2D_DESC bb{};
    backbuffer->GetDesc(&bb);
    if (bb.SampleDesc.Count != 1) return false;
    if (s_bridge->Matches(bb.Width, bb.Height, bb.Format)) return true;
    if (s_initFailures >= 3) return false;

    auto* pi = PG_GetProxyInterface();
    if (!pi || !pi->device || !pi->context) return false;
    raw::bridge::BridgeConfig c;
    c.width = bb.Width;
    c.height = bb.Height;
    c.colorFormat = bb.Format;
    c.debugLayer = s_cfg.debugLayer;
    c.probeStages = s_initFailures == 0;
    if (s_bridge->Initialize(pi->device, pi->context, c, &Log)) return true;
    ++s_initFailures;
    return false;
}

void RunOnBackbuffer(bool atUI)
{
    auto* pi = PG_GetProxyInterface();
    if (!pi || !pi->swapChain) return;
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(pi->swapChain->GetBuffer(0, IID_PPV_ARGS(&bb))) || !bb) return;
    if (EnsureBridge(bb)) {
        raw::bridge::PassConstants k = s_cfg.k;
        k.frame = static_cast<float>(s_frame & 0xFFFF);
        if (s_bridge->Execute(bb, bb, k)) {
            s_ranThisFrame = true;
            ++(atUI ? s_atUI : s_atPresent);
        }
    }
    bb->Release();
}

// Writes the backbuffer (8-bit RGBA or BGRA) to a 32-bit BMP next to the exe:
// what the game is about to present, read from the GPU, not from the desktop.
void CaptureBackbuffer()
{
    auto* pi = PG_GetProxyInterface();
    if (!pi || !pi->swapChain || !pi->device || !pi->context) return;
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(pi->swapChain->GetBuffer(0, IID_PPV_ARGS(&bb))) || !bb) return;
    D3D11_TEXTURE2D_DESC d{};
    bb->GetDesc(&d);
    const bool bgra = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    const bool rgba = d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    ID3D11Texture2D* st = nullptr;
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
    HRESULT chr = (bgra || rgba) ? pi->device->CreateTexture2D(&d, nullptr, &st) : E_INVALIDARG;
    Log("[D3D12Bridge] capture: format %d, staging 0x%08X", (int)d.Format, (unsigned)chr);
    if (SUCCEEDED(chr)) {
        ID3D11DeviceContext* c = pi->context;   // the swap chain device's own context
        c->CopyResource(st, bb);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(c->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
            char path[MAX_PATH]{};
            GetModuleFileNameA(nullptr, path, MAX_PATH);
            std::string out(path);
            out = out.substr(0, out.rfind('\\') + 1) + "d3d12_bridge_capture.bmp";
            FILE* f = nullptr;
            if (fopen_s(&f, out.c_str(), "wb") == 0 && f) {
                const uint32_t img = d.Width * d.Height * 4;
                uint8_t hdr[54]{'B', 'M'};
                auto put = [&](int at, uint32_t v) { memcpy(hdr + at, &v, 4); };
                put(2, 54 + img); put(10, 54); put(14, 40); put(18, d.Width);
                put(22, static_cast<uint32_t>(-static_cast<int32_t>(d.Height)));   // top-down
                hdr[26] = 1; hdr[28] = 32; put(34, img);
                fwrite(hdr, 1, 54, f);
                std::string row(d.Width * 4, char(0));
                for (UINT y = 0; y < d.Height; ++y) {
                    const uint8_t* src = static_cast<const uint8_t*>(m.pData) + y * m.RowPitch;
                    for (UINT x = 0; x < d.Width; ++x) {   // BMP wants B,G,R,A
                        const uint8_t* p = src + x * 4;
                        row[x * 4 + 0] = static_cast<char>(bgra ? p[0] : p[2]);
                        row[x * 4 + 1] = static_cast<char>(p[1]);
                        row[x * 4 + 2] = static_cast<char>(bgra ? p[2] : p[0]);
                        row[x * 4 + 3] = static_cast<char>(p[3]);
                    }
                    fwrite(row.data(), 1, row.size(), f);
                }
                fclose(f);
                Log("[D3D12Bridge] captured backbuffer %ux%u to %s", d.Width, d.Height, out.c_str());
            }
            c->Unmap(st, 0);
        }
        st->Release();
    }
    bb->Release();
}

void OnPhase(RenderPhase oldPhase, RenderPhase newPhase)
{
    if (!s_cfg.enabled || !s_bridge) return;
    if (oldPhase == RenderPhase::GeometryMain && newPhase != RenderPhase::GeometryMain &&
        s_bridge->Ready()) {
        ID3D11ShaderResourceView* srv = DepthIntercept_GetSRV();
        if (!srv) if (auto* pi = PG_GetProxyInterface()) srv = pi->gameDepthSRV;   // copy-based capture
        if (s_bridge->CaptureDepth(srv)) ++s_depthCaptures;
    }
    if (newPhase == RenderPhase::UI && oldPhase != RenderPhase::UI && !s_ranThisFrame) {
        static int logged = 0;
        if (logged++ < 10)
            Log("[D3D12Bridge] UI phase entered at frame %llu (tid=%lu)",
                (unsigned long long)s_frame, GetCurrentThreadId());
        RunOnBackbuffer(true);
    }
}

void AccumulateTimings()
{
    const uint64_t n = s_bridge->FramesExecuted();
    raw::bridge::BridgeTimings t;
    if (n < 3 || !s_bridge->ReadTimings(n - 3, false, &t)) return;
    s_sum.depthCopyMs += t.depthCopyMs; s_sum.copyInMs += t.copyInMs; s_sum.copyOutMs += t.copyOutMs;
    s_sum.stallMs += t.stallMs; s_sum.passMs += t.passMs; s_sum.syncMs += t.syncMs;
    s_sum.bridgeCostMs += t.bridgeCostMs;
    if (++s_samples < static_cast<uint32_t>(s_cfg.timingLogEvery > 0 ? s_cfg.timingLogEvery : 600)) return;
    const double inv = 1.0 / s_samples;
    Log("[D3D12Bridge] mean over %u frames: bridge cost %.3f ms (depth copy %.3f, copy in %.3f, "
        "copy out %.3f, fence sync %.3f), D3D12 pass %.3f ms, D3D11 stall %.3f ms; "
        "runs at UI=%llu at Present=%llu, depth captures=%llu",
        s_samples, s_sum.bridgeCostMs * inv, s_sum.depthCopyMs * inv, s_sum.copyInMs * inv,
        s_sum.copyOutMs * inv, s_sum.syncMs * inv, s_sum.passMs * inv, s_sum.stallMs * inv,
        (unsigned long long)s_atUI, (unsigned long long)s_atPresent,
        (unsigned long long)s_depthCaptures);
    s_sum = {};
    s_samples = 0;
}
} // namespace

void BridgeHook_OnDeviceCreated(ID3D11Device* realDevice, ID3D11DeviceContext* realContext)
{
    if (PG_IsSafeMode()) return;
    if (!s_started) { s_started = true; LoadHookConfig(); }
    if (s_cfg.enabled) raw::bridge::ProbeFenceSupport(realDevice, realContext, &Log, "after device creation");
}

void BridgeHook_OnPresent()
{
    if (PG_IsSafeMode()) return;
    if (!s_started) {
        s_started = true;
        LoadHookConfig();
    }
    if (s_cfg.enabled && !s_bridge) {
        if (auto* pi = PG_GetProxyInterface())
            raw::bridge::ProbeFenceSupport(pi->device, pi->context, &Log, "first Present");
        s_bridge = new raw::bridge::D3D12Bridge();
        RenderPhaseDetector::Get().RegisterPhaseChangeCallback(OnPhase);
    }
    if (!s_cfg.enabled || !s_bridge) return;
    if (s_frame < 8 || (s_frame % 300) == 0)
        Log("[D3D12Bridge] heartbeat t=%llums frame %llu: executed=%llu (UI %llu, Present %llu), depth %llu, "
            "fence D3D12=%llu waited=%llu completed=%llu tid=%lu",
            (unsigned long long)GetTickCount64(), (unsigned long long)s_frame,
            (unsigned long long)s_bridge->FramesExecuted(),
            (unsigned long long)s_atUI, (unsigned long long)s_atPresent,
            (unsigned long long)s_depthCaptures, (unsigned long long)s_bridge->LastD3D12Value(),
            (unsigned long long)s_bridge->LastWaitedValue(),
            (unsigned long long)s_bridge->CompletedValue(), GetCurrentThreadId());
    if (!s_ranThisFrame && s_cfg.fallbackAtPresent) RunOnBackbuffer(false);
    if (s_bridge->Ready()) AccumulateTimings();
    if (s_cfg.captureFrame > 0 && s_frame == static_cast<uint64_t>(s_cfg.captureFrame)) CaptureBackbuffer();
    s_ranThisFrame = false;
    ++s_frame;
}

} // namespace SB::Proxy
