// d3d11_record_selftest: records frame 3 of a small animated scene through the
// recorder wrappers on WARP, then proves the file by replaying it.
//   d3d11_record_selftest [output directory] [--keep]
// Exit code 0 only when every check passes.
#include "selftest_scene.h"
#include "rec_api.h"
#include "rec_util.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace selftest
{
bool CreateWarp(ComPtr<ID3D11Device>* dev, ComPtr<ID3D11DeviceContext>* ctx)
{
    const D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL got{};
    return SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &want, 1, D3D11_SDK_VERSION,
                                       dev->GetAddressOf(), &got, ctx->GetAddressOf()));
}

std::vector<uint8_t> ReadTexture(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* tex)
{
    std::vector<uint8_t> out;
    if (!tex) return out;
    D3D11_TEXTURE2D_DESC d{};
    tex->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &staging))) return out;
    ctx->CopyResource(staging.Get(), tex);
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return out;
    out.resize(static_cast<size_t>(d.Width) * d.Height * 4);
    for (UINT y = 0; y < d.Height; ++y)
        memcpy(out.data() + static_cast<size_t>(y) * d.Width * 4,
               static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch, d.Width * 4);
    ctx->Unmap(staging.Get(), 0);
    return out;
}
} // namespace selftest

namespace
{
using namespace selftest;

std::wstring OutputPath(int argc, wchar_t** argv)
{
    std::wstring dir;
    for (int i = 1; i < argc; ++i)
        if (wcscmp(argv[i], L"--keep") != 0) dir = argv[i];
    if (dir.empty()) {
        wchar_t temp[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temp);
        dir = temp;
    }
    if (dir.back() != L'\\' && dir.back() != L'/') dir += L'\\';
    return dir + L"d3d11_record_selftest.rawd3d11";
}

// Renders frames 1 to 4 through the wrappers and captures frame 3.
bool Record(Capture* cap)
{
    ComPtr<ID3D11Device> realDev, dev;
    ComPtr<ID3D11DeviceContext> realCtx, ctx;
    if (!CreateWarp(&realDev, &realCtx)) { printf("WARP device creation failed\n"); return false; }
    if (FAILED(raw::rec::Install(realDev.Get(), realCtx.Get(), &dev, &ctx))) return false;
    raw::rec::Recorder* rec = raw::rec::GetRecorder(dev.Get());
    Scene scene;
    if (!rec || !scene.Init(dev.Get(), ctx.Get())) { printf("scene setup failed\n"); return false; }
    rec->SetBackbufferInfo(kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM);

    std::vector<uint8_t> frame2;
    for (int frame = 1; frame <= 4; ++frame) {
        scene.Frame(frame);
        if (frame == 2) {
            frame2 = ReadTexture(realDev.Get(), realCtx.Get(), scene.Backbuffer());
            if (!rec->BeginCapture(cap->path.c_str())) return false;
        }
        rec->OnPresent();       // frame boundary: starts the capture after frame 2, ends it after frame 3
        if (frame == 3) cap->reference = ReadTexture(realDev.Get(), realCtx.Get(), scene.Backbuffer());
    }
    cap->backbufferId = raw::rec::GetId(scene.Backbuffer());
    cap->historyId = raw::rec::GetId(scene.History());
    cap->depthId = raw::rec::GetId(scene.Depth());
    const bool animated = !frame2.empty() && frame2 != cap->reference;
    printf("capture: completed=%u ok=%d, frame 3 differs from frame 2: %d\n", rec->CapturesCompleted(),
           rec->LastCaptureOk(), animated);
    return rec->CapturesCompleted() == 1 && rec->LastCaptureOk() && animated && !rec->IsCapturing();
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    Capture cap;
    cap.path = OutputPath(argc, argv);
    bool keep = false;
    for (int i = 1; i < argc; ++i) keep = keep || wcscmp(argv[i], L"--keep") == 0;

    if (!Record(&cap)) {
        printf("capture: FAIL\n");
        return 1;
    }
    PrintFileStats(cap);
    raw::rec::Reader reader;
    const bool target = reader.Open(cap.path.c_str())
                     && reader.GetSummary().lastRenderTarget == cap.backbufferId && cap.backbufferId != 0;
    printf("summary names the backbuffer as the last render target: %s\n", target ? "PASS" : "FAIL");

    int failed = target ? 0 : 1;
    failed += CheckReplay(cap) ? 0 : 1;
    failed += CheckReplayStaging(cap) ? 0 : 1;
    failed += CheckCorruptContent(cap) ? 0 : 1;
    failed += CheckZeroDepth(cap) ? 0 : 1;
    failed += CheckSkipInitialState(cap) ? 0 : 1;
    failed += CheckTruncated(cap) ? 0 : 1;
    failed += CheckDump(cap) ? 0 : 1;
    if (!keep) DeleteFileW(cap.path.c_str());
    printf("%s (%d failed)\n", failed == 0 ? "ALL CHECKS PASSED" : "SELFTEST FAILED", failed);
    return failed == 0 ? 0 : 1;
}
