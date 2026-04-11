#include "ShaderCache.h"

#include <d3dcompiler.h>
#include <fstream>
#include <cstring>

#include <SKSE/SKSE.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace SB
{
    // ── Initialization ────────────────────────────────────────────────────

    void ShaderCache::Initialize(const std::filesystem::path& cacheDir)
    {
        std::lock_guard lock(m_mutex);

        m_cacheDir = cacheDir;

        // Create directory if it doesn't exist
        std::error_code ec;
        std::filesystem::create_directories(m_cacheDir, ec);
        if (ec) {
            SKSE::log::error("ShaderCache: failed to create directory '{}': {}",
                m_cacheDir.string(), ec.message());
            m_enabled = false;
            return;
        }

        m_enabled = true;
        SKSE::log::info("ShaderCache: initialized — cache dir: {}", m_cacheDir.string());
    }

    void ShaderCache::Shutdown()
    {
        std::lock_guard lock(m_mutex);
        m_memCache.clear();
        m_enabled = false;
    }

    // ── Hash ──────────────────────────────────────────────────────────────

    uint64_t ShaderCache::ComputeHash(
        LPCVOID pSrcData, SIZE_T srcDataSize,
        const D3D_SHADER_MACRO* pDefines,
        LPCSTR pEntrypoint, LPCSTR pTarget,
        UINT Flags1, UINT Flags2) const
    {
        constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
        constexpr uint64_t FNV_PRIME  = 1099511628211ULL;

        uint64_t hash = FNV_OFFSET;

        auto feedByte = [&](uint8_t b) {
            hash ^= b;
            hash *= FNV_PRIME;
        };

        auto feedBytes = [&](const void* data, size_t len) {
            auto* p = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < len; i++)
                feedByte(p[i]);
        };

        // Hash the full source text
        feedBytes(pSrcData, srcDataSize);

        // Hash defines (Name=Definition pairs)
        if (pDefines) {
            for (auto* d = pDefines; d->Name; d++) {
                feedBytes(d->Name, std::strlen(d->Name));
                feedByte(0);
                if (d->Definition)
                    feedBytes(d->Definition, std::strlen(d->Definition));
                feedByte(0);
            }
        }
        feedByte(0xFF); // separator

        if (pEntrypoint)
            feedBytes(pEntrypoint, std::strlen(pEntrypoint));
        feedByte(0);

        if (pTarget)
            feedBytes(pTarget, std::strlen(pTarget));
        feedByte(0);

        feedBytes(&Flags1, sizeof(Flags1));
        feedBytes(&Flags2, sizeof(Flags2));

        return hash;
    }

    std::filesystem::path ShaderCache::HashToPath(uint64_t hash) const
    {
        char name[32];
        std::snprintf(name, sizeof(name), "%016llX.dxbc",
            static_cast<unsigned long long>(hash));
        return m_cacheDir / name;
    }

    // ── Cache lookup ──────────────────────────────────────────────────────

    bool ShaderCache::TryGetCached(
        LPCVOID pSrcData, SIZE_T srcDataSize,
        const D3D_SHADER_MACRO* pDefines,
        LPCSTR pEntrypoint, LPCSTR pTarget,
        UINT Flags1, UINT Flags2,
        ID3DBlob** ppCode)
    {
        if (!m_enabled || !ppCode)
            return false;

        uint64_t hash = ComputeHash(pSrcData, srcDataSize, pDefines,
            pEntrypoint, pTarget, Flags1, Flags2);

        std::lock_guard lock(m_mutex);

        // Check memory cache first
        auto memIt = m_memCache.find(hash);
        if (memIt != m_memCache.end()) {
            const auto& data = memIt->second;
            ID3DBlob* blob = nullptr;
            HRESULT hr = D3DCreateBlob(data.size(), &blob);
            if (SUCCEEDED(hr) && blob) {
                std::memcpy(blob->GetBufferPointer(), data.data(), data.size());
                *ppCode = blob;
                ++m_hits;
                return true;
            }
        }

        // Check disk
        auto path = HashToPath(hash);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
        {
            ++m_misses;
            return false;
        }

        // Read from disk
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            ++m_misses;
            return false;
        }

        auto fileSize = static_cast<size_t>(file.tellg());
        if (fileSize == 0) {
            ++m_misses;
            return false;
        }

        file.seekg(0);
        std::vector<uint8_t> data(fileSize);
        file.read(reinterpret_cast<char*>(data.data()), fileSize);
        file.close();

        // Create blob
        ID3DBlob* blob = nullptr;
        HRESULT hr = D3DCreateBlob(fileSize, &blob);
        if (FAILED(hr) || !blob) {
            ++m_misses;
            return false;
        }

        std::memcpy(blob->GetBufferPointer(), data.data(), fileSize);
        *ppCode = blob;

        // Store in memory cache
        m_memCache[hash] = std::move(data);

        ++m_hits;

        if (m_hits <= 5 || (m_hits % 50 == 0)) {
            SKSE::log::info("ShaderCache: hit #{} — {} (entry={}, target={})",
                m_hits, path.filename().string(),
                pEntrypoint ? pEntrypoint : "?",
                pTarget ? pTarget : "?");
        }

        return true;
    }

    // ── Cache store ───────────────────────────────────────────────────────

    void ShaderCache::StoreCached(
        LPCVOID pSrcData, SIZE_T srcDataSize,
        const D3D_SHADER_MACRO* pDefines,
        LPCSTR pEntrypoint, LPCSTR pTarget,
        UINT Flags1, UINT Flags2,
        ID3DBlob* pCode)
    {
        if (!m_enabled || !pCode)
            return;

        uint64_t hash = ComputeHash(pSrcData, srcDataSize, pDefines,
            pEntrypoint, pTarget, Flags1, Flags2);

        auto* blobData = static_cast<const uint8_t*>(pCode->GetBufferPointer());
        auto blobSize = pCode->GetBufferSize();

        std::lock_guard lock(m_mutex);

        // Store in memory
        m_memCache[hash].assign(blobData, blobData + blobSize);

        // Write to disk
        auto path = HashToPath(hash);
        std::ofstream file(path, std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(blobData), blobSize);
            ++m_stores;

            if (m_stores <= 5 || (m_stores % 50 == 0)) {
                SKSE::log::info("ShaderCache: stored #{} — {} ({} bytes, entry={}, target={})",
                    m_stores, path.filename().string(), blobSize,
                    pEntrypoint ? pEntrypoint : "?",
                    pTarget ? pTarget : "?");
            }
        }
    }

    // ── Cache clear ───────────────────────────────────────────────────────

    void ShaderCache::ClearCache()
    {
        std::lock_guard lock(m_mutex);
        m_memCache.clear();

        std::error_code ec;
        for (auto& entry : std::filesystem::directory_iterator(m_cacheDir, ec)) {
            if (entry.path().extension() == ".dxbc")
                std::filesystem::remove(entry.path(), ec);
        }

        m_hits = 0;
        m_misses = 0;
        m_stores = 0;

        SKSE::log::info("ShaderCache: cache cleared");
    }

} // namespace SB
