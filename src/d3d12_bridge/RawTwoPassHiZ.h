#pragma once
// RawTwoPassHiZ.h: Wave 83 Two-Pass GPU Hi-Z Occlusion Culling + Continuous Meshlet LOD
// Distance Budgeter coupled to a 1%-Low Frame-Pacing Governor.
// Exact C++23 mirror of RawGpuCull.hlsl Pass 1 / Pass 2 disocclusion retest and watertight
// monotonic meshlet LOD cut (lodErrorWorld <= parentErrorWorld).
// Clean-room MIT, zero external dependencies, MSVC /W4 /WX clean.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace raw::bridge::wave83
{

// Exact 48-byte GPU meshlet descriptor matching RawGpuCull.hlsl::GpuMeshletDesc byte-for-byte.
struct GpuMeshletDesc
{
    float    boundSphere[4]       = {0.0f, 0.0f, 0.0f, 1.0f}; // xyz center, w radius
    uint32_t packedConeAxisCutoff = 0u;                       // snorm8 xyz + cutoff
    float    lodErrorWorld        = 0.0f;                     // world-space geometric error
    float    parentErrorWorld     = 1.0e9f;                   // parent DAG node error (>= lodErrorWorld)
    uint32_t meshletOffset        = 0u;
    uint32_t triVertCounts        = 0u;                       // low16=verts, high16=tris
    uint32_t psoBucketId          = 0u;
    uint32_t pad0                 = 0u;
    uint32_t pad1                 = 0u;
};
static_assert(sizeof(GpuMeshletDesc) == 48u, "GpuMeshletDesc must be 48 bytes");

struct TwoPassCullInput
{
    uint32_t instanceId      = 0u;
    float    minObjZ         = 0.5f;   // nearest projected Standard-Z depth [0..1]
    float    prevFrameHiZMax = 0.2f;   // conservative max() depth in previous frame Hi-Z
    float    currFrameHiZMax = 0.9f;   // conservative max() depth in current frame rebuilt Hi-Z
    bool     inFrustum       = true;
    bool     withinDist      = true;
};

struct TwoPassCullOutput
{
    std::vector<uint32_t> pass1Visible;
    std::vector<uint32_t> pass2Disoccluded;
    uint32_t              pass1HiZCulled   = 0u;
    uint32_t              stillOccluded    = 0u;
    uint32_t              totalVisible     = 0u;
};

// Conservative Standard-Z (0=near, 1=far) 2x2 Hi-Z reduction MUST use max() (never min()!).
inline float ConservativeHiZReduce2x2(float z00, float z10, float z01, float z11, bool useBuggyMin = false) noexcept
{
    if (useBuggyMin) {
        return (std::min)((std::min)(z00, z10), (std::min)(z01, z11));
    }
    return (std::max)((std::max)(z00, z10), (std::max)(z01, z11));
}

// Exact Two-Pass Hi-Z occlusion simulation:
//   Pass 1 tests against previous-frame Hi-Z -> draws occluders -> rebuilds current-frame Hi-Z.
//   Pass 2 retests Pass-1 Hi-Z-culled candidates against current-frame Hi-Z -> 0 single-frame pop-in!
inline TwoPassCullOutput RunTwoPassHiZCull(
    const std::vector<TwoPassCullInput>& items,
    float depthBias,
    bool  enablePass2Retest = true)
{
    TwoPassCullOutput out{};
    std::vector<size_t> retestCandidates;
    retestCandidates.reserve(items.size());

    for (size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        if (!it.inFrustum || !it.withinDist) { continue; }
        const bool pass1Occluded = (it.minObjZ > (it.prevFrameHiZMax + depthBias));
        if (!pass1Occluded) {
            out.pass1Visible.push_back(it.instanceId);
        } else {
            ++out.pass1HiZCulled;
            retestCandidates.push_back(i);
        }
    }

    if (enablePass2Retest) {
        for (size_t idx : retestCandidates) {
            const auto& it = items[idx];
            const bool pass2Occluded = (it.minObjZ > (it.currFrameHiZMax + depthBias));
            if (!pass2Occluded) {
                out.pass2Disoccluded.push_back(it.instanceId);
            } else {
                ++out.stillOccluded;
            }
        }
    } else {
        out.stillOccluded = static_cast<uint32_t>(retestCandidates.size());
    }

    out.totalVisible = static_cast<uint32_t>(out.pass1Visible.size() + out.pass2Disoccluded.size());
    return out;
}

// Continuous screen-space projected pixel error meshlet LOD cut:
//   Selects node iff child errPx <= targetPixelError AND parentErrPx > targetPixelError.
//   Requires strict DAG monotonicity (lodErrorWorld <= parentErrorWorld) for crack-free watertight cuts!
inline bool ValidateMonotonicLodDag(const std::vector<GpuMeshletDesc>& nodes) noexcept
{
    for (const auto& n : nodes) {
        if (!std::isfinite(n.lodErrorWorld) || !std::isfinite(n.parentErrorWorld)) { return false; }
        if (n.lodErrorWorld < 0.0f || n.parentErrorWorld < n.lodErrorWorld) { return false; }
    }
    return true;
}

inline bool SelectMeshletLodNode(
    const GpuMeshletDesc& node,
    float viewZ,
    float projScalePx,
    float targetPixelError) noexcept
{
    const float zSafe = (std::max)(viewZ, 1.0f);
    const float childErrPx  = (node.lodErrorWorld * projScalePx) / zSafe;
    const float parentErrPx = (node.parentErrorWorld * projScalePx) / zSafe;
    return (childErrPx <= targetPixelError) && (parentErrPx > targetPixelError);
}

// 1%-Low (p99) Frame-Pacing Budget Governor smoothly adjusting targetPixelError in [0.85..3.50 px]
// with deadband hysteresis so LOD selection never oscillates frame-to-frame.
class FrameBudgetLodGovernor
{
public:
    explicit FrameBudgetLodGovernor(float targetFrameMs = 10.0f, float initialPxErr = 1.25f) noexcept
        : m_targetMs(targetFrameMs), m_targetPxErr((std::clamp)(initialPxErr, 0.85f, 3.50f)) {}

    float UpdateFromP99(float p99FrameMs) noexcept
    {
        if (!std::isfinite(p99FrameMs) || p99FrameMs <= 0.0f) { return m_targetPxErr; }
        const float ratio = p99FrameMs / (std::max)(m_targetMs, 1.0f);
        if (ratio > 1.06f) {
            // Over 1%-low budget: smoothly relax pixel error tolerance to shed distant meshlet load
            m_targetPxErr = (std::min)(m_targetPxErr + 0.12f * (std::min)(ratio - 1.0f, 1.0f), 3.50f);
        } else if (ratio < 0.88f) {
            // Headroom available: smoothly tighten pixel error back toward razor-sharp LOD0
            m_targetPxErr = (std::max)(m_targetPxErr - 0.06f, 0.85f);
        }
        return m_targetPxErr;
    }

    [[nodiscard]] float TargetPixelError() const noexcept { return m_targetPxErr; }

private:
    float m_targetMs    = 10.0f;
    float m_targetPxErr = 1.25f;
};

} // namespace raw::bridge::wave83
