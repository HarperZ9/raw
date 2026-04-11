#pragma once
//=============================================================================
//  ClusteredLighting.h — GPU clustered forward+ lighting (Light Limit Fix)
//
//  Skyrim's vanilla forward renderer limits each draw call to 4 lights.
//  Community Shaders' Light Limit Fix raises this to 1024 via 16×16×32
//  frustum clusters + 2 compute passes. SkyrimBridge improves upon this:
//
//    - Adaptive cluster sizing (denser near camera, sparser far away)
//    - Temporal light caching (skip rebuild if light set hasn't changed)
//    - Hi-Z shadow-aware culling (use our Hi-Z pyramid)
//    - Higher light count: 2048 via BVH-backed structured buffer
//    - Per-cluster light index list in StructuredBuffer (not flat array)
//
//  Architecture:
//    1. Collect lights from game scene graph (NiPointLight, NiSpotLight)
//    2. Upload light data to GPU StructuredBuffer
//    3. ClusterBuildCS: assign screen-space clusters to frustum slices
//    4. ClusterCullCS: cull lights per cluster using sphere/frustum test
//    5. Inject light buffer + cluster data as SRVs for game pixel shaders
//    6. Replace BSLightingShader PS with modified version that reads clusters
//
//  Requires: d3d11 proxy (WrappedContext for shader replacement) OR
//            standalone compute dispatch + SRV injection
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <d3d11.h>
#include <DirectXMath.h>
#include <cstdint>
#include <vector>
#include <atomic>

namespace SB
{

// ── Constants ────────────────────────────────────────────────────────────

static constexpr uint32_t kMaxLights        = 2048;
static constexpr uint32_t kClusterGridX     = 16;
static constexpr uint32_t kClusterGridY     = 16;
static constexpr uint32_t kClusterGridZ     = 32;
static constexpr uint32_t kTotalClusters    = kClusterGridX * kClusterGridY * kClusterGridZ;
static constexpr uint32_t kMaxLightsPerCluster = 128;

// SRV/UAV slots for cluster data injection
static constexpr uint32_t kLightBufferSRVSlot  = 20;  // t20: StructuredBuffer<LightData>
static constexpr uint32_t kClusterGridSRVSlot  = 21;  // t21: StructuredBuffer<ClusterInfo>
static constexpr uint32_t kLightIndexSRVSlot   = 22;  // t22: Buffer<uint> light index list

// ── GPU-side structures (must match HLSL) ────────────────────────────────

struct alignas(16) GPULightData
{
    DirectX::XMFLOAT4 positionAndRadius;  // xyz = world pos, w = radius
    DirectX::XMFLOAT4 colorAndIntensity;  // xyz = linear RGB, w = intensity
    DirectX::XMFLOAT4 directionAndAngle;  // xyz = spot dir (0 for point), w = cos(halfAngle)
    uint32_t           flags;              // bit 0: isSpot, bit 1: castShadow
    uint32_t           pad[3];
};

struct alignas(16) ClusterInfo
{
    uint32_t offset;     // Start index in light index list
    uint32_t count;      // Number of lights in this cluster
    uint32_t pad[2];
};

struct alignas(16) ClusterBuildCB
{
    DirectX::XMFLOAT4X4 viewMatrix;
    DirectX::XMFLOAT4X4 projMatrix;
    DirectX::XMFLOAT4X4 invProjMatrix;
    float   nearZ;
    float   farZ;
    float   screenWidth;
    float   screenHeight;
    uint32_t lightCount;
    uint32_t clusterGridX;
    uint32_t clusterGridY;
    uint32_t clusterGridZ;
};

// ── CPU-side light data (collected from game scene graph) ────────────────

struct SceneLight
{
    DirectX::XMFLOAT3 position;
    float              radius;
    DirectX::XMFLOAT3 color;       // Linear RGB
    float              intensity;
    DirectX::XMFLOAT3 direction;   // For spotlights
    float              spotAngle;   // Half-angle in radians (0 = point light)
    bool               castsShadow;
};

// ── Main class ───────────────────────────────────────────────────────────

class ClusteredLighting
{
public:
    static ClusteredLighting& Get()
    {
        static ClusteredLighting inst;
        return inst;
    }

    // Initialize GPU resources (buffers, compute shaders, SRVs)
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    // Shutdown and release all resources
    void Shutdown();

    // Is the system initialized and ready?
    bool IsInitialized() const { return m_initialized; }

    // Is the system enabled (may be disabled by feature negotiation)
    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool enabled) { m_enabled = enabled; }

    // ── Per-frame pipeline ──────────────────────────────────────────────

    // Step 1: Collect lights from the game scene graph.
    // Call this during tracker updates (main thread, game state valid).
    void CollectLights();

    // Step 2: Upload light data + dispatch compute passes.
    // Call this from the render thread (D3D11 context available).
    // Needs view/proj matrices from CameraTracker.
    void Dispatch(ID3D11DeviceContext* ctx,
                  const DirectX::XMFLOAT4X4& view,
                  const DirectX::XMFLOAT4X4& proj,
                  float nearZ, float farZ,
                  uint32_t screenWidth, uint32_t screenHeight);

    // ── Query ───────────────────────────────────────────────────────────

    uint32_t GetVisibleLightCount() const { return m_visibleLightCount; }
    uint32_t GetClusteredLightCount() const { return m_clusteredLightCount; }

    // SRVs for injection into game pixel shaders
    ID3D11ShaderResourceView* GetLightBufferSRV() const { return m_lightBufferSRV; }
    ID3D11ShaderResourceView* GetClusterGridSRV() const { return m_clusterGridSRV; }
    ID3D11ShaderResourceView* GetLightIndexSRV() const { return m_lightIndexSRV; }

    // Stats
    uint32_t GetTotalLightsCollected() const { return static_cast<uint32_t>(m_sceneLights.size()); }
    bool     IsTemporalCacheActive() const { return m_temporalCacheHit; }

private:
    ClusteredLighting() = default;

    bool CompileComputeShaders(ID3D11Device* device);
    bool CreateBuffers(ID3D11Device* device);
    void UploadLightData(ID3D11DeviceContext* ctx);
    bool ShouldRebuildClusters() const;

    // ── State ───────────────────────────────────────────────────────────
    bool m_initialized = false;
    bool m_enabled     = true;

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // ── Compute shaders ─────────────────────────────────────────────────
    ID3D11ComputeShader* m_clusterBuildCS = nullptr;
    ID3D11ComputeShader* m_clusterCullCS  = nullptr;

    // ── GPU buffers ─────────────────────────────────────────────────────
    // Light data (StructuredBuffer<GPULightData>)
    ID3D11Buffer*             m_lightBuffer    = nullptr;
    ID3D11ShaderResourceView* m_lightBufferSRV = nullptr;
    ID3D11UnorderedAccessView* m_lightBufferUAV = nullptr;  // for CS write

    // Cluster grid (StructuredBuffer<ClusterInfo>)
    ID3D11Buffer*             m_clusterGridBuffer = nullptr;
    ID3D11ShaderResourceView* m_clusterGridSRV    = nullptr;
    ID3D11UnorderedAccessView* m_clusterGridUAV   = nullptr;

    // Light index list (Buffer<uint>)
    ID3D11Buffer*             m_lightIndexBuffer = nullptr;
    ID3D11ShaderResourceView* m_lightIndexSRV    = nullptr;
    ID3D11UnorderedAccessView* m_lightIndexUAV   = nullptr;

    // Constants CB
    ID3D11Buffer* m_constantsCB = nullptr;

    // Counter buffer for atomic index allocation
    ID3D11Buffer*             m_counterBuffer    = nullptr;
    ID3D11UnorderedAccessView* m_counterUAV      = nullptr;

    // ── CPU-side data ───────────────────────────────────────────────────
    std::vector<SceneLight> m_sceneLights;
    uint32_t m_visibleLightCount   = 0;
    uint32_t m_clusteredLightCount = 0;

    // ── Temporal caching ────────────────────────────────────────────────
    // Skip cluster rebuild if the light set hasn't changed
    uint64_t m_lastLightHash     = 0;
    uint32_t m_lastLightCount    = 0;
    bool     m_temporalCacheHit  = false;
    uint32_t m_framesSinceRebuild = 0;
    static constexpr uint32_t kForceRebuildInterval = 4;  // rebuild every N frames regardless
};

} // namespace SB
