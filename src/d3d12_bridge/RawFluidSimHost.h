#pragma once
// RawFluidSimHost.h: Native DX12 GPU Compute Host for RAW's Real-Time 3D Eulerian Navier-Stokes
// Fire/Smoke + Radiance Cascade Solver (64x64x64) and 2D Toroidal Shallow-Water Hydrodynamics Solver (256x256).
//
// Exposes shader-visible SRVs (FireThermoSrv, FireRadianceSrv, WaterStateSrv, WaterNormalSrv) so
// LightingPass, LightFog, and RtWater can bind the live GPU fluid volumes directly.
// Original clean-room implementation, MIT License.

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <string>

namespace raw::bridge
{

struct FluidFireEmitter {
    float posW[3]       = {0.0f, 0.0f, 0.0f}; // verified world-space position
    float radius        = 0.0f;               // world units; <= 0 disables emitter
    float ignitionTempK = 2400.0f;            // 1800K..3400K flame ignition core temperature
    float fuelRate      = 2.2f;               // fuel injection rate (1/s)
    float jetSpeed      = 32.0f;              // upward/directed jet speed (world units/s)
    float curlStrength  = 14.0f;              // analytical curl-noise turbulence gain
    float jetDir[3]     = {0.0f, 0.0f, 1.0f}; // Z-up default jet direction
    float sootSeedRate  = 0.35f;              // direct ember/soot seed rate
};

struct FluidFireObstacle {
    float minW[3]   = {0.0f, 0.0f, 0.0f};
    float maxW[3]   = {0.0f, 0.0f, 0.0f};
    bool  active    = false;
    float roughness = 0.5f;
};

struct FluidFireParams {
    float    domainOriginW[3]  = {0.0f, 0.0f, 0.0f}; // world min corner of 64x64x64 voxel cube
    float    cellSize          = 4.0f;               // world units per voxel (64*4 = 256u cube)
    float    dt                = 1.0f / 60.0f;       // frame step (seconds)
    float    timeSec           = 0.0f;               // elapsed simulation time (seconds)
    float    windVelW[3]       = {6.0f, 2.0f, 0.0f}; // synoptic cross-wind (world units/s)
    float    ambientTempK      = 293.15f;            // ambient air temperature (K)
    float    vorticityEps      = 1.85f;              // vorticity confinement gain
    float    buoyancyAlpha     = 0.045f;             // thermal buoyancy acceleration per Kelvin
    float    sootWeightBeta    = 6.5f;               // cool dense soot downward gravity drag
    float    windDrag          = 0.85f;              // synoptic cross-wind drag coefficient
    float    arrheniusRateA    = 18.0f;              // Arrhenius pre-exponential rate factor
    float    activationTempTa  = 1600.0f;            // Arrhenius activation temperature (K)
    float    heatReleaseQ      = 2200.0f;            // combustion enthalpy release (K per unit fuel)
    float    sootYield         = 0.85f;              // soot generated per unit burned fuel
    float    radiativeCoolSig  = 1.6e-10f;           // Stefan-Boltzmann radiative cooling coefficient
    float    velDamping        = 0.25f;              // viscous air velocity damping (1/s)
    float    sootDamping       = 0.22f;              // smoke dissipation rate (1/s)
    float    thermalExpansion  = 1.65f;              // divergence expansion from active combustion
    float    floorZLocal       = 1.0f;               // solid floor voxel layer (0..64)
    float    ceilingZLocal     = 63.0f;              // optional solid ceiling voxel layer (0..64)
    bool     enableCeiling     = false;              // enable solid ceiling deflection (interiors/caves)
    float    scatterExtinction = 1.65f;              // Beer-Lambert soot extinction for Radiance Cascade
    uint32_t jacobiIterations  = 8u;                 // pressure relaxation iterations (even count recommended; min 2, max 32)
    uint32_t emitterCount      = 0u;                 // active emitters (0..16)
    uint32_t obstacleCount     = 0u;                 // active AABB obstacles (0..8)
    FluidFireEmitter  emitters[16]  = {};
    FluidFireObstacle obstacles[8]  = {};
};

struct FluidWaterTrail {
    float posW[3]     = {0.0f, 0.0f, 0.0f}; // actor world position
    float strength    = 0.0f;               // wake impulse strength (<= 0 disables)
    float velW[2]     = {0.0f, 0.0f};       // actor horizontal velocity (world units/s)
    float radius      = 18.0f;              // wake radius (world units)
    float splashScale = 1.0f;               // aerated foam splash multiplier
};

struct FluidWaterParams {
    float    camPosXY[2]       = {0.0f, 0.0f};   // camera world XY (toroidal center)
    float    cellSize          = 8.0f;           // world units per texel (256*8 = 2048u toroidal span)
    float    dt                = 1.0f / 60.0f;   // frame step (seconds)
    float    timeSec           = 0.0f;           // elapsed simulation time (seconds)
    float    gravityG          = 386.0f;         // gravity in Skyrim world units/s^2 (~9.81 m/s^2 * ~39.4 u/m)
    float    restDepthH0       = 48.0f;          // mean shallow-water rest depth (world units)
    float    velDragGamma      = 0.65f;          // velocity drag damping (1/s)
    float    viscosityNu       = 2.5f;           // lateral viscous smoothing (world units^2/s)
    float    windDirXY[2]      = {0.92f, 0.39f}; // dominant wind/swell direction
    float    swellAmp          = 2.2f;           // JONSWAP directional swell amplitude
    float    riverFlowSpeed    = 14.0f;          // background downstream flow speed (world units/s)
    float    foamDecayRate     = 0.35f;          // exponential foam persistence decay (1/s)
    float    crestFoamThresh   = 0.035f;         // swell steepness threshold for whitecap generation
    float    rainIntensity     = 0.0f;           // 0..1 active rainfall for raindrop rings
    float    shoreBreakStrength = 0.45f;         // shoreline wave-break energy
    float    waterPlaneZ       = 0.0f;           // live cell water elevation Z
    float    shorelineZBias    = 0.0f;           // optional shoreline shoaling bias
    uint32_t trailCount        = 0u;             // active player (0..15) + NPC (16..31) wake trails
    FluidWaterTrail trails[32] = {};
};

struct FluidWaterDomainInfo {
    float originXY[2] = {0.0f, 0.0f}; // world-space min XY corner of the 256x256 toroidal window
    float worldSpanU  = 0.0f;         // total span in world units (256 * cellSize)
    bool  active      = false;        // true once StepWater has executed
};

struct FluidFireDomainInfo {
    float    originXYZ[3] = {0.0f, 0.0f, 0.0f}; // world-space min XYZ corner of the 64x64x64 voxel cube
    float    worldSpanU   = 0.0f;               // total cube span in world units (64 * cellSize)
    bool     active       = false;              // true once StepFire has executed
    uint32_t emitterCount = 0u;                 // active verified world emitters in the last StepFire
};

// Optional explicit DXIL shader bytecode bundle (for runtime loading or standalone test harnesses).
struct FluidShaderBlobs {
    D3D12_SHADER_BYTECODE fireInject   = {};
    D3D12_SHADER_BYTECODE fireAdvect   = {};
    D3D12_SHADER_BYTECODE fireForces   = {};
    D3D12_SHADER_BYTECODE fireDiv      = {};
    D3D12_SHADER_BYTECODE firePress    = {};
    D3D12_SHADER_BYTECODE fireProj     = {};
    D3D12_SHADER_BYTECODE fireRad      = {};
    D3D12_SHADER_BYTECODE waterInject  = {};
    D3D12_SHADER_BYTECODE waterStep    = {};
    D3D12_SHADER_BYTECODE waterNormals = {};

    bool Complete() const
    {
        return fireInject.pShaderBytecode && fireAdvect.pShaderBytecode && fireForces.pShaderBytecode
            && fireDiv.pShaderBytecode && firePress.pShaderBytecode && fireProj.pShaderBytecode
            && fireRad.pShaderBytecode && waterInject.pShaderBytecode && waterStep.pShaderBytecode
            && waterNormals.pShaderBytecode;
    }
};

class RawFluidSimHost {
public:
    static constexpr uint32_t kFireGridRes     = 64u;
    static constexpr uint32_t kWaterGridRes    = 256u;
    static constexpr uint32_t kFireJacobiIters = 8u;
    static constexpr uint32_t kCbRingCount     = 4u;

    RawFluidSimHost() = default;
    ~RawFluidSimHost();

    RawFluidSimHost(const RawFluidSimHost&) = delete;
    RawFluidSimHost& operator=(const RawFluidSimHost&) = delete;

    // Initializes using CMake-generated embedded DXIL bytecode headers (cs_6_6 / cs_6_10).
    bool Init(ID3D12Device* dev, std::string* outErr = nullptr);

    // Initializes using caller-supplied DXIL shader bytecode blobs (e.g. from unit tests or runtime DXC).
    bool InitWithBlobs(ID3D12Device* dev, const FluidShaderBlobs& blobs, std::string* outErr = nullptr);

    bool Ready() const { return m_ready; }
    void Reset();

    // Runs 3D Eulerian Navier-Stokes + Radiance Cascade:
    // Inject -> Advect -> ForcesCombust -> Divergence -> Nx Pressure -> Project -> Radiance
    void StepFire(ID3D12GraphicsCommandList* cmd, const FluidFireParams& params);

    // Runs 256x256 Toroidal Shallow-Water Equations + Wake/Rain + Foam Advection + Analytical Normals:
    // Inject -> Step -> Normals
    void StepWater(ID3D12GraphicsCommandList* cmd, const FluidWaterParams& params);

    // Shader-visible SRV descriptor heap & handles for direct binding in LightingPass / LightFog / RtWater:
    ID3D12DescriptorHeap*       DescriptorHeap() const { return m_heap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE FireThermoSrv() const;
    D3D12_GPU_DESCRIPTOR_HANDLE FireRadianceSrv() const;
    D3D12_GPU_DESCRIPTOR_HANDLE WaterStateSrv() const;
    D3D12_GPU_DESCRIPTOR_HANDLE WaterNormalSrv() const;

    // CPU SRV handles (for copying into external bindless/pass descriptor heaps):
    D3D12_CPU_DESCRIPTOR_HANDLE FireThermoSrvCpu() const;
    D3D12_CPU_DESCRIPTOR_HANDLE FireRadianceSrvCpu() const;
    D3D12_CPU_DESCRIPTOR_HANDLE WaterStateSrvCpu() const;
    D3D12_CPU_DESCRIPTOR_HANDLE WaterNormalSrvCpu() const;

    // Underlying D3D12 resources (latest completed output banks):
    ID3D12Resource* FireThermoResource() const { return m_fireThermo[m_fireParity].Get(); }
    ID3D12Resource* FireRadianceResource() const { return m_fireRad[m_fireParity].Get(); }
    ID3D12Resource* FireVelocityResource() const { return m_fireVel[m_fireParity].Get(); }
    ID3D12Resource* WaterStateResource() const { return m_waterState[0].Get(); }
    ID3D12Resource* WaterNormalResource() const { return m_waterNormal.Get(); }

    // Live world-space domain bounds & current tracked D3D12 resource states:
    FluidFireDomainInfo   GetFireDomainInfo() const { return m_fireDomain; }
    FluidWaterDomainInfo  GetWaterDomainInfo() const { return m_waterDomain; }
    D3D12_RESOURCE_STATES FireResourceState() const { return m_fireThermo[m_fireParity].state; }
    D3D12_RESOURCE_STATES WaterResourceState() const { return m_waterState[0].state; }

    uint32_t FireFrameIndex() const { return m_fireFrameIdx; }
    uint32_t WaterFrameIndex() const { return m_waterFrameIdx; }

private:
    struct TrackedTex {
        Microsoft::WRL::ComPtr<ID3D12Resource> res;
        D3D12_RESOURCE_STATES                  state = D3D12_RESOURCE_STATE_COMMON;
        ID3D12Resource* Get() const { return res.Get(); }
    };

    void Transition(ID3D12GraphicsCommandList* cmd, TrackedTex& tex, D3D12_RESOURCE_STATES target);
    void UavBarrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res);
    D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(uint32_t slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(uint32_t slot) const;

    bool                                        m_ready = false;
    ID3D12Device*                               m_dev = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t                                    m_descInc = 0;

    // Fire 3D solver (64x64x64 ping-pong volumes)
    TrackedTex m_fireVel[2];
    TrackedTex m_fireThermo[2];
    TrackedTex m_firePress[2];
    TrackedTex m_fireRad[2];

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_fireRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoFireInject;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoFireAdvect;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoFireForces;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoFireDiv;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoFirePress;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoFireProj;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoFireRad;

    // Water 2D solver (256x256 toroidal state + analytical normals)
    TrackedTex m_waterState[2];
    TrackedTex m_waterNormal;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_waterRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoWaterInject;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoWaterStep;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoWaterNormals;

    // Ring-buffered constant buffers (4 frames in flight)
    Microsoft::WRL::ComPtr<ID3D12Resource> m_fireCbBuf;
    uint8_t*                               m_fireCbMapped = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_waterCbBuf;
    uint8_t*                               m_waterCbMapped = nullptr;

    uint32_t m_fireParity     = 0u;
    uint32_t m_fireFrameIdx   = 0u;
    uint32_t m_waterFrameIdx  = 0u;
    bool     m_fireResetNext  = true;
    bool     m_waterResetNext = true;
    float                m_prevWaterCamXY[2] = {0.0f, 0.0f};
    bool                 m_hasPrevWaterCam   = false;
    FluidFireDomainInfo  m_fireDomain{};
    FluidWaterDomainInfo m_waterDomain{};
};

} // namespace raw::bridge
