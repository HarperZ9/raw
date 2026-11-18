// Submission, command allocators, shader-visible descriptor pages and sampler tables.
#include "tx_context.h"

namespace raw::tx {

static constexpr uint32_t kGpuDescriptors = 976 * 1024;   // under the 1,000,000 limit of resource binding tiers 1 and 2
static constexpr uint32_t kSamplerDescriptors = 2048;

HRESULT Device::InitHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kGpuDescriptors,
                                  D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    HRESULT hr = d3d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&gpuHeap));
    if (FAILED(hr)) return hr;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    hd.NumDescriptors = kSamplerDescriptors;
    if (FAILED(hr = d3d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&samplerHeap)))) return hr;
    srvInc = d3d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    sampInc = d3d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    for (uint32_t p = kGpuDescriptors / kDescPage; p-- > 0;) freePages_.push_back(p * kDescPage);

    nullRtv = cpuRtv.Alloc();
    D3D12_RENDER_TARGET_VIEW_DESC rd{};
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    d3d->CreateRenderTargetView(nullptr, &rd, nullRtv);

    D3D12_INDIRECT_ARGUMENT_DESC arg{};
    D3D12_COMMAND_SIGNATURE_DESC cs{};
    cs.NumArgumentDescs = 1; cs.pArgumentDescs = &arg;
    arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW; cs.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
    d3d->CreateCommandSignature(&cs, nullptr, IID_PPV_ARGS(&sigDraw));
    arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED; cs.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    d3d->CreateCommandSignature(&cs, nullptr, IID_PPV_ARGS(&sigDrawIndexed));
    arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH; cs.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
    d3d->CreateCommandSignature(&cs, nullptr, IID_PPV_ARGS(&sigDispatch));
    return S_OK;
}

ComPtr<ID3D12CommandAllocator> Device::GetAllocator()
{
    uint64_t done = Completed();
    {
        std::lock_guard<std::mutex> g(poolM_);
        for (size_t i = 0; i < allocPool_.size(); ++i) {
            if (allocPool_[i].serial > done) continue;
            ComPtr<ID3D12CommandAllocator> a = std::move(allocPool_[i].a);
            allocPool_[i] = std::move(allocPool_.back());
            allocPool_.pop_back();
            a->Reset();
            return a;
        }
    }
    ComPtr<ID3D12CommandAllocator> a;
    d3d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&a));
    return a;
}

void Device::RecycleAllocator(ComPtr<ID3D12CommandAllocator> a, uint64_t serial)
{
    if (!a) return;
    std::lock_guard<std::mutex> g(poolM_);
    allocPool_.push_back({std::move(a), serial});
}

void Device::OpenRecording(Recording& r)
{
    r.alloc = GetAllocator();
    if (!r.cl) {
        std::lock_guard<std::mutex> g(poolM_);
        if (!listPool_.empty()) { r.cl = std::move(listPool_.back()); listPool_.pop_back(); }
    }
    if (r.cl) r.cl->Reset(r.alloc.Get(), nullptr);
    else d3d->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, r.alloc.Get(), nullptr, IID_PPV_ARGS(&r.cl));
    r.cmdCount = 0;
    r.draws = 0;
}

void Device::RecycleList(Recording& r)
{
    RecycleAllocator(std::move(r.alloc), LastSubmitted());
    std::lock_guard<std::mutex> g(poolM_);
    if (r.cl) listPool_.push_back(std::move(r.cl));
}

ID3D12GraphicsCommandList* Device::FixupList(size_t index)
{
    ComPtr<ID3D12CommandAllocator> a = GetAllocator();
    if (index >= fixLists_.size()) {
        ComPtr<ID3D12GraphicsCommandList> l;
        d3d->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, a.Get(), nullptr, IID_PPV_ARGS(&l));
        fixLists_.push_back(l);
    } else {
        fixLists_[index]->Reset(a.Get(), nullptr);
    }
    fixAllocs_.push_back(std::move(a));
    return fixLists_[index].Get();
}

uint64_t Device::Submit(Recording** recs, int n, bool consume)
{
    std::lock_guard<std::mutex> g(submitM_);
    std::lock_guard<std::mutex> gi(initM_);
    Recording* all[9];
    int count = 0;
    bool uploads = initCmds_ != 0;
    if (uploads) { init_.cl->Close(); all[count++] = &init_; }
    for (int i = 0; i < n && count < 9; ++i) all[count++] = recs[i];

    execLists_.clear();
    size_t fix = 0;
    for (int i = 0; i < count; ++i) {
        fixups_.clear();
        all[i]->tracker.Resolve(fixups_);
        if (!fixups_.empty()) {
            ID3D12GraphicsCommandList* fl = FixupList(fix++);
            fl->ResourceBarrier(UINT(fixups_.size()), fixups_.data());
            fl->Close();
            execLists_.push_back(fl);
            nBarriers += fixups_.size();
        }
        execLists_.push_back(all[i]->cl.Get());
    }
    if (!execLists_.empty()) queue->ExecuteCommandLists(UINT(execLists_.size()), execLists_.data());
    uint64_t serial = lastSubmitted_ + 1;
    queue->Signal(fence.Get(), serial);
    lastSubmitted_ = serial;
    ++nSubmit;

    for (int i = 0; i < count; ++i) {
        Recording* r = all[i];
        bool take = consume || r == &init_;
        Retire ret;
        if (take) { ret = std::move(r->retire); r->retire = Retire{}; }
        ret.serial = serial;
        r->tracker.Keep(ret.keep);
        retireQ_.push_back(std::move(ret));
        nBarriers += r->tracker.barrierCount;
        r->tracker.barrierCount = 0;
        nDraws += r->draws;
        for (TQuery* q : r->queries) q->serial = serial;
        if (!take) continue;
        for (TQuery* q : r->queries) q->IntRelease();
        r->queries.clear();
        r->tracker.Reset();
        RecycleAllocator(std::move(r->alloc), serial);
        r->draws = 0;
        r->cmdCount = 0;
    }
    for (auto& a : fixAllocs_) RecycleAllocator(std::move(a), serial);
    fixAllocs_.clear();
    if (uploads) { OpenRecording(init_); initCmds_ = 0; }
    ReclaimLocked(false);
    return serial;
}

void Device::WaitFor(uint64_t serial)
{
    if (fence->GetCompletedValue() >= serial) return;
    fence->SetEventOnCompletion(serial, nullptr);   // null event: blocks until reached
}

void Device::ReclaimLocked(bool force)
{
    uint64_t done = force ? ~0ull : Completed();
    while (!retireQ_.empty() && retireQ_.front().serial <= done) {
        Retire& r = retireQ_.front();
        if (!r.chunks.empty()) slab.Free(r.chunks.data(), r.chunks.size());
        for (uint32_t p : r.descPages) freePages_.push_back(p);
        retireQ_.pop_front();
    }
}

void Device::Reclaim()
{
    std::lock_guard<std::mutex> g(submitM_);
    ReclaimLocked(false);
}

void Device::RetireLater(Retire&& r)
{
    if (r.Empty()) return;
    std::lock_guard<std::mutex> g(submitM_);
    r.serial = lastSubmitted_ + 1;
    retireQ_.push_back(std::move(r));
}

uint32_t Device::AllocDescPage()
{
    for (int attempt = 0; attempt < 64; ++attempt) {
        uint64_t waitSerial = 0;
        {
            std::lock_guard<std::mutex> g(submitM_);
            if (freePages_.empty()) ReclaimLocked(false);
            if (!freePages_.empty()) {
                uint32_t p = freePages_.back();
                freePages_.pop_back();
                return p;
            }
            if (retireQ_.empty() || retireQ_.front().serial > lastSubmitted_) break;
            waitSerial = retireQ_.front().serial;
        }
        WaitFor(waitSerial);
    }
    Log("shader-visible descriptor heap exhausted");
    return ~0u;
}

D3D12_GPU_DESCRIPTOR_HANDLE Device::SamplerTable(const uint32_t* ids, uint32_t n)
{
    uint64_t key = Fnv(ids, n * sizeof(uint32_t), 1469598103934665603ull ^ n);
    std::lock_guard<std::mutex> g(cacheM_);
    auto it = sampTables_.find(key);
    if (it != sampTables_.end()) return it->second;
    if (sampUsed_ + n > kSamplerDescriptors) {
        Log("sampler heap exhausted (%u unique tables)", unsigned(sampTables_.size()));
        return samplerHeap->GetGPUDescriptorHandleForHeapStart();
    }
    D3D12_CPU_DESCRIPTOR_HANDLE c = samplerHeap->GetCPUDescriptorHandleForHeapStart();
    c.ptr += SIZE_T(sampUsed_) * sampInc;
    for (uint32_t i = 0; i < n; ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE dst{c.ptr + SIZE_T(i) * sampInc};
        d3d->CopyDescriptorsSimple(1, dst, sampHandles_[ids[i]], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    }
    D3D12_GPU_DESCRIPTOR_HANDLE h = samplerHeap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += UINT64(sampUsed_) * sampInc;
    sampUsed_ += n;
    sampTables_.emplace(key, h);
    return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE Device::NullSrv(uint8_t dim)
{
    std::lock_guard<std::mutex> g(cacheM_);
    auto it = nullSrv_.find(dim);
    if (it != nullSrv_.end()) return it->second;
    D3D12_SHADER_RESOURCE_VIEW_DESC d{};
    d.ViewDimension = dim ? static_cast<D3D12_SRV_DIMENSION>(dim) : D3D12_SRV_DIMENSION_TEXTURE2D;
    d.Format = d.ViewDimension == D3D12_SRV_DIMENSION_BUFFER ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R8G8B8A8_UNORM;
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    D3D12_CPU_DESCRIPTOR_HANDLE h = cpuSrv.Alloc();
    d3d->CreateShaderResourceView(nullptr, &d, h);
    nullSrv_.emplace(dim, h);
    return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE Device::NullUav(uint8_t dim)
{
    std::lock_guard<std::mutex> g(cacheM_);
    auto it = nullUav_.find(dim);
    if (it != nullUav_.end()) return it->second;
    D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
    d.ViewDimension = dim ? static_cast<D3D12_UAV_DIMENSION>(dim) : D3D12_UAV_DIMENSION_TEXTURE2D;
    d.Format = d.ViewDimension == D3D12_UAV_DIMENSION_BUFFER ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R8G8B8A8_UNORM;
    D3D12_CPU_DESCRIPTOR_HANDLE h = cpuSrv.Alloc();
    d3d->CreateUnorderedAccessView(nullptr, nullptr, &d, h);
    nullUav_.emplace(dim, h);
    return h;
}

}  // namespace raw::tx
