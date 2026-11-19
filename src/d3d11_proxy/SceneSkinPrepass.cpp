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
uint64_t g_ppColour = 0, g_ppMatched = 0, g_ppSameRange = 0, g_ppColourInside = 0, g_ppPrepassInside = 0, g_ppOther = 0;
uint64_t g_ppPaletteEqual = 0, g_ppRowsDiffer = 0, g_ppVsEqual = 0;
uint32_t g_ppDetail = 0;
uint64_t StreamKey(ID3D11DeviceContext* real, const Layout& L, INT baseVertex)
{
    UINT s0 = 0, o0 = 0, s1 = 0, o1 = 0;
    ID3D11Buffer* vb = geom::VertexBuffer(real, L.pos.slot, &s0, &o0);
    ID3D11Buffer* bv = geom::VertexBuffer(real, L.blendIdx.slot, &s1, &o1);
    const uint64_t k = (reinterpret_cast<uint64_t>(vb) * 1099511628211ull) ^ (reinterpret_cast<uint64_t>(bv) * 6364136223846793005ull) ^
                       (uint64_t(uint32_t(baseVertex)) << 20) ^ (uint64_t(o0) << 40) ^ o1;
    if (vb) vb->Release();
    if (bv) bv->Release();
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
    ID3D11Buffer* bb[1] = {};
    real->VSGetConstantBuffers(UINT(boneSlot), 1, bb);
    const uint8_t* bones = bb[0] && CBDirtyTracker::Get().GetShadowSize(bb[0]) >= 3840 ? CBDirtyTracker::Get().GetShadowData(bb[0]) : nullptr;
    if (bb[0]) bb[0]->Release();
    if (!bones) return;
    if (g_prepass.size() > 4096) g_prepass.clear();
    Prepass& p = g_prepass[StreamKey(real, L, baseVertex)];
    p.start = startIndex; p.count = indexCount; p.vs = BoundVsHash(real);
    std::memcpy(p.bones, bones, 3840);
}
void SceneSkin_ComparePrepass(ID3D11DeviceContext* real, const Layout& L, UINT indexCount, UINT startIndex, INT baseVertex, const uint8_t* bones)
{
    ++g_ppColour;
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
        const uint64_t vs = BoundVsHash(real);
        if (vs == p.vs) ++g_ppVsEqual;
        if ((rows || !same) && g_ppDetail < 16) {
            ++g_ppDetail;
            Log("[RT] prepass check: colour draw %u indices from %u, base %d, vs %016llx; its prepass %u from %u, vs %016llx; %u of 240 palette rows "
                "differ (first %u, largest difference %.3f)", indexCount, startIndex, baseVertex, (unsigned long long)vs, p.count, p.start,
                (unsigned long long)p.vs, rows, first, worst);
        }
    }
    if ((g_ppColour % 2000) == 0)
        Log("[RT] prepass check: %llu actor colour draws, %llu with a prepass of the same streams; index range same %llu, colour inside the "
            "prepass %llu, prepass inside the colour %llu, other %llu; palette equal %llu (rows differing in the rest: %llu); vertex shader equal %llu",
            (unsigned long long)g_ppColour, (unsigned long long)g_ppMatched, (unsigned long long)g_ppSameRange, (unsigned long long)g_ppColourInside,
            (unsigned long long)g_ppPrepassInside, (unsigned long long)g_ppOther, (unsigned long long)g_ppPaletteEqual, (unsigned long long)g_ppRowsDiffer,
            (unsigned long long)g_ppVsEqual);
}

} // namespace SB::Proxy
