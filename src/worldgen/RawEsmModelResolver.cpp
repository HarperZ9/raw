// RawEsmModelResolver.cpp: Fast Streaming `.ESM` Model-Path Resolver & Tamriel Cell Scene Reconstructor.
// Connects Skyrim.esm (STAT/TREE/MSTT/ACTI/CONT/FURN/DOOR/LIGH MODL + CELL/LAND/REFR) -> RawBsaArchive ->
// RawNifLoader -> hardware-validated 64v/126t GPU meshlets (`ProceduralMesh`). Zero external deps.
// Original work, MIT.

#include "RawEsmModelResolver.h"
#include "RawBsaArchive.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace raw::worldgen {
namespace {

void SetErr(std::string* outErr, const std::string& msg) {
    if (outErr) *outErr = msg;
}

inline uint16_t ReadU16LE(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

inline uint32_t ReadU32LE(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline bool TagEq(const uint8_t* p, const char tag[4]) {
    return p[0] == uint8_t(tag[0]) && p[1] == uint8_t(tag[1]) &&
           p[2] == uint8_t(tag[2]) && p[3] == uint8_t(tag[3]);
}

bool IsTargetModelGroupTag(const uint8_t* label) {
    return TagEq(label, "STAT") || TagEq(label, "TREE") || TagEq(label, "MSTT") ||
           TagEq(label, "ACTI") || TagEq(label, "CONT") || TagEq(label, "FURN") ||
           TagEq(label, "DOOR") || TagEq(label, "LIGH");
}

void IncrementTypeCounter(const char type[4], EsmModelIndex& idx) {
    if (std::memcmp(type, "STAT", 4) == 0) ++idx.statCount;
    else if (std::memcmp(type, "TREE", 4) == 0) ++idx.treeCount;
    else if (std::memcmp(type, "MSTT", 4) == 0) ++idx.msttCount;
    else if (std::memcmp(type, "ACTI", 4) == 0) ++idx.actiCount;
    else if (std::memcmp(type, "CONT", 4) == 0) ++idx.contCount;
    else if (std::memcmp(type, "FURN", 4) == 0) ++idx.furnCount;
    else if (std::memcmp(type, "DOOR", 4) == 0) ++idx.doorCount;
    else if (std::memcmp(type, "LIGH", 4) == 0) ++idx.lighCount;
}

void ParseModelRecordsInGroupBuffer(const uint8_t* buf, size_t len, EsmModelIndex& outIndex) {
    size_t pos = 0u;
    std::vector<uint8_t> inflated;

    while (pos + 24u <= len) {
        const uint8_t* rec = buf + pos;
        if (TagEq(rec, "GRUP")) {
            const uint32_t subGrupSz = ReadU32LE(rec + 4u);
            if (subGrupSz < 24u || pos + size_t(subGrupSz) > len) break;
            ParseModelRecordsInGroupBuffer(rec + 24u, size_t(subGrupSz) - 24u, outIndex);
            pos += size_t(subGrupSz);
            continue;
        }

        const uint32_t dataSize = ReadU32LE(rec + 4u);
        const uint32_t flags    = ReadU32LE(rec + 8u);
        const uint32_t formId   = ReadU32LE(rec + 12u);
        const size_t recTotal   = 24u + size_t(dataSize);
        if (pos + recTotal > len) break;

        if (IsTargetModelGroupTag(rec)) {
            const uint8_t* payload = rec + 24u;
            size_t payloadLen = dataSize;

            if ((flags & 0x00040000u) != 0u) {
                ++outIndex.compressedSeen;
                if (dataSize < 5u) {
                    pos += recTotal;
                    continue;
                }
                const uint32_t uncompSz = ReadU32LE(payload);
                inflated.clear();
                if (!InflateEsmZlib(payload + 4u, size_t(dataSize) - 4u, uncompSz, &inflated)) {
                    pos += recTotal;
                    continue;
                }
                payload = inflated.data();
                payloadLen = inflated.size();
            }

            std::string edid;
            std::string modl;
            size_t sp = 0u;
            uint32_t pendingLargeSize = 0u;
            while (sp + 6u <= payloadLen) {
                const uint8_t* sub = payload + sp;
                uint32_t subSz = ReadU16LE(sub + 4u);
                sp += 6u;
                if (TagEq(sub, "XXXX") && subSz == 4u && sp + 4u <= payloadLen) {
                    pendingLargeSize = ReadU32LE(payload + sp);
                    sp += 4u;
                    continue;
                }
                if (subSz == 0u && pendingLargeSize > 0u) {
                    subSz = pendingLargeSize;
                    pendingLargeSize = 0u;
                }
                if (sp + size_t(subSz) > payloadLen) break;

                if (TagEq(sub, "EDID") && subSz > 0u) {
                    size_t n = subSz;
                    while (n > 0u && payload[sp + n - 1u] == 0u) --n;
                    edid.assign(reinterpret_cast<const char*>(payload + sp), n);
                } else if (TagEq(sub, "MODL") && subSz > 0u) {
                    size_t n = subSz;
                    while (n > 0u && payload[sp + n - 1u] == 0u) --n;
                    modl.assign(reinterpret_cast<const char*>(payload + sp), n);
                }
                sp += size_t(subSz);
            }

            if (!modl.empty()) {
                EsmModelRecord mr{};
                mr.formId = formId;
                std::memcpy(mr.recordType, rec, 4u);
                mr.recordType[4]  = '\0';
                mr.editorId       = std::move(edid);
                mr.rawModelPath   = modl;
                mr.bsaVirtualPath = NormalizeEsmModelToBsaPath(modl);
                if (outIndex.byFormId.find(formId) == outIndex.byFormId.end()) {
                    IncrementTypeCounter(mr.recordType, outIndex);
                    outIndex.byFormId.emplace(formId, std::move(mr));
                }
            }
        }
        pos += recTotal;
    }
}

// Applies Skyrim REFR Euler XYZ rotation (Rx * Ry * Rz in Creation Engine convention) to a vector.
Vec3 RotateSkyrimEulerXYZ(Vec3 v, Vec3 rotRad) {
    const float cx = std::cos(rotRad.x), sx = std::sin(rotRad.x);
    const float cy = std::cos(rotRad.y), sy = std::sin(rotRad.y);
    const float cz = std::cos(rotRad.z), sz = std::sin(rotRad.z);
    // Rz(-z) * Ry(-y) * Rx(-x) -> standard Creation Engine clockwise-Z / Euler XYZ placement:
    // Step 1: X rotation
    const Vec3 r1{v.x, v.y * cx + v.z * sx, -v.y * sx + v.z * cx};
    // Step 2: Y rotation
    const Vec3 r2{r1.x * cy - r1.z * sy, r1.y, r1.x * sy + r1.z * cy};
    // Step 3: Z rotation
    return {r2.x * cz + r2.y * sz, -r2.x * sz + r2.y * cz, r2.z};
}

}  // namespace

const EsmModelRecord* EsmModelIndex::FindByFormId(uint32_t formId) const {
    auto it = byFormId.find(formId);
    return (it == byFormId.end()) ? nullptr : &it->second;
}

std::string NormalizeEsmModelToBsaPath(const std::string& rawModl) {
    std::string norm = NormalizeBsaVirtualPath(rawModl);
    if (norm.empty()) return {};
    if (norm.rfind("meshes/", 0u) != 0u) {
        norm = "meshes/" + norm;
    }
    return norm;
}

bool IndexEsmModelsFromMemory(const uint8_t* bytes, size_t size, const std::string& label,
                              EsmModelIndex* outIndex, std::string* outErr) {
    if (!bytes || !outIndex || size < 24u) {
        SetErr(outErr, "ESM memory buffer null or smaller than 24-byte header.");
        return false;
    }
    if (!TagEq(bytes, "TES4")) {
        SetErr(outErr, "Invalid ESM magic (expected 'TES4').");
        return false;
    }
    const auto t0 = std::chrono::steady_clock::now();

    EsmModelIndex idx{};
    idx.esmPath = label;
    idx.byFormId.reserve(16384u);

    const uint32_t tes4DataSz = ReadU32LE(bytes + 4u);
    size_t pos = 24u + size_t(tes4DataSz);
    if (pos > size) {
        SetErr(outErr, "ESM TES4 header record extends past EOF.");
        return false;
    }

    while (pos + 24u <= size) {
        const uint8_t* hdr = bytes + pos;
        if (!TagEq(hdr, "GRUP")) {
            const uint32_t recSz = ReadU32LE(hdr + 4u);
            if (pos + 24u + size_t(recSz) > size) break;
            pos += 24u + size_t(recSz);
            continue;
        }
        const uint32_t groupSize = ReadU32LE(hdr + 4u);
        const uint32_t groupType = ReadU32LE(hdr + 12u);
        if (groupSize < 24u || pos + size_t(groupSize) > size) break;

        if (groupType == 0u && IsTargetModelGroupTag(hdr + 8u)) {
            ParseModelRecordsInGroupBuffer(hdr + 24u, size_t(groupSize) - 24u, idx);
        }
        pos += size_t(groupSize);
    }

    const auto t1 = std::chrono::steady_clock::now();
    idx.indexTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    *outIndex = std::move(idx);
    return true;
}

bool IndexEsmModels(const std::string& esmPath, EsmModelIndex* outIndex, std::string* outErr) {
    if (esmPath.empty() || !outIndex) {
        SetErr(outErr, "Empty ESM path or null output index.");
        return false;
    }
    std::ifstream f(esmPath, std::ios::binary | std::ios::ate);
    if (!f) {
        SetErr(outErr, "Failed to open ESM file: " + esmPath);
        return false;
    }
    const std::streamoff endOff = f.tellg();
    if (endOff < 24) {
        SetErr(outErr, "ESM file smaller than 24-byte TES4 header.");
        return false;
    }
    const uint64_t totalSize = static_cast<uint64_t>(endOff);
    f.seekg(0, std::ios::beg);

    uint8_t tes4Hdr[24]{};
    if (!f.read(reinterpret_cast<char*>(tes4Hdr), 24) || !TagEq(tes4Hdr, "TES4")) {
        SetErr(outErr, "Invalid ESM header (expected 'TES4').");
        return false;
    }
    const auto t0 = std::chrono::steady_clock::now();

    EsmModelIndex idx{};
    idx.esmPath = esmPath;
    idx.byFormId.reserve(24576u);

    uint64_t pos = 24ull + uint64_t(ReadU32LE(tes4Hdr + 4u));
    std::vector<uint8_t> groupBuf;

    while (pos + 24ull <= totalSize) {
        f.seekg(static_cast<std::streamoff>(pos), std::ios::beg);
        uint8_t hdr[24]{};
        if (!f.read(reinterpret_cast<char*>(hdr), 24)) break;

        if (!TagEq(hdr, "GRUP")) {
            const uint32_t recSz = ReadU32LE(hdr + 4u);
            pos += 24ull + uint64_t(recSz);
            continue;
        }
        const uint32_t groupSize = ReadU32LE(hdr + 4u);
        const uint32_t groupType = ReadU32LE(hdr + 12u);
        if (groupSize < 24u || pos + uint64_t(groupSize) > totalSize) break;

        if (groupType == 0u && IsTargetModelGroupTag(hdr + 8u)) {
            const size_t payloadLen = size_t(groupSize) - 24u;
            groupBuf.resize(payloadLen);
            if (payloadLen > 0u && f.read(reinterpret_cast<char*>(groupBuf.data()), static_cast<std::streamsize>(payloadLen))) {
                ParseModelRecordsInGroupBuffer(groupBuf.data(), payloadLen, idx);
            }
        }
        pos += uint64_t(groupSize);
    }

    const auto t1 = std::chrono::steady_clock::now();
    idx.indexTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    *outIndex = std::move(idx);
    return true;
}

bool ReconstructTamrielCellSceneWithIndices(const EsmWorldIndex& worldIndex,
                                            const EsmModelIndex& modelIndex,
                                            const std::string& meshesBsaPath,
                                            int32_t cellX,
                                            int32_t cellY,
                                            uint32_t maxUniqueModels,
                                            ReconstructedCellScene* outScene,
                                            std::string* outErr) {
    if (!outScene) {
        SetErr(outErr, "Null output ReconstructedCellScene.");
        return false;
    }

    EsmLoadedCell cell{};
    if (!LoadEsmExteriorCell(worldIndex, cellX, cellY, &cell, outErr)) {
        return false;
    }

    ReconstructedCellScene scene{};
    scene.cellX          = cellX;
    scene.cellY          = cellY;
    scene.cellEdid       = cell.meta.edid;
    scene.cellFormId     = cell.meta.cellFormId;
    scene.waterHeightZ   = cell.meta.hasWaterHeight ? cell.meta.waterHeight : worldIndex.selectedWorld.defaultWaterHeight;
    scene.totalCellRefrs = static_cast<uint32_t>(cell.placements.size());

    if (!BuildMeshFromEsmCell(cell, worldIndex.selectedWorld.defaultWaterHeight, 126u,
                              &scene.terrainMesh, &scene.terrainChunk, outErr)) {
        return false;
    }

    RawBsaArchive primaryBsa;
    if (!primaryBsa.Open(meshesBsaPath, outErr)) {
        return false;
    }

    // Automatically open sibling `Skyrim - Meshes1.bsa` if present next to `Skyrim - Meshes0.bsa`
    RawBsaArchive siblingBsa;
    bool hasSibling = false;
    {
        const std::filesystem::path p(meshesBsaPath);
        const std::string fname = p.filename().string();
        if (fname.find("Meshes0.bsa") != std::string::npos || fname.find("meshes0.bsa") != std::string::npos) {
            const std::filesystem::path sib = p.parent_path() / "Skyrim - Meshes1.bsa";
            std::error_code ec;
            if (std::filesystem::exists(sib, ec)) {
                hasSibling = siblingBsa.Open(sib.string(), nullptr);
            }
        }
    }

    const uint32_t modelCap = (maxUniqueModels == 0u) ? 256u : maxUniqueModels;
    std::unordered_map<std::string, int32_t> slotByPath; // >=0 valid index, -1 failed/skipped

    for (const auto& refr : cell.placements) {
        const EsmModelRecord* mr = modelIndex.FindByFormId(refr.baseFormId);
        if (!mr || mr->bsaVirtualPath.empty()) continue;
        ++scene.resolvedModelRefrs;

        int32_t modelSlot = -1;
        auto it = slotByPath.find(mr->bsaVirtualPath);
        if (it != slotByPath.end()) {
            modelSlot = it->second;
        } else if (scene.uniqueModels.size() < modelCap) {
            std::vector<uint8_t> nifBytes;
            bool extracted = false;
            bool fromPrimary = false;
            if (primaryBsa.HasFile(mr->bsaVirtualPath) &&
                primaryBsa.ExtractFile(mr->bsaVirtualPath, &nifBytes, nullptr)) {
                extracted = true;
                fromPrimary = true;
            } else if (hasSibling && siblingBsa.HasFile(mr->bsaVirtualPath) &&
                       siblingBsa.ExtractFile(mr->bsaVirtualPath, &nifBytes, nullptr)) {
                extracted = true;
            }

            if (extracted) {
                NifScene nifScene{};
                ProceduralMesh propMesh{};
                if (ParseNifBytes(nifBytes.data(), nifBytes.size(), mr->bsaVirtualPath, &nifScene, nullptr) &&
                    BuildProceduralMeshFromNif(nifScene, 126u, &propMesh, nullptr)) {
                    ReconstructedPropModel pm{};
                    pm.baseFormId     = mr->formId;
                    pm.recordType     = mr->recordType;
                    pm.editorId       = mr->editorId;
                    pm.bsaVirtualPath = mr->bsaVirtualPath;
                    pm.nifShapeCount  = static_cast<uint32_t>(nifScene.shapes.size());
                    for (const auto& sh : nifScene.shapes) {
                        if (!sh.textureSet.diffusePath.empty()) {
                            pm.primaryDiffuseDds = sh.textureSet.diffusePath;
                            break;
                        }
                    }
                    pm.mesh = std::move(propMesh);
                    modelSlot = static_cast<int32_t>(scene.uniqueModels.size());
                    scene.uniqueModels.push_back(std::move(pm));
                    if (fromPrimary) ++scene.extractedFromPrimary;
                    else             ++scene.extractedFromSibling;
                }
            }
            slotByPath.emplace(mr->bsaVirtualPath, modelSlot);
        }

        if (modelSlot >= 0) {
            ReconstructedPropInstance inst{};
            inst.refrFormId  = refr.refrFormId;
            inst.baseFormId  = refr.baseFormId;
            inst.modelIndex  = static_cast<uint32_t>(modelSlot);
            inst.position    = refr.position;
            inst.rotationRad = refr.rotationRad;
            inst.scale       = (std::isfinite(refr.scale) && refr.scale > 0.05f && refr.scale < 25.0f) ? refr.scale : 1.0f;
            scene.instances.push_back(inst);
        }
    }

    *outScene = std::move(scene);
    return true;
}

bool ReconstructTamrielCellScene(const std::string& esmPath,
                                 const std::string& meshesBsaPath,
                                 int32_t cellX,
                                 int32_t cellY,
                                 uint32_t maxUniqueModels,
                                 ReconstructedCellScene* outScene,
                                 std::string* outErr) {
    EsmModelIndex modelIndex{};
    if (!IndexEsmModels(esmPath, &modelIndex, outErr)) return false;

    EsmWorldIndex worldIndex{};
    if (!IndexEsmWorldspace(esmPath, kTamrielWorldFormId, &worldIndex, outErr)) return false;

    return ReconstructTamrielCellSceneWithIndices(worldIndex, modelIndex, meshesBsaPath,
                                                  cellX, cellY, maxUniqueModels, outScene, outErr);
}

bool BakeReconstructedCellPropsToWorldMesh(const ReconstructedCellScene& scene,
                                           uint32_t maxMeshletTris,
                                           ProceduralMesh* outMergedProps,
                                           std::string* outErr) {
    if (!outMergedProps) {
        SetErr(outErr, "Null output ProceduralMesh.");
        return false;
    }
    if (scene.instances.empty() || scene.uniqueModels.empty()) {
        SetErr(outErr, "ReconstructedCellScene has no placed prop instances.");
        return false;
    }

    std::vector<TerrainVertex> srcV;
    std::vector<uint32_t> srcI;

    for (const auto& inst : scene.instances) {
        if (inst.modelIndex >= scene.uniqueModels.size()) continue;
        const ProceduralMesh& pm = scene.uniqueModels[inst.modelIndex].mesh;
        if (pm.vertices.empty() || pm.meshlets.empty()) continue;

        const uint32_t baseV = static_cast<uint32_t>(srcV.size());
        srcV.reserve(baseV + pm.vertices.size());
        for (const auto& v : pm.vertices) {
            TerrainVertex tv = v;
            const Vec3 pLocal{v.pos[0] * inst.scale, v.pos[1] * inst.scale, v.pos[2] * inst.scale};
            const Vec3 pWorld = RotateSkyrimEulerXYZ(pLocal, inst.rotationRad) + inst.position;
            const Vec3 nWorld = Normalize(RotateSkyrimEulerXYZ({v.normal[0], v.normal[1], v.normal[2]}, inst.rotationRad));
            tv.pos[0] = pWorld.x; tv.pos[1] = pWorld.y; tv.pos[2] = pWorld.z;
            tv.normal[0] = nWorld.x; tv.normal[1] = nWorld.y; tv.normal[2] = nWorld.z;
            srcV.push_back(tv);
        }

        for (const auto& ml : pm.meshlets) {
            for (uint32_t t = 0u; t < ml.triangleCount; ++t) {
                const size_t idxBase = size_t(ml.triangleOffset) + size_t(t) * 3u;
                if (idxBase + 2u >= pm.indices.size()) break;
                const uint32_t g0 = baseV + ml.vertexOffset + pm.indices[idxBase + 0u];
                const uint32_t g1 = baseV + ml.vertexOffset + pm.indices[idxBase + 1u];
                const uint32_t g2 = baseV + ml.vertexOffset + pm.indices[idxBase + 2u];
                const Vec3 p0{srcV[g0].pos[0], srcV[g0].pos[1], srcV[g0].pos[2]};
                const Vec3 p1{srcV[g1].pos[0], srcV[g1].pos[1], srcV[g1].pos[2]};
                const Vec3 p2{srcV[g2].pos[0], srcV[g2].pos[1], srcV[g2].pos[2]};
                const Vec3 cr = Cross(p1 - p0, p2 - p0);
                if (Dot(cr, cr) < 1.0e-8f) continue;
                srcI.push_back(g0);
                srcI.push_back(g1);
                srcI.push_back(g2);
            }
        }
    }

    if (srcV.empty() || srcI.size() < 3u) {
        SetErr(outErr, "Merged cell props yielded zero triangles.");
        return false;
    }

    Vec3 mn{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 mx{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    for (const auto& v : srcV) {
        mn.x = std::min(mn.x, v.pos[0]); mn.y = std::min(mn.y, v.pos[1]); mn.z = std::min(mn.z, v.pos[2]);
        mx.x = std::max(mx.x, v.pos[0]); mx.y = std::max(mx.y, v.pos[1]); mx.z = std::max(mx.z, v.pos[2]);
    }

    outMergedProps->name            = "esm_cell_" + std::to_string(scene.cellX) + "_" + std::to_string(scene.cellY) + "_props";
    outMergedProps->primaryMaterial = TerrainMaterialId::RockGranite;
    outMergedProps->lodLevel        = 0u;
    outMergedProps->aabbMin         = mn;
    outMergedProps->aabbMax         = mx;

    const uint32_t clampedTris = std::clamp(maxMeshletTris, 16u, 126u);
    BuildMeshlets(srcV, srcI, 64u, clampedTris,
                  &outMergedProps->vertices, &outMergedProps->indices, &outMergedProps->meshlets);
    return !outMergedProps->meshlets.empty();
}

}  // namespace raw::worldgen
