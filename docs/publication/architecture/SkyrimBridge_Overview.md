# SkyrimBridge v3.0 — Overview

> **This document has been superseded.** See the project [README.md](../README.md) for the current overview.

For detailed documentation:
- [ARCHITECTURE.md](ARCHITECTURE.md) — System design, data flow, initialization order
- [PARAMETER_REFERENCE.md](PARAMETER_REFERENCE.md) — Complete float4 parameter reference
- [SHADER_INTEGRATION.md](SHADER_INTEGRATION.md) — Guide for ENB shader authors
- [CONFIGURATION.md](CONFIGURATION.md) — INI file reference
- [SHARED_MEMORY.md](SHARED_MEMORY.md) — External app integration
- [EXTENDER_COMPAT.md](EXTENDER_COMPAT.md) — ENB Extender replacement systems
- [CREDITS.md](CREDITS.md) — Credits and acknowledgments

## Summary

SkyrimBridge is an SKSE64 plugin that reads ~150 float4 parameters across 24 data domains from Skyrim's engine via CommonLibSSE-NG and pushes them to ENB shaders via ENBSetParameter every frame.

Key systems:
- **24 domain trackers** — celestial, atmosphere, fog, weather, player, camera, interior, shadow, effects, render, image space, lights, actor values, crosshair, equipment, quest, UI state, feedback, region, audio, NPC detection, performance, scene composition, theme
- **Dirty tracking** — memcmp per float4, ~200 calls/frame instead of ~1100
- **GPU feedback loop** — 5x5 backbuffer sampling with temporal analysis
- **Shader bytecode cache** — FNV-1a disk cache eliminates startup recompilation
- **ENB Extender replacement** — annotation parser, weather separation, extern bindings, parameter binding, native ATB panels
- **Scene observation** — BSShader vtable hooks for per-draw material properties
- **Write-back processor** — INI-driven game state modification
- **Shared memory bridge** — for external apps (OBS, LED sync, companion tools)
- **Papyrus bridge** — native functions for mod authors

No D3D11 resource slots are used. The only data path to ENB shaders is ENBSetParameter.
