#pragma once
// FrameLedger internals, shared by FrameLedger.cpp (tracking) and
// FrameLedgerDump.cpp (files). Original work, MIT.
#include "FrameLedger.h"
#include "RenderPhaseDetector.h"
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace SB::Proxy::ledger
{

struct TexRec
{
    ID3D11Resource* res = nullptr;      // AddRef'd for the armed frame
    ID3D11Resource* staging = nullptr;  // CPU-readable clone, made on first dump
    int  id = 0;
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    UINT w = 0, h = 0, depth = 1, array = 1, mips = 1, samples = 1, bind = 0, misc = 0;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    uint32_t rtIntervals = 0, dsIntervals = 0, uavWrites = 0, copiesIn = 0, copiesOut = 0, draws = 0;
    bool written = false, dumpFailed = false;
    int  sharedEqual = 0, sharedDifferent = 0, sharedFailed = 0;   // slices, D3D12 read against the D3D11 bytes
    std::string note;
};

struct ViewRef
{
    int tex = -1;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    UINT mip = 0, slice = 0, slices = 1, flags = 0;
};

struct Interval
{
    int seq = 0;
    RenderPhase phaseOpen = RenderPhase::Unknown, phaseClose = RenderPhase::Unknown;
    ViewRef rt[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
    UINT numRt = 0;
    ViewRef ds;
    float vpW = 0, vpH = 0;
    uint32_t draws = 0, blendDraws = 0, depthWriteDraws = 0, firstDraw = 0;
    std::vector<uint64_t> ps;       // distinct pixel shaders, in first-use order
    std::vector<int> history;       // texture ids dumped after this interval
};

struct Event   // a clear, copy or dispatch, placed after interval `afterSeq` opened
{
    int afterSeq = 0;
    std::string json;
};

struct State
{
    // config
    bool enabled = false, history = true, loaded = false;
    int after = 600, minDraws = 500, captures = 1, every = 600;
    std::string outRoot;
    // run
    int captured = 0, gameplay = 0;
    UINT bbW = 0, bbH = 0;
    std::string dir;
    std::unordered_map<ID3D11Resource*, int> byRes;
    std::vector<TexRec> tex;
    std::vector<Interval> intervals;
    std::vector<Event> events;
    std::unordered_map<uint64_t, ID3D11PixelShader*> psSeen;
    uint64_t curPs = 0, curVs = 0;
    uint32_t drawCount = 0;
    FILE* draws = nullptr;
    FILE* drawCb = nullptr;
    uint64_t drawCbBytes = 0;
};

State& S();

int  Track(ID3D11Resource* res);                       // texture id, or -1 for a non-texture
ViewRef RefOf(ID3D11View* view);
// Copies texture `id` to its staging clone and writes it as <dir>/<name>.rawtex.
bool DumpTexture(ID3D11DeviceContext* ctx, int id, const char* name, bool shareCheck = false);
// Ends the open interval: counts its writes and keeps full-size targets (History=1).
void CloseInterval(ID3D11DeviceContext* ctx);
bool FullSize(const TexRec& t);
void WriteManifest();
void DumpShaders();
const char* FormatName(DXGI_FORMAT f);
UINT BytesPerPixel(DXGI_FORMAT f);

} // namespace SB::Proxy::ledger
