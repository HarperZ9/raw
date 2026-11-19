#pragma once
// Live engine state: the scene's lights and the shader manager's and renderer's state (RAW engine runtime).
// Original work, MIT.
//
// Each present, on the render thread, under structured exception handling:
//   BSShaderManager::State (0x2033060): shadowSceneNode[0] +0x00, timers +0x20, interior +0x44, grid centre +0xB0,
//     current technique +0xBC, ambient specular +0xFC, character light +0x10C, camera in water +0x128, water
//     intersect +0x144.
//   ShadowSceneNode: activeLights +0x130 and shadowCasterLights +0x230 (BSTArray: data +0, size +0x10), sunLight
//     +0x200, shadowDirLight +0x210. Each BSLight: pointLight +0x44, NiLight* +0x48; the NiLight's world position
//     +0xA0, diffuse +0x11C, radius +0x128, fade +0x134.
//   BSGraphics::State (0x328CC20): frame buffer viewport +0x2C, insideFrame +0x50.
// The light list is exported for the ray-tracing worker (RAW_EngineLights); `live <every>` writes the state to
// raw_engine_out/live_state.jsonl every <every> presents (0 stops), and `lights` writes the current list once.
#include <cstdint>
#include <string>

#include "rt_image.h"

namespace raw::rt {

struct EngineLight {
    float pos[3];
    float radius;
    float color[3];   // diffuse times fade
    float fade;
    uint32_t flags;   // 1 point light, 2 shadow caster, 4 the sun, 8 fire or torch, 16 magic, 32 an emitter (rt_emitters.h)
    uint32_t pad;
};

void LiveStateInit(const Image& img, const std::string& outDir, bool exportEmitters);
// glowing emitters (rt_emitters.h) in the exported light list, flagged kLightEmitter
void LiveStateExportEmitters(bool on);
void LiveStateOnPresent();
std::string LiveStateCommand(const std::string& op, const std::string& arg);

}  // namespace raw::rt

// For the ray-tracing worker: the lights of the last present. Copies up to `max` lights into `out` and returns how
// many the scene has; `*frame` gets the present they were read at. Safe from any thread.
extern "C" __declspec(dllexport) uint32_t RAW_EngineLights(raw::rt::EngineLight* out, uint32_t max, uint64_t* frame);
