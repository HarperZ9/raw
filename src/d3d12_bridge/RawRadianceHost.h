#pragma once
// RawRadianceHost.h: Native DX12 GPU 4-Cascade Penumbra-Exact Radiance Cascades Probe GI Compute Host.
// Allocates two 256x256 RGBA16F ping-pong cascade atlases and a 256x256 RGBA16F resolved diffuse GI
// irradiance texture, executing the full top-down c=3 -> 2 -> 1 -> 0 -> Resolve pipeline in a single
// command list pass with zero stochastic noise and exact penumbra merging.
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace raw::bridge
{

struct RadianceCascadeParams {
    float baseIntervalTexels = 3.5f;             // Cascade 0 range [0..3.5]; geometric 4x growth covers ~300 texels
    float maxTraceTexels     = 300.0f;           // Cascade 3 outer trace radius (texels)
    float opacityScale       = 2.5f;             // Beer-Lambert optical density per opaque occluder texel
    float emissiveScale      = 0.15f;            // Volumetric emission gain when scene alpha == 0
    float irradianceScale    = 1.0f;             // Linear output irradiance scale (1.0 = exact angular mean)
    float skyRadianceRGB[3]  = {0.0f, 0.0f, 0.0f}; // Optional far-field ambient sky radiance
};

// Optional explicit DXIL shader bytecode bundle (for runtime loading or standalone test harnesses).
struct RadianceShaderBlobs {
    D3D12_SHADER_BYTECODE traceMerge  = {};
    D3D12_SHADER_BYTECODE resolve     = {};
    D3D12_SHADER_BYTECODE screenPrep  = {}; // optional: required for ExecuteScreenGI()
    D3D12_SHADER_BYTECODE screenApply = {}; // optional: required for ExecuteScreenGI()

    bool Complete() const
    {
        return traceMerge.pShaderBytecode && traceMerge.BytecodeLength > 0 &&
               resolve.pShaderBytecode && resolve.BytecodeLength > 0;
    }

    bool ScreenComplete() const
    {
        return screenPrep.pShaderBytecode && screenPrep.BytecodeLength > 0 &&
               screenApply.pShaderBytecode && screenApply.BytecodeLength > 0;
    }
};

class RawRadianceHost {
public:
    static constexpr uint32_t kAtlasRes     = 256u;
    static constexpr uint32_t kCascadeCount = 4u;
    static constexpr uint32_t kCbRingCount  = 4u;

    RawRadianceHost() = default;
    ~RawRadianceHost();

    RawRadianceHost(const RawRadianceHost&) = delete;
    RawRadianceHost& operator=(const RawRadianceHost&) = delete;

    // Initializes using CMake-generated embedded DXIL headers (raw_rc_trace_merge_cs.h / raw_rc_resolve_cs.h).
    bool Init(ID3D12Device* dev, std::string* outErr = nullptr);

    // Initializes using caller-supplied DXIL bytecode blobs.
    bool InitWithBlobs(ID3D12Device* dev, const RadianceShaderBlobs& blobs, std::string* outErr = nullptr);

    bool Ready() const { return m_ready; }
    bool ScreenGIReady() const { return m_ready && m_psoScreenPrep != nullptr && m_psoScreenApply != nullptr; }

    // Runs full top-down 4-cascade GI solve: c=3 -> c=2 -> c=1 -> c=0 -> Resolve.
    // sceneEmissiveAlpha must be a 2D RGBA16F/RGBA32F/RGBA8 texture (rgb = HDR emission, a = occluder opacity).
    // If sceneState is D3D12_RESOURCE_STATE_COMMON, transitions cleanly to NON_PIXEL_SHADER_RESOURCE and back.
    void ComputeGI(ID3D12GraphicsCommandList* cmd,
                   ID3D12Resource* sceneEmissiveAlpha,
                   const RadianceCascadeParams& params,
                   D3D12_RESOURCE_STATES sceneState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // Full live screen-space Radiance Cascades GI pass for D3D12Bridge:
    //   1) cs_rc_screen_prep  : downsamples colorInOut + sceneDepth -> internal 256x256 m_emissiveAlpha
    //   2) ComputeGI          : 4-cascade c=3 -> 2 -> 1 -> 0 -> Resolve -> 256x256 m_irradiance
    //   3) cs_rc_screen_apply : depth-aware bilateral upsample & energy-conserving bounce onto colorInOut
    // Expects colorInOut & sceneDepth in COMMON; transitions colorInOut (COMMON -> SRV -> UAV -> COMMON)
    // and sceneDepth (COMMON -> NON_PIXEL_SHADER_RESOURCE -> COMMON).
    void ExecuteScreenGI(ID3D12GraphicsCommandList* cmd,
                         ID3D12Resource* colorInOut,
                         ID3D12Resource* sceneDepth,
                         uint32_t width,
                         uint32_t height,
                         float giStrength = 0.16f);

    ID3D12DescriptorHeap*       DescriptorHeap() const { return m_heap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE IrradianceSrv() const;
    D3D12_CPU_DESCRIPTOR_HANDLE IrradianceSrvCpu() const;
    ID3D12Resource*             IrradianceResource() const { return m_irradiance.Get(); }
    D3D12_RESOURCE_STATES       IrradianceResourceState() const { return m_irradiance.state; }
    uint32_t                    FrameIndex() const { return m_frameIdx; }

private:
    struct TrackedTex {
        Microsoft::WRL::ComPtr<ID3D12Resource> res;
        D3D12_RESOURCE_STATES                  state = D3D12_RESOURCE_STATE_COMMON;
        ID3D12Resource* Get() const { return res.Get(); }
    };

    void Transition(ID3D12GraphicsCommandList* cmd, TrackedTex& tex, D3D12_RESOURCE_STATES target);
    D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(uint32_t slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(uint32_t slot) const;

    bool                                        m_ready = false;
    ID3D12Device*                               m_dev = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t                                    m_descInc = 0;

    TrackedTex m_atlas[2];      // 256x256 RGBA16F ping-pong cascade atlases
    TrackedTex m_irradiance;    // 256x256 RGBA16F resolved diffuse GI irradiance
    TrackedTex m_emissiveAlpha; // 256x256 RGBA16F screen-prep extracted emitters + depth occluders

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoTraceMerge;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoResolve;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoScreenPrep;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoScreenApply;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_cbBuf;
    uint8_t*                               m_cbMapped = nullptr;
    uint32_t                               m_frameIdx = 0u;
    uint32_t                               m_screenFrameIdx = 0u;
};

} // namespace raw::bridge
