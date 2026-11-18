// bridge_roundtrip: drives raw::bridge::D3D12Bridge from a plain D3D11 device,
// the way the proxy does in game, and checks pixels after the round trip.
//
//   1. identity pass: output must equal the input exactly
//   2. haze pass over a banded depth texture: matches a CPU reference (tol 2/255)
//   3. haze pass over a real depth-stencil texture cleared to 0.75
//   4. comparator self-check: one corrupted reference pixel must be caught
//   5. CONTROL: D3D12 waits on a fence value D3D11 never signals; the test
//      must detect the stall (timeout), release it, and recover
//   6. timing: GPU timestamps on both queues at 1080p and 2160p
//
// Exit code 0 only when every check holds.
#include "fixtures.h"
#include "extra_shares.h"
#include "more_shares.h"
#include <dxgi.h>
#include <cstdarg>
#include <cstdio>
#include <string>

using namespace raw::bridge;
using fx::ComPtr;

static void Log(const char* f, ...)
{
    va_list a; va_start(a, f); vprintf(f, a); va_end(a); printf("\n");
}

static int g_fail = 0;
static void Check(bool ok, const char* what)
{
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

struct Rig
{
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<IDXGISwapChain> sc;
    // skyrimLike: a DXGI 1.0 factory adapter, D3D11CreateDeviceAndSwapChain
    // and a blit-model swap chain on a hidden window, as Skyrim SE does.
    bool Create(bool warp, bool skyrimLike)
    {
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        if (skyrimLike) {
            HMODULE dx = LoadLibraryW(L"dxgi.dll");
            using PFN_F = HRESULT(WINAPI*)(REFIID, void**);
            auto mk = reinterpret_cast<PFN_F>(GetProcAddress(dx, "CreateDXGIFactory"));
            ComPtr<IDXGIFactory> f; ComPtr<IDXGIAdapter> a;
            if (!mk || FAILED(mk(IID_PPV_ARGS(&f))) || FAILED(f->EnumAdapters(0, &a))) return false;
            WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.lpszClassName = L"raw_bridge_test";
            wc.hInstance = GetModuleHandleW(nullptr); RegisterClassW(&wc);
            HWND hwnd = CreateWindowW(wc.lpszClassName, L"raw", WS_OVERLAPPEDWINDOW, 0, 0, 256, 128,
                                      nullptr, nullptr, wc.hInstance, nullptr);
            DXGI_SWAP_CHAIN_DESC d{};
            d.BufferDesc.Width = 256; d.BufferDesc.Height = 128; d.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            d.SampleDesc.Count = 1; d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = 2;
            d.OutputWindow = hwnd; d.Windowed = TRUE; d.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            if (FAILED(D3D11CreateDeviceAndSwapChain(a.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &fl, 1,
                    D3D11_SDK_VERSION, &d, &sc, &dev, nullptr, &ctx)))
                return false;
            printf("       Skyrim-like device: DXGI 1.0 adapter, CreateDeviceAndSwapChain, blit swap chain\n");
        } else if (FAILED(D3D11CreateDevice(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE,
                nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
            return false;
        // Run next to the proxy d3d11.dll, the device above is RAW's wrapper.
        // Take the real device from the proxy interface, as the game-side hook
        // does, so this reproduces the in-game process layout.
        using GetPI = void* (*)();
        HMODULE proxy = GetModuleHandleW(L"d3d11.dll");
        auto get = proxy ? reinterpret_cast<GetPI>(GetProcAddress(proxy, "PG_GetProxyInterface")) : nullptr;
        if (get) {
            struct Head { uint32_t version; ID3D11Device* device; ID3D11DeviceContext* context; };
            auto* pi = static_cast<Head*>(get());
            printf("       running through the RAW proxy d3d11.dll (real device %p)\n", (void*)pi->device);
            ComPtr<ID3D11Device> realDev(pi->device);
            ComPtr<ID3D11DeviceContext> realCtx(pi->context);
            proxied = true;
            dev = realDev; ctx = realCtx;
        }
        return true;
    }
    bool proxied = false;
};

static std::vector<uint32_t> ColourPattern(UINT w, UINT h)
{
    std::vector<uint32_t> v(w * h);
    for (UINT y = 0; y < h; ++y) for (UINT x = 0; x < w; ++x) v[y * w + x] = fx::ColourAt(x, y);
    return v;
}

static void Correctness(Rig& r)
{
    const UINT W = 256, H = 128;
    auto colour = ColourPattern(W, H);
    std::vector<uint32_t> depth(W * H);
    for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) depth[y * W + x] = fx::DepthWordAt(x, y, W);

    auto src = fx::MakeTex(r.dev.Get(), W, H, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, colour.data(), W * 4);
    auto dst = fx::MakeTex(r.dev.Get(), W, H, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET, nullptr, 0);
    auto dep = fx::MakeTex(r.dev.Get(), W, H, DXGI_FORMAT_R24G8_TYPELESS, D3D11_BIND_SHADER_RESOURCE, depth.data(), W * 4);
    Check(src && dst && dep, "fixture textures created");
    auto depSrv = fx::MakeSrv(r.dev.Get(), dep.Get(), DXGI_FORMAT_R24_UNORM_X8_TYPELESS);

    D3D12Bridge b;
    BridgeConfig cfg; cfg.width = W; cfg.height = H; cfg.debugLayer = true;
    if (getenv("RAW_TEST_OPEN_DISJOINT")) {   // experiment: a query left open by the host
        D3D11_QUERY_DESC qd0{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        static ComPtr<ID3D11Query> q0; r.dev->CreateQuery(&qd0, &q0); r.ctx->Begin(q0.Get());
        printf("       experiment: a TIMESTAMP_DISJOINT query is open on the context\n");
    }
    Check(b.Initialize(r.dev.Get(), r.ctx.Get(), cfg, Log), "bridge initialised (D3D12 device on the D3D11 adapter, shared textures and fence)");
    if (!b.Ready()) return;

    PassConstants k; k.density = 0.002f; k.fogColor[0] = 0.55f; k.fogColor[1] = 0.62f; k.fogColor[2] = 0.80f;
    k.maxOpacity = 0.85f; k.nearZ = 15.0f; k.farZ = 353840.0f;

    k.mode = 0;
    b.CaptureDepth(depSrv.Get());
    Check(b.Execute(src.Get(), dst.Get(), k), "identity round trip submitted");
    auto got = fx::ReadBack(r.dev.Get(), r.ctx.Get(), dst.Get(), W, H);
    fx::Diff d = fx::Compare(got, colour, 0);
    printf("       identity: %u mismatches, max delta %d\n", d.mismatches, d.maxDelta);
    Check(!got.empty() && d.mismatches == 0, "identity: every pixel identical after D3D11 -> D3D12 -> D3D11");

    k.mode = 1;
    Check(b.CaptureDepth(depSrv.Get()), "banded R24G8 depth copied into shared depth");
    b.Execute(src.Get(), dst.Get(), k);
    got = fx::ReadBack(r.dev.Get(), r.ctx.Get(), dst.Get(), W, H);
    auto want = fx::Expected(W, H, k, false, 0);
    d = fx::Compare(got, want, 2);
    printf("       haze/banded: %u mismatches, max delta %d\n", d.mismatches, d.maxDelta);
    Check(!got.empty() && d.mismatches == 0, "haze over banded depth matches the CPU reference (tol 2/255)");

    // A real depth-stencil, as the game's depth is after the typeless upgrade.
    auto ds = fx::MakeTex(r.dev.Get(), W, H, DXGI_FORMAT_R24G8_TYPELESS,
                          D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, nullptr, 0);
    ComPtr<ID3D11DepthStencilView> dsv;
    D3D11_DEPTH_STENCIL_VIEW_DESC dvd{}; dvd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    r.dev->CreateDepthStencilView(ds.Get(), &dvd, &dsv);
    r.ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.75f, 0);
    auto dsSrv = fx::MakeSrv(r.dev.Get(), ds.Get(), DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
    Check(b.CaptureDepth(dsSrv.Get()), "depth-stencil texture copied into shared depth");
    b.Execute(src.Get(), dst.Get(), k);
    got = fx::ReadBack(r.dev.Get(), r.ctx.Get(), dst.Get(), W, H);
    float d075 = static_cast<float>(std::lround(0.75 * 16777215.0)) / 16777215.0f;
    want = fx::Expected(W, H, k, true, d075);
    d = fx::Compare(got, want, 2);
    printf("       haze/depth-stencil: %u mismatches, max delta %d\n", d.mismatches, d.maxDelta);
    Check(!got.empty() && d.mismatches == 0, "haze over a cleared depth-stencil matches the CPU reference");

    // The comparator must notice a single wrong pixel.
    auto bad = want; bad[W * 7 + 9] ^= 0x00404040u;
    Check(fx::Compare(got, bad, 2).mismatches == 1, "comparator self-check: one corrupted reference pixel is reported");

    // CONTROL: wrong fence value.
    k.mode = 0;
    b.SetWaitSkew(1000);
    b.Execute(src.Get(), dst.Get(), k);
    ComPtr<ID3D11Query> ev; D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0};
    r.dev->CreateQuery(&qd, &ev); r.ctx->End(ev.Get()); r.ctx->Flush();
    bool d12Done = b.WaitForD3D12(750);
    BOOL evDone = FALSE;
    bool d11Done = r.ctx->GetData(ev.Get(), &evDone, sizeof(evDone), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && evDone;
    printf("       control: D3D12 waits for %llu, D3D11 signalled %llu; D3D12 done=%d, D3D11 done=%d after 750 ms\n",
           (unsigned long long)b.LastWaitedValue(), (unsigned long long)(b.LastWaitedValue() - 1000),
           (int)d12Done, (int)d11Done);
    Check(!d12Done && !d11Done, "CONTROL: wrong fence value is detected (both queues stall, CPU wait times out)");
    b.ForceSignal(b.LastWaitedValue());
    bool released = b.WaitForD3D12(3000);
    Check(released, "CONTROL: stall released by signalling the awaited value from the CPU");
    b.SetWaitSkew(0);
    b.Execute(src.Get(), dst.Get(), k);
    got = fx::ReadBack(r.dev.Get(), r.ctx.Get(), dst.Get(), W, H);
    Check(!got.empty() && fx::Compare(got, colour, 0).mismatches == 0, "after the control, a correct round trip passes again");
}

struct Stat { std::vector<double> v; double Median() { std::sort(v.begin(), v.end()); return v.empty() ? 0 : v[v.size() / 2]; }
              double P95() { std::sort(v.begin(), v.end()); return v.empty() ? 0 : v[(v.size() * 95) / 100]; } };

static std::string Perf(Rig& r, UINT W, UINT H, int frames)
{
    auto src = fx::MakeTex(r.dev.Get(), W, H, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, nullptr, 0);
    auto ds = fx::MakeTex(r.dev.Get(), W, H, DXGI_FORMAT_R24G8_TYPELESS, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, nullptr, 0);
    auto dsSrv = fx::MakeSrv(r.dev.Get(), ds.Get(), DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
    D3D12Bridge b;
    BridgeConfig cfg; cfg.width = W; cfg.height = H;
    if (!b.Initialize(r.dev.Get(), r.ctx.Get(), cfg, Log)) { Check(false, "perf bridge initialised"); return "{}"; }
    PassConstants k;
    Stat depth, in, out, stall, pass, sync, cost;
    for (int f = 0; f < frames; ++f) {
        b.CaptureDepth(dsSrv.Get());
        b.Execute(src.Get(), src.Get(), k);
        BridgeTimings t;
        if (f >= 8 && b.ReadTimings(b.FramesExecuted() - 1, true, &t)) {
            depth.v.push_back(t.depthCopyMs); in.v.push_back(t.copyInMs); out.v.push_back(t.copyOutMs);
            stall.v.push_back(t.stallMs); pass.v.push_back(t.passMs); sync.v.push_back(t.syncMs); cost.v.push_back(t.bridgeCostMs);
        }
    }
    char buf[1024];
    snprintf(buf, sizeof buf,
        "{\"width\":%u,\"height\":%u,\"samples\":%zu,\"median_ms\":{\"depth_copy\":%.4f,\"copy_in\":%.4f,\"copy_out\":%.4f,"
        "\"stall\":%.4f,\"d3d12_pass\":%.4f,\"sync\":%.4f,\"bridge_cost\":%.4f},\"p95_ms\":{\"sync\":%.4f,\"bridge_cost\":%.4f}}",
        W, H, cost.v.size(), depth.Median(), in.Median(), out.Median(), stall.Median(), pass.Median(), sync.Median(),
        cost.Median(), sync.P95(), cost.P95());
    printf("       %ux%u: bridge cost median %.3f ms (depth %.3f, in %.3f, out %.3f, sync %.3f), D3D12 pass %.3f ms, n=%zu\n",
           W, H, cost.Median(), depth.Median(), in.Median(), out.Median(), sync.Median(), pass.Median(), cost.v.size());
    Check(cost.v.size() > static_cast<size_t>(frames / 2), "timestamps read back on both queues");
    return buf;
}

int main(int argc, char** argv)
{
    bool warp = false, skyrimLike = false; const char* outPath = nullptr; int frames = 240;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--warp") warp = true;
        else if (a == "--skyrim-like") skyrimLike = true;
        else if (a == "--out" && i + 1 < argc) outPath = argv[++i];
        else if (a == "--frames" && i + 1 < argc) frames = atoi(argv[++i]);
    }
    Rig r;
    if (!r.Create(warp, skyrimLike)) { printf("[FAIL] D3D11 device\n"); return 2; }
    Correctness(r);
    {
        D3D12Bridge b;
        BridgeConfig cfg; cfg.width = 256; cfg.height = 128; cfg.debugLayer = true;
        if (b.Initialize(r.dev.Get(), r.ctx.Get(), cfg, Log)) { fx::ExtraShares(r.dev.Get(), b, Check); fx::MoreShares(r.dev.Get(), r.ctx.Get(), b, Check); }
        else Check(false, "bridge for the on-demand shares initialised");
    }
    std::string p1 = Perf(r, 1920, 1080, frames), p2 = Perf(r, 3840, 2160, frames);
    if (outPath) {
        FILE* f = nullptr;
        if (fopen_s(&f, outPath, "w") == 0 && f) {
            fprintf(f, "{\"adapter\":\"%s\",\"checks_failed\":%d,\"perf\":[%s,%s]}\n",
                    warp ? "WARP" : "hardware", g_fail, p1.c_str(), p2.c_str());
            fclose(f);
        }
    }
    printf("%s: %d check(s) failed\n", g_fail ? "FAILED" : "ALL CHECKS PASSED", g_fail);
    return g_fail ? 1 : 0;
}
