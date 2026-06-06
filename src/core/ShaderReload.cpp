// ShaderReload.cpp — Hot-reload implementation.
// Copyright (c) 2026 Zain D. Harper (papacr0w). All rights reserved.

#include "ShaderReload.h"
#include "ShaderLoader.h"
#include "GTAORenderer.h"
#include "ContactShadowRenderer.h"
#include "SkylightingRenderer.h"
#include "SSRRenderer.h"
#include "SSGIRenderer.h"
#include "SceneCompositor.h"
#include <SKSE/SKSE.h>

namespace SB
{

int ReloadAllShaders()
{
    // Invalidate cached source files (forces re-read from disk)
    ShaderLoader::InvalidateAll();

    int ok = 0, fail = 0;

    // Each renderer that supports hot-reload
    auto tryReload = [&](const char* name, auto& renderer) {
        if (!renderer.IsInitialized()) return;
        if (renderer.RecompileShaders()) {
            ok++;
            SKSE::log::info("ShaderReload: {} OK", name);
        } else {
            fail++;
            SKSE::log::error("ShaderReload: {} FAILED", name);
        }
    };

    tryReload("GTAO", GTAORenderer::Get());
    tryReload("ContactShadows", ContactShadowRenderer::Get());
    tryReload("Skylighting", SkylightingRenderer::Get());
    tryReload("SSR", SSRRenderer::Get());
    tryReload("SSGI", SSGIRenderer::Get());

    SKSE::log::info("ShaderReload: {}/{} succeeded", ok, ok + fail);
    return ok;
}

void ShaderAutoReloadTick()
{
    static uint32_t s_frameCounter = 0;
    if (++s_frameCounter < 60) return;  // check every ~1 second at 60fps
    s_frameCounter = 0;

    auto changed = ShaderLoader::CheckForChanges();
    if (changed.empty()) return;

    SKSE::log::info("ShaderAutoReload: {} file(s) changed on disk, reloading...", changed.size());
    for (auto& name : changed)
        SKSE::log::info("  changed: {}", name);

    ReloadAllShaders();
}

} // namespace SB
