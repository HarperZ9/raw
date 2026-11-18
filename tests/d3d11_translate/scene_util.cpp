// Shader, resource and state helpers for the D3D11 test scenes.
#include "scene_util.h"
#include <cstring>

namespace txtest {

bool Fail(const char* scene, const char* what, HRESULT hr)
{
    fprintf(stderr, "[%s] FAILED: %s (hr=0x%08lX)\n", scene, what, static_cast<unsigned long>(hr));
    return false;
}

static bool Bad(const char* what, HRESULT hr)
{
    fprintf(stderr, "[scene_util] %s failed (hr=0x%08lX)\n", what, static_cast<unsigned long>(hr));
    return false;
}

const char* const kFullscreenVS = R"(
struct VO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VO vs(uint id : SV_VertexID)
{
    VO o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
)";

ComPtr<ID3DBlob> Compile(const char* src, const char* entry, const char* target)
{
    ComPtr<ID3DBlob> code, err;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = D3DCompile(src, strlen(src), entry, nullptr, nullptr, entry, target, flags, 0,
                            &code, &err);
    if (FAILED(hr)) {
        fprintf(stderr, "[scene_util] D3DCompile %s (%s) failed hr=0x%08lX:\n%s\n", entry, target,
                static_cast<unsigned long>(hr),
                err ? static_cast<const char*>(err->GetBufferPointer()) : "(no message)");
        return nullptr;
    }
    return code;
}

ComPtr<ID3D11VertexShader> MakeVS(ID3D11Device* dev, const char* src, const char* entry,
                                  ID3DBlob** blobOut)
{
    ComPtr<ID3D11VertexShader> s;
    ComPtr<ID3DBlob> b = Compile(src, entry, "vs_5_0");
    if (!b) return nullptr;
    HRESULT hr = dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s);
    if (FAILED(hr)) { Bad("CreateVertexShader", hr); return nullptr; }
    if (blobOut) *blobOut = b.Detach();
    return s;
}

ComPtr<ID3D11PixelShader> MakePS(ID3D11Device* dev, const char* src, const char* entry)
{
    ComPtr<ID3D11PixelShader> s;
    ComPtr<ID3DBlob> b = Compile(src, entry, "ps_5_0");
    if (!b) return nullptr;
    HRESULT hr = dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s);
    if (FAILED(hr)) { Bad("CreatePixelShader", hr); return nullptr; }
    return s;
}

ComPtr<ID3D11ComputeShader> MakeCS(ID3D11Device* dev, const char* src, const char* entry)
{
    ComPtr<ID3D11ComputeShader> s;
    ComPtr<ID3DBlob> b = Compile(src, entry, "cs_5_0");
    if (!b) return nullptr;
    HRESULT hr = dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s);
    if (FAILED(hr)) { Bad("CreateComputeShader", hr); return nullptr; }
    return s;
}

ComPtr<ID3D11InputLayout> MakeLayout(ID3D11Device* dev, const D3D11_INPUT_ELEMENT_DESC* el,
                                     UINT count, ID3DBlob* vsBlob)
{
    ComPtr<ID3D11InputLayout> l;
    if (!vsBlob) return nullptr;
    HRESULT hr = dev->CreateInputLayout(el, count, vsBlob->GetBufferPointer(),
                                        vsBlob->GetBufferSize(), &l);
    if (FAILED(hr)) { Bad("CreateInputLayout", hr); return nullptr; }
    return l;
}

ComPtr<ID3D11Buffer> MakeBuffer(ID3D11Device* dev, UINT bytes, UINT bind, const void* init,
                                D3D11_USAGE usage, UINT misc, UINT stride)
{
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = bytes;
    bd.Usage = usage;
    bd.BindFlags = bind;
    bd.CPUAccessFlags = usage == D3D11_USAGE_DYNAMIC ? D3D11_CPU_ACCESS_WRITE : 0;
    bd.MiscFlags = misc;
    bd.StructureByteStride = stride;
    D3D11_SUBRESOURCE_DATA sd{init, 0, 0};
    ComPtr<ID3D11Buffer> b;
    HRESULT hr = dev->CreateBuffer(&bd, init ? &sd : nullptr, &b);
    if (FAILED(hr)) { Bad("CreateBuffer", hr); return nullptr; }
    return b;
}

ComPtr<ID3D11Texture2D> MakeTex(ID3D11Device* dev, const TexDesc& d,
                                const D3D11_SUBRESOURCE_DATA* init)
{
    D3D11_TEXTURE2D_DESC td{};
    td.Width = d.w; td.Height = d.h; td.MipLevels = d.mips; td.ArraySize = 1;
    td.Format = d.fmt; td.SampleDesc.Count = d.samples; td.SampleDesc.Quality = d.quality;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = d.bind; td.MiscFlags = d.misc;
    ComPtr<ID3D11Texture2D> t;
    HRESULT hr = dev->CreateTexture2D(&td, init, &t);
    if (FAILED(hr)) { Bad("CreateTexture2D", hr); return nullptr; }
    return t;
}

ComPtr<ID3D11RenderTargetView> MakeRTV(ID3D11Device* dev, ID3D11Texture2D* t)
{
    ComPtr<ID3D11RenderTargetView> v;
    if (!t) return nullptr;
    HRESULT hr = dev->CreateRenderTargetView(t, nullptr, &v);
    if (FAILED(hr)) { Bad("CreateRenderTargetView", hr); return nullptr; }
    return v;
}

ComPtr<ID3D11ShaderResourceView> MakeSRV(ID3D11Device* dev, ID3D11Texture2D* t, DXGI_FORMAT fmt)
{
    ComPtr<ID3D11ShaderResourceView> v;
    if (!t) return nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC d{};
    d.Format = fmt;
    d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    d.Texture2D.MipLevels = static_cast<UINT>(-1);
    HRESULT hr = dev->CreateShaderResourceView(t, fmt == DXGI_FORMAT_UNKNOWN ? nullptr : &d, &v);
    if (FAILED(hr)) { Bad("CreateShaderResourceView", hr); return nullptr; }
    return v;
}

ComPtr<ID3D11DepthStencilView> MakeDSV(ID3D11Device* dev, ID3D11Texture2D* t, DXGI_FORMAT fmt)
{
    ComPtr<ID3D11DepthStencilView> v;
    if (!t) return nullptr;
    D3D11_DEPTH_STENCIL_VIEW_DESC d{};
    d.Format = fmt;
    d.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    HRESULT hr = dev->CreateDepthStencilView(t, fmt == DXGI_FORMAT_UNKNOWN ? nullptr : &d, &v);
    if (FAILED(hr)) { Bad("CreateDepthStencilView", hr); return nullptr; }
    return v;
}

ComPtr<ID3D11UnorderedAccessView> MakeUAV(ID3D11Device* dev, ID3D11Texture2D* t)
{
    ComPtr<ID3D11UnorderedAccessView> v;
    if (!t) return nullptr;
    HRESULT hr = dev->CreateUnorderedAccessView(t, nullptr, &v);
    if (FAILED(hr)) { Bad("CreateUnorderedAccessView", hr); return nullptr; }
    return v;
}

bool MakeTarget(ID3D11Device* dev, UINT w, UINT h, DXGI_FORMAT fmt, Target& out)
{
    TexDesc d;
    d.w = w; d.h = h; d.fmt = fmt;
    d.bind = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    out.tex = MakeTex(dev, d);
    out.rtv = MakeRTV(dev, out.tex.Get());
    out.srv = MakeSRV(dev, out.tex.Get());
    return out.tex && out.rtv && out.srv;
}

bool MakeDepth(ID3D11Device* dev, UINT w, UINT h, Depth& out)
{
    TexDesc d;
    d.w = w; d.h = h; d.fmt = DXGI_FORMAT_D24_UNORM_S8_UINT; d.bind = D3D11_BIND_DEPTH_STENCIL;
    out.tex = MakeTex(dev, d);
    out.dsv = MakeDSV(dev, out.tex.Get());
    return out.tex && out.dsv;
}

ComPtr<ID3D11RasterizerState> MakeRaster(ID3D11Device* dev, D3D11_CULL_MODE cull, int depthBias,
                                         float slopeBias, bool scissor, bool msaa)
{
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = cull;
    rd.DepthBias = depthBias;
    rd.SlopeScaledDepthBias = slopeBias;
    rd.DepthClipEnable = TRUE;
    rd.ScissorEnable = scissor;
    rd.MultisampleEnable = msaa;
    ComPtr<ID3D11RasterizerState> s;
    HRESULT hr = dev->CreateRasterizerState(&rd, &s);
    if (FAILED(hr)) { Bad("CreateRasterizerState", hr); return nullptr; }
    return s;
}

ComPtr<ID3D11DepthStencilState> MakeDepthState(ID3D11Device* dev, bool enable, bool write,
                                               D3D11_COMPARISON_FUNC func)
{
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = enable;
    dd.DepthWriteMask = write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = func;
    ComPtr<ID3D11DepthStencilState> s;
    HRESULT hr = dev->CreateDepthStencilState(&dd, &s);
    if (FAILED(hr)) { Bad("CreateDepthStencilState", hr); return nullptr; }
    return s;
}

ComPtr<ID3D11BlendState> MakeBlend(ID3D11Device* dev, const BlendSpec& sp)
{
    D3D11_BLEND_DESC bd{};
    auto& rt = bd.RenderTarget[0];
    rt.BlendEnable = sp.enable;
    rt.SrcBlend = sp.src; rt.DestBlend = sp.dst; rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = sp.srcA; rt.DestBlendAlpha = sp.dstA; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = sp.mask;
    ComPtr<ID3D11BlendState> s;
    HRESULT hr = dev->CreateBlendState(&bd, &s);
    if (FAILED(hr)) { Bad("CreateBlendState", hr); return nullptr; }
    return s;
}

ComPtr<ID3D11SamplerState> MakeSampler(ID3D11Device* dev, D3D11_FILTER filter,
                                       D3D11_TEXTURE_ADDRESS_MODE addr,
                                       D3D11_COMPARISON_FUNC cmp, float border)
{
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = filter;
    sd.AddressU = sd.AddressV = sd.AddressW = addr;
    sd.MaxAnisotropy = 1;
    sd.ComparisonFunc = cmp;
    for (float& b : sd.BorderColor) b = border;
    sd.MinLOD = 0.0f;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> s;
    HRESULT hr = dev->CreateSamplerState(&sd, &s);
    if (FAILED(hr)) { Bad("CreateSamplerState", hr); return nullptr; }
    return s;
}

void SetViewport(ID3D11DeviceContext* ctx, float w, float h, float x, float y)
{
    D3D11_VIEWPORT vp{x, y, w, h, 0.0f, 1.0f};
    ctx->RSSetViewports(1, &vp);
}

void BindVB(ID3D11DeviceContext* ctx, ID3D11Buffer* vb, UINT stride)
{
    UINT off = 0;
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &off);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

bool WaitQuery(ID3D11DeviceContext* ctx, ID3D11Asynchronous* q, void* data, UINT size)
{
    const ULONGLONG start = GetTickCount64();
    for (;;) {
        HRESULT hr = ctx->GetData(q, data, size, 0);
        if (hr == S_OK) return true;
        if (FAILED(hr)) return Bad("GetData", hr);
        if (GetTickCount64() - start > 5000) return Bad("GetData (5 s timeout)", hr);
        SwitchToThread();
    }
}

}  // namespace txtest
