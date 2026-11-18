// Resource contents: staging readback for DEFINE records, and the data
// carried by Unmap and UpdateSubresource records. All of them use the SUBDATA
// structure (see the format doc): rows are stored tightly packed.
#include "rec_emit.h"
#include "rec_util.h"

namespace raw::rec
{
void Recorder::WriteSub(const u8* src, u32 rowPitch, u32 slicePitch, const SubGeom& g)
{
    const u64 total = static_cast<u64>(g.rowBytes) * g.rows * g.slices;
    m_writer.U32(SUB_DATA);
    m_writer.U32(WHY_NONE);
    m_writer.U32(g.rowBytes);
    m_writer.U32(g.rows);
    m_writer.U32(g.slices);
    m_writer.U32(0);
    m_writer.BlobBegin(total);
    for (u32 z = 0; z < g.slices; ++z)
        for (u32 y = 0; y < g.rows; ++y)
            m_writer.Raw(src + static_cast<size_t>(z) * slicePitch + static_cast<size_t>(y) * rowPitch,
                         g.rowBytes);
    m_writer.Pad8();
    m_contentBytes += total;
}

void Recorder::WriteSubUndefined(u64 id, u32 sub, u32 reason)
{
    m_writer.U32(SUB_UNDEFINED);
    m_writer.U32(reason);
    for (int i = 0; i < 4; ++i) m_writer.U32(0);
    m_undefined.push_back({id, sub, reason});
}

void Recorder::WriteBufferContents(ID3D11Buffer* b, const D3D11_BUFFER_DESC& d, u64 id)
{
    D3D11_BUFFER_DESC sd{d.ByteWidth, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
    ID3D11Buffer* twin = nullptr;
    if (FAILED(m_dev->CreateBuffer(&sd, nullptr, &twin))) {
        WriteSubUndefined(id, 0, WHY_STAGING_CREATE_FAILED);
        return;
    }
    m_ctx->CopyResource(twin, b);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(m_ctx->Map(twin, 0, D3D11_MAP_READ, 0, &m))) {
        SubGeom g{d.ByteWidth, 1, 1, true};
        WriteSub(static_cast<const u8*>(m.pData), d.ByteWidth, d.ByteWidth, g);
        m_ctx->Unmap(twin, 0);
    } else {
        WriteSubUndefined(id, 0, WHY_MAP_FAILED);
    }
    twin->Release();
}

void Recorder::WriteTextureContents(ID3D11Resource* r, const TexDesc& t, u64 id)
{
    u32 why = WHY_NONE;
    if (t.sampleCount > 1) why = WHY_MULTISAMPLED;
    else if (FormatInfo(static_cast<DXGI_FORMAT>(t.format)).bytes == 0) why = WHY_UNKNOWN_FORMAT;

    ID3D11Resource* twin = nullptr;
    if (why == WHY_NONE) {
        TexDesc s = t;
        s.usage = D3D11_USAGE_STAGING;
        s.bind = 0;
        s.cpu = D3D11_CPU_ACCESS_READ;
        s.misc = 0;
        if (FAILED(CreateTexture(m_dev, s, nullptr, &twin))) why = WHY_STAGING_CREATE_FAILED;
    }
    if (twin) m_ctx->CopyResource(twin, r);
    for (u32 sub = 0; sub < t.SubCount(); ++sub) {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (!twin) {
            WriteSubUndefined(id, sub, why);
        } else if (SUCCEEDED(m_ctx->Map(twin, sub, D3D11_MAP_READ, 0, &m))) {
            WriteSub(static_cast<const u8*>(m.pData), m.RowPitch, m.DepthPitch,
                     TexSubGeom(t, sub, nullptr));
            m_ctx->Unmap(twin, sub);
        } else {
            WriteSubUndefined(id, sub, WHY_MAP_FAILED);
        }
    }
    SafeRelease(twin);
}

// Unmap of a write map: the whole mapped subresource, read before the real
// Unmap. A read map carries no data. A map taken before the capture began has
// no pointer on record, so its bytes are reported as undefined.
void Recorder::EmitUnmap(ID3D11Resource* r, u32 sub)
{
    Emit e(*this, OP_UNMAP);
    e.id(r, K_RESOURCE).u(sub).u(0);
    for (size_t i = 0; i < m_maps.size(); ++i) {
        if (m_maps[i].res != r || m_maps[i].sub != sub) continue;
        const PendingMap pm = m_maps[i];
        m_maps.erase(m_maps.begin() + static_cast<ptrdiff_t>(i));
        const SubGeom g = ResourceSubGeom(r, sub, nullptr);
        if (!pm.write) WriteSubNone();
        else if (g.ok && pm.data) WriteSub(pm.data, pm.rowPitch, pm.depthPitch, g);
        else WriteSubUndefined(GetId(r), sub, WHY_UNKNOWN_FORMAT);
        return;
    }
    WriteSubUndefined(GetId(r), sub, WHY_NOT_MAPPED_IN_CAPTURE);
}

void Recorder::WriteSubNone()
{
    m_writer.U32(SUB_NONE);
    for (int i = 0; i < 5; ++i) m_writer.U32(0);
}

void Recorder::EmitUpdate(ID3D11Resource* r, u32 sub, const D3D11_BOX* box, const void* data,
                          u32 rowPitch, u32 depthPitch)
{
    const D3D11_BOX none{};
    const D3D11_BOX& b = box ? *box : none;
    Emit e(*this, OP_UPDATE_SUBRESOURCE);
    e.id(r, K_RESOURCE).u(sub).u(box ? 1 : 0);
    e.u(b.left).u(b.top).u(b.front).u(b.right).u(b.bottom).u(b.back);
    const SubGeom g = ResourceSubGeom(r, sub, box);
    if (g.ok && data) WriteSub(static_cast<const u8*>(data), rowPitch, depthPitch, g);
    else WriteSubUndefined(GetId(r), sub, WHY_UNKNOWN_FORMAT);
}
} // namespace raw::rec
