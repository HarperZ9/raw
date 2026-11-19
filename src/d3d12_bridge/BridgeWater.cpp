// The water hand-over (RtWater.cpp, docs/RAYTRACING.md "Water"): attached to raw::tx only. The game has
// just composited its water into the HDR colour. The colour is copied in, the layer's work is handed to the
// queue with RAW's layer target and both water textures in COMMON, RtScene's water list follows on the same
// queue, and the copy back lands in the layer's next list. Original work, MIT.
#include "BridgeImpl.h"
#include "LightingPass.h"
#include "RtScene.h"

namespace raw::bridge
{

bool D3D12Bridge::ExecuteWater(ID3D11Resource* sceneHdr, ID3D11Resource* layer, bool water, float sssWidth, float sssStrength)
{
    if (!m->ready || !m->attached || !m->cfg.lightingPass || !m->rt || !m->rt->Active() || !sceneHdr || !layer || m->waterFailed) return false;
    if (!m->waterIn.tex11) {
        if (!m->CreateAttached(m->waterIn, DXGI_FORMAT_R16G16B16A16_FLOAT, false, "water-in", 0, 0) ||
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
    bool inB = false;
    if (!m->rt->RecordWater(u, ub, m->depthIn.res12.Get(), layer12, m->shared[static_cast<UINT>(Shared::Albedo)].res12.Get(),
                            m->shared[static_cast<UINT>(Shared::Specular)].res12.Get(), m->waterIn.res12.Get(), m->waterOut.res12.Get(),
                            water, sssWidth, sssStrength, &inB))
        return false;
    const std::vector<ID3D11Resource*> hand{layer, m->waterIn.tex11.Get(), m->waterOut.tex11.Get()};
    if (!m->HandToHost(hand) || !m->rt->SubmitWater(m->queue.Get())) {
        m->log("[RT] water hand-over failed; water off");
        m->waterFailed = true;
        return false;
    }
    c->CopyResource(sceneHdr, inB ? m->waterIn.tex11.Get() : m->waterOut.tex11.Get());
    return true;
}

} // namespace raw::bridge
