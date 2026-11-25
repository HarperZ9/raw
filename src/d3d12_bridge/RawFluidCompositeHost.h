#pragma once
// RawFluidCompositeHost.h: Native DX12 Compute Compositor for RAW's live 64x64x64 3D Eulerian
// Navier-Stokes Fire/Smoke/Radiance volumes onto HDR ColorOut (RGBA16F) with SceneDepth occlusion.
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace raw::bridge
{

struct FluidCompositeParams {
    float    clipToWorld[16] = {};
    float    camPosW[3]      = {};
    float    simTimeSec      = 0.0f;
    float    domainMinW[3]   = {};
    float    domainSpanW     = 256.0f;
    float    resScale[2]     = {1.0f, 1.0f};
    float    sunDir[3]       = {0.4f, 0.3f, 0.86f};
    float    sunIntensity    = 1.0f;
    float    sunCol[3]       = {1.0f, 0.95f, 0.88f};
    float    ambientFill     = 0.5f;
    uint32_t width           = 0u;
    uint32_t height          = 0u;
    uint32_t activeEmitters  = 0u;
    bool     isInterior      = false;
};

class RawFluidCompositeHost {
public:
    static constexpr uint32_t kRingCount = 4u;

    RawFluidCompositeHost() = default;
    ~RawFluidCompositeHost();
    RawFluidCompositeHost(const RawFluidCompositeHost&) = delete;
    RawFluidCompositeHost& operator=(const RawFluidCompositeHost&) = delete;

    // Initializes using CMake-generated embedded DXIL header (raw_fluid_fire_composite_cs.h).
    bool Init(ID3D12Device* dev, std::string* outErr = nullptr);

    // Initializes using caller-supplied DXIL bytecode (for standalone tests or hot-reload).
    bool InitWithBlob(ID3D12Device* dev, D3D12_SHADER_BYTECODE csBlob, std::string* outErr = nullptr);

    void Shutdown();
    bool Ready() const { return m_ready; }

    // Records the volumetric fire/smoke composite into an already-open command list.
    // Expects colorOut in COMMON (transitions COMMON -> UAV -> COMMON), depthIn in COMMON
    // (transitions COMMON -> NON_PIXEL_SRV -> COMMON), and fireThermo/fireRadiance already in SRV state.
    bool RecordComposite(ID3D12GraphicsCommandList* cmd,
                         ID3D12Resource* colorOut,
                         ID3D12Resource* depthIn,
                         ID3D12Resource* fireThermo,
                         ID3D12Resource* fireRadiance,
                         const FluidCompositeParams& params);

    // Self-contained per-frame execution on a direct queue with internal ring allocators & fence.
    bool Composite(ID3D12CommandQueue* queue,
                   ID3D12Resource* colorOut,
                   ID3D12Resource* depthIn,
                   ID3D12Resource* fireThermo,
                   ID3D12Resource* fireRadiance,
                   const FluidCompositeParams& params);

private:
    bool                                        m_ready = false;
    Microsoft::WRL::ComPtr<ID3D12Device>        m_dev;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    UINT                                        m_descInc = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource>      m_cbBuf;
    uint8_t*                                    m_cbMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    m_alloc[kRingCount];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_list;
    Microsoft::WRL::ComPtr<ID3D12Fence>               m_fence;
    HANDLE                                            m_event = nullptr;
    uint64_t                                          m_fenceVal = 0;
    uint64_t                                          m_slotVal[kRingCount] = {};
    uint32_t                                          m_frameIdx = 0;
};

} // namespace raw::bridge
