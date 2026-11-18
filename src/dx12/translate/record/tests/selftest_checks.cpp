// Checks of d3d11_record_selftest. Each replay runs on its own fresh WARP device.
#include "selftest_scene.h"
#include "rec_api.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace selftest
{
namespace
{
using raw::rec::Replayer;
using raw::rec::ReplayOptions;

std::vector<uint8_t> ReadFile(const std::wstring& path)
{
    std::vector<uint8_t> bytes;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return bytes;
    fseek(f, 0, SEEK_END);
    bytes.resize(static_cast<size_t>(ftell(f)));
    fseek(f, 0, SEEK_SET);
    if (fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) bytes.clear();
    fclose(f);
    return bytes;
}

size_t Differing(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
    if (a.size() != b.size()) return a.size() > b.size() ? a.size() : b.size();
    size_t n = 0;
    for (size_t i = 0; i < a.size(); ++i) n += a[i] != b[i];
    return n;
}

// Replays bytes on a fresh device and returns the final render target's
// pixels. Empty when the replay failed; *ran reports whether Run succeeded.
std::vector<uint8_t> ReplayImage(std::vector<uint8_t> bytes, const ReplayOptions& opt, bool* ran,
                                 raw::rec::ReplayResult* result)
{
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    Replayer rp;
    *ran = false;
    if (!CreateWarp(&dev, &ctx)) { printf("    WARP device creation failed\n"); return {}; }
    if (!rp.OpenMemory(std::move(bytes))) { printf("    open failed: %s\n", rp.Error().c_str()); return {}; }
    *ran = rp.Run(dev.Get(), ctx.Get(), opt);
    if (result) *result = rp.Result();
    if (!*ran) printf("    replay failed: %s\n", rp.Result().message.c_str());
    return ReadTexture(dev.Get(), ctx.Get(), rp.FindTexture(rp.LastRenderTarget()));
}

// File offset and size of the recorded contents of subresource 0 of a texture.
bool FindContents(const Capture& cap, uint64_t id, size_t* at, size_t* bytes)
{
    raw::rec::Reader reader;
    if (!reader.Open(cap.path.c_str())) return false;
    for (const raw::rec::Record& r : reader.Records()) {
        raw::rec::Cursor c(r);
        if (r.op != raw::rec::OP_DEFINE_TEXTURE || c.U64() != id) continue;
        c.Raw(14 * 4);                                  // the rest of the description
        const raw::rec::SubData s = raw::rec::ReadSubData(c);
        if (s.state != raw::rec::SUB_DATA) return false;
        *at = static_cast<size_t>(r.offset) + 8 + static_cast<size_t>(s.data - r.data);
        *bytes = static_cast<size_t>(s.bytes);
        return true;
    }
    return false;
}

bool Report(const char* name, bool pass)
{
    printf("%s: %s\n", name, pass ? "PASS" : "FAIL");
    return pass;
}
} // namespace

void PrintFileStats(const Capture& cap)
{
    raw::rec::Reader reader;
    if (!reader.Open(cap.path.c_str())) { printf("file does not load: %s\n", reader.Error().c_str()); return; }
    const raw::rec::Summary& s = reader.GetSummary();
    printf("file: %llu bytes, %zu records, %llu content bytes, adapter \"%s\"\n",
           static_cast<unsigned long long>(reader.FileBytes()), reader.Records().size(),
           static_cast<unsigned long long>(s.contentBytes), reader.Header().adapter.c_str());
    for (const auto& oc : s.opCounts) printf("  %-28s %u\n", raw::rec::OpName(oc.first), oc.second);
    printf("  defined objects:");
    for (const auto& kc : s.kindCounts) printf(" %s=%u", raw::rec::KindName(kc.first), kc.second);
    printf("\n  undefined contents: %zu\n", s.undefined.size());
}

bool CheckReplay(const Capture& cap)
{
    bool ran = false;
    raw::rec::ReplayResult res;
    const std::vector<uint8_t> image = ReplayImage(ReadFile(cap.path), {}, &ran, &res);
    const size_t diff = Differing(image, cap.reference);
    size_t lit = 0;
    for (size_t i = 0; i < image.size(); i += 4) lit += image[i] > 16 || image[i + 1] > 16;
    printf("  (a) strict replay ok=%d, %u objects, %llu records, depth restored %u / not restored %u, "
           "contents not restored %u, staging uploads %u, GetData polls %u (timeouts %u)\n",
           ran, res.objects, static_cast<unsigned long long>(res.records), res.depthRestored,
           res.depthNotRestored, res.contentsNotRestored, res.stagingUploads, res.getDataPolls,
           res.getDataTimeouts);
    printf("  (a) %zu of %zu bytes differ from the direct frame; %zu of %zu pixels lit\n", diff,
           cap.reference.size(), lit, image.size() / 4);
    return Report("(a) replay on a fresh device is bit-identical",
                  ran && !image.empty() && diff == 0 && lit > 1000 && res.depthNotRestored == 0);
}

// WARP accepts initial data for depth-stencil and generated-mip textures, so
// check (a) never takes the staging-copy route. This run forces it.
bool CheckReplayStaging(const Capture& cap)
{
    ReplayOptions opt;
    opt.uploadThroughStaging = true;
    bool ran = false;
    raw::rec::ReplayResult res;
    const std::vector<uint8_t> image = ReplayImage(ReadFile(cap.path), opt, &ran, &res);
    const size_t diff = Differing(image, cap.reference);
    printf("  (a2) staging uploads %u, depth restored %u / not restored %u, contents not restored %u; "
           "%zu bytes differ\n", res.stagingUploads, res.depthRestored, res.depthNotRestored,
           res.contentsNotRestored, diff);
    return Report("(a2) replay with staging-copy uploads is bit-identical",
                  ran && !image.empty() && diff == 0 && res.stagingUploads > 0 && res.depthRestored == 1
                      && res.depthNotRestored == 0 && res.contentsNotRestored == 0);
}

// Flips one byte of the history texture's recorded contents: the centre
// texel's red channel. The texture is shown by a quad in the captured frame.
bool CheckCorruptContent(const Capture& cap)
{
    std::vector<uint8_t> bytes = ReadFile(cap.path);
    size_t at = 0, size = 0;
    if (!FindContents(cap, cap.historyId, &at, &size) || size != kHalf * kHalf * 4 || at + size > bytes.size())
        return Report("(b) corrupted content byte changes the image", false);
    at += (kHalf / 2 * kHalf + kHalf / 2) * 4;
    bytes[at] ^= 0x80;
    bool ran = false;
    const std::vector<uint8_t> image = ReplayImage(std::move(bytes), {}, &ran, nullptr);
    const size_t diff = Differing(image, cap.reference);
    printf("  (b) flipped bit 7 of file byte %zu; replay ok=%d; %zu bytes differ\n", at, ran, diff);
    return Report("(b) corrupted content byte changes the image", ran && !image.empty() && diff > 0);
}

// The captured frame tests one quad against the depth buffer the previous
// frame left. With the recorded depth zeroed that quad fails everywhere.
bool CheckZeroDepth(const Capture& cap)
{
    std::vector<uint8_t> bytes = ReadFile(cap.path);
    size_t at = 0, size = 0;
    if (!FindContents(cap, cap.depthId, &at, &size) || size != kSize * kSize * 4 || at + size > bytes.size())
        return Report("(b2) zeroed depth contents change the image", false);
    memset(bytes.data() + at, 0, size);
    bool ran = false;
    const std::vector<uint8_t> image = ReplayImage(std::move(bytes), {}, &ran, nullptr);
    const size_t diff = Differing(image, cap.reference);
    printf("  (b2) zeroed %zu bytes of recorded depth; replay ok=%d; %zu bytes differ\n", size, ran, diff);
    return Report("(b2) zeroed depth contents change the image", ran && !image.empty() && diff > 0);
}

bool CheckSkipInitialState(const Capture& cap)
{
    ReplayOptions opt;
    opt.skipInitialState = true;
    bool ran = false;
    const std::vector<uint8_t> image = ReplayImage(ReadFile(cap.path), opt, &ran, nullptr);
    const size_t diff = Differing(image, cap.reference);
    printf("  (c) replay without the initial-state block ok=%d; %zu bytes differ\n", ran, diff);
    return Report("(c) skipping the initial state changes the image", !image.empty() && diff > 0);
}

bool CheckTruncated(const Capture& cap)
{
    const std::vector<uint8_t> bytes = ReadFile(cap.path);
    raw::rec::Reader whole;
    if (bytes.empty() || !whole.Open(cap.path.c_str())) return Report("(d) truncated files give a clean error", false);
    const size_t boundary = static_cast<size_t>(whole.Records()[whole.Records().size() / 2].offset);
    const size_t cuts[] = {0, 20, 44, bytes.size() / 3, boundary, bytes.size() - 8, bytes.size() - 1};
    bool pass = true;
    for (const size_t cut : cuts) {
        Replayer rp;
        ComPtr<ID3D11Device> dev;
        ComPtr<ID3D11DeviceContext> ctx;
        const bool opened = rp.OpenMemory(std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<ptrdiff_t>(cut)));
        const bool ran = CreateWarp(&dev, &ctx) && rp.Run(dev.Get(), ctx.Get());
        printf("  (d) %zu of %zu bytes: open=%d run=%d, \"%s\"\n", cut, bytes.size(), opened, ran,
               rp.Error().c_str());
        pass = pass && !opened && !ran && !rp.Error().empty();
    }
    return Report("(d) truncated files give a clean error", pass);
}

bool CheckDump(const Capture& cap)
{
    // Library listing into a file, then the command-line tool beside this exe.
    const std::wstring listing = cap.path + L".txt";
    FILE* f = nullptr;
    bool lib = _wfopen_s(&f, listing.c_str(), L"w") == 0 && f && raw::rec::DumpListing(cap.path.c_str(), f);
    if (f) fclose(f);
    const std::vector<uint8_t> text = ReadFile(listing);
    const std::string s(text.begin(), text.end());
    lib = lib && s.find("DRAW_INDEXED") != std::string::npos && s.find("summary:") != std::string::npos;
    DeleteFileW(listing.c_str());

    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring tool(exe);
    tool = tool.substr(0, tool.find_last_of(L"\\/") + 1) + L"d3d11_stream_dump.exe";
    const std::wstring cmd = L"\"\"" + tool + L"\" \"" + cap.path + L"\"\"";
    size_t lines = 0;
    bool sawDraw = false, sawSummary = false;
    int code = -1;
    if (FILE* p = _wpopen(cmd.c_str(), L"r")) {
        char line[1024];
        while (fgets(line, sizeof(line), p)) {
            ++lines;
            sawDraw = sawDraw || strstr(line, "DRAW_INDEXED");
            sawSummary = sawSummary || strstr(line, "summary:");
        }
        code = _pclose(p);
    }
    printf("  (e) library listing %zu bytes ok=%d; d3d11_stream_dump exit %d, %zu lines\n", text.size(), lib,
           code, lines);
    return Report("(e) the dump tool lists the file", lib && code == 0 && lines > 50 && sawDraw && sawSummary);
}
} // namespace selftest
