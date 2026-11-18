#include "tx_track.h"

namespace raw::tx {

static constexpr uint32_t kReadStates =
    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT | D3D12_RESOURCE_STATE_COPY_SOURCE |
    D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_RESOLVE_SOURCE;

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

void Tracker::Reset()
{
    map_.clear();
    pend_.clear();
    lastKey_ = nullptr;
    last_ = nullptr;
}

Tracker::Local& Tracker::Find(const std::shared_ptr<TrackedRes>& r)
{
    if (lastKey_ == r.get()) return *last_;
    auto it = map_.find(r.get());
    if (it == map_.end()) {
        Local l;
        l.r = r;
        l.s.reset(new uint32_t[size_t(r->subs) * 2]);
        for (uint32_t i = 0; i < r->subs * 2; ++i) l.s[i] = kUnknownState;
        it = map_.emplace(r.get(), std::move(l)).first;
    }
    lastKey_ = r.get();
    last_ = &it->second;
    return *last_;
}

bool Tracker::Require(const std::shared_ptr<TrackedRes>& r, uint32_t sub, uint32_t state)
{
    Local& l = Find(r);
    uint32_t* first = l.s.get();
    uint32_t* cur = first + r->subs;
    uint32_t c = cur[sub];
    if (c == kUnknownState) { first[sub] = cur[sub] = state; return true; }
    if (c == state) return false;
    uint32_t ns = state;
    bool bothRead = state != 0 && c != 0 && (state & ~kReadStates) == 0 && (c & ~kReadStates) == 0;
    if (bothRead) {
        ns = c | state;
        if (ns == c) return false;
    }
    bool drop = (faults & FaultSkipBarrier) && c == D3D12_RESOURCE_STATE_RENDER_TARGET &&
                (state & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (!drop) {
        pend_.push_back(Transition(r->res.Get(), sub, c, ns));
        ++barrierCount;
    }
    cur[sub] = ns;
    return true;
}

bool Tracker::RequireAll(const std::shared_ptr<TrackedRes>& r, uint32_t state)
{
    bool changed = false;
    for (uint32_t i = 0; i < r->subs; ++i) changed |= Require(r, i, state);
    return changed;
}

bool Tracker::RequireRange(const std::shared_ptr<TrackedRes>& r, const SubRange& range,
                           uint32_t mips, uint32_t slices, uint32_t state)
{
    if (range.all) return RequireAll(r, state);
    bool changed = false;
    for (uint32_t p = 0; p < 2; ++p) {
        if (!(range.planeMask & (1u << p))) continue;
        for (uint32_t s = range.slice0; s < uint32_t(range.slice0) + range.sliceN; ++s)
            for (uint32_t m = range.mip0; m < uint32_t(range.mip0) + range.mipN; ++m) {
                uint32_t sub = m + s * mips + p * mips * slices;
                if (sub < r->subs) changed |= Require(r, sub, state);
            }
    }
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
    for (auto& kv : map_) {
        TrackedRes* r = kv.first;
        uint32_t* first = kv.second.s.get();
        uint32_t* cur = first + r->subs;
        for (uint32_t i = 0; i < r->subs; ++i) {
            if (first[i] == kUnknownState) continue;
            if (r->global[i] != first[i])
                fixups.push_back(Transition(r->res.Get(), i, r->global[i], first[i]));
            r->global[i] = cur[i];
        }
    }
}

void Tracker::Keep(std::vector<std::shared_ptr<TrackedRes>>& out) const
{
    for (auto& kv : map_) out.push_back(kv.second.r);
}

}  // namespace raw::tx
