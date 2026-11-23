// SceneSkinPrepass: [RT] SkinPrepassCheck, a diagnosis of the floating skinned corners (docs/RAYTRACING.md).
// Original work, MIT.
#include "SceneGeometry.h"
#include "SceneGeometryDetail.h"
#include "ProxyLog.h"
#include "ShaderManager.h"
#include "CBDirtyTracker.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace SB::Proxy
{
using geom::Layout;
// [RT] SkinPrepassCheck=1 (the floating skinned corners, docs/RAYTRACING.md): the actors' colour draws test EQUAL
// against the depth their prepass wrote (a capture: depth write 0, function 3), so a colour-draw triangle that
// lands anywhere the prepass did not put it is never seen in the game. Each prepass draw (no pixel shader) is kept
// by its streams; the colour draw of the same streams is compared with it: index range, the bone palette row by
// row, and the vertex shader.
namespace
{
struct Prepass { UINT start = 0, count = 0; uint64_t vs = 0; float bones[960]; };
int g_prepassCheck = -1;
std::unordered_map<uint64_t, Prepass> g_prepass;
// Up to eight palettes per set of streams: one mesh's streams are shared by several actors, so the colour draw is
// compared with the nearest of them (by the bones' rotation).
struct PrepassSet { uint32_t next = 0, n = 0; float bones[8][960]; };
std::unordered_map<uint64_t, PrepassSet> g_sets;
uint64_t g_bestDraws = 0, g_bestExact = 0, g_bestBonesUsed = 0, g_bestBonesRot = 0, g_bestBonesMoved = 0;
uint32_t g_bestDetail = 0;
uint64_t g_ppColour = 0, g_ppMatched = 0, g_ppSameRange = 0, g_ppColourInside = 0, g_ppPrepassInside = 0, g_ppOther = 0;
uint64_t g_ppPaletteEqual = 0, g_ppRowsDiffer = 0, g_ppVsEqual = 0;
uint32_t g_ppDetail = 0, g_ppBoneDetail = 0;
uint64_t g_ppBonesUsed = 0, g_ppBonesRotated = 0, g_ppBonesMoved = 0;
uint64_t StreamKey(ID3D11DeviceContext* real, const Layout& L, INT baseVertex)
{
    UINT s0 = 0, o0 = 0, s1 = 0, o1 = 0, io = 0;
    DXGI_FORMAT f = DXGI_FORMAT_UNKNOWN;
    ID3D11Buffer* vb = geom::VertexBuffer(real, L.pos.slot, &s0, &o0);
    ID3D11Buffer* bv = geom::VertexBuffer(real, L.blendIdx.slot, &s1, &o1);
    ID3D11Buffer* ib = geom::IndexBuffer(real, &f, &io);
    const uint64_t k = (reinterpret_cast<uint64_t>(vb) * 1099511628211ull) ^ (reinterpret_cast<uint64_t>(bv) * 6364136223846793005ull) ^
                       (reinterpret_cast<uint64_t>(ib) * 0x9E3779B97F4A7C15ull) ^ (uint64_t(uint32_t(baseVertex)) << 20) ^
                       (uint64_t(o0) << 40) ^ o1 ^ (uint64_t(io) << 8);
    raw::bridge::RtDraw tmp;   // the references go back the way they were taken (the layer's or COM)
    tmp.drop = geom::DropFor(real); tmp.vb = vb; tmp.blendVb = bv; tmp.ib = ib;
    raw::bridge::ReleaseRtDraw(tmp);
    return k;
}
uint64_t BoundVsHash(ID3D11DeviceContext* real)
{
    ID3D11VertexShader* vs = nullptr;
    real->VSGetShader(&vs, nullptr, nullptr);
    const uint64_t h = vs ? ShaderManager::Get().GetVSHash(vs) : 0;
    if (vs) vs->Release();
    return h;
}
// The nearest kept prepass palette to this colour draw's, bone by bone: rotation, and translation against the common
// offset. A best match with no bone apart says the prepass skinned this mesh as the colour draw does.
void BestPrepass(uint64_t key, const float* b)
{
    auto it = g_sets.find(key);
    if (it == g_sets.end()) return;
    const PrepassSet& ps = it->second;
    float bestScore = 1e30f; uint32_t bestUsed = 0, bestRot = 0, bestMoved = 0; float bestMove = 0.0f;
    for (uint32_t c = 0; c < ps.n; ++c) {
        const float* pb = ps.bones[c];
        uint32_t used = 0, rot = 0, moved = 0; float score = 0.0f, off[3] = {}, worstMove = 0.0f;
        for (uint32_t k = 0; k < 80; ++k) {
            const float* x = b + k * 12; const float* y = pb + k * 12;
            float mag = 0.0f, r = 0.0f;
            for (int i = 0; i < 12; ++i) mag = std::max(mag, std::fabs(x[i]) + std::fabs(y[i]));
            if (mag == 0.0f) continue;
            for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col) r = std::max(r, std::fabs(x[row * 4 + col] - y[row * 4 + col]));
            const float t[3] = {x[3] - y[3], x[7] - y[7], x[11] - y[11]};
            if (!used++) std::memcpy(off, t, sizeof off);
            const float mv = std::max({std::fabs(t[0] - off[0]), std::fabs(t[1] - off[1]), std::fabs(t[2] - off[2])});
            score += r; if (r > 1e-3f) ++rot; if (mv > 1.0f) { ++moved; worstMove = std::max(worstMove, mv); }
        }
        if (score < bestScore) { bestScore = score; bestUsed = used; bestRot = rot; bestMoved = moved; bestMove = worstMove; }
    }
    ++g_bestDraws; g_bestBonesUsed += bestUsed; g_bestBonesRot += bestRot; g_bestBonesMoved += bestMoved;
    if (!bestRot && !bestMoved) ++g_bestExact;
    else if (g_bestDetail < 24) {
        ++g_bestDetail;
        Log("[RT] prepass nearest: %u of %u kept palettes; %u bones used, %u rotated, %u moved (largest %.1f)", ps.n, ps.n, bestUsed, bestRot,
            bestMoved, bestMove);
    }
    if ((g_bestDraws % 3000) == 0)
        Log("[RT] prepass nearest so far: %llu colour draws, %llu with a prepass palette that matches every bone; bones used %llu, rotated %llu, "
            "moved %llu", (unsigned long long)g_bestDraws, (unsigned long long)g_bestExact, (unsigned long long)g_bestBonesUsed,
            (unsigned long long)g_bestBonesRot, (unsigned long long)g_bestBonesMoved);
}
} // namespace

bool SceneSkin_PrepassCheckOn()
{
    if (g_prepassCheck < 0) {
        char path[MAX_PATH]{};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string ini(path);
        ini = ini.substr(0, ini.rfind(char(92)) + 1) + "d3d11_proxy.ini";
        g_prepassCheck = GetPrivateProfileIntA("RT", "SkinPrepassCheck", 0, ini.c_str());
    }
    return g_prepassCheck > 0;
}
void SceneSkin_KeepPrepass(ID3D11DeviceContext* real, const Layout& L, int boneSlot, UINT indexCount, UINT startIndex, INT baseVertex)
{
    const uint64_t key = StreamKey(real, L, baseVertex);
    ID3D11Buffer* bb[1] = {};
    real->VSGetConstantBuffers(UINT(boneSlot), 1, bb);
    const uint8_t* bones = bb[0] && CBDirtyTracker::Get().GetShadowSize(bb[0]) >= 3840 ? CBDirtyTracker::Get().GetShadowData(bb[0]) : nullptr;
    if (bb[0]) bb[0]->Release();
    if (!bones) return;
    if (g_sets.size() > 1024) g_sets.clear();
    PrepassSet& ps = g_sets[key];
    std::memcpy(ps.bones[ps.next], bones, 3840);
    ps.next = (ps.next + 1) & 7; ps.n = std::min(ps.n + 1, 8u);
    if (g_prepass.size() > 4096) g_prepass.clear();
    Prepass& p = g_prepass[key];
    p.start = startIndex; p.count = indexCount; p.vs = BoundVsHash(real);
    std::memcpy(p.bones, bones, 3840);
}
void SceneSkin_ComparePrepass(ID3D11DeviceContext* real, const Layout& L, UINT indexCount, UINT startIndex, INT baseVertex, const uint8_t* bones)
{
    ++g_ppColour;
    BestPrepass(StreamKey(real, L, baseVertex), reinterpret_cast<const float*>(bones));
    auto it = g_prepass.find(StreamKey(real, L, baseVertex));
    if (it != g_prepass.end()) {
        const Prepass& p = it->second;
        ++g_ppMatched;
        const UINT ce = startIndex + indexCount, pe = p.start + p.count;
        const bool same = p.start == startIndex && p.count == indexCount;
        if (same) ++g_ppSameRange;
        else if (startIndex >= p.start && ce <= pe) ++g_ppColourInside;
        else if (p.start >= startIndex && pe <= ce) ++g_ppPrepassInside;
        else ++g_ppOther;
        uint32_t rows = 0, first = 0;
        float worst = 0.0f;
        const float* b = reinterpret_cast<const float*>(bones);
        for (uint32_t r = 0; r < 240; ++r) {
            float d = 0.0f;
            for (int c = 0; c < 4; ++c) d = std::max(d, std::fabs(b[r * 4 + c] - p.bones[r * 4 + c]));
            if (d > 1e-3f) { if (!rows) first = r; ++rows; worst = std::max(worst, d); }
        }
        if (!rows) ++g_ppPaletteEqual; else g_ppRowsDiffer += rows;
        // Bone by bone (three rows each): the rotation part, and the translation's difference against the first used
        // bone's (a camera offset between the two conventions moves every bone alike).
        uint32_t used = 0, rotated = 0, moved = 0, firstOdd = 999;
        float off[3] = {}, worstRot = 0.0f, worstMove = 0.0f;
        for (uint32_t k = 0; k < 80; ++k) {
            const float* cb = b + k * 12; const float* pb = p.bones + k * 12;
            float mag = 0.0f, rot = 0.0f;
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c) mag = std::max(mag, std::fabs(cb[r * 4 + c]) + std::fabs(pb[r * 4 + c]));
            if (mag == 0.0f) continue;
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) rot = std::max(rot, std::fabs(cb[r * 4 + c] - pb[r * 4 + c]));
            const float t[3] = {cb[3] - pb[3], cb[7] - pb[7], cb[11] - pb[11]};
            if (!used++) std::memcpy(off, t, sizeof off);
            const float mv = std::max({std::fabs(t[0] - off[0]), std::fabs(t[1] - off[1]), std::fabs(t[2] - off[2])});
            if (rot > 1e-3f) { ++rotated; worstRot = std::max(worstRot, rot); }
            if (mv > 1.0f) { ++moved; worstMove = std::max(worstMove, mv); }
            if ((rot > 1e-3f || mv > 1.0f) && firstOdd == 999) firstOdd = k;
        }
        g_ppBonesUsed += used; g_ppBonesRotated += rotated; g_ppBonesMoved += moved;
        if ((rotated || moved) && g_ppBoneDetail < 24) {
            ++g_ppBoneDetail;
            const float* cb = b + firstOdd * 12; const float* pb = p.bones + firstOdd * 12;
            Log("[RT] prepass bones: %u used, %u rotated (largest %.4f), %u moved off the common offset %.1f %.1f %.1f (largest %.1f); bone %u "
                "colour row0 %.3f %.3f %.3f %.1f, prepass row0 %.3f %.3f %.3f %.1f", used, rotated, worstRot, moved, off[0], off[1], off[2],
                worstMove, firstOdd, firstOdd < 80 ? cb[0] : 0.f, firstOdd < 80 ? cb[1] : 0.f, firstOdd < 80 ? cb[2] : 0.f, firstOdd < 80 ? cb[3] : 0.f,
                firstOdd < 80 ? pb[0] : 0.f, firstOdd < 80 ? pb[1] : 0.f, firstOdd < 80 ? pb[2] : 0.f, firstOdd < 80 ? pb[3] : 0.f);
        }
        if ((g_ppMatched % 3000) == 0)
            Log("[RT] prepass bones so far: %llu used, %llu rotated, %llu moved off their draw's common offset", (unsigned long long)g_ppBonesUsed,
                (unsigned long long)g_ppBonesRotated, (unsigned long long)g_ppBonesMoved);
        const uint64_t vs = BoundVsHash(real);
        if (vs == p.vs) ++g_ppVsEqual;
        if ((rows || !same) && g_ppDetail < 16) {
            ++g_ppDetail;
            Log("[RT] prepass check: colour draw %u indices from %u, base %d, vs %016llx; its prepass %u from %u, vs %016llx; %u of 240 palette rows "
                "differ (first %u, largest difference %.3f)", indexCount, startIndex, baseVertex, (unsigned long long)vs, p.count, p.start,
                (unsigned long long)p.vs, rows, first, worst);
        }
    }
    if ((g_ppColour % 200000) == 0)
        Log("[RT] prepass check: %llu actor colour draws, %llu with a prepass of the same streams; index range same %llu, colour inside the "
            "prepass %llu, prepass inside the colour %llu, other %llu; palette equal %llu (rows differing in the rest: %llu); vertex shader equal %llu",
            (unsigned long long)g_ppColour, (unsigned long long)g_ppMatched, (unsigned long long)g_ppSameRange, (unsigned long long)g_ppColourInside,
            (unsigned long long)g_ppPrepassInside, (unsigned long long)g_ppOther, (unsigned long long)g_ppPaletteEqual, (unsigned long long)g_ppRowsDiffer,
            (unsigned long long)g_ppVsEqual);
}

} // namespace SB::Proxy
