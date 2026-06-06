#include "PhaseDispatcher.h"
#include "D3D11StateBackup.h"
#include "RenderPipeline.h"
#include "SceneData.h"
#include "FrameCapture.h"
#include <SKSE/SKSE.h>
#include <fstream>
#include <filesystem>
#include <system_error>
#include <processthreadsapi.h>   // GetCurrentThreadId (thread-id witness, Tier 2.2 rider)

// Coherence membrane (Tier 2.2): STATE_RESTORED oracle. Probe the live context
// independently before mid-frame dispatch and after Restore, then diff pointer
// identities. This catches state RAW dirtied that the backup does not even cover
// (stronger than comparing to the backup, which Restore nulls). char(34)/char(10)
// for quote/newline so the source patcher cannot mangle escapes.
namespace {
struct StateProbe {
    ID3D11RenderTargetView*  rtv0 = nullptr;  ID3D11DepthStencilView*  dsv = nullptr;
    ID3D11BlendState*        blend = nullptr; ID3D11DepthStencilState* depth = nullptr;
    ID3D11RasterizerState*   raster = nullptr; ID3D11InputLayout*      il = nullptr;
    ID3D11PixelShader*       ps = nullptr;    ID3D11VertexShader*      vs = nullptr;
    ID3D11ComputeShader*     cs = nullptr;
};

void ProbeState(ID3D11DeviceContext* ctx, StateProbe& s)
{
    if (!ctx) return;
    ID3D11RenderTargetView* rtvs[1] = {};
    ctx->OMGetRenderTargets(1, rtvs, &s.dsv); s.rtv0 = rtvs[0];
    FLOAT bf[4]; UINT sm; ctx->OMGetBlendState(&s.blend, bf, &sm);
    UINT sr; ctx->OMGetDepthStencilState(&s.depth, &sr);
    ctx->RSGetState(&s.raster);
    ctx->IAGetInputLayout(&s.il);
    ctx->PSGetShader(&s.ps, nullptr, nullptr);
    ctx->VSGetShader(&s.vs, nullptr, nullptr);
    ctx->CSGetShader(&s.cs, nullptr, nullptr);
    // Release the AddRef'd interfaces; retain only pointer VALUES for identity diff.
    IUnknown* refs[] = { s.rtv0, s.dsv, s.blend, s.depth, s.raster, s.il, s.ps, s.vs, s.cs };
    for (IUnknown* r : refs) if (r) r->Release();
}

void EmitRestoreCheck(int phase, const StateProbe& a, const StateProbe& b)
{
    struct F { const char* name; const void* x; const void* y; };
    const F fields[] = {
        {"rtv0", a.rtv0, b.rtv0}, {"dsv", a.dsv, b.dsv}, {"blend", a.blend, b.blend},
        {"depth", a.depth, b.depth}, {"raster", a.raster, b.raster}, {"il", a.il, b.il},
        {"ps", a.ps, b.ps}, {"vs", a.vs, b.vs}, {"cs", a.cs, b.cs},
    };
    int mism = 0; for (const F& f : fields) if (f.x != f.y) ++mism;
    if (mism == 0) return;  // emit only when the restore was dirty
    try {
        std::error_code ec;
        std::filesystem::create_directories("Data/SKSE/Plugins/RAW/live", ec);
        std::ofstream out("Data/SKSE/Plugins/RAW/live/restores.jsonl", std::ios::app);
        if (!out.is_open()) return;
        const char Q = static_cast<char>(34);
        out << '{' << Q << "phase" << Q << ':' << phase
            << ',' << Q << "assert" << Q << ':' << Q << "STATE_NOT_RESTORED" << Q
            << ',' << Q << "dirty" << Q << ':' << mism
            << ',' << Q << "tid" << Q << ':' << static_cast<unsigned long>(GetCurrentThreadId())
            << ',' << Q << "fields" << Q << ":[";
        bool first = true;
        for (const F& f : fields) if (f.x != f.y) { if (!first) out << ','; out << Q << f.name << Q; first = false; }
        out << "]}" << static_cast<char>(10);
    } catch (...) {}
}
} // anonymous namespace



namespace SB
{

// ── RenderPhase values (must match d3d11_proxy/RenderPhaseDetector.h) ────
// We use raw uint8_t to avoid including proxy headers in the SKSE plugin.
namespace Phase {
    constexpr uint8_t Unknown       = 0;
    constexpr uint8_t DepthPrepass  = 1;
    constexpr uint8_t ShadowMap     = 2;
    constexpr uint8_t GeometryMain  = 3;
    constexpr uint8_t Decals        = 4;
    constexpr uint8_t Sky           = 5;
    constexpr uint8_t AlphaBlend    = 6;
    constexpr uint8_t PostProcess   = 7;
    constexpr uint8_t UI            = 8;
}


bool PhaseDispatcher::Initialize(ID3D11DeviceContext* realContext,
                                  void (*invalidateCache)())
{
    if (m_initialized) return true;
    if (!realContext) return false;

    m_realContext     = realContext;
    m_invalidateCache = invalidateCache;
    m_initialized    = true;
    m_enabled        = true;

    ResetFrame();

    SKSE::log::info("PhaseDispatcher: initialized (mid-frame dispatch enabled)");
    return true;
}

void PhaseDispatcher::Shutdown()
{
    m_initialized     = false;
    m_realContext      = nullptr;
    m_invalidateCache = nullptr;
}


void PhaseDispatcher::ResetFrame()
{
    for (auto& d : m_dispatched)
        d = false;
}


void PhaseDispatcher::OnPhaseChange(uint8_t oldPhase, uint8_t newPhase)
{
    // Diagnostic: log first 30 callbacks to verify proxy→SKSE wiring
    {
        static int s_cbCount = 0;
        if (s_cbCount++ < 5) {
            SKSE::log::info("PhaseDispatcher::OnPhaseChange({} -> {}) init={} en={} dispatching={}",
                oldPhase, newPhase, m_initialized, m_enabled, m_dispatching);
        }
    }

    if (!m_initialized || !m_enabled) return;

    // Re-entrancy guard: if an effect triggers a state change that fires
    // another phase transition, don't recurse
    if (m_dispatching) return;

    // Phase → Unknown means frame reset (OnPresent)
    if (newPhase == Phase::Unknown) {
        ResetFrame();
        return;
    }

    auto& pipeline = RenderPipeline::Get();
    if (!pipeline.IsInitialized()) return;

    // Map phase transitions to pipeline stages.
    // Each stage fires AT MOST ONCE per frame (guarded by m_dispatched[]).
    PipelineStage stage = PipelineStage::Count;  // sentinel = no dispatch

    // DepthPrepass → anything else = depth prepass complete
    if (oldPhase == Phase::DepthPrepass && newPhase != Phase::DepthPrepass) {
        stage = PipelineStage::PostDepthPrepass;
    }
    // GeometryMain → Sky/AlphaBlend/Decals/PostProcess = opaque geometry complete
    else if (oldPhase == Phase::GeometryMain &&
             (newPhase == Phase::Sky || newPhase == Phase::AlphaBlend ||
              newPhase == Phase::Decals || newPhase == Phase::PostProcess)) {
        stage = PipelineStage::PostGeometry;
    }
    // Sky → AlphaBlend/PostProcess = sky rendering complete
    else if (oldPhase == Phase::Sky &&
             (newPhase == Phase::AlphaBlend || newPhase == Phase::PostProcess)) {
        stage = PipelineStage::PostSky;
    }
    // PostProcess → UI = last chance before HUD rendering
    else if (oldPhase == Phase::PostProcess && newPhase == Phase::UI) {
        stage = PipelineStage::PreUI;
    }

    // Log phase transition to frame capture
    {
        auto& cap = FrameCapture::Get();
        if (cap.IsCapturing()) {
            CapturedPhase cp;
            cp.oldPhase     = oldPhase;
            cp.newPhase     = newPhase;
            cp.mappedStage  = (stage != PipelineStage::Count)
                                  ? static_cast<uint8_t>(stage) : 0xFF;
            cp.dispatched   = (stage != PipelineStage::Count);
            cap.LogPhase(cp);
        }
    }

    if (stage == PipelineStage::Count) return;  // no matching transition

    auto stageIdx = static_cast<uint8_t>(stage);
    if (stageIdx >= 8) return;

    // Don't dispatch the same stage twice per frame
    if (m_dispatched[stageIdx]) return;
    m_dispatched[stageIdx] = true;

    // ── Dispatch! ────────────────────────────────────────────────────
    m_dispatching = true;

    // Log first 10 dispatches, then periodic every 600 frames (skip noisy PostDepthPrepass)
    if (m_dispatchCount < 10 || (m_dispatchCount % 600 == 0 && stage != PipelineStage::PostDepthPrepass)) {
        SKSE::log::info("PhaseDispatcher: dispatching {} (phase {}->{}, dispatch #{})",
            PipelineStageName(stage), oldPhase, newPhase, m_dispatchCount);
    }

    // Update SceneMatrices from NiCamera so effects have live camera data.
    // This reads NiCamera directly — no dependency on tracker system.
    SceneMatrices::Get().UpdateFromNiCamera();

    // Save full D3D11 state on the REAL context.
    D3D11StateBackup backup;
    backup.Save(m_realContext);
    StateProbe s_before; ProbeState(m_realContext, s_before);

    // ── DEPTH DIAGNOSTIC: read back center pixel from depth buffer ──────
    // Runs every 600 dispatches (~10s). Tells us if depth has data at this moment.
    if (stage == PipelineStage::PostGeometry && backup.dsv) {
        static uint32_t s_depthDiag = 0;
        if (s_depthDiag++ % 600 == 0) {
            ID3D11Resource* depthRes = nullptr;
            backup.dsv->GetResource(&depthRes);
            if (depthRes) {
                ID3D11Texture2D* depthTex = nullptr;
                depthRes->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&depthTex);
                depthRes->Release();
                if (depthTex) {
                    D3D11_TEXTURE2D_DESC desc;
                    depthTex->GetDesc(&desc);

                    // Create staging texture for CPU readback
                    D3D11_TEXTURE2D_DESC stagingDesc = desc;
                    stagingDesc.Usage = D3D11_USAGE_STAGING;
                    stagingDesc.BindFlags = 0;
                    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                    stagingDesc.MiscFlags = 0;

                    ID3D11Device* dev = nullptr;
                    m_realContext->GetDevice(&dev);
                    if (dev) {
                        ID3D11Texture2D* staging = nullptr;
                        if (SUCCEEDED(dev->CreateTexture2D(&stagingDesc, nullptr, &staging))) {
                            m_realContext->CopyResource(staging, depthTex);
                            D3D11_MAPPED_SUBRESOURCE mapped;
                            if (SUCCEEDED(m_realContext->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
                                // Read center pixel (4 bytes for D24S8: 3 bytes depth + 1 byte stencil)
                                uint32_t cx = desc.Width / 2;
                                uint32_t cy = desc.Height / 2;
                                uint32_t* row = reinterpret_cast<uint32_t*>(
                                    static_cast<uint8_t*>(mapped.pData) + cy * mapped.RowPitch);
                                uint32_t raw = row[cx];
                                // D24: upper 24 bits = depth, lower 8 = stencil
                                uint32_t depthBits = raw >> 8;
                                float depthFloat = static_cast<float>(depthBits) / 16777215.0f;

                                // Also read corners
                                uint32_t rawTL = reinterpret_cast<uint32_t*>(
                                    static_cast<uint8_t*>(mapped.pData))[0];
                                uint32_t rawBR = reinterpret_cast<uint32_t*>(
                                    static_cast<uint8_t*>(mapped.pData) + (desc.Height-1) * mapped.RowPitch)[desc.Width-1];

                                float dTL = static_cast<float>(rawTL >> 8) / 16777215.0f;
                                float dBR = static_cast<float>(rawBR >> 8) / 16777215.0f;

                                SKSE::log::info("DepthProbe[f{}]: center={:.6f} topLeft={:.6f} botRight={:.6f} (fmt={} {}x{})",
                                    s_depthDiag, depthFloat, dTL, dBR,
                                    static_cast<int>(desc.Format), desc.Width, desc.Height);

                                m_realContext->Unmap(staging, 0);
                            }
                            staging->Release();
                        }
                        dev->Release();
                    }
                    depthTex->Release();
                }
            }
        }
    }

    // Unbind all OM targets to prevent D3D11 read-write hazards.
    {
        ID3D11RenderTargetView* nullRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        m_realContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                          nullRTVs, nullptr);
    }

    // Execute all passes registered at this stage, passing the game's
    // active RTV/DSV so effects can target the correct render target.
    pipeline.ExecuteStage(stage, 0.0f, nullptr, backup.rtvs[0], backup.dsv);

    // Restore state so the game's next phase starts correctly
    backup.Restore(m_realContext);
    { StateProbe s_after; ProbeState(m_realContext, s_after); EmitRestoreCheck(static_cast<int>(stage), s_before, s_after); }

    // Invalidate proxy's state cache — our effects modified state on the
    // real context, so the proxy's redundancy filter is out of sync
    if (m_invalidateCache)
        m_invalidateCache();

    ++m_dispatchCount;
    m_dispatching = false;
}

} // namespace SB
