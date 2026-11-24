// The water hand-over (both water textures take UAVs: the skin scattering writes back into water-in after the water) (RtWater.cpp, docs/RAYTRACING.md "Water"): attached to raw::tx only. The game has
// just composited its water into the HDR colour. The colour is copied in, the layer's work is handed to the
// queue with RAW's layer target and both water textures in COMMON, RtScene's water list follows on the same
// queue, and the copy back lands in the layer's next list. Original work, MIT.
#include "BridgeImpl.h"
#include "LightingPass.h"
#include "RtScene.h"
#include "StreamlineHost.h"

namespace raw::bridge
{

bool D3D12Bridge::CaptureBedDepth(ID3D11Resource* depth)
{
    if (!m->ready || !m->attached || !depth) return false;
    D3D11_RESOURCE_DIMENSION dim{};
    depth->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return false;
    D3D11_TEXTURE2D_DESC d{};
    static_cast<ID3D11Texture2D*>(depth)->GetDesc(&d);
    if (!m->bed11) {
        const DXGI_FORMAT srv = d.Format == DXGI_FORMAT_R24G8_TYPELESS || d.Format == DXGI_FORMAT_D24_UNORM_S8_UINT ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS
                              : d.Format == DXGI_FORMAT_R32_TYPELESS || d.Format == DXGI_FORMAT_D32_FLOAT ? DXGI_FORMAT_R32_FLOAT
                              : d.Format == DXGI_FORMAT_R32G8X24_TYPELESS ? DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS : DXGI_FORMAT_UNKNOWN;
        D3D11_TEXTURE2D_DESC b = d;
        b.BindFlags = D3D11_BIND_SHADER_RESOURCE; b.CPUAccessFlags = 0; b.MiscFlags = 0; b.Usage = D3D11_USAGE_DEFAULT;
        if (b.Format == DXGI_FORMAT_D24_UNORM_S8_UINT) b.Format = DXGI_FORMAT_R24G8_TYPELESS;
        if (b.Format == DXGI_FORMAT_D32_FLOAT) b.Format = DXGI_FORMAT_R32_TYPELESS;
        ComPtr<ID3D11Texture2D> t;
        if (srv == DXGI_FORMAT_UNKNOWN || d.SampleDesc.Count != 1 || FAILED(m->dev11->CreateTexture2D(&b, nullptr, &t)) || !(m->bed12 = m->cfg.layer.native(t.Get()))) {
            m->log("[RT] bed depth off: the game's depth is format %d, %u samples", int(d.Format), d.SampleDesc.Count);
            m->bedSrv = DXGI_FORMAT_UNKNOWN;
            return false;
        }
        m->bed11 = t;
        m->bedSrv = srv;
        m->log("[RT] bed depth: %ux%u, format %d, read as %d", d.Width, d.Height, int(b.Format), int(srv));
    }
    m->ctx11->CopyResource(m->bed11.Get(), depth);
    m->bedFrame = m->frames;
    return true;
}

bool D3D12Bridge::CapturePreWater(ID3D11Resource* sceneHdr)
{
    if (!m->ready || !m->attached || !sceneHdr || m->waterFailed) return false;
    if (!m->preWater.tex11 && !m->CreateAttached(m->preWater, DXGI_FORMAT_R16G16B16A16_FLOAT, true, "water-before", 0, 0)) {
        m->log("[RT] refraction off: no texture for the frame before the water");
        return false;
    }
    m->ctx11->CopyResource(m->preWater.tex11.Get(), sceneHdr);
    m->preWaterFrame = m->frames;
    return true;
}

bool D3D12Bridge::ExecuteWater(ID3D11Resource* sceneHdr, ID3D11Resource* layer, bool water, float sssWidth, float sssStrength)
{
    if (!m->ready || !m->attached || !m->cfg.lightingPass || !m->rt || !m->rt->Active() || !sceneHdr || !layer || m->waterFailed) return false;
    if (!m->waterIn.tex11) {
        if (!m->CreateAttached(m->waterIn, DXGI_FORMAT_R16G16B16A16_FLOAT, true, "water-in", 0, 0) ||
            !m->CreateAttached(m->waterOut, DXGI_FORMAT_R16G16B16A16_FLOAT, true, "water-out", 0, 0)) {
            m->waterFailed = true;
            m->log("[RT] water off: no targets");
            return false;
        }
        m->log("[RT] water targets made: %ux%u RGBA16F, in and out", m->cfg.width, m->cfg.height);
    }
    const void* u = nullptr;
    size_t ub = 0;
    ID3D12Resource* layer12 = m->cfg.layer.native(layer);
    if (!layer12 || !LightingPassUniform(m->pass.get(), &u, &ub)) return false;
    auto* c = m->ctx11.Get();
    c->CopyResource(m->waterIn.tex11.Get(), sceneHdr);
    std::vector<ID3D11Resource*> hand{layer, m->waterIn.tex11.Get(), m->waterOut.tex11.Get()};
    if (m->bed11) hand.push_back(m->bed11.Get());
    if (m->preWater.tex11) hand.push_back(m->preWater.tex11.Get());
    if (!m->HandToHost(hand)) {
        m->log("[RT] water hand-over failed; water off");
        m->waterFailed = true;
        return false;
    }
    // Run NVIDIA DLSS Ray Reconstruction (Preset F) on the opaque RT-lit frame BEFORE RtWater!
    // Reason: DLSS-RR receives static opaque depth, static opaque normal, and (0,0) motion vectors on water pixels.
    // Running DLSS-RR *after* RtWater caused it to temporally average/freeze animated water waves as RT noise.
    // Running DLSS-RR *before* RtWater gives RtWater a clean denoised scene for sharp reflections AND keeps
    // animated Gerstner waves, flowing river current, caustics & foam 100% crisp and un-smeared!
    bool slPreRan = false;
    if (water && m->motionIn.res12) {
        StreamlineHost::EvalCamera slCam{};
        if (m->camera.valid && m->lighting.hasCamera) {
            slCam.valid = true;
            slCam.nearZ = m->camera.nearZ;
            slCam.farZ = m->camera.farZ;
            slCam.fovY = 2.0f * std::atan(m->camera.tanHalfFovY);
            slCam.aspect = m->camera.aspect;
            std::memcpy(slCam.proj, m->camera.proj, sizeof(slCam.proj));
            std::memcpy(slCam.viewToWorld, m->lighting.viewToWorld, sizeof(slCam.viewToWorld));
            std::memcpy(slCam.worldToClip, m->lighting.worldToClip, sizeof(slCam.worldToClip));
            slCam.pos[0] = m->lighting.posAdjust[0];
            slCam.pos[1] = m->lighting.posAdjust[1];
            slCam.pos[2] = m->lighting.posAdjust[2];
            slCam.jitterNdc[0] = m->lighting.jitterNdc[0];
            slCam.jitterNdc[1] = m->lighting.jitterNdc[1];
        }
        slPreRan = StreamlineHost::Get().Evaluate(m->queue.Get(), m->waterIn.res12.Get(), m->waterOut.res12.Get(),
                                                  m->depthIn.res12.Get(), m->motionIn.res12.Get(),
                                                  m->shared[static_cast<UINT>(Shared::Albedo)].res12.Get(),
                                                  m->normalIn.res12.Get(),
                                                  m->shared[static_cast<UINT>(Shared::Specular)].res12.Get(),
                                                  m->cfg.width, m->cfg.height,
                                                  static_cast<uint32_t>(m->frames), slCam, m->log);
    }
    ID3D12Resource* wSrc = slPreRan ? m->waterOut.res12.Get() : m->waterIn.res12.Get();
    ID3D12Resource* wDst = slPreRan ? m->waterIn.res12.Get() : m->waterOut.res12.Get();
    bool inB = false;
    if (!m->rt->RecordWater(u, ub, m->depthIn.res12.Get(), layer12, m->shared[static_cast<UINT>(Shared::Albedo)].res12.Get(),
                            m->shared[static_cast<UINT>(Shared::Specular)].res12.Get(), wSrc, wDst,
                            water, sssWidth, sssStrength, &inB, m->bedFrame + 1 >= m->frames ? m->bed12 : nullptr, m->bedSrv,
                            m->preWaterFrame + 1 >= m->frames ? m->preWater.res12.Get() : nullptr))
        return false;
    if (!m->rt->SubmitWater(m->queue.Get())) {
        m->log("[RT] water submit failed; water off");
        m->waterFailed = true;
        return false;
    }
    bool resInWaterIn = slPreRan ? !inB : inB;
    // When there is no water this frame (interiors / dry views), run DLSS-RR after SSS as usual.
    if (!slPreRan && m->motionIn.res12) {
        ID3D12Resource* src12 = resInWaterIn ? m->waterIn.res12.Get() : m->waterOut.res12.Get();
        ID3D12Resource* dst12 = resInWaterIn ? m->waterOut.res12.Get() : m->waterIn.res12.Get();
        StreamlineHost::EvalCamera slCam{};
        if (m->camera.valid && m->lighting.hasCamera) {
            slCam.valid = true;
            slCam.nearZ = m->camera.nearZ;
            slCam.farZ = m->camera.farZ;
            slCam.fovY = 2.0f * std::atan(m->camera.tanHalfFovY);
            slCam.aspect = m->camera.aspect;
            std::memcpy(slCam.proj, m->camera.proj, sizeof(slCam.proj));
            std::memcpy(slCam.viewToWorld, m->lighting.viewToWorld, sizeof(slCam.viewToWorld));
            std::memcpy(slCam.worldToClip, m->lighting.worldToClip, sizeof(slCam.worldToClip));
            slCam.pos[0] = m->lighting.posAdjust[0];
            slCam.pos[1] = m->lighting.posAdjust[1];
            slCam.pos[2] = m->lighting.posAdjust[2];
            slCam.jitterNdc[0] = m->lighting.jitterNdc[0];
            slCam.jitterNdc[1] = m->lighting.jitterNdc[1];
        }
        if (StreamlineHost::Get().Evaluate(m->queue.Get(), src12, dst12, m->depthIn.res12.Get(),
                                           m->motionIn.res12.Get(),
                                           m->shared[static_cast<UINT>(Shared::Albedo)].res12.Get(),
                                           m->normalIn.res12.Get(),
                                           m->shared[static_cast<UINT>(Shared::Specular)].res12.Get(),
                                           m->cfg.width, m->cfg.height,
                                           static_cast<uint32_t>(m->frames), slCam, m->log)) {
            resInWaterIn = !resInWaterIn;
        }
    }
    c->CopyResource(sceneHdr, resInWaterIn ? m->waterIn.tex11.Get() : m->waterOut.tex11.Get());
    return true;
}

bool D3D12Bridge::ReloadWater(const std::vector<uint8_t>& water, const std::vector<uint8_t>& sss, std::string& err)
{
    if (!m->rt) { err = "no ray-tracing scene (RtShadows=0, or not on raw::tx)"; return false; }
    return m->rt->ReloadWater(water, sss, err);
}

} // namespace raw::bridge
