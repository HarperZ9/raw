#pragma once
//=============================================================================
//  Projection.h — World-to-screen projection via NiCamera
//=============================================================================

#include <RE/Skyrim.h>
#include "BridgeData.h"
#include <cmath>

namespace SB::Projection
{
    // Project a world-space point to NDC [-1,1].
    // Returns {ndcX, ndcY, onScreen(0/1), screenZ}.
    inline Float4 WorldToNDC(const RE::NiPoint3& a_worldPos)
    {
        auto* camera = RE::Main::WorldRootCamera();
        if (!camera)
            return {};

        const auto& camData = camera->GetRuntimeData();
        const auto& camData2 = camera->GetRuntimeData2();
        float sx = 0.f, sy = 0.f, sz = 0.f;
        float tol = 1e-5f;

        bool ok = RE::NiCamera::WorldPtToScreenPt3(
            camData.worldToCam, camData2.port,
            a_worldPos, sx, sy, sz, tol);

        if (!ok || sz <= 0.0f)
            return {};

        // UV [0,1] → NDC [-1,1], Y-flipped to match LightParameters
        float ndcX =  (sx - 0.5f) * 2.0f;
        float ndcY = -(sy - 0.5f) * 2.0f;

        bool onScreen = (ndcX >= -1.f && ndcX <= 1.f &&
                         ndcY >= -1.f && ndcY <= 1.f);

        return { ndcX, ndcY, onScreen ? 1.f : 0.f, sz };
    }

    // Normalize a position vector and return direction + elevation.
    // Returns {dirX, dirY, dirZ, elevationRadians}.
    inline Float4 DirectionAndElevation(const RE::NiPoint3& a_pos)
    {
        float dist = std::sqrt(a_pos.x * a_pos.x + a_pos.y * a_pos.y + a_pos.z * a_pos.z);
        if (dist < 0.001f)
            return {};

        float dx = a_pos.x / dist;
        float dy = a_pos.y / dist;
        float dz = a_pos.z / dist;

        // Elevation = angle above horizontal plane (XY). Z is up in Skyrim.
        float elevation = std::asin(std::clamp(dz, -1.f, 1.f));

        return { dx, dy, dz, elevation };
    }
}
