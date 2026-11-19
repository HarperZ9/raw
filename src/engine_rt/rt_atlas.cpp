// Live shadow-atlas reallocation (RAW engine runtime). Original work, MIT.
//
// The renderer keeps each depth-stencil target as {texture, 8 views, 8 read-only views, depth SRV,
// stencil SRV} (BSGraphics::RendererData::depthStencils, +0x1FA8, 0x98 each; layout from CommonLibSSE-NG).
// At a present, the frame is finished, so the shadow entry is rebuilt at the new size: a texture with the
// old description and new width and height, then every view recreated from the old view's own description.
// The pointers swap in place; the old objects are released 4 presents later, after any in-flight use.
#include "rt_atlas.h"

#include <cstdio>
#include <vector>

namespace raw::rt {
namespace {

struct DepthStencilData {
    ID3D11Texture2D* texture;
    ID3D11DepthStencilView* views[8];
    ID3D11DepthStencilView* readOnlyViews[8];
    ID3D11ShaderResourceView* depthSRV;
    ID3D11ShaderResourceView* stencilSRV;
};
static_assert(sizeof(DepthStencilData) == 0x98);

struct Retired {
    std::vector<IUnknown*> objects;
    int presentsLeft;
};
std::vector<Retired> g_retired;

}  // namespace

std::string ResizeShadowAtlasLive(ID3D11DeviceContext* ctx, const Image& img, const std::vector<RefTarget>& refs,
                                  int entry, uint32_t size) {
    const RefTarget* r = nullptr;
    for (const auto& x : refs)
        if (x.name == "Renderer::GetRendererData") { r = &x; break; }
    if (!r) return "atlas: Renderer::GetRendererData not in the refs";
    uint8_t* data = *reinterpret_cast<uint8_t**>(const_cast<uint8_t*>(img.base) + r->rva);
    if (!data) return "atlas: no renderer data yet";
    auto* ds = reinterpret_cast<DepthStencilData*>(data + 0x1FA8 + 0x98 * entry);
    if (!ds->texture) return "atlas: entry has no texture";
    D3D11_TEXTURE2D_DESC td;
    ds->texture->GetDesc(&td);
    char line[256];
    const uint32_t oldW = td.Width, oldH = td.Height;
    if (oldW == size) return "atlas: already that size";
    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    td.Width = td.Height = size;
    DepthStencilData fresh = {};
    HRESULT hr = dev->CreateTexture2D(&td, nullptr, &fresh.texture);
    bool ok = SUCCEEDED(hr);
    for (int i = 0; ok && i < 8; ++i) {
        if (ds->views[i]) {
            D3D11_DEPTH_STENCIL_VIEW_DESC vd; ds->views[i]->GetDesc(&vd);
            ok = SUCCEEDED(dev->CreateDepthStencilView(fresh.texture, &vd, &fresh.views[i]));
        }
        if (ok && ds->readOnlyViews[i]) {
            D3D11_DEPTH_STENCIL_VIEW_DESC vd; ds->readOnlyViews[i]->GetDesc(&vd);
            ok = SUCCEEDED(dev->CreateDepthStencilView(fresh.texture, &vd, &fresh.readOnlyViews[i]));
        }
    }
    if (ok && ds->depthSRV) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd; ds->depthSRV->GetDesc(&sd);
        ok = SUCCEEDED(dev->CreateShaderResourceView(fresh.texture, &sd, &fresh.depthSRV));
    }
    if (ok && ds->stencilSRV) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd; ds->stencilSRV->GetDesc(&sd);
        ok = SUCCEEDED(dev->CreateShaderResourceView(fresh.texture, &sd, &fresh.stencilSRV));
    }
    dev->Release();
    auto all = [](DepthStencilData& d) {
        std::vector<IUnknown*> v;
        if (d.texture) v.push_back(d.texture);
        for (auto* p : d.views) if (p) v.push_back(p);
        for (auto* p : d.readOnlyViews) if (p) v.push_back(p);
        if (d.depthSRV) v.push_back(d.depthSRV);
        if (d.stencilSRV) v.push_back(d.stencilSRV);
        return v;
    };
    if (!ok) {
        for (IUnknown* p : all(fresh)) p->Release();
        std::snprintf(line, sizeof line, "atlas: entry %d %ux%u -> %u failed (hr 0x%08lX)", entry, oldW, oldH, size, hr);
        return line;
    }
    // Unbind the old views from the pipeline so nothing still points at them, then swap.
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView* none[16] = {};
    ctx->PSSetShaderResources(0, 16, none);
    g_retired.push_back({all(*ds), 4});
    *ds = fresh;
    std::snprintf(line, sizeof line, "atlas: entry %d %ux%u x%u -> %ux%u", entry, oldW, oldH, td.ArraySize, size, size);
    return line;
}

void AtlasOnPresent() {
    for (auto it = g_retired.begin(); it != g_retired.end();) {
        if (--it->presentsLeft <= 0) {
            for (IUnknown* p : it->objects) p->Release();
            it = g_retired.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace raw::rt
