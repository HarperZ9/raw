#include "RenderTracker.h"
#include <RE/Skyrim.h>

namespace SB::RenderTracker
{
    static uint32_t s_frameCount = 0;

    RenderData Update(float a_deltaTime)
    {
        RenderData data{};
        s_frameCount++;

        data.FrameInfo.x = static_cast<float>(s_frameCount);
        data.FrameInfo.y = a_deltaTime;

        // Screen dimensions from BSGraphics::State
        auto* gfx = RE::BSGraphics::State::GetSingleton();
        if (gfx) {
            data.FrameInfo.z = static_cast<float>(gfx->screenWidth);
            data.FrameInfo.w = static_cast<float>(gfx->screenHeight);
        }

        // ── TAA jitter ──────────────────────────────────────────────────
        // Skyrim's TAA applies a subpixel jitter to the projection matrix.
        // If we can read the jitter offset, shaders can unjitter for
        // clean motion vectors and temporal accumulation.
        //
        // The jitter is typically stored in BSGraphics::State or applied
        // as a projection matrix offset.  For now we provide the frame
        // index for Halton/blue noise sequences in the shader.
        data.Jitter.z = static_cast<float>(s_frameCount % 16);

        // Halton(2,3) sequence for 16 frames — computed in shader.
        // We just provide the index.

        return data;
    }
}
