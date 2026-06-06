#include "CBDirtyTracker.h"
#include "BindingLedger.h"   // CB-ptr -> slot/stage lookup for cb_meta attribution (1.2-p3)
#include <cstring>
#include <fstream>
#include <filesystem>
#include <system_error>
#include <cstdint>

namespace SB::Proxy
{

CBDirtyTracker& CBDirtyTracker::Get()
{
    static CBDirtyTracker instance;
    return instance;
}

CBDirtyTracker::CBEntry* CBDirtyTracker::GetOrCreateEntry(ID3D11Resource* pResource)
{
    auto it = m_entries.find(pResource);
    if (it != m_entries.end())
        return &it->second;

    // Query resource type — only track buffers
    D3D11_RESOURCE_DIMENSION dim;
    pResource->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_BUFFER)
        return nullptr;

    auto* buf = static_cast<ID3D11Buffer*>(pResource);
    D3D11_BUFFER_DESC desc;
    buf->GetDesc(&desc);

    // Only track dynamic constant buffers within size limit
    if (!(desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER))
        return nullptr;
    if (desc.Usage != D3D11_USAGE_DYNAMIC)
        return nullptr;
    if (desc.ByteWidth == 0 || desc.ByteWidth > kMaxTrackedSize)
        return nullptr;

    auto& entry = m_entries[pResource];
    entry.byteWidth = desc.ByteWidth;
    entry.shadow.resize(desc.ByteWidth, 0);
    entry.staging.resize(desc.ByteWidth, 0);
    entry.intercepted = false;
    entry.mappedThisFrame = false;
    return &entry;
}

bool CBDirtyTracker::InterceptMap(ID3D11DeviceContext* realCtx, ID3D11Resource* pResource,
                                   UINT Subresource, D3D11_MAP MapType, UINT MapFlags,
                                   D3D11_MAPPED_SUBRESOURCE* pMappedResource)
{
    // Only intercept WRITE_DISCARD on subresource 0
    if (MapType != D3D11_MAP_WRITE_DISCARD || Subresource != 0)
        return false;

    auto* entry = GetOrCreateEntry(pResource);
    if (!entry)
        return false;

    ++m_mapCalls;
    entry->mappedThisFrame = true;

    // Return our staging buffer instead of calling real Map.
    // The engine writes into staging; we compare on Unmap.
    pMappedResource->pData      = entry->staging.data();
    pMappedResource->RowPitch   = entry->byteWidth;
    pMappedResource->DepthPitch = 0;
    entry->intercepted = true;
    return true;
}

// Coherence membrane (Tier 1.2): CB content oracle. The shadow already lives on the
// CPU; scan committed bytes for NaN/Inf with no GPU readback. Emit only on corruption.
// char(34)/char(10) used for quote/newline so the source patcher cannot mangle escapes.
static uint64_t g_cbFrame = 0;

static void ScanCB(const uint8_t* data, uint32_t size, const void* ptr)
{
    if (!data || size < 4) return;
    const float*    fv = reinterpret_cast<const float*>(data);
    const uint32_t* bv = reinterpret_cast<const uint32_t*>(data);
    uint32_t n = size / 4;
    bool hasNan = false, hasInf = false; uint32_t nanIdx = 0;
    float mn = 1e30f, mx = -1e30f;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t b = bv[i];
        if (((b >> 23) & 0xFFu) == 0xFFu) {
            if (b & 0x7FFFFFu) { if (!hasNan) nanIdx = i; hasNan = true; }
            else hasInf = true;
        } else {
            float v = fv[i];
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
    }
    if (!hasNan && !hasInf) return;
    try {
        std::error_code ec;
        std::filesystem::create_directories("Data/SKSE/Plugins/RAW/live", ec);
        std::ofstream out("Data/SKSE/Plugins/RAW/live/cb_meta.jsonl", std::ios::app);
        if (!out.is_open()) return;
        char bindBuf[32] = {0};
        bool haveBind = BindingLedger::Get().LookupCB(const_cast<void*>(ptr), bindBuf, sizeof(bindBuf));
        const char Q = static_cast<char>(34);
        out << '{' << Q << "frame" << Q << ':' << g_cbFrame
            << ',' << Q << "ptr" << Q << ':' << Q << "0x" << std::hex
            << reinterpret_cast<uintptr_t>(ptr) << std::dec << Q
            << ',' << Q << "size" << Q << ':' << size
            << ',' << Q << "has_nan" << Q << ':' << (hasNan ? "true" : "false")
            << ',' << Q << "has_inf" << Q << ':' << (hasInf ? "true" : "false")
            << ',' << Q << "nan_at" << Q << ':' << nanIdx
            << ',' << Q << "min" << Q << ':' << mn
            << ',' << Q << "max" << Q << ':' << mx
            << ',' << Q << "bind" << Q << ':' << Q << (haveBind ? bindBuf : "?") << Q
            << '}' << static_cast<char>(10);
    } catch (...) {}
}

bool CBDirtyTracker::InterceptUnmap(ID3D11DeviceContext* realCtx, ID3D11Resource* pResource,
                                     UINT Subresource)
{
    if (Subresource != 0)
        return false;

    auto it = m_entries.find(pResource);
    if (it == m_entries.end() || !it->second.intercepted)
        return false;

    auto& entry = it->second;
    entry.intercepted = false;

    // Compare what the engine just wrote (staging) vs last committed data (shadow).
    if (std::memcmp(entry.staging.data(), entry.shadow.data(), entry.byteWidth) == 0) {
        // Data unchanged — skip the real Map/Unmap entirely.
        // The GPU buffer still holds the correct data from the last real commit.
        ++m_skippedUpdates;
        return true;
    }

    // Data changed — commit to GPU via real Map(WRITE_DISCARD) + memcpy + Unmap
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = realCtx->Map(pResource, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        std::memcpy(mapped.pData, entry.staging.data(), entry.byteWidth);
        realCtx->Unmap(pResource, 0);
        // Update shadow to match what we just committed
        std::memcpy(entry.shadow.data(), entry.staging.data(), entry.byteWidth);
        ScanCB(entry.staging.data(), entry.byteWidth, pResource);
    }
    ++m_committedUpdates;
    return true;
}

void CBDirtyTracker::OnFrameEnd()
{
    // Evict entries that weren't mapped this frame.
    // Handles the case where a buffer is destroyed and a new buffer is created
    // at the same pointer — the stale shadow would be incorrect.
    for (auto it = m_entries.begin(); it != m_entries.end(); ) {
        if (!it->second.mappedThisFrame) {
            it = m_entries.erase(it);
        } else {
            it->second.mappedThisFrame = false;
            ++it;
        }
    }

    // Reset per-frame counters
    m_mapCalls         = 0;
    m_skippedUpdates   = 0;
    m_committedUpdates = 0;
    ++g_cbFrame;
}

void CBDirtyTracker::OnClearState()
{
    // ClearState doesn't destroy buffers, but any in-flight intercepts are invalid
    for (auto& [_, entry] : m_entries)
        entry.intercepted = false;
}

const uint8_t* CBDirtyTracker::GetShadowData(ID3D11Resource* pResource) const
{
    auto it = m_entries.find(pResource);
    if (it == m_entries.end()) return nullptr;
    if (it->second.shadow.empty()) return nullptr;
    return it->second.shadow.data();
}

uint32_t CBDirtyTracker::GetShadowSize(ID3D11Resource* pResource) const
{
    auto it = m_entries.find(pResource);
    if (it == m_entries.end()) return 0;
    return it->second.byteWidth;
}

} // namespace SB::Proxy
