// Controls: each plants one defect in the translation layer and requires the
// suite's own checks to catch it. A control passes when the defect is seen.
#include "suite.h"
#include "tx_reflect.h"
#include <d3d11shader.h>

namespace suite {

static void FaultControl(const Opts& o, Tally& t, const char* name, const char* scene, uint32_t fault, bool viaDebugLayer)
{
    const Scene* s = FindScene(scene);
    RunCfg a, b;
    a.hardware = b.hardware = o.hardware;
    b.be = Backend::Translate;
    RunOut ra, clean, bad;
    RunOnce(*s, a, ra);
    RunOnce(*s, b, clean);
    b.faults = fault;
    RunOnce(*s, b, bad);
    int cleanDiff = MaxDiff(ra.img, clean.img), badDiff = MaxDiff(ra.img, bad.img);
    bool cleanOk = cleanDiff >= 0 && cleanDiff <= s->tolerance && clean.stats.debugErrors == 0;
    bool caught = viaDebugLayer ? bad.stats.debugErrors > 0 : (badDiff < 0 || badDiff > s->tolerance);
    char d[256];
    snprintf(d, sizeof(d), "%s: without the defect max diff %d and %llu debug errors; with it max diff %d and %llu debug errors",
             scene, cleanDiff, (unsigned long long)clean.stats.debugErrors, badDiff, (unsigned long long)bad.stats.debugErrors);
    t.Add(cleanOk && caught, name, d);
}

void RunControls(const Opts& o, Tally& t)
{
    // The comparator itself: one channel moved past the tolerance must be reported.
    Image a;
    a.width = a.height = 8;
    a.rgba.assign(8 * 8 * 4, 100);
    Image b = a;
    b.rgba[37] = 103;
    t.Add(MaxDiff(a, b) == 3 && MaxDiff(a, a) == 0, "control_comparator", "one channel changed by 3 is reported as 3");

    FaultControl(o, t, "control_wrong_blend_mapping", "alpha_blend", raw::tx::FaultBlendSwap, false);
    FaultControl(o, t, "control_missing_barrier", "rtt_post", raw::tx::FaultSkipBarrier, true);
    FaultControl(o, t, "control_stale_constant_buffer", "many_draws", raw::tx::FaultStaleCB, false);
}

// ---- bytecode readers ----

static const char* kPs =
    "cbuffer A : register(b0) { float4 a; }; cbuffer B : register(b3) { float4 b; };\n"
    "Texture2D t0 : register(t0); TextureCube t2 : register(t2); Texture2DArray t5 : register(t5);\n"
    "SamplerState s0 : register(s0); SamplerComparisonState s4 : register(s4);\n"
    "float4 main(float4 p : SV_Position) : SV_Target {\n"
    "  return a + b + t0.Sample(s0, p.xy) + t2.Sample(s0, p.xyz) + t5.SampleCmpLevelZero(s4, p.xyz, 0.5); }\n";
static const char* kCs =
    "cbuffer K : register(b1) { uint k; };\n"
    "struct S { float4 v; }; StructuredBuffer<S> sb : register(t1); Texture3D<float4> vol : register(t0);\n"
    "RWTexture2D<float4> o0 : register(u0); RWStructuredBuffer<S> o2 : register(u2); RWTexture3D<float4> o3 : register(u3);\n"
    "[numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) {\n"
    "  float4 v = sb[id.x + k].v + vol[id]; o0[id.xy] = v; o2[id.x].v = v; o3[id] = v; }\n";
static const char* kVs =
    "cbuffer A : register(b2) { float4x4 m; }; Buffer<float4> buf : register(t3);\n"
    "float4 main(float4 p : POSITION, uint id : SV_VertexID) : SV_Position { return mul(p, m) + buf[id]; }\n";

static bool FromD3DReflect(ID3DBlob* blob, raw::tx::ShaderData& out)
{
    ComPtr<ID3D11ShaderReflection> r;
    if (FAILED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&r)))) return false;
    D3D11_SHADER_DESC sd{};
    r->GetDesc(&sd);
    for (UINT i = 0; i < sd.BoundResources; ++i) {
        D3D11_SHADER_INPUT_BIND_DESC b{};
        r->GetResourceBindingDesc(i, &b);
        UINT end = b.BindPoint + (b.BindCount ? b.BindCount : 1);
        if (b.Type == D3D_SIT_CBUFFER) for (UINT s = b.BindPoint; s < end; ++s) out.bind.cbMask |= uint16_t(1u << s);
        else if (b.Type == D3D_SIT_SAMPLER) { if (out.bind.sampCount < end) out.bind.sampCount = uint8_t(end); }
        else if (b.Type == D3D_SIT_TEXTURE || b.Type == D3D_SIT_STRUCTURED || b.Type == D3D_SIT_BYTEADDRESS || b.Type == D3D_SIT_TBUFFER) {
            if (out.bind.srvCount < end) out.bind.srvCount = uint8_t(end);
            if (out.srvDim.size() < end) out.srvDim.resize(end, 0);
            for (UINT s = b.BindPoint; s < end; ++s) out.srvDim[s] = b.Type == D3D_SIT_TEXTURE ? uint8_t(b.Dimension == D3D_SRV_DIMENSION_BUFFEREX ? 1 : b.Dimension) : uint8_t(1);
        } else {
            if (out.bind.uavCount < end) out.bind.uavCount = uint8_t(end);
            if (out.uavDim.size() < end) out.uavDim.resize(end, 0);
            for (UINT s = b.BindPoint; s < end; ++s) out.uavDim[s] = b.Type == D3D_SIT_UAV_RWTYPED ? uint8_t(b.Dimension) : uint8_t(1);
        }
    }
    return true;
}

static bool Same(const raw::tx::ShaderData& a, const raw::tx::ShaderData& b)
{
    return a.bind.cbMask == b.bind.cbMask && a.bind.srvCount == b.bind.srvCount && a.bind.sampCount == b.bind.sampCount &&
           a.bind.uavCount == b.bind.uavCount && a.srvDim == b.srvDim && a.uavDim == b.uavDim;
}

void RunReflection(Tally& t)
{
    const struct { const char* name; const char* src; const char* target; } k[3] = {
        {"reflect_pixel_shader", kPs, "ps_5_0"}, {"reflect_compute_shader", kCs, "cs_5_0"}, {"reflect_vertex_shader", kVs, "vs_5_0"}};
    for (const auto& c : k) {
        ComPtr<ID3DBlob> blob = Compile(c.src, "main", c.target);
        if (!blob) { t.Add(false, c.name, "compile failed"); continue; }
        raw::tx::ShaderData want, rdef, prog, stripped;
        bool ok = FromD3DReflect(blob.Get(), want);
        ok = ok && raw::tx::ReflectDxbc(blob->GetBufferPointer(), blob->GetBufferSize(), raw::tx::ReflectSource::Rdef, rdef);
        ok = ok && raw::tx::ReflectDxbc(blob->GetBufferPointer(), blob->GetBufferSize(), raw::tx::ReflectSource::Program, prog);
        ComPtr<ID3DBlob> bare;
        ok = ok && SUCCEEDED(D3DStripShader(blob->GetBufferPointer(), blob->GetBufferSize(), D3DCOMPILER_STRIP_REFLECTION_DATA, &bare));
        bool noRdef = ok && !raw::tx::ReflectDxbc(bare->GetBufferPointer(), bare->GetBufferSize(), raw::tx::ReflectSource::Rdef, stripped);
        ok = ok && raw::tx::ReflectDxbc(bare->GetBufferPointer(), bare->GetBufferSize(), raw::tx::ReflectSource::Auto, stripped);
        bool same = ok && Same(want, rdef) && Same(want, prog) && Same(want, stripped);
        char d[200];
        snprintf(d, sizeof(d), "cb mask 0x%04x, %u srv, %u samplers, %u uav: RDEF reader, program reader and stripped bytecode %s D3DReflect",
                 want.bind.cbMask, want.bind.srvCount, want.bind.sampCount, want.bind.uavCount, same ? "match" : "DIFFER from");
        t.Add(same && noRdef, c.name, d);
    }
}

// ---- swap chain ----

void RunSwapChain(const Opts& o, Tally& t)
{
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "tx_suite_window";
    RegisterClassA(&wc);
    HWND wnd = CreateWindowA(wc.lpszClassName, "tx", WS_OVERLAPPEDWINDOW, 0, 0, 320, 240, nullptr, nullptr, wc.hInstance, nullptr);
    RunCfg cfg;
    cfg.be = Backend::Translate;
    cfg.hardware = o.hardware;
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    bool ok = wnd && MakeDevice(cfg, dev, ctx);
    HRESULT hr = E_FAIL;
    int presents = 0;
    Image img;
    if (ok) {
        DXGI_SWAP_CHAIN_DESC d{};
        d.BufferDesc.Width = 256; d.BufferDesc.Height = 128; d.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1; d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = 1;
        d.OutputWindow = wnd; d.Windowed = TRUE;
        ComPtr<IDXGISwapChain> sc;
        hr = raw::tx::CreateSwapChain(dev.Get(), &d, &sc);
        for (int pass = 0; pass < 2 && SUCCEEDED(hr); ++pass) {
            ComPtr<ID3D11Texture2D> bb;
            hr = sc->GetBuffer(0, IID_PPV_ARGS(&bb));
            if (FAILED(hr)) break;
            ComPtr<ID3D11RenderTargetView> rtv = MakeRTV(dev.Get(), bb.Get());
            for (int f = 0; f < 3 && SUCCEEDED(hr); ++f) {
                ctx->ClearRenderTargetView(rtv.Get(), kClear);
                hr = sc->Present(0, 0);
                if (SUCCEEDED(hr)) ++presents;
            }
            SceneCtx c;
            c.dev = dev.Get(); c.ctx = ctx.Get();
            ok = ok && Readback(c, bb.Get(), img) && IsClear(img, img.width / 2, img.height / 2);
            rtv.Reset();
            bb.Reset();
            if (pass == 0 && SUCCEEDED(hr)) hr = sc->ResizeBuffers(0, 192, 96, DXGI_FORMAT_UNKNOWN, 0);
        }
    }
    raw::tx::Stats st{};
    if (dev) { ctx->ClearState(); ctx->Flush(); st = raw::tx::GetStats(dev.Get()); }
    ok = ok && SUCCEEDED(hr) && presents == 6 && img.width == 192 && img.height == 96 && st.debugErrors == 0;
    char d[200];
    snprintf(d, sizeof(d), "flip-model chain: %d presents, resize to %ux%u, hr 0x%08lX, debug errors %llu",
             presents, img.width, img.height, (unsigned long)hr, (unsigned long long)st.debugErrors);
    t.Add(ok, "swap_chain_present_resize", d);
    ctx.Reset();
    dev.Reset();
    if (wnd) DestroyWindow(wnd);
}

}  // namespace suite
