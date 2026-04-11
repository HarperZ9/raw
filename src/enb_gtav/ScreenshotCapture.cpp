//=============================================================================
//  ScreenshotCapture.cpp — BMP Screenshot Implementation
//
//  Matches original ENB naming: enb%d_%d_%d_%02d_%02d_%02d_%02d.bmp
//=============================================================================

#include "ScreenshotCapture.h"
#include <cstdio>

ScreenshotCapture g_Screenshot;

bool ScreenshotCapture::Capture(ID3D11Device* device, ID3D11DeviceContext* ctx,
                                 IDXGISwapChain* swapChain, const char* gameDir)
{
    if (!device || !ctx || !swapChain)
        return false;

    // Get backbuffer
    ID3D11Texture2D* backbuffer = nullptr;
    HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                       reinterpret_cast<void**>(&backbuffer));
    if (FAILED(hr) || !backbuffer)
        return false;

    D3D11_TEXTURE2D_DESC desc;
    backbuffer->GetDesc(&desc);

    // Create staging texture for CPU readback
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage          = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags      = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags      = 0;

    ID3D11Texture2D* staging = nullptr;
    hr = device->CreateTexture2D(&stagingDesc, nullptr, &staging);
    if (FAILED(hr))
    {
        backbuffer->Release();
        return false;
    }

    ctx->CopyResource(staging, backbuffer);
    backbuffer->Release();

    // Map and read pixels
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr))
    {
        staging->Release();
        return false;
    }

    // Generate filename with timestamp
    SYSTEMTIME st;
    GetLocalTime(&st);

    char filename[MAX_PATH];
    snprintf(filename, MAX_PATH,
             "%s\\enb_%d_%d_%d_%02d_%02d_%02d_%02d.bmp",
             gameDir,
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond,
             st.wMilliseconds / 10);

    // Write BMP file
    UINT width  = desc.Width;
    UINT height = desc.Height;
    UINT rowPitch = width * 3;
    UINT rowPadding = (4 - (rowPitch % 4)) % 4;
    UINT bmpRowSize = rowPitch + rowPadding;
    UINT pixelDataSize = bmpRowSize * height;

    HANDLE hFile = CreateFileA(filename, GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        ctx->Unmap(staging, 0);
        staging->Release();
        return false;
    }

    // BMP file header (14 bytes)
    BITMAPFILEHEADER bmfh = {};
    bmfh.bfType    = 0x4D42; // "BM"
    bmfh.bfSize    = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + pixelDataSize;
    bmfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    // BMP info header (40 bytes)
    BITMAPINFOHEADER bmih = {};
    bmih.biSize        = sizeof(BITMAPINFOHEADER);
    bmih.biWidth       = width;
    bmih.biHeight      = height; // positive = bottom-up
    bmih.biPlanes      = 1;
    bmih.biBitCount    = 24;
    bmih.biCompression = BI_RGB;
    bmih.biSizeImage   = pixelDataSize;

    DWORD written;
    WriteFile(hFile, &bmfh, sizeof(bmfh), &written, nullptr);
    WriteFile(hFile, &bmih, sizeof(bmih), &written, nullptr);

    // Write pixel rows (BMP is bottom-up, BGRA -> BGR)
    BYTE* rowBuf = new BYTE[bmpRowSize];
    memset(rowBuf, 0, bmpRowSize);

    for (int y = static_cast<int>(height) - 1; y >= 0; y--)
    {
        const BYTE* srcRow = static_cast<const BYTE*>(mapped.pData) + y * mapped.RowPitch;

        for (UINT x = 0; x < width; x++)
        {
            // Source is RGBA or BGRA depending on format
            // Most common backbuffer format is R8G8B8A8_UNORM
            BYTE r = srcRow[x * 4 + 0];
            BYTE g = srcRow[x * 4 + 1];
            BYTE b = srcRow[x * 4 + 2];

            // BMP stores BGR
            if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
            {
                // Already BGR in source
                rowBuf[x * 3 + 0] = r; // B
                rowBuf[x * 3 + 1] = g; // G
                rowBuf[x * 3 + 2] = b; // R
            }
            else
            {
                // RGBA -> BGR
                rowBuf[x * 3 + 0] = b; // B
                rowBuf[x * 3 + 1] = g; // G
                rowBuf[x * 3 + 2] = r; // R
            }
        }

        WriteFile(hFile, rowBuf, bmpRowSize, &written, nullptr);
    }

    delete[] rowBuf;
    CloseHandle(hFile);
    ctx->Unmap(staging, 0);
    staging->Release();

    OutputDebugStringA("[ENB] Screenshot saved: ");
    OutputDebugStringA(filename);
    OutputDebugStringA("\n");

    return true;
}
