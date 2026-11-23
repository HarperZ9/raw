// BridgeHook captures: the backbuffer as a BMP, the gameplay capture set, and
// the check that a game buffer read on D3D12 equals its D3D11 bytes.
// Original work, MIT.
#include "BridgeHookState.h"
#include "CBDirtyTracker.h"
#include "FrameLedger.h"
#include "ProxyAPI.h"
#include "ProxyLog.h"
#include "WeatherDrive.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <direct.h>
#include <vector>

namespace SB::Proxy::hook
{
using raw::bridge::Shared;

void CaptureBackbuffer(const std::string& out)
{
    auto* pi = PG_GetProxyInterface();
    if (!pi || !pi->swapChain || !pi->device || !pi->context) return;
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(pi->swapChain->GetBuffer(0, IID_PPV_ARGS(&bb))) || !bb) return;
    D3D11_TEXTURE2D_DESC d{};
    bb->GetDesc(&d);
    const bool bgra = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    const bool rgba = d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    ID3D11Texture2D* st = nullptr;
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
    const HRESULT chr = (bgra || rgba) ? pi->device->CreateTexture2D(&d, nullptr, &st) : E_INVALIDARG;
    if (SUCCEEDED(chr)) {
        ID3D11DeviceContext* c = pi->context;   // the swap chain device's own context
        c->CopyResource(st, bb);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(c->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
            FILE* f = nullptr;
            if (fopen_s(&f, out.c_str(), "wb") == 0 && f) {
                const uint32_t img = d.Width * d.Height * 4;
                uint8_t hdr[54]{'B', 'M'};
                auto put = [&](int at, uint32_t v) { memcpy(hdr + at, &v, 4); };
                put(2, 54 + img); put(10, 54); put(14, 40); put(18, d.Width);
                put(22, static_cast<uint32_t>(-static_cast<int32_t>(d.Height)));   // top-down
                hdr[26] = 1; hdr[28] = 32; put(34, img);
                fwrite(hdr, 1, 54, f);
                std::string row(d.Width * 4, char(0));
                for (UINT y = 0; y < d.Height; ++y) {
                    const uint8_t* src = static_cast<const uint8_t*>(m.pData) + y * m.RowPitch;
                    for (UINT x = 0; x < d.Width; ++x) {   // BMP wants B,G,R,A
                        const uint8_t* p = src + x * 4;
                        row[x * 4 + 0] = static_cast<char>(bgra ? p[0] : p[2]);
                        row[x * 4 + 1] = static_cast<char>(p[1]);
                        row[x * 4 + 2] = static_cast<char>(bgra ? p[2] : p[0]);
                        row[x * 4 + 3] = static_cast<char>(p[3]);
                    }
                    fwrite(row.data(), 1, row.size(), f);
                }
                fclose(f);
                Log("[D3D12Bridge] captured backbuffer %ux%u to %s", d.Width, d.Height, out.c_str());
            }
            c->Unmap(st, 0);
        }
        st->Release();
    }
    bb->Release();
}

static void WriteRows(const std::string& path, UINT w, UINT h, UINT rowBytes, const void* data, size_t bytes)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) return;
    const uint32_t hdr[3] = {w, h, rowBytes};
    fwrite(hdr, 4, 3, f);
    fwrite(data, 1, bytes, f);
    fclose(f);
}

void CaptureStep(uint64_t bridgeFrames)
{
    Config& c = Cfg();
    static float savedMode = 1.0f;
    if (c.captureAfter <= 0 || c.captureDir.empty()) return;
    const uint64_t at = static_cast<uint64_t>(c.captureAfter);
    if (bridgeFrames == at) {
        _mkdir(c.captureDir.c_str());
        CaptureBackbuffer(c.captureDir + "\\frame_pass_on.bmp");
        const raw::bridge::BridgeConfig& bc = Bridge()->Config();
        std::vector<float> vis;
        if (Bridge()->ReadEngineVisibility(&vis))
            WriteRows(c.captureDir + "\\engine_visibility.bin", bc.width, bc.height, bc.width * 4, vis.data(), vis.size() * 4);
        static const char* kNames[5] = {"color_in", "depth", "normal", "color_out", "motion"};
        for (UINT k = 0; k < 5; ++k) {
            std::vector<uint8_t> bytes;
            UINT row = 0;
            if (Bridge()->ReadShared(static_cast<Shared>(k), &bytes, &row))
                WriteRows(c.captureDir + "\\d3d12_" + kNames[k] + ".bin", bc.width, bc.height, row, bytes.data(), bytes.size());
        }
        savedMode = c.k.mode;
        c.k.mode = 0.0f;   // the next frames pass through, for the comparison image
        Log("[D3D12Bridge] capture set: frame with the pass and the D3D12 reads written to %s", c.captureDir.c_str());
    } else if (bridgeFrames == at + 6) {
        CaptureBackbuffer(c.captureDir + "\\frame_pass_off.bmp");
        c.k.mode = savedMode;
        Log("[D3D12Bridge] capture set done");
    }
}

// FrameLedger's final dump of a texture: the same slice, shared and read on
// D3D12, must hold the same bytes (D24S8 crosses as float depth).
static int ShareCheck(ID3D11Texture2D* tex, UINT slice, const void* rows, UINT rowPitch, UINT rowBytes, UINT height)
{
    raw::bridge::D3D12Bridge* b = Bridge();
    if (!b || !b->Ready()) return 0;
    D3D11_TEXTURE2D_DESC d{};
    tex->GetDesc(&d);
    std::vector<uint8_t> got;
    UINT gotRow = 0;
    DXGI_FORMAT crossed = DXGI_FORMAT_UNKNOWN;
    if (!b->ShareAndRead(tex, slice, &got, &gotRow, &crossed)) return 3;
    const bool d24 = d.Format == DXGI_FORMAT_R24G8_TYPELESS || d.Format == DXGI_FORMAT_D24_UNORM_S8_UINT;
    const bool d32 = d.Format == DXGI_FORMAT_R32G8X24_TYPELESS || d.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    if (got.size() < size_t(gotRow) * height) return 3;
    for (UINT y = 0; y < height; ++y) {
        const uint8_t* a = static_cast<const uint8_t*>(rows) + size_t(y) * rowPitch;
        const uint8_t* g = got.data() + size_t(y) * gotRow;
        if (d24) {
            const uint32_t* u = reinterpret_cast<const uint32_t*>(a);
            const float* f = reinterpret_cast<const float*>(g);
            for (UINT x = 0; x < d.Width; ++x)
                if (std::fabs(double(f[x]) - double(u[x] & 0xFFFFFF) / 16777215.0) > 6.0e-8) return 2;
        } else if (d32) {
            const float* fa = reinterpret_cast<const float*>(a);
            const float* fg = reinterpret_cast<const float*>(g);
            for (UINT x = 0; x < d.Width; ++x)
                if (std::fabs(double(fg[x]) - double(fa[x * 2])) > 1.0e-6) return 2;
        } else if (gotRow != rowBytes || memcmp(a, g, rowBytes) != 0) {
            return 2;
        }
    }
    return 1;
}

void InstallShareCheck() { g_ledgerShareCheck = &ShareCheck; }

// The game's view-to-clip matrix: the second matrix of its per-frame constant
// buffer (vertex slot 12), as the game last wrote it.
raw::bridge::CameraConstants ReadCamera(ID3D11DeviceContext* real)
{
    raw::bridge::CameraConstants cam;
    ID3D11Buffer* cb = nullptr;
    real->VSGetConstantBuffers(12, 1, &cb);
    if (!cb) return cam;
    const uint8_t* p = CBDirtyTracker::Get().GetShadowData(cb);
    const uint32_t size = CBDirtyTracker::Get().GetShadowSize(cb);
    cb->Release();
    if (!p || size < 128) return cam;
    memcpy(cam.proj, p + 64, 64);
    const float* m = cam.proj;
    // A perspective projection along +z: w = z, depth = m[10] + m[11] / z.
    cam.valid = m[14] == 1.0f && m[15] == 0.0f && m[0] > 0.0f && m[5] > 0.0f && m[10] > 1.0f && m[11] < 0.0f;
    if (!cam.valid) {
        // Direct NiCamera fallback from raw_engine.dll (PlayerCamera::Singleton) when b12 is non-projective.
        EngineCameraState ec;
        if (EngineCamera_Get(ec) && ec.fovDeg > 10.0f && ec.fovDeg < 170.0f) {
            const float halfFovRad = ec.fovDeg * 0.5f * 0.0174532925f;
            cam.aspect = 16.0f / 9.0f;
            cam.tanHalfFovY = std::tan(halfFovRad) / cam.aspect;
            cam.nearZ = 15.0f;
            cam.farZ = 400000.0f;
            cam.valid = true;
        }
        return cam;
    }
    cam.tanHalfFovY = 1.0f / m[5];
    cam.aspect = m[5] / m[0];
    cam.nearZ = -m[11] / m[10];
    cam.farZ = -m[11] / (m[10] - 1.0f);
    return cam;
}


} // namespace SB::Proxy::hook
