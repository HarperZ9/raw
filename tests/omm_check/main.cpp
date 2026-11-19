// omm_check: which micro-triangle of an opacity micromap a barycentric point falls in, checked on the GPU.
//
// RtOmm bakes opacity micromaps from the alpha masks, and needs the order in which a level-L micromap
// lists its 4^L micro-triangles (a space-filling curve over the triangle; the DXR spec shows it only as
// a picture). --derive reads that order from the GPU into OmmTable.h. The plain run builds one unit
// triangle with a 2-state micromap whose bits follow a random pattern placed with the table (through
// CellOf, as the bake places them), traces one ray through the centre of every micro-triangle, and
// compares hit or miss with the pattern. Pass: every ray agrees, at levels 1 to 5, and no unknown state
// reaches the shader. Needs DXR 1.2 and the Agility SDK (RAW_TX_AGILITY_PATH, RAW_TX_AGILITY_VERSION).
// Original work, MIT.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "omm_check_cs.h"   // kDxil_omm_check
#include "../../src/d3d12_bridge/OmmIndex.h"

using Microsoft::WRL::ComPtr;

namespace
{
ComPtr<ID3D12Device5> g_dev;
ComPtr<ID3D12CommandQueue> g_q;
ComPtr<ID3D12CommandAllocator> g_alloc;
ComPtr<ID3D12GraphicsCommandList4> g_cl;
ComPtr<ID3D12Fence> g_fence;
uint64_t g_fv = 0;

ComPtr<ID3D12Resource> Buf(uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES s, bool uav)
{
    D3D12_HEAP_PROPERTIES hp{heap};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = bytes < 256 ? 256 : bytes; d.Height = 1; d.DepthOrArraySize = 1;
    d.MipLevels = 1; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> r;
    g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, s, nullptr, IID_PPV_ARGS(&r));
    return r;
}
ComPtr<ID3D12Resource> Upload(const void* p, uint64_t bytes)
{
    ComPtr<ID3D12Resource> r = Buf(bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false);
    void* m = nullptr;
    r->Map(0, nullptr, &m);
    std::memcpy(m, p, size_t(bytes));
    r->Unmap(0, nullptr);
    return r;
}
void Run()
{
    g_cl->Close();
    ID3D12CommandList* l = g_cl.Get();
    g_q->ExecuteCommandLists(1, &l);
    g_q->Signal(g_fence.Get(), ++g_fv);
    HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_fence->SetEventOnCompletion(g_fv, e);
    WaitForSingleObject(e, 10000);
    CloseHandle(e);
    g_alloc->Reset();
    g_cl->Reset(g_alloc.Get(), nullptr);
}
D3D12_RESOURCE_BARRIER Uav() { D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; return b; }

bool MakeDevice()
{
    const char* path = std::getenv("RAW_TX_AGILITY_PATH");
    const char* ver = std::getenv("RAW_TX_AGILITY_VERSION");
    if (!path || !ver) { std::printf("SKIP: RAW_TX_AGILITY_PATH and RAW_TX_AGILITY_VERSION not set\n"); return false; }
    using GetIface = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
    auto get = reinterpret_cast<GetIface>(GetProcAddress(LoadLibraryW(L"d3d12.dll"), "D3D12GetInterface"));
    ComPtr<ID3D12SDKConfiguration1> cfg;
    ComPtr<ID3D12DeviceFactory> fac;
    if (!get || FAILED(get(CLSID_D3D12SDKConfiguration, IID_PPV_ARGS(&cfg))) ||
        FAILED(cfg->CreateDeviceFactory(UINT(std::atoi(ver)), path, IID_PPV_ARGS(&fac))) ||
        FAILED(fac->CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&g_dev)))) {
        std::printf("FAIL: no Agility SDK device\n");
        return false;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
    g_dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof o5);
    if (o5.RaytracingTier < D3D12_RAYTRACING_TIER_1_2) { std::printf("SKIP: DXR tier %d, opacity micromaps need 1.2\n", int(o5.RaytracingTier)); return false; }
    D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT};
    g_dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&g_q));
    g_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_alloc));
    g_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_alloc.Get(), nullptr, IID_PPV_ARGS(&g_cl));
    g_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence));
    return true;
}

// One level: returns the number of rays that disagree with the pattern (or -1 on an API failure).
// Builds a level-`level` 2-state micromap from `bits` on the unit triangle and traces one ray at each
// point; returns per ray (hit | candidates << 1), or an empty vector on an API failure.
std::vector<uint32_t> Trace(uint32_t level, const std::vector<uint8_t>& bits, const std::vector<float>& pts, bool four,
                            ID3D12RootSignature* root, ID3D12PipelineState* pso)
{
    const uint32_t count = uint32_t(pts.size() / 2);
    // The micromap array: one micromap (level, 2-state), its descriptor, the histogram.
    ComPtr<ID3D12Resource> input = Upload(bits.data(), bits.size());
    D3D12_RAYTRACING_OPACITY_MICROMAP_DESC desc{};
    desc.ByteOffset = 0; desc.SubdivisionLevel = level; desc.Format = four ? D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_4_STATE : D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_2_STATE;
    ComPtr<ID3D12Resource> descs = Upload(&desc, sizeof desc);
    D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY hist{1, level, desc.Format};
    D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC ad{};
    ad.NumOmmHistogramEntries = 1; ad.pOmmHistogram = &hist;
    ad.InputBuffer = input->GetGPUVirtualAddress();
    ad.PerOmmDescs = {descs->GetGPUVirtualAddress(), sizeof desc};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS oi{};
    oi.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_OPACITY_MICROMAP_ARRAY;
    oi.NumDescs = 1; oi.pOpacityMicromapArrayDesc = &ad;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO pre{};
    g_dev->GetRaytracingAccelerationStructurePrebuildInfo(&oi, &pre);
    if (!pre.ResultDataMaxSizeInBytes) return {};
    ComPtr<ID3D12Resource> omm = Buf(pre.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, true);
    ComPtr<ID3D12Resource> scratch = Buf(pre.ScratchDataSizeInBytes + 65536, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC b{};
    b.Inputs = oi; b.DestAccelerationStructureData = omm->GetGPUVirtualAddress(); b.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
    g_cl->BuildRaytracingAccelerationStructure(&b, 0, nullptr);
    D3D12_RESOURCE_BARRIER u = Uav();
    g_cl->ResourceBarrier(1, &u);

    // The BLAS: one triangle linked to micromap 0.
    const float verts[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const uint32_t ommIndex = 0;
    ComPtr<ID3D12Resource> vb = Upload(verts, sizeof verts), ib = Upload(&ommIndex, sizeof ommIndex);
    D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC tri{};
    tri.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT; tri.VertexCount = 3; tri.VertexBuffer = {vb->GetGPUVirtualAddress(), 12};
    D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC link{};
    link.OpacityMicromapIndexBuffer = {ib->GetGPUVirtualAddress(), 4};
    link.OpacityMicromapIndexFormat = DXGI_FORMAT_R32_UINT;
    link.OpacityMicromapArray = omm->GetGPUVirtualAddress();
    D3D12_RAYTRACING_GEOMETRY_OMM_TRIANGLES_DESC ot{&tri, &link};
    D3D12_RAYTRACING_GEOMETRY_DESC g{};
    g.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES; g.OmmTriangles = ot;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bi{};
    bi.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL; bi.NumDescs = 1; bi.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY; bi.pGeometryDescs = &g;
    g_dev->GetRaytracingAccelerationStructurePrebuildInfo(&bi, &pre);
    ComPtr<ID3D12Resource> blas = Buf(pre.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, true);
    b = {}; b.Inputs = bi; b.DestAccelerationStructureData = blas->GetGPUVirtualAddress(); b.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
    g_cl->BuildRaytracingAccelerationStructure(&b, 0, nullptr);
    g_cl->ResourceBarrier(1, &u);
    D3D12_RAYTRACING_INSTANCE_DESC inst{};
    inst.Transform[0][0] = inst.Transform[1][1] = inst.Transform[2][2] = 1.0f;
    inst.InstanceMask = 0xFF; inst.AccelerationStructure = blas->GetGPUVirtualAddress();
    ComPtr<ID3D12Resource> instBuf = Upload(&inst, sizeof inst);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS ti{};
    ti.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL; ti.NumDescs = 1; ti.InstanceDescs = instBuf->GetGPUVirtualAddress();
    g_dev->GetRaytracingAccelerationStructurePrebuildInfo(&ti, &pre);
    ComPtr<ID3D12Resource> tlas = Buf(pre.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, true);
    b = {}; b.Inputs = ti; b.DestAccelerationStructureData = tlas->GetGPUVirtualAddress(); b.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
    g_cl->BuildRaytracingAccelerationStructure(&b, 0, nullptr);
    g_cl->ResourceBarrier(1, &u);

    // The rays.
    ComPtr<ID3D12Resource> ptsBuf = Upload(pts.data(), pts.size() * 4);
    ComPtr<ID3D12Resource> out = Buf(count * 4ull, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
    ComPtr<ID3D12Resource> read = Buf(count * 4ull, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, false);
    g_cl->SetComputeRootSignature(root);
    g_cl->SetPipelineState(pso);
    g_cl->SetComputeRootShaderResourceView(0, tlas->GetGPUVirtualAddress());
    g_cl->SetComputeRootShaderResourceView(1, ptsBuf->GetGPUVirtualAddress());
    g_cl->SetComputeRootUnorderedAccessView(2, out->GetGPUVirtualAddress());
    g_cl->SetComputeRoot32BitConstant(3, count, 0);
    g_cl->Dispatch((count + 63) / 64, 1, 1);
    D3D12_RESOURCE_BARRIER t{};
    t.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    t.Transition = {out.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
    g_cl->ResourceBarrier(1, &t);
    g_cl->CopyResource(read.Get(), out.Get());
    Run();
    std::vector<uint32_t> res(count);
    uint32_t* r = nullptr;
    read->Map(0, nullptr, reinterpret_cast<void**>(&r));
    std::memcpy(res.data(), r, count * 4);
    read->Unmap(0, nullptr);
    return res;
}

// The grid's micro-triangle centres in raw::omm::Centre order.
std::vector<float> Centres(uint32_t level)
{
    const uint32_t n = 1u << level;
    std::vector<float> pts;
    for (uint32_t row = 0; row < n; ++row)
        for (uint32_t k = 0; k < 2 * (n - row) - 1; ++k) {
            float u, v;
            raw::omm::Centre(level, row, k, u, v);
            pts.push_back(u); pts.push_back(v);
        }
    return pts;
}

// The hardware as the oracle: map j sets the micro-triangles whose index has bit j, so the hit pattern of
// a grid cell over the maps spells its index. Returns the index of each cell in Centre order.
std::vector<uint32_t> Derive(uint32_t level, ID3D12RootSignature* root, ID3D12PipelineState* pso)
{
    const uint32_t count = 1u << (2 * level);
    const std::vector<float> pts = Centres(level);
    std::vector<uint32_t> idx(count, 0);
    for (uint32_t j = 0; j < 2 * level; ++j) {
        std::vector<uint8_t> bits((count + 7) / 8, 0);
        for (uint32_t i = 0; i < count; ++i) if ((i >> j) & 1u) bits[i >> 3] |= uint8_t(1u << (i & 7));
        const std::vector<uint32_t> r = Trace(level, bits, pts, false, root, pso);
        if (r.size() != count) return {};
        for (uint32_t c = 0; c < count; ++c) idx[c] |= (r[c] & 1u) << j;
    }
    return idx;
}

// four: the 4-state format, two bits per micro-triangle (transparent 0, opaque 1), as RtOmmBake packs them.
int CheckLevel(uint32_t level, bool four, ID3D12RootSignature* root, ID3D12PipelineState* pso, int* candidates)
{
    const uint32_t n = 1u << level, count = n * n;
    // A random pattern placed in micromap order with raw::omm::Index; the rays in grid order.
    std::vector<uint8_t> bits(four ? (count + 3) / 4 : (count + 7) / 8, 0);
    const std::vector<float> pts = Centres(level);
    std::vector<uint32_t> want;
    uint32_t seed = 12345u + level;
    for (uint32_t c = 0; c < count; ++c) {
        const uint32_t idx = raw::omm::IndexOfCell(level, raw::omm::CellOf(level, pts[2 * c], pts[2 * c + 1]));
        seed = seed * 1664525u + 1013904223u;
        const uint32_t on = (seed >> 16) & 1u;
        if (idx >= count) return 100000;
        if (on && four) bits[idx >> 2] |= uint8_t(1u << (2 * (idx & 3)));
        else if (on) bits[idx >> 3] |= uint8_t(1u << (idx & 7));
        want.push_back(on);
    }
    const std::vector<uint32_t> r = Trace(level, bits, pts, four, root, pso);
    if (r.size() != count) return -1;
    int bad = 0;
    *candidates = 0;
    for (uint32_t i = 0; i < count; ++i) { bad += (r[i] & 1u) != want[i]; *candidates += int(r[i] >> 1); }
    return bad;
}
} // namespace

int main(int argc, char** argv)
{
    if (!MakeDevice()) return 0;
    // Root: TLAS (t0), points (t1), output (u0), the count (b0).
    D3D12_ROOT_PARAMETER p[4]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; p[0].Descriptor = {0, 0};
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; p[1].Descriptor = {1, 0};
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; p[2].Descriptor = {0, 0};
    p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[3].Constants = {0, 0, 4};
    D3D12_ROOT_SIGNATURE_DESC rs{4, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, err;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pso;
    D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    g_dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = root.Get();
    pd.CS = {kDxil_omm_check, sizeof kDxil_omm_check};
    pd.Flags = D3D12_PIPELINE_STATE_FLAG_RAYQUERY_ALLOW_OPACITY_MICROMAPS_ON_PRE_SM69_SHADERS;   // the shader is cs_6_6
    if (FAILED(g_dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)))) { std::printf("FAIL: pipeline\n"); return 1; }
    // --derive FILE: read the micromap order from the hardware (levels 1 to 5) and write it as a table.
    if (argc == 3 && std::strcmp(argv[1], "--derive") == 0) {
        FILE* f = nullptr;
        if (fopen_s(&f, argv[2], "w") != 0 || !f) { std::printf("FAIL: cannot write %s\n", argv[2]); return 1; }
        std::fprintf(f, "// OmmTable.h: the micromap index of each grid micro-triangle (raw::omm::Centre order), levels 1 to 5,\n"
                        "// read from the GPU by tests/omm_check --derive (generated; do not edit). Original work, MIT.\n#pragma once\n#include <cstdint>\n"
                        "namespace raw::omm\n{\n");
        for (uint32_t level = 1; level <= 5; ++level) {
            const std::vector<uint32_t> idx = Derive(level, root.Get(), pso.Get());
            if (idx.empty()) { std::printf("FAIL: derive level %u\n", level); fclose(f); return 1; }
            std::fprintf(f, "inline constexpr uint16_t kCellIndex%u[%zu] = {", level, idx.size());
            for (size_t i = 0; i < idx.size(); ++i) std::fprintf(f, "%s%u", i ? (i % 32 ? "," : ",\n    ") : "", idx[i]);
            std::fprintf(f, "};\n");
        }
        std::fprintf(f, "} // namespace raw::omm\n");
        fclose(f);
        std::printf("wrote %s\n", argv[2]);
        return 0;
    }
    // The table in use (OmmTable.h) must place every micro-triangle where the hardware reads it.
    int failed = 0;
    for (int four = 0; four < 2; ++four)
        for (uint32_t level = 1; level <= 5; ++level) {
            int cand = 0;
            const int bad = CheckLevel(level, four != 0, root.Get(), pso.Get(), &cand);
            const bool ok = bad == 0 && cand == 0;
            failed += !ok;
            std::printf("%s %s level %u: %u micro-triangles, %d rays disagree, %d candidates\n", ok ? "PASS" : "FAIL", four ? "4-state" : "2-state",
                        level, 1u << (2 * level), bad, cand);
        }
    std::printf("%s\n", failed ? "FAIL" : "PASS: the micromap table and both bit packings match the hardware at levels 1 to 5");
    return failed ? 1 : 0;
}
