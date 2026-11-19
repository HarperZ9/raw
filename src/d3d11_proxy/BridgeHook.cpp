//=============================================================================
//  BridgeHook: wires raw::bridge::D3D12Bridge into the live frame.
//
//  The hook follows the game's own render targets (docs/FRAME_BUFFERS.md):
//
//  Opaque pass: the game binds four targets with its depth-stencil (HDR
//  colour, motion vectors, normals, a two-channel mask). When the normal
//  target is unbound, the opaque scene is complete: the hook copies depth and
//  normals into the shared textures and reads the camera from the game's
//  per-frame constants (slot 12).
//  Before the UI: the game blits the finished frame into the backbuffer, then
//  binds the backbuffer again for the HUD. At that second bind the pass runs
//  and its result goes back into the backbuffer, under the HUD.
//  A frame with no world (menus, loading) runs the pass at Present when
//  FallbackAtPresent=1.
//
//  d3d11_proxy.ini:
//    [D3D12Bridge]
//    Enabled=0            1 turns the bridge on (default off)
//    Pass=haze            haze: the built-in depth haze. engine: the Playground
//                         engine's GTAO (needs a build with RAW_ENGINE_DIR)
//    Mode=1               0 pass-through, 1 apply, 2 view (linear depth or visibility)
//    Density, FogR/FogG/FogB, MaxOpacity, Near, Far, Marker    haze pass
//    AoRadius=35          engine pass: radius in game units; AoSlices, AoSteps
//                         (0 keeps the engine's 16 and 16), AoStrength=1
//    FallbackAtPresent=1  DebugLayer=0  TimingLogEvery=600
//    RetireTimeoutMs=15000  engine pass: how long a frame waits for the one before
//    TestStallAtBridgeFrame=0  test only: stall the pass at that gameplay frame
//    CaptureFrame=0       absolute frame for d3d12_bridge_capture.bmp beside the exe
//    CaptureAfter=0       bridge frames in gameplay before the capture set
//    CaptureDir=          absolute folder for the capture set
//
//  Original work, MIT.
//=============================================================================
#include "BridgeHookState.h"
#include "CBDirtyTracker.h"
#include "ProxyAPI.h"
#include "ProxyLog.h"
#include "RenderPhaseDetector.h"
#include "SceneLights.h"
#include "SceneGeometry.h"
#include "GameComposite.h"
#include <cstdlib>
#include <cstring>

namespace SB::Proxy
{
using namespace hook;
namespace hook
{
Config& Cfg() { static Config c; return c; }
static raw::bridge::D3D12Bridge* s_bridge = nullptr;   // leaked at exit on purpose (no work under loader lock)
raw::bridge::D3D12Bridge* Bridge() { return s_bridge; }
}

namespace
{
// What the hook knows about the frame in progress. Pointers are compared,
// never dereferenced after the frame, and views are made per use.
struct FrameState
{
    ID3D11Resource* backbuffer = nullptr;
    ID3D11Resource* normalTex = nullptr;
    ID3D11Resource* depthTex = nullptr;
    ID3D11Resource* motionTex = nullptr;
    ID3D11Resource* sceneTex = nullptr;   // the HDR scene colour of the opaque set
    bool normalBound = false, captured = false, ran = false, inBackbuffer = false;
    bool waterSeen = false, compositeOpen = false, waterRan = false;
    bool opaqueSet = false, bedCaptured = false;   // RtWater: the bed's depth, copied when the four-target opaque set ends   // RtWater: the game's water span, then its composite
    uint32_t drawsAtBind = 0;
};
FrameState s_f;
bool     s_started = false, s_wasReady = false;
int      s_initFailures = 0;
uint64_t s_offFrames = 0, s_frame = 0, s_atUI = 0, s_atPresent = 0, s_captures = 0, s_bridgeFrames = 0;
raw::bridge::BridgeTimings s_sum{};
uint32_t s_samples = 0;

ID3D11Resource* ResourceOf(ID3D11View* v)
{
    if (!v) return nullptr;
    ID3D11Resource* r = nullptr;
    v->GetResource(&r);
    if (r) r->Release();   // compared by address only
    return r;
}

bool FullSize(ID3D11Resource* r, DXGI_FORMAT format, const D3D11_TEXTURE2D_DESC& bb)
{
    D3D11_RESOURCE_DIMENSION dim{};
    r->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return false;
    D3D11_TEXTURE2D_DESC d{};
    static_cast<ID3D11Texture2D*>(r)->GetDesc(&d);
    return d.Width == bb.Width && d.Height == bb.Height && d.Format == format && d.SampleDesc.Count == 1;
}

bool EnsureBridge(ID3D11Texture2D* backbuffer)
{
    D3D11_TEXTURE2D_DESC bb{};
    backbuffer->GetDesc(&bb);
    if (bb.SampleDesc.Count != 1) return false;
    if (s_bridge->Matches(bb.Width, bb.Height, bb.Format)) { s_wasReady = true; return true; }
    if (s_wasReady && s_bridge->Config().width == bb.Width && s_bridge->Config().height == bb.Height) {
        // It ran and then turned itself off (a pass failed). It stays off: a
        // second device in the same state would fail the same way.
        static bool logged = false;
        if (!logged) { logged = true; Log("[D3D12Bridge] the bridge turned itself off at frame %llu and stays off", (unsigned long long)s_frame); }
        return false;
    }
    if (s_initFailures >= 3) return false;
    auto* pi = PG_GetProxyInterface();
    if (!pi || !pi->device || !pi->context) return false;
    raw::bridge::BridgeConfig c;
    c.width = bb.Width;
    c.height = bb.Height;
    c.colorFormat = bb.Format;
    c.debugLayer = Cfg().debugLayer;
    c.probeStages = s_initFailures == 0;
    c.enginePass = Cfg().enginePass;
    c.passRetireMs = static_cast<DWORD>(Cfg().retireTimeoutMs);
    c.normalFormat = DXGI_FORMAT_R8G8B8A8_UNORM;   // the game's normal target
    c.realtimeAo = Cfg().realtimeAo;
    if (c.realtimeAo) c.motionFormat = DXGI_FORMAT_R16G16_FLOAT;   // the game's motion vectors
    if (s_bridge->Initialize(pi->device, pi->context, c, &Log)) {
        s_bridge->TuneEngine(Cfg().tune);
        return true;
    }
    ++s_initFailures;
    return false;
}

void RunOnBackbuffer(bool beforeUI)
{
    auto* pi = PG_GetProxyInterface();
    if (!pi || !pi->swapChain) return;
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(pi->swapChain->GetBuffer(0, IID_PPV_ARGS(&bb))) || !bb) return;
    if (EnsureBridge(bb)) {
        raw::bridge::PassConstants k = Cfg().k;
        k.frame = static_cast<float>(s_frame & 0xFFFF);
        // Test of the failure path: one frame whose pass waits for a fence value D3D11 never
        // signals. The next frame cannot retire it, the pass fails, and the bridge must release
        // the game's D3D11 queue.
        const bool stall = beforeUI && Cfg().testStallAt > 0 && s_bridgeFrames + 1 == static_cast<uint64_t>(Cfg().testStallAt);
        if (stall) { s_bridge->SetWaitSkew(1000); Log("[D3D12Bridge] test: stalling the pass at gameplay bridge frame %d", Cfg().testStallAt); }
        const bool ran = s_bridge->Execute(bb, bb, k);
        if (stall) s_bridge->SetWaitSkew(0);
        if (ran) {
            s_f.ran = true;
            ++(beforeUI ? s_atUI : s_atPresent);
            if (beforeUI) ++s_bridgeFrames;
        }
    }
    bb->Release();
}

// End of the opaque pass: depth and normals into the shared textures.
void CaptureOpaque(ID3D11DeviceContext* real)
{
    s_f.captured = true;
    if (Cfg().lighting && !LightingEnsure()) return;
    if (!s_bridge->Ready() || !s_f.depthTex || !s_f.normalTex) return;
    // The opaque set is still bound. Where its depth-stencil view is writable (interiors), D3D11
    // refuses a shader view of the same depth and the copy reads zeros; the game binds its next
    // targets right after this, so the set is released here.
    real->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11Device* dev = nullptr;
    real->GetDevice(&dev);
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    ID3D11ShaderResourceView* srv = nullptr;
    const HRESULT hr = dev->CreateShaderResourceView(s_f.depthTex, &sd, &srv);
    dev->Release();
    const raw::bridge::CameraConstants cam = ReadCamera(real);
    s_bridge->SetCamera(cam);
    const bool depthOk = SUCCEEDED(hr) && s_bridge->CaptureDepth(srv);
    if (s_f.motionTex) s_bridge->CaptureMotion(static_cast<ID3D11Texture2D*>(s_f.motionTex));
    const bool normalOk = s_bridge->CaptureNormal(static_cast<ID3D11Texture2D*>(s_f.normalTex));
    if (srv) srv->Release();
    if (depthOk && normalOk) ++s_captures;
    if (Cfg().lighting && depthOk && normalOk) s_f.ran = LightingAtOpaque(real, s_f.sceneTex);
    static int logged = 0;
    if (logged++ < 3)
        Log("[D3D12Bridge] opaque pass done at frame %llu: depth %d, normals %d, camera %d (tanHalfFovY %.5f, "
            "aspect %.4f, near %.3f, far %.0f)", (unsigned long long)s_frame, depthOk, normalOk, cam.valid,
            cam.tanHalfFovY, cam.aspect, cam.nearZ, cam.farZ);
}

void AccumulateTimings()
{
    const uint64_t n = s_bridge->FramesExecuted();
    raw::bridge::BridgeTimings t;
    if (n < 3 || !s_bridge->ReadTimings(n - 3, false, &t)) return;
    s_sum.depthCopyMs += t.depthCopyMs; s_sum.normalCopyMs += t.normalCopyMs; s_sum.copyInMs += t.copyInMs;
    s_sum.copyOutMs += t.copyOutMs; s_sum.stallMs += t.stallMs; s_sum.passMs += t.passMs; s_sum.syncMs += t.syncMs;
    s_sum.bridgeCostMs += t.bridgeCostMs;
    for (int k = 0; k < 4; ++k) { s_sum.pass.stageMs[k] += t.pass.stageMs[k]; s_sum.pass.stageName[k] = t.pass.stageName[k]; }
    const uint32_t every = static_cast<uint32_t>(Cfg().timingLogEvery > 0 ? Cfg().timingLogEvery : 600);
    if (++s_samples < every) return;
    const double inv = 1.0 / s_samples;
    char stages[160] = "";
    for (int k = 0; k < 4 && s_sum.pass.stageName[k]; ++k)
        sprintf_s(stages + strlen(stages), sizeof(stages) - strlen(stages), "%s%s %.3f", k ? ", " : "",
                  s_sum.pass.stageName[k], s_sum.pass.stageMs[k] * inv);
    Log("[D3D12Bridge] mean over %u frames: D3D12 pass %.3f ms (%s); bridge cost %.3f ms (depth copy %.3f, "
        "normal copy %.3f, copy in %.3f, copy out %.3f, fence sync %.3f); D3D11 stall %.3f ms; "
        "runs before UI=%llu at Present=%llu, opaque captures=%llu",
        s_samples, s_sum.passMs * inv, stages, s_sum.bridgeCostMs * inv, s_sum.depthCopyMs * inv,
        s_sum.normalCopyMs * inv, s_sum.copyInMs * inv, s_sum.copyOutMs * inv, s_sum.syncMs * inv,
        s_sum.stallMs * inv, (unsigned long long)s_atUI, (unsigned long long)s_atPresent,
        (unsigned long long)s_captures);
    s_sum = {};
    s_samples = 0;
}
} // namespace

void BridgeHook_OnTargets(ID3D11DeviceContext* real, UINT n, ID3D11RenderTargetView* const* rtvs,
                          ID3D11DepthStencilView* dsv)
{
    if (!s_bridge) return;
    const uint32_t draws = RenderPhaseDetector::Get().GetTotalDrawsThisFrame();
    ID3D11Resource* rt[4] = {};
    for (UINT k = 0; k < n && k < 4; ++k) rt[k] = ResourceOf(rtvs ? rtvs[k] : nullptr);

    // The span that just ended.
    if (s_f.inBackbuffer && draws > s_f.drawsAtBind && s_f.captured && !s_f.ran && !Cfg().lighting) RunOnBackbuffer(true);
    if (s_f.normalBound && !s_f.captured) {
        bool still = false;
        for (ID3D11Resource* r : rt) still |= r == s_f.normalTex;
        if (!still) CaptureOpaque(real);
    }
    if (s_f.compositeOpen && draws > s_f.drawsAtBind && !s_f.waterRan && Cfg().lighting) {
        // The game has composited its water into the HDR colour: RAW's water goes on top (RtWater.hlsl).
        s_f.waterRan = true;
        LightingAtWater(real, s_f.sceneTex, true);
    }
    s_f.compositeOpen = false;

    // The span that starts.
    if (!s_f.backbuffer)
        if (auto* pi = PG_GetProxyInterface(); pi && pi->swapChain) {
            ID3D11Texture2D* bb = nullptr;
            if (SUCCEEDED(pi->swapChain->GetBuffer(0, IID_PPV_ARGS(&bb))) && bb) { s_f.backbuffer = bb; bb->Release(); }
        }
    if (!s_f.normalTex && n >= 4 && rt[0] && rt[1] && rt[2] && rt[3] && dsv && s_f.backbuffer) {
        D3D11_TEXTURE2D_DESC bb{};
        static_cast<ID3D11Texture2D*>(s_f.backbuffer)->GetDesc(&bb);
        // The opaque set: HDR colour, motion vectors, normals, a two-channel mask.
        if (FullSize(rt[0], DXGI_FORMAT_R16G16B16A16_FLOAT, bb) && FullSize(rt[1], DXGI_FORMAT_R16G16_FLOAT, bb) &&
            FullSize(rt[2], DXGI_FORMAT_R8G8B8A8_UNORM, bb) && FullSize(rt[3], DXGI_FORMAT_R8G8_UNORM, bb)) {
            s_f.normalTex = rt[2];
            s_f.motionTex = rt[1];
            s_f.sceneTex = rt[0];
            if (Cfg().lighting) GameComposite_SetScene(rt[0]);
            s_f.depthTex = ResourceOf(dsv);
        }
    }
    if (s_f.captured && s_f.sceneTex && rt[0] && s_f.backbuffer && Cfg().lighting) {
        D3D11_TEXTURE2D_DESC bb{};
        static_cast<ID3D11Texture2D*>(s_f.backbuffer)->GetDesc(&bb);
        // The water span: one full-size RGBA16F target that is not the scene colour, on the scene depth
        // (docs/FRAME_BUFFERS.md); then one draw composites it into the scene colour and the water mask.
        if (n == 1 && rt[0] != s_f.sceneTex && dsv && ResourceOf(dsv) == s_f.depthTex && FullSize(rt[0], DXGI_FORMAT_R16G16B16A16_FLOAT, bb))
            s_f.waterSeen = true;
        else if (s_f.waterSeen && !s_f.waterRan && n >= 2 && rt[0] == s_f.sceneTex && rt[1] && FullSize(rt[1], DXGI_FORMAT_R10G10B10A2_UNORM, bb))
            s_f.compositeOpen = true;
        else if (!s_f.waterSeen && !s_f.waterRan && n == 3 && rt[0] == s_f.sceneTex && dsv) {
            // No water this frame: the skin's scattering runs where the water would have, before the blended draws.
            s_f.waterRan = true;
            LightingAtWater(real, s_f.sceneTex, false);
        }
    }
    // The four-target opaque set just ended: the depth still holds the bed under the water. The game writes the
    // water's depth later in the opaque pass (rt6-wat-4: RAW's end-of-opaque copy already holds the surface).
    if (s_f.opaqueSet && !s_f.bedCaptured && Cfg().lighting && s_f.depthTex) {
        s_f.bedCaptured = true;
        LightingCaptureBed(s_f.depthTex);
    }
    s_f.opaqueSet = n >= 4 && rt[0] && rt[0] == s_f.sceneTex && rt[2] == s_f.normalTex && rt[3];
    s_f.normalBound = false;
    if (s_f.normalTex)
        for (ID3D11Resource* r : rt) s_f.normalBound |= r == s_f.normalTex;
    if (Cfg().lighting) {
        SceneLights_SetOpaque(s_f.normalBound);
        DXGI_FORMAT f0 = DXGI_FORMAT_UNKNOWN;
        UINT w0 = 0;
        D3D11_RESOURCE_DIMENSION dim{};
        if (rt[0]) rt[0]->GetType(&dim);
        if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
            D3D11_TEXTURE2D_DESC d{};
            static_cast<ID3D11Texture2D*>(rt[0])->GetDesc(&d);
            f0 = d.Format; w0 = d.Width;
        }
        UINT count = 0;
        for (UINT k = 0; k < n && k < 4; ++k) count += rt[k] != nullptr;
        SceneGeometry_OnTargets(dsv && s_f.depthTex && ResourceOf(dsv) == s_f.depthTex, count, f0, w0, s_f.captured);
    }
    s_f.inBackbuffer = rt[0] && rt[0] == s_f.backbuffer;
    s_f.drawsAtBind = draws;
}

void BridgeHook_OnDeviceCreated(ID3D11Device* realDevice, ID3D11DeviceContext* realContext)
{
    if (PG_IsSafeMode()) return;
    if (!s_started) { s_started = true; LoadConfig(); }
    if (Cfg().enabled) raw::bridge::ProbeFenceSupport(realDevice, realContext, &Log, "after device creation");
}

void BridgeHook_OnPresent()
{
    if (PG_IsSafeMode()) return;
    if (!s_started) { s_started = true; LoadConfig(); }
    if (Cfg().enabled && !s_bridge) {
        s_bridge = new raw::bridge::D3D12Bridge();
        InstallShareCheck();
    }
    if (!Cfg().enabled || !s_bridge) return;
    if (s_frame < 4 || (s_frame % 600) == 0)
        Log("[D3D12Bridge] heartbeat frame %llu: executed=%llu (before UI %llu, at Present %llu), opaque captures %llu, "
            "fence D3D12=%llu completed=%llu", (unsigned long long)s_frame,
            (unsigned long long)s_bridge->FramesExecuted(), (unsigned long long)s_atUI,
            (unsigned long long)s_atPresent, (unsigned long long)s_captures,
            (unsigned long long)s_bridge->LastD3D12Value(), (unsigned long long)s_bridge->CompletedValue());
    if (!s_f.ran && Cfg().fallbackAtPresent && !Cfg().lighting) RunOnBackbuffer(false);
    if (s_wasReady && !s_bridge->Ready() && ++s_offFrames % 120 == 0 && s_offFrames <= 1200)
        Log("[D3D12Bridge] bridge off for %llu frames; the game still presents (frame %llu)",
            (unsigned long long)s_offFrames, (unsigned long long)s_frame);
    if (s_bridge->Ready()) AccumulateTimings();
    if (Cfg().captureFrame > 0 && s_frame == static_cast<uint64_t>(Cfg().captureFrame)) {
        char path[MAX_PATH]{};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string out(path);
        CaptureBackbuffer(out.substr(0, out.rfind('\\') + 1) + "d3d12_bridge_capture.bmp");
    }
    if (Cfg().lighting) { LightingOnPresent(s_f.captured); SceneLights_OnPresent(); }
    else if (s_f.ran && s_f.captured) CaptureStep(s_bridgeFrames);
    if (Cfg().lighting) {   // how often the game drew water, and whether its composite was found
        static uint32_t frames = 0, water = 0, composite = 0;
        ++frames; water += s_f.waterSeen; composite += s_f.waterRan;
        if (frames % 600 == 0) { Log("[RT] water spans in %u of 600 frames, composites found in %u", water, composite); water = composite = 0; }
    }
    s_f = {};
    ++s_frame;
}

} // namespace SB::Proxy
