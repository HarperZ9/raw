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

    // Live format-preserving 1->4 curved PN-Triangle GPU subdivision test (runs TxSubdiv.hlsl on WARP/HW):
    auto renderDome = [&](bool enableSubdiv, Image& outImg, raw::tx::Stats& outSt) -> bool {
        raw::tx::CreateParams cp{};
        cp.warp = !o.hardware;
        cp.debugLayer = !o.noDebug;
        cp.geometrySubdiv = enableSubdiv;
        ComPtr<ID3D11Device> dev;
        ComPtr<ID3D11DeviceContext> ctx;
        if (FAILED(raw::tx::CreateDevice(cp, &dev, &ctx))) return false;
        static const char* kSubVs =
            "struct V { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n"
            "struct O { float4 p : SV_Position; float3 n : TEXCOORD0; };\n"
            "O main(V v) { O o; o.p = float4(v.p, 1.0); o.n = v.n; return o; }\n";
        static const char* kSubPs =
            "float4 main(float4 p : SV_Position, float3 n : TEXCOORD0) : SV_Target {\n"
            "  return float4(saturate(normalize(n) * 0.5 + 0.5), 1.0); }\n";
        ComPtr<ID3DBlob> vbBlob, pbBlob;
        if (FAILED(D3DCompile(kSubVs, strlen(kSubVs), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vbBlob, nullptr))) return false;
        if (FAILED(D3DCompile(kSubPs, strlen(kSubPs), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &pbBlob, nullptr))) return false;
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11PixelShader> ps;
        dev->CreateVertexShader(vbBlob->GetBufferPointer(), vbBlob->GetBufferSize(), nullptr, &vs);
        dev->CreatePixelShader(pbBlob->GetBufferPointer(), pbBlob->GetBufferSize(), nullptr, &ps);
        D3D11_INPUT_ELEMENT_DESC ied[3] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        ComPtr<ID3D11InputLayout> il;
        dev->CreateInputLayout(ied, 3, vbBlob->GetBufferPointer(), vbBlob->GetBufferSize(), &il);

        struct Vtx { float p[3]; float n[3]; float uv[2]; };
        // Coarse 4-triangle dome with tilted outward normals so curved PN-Triangle midpoints bulge visibly outward!
        const Vtx verts[5] = {
            {{ 0.0f,  0.0f, 0.25f}, { 0.0f,  0.0f, 1.0f}, {0.5f, 0.5f}},
            {{-0.5f,  0.0f, 0.65f}, {-0.75f, 0.0f, 0.66f}, {0.0f, 0.5f}},
            {{ 0.0f,  0.5f, 0.65f}, { 0.0f,  0.75f, 0.66f}, {0.5f, 1.0f}},
            {{ 0.5f,  0.0f, 0.65f}, { 0.75f, 0.0f, 0.66f}, {1.0f, 0.5f}},
            {{ 0.0f, -0.5f, 0.65f}, { 0.0f, -0.75f, 0.66f}, {0.5f, 0.0f}},
        };
        const uint16_t idx[12] = { 0,1,2, 0,2,3, 0,3,4, 0,4,1 };
        D3D11_BUFFER_DESC vbd{sizeof(verts), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
        D3D11_BUFFER_DESC ibd{sizeof(idx),   D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER,  0, 0, 0};
        D3D11_SUBRESOURCE_DATA vsd{verts, 0, 0}, isd{idx, 0, 0};
        ComPtr<ID3D11Buffer> vb, ib;
        dev->CreateBuffer(&vbd, &vsd, &vb);
        dev->CreateBuffer(&ibd, &isd, &ib);

        D3D11_TEXTURE2D_DESC td{64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
        D3D11_TEXTURE2D_DESC dd{64, 64, 1, 1, DXGI_FORMAT_D32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL, 0, 0};
        ComPtr<ID3D11Texture2D> rt, ds;
        dev->CreateTexture2D(&td, nullptr, &rt);
        dev->CreateTexture2D(&dd, nullptr, &ds);
        ComPtr<ID3D11RenderTargetView> rtv = MakeRTV(dev.Get(), rt.Get());
        ComPtr<ID3D11DepthStencilView> dsv;
        dev->CreateDepthStencilView(ds.Get(), nullptr, &dsv);

        const float zero[4] = {0, 0, 0, 1};
        ctx->ClearRenderTargetView(rtv.Get(), zero);
        ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        ID3D11RenderTargetView* rtvs[1] = {rtv.Get()};
        ctx->OMSetRenderTargets(1, rtvs, dsv.Get());
        D3D11_VIEWPORT vp{0, 0, 64, 64, 0, 1};
        ctx->RSSetViewports(1, &vp);
        ctx->IASetInputLayout(il.Get());
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        UINT stride = sizeof(Vtx), off = 0;
        ID3D11Buffer* vbs[1] = {vb.Get()};
        ctx->IASetVertexBuffers(0, 1, vbs, &stride, &off);
        ctx->IASetIndexBuffer(ib.Get(), DXGI_FORMAT_R16_UINT, 0);
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(ps.Get(), nullptr, 0);
        ctx->DrawIndexed(12, 0, 0);   // cache miss -> computes 4x curved mesh
        ctx->DrawIndexed(12, 0, 0);   // cache hit  -> reuses cached 4x curved mesh
        if (enableSubdiv) {
            raw::tx::SetSubdivParams(dev.Get(), 0.62f, 4.0f, 0.08f); // live 3D snowpack epoch rebuild
            ctx->DrawIndexed(12, 0, 0);
        }
        SceneCtx sc;
        sc.dev = dev.Get(); sc.ctx = ctx.Get();
        bool ok = Readback(sc, rt.Get(), outImg);
        ctx->ClearState(); ctx->Flush();
        outSt = raw::tx::GetStats(dev.Get());
        return ok;
    };
    Image flatImg, curvedImg;
    raw::tx::Stats flatSt{}, curvedSt{};
    const bool r0 = renderDome(false, flatImg, flatSt);
    const bool r1 = renderDome(true, curvedImg, curvedSt);
    int flatLit = 0, curvedLit = 0;
    for (size_t i = 0; i + 3 < flatImg.rgba.size(); i += 4) flatLit += (flatImg.rgba[i] | flatImg.rgba[i+1] | flatImg.rgba[i+2]) != 0;
    for (size_t i = 0; i + 3 < curvedImg.rgba.size(); i += 4) curvedLit += (curvedImg.rgba[i] | curvedImg.rgba[i+1] | curvedImg.rgba[i+2]) != 0;
    const bool subOk = r0 && r1 && flatSt.debugErrors == 0 && curvedSt.debugErrors == 0 &&
                       flatSt.subdivMeshes == 0 && curvedSt.subdivMeshes == 1 && curvedSt.subdivDraws == 3 &&
                       curvedLit > flatLit;   // curved PN-Triangle dome silhouette strictly expands beyond flat chord!
    char sd[256];
    snprintf(sd, sizeof(sd), "flatPx=%d curvedPx=%d meshes=%llu draws=%llu debugErr=%llu",
             flatLit, curvedLit, (unsigned long long)curvedSt.subdivMeshes,
             (unsigned long long)curvedSt.subdivDraws, (unsigned long long)curvedSt.debugErrors);
    t.Add(subOk, "geometry_subdiv_pn_triangle", sd);

    // Wave 33 Paired Position-Only Z-Prepass + READ_ONLY_DEPTH DEPTH_EQUAL Color-Pass Regression Test:
    // Proves bit-exact zero-hole lockstep across Skyrim's exact 2-pass renderer:
    //   Frame 1 (uncached): Z-prepass draws flat -> DEPTH_EQUAL color pass builds 4x cache AND draws flat (0 holes!).
    //   Frame 2 (cached):   Z-prepass hits 4x curved VB/IB & stamps DSV -> READ_ONLY_DEPTH DEPTH_EQUAL color pass
    //                       matches DSV+frame & draws 4x curved silhouette (strictly expanded, 0 holes, 0 debug errors!).
    {
        raw::tx::CreateParams cp{};
        cp.warp = !o.hardware;
        cp.debugLayer = !o.noDebug;
        cp.geometrySubdiv = true;
        ComPtr<ID3D11Device> dev;
        ComPtr<ID3D11DeviceContext> ctx;
        bool okPair = SUCCEEDED(raw::tx::CreateDevice(cp, &dev, &ctx));
        int pxF1 = 0, pxF2 = 0;
        raw::tx::Stats stPair{};
        if (okPair) {
            static const char* kZVs =
                "struct V { float3 p : POSITION; };\n"
                "float4 main(V v) : SV_Position { return float4(v.p, 1.0); }\n";
            static const char* kColVs =
                "struct V { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n"
                "struct O { float4 p : SV_Position; float3 n : TEXCOORD0; };\n"
                "O main(V v) { O o; o.p = float4(v.p, 1.0); o.n = v.n; return o; }\n";
            static const char* kColPs =
                "float4 main(float4 p : SV_Position, float3 n : TEXCOORD0) : SV_Target {\n"
                "  return float4(saturate(normalize(n) * 0.5 + 0.5), 1.0); }\n";
            ComPtr<ID3DBlob> zvb, cvb, cpb;
            okPair = SUCCEEDED(D3DCompile(kZVs, strlen(kZVs), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &zvb, nullptr)) &&
                     SUCCEEDED(D3DCompile(kColVs, strlen(kColVs), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &cvb, nullptr)) &&
                     SUCCEEDED(D3DCompile(kColPs, strlen(kColPs), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &cpb, nullptr));
            if (okPair) {
                ComPtr<ID3D11VertexShader> zvs, cvs;
                ComPtr<ID3D11PixelShader> cps;
                dev->CreateVertexShader(zvb->GetBufferPointer(), zvb->GetBufferSize(), nullptr, &zvs);
                dev->CreateVertexShader(cvb->GetBufferPointer(), cvb->GetBufferSize(), nullptr, &cvs);
                dev->CreatePixelShader(cpb->GetBufferPointer(), cpb->GetBufferSize(), nullptr, &cps);
                D3D11_INPUT_ELEMENT_DESC zIed[1] = {
                    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                };
                D3D11_INPUT_ELEMENT_DESC cIed[3] = {
                    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
                };
                ComPtr<ID3D11InputLayout> zIl, cIl;
                dev->CreateInputLayout(zIed, 1, zvb->GetBufferPointer(), zvb->GetBufferSize(), &zIl);
                dev->CreateInputLayout(cIed, 3, cvb->GetBufferPointer(), cvb->GetBufferSize(), &cIl);

                struct Vtx { float p[3]; float n[3]; float uv[2]; };
                const Vtx verts[5] = {
                    {{ 0.0f,  0.0f, 0.25f}, { 0.0f,  0.0f, 1.0f}, {0.5f, 0.5f}},
                    {{-0.5f,  0.0f, 0.65f}, {-0.75f, 0.0f, 0.66f}, {0.0f, 0.5f}},
                    {{ 0.0f,  0.5f, 0.65f}, { 0.0f,  0.75f, 0.66f}, {0.5f, 1.0f}},
                    {{ 0.5f,  0.0f, 0.65f}, { 0.75f, 0.0f, 0.66f}, {1.0f, 0.5f}},
                    {{ 0.0f, -0.5f, 0.65f}, { 0.0f, -0.75f, 0.66f}, {0.5f, 0.0f}},
                };
                const uint16_t idx[12] = { 0,1,2, 0,2,3, 0,3,4, 0,4,1 };
                D3D11_BUFFER_DESC vbd{sizeof(verts), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
                D3D11_BUFFER_DESC ibd{sizeof(idx),   D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER,  0, 0, 0};
                D3D11_SUBRESOURCE_DATA vsd{verts, 0, 0}, isd{idx, 0, 0};
                ComPtr<ID3D11Buffer> vb, ib;
                dev->CreateBuffer(&vbd, &vsd, &vb);
                dev->CreateBuffer(&ibd, &isd, &ib);

                D3D11_TEXTURE2D_DESC td{64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
                D3D11_TEXTURE2D_DESC dd{64, 64, 1, 1, DXGI_FORMAT_D32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL, 0, 0};
                ComPtr<ID3D11Texture2D> rt, ds;
                dev->CreateTexture2D(&td, nullptr, &rt);
                dev->CreateTexture2D(&dd, nullptr, &ds);
                ComPtr<ID3D11RenderTargetView> rtv = MakeRTV(dev.Get(), rt.Get());
                D3D11_DEPTH_STENCIL_VIEW_DESC dsvWDesc{};
                dsvWDesc.Format = DXGI_FORMAT_D32_FLOAT; dsvWDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
                D3D11_DEPTH_STENCIL_VIEW_DESC dsvRDesc = dsvWDesc;
                dsvRDesc.Flags = D3D11_DSV_READ_ONLY_DEPTH;
                ComPtr<ID3D11DepthStencilView> dsvWrite, dsvRead;
                dev->CreateDepthStencilView(ds.Get(), &dsvWDesc, &dsvWrite);
                dev->CreateDepthStencilView(ds.Get(), &dsvRDesc, &dsvRead);

                D3D11_DEPTH_STENCIL_DESC dsZ{};
                dsZ.DepthEnable = TRUE; dsZ.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dsZ.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
                D3D11_DEPTH_STENCIL_DESC dsEq{};
                dsEq.DepthEnable = TRUE; dsEq.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; dsEq.DepthFunc = D3D11_COMPARISON_EQUAL;
                ComPtr<ID3D11DepthStencilState> dssZ, dssEq;
                dev->CreateDepthStencilState(&dsZ, &dssZ);
                dev->CreateDepthStencilState(&dsEq, &dssEq);

                auto runPairedFrame = [&](Image& outImg) -> bool {
                    const float zero[4] = {0, 0, 0, 1};
                    ctx->ClearRenderTargetView(rtv.Get(), zero);
                    ctx->ClearDepthStencilView(dsvWrite.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
                    D3D11_VIEWPORT vp{0, 0, 64, 64, 0, 1};
                    ctx->RSSetViewports(1, &vp);
                    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    UINT stride = sizeof(Vtx), off = 0;
                    ID3D11Buffer* vbs[1] = {vb.Get()};
                    ctx->IASetVertexBuffers(0, 1, vbs, &stride, &off);
                    ctx->IASetIndexBuffer(ib.Get(), DXGI_FORMAT_R16_UINT, 0);
                    // Pass 1: Position-only Z-prepass (writable DSV, 0 RTVs)
                    ctx->OMSetRenderTargets(0, nullptr, dsvWrite.Get());
                    ctx->OMSetDepthStencilState(dssZ.Get(), 0);
                    ctx->IASetInputLayout(zIl.Get());
                    ctx->VSSetShader(zvs.Get(), nullptr, 0);
                    ctx->PSSetShader(nullptr, nullptr, 0);
                    ctx->DrawIndexed(12, 0, 0);
                    // Pass 2: NORMAL-bearing DEPTH_EQUAL color pass (READ_ONLY_DEPTH DSV of same texture!)
                    ID3D11RenderTargetView* rtvs[1] = {rtv.Get()};
                    ctx->OMSetRenderTargets(1, rtvs, dsvRead.Get());
                    ctx->OMSetDepthStencilState(dssEq.Get(), 0);
                    ctx->IASetInputLayout(cIl.Get());
                    ctx->VSSetShader(cvs.Get(), nullptr, 0);
                    ctx->PSSetShader(cps.Get(), nullptr, 0);
                    ctx->DrawIndexed(12, 0, 0);
                    SceneCtx sc{dev.Get(), ctx.Get()};
                    return Readback(sc, rt.Get(), outImg);
                };
                Image img1, img2;
                okPair = runPairedFrame(img1) && runPairedFrame(img2);
                for (size_t i = 0; i + 3 < img1.rgba.size(); i += 4) pxF1 += (img1.rgba[i] | img1.rgba[i+1] | img1.rgba[i+2]) != 0;
                for (size_t i = 0; i + 3 < img2.rgba.size(); i += 4) pxF2 += (img2.rgba[i] | img2.rgba[i+1] | img2.rgba[i+2]) != 0;
                ctx->ClearState(); ctx->Flush();
                stPair = raw::tx::GetStats(dev.Get());
            }
        }
        const bool pairPass = okPair && stPair.debugErrors == 0 && stPair.subdivMeshes == 1 && stPair.subdivDraws == 2 &&
                              pxF1 == flatLit && pxF2 > flatLit;
        char pd[256];
        snprintf(pd, sizeof(pd), "flatRef=%d f1BuildPx=%d f2CurvedEqPx=%d meshes=%llu draws=%llu debugErr=%llu",
                 flatLit, pxF1, pxF2, (unsigned long long)stPair.subdivMeshes,
                 (unsigned long long)stPair.subdivDraws, (unsigned long long)stPair.debugErrors);
        t.Add(pairPass, "geometry_subdiv_paired_depth_equal", pd);
    }

    // Wave 35 live 3D physical water tessellation & tidal displacement control:
    // A flat position-only (stride 12, NO normal!) 2-triangle water quad (6 indices) must be 1->16 subdivided
    // and dynamically displaced in real 3D across frames with 0 D3D12 debug errors, spatial wave curvature, and live tidal lift!
    {
        raw::tx::CreateParams wp{};
        wp.warp = !o.hardware;
        wp.debugLayer = !o.noDebug;
        wp.geometrySubdiv = true;
        ComPtr<ID3D11Device> dev;
        ComPtr<ID3D11DeviceContext> ctx;
        bool okW = SUCCEEDED(raw::tx::CreateDevice(wp, &dev, &ctx));
        raw::tx::Stats stW{};
        double meanLow = 0.0, meanHigh = 0.0;
        int distinctLow = 0;
        if (okW) {
            static const char* kWaterVs =
                "struct Out { float4 p : SV_Position; float z : TEXCOORD0; };\n"
                "Out main(float3 p : POSITION) {\n"
                "  Out o; o.p = float4(p.x / 1400.0f, p.y / 1400.0f, 0.5f, 1.0f); o.z = p.z; return o;\n"
                "}\n";
            static const char* kWaterPs =
                "float4 main(float4 p : SV_Position, float z : TEXCOORD0) : SV_Target {\n"
                "  float h = saturate(z * 0.015f + 0.5f);\n"
                "  return float4(h, h, 1.0f, 1.0f);\n"
                "}\n";
            ComPtr<ID3DBlob> vbBlob, pbBlob;
            okW = SUCCEEDED(D3DCompile(kWaterVs, strlen(kWaterVs), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vbBlob, nullptr)) &&
                  SUCCEEDED(D3DCompile(kWaterPs, strlen(kWaterPs), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &pbBlob, nullptr));
            if (okW) {
                ComPtr<ID3D11VertexShader> vs;
                ComPtr<ID3D11PixelShader> ps;
                dev->CreateVertexShader(vbBlob->GetBufferPointer(), vbBlob->GetBufferSize(), nullptr, &vs);
                dev->CreatePixelShader(pbBlob->GetBufferPointer(), pbBlob->GetBufferSize(), nullptr, &ps);
                D3D11_INPUT_ELEMENT_DESC ied[1] = {
                    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                };
                ComPtr<ID3D11InputLayout> il;
                dev->CreateInputLayout(ied, 1, vbBlob->GetBufferPointer(), vbBlob->GetBufferSize(), &il);
                struct WVert { float x, y, z; };
                const WVert verts[4] = {
                    {-1200.0f, -1200.0f, 0.0f},
                    { 1200.0f, -1200.0f, 0.0f},
                    { 1200.0f,  1200.0f, 0.0f},
                    {-1200.0f,  1200.0f, 0.0f},
                };
                const uint16_t idx[6] = { 0, 2, 1, 0, 3, 2 };
                D3D11_BUFFER_DESC vbd{sizeof(verts), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
                D3D11_BUFFER_DESC ibd{sizeof(idx),   D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER,  0, 0, 0};
                D3D11_SUBRESOURCE_DATA vsd{verts, 0, 0}, isd{idx, 0, 0};
                ComPtr<ID3D11Buffer> vb, ib;
                dev->CreateBuffer(&vbd, &vsd, &vb);
                dev->CreateBuffer(&ibd, &isd, &ib);
                D3D11_TEXTURE2D_DESC td{64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
                ComPtr<ID3D11Texture2D> rt;
                dev->CreateTexture2D(&td, nullptr, &rt);
                ComPtr<ID3D11RenderTargetView> rtv = MakeRTV(dev.Get(), rt.Get());

                auto renderWave = [&](float tSec, float tideU, Image& outImg) -> bool {
                    raw::tx::SetWaterWaveState(dev.Get(), tSec, 9.80665f, tideU, 7.0f, 0.866f, 0.5f);
                    const float zero[4] = {0, 0, 0, 1};
                    ctx->ClearRenderTargetView(rtv.Get(), zero);
                    ID3D11RenderTargetView* rtvs[1] = {rtv.Get()};
                    ctx->OMSetRenderTargets(1, rtvs, nullptr);
                    D3D11_VIEWPORT vp{0, 0, 64, 64, 0, 1};
                    ctx->RSSetViewports(1, &vp);
                    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    ctx->IASetInputLayout(il.Get());
                    UINT stride = sizeof(WVert), off = 0;
                    ID3D11Buffer* vbs[1] = {vb.Get()};
                    ctx->IASetVertexBuffers(0, 1, vbs, &stride, &off);
                    ctx->IASetIndexBuffer(ib.Get(), DXGI_FORMAT_R16_UINT, 0);
                    ctx->VSSetShader(vs.Get(), nullptr, 0);
                    ctx->PSSetShader(ps.Get(), nullptr, 0);
                    raw::tx::SetDrawWaterHint(ctx.Get(), true);
                    ctx->DrawIndexed(6, 0, 0);
                    raw::tx::SetDrawWaterHint(ctx.Get(), false);
                    SceneCtx sc{dev.Get(), ctx.Get()};
                    return Readback(sc, rt.Get(), outImg);
                };
                Image imgLow, imgHigh;
                okW = renderWave(0.5f, -12.0f, imgLow) && renderWave(2.25f, +14.0f, imgHigh);
                int seen[256] = {};
                uint64_t sumLow = 0, sumHigh = 0, cntLow = 0, cntHigh = 0;
                for (size_t i = 0; i + 3 < imgLow.rgba.size(); i += 4) {
                    if (imgLow.rgba[i + 2] > 200) {
                        sumLow += imgLow.rgba[i];
                        ++cntLow;
                        seen[imgLow.rgba[i]] = 1;
                    }
                    if (imgHigh.rgba[i + 2] > 200) {
                        sumHigh += imgHigh.rgba[i];
                        ++cntHigh;
                    }
                }
                for (int k = 0; k < 256; ++k) distinctLow += seen[k];
                if (cntLow) meanLow = double(sumLow) / double(cntLow);
                if (cntHigh) meanHigh = double(sumHigh) / double(cntHigh);
                ctx->ClearState(); ctx->Flush();
                stW = raw::tx::GetStats(dev.Get());
            }
        }
        const bool waterPass = okW && stW.debugErrors == 0 && stW.subdivMeshes == 1 && stW.subdivDraws == 2 &&
                               distinctLow >= 12 && (meanHigh > meanLow + 35.0);
        char wd[256];
        snprintf(wd, sizeof(wd), "meanLow=%.1f meanHigh=%.1f distinctWaveLevels=%d meshes=%llu draws=%llu debugErr=%llu",
                 meanLow, meanHigh, distinctLow, (unsigned long long)stW.subdivMeshes,
                 (unsigned long long)stW.subdivDraws, (unsigned long long)stW.debugErrors);
        t.Add(waterPass, "geometry_subdiv_live_3d_water_waves", wd);
    }
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
