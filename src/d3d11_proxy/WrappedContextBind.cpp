#include "ContextHooksShared.h"

namespace SB::Proxy
{

// ============================================================================
//  Compute (forwarded)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::Dispatch(UINT X, UINT Y, UINT Z)
{
    BindingLedger::Get().OnDispatch("Dispatch");
    m_real->Dispatch(X, Y, Z);
    if (g_frameLedgerArmed) FrameLedgerHooks::OnDispatch(m_real, X, Y, Z);
}

void STDMETHODCALLTYPE WrappedContext::DispatchIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
    BindingLedger::Get().OnDispatch("DispatchIndirect");
    m_real->DispatchIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
}

void STDMETHODCALLTYPE WrappedContext::CSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs)
{
    BindingLedger::Get().OnSRV(LS_CS, StartSlot, NumViews, ppSRVs);
    m_real->CSSetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::CSSetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUAVs, const UINT* pUAVInitialCounts)
{
    BindingLedger::Get().OnCSUAV(StartSlot, NumUAVs, ppUAVs);
    m_real->CSSetUnorderedAccessViews(StartSlot, NumUAVs, ppUAVs, pUAVInitialCounts);
}

void STDMETHODCALLTYPE WrappedContext::CSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
{
    m_real->CSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::CSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppCBs)
{
    BindingLedger::Get().OnCB(LS_CS, StartSlot, NumBuffers, ppCBs);
    m_real->CSSetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

// ============================================================================
//  Input assembler (forwarded)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::IASetInputLayout(ID3D11InputLayout* pInputLayout)
{
    m_real->IASetInputLayout(pInputLayout);
}

void STDMETHODCALLTYPE WrappedContext::IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppVBs, const UINT* pStrides, const UINT* pOffsets)
{
    m_real->IASetVertexBuffers(StartSlot, NumBuffers, ppVBs, pStrides, pOffsets);
}

void STDMETHODCALLTYPE WrappedContext::IASetIndexBuffer(ID3D11Buffer* pIB, DXGI_FORMAT Format, UINT Offset)
{
    m_real->IASetIndexBuffer(pIB, Format, Offset);
}

void STDMETHODCALLTYPE WrappedContext::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY Topology)
{
    m_real->IASetPrimitiveTopology(Topology);
}

// ============================================================================
//  VS/GS/HS/DS binding (forwarded)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::VSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppCBs)
{
    if (PG_IsSafeMode()) { m_real->VSSetConstantBuffers(StartSlot, NumBuffers, ppCBs); return; }

    BindingLedger::Get().OnCB(LS_VS, StartSlot, NumBuffers, ppCBs);
    OcclusionCuller::Get().OnVSSetConstantBuffers(StartSlot, NumBuffers, ppCBs);
    m_real->VSSetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE WrappedContext::VSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs)
{
    BindingLedger::Get().OnSRV(LS_VS, StartSlot, NumViews, ppSRVs);
    m_real->VSSetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::VSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
{
    m_real->VSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::GSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppCBs)
{
    BindingLedger::Get().OnCB(LS_GS, StartSlot, NumBuffers, ppCBs);
    m_real->GSSetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE WrappedContext::GSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs)
{
    BindingLedger::Get().OnSRV(LS_GS, StartSlot, NumViews, ppSRVs);
    m_real->GSSetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::GSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
{
    m_real->GSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::HSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs)
{
    BindingLedger::Get().OnSRV(LS_HS, StartSlot, NumViews, ppSRVs);
    m_real->HSSetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::HSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
{
    m_real->HSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::HSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppCBs)
{
    BindingLedger::Get().OnCB(LS_HS, StartSlot, NumBuffers, ppCBs);
    m_real->HSSetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE WrappedContext::DSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs)
{
    BindingLedger::Get().OnSRV(LS_DS, StartSlot, NumViews, ppSRVs);
    m_real->DSSetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::DSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
{
    m_real->DSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::DSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppCBs)
{
    BindingLedger::Get().OnCB(LS_DS, StartSlot, NumBuffers, ppCBs);
    m_real->DSSetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

// ============================================================================
//  Rasterizer / Stream Output (forwarded)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::RSSetState(ID3D11RasterizerState* pRS)
{
    if (PG_IsSafeMode()) { m_real->RSSetState(pRS); return; }

    ++m_totalRS;
    if (pRS == m_cachedRSState) {
        ++m_redundantRS;
        return;
    }
    m_cachedRSState = pRS;
    if (pRS) { D3D11_RASTERIZER_DESC d; pRS->GetDesc(&d);
        EmitStateLine("raster", pRS, {{"fill",(int)d.FillMode},{"cull",(int)d.CullMode},{"front_ccw",(int)d.FrontCounterClockwise},{"scissor",(int)d.ScissorEnable},{"depth_clip",(int)d.DepthClipEnable}}); }
    else EmitStateLine("raster", nullptr, {{"null",1}});
    m_real->RSSetState(pRS);
}

// Coherence membrane (Tier 2.1, sanity slice): viewport/scissor "nothing renders"
// oracle. Throttled to transitions. char(34)/char(10) for quote/newline.
static float g_vpX = 0, g_vpY = 0, g_vpW = 0, g_vpH = 0;
static bool  g_vpWasZero = false;
static int   g_scissorEmits = 0;

static void EmitPipelineState(const char* kind, const char* detail)
{
    if (!PG_DiagPipelineAsserts()) return;
    try {
        std::error_code ec;
        std::filesystem::create_directories("Data/SKSE/Plugins/RAW/live", ec);
        std::ofstream out("Data/SKSE/Plugins/RAW/live/pipeline_state.jsonl", std::ios::app);
        if (!out.is_open()) return;
        const char Q = static_cast<char>(34);
        out << '{' << Q << "assert" << Q << ':' << Q << kind << Q
            << ',' << Q << "detail" << Q << ':' << Q << detail << Q
            << '}' << static_cast<char>(10);
    } catch (...) {}
}

void STDMETHODCALLTYPE WrappedContext::RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT* pViewports)
{
    if (PG_IsSafeMode()) { m_real->RSSetViewports(NumViewports, pViewports); return; }

    if (NumViewports > 0 && pViewports)
        RenderPhaseDetector::Get().OnViewportChange(pViewports[0].Width, pViewports[0].Height);
    if (NumViewports > 0 && pViewports) {
        g_vpX = pViewports[0].TopLeftX; g_vpY = pViewports[0].TopLeftY;
        g_vpW = pViewports[0].Width;    g_vpH = pViewports[0].Height;
        bool zero = (g_vpW <= 0.0f || g_vpH <= 0.0f);
        if (zero && !g_vpWasZero) EmitPipelineState("VIEWPORT_SIZE_ZERO", "viewport width or height is zero");
        g_vpWasZero = zero;
    }
    if (!g_viewportCallbacks.empty() && pViewports && NumViewports > 0 &&
        NumViewports <= D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) {
        D3D11_VIEWPORT edited[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        std::memcpy(edited, pViewports, sizeof(D3D11_VIEWPORT) * NumViewports);
        for (auto& cb : g_viewportCallbacks)
            if (cb) cb(NumViewports, edited);
        m_real->RSSetViewports(NumViewports, edited);
        return;
    }
    m_real->RSSetViewports(NumViewports, pViewports);
}

void STDMETHODCALLTYPE WrappedContext::RSSetScissorRects(UINT NumRects, const D3D11_RECT* pRects)
{
    if (NumRects > 0 && pRects && g_vpW > 0.0f && g_vpH > 0.0f && g_scissorEmits < 32) {
        const D3D11_RECT& r = pRects[0];
        if (r.right <= (LONG)g_vpX || r.left >= (LONG)(g_vpX + g_vpW) ||
            r.bottom <= (LONG)g_vpY || r.top >= (LONG)(g_vpY + g_vpH)) {
            EmitPipelineState("SCISSOR_OUTSIDE_VIEWPORT", "scissor rect is entirely outside the viewport");
            ++g_scissorEmits;
        }
    }
    m_real->RSSetScissorRects(NumRects, pRects);
}

void STDMETHODCALLTYPE WrappedContext::SOSetTargets(UINT NumBuffers, ID3D11Buffer* const* ppSOTargets, const UINT* pOffsets)
{
    m_real->SOSetTargets(NumBuffers, ppSOTargets, pOffsets);
}

} // namespace SB::Proxy
