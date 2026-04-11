I have fully explored the codebase. Here is the complete implementation plan.

---

# Implementation Plan: 4 New SkyrimBridge Backend Features

## Codebase Patterns Summary

Every singleton in this project follows the same pattern:
- Meyer's singleton via `static T& Get()` returning a function-local static
- `Initialize(ID3D11Device*, ...)` for GPU resource creation
- Raw COM pointers (not ComPtr) for most resources, with manual `Release()` in `Shutdown()`
- SKSE::log for all logging
- vtable hooks via `VirtualProtect` on `ID3D11DeviceContext` (indices: 8=PSSetSRV, 9=PSSetShader, 12=DrawIndexed, 15=CreatePS, 16=PSSetCB)
- Initialization at `kDataLoaded` in `main.cpp`, after `D3D11Hook::Init()` provides device/context/swapChain
- Per-frame work in `OnENBFrame()` (callback type 1, before ENB renders)
- Post-render work in `D3D11Hook::HookedPresent()` (after ENB, before original Present)

---

## Feature 1: Shader Bytecode Caching (`ShaderCache`)

### New Files
- `C:\Users\Zain\SKSE\SkyrimBridge_v3\src\core\ShaderCache.h`
- `C:\Users\Zain\SKSE\SkyrimBridge_v3\src\core\ShaderCache.cpp`

### Design

The ShaderCache does NOT install its own IAT hooks. Instead, it integrates into the existing `SB_ShaderDebug.cpp` hooks. The `HookD3DCompile` and `HookD3DCompile2` functions currently call `s_origD3DCompile` unconditionally, then call `RecordCompilation`. The modification is: before calling the real compiler, check the cache; after a successful compile, write to the cache.

**ShaderCache class (singleton):**

```cpp
// ShaderCache.h
#pragma once
#include <d3dcompiler.h>
#include <filesystem>
#include <string>
#include <mutex>
#include <unordered_map>

namespace SB
{
    class ShaderCache
    {
    public:
        static ShaderCache& Get();

        // Set the cache directory and enable caching.
        // Call from main.cpp kDataLoaded, before ShaderDebug::Install().
        void Initialize(const std::filesystem::path& cacheDir);
        void Shutdown();

        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool a_enabled) { m_enabled = a_enabled; }

        // Try to find a cached blob for the given compile inputs.
        // Returns true if found; ppCode is filled with an ID3DBlob.
        bool TryGetCached(
            LPCVOID pSrcData, SIZE_T srcDataSize,
            const D3D_SHADER_MACRO* pDefines,
            LPCSTR pEntrypoint, LPCSTR pTarget,
            UINT Flags1, UINT Flags2,
            ID3DBlob** ppCode);

        // Store a compiled blob in the cache.
        void StoreCached(
            LPCVOID pSrcData, SIZE_T srcDataSize,
            const D3D_SHADER_MACRO* pDefines,
            LPCSTR pEntrypoint, LPCSTR pTarget,
            UINT Flags1, UINT Flags2,
            ID3DBlob* pCode);

        // Statistics
        uint32_t GetHitCount()  const { return m_hits; }
        uint32_t GetMissCount() const { return m_misses; }
        uint32_t GetStoreCount() const { return m_stores; }

        // Clear the entire on-disk cache
        void ClearCache();

    private:
        ShaderCache() = default;

        // Compute FNV-1a hash of (source + defines + entrypoint + target + flags)
        uint64_t ComputeHash(
            LPCVOID pSrcData, SIZE_T srcDataSize,
            const D3D_SHADER_MACRO* pDefines,
            LPCSTR pEntrypoint, LPCSTR pTarget,
            UINT Flags1, UINT Flags2) const;

        // Convert hash to filename: "Data/SKSE/Plugins/SkyrimBridge/ShaderCache/<hex>.dxbc"
        std::filesystem::path HashToPath(uint64_t hash) const;

        std::filesystem::path m_cacheDir;
        bool m_enabled = false;
        mutable std::mutex m_mutex;

        // In-memory LRU: hash -> blob bytes (avoid repeated disk reads within session)
        std::unordered_map<uint64_t, std::vector<uint8_t>> m_memCache;

        uint32_t m_hits   = 0;
        uint32_t m_misses = 0;
        uint32_t m_stores = 0;
    };
}
```

**Hash computation (FNV-1a 64-bit):**
```cpp
uint64_t ShaderCache::ComputeHash(
    LPCVOID pSrcData, SIZE_T srcDataSize,
    const D3D_SHADER_MACRO* pDefines,
    LPCSTR pEntrypoint, LPCSTR pTarget,
    UINT Flags1, UINT Flags2) const
{
    constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
    constexpr uint64_t FNV_PRIME  = 1099511628211ULL;

    uint64_t hash = FNV_OFFSET;
    auto feedByte = [&](uint8_t b) { hash ^= b; hash *= FNV_PRIME; };
    auto feedBytes = [&](const void* data, size_t len) {
        auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; i++) feedByte(p[i]);
    };

    // Hash the full source text (includes all #include content resolved by ENB)
    feedBytes(pSrcData, srcDataSize);

    // Hash defines (Name=Definition pairs, null-terminated)
    if (pDefines) {
        for (auto* d = pDefines; d->Name; d++) {
            feedBytes(d->Name, strlen(d->Name));
            feedByte(0);
            if (d->Definition) feedBytes(d->Definition, strlen(d->Definition));
            feedByte(0);
        }
    }
    feedByte(0xFF); // separator

    if (pEntrypoint) feedBytes(pEntrypoint, strlen(pEntrypoint));
    feedByte(0);
    if (pTarget)     feedBytes(pTarget, strlen(pTarget));
    feedByte(0);

    feedBytes(&Flags1, sizeof(Flags1));
    feedBytes(&Flags2, sizeof(Flags2));

    return hash;
}
```

**Key design decision:** The hash includes the full preprocessed source text. ENB's D3DCompile calls pass the fully preprocessed HLSL (all `#include` directives resolved by ENB's own include handler). So hashing the source bytes naturally includes all dependency file content. If any `.fxh` file changes, the preprocessed source changes, the hash changes, and the cache misses. No need for separate file modification time tracking.

**TryGetCached implementation:**
1. Compute hash
2. Lock mutex, check `m_memCache` first
3. If not in memory, check disk: `m_cacheDir / fmt::format("{:016X}.dxbc", hash)`
4. If file exists, read it, store in `m_memCache`, create an `ID3DBlob` via `D3DCreateBlob` + memcpy, return true
5. If not found, return false, increment m_misses

**StoreCached implementation:**
1. Compute hash
2. Get blob data from `pCode->GetBufferPointer()` / `GetBufferSize()`
3. Write to disk file
4. Store in `m_memCache`
5. Increment m_stores

**Creating the ID3DBlob for cache hits:** Use `D3DCreateBlob(size, &blob)` (from d3dcompiler.lib, already linked), then `memcpy` into `blob->GetBufferPointer()`.

### Modifications to Existing Files

**`C:\Users\Zain\SKSE\SkyrimBridge_v3\src\core\SB_ShaderDebug.cpp`** -- modify `HookD3DCompile` and `HookD3DCompile2`:

```cpp
HRESULT WINAPI ShaderDebug::HookD3DCompile(
    LPCVOID pSrcData, SIZE_T SrcDataSize,
    LPCSTR pSourceName, const D3D_SHADER_MACRO* pDefines,
    ID3DInclude* pInclude, LPCSTR pEntrypoint,
    LPCSTR pTarget, UINT Flags1, UINT Flags2,
    ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs)
{
    auto& self = Get();

    // ── Cache check (before real compile) ───────────────────────────
    auto& cache = SB::ShaderCache::Get();
    if (cache.IsEnabled() && ppCode) {
        if (cache.TryGetCached(pSrcData, SrcDataSize, pDefines,
                               pEntrypoint, pTarget, Flags1, Flags2, ppCode))
        {
            // Cache hit: no compilation needed. Clear error msgs.
            if (ppErrorMsgs) *ppErrorMsgs = nullptr;
            return S_OK;
        }
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // Call the real D3DCompile
    HRESULT hr = s_origD3DCompile(
        pSrcData, SrcDataSize, pSourceName, pDefines, pInclude,
        pEntrypoint, pTarget, Flags1, Flags2, ppCode, ppErrorMsgs);

    auto endTime = std::chrono::high_resolution_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(
        endTime - startTime).count();

    // Record the attempt regardless of success/failure (existing behavior preserved)
    self.RecordCompilation(hr, pSrcData, SrcDataSize,
                           pSourceName, pEntrypoint, pTarget,
                           ppErrorMsgs, elapsedMs);

    // ── Cache store (after successful compile) ──────────────────────
    if (cache.IsEnabled() && SUCCEEDED(hr) && ppCode && *ppCode) {
        cache.StoreCached(pSrcData, SrcDataSize, pDefines,
                          pEntrypoint, pTarget, Flags1, Flags2, *ppCode);
    }

    return hr;
}
```

The identical pattern applies to `HookD3DCompile2`.

**`C:\Users\Zain\SKSE\SkyrimBridge_v3\src\core\SB_ShaderDebug.h`** -- add `#include "ShaderCache.h"` at top (or forward-declare in the .cpp only).

**`C:\Users\Zain\SKSE\SkyrimBridge_v3\src\core\main.cpp`** -- add initialization before ShaderDebug:

```cpp
// Initialize shader bytecode cache (must precede ShaderDebug which hooks D3DCompile)
{
    auto cacheDir = std::filesystem::path("Data/SKSE/Plugins/SkyrimBridge/ShaderCache");
    SB::ShaderCache::Get().Initialize(cacheDir);
    SKSE::log::info("SkyrimBridge: ShaderCache active — cache dir: {}", cacheDir.string());
}
```

### Resource Slots
None -- this feature is CPU-only (filesystem + memory).

### Initialization Sequence
1. `main.cpp` kDataLoaded: `ShaderCache::Get().Initialize(cacheDir)` -- creates directory if needed
2. `SB_ShaderDebug::Install()` -- installs IAT hooks (unchanged)
3. First ENB startup triggers shader compilations -- cache misses populate the cache
4. Subsequent launches: cache hits bypass D3DCompile entirely

### Shutdown
`ShaderCache::Shutdown()` -- clears `m_memCache`, no resource release needed.

---

## Feature 2: Compute-based Luminance Histogram (`LuminanceHistogram`)

### New Files
- `C:\Users\Zain\SKSE\SkyrimBridge_v3\src\core\LuminanceHistogram.h`
- `C:\Users\Zain\SKSE\SkyrimBridge_v3\src\core\LuminanceHistogram.cpp`

### Design

**LuminanceHistogram class (singleton):**

```cpp
// LuminanceHistogram.h
#pragma once
#include "BridgeData.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ComputeShader;
struct ID3D11Buffer;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11UnorderedAccessView;
struct IDXGISwapChain;

namespace SB
{
    struct HistogramResult
    {
        float bins[256]{};          // Normalized histogram (sum = 1.0)
        float avgLuminance = 0.f;
        float minLuminance = 0.f;
        float maxLuminance = 0.f;
        float p05 = 0.f;           // 5th percentile
        float p50 = 0.f;           // Median
        float p95 = 0.f;           // 95th percentile
        float avgR = 0.f, avgG = 0.f, avgB = 0.f;
    };

    class LuminanceHistogram
    {
    public:
        static LuminanceHistogram& Get();

        bool Initialize(ID3D11Device* a_device, IDXGISwapChain* a_swapChain);
        void Shutdown();

        // Dispatch compute shader to analyze the backbuffer.
        // Call from HookedPresent BEFORE FeedbackProcessor::CollectFeedback.
        void Dispatch(ID3D11DeviceContext* a_ctx, IDXGISwapChain* a_swapChain);

        // Read back results from the staging buffer (1-frame delay).
        // Call from HookedPresent after Dispatch.
        void ReadBack(ID3D11DeviceContext* a_ctx);

        // Get latest results for CPU consumption (FeedbackProcessor).
        const HistogramResult& GetResult() const { return m_result[1 - m_writeIndex]; }

        // Get histogram SRV for shader binding (t17).
        ID3D11ShaderResourceView* GetHistogramSRV() const { return m_histogramSRV; }

        bool IsInitialized() const { return m_initialized; }
        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool a_enabled) { m_enabled = a_enabled; }

    private:
        LuminanceHistogram() = default;

        bool CompileComputeShader(ID3D11Device* a_device);
        bool CreateResources(ID3D11Device* a_device, uint32_t w, uint32_t h);
        void ReleaseResources();

        bool m_initialized = false;
        bool m_enabled = true;

        // Compute shader: bins luminance into 256-bin histogram
        ID3D11ComputeShader* m_histogramCS = nullptr;

        // Reduction compute shader: sums partial histograms + computes stats
        ID3D11ComputeShader* m_reductionCS = nullptr;

        // GPU resources
        ID3D11Buffer* m_histogramBuffer = nullptr;     // RWStructuredBuffer<uint> [256]
        ID3D11Buffer* m_statsBuffer = nullptr;          // RWStructuredBuffer<float4> [4]: avg, min, max, percentiles, color
        ID3D11Buffer* m_stagingHistogram = nullptr;     // CPU-readable copy
        ID3D11Buffer* m_stagingStats = nullptr;         // CPU-readable copy

        // SRV for shaders: 256x1 R32_FLOAT texture (from histogram buffer)
        ID3D11Texture2D* m_histogramTex = nullptr;
        ID3D11ShaderResourceView* m_histogramSRV = nullptr;
        ID3D11UnorderedAccessView* m_histogramBufUAV = nullptr;
        ID3D11UnorderedAccessView* m_statsBufUAV = nullptr;

        // Backbuffer SRV (created per-frame from backbuffer)
        ID3D11ShaderResourceView* m_backbufferSRV = nullptr;

        uint32_t m_width = 0, m_height = 0;

        // Double-buffered results
        HistogramResult m_result[2]{};
        int m_writeIndex = 0;
        bool m_hasData = false;
    };
}
```

### Embedded HLSL Compute Shader

Two passes: histogram binning, then reduction.

**Pass 1: Histogram CS** (dispatched as `ceil(width/16) x ceil(height/16) x 1` thread groups)

```hlsl
// Embedded as static constexpr const char* in LuminanceHistogram.cpp
static constexpr const char* kHistogramCS = R"(
Texture2D<float4> BackBuffer : register(t0);

RWStructuredBuffer<uint> Histogram : register(u0);     // 256 bins
RWStructuredBuffer<float4> Stats   : register(u1);     // [0]=sum(rgb,lum), [1]=min/max, [2]=count, [3]=reserved

groupshared uint gs_hist[256];

[numthreads(16, 16, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID, uint GI : SV_GroupIndex)
{
    // Clear shared memory
    if (GI < 256)
        gs_hist[GI] = 0;
    GroupMemoryBarrierWithGroupSync();

    // Load pixel
    uint2 dims;
    BackBuffer.GetDimensions(dims.x, dims.y);

    if (DTid.x < dims.x && DTid.y < dims.y)
    {
        float3 color = BackBuffer[DTid.xy].rgb;

        // Clamp HDR values
        color = clamp(color, 0.0, 64.0);

        // Rec.709 luminance
        float lum = dot(color, float3(0.2126, 0.7152, 0.0722));

        // Log-space binning: bin = clamp(floor((log2(lum + 0.001) + 10) / 20 * 256), 0, 255)
        // Maps luminance range [~0.001, ~1024] to [0, 255]
        float logLum = log2(lum + 0.001);
        uint bin = (uint)clamp(floor((logLum + 10.0) / 20.0 * 256.0), 0.0, 255.0);

        InterlockedAdd(gs_hist[bin], 1);

        // Atomically accumulate for averages (using integer representation)
        // We'll use Stats buffer for aggregate accumulation
        InterlockedAdd(Stats[2].x, 1);  // pixel count (as uint reinterpret)
    }

    GroupMemoryBarrierWithGroupSync();

    // Merge shared memory to global
    if (GI < 256)
        InterlockedAdd(Histogram[GI], gs_hist[GI]);
}
)";
```

**Pass 2: Reduction CS** (single thread group, 256 threads)

```hlsl
static constexpr const char* kReductionCS = R"(
RWStructuredBuffer<uint> Histogram : register(u0);
RWStructuredBuffer<float4> Stats   : register(u1);  // [0]=avgRGB+avgLum, [1]=min+max+p05+p95, [2]=count+median, [3]=reserved

groupshared uint gs_hist[256];
groupshared uint gs_prefix[256];

[numthreads(256, 1, 1)]
void CSReduction(uint GI : SV_GroupIndex)
{
    gs_hist[GI] = Histogram[GI];
    GroupMemoryBarrierWithGroupSync();

    // Compute total pixel count
    // Simple sequential reduction by thread 0
    if (GI == 0)
    {
        uint totalPixels = 0;
        float sumLogLum = 0.0;
        uint minBin = 255, maxBin = 0;

        for (uint i = 0; i < 256; i++)
        {
            totalPixels += gs_hist[i];
            if (gs_hist[i] > 0 && i < minBin) minBin = i;
            if (gs_hist[i] > 0 && i > maxBin) maxBin = i;

            // Log-average: sum of (bin_center * count)
            float binCenter = (float(i) + 0.5) / 256.0 * 20.0 - 10.0; // log2 luminance
            sumLogLum += binCenter * float(gs_hist[i]);
        }

        float invTotal = (totalPixels > 0) ? 1.0 / float(totalPixels) : 0.0;

        // Log-average luminance
        float avgLogLum = sumLogLum * invTotal;
        float avgLum = exp2(avgLogLum);

        // Min/max luminance from bin edges
        float minLum = exp2(float(minBin) / 256.0 * 20.0 - 10.0);
        float maxLum = exp2(float(maxBin + 1) / 256.0 * 20.0 - 10.0);

        // Percentiles (prefix sum)
        uint cumulative = 0;
        float p05 = minLum, p50 = avgLum, p95 = maxLum;
        uint threshold05 = uint(float(totalPixels) * 0.05);
        uint threshold50 = uint(float(totalPixels) * 0.50);
        uint threshold95 = uint(float(totalPixels) * 0.95);

        for (uint j = 0; j < 256; j++)
        {
            cumulative += gs_hist[j];
            float binLum = exp2((float(j) + 0.5) / 256.0 * 20.0 - 10.0);
            if (cumulative >= threshold05 && p05 == minLum) p05 = binLum;
            if (cumulative >= threshold50 && p50 == avgLum) p50 = binLum;
            if (cumulative >= threshold95 && p95 == maxLum) p95 = binLum;
        }

        // Normalize histogram bins (write as float for SRV texture)
        // Actually, we normalize on readback. Store raw counts.

        Stats[0] = float4(avgLum, minLum, maxLum, float(totalPixels));
        Stats[1] = float4(p05, p50, p95, 0.0);
    }
}
)";
```

### Per-Frame Execution Sequence

In `D3D11Hook::HookedPresent`, BEFORE FeedbackProcessor::CollectFeedback:

```
1. LuminanceHistogram::Get().ReadBack(context)     -- read PREVIOUS frame's results
2. LuminanceHistogram::Get().Dispatch(context, swapChain)  -- dispatch THIS frame
3. FeedbackProcessor::Get().CollectFeedback(context, swapChain) -- existing
```

**Dispatch implementation:**
1. Get backbuffer texture from swapChain
2. Create a temporary SRV for the backbuffer (or reuse a cached one if resolution unchanged)
3. Clear histogram buffer UAV to 0, clear stats buffer to 0
4. Bind backbuffer SRV to CS slot t0, histogram UAV to u0, stats UAV to u1
5. `CSSetShader(m_histogramCS, nullptr, 0)`
6. `Dispatch(ceil(w/16), ceil(h/16), 1)`
7. Unbind UAVs from CS
8. Bind histogram UAV to u0, stats UAV to u1
9. `CSSetShader(m_reductionCS, nullptr, 0)`
10. `Dispatch(1, 1, 1)`
11. Unbind all CS resources
12. `CopyResource(m_stagingHistogram, m_histogramBuffer)` for CPU readback
13. `CopyResource(m_stagingStats, m_statsBuffer)` for CPU readback
14. Copy histogram data to `m_histogramTex` for SRV binding (or use a CopyStructuredToTexture step)

**ReadBack implementation:**
1. Map staging histogram buffer, read 256 uint values
2. Map staging stats buffer, read HistogramResult values
3. Normalize histogram bins (each / totalPixels)
4. Store in `m_result[m_writeIndex]`, flip `m_writeIndex`

### SRV Binding

The histogram texture SRV at t17 must be injected during ENB passes. This goes through the existing `GBufferManager::HookedPSSetShaderResources` hook. The hook function needs to be extended to also bind the histogram SRV.

**Modification to `C:\Users\Zain\SKSE\SkyrimBridge_v3\src\core\GBufferManager.cpp`**, in `HookedPSSetShaderResources`:

```cpp
void __stdcall HookedPSSetShaderResources(
    ID3D11DeviceContext* a_ctx,
    UINT a_startSlot, UINT a_numViews,
    ID3D11ShaderResourceView* const* a_views)
{
    s_originalPSSetSRV(a_ctx, a_startSlot, a_numViews, a_views);

    auto& gbuf = GBufferManager::Get();
    if (gbuf.IsENBPassActive()) {
        // Material SRV at t15
        if (gbuf.GetSRV()) {
            ID3D11ShaderResourceView* srv = gbuf.GetSRV();
            s_originalPSSetSRV(a_ctx, kMaterialSRVSlot, 1, &srv);
        }

        // Histogram SRV at t17
        auto& hist = LuminanceHistogram::Get();
        if (hist.IsInitialized() && hist.GetHistogramSRV()) {
            ID3D11ShaderResourceView* histSrv = hist.GetHistogramSRV();
            s_originalPSSetSRV(a_ctx, 17, 1, &histSrv);
        }
    }
}
```

### Resource Slots
- **t17**: 256x1 R32_FLOAT histogram texture SRV
- **CS u0/u1**: temporary, only during compute dispatch (not persistent)
- **CS t0**: temporary backbuffer SRV during dispatch

### Initialization
`main.cpp` kDataLoaded, after D3D11Hook::Init:
```cpp
if (SB::LuminanceHistogram::Get().Initialize(dev, sc)) {
    SKSE::log::info("SkyrimBridge: LuminanceHistogram active — 256-bin histogram at t17");
}
```

### Integration with FeedbackProcessor

FeedbackProcessor can optionally read from `LuminanceHistogram::Get().GetResult()` to populate its feedback fields with higher-quality data. This replaces the crude 5x5 grid sampling for histogram/stats. The center-pixel and EMA code stays -- only the histogram bins and scene stats get upgraded.

In `FeedbackProcessor::CollectFeedback`, after the existing grid sampling block, add:

```cpp
// Override histogram from compute if available
auto& histCompute = LuminanceHistogram::Get();
if (histCompute.IsInitialized() && histCompute.IsEnabled()) {
    auto& hr = histCompute.GetResult();
    // Use compute histogram percentiles for 4-bin approximation
    // ... (map 256-bin to 4-bin thresholds)
    fb.SceneStats.x = hr.avgLuminance;  // key value (much better than 25-sample estimate)
}
```

---

## Feature 3: Texture3D LUT Injection (`LUTManager`)

### New Files
- `C:\Users\Zain\SKSE\SkyrimBridge_v3\src\core\LUTManager.h`
- `C:\Users\Zain\SKSE\SkyrimBridge_v3\src\core\LUTManager.cpp`

### Design

**LUTManager class (singleton):**

```cpp
// LUTManager.h
#pragma once
#include <d3d11.h>
#include <string>
#include <vector>
#include <filesystem>

namespace SB
{
    struct LUTEntry
    {
        std::string name;           // Filename without extension
        ID3D11Texture3D* texture = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
    };

    class LUTManager
    {
    public:
        static LUTManager& Get();

        bool Initialize(ID3D11Device* a_device, const std::filesystem::path& lutDir);
        void Shutdown();

        // Get the currently active LUT SRV for shader binding (t18)
        ID3D11ShaderResourceView* GetActiveSRV() const;

        // Get the trilinear sampler (s2)
        ID3D11SamplerState* GetSampler() const { return m_sampler; }

        // LUT management
        int GetLUTCount() const { return static_cast<int>(m_luts.size()); }
        const std::string& GetLUTName(int index) const { return m_luts[index].name; }
        int GetActiveIndex() const { return m_activeIndex; }
        void SetActiveIndex(int index);

        // Get the sampler slot used (for hook injection)
        static constexpr uint32_t kSamplerSlot = 2;
        static constexpr uint32_t kSRVSlot = 18;

        bool IsInitialized() const { return m_initialized; }
        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool a_enabled) { m_enabled = a_enabled; }

    private:
        LUTManager() = default;

        // Load a single 8x8 tiled PNG atlas and convert to 64^3 Texture3D
        bool LoadAtlasPNG(ID3D11Device* a_device, const std::filesystem::path& path);

        // Decode PNG to RGBA pixels (minimal stb_image or WIC decode)
        bool DecodePNG(const std::filesystem::path& path,
                       std::vector<uint8_t>& pixels, uint32_t& w, uint32_t& h);

        // Rearrange 8x8 tiled atlas (512x512 RGBA) into 64x64x64 volume
        bool AtlasToVolume(const uint8_t* atlas, uint32_t atlasW, uint32_t atlasH,
                           std::vector<uint8_t>& volume);

        bool m_initialized = false;
        bool m_enabled = true;
        int m_activeIndex = 0;

        std::vector<LUTEntry> m_luts;
        ID3D11SamplerState* m_sampler = nullptr;
    };
}
```

### Atlas-to-Volume Conversion

An 8x8 tiled atlas stores 64 slices of a 64x64x64 LUT. Each tile is 64x64 pixels. The atlas is 512x512 pixels total (8 tiles x 64 pixels per tile = 512).

Tile layout: tile at row `r`, column `c` contains slice `z = r * 8 + c`.

```cpp
bool LUTManager::AtlasToVolume(const uint8_t* atlas, uint32_t atlasW, uint32_t atlasH,
                                std::vector<uint8_t>& volume)
{
    constexpr int kLUTSize = 64;
    constexpr int kTilesPerRow = 8;
    constexpr int kBPP = 4; // RGBA

    if (atlasW != kLUTSize * kTilesPerRow || atlasH != kLUTSize * kTilesPerRow)
        return false; // expected 512x512

    volume.resize(kLUTSize * kLUTSize * kLUTSize * kBPP);

    for (int z = 0; z < kLUTSize; z++) {
        int tileCol = z % kTilesPerRow;
        int tileRow = z / kTilesPerRow;
        int tileX = tileCol * kLUTSize;
        int tileY = tileRow * kLUTSize;

        for (int y = 0; y < kLUTSize; y++) {
            for (int x = 0; x < kLUTSize; x++) {
                int srcIdx = ((tileY + y) * atlasW + (tileX + x)) * kBPP;
                int dstIdx = (z * kLUTSize * kLUTSize + y * kLUTSize + x) * kBPP;
                std::memcpy(&volume[dstIdx], &atlas[srcIdx], kBPP);
            }
        }
    }
    return true;
}
```

### PNG Loading

For PNG decoding, use the Windows Imaging Component (WIC) since it is always available on Windows 10+ and avoids adding stb_image as a dependency. The implementation uses `IWICImagingFactory`, `IWICBitmapDecoder`, `IWICFormatConverter` to decode to RGBA8.

Alternatively, add `stb_image.h` as a single-header include (simpler). Since this is a tools concern, a single-header library is acceptable:

```cpp
// At the top of LUTManager.cpp
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"  // Place stb_image.h in src/core/ or src/vendor/

bool LUTManager::DecodePNG(const std::filesystem::path& path,
                            std::vector<uint8_t>& pixels, uint32_t& w, uint32_t& h)
{
    int iw, ih, channels;
    uint8_t* data = stbi_load(path.string().c_str(), &iw, &ih, &channels, 4); // force RGBA
    if (!data) return false;

    w = static_cast<uint32_t>(iw);
    h = static_cast<uint32_t>(ih);
    pixels.assign(data, data + w * h * 4);
    stbi_image_free(data);
    return true;
}
```

### Texture3D Creation

```cpp
bool LUTManager::LoadAtlasPNG(ID3D11Device* a_device, const std::filesystem::path& path)
{
    std::vector<uint8_t> pixels;
    uint32_t w, h;
    if (!DecodePNG(path, pixels, w, h)) return false;

    std::vector<uint8_t> volume;
    if (!AtlasToVolume(pixels.data(), w, h, volume)) return false;

    constexpr int kLUTSize = 64;

    D3D11_TEXTURE3D_DESC desc{};
    desc.Width = kLUTSize;
    desc.Height = kLUTSize;
    desc.Depth = kLUTSize;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem = volume.data();
    initData.SysMemPitch = kLUTSize * 4;        // row pitch (one row of one slice)
    initData.SysMemSlicePitch = kLUTSize * kLUTSize * 4; // slice pitch

    LUTEntry entry;
    entry.name = path.stem().string();

    HRESULT hr = a_device->CreateTexture3D(&desc, &initData, &entry.texture);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
    srvDesc.Texture3D.MipLevels = 1;
    srvDesc.Texture3D.MostDetailedMip = 0;

    hr = a_device->CreateShaderResourceView(entry.texture, &srvDesc, &entry.srv);
    if (FAILED(hr)) { entry.texture->Release(); return false; }

    m_luts.push_back(std::move(entry));
    return true;
}
```

### Sampler Creation

```cpp
// In Initialize:
D3D11_SAMPLER_DESC sampDesc{};
sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; // Trilinear
sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

hr = a_device->CreateSamplerState(&sampDesc, &m_sampler);
```

### SRV and Sampler Binding

Modify the existing `HookedPSSetShaderResources` in `GBufferManager.cpp` to also inject the LUT SRV at t18. Additionally, we need a sampler hook. ENB may not call `PSSetSamplers` at every draw call, so the safest approach is to bind the sampler alongside the SRV injection.

**In the PSSetShaderResources hook (already modified for Feature 2):**

```cpp
// LUT SRV at t18 + sampler at s2
auto& lut = LUTManager::Get();
if (lut.IsInitialized() && lut.IsEnabled() && lut.GetActiveSRV()) {
    ID3D11ShaderResourceView* lutSrv = lut.GetActiveSRV();
    s_originalPSSetSRV(a_ctx, LUTManager::kSRVSlot, 1, &lutSrv);

    // Also bind the trilinear sampler — must use original context call
    ID3D11SamplerState* samp = lut.GetSampler();
    a_ctx->PSSetSamplers(LUTManager::kSamplerSlot, 1, &samp);
}
```

Note: `PSSetSamplers` is called directly on the context (no hook needed). The sampler state is persistent until overwritten, so setting it during every PSSetShaderResources call is safe but slightly redundant. A better approach: set the sampler once per frame in `OnENBFrame` after `GBufferManager::SetENBPassActive(true)`.

### Resource Slots
- **t18**: Texture3D SRV (64^3 RGBA8)
- **s2**: Trilinear SamplerState

### Initialization
`main.cpp` kDataLoaded:
```cpp
{
    auto lutDir = std::filesystem::path("Data/SKSE/Plugins/SkyrimBridge/LUTs");
    if (SB::LUTManager::Get().Initialize(dev, lutDir)) {
        SKSE::log::info("SkyrimBridge: LUTManager active — {} LUTs loaded, t18/s2",
            SB::LUTManager::Get().GetLUTCount());
    }
}
```

### HLSL Shader Usage

In ENB shaders:

```hlsl
Texture3D<float4> SB_FilmLUT : register(t18);
SamplerState SB_LUTSampler   : register(s2);

float3 ApplyLUT(float3 color)
{
    // Clamp to [0,1] for LUT lookup (LUT stores the transform)
    float3 coord = saturate(color);
    return SB_FilmLUT.Sample(SB_LUTSampler, coord).rgb;
}
```

### Dependencies
- `stb_image.h` must be added to the project (single file, place in `src/vendor/` or `src/core/`)
- No new vcpkg dependency

---

## Feature 4: Hi-Z Depth Pyramid (`HiZPyramid`)

### New Files
- `C:\Users\Zain\SKSE\SkyrimBridge_v3\src\core\HiZPyramid.h`
- `C:\Users\Zain\SKSE\SkyrimBridge_v3\src\core\HiZPyramid.cpp`

### Design

**HiZPyramid class (singleton):**

```cpp
// HiZPyramid.h
#pragma once
#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ComputeShader;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11UnorderedAccessView;
struct ID3D11Buffer;
struct IDXGISwapChain;

namespace SB
{
    class HiZPyramid
    {
    public:
        static HiZPyramid& Get();

        bool Initialize(ID3D11Device* a_device, IDXGISwapChain* a_swapChain);
        void Shutdown();

        // Build the mip chain from the game's depth buffer.
        // Call from OnENBFrame (after game renders, before ENB passes).
        void BuildPyramid(ID3D11DeviceContext* a_ctx);

        // Get SRV for the full mip chain (t19)
        ID3D11ShaderResourceView* GetSRV() const { return m_pyramidSRV; }

        uint32_t GetWidth()    const { return m_width; }
        uint32_t GetHeight()   const { return m_height; }
        uint32_t GetMipCount() const { return m_mipCount; }

        static constexpr uint32_t kSRVSlot = 19;

        bool IsInitialized() const { return m_initialized; }
        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool a_enabled) { m_enabled = a_enabled; }

    private:
        HiZPyramid() = default;

        bool CompileComputeShader(ID3D11Device* a_device);
        bool CreatePyramidTexture(ID3D11Device* a_device, uint32_t w, uint32_t h);
        bool AcquireDepthSRV(ID3D11DeviceContext* a_ctx);
        void ReleasePyramidTexture();

        bool m_initialized = false;
        bool m_enabled = true;

        ID3D11ComputeShader* m_downsampleCS = nullptr;

        // Pyramid texture: R32_FLOAT with full mip chain
        // Stores MIN depth (conservative occlusion)
        ID3D11Texture2D* m_pyramidTex = nullptr;

        // Full-chain SRV for shader reads
        ID3D11ShaderResourceView* m_pyramidSRV = nullptr;

        // Per-mip UAVs for compute writes
        static constexpr uint32_t kMaxMips = 14; // log2(16384) = 14
        ID3D11UnorderedAccessView* m_mipUAV[kMaxMips]{};

        // Per-mip SRVs for reading the previous level
        ID3D11ShaderResourceView* m_mipSRV[kMaxMips]{};

        // Constant buffer for per-mip dispatch parameters
        ID3D11Buffer* m_paramCB = nullptr;

        // Game's depth buffer SRV (fetched each frame)
        ID3D11ShaderResourceView* m_depthSRV = nullptr;
        bool m_ownDepthSRV = false;

        uint32_t m_width = 0, m_height = 0;
        uint32_t m_mipCount = 0;

        ID3D11Device* m_device = nullptr;
    };
}
```

### Accessing the Depth Buffer

Skyrim uses a D24_UNORM_S8_UINT or D32_FLOAT_S8X24_UINT depth-stencil buffer. To read it in a compute shader, we need an SRV. The depth-stencil texture is owned by the game and may not have `D3D11_BIND_SHADER_RESOURCE` set.

**Strategy:** Get the currently bound depth-stencil view from the context via `OMGetRenderTargets`, extract the underlying `ID3D11Texture2D`, then check its format. If it has `BIND_SHADER_RESOURCE`, create an SRV with the depth-read format (`DXGI_FORMAT_R24_UNORM_X8_TYPELESS` for D24S8, or `DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS` for D32S8X24). If it does not have the bind flag, CopyResource to a separate texture that does.

```cpp
bool HiZPyramid::AcquireDepthSRV(ID3D11DeviceContext* a_ctx)
{
    // Release previous
    if (m_depthSRV && m_ownDepthSRV) { m_depthSRV->Release(); m_depthSRV = nullptr; }
    m_ownDepthSRV = false;

    // Get currently bound depth-stencil view
    ID3D11DepthStencilView* dsv = nullptr;
    a_ctx->OMGetRenderTargets(0, nullptr, &dsv);
    if (!dsv) return false;

    ID3D11Resource* depthRes = nullptr;
    dsv->GetResource(&depthRes);
    dsv->Release();
    if (!depthRes) return false;

    ID3D11Texture2D* depthTex = nullptr;
    depthRes->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&depthTex);
    depthRes->Release();
    if (!depthTex) return false;

    D3D11_TEXTURE2D_DESC depthDesc;
    depthTex->GetDesc(&depthDesc);

    // Determine SRV format from typeless depth format
    DXGI_FORMAT srvFormat = DXGI_FORMAT_UNKNOWN;
    if (depthDesc.Format == DXGI_FORMAT_R24G8_TYPELESS ||
        depthDesc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT)
        srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    else if (depthDesc.Format == DXGI_FORMAT_R32G8X24_TYPELESS ||
             depthDesc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
        srvFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    else if (depthDesc.Format == DXGI_FORMAT_R32_TYPELESS ||
             depthDesc.Format == DXGI_FORMAT_D32_FLOAT)
        srvFormat = DXGI_FORMAT_R32_FLOAT;

    if (srvFormat != DXGI_FORMAT_UNKNOWN && (depthDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE)) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = srvFormat;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(depthTex, &srvDesc, &m_depthSRV);
        m_ownDepthSRV = true;
    }

    depthTex->Release();
    return m_depthSRV != nullptr;
}
```

### Compute Shader (Embedded HLSL)

```hlsl
static constexpr const char* kDownsampleCS = R"(
Texture2D<float> SrcMip : register(t0);
RWTexture2D<float> DstMip : register(u0);

cbuffer HiZParams : register(b0)
{
    uint2 DstDimensions;
    uint2 Padding;
};

[numthreads(8, 8, 1)]
void CSDownsample(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= DstDimensions.x || DTid.y >= DstDimensions.y)
        return;

    // Sample 2x2 block from source
    uint2 srcCoord = DTid.xy * 2;

    float d00 = SrcMip[srcCoord + uint2(0, 0)];
    float d10 = SrcMip[srcCoord + uint2(1, 0)];
    float d01 = SrcMip[srcCoord + uint2(0, 1)];
    float d11 = SrcMip[srcCoord + uint2(1, 1)];

    // MIN for conservative occlusion (closer = smaller depth in [0,1] reversed Z)
    // Skyrim uses reversed-Z: near=1.0, far=0.0
    // For conservative occlusion culling, we want MAX in reversed-Z (= closest surface)
    float result = max(max(d00, d10), max(d01, d11));

    DstMip[DTid.xy] = result;
}
)";
```

Note: Skyrim uses reversed-Z depth, so the "closest" surface has the largest depth value. For conservative occlusion, we take `max` of the 2x2 block.

### Pyramid Texture Creation

```cpp
bool HiZPyramid::CreatePyramidTexture(ID3D11Device* a_device, uint32_t w, uint32_t h)
{
    m_width = w;
    m_height = h;
    m_mipCount = static_cast<uint32_t>(std::floor(std::log2(std::max(w, h)))) + 1;
    if (m_mipCount > kMaxMips) m_mipCount = kMaxMips;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = m_mipCount;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    HRESULT hr = a_device->CreateTexture2D(&desc, nullptr, &m_pyramidTex);
    if (FAILED(hr)) return false;

    // Full-chain SRV for shader reads
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = m_mipCount;
    hr = a_device->CreateShaderResourceView(m_pyramidTex, &srvDesc, &m_pyramidSRV);
    if (FAILED(hr)) { m_pyramidTex->Release(); return false; }

    // Per-mip UAVs (for compute writes) and SRVs (for reading previous level)
    for (uint32_t i = 0; i < m_mipCount; i++) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = i;
        a_device->CreateUnorderedAccessView(m_pyramidTex, &uavDesc, &m_mipUAV[i]);

        D3D11_SHADER_RESOURCE_VIEW_DESC mipSrvDesc{};
        mipSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        mipSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        mipSrvDesc.Texture2D.MostDetailedMip = i;
        mipSrvDesc.Texture2D.MipLevels = 1;
        a_device->CreateShaderResourceView(m_pyramidTex, &mipSrvDesc, &m_mipSRV[i]);
    }

    // Params constant buffer
    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = 16; // uint2 + uint2 padding
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    a_device->CreateBuffer(&cbDesc, nullptr, &m_paramCB);

    return true;
}
```

### Per-Frame Build Sequence

Called from `OnENBFrame` right before `GBufferManager::Clear` and `SetENBPassActive(true)`:

```cpp
void HiZPyramid::BuildPyramid(ID3D11DeviceContext* a_ctx)
{
    if (!m_initialized || !m_enabled) return;

    // Step 1: Acquire depth buffer SRV
    if (!AcquireDepthSRV(a_ctx)) return;

    // Step 2: Copy mip 0 from depth buffer
    // Bind depth SRV as CS t0, pyramid mip 0 UAV as CS u0
    // Dispatch a simple copy/convert shader (or use CopySubresourceRegion if formats match)
    // For simplicity, dispatch the downsample CS with source = depth SRV, dst = mip0 UAV,
    // but with a 1:1 copy (no minification)

    // Actually, for mip 0 we can use CopySubresourceRegion if the depth is R32_FLOAT.
    // If not (D24S8), we need a convert shader. Use the CS for all levels including 0.

    a_ctx->CSSetShader(m_downsampleCS, nullptr, 0);

    // Mip 0: source = game depth, dst = pyramid mip 0
    // Need a special "copy" pass since source dimensions == dst dimensions
    {
        ID3D11ShaderResourceView* srv = m_depthSRV;
        ID3D11UnorderedAccessView* uav = m_mipUAV[0];
        a_ctx->CSSetShaderResources(0, 1, &srv);
        a_ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

        // Update params CB
        struct { uint32_t w, h, pad0, pad1; } params = { m_width, m_height, 0, 0 };
        D3D11_MAPPED_SUBRESOURCE mapped;
        a_ctx->Map(m_paramCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, &params, sizeof(params));
        a_ctx->Unmap(m_paramCB, 0);
        a_ctx->CSSetConstantBuffers(0, 1, &m_paramCB);

        a_ctx->Dispatch((m_width + 7) / 8, (m_height + 7) / 8, 1);
    }

    // Mip 1..N: source = previous mip SRV, dst = current mip UAV
    for (uint32_t mip = 1; mip < m_mipCount; mip++) {
        // Unbind previous UAV, bind previous mip as SRV
        ID3D11UnorderedAccessView* nullUAV = nullptr;
        a_ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

        ID3D11ShaderResourceView* prevSRV = m_mipSRV[mip - 1];
        ID3D11UnorderedAccessView* curUAV = m_mipUAV[mip];
        a_ctx->CSSetShaderResources(0, 1, &prevSRV);
        a_ctx->CSSetUnorderedAccessViews(0, 1, &curUAV, nullptr);

        uint32_t mipW = std::max(1u, m_width >> mip);
        uint32_t mipH = std::max(1u, m_height >> mip);

        struct { uint32_t w, h, pad0, pad1; } params = { mipW, mipH, 0, 0 };
        D3D11_MAPPED_SUBRESOURCE mapped;
        a_ctx->Map(m_paramCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, &params, sizeof(params));
        a_ctx->Unmap(m_paramCB, 0);

        a_ctx->Dispatch((mipW + 7) / 8, (mipH + 7) / 8, 1);
    }

    // Cleanup: unbind all CS resources
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    a_ctx->CSSetShaderResources(0, 1, &nullSRV);
    a_ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    a_ctx->CSSetShader(nullptr, nullptr, 0);
}
```

**Important note on mip 0:** The existing downsample CS uses `SrcMip[coord * 2]` which is wrong for the initial copy from depth to mip 0 (1:1 copy). We need either a separate copy CS or a conditional branch. The simplest solution: for mip 0, use a separate pass that reads `SrcMip[DTid.xy]` and writes `DstMip[DTid.xy] = SrcMip[DTid.xy]`. Since adding a second CS is cheap, define `kCopyCS` for mip 0 and `kDownsampleCS` for mip 1+.

```hlsl
static constexpr const char* kCopyCS = R"(
Texture2D<float> SrcDepth : register(t0);
RWTexture2D<float> DstMip : register(u0);

cbuffer HiZParams : register(b0)
{
    uint2 DstDimensions;
    uint2 Padding;
};

[numthreads(8, 8, 1)]
void CSCopy(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= DstDimensions.x || DTid.y >= DstDimensions.y)
        return;
    DstMip[DTid.xy] = SrcDepth[DTid.xy];
}
)";
```

### SRV Binding

In `GBufferManager.cpp` `HookedPSSetShaderResources`, add:

```cpp
// Hi-Z pyramid SRV at t19
auto& hiz = HiZPyramid::Get();
if (hiz.IsInitialized() && hiz.IsEnabled() && hiz.GetSRV()) {
    ID3D11ShaderResourceView* hizSrv = hiz.GetSRV();
    s_originalPSSetSRV(a_ctx, HiZPyramid::kSRVSlot, 1, &hizSrv);
}
```

### Resource Slots
- **t19**: Texture2D R32_FLOAT with full mip chain (SRV for ENB shaders)
- **CS t0, u0, b0**: temporary during compute dispatch

### Initialization
`main.cpp` kDataLoaded, after GBufferManager init:
```cpp
if (SB::HiZPyramid::Get().Initialize(dev, sc)) {
    SKSE::log::info("SkyrimBridge: HiZPyramid active — {} mips at t19",
        SB::HiZPyramid::Get().GetMipCount());
}
```

### HLSL Shader Usage

```hlsl
Texture2D<float> SB_HiZPyramid : register(t19);

float SampleHiZ(float2 uv, uint mipLevel)
{
    uint2 dims;
    SB_HiZPyramid.GetDimensions(dims.x, dims.y); // returns mip0 dimensions
    dims >>= mipLevel;
    uint2 coord = uint2(uv * float2(dims));
    return SB_HiZPyramid.Load(int3(coord, mipLevel));
}
```

---

## Overall Integration Changes

### CMakeLists.txt Additions

Add these four new source files to the `SOURCES` list:

```cmake
# Feature 1: Shader Bytecode Cache
src/core/ShaderCache.cpp

# Feature 2: Compute-based Luminance Histogram
src/core/LuminanceHistogram.cpp

# Feature 3: Texture3D LUT Injection
src/core/LUTManager.cpp

# Feature 4: Hi-Z Depth Pyramid
src/core/HiZPyramid.cpp
```

No new link libraries needed (d3dcompiler already linked). If using stb_image for Feature 3, add `src/vendor/stb_image.h` or a header-only include directory.

### main.cpp Initialization Order (kDataLoaded)

The order matters because of dependencies:

```
1. D3D11Hook::Init()                           // existing — provides device/ctx/swapChain
2. SB::ConstantBuffer::Initialize()             // existing
3. SB::ShaderCache::Initialize(cacheDir)        // NEW — must precede ShaderDebug
4. SB::Debug::ShaderDebug::Install()            // existing — hooks D3DCompile (now with cache)
5. SB::FeedbackProcessor::Initialize()          // existing
6. SB::LuminanceHistogram::Initialize()         // NEW — compute shader compiled here
7. SB::WriteBackProcessor::LoadConfig()         // existing
8. SB::MaterialTracker::Install()               // existing
9. SB::GBufferManager::Initialize() + Hook()    // existing — hook now injects t15,t17,t18,t19
10. SB::LUTManager::Initialize(lutDir)          // NEW — loads PNGs, creates Texture3D
11. SB::HiZPyramid::Initialize()                // NEW — creates pyramid + CS
12. SB::DXBCPatcher::Install()                  // existing
13. SB::PerfMonitor::Initialize()               // existing
```

### OnENBFrame Execution Order

```
existing: BeginFrame(), PerfMonitor::BeginFrame(), tracker updates...
existing: FeedbackProcessor::DistributeFeedback()
existing: WriteBackProcessor::Execute()

NEW:      HiZPyramid::BuildPyramid(ctx)          // build from depth buffer BEFORE ENB
existing: GBufferManager::Clear() + SetENBPassActive(true)
          // LUT sampler binding happens implicitly during PSSetSRV hook

existing: ConstantBuffer::UpdateAndBind()
existing: SharedMemoryBridge::WriteFrame()
existing: PerfMonitor::EndFrame()
```

### HookedPresent Execution Order

```
NEW:      LuminanceHistogram::ReadBack(ctx)        // read PREVIOUS frame's compute results
NEW:      LuminanceHistogram::Dispatch(ctx, sc)    // dispatch THIS frame's compute
existing: FeedbackProcessor::CollectFeedback(ctx, sc)
existing: GBufferManager::SetENBPassActive(false)
existing: ImGui rendering...
existing: ShaderDebug overlay...
existing: s_originalPresent()
```

### GBufferManager.cpp PSSetShaderResources Hook (Final Form)

The hook function in `GBufferManager.cpp` becomes the central SRV injection point for all features:

```cpp
void __stdcall HookedPSSetShaderResources(
    ID3D11DeviceContext* a_ctx,
    UINT a_startSlot, UINT a_numViews,
    ID3D11ShaderResourceView* const* a_views)
{
    s_originalPSSetSRV(a_ctx, a_startSlot, a_numViews, a_views);

    auto& gbuf = GBufferManager::Get();
    if (!gbuf.IsENBPassActive()) return;

    // t15: Material ID
    if (auto* srv = gbuf.GetSRV()) {
        s_originalPSSetSRV(a_ctx, 15, 1, &srv);
    }

    // t17: Luminance Histogram
    auto& hist = LuminanceHistogram::Get();
    if (hist.IsInitialized() && hist.IsEnabled()) {
        if (auto* srv = hist.GetHistogramSRV()) {
            s_originalPSSetSRV(a_ctx, 17, 1, &srv);
        }
    }

    // t18: Film LUT + s2: Trilinear sampler
    auto& lut = LUTManager::Get();
    if (lut.IsInitialized() && lut.IsEnabled()) {
        if (auto* srv = lut.GetActiveSRV()) {
            s_originalPSSetSRV(a_ctx, 18, 1, &srv);
            auto* samp = lut.GetSampler();
            a_ctx->PSSetSamplers(2, 1, &samp);
        }
    }

    // t19: Hi-Z Depth Pyramid
    auto& hiz = HiZPyramid::Get();
    if (hiz.IsInitialized() && hiz.IsEnabled()) {
        if (auto* srv = hiz.GetSRV()) {
            s_originalPSSetSRV(a_ctx, 19, 1, &srv);
        }
    }
}
```

### Resource Slot Summary

| Slot | Feature | Format | Description |
|------|---------|--------|-------------|
| b7 | Existing | AllData CB | SkyrimBridge constant buffer |
| b15 | Existing | uint CB | Material type constant |
| t15 | Existing | R8_UINT | Material ID g-buffer |
| t17 | Feature 2 | R32_FLOAT 256x1 | Luminance histogram |
| t18 | Feature 3 | R8G8B8A8 64^3 | Film LUT (Texture3D) |
| t19 | Feature 4 | R32_FLOAT mipped | Hi-Z depth pyramid |
| s0 | Existing (ENB) | Point sampler | |
| s1 | Existing (ENB) | Linear sampler | |
| s2 | Feature 3 | Trilinear clamp | LUT sampler |
| u4 | Existing | R8_UINT UAV | Material g-buffer write |

### Potential Challenges

1. **Feature 1 (ShaderCache):** ENB's `ID3DInclude` handler resolves `#include` directives. If ENB passes pre-processed source to D3DCompile (no includes left), hashing the source text is sufficient. If ENB passes un-preprocessed source, the include resolution happens inside D3DCompile and our cache would miss file changes in `.fxh` files. Need to verify at runtime which is the case. Mitigation: log `SrcDataSize` on first few compilations to determine if source is pre-processed.

2. **Feature 2 (LuminanceHistogram):** Creating an SRV from the swap chain backbuffer requires it to have `BIND_SHADER_RESOURCE`. Skyrim's swap chain format is typically `DXGI_FORMAT_R8G8B8A8_UNORM` or similar, which should support SRV. If not, CopyResource to a staging texture with SRV support first.

3. **Feature 3 (LUTManager):** The `stb_image.h` include introduces a dependency. Alternative: use WIC (`IWICImagingFactory`) for zero-dependency PNG loading, but WIC code is more verbose. Recommend stb_image for simplicity.

4. **Feature 4 (HiZPyramid):** The depth buffer may not have `D3D11_BIND_SHADER_RESOURCE`. Skyrim on DX11 typically creates a typeless depth texture (`R24G8_TYPELESS` or `R32G8X24_TYPELESS`) which supports SRV creation. However, if the game's depth texture lacks the SRV bind flag, a per-frame CopyResource to a separate SRV-capable texture is needed, which costs VRAM and bandwidth.

5. **All compute features:** Compute shader dispatch must save and restore any CS state that ENB or the game might depend on. Use `CSGetShader`/`CSGetShaderResources`/`CSGetUnorderedAccessViews` before dispatch, restore after. This is critical to avoid corrupting game/ENB render state.

---

### Critical Files for Implementation
- `C:\Users\Zain\SKSE\SkyrimBridge_v3\src\core\SB_ShaderDebug.cpp` - Must modify HookD3DCompile/HookD3DCompile2 to add cache lookup/store before/after real compilation
- `C:\Users\Zain\SKSE\SkyrimBridge_v3\src\core\GBufferManager.cpp` - Must extend HookedPSSetShaderResources to inject SRVs at t17, t18, t19 alongside existing t15
- `C:\Users\Zain\SKSE\SkyrimBridge_v3\src\core\main.cpp` - Must add initialization calls for all 4 new singletons in correct dependency order at kDataLoaded, and dispatch calls in OnENBFrame
- `C:\Users\Zain\SKSE\SkyrimBridge_v3\src\core\D3D11Hook.cpp` - Must add LuminanceHistogram dispatch + readback calls in HookedPresent before FeedbackProcessor
- `C:\Users\Zain\SKSE\SkyrimBridge_v3\CMakeLists.txt` - Must add 4 new .cpp source files to SOURCES list