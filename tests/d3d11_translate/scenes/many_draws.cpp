// Scene 8: many small draws, each preceded by a constant-buffer update. The frame
// loop, the deferred-context threads and the timing live here; resources and the
// per-draw recording are in many_draws_record.cpp.
#include "many_draws.h"
#include <algorithm>
#include <thread>

namespace txtest {
using namespace manydraws;
namespace {

struct Worker {
    ComPtr<ID3D11DeviceContext> dc;
    CbPair cb;
    ComPtr<ID3D11CommandList> list;
    uint32_t begin = 0, end = 0;
    bool ok = true;
    HRESULT hr = S_OK;
};

struct State {
    Shared s;
    CbPair immCb;
    std::vector<Worker> workers;
    std::vector<std::thread> threads;
    ComPtr<ID3D11Query> done;
    LARGE_INTEGER freq{};
};

bool Setup(SceneCtx& c, State& st)
{
    if (!MakeShared(c, st.s)) return false;
    TX_REQ(kName, MakeCbPair(c.dev, false, st.immCb));
    const D3D11_QUERY_DESC qd = {D3D11_QUERY_EVENT, 0};
    TX_HR(kName, c.dev->CreateQuery(&qd, &st.done));
    QueryPerformanceFrequency(&st.freq);
    const uint32_t n = std::min(c.threads, 64u);
    st.workers.resize(n);
    st.threads.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        Worker& w = st.workers[i];
        TX_HR(kName, c.dev->CreateDeferredContext(0, &w.dc));
        TX_REQ(kName, MakeCbPair(c.dev, true, w.cb));
        w.begin = static_cast<uint32_t>(static_cast<uint64_t>(c.drawCount) * i / n);
        w.end = static_cast<uint32_t>(static_cast<uint64_t>(c.drawCount) * (i + 1) / n);
    }
    return true;
}

void RecordWorker(Worker* w, const Shared* s)
{
    w->ok = RecordRange(w->dc.Get(), *s, w->cb, w->begin, w->end, true);
    w->hr = w->dc->FinishCommandList(FALSE, w->list.ReleaseAndGetAddressOf());
}

bool RecordThreaded(SceneCtx& c, State& st)
{
    for (Worker& w : st.workers) st.threads.emplace_back(RecordWorker, &w, &st.s);
    for (std::thread& t : st.threads) t.join();
    st.threads.clear();
    bool ok = true;
    for (Worker& w : st.workers) {
        if (!w.ok || FAILED(w.hr) || !w.list) {
            ok = Fail(kName, "deferred record / FinishCommandList", w.hr);
            continue;
        }
        c.ctx->ExecuteCommandList(w.list.Get(), FALSE);
    }
    return ok;
}

double Ms(const LARGE_INTEGER& a, const LARGE_INTEGER& b, const LARGE_INTEGER& f)
{
    return static_cast<double>(b.QuadPart - a.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
}

bool Frame(SceneCtx& c, State& st)
{
    c.ctx->ClearRenderTargetView(st.s.rt.rtv.Get(), kClear);
    LARGE_INTEGER t0, t1, t2;
    QueryPerformanceCounter(&t0);
    bool ok = st.workers.empty() ? RecordRange(c.ctx, st.s, st.immCb, 0, c.drawCount, false)
                                 : RecordThreaded(c, st);
    c.ctx->End(st.done.Get());
    c.ctx->Flush();
    QueryPerformanceCounter(&t1);
    BOOL finished = FALSE;
    ok = WaitQuery(c.ctx, st.done.Get(), &finished, sizeof(finished)) && ok;
    QueryPerformanceCounter(&t2);
    for (Worker& w : st.workers) w.list.Reset();
    c.recordMs.push_back(Ms(t0, t1, st.freq));
    c.totalMs.push_back(Ms(t0, t2, st.freq));
    return ok;
}
}  // namespace

bool RunManyDraws(SceneCtx& c, Image& out)
{
    c.ctx->ClearState();
    State st;
    if (!Setup(c, st)) return false;
    const uint32_t frames = std::max(c.frames, 1u);
    c.recordMs.reserve(c.recordMs.size() + frames);
    c.totalMs.reserve(c.totalMs.size() + frames);
    for (uint32_t f = 0; f < frames; ++f)
        if (!Frame(c, st)) return Fail(kName, "frame");
    TX_REQ(kName, Readback(c, st.s.rt.tex.Get(), out));
    c.ctx->ClearState();
    return CheckImage(c, st.s, out);
}
}  // namespace txtest
