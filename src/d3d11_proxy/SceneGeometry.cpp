// SceneGeometry: the frame's rigid opaque lit draws for the ray-tracing scene (RtScene.h). At each
// indexed draw that SceneLights counted as a lit draw of the opaque set, the bound vertex and index
// buffers, the position element of the input layout and the draw's world matrix are recorded.
// Left out: soft-lit foliage, alpha-tested draws (their pixel shader discards; a BLAS of the full quads would shadow
// like solid cards), skinned draws (their vertex buffer 2 is not the 15-vector world block, so
// SceneLights never counts them) and non-indexed draws. The game's own shadow mask still carries
// those shadows; the lighting pass takes the darker of the two. Original work, MIT.
#include "SceneGeometry.h"
#include "SceneLights.h"
#include "ProxyLog.h"
#include "../core/dxbc/GBufferPatch.h"

#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace SB::Proxy
{
namespace
{
struct Pos { UINT slot = 0, offset = 0; DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN; };
std::mutex g_m;
std::unordered_map<ID3D11InputLayout*, Pos> g_layouts;
std::unordered_set<ID3D11PixelShader*> g_discard;
std::vector<raw::bridge::RtDraw> g_frame;
uint64_t g_alphaTested = 0, g_noLayout = 0;

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

void SceneGeometry_OnInputLayout(ID3D11InputLayout* layout, const D3D11_INPUT_ELEMENT_DESC* e, UINT n)
{
    if (!layout || !e) return;
    for (UINT i = 0; i < n; ++i) {
        if (_stricmp(e[i].SemanticName, "POSITION") != 0 || e[i].SemanticIndex != 0) continue;
        std::lock_guard<std::mutex> g(g_m);
        g_layouts[layout] = {e[i].InputSlot, e[i].AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT ? 0u : e[i].AlignedByteOffset, e[i].Format};
        return;
    }
}

void SceneGeometry_OnPixelShader(ID3D11PixelShader* ps, const void* bc, size_t n)
{
    // Soft-lit foliage (grass clumps, bushes) is left out too: blades and twigs as solid triangles
    // cast slab shadows; the game's mask carries their dappled shadow.
    if (!ps || (!HasDiscard(bc, n) && raw::dxbc::Inspect(bc, n).materialClass != raw::dxbc::material::SoftLit)) return;
    std::lock_guard<std::mutex> g(g_m);
    g_discard.insert(ps);
}

void SceneGeometry_OnDraw(ID3D11DeviceContext* real, UINT indexCount, UINT startIndex, INT baseVertex)
{
    const float* w = SceneLights_DrawWorld();
    if (!w || indexCount < 3) return;
    ID3D11PixelShader* ps = nullptr;
    real->PSGetShader(&ps, nullptr, nullptr);
    bool alpha;
    { std::lock_guard<std::mutex> g(g_m); alpha = ps && g_discard.count(ps); }
    if (ps) ps->Release();
    if (alpha) { ++g_alphaTested; return; }
    ID3D11InputLayout* il = nullptr;
    real->IAGetInputLayout(&il);
    Pos pos;
    bool found;
    { std::lock_guard<std::mutex> g(g_m); auto it = g_layouts.find(il); found = it != g_layouts.end(); if (found) pos = it->second; }
    if (il) il->Release();
    const bool fmtOk = pos.format == DXGI_FORMAT_R32G32B32_FLOAT || pos.format == DXGI_FORMAT_R32G32B32A32_FLOAT ||
                       pos.format == DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (!found || !fmtOk) { ++g_noLayout; return; }
    raw::bridge::RtDraw d;
    UINT stride = 0, offset = 0;
    real->IAGetVertexBuffers(pos.slot, 1, &d.vb, &stride, &offset);
    real->IAGetIndexBuffer(&d.ib, &d.ibFormat, &d.ibOffset);
    if (!d.vb || !d.ib) { if (d.vb) d.vb->Release(); if (d.ib) d.ib->Release(); return; }
    d.stride = stride; d.vbOffset = offset; d.posOffset = pos.offset; d.posFormat = pos.format;
    d.indexCount = indexCount; d.startIndex = startIndex; d.baseVertex = baseVertex;
    std::memcpy(d.world, w, sizeof d.world);
    g_frame.push_back(d);   // the Get calls added the references the bridge releases
}

std::vector<raw::bridge::RtDraw> SceneGeometry_Take()
{
    static uint64_t frames = 0;
    if (++frames % 600 == 1)
        Log("[RT] frame %llu: %zu rigid opaque draws captured; left out so far: %llu alpha-tested, %llu without a usable position element",
            (unsigned long long)frames, g_frame.size(), (unsigned long long)g_alphaTested, (unsigned long long)g_noLayout);
    std::vector<raw::bridge::RtDraw> out;
    out.swap(g_frame);
    return out;
}

void SceneGeometry_Drop()
{
    for (auto& d : g_frame) { if (d.vb) d.vb->Release(); if (d.ib) d.ib->Release(); }
    g_frame.clear();
}

} // namespace SB::Proxy
