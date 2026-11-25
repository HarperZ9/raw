// RawNifLoader.cpp: 100% Clean-Room Skyrim SE `.NIF` v20.2.0.7 Scene-Graph & BSTriShape Parser (`raw::worldgen`).
// Zero Bethesda SDK / third-party / platform dependencies. Original work, MIT.

#include "RawNifLoader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <unordered_map>

namespace raw::worldgen {
namespace {

void SetErr(std::string* outErr, const std::string& msg) {
    if (outErr) *outErr = msg;
}

template <class T>
bool Rd(const uint8_t* p, size_t sz, size_t& off, T* out) {
    if (off + sizeof(T) > sz) return false;
    std::memcpy(out, p + off, sizeof(T));
    off += sizeof(T);
    return true;
}

bool SkipShortStr(const uint8_t* p, size_t sz, size_t& off) {
    uint8_t n = 0u;
    if (!Rd(p, sz, off, &n) || off + size_t(n) > sz) return false;
    off += size_t(n);
    return true;
}

bool ReadSizedStr(const uint8_t* p, size_t sz, size_t& off, std::string* out) {
    uint32_t n = 0u;
    if (!Rd(p, sz, off, &n) || n > 16384u || off + size_t(n) > sz) return false;
    if (out) {
        size_t len = n;
        while (len > 0u && p[off + len - 1u] == 0u) --len;
        out->assign(reinterpret_cast<const char*>(p + off), len);
    }
    off += size_t(n);
    return true;
}

std::string LookupString(const std::vector<std::string>& strings, int32_t idx) {
    if (idx < 0 || static_cast<size_t>(idx) >= strings.size()) return {};
    return strings[static_cast<size_t>(idx)];
}

std::string ToLowerAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c == '\\') c = '/';
        else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

inline bool IsFiniteVec3(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

inline Vec3 SafeUnitNormal(Vec3 v) {
    if (!IsFiniteVec3(v)) return {0.0f, 0.0f, 1.0f};
    const float l = Length(v);
    if (!std::isfinite(l) || l < 1.0e-5f) return {0.0f, 0.0f, 1.0f};
    return {v.x / l, v.y / l, v.z / l};
}

bool IsNodeBlockType(const std::string& bt) {
    return bt == "NiNode" || bt == "BSFadeNode" || bt == "BSMultiBoundNode" ||
           bt == "BSLeafAnimNode" || bt == "BSTreeNode" || bt == "BSOrderedNode" ||
           bt == "BSValueNode" || bt == "NiBillboardNode" || bt == "NiSwitchNode";
}

bool IsTriShapeBlockType(const std::string& bt) {
    return bt == "BSTriShape" || bt == "BSDynamicTriShape" ||
           bt == "BSSubIndexTriShape" || bt == "BSMeshLODTriShape";
}

bool ParseNiAvObjectPrefix(const uint8_t* blk, size_t blkSz, uint32_t userVer2,
                           const std::vector<std::string>& strings,
                           size_t& off, std::string* outName, uint32_t* outFlags,
                           NifTransform* outTransform) {
    int32_t nameIdx = -1;
    uint32_t numExtra = 0u;
    int32_t controllerRef = -1;
    if (!Rd(blk, blkSz, off, &nameIdx) || !Rd(blk, blkSz, off, &numExtra) || numExtra > 512u) return false;
    if (off + size_t(numExtra) * 4u > blkSz) return false;
    off += size_t(numExtra) * 4u;
    if (!Rd(blk, blkSz, off, &controllerRef)) return false;

    uint32_t flags = 0u;
    if (userVer2 >= 100u) {
        if (!Rd(blk, blkSz, off, &flags)) return false;
    } else {
        uint16_t flags16 = 0u;
        if (!Rd(blk, blkSz, off, &flags16)) return false;
        flags = flags16;
    }

    NifTransform xf{};
    float tr[3]{};
    if (!Rd(blk, blkSz, off, &tr[0]) || !Rd(blk, blkSz, off, &tr[1]) || !Rd(blk, blkSz, off, &tr[2])) return false;
    xf.translation = {tr[0], tr[1], tr[2]};
    for (int i = 0; i < 9; ++i) {
        if (!Rd(blk, blkSz, off, &xf.rotation[i])) return false;
    }
    if (!Rd(blk, blkSz, off, &xf.scale)) return false;
    if (!std::isfinite(xf.scale) || std::fabs(xf.scale) < 1.0e-5f) xf.scale = 1.0f;

    // In SSE (userVersion2 >= 34), properties array was removed; collisionObject (int32) follows directly.
    int32_t collisionRef = -1;
    if (!Rd(blk, blkSz, off, &collisionRef)) return false;

    if (outName) *outName = LookupString(strings, nameIdx);
    if (outFlags) *outFlags = flags;
    if (outTransform) *outTransform = xf;
    return true;
}

bool ParseNodeBlock(const uint8_t* blk, size_t blkSz, int32_t blockIdx, const std::string& blockType,
                    uint32_t userVer2, const std::vector<std::string>& strings, NifNodeInfo* outNode) {
    size_t off = 0u;
    NifNodeInfo node{};
    node.blockIndex = blockIdx;
    node.blockType  = blockType;
    if (!ParseNiAvObjectPrefix(blk, blkSz, userVer2, strings, off, &node.name, &node.flags, &node.localTransform)) {
        return false;
    }
    uint32_t numChildren = 0u;
    if (!Rd(blk, blkSz, off, &numChildren) || numChildren > 4096u || off + size_t(numChildren) * 4u > blkSz) {
        return false;
    }
    node.children.reserve(numChildren);
    for (uint32_t i = 0u; i < numChildren; ++i) {
        int32_t childRef = -1;
        if (!Rd(blk, blkSz, off, &childRef)) return false;
        if (childRef >= 0) node.children.push_back(childRef);
    }
    *outNode = std::move(node);
    return true;
}

bool ParseTextureSetBlock(const uint8_t* blk, size_t blkSz, int32_t blockIdx, NifTextureSet* outSet) {
    size_t off = 0u;
    int32_t numTex = 0;
    if (!Rd(blk, blkSz, off, &numTex) || numTex < 0 || numTex > 64) return false;
    NifTextureSet set{};
    set.blockIndex = blockIdx;
    set.textures.resize(static_cast<size_t>(numTex));
    for (int32_t i = 0; i < numTex; ++i) {
        if (!ReadSizedStr(blk, blkSz, off, &set.textures[static_cast<size_t>(i)])) return false;
    }
    if (set.textures.size() > 0u) set.diffusePath  = set.textures[0];
    if (set.textures.size() > 1u) set.normalPath   = set.textures[1];
    if (set.textures.size() > 5u) set.specularPath = set.textures[5];
    *outSet = std::move(set);
    return true;
}

// Extracts textureSetRef from a BSLightingShaderProperty (SSE userVersion2 == 100).
bool ParseLightingShaderPropTexSetRef(const uint8_t* blk, size_t blkSz, int32_t* outTexSetRef) {
    size_t off = 0u;
    // skyrimShaderType (uint32) + NiObjectNET (nameIdx int32, numExtra uint32, extra[n], controller int32)
    uint32_t shaderType = 0u;
    int32_t nameIdx = -1, ctrlRef = -1;
    uint32_t numExtra = 0u;
    if (!Rd(blk, blkSz, off, &shaderType) || !Rd(blk, blkSz, off, &nameIdx) ||
        !Rd(blk, blkSz, off, &numExtra) || numExtra > 256u) {
        return false;
    }
    if (off + size_t(numExtra) * 4u > blkSz) return false;
    off += size_t(numExtra) * 4u;
    if (!Rd(blk, blkSz, off, &ctrlRef)) return false;

    // shaderFlags1 (4) + shaderFlags2 (4) + uvOffset (8) + uvScale (8) + textureSetRef (4) = 28 bytes
    if (off + 28u > blkSz) return false;
    off += 24u;
    int32_t texSetRef = -1;
    if (!Rd(blk, blkSz, off, &texSetRef)) return false;
    *outTexSetRef = texSetRef;
    return true;
}

bool ParseTriShapeBlock(const uint8_t* blk, size_t blkSz, int32_t blockIdx, const std::string& blockType,
                        uint32_t userVer2, const std::vector<std::string>& strings, NifTriShape* outShape) {
    size_t off = 0u;
    NifTriShape shape{};
    shape.blockIndex = blockIdx;
    shape.blockType  = blockType;
    uint32_t flags = 0u;
    if (!ParseNiAvObjectPrefix(blk, blkSz, userVer2, strings, off, &shape.name, &flags, &shape.localTransform)) {
        return false;
    }

    // BSTriShape header: boundSphere (16B), skinRef (4B), shaderPropRef (4B), alphaPropRef (4B),
    // vertexDesc (8B), numTriangles (uint16 for SSE v100, uint32 for FO4 v130), numVertices (uint16), dataSize (uint32)
    if (off + 36u > blkSz) return false;
    off += 16u; // bounding sphere
    int32_t skinRef = -1, shaderRef = -1, alphaRef = -1;
    if (!Rd(blk, blkSz, off, &skinRef) || !Rd(blk, blkSz, off, &shaderRef) || !Rd(blk, blkSz, off, &alphaRef)) {
        return false;
    }
    shape.shaderPropertyRef = shaderRef;

    uint64_t vdesc = 0ull;
    uint32_t numTris = 0u;
    uint16_t numVerts = 0u;
    uint32_t dataSize = 0u;
    if (!Rd(blk, blkSz, off, &vdesc)) return false;
    if (userVer2 >= 130u) {
        if (!Rd(blk, blkSz, off, &numTris)) return false;
    } else {
        uint16_t numTris16 = 0u;
        if (!Rd(blk, blkSz, off, &numTris16)) return false;
        numTris = numTris16;
    }
    if (!Rd(blk, blkSz, off, &numVerts) || !Rd(blk, blkSz, off, &dataSize)) return false;
    if (numVerts == 0u || numTris == 0u || dataSize == 0u || off + size_t(dataSize) > blkSz) {
        return false;
    }

    const uint32_t stride = uint32_t(vdesc & 0xFu) * 4u;
    const uint32_t vFlags = uint32_t((vdesc >> 44u) & 0x7FFu);
    const bool hasPos  = (vFlags & kNifVfVertex) != 0u;
    const bool hasUv   = (vFlags & kNifVfUv) != 0u;
    const bool hasNrm  = (vFlags & kNifVfNormal) != 0u;
    const bool hasTan  = (vFlags & kNifVfTangent) != 0u;
    const bool hasCol  = (vFlags & kNifVfColors) != 0u;
    const bool hasSkin = (vFlags & kNifVfSkinned) != 0u;
    const bool fullPos = (vFlags & kNifVfFullPrecision) != 0u;

    const size_t vbBytes = size_t(numVerts) * size_t(stride);
    const size_t ibBytes = size_t(numTris) * 6u;
    if (stride < 8u || vbBytes + ibBytes > size_t(dataSize)) return false;

    const bool isDynamic = (blockType == "BSDynamicTriShape");
    const uint8_t* dynPosPtr = nullptr;
    if (isDynamic) {
        const size_t afterData = off + size_t(dataSize);
        if (afterData + 4u <= blkSz) {
            uint32_t dynSz = 0u;
            std::memcpy(&dynSz, blk + afterData, 4u);
            if (dynSz >= uint32_t(numVerts) * 16u && afterData + 4u + size_t(dynSz) <= blkSz) {
                dynPosPtr = blk + afterData + 4u;
            }
        }
    }
    if (!hasPos && !dynPosPtr) return false;

    // Resolve exact sequential attribute offsets inside each vertex record:
    // Note: Skyrim SE (userVersion2 == 100) static BSTriShapes commonly store FP32 positions (16B = float3 + bitangentX)
    // even when VF_FULL_PRECISION (bit 54) is clear! Checking `stride >= 12 + tailBytes` handles BOTH SSE FP32 and FP16
    // (as well as explicit VF_FULL_PRECISION and FO4 half-float streams) with 100% precision.
    const bool hasEye = (vFlags & kNifVfEyeData) != 0u;
    const size_t tailBytes = (hasUv ? 4u : 0u) + (hasNrm ? 4u : 0u) + (hasTan ? 4u : 0u) +
                             (hasCol ? 4u : 0u) + (hasSkin ? 12u : 0u) + (hasEye ? 4u : 0u);
    const bool useFullPos = fullPos || (hasPos && stride >= 12u + tailBytes);
    size_t posBytes = 0u;
    if (hasPos) {
        if (useFullPos) {
            posBytes = (stride >= 16u + tailBytes) ? 16u : 12u;
        } else {
            posBytes = 8u;
        }
    }
    const size_t uvOff  = posBytes;
    const size_t nrmOff = uvOff + (hasUv ? 4u : 0u);
    const size_t tanOff = nrmOff + (hasNrm ? 4u : 0u);
    const size_t colOff = tanOff + (hasTan ? 4u : 0u);

    shape.vertexDesc       = vdesc;
    shape.vertexFlags      = vFlags;
    shape.vertexStride     = stride;
    shape.fullPrecisionPos = useFullPos || (dynPosPtr != nullptr);
    shape.hasNormals       = hasNrm;
    shape.hasUvs           = hasUv;
    shape.hasColors        = hasCol;
    shape.isSkinned        = hasSkin;
    shape.vertices.resize(numVerts);

    const uint8_t* vb = blk + off;
    for (uint16_t i = 0u; i < numVerts; ++i) {
        const uint8_t* v = vb + size_t(i) * size_t(stride);
        NifVertex nv{};

        if (dynPosPtr) {
            float fp[3]{};
            std::memcpy(fp, dynPosPtr + size_t(i) * 16u, 12u);
            nv.position = {fp[0], fp[1], fp[2]};
        } else if (useFullPos && stride >= 12u) {
            float fp[3]{};
            std::memcpy(fp, v, 12u);
            nv.position = {fp[0], fp[1], fp[2]};
        } else {
            uint16_t hp[3]{};
            std::memcpy(hp, v, 6u);
            nv.position = {HalfBitsToFloat(hp[0]), HalfBitsToFloat(hp[1]), HalfBitsToFloat(hp[2])};
        }
        if (!IsFiniteVec3(nv.position)) nv.position = {0.0f, 0.0f, 0.0f};

        if (hasUv && uvOff + 4u <= stride) {
            uint16_t huv[2]{};
            std::memcpy(huv, v + uvOff, 4u);
            const float u = HalfBitsToFloat(huv[0]);
            const float tv = HalfBitsToFloat(huv[1]);
            nv.uv = {std::isfinite(u) ? u : 0.0f, std::isfinite(tv) ? tv : 0.0f};
        }
        if (hasNrm && nrmOff + 3u <= stride) {
            const float nx = (float(v[nrmOff + 0u]) / 255.0f) * 2.0f - 1.0f;
            const float ny = (float(v[nrmOff + 1u]) / 255.0f) * 2.0f - 1.0f;
            const float nz = (float(v[nrmOff + 2u]) / 255.0f) * 2.0f - 1.0f;
            nv.normal = SafeUnitNormal({nx, ny, nz});
        }
        if (hasTan && tanOff + 3u <= stride) {
            const float tx = (float(v[tanOff + 0u]) / 255.0f) * 2.0f - 1.0f;
            const float ty = (float(v[tanOff + 1u]) / 255.0f) * 2.0f - 1.0f;
            const float tz = (float(v[tanOff + 2u]) / 255.0f) * 2.0f - 1.0f;
            nv.tangent = SafeUnitNormal({tx, ty, tz});
        }
        if (hasCol && colOff + 4u <= stride) {
            nv.color[0] = float(v[colOff + 0u]) / 255.0f;
            nv.color[1] = float(v[colOff + 1u]) / 255.0f;
            nv.color[2] = float(v[colOff + 2u]) / 255.0f;
            nv.color[3] = float(v[colOff + 3u]) / 255.0f;
        }
        shape.vertices[i] = nv;
    }

    const uint8_t* ib = vb + vbBytes;
    shape.indices.reserve(size_t(numTris) * 3u);
    for (uint32_t t = 0u; t < numTris; ++t) {
        uint16_t tri[3]{};
        std::memcpy(tri, ib + size_t(t) * 6u, 6u);
        if (tri[0] < numVerts && tri[1] < numVerts && tri[2] < numVerts &&
            tri[0] != tri[1] && tri[1] != tri[2] && tri[2] != tri[0]) {
            shape.indices.push_back(tri[0]);
            shape.indices.push_back(tri[1]);
            shape.indices.push_back(tri[2]);
        }
    }

    if (shape.indices.size() < 3u) return false;
    *outShape = std::move(shape);
    return true;
}

void RecomputeShapeSmoothNormals(NifTriShape& shape) {
    std::vector<Vec3> acc(shape.vertices.size(), Vec3{0.0f, 0.0f, 0.0f});
    for (size_t i = 0u; i + 2u < shape.indices.size(); i += 3u) {
        const uint32_t i0 = shape.indices[i + 0u];
        const uint32_t i1 = shape.indices[i + 1u];
        const uint32_t i2 = shape.indices[i + 2u];
        const Vec3 fn = Cross(shape.vertices[i1].position - shape.vertices[i0].position,
                              shape.vertices[i2].position - shape.vertices[i0].position);
        if (IsFiniteVec3(fn)) {
            acc[i0] = acc[i0] + fn;
            acc[i1] = acc[i1] + fn;
            acc[i2] = acc[i2] + fn;
        }
    }
    for (size_t i = 0u; i < shape.vertices.size(); ++i) {
        shape.vertices[i].normal = SafeUnitNormal(acc[i]);
    }
}

}  // namespace

// ============================================================================
// Public Helpers & IEEE-754 Binary16 Codec
// ============================================================================

float HalfBitsToFloat(uint16_t h) {
    const uint32_t sign = uint32_t(h & 0x8000u) << 16;
    const uint32_t exp  = (h >> 10) & 0x1Fu;
    const uint32_t mant = h & 0x03FFu;
    uint32_t bits = 0u;
    if (exp == 0u) {
        if (mant == 0u) {
            bits = sign;
        } else {
            uint32_t m = mant;
            uint32_t e = 127u - 14u;
            while ((m & 0x0400u) == 0u) {
                m <<= 1;
                --e;
            }
            m &= 0x03FFu;
            bits = sign | (e << 23) | (m << 13);
        }
    } else if (exp == 31u) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + (127u - 15u)) << 23) | (mant << 13);
    }
    float f = 0.0f;
    std::memcpy(&f, &bits, 4u);
    return f;
}

uint16_t FloatToHalfBits(float f) {
    uint32_t bits = 0u;
    std::memcpy(&bits, &f, 4u);
    const uint16_t sign = static_cast<uint16_t>((bits >> 16) & 0x8000u);
    const int32_t  exp  = int32_t((bits >> 23) & 0xFFu) - 127;
    const uint32_t mant = bits & 0x007FFFFFu;
    if (exp == 128) {
        return static_cast<uint16_t>(sign | 0x7C00u | (mant ? 0x0200u : 0u));
    }
    if (exp > 15) {
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    if (exp < -14) {
        if (exp < -24) return sign;
        const uint32_t sub = (mant | 0x00800000u) >> static_cast<uint32_t>(-14 - exp + 13);
        return static_cast<uint16_t>(sign | (sub & 0x03FFu));
    }
    const uint16_t hExp  = static_cast<uint16_t>((exp + 15) << 10);
    const uint16_t hMant = static_cast<uint16_t>((mant + 0x00001000u) >> 13);
    return static_cast<uint16_t>(sign | hExp | hMant);
}

Vec3 NifTransform::TransformPoint(Vec3 p) const {
    return {
        (rotation[0] * p.x + rotation[1] * p.y + rotation[2] * p.z) * scale + translation.x,
        (rotation[3] * p.x + rotation[4] * p.y + rotation[5] * p.z) * scale + translation.y,
        (rotation[6] * p.x + rotation[7] * p.y + rotation[8] * p.z) * scale + translation.z,
    };
}

Vec3 NifTransform::TransformDirection(Vec3 v) const {
    return SafeUnitNormal({
        rotation[0] * v.x + rotation[1] * v.y + rotation[2] * v.z,
        rotation[3] * v.x + rotation[4] * v.y + rotation[5] * v.z,
        rotation[6] * v.x + rotation[7] * v.y + rotation[8] * v.z,
    });
}

NifTransform NifTransform::Compose(const NifTransform& parent, const NifTransform& local) {
    NifTransform out{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            out.rotation[r * 3 + c] =
                parent.rotation[r * 3 + 0] * local.rotation[0 * 3 + c] +
                parent.rotation[r * 3 + 1] * local.rotation[1 * 3 + c] +
                parent.rotation[r * 3 + 2] * local.rotation[2 * 3 + c];
        }
    }
    out.scale = parent.scale * local.scale;
    out.translation = parent.TransformPoint(local.translation);
    return out;
}

TerrainMaterialId ClassifyNifTextureMaterial(const std::string& diffusePath) {
    const std::string p = ToLowerAscii(diffusePath);
    if (p.find("magma") != std::string::npos || p.find("lava") != std::string::npos) return TerrainMaterialId::MagmaFissure;
    if (p.find("glacier") != std::string::npos || p.find("ice") != std::string::npos) return TerrainMaterialId::GlacialIce;
    if (p.find("snow") != std::string::npos) return TerrainMaterialId::SnowPack;
    if (p.find("crystal") != std::string::npos || p.find("gem") != std::string::npos || p.find("soul") != std::string::npos)
        return TerrainMaterialId::ArcaneCrystal;
    if (p.find("flora") != std::string::npos || p.find("tree") != std::string::npos ||
        p.find("leaf") != std::string::npos || p.find("moss") != std::string::npos ||
        p.find("pine") != std::string::npos || p.find("grass") != std::string::npos)
        return TerrainMaterialId::MossMeadow;
    if (p.find("basalt") != std::string::npos || p.find("obsidian") != std::string::npos ||
        p.find("daedric") != std::string::npos || p.find("ebony") != std::string::npos)
        return TerrainMaterialId::BasaltColumn;
    if (p.find("wood") != std::string::npos || p.find("bark") != std::string::npos ||
        p.find("dirt") != std::string::npos || p.find("sand") != std::string::npos ||
        p.find("timber") != std::string::npos)
        return TerrainMaterialId::RiverPebbleSand;
    return TerrainMaterialId::RockGranite;
}

// ============================================================================
// Core Skyrim SE `.NIF` v20.2.0.7 Binary Parser
// ============================================================================

bool ParseNifBytes(const uint8_t* bytes, size_t size, const std::string& label,
                   NifScene* outScene, std::string* outErr) {
    if (!bytes || !outScene || size < 64u) {
        SetErr(outErr, "NIF buffer null or smaller than minimum 64-byte header.");
        return false;
    }

    size_t nl = 0u;
    while (nl < size && nl < 128u && bytes[nl] != '\n') ++nl;
    if (nl >= size || nl < 20u || std::memcmp(bytes, "Gamebryo File Format", 20u) != 0) {
        SetErr(outErr, "Invalid NIF magic header (expected 'Gamebryo File Format').");
        return false;
    }

    NifScene scene{};
    scene.label = label;
    scene.header.headerString.assign(reinterpret_cast<const char*>(bytes), nl);

    size_t off = nl + 1u;
    if (!Rd(bytes, size, off, &scene.header.version) ||
        !Rd(bytes, size, off, &scene.header.endianType) ||
        !Rd(bytes, size, off, &scene.header.userVersion) ||
        !Rd(bytes, size, off, &scene.header.numBlocks) ||
        !Rd(bytes, size, off, &scene.header.userVersion2)) {
        SetErr(outErr, "Truncated NIF header version/block fields.");
        return false;
    }
    if (scene.header.version != kNifVersion20_2_0_7) {
        SetErr(outErr, "Unsupported NIF version (expected 20.2.0.7 / 0x14020007).");
        return false;
    }
    if (scene.header.endianType != 1u) {
        SetErr(outErr, "Unsupported big-endian NIF file.");
        return false;
    }
    if (scene.header.numBlocks == 0u || scene.header.numBlocks > 131072u) {
        SetErr(outErr, "Invalid NIF numBlocks count.");
        return false;
    }

    // ExportInfo (3 short strings in SSE v12/100: Author, Process Script, Export Script)
    if (!SkipShortStr(bytes, size, off) || !SkipShortStr(bytes, size, off) || !SkipShortStr(bytes, size, off)) {
        SetErr(outErr, "Truncated NIF ExportInfo strings.");
        return false;
    }
    if (scene.header.userVersion2 >= 130u) {
        // Fallout 4 stream 130 adds a 4th maxFilepath short string
        if (!SkipShortStr(bytes, size, off)) {
            SetErr(outErr, "Truncated FO4 NIF maxFilepath string.");
            return false;
        }
    }

    if (!Rd(bytes, size, off, &scene.header.numBlockTypes) || scene.header.numBlockTypes == 0u) {
        SetErr(outErr, "Invalid NIF numBlockTypes.");
        return false;
    }
    scene.blockTypes.resize(scene.header.numBlockTypes);
    for (uint16_t i = 0u; i < scene.header.numBlockTypes; ++i) {
        if (!ReadSizedStr(bytes, size, off, &scene.blockTypes[i])) {
            SetErr(outErr, "Truncated NIF block type string.");
            return false;
        }
    }

    std::vector<uint16_t> blockTypeIdx(scene.header.numBlocks);
    for (uint32_t i = 0u; i < scene.header.numBlocks; ++i) {
        if (!Rd(bytes, size, off, &blockTypeIdx[i]) || blockTypeIdx[i] >= scene.header.numBlockTypes) {
            SetErr(outErr, "Invalid NIF blockTypeIndex.");
            return false;
        }
    }

    std::vector<uint32_t> blockSizes(scene.header.numBlocks);
    for (uint32_t i = 0u; i < scene.header.numBlocks; ++i) {
        if (!Rd(bytes, size, off, &blockSizes[i])) {
            SetErr(outErr, "Truncated NIF blockSizes array.");
            return false;
        }
    }

    uint32_t maxStrLen = 0u;
    if (!Rd(bytes, size, off, &scene.header.numStrings) || !Rd(bytes, size, off, &maxStrLen)) {
        SetErr(outErr, "Truncated NIF string table header.");
        return false;
    }
    if (scene.header.numStrings > 131072u) {
        SetErr(outErr, "NIF string table count exceeds safety cap.");
        return false;
    }
    scene.strings.resize(scene.header.numStrings);
    for (uint32_t i = 0u; i < scene.header.numStrings; ++i) {
        if (!ReadSizedStr(bytes, size, off, &scene.strings[i])) {
            SetErr(outErr, "Truncated NIF string table entry.");
            return false;
        }
    }

    uint32_t numGroups = 0u;
    if (!Rd(bytes, size, off, &numGroups) || off + size_t(numGroups) * 4u > size) {
        SetErr(outErr, "Truncated NIF groups table.");
        return false;
    }
    off += size_t(numGroups) * 4u;

    // Precompute exact O(1) block payload offsets using blockSizes
    std::vector<size_t> blockOffsets(scene.header.numBlocks, 0u);
    for (uint32_t i = 0u; i < scene.header.numBlocks; ++i) {
        blockOffsets[i] = off;
        if (off + size_t(blockSizes[i]) > size) {
            SetErr(outErr, "NIF block " + std::to_string(i) + " payload extends past EOF.");
            return false;
        }
        off += size_t(blockSizes[i]);
    }

    // Pass 1: Decode scene-graph nodes, shader properties, texture sets, and TriShape blocks
    std::unordered_map<int32_t, size_t> nodeByBlock;
    std::unordered_map<int32_t, int32_t> shaderToTexSet;
    std::unordered_map<int32_t, NifTextureSet> texSetsByBlock;

    for (uint32_t i = 0u; i < scene.header.numBlocks; ++i) {
        const std::string& bt = scene.blockTypes[blockTypeIdx[i]];
        const uint8_t* blk = bytes + blockOffsets[i];
        const size_t bsz = blockSizes[i];

        if (IsNodeBlockType(bt)) {
            NifNodeInfo node{};
            if (ParseNodeBlock(blk, bsz, static_cast<int32_t>(i), bt, scene.header.userVersion2, scene.strings, &node)) {
                nodeByBlock.emplace(static_cast<int32_t>(i), scene.nodes.size());
                scene.nodes.push_back(std::move(node));
            }
        } else if (IsTriShapeBlockType(bt)) {
            NifTriShape shape{};
            if (ParseTriShapeBlock(blk, bsz, static_cast<int32_t>(i), bt, scene.header.userVersion2, scene.strings, &shape)) {
                scene.shapes.push_back(std::move(shape));
            }
        } else if (bt == "BSLightingShaderProperty") {
            int32_t texRef = -1;
            if (ParseLightingShaderPropTexSetRef(blk, bsz, &texRef) && texRef >= 0) {
                shaderToTexSet.emplace(static_cast<int32_t>(i), texRef);
            }
        } else if (bt == "BSShaderTextureSet") {
            NifTextureSet set{};
            if (ParseTextureSetBlock(blk, bsz, static_cast<int32_t>(i), &set)) {
                texSetsByBlock.emplace(static_cast<int32_t>(i), std::move(set));
            }
        }
    }

    // Pass 2: Build parent links & compute hierarchical world transforms (guarding against cycles)
    std::unordered_map<int32_t, int32_t> parentOfBlock;
    for (const auto& n : scene.nodes) {
        for (int32_t ch : n.children) {
            if (ch != n.blockIndex && parentOfBlock.find(ch) == parentOfBlock.end()) {
                parentOfBlock.emplace(ch, n.blockIndex);
            }
        }
    }

    auto resolveNodeWorld = [&](int32_t blockIdx) -> NifTransform {
        std::vector<int32_t> chain;
        int32_t cur = blockIdx;
        for (int depth = 0; depth < 64 && cur >= 0; ++depth) {
            chain.push_back(cur);
            auto pit = parentOfBlock.find(cur);
            cur = (pit == parentOfBlock.end()) ? -1 : pit->second;
        }
        NifTransform acc{};
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            auto nit = nodeByBlock.find(*it);
            if (nit != nodeByBlock.end()) {
                acc = NifTransform::Compose(acc, scene.nodes[nit->second].localTransform);
            }
        }
        return acc;
    };

    for (auto& n : scene.nodes) {
        auto pit = parentOfBlock.find(n.blockIndex);
        n.parentBlock = (pit == parentOfBlock.end()) ? -1 : pit->second;
        n.worldTransform = resolveNodeWorld(n.blockIndex);
    }

    // Pass 3: Apply hierarchical world transforms & attach resolved BSShaderTextureSet to every TriShape
    for (auto& s : scene.shapes) {
        auto pit = parentOfBlock.find(s.blockIndex);
        s.parentBlock = (pit == parentOfBlock.end()) ? -1 : pit->second;
        const NifTransform parentWorld = (s.parentBlock >= 0) ? resolveNodeWorld(s.parentBlock) : NifTransform{};
        s.worldTransform = NifTransform::Compose(parentWorld, s.localTransform);

        for (auto& v : s.vertices) {
            v.position = s.worldTransform.TransformPoint(v.position);
            if (s.hasNormals) {
                v.normal = s.worldTransform.TransformDirection(v.normal);
            }
            v.tangent = s.worldTransform.TransformDirection(v.tangent);
        }
        if (!s.hasNormals) {
            RecomputeShapeSmoothNormals(s);
        }

        if (s.shaderPropertyRef >= 0) {
            auto sit = shaderToTexSet.find(s.shaderPropertyRef);
            if (sit != shaderToTexSet.end()) {
                s.textureSetRef = sit->second;
                auto tit = texSetsByBlock.find(s.textureSetRef);
                if (tit != texSetsByBlock.end()) {
                    s.textureSet = tit->second;
                }
            }
        }

        scene.totalVertices  += static_cast<uint32_t>(s.vertices.size());
        scene.totalTriangles += static_cast<uint32_t>(s.indices.size() / 3u);
    }

    *outScene = std::move(scene);
    return true;
}

bool ParseNifFile(const std::string& path, NifScene* outScene, std::string* outErr) {
    if (path.empty() || !outScene) {
        SetErr(outErr, "Empty NIF path or null output scene.");
        return false;
    }
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        SetErr(outErr, "Failed to open NIF file: " + path);
        return false;
    }
    const std::streamoff endPos = f.tellg();
    if (endPos < 64 || endPos > 256ll * 1024ll * 1024ll) {
        SetErr(outErr, "NIF file size out of supported range.");
        return false;
    }
    std::vector<uint8_t> buf(static_cast<size_t>(endPos));
    f.seekg(0, std::ios::beg);
    if (!f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()))) {
        SetErr(outErr, "Failed to read NIF file bytes.");
        return false;
    }
    return ParseNifBytes(buf.data(), buf.size(), path, outScene, outErr);
}

bool BuildProceduralMeshFromNif(const NifScene& scene, uint32_t maxMeshletTris,
                                ProceduralMesh* outMesh, std::string* outErr) {
    if (!outMesh) {
        SetErr(outErr, "Null output ProceduralMesh.");
        return false;
    }
    if (scene.shapes.empty()) {
        SetErr(outErr, "NifScene contains no decodable BSTriShape geometry.");
        return false;
    }

    std::vector<TerrainVertex> srcV;
    std::vector<uint32_t> srcI;
    srcV.reserve(scene.totalVertices);
    srcI.reserve(size_t(scene.totalTriangles) * 3u);

    TerrainMaterialId dominantMat = TerrainMaterialId::RockGranite;
    bool setDominant = false;

    for (const auto& shape : scene.shapes) {
        if (shape.vertices.empty() || shape.indices.size() < 3u) continue;
        const TerrainMaterialId mat = ClassifyNifTextureMaterial(shape.textureSet.diffusePath);
        if (!setDominant) {
            dominantMat = mat;
            setDominant = true;
        }
        const uint32_t matIdx = std::min<uint32_t>(static_cast<uint32_t>(mat), 7u);

        // Emit only vertices referenced by valid non-degenerate triangles so RawAssetValidator sees 0 zero-area tris!
        std::vector<uint32_t> remap(shape.vertices.size(), std::numeric_limits<uint32_t>::max());
        auto emitVert = [&](uint32_t localIdx) -> uint32_t {
            if (remap[localIdx] != std::numeric_limits<uint32_t>::max()) return remap[localIdx];
            const NifVertex& nv = shape.vertices[localIdx];
            TerrainVertex tv{};
            tv.pos[0] = nv.position.x; tv.pos[1] = nv.position.y; tv.pos[2] = nv.position.z;
            const Vec3 n = SafeUnitNormal(nv.normal);
            tv.normal[0] = n.x; tv.normal[1] = n.y; tv.normal[2] = n.z;
            tv.uv[0] = std::isfinite(nv.uv.x) ? nv.uv.x : 0.0f;
            tv.uv[1] = std::isfinite(nv.uv.y) ? nv.uv.y : 0.0f;
            tv.splat0[0] = tv.splat0[1] = tv.splat0[2] = tv.splat0[3] = 0.0f;
            tv.splat1[0] = tv.splat1[1] = tv.splat1[2] = tv.splat1[3] = 0.0f;
            if (matIdx < 4u) tv.splat0[matIdx] = 1.0f;
            else             tv.splat1[matIdx - 4u] = 1.0f;
            tv.meta[0] = 0.0f;
            tv.meta[1] = 0.0f;
            tv.meta[2] = 0.0f;
            tv.meta[3] = 0.0f;

            const uint32_t globalIdx = static_cast<uint32_t>(srcV.size());
            srcV.push_back(tv);
            remap[localIdx] = globalIdx;
            return globalIdx;
        };

        for (size_t t = 0u; t + 2u < shape.indices.size(); t += 3u) {
            const uint32_t i0 = shape.indices[t + 0u];
            const uint32_t i1 = shape.indices[t + 1u];
            const uint32_t i2 = shape.indices[t + 2u];
            if (i0 >= shape.vertices.size() || i1 >= shape.vertices.size() || i2 >= shape.vertices.size()) continue;
            if (i0 == i1 || i1 == i2 || i2 == i0) continue;
            const Vec3& p0 = shape.vertices[i0].position;
            const Vec3& p1 = shape.vertices[i1].position;
            const Vec3& p2 = shape.vertices[i2].position;
            if (!IsFiniteVec3(p0) || !IsFiniteVec3(p1) || !IsFiniteVec3(p2)) continue;
            const Vec3 cr = Cross(p1 - p0, p2 - p0);
            const float areaSq = Dot(cr, cr);
            if (!std::isfinite(areaSq) || areaSq < 1.0e-8f) continue;

            srcI.push_back(emitVert(i0));
            srcI.push_back(emitVert(i1));
            srcI.push_back(emitVert(i2));
        }
    }

    if (srcV.empty() || srcI.size() < 3u) {
        SetErr(outErr, "NifScene yielded zero non-degenerate triangles.");
        return false;
    }

    Vec3 mn{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 mx{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    for (const auto& v : srcV) {
        mn.x = std::min(mn.x, v.pos[0]); mn.y = std::min(mn.y, v.pos[1]); mn.z = std::min(mn.z, v.pos[2]);
        mx.x = std::max(mx.x, v.pos[0]); mx.y = std::max(mx.y, v.pos[1]); mx.z = std::max(mx.z, v.pos[2]);
    }

    outMesh->name            = scene.label.empty() ? "nif_mesh" : scene.label;
    outMesh->primaryMaterial = dominantMat;
    outMesh->lodLevel        = 0u;
    outMesh->aabbMin         = mn;
    outMesh->aabbMax         = mx;

    const uint32_t clampedTris = std::clamp(maxMeshletTris, 16u, 126u);
    BuildMeshlets(srcV, srcI, 64u, clampedTris, &outMesh->vertices, &outMesh->indices, &outMesh->meshlets);
    if (outMesh->meshlets.empty()) {
        SetErr(outErr, "BuildMeshlets produced zero meshlets from NIF geometry.");
        return false;
    }
    return true;
}

}  // namespace raw::worldgen
