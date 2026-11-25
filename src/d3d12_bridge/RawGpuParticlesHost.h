#pragma once
// RawGpuParticlesHost.h: Native DX12 GPU Compute 16,384-Particle 3D Physics & Screen-Space Splat Engine (SM 6.6 / 6.10).
// Supports deterministic emit/respawn, 3D curl-noise wind advection, mode-dependent buoyancy/gravity
// (Embers, Snow, Rain, WaterfallMist), ground elastic bounce restitution, atomic active-particle stats,
// race-free Q22.10 atomic additive screen-space splat buffer, and hardware ID3D12QueryHeap GPU profiling.
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace raw::bridge
{

enum class ParticleMode : uint32_t {
    Embers        = 0u,
    Snow          = 1u,
    Rain          = 2u,
    WaterfallMist = 3u
};

// Exact 64-byte GPU particle layout mirrored in RawGpuParticlesSim.hlsl
struct alignas(16) GpuParticleCpuView {
    float    pos[3];
    float    age;
    float    vel[3];
    float    maxLife;
    float    color[3];
    float    size;
    uint32_t mode;
    uint32_t state;       // bit 0 = alive, bit 1 = bounced
    uint32_t seed;
    uint32_t bounceCount;
};
static_assert(sizeof(GpuParticleCpuView) == 64u, "GpuParticleCpuView must be 64 bytes");

struct GpuParticlesStats {
    uint32_t activeCount   = 0u;
    uint32_t bouncedCount  = 0u;
    uint32_t respawnCount  = 0u;
    uint32_t splattedCount = 0u;
};

struct GpuParticlesDesc {
    uint32_t              particleCount     = 16384u;
    uint32_t              splatWidth        = 480u;
    uint32_t              splatHeight       = 270u;
    D3D12_SHADER_BYTECODE customSimShader   = {}; // optional override; uses raw_particles_sim_cs.h
    D3D12_SHADER_BYTECODE customSplatShader = {}; // optional override; uses raw_particles_splat_cs.h
};

struct GpuParticlesParams {
    // Row-major LH view-projection matrix (row-vector mul(float4(pos, 1), viewProj))
    float        viewProj[16]       = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    float        emitterPos[3]      = {0.0f, 1.5f, 0.0f};
    float        emitterRadius      = 0.85f;
    float        windVel[3]         = {1.5f, 0.0f, 0.4f};
    float        curlStrength       = 1.4f;
    float        dtSec              = 1.0f / 60.0f;
    float        timeSec            = 0.0f;
    float        groundY            = 0.0f;
    float        restitution        = 0.65f;
    float        gravityScale       = 1.0f;
    float        buoyancyAccel      = 4.2f;
    float        dragCoeff          = 0.85f;
    float        splatIntensity     = 1.0f;
    ParticleMode mode               = ParticleMode::Embers;
    uint32_t     frameIndex         = 0u;
    bool         resetAll           = false;
    bool         enableRespawn      = true;
    bool         enableGroundBounce = true;
};

// Pure CPU helper: builds row-major DX12 LH LookAt * Perspective projection for particle splatting.
void BuildParticlesViewProjLH(const float eye[3], const float target[3], const float up[3],
                              float fovYRad, float aspect, float nearZ, float farZ,
                              float outViewProj[16]);

class RawGpuParticlesHost {
public:
    static constexpr uint32_t kMaxParticles = 16384u;
    static constexpr uint32_t kRingCount    = 4u;
    static constexpr uint32_t kTotalDescs   = 4u; // u0 Particles, u1 Stats, u2 SplatBuf, t0 SplatSRV

    RawGpuParticlesHost() = default;
    ~RawGpuParticlesHost();

    RawGpuParticlesHost(const RawGpuParticlesHost&) = delete;
    RawGpuParticlesHost& operator=(const RawGpuParticlesHost&) = delete;

    bool Init(ID3D12Device* dev, const GpuParticlesDesc& desc = {}, std::string* outErr = nullptr);
    bool Ready() const { return m_ready; }

    // Runs clear -> cs_particles_sim -> UAV barrier -> cs_particles_splat.
    // Leaves ParticleBuffer/StatsBuffer/SplatBuffer in D3D12_RESOURCE_STATE_UNORDERED_ACCESS.
    bool Execute(ID3D12GraphicsCommandList* cmd, const GpuParticlesParams& params);

    ID3D12Resource* GetParticleBuffer() const { return m_particleBuf.Get(); }
    ID3D12Resource* GetStatsBuffer() const { return m_statsBuf.Get(); }
    ID3D12Resource* GetSplatBuffer() const { return m_splatBuf.Get(); }
    uint32_t        ParticleCount() const { return m_particleCount; }
    uint32_t        SplatWidth() const { return m_splatW; }
    uint32_t        SplatHeight() const { return m_splatH; }
    uint64_t        SplatBufferBytes() const { return static_cast<uint64_t>(m_splatW) * m_splatH * 16ull; }

    ID3D12DescriptorHeap*       DescriptorHeap() const { return m_heap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetSplatSRV() const;

    void  SetTimestampFrequency(uint64_t hz) { m_tsFreqHz = hz; }
    float GetLastSimMs() const { return m_lastSimMs; }
    float GetLastSplatMs() const { return m_lastSplatMs; }
    float GetLastTotalMs() const { return m_lastSimMs + m_lastSplatMs; }
    bool  HasValidTiming() const { return m_hasValidTiming; }

    void Shutdown();

private:
    bool HarvestTimestampTriplet(uint32_t queryStartIdx, float& outSimMs, float& outSplatMs) const;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(uint32_t slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(uint32_t slot) const;
    D3D12_CPU_DESCRIPTOR_HANDLE ClearCpuAt(uint32_t slot) const;

    bool                                         m_ready = false;
    ID3D12Device*                                m_dev   = nullptr;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;      // Shader-visible heap
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_clearHeap; // Non-shader-visible CPU heap for ClearUAVUint
    uint32_t                                     m_descInc = 0u;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>  m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_simPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  m_splatPso;

    Microsoft::WRL::ComPtr<ID3D12Resource>       m_particleBuf;
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_statsBuf;
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_splatBuf;
    uint32_t                                     m_particleCount = 0u;
    uint32_t                                     m_splatW        = 0u;
    uint32_t                                     m_splatH        = 0u;

    Microsoft::WRL::ComPtr<ID3D12Resource>       m_cbBuf;
    uint8_t*                                     m_cbMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap>      m_tsHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_tsReadback;
    uint64_t                                     m_tsFreqHz       = 0u;
    uint32_t                                     m_frameIdx       = 0u;
    float                                        m_lastSimMs      = 0.0f;
    float                                        m_lastSplatMs    = 0.0f;
    bool                                         m_hasValidTiming = false;
};

} // namespace raw::bridge
