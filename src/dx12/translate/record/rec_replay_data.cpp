// Replayer: draws, clears, resource data, queries.
#include "rec_replayer.h"
#include "rec_util.h"
#include <algorithm>

namespace raw::rec
{
HRESULT Replayer::Draw(const Record& r)
{
    Cursor c(r);
    const u32 op = r.op;
    if (op >= OP_CLEAR_RTV) return Clear(op, c);
    if (op == OP_DRAW_AUTO) { m_ctx->DrawAuto(); return S_OK; }
    if (op == OP_DRAW_INDEXED_INSTANCED_INDIRECT || op == OP_DRAW_INSTANCED_INDIRECT
        || op == OP_DISPATCH_INDIRECT) {
        ID3D11Buffer* args = static_cast<ID3D11Buffer*>(Get(c.U64(), K_BUFFER, K_BUFFER));
        const u32 offset = c.U32();
        if (!c.ok) return Short();
        if (!args) return S_OK;
        if (op == OP_DRAW_INDEXED_INSTANCED_INDIRECT) m_ctx->DrawIndexedInstancedIndirect(args, offset);
        else if (op == OP_DRAW_INSTANCED_INDIRECT) m_ctx->DrawInstancedIndirect(args, offset);
        else m_ctx->DispatchIndirect(args, offset);
        return S_OK;
    }
    u32 a[5] = {};
    const u32 n = op == OP_DRAW ? 2 : op == OP_DRAW_INSTANCED ? 4 : op == OP_DRAW_INDEXED_INSTANCED ? 5 : 3;
    for (u32 i = 0; i < n; ++i) a[i] = c.U32();
    if (!c.ok) return Short();
    switch (op) {
    case OP_DRAW: m_ctx->Draw(a[0], a[1]); break;
    case OP_DRAW_INDEXED: m_ctx->DrawIndexed(a[0], a[1], static_cast<INT>(a[2])); break;
    case OP_DRAW_INSTANCED: m_ctx->DrawInstanced(a[0], a[1], a[2], a[3]); break;
    case OP_DRAW_INDEXED_INSTANCED:
        m_ctx->DrawIndexedInstanced(a[0], a[1], a[2], static_cast<INT>(a[3]), a[4]); break;
    case OP_DISPATCH: m_ctx->Dispatch(a[0], a[1], a[2]); break;
    default: break;
    }
    return S_OK;
}

HRESULT Replayer::Clear(u32 op, Cursor& c)
{
    const u64 id = c.U64();
    if (op == OP_CLEAR_DSV) {
        const u32 flags = c.U32();
        const float depth = c.F32();
        const u32 stencil = c.U32();
        if (!c.ok) return Short();
        if (ID3D11DeviceChild* v = Get(id, K_DSV, K_DSV))
            m_ctx->ClearDepthStencilView(static_cast<ID3D11DepthStencilView*>(v), flags, depth,
                                         static_cast<UINT8>(stencil));
        return S_OK;
    }
    if (op == OP_CLEAR_UAV_UINT) {
        const UINT v4[4] = {c.U32(), c.U32(), c.U32(), c.U32()};
        if (!c.ok) return Short();
        if (ID3D11DeviceChild* v = Get(id, K_UAV, K_UAV))
            m_ctx->ClearUnorderedAccessViewUint(static_cast<ID3D11UnorderedAccessView*>(v), v4);
        return S_OK;
    }
    const FLOAT f4[4] = {c.F32(), c.F32(), c.F32(), c.F32()};
    if (!c.ok) return Short();
    if (op == OP_CLEAR_RTV) {
        if (ID3D11DeviceChild* v = Get(id, K_RTV, K_RTV))
            m_ctx->ClearRenderTargetView(static_cast<ID3D11RenderTargetView*>(v), f4);
    } else if (op == OP_CLEAR_UAV_FLOAT) {
        if (ID3D11DeviceChild* v = Get(id, K_UAV, K_UAV))
            m_ctx->ClearUnorderedAccessViewFloat(static_cast<ID3D11UnorderedAccessView*>(v), f4);
    }
    return S_OK;
}

HRESULT Replayer::Data(const Record& r)
{
    Cursor c(r);
    switch (r.op) {
    case OP_MAP: return MapCall(c);
    case OP_UNMAP: return UnmapCall(c);
    case OP_UPDATE_SUBRESOURCE: return UpdateCall(c);
    case OP_COPY_RESOURCE: case OP_COPY_SUBRESOURCE_REGION: case OP_COPY_STRUCTURE_COUNT:
    case OP_RESOLVE_SUBRESOURCE: case OP_GENERATE_MIPS:
        return CopyCall(r.op, c);
    case OP_BEGIN: case OP_END: return Query(r.op, c);
    case OP_GET_DATA: return GetDataCall(c);
    case OP_FLUSH: m_ctx->Flush(); return S_OK;
    case OP_EXECUTE_COMMAND_LIST: case OP_FINISH_COMMAND_LIST: ++m_res.unsupported; return S_OK;
    default: return S_OK;
    }
}

HRESULT Replayer::MapCall(Cursor& c)
{
    ID3D11Resource* res = Res(c);
    const u32 sub = c.U32(), type = c.U32(), flags = c.U32();
    if (!c.ok) return Short();
    if (!res) return S_OK;
    Mapped m{res, sub, {}, type != D3D11_MAP_READ};
    const HRESULT hr = m_ctx->Map(res, sub, static_cast<D3D11_MAP>(type),
                                  flags & ~static_cast<u32>(D3D11_MAP_FLAG_DO_NOT_WAIT), &m.m);
    if (SUCCEEDED(hr)) m_maps.push_back(m);
    return hr;
}

HRESULT Replayer::UnmapCall(Cursor& c)
{
    ID3D11Resource* res = Res(c);
    const u32 sub = c.U32();
    c.U32();
    const SubData s = ReadSubData(c);
    if (!c.ok) return Short();
    const auto it = std::find_if(m_maps.begin(), m_maps.end(),
                                 [&](const Mapped& m) { return m.res == res && m.sub == sub; });
    if (it == m_maps.end()) {           // the map was opened before the capture began
        if (res) ++m_res.contentsNotRestored;
        return S_OK;
    }
    HRESULT hr = S_OK;
    if (it->write && s.state == SUB_DATA) {
        const SubGeom g = ResourceSubGeom(res, sub, nullptr);
        if (g.ok && g.rowBytes == s.rowBytes && g.rows == s.rows && g.slices == s.slices)
            CopyRowsOut(it->m.pData, it->m.RowPitch, it->m.DepthPitch, s.data, s.rowBytes, s.rows, s.slices);
        else
            hr = Fail(E_INVALIDARG, "mapped data does not fit the subresource");
    } else if (it->write) {
        ++m_res.contentsNotRestored;
    }
    m_ctx->Unmap(res, sub);
    m_maps.erase(it);
    return hr;
}

HRESULT Replayer::UpdateCall(Cursor& c)
{
    ID3D11Resource* res = Res(c);
    const u32 sub = c.U32(), hasBox = c.U32();
    D3D11_BOX box{};
    box.left = c.U32(); box.top = c.U32(); box.front = c.U32();
    box.right = c.U32(); box.bottom = c.U32(); box.back = c.U32();
    const SubData s = ReadSubData(c);
    if (!c.ok) return Short();
    if (!res) return S_OK;
    if (s.state != SUB_DATA) { ++m_res.contentsNotRestored; return S_OK; }
    const SubGeom g = ResourceSubGeom(res, sub, hasBox ? &box : nullptr);
    if (!g.ok || g.rowBytes != s.rowBytes || g.rows != s.rows || g.slices != s.slices)
        return Fail(E_INVALIDARG, "update data does not fit the box");
    m_ctx->UpdateSubresource(res, sub, hasBox ? &box : nullptr, s.data, s.rowBytes, s.rowBytes * s.rows);
    return S_OK;
}

HRESULT Replayer::CopyCall(u32 op, Cursor& c)
{
    if (op == OP_GENERATE_MIPS) {
        ID3D11DeviceChild* srv = Get(c.U64(), K_SRV, K_SRV);
        if (!c.ok) return Short();
        if (srv) m_ctx->GenerateMips(static_cast<ID3D11ShaderResourceView*>(srv));
        return S_OK;
    }
    ID3D11Resource* dst = Res(c);
    if (op == OP_COPY_RESOURCE) {
        ID3D11Resource* src = Res(c);
        if (!c.ok) return Short();
        if (dst && src) m_ctx->CopyResource(dst, src);
        return S_OK;
    }
    if (op == OP_COPY_STRUCTURE_COUNT) {
        const u32 offset = c.U32();
        c.U32();
        ID3D11DeviceChild* view = Get(c.U64(), K_UAV, K_UAV);
        if (!c.ok) return Short();
        if (dst && view)
            m_ctx->CopyStructureCount(static_cast<ID3D11Buffer*>(dst), offset,
                                      static_cast<ID3D11UnorderedAccessView*>(view));
        return S_OK;
    }
    if (op == OP_RESOLVE_SUBRESOURCE) {
        const u32 dstSub = c.U32();
        c.U32();
        ID3D11Resource* src = Res(c);
        const u32 srcSub = c.U32(), fmt = c.U32();
        if (!c.ok) return Short();
        if (dst && src) m_ctx->ResolveSubresource(dst, dstSub, src, srcSub, static_cast<DXGI_FORMAT>(fmt));
        return S_OK;
    }
    return CopyRegion(dst, c);
}

HRESULT Replayer::CopyRegion(ID3D11Resource* dst, Cursor& c)
{
    const u32 dstSub = c.U32(), x = c.U32(), y = c.U32(), z = c.U32();
    ID3D11Resource* src = Res(c);
    const u32 srcSub = c.U32(), hasBox = c.U32();
    D3D11_BOX box{};
    box.left = c.U32(); box.top = c.U32(); box.front = c.U32();
    box.right = c.U32(); box.bottom = c.U32(); box.back = c.U32();
    if (!c.ok) return Short();
    if (dst && src) m_ctx->CopySubresourceRegion(dst, dstSub, x, y, z, src, srcSub, hasBox ? &box : nullptr);
    return S_OK;
}

HRESULT Replayer::Query(u32 op, Cursor& c)
{
    const u64 id = c.U64();
    if (!c.ok) return Short();
    ID3D11Asynchronous* a = static_cast<ID3D11Asynchronous*>(Get(id, K_QUERY, K_PREDICATE));
    if (!a) return S_OK;
    if (op == OP_BEGIN) {
        m_ctx->Begin(a);
    } else {
        m_ctx->End(a);
        if (std::find(m_ended.begin(), m_ended.end(), id) == m_ended.end()) m_ended.push_back(id);
    }
    return S_OK;
}

// GetData is replayed as a blocking poll, so later calls see finished work.
// The result is not compared with the capture. A query that never had End
// issued in this replay (it was issued in an earlier frame) is not polled.
HRESULT Replayer::GetDataCall(Cursor& c)
{
    const u64 id = c.U64();
    const u32 size = c.U32();
    c.U32();                            // recorded flags; the poll always flushes
    if (!c.ok || size > 4096) return Short();
    ID3D11Asynchronous* a = static_cast<ID3D11Asynchronous*>(Get(id, K_QUERY, K_PREDICATE));
    if (!a) return S_OK;
    if (std::find(m_ended.begin(), m_ended.end(), id) == m_ended.end()) {
        ++m_res.getDataNotIssued;
        return S_OK;
    }
    u8 buf[4096];
    const ULONGLONG start = GetTickCount64();
    ++m_res.getDataPolls;
    while (m_ctx->GetData(a, size ? buf : nullptr, size, 0) == S_FALSE) {
        if (GetTickCount64() - start > m_opt.getDataTimeoutMs) {
            ++m_res.getDataTimeouts;
            break;
        }
        Sleep(0);
    }
    return S_OK;
}
} // namespace raw::rec
