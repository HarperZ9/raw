// RawTextureBridge.cpp: Zero-dependency Windows WIC (.png/.jpg/.tif/.bmp) + native .tga decoder + raw_assets/ scanner.
// Original work, MIT.
#include "RawTextureBridge.h"
#include "RawAssetBridge.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace raw::bridge
{
namespace
{
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

std::string LowerExt(const std::string& p)
{
    const size_t dot = p.rfind('.');
    if (dot == std::string::npos) return {};
    std::string e = p.substr(dot);
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

bool LoadTga(const std::string& path, RawTextureAsset* out, std::string* err)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "failed to open .tga: " + path; return false; }
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 18) { if (err) *err = ".tga too small"; return false; }
    const uint8_t idLen = b[0], imgType = b[2], bpp = b[16], desc = b[17];
    const uint16_t w = uint16_t(b[12]) | (uint16_t(b[13]) << 8);
    const uint16_t h = uint16_t(b[14]) | (uint16_t(b[15]) << 8);
    if ((imgType != 2 && imgType != 10) || (bpp != 24 && bpp != 32) || w == 0 || h == 0) {
        if (err) *err = "unsupported .tga format (need 24/32-bit truecolor)";
        return false;
    }
    const size_t bytesPerPx = bpp / 8;
    const size_t pxCount = size_t(w) * size_t(h);
    out->width = w; out->height = h; out->name = path;
    out->rgba8.resize(pxCount * 4);
    size_t src = 18 + idLen, dstPx = 0;
    auto writePx = [&](size_t idx, const uint8_t* p) {
        const size_t y = idx / w, x = idx % w;
        const size_t outY = (desc & 0x20) ? y : (h - 1 - y);
        uint8_t* o = out->rgba8.data() + (outY * w + x) * 4;
        o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; o[3] = (bytesPerPx == 4) ? p[3] : 255u;
    };
    if (imgType == 2) {
        if (src + pxCount * bytesPerPx > b.size()) { if (err) *err = ".tga truncated"; return false; }
        for (size_t i = 0; i < pxCount; ++i) writePx(i, b.data() + src + i * bytesPerPx);
    } else {
        while (dstPx < pxCount && src < b.size()) {
            const uint8_t hdr = b[src++];
            const uint32_t run = (hdr & 0x7Fu) + 1u;
            if (hdr & 0x80u) {
                if (src + bytesPerPx > b.size()) break;
                const uint8_t* p = b.data() + src; src += bytesPerPx;
                for (uint32_t k = 0; k < run && dstPx < pxCount; ++k) writePx(dstPx++, p);
            } else {
                for (uint32_t k = 0; k < run && dstPx < pxCount && src + bytesPerPx <= b.size(); ++k) {
                    writePx(dstPx++, b.data() + src); src += bytesPerPx;
                }
            }
        }
    }
    return true;
}

bool LoadWic(const std::string& path, RawTextureAsset* out, std::string* err)
{
    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool didInit = SUCCEEDED(hrCo);
    ComPtr<IWICImagingFactory> fac;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac)))) {
        if (didInit) CoUninitialize();
        if (err) *err = "WIC factory creation failed";
        return false;
    }
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(wlen > 0 ? wlen - 1 : 0, L'\0');
    if (wlen > 1) MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);

    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> conv;
    bool ok = SUCCEEDED(fac->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec))
           && SUCCEEDED(dec->GetFrame(0, &frame))
           && SUCCEEDED(fac->CreateFormatConverter(&conv))
           && SUCCEEDED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
    if (ok) {
        UINT w = 0, h = 0;
        conv->GetSize(&w, &h);
        out->width = w; out->height = h; out->name = path;
        out->rgba8.resize(size_t(w) * size_t(h) * 4);
        ok = SUCCEEDED(conv->CopyPixels(nullptr, w * 4, UINT(out->rgba8.size()), out->rgba8.data()));
    }
    conv.Reset(); frame.Reset(); dec.Reset(); fac.Reset();
    if (didInit) CoUninitialize();
    if (!ok && err) *err = "WIC decode failed: " + path;
    return ok;
}
} // namespace

bool LoadModernTextureFromMemory(const uint8_t* bytes, size_t size, const std::string& label,
                                 RawTextureAsset* outTex, std::string* outErr)
{
    if (!bytes || size < 8 || !outTex) {
        if (outErr) *outErr = "empty texture memory buffer";
        return false;
    }
    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool didInit = SUCCEEDED(hrCo);
    ComPtr<IWICImagingFactory> fac;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac)))) {
        if (didInit) CoUninitialize();
        if (outErr) *outErr = "WIC factory creation failed";
        return false;
    }
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> conv;
    bool ok = SUCCEEDED(fac->CreateStream(&stream))
           && SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes), static_cast<DWORD>(size)))
           && SUCCEEDED(fac->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &dec))
           && SUCCEEDED(dec->GetFrame(0, &frame))
           && SUCCEEDED(fac->CreateFormatConverter(&conv))
           && SUCCEEDED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
    if (ok) {
        UINT w = 0, h = 0;
        conv->GetSize(&w, &h);
        outTex->width = w; outTex->height = h; outTex->name = label;
        outTex->rgba8.resize(size_t(w) * size_t(h) * 4);
        ok = SUCCEEDED(conv->CopyPixels(nullptr, w * 4, UINT(outTex->rgba8.size()), outTex->rgba8.data()));
    }
    conv.Reset(); frame.Reset(); dec.Reset(); stream.Reset(); fac.Reset();
    if (didInit) CoUninitialize();
    if (!ok && outErr) *outErr = "WIC memory decode failed: " + label;
    return ok;
}

bool LoadModernTextureAsset(const std::string& path, RawTextureAsset* outTex, std::string* outErr)
{
    if (!outTex) return false;
    const std::string ext = LowerExt(path);
    if (ext == ".tga") return LoadTga(path, outTex, outErr);
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tif" || ext == ".tiff" || ext == ".bmp")
        return LoadWic(path, outTex, outErr);
    if (outErr) *outErr = "unsupported texture extension: " + ext;
    return false;
}

RawAssetScanSummary ScanRawAssetsFolder(const std::string& gameDir, std::string* outLog)
{
    RawAssetScanSummary s{};
    std::error_code ec;
    const fs::path root = fs::path(gameDir) / "raw_assets";
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) return s;
    for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string p = entry.path().string();
        const std::string ext = LowerExt(p);
        if (ext == ".glb" || ext == ".obj") {
            RawMeshAsset m;
            if (LoadModernMeshAsset(p, &m)) {
                s.meshFiles++;
                s.totalMeshlets += static_cast<uint32_t>(m.meshlets.size());
            }
        } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".tif" || ext == ".bmp") {
            s.textureFiles++;
        }
    }
    if (outLog) {
        std::ostringstream oss;
        oss << "[RawAssets] scanned raw_assets/: " << s.meshFiles << " modern meshes (" << s.totalMeshlets
            << " RAW-MESH meshlets), " << s.textureFiles << " modern textures (.png/.jpg/.tga/.tif/.bmp)";
        *outLog = oss.str();
    }
    return s;
}

} // namespace raw::bridge
