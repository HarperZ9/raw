#pragma once
// RawBsaArchive.h: 100% Clean-Room Native Bethesda `.BSA` Archive Reader (`raw::worldgen`).
// Supports TES4/TES5 v103/v104 (zlib RFC 1950) AND Skyrim Special Edition / AE v105 (LZ4 Frame 0x184D2204),
// including embedded file-name flags (0x0100), per-file compression inversion (bit 30), v104 16B vs v105 24B
// padded folder records, O(1) normalized lowercase forward-slash lookup, and streaming on-demand extraction.
// Zero Bethesda SDK / third-party library / Windows header dependencies. Original work, MIT.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace raw::worldgen {

inline constexpr uint32_t kBsaMagic              = 0x00415342u; // "BSA\0"
inline constexpr uint32_t kBsaVersionOblivion    = 103u;
inline constexpr uint32_t kBsaVersionSkyrimLE    = 104u;
inline constexpr uint32_t kBsaVersionSkyrimSE    = 105u;

inline constexpr uint32_t kBsaFlagDirNames       = 0x0001u;
inline constexpr uint32_t kBsaFlagFileNames      = 0x0002u;
inline constexpr uint32_t kBsaFlagCompressed     = 0x0004u;
inline constexpr uint32_t kBsaFlagEmbedFileNames = 0x0100u;

inline constexpr uint32_t kLz4FrameMagic         = 0x184D2204u;

struct BsaArchiveHeader {
    uint32_t version               = 0u;
    uint32_t folderOffset          = 0u;
    uint32_t archiveFlags          = 0u;
    uint32_t folderCount           = 0u;
    uint32_t fileCount             = 0u;
    uint32_t totalFolderNameLength = 0u;
    uint32_t totalFileNameLength   = 0u;
    uint32_t fileFlags             = 0u;
};

struct BsaEntryInfo {
    std::string virtualPath;    // Normalized lowercase forward-slash path (e.g. "meshes/actors/...")
    uint64_t    folderHash      = 0ull;
    uint64_t    fileHash        = 0ull;
    uint64_t    dataOffset      = 0ull;
    uint32_t    storedSize      = 0u;
    bool        compressed      = false;
};

struct BsaArchiveStats {
    std::string path;
    uint32_t    version            = 0u;
    uint32_t    archiveFlags       = 0u;
    uint32_t    folderCount        = 0u;
    uint32_t    fileCount          = 0u;
    uint32_t    compressedFiles    = 0u;
    uint32_t    uncompressedFiles  = 0u;
    uint64_t    archiveFileBytes   = 0ull;
    uint64_t    totalStoredBytes   = 0ull;
    bool        embedsFileNames    = false;
};

// Clean-room zero-dependency LZ4 Frame (0x184D2204) & raw LZ4 Block decompressor.
// Supports independent & dependent blocks, uncompressed blocks, block/content checksums, and overlapping RLE matches.
bool DecompressLz4Block(const uint8_t* src, size_t srcLen, size_t maxOutBytes, std::vector<uint8_t>* out);
bool DecompressLz4Frame(const uint8_t* src, size_t srcLen, size_t expectedOutBytes,
                        std::vector<uint8_t>* out, std::string* outErr = nullptr);

// Clean-room LZ4 Frame (0x184D2204) encoder (valid LZ4 block sequence compression with real match tokens).
// Useful for round-trip testing and clean-room archive generation.
bool CompressLz4Frame(const uint8_t* src, size_t srcLen, std::vector<uint8_t>* out);

// Normalizes any Bethesda virtual asset path to lowercase forward slashes with no leading slash.
std::string NormalizeBsaVirtualPath(const std::string& rawPath);

class RawBsaArchive {
public:
    bool Open(const std::string& path, std::string* outErr = nullptr);
    bool OpenFromMemory(const uint8_t* data, size_t size, const std::string& label = "<memory>",
                        std::string* outErr = nullptr);
    void Close();

    bool IsOpen() const noexcept { return open_; }

    // Lists virtual paths matching `prefixOrSubstrFilter` (case-insensitive; empty = all files).
    // If `prefixOnly` is true, matches path prefix; otherwise substring match.
    std::vector<std::string> ListFiles(const std::string& filter = "", bool prefixOnly = false) const;

    bool HasFile(const std::string& virtualPath) const;
    const BsaEntryInfo* FindEntry(const std::string& virtualPath) const;

    // Extracts and transparently decompresses (zlib for v103/v104, LZ4 Frame for v105) a file by virtual path.
    bool ExtractFile(const std::string& virtualPath, std::vector<uint8_t>* outBytes,
                     std::string* outErr = nullptr) const;
    bool ExtractEntry(const BsaEntryInfo& entry, std::vector<uint8_t>* outBytes,
                      std::string* outErr = nullptr) const;

    const BsaArchiveHeader&           Header() const noexcept { return header_; }
    const std::vector<BsaEntryInfo>&  Entries() const noexcept { return entries_; }
    BsaArchiveStats                   GetArchiveStats() const;

private:
    bool ParseIndexFromBuffer(const uint8_t* idxBuf, size_t idxSize, uint64_t totalFileSize,
                              std::string* outErr);
    bool ReadSlice(uint64_t offset, uint32_t size, std::vector<uint8_t>* out, std::string* outErr) const;

    bool                                    open_        = false;
    bool                                    fromMemory_  = false;
    std::string                             path_;
    uint64_t                                fileBytes_   = 0ull;
    std::vector<uint8_t>                    memBuffer_;
    BsaArchiveHeader                        header_{};
    std::vector<BsaEntryInfo>               entries_;
    std::unordered_map<std::string, size_t> byPath_;
    BsaArchiveStats                         stats_{};
};

}  // namespace raw::worldgen
