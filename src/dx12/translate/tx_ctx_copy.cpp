// Copies, resolves and mip generation.
#include "tx_context.h"

namespace raw::tx {

static constexpr uint32_t kShaderRead =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

void Context::CopyBuffer(ResCore& dst, uint64_t dstOff, ResCore& src, uint64_t srcOff, uint64_t n)
{
    if (srcOff + n > src.bytes || dstOff + n > dst.bytes) return;
    if (dst.store == Store::Renamed) {
        const uint8_t* from = nullptr;
        if (src.store == Store::Renamed) from = Pin(static_cast<TBuffer*>(src.self))->cpu;
        else if (src.store == Store::Staging) { Sync(); from = src.mapped; }
        if (!from) { CountUnsupported(dev_, "copy from a GPU buffer into a constant or dynamic buffer"); return; }
        TBuffer* db = static_cast<TBuffer*>(dst.self);
        const uint8_t* old = Pin(db)->cpu;
        RenameBuffer(db);
        uint8_t* cur = Pin(db)->cpu;
        if (n != dst.bytes) memcpy(cur, old, dst.bytes);
        memcpy(cur + dstOff, from + srcOff, n);
        return;
    }
    ID3D12Resource* sres = src.Res();
    if (src.store == Store::Renamed) {
        const Chunk* ch = Pin(static_cast<TBuffer*>(src.self));
        sres = ch->res;
        srcOff += ch->offset;
    } else {
        rec_.tracker.Require(src.tr, 0, D3D12_RESOURCE_STATE_COPY_SOURCE);
    }
    rec_.tracker.Require(dst.tr, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    rec_.tracker.Flush(Cl());
    Cl()->CopyBufferRegion(dst.Res(), dstOff, sres, srcOff, n);
    ++rec_.cmdCount;
    dirty_ |= D_SCAN;
}

void Context::StagingCpuCopy(ResCore& dst, UINT dsub, UINT x, UINT y, UINT z, ResCore& src, UINT ssub, const D3D11_BOX* box)
{
    Sync();
    const auto& df = dst.fp[dsub];
    const auto& sf = src.fp[ssub];
    UINT bpp = sf.Footprint.RowPitch / (sf.Footprint.Width ? sf.Footprint.Width : 1);
    D3D11_BOX b{0, 0, 0, sf.Footprint.Width, sf.Footprint.Height, sf.Footprint.Depth};
    if (box) b = *box;
    if (b.right <= b.left || b.bottom <= b.top || b.back <= b.front) return;
    for (UINT zz = b.front; zz < b.back; ++zz)
        for (UINT yy = b.top; yy < b.bottom; ++yy)
            memcpy(dst.mapped + df.Offset + (size_t(z + zz - b.front) * df.Footprint.Height + y + yy - b.top) * df.Footprint.RowPitch + size_t(x) * bpp,
                   src.mapped + sf.Offset + (size_t(zz) * sf.Footprint.Height + yy) * sf.Footprint.RowPitch + size_t(b.left) * bpp,
                   size_t(b.right - b.left) * bpp);
}

static D3D12_TEXTURE_COPY_LOCATION Location(const ResCore& c, UINT sub, UINT plane)
{
    D3D12_TEXTURE_COPY_LOCATION l{c.Res(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    if (c.store == Store::Staging) {
        l.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        l.PlacedFootprint = c.fp[sub];
    } else {
        l.SubresourceIndex = sub + plane * c.mips * c.slices;
    }
    return l;
}

void Context::CopyTexture(ResCore& dst, UINT dsub, UINT x, UINT y, UINT z, ResCore& src, UINT ssub, const D3D11_BOX* box)
{
    bool ds = dst.store == Store::Staging, ss = src.store == Store::Staging;
    if (ds && ss) { StagingCpuCopy(dst, dsub, x, y, z, src, ssub, box); return; }
    if ((ds && dsub >= dst.fp.size()) || (ss && ssub >= src.fp.size())) return;
    UINT planes = (!ds && !ss && dst.planes > 1 && src.planes > 1) ? 2 : 1;
    if ((ds || ss) && (dst.planes > 1 || src.planes > 1))
        CountUnsupported(dev_, "staging copy of a packed depth-stencil format (depth plane only)");
    D3D12_BOX b{};
    if (box) {
        if (box->right <= box->left || box->bottom <= box->top || box->back <= box->front) return;
        b = {box->left, box->top, box->front, box->right, box->bottom, box->back};
    }
    for (UINT p = 0; p < planes; ++p) {
        D3D12_TEXTURE_COPY_LOCATION dl = Location(dst, dsub, p), sl = Location(src, ssub, p);
        rec_.tracker.Require(dst.tr, ds ? 0 : dl.SubresourceIndex, D3D12_RESOURCE_STATE_COPY_DEST);
        rec_.tracker.Require(src.tr, ss ? 0 : sl.SubresourceIndex, D3D12_RESOURCE_STATE_COPY_SOURCE);
        rec_.tracker.Flush(Cl());
        Cl()->CopyTextureRegion(&dl, x, y, z, &sl, box ? &b : nullptr);
    }
    ++rec_.cmdCount;
    dirty_ |= D_SCAN;
}

STDMETHODIMP_(void) Context::CopyResource(ID3D11Resource* dstRes, ID3D11Resource* srcRes)
{
    TX_TRACE("Context::CopyResource");
    ResCore* d = CoreOf(dstRes);
    ResCore* s = CoreOf(srcRes);
    bool recorded = false;
    if (!StreamCopy(d, s, recorded)) RunBreak(__func__);
    if (recorded) { SHead(SO_Copy); SRes(d); SRes(s); return; }
    if (!d || !s || d == s || d->dim != s->dim) return;
    if (d->IsBuffer()) { CopyBuffer(*d, 0, *s, 0, d->bytes < s->bytes ? d->bytes : s->bytes); return; }
    if (d->store == Store::Committed && s->store == Store::Committed) {
        rec_.tracker.RequireAll(d->tr, D3D12_RESOURCE_STATE_COPY_DEST);
        rec_.tracker.RequireAll(s->tr, D3D12_RESOURCE_STATE_COPY_SOURCE);
        rec_.tracker.Flush(Cl());
        Cl()->CopyResource(d->Res(), s->Res());
        ++rec_.cmdCount;
        dirty_ |= D_SCAN;
        return;
    }
    for (UINT sub = 0; sub < d->mips * d->slices && sub < s->mips * s->slices; ++sub)
        CopyTexture(*d, sub, 0, 0, 0, *s, sub, nullptr);
}

STDMETHODIMP_(void) Context::CopySubresourceRegion(ID3D11Resource* dstRes, UINT dsub, UINT x, UINT y, UINT z,
                                                   ID3D11Resource* srcRes, UINT ssub, const D3D11_BOX* box)
{
    TX_TRACE("Context::CopySubresourceRegion");
    ResCore* d = CoreOf(dstRes);
    ResCore* s = CoreOf(srcRes);
    bool recorded = false;
    if (!StreamCopy(d, s, recorded)) RunBreak(__func__);
    if (recorded) {
        SHead(SO_CopyRegion, 0, 0, box ? 1 : 0);
        SRes(d); SRes(s);
        seg_->words.push_back(uint64_t(dsub) | uint64_t(ssub) << 32);
        seg_->words.push_back(uint64_t(x) | uint64_t(y) << 32);
        seg_->words.push_back(z);
        if (box) { uint64_t b[3]; memcpy(b, box, sizeof(D3D11_BOX)); for (uint64_t v : b) seg_->words.push_back(v); }
        return;
    }
    if (!d || !s || d->dim != s->dim) return;
    if (!d->IsBuffer()) { CopyTexture(*d, dsub, x, y, z, *s, ssub, box); return; }
    if (box && box->right <= box->left) return;
    uint64_t off = box ? box->left : 0;
    uint64_t n = box ? uint64_t(box->right) - box->left : s->bytes;
    CopyBuffer(*d, x, *s, off, n);
}

STDMETHODIMP_(void) Context::CopyStructureCount(ID3D11Buffer*, UINT, ID3D11UnorderedAccessView*)
{
    TX_TRACE("Context::CopyStructureCount");
    CountUnsupported(dev_, "CopyStructureCount (UAV hidden counter)");
}

STDMETHODIMP_(void) Context::ResolveSubresource(ID3D11Resource* dstRes, UINT dsub, ID3D11Resource* srcRes, UINT ssub, DXGI_FORMAT fmt)
{
    RunBreak(__func__);
    TX_TRACE("Context::ResolveSubresource");
    ResCore* d = CoreOf(dstRes);
    ResCore* s = CoreOf(srcRes);
    if (!d || !s || d->store != Store::Committed || s->store != Store::Committed) return;
    rec_.tracker.Require(d->tr, dsub, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    rec_.tracker.Require(s->tr, ssub, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    rec_.tracker.Flush(Cl());
    Cl()->ResolveSubresource(d->Res(), dsub, s->Res(), ssub, fmt);
    ++rec_.cmdCount;
    dirty_ |= D_SCAN;
}

STDMETHODIMP_(void) Context::GenerateMips(ID3D11ShaderResourceView* view)
{
    RunBreak(__func__);
    TX_TRACE("Context::GenerateMips");
    TSRV* v = static_cast<TSRV*>(view);
    if (!v) return;
    ResCore& c = *v->core;
    if (!(c.misc & D3D11_RESOURCE_MISC_GENERATE_MIPS) || c.store != Store::Committed) return;
    if (c.dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D || c.samples > 1) {
        CountUnsupported(dev_, "GenerateMips on a 1D or 3D texture");
        return;
    }
    ID3D12PipelineState* pso = dev_->GetMipPso(v->format);
    RootSig* root = dev_->mipRootSig;
    if (!pso || !root) return;
    ID3D12GraphicsCommandList* cl = Cl();
    cl->SetGraphicsRootSignature(root->rs.Get());
    cl->SetPipelineState(pso);
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    uint32_t sampler = dev_->MipSampler();
    cl->SetGraphicsRootDescriptorTable(root->st[S_PS].samp, dev_->SamplerTable(&sampler, 1));
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = dev_->cpuRtv.Alloc();
    for (UINT slice = v->range.slice0; slice < UINT(v->range.slice0) + v->range.sliceN; ++slice) {
        for (UINT m = v->range.mip0 + 1u; m < UINT(v->range.mip0) + v->range.mipN; ++m) {
            rec_.tracker.Require(c.tr, c.Sub(m - 1, slice), kShaderRead);
            rec_.tracker.Require(c.tr, c.Sub(m, slice), D3D12_RESOURCE_STATE_RENDER_TARGET);
            rec_.tracker.Flush(cl);
            D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.Format = v->format;
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sd.Texture2DArray.MostDetailedMip = m - 1; sd.Texture2DArray.MipLevels = 1;
            sd.Texture2DArray.FirstArraySlice = slice; sd.Texture2DArray.ArraySize = 1;
            uint32_t idx = AllocDescs(1);
            dev_->d3d->CreateShaderResourceView(c.Res(), &sd, CpuHandle(idx));
            cl->SetGraphicsRootDescriptorTable(root->st[S_PS].srv, GpuHandle(idx));
            D3D12_RENDER_TARGET_VIEW_DESC rd{};
            rd.Format = v->format;
            rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            rd.Texture2DArray.MipSlice = m; rd.Texture2DArray.FirstArraySlice = slice; rd.Texture2DArray.ArraySize = 1;
            dev_->d3d->CreateRenderTargetView(c.Res(), &rd, rtv);
            cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            UINT w = (c.width >> m) ? (c.width >> m) : 1, h = (c.height >> m) ? (c.height >> m) : 1;
            D3D12_VIEWPORT vp{0, 0, float(w), float(h), 0, 1};
            D3D12_RECT sc{0, 0, LONG(w), LONG(h)};
            cl->RSSetViewports(1, &vp);
            cl->RSSetScissorRects(1, &sc);
            cl->DrawInstanced(3, 1, 0, 0);
        }
    }
    dev_->cpuRtv.Free(rtv);
    root_ = nullptr;   // the application's pipeline is rebound at its next draw
    pso_ = nullptr;
    dirty_ = D_ALL;
    ++rec_.cmdCount;
}

}  // namespace raw::tx
