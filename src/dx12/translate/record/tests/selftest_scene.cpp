#include "selftest_scene.h"
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>

namespace selftest
{
namespace
{
const char kShaders[] = R"(
cbuffer C : register(b0) { float4 rect; float4 weights; float4 tint; float4 misc; };
struct VI { float2 pos : POSITION; float2 uv : TEXCOORD0; };
struct VO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VO vs(VI i) { VO o; o.pos = float4(i.pos * rect.zw + rect.xy, misc.x, 1); o.uv = i.uv; return o; }
Texture2D t0 : register(t0); Texture2D t1 : register(t1); Texture2D t2 : register(t2);
SamplerState s0 : register(s0);
float4 ps(VO i) : SV_Target
{
    float4 c = t0.Sample(s0, i.uv) * weights.x + t1.Sample(s0, i.uv * 2) * weights.y
             + t2.Sample(s0, i.uv) * weights.z + tint;
    return float4(c.rgb, weights.w);
}
RWTexture2D<float4> target : register(u0);
[numthreads(8, 8, 1)] void cs(uint3 id : SV_DispatchThreadID)
{
    uint v = id.x * 7 + id.y * 13 + (uint)misc.y * 29;
    target[id.xy] = float4((v & 255) / 255.0, ((v >> 2) & 63) / 63.0, ((id.x ^ id.y) & 15) / 15.0, 1);
}
)";

ComPtr<ID3DBlob> Compile(const char* entry, const char* profile)
{
    ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(kShaders, sizeof(kShaders) - 1, "selftest", nullptr, nullptr, entry, profile,
                                  D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, &code, &errors);
    if (FAILED(hr))
        printf("shader %s failed: %s\n", entry, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    return code;
}

bool Tex(ID3D11Device* dev, UINT size, UINT mips, DXGI_FORMAT fmt, UINT bind, UINT misc,
         ComPtr<ID3D11Texture2D>* out)
{
    const D3D11_TEXTURE2D_DESC d{size, size, mips, 1, fmt, {1, 0}, D3D11_USAGE_DEFAULT, bind, 0, misc};
    return SUCCEEDED(dev->CreateTexture2D(&d, nullptr, out->GetAddressOf()));
}
} // namespace

bool Scene::Init(ID3D11Device* dev, ID3D11DeviceContext* ctx)
{
    m_dev = dev;
    m_ctx = ctx;
    if (!CreateShaders() || !CreateGeometry() || !CreateTargets() || !CreateTextures() || !CreateStates())
        return false;
    BindStatic();
    return true;
}

bool Scene::CreateShaders()
{
    ComPtr<ID3DBlob> vs = Compile("vs", "vs_5_0"), ps = Compile("ps", "ps_5_0"), cs = Compile("cs", "cs_5_0");
    if (!vs || !ps || !cs) return false;
    const D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    return SUCCEEDED(m_dev->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &m_vs))
        && SUCCEEDED(m_dev->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &m_ps))
        && SUCCEEDED(m_dev->CreateComputeShader(cs->GetBufferPointer(), cs->GetBufferSize(), nullptr, &m_cs))
        && SUCCEEDED(m_dev->CreateInputLayout(il, 2, vs->GetBufferPointer(), vs->GetBufferSize(), &m_layout));
}

bool Scene::CreateGeometry()
{
    const float verts[] = {-1, -1, 0, 1, -1, 1, 0, 0, 1, 1, 1, 0, 1, -1, 1, 1};
    const uint16_t indices[] = {0, 1, 2, 0, 2, 3};
    const D3D11_BUFFER_DESC vbd{sizeof(verts), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
    const D3D11_BUFFER_DESC ibd{sizeof(indices), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0};
    const D3D11_BUFFER_DESC cbd{sizeof(Constants), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER,
                                D3D11_CPU_ACCESS_WRITE, 0, 0};
    const D3D11_SUBRESOURCE_DATA vdata{verts, 0, 0}, idata{indices, 0, 0};
    return SUCCEEDED(m_dev->CreateBuffer(&vbd, &vdata, &m_vb)) && SUCCEEDED(m_dev->CreateBuffer(&ibd, &idata, &m_ib))
        && SUCCEEDED(m_dev->CreateBuffer(&cbd, nullptr, &m_cb));
}

bool Scene::CreateTargets()
{
    const DXGI_FORMAT rgba = DXGI_FORMAT_R8G8B8A8_UNORM;
    const UINT rt = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    return Tex(m_dev, kSize, 1, rgba, rt, 0, &m_bb)
        && Tex(m_dev, kSize, 1, DXGI_FORMAT_D24_UNORM_S8_UINT, D3D11_BIND_DEPTH_STENCIL, 0, &m_depth)
        && Tex(m_dev, kHalf, 1, rgba, rt, 0, &m_off)
        && SUCCEEDED(m_dev->CreateRenderTargetView(m_bb.Get(), nullptr, &m_bbRtv))
        && SUCCEEDED(m_dev->CreateRenderTargetView(m_off.Get(), nullptr, &m_offRtv))
        && SUCCEEDED(m_dev->CreateDepthStencilView(m_depth.Get(), nullptr, &m_dsv))
        && SUCCEEDED(m_dev->CreateShaderResourceView(m_off.Get(), nullptr, &m_offSrv));
}

bool Scene::CreateTextures()
{
    const DXGI_FORMAT rgba = DXGI_FORMAT_R8G8B8A8_UNORM;
    const UINT rt = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (!Tex(m_dev, 64, 0, rgba, rt, D3D11_RESOURCE_MISC_GENERATE_MIPS, &m_checker)
        || !Tex(m_dev, 64, 1, rgba, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, 0, &m_comp)
        || !Tex(m_dev, kHalf, 1, rgba, D3D11_BIND_SHADER_RESOURCE, 0, &m_hist))
        return false;
    std::vector<uint32_t> px(64 * 64), grad(kHalf * kHalf);
    for (UINT y = 0; y < 64; ++y)
        for (UINT x = 0; x < 64; ++x)
            px[y * 64 + x] = (((x / 8) ^ (y / 8)) & 1) ? 0xFFE0E0E0u : (0xFF000000u | (x * 4) | ((y * 4) << 8));
    for (UINT y = 0; y < kHalf; ++y)
        for (UINT x = 0; x < kHalf; ++x) grad[y * kHalf + x] = 0xFF000000u | (x * 2) | ((y * 2) << 16);
    m_ctx->UpdateSubresource(m_checker.Get(), 0, nullptr, px.data(), 64 * 4, 0);
    m_ctx->UpdateSubresource(m_hist.Get(), 0, nullptr, grad.data(), kHalf * 4, 0);
    return SUCCEEDED(m_dev->CreateShaderResourceView(m_checker.Get(), nullptr, &m_checkerSrv))
        && SUCCEEDED(m_dev->CreateShaderResourceView(m_comp.Get(), nullptr, &m_compSrv))
        && SUCCEEDED(m_dev->CreateShaderResourceView(m_hist.Get(), nullptr, &m_histSrv))
        && SUCCEEDED(m_dev->CreateUnorderedAccessView(m_comp.Get(), nullptr, &m_compUav));
}

bool Scene::CreateStates()
{
    D3D11_BLEND_DESC alpha{}, opaque{};
    alpha.RenderTarget[0] = {TRUE, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD,
                             D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL};
    opaque.RenderTarget[0] = alpha.RenderTarget[0];
    opaque.RenderTarget[0].BlendEnable = FALSE;
    opaque.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN
                                                 | D3D11_COLOR_WRITE_ENABLE_BLUE;
    D3D11_DEPTH_STENCIL_DESC dss{};
    dss.DepthEnable = TRUE;
    dss.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dss.DepthFunc = D3D11_COMPARISON_LESS;
    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE;
    rs.DepthClipEnable = TRUE;
    D3D11_SAMPLER_DESC samp{};
    samp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samp.MaxLOD = D3D11_FLOAT32_MAX;
    const D3D11_QUERY_DESC qd{D3D11_QUERY_OCCLUSION, 0};
    return SUCCEEDED(m_dev->CreateBlendState(&alpha, &m_blendAlpha))
        && SUCCEEDED(m_dev->CreateBlendState(&opaque, &m_blendOpaque))
        && SUCCEEDED(m_dev->CreateDepthStencilState(&dss, &m_dss))
        && SUCCEEDED(m_dev->CreateRasterizerState(&rs, &m_rs))
        && SUCCEEDED(m_dev->CreateSamplerState(&samp, &m_sampler)) && SUCCEEDED(m_dev->CreateQuery(&qd, &m_query));
}

// State set once, before any capture. A captured frame never sets it again,
// so a replay only works when the initial-state block restores it.
void Scene::BindStatic()
{
    const UINT stride = 16, offset = 0;
    const float one[4] = {1, 1, 1, 1};
    m_ctx->GenerateMips(m_checkerSrv.Get());
    m_ctx->IASetInputLayout(m_layout.Get());
    m_ctx->IASetVertexBuffers(0, 1, m_vb.GetAddressOf(), &stride, &offset);
    m_ctx->IASetIndexBuffer(m_ib.Get(), DXGI_FORMAT_R16_UINT, 0);
    m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_ctx->VSSetShader(m_vs.Get(), nullptr, 0);
    m_ctx->PSSetShader(m_ps.Get(), nullptr, 0);
    m_ctx->CSSetShader(m_cs.Get(), nullptr, 0);
    m_ctx->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
    m_ctx->PSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
    m_ctx->CSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
    m_ctx->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    m_ctx->PSSetShaderResources(2, 1, m_histSrv.GetAddressOf());
    m_ctx->RSSetState(m_rs.Get());
    m_ctx->OMSetDepthStencilState(m_dss.Get(), 0);
    m_ctx->OMSetBlendState(m_blendOpaque.Get(), one, 0xFFFFFFFFu);
    m_ctx->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void Scene::SetConstants(const Constants& c)
{
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(m_ctx->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    memcpy(m.pData, &c, sizeof(c));
    m_ctx->Unmap(m_cb.Get(), 0);
}

void Scene::Quad(float x, float y, float sx, float sy, float z, float w0, float w1, float w2, float alpha,
                 float r, float g, float b)
{
    SetConstants({{x, y, sx, sy}, {w0, w1, w2, alpha}, {r, g, b, 0}, {z, 0, 0, 0}});
    m_ctx->DrawIndexed(6, 0, 0);
}

void Scene::ComputePass(int frame)
{
    ID3D11UnorderedAccessView* none = nullptr;
    SetConstants({{}, {}, {}, {0, static_cast<float>(frame), 0, 0}});
    m_ctx->CSSetUnorderedAccessViews(0, 1, m_compUav.GetAddressOf(), nullptr);
    m_ctx->Dispatch(8, 8, 1);
    m_ctx->CSSetUnorderedAccessViews(0, 1, &none, nullptr);
}

void Scene::UpdateChecker(int frame)
{
    uint32_t block[64];
    for (uint32_t& p : block) p = 0xFF000000u | static_cast<uint32_t>(40 * frame) | (static_cast<uint32_t>(255 - 30 * frame) << 8);
    const UINT x = static_cast<UINT>(frame * 5 % 56), y = static_cast<UINT>(frame * 3 % 56);
    const D3D11_BOX box{x, y, 0, x + 8, y + 8, 1};
    m_ctx->UpdateSubresource(m_checker.Get(), 0, &box, block, 8 * 4, 0);
    m_ctx->GenerateMips(m_checkerSrv.Get());
}

// Draws the checker into the offscreen target. The blend state is whatever
// the previous frame left bound, and slots t1 and t2 keep last frame's views.
void Scene::OffscreenPass(int frame)
{
    const D3D11_VIEWPORT vp{0, 0, static_cast<float>(kHalf), static_cast<float>(kHalf), 0, 1};
    const float clear[4] = {0.1f * static_cast<float>(frame), 0.2f, 0.3f, 1};
    m_ctx->OMSetRenderTargets(1, m_offRtv.GetAddressOf(), nullptr);
    m_ctx->RSSetViewports(1, &vp);
    m_ctx->ClearRenderTargetView(m_offRtv.Get(), clear);
    m_ctx->PSSetShaderResources(0, 1, m_checkerSrv.GetAddressOf());
    Quad(0.1f * static_cast<float>(frame) - 0.2f, 0, 0.8f, 0.8f, 0.5f, 1, 0.5f, 0.25f, 0.7f, 0, 0, 0);
}

bool Scene::MakeStamp(int frame, ComPtr<ID3D11ShaderResourceView>* srv)
{
    uint32_t px[16];
    for (int i = 0; i < 16; ++i) px[i] = 0xFF000000u | static_cast<uint32_t>((i * 16 + frame * 20) & 255) << 16;
    const D3D11_TEXTURE2D_DESC d{4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_IMMUTABLE,
                                 D3D11_BIND_SHADER_RESOURCE, 0, 0};
    const D3D11_SUBRESOURCE_DATA init{px, 16, 0};
    ComPtr<ID3D11Texture2D> tex;
    return SUCCEEDED(m_dev->CreateTexture2D(&d, &init, &tex))
        && SUCCEEDED(m_dev->CreateShaderResourceView(tex.Get(), nullptr, srv->GetAddressOf()));
}

void Scene::MainPass(int frame)
{
    const D3D11_VIEWPORT vp{0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1};
    const float clear[4] = {0.02f, 0.02f, 0.05f, 1}, one[4] = {1, 1, 1, 1};
    const float t = static_cast<float>(frame);
    ComPtr<ID3D11ShaderResourceView> stamp;     // created inside the frame, with initial data
    MakeStamp(frame, &stamp);
    m_ctx->OMSetRenderTargets(1, m_bbRtv.GetAddressOf(), m_dsv.Get());
    m_ctx->RSSetViewports(1, &vp);
    m_ctx->ClearRenderTargetView(m_bbRtv.Get(), clear);
    m_ctx->OMSetBlendState(m_blendOpaque.Get(), one, 0xFFFFFFFFu);
    Quad(0, 0, 1, 1, 0.5f, 0, 0, 0, 1, 0.1f, 0.3f, 0.1f);     // tested against LAST frame's depth
    m_ctx->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    m_ctx->PSSetShaderResources(0, 1, m_offSrv.GetAddressOf());
    m_ctx->PSSetShaderResources(1, 1, stamp.GetAddressOf());
    m_ctx->Begin(m_query.Get());
    Quad(-0.6f + 0.15f * t, 0.4f, 0.3f, 0.4f, 0.3f, 1, 0.5f, 0, 1, 0, 0, 0);
    m_ctx->End(m_query.Get());
    m_ctx->PSSetShaderResources(1, 1, m_compSrv.GetAddressOf());
    Quad(0.5f - 0.1f * t, -0.45f, 0.4f, 0.35f, 0.6f, 0, 1, 0, 1, 0, 0, 0);
    Quad(0.1f, 0.05f, 0.45f, 0.45f, 0.2f, 0, 0, 1, 1, 0.02f * t, 0, 0);     // shows the history texture
    const D3D11_BOX box{64, 64, 0, 64 + kHalf, 64 + kHalf, 1};
    m_ctx->CopySubresourceRegion(m_hist.Get(), 0, 0, 0, 0, m_bb.Get(), 0, &box);
}

void Scene::Frame(int frame)
{
    const float one[4] = {1, 1, 1, 1};
    UINT64 samples = 0;
    ComputePass(frame);
    UpdateChecker(frame);
    OffscreenPass(frame);
    MainPass(frame);
    // Left bound for the next frame's offscreen draw.
    m_ctx->OMSetBlendState((frame & 1) ? m_blendAlpha.Get() : m_blendOpaque.Get(), one, 0xFFFFFFFFu);
    while (m_ctx->GetData(m_query.Get(), &samples, sizeof(samples), 0) == S_FALSE) Sleep(0);
}
} // namespace selftest
