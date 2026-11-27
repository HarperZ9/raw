// RawPsoCache.cpp: Wave 83 Deterministic SHA-256 Keyed Async PSO Pre-Warm & Binary Disk Cache
// Clean-room MIT, zero external dependencies, MSVC /W4 /WX clean.
#include "RawPsoCache.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <thread>

namespace raw::bridge::wave83
{
namespace
{
inline uint32_t Rotr32(uint32_t x, uint32_t n) noexcept
{
    return (x >> n) | (x << (32u - n));
}

constexpr uint32_t kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

struct Sha256Ctx
{
    uint32_t state[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    uint8_t  buf[64] = {};
    uint32_t bufLen  = 0u;
    uint64_t totalBits = 0ull;

    void Transform(const uint8_t block[64]) noexcept
    {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4]) << 24)
                 | (static_cast<uint32_t>(block[i * 4 + 1]) << 16)
                 | (static_cast<uint32_t>(block[i * 4 + 2]) << 8)
                 | static_cast<uint32_t>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = Rotr32(w[i - 15], 7) ^ Rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = Rotr32(w[i - 2], 17) ^ Rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = Rotr32(e, 6) ^ Rotr32(e, 11) ^ Rotr32(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t t1 = h + S1 + ch + kSha256K[i] + w[i];
            const uint32_t S0 = Rotr32(a, 2) ^ Rotr32(a, 13) ^ Rotr32(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

    void Update(const uint8_t* data, size_t len) noexcept
    {
        for (size_t i = 0; i < len; ++i) {
            buf[bufLen++] = data[i];
            if (bufLen == 64u) {
                Transform(buf);
                totalBits += 512ull;
                bufLen = 0u;
            }
        }
    }

    PsoDigest Final() noexcept
    {
        totalBits += static_cast<uint64_t>(bufLen) * 8ull;
        buf[bufLen++] = 0x80u;
        if (bufLen > 56u) {
            while (bufLen < 64u) { buf[bufLen++] = 0u; }
            Transform(buf);
            bufLen = 0u;
        }
        while (bufLen < 56u) { buf[bufLen++] = 0u; }
        for (int i = 7; i >= 0; --i) {
            buf[bufLen++] = static_cast<uint8_t>((totalBits >> (i * 8)) & 0xFFu);
        }
        Transform(buf);
        PsoDigest out{};
        for (int i = 0; i < 8; ++i) {
            out.bytes[static_cast<size_t>(i * 4 + 0)] = static_cast<uint8_t>((state[i] >> 24) & 0xFFu);
            out.bytes[static_cast<size_t>(i * 4 + 1)] = static_cast<uint8_t>((state[i] >> 16) & 0xFFu);
            out.bytes[static_cast<size_t>(i * 4 + 2)] = static_cast<uint8_t>((state[i] >> 8) & 0xFFu);
            out.bytes[static_cast<size_t>(i * 4 + 3)] = static_cast<uint8_t>(state[i] & 0xFFu);
        }
        return out;
    }
};
} // namespace

PsoDigest ComputeSha256(const uint8_t* data, size_t len) noexcept
{
    Sha256Ctx ctx;
    if (data && len > 0u) { ctx.Update(data, len); }
    return ctx.Final();
}

std::string DigestToHex(const PsoDigest& d)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    s.resize(64);
    for (size_t i = 0; i < 32u; ++i) {
        s[i * 2]     = kHex[(d.bytes[i] >> 4) & 0xFu];
        s[i * 2 + 1] = kHex[d.bytes[i] & 0xFu];
    }
    return s;
}

RawPsoCache::RawPsoCache(std::filesystem::path cacheDir, uint32_t driverTag)
    : m_cacheDir(std::move(cacheDir)), m_driverTag(driverTag)
{
    if (!m_cacheDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(m_cacheDir, ec);
    }
}

PsoDigest RawPsoCache::ComputePsoKey(
    const uint8_t* shaderBytes, size_t shaderLen,
    const uint8_t* stateBytes, size_t stateLen,
    uint32_t driverTag) noexcept
{
    Sha256Ctx ctx;
    const uint8_t tag[4] = {
        static_cast<uint8_t>(driverTag & 0xFFu),
        static_cast<uint8_t>((driverTag >> 8) & 0xFFu),
        static_cast<uint8_t>((driverTag >> 16) & 0xFFu),
        static_cast<uint8_t>((driverTag >> 24) & 0xFFu)
    };
    ctx.Update(tag, 4u);
    if (shaderBytes && shaderLen > 0u) { ctx.Update(shaderBytes, shaderLen); }
    if (stateBytes && stateLen > 0u) { ctx.Update(stateBytes, stateLen); }
    return ctx.Final();
}

std::filesystem::path RawPsoCache::BlobPathForKey(const PsoDigest& key) const
{
    return m_cacheDir / (DigestToHex(key) + ".rpso");
}

bool RawPsoCache::StoreBlob(const PsoDigest& key, const std::vector<uint8_t>& blob)
{
    if (blob.empty()) { return false; }
    const std::string hex = DigestToHex(key);
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_memCache[hex] = blob;
    }
    if (m_cacheDir.empty()) { return true; }

    RawPsoBlobHeader hdr{};
    hdr.magic     = kPsoCacheMagic;
    hdr.version   = kPsoCacheVersion;
    hdr.blobBytes = static_cast<uint32_t>(blob.size());
    hdr.driverTag = m_driverTag;
    std::memcpy(hdr.keySha256, key.bytes.data(), 32u);
    const PsoDigest blobHash = ComputeSha256(blob.data(), blob.size());
    std::memcpy(hdr.blobSha256, blobHash.bytes.data(), 32u);

    const auto finalPath = BlobPathForKey(key);
    const auto tmpPath   = finalPath.string() + ".tmp";
    {
        std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
        if (!f) { return false; }
        f.write(reinterpret_cast<const char*>(&hdr), static_cast<std::streamsize>(sizeof(hdr)));
        f.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
        if (!f.good()) { return false; }
    }
    std::error_code ec;
    std::filesystem::rename(tmpPath, finalPath, ec);
    return !ec;
}

bool RawPsoCache::LoadBlob(const PsoDigest& key, std::vector<uint8_t>& outBlob)
{
    const std::string hex = DigestToHex(key);
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        const auto it = m_memCache.find(hex);
        if (it != m_memCache.end()) {
            outBlob = it->second;
            m_hits.fetch_add(1ull, std::memory_order_relaxed);
            return true;
        }
    }
    if (m_cacheDir.empty()) {
        m_misses.fetch_add(1ull, std::memory_order_relaxed);
        return false;
    }
    const auto path = BlobPathForKey(key);
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        m_misses.fetch_add(1ull, std::memory_order_relaxed);
        return false;
    }
    const auto totalSize = f.tellg();
    if (totalSize < static_cast<std::streamoff>(sizeof(RawPsoBlobHeader))) {
        f.close();
        std::error_code ec;
        std::filesystem::remove(path, ec);
        m_corruptRejected.fetch_add(1ull, std::memory_order_relaxed);
        return false;
    }
    f.seekg(0, std::ios::beg);
    RawPsoBlobHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), static_cast<std::streamsize>(sizeof(hdr)));
    const bool hdrOk = (hdr.magic == kPsoCacheMagic)
                    && (hdr.version == kPsoCacheVersion)
                    && (hdr.driverTag == m_driverTag)
                    && (hdr.blobBytes > 0u)
                    && (static_cast<uint64_t>(totalSize) == sizeof(RawPsoBlobHeader) + hdr.blobBytes)
                    && (std::memcmp(hdr.keySha256, key.bytes.data(), 32u) == 0);
    if (!hdrOk) {
        f.close();
        std::error_code ec;
        std::filesystem::remove(path, ec);
        m_corruptRejected.fetch_add(1ull, std::memory_order_relaxed);
        return false;
    }
    std::vector<uint8_t> payload(hdr.blobBytes);
    f.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(hdr.blobBytes));
    const PsoDigest actual = ComputeSha256(payload.data(), payload.size());
    if (std::memcmp(actual.bytes.data(), hdr.blobSha256, 32u) != 0) {
        f.close();
        std::error_code ec;
        std::filesystem::remove(path, ec);
        m_corruptRejected.fetch_add(1ull, std::memory_order_relaxed);
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_memCache[hex] = payload;
    }
    outBlob = std::move(payload);
    m_hits.fetch_add(1ull, std::memory_order_relaxed);
    return true;
}

std::vector<uint8_t> RawPsoCache::LookupOrBuild(
    const PsoDigest& key,
    const std::function<std::vector<uint8_t>()>& builder,
    bool* outWasCacheHit)
{
    std::vector<uint8_t> blob;
    if (LoadBlob(key, blob)) {
        if (outWasCacheHit) { *outWasCacheHit = true; }
        return blob;
    }
    if (outWasCacheHit) { *outWasCacheHit = false; }
    blob = builder ? builder() : std::vector<uint8_t>{};
    if (!blob.empty()) {
        StoreBlob(key, blob);
    }
    return blob;
}

uint32_t RawPsoCache::PrewarmBatchAsync(const std::vector<PsoPrewarmItem>& items, uint32_t workerCount)
{
    if (items.empty()) { return 0u; }
    const uint32_t nThreads = (std::clamp)(workerCount, 1u, static_cast<uint32_t>((std::min<size_t>)(items.size(), 6u)));
    std::atomic<size_t> nextIdx{0u};
    std::atomic<uint32_t> builtCount{0u};
    std::vector<std::thread> workers;
    workers.reserve(nThreads);
    for (uint32_t t = 0u; t < nThreads; ++t) {
        workers.emplace_back([&]() {
            while (true) {
                const size_t i = nextIdx.fetch_add(1u, std::memory_order_relaxed);
                if (i >= items.size()) { break; }
                bool hit = false;
                auto b = LookupOrBuild(items[i].key, items[i].builder, &hit);
                if (!b.empty()) {
                    builtCount.fetch_add(1u, std::memory_order_relaxed);
                    m_prewarmed.fetch_add(1ull, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& w : workers) {
        if (w.joinable()) { w.join(); }
    }
    return builtCount.load(std::memory_order_acquire);
}

PsoCacheStats RawPsoCache::GetStats() const noexcept
{
    return {
        m_hits.load(std::memory_order_relaxed),
        m_misses.load(std::memory_order_relaxed),
        m_corruptRejected.load(std::memory_order_relaxed),
        m_prewarmed.load(std::memory_order_relaxed)
    };
}

} // namespace raw::bridge::wave83
