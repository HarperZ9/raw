// SceneGeometryBound: what SceneGeometry.cpp and SceneSkin.cpp read at every draw, without a lock and
// without a Get and Release pair. The input layouts and pixel shaders seen at creation sit in two
// open-addressed tables: written under a mutex (creation is rare), read with acquire loads (every draw).
// A slot's key never changes once set, so a reader that finds its key reads a whole value; a value is
// either a byte or a pointer to an immutable record, swapped atomically when an address is reused.
// On raw::tx the bindings are read straight from the layer's shadow state (raw::tx::Peek*); on the system
// runtime the Get's reference is dropped at once, which is safe while the object stays bound for the
// draw. Only what a draw keeps for the bridge (vertex, index buffer, texture) takes a reference.
// Original work, MIT.
#include "SceneGeometryDetail.h"
#include "../dx12/translate/Translate.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <unordered_map>

namespace SB::Proxy::geom
{
namespace
{
// Pointer-keyed table, lock-free for readers. N is a power of two; a key probes at most kProbe slots,
// then falls to a locked map (never used at Skyrim's counts, kept so a full table stays correct).
template <class V, size_t N>
class PtrTable
{
    static constexpr size_t kProbe = 32;
    struct Slot { std::atomic<const void*> key{nullptr}; std::atomic<V> val{}; };
    Slot s_[N];
    std::mutex wm_;
    std::unordered_map<const void*, V> overflow_;
    std::atomic<bool> hasOverflow_{false};

    static size_t Hash(const void* p)
    {
        uint64_t x = reinterpret_cast<uint64_t>(p);
        x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33;
        return size_t(x) & (N - 1);
    }

public:
    bool Find(const void* k, V& out)
    {
        if (!k) return false;
        size_t i = Hash(k);
        for (size_t n = 0; n < kProbe; ++n, i = (i + 1) & (N - 1)) {
            const void* c = s_[i].key.load(std::memory_order_acquire);
            if (c == k) { out = s_[i].val.load(std::memory_order_acquire); return true; }
            if (!c) return false;
        }
        if (!hasOverflow_.load(std::memory_order_acquire)) return false;
        std::lock_guard<std::mutex> g(wm_);
        auto it = overflow_.find(k);
        if (it == overflow_.end()) return false;
        out = it->second;
        return true;
    }

    void Put(const void* k, V v)
    {
        if (!k) return;
        std::lock_guard<std::mutex> g(wm_);
        size_t i = Hash(k);
        for (size_t n = 0; n < kProbe; ++n, i = (i + 1) & (N - 1)) {
            const void* c = s_[i].key.load(std::memory_order_relaxed);
            if (c == k) { s_[i].val.store(v, std::memory_order_release); return; }
            if (!c) {
                s_[i].val.store(v, std::memory_order_relaxed);
                s_[i].key.store(k, std::memory_order_release);   // publishes the value with the key
                return;
            }
        }
        overflow_[k] = v;
        hasOverflow_.store(true, std::memory_order_release);
    }
};

PtrTable<const Layout*, 4096> g_layouts;      // nullptr: a layout without a position element
PtrTable<uint8_t, 32768> g_alphaPs;           // RtAlphaDiscard, RtAlphaFoliage or none
std::mutex g_ownM;
std::vector<std::unique_ptr<Layout>> g_owned;   // every record stays alive: a reader may still hold one

bool Layer(ID3D11DeviceContext* real) { return raw::tx::IsLayerContext(real); }
} // namespace

void RecordLayout(ID3D11InputLayout* il, const Layout* L)
{
    const Layout* rec = nullptr;
    if (L) {
        std::lock_guard<std::mutex> g(g_ownM);
        g_owned.push_back(std::make_unique<Layout>(*L));
        rec = g_owned.back().get();
    }
    g_layouts.Put(il, rec);   // a reused address without a position no longer finds the old layout
}

void RecordPixelShader(ID3D11PixelShader* ps, uint8_t kind) { g_alphaPs.Put(ps, kind); }

const Layout* LayoutOf(ID3D11InputLayout* il)
{
    const Layout* L = nullptr;
    return g_layouts.Find(il, L) ? L : nullptr;
}

bool FindLayout(ID3D11InputLayout* il, Layout& out)
{
    const Layout* L = LayoutOf(il);
    if (L) out = *L;
    return L != nullptr;
}

uint8_t AlphaKindOf(ID3D11PixelShader* ps)
{
    uint8_t k = raw::bridge::RtAlphaNone;
    return g_alphaPs.Find(ps, k) ? k : uint8_t(raw::bridge::RtAlphaNone);
}

bool IsDiscardPs(ID3D11PixelShader* ps) { return AlphaKindOf(ps) == raw::bridge::RtAlphaDiscard; }

ID3D11InputLayout* BoundLayout(ID3D11DeviceContext* real)
{
    if (Layer(real)) return raw::tx::PeekInputLayout(real);
    ID3D11InputLayout* il = nullptr;
    real->IAGetInputLayout(&il);
    if (il) il->Release();
    return il;
}

ID3D11PixelShader* BoundPs(ID3D11DeviceContext* real)
{
    if (Layer(real)) return raw::tx::PeekPixelShader(real);
    ID3D11PixelShader* ps = nullptr;
    real->PSGetShader(&ps, nullptr, nullptr);
    if (ps) ps->Release();
    return ps;
}

// Alpha to coverage on a single-sample target cuts at alpha 0.5 like a test; a blended lit draw in the
// opaque set cuts by its alpha too (the primary-ray view showed the sapling and the clover as solid
// opaque cards with neither a discard nor coverage). Returns RtAlphaCoverage, RtAlphaBlend or none.
uint8_t BlendAlpha(ID3D11DeviceContext* real)
{
    D3D11_BLEND_DESC local{};
    const D3D11_BLEND_DESC* d = nullptr;
    if (Layer(real)) {
        d = raw::tx::PeekBlendDesc(real);
    } else {
        ID3D11BlendState* bs = nullptr;
        FLOAT f[4];
        UINT m = 0;
        real->OMGetBlendState(&bs, f, &m);
        if (bs) { bs->GetDesc(&local); bs->Release(); d = &local; }
    }
    if (!d) return raw::bridge::RtAlphaNone;
    if (d->AlphaToCoverageEnable) return raw::bridge::RtAlphaCoverage;
    const D3D11_RENDER_TARGET_BLEND_DESC& t = d->RenderTarget[0];
    const bool usesAlpha = t.SrcBlend == D3D11_BLEND_SRC_ALPHA || t.DestBlend == D3D11_BLEND_INV_SRC_ALPHA;
    return t.BlendEnable && usesAlpha ? raw::bridge::RtAlphaBlend : raw::bridge::RtAlphaNone;
}

ID3D11ShaderResourceView* AlphaTexture(ID3D11DeviceContext* real)
{
    if (Layer(real)) {
        D3D11_SRV_DIMENSION dim = D3D11_SRV_DIMENSION_UNKNOWN;
        ID3D11ShaderResourceView* v = raw::tx::PeekShaderResource(real, 4, 0, &dim);
        if (!v || dim != D3D11_SRV_DIMENSION_TEXTURE2D) return nullptr;
        raw::tx::HoldView(v, +1);
        return v;
    }
    ID3D11ShaderResourceView* v = nullptr;
    real->PSGetShaderResources(0, 1, &v);
    if (!v) return nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC d{};
    v->GetDesc(&d);
    if (d.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D) { v->Release(); return nullptr; }
    return v;
}

namespace
{
bool g_follow = true;
std::mutex g_followM;
std::unordered_set<uint64_t> g_alphaRanges;   // index ranges a discarding draw has drawn
uint64_t g_followed = 0;
} // namespace

void SetAlphaFollow(bool on) { g_follow = on; }
uint64_t AlphaFollowed() { return g_followed; }

bool AlphaFollows(ID3D11DeviceContext* real, UINT indexCount, UINT startIndex, INT baseVertex)
{
    if (!g_follow || indexCount < 3) return false;
    DXGI_FORMAT f = DXGI_FORMAT_UNKNOWN;
    UINT io = 0;
    ID3D11Buffer* ib = nullptr;
    if (Layer(real)) ib = raw::tx::PeekIndexBuffer(real, &f, &io);
    else { real->IAGetIndexBuffer(&ib, &f, &io); if (ib) ib->Release(); }
    if (!ib) return false;
    // The index buffer and the range in it: one mesh's, whichever actor draws it (several share one mesh's buffers).
    const uint64_t key = (reinterpret_cast<uint64_t>(ib) * 0x9E3779B97F4A7C15ull) ^ (uint64_t(io) << 7) ^ (uint64_t(startIndex) << 21) ^
                         (uint64_t(indexCount) * 1099511628211ull) ^ (uint64_t(uint32_t(baseVertex)) << 43);
    std::lock_guard<std::mutex> g(g_followM);
    if (IsDiscardPs(BoundPs(real))) {
        if (g_alphaRanges.size() > 65536) g_alphaRanges.clear();   // a bound: released buffers' addresses come back
        g_alphaRanges.insert(key);
        return false;
    }
    if (g_alphaRanges.count(key) == 0) return false;
    ++g_followed;
    return true;
}

ID3D11Buffer* VertexBuffer(ID3D11DeviceContext* real, UINT slot, UINT* stride, UINT* offset)
{
    ID3D11Buffer* b = nullptr;
    if (Layer(real)) { b = raw::tx::PeekVertexBuffer(real, slot, stride, offset); if (b) raw::tx::HoldBuffer(b, +1); }
    else real->IAGetVertexBuffers(slot, 1, &b, stride, offset);
    return b;
}

ID3D11Buffer* IndexBuffer(ID3D11DeviceContext* real, DXGI_FORMAT* format, UINT* offset)
{
    ID3D11Buffer* b = nullptr;
    if (Layer(real)) { b = raw::tx::PeekIndexBuffer(real, format, offset); if (b) raw::tx::HoldBuffer(b, +1); }
    else real->IAGetIndexBuffer(&b, format, offset);
    return b;
}

namespace
{
// The references of a draw captured on raw::tx: the layer's plain host references, given back on the
// render thread like they were taken (RtScene and the proxy release draws there).
void DropPlain(raw::bridge::RtDraw& d)
{
    if (d.vb) raw::tx::HoldBuffer(d.vb, -1);
    if (d.ib) raw::tx::HoldBuffer(d.ib, -1);
    if (d.uvVb) raw::tx::HoldBuffer(d.uvVb, -1);
    if (d.blendVb) raw::tx::HoldBuffer(d.blendVb, -1);
    if (d.alphaSrv) raw::tx::HoldView(d.alphaSrv, -1);
    d.vb = d.ib = d.uvVb = d.blendVb = nullptr;
    d.alphaSrv = nullptr;
}
} // namespace

void (*DropFor(ID3D11DeviceContext* real))(raw::bridge::RtDraw&) { return Layer(real) ? &DropPlain : nullptr; }

} // namespace SB::Proxy::geom
