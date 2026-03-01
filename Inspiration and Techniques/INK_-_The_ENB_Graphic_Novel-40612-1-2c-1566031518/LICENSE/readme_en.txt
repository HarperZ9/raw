//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//ENBSeries is a set of graphical modifications for games
//Description on the web page may not be equal to this info.
//created by Boris Vorontsov http://enbdev.com
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

PUBLISHING BINARY FILES OF ENBSERIES ON NEXUS SITES (FALLOUT NEXUS, TES NEXUS, ETC)
IS STRICTLY PROHIBITED. ONLY PRESETS AND SHADERS CAN BE HOSTED THERE.



ENBSeries v0.385 for Fallout 4.

Added bloom thresold and scale to Params01 of enbeffect.fx shader. Added reflection flatness
parameter for water. Fixed invalid interior detection in some places. Improved water reflections.
Added color intensity parameters for fog and reduced artifacts with water reflection.


WARNING! For compatibility with some other software (i call it crapware) read "problems"
category of this document, it's very important to not have any other tools hooking in to
game process or graphics. Crashes, graphic artifacts after installing ENB mods/patches
almost always means crapware is running.

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//CHANGES LOG
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
Version 0.384
Added palette bitmap support. Added screen space reflections, improving existing in-game
effect and reflection blurring factor for water.

Version 0.382
Added water parameters and effects of castics, dispersion, parallax, sun scattering.
To use parallax displacement feature, use textures with alpha channel height map.

Version 0.355
Added new ao type with self intersecting and increased performance of ssao/ssil effect.
Timer.z is now frame number which cyclically wraps to 0 after 9999 frames passed.

Version 0.344
Increased performance by reducing cpu bottleneck when many objects visible on the screen.

Version 0.323
Added experimental skylighting effect, it is buggy and may not work with some shadow
quality and shadow distance set to ultra. Maybe after Bethesda will stop patching game,
i'll fix artifacts and if this effect worth to use in this game.

Version 0.317
Added detailed shadow effect, currently it's can't be tweaked. Added shadows from clouds
and automatic directional ambient calculation now depends from this kind of shadows.
Fixed wrong interior detection for some places and bugs in directional and ambient
lighting not working at some conditions.

Version 0.316
Increased quality of ssao effect, added new parameters to it UseComplexAmbientOcclusion,
EnableComplexFilter, FilterBluriness. Added automatic calculation of directional ambient
from sky information and new parameters for this feature EnableSkyAmbientCalculation,
SkyAmbientTopIntensity, SkyAmbientBottomIntensity. Updated enbseries sdk.

Version 0.311
Added fog parameters, fixed UseOriginalColorFilter for latest game patch.
Added support of loading multiple plugins (.dllplugin extention) from "enbseries" folder.
Increased quality and performance of ssao effect and decreased startup time.

Version 0.307
Added some environment lighting controls and ambient occlusion.

Version 0.291
Increased dithering amount for sky gradient, because previous reported was not enough.
Added SDK with example for using ENBSeries with script extenders.

Version 0.289
Added time of the day separation, but interiors are forced to have day time temporary.
Added ENightDayFactor and EInteriorFactor parameters to external shaders. Added sky,
sun, moon, clouds parameters to enbseries.ini. Weather system is still not active,
but parameters declared in configuration file.

Version 0.288
Added enbbloom.fx and enblens.fx shaders as examples for preset makers, bloom is
drawed after depth of field and lens after bloom. Added parameters and lens texture
to enbeffect.fx shader. Added RenderTargetRGB32F temporary render target to depth
of field and post process shader for optimization purposes. Changed render targets
format of enbeffectpostpass.fx to 32 bit with 10 bits per color channel and 2 bits
for alpha, performance is more important than quality difference which nobody can
see when using 64 bit format. Added tempInfo2 parameter to external shaders. Fixed
mistakes in my comments for shader files.

Version 0.287
Added depth of field shader with example code for bokeh dof, computation of
focusing distance without first person models and aperture. Added aperture
texture to enbeffect.fx shader as result of depth of field processing.
Added depth of field parameters to enbseries.ini.

Version 0.286:
Added support of temporary render targets for enbeffectpostpass.fx shader file and
new textures for it, temporary targets have predefined formats and full screen size.
Added enbadaptation.fx external shader for eye adaptation effect. Added adaptation
parameters to enbseries.ini config file. Changed format of main enbeffectpostpass.fx
render targets to 16 bit per channel format to hide color banding completely. Added
dithering to reduce color banding artifacts for game hdr texture. Fixed all reported bugs.

Version 0.285:
Added support of external enbeffectpostpass.fx shader, which is replacement of
effect.txt shader. Sample file is included, but file format may change after
discussion with modders, so it's just preview. Fixed reported bugs of previous
version. Added parameter to toggle post pass shader.

Version 0.284:
First release of graphic mod as beta test. ENBoost part of it is included and recommended
for performance reasons instead of version 0.283. Added support of external enbeffect.fx
shader and some parameters in enbseries.ini. This version do not have any changes to visuals
by default and require editing of shader (except color correction parameters). I just need
to get statistics if everything works properly and if new file standart is good enough for
custom shaders.

Version 0.283:
Added draw calls statistics to profiler. Added UsePatchSpeedhackWithoutGraphics parameter
to enblocal.ini to disable graphic modification code for maximal performance.

Version 0.282:
Did workaround for Steam GameOverlayRenderer64.dll library which hooks in to game even
when Steam overlay disabled which crashes with the ENBoost.
Added memory control feature to avoid lod issues for users with not enough physical
vram size and for future mods, modify enblocal.ini [MEMORY] category based on information
reported by VRamSizeTest dx11 tool (do not set value bigger than reported by it).
Added ApplyStabilityPatch parameter which toggles off bugfixes, but keep vram adjust feature.
Added fix for the bug with ENBoost startup message or editor visible on character face, it
also should improve game stability (active only when ApplyStabilityPatch=true is set).

Version 0.281:
This build named ENBoost because of similar functionality to Skyrim and Fallout 3 ENBoost.
Added memory control feature to avoid lod issues for users with not enough physical
vram size and for future mods, modify enblocal.ini [MEMORY] category based on information
reported by VRamSizeTest dx11 tool (do not set value bigger than reported by it).
Added ApplyStabilityPatch parameter which toggles off bugfixes, but keep vram adjust feature.

Version 0.280:
First release for Fallout 4, patch may fix some game specific issues. I can't test it
myself, because not suffering from bugs yet, so any reports please post on the forum
of ENBSeries. Added DisableFakeLights to enblocal.ini to remove unrealistic back
lighting for characters.



//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//INSTALL
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
Extract files from folder Patch or WrapperVersion to your game folder, where game
execution file exist.



//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//PROBLEMS
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
If game crashing or work not as expected, make sure you are not running XFire, Afterburner,
EVGA, Steam overlay, screen and video capturing tools, GeForce Experience, Razer and Logitek
utils, other kind of tools and overlays (crapware). Antiviruses and various fake boosters
also may affect the mod.



//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
http://enbdev.com
Copyright (c) 2007-2019 Vorontsov Boris (ENB developer)



//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//USED THIRD PARTY CODE/MIDDLEWARE AND THEIR LICENSES (THESE ARE NOT MOD LICENSES)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
Using AntTweakBar
Copyright (C) 2005-2019 Philippe Decaudin
AntTweakBar web site: http://www.antisphere.com


Using 3Dmigoto
3Dmigoto authors: Chiri, Bo3b Johnson, Ulf Jalmgrant (AKA Flugan), Ian Munsie (AKA DarkStarSword)

The MIT License (MIT)

Copyright (c) 2014-2019 the 3Dmigoto project authors (see the file AUTHORS.txt
for a complete list)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
