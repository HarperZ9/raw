#pragma once
//=============================================================================
//  ScreenshotCapture.h — BMP Screenshot System
//
//  Captures the backbuffer and saves as BMP with ENB naming convention:
//  enb_YYYY_MM_DD_HH_MM_SS_MS.bmp
//=============================================================================

#include <Windows.h>
#include <d3d11.h>

class ScreenshotCapture
{
public:
    // Capture the current backbuffer to a BMP file
    bool Capture(ID3D11Device* device, ID3D11DeviceContext* ctx,
                 IDXGISwapChain* swapChain, const char* gameDir);
};

extern ScreenshotCapture g_Screenshot;
