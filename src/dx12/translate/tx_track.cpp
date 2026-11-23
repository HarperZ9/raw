#include "tx_track.h"

namespace raw::tx {

static constexpr uint32_t kReadStates =
    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT | D3D12_RESOURCE_STATE_COPY_SOURCE |
    D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_RESOLVE_SOURCE;

static std::atomic<uint32_t> g_epoch{0};

static uint32_t NextEpoch()
{
    uint32_t e = ++g_epoch;
    return e ? e : ++g_epoch;   // 0 means "no slot cache"
}

static D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* res, uint32_t sub, uint32_t from, uint32_t to)
{
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = sub;
    b.Transition.StateBefore = static_cast<D3D12_RESOURCE_STATES>(from);
    b.Transition.StateAfter = static_cast<D3D12_RESOURCE_STATES>(to);
    return b;
}

static bool IsRead(uint32_t s) { return s != 0 && (s & ~kReadStates) == 0; }

void Tracker::EnableSlots()
{
    Reset();
    epoch_ = NextEpoch();
}

void Tracker::Reset()
{
    locals_.clear();
    map_.clear();
    pend_.clear();
    if (epoch_) epoch_ = NextEpoch();
}

Tracker::Local& Tracker::Add(const std::shared_ptr<TrackedRes>& r)
{
    Local& l = locals_.emplace_back();
    l.r = r.get();
    if (!epoch_) l.own = r;
    const uint32_t n = r->subs * 2;
    if (n > 8) l.heap.reset(new uint32_t[n]);
    uint32_t* s = l.S();
    for (uint32_t i = 0; i < n; ++i) s[i] = kUnknownState;
    return l;
}

Tracker::Local& Tracker::Find(const std::shared_ptr<TrackedRes>& r)
{
    TrackedRes* k = r.get();
    if (epoch_) {
        if (k->slotEpoch == epoch_) return locals_[k->slot];
        k->slotEpoch = epoch_;
        k->slot = uint32_t(locals_.size());
        return Add(r);
    }
    auto ins = map_.try_emplace(k, uint32_t(locals_.size()));
    if (!ins.second) return locals_[ins.first->second];
    return Add(r);
}

// True when asking for `state` cannot change a resource whose subresources all share `l.uni`.
bool Tracker::NoChange(const Local& l, uint32_t state)
{
    const uint32_t u = l.uni;
    if (u == kMixedState || u == kUnknownState) return false;
    if (u == state) return true;
    return IsRead(state) && IsRead(u) && (u | state) == u;
}

uint32_t Tracker::Uniform(Local& l)
{
    const uint32_t n = l.r->subs;
    const uint32_t* cur = l.S() + n;
    const uint32_t c0 = cur[0];
    for (uint32_t i = 1; i < n; ++i)
        if (cur[i] != c0) return kMixedState;
    return c0;
}

bool Tracker::Step(Local& l, uint32_t sub, uint32_t state)
{
    uint32_t* first = l.S();
    uint32_t* cur = first + l.r->subs;
    const uint32_t c = cur[sub];
    if (c == kUnknownState) { first[sub] = cur[sub] = state; return true; }
    if (c == state) return false;
    uint32_t ns = state;
    if (IsRead(state) && IsRead(c)) {
        ns = c | state;
        if (ns == c) return false;
    }
    bool drop = (faults & FaultSkipBarrier) && c == D3D12_RESOURCE_STATE_RENDER_TARGET &&
                (state & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (!drop) {
        pend_.push_back(Transition(l.r->res.Get(), sub, c, ns));
        ++barrierCount;
    }
    cur[sub] = ns;
    return true;
}

bool Tracker::Require(const std::shared_ptr<TrackedRes>& r, uint32_t sub, uint32_t state)
{
    if (!r || !r->res) return false;
    Local& l = Find(r);
    if (NoChange(l, state) || !Step(l, sub, state)) return false;
    Settle(l);
    return true;
}

bool Tracker::RequireAll(const std::shared_ptr<TrackedRes>& r, uint32_t state)
{
    if (!r || !r->res) return false;
    Local& l = Find(r);
    if (NoChange(l, state)) return false;
    // Fast-path whole-resource ALL_SUBRESOURCES barrier coalescing: replaces 11-48 per-subresource barriers
    // on mipmapped textures, cubemaps, and shadow atlases with 1 single D3D12 transition barrier.
    if (r->subs > 1 && l.uni != kMixedState && l.uni != kUnknownState) {
        const uint32_t c = l.uni;
        uint32_t ns = state;
        if (IsRead(state) && IsRead(c)) {
            ns = c | state;
            if (ns == c) return false;
        }
        bool drop = (faults & FaultSkipBarrier) && c == D3D12_RESOURCE_STATE_RENDER_TARGET &&
                    (state & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (!drop) {
            pend_.push_back(Transition(l.r->res.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, c, ns));
            ++barrierCount;
        }
        uint32_t* cur = l.S() + r->subs;
        for (uint32_t i = 0; i < r->subs; ++i) cur[i] = ns;
        l.uni = ns;
        return true;
    }
    bool changed = false;
    for (uint32_t i = 0; i < r->subs; ++i) changed |= Step(l, i, state);
    if (changed) Settle(l);
    return changed;
}

bool Tracker::RequireRange(const std::shared_ptr<TrackedRes>& r, const SubRange& range,
                           uint32_t mips, uint32_t slices, uint32_t state)
{
    if (range.all) return RequireAll(r, state);
    Local& l = Find(r);
    if (NoChange(l, state)) return false;
    bool changed = false;
    for (uint32_t p = 0; p < 2; ++p) {
        if (!(range.planeMask & (1u << p))) continue;
        for (uint32_t s = range.slice0; s < uint32_t(range.slice0) + range.sliceN; ++s)
            for (uint32_t m = range.mip0; m < uint32_t(range.mip0) + range.mipN; ++m) {
                uint32_t sub = m + s * mips + p * mips * slices;
                if (sub < r->subs) changed |= Step(l, sub, state);
            }
    }
    if (changed) Settle(l);
    return changed;
}

void Tracker::UavBarrier(ID3D12Resource* res)
{
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = res;
    pend_.push_back(b);
}

void Tracker::Flush(ID3D12GraphicsCommandList* cl)
{
    if (pend_.empty()) return;
    cl->ResourceBarrier(static_cast<UINT>(pend_.size()), pend_.data());
    pend_.clear();
}

void Tracker::Resolve(std::vector<D3D12_RESOURCE_BARRIER>& fixups)
{
    for (Local& l : locals_) {
        TrackedRes* r = l.r;
        uint32_t* first = l.S();
        uint32_t* cur = first + r->subs;
        for (uint32_t i = 0; i < r->subs; ++i) {
            if (first[i] == kUnknownState) continue;
            const bool drop = (faults & FaultSkipBarrier) && r->global[i] == D3D12_RESOURCE_STATE_RENDER_TARGET &&
                              (first[i] & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);   // the control's defect here too
            if (r->global[i] != first[i] && !drop)
                fixups.push_back(Transition(r->res.Get(), i, r->global[i], first[i]));
            r->global[i] = cur[i];
        }
    }
}

void Tracker::Keep(std::vector<std::shared_ptr<TrackedRes>>& out, bool move)
{
    if (epoch_) return;   // plain pointers: DestroyCore's retire keeps a destroyed resource (see Local)
    out.reserve(out.size() + locals_.size());
    if (move) for (Local& l : locals_) out.push_back(std::move(l.own));
    else for (const Local& l : locals_) out.push_back(l.own);
}

}  // namespace raw::tx
