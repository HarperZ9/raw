//=============================================================================
//  RenderTargetPool.cpp — Named Render Target Management
//=============================================================================

#include "RenderTargetPool.h"
#include <cstdio>

RenderTargetPool g_RTPool;

static void RTLog(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
}

bool RenderTargetPool::CreateRT(ID3D11Device* device, const std::string& name,
                                 UINT width, UINT height, DXGI_FORMAT format)
{
    RenderTargetEntry entry;
    entry.width  = width;
    entry.height = height;
    entry.format = format;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width      = width;
    texDesc.Height     = height;
    texDesc.MipLevels  = 1;
    texDesc.ArraySize  = 1;
    texDesc.Format     = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage      = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags  = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, &entry.texture);
    if (FAILED(hr))
    {
        RTLog("[ENB] Failed to create RT '%s' (%ux%u): 0x%08X\n", name.c_str(), width, height, hr);
        return false;
    }

    hr = device->CreateRenderTargetView(entry.texture, nullptr, &entry.rtv);
    if (FAILED(hr))
    {
        entry.texture->Release();
        return false;
    }

    hr = device->CreateShaderResourceView(entry.texture, nullptr, &entry.srv);
    if (FAILED(hr))
    {
        entry.rtv->Release();
        entry.texture->Release();
        return false;
    }

    m_targets[name] = entry;
    return true;
}

bool RenderTargetPool::Initialize(ID3D11Device* device, UINT screenWidth, UINT screenHeight)
{
    m_screenWidth  = screenWidth;
    m_screenHeight = screenHeight;

    // Fixed-size render targets (bloom/lens pipeline)
    // R16G16B16A16_FLOAT for HDR precision
    CreateRT(device, "RenderTarget1024", 1024, 1024, DXGI_FORMAT_R16G16B16A16_FLOAT);
    CreateRT(device, "RenderTarget512",  512,  512,  DXGI_FORMAT_R16G16B16A16_FLOAT);
    CreateRT(device, "RenderTarget256",  256,  256,  DXGI_FORMAT_R16G16B16A16_FLOAT);
    CreateRT(device, "RenderTarget128",  128,  128,  DXGI_FORMAT_R16G16B16A16_FLOAT);
    CreateRT(device, "RenderTarget64",   64,   64,   DXGI_FORMAT_R16G16B16A16_FLOAT);
    CreateRT(device, "RenderTarget32",   32,   32,   DXGI_FORMAT_R16G16B16A16_FLOAT);
    CreateRT(device, "RenderTarget16",   16,   16,   DXGI_FORMAT_R16G16B16A16_FLOAT);

    // Screen-size render targets
    CreateRT(device, "RenderTargetRGBA32",  screenWidth, screenHeight, DXGI_FORMAT_R8G8B8A8_UNORM);
    CreateRT(device, "RenderTargetRGBA64",  screenWidth, screenHeight, DXGI_FORMAT_R16G16B16A16_UNORM);
    CreateRT(device, "RenderTargetRGBA64F", screenWidth, screenHeight, DXGI_FORMAT_R16G16B16A16_FLOAT);
    CreateRT(device, "RenderTargetR16F",    screenWidth, screenHeight, DXGI_FORMAT_R16_FLOAT);
    CreateRT(device, "RenderTargetR32F",    screenWidth, screenHeight, DXGI_FORMAT_R32_FLOAT);
    CreateRT(device, "RenderTargetRGB32F",  screenWidth, screenHeight, DXGI_FORMAT_R11G11B10_FLOAT);

    // Adaptation textures (ping-pong)
    CreateRT(device, "TexturePrevious", 1, 1, DXGI_FORMAT_R32_FLOAT);
    CreateRT(device, "TextureCurrent",  16, 16, DXGI_FORMAT_R32_FLOAT);

    // Downsampled texture for bloom/lens input (1024x1024 from screen)
    CreateRT(device, "TextureDownsampled", 1024, 1024, DXGI_FORMAT_R16G16B16A16_FLOAT);

    RTLog("[ENB] Render target pool: %zu targets created (screen %ux%u)\n",
          m_targets.size(), screenWidth, screenHeight);
    return true;
}

void RenderTargetPool::DestroyAll()
{
    for (auto& [name, entry] : m_targets)
    {
        if (entry.srv)     entry.srv->Release();
        if (entry.rtv)     entry.rtv->Release();
        if (entry.texture) entry.texture->Release();
    }
    m_targets.clear();
}

void RenderTargetPool::Shutdown()
{
    DestroyAll();
}

void RenderTargetPool::Resize(ID3D11Device* device, UINT screenWidth, UINT screenHeight)
{
    if (screenWidth == m_screenWidth && screenHeight == m_screenHeight)
        return;

    DestroyAll();
    Initialize(device, screenWidth, screenHeight);
}

RenderTargetEntry* RenderTargetPool::Get(const std::string& name)
{
    auto it = m_targets.find(name);
    return (it != m_targets.end()) ? &it->second : nullptr;
}

ID3D11ShaderResourceView* RenderTargetPool::GetSRV(const std::string& name)
{
    auto* entry = Get(name);
    return entry ? entry->srv : nullptr;
}

ID3D11RenderTargetView* RenderTargetPool::GetRTV(const std::string& name)
{
    auto* entry = Get(name);
    return entry ? entry->rtv : nullptr;
}
