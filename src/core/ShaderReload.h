#pragma once
// ShaderReload.h — Hot-reload all renderer shaders from disk.
// Called by F12 key handler after ShaderLoader::InvalidateAll().
// Copyright (c) 2026 Zain D. Harper (papacr0w). All rights reserved.

namespace SB
{
    // Recompile all renderer shaders from disk/embedded sources.
    // Releases old ID3D11ComputeShader/PixelShader objects, creates new ones.
    // Returns number of shaders successfully recompiled.
    int ReloadAllShaders();

    // Call once per frame. Checks for shader file changes every ~60 frames.
    // Auto-reloads changed shaders without requiring F12.
    void ShaderAutoReloadTick();
}
