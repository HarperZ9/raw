// d3d11_scenes_baseline: runs the test scenes on a real D3D11 device (WARP by
// default), each scene twice on fresh devices, and requires bit-identical images.
// Writes <out>/<scene>.bmp and one result line per scene. Exit code 0: all passed.
#include "scene_util.h"
#include <d3d11sdklayers.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

using namespace txtest;

namespace {

struct Opts {
    std::string scene, out = "out";
    uint32_t draws = 2000, threads = 0, frames = 1;
    bool debug = false, hardware = false;
};

bool ParseArgs(int argc, char** argv, Opts& o)
{
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const char* v = i + 1 < argc ? argv[i + 1] : nullptr;
        auto num = [&](uint32_t& dst) { dst = static_cast<uint32_t>(strtoul(v, nullptr, 10)); ++i; };
        if (a == "--debug") o.debug = true;
        else if (a == "--hardware") o.hardware = true;
        else if (a == "--scene" && v) { o.scene = v; ++i; }
        else if (a == "--out" && v) { o.out = v; ++i; }
        else if (a == "--draws" && v) num(o.draws);
        else if (a == "--threads" && v) num(o.threads);
        else if (a == "--frames" && v) num(o.frames);
        else {
            fprintf(stderr, "usage: d3d11_scenes_baseline [--scene NAME] [--out DIR] [--draws N] "
                            "[--threads N] [--frames N] [--debug] [--hardware]\n");
            return false;
        }
    }
    return true;
}

bool MakeDevice(const Opts& o, ComPtr<ID3D11Device>& dev, ComPtr<ID3D11DeviceContext>& ctx)
{
    const D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL got{};
    const D3D_DRIVER_TYPE type = o.hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP;
    const UINT flags = o.debug ? D3D11_CREATE_DEVICE_DEBUG : 0;
    HRESULT hr = D3D11CreateDevice(nullptr, type, nullptr, flags, &want, 1, D3D11_SDK_VERSION, &dev,
                                   &got, &ctx);
    if (FAILED(hr)) return Fail("baseline", "D3D11CreateDevice", hr);
    return true;
}

// Prints every debug-layer message of severity WARNING or worse; returns the count.
uint32_t DrainDebug(ID3D11Device* dev, const char* scene)
{
    ComPtr<ID3D11InfoQueue> q;
    if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&q)))) return 0;
    uint32_t bad = 0;
    const UINT64 n = q->GetNumStoredMessages();
    for (UINT64 i = 0; i < n; ++i) {
        SIZE_T len = 0;
        q->GetMessage(i, nullptr, &len);
        std::vector<uint8_t> buf(len);
        auto* m = reinterpret_cast<D3D11_MESSAGE*>(buf.data());
        if (FAILED(q->GetMessage(i, m, &len))) continue;
        if (m->Severity > D3D11_MESSAGE_SEVERITY_WARNING) continue;
        fprintf(stderr, "[%s] debug layer: %.*s\n", scene, static_cast<int>(m->DescriptionByteLength),
                m->pDescription);
        ++bad;
    }
    q->ClearStoredMessages();
    return bad;
}

struct RunOut {
    Image img;
    SceneCtx c;
};

// One run on a fresh device. False on a scene failure, a debug-layer message or a leak.
bool RunOnce(const Scene& s, const Opts& o, uint32_t threads, RunOut& r)
{
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    if (!MakeDevice(o, dev, ctx)) return false;
    r.c = SceneCtx{};
    r.c.dev = dev.Get();
    r.c.ctx = ctx.Get();
    r.c.drawCount = o.draws;
    r.c.threads = threads;
    r.c.frames = o.frames;
    bool ok = s.run(r.c, r.img);
    ctx->ClearState();
    ctx->Flush();
    if (o.debug && DrainDebug(dev.Get(), s.name) != 0) ok = false;
    r.c.dev = nullptr;
    r.c.ctx = nullptr;
    ctx.Reset();
    const ULONG left = dev.Reset();
    if (left != 0 && !o.debug) {   // the debug layer keeps its own references
        fprintf(stderr, "[%s] device still has %lu references after the scene (leak)\n", s.name, left);
        ok = false;
    }
    return ok;
}

double Median(std::vector<double> v)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

void PrintLine(const Scene& s, bool pass, const RunOut& r, const char* why)
{
    printf("%s %-20s hash=%016llx values=[", pass ? "PASS" : "FAIL", s.name,
           static_cast<unsigned long long>(HashImage(r.img)));
    for (size_t i = 0; i < r.c.values.size(); ++i)
        printf("%s%llu", i ? "," : "", static_cast<unsigned long long>(r.c.values[i]));
    printf("]");
    if (!r.c.recordMs.empty())
        printf(" record_ms=%.3f total_ms=%.3f", Median(r.c.recordMs), Median(r.c.totalMs));
    if (!pass) printf(" (%s)", why);
    printf("\n");
    fflush(stdout);
}

bool RunScene(const Scene& s, const Opts& o)
{
    RunOut a, b;
    const char* why = "";
    bool pass = RunOnce(s, o, o.threads, a);
    if (!pass) why = "first run failed";
    if (pass && !RunOnce(s, o, o.threads, b)) { pass = false; why = "second run failed"; }
    if (pass && (MaxDiff(a.img, b.img) != 0 || a.c.values != b.c.values)) {
        pass = false;
        why = "two runs differ";
    }
    if (pass && strcmp(s.name, "many_draws") == 0) {
        // The deferred-context picture must equal the immediate-context picture.
        RunOut x;
        if (!RunOnce(s, o, o.threads == 0 ? 4 : 0, x) || MaxDiff(a.img, x.img) != 0) {
            pass = false;
            why = "immediate and deferred pictures differ";
        }
    }
    if (!a.img.rgba.empty()) {
        const std::string path = o.out + "/" + s.name + ".bmp";
        if (!SaveBMP(path.c_str(), a.img)) { pass = false; why = "cannot write the BMP"; }
    }
    PrintLine(s, pass, a, why);
    return pass;
}

}  // namespace

int main(int argc, char** argv)
{
    Opts o;
    if (!ParseArgs(argc, argv, o)) return 2;
    std::error_code ec;
    std::filesystem::create_directories(o.out, ec);
    if (ec) {
        fprintf(stderr, "cannot create %s: %s\n", o.out.c_str(), ec.message().c_str());
        return 2;
    }
    if (!o.scene.empty() && !FindScene(o.scene.c_str())) {
        fprintf(stderr, "unknown scene %s\n", o.scene.c_str());
        return 2;
    }
    int failed = 0, ran = 0;
    for (const Scene& s : AllScenes()) {
        if (!o.scene.empty() && o.scene != s.name) continue;
        ++ran;
        if (!RunScene(s, o)) ++failed;
    }
    printf("%d of %d scenes passed (%s%s)\n", ran - failed, ran, o.hardware ? "hardware" : "WARP",
           o.debug ? ", debug layer" : "");
    return failed == 0 ? 0 : 1;
}
