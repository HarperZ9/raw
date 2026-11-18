// On-demand sharing of any game buffer with the D3D12 device, with a D3D12
// read as the proof of what crossed. Depth, normals and the frame cross every
// frame through their own textures (BridgeFrame.cpp); this path carries the
// rest: motion vectors, the HDR scene, shadow maps, cube faces, masks.
// Original work, MIT.
#include "BridgeImpl.h"

namespace raw::bridge
{
namespace
{
// The format a buffer takes on the shared texture. D3D11 opens a D3D12
// texture only when it is render-target capable, so depth formats cross as
// the colour format with the same bits, and D24S8 as float depth.
DXGI_FORMAT CrossingFormat(DXGI_FORMAT f, bool* viaCompute)
{
    *viaCompute = false;
    switch (f) {
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: *viaCompute = true; return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_D16_UNORM:             return DXGI_FORMAT_R16_UNORM;
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT:             return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:     return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:     return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:  return DXGI_FORMAT_R10G10B10A2_UNORM;
        default: return f;
    }
}
} // namespace

D3D12Bridge::Impl::Extra* D3D12Bridge::Impl::ExtraFor(UINT w, UINT h, DXGI_FORMAT fmt, bool uav)
{
    for (Extra& e : extras)
        if (e.w == w && e.h == h && e.fmt == fmt && e.uav == uav) return &e;
    Extra e;
    e.w = w; e.h = h; e.fmt = fmt; e.uav = uav;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w; rd.Height = h; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = fmt; rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
               (uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE);
    HANDLE handle = nullptr;
    HRESULT hr = dev12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd, D3D12_RESOURCE_STATE_COMMON,
                                                nullptr, IID_PPV_ARGS(&e.tex.res12));
    if (SUCCEEDED(hr)) hr = dev12->CreateSharedHandle(e.tex.res12.Get(), nullptr, GENERIC_ALL, nullptr, &handle);
    if (SUCCEEDED(hr)) hr = dev11->OpenSharedResource1(handle, IID_PPV_ARGS(&e.tex.tex11));
    if (handle) CloseHandle(handle);
    if (SUCCEEDED(hr) && uav) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = fmt;
        ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        hr = dev11->CreateUnorderedAccessView(e.tex.tex11.Get(), &ud, &e.uav11);
    }
    if (FAILED(hr)) {
        log("[D3D12Bridge] no shared texture for %ux%u format %d: 0x%08X", w, h, static_cast<int>(fmt),
            static_cast<unsigned>(hr));
        return nullptr;
    }
    extras.push_back(std::move(e));
    return &extras.back();
}

bool D3D12Bridge::ShareAndRead(ID3D11Texture2D* src, UINT slice, std::vector<uint8_t>* out, UINT* rowBytes,
                               DXGI_FORMAT* sharedFormat)
{
    if (!m->ready || !src || !out) return false;
    D3D11_TEXTURE2D_DESC d{};
    src->GetDesc(&d);
    if (d.SampleDesc.Count != 1 || slice >= d.ArraySize) return false;
    bool viaCompute = false;
    const DXGI_FORMAT fmt = CrossingFormat(d.Format, &viaCompute);
    if (viaCompute && d.ArraySize != 1) return false;   // the depth copy shader reads a plain 2D texture
    Impl::Extra* e = m->ExtraFor(d.Width, d.Height, fmt, viaCompute);
    if (!e) return false;
    if (sharedFormat) *sharedFormat = fmt;

    auto* c = m->ctx11.Get();
    if (viaCompute) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        ComPtr<ID3D11ShaderResourceView> srv;
        if (FAILED(m->dev11->CreateShaderResourceView(src, &sd, &srv))) return false;
        ComPtr<ID3D11ComputeShader> oldCS;
        ComPtr<ID3D11ShaderResourceView> oldSRV;
        ComPtr<ID3D11UnorderedAccessView> oldUAV;
        c->CSGetShader(&oldCS, nullptr, nullptr);
        c->CSGetShaderResources(0, 1, &oldSRV);
        c->CSGetUnorderedAccessViews(0, 1, &oldUAV);
        ID3D11ShaderResourceView* s0 = srv.Get();
        ID3D11UnorderedAccessView* u0 = e->uav11.Get();
        c->CSSetShader(m->depthCS.Get(), nullptr, 0);
        c->CSSetShaderResources(0, 1, &s0);
        c->CSSetUnorderedAccessViews(0, 1, &u0, nullptr);
        c->Dispatch((d.Width + 7) / 8, (d.Height + 7) / 8, 1);
        s0 = oldSRV.Get();
        u0 = oldUAV.Get();
        const UINT keep = ~0u;
        c->CSSetUnorderedAccessViews(0, 1, &u0, &keep);
        c->CSSetShaderResources(0, 1, &s0);
        c->CSSetShader(oldCS.Get(), nullptr, 0);
    } else {
        c->CopySubresourceRegion(e->tex.tex11.Get(), 0, 0, 0, 0, src, D3D11CalcSubresource(0, slice, d.MipLevels),
                                 nullptr);
    }
    // D3D11 signals after its copy; the D3D12 read waits for that value.
    const uint64_t v = m->nextValue++;
    if (FAILED(c->Signal(m->fence11.Get(), v))) return false;
    c->Flush();
    return m->ReadTexture12(e->tex.res12.Get(), v, out, rowBytes);
}

} // namespace raw::bridge
