// SceneGeometry: the frame's rigid lit draws for the ray-tracing scene (RtScene.h). At each indexed
// draw that SceneLights counted as a lit draw of the opaque set, the bound vertex and index buffers,
// the position element of the input layout and the draw's world matrix are recorded.
// Alpha-tested draws (the pixel shader discards, the blend state turns alpha into coverage, or the
// shader is soft-lit foliage) also carry their texture coordinate element and the texture at t0, so
// the rays test the same alpha the raster does; without either they are left out as before (a BLAS of
// the full quads would shadow like solid cards). Skinned draws (their vertex buffer 2 is not the
// 15-vector world block, so SceneLights never counts them) and non-indexed draws are left out. The
// game's own shadow mask still carries those shadows; the lighting pass takes the darker of the two.
// Every reason a draw is left out is counted and logged. Original work, MIT.
#include "SceneGeometry.h"
#include "SceneLights.h"
#include "ProxyLog.h"
#include "CBDirtyTracker.h"
#include "../core/dxbc/GBufferPatch.h"

#include <cstring>
#include <mutex>
#include <unordered_map>

namespace SB::Proxy
{
namespace
{
struct Elem { UINT slot = 0, offset = 0; DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN; bool set = false; };
struct Layout { Elem pos, uv, blendIdx, blendW; };
std::mutex g_m;
std::unordered_map<ID3D11InputLayout*, Layout> g_layouts;
std::unordered_map<ID3D11PixelShader*, uint8_t> g_alphaPs;   // RtAlphaDiscard or RtAlphaFoliage
std::vector<raw::bridge::RtDraw> g_frame;
// Why draws were left out, and how many alpha-tested ones joined, since the start.
struct Counts { uint64_t alphaNoTexture = 0, alphaNoUv = 0, alphaOff = 0, noLayout = 0, badFormat = 0, noBuffers = 0,
                         alphaJoined[5] = {}, opaque = 0, opaqueMat = 0; } g_n;
bool g_alphaOn = true;
bool g_matOn = true;

// True when the shader bytecode holds a discard instruction (SM4/5 opcode 13).
bool HasDiscard(const void* bc, size_t n)
{
    const uint8_t* b = static_cast<const uint8_t*>(bc);
    if (n < 32 || std::memcmp(b, "DXBC", 4) != 0) return false;
    uint32_t chunks = 0;
    std::memcpy(&chunks, b + 28, 4);
    for (uint32_t i = 0; i < chunks && 32 + i * 4 + 4 <= n; ++i) {
        uint32_t off = 0;
        std::memcpy(&off, b + 32 + i * 4, 4);
        if (off + 8 > n) continue;
        if (std::memcmp(b + off, "SHEX", 4) != 0 && std::memcmp(b + off, "SHDR", 4) != 0) continue;
        uint32_t size = 0;
        std::memcpy(&size, b + off + 4, 4);
        const uint32_t* tok = reinterpret_cast<const uint32_t*>(b + off + 8);
        const size_t count = std::min<size_t>(size / 4, (n - off - 8) / 4);
        for (size_t k = 2; k < count;) {
            const uint32_t t = tok[k], op = t & 0x7FF;
            uint32_t len = (t >> 24) & 0x7F;
            if (op == 35 && k + 1 < count) len = tok[k + 1];   // customdata: the length is the next token
            if (op == 13) return true;
            if (len == 0) break;
            k += len;
        }
    }
    return false;
}
} // namespace

void SceneGeometry_SetAlpha(bool on) { g_alphaOn = on; }
void SceneGeometry_SetMaterials(bool on) { g_matOn = on; }

// Byte size of a vertex element format, for D3D11_APPEND_ALIGNED_ELEMENT offsets.
static UINT FormatBytes(DXGI_FORMAT f)
{
    switch (f) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_UINT: case DXGI_FORMAT_R32G32B32A32_SINT: return 16;
    case DXGI_FORMAT_R32G32B32_FLOAT: case DXGI_FORMAT_R32G32B32_UINT: case DXGI_FORMAT_R32G32B32_SINT: return 12;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT: case DXGI_FORMAT_R16G16B16A16_SINT: case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT: case DXGI_FORMAT_R32G32_SINT: return 8;
    case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R16G16_UNORM: case DXGI_FORMAT_R16G16_SNORM: case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_SNORM: case DXGI_FORMAT_R8G8B8A8_UINT: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT: case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R11G11B10_FLOAT: return 4;
    default: return 0;
    }
}

void SceneGeometry_OnInputLayout(ID3D11InputLayout* layout, const D3D11_INPUT_ELEMENT_DESC* e, UINT n)
{
    if (!layout || !e) return;
    Layout L;
    UINT next[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
    for (UINT i = 0; i < n; ++i) {
        const UINT slot = e[i].InputSlot < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT ? e[i].InputSlot : 0;
        const UINT off = e[i].AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT ? next[slot] : e[i].AlignedByteOffset;
        next[slot] = off + FormatBytes(e[i].Format);
        if (e[i].SemanticIndex != 0) continue;
        if (_stricmp(e[i].SemanticName, "POSITION") == 0) L.pos = {e[i].InputSlot, off, e[i].Format, true};
        else if (_stricmp(e[i].SemanticName, "TEXCOORD") == 0) L.uv = {e[i].InputSlot, off, e[i].Format, true};
        else if (_stricmp(e[i].SemanticName, "BLENDINDICES") == 0) L.blendIdx = {e[i].InputSlot, off, e[i].Format, true};
        else if (_stricmp(e[i].SemanticName, "BLENDWEIGHT") == 0) L.blendW = {e[i].InputSlot, off, e[i].Format, true};
    }
    if (!L.pos.set) return;
    std::lock_guard<std::mutex> g(g_m);
    g_layouts[layout] = L;
}

void SceneGeometry_OnPixelShader(ID3D11PixelShader* ps, const void* bc, size_t n)
{
    // Soft-lit foliage (grass clumps, bushes) is left out too: blades and twigs as solid triangles
    // cast slab shadows; the game's mask carries their dappled shadow.
    if (!ps) return;
    const uint8_t kind = HasDiscard(bc, n) ? raw::bridge::RtAlphaDiscard
                         : raw::dxbc::Inspect(bc, n).materialClass == raw::dxbc::material::SoftLit ? raw::bridge::RtAlphaFoliage
                                                                                                   : raw::bridge::RtAlphaNone;
    if (!kind) return;
    std::lock_guard<std::mutex> g(g_m);
    g_alphaPs[ps] = kind;
}

namespace
{
// Alpha to coverage on a single-sample target cuts at alpha 0.5 like a test; a blended lit draw in the
// opaque set cuts by its alpha too (the primary-ray view showed the sapling and the clover as solid
// opaque cards with neither a discard nor coverage). Returns RtAlphaCoverage, RtAlphaBlend or none.
uint8_t BlendAlpha(ID3D11DeviceContext* real)
{
    ID3D11BlendState* bs = nullptr;
    FLOAT f[4];
    UINT m = 0;
    real->OMGetBlendState(&bs, f, &m);
    if (!bs) return raw::bridge::RtAlphaNone;
    D3D11_BLEND_DESC d{};
    bs->GetDesc(&d);
    bs->Release();
    if (d.AlphaToCoverageEnable) return raw::bridge::RtAlphaCoverage;
    const D3D11_RENDER_TARGET_BLEND_DESC& t = d.RenderTarget[0];
    const bool usesAlpha = t.SrcBlend == D3D11_BLEND_SRC_ALPHA || t.DestBlend == D3D11_BLEND_INV_SRC_ALPHA;
    return t.BlendEnable && usesAlpha ? raw::bridge::RtAlphaBlend : raw::bridge::RtAlphaNone;
}

bool UvFormatOk(DXGI_FORMAT f)
{
    return f == DXGI_FORMAT_R16G16_FLOAT || f == DXGI_FORMAT_R32G32_FLOAT || f == DXGI_FORMAT_R16G16B16A16_FLOAT ||
           f == DXGI_FORMAT_R32G32B32A32_FLOAT || f == DXGI_FORMAT_R32G32B32_FLOAT;
}

// The texture at t0 when it is a plain 2D texture view, with a reference.
ID3D11ShaderResourceView* AlphaTexture(ID3D11DeviceContext* real)
{
    ID3D11ShaderResourceView* v = nullptr;
    real->PSGetShaderResources(0, 1, &v);
    if (!v) return nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC d{};
    v->GetDesc(&d);
    if (d.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D) { v->Release(); return nullptr; }
    return v;
}
} // namespace

// Diagnosis for skinned actors (their per-frame BLAS, docs/RAYTRACING.md): a draw the lit-draw decode
// did not count whose input layout carries blend indices. Logs its layout and the sizes of the vertex
// constant buffers bound, a few times, so the bone palette's slot and layout can be read off the log.
static void SkinnedProbe(ID3D11DeviceContext* real, UINT indexCount)
{
    static uint32_t seen = 0, logged = 0;
    if (logged >= 12) return;
    ID3D11InputLayout* il = nullptr;
    real->IAGetInputLayout(&il);
    Layout L;
    bool found;
    { std::lock_guard<std::mutex> g(g_m); auto it = g_layouts.find(il); found = it != g_layouts.end(); if (found) L = it->second; }
    if (il) il->Release();
    if (!found || !L.blendIdx.set || (++seen % 97) != 1) return;
    ID3D11Buffer* cb[14] = {};
    real->VSGetConstantBuffers(0, 14, cb);
    char sizes[256]{};
    int at = 0;
    for (int k = 0; k < 14; ++k) {
        const uint32_t n = cb[k] ? CBDirtyTracker::Get().GetShadowSize(cb[k]) : 0;
        at += snprintf(sizes + at, sizeof sizes - at, " b%d=%u", k, n);
        if (cb[k]) cb[k]->Release();
    }
    ++logged;
    Log("[RT] skinned draw: %u indices; position slot %u offset %u format %d; blend indices slot %u offset %u format %d; weights offset %u format %d; "
        "coordinates offset %u format %d; vertex constant floats:%s",
        indexCount, L.pos.slot, L.pos.offset, int(L.pos.format), L.blendIdx.slot, L.blendIdx.offset, int(L.blendIdx.format), L.blendW.offset,
        int(L.blendW.format), L.uv.offset, int(L.uv.format), sizes);
}

void SceneGeometry_OnDraw(ID3D11DeviceContext* real, UINT indexCount, UINT startIndex, INT baseVertex)
{
    const float* w = SceneLights_DrawWorld();
    if (!w) SkinnedProbe(real, indexCount);
    if (!w || indexCount < 3) return;
    ID3D11PixelShader* ps = nullptr;
    real->PSGetShader(&ps, nullptr, nullptr);
    uint8_t alpha = raw::bridge::RtAlphaNone;
    {
        std::lock_guard<std::mutex> g(g_m);
        auto it = ps ? g_alphaPs.find(ps) : g_alphaPs.end();
        if (it != g_alphaPs.end()) alpha = it->second;
    }
    if (ps) ps->Release();
    if (!alpha) alpha = BlendAlpha(real);
    if (alpha && !g_alphaOn) { ++g_n.alphaOff; return; }
    ID3D11InputLayout* il = nullptr;
    real->IAGetInputLayout(&il);
    Layout L;
    bool found;
    { std::lock_guard<std::mutex> g(g_m); auto it = g_layouts.find(il); found = it != g_layouts.end(); if (found) L = it->second; }
    if (il) il->Release();
    if (!found) { ++g_n.noLayout; return; }
    const DXGI_FORMAT pf = L.pos.format;
    if (pf != DXGI_FORMAT_R32G32B32_FLOAT && pf != DXGI_FORMAT_R32G32B32A32_FLOAT && pf != DXGI_FORMAT_R16G16B16A16_FLOAT) { ++g_n.badFormat; return; }
    raw::bridge::RtDraw d;
    if (alpha) {
        // The rays read the coordinates from the position's own vertex buffer.
        // Coordinates in their own stream belong to the tree shaders, whose positions the vertex shader
        // still moves (the primary-ray view showed their leaves scattered and huge): left out for now.
        if (!L.uv.set || L.uv.slot != L.pos.slot || !UvFormatOk(L.uv.format)) {
            if (g_n.alphaNoUv++ < 64 && (g_n.alphaNoUv & 7) == 1)
                Log("[RT] alpha-tested draw left out: coordinates %s, slot %u (position slot %u), format %d", L.uv.set ? "found" : "absent",
                    L.uv.slot, L.pos.slot, int(L.uv.format));
            return;
        }
        d.alphaSrv = AlphaTexture(real);
        if (!d.alphaSrv) { ++g_n.alphaNoTexture; return; }
        d.alphaKind = alpha; d.uvOffset = L.uv.offset; d.uvFormat = L.uv.format;
    } else if (g_matOn && L.uv.set && L.uv.slot == L.pos.slot && UvFormatOk(L.uv.format)) {
        // An opaque draw: its colour texture (t0 of the lit shaders) and coordinates, so an off-screen
        // ray hit is shaded from the game's own material. Without them it still joins, uncoloured.
        d.alphaSrv = AlphaTexture(real);
        if (d.alphaSrv) { d.uvOffset = L.uv.offset; d.uvFormat = L.uv.format; ++g_n.opaqueMat; }
    }
    UINT stride = 0, offset = 0;
    real->IAGetVertexBuffers(L.pos.slot, 1, &d.vb, &stride, &offset);
    real->IAGetIndexBuffer(&d.ib, &d.ibFormat, &d.ibOffset);
    if (!d.vb || !d.ib) { raw::bridge::ReleaseRtDraw(d); ++g_n.noBuffers; return; }
    d.stride = stride; d.vbOffset = offset; d.posOffset = L.pos.offset; d.posFormat = pf;
    d.indexCount = indexCount; d.startIndex = startIndex; d.baseVertex = baseVertex;
    std::memcpy(d.world, w, sizeof d.world);
    if (alpha) ++g_n.alphaJoined[alpha]; else ++g_n.opaque;
    g_frame.push_back(d);   // the Get calls added the references the bridge releases
}

std::vector<raw::bridge::RtDraw> SceneGeometry_Take()
{
    static uint64_t frames = 0;
    if (++frames % 600 == 1)
        Log("[RT] frame %llu: %zu draws captured; so far %llu opaque (%llu with a colour texture), alpha-tested joined %llu discard / %llu coverage / %llu foliage / %llu blend; "
            "left out: %llu alpha without coordinates, %llu alpha without a 2D texture, %llu alpha (RtAlpha=0), %llu no layout, "
            "%llu position format, %llu no buffers",
            (unsigned long long)frames, g_frame.size(), (unsigned long long)g_n.opaque, (unsigned long long)g_n.opaqueMat, (unsigned long long)g_n.alphaJoined[1],
            (unsigned long long)g_n.alphaJoined[2], (unsigned long long)g_n.alphaJoined[3], (unsigned long long)g_n.alphaJoined[4], (unsigned long long)g_n.alphaNoUv,
            (unsigned long long)g_n.alphaNoTexture, (unsigned long long)g_n.alphaOff, (unsigned long long)g_n.noLayout,
            (unsigned long long)g_n.badFormat, (unsigned long long)g_n.noBuffers);
    std::vector<raw::bridge::RtDraw> out;
    out.swap(g_frame);
    return out;
}

void SceneGeometry_Drop()
{
    for (auto& d : g_frame) raw::bridge::ReleaseRtDraw(d);
    g_frame.clear();
}

} // namespace SB::Proxy
