#include "ProxyDiagnostics.h"
#include <Windows.h>
#include <cstring>

// We replicate the minimal ProxyInterface layout needed to read stats.
// This avoids including the full proxy header (which has d3d11 types that
// may conflict in the SKSE plugin context).
namespace
{

// Matches SB::Proxy::OptimizationStats from ProxyAPI.h
struct OptStats
{
    uint32_t cbMapsIntercepted;
    uint32_t cbUpdatesSkipped;
    uint32_t cbUpdatesCommitted;
    uint32_t cbTrackedBuffers;

    uint32_t srvCallsRedundant;
    uint32_t srvCallsTotal;
    uint32_t blendCallsRedundant;
    uint32_t blendCallsTotal;
    uint32_t dsCallsRedundant;
    uint32_t dsCallsTotal;
    uint32_t rsCallsRedundant;
    uint32_t rsCallsTotal;

    uint32_t occDrawsTested;
    uint32_t occDrawsCulled;
};

// Minimal mirror of SB::Proxy::ProxyInterface — field offsets must match exactly.
// We only read scalar fields, so pointer alignment is all that matters.
struct ProxyInterfaceMirror
{
    uint32_t version;

    void*    device;       // ID3D11Device*
    void*    context;      // ID3D11DeviceContext*
    void*    swapChain;    // IDXGISwapChain*

    bool     hdrCapable;
    bool     hdrEnabled;
    uint32_t backbufferFormat;  // DXGI_FORMAT

    uint32_t drawCallsThisFrame;
    uint32_t rtSwitchesThisFrame;
    uint32_t shaderChangesThisFrame;
    uint32_t frameCount;

    // Callback function pointers (5 of them)
    void*    RegisterPrePresent;
    void*    RegisterOnDraw;
    void*    RegisterOnRTChange;
    void*    RegisterOnShaderBind;
    void*    RegisterOnResize;

    // HDR control
    void*    SetHDREnabled;
    float    hdrMaxNits;
    float    hdrPaperWhite;

    // Render phase
    uint8_t  currentPhase;
    void*    GetPhaseName;   // const char*(*)()

    // Material pipeline
    void*    SetMaterialPipelineEnabled;
    bool     materialPipelineActive;
    uint32_t materialPatchedCount;
    uint32_t materialCandidateCount;
    uint32_t materialClassifiedCount;

    // G-buffer SRVs
    bool     deferredActive;
    void*    gBufferAlbedo;
    void*    gBufferNormals;
    void*    gBufferMaterial;
    void*    gameDepthDSV;
    void*    gameDepthSRV;

    // Optimization stats
    OptStats optStats;
};

} // anonymous namespace

namespace SB
{

bool ProxyDiagnostics::Connect()
{
    // Look for our proxy d3d11.dll
    HMODULE hMod = GetModuleHandleA("d3d11.dll");
    if (!hMod) {
        m_connected = false;
        return false;
    }

    // Try both export names
    using GetProxyFn = void*(*)();
    auto fn = reinterpret_cast<GetProxyFn>(GetProcAddress(hMod, "PG_GetProxyInterface"));
    if (!fn)
        fn = reinterpret_cast<GetProxyFn>(GetProcAddress(hMod, "SB_GetProxyInterface"));

    if (!fn) {
        m_connected = false;
        return false;
    }

    m_proxyInterface = fn();
    m_connected = (m_proxyInterface != nullptr);
    return m_connected;
}

void ProxyDiagnostics::Update()
{
    if (!m_connected || !m_proxyInterface) {
        m_snap.connected = false;
        return;
    }

    auto* pi = static_cast<ProxyInterfaceMirror*>(m_proxyInterface);
    m_snap.connected = true;

    // Frame stats
    m_snap.drawCalls     = pi->drawCallsThisFrame;
    m_snap.rtSwitches    = pi->rtSwitchesThisFrame;
    m_snap.shaderChanges = pi->shaderChangesThisFrame;
    m_snap.frameCount    = pi->frameCount;

    // HDR
    m_snap.hdrCapable    = pi->hdrCapable;
    m_snap.hdrEnabled    = pi->hdrEnabled;
    m_snap.hdrMaxNits    = pi->hdrMaxNits;
    m_snap.hdrPaperWhite = pi->hdrPaperWhite;
    m_snap.backbufferFmt = pi->backbufferFormat;

    // CB dirty tracking
    const auto& os = pi->optStats;
    m_snap.cbMaps      = os.cbMapsIntercepted;
    m_snap.cbSkipped   = os.cbUpdatesSkipped;
    m_snap.cbCommitted = os.cbUpdatesCommitted;
    m_snap.cbTracked   = os.cbTrackedBuffers;
    m_snap.cbSaveRate  = (os.cbMapsIntercepted > 0)
        ? (100.0f * os.cbUpdatesSkipped / os.cbMapsIntercepted)
        : 0.0f;

    // State cache
    m_snap.srvRedundant   = os.srvCallsRedundant;
    m_snap.srvTotal       = os.srvCallsTotal;
    m_snap.blendRedundant = os.blendCallsRedundant;
    m_snap.blendTotal     = os.blendCallsTotal;
    m_snap.dsRedundant    = os.dsCallsRedundant;
    m_snap.dsTotal        = os.dsCallsTotal;
    m_snap.rsRedundant    = os.rsCallsRedundant;
    m_snap.rsTotal        = os.rsCallsTotal;

    uint32_t totalRedundant = os.srvCallsRedundant + os.blendCallsRedundant +
                              os.dsCallsRedundant + os.rsCallsRedundant;
    uint32_t totalCalls     = os.srvCallsTotal + os.blendCallsTotal +
                              os.dsCallsTotal + os.rsCallsTotal;
    m_snap.totalSaveRate = (totalCalls > 0) ? (100.0f * totalRedundant / totalCalls) : 0.0f;

    // Occlusion culling
    m_snap.occTested   = os.occDrawsTested;
    m_snap.occCulled   = os.occDrawsCulled;
    m_snap.occCullRate = (os.occDrawsTested > 0)
        ? (100.0f * os.occDrawsCulled / os.occDrawsTested)
        : 0.0f;

    // Render phase
    m_snap.phaseId = pi->currentPhase;
    if (pi->GetPhaseName) {
        auto getPhaseFn = reinterpret_cast<const char*(*)()>(pi->GetPhaseName);
        m_snap.phaseName = getPhaseFn();
    }

    // Material pipeline
    m_snap.matActive     = pi->materialPipelineActive;
    m_snap.matPatched    = pi->materialPatchedCount;
    m_snap.matCandidates = pi->materialCandidateCount;
    m_snap.matClassified = pi->materialClassifiedCount;
    m_snap.deferredActive = pi->deferredActive;
}

} // namespace SB
