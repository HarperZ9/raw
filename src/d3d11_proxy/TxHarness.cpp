// TxHarness: the swap-chain wrapper of the translate mode (TxMode.h). It forwards every call, and
// on Present it writes the harness heartbeat (presents, world frames, loading screens), and runs
// the harness's capture requests: hold N frames, time them, write the backbuffer as a BMP and one
// JSON line. A world frame is one that drew more than 100,000 primitives, counted by a pipeline
// statistics query read three frames late. Original work, MIT.
#include "TxMode.h"
#include "BridgeHookState.h"
#include "ProxyAPI.h"
#include "ProxyLog.h"
#include "Translate.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace SB::Proxy
{
namespace
{
std::string GameFile(const char* name)
{
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string p(path);
    return p.substr(0, p.rfind('\\') + 1) + name;
}

class HarnessSwapChain final : public IDXGISwapChain
{
public:
    HarnessSwapChain(IDXGISwapChain* inner, ID3D11Device* dev) : in_(inner), dev_(dev)
    {
        dev_->AddRef();
        dev_->GetImmediateContext(&ctx_);
        D3D11_QUERY_DESC q{D3D11_QUERY_PIPELINE_STATISTICS, 0};
        for (auto& s : stats_) dev_->CreateQuery(&q, &s);
        QueryPerformanceFrequency(&qpf_);
    }
    ~HarnessSwapChain()
    {
        for (auto& s : stats_) if (s) s->Release();
        if (ctx_) ctx_->Release();
        dev_->Release();
        in_->Release();
    }

    STDMETHODIMP QueryInterface(REFIID r, void** o) override
    {
        if (!o) return E_POINTER;
        if (r == __uuidof(IUnknown) || r == __uuidof(IDXGIObject) || r == __uuidof(IDXGIDeviceSubObject) || r == __uuidof(IDXGISwapChain)) {
            *o = static_cast<IDXGISwapChain*>(this);
            AddRef();
            return S_OK;
        }
        const HRESULT hr = in_->QueryInterface(r, o);
        Log("[Tx] swap chain QueryInterface {%08lX-%04hX-%04hX-...}: 0x%08X%s", r.Data1, r.Data2, r.Data3, (unsigned)hr,
            SUCCEEDED(hr) ? " (the inner chain's own object: Present through it bypasses the harness)" : "");
        return hr;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&rc_); }
    STDMETHODIMP_(ULONG) Release() override { ULONG n = InterlockedDecrement(&rc_); if (!n) delete this; return n; }
    STDMETHODIMP SetPrivateData(REFGUID g, UINT s, const void* d) override { return in_->SetPrivateData(g, s, d); }
    STDMETHODIMP SetPrivateDataInterface(REFGUID g, const IUnknown* p) override { return in_->SetPrivateDataInterface(g, p); }
    STDMETHODIMP GetPrivateData(REFGUID g, UINT* s, void* d) override { return in_->GetPrivateData(g, s, d); }
    STDMETHODIMP GetParent(REFIID r, void** o) override { return in_->GetParent(r, o); }
    STDMETHODIMP GetDevice(REFIID r, void** o) override { return in_->GetDevice(r, o); }
    STDMETHODIMP Present(UINT sync, UINT flags) override
    {
        BeforePresent();
        const HRESULT hr = in_->Present(sync, flags);
        if (FAILED(hr) && fails_++ < 8) Log("[Tx] Present failed 0x%08X", (unsigned)hr);
        return hr;
    }
    STDMETHODIMP GetBuffer(UINT i, REFIID r, void** o) override { return in_->GetBuffer(i, r, o); }
    STDMETHODIMP SetFullscreenState(BOOL f, IDXGIOutput* t) override { return in_->SetFullscreenState(f, t); }
    STDMETHODIMP GetFullscreenState(BOOL* f, IDXGIOutput** t) override { return in_->GetFullscreenState(f, t); }
    STDMETHODIMP GetDesc(DXGI_SWAP_CHAIN_DESC* d) override { return in_->GetDesc(d); }
    STDMETHODIMP ResizeBuffers(UINT c, UINT w, UINT h, DXGI_FORMAT f, UINT fl) override
    {
        Log("[Tx] ResizeBuffers %u %ux%u fmt %d flags 0x%X", c, w, h, (int)f, fl);
        return in_->ResizeBuffers(c, w, h, f, fl);
    }
    STDMETHODIMP ResizeTarget(const DXGI_MODE_DESC* m) override { return in_->ResizeTarget(m); }
    STDMETHODIMP GetContainingOutput(IDXGIOutput** o) override { return in_->GetContainingOutput(o); }
    STDMETHODIMP GetFrameStatistics(DXGI_FRAME_STATISTICS* s) override { return in_->GetFrameStatistics(s); }
    STDMETHODIMP GetLastPresentCount(UINT* c) override { return in_->GetLastPresentCount(c); }

private:
    void CountPrimitives()
    {
        if (!stats_[0]) return;
        const uint64_t k = presents_;
        if (k > 0) ctx_->End(stats_[(k - 1) % 4]);
        if (k >= 3) {
            D3D11_QUERY_DATA_PIPELINE_STATISTICS ps{};
            if (ctx_->GetData(stats_[(k - 3) % 4], &ps, sizeof ps, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) lastPrims_ = ps.IAPrimitives;
        }
        ctx_->Begin(stats_[k % 4]);
    }

    void Heartbeat()
    {
        const bool world = lastPrims_ > 100000;
        world_ += world;
        if (!world) ++gap_;
        else { if (gap_ >= 10) ++loads_; gap_ = 0; }
        if (presents_ % 30 != 0) return;
        FILE* f = nullptr;
        if (fopen_s(&f, GameFile("raw_harness_status.txt").c_str(), "w") == 0 && f) {
            // cell: the scene's size in primitives on a half-octave scale. A coc into a cell still in
            // memory shows no loading screen; the primitive count still changes (Riverwood about
            // 3.4 million, the Bannered Mare about 130,000).
            const int cell = lastPrims_ ? int(std::log2(double(lastPrims_)) * 2.0) : 0;
            fprintf(f, "presents=%llu world=%llu loads=%llu cell=%d active=%d prims=%llu\n", (unsigned long long)presents_,
                    (unsigned long long)world_, (unsigned long long)loads_, cell, active_ ? 1 : 0, (unsigned long long)lastPrims_);
            fclose(f);
        }
        if (presents_ % 600 == 0) {
            if (TxMode_Get() == 1) {
                const raw::tx::Stats s = raw::tx::GetStats(dev_);
                Log("[Tx] present %llu: %llu primitives a frame, draws %llu, submissions %llu, PSOs %llu, root signatures %llu, "
                    "barriers %llu, unsupported calls %llu, debug errors %llu", (unsigned long long)presents_, (unsigned long long)lastPrims_,
                    (unsigned long long)s.draws, (unsigned long long)s.submissions, (unsigned long long)s.psoCreated,
                    (unsigned long long)s.rootSignatures, (unsigned long long)s.barriers, (unsigned long long)s.unsupportedCalls,
                    (unsigned long long)s.debugErrors);
            } else {
                Log("[Tx] present %llu (native D3D11): %llu primitives a frame", (unsigned long long)presents_, (unsigned long long)lastPrims_);
            }
        }
    }

    void Poll()
    {
        if (active_ || presents_ % 20 != 0) return;
        const std::string go = GameFile("raw_harness_go.txt");
        FILE* f = nullptr;
        if (fopen_s(&f, go.c_str(), "r") != 0 || !f) return;
        char line[1024];
        scene_.clear(); key_.clear(); dir_.clear(); hold_ = 40;
        while (fgets(line, sizeof line, f)) {
            std::string l(line);
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            const size_t eq = l.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = l.substr(0, eq), v = l.substr(eq + 1);
            if (k == "scene") scene_ = v; else if (k == "keyframe") key_ = v; else if (k == "dir") dir_ = v;
            else if (k == "hold") hold_ = std::atoi(v.c_str());
        }
        fclose(f);
        DeleteFileA(go.c_str());
        active_ = !dir_.empty();
        held_ = 0; msSum_ = 0; primSum_ = 0;
        draws0_ = TxMode_Get() == 1 ? raw::tx::GetStats(dev_).draws : 0;
        Log("[Tx] harness go: %s/%s, hold %d", scene_.c_str(), key_.c_str(), hold_);
    }

    void Capture(double frameMs)
    {
        if (!active_) return;
        msSum_ += frameMs; primSum_ += lastPrims_;
        if (++held_ < uint32_t(hold_ > 0 ? hold_ : 40)) return;
        const char* mode = TxMode_Get() == 1 ? "tx" : "native";
        const std::string base = dir_ + "\\" + scene_ + "_" + key_ + "_" + mode;
        hook::CaptureBackbuffer(base + ".bmp");
        raw::tx::Stats s{};
        if (TxMode_Get() == 1) s = raw::tx::GetStats(dev_);
        FILE* f = nullptr;
        if (fopen_s(&f, (dir_ + "\\harness_results.jsonl").c_str(), "a") == 0 && f) {
            fprintf(f, "{\"scene\":\"%s\",\"keyframe\":\"%s\",\"step\":\"%s\",\"frames\":%u,\"frame_ms\":%.3f,\"primitives\":%.0f,"
                       "\"draws_per_frame\":%.1f,\"unsupported_calls\":%llu,\"debug_errors\":%llu,\"psos\":%llu,\"presents\":%llu}\n",
                    scene_.c_str(), key_.c_str(), mode, held_, msSum_ / held_, double(primSum_) / held_,
                    TxMode_Get() == 1 ? double(s.draws - draws0_) / held_ : 0.0, (unsigned long long)s.unsupportedCalls,
                    (unsigned long long)s.debugErrors, (unsigned long long)s.psoCreated, (unsigned long long)presents_);
            fclose(f);
        }
        active_ = false;
        FILE* d = nullptr;
        if (fopen_s(&d, GameFile("raw_harness_done.txt").c_str(), "w") == 0 && d) { fprintf(d, "%s ok\n", key_.c_str()); fclose(d); }
    }

    void BeforePresent()
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const double ms = last_.QuadPart ? double(now.QuadPart - last_.QuadPart) * 1000.0 / double(qpf_.QuadPart) : 0.0;
        last_ = now;
        ++presents_;
        if (presents_ <= 3 || presents_ == 100 || presents_ == 1000) Log("[Tx] present %llu", (unsigned long long)presents_);
        if (auto* pi = PG_GetProxyInterface()) pi->frameCount = uint32_t(presents_);
        CountPrimitives();
        Heartbeat();
        Poll();
        Capture(ms);
    }

    IDXGISwapChain* in_;
    ID3D11Device* dev_;
    ID3D11DeviceContext* ctx_ = nullptr;
    ID3D11Query* stats_[4] = {};
    volatile ULONG rc_ = 1;
    uint64_t presents_ = 0, world_ = 0, loads_ = 0, gap_ = 0, lastPrims_ = 0, draws0_ = 0, primSum_ = 0;
    LARGE_INTEGER qpf_{}, last_{};
    bool active_ = false;
    std::string scene_, key_, dir_;
    int hold_ = 40;
    uint32_t held_ = 0, fails_ = 0;
    double msSum_ = 0;
};
} // namespace

IDXGISwapChain* TxMode_WrapSwapChain(IDXGISwapChain* inner, ID3D11Device* dev)
{
    return inner && dev ? new HarnessSwapChain(inner, dev) : inner;
}

} // namespace SB::Proxy
