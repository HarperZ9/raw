# Credits & Acknowledgments

## Author

**Zain Dana Harper** — Design, implementation, HLSL shaders, documentation

## License

MIT License. See individual file headers for per-file attribution.

---

## Third-Party Libraries

### CommonLibSSE-NG
- **Authors:** Ryan McKenzie (original), CharmedBaryon, colorglass (NG fork)
- **License:** MIT
- **Repository:** [gitlab.com/colorglass/commonlibsse-ng](https://gitlab.com/colorglass/commonlibsse-ng)
- **Usage:** Skyrim engine reverse engineering — all 24 trackers access game state through CommonLibSSE types and addresses

### Dear ImGui
- **Author:** Omar Cornut
- **License:** MIT
- **Repository:** [github.com/ocornut/imgui](https://github.com/ocornut/imgui)
- **Usage:** Debug GUI overlay (INSERT key), parameter inspection, tracker health display

### stb_image.h
- **Author:** Sean Barrett
- **License:** MIT / Public Domain
- **Repository:** [github.com/nothings/stb](https://github.com/nothings/stb)
- **Usage:** PNG texture loading (included in `src/vendor/`)

### spdlog
- **Author:** Gabi Melman
- **License:** MIT
- **Repository:** [github.com/gabime/spdlog](https://github.com/gabime/spdlog)
- **Usage:** Structured logging (via CommonLibSSE dependency)

---

## ENB SDK & Tools

### ENBSeries
- **Author:** Boris Vorontsov
- **Website:** [enbdev.com](http://enbdev.com)
- **Usage:** ENBSeries SDK functions (ENBSetParameter, ENBGetParameter, ENBSetCallbackFunction) resolved at runtime from d3d11.dll. AntTweakBar (ATB) API exported by d3d11.dll for native GUI panels.

### enb-api
- **Author:** doodlum
- **Repository:** [github.com/doodlum/enb-api](https://github.com/doodlum/enb-api)
- **Usage:** Reference for correct ENB SDK function signatures and calling conventions. Playground's ENBInterface.h is informed by doodlum's wrapper.

### ENB Extender
- **Author:** Kitsuune
- **Nexus:** [ENB Extender on Nexus Mods](https://www.nexusmods.com/skyrimspecialedition/mods/104824)
- **Usage:** Playground's ShaderPreProcessor, ExternBindingProcessor, WeatherSeparationEngine, and ParameterBindingEngine are designed as drop-in replacements for ENB Extender's annotation system. The annotation format (Separation, UIGroup, UIBinding, ExternBinding) is compatible with Extender's specification.

### enbParmLink
- **Author:** kingeric1992
- **Nexus:** [enbParmLink on Nexus Mods](https://www.nexusmods.com/skyrimspecialedition/mods/53629)
- **Usage:** Playground's ParmLinkCompat layer provides backward compatibility with enbParmLink's parameter naming conventions.

### enbPanels-Reference
- **Author:** kingeric1992
- **Usage:** Reference for ENBSetParameter UIName matching behavior and ENB editor panel conventions.

### Community Shaders
- **Author:** doodlum and contributors
- **Usage:** Reference for Light Limit Fix clustered forward+ lighting architecture. Playground's ClusteredLighting extends this with adaptive cluster sizing, temporal caching, and Hi-Z shadow-aware culling.

---

## SKSE & Modding Infrastructure

### SKSE64 (Skyrim Script Extender)
- **Authors:** ianpatt, behippo, purpox
- **Website:** [skse.silverlock.org](https://skse.silverlock.org)
- **Usage:** Plugin loading, messaging interface, Papyrus native function registration

### Address Library for SKSE Plugins
- **Author:** meh321
- **Usage:** Runtime address resolution for SE/AE binary compatibility

### vcpkg
- **Author:** Microsoft
- **Repository:** [github.com/microsoft/vcpkg](https://github.com/microsoft/vcpkg)
- **Usage:** C++ dependency management (CommonLibSSE-NG, ImGui)

---

## Rendering Technique References

The GPU rendering pipeline implements techniques from the following researchers and publications:

- **Jorge Jimenez** — Ground Truth Ambient Occlusion (GTAO, "Practical Realtime Strategies for Accurate Indirect Occlusion", SIGGRAPH 2016), separable subsurface scattering, temporal AA
- **Tomasz Stachowiak** — Hi-Z accelerated screen-space reflections ("Stochastic Screen-Space Reflections", SIGGRAPH 2015)
- **Eric Bruneton** — Precomputed atmospheric scattering model ("Precomputed Atmospheric Scattering", EGSR 2008)
- **Sébastien Hillaire** — Improved atmospheric scattering LUTs ("A Scalable and Production Ready Sky and Atmosphere Rendering Technique", EGSR 2020)
- **Andrew Lauritzen** — Sample Distribution Shadow Maps (SDSM, depth histogram cascade optimization)
- **Troy Sobotka** — AgX display transform (perceptual tone mapping)
- **Timothy Lottes** — Tonemap operator (Lottes 2016)
- **Hajime Uchimura** — Gran Turismo tonemap operator
- **Andrew Schneider & Nathan Vos** — Volumetric cloud ray marching (Nubis system, SIGGRAPH 2015/2017, Guerrilla Games)
- **Bart Wronski** — Volumetric fog, temporal reprojection techniques
- **Frans Bouma** — ReShade framework techniques (DOF, SSAO, color grading patterns)

### ENB Preset Authors
- **Adrien Bock** — PI-CHO ENB (ScreenSize convention documentation, technique patterns)
- **Kitsuune** — Rudy ENB for Cathedral (multi-pass technique architecture, prepass conventions)
- **Sandvich Maker** — Sandvich ENB (adaptation patterns)
- **Juicehead** — Various preset analysis

---

## Research Documents

The `Inspiration and Techniques/` directory contains research notes that informed shader development:

| Document | Topic |
|---|---|
| ShaderTechniques_MasterReference.md | Bloom, volumetrics, AO, tonemapping, DOF, color, lens, temporal, architecture |
| ThirdParty_Shader_Analysis.md | ENB preset author technique inventory |
| FransBouma_Technique_Analysis.md | ReShade technique patterns |
| FilmColorScience_ENB_Research.md | Film color science for ENB |
| FilmColorGrading_Techniques_Research.md | Film grading techniques |
| ENB_Shader_Author_Technique_Inventory.md | Technique catalog across presets |

---

## Tools

- **Visual Studio 2022** — C++ compilation (v143 toolset, C++23)
- **CMake** — Build system
- **Mod Organizer 2** — Deployment testing (VFS-aware path resolution)
