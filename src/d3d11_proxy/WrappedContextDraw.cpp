#include "ContextHooksShared.h"
#include "../dx12/translate/Translate.h"
#include "../core/dxbc/GBufferPatch.h"

namespace SB::Proxy
{

// ============================================================================
//  Shader binding (HOOKED)
// ============================================================================

const WrappedContext::BindCache& WrappedContext::VsBind(ID3D11VertexShader* vs, uint64_t gen)
{
    BindCache& e = m_vsTab[BindSlot(vs)];
    if (e.ptr != vs || e.gen != gen) {
        auto& sm = ShaderManager::Get();
        e = {vs, gen, sm.GetReplacementVS(vs), sm.GetVSHash(vs)};
    }
    return e;
}

const WrappedContext::BindCache& WrappedContext::PsBind(ID3D11PixelShader* ps, uint64_t gen)
{
    BindCache& e = m_psTab[BindSlot(ps)];
    if (e.ptr != ps || e.gen != gen) {
        auto& sm = ShaderManager::Get();
        e = {ps, gen, sm.GetReplacementPS(ps), sm.GetPSHash(ps)};
    }
    return e;
}

void STDMETHODCALLTYPE WrappedContext::VSSetShader(ID3D11VertexShader* pVS, ID3D11ClassInstance* const* ppCI, UINT NumCI)
{
    if (PG_IsSafeMode()) { m_real->VSSetShader(pVS, ppCI, NumCI); return; }

    m_currentVS = pVS;
    auto& sm = ShaderManager::Get();
    const uint64_t gen = sm.Generation();
    if (m_vsCache.ptr != pVS || m_vsCache.gen != gen)
        m_vsCache = VsBind(pVS, gen);
    auto* replacement = static_cast<ID3D11VertexShader*>(m_vsCache.repl);
    m_real->VSSetShader(replacement ? replacement : pVS, ppCI, NumCI);

    uint64_t vsHash = m_vsCache.hash;
    if (m_psCache.ptr != m_currentPS || m_psCache.gen != gen)
        m_psCache = PsBind(m_currentPS, gen);
    uint64_t psHash = m_psCache.hash;
    RenderPhaseDetector::Get().OnShaderBind(psHash, vsHash);
    if (g_frameLedgerArmed) FrameLedgerHooks::OnShaders(psHash, vsHash, m_currentPS);
    for (auto& cb : g_shaderBindCallbacks)
        if (cb) cb(m_currentPS, pVS);
}

void STDMETHODCALLTYPE WrappedContext::PSSetShader(ID3D11PixelShader* pPS, ID3D11ClassInstance* const* ppCI, UINT NumCI)
{
    if (PG_IsSafeMode()) { m_real->PSSetShader(pPS, ppCI, NumCI); return; }

    if (pPS != m_currentPS)
    {
        m_currentPS = pPS;
        ++m_shaderChanges;
    }
    // Check for shader replacement
    auto& sm = ShaderManager::Get();
    const uint64_t gen = sm.Generation();
    if (m_psCache.ptr != pPS || m_psCache.gen != gen)
        m_psCache = PsBind(pPS, gen);
    auto* replacement = static_cast<ID3D11PixelShader*>(m_psCache.repl);
    m_real->PSSetShader(replacement ? replacement : pPS, ppCI, NumCI);
    if (g_gbufferActive) GBuffer_OnPixelShader(pPS);

    // Fire shader bind callbacks + phase detector
    uint64_t psHash = m_psCache.hash;
    if (m_vsCache.ptr != m_currentVS || m_vsCache.gen != gen)
        m_vsCache = VsBind(m_currentVS, gen);
    uint64_t vsHash = m_vsCache.hash;
    RenderPhaseDetector::Get().OnShaderBind(psHash, vsHash);
    if (g_frameLedgerArmed) FrameLedgerHooks::OnShaders(psHash, vsHash, pPS);
    {   // Coherence membrane (Tier 3.2): which compiled variant is actually BOUND.
        static std::unordered_set<uint64_t> g_seenUse;
        static uint64_t g_seenRecent[1024] = {};   // pairs already in the set: no hash-set probe on a rebind
        uint64_t key = psHash ^ (vsHash * 1099511628211ULL);
        uint64_t& recent = g_seenRecent[(key ^ (key >> 29)) & 1023];
        if (psHash && recent != key && (recent = key, g_seenUse.insert(key).second)) {
            try {
                std::error_code ec;
                std::filesystem::create_directories("Data/SKSE/Plugins/RAW/live", ec);
                std::ofstream out("Data/SKSE/Plugins/RAW/live/shader_variants.jsonl", std::ios::app);
                if (out.is_open()) {
                    const char Q = static_cast<char>(34);
                    out << '{' << Q << "kind" << Q << ':' << Q << "shader_used" << Q
                        << ',' << Q << "ps_hash" << Q << ':' << Q << "0x" << std::hex << psHash << std::dec << Q
                        << ',' << Q << "vs_hash" << Q << ':' << Q << "0x" << std::hex << vsHash << std::dec << Q
                        << '}' << static_cast<char>(10);
                }
            } catch (...) {}
        }
    }
    for (auto& cb : g_shaderBindCallbacks)
        if (cb) cb(pPS, m_currentVS);
}

void STDMETHODCALLTYPE WrappedContext::GSSetShader(ID3D11GeometryShader* pShader, ID3D11ClassInstance* const* ppCI, UINT NumCI)
{
    m_real->GSSetShader(pShader, ppCI, NumCI);
}

void STDMETHODCALLTYPE WrappedContext::HSSetShader(ID3D11HullShader* pShader, ID3D11ClassInstance* const* ppCI, UINT NumCI)
{
    m_real->HSSetShader(pShader, ppCI, NumCI);
}

void STDMETHODCALLTYPE WrappedContext::DSSetShader(ID3D11DomainShader* pShader, ID3D11ClassInstance* const* ppCI, UINT NumCI)
{
    m_real->DSSetShader(pShader, ppCI, NumCI);
}

void STDMETHODCALLTYPE WrappedContext::CSSetShader(ID3D11ComputeShader* pShader, ID3D11ClassInstance* const* ppCI, UINT NumCI)
{
    m_real->CSSetShader(pShader, ppCI, NumCI);
}

// ============================================================================
//  Draw calls (HOOKED)
// ============================================================================

void WrappedContext::RecordLiveDrawClassification(uint32_t indexOrVertexCount, bool indexed, bool transformedActive)
{
    if (indexOrVertexCount <= 6u) return; // fullscreen quad / UI strip
    const bool triTopo = (m_drawBindState.topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST ||
                          m_drawBindState.topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP ||
                          m_drawBindState.topology == D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
    if (!triTopo) return;

    raw::dxbc::material::DrawClassifyInput in{};
    in.materialBase   = indexed ? raw::dxbc::material::Lit : raw::dxbc::material::Landscape;
    in.primitiveCount = indexOrVertexCount / 3u;
    in.vbStride       = m_drawBindState.vb0Stride;
    in.srvSignature   = m_drawBindState.psSrvHash ^ reinterpret_cast<uintptr_t>(m_currentPS);
    in.hasSkinning    = (m_drawBindState.vb0Stride >= 52u);
    const auto cls    = raw::dxbc::material::ClassifyLiveDraw14(in);
    const uint8_t id  = static_cast<uint8_t>(cls);
    if (id <= raw::dxbc::material::kAssetClass14Count) {
        ++m_classDrawHistogram[id];
    }
    if (transformedActive && id != 0u) {
        ++m_transformedDrawCalls;
    } else {
        ++m_vanillaDrawCalls;
    }
}

void STDMETHODCALLTYPE WrappedContext::Draw(UINT VertexCount, UINT StartVertexLocation)
{
    if (PG_IsSafeMode()) { m_real->Draw(VertexCount, StartVertexLocation); return; }
    BindingLedger::Get().OnDraw("Draw");
    if (g_frameLedgerArmed) FrameLedgerHooks::OnDraw(m_real, "Draw", VertexCount, 1);

    ++m_drawCalls;
    RecordLiveDrawClassification(VertexCount, false, g_gbufferActive);
    RenderPhaseDetector::Get().OnDraw(VertexCount, false);
    for (auto& cb : g_drawCallbacks)
        if (cb) cb(VertexCount, 1);
    if (g_sceneLightsActive) SceneLights_BeforeDraw(m_real);
    if (g_sceneGeometryActive) SceneGeometry_Census(m_real, 3);
    if (g_gbufferActive) GBuffer_BeforeDraw(m_real);
    if (g_sceneLightsActive) GameComposite_BeforeDraw(m_real, VertexCount);
    if (VertexCount <= 6) Tonemap_BeforeDraw(m_real);   // a full-screen quad at most
    m_real->Draw(VertexCount, StartVertexLocation);
    if (g_sceneLightsActive) GameComposite_AfterDraw(m_real);
    Tonemap_AfterDraw(m_real);
}

void STDMETHODCALLTYPE WrappedContext::DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{
    if (PG_IsSafeMode()) { m_real->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation); return; }
    BindingLedger::Get().OnDraw("DrawIndexed");
    if (g_frameLedgerArmed) FrameLedgerHooks::OnDraw(m_real, "DrawIndexed", IndexCount, 1);

    ++m_drawCalls;
    RecordLiveDrawClassification(IndexCount, true, g_gbufferActive);
    RenderPhaseDetector::Get().OnDraw(IndexCount, true);
    for (auto& cb : g_drawCallbacks)
        if (cb) cb(IndexCount, 1);
    if (g_sceneLightsActive) SceneLights_BeforeDraw(m_real);
    if (g_sceneGeometryActive) SceneGeometry_OnDraw(m_real, IndexCount, StartIndexLocation, BaseVertexLocation);
    // Temporal-reprojected Hi-Z occlusion culling runs AFTER SceneGeometry_OnDraw so occluded static objects
    // still populate the Persistent World TLAS (zero RT shadow/reflection pop-in) while skipping rasterization.
    if (OcclusionCuller::Get().ShouldCull(IndexCount))
        return;
    if (g_gbufferActive) GBuffer_BeforeDraw(m_real);
    if (g_sceneLightsActive) GameComposite_BeforeDraw(m_real, IndexCount);
    if (IndexCount <= 6) Tonemap_BeforeDraw(m_real);   // a full-screen quad at most
    m_real->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
    if (g_gbufferActive) raw::tx::SetDrawWaterHint(m_real, false);
    if (g_sceneLightsActive) GameComposite_AfterDraw(m_real);
    Tonemap_AfterDraw(m_real);
}

void STDMETHODCALLTYPE WrappedContext::DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation)
{
    if (PG_IsSafeMode()) { m_real->DrawInstanced(VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation); return; }
    BindingLedger::Get().OnDraw("DrawInstanced");
    if (g_frameLedgerArmed) FrameLedgerHooks::OnDraw(m_real, "DrawInstanced", VertexCountPerInstance, InstanceCount);

    ++m_drawCalls;
    RenderPhaseDetector::Get().OnDraw(VertexCountPerInstance, false);
    for (auto& cb : g_drawCallbacks)
        if (cb) cb(VertexCountPerInstance, InstanceCount);
    if (g_sceneLightsActive) SceneLights_BeforeDraw(m_real);
    if (g_sceneGeometryActive) SceneGeometry_Census(m_real, 2);
    if (g_gbufferActive) GBuffer_BeforeDraw(m_real);
    m_real->DrawInstanced(VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
}

void STDMETHODCALLTYPE WrappedContext::DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation)
{
    if (PG_IsSafeMode()) { m_real->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation); return; }
    BindingLedger::Get().OnDraw("DrawIndexedInstanced");
    if (g_frameLedgerArmed) FrameLedgerHooks::OnDraw(m_real, "DrawIndexedInstanced", IndexCountPerInstance, InstanceCount);

    ++m_drawCalls;
    RenderPhaseDetector::Get().OnDraw(IndexCountPerInstance, true);
    for (auto& cb : g_drawCallbacks)
        if (cb) cb(IndexCountPerInstance, InstanceCount);
    if (g_sceneLightsActive) SceneLights_BeforeDraw(m_real);
    if (g_sceneGeometryActive) SceneGeometry_Census(m_real, 1);
    if (g_gbufferActive) GBuffer_BeforeDraw(m_real);
    m_real->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
}

void STDMETHODCALLTYPE WrappedContext::DrawAuto()
{
    if (PG_IsSafeMode()) { m_real->DrawAuto(); return; }

    ++m_drawCalls;
    m_real->DrawAuto();
}

void STDMETHODCALLTYPE WrappedContext::DrawIndexedInstancedIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
    if (PG_IsSafeMode()) { m_real->DrawIndexedInstancedIndirect(pBufferForArgs, AlignedByteOffsetForArgs); return; }

    ++m_drawCalls;
    m_real->DrawIndexedInstancedIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
}

void STDMETHODCALLTYPE WrappedContext::DrawInstancedIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
    if (PG_IsSafeMode()) { m_real->DrawInstancedIndirect(pBufferForArgs, AlignedByteOffsetForArgs); return; }

    ++m_drawCalls;
    m_real->DrawInstancedIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
}

} // namespace SB::Proxy
