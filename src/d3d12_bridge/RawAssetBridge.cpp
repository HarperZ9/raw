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

// Zero-dependency minimal JSON DOM for spec-compliant glTF 2.0 .glb parsing.
struct JVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    double num = 0.0;
    std::string str;
    std::vector<JVal> arr;
    std::unordered_map<std::string, JVal> obj;

    const JVal* get(const char* k) const {
        if (type != Obj) return nullptr;
        auto it = obj.find(k);
        return it == obj.end() ? nullptr : &it->second;
    }
    int64_t asInt(int64_t def = -1) const { return type == Num ? static_cast<int64_t>(num) : def; }
    float asFloat(float def = 0.0f) const { return type == Num ? static_cast<float>(num) : def; }
};

struct JParser {
    const char* p = nullptr;
    const char* end = nullptr;

    void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p; }
    bool parseStr(std::string& out) {
        ws();
        if (p >= end || *p != '"') return false;
        ++p;
        out.clear();
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) { ++p; out.push_back(*p++); }
            else out.push_back(*p++);
        }
        if (p >= end || *p != '"') return false;
        ++p;
        return true;
    }
    bool parseVal(JVal& v, int depth = 0) {
        if (depth > 32) return false;
        ws();
        if (p >= end) return false;
        if (*p == '{') {
            v.type = JVal::Obj; ++p; ws();
            if (p < end && *p == '}') { ++p; return true; }
            while (p < end) {
                std::string key;
                if (!parseStr(key)) return false;
                ws(); if (p >= end || *p != ':') return false; ++p;
                JVal child;
                if (!parseVal(child, depth + 1)) return false;
                v.obj.emplace(std::move(key), std::move(child));
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == '}') { ++p; return true; }
                return false;
            }
            return false;
        }
        if (*p == '[') {
            v.type = JVal::Arr; ++p; ws();
            if (p < end && *p == ']') { ++p; return true; }
            while (p < end) {
                JVal child;
                if (!parseVal(child, depth + 1)) return false;
                v.arr.push_back(std::move(child));
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == ']') { ++p; return true; }
                return false;
            }
            return false;
        }
        if (*p == '"') { v.type = JVal::Str; return parseStr(v.str); }
        if (end - p >= 4 && std::memcmp(p, "true", 4) == 0)  { v.type = JVal::Bool; v.num = 1.0; p += 4; return true; }
        if (end - p >= 5 && std::memcmp(p, "false", 5) == 0) { v.type = JVal::Bool; v.num = 0.0; p += 5; return true; }
        if (end - p >= 4 && std::memcmp(p, "null", 4) == 0)  { v.type = JVal::Null; p += 4; return true; }
        char* next = nullptr;
        v.num = std::strtod(p, &next);
        if (next == p || next > end) return false;
        v.type = JVal::Num;
        p = next;
        return true;
    }
};

struct GlbAccessorSlice {
    const uint8_t* ptr = nullptr;
    size_t         stride = 0;
    uint32_t       count = 0;
    uint32_t       compType = 0;
};

bool ResolveAccessor(const JVal& root, const uint8_t* binPtr, size_t binSize, int64_t accIdx,
                     size_t elemBytes, GlbAccessorSlice* out)
{
    const JVal* accs = root.get("accessors");
    const JVal* bvs = root.get("bufferViews");
    if (!accs || !bvs || accIdx < 0 || size_t(accIdx) >= accs->arr.size()) return false;
    const JVal& acc = accs->arr[size_t(accIdx)];
    const int64_t bvIdx = acc.get("bufferView") ? acc.get("bufferView")->asInt(-1) : -1;
    const int64_t count = acc.get("count") ? acc.get("count")->asInt(0) : 0;
    const int64_t comp  = acc.get("componentType") ? acc.get("componentType")->asInt(0) : 0;
    const int64_t aOff  = acc.get("byteOffset") ? acc.get("byteOffset")->asInt(0) : 0;
    if (bvIdx < 0 || size_t(bvIdx) >= bvs->arr.size() || count <= 0 || aOff < 0) return false;
    const JVal& bv = bvs->arr[size_t(bvIdx)];
    const int64_t bvOff = bv.get("byteOffset") ? bv.get("byteOffset")->asInt(0) : 0;
    const int64_t bvLen = bv.get("byteLength") ? bv.get("byteLength")->asInt(0) : 0;
    const int64_t bvStr = bv.get("byteStride") ? bv.get("byteStride")->asInt(0) : 0;
    const size_t stride = bvStr > 0 ? size_t(bvStr) : elemBytes;
    if (bvOff < 0 || bvLen <= 0 || stride < elemBytes) return false;
    const size_t start = size_t(bvOff) + size_t(aOff);
    const size_t need = (size_t(count) - 1u) * stride + elemBytes;
    if (start + need > binSize || size_t(aOff) + need > size_t(bvLen)) return false;
    out->ptr = binPtr + start;
    out->stride = stride;
    out->count = static_cast<uint32_t>(count);
    out->compType = static_cast<uint32_t>(comp);
    return true;
}

// Spec-compliant glTF 2.0 binary (.glb) parser: bufferViews, strided accessors, multi-primitive merge,
// u8/u16/u32 indices, POSITION/NORMAL/TEXCOORD_0, smooth auto-normals, Y-up->Z-up transform, PBR materials & embedded images.
bool LoadGlbBytes(const uint8_t* bytes, size_t size, std::vector<RawMeshVertex>& outV, std::vector<uint32_t>& outI,
                  std::vector<RawPbrMaterial>& outMats, bool yUpToZUp, std::string* err)
{
    if (!bytes || size < 20) { if (err) *err = ".glb too small"; return false; }
    uint32_t magic = 0, version = 0, totalLen = 0;
    std::memcpy(&magic, bytes, 4);
    std::memcpy(&version, bytes + 4, 4);
    std::memcpy(&totalLen, bytes + 8, 4);
    if (magic != 0x46546C67u || version != 2u || totalLen > size || totalLen < 20u) {
        if (err) *err = "invalid glTF 2.0 .glb header";
        return false;
    }
    size_t off = 12;
    const char* jsonPtr = nullptr;
    size_t jsonLen = 0;
    const uint8_t* binPtr = nullptr;
    size_t binSize = 0;
    while (off + 8 <= totalLen) {
        uint32_t cLen = 0, cType = 0;
        std::memcpy(&cLen, bytes + off, 4);
        std::memcpy(&cType, bytes + off + 4, 4);
        off += 8;
        if (off + cLen > totalLen) { if (err) *err = ".glb truncated chunk"; return false; }
        if (cType == 0x4E4F534Au) { jsonPtr = reinterpret_cast<const char*>(bytes + off); jsonLen = cLen; }
        else if (cType == 0x004E4942u) { binPtr = bytes + off; binSize = cLen; }
        off += cLen;
    }
    if (!jsonPtr || !binPtr || jsonLen == 0 || binSize == 0) {
        if (err) *err = ".glb missing JSON or BIN chunk";
        return false;
    }
    JVal root;
    JParser jp{jsonPtr, jsonPtr + jsonLen};
    if (!jp.parseVal(root) || root.type != JVal::Obj) {
        if (err) *err = ".glb JSON parse error";
        return false;
    }

    // Parse PBR materials + optional embedded baseColor image bytes from images[*].bufferView.
    outMats.clear();
    const JVal* jMats = root.get("materials");
    const JVal* jTexs = root.get("textures");
    const JVal* jImgs = root.get("images");
    const JVal* jBvs  = root.get("bufferViews");
    if (jMats && jMats->type == JVal::Arr) {
        for (const JVal& jm : jMats->arr) {
            RawPbrMaterial m{};
            if (const JVal* nm = jm.get("name"); nm && nm->type == JVal::Str) m.name = nm->str;
            if (const JVal* pbr = jm.get("pbrMetallicRoughness")) {
                if (const JVal* bcf = pbr->get("baseColorFactor"); bcf && bcf->arr.size() >= 4) {
                    for (int k = 0; k < 4; ++k) m.baseColorFactor[k] = bcf->arr[k].asFloat(1.0f);
                }
                if (const JVal* mf = pbr->get("metallicFactor"))  m.metallicFactor = mf->asFloat(0.0f);
                if (const JVal* rf = pbr->get("roughnessFactor")) m.roughnessFactor = rf->asFloat(0.65f);
                if (const JVal* bct = pbr->get("baseColorTexture")) {
                    const int64_t ti = bct->get("index") ? bct->get("index")->asInt(-1) : -1;
                    if (jTexs && jImgs && jBvs && ti >= 0 && size_t(ti) < jTexs->arr.size()) {
                        const int64_t ii = jTexs->arr[size_t(ti)].get("source") ? jTexs->arr[size_t(ti)].get("source")->asInt(-1) : -1;
                        if (ii >= 0 && size_t(ii) < jImgs->arr.size()) {
                            const JVal& img = jImgs->arr[size_t(ii)];
                            const int64_t bvi = img.get("bufferView") ? img.get("bufferView")->asInt(-1) : -1;
                            if (bvi >= 0 && size_t(bvi) < jBvs->arr.size()) {
                                const JVal& bv = jBvs->arr[size_t(bvi)];
                                const int64_t bOff = bv.get("byteOffset") ? bv.get("byteOffset")->asInt(0) : 0;
                                const int64_t bLen = bv.get("byteLength") ? bv.get("byteLength")->asInt(0) : 0;
                                if (bOff >= 0 && bLen > 0 && size_t(bOff + bLen) <= binSize) {
                                    m.embeddedBaseColorImage.assign(binPtr + bOff, binPtr + bOff + bLen);
                                    if (const JVal* mt = img.get("mimeType"); mt && mt->type == JVal::Str) m.embeddedBaseColorMime = mt->str;
                                }
                            }
                        }
                    }
                }
            }
            if (const JVal* ef = jm.get("emissiveFactor"); ef && ef->arr.size() >= 3) {
                for (int k = 0; k < 3; ++k) m.emissiveFactor[k] = ef->arr[k].asFloat(0.0f);
            }
            outMats.push_back(std::move(m));
        }
    }

    const JVal* meshes = root.get("meshes");
    if (!meshes || meshes->type != JVal::Arr || meshes->arr.empty()) {
        if (err) *err = ".glb contains no meshes";
        return false;
    }

    outV.clear();
    outI.clear();
    for (const JVal& mesh : meshes->arr) {
        const JVal* prims = mesh.get("primitives");
        if (!prims || prims->type != JVal::Arr) continue;
        for (const JVal& prim : prims->arr) {
            const JVal* attrs = prim.get("attributes");
            if (!attrs || !attrs->get("POSITION")) continue;
            GlbAccessorSlice posSl{}, nrmSl{}, uvSl{};
            if (!ResolveAccessor(root, binPtr, binSize, attrs->get("POSITION")->asInt(-1), 12, &posSl) || posSl.compType != 5126u) {
                if (err) *err = ".glb invalid POSITION accessor";
                return false;
            }
            const bool hasNrm = attrs->get("NORMAL") && ResolveAccessor(root, binPtr, binSize, attrs->get("NORMAL")->asInt(-1), 12, &nrmSl) && nrmSl.compType == 5126u && nrmSl.count == posSl.count;
            const bool hasUv  = attrs->get("TEXCOORD_0") && ResolveAccessor(root, binPtr, binSize, attrs->get("TEXCOORD_0")->asInt(-1), 8, &uvSl) && uvSl.compType == 5126u && uvSl.count == posSl.count;

            const uint32_t baseV = static_cast<uint32_t>(outV.size());
            outV.resize(baseV + posSl.count);
            for (uint32_t i = 0; i < posSl.count; ++i) {
                float p[3]{};
                std::memcpy(p, posSl.ptr + size_t(i) * posSl.stride, 12);
                RawMeshVertex& rv = outV[baseV + i];
                if (yUpToZUp) { rv.pos[0] = p[0]; rv.pos[1] = -p[2]; rv.pos[2] = p[1]; }
                else          { rv.pos[0] = p[0]; rv.pos[1] =  p[1]; rv.pos[2] = p[2]; }
                if (hasNrm) {
                    float n[3]{};
                    std::memcpy(n, nrmSl.ptr + size_t(i) * nrmSl.stride, 12);
                    const Vec3 nv = yUpToZUp ? Norm({n[0], -n[2], n[1]}) : Norm({n[0], n[1], n[2]});
                    rv.normal[0] = nv.x; rv.normal[1] = nv.y; rv.normal[2] = nv.z;
                } else {
                    rv.normal[0] = 0.0f; rv.normal[1] = 0.0f; rv.normal[2] = 0.0f;
                }
                if (hasUv) {
                    std::memcpy(rv.uv, uvSl.ptr + size_t(i) * uvSl.stride, 8);
                } else {
                    rv.uv[0] = 0.0f; rv.uv[1] = 0.0f;
                }
            }

            const size_t baseI = outI.size();
            if (const JVal* idxJ = prim.get("indices")) {
                const int64_t idxAcc = idxJ->asInt(-1);
                const JVal* accs = root.get("accessors");
                const uint32_t ct = (accs && idxAcc >= 0 && size_t(idxAcc) < accs->arr.size() && accs->arr[size_t(idxAcc)].get("componentType"))
                    ? static_cast<uint32_t>(accs->arr[size_t(idxAcc)].get("componentType")->asInt(0)) : 0u;
                const size_t ibBytes = (ct == 5121u) ? 1u : (ct == 5123u ? 2u : (ct == 5125u ? 4u : 0u));
                GlbAccessorSlice idxSl{};
                if (ibBytes == 0 || !ResolveAccessor(root, binPtr, binSize, idxAcc, ibBytes, &idxSl)) {
                    if (err) *err = ".glb invalid indices accessor";
                    return false;
                }
                outI.reserve(baseI + idxSl.count);
                for (uint32_t i = 0; i < idxSl.count; ++i) {
                    uint32_t vi = 0;
                    const uint8_t* ip = idxSl.ptr + size_t(i) * idxSl.stride;
                    if (ibBytes == 1) vi = *ip;
                    else if (ibBytes == 2) { uint16_t u16 = 0; std::memcpy(&u16, ip, 2); vi = u16; }
                    else { std::memcpy(&vi, ip, 4); }
                    if (vi < posSl.count) outI.push_back(baseV + vi);
                }
            } else {
                const uint32_t n = posSl.count - (posSl.count % 3u);
                for (uint32_t i = 0; i < n; ++i) outI.push_back(baseV + i);
            }

            // Compute area-weighted smooth vertex normals when NORMAL accessor is absent.
            if (!hasNrm) {
                for (size_t t = baseI; t + 2 < outI.size(); t += 3) {
                    const uint32_t i0 = outI[t], i1 = outI[t + 1], i2 = outI[t + 2];
                    const Vec3 p0{outV[i0].pos[0], outV[i0].pos[1], outV[i0].pos[2]};
                    const Vec3 p1{outV[i1].pos[0], outV[i1].pos[1], outV[i1].pos[2]};
                    const Vec3 p2{outV[i2].pos[0], outV[i2].pos[1], outV[i2].pos[2]};
                    const Vec3 fn = Cross(Sub(p1, p0), Sub(p2, p0));
                    for (uint32_t k : {i0, i1, i2}) {
                        outV[k].normal[0] += fn.x; outV[k].normal[1] += fn.y; outV[k].normal[2] += fn.z;
                    }
                }
                for (uint32_t i = baseV; i < baseV + posSl.count; ++i) {
                    const Vec3 nv = Norm({outV[i].normal[0], outV[i].normal[1], outV[i].normal[2]});
                    outV[i].normal[0] = nv.x; outV[i].normal[1] = nv.y; outV[i].normal[2] = nv.z;
                }
            }
        }
    }
    if (outV.empty() || outI.size() < 3) { if (err) *err = ".glb yielded no triangles"; return false; }
    return true;
}

bool LoadGlb(const std::string& path, std::vector<RawMeshVertex>& outV, std::vector<uint32_t>& outI,
             std::vector<RawPbrMaterial>& outMats, std::string* err)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "failed to open .glb: " + path; return false; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return LoadGlbBytes(bytes.data(), bytes.size(), outV, outI, outMats, true, err);
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

bool LoadModernMeshAssetFromGlbBytes(const uint8_t* bytes, size_t size, const std::string& label,
                                     RawMeshAsset* outAsset, std::string* outErr, bool yUpToZUp)
{
    if (!outAsset) return false;
    std::vector<RawMeshVertex> rawV;
    std::vector<uint32_t> rawI;
    outAsset->materials.clear();
    if (!LoadGlbBytes(bytes, size, rawV, rawI, outAsset->materials, yUpToZUp, outErr)) return false;
    outAsset->name = label;
    outAsset->fromGlb = true;
    BuildRawMeshlets(rawV, rawI, &outAsset->vertices, &outAsset->indices, &outAsset->meshlets);
    return !outAsset->meshlets.empty();
}

bool LoadModernMeshAsset(const std::string& path, RawMeshAsset* outAsset, std::string* outErr)
{
    if (!outAsset) return false;
    std::vector<RawMeshVertex> rawV;
    std::vector<uint32_t> rawI;
    outAsset->materials.clear();
    const bool isGlb = path.size() >= 4 && (path.compare(path.size() - 4, 4, ".glb") == 0 || path.compare(path.size() - 4, 4, ".GLB") == 0);
    const bool ok = isGlb ? LoadGlb(path, rawV, rawI, outAsset->materials, outErr) : LoadObj(path, rawV, rawI, outErr);
    if (!ok) return false;
    outAsset->name = path;
    outAsset->fromGlb = isGlb;
    BuildRawMeshlets(rawV, rawI, &outAsset->vertices, &outAsset->indices, &outAsset->meshlets);
    return !outAsset->meshlets.empty();
}

} // namespace raw::bridge
