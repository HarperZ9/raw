// State getters: the same set-then-get sequence on the system D3D11 runtime and
// on the translation layer. Both must return what was set, apply the D3D11 rule
// that a render target cannot stay bound as a shader input, and reset on
// ClearState. Running it on D3D11 first checks the test itself.
#include "suite.h"
#include <atomic>

namespace suite {

static const char* kPs = "Texture2D t : register(t1); SamplerState s : register(s0); cbuffer C : register(b2) { float4 c; };\n"
                         "float4 ps(float4 p : SV_Position) : SV_Target { return t.Sample(s, p.xy) + c; }\n";
static const char* kCs = "RWTexture2D<float4> o : register(u0); [numthreads(1,1,1)] void cs(uint3 i : SV_DispatchThreadID) { o[i.xy] = 1; }\n";

struct Objects {
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11ComputeShader> cs;
    ComPtr<ID3D11Buffer> cb, vb, ib;
    ComPtr<ID3D11SamplerState> samp;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11DepthStencilState> ds;
    ComPtr<ID3D11RasterizerState> rs;
    Target rt;
    Depth depth;
};

static bool Make(ID3D11Device* dev, Objects& o)
{
    o.vs = MakeVS(dev, kFullscreenVS, "vs");
    o.ps = MakePS(dev, kPs, "ps");
    o.cs = MakeCS(dev, kCs, "cs");
    o.cb = MakeBuffer(dev, 64, D3D11_BIND_CONSTANT_BUFFER, nullptr, D3D11_USAGE_DYNAMIC);
    o.vb = MakeBuffer(dev, 256, D3D11_BIND_VERTEX_BUFFER, nullptr);
    o.ib = MakeBuffer(dev, 64, D3D11_BIND_INDEX_BUFFER, nullptr);
    o.samp = MakeSampler(dev, D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP);
    BlendSpec bs;
    bs.enable = true; bs.src = D3D11_BLEND_SRC_ALPHA; bs.dst = D3D11_BLEND_INV_SRC_ALPHA;
    o.blend = MakeBlend(dev, bs);
    o.ds = MakeDepthState(dev, true, false, D3D11_COMPARISON_GREATER);
    o.rs = MakeRaster(dev, D3D11_CULL_FRONT, 0, 0.0f, true);
    return o.vs && o.ps && o.cs && o.cb && o.vb && o.ib && o.samp && o.blend && o.ds && o.rs &&
           MakeTarget(dev, 32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, o.rt) && MakeDepth(dev, 32, 32, o.depth);
}

// Number of wrong answers; each `bad` line is one check.
static int Sequence(ID3D11DeviceContext* x, const Objects& o)
{
    int bad = 0;
    auto check = [&](bool ok) { if (!ok) ++bad; };
    x->ClearState();
    x->VSSetShader(o.vs.Get(), nullptr, 0);
    x->PSSetShader(o.ps.Get(), nullptr, 0);
    x->CSSetShader(o.cs.Get(), nullptr, 0);
    ID3D11Buffer* cb = o.cb.Get();
    x->VSSetConstantBuffers(2, 1, &cb); x->PSSetConstantBuffers(2, 1, &cb); x->CSSetConstantBuffers(2, 1, &cb);
    x->PSSetSamplers(0, 1, o.samp.GetAddressOf());
    const UINT stride = 12, offset = 4;
    x->IASetVertexBuffers(1, 1, o.vb.GetAddressOf(), &stride, &offset);
    x->IASetIndexBuffer(o.ib.Get(), DXGI_FORMAT_R16_UINT, 2);
    x->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
    const float factor[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    x->OMSetBlendState(o.blend.Get(), factor, 0x0F0F0F0Fu);
    x->OMSetDepthStencilState(o.ds.Get(), 7);
    x->RSSetState(o.rs.Get());
    const D3D11_VIEWPORT vp[2] = {{0, 0, 32, 32, 0, 1}, {4, 4, 8, 8, 0.25f, 0.75f}};
    x->RSSetViewports(2, vp);
    const D3D11_RECT sc = {1, 2, 3, 4};
    x->RSSetScissorRects(1, &sc);

    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps; ComPtr<ID3D11ComputeShader> cs;
    ComPtr<ID3D11GeometryShader> gs; ComPtr<ID3D11HullShader> hs; ComPtr<ID3D11DomainShader> dsh;
    x->VSGetShader(&vs, nullptr, nullptr); x->PSGetShader(&ps, nullptr, nullptr); x->CSGetShader(&cs, nullptr, nullptr);
    x->GSGetShader(&gs, nullptr, nullptr); x->HSGetShader(&hs, nullptr, nullptr); x->DSGetShader(&dsh, nullptr, nullptr);
    check(vs == o.vs && ps == o.ps && cs == o.cs && !gs && !hs && !dsh);
    ComPtr<ID3D11Buffer> got[2];
    x->VSGetConstantBuffers(2, 2, got[0].GetAddressOf()); check(got[0] == o.cb && !got[1]);
    got[0].Reset(); x->PSGetConstantBuffers(2, 1, &got[0]); check(got[0] == o.cb);
    got[0].Reset(); x->CSGetConstantBuffers(2, 1, &got[0]); check(got[0] == o.cb);
    ComPtr<ID3D11SamplerState> samp; x->PSGetSamplers(0, 1, &samp); check(samp == o.samp);
    UINT gs1 = 0, go1 = 0; got[0].Reset();
    x->IAGetVertexBuffers(1, 1, &got[0], &gs1, &go1); check(got[0] == o.vb && gs1 == 12 && go1 == 4);
    DXGI_FORMAT fmt{}; UINT ioff = 0; got[0].Reset();
    x->IAGetIndexBuffer(&got[0], &fmt, &ioff); check(got[0] == o.ib && fmt == DXGI_FORMAT_R16_UINT && ioff == 2);
    D3D11_PRIMITIVE_TOPOLOGY topo{}; x->IAGetPrimitiveTopology(&topo); check(topo == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
    ComPtr<ID3D11BlendState> blend; float f[4] = {}; UINT mask = 0;
    x->OMGetBlendState(&blend, f, &mask); check(blend == o.blend && f[2] == 0.3f && mask == 0x0F0F0F0Fu);
    ComPtr<ID3D11DepthStencilState> dss; UINT ref = 0; x->OMGetDepthStencilState(&dss, &ref); check(dss == o.ds && ref == 7);
    ComPtr<ID3D11RasterizerState> rs; x->RSGetState(&rs); check(rs == o.rs);
    UINT nvp = 4; D3D11_VIEWPORT gvp[4]; x->RSGetViewports(&nvp, gvp); check(nvp == 2 && gvp[1].TopLeftX == 4 && gvp[1].MaxDepth == 0.75f);
    UINT nsc = 4; D3D11_RECT gsc[4]; x->RSGetScissorRects(&nsc, gsc); check(nsc == 1 && gsc[0].right == 3);

    // A texture bound as a shader input is unbound when it becomes the render target...
    x->PSSetShaderResources(1, 1, o.rt.srv.GetAddressOf());
    ComPtr<ID3D11ShaderResourceView> srv; x->PSGetShaderResources(1, 1, &srv); check(srv == o.rt.srv);
    x->OMSetRenderTargets(1, o.rt.rtv.GetAddressOf(), o.depth.dsv.Get());
    srv.Reset(); x->PSGetShaderResources(1, 1, &srv); check(!srv);
    // ...and cannot be bound as an input while it is the target.
    x->PSSetShaderResources(1, 1, o.rt.srv.GetAddressOf());
    srv.Reset(); x->PSGetShaderResources(1, 1, &srv); check(!srv);
    ComPtr<ID3D11RenderTargetView> rtv; ComPtr<ID3D11DepthStencilView> dsv;
    x->OMGetRenderTargets(1, &rtv, &dsv); check(rtv == o.rt.rtv && dsv == o.depth.dsv);
    check(x->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE);

    x->ClearState();
    vs.Reset(); x->VSGetShader(&vs, nullptr, nullptr); check(!vs);
    rtv.Reset(); dsv.Reset(); x->OMGetRenderTargets(1, &rtv, &dsv); check(!rtv && !dsv);
    blend.Reset(); x->OMGetBlendState(&blend, f, &mask); check(!blend && f[0] == 1.0f && mask == 0xFFFFFFFFu);
    x->IAGetPrimitiveTopology(&topo); check(topo == D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
    nvp = 0; x->RSGetViewports(&nvp, nullptr); check(nvp == 0);
    return bad;
}

// Counts its references: set as private data on an object, it is released when the object dies.
struct Sentinel final : IUnknown {
    std::atomic<ULONG> n{1};
    STDMETHODIMP QueryInterface(REFIID r, void** o) override
    {
        if (r != __uuidof(IUnknown)) { *o = nullptr; return E_NOINTERFACE; }
        *o = this; AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++n; }
    STDMETHODIMP_(ULONG) Release() override { return --n; }
};
static const GUID kSentinelGuid = {0x7a1c2e55, 0x31f0, 0x4b8e, {0x9d, 0x42, 0x0e, 0x6b, 0x51, 0xa3, 0xc7, 0x19}};

// A view bound while the application drops its reference stays alive while bound, and dies once unbound
// (after a Flush: the immediate context gives its binding references back there). Returns wrong answers.
// The sentinels outlive every device: a runtime may destroy an object later than its last release.
static Sentinel g_sentinels[16];
static int g_sentinelAt = 0;

static int Lifetime(ID3D11Device* dev, ID3D11DeviceContext* x)
{
    int bad = 0;
    for (int round = 0; round < 3; ++round) {
        Target rt;
        if (!MakeTarget(dev, 8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, rt) || g_sentinelAt >= 16) return 100;
        Sentinel& s = g_sentinels[g_sentinelAt++];
        ID3D11ShaderResourceView* v = rt.srv.Detach();
        v->SetPrivateDataInterface(kSentinelGuid, &s);
        const UINT slot = round;   // the same object bound in a few slots and rebound, as the game does
        x->PSSetShaderResources(slot, 1, &v);
        x->VSSetShaderResources(slot, 1, &v);
        x->PSSetShaderResources(slot, 1, &v);
        v->Release();
        rt = Target{};
        x->Flush();
        if (s.n != 2) ++bad;   // still bound: alive
        ID3D11ShaderResourceView* none = nullptr;
        x->PSSetShaderResources(slot, 1, &none);
        x->Flush();
        if (s.n != 2) ++bad;   // still bound to the vertex stage
        x->VSSetShaderResources(slot, 1, &none);
        x->Flush();
        if (x->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED) {
            // A deferred context's recorded commands keep what they used until its command list goes.
            ComPtr<ID3D11CommandList> cl;
            if (FAILED(x->FinishCommandList(FALSE, &cl))) ++bad;
        }
        if (s.n != 1) ++bad;   // unbound and released: gone
    }
    return bad;
}

void RunGetters(const Opts& o, Tally& t)
{
    int bad[2] = {-1, -1};
    unsigned long refs = 99;
    for (int be = 0; be < 2; ++be) {
        RunCfg cfg;
        cfg.be = be ? Backend::Translate : Backend::D3D11;
        cfg.hardware = o.hardware;
        cfg.debug = false;
        ComPtr<ID3D11Device> dev;
        ComPtr<ID3D11DeviceContext> ctx;
        if (!MakeDevice(cfg, dev, ctx)) continue;
        {
            Objects obj;
            if (Make(dev.Get(), obj)) bad[be] = Sequence(ctx.Get(), obj);
            ComPtr<ID3D11DeviceContext> dc;
            if (SUCCEEDED(dev->CreateDeferredContext(0, &dc)) && dc->GetType() != D3D11_DEVICE_CONTEXT_DEFERRED) ++bad[be];
        }
        ctx.Reset();
        if (be) refs = dev.Reset();
    }
    char d[160];
    snprintf(d, sizeof(d), "24 set-then-get checks: %d wrong on D3D11, %d wrong on D3D12, device references left %lu", bad[0], bad[1], refs);
    t.Add(bad[0] == 0 && bad[1] == 0 && refs == 0, "state_getters", d);
}

void RunLifetime(const Opts& o, Tally& t)
{
    int bad[2] = {-1, -1};
    unsigned long refs = 99;
    for (int be = 0; be < 2; ++be) {
        RunCfg cfg;
        cfg.be = be ? Backend::Translate : Backend::D3D11;
        cfg.hardware = o.hardware;
        cfg.debug = false;
        ComPtr<ID3D11Device> dev;
        ComPtr<ID3D11DeviceContext> ctx;
        if (!MakeDevice(cfg, dev, ctx)) continue;
        bad[be] = Lifetime(dev.Get(), ctx.Get());
        ComPtr<ID3D11DeviceContext> dc;
        if (SUCCEEDED(dev->CreateDeferredContext(0, &dc))) bad[be] += Lifetime(dev.Get(), dc.Get());
        dc.Reset();
        ctx.Reset();
        if (be) refs = dev.Reset();
    }
    char d[160];
    snprintf(d, sizeof(d), "18 checks on the immediate and a deferred context: %d wrong on D3D11, %d wrong on D3D12, device references left %lu",
             bad[0], bad[1], refs);
    t.Add(bad[0] == 0 && bad[1] == 0 && refs == 0, "binding_lifetime", d);
}

}  // namespace suite
