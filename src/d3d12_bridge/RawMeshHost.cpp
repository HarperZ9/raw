// RawMeshHost.cpp: Hardware DX12 Amplification + Mesh Shader (as_6_6 / ms_6_6) PSO,
// RawSubdiv PN-Triangle compute host, and live RawTerrainWorld.hlsl multi-biome graphics renderer.
// Original work, MIT.
#include "RawMeshHost.h"
#include "raw_mesh_as.h"     // kDxil_raw_mesh_as (dxc, as_6_6)
#include "raw_mesh_ms.h"     // kDxil_raw_mesh_ms (dxc, ms_6_6)
#include "raw_subdiv_cs.h"   // kDxil_raw_subdiv_cs (dxc, cs_6_6)
#include "raw_terrain_as.h"  // kDxil_raw_terrain_as (dxc, as_6_6)
#include "raw_terrain_ms.h"  // kDxil_raw_terrain_ms (dxc, ms_6_6)
#include "raw_terrain_vs.h"  // kDxil_raw_terrain_vs (dxc, vs_6_6)
#include "raw_terrain_ps.h"  // kDxil_raw_terrain_ps (dxc, ps_6_6)

#include <algorithm>
#include <cmath>
#include <cstring>

namespace raw::bridge
{
namespace
{
using Microsoft::WRL::ComPtr;

template <D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type, typename T>
struct alignas(void*) Subobject {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Type;
    T                                   data{};
};

struct MeshPsoStream {
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*> root;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, D3D12_SHADER_BYTECODE>            as;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, D3D12_SHADER_BYTECODE>            ms;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC>    rast;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, D3D12_DEPTH_STENCIL_DESC> ds;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> rtv;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT>    dsv;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC>        sample;
};

struct TerrainMeshPsoStream {
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*> root;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, D3D12_SHADER_BYTECODE>            as;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, D3D12_SHADER_BYTECODE>            ms;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE>            ps;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, D3D12_BLEND_DESC>              blend;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC>    rast;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, D3D12_DEPTH_STENCIL_DESC> ds;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> rtv;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT>    dsv;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC>        sample;
    Subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT>                    sampleMask;
};

bool CreateUploadBuffer(ID3D12Device* dev, const void* data, size_t bytes, ComPtr<ID3D12Resource>& out)
{
    if (!dev || bytes == 0) return false;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (bytes + 255) & ~size_t(255);
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&out))))
        return false;
    if (data) {
        void* p = nullptr;
        D3D12_RANGE r{0, 0};
        if (FAILED(out->Map(0, &r, &p))) return false;
        std::memcpy(p, data, bytes);
        out->Unmap(0, nullptr);
    }
    return true;
}

// Extract normalized Gribb-Hartmann frustum planes from row-major D3D clip matrix (z in [0, w]).
void ExtractFrustumPlanes(const float m[16], float outPlanes[6][4])
{
    auto setPlane = [&](int idx, float a, float b, float c, float d) {
        const float len = std::sqrt(a * a + b * b + c * c);
        const float inv = len > 1.0e-7f ? (1.0f / len) : 1.0f;
        outPlanes[idx][0] = a * inv;
        outPlanes[idx][1] = b * inv;
        outPlanes[idx][2] = c * inv;
        outPlanes[idx][3] = d * inv;
    };
    // Row vectors: r0 = m[0..3], r1 = m[4..7], r2 = m[8..11], r3 = m[12..15]
    setPlane(0, m[12] + m[0], m[13] + m[1], m[14] + m[2], m[15] + m[3]); // Left:   w + x >= 0
    setPlane(1, m[12] - m[0], m[13] - m[1], m[14] - m[2], m[15] - m[3]); // Right:  w - x >= 0
    setPlane(2, m[12] + m[4], m[13] + m[5], m[14] + m[6], m[15] + m[7]); // Bottom: w + y >= 0
    setPlane(3, m[12] - m[4], m[13] - m[5], m[14] - m[6], m[15] - m[7]); // Top:    w - y >= 0
    setPlane(4, m[8],         m[9],         m[10],        m[11]);        // Near:   z >= 0
    setPlane(5, m[12] - m[8], m[13] - m[9], m[14] - m[10], m[15] - m[11]); // Far:  w - z >= 0
}

bool SphereInFrustum(const float planes[6][4], const float relCenter[3], float radius)
{
    for (int i = 0; i < 6; ++i) {
        const float dist = planes[i][0] * relCenter[0] + planes[i][1] * relCenter[1] +
                           planes[i][2] * relCenter[2] + planes[i][3];
        if (dist < -radius) return false;
    }
    return true;
}

D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    return b;
}

D3D12_BLEND_DESC OpaqueBlendDesc()
{
    D3D12_BLEND_DESC b{};
    b.AlphaToCoverageEnable = FALSE;
    b.IndependentBlendEnable = FALSE;
    b.RenderTarget[0].BlendEnable = FALSE;
    b.RenderTarget[0].LogicOpEnable = FALSE;
    b.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    return b;
}

D3D12_RASTERIZER_DESC TerrainRasterDesc()
{
    D3D12_RASTERIZER_DESC r{};
    r.FillMode = D3D12_FILL_MODE_SOLID;
    // Two-sided rendering guarantees immunity to projection handedness and renders 3D conifer fronds from above & below.
    r.CullMode = D3D12_CULL_MODE_NONE;
    r.FrontCounterClockwise = FALSE;
    r.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    r.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    r.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    r.DepthClipEnable = TRUE;
    r.MultisampleEnable = FALSE;
    r.AntialiasedLineEnable = FALSE;
    r.ForcedSampleCount = 0;
    r.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    return r;
}

D3D12_DEPTH_STENCIL_DESC TerrainDepthDesc()
{
    D3D12_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    // Skyrim uses standard-Z (0 = near, 1 = far), so closer fragments have smaller Z.
    ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ds.StencilEnable = FALSE;
    return ds;
}

} // namespace

RawMeshHost::~RawMeshHost()
{
    Shutdown();
}

void RawMeshHost::Shutdown()
{
    if (m_terrainFence && m_terrainEvent && m_terrainFenceVal > 0) {
        if (m_terrainFence->GetCompletedValue() < m_terrainFenceVal) {
            m_terrainFence->SetEventOnCompletion(m_terrainFenceVal, m_terrainEvent);
            WaitForSingleObject(m_terrainEvent, 3000);
        }
    }
    m_retiredChunks.clear();
    m_terrainDepth.Reset();
    m_dummyAtlas.Reset();
    m_terrainRtvHeap.Reset();
    m_terrainDsvHeap.Reset();
    m_terrainSrvHeap.Reset();
    for (uint32_t i = 0; i < kTerrainRing; ++i) {
        m_terrainLightCb[i].Reset();
        m_terrainLightCbMap[i] = nullptr;
        m_terrainDrawCb[i].Reset();
        m_terrainDrawCbMap[i] = nullptr;
        m_terrainAlloc[i].Reset();
        m_terrainSlotVal[i] = 0;
    }
    m_terrainList.Reset();
    m_terrainMsPso.Reset();
    m_terrainVsPso.Reset();
    m_terrainRoot.Reset();
    m_terrainFence.Reset();
    if (m_terrainEvent) {
        CloseHandle(m_terrainEvent);
        m_terrainEvent = nullptr;
    }
    m_subdivCbBuf.Reset();
    m_subdivCbMapped = nullptr;
    m_subdivPso.Reset();
    m_subdivRoot.Reset();
    m_cbBuf.Reset();
    m_cbMapped = nullptr;
    m_pso.Reset();
    m_root.Reset();
    m_dev2.Reset();
    m_dev.Reset();
    m_ready = false;
    m_terrainReady = false;
    m_depthW = 0;
    m_depthH = 0;
    m_terrainFenceVal = 0;
    m_terrainSlot = 0;
}

bool RawMeshHost::Init(ID3D12Device* dev, std::string* outErr)
{
    Shutdown();
    if (!dev) { if (outErr) *outErr = "null D3D12 device"; return false; }
    m_dev = dev;
    dev->QueryInterface(IID_PPV_ARGS(&m_dev2));

    D3D12_FEATURE_DATA_D3D12_OPTIONS7 o7{};
    const bool hasMeshShaders = m_dev2 &&
        SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &o7, sizeof o7)) &&
        o7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1;

    D3D12_RANGE zeroRange{0, 0};

    // 1. Optional Hardware AS+MS RawMesh.hlsl pipeline (requires Mesh Shader Tier 1)
    if (hasMeshShaders) {
        D3D12_ROOT_PARAMETER rp[4]{};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; rp[0].Descriptor = {0, 0}; rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; rp[1].Descriptor = {0, 0}; rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; rp[2].Descriptor = {1, 0}; rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_MESH;
        rp[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; rp[3].Descriptor = {2, 0}; rp[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_MESH;
        D3D12_ROOT_SIGNATURE_DESC rsd{4, rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> sig, errBlob;
        if (SUCCEEDED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &errBlob)) &&
            SUCCEEDED(m_dev2->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
            MeshPsoStream st{};
            st.root.data = m_root.Get();
            st.as.data = {kDxil_raw_mesh_as, sizeof kDxil_raw_mesh_as};
            st.ms.data = {kDxil_raw_mesh_ms, sizeof kDxil_raw_mesh_ms};
            st.rast.data.FillMode = D3D12_FILL_MODE_SOLID;
            st.rast.data.CullMode = D3D12_CULL_MODE_BACK;
            st.rast.data.DepthClipEnable = TRUE;
            st.ds.data.DepthEnable = TRUE;
            st.ds.data.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            st.ds.data.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            st.rtv.data.NumRenderTargets = 0;
            st.dsv.data = DXGI_FORMAT_D32_FLOAT;
            st.sample.data = {1, 0};
            D3D12_PIPELINE_STATE_STREAM_DESC psd{sizeof st, &st};
            if (SUCCEEDED(m_dev2->CreatePipelineState(&psd, IID_PPV_ARGS(&m_pso))) &&
                CreateUploadBuffer(dev, nullptr, sizeof(RawMeshFrameCB), m_cbBuf) &&
                SUCCEEDED(m_cbBuf->Map(0, &zeroRange, &m_cbMapped))) {
                m_ready = true;
            }
        }
    }

    // 2. Optional RawSubdiv.hlsl (cs_6_6) 1->4 curved PN-Triangle compute pipeline (all DX12 SM 6.6 GPUs)
    {
        D3D12_ROOT_PARAMETER sp[5]{};
        sp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; sp[0].Descriptor = {0, 0}; sp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        sp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; sp[1].Descriptor = {0, 0}; sp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        sp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; sp[2].Descriptor = {1, 0}; sp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        sp[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; sp[3].Descriptor = {0, 0}; sp[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        sp[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; sp[4].Descriptor = {1, 0}; sp[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC srsd{5, sp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> sSig, sErr;
        if (SUCCEEDED(D3D12SerializeRootSignature(&srsd, D3D_ROOT_SIGNATURE_VERSION_1, &sSig, &sErr)) &&
            SUCCEEDED(dev->CreateRootSignature(0, sSig->GetBufferPointer(), sSig->GetBufferSize(), IID_PPV_ARGS(&m_subdivRoot)))) {
            D3D12_COMPUTE_PIPELINE_STATE_DESC cpd{};
            cpd.pRootSignature = m_subdivRoot.Get();
            cpd.CS = {kDxil_raw_subdiv_cs, sizeof kDxil_raw_subdiv_cs};
            if (SUCCEEDED(dev->CreateComputePipelineState(&cpd, IID_PPV_ARGS(&m_subdivPso))) &&
                CreateUploadBuffer(dev, nullptr, sizeof(RawSubdivCB), m_subdivCbBuf)) {
                m_subdivCbBuf->Map(0, &zeroRange, &m_subdivCbMapped);
            }
        }
    }

    // 3. Live RawTerrainWorld.hlsl Graphics Renderer:
    //    Root signature: b0 (LightUniform CBV), b1 (TerrainDrawCB CBV),
    //                    t0 (TerrainMeshlets SRV), t1 (TerrainVertices SRV), t2 (TerrainIndices SRV),
    //                    DescriptorTable(t3 SceneDepth + t4 ShadowAtlas, PS visibility).
    {
        D3D12_DESCRIPTOR_RANGE texRange{};
        texRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        texRange.NumDescriptors = 2; // t3 = SceneDepth, t4 = ShadowAtlas
        texRange.BaseShaderRegister = 3;
        texRange.RegisterSpace = 0;
        texRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER tp[6]{};
        tp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; tp[0].Descriptor = {0, 0}; tp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        tp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; tp[1].Descriptor = {1, 0}; tp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        tp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; tp[2].Descriptor = {0, 0}; tp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        tp[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; tp[3].Descriptor = {1, 0}; tp[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        tp[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; tp[4].Descriptor = {2, 0}; tp[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        tp[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        tp[5].DescriptorTable = {1, &texRange};
        tp[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC trsd{6, tp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> tSig, tErr;
        if (SUCCEEDED(D3D12SerializeRootSignature(&trsd, D3D_ROOT_SIGNATURE_VERSION_1, &tSig, &tErr)) &&
            SUCCEEDED(dev->CreateRootSignature(0, tSig->GetBufferPointer(), tSig->GetBufferSize(), IID_PPV_ARGS(&m_terrainRoot)))) {

            // Primary AS+MS+PS pipeline (hardware DX12 Mesh Shader GPUs)
            if (hasMeshShaders) {
                TerrainMeshPsoStream tst{};
                tst.root.data = m_terrainRoot.Get();
                tst.as.data = {kDxil_raw_terrain_as, sizeof kDxil_raw_terrain_as};
                tst.ms.data = {kDxil_raw_terrain_ms, sizeof kDxil_raw_terrain_ms};
                tst.ps.data = {kDxil_raw_terrain_ps, sizeof kDxil_raw_terrain_ps};
                tst.blend.data = OpaqueBlendDesc();
                tst.rast.data = TerrainRasterDesc();
                tst.ds.data = TerrainDepthDesc();
                tst.rtv.data.NumRenderTargets = 1;
                tst.rtv.data.RTFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
                tst.dsv.data = DXGI_FORMAT_D32_FLOAT;
                tst.sample.data = {1, 0};
                tst.sampleMask.data = UINT_MAX;
                D3D12_PIPELINE_STATE_STREAM_DESC tpsd{sizeof tst, &tst};
                m_dev2->CreatePipelineState(&tpsd, IID_PPV_ARGS(&m_terrainMsPso));
            }

            // Fallback VS+PS pipeline (non-mesh-shader GPUs & WARP)
            {
                D3D12_GRAPHICS_PIPELINE_STATE_DESC gps{};
                gps.pRootSignature = m_terrainRoot.Get();
                gps.VS = {kDxil_raw_terrain_vs, sizeof kDxil_raw_terrain_vs};
                gps.PS = {kDxil_raw_terrain_ps, sizeof kDxil_raw_terrain_ps};
                gps.BlendState = OpaqueBlendDesc();
                gps.SampleMask = UINT_MAX;
                gps.RasterizerState = TerrainRasterDesc();
                gps.DepthStencilState = TerrainDepthDesc();
                gps.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                gps.NumRenderTargets = 1;
                gps.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
                gps.DSVFormat = DXGI_FORMAT_D32_FLOAT;
                gps.SampleDesc = {1, 0};
                dev->CreateGraphicsPipelineState(&gps, IID_PPV_ARGS(&m_terrainVsPso));
            }

            bool infraOk = (m_terrainMsPso || m_terrainVsPso);
            for (uint32_t i = 0; i < kTerrainRing && infraOk; ++i) {
                infraOk = SUCCEEDED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_terrainAlloc[i])))
                       && CreateUploadBuffer(dev, nullptr, 65536u, m_terrainLightCb[i])
                       && SUCCEEDED(m_terrainLightCb[i]->Map(0, &zeroRange, &m_terrainLightCbMap[i]))
                       && CreateUploadBuffer(dev, nullptr, size_t(kMaxTerrainDrawsPerFrame) * sizeof(TerrainDrawCB), m_terrainDrawCb[i])
                       && SUCCEEDED(m_terrainDrawCb[i]->Map(0, &zeroRange, &m_terrainDrawCbMap[i]));
            }
            if (infraOk) {
                infraOk = SUCCEEDED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_terrainAlloc[0].Get(), nullptr, IID_PPV_ARGS(&m_terrainList)))
                       && SUCCEEDED(m_terrainList->Close())
                       && SUCCEEDED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_terrainFence)));
            }
            if (infraOk) {
                m_terrainEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                D3D12_DESCRIPTOR_HEAP_DESC rtvD{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
                D3D12_DESCRIPTOR_HEAP_DESC dsvD{D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
                D3D12_DESCRIPTOR_HEAP_DESC srvD{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kTerrainRing * 2u, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
                infraOk = (m_terrainEvent != nullptr)
                       && SUCCEEDED(dev->CreateDescriptorHeap(&rtvD, IID_PPV_ARGS(&m_terrainRtvHeap)))
                       && SUCCEEDED(dev->CreateDescriptorHeap(&dsvD, IID_PPV_ARGS(&m_terrainDsvHeap)))
                       && SUCCEEDED(dev->CreateDescriptorHeap(&srvD, IID_PPV_ARGS(&m_terrainSrvHeap)));
                m_srvStep = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
            // 1x1 R32_FLOAT dummy shadow atlas fallback
            if (infraOk) {
                D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
                D3D12_RESOURCE_DESC td{};
                td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                td.Width = 1; td.Height = 1; td.DepthOrArraySize = 1; td.MipLevels = 1;
                td.Format = DXGI_FORMAT_R32_FLOAT;
                td.SampleDesc.Count = 1;
                infraOk = SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
                                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                                 nullptr, IID_PPV_ARGS(&m_dummyAtlas)));
            }
            m_terrainReady = infraOk;
        }
    }

    if (!m_ready && !m_terrainReady) {
        if (outErr) *outErr = hasMeshShaders ? "RAW-MESH & Terrain pipeline creation failed"
                                             : "GPU lacks Mesh Shaders and VS Terrain fallback failed";
        return false;
    }
    if (!m_ready && outErr) {
        *outErr = "Mesh Shader Tier 1 absent (VS+PS Terrain fallback READY)";
    }
    return m_ready || m_terrainReady;
}

bool RawMeshHost::EnsureTerrainDepth(UINT width, UINT height)
{
    if (!m_dev || width == 0 || height == 0) return false;
    if (m_terrainDepth && m_depthW == width && m_depthH == height) return true;
    // Wait for any in-flight terrain work before replacing depth target.
    if (m_terrainFence && m_terrainEvent && m_terrainFence->GetCompletedValue() < m_terrainFenceVal) {
        m_terrainFence->SetEventOnCompletion(m_terrainFenceVal, m_terrainEvent);
        WaitForSingleObject(m_terrainEvent, 3000);
    }
    m_terrainDepth.Reset();
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC dd{};
    dd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    dd.Width = width;
    dd.Height = height;
    dd.DepthOrArraySize = 1;
    dd.MipLevels = 1;
    dd.Format = DXGI_FORMAT_D32_FLOAT;
    dd.SampleDesc.Count = 1;
    dd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = DXGI_FORMAT_D32_FLOAT;
    cv.DepthStencil.Depth = 1.0f;
    cv.DepthStencil.Stencil = 0;
    if (FAILED(m_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &dd,
                                              D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv,
                                              IID_PPV_ARGS(&m_terrainDepth)))) {
        return false;
    }
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    m_dev->CreateDepthStencilView(m_terrainDepth.Get(), &dsv, m_terrainDsvHeap->GetCPUDescriptorHandleForHeapStart());
    m_depthW = width;
    m_depthH = height;
    return true;
}

void RawMeshHost::CollectRetiredChunks()
{
    if (m_retiredChunks.empty() || !m_terrainFence) return;
    const uint64_t done = m_terrainFence->GetCompletedValue();
    m_retiredChunks.erase(
        std::remove_if(m_retiredChunks.begin(), m_retiredChunks.end(),
                       [done](const auto& item) { return item.first <= done; }),
        m_retiredChunks.end());
}

void RawMeshHost::DeferRelease(TerrainGpuChunk&& chunk)
{
    if (!chunk.meshletBuf && !chunk.vertexBuf && !chunk.meshletIdxBuf && !chunk.flatIdxBuf) return;
    m_retiredChunks.emplace_back(m_terrainFenceVal, std::move(chunk));
}

bool RawMeshHost::UploadAsset(const RawMeshAsset& src, RawMeshGpuAsset* outGpu, std::string* outErr)
{
    if (!m_ready || !m_dev || !outGpu || src.meshlets.empty() || src.vertices.empty() || src.indices.empty()) {
        if (outErr) *outErr = "RawMeshHost not ready or empty asset";
        return false;
    }
    outGpu->name = src.name;
    outGpu->meshletCount = static_cast<uint32_t>(src.meshlets.size());
    const bool ok = CreateUploadBuffer(m_dev.Get(), src.meshlets.data(), src.meshlets.size() * sizeof(RawMeshletDesc), outGpu->meshletBuf)
                 && CreateUploadBuffer(m_dev.Get(), src.vertices.data(), src.vertices.size() * sizeof(RawMeshVertex), outGpu->vertexBuf)
                 && CreateUploadBuffer(m_dev.Get(), src.indices.data(), src.indices.size() * sizeof(uint32_t), outGpu->indexBuf);
    if (!ok && outErr) *outErr = "failed to upload RAW-MESH GPU buffers";
    return ok;
}

bool RawMeshHost::UploadTerrainChunk(const raw::worldgen::ProceduralMesh& src, TerrainGpuChunk* outGpu, std::string* outErr)
{
    if (!m_terrainReady || !m_dev || !outGpu || src.meshlets.empty() || src.vertices.empty() || src.indices.empty()) {
        if (outErr) *outErr = "Terrain pipeline not ready or empty ProceduralMesh";
        return false;
    }

    // Build flat global uint32 index buffer for the VS+PS fallback path (0 extra work when drawing).
    std::vector<uint32_t> flatIndices;
    flatIndices.reserve(src.indices.size());
    for (const auto& ml : src.meshlets) {
        const uint32_t count = ml.triangleCount * 3u;
        for (uint32_t k = 0; k < count; ++k) {
            const uint32_t srcPos = ml.triangleOffset + k;
            if (srcPos < src.indices.size()) {
                flatIndices.push_back(ml.vertexOffset + src.indices[srcPos]);
            }
        }
    }
    if (flatIndices.empty()) {
        if (outErr) *outErr = "empty flattened terrain indices";
        return false;
    }

    outGpu->name = src.name;
    outGpu->meshletCount = static_cast<uint32_t>(src.meshlets.size());
    outGpu->indexCount = static_cast<uint32_t>(flatIndices.size());

    // Coarse bounding sphere from ProceduralMesh AABB for fast CPU frustum culling.
    const float cx = 0.5f * (src.aabbMin.x + src.aabbMax.x);
    const float cy = 0.5f * (src.aabbMin.y + src.aabbMax.y);
    const float cz = 0.5f * (src.aabbMin.z + src.aabbMax.z);
    const float ex = 0.5f * (src.aabbMax.x - src.aabbMin.x);
    const float ey = 0.5f * (src.aabbMax.y - src.aabbMin.y);
    const float ez = 0.5f * (src.aabbMax.z - src.aabbMin.z);
    outGpu->boundSphere[0] = cx;
    outGpu->boundSphere[1] = cy;
    outGpu->boundSphere[2] = cz;
    outGpu->boundSphere[3] = std::sqrt(ex * ex + ey * ey + ez * ez) + 64.0f;

    const bool ok =
        CreateUploadBuffer(m_dev.Get(), src.meshlets.data(), src.meshlets.size() * sizeof(raw::worldgen::MeshletDesc), outGpu->meshletBuf) &&
        CreateUploadBuffer(m_dev.Get(), src.vertices.data(), src.vertices.size() * sizeof(raw::worldgen::TerrainVertex), outGpu->vertexBuf) &&
        CreateUploadBuffer(m_dev.Get(), src.indices.data(), src.indices.size() * sizeof(uint32_t), outGpu->meshletIdxBuf) &&
        CreateUploadBuffer(m_dev.Get(), flatIndices.data(), flatIndices.size() * sizeof(uint32_t), outGpu->flatIdxBuf);
    if (!ok) {
        if (outErr) *outErr = "failed to upload TerrainGpuChunk GPU buffers";
        return false;
    }
    outGpu->flatIbv.BufferLocation = outGpu->flatIdxBuf->GetGPUVirtualAddress();
    outGpu->flatIbv.SizeInBytes = static_cast<UINT>(flatIndices.size() * sizeof(uint32_t));
    outGpu->flatIbv.Format = DXGI_FORMAT_R32_UINT;
    return true;
}

bool RawMeshHost::ExecuteTerrainPass(ID3D12CommandQueue* queue,
                                     ID3D12Resource* colorOut,
                                     ID3D12Resource* depthIn,
                                     ID3D12Resource* shadowAtlas,
                                     UINT width, UINT height,
                                     const void* lightUniformData, size_t lightUniformBytes,
                                     const float worldToClip[16],
                                     const float camPos[3],
                                     float timeSec,
                                     const std::unordered_map<uint64_t, TerrainGpuChunk>& chunks,
                                     uint32_t* outDrawnChunks,
                                     uint32_t* outDrawnMeshlets)
{
    if (outDrawnChunks) *outDrawnChunks = 0u;
    if (outDrawnMeshlets) *outDrawnMeshlets = 0u;
    if (!m_terrainReady || !queue || !colorOut || !depthIn || !lightUniformData || lightUniformBytes == 0 ||
        lightUniformBytes > 65536u || chunks.empty()) {
        return false;
    }

    CollectRetiredChunks();
    if (!EnsureTerrainDepth(width, height)) return false;

    const uint32_t slot = m_terrainSlot;
    if (m_terrainSlotVal[slot] > 0 && m_terrainFence->GetCompletedValue() < m_terrainSlotVal[slot]) {
        m_terrainFence->SetEventOnCompletion(m_terrainSlotVal[slot], m_terrainEvent);
        WaitForSingleObject(m_terrainEvent, 3000);
    }

    if (FAILED(m_terrainAlloc[slot]->Reset()) ||
        FAILED(m_terrainList->Reset(m_terrainAlloc[slot].Get(), nullptr))) {
        return false;
    }

    // Upload LightUniform (cbuffer P at b0) into this ring slot.
    std::memcpy(m_terrainLightCbMap[slot], lightUniformData, lightUniformBytes);

    // Update RTV for colorOut (RGBA16F) and per-slot SRVs for SceneDepth (t3) + ShadowAtlas (t4).
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvH = m_terrainRtvHeap->GetCPUDescriptorHandleForHeapStart();
    const D3D12_CPU_DESCRIPTOR_HANDLE dsvH = m_terrainDsvHeap->GetCPUDescriptorHandleForHeapStart();
    m_dev->CreateRenderTargetView(colorOut, nullptr, rtvH);

    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = m_terrainSrvHeap->GetCPUDescriptorHandleForHeapStart();
    srvCpu.ptr += SIZE_T(slot * 2u) * m_srvStep;
    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv{};
    depthSrv.Format = DXGI_FORMAT_R32_FLOAT;
    depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrv.Texture2D.MipLevels = 1;
    m_dev->CreateShaderResourceView(depthIn, &depthSrv, srvCpu);

    D3D12_CPU_DESCRIPTOR_HANDLE atlasCpu = srvCpu;
    atlasCpu.ptr += m_srvStep;
    m_dev->CreateShaderResourceView(shadowAtlas ? shadowAtlas : m_dummyAtlas.Get(), &depthSrv, atlasCpu);

    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = m_terrainSrvHeap->GetGPUDescriptorHandleForHeapStart();
    srvGpu.ptr += UINT64(slot * 2u) * m_srvStep;

    // Resource transitions: shared resources arrive in COMMON from LightingPass::endRecording.
    D3D12_RESOURCE_BARRIER preBarriers[3]{};
    UINT preCount = 0;
    preBarriers[preCount++] = TransitionBarrier(colorOut, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
    preBarriers[preCount++] = TransitionBarrier(depthIn, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (shadowAtlas) {
        preBarriers[preCount++] = TransitionBarrier(shadowAtlas, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    m_terrainList->ResourceBarrier(preCount, preBarriers);

    m_terrainList->ClearDepthStencilView(dsvH, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    m_terrainList->OMSetRenderTargets(1, &rtvH, FALSE, &dsvH);

    D3D12_VIEWPORT vp{0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f};
    D3D12_RECT sc{0, 0, LONG(width), LONG(height)};
    m_terrainList->RSSetViewports(1, &vp);
    m_terrainList->RSSetScissorRects(1, &sc);

    ID3D12DescriptorHeap* heaps[1] = {m_terrainSrvHeap.Get()};
    m_terrainList->SetDescriptorHeaps(1, heaps);
    m_terrainList->SetGraphicsRootSignature(m_terrainRoot.Get());

    ComPtr<ID3D12GraphicsCommandList6> cmd6;
    const bool useMs = (m_terrainMsPso != nullptr) && SUCCEEDED(m_terrainList.As(&cmd6));
    m_terrainList->SetPipelineState(useMs ? m_terrainMsPso.Get() : m_terrainVsPso.Get());
    m_terrainList->SetGraphicsRootConstantBufferView(0, m_terrainLightCb[slot]->GetGPUVirtualAddress());
    m_terrainList->SetGraphicsRootDescriptorTable(5, srvGpu);
    if (!useMs) {
        m_terrainList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    float frustum[6][4]{};
    ExtractFrustumPlanes(worldToClip, frustum);

    const D3D12_GPU_VIRTUAL_ADDRESS drawCbBase = m_terrainDrawCb[slot]->GetGPUVirtualAddress();
    uint8_t* drawCbPtr = static_cast<uint8_t*>(m_terrainDrawCbMap[slot]);
    uint32_t drawIdx = 0u;
    uint32_t totalMeshlets = 0u;

    for (const auto& kv : chunks) {
        if (drawIdx >= kMaxTerrainDrawsPerFrame) break;
        const TerrainGpuChunk& ch = kv.second;
        if (ch.meshletCount == 0u || !ch.meshletBuf || !ch.vertexBuf || !ch.meshletIdxBuf) continue;

        const float relC[3] = {
            ch.boundSphere[0] - camPos[0],
            ch.boundSphere[1] - camPos[1],
            ch.boundSphere[2] - camPos[2],
        };
        if (!SphereInFrustum(frustum, relC, ch.boundSphere[3])) continue;

        TerrainDrawCB cb{};
        std::memcpy(cb.frustumPlanes, frustum, sizeof frustum);
        cb.chunkOriginAndScale[0] = 0.0f;
        cb.chunkOriginAndScale[1] = 0.0f;
        cb.chunkOriginAndScale[2] = 0.0f;
        cb.chunkOriginAndScale[3] = 1.0f;
        cb.meshletCount = ch.meshletCount;
        cb.lodMorphFactor = 0.0f;
        cb.magmaFlowPhase = timeSec * 0.25f;
        cb.fogHeightFalloff = 0.00028f;
        std::memcpy(drawCbPtr + size_t(drawIdx) * sizeof(TerrainDrawCB), &cb, sizeof cb);

        m_terrainList->SetGraphicsRootConstantBufferView(1, drawCbBase + UINT64(drawIdx) * sizeof(TerrainDrawCB));
        m_terrainList->SetGraphicsRootShaderResourceView(2, ch.meshletBuf->GetGPUVirtualAddress());
        m_terrainList->SetGraphicsRootShaderResourceView(3, ch.vertexBuf->GetGPUVirtualAddress());
        m_terrainList->SetGraphicsRootShaderResourceView(4, ch.meshletIdxBuf->GetGPUVirtualAddress());

        if (useMs) {
            cmd6->DispatchMesh((ch.meshletCount + 31u) / 32u, 1u, 1u);
        } else if (ch.flatIdxBuf && ch.indexCount > 0u) {
            m_terrainList->IASetIndexBuffer(&ch.flatIbv);
            m_terrainList->DrawIndexedInstanced(ch.indexCount, 1u, 0u, 0, 0u);
        }

        totalMeshlets += ch.meshletCount;
        ++drawIdx;
    }

    // Restore shared resources back to COMMON for D3D11 copy-out / next frame.
    D3D12_RESOURCE_BARRIER postBarriers[3]{};
    UINT postCount = 0;
    postBarriers[postCount++] = TransitionBarrier(colorOut, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
    postBarriers[postCount++] = TransitionBarrier(depthIn, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    if (shadowAtlas) {
        postBarriers[postCount++] = TransitionBarrier(shadowAtlas, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    }
    m_terrainList->ResourceBarrier(postCount, postBarriers);

    if (FAILED(m_terrainList->Close())) return false;

    ID3D12CommandList* lists[1] = {m_terrainList.Get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(m_terrainFence.Get(), ++m_terrainFenceVal);
    m_terrainSlotVal[slot] = m_terrainFenceVal;
    m_terrainSlot = (slot + 1u) % kTerrainRing;

    if (outDrawnChunks) *outDrawnChunks = drawIdx;
    if (outDrawnMeshlets) *outDrawnMeshlets = totalMeshlets;
    return true;
}

void RawMeshHost::Dispatch(ID3D12GraphicsCommandList* cmd, const RawMeshGpuAsset& asset, const RawMeshFrameCB& cb)
{
    if (!m_ready || !cmd || asset.meshletCount == 0 || !m_cbMapped) return;
    ComPtr<ID3D12GraphicsCommandList6> cmd6;
    if (FAILED(cmd->QueryInterface(IID_PPV_ARGS(&cmd6)))) return;
    RawMeshFrameCB local = cb;
    local.meshletCount = asset.meshletCount;
    std::memcpy(m_cbMapped, &local, sizeof local);
    cmd6->SetGraphicsRootSignature(m_root.Get());
    cmd6->SetPipelineState(m_pso.Get());
    cmd6->SetGraphicsRootConstantBufferView(0, m_cbBuf->GetGPUVirtualAddress());
    cmd6->SetGraphicsRootShaderResourceView(1, asset.meshletBuf->GetGPUVirtualAddress());
    cmd6->SetGraphicsRootShaderResourceView(2, asset.vertexBuf->GetGPUVirtualAddress());
    cmd6->SetGraphicsRootShaderResourceView(3, asset.indexBuf->GetGPUVirtualAddress());
    cmd6->DispatchMesh((asset.meshletCount + 31u) / 32u, 1u, 1u);
}

void RawMeshHost::SubdivideMesh(ID3D12GraphicsCommandList* cmd,
                                D3D12_GPU_VIRTUAL_ADDRESS inVertsSrv, D3D12_GPU_VIRTUAL_ADDRESS inIdxSrv,
                                D3D12_GPU_VIRTUAL_ADDRESS outVertsUav, D3D12_GPU_VIRTUAL_ADDRESS outIdxUav,
                                const RawSubdivCB& cb)
{
    if (!cmd || !m_subdivPso || !m_subdivRoot || !m_subdivCbMapped || cb.inTriangleCount == 0) return;
    std::memcpy(m_subdivCbMapped, &cb, sizeof cb);
    cmd->SetComputeRootSignature(m_subdivRoot.Get());
    cmd->SetPipelineState(m_subdivPso.Get());
    cmd->SetComputeRootConstantBufferView(0, m_subdivCbBuf->GetGPUVirtualAddress());
    cmd->SetComputeRootShaderResourceView(1, inVertsSrv);
    cmd->SetComputeRootShaderResourceView(2, inIdxSrv);
    cmd->SetComputeRootUnorderedAccessView(3, outVertsUav);
    cmd->SetComputeRootUnorderedAccessView(4, outIdxUav);
    cmd->Dispatch((cb.inTriangleCount + 63u) / 64u, 1u, 1u);
}

} // namespace raw::bridge
