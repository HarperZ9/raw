#include "CBDirtyTracker.h"
#include "BindingLedger.h"   // CB-ptr -> slot/stage lookup for cb_meta attribution (1.2-p3)
#include "ProxyAPI.h"        // PG_DiagCBScan, PG_CBDirtyTracker, PG_CBShadowAll
#include <cstring>
#include <fstream>
#include <filesystem>
#include <system_error>
#include <cstdint>
#include <utility>

namespace SB::Proxy
{

CBDirtyTracker& CBDirtyTracker::Get()
{
    static CBDirtyTracker instance;
    return instance;
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


namespace {
inline size_t Slot(const void* p, size_t mask)
{
    return size_t((reinterpret_cast<uintptr_t>(p) >> 4) * 0x9E3779B97F4A7C15ull >> 20) & mask;
}
}  // namespace

CBDirtyTracker::CBEntry* CBDirtyTracker::Find(ID3D11Resource* p)
{
    if (m_table.empty()) return nullptr;
    const size_t mask = m_table.size() - 1;
    for (size_t i = Slot(p, mask);; i = (i + 1) & mask) {
        CBEntry& e = m_table[i];
        if (e.key == p) return &e;
        if (!e.key) return nullptr;
    }
}

void CBDirtyTracker::Grow()
{
    // Rehash at twice the size, dropping entries not seen for 600 frames (resources since freed).
    std::vector<CBEntry> old;
    old.swap(m_table);
    size_t n = old.empty() ? 1024 : old.size();
    size_t live = 0;
    for (auto& e : old) live += e.key && e.seenFrame + 600 >= m_frame;
    while (live * 2 >= n) n *= 2;
    m_table.resize(n);
    m_used = 0;
    const size_t mask = n - 1;
    for (auto& e : old) {
        if (!e.key || e.seenFrame + 600 < m_frame) continue;
        size_t i = Slot(e.key, mask);
        while (m_table[i].key) i = (i + 1) & mask;
        m_table[i] = std::move(e);
        ++m_used;
    }
}

CBDirtyTracker::CBEntry* CBDirtyTracker::Classify(ID3D11Resource* p)
{
    CBEntry* e = Find(p);
    if (e && e->seenFrame + 1 >= m_frame) {
        e->seenFrame = m_frame;
        return e;
    }
    if (!e) {
        if ((m_used + 1) * 2 > m_table.size()) Grow();
        const size_t mask = m_table.size() - 1;
        size_t i = Slot(p, mask);
        while (m_table[i].key) i = (i + 1) & mask;
        e = &m_table[i];
        e->key = p;
        ++m_used;
    }
    // New, or not seen for a whole frame: the object at this address may be a different one now.
    e->seenFrame = m_frame;
    e->valid = false;
    e->intercepted = false;
    uint32_t width = 0;
    D3D11_RESOURCE_DIMENSION dim;
    p->GetType(&dim);
    if (dim == D3D11_RESOURCE_DIMENSION_BUFFER) {
        D3D11_BUFFER_DESC desc;
        static_cast<ID3D11Buffer*>(p)->GetDesc(&desc);
        if ((desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) && desc.Usage == D3D11_USAGE_DYNAMIC &&
            desc.ByteWidth > 0 && desc.ByteWidth <= kMaxTrackedSize)
            width = desc.ByteWidth;
    }
    if (width != e->byteWidth || (width && !e->shadow)) {
        e->byteWidth = width;
        e->shadow.reset(width ? new uint8_t[width]() : nullptr);
        e->staging.reset(width ? new uint8_t[width]() : nullptr);
    }
    return e;
}

bool CBDirtyTracker::Wanted(const CBEntry& e) const
{
    return PG_CBDirtyTracker() || PG_CBShadowAll() || (e.wantFrame && e.wantFrame + kWantFrames >= m_frame);
}

bool CBDirtyTracker::InterceptMap(ID3D11DeviceContext*, ID3D11Resource* pResource,
                                  UINT Subresource, D3D11_MAP MapType, UINT,
                                  D3D11_MAPPED_SUBRESOURCE* pMappedResource)
{
    if (MapType != D3D11_MAP_WRITE_DISCARD || Subresource != 0)
        return false;
    CBEntry* e = Classify(pResource);
    if (!e->byteWidth) return false;
    if (!Wanted(*e)) {
        e->valid = false;   // the GPU copy changes without us: the shadow no longer matches it
        return false;
    }
    ++m_mapCalls;
    // The game writes into cached memory; Unmap copies it to the GPU.
    pMappedResource->pData      = e->staging.get();
    pMappedResource->RowPitch   = e->byteWidth;
    pMappedResource->DepthPitch = 0;
    e->intercepted = true;
    return true;
}

bool CBDirtyTracker::InterceptUnmap(ID3D11DeviceContext* realCtx, ID3D11Resource* pResource, UINT Subresource)
{
    if (Subresource != 0) return false;
    CBEntry* e = Find(pResource);
    if (!e || !e->intercepted) return false;
    e->intercepted = false;

    // Dedupe mode: unchanged bytes skip the upload; the GPU already holds them.
    if (PG_CBDirtyTracker() && e->valid && std::memcmp(e->staging.get(), e->shadow.get(), e->byteWidth) == 0) {
        ++m_skippedUpdates;
        e->mapFrame = m_frame;
        return true;
    }
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(realCtx->Map(pResource, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        e->valid = false;
        return true;
    }
    std::memcpy(mapped.pData, e->staging.get(), e->byteWidth);
    realCtx->Unmap(pResource, 0);
    // The staging block becomes the shadow; the old shadow takes the next write.
    std::swap(e->staging, e->shadow);
    e->valid = true;
    e->mapFrame = m_frame;
    if (PG_DiagCBScan()) ScanCB(e->shadow.get(), e->byteWidth, pResource);
    ++m_committedUpdates;
    return true;
}

void CBDirtyTracker::OnFrameEnd()
{
    uint32_t shadowed = 0;
    for (const auto& e : m_table) shadowed += e.key && e.valid && e.mapFrame == m_frame;
    m_shadowed = shadowed;
    ++m_frame;
    m_mapCalls         = 0;
    m_skippedUpdates   = 0;
    m_committedUpdates = 0;
    ++g_cbFrame;
}

void CBDirtyTracker::OnClearState()
{
    // ClearState doesn't destroy buffers, but any in-flight intercepts are invalid
    for (auto& e : m_table) e.intercepted = false;
}

const uint8_t* CBDirtyTracker::GetShadowData(ID3D11Resource* pResource)
{
    if (!pResource) return nullptr;
    CBEntry* e = Classify(pResource);
    if (!e->byteWidth) return nullptr;
    e->wantFrame = m_frame;
    return e->valid && e->mapFrame + 1 >= m_frame ? e->shadow.get() : nullptr;
}

uint32_t CBDirtyTracker::GetShadowSize(ID3D11Resource* pResource)
{
    if (!pResource) return 0;
    CBEntry* e = Classify(pResource);
    if (e->byteWidth) e->wantFrame = m_frame;
    return e->byteWidth;
}

} // namespace SB::Proxy
