// tx_subdiv.cpp: see tx_subdiv.h. Original work, MIT.
#include "tx_subdiv.h"
#include "tx_device.h"
#include "TxSubdiv_CS.h" // g_TxSubdivCS (fxc, cs_5_0)

#include <cstring>

namespace raw::tx {

namespace {
uint32_t FmtBytes(DXGI_FORMAT f)
{
    switch (f) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_UINT: case DXGI_FORMAT_R32G32B32A32_SINT: return 16;
    case DXGI_FORMAT_R32G32B32_FLOAT:    case DXGI_FORMAT_R32G32B32_UINT:    case DXGI_FORMAT_R32G32B32_SINT:    return 12;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:  case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_FLOAT:       case DXGI_FORMAT_R32G32_UINT:       case DXGI_FORMAT_R32G32_SINT:       return 8;
    case DXGI_FORMAT_R16G16_FLOAT:       case DXGI_FORMAT_R16G16_UNORM:      case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM:     case DXGI_FORMAT_R8G8B8A8_SNORM:    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_B8G8R8A8_UNORM:     case DXGI_FORMAT_R32_FLOAT:         case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R10G10B10A2_UNORM:  case DXGI_FORMAT_R11G11B10_FLOAT:                                        return 4;
    default: return 0;
    }
}

uint32_t PackOffFmt(uint32_t off, uint32_t fmtCode)
{
    return (off & 0xFFFFu) | ((fmtCode & 0xFFFFu) << 16u);
}

struct SubdivCB {
    uint32_t triCount;
    uint32_t strideBytes;
    uint32_t vbByteOffset;
    uint32_t ibByteOffset;
    int32_t  baseVertex;
    uint32_t idxIs32Bit;
    uint32_t posOffFmt;
    uint32_t normOffFmt;
    uint32_t tangOffFmt;
    uint32_t uvOffFmt;
    uint32_t colOffFmt;
    float    curvatureGain;
    uint32_t maxVertIdx;
    float    maxOffsetFrac;
    float    snowDepthU;
    float    displaceAmp;
};
static_assert(sizeof(SubdivCB) == 64, "SubdivCB must be 16 DWORDs");

bool CreateDefaultBuf(ID3D12Device* dev, uint64_t bytes, ComPtr<ID3D12Resource>& out)
{
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (bytes + 15ull) & ~15ull;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                  D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&out)));
}
}  // namespace

bool SubdivCache::Init(ID3D12Device* dev)
{
    if (!dev) return false;
    D3D12_ROOT_PARAMETER rp[5]{};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rp[0].Constants = {0, 0, 16};
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; rp[1].Descriptor = {0, 0};
    rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; rp[2].Descriptor = {1, 0};
    rp[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; rp[3].Descriptor = {0, 0};
    rp[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; rp[4].Descriptor = {1, 0};
    D3D12_ROOT_SIGNATURE_DESC rsd{5, rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> sig, err;
    if (FAILED(TxD3D12SerializeRootSignature(&rsd, &sig, &err)) ||
        FAILED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&root_)))) {
        Log("tx_subdiv: root signature creation failed");
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC cpd{};
    cpd.pRootSignature = root_.Get();
    cpd.CS = {g_TxSubdivCS, sizeof(g_TxSubdivCS)};
    if (FAILED(dev->CreateComputePipelineState(&cpd, IID_PPV_ARGS(&pso_)))) {
        Log("tx_subdiv: compute PSO creation failed");
        root_.Reset();
        return false;
    }
    return true;
}

void SubdivCache::Shutdown()
{
    std::unique_lock<std::shared_mutex> lk(m_);
    cache_.clear();
    layoutInfo_.clear();
    pso_.Reset();
    root_.Reset();
    meshCount_.store(0, std::memory_order_relaxed);
    vramBytes_.store(0, std::memory_order_relaxed);
}

SubdivLayoutInfo SubdivCache::AnalyzeLayout(const LayoutData& data) const
{
    SubdivLayoutInfo info{};
    uint32_t nextOff[32]{};
    bool hasSkin = false;
    bool hasMultiSlot = false;
    for (const auto& e : data.elems) {
        if (e.InputSlot >= 32) return info;
        if (e.InputSlot != 0) hasMultiSlot = true;
        if (e.InputSlotClass != D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA) return info;
        const uint32_t off = (e.AlignedByteOffset == D3D12_APPEND_ALIGNED_ELEMENT) ? nextOff[e.InputSlot] : e.AlignedByteOffset;
        const uint32_t sz = FmtBytes(e.Format);
        nextOff[e.InputSlot] = off + sz;
        if (!e.SemanticName) continue;
        if (_stricmp(e.SemanticName, "BLENDWEIGHT") == 0 || _stricmp(e.SemanticName, "BLENDINDICES") == 0) {
            hasSkin = true;
        }
        if (e.InputSlot != 0 || e.SemanticIndex != 0 || (off & 3u) != 0u) continue;
        if (_stricmp(e.SemanticName, "POSITION") == 0) {
            if (e.Format == DXGI_FORMAT_R32G32B32_FLOAT)         info.posOffFmt = PackOffFmt(off, 1u);
            else if (e.Format == DXGI_FORMAT_R32G32B32A32_FLOAT) info.posOffFmt = PackOffFmt(off, 2u);
            else if (e.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) info.posOffFmt = PackOffFmt(off, 3u);
        } else if (_stricmp(e.SemanticName, "NORMAL") == 0) {
            if (e.Format == DXGI_FORMAT_R32G32B32_FLOAT)         info.normOffFmt = PackOffFmt(off, 1u);
            else if (e.Format == DXGI_FORMAT_R8G8B8A8_UNORM)     info.normOffFmt = PackOffFmt(off, 2u);
            else if (e.Format == DXGI_FORMAT_R8G8B8A8_SNORM)     info.normOffFmt = PackOffFmt(off, 3u);
            else if (e.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) info.normOffFmt = PackOffFmt(off, 4u);
        } else if (_stricmp(e.SemanticName, "TANGENT") == 0) {
            if (e.Format == DXGI_FORMAT_R32G32B32_FLOAT)         info.tangOffFmt = PackOffFmt(off, 1u);
            else if (e.Format == DXGI_FORMAT_R8G8B8A8_UNORM)     info.tangOffFmt = PackOffFmt(off, 2u);
            else if (e.Format == DXGI_FORMAT_R8G8B8A8_SNORM)     info.tangOffFmt = PackOffFmt(off, 3u);
            else if (e.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) info.tangOffFmt = PackOffFmt(off, 4u);
        } else if (_stricmp(e.SemanticName, "TEXCOORD") == 0) {
            if (e.Format == DXGI_FORMAT_R32G32_FLOAT)            info.uvOffFmt = PackOffFmt(off, 1u);
            else if (e.Format == DXGI_FORMAT_R16G16_FLOAT)       info.uvOffFmt = PackOffFmt(off, 2u);
        } else if (_stricmp(e.SemanticName, "COLOR") == 0) {
            if (e.Format == DXGI_FORMAT_R8G8B8A8_UNORM)          info.colOffFmt = PackOffFmt(off, 1u);
        }
    }
    // Require single-slot rigid geometry with both POSITION and NORMAL so curved PN-Triangle tangents are physically valid.
    info.eligible = !hasSkin && !hasMultiSlot && (info.posOffFmt >> 16u) != 0u && (info.normOffFmt >> 16u) != 0u;
    return info;
}

const SubdivLayoutInfo* SubdivCache::GetLayoutInfo(uint32_t layoutId, const LayoutData* data)
{
    if (!layoutId || !data) return nullptr;
    {
        std::shared_lock<std::shared_mutex> lk(m_);
        auto it = layoutInfo_.find(layoutId);
        if (it != layoutInfo_.end()) return &it->second;
    }
    SubdivLayoutInfo info = AnalyzeLayout(*data);
    std::unique_lock<std::shared_mutex> lk(m_);
    auto [it, _] = layoutInfo_.emplace(layoutId, info);
    return &it->second;
}

std::shared_ptr<const SubdivEntry> SubdivCache::Find(const SubdivKey& key) const
{
    std::shared_lock<std::shared_mutex> lk(m_);
    auto it = cache_.find(key);
    return it != cache_.end() ? it->second : nullptr;
}

std::shared_ptr<const SubdivEntry> SubdivCache::Build(ID3D12Device* dev, ID3D12GraphicsCommandList* cl,
                                                      const SubdivBuildReq& req, bool& outDispatched)
{
    outDispatched = false;
    if (!Ready() || !dev || !cl || !req.layout || !req.layout->eligible || !req.inVerts || !req.inIdx) return nullptr;
    if (req.key.indexCount < 12u || req.key.indexCount > 36000u || (req.key.indexCount % 3u) != 0u) return nullptr;
    if (req.key.stride < 16u || req.key.stride > 128u || (req.key.stride & 3u) != 0u) return nullptr;
    if (req.inVertsBytes <= req.key.vbOffset + req.key.stride) return nullptr;

    const bool is32 = (req.key.ibFmt == DXGI_FORMAT_R32_UINT);
    const uint64_t idxBytesNeeded = uint64_t(req.key.ibOffset) + (uint64_t(req.key.startIndex) + req.key.indexCount) * (is32 ? 4ull : 2ull);
    if (idxBytesNeeded > req.inIdxBytes) return nullptr;

    {
        std::shared_lock<std::shared_mutex> lk(m_);
        auto it = cache_.find(req.key);
        if (it != cache_.end()) return it->second;
    }

    const uint32_t triCount = req.key.indexCount / 3u;
    const uint64_t outVertBytes = uint64_t(triCount) * 6ull * uint64_t(req.key.stride);
    const uint64_t outIdxBytes = uint64_t(triCount) * 12ull * 4ull;
    const uint64_t totalNeed = outVertBytes + outIdxBytes;
    if (vramBytes_.load(std::memory_order_relaxed) + totalNeed > kMaxVramBytes ||
        meshCount_.load(std::memory_order_relaxed) >= kMaxMeshes) {
        return nullptr;
    }

    auto entry = std::make_shared<SubdivEntry>();
    entry->vbTr = std::make_shared<TrackedRes>();
    entry->ibTr = std::make_shared<TrackedRes>();
    if (!CreateDefaultBuf(dev, outVertBytes, entry->vbTr->res) ||
        !CreateDefaultBuf(dev, outIdxBytes, entry->ibTr->res)) {
        return nullptr;
    }
    entry->vbTr->subs = 1;
    entry->vbTr->global.assign(1, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    entry->ibTr->subs = 1;
    entry->ibTr->global.assign(1, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    entry->vbView = {entry->vbTr->res->GetGPUVirtualAddress(), UINT(outVertBytes), req.key.stride};
    entry->ibView = {entry->ibTr->res->GetGPUVirtualAddress(), UINT(outIdxBytes), DXGI_FORMAT_R32_UINT};
    entry->outIndexCount = triCount * 12u;
    entry->vramBytes = totalNeed;

    const uint64_t availVerts = (req.inVertsBytes - req.key.vbOffset) / req.key.stride;
    if (availVerts == 0) return nullptr;

    SubdivCB cb{};
    cb.triCount = triCount;
    cb.strideBytes = req.key.stride;
    cb.vbByteOffset = req.key.vbOffset;
    cb.ibByteOffset = req.key.ibOffset + req.key.startIndex * (is32 ? 4u : 2u);
    cb.baseVertex = req.key.baseVertex;
    cb.idxIs32Bit = is32 ? 1u : 0u;
    cb.posOffFmt = req.layout->posOffFmt;
    cb.normOffFmt = req.layout->normOffFmt;
    cb.tangOffFmt = req.layout->tangOffFmt;
    cb.uvOffFmt = req.layout->uvOffFmt;
    cb.colOffFmt = req.layout->colOffFmt;
    cb.curvatureGain = req.curvatureGain;
    cb.maxVertIdx = static_cast<uint32_t>(availVerts - 1ull);
    cb.maxOffsetFrac = 0.18f;
    cb.snowDepthU = req.snowDepthU;
    cb.displaceAmp = req.displaceAmp;

    // Newly created buffers start in COMMON and implicitly promote to UNORDERED_ACCESS on first compute write.
    cl->SetComputeRootSignature(root_.Get());
    cl->SetPipelineState(pso_.Get());
    cl->SetComputeRoot32BitConstants(0, 16, &cb, 0);
    cl->SetComputeRootShaderResourceView(1, req.inVerts->GetGPUVirtualAddress());
    cl->SetComputeRootShaderResourceView(2, req.inIdx->GetGPUVirtualAddress());
    cl->SetComputeRootUnorderedAccessView(3, entry->vbTr->res->GetGPUVirtualAddress());
    cl->SetComputeRootUnorderedAccessView(4, entry->ibTr->res->GetGPUVirtualAddress());
    cl->Dispatch((triCount + 63u) / 64u, 1u, 1u);

    D3D12_RESOURCE_BARRIER b[2]{};
    b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[0].Transition = {entry->vbTr->res.Get(), 0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER};
    b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[1].Transition = {entry->ibTr->res.Get(), 0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDEX_BUFFER};
    cl->ResourceBarrier(2, b);
    outDispatched = true;

    std::unique_lock<std::shared_mutex> lk(m_);
    auto [it, inserted] = cache_.emplace(req.key, entry);
    if (inserted) {
        meshCount_.fetch_add(1, std::memory_order_relaxed);
        vramBytes_.fetch_add(totalNeed, std::memory_order_relaxed);
        return entry;
    }
    return it->second;
}

void SubdivCache::InvalidateSource(const TrackedRes* tr, Retire& retire)
{
    if (!tr) return;
    std::unique_lock<std::shared_mutex> lk(m_);
    for (auto it = cache_.begin(); it != cache_.end(); ) {
        if (it->first.vbTr == tr || it->first.ibTr == tr) {
            if (it->second) {
                vramBytes_.fetch_sub(it->second->vramBytes, std::memory_order_relaxed);
                meshCount_.fetch_sub(1, std::memory_order_relaxed);
                if (it->second->vbTr) retire.keep.push_back(std::move(it->second->vbTr));
                if (it->second->ibTr) retire.keep.push_back(std::move(it->second->ibTr));
            }
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

void SubdivCache::ClearAll(Retire& retire)
{
    std::unique_lock<std::shared_mutex> lk(m_);
    for (auto& [k, v] : cache_) {
        if (!v) continue;
        if (v->vbTr) retire.keep.push_back(std::move(v->vbTr));
        if (v->ibTr) retire.keep.push_back(std::move(v->ibTr));
    }
    cache_.clear();
    meshCount_.store(0, std::memory_order_relaxed);
    vramBytes_.store(0, std::memory_order_relaxed);
}

}  // namespace raw::tx
