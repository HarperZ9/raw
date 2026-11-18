// Scene 14 internals shared by volumes_cubes_arrays.cpp (composite, checks),
// volumes_cubes_arrays_res.cpp (textures, views, buffers),
// volumes_cubes_arrays_shaders.cpp and volumes_cubes_arrays_passes.cpp.
#pragma once
#include "../scene_util.h"

namespace txtest::volumes {

inline const char* const kName = "volumes_cubes_arrays";
constexpr UINT kCubeSize = 64, kArrSize = 128, kVolW = 32, kVolH = 32, kVolD = 16;
constexpr UINT kMipSize = 64, kMips = 7, kInstances = 5;
// Clear colour of each cube face (HDR; shown halved).
inline constexpr float kFaceBase[6][4] = {
    {1.6f, 0.4f, 0.4f, 1}, {0.4f, 1.6f, 0.4f, 1}, {0.4f, 0.4f, 1.6f, 1},
    {1.6f, 1.6f, 0.4f, 1}, {1.6f, 0.4f, 1.6f, 1}, {0.4f, 1.6f, 1.6f, 1},
};

struct Res {
    Target out;
    // cube map: six faces of one R16G16B16A16_FLOAT texture, one shared depth buffer
    ComPtr<ID3D11Texture2D> cubeTex;
    ComPtr<ID3D11RenderTargetView> cubeRtv[6];
    ComPtr<ID3D11ShaderResourceView> cubeSrv;
    Depth cubeDepth;
    // depth array: R16_TYPELESS, two slices
    ComPtr<ID3D11Texture2D> arrTex;
    ComPtr<ID3D11DepthStencilView> arrDsv[2];
    ComPtr<ID3D11ShaderResourceView> arrSrv;
    // volumes: R16_FLOAT 3D, written by compute
    ComPtr<ID3D11Texture3D> volA, volB;
    ComPtr<ID3D11UnorderedAccessView> uavA, uavB;
    ComPtr<ID3D11ShaderResourceView> volSrvA, volSrvB;
    // mip pyramid: R32_FLOAT, each mip rendered from the one above
    ComPtr<ID3D11Texture2D> mipTex;
    ComPtr<ID3D11RenderTargetView> mipRtv[kMips];
    ComPtr<ID3D11ShaderResourceView> mipSrv[kMips], mipSrvAll;
    // shaders
    ColorPipe pipe;
    ComPtr<ID3D11VertexShader> fsVs, vsInst;
    ComPtr<ID3D11InputLayout> layoutInst;
    ComPtr<ID3D11PixelShader> psMipSeed, psMipMax, psCube, psArr, psVol, psMip;
    ComPtr<ID3D11ComputeShader> csFill, csDerive;
    // buffers and state
    ComPtr<ID3D11Buffer> vbCube, vbArr, vbInst, args, cb;
    ComPtr<ID3D11SamplerState> point, linear;
    ComPtr<ID3D11RasterizerState> rs;
    ComPtr<ID3D11DepthStencilState> dsLess;
};

bool MakeResources(SceneCtx& c, Res& r);   // _res.cpp
bool MakeShaders(SceneCtx& c, Res& r);     // _shaders.cpp
void RenderCube(SceneCtx& c, const Res& r);        // _passes.cpp
void RenderDepthArray(SceneCtx& c, const Res& r);
void RunVolumeCompute(SceneCtx& c, const Res& r);
void BuildMipPyramid(SceneCtx& c, const Res& r);

}  // namespace txtest::volumes
