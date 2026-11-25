// RawAudioOutput.cpp: Real-Time Windows WASAPI Event-Driven Background Audio Streamer &
// Deterministic NullSink Capture Backend (`raw::audio`).
// Original work, MIT.

#include "RawAudioOutput.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#endif

namespace raw::audio {
namespace {

constexpr float kPiF = 3.14159265358979323846f;
constexpr uint32_t kNullRingFramesPow2 = 1u << 16; // 65,536 stereo frames (~1.36 s @ 48 kHz)
constexpr uint32_t kNullRingMask       = kNullRingFramesPow2 - 1u;

inline float ClampF(float v, float lo, float hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float SanitizeSample(float s) noexcept {
    return std::isfinite(s) ? ClampF(s, -0.99f, 0.99f) : 0.0f;
}

// Converts Beaufort scale (0..12) to physical wind speed in m/s: v = 0.836 * B^(3/2).
inline float BeaufortToWindMs(float beaufort) noexcept {
    const float b = ClampF(beaufort, 0.0f, 12.0f);
    return 0.836f * std::pow(b, 1.5f);
}

inline AudioSceneState ClampSceneState(const AudioSceneState& in) noexcept {
    AudioSceneState s = in;
    if (static_cast<uint8_t>(s.environment) >= static_cast<uint8_t>(AcousticEnvironment::Count)) {
        s.environment = AcousticEnvironment::SnowDampedPineForest;
    }
    s.beaufortScale    = ClampF(std::isfinite(s.beaufortScale) ? s.beaufortScale : 0.0f, 0.0f, 12.0f);
    s.rain01           = ClampF(std::isfinite(s.rain01) ? s.rain01 : 0.0f, 0.0f, 1.0f);
    s.snow01           = ClampF(std::isfinite(s.snow01) ? s.snow01 : 0.0f, 0.0f, 1.0f);
    s.fireIntensity01  = ClampF(std::isfinite(s.fireIntensity01) ? s.fireIntensity01 : 0.0f, 0.0f, 1.0f);
    s.fireDistanceU    = std::max(16.0f, std::isfinite(s.fireDistanceU) ? s.fireDistanceU : 256.0f);
    s.waterIntensity01 = ClampF(std::isfinite(s.waterIntensity01) ? s.waterIntensity01 : 0.0f, 0.0f, 1.0f);
    s.underwater01     = ClampF(std::isfinite(s.underwater01) ? s.underwater01 : 0.0f, 0.0f, 1.0f);
    s.masterGain       = ClampF(std::isfinite(s.masterGain) ? s.masterGain : 0.0f, 0.0f, RawAudioOutput::kMaxMasterGain);
    return s;
}

#ifdef _WIN32
// Inline GUID definitions so ZERO extra Win32 static libraries (uuid.lib / ole32.lib) are required.
constexpr GUID kClsidMMDeviceEnumerator = {
    0xBCDE0395u, 0xE52Fu, 0x467Cu, {0x8Eu, 0x3Du, 0xC4u, 0x57u, 0x92u, 0x91u, 0x69u, 0x2Eu}
};
constexpr GUID kIidIMMDeviceEnumerator = {
    0xA95664D2u, 0x9614u, 0x4F35u, {0xA7u, 0x46u, 0xDEu, 0x8Du, 0xB6u, 0x36u, 0x17u, 0xE6u}
};
constexpr GUID kIidIAudioClient = {
    0x1CB9AD4Cu, 0xDBFAu, 0x4C32u, {0xB1u, 0x78u, 0xC2u, 0xF5u, 0x68u, 0xA7u, 0x03u, 0xB2u}
};
constexpr GUID kIidIAudioRenderClient = {
    0xF294ACFCu, 0x3146u, 0x4483u, {0xA7u, 0xBFu, 0xADu, 0xDCu, 0xA7u, 0xC2u, 0x60u, 0xE2u}
};
constexpr GUID kSubformatIeeeFloat = {
    0x00000003u, 0x0000u, 0x0010u, {0x80u, 0x00u, 0x00u, 0xAAu, 0x00u, 0x38u, 0x9Bu, 0x71u}
};
constexpr GUID kSubformatPcm = {
    0x00000001u, 0x0000u, 0x0010u, {0x80u, 0x00u, 0x00u, 0xAAu, 0x00u, 0x38u, 0x9Bu, 0x71u}
};

struct DynOle32 {
    HMODULE mod = nullptr;
    using CoInitializeExFn   = HRESULT(WINAPI*)(LPVOID, DWORD);
    using CoUninitializeFn   = void(WINAPI*)();
    using CoCreateInstanceFn = HRESULT(WINAPI*)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
    using CoTaskMemFreeFn    = void(WINAPI*)(LPVOID);

    CoInitializeExFn   coInit   = nullptr;
    CoUninitializeFn   coUninit = nullptr;
    CoCreateInstanceFn coCreate = nullptr;
    CoTaskMemFreeFn    coFree   = nullptr;

    bool Load() {
        mod = LoadLibraryW(L"ole32.dll");
        if (!mod) return false;
        coInit   = reinterpret_cast<CoInitializeExFn>(GetProcAddress(mod, "CoInitializeEx"));
        coUninit = reinterpret_cast<CoUninitializeFn>(GetProcAddress(mod, "CoUninitialize"));
        coCreate = reinterpret_cast<CoCreateInstanceFn>(GetProcAddress(mod, "CoCreateInstance"));
        coFree   = reinterpret_cast<CoTaskMemFreeFn>(GetProcAddress(mod, "CoTaskMemFree"));
        return coInit && coUninit && coCreate && coFree;
    }

    ~DynOle32() {
        if (mod) FreeLibrary(mod);
    }
};

struct DynAvrt {
    HMODULE mod = nullptr;
    using SetCharsFn = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
    using RevertFn   = BOOL(WINAPI*)(HANDLE);
    SetCharsFn setChars = nullptr;
    RevertFn   revert   = nullptr;

    void Load() {
        mod = LoadLibraryW(L"avrt.dll");
        if (!mod) return;
        setChars = reinterpret_cast<SetCharsFn>(GetProcAddress(mod, "AvSetMmThreadCharacteristicsW"));
        revert   = reinterpret_cast<RevertFn>(GetProcAddress(mod, "AvRevertMmThreadCharacteristics"));
    }

    ~DynAvrt() {
        if (mod) FreeLibrary(mod);
    }
};

bool IsWaveFormatFloat(const WAVEFORMATEX* wf) noexcept {
    if (!wf) return false;
    if (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wf->cbSize >= 22u) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
        return std::memcmp(&ext->SubFormat, &kSubformatIeeeFloat, sizeof(GUID)) == 0;
    }
    return false;
}

bool IsWaveFormatPcm16(const WAVEFORMATEX* wf) noexcept {
    if (!wf || wf->wBitsPerSample != 16u) return false;
    if (wf->wFormatTag == WAVE_FORMAT_PCM) return true;
    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wf->cbSize >= 22u) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
        return std::memcmp(&ext->SubFormat, &kSubformatPcm, sizeof(GUID)) == 0;
    }
    return false;
}

std::string WideToUtf8(const wchar_t* ws) {
    if (!ws || !*ws) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, out.data(), n, nullptr, nullptr);
    return out;
}
#endif

}  // namespace

struct RawAudioOutput::Impl {
    // Wait-free SeqLock mailbox (writers never block the real-time audio reader)
    std::atomic_flag       writerLock = ATOMIC_FLAG_INIT;
    std::atomic<uint32_t>  mailboxSeq{0u};
    AudioSceneState        mailboxData{};
    AudioSceneState        audioCachedState{};

    // Smoothed DSP parameters (exponential tau = 80 ms)
    float                  smWindMs      = 4.0f;
    float                  smGust01      = 0.35f;
    float                  smFire01      = 0.0f;
    float                  smWater01     = 0.0f;
    float                  smUnderwater  = 0.0f;
    float                  smOutGain     = 0.0f; // Starts at 0.0 for click-free fade-in!

    // Dual-bus SoundscapeSynth for click-free 80 ms equal-power environment crossfading
    AcousticEnvironment    activeEnv     = AcousticEnvironment::SnowDampedPineForest;
    AcousticEnvironment    prevEnv       = AcousticEnvironment::SnowDampedPineForest;
    float                  envXfade01    = 1.0f; // 1.0 = fully on activeBus, <1.0 = crossfading from prevBus
    SoundscapeSynth        busA{48000u, 202645u};
    SoundscapeSynth        busB{48000u, 202645u ^ 0x5A3C1E7Bu};
    bool                   activeIsA     = true;
    std::vector<float>     scratchA;
    std::vector<float>     scratchB;

    // Lock-free SPSC stereo ring buffer for NullSink capture
    std::vector<float>     ringStereo;
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
    alignas(64) std::atomic<uint64_t> ringWriteFrames{0ull};
    alignas(64) std::atomic<uint64_t> ringReadFrames{0ull};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

    // Lifecycle & telemetry atomics
    std::atomic<bool>      running{false};
    std::atomic<bool>      stopRequested{false};
    std::atomic<bool>      deviceActive{false};
    AudioBackendMode       mode       = AudioBackendMode::NullSink;
    uint32_t               sampleRate = 48000u;
    uint32_t               seed       = 202645u;
    std::thread            worker;
#ifdef _WIN32
    HANDLE                 stopEvent  = nullptr;
#endif

    std::atomic<uint64_t>  framesRendered{0ull};
    std::atomic<uint64_t>  callbacksRun{0ull};
    std::atomic<uint64_t>  ringOverruns{0ull};
    std::atomic<uint64_t>  envTransitions{0ull};
    std::atomic<uint32_t>  lastPeakBits{0u};
    std::atomic<uint32_t>  lastRmsBits{0u};
    WasapiDeviceInfo       lastWasapiInfo{};

    Impl() {
        ringStereo.assign(size_t(kNullRingFramesPow2) * 2u, 0.0f);
        scratchA.reserve(2048u);
        scratchB.reserve(2048u);
    }

    ~Impl() {
#ifdef _WIN32
        if (stopEvent) {
            CloseHandle(stopEvent);
            stopEvent = nullptr;
        }
#endif
    }

    void WriteMailbox(const AudioSceneState& s) noexcept {
        const AudioSceneState clamped = ClampSceneState(s);
        while (writerLock.test_and_set(std::memory_order_acquire)) {
            // Writer-vs-writer contention is extremely rare; audio thread never touches writerLock.
        }
        mailboxSeq.fetch_add(1u, std::memory_order_acq_rel); // odd = writing
        mailboxData = clamped;
        mailboxSeq.fetch_add(1u, std::memory_order_release); // even = clean
        writerLock.clear(std::memory_order_release);
    }

    AudioSceneState ReadMailboxWaitFree() noexcept {
        const uint32_t s0 = mailboxSeq.load(std::memory_order_acquire);
        if ((s0 & 1u) != 0u) {
            return audioCachedState; // Writer mid-update -> keep last clean snapshot (zero blocking!)
        }
        const AudioSceneState snap = mailboxData;
        const uint32_t s1 = mailboxSeq.load(std::memory_order_acquire);
        if (s0 == s1) {
            audioCachedState = snap;
        }
        return audioCachedState;
    }

    void ResetDspState(uint32_t fs, uint32_t rngSeed) {
        sampleRate = std::clamp(fs, 16000u, 192000u);
        seed       = rngSeed;
        busA.SetSampleRate(sampleRate);
        busB.SetSampleRate(sampleRate);
        busA.Reset(seed);
        busB.Reset(seed ^ 0x5A3C1E7Bu);
        activeIsA = true;

        const AudioSceneState st = ReadMailboxWaitFree();
        activeEnv    = (st.underwater01 > 0.5f) ? AcousticEnvironment::Underwater : st.environment;
        prevEnv      = activeEnv;
        envXfade01   = 1.0f;
        smWindMs     = BeaufortToWindMs(st.beaufortScale);
        smGust01     = ClampF(st.beaufortScale / 12.0f, 0.1f, 1.0f);
        smFire01     = st.fireIntensity01;
        smWater01    = std::max(st.waterIntensity01, st.rain01 * 0.75f);
        smUnderwater = st.underwater01;
        smOutGain    = 0.0f; // Always fade smoothly from 0 on start (zero startup pop!)
    }

    // Renders `frameCount` click-free interleaved stereo frames using 64-frame ramped sub-blocks
    // + dual-bus 80 ms equal-power environment crossfading.
    void RenderStereoDsp(float* outStereo, size_t frameCount) {
        if (!outStereo || frameCount == 0u) return;

        const AudioSceneState target = ReadMailboxWaitFree();
        const AcousticEnvironment targetEnv = (target.underwater01 > 0.5f)
                                                ? AcousticEnvironment::Underwater
                                                : target.environment;

        if (targetEnv != activeEnv) {
            prevEnv    = activeEnv;
            activeEnv  = targetEnv;
            activeIsA  = !activeIsA;
            envXfade01 = 0.0f; // Begin smooth 80 ms raised-cosine crossfade!
            envTransitions.fetch_add(1ull, std::memory_order_relaxed);
        }

        // Convert high-level scene parameters to physical DSP targets
        const float targetWindMs = BeaufortToWindMs(target.beaufortScale) * (1.0f - 0.30f * target.snow01);
        const float targetGust   = ClampF(0.20f + 0.80f * (target.beaufortScale / 12.0f), 0.10f, 1.0f);
        // Fire inverse-distance attenuation (~70 Skyrim units per meter; full intensity <= 140u)
        const float fireDistM    = std::max(1.0f, target.fireDistanceU / 70.0f);
        const float fireAtten    = ClampF(2.0f / std::max(2.0f, fireDistM), 0.0f, 1.0f);
        const float targetFire   = target.fireIntensity01 * fireAtten;
        const float targetWater  = ClampF(std::max(target.waterIntensity01, target.rain01 * 0.80f), 0.0f, 1.0f);
        const float targetUw     = target.underwater01;
        const float targetGain   = target.muted ? 0.0f : ClampF(target.masterGain, 0.0f, kMaxMasterGain);

        const float fs = float(sampleRate);
        const float sampleAlpha = 1.0f - std::exp(-1.0f / (kRampTauSeconds * fs));
        const float xfadeStep   = 1.0f / std::max(1.0f, kRampTauSeconds * fs);

        constexpr size_t kSubBlock = 64u;
        float blockPeak = 0.0f;
        double blockSqSum = 0.0;

        size_t offset = 0u;
        while (offset < frameCount) {
            const size_t n = std::min(kSubBlock, frameCount - offset);
            const float blockAlpha = 1.0f - std::exp(-float(n) / (kRampTauSeconds * fs));

            smWindMs     += (targetWindMs - smWindMs) * blockAlpha;
            smGust01     += (targetGust   - smGust01) * blockAlpha;
            smFire01     += (targetFire   - smFire01) * blockAlpha;
            smWater01    += (targetWater  - smWater01) * blockAlpha;
            smUnderwater += (targetUw     - smUnderwater) * blockAlpha;

            SoundscapeParams pActive{};
            pActive.environment      = activeEnv;
            pActive.windSpeedMs      = smWindMs;
            pActive.windGust01       = smGust01;
            pActive.fireIntensity01  = smFire01;
            pActive.waterFlow01      = smWater01;
            pActive.underwaterDepthM = (activeEnv == AcousticEnvironment::Underwater)
                                         ? std::max(0.25f, smUnderwater * 2.0f)
                                         : (smUnderwater * 0.45f);
            pActive.masterGain       = 1.0f; // Final smoothed masterGain applied per-sample below
            pActive.enableReverb     = true;

            scratchA.resize(n * 2u);
            SoundscapeSynth& curSynth  = activeIsA ? busA : busB;
            SoundscapeSynth& prevSynth = activeIsA ? busB : busA;
            curSynth.SynthesizeStereo(pActive, scratchA.data(), n);

            const bool crossfading = (envXfade01 < 0.9999f);
            if (crossfading) {
                SoundscapeParams pPrev = pActive;
                pPrev.environment      = prevEnv;
                pPrev.underwaterDepthM = (prevEnv == AcousticEnvironment::Underwater) ? 1.0f : 0.0f;
                scratchB.resize(n * 2u);
                prevSynth.SynthesizeStereo(pPrev, scratchB.data(), n);
            }

            float* dst = outStereo + offset * 2u;
            for (size_t i = 0; i < n; ++i) {
                smOutGain += (targetGain - smOutGain) * sampleAlpha;

                float l = scratchA[i * 2u + 0u];
                float r = scratchA[i * 2u + 1u];
                if (crossfading) {
                    envXfade01 = std::min(1.0f, envXfade01 + xfadeStep);
                    // Smooth raised-cosine equal-power fade (C1 continuous derivative at endpoints -> zero click!)
                    const float angle = envXfade01 * (0.5f * kPiF);
                    const float wNew  = std::sin(angle);
                    const float wOld  = std::cos(angle);
                    l = l * wNew + scratchB[i * 2u + 0u] * wOld;
                    r = r * wNew + scratchB[i * 2u + 1u] * wOld;
                }

                // Scale by smoothed gain + gentle soft-clipper
                l = SanitizeSample(std::tanh(l * smOutGain * 1.4f));
                r = SanitizeSample(std::tanh(r * smOutGain * 1.4f));

                dst[i * 2u + 0u] = l;
                dst[i * 2u + 1u] = r;

                blockPeak = std::max(blockPeak, std::max(std::fabs(l), std::fabs(r)));
                blockSqSum += double(l) * double(l) + double(r) * double(r);
            }

            offset += n;
        }

        framesRendered.fetch_add(frameCount, std::memory_order_relaxed);
        callbacksRun.fetch_add(1ull, std::memory_order_relaxed);

        const float rms = float(std::sqrt(blockSqSum / double(frameCount * 2u)));
        uint32_t peakBits = 0u, rmsBits = 0u;
        std::memcpy(&peakBits, &blockPeak, 4u);
        std::memcpy(&rmsBits,  &rms,       4u);
        lastPeakBits.store(peakBits, std::memory_order_relaxed);
        lastRmsBits.store(rmsBits,   std::memory_order_relaxed);
    }

    void PushToNullRing(const float* stereo, size_t frameCount) noexcept {
        uint64_t w = ringWriteFrames.load(std::memory_order_relaxed);
        const uint64_t r = ringReadFrames.load(std::memory_order_acquire);
        const uint64_t freeFrames = (w >= r && (w - r) <= kNullRingFramesPow2)
                                      ? (kNullRingFramesPow2 - (w - r))
                                      : 0ull;
        const size_t toWrite = static_cast<size_t>(std::min<uint64_t>(frameCount, freeFrames));
        if (toWrite < frameCount) {
            ringOverruns.fetch_add(frameCount - toWrite, std::memory_order_relaxed);
        }
        for (size_t i = 0; i < toWrite; ++i) {
            const size_t idx = size_t((w + i) & kNullRingMask) * 2u;
            ringStereo[idx + 0u] = stereo[i * 2u + 0u];
            ringStereo[idx + 1u] = stereo[i * 2u + 1u];
        }
        ringWriteFrames.store(w + toWrite, std::memory_order_release);
    }

    void RunNullSinkWorker() {
        deviceActive.store(true, std::memory_order_release);
        constexpr size_t kChunkFrames = 480u; // 10 ms @ 48 kHz
        std::vector<float> buf(kChunkFrames * 2u, 0.0f);

        while (!stopRequested.load(std::memory_order_acquire)) {
            RenderStereoDsp(buf.data(), kChunkFrames);
            PushToNullRing(buf.data(), kChunkFrames);

#ifdef _WIN32
            if (stopEvent) {
                if (WaitForSingleObject(stopEvent, 4u) == WAIT_OBJECT_0) break;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
#endif
        }
        deviceActive.store(false, std::memory_order_release);
    }

    void RunWasapiWorker() {
#ifndef _WIN32
        RunNullSinkWorker();
#else
        DynOle32 ole;
        if (!ole.Load()) {
            lastWasapiInfo.available   = false;
            lastWasapiInfo.lastHresult = E_FAIL;
            deviceActive.store(false, std::memory_order_release);
            return;
        }

        const HRESULT hrCom = ole.coInit(nullptr, COINIT_MULTITHREADED);
        const bool needUninit = SUCCEEDED(hrCom);

        DynAvrt avrt;
        avrt.Load();
        DWORD mmcssTaskIdx = 0;
        HANDLE mmcssHandle = avrt.setChars ? avrt.setChars(L"Pro Audio", &mmcssTaskIdx) : nullptr;

        IMMDeviceEnumerator* enumerator = nullptr;
        IMMDevice*           device     = nullptr;
        IAudioClient*        client     = nullptr;
        IAudioRenderClient*  renderCli  = nullptr;
        WAVEFORMATEX*        mixFmt     = nullptr;
        HANDLE               bufEvent   = nullptr;

        auto cleanup = [&]() {
            if (client) {
                client->Stop();
                client->Release();
            }
            if (renderCli)  renderCli->Release();
            if (mixFmt)     ole.coFree(mixFmt);
            if (device)     device->Release();
            if (enumerator) enumerator->Release();
            if (bufEvent)   CloseHandle(bufEvent);
            if (mmcssHandle && avrt.revert) avrt.revert(mmcssHandle);
            if (needUninit) ole.coUninit();
            deviceActive.store(false, std::memory_order_release);
        };

        HRESULT hr = ole.coCreate(kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                  kIidIMMDeviceEnumerator, reinterpret_cast<void**>(&enumerator));
        if (FAILED(hr) || !enumerator) {
            lastWasapiInfo.available   = false;
            lastWasapiInfo.lastHresult = hr;
            cleanup();
            return;
        }

        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(hr) || !device) {
            lastWasapiInfo.available   = false;
            lastWasapiInfo.lastHresult = hr;
            cleanup();
            return;
        }

        hr = device->Activate(kIidIAudioClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client));
        if (FAILED(hr) || !client) {
            lastWasapiInfo.available   = false;
            lastWasapiInfo.lastHresult = hr;
            cleanup();
            return;
        }

        hr = client->GetMixFormat(&mixFmt);
        if (FAILED(hr) || !mixFmt) {
            lastWasapiInfo.available   = false;
            lastWasapiInfo.lastHresult = hr;
            cleanup();
            return;
        }

        const bool isFloat32 = IsWaveFormatFloat(mixFmt);
        const bool isPcm16   = !isFloat32 && IsWaveFormatPcm16(mixFmt);
        lastWasapiInfo.available     = true;
        lastWasapiInfo.mixSampleRate = mixFmt->nSamplesPerSec;
        lastWasapiInfo.mixChannels   = mixFmt->nChannels;
        lastWasapiInfo.bitsPerSample = mixFmt->wBitsPerSample;
        lastWasapiInfo.isFloat       = isFloat32;
        lastWasapiInfo.lastHresult   = S_OK;

        if (!isFloat32 && !isPcm16) {
            lastWasapiInfo.available   = false;
            lastWasapiInfo.lastHresult = AUDCLNT_E_UNSUPPORTED_FORMAT;
            cleanup();
            return;
        }

        // Match device mix sample rate cleanly for zero SRC artifacts
        if (mixFmt->nSamplesPerSec != sampleRate) {
            ResetDspState(mixFmt->nSamplesPerSec, seed);
        }

        // 20 ms shared-mode event-driven buffer (200,000 * 100ns units)
        constexpr REFERENCE_TIME kBufferDuration = 200000;
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                kBufferDuration, 0, mixFmt, nullptr);
        if (FAILED(hr)) {
            lastWasapiInfo.available   = false;
            lastWasapiInfo.lastHresult = hr;
            cleanup();
            return;
        }

        bufEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!bufEvent) {
            cleanup();
            return;
        }
        hr = client->SetEventHandle(bufEvent);
        if (FAILED(hr)) {
            lastWasapiInfo.lastHresult = hr;
            cleanup();
            return;
        }

        UINT32 bufferFrames = 0u;
        hr = client->GetBufferSize(&bufferFrames);
        if (FAILED(hr) || bufferFrames == 0u) {
            cleanup();
            return;
        }

        hr = client->GetService(kIidIAudioRenderClient, reinterpret_cast<void**>(&renderCli));
        if (FAILED(hr) || !renderCli) {
            cleanup();
            return;
        }

        std::vector<float> stereoScratch(size_t(bufferFrames) * 2u, 0.0f);
        const uint16_t outCh = std::max<uint16_t>(1u, mixFmt->nChannels);

        auto writeFramesToDevice = [&](UINT32 framesToWrite) {
            if (framesToWrite == 0u) return;
            BYTE* dstBytes = nullptr;
            if (FAILED(renderCli->GetBuffer(framesToWrite, &dstBytes)) || !dstBytes) return;

            if (stereoScratch.size() < size_t(framesToWrite) * 2u) {
                stereoScratch.resize(size_t(framesToWrite) * 2u);
            }
            RenderStereoDsp(stereoScratch.data(), framesToWrite);

            if (isFloat32) {
                auto* dstF = reinterpret_cast<float*>(dstBytes);
                for (UINT32 f = 0; f < framesToWrite; ++f) {
                    const float l = stereoScratch[size_t(f) * 2u + 0u];
                    const float r = stereoScratch[size_t(f) * 2u + 1u];
                    if (outCh == 1u) {
                        dstF[f] = 0.5f * (l + r);
                    } else {
                        dstF[size_t(f) * outCh + 0u] = l;
                        dstF[size_t(f) * outCh + 1u] = r;
                        for (uint16_t c = 2u; c < outCh; ++c) dstF[size_t(f) * outCh + c] = 0.0f;
                    }
                }
            } else {
                auto* dstI = reinterpret_cast<int16_t*>(dstBytes);
                for (UINT32 f = 0; f < framesToWrite; ++f) {
                    const float l = stereoScratch[size_t(f) * 2u + 0u];
                    const float r = stereoScratch[size_t(f) * 2u + 1u];
                    const auto toI16 = [](float x) -> int16_t {
                        return static_cast<int16_t>(std::clamp(static_cast<int32_t>(std::lrint(x * 32767.0f)), -32768, 32767));
                    };
                    if (outCh == 1u) {
                        dstI[f] = toI16(0.5f * (l + r));
                    } else {
                        dstI[size_t(f) * outCh + 0u] = toI16(l);
                        dstI[size_t(f) * outCh + 1u] = toI16(r);
                        for (uint16_t c = 2u; c < outCh; ++c) dstI[size_t(f) * outCh + c] = 0;
                    }
                }
            }
            renderCli->ReleaseBuffer(framesToWrite, 0);
        };

        // Pre-fill initial buffer & start WASAPI stream
        writeFramesToDevice(bufferFrames);
        hr = client->Start();
        if (FAILED(hr)) {
            lastWasapiInfo.lastHresult = hr;
            cleanup();
            return;
        }
        deviceActive.store(true, std::memory_order_release);

        HANDLE waits[2] = {stopEvent, bufEvent};
        while (!stopRequested.load(std::memory_order_acquire)) {
            const DWORD wr = WaitForMultipleObjects(2u, waits, FALSE, 40u);
            if (wr == WAIT_OBJECT_0 || stopRequested.load(std::memory_order_acquire)) {
                break;
            }
            UINT32 padding = 0u;
            if (FAILED(client->GetCurrentPadding(&padding))) {
                break; // Device unplugged / invalidated -> exit cleanly without crashing
            }
            if (padding < bufferFrames) {
                writeFramesToDevice(bufferFrames - padding);
            }
        }

        cleanup();
#endif
    }
};

WasapiDeviceInfo ProbeWasapiDefaultDevice() {
    WasapiDeviceInfo info{};
#ifdef _WIN32
    DynOle32 ole;
    if (!ole.Load()) {
        info.lastHresult = E_FAIL;
        return info;
    }
    const HRESULT hrCom = ole.coInit(nullptr, COINIT_MULTITHREADED);
    const bool needUninit = SUCCEEDED(hrCom);

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice*           device     = nullptr;
    IAudioClient*        client     = nullptr;
    WAVEFORMATEX*        mixFmt     = nullptr;
    LPWSTR               devId      = nullptr;

    HRESULT hr = ole.coCreate(kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL,
                              kIidIMMDeviceEnumerator, reinterpret_cast<void**>(&enumerator));
    if (SUCCEEDED(hr) && enumerator) {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (SUCCEEDED(hr) && device) {
            if (SUCCEEDED(device->GetId(&devId)) && devId) {
                info.deviceName = WideToUtf8(devId);
                ole.coFree(devId);
            }
            hr = device->Activate(kIidIAudioClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client));
            if (SUCCEEDED(hr) && client) {
                hr = client->GetMixFormat(&mixFmt);
                if (SUCCEEDED(hr) && mixFmt) {
                    info.available     = true;
                    info.mixSampleRate = mixFmt->nSamplesPerSec;
                    info.mixChannels   = mixFmt->nChannels;
                    info.bitsPerSample = mixFmt->wBitsPerSample;
                    info.isFloat       = IsWaveFormatFloat(mixFmt);
                    ole.coFree(mixFmt);
                }
                client->Release();
            }
            device->Release();
        }
        enumerator->Release();
    }
    info.lastHresult = hr;
    if (needUninit) ole.coUninit();
#endif
    return info;
}

RawAudioOutput::RawAudioOutput() : m_impl(std::make_unique<Impl>()) {}

RawAudioOutput::~RawAudioOutput() {
    Stop();
}

bool RawAudioOutput::Start(AudioBackendMode mode, uint32_t preferredSampleRate, uint32_t seed) {
    Stop();

    m_impl->mode = mode;
    m_impl->stopRequested.store(false, std::memory_order_release);
    m_impl->deviceActive.store(false, std::memory_order_release);
    m_impl->ringWriteFrames.store(0ull, std::memory_order_release);
    m_impl->ringReadFrames.store(0ull, std::memory_order_release);
    m_impl->framesRendered.store(0ull, std::memory_order_relaxed);
    m_impl->callbacksRun.store(0ull, std::memory_order_relaxed);
    m_impl->ringOverruns.store(0ull, std::memory_order_relaxed);
    m_impl->envTransitions.store(0ull, std::memory_order_relaxed);
    m_impl->ResetDspState(preferredSampleRate, seed);

#ifdef _WIN32
    if (!m_impl->stopEvent) {
        m_impl->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    } else {
        ResetEvent(m_impl->stopEvent);
    }
#endif

    m_impl->running.store(true, std::memory_order_release);
    m_impl->worker = std::thread([this, mode]() {
        if (mode == AudioBackendMode::WasapiShared) {
            m_impl->RunWasapiWorker();
        } else {
            m_impl->RunNullSinkWorker();
        }
        m_impl->running.store(false, std::memory_order_release);
    });
    return true;
}

void RawAudioOutput::Stop() {
    if (!m_impl) return;
    m_impl->stopRequested.store(true, std::memory_order_release);
#ifdef _WIN32
    if (m_impl->stopEvent) {
        SetEvent(m_impl->stopEvent);
    }
#endif
    if (m_impl->worker.joinable()) {
        m_impl->worker.join();
    }
    m_impl->running.store(false, std::memory_order_release);
    m_impl->deviceActive.store(false, std::memory_order_release);
}

bool RawAudioOutput::IsRunning() const noexcept {
    return m_impl && m_impl->running.load(std::memory_order_acquire);
}

void RawAudioOutput::UpdateSceneState(const AudioSceneState& state) noexcept {
    if (m_impl) m_impl->WriteMailbox(state);
}

AudioSceneState RawAudioOutput::GetSceneState() const noexcept {
    return m_impl ? m_impl->ReadMailboxWaitFree() : AudioSceneState{};
}

size_t RawAudioOutput::NullSinkAvailableFrames() const noexcept {
    if (!m_impl) return 0u;
    const uint64_t w = m_impl->ringWriteFrames.load(std::memory_order_acquire);
    const uint64_t r = m_impl->ringReadFrames.load(std::memory_order_relaxed);
    return (w >= r) ? static_cast<size_t>(std::min<uint64_t>(w - r, kNullRingFramesPow2)) : 0u;
}

size_t RawAudioOutput::ReadNullSinkStereo(float* outStereoInterleaved, size_t maxFrames) noexcept {
    if (!m_impl || !outStereoInterleaved || maxFrames == 0u) return 0u;
    const uint64_t w = m_impl->ringWriteFrames.load(std::memory_order_acquire);
    const uint64_t r = m_impl->ringReadFrames.load(std::memory_order_relaxed);
    if (w <= r) return 0u;
    const size_t avail = static_cast<size_t>(std::min<uint64_t>(w - r, kNullRingFramesPow2));
    const size_t count = std::min(maxFrames, avail);
    for (size_t i = 0; i < count; ++i) {
        const size_t idx = size_t((r + i) & kNullRingMask) * 2u;
        outStereoInterleaved[i * 2u + 0u] = m_impl->ringStereo[idx + 0u];
        outStereoInterleaved[i * 2u + 1u] = m_impl->ringStereo[idx + 1u];
    }
    m_impl->ringReadFrames.store(r + count, std::memory_order_release);
    return count;
}

void RawAudioOutput::RenderOfflineFrames(float* outStereoInterleaved, size_t frameCount) {
    if (m_impl) m_impl->RenderStereoDsp(outStereoInterleaved, frameCount);
}

AudioOutputStats RawAudioOutput::GetStats() const noexcept {
    AudioOutputStats st{};
    if (!m_impl) return st;
    st.running        = m_impl->running.load(std::memory_order_acquire);
    st.deviceActive   = m_impl->deviceActive.load(std::memory_order_acquire);
    st.mode           = m_impl->mode;
    st.sampleRate     = m_impl->sampleRate;
    st.framesRendered = m_impl->framesRendered.load(std::memory_order_relaxed);
    st.callbacksRun   = m_impl->callbacksRun.load(std::memory_order_relaxed);
    st.ringOverruns   = m_impl->ringOverruns.load(std::memory_order_relaxed);
    st.envTransitions = m_impl->envTransitions.load(std::memory_order_relaxed);
    const uint32_t pb = m_impl->lastPeakBits.load(std::memory_order_relaxed);
    const uint32_t rb = m_impl->lastRmsBits.load(std::memory_order_relaxed);
    std::memcpy(&st.lastPeakAbs, &pb, 4u);
    std::memcpy(&st.lastRms,     &rb, 4u);
    return st;
}

WasapiDeviceInfo RawAudioOutput::GetLastWasapiInfo() const noexcept {
    return m_impl ? m_impl->lastWasapiInfo : WasapiDeviceInfo{};
}

}  // namespace raw::audio
