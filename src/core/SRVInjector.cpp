#include "SRVInjector.h"
#include "D3D11Hook.h"

#include <SKSE/SKSE.h>

namespace SB
{

SRVInjector& SRVInjector::Get()
{
    static SRVInjector inst;
    return inst;
}

bool SRVInjector::Initialize(ID3D11DeviceContext* ctx)
{
    if (!ctx) return false;
    m_context = ctx;
    SKSE::log::info("SRVInjector: initialized");
    return true;
}

static const char* SRVSlotName(UINT slot)
{
    switch (slot) {
    case 17: return "LuminanceHistogram.tex";  case 18: return "LUT.tex";
    case 19: return "HiZ.depth";               case 20: return "GTAO.output";
    case 21: return "ClusteredLighting.grid";  case 22: return "TAA.history";
    case 25: return "MaterialClassifier.ids";  case 26: return "SSGI.output";
    case 27: return "SSR.output";              case 28: return "ContactShadow.mask";
    case 29: return "Skylighting.vis";         case 30: return "BlueNoise.tex";
    case 31: return "LinearDepth.tex";         case 32: return "IndirectSpecular.output";
    case 33: return "VolumetricLighting.scatter"; case 34: return "DynamicCubemap.env";
    case 35: return "LightBuffer.data";        case 36: return "LightIndex.list";
    case 37: return "VolumetricClouds.scatter"; case 38: return "Celestial.tex";
    default: return "RAW.srv";
    }
}

void SRVInjector::RegisterSRV(UINT slot, ID3D11ShaderResourceView* srv)
{
    if (slot >= kMaxSlots) return;
    m_srvs[slot]      = srv;
    m_srvActive[slot]  = (srv != nullptr);
    D3D11Hook::LedgerRegisterName(srv, SRVSlotName(slot));
}

void SRVInjector::UnregisterSRV(UINT slot)
{
    if (slot >= kMaxSlots) return;
    m_srvs[slot]      = nullptr;
    m_srvActive[slot]  = false;
}

void SRVInjector::RegisterSampler(UINT slot, ID3D11SamplerState* sampler)
{
    if (slot >= kMaxSlots) return;
    m_samplers[slot]      = sampler;
    m_samplerActive[slot] = (sampler != nullptr);
}

void SRVInjector::UnregisterSampler(UINT slot)
{
    if (slot >= kMaxSlots) return;
    m_samplers[slot]      = nullptr;
    m_samplerActive[slot] = false;
}

void SRVInjector::InjectAll()
{
    if (!m_context) return;

    // Bind each registered SRV individually at its slot
    for (UINT i = 0; i < kMaxSlots; ++i) {
        if (m_srvActive[i]) {
            m_context->PSSetShaderResources(i, 1, &m_srvs[i]);
        }
    }

    // Bind each registered sampler individually at its slot
    for (UINT i = 0; i < kMaxSlots; ++i) {
        if (m_samplerActive[i]) {
            m_context->PSSetSamplers(i, 1, &m_samplers[i]);
        }
    }
}

void SRVInjector::ClearAll()
{
    if (!m_context) return;

    ID3D11ShaderResourceView* nullSRV = nullptr;
    for (UINT i = 0; i < kMaxSlots; ++i) {
        if (m_srvActive[i]) {
            m_context->PSSetShaderResources(i, 1, &nullSRV);
        }
    }

    ID3D11SamplerState* nullSampler = nullptr;
    for (UINT i = 0; i < kMaxSlots; ++i) {
        if (m_samplerActive[i]) {
            m_context->PSSetSamplers(i, 1, &nullSampler);
        }
    }
}

} // namespace SB
