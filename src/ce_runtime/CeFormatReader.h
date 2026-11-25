#pragma once
// CeFormatReader.h: Clean-Room Open Creation Engine Plugin (.esm/.esp/.esl) & Archive (.bsa/.ba2) Reader.
// Foundation layer for owning Creation Engine world/asset loading outside Bethesda's executable across
// Skyrim SE/AE (TES4 v44, BSA v105), Fallout 4 (TES4 v131, BA2 v1), Fallout 76 (BA2), and Starfield (TES4 v575, BA2 v7/v8).
// Zero proprietary code. Original work, MIT.

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace raw::ce {

struct Subrecord {
    char                 type[5]{};   // 4-char tag + null terminator (e.g. "EDID", "MODL", "DATA")
    std::vector<uint8_t> data;
};

struct PluginRecord {
    char                   type[5]{}; // 4-char tag (e.g. "TES4", "LIGH", "STAT", "CELL", "WRLD", "REFR")
    uint32_t               flags = 0;
    uint32_t               formId = 0;
    uint16_t               formVersion = 0;
    bool                   compressed = false;
    std::vector<Subrecord> subrecords;
    std::string            edid;      // decoded EDID subrecord when present
};

struct PluginSummary {
    float                     headerVersion = 0.0f;
    uint32_t                  recordCount = 0;
    uint32_t                  nextObjectId = 0;
    uint16_t                  formVersion = 0;
    std::string               author;
    std::string               description;
    std::vector<std::string>  masters;
    std::vector<PluginRecord> records;
};

struct BsaHeader {
    uint32_t version = 0;       // 103=Oblivion, 104=Skyrim LE/FO3/FNV, 105=Skyrim SE/AE
    uint32_t folderOffset = 0;
    uint32_t archiveFlags = 0;
    uint32_t folderCount = 0;
    uint32_t fileCount = 0;
    uint32_t totalFolderNameLength = 0;
    uint32_t totalFileNameLength = 0;
    uint16_t fileFlags = 0;
};

struct Ba2Header {
    uint32_t version = 0;       // 1=Fallout 4 / FO76, 7/8=Starfield
    char     type[5]{};         // "GNRL" (general files) or "DX10" (textures)
    uint32_t fileCount = 0;
    uint64_t nameTableOffset = 0;
};

struct ArchiveFileEntry {
    std::string virtualPath;    // normalized lowercase forward-slash path (e.g. "meshes/clutter/bucket.nif")
    uint64_t    offset = 0;
    uint32_t    size = 0;
    bool        compressed = false;
};

struct BsaArchiveIndex {
    BsaHeader                                header{};
    std::vector<ArchiveFileEntry>            files;
    std::unordered_map<std::string, size_t>  byPath;
};

struct Ba2ArchiveIndex {
    Ba2Header                                header{};
    std::vector<std::string>                 names;
    std::unordered_map<std::string, size_t>  byPath;
};

struct CeLightDef {
    uint32_t    formId = 0;
    std::string edid;
    uint32_t    radius = 0;
    uint8_t     color[3]{};
    uint32_t    flags = 0;
    float       fade = 1.0f;
};

struct CeStaticDef {
    uint32_t    formId = 0;
    std::string edid;
    std::string modelPath;
};

struct CeCellDef {
    uint32_t    formId = 0;
    std::string edid;
    bool        isInterior = false;
    bool        hasWater = false;
    float       waterHeight = 0.0f;
};

struct WorldPluginIndex {
    float                                    headerVersion = 0.0f;
    uint16_t                                 formVersion = 0;
    uint32_t                                 totalRecords = 0;
    uint32_t                                 compressedRecords = 0;
    std::vector<std::string>                 masters;
    std::unordered_map<std::string, uint32_t> recordCounts;
    std::vector<CeLightDef>                  lights;
    std::vector<CeStaticDef>                 statics;
    std::vector<CeCellDef>                   cells;
    std::vector<std::string>                 weathers;
};

// Parses an in-memory or file-loaded TES4 plugin (.esm/.esp/.esl), validating TES4 header, GRUP hierarchy,
// and uncompressed record subrecords. Returns false with reason on malformed/truncated data.
bool ParsePluginBytes(const uint8_t* bytes, size_t size, PluginSummary* out, std::string* outErr = nullptr);
bool ParsePluginFile(const std::string& path, PluginSummary* out, std::string* outErr = nullptr);

// High-speed streaming world indexer for production multi-hundred-MB .esm files (Skyrim.esm, Fallout4.esm, Starfield.esm).
bool IndexPluginWorldFile(const std::string& path, WorldPluginIndex* out, std::string* outErr = nullptr);

// Parses a Skyrim/Oblivion BSA archive header (magic "BSA\0") or builds the complete virtual-path directory index.
bool ParseBsaHeaderBytes(const uint8_t* bytes, size_t size, BsaHeader* out, std::string* outErr = nullptr);
bool IndexBsaArchiveFile(const std::string& path, BsaArchiveIndex* out, std::string* outErr = nullptr);

// Extracts and decompresses (zero-dependency LZ4 frame for BSA v105) a virtual file from an indexed BSA archive.
bool ExtractBsaFile(const std::string& archivePath, const BsaArchiveIndex& index, const std::string& virtualPath,
                    std::vector<uint8_t>* outBytes, std::string* outErr = nullptr);

// Zero-dependency LZ4 frame decompressor (used by Skyrim SE/AE BSA v105 and BA2 compressed chunks).
bool DecompressLz4Frame(const uint8_t* src, size_t srcSize, std::vector<uint8_t>* out, std::string* outErr = nullptr);

// Parses a Fallout 4 / Fallout 76 / Starfield BA2 archive header (magic "BTDX") or indexes its name table.
bool ParseBa2HeaderBytes(const uint8_t* bytes, size_t size, Ba2Header* out, std::string* outErr = nullptr);
bool IndexBa2ArchiveFile(const std::string& path, Ba2ArchiveIndex* out, std::string* outErr = nullptr);

}  // namespace raw::ce
