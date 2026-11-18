// Scene 14 passes that fill the resources: cube faces, depth-array slices, the two
// compute-written volumes and the rendered mip pyramid.
#include "volumes_cubes_arrays.h"

namespace txtest::volumes {

void RenderCube(SceneCtx& c, const Res& r)
{
    SetViewport(c.ctx, static_cast<float>(kCubeSize), static_cast<float>(kCubeSize));
    c.ctx->OMSetDepthStencilState(r.dsLess.Get(), 0);
    BindColorPipe(c.ctx, r.pipe);
    BindVB(c.ctx, r.vbCube.Get(), sizeof(VtxPC));
    for (UINT f = 0; f < 6; ++f) {
        c.ctx->ClearRenderTargetView(r.cubeRtv[f].Get(), kFaceBase[f]);
        c.ctx->ClearDepthStencilView(r.cubeDepth.dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        c.ctx->OMSetRenderTargets(1, r.cubeRtv[f].GetAddressOf(), r.cubeDepth.dsv.Get());
        c.ctx->Draw(12, 12 * f);
    }
    c.ctx->OMSetRenderTargets(0, nullptr, nullptr);
}

void RenderDepthArray(SceneCtx& c, const Res& r)
{
    SetViewport(c.ctx, static_cast<float>(kArrSize), static_cast<float>(kArrSize));
    c.ctx->OMSetDepthStencilState(r.dsLess.Get(), 0);
    BindColorPipe(c.ctx, r.pipe);
    c.ctx->PSSetShader(nullptr, nullptr, 0);   // depth only
    BindVB(c.ctx, r.vbArr.Get(), sizeof(VtxPC));
    for (UINT s = 0; s < 2; ++s) {
        c.ctx->ClearDepthStencilView(r.arrDsv[s].Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        c.ctx->OMSetRenderTargets(0, nullptr, r.arrDsv[s].Get());
        c.ctx->Draw(12, 12 * s);
    }
    c.ctx->OMSetRenderTargets(0, nullptr, nullptr);
}

void RunVolumeCompute(SceneCtx& c, const Res& r)
{
    ID3D11UnorderedAccessView* noUav = nullptr;
    ID3D11ShaderResourceView* noSrv = nullptr;
    c.ctx->CSSetShader(r.csFill.Get(), nullptr, 0);
    c.ctx->CSSetUnorderedAccessViews(0, 1, r.uavA.GetAddressOf(), nullptr);
    c.ctx->Dispatch(kVolW / 8, kVolH / 8, kVolD / 8);
    // A stops being an output before it becomes an input.
    c.ctx->CSSetUnorderedAccessViews(0, 1, &noUav, nullptr);
    c.ctx->CSSetShader(r.csDerive.Get(), nullptr, 0);
    c.ctx->CSSetShaderResources(0, 1, r.volSrvA.GetAddressOf());
    c.ctx->CSSetUnorderedAccessViews(0, 1, r.uavB.GetAddressOf(), nullptr);
    c.ctx->Dispatch(kVolW / 8, kVolH / 8, kVolD / 8);
    c.ctx->CSSetUnorderedAccessViews(0, 1, &noUav, nullptr);
    c.ctx->CSSetShaderResources(0, 1, &noSrv);
    c.ctx->CSSetShader(nullptr, nullptr, 0);
}

// Mip 0 from a procedural pattern, then mip n from mip n-1: the same texture is read
// at one mip and written at the next.
void BuildMipPyramid(SceneCtx& c, const Res& r)
{
    ID3D11ShaderResourceView* noSrv = nullptr;
    c.ctx->OMSetDepthStencilState(nullptr, 0);
    c.ctx->IASetInputLayout(nullptr);
    c.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c.ctx->VSSetShader(r.fsVs.Get(), nullptr, 0);
    c.ctx->PSSetShader(r.psMipSeed.Get(), nullptr, 0);
    c.ctx->OMSetRenderTargets(1, r.mipRtv[0].GetAddressOf(), nullptr);
    SetViewport(c.ctx, static_cast<float>(kMipSize), static_cast<float>(kMipSize));
    c.ctx->Draw(3, 0);
    c.ctx->PSSetShader(r.psMipMax.Get(), nullptr, 0);
    for (UINT m = 1; m < kMips; ++m) {
        const float size = static_cast<float>(kMipSize >> m);
        c.ctx->OMSetRenderTargets(1, r.mipRtv[m].GetAddressOf(), nullptr);
        c.ctx->PSSetShaderResources(0, 1, r.mipSrv[m - 1].GetAddressOf());
        SetViewport(c.ctx, size, size);
        c.ctx->Draw(3, 0);
    }
    c.ctx->OMSetRenderTargets(0, nullptr, nullptr);
    c.ctx->PSSetShaderResources(0, 1, &noSrv);
}

}  // namespace txtest::volumes
