#pragma once
//=============================================================================
//  CBDirtyTracker: CPU copies ("shadows") of the game's dynamic constant buffers
//
//  The bridge reads constant buffers on the CPU (camera, world matrices, bone palettes, lights). A dynamic
//  constant buffer lives in write-combined memory and cannot be read back, so the proxy keeps a CPU copy:
//  the game writes into a cached staging block, and Unmap copies it to the GPU.
//
//  Two modes:
//    dedupe  ([Proxy] CBDirtyTracker=1): every dynamic constant buffer is intercepted, and an upload whose
//            bytes did not change is skipped. Measured in Riverwood, its compares and copies cost more CPU
//            than the skipped driver maps save, so it is off by default.
//    shadows (the default): only the buffers a reader has asked for are intercepted. Unmap copies the
//            staging block to the GPU once and swaps it with the shadow: no compare and no second copy.
//            A buffer is asked for by its first GetShadowData/GetShadowSize; its shadow exists from its
//            next Map on. A buffer no reader has asked for in kWantFrames frames goes back to the plain
//            driver path. [Proxy] CBShadowAll=1 shadows every dynamic constant buffer (the old behaviour
//            with the dedupe off), for A/B runs.
//  A buffer not mapped in a frame loses its shadow at the end of the next frame, and its description is read
//  again, so a new buffer created at a freed one's address is never served stale bytes.
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace SB::Proxy
{

class CBDirtyTracker
{
public:
    static CBDirtyTracker& Get();

    // Called from WrappedContext::Map. Returns true if this Map was intercepted (caller must NOT call the
    // real Map); pMappedResource then points at a staging block.
    bool InterceptMap(ID3D11DeviceContext* realCtx, ID3D11Resource* pResource,
                      UINT Subresource, D3D11_MAP MapType, UINT MapFlags,
                      D3D11_MAPPED_SUBRESOURCE* pMappedResource);

    // Called from WrappedContext::Unmap. Returns true if handled (caller must NOT call the real Unmap).
    bool InterceptUnmap(ID3D11DeviceContext* realCtx, ID3D11Resource* pResource, UINT Subresource);

    // Called once per frame from WrappedSwapChain::Present: counters reset, the frame stamp advances.
    void OnFrameEnd();

    // The lighting decode and the ray-tracing scene read the shadows (world matrices, bone palettes): with the
    // bridge on, the buffers they ask for are shadowed whatever [Proxy] CBDirtyTracker says (raw#27).
    static void SetShadowsRequired(bool on) { s_required = on; }
    static bool ShadowsRequired() { return s_required; }

    // Called from WrappedContext::ClearState
    void OnClearState();

    // Per-frame stats (valid until next OnFrameEnd)
    uint32_t GetMapCalls()         const { return m_mapCalls; }
    uint32_t GetSkippedUpdates()   const { return m_skippedUpdates; }
    uint32_t GetCommittedUpdates() const { return m_committedUpdates; }
    uint32_t GetTrackedBuffers()   const { return m_shadowed; }

    // The CPU copy of a dynamic constant buffer as last written, or null. Asking registers the buffer: a
    // buffer asked for and not yet shadowed returns null now and its bytes from its next Map on.
    const uint8_t* GetShadowData(ID3D11Resource* pResource);
    // The buffer's size in bytes when it is a dynamic constant buffer up to kMaxTrackedSize, else 0. Also
    // registers the buffer.
    uint32_t       GetShadowSize(ID3D11Resource* pResource);

private:
    CBDirtyTracker() = default;

    static constexpr uint32_t kMaxTrackedSize = 4096;
    static constexpr uint64_t kWantFrames = 300;
    static inline bool s_required = false;

    struct CBEntry {
        ID3D11Resource*            key = nullptr;
        uint32_t                   byteWidth = 0;      // 0: not a dynamic constant buffer we track
        uint64_t                   seenFrame = 0;      // last frame it was mapped or classified
        uint64_t                   mapFrame = 0;       // last committed upload
        uint64_t                   wantFrame = 0;      // last reader request (0: never)
        bool                       intercepted = false;
        bool                       valid = false;      // shadow holds the last committed bytes
        std::unique_ptr<uint8_t[]> shadow, staging;
    };

    CBEntry* Find(ID3D11Resource* p);
    CBEntry* Classify(ID3D11Resource* p);   // find or insert, (re)reading the description when stale
    void     Grow();
    bool     Wanted(const CBEntry& e) const;

    std::vector<CBEntry> m_table;            // open addressing, linear probe, size a power of two
    uint32_t             m_used = 0;
    uint64_t             m_frame = 2;

    uint32_t m_mapCalls         = 0;
    uint32_t m_skippedUpdates   = 0;
    uint32_t m_committedUpdates = 0;
    uint32_t m_shadowed         = 0;
};

} // namespace SB::Proxy
