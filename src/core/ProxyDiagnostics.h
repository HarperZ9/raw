#pragma once
//=============================================================================
//  ProxyDiagnostics — Bridge proxy-side stats into SKSE-side DebugGUI
//
//  The d3d11 proxy DLL exposes statistics via ProxyInterface (frame stats,
//  CB dirty tracking, state cache redundancy, render phases, occlusion,
//  material pipeline, HDR).  This class reads those stats each frame and
//  presents them in a structured format for the DebugGUI "Diagnostics" tab.
//
//  All data is read-only — no proxy state is modified.
//=============================================================================

#include <cstdint>

namespace SB
{

// Snapshot of proxy diagnostics (copied from ProxyInterface each frame)
struct ProxySnapshot
{
    bool     connected = false;    // ProxyInterface* found

    // Frame stats
    uint32_t drawCalls      = 0;
    uint32_t rtSwitches     = 0;
    uint32_t shaderChanges  = 0;
    uint32_t frameCount     = 0;

    // HDR
    bool     hdrCapable     = false;
    bool     hdrEnabled     = false;
    float    hdrMaxNits     = 0.0f;
    float    hdrPaperWhite  = 0.0f;
    uint32_t backbufferFmt  = 0;

    // CB dirty tracking
    uint32_t cbMaps         = 0;
    uint32_t cbSkipped      = 0;
    uint32_t cbCommitted    = 0;
    uint32_t cbTracked      = 0;
    float    cbSaveRate     = 0.0f;   // % skipped

    // State cache redundancy
    uint32_t srvRedundant   = 0;
    uint32_t srvTotal       = 0;
    uint32_t blendRedundant = 0;
    uint32_t blendTotal     = 0;
    uint32_t dsRedundant    = 0;
    uint32_t dsTotal        = 0;
    uint32_t rsRedundant    = 0;
    uint32_t rsTotal        = 0;
    float    totalSaveRate  = 0.0f;   // overall redundancy %

    // Occlusion culling
    uint32_t occTested      = 0;
    uint32_t occCulled      = 0;
    float    occCullRate    = 0.0f;

    // Render phase
    const char* phaseName   = "N/A";
    uint8_t     phaseId     = 0;

    // Material pipeline
    bool     matActive      = false;
    uint32_t matPatched     = 0;
    uint32_t matCandidates  = 0;
    uint32_t matClassified  = 0;
    bool     deferredActive = false;
};

class ProxyDiagnostics
{
public:
    static ProxyDiagnostics& Get()
    {
        static ProxyDiagnostics inst;
        return inst;
    }

    // Attempt to connect to proxy (call once at kDataLoaded)
    bool Connect();

    // Update snapshot from proxy (call each frame)
    void Update();

    // Get current snapshot
    const ProxySnapshot& GetSnapshot() const { return m_snap; }
    bool IsConnected() const { return m_connected; }

private:
    ProxyDiagnostics() = default;

    bool m_connected = false;
    void* m_proxyInterface = nullptr;   // ProxyInterface* (opaque to avoid header dep)
    ProxySnapshot m_snap{};
};

} // namespace SB
