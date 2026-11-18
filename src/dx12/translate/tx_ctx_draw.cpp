// Draws and dispatches: the dirty D3D11 state becomes D3D12 state here.
#include "tx_context.h"

namespace raw::tx {

static constexpr uint32_t kShaderRead =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
static constexpr uint32_t kGraphicsTableBits = (D_SRV0 * 0x1F) | (D_SAMP0 * 0x1F) | D_UAV;

uint32_t Context::AllocDescs(uint32_t n)
{
    if (descBase_ == ~0u || descUsed_ + n > Device::kDescPage) {
        if (descBase_ != ~0u) rec_.retire.descPages.push_back(descBase_);
        descBase_ = dev_->AllocDescPage();
        descUsed_ = 0;
        if (descBase_ == ~0u) return 0;
    }
    descUsed_ += n;
    return descBase_ + descUsed_ - n;
}

D3D12_GPU_DESCRIPTOR_HANDLE Context::GpuHandle(uint32_t index) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = dev_->gpuHeap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += UINT64(index) * dev_->srvInc;
    return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE Context::CpuHandle(uint32_t index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = dev_->gpuHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += SIZE_T(index) * dev_->srvInc;
    return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE Context::NullSrvFor(const ShaderData* sd, uint32_t slot)
{
    uint8_t dim = sd && slot < sd->srvDim.size() && sd->srvDim[slot] ? sd->srvDim[slot] : uint8_t(4);
    if (dim > 11) dim = 4;
    if (!nullSrv_[dim].ptr) nullSrv_[dim] = dev_->NullSrv(dim);
    return nullSrv_[dim];
}

D3D12_CPU_DESCRIPTOR_HANDLE Context::NullUavFor(const ShaderData* sd, uint32_t slot)
{
    uint8_t dim = sd && slot < sd->uavDim.size() && sd->uavDim[slot] ? sd->uavDim[slot] : uint8_t(4);
    if (dim > 11) dim = 4;
    if (!nullUav_[dim].ptr) nullUav_[dim] = dev_->NullUav(dim);
    return nullUav_[dim];
}

static uint8_t TopoType(D3D11_PRIMITIVE_TOPOLOGY t)
{
    if (t == D3D11_PRIMITIVE_TOPOLOGY_POINTLIST) return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    if (t == D3D11_PRIMITIVE_TOPOLOGY_LINELIST || t == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP ||
        t == D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ || t == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ)
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    if (t == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST || t == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP ||
        t == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ || t == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ)
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    if (t >= D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST) return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
}

bool Context::ResolvePipeline()
{
    PsoKey k;
    memset(&k, 0, sizeof(k));
    ShaderData* sd[5];
    for (int i = 0; i < 5; ++i) { sd[i] = s_.st[i].sh.sd; k.sh[i] = sd[i] ? sd[i]->id : 0; }
    k.topoType = TopoType(s_.topo);
    if (!sd[0] || !k.topoType) return false;
    k.layout = s_.layout ? s_.layout->id : 0;
    k.blend = s_.blend ? s_.blend->id : 0;
    k.ds = s_.ds ? s_.ds->id : 0;
    k.rs = s_.rs ? s_.rs->id : 0;
    k.sampleMask = s_.sampleMask;
    bool strip = s_.topo == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP || s_.topo == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP ||
                 s_.topo == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ || s_.topo == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
    if (strip) k.stripCut = s_.ibFmt == DXGI_FORMAT_R16_UINT ? 1 : s_.ibFmt == DXGI_FORMAT_R32_UINT ? 2 : 0;
    k.rtCount = uint8_t(s_.rtCount);
    const ResCore* first = s_.dsv ? s_.dsv->core : nullptr;
    for (uint32_t i = 0; i < s_.rtCount; ++i) {
        if (!s_.rtv[i]) continue;
        k.rtFmt[i] = uint16_t(s_.rtv[i]->format);
        first = s_.rtv[i]->core;
    }
    k.dsFmt = s_.dsv ? uint16_t(s_.dsv->format) : 0;
    k.samples = uint8_t(first ? first->samples : 1);
    k.quality = uint8_t(first ? first->quality : 0);
    auto it = psoCache_.find(k);
    if (it == psoCache_.end()) {
        StageBind sb[5]{};
        for (int i = 0; i < 5; ++i) if (sd[i]) sb[i] = sd[i]->bind;
        PsoEntry e{};
        e.root = dev_->GetRootSig(sb, false);
        e.pso = e.root ? dev_->GetPso(k, sd, e.root) : nullptr;
        it = psoCache_.emplace(k, e).first;
    }
    cur_ = it->second;
    return cur_.pso != nullptr;
}

void Context::ScanGraphics(const RootSig* root)
{
    Tracker& t = rec_.tracker;
    for (uint32_t i = 0; i < s_.rtCount; ++i)
        if (TRTV* v = s_.rtv[i]) t.RequireRange(v->core->tr, v->range, v->core->mips, v->core->slices, D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (TDSV* v = s_.dsv) {
        SubRange r = v->range;
        r.all = false;
        r.planeMask = 1;
        t.RequireRange(v->core->tr, r, v->core->mips, v->core->slices,
            (v->desc.Flags & D3D11_DSV_READ_ONLY_DEPTH) ? D3D12_RESOURCE_STATE_DEPTH_READ : D3D12_RESOURCE_STATE_DEPTH_WRITE);
        if (v->core->planes > 1) {
            r.planeMask = 2;
            t.RequireRange(v->core->tr, r, v->core->mips, v->core->slices,
                (v->desc.Flags & D3D11_DSV_READ_ONLY_STENCIL) ? D3D12_RESOURCE_STATE_DEPTH_READ : D3D12_RESOURCE_STATE_DEPTH_WRITE);
        }
    }
    for (uint32_t i = 0; i < root->sb[S_PS].uavCount && i < 8; ++i)
        if (TUAV* v = s_.uav[i]) t.RequireRange(v->core->tr, v->range, v->core->mips, v->core->slices, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    for (int st = 0; st < 5; ++st) {
        uint32_t n = root->sb[st].srvCount < s_.st[st].srvHi ? root->sb[st].srvCount : s_.st[st].srvHi;
        for (uint32_t i = 0; i < n; ++i) {
            TSRV* v = s_.st[st].srv[i];
            if (v && v->core->tr) t.RequireRange(v->core->tr, v->range, v->core->mips, v->core->slices, kShaderRead);
        }
    }
    for (uint32_t i = 0; i < s_.vbHi; ++i)
        if (s_.vb[i].buf && s_.vb[i].buf->core.store == Store::Committed)
            t.Require(s_.vb[i].buf->core.tr, 0, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    if (s_.ib.buf && s_.ib.buf->core.store == Store::Committed)
        t.Require(s_.ib.buf->core.tr, 0, D3D12_RESOURCE_STATE_INDEX_BUFFER);
}

void Context::ApplyTables(const RootSig* root, int first, int last, bool compute)
{
    ID3D12GraphicsCommandList* cl = Cl();
    for (int st = first; st <= last; ++st) {
        const auto& p = root->st[compute ? 0 : st];
        const StageBind& b = root->sb[compute ? 0 : st];
        StageState& s = s_.st[st];
        if (p.srv >= 0 && (dirty_ & (D_SRV0 << st))) {
            UINT n = b.srvCount;
            uint32_t base = AllocDescs(n);
            D3D12_CPU_DESCRIPTOR_HANDLE src[128];
            for (UINT i = 0; i < n; ++i) src[i] = (i < s.srvHi && s.srv[i]) ? s.srv[i]->h : NullSrvFor(s.sh.sd, i);
            D3D12_CPU_DESCRIPTOR_HANDLE dst = CpuHandle(base);
            dev_->d3d->CopyDescriptors(1, &dst, &n, n, src, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            if (compute) cl->SetComputeRootDescriptorTable(p.srv, GpuHandle(base));
            else cl->SetGraphicsRootDescriptorTable(p.srv, GpuHandle(base));
        }
        if (p.samp >= 0 && (dirty_ & (D_SAMP0 << st))) {
            uint32_t ids[16];
            for (UINT i = 0; i < b.sampCount; ++i) ids[i] = s.samp[i] ? s.samp[i]->id : 0;
            uint64_t key = Fnv(ids, b.sampCount * sizeof(uint32_t));
            if (key != sampKey_[st]) {
                D3D12_GPU_DESCRIPTOR_HANDLE h = dev_->SamplerTable(ids, b.sampCount);
                if (compute) cl->SetComputeRootDescriptorTable(p.samp, h);
                else cl->SetGraphicsRootDescriptorTable(p.samp, h);
                sampKey_[st] = key;
            }
        }
        if (p.uav >= 0 && (dirty_ & (compute ? D_CSUAV : D_UAV))) {
            TUAV** uav = compute ? s_.csUav : s_.uav;
            UINT n = b.uavCount > 8 ? 8 : b.uavCount;
            uint32_t base = AllocDescs(n);
            D3D12_CPU_DESCRIPTOR_HANDLE src[8];
            for (UINT i = 0; i < n; ++i) src[i] = uav[i] ? uav[i]->h : NullUavFor(s.sh.sd, i);
            D3D12_CPU_DESCRIPTOR_HANDLE dst = CpuHandle(base);
            dev_->d3d->CopyDescriptors(1, &dst, &n, n, src, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            if (compute) cl->SetComputeRootDescriptorTable(p.uav, GpuHandle(base));
            else cl->SetGraphicsRootDescriptorTable(p.uav, GpuHandle(base));
        }
        for (int slot = 0; slot < 14; ++slot) {
            if (p.cb[slot] < 0) continue;
            const BufSlot& c = s.cb[slot];
            D3D12_GPU_VIRTUAL_ADDRESS va = c.chunk ? c.chunk->va : dev_->zeroCb.va;
            bool known = cbVa_[st][slot] != ~0ull;
            if ((dev_->faults & FaultStaleCB) && known && cbBuf_[st][slot] == c.buf) continue;
            if (va == cbVa_[st][slot]) continue;
            if (compute) cl->SetComputeRootConstantBufferView(p.cb[slot], va);
            else cl->SetGraphicsRootConstantBufferView(p.cb[slot], va);
            cbVa_[st][slot] = va;
            cbBuf_[st][slot] = c.buf;
        }
    }
}

void Context::ApplyIA(bool indexed)
{
    ID3D12GraphicsCommandList* cl = Cl();
    if ((dirty_ & D_VB) && s_.vbHi) {
        D3D12_VERTEX_BUFFER_VIEW v[32] = {};
        for (uint32_t i = 0; i < s_.vbHi; ++i) {
            const BufSlot& s = s_.vb[i];
            if (!s.buf || !s.chunk || s.offset >= s.buf->core.bytes) continue;
            v[i] = {s.chunk->va + s.offset, UINT(s.buf->core.bytes - s.offset), s.stride};
        }
        cl->IASetVertexBuffers(0, s_.vbHi, v);
    }
    if (indexed && (dirty_ & D_IB)) {
        if (s_.ib.buf && s_.ib.chunk) {
            D3D12_INDEX_BUFFER_VIEW v{s_.ib.chunk->va + s_.ib.offset, UINT(s_.ib.buf->core.bytes - s_.ib.offset), s_.ibFmt};
            cl->IASetIndexBuffer(&v);
        }
        dirty_ &= ~D_IB;
    }
    if (dirty_ & D_TOPO) cl->IASetPrimitiveTopology(static_cast<D3D12_PRIMITIVE_TOPOLOGY>(s_.topo));
}

void Context::ApplyDynamic()
{
    ID3D12GraphicsCommandList* cl = Cl();
    if ((dirty_ & D_VP) && s_.vpCount)
        cl->RSSetViewports(s_.vpCount, reinterpret_cast<const D3D12_VIEWPORT*>(s_.vp));   // same six floats
    if (dirty_ & D_SC) {
        // D3D12 always scissors. With the D3D11 scissor test off, the rectangle covers any target.
        bool enabled = s_.rs && s_.rs->desc.ScissorEnable;
        D3D12_RECT r[16];
        UINT n = s_.vpCount ? s_.vpCount : 1;
        for (UINT i = 0; i < n; ++i) {
            if (!enabled) r[i] = {0, 0, 16384, 16384};
            else if (i < s_.scCount) r[i] = s_.sc[i];
            else r[i] = {0, 0, 0, 0};
        }
        cl->RSSetScissorRects(n, r);
    }
    if (dirty_ & D_BF) cl->OMSetBlendFactor(s_.factor);
    if (dirty_ & D_SREF) cl->OMSetStencilRef(s_.stencilRef);
}

bool Context::PrepareDraw(bool indexed)
{
    if ((dirty_ & D_PSO) || !cur_.pso) {
        if (!ResolvePipeline()) return false;
    }
    ID3D12GraphicsCommandList* cl = Cl();
    if (root_ != cur_.root) {
        cl->SetGraphicsRootSignature(cur_.root->rs.Get());
        root_ = cur_.root;
        memset(cbVa_, 0xFF, sizeof(cbVa_[0]) * 5);
        for (int i = 0; i < 5; ++i) sampKey_[i] = ~0ull;
        dirty_ |= kGraphicsTableBits;
    }
    if (pso_ != cur_.pso) { cl->SetPipelineState(cur_.pso); pso_ = cur_.pso; }
    if (dirty_ & D_SCAN) ScanGraphics(root_);
    rec_.tracker.Flush(cl);
    if (dirty_ & D_RT) {
        D3D12_CPU_DESCRIPTOR_HANDLE h[8];
        for (uint32_t i = 0; i < s_.rtCount; ++i) h[i] = s_.rtv[i] ? s_.rtv[i]->h : dev_->nullRtv;
        cl->OMSetRenderTargets(s_.rtCount, h, FALSE, s_.dsv ? &s_.dsv->h : nullptr);
    }
    ApplyTables(root_, S_VS, S_PS, false);
    ApplyIA(indexed);
    ApplyDynamic();
    dirty_ &= ~(D_PSO | D_RT | D_VP | D_SC | D_BF | D_SREF | D_TOPO | D_VB | D_SCAN | kGraphicsTableBits);
    ++rec_.cmdCount;
    ++rec_.draws;
    return true;
}

STDMETHODIMP_(void) Context::Draw(UINT n, UINT start)
{
    if (PrepareDraw(false)) Cl()->DrawInstanced(n, 1, start, 0);
}
STDMETHODIMP_(void) Context::DrawIndexed(UINT n, UINT start, INT base)
{
    if (PrepareDraw(true)) Cl()->DrawIndexedInstanced(n, 1, start, base, 0);
}
STDMETHODIMP_(void) Context::DrawInstanced(UINT n, UINT inst, UINT start, UINT startInst)
{
    if (PrepareDraw(false)) Cl()->DrawInstanced(n, inst, start, startInst);
}
STDMETHODIMP_(void) Context::DrawIndexedInstanced(UINT n, UINT inst, UINT start, INT base, UINT startInst)
{
    if (PrepareDraw(true)) Cl()->DrawIndexedInstanced(n, inst, start, base, startInst);
}
STDMETHODIMP_(void) Context::DrawAuto() { CountUnsupported(dev_, "DrawAuto (stream output)"); }

}  // namespace raw::tx
