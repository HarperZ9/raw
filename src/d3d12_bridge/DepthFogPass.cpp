// DepthFogPass: the first pass on RAW's D3D12 device. A full-screen triangle
// that reads shared colour and depth and writes the shared output.
// Shaders are compiled by fxc at build time (DepthFog_VS.h, DepthFog_PS.h).
// Original work, MIT.
#include "D3D12Bridge.h"
#include <wrl/client.h>

#include "DepthFog_VS.h"   // g_DepthFogVS
#include "DepthFog_PS.h"   // g_DepthFogPS

namespace raw::bridge
{
using Microsoft::WRL::ComPtr;

namespace
{
class DepthFogPass final : public IBridgePass
{
public:
    const char* Name() const override { return "depth-fog"; }

    bool Create(ID3D12Device* dev, DXGI_FORMAT outFormat, LogFn log) override
    {
        HMODULE d3d12 = GetModuleHandleW(L"d3d12.dll");
        auto serialize = d3d12 ? reinterpret_cast<PFN_D3D12_SERIALIZE_ROOT_SIGNATURE>(
            GetProcAddress(d3d12, "D3D12SerializeRootSignature")) : nullptr;
        if (!serialize) { log("[DepthFogPass] D3D12SerializeRootSignature missing"); return false; }

        // Root: 16 constants at b0, one table with t0 (colour) and t1 (depth).
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 2;
        range.BaseShaderRegister = 0;
        range.OffsetInDescriptorsFromTableStart = 0;
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.Num32BitValues = 16;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &range;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 2;
        rsd.pParameters = params;

        ComPtr<ID3DBlob> blob, err;
        HRESULT hr = serialize(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
        if (FAILED(hr)) {
            log("[DepthFogPass] root signature: %s",
                err ? static_cast<const char*>(err->GetBufferPointer()) : "unknown error");
            return false;
        }
        hr = dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                      IID_PPV_ARGS(&m_rs));
        if (FAILED(hr)) { log("[DepthFogPass] CreateRootSignature 0x%08X", (unsigned)hr); return false; }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = m_rs.Get();
        pd.VS = {g_DepthFogVS, sizeof(g_DepthFogVS)};
        pd.PS = {g_DepthFogPS, sizeof(g_DepthFogPS)};
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0] = outFormat;
        pd.SampleDesc.Count = 1;
        hr = dev->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&m_pso));
        if (FAILED(hr)) { log("[DepthFogPass] CreateGraphicsPipelineState 0x%08X", (unsigned)hr); return false; }
        return true;
    }

    void Record(ID3D12GraphicsCommandList* cl, const PassIO& io,
                const PassConstants& k) override
    {
        D3D12_VIEWPORT vp{0, 0, static_cast<float>(io.width), static_cast<float>(io.height), 0, 1};
        D3D12_RECT sc{0, 0, static_cast<LONG>(io.width), static_cast<LONG>(io.height)};
        cl->SetPipelineState(m_pso.Get());
        cl->SetGraphicsRootSignature(m_rs.Get());
        cl->SetGraphicsRoot32BitConstants(0, 16, &k, 0);
        cl->SetGraphicsRootDescriptorTable(1, io.inputs);
        cl->RSSetViewports(1, &vp);
        cl->RSSetScissorRects(1, &sc);
        cl->OMSetRenderTargets(1, &io.output, FALSE, nullptr);
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cl->DrawInstanced(3, 1, 0, 0);
    }

private:
    ComPtr<ID3D12RootSignature> m_rs;
    ComPtr<ID3D12PipelineState> m_pso;
};
} // namespace

std::unique_ptr<IBridgePass> MakeDepthFogPass() { return std::make_unique<DepthFogPass>(); }

} // namespace raw::bridge
