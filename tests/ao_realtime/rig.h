#pragma once
// ao_realtime_check rig: the D3D11 input textures, a bridge with one of the two
// engine passes, frames through the real path, and the score against a
// reference visibility. Included by main.cpp. Original work, MIT.

struct Score
{
    long pixels = 0, over005 = 0, occludedPixels = 0;
    double meanAbs = 0, meanSigned = 0, occludedMeanAbs = 0;   // occluded: where the reference is under 0.9
};

// Drawn pixels only (depth under 1). Visibility is clamped to 1 as the composite clamps it.
static Score ScoreAgainst(const std::vector<float>& vis, const std::vector<float>& ref, const Inputs& in)
{
    Score s;
    double sum = 0, occ = 0, sgn = 0;
    for (size_t i = 0; i < ref.size(); ++i) {
        if ((in.depth[i] & 0xFFFFFF) == 0xFFFFFF) continue;
        const double e = std::fabs(double(std::min(vis[i], 1.0f)) - double(ref[i]));
        ++s.pixels; sum += e; s.over005 += e > 0.05; sgn += double(std::min(vis[i], 1.0f)) - double(ref[i]);
        if (ref[i] < 0.9f) { ++s.occludedPixels; occ += e; }
    }
    s.meanAbs = s.pixels ? sum / double(s.pixels) : 0;
    s.meanSigned = s.pixels ? sgn / double(s.pixels) : 0;
    s.occludedMeanAbs = s.occludedPixels ? occ / double(s.occludedPixels) : 0;
    return s;
}

// The inputs moved `dx` pixels to the right, edge texels repeated: the frame a camera
// pan would show if every surface kept its depth. The reference for a moved frame is
// computed on the moved buffers, so the comparison never depends on this model.
static Inputs Shifted(const Inputs& in, int dx)
{
    Inputs o = in;
    using Plane = std::vector<uint32_t> Inputs::*;
    for (Plane plane : {&Inputs::depth, &Inputs::normal, &Inputs::colour})
        for (UINT y = 0; y < in.h; ++y)
            for (UINT x = 0; x < in.w; ++x)
                (o.*plane)[size_t(y) * in.w + x] = (in.*plane)[size_t(y) * in.w + UINT(std::clamp(int(x) - dx, 0, int(in.w) - 1))];
    return o;
}

static uint16_t FloatToHalf(float f)
{
    if (f == 0) return 0;
    int e;
    const float m = std::frexp(std::fabs(f), &e);
    if (e < -13) return 0;
    return uint16_t((f < 0 ? 0x8000 : 0) | ((e + 14) << 10) | (int(std::lround(m * 2048.0f)) - 1024));
}

struct Stats { double median = 0, p10 = 0, p90 = 0; size_t n = 0; };
static Stats StatsOf(std::vector<double> x)
{
    Stats s;
    if (x.empty()) return s;
    std::sort(x.begin(), x.end());
    s.n = x.size(); s.median = x[x.size() / 2]; s.p10 = x[x.size() / 10]; s.p90 = x[x.size() * 9 / 10];
    return s;
}

struct Rig
{
    fx::ComPtr<ID3D11Device> dev;
    fx::ComPtr<ID3D11DeviceContext> ctx;
    fx::ComPtr<ID3D11Texture2D> colour, dst, depth, normal, motion;
    fx::ComPtr<ID3D11ShaderResourceView> depthSrv;
    std::unique_ptr<raw::bridge::D3D12Bridge> bridge;
    UINT w = 0, h = 0;
    raw::bridge::CameraConstants cam;
    raw::bridge::LogFn log = nullptr;

    bool Create(const Inputs& in, bool warp, raw::bridge::LogFn logFn)
    {
        log = logFn; w = in.w; h = in.h;
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        if (FAILED(D3D11CreateDevice(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1,
                                     D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
            return false;
        colour = fx::MakeTex(dev.Get(), w, h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, in.colour.data(), w * 4);
        dst = fx::MakeTex(dev.Get(), w, h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET, nullptr, 0);
        depth = fx::MakeTex(dev.Get(), w, h, DXGI_FORMAT_R24G8_TYPELESS, D3D11_BIND_SHADER_RESOURCE, in.depth.data(), w * 4);
        normal = fx::MakeTex(dev.Get(), w, h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, in.normal.data(), w * 4);
        const std::vector<uint32_t> still(size_t(w) * h, 0);
        motion = fx::MakeTex(dev.Get(), w, h, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE, still.data(), w * 4);
        depthSrv = fx::MakeSrv(dev.Get(), depth.Get(), DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
        cam.valid = true;
        cam.proj[0] = in.sx; cam.proj[5] = in.sy; cam.proj[10] = in.A; cam.proj[11] = -in.B; cam.proj[14] = 1.0f;
        cam.tanHalfFovY = 1.0f / in.sy; cam.aspect = in.sy / in.sx; cam.nearZ = in.B / in.A; cam.farZ = in.B / (in.A - 1.0f);
        return colour && dst && depth && normal && motion && depthSrv;
    }

    // A fresh bridge with the reference GTAO (realtime = false) or the real-time AO.
    bool Bridge(bool realtime, bool debugLayer)
    {
        bridge.reset();
        bridge = std::make_unique<raw::bridge::D3D12Bridge>();
        raw::bridge::BridgeConfig cfg;
        cfg.width = w; cfg.height = h;
        cfg.debugLayer = debugLayer;
        cfg.normalFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        cfg.motionFormat = realtime ? DXGI_FORMAT_R16G16_FLOAT : DXGI_FORMAT_UNKNOWN;
        cfg.enginePass = true;
        cfg.realtimeAo = realtime;
        cfg.cpuWaitTimeout = 120000;
        if (!bridge->Initialize(dev.Get(), ctx.Get(), cfg, log)) return false;
        bridge->SetCamera(cam);
        return true;
    }

    void Upload(const Inputs& in)
    {
        ctx->UpdateSubresource(colour.Get(), 0, nullptr, in.colour.data(), w * 4, 0);
        ctx->UpdateSubresource(depth.Get(), 0, nullptr, in.depth.data(), w * 4, 0);
        ctx->UpdateSubresource(normal.Get(), 0, nullptr, in.normal.data(), w * 4, 0);
    }

    // The motion target filled with one vector, in the game's units (a fraction of the frame).
    void SetMotion(float u, float v)
    {
        const uint32_t texel = uint32_t(FloatToHalf(u)) | (uint32_t(FloatToHalf(v)) << 16);
        const std::vector<uint32_t> all(size_t(w) * h, texel);
        ctx->UpdateSubresource(motion.Get(), 0, nullptr, all.data(), w * 4, 0);
    }

    bool Frame(float mode = 2.0f)
    {
        raw::bridge::PassConstants k;
        k.mode = mode;
        if (bridge->Config().realtimeAo) bridge->CaptureMotion(motion.Get());
        return bridge->CaptureDepth(depthSrv.Get()) && bridge->CaptureNormal(normal.Get()) && bridge->Execute(colour.Get(), dst.Get(), k);
    }

    // `frames` frames from an empty history, then the visibility.
    bool Run(raw::bridge::EngineTuning t, int frames, std::vector<float>* vis)
    {
        t.resetHistory = true;
        bridge->TuneEngine(t);
        bool ok = Frame();
        t.resetHistory = false;
        bridge->TuneEngine(t);
        for (int f = 1; ok && f < frames; ++f) ok = Frame();
        return ok && bridge->ReadEngineVisibility(vis) && vis->size() == size_t(w) * h;
    }

    // GPU cost over `frames` further frames (the first two dropped): the pass by the RHI's
    // timestamps, and the D3D11 copies that feed it (depth, normals with motion).
    void Time(int frames, Stats* pass, Stats* stage, Stats* inputs, Stats* bridgeCost)
    {
        std::vector<double> total, st[3], in, cost;
        for (int f = 0; f < frames + 2; ++f) {
            Frame(1.0f);
            raw::bridge::BridgeTimings t;
            if (!bridge->ReadTimings(bridge->FramesExecuted() - 1, true, &t) || f < 2) continue;
            total.push_back(t.passMs);
            for (int k = 0; k < 3; ++k) st[k].push_back(t.pass.stageMs[k]);
            in.push_back(t.depthCopyMs + t.normalCopyMs);
            cost.push_back(t.bridgeCostMs);
        }
        *pass = StatsOf(total);
        for (int k = 0; k < 3; ++k) stage[k] = StatsOf(st[k]);
        *inputs = StatsOf(in);
        *bridgeCost = StatsOf(cost);
    }
};
