#pragma once
// Backbuffer to BMP (RAW engine runtime). Original work, MIT.
#include <d3d11.h>
#include <dxgi.h>

#include <string>

namespace raw::rt {
bool CaptureBackbuffer(ID3D11DeviceContext* ctx, IDXGISwapChain* sc, const std::string& path);
}
