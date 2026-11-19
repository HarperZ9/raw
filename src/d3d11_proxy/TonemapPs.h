#pragma once
// TonemapPs.h: RAW's tone map, in place of the game's (Tonemap.cpp); the shader's text is src/d3d12_bridge/shaders/Tonemap.hlsl,
// embedded at configure time as TonemapSrc.h. It takes the game's tone map's
// bindings as they are: t1 the HDR scene, t0 the bloom, t2 the eye-adapted luminance (x the adapted
// average, y the game's target), pixel buffer 2 the imagespace values (bloom limit, white, curve choice,
// saturation, brightness, contrast, tint) and per-frame buffer 12 (the output gamma, the dynamic
// resolution scale). RAW's own values come in buffer 5.
//
// Exposure, mode 0: the game's (target / adapted average). Mode 1, a camera's: the metered average,
// held between MinLum and MaxLum, is brought to Key (mid grey), then ExposureBias stops. The curve,
// 0: the game's own; 1: a filmic curve per channel (the fitted ACES curve published by K. Narkowicz,
// 2015) with its input scaled by White, which rolls highlights off to white and desaturates them as film
// does. After the curve the game's imagespace grade applies as the game applies it, so weathers and
// interiors keep their look. Original work, MIT.

#include "TonemapSrc.h"   // kTonemapHlsl, generated from shaders/Tonemap.hlsl (CMakeLists.txt)
