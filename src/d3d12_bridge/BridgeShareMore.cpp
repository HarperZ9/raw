// The two buffers a plain copy cannot carry across the bridge: the stencil
// byte of a D24S8 buffer and 3D textures. Each goes through a small compute
// copy on the D3D11 queue into a shared 2D texture, then a D3D12 read proves
// what crossed. Original work, MIT.
#include "BridgeImpl.h"

#include "StencilCopy_CS.h"
#include "VolumeCopy_CS.h"

#include <cmath>

namespace raw::bridge
{

bool D3D12Bridge::Impl::ComputeCopyAndRead(ID3D11ComputeShader* cs, ID3D11ShaderResourceView* src, Extra* e,
                                           std::vector<uint8_t>* out, UINT* rowBytes)
{
    auto* c = ctx11.Get();
    // The proxy's state cache covers graphics stages only, so the three compute bindings are put back.
    ComPtr<ID3D11ComputeShader> oldCS;
    ComPtr<ID3D11ShaderResourceView> oldSRV;
    ComPtr<ID3D11UnorderedAccessView> oldUAV;
    c->CSGetShader(&oldCS, nullptr, nullptr);
    c->CSGetShaderResources(0, 1, &oldSRV);
    c->CSGetUnorderedAccessViews(0, 1, &oldUAV);
    ID3D11UnorderedAccessView* u0 = e->uav11.Get();
    c->CSSetShader(cs, nullptr, 0);
    c->CSSetShaderResources(0, 1, &src);
    c->CSSetUnorderedAccessViews(0, 1, &u0, nullptr);
    c->Dispatch((e->w + 7) / 8, (e->h + 7) / 8, 1);
    ID3D11ShaderResourceView* s0 = oldSRV.Get();
    u0 = oldUAV.Get();
    const UINT keep = ~0u;
    c->CSSetUnorderedAccessViews(0, 1, &u0, &keep);
    c->CSSetShaderResources(0, 1, &s0);
    c->CSSetShader(oldCS.Get(), nullptr, 0);
    // D3D11 signals after its copy; the D3D12 read waits for that value.
    const uint64_t v = nextValue++;
    if (FAILED(ctx4->Signal(fence11.Get(), v))) return false;
    c->Flush();
    return ReadTexture12(e->tex.res12.Get(), v, out, rowBytes);
}

bool D3D12Bridge::ShareStencilAndRead(ID3D11Texture2D* depthStencil, std::vector<uint8_t>* out, UINT* rowBytes)
{
    if (!m->ready || !depthStencil || !out) return false;
    D3D11_TEXTURE2D_DESC d{};
    depthStencil->GetDesc(&d);
    const bool is32 = d.Format == DXGI_FORMAT_R32G8X24_TYPELESS;
    if ((d.Format != DXGI_FORMAT_R24G8_TYPELESS && !is32) || d.SampleDesc.Count != 1 || d.ArraySize != 1) return false;
    if (!m->stencilCS && FAILED(m->dev11->CreateComputeShader(g_StencilCopyCS, sizeof g_StencilCopyCS, nullptr, &m->stencilCS)))
        return false;
    Impl::Extra* e = m->ExtraFor(d.Width, d.Height, DXGI_FORMAT_R8_UINT, true);
    if (!e) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = is32 ? DXGI_FORMAT_X32_TYPELESS_G8X24_UINT : DXGI_FORMAT_X24_TYPELESS_G8_UINT;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(m->dev11->CreateShaderResourceView(depthStencil, &sd, &srv))) return false;
    return m->ComputeCopyAndRead(m->stencilCS.Get(), srv.Get(), e, out, rowBytes);
}

bool D3D12Bridge::ShareVolumeAndRead(ID3D11Texture3D* volume, std::vector<uint8_t>* out, UINT* rowBytes, UINT* columns,
                                     DXGI_FORMAT* sharedFormat)
{
    if (!m->ready || !volume || !out) return false;
    D3D11_TEXTURE3D_DESC d{};
    volume->GetDesc(&d);
    DXGI_FORMAT fmt = d.Format;
    if (fmt == DXGI_FORMAT_R16_TYPELESS) fmt = DXGI_FORMAT_R16_FLOAT;
    if (fmt == DXGI_FORMAT_R16G16B16A16_TYPELESS) fmt = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (!(d.BindFlags & D3D11_BIND_SHADER_RESOURCE)) return false;
    if (!m->volumeCS && FAILED(m->dev11->CreateComputeShader(g_VolumeCopyCS, sizeof g_VolumeCopyCS, nullptr, &m->volumeCS)))
        return false;
    const UINT cols = static_cast<UINT>(std::ceil(std::sqrt(static_cast<double>(d.Depth))));
    const UINT rows = (d.Depth + cols - 1) / cols;
    Impl::Extra* e = m->ExtraFor(d.Width * cols, d.Height * rows, fmt, true);
    if (!e) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = fmt;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
    sd.Texture3D.MipLevels = 1;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(m->dev11->CreateShaderResourceView(volume, &sd, &srv))) return false;
    if (columns) *columns = cols;
    if (sharedFormat) *sharedFormat = fmt;
    return m->ComputeCopyAndRead(m->volumeCS.Get(), srv.Get(), e, out, rowBytes);
}

} // namespace raw::bridge
