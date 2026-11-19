// Resource state tracking.
//
// D3D11 has no resource states; D3D12 needs a transition barrier whenever a
// resource changes use. Each command list tracks the states it needs on its
// own (Tracker), so lists can be recorded on any thread without knowing what
// ran before them. At submission the device compares the first state a list
// needs for each subresource with the global state and puts the missing
// transitions into a small list that runs just before it.
//
// Lookups are flat: a list's resources sit in one array, found by a hash map,
// or, for the one tracker that owns the slot cache (the immediate context's),
// by an index stored on the resource itself with no hashing at all. Each entry
// also keeps the state its subresources share when they agree, so a request
// that changes nothing returns without visiting the subresources.
#pragma once
#include "tx_common.h"

namespace raw::tx {

constexpr uint32_t kUnknownState = 0xFFFFFFFFu;
constexpr uint32_t kMixedState = 0xFFFFFFFEu;   // the subresources disagree

struct TrackedRes {
    ComPtr<ID3D12Resource> res;
    uint32_t subs = 1;
    std::vector<uint32_t> global;   // one D3D12_RESOURCE_STATES per subresource
    // The slot cache: written and read only by the tracker that owns it (EnableSlots), on its own thread.
    uint32_t slotEpoch = 0;
    uint32_t slot = 0;
};

// The subresources a view covers.
struct SubRange {
    uint16_t mip0 = 0, mipN = 1, slice0 = 0, sliceN = 1;
    uint8_t planeMask = 1;
    bool all = true;
};

class Tracker {
public:
    uint32_t faults = 0;
    uint64_t barrierCount = 0;

    // The tracker that may use the slot cache on TrackedRes: one in the process (the immediate context's).
    void EnableSlots();
    void Reset();
    // Queue the transition `sub` of `r` needs for `state`. True when the
    // list's known state changed. Read states accumulate.
    bool Require(const std::shared_ptr<TrackedRes>& r, uint32_t sub, uint32_t state);
    bool RequireAll(const std::shared_ptr<TrackedRes>& r, uint32_t state);
    bool RequireRange(const std::shared_ptr<TrackedRes>& r, const SubRange& range,
                      uint32_t mips, uint32_t slices, uint32_t state);
    void UavBarrier(ID3D12Resource* res);
    void Flush(ID3D12GraphicsCommandList* cl);

    // Submission: transitions from the global state to this list's first
    // needs; then the global state becomes this list's final state.
    void Resolve(std::vector<D3D12_RESOURCE_BARRIER>& fixups);
    // The resources this list used, kept alive until it has run. `move`: the tracker is reset next, so its
    // references move out instead of being copied.
    void Keep(std::vector<std::shared_ptr<TrackedRes>>& out, bool move = false);

private:
    // The immediate context's tracker (the slot cache) holds a plain pointer: every submission comes from
    // the immediate context's thread and carries its open list, and a destroyed resource is retired until the
    // next submission completes (DestroyCore), so the resource outlives the list's Resolve. Copying the
    // shared_ptr instead cost two locked instructions per resource per list. Other trackers own a reference.
    struct Local {
        TrackedRes* r = nullptr;
        std::shared_ptr<TrackedRes> own;
        uint32_t uni = kUnknownState;    // the state every subresource is in now, or kMixedState
        uint32_t inl[8];                 // first[subs], then cur[subs], for up to 4 subresources
        std::unique_ptr<uint32_t[]> heap;
        uint32_t* S() { return heap ? heap.get() : inl; }
    };
    Local& Find(const std::shared_ptr<TrackedRes>& r);
    Local& Add(const std::shared_ptr<TrackedRes>& r);
    bool Step(Local& l, uint32_t sub, uint32_t state);   // one subresource; true when it changed
    static bool NoChange(const Local& l, uint32_t state);
    static uint32_t Uniform(Local& l);
    void Settle(Local& l) { l.uni = l.r->subs == 1 ? l.S()[1] : Uniform(l); }

    std::vector<Local> locals_;
    std::unordered_map<TrackedRes*, uint32_t> map_;   // trackers without the slot cache
    uint32_t epoch_ = 0;                              // 0: no slot cache
    std::vector<D3D12_RESOURCE_BARRIER> pend_;
};

}  // namespace raw::tx
