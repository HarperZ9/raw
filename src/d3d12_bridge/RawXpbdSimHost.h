#pragma once
// RawXpbdSimHost.h: Native DX12 GPU Compute Host for RAW's Multi-Chain XPBD Soft-Body,
// Hair Strand, Cloth Cape & Articulated Bush-Bone Foliage Solver (SM 6.6 / 6.10).
//
// Runs up to 256 chains x 16 nodes = 4,096 simulated particles in ONE compute dispatch
// with hardware DX12 ID3D12QueryHeap GPU timestamp profiling and analytic capsule/sphere colliders.
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

namespace raw::bridge
{

enum class XpbdChainType : uint32_t {
    HairStrand      = 0u,
    ClothCape       = 1u,
    ArticulatedBush = 2u
};

// 64-byte GPU particle structure matching RawXpbdClothSim.hlsl byte-for-byte.
struct XpbdParticle {
    float pos[3]      = {0.0f, 0.0f, 0.0f};
    float invMass     = 1.0f; // 0.0 pins root node 0
    float prevPos[3]  = {0.0f, 0.0f, 0.0f};
    float restLen     = 0.1f; // rest segment length to parent (i - 1)
    float vel[3]      = {0.0f, 0.0f, 0.0f};
    float bendRestLen = 0.2f; // rest 2-hop chord length between (i - 1) and (i + 1)
    float restPos[3]  = {0.0f, 0.0f, 0.0f};
    float dragCoeff   = 1.0f; // aerodynamic wind drag gain
};
static_assert(sizeof(XpbdParticle) == 64u, "XpbdParticle must be 64 bytes");

// 32-byte per-chain metadata matching RawXpbdClothSim.hlsl byte-for-byte.
struct XpbdChainMeta {
    uint32_t startParticle       = 0u;
    uint32_t nodeCount           = 0u;      // 2..16
    uint32_t chainType           = 0u;      // XpbdChainType
    uint32_t flags               = 0u;
    float    distCompliance      = 1.0e-6f; // XPBD distance compliance
    float    bendCompliance      = 2.0e-3f; // XPBD 2-hop bending compliance
    float    restSpringStiffness = 0.0f;    // elastic rest-pose recovery [0..1]
    float    damping             = 1.5f;    // exponential velocity damping (1/s)
};
static_assert(sizeof(XpbdChainMeta) == 32u, "XpbdChainMeta must be 32 bytes");

// 32-byte analytic sphere/capsule collider matching RawXpbdClothSim.hlsl byte-for-byte.
struct XpbdCollider {
    float p0[3]  = {0.0f, 0.0f, 0.0f}; // sphere center OR capsule endpoint A
    float radius = 0.0f;               // > 0 active; <= 0 disabled
    float p1[3]  = {0.0f, 0.0f, 0.0f}; // capsule endpoint B
    float flags  = 0.0f;               // 0.0 = sphere, 1.0 = capsule
};
static_assert(sizeof(XpbdCollider) == 32u, "XpbdCollider must be 32 bytes");

struct XpbdSimDesc {
    uint32_t              maxChains    = 256u;
    uint32_t              maxParticles = 4096u;
    D3D12_SHADER_BYTECODE customShader = {}; // optional override; uses embedded raw_xpbd_cloth_cs.h when empty
};

struct XpbdStepParams {
    float        dt              = 1.0f / 60.0f;
    float        timeSec         = 0.0f;
    uint32_t     substeps        = 4u;
    float        gravityZ        = -9.80665f; // Z-up gravity (m/s^2)
    float        windDir[3]      = {1.0f, 0.25f, 0.0f};
    float        windSpeedMps    = 3.0f;
    float        gustPhase       = 0.0f;
    float        snowLoad01      = 0.0f;      // 0..1 winter snow-load mass multiplier
    float        maxSpeed        = 35.0f;     // finite velocity clamp
    float        collisionMargin = 0.005f;    // safety margin outside analytic colliders
    uint32_t     colliderCount   = 0u;        // 0..8
    XpbdCollider colliders[8]    = {};
};

// Deterministic CPU builder generating hair strands, cloth cape ribbons, and articulated multi-branch bushes
// (default 256 chains x 16 nodes = 4,096 particles).
void BuildHairAndFoliageDemoChains(std::vector<XpbdParticle>& outParticles,
                                   std::vector<XpbdChainMeta>& outChains,
                                   uint32_t totalChains = 256u);

class RawXpbdSimHost {
public:
    static constexpr uint32_t kMaxNodesPerChain = 16u;
    static constexpr uint32_t kChainsPerGroup   = 4u;
    static constexpr uint32_t kCbRingCount      = 4u;

    RawXpbdSimHost() = default;
    ~RawXpbdSimHost();

    RawXpbdSimHost(const RawXpbdSimHost&) = delete;
    RawXpbdSimHost& operator=(const RawXpbdSimHost&) = delete;

    bool Init(ID3D12Device* dev, const XpbdSimDesc& desc = {}, std::string* outErr = nullptr);
    bool Ready() const { return m_ready; }

    // Stages & copies initial particle and chain metadata buffers onto the GPU (leaves ParticleBuf in SRV state).
    bool UploadChains(ID3D12GraphicsCommandList* cmd,
                      const std::vector<XpbdParticle>& particles,
                      const std::vector<XpbdChainMeta>& chains);

    // Dispatches ONE single cs_xpbd_step compute pass for all active chains with 4-frame timestamp profiling.
    void Step(ID3D12GraphicsCommandList* cmd, const XpbdStepParams& params);

    ID3D12DescriptorHeap*       DescriptorHeap() const { return m_heap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetParticleSRV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetParticleSRVCpu() const;
    ID3D12Resource*             ParticleResource() const { return m_particleBuf.Get(); }
    D3D12_RESOURCE_STATES       ParticleResourceState() const { return m_particleState; }

    void     SetTimestampFrequency(uint64_t hz) { m_tsFreqHz = hz; }
    float    GetLastStepMs() const { return m_lastStepMs; }
    bool     HasValidTiming() const { return m_hasValidTiming; }
    uint32_t ChainCount() const { return m_chainCount; }
    uint32_t ParticleCount() const { return m_particleCount; }

    void Shutdown();

private:
    void Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                    D3D12_RESOURCE_STATES& curState, D3D12_RESOURCE_STATES target);
    bool HarvestTimestampPair(uint32_t queryStartIdx, float& outMs) const;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(uint32_t slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(uint32_t slot) const;

    bool                                        m_ready = false;
    ID3D12Device*                               m_dev = nullptr;
    uint32_t                                    m_maxChains = 256u;
    uint32_t                                    m_maxParticles = 4096u;
    uint32_t                                    m_chainCount = 0u;
    uint32_t                                    m_particleCount = 0u;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t                                     m_descInc = 0u;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_particleBuf;
    D3D12_RESOURCE_STATES                  m_particleState = D3D12_RESOURCE_STATE_COMMON;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_chainMetaBuf;
    D3D12_RESOURCE_STATES                  m_chainMetaState = D3D12_RESOURCE_STATE_COMMON;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_particleUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_chainMetaUpload;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_cbBuf;
    uint8_t*                               m_cbMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_tsHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>  m_tsReadback;
    uint64_t                                m_tsFreqHz = 0u;
    uint32_t                                m_frameIdx = 0u;
    float                                   m_lastStepMs = 0.0f;
    bool                                    m_hasValidTiming = false;
};

} // namespace raw::bridge
