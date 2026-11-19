// Parallel translation, the application's side: segments opened, recorded and closed on the immediate
// context, and the queue of closed lists submitted in call order (tx_stream.h). Original work, MIT.
#include "tx_context.h"

namespace raw::tx {

void Context::SBuf(TBuffer* b)
{
    seg_->words.push_back(reinterpret_cast<uint64_t>(b));
    seg_->words.push_back(b && b->core.store == Store::Renamed ? b->core.chunk.va : 0);
    SHold(*seg_, b);
}

// The whole binding state, copied when a segment opens, with a hold on every object in it and the GPU
// address of every renamed buffer bound.
// A segment opened on a list whose D3D12 state is current starts lazy: the list is not split until the
// segment reaches minDraws_ draws (Commit). A run that ends before that is replayed on this context, into
// the same list (InlineReplay), so short runs between other calls cost no extra lists.
void Context::BeginSegment()
{
    Segment* s = nullptr;
    if (!freeSegs_.empty()) { s = freeSegs_.back(); freeSegs_.pop_back(); }
    else { s = new Segment; s->words.reserve(16384); s->held.reserve(4096); s->start = std::make_unique<State>(); }
    s->lazy = !stale_;
    s->startDirty = dirty_;
    *s->start = s_;
    auto buf = [&](TBuffer* b) {
        if (!b) return;
        SHold(*s, b);
        if (b->core.store == Store::Renamed) s->va.emplace_back(b, b->core.chunk.va);
    };
    for (StageState& st : s_.st) {
        if (st.sh.hold) { ++st.sh.hold->n; s->held.push_back(st.sh.hold); }   // bound: already pinned
        for (BufSlot& c : st.cb) buf(c.buf);
        for (uint32_t i = 0; i < st.srvHi; ++i) SHold(*s, st.srv[i]);
        for (TSampler* p : st.samp) SHold(*s, p);
    }
    SHold(*s, s_.layout);
    for (uint32_t i = 0; i < s_.vbHi; ++i) buf(s_.vb[i].buf);
    buf(s_.ib.buf);
    for (TRTV* v : s_.rtv) SHold(*s, v);
    SHold(*s, s_.dsv);
    for (TUAV* u : s_.uav) SHold(*s, u);
    for (TUAV* u : s_.csUav) SHold(*s, u);
    SHold(*s, s_.blend);
    SHold(*s, s_.ds);
    SHold(*s, s_.rs);
    seg_ = s;
}

void Context::Commit()
{
    if (rec_.cmdCount) {   // what the immediate context recorded so far runs before the segment
        rec_.cl->Close();
        Pending p;
        p.rec = std::make_unique<Recording>(std::move(rec_));
        pending_.push_back(std::move(p));
        rec_ = Recording{};
        rec_.tracker.faults = dev_->faults;
        rec_.tracker.EnableSlots();
        dev_->OpenRecording(rec_);
        BeginList();
    }
    seg_->lazy = false;
    stale_ = true;
}

void Context::InlineReplay(Segment* s)
{
    holds_.Suspend(true);   // the end state's objects are counted already; the segment holds the rest
    s_ = *s->start;
    dirty_ = s->startDirty;
    replay_ = true;
    override_.clear();
    for (const auto& [b, va] : s->va) override_[static_cast<TBuffer*>(b)].va = va;
    auto fix = [this](BufSlot& c) { c.chunk = c.buf ? Pin(c.buf) : nullptr; };
    for (StageState& st : s_.st) for (BufSlot& c : st.cb) fix(c);
    for (BufSlot& v : s_.vb) fix(v);
    fix(s_.ib);
    ReplayWords(this, *s);
    replay_ = false;
    for (StageState& st : s_.st) for (BufSlot& c : st.cb) fix(c);   // the live chunks again
    for (BufSlot& v : s_.vb) fix(v);
    fix(s_.ib);
    override_.clear();
    holds_.Suspend(false);
    ++sstats_.inlined;
    DropSegment(s);
}

bool Context::StreamCopy(ResCore* d, ResCore* s, bool& recorded)
{
    recorded = false;
    if (!pool_ || !d || !s || d->store != Store::Committed || s->store != Store::Committed) return false;
    if (seg_ || stale_) {
        if (!seg_) BeginSegment();
        recorded = true;
        ++seg_->copies;
    }
    return true;   // not recorded: straight to the immediate list, and the run goes on
}

bool Context::StreamDraw()
{
    ++runDraws_;
    if (openQueries_) { ++sstats_.queryDraws; return false; }
    if (!runMode_) {
        const bool longLast = runIndex_ < lastRuns_.size() && lastRuns_[runIndex_] >= minDraws_;
        runMode_ = longLast || stale_ || !predict_ ? 1 : 2;   // a run right after a segment keeps recording (no split)
        if (runMode_ == 2) ++sstats_.directRuns;
    }
    if (runMode_ == 2) return false;
    if (!seg_) BeginSegment();
    ++sstats_.draws;
    return true;
}

// The segment goes to a worker; the next draw opens another. The open list stays stale until EndStream.
void Context::CutSegment()
{
    Segment* s = seg_;
    if (!s) return;
    seg_ = nullptr;
    if (s->words.empty() || (!s->draws && !s->copies)) { DropSegment(s); return; }   // binds only: the shadow state holds them
    Pending p;
    p.seg = s;
    pending_.push_back(std::move(p));
    ++sstats_.segments;
    pool_->Push(s);
}

void Context::EndStream(const char* why)
{
    ++sstats_.ends;
    for (int k = 0; k < 8; ++k)
        if (sstats_.why[k] == why || !sstats_.why[k]) { sstats_.why[k] = why; ++sstats_.whyN[k]; break; }
    if (seg_ && seg_->lazy) { Segment* s = seg_; seg_ = nullptr; InlineReplay(s); return; }
    CutSegment();
    if (!stale_) return;
    stale_ = false;
    // The open list has run no draw since its BeginList: everything is set again on the next one.
    dirty_ = D_ALL;
    root_ = croot_ = nullptr;
    pso_ = nullptr;
    memset(cbVa_, 0xFF, sizeof(cbVa_));
    memset(cbBuf_, 0, sizeof(cbBuf_));
    for (auto& k : sampKey_) k = ~0ull;
}

void Context::DropSegment(Segment* s)
{
    for (ImmHold* h : s->held) --h->n;   // the pins go back at a later Drain
    s->words.clear(); s->va.clear(); s->held.clear(); s->out.reset();
    s->draws = 0; s->copies = 0; s->lazy = false; s->done.store(false, std::memory_order_relaxed);
    if (freeSegs_.size() < 256) freeSegs_.push_back(s); else delete s;
}

// Every closed list in call order, then the open one, as one submission. The workers finish first.
uint64_t Context::SubmitPending()
{
    std::vector<Recording*> recs;
    recs.reserve(pending_.size() + 1);
    LARGE_INTEGER t0, t1, f;
    QueryPerformanceCounter(&t0);
    for (Pending& p : pending_) {
        if (p.seg) { pool_->Wait(p.seg); recs.push_back(p.seg->out.get()); }
        else recs.push_back(p.rec.get());
    }
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&f);
    sstats_.waitUs += double(t1.QuadPart - t0.QuadPart) * 1e6 / double(f.QuadPart);
    const bool own = rec_.cmdCount != 0;
    if (own) { rec_.cl->Close(); recs.push_back(&rec_); }
    const uint64_t serial = dev_->Submit(recs.data(), int(recs.size()), true);
    for (Pending& p : pending_) {
        Recording* r = p.seg ? p.seg->out.get() : p.rec.get();
        dev_->RecycleList(*r);
        if (p.seg) DropSegment(p.seg);
    }
    pending_.clear();
    if (own) { dev_->OpenRecording(rec_); BeginList(); }
    return serial;
}

void Context::StreamStatsLog(uint64_t frames)
{
    if (!pool_ || !frames) return;
    const double n = double(frames);
    Log("workers %u: per frame %.1f segments to the workers, %.1f short runs replayed here, %.0f draws recorded, %.1f runs ended, "
        "%.3f ms waiting for the workers", pool_->Workers(), sstats_.segments / n, sstats_.inlined / n, sstats_.draws / n,
        sstats_.ends / n, sstats_.waitUs / n / 1000.0);
    char why[512] = {};
    int at = 0;
    for (int k = 0; k < 8 && sstats_.why[k]; ++k)
        at += snprintf(why + at, sizeof why - at, " %s %.1f", sstats_.why[k], sstats_.whyN[k] / n);
    Log("  runs ended by (per frame):%s; draws kept on the immediate list by an open query %.0f; runs sent straight to the list "
        "(short last frame) %.1f", why, sstats_.queryDraws / n, sstats_.directRuns / n);
    sstats_ = StreamStats{};
}

// ---- the pool ----

StreamPool::StreamPool(Device* dev, uint32_t workers) : dev_(dev)
{
    for (uint32_t i = 0; i < workers; ++i) ctx_.push_back(new Context(dev, true, true));
    for (uint32_t i = 0; i < workers; ++i) threads_.emplace_back([this, i] { Run(i); });
}

StreamPool::~StreamPool()
{
    { std::lock_guard<std::mutex> g(m_); stop_ = true; }
    cv_.notify_all();
    for (auto& t : threads_) t.join();
    for (Context* c : ctx_) delete c;
}

void StreamPool::Push(Segment* s)
{
    { std::lock_guard<std::mutex> g(m_); q_.push_back(s); }
    cv_.notify_one();
}

void StreamPool::Wait(Segment* s)
{
    if (s->done.load(std::memory_order_acquire)) return;
    std::unique_lock<std::mutex> g(m_);
    doneCv_.wait(g, [s] { return s->done.load(std::memory_order_acquire); });
}

void StreamPool::Run(uint32_t index)
{
    SetThreadDescription(GetCurrentThread(), L"raw::tx worker");
    for (;;) {
        Segment* s = nullptr;
        {
            std::unique_lock<std::mutex> g(m_);
            cv_.wait(g, [this] { return stop_ || !q_.empty(); });
            if (q_.empty()) return;
            s = q_.front();
            q_.pop_front();
        }
        ReplaySegment(ctx_[index], *s);
        { std::lock_guard<std::mutex> g(m_); s->done.store(true, std::memory_order_release); }
        doneCv_.notify_all();
    }
}

}  // namespace raw::tx
