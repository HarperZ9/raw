// SceneLights: see the header. Original work, MIT.
#include "SceneLights.h"
#include "CBDirtyTracker.h"
#include "ProxyLog.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace SB::Proxy
{
namespace
{
using raw::bridge::SceneLight;
using raw::bridge::SceneLighting;

constexpr uint32_t kLitPs2 = 480, kLitVs2 = 240, kMaskPs2 = 400, kFrameCb = 720;

struct Accum
{
    double pos[3] = {};
    float radius = 0, colour[3] = {};
    int shadowChannel = -1;
    uint32_t n = 0;
};

struct State
{
    SceneLighting frame;
    std::vector<Accum> lights;
    double sunDir[3] = {}, sunCol[3] = {}, amb[3][4] = {}, knee = 0;
    uint32_t sunDraws = 0, ambDraws = 0, kneeDraws = 0;
    std::vector<float> specScales;
    ID3D11Texture2D* mask = nullptr;     // held for the frame
    ID3D11Texture2D* cascades = nullptr; // held for the frame
    bool merged = false;
    bool opaque = false;                 // the game's opaque set is bound (the main camera's draws)
};
State& S() { static State s; return s; }

const float* Shadow(ID3D11Buffer* b, uint32_t want)
{
    if (!b) return nullptr;
    auto& t = CBDirtyTracker::Get();
    if (t.GetShadowSize(b) < want) return nullptr;
    return reinterpret_cast<const float*>(t.GetShadowData(b));
}

// out = rot * v (+ move), rot and move from three rows of a world matrix.
void Apply(const float* w, const float* v, bool point, double* out)
{
    for (int r = 0; r < 3; ++r)
        out[r] = double(w[r * 4]) * v[0] + double(w[r * 4 + 1]) * v[1] + double(w[r * 4 + 2]) * v[2] + (point ? w[r * 4 + 3] : 0.0);
}

void AddLight(State& s, const double* p, float radius, const float* col, int channel)
{
    if (radius <= 0 || (col[0] <= 0 && col[1] <= 0 && col[2] <= 0)) return;
    ++s.frame.lightSightings;
    for (Accum& a : s.lights) {
        if (std::fabs(a.radius - radius) > 0.5f || std::fabs(a.colour[0] - col[0]) > 2e-3f ||
            std::fabs(a.colour[1] - col[1]) > 2e-3f || std::fabs(a.colour[2] - col[2]) > 2e-3f) continue;
        const double dx = a.pos[0] / a.n - p[0], dy = a.pos[1] / a.n - p[1], dz = a.pos[2] / a.n - p[2];
        if (dx * dx + dy * dy + dz * dz > 64.0 * 64.0) continue;   // one (radius, colour) can be several lamps
        for (int k = 0; k < 3; ++k) a.pos[k] += p[k];
        ++a.n;
        if (channel >= 0) a.shadowChannel = channel;
        return;
    }
    if (s.lights.size() >= 4 * raw::bridge::kMaxSceneLights) { ++s.frame.droppedLights; return; }   // copies are dropped at the merge
    Accum a;
    for (int k = 0; k < 3; ++k) { a.pos[k] = p[k]; a.colour[k] = col[k]; }
    a.radius = radius;
    a.shadowChannel = channel;
    a.n = 1;
    s.lights.push_back(a);
}

void LitDraw(State& s, const float* g, const float* w, const float* ps0, const float* ps1)
{
    const double sx = std::sqrt(double(w[0]) * w[0] + double(w[4]) * w[4] + double(w[8]) * w[8]);
    if (sx <= 1e-6) return;
    ++s.frame.litDraws;
    double d[3];
    if (std::fabs(g[0]) + std::fabs(g[1]) + std::fabs(g[2]) > 0) {
        Apply(w, g, false, d);
        const double len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len > 0) {
            for (int k = 0; k < 3; ++k) { s.sunDir[k] += d[k] / len; s.sunCol[k] += g[4 + k]; }
            ++s.sunDraws;
        }
    }
    // Directional ambient: a_world = rot * a_model / scale (the model normal is normalised).
    for (int c = 0; c < 3; ++c) {
        Apply(w, g + (11 + c) * 4, false, d);
        for (int k = 0; k < 3; ++k) s.amb[c][k] += d[k] / sx;
        s.amb[c][3] += g[(11 + c) * 4 + 3];
    }
    ++s.ambDraws;
    if (ps0) { s.knee += ps0[3]; ++s.kneeDraws; }
    if (ps1) s.specScales.push_back((ps1[16] + ps1[17] + ps1[18]) / 3.0f * g[3 * 4 + 1]);
    const int count = int(std::min(g[29 * 4], 7.0f)), shadowed = int(std::min(g[29 * 4 + 1], 4.0f));
    for (int i = 0; i < count; ++i) {
        const float* l = g + (15 + i) * 4;
        Apply(w, l, true, d);
        const int channel = i < shadowed ? int(g[2 * 4 + i]) : -1;
        AddLight(s, d, float(l[3] * sx), g + (22 + i) * 4, channel);
    }
}

void MaskDraw(State& s, ID3D11DeviceContext* real, const float* g, const float* ps0)
{
    SceneLighting& f = s.frame;
    std::memcpy(f.cascade[0], g + 16 * 4, sizeof(float) * 12);
    std::memcpy(f.cascade[1], g + 19 * 4, sizeof(float) * 12);
    f.cascadeBias[0] = g[2 * 4 + 1];
    f.cascadeBias[1] = g[2 * 4 + 2];
    f.splitDepth = ps0[2 * 4];
    f.cascades = uint32_t(std::lround(ps0[2 * 4 + 3]));
    if (s.mask) { s.mask->Release(); s.mask = nullptr; }
    if (s.cascades) { s.cascades->Release(); s.cascades = nullptr; }
    ID3D11RenderTargetView* rtv = nullptr;
    real->OMGetRenderTargets(1, &rtv, nullptr);
    if (rtv) {
        ID3D11Resource* r = nullptr;
        rtv->GetResource(&r);
        rtv->Release();
        if (r && FAILED(r->QueryInterface(IID_PPV_ARGS(&s.mask)))) s.mask = nullptr;
        if (r) r->Release();
    }
    ID3D11ShaderResourceView* srvs[16] = {};
    real->PSGetShaderResources(0, 16, srvs);
    for (ID3D11ShaderResourceView* v : srvs) {
        if (!v) continue;
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
        v->GetDesc(&vd);
        ID3D11Resource* r = nullptr;
        v->GetResource(&r);
        ID3D11Texture2D* t = nullptr;
        if (!s.cascades && r && vd.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2DARRAY && SUCCEEDED(r->QueryInterface(IID_PPV_ARGS(&t)))) {
            D3D11_TEXTURE2D_DESC td{};
            t->GetDesc(&td);
            if (td.Format == DXGI_FORMAT_R16_TYPELESS && td.ArraySize >= 2 && td.Width >= 1024 && td.Width == td.Height) s.cascades = t;
            else t->Release();
        }
        if (r) r->Release();
        v->Release();
    }
    f.hasCascades = s.cascades && f.cascades >= 1;
}

void Camera(SceneLighting& f, const float* m)
{
    std::memcpy(f.worldToView, m + 0 * 16, 64);
    std::memcpy(f.viewToWorld, m + 7 * 16, 64);
    std::memcpy(f.clipToWorld, m + 8 * 16, 64);
    std::memcpy(f.worldToClip, m + 2 * 16, 64);
    f.resolutionScale[0] = m[44 * 4] > 0 ? m[44 * 4] : 1.0f;
    f.resolutionScale[1] = m[44 * 4 + 1] > 0 ? m[44 * 4 + 1] : 1.0f;
    f.hasCamera = true;
}

// A light seen by a few draws whose colour matches a light seen by many lies where the world
// matrix of those draws does not explain their space (skinned and multi-part meshes): a copy of
// the real light at a wrong position. lights.py found the same split on captured frames.
bool Phantom(const State& s, const Accum& a)
{
    if (a.n >= 10) return false;
    for (const Accum& b : s.lights)
        if (&b != &a && b.n >= 8 * a.n && std::fabs(b.colour[0] - a.colour[0]) < 2e-3f &&
            std::fabs(b.colour[1] - a.colour[1]) < 2e-3f && std::fabs(b.colour[2] - a.colour[2]) < 2e-3f) return true;
    return false;
}

void Merge(State& s)
{
    SceneLighting& f = s.frame;
    f.lightCount = 0;
    f.phantomLights = 0;
    for (const Accum& a : s.lights) {
        if (Phantom(s, a)) { ++f.phantomLights; continue; }
        if (f.lightCount >= raw::bridge::kMaxSceneLights) { ++f.droppedLights; continue; }
        SceneLight& l = f.lights[f.lightCount++];
        for (int k = 0; k < 3; ++k) { l.position[k] = float(a.pos[k] / a.n); l.colour[k] = a.colour[k]; }
        l.radius = a.radius;
        l.shadowChannel = a.shadowChannel;
        l.draws = a.n;
    }
    f.hasSun = s.sunDraws > 0;
    if (f.hasSun) {
        double len = 0;
        for (int k = 0; k < 3; ++k) len += s.sunDir[k] * s.sunDir[k];
        len = std::sqrt(len);
        for (int k = 0; k < 3; ++k) { f.sunDirection[k] = float(s.sunDir[k] / len); f.sunColour[k] = float(s.sunCol[k] / s.sunDraws); }
    }
    if (s.ambDraws)
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 4; ++k) f.ambient[c][k] = float(s.amb[c][k] / s.ambDraws);
    if (s.kneeDraws) f.knee = float(s.knee / s.kneeDraws);
    if (!s.specScales.empty()) {
        auto mid = s.specScales.begin() + s.specScales.size() / 2;
        std::nth_element(s.specScales.begin(), mid, s.specScales.end());
        f.specularScale = *mid;
    }
    f.valid = f.hasCamera && f.litDraws > 0;
    s.merged = true;
}
} // namespace

void SceneLights_BeforeDraw(ID3D11DeviceContext* real)
{
    // The bound buffers come from the context itself: the game binds some through paths the
    // wrapped Set calls do not see, as the frame ledger found.
    State& s = S();
    ID3D11Buffer* ps[3] = {};
    real->PSGetConstantBuffers(0, 3, ps);
    struct Release { ID3D11Buffer** b; int n; ~Release() { for (int k = 0; k < n; ++k) if (b[k]) b[k]->Release(); } } r1{ps, 3};
    const float* g = Shadow(ps[2], 0);
    if (!g) return;
    const uint32_t size = CBDirtyTracker::Get().GetShadowSize(ps[2]);
    if (size == kLitPs2 && s.opaque) {   // reflection-cube faces draw lit too, from their own camera
        ID3D11Buffer* vs[1] = {};
        real->VSGetConstantBuffers(2, 1, vs);
        Release r2{vs, 1};
        const float* w = Shadow(vs[0], kLitVs2);
        if (!w || CBDirtyTracker::Get().GetShadowSize(vs[0]) != kLitVs2) return;
        if (!s.mask) {
            // The lit shader reads the shadow mask at t14 (sun in x, shadowed point lights in y, z, w).
            // Interiors have no cascade draw to find it from, so it is taken here.
            ID3D11ShaderResourceView* v = nullptr;
            real->PSGetShaderResources(14, 1, &v);
            if (v) {
                ID3D11Resource* r = nullptr;
                v->GetResource(&r);
                if (r && FAILED(r->QueryInterface(IID_PPV_ARGS(&s.mask)))) s.mask = nullptr;
                if (r) r->Release();
                v->Release();
            }
        }
        LitDraw(s, g, w, Shadow(ps[0], 16), Shadow(ps[1], 80));
        s.merged = false;
    } else if (size == kMaskPs2) {
        if (const float* ps0 = Shadow(ps[0], 48)) MaskDraw(s, real, g, ps0);
    }
}

void SceneLights_SetOpaque(bool open) { S().opaque = open; }

void SceneLights_SetCamera(ID3D11DeviceContext* real)
{
    ID3D11Buffer* fr[1] = {};
    real->VSGetConstantBuffers(12, 1, fr);
    if (!fr[0]) return;
    if (const float* m = Shadow(fr[0], kFrameCb)) Camera(S().frame, m);
    fr[0]->Release();
    S().merged = false;
}

const raw::bridge::SceneLighting& SceneLights_Frame()
{
    State& s = S();
    if (!s.merged) Merge(s);
    return s.frame;
}

ID3D11Texture2D* SceneLights_ShadowMask() { return S().mask; }
ID3D11Texture2D* SceneLights_Cascades() { return S().cascades; }

void SceneLights_OnPresent()
{
    State& s = S();
    static uint64_t frame = 0;
    if (++frame % 600 == 1 && s.frame.litDraws) {
        const SceneLighting& f = SceneLights_Frame();
        Log("[SceneLights] frame %llu: %u lit draws, %u lights from %u sightings (%u over the limit, %u copies at a wrong place), sun %d (%.3f %.3f %.3f, colour %.3f %.3f %.3f), "
            "ambient w %.3f %.3f %.3f, cascades %u (split %.6f), knee %.4f, specular scale %.3f",
            (unsigned long long)frame, f.litDraws, f.lightCount, f.lightSightings, f.droppedLights, f.phantomLights, f.hasSun, f.sunDirection[0],
            f.sunDirection[1], f.sunDirection[2], f.sunColour[0], f.sunColour[1], f.sunColour[2], f.ambient[0][3], f.ambient[1][3],
            f.ambient[2][3], f.hasCascades ? f.cascades : 0, f.splitDepth, f.knee, f.specularScale);
    }
    if (s.mask) s.mask->Release();
    if (s.cascades) s.cascades->Release();
    s.mask = s.cascades = nullptr;
    s.frame = SceneLighting{};
    s.lights.clear();
    s.specScales.clear();
    std::memset(s.sunDir, 0, sizeof s.sunDir); std::memset(s.sunCol, 0, sizeof s.sunCol); std::memset(s.amb, 0, sizeof s.amb);
    s.knee = 0;
    s.sunDraws = s.ambDraws = s.kneeDraws = 0;
    s.merged = false;
}

} // namespace SB::Proxy
