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
int g_boneSlot = 10;
uint64_t g_skinJoined = 0, g_skinNoBones = 0, g_skinNotOpaque = 0, g_skinFormat = 0, g_skinAlpha = 0;
// Actors (people, creatures, the player in third person) draw skinned outside the opaque set's targets.
// Those on the main depth target join; drawn after the frame's capture, they wait for the next frame.
int g_skinFilter = 0;   // diagnosis (RtSkinFilter): 1 float positions only, 2 half positions only, 3 trees only, 4 actors only
bool g_actors = true, g_mainDepth = false, g_afterCapture = false, g_took = false;
UINT g_tgtRtvs = 0, g_tgtWidth = 0;
DXGI_FORMAT g_tgtFormat = DXGI_FORMAT_UNKNOWN;
uint64_t g_actorJoined = 0, g_actorLate = 0, g_actorWorld = 0;
std::unordered_map<uint64_t, uint64_t> g_outside;   // skinned draws outside the opaque set, by the targets bound
std::vector<raw::bridge::RtDraw> g_carry;           // joined after the capture: the next frame's
uint64_t g_census[4][2][2] = {};   // opaque-set draws by call (indexed, indexed instanced, instanced, plain), skinned layout, world block
} // namespace

void SceneGeometry_SetActors(bool on) { g_actors = on; }
void SceneGeometry_SetSkinFilter(int f) { g_skinFilter = f; }
void SceneGeometry_Census(ID3D11DeviceContext* real, int call)
{
    if (!SceneLights_Opaque() || call < 0 || call > 3) return;
    ID3D11InputLayout* il = nullptr;
    real->IAGetInputLayout(&il);
    Layout L;
    const bool skinned = geom::FindLayout(il, L) && L.blendIdx.set;
    if (il) il->Release();
    ++g_census[call][skinned][SceneLights_DrawWorld() != nullptr];
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

bool SceneSkin_OnDraw(ID3D11DeviceContext* real, UINT indexCount, UINT startIndex, INT baseVertex)
{
    static uint32_t seen = 0, logged = 0;
    ID3D11InputLayout* il = nullptr;
    real->IAGetInputLayout(&il);
    Layout L;
    const bool found = geom::FindLayout(il, L);
    if (il) il->Release();
    if (!found || !L.blendIdx.set) return false;
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
        ID3D11PixelShader* ps = nullptr;
        real->PSGetShader(&ps, nullptr, nullptr);
        alpha = geom::IsDiscardPs(ps);
        if (ps) ps->Release();
        alpha = alpha || BlendAlpha(real) != raw::bridge::RtAlphaNone;
    }
    if (g_skinOn) { if (!open) ++g_skinNotOpaque; else if (!formatOk) ++g_skinFormat; else if (alpha) ++g_skinAlpha; }
    if (g_skinOn && open && indexCount >= 3 && formatOk && !alpha) {
        // A skinned draw of the opaque set: its streams, index range and bone palette (RtSkin.h).
        ID3D11Buffer* bb[1] = {};
        real->VSGetConstantBuffers(UINT(g_boneSlot), 1, bb);
        const uint8_t* bones = bb[0] && CBDirtyTracker::Get().GetShadowSize(bb[0]) >= 3840 ? CBDirtyTracker::Get().GetShadowData(bb[0]) : nullptr;
        if (bones && actor) CheckPaletteAgainstGpu(real, bb[0], bones, indexCount);
        if (bb[0]) bb[0]->Release();
        if (!bones) { ++g_skinNoBones; return true; }
        const bool half = L.pos.format == DXGI_FORMAT_R16G16B16A16_FLOAT;
        if ((g_skinFilter == 1 && half) || (g_skinFilter == 2 && !half) || (g_skinFilter == 3 && actor) || (g_skinFilter == 4 && !actor)) return true;
        raw::bridge::RtDraw d;
        UINT stride = 0, offset = 0, bs = 0, bo = 0;
        real->IAGetVertexBuffers(L.pos.slot, 1, &d.vb, &stride, &offset);
        real->IAGetVertexBuffers(L.blendIdx.slot, 1, &d.blendVb, &bs, &bo);
        real->IAGetIndexBuffer(&d.ib, &d.ibFormat, &d.ibOffset);
        if (!d.vb || !d.ib || !d.blendVb) { raw::bridge::ReleaseRtDraw(d); return true; }
        d.skinned = true; d.actor = actor;
        d.stride = stride; d.vbOffset = offset; d.posOffset = L.pos.offset; d.posFormat = L.pos.format;
        d.blendStride = bs; d.blendVbOffset = bo; d.idxOffset = L.blendIdx.offset; d.wOffset = L.blendW.offset;
        d.indexCount = indexCount; d.startIndex = startIndex; d.baseVertex = baseVertex;
        d.bones = new float[960];
        std::memcpy(d.bones, bones, 3840);
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
            "%llu layout formats, %llu alpha-tested", (unsigned long long)g_skinJoined, (unsigned long long)g_skinNoBones, g_boneSlot,
            (unsigned long long)g_skinNotOpaque, (unsigned long long)g_skinFormat, (unsigned long long)g_skinAlpha);
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
