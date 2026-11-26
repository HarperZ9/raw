#pragma once
// NativeDx12Host.h: Native Direct3D 12 Adoption Seam for Creation Engine 2 (Starfield) & Clean-Room CE Runtime.
// Bypasses the D3D11->D3D12 translation layer (raw::tx) completely: adopts an existing native ID3D12Device,
// ID3D12CommandQueue, and native DX12 G-buffer/depth/motion/HDR resources directly into RAW's SM 6.9/6.10
// render graph (LightingPass, RtScene, RawMeshHost, Rawlss5Pass). Original work, MIT.

#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>

#include "D3D12Bridge.h"
#include "HardwareProfile.h"
#include "RawFramePacing.h"
#include "../common/raw_engine_profile.h"

namespace raw::bridge {

struct NativeDx12FrameInputs {
    ID3D12Resource* hdrColorIn   = nullptr;   // RGBA16F scene radiance before post/tonemap
    ID3D12Resource* hdrColorOut  = nullptr;   // RGBA16F reconstructed output (may alias hdrColorIn via barrier)
    ID3D12Resource* depth        = nullptr;   // R32F / D32F hardware depth
    ID3D12Resource* normal       = nullptr;   // world/view normals
    ID3D12Resource* albedo       = nullptr;   // base color / albedo manifold (feeds RAWLSS 7.0 demodulation)
    ID3D12Resource* material     = nullptr;   // roughness/metallic/specular layer
    ID3D12Resource* motion       = nullptr;   // RG16F velocity vectors
    ID3D12Resource* emissive     = nullptr;   // optional emissive buffer
    uint32_t        width        = 0;
    uint32_t        height       = 0;
    DXGI_FORMAT     colorFormat  = DXGI_FORMAT_R16G16B16A16_FLOAT;
    DXGI_FORMAT     normalFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
};

struct NativeDx12HostDesc {
    ID3D12Device*                device = nullptr;
    ID3D12CommandQueue*          directQueue = nullptr;
    ID3D12CommandQueue*          asyncComputeQueue = nullptr;   // optional: overlaps BLAS/TLAS & froxel scans
    ID3D12DescriptorHeap*        srvUavHeap = nullptr;          // CE2 shader-visible descriptor heap seam
    uint32_t                     agilitySdkVersion = 722;       // 620 = 1.620.x, 722 = 1.722.1-preview
    raw::versions::EngineProfile engineProfile{};
    HardwareProfile              hwProfile{};
};

enum class NativeDx12ContractError : uint32_t {
    Ok                             = 0,
    MultiplayerGuardBlocked        = 1,
    WrongEngineGenForNativeDx12    = 2,
    ShaderModelTooLow              = 3,
    AgilitySdkVersionTooOld        = 4,
    InvalidFrameDimensions         = 5,
    SwapchainFormatMismatch        = 6,
    NormalFormatMismatch           = 7,
    MissingRequiredGpuResource     = 8,
};

struct NativeDx12Capabilities {
    bool supportsSm69LongVectors   = false;
    bool supportsSm610Preview      = false;
    bool supportsAgility722Preview = false;
    bool supportsAsyncFroxelOverlap = false;
    bool usesReversedZ             = false;
    bool usesMinusMotionVectorSign = false;
    FramePacingPolicy pacingPolicy{};
};

struct NativeDx12ValidationResult {
    bool                    ok = false;
    NativeDx12ContractError error = NativeDx12ContractError::Ok;
    const char*             reason = "ok";
    NativeDx12Capabilities  caps{};
};

// Wave 72: Fail-closed cross-game ABI & Agility SDK 1.722.1 contract validator for CE2 Native DX12 hosts.
inline NativeDx12ValidationResult ValidateNativeDx12HostContract(
    const NativeDx12HostDesc& desc,
    const NativeDx12FrameInputs& frame,
    bool allowNullGpuPointersForOfflineTest = false) {
    NativeDx12ValidationResult res{};
    const auto& ep = desc.engineProfile;
    const auto& rc = ep.renderContract;
    if (ep.isMultiplayerOnline) {
        res.error = NativeDx12ContractError::MultiplayerGuardBlocked;
        res.reason = "multiplayer_online_title_blocked_fail_closed";
        return res;
    }
    if (!ep.usesNativeDx12 || !rc.usesNativeDx12Swapchain || ep.gen != raw::versions::EngineGen::CE2_Starfield_DX12) {
        res.error = NativeDx12ContractError::WrongEngineGenForNativeDx12;
        res.reason = "dx11_title_must_use_raw_tx_bridge_not_native_dx12_host";
        return res;
    }
    if (desc.hwProfile.shaderModel < rc.minShaderModel) {
        res.error = NativeDx12ContractError::ShaderModelTooLow;
        res.reason = "gpu_shader_model_below_sm66_minimum";
        return res;
    }
    if (desc.agilitySdkVersion < rc.minAgilitySdkVersion) {
        res.error = NativeDx12ContractError::AgilitySdkVersionTooOld;
        res.reason = "agility_sdk_below_1_620_minimum";
        return res;
    }
    if (frame.width < 64u || frame.height < 64u || frame.width > 16384u || frame.height > 16384u) {
        res.error = NativeDx12ContractError::InvalidFrameDimensions;
        res.reason = "frame_dimensions_out_of_bounds";
        return res;
    }
    if (static_cast<uint32_t>(frame.colorFormat) != rc.hdrDxgiFormat) {
        res.error = NativeDx12ContractError::SwapchainFormatMismatch;
        res.reason = "hdr_color_format_mismatch_against_engine_contract";
        return res;
    }
    if (static_cast<uint32_t>(frame.normalFormat) != rc.normalDxgiFormat) {
        res.error = NativeDx12ContractError::NormalFormatMismatch;
        res.reason = "gbuffer_normal_format_mismatch_against_engine_contract";
        return res;
    }
    if (!allowNullGpuPointersForOfflineTest) {
        if (!desc.device || !desc.directQueue || !frame.hdrColorIn || !frame.hdrColorOut || !frame.depth || !frame.motion) {
            res.error = NativeDx12ContractError::MissingRequiredGpuResource;
            res.reason = "missing_required_d3d12_device_queue_or_frame_resource";
            return res;
        }
    }
    res.ok = true;
    res.error = NativeDx12ContractError::Ok;
    res.reason = "ok";
    res.caps.supportsSm69LongVectors = (desc.hwProfile.shaderModel >= 0x69u);
    res.caps.supportsSm610Preview = (desc.hwProfile.shaderModel >= 0x6Au) && (desc.agilitySdkVersion >= 722u);
    res.caps.supportsAgility722Preview = (desc.agilitySdkVersion >= 722u);
    res.caps.supportsAsyncFroxelOverlap = (desc.asyncComputeQueue != nullptr) || allowNullGpuPointersForOfflineTest;
    res.caps.usesReversedZ = (rc.depthConvention == raw::versions::DepthConvention::ReversedZ_1to0);
    res.caps.usesMinusMotionVectorSign = (rc.mvSign == raw::versions::MotionVectorSign::MinusUv_PrevEqCurMinusMv);
    res.caps.pacingPolicy = ResolveFramePacingPolicy(ep);
    return res;
}

// Populates a HostLayer compatible with raw::bridge passes directly from a native DX12 host (zero D3D11 wrapper).
inline HostLayer MakeNativeDx12HostLayer(const NativeDx12HostDesc& desc) {
    HostLayer h{};
    h.device = desc.device;
    h.queue = desc.directQueue;
    h.native = nullptr;    // native DX12 resources need no ID3D11Resource* unwrapping
    h.prepare = nullptr;
    return h;
}

}  // namespace raw::bridge
