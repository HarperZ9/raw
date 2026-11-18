#pragma once
// Survey of a capture folder: patch every pixel shader the frame used, check
// each container hash, create each patched shader on the device, and count
// what reflection found. Shader bytecode of the game stays out of the
// repository; this reads a local capture. Original work, MIT.
#include "../../src/core/dxbc/DxbcContainer.h"
#include "../../src/core/dxbc/GBufferPatch.h"
#include "fixtures.h"

#include <filesystem>
#include <fstream>
#include <map>

namespace bench
{

inline const char* KindName(raw::dxbc::ShaderKind k)
{
    static const char* n[] = {"unknown", "lighting", "distant-lod", "effect", "water", "generic"};
    return n[static_cast<int>(k)];
}

inline int Survey(Bench& b, const char* dir, const char* outPath)
{
    using namespace raw::dxbc;
    struct Row { int shaders = 0, patched = 0, created = 0, albedo = 0, inPlace = 0, mask = 0, gloss = 0, emissive = 0, depth = 0; };
    std::map<std::string, Row> rows;
    std::map<std::string, int> refused;
    int total = 0, hashOk = 0, failures = 0;
    std::string perShader;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        const std::string name = e.path().filename().string();
        if (name.rfind("ps_", 0) != 0 || e.path().extension() != ".dxbc") continue;
        std::ifstream f(e.path(), std::ios::binary);
        const std::vector<uint8_t> code((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        ++total;
        hashOk += HashMatches(code.data(), code.size());
        PatchInfo info;
        std::vector<uint8_t> out;
        const bool ok = PatchForGBuffer(code.data(), code.size(), {}, &out, &info);
        Row& r = rows[KindName(info.kind)];
        ++r.shaders;
        bool created = false;
        if (ok) {
            ++r.patched;
            ComPtr<ID3D11PixelShader> ps;
            created = SUCCEEDED(b.dev->CreatePixelShader(out.data(), out.size(), nullptr, &ps));
            r.created += created;
            if (!created) { ++failures; printf("  %s: the device refused the patched shader\n", name.c_str()); }
            r.albedo += info.albedo; r.inPlace += info.albedoInPlace; r.mask += info.specularMask;
            r.gloss += info.gloss; r.emissive += info.emissive; r.depth += info.depth;
        } else {
            ++refused[info.refused ? info.refused : "?"];
        }
        char line[256];
        snprintf(line, sizeof line, "%s{\"shader\":\"%s\",\"kind\":\"%s\",\"class\":%d,\"patched\":%d,\"created\":%d,\"albedo\":%d,"
                 "\"in_place\":%d,\"mask\":%d,\"gloss\":%d,\"emissive\":%d}", perShader.empty() ? "" : ",\n", name.substr(3, 16).c_str(),
                 KindName(info.kind), info.materialClass, ok, created, info.albedo, info.albedoInPlace, info.specularMask, info.gloss, info.emissive);
        perShader += line;
    }
    printf("%d pixel shaders, %d container hashes equal the computed hash\n", total, hashOk);
    printf("%-12s %7s %7s %7s %7s %8s %5s %5s %8s %5s\n", "kind", "shaders", "patched", "created", "albedo", "in place", "mask", "gloss", "emissive", "depth");
    for (const auto& [k, r] : rows)
        printf("%-12s %7d %7d %7d %7d %8d %5d %5d %8d %5d\n", k.c_str(), r.shaders, r.patched, r.created, r.albedo, r.inPlace, r.mask, r.gloss, r.emissive, r.depth);
    for (const auto& [why, n] : refused) printf("refused, %s: %d\n", why.c_str(), n);
    if (outPath) {
        std::ofstream o(outPath);
        o << "{\"shaders\":" << total << ",\"hash_equal\":" << hashOk << ",\"device\":\"" << (b.warp ? "WARP" : "hardware") << "\",\"per_shader\":[\n" << perShader << "\n]}\n";
    }
    return (failures || hashOk != total) ? 1 : 0;
}

} // namespace bench
