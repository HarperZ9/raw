// CeNifReader.cpp: Clean-Room Skyrim SE / Fallout 4 NetImmerse (.nif v20.2.0.7 BSTriShape) Geometry Extractor.
// Original work, MIT.
#include "CeNifReader.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace raw::ce {
namespace {

template <class T>
bool Rd(const uint8_t* p, size_t sz, size_t& off, T* v) {
    if (off + sizeof(T) > sz) return false;
    std::memcpy(v, p + off, sizeof(T));
    off += sizeof(T);
    return true;
}

bool SkipShortStr(const uint8_t* p, size_t sz, size_t& off) {
    uint8_t n = 0;
    if (!Rd(p, sz, off, &n) || off + n > sz) return false;
    off += n;
    return true;
}

bool ReadSizedStr(const uint8_t* p, size_t sz, size_t& off, std::string* out) {
    uint32_t n = 0;
    if (!Rd(p, sz, off, &n) || n > 4096u || off + n > sz) return false;
    if (out) out->assign(reinterpret_cast<const char*>(p + off), n);
    off += n;
    return true;
}

float HalfToFloat(uint16_t h) {
    const uint32_t sign = uint32_t(h & 0x8000u) << 16;
    const uint32_t exp  = (h >> 10) & 0x1Fu;
    const uint32_t mant = h & 0x03FFu;
    uint32_t f = 0;
    if (exp == 0) {
        if (mant == 0) f = sign;
        else {
            uint32_t m = mant, e = 127u - 14u;
            while ((m & 0x0400u) == 0) { m <<= 1; --e; }
            m &= 0x03FFu;
            f = sign | (e << 23) | (m << 13);
        }
    } else if (exp == 31) {
        f = sign | 0x7F800000u | (mant << 13);
    } else {
        f = sign | ((exp + (127u - 15u)) << 23) | (mant << 13);
    }
    float out = 0.0f;
    std::memcpy(&out, &f, 4);
    return out;
}

// Parse one BSTriShape (SSE userVersion2 == 100) block starting at [blk, blk + blkSz).
bool ParseBsTriShapeBlock(const uint8_t* blk, size_t blkSz,
                          std::vector<raw::bridge::RawMeshVertex>& outV,
                          std::vector<uint32_t>& outI) {
    size_t off = 0;
    // NiObjectNET: name (int32 string idx), numExtraDataList (uint32) + refs (int32*n), controller (int32)
    int32_t nameIdx = 0, ctrl = 0;
    uint32_t numExtra = 0;
    if (!Rd(blk, blkSz, off, &nameIdx) || !Rd(blk, blkSz, off, &numExtra) || numExtra > 256u) return false;
    off += size_t(numExtra) * 4u;
    if (!Rd(blk, blkSz, off, &ctrl)) return false;

    // NiAVObject (SSE): flags (uint32), translation (12B), rotation (36B), scale (4B), collisionObject (4B) = 60 bytes
    if (off + 60u > blkSz) return false;
    float tr[3]{}, rot[9]{}, scale = 1.0f;
    std::memcpy(tr, blk + off + 4u, 12);
    std::memcpy(rot, blk + off + 16u, 36);
    std::memcpy(&scale, blk + off + 52u, 4);
    off += 60u;

    // BSTriShape header: boundSphere (16B), skin (4B), shaderProp (4B), alphaProp (4B), vertexDesc (8B),
    // numTriangles (uint16), numVertices (uint16), dataSize (uint32)
    if (off + 44u > blkSz) return false;
    off += 28u;  // skip boundSphere(16) + skin(4) + shaderProp(4) + alphaProp(4)
    uint64_t vdesc = 0;
    uint16_t numTris = 0, numVerts = 0;
    uint32_t dataSize = 0;
    if (!Rd(blk, blkSz, off, &vdesc) || !Rd(blk, blkSz, off, &numTris) ||
        !Rd(blk, blkSz, off, &numVerts) || !Rd(blk, blkSz, off, &dataSize)) return false;

    if (numVerts == 0 || numTris == 0 || dataSize == 0 || off + dataSize > blkSz) return false;

    // SSE BSTriShape VertexDesc bit layout:
    //   bits 0..3:   vertexSize in 4-byte units
    //   bits 44..55: vertex flags (VF_VERTEX=0x01, VF_UV=0x02, VF_NORMAL=0x08, VF_TANGENT=0x10, VF_COLORS=0x20, VF_SKINNED=0x40, VF_FULL_PRECISION=0x400)
    const uint32_t stride = uint32_t(vdesc & 0xFu) * 4u;
    const uint32_t vFlags = uint32_t((vdesc >> 44u) & 0xFFFu);
    const bool hasPos  = (vFlags & 0x001u) != 0;
    const bool hasUv   = (vFlags & 0x002u) != 0;
    const bool hasNrm  = (vFlags & 0x008u) != 0;
    const bool fullPos = (vFlags & 0x400u) != 0;
    const size_t vbBytes = size_t(numVerts) * stride;
    const size_t ibBytes = size_t(numTris) * 6u;
    if (!hasPos || stride < 8u || vbBytes + ibBytes > dataSize) return false;

    // Attribute offsets packed sequentially in SSE BSTriShape:
    // Position: 12B (full) or 8B (4x half: x,y,z,bitangentX) -> then optional 4B UV (2x half) -> optional 4B Normal (3x snorm8 + bitangentY)
    const size_t posBytes = fullPos ? 16u : 8u;   // full precision stores float3 + float bitangentX (16B) when normal/tangent present, or 12B
    const size_t uvOff    = (fullPos && !hasNrm) ? 12u : posBytes;
    const size_t nrmOff   = uvOff + (hasUv ? 4u : 0u);

    const uint32_t baseV = static_cast<uint32_t>(outV.size());
    outV.reserve(baseV + numVerts);
    const uint8_t* vb = blk + off;
    for (uint16_t i = 0; i < numVerts; ++i) {
        const uint8_t* v = vb + size_t(i) * stride;
        raw::bridge::RawMeshVertex rv{};
        float px = 0, py = 0, pz = 0;
        if (fullPos && stride >= 12u) {
            std::memcpy(&px, v + 0, 4); std::memcpy(&py, v + 4, 4); std::memcpy(&pz, v + 8, 4);
        } else {
            uint16_t hx = 0, hy = 0, hz = 0;
            std::memcpy(&hx, v + 0, 2); std::memcpy(&hy, v + 2, 2); std::memcpy(&hz, v + 4, 2);
            px = HalfToFloat(hx); py = HalfToFloat(hy); pz = HalfToFloat(hz);
        }
        rv.pos[0] = (rot[0] * px + rot[1] * py + rot[2] * pz) * scale + tr[0];
        rv.pos[1] = (rot[3] * px + rot[4] * py + rot[5] * pz) * scale + tr[1];
        rv.pos[2] = (rot[6] * px + rot[7] * py + rot[8] * pz) * scale + tr[2];
        if (hasUv && uvOff + 4u <= stride) {
            uint16_t hu = 0, hv = 0;
            std::memcpy(&hu, v + uvOff, 2); std::memcpy(&hv, v + uvOff + 2, 2);
            rv.uv[0] = HalfToFloat(hu); rv.uv[1] = HalfToFloat(hv);
        }
        if (hasNrm && nrmOff + 3u <= stride) {
            const float nx = (float(v[nrmOff + 0]) / 255.0f) * 2.0f - 1.0f;
            const float ny = (float(v[nrmOff + 1]) / 255.0f) * 2.0f - 1.0f;
            const float nz = (float(v[nrmOff + 2]) / 255.0f) * 2.0f - 1.0f;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-5f) { rv.normal[0] = nx / len; rv.normal[1] = ny / len; rv.normal[2] = nz / len; }
            else { rv.normal[2] = 1.0f; }
        } else {
            rv.normal[2] = 1.0f;
        }
        outV.push_back(rv);
    }

    const uint8_t* ib = vb + vbBytes;
    outI.reserve(outI.size() + size_t(numTris) * 3u);
    for (uint32_t t = 0; t < uint32_t(numTris) * 3u; ++t) {
        uint16_t idx = 0;
        std::memcpy(&idx, ib + size_t(t) * 2u, 2);
        if (idx < numVerts) outI.push_back(baseV + idx);
    }
    return true;
}

}  // namespace

bool LoadNifMeshBytes(const uint8_t* bytes, size_t size, const std::string& label,
                      raw::bridge::RawMeshAsset* outAsset, NifSummary* outSummary, std::string* outErr) {
    if (!bytes || size < 64) { if (outErr) *outErr = "NIF buffer too small"; return false; }
    // Header line ends with '\n' (e.g. "Gamebryo File Format, Version 20.2.0.7\n")
    size_t nl = 0;
    while (nl < size && nl < 128 && bytes[nl] != '\n') ++nl;
    if (nl >= size || nl < 16 || std::memcmp(bytes, "Gamebryo File Format", 20) != 0) {
        if (outErr) *outErr = "invalid NetImmerse/Gamebryo NIF header";
        return false;
    }
    size_t off = nl + 1;
    uint32_t version = 0, userVer = 0, numBlocks = 0, userVer2 = 0;
    uint8_t endian = 1;
    if (!Rd(bytes, size, off, &version) || !Rd(bytes, size, off, &endian) ||
        !Rd(bytes, size, off, &userVer) || !Rd(bytes, size, off, &numBlocks) ||
        !Rd(bytes, size, off, &userVer2)) {
        if (outErr) *outErr = "truncated NIF header fields";
        return false;
    }
    if (version != 0x14020007u || numBlocks == 0 || numBlocks > 65536u) {
        if (outErr) *outErr = "unsupported NIF version (expected 20.2.0.7)";
        return false;
    }
    // Export info: Author, Process Script, Export Script (3 short strings)
    if (!SkipShortStr(bytes, size, off) || !SkipShortStr(bytes, size, off) || !SkipShortStr(bytes, size, off)) {
        if (outErr) *outErr = "truncated NIF export info";
        return false;
    }
    uint16_t numBlockTypes = 0;
    if (!Rd(bytes, size, off, &numBlockTypes) || numBlockTypes == 0) {
        if (outErr) *outErr = "invalid NIF block types";
        return false;
    }
    std::vector<std::string> blockTypes(numBlockTypes);
    for (uint16_t i = 0; i < numBlockTypes; ++i) {
        if (!ReadSizedStr(bytes, size, off, &blockTypes[i])) return false;
    }
    std::vector<uint16_t> blockTypeIdx(numBlocks);
    for (uint32_t i = 0; i < numBlocks; ++i) {
        if (!Rd(bytes, size, off, &blockTypeIdx[i]) || blockTypeIdx[i] >= numBlockTypes) return false;
    }
    std::vector<uint32_t> blockSizes(numBlocks);
    for (uint32_t i = 0; i < numBlocks; ++i) {
        if (!Rd(bytes, size, off, &blockSizes[i])) return false;
    }
    // Global string table: numStrings (uint32), maxStringLen (uint32), strings[numStrings]
    uint32_t numStrings = 0, maxStrLen = 0;
    if (!Rd(bytes, size, off, &numStrings) || !Rd(bytes, size, off, &maxStrLen)) return false;
    for (uint32_t i = 0; i < numStrings; ++i) {
        if (!ReadSizedStr(bytes, size, off, nullptr)) return false;
    }
    // Groups: numGroups (uint32) + groups[numGroups]
    uint32_t numGroups = 0;
    if (!Rd(bytes, size, off, &numGroups) || off + size_t(numGroups) * 4u > size) return false;
    off += size_t(numGroups) * 4u;

    NifSummary sum{};
    sum.version = version;
    sum.userVersion = userVer;
    sum.userVersion2 = userVer2;
    sum.numBlocks = numBlocks;

    std::vector<raw::bridge::RawMeshVertex> rawV;
    std::vector<uint32_t> rawI;

    // Walk blocks directly via blockSizes[i] (O(1) skip over non-geometry blocks!):
    for (uint32_t i = 0; i < numBlocks; ++i) {
        const uint32_t bsz = blockSizes[i];
        if (off + bsz > size) { if (outErr) *outErr = "truncated NIF block payload"; return false; }
        const std::string& bt = blockTypes[blockTypeIdx[i]];
        if (bt == "BSTriShape" || bt == "BSMeshLODTriShape") {
            if (ParseBsTriShapeBlock(bytes + off, bsz, rawV, rawI)) {
                ++sum.triShapeBlocks;
            }
        }
        off += bsz;
    }

    sum.totalVertices = static_cast<uint32_t>(rawV.size());
    sum.totalTriangles = static_cast<uint32_t>(rawI.size() / 3u);
    if (outSummary) *outSummary = sum;

    if (rawV.empty() || rawI.size() < 3) {
        if (outErr) *outErr = "NIF contained no decodable BSTriShape triangles";
        return false;
    }
    if (outAsset) {
        outAsset->name = label;
        outAsset->fromGlb = false;
        outAsset->materials.clear();
        raw::bridge::BuildRawMeshlets(rawV, rawI, &outAsset->vertices, &outAsset->indices, &outAsset->meshlets);
    }
    return true;
}

}  // namespace raw::ce
