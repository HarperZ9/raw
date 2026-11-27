#pragma once
// RawPsoCache.h: Wave 83 Deterministic SHA-256 Keyed Async PSO Pre-Warm & Binary Disk Cache
// Eliminates shader-compilation stutter via background pre-warm workers and atomic SHA-256
// verified binary blob caching. Corrupt, truncated, or driver/version-mismatched PSO blobs
// are rejected fail-closed and cleanly rebuilt.
// Clean-room MIT, zero external dependencies, MSVC /W4 /WX clean.
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace raw::bridge::wave83
{

inline constexpr uint32_t kPsoCacheMagic   = 0x4F535052u; // 'RPSO'
inline constexpr uint32_t kPsoCacheVersion = 1u;

struct PsoDigest
{
    std::array<uint8_t, 32> bytes{};
    bool operator==(const PsoDigest& o) const noexcept { return bytes == o.bytes; }
};

// Exact 80-byte binary on-disk PSO header (magic + version + key digest + blob digest + size + driverTag).
struct RawPsoBlobHeader
{
    uint32_t magic          = kPsoCacheMagic;
    uint32_t version        = kPsoCacheVersion;
    uint32_t blobBytes      = 0u;
    uint32_t driverTag      = 0u;
    uint8_t  keySha256[32]  = {};
    uint8_t  blobSha256[32] = {};
};
static_assert(sizeof(RawPsoBlobHeader) == 80u, "RawPsoBlobHeader must be 80 bytes");

struct PsoCacheStats
{
    uint64_t hits            = 0ull;
    uint64_t misses          = 0ull;
    uint64_t corruptRejected = 0ull;
    uint64_t prewarmed       = 0ull;
};

struct PsoPrewarmItem
{
    PsoDigest                                    key;
    uint32_t                                     driverTag = 0u;
    std::function<std::vector<uint8_t>()>        builder;
};

// Standalone FIPS 180-4 SHA-256 (clean-room MIT, zero external crypto deps).
PsoDigest ComputeSha256(const uint8_t* data, size_t len) noexcept;
std::string DigestToHex(const PsoDigest& d);

class RawPsoCache
{
public:
    explicit RawPsoCache(std::filesystem::path cacheDir = {}, uint32_t driverTag = 0x00010722u);

    static PsoDigest ComputePsoKey(
        const uint8_t* shaderBytes, size_t shaderLen,
        const uint8_t* stateBytes, size_t stateLen,
        uint32_t driverTag) noexcept;

    // Stores verified blob atomically in memory and (if cacheDir non-empty) on disk.
    bool StoreBlob(const PsoDigest& key, const std::vector<uint8_t>& blob);

    // Fail-closed lookup: validates header magic, version, driverTag, keySha256, exact size,
    // and full payload SHA-256. Any corruption evicts the bad entry and returns false.
    bool LoadBlob(const PsoDigest& key, std::vector<uint8_t>& outBlob);

    // Lookup-or-build helper (0 ms hitch on hit; rebuilds cleanly on miss or corrupt eviction).
    std::vector<uint8_t> LookupOrBuild(
        const PsoDigest& key,
        const std::function<std::vector<uint8_t>()>& builder,
        bool* outWasCacheHit = nullptr);

    // Asynchronous background pre-warm across a bounded worker set (blocks cleanly at Drain or returns count).
    uint32_t PrewarmBatchAsync(const std::vector<PsoPrewarmItem>& items, uint32_t workerCount = 2u);

    std::filesystem::path BlobPathForKey(const PsoDigest& key) const;
    PsoCacheStats GetStats() const noexcept;

private:
    std::filesystem::path                               m_cacheDir;
    uint32_t                                            m_driverTag = 0u;
    mutable std::mutex                                  m_mtx;
    std::unordered_map<std::string, std::vector<uint8_t>> m_memCache;
    std::atomic<uint64_t>                               m_hits{0ull};
    std::atomic<uint64_t>                               m_misses{0ull};
    std::atomic<uint64_t>                               m_corruptRejected{0ull};
    std::atomic<uint64_t>                               m_prewarmed{0ull};
};

} // namespace raw::bridge::wave83
