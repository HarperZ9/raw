#pragma once
// Fire, torch and magic light for the ray tracer: light names and glowing emitters (RAW engine runtime).
// Original work, MIT.
//
// Lights: the NiLight's name and its parents' names (NiObjectNET +0x10, NiAVObject parent +0x30) name what a light
// belongs to; LightFlagsFromNames turns them into the flags below.
// Emitters: the game's effect and particle shaders (BSEffectShader vftable 0x1ABFB50, BSParticleShader 0x1AC3A80)
// are hooked at SetupGeometry (slot 6). A draw whose alpha property blends additively (NiAlphaProperty +0x30:
// destination factor ONE) glows. Its geometry (BSRenderPass +0x10) gives the place (world bound +0xE4, radius
// +0xF0); its effect material (BSShaderProperty +0x78) the colour (baseColor +0x48 times baseColorScale +0x6C) and
// the texture path (+0x70) that names fire or magic. Draws within 48 units merge. The intensity and radius of
// such a light are estimates from the effect's colour and size, not values the game authored.
#include <cstdint>
#include <string>
#include <vector>

#include "rt_image.h"

namespace raw::rt {

// EngineLight::flags bits (rt_livestate.h): 1 point light, 2 shadow caster, 4 the sun, and from here
constexpr uint32_t kLightFire = 8, kLightMagic = 16, kLightEmitter = 32;

uint32_t LightFlagsFromNames(const std::string& names);

struct Emitter {
    float pos[3];
    float radius;
    float color[3];
    uint32_t flags;   // kLightEmitter, plus kLightFire or kLightMagic from the texture path
    uint32_t draws;
};

// hooks on first use; `emitters` reports the last frame's list
std::string EmittersInstall(const Image& img);
void EmittersOnPresent();
std::vector<Emitter> EmittersLast();
std::string EmittersJson();

}  // namespace raw::rt
