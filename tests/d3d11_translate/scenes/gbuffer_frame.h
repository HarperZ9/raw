// Scene 13 internals shared by gbuffer_frame.cpp (passes, checks) and
// gbuffer_frame_res.cpp (shaders, resources, states).
#pragma once
#include "../scene_util.h"

namespace txtest::gbuffer {

inline const char* const kName = "gbuffer_frame";

// Vertex ranges in the one vertex buffer (6 vertices a quad).
constexpr UINT kFirstB = 0, kFirstOthers = 6, kOthersCount = 18, kOpaqueCount = 24, kFirstLate = 24;

struct Res {
    Target out;                                     // R8G8B8A8_UNORM
    Target hdr, motion, normal, mask, lit, late;    // the six colour targets
    ComPtr<ID3D11Texture2D> depthTex, depthCopy;    // R24G8_TYPELESS
    ComPtr<ID3D11DepthStencilView> dsv, dsvReadOnly, dsvCopy;
    ComPtr<ID3D11ShaderResourceView> depthSrv;      // R24_UNORM_X8_TYPELESS
    ColorPipe pipe;
    ComPtr<ID3D11VertexShader> fsVs;
    ComPtr<ID3D11PixelShader> psOpaque, psLit, psComposite;
    ComPtr<ID3D11Buffer> vb, cb;
    ComPtr<ID3D11RasterizerState> rs;
    ComPtr<ID3D11DepthStencilState> dsPrepass, dsOpaque, dsLit, dsLate;
    ComPtr<ID3D11BlendState> blendMrt;
    ComPtr<ID3D11SamplerState> point;
};

bool Setup(SceneCtx& c, Res& r);

}  // namespace txtest::gbuffer
