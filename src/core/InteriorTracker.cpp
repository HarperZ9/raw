#include "InteriorTracker.h"
#include <RE/Skyrim.h>
#include <cmath>

namespace SB::InteriorTracker
{
    static Float4 ColorToFloat4(uint32_t a_abgr)
    {
        return {
            ((a_abgr >> 0)  & 0xFF) / 255.0f,
            ((a_abgr >> 8)  & 0xFF) / 255.0f,
            ((a_abgr >> 16) & 0xFF) / 255.0f,
            ((a_abgr >> 24) & 0xFF) / 255.0f,
        };
    }

    static Float4 NiColorToFloat4(const RE::NiColor& c, float a_alpha = 1.f)
    {
        return { c.red, c.green, c.blue, a_alpha };
    }

    InteriorData Update()
    {
        InteriorData data{};

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return data;

        auto* cell = player->GetParentCell();
        if (!cell) return data;

        data.IsInterior.x = cell->IsInteriorCell() ? 1.f : 0.f;

        if (!cell->IsInteriorCell())
            return data;

        // ── Interior lighting data ─────────────────────────────────────────
        auto* ld = cell->GetLighting();
        if (!ld) {
            data.IsInterior.y = 0.f;
            return data;
        }

        data.IsInterior.y = 1.f;  // has lighting data

        // Ambient color (Color type has red/green/blue/alpha members)
        data.AmbientColor = {
            ld->ambient.red   / 255.0f,
            ld->ambient.green / 255.0f,
            ld->ambient.blue  / 255.0f,
            1.0f
        };

        // Directional light color
        data.DirectionalColor = {
            ld->directional.red   / 255.0f,
            ld->directional.green / 255.0f,
            ld->directional.blue  / 255.0f,
            ld->directionalFade
        };

        // Directional light direction (XY rotation angles → direction vector)
        // directionalXY and directionalZ are stored as uint32_t but represent angles
        float rotX = static_cast<float>(ld->directionalXY) * (3.14159265f / 180.0f);
        float rotZ = static_cast<float>(ld->directionalZ)  * (3.14159265f / 180.0f);
        data.DirectionalDir.x = std::cos(rotX) * std::sin(rotZ);
        data.DirectionalDir.y = std::sin(rotX) * std::sin(rotZ);
        data.DirectionalDir.z = std::cos(rotZ);

        // Interior fog
        data.InteriorFogColor = {
            ld->fogColorNear.red   / 255.0f,
            ld->fogColorNear.green / 255.0f,
            ld->fogColorNear.blue  / 255.0f,
            0.f
        };

        data.InteriorFogDist.x = ld->fogNear;
        data.InteriorFogDist.y = ld->fogFar;
        data.InteriorFogDist.z = ld->fogPower;
        data.InteriorFogDist.w = ld->clipDist;

        return data;
    }
}
