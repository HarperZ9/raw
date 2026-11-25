// RawXpbdSimHost.cpp: Native DX12 GPU Compute Host for Multi-Chain XPBD Soft-Body, Hair, Cape & Bush-Bone Solver.
// Original clean-room implementation, MIT License.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "RawXpbdSimHost.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if __has_include("raw_xpbd_cloth_cs.h")
#define RAW_XPBD_HAS_EMBEDDED_DXIL 1
#include "raw_xpbd_cloth_cs.h"
#else
#define RAW_XPBD_HAS_EMBEDDED_DXIL 0
#endif

namespace raw::bridge
{
namespace
{
using Microsoft::WRL::ComPtr;

// GPU constant buffer matching RawXpbdClothSim.hlsl byte-for-byte (256-byte aligned).
struct alignas(256) GpuXpbdStepCB {
    uint32_t     counts[4];       // x = chainCount, y = particleCount, z = substeps, w = colliderCount
    float        timeAndGrav[4];  // x = dt, y = timeSec, z = gravityZ, w = snowLoad01
    float        windParams[4];   // xyz = windDir, w = windSpeedMps
    float        gustAndClamp[4]; // x = gustPhase, y = maxSpeed, z = collisionMargin, w = 0
    XpbdCollider colliders[8];    // 8 * 32 = 256 bytes
    uint32_t     _pad[48];        // 64 + 256 + 192 = 512 bytes exact
};
static_assert(sizeof(GpuXpbdStepCB) == 512u, "GpuXpbdStepCB must be 512 bytes");

// Fixed shader-visible descriptor heap layout:
//   Slot 0: ChainMetaBuf SRV (t0)
//   Slot 1: ParticleBuf UAV  (u0)
//   Slot 2: ParticleBuf SRV  (external consumer / vertex shader binding)
constexpr uint32_t kTotalDescCount     = 4u;
constexpr uint32_t kSlotChainMetaSrv   = 0u;
constexpr uint32_t kSlotParticleUav    = 1u;
constexpr uint32_t kSlotParticleExtSrv = 2u;

bool CreateBuffer(ID3D12Device* dev, uint64_t bytes, D3D12_HEAP_TYPE heapType,
                  D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, ComPtr<ID3D12Resource>& out)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = heapType;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (bytes + 255ull) & ~255ull;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = flags;
    return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initState, nullptr, IID_PPV_ARGS(&out)));
}

void WriteStructuredSrv(ID3D12Device* dev, ID3D12Resource* res, uint32_t count, uint32_t stride,
                        D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC d{};
    d.Format = DXGI_FORMAT_UNKNOWN;
    d.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.Buffer.FirstElement = 0;
    d.Buffer.NumElements = count;
    d.Buffer.StructureByteStride = stride;
    dev->CreateShaderResourceView(res, &d, dst);
}

void WriteStructuredUav(ID3D12Device* dev, ID3D12Resource* res, uint32_t count, uint32_t stride,
                        D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
    d.Format = DXGI_FORMAT_UNKNOWN;
    d.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    d.Buffer.FirstElement = 0;
    d.Buffer.NumElements = count;
    d.Buffer.StructureByteStride = stride;
    dev->CreateUnorderedAccessView(res, nullptr, &d, dst);
}

} // namespace

// Deterministic CPU builder generating hair strands, cloth cape ribbons, and articulated multi-branch bushes
// (default 256 chains x 16 nodes = 4,096 simulated particles).
void BuildHairAndFoliageDemoChains(std::vector<XpbdParticle>& outParticles,
                                   std::vector<XpbdChainMeta>& outChains,
                                   uint32_t totalChains)
{
    outParticles.clear();
    outChains.clear();
    if (totalChains == 0u) return;

    constexpr uint32_t kNodes = RawXpbdSimHost::kMaxNodesPerChain; // 16
    constexpr float kTwoPi = 6.28318530718f;
    outParticles.reserve(static_cast<size_t>(totalChains) * kNodes);
    outChains.reserve(totalChains);

    const uint32_t hairCount = (totalChains * 3u) / 8u;                 // ~96 chains
    const uint32_t capeCount = (totalChains * 5u) / 16u;                // ~80 chains
    const uint32_t bushStart = hairCount + capeCount;                   // ~176..255 (80 chains)

    for (uint32_t ci = 0u; ci < totalChains; ++ci) {
        XpbdChainMeta meta{};
        meta.startParticle = static_cast<uint32_t>(outParticles.size());
        meta.nodeCount     = kNodes;

        float rootX = 0.0f, rootY = 0.0f, rootZ = 0.0f;
        float dirX = 0.0f, dirY = 0.0f, dirZ = -1.0f;
        float segLen = 0.08f;
        float baseDrag = 1.0f;

        if (ci < hairCount) {
            // Hair strands draped around actor head at (0, 0, 1.72m), falling along -Z outside torso/head
            meta.chainType           = static_cast<uint32_t>(XpbdChainType::HairStrand);
            meta.distCompliance      = 1.0e-6f;
            meta.bendCompliance      = 1.5e-3f;
            meta.restSpringStiffness = 0.14f;
            meta.damping             = 2.2f;
            const float ang = kTwoPi * (static_cast<float>(ci) + 0.5f) / static_cast<float>(std::max(hairCount, 1u));
            const float rScalp = 0.14f;
            rootX = std::cos(ang) * rScalp;
            rootY = std::sin(ang) * rScalp;
            rootZ = 1.75f;
            dirX  = std::cos(ang) * 0.22f;
            dirY  = std::sin(ang) * 0.22f;
            dirZ  = -0.975f;
            segLen = 0.045f;
            baseDrag = 1.35f;
        } else if (ci < bushStart) {
            // Cloth cape ribbons hanging from actor shoulders at y = -0.22m, z = 1.48m
            meta.chainType           = static_cast<uint32_t>(XpbdChainType::ClothCape);
            meta.distCompliance      = 1.0e-6f;
            meta.bendCompliance      = 3.0e-3f;
            meta.restSpringStiffness = 0.0f;
            meta.damping             = 1.6f;
            const uint32_t k = ci - hairCount;
            const float u = (capeCount > 1u) ? (static_cast<float>(k) / static_cast<float>(capeCount - 1u) - 0.5f) : 0.0f;
            rootX = u * 0.56f;
            rootY = -0.24f - 0.04f * (1.0f - 4.0f * u * u);
            rootZ = 1.48f;
            dirX  = u * 0.08f;
            dirY  = -0.18f;
            dirZ  = -0.980f;
            segLen = 0.075f;
            baseDrag = 1.65f;
        } else {
            // Articulated multi-branch bushes rooted at z = 0 around bush center (3.0, 0.0, 0.0)
            // (upright curved branches with elastic spring-back that part smoothly around a passing player sphere!)
            meta.chainType           = static_cast<uint32_t>(XpbdChainType::ArticulatedBush);
            meta.distCompliance      = 1.0e-6f;
            meta.bendCompliance      = 8.0e-4f;
            meta.restSpringStiffness = 0.35f;
            meta.damping             = 2.6f;
            const uint32_t k = ci - bushStart;
            const uint32_t bushCount = std::max(totalChains - bushStart, 1u);
            const float ang = kTwoPi * (static_cast<float>(k) * 0.6180339887f);
            const float ringR = 0.06f + 0.38f * std::sqrt((static_cast<float>(k) + 0.5f) / static_cast<float>(bushCount));
            rootX = 3.0f + std::cos(ang) * ringR;
            rootY = std::sin(ang) * ringR;
            rootZ = 0.0f;
            const float splay = 0.25f + 0.35f * (ringR / 0.44f);
            dirX  = std::cos(ang) * splay;
            dirY  = std::sin(ang) * splay;
            dirZ  = std::sqrt(std::max(1.0f - splay * splay, 0.1f));
            segLen = 0.085f;
            baseDrag = 0.95f;
        }

        const float dNorm = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
        dirX /= dNorm; dirY /= dNorm; dirZ /= dNorm;

        for (uint32_t ni = 0u; ni < kNodes; ++ni) {
            XpbdParticle p{};
            const float s = static_cast<float>(ni) * segLen;
            p.pos[0] = rootX + dirX * s;
            p.pos[1] = rootY + dirY * s;
            p.pos[2] = rootZ + dirZ * s;
            p.prevPos[0] = p.pos[0];
            p.prevPos[1] = p.pos[1];
            p.prevPos[2] = p.pos[2];
            p.restPos[0] = p.pos[0];
            p.restPos[1] = p.pos[1];
            p.restPos[2] = p.pos[2];
            p.invMass     = (ni == 0u) ? 0.0f : 1.0f; // root strictly pinned
            p.restLen     = segLen;
            p.bendRestLen = 2.0f * segLen * 0.985f;   // 2-hop straightness chord
            p.dragCoeff   = baseDrag * (0.65f + 0.70f * (static_cast<float>(ni) / static_cast<float>(kNodes - 1u)));
            outParticles.push_back(p);
        }
        outChains.push_back(meta);
    }
}

RawXpbdSimHost::~RawXpbdSimHost()
{
    Shutdown();
}

void RawXpbdSimHost::Shutdown()
{
    if (m_cbBuf && m_cbMapped) {
        m_cbBuf->Unmap(0, nullptr);
        m_cbMapped = nullptr;
    }
    m_tsReadback.Reset();
    m_tsHeap.Reset();
    m_cbBuf.Reset();
    m_chainMetaUpload.Reset();
    m_particleUpload.Reset();
    m_chainMetaBuf.Reset();
    m_particleBuf.Reset();
    m_pso.Reset();
    m_root.Reset();
    m_heap.Reset();
    m_dev = nullptr;
    m_ready = false;
    m_chainCount = 0u;
    m_particleCount = 0u;
    m_frameIdx = 0u;
    m_lastStepMs = 0.0f;
    m_hasValidTiming = false;
}

bool RawXpbdSimHost::Init(ID3D12Device* dev, const XpbdSimDesc& desc, std::string* outErr)
{
    Shutdown();
    if (!dev) {
        if (outErr) *outErr = "RawXpbdSimHost: null ID3D12Device";
        return false;
    }

    D3D12_SHADER_BYTECODE cs = desc.customShader;
    if (!cs.pShaderBytecode || cs.BytecodeLength == 0) {
#if RAW_XPBD_HAS_EMBEDDED_DXIL
        cs = {kDxil_raw_xpbd_cloth, sizeof kDxil_raw_xpbd_cloth};
#else
        if (outErr) *outErr = "RawXpbdSimHost: no embedded raw_xpbd_cloth_cs.h and no customShader provided";
        return false;
#endif
    }

    m_dev          = dev;
    m_maxChains    = std::max(desc.maxChains, 1u);
    m_maxParticles = std::max(desc.maxParticles, 16u);

    // 1) Descriptor heap (4 shader-visible descriptors)
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kTotalDescCount;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) {
        if (outErr) *outErr = "RawXpbdSimHost: CreateDescriptorHeap failed";
        return false;
    }
    m_descInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 2) Root signature: b0 (CBV), t0 (ChainMetaBuf SRV table), u0 (ParticleBuf UAV table)
    {
        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.BaseShaderRegister = 0;

        D3D12_DESCRIPTOR_RANGE uavRange{};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1;
        uavRange.BaseShaderRegister = 0;

        D3D12_ROOT_PARAMETER rp[3]{};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rp[0].Descriptor = {0, 0};
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[1].DescriptorTable = {1, &srvRange};
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[2].DescriptorTable = {1, &uavRange};
        rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd{3, rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) ||
            FAILED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
            if (outErr) *outErr = "RawXpbdSimHost: root signature creation failed";
            return false;
        }
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_root.Get();
    pd.CS = cs;
    if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_pso)))) {
        if (outErr) *outErr = "RawXpbdSimHost: CreateComputePipelineState failed";
        return false;
    }

    // 3) GPU default & upload staging buffers
    const uint64_t particleBytes = static_cast<uint64_t>(m_maxParticles) * sizeof(XpbdParticle);
    const uint64_t chainBytes    = static_cast<uint64_t>(m_maxChains) * sizeof(XpbdChainMeta);
    const uint64_t cbBytes       = static_cast<uint64_t>(kCbRingCount) * sizeof(GpuXpbdStepCB);

    if (!CreateBuffer(dev, particleBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                      D3D12_RESOURCE_STATE_COMMON, m_particleBuf) ||
        !CreateBuffer(dev, chainBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
                      D3D12_RESOURCE_STATE_COMMON, m_chainMetaBuf) ||
        !CreateBuffer(dev, particleBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                      D3D12_RESOURCE_STATE_GENERIC_READ, m_particleUpload) ||
        !CreateBuffer(dev, chainBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                      D3D12_RESOURCE_STATE_GENERIC_READ, m_chainMetaUpload) ||
        !CreateBuffer(dev, cbBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                      D3D12_RESOURCE_STATE_GENERIC_READ, m_cbBuf)) {
        if (outErr) *outErr = "RawXpbdSimHost: GPU buffer allocation failed";
        return false;
    }
    m_particleState  = D3D12_RESOURCE_STATE_COMMON;
    m_chainMetaState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RANGE rr{0, 0};
    void* ptr = nullptr;
    if (FAILED(m_cbBuf->Map(0, &rr, &ptr))) {
        if (outErr) *outErr = "RawXpbdSimHost: failed to map CBV ring";
        return false;
    }
    m_cbMapped = static_cast<uint8_t*>(ptr);
    std::memset(m_cbMapped, 0, static_cast<size_t>(cbBytes));

    // 4) Populate descriptors
    WriteStructuredSrv(dev, m_chainMetaBuf.Get(), m_maxChains,    sizeof(XpbdChainMeta), CpuAt(kSlotChainMetaSrv));
    WriteStructuredUav(dev, m_particleBuf.Get(),  m_maxParticles, sizeof(XpbdParticle),  CpuAt(kSlotParticleUav));
    WriteStructuredSrv(dev, m_particleBuf.Get(),  m_maxParticles, sizeof(XpbdParticle),  CpuAt(kSlotParticleExtSrv));

    // 5) Optional 4-frame ring GPU timestamp query heap (2 queries/frame: Start, End)
    {
        constexpr uint32_t kTotalQueries = kCbRingCount * 2u;
        D3D12_QUERY_HEAP_DESC qd{};
        qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qd.Count = kTotalQueries;
        if (FAILED(dev->CreateQueryHeap(&qd, IID_PPV_ARGS(&m_tsHeap))) ||
            !CreateBuffer(dev, static_cast<uint64_t>(kTotalQueries) * sizeof(uint64_t),
                          D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
                          D3D12_RESOURCE_STATE_COPY_DEST, m_tsReadback)) {
            m_tsHeap.Reset();
            m_tsReadback.Reset();
        }
    }

    m_ready = true;
    return true;
}

void RawXpbdSimHost::Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                                D3D12_RESOURCE_STATES& curState, D3D12_RESOURCE_STATES target)
{
    if (!res || curState == target) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.StateBefore = curState;
    b.Transition.StateAfter = target;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &b);
    curState = target;
}

bool RawXpbdSimHost::HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const
{
    if (!m_tsReadback || m_tsFreqHz == 0u) return false;
    const SIZE_T byteOff = static_cast<SIZE_T>(queryStartIdx) * sizeof(uint64_t);
    const D3D12_RANGE rr{byteOff, byteOff + 2u * sizeof(uint64_t)};
    void* ptr = nullptr;
    if (FAILED(m_tsReadback->Map(0, &rr, &ptr))) return false;
    const auto* ticks = reinterpret_cast<const uint64_t*>(static_cast<const uint8_t*>(ptr) + byteOff);
    const uint64_t t0 = ticks[0];
    const uint64_t t1 = ticks[1];
    const D3D12_RANGE wr{0, 0};
    m_tsReadback->Unmap(0, &wr);
    if (t1 < t0) return false;
    const double ms = static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(m_tsFreqHz);
    if (!std::isfinite(ms) || ms < 0.0 || ms > 5000.0) return false;
    outMs = static_cast<float>(ms);
    return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE RawXpbdSimHost::CpuAt(uint32_t slot) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * m_descInc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawXpbdSimHost::GpuAt(uint32_t slot) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * m_descInc;
    return h;
}

bool RawXpbdSimHost::UploadChains(ID3D12GraphicsCommandList* cmd,
                                  const std::vector<XpbdParticle>& particles,
                                  const std::vector<XpbdChainMeta>& chains)
{
    if (!m_ready || !cmd || particles.empty() || chains.empty()) return false;
    if (particles.size() > m_maxParticles || chains.size() > m_maxChains) return false;

    const size_t pBytes = particles.size() * sizeof(XpbdParticle);
    const size_t cBytes = chains.size() * sizeof(XpbdChainMeta);

    D3D12_RANGE rr{0, 0};
    void* pPtr = nullptr;
    void* cPtr = nullptr;
    if (FAILED(m_particleUpload->Map(0, &rr, &pPtr))) return false;
    std::memcpy(pPtr, particles.data(), pBytes);
    m_particleUpload->Unmap(0, nullptr);

    if (FAILED(m_chainMetaUpload->Map(0, &rr, &cPtr))) return false;
    std::memcpy(cPtr, chains.data(), cBytes);
    m_chainMetaUpload->Unmap(0, nullptr);

    Transition(cmd, m_particleBuf.Get(),  m_particleState,  D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(cmd, m_chainMetaBuf.Get(), m_chainMetaState, D3D12_RESOURCE_STATE_COPY_DEST);

    cmd->CopyBufferRegion(m_particleBuf.Get(),  0, m_particleUpload.Get(),  0, pBytes);
    cmd->CopyBufferRegion(m_chainMetaBuf.Get(), 0, m_chainMetaUpload.Get(), 0, cBytes);

    constexpr D3D12_RESOURCE_STATES kAllSrv =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    Transition(cmd, m_particleBuf.Get(),  m_particleState,  kAllSrv);
    Transition(cmd, m_chainMetaBuf.Get(), m_chainMetaState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    m_particleCount = static_cast<uint32_t>(particles.size());
    m_chainCount    = static_cast<uint32_t>(chains.size());
    return true;
}

void RawXpbdSimHost::Step(ID3D12GraphicsCommandList* cmd, const XpbdStepParams& params)
{
    if (!m_ready || !cmd || m_chainCount == 0u || m_particleCount == 0u || !m_cbMapped) return;

    // Harvest 3-frames-old retired timestamp slot (non-blocking)
    if (m_frameIdx >= (kCbRingCount - 1u)) {
        const uint32_t retired = (m_frameIdx - (kCbRingCount - 1u)) % kCbRingCount;
        float ms = 0.0f;
        if (HarvestTimestampPair(retired * 2u, ms)) {
            m_lastStepMs = ms;
            m_hasValidTiming = true;
        }
    }

    GpuXpbdStepCB cb{};
    cb.counts[0] = m_chainCount;
    cb.counts[1] = m_particleCount;
    cb.counts[2] = std::clamp(params.substeps, 1u, 8u);
    cb.counts[3] = std::min(params.colliderCount, 8u);

    cb.timeAndGrav[0] = std::clamp(params.dt, 1.0e-4f, 0.05f);
    cb.timeAndGrav[1] = params.timeSec;
    cb.timeAndGrav[2] = params.gravityZ;
    cb.timeAndGrav[3] = std::clamp(params.snowLoad01, 0.0f, 1.0f);

    cb.windParams[0] = params.windDir[0];
    cb.windParams[1] = params.windDir[1];
    cb.windParams[2] = params.windDir[2];
    cb.windParams[3] = std::max(params.windSpeedMps, 0.0f);

    cb.gustAndClamp[0] = params.gustPhase;
    cb.gustAndClamp[1] = std::max(params.maxSpeed, 1.0f);
    cb.gustAndClamp[2] = std::max(params.collisionMargin, 0.0f);
    cb.gustAndClamp[3] = 0.0f;

    for (uint32_t i = 0; i < cb.counts[3]; ++i) {
        cb.colliders[i] = params.colliders[i];
    }

    const uint32_t ring = m_frameIdx % kCbRingCount;
    const size_t byteOff = static_cast<size_t>(ring) * sizeof(GpuXpbdStepCB);
    std::memcpy(m_cbMapped + byteOff, &cb, sizeof(cb));
    const D3D12_GPU_VIRTUAL_ADDRESS cbGpu = m_cbBuf->GetGPUVirtualAddress() + byteOff;

    Transition(cmd, m_chainMetaBuf.Get(), m_chainMetaState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_particleBuf.Get(),  m_particleState,  D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (m_tsHeap) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 2u + 0u);
    }

    ID3D12DescriptorHeap* heaps[1] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetComputeRootSignature(m_root.Get());
    cmd->SetPipelineState(m_pso.Get());
    cmd->SetComputeRootConstantBufferView(0, cbGpu);
    cmd->SetComputeRootDescriptorTable(1, GpuAt(kSlotChainMetaSrv));
    cmd->SetComputeRootDescriptorTable(2, GpuAt(kSlotParticleUav));

    // ONE SINGLE COMPUTE DISPATCH for all active chains (4 chains per 64-thread group)
    const uint32_t groups = (m_chainCount + kChainsPerGroup - 1u) / kChainsPerGroup;
    cmd->Dispatch(groups, 1u, 1u);

    if (m_tsHeap && m_tsReadback) {
        cmd->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 2u + 1u);
        cmd->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, ring * 2u + 0u, 2u,
                              m_tsReadback.Get(), static_cast<UINT64>(ring * 2u) * sizeof(uint64_t));
    }

    constexpr D3D12_RESOURCE_STATES kAllSrv =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    Transition(cmd, m_particleBuf.Get(), m_particleState, kAllSrv);

    ++m_frameIdx;
}

D3D12_GPU_DESCRIPTOR_HANDLE RawXpbdSimHost::GetParticleSRV() const
{
    return GpuAt(kSlotParticleExtSrv);
}

D3D12_CPU_DESCRIPTOR_HANDLE RawXpbdSimHost::GetParticleSRVCpu() const
{
    return CpuAt(kSlotParticleExtSrv);
}

} // namespace raw::bridge
