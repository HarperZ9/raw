#pragma once
// Scene for d3d11_record_selftest: a small animated frame that exercises the
// recorder. Every frame depends on what the previous one left behind: bound
// pipeline state, the depth buffer, a history texture and a blend state.
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <vector>

namespace selftest
{
using Microsoft::WRL::ComPtr;

constexpr UINT kSize = 256;     // backbuffer edge
constexpr UINT kHalf = 128;     // offscreen target and history edge

struct Constants { float rect[4]; float weights[4]; float tint[4]; float misc[4]; };

// Creates a WARP device at feature level 11.0. False on failure.
bool CreateWarp(ComPtr<ID3D11Device>* dev, ComPtr<ID3D11DeviceContext>* ctx);

// Tightly packed bytes of subresource 0 of a 2D texture, through a staging copy.
std::vector<uint8_t> ReadTexture(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* tex);

class Scene
{
public:
    // dev and ctx are the objects the "application" uses: the recorder wrappers.
    bool Init(ID3D11Device* dev, ID3D11DeviceContext* ctx);
    void Frame(int frame);

    ID3D11Texture2D* Backbuffer() const { return m_bb.Get(); }
    ID3D11Texture2D* History() const { return m_hist.Get(); }
    ID3D11Texture2D* Depth() const { return m_depth.Get(); }

private:
    bool CreateShaders();
    bool CreateGeometry();
    bool CreateTargets();
    bool CreateTextures();
    bool CreateStates();
    void BindStatic();
    void ComputePass(int frame);
    void UpdateChecker(int frame);
    void OffscreenPass(int frame);
    void MainPass(int frame);
    bool MakeStamp(int frame, ComPtr<ID3D11ShaderResourceView>* srv);
    void SetConstants(const Constants& c);
    void Quad(float x, float y, float sx, float sy, float z, float w0, float w1, float w2, float alpha,
              float r, float g, float b);

    ID3D11Device*        m_dev = nullptr;
    ID3D11DeviceContext* m_ctx = nullptr;
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader> m_ps;
    ComPtr<ID3D11ComputeShader> m_cs;
    ComPtr<ID3D11InputLayout> m_layout;
    ComPtr<ID3D11Buffer> m_vb, m_ib, m_cb;
    ComPtr<ID3D11Texture2D> m_bb, m_depth, m_off, m_checker, m_comp, m_hist;
    ComPtr<ID3D11RenderTargetView> m_bbRtv, m_offRtv;
    ComPtr<ID3D11DepthStencilView> m_dsv;
    ComPtr<ID3D11ShaderResourceView> m_offSrv, m_checkerSrv, m_compSrv, m_histSrv;
    ComPtr<ID3D11UnorderedAccessView> m_compUav;
    ComPtr<ID3D11BlendState> m_blendAlpha, m_blendOpaque;
    ComPtr<ID3D11DepthStencilState> m_dss;
    ComPtr<ID3D11RasterizerState> m_rs;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11Query> m_query;
};

// ---- checks (selftest_checks.cpp) ---------------------------------------------
struct Capture
{
    std::wstring path;                  // the captured file
    std::vector<uint8_t> reference;     // frame-3 backbuffer bytes, rendered directly
    uint64_t backbufferId = 0, historyId = 0, depthId = 0;
};

bool CheckReplay(const Capture& cap);           // (a) bit-identical on a fresh device
bool CheckReplayStaging(const Capture& cap);    // (a2) same, contents restored by staging copies
bool CheckCorruptContent(const Capture& cap);   // (b) one content byte changed: image differs
bool CheckZeroDepth(const Capture& cap);        // (b2) depth contents zeroed: image differs
bool CheckSkipInitialState(const Capture& cap); // (c) no initial state: image differs
bool CheckTruncated(const Capture& cap);        // (d) truncated files: clean error
bool CheckDump(const Capture& cap);             // (e) listing by the library and by the tool
void PrintFileStats(const Capture& cap);
} // namespace selftest
