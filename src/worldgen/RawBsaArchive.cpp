// RawBsaArchive.cpp: 100% Clean-Room Native Bethesda `.BSA` Archive Reader (v103/v104 zlib + v105 LZ4 Frame).
// Zero third-party library, Bethesda SDK, or platform dependencies. Original work, MIT.

#include "RawBsaArchive.h"
#include "RawEsmWorldLoader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>

namespace raw::worldgen {
namespace {

inline uint16_t ReadU16LE(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

inline uint32_t ReadU32LE(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline uint64_t ReadU64LE(const uint8_t* p) {
    return uint64_t(ReadU32LE(p)) | (uint64_t(ReadU32LE(p + 4)) << 32);
}

inline void AppendU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v & 0xFFu));
    out.push_back(uint8_t((v >> 8) & 0xFFu));
    out.push_back(uint8_t((v >> 16) & 0xFFu));
    out.push_back(uint8_t((v >> 24) & 0xFFu));
}

// Clean-room XXH32 hash (used for LZ4 Frame header checksum byte HC = (XXH32(desc, 0) >> 8) & 0xFF).
uint32_t Xxh32(const uint8_t* data, size_t len, uint32_t seed = 0u) {
    constexpr uint32_t P1 = 2654435761u;
    constexpr uint32_t P2 = 2246822519u;
    constexpr uint32_t P3 = 3266489917u;
    constexpr uint32_t P4 = 668265263u;
    constexpr uint32_t P5 = 374761393u;
    auto rotl = [](uint32_t x, int r) -> uint32_t { return (x << r) | (x >> (32 - r)); };

    size_t p = 0;
    uint32_t h32 = 0u;
    if (len >= 16u) {
        uint32_t v1 = seed + P1 + P2;
        uint32_t v2 = seed + P2;
        uint32_t v3 = seed;
        uint32_t v4 = seed - P1;
        while (p + 16u <= len) {
            v1 = rotl(v1 + ReadU32LE(data + p) * P2, 13) * P1; p += 4;
            v2 = rotl(v2 + ReadU32LE(data + p) * P2, 13) * P1; p += 4;
            v3 = rotl(v3 + ReadU32LE(data + p) * P2, 13) * P1; p += 4;
            v4 = rotl(v4 + ReadU32LE(data + p) * P2, 13) * P1; p += 4;
        }
        h32 = rotl(v1, 1) + rotl(v2, 7) + rotl(v3, 12) + rotl(v4, 18);
    } else {
        h32 = seed + P5;
    }
    h32 += static_cast<uint32_t>(len);
    while (p + 4u <= len) {
        h32 = rotl(h32 + ReadU32LE(data + p) * P3, 17) * P4;
        p += 4u;
    }
    while (p < len) {
        h32 = rotl(h32 + uint32_t(data[p++]) * P5, 11) * P1;
    }
    h32 ^= h32 >> 15; h32 *= P2;
    h32 ^= h32 >> 13; h32 *= P3;
    h32 ^= h32 >> 16;
    return h32;
}

void SetErr(std::string* outErr, const std::string& msg) {
    if (outErr) *outErr = msg;
}

}  // namespace

// ============================================================================
// Clean-Room LZ4 Block & LZ4 Frame (0x184D2204) Codec
// ============================================================================

bool DecompressLz4Block(const uint8_t* src, size_t srcLen, size_t maxOutBytes, std::vector<uint8_t>* out) {
    if (!out || (!src && srcLen > 0u)) return false;
    size_t p = 0u;
    while (p < srcLen) {
        const uint8_t token = src[p++];
        size_t litLen = size_t(token >> 4);
        if (litLen == 15u) {
            while (true) {
                if (p >= srcLen) return false;
                const uint8_t s = src[p++];
                litLen += s;
                if (s != 255u) break;
                if (litLen > srcLen) return false;
            }
        }
        if (p + litLen > srcLen) return false;
        if (maxOutBytes > 0u && out->size() + litLen > maxOutBytes) return false;
        out->insert(out->end(), src + p, src + p + litLen);
        p += litLen;

        // End of block: final LZ4 sequence contains literals only (no offset/match).
        if (p == srcLen) break;

        if (p + 2u > srcLen) return false;
        const uint16_t offset = ReadU16LE(src + p);
        p += 2u;
        if (offset == 0u || size_t(offset) > out->size()) return false;

        size_t matchLen = size_t(token & 0x0Fu) + 4u;
        if ((token & 0x0Fu) == 15u) {
            while (true) {
                if (p >= srcLen) return false;
                const uint8_t s = src[p++];
                matchLen += s;
                if (s != 255u) break;
                if (maxOutBytes > 0u && out->size() + matchLen > maxOutBytes) return false;
            }
        }
        if (maxOutBytes > 0u && out->size() + matchLen > maxOutBytes) return false;

        // Byte-by-byte copy handles overlapping RLE matches (offset < matchLen) faithfully!
        const size_t baseSize = out->size();
        out->resize(baseSize + matchLen);
        uint8_t* dst = out->data();
        for (size_t i = 0u; i < matchLen; ++i) {
            dst[baseSize + i] = dst[baseSize + i - size_t(offset)];
        }
    }
    return true;
}

bool DecompressLz4Frame(const uint8_t* src, size_t srcLen, size_t expectedOutBytes,
                        std::vector<uint8_t>* out, std::string* outErr) {
    if (!src || !out || srcLen < 7u) {
        SetErr(outErr, "LZ4 frame input truncated (<7 bytes).");
        return false;
    }
    out->clear();
    if (expectedOutBytes > 0u && expectedOutBytes <= 256ull * 1024ull * 1024ull) {
        out->reserve(expectedOutBytes);
    }

    size_t pos = 0u;
    // Skip any leading LZ4 skippable frames (0x184D2A50..0x184D2A5F) if present.
    while (pos + 8u <= srcLen) {
        const uint32_t m = ReadU32LE(src + pos);
        if (m >= 0x184D2A50u && m <= 0x184D2A5Fu) {
            const uint32_t skipSz = ReadU32LE(src + pos + 4u);
            if (pos + 8u + size_t(skipSz) > srcLen) {
                SetErr(outErr, "LZ4 skippable frame truncated.");
                return false;
            }
            pos += 8u + size_t(skipSz);
        } else {
            break;
        }
    }

    if (pos + 7u > srcLen) {
        SetErr(outErr, "LZ4 frame header missing after skippable frames.");
        return false;
    }
    const uint32_t magic = ReadU32LE(src + pos);
    if (magic != kLz4FrameMagic) {
        SetErr(outErr, "Invalid LZ4 frame magic (expected 0x184D2204).");
        return false;
    }
    pos += 4u;

    const uint8_t flg = src[pos++];
    const uint8_t bd  = src[pos++];
    (void)bd;
    if (((flg >> 6) & 0x03u) != 1u || (flg & 0x02u) != 0u) {
        SetErr(outErr, "Unsupported LZ4 frame FLG version or reserved bit set.");
        return false;
    }
    const bool hasBlockChecksum   = (flg & 0x10u) != 0u;
    const bool hasContentSize     = (flg & 0x08u) != 0u;
    const bool hasContentChecksum = (flg & 0x04u) != 0u;
    const bool hasDictId          = (flg & 0x01u) != 0u;

    uint64_t frameContentSize = 0ull;
    if (hasContentSize) {
        if (pos + 8u > srcLen) { SetErr(outErr, "LZ4 frame truncated in contentSize."); return false; }
        frameContentSize = ReadU64LE(src + pos);
        pos += 8u;
        if (frameContentSize > 0ull && frameContentSize <= 256ull * 1024ull * 1024ull && out->capacity() < frameContentSize) {
            out->reserve(static_cast<size_t>(frameContentSize));
        }
    }
    if (hasDictId) {
        if (pos + 4u > srcLen) { SetErr(outErr, "LZ4 frame truncated in dictId."); return false; }
        pos += 4u;
    }
    if (pos + 1u > srcLen) { SetErr(outErr, "LZ4 frame truncated in header checksum."); return false; }
    pos += 1u; // Header checksum HC

    // Safety cap to prevent decompression bombs
    const size_t maxAllowed = (expectedOutBytes > 0u)
                            ? std::max<size_t>(expectedOutBytes, size_t(frameContentSize)) + 64u
                            : (256ull * 1024ull * 1024ull);

    while (true) {
        if (pos + 4u > srcLen) {
            SetErr(outErr, "LZ4 frame truncated waiting for block size / EndMark.");
            return false;
        }
        const uint32_t rawBlockSz = ReadU32LE(src + pos);
        pos += 4u;
        if (rawBlockSz == 0u) break; // EndMark!

        const bool isUncompressed = (rawBlockSz & 0x80000000u) != 0u;
        const uint32_t blockBytes = rawBlockSz & 0x7FFFFFFFu;
        if (pos + size_t(blockBytes) > srcLen) {
            SetErr(outErr, "LZ4 block data exceeds input buffer.");
            return false;
        }

        if (isUncompressed) {
            if (out->size() + size_t(blockBytes) > maxAllowed) {
                SetErr(outErr, "LZ4 uncompressed block exceeds max output size.");
                return false;
            }
            out->insert(out->end(), src + pos, src + pos + blockBytes);
        } else {
            if (!DecompressLz4Block(src + pos, blockBytes, maxAllowed, out)) {
                SetErr(outErr, "LZ4 block decode failed (corrupt token/offset).");
                return false;
            }
        }
        pos += size_t(blockBytes);

        if (hasBlockChecksum) {
            if (pos + 4u > srcLen) { SetErr(outErr, "LZ4 frame truncated in block checksum."); return false; }
            pos += 4u;
        }
    }

    if (hasContentChecksum) {
        if (pos + 4u > srcLen) { SetErr(outErr, "LZ4 frame truncated in content checksum."); return false; }
        const uint32_t expectedHash = ReadU32LE(src + pos);
        const uint32_t actualHash = Xxh32(out->data(), out->size(), 0u);
        if (expectedHash != actualHash) {
            SetErr(outErr, "LZ4 frame content XXH32 checksum mismatch.");
            return false;
        }
        pos += 4u;
    }

    if (hasContentSize && out->size() != size_t(frameContentSize)) {
        SetErr(outErr, "LZ4 decoded size mismatch vs frame contentSize.");
        return false;
    }
    if (expectedOutBytes > 0u && out->size() != expectedOutBytes) {
        SetErr(outErr, "LZ4 decoded size mismatch vs expected BSA uncompressed size.");
        return false;
    }
    return true;
}

bool CompressLz4Frame(const uint8_t* src, size_t srcLen, std::vector<uint8_t>* out) {
    if (!out || (!src && srcLen > 0u)) return false;
    out->clear();

    // Encode a single LZ4 block using a 4096-entry 4-byte hash table (produces real LZ4 match tokens!)
    std::vector<uint8_t> block;
    block.reserve(srcLen + srcLen / 32u + 16u);

    auto emitLenExt = [&](size_t extra) {
        while (extra >= 255u) {
            block.push_back(255u);
            extra -= 255u;
        }
        block.push_back(static_cast<uint8_t>(extra));
    };

    std::array<int32_t, 4096> hashTab;
    hashTab.fill(-1);

    size_t anchor = 0u;
    size_t ip = 0u;
    const size_t matchLimit = (srcLen > 12u) ? (srcLen - 12u) : 0u;

    while (ip < matchLimit) {
        const uint32_t seq = ReadU32LE(src + ip);
        const uint32_t h = ((seq * 2654435761u) >> 20) & 4095u;
        const int32_t ref = hashTab[h];
        hashTab[h] = static_cast<int32_t>(ip);

        if (ref >= 0 && ip > size_t(ref) && (ip - size_t(ref)) <= 65535u &&
            ReadU32LE(src + ref) == seq) {
            // Found 4+ byte match: extend greedily while leaving last 5 bytes as literals per LZ4 spec
            size_t matchLen = 4u;
            const size_t maxMatch = (srcLen > 5u) ? (srcLen - 5u - ip) : 0u;
            while (matchLen < maxMatch && src[size_t(ref) + matchLen] == src[ip + matchLen]) {
                ++matchLen;
            }
            const size_t litLen = ip - anchor;
            const size_t mlCode = matchLen - 4u;
            const uint8_t tok = static_cast<uint8_t>(
                ((std::min<size_t>(litLen, 15u)) << 4) | std::min<size_t>(mlCode, 15u));
            block.push_back(tok);
            if (litLen >= 15u) emitLenExt(litLen - 15u);
            block.insert(block.end(), src + anchor, src + anchor + litLen);

            const uint16_t off = static_cast<uint16_t>(ip - size_t(ref));
            block.push_back(uint8_t(off & 0xFFu));
            block.push_back(uint8_t((off >> 8) & 0xFFu));
            if (mlCode >= 15u) emitLenExt(mlCode - 15u);

            ip += matchLen;
            anchor = ip;
        } else {
            ++ip;
        }
    }

    // Final trailing literals sequence (required by LZ4 block format)
    const size_t lastLit = srcLen - anchor;
    if (lastLit > 0u || srcLen == 0u) {
        const uint8_t tok = static_cast<uint8_t>(std::min<size_t>(lastLit, 15u) << 4);
        block.push_back(tok);
        if (lastLit >= 15u) emitLenExt(lastLit - 15u);
        if (lastLit > 0u) {
            block.insert(block.end(), src + anchor, src + srcLen);
        }
    }

    // Emit valid LZ4 Frame header: Magic + FLG(0x64: v01, indep, contentChecksum) + BD(0x70: 4MB) + HC
    AppendU32LE(*out, kLz4FrameMagic);
    const uint8_t desc[2] = {0x64u, 0x70u};
    out->push_back(desc[0]);
    out->push_back(desc[1]);
    const uint8_t hc = static_cast<uint8_t>((Xxh32(desc, 2u, 0u) >> 8) & 0xFFu);
    out->push_back(hc);

    if (srcLen > 0u) {
        AppendU32LE(*out, static_cast<uint32_t>(block.size()));
        out->insert(out->end(), block.begin(), block.end());
    }
    AppendU32LE(*out, 0u); // EndMark
    AppendU32LE(*out, Xxh32(src, srcLen, 0u)); // Content XXH32 checksum
    return true;
}

// ============================================================================
// Clean-Room Native `.BSA` Reader (v103/v104 zlib & v105 LZ4 Frame)
// ============================================================================

std::string NormalizeBsaVirtualPath(const std::string& rawPath) {
    std::string out;
    out.reserve(rawPath.size());
    for (char c : rawPath) {
        if (c == '\\') c = '/';
        else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c == '/' && (out.empty() || out.back() == '/')) continue;
        out.push_back(c);
    }
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out;
}

void RawBsaArchive::Close() {
    open_ = false;
    fromMemory_ = false;
    path_.clear();
    fileBytes_ = 0ull;
    memBuffer_.clear();
    header_ = {};
    entries_.clear();
    byPath_.clear();
    stats_ = {};
}

bool RawBsaArchive::ParseIndexFromBuffer(const uint8_t* idxBuf, size_t idxSize,
                                         uint64_t totalFileSize, std::string* outErr) {
    const bool isV105 = (header_.version == kBsaVersionSkyrimSE);
    const size_t folderRecSize = isV105 ? 24u : 16u;
    const size_t folderTableBytes = size_t(header_.folderCount) * folderRecSize;
    if (folderTableBytes > idxSize) {
        SetErr(outErr, "BSA folder record table exceeds index buffer.");
        return false;
    }

    struct FolderRec {
        uint64_t nameHash = 0ull;
        uint32_t fileCount = 0u;
        uint64_t blockOffset = 0ull;
    };
    std::vector<FolderRec> folders(header_.folderCount);
    uint64_t sumFiles = 0ull;
    for (uint32_t f = 0u; f < header_.folderCount; ++f) {
        const uint8_t* rec = idxBuf + size_t(f) * folderRecSize;
        folders[f].nameHash  = ReadU64LE(rec + 0u);
        folders[f].fileCount = ReadU32LE(rec + 8u);
        folders[f].blockOffset = isV105 ? ReadU64LE(rec + 16u) : uint64_t(ReadU32LE(rec + 12u));
        sumFiles += folders[f].fileCount;
    }
    if (sumFiles != header_.fileCount) {
        SetErr(outErr, "BSA folder file count sum disagrees with header.fileCount.");
        return false;
    }

    const bool hasDirNames  = (header_.archiveFlags & kBsaFlagDirNames) != 0u;
    const bool hasFileNames = (header_.archiveFlags & kBsaFlagFileNames) != 0u;
    const bool defaultComp  = (header_.archiveFlags & kBsaFlagCompressed) != 0u;

    size_t pos = folderTableBytes;
    std::vector<std::string> folderNames(header_.folderCount);
    entries_.clear();
    entries_.reserve(header_.fileCount);
    byPath_.clear();
    byPath_.reserve(header_.fileCount);

    struct RawFileRec {
        uint32_t folderIdx = 0u;
        uint64_t nameHash  = 0ull;
        uint32_t storedSz  = 0u;
        uint32_t offset    = 0u;
        bool     comp      = false;
    };
    std::vector<RawFileRec> rawFiles;
    rawFiles.reserve(header_.fileCount);

    for (uint32_t f = 0u; f < header_.folderCount; ++f) {
        if (hasDirNames) {
            if (pos + 1u > idxSize) { SetErr(outErr, "BSA truncated in folder name length."); return false; }
            const uint8_t nameLen = idxBuf[pos++];
            if (pos + size_t(nameLen) > idxSize) { SetErr(outErr, "BSA truncated in folder name string."); return false; }
            size_t strLen = nameLen;
            while (strLen > 0u && idxBuf[pos + strLen - 1u] == 0u) --strLen;
            folderNames[f].assign(reinterpret_cast<const char*>(idxBuf + pos), strLen);
            pos += size_t(nameLen);
        }
        const size_t fileBlockBytes = size_t(folders[f].fileCount) * 16u;
        if (pos + fileBlockBytes > idxSize) {
            SetErr(outErr, "BSA truncated in file record block.");
            return false;
        }
        for (uint32_t i = 0u; i < folders[f].fileCount; ++i) {
            const uint8_t* fr = idxBuf + pos + size_t(i) * 16u;
            const uint64_t fHash = ReadU64LE(fr + 0u);
            const uint32_t szFld = ReadU32LE(fr + 8u);
            const uint32_t off   = ReadU32LE(fr + 12u);
            const bool invert    = (szFld & 0x40000000u) != 0u;
            const uint32_t sz    = szFld & 0x3FFFFFFFu;
            if (uint64_t(off) + uint64_t(sz) > totalFileSize) {
                SetErr(outErr, "BSA file entry payload extends beyond archive EOF.");
                return false;
            }
            rawFiles.push_back({f, fHash, sz, off, defaultComp != invert});
        }
        pos += fileBlockBytes;
    }

    // Parse global null-terminated file name table
    const size_t nameBlockStart = pos;
    if (hasFileNames && nameBlockStart + size_t(header_.totalFileNameLength) > idxSize) {
        SetErr(outErr, "BSA file name table truncated.");
        return false;
    }

    uint32_t compCount = 0u;
    uint32_t uncompCount = 0u;
    uint64_t totalStored = 0ull;

    for (size_t i = 0u; i < rawFiles.size(); ++i) {
        std::string fileName;
        if (hasFileNames) {
            const size_t start = pos;
            while (pos < idxSize && idxBuf[pos] != 0u) ++pos;
            if (pos >= idxSize) { SetErr(outErr, "BSA file name missing null terminator."); return false; }
            fileName.assign(reinterpret_cast<const char*>(idxBuf + start), pos - start);
            ++pos; // skip '\0'
        } else {
            fileName = "file_" + std::to_string(i);
        }

        const RawFileRec& rf = rawFiles[i];
        std::string fullPath = folderNames[rf.folderIdx].empty()
                             ? fileName
                             : (folderNames[rf.folderIdx] + "/" + fileName);
        fullPath = NormalizeBsaVirtualPath(fullPath);

        BsaEntryInfo info{};
        info.virtualPath = fullPath;
        info.folderHash  = folders[rf.folderIdx].nameHash;
        info.fileHash    = rf.nameHash;
        info.dataOffset  = rf.offset;
        info.storedSize  = rf.storedSz;
        info.compressed  = rf.comp;

        if (rf.comp) ++compCount;
        else         ++uncompCount;
        totalStored += rf.storedSz;

        const size_t entryIdx = entries_.size();
        entries_.push_back(std::move(info));
        byPath_.emplace(entries_.back().virtualPath, entryIdx);
    }

    stats_.path              = path_;
    stats_.version           = header_.version;
    stats_.archiveFlags      = header_.archiveFlags;
    stats_.folderCount       = header_.folderCount;
    stats_.fileCount         = header_.fileCount;
    stats_.compressedFiles   = compCount;
    stats_.uncompressedFiles = uncompCount;
    stats_.archiveFileBytes  = totalFileSize;
    stats_.totalStoredBytes  = totalStored;
    stats_.embedsFileNames   = (header_.version >= kBsaVersionSkyrimLE) &&
                               ((header_.archiveFlags & kBsaFlagEmbedFileNames) != 0u);
    open_ = true;
    return true;
}

bool RawBsaArchive::Open(const std::string& path, std::string* outErr) {
    Close();
    if (path.empty()) {
        SetErr(outErr, "Empty BSA path.");
        return false;
    }
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        SetErr(outErr, "Failed to open BSA file: " + path);
        return false;
    }
    const std::streamoff endPos = f.tellg();
    if (endPos < 36) {
        SetErr(outErr, "BSA file smaller than 36-byte header.");
        return false;
    }
    const uint64_t totalFileSize = static_cast<uint64_t>(endPos);
    f.seekg(0, std::ios::beg);

    uint8_t hdr[36]{};
    if (!f.read(reinterpret_cast<char*>(hdr), 36)) {
        SetErr(outErr, "Failed to read 36-byte BSA header.");
        return false;
    }
    if (ReadU32LE(hdr + 0u) != kBsaMagic) {
        SetErr(outErr, "Invalid BSA magic (expected 'BSA\\0').");
        return false;
    }
    header_.version               = ReadU32LE(hdr + 4u);
    header_.folderOffset          = ReadU32LE(hdr + 8u);
    header_.archiveFlags          = ReadU32LE(hdr + 12u);
    header_.folderCount           = ReadU32LE(hdr + 16u);
    header_.fileCount             = ReadU32LE(hdr + 20u);
    header_.totalFolderNameLength = ReadU32LE(hdr + 24u);
    header_.totalFileNameLength   = ReadU32LE(hdr + 28u);
    header_.fileFlags             = ReadU32LE(hdr + 32u);

    if (header_.version != kBsaVersionOblivion &&
        header_.version != kBsaVersionSkyrimLE &&
        header_.version != kBsaVersionSkyrimSE) {
        SetErr(outErr, "Unsupported BSA version: " + std::to_string(header_.version));
        return false;
    }
    if (header_.folderOffset < 36u || uint64_t(header_.folderOffset) > totalFileSize) {
        SetErr(outErr, "Invalid BSA folderOffset.");
        return false;
    }

    // Compute exact byte size of the entire index region (folder records + file record blocks + file names)
    const uint64_t folderRecSize = (header_.version == kBsaVersionSkyrimSE) ? 24ull : 16ull;
    const uint64_t hasDirNames = (header_.archiveFlags & kBsaFlagDirNames) ? 1ull : 0ull;
    const uint64_t hasFileNames = (header_.archiveFlags & kBsaFlagFileNames) ? 1ull : 0ull;
    const uint64_t indexBytes =
        uint64_t(header_.folderCount) * folderRecSize +
        hasDirNames * (uint64_t(header_.folderCount) + uint64_t(header_.totalFolderNameLength)) +
        uint64_t(header_.fileCount) * 16ull +
        hasFileNames * uint64_t(header_.totalFileNameLength);

    if (uint64_t(header_.folderOffset) + indexBytes > totalFileSize || indexBytes > 256ull * 1024ull * 1024ull) {
        SetErr(outErr, "BSA index region exceeds archive file size.");
        return false;
    }

    std::vector<uint8_t> idxBuf(static_cast<size_t>(indexBytes));
    f.seekg(static_cast<std::streamoff>(header_.folderOffset), std::ios::beg);
    if (indexBytes > 0ull && !f.read(reinterpret_cast<char*>(idxBuf.data()), static_cast<std::streamsize>(indexBytes))) {
        SetErr(outErr, "Failed to read BSA index block.");
        return false;
    }

    path_ = path;
    fileBytes_ = totalFileSize;
    fromMemory_ = false;
    return ParseIndexFromBuffer(idxBuf.data(), idxBuf.size(), totalFileSize, outErr);
}

bool RawBsaArchive::OpenFromMemory(const uint8_t* data, size_t size,
                                   const std::string& label, std::string* outErr) {
    Close();
    if (!data || size < 36u) {
        SetErr(outErr, "In-memory BSA buffer smaller than 36 bytes.");
        return false;
    }
    if (ReadU32LE(data + 0u) != kBsaMagic) {
        SetErr(outErr, "Invalid BSA magic (expected 'BSA\\0').");
        return false;
    }
    header_.version               = ReadU32LE(data + 4u);
    header_.folderOffset          = ReadU32LE(data + 8u);
    header_.archiveFlags          = ReadU32LE(data + 12u);
    header_.folderCount           = ReadU32LE(data + 16u);
    header_.fileCount             = ReadU32LE(data + 20u);
    header_.totalFolderNameLength = ReadU32LE(data + 24u);
    header_.totalFileNameLength   = ReadU32LE(data + 28u);
    header_.fileFlags             = ReadU32LE(data + 32u);

    if (header_.version != kBsaVersionOblivion &&
        header_.version != kBsaVersionSkyrimLE &&
        header_.version != kBsaVersionSkyrimSE) {
        SetErr(outErr, "Unsupported BSA version: " + std::to_string(header_.version));
        return false;
    }
    if (header_.folderOffset < 36u || size_t(header_.folderOffset) > size) {
        SetErr(outErr, "Invalid BSA folderOffset.");
        return false;
    }

    path_ = label;
    fileBytes_ = uint64_t(size);
    fromMemory_ = true;
    memBuffer_.assign(data, data + size);
    return ParseIndexFromBuffer(memBuffer_.data() + header_.folderOffset,
                                memBuffer_.size() - size_t(header_.folderOffset),
                                fileBytes_, outErr);
}

bool RawBsaArchive::ReadSlice(uint64_t offset, uint32_t size, std::vector<uint8_t>* out, std::string* outErr) const {
    if (!open_ || !out) return false;
    if (offset + uint64_t(size) > fileBytes_) {
        SetErr(outErr, "BSA payload read out of archive bounds.");
        return false;
    }
    out->resize(size);
    if (size == 0u) return true;
    if (fromMemory_) {
        std::memcpy(out->data(), memBuffer_.data() + size_t(offset), size);
        return true;
    }
    std::ifstream f(path_, std::ios::binary);
    if (!f) {
        SetErr(outErr, "Failed to re-open BSA file for extraction: " + path_);
        return false;
    }
    f.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!f.read(reinterpret_cast<char*>(out->data()), static_cast<std::streamsize>(size))) {
        SetErr(outErr, "Failed to read BSA payload bytes.");
        return false;
    }
    return true;
}

std::vector<std::string> RawBsaArchive::ListFiles(const std::string& filter, bool prefixOnly) const {
    std::vector<std::string> out;
    if (!open_) return out;
    const std::string normFilter = NormalizeBsaVirtualPath(filter);
    out.reserve(normFilter.empty() ? entries_.size() : 64u);
    for (const auto& e : entries_) {
        if (normFilter.empty()) {
            out.push_back(e.virtualPath);
        } else if (prefixOnly) {
            if (e.virtualPath.rfind(normFilter, 0) == 0u) out.push_back(e.virtualPath);
        } else {
            if (e.virtualPath.find(normFilter) != std::string::npos) out.push_back(e.virtualPath);
        }
    }
    return out;
}

const BsaEntryInfo* RawBsaArchive::FindEntry(const std::string& virtualPath) const {
    if (!open_) return nullptr;
    const std::string norm = NormalizeBsaVirtualPath(virtualPath);
    auto it = byPath_.find(norm);
    return (it == byPath_.end()) ? nullptr : &entries_[it->second];
}

bool RawBsaArchive::HasFile(const std::string& virtualPath) const {
    return FindEntry(virtualPath) != nullptr;
}

bool RawBsaArchive::ExtractEntry(const BsaEntryInfo& entry, std::vector<uint8_t>* outBytes,
                                 std::string* outErr) const {
    if (!open_ || !outBytes) {
        SetErr(outErr, "BSA archive not open or null output vector.");
        return false;
    }
    std::vector<uint8_t> raw;
    if (!ReadSlice(entry.dataOffset, entry.storedSize, &raw, outErr)) return false;

    size_t pos = 0u;
    if (stats_.embedsFileNames) {
        if (pos + 1u > raw.size()) {
            SetErr(outErr, "BSA embedded filename length truncated.");
            return false;
        }
        const uint8_t nameLen = raw[pos++];
        if (pos + size_t(nameLen) > raw.size()) {
            SetErr(outErr, "BSA embedded filename exceeds stored record size.");
            return false;
        }
        pos += size_t(nameLen);
    }

    if (!entry.compressed) {
        outBytes->assign(raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.end());
        return true;
    }

    if (pos + 4u > raw.size()) {
        SetErr(outErr, "BSA compressed entry missing 4-byte uncompressed size prefix.");
        return false;
    }
    const uint32_t uncompressedSize = ReadU32LE(raw.data() + pos);
    pos += 4u;
    const uint8_t* compPtr = raw.data() + pos;
    const size_t   compLen = raw.size() - pos;

    if (header_.version == kBsaVersionSkyrimSE) {
        return DecompressLz4Frame(compPtr, compLen, uncompressedSize, outBytes, outErr);
    }

    // TES4/TES5 v103/v104: RFC 1950 zlib stream via clean-room InflateEsmZlib!
    if (!InflateEsmZlib(compPtr, compLen, uncompressedSize, outBytes)) {
        SetErr(outErr, "BSA v104 zlib inflate failed.");
        return false;
    }
    return true;
}

bool RawBsaArchive::ExtractFile(const std::string& virtualPath, std::vector<uint8_t>* outBytes,
                                std::string* outErr) const {
    const BsaEntryInfo* entry = FindEntry(virtualPath);
    if (!entry) {
        SetErr(outErr, "File not found in BSA archive: " + virtualPath);
        return false;
    }
    return ExtractEntry(*entry, outBytes, outErr);
}

BsaArchiveStats RawBsaArchive::GetArchiveStats() const {
    return stats_;
}

}  // namespace raw::worldgen
