// Parallel translation of the immediate context (tx_stream.cpp, tx_stream_play.cpp). Original work, MIT.
//
// With workers on ([Translate] Workers=N, CreateParams::workers), the immediate context stops turning
// draws into D3D12 calls on the application's thread. From the first draw, the bindings and draws go into
// a segment: a compact record of the calls (setters, draws, buffer renames), opened with a copy of the
// whole binding state. The application's thread still keeps the shadow state (the getters and the host's
// Peek reads see it) and still renames buffers on Map, so the game writes where it always did; the record
// carries each renamed buffer's GPU address. After segmentDraws draws (256) the segment goes to a worker, which
// replays it on its own replay context into its own D3D12 command list, with its own state tracker; the
// next draw opens the next segment. Any other call (a copy, a clear, a query, an upload, host access,
// Present) ends the run: the immediate context's own list continues after the segments.
//
// Order on the GPU is the order of the calls: the immediate context keeps a queue of what it closed (its
// own lists and the segments, in order) and FlushInternal waits for the workers and submits the whole
// queue as one submission, whose state fix-ups between lists the trackers already provide.
//
// Lifetime: every object a segment names is held with the immediate context's plain count (BindHolds::Add)
// on the application's thread and given back there once the submission is made.
#pragma once
#include "tx_common.h"
#include <condition_variable>
#include <deque>
#include <thread>

namespace raw::tx {

struct Recording;
struct State;
struct ImmHold;
class Context;

enum StreamOp : uint8_t {
    SO_Shader, SO_CB, SO_SRV, SO_Samp, SO_Layout, SO_VB, SO_IB, SO_Topo, SO_RT, SO_RTUAV, SO_Blend, SO_DS, SO_RS,
    SO_VP, SO_SC, SO_CSUAV, SO_Chunk, SO_Copy, SO_CopyRegion, SO_Draw, SO_DrawIndexed, SO_DrawInstanced, SO_DrawIndexedInstanced,
};

// One run of calls for a worker. Words are 64-bit; a record is a header word (op, stage, start, count)
// followed by its arguments.
struct Segment {
    std::vector<uint64_t> words;
    std::unique_ptr<State> start;                 // the binding state when the segment opened
    std::vector<std::pair<void*, uint64_t>> va;   // renamed buffers bound at the start: their GPU address then
    std::vector<ImmHold*> held;                   // given back after submission
    std::unique_ptr<Recording> out;               // the worker's closed list
    uint32_t draws = 0, copies = 0;
    uint32_t startDirty = 0;   // the context's dirty flags when it opened (an inline replay starts from them)
    bool lazy = false;         // the immediate list is not split yet (Context::Commit)
    std::atomic<bool> done{false};
};

struct StreamStats {
    uint64_t segments = 0, draws = 0, ends = 0, queryDraws = 0, inlined = 0, directRuns = 0;
    const char* why[8] = {};   // what ended the runs, the first eight kinds
    uint64_t whyN[8] = {};
    double waitUs = 0;   // the application's thread waiting for the workers at submission
};

// The workers and their replay contexts.
class StreamPool {
public:
    StreamPool(Device* dev, uint32_t workers);
    ~StreamPool();
    void Push(Segment* s);
    void Wait(Segment* s);   // until the worker closed the segment's list
    uint32_t Workers() const { return uint32_t(threads_.size()); }

private:
    void Run(uint32_t index);
    Device* dev_;
    std::vector<std::thread> threads_;
    std::vector<Context*> ctx_;
    std::mutex m_;
    std::condition_variable cv_, doneCv_;
    std::deque<Segment*> q_;
    bool stop_ = false;
};

void ReplaySegment(Context* replay, Segment& s);   // tx_stream_play.cpp
void ReplayWords(Context* c, const Segment& s);   // the records alone, on any context

}  // namespace raw::tx
