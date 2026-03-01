//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//ENBSeries is a set of graphical modifications for games
//Description on the web page may not be equal to this info.
//created by Boris Vorontsov http://enbdev.com
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

PUBLISHING BINARY FILES OF ENBSERIES ON NEXUS SITES (FALLOUT NEXUS, TES NEXUS, ETC)
IS STRICTLY PROHIBITED. ONLY PRESETS AND SHADERS CAN BE HOSTED THERE.



ENBSeries v0.356 for TES Skyrim SE.

Added enbeffectprepass.fx shader which is similar to post pass, but executed before
enbdepthoffield.fx shader.


WARNING! For compatibility with some other software (i call it crapware) read "problems"
category of this document, it's very important to not have any other tools hooking in to
game process or graphics. Crashes, graphic artifacts after installing ENB mods/patches
almost always means crapware is running.

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//CHANGES LOG
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
Version 0.352
Added skylighting effect. Added new type of ssao/ssil effect and increased it's
performance and quality of it's filter.

Version 0.347
Added shader caching to "enbcache" folder to reduce startup time. Can be turned off in
enblocal.ini. Cache is regenerated on any changes made to enblocal.ini or enbseries.ini.
Do not share cache folder with presets.

Version 0.345
Added point light parameters to [ENVIRONMENT] category and game screen space reflection
parameters. Fixed subsurface scattering bugs, game water bug, decreased startup time.

Version 0.343
Added subsurface scattering same as in mod for old Skyrim. Increased performance.
Some minor bugfixes.

Version 0.341
Fixed game shaders to remove limitation of ldr, i didn't notice earlier that game is
forcing ldr. Changed code of animated stars, they use alpha channel to detect stars,
with new parameters fixing incompatibility with custom textures of stars and galaxy.
Added parameters to adjust intensity of game lens flare effect which look more intense
after recent removing of ldr limit.

Version 0.338
Added clouds edge parameters for [SKY] category from old Skyrim mod. Implemented backward
compatibility of all external shaders with VR optimized versions of them which will be
created in future via handling DynamicScaling parameter. Added animated stars parameter
and fixed game code which do not allow full darkness for stars and sky gradient.

Version 0.334
Increased performance in places with many objects visible in camera, these are CPU
optimizations, so they can be seen if you are not bottlenecked by videocard.
Added EnableDenosier parameter to ssao category, similar to old Skyrim mod.
Added edge antialiasing and two dithering types to enblocal.ini, removed dithering
parameter from enbseries.ini.

Version 0.331
Added DisableFakeLights parameter to enblocal.ini for switching on/off character light.
Added game ssao parameters for tweaking its amount and make it colorful. Some bug fixes.

Version 0.330
Added image based lighting effect from old Skyrim mod, fixed bug with clouds transition.
Disabled mod when Creation Kit is running. Added specular parameters for VEGETATION, EYE,
OBJECT categories and improved separation of such objects similar to old Skyrim mod.

Version 0.329
Added cloud shadows effect in its simplest form, few extra fog parameters and rain refraction
control.

Version 0.328
Added parameters for game volumetric rays, underwater and procedural sun.

Version 0.327
Added specular parameters to environment category and rain replacement. Tga rain
drops no longer supported, only dds, png and bmp. Added depth of field toggle hotkey.

Version 0.325
Added ssao/ssil effect. Original by the game is untouched.

Version 0.324
Added fix for reflection of trees in water as FixReflectionTrees parameter in enblocal.ini.
Added WINDOWLIGHT, LIGHTSPRITE, VOLUMETRICFOG, FIRE, PARTICLE parameters which mostly
similar to old Skyrim version, but not all objects yet handled and I'll keep searching
for missed.

Version 0.320
Added directional light parameters, aurora borealis, stars, clouds, sky gradient parameters.
Some compatibility fix for old AMD videocards.

Version 0.310
Added time of the day, interior and exterior detection. Fixed bug with not worked adaptation.
Added support of loading multiple plugins (.dllplugin extention) from "enbseries" folder.

Version 0.309
First version for Skyrim SE. Made all post processing shaders similar or equal to mod
for Fallout 4. Time of the day, interior and exterior separation, weather system are
not available yet. This version is mostly to remove original game post processing by
replacing it with custom from enbeffect.fx shader (untweaked, most code from old Skyrim
mod) or ReShade/SweetFX.




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
Copyright (c) 2007-2018 Vorontsov Boris (ENB developer)



//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//USED THIRD PARTY CODE/MIDDLEWARE AND THEIR LICENSES (THESE ARE NOT MOD LICENSES)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
Using AntTweakBar
Copyright (C) 2005-2018 Philippe Decaudin
AntTweakBar web site: http://www.antisphere.com


Using 3Dmigoto
3Dmigoto authors: Chiri, Bo3b Johnson, Ulf Jalmgrant (AKA Flugan), Ian Munsie (AKA DarkStarSword)

The MIT License (MIT)

Copyright (c) 2014-2018 the 3Dmigoto project authors (see the file AUTHORS.txt
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
