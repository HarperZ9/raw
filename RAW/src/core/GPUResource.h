#pragma once
// GPUResource.h — Minimal D3D11 resource creation helpers.
// Eliminates the 15-line texture creation boilerplate repeated 71+ times.
// Copyright (c) 2026 Zain D. Harper (papacr0w). All rights reserved.

#include <d3d11.h>
#include <SKSE/SKSE.h>

namespace SB
{

// Create a 2D texture + SRV + optional UAV in one call.
// Returns false on any failure. Caller owns all output pointers.
inline bool CreateGPUTexture(
    ID3D11Device* dev, uint32_t w, uint32_t h, DXGI_FORMAT fmt,
    ID3D11Texture2D** tex, ID3D11ShaderResourceView** srv,
    ID3D11UnorderedAccessView** uav = nullptr,
    const char* debugName = nullptr)
{
    D3D11_TEXTURE2D_DESC d = {};
    d.Width      = w;
    d.Height     = h;
    d.MipLevels  = 1;
    d.ArraySize  = 1;
    d.Format     = fmt;
    d.SampleDesc = {1, 0};
    d.Usage      = D3D11_USAGE_DEFAULT;
    d.BindFlags  = D3D11_BIND_SHADER_RESOURCE | (uav ? D3D11_BIND_UNORDERED_ACCESS : 0);

    if (FAILED(dev->CreateTexture2D(&d, nullptr, tex))) {
        if (debugName) SKSE::log::error("CreateGPUTexture: {} failed", debugName);
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = fmt;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    if (FAILED(dev->CreateShaderResourceView(*tex, &sd, srv))) return false;

    if (uav) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
        ud.Format = fmt;
        ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        if (FAILED(dev->CreateUnorderedAccessView(*tex, &ud, uav))) return false;
    }
    return true;
}

// Create a constant buffer.
inline bool CreateCB(ID3D11Device* dev, uint32_t size, ID3D11Buffer** buf)
{
    D3D11_BUFFER_DESC d = {};
    d.ByteWidth     = (size + 15) & ~15;  // 16-byte aligned
    d.Usage         = D3D11_USAGE_DYNAMIC;
    d.BindFlags     = D3D11_BIND_CONSTANT_BUFFER;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(dev->CreateBuffer(&d, nullptr, buf));
}

// Upload data to a dynamic constant buffer.
inline bool UploadCB(ID3D11DeviceContext* ctx, ID3D11Buffer* buf, const void* data, uint32_t size)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(ctx->Map(buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
    memcpy(mapped.pData, data, size);
    ctx->Unmap(buf, 0);
    return true;
}

// Safe release helper.
template<typename T>
inline void SafeRelease(T*& ptr) { if (ptr) { ptr->Release(); ptr = nullptr; } }

} // namespace SB
