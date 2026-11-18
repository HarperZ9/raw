// Shared helpers for the D3D11 test scenes: shader compile, resource and state
// creation, readback, a PRNG, matrices, a cube mesh, and image tools.
// Creation helpers log the failure to stderr and return a null pointer.
#pragma once
#include "scene.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>

namespace txtest {
using Microsoft::WRL::ComPtr;

bool Fail(const char* scene, const char* what, HRESULT hr = S_OK);
#define TX_HR(scene, expr) \
    do { HRESULT hr_ = (expr); if (FAILED(hr_)) return ::txtest::Fail(scene, #expr, hr_); } while (0)
#define TX_REQ(scene, cond) \
    do { if (!(cond)) return ::txtest::Fail(scene, #cond); } while (0)

// ---- shaders ----
extern const char* const kFullscreenVS;   // entry "vs": SV_VertexID triangle, TEXCOORD0 uv
ComPtr<ID3DBlob> Compile(const char* src, const char* entry, const char* target);
ComPtr<ID3D11VertexShader> MakeVS(ID3D11Device* dev, const char* src, const char* entry,
                                  ID3DBlob** blobOut = nullptr);
ComPtr<ID3D11PixelShader> MakePS(ID3D11Device* dev, const char* src, const char* entry);
ComPtr<ID3D11ComputeShader> MakeCS(ID3D11Device* dev, const char* src, const char* entry);
ComPtr<ID3D11InputLayout> MakeLayout(ID3D11Device* dev, const D3D11_INPUT_ELEMENT_DESC* el,
                                     UINT count, ID3DBlob* vsBlob);

// ---- buffers and textures ----
ComPtr<ID3D11Buffer> MakeBuffer(ID3D11Device* dev, UINT bytes, UINT bind, const void* init,
                                D3D11_USAGE usage = D3D11_USAGE_DEFAULT, UINT misc = 0,
                                UINT stride = 0);
struct TexDesc {
    UINT w = 1, h = 1;
    DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    UINT bind = 0, mips = 1, samples = 1, quality = 0, misc = 0;
};
ComPtr<ID3D11Texture2D> MakeTex(ID3D11Device* dev, const TexDesc& d,
                                const D3D11_SUBRESOURCE_DATA* init = nullptr);
ComPtr<ID3D11RenderTargetView> MakeRTV(ID3D11Device* dev, ID3D11Texture2D* t);
// fmt UNKNOWN: the texture's own format and every mip.
ComPtr<ID3D11ShaderResourceView> MakeSRV(ID3D11Device* dev, ID3D11Texture2D* t,
                                         DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN);
ComPtr<ID3D11DepthStencilView> MakeDSV(ID3D11Device* dev, ID3D11Texture2D* t,
                                       DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN);
ComPtr<ID3D11UnorderedAccessView> MakeUAV(ID3D11Device* dev, ID3D11Texture2D* t);

struct Target {
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11ShaderResourceView> srv;
};
bool MakeTarget(ID3D11Device* dev, UINT w, UINT h, DXGI_FORMAT fmt, Target& out);
struct Depth {
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11DepthStencilView> dsv;
};
bool MakeDepth(ID3D11Device* dev, UINT w, UINT h, Depth& out);   // D24_UNORM_S8_UINT

// ---- state ----
ComPtr<ID3D11RasterizerState> MakeRaster(ID3D11Device* dev, D3D11_CULL_MODE cull,
                                         int depthBias = 0, float slopeBias = 0.0f,
                                         bool scissor = false, bool msaa = false);
ComPtr<ID3D11DepthStencilState> MakeDepthState(ID3D11Device* dev, bool enable, bool write,
                                               D3D11_COMPARISON_FUNC func);
struct BlendSpec {
    bool enable = false;
    D3D11_BLEND src = D3D11_BLEND_ONE, dst = D3D11_BLEND_ZERO;
    D3D11_BLEND srcA = D3D11_BLEND_ONE, dstA = D3D11_BLEND_ZERO;
    UINT8 mask = D3D11_COLOR_WRITE_ENABLE_ALL;
};
ComPtr<ID3D11BlendState> MakeBlend(ID3D11Device* dev, const BlendSpec& s);
ComPtr<ID3D11SamplerState> MakeSampler(ID3D11Device* dev, D3D11_FILTER filter,
                                       D3D11_TEXTURE_ADDRESS_MODE addr,
                                       D3D11_COMPARISON_FUNC cmp = D3D11_COMPARISON_NEVER,
                                       float border = 0.0f);
void SetViewport(ID3D11DeviceContext* ctx, float w, float h, float x = 0.0f, float y = 0.0f);
void BindVB(ID3D11DeviceContext* ctx, ID3D11Buffer* vb, UINT stride);   // slot 0, triangle list
// Polls GetData (flag 0) until S_OK; false after 5 s or on an error.
bool WaitQuery(ID3D11DeviceContext* ctx, ID3D11Asynchronous* q, void* data, UINT size);

// ---- position + colour pipeline (clip-space pass-through) ----
struct VtxPC { float x, y, z, r, g, b, a; };
struct Rgba { float r, g, b, a; };
struct ColorPipe {
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
};
bool MakeColorPipe(ID3D11Device* dev, ColorPipe& out);
void BindColorPipe(ID3D11DeviceContext* ctx, const ColorPipe& p);
// Appends 6 vertices (clockwise) for a rectangle given in pixels of a w x h target.
void PushQuadPx(std::vector<VtxPC>& v, float w, float h, float x0, float y0, float x1,
                float y1, float z, Rgba c);
VtxPC VertPx(float w, float h, float x, float y, float z, Rgba c);

// ---- mesh: unit cube (-1..1), 24 vertices, 36 indices, clockwise front faces ----
struct VtxPNT { float p[3], n[3], uv[2]; };
void MakeCube(std::vector<VtxPNT>& verts, std::vector<uint16_t>& idx);
extern const D3D11_INPUT_ELEMENT_DESC kPNTLayout[3];

// ---- math: row vectors, row-major storage; HLSL side uses row_major and mul(v, M) ----
struct Vec3 { float x, y, z; };
struct Mat4 { float m[4][4]; };
Mat4 Identity();
Mat4 Mul(const Mat4& a, const Mat4& b);
Mat4 Translate(float x, float y, float z);
Mat4 Scale(float x, float y, float z);
Mat4 RotX(float rad);
Mat4 RotY(float rad);
Mat4 LookAtLH(Vec3 eye, Vec3 at, Vec3 up);
Mat4 PerspLH(float fovY, float aspect, float zn, float zf);
Mat4 OrthoLH(float w, float h, float zn, float zf);

// ---- deterministic PRNG (xorshift32) ----
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float unit() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
};

// ---- images ----
inline constexpr float kClear[4] = {0.2f, 0.4f, 0.6f, 1.0f};   // bytes 51,102,153,255
bool Readback(SceneCtx& c, ID3D11Texture2D* rgba8Tex, Image& out);
bool SaveBMP(const char* path, const Image& img);
uint64_t HashImage(const Image& img);
int MaxDiff(const Image& a, const Image& b);   // -1 when the sizes differ
const uint8_t* Px(const Image& img, uint32_t x, uint32_t y);
bool PxNear(const Image& img, uint32_t x, uint32_t y, int r, int g, int b, int tol = 1);
bool IsClear(const Image& img, uint32_t x, uint32_t y);
uint32_t CountNotClear(const Image& img);

}  // namespace txtest
