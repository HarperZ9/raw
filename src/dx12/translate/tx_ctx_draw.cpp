// Draws and dispatches: the dirty D3D11 state becomes D3D12 state here.
#include "tx_context.h"

namespace raw::tx {

static constexpr uint32_t kShaderRead =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
static constexpr uint32_t kGraphicsTableBits = (D_SRV0 * 0x1F) | (D_SAMP0 * 0x1F) | D_UAV;

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
    if (cur_.pso && k == lastKey_) return true;   // shaders rebound to the same pipeline: no lookup
    lastKey_ = k;
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
    const uint32_t d = dirty_;
    if (d & D_SCAN_OM) {
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
    }
    for (int st = 0; st < 5; ++st) {
        if (!(d & (D_SCAN_SRV0 << st))) continue;
        StageState& ss = s_.st[st];
        uint32_t n = root->sb[st].srvCount < ss.srvHi ? root->sb[st].srvCount : ss.srvHi;
        // Only the slots bound anew, unless something may have moved a bound resource out of the read state.
        uint64_t slots = (d & D_SCAN_ALLSRV) || n > 64 ? ~0ull : ss.srvScan;
        if (n < 64) slots &= (1ull << n) - 1;
        ss.srvScan = 0;
        if (slots == ~0ull) {
            for (uint32_t i = 0; i < n; ++i) {
                TSRV* v = ss.srv[i];
                if (v && v->core->tr) t.RequireRange(v->core->tr, v->range, v->core->mips, v->core->slices, kShaderRead);
            }
            continue;
        }
        for (; slots; slots &= slots - 1) {
            TSRV* v = ss.srv[std::countr_zero(slots)];
            if (v && v->core->tr) t.RequireRange(v->core->tr, v->range, v->core->mips, v->core->slices, kShaderRead);
        }
        for (uint32_t cb = 0; cb < 14; ++cb) {
            TBuffer* b = ss.cb[cb].buf;
            if (b && b->core.store == Store::Committed && b->core.tr)
                t.Require(b->core.tr, 0, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        }
    }
    if (!(d & D_SCAN_IA)) return;
    for (uint32_t i = 0; i < s_.vbHi; ++i)
        if (s_.vb[i].buf && s_.vb[i].buf->core.store == Store::Committed && s_.vb[i].buf->core.tr)
            t.Require(s_.vb[i].buf->core.tr, 0, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    if (s_.ib.buf && s_.ib.buf->core.store == Store::Committed && s_.ib.buf->core.tr)
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
            uint64_t keys[128];
            for (UINT i = 0; i < n; ++i) {
                const TSRV* v = i < s.srvHi ? s.srv[i] : nullptr;
                uint8_t dim = s.sh.sd && i < s.sh.sd->srvDim.size() ? s.sh.sd->srvDim[i] : 0;
                keys[i] = v ? v->uid : (uint64_t(1) << 63) | dim;   // a null descriptor: its dimension
            }
            const uint64_t hash = HashBytes(keys, n * sizeof(uint64_t), n);
            uint32_t base = tables_.Find(keys, n, hash);
            if (base == ~0u) {
                ++tables_.misses;
                base = AllocDescs(n);
                D3D12_CPU_DESCRIPTOR_HANDLE src[128];
                for (UINT i = 0; i < n; ++i) src[i] = (i < s.srvHi && s.srv[i]) ? s.srv[i]->h : NullSrvFor(s.sh.sd, i);
                D3D12_CPU_DESCRIPTOR_HANDLE dst = CpuHandle(base);
                dev_->d3d->CopyDescriptors(1, &dst, &n, n, src, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                tables_.Put(keys, n, hash, base);
            } else {
                ++tables_.hits;
            }
            const D3D12_GPU_DESCRIPTOR_HANDLE h = GpuHandle(base);
            if (h.ptr != srvTable_[st].ptr) {   // the same table on the same root signature: nothing to set
                if (compute) cl->SetComputeRootDescriptorTable(p.srv, h);
                else cl->SetGraphicsRootDescriptorTable(p.srv, h);
                srvTable_[st] = h;
            }
        }
        if (p.samp >= 0 && (dirty_ & (D_SAMP0 << st))) {
            uint32_t ids[16];
            for (UINT i = 0; i < b.sampCount; ++i) ids[i] = s.samp[i] ? s.samp[i]->id : 0;
            uint64_t key = HashBytes(ids, b.sampCount * sizeof(uint32_t));
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
        for (uint32_t bits = p.cbBits; bits; bits &= bits - 1) {
            const int slot = std::countr_zero(bits);
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
        for (int i = 0; i < 5; ++i) { sampKey_[i] = ~0ull; srvTable_[i].ptr = 0; }   // root arguments start unset
        // Another root signature can read more shader-resource or UAV slots than the last scan covered.
        dirty_ |= kGraphicsTableBits | D_SCAN_OM | (D_SCAN_SRV0 * 0x1F) | D_SCAN_ALLSRV;
    }
    if (pso_ != cur_.pso) { cl->SetPipelineState(cur_.pso); pso_ = cur_.pso; }
    if (dirty_ & D_SCAN) ScanGraphics(root_);   // any of the scan parts
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
    TX_TRACE("Context::Draw");
    if (pool_ && !replay_ && StreamDraw()) { SHead(SO_Draw, 0, 0, n); seg_->words.push_back(start); return SegDrawn(); }
    if (seg_ || stale_) EndStream(__func__);
    if (PrepareDraw(false)) Cl()->DrawInstanced(n, 1, start, 0);
}
std::shared_ptr<const SubdivEntry> Context::TrySubdivideDraw(UINT n, UINT start, INT base)
{
    if (!dev_->geometrySubdiv || !dev_->subdiv.Ready()) return nullptr;
    if (s_.topo != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST) return nullptr;
    if (!s_.dsv || (s_.dsv->desc.Flags & D3D11_DSV_READ_ONLY_DEPTH) != 0) return nullptr;
    if (s_.st[S_HS].sh.sd || s_.st[S_GS].sh.sd || !s_.layout || !s_.layout->id) return nullptr;
    if (n < 12u || n > 36000u || (n % 3u) != 0u) return nullptr;
    const BufSlot& vb = s_.vb[0];
    const BufSlot& ib = s_.ib;
    if (!vb.buf || !ib.buf || vb.buf->core.store != Store::Committed || ib.buf->core.store != Store::Committed) return nullptr;
    if (!vb.buf->core.tr || !ib.buf->core.tr) return nullptr;
    if (vb.stride < 16u || vb.stride > 128u || (vb.stride & 3u) != 0u) return nullptr;
    if (s_.ibFmt != DXGI_FORMAT_R16_UINT && s_.ibFmt != DXGI_FORMAT_R32_UINT) return nullptr;

    const SubdivLayoutInfo* li = dev_->GetSubdivLayout(s_.layout->id);
    if (!li || !li->eligible) return nullptr;

    SubdivKey key{};
    key.vbTr = vb.buf->core.tr.get();
    key.ibTr = ib.buf->core.tr.get();
    key.vbOffset = vb.offset;
    key.ibOffset = ib.offset;
    key.startIndex = start;
    key.indexCount = n;
    key.baseVertex = base;
    key.layoutId = s_.layout->id;
    key.stride = vb.stride;
    key.ibFmt = static_cast<uint32_t>(s_.ibFmt);

    if (auto hit = dev_->subdiv.Find(key)) return hit;
    if (replay_) return nullptr;   // workers only consume cached entries; misses build once on the immediate context

    ID3D12GraphicsCommandList* cl = Cl();
    rec_.tracker.Require(vb.buf->core.tr, 0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    rec_.tracker.Require(ib.buf->core.tr, 0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    rec_.tracker.Flush(cl);

    SubdivBuildReq req{};
    req.key = key;
    req.layout = li;
    req.inVerts = vb.buf->core.Res();
    req.inIdx = ib.buf->core.Res();
    req.inVertsBytes = vb.buf->core.bytes;
    req.inIdxBytes = ib.buf->core.bytes;
    req.curvatureGain = 0.62f;

    bool dispatched = false;
    auto built = dev_->subdiv.Build(dev_->d3d.Get(), cl, req, dispatched);
    if (dispatched) {
        pso_ = nullptr;
        croot_ = nullptr;
        dirty_ |= D_SCAN_IA | D_VB | D_IB;
    }
    return built;
}

STDMETHODIMP_(void) Context::DrawIndexed(UINT n, UINT start, INT base)
{
    TX_TRACE("Context::DrawIndexed");
    // On an uncached eligible static mesh draw, break streaming once so immediate context builds & caches the 4x mesh.
    const bool needBuild = dev_->geometrySubdiv && !replay_ && s_.topo == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST &&
                           s_.dsv && (s_.dsv->desc.Flags & D3D11_DSV_READ_ONLY_DEPTH) == 0 &&
                           n >= 12u && n <= 36000u && (n % 3u) == 0u &&
                           s_.vb[0].buf && s_.ib.buf && s_.vb[0].buf->core.store == Store::Committed &&
                           s_.ib.buf->core.store == Store::Committed && s_.layout && s_.layout->id &&
                           !dev_->subdiv.Find({s_.vb[0].buf->core.tr.get(), s_.ib.buf->core.tr.get(), s_.vb[0].offset,
                                               s_.ib.offset, start, n, base, s_.layout->id, s_.vb[0].stride,
                                               static_cast<uint32_t>(s_.ibFmt)});
    if (!needBuild && pool_ && !replay_ && StreamDraw()) { SHead(SO_DrawIndexed, 0, 0, n); seg_->words.push_back(uint64_t(start) | uint64_t(uint32_t(base)) << 32); return SegDrawn(); }
    if (seg_ || stale_) EndStream(__func__);
    auto sub = TrySubdivideDraw(n, start, base);
    if (PrepareDraw(true)) {
        if (sub) {
            ID3D12GraphicsCommandList* cl = Cl();
            cl->IASetVertexBuffers(0, 1, &sub->vbView);
            cl->IASetIndexBuffer(&sub->ibView);
            cl->DrawIndexedInstanced(sub->outIndexCount, 1, 0, 0, 0);
            dirty_ |= D_VB | D_IB;
            dev_->nSubdivDraws.fetch_add(1, std::memory_order_relaxed);
        } else {
            Cl()->DrawIndexedInstanced(n, 1, start, base, 0);
        }
    }
}
STDMETHODIMP_(void) Context::DrawInstanced(UINT n, UINT inst, UINT start, UINT startInst)
{
    TX_TRACE("Context::DrawInstanced");
    if (pool_ && !replay_ && StreamDraw()) {
        SHead(SO_DrawInstanced, 0, 0, n);
        seg_->words.push_back(uint64_t(inst) | uint64_t(start) << 32); seg_->words.push_back(startInst);
        return SegDrawn();
    }
    if (seg_ || stale_) EndStream(__func__);
    if (PrepareDraw(false)) Cl()->DrawInstanced(n, inst, start, startInst);
}
STDMETHODIMP_(void) Context::DrawIndexedInstanced(UINT n, UINT inst, UINT start, INT base, UINT startInst)
{
    TX_TRACE("Context::DrawIndexedInstanced");
    if (pool_ && !replay_ && StreamDraw()) {
        SHead(SO_DrawIndexedInstanced, 0, 0, n);
        seg_->words.push_back(uint64_t(inst) | uint64_t(start) << 32);
        seg_->words.push_back(uint64_t(uint32_t(base)) | uint64_t(startInst) << 32);
        return SegDrawn();
    }
    if (seg_ || stale_) EndStream(__func__);
    if (PrepareDraw(true)) Cl()->DrawIndexedInstanced(n, inst, start, base, startInst);
}
STDMETHODIMP_(void) Context::DrawAuto() { CountUnsupported(dev_, "DrawAuto (stream output)"); }

}  // namespace raw::tx
