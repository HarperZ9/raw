// Call-stream replay: each scene is recorded on the system D3D11 runtime, then
// the file is replayed on a fresh D3D11 device and on the translation layer.
// The D3D11 replay must reproduce the direct image exactly; the D3D12 replay
// must match it within the scene's tolerance.
#include "suite.h"
#include "rec_api.h"

namespace suite {

struct Capture {
    raw::rec::Recorder* rec = nullptr;
    uint64_t finalId = 0;
};

static bool Record(const Scene& s, const Opts& o, const std::wstring& path, Image& direct, uint64_t& finalId)
{
    RunCfg cfg;
    cfg.hardware = o.hardware;
    cfg.debug = false;
    ComPtr<ID3D11Device> real, dev;
    ComPtr<ID3D11DeviceContext> realCtx, ctx;
    if (!MakeDevice(cfg, real, realCtx)) return false;
    if (FAILED(raw::rec::Install(real.Get(), realCtx.Get(), &dev, &ctx))) return false;
    Capture cap;
    cap.rec = raw::rec::GetRecorder(dev.Get());
    if (!cap.rec || !cap.rec->BeginCapture(path.c_str())) return false;
    cap.rec->OnPresent();   // the capture starts here
    SceneCtx c;
    c.dev = dev.Get(); c.ctx = ctx.Get();
    c.user = &cap;
    c.onReadback = [](void* user, ID3D11Texture2D* tex) {
        auto* k = static_cast<Capture*>(user);
        k->finalId = k->rec->Ref(tex, raw::rec::K_TEX2D);
    };
    bool ok = s.run(c, direct);
    cap.rec->OnPresent();   // and ends here
    finalId = cap.finalId;
    return ok && cap.rec->LastCaptureOk() && cap.rec->CapturesCompleted() == 1 && finalId != 0;
}

static bool Replay(const std::wstring& path, uint64_t finalId, const RunCfg& cfg, Image& img, raw::rec::ReplayResult& res, raw::tx::Stats& st)
{
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    if (!MakeDevice(cfg, dev, ctx)) return false;
    bool ok = false;
    {
        raw::rec::Replayer rp;
        if (rp.Open(path.c_str()) && rp.Run(dev.Get(), ctx.Get())) {
            SceneCtx c;
            c.dev = dev.Get(); c.ctx = ctx.Get();
            ID3D11Texture2D* tex = rp.FindTexture(finalId);
            ok = tex && Readback(c, tex, img);
        }
        res = rp.Result();
        ctx->ClearState();
    }
    ctx->Flush();
    if (cfg.be == Backend::Translate) st = raw::tx::GetStats(dev.Get());
    return ok;
}

void RunReplay(const Opts& o, Tally& t)
{
    for (const Scene& s : AllScenes()) {
        std::string name = std::string("replay_") + s.name;
        std::string file = o.out + "/" + s.name + ".d3d11stream";
        std::wstring path(file.begin(), file.end());
        Image direct, on11, on12;
        uint64_t finalId = 0;
        if (!Record(s, o, path, direct, finalId)) { t.Add(false, name.c_str(), "recording failed"); continue; }
        RunCfg a, b;
        a.hardware = b.hardware = o.hardware;
        a.debug = false;
        b.be = Backend::Translate;
        raw::rec::ReplayResult r11, r12;
        raw::tx::Stats st{}, unused{};
        bool ok11 = Replay(path, finalId, a, on11, r11, unused);
        bool ok12 = Replay(path, finalId, b, on12, r12, st);
        int d11 = MaxDiff(direct, on11), d12 = MaxDiff(direct, on12);
        bool ok = ok11 && ok12 && d11 == 0 && d12 >= 0 && d12 <= s.tolerance && st.debugErrors == 0 && st.unsupportedCalls == 0;
        char d[320];
        snprintf(d, sizeof(d), "%llu records, %u objects; replay on D3D11 max diff %d, on D3D12 max diff %d (allowed %d), debug errors %llu, unsupported %llu%s%s",
                 (unsigned long long)r12.records, r12.objects, d11, d12, s.tolerance, (unsigned long long)st.debugErrors,
                 (unsigned long long)st.unsupportedCalls, r12.ok ? "" : "; D3D12 replay stopped: ", r12.ok ? "" : r12.message.c_str());
        t.Add(ok, name.c_str(), d);
    }
}

}  // namespace suite
