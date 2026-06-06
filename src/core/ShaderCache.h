#pragma once
//=============================================================================
//  ShaderCache — Disk + memory cache for compiled shader bytecode
//
//  Integrates into SB_ShaderDebug's D3DCompile IAT hook. Hashes the full
//  preprocessed source + defines + entry point + target + flags to produce
//  a deterministic key. Cache files live on disk as raw DXBC blobs.
//=============================================================================

#include <d3dcompiler.h>
#include <filesystem>
#include <string>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace SB
{
    class ShaderCache
    {
    public:
        static ShaderCache& Get()
        {
            static ShaderCache instance;
            return instance;
        }

        /// Initialize the cache directory (creates it if needed).
        /// Call from main.cpp kDataLoaded, BEFORE ShaderDebug::Install().
        void Initialize(const std::filesystem::path& cacheDir);
        void Shutdown();

        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool a_enabled) { m_enabled = a_enabled; }

        /// Try to find a cached blob for the given compile inputs.
        /// Returns true if found; ppCode is filled with an ID3DBlob.
        bool TryGetCached(
            LPCVOID pSrcData, SIZE_T srcDataSize,
            const D3D_SHADER_MACRO* pDefines,
            LPCSTR pEntrypoint, LPCSTR pTarget,
            UINT Flags1, UINT Flags2,
            ID3DBlob** ppCode);

        /// Store a compiled blob in the cache.
        void StoreCached(
            LPCVOID pSrcData, SIZE_T srcDataSize,
            const D3D_SHADER_MACRO* pDefines,
            LPCSTR pEntrypoint, LPCSTR pTarget,
            UINT Flags1, UINT Flags2,
            ID3DBlob* pCode);

        // Statistics
        uint32_t GetHitCount()   const { return m_hits; }
        uint32_t GetMissCount()  const { return m_misses; }
        uint32_t GetStoreCount() const { return m_stores; }

        /// Clear the entire on-disk cache
        void ClearCache();

    private:
        ShaderCache() = default;

        /// Compute FNV-1a 64-bit hash of all compile inputs
        uint64_t ComputeHash(
            LPCVOID pSrcData, SIZE_T srcDataSize,
            const D3D_SHADER_MACRO* pDefines,
            LPCSTR pEntrypoint, LPCSTR pTarget,
            UINT Flags1, UINT Flags2) const;

        /// Convert hash to cache file path
        std::filesystem::path HashToPath(uint64_t hash) const;

        std::filesystem::path m_cacheDir;
        bool m_enabled = false;
        mutable std::mutex m_mutex;

        // In-memory cache: hash → blob bytes (avoids repeated disk reads)
        std::unordered_map<uint64_t, std::vector<uint8_t>> m_memCache;

        uint32_t m_hits   = 0;
        uint32_t m_misses = 0;
        uint32_t m_stores = 0;
    };

} // namespace SB
