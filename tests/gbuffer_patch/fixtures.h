#pragma once
// A small D3D11 bench for the patch tests: compile HLSL, draw one full-screen
// triangle at a known depth into eight float targets, read them back.
// Original work, MIT.
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace bench
{
using Microsoft::WRL::ComPtr;
constexpr UINT kSize = 8, kTargets = 8;
constexpr float kDepth = 0.25f;

inline const char* kVertex = R"(
struct Out { float4 pos : SV_Position; float2 uv : TEXCOORD0; float3 world : TEXCOORD1; };
Out main(uint id : SV_VertexID) {
    Out o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0.25, 1);
    o.uv = p;
    o.world = float3(p, 1);
    return o;
})";

struct Pixel { float v[4]; };

struct Bench
{
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11Texture2D> rt[kTargets];
    ComPtr<ID3D11RenderTargetView> rtv[kTargets];
    ComPtr<ID3D11Texture2D> staging;
    ComPtr<ID3D11Buffer> cb[13];
    ComPtr<ID3D11ShaderResourceView> srv[2];
    ComPtr<ID3D11SamplerState> sampler;
    bool warp = false;

    bool Create(bool forceWarp)
    {
        const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        HRESULT hr = forceWarp ? E_FAIL : D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1,
                                                            D3D11_SDK_VERSION, &dev, nullptr, &ctx);
        if (FAILED(hr)) {
            warp = true;
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
        }
        if (FAILED(hr)) return false;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = td.Height = kSize; td.MipLevels = td.ArraySize = 1; td.SampleDesc.Count = 1;
        td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
        for (UINT i = 0; i < kTargets; ++i)
            if (FAILED(dev->CreateTexture2D(&td, nullptr, &rt[i])) || FAILED(dev->CreateRenderTargetView(rt[i].Get(), nullptr, &rtv[i]))) return false;
        td.BindFlags = 0; td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &staging))) return false;
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        dev->CreateSamplerState(&sd, &sampler);
        const std::vector<uint8_t> v = Compile(kVertex, "vs_5_0");
        return !v.empty() && SUCCEEDED(dev->CreateVertexShader(v.data(), v.size(), nullptr, &vs));
    }

    static std::vector<uint8_t> Compile(const char* src, const char* target)
    {
        ComPtr<ID3DBlob> code, err;
        if (FAILED(D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err))) {
            printf("compile failed: %s\n", err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
            return {};
        }
        const uint8_t* p = static_cast<const uint8_t*>(code->GetBufferPointer());
        return std::vector<uint8_t>(p, p + code->GetBufferSize());
    }

    static std::string Disassemble(const std::vector<uint8_t>& code)
    {
        ComPtr<ID3DBlob> text;
        if (FAILED(D3DDisassemble(code.data(), code.size(), 0, nullptr, &text))) return {};
        return std::string(static_cast<const char*>(text->GetBufferPointer()));
    }

    // A constant buffer of `vectors` float4 at a slot, filled from data.
    void SetConstants(UINT slot, const float* data, UINT vectors)
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = vectors * 16; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA init{data};
        dev->CreateBuffer(&bd, &init, cb[slot].ReleaseAndGetAddressOf());
    }
    // A 1 x 1 float texture at a pixel shader slot.
    void SetTexel(UINT slot, float r, float g, float b, float a)
    {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = td.Height = 1; td.MipLevels = td.ArraySize = 1; td.SampleDesc.Count = 1;
        td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const float px[4] = {r, g, b, a};
        D3D11_SUBRESOURCE_DATA init{px, 16, 16};
        ComPtr<ID3D11Texture2D> t;
        dev->CreateTexture2D(&td, &init, &t);
        dev->CreateShaderResourceView(t.Get(), nullptr, srv[slot].ReleaseAndGetAddressOf());
    }

    // Draws with a pixel shader and returns the centre pixel of each target. Targets are
    // cleared to -1 first, so an output the shader does not write reads back as -1.
    bool Draw(const std::vector<uint8_t>& ps, Pixel out[kTargets], ID3D11BlendState* blend = nullptr)
    {
        ComPtr<ID3D11PixelShader> shader;
        const HRESULT hr = dev->CreatePixelShader(ps.data(), ps.size(), nullptr, &shader);
        if (FAILED(hr)) { printf("CreatePixelShader failed: 0x%08lX\n", static_cast<unsigned long>(hr)); return false; }
        const float clear[4] = {-1, -1, -1, -1};
        ID3D11RenderTargetView* views[kTargets];
        for (UINT i = 0; i < kTargets; ++i) { views[i] = rtv[i].Get(); ctx->ClearRenderTargetView(views[i], clear); }
        ctx->OMSetRenderTargets(kTargets, views, nullptr);
        ctx->OMSetBlendState(blend, nullptr, 0xFFFFFFFF);
        const D3D11_VIEWPORT vp{0, 0, float(kSize), float(kSize), 0, 1};
        ctx->RSSetViewports(1, &vp);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(shader.Get(), nullptr, 0);
        ID3D11Buffer* buffers[13];
        for (int i = 0; i < 13; ++i) buffers[i] = cb[i].Get();
        ctx->PSSetConstantBuffers(0, 13, buffers);
        ID3D11ShaderResourceView* views2[2] = {srv[0].Get(), srv[1].Get()};
        ctx->PSSetShaderResources(0, 2, views2);
        ID3D11SamplerState* samplers[2] = {sampler.Get(), sampler.Get()};
        ctx->PSSetSamplers(0, 2, samplers);
        ctx->Draw(3, 0);
        for (UINT i = 0; i < kTargets; ++i) {
            ctx->CopyResource(staging.Get(), rt[i].Get());
            D3D11_MAPPED_SUBRESOURCE m{};
            if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
            memcpy(out[i].v, static_cast<const uint8_t*>(m.pData) + m.RowPitch * (kSize / 2) + 16 * (kSize / 2), 16);
            ctx->Unmap(staging.Get(), 0);
        }
        return true;
    }
};

} // namespace bench
