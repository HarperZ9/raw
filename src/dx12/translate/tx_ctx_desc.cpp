// The context's shader-visible descriptor pages and null descriptors.
#include "tx_context.h"

namespace raw::tx {

uint32_t Context::AllocDescs(uint32_t n)
{
    if (descBase_ == ~0u || descUsed_ + n > Device::kDescPage) {
        if (descBase_ != ~0u) rec_.retire.descPages.push_back(descBase_);
        descBase_ = dev_->AllocDescPage();
        descUsed_ = 0;
        if (descBase_ == ~0u) return 0;
    }
    descUsed_ += n;
    return descBase_ + descUsed_ - n;
}

D3D12_GPU_DESCRIPTOR_HANDLE Context::GpuHandle(uint32_t index) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = dev_->gpuHeap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += UINT64(index) * dev_->srvInc;
    return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE Context::CpuHandle(uint32_t index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = dev_->gpuHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += SIZE_T(index) * dev_->srvInc;
    return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE Context::NullSrvFor(const ShaderData* sd, uint32_t slot)
{
    uint8_t dim = sd && slot < sd->srvDim.size() && sd->srvDim[slot] ? sd->srvDim[slot] : uint8_t(4);
    if (dim > 11) dim = 4;
    if (!nullSrv_[dim].ptr) nullSrv_[dim] = dev_->NullSrv(dim);
    return nullSrv_[dim];
}

D3D12_CPU_DESCRIPTOR_HANDLE Context::NullUavFor(const ShaderData* sd, uint32_t slot)
{
    uint8_t dim = sd && slot < sd->uavDim.size() && sd->uavDim[slot] ? sd->uavDim[slot] : uint8_t(4);
    if (dim > 11) dim = 4;
    if (!nullUav_[dim].ptr) nullUav_[dim] = dev_->NullUav(dim);
    return nullUav_[dim];
}

}  // namespace raw::tx
