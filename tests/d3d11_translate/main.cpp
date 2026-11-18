// d3d11_translate_suite: every scene runs on the system D3D11 runtime and on the
// D3D11-to-D3D12 translation layer, and the two images are compared. Then the
// controls, the bytecode readers, the swap chain and the call-stream replay.
// Exit code 0 only when every check holds.
#include "suite.h"
#include <d3d11sdklayers.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace suite {

void Tally::Add(bool ok, const char* name, const std::string& detail)
{
    (ok ? passed : failed)++;
    printf("%s %-34s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    fflush(stdout);
    if (!json.empty()) json += ",\n";
    std::string esc;
    for (char ch : detail) { if (ch == '"' || ch == '\\') esc += '\\'; esc += ch; }
    json += "    {\"check\": \"" + std::string(name) + "\", \"pass\": " + (ok ? "true" : "false") + ", \"detail\": \"" + esc + "\"}";
}

double Median(std::vector<double> v, size_t skip)
{
    if (v.size() > skip) v.erase(v.begin(), v.begin() + skip);
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

bool MakeDevice(const RunCfg& cfg, ComPtr<ID3D11Device>& dev, ComPtr<ID3D11DeviceContext>& ctx)
{
    HRESULT hr;
    if (cfg.be == Backend::Translate) {
        raw::tx::CreateParams p;
        p.warp = !cfg.hardware;
        p.debugLayer = cfg.debug;
        p.faults = cfg.faults;
        hr = raw::tx::CreateDevice(p, &dev, &ctx);
    } else {
        const D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0;
        hr = D3D11CreateDevice(nullptr, cfg.hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP, nullptr,
                               cfg.debug ? D3D11_CREATE_DEVICE_DEBUG : 0, &want, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    }
    if (FAILED(hr)) return Fail("suite", "device creation", hr);
    return true;
}

static uint32_t DrainD3D11(ID3D11Device* dev)
{
    ComPtr<ID3D11InfoQueue> q;
    if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&q)))) return 0;
    uint32_t bad = 0;
    for (UINT64 i = 0, n = q->GetNumStoredMessages(); i < n; ++i) {
        SIZE_T len = 0;
        q->GetMessage(i, nullptr, &len);
        std::vector<uint8_t> buf(len);
        auto* m = reinterpret_cast<D3D11_MESSAGE*>(buf.data());
        if (SUCCEEDED(q->GetMessage(i, m, &len)) && m->Severity <= D3D11_MESSAGE_SEVERITY_WARNING) ++bad;
    }
    return bad;
}

bool RunOnce(const Scene& s, const RunCfg& cfg, RunOut& out)
{
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    out = RunOut{};
    if (!MakeDevice(cfg, dev, ctx)) return false;
    SceneCtx c;
    c.dev = dev.Get(); c.ctx = ctx.Get();
    c.drawCount = cfg.draws; c.threads = cfg.threads; c.frames = cfg.frames;
    out.ok = s.run(c, out.img);
    ctx->ClearState();
    ctx->Flush();
    out.values = c.values; out.callMs = c.callMs; out.recordMs = c.recordMs; out.totalMs = c.totalMs;
    if (cfg.be == Backend::Translate) out.stats = raw::tx::GetStats(dev.Get());
    else if (cfg.debug) out.d3d11Messages = DrainD3D11(dev.Get());
    ctx.Reset();
    out.leftRefs = dev.Reset();
    if (cfg.be == Backend::D3D11 && cfg.debug) out.leftRefs = 0;   // the D3D11 debug layer keeps references
    return out.ok;
}

void RunScenes(const Opts& o, Tally& t)
{
    for (const Scene& s : AllScenes()) {
        if (!o.scene.empty() && o.scene != s.name) continue;
        RunCfg a, b;
        a.hardware = b.hardware = o.hardware;
        a.debug = b.debug = !o.noDebug;
        b.be = Backend::Translate;
        RunOut ra, rb;
        RunOnce(s, a, ra);
        RunOnce(s, b, rb);
        int diff = MaxDiff(ra.img, rb.img);
        bool ok = ra.ok && rb.ok && diff >= 0 && diff <= s.tolerance && ra.values == rb.values &&
                  rb.stats.debugErrors == 0 && rb.stats.unsupportedCalls == 0 && rb.leftRefs == 0 && ra.d3d11Messages == 0;
        SaveBMP((o.out + "/" + s.name + "_d3d11.bmp").c_str(), ra.img);
        SaveBMP((o.out + "/" + s.name + "_d3d12.bmp").c_str(), rb.img);
        char d[320];
        snprintf(d, sizeof(d), "max diff %d (allowed %d), d3d11 %016llx, d3d12 %016llx, values %s, debug errors %llu, unsupported %llu, refs left %lu, psos %llu, barriers %llu",
                 diff, s.tolerance, (unsigned long long)HashImage(ra.img), (unsigned long long)HashImage(rb.img),
                 ra.values == rb.values ? "equal" : "DIFFER", (unsigned long long)rb.stats.debugErrors,
                 (unsigned long long)rb.stats.unsupportedCalls, rb.leftRefs, (unsigned long long)rb.stats.psoCreated,
                 (unsigned long long)rb.stats.barriers);
        t.Add(ok, s.name, d);
        if (std::string(s.name) != "many_draws") continue;
        b.threads = 4;   // deferred contexts recorded on four threads
        RunOut rc;
        RunOnce(s, b, rc);
        diff = MaxDiff(ra.img, rc.img);
        ok = rc.ok && diff >= 0 && diff <= s.tolerance && rc.stats.debugErrors == 0 && rc.stats.unsupportedCalls == 0 && rc.leftRefs == 0;
        snprintf(d, sizeof(d), "4 deferred contexts on D3D12 against immediate D3D11: max diff %d, debug errors %llu, submissions %llu",
                 diff, (unsigned long long)rc.stats.debugErrors, (unsigned long long)rc.stats.submissions);
        t.Add(ok, "many_draws_deferred", d);
    }
}

}  // namespace suite

int main(int argc, char** argv)
{
    using namespace suite;
    Opts o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        const char* v = i + 1 < argc ? argv[i + 1] : nullptr;
        if (a == "--hardware") o.hardware = true;
        else if (a == "--bench") o.bench = true;
        else if (a == "--no-debug") o.noDebug = true;
        else if (a == "--scene" && v) { o.scene = v; ++i; }
        else if (a == "--out" && v) { o.out = v; ++i; }
        else if (a == "--json" && v) { o.json = v; ++i; }
        else if (a == "--frames" && v) { o.benchFrames = uint32_t(atoi(v)); ++i; }
        else if (a == "--threads" && v) { o.benchThreads = uint32_t(atoi(v)); ++i; }
        else {
            fprintf(stderr, "usage: d3d11_translate_suite [--scene NAME] [--out DIR] [--json FILE] [--hardware] [--no-debug]\n"
                            "       d3d11_translate_suite --bench [--frames N] [--threads N] [--hardware] [--json FILE]\n");
            return 2;
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(o.out, ec);
    Tally t;
    if (o.bench) {
        RunBench(o, t);
    } else {
        RunScenes(o, t);
        if (o.scene.empty()) {
            RunControls(o, t);
            RunReflection(t);
            RunGetters(o, t);
            RunSwapChain(o, t);
            RunReplay(o, t);
        }
    }
    printf("%d passed, %d failed (%s)\n", t.passed, t.failed, o.hardware ? "hardware adapter" : "WARP");
    if (!o.json.empty()) {
        if (FILE* f = fopen(o.json.c_str(), "wb")) {
            fprintf(f, "{\n  \"adapter\": \"%s\",\n  \"passed\": %d,\n  \"failed\": %d,\n  \"results\": [\n%s\n  ]\n}\n",
                    o.hardware ? "hardware" : "WARP", t.passed, t.failed, t.json.c_str());
            fclose(f);
        }
    }
    return t.failed == 0 ? 0 : 1;
}
