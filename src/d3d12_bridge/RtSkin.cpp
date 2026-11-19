// RtSkin: see the header. Original work, MIT.
#include "RtSkin.h"
#include "rt_skin.h"   // kDxil_rt_skin (dxc, cs_6_6)
#include "rt_skin_check.h"   // kDxil_rt_skin_check

#include <algorithm>
#include <cstring>

namespace raw::bridge
{
using Microsoft::WRL::ComPtr;
namespace
{
constexpr uint64_t Align256(uint64_t v) { return (v + 255) & ~255ull; }
}

bool RtSkin::Create(ID3D12Device5* dev, ID3D12DescriptorHeap* heap, uint32_t base, std::string& err)
{
    m_dev = dev; m_heap = heap; m_base = base;
    m_inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto buffer = [&](ComPtr<ID3D12Resource>& r, uint64_t bytes, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES s, bool uav, const wchar_t* name) {
        D3D12_HEAP_PROPERTIES hp{type};
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width = bytes; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1; d.SampleDesc.Count = 1;
        d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        d.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, s, nullptr, IID_PPV_ARGS(&r)))) return false;
        r->SetName(name);
        return true;
    };
    bool ok = buffer(m_scratch, kScratch, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, L"rt skin scratch");
    const D3D12_RANGE none{0, 0};
    for (int k = 0; ok && k < 3; ++k)
        ok = buffer(m_pool[k], kPool, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, L"rt skin positions") &&
             buffer(m_blas[k], kBlas, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, true, L"rt skin blas") &&
             buffer(m_bones[k], kBones, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false, L"rt skin bones") &&
             SUCCEEDED(m_bones[k]->Map(0, &none, reinterpret_cast<void**>(&m_bonesMapped[k]))) &&
             buffer(m_idx[k], kIdx, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, L"rt skin indices") &&
             buffer(m_stats[k], kStats, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, L"rt skin stats") &&
             buffer(m_statsRead[k], kStats, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, false, L"rt skin stats read");
    ok = ok && buffer(m_zero, kStats, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, false, L"rt skin zero");
    if (ok) {
        void* z = nullptr;
        if (SUCCEEDED(m_zero->Map(0, &none, &z))) { std::memset(z, 0, size_t(kStats)); m_zero->Unmap(0, nullptr); } else ok = false;
    }
    if (!ok) { err = "skin resources"; return false; }
    D3D12_DESCRIPTOR_RANGE srv{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
    D3D12_DESCRIPTOR_RANGE uav{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER p[4]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[0].Constants = {0, 0, 16};
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[1].DescriptorTable = {1, &srv};
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; p[2].Descriptor = {2, 0};
    p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[3].DescriptorTable = {1, &uav};
    D3D12_ROOT_SIGNATURE_DESC rs{4, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, errs;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errs)) ||
        FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_root)))) {
        err = errs ? static_cast<const char*>(errs->GetBufferPointer()) : "skin root signature";
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_root.Get();
    pd.CS = {kDxil_rt_skin, sizeof kDxil_rt_skin};
    if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_pso)))) { err = "skin pipeline"; return false; }
    // The check: t0 the game's index buffer, t1 and t2 the two streams, u0 the pool, u1 the index pool, u2 the stats.
    D3D12_DESCRIPTOR_RANGE ibR{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_DESCRIPTOR_RANGE vbR{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0, 0};
    D3D12_DESCRIPTOR_RANGE uR{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0, 0, 0};
    D3D12_DESCRIPTOR_RANGE dR{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0, 0};
    D3D12_ROOT_PARAMETER q[5]{};
    q[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; q[0].Constants = {0, 0, 40};
    q[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; q[4].DescriptorTable = {1, &dR};
    q[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; q[1].DescriptorTable = {1, &ibR};
    q[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; q[2].DescriptorTable = {1, &vbR};
    q[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; q[3].DescriptorTable = {1, &uR};
    D3D12_ROOT_SIGNATURE_DESC qs{5, q, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    blob.Reset(); errs.Reset();
    if (FAILED(D3D12SerializeRootSignature(&qs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errs)) ||
        FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_checkRoot)))) {
        err = errs ? static_cast<const char*>(errs->GetBufferPointer()) : "skin check root signature";
        return false;
    }
    pd.pRootSignature = m_checkRoot.Get();
    pd.CS = {kDxil_rt_skin_check, sizeof kDxil_rt_skin_check};
    if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_checkPso)))) { err = "skin check pipeline"; return false; }
    return true;
}

void RtSkin::SetDraws(std::vector<RtDraw>& draws, std::vector<ID3D11Resource*>& toHand, ID3D12Resource* (*native)(ID3D11Resource*))
{
    m_jobs.clear(); m_boneData.clear(); m_vertices = 0;
    uint32_t outFirst = 0, idxFirst = 0;
    for (const RtDraw& d : draws) {
        if (!d.skinned || !d.bones || m_jobs.size() >= kMaxDraws) continue;
        D3D11_BUFFER_DESC vd{}, bd{}, id{};
        d.vb->GetDesc(&vd); d.blendVb->GetDesc(&bd); d.ib->GetDesc(&id);
        ID3D12Resource* v12 = native(d.vb);
        ID3D12Resource* b12 = native(d.blendVb);
        ID3D12Resource* i12 = native(d.ib);
        const int64_t vStart = int64_t(d.vbOffset) + int64_t(d.baseVertex) * d.stride;
        const int64_t bStart = int64_t(d.blendVbOffset) + int64_t(d.baseVertex) * d.blendStride;
        const uint32_t isz = d.ibFormat == DXGI_FORMAT_R16_UINT ? 2 : 4;
        const uint64_t iStart = uint64_t(d.ibOffset) + uint64_t(d.startIndex) * isz;
        if (!v12 || !b12 || !i12 || !d.stride || !d.blendStride || vStart < 0 || bStart < 0 || uint64_t(vStart) >= vd.ByteWidth ||
            uint64_t(bStart) >= bd.ByteWidth || iStart + uint64_t(d.indexCount) * isz > id.ByteWidth) continue;
        const uint32_t count = uint32_t(std::min<uint64_t>({(vd.ByteWidth - uint64_t(vStart)) / d.stride, (bd.ByteWidth - uint64_t(bStart)) / d.blendStride, uint64_t(kMaxVertices)}));
        if (uint64_t(outFirst + count) * 12 > kPool || (uint64_t(idxFirst) + d.indexCount) * 4 > kIdx) break;
        Job j{};
        j.vb = v12; j.bv = b12; j.ib = i12; j.vbBytes = vd.ByteWidth; j.bvBytes = bd.ByteWidth; j.iStart = iStart;
        j.ibFormat = d.ibFormat; j.indexCount = d.indexCount / 3 * 3; j.idxFirst = idxFirst; j.ibBytes = id.ByteWidth;
        j.actor = d.actor;
        const uint32_t posFmt = d.posFormat == DXGI_FORMAT_R16G16B16A16_FLOAT ? 0u : 1u;
        j.posFmt = posFmt;
        uint32_t c[16] = {uint32_t(vStart), d.stride, d.posOffset, posFmt, uint32_t(bStart), d.blendStride, d.idxOffset, d.wOffset,
                          count, outFirst, uint32_t(m_jobs.size() * 240), 0};
        std::memcpy(c + 12, m_cam, 12);
        std::memcpy(j.c, c, sizeof c);
        m_boneData.insert(m_boneData.end(), d.bones, d.bones + 960);
        if (d.diagVb) {
            j.diagVb.assign(d.diagVb, d.diagVb + d.diagVbBytes);
            j.diagBv.assign(d.diagBv, d.diagBv + d.diagBvBytes);
            j.diagBones.assign(d.bones, d.bones + 960);
        }
        m_jobs.push_back(std::move(j));
        outFirst += count;
        idxFirst += j.indexCount;
        m_vertices += count;
        toHand.push_back(d.vb); toHand.push_back(d.blendVb); toHand.push_back(d.ib);
    }
}

// The slot's stats from three frames ago (the caller waited for the slot): stretched triangles counted, and
// every logEvery frames the draws that had any, with their first stretched triangle in full.
void RtSkin::ReadStats(uint32_t slot)
{
    const std::vector<Meta>& meta = m_meta[slot];
    if (meta.empty()) return;
    void* p = nullptr;
    const D3D12_RANGE r{0, size_t(meta.size()) * kStatWords * 4};
    if (FAILED(m_statsRead[slot]->Map(0, &r, &p)) || !p) return;
    const uint32_t* s = static_cast<const uint32_t*>(p);
    uint64_t stretched = 0, range = 0, tris = 0, floating = 0;
    for (size_t k = 0; k < meta.size(); ++k) {
        tris += s[k * kStatWords]; range += s[k * kStatWords + 1]; stretched += s[k * kStatWords + 2]; floating += s[k * kStatWords + 38];
    }
    m_stretched = stretched; m_floating = floating;
    if (m_log && m_logEvery && ++m_checkFrames >= m_logEvery) {
        m_checkFrames = 0;
        m_log("[RT] skin check: %zu draws, %llu triangles, %llu stretched (edge over %.1fx its bind length and %.0f units; RtSkinCull=%d), "
              "%llu with an index past the vertex count; %llu floating in front of the game's depth", meta.size(), (unsigned long long)tris,
              (unsigned long long)stretched, m_ratio, m_minUnits, int(m_filter), (unsigned long long)range, (unsigned long long)floating);
        auto half = [](uint32_t b) {
            const uint32_t e = (b >> 10) & 31, f = b & 1023;
            const float v = e == 0 ? f / 1024.0f / 16384.0f : (1.0f + f / 1024.0f) * float(1u << e) / 32768.0f;
            return (b >> 15) & 1 ? -v : v;
        };
        // The draws with the most stretched triangles first.
        std::vector<uint32_t> order;
        auto bad = [&](uint32_t k) { return s[k * kStatWords + 2] + s[k * kStatWords + 38]; };
        for (uint32_t k = 0; k < meta.size(); ++k) if (bad(k) || s[k * kStatWords + 1]) order.push_back(k);
        std::sort(order.begin(), order.end(), [&](uint32_t x, uint32_t y) { return bad(x) > bad(y); });
        for (size_t o = 0; o < order.size() && o < 6; ++o) {
            const size_t k = order[o];
            const uint32_t* w = s + k * kStatWords;
            const Meta& m = meta[k];
            m_log("[RT]   draw %zu: %s, %s positions, stride %u, %u vertices, %u triangles: %u stretched, %u floating, %u past the count; first "
                  "stretched at triangle %u", k, m.actor ? "actor" : "tree", m.posFmt ? "float" : "half", m.stride, m.count, w[0], w[2], w[38], w[1], w[4]);
            if (w[39]) {
                float sp[3], ahead;
                std::memcpy(sp, w + 41, 12); std::memcpy(&ahead, w + 47, 4);
                m_log("[RT]     floating vertex %u: skinned %.1f %.1f %.1f, %.0f units in front of the game's surface, bones %u %u %u %u, weights %.3f %.3f "
                      "%.3f %.3f", w[40], sp[0], sp[1], sp[2], ahead, w[44] & 255, (w[44] >> 8) & 255, (w[44] >> 16) & 255, w[44] >> 24,
                      half(w[45] & 0xFFFF), half(w[45] >> 16), half(w[46] & 0xFFFF), half(w[46] >> 16));
                // SkinVbCheck: the same vertex from the bytes the game's draw saw, skinned on the CPU with the draw's palette.
                const uint32_t v = w[40];
                if (!m.diagVb.empty() && m.posFmt && uint64_t(v) * m.stride + m.posOffset + 12 <= m.diagVb.size() &&
                    uint64_t(v) * m.bStride + m.wOffset + 8 <= m.diagBv.size() && uint64_t(v) * m.bStride + m.idxOffset + 4 <= m.diagBv.size()) {
                    float p[3];
                    std::memcpy(p, m.diagVb.data() + size_t(v) * m.stride + m.posOffset, 12);
                    uint32_t iw, ww[2];
                    std::memcpy(&iw, m.diagBv.data() + size_t(v) * m.bStride + m.idxOffset, 4);
                    std::memcpy(ww, m.diagBv.data() + size_t(v) * m.bStride + m.wOffset, 8);
                    const float wt[4] = {half(ww[0] & 0xFFFF), half(ww[0] >> 16), half(ww[1] & 0xFFFF), half(ww[1] >> 16)};
                    float o[3] = {};
                    for (int b = 0; b < 4; ++b) {
                        const uint32_t row = std::min<uint32_t>(uint32_t(float((iw >> (8 * b)) & 255) / 255.0f * 765.01f), 237u);
                        for (int r = 0; r < 3; ++r) {
                            const float* R = m.diagBones.data() + (row + r) * 4;
                            o[r] += wt[b] * (R[0] * p[0] + R[1] * p[1] + R[2] * p[2] + R[3]);
                        }
                    }
                    m_log("[RT]     the same vertex at the game's draw: position %.2f %.2f %.2f, bones %u %u %u %u, weights %.3f %.3f %.3f %.3f, "
                          "skinned on the CPU %.1f %.1f %.1f (world, before the camera is taken off)", p[0], p[1], p[2], iw & 255, (iw >> 8) & 255,
                          (iw >> 16) & 255, iw >> 24, wt[0], wt[1], wt[2], wt[3], o[0], o[1], o[2]);
                }
            }
            for (int v = 0; v < 3 && w[3]; ++v) {
                const uint32_t* x = w + 5 + v * 11;
                float bp[3], sp[3];
                std::memcpy(bp, x + 1, 12); std::memcpy(sp, x + 4, 12);
                m_log("[RT]     vertex %u: bind %.2f %.2f %.2f, skinned %.1f %.1f %.1f, bones %u %u %u %u, weights %.3f %.3f %.3f %.3f", x[0], bp[0],
                      bp[1], bp[2], sp[0], sp[1], sp[2], x[7] & 255, (x[7] >> 8) & 255, (x[7] >> 16) & 255, x[7] >> 24, half(x[8] & 0xFFFF),
                      half(x[8] >> 16), half(x[9] & 0xFFFF), half(x[9] >> 16));
            }
        }
    }
    const D3D12_RANGE none{0, 0};
    m_statsRead[slot]->Unmap(0, &none);
}

bool RtSkin::Record(ID3D12GraphicsCommandList4* cl, uint32_t slot, ID3D12Resource* depth, const void* uniform, size_t bytes)
{
    m_built = 0;
    ReadStats(slot);
    if (m_jobs.empty()) { m_meta[slot].clear(); return false; }
    std::memcpy(m_bonesMapped[slot], m_boneData.data(), m_boneData.size() * sizeof(float));
    const D3D12_CPU_DESCRIPTOR_HANDLE cpu0 = m_heap->GetCPUDescriptorHandleForHeapStart();
    const D3D12_GPU_DESCRIPTOR_HANDLE gpu0 = m_heap->GetGPUDescriptorHandleForHeapStart();
    cl->SetComputeRootSignature(m_root.Get());
    cl->SetPipelineState(m_pso.Get());
    cl->SetComputeRootShaderResourceView(2, m_bones[slot]->GetGPUVirtualAddress());
    auto raw = [&](ID3D12Resource* r, uint64_t bytes, uint32_t k, bool uav) {
        if (uav) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
            ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            ud.Buffer.NumElements = UINT(bytes / 4); ud.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            m_dev->CreateUnorderedAccessView(r, nullptr, &ud, {cpu0.ptr + SIZE_T(k) * m_inc});
            return;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R32_TYPELESS; sd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Buffer.NumElements = UINT(bytes / 4); sd.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        m_dev->CreateShaderResourceView(r, &sd, {cpu0.ptr + SIZE_T(k) * m_inc});
    };
    const uint32_t first = m_base + slot * kMaxDraws * kPerDraw;
    uint32_t at = first;
    for (const Job& j : m_jobs) {
        raw(j.vb, j.vbBytes, at, false);
        raw(j.bv, j.bvBytes, at + 1, false);
        raw(m_pool[slot].Get(), kPool, at + 2, true);
        raw(m_idx[slot].Get(), kIdx, at + 3, true);
        raw(m_stats[slot].Get(), kStats, at + 4, true);
        raw(j.ib, j.ibBytes / 4 * 4, at + 5, false);
        cl->SetComputeRootDescriptorTable(1, {gpu0.ptr + UINT64(at) * m_inc});
        cl->SetComputeRootDescriptorTable(3, {gpu0.ptr + UINT64(at + 2) * m_inc});
        cl->SetComputeRoot32BitConstants(0, 16, j.c, 0);
        cl->Dispatch((j.c[8] + 63) / 64, 1, 1);
        at += kPerDraw;
    }
    // The stats start at zero each frame (a copy from the zero buffer).
    auto trans = [&](ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
        D3D12_RESOURCE_BARRIER t{};
        t.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        t.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, a, b};
        cl->ResourceBarrier(1, &t);
    };
    trans(m_stats[slot].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    cl->CopyBufferRegion(m_stats[slot].Get(), 0, m_zero.Get(), 0, kStats);
    trans(m_stats[slot].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    D3D12_RESOURCE_BARRIER u{};
    u.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    u.UAV.pResource = m_pool[slot].Get();
    cl->ResourceBarrier(1, &u);
    // Each draw's triangles into the index pool, stretched ones made degenerate (RtSkinCheck.hlsl).
    cl->SetComputeRootSignature(m_checkRoot.Get());
    cl->SetPipelineState(m_checkPso.Get());
    // The game's depth for the floating check, and the camera from the lighting uniform (dims at float 0,
    // proj at 4, worldToClip at 24; LightUniform in LightingPass.cpp).
    const uint32_t depthAt = m_base + 3 * kMaxDraws * kPerDraw + slot;
    float cam[22] = {};
    const bool haveCam = depth && uniform && bytes >= 40 * 4;
    if (haveCam) {
        const float* uf = static_cast<const float*>(uniform);
        cam[0] = uf[4]; cam[1] = uf[5];
        std::memcpy(cam + 2, uf + 24, 64);
        cam[18] = uf[0]; cam[19] = uf[1];
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R32_FLOAT; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        m_dev->CreateShaderResourceView(depth, &sd, {cpu0.ptr + SIZE_T(depthAt) * m_inc});
    } else {
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R32_FLOAT; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        m_dev->CreateShaderResourceView(nullptr, &sd, {cpu0.ptr + SIZE_T(depthAt) * m_inc});   // a null view: the check is off
    }
    cl->SetComputeRootDescriptorTable(4, {gpu0.ptr + UINT64(depthAt) * m_inc});
    at = first;
    for (uint32_t k = 0; k < m_jobs.size(); ++k, at += kPerDraw) {
        const Job& j = m_jobs[k];
        uint32_t c[40] = {};
        std::memcpy(c, j.c, 10 * 4);
        c[10] = uint32_t(j.iStart); c[11] = j.ibFormat == DXGI_FORMAT_R16_UINT ? 0u : 1u; c[12] = j.indexCount / 3; c[13] = j.idxFirst;
        c[14] = k; c[15] = m_filter ? 1u : 0u;
        std::memcpy(c + 16, &m_ratio, 4); std::memcpy(c + 17, &m_minUnits, 4);
        std::memcpy(c + 18, cam, sizeof cam);   // projA, projB, worldToClip, screen (0: no floating check)
        cl->SetComputeRootDescriptorTable(1, {gpu0.ptr + UINT64(at + 5) * m_inc});
        cl->SetComputeRootDescriptorTable(2, {gpu0.ptr + UINT64(at) * m_inc});
        cl->SetComputeRootDescriptorTable(3, {gpu0.ptr + UINT64(at + 2) * m_inc});
        cl->SetComputeRoot32BitConstants(0, 40, c, 0);
        cl->Dispatch((j.indexCount / 3 + 63) / 64, 1, 1);
    }
    D3D12_RESOURCE_BARRIER u2[2]{};
    u2[0].Type = u2[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    u2[0].UAV.pResource = m_idx[slot].Get(); u2[1].UAV.pResource = m_stats[slot].Get();
    cl->ResourceBarrier(2, u2);
    trans(m_stats[slot].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cl->CopyBufferRegion(m_statsRead[slot].Get(), 0, m_stats[slot].Get(), 0, uint64_t(m_jobs.size()) * kStatWords * 4);
    trans(m_stats[slot].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_meta[slot].clear();
    for (const Job& j : m_jobs) {
        Meta m{j.indexCount, j.c[8], j.c[1], j.posFmt, j.actor, j.diagVb, j.diagBv, j.diagBones};
        m.posOffset = j.c[2]; m.bStride = j.c[5]; m.idxOffset = j.c[6]; m.wOffset = j.c[7];
        m_meta[slot].push_back(std::move(m));
    }
    uint64_t blasAt = 0, scratchAt = 0;
    for (Job& j : m_jobs) {
        D3D12_RAYTRACING_GEOMETRY_DESC g{};
        g.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        g.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        g.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        g.Triangles.VertexBuffer = {m_pool[slot]->GetGPUVirtualAddress() + uint64_t(j.c[9]) * 12, 12};
        g.Triangles.VertexCount = j.c[8];
        g.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        g.Triangles.IndexBuffer = m_idx[slot]->GetGPUVirtualAddress() + uint64_t(j.idxFirst) * 4;
        g.Triangles.IndexCount = j.indexCount;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
        in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
        in.NumDescs = 1; in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY; in.pGeometryDescs = &g;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO pre{};
        m_dev->GetRaytracingAccelerationStructurePrebuildInfo(&in, &pre);
        const uint64_t size = Align256(pre.ResultDataMaxSizeInBytes), scratch = Align256(pre.ScratchDataSizeInBytes);
        j.built = false;
        if (blasAt + size > kBlas || scratchAt + scratch > kScratch) continue;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC b{};
        b.Inputs = in;
        b.DestAccelerationStructureData = m_blas[slot]->GetGPUVirtualAddress() + blasAt;
        b.ScratchAccelerationStructureData = m_scratch->GetGPUVirtualAddress() + scratchAt;
        cl->BuildRaytracingAccelerationStructure(&b, 0, nullptr);
        j.blas = blasAt; j.built = true;
        blasAt += size; scratchAt += scratch;
        ++m_built;
    }
    return true;
}

uint32_t RtSkin::Place(D3D12_RAYTRACING_INSTANCE_DESC* out, uint32_t room, uint32_t slot, uint64_t& tris) const
{
    uint32_t n = 0;
    for (const Job& j : m_jobs) {
        if (!j.built || n >= room) continue;
        D3D12_RAYTRACING_INSTANCE_DESC& x = out[n++];
        std::memset(&x, 0, sizeof x);
        x.Transform[0][0] = x.Transform[1][1] = x.Transform[2][2] = 1.0f;
        x.InstanceMask = 0xFF;
        x.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE | D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
        x.AccelerationStructure = m_blas[slot]->GetGPUVirtualAddress() + j.blas;
        tris += j.indexCount / 3;
    }
    return n;
}

} // namespace raw::bridge
