// RecContext, part 2: draws, clears, resource data, queries and the rest.
// A record is written before the real call so that an object referenced for
// the first time is defined with the contents it had before the call.
#include "rec_emit.h"
#include "rec_util.h"
#include "rec_wrappers.h"

namespace raw::rec
{
#define RAW_REC_CAP if (m_rec->Capturing())

// ---- draws -------------------------------------------------------------------
void RecContext::DrawIndexed(UINT indexCount, UINT startIndex, INT baseVertex)
{
    RAW_REC_CAP { Emit e(*m_rec, OP_DRAW_INDEXED); e.u(indexCount).u(startIndex).u(static_cast<u32>(baseVertex)); }
    m_real->DrawIndexed(indexCount, startIndex, baseVertex);
}
void RecContext::Draw(UINT vertexCount, UINT startVertex)
{
    RAW_REC_CAP { Emit e(*m_rec, OP_DRAW); e.u(vertexCount).u(startVertex); }
    m_real->Draw(vertexCount, startVertex);
}
void RecContext::DrawIndexedInstanced(UINT indexCount, UINT instances, UINT startIndex, INT baseVertex, UINT startInstance)
{
    RAW_REC_CAP {
        Emit e(*m_rec, OP_DRAW_INDEXED_INSTANCED);
        e.u(indexCount).u(instances).u(startIndex).u(static_cast<u32>(baseVertex)).u(startInstance);
    }
    m_real->DrawIndexedInstanced(indexCount, instances, startIndex, baseVertex, startInstance);
}
void RecContext::DrawInstanced(UINT vertexCount, UINT instances, UINT startVertex, UINT startInstance)
{
    RAW_REC_CAP { Emit e(*m_rec, OP_DRAW_INSTANCED); e.u(vertexCount).u(instances).u(startVertex).u(startInstance); }
    m_real->DrawInstanced(vertexCount, instances, startVertex, startInstance);
}
void RecContext::DrawAuto()
{
    RAW_REC_CAP { Emit e(*m_rec, OP_DRAW_AUTO); }
    m_real->DrawAuto();
}
void RecContext::DrawIndexedInstancedIndirect(ID3D11Buffer* args, UINT offset)
{
    RAW_REC_CAP { Emit e(*m_rec, OP_DRAW_INDEXED_INSTANCED_INDIRECT); e.id(args, K_BUFFER).u(offset); }
    m_real->DrawIndexedInstancedIndirect(args, offset);
}
void RecContext::DrawInstancedIndirect(ID3D11Buffer* args, UINT offset)
{
    RAW_REC_CAP { Emit e(*m_rec, OP_DRAW_INSTANCED_INDIRECT); e.id(args, K_BUFFER).u(offset); }
    m_real->DrawInstancedIndirect(args, offset);
}
void RecContext::Dispatch(UINT x, UINT y, UINT z)
{
    RAW_REC_CAP { Emit e(*m_rec, OP_DISPATCH); e.u(x).u(y).u(z); }
    m_real->Dispatch(x, y, z);
}
void RecContext::DispatchIndirect(ID3D11Buffer* args, UINT offset)
{
    RAW_REC_CAP { Emit e(*m_rec, OP_DISPATCH_INDIRECT); e.id(args, K_BUFFER).u(offset); }
    m_real->DispatchIndirect(args, offset);
}

// ---- clears ------------------------------------------------------------------
void RecContext::ClearRenderTargetView(ID3D11RenderTargetView* rtv, const FLOAT rgba[4])
{
    RAW_REC_CAP { Emit e(*m_rec, OP_CLEAR_RTV); e.id(rtv, K_RTV).fs(4, rgba, 0.0f); }
    m_real->ClearRenderTargetView(rtv, rgba);
}
void RecContext::ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView* uav, const UINT values[4])
{
    RAW_REC_CAP { Emit e(*m_rec, OP_CLEAR_UAV_UINT); e.id(uav, K_UAV).us(4, values, 0); }
    m_real->ClearUnorderedAccessViewUint(uav, values);
}
void RecContext::ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView* uav, const FLOAT values[4])
{
    RAW_REC_CAP { Emit e(*m_rec, OP_CLEAR_UAV_FLOAT); e.id(uav, K_UAV).fs(4, values, 0.0f); }
    m_real->ClearUnorderedAccessViewFloat(uav, values);
}
void RecContext::ClearDepthStencilView(ID3D11DepthStencilView* dsv, UINT flags, FLOAT depth, UINT8 stencil)
{
    RAW_REC_CAP { Emit e(*m_rec, OP_CLEAR_DSV); e.id(dsv, K_DSV).u(flags).f(depth).u(stencil); }
    m_real->ClearDepthStencilView(dsv, flags, depth, stencil);
}
void RecContext::ClearState()
{
    RAW_REC_CAP { Emit e(*m_rec, OP_CLEAR_STATE); }
    m_real->ClearState();
}
void RecContext::Flush()
{
    RAW_REC_CAP { Emit e(*m_rec, OP_FLUSH); }
    m_real->Flush();
}

// ---- resource data -----------------------------------------------------------
HRESULT RecContext::Map(ID3D11Resource* r, UINT sub, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* out)
{
    const bool cap = m_rec->Capturing();
    if (cap) m_rec->Touch(r, K_RESOURCE);       // define with the contents before the map
    D3D11_MAPPED_SUBRESOURCE m{};
    const HRESULT hr = m_real->Map(r, sub, type, flags, &m);
    if (out) *out = m;
    if (cap && SUCCEEDED(hr)) m_rec->EmitMap(r, sub, type, flags, m);
    return hr;
}
void RecContext::Unmap(ID3D11Resource* r, UINT sub)
{
    RAW_REC_CAP m_rec->EmitUnmap(r, sub);       // reads the mapped bytes, so before the real Unmap
    m_real->Unmap(r, sub);
}
void RecContext::UpdateSubresource(ID3D11Resource* r, UINT sub, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch)
{
    RAW_REC_CAP m_rec->EmitUpdate(r, sub, box, data, rowPitch, depthPitch);
    m_real->UpdateSubresource(r, sub, box, data, rowPitch, depthPitch);
}
void RecContext::CopySubresourceRegion(ID3D11Resource* dst, UINT dstSub, UINT x, UINT y, UINT z, ID3D11Resource* src, UINT srcSub, const D3D11_BOX* box)
{
    RAW_REC_CAP {
        const D3D11_BOX none{};
        const D3D11_BOX& b = box ? *box : none;
        Emit e(*m_rec, OP_COPY_SUBRESOURCE_REGION);
        e.id(dst, K_RESOURCE).u(dstSub).u(x).u(y).u(z).id(src, K_RESOURCE).u(srcSub).u(box ? 1 : 0);
        e.u(b.left).u(b.top).u(b.front).u(b.right).u(b.bottom).u(b.back);
    }
    m_real->CopySubresourceRegion(dst, dstSub, x, y, z, src, srcSub, box);
}
void RecContext::CopyResource(ID3D11Resource* dst, ID3D11Resource* src)
{
    RAW_REC_CAP { Emit e(*m_rec, OP_COPY_RESOURCE); e.id(dst, K_RESOURCE).id(src, K_RESOURCE); }
    m_real->CopyResource(dst, src);
}
void RecContext::CopyStructureCount(ID3D11Buffer* dst, UINT offset, ID3D11UnorderedAccessView* src)
{
    RAW_REC_CAP { Emit e(*m_rec, OP_COPY_STRUCTURE_COUNT); e.id(dst, K_BUFFER).u(offset).u(0).id(src, K_UAV); }
    m_real->CopyStructureCount(dst, offset, src);
}
void RecContext::ResolveSubresource(ID3D11Resource* dst, UINT dstSub, ID3D11Resource* src, UINT srcSub, DXGI_FORMAT fmt)
{
    RAW_REC_CAP {
        Emit e(*m_rec, OP_RESOLVE_SUBRESOURCE);
        e.id(dst, K_RESOURCE).u(dstSub).u(0).id(src, K_RESOURCE).u(srcSub).u(fmt);
    }
    m_real->ResolveSubresource(dst, dstSub, src, srcSub, fmt);
}
void RecContext::GenerateMips(ID3D11ShaderResourceView* srv)
{
    RAW_REC_CAP { Emit e(*m_rec, OP_GENERATE_MIPS); e.id(srv, K_SRV); }
    m_real->GenerateMips(srv);
}
void RecContext::SetResourceMinLOD(ID3D11Resource* r, FLOAT lod)
{
    RAW_REC_CAP { Emit e(*m_rec, OP_SET_RESOURCE_MIN_LOD); e.id(r, K_RESOURCE).f(lod); }
    m_real->SetResourceMinLOD(r, lod);
}
FLOAT RecContext::GetResourceMinLOD(ID3D11Resource* r) { return m_real->GetResourceMinLOD(r); }

// ---- queries, predication, command lists ---------------------------------------
void RecContext::Begin(ID3D11Asynchronous* a)
{
    RAW_REC_CAP { Emit e(*m_rec, OP_BEGIN); e.id(a, AsyncKind(a)); }
    m_real->Begin(a);
}
void RecContext::End(ID3D11Asynchronous* a)
{
    RAW_REC_CAP { Emit e(*m_rec, OP_END); e.id(a, AsyncKind(a)); }
    m_real->End(a);
}
HRESULT RecContext::GetData(ID3D11Asynchronous* a, void* data, UINT size, UINT flags)
{
    RAW_REC_CAP { Emit e(*m_rec, OP_GET_DATA); e.id(a, AsyncKind(a)).u(size).u(flags); }
    return m_real->GetData(a, data, size, flags);
}
void RecContext::SetPredication(ID3D11Predicate* p, BOOL value)
{
    RAW_REC_CAP { Emit e(*m_rec, OP_SET_PREDICATION); e.id(p, K_PREDICATE).u(value ? 1 : 0); }
    m_real->SetPredication(p, value);
}
void RecContext::GetPredication(ID3D11Predicate** p, BOOL* value) { m_real->GetPredication(p, value); }

// Command lists come from deferred contexts, which are not recorded. The
// stream keeps a marker so a reader can see that the frame used them.
void RecContext::ExecuteCommandList(ID3D11CommandList* list, BOOL restore)
{
    RAW_REC_CAP { Emit e(*m_rec, OP_EXECUTE_COMMAND_LIST); e.u(UNS_EXECUTE_COMMAND_LIST).u(restore ? 1 : 0); }
    m_real->ExecuteCommandList(list, restore);
}
HRESULT RecContext::FinishCommandList(BOOL restore, ID3D11CommandList** list)
{
    RAW_REC_CAP { Emit e(*m_rec, OP_FINISH_COMMAND_LIST); e.u(UNS_FINISH_COMMAND_LIST).u(restore ? 1 : 0); }
    return m_real->FinishCommandList(restore, list);
}
} // namespace raw::rec
