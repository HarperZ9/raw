#include "ContextHooksShared.h"

namespace SB::Proxy
{

// ============================================================================
//  Output merger (HOOKED)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView* const* ppRTVs, ID3D11DepthStencilView* pDSV)
{
    if (PG_IsSafeMode()) { m_real->OMSetRenderTargets(NumViews, ppRTVs, pDSV); return; }

    // Substitute our owned DSV for the game's depth DSV
    ID3D11DepthStencilView* actualDSV = DepthOwn_SubstituteDSV(pDSV);
    BindingLedger::Get().OnRTV(NumViews, ppRTVs, actualDSV);

    OnDepthUnbind(m_real, actualDSV);

    ++m_rtSwitches;
    RenderPhaseDetector::Get().OnRTChange(NumViews, ppRTVs, pDSV);  // detect with game's DSV
    if (g_bridgeHookActive) BridgeHook_OnTargets(m_real, NumViews, ppRTVs, actualDSV);
    // G-buffer completion: RAW's own targets join the game's list for four kinds of span.
    ID3D11RenderTargetView* gbViews[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    UINT gbCount = 0;
    const bool gbAdds = g_gbufferActive && GBuffer_OnTargets(m_real, NumViews, ppRTVs, actualDSV, gbViews, &gbCount);
    if (g_frameLedgerArmed) FrameLedgerHooks::OnTargets(m_real, gbAdds ? gbCount : NumViews, gbAdds ? gbViews : ppRTVs, actualDSV);
    for (auto& cb : g_rtChangeCallbacks)
        if (cb) cb(NumViews, ppRTVs, pDSV);
    if (gbAdds) { m_real->OMSetRenderTargets(gbCount, gbViews, actualDSV); return; }

    // G-buffer MRT injection
    auto& phase = RenderPhaseDetector::Get();
    auto& material = MaterialPipeline::Get();
    if (phase.GetCurrentPhase() == RenderPhase::GeometryMain && actualDSV != nullptr) {
        ID3D11RenderTargetView* rtvBuf[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        UINT outCount = 0;
        if (material.InjectGBufferRTs(NumViews, ppRTVs, rtvBuf, outCount)) {
            m_real->OMSetRenderTargets(outCount, rtvBuf, actualDSV);
            return;
        }
    }

    m_real->OMSetRenderTargets(NumViews, ppRTVs, actualDSV);
}

void STDMETHODCALLTYPE WrappedContext::OMSetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView* const* ppRTVs, ID3D11DepthStencilView* pDSV, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUAVs, const UINT* pUAVInitialCounts)
{
    if (PG_IsSafeMode()) { m_real->OMSetRenderTargetsAndUnorderedAccessViews(NumRTVs, ppRTVs, pDSV, UAVStartSlot, NumUAVs, ppUAVs, pUAVInitialCounts); return; }

    ID3D11DepthStencilView* actualDSV = DepthOwn_SubstituteDSV(pDSV);
    BindingLedger::Get().OnRTV(NumRTVs, ppRTVs, actualDSV);
    BindingLedger::Get().OnOMUAV(UAVStartSlot, NumUAVs, ppUAVs);

    OnDepthUnbind(m_real, actualDSV);

    ++m_rtSwitches;
    RenderPhaseDetector::Get().OnRTChange(NumRTVs, ppRTVs, pDSV);
    if (g_bridgeHookActive) BridgeHook_OnTargets(m_real, NumRTVs, ppRTVs, actualDSV);
    if (g_gbufferActive) { ID3D11RenderTargetView* none[8]; UINT c = 0; GBuffer_OnTargets(m_real, 0, nullptr, nullptr, none, &c); }
    if (g_frameLedgerArmed) FrameLedgerHooks::OnTargets(m_real, NumRTVs, ppRTVs, actualDSV);
    for (auto& cb : g_rtChangeCallbacks)
        if (cb) cb(NumRTVs, ppRTVs, pDSV);
    m_real->OMSetRenderTargetsAndUnorderedAccessViews(NumRTVs, ppRTVs, actualDSV, UAVStartSlot, NumUAVs, ppUAVs, pUAVInitialCounts);
}

void STDMETHODCALLTYPE WrappedContext::OMSetBlendState(ID3D11BlendState* pBS, const FLOAT BlendFactor[4], UINT SampleMask)
{
    if (PG_IsSafeMode()) { m_real->OMSetBlendState(pBS, BlendFactor, SampleMask); return; }

    ++m_totalBlend;
    if (g_gbufferActive) GBuffer_OnBlendState(pBS, BlendFactor, SampleMask);

    // Normalize null blend factor to default {1,1,1,1}
    float bf[4] = {1.f, 1.f, 1.f, 1.f};
    if (BlendFactor) {
        bf[0] = BlendFactor[0]; bf[1] = BlendFactor[1];
        bf[2] = BlendFactor[2]; bf[3] = BlendFactor[3];
    }

    if (pBS == m_cachedBlendState && SampleMask == m_cachedSampleMask &&
        bf[0] == m_cachedBlendFactor[0] && bf[1] == m_cachedBlendFactor[1] &&
        bf[2] == m_cachedBlendFactor[2] && bf[3] == m_cachedBlendFactor[3])
    {
        ++m_redundantBlend;
        return;
    }

    m_cachedBlendState = pBS;
    if (pBS) { D3D11_BLEND_DESC d; pBS->GetDesc(&d);
        EmitStateLine("blend", pBS, {{"enable",(int)d.RenderTarget[0].BlendEnable},{"src",(int)d.RenderTarget[0].SrcBlend},{"dst",(int)d.RenderTarget[0].DestBlend},{"op",(int)d.RenderTarget[0].BlendOp},{"writemask",(int)d.RenderTarget[0].RenderTargetWriteMask},{"a2c",(int)d.AlphaToCoverageEnable}}); }
    else EmitStateLine("blend", nullptr, {{"null",1}});
    m_cachedSampleMask = SampleMask;
    m_cachedBlendFactor[0] = bf[0]; m_cachedBlendFactor[1] = bf[1];
    m_cachedBlendFactor[2] = bf[2]; m_cachedBlendFactor[3] = bf[3];
    m_real->OMSetBlendState(pBS, BlendFactor, SampleMask);
}

void STDMETHODCALLTYPE WrappedContext::OMSetDepthStencilState(ID3D11DepthStencilState* pDSS, UINT StencilRef)
{
    if (PG_IsSafeMode()) { m_real->OMSetDepthStencilState(pDSS, StencilRef); return; }

    ++m_totalDS;
    if (pDSS == m_cachedDSState && StencilRef == m_cachedStencilRef)
    {
        ++m_redundantDS;
        return;
    }
    m_cachedDSState    = pDSS;
    if (pDSS) { D3D11_DEPTH_STENCIL_DESC d; pDSS->GetDesc(&d);
        EmitStateLine("depth", pDSS, {{"depth_enable",(int)d.DepthEnable},{"write",(int)d.DepthWriteMask},{"func",(int)d.DepthFunc},{"stencil",(int)d.StencilEnable}}); }
    else EmitStateLine("depth", nullptr, {{"null",1}});
    m_cachedStencilRef = StencilRef;
    m_real->OMSetDepthStencilState(pDSS, StencilRef);
}

// ============================================================================
//  Clear (HOOKED — phase detection)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::ClearRenderTargetView(ID3D11RenderTargetView* pRTV, const FLOAT ColorRGBA[4])
{
    if (g_frameLedgerArmed) FrameLedgerHooks::OnClearTarget(pRTV, ColorRGBA);
    m_real->ClearRenderTargetView(pRTV, ColorRGBA);
}

void STDMETHODCALLTYPE WrappedContext::ClearDepthStencilView(ID3D11DepthStencilView* pDSV, UINT ClearFlags, FLOAT Depth, UINT8 Stencil)
{
    if (PG_IsSafeMode()) { m_real->ClearDepthStencilView(pDSV, ClearFlags, Depth, Stencil); return; }

    PreClearDepthCopy(m_real, pDSV);

    RenderPhaseDetector::Get().OnClearDepth(pDSV);
    if (g_frameLedgerArmed) FrameLedgerHooks::OnClearDepth(pDSV, ClearFlags, Depth, Stencil);
    m_real->ClearDepthStencilView(pDSV, ClearFlags, Depth, Stencil);
}

void STDMETHODCALLTYPE WrappedContext::ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView* pUAV, const UINT Values[4])
{
    m_real->ClearUnorderedAccessViewUint(pUAV, Values);
}

void STDMETHODCALLTYPE WrappedContext::ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView* pUAV, const FLOAT Values[4])
{
    m_real->ClearUnorderedAccessViewFloat(pUAV, Values);
}

// ============================================================================
//  Resource binding (HOOKED — injection hooks added later)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::PSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs)
{
    if (PG_IsSafeMode()) { m_real->PSSetShaderResources(StartSlot, NumViews, ppSRVs); return; }
    BindingLedger::Get().OnSRV(LS_PS, StartSlot, NumViews, ppSRVs);

    ++m_totalSRV;

    // Check if all SRVs in range match our cache
    if (ppSRVs && StartSlot + NumViews <= kMaxPSSRVSlots) {
        bool allMatch = true;
        for (UINT i = 0; i < NumViews; ++i) {
            if (m_cachedPSSRVs[StartSlot + i] != ppSRVs[i]) {
                allMatch = false;
                break;
            }
        }
        if (allMatch) {
            ++m_redundantSRV;
            return;
        }
        // Update cache
        for (UINT i = 0; i < NumViews; ++i)
            m_cachedPSSRVs[StartSlot + i] = ppSRVs[i];
    }

    m_real->PSSetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::PSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppCBs)
{
    BindingLedger::Get().OnCB(LS_PS, StartSlot, NumBuffers, ppCBs);
    m_real->PSSetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE WrappedContext::PSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
{
    if (ppSamplers && NumSamplers > 0 && ppSamplers[0]) {
        D3D11_SAMPLER_DESC d; ppSamplers[0]->GetDesc(&d);
        EmitStateLine("sampler", ppSamplers[0], {{"filter",(int)d.Filter},{"addr_u",(int)d.AddressU},{"addr_v",(int)d.AddressV},{"cmp",(int)d.ComparisonFunc}});
    }
    m_real->PSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

// ============================================================================
//  Map / Unmap (HOOKED — CB dirty tracking)
//
//  For dynamic constant buffers with WRITE_DISCARD:
//    Map  → returns a staging buffer (real Map is NOT called)
//    Unmap → compares staging vs shadow; if identical, skips real Map/Unmap
//  Expected savings: 60-80% of CB uploads (engine re-uploads identical data).
// ============================================================================

HRESULT STDMETHODCALLTYPE WrappedContext::Map(ID3D11Resource* pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource)
{
    if (PG_IsSafeMode()) return m_real->Map(pResource, Subresource, MapType, MapFlags, pMappedResource);

    if (CBDirtyTracker::Get().InterceptMap(m_real, pResource, Subresource, MapType, MapFlags, pMappedResource))
        return S_OK;
    return m_real->Map(pResource, Subresource, MapType, MapFlags, pMappedResource);
}

void STDMETHODCALLTYPE WrappedContext::Unmap(ID3D11Resource* pResource, UINT Subresource)
{
    if (PG_IsSafeMode()) { m_real->Unmap(pResource, Subresource); return; }

    if (CBDirtyTracker::Get().InterceptUnmap(m_real, pResource, Subresource))
        return;
    m_real->Unmap(pResource, Subresource);
}

} // namespace SB::Proxy
