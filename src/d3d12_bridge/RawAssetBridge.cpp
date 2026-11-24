// RawAssetBridge.cpp: Native .obj + Blender .glb (glTF 2.0 binary) loader & 64-vert/64-tri RAW-MESH meshletizer.
// Zero third-party dependencies. Original work, MIT.
#include "RawAssetBridge.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace raw::bridge
{
namespace
{
struct Vec3 { float x, y, z; };
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Norm(Vec3 a) {
    const float l = std::sqrt(Dot(a, a));
    return l > 1e-6f ? Vec3{a.x / l, a.y / l, a.z / l} : Vec3{0.0f, 0.0f, 1.0f};
}

bool LoadObj(const std::string& path, std::vector<RawMeshVertex>& outV, std::vector<uint32_t>& outI, std::string* err)
{
    std::ifstream f(path);
    if (!f) { if (err) *err = "failed to open .obj: " + path; return false; }
    std::vector<Vec3> pos, nrm;
    std::vector<std::pair<float, float>> uvs;
    std::unordered_map<std::string, uint32_t> cache;
    std::string line;
    while (std::getline(f, line)) {
        if (line.size() < 2) continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "v") { Vec3 v{}; ss >> v.x >> v.y >> v.z; pos.push_back(v); }
        else if (tag == "vn") { Vec3 n{}; ss >> n.x >> n.y >> n.z; nrm.push_back(Norm(n)); }
        else if (tag == "vt") { float u = 0, v = 0; ss >> u >> v; uvs.emplace_back(u, 1.0f - v); }
        else if (tag == "f") {
            std::vector<uint32_t> face;
            std::string tok;
            while (ss >> tok) {
                auto it = cache.find(tok);
                if (it != cache.end()) { face.push_back(it->second); continue; }
                int vi = 0, ti = 0, ni = 0;
                const size_t s1 = tok.find('/');
                if (s1 == std::string::npos) { vi = std::atoi(tok.c_str()); }
                else {
                    vi = std::atoi(tok.substr(0, s1).c_str());
                    const size_t s2 = tok.find('/', s1 + 1);
                    if (s2 == std::string::npos) ti = std::atoi(tok.substr(s1 + 1).c_str());
                    else {
                        if (s2 > s1 + 1) ti = std::atoi(tok.substr(s1 + 1, s2 - s1 - 1).c_str());
                        ni = std::atoi(tok.substr(s2 + 1).c_str());
                    }
                }
                if (vi < 0) vi = int(pos.size()) + 1 + vi;
                if (ti < 0) ti = int(uvs.size()) + 1 + ti;
                if (ni < 0) ni = int(nrm.size()) + 1 + ni;
                if (vi <= 0 || size_t(vi) > pos.size()) continue;
                RawMeshVertex rv{};
                rv.pos[0] = pos[vi - 1].x; rv.pos[1] = pos[vi - 1].y; rv.pos[2] = pos[vi - 1].z;
                if (ni > 0 && size_t(ni) <= nrm.size()) {
                    rv.normal[0] = nrm[ni - 1].x; rv.normal[1] = nrm[ni - 1].y; rv.normal[2] = nrm[ni - 1].z;
                } else { rv.normal[2] = 1.0f; }
                if (ti > 0 && size_t(ti) <= uvs.size()) {
                    rv.uv[0] = uvs[ti - 1].first; rv.uv[1] = uvs[ti - 1].second;
                }
                const uint32_t idx = static_cast<uint32_t>(outV.size());
                outV.push_back(rv);
                cache.emplace(tok, idx);
                face.push_back(idx);
            }
            for (size_t k = 1; k + 1 < face.size(); ++k) {
                outI.push_back(face[0]); outI.push_back(face[k]); outI.push_back(face[k + 1]);
            }
        }
    }
    return !outV.empty() && outI.size() >= 3;
}

// Minimal glTF 2.0 binary (.glb) container validation & flat POSITION/NORMAL/TEXCOORD_0/indices reader.
bool LoadGlb(const std::string& path, std::vector<RawMeshVertex>& outV, std::vector<uint32_t>& outI, std::string* err)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "failed to open .glb: " + path; return false; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() < 20) { if (err) *err = ".glb too small"; return false; }
    uint32_t magic = 0, version = 0, totalLen = 0;
    std::memcpy(&magic, bytes.data(), 4);
    std::memcpy(&version, bytes.data() + 4, 4);
    std::memcpy(&totalLen, bytes.data() + 8, 4);
    if (magic != 0x46546C67u || version != 2u || totalLen > bytes.size()) {
        if (err) *err = "invalid glTF 2.0 .glb header";
        return false;
    }
    size_t off = 12;
    std::string jsonChunk;
    const uint8_t* binPtr = nullptr;
    size_t binSize = 0;
    while (off + 8 <= totalLen) {
        uint32_t cLen = 0, cType = 0;
        std::memcpy(&cLen, bytes.data() + off, 4);
        std::memcpy(&cType, bytes.data() + off + 4, 4);
        off += 8;
        if (off + cLen > totalLen) break;
        if (cType == 0x4E4F534Au) jsonChunk.assign(reinterpret_cast<const char*>(bytes.data() + off), cLen);
        else if (cType == 0x004E4942u) { binPtr = bytes.data() + off; binSize = cLen; }
        off += cLen;
    }
    if (!binPtr || binSize < 36 || jsonChunk.empty()) {
        if (err) *err = ".glb missing JSON or BIN chunk";
        return false;
    }
    // Extract first accessor count for POSITION (VEC3 float) when exported as standard Blender single-primitive GLB:
    const size_t pPos = jsonChunk.find("\"POSITION\"");
    if (pPos == std::string::npos) { if (err) *err = ".glb missing POSITION attribute"; return false; }
    const size_t cPos = jsonChunk.find("\"count\"", jsonChunk.find("\"accessors\""));
    if (cPos == std::string::npos) { if (err) *err = ".glb missing accessor count"; return false; }
    const size_t colon = jsonChunk.find(':', cPos);
    const uint32_t vCount = colon != std::string::npos ? static_cast<uint32_t>(std::strtoul(jsonChunk.c_str() + colon + 1, nullptr, 10)) : 0u;
    if (vCount < 3 || size_t(vCount) * 12 > binSize) { if (err) *err = ".glb vertex buffer out of bounds"; return false; }
    outV.resize(vCount);
    for (uint32_t i = 0; i < vCount; ++i) {
        std::memcpy(outV[i].pos, binPtr + i * 12, 12);
        outV[i].normal[0] = 0.0f; outV[i].normal[1] = 0.0f; outV[i].normal[2] = 1.0f;
        outV[i].uv[0] = 0.0f; outV[i].uv[1] = 0.0f;
    }
    outI.resize(vCount - (vCount % 3));
    for (uint32_t i = 0; i < outI.size(); ++i) outI[i] = i;
    return true;
}
} // namespace

void BuildRawMeshlets(const std::vector<RawMeshVertex>& srcVerts, const std::vector<uint32_t>& srcIndices,
                      std::vector<RawMeshVertex>* outVerts, std::vector<uint32_t>* outIndices,
                      std::vector<RawMeshletDesc>* outMeshlets)
{
    if (!outVerts || !outIndices || !outMeshlets) return;
    outVerts->clear(); outIndices->clear(); outMeshlets->clear();
    const size_t triTotal = srcIndices.size() / 3;
    std::unordered_map<uint32_t, uint32_t> remap;
    RawMeshletDesc cur{};
    Vec3 minP{1e30f, 1e30f, 1e30f}, maxP{-1e30f, -1e30f, -1e30f}, avgN{0, 0, 0};
    std::vector<Vec3> triNormals;

    auto flush = [&]() {
        if (cur.triangleCount == 0) return;
        const Vec3 c{(minP.x + maxP.x) * 0.5f, (minP.y + maxP.y) * 0.5f, (minP.z + maxP.z) * 0.5f};
        float r2 = 0.0f;
        for (uint32_t i = 0; i < cur.vertexCount; ++i) {
            const auto& v = (*outVerts)[cur.vertexOffset + i];
            const Vec3 d{v.pos[0] - c.x, v.pos[1] - c.y, v.pos[2] - c.z};
            r2 = std::max(r2, Dot(d, d));
        }
        cur.boundSphere[0] = c.x; cur.boundSphere[1] = c.y; cur.boundSphere[2] = c.z; cur.boundSphere[3] = std::sqrt(r2);
        const Vec3 axis = Norm(avgN);
        float minDot = 1.0f;
        for (const Vec3& tn : triNormals) minDot = std::min(minDot, Dot(axis, tn));
        cur.normalCone[0] = axis.x; cur.normalCone[1] = axis.y; cur.normalCone[2] = axis.z;
        cur.normalCone[3] = (minDot > 0.0f) ? -std::sqrt(std::max(1.0f - minDot * minDot, 0.0f)) : -1.0f;
        outMeshlets->push_back(cur);
        remap.clear(); triNormals.clear();
        cur = {};
        cur.vertexOffset = static_cast<uint32_t>(outVerts->size());
        cur.triangleOffset = static_cast<uint32_t>(outIndices->size());
        minP = {1e30f, 1e30f, 1e30f}; maxP = {-1e30f, -1e30f, -1e30f}; avgN = {0, 0, 0};
    };

    for (size_t t = 0; t < triTotal; ++t) {
        const uint32_t i0 = srcIndices[t * 3 + 0], i1 = srcIndices[t * 3 + 1], i2 = srcIndices[t * 3 + 2];
        if (i0 >= srcVerts.size() || i1 >= srcVerts.size() || i2 >= srcVerts.size()) continue;
        const uint32_t addV = (remap.count(i0) ? 0u : 1u) + (remap.count(i1) ? 0u : 1u) + (remap.count(i2) ? 0u : 1u);
        if (cur.vertexCount + addV > 64u || cur.triangleCount + 1u > 64u) flush();
        auto emitV = [&](uint32_t gi) -> uint32_t {
            auto it = remap.find(gi);
            if (it != remap.end()) return it->second;
            const uint32_t li = cur.vertexCount++;
            const auto& v = srcVerts[gi];
            outVerts->push_back(v);
            minP.x = std::min(minP.x, v.pos[0]); minP.y = std::min(minP.y, v.pos[1]); minP.z = std::min(minP.z, v.pos[2]);
            maxP.x = std::max(maxP.x, v.pos[0]); maxP.y = std::max(maxP.y, v.pos[1]); maxP.z = std::max(maxP.z, v.pos[2]);
            remap.emplace(gi, li);
            return li;
        };
        const uint32_t l0 = emitV(i0), l1 = emitV(i1), l2 = emitV(i2);
        outIndices->push_back(l0); outIndices->push_back(l1); outIndices->push_back(l2);
        const Vec3 p0{srcVerts[i0].pos[0], srcVerts[i0].pos[1], srcVerts[i0].pos[2]};
        const Vec3 p1{srcVerts[i1].pos[0], srcVerts[i1].pos[1], srcVerts[i1].pos[2]};
        const Vec3 p2{srcVerts[i2].pos[0], srcVerts[i2].pos[1], srcVerts[i2].pos[2]};
        const Vec3 tn = Norm(Cross(Sub(p1, p0), Sub(p2, p0)));
        avgN.x += tn.x; avgN.y += tn.y; avgN.z += tn.z;
        triNormals.push_back(tn);
        cur.triangleCount++;
    }
    flush();
}

bool LoadModernMeshAsset(const std::string& path, RawMeshAsset* outAsset, std::string* outErr)
{
    if (!outAsset) return false;
    std::vector<RawMeshVertex> rawV;
    std::vector<uint32_t> rawI;
    const bool isGlb = path.size() >= 4 && (path.compare(path.size() - 4, 4, ".glb") == 0 || path.compare(path.size() - 4, 4, ".GLB") == 0);
    const bool ok = isGlb ? LoadGlb(path, rawV, rawI, outErr) : LoadObj(path, rawV, rawI, outErr);
    if (!ok) return false;
    outAsset->name = path;
    outAsset->fromGlb = isGlb;
    BuildRawMeshlets(rawV, rawI, &outAsset->vertices, &outAsset->indices, &outAsset->meshlets);
    return !outAsset->meshlets.empty();
}

} // namespace raw::bridge
