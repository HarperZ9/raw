// Map, Unmap and UpdateSubresource.
//
// Constant buffers and dynamic vertex/index buffers are "renamed": every
// discard takes a fresh upload-heap chunk, so the GPU keeps reading the old
// contents while the CPU writes the new ones and nothing waits.
#include "tx_context.h"

namespace raw::tx {

static void MipSize(const ResCore& c, uint32_t mip, uint32_t& w, uint32_t& h, uint32_t& d)
{
    w = (c.width >> mip) ? (c.width >> mip) : 1;
    h = (c.height >> mip) ? (c.height >> mip) : 1;
    d = (c.depth >> mip) ? (c.depth >> mip) : 1;
}

// Map and Unmap counts by storage, logged every 600 presents (MapStatsLog): which path the game's per-draw
// uploads take on the translation layer.
namespace {
uint64_t g_map[4] = {}, g_unmap[3] = {}, g_unmapBytes = 0;   // map: renamed, staging, committed buffer, texture
}
void MapStatsLog(uint64_t frames)
{
    if (!frames) return;
    const double f = double(frames);
    Log("map stats per frame: renamed %.0f, staging %.0f, committed buffer %.0f, texture %.0f; unmap copies buffer %.0f, "
        "texture %.0f (%.0f KB), no-op %.0f", g_map[0] / f, g_map[1] / f, g_map[2] / f, g_map[3] / f, g_unmap[0] / f,
        g_unmap[1] / f, g_unmapBytes / f / 1024.0, g_unmap[2] / f);
    for (auto& x : g_map) x = 0;
    for (auto& x : g_unmap) x = 0;
    g_unmapBytes = 0;
}

STDMETHODIMP Context::Map(ID3D11Resource* res, UINT sub, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* out)
{
    TX_TRACE("Context::Map");
    ResCore* c = CoreOf(res);
    if (!c || !out) return E_INVALIDARG;
    *out = D3D11_MAPPED_SUBRESOURCE{};
    if (c->store == Store::Renamed) {
        ++g_map[0];
        TBuffer* b = static_cast<TBuffer*>(res);
        if (type == D3D11_MAP_WRITE_DISCARD) {
            RenameBuffer(b);
            if (seg_) { SHead(SO_Chunk); SBuf(b); }   // the segment's later draws read the new memory
        } else if (type != D3D11_MAP_WRITE_NO_OVERWRITE) return E_INVALIDARG;
        out->pData = Pin(b)->cpu;
        out->RowPitch = out->DepthPitch = UINT(c->bytes);
        return S_OK;
    }
    RunBreak(__func__);
    if (c->store == Store::Staging) {
        ++g_map[1];
        if (deferred_) return E_INVALIDARG;
        uint64_t serial = FlushInternal();
        if (dev_->Completed() < serial) {
            if (flags & D3D11_MAP_FLAG_DO_NOT_WAIT) return DXGI_ERROR_WAS_STILL_DRAWING;
            dev_->WaitFor(serial);
        }
        if (c->IsBuffer()) {
            out->pData = c->mapped;
            out->RowPitch = out->DepthPitch = UINT(c->bytes);
            return S_OK;
        }
        if (sub >= c->fp.size()) return E_INVALIDARG;
        uint32_t w, h, d;
        MipSize(*c, sub % c->mips, w, h, d);
        out->pData = c->mapped + c->fp[sub].Offset;
        out->RowPitch = c->fp[sub].Footprint.RowPitch;
        out->DepthPitch = out->RowPitch * c->fp[sub].Footprint.Height;
        return S_OK;
    }
    // Committed dynamic resource: write into scratch, copy on Unmap.
    if (type != D3D11_MAP_WRITE_DISCARD && type != D3D11_MAP_WRITE_NO_OVERWRITE) return E_INVALIDARG;
    if (type == D3D11_MAP_WRITE_NO_OVERWRITE) CountUnsupported(dev_, "Map NO_OVERWRITE on a dynamic resource with a view (contents restart)");
    if (c->mapChunk.res) return E_INVALIDARG;
    c->mapSub = sub;
    ++g_map[c->IsBuffer() ? 2 : 3];
    if (c->IsBuffer()) {
        c->mapChunk = chunks_.Alloc(c->bytes);
        out->pData = c->mapChunk.cpu;
        out->RowPitch = out->DepthPitch = UINT(c->bytes);
        return c->mapChunk.res ? S_OK : E_OUTOFMEMORY;
    }
    uint32_t w, h, d;
    MipSize(*c, sub % c->mips, w, h, d);
    D3D12_RESOURCE_DESC rd = c->Res()->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT64 total = 0;
    dev_->d3d->GetCopyableFootprints(&rd, sub, 1, 0, &fp, nullptr, nullptr, &total);
    c->mapChunk = chunks_.Alloc(total);
    if (!c->mapChunk.res) return E_OUTOFMEMORY;
    out->pData = c->mapChunk.cpu;
    out->RowPitch = fp.Footprint.RowPitch;
    out->DepthPitch = fp.Footprint.RowPitch * fp.Footprint.Height;
    return S_OK;
}

STDMETHODIMP_(void) Context::Unmap(ID3D11Resource* res, UINT sub)
{
    ResCore* c = CoreOf(res);
    if (!c || c->store != Store::Committed || !c->mapChunk.res) { ++g_unmap[2]; return; }   // renamed: nothing to do
    TX_TRACE("Context::Unmap");
    RunBreak(__func__);
    Chunk ch = c->mapChunk;
    ++g_unmap[c->IsBuffer() ? 0 : 1];
    g_unmapBytes += ch.size;
    c->mapChunk = Chunk{};
    Tracker& t = rec_.tracker;
    if (c->IsBuffer()) {
        t.Require(c->tr, 0, D3D12_RESOURCE_STATE_COPY_DEST);
        t.Flush(Cl());
        Cl()->CopyBufferRegion(c->Res(), 0, ch.res, ch.offset, c->bytes);
    } else {
        D3D12_RESOURCE_DESC rd = c->Res()->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        dev_->d3d->GetCopyableFootprints(&rd, sub, 1, ch.offset, &fp, nullptr, nullptr, nullptr);
        t.Require(c->tr, sub, D3D12_RESOURCE_STATE_COPY_DEST);
        t.Flush(Cl());
        D3D12_TEXTURE_COPY_LOCATION dst{c->Res(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        dst.SubresourceIndex = sub;
        D3D12_TEXTURE_COPY_LOCATION src{ch.res, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        src.PlacedFootprint = fp;
        Cl()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
    rec_.retire.chunks.push_back(ch);
    ++rec_.cmdCount;
    dirty_ |= D_SCAN;
}

void Context::WriteTexture(ResCore& c, UINT sub, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch)
{
    RunBreak(__func__);
    uint32_t w, h, d;
    MipSize(c, sub % c.mips, w, h, d);
    UINT x = 0, y = 0, z = 0;
    if (box) {
        if (box->right <= box->left || box->bottom <= box->top || box->back <= box->front) return;
        x = box->left; y = box->top; z = box->front;
        w = box->right - box->left; h = box->bottom - box->top; d = box->back - box->front;
    }
    if (c.store == Store::Staging) {
        Sync();
        const auto& fp = c.fp[sub];
        UINT bpp = fp.Footprint.RowPitch / (fp.Footprint.Width ? fp.Footprint.Width : 1);
        size_t rowBytes = rowPitch && rowPitch < w * bpp ? rowPitch : size_t(w) * bpp;
        for (UINT zz = 0; zz < d; ++zz)
            for (UINT yy = 0; yy < h; ++yy)
                memcpy(c.mapped + fp.Offset + (size_t(z + zz) * fp.Footprint.Height + y + yy) * fp.Footprint.RowPitch + size_t(x) * bpp,
                       static_cast<const uint8_t*>(data) + size_t(zz) * depthPitch + size_t(yy) * rowPitch, rowBytes);
        return;
    }
    UploadRegion(dev_, rec_, chunks_, c, sub, x, y, z, w, h, d, data, rowPitch, depthPitch);
    dirty_ |= D_SCAN;
}

STDMETHODIMP_(void) Context::UpdateSubresource(ID3D11Resource* res, UINT sub, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch)
{
    TX_TRACE("Context::UpdateSubresource");
    ResCore* c = CoreOf(res);
    if (!c || !data) return;
    if (!c->IsBuffer() || c->store != Store::Renamed) RunBreak(__func__);   // a renamed buffer is written here, like Map
    if (!c->IsBuffer()) { WriteTexture(*c, sub, box, data, rowPitch, depthPitch); return; }
    uint64_t off = box ? box->left : 0;
    uint64_t n = box ? uint64_t(box->right) - box->left : c->bytes;
    if (box && box->right <= box->left) return;
    if (off + n > c->bytes) return;
    if (c->store == Store::Renamed) {
        TBuffer* b = static_cast<TBuffer*>(res);
        const uint8_t* old = Pin(b)->cpu;
        RenameBuffer(b);
        uint8_t* cur = Pin(b)->cpu;
        if (n != c->bytes) memcpy(cur, old, c->bytes);   // a partial update keeps the rest
        memcpy(cur + off, data, n);
        if (seg_) { SHead(SO_Chunk); SBuf(b); }
        return;
    }
    if (c->store == Store::Staging) {
        Sync();
        memcpy(c->mapped + off, data, n);
        return;
    }
    Chunk ch = chunks_.Alloc(n);
    if (!ch.res) return;
    memcpy(ch.cpu, data, n);
    rec_.tracker.Require(c->tr, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    rec_.tracker.Flush(Cl());
    Cl()->CopyBufferRegion(c->Res(), off, ch.res, ch.offset, n);
    rec_.retire.chunks.push_back(ch);
    ++rec_.cmdCount;
    dirty_ |= D_SCAN;
}

}  // namespace raw::tx
