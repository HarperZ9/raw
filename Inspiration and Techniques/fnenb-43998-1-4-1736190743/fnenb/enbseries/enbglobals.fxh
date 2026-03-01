//----------------------------------------------------------------------------------------------//
//										GLOBAL OPTIONS FILE										//
//----------------------------------------------------------------------------------------------//
//								==================================								//
//								//     Silent Horizons ENB      //								//
//								//								//								//
//								//		by LonelyKitsuune		//								//
//								==================================								//
//----------------------------------------------------------------------------------------------//


//Simplified version of the most important settings for tweakers
// -> Everything that beginns with #define in this file is a global setting
// -> The values inside the brackets "[]" behind the main variable are the possible inputs


//For more experience users:
//If you want more control over the shaders go into the .fx files and
//enable LOCAL_OVERRIDE to tune each setting manually



//--------------------------------------------------------------------------------
//LENS OPTIONS
//--------------------------------------------------------------------------------
//Enables individual per droplet light dispersion
#define RAINFX_ENABLE_DISPERSION				0 //[0-1]


//Switches to a single layer frost texture with approximated refractions
#define FROSTFX_USE_REFRACTION_METHOD			1 //[0-1]


//Use only the brightest burst when more than one intersect
#define STARBURST_USE_MAX_COLOR					0 //[0-1]



//--------------------------------------------------------------------------------
//BLOOM OPTIONS
//--------------------------------------------------------------------------------
//Enable lens-dirt bloom reflections
#define ENABLE_DIRT_REFLECTION		0 //[0-1]


//Enables the option to mask out the bloom source
#define ENABLE_SOURCE_MASKING		0 //[0-1]


//Enables depth based bloom width/intensity manipulation
#define ENABLE_DEPTH_TESTING		0 //[0-1]


//Enables the option to limit bloom from skin (requires an active prepass!)
#define ENABLE_SKIN_ATTENUATION		1 //[0-1]



//--------------------------------------------------------------------------------
//POSTPASS OPTIONS
//--------------------------------------------------------------------------------
//Note: Disabling an AA method via the UI isn't the same as disabling it in here!
//      Doing it here safes you additional performance in most cases (especially
//      FXAA & SMAA), so make sure to choose your support preset accordingly!

//Makes different antialiasing methods available ingame
#define AA_SUPPORT_PRESET		4 //[0-5]-G
// 0 - No AA support
// 1 - Only SMAA
// 2 - Only FXAA
// 3 - SMAA and FXAA
// 4 - SMAA and CREAA
// 5 - SMAA, FXAA and CREAA


//Choose the SMAA quality level
#define SMAA_PRESET				2 //[0-3]
//0 = low,  1 = medium
//2 = high, 3 = ultra


//Choose the FXAA quality level
#define FXAA_PRESET				1 //[0-4]
// 0 = low,  1 = medium
// 2 = high, 3 = very high
// 4 = ultra


//Enables an optional blur pass (features full screen, radial and depth based controls)
#define ENABLE_BLUR_SUITE		0 //[0-1]



//--------------------------------------------------------------------------------
//DEPTH OF FIELD OPTIONS
//--------------------------------------------------------------------------------
//Enables partial occlusion of bokeh discs at screen corners
#define ENABLE_OPTICAL_VIGNETTE			1 //[0-1]


//Enables bokeh specific chromatic aberration
#define ENABLE_CHROMATIC_ABERRATION		1 //[0-1]


//Enables custom texture-based bokeh shapes
#define ENABLE_STYLIZED_BOKEHSHAPES		0 //[0-1]


//Enables uneven weighting of bokeh discs
#define ENABLE_SPHERICAL_ABERRATION		1 //[0-1]


//Enables bokeh shape diffraction
#define ENABLE_DIFFRACTION				1 //[0-1]


//Enables DoF limited graining
// 1 - Individual bokeh shape grain
// 2 - Full screen grain
#define ENABLE_GRAINING					2 //[0-2]


//Enables the option to visualize the focusing area
#define ENABLE_FOCUSING_TOOL			1 //[0-1]

