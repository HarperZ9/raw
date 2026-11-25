// RawEsmWorldLoader.cpp: 100% Clean-Room Native TES5 (.ESM/.ESP) Binary Worldspace Loader (`raw::worldgen`).
// Original work, MIT.

#include "RawEsmWorldLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>

namespace raw::worldgen {
namespace {

constexpr uint32_t kRecFlagCompressed = 0x00040000u;
constexpr uint32_t kHeaderSize        = 24u;

inline uint32_t MakeTag(const char s[4]) noexcept {
    return uint32_t(uint8_t(s[0])) |
           (uint32_t(uint8_t(s[1])) << 8) |
           (uint32_t(uint8_t(s[2])) << 16) |
           (uint32_t(uint8_t(s[3])) << 24);
}

inline uint16_t ReadU16LE(const uint8_t* p) noexcept {
    uint16_t v = 0;
    std::memcpy(&v, p, 2);
    return v;
}

inline uint32_t ReadU32LE(const uint8_t* p) noexcept {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

inline int32_t ReadI32LE(const uint8_t* p) noexcept {
    int32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

inline float ReadF32LE(const uint8_t* p) noexcept {
    float v = 0.0f;
    std::memcpy(&v, p, 4);
    return v;
}

struct EsmRawHeader {
    uint32_t tag      = 0u;
    uint32_t size     = 0u;  // For GRUP: total bytes incl. 24B header. For record: payload bytes after 24B header.
    uint32_t field8   = 0u;  // For GRUP: label. For record: flags.
    uint32_t field12  = 0u;  // For GRUP: groupType. For record: formId.
    uint32_t field16  = 0u;
    uint16_t field20  = 0u;  // For record: formVersion
    uint16_t field22  = 0u;
};

bool ReadHeaderAt(std::ifstream& f, uint64_t pos, uint64_t fileSize, EsmRawHeader* out) {
    if (!out || pos + kHeaderSize > fileSize) return false;
    f.seekg(static_cast<std::streamoff>(pos), std::ios::beg);
    uint8_t buf[kHeaderSize]{};
    if (!f.read(reinterpret_cast<char*>(buf), kHeaderSize)) return false;
    out->tag     = ReadU32LE(buf + 0);
    out->size    = ReadU32LE(buf + 4);
    out->field8  = ReadU32LE(buf + 8);
    out->field12 = ReadU32LE(buf + 12);
    out->field16 = ReadU32LE(buf + 16);
    out->field20 = ReadU16LE(buf + 20);
    out->field22 = ReadU16LE(buf + 22);
    return true;
}

bool ReadRecordPayload(std::ifstream& f,
                       uint64_t recordPos,
                       uint64_t fileSize,
                       const EsmRawHeader& hdr,
                       std::vector<uint8_t>* outPayload,
                       uint32_t* compressedCounter = nullptr) {
    if (!outPayload) return false;
    outPayload->clear();
    const uint64_t dataPos = recordPos + kHeaderSize;
    if (dataPos + hdr.size > fileSize) return false;
    if (hdr.size == 0u) return true;

    f.seekg(static_cast<std::streamoff>(dataPos), std::ios::beg);
    if ((hdr.field8 & kRecFlagCompressed) != 0u) {
        if (compressedCounter) ++(*compressedCounter);
        if (hdr.size < 10u) return false;
        std::vector<uint8_t> raw(hdr.size);
        if (!f.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(hdr.size))) return false;
        const uint32_t uncompSize = ReadU32LE(raw.data());
        if (uncompSize == 0u || uncompSize > 32u * 1024u * 1024u) return false;
        return InflateEsmZlib(raw.data() + 4, raw.size() - 4, uncompSize, outPayload);
    }

    outPayload->resize(hdr.size);
    return static_cast<bool>(f.read(reinterpret_cast<char*>(outPayload->data()), static_cast<std::streamsize>(hdr.size)));
}

// Clean subrecord iterator supporting TES5 `XXXX` extended (>64 KB) subrecords.
template <typename Fn>
void ForEachSubrecord(const uint8_t* payload, size_t payloadSize, Fn&& cb) {
    size_t pos = 0;
    uint32_t overrideSize = 0u;
    bool hasOverride = false;
    const uint32_t tagXXXX = MakeTag("XXXX");
    while (pos + 6u <= payloadSize) {
        const uint32_t tag = ReadU32LE(payload + pos);
        const uint16_t rawLen = ReadU16LE(payload + pos + 4);
        pos += 6u;
        const uint32_t len = hasOverride ? overrideSize : uint32_t(rawLen);
        hasOverride = false;
        if (pos + len > payloadSize) break;
        if (tag == tagXXXX && len == 4u) {
            overrideSize = ReadU32LE(payload + pos);
            hasOverride = true;
        } else {
            cb(tag, payload + pos, len);
        }
        pos += len;
    }
}

std::string ReadZString(const uint8_t* p, uint32_t len) {
    size_t n = 0;
    while (n < len && p[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}

bool ContainsCI(const std::string& hay, const char* needle) noexcept {
    const size_t nLen = std::strlen(needle);
    if (nLen == 0 || hay.size() < nLen) return false;
    for (size_t i = 0; i + nLen <= hay.size(); ++i) {
        size_t j = 0;
        for (; j < nLen; ++j) {
            char a = hay[i + j]; if (a >= 'A' && a <= 'Z') a = char(a + 32);
            char b = needle[j];  if (b >= 'A' && b <= 'Z') b = char(b + 32);
            if (a != b) break;
        }
        if (j == nLen) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Clean-Room Zero-Dependency RFC 1950 (zlib) / RFC 1951 (DEFLATE) Inflator
// ---------------------------------------------------------------------------

struct BitReader {
    const uint8_t* data     = nullptr;
    size_t         size     = 0;
    size_t         bytePos  = 0;
    uint32_t       bitBuf   = 0;
    int            bitCount = 0;

    bool Ensure(int n) noexcept {
        while (bitCount < n) {
            if (bytePos >= size) return false;
            bitBuf |= uint32_t(data[bytePos++]) << bitCount;
            bitCount += 8;
        }
        return true;
    }
    bool ReadBits(int n, uint32_t& out) noexcept {
        if (n == 0) { out = 0; return true; }
        if (!Ensure(n)) return false;
        out = bitBuf & ((1u << n) - 1u);
        bitBuf >>= n;
        bitCount -= n;
        return true;
    }
    void AlignByte() noexcept {
        const int drop = bitCount & 7;
        bitBuf >>= drop;
        bitCount -= drop;
    }
};

struct HuffTable {
    static constexpr int kMaxBits = 15;
    int      count[kMaxBits + 1]{};
    uint16_t symbols[320]{};
    int      maxSym = 0;

    bool Build(const uint8_t* lengths, int numSyms) noexcept {
        std::memset(count, 0, sizeof(count));
        maxSym = numSyms;
        for (int i = 0; i < numSyms; ++i) {
            if (lengths[i] > kMaxBits) return false;
            count[lengths[i]]++;
        }
        count[0] = 0;
        int offs[kMaxBits + 1]{};
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

    bool Decode(BitReader& br, int& outSym) const noexcept {
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

// Recursively walks WRLD GRUP Type 1 -> Type 4 (Exterior Cell Block) -> Type 5 (Sub-Block)
// indexing every CELL and its immediate Type 6 (Cell Children) LAND/REFR file offsets.
void IndexWorldGrupTree(std::ifstream& f,
                        uint64_t startPos,
                        uint64_t endPos,
                        uint64_t fileSize,
                        int depth,
                        EsmWorldIndex& idx) {
    if (depth > 8 || startPos >= endPos || endPos > fileSize) return;
    const uint32_t tagGRUP = MakeTag("GRUP");
    const uint32_t tagCELL = MakeTag("CELL");
    const uint32_t tagLAND = MakeTag("LAND");
    const uint32_t tagREFR = MakeTag("REFR");
    const uint32_t tagEDID = MakeTag("EDID");
    const uint32_t tagDATA = MakeTag("DATA");
    const uint32_t tagXCLC = MakeTag("XCLC");
    const uint32_t tagXCLW = MakeTag("XCLW");

    uint64_t pos = startPos;
    EsmCellCoordKey lastCellKey{};
    bool hasLastCell = false;

    while (pos + kHeaderSize <= endPos) {
        EsmRawHeader hdr{};
        if (!ReadHeaderAt(f, pos, fileSize, &hdr)) break;

        if (hdr.tag == tagGRUP) {
            if (hdr.size < kHeaderSize || pos + hdr.size > endPos) break;
            const uint32_t gType = hdr.field12;
            const uint64_t childStart = pos + kHeaderSize;
            const uint64_t childEnd   = pos + hdr.size;

            if (gType == 4u || gType == 5u) {
                // Exterior Cell Block / Sub-Block -> recurse
                IndexWorldGrupTree(f, childStart, childEnd, fileSize, depth + 1, idx);
            } else if (gType == 6u && hasLastCell) {
                // Cell Children GRUP -> record offset & quickly scan headers for LAND & REFR count (zero decompression!)
                auto it = idx.cells.find(lastCellKey);
                if (it != idx.cells.end()) {
                    it->second.cellGrupFileOffset = pos;
                    it->second.cellGrupByteSize   = hdr.size;

                    // Fast header-only walk inside Type 6 -> Type 8 / Type 9
                    auto scanCellChildren = [&](auto&& self, uint64_t cs, uint64_t ce, int cd) -> void {
                        if (cd > 3) return;
                        uint64_t cp = cs;
                        while (cp + kHeaderSize <= ce) {
                            EsmRawHeader ch{};
                            if (!ReadHeaderAt(f, cp, fileSize, &ch)) break;
                            if (ch.tag == tagGRUP) {
                                if (ch.size < kHeaderSize || cp + ch.size > ce) break;
                                self(self, cp + kHeaderSize, cp + ch.size, cd + 1);
                                cp += ch.size;
                            } else {
                                if (ch.tag == tagLAND && !it->second.hasLand) {
                                    it->second.hasLand = true;
                                    it->second.landRecordOffset = cp;
                                    ++idx.totalCellsWithLand;
                                } else if (ch.tag == tagREFR) {
                                    ++it->second.refrCountEstimate;
                                    ++idx.totalIndexedRefrs;
                                }
                                if ((ch.field8 & kRecFlagCompressed) != 0u) ++idx.compressedRecordsSeen;
                                cp += kHeaderSize + ch.size;
                            }
                        }
                    };
                    scanCellChildren(scanCellChildren, childStart, childEnd, 0);
                }
            }
            pos = childEnd;
        } else {
            const uint64_t nextPos = pos + kHeaderSize + hdr.size;
            if (nextPos > endPos) break;
            if (hdr.tag == tagCELL) {
                std::vector<uint8_t> payload;
                if (ReadRecordPayload(f, pos, fileSize, hdr, &payload, &idx.compressedRecordsSeen)) {
                    EsmCellIndexEntry ce{};
                    ce.cellFormId = hdr.field12;
                    bool hasXclc = false;
                    ForEachSubrecord(payload.data(), payload.size(), [&](uint32_t stag, const uint8_t* sdata, uint32_t slen) {
                        if (stag == tagEDID) {
                            ce.edid = ReadZString(sdata, slen);
                        } else if (stag == tagDATA && slen >= 1u) {
                            ce.cellFlags = (slen >= 2u) ? ReadU16LE(sdata) : uint16_t(sdata[0]);
                        } else if (stag == tagXCLC && slen >= 8u) {
                            ce.cellX = ReadI32LE(sdata + 0);
                            ce.cellY = ReadI32LE(sdata + 4);
                            hasXclc = true;
                        } else if (stag == tagXCLW && slen >= 4u) {
                            const float wh = ReadF32LE(sdata);
                            if (std::isfinite(wh) && std::fabs(wh) < 1.0e8f) {
                                ce.hasWaterHeight = true;
                                ce.waterHeight = wh;
                            }
                        }
                    });
                    if (hasXclc && (ce.cellFlags & 0x01u) == 0u) {
                        const EsmCellCoordKey key{ce.cellX, ce.cellY};
                        idx.cells[key] = std::move(ce);
                        lastCellKey = key;
                        hasLastCell = true;
                    } else {
                        hasLastCell = false;
                    }
                }
            }
            pos = nextPos;
        }
    }
}

void AddSplatWeight(Vec4& s0, Vec4& s1, TerrainMaterialId mat, float w) noexcept {
    const uint32_t m = std::min<uint32_t>(static_cast<uint32_t>(mat), 7u);
    if (m == 0u)      s0.x += w;
    else if (m == 1u) s0.y += w;
    else if (m == 2u) s0.z += w;
    else if (m == 3u) s0.w += w;
    else if (m == 4u) s1.x += w;
    else if (m == 5u) s1.y += w;
    else if (m == 6u) s1.z += w;
    else              s1.w += w;
}

void NormalizeSplat8(Vec4& s0, Vec4& s1) noexcept {
    s0.x = std::max(0.0f, s0.x); s0.y = std::max(0.0f, s0.y);
    s0.z = std::max(0.0f, s0.z); s0.w = std::max(0.0f, s0.w);
    s1.x = std::max(0.0f, s1.x); s1.y = std::max(0.0f, s1.y);
    s1.z = std::max(0.0f, s1.z); s1.w = std::max(0.0f, s1.w);
    const float sum = s0.x + s0.y + s0.z + s0.w + s1.x + s1.y + s1.z + s1.w;
    if (sum > 1.0e-6f) {
        const float inv = 1.0f / sum;
        s0.x *= inv; s0.y *= inv; s0.z *= inv; s0.w *= inv;
        s1.x *= inv; s1.y *= inv; s1.z *= inv; s1.w *= inv;
    } else {
        s0 = {1.0f, 0.0f, 0.0f, 0.0f};
        s1 = {0.0f, 0.0f, 0.0f, 0.0f};
    }
}

PropKind GuessPropKindForRefr(const EsmRefPlacement& r) noexcept {
    if (ContainsCI(r.edid, "tree") || ContainsCI(r.edid, "pine") || ContainsCI(r.edid, "spruce")) {
        return PropKind::ConiferTree;
    }
    if (ContainsCI(r.edid, "ice") || ContainsCI(r.edid, "glacier")) return PropKind::GlacialIceArch;
    if (ContainsCI(r.edid, "snow"))    return PropKind::SnowDrift;
    if (ContainsCI(r.edid, "crystal")) return PropKind::CrystalMonolith;
    if (ContainsCI(r.edid, "grass") || ContainsCI(r.edid, "shrub")) return PropKind::AlpineGrassClump;
    return PropKind::AlpineBoulder;
}

}  // namespace

const EsmCellIndexEntry* EsmWorldIndex::FindCell(int32_t cellX, int32_t cellY) const {
    const auto it = cells.find(EsmCellCoordKey{cellX, cellY});
    return it != cells.end() ? &it->second : nullptr;
}

bool InflateEsmZlib(const uint8_t* src, size_t srcLen, size_t expectedOut, std::vector<uint8_t>* out) {
    if (!out) return false;
    out->clear();
    if (!src || srcLen < 6 || expectedOut == 0 || expectedOut > 32u * 1024u * 1024u) return false;

    const uint8_t cmf = src[0], flg = src[1];
    if ((cmf & 0x0Fu) != 8u || (((uint32_t(cmf) << 8) | flg) % 31u) != 0u) return false;
    size_t hdr = 2;
    if (flg & 0x20u) {
        if (srcLen < 10) return false;
        hdr += 4;
    }

    BitReader br{src + hdr, srcLen - hdr, 0, 0, 0};
    out->reserve(expectedOut);

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
                if (out->size() >= expectedOut + 64u) return false;
                out->push_back(uint8_t(b));
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
                uint8_t clLens[19]{};
                for (uint32_t i = 0; i < hclen; ++i) {
                    uint32_t v = 0;
                    if (!br.ReadBits(3, v)) return false;
                    clLens[kClOrder[i]] = uint8_t(v);
                }
                HuffTable clTab{};
                if (!clTab.Build(clLens, 19)) return false;
                uint8_t allLens[320]{};
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
                    if (out->size() >= expectedOut + 64u) return false;
                    out->push_back(uint8_t(sym));
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
                    if (dist == 0 || dist > out->size() || out->size() + copyLen > expectedOut + 64u) return false;
                    for (uint32_t k = 0; k < copyLen; ++k) {
                        out->push_back((*out)[out->size() - dist]);
                    }
                }
            }
        } else {
            return false;
        }
    } while (!bfinal);
    return out->size() >= expectedOut;
}

TerrainMaterialId ClassifySkyrimLandTexture(const std::string& ltexEdid, uint32_t ltexFormId) {
    if (!ltexEdid.empty()) {
        if (ContainsCI(ltexEdid, "ice") || ContainsCI(ltexEdid, "glacier") || ContainsCI(ltexEdid, "frozen")) {
            return TerrainMaterialId::GlacialIce;
        }
        if (ContainsCI(ltexEdid, "snow") || ContainsCI(ltexEdid, "frost")) {
            return TerrainMaterialId::SnowPack;
        }
        if (ContainsCI(ltexEdid, "lava") || ContainsCI(ltexEdid, "magma") || ContainsCI(ltexEdid, "ember")) {
            return TerrainMaterialId::MagmaFissure;
        }
        if (ContainsCI(ltexEdid, "volcan") || ContainsCI(ltexEdid, "ash") || ContainsCI(ltexEdid, "basalt") ||
            ContainsCI(ltexEdid, "blackreach") || ContainsCI(ltexEdid, "sulf")) {
            return TerrainMaterialId::BasaltColumn;
        }
        if (ContainsCI(ltexEdid, "crystal") || ContainsCI(ltexEdid, "soulcairn") || ContainsCI(ltexEdid, "aether")) {
            return TerrainMaterialId::ArcaneCrystal;
        }
        if (ContainsCI(ltexEdid, "grass") || ContainsCI(ltexEdid, "moss") || ContainsCI(ltexEdid, "forest") ||
            ContainsCI(ltexEdid, "pine") || ContainsCI(ltexEdid, "tundra") || ContainsCI(ltexEdid, "leaves") ||
            ContainsCI(ltexEdid, "field") || ContainsCI(ltexEdid, "fall")) {
            return TerrainMaterialId::MossMeadow;
        }
        if (ContainsCI(ltexEdid, "river") || ContainsCI(ltexEdid, "sand") || ContainsCI(ltexEdid, "pebble") ||
            ContainsCI(ltexEdid, "mud") || ContainsCI(ltexEdid, "coast") || ContainsCI(ltexEdid, "beach") ||
            ContainsCI(ltexEdid, "dirt") || ContainsCI(ltexEdid, "path") || ContainsCI(ltexEdid, "road") ||
            ContainsCI(ltexEdid, "gravel")) {
            return TerrainMaterialId::RiverPebbleSand;
        }
        if (ContainsCI(ltexEdid, "rock") || ContainsCI(ltexEdid, "stone") || ContainsCI(ltexEdid, "mountain") ||
            ContainsCI(ltexEdid, "cliff") || ContainsCI(ltexEdid, "cobble") || ContainsCI(ltexEdid, "reach")) {
            return TerrainMaterialId::RockGranite;
        }
    }
    // Deterministic fallback hash across Rock/Moss/Sand/Snow when LTEX EDID is unknown
    static constexpr TerrainMaterialId kFallback[4] = {
        TerrainMaterialId::RockGranite, TerrainMaterialId::MossMeadow,
        TerrainMaterialId::RiverPebbleSand, TerrainMaterialId::SnowPack
    };
    return kFallback[(ltexFormId ^ (ltexFormId >> 5)) & 3u];
}

bool DecodeEsmLandPayload(const uint8_t* payload, size_t payloadSize, uint32_t landFormId,
                          const std::unordered_map<uint32_t, std::string>& landTexEdids,
                          EsmLandData* outLand, std::string* outErr) {
    if (!payload || payloadSize == 0 || !outLand) {
        if (outErr) *outErr = "null LAND payload or output";
        return false;
    }
    *outLand = EsmLandData{};
    outLand->landFormId = landFormId;
    for (uint32_t i = 0; i < kEsmLandVertCount; ++i) {
        outLand->normals[i] = {0.0f, 0.0f, 1.0f};
        outLand->colors[i]  = {1.0f, 1.0f, 1.0f};
        outLand->splat0[i]  = {0.0f, 0.0f, 0.0f, 0.0f};
        outLand->splat1[i]  = {0.0f, 0.0f, 0.0f, 0.0f};
    }

    const uint32_t tagVHGT = MakeTag("VHGT");
    const uint32_t tagVNML = MakeTag("VNML");
    const uint32_t tagVCLR = MakeTag("VCLR");
    const uint32_t tagBTXT = MakeTag("BTXT");
    const uint32_t tagATXT = MakeTag("ATXT");
    const uint32_t tagVTXT = MakeTag("VTXT");

    auto lookupMat = [&](uint32_t texFormId) -> TerrainMaterialId {
        const auto it = landTexEdids.find(texFormId);
        return ClassifySkyrimLandTexture(it != landTexEdids.end() ? it->second : std::string{}, texFormId);
    };

    uint8_t  activeQuad = 0u;
    TerrainMaterialId activeAlphaMat = TerrainMaterialId::RockGranite;
    bool hasActiveAtxt = false;
    std::array<bool, 4> hasQuadBase{};

    ForEachSubrecord(payload, payloadSize, [&](uint32_t tag, const uint8_t* data, uint32_t len) {
        if (tag == tagVHGT && len >= 4u + kEsmLandVertCount) {
            const float baseOff = ReadF32LE(data);
            const auto* deltas = reinterpret_cast<const int8_t*>(data + 4);
            outLand->vhgtBaseOffset = baseOff;
            float rowOff = baseOff;
            float mnZ = std::numeric_limits<float>::max();
            float mxZ = std::numeric_limits<float>::lowest();
            double sumZ = 0.0;
            for (uint32_t y = 0; y < kEsmLandGridSize; ++y) {
                rowOff += float(deltas[y * kEsmLandGridSize + 0]);
                float colAcc = rowOff;
                for (uint32_t x = 0; x < kEsmLandGridSize; ++x) {
                    if (x > 0u) colAcc += float(deltas[y * kEsmLandGridSize + x]);
                    const float z = colAcc * kEsmHeightScaleU;
                    outLand->heightsZ[y * kEsmLandGridSize + x] = z;
                    mnZ = std::min(mnZ, z);
                    mxZ = std::max(mxZ, z);
                    sumZ += double(z);
                }
            }
            outLand->minHeightZ  = mnZ;
            outLand->maxHeightZ  = mxZ;
            outLand->meanHeightZ = float(sumZ / double(kEsmLandVertCount));
            outLand->hasVhgt     = true;
        } else if (tag == tagVNML && len >= kEsmLandVertCount * 3u) {
            const auto* nBytes = reinterpret_cast<const int8_t*>(data);
            for (uint32_t i = 0; i < kEsmLandVertCount; ++i) {
                const Vec3 rawN{float(nBytes[i * 3 + 0]) / 127.0f,
                                float(nBytes[i * 3 + 1]) / 127.0f,
                                float(nBytes[i * 3 + 2]) / 127.0f};
                outLand->normals[i] = Normalize(rawN);
            }
            outLand->hasVnml = true;
        } else if (tag == tagVCLR && len >= kEsmLandVertCount * 3u) {
            for (uint32_t i = 0; i < kEsmLandVertCount; ++i) {
                outLand->colors[i] = {float(data[i * 3 + 0]) / 255.0f,
                                      float(data[i * 3 + 1]) / 255.0f,
                                      float(data[i * 3 + 2]) / 255.0f};
            }
            outLand->hasVclr = true;
        } else if (tag == tagBTXT && len >= 8u) {
            const uint32_t texId = ReadU32LE(data + 0);
            const uint8_t quad = data[4];
            if (quad < 4u) {
                outLand->baseQuadTexFormId[quad] = texId;
                hasQuadBase[quad] = true;
                outLand->hasTextures = true;
                const TerrainMaterialId mat = lookupMat(texId);
                const uint32_t ox = (quad & 1u) ? 16u : 0u;
                const uint32_t oy = (quad & 2u) ? 16u : 0u;
                for (uint32_t qy = 0; qy < 17u; ++qy) {
                    for (uint32_t qx = 0; qx < 17u; ++qx) {
                        const uint32_t idx = (oy + qy) * kEsmLandGridSize + (ox + qx);
                        AddSplatWeight(outLand->splat0[idx], outLand->splat1[idx], mat, 0.55f);
                    }
                }
            }
            hasActiveAtxt = false;
        } else if (tag == tagATXT && len >= 8u) {
            const uint32_t texId = ReadU32LE(data + 0);
            activeQuad = data[4];
            activeAlphaMat = lookupMat(texId);
            hasActiveAtxt = (activeQuad < 4u);
            if (hasActiveAtxt) {
                ++outLand->alphaLayerCount;
                outLand->hasTextures = true;
            }
        } else if (tag == tagVTXT && hasActiveAtxt && len >= 8u) {
            const uint32_t ox = (activeQuad & 1u) ? 16u : 0u;
            const uint32_t oy = (activeQuad & 2u) ? 16u : 0u;
            const uint32_t entries = len / 8u;
            for (uint32_t e = 0; e < entries; ++e) {
                const uint16_t vpos = ReadU16LE(data + e * 8u + 0u);
                const float opacity = Clamp01(ReadF32LE(data + e * 8u + 4u));
                if (vpos < 289u && opacity > 1.0e-4f) {
                    const uint32_t qx = vpos % 17u;
                    const uint32_t qy = vpos / 17u;
                    const uint32_t idx = (oy + qy) * kEsmLandGridSize + (ox + qx);
                    AddSplatWeight(outLand->splat0[idx], outLand->splat1[idx], activeAlphaMat, opacity);
                }
            }
        }
    });

    if (!outLand->hasVhgt) {
        if (outErr) *outErr = "LAND record missing VHGT heightmap subrecord";
        return false;
    }

    // Recompute unit normals from finite differences if VNML was absent
    if (!outLand->hasVnml) {
        constexpr float kStepU = kEsmCellSizeU / 32.0f; // 128.0u
        for (uint32_t y = 0; y < kEsmLandGridSize; ++y) {
            for (uint32_t x = 0; x < kEsmLandGridSize; ++x) {
                const uint32_t xm = x > 0 ? x - 1 : 0;
                const uint32_t xp = x + 1 < kEsmLandGridSize ? x + 1 : kEsmLandGridSize - 1;
                const uint32_t ym = y > 0 ? y - 1 : 0;
                const uint32_t yp = y + 1 < kEsmLandGridSize ? y + 1 : kEsmLandGridSize - 1;
                const float dX = (outLand->heightsZ[y * kEsmLandGridSize + xp] -
                                  outLand->heightsZ[y * kEsmLandGridSize + xm]) / (float(xp - xm) * kStepU);
                const float dY = (outLand->heightsZ[yp * kEsmLandGridSize + x] -
                                  outLand->heightsZ[ym * kEsmLandGridSize + x]) / (float(yp - ym) * kStepU);
                outLand->normals[y * kEsmLandGridSize + x] = Normalize({-dX, -dY, 1.0f});
            }
        }
    }

    // Normalize all 8-channel PBR splat weights (with elevation/slope geological fallback if no BTXT/ATXT present)
    for (uint32_t i = 0; i < kEsmLandVertCount; ++i) {
        const float sum = outLand->splat0[i].x + outLand->splat0[i].y + outLand->splat0[i].z + outLand->splat0[i].w +
                          outLand->splat1[i].x + outLand->splat1[i].y + outLand->splat1[i].z + outLand->splat1[i].w;
        if (sum < 1.0e-5f) {
            const float z = outLand->heightsZ[i];
            const float slope = 1.0f - Clamp01(outLand->normals[i].z);
            const float snowW = Smoothstep(3200.0f, 6500.0f, z) * (1.0f - Smoothstep(0.45f, 0.72f, slope));
            const float rockW = Smoothstep(0.25f, 0.58f, slope) + 0.15f;
            const float mossW = (1.0f - snowW) * (1.0f - Smoothstep(0.28f, 0.55f, slope));
            outLand->splat0[i] = {rockW, snowW, 0.0f, 0.0f};
            outLand->splat1[i] = {0.0f, mossW, 0.0f, 0.0f};
        }
        NormalizeSplat8(outLand->splat0[i], outLand->splat1[i]);
    }
    return true;
}

bool IndexEsmWorldspace(const std::string& esmPath,
                        uint32_t targetWorldFormId,
                        EsmWorldIndex* outIndex,
                        std::string* outErr) {
    if (!outIndex) {
        if (outErr) *outErr = "outIndex is null";
        return false;
    }
    *outIndex = EsmWorldIndex{};
    outIndex->esmPath = esmPath;

    std::ifstream f(esmPath, std::ios::binary | std::ios::ate);
    if (!f) {
        if (outErr) *outErr = "failed to open ESM/ESP file: " + esmPath;
        return false;
    }
    const uint64_t fileSize = static_cast<uint64_t>(f.tellg());
    if (fileSize < kHeaderSize) {
        if (outErr) *outErr = "ESM/ESP file too small (< 24 bytes)";
        return false;
    }

    const uint32_t tagTES4 = MakeTag("TES4");
    const uint32_t tagGRUP = MakeTag("GRUP");
    const uint32_t tagLTEX = MakeTag("LTEX");
    const uint32_t tagWRLD = MakeTag("WRLD");
    const uint32_t tagHEDR = MakeTag("HEDR");
    const uint32_t tagEDID = MakeTag("EDID");
    const uint32_t tagDNAM = MakeTag("DNAM");

    // 1. Validate TES4 header record
    EsmRawHeader tes4{};
    if (!ReadHeaderAt(f, 0, fileSize, &tes4) || tes4.tag != tagTES4) {
        if (outErr) *outErr = "invalid plugin header (expected TES4 magic)";
        return false;
    }
    outIndex->formVersion = tes4.field20;
    {
        std::vector<uint8_t> tes4Payload;
        if (!ReadRecordPayload(f, 0, fileSize, tes4, &tes4Payload)) {
            if (outErr) *outErr = "truncated TES4 header payload";
            return false;
        }
        ForEachSubrecord(tes4Payload.data(), tes4Payload.size(), [&](uint32_t stag, const uint8_t* sdata, uint32_t slen) {
            if (stag == tagHEDR && slen >= 4u) {
                outIndex->headerVersion = ReadF32LE(sdata);
            }
        });
    }

    // 2. Fast streaming scan over top-level GRUPs (seeks directly past everything except LTEX & WRLD!)
    uint64_t pos = kHeaderSize + tes4.size;
    while (pos + kHeaderSize <= fileSize) {
        EsmRawHeader gh{};
        if (!ReadHeaderAt(f, pos, fileSize, &gh)) break;
        if (gh.tag != tagGRUP) {
            pos += kHeaderSize + gh.size;
            continue;
        }
        if (gh.size < kHeaderSize || pos + gh.size > fileSize) {
            if (outErr) *outErr = "malformed top-level GRUP size";
            return false;
        }
        const uint64_t gEnd = pos + gh.size;

        if (gh.field12 == 0u && gh.field8 == tagLTEX) {
            // Read LTEX formId -> EDID mapping for accurate 8-channel PBR terrain material splatting
            uint64_t lp = pos + kHeaderSize;
            while (lp + kHeaderSize <= gEnd) {
                EsmRawHeader lh{};
                if (!ReadHeaderAt(f, lp, fileSize, &lh)) break;
                if (lh.tag == tagGRUP) {
                    if (lh.size < kHeaderSize) break;
                    lp += lh.size;
                    continue;
                }
                if (lh.tag == tagLTEX) {
                    std::vector<uint8_t> lpData;
                    if (ReadRecordPayload(f, lp, fileSize, lh, &lpData, &outIndex->compressedRecordsSeen)) {
                        ForEachSubrecord(lpData.data(), lpData.size(), [&](uint32_t stag, const uint8_t* sdata, uint32_t slen) {
                            if (stag == tagEDID) {
                                outIndex->landTextureEdids[lh.field12] = ReadZString(sdata, slen);
                            }
                        });
                    }
                }
                lp += kHeaderSize + lh.size;
            }
        } else if (gh.field12 == 0u && gh.field8 == tagWRLD) {
            // Walk WRLD records and their Type-1 World Children GRUPs
            uint64_t wp = pos + kHeaderSize;
            EsmWorldspaceInfo lastWorld{};
            bool hasLastWorld = false;

            while (wp + kHeaderSize <= gEnd) {
                EsmRawHeader wh{};
                if (!ReadHeaderAt(f, wp, fileSize, &wh)) break;
                if (wh.tag == tagWRLD) {
                    lastWorld = EsmWorldspaceInfo{};
                    lastWorld.formId = wh.field12;
                    std::vector<uint8_t> wpData;
                    if (ReadRecordPayload(f, wp, fileSize, wh, &wpData, &outIndex->compressedRecordsSeen)) {
                        ForEachSubrecord(wpData.data(), wpData.size(), [&](uint32_t stag, const uint8_t* sdata, uint32_t slen) {
                            if (stag == tagEDID) {
                                lastWorld.edid = ReadZString(sdata, slen);
                            } else if (stag == tagDNAM && slen >= 8u) {
                                lastWorld.defaultLandHeight  = ReadF32LE(sdata + 0);
                                lastWorld.defaultWaterHeight = ReadF32LE(sdata + 4);
                            }
                        });
                    }
                    hasLastWorld = true;
                    outIndex->worldspaces.push_back(lastWorld);
                    wp += kHeaderSize + wh.size;
                } else if (wh.tag == tagGRUP) {
                    if (wh.size < kHeaderSize || wp + wh.size > gEnd) break;
                    if (wh.field12 == 1u && hasLastWorld && wh.field8 == lastWorld.formId) {
                        outIndex->worldspaces.back().grupFileOffset = wp;
                        outIndex->worldspaces.back().grupByteSize   = wh.size;
                        const bool isTarget = (targetWorldFormId == 0u && outIndex->selectedWorld.formId == 0u) ||
                                              (lastWorld.formId == targetWorldFormId);
                        if (isTarget) {
                            outIndex->selectedWorld = outIndex->worldspaces.back();
                            IndexWorldGrupTree(f, wp + kHeaderSize, wp + wh.size, fileSize, 0, *outIndex);
                        }
                    }
                    wp += wh.size;
                } else {
                    wp += kHeaderSize + wh.size;
                }
            }
        }
        pos = gEnd;
    }

    outIndex->totalIndexedCells = static_cast<uint32_t>(outIndex->cells.size());
    if (outIndex->selectedWorld.formId == 0u) {
        if (outErr) *outErr = "target WRLD FormID not found in plugin";
        return false;
    }
    return true;
}

bool LoadEsmExteriorCell(const EsmWorldIndex& index,
                         int32_t cellX,
                         int32_t cellY,
                         EsmLoadedCell* outCell,
                         std::string* outErr) {
    if (!outCell) {
        if (outErr) *outErr = "outCell is null";
        return false;
    }
    const EsmCellIndexEntry* entry = index.FindCell(cellX, cellY);
    if (!entry) {
        if (outErr) *outErr = "cell (" + std::to_string(cellX) + "," + std::to_string(cellY) + ") not found in worldspace index";
        return false;
    }

    std::ifstream f(index.esmPath, std::ios::binary | std::ios::ate);
    if (!f) {
        if (outErr) *outErr = "failed to reopen ESM file: " + index.esmPath;
        return false;
    }
    const uint64_t fileSize = static_cast<uint64_t>(f.tellg());

    *outCell = EsmLoadedCell{};
    outCell->meta = *entry;

    const uint32_t tagGRUP = MakeTag("GRUP");
    const uint32_t tagLAND = MakeTag("LAND");
    const uint32_t tagREFR = MakeTag("REFR");
    const uint32_t tagNAME = MakeTag("NAME");
    const uint32_t tagDATA = MakeTag("DATA");
    const uint32_t tagXSCL = MakeTag("XSCL");
    const uint32_t tagEDID = MakeTag("EDID");

    if (entry->cellGrupFileOffset == 0ull || entry->cellGrupByteSize < kHeaderSize) {
        if (outErr) *outErr = "cell has no Cell Children GRUP";
        return false;
    }

    auto walkCellChildren = [&](auto&& self, uint64_t startPos, uint64_t endPos, bool persistent, int depth) -> void {
        if (depth > 3 || startPos >= endPos || endPos > fileSize) return;
        uint64_t pos = startPos;
        while (pos + kHeaderSize <= endPos) {
            EsmRawHeader hdr{};
            if (!ReadHeaderAt(f, pos, fileSize, &hdr)) break;
            if (hdr.tag == tagGRUP) {
                if (hdr.size < kHeaderSize || pos + hdr.size > endPos) break;
                const bool isPers = persistent || (hdr.field12 == 8u);
                self(self, pos + kHeaderSize, pos + hdr.size, isPers, depth + 1);
                pos += hdr.size;
            } else {
                if (hdr.tag == tagLAND) {
                    std::vector<uint8_t> payload;
                    if (ReadRecordPayload(f, pos, fileSize, hdr, &payload)) {
                        DecodeEsmLandPayload(payload.data(), payload.size(), hdr.field12,
                                             index.landTextureEdids, &outCell->land, nullptr);
                    }
                } else if (hdr.tag == tagREFR) {
                    std::vector<uint8_t> payload;
                    if (ReadRecordPayload(f, pos, fileSize, hdr, &payload)) {
                        EsmRefPlacement ref{};
                        ref.refrFormId = hdr.field12;
                        ref.persistent = persistent;
                        bool hasData = false;
                        ForEachSubrecord(payload.data(), payload.size(), [&](uint32_t stag, const uint8_t* sdata, uint32_t slen) {
                            if (stag == tagNAME && slen >= 4u) {
                                ref.baseFormId = ReadU32LE(sdata);
                            } else if (stag == tagDATA && slen >= 24u) {
                                ref.position    = {ReadF32LE(sdata + 0),  ReadF32LE(sdata + 4),  ReadF32LE(sdata + 8)};
                                ref.rotationRad = {ReadF32LE(sdata + 12), ReadF32LE(sdata + 16), ReadF32LE(sdata + 20)};
                                hasData = true;
                            } else if (stag == tagXSCL && slen >= 4u) {
                                const float sc = ReadF32LE(sdata);
                                if (std::isfinite(sc) && sc > 0.01f && sc < 100.0f) ref.scale = sc;
                            } else if (stag == tagEDID) {
                                ref.edid = ReadZString(sdata, slen);
                            }
                        });
                        if (hasData && ref.baseFormId != 0u &&
                            std::isfinite(ref.position.x) && std::isfinite(ref.position.y) && std::isfinite(ref.position.z)) {
                            PropInstance prop{};
                            prop.kind     = GuessPropKindForRefr(ref);
                            prop.biome    = BiomeId::OldGrowthConiferHighlands;
                            prop.material = TerrainMaterialId::RockGranite;
                            prop.position = ref.position;
                            prop.upNormal = {0.0f, 0.0f, 1.0f};
                            prop.yawRad   = ref.rotationRad.z;
                            prop.scale    = ref.scale;
                            prop.seed     = ref.refrFormId ^ (ref.baseFormId * 2654435761u);
                            outCell->props.push_back(prop);
                            outCell->placements.push_back(std::move(ref));
                        }
                    }
                }
                pos += kHeaderSize + hdr.size;
            }
        }
    };

    walkCellChildren(walkCellChildren,
                     entry->cellGrupFileOffset + kHeaderSize,
                     entry->cellGrupFileOffset + entry->cellGrupByteSize,
                     false, 0);

    if (!outCell->land.hasVhgt) {
        if (outErr) *outErr = "exterior cell has no decodable LAND/VHGT record";
        return false;
    }
    return true;
}

bool BuildTerrainChunkFromEsmCell(const EsmLoadedCell& cell,
                                  float defaultWaterHeightZ,
                                  TerrainChunkData* outChunk,
                                  std::string* outErr) {
    if (!outChunk || !cell.land.hasVhgt) {
        if (outErr) *outErr = "cannot build TerrainChunkData: missing VHGT heightfield";
        return false;
    }

    TerrainChunkData chunk{};
    chunk.chunkX     = cell.meta.cellX;
    chunk.chunkY     = cell.meta.cellY;
    chunk.lodLevel   = 1u; // 32x32 quads (exact Skyrim 33x33 LAND native resolution!)
    chunk.gridN      = 32u;
    chunk.stride     = kEsmLandGridSize;
    chunk.stitchMask = StitchNone;
    chunk.chunkSizeU = kEsmCellSizeU;
    chunk.originX    = float(cell.meta.cellX) * kEsmCellSizeU;
    chunk.originY    = float(cell.meta.cellY) * kEsmCellSizeU;
    chunk.minZ       = cell.land.minHeightZ;
    chunk.maxZ       = cell.land.maxHeightZ;

    const size_t n = kEsmLandVertCount;
    chunk.heights.assign(cell.land.heightsZ.begin(), cell.land.heightsZ.end());
    chunk.normals.assign(cell.land.normals.begin(), cell.land.normals.end());
    chunk.curvatures.assign(n, 0.0f);
    chunk.flow.assign(n, 0.0f);
    chunk.erosion.assign(n, 0.0f);
    chunk.snow.assign(n, 0.0f);
    chunk.wetness.assign(n, 0.0f);
    chunk.riverDepth.assign(n, 0.0f);
    chunk.borderBlend.assign(n, 0.0f); // Native Tamriel interior — zero frontier blend
    chunk.splat0.assign(cell.land.splat0.begin(), cell.land.splat0.end());
    chunk.splat1.assign(cell.land.splat1.begin(), cell.land.splat1.end());
    chunk.emissiveMask.assign(n, 0.0f);
    chunk.primaryBiome.assign(n, static_cast<uint8_t>(BiomeId::OldGrowthConiferHighlands));

    const float effWaterZ = cell.meta.hasWaterHeight ? cell.meta.waterHeight : defaultWaterHeightZ;
    constexpr float kStepU = kEsmCellSizeU / 32.0f;

    for (uint32_t y = 0; y < kEsmLandGridSize; ++y) {
        for (uint32_t x = 0; x < kEsmLandGridSize; ++x) {
            const uint32_t idx = y * kEsmLandGridSize + x;
            const uint32_t xm = x > 0 ? x - 1 : 0;
            const uint32_t xp = x + 1 < kEsmLandGridSize ? x + 1 : kEsmLandGridSize - 1;
            const uint32_t ym = y > 0 ? y - 1 : 0;
            const uint32_t yp = y + 1 < kEsmLandGridSize ? y + 1 : kEsmLandGridSize - 1;

            const float hC = chunk.heights[idx];
            const float avgN = 0.25f * (chunk.heights[y * kEsmLandGridSize + xm] +
                                        chunk.heights[y * kEsmLandGridSize + xp] +
                                        chunk.heights[ym * kEsmLandGridSize + x] +
                                        chunk.heights[yp * kEsmLandGridSize + x]);
            chunk.curvatures[idx] = Clamp((hC - avgN) / (kStepU * 0.25f), -1.0f, 1.0f);
            chunk.snow[idx]       = chunk.splat0[idx].y;
            if (effWaterZ > -1.0e6f) {
                const float depthBelowWater = effWaterZ - hC;
                if (depthBelowWater > 0.0f) {
                    chunk.riverDepth[idx] = depthBelowWater;
                    chunk.wetness[idx]    = 1.0f;
                } else {
                    chunk.wetness[idx] = 1.0f - Smoothstep(0.0f, 180.0f, -depthBelowWater);
                }
            }
            chunk.normals[idx] = Normalize(chunk.normals[idx]);
            NormalizeSplat8(chunk.splat0[idx], chunk.splat1[idx]);
        }
    }

    *outChunk = std::move(chunk);
    return true;
}

bool BuildMeshFromEsmCell(const EsmLoadedCell& cell,
                          float defaultWaterHeightZ,
                          uint32_t maxMeshletTris,
                          ProceduralMesh* outMesh,
                          TerrainChunkData* outChunkOpt,
                          std::string* outErr) {
    if (!outMesh) {
        if (outErr) *outErr = "outMesh is null";
        return false;
    }
    TerrainChunkData chunk{};
    if (!BuildTerrainChunkFromEsmCell(cell, defaultWaterHeightZ, &chunk, outErr)) {
        return false;
    }
    const uint32_t clampedTris = std::clamp(maxMeshletTris, 16u, 126u);
    if (!BuildTerrainChunkMesh(chunk, clampedTris, outMesh)) {
        if (outErr) *outErr = "BuildTerrainChunkMesh failed for ESM cell";
        return false;
    }
    char nameBuf[128];
    std::snprintf(nameBuf, sizeof(nameBuf), "esm_%s_cell_%d_%d",
                  cell.meta.edid.empty() ? "tamriel" : cell.meta.edid.c_str(),
                  cell.meta.cellX, cell.meta.cellY);
    outMesh->name = nameBuf;
    if (outChunkOpt) *outChunkOpt = std::move(chunk);
    return true;
}

bool ExportEsmCellToGlb(const EsmLoadedCell& cell, float defaultWaterHeightZ, const std::string& outGlbPath,
                        bool zUpToYUp, std::string* outErr) {
    ProceduralMesh mesh{};
    if (!BuildMeshFromEsmCell(cell, defaultWaterHeightZ, 126u, &mesh, nullptr, outErr)) return false;
    if (!ExportMeshToGlbFile(mesh, outGlbPath, zUpToYUp)) {
        if (outErr) *outErr = "ExportMeshToGlbFile failed: " + outGlbPath;
        return false;
    }
    return true;
}

bool ExportEsmCellToObj(const EsmLoadedCell& cell, float defaultWaterHeightZ, const std::string& outObjPath,
                        std::string* outErr) {
    ProceduralMesh mesh{};
    if (!BuildMeshFromEsmCell(cell, defaultWaterHeightZ, 126u, &mesh, nullptr, outErr)) return false;
    if (!ExportMeshToObjFile(mesh, outObjPath)) {
        if (outErr) *outErr = "ExportMeshToObjFile failed: " + outObjPath;
        return false;
    }
    return true;
}

}  // namespace raw::worldgen
