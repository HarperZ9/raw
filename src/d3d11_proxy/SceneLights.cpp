// SceneLights: see the header. Original work, MIT.
#include "ModuleHost.h"
#include "SceneLights.h"
#include "CBDirtyTracker.h"
#include "ProxyLog.h"
#include "WeatherDrive.h"
#include "../dx12/translate/Translate.h"

#include <algorithm>
#include <cmath>
#include <string>
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
    struct SunSample { double d[3]; };
    std::vector<SunSample> sunSamples;
    double stableSunDir[3] = {};
    bool hasStableSun = false;
    uint32_t sunJumpStreak = 0;
    uint32_t sunDraws = 0, ambDraws = 0, kneeDraws = 0;
    std::vector<float> specScales;
    std::vector<float> steps[3];         // per lit draw: previous minus current world translation
    ID3D11Texture2D* mask = nullptr;     // held for the frame
    ID3D11Texture2D* cascades = nullptr; // held for the frame
    bool merged = false;
    bool opaque = false;                 // the game's opaque set is bound (the main camera's draws)
};
State& S() { static State s; return s; }
const float* g_drawWorld = nullptr;

const float* Shadow(ID3D11Buffer* b, uint32_t want, uint32_t* sizeOut = nullptr)
{
    uint32_t size = 0;
    const uint8_t* d = b ? CBDirtyTracker::Get().Shadow(b, &size) : nullptr;
    if (sizeOut) *sizeOut = size;
    return d && size >= want ? reinterpret_cast<const float*>(d) : nullptr;
}

// The constant buffers bound to a stage (0 VS, 4 PS), borrowed: on raw::tx they are read without a
// reference (no Get and Release per draw); on the system runtime the references are dropped at once,
// which is safe while the buffers stay bound for the draw.
void BoundCBs(ID3D11DeviceContext* real, UINT stage, UINT start, UINT n, ID3D11Buffer** out)
{
    static ID3D11DeviceContext* known = nullptr;
    static bool layer = false;
    if (real != known) { known = real; layer = raw::tx::IsLayerContext(real); }
    if (layer) { raw::tx::PeekConstantBuffers(real, stage, start, n, out); return; }
    if (stage == 4) real->PSGetConstantBuffers(start, n, out); else real->VSGetConstantBuffers(start, n, out);
    for (UINT k = 0; k < n; ++k) if (out[k]) out[k]->Release();
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
            const double nx = d[0] / len, ny = d[1] / len, nz = d[2] / len;
            for (int k = 0; k < 3; ++k) { s.sunDir[k] += d[k] / len; s.sunCol[k] += g[4 + k]; }
            if (s.sunSamples.size() < 96) s.sunSamples.push_back({{nx, ny, nz}});
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
    std::memcpy(f.posAdjust, m + 40 * 4, 16);
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

// The engine module's light list (RAW_EngineLights, raw_engine.dll's rt_livestate.h): every active light of the
// scene, the ones no visible lit draw carried included (off screen, behind the camera, past a draw's light
// limit). A light the draws already gave is matched by place and kept as decoded; the rest join with no shadow
// channel, and the rays shadow them. [Lighting] EngineLights=0 keeps the decoded list only.
struct EngineLight { float pos[3]; float radius; float color[3]; float fade; uint32_t flags; uint32_t pad; };
using EngineLightsFn = uint32_t (*)(EngineLight*, uint32_t, uint64_t*);
bool g_engineLights = true;
struct EngineStats { uint64_t frames = 0, seen = 0, matched = 0, added = 0; double dist = 0, ratio = 0; uint32_t ratios = 0; } g_es;

void AddEngineLights(SceneLighting& f)
{
    static EngineLightsFn fn = nullptr;
    static int tries = 0;
    static uint32_t generation = 0;
    if (!g_engineLights || !f.hasCamera) return;
    // a live reload replaces the module (ModuleHost.h): the old export is gone, so it is looked up again
    if (generation != SB::Proxy::modules::Generation()) { generation = SB::Proxy::modules::Generation(); fn = nullptr; tries = 0; }
    if (!fn && tries < 600 && (++tries % 60) == 1)
        if (HMODULE m = SB::Proxy::modules::Find("raw_engine.dll")) fn = reinterpret_cast<EngineLightsFn>(GetProcAddress(m, "RAW_EngineLights"));
    if (!fn) return;
    EngineLight e[512];
    uint64_t at = 0;
    const uint32_t n = std::min<uint32_t>(fn(e, 512u, &at), 512u);
    ++g_es.frames;
    const uint32_t decoded = f.lightCount;
    bool hasEngineSun = false;
    float engineSunDir[3] = {};
    // Rank all up-to-512 ShadowSceneNode active/shadow/emitter lights by perceptual importance (luminance * radius / dist)
    // so dense cities & interiors (Solitude, Whiterun, Dragonsreach) always fill the 128 DX12+DXR slots with zero light popping.
    uint32_t order[512];
    float importance[512];
    uint32_t candCount = 0;
    for (uint32_t k = 0; k < n; ++k) {
        if (e[k].flags & 4) {
            f.inWater = e[k].pad != 0;
            const float sl = std::sqrt(e[k].pos[0] * e[k].pos[0] + e[k].pos[1] * e[k].pos[1] + e[k].pos[2] * e[k].pos[2]);
            if (sl > 0.5f && sl < 1.5f) {
                engineSunDir[0] = e[k].pos[0] / sl;
                engineSunDir[1] = e[k].pos[1] / sl;
                engineSunDir[2] = e[k].pos[2] / sl;
                hasEngineSun = true;
            }
            continue;
        }
        if (e[k].radius <= 0) continue;
        if (e[k].color[0] <= 0 && e[k].color[1] <= 0 && e[k].color[2] <= 0) continue;
        const float rx = e[k].pos[0] - f.posAdjust[0], ry = e[k].pos[1] - f.posAdjust[1], rz = e[k].pos[2] - f.posAdjust[2];
        const float dist = std::sqrt(rx * rx + ry * ry + rz * rz);
        const float lum = e[k].color[0] * 0.2126f + e[k].color[1] * 0.7152f + e[k].color[2] * 0.0722f;
        const float casterBoost = (e[k].flags & 2) ? 1.65f : ((e[k].flags & 8) ? 1.25f : 1.0f);
        order[candCount] = k;
        importance[k] = (lum * e[k].radius * casterBoost) / (dist + 64.0f);
        ++candCount;
    }
    if (candCount > raw::bridge::kMaxSceneLights) {
        std::partial_sort(order, order + raw::bridge::kMaxSceneLights, order + candCount,
                          [&](uint32_t a, uint32_t b) { return importance[a] > importance[b]; });
        candCount = raw::bridge::kMaxSceneLights;
    }
    // Camera-independent world-space temporal light smoother: prevents 1-frame intensity/radius snaps
    // when walking through interiors or crossing doorways as ShadowSceneNode updates active lights.
    struct SmoothLight { float wpos[3]; float col[3]; float radius; uint32_t age; };
    static std::vector<SmoothLight> g_smooth;
    for (auto& sl : g_smooth) ++sl.age;
    auto smoothColor = [&](const float wpos[3], float col[3], float radius) {
        int hit = -1;
        float bestD2 = 36.0f * 36.0f;
        for (size_t i = 0; i < g_smooth.size(); ++i) {
            const float dx = g_smooth[i].wpos[0] - wpos[0], dy = g_smooth[i].wpos[1] - wpos[1], dz = g_smooth[i].wpos[2] - wpos[2];
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < bestD2) { bestD2 = d2; hit = int(i); }
        }
        if (hit >= 0) {
            SmoothLight& sl = g_smooth[hit];
            for (int c = 0; c < 3; ++c) {
                sl.col[c] = sl.col[c] + (col[c] - sl.col[c]) * 0.22f;
                col[c] = sl.col[c];
                sl.wpos[c] = wpos[c];
            }
            sl.radius = sl.radius + (radius - sl.radius) * 0.22f;
            sl.age = 0;
        } else if (g_smooth.size() < 256) {
            SmoothLight sl{};
            for (int c = 0; c < 3; ++c) { sl.wpos[c] = wpos[c]; sl.col[c] = col[c] * 0.35f; col[c] = sl.col[c]; }
            sl.radius = radius;
            sl.age = 0;
            g_smooth.push_back(sl);
        }
    };

    // Snapshot decoded draw-scraped lights so we can harvest their shadowChannel WITHOUT letting
    // frustum-culled draws overwrite ShadowSceneNode's camera-independent color or radius!
    std::vector<SceneLight> decodedCopy(f.lights, f.lights + decoded);
    std::vector<bool> decodedUsed(decoded, false);
    if (candCount > 0) f.lightCount = 0;   // ShadowSceneNode is authoritative when live!

    for (uint32_t ci = 0; ci < candCount; ++ci) {
        const uint32_t k = order[ci];
        ++g_es.seen;
        float rel[3];
        for (int c = 0; c < 3; ++c) rel[c] = e[k].pos[c] - f.posAdjust[c];   // world to the game's camera-relative space
        int best = -1;
        float bestD = 56.0f * 56.0f;
        for (uint32_t j = 0; j < decoded; ++j) {
            if (decodedUsed[j]) continue;
            const float dx = decodedCopy[j].position[0] - rel[0], dy = decodedCopy[j].position[1] - rel[1], dz = decodedCopy[j].position[2] - rel[2];
            const float d = dx * dx + dy * dy + dz * dz;
            if (d < bestD) { bestD = d; best = int(j); }
        }
        int shadowCh = -1;
        uint32_t draws = 0;
        if (best >= 0) {
            decodedUsed[best] = true;
            shadowCh = decodedCopy[best].shadowChannel;
            draws = decodedCopy[best].draws;
            ++g_es.matched;
            g_es.dist += std::sqrt(bestD);
            const float a = decodedCopy[best].colour[0] + decodedCopy[best].colour[1] + decodedCopy[best].colour[2];
            const float b = e[k].color[0] + e[k].color[1] + e[k].color[2];
            if (b > 0) { g_es.ratio += a / b; ++g_es.ratios; }
        }
        if (f.lightCount >= raw::bridge::kMaxSceneLights) { ++f.droppedLights; continue; }
        SceneLight& l = f.lights[f.lightCount++];
        float col[3] = { e[k].color[0] * 1.85f, e[k].color[1] * 1.85f, e[k].color[2] * 1.85f };
        smoothColor(e[k].pos, col, e[k].radius);
        for (int c = 0; c < 3; ++c) { l.position[c] = rel[c]; l.colour[c] = col[c]; }
        l.radius = e[k].radius;
        l.shadowChannel = shadowCh;
        l.draws = draws;
        ++g_es.added;
    }
    // Keep any unmatched decoded lights (e.g. transient spell impacts not in ShadowSceneNode), smoothly damped.
    if (candCount > 0) {
        for (uint32_t j = 0; j < decoded && f.lightCount < raw::bridge::kMaxSceneLights; ++j) {
            if (decodedUsed[j]) continue;
            SceneLight& l = f.lights[f.lightCount++];
            l = decodedCopy[j];
            float wp[3] = { l.position[0] + f.posAdjust[0], l.position[1] + f.posAdjust[1], l.position[2] + f.posAdjust[2] };
            smoothColor(wp, l.colour, l.radius);
        }
    }
    g_smooth.erase(std::remove_if(g_smooth.begin(), g_smooth.end(), [](const SmoothLight& sl) { return sl.age > 30; }), g_smooth.end());
    if (hasEngineSun) {
        f.hasSun = true;
        for (int c = 0; c < 3; ++c) f.sunDirection[c] = engineSunDir[c];
    }
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
    AddEngineLights(f);
    const bool engineLockedSun = f.hasSun;
    f.hasSun = engineLockedSun || s.sunDraws > 0 || s.hasStableSun;
    if (s.sunDraws > 0) {
        if (engineLockedSun) {
            for (int k = 0; k < 3; ++k) f.sunColour[k] = float(s.sunCol[k] / s.sunDraws);
        } else {
        // Geometric consensus cluster (dot > 0.998): fallback when raw_engine.dll is absent.
        double nd[3] = {};
        uint32_t bestCount = 0;
        const size_t nSamp = s.sunSamples.size();
        for (size_t i = 0; i < nSamp; ++i) {
            uint32_t cnt = 0;
            double sum[3] = {};
            const auto& a = s.sunSamples[i].d;
            for (size_t j = 0; j < nSamp; ++j) {
                const auto& b = s.sunSamples[j].d;
                if (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] > 0.998) {
                    ++cnt;
                    sum[0] += b[0]; sum[1] += b[1]; sum[2] += b[2];
                }
            }
            if (cnt > bestCount) {
                bestCount = cnt;
                const double cl = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
                if (cl > 1e-6) { nd[0] = sum[0] / cl; nd[1] = sum[1] / cl; nd[2] = sum[2] / cl; }
            }
        }
        if (bestCount >= 6) {
            if (!s.hasStableSun) {
                for (int k = 0; k < 3; ++k) s.stableSunDir[k] = nd[k];
                s.hasStableSun = true;
                s.sunJumpStreak = 0;
            } else {
                const double cosA = std::clamp(nd[0] * s.stableSunDir[0] + nd[1] * s.stableSunDir[1] + nd[2] * s.stableSunDir[2], -1.0, 1.0);
                // Real Skyrim sun moves ~0.001 deg/frame. Clamp slew unless sustained 10-frame jump (set_time / wait / cell load).
                if (cosA < 0.96) {
                    if (++s.sunJumpStreak >= 10 && bestCount >= 16) {
                        for (int k = 0; k < 3; ++k) s.stableSunDir[k] = nd[k];
                        s.sunJumpStreak = 0;
                    }
                } else {
                    s.sunJumpStreak = 0;
                    const double alpha = bestCount >= 20 ? 0.015 : 0.004;
                    double sl = 0;
                    for (int k = 0; k < 3; ++k) { s.stableSunDir[k] += (nd[k] - s.stableSunDir[k]) * alpha; sl += s.stableSunDir[k] * s.stableSunDir[k]; }
                    sl = std::sqrt(sl);
                    if (sl > 1e-6) for (int k = 0; k < 3; ++k) s.stableSunDir[k] /= sl;
                }
            }
        }
        if (s.hasStableSun) {
            for (int k = 0; k < 3; ++k) { f.sunDirection[k] = float(s.stableSunDir[k]); f.sunColour[k] = float(s.sunCol[k] / s.sunDraws); }
        }
        }
    } else if (!engineLockedSun && s.hasStableSun) {
        for (int k = 0; k < 3; ++k) f.sunDirection[k] = float(s.stableSunDir[k]);
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
    g_drawWorld = nullptr;
    ID3D11Buffer* ps[3] = {};
    BoundCBs(real, 4, 0, 3, ps);
    uint32_t size = 0;
    const float* g = Shadow(ps[2], 0, &size);
    if (!g) return;
    if (size == kLitPs2 && s.opaque) {   // reflection-cube faces draw lit too, from their own camera
        if (!s.frame.hasCamera) {
            ID3D11Buffer* fr[1] = {};
            BoundCBs(real, 0, 12, 1, fr);
            if (const float* m = Shadow(fr[0], kFrameCb)) Camera(s.frame, m);
        }
        ID3D11Buffer* vs[1] = {};
        BoundCBs(real, 0, 2, 1, vs);
        uint32_t vsSize = 0;
        const float* w = Shadow(vs[0], kLitVs2, &vsSize);
        if (!w || vsSize != kLitVs2) return;
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
        // Rows 3 to 5: the previous frame's world matrix. A static draw's translation moves by the
        // camera's step (world space is camera-relative); the median over the frame keeps out what moved.
        if (s.steps[0].size() < 4096)
            for (int k = 0; k < 3; ++k) s.steps[k].push_back(w[12 + k * 4 + 3] - w[k * 4 + 3]);
        g_drawWorld = w;
        s.merged = false;
    } else if (size == kMaskPs2) {
        if (const float* ps0 = Shadow(ps[0], 48)) MaskDraw(s, real, g, ps0);
    }
}

void SceneLights_SetOpaque(bool open) { S().opaque = open; }
void SceneLights_SetEngineLights(bool on) { g_engineLights = on; }
bool SceneLights_Opaque() { return S().opaque; }

void SceneLights_SetCamera(ID3D11DeviceContext* real)
{
    ID3D11Buffer* fr[1] = {};
    real->VSGetConstantBuffers(12, 1, fr);
    if (!fr[0]) return;
    if (const float* m = Shadow(fr[0], kFrameCb)) Camera(S().frame, m);
    fr[0]->Release();
    State& s = S();
    // Direct NiCamera & TESObjectCELL::IsInterior() ownership from raw_engine.dll (PG_SetEngineCamera).
    EngineCameraState ec;
    if (EngineCamera_Get(ec)) {
        s.frame.hasEngineCell = true;
        s.frame.isInterior = ec.interior != 0;
        s.frame.menuPaused = ec.menuPaused != 0;
        s.frame.waterHeight = ec.waterHeight;
        s.frame.camStateId = ec.camStateId;
        if (s.frame.posAdjust[0] == 0.0f && s.frame.posAdjust[1] == 0.0f && s.frame.posAdjust[2] == 0.0f) {
            s.frame.posAdjust[0] = ec.camPos[0];
            s.frame.posAdjust[1] = ec.camPos[1];
            s.frame.posAdjust[2] = ec.camPos[2];
        }
    }
    // Compute exact cameraStep from posAdjust delta (eliminates median-draw/skinned-mesh jitter in RtScene::SetCamera).
    static float s_prevPosAdj[3] = {};
    static bool s_hasPrevPosAdj = false;
    const bool hasPosAdj = (s.frame.posAdjust[0] != 0.0f || s.frame.posAdjust[1] != 0.0f || s.frame.posAdjust[2] != 0.0f);
    if (hasPosAdj && s_hasPrevPosAdj) {
        s.frame.cameraStep[0] = s.frame.posAdjust[0] - s_prevPosAdj[0];
        s.frame.cameraStep[1] = s.frame.posAdjust[1] - s_prevPosAdj[1];
        s.frame.cameraStep[2] = s.frame.posAdjust[2] - s_prevPosAdj[2];
        s.frame.cameraSteps = 1024u;
    } else {
        s.frame.cameraSteps = uint32_t(s.steps[0].size());
        for (int k = 0; k < 3 && !s.steps[k].empty(); ++k) {
            std::vector<float>& v = s.steps[k];
            std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
            s.frame.cameraStep[k] = v[v.size() / 2];
        }
    }
    if (hasPosAdj) {
        s_prevPosAdj[0] = s.frame.posAdjust[0];
        s_prevPosAdj[1] = s.frame.posAdjust[1];
        s_prevPosAdj[2] = s.frame.posAdjust[2];
        s_hasPrevPosAdj = true;
    }
    S().merged = false;
}

const raw::bridge::SceneLighting& SceneLights_Frame()
{
    State& s = S();
    if (!s.merged) Merge(s);
    return s.frame;
}

ID3D11Texture2D* SceneLights_ShadowMask() { return S().mask; }
const float* SceneLights_DrawWorld() { return g_drawWorld; }
ID3D11Texture2D* SceneLights_Cascades() { return S().cascades; }

void SceneLights_OnPresent()
{
    State& s = S();
    static uint64_t frame = 0;
    // [SceneLights] LogEvery: how often the frame's decoded lighting goes to the log (600 presents by default)
    static const uint64_t every = [] {
        char exe[MAX_PATH];
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string ini(exe);
        ini = ini.substr(0, ini.rfind('\\') + 1) + "d3d11_proxy.ini";
        const int n = GetPrivateProfileIntA("SceneLights", "LogEvery", 600, ini.c_str());
        return uint64_t(n > 0 ? n : 600);
    }();
    if (++frame % every == 1 && s.frame.litDraws) {
        const SceneLighting& f = SceneLights_Frame();
        if (f.hasCascades) {
            // the length of each cascade's first matrix row is 1 / its half-width in world units
            auto len = [](const float* r) { return std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]); };
            const float l0 = len(f.cascade[0][0]), l1 = len(f.cascade[1][0]);
            Log("[SceneLights] frame %llu: cascade half-widths %.1f %.1f", (unsigned long long)frame, l0 > 0 ? 1.0f / l0 : 0.0f,
                l1 > 0 ? 1.0f / l1 : 0.0f);
        }
        Log("[SceneLights] frame %llu: %u lit draws, %u lights from %u sightings (%u over the limit, %u copies at a wrong place), sun %d (%.3f %.3f %.3f, colour %.3f %.3f %.3f), "
            "ambient w %.3f %.3f %.3f, cascades %u (split %.6f), knee %.4f, specular scale %.3f",
            (unsigned long long)frame, f.litDraws, f.lightCount, f.lightSightings, f.droppedLights, f.phantomLights, f.hasSun, f.sunDirection[0],
            f.sunDirection[1], f.sunDirection[2], f.sunColour[0], f.sunColour[1], f.sunColour[2], f.ambient[0][3], f.ambient[1][3],
            f.ambient[2][3], f.hasCascades ? f.cascades : 0, f.splitDepth, f.knee, f.specularScale);
        // The eye in the game's camera-relative space (view-to-world translation, both storage orders): the ray-traced views
        // assume it sits at the origin, float4 40 of the per-frame constants (docs/RAYTRACING.md, the actors' views).
        Log("[SceneLights] frame %llu: eye from view-to-world: column %.2f %.2f %.2f, row %.2f %.2f %.2f; float4 40 %.1f %.1f %.1f",
            (unsigned long long)frame, f.viewToWorld[0][3], f.viewToWorld[1][3], f.viewToWorld[2][3], f.viewToWorld[3][0], f.viewToWorld[3][1],
            f.viewToWorld[3][2], f.posAdjust[0], f.posAdjust[1], f.posAdjust[2]);
    }
    if (frame % every == 1 && g_es.frames) {
        Log("[SceneLights] engine lights over %llu frames: %.1f point lights a frame, %.1f matched to a decoded light (mean %.1f units "
            "apart, decoded/engine colour %.3f), %.1f added", (unsigned long long)g_es.frames, double(g_es.seen) / g_es.frames,
            double(g_es.matched) / g_es.frames, g_es.matched ? g_es.dist / g_es.matched : 0.0, g_es.ratios ? g_es.ratio / g_es.ratios : 0.0,
            double(g_es.added) / g_es.frames);
        g_es = EngineStats{};
    }
    if (s.mask) s.mask->Release();
    if (s.cascades) s.cascades->Release();
    s.mask = s.cascades = nullptr;
    s.frame = SceneLighting{};
    s.lights.clear();
    s.sunSamples.clear();
    s.specScales.clear();
    for (auto& v : s.steps) v.clear();
    std::memset(s.sunDir, 0, sizeof s.sunDir); std::memset(s.sunCol, 0, sizeof s.sunCol); std::memset(s.amb, 0, sizeof s.amb);
    s.knee = 0;
    s.sunDraws = s.ambDraws = s.kneeDraws = 0;
    s.merged = false;
}

} // namespace SB::Proxy
