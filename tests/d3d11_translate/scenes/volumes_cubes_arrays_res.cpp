// Scene 14 resources: the cube map and its face views, the depth array and its slice
// views, two 3D volumes, the mip pyramid with one view per mip, geometry and the
// indirect-draw arguments.
#include "volumes_cubes_arrays.h"

namespace txtest::volumes {
namespace {
ComPtr<ID3D11Texture2D> MakeArrayTex(ID3D11Device* dev, UINT size, UINT slices, DXGI_FORMAT fmt,
                                     UINT bind, UINT misc)
{
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = size; td.MipLevels = 1; td.ArraySize = slices;
    td.Format = fmt; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = bind; td.MiscFlags = misc;
    ComPtr<ID3D11Texture2D> t;
    const HRESULT hr = dev->CreateTexture2D(&td, nullptr, &t);
    if (FAILED(hr)) { Fail(kName, "CreateTexture2D(array)", hr); return nullptr; }
    return t;
}

bool MakeCube(SceneCtx& c, Res& r)
{
    r.cubeTex = MakeArrayTex(c.dev, kCubeSize, 6, DXGI_FORMAT_R16G16B16A16_FLOAT,
                             D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                             D3D11_RESOURCE_MISC_TEXTURECUBE);
    TX_REQ(kName, r.cubeTex);
    for (UINT f = 0; f < 6; ++f) {
        D3D11_RENDER_TARGET_VIEW_DESC rv{};
        rv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        rv.Texture2DArray.FirstArraySlice = f;
        rv.Texture2DArray.ArraySize = 1;
        TX_HR(kName, c.dev->CreateRenderTargetView(r.cubeTex.Get(), &rv, &r.cubeRtv[f]));
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    sv.TextureCube.MipLevels = 1;
    TX_HR(kName, c.dev->CreateShaderResourceView(r.cubeTex.Get(), &sv, &r.cubeSrv));
    TX_REQ(kName, MakeDepth(c.dev, kCubeSize, kCubeSize, r.cubeDepth));
    return true;
}

bool MakeDepthArray(SceneCtx& c, Res& r)
{
    r.arrTex = MakeArrayTex(c.dev, kArrSize, 2, DXGI_FORMAT_R16_TYPELESS,
                            D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0);
    TX_REQ(kName, r.arrTex);
    for (UINT s = 0; s < 2; ++s) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dv{};
        dv.Format = DXGI_FORMAT_D16_UNORM;
        dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        dv.Texture2DArray.FirstArraySlice = s;
        dv.Texture2DArray.ArraySize = 1;
        TX_HR(kName, c.dev->CreateDepthStencilView(r.arrTex.Get(), &dv, &r.arrDsv[s]));
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R16_UNORM;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    sv.Texture2DArray.MipLevels = 1;
    sv.Texture2DArray.ArraySize = 2;
    TX_HR(kName, c.dev->CreateShaderResourceView(r.arrTex.Get(), &sv, &r.arrSrv));
    return true;
}

bool MakeVolume(SceneCtx& c, ComPtr<ID3D11Texture3D>& tex, ComPtr<ID3D11UnorderedAccessView>& uav,
                ComPtr<ID3D11ShaderResourceView>& srv)
{
    D3D11_TEXTURE3D_DESC td{};
    td.Width = kVolW; td.Height = kVolH; td.Depth = kVolD; td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R16_FLOAT;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    TX_HR(kName, c.dev->CreateTexture3D(&td, nullptr, &tex));
    D3D11_UNORDERED_ACCESS_VIEW_DESC uv{};
    uv.Format = DXGI_FORMAT_R16_FLOAT;
    uv.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
    uv.Texture3D.WSize = kVolD;
    TX_HR(kName, c.dev->CreateUnorderedAccessView(tex.Get(), &uv, &uav));
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R16_FLOAT;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
    sv.Texture3D.MipLevels = 1;
    TX_HR(kName, c.dev->CreateShaderResourceView(tex.Get(), &sv, &srv));
    return true;
}

bool MakeMipPyramid(SceneCtx& c, Res& r)
{
    TexDesc d;
    d.w = d.h = kMipSize; d.mips = kMips; d.fmt = DXGI_FORMAT_R32_FLOAT;
    d.bind = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    r.mipTex = MakeTex(c.dev, d);
    r.mipSrvAll = MakeSRV(c.dev, r.mipTex.Get());
    TX_REQ(kName, r.mipTex && r.mipSrvAll);
    for (UINT m = 0; m < kMips; ++m) {
        D3D11_RENDER_TARGET_VIEW_DESC rv{};
        rv.Format = DXGI_FORMAT_R32_FLOAT;
        rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rv.Texture2D.MipSlice = m;
        TX_HR(kName, c.dev->CreateRenderTargetView(r.mipTex.Get(), &rv, &r.mipRtv[m]));
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = DXGI_FORMAT_R32_FLOAT;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MostDetailedMip = m;
        sv.Texture2D.MipLevels = 1;
        TX_HR(kName, c.dev->CreateShaderResourceView(r.mipTex.Get(), &sv, &r.mipSrv[m]));
    }
    return true;
}

bool MakeGeometry(SceneCtx& c, Res& r)
{
    // Cube faces (64 px): a white marker at the top left whose length names the face,
    // then a black quad behind it. 12 vertices a face.
    std::vector<VtxPC> v;
    const float cs = static_cast<float>(kCubeSize), as = static_cast<float>(kArrSize);
    for (int f = 0; f < 6; ++f) {
        const float k = static_cast<float>(f);
        PushQuadPx(v, cs, cs, 4, 4, 20 + 4 * k, 16, 0.3f, {2, 2, 2, 1});
        PushQuadPx(v, cs, cs, 12, 10, 40, 40 + 2 * k, 0.6f, {0, 0, 0, 1});
    }
    r.vbCube = MakeBuffer(c.dev, static_cast<UINT>(v.size() * sizeof(VtxPC)), D3D11_BIND_VERTEX_BUFFER, v.data());
    // Depth array (128 px): 12 vertices a slice; the nearer quad is drawn first.
    v.clear();
    PushQuadPx(v, as, as, 16, 16, 80, 80, 0.2f, {0, 0, 0, 1});
    PushQuadPx(v, as, as, 48, 48, 112, 112, 0.6f, {0, 0, 0, 1});
    PushQuadPx(v, as, as, 40, 8, 88, 120, 0.4f, {0, 0, 0, 1});
    PushQuadPx(v, as, as, 8, 40, 120, 88, 0.8f, {0, 0, 0, 1});
    r.vbArr = MakeBuffer(c.dev, static_cast<UINT>(v.size() * sizeof(VtxPC)), D3D11_BIND_VERTEX_BUFFER, v.data());
    const float quad[12] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
    r.vbInst = MakeBuffer(c.dev, sizeof(quad), D3D11_BIND_VERTEX_BUFFER, quad);
    // Two argument records; the draw uses the second one (byte offset 16).
    const UINT args[8] = {3, 1, 0, 0, 6, kInstances, 0, 0};
    r.args = MakeBuffer(c.dev, sizeof(args), 0, args, D3D11_USAGE_DEFAULT,
                        D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS);
    r.cb = MakeBuffer(c.dev, 16, D3D11_BIND_CONSTANT_BUFFER, nullptr);
    TX_REQ(kName, r.vbCube && r.vbArr && r.vbInst && r.args && r.cb);
    return true;
}
}  // namespace

bool MakeResources(SceneCtx& c, Res& r)
{
    TX_REQ(kName, MakeTarget(c.dev, c.width, c.height, DXGI_FORMAT_R8G8B8A8_UNORM, r.out));
    if (!MakeCube(c, r) || !MakeDepthArray(c, r) || !MakeMipPyramid(c, r)) return false;
    if (!MakeVolume(c, r.volA, r.uavA, r.volSrvA) || !MakeVolume(c, r.volB, r.uavB, r.volSrvB)) return false;
    if (!MakeGeometry(c, r)) return false;
    r.point = MakeSampler(c.dev, D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
    r.linear = MakeSampler(c.dev, D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
    r.rs = MakeRaster(c.dev, D3D11_CULL_NONE);
    r.dsLess = MakeDepthState(c.dev, true, true, D3D11_COMPARISON_LESS);
    TX_REQ(kName, r.point && r.linear && r.rs && r.dsLess);
    return true;
}

}  // namespace txtest::volumes
