#include "WrappedContextImpl.h"

namespace SB::Proxy
{

// ============================================================================
//  Get state (all forwarded)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::VSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppCBs)
{
    m_real->VSGetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE WrappedContext::PSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppSRVs)
{
    m_real->PSGetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::PSGetShader(ID3D11PixelShader** ppPS, ID3D11ClassInstance** ppCI, UINT* pNumCI)
{
    m_real->PSGetShader(ppPS, ppCI, pNumCI);
}

void STDMETHODCALLTYPE WrappedContext::PSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers)
{
    m_real->PSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::VSGetShader(ID3D11VertexShader** ppVS, ID3D11ClassInstance** ppCI, UINT* pNumCI)
{
    m_real->VSGetShader(ppVS, ppCI, pNumCI);
}

void STDMETHODCALLTYPE WrappedContext::PSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppCBs)
{
    m_real->PSGetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE WrappedContext::IAGetInputLayout(ID3D11InputLayout** ppInputLayout)
{
    m_real->IAGetInputLayout(ppInputLayout);
}

void STDMETHODCALLTYPE WrappedContext::IAGetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppVBs, UINT* pStrides, UINT* pOffsets)
{
    m_real->IAGetVertexBuffers(StartSlot, NumBuffers, ppVBs, pStrides, pOffsets);
}

void STDMETHODCALLTYPE WrappedContext::IAGetIndexBuffer(ID3D11Buffer** pIB, DXGI_FORMAT* Format, UINT* Offset)
{
    m_real->IAGetIndexBuffer(pIB, Format, Offset);
}

void STDMETHODCALLTYPE WrappedContext::GSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppCBs)
{
    m_real->GSGetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE WrappedContext::GSGetShader(ID3D11GeometryShader** ppGS, ID3D11ClassInstance** ppCI, UINT* pNumCI)
{
    m_real->GSGetShader(ppGS, ppCI, pNumCI);
}

void STDMETHODCALLTYPE WrappedContext::IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY* pTopology)
{
    m_real->IAGetPrimitiveTopology(pTopology);
}

void STDMETHODCALLTYPE WrappedContext::VSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppSRVs)
{
    m_real->VSGetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::VSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers)
{
    m_real->VSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::GetPredication(ID3D11Predicate** ppPredicate, BOOL* pPredicateValue)
{
    m_real->GetPredication(ppPredicate, pPredicateValue);
}

void STDMETHODCALLTYPE WrappedContext::GSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppSRVs)
{
    m_real->GSGetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::GSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers)
{
    m_real->GSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::OMGetRenderTargets(UINT NumViews, ID3D11RenderTargetView** ppRTVs, ID3D11DepthStencilView** ppDSV)
{
    m_real->OMGetRenderTargets(NumViews, ppRTVs, ppDSV);
}

void STDMETHODCALLTYPE WrappedContext::OMGetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView** ppRTVs, ID3D11DepthStencilView** ppDSV, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView** ppUAVs)
{
    m_real->OMGetRenderTargetsAndUnorderedAccessViews(NumRTVs, ppRTVs, ppDSV, UAVStartSlot, NumUAVs, ppUAVs);
}

void STDMETHODCALLTYPE WrappedContext::OMGetBlendState(ID3D11BlendState** ppBS, FLOAT BlendFactor[4], UINT* pSampleMask)
{
    m_real->OMGetBlendState(ppBS, BlendFactor, pSampleMask);
}

void STDMETHODCALLTYPE WrappedContext::OMGetDepthStencilState(ID3D11DepthStencilState** ppDSS, UINT* pStencilRef)
{
    m_real->OMGetDepthStencilState(ppDSS, pStencilRef);
}

void STDMETHODCALLTYPE WrappedContext::SOGetTargets(UINT NumBuffers, ID3D11Buffer** ppSOTargets)
{
    m_real->SOGetTargets(NumBuffers, ppSOTargets);
}

void STDMETHODCALLTYPE WrappedContext::RSGetState(ID3D11RasterizerState** ppRS)
{
    m_real->RSGetState(ppRS);
}

void STDMETHODCALLTYPE WrappedContext::RSGetViewports(UINT* pNumViewports, D3D11_VIEWPORT* pViewports)
{
    m_real->RSGetViewports(pNumViewports, pViewports);
}

void STDMETHODCALLTYPE WrappedContext::RSGetScissorRects(UINT* pNumRects, D3D11_RECT* pRects)
{
    m_real->RSGetScissorRects(pNumRects, pRects);
}

void STDMETHODCALLTYPE WrappedContext::HSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppSRVs)
{
    m_real->HSGetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::HSGetShader(ID3D11HullShader** ppHS, ID3D11ClassInstance** ppCI, UINT* pNumCI)
{
    m_real->HSGetShader(ppHS, ppCI, pNumCI);
}

void STDMETHODCALLTYPE WrappedContext::HSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers)
{
    m_real->HSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::HSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppCBs)
{
    m_real->HSGetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE WrappedContext::DSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppSRVs)
{
    m_real->DSGetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::DSGetShader(ID3D11DomainShader** ppDS, ID3D11ClassInstance** ppCI, UINT* pNumCI)
{
    m_real->DSGetShader(ppDS, ppCI, pNumCI);
}

void STDMETHODCALLTYPE WrappedContext::DSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers)
{
    m_real->DSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::DSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppCBs)
{
    m_real->DSGetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE WrappedContext::CSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppSRVs)
{
    m_real->CSGetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::CSGetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView** ppUAVs)
{
    m_real->CSGetUnorderedAccessViews(StartSlot, NumUAVs, ppUAVs);
}

void STDMETHODCALLTYPE WrappedContext::CSGetShader(ID3D11ComputeShader** ppCS, ID3D11ClassInstance** ppCI, UINT* pNumCI)
{
    m_real->CSGetShader(ppCS, ppCI, pNumCI);
}

void STDMETHODCALLTYPE WrappedContext::CSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers)
{
    m_real->CSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::CSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppCBs)
{
    m_real->CSGetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

} // namespace SB::Proxy
