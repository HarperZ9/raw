// FrameLedger files: raw texture dumps, the manifest and the pixel shaders the
// frame used. Original work, MIT.
//
// .rawtex layout (little endian): "RAWTEX1\0", then ten uint32: width, height,
// depth, arraySize, mipLevels (of the source; only mip 0 is stored), DXGI
// format, dimension (2 or 3), rowBytes, cube (0 or 1), samples. Then
// arraySize * depth images of height rows, rowBytes each, top row first.
#include "FrameLedgerImpl.h"
#include "ProxyLog.h"
#include "ShaderManager.h"
#include <cstring>

namespace SB::Proxy::ledger
{

UINT BytesPerPixel(DXGI_FORMAT f)
{
    const int v = static_cast<int>(f);
    if (v >= 1 && v <= 4) return 16;                      // R32G32B32A32
    if (v >= 5 && v <= 8) return 12;                      // R32G32B32
    if (v >= 9 && v <= 14) return 8;                      // R16G16B16A16
    if (v >= 15 && v <= 22) return 8;                     // R32G32, R32G8X24
    if (v >= 23 && v <= 32) return 4;                     // R10G10B10A2, R11G11B10, R8G8B8A8
    if (v >= 33 && v <= 47) return 4;                     // R16G16, R32, R24G8
    if (v >= 48 && v <= 59) return 2;                     // R8G8, R16
    if (v >= 60 && v <= 65) return 1;                     // R8, A8
    if (v >= 85 && v <= 93) return v <= 86 ? 2 : 4;       // B5G6R5, B5G5R5A1, B8G8R8A8/X8
    return 0;                                             // unknown: rows are stored at their pitch
}

const char* FormatName(DXGI_FORMAT f)
{
    switch (f) {
        case DXGI_FORMAT_R32G32B32A32_FLOAT: return "R32G32B32A32_FLOAT";
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: return "R16G16B16A16_TYPELESS";
        case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
        case DXGI_FORMAT_R16G16B16A16_UNORM: return "R16G16B16A16_UNORM";
        case DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
        case DXGI_FORMAT_R11G11B10_FLOAT: return "R11G11B10_FLOAT";
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: return "R8G8B8A8_TYPELESS";
        case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
        case DXGI_FORMAT_R16G16_FLOAT: return "R16G16_FLOAT";
        case DXGI_FORMAT_R16G16_UNORM: return "R16G16_UNORM";
        case DXGI_FORMAT_R32_TYPELESS: return "R32_TYPELESS";
        case DXGI_FORMAT_D32_FLOAT: return "D32_FLOAT";
        case DXGI_FORMAT_R32_FLOAT: return "R32_FLOAT";
        case DXGI_FORMAT_R32_UINT: return "R32_UINT";
        case DXGI_FORMAT_R24G8_TYPELESS: return "R24G8_TYPELESS";
        case DXGI_FORMAT_D24_UNORM_S8_UINT: return "D24_UNORM_S8_UINT";
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: return "R24_UNORM_X8_TYPELESS";
        case DXGI_FORMAT_R8G8_UNORM: return "R8G8_UNORM";
        case DXGI_FORMAT_R16_TYPELESS: return "R16_TYPELESS";
        case DXGI_FORMAT_R16_FLOAT: return "R16_FLOAT";
        case DXGI_FORMAT_D16_UNORM: return "D16_UNORM";
        case DXGI_FORMAT_R16_UNORM: return "R16_UNORM";
        case DXGI_FORMAT_R8_UNORM: return "R8_UNORM";
        case DXGI_FORMAT_A8_UNORM: return "A8_UNORM";
        case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: return "B8G8R8A8_TYPELESS";
        default: return "";
    }
}

static bool MakeStaging(ID3D11DeviceContext* ctx, TexRec& t)
{
    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    HRESULT hr = E_FAIL;
    if (t.dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        D3D11_TEXTURE2D_DESC d{};
        static_cast<ID3D11Texture2D*>(t.res)->GetDesc(&d);
        d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        d.MiscFlags &= D3D11_RESOURCE_MISC_TEXTURECUBE;
        ID3D11Texture2D* st = nullptr;
        hr = dev->CreateTexture2D(&d, nullptr, &st);
        t.staging = st;
    } else {
        D3D11_TEXTURE3D_DESC d{};
        static_cast<ID3D11Texture3D*>(t.res)->GetDesc(&d);
        d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
        ID3D11Texture3D* st = nullptr;
        hr = dev->CreateTexture3D(&d, nullptr, &st);
        t.staging = st;
    }
    dev->Release();
    if (FAILED(hr)) {
        char buf[64];
        sprintf_s(buf, "staging clone failed 0x%08X", static_cast<unsigned>(hr));
        t.note = buf;
        t.dumpFailed = true;
        t.staging = nullptr;
    }
    return SUCCEEDED(hr);
}

bool DumpTexture(ID3D11DeviceContext* ctx, int id, const char* name, bool shareCheck)
{
    State& s = S();
    TexRec& t = s.tex[id];
    if (t.dumpFailed) return false;
    if (t.samples > 1) { t.note = "multisampled: not dumped"; t.dumpFailed = true; return false; }
    if (!t.staging && !MakeStaging(ctx, t)) return false;
    ctx->CopyResource(t.staging, t.res);

    FILE* f = nullptr;
    const std::string path = s.dir + "\\" + name + ".rawtex";
    if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) return false;
    const UINT bpp = BytesPerPixel(t.fmt);
    bool ok = true;
    for (UINT slice = 0; slice < t.array && ok; ++slice) {
        const UINT sub = slice * t.mips;   // mip 0 of each slice
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(t.staging, sub, D3D11_MAP_READ, 0, &m))) { ok = false; break; }
        const UINT rowBytes = bpp ? t.w * bpp : m.RowPitch;
        if (slice == 0) {
            const uint32_t hdr[10] = {t.w, t.h, t.depth, t.array, t.mips, static_cast<uint32_t>(t.fmt),
                                      t.dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D ? 3u : 2u, rowBytes,
                                      (t.misc & D3D11_RESOURCE_MISC_TEXTURECUBE) ? 1u : 0u, t.samples};
            fwrite("RAWTEX1", 1, 8, f);
            fwrite(hdr, 4, 10, f);
        }
        for (UINT z = 0; z < t.depth; ++z)
            for (UINT y = 0; y < t.h; ++y)
                fwrite(static_cast<const uint8_t*>(m.pData) + size_t(z) * m.DepthPitch + size_t(y) * m.RowPitch,
                       1, rowBytes, f);
        if (shareCheck && g_ledgerShareCheck && t.dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
            const int r = g_ledgerShareCheck(static_cast<ID3D11Texture2D*>(t.res), slice, m.pData, m.RowPitch, rowBytes, t.h);
            t.sharedEqual += r == 1; t.sharedDifferent += r == 2; t.sharedFailed += r == 3;
        }
        ctx->Unmap(t.staging, sub);
    }
    fclose(f);
    if (!ok) { t.note = "Map of the staging clone failed"; t.dumpFailed = true; }
    return ok;
}

static void PutView(FILE* f, const ViewRef& v)
{
    fprintf(f, "{\"tex\":%d,\"format\":%d,\"formatName\":\"%s\",\"mip\":%u,\"slice\":%u,\"slices\":%u,\"flags\":%u}",
            v.tex, static_cast<int>(v.fmt), FormatName(v.fmt), v.mip, v.slice, v.slices, v.flags);
}

void WriteManifest()
{
    State& s = S();
    FILE* f = nullptr;
    if (fopen_s(&f, (s.dir + "\\manifest.json").c_str(), "w") != 0 || !f) return;
    fprintf(f, "{\n\"backbuffer\":[%u,%u],\n\"draws\":%u,\n\"textures\":[\n", s.bbW, s.bbH, s.drawCount);
    for (size_t k = 0; k < s.tex.size(); ++k) {
        const TexRec& t = s.tex[k];
        fprintf(f, "{\"id\":%d,\"dim\":%d,\"w\":%u,\"h\":%u,\"depth\":%u,\"array\":%u,\"mips\":%u,\"samples\":%u,"
                   "\"format\":%d,\"formatName\":\"%s\",\"bind\":%u,\"misc\":%u,\"rtIntervals\":%u,\"dsIntervals\":%u,"
                   "\"uavWrites\":%u,\"copiesIn\":%u,\"copiesOut\":%u,\"draws\":%u,\"written\":%d,\"dumped\":%d,"
                   "\"sharedEqual\":%d,\"sharedDifferent\":%d,\"sharedFailed\":%d,"
                   "\"note\":\"%s\"}%s\n",
                t.id, t.dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D ? 3 : 2, t.w, t.h, t.depth, t.array, t.mips,
                t.samples, static_cast<int>(t.fmt), FormatName(t.fmt), t.bind, t.misc, t.rtIntervals,
                t.dsIntervals, t.uavWrites, t.copiesIn, t.copiesOut, t.draws, t.written ? 1 : 0,
                (t.written && !t.dumpFailed) ? 1 : 0, t.sharedEqual, t.sharedDifferent, t.sharedFailed,
                t.note.c_str(), k + 1 < s.tex.size() ? "," : "");
    }
    fprintf(f, "],\n\"intervals\":[\n");
    for (size_t k = 0; k < s.intervals.size(); ++k) {
        const Interval& iv = s.intervals[k];
        fprintf(f, "{\"seq\":%d,\"phaseOpen\":\"%s\",\"phaseClose\":\"%s\",\"viewport\":[%g,%g],\"draws\":%u,"
                   "\"firstDraw\":%u,\"blendDraws\":%u,\"depthWriteDraws\":%u,\"rt\":[",
                iv.seq, RenderPhaseDetector::PhaseName(iv.phaseOpen), RenderPhaseDetector::PhaseName(iv.phaseClose),
                iv.vpW, iv.vpH, iv.draws, iv.firstDraw, iv.blendDraws, iv.depthWriteDraws);
        for (UINT r = 0; r < iv.numRt; ++r) { if (r) fputc(',', f); PutView(f, iv.rt[r]); }
        fprintf(f, "],\"ds\":");
        PutView(f, iv.ds);
        fprintf(f, ",\"ps\":[");
        for (size_t p = 0; p < iv.ps.size(); ++p)
            fprintf(f, "%s\"%016llx\"", p ? "," : "", static_cast<unsigned long long>(iv.ps[p]));
        fprintf(f, "],\"history\":[");
        for (size_t h = 0; h < iv.history.size(); ++h) fprintf(f, "%s%d", h ? "," : "", iv.history[h]);
        fprintf(f, "]}%s\n", k + 1 < s.intervals.size() ? "," : "");
    }
    fprintf(f, "],\n\"events\":[\n");
    for (size_t k = 0; k < s.events.size(); ++k)
        fprintf(f, "{\"after\":%d,\"event\":%s}%s\n", s.events[k].afterSeq, s.events[k].json.c_str(),
                k + 1 < s.events.size() ? "," : "");
    fprintf(f, "]\n}\n");
    fclose(f);
}

void DumpShaders()
{
    State& s = S();
    for (const auto& [hash, ps] : s.psSeen) {
        const std::vector<uint8_t> code = ShaderManager::Get().GetPSBytecode(ps);
        if (code.empty()) continue;
        char name[48];
        sprintf_s(name, "\\ps_%016llx.dxbc", static_cast<unsigned long long>(hash));
        FILE* f = nullptr;
        if (fopen_s(&f, (s.dir + name).c_str(), "wb") == 0 && f) {
            fwrite(code.data(), 1, code.size(), f);
            fclose(f);
        }
    }
}

} // namespace SB::Proxy::ledger
