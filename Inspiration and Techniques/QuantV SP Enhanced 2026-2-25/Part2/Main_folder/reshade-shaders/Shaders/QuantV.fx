//QuantV ReShade effect

#define E __RENDERER__ != 0xc000
#define C __APPLICATION__ != 1226744222
#define FA __APPLICATION__ != -1664759584
#if C
uniform float2 Corona_Light_Intensity < ui_label = "Intensity"; ui_text = "Intensity  (Global  |  Emergency Vehicles)"; ui_category = "CORONA LIGHTS"; ui_type = "drag"; ui_tooltip = "Global  |  Emergency Vehicles\n\nGlobal: Intensity of all corona lights (streetlights, vehicles, etc)\nEmergency Vehicles: Intensity of emergency vehicle's corona lights only"; ui_min = 0.001; ui_max = 500.0; > = float2(1.0, 1.5);
#if E
uniform int Corona_Light_Style < ui_label = "Style"; ui_category = "CORONA LIGHTS"; ui_type = "combo"; ui_items = "Smooth Rays\0Rays V3\0Thin Rays\0"; > = 0;
#endif
uniform int CoronaLightToggle < ui_label = "Toggle"; ui_category = "CORONA LIGHTS"; ui_type = "combo"; ui_items = "Default ON\0Disable from Vehicles\0Disable Everywhere\0"; > = 0;
uniform int sep0 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;
#if E
uniform bool Streetlights < ui_label = "Use Streetlights"; ui_category = "STREETLIGHTS"; ui_category_toggle = true; > = false;
uniform float Streetlights_Mult < ui_label = "Intensity"; ui_category = "STREETLIGHTS"; ui_type = "drag"; ui_min = 0.001; ui_max = 10.0; > = 1.0;
uniform float3 Streetlights_Col < ui_label = "Color"; ui_category = "STREETLIGHTS"; ui_type = "color"; > = float3(1.0, 0.41, 0.0);
uniform float Streetlights_Reflect < ui_label = "Reflect Intensity"; ui_category = "STREETLIGHTS"; ui_type = "drag"; ui_min = 0.001; ui_max = 100.0; > = 1.0;
uniform int sep1 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;
#endif
#endif
uniform int ScreenRaindrops < ui_label = "Style"; ui_category = "SCREEN RAINDROPS"; ui_type = "combo"; ui_items = "Disabled\0Classic\0Racing\0NFS\0"; > = 2;
uniform bool Screen_Raindrops_Force_Enable < ui_label = " > Force Enable"; ui_category = "SCREEN RAINDROPS"; ui_tooltip = "Enables Screen Raindrops all the time, even in sunny weathers"; > = false;
uniform float Screen_Raindrops_Scale < ui_label = "Scale"; ui_category = "SCREEN RAINDROPS"; ui_tooltip = "Scale size of the droplets"; ui_type = "drag"; ui_min = 0.25; ui_max = 10.0; > = 1.0;
uniform bool Screen_Frozen_Snow < ui_label = "Screen Frozen Snow"; ui_spacing = 2; ui_category = "SCREEN RAINDROPS"; > = false;
uniform bool Screen_Frozen_Snow_ForceEnable < ui_label = " > Force Enable"; ui_category = "SCREEN RAINDROPS"; > = false;
#if C
uniform int sep2 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;
uniform float2 vehSpecIntensity < ui_label = "Specular Intensity"; ui_tooltip = "DAY    |    NIGHT    (a.k.a Reflection Amount)"; ui_category = "VEHICLE"; ui_type = "drag"; ui_min = 0.001; > = float2(1.1, 1.2);
uniform float2 vehFresnel < ui_label = "Specular Fresnel"; ui_tooltip = "DAY    |    NIGHT"; ui_category = "VEHICLE"; ui_type = "drag"; ui_min = 0.001; > = float2(0.98, 0.95);
#if E
uniform float2 vehFalloff < ui_label = "Specular Falloff"; ui_tooltip = "DAY    |    NIGHT    (a.k.a Reflection Glossiness)"; ui_category = "VEHICLE"; ui_type = "drag"; ui_min = 0.001; > = float2(1.0, 1.0);
#endif
uniform float2 vehMask < ui_label = "Specular Mask"; ui_tooltip = "DAY    |    NIGHT    (a.k.a Metallic Amount)"; ui_category = "VEHICLE"; ui_type = "drag"; ui_min = 0.001; > = float2(1.3, 1.5);
#if E
uniform int Vehicle_Paint < ui_label = "Paint Shader"; ui_category = "VEHICLE"; ui_type = "combo"; ui_items = "Default Paint\0Metallic Paint (Pearlescent)\0Iridescent (Metal)\0Iridescence Wrap (Pearlescent/Matte/Metal)\0Carbon Fiber (Pearlescent/Matte)\0Opal (Pearlescent/Matte/Metal)\0Brushed Metal (Pearlescent/Metal/Chrome)\0Colored Chrome (Chrome)\0"; ui_spacing = 2; > = 0;
uniform bool Vehicle_Carbon < ui_label = "Custom Carbon Fiber"; ui_category = "VEHICLE"; ui_spacing = 2; > = true;
uniform bool Use_Colored_Carbon_Fiber < ui_label = "Use Colored Carbon Fiber"; ui_category = "VEHICLE"; > = false;
uniform float3 Color_Carbon_Fiber < ui_label = "Colored Carbon Fiber"; ui_type = "color"; ui_category = "VEHICLE"; > = float3(0.01, 0.01, 0.01);
#endif
#if E
uniform float2 vehGlass < ui_label = "Glass Reflection"; ui_tooltip = "DAY    |    NIGHT"; ui_category = "VEHICLE"; ui_type = "drag"; ui_min = 0.001; ui_max = 10.0; ui_spacing = 2; > = float2(1.8, 2.3);
#else
uniform float2 vehGlass < ui_label = "Glass Reflection"; ui_tooltip = "DAY    |    NIGHT"; ui_category = "VEHICLE"; ui_type = "drag"; ui_min = 0.001; ui_max = 10.0; ui_spacing = 2; > = float2(1.25, 1.7);
#endif
uniform float vehGlassTint < ui_label = "Glass Tint Opacity"; ui_category = "VEHICLE"; ui_type = "drag"; ui_min = 0.001; ui_max = 10.0; > = 1.01;
#if E
uniform bool Black_Chrome < ui_label = "Black Chrome"; ui_category = "VEHICLE"; ui_spacing = 2; > = false;
#endif
uniform float2 Vehicle_Chrome_Reflection < ui_label = "Chrome Reflection"; ui_tooltip = "DAY    |    NIGHT"; ui_category = "VEHICLE"; ui_type = "drag"; ui_min = 0.001; > = float2(1.1, 1.4);
uniform float2 Vehicle_Chrome_Specular < ui_label = "Chrome Specular"; ui_tooltip = "DAY    |    NIGHT"; ui_category = "VEHICLE"; ui_type = "drag"; ui_min = 0.001; > = float2(1.0, 1.0);
#if E
uniform float2 Vehicle_Wheel_Specular < ui_label = "Wheels Specular"; ui_tooltip = "DAY    |    NIGHT"; ui_category = "VEHICLE"; ui_type = "drag"; ui_min = 0.001; ui_spacing = 2; > = float2(1.0, 1.0);
uniform float2 Vehicle_Rims_Reflection < ui_label = "Rims Reflection"; ui_tooltip = "DAY    |    NIGHT"; ui_category = "VEHICLE"; ui_type = "drag"; ui_min = 0.001; > = float2(1.05, 1.3);
uniform float2 Vehicle_Tire_Reflection < ui_label = "Tire Reflection"; ui_tooltip = "DAY    |    NIGHT"; ui_category = "VEHICLE"; ui_type = "drag"; ui_min = 0.001; > = float2(1.0, 1.0);
uniform float2 Vehicle_Tire_Wetness < ui_label = "Tire Wetness"; ui_tooltip = "DAY    |    NIGHT"; ui_category = "VEHICLE"; ui_type = "drag"; ui_min = 0.001; > = float2(1.1, 1.1);
uniform bool Vehicle_Wheel_Chrome_Color < ui_label = "Wheel Chrome Color (pearlescent)"; ui_category = "VEHICLE"; ui_tooltip = "Paint 3. Pearlescent Color"; > = false;
uniform int Vehicle_Wheel_HotDisc < ui_label = "Wheel Brake Disc"; ui_category = "VEHICLE"; ui_type = "combo"; ui_items = "default\0Hot\0Very Hot\0"; ui_tooltip = "(Player must be inside the vehicle)"; > = 0;
uniform bool Vehicle_Raindrops < ui_label = "Vehicle Raindrops"; ui_category = "VEHICLE"; ui_tooltip = "Vehicle Rain Drops effect V2"; ui_spacing = 2; > = true;
uniform float Vehicle_Raindrops_Scale < ui_label = "Veh.Raindrops Size"; ui_category = "VEHICLE"; ui_type = "drag"; ui_min = 0.5; ui_max = 2.0; ui_tooltip = "Droplets Scale Size"; > = 1.0;
uniform float Vehicle_Raindrops_Speed < ui_label = "Veh.Raindrops Speed"; ui_category = "VEHICLE"; ui_type = "drag"; ui_min = 0.011; ui_max = 7.0; ui_tooltip = "Droplets Animation Speed"; > = 1.0;
uniform float Vehicle_Raindrops_Ref < ui_label = "Veh.Raindrops Ref Intensity"; ui_category = "VEHICLE"; ui_type = "drag"; ui_min = 0.011; ui_max = 2.0; ui_tooltip = "Droplets Light Refraction Intensity"; > = 1.0;
uniform bool Vehicle_Rain_ForceEnable < ui_label = " > Force Enable"; ui_category = "VEHICLE"; > = false;
uniform int2 Vehicle_Snow < ui_label = "Vehicle Snow"; ui_text = "Vehicle Snow   (Bottom     |    Roof)"; ui_type = "slider"; ui_category = "VEHICLE"; ui_min = 0; ui_max = 1; ui_tooltip = "Bottom Snow    |    Roof Snow\n\nBottom: Snow on wheels and lower body\nRoof: Snow on roof and upper surfaces"; ui_spacing = 2; > = int2(1, 0);
uniform bool Vehicle_Snow_Force < ui_label = " > Force Enable"; ui_category = "VEHICLE"; ui_tooltip = "Make sure your vehicle isn't 100% clean ;)"; > = false;
uniform int sep3 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;
uniform bool Vehicle_Glass_Refraction < ui_label = "GlassLight Refraction effect"; ui_category = "VEHICLE LIGHTS"; > = true;
uniform int Vehicle_Emergency_Lights < ui_label = "Emergency Lights Preset"; ui_category = "VEHICLE LIGHTS"; ui_type = "combo"; ui_items = "default\0Bright\0Very Bright\0"; ui_spacing = 2; > = 1;
uniform float2 Emissive_Global < ui_label = "Emissive Global"; ui_tooltip = "DAY    |    NIGHT"; ui_category = "VEHICLE LIGHTS"; ui_type = "drag"; ui_min = 0.001; ui_max = 300.0; ui_spacing = 2; > = float2(1.3, 1.3);
uniform float2 Emissive_Emergency < ui_label = "Emissive Emergency"; ui_tooltip = "DAY    |    NIGHT"; ui_category = "VEHICLE LIGHTS"; ui_type = "drag"; ui_min = 0.001; ui_max = 300.0; > = float2(3.4, 3.1);
uniform float2 Emissive_Headlight < ui_label = "Emissive Headlight"; ui_tooltip = "DAY    |    NIGHT"; ui_category = "VEHICLE LIGHTS"; ui_type = "drag"; ui_min = 0.001; ui_max = 300.0; > = float2(1.0, 1.0);
uniform float2 Emissive_Taillight < ui_label = "Emissive Taillight"; ui_tooltip = "DAY    |    NIGHT"; ui_category = "VEHICLE LIGHTS"; ui_type = "drag"; ui_min = 0.001; ui_max = 300.0; > = float2(1.0, 1.0);
uniform float Emissive_Indicator < ui_label = "Emissive Indicator"; ui_category = "VEHICLE LIGHTS"; ui_type = "drag"; ui_min = 0.001; ui_max = 300.0; > = 1.0;
uniform float Headlight_Beam < ui_label = "Headlight Beam Intensity"; ui_category = "VEHICLE LIGHTS"; ui_type = "drag"; ui_min = 0.001; ui_max = 10.0; ui_spacing = 2; > = 1.0;
uniform float Headlight_Beam_Distance < ui_label = "Headlight Beam Distance"; ui_category = "VEHICLE LIGHTS"; ui_type = "drag"; ui_min = 0.001; ui_max = 2.0; > = 1.0;
uniform float Taillight_Beam < ui_label = "Taillight Beam Intensity"; ui_category = "VEHICLE LIGHTS"; ui_type = "drag"; ui_min = 0.001; ui_max = 10.0; > = 1.0;
uniform float4 Vehicle_Headlight_Tint < ui_label = "Headlight Tint"; ui_category = "VEHICLE LIGHTS"; ui_type = "color"; ui_spacing = 2; > = float4(1.0, 1.0, 1.0, 0.0);
uniform float4 Vehicle_Taillight_Tint < ui_label = "Taillight Tint"; ui_category = "VEHICLE LIGHTS"; ui_type = "color"; > = float4(1.0, 0.2, 0.0, 0.0);
uniform float Emergency_Reflect < ui_label = "Emergency Reflect Intensity"; ui_category = "VEHICLE LIGHTS"; ui_type = "drag"; ui_min = 0.001; ui_max = 30.0; ui_spacing = 2; > = 1.0;
uniform float Emergency_Reflect_Far < ui_label = "Emergency Reflect Distance"; ui_category = "VEHICLE LIGHTS"; ui_type = "slider"; ui_min = 0.001; ui_max = 2.0; > = 1.0;
uniform bool Vehicle_Indicator_Animation < ui_label = "Indicator Led Animation"; ui_category = "VEHICLE LIGHTS"; ui_spacing = 2; > = false;
uniform int Animation_Profile < ui_label = "Indicator Animation Profile"; ui_tooltip = "if default animation doesn't match vehicle's animation then switch to the correct profile"; ui_category = "VEHICLE LIGHTS"; ui_type = "combo"; ui_items = "Default (auto)\0Profile#1\0Profile#2\0Profile#3\0Profile#4\0Profile#5\0Profile#6\0Profile#B1\0Profile#B2\0Profile#B3\0"; > = 0;
uniform bool Vehicle_DRL_Indicator < ui_label = "DRL Indicator"; ui_tooltip = "Indicator - Daytime Running Lights"; ui_category = "VEHICLE LIGHTS"; > = false;
uniform bool Vehicle_Lights_Flicker < ui_label = "Lights Flicker"; ui_category = "VEHICLE LIGHTS"; ui_tooltip = "Stroboscopic effect - Vehicle lights flickering in the camera for cinematic effect"; ui_spacing = 2; > = false;
#endif
uniform int sep4 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;
#if E
uniform float2 AmbientOcclusion < ui_label = "Ambient Occlusion"; ui_text = "AO:  (Intensity  |  Quality)"; ui_tooltip = "Ambient Occlusion:\n\nIntensity    |    Quality"; ui_category = "LIGHTING"; ui_type = "drag"; ui_min = 0.01; ui_max = 2.0; > = float2(1.16, 1.3);
uniform float2 Shadows < ui_label = "Local Shadows"; ui_text = "Shadows:  (Vegetation  |  ParticleFX)"; ui_tooltip = "Vegetation    |    ParticleFX"; ui_category = "LIGHTING"; ui_type = "drag"; ui_min = 0.001; ui_max = 3.0; ui_spacing = 1; > = float2(1.0, 1.5);
#else
uniform float AmbientOcclusion < ui_label = "Ambient Occlusion"; ui_text = "AO:"; ui_tooltip = "AO Intensity"; ui_category = "LIGHTING"; ui_type = "drag"; ui_min = 0.01; ui_max = 2.0; > = 1.2;
uniform float Shadows < ui_label = "Local Shadows"; ui_text = "Shadows:  (Vegetation)"; ui_tooltip = "Vegetation"; ui_category = "LIGHTING"; ui_type = "drag"; ui_min = 0.001; ui_max = 3.0; ui_spacing = 1; > = 1.0;
#endif
#if E
uniform int sep5 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;
uniform bool ParticleFX < ui_category = "PARTICLE FX"; ui_tooltip = "Important:\nNot compatible with BloodFX mods"; > = true;
uniform float2 ExhaustFireSettings < ui_label = "Exhaust Fire"; ui_text = "Intensity      |      Size/Scale    "; ui_type = "drag"; ui_min = 0.001; ui_max = 3.0; ui_category = "PARTICLE FX"; ui_tooltip = "Exhaust Fire-Nitrous"; > = float2(1.0, 1.0);
uniform float3 ExhaustColor < ui_label = "Exhaust Fire Color"; ui_type = "color"; ui_category = "PARTICLE FX"; ui_tooltip = "Exhaust Fire-Nitrous"; ui_tooltip = "Backfire Color\n(black = original color)"; > = float3(0.0, 0.0, 0.0);
uniform float2 ExhaustSmoke < ui_label = "Exhaust Smoke"; ui_text = "Smoke Density     |     Smoke Size"; ui_type = "drag"; ui_min = 0.02; ui_max = 10.0; ui_category = "PARTICLE FX"; ui_tooltip = "Exhaust Fire-Nitrous"; ui_spacing = 1; > = float2(1.0, 1.0);
uniform int Vehicle_Burnout < ui_type = "slider"; ui_label = "Vehicle Burnout"; ui_min = 0; ui_max = 2; ui_category = "PARTICLE FX"; ui_spacing = 2; > = 1;
uniform int Vehicle_Burnout_Density < ui_type = "slider"; ui_label = "Vehicle Burnout Density"; ui_min = 0; ui_max = 2; ui_category = "PARTICLE FX"; > = 0;
uniform float Vehicle_Burnout_Bright < ui_label = "Vehicle Burnout Bright"; ui_type = "drag"; ui_min = 0.001; ui_max = 3.0; ui_category = "PARTICLE FX"; > = 1.0;
uniform float Vehicle_Burnout_Scale < ui_label = "Vehicle Burnout Scale"; ui_type = "drag"; ui_min = 0.001; ui_max = 10.0; ui_category = "PARTICLE FX"; > = 1.0;
uniform float3 Vehicle_Burnout_Color < ui_label = "Vehicle Burnout Color"; ui_type = "color"; ui_category = "PARTICLE FX"; > = float3(1.0, 1.0, 1.0);
uniform float Vehicle_Engine_Fire < ui_label = "Damaged Engine Fire Intensity"; ui_type = "drag"; ui_min = 0.001; ui_max = 10.0; ui_category = "PARTICLE FX"; ui_spacing = 2; > = 1.0;
uniform float Vehicle_Engine_Smoke < ui_label = "Damaged Engine Smoke Density"; ui_type = "drag"; ui_min = 0.001; ui_max = 10.0; ui_category = "PARTICLE FX"; > = 1.0;
uniform float Vehicle_Engine_Smoke_Scale < ui_label = "Damaged Engine Smoke Scale"; ui_type = "drag"; ui_min = 0.001; ui_max = 8.0; ui_category = "PARTICLE FX"; > = 1.0;
uniform float Gun_Muzzle_Flash_Intensity < ui_label = "Gun Muzzle Flash Intensity"; ui_type = "drag"; ui_min = 0.001; ui_max = 6.0; ui_category = "PARTICLE FX"; ui_spacing = 2; > = 1.0;
uniform float Gun_Muzzle_Flash_Scale < ui_label = "Gun Muzzle Flash Scale"; ui_type = "drag"; ui_min = 0.001; ui_max = 8.0; ui_category = "PARTICLE FX"; > = 1.0;
uniform float Gun_Muzzle_Smoke_Bright < ui_label = "Gun Muzzle Smoke Bright"; ui_type = "drag"; ui_min = 0.001; ui_max = 6.0; ui_category = "PARTICLE FX"; > = 1.0;
uniform float Gun_Muzzle_Smoke_Density < ui_label = "Gun Muzzle Smoke Density"; ui_type = "drag"; ui_min = 0.001; ui_max = 6.0; ui_category = "PARTICLE FX"; > = 1.0;
uniform float Gun_Muzzle_Smoke_Scale < ui_label = "Gun Muzzle Smoke Scale"; ui_type = "drag"; ui_min = 0.001; ui_max = 10.0; ui_category = "PARTICLE FX"; > = 1.0;
uniform float Explosion_Intensity < ui_label = "Explosion Intensity"; ui_type = "drag"; ui_min = 0.001; ui_max = 10.0; ui_category = "PARTICLE FX"; ui_spacing = 2; > = 1.0;
uniform float Explosion_Scale < ui_label = "Explosion Scale"; ui_type = "drag"; ui_min = 0.001; ui_max = 10.0; ui_category = "PARTICLE FX"; > = 1.0;
uniform int Blood_Puddle < ui_type = "combo"; ui_label = "Blood Puddles"; ui_items = "Texture Glossiness\0Procedural\0Texture Glossiness + Procedural\0"; ui_category = "PARTICLE FX"; ui_spacing = 2; > = 0;
uniform float Snowflake_Intensity < ui_label = "Snowflake Intensity"; ui_type = "drag"; ui_min = 0.001; ui_max = 10.0; ui_category = "PARTICLE FX"; ui_spacing = 2; > = 1.0;
#endif
uniform int sep6 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;
uniform int Vegetation < ui_label = "Vegetation Season"; ui_category = "VEGETATION"; ui_type = "combo"; ui_items = "Spring\0Summer (default)\0Autumn1\0Autumn2\0Autumn3\0Autumn4\0Winter\0"; > = 0;
uniform bool CherryBlossom < ui_label = "Cherry Blossom"; ui_category = "VEGETATION"; ui_tooltip = "Transform your trees into Sakura trees"; > = false;
uniform bool VegetationSharp < ui_label = "Vegetation Sharpen"; ui_category = "VEGETATION"; > = true;
#if E
uniform bool Vegetation_Complex_Shadows < ui_label = "Vegetation Complex Shadows"; ui_category = "VEGETATION"; ui_tooltip = "Experimental Complex Shadows on grass / bushes / trees"; > = false;
uniform float Grass_Fur < ui_label = "Grass Fur Density"; ui_type = "drag"; ui_min = 0.001; ui_max = 9.0; ui_tooltip = "Fur Grass Density (default = 1.0)"; ui_category = "VEGETATION"; > = 2.0;
uniform bool Grass_Detail < ui_label = "Grass Detail Map"; ui_category = "VEGETATION"; > = true;
uniform int sep7 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;
uniform bool Enable_MB_in_FPV < ui_label = "Enable MotionBlur while driving in FPV"; ui_category = "MOTION BLUR"; ui_tooltip = "Re-enable motion blur in FPV\nRequires Motion Blur enabled"; > = false;
uniform bool Disable_MB < ui_label = "Disable MotionBlur"; ui_category = "MOTION BLUR"; > = false;
#if FA
uniform int sep8 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;
uniform bool Server_Overbloom_Fix < ui_label = "Servers with Over-Bloom Fix"; ui_tooltip = "Workaround for FiveM servers that overwrites visualsettings.dat\ncausing too much bloom on bright surfaces"; ui_category = "FiveM Specific Workarounds"; > = false;
#endif
#endif
uniform int sep9 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;
#if E
uniform bool GodRay < ui_label = "GodRays"; ui_category = "LENS FLARE"; ui_tooltip = "Detailed Volumetric Sun&Moon Light Rays"; > = false;
uniform float GodRayIntensity < ui_label = "GodRay Intensity"; ui_type = "drag"; ui_min = 0.0; ui_max = 2.0; ui_category = "LENS FLARE"; > = 1.0;
uniform float LightShaftsIntensity < ui_label = "GodRay LightShafts Intensity"; ui_type = "drag"; ui_min = 0.0; ui_max = 2.0; ui_category = "LENS FLARE"; > = 1.0;
#endif
uniform bool GPLens < ui_label = "GPLens"; ui_category = "LENS FLARE"; ui_tooltip = "GP's lensflare"; ui_spacing = 3; > = false;
uniform float GPLens_Intensity < ui_label = "GPLens Intensity"; ui_type = "drag"; ui_min = 0.001; ui_max = 10.0; ui_category = "LENS FLARE"; > = 1.0;
#if E
uniform bool SixLens < ui_label = "SixLens"; ui_category = "LENS FLARE"; ui_tooltip = "Lensflare partially inspired by 'Six' #1 trailer"; ui_spacing = 2; > = false;
uniform float SixLensIntensity < ui_label = "Lens Flare Intensity"; ui_category = "LENS FLARE"; ui_type = "drag"; ui_min = 0.0; ui_max = 8.0; > = 1.0;
#endif
uniform int lightStreak < ui_label = "Light Streak"; ui_category = "LENS FLARE"; ui_type = "combo"; ui_items = "Disabled\0StreakX4\0StreakX6\0StreakX8\0FH5\0VI\0"; ui_tooltip = "Starburst / Diffraction Spikes\nEffect produced by bright surfaces radiating outwards in a star-like pattern"; ui_spacing = 2; > = 2;
uniform float lightStreakIntensity < ui_label = "LightStreak:: Intensity"; ui_category = "LENS FLARE"; ui_type = "drag"; ui_min = 0.0; ui_max = 3.0; > = 1.0;
uniform float lightStreakThreshold < ui_label = "LightStreak:: Threshold"; ui_category = "LENS FLARE"; ui_type = "drag"; ui_min = 0.25; ui_max = 4.0; > = 1.0;
uniform float lightStreakSize < ui_label = "LightStreak:: Size"; ui_category = "LENS FLARE"; ui_type = "drag"; ui_min = 0.25; ui_max = 1.0; > = 0.85;
uniform float lightStreakCA < ui_label = "LightStreak:: CA"; ui_category = "LENS FLARE"; ui_type = "drag"; ui_min = 0.01; ui_max = 2.0; ui_tooltip = "Airy Disk Chromatic Aberration Intensity"; > = 1.0;
#if E
uniform float Anamorphic_Lens_Global < ui_label = "Anamorphic flare Global"; ui_category = "LENS FLARE"; ui_type = "drag"; ui_min = 0.0; ui_max = 60.0; ui_tooltip = "Anamorphic lensflare produced by bright spots"; ui_spacing = 3; > = 1.0;
uniform float Anamorphic_Lens_Lights < ui_label = "Anamorphic flare Lights"; ui_category = "LENS FLARE"; ui_type = "drag"; ui_min = 0.0; ui_max = 600.0; ui_tooltip = "Anamorphic lensflare produced by lights sources"; > = 1.0;
uniform float Anamorphic_Lens_EmergencyLights < ui_label = "Anamorphic flare Emergency Lights"; ui_category = "LENS FLARE"; ui_type = "drag"; ui_min = 0.0; ui_max = 600.0; ui_tooltip = "Anamorphic lensflare produced by emergency lights"; > = 1.9;
uniform int Anamorphic_Style < ui_label = "Anamorphic Style"; ui_category = "LENS FLARE"; ui_type = "combo"; ui_items = "default\0QuantV\0"; ui_tooltip = "Anamorphic lensflare (lights) Style"; > = 1;
uniform int sep10 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;
uniform bool Brighter_Nights < ui_label = "Brighter Nights"; > = false;
uniform bool Smooth_Night_Shadows < ui_label = "Smooth Night Shadows"; > = false;
#endif
uniform int sep11 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;
#if E
uniform bool Volumetric_Clouds < ui_label = "Clouds (OUTDATED)"; ui_category = "SKY"; ui_tooltip = "Outdated - Not recommended\nUse V2 instead"; > = false;
#endif
uniform bool Clouds < ui_label = "Clouds V2"; ui_category = "SKY"; ui_tooltip = "Volumetric Clouds"; > = true;
uniform float2 CloudsDensity < ui_label = "Clouds:: Density"; ui_text = "Bottom        |        Top"; ui_category = "SKY"; ui_type = "drag"; ui_min = 0.0; ui_max = 1.9; ui_tooltip = "Volumetric Clouds Density\nBottom     |    Top"; > = float2(1.0, 1.0);
#if E
uniform int CloudsType < ui_label = "Clouds:: Type"; ui_category = "SKY"; ui_type = "combo"; ui_items = "Performance\0Detail1 (Default)\0Detail2\0"; ui_tooltip = "Volumetric Clouds Performance/Detail presets"; > = 1;
uniform int CloudsReflections < ui_label = "Clouds:: Reflections"; ui_category = "SKY"; ui_type = "combo"; ui_items = "Disabled\0Performance\0Detail Max (Default)\0"; ui_tooltip = "Volumetric Clouds Reflections"; > = 0;
#else
uniform int CloudsType < ui_label = "Clouds:: Type"; ui_category = "SKY"; ui_type = "combo"; ui_items = "Performance\0Detail (Default)\0"; > = 1;
uniform int CloudsReflections < ui_label = "Clouds:: Reflections"; ui_category = "SKY"; ui_type = "combo"; ui_items = "Disabled\0Performance\0Detail\0"; > = 0;
#endif
uniform float3 HighClouds < ui_text = "Cirrocumulus    |    Cirrus    |    Contrail"; ui_label = "High Clouds Intensity"; ui_type = "drag"; ui_min = 0.0; ui_max = 10.0; ui_category = "SKY"; ui_tooltip = "Cirrocumulus    |    Cirrus    |    Contrail"; ui_spacing = 1; > = float3(1.0, 1.0, 1.0);
uniform bool Starfield_Galaxy < ui_label = "Starfield Galaxy"; ui_category = "SKY"; ui_spacing = 3; > = false;
uniform int Galaxy_Color < ui_label = "Color Type"; ui_category = "SKY"; ui_type = "combo"; ui_items = "Default Realistic\0RGB\0Single Color (Tweakable)\0"; > = 0;
uniform float4 Galaxy_Tint < ui_label = "Single Color"; ui_type = "color"; ui_category = "SKY"; ui_tooltip = "Requires Color Type: 'Single Color'"; > = float4(0.4, 0.2, 0.02, 0.6);
uniform float3 Galaxy_Rotation < ui_label = "Rotation Position"; ui_type = "drag"; ui_min = -3.1416; ui_max = 3.1416; ui_tooltip = " X  |  Y  |  Z "; ui_category = "SKY"; > = float3(0.0, 0.0, 0.0);
uniform float2 Starfield_Intensity < ui_label = "Stars Intensity"; ui_text = "Intensity:  Starfield  |  Shooting Stars"; ui_type = "drag"; ui_tooltip = "Starfield   |   Shooting Stars Intensity\n\nStarfield: Overal starfield intensity multiplier\nShooting Stars: Only comets intensity."; ui_min = 0.0; ui_max = 10.0; ui_category = "SKY"; ui_spacing = 2; > = float2(1.0, 0.22);
#if E
uniform bool Aurora_Borealis < ui_label = "Aurora Borealis"; ui_category = "SKY"; ui_spacing = 3; > = false;
uniform float Aurora_Borealis_Intensity < ui_label = "Aurora Borealis Intensity"; ui_category = "SKY"; ui_type = "drag"; ui_min = 0.0; ui_max = 6.0; > = 1.0;
uniform float3 Aurora_Borealis_Color_Low < ui_label = "Aurora Borealis Color Low"; ui_type = "color"; ui_category = "SKY"; > = float3(0.0, 0.88, 0.63);
uniform float3 Aurora_Borealis_Color_High < ui_label = "Aurora Borealis Color High"; ui_type = "color"; ui_category = "SKY"; > = float3(0.59, 0.0, 1.0);
uniform int sep12 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;
uniform bool Dusty_Wind < ui_label = "Volumetric Dusty Wind - Desert Area - Blizzard - Snow"; ui_tooltip = "Experimental effect with sun&moon scattering light"; > = false;
#endif
uniform int sep13 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;
uniform float HDR_Reflection < ui_label = "HDR Reflections"; ui_type = "drag"; ui_min = 0.01; ui_max = 10.0; ui_category = "ROADS & SURFACES"; ui_tooltip = "Environment HDR Reflections"; > = 1.0;
#if E
uniform bool Road < ui_label = "Roads Overhaul"; ui_category = "ROADS & SURFACES"; ui_tooltip = "Work In Progress..."; ui_spacing = 2; > = false;
uniform int Wet_Roads < ui_label = "Wet Roads"; ui_category = "ROADS & SURFACES"; ui_type = "combo"; ui_items = "very low wetness\0low wetness\0default wetness (vanilla)\0high wetness\0very high wetness\0"; > = 2;
uniform bool Puddle_Reflection < ui_label = "Reflections (SSR)"; ui_category = "ROADS & SURFACES"; ui_tooltip = "Screen Space Reflections"; ui_spacing = 2; > = true;
uniform float Puddle_Reflection_Quality < ui_label = "Reflection Quality"; ui_type = "drag"; ui_min = 0.5; ui_max = 8.0; ui_category = "ROADS & SURFACES"; ui_tooltip = "Puddle Reflections Quality"; > = 1.0;
#endif
uniform float Puddle_Scale < ui_label = "Puddle Scale"; ui_type = "drag"; ui_min = 0.0; ui_max = 9.9; ui_category = "ROADS & SURFACES"; ui_tooltip = "Puddles Scale Size"; > = 1.0;
uniform bool Puddles_Covering_Full_Ground < ui_label = "Puddles Covering Full Ground"; ui_category = "ROADS & SURFACES"; ui_tooltip = "Floor looks like a mirror when its raining"; > = false;
#if E
uniform int Snow < ui_label = "Snow Vanilla"; ui_type = "combo"; ui_items = "Vanilla\0Snow Mod - Full Cover\0Snow Mod - Melting Puddles on Roads\0Snow Mod - Clean Roads\0"; ui_category = "ROADS & SURFACES"; ui_tooltip = "Modification of existing snow\nRequires game snow enabled"; ui_spacing = 2; > = 0;
uniform int QV_Snow < ui_label = "Snow QV (additional)"; ui_type = "combo"; ui_items = "Disabled\0Full Cover\0Clean Roads\0Melting Puddles on Roads\0"; ui_category = "ROADS & SURFACES"; ui_tooltip = "Custom Snow Shader (Independent)\nDoes not requires game snow"; > = 0;
uniform float2 Snow_Altitude_FadeInOut < ui_label = "Altitude Fade In & Out"; ui_type = "drag"; ui_min = 0.0; ui_max = 1000.0; ui_category = "ROADS & SURFACES"; ui_tooltip = "Snow Altitude\nFade IN    |    Fade OUT\n\nexample:\n1.2-1.3 beach shore\n360-560 mountain top"; > = float2(1.2, 1.3);
uniform float Snow_Amount < ui_label = "Snow Amount"; ui_type = "drag"; ui_min = 0.0; ui_max = 1.0; ui_category = "ROADS & SURFACES"; > = 1.0;
uniform float BuildingEmissive < ui_label = "Building Emissive Lights"; ui_type = "drag"; ui_min = 0.0; ui_max = 3.0; ui_category = "ROADS & SURFACES"; ui_spacing = 2; > = 1.1;
#endif
uniform float2 BuildingReflection < ui_label = "Reflection Fresnel"; ui_text = "Building Reflection:  Global   |   Glass"; ui_type = "drag"; ui_min = 0.0; ui_max = 3.0; ui_category = "ROADS & SURFACES"; ui_tooltip = "Building Global    |    Building Glass (windows)\n\ndefault: 1.0\nmirror: 0.0"; > = float2(0.99, 0.6);
#if E
uniform float BuildingReflectionSharp < ui_label = "Reflection Sharp"; ui_text = "Glass Reflection Sharp"; ui_type = "drag"; ui_min = 0.0; ui_max = 3.0; ui_category = "ROADS & SURFACES"; > = 2.0;
uniform int sep14 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;
uniform float4 Water_Color < ui_label = "Color Tint"; ui_type = "color"; ui_category = "WATER"; > = float4(0.0, 0.5, 0.7, 0.4);
uniform float Water_Transparency < ui_label = "Transparency"; ui_type = "slider"; ui_min = 0.0; ui_max = 0.9; ui_category = "WATER"; > = 0.3;
#endif
uniform int sep15 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;
uniform float SSS_Intensity < ui_label = "SSS Intensity"; ui_type = "drag"; ui_min = 0.001; ui_max = 1.0; ui_category = "PLAYER SUBSURFACE SCATTERING"; ui_tooltip = "SubSurface Scattering Intensity"; > = 0.9;
uniform float SSS_Radius < ui_label = "SSS Radius"; ui_type = "drag"; ui_min = 0.001; ui_max = 4.0; ui_category = "PLAYER SUBSURFACE SCATTERING"; ui_tooltip = "SubSurface Scattering Radius"; > = 1.0;
uniform float SSS_Saturation < ui_label = "SSS Saturation"; ui_type = "drag"; ui_min = 0.1; ui_max = 1.8; ui_category = "PLAYER SUBSURFACE SCATTERING"; ui_tooltip = "SubSurface Scattering Diffuse Saturation"; > = 1.0;
#if E
uniform float2 SSS_EpidermalSubdermal < ui_text = "Scattering: Epidermal   |   Subdermal"; ui_label = "Shallow   |   Deep Scattering"; ui_type = "drag"; ui_min = 0.01; ui_max = 4.0; ui_category = "PLAYER SUBSURFACE SCATTERING"; ui_tooltip = "Epidermal Shallow Scattering   |   Subdermal Deep Scattering"; > = float2(1.0, 1.0);
uniform int sep16 < ui_label = " "; ui_type = "radio"; ui_text = "\n"; noedit = true; >;
uniform bool useCustomSpec < ui_label = "Custom Ped Specular"; ui_category = "PLAYER PED REFLECTION"; ui_category_toggle = true; ui_tooltip = "Toggle on/off will apply on next game restart\n\nDisabled by default to avoid issues with some custom clothes like EUP"; > = false;
uniform float SSS_Reflection < ui_label = "SSS Reflection"; ui_type = "drag"; ui_min = 0.001; ui_max = 20.0; ui_category = "PLAYER PED REFLECTION"; ui_tooltip = "SubSurface Scattering Reflection\n\nRequires 'Custom Ped Specular' enabled"; > = 1.0;
uniform float skinSpecIntensity < ui_label = "Ped Skin Specular Intensity"; ui_type = "drag"; ui_min = 0.001; ui_max = 1000.0; ui_category = "PLAYER PED REFLECTION"; ui_tooltip = "Skin Specular Intensity\n\nRequires 'Custom Ped Specular' enabled"; > = 1.0;
uniform float skinSpecFresnel < ui_label = "Ped Skin Specular Fresnel"; ui_type = "drag"; ui_min = 0.001; ui_max = 10.0; ui_category = "PLAYER PED REFLECTION"; ui_tooltip = "Skin Specular Fresnel\n\nRequires 'Custom Ped Specular' enabled"; > = 1.0;
uniform float pedBumpiness < ui_label = "Ped Skin Bumpiness"; ui_type = "drag"; ui_min = 0.001; ui_max = 3.0; ui_category = "PLAYER PED REFLECTION"; ui_tooltip = "Skin Bumpiness\n\nRequires 'Custom Ped Specular' enabled"; > = 1.0;
uniform float skinDetail < ui_label = "Ped Skin Detail"; ui_type = "drag"; ui_min = 0.001; ui_max = 4.0; ui_category = "PLAYER PED REFLECTION"; ui_tooltip = "Skin / Facial Detail\n\nRequires 'Custom Ped Specular' enabled"; > = 1.0;
#endif
uniform float pedLighting < ui_label = "Ped Lighting"; ui_type = "drag"; ui_min = 0.0; ui_max = 10.0; ui_category = "PLAYER PED LIGHTING"; ui_tooltip = "Ped Additive Lighting Intensity"; > = 0.9;
uniform float3 pedLightingColor < ui_label = "Ped Lighting Color"; ui_type = "color"; ui_category = "PLAYER PED LIGHTING"; ui_tooltip = "Ped Additive Lighting Color Tint"; > = float3(1.0, 1.0, 1.0);
#if E
uniform int sep17 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;
uniform int dof < ui_label = "DoF"; ui_category = "DEPTH OF FIELD"; ui_type = "combo"; ui_items = "Disabled\0Default\0Static\0Dynamic\0"; ui_tooltip = "Default: Vanilla Focus Behavior\nStatic: Custom Static Focus\nDynamic: Custom Dynamic Focus"; > = 1;
uniform float2 dofIntensity < ui_label = "Intensity"; ui_text = "Intensity:  Near    |    Far"; ui_category = "DEPTH OF FIELD"; ui_type = "drag"; ui_tooltip = "Blurring Intensity\n\nNear Plane   |   Far Plane"; ui_min = 0.001; ui_max = 3.0; ui_spacing = 1; > = float2(1.5, 2.0);
uniform float2 Focus < ui_label = "Focus"; ui_text = "Distance Shift    |    Tilt Shift"; ui_category = "DEPTH OF FIELD"; ui_type = "drag"; ui_tooltip = "Focus Distance   |   Focus Tilt"; ui_min = -1.0; ui_max = 1.0; ui_spacing = 1; > = float2(0.0, -0.175);
uniform int bokehShape < ui_label = "Bokeh Shape"; ui_category = "DEPTH OF FIELD"; ui_type = "combo"; ui_items = "Circular\0Polygonal\0Classic Bokeh\0"; ui_spacing = 1; > = 0;
uniform float4 bokeh < ui_label = "Bokeh"; ui_text = "Bright   |   Size   |  Anamorphic  |  Vignette"; ui_category = "DEPTH OF FIELD"; ui_type = "drag"; ui_tooltip = "Bright  |  Size  |  Anamorphic  |  Vignette\n\nBright: Bokeh Brightness\nSize: Bokeh Size Scale\nAnamorphic: Anamorphic Lens Bokeh -sort of CinemaScope optics\nOptical Vignette / Cat's Eye -kind of effect"; ui_min = 0.001; ui_max = 3.0; ui_spacing = 1; > = float4(1.0, 0.9, 1.06, 1.4);
#endif
uniform int sep18 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;
uniform bool Disable_Chromatic_Aberration_n_Distortion < ui_label = "Disable ChromaticAberration & Distortion"; ui_category = "DISABLE FEATURES"; > = false;
#if E
uniform bool Disable_Volume_Lights < ui_label = "Disable Volume Lights"; ui_category = "DISABLE FEATURES"; ui_tooltip = "Disabling volume lights improves performance at night time"; > = false;
uniform bool Disable_Weapon_Reticle < ui_label = "Disable Weapon Reticle"; ui_category = "DISABLE FEATURES"; > = false;
#endif
uniform bool Blood_Remove < ui_label = "Disable Screen Blood effect"; ui_category = "DISABLE FEATURES"; > = false;
#if E
uniform int sep19 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;
uniform bool FPS_Limit < ui_label = "Use FPS Limit"; ui_tooltip = "Frame Rate Limiter"; ui_category = "MISC"; > = false;
uniform int FPSLimit < ui_label = "FPS Limit"; ui_min = 1; ui_max = 144; ui_category = "MISC"; > = 60;
uniform bool Skip_HUD < ui_label = "Skip HUD Elements"; ui_category = "MISC"; ui_spacing = 1; > = false;
#endif
#endif
uniform int sep20 < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;

uniform float timer < source = "timer"; >;
uniform bool overlay_open < source = "overlay_open"; >;
uniform float wasdf < hidden = true; >;
uniform int waasdf < hidden = true; > = __APPLICATION__;
static const float2 rcpFrame = float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);
uniform float4 QuantV_1 < hidden = true; >;
uniform float4 QuantV_2 < hidden = true; >;
uniform float4 QuantV_3 < hidden = true; >;

texture BackBufferTex : COLOR;
texture DepthBufferTex : DEPTH;
sampler BackBuffer
{
    Texture = BackBufferTex;
};
sampler DepthBuffer
{
    Texture = DepthBufferTex;
};
texture2D TextureOriginal
{
    Width = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = RGBA16F;
};
sampler2D sTextureOriginal
{
    Texture = TextureOriginal;
};
texture2D t8
{
    Width = BUFFER_WIDTH * 0.125;
    Height = BUFFER_HEIGHT * 0.125;
    Format = RGB10A2;
};
sampler2D st8
{
    Texture = t8;
};
texture shape < source = "QuantV/LensRain.png"; >
{
    Width = 1024;
    Height = 1024;
    Format = R8;
};
sampler2D shapes
{
    Texture = shape;
};
sampler BackBufferM
{
    Texture = BackBufferTex;
    AddressU = MIRROR;
    AddressV = MIRROR;
};
texture dd < source = "QuantV/18673.png"; >
{
    Width = 512;
    Height = 512;
};
sampler2D sdd
{
    Texture = dd;
    AddressU = WRAP;
    AddressV = WRAP;
};
#if (__RENDERER__ == 0xa000 || __RENDERER__ == 0xa100)
texture dx < source = "QuantV/dx.png"; >
{
    Width = 444;
    Height = 45;
};
sampler2D dxbuffer
{
    Texture = dx;
};
#endif

#define sngbool QuantV_1.x > 0.001 && !(ScreenRaindrops ^ 3)
#define t (timer > QuantV_1.y * 10000000.0 - 100.0) * saturate(tex2Dfetch(sTextureOriginal, 0).y) * (timer < QuantV_1.y * 10000000.0 + 100.0) * 23.9999
#define df(x,y) lerp(lerp(y, x, smoothstep(5.0, 5.5, t)), y, smoothstep(21.0, 21.5, t))
#define srs 0.05 * Screen_Raindrops_Scale
static const uint2 seed[72] = { uint2(150, 183), uint2(140, 181), uint2(148, 157), uint2(218, 190), uint2(184, 170), uint2(212, 205), uint2(137, 188), uint2(125, 138), uint2(129, 156), uint2(164, 198), uint2(127, 130), uint2(152, 175), uint2(163, 111), uint2(147, 191), uint2(117, 161), uint2(124, 197), uint2(220, 216), uint2(106, 193), uint2(176, 143), uint2(173, 171), uint2(209, 217), uint2(103, 204), uint2(141, 177), uint2(159, 122), uint2(201, 207), uint2(134, 200), uint2(211, 146), uint2(195, 105), uint2(196, 165), uint2(107, 100), uint2(178, 219), uint2(186, 135), uint2(101, 208), uint2(123, 144), uint2(162, 202), uint2(115, 214), uint2(149, 153), uint2(108, 155), uint2(168, 187), uint2(136, 133), uint2(145, 199), uint2(110, 126), uint2(121, 180), uint2(215, 119), uint2(172, 128), uint2(160, 112), uint2(203, 194), uint2(139, 192), uint2(113, 182), uint2(169, 174), uint2(154, 131), uint2(206, 104), uint2(142, 151), uint2(116, 132), uint2(166, 120), uint2(102, 109), uint2(179, 167), uint2(118, 158), uint2(210, 213), uint2(114, 185), uint2(241, 245), uint2(235, 231), uint2(221, 232), uint2(222, 238), uint2(239, 226), uint2(230, 234), uint2(240, 242), uint2(229, 237), uint2(223, 225), uint2(233, 243), uint2(228, 244), uint2(224, 236) };
void VS_Q_00(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD)
{
    texcoord.x = (id == 2) ? 2.0 : 0.0;
    texcoord.y = (id == 1) ? 2.0 : 0.0;
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
void VS_Q_01(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD, out float4 t1[3] : TEXCOORD1)
{
    texcoord.x = (id == 2) ? 2.0 : 0.0;
    texcoord.y = (id == 1) ? 2.0 : 0.0;
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    float sz = 4.0 * 4.0 * 1.0;
    texcoord *= 1.0 - saturate(tex2Dfetch(sTextureOriginal, int2(0, 1)).y);
    t1[0].xy = texcoord + float2(rcpFrame.x * sz, 0.0);
    t1[0].zw = texcoord + rcpFrame * sz * float2(0.5, 0.8660254);
    t1[1].xy = texcoord + rcpFrame * sz * float2(-0.5, 0.8660254);
    t1[1].zw = texcoord + float2(-rcpFrame.x * sz, 0.0);
    t1[2].xy = texcoord + rcpFrame * sz * float2(-0.5, -0.8660254);
    t1[2].zw = texcoord + rcpFrame * sz * float2(0.5, -0.8660254);
}
void VS_Q_02(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD, out float4 t1[3] : TEXCOORD1, out nointerpolation float2 t2 : TEXCOORD4, out nointerpolation float4 t3 : TEXCOORD5)
{
    texcoord.x = (id == 2) ? 2.0 : 0.0;
    texcoord.y = (id == 1) ? 2.0 : 0.0;
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    float sz = 4.0 * 4.0 * 3.0;
    t1[0].xy = texcoord + float2(rcpFrame.x * sz, 0.0);
    t1[0].zw = texcoord + rcpFrame * sz * float2(0.5, 0.8660254);
    t1[1].xy = texcoord + rcpFrame * sz * float2(-0.5, 0.8660254);
    t1[1].zw = texcoord + float2(-rcpFrame.x * sz, 0.0);
    t1[2].xy = texcoord + rcpFrame * sz * float2(-0.5, -0.8660254);
    t1[2].zw = texcoord + rcpFrame * sz * float2(0.5, -0.8660254);
    t2.xy = tex2Dfetch(sTextureOriginal, 0).yz;
    t3.x = df(3.98, 0.55);
    t3.y = (QuantV_1.w || Screen_Frozen_Snow_ForceEnable);
    t3.z = t3.y * Screen_Frozen_Snow * (1.0 - saturate(tex2Dfetch(sTextureOriginal, int2(0, 1)).y));
    t3.w = 0.0;
}
void func_qr(float2 texcoord, inout float4 t1[30], uint index)
{
    float2 sz[30];
    float2 fo[30];
    float ist = ((timer > QuantV_1.y * 10000000.0 - 100.0) && (timer < QuantV_1.y * 10000000.0 + 100.0)) * lerp(0.741, 1.049, saturate(tex2Dfetch(sTextureOriginal, 0).x));
    [unroll] for (int i = 0; i < 30; ++i)
    {
        float sd = QuantV_1.y * seed[i + index].x * 21.0;
        float2 noise = frac(sin(floor(sd) * 12.9898 + seed[i + index] * 78.233) * 43758.5453);
        float2 coord = noise - float2(0.499, 0.52);
        float vignt = length(coord);
        sz[i] = (1.0 + frac(sd) * 0.7 + 0.3) * 0.05;
        sz[i].x *= (rcpFrame.x / rcpFrame.y);
        sz[i] *= lerp(1.0, 0.4, noise.y) * (1.0 - saturate(tex2Dfetch(sTextureOriginal, int2(0, 1)).y));
        fo[i] = coord / vignt * lerp(0.225, float2(1.0, 0.5), vignt * 2.0);
        fo[i].x *= (rcpFrame.x / rcpFrame.y);
        float2 Vec; sincos(seed[i + index].x, Vec.y, Vec.x);
        t1[i].xy = mul(float2x2(Vec.x, -Vec.y, Vec.y, Vec.x), ((texcoord * ist + (1.0 - ist) * float2(0.5, 0.19)) - float2(0.499, 0.52) - fo[i]) / sz[i]);
        t1[i].zw = (float2(4.0 * frac(i * 0.25), floor(i * 0.25)) + t1[i].xy + 0.5) * 0.25;
    }
}
void VS_Q_0(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD, out float4 t1[30] : TEXCOORD1)
{
    texcoord.x = (id == 2) ? 2.0 : 0.0;
    texcoord.y = (id == 1) ? 2.0 : 0.0;
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    [unroll] for (int i = 0; i < 30; ++i) t1[i] = 0.0;
    if (sngbool) func_qr(texcoord, t1, 0);
}
void VS_Q_1(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD, out float4 t1[30] : TEXCOORD1)
{
    texcoord.x = (id == 2) ? 2.0 : 0.0;
    texcoord.y = (id == 1) ? 2.0 : 0.0;
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    [unroll] for (int i = 0; i < 30; ++i) t1[i] = 0.0;
    if (sngbool) func_qr(texcoord, t1, 30);
}
float4 PS_Q_00(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    float d = tex2Dlod(DepthBuffer, float4(texcoord, 0.0, 0.0)).x == 0.0;
    float4 r = tex2Dfetch(BackBuffer, int2(0, 1)).wywy;
    d += tex2Dlod(DepthBuffer, float4(texcoord, 0.0, 0.0), int2(1, 0)).x == 0.0;
    d += tex2Dlod(DepthBuffer, float4(texcoord, 0.0, 0.0), int2(0, 1)).x == 0.0;
    d += tex2Dlod(DepthBuffer, float4(texcoord, 0.0, 0.0), int2(-1, 0)).x == 0.0;
    d += tex2Dlod(DepthBuffer, float4(texcoord, 0.0, 0.0), int2(0, -1)).x == 0.0;
    r.w = r.w > 0.0001 && r.w < 0.9999;
    r.xyz = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0)).xyz;
    return lerp(float4(r.xyz, d * 0.2), float4(0, 1, 0, d * 0.2), r.w && position.x < 1 && position.y < 3);
}
float4 PS_Q_01(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    return float4(tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0)).xyz, 0.0);
}
float4 PS_Q_1(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    return tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0));
}
float4 PS_Q_2(float4 position : SV_Position, float2 texcoord : TEXCOORD, float4 t1[30] : TEXCOORD1) : SV_Target
{
    float4 res = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0));
    if (sngbool)
    {
        [unroll] for (int i = 0; i < 30; ++i)
        {
            float lens = tex2Dlod(shapes, float4(t1[i].zw, 0.0, 0.0)).x;
            bool2 k1 = (0.5 >= abs(t1[i].xy));
            res.w += lens * k1.x * k1.y * (1.0 - frac(QuantV_1.y * seed[i].x * 21.0)) * (1.0 - saturate(tex2Dfetch(sTextureOriginal, int2(0, 1)).z)) * lerp(lerp(1.0, 0.2, smoothstep(4.9, 5.4, t)), 1.0, smoothstep(20.5, 21.1, t));
        }
        res.w *= QuantV_1.x;
    }
    return res;
}
float4 PS_Q_3(float4 position : SV_Position, float2 texcoord : TEXCOORD, float4 t1[30] : TEXCOORD1) : SV_Target
{
    float4 res = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0));
    if (sngbool)
    {
        [unroll] for (int i = 0; i < 30; ++i)
        {
            float lens = tex2Dlod(shapes, float4(t1[i].zw, 0.0, 0.0)).x;
            bool2 k1 = (0.5 >= abs(t1[i].xy));
            res.w += lens * k1.x * k1.y * (1.0 - frac(QuantV_1.y * seed[i + 30].x * 21.0)) * (1.0 - saturate(tex2Dfetch(sTextureOriginal, int2(0, 1)).z)) * lerp(lerp(1.0, 0.2, smoothstep(4.9, 5.4, t)), 1.0, smoothstep(20.5, 21.1, t));
        }
        res.w *= QuantV_1.x;
    }
    return res;
}
float4 PS_Q_bb(float4 position : SV_Position, float2 texcoord : TEXCOORD, float4 t1[3] : TEXCOORD1) : SV_Target
{
    float3 res = tex2Dlod(BackBuffer, float4(texcoord, 0, 0)).xyz;
    if (sngbool)
    {
        float3 b = res;
        b += tex2Dlod(BackBuffer, float4(t1[0].xy, 0, 0)).xyz;
        b += tex2Dlod(BackBuffer, float4(t1[0].zw, 0, 0)).xyz;
        b += tex2Dlod(BackBuffer, float4(t1[1].xy, 0, 0)).xyz;
        b += tex2Dlod(BackBuffer, float4(t1[1].zw, 0, 0)).xyz;
        b += tex2Dlod(BackBuffer, float4(t1[2].xy, 0, 0)).xyz;
        b += tex2Dlod(BackBuffer, float4(t1[2].zw, 0, 0)).xyz;
        float3 fl = tex2Dlod(BackBufferM, float4(texcoord + (0.5 - texcoord) * 1.5, 0, 0)).xyz;
        fl = smoothstep(fl, 0.0, saturate(0.97 - dot(fl, float3(0.2126, 0.7152, 0.0722))));
        b += lerp(dot(fl, float3(0.2126, 0.7152, 0.0722)), fl, 1.5) * 2.5;
        res = b * (1.0 / 7.0);
    }
    return float4(res, 0.0);
}
float4 PS_Q_mx2(float4 position : SV_Position, float2 texcoord : TEXCOORD, float4 t1[3] : TEXCOORD1, nointerpolation float2 t2 : TEXCOORD4, nointerpolation float4 t3 : TEXCOORD5) : SV_Target
{
    float4 res = tex2Dlod(BackBuffer, float4(texcoord, 0, 0));
    res.w = ((position.x < 1) && (position.y < 1)) ? t2.y : t2.x;
    if ((position.x < 1) && (position.y < 3)) res.xyz = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0), int2(1, 0)).xyz;
    if (sngbool)
    {
        float3 b = tex2Dlod(st8, float4(texcoord, 0, 0)).xyz;
        b += tex2Dlod(st8, float4(t1[0].xy, 0, 0)).xyz;
        b += tex2Dlod(st8, float4(t1[0].zw, 0, 0)).xyz;
        b += tex2Dlod(st8, float4(t1[1].xy, 0, 0)).xyz;
        b += tex2Dlod(st8, float4(t1[1].zw, 0, 0)).xyz;
        b += tex2Dlod(st8, float4(t1[2].xy, 0, 0)).xyz;
        b += tex2Dlod(st8, float4(t1[2].zw, 0, 0)).xyz;
        b *= (1.0 / 7.0);
        b += tex2Dlod(st8, float4(texcoord + float2(-0.06, 0.35), 0, 0)).xyz * 0.3;
        b += tex2Dlod(st8, float4(texcoord + float2(0.01, -0.25), 0, 0)).xyz * 0.2;
        b = pow(saturate(b), 1.2);
        float dt = tex2Dlod(BackBuffer, float4(texcoord, 0, 0)).w;
        float rr = pow(tex2Dlod(BackBuffer, float4(texcoord, 0, 0), int2(1, 0)).w, 2.0);
        float uu = pow(tex2Dlod(BackBuffer, float4(texcoord, 0, 0), int2(0, 1)).w, 2.0);
        b = saturate(lerp(dot(b, float3(0.2126, 0.7152, 0.0722)), b, 2.4) * dt * 2.8);
        res.xyz = lerp(tex2Dlod(sTextureOriginal, float4(texcoord + saturate(dt - float2(rr, uu)) * 0.02, 0, 0)).xyz, 0.96, b);
    }
    if (Screen_Frozen_Snow)
    {
        float3 tex = tex2Dlod(sdd, float4(texcoord * 2.0 - timer * 0.0000002, 0, 0)).xyz;
        tex.xy -= 0.5;
        float vig = pow(abs(1.0 - smoothstep(0.975, 0.05, length(texcoord - 0.5) * t3.y)), 7.0) * t3.z;
        float noise = frac(sin(dot(texcoord, float2(12.9898, 78.233))) * 43758.5453);
        noise *= 0.025 * vig + tex.z * vig * t3.x;
        res.xyz = tex2Dlod(BackBufferM, float4(texcoord + tex.xy * vig, 0.0, 0.0)).xyz;
        res.xyz = lerp(res.xyz, 1.0, saturate(noise * 5.0));
    }
    return res;
}
#if (__RENDERER__ == 0xa000 || __RENDERER__ == 0xa100)
float4 PS_dx(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    float4 res = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0));
    float2 scale = float2(BUFFER_WIDTH, BUFFER_HEIGHT) * float2(1.0 / 444.0, 1.0 / 45.0);
    float2 uv = (texcoord - 0.5) + 0.5 / scale;
    float3 m = tex2Dlod(dxbuffer, float4(uv * scale, 0.0, 0.0)).xyz;
    if (timer < 20000.0) res.xyz = lerp(res.xyz, m, saturate(m * rcp(timer) * 33333.0));
    return res;
}
#endif

technique QuantV < ui_tooltip = "QuantV"; >
{
#if (__RENDERER__ == 0xa000 || __RENDERER__ == 0xa100)
    pass p0
    {
        VertexShader = VS_Q_00;
        PixelShader = PS_dx;
    }
#else
    pass p0
    {
        VertexShader = VS_Q_00;
        PixelShader = PS_Q_00;
        RenderTarget = TextureOriginal;
    }
    pass p1
    {
        VertexShader = VS_Q_00;
        PixelShader = PS_Q_01;
    }
    pass p2
    {
        VertexShader = VS_Q_0;
        PixelShader = PS_Q_2;
    }
    pass p3
    {
        VertexShader = VS_Q_1;
        PixelShader = PS_Q_3;
    }
    pass p4
    {
        VertexShader = VS_Q_01;
        PixelShader = PS_Q_bb;
        RenderTarget = t8;
    }
    pass p5
    {
        VertexShader = VS_Q_02;
        PixelShader = PS_Q_mx2;
    }
#endif
}