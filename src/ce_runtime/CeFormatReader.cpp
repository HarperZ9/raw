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

}  // namespace raw::ce
