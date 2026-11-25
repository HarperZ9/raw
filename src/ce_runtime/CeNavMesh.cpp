// CeNavMesh.cpp: Clean-Room Skyrim SE NAVM Parser & Dynamic Obstacle-Aware 3D Navigation Graph (Zero SKSE).
// Original work, MIT.
#include "CeNavMesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <queue>
#include <unordered_map>

namespace raw::ce {
namespace {

template <class T>
bool ReadPod(const uint8_t* p, size_t len, size_t& off, T& out) {
    if (off + sizeof(T) > len) return false;
    std::memcpy(&out, p + off, sizeof(T));
    off += sizeof(T);
    return true;
}

inline float Dist3(NavVec3 a, NavVec3 b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline float Dist2D(NavVec3 a, NavVec3 b) {
    const float dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

inline float TriArea2D(NavVec3 a, NavVec3 b, NavVec3 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// Clean-room zero-dependency RFC 1950 zlib / RFC 1951 DEFLATE inflator for compressed Skyrim.esm NAVM records.
// Supports stored (type 0), fixed Huffman (type 1), and dynamic Huffman (type 2) blocks with strict bounds checks.
constexpr uint32_t kRecFlagCompressed = 0x00040000u;

struct BitReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t bytePos = 0;
    uint32_t bitBuf = 0;
    int bitCount = 0;

    bool Ensure(int n) {
        while (bitCount < n) {
            if (bytePos >= size) return false;
            bitBuf |= uint32_t(data[bytePos++]) << bitCount;
            bitCount += 8;
        }
        return true;
    }
    bool ReadBits(int n, uint32_t& out) {
        if (n == 0) { out = 0; return true; }
        if (!Ensure(n)) return false;
        out = bitBuf & ((1u << n) - 1u);
        bitBuf >>= n;
        bitCount -= n;
        return true;
    }
    void AlignByte() {
        const int drop = bitCount & 7;
        bitBuf >>= drop;
        bitCount -= drop;
    }
};

struct HuffTable {
    static constexpr int kMaxBits = 15;
    int count[kMaxBits + 1] = {};
    uint16_t symbols[320] = {};
    int maxSym = 0;

    bool Build(const uint8_t* lengths, int numSyms) {
        std::memset(count, 0, sizeof count);
        maxSym = numSyms;
        for (int i = 0; i < numSyms; ++i) {
            if (lengths[i] > kMaxBits) return false;
            count[lengths[i]]++;
        }
        count[0] = 0;
        int offs[kMaxBits + 1] = {};
        int total = 0;
        for (int b = 1; b <= kMaxBits; ++b) {
            offs[b] = total;
            total += count[b];
        }
        if (total > 320) return false;
        for (int i = 0; i < numSyms; ++i) {
            if (lengths[i] != 0) symbols[offs[lengths[i]]++] = uint16_t(i);
        }
        return true;
    }

    bool Decode(BitReader& br, int& outSym) const {
        int code = 0, first = 0, idx = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            uint32_t bit = 0;
            if (!br.ReadBits(1, bit)) return false;
            code |= int(bit);
            const int c = count[len];
            if (code - c < first) {
                outSym = symbols[idx + (code - first)];
                return true;
            }
            idx += c;
            first = (first + c) << 1;
            code <<= 1;
        }
        return false;
    }
};

bool InflateZlibStream(const uint8_t* src, size_t srcLen, size_t expectedOut, std::vector<uint8_t>& out) {
    out.clear();
    if (!src || srcLen < 6 || expectedOut == 0 || expectedOut > 16u * 1024u * 1024u) return false;
    const uint8_t cmf = src[0], flg = src[1];
    if ((cmf & 0x0Fu) != 8u || (((uint32_t(cmf) << 8) | flg) % 31u) != 0u) return false;
    size_t hdr = 2;
    if (flg & 0x20u) { if (srcLen < 10) return false; hdr += 4; }

    BitReader br{src + hdr, srcLen - hdr, 0, 0, 0};
    out.reserve(expectedOut);

    static constexpr uint16_t kLenBase[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    static constexpr uint8_t kLenExtra[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    static constexpr uint16_t kDistBase[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    static constexpr uint8_t kDistExtra[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };
    static constexpr uint8_t kClOrder[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};

    uint32_t bfinal = 0;
    do {
        uint32_t btype = 0;
        if (!br.ReadBits(1, bfinal) || !br.ReadBits(2, btype)) return false;
        if (btype == 0u) {
            br.AlignByte();
            uint32_t len = 0, nlen = 0;
            if (!br.ReadBits(16, len) || !br.ReadBits(16, nlen)) return false;
            if ((len ^ 0xFFFFu) != nlen) return false;
            for (uint32_t i = 0; i < len; ++i) {
                uint32_t b = 0;
                if (!br.ReadBits(8, b)) return false;
                if (out.size() >= expectedOut + 64) return false;
                out.push_back(uint8_t(b));
            }
        } else if (btype == 1u || btype == 2u) {
            HuffTable litTab{}, distTab{};
            if (btype == 1u) {
                uint8_t ll[288], dl[32];
                for (int i = 0; i <= 143; ++i) ll[i] = 8;
                for (int i = 144; i <= 255; ++i) ll[i] = 9;
                for (int i = 256; i <= 279; ++i) ll[i] = 7;
                for (int i = 280; i <= 287; ++i) ll[i] = 8;
                for (int i = 0; i < 32; ++i) dl[i] = 5;
                if (!litTab.Build(ll, 288) || !distTab.Build(dl, 32)) return false;
            } else {
                uint32_t hlit = 0, hdist = 0, hclen = 0;
                if (!br.ReadBits(5, hlit) || !br.ReadBits(5, hdist) || !br.ReadBits(4, hclen)) return false;
                hlit += 257u; hdist += 1u; hclen += 4u;
                if (hlit > 286u || hdist > 30u) return false;
                uint8_t clLens[19] = {};
                for (uint32_t i = 0; i < hclen; ++i) {
                    uint32_t v = 0;
                    if (!br.ReadBits(3, v)) return false;
                    clLens[kClOrder[i]] = uint8_t(v);
                }
                HuffTable clTab{};
                if (!clTab.Build(clLens, 19)) return false;
                uint8_t allLens[320] = {};
                uint32_t idx = 0, need = hlit + hdist;
                while (idx < need) {
                    int sym = 0;
                    if (!clTab.Decode(br, sym)) return false;
                    if (sym < 16) {
                        allLens[idx++] = uint8_t(sym);
                    } else if (sym == 16) {
                        if (idx == 0) return false;
                        uint32_t rep = 0;
                        if (!br.ReadBits(2, rep)) return false;
                        rep += 3u;
                        const uint8_t prev = allLens[idx - 1];
                        while (rep-- && idx < need) allLens[idx++] = prev;
                    } else if (sym == 17) {
                        uint32_t rep = 0;
                        if (!br.ReadBits(3, rep)) return false;
                        rep += 3u;
                        while (rep-- && idx < need) allLens[idx++] = 0;
                    } else if (sym == 18) {
                        uint32_t rep = 0;
                        if (!br.ReadBits(7, rep)) return false;
                        rep += 11u;
                        while (rep-- && idx < need) allLens[idx++] = 0;
                    } else {
                        return false;
                    }
                }
                if (!litTab.Build(allLens, int(hlit)) || !distTab.Build(allLens + hlit, int(hdist))) return false;
            }
            for (;;) {
                int sym = 0;
                if (!litTab.Decode(br, sym)) return false;
                if (sym < 256) {
                    if (out.size() >= expectedOut + 64) return false;
                    out.push_back(uint8_t(sym));
                } else if (sym == 256) {
                    break;
                } else {
                    const int lc = sym - 257;
                    if (lc < 0 || lc >= 29) return false;
                    uint32_t lExtra = 0;
                    if (!br.ReadBits(kLenExtra[lc], lExtra)) return false;
                    const uint32_t copyLen = kLenBase[lc] + lExtra;
                    int dc = 0;
                    if (!distTab.Decode(br, dc) || dc < 0 || dc >= 30) return false;
                    uint32_t dExtra = 0;
                    if (!br.ReadBits(kDistExtra[dc], dExtra)) return false;
                    const uint32_t dist = kDistBase[dc] + dExtra;
                    if (dist == 0 || dist > out.size() || out.size() + copyLen > expectedOut + 64) return false;
                    for (uint32_t k = 0; k < copyLen; ++k) {
                        out.push_back(out[out.size() - dist]);
                    }
                }
            }
        } else {
            return false;
        }
    } while (!bfinal);
    return out.size() >= expectedOut;
}

}  // namespace

bool ParseNavmRecordPayload(uint32_t formId, const uint8_t* payload, size_t payloadSize, CeNavMeshData& out) {
    out = {};
    out.formId = formId;
    if (!payload || payloadSize < 16) return false;

    size_t off = 0;
    while (off + 6 <= payloadSize) {
        char tag[4];
        std::memcpy(tag, payload + off, 4);
        uint16_t subSize = 0;
        std::memcpy(&subSize, payload + off + 4, 2);
        off += 6;
        uint32_t realSize = subSize;
        if (std::memcmp(tag, "XXXX", 4) == 0 && subSize == 4 && off + 4 <= payloadSize) {
            uint32_t bigSize = 0;
            std::memcpy(&bigSize, payload + off, 4);
            off += 4;
            if (off + 6 > payloadSize) break;
            std::memcpy(tag, payload + off, 4);
            off += 6;
            realSize = bigSize;
        }
        if (off + realSize > payloadSize) return false;

        if (std::memcmp(tag, "NVNM", 4) == 0 && realSize >= 24) {
            const uint8_t* nv = payload + off;
            size_t p = 0;
            uint32_t ver = 0, magic = 0, wsId = 0, cellId = 0;
            if (!ReadPod(nv, realSize, p, ver) || !ReadPod(nv, realSize, p, magic) ||
                !ReadPod(nv, realSize, p, wsId) || !ReadPod(nv, realSize, p, cellId)) {
                return false;
            }
            // Skyrim SE NVNM magic is 0xA5E9A03C (ver 12). Also accept custom clean-room test ver >= 1.
            if (ver == 0 || ver > 64) return false;
            out.nvnmVersion = ver;
            out.worldspaceId = wsId;
            out.cellId = cellId;

            // If exterior worldspace (wsId != 0), NVNM stores 4 bytes of grid coords (x, y) before vertex count.
            if (wsId != 0) {
                uint32_t gridXY = 0;
                if (!ReadPod(nv, realSize, p, gridXY)) return false;
            }

            uint32_t vertCount = 0;
            if (!ReadPod(nv, realSize, p, vertCount) || vertCount == 0 || vertCount > 65535u) return false;
            if (p + size_t(vertCount) * 12u > realSize) return false;
            out.vertices.resize(vertCount);
            std::memcpy(out.vertices.data(), nv + p, size_t(vertCount) * 12u);
            p += size_t(vertCount) * 12u;

            uint32_t triCount = 0;
            if (!ReadPod(nv, realSize, p, triCount) || triCount == 0 || triCount > 65535u) return false;
            // Skyrim SE NVNM triangle struct is 16 bytes: v0,v1,v2 (u16), e0,e1,e2 (i16), flags (u16), coverFlags (u16).
            if (p + size_t(triCount) * 16u > realSize) return false;
            out.triangles.resize(triCount);
            for (uint32_t i = 0; i < triCount; ++i) {
                NavTri t{};
                uint16_t coverFlags = 0;
                ReadPod(nv, realSize, p, t.v[0]);
                ReadPod(nv, realSize, p, t.v[1]);
                ReadPod(nv, realSize, p, t.v[2]);
                ReadPod(nv, realSize, p, t.neighbor[0]);
                ReadPod(nv, realSize, p, t.neighbor[1]);
                ReadPod(nv, realSize, p, t.neighbor[2]);
                ReadPod(nv, realSize, p, t.flags);
                ReadPod(nv, realSize, p, coverFlags);
                if (t.v[0] >= vertCount || t.v[1] >= vertCount || t.v[2] >= vertCount) return false;
                // Sanitize external edge links (flags bits 0..2 mean neighbor[e] indexes external edge array, not local tri).
                for (int e = 0; e < 3; ++e) {
                    if ((t.flags & (1u << e)) != 0u || t.neighbor[e] < 0 || uint32_t(t.neighbor[e]) >= triCount) {
                        t.neighbor[e] = -1;
                    }
                }
                out.triangles[i] = t;
            }
            return true;
        }
        off += realSize;
    }
    return false;
}

bool ExtractNavMeshesFromPlugin(const std::string& esmPath, uint32_t maxMeshes,
                                std::vector<CeNavMeshData>& outMeshes, std::string* outErr) {
    outMeshes.clear();
    std::ifstream in(esmPath, std::ios::binary);
    if (!in) {
        if (outErr) *outErr = "cannot open plugin file";
        return false;
    }
    in.seekg(0, std::ios::end);
    const uint64_t fileSize = static_cast<uint64_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    if (fileSize < 24) {
        if (outErr) *outErr = "file too small";
        return false;
    }

    // Stream TES4/GRUP/record headers to locate uncompressed NAVM records cleanly in O(file) with tiny RAM.
    uint64_t pos = 0;
    uint8_t hdr[24];
    std::vector<uint8_t> recBuf;

    while (pos + 24 <= fileSize && outMeshes.size() < maxMeshes) {
        in.seekg(static_cast<std::streamoff>(pos), std::ios::beg);
        if (!in.read(reinterpret_cast<char*>(hdr), 24)) break;
        if (std::memcmp(hdr, "GRUP", 4) == 0) {
            // Enter GRUP header (24 bytes) to inspect nested cells/NAVM records.
            pos += 24;
            continue;
        }
        uint32_t dataSize = 0, flags = 0, formId = 0;
        std::memcpy(&dataSize, hdr + 4, 4);
        std::memcpy(&flags,    hdr + 8, 4);
        std::memcpy(&formId,   hdr + 12, 4);
        const uint64_t nextPos = pos + 24 + uint64_t(dataSize);
        if (nextPos > fileSize) break;

        if (std::memcmp(hdr, "NAVM", 4) == 0 && dataSize >= 24 && dataSize < 4u * 1024u * 1024u) {
            recBuf.resize(dataSize);
            if (in.read(reinterpret_cast<char*>(recBuf.data()), static_cast<std::streamsize>(dataSize))) {
                CeNavMeshData nav{};
                bool parsed = false;
                if ((flags & kRecFlagCompressed) != 0u) {
                    if (recBuf.size() > 10) {
                        uint32_t uncompSize = 0;
                        std::memcpy(&uncompSize, recBuf.data(), 4);
                        std::vector<uint8_t> inflated;
                        if (InflateZlibStream(recBuf.data() + 4, recBuf.size() - 4, uncompSize, inflated)) {
                            parsed = ParseNavmRecordPayload(formId, inflated.data(), inflated.size(), nav);
                        }
                    }
                } else {
                    parsed = ParseNavmRecordPayload(formId, recBuf.data(), recBuf.size(), nav);
                }
                if (parsed) outMeshes.push_back(std::move(nav));
            }
        }
        pos = nextPos;
    }
    return !outMeshes.empty();
}

void RawNavGraph::Load(CeNavMeshData mesh) {
    m_mesh = std::move(mesh);
}

NavVec3 RawNavGraph::TriCentroid(uint32_t triIdx) const {
    const auto& t = m_mesh.triangles[triIdx];
    const NavVec3& a = m_mesh.vertices[t.v[0]];
    const NavVec3& b = m_mesh.vertices[t.v[1]];
    const NavVec3& c = m_mesh.vertices[t.v[2]];
    return {(a.x + b.x + c.x) * (1.0f / 3.0f),
            (a.y + b.y + c.y) * (1.0f / 3.0f),
            (a.z + b.z + c.z) * (1.0f / 3.0f)};
}

float RawNavGraph::TriExtraCost(uint32_t triIdx) const {
    const NavVec3 c = TriCentroid(triIdx);
    const auto& t = m_mesh.triangles[triIdx];
    float mult = 1.0f;
    // Preferred road/path triangles get lower cost; water triangles cost more; deep snow slows off-road travel.
    if ((t.flags & (1u << 10)) != 0u) mult *= 0.80f;
    if ((t.flags & (1u << 9)) != 0u)  mult *= 1.65f;
    if (m_snowDepthU > 2.0f && (t.flags & (1u << 10)) == 0u) {
        mult += std::clamp(m_snowDepthU * 0.025f, 0.0f, 1.25f);
    }
    for (const auto& obs : m_obstacles) {
        if (Dist2D(c, obs.center) <= obs.radius) {
            if (obs.extraCost >= 999.0f) return -1.0f;  // hard blocked obstacle
            mult += obs.extraCost;
        }
    }
    return mult;
}

int32_t RawNavGraph::FindNearestTriangle(NavVec3 p) const {
    if (m_mesh.triangles.empty()) return -1;
    int32_t best = -1;
    float bestD = std::numeric_limits<float>::max();
    for (uint32_t i = 0; i < m_mesh.triangles.size(); ++i) {
        const float d = Dist3(p, TriCentroid(i));
        if (d < bestD) { bestD = d; best = int32_t(i); }
    }
    return best;
}

void RawNavGraph::SmoothFunnel(NavVec3 start, NavVec3 goal, const std::vector<uint32_t>& corridor,
                               std::vector<NavVec3>& outWaypoints) const {
    outWaypoints.clear();
    outWaypoints.push_back(start);
    if (corridor.size() <= 1) {
        outWaypoints.push_back(goal);
        return;
    }

    // Build portal left/right pairs across consecutive triangles in the A* corridor.
    std::vector<NavVec3> portalL, portalR;
    portalL.push_back(start);
    portalR.push_back(start);

    for (size_t i = 0; i + 1 < corridor.size(); ++i) {
        const uint32_t aIdx = corridor[i];
        const uint32_t bIdx = corridor[i + 1];
        const auto& ta = m_mesh.triangles[aIdx];
        int sharedEdge = -1;
        for (int e = 0; e < 3; ++e) {
            if (ta.neighbor[e] == int16_t(bIdx)) { sharedEdge = e; break; }
        }
        if (sharedEdge >= 0) {
            const NavVec3 p0 = m_mesh.vertices[ta.v[sharedEdge]];
            const NavVec3 p1 = m_mesh.vertices[ta.v[(sharedEdge + 1) % 3]];
            const NavVec3 ca = TriCentroid(aIdx);
            // Orient left/right consistently relative to forward direction.
            if (TriArea2D(ca, p0, p1) >= 0.0f) {
                portalL.push_back(p1);
                portalR.push_back(p0);
            } else {
                portalL.push_back(p0);
                portalR.push_back(p1);
            }
        } else {
            // Off-mesh climb/vault link: force exact portal pass-through at target centroid.
            const NavVec3 mid = TriCentroid(bIdx);
            portalL.push_back(mid);
            portalR.push_back(mid);
        }
    }
    portalL.push_back(goal);
    portalR.push_back(goal);

    // Simple Stupid Funnel Algorithm (Mikko Mononen / Recast style, right-handed +Y up where left is CCW of right).
    NavVec3 apex = start, left = start, right = start;
    size_t apexIdx = 0, leftIdx = 0, rightIdx = 0;
    for (size_t i = 1; i < portalL.size(); ++i) {
        const NavVec3 pL = portalL[i];
        const NavVec3 pR = portalR[i];
        // Update right vertex (tightens CCW toward left when TriArea2D(apex, right, pR) >= 0)
        if (TriArea2D(apex, right, pR) >= 0.0f) {
            if (Dist2D(apex, right) < 1e-3f || TriArea2D(apex, left, pR) < 0.0f) {
                right = pR; rightIdx = i;
            } else {
                outWaypoints.push_back(left);
                apex = left; apexIdx = leftIdx;
                left = apex; right = apex;
                leftIdx = apexIdx; rightIdx = apexIdx;
                i = apexIdx;
                continue;
            }
        }
        // Update left vertex (tightens CW toward right when TriArea2D(apex, left, pL) <= 0)
        if (TriArea2D(apex, left, pL) <= 0.0f) {
            if (Dist2D(apex, left) < 1e-3f || TriArea2D(apex, right, pL) > 0.0f) {
                left = pL; leftIdx = i;
            } else {
                outWaypoints.push_back(right);
                apex = right; apexIdx = rightIdx;
                left = apex; right = apex;
                leftIdx = apexIdx; rightIdx = apexIdx;
                i = apexIdx;
                continue;
            }
        }
    }
    if (Dist3(outWaypoints.back(), goal) > 1e-2f) outWaypoints.push_back(goal);
}

NavPathResult RawNavGraph::FindPath(NavVec3 start, NavVec3 goal) const {
    NavPathResult res{};
    const uint32_t nTris = static_cast<uint32_t>(m_mesh.triangles.size());
    if (nTris == 0) return res;

    const int32_t sTri = FindNearestTriangle(start);
    const int32_t gTri = FindNearestTriangle(goal);
    if (sTri < 0 || gTri < 0) return res;
    if (TriExtraCost(uint32_t(gTri)) < 0.0f) return res;  // goal sits inside hard-blocked obstacle

    std::vector<float> gScore(nTris, std::numeric_limits<float>::infinity());
    std::vector<int32_t> cameFrom(nTris, -1);
    std::vector<uint8_t> closed(nTris, 0);

    struct OpenNode {
        float f;
        uint32_t idx;
        bool operator>(const OpenNode& o) const { return f > o.f; }
    };
    std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<OpenNode>> open;

    gScore[uint32_t(sTri)] = 0.0f;
    open.push({Dist3(TriCentroid(uint32_t(sTri)), goal), uint32_t(sTri)});

    while (!open.empty()) {
        const uint32_t cur = open.top().idx;
        open.pop();
        if (closed[cur]) continue;
        closed[cur] = 1;
        ++res.nodesExpanded;

        if (int32_t(cur) == gTri) {
            res.found = true;
            res.totalCost = gScore[cur];
            std::vector<uint32_t> rev;
            for (int32_t k = int32_t(cur); k >= 0; k = cameFrom[uint32_t(k)]) rev.push_back(uint32_t(k));
            res.triCorridor.assign(rev.rbegin(), rev.rend());
            SmoothFunnel(start, goal, res.triCorridor, res.waypoints);
            return res;
        }

        const NavVec3 cCur = TriCentroid(cur);
        const auto& t = m_mesh.triangles[cur];

        // 1. Standard adjacent navmesh triangle edges
        for (int e = 0; e < 3; ++e) {
            const int16_t nb = t.neighbor[e];
            if (nb < 0 || uint32_t(nb) >= nTris || closed[uint32_t(nb)]) continue;
            const float costMult = TriExtraCost(uint32_t(nb));
            if (costMult < 0.0f) continue;  // dynamically blocked by obstacle
            const float step = Dist3(cCur, TriCentroid(uint32_t(nb))) * costMult;
            const float cand = gScore[cur] + step;
            if (cand < gScore[uint32_t(nb)]) {
                gScore[uint32_t(nb)] = cand;
                cameFrom[uint32_t(nb)] = int32_t(cur);
                open.push({cand + Dist3(TriCentroid(uint32_t(nb)), goal), uint32_t(nb)});
            }
        }

        // 2. Dynamic off-mesh jump/vault/climb links
        for (const auto& lnk : m_mesh.offMeshLinks) {
            if (lnk.fromTri != cur || lnk.toTri >= nTris || closed[lnk.toTri]) continue;
            const float costMult = TriExtraCost(lnk.toTri);
            if (costMult < 0.0f) continue;
            const float step = Dist3(lnk.startPos, lnk.endPos) * lnk.costMult * costMult;
            const float cand = gScore[cur] + step;
            if (cand < gScore[lnk.toTri]) {
                gScore[lnk.toTri] = cand;
                cameFrom[lnk.toTri] = int32_t(cur);
                open.push({cand + Dist3(TriCentroid(lnk.toTri), goal), lnk.toTri});
            }
        }
    }
    return res;
}

}  // namespace raw::ce
