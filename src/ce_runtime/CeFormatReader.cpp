// CeFormatReader.cpp: Clean-Room Open Creation Engine Plugin (.esm/.esp/.esl) & Archive (.bsa/.ba2) Reader.
// Original work, MIT.
#include "CeFormatReader.h"

#include <cstring>
#include <fstream>

namespace raw::ce {
namespace {

template <class T>
bool ReadVal(const uint8_t* p, size_t size, size_t& off, T* out) {
    if (off + sizeof(T) > size) return false;
    std::memcpy(out, p + off, sizeof(T));
    off += sizeof(T);
    return true;
}

std::string ReadZString(const std::vector<uint8_t>& d) {
    size_t len = d.size();
    while (len > 0 && d[len - 1] == 0) --len;
    return std::string(reinterpret_cast<const char*>(d.data()), len);
}

bool ParseSubrecords(const uint8_t* data, size_t len, PluginRecord& rec) {
    size_t off = 0;
    uint32_t extendedSize = 0;
    while (off < len) {
        if (off + 6 > len) return false;
        Subrecord sub{};
        std::memcpy(sub.type, data + off, 4);
        sub.type[4] = '\0';
        uint16_t subLen = 0;
        std::memcpy(&subLen, data + off + 4, 2);
        off += 6;

        uint32_t actualLen = subLen;
        if (std::memcmp(sub.type, "XXXX", 4) == 0 && subLen == 4 && off + 4 <= len) {
            std::memcpy(&extendedSize, data + off, 4);
            off += 4;
            continue;
        }
        if (subLen == 0 && extendedSize > 0) {
            actualLen = extendedSize;
            extendedSize = 0;
        }
        if (off + actualLen > len) return false;
        sub.data.assign(data + off, data + off + actualLen);
        off += actualLen;
        if (std::memcmp(sub.type, "EDID", 4) == 0 && rec.edid.empty()) {
            rec.edid = ReadZString(sub.data);
        }
        rec.subrecords.push_back(std::move(sub));
    }
    return true;
}

bool ParseRecordStream(const uint8_t* bytes, size_t endOff, size_t& off, PluginSummary& out, std::string* err, int depth) {
    if (depth > 8) {
        if (err) *err = "GRUP nesting exceeded maximum depth";
        return false;
    }
    while (off < endOff) {
        if (off + 24 > endOff) {
            if (err) *err = "truncated record/GRUP header";
            return false;
        }
        char tag[5]{};
        std::memcpy(tag, bytes + off, 4);
        uint32_t sz = 0;
        std::memcpy(&sz, bytes + off + 4, 4);
        if (std::memcmp(tag, "GRUP", 4) == 0) {
            if (sz < 24 || off + sz > endOff) {
                if (err) *err = "truncated GRUP block";
                return false;
            }
            const size_t grupEnd = off + sz;
            off += 24;
            if (!ParseRecordStream(bytes, grupEnd, off, out, err, depth + 1)) return false;
            off = grupEnd;
            continue;
        }
        PluginRecord rec{};
        std::memcpy(rec.type, tag, 5);
        std::memcpy(&rec.flags, bytes + off + 8, 4);
        std::memcpy(&rec.formId, bytes + off + 12, 4);
        std::memcpy(&rec.formVersion, bytes + off + 20, 2);
        rec.compressed = (rec.flags & 0x00040000u) != 0;
        off += 24;
        if (off + sz > endOff) {
            if (err) *err = std::string("truncated record payload for ") + rec.type;
            return false;
        }
        if (!rec.compressed) {
            if (!ParseSubrecords(bytes + off, sz, rec)) {
                if (err) *err = std::string("malformed subrecords in ") + rec.type;
                return false;
            }
        }
        off += sz;
        out.records.push_back(std::move(rec));
    }
    return true;
}

}  // namespace

bool ParsePluginBytes(const uint8_t* bytes, size_t size, PluginSummary* out, std::string* outErr) {
    if (!bytes || !out || size < 24) {
        if (outErr) *outErr = "plugin buffer too small for TES4 header";
        return false;
    }
    if (std::memcmp(bytes, "TES4", 4) != 0) {
        if (outErr) *outErr = "missing TES4 plugin magic";
        return false;
    }
    *out = {};
    uint32_t hdrDataSize = 0;
    std::memcpy(&hdrDataSize, bytes + 4, 4);
    std::memcpy(&out->formVersion, bytes + 20, 2);
    if (24ull + hdrDataSize > size) {
        if (outErr) *outErr = "truncated TES4 header payload";
        return false;
    }
    PluginRecord tes4{};
    std::memcpy(tes4.type, "TES4", 5);
    tes4.formVersion = out->formVersion;
    if (!ParseSubrecords(bytes + 24, hdrDataSize, tes4)) {
        if (outErr) *outErr = "malformed TES4 header subrecords";
        return false;
    }
    for (const auto& s : tes4.subrecords) {
        if (std::memcmp(s.type, "HEDR", 4) == 0 && s.data.size() >= 12) {
            std::memcpy(&out->headerVersion, s.data.data() + 0, 4);
            std::memcpy(&out->recordCount,   s.data.data() + 4, 4);
            std::memcpy(&out->nextObjectId,  s.data.data() + 8, 4);
        } else if (std::memcmp(s.type, "CNAM", 4) == 0) {
            out->author = ReadZString(s.data);
        } else if (std::memcmp(s.type, "SNAM", 4) == 0) {
            out->description = ReadZString(s.data);
        } else if (std::memcmp(s.type, "MAST", 4) == 0) {
            out->masters.push_back(ReadZString(s.data));
        }
    }
    size_t off = 24u + hdrDataSize;
    return ParseRecordStream(bytes, size, off, *out, outErr, 0);
}

bool ParsePluginFile(const std::string& path, PluginSummary* out, std::string* outErr) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (outErr) *outErr = "failed to open plugin: " + path;
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return ParsePluginBytes(bytes.data(), bytes.size(), out, outErr);
}

bool ParseBsaHeaderBytes(const uint8_t* bytes, size_t size, BsaHeader* out, std::string* outErr) {
    if (!bytes || !out || size < 36) {
        if (outErr) *outErr = "BSA buffer too small";
        return false;
    }
    if (bytes[0] != 'B' || bytes[1] != 'S' || bytes[2] != 'A' || bytes[3] != '\0') {
        if (outErr) *outErr = "invalid BSA magic";
        return false;
    }
    size_t off = 4;
    BsaHeader h{};
    if (!ReadVal(bytes, size, off, &h.version) ||
        !ReadVal(bytes, size, off, &h.folderOffset) ||
        !ReadVal(bytes, size, off, &h.archiveFlags) ||
        !ReadVal(bytes, size, off, &h.folderCount) ||
        !ReadVal(bytes, size, off, &h.fileCount) ||
        !ReadVal(bytes, size, off, &h.totalFolderNameLength) ||
        !ReadVal(bytes, size, off, &h.totalFileNameLength) ||
        !ReadVal(bytes, size, off, &h.fileFlags)) {
        if (outErr) *outErr = "truncated BSA header";
        return false;
    }
    if (h.version < 103 || h.version > 105 || h.folderOffset < 36) {
        if (outErr) *outErr = "unsupported BSA version or invalid folder offset";
        return false;
    }
    *out = h;
    return true;
}

bool ParseBa2HeaderBytes(const uint8_t* bytes, size_t size, Ba2Header* out, std::string* outErr) {
    if (!bytes || !out || size < 24) {
        if (outErr) *outErr = "BA2 buffer too small";
        return false;
    }
    if (std::memcmp(bytes, "BTDX", 4) != 0) {
        if (outErr) *outErr = "invalid BA2 magic (expected BTDX)";
        return false;
    }
    Ba2Header h{};
    size_t off = 4;
    if (!ReadVal(bytes, size, off, &h.version)) return false;
    std::memcpy(h.type, bytes + off, 4);
    h.type[4] = '\0';
    off += 4;
    if (!ReadVal(bytes, size, off, &h.fileCount) || !ReadVal(bytes, size, off, &h.nameTableOffset)) {
        if (outErr) *outErr = "truncated BA2 header";
        return false;
    }
    const bool knownType = (std::memcmp(h.type, "GNRL", 4) == 0 || std::memcmp(h.type, "DX10", 4) == 0);
    if (!knownType || (h.version != 1u && h.version != 7u && h.version != 8u)) {
        if (outErr) *outErr = "unsupported BA2 version or archive type";
        return false;
    }
    *out = h;
    return true;
}

namespace {

std::string NormalizeVirtualPath(std::string s) {
    for (char& c : s) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    }
    return s;
}

std::string ReadZSpan(const uint8_t* p, size_t len) {
    while (len > 0 && p[len - 1] == 0) --len;
    return std::string(reinterpret_cast<const char*>(p), len);
}

void IndexOneRecord(const char tag[4], uint32_t formId, const uint8_t* payload, size_t sz, WorldPluginIndex& out) {
    const bool isLight = (std::memcmp(tag, "LIGH", 4) == 0);
    const bool isStat  = (std::memcmp(tag, "STAT", 4) == 0);
    const bool isCell  = (std::memcmp(tag, "CELL", 4) == 0);
    const bool isWthr  = (std::memcmp(tag, "WTHR", 4) == 0);
    if (!isLight && !isStat && !isCell && !isWthr) return;

    std::string edid, modl;
    CeLightDef ldef{}; ldef.formId = formId;
    CeCellDef  cdef{}; cdef.formId = formId;
    bool gotLightData = false;

    size_t off = 0;
    uint32_t extSize = 0;
    while (off + 6 <= sz) {
        const char* st = reinterpret_cast<const char*>(payload + off);
        uint16_t sl = 0;
        std::memcpy(&sl, payload + off + 4, 2);
        off += 6;
        uint32_t actual = sl;
        if (std::memcmp(st, "XXXX", 4) == 0 && sl == 4 && off + 4 <= sz) {
            std::memcpy(&extSize, payload + off, 4);
            off += 4;
            continue;
        }
        if (sl == 0 && extSize > 0) { actual = extSize; extSize = 0; }
        if (off + actual > sz) break;
        const uint8_t* sp = payload + off;
        off += actual;

        if (std::memcmp(st, "EDID", 4) == 0 && edid.empty()) {
            edid = ReadZSpan(sp, actual);
        } else if (isStat && std::memcmp(st, "MODL", 4) == 0 && modl.empty()) {
            modl = NormalizeVirtualPath(ReadZSpan(sp, actual));
        } else if (isLight && std::memcmp(st, "DATA", 4) == 0 && actual >= 20) {
            std::memcpy(&ldef.radius, sp + 4, 4);
            ldef.color[0] = sp[8]; ldef.color[1] = sp[9]; ldef.color[2] = sp[10];
            std::memcpy(&ldef.flags, sp + 12, 4);
            gotLightData = true;
        } else if (isLight && std::memcmp(st, "FNAM", 4) == 0 && actual >= 4) {
            std::memcpy(&ldef.fade, sp, 4);
        } else if (isCell && std::memcmp(st, "DATA", 4) == 0 && actual >= 1) {
            const uint8_t cf = sp[0];
            cdef.isInterior = (cf & 0x01u) != 0;
            cdef.hasWater   = (cf & 0x02u) != 0;
        } else if (isCell && std::memcmp(st, "XCLW", 4) == 0 && actual >= 4) {
            std::memcpy(&cdef.waterHeight, sp, 4);
        }
    }

    if (isLight && gotLightData) {
        ldef.edid = std::move(edid);
        out.lights.push_back(std::move(ldef));
    } else if (isStat && !modl.empty()) {
        out.statics.push_back({formId, std::move(edid), std::move(modl)});
    } else if (isCell) {
        cdef.edid = std::move(edid);
        out.cells.push_back(std::move(cdef));
    } else if (isWthr && !edid.empty()) {
        out.weathers.push_back(std::move(edid));
    }
}

bool IndexPluginStream(const uint8_t* bytes, size_t endOff, size_t& off, WorldPluginIndex& out, int depth) {
    if (depth > 10) return false;
    while (off + 24 <= endOff) {
        char tag[5]{};
        std::memcpy(tag, bytes + off, 4);
        uint32_t sz = 0;
        std::memcpy(&sz, bytes + off + 4, 4);
        if (std::memcmp(tag, "GRUP", 4) == 0) {
            if (sz < 24 || off + sz > endOff) return false;
            const size_t gEnd = off + sz;
            off += 24;
            if (!IndexPluginStream(bytes, gEnd, off, out, depth + 1)) return false;
            off = gEnd;
            continue;
        }
        uint32_t flags = 0, formId = 0;
        std::memcpy(&flags, bytes + off + 8, 4);
        std::memcpy(&formId, bytes + off + 12, 4);
        off += 24;
        if (off + sz > endOff) return false;
        ++out.totalRecords;
        ++out.recordCounts[std::string(tag, 4)];
        if ((flags & 0x00040000u) != 0) {
            ++out.compressedRecords;
        } else {
            IndexOneRecord(tag, formId, bytes + off, sz, out);
        }
        off += sz;
    }
    return true;
}

bool DecompressLz4Block(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dst, size_t maxOut) {
    size_t sp = 0;
    while (sp < srcSize) {
        const uint8_t token = src[sp++];
        size_t litLen = token >> 4;
        if (litLen == 15u) {
            uint8_t b = 255u;
            while (sp < srcSize && b == 255u) { b = src[sp++]; litLen += b; }
        }
        if (sp + litLen > srcSize || dst.size() + litLen > maxOut) return false;
        dst.insert(dst.end(), src + sp, src + sp + litLen);
        sp += litLen;
        if (sp >= srcSize) break;
        if (sp + 2u > srcSize) return false;
        const uint16_t matchOff = uint16_t(src[sp]) | (uint16_t(src[sp + 1]) << 8);
        sp += 2u;
        if (matchOff == 0 || matchOff > dst.size()) return false;
        size_t matchLen = (token & 0x0Fu) + 4u;
        if ((token & 0x0Fu) == 15u) {
            uint8_t b = 255u;
            while (sp < srcSize && b == 255u) { b = src[sp++]; matchLen += b; }
        }
        if (dst.size() + matchLen > maxOut) return false;
        const size_t base = dst.size() - matchOff;
        dst.reserve(dst.size() + matchLen);
        for (size_t i = 0; i < matchLen; ++i) dst.push_back(dst[base + i]);
    }
    return true;
}

}  // namespace

bool DecompressLz4Frame(const uint8_t* src, size_t srcSize, std::vector<uint8_t>* out, std::string* outErr) {
    if (!src || !out || srcSize < 7) { if (outErr) *outErr = "LZ4 frame too small"; return false; }
    uint32_t magic = 0;
    std::memcpy(&magic, src, 4);
    if (magic != 0x184D2204u) { if (outErr) *outErr = "invalid LZ4 frame magic"; return false; }
    const uint8_t flg = src[4];
    if ((flg >> 6) != 1u) { if (outErr) *outErr = "unsupported LZ4 frame version"; return false; }
    const bool blockChecksum   = (flg & 0x10u) != 0;
    const bool hasContentSize  = (flg & 0x08u) != 0;
    const bool hasDictId       = (flg & 0x01u) != 0;
    size_t pos = 6;
    uint64_t expectedSize = 64ull << 20;
    if (hasContentSize) {
        if (pos + 8 > srcSize) { if (outErr) *outErr = "truncated LZ4 content size"; return false; }
        std::memcpy(&expectedSize, src + pos, 8);
        pos += 8;
        if (expectedSize > (256ull << 20)) { if (outErr) *outErr = "LZ4 content size exceeds 256MB bound"; return false; }
    }
    if (hasDictId) {
        if (pos + 4 > srcSize) { if (outErr) *outErr = "truncated LZ4 dict id"; return false; }
        pos += 4;
    }
    if (pos + 1 > srcSize) { if (outErr) *outErr = "truncated LZ4 header checksum"; return false; }
    ++pos;  // header checksum byte

    out->clear();
    if (hasContentSize) out->reserve(static_cast<size_t>(expectedSize));
    const size_t maxOut = static_cast<size_t>(hasContentSize ? expectedSize : (64ull << 20));

    while (pos + 4 <= srcSize) {
        uint32_t blk = 0;
        std::memcpy(&blk, src + pos, 4);
        pos += 4;
        if (blk == 0u) return true;  // EndMark
        const bool uncomp = (blk & 0x80000000u) != 0;
        const uint32_t blkSz = blk & 0x7FFFFFFFu;
        if (pos + blkSz > srcSize) { if (outErr) *outErr = "truncated LZ4 block"; return false; }
        if (uncomp) {
            if (out->size() + blkSz > maxOut) { if (outErr) *outErr = "LZ4 uncompressed block exceeds bound"; return false; }
            out->insert(out->end(), src + pos, src + pos + blkSz);
        } else {
            if (!DecompressLz4Block(src + pos, blkSz, *out, maxOut)) {
                if (outErr) *outErr = "LZ4 block decode failed";
                return false;
            }
        }
        pos += blkSz;
        if (blockChecksum) {
            if (pos + 4 > srcSize) return false;
            pos += 4;
        }
    }
    if (outErr) *outErr = "missing LZ4 EndMark";
    return false;
}

bool ExtractBsaFile(const std::string& archivePath, const BsaArchiveIndex& index, const std::string& virtualPath,
                    std::vector<uint8_t>* outBytes, std::string* outErr) {
    if (!outBytes) return false;
    const std::string key = NormalizeVirtualPath(virtualPath);
    const auto it = index.byPath.find(key);
    if (it == index.byPath.end()) { if (outErr) *outErr = "file not found in BSA index: " + key; return false; }
    const ArchiveFileEntry& e = index.files[it->second];

    std::ifstream f(archivePath, std::ios::binary);
    if (!f) { if (outErr) *outErr = "failed to open BSA: " + archivePath; return false; }
    f.seekg(static_cast<std::streamoff>(e.offset));
    std::vector<uint8_t> raw(e.size);
    if (!f.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(e.size))) {
        if (outErr) *outErr = "truncated BSA file payload";
        return false;
    }

    size_t pos = 0;
    // BSA v104/v105 embedded filename prefix (archive flag 0x100):
    if (index.header.version >= 104u && (index.header.archiveFlags & 0x100u) != 0) {
        if (pos >= raw.size()) return false;
        const uint8_t nameLen = raw[pos++];
        if (pos + nameLen > raw.size()) return false;
        pos += nameLen;
    }
    if (!e.compressed) {
        outBytes->assign(raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.end());
        return true;
    }
    if (pos + 4 > raw.size()) { if (outErr) *outErr = "missing compressed payload size"; return false; }
    pos += 4;  // uncompressedSize uint32
    if (index.header.version >= 105u) {
        return DecompressLz4Frame(raw.data() + pos, raw.size() - pos, outBytes, outErr);
    }
    if (outErr) *outErr = "BSA v104 zlib decompression requires zlib; v105 LZ4 supported natively";
    return false;
}

bool IndexPluginWorldFile(const std::string& path, WorldPluginIndex* out, std::string* outErr) {
    if (!out) return false;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { if (outErr) *outErr = "failed to open plugin: " + path; return false; }
    const std::streamsize fsz = f.tellg();
    if (fsz < 24) { if (outErr) *outErr = "plugin too small"; return false; }
    std::vector<uint8_t> bytes(static_cast<size_t>(fsz));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(bytes.data()), fsz)) {
        if (outErr) *outErr = "failed to read plugin bytes";
        return false;
    }
    if (std::memcmp(bytes.data(), "TES4", 4) != 0) {
        if (outErr) *outErr = "missing TES4 magic";
        return false;
    }
    *out = {};
    uint32_t hdrSz = 0;
    std::memcpy(&hdrSz, bytes.data() + 4, 4);
    std::memcpy(&out->formVersion, bytes.data() + 20, 2);
    if (24ull + hdrSz > bytes.size()) { if (outErr) *outErr = "truncated TES4 header"; return false; }
    PluginRecord tes4{};
    if (ParseSubrecords(bytes.data() + 24, hdrSz, tes4)) {
        for (const auto& s : tes4.subrecords) {
            if (std::memcmp(s.type, "HEDR", 4) == 0 && s.data.size() >= 4) std::memcpy(&out->headerVersion, s.data.data(), 4);
            else if (std::memcmp(s.type, "MAST", 4) == 0) out->masters.push_back(ReadZString(s.data));
        }
    }
    size_t off = 24u + hdrSz;
    if (!IndexPluginStream(bytes.data(), bytes.size(), off, *out, 0)) {
        if (outErr) *outErr = "error indexing GRUP/record stream";
        return false;
    }
    return true;
}

bool IndexBsaArchiveFile(const std::string& path, BsaArchiveIndex* out, std::string* outErr) {
    if (!out) return false;
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (outErr) *outErr = "failed to open BSA: " + path; return false; }
    uint8_t hdrBytes[36]{};
    if (!f.read(reinterpret_cast<char*>(hdrBytes), 36)) { if (outErr) *outErr = "truncated BSA header"; return false; }
    *out = {};
    if (!ParseBsaHeaderBytes(hdrBytes, 36, &out->header, outErr)) return false;
    const auto& h = out->header;
    const bool hasDirNames  = (h.archiveFlags & 0x1u) != 0;
    const bool hasFileNames = (h.archiveFlags & 0x2u) != 0;
    const bool defaultComp  = (h.archiveFlags & 0x4u) != 0;

    // Folder records: 16 bytes in v103/v104, 24 bytes in v105 (Skyrim SE/AE).
    const size_t folderRecBytes = (h.version >= 105u) ? 24u : 16u;
    const size_t dirTableBytes  = size_t(h.folderCount) * folderRecBytes;
    const size_t fileBlockBytes = (hasDirNames ? (size_t(h.folderCount) + size_t(h.totalFolderNameLength)) : 0u)
                                + size_t(h.fileCount) * 16u;
    const size_t metaBytes = dirTableBytes + fileBlockBytes + (hasFileNames ? size_t(h.totalFileNameLength) : 0u);
    if (metaBytes > (512ull << 20)) { if (outErr) *outErr = "BSA metadata exceeds safety bound"; return false; }

    f.seekg(static_cast<std::streamoff>(h.folderOffset));
    std::vector<uint8_t> meta(metaBytes);
    if (!f.read(reinterpret_cast<char*>(meta.data()), static_cast<std::streamsize>(metaBytes))) {
        if (outErr) *outErr = "truncated BSA directory tables";
        return false;
    }

    std::vector<uint32_t> folderCounts(h.folderCount);
    for (uint32_t i = 0; i < h.folderCount; ++i) {
        std::memcpy(&folderCounts[i], meta.data() + size_t(i) * folderRecBytes + 8u, 4);
    }

    size_t fOff = dirTableBytes;
    size_t nameOff = dirTableBytes + fileBlockBytes;
    out->files.reserve(h.fileCount);
    out->byPath.reserve(h.fileCount);

    for (uint32_t fi = 0; fi < h.folderCount; ++fi) {
        std::string dirName;
        if (hasDirNames) {
            if (fOff >= dirTableBytes + fileBlockBytes) { if (outErr) *outErr = "BSA folder name overrun"; return false; }
            const uint8_t dLen = meta[fOff++];
            if (fOff + dLen > dirTableBytes + fileBlockBytes) { if (outErr) *outErr = "BSA folder name truncated"; return false; }
            dirName = ReadZSpan(meta.data() + fOff, dLen);
            fOff += dLen;
        }
        for (uint32_t k = 0; k < folderCounts[fi]; ++k) {
            if (fOff + 16u > dirTableBytes + fileBlockBytes) { if (outErr) *outErr = "BSA file record overrun"; return false; }
            uint32_t rawSize = 0, rawOffset = 0;
            std::memcpy(&rawSize,   meta.data() + fOff + 8u,  4);
            std::memcpy(&rawOffset, meta.data() + fOff + 12u, 4);
            fOff += 16u;

            std::string fileName;
            if (hasFileNames) {
                const size_t start = nameOff;
                while (nameOff < meta.size() && meta[nameOff] != 0) ++nameOff;
                fileName.assign(reinterpret_cast<const char*>(meta.data() + start), nameOff - start);
                if (nameOff < meta.size()) ++nameOff;
            }
            ArchiveFileEntry e{};
            e.virtualPath = NormalizeVirtualPath(dirName.empty() ? fileName : (dirName + "/" + fileName));
            e.offset = rawOffset;
            e.size = rawSize & 0x3FFFFFFFu;
            const bool invertComp = (rawSize & 0x40000000u) != 0;
            e.compressed = invertComp ? !defaultComp : defaultComp;
            const size_t idx = out->files.size();
            if (!e.virtualPath.empty()) out->byPath.emplace(e.virtualPath, idx);
            out->files.push_back(std::move(e));
        }
    }
    return true;
}

bool IndexBa2ArchiveFile(const std::string& path, Ba2ArchiveIndex* out, std::string* outErr) {
    if (!out) return false;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { if (outErr) *outErr = "failed to open BA2: " + path; return false; }
    const uint64_t fsz = static_cast<uint64_t>(f.tellg());
    if (fsz < 24) { if (outErr) *outErr = "BA2 too small"; return false; }
    f.seekg(0);
    uint8_t hdrBytes[24]{};
    if (!f.read(reinterpret_cast<char*>(hdrBytes), 24)) return false;
    *out = {};
    if (!ParseBa2HeaderBytes(hdrBytes, 24, &out->header, outErr)) return false;
    if (out->header.nameTableOffset > 0 && out->header.nameTableOffset < fsz) {
        const uint64_t rem = fsz - out->header.nameTableOffset;
        if (rem <= (256ull << 20)) {
            f.seekg(static_cast<std::streamoff>(out->header.nameTableOffset));
            std::vector<uint8_t> names(static_cast<size_t>(rem));
            if (f.read(reinterpret_cast<char*>(names.data()), static_cast<std::streamsize>(rem))) {
                size_t p = 0;
                out->names.reserve(out->header.fileCount);
                for (uint32_t i = 0; i < out->header.fileCount && p + 2 <= names.size(); ++i) {
                    uint16_t len = 0;
                    std::memcpy(&len, names.data() + p, 2);
                    p += 2;
                    if (p + len > names.size()) break;
                    std::string vp = NormalizeVirtualPath(std::string(reinterpret_cast<const char*>(names.data() + p), len));
                    p += len;
                    const size_t idx = out->names.size();
                    out->byPath.emplace(vp, idx);
                    out->names.push_back(std::move(vp));
                }
            }
        }
    }
    return true;
}

}  // namespace raw::ce
