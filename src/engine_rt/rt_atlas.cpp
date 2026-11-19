// Live shadow-atlas reallocation (RAW engine runtime). Original work, MIT.
//
// The renderer keeps each depth-stencil target as {texture, 8 views, 8 read-only views, depth SRV,
// stencil SRV} (BSGraphics::RendererData::depthStencils, +0x1FA8, 0x98 each; layout from CommonLibSSE-NG).
// At a present, the frame is finished, so the shadow entry is rebuilt at the new size: a texture with the
// old description and new width and height, then every view recreated from the old view's own description.
// The pointers swap in place; the old objects are released 4 presents later, after any in-flight use.
#include "rt_atlas.h"

#include <windows.h>

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

// A pointer read from game memory is treated as untrusted: it must answer QueryInterface for
// ID3D11Texture2D, under structured exception handling, before anything else is called on it.
static bool SafeTexDescRaw(void* p, D3D11_TEXTURE2D_DESC* out) {
    __try {
        auto* unk = static_cast<IUnknown*>(p);
        ID3D11Texture2D* t = nullptr;
        if (FAILED(unk->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t))) || !t) return false;
        t->GetDesc(out);
        t->Release();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
static bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!p || !VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return reinterpret_cast<const uint8_t*>(p) + n <= reinterpret_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
}

// The depth-stencil table's start inside RendererData: entry 2 is the sun's cascade atlas, a square
// R16_TYPELESS texture with two slices. Returns 0 when no entry has that shape.
static size_t FindDepthTable(uint8_t* data) {
    for (size_t off = 0x1800; Readable(data, 0x2800) && off < 0x2800; off += 8) {
        void* p = *reinterpret_cast<void**>(data + off);
        if (!Readable(p, 16) || !Readable(*reinterpret_cast<void**>(p), 64)) continue;
        D3D11_TEXTURE2D_DESC td = {};
        if (SafeTexDescRaw(p, &td) && td.Format == DXGI_FORMAT_R16_TYPELESS && td.ArraySize == 2 && td.Width == td.Height &&
            td.Width >= 1024)
            return off - 2 * 0x98;
    }
    return 0;
}

std::string ResizeShadowAtlasLive(ID3D11DeviceContext* ctx, const Image& img, const std::vector<RefTarget>& refs,
                                  int entry, uint32_t size) {
    const RefTarget* r = nullptr;
    for (const auto& x : refs)
        if (x.name == "Renderer::GetRendererData") { r = &x; break; }
    if (!r) return "atlas: Renderer::GetRendererData not in the refs";
    uint8_t* data = *reinterpret_cast<uint8_t**>(const_cast<uint8_t*>(img.base) + r->rva);
    if (!data) return "atlas: no renderer data yet";
    // CommonLib places the depth table at +0x1FA8; on 1.6.1170 it sits 0x60 later (found by the dump:
    // the 4096 x 2 cascade atlas at +0x2138 = entry 2). Find it by shape rather than trust either number.
    const size_t table = FindDepthTable(data);
    if (!table) return "atlas: depth table not found (no square R16 two-slice target)";
    auto* ds = reinterpret_cast<DepthStencilData*>(data + table + 0x98 * entry);
    D3D11_TEXTURE2D_DESC td = {};
    if (!Readable(ds, sizeof *ds) || !ds->texture || !SafeTexDescRaw(ds->texture, &td)) return "atlas: entry has no texture";
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

std::string DumpDepthTargets(const Image& img, const std::vector<RefTarget>& refs) {
    std::string out = "{\"cmd\":\"dump\"";
    auto find = [&](const char* n) -> const RefTarget* {
        for (const auto& x : refs) if (x.name == n) return &x;
        return nullptr;
    };
    char buf[160];
    if (const RefTarget* m = find("BSGraphics::RenderTargetManager::GetSingleton")) {
        uint8_t* mgr = *reinterpret_cast<uint8_t**>(const_cast<uint8_t*>(img.base) + m->rva);
        out += ",\"table\":[";
        for (int i = 0; Readable(mgr, 0xD38) && i < 12; ++i) {
            const auto* e = reinterpret_cast<const uint32_t*>(mgr + 0xC78 + 0x10 * i);
            std::snprintf(buf, sizeof buf, "%s[%u,%u,%u]", i ? "," : "", e[0], e[1], e[2]);
            out += buf;
        }
        out += "]";
    }
    if (const RefTarget* r = find("Renderer::GetRendererData")) {
        uint8_t* data = *reinterpret_cast<uint8_t**>(const_cast<uint8_t*>(img.base) + r->rva);
        // every texture held directly in RendererData, with its offset: shows where the depth table sits
        out += ",\"textures\":[";
        bool first = true;
        for (size_t off = 0; Readable(data, 0x2800) && off < 0x2800; off += 8) {
            void* p = *reinterpret_cast<void**>(data + off);
            if (!Readable(p, 16) || !Readable(*reinterpret_cast<void**>(p), 64)) continue;
            D3D11_TEXTURE2D_DESC td = {};
            if (!SafeTexDescRaw(p, &td)) continue;
            std::snprintf(buf, sizeof buf, "%s[\"0x%zX\",%u,%u,%u,%u]", first ? "" : ",", off, td.Width, td.Height, td.ArraySize,
                          unsigned(td.Format));
            out += buf;
            first = false;
        }
        out += "]";
    }
    return out + "}";
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
