// Resource state tracking.
//
// D3D11 has no resource states; D3D12 needs a transition barrier whenever a
// resource changes use. Each command list tracks the states it needs on its
// own (Tracker), so lists can be recorded on any thread without knowing what
// ran before them. At submission the device compares the first state a list
// needs for each subresource with the global state and puts the missing
// transitions into a small list that runs just before it.
#pragma once
#include "tx_common.h"

namespace raw::tx {

constexpr uint32_t kUnknownState = 0xFFFFFFFFu;

struct TrackedRes {
    ComPtr<ID3D12Resource> res;
    uint32_t subs = 1;
    std::vector<uint32_t> global;   // one D3D12_RESOURCE_STATES per subresource
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
    void Keep(std::vector<std::shared_ptr<TrackedRes>>& out) const;

private:
    struct Local {
        std::shared_ptr<TrackedRes> r;
        std::unique_ptr<uint32_t[]> s;   // first[subs], then cur[subs]
    };
    Local& Find(const std::shared_ptr<TrackedRes>& r);
    std::unordered_map<TrackedRes*, Local> map_;
    TrackedRes* lastKey_ = nullptr;
    Local* last_ = nullptr;
    std::vector<D3D12_RESOURCE_BARRIER> pend_;
};

}  // namespace raw::tx
