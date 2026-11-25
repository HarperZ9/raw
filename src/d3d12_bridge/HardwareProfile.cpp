// HardwareProfile.cpp: see HardwareProfile.h. Original work, MIT.
#include "HardwareProfile.h"

#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <thread>

namespace raw::bridge
{
using Microsoft::WRL::ComPtr;

namespace
{
std::string ToLower(const std::wstring& w)
{
    std::string s;
    s.reserve(w.size());
    for (wchar_t ch : w) s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch & 0xFF))));
    return s;
}

bool HasAny(const std::string& hay, const std::initializer_list<const char*>& needles)
{
    for (const char* n : needles) {
        if (hay.find(n) != std::string::npos) return true;
    }
    return false;
}

int ClassifyTier(const HardwareProfile& p)
{
    const std::string name = p.gpuName;
    std::string lo = name;
    for (char& c : lo) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (p.vramMB < 5500) return 0;

    // NVIDIA RTX 2000-5000 architectural tiers (never conflate RTX 3060 12GB with RTX 4070 Ti / 4090):
    if (p.vendorId == 0x10DE) {
        if (HasAny(lo, {"5090", "5080", "pro 6000", "blackwell"})) return 4;
        if (HasAny(lo, {"4090", "4080", "3090", "5070 ti", "a6000", "ada 6000"})) return 3;
        if (HasAny(lo, {"5070", "4070", "3080", "3070", "4060 ti", "5060 ti"})) return 2;
        if (HasAny(lo, {"4060", "5060", "3060", "3050", "2080", "2070", "2060"})) return 1;
        if (HasAny(lo, {"gtx", "1660", "1650", "1080", "1070", "1060"})) return 0;
    }
    // AMD RDNA2-RDNA4:
    if (p.vendorId == 0x1002) {
        if (HasAny(lo, {"9070 xt", "7900 xtx", "7900 xt"})) return 3;
        if (HasAny(lo, {"9070", "7800", "7700", "6950", "6900", "6800"})) return 2;
        if (HasAny(lo, {"7600", "6750", "6700", "6650", "6600"})) return 1;
    }
    // Intel Arc:
    if (p.vendorId == 0x8086) {
        if (HasAny(lo, {"b580", "b770", "a770"})) return 2;
        if (HasAny(lo, {"a750", "a580", "b570"})) return 1;
    }
    // Fallback by verified feature tier + VRAM:
    if (p.dxrTier >= 12 && p.ser && p.vramMB >= 15000) return 4;
    if (p.vramMB >= 15000) return 3;
    if (p.vramMB >= 10000) return 2;
    if (p.vramMB >= 7000) return 1;
    return 0;
}
} // namespace

uint32_t RecommendTxWorkers()
{
    const uint32_t cores = std::max(1u, std::thread::hardware_concurrency());
    if (cores < 8u) return 0u;
    return std::clamp(cores / 4u, 2u, 4u);
}

HardwareProfile DetectHardwareProfile(ID3D12Device* dev)
{
    HardwareProfile p{};
    p.logicalCores = std::max(1u, std::thread::hardware_concurrency());
    p.recommendedTxWorkers = RecommendTxWorkers();

    ComPtr<IDXGIFactory1> fac;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&fac))) && fac) {
        LUID targetLuid{};
        const bool matchLuid = (dev != nullptr);
        if (matchLuid) targetLuid = dev->GetAdapterLuid();
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter1> ad;
            if (fac->EnumAdapters1(i, &ad) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED(ad->GetDesc1(&desc))) continue;
            if (matchLuid) {
                if (desc.AdapterLuid.LowPart != targetLuid.LowPart || desc.AdapterLuid.HighPart != targetLuid.HighPart) continue;
            } else if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                continue;
            }
            p.vendorId = desc.VendorId;
            p.vramMB = static_cast<uint32_t>(desc.DedicatedVideoMemory / (1024ULL * 1024ULL));
            p.gpuName.reserve(64);
            for (wchar_t ch : desc.Description) {
                if (!ch) break;
                p.gpuName.push_back(static_cast<char>(ch & 0x7F));
            }
            break;
        }
    }

    if (dev) {
        D3D12_FEATURE_DATA_SHADER_MODEL sm{};
        for (int v = 0x6A; v >= 0x60; --v) {
            sm.HighestShaderModel = static_cast<D3D_SHADER_MODEL>(v);
            if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof sm))) {
                p.shaderModel = static_cast<uint32_t>(sm.HighestShaderModel);
                break;
            }
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS4 o4{};
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 o7{};
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 o12{};
        D3D12_FEATURE_DATA_D3D12_OPTIONS21 o21{};
        if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &o4, sizeof o4)))
            p.fp16 = o4.Native16BitShaderOpsSupported != FALSE;
        if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof o5)))
            p.dxrTier = static_cast<uint32_t>(o5.RaytracingTier);
        if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &o7, sizeof o7)))
            p.meshTier = static_cast<uint32_t>(o7.MeshShaderTier);
        if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &o12, sizeof o12)))
            p.enhancedBarriers = o12.EnhancedBarriersSupported != FALSE;
        if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS21, &o21, sizeof o21)))
            p.workGraphsTier = static_cast<uint32_t>(o21.WorkGraphsTier);
        p.omm = (p.dxrTier >= 12);
#ifdef D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_BYTE_ALIGNMENT
        D3D12_FEATURE_DATA_D3D12_OPTIONS22 o22{};
        if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS22, &o22, sizeof o22)))
            p.ser = o22.ShaderExecutionReorderingActuallyReorders != FALSE;
#endif
    }

    p.tier = ClassifyTier(p);
    if (dev && p.dxrTier < 11) p.tier = 0;
    p.recommendedQuality = std::clamp(p.tier + (p.tier >= 1 && p.tier <= 3 ? 1 : 0), 0, 4);
    return p;
}

FrameBudgetAllocation SolveFrameBudgetLP(const HardwareProfile& prof, float targetFrameMs, bool isInterior)
{
    // Exact 2-segment piecewise-linear concave LP (Core tier + High-Detail tier per GPU pass):
    // Maximizes sum(w_core_i * x_core_i + w_hi_i * x_hi_i) s.t. lo_i <= x_i <= hi_i and sum(x_i) <= fxBudgetMs.
    // Prevents any single pass from starving clouds/froxels/subdivision while strictly respecting the frame budget.
    const float fxBudget = std::max(targetFrameMs * 0.52f, 1.5f);
    const float tierScale = (prof.tier >= 4) ? 1.35f : (prof.tier == 3 ? 1.15f : (prof.tier == 2 ? 0.92f : 0.68f));

    struct StageSpec { float lo, mid, hi, uCore, uHi, alloc; };
    // Order: 0=DXR Shadow, 1=GI Bounce, 2=Froxel Vol, 3=Cloud March, 4=Mesh Subdiv
    StageSpec st[5] = {
        { 0.35f, 1.15f * tierScale, 2.20f * tierScale, isInterior ? 2.40f : 2.60f, 1.25f, 0.0f },
        { 0.30f, 1.25f * tierScale, 2.60f * tierScale, isInterior ? 2.75f : 2.35f, 1.30f, 0.0f },
        { 0.25f, 0.85f * tierScale, 1.85f * tierScale, isInterior ? 2.05f : 2.15f, 1.05f, 0.0f },
        { isInterior ? 0.00f : 0.25f, isInterior ? 0.00f : 0.85f * tierScale, isInterior ? 0.00f : 1.90f * tierScale,
          isInterior ? 0.00f : 2.00f, isInterior ? 0.00f : 0.98f, 0.0f },
        { 0.20f, 0.75f * tierScale, 1.75f * tierScale, 1.90f, 0.95f, 0.0f }
    };

    float sumLo = 0.0f;
    for (int i = 0; i < 5; ++i) sumLo += st[i].lo;
    if (sumLo > fxBudget) {
        const float shrink = fxBudget / std::max(sumLo, 1.0e-4f);
        for (int i = 0; i < 5; ++i) st[i].alloc = st[i].lo * shrink;
    } else {
        float rem = fxBudget - sumLo;
        for (int i = 0; i < 5; ++i) st[i].alloc = st[i].lo;
        struct Seg { int stage; float cap; float util; };
        Seg segs[10];
        int nSeg = 0;
        for (int i = 0; i < 5; ++i) {
            const float c1 = std::max(0.0f, st[i].mid - st[i].lo);
            const float c2 = std::max(0.0f, st[i].hi - std::max(st[i].mid, st[i].lo));
            if (c1 > 1.0e-5f && st[i].uCore > 0.0f) segs[nSeg++] = {i, c1, st[i].uCore};
            if (c2 > 1.0e-5f && st[i].uHi > 0.0f)   segs[nSeg++] = {i, c2, st[i].uHi};
        }
        std::sort(segs, segs + nSeg, [](const Seg& a, const Seg& b) { return a.util > b.util; });
        for (int k = 0; k < nSeg && rem > 1.0e-5f; ++k) {
            const float add = std::min(rem, segs[k].cap);
            st[segs[k].stage].alloc += add;
            rem -= add;
        }
    }

    FrameBudgetAllocation out{};
    out.dxrShadowMs  = st[0].alloc;
    out.giBounceMs   = st[1].alloc;
    out.froxelVolMs  = st[2].alloc;
    out.cloudMarchMs = st[3].alloc;
    out.meshSubdivMs = st[4].alloc;
    out.totalMs = out.dxrShadowMs + out.giBounceMs + out.froxelVolMs + out.cloudMarchMs + out.meshSubdivMs;
    out.cloudSteps = isInterior ? 0u : static_cast<uint32_t>(std::clamp(int(16.0f + out.cloudMarchMs * 24.0f), 16, 64));
    out.subdivTier = (out.meshSubdivMs >= 1.15f) ? 3u : ((out.meshSubdivMs >= 0.55f) ? 2u : 1u);
    return out;
}

} // namespace raw::bridge
