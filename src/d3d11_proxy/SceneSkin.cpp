// SceneSkin: skinned draws for the ray-tracing scene (RtSkin.h, docs/RAYTRACING.md). A draw whose input
// layout carries blend indices joins with its streams, index range and bone palette (vertex constant
// buffer RtSkinBones), whether or not the lit-draw decode found a world block for it: the trees' wind
// bones and the actors (people, creatures, the player in third person). In the opaque set it joins that
// frame; on the main depth target outside it (RtActors) it joins too, and drawn after the frame's capture
// it waits for the next frame. Every reason a draw is left out is counted and logged, with a census of
// the opaque set's draws by call and layout. Original work, MIT.
#include "SceneGeometry.h"
#include "SceneGeometryDetail.h"
#include "SceneLights.h"
#include "ProxyLog.h"
#include "ShaderManager.h"
#include "CBDirtyTracker.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>

namespace SB::Proxy
{
using geom::Layout;
using geom::BlendAlpha;
namespace
{
bool g_skinOn = true;
bool g_skinAlphaOn = true;   // RtSkinAlpha: alpha-tested skinned draws join, cut by their texture's alpha
uint64_t g_skinAlphaJoined = 0, g_skinAlphaNoUv = 0;
int g_boneSlot = 10;
uint64_t g_skinJoined = 0, g_skinNoBones = 0, g_skinNotOpaque = 0, g_skinFormat = 0, g_skinAlpha = 0;
// Actors (people, creatures, the player in third person) draw skinned outside the opaque set's targets.
// Those on the main depth target join; drawn after the frame's capture, they wait for the next frame.
int g_skinFilter = 0;   // diagnosis (RtSkinFilter): 1 float positions only, 2 half positions only, 3 trees only, 4 actors only
bool g_actors = true, g_mainDepth = false, g_afterCapture = false, g_took = false;
uint64_t g_skinNoPs = 0, g_skinAlphaFollowed = 0;   // skinned draws with no pixel shader, left out
UINT g_tgtRtvs = 0, g_tgtWidth = 0;
DXGI_FORMAT g_tgtFormat = DXGI_FORMAT_UNKNOWN;
uint64_t g_actorJoined = 0, g_actorLate = 0, g_actorWorld = 0;
std::unordered_map<uint64_t, uint64_t> g_outside;   // skinned draws outside the opaque set, by the targets bound
std::vector<raw::bridge::RtDraw> g_carry;           // joined after the capture: the next frame's
uint64_t g_census[4][2][2] = {};   // opaque-set draws by call (indexed, indexed instanced, instanced, plain), skinned layout, world block
} // namespace

void SceneGeometry_SetActors(bool on) { g_actors = on; }
void SceneGeometry_SetSkinAlpha(bool on) { g_skinAlphaOn = on; }

// The actor sheet (docs/RAYTRACING.md): RtSkin skins the actors' draws exactly as their own vertex shaders do, yet
// some corners land far from anything the main view shows. A draw whose view-projection (vertex cb12 rows 8 to 11)
// is not the main view's belongs to another pass that also draws on the main depth target; RtActorView=1 leaves
// those out. The main view comes from the frame's rigid lit draws.
namespace
{
bool g_viewCheck = true, g_mainSet = false;
float g_main[16] = {};
uint64_t g_viewKept = 0, g_viewDropped = 0, g_viewUnknown = 0;
float g_worstDiff = 0.0f;
}
void SceneGeometry_SetActorView(bool on) { g_viewCheck = on; }
bool SceneSkin_ReadView(ID3D11DeviceContext* real, float out[16])
{
    ID3D11Buffer* b[1] = {};
    real->VSGetConstantBuffers(12, 1, b);
    if (!b[0]) return false;
    uint32_t n = 0;
    const uint8_t* p = CBDirtyTracker::Get().Shadow(b[0], &n);
    b[0]->Release();
    if (!p || n < 192) return false;
    std::memcpy(out, p + 128, 64);
    return true;
}
void SceneSkin_NoteMainView(ID3D11DeviceContext* real)
{
    if (!g_mainSet) g_mainSet = SceneSkin_ReadView(real, g_main);
}
// At the hand-over: actor draws whose view is not the main one leave the list (their references given back).
void SceneSkin_FilterViews(std::vector<raw::bridge::RtDraw>& out)
{
    if (g_mainSet) {
        float scale = 0.0f;
        for (float v : g_main) scale = std::max(scale, std::fabs(v));
        size_t w = 0;
        for (size_t k = 0; k < out.size(); ++k) {
            raw::bridge::RtDraw& d = out[k];
            bool keep = true;
            if (d.actor && d.hasView) {
                float diff = 0.0f;
                for (int i = 0; i < 16; ++i) diff = std::max(diff, std::fabs(d.viewProj[i] - g_main[i]));
                diff /= std::max(scale, 1.0e-6f);
                g_worstDiff = std::max(g_worstDiff, diff);
                keep = diff < 1.0e-3f;
                (keep ? g_viewKept : g_viewDropped)++;
                if (!g_viewCheck) keep = true;
            } else if (d.actor) {
                ++g_viewUnknown;
            }
            if (keep) out[w++] = d; else raw::bridge::ReleaseRtDraw(d);
        }
        out.resize(w);
    }
    g_mainSet = false;
    static uint32_t frames = 0;
    if (++frames % 600 == 1)
        Log("[RT] actor views: %llu draws on the main view, %llu on another view (%s), %llu without a view; largest difference %.4f of the matrix",
            (unsigned long long)g_viewKept, (unsigned long long)g_viewDropped, g_viewCheck ? "left out" : "kept, RtActorView=0",
            (unsigned long long)g_viewUnknown, g_worstDiff);
}
void SceneGeometry_SetSkinFilter(int f) { g_skinFilter = f; }
void SceneGeometry_CensusWith(const Layout* L, int call)
{
    if (!SceneLights_Opaque() || call < 0 || call > 3) return;
    ++g_census[call][L && L->blendIdx.set][SceneLights_DrawWorld() != nullptr];
}
void SceneGeometry_Census(ID3D11DeviceContext* real, int call)
{
    if (!SceneLights_Opaque() || call < 0 || call > 3) return;   // the lookup only for the opaque set's draws
    SceneGeometry_CensusWith(geom::LayoutOf(geom::BoundLayout(real)), call);
}
void SceneGeometry_OnTargets(bool mainDepth, UINT rtvs, DXGI_FORMAT rt0, UINT rt0Width, bool opaqueCaptured)
{
    g_mainDepth = mainDepth; g_tgtRtvs = rtvs; g_tgtFormat = rt0; g_tgtWidth = rt0Width; g_afterCapture = opaqueCaptured;
}
void SceneGeometry_SetSkin(bool on, int boneSlot) { g_skinOn = on; g_boneSlot = boneSlot; }

// Diagnosis for skinned actors (their per-frame BLAS, docs/RAYTRACING.md): a draw the lit-draw decode
// did not count whose input layout carries blend indices. Logs its layout and the sizes of the vertex
// constant buffers bound, a few times, so the bone palette's slot and layout can be read off the log.
// Diagnosis of the actor sheet ([RT] SkinCbCheck=1): the bone palette as the GPU holds it, copied back and read
// on the CPU (a stall), against the tracker's shadow copy that RtSkin uses. Every 240th actor draw.
static void CheckPaletteAgainstGpu(ID3D11DeviceContext* real, ID3D11Buffer* cb, const uint8_t* shadow, UINT indexCount)
{
    static int on = -1;
    static uint32_t n = 0, checks = 0;
    if (on < 0) {
        char path[MAX_PATH]{};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string ini(path);
        ini = ini.substr(0, ini.rfind(char(92)) + 1) + "d3d11_proxy.ini";
        on = GetPrivateProfileIntA("RT", "SkinCbCheck", 0, ini.c_str());
    }
    if (on <= 0 || (n++ % 240) != 0 || checks >= 40) return;
    ID3D11Device* dev = nullptr;
    real->GetDevice(&dev);
    D3D11_BUFFER_DESC d{};
    cb->GetDesc(&d);
    D3D11_BUFFER_DESC sd{};
    sd.ByteWidth = d.ByteWidth; sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Buffer* st = nullptr;
    if (dev && SUCCEEDED(dev->CreateBuffer(&sd, nullptr, &st)) && st) {
        real->CopyResource(st, cb);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(real->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
            const uint8_t* g = static_cast<const uint8_t*>(m.pData);
            uint32_t rows = 0, first[6] = {}, nf = 0;
            for (uint32_t r = 0; r < 240; ++r)
                if (std::memcmp(g + r * 16, shadow + r * 16, 16) != 0) { ++rows; if (nf < 6) first[nf++] = r; }
            ++checks;
            Log("[RT] palette check %u (%u indices, usage %d, cpu %u): %u of 240 rows differ between the GPU and the shadow; first rows %u %u %u %u %u %u",
                checks, indexCount, int(d.Usage), d.CPUAccessFlags, rows, first[0], first[1], first[2], first[3], first[4], first[5]);
            real->Unmap(st, 0);
        }
        st->Release();
    }
    if (dev) dev->Release();
}

// [RT] SkinVbCheck=1: the actor draw's vertex and blend streams read back at the draw (a stall), from its first
// vertex, up to 256 KB each, so RtSkin's check can compare a floating vertex with what the game's draw saw.
static void ReadStreamsAtDraw(ID3D11DeviceContext* real, raw::bridge::RtDraw& d)
{
    static int on = -1;
    if (on < 0) {
        char path[MAX_PATH]{};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string ini(path);
        ini = ini.substr(0, ini.rfind(char(92)) + 1) + "d3d11_proxy.ini";
        on = GetPrivateProfileIntA("RT", "SkinVbCheck", 0, ini.c_str());
    }
    if (on <= 0) return;
    ID3D11Device* dev = nullptr;
    real->GetDevice(&dev);
    auto read = [&](ID3D11Buffer* b, uint64_t start, uint8_t** out, uint32_t* bytes) {
        D3D11_BUFFER_DESC bd{};
        b->GetDesc(&bd);
        if (start >= bd.ByteWidth) return;
        const uint32_t n = uint32_t(std::min<uint64_t>(bd.ByteWidth - start, 256u << 10));
        D3D11_BUFFER_DESC sd{};
        sd.ByteWidth = bd.ByteWidth; sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Buffer* st = nullptr;
        if (FAILED(dev->CreateBuffer(&sd, nullptr, &st)) || !st) return;
        real->CopyResource(st, b);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(real->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
            *out = new uint8_t[n];
            std::memcpy(*out, static_cast<const uint8_t*>(m.pData) + start, n);
            *bytes = n;
            real->Unmap(st, 0);
        }
        st->Release();
    };
    if (dev) {
        read(d.vb, uint64_t(d.vbOffset) + uint64_t(int64_t(d.baseVertex) * d.stride), &d.diagVb, &d.diagVbBytes);
        read(d.blendVb, uint64_t(d.blendVbOffset) + uint64_t(int64_t(d.baseVertex) * d.blendStride), &d.diagBv, &d.diagBvBytes);
        dev->Release();
    }
}

bool SceneSkin_OnDraw(ID3D11DeviceContext* real, const Layout* bound, UINT indexCount, UINT startIndex, INT baseVertex, bool alphaFollows)
{
    static uint32_t seen = 0, logged = 0;
    if (!bound || !bound->blendIdx.set) return false;
    const Layout& L = *bound;
    // A skinned draw with a world block is an actor's: RtActors=0 sends it the rigid way, as before.
    const bool actor = SceneLights_DrawWorld() != nullptr;
    if (actor && !g_actors) return false;
    const bool formatOk = L.blendW.set && L.blendIdx.format == DXGI_FORMAT_R8G8B8A8_UNORM && L.blendW.format == DXGI_FORMAT_R16G16B16A16_FLOAT &&
                          L.blendW.slot == L.blendIdx.slot &&
                          (L.pos.format == DXGI_FORMAT_R32G32B32A32_FLOAT || L.pos.format == DXGI_FORMAT_R32G32B32_FLOAT || L.pos.format == DXGI_FORMAT_R16G16B16A16_FLOAT);
    const bool opaque = SceneLights_Opaque();
    if (!opaque) {
        const uint64_t key = (uint64_t(g_mainDepth) << 63) | (uint64_t(g_afterCapture) << 62) | (uint64_t(g_tgtRtvs & 15) << 48) |
                             (uint64_t(g_tgtFormat & 0xFFFF) << 32) | uint64_t(g_tgtWidth);
        ++g_outside[key];
    }
    const bool open = opaque || (g_actors && g_mainDepth);
    bool alpha = false;
    if (g_skinOn && formatOk && open) {
        // Alpha-tested skinned draws (tree leaves) would be solid cards: left out until the rays test them.
        alpha = geom::IsDiscardPs(geom::BoundPs(real));
        alpha = alpha || BlendAlpha(real) != raw::bridge::RtAlphaNone;
        // Hair, fur and capes: their prepass tests the alpha, and the colour draw that follows (depth EQUAL, a shader
        // with no discard) would otherwise join as solid cards. Those cards were the floating skinned triangles that
        // stood outside the game's silhouette and shadowed the actors (docs/RAYTRACING.md).
        if (!alpha && alphaFollows) { alpha = true; ++g_skinAlphaFollowed; }
    }
    // Alpha-tested skinned draws (hair, fur, capes, the trees' leaves) join cut by the alpha of their colour texture
    // (t0), with the coordinates from the position's stream or their own: the rays test it as the game's prepass does.
    const bool uvOk = L.uv.set && (L.uv.format == DXGI_FORMAT_R16G16_FLOAT || L.uv.format == DXGI_FORMAT_R32G32_FLOAT ||
                                   L.uv.format == DXGI_FORMAT_R16G16B16A16_FLOAT || L.uv.format == DXGI_FORMAT_R32G32B32A32_FLOAT);
    const bool alphaJoins = alpha && g_skinAlphaOn && uvOk;
    if (alpha && g_skinAlphaOn && !uvOk) ++g_skinAlphaNoUv;
    if (g_skinOn) { if (!open) ++g_skinNotOpaque; else if (!formatOk) ++g_skinFormat; else if (alpha && !alphaJoins) ++g_skinAlpha; }
    // The actor sheet's cause (rt6-next-3): the floating draws bind no pixel shader and write no depth, a pass that
    // draws the actors' skinned meshes for something other than the picture (occlusion queries, by the look of it),
    // with the same view and palette. They make no surface, so they stay out of the scene.
    if (g_skinOn && open && indexCount >= 3 && formatOk && !alpha && !geom::BoundPs(real)) {
        if (SceneSkin_PrepassCheckOn()) SceneSkin_KeepPrepass(real, L, g_boneSlot, indexCount, startIndex, baseVertex);
        ++g_skinNoPs;
        return true;
    }
    if (g_skinOn && open && indexCount >= 3 && formatOk && (!alpha || alphaJoins)) {
        // A skinned draw of the opaque set: its streams, index range and bone palette (RtSkin.h).
        ID3D11Buffer* bb[1] = {};
        real->VSGetConstantBuffers(UINT(g_boneSlot), 1, bb);
        const uint8_t* bones = bb[0] && CBDirtyTracker::Get().GetShadowSize(bb[0]) >= 3840 ? CBDirtyTracker::Get().GetShadowData(bb[0]) : nullptr;
        if (bones && actor) CheckPaletteAgainstGpu(real, bb[0], bones, indexCount);
        if (bones && actor && SceneSkin_PrepassCheckOn()) SceneSkin_ComparePrepass(real, L, indexCount, startIndex, baseVertex, bones);
        if (bb[0]) bb[0]->Release();
        if (!bones) { ++g_skinNoBones; return true; }
        const bool half = L.pos.format == DXGI_FORMAT_R16G16B16A16_FLOAT;
        if ((g_skinFilter == 1 && half) || (g_skinFilter == 2 && !half) || (g_skinFilter == 3 && actor) || (g_skinFilter == 4 && !actor)) return true;
        raw::bridge::RtDraw d;
        d.drop = geom::DropFor(real);
        UINT stride = 0, offset = 0, bs = 0, bo = 0;
        d.vb = geom::VertexBuffer(real, L.pos.slot, &stride, &offset);
        d.blendVb = geom::VertexBuffer(real, L.blendIdx.slot, &bs, &bo);
        d.ib = geom::IndexBuffer(real, &d.ibFormat, &d.ibOffset);
        if (!d.vb || !d.ib || !d.blendVb) { raw::bridge::ReleaseRtDraw(d); return true; }
        d.skinned = true; d.actor = actor;
        if (alpha) {
            d.alphaSrv = geom::AlphaTexture(real);
            if (!d.alphaSrv) { raw::bridge::ReleaseRtDraw(d); ++g_skinAlphaNoUv; return true; }
            d.alphaKind = raw::bridge::RtAlphaDiscard; d.uvOffset = L.uv.offset; d.uvFormat = L.uv.format;
            if (L.uv.slot != L.pos.slot) {
                UINT us = 0, uo = 0;
                d.uvVb = geom::VertexBuffer(real, L.uv.slot, &us, &uo);
                if (!d.uvVb) { raw::bridge::ReleaseRtDraw(d); ++g_skinAlphaNoUv; return true; }
                d.uvStride = us; d.uvVbOffset = uo;
            }
            ++g_skinAlphaJoined;
        }
        d.stride = stride; d.vbOffset = offset; d.posOffset = L.pos.offset; d.posFormat = L.pos.format;
        d.blendStride = bs; d.blendVbOffset = bo; d.idxOffset = L.blendIdx.offset; d.wOffset = L.blendW.offset;
        d.indexCount = indexCount; d.startIndex = startIndex; d.baseVertex = baseVertex;
        d.bones = new float[960];
        std::memcpy(d.bones, bones, 3840);
        if (actor) {
            ReadStreamsAtDraw(real, d);
            d.hasView = SceneSkin_ReadView(real, d.viewProj);
            if (ID3D11PixelShader* ps = geom::BoundPs(real)) d.psHash = ShaderManager::Get().GetPSHash(ps);
            ID3D11BlendState* bs = nullptr;
            float bf[4];
            UINT sm = 0;
            real->OMGetBlendState(&bs, bf, &sm);
            if (bs) {
                D3D11_BLEND_DESC bd{};
                bs->GetDesc(&bd);
                d.blendInfo = bd.RenderTarget[0].RenderTargetWriteMask | (bd.RenderTarget[0].BlendEnable ? 256u : 0u);
                bs->Release();
            } else {
                d.blendInfo = 15u;
            }
            ID3D11DepthStencilState* ds = nullptr;
            UINT ref = 0;
            real->OMGetDepthStencilState(&ds, &ref);
            if (ds) {
                D3D11_DEPTH_STENCIL_DESC dd{};
                ds->GetDesc(&dd);
                if (dd.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL) d.blendInfo |= 512u;
                ds->Release();
            } else {
                d.blendInfo |= 512u;
            }
        }
        ++g_skinJoined;
        if (actor) {   // which vertex shaders skin the actors, with their streams (RtSkin diagnosis; [RT] DumpSkinVs)
            static std::unordered_map<uint64_t, uint32_t> vsSeen;
            ID3D11VertexShader* vs = nullptr;
            real->VSGetShader(&vs, nullptr, nullptr);
            const uint64_t h = vs ? ShaderManager::Get().GetVSHash(vs) : 0;
            if (vs) vs->Release();
            if (vsSeen.size() < 48 && vsSeen[h]++ == 0) {
                D3D11_BUFFER_DESC vd{};
                d.vb->GetDesc(&vd);
                Log("[RT] actor skinned draw: vs %016llx, %u indices from %u base %d, position format %d stride %u, blend stride %u (indices at %u, "
                    "weights at %u); vertex buffer %u bytes (%u vertices), usage %d, cpu access %u, offset %u",
                    (unsigned long long)h, indexCount, startIndex, baseVertex, int(L.pos.format), stride, bs, L.blendIdx.offset, L.blendW.offset,
                    vd.ByteWidth, stride ? vd.ByteWidth / stride : 0, int(vd.Usage), vd.CPUAccessFlags, offset);
            }
        }
        if (!opaque) { ++g_actorJoined; if (g_afterCapture) ++g_actorLate; }
        if (actor) ++g_actorWorld;
        geom::Frame().push_back(d);   // after the capture: carried to the next frame by SceneSkin_Drop
    }
    if (logged >= 12 || (++seen % 97) != 1) return true;
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
    return true;
}

void SceneSkin_Log()
{
    Log("[RT] skinned draws so far: %llu joined, %llu without a bone palette at vertex slot %d; left out: %llu outside the opaque set, "
            "%llu layout formats, %llu alpha-tested (%llu of them by their prepass), %llu with no pixel shader; alpha-tested joined %llu, without "
            "coordinates or texture %llu", (unsigned long long)g_skinJoined, (unsigned long long)g_skinNoBones, g_boneSlot,
            (unsigned long long)g_skinNotOpaque, (unsigned long long)g_skinFormat, (unsigned long long)g_skinAlpha, (unsigned long long)g_skinAlphaFollowed, (unsigned long long)g_skinNoPs, (unsigned long long)g_skinAlphaJoined, (unsigned long long)g_skinAlphaNoUv);
    {
        Log("[RT] actors (skinned, outside the opaque set, on the main depth target): %llu joined, %llu of them after the capture "
            "(next frame); %llu with a world block (the opaque set's actors); RtActors=%d", (unsigned long long)g_actorJoined,
            (unsigned long long)g_actorLate, (unsigned long long)g_actorWorld, int(g_actors));
        std::vector<std::pair<uint64_t, uint64_t>> v(g_outside.begin(), g_outside.end());
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
        for (size_t k = 0; k < v.size() && k < 8; ++k)
            Log("[RT]   outside the opaque set: %llu draws; main depth %d, after capture %d, %u colour targets, first format %u width %u",
                (unsigned long long)v[k].second, int(v[k].first >> 63), int((v[k].first >> 62) & 1), unsigned((v[k].first >> 48) & 15),
                unsigned((v[k].first >> 32) & 0xFFFF), unsigned(v[k].first & 0xFFFFFFFF));
    }
    {
        static const char* calls[4] = {"indexed", "indexed instanced", "instanced", "plain"};
        for (int c = 0; c < 4; ++c)
            Log("[RT]   opaque-set %s draws: rigid layout %llu (world block %llu), skinned layout %llu (world block %llu)", calls[c],
                (unsigned long long)(g_census[c][0][0] + g_census[c][0][1]), (unsigned long long)g_census[c][0][1],
                (unsigned long long)(g_census[c][1][0] + g_census[c][1][1]), (unsigned long long)g_census[c][1][1]);
    }
}

void SceneSkin_Took(std::vector<raw::bridge::RtDraw>& out)
{
    for (auto& d : g_carry) out.push_back(d);
    g_carry.clear();
    g_took = true;
}

void SceneSkin_Drop(std::vector<raw::bridge::RtDraw>& frame)
{
    for (auto& d : g_carry) raw::bridge::ReleaseRtDraw(d);   // a carry no frame took
    g_carry.clear();
    for (auto& d : frame) {
        if (g_took && d.skinned) g_carry.push_back(d);   // drawn after this frame's capture: the next frame's
        else raw::bridge::ReleaseRtDraw(d);
    }
    g_took = false;
}

} // namespace SB::Proxy
