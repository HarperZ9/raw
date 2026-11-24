// SceneGeometry: the frame's rigid lit draws for the ray-tracing scene (RtScene.h). At each indexed
// draw that SceneLights counted as a lit draw of the opaque set, the bound vertex and index buffers,
// the position element of the input layout and the draw's world matrix are recorded.
// Alpha-tested draws (the pixel shader discards, the blend state turns alpha into coverage, or the
// shader is soft-lit foliage) also carry their texture coordinate element and the texture at t0, so
// the rays test the same alpha the raster does; without either they are left out as before (a BLAS of
// the full quads would shadow like solid cards). Draws with a skinned layout go to SceneSkin.cpp;
// non-indexed draws are left out.
// Every reason a draw is left out is counted and logged. Original work, MIT.
#include "SceneGeometry.h"
#include "SceneGeometryDetail.h"
#include "SceneLights.h"
#include "ProxyLog.h"
#include "CBDirtyTracker.h"
#include "../core/dxbc/GBufferPatch.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace SB::Proxy
{
using geom::AlphaTexture;
using geom::BlendAlpha;
using geom::Layout;
namespace
{
std::vector<raw::bridge::RtDraw> g_frame;
// Why draws were left out, and how many alpha-tested ones joined, since the start.
struct Counts { uint64_t alphaNoTexture = 0, alphaNoUv = 0, alphaOff = 0, noLayout = 0, badFormat = 0, noBuffers = 0,
                         alphaJoined[5] = {}, opaque = 0, opaqueMat = 0, alphaFollowed = 0, lodLand = 0; } g_n;
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
void SceneGeometry_SetAlphaFollow(bool on) { geom::SetAlphaFollow(on); }

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
    geom::RecordLayout(layout, L.pos.set ? &L : nullptr);
}

void SceneGeometry_OnPixelShader(ID3D11PixelShader* ps, const void* bc, size_t n)
{
    // Soft-lit foliage (grass clumps, bushes) is left out too: blades and twigs as solid triangles
    // cast slab shadows; the game's mask carries their dappled shadow.
    if (!ps) return;
    const uint8_t kind = HasDiscard(bc, n) ? raw::bridge::RtAlphaDiscard
                         : raw::dxbc::Inspect(bc, n).materialClass == raw::dxbc::material::SoftLit ? raw::bridge::RtAlphaFoliage
                                                                                                   : raw::bridge::RtAlphaNone;
    geom::RecordPixelShader(ps, kind);   // none too: a reused address drops the old shader's kind
}

namespace
{
// Distant land: the game's LOD terrain chunks, a uniform scale of 4 or more (4, 8, 16, 32) with no rotation, whose
// vertex buffer 0 starts with the loaded area (centre xy, half size zw near 2.5 cells; 1042, -12, 10225, 10225 beside
// Riverwood, 2026-11-19). The chunks lie under the loaded cells too, where the game's vertex shader keeps them below the
// full-detail ground; as written in their vertex buffers they stand up to tens of units above it in hollows, and every
// sun, sky and light ray from the ground beneath them hit them (the black patch beside Riverwood, docs/RAYTRACING.md).
bool LodLand(ID3D11DeviceContext* real, const float* w, float rect[4])
{
    const float s = w[0];
    if (s < 3.9f || std::fabs(w[5] - s) > 1e-3f || std::fabs(w[10] - s) > 1e-3f) return false;
    if (std::fabs(w[1]) + std::fabs(w[2]) + std::fabs(w[4]) + std::fabs(w[6]) + std::fabs(w[8]) + std::fabs(w[9]) > 1e-3f) return false;
    ID3D11Buffer* cb = nullptr;
    real->VSGetConstantBuffers(0, 1, &cb);
    uint32_t size = 0;
    const uint8_t* p = cb ? CBDirtyTracker::Get().Shadow(cb, &size) : nullptr;
    if (cb) cb->Release();
    if (!p || size < 16) return false;
    std::memcpy(rect, p, 16);
    return rect[2] > 1000.0f && rect[3] > 1000.0f && rect[2] < 1.0e6f && rect[3] < 1.0e6f;
}

bool UvFormatOk(DXGI_FORMAT f)
{
    return f == DXGI_FORMAT_R16G16_FLOAT || f == DXGI_FORMAT_R32G32_FLOAT || f == DXGI_FORMAT_R16G16B16A16_FLOAT ||
           f == DXGI_FORMAT_R32G32B32A32_FLOAT || f == DXGI_FORMAT_R32G32B32_FLOAT;
}
} // namespace

void SceneGeometry_OnDraw(ID3D11DeviceContext* real, UINT indexCount, UINT startIndex, INT baseVertex)
{
    // One layout lookup per draw, shared by the census, the skinned path and the rigid path.
    const Layout* bound = geom::LayoutOf(geom::BoundLayout(real));
    SceneGeometry_CensusWith(bound, 0);
    // A skinned layout goes the skinned way whether or not the lit-draw decode found a world block: the
    // actors' draws carry one (434,193 of 538,843 skinned opaque-set draws in Riverwood), and as rigid
    // meshes their bind-pose positions under that matrix put nothing where the body is.
    const bool follows = geom::AlphaFollows(real, indexCount, startIndex, baseVertex);
    if (SceneSkin_OnDraw(real, bound, indexCount, startIndex, baseVertex, follows)) return;
    const float* w = SceneLights_DrawWorld();
    if (!w || indexCount < 3) return;
    SceneSkin_NoteMainView(real);   // the main view, from the frame's first rigid lit draw
    uint8_t alpha = geom::AlphaKindOf(geom::BoundPs(real));
    if (!alpha) alpha = BlendAlpha(real);
    // Exclude non-depth-writing alpha-blended ground decals (ant trails, blood/dirt/snow/scorch decals):
    // flat decals float 1 cm above terrain/stumps and must never enter the DXR TLAS as square shadow casters!
    if (alpha == raw::bridge::RtAlphaBlend) {
        ID3D11DepthStencilState* ds = nullptr;
        UINT sr = 0;
        real->OMGetDepthStencilState(&ds, &sr);
        bool writesDepth = true;
        if (ds) {
            D3D11_DEPTH_STENCIL_DESC dsd{};
            ds->GetDesc(&dsd);
            ds->Release();
            writesDepth = (dsd.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL);
        }
        if (!writesDepth) return;
    }
    if (!alpha && follows) { alpha = raw::bridge::RtAlphaDiscard; ++g_n.alphaFollowed; }   // its prepass tested the alpha
    if (alpha && !g_alphaOn) { ++g_n.alphaOff; return; }
    if (!bound) { ++g_n.noLayout; return; }
    const Layout& L = *bound;
    const DXGI_FORMAT pf = L.pos.format;
    if (pf != DXGI_FORMAT_R32G32B32_FLOAT && pf != DXGI_FORMAT_R32G32B32A32_FLOAT && pf != DXGI_FORMAT_R16G16B16A16_FLOAT) { ++g_n.badFormat; return; }
    raw::bridge::RtDraw d;
    d.drop = geom::DropFor(real);   // before any reference is taken
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
    d.vb = geom::VertexBuffer(real, L.pos.slot, &stride, &offset);
    d.ib = geom::IndexBuffer(real, &d.ibFormat, &d.ibOffset);
    if (!d.vb || !d.ib) { raw::bridge::ReleaseRtDraw(d); ++g_n.noBuffers; return; }
    d.stride = stride; d.vbOffset = offset; d.posOffset = L.pos.offset; d.posFormat = pf;
    d.indexCount = indexCount; d.startIndex = startIndex; d.baseVertex = baseVertex;
    std::memcpy(d.world, w, sizeof d.world);
    // Alpha-tested too: most of the land's draws follow a prepass that tested the alpha (AlphaFollows).
    if (LodLand(real, w, d.lodRect)) { d.lodLand = true; ++g_n.lodLand; }
    if (alpha) ++g_n.alphaJoined[alpha]; else ++g_n.opaque;
    g_frame.push_back(d);   // the Get calls added the references the bridge releases
}

std::vector<raw::bridge::RtDraw> SceneGeometry_Take()
{
    static uint64_t frames = 0;
    if (++frames % 600 == 1)
        Log("[RT] frame %llu: %zu draws captured; so far %llu opaque (%llu with a colour texture), alpha-tested joined %llu discard / %llu coverage / %llu foliage / %llu blend; "
            "left out: %llu alpha without coordinates, %llu alpha without a 2D texture, %llu alpha (RtAlpha=0), %llu no layout, "
            "%llu position format, %llu no buffers; %llu rigid draws alpha-tested by their prepass; %llu distant-land draws",
            (unsigned long long)frames, g_frame.size(), (unsigned long long)g_n.opaque, (unsigned long long)g_n.opaqueMat, (unsigned long long)g_n.alphaJoined[1],
            (unsigned long long)g_n.alphaJoined[2], (unsigned long long)g_n.alphaJoined[3], (unsigned long long)g_n.alphaJoined[4], (unsigned long long)g_n.alphaNoUv,
            (unsigned long long)g_n.alphaNoTexture, (unsigned long long)g_n.alphaOff, (unsigned long long)g_n.noLayout,
            (unsigned long long)g_n.badFormat, (unsigned long long)g_n.noBuffers, (unsigned long long)g_n.alphaFollowed,
            (unsigned long long)g_n.lodLand);
    if (frames % 600 == 1) SceneSkin_Log();
    std::vector<raw::bridge::RtDraw> out;
    out.swap(g_frame);
    SceneSkin_Took(out);   // the last frame's actors drawn after its capture
    SceneSkin_FilterViews(out);
    return out;
}

void SceneGeometry_Drop()
{
    SceneSkin_Drop(g_frame);   // skinned draws after this frame's capture wait for the next frame
    g_frame.clear();
}


namespace geom
{
std::vector<raw::bridge::RtDraw>& Frame() { return g_frame; }
} // namespace geom

} // namespace SB::Proxy
