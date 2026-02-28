//----------------------------------------------------------------------------------------------//
//                                                                                              //
//               enbeffectprepass.fx - Screen-Space Subsurface Scattering                       //
//                    + GTAO / SSGI / Contact Shadows / Micro-Detail /                          //
//                        Clarity / Painterly Filter                                            //
//                   for Skyrim SE ENB (DirectX 11 Shader Model 5)                              //
//                                                                                              //
//         Original framework by LonelyKitsuune / T.Thanner - CC BY-NC-ND 4.0                  //
//                            Boris Vorontsov for ENBSeries                                     //
//                                                                                              //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//     Physically-motivated rendering pipeline by Zain Dana Harper - February 2026              //
//                                                                                              //
//  Architecture: 11 techniques, 17 passes                                                      //
//                                                                                              //
//    Tech 0 (two-pass → RenderTargetRGBA64):                                                   //
//        Pass 0: Clear render target                                                           //
//        Pass 1: Horizontal separable SSS diffusion                                            //
//                                                                                              //
//    Tech 1: Vertical separable SSS diffusion → TextureColor                                   //
//                                                                                              //
//    Tech 2: SSS final composite → TextureColor                                                //
//                                                                                              //
//    Tech 3 (two-pass → RenderTargetRGBA64):                                                   //
//        Pass 0: Clear render target                                                           //
//        Pass 1: GTAO (horizon-based ambient occlusion)                                        //
//                                                                                              //
//    Tech 4 (two-pass → RenderTargetRGBA64F):                                                  //
//        Pass 0: Clear render target                                                           //
//        Pass 1: SSGI (screen-space global illumination)                                       //
//                                                                                              //
//    Tech 5 (two-pass → RenderTargetR16F):                                                     //
//        Pass 0: Clear render target                                                           //
//        Pass 1: Contact shadows (ray-marched micro-shadows)                                   //
//                                                                                              //
//    Tech 6: Effects composite (apply AO + GI + CS + skin detail + clarity)                    //
//                                                                                              //
//    Tech 7: Painterly filter (anisotropic Kuwahara, final pass)                                //
//                                                                                              //
//    Tech 8: Effects composite (fog, atmospheric scattering, skin/clarity)                     //
//    Tech 9: Painterly filter (Kuwahara)                                                       //
//    Tech 10: Realism enhance (chromatic adaptation, micro-shadow detail)                      //
//        (Film halation, gate weave, letterbox deduplicated to CinematicFX)                    //
//                                                                                              //
//  SSS Diffusion Model: Christensen-Burley normalized diffusion profile                        //
//    - Runtime kernel evaluation (per-channel weights from user scatter distances)              //
//    - 13-tap separable convolution (importance-sampled positions)                              //
//    - Depth-scaled radius (world-space consistent scattering distance)                        //
//    - Normal-bilateral edge rejection (prevents cross-surface bleeding)                       //
//    - Shadow-bleed asymmetry (lit samples bleed into shadow, not reverse)                     //
//    - Temporal IGN jitter on kernel offsets (eliminates banding)                               //
//    - Surface-following via depth gradient (kernel bends with curvature)                      //
//                                                                                              //
//  GTAO: Ground Truth Ambient Occlusion (Jimenez 2016)                                        //
//    - Multi-slice horizon search with analytic visibility integral                            //
//    - Thickness heuristic to prevent over-occlusion from thin surfaces                       //
//    - Temporal rotation + spatial jitter for noise-free accumulation                          //
//    - Multi-bounce energy approximation (albedo-dependent AO response)                       //
//    - Bent normal output for future use                                                       //
//                                                                                              //
//  SSGI: Screen-Space Global Illumination                                                      //
//    - Single-bounce indirect illumination with form-factor evaluation                        //
//    - Golden-angle low-discrepancy sampling for optimal coverage                             //
//    - Depth rejection + normal weighting for geometric accuracy                              //
//    - Configurable color bleed saturation                                                     //
//                                                                                              //
//  Contact Shadows: Screen-Space Ray-Marched Micro-Shadows                                     //
//    - Ray-march along sun direction in screen space                                           //
//    - Thickness-windowed hit test (avoids false positives)                                    //
//    - Distance-dependent penumbra softness                                                    //
//    - N·L fade prevents tracing on back-facing surfaces                                      //
//                                                                                              //
//  Skin Micro-Detail: High-Pass Pore Injection                                                 //
//    - Gaussian high-pass extracts pore-scale detail lost to SSS                              //
//    - Skin-masked application with luminance-only option                                     //
//                                                                                              //
//  Clarity: Local Contrast Enhancement                                                         //
//    - Lightroom-style midtone local contrast boost                                            //
//    - Depth-aware sampling prevents cross-edge artifacts                                     //
//    - Optional skin preservation to maintain SSS softening                                   //
//                                                                                              //
//  Painterly: Anisotropic Kuwahara Filter (Kyprianidis et al., 2009)                            //
//    - Structure-tensor-guided elliptical sampling kernels                                      //
//    - Brush strokes follow local edge contours (hair, fabric, skin)                           //
//    - Per-sector mean/variance analysis for flat color regions                                //
//    - Polynomial spatial weighting for smooth sector transitions                              //
//    - Skin preservation to avoid overwriting SSS softening                                    //
//                                                                                              //
//  Final Composite (in SSS pass):                                                              //
//    - Fresnel-enhanced specular preservation                                                  //
//    - Skin softening (diffusion-based pore smoothing)                                         //
//    - Full color grading pipeline (exposure/contrast/sat/tint/levels/SDI)                     //
//    - Penner pre-integrated wrap lighting (curvature-adaptive, per-channel)                   //
//    - Backlit translucency with Burley transmission profile thickness                         //
//    - Subsurface color shift (hemoglobin absorption in shadow regions)                        //
//    - Smooth multi-factor skin mask (material + depth + normal variance)                      //
//                                                                                              //
//  References:                                                                                 //
//    [1] Jimenez et al., "Separable Subsurface Scattering", GPU Pro 360, 2015                  //
//    [2] Christensen & Burley, "Approximate Reflectance Profiles for                           //
//        Efficient Subsurface Scattering", SIGGRAPH 2015                                       //
//    [3] d'Eon & Luebke, "Advanced Techniques for Realistic Real-Time Skin                     //
//        Rendering", GPU Gems 3 Ch.14, 2007                                                    //
//    [4] Penner, "Pre-Integrated Skin Shading", SIGGRAPH 2011                                  //
//    [5] Jimenez et al., "Real-Time Realistic Skin Translucency", IEEE CG&A, 2010             //
//    [6] Golubev, "Efficient Screen-Space SSS", Advances in RTR, SIGGRAPH 2018                //
//    [7] Jimenez, Sainz, Mara, "Practical Real-time Strategies for Accurate                    //
//        Indirect Occlusion", SIGGRAPH 2016                                                    //
//    [8] Kyprianidis, Kang, Döllner, "Image and Video Abstraction by                           //
//        Anisotropic Kuwahara Filtering", Pacific Graphics / CGF, 2009                         //
//    [9] Ritschel et al., "Approximating Dynamic GI in Image Space",                           //
//        I3D 2009                                                                              //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//=============================================================================//
//                                OPTIONS                                      //
//=============================================================================//

#define ENABLE_TOOLS 0   // [0-1] Debug visualizations


//=============================================================================//
//                           UI PARAMETER INCLUDES                             //
//=============================================================================//

#include "UI/enbUI_Primer.fxh"

// Group 0: Debug tools
#define SHADERGROUP 0
#include "UI/enbUI_PrePass.fxh"

// Group 1: SSS Detection & Diffusion Profile (global)
#define SHADERGROUP 1
#include "UI/enbUI_PrePass.fxh"

// Group 2: Skin Color Grading (DNI)
#define SHADERGROUP 2
#define TODIE Day
#include "UI/enbUI_PrePass.fxh"
UI_SPECIAL_WHITESPACE(3)

#define TODIE Night
#include "UI/enbUI_PrePass.fxh"
UI_SPECIAL_WHITESPACE(4)

#define TODIE Interior
#include "UI/enbUI_PrePass.fxh"

#undef SHADERGROUP
#undef NOTFIRSTTIME

// Group 3: Translucency & Advanced (DNI)
#define SHADERGROUP 3
#define TODIE Day
#include "UI/enbUI_PrePass.fxh"
UI_SPECIAL_WHITESPACE(5)

#define TODIE Night
#include "UI/enbUI_PrePass.fxh"
UI_SPECIAL_WHITESPACE(6)

#define TODIE Interior
#include "UI/enbUI_PrePass.fxh"

#undef SHADERGROUP
#undef NOTFIRSTTIME


//=============================================================================//
//                                                                             //
//  INLINE UI PARAMETERS — GTAO / SSGI / CONTACT SHADOWS /                    //
//  SKIN MICRO-DETAIL / CLARITY / PAINTERLY                                    //
//                                                                             //
//  These are defined inline rather than in enbUI_PrePass.fxh because they     //
//  were added after the original UI structure. Migrate to the UI include      //
//  system when convenient.                                                    //
//                                                                             //
//=============================================================================//

// ─────────────────── GTAO (Ground Truth Ambient Occlusion) ──────────────────
int _spc36 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrGTAO < string UIName = "========= AMBIENT OCCLUSION (GTAO) ========="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UIGTAO_Enable       < string UIName = "AO | Enable"; > = true;
float UIGTAO_Intensity    < string UIName = "AO | Intensity";        string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 3.0;   float UIStep = 0.01; > = 1.5;
float UIGTAO_Radius       < string UIName = "AO | World Radius";     string UIWidget = "spinner"; float UIMin = 0.1;  float UIMax = 8.0;   float UIStep = 0.1;  > = 2.0;
int   UIGTAO_Slices       < string UIName = "AO | Direction Slices"; string UIWidget = "spinner"; int   UIMin = 2;    int   UIMax = 8;                          > = 6;
int   UIGTAO_Steps        < string UIName = "AO | Steps Per Slice";  string UIWidget = "spinner"; int   UIMin = 2;    int   UIMax = 12;                         > = 8;
float UIGTAO_Power        < string UIName = "AO | Power Curve";      string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 4.0;   float UIStep = 0.1;  > = 1.2;
float UIGTAO_DepthFade    < string UIName = "AO | Depth Fade Start"; string UIWidget = "spinner"; float UIMin = 10.0; float UIMax = 500.0; float UIStep = 5.0;  > = 200.0;
float UIGTAO_ThickBias    < string UIName = "AO | Thickness Bias";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;   float UIStep = 0.01; > = 0.15;
float UIGTAO_NormBias     < string UIName = "AO | Normal Bias";      string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 0.5;   float UIStep = 0.01; > = 0.05;
bool  UIGTAO_Temporal     < string UIName = "AO | Temporal Rotation"; > = true;
bool  UIGTAO_MultiBounce  < string UIName = "AO | Multi-Bounce Approx."; > = true;
float3 UIGTAO_Tint        < string UIName = "AO | Shadow Tint"; string UIWidget = "color"; > = {0.7, 0.75, 0.85};

// ─────────────────── SSGI (Screen-Space Global Illumination) ────────────────
int _spc37 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrSSGI < string UIName = "========= GLOBAL ILLUMINATION (SSGI) ========="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UISSGI_Enable       < string UIName = "GI | Enable"; > = true;
float UISSGI_Intensity    < string UIName = "GI | Intensity";        string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 5.0;   float UIStep = 0.01; > = 1.5;
float UISSGI_Radius       < string UIName = "GI | Sample Radius";    string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 12.0;  float UIStep = 0.1;  > = 4.0;
int   UISSGI_Samples      < string UIName = "GI | Sample Count";     string UIWidget = "spinner"; int   UIMin = 4;    int   UIMax = 24;                         > = 14;
float UISSGI_DepthReject  < string UIName = "GI | Depth Rejection";  string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 50.0;  float UIStep = 0.5;  > = 5.0;
float UISSGI_NormWeight   < string UIName = "GI | SSDO Balance";     string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 2.0;   float UIStep = 0.01; > = 1.0;
float UISSGI_ColorBleed   < string UIName = "GI | Color Bleed";      string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 2.0;   float UIStep = 0.01; > = 1.2;
float UISSGI_DepthFade    < string UIName = "GI | Depth Fade Start"; string UIWidget = "spinner"; float UIMin = 10.0; float UIMax = 500.0; float UIStep = 5.0;  > = 200.0;
bool  UISSGI_Temporal     < string UIName = "GI | Temporal Jitter"; > = true;
float UISSGI_Saturation   < string UIName = "GI | Bounce Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0;   float UIStep = 0.01; > = 1.3;

// ─────────────────── Contact Shadows ────────────────────────────────────────
int _spc38 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrCS < string UIName = "========= CONTACT SHADOWS ========="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UICS_Enable         < string UIName = "CS | Enable"; > = true;
float UICS_Intensity      < string UIName = "CS | Intensity";        string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 2.0;   float UIStep = 0.01; > = 1.0;
float UICS_MaxDist        < string UIName = "CS | Max Ray Distance"; string UIWidget = "spinner"; float UIMin = 0.5;   float UIMax = 20.0;  float UIStep = 0.5;  > = 5.0;
int   UICS_Steps          < string UIName = "CS | Ray Steps";        string UIWidget = "spinner"; int   UIMin = 8;     int   UIMax = 48;                         > = 24;
float UICS_Thickness      < string UIName = "CS | Occluder Thickness"; string UIWidget = "spinner"; float UIMin = 0.001; float UIMax = 0.5;  float UIStep = 0.005; > = 0.04;
float UICS_DepthFade      < string UIName = "CS | Depth Fade";       string UIWidget = "spinner"; float UIMin = 10.0;  float UIMax = 500.0; float UIStep = 5.0;  > = 200.0;
float UICS_NdLFade        < string UIName = "CS | NdL Fade Power";   string UIWidget = "spinner"; float UIMin = 0.5;   float UIMax = 4.0;   float UIStep = 0.1;  > = 1.5;
bool  UICS_Temporal       < string UIName = "CS | Temporal Dither"; > = true;
float UICS_Softness       < string UIName = "CS | Shadow Softness";  string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 1.0;   float UIStep = 0.01; > = 0.5;

// ─────────────────── Screen-Space Reflections ────────────────────────────────
int _spc50 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrSSR < string UIName = "========= SCREEN-SPACE REFLECTIONS ========="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UISSR_Enable        < string UIName = "SSR | Enable"; > = true;
float UISSR_Intensity     < string UIName = "SSR | Intensity";       string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 2.0;   float UIStep = 0.01; > = 0.7;
float UISSR_MaxDist       < string UIName = "SSR | Max Distance";    string UIWidget = "spinner"; float UIMin = 5.0;  float UIMax = 200.0; float UIStep = 1.0;  > = 60.0;
int   UISSR_Steps         < string UIName = "SSR | Ray Steps";       string UIWidget = "spinner"; int   UIMin = 16;   int   UIMax = 64;                         > = 32;
int   UISSR_RefineSteps   < string UIName = "SSR | Refine Steps";    string UIWidget = "spinner"; int   UIMin = 0;    int   UIMax = 8;                          > = 4;
float UISSR_Thickness     < string UIName = "SSR | Hit Thickness";   string UIWidget = "spinner"; float UIMin = 0.1;  float UIMax = 10.0;  float UIStep = 0.1;  > = 2.0;
float UISSR_DepthFade     < string UIName = "SSR | Depth Fade";      string UIWidget = "spinner"; float UIMin = 10.0; float UIMax = 500.0; float UIStep = 5.0;  > = 250.0;
float UISSR_EdgeFade      < string UIName = "SSR | Edge Fade";       string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 0.3;   float UIStep = 0.01; > = 0.08;
float UISSR_FresnelPow    < string UIName = "SSR | Fresnel Power";   string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 8.0;   float UIStep = 0.1;  > = 3.0;
bool  UISSR_Temporal      < string UIName = "SSR | Temporal Jitter"; > = true;

// ─────────────────── Volumetric God Rays ─────────────────────────────────────
int _spc51 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrGR < string UIName = "========= GOD RAYS ========="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UIGR_Enable         < string UIName = "Rays | Enable"; > = true;
float UIGR_Intensity      < string UIName = "Rays | Intensity";      string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 3.0;  float UIStep = 0.01; > = 0.50;
float UIGR_Density        < string UIName = "Rays | Density";        string UIWidget = "spinner"; float UIMin = 0.3;  float UIMax = 2.0;  float UIStep = 0.01; > = 0.80;
float UIGR_Decay          < string UIName = "Rays | Decay";          string UIWidget = "spinner"; float UIMin = 0.90; float UIMax = 1.0;   float UIStep = 0.001;> = 0.970;
float UIGR_Exposure       < string UIName = "Rays | Exposure";       string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 1.0;  float UIStep = 0.01; > = 0.20;
int   UIGR_Samples        < string UIName = "Rays | Samples";        string UIWidget = "spinner"; int   UIMin = 16;   int   UIMax = 128;                        > = 64;
float UIGR_Threshold      < string UIName = "Rays | Sky Threshold";  string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 1.0;  float UIStep = 0.01; > = 0.92;
float3 UIGR_Tint          < string UIName = "Rays | Sun Tint"; string UIWidget = "color"; > = {1.0, 0.95, 0.85};

// ─────────────────── Atmospheric Fog ─────────────────────────────────────────
int _spc52 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrFOG < string UIName = "========= ATMOSPHERIC FOG ========="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UIFOG_Enable        < string UIName = "Fog | Enable"; > = false;

int _spcFogDist < string UIName = "   --- Distance Fog ---"; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
float UIFOG_Density       < string UIName = "Fog | Density";         string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 0.05;  float UIStep = 0.0005;> = 0.005;
float UIFOG_MaxDist       < string UIName = "Fog | Max Distance";    string UIWidget = "spinner"; float UIMin = 50.0;  float UIMax = 3000.0;float UIStep = 10.0; > = 800.0;
float UIFOG_MaxOpacity    < string UIName = "Fog | Max Opacity";     string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 1.0;   float UIStep = 0.01; > = 0.85;
float UIFOG_SkyThreshold  < string UIName = "Fog | Sky Threshold";   string UIWidget = "spinner"; float UIMin = 0.90;  float UIMax = 1.0;   float UIStep = 0.001;> = 0.98;

int _spcFogHeight < string UIName = "   --- Height Fog ---"; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
float UIFOG_HeightFalloff < string UIName = "Fog | Height Falloff";  string UIWidget = "spinner"; float UIMin = 0.001; float UIMax = 0.1;   float UIStep = 0.001; > = 0.015;
float UIFOG_BaseHeight    < string UIName = "Fog | Base Height";     string UIWidget = "spinner"; float UIMin = -500.0;float UIMax = 500.0; float UIStep = 5.0;  > = 0.0;

int _spcFogColor < string UIName = "   --- Fog Color ---"; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
bool  UIFOG_SkyColorEnable< string UIName = "Fog | Sample Sky Color"; > = true;
float UIFOG_SkySampleY    < string UIName = "Fog | Sky Sample Height";string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 0.5;   float UIStep = 0.01; > = 0.25;
float UIFOG_SkySpread     < string UIName = "Fog | Sky Sample Spread";string UIWidget = "spinner"; float UIMin = 0.01;  float UIMax = 0.5;   float UIStep = 0.01; > = 0.15;
float UIFOG_SkyDesat      < string UIName = "Fog | Sky Desaturation"; string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 1.0;   float UIStep = 0.01; > = 0.30;
float3 UIFOG_Tint         < string UIName = "Fog | Tint Color"; string UIWidget = "color"; > = {1.0, 1.0, 1.0};
float UIFOG_TintWeight    < string UIName = "Fog | Tint Weight";     string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 1.0;   float UIStep = 0.01; > = 0.0;
float UIFOG_Brightness    < string UIName = "Fog | Brightness";      string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 3.0;   float UIStep = 0.01; > = 1.0;
float3 UIFOG_ColorFallback< string UIName = "Fog | Fallback Color"; string UIWidget = "color"; > = {0.65, 0.70, 0.78};

int _spcFogAtmo < string UIName = "   --- Atmosphere ---"; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
float UIFOG_Inscatter     < string UIName = "Fog | Inscatter";       string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 1.0;   float UIStep = 0.01; > = 0.15;
float3 UIFOG_InscatterTint< string UIName = "Fog | Inscatter Color"; string UIWidget = "color"; > = {1.0, 0.90, 0.70};
float UIFOG_AerialDesat   < string UIName = "Fog | Aerial Desat";    string UIWidget = "spinner"; float UIMin = 0.0;   float UIMax = 0.5;   float UIStep = 0.01; > = 0.15;

// ─────────────────── Skin Micro-Detail ──────────────────────────────────────
int _spc39 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrSMD < string UIName = "========= SKIN MICRO-DETAIL ========="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UISMD_Enable        < string UIName = "Detail | Enable"; > = true;
float UISMD_Intensity     < string UIName = "Detail | Pore Intensity"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01; > = 0.6;
float UISMD_BlurRadius    < string UIName = "Detail | Blur Radius";    string UIWidget = "spinner"; float UIMin = 1.0; float UIMax = 6.0; float UIStep = 0.1;  > = 2.5;
float UISMD_HighPassGain  < string UIName = "Detail | High-Pass Gain"; string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 5.0; float UIStep = 0.1;  > = 2.0;
float UISMD_SkinMaskPow   < string UIName = "Detail | Mask Tightness"; string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 4.0; float UIStep = 0.1;  > = 2.0;
bool  UISMD_LumaOnly      < string UIName = "Detail | Luminance Only"; > = true;

// ─────────────────── Clarity (Local Contrast Enhancement) ───────────────────
int _spc40 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrCLR < string UIName = "========= CLARITY / LOCAL CONTRAST ========="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UICLR_Enable        < string UIName = "Clarity | Enable"; > = true;
float UICLR_Amount        < string UIName = "Clarity | Amount";       string UIWidget = "spinner"; float UIMin = -1.0; float UIMax = 2.0;  float UIStep = 0.01; > = 0.50;
float UICLR_MidPoint      < string UIName = "Clarity | Mid-Point";    string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;  float UIStep = 0.01; > = 0.5;
float UICLR_Radius        < string UIName = "Clarity | Blur Radius";  string UIWidget = "spinner"; float UIMin = 2.0;  float UIMax = 16.0; float UIStep = 0.5;  > = 8.0;
float UICLR_DepthAware    < string UIName = "Clarity | Depth Aware";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;  float UIStep = 0.01; > = 0.7;
bool  UICLR_PreserveSkin  < string UIName = "Clarity | Preserve Skin"; > = true;

// ─────────────────── Watercolor Filter ───────────────────────────────────────
int _spc41 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrPAINT < string UIName = "========= WATERCOLOR FILTER ========="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UIPAINT_Enable       < string UIName = "WC | Enable"; > = true;
float UIPAINT_Intensity    < string UIName = "WC | Intensity";       string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;   float UIStep = 0.01; > = 0.90;
float UIPAINT_Radius       < string UIName = "WC | Wash Radius";     string UIWidget = "spinner"; float UIMin = 1.0;  float UIMax = 12.0;  float UIStep = 0.1;  > = 5.0;
int   UIPAINT_Sectors      < string UIName = "WC | Sectors";         string UIWidget = "spinner"; int   UIMin = 4;    int   UIMax = 8;                          > = 8;
float UIPAINT_Sharpness    < string UIName = "WC | Wash Flatness";   string UIWidget = "spinner"; float UIMin = 1.0;  float UIMax = 32.0;  float UIStep = 0.5;  > = 14.0;
float UIPAINT_Anisotropy   < string UIName = "WC | Flow Anisotropy"; string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;   float UIStep = 0.01; > = 0.8;
float UIPAINT_Hardness     < string UIName = "WC | Brush Softness";  string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 8.0;   float UIStep = 0.1;  > = 1.5;
float UIPAINT_EdgeInk      < string UIName = "WC | Edge Ink";        string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 2.0;   float UIStep = 0.01; > = 0.50;
float UIPAINT_EdgeWidth    < string UIName = "WC | Edge Width";      string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 4.0;   float UIStep = 0.1;  > = 1.5;
float UIPAINT_Posterize    < string UIName = "WC | Color Levels";    string UIWidget = "spinner"; float UIMin = 4.0;  float UIMax = 48.0;  float UIStep = 1.0;  > = 18.0;
float UIPAINT_PaperGrain   < string UIName = "WC | Paper Grain";     string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;   float UIStep = 0.01; > = 0.12;
float UIPAINT_WetEdge      < string UIName = "WC | Wet Edge";        string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 2.0;   float UIStep = 0.01; > = 0.35;
float UIPAINT_Bleed        < string UIName = "WC | Pigment Bleed";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;   float UIStep = 0.01; > = 0.25;
float UIPAINT_DepthFade    < string UIName = "WC | Depth Fade";      string UIWidget = "spinner"; float UIMin = 10.0; float UIMax = 500.0; float UIStep = 5.0;  > = 300.0;
bool  UIPAINT_PreserveSkin < string UIName = "WC | Preserve Skin"; > = true;
float UIPAINT_SkinReduce   < string UIName = "WC | Skin Reduction";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;   float UIStep = 0.01; > = 0.6;


//=============================================================================//
//  CINEMATIC FILM & REALISM EXTENSIONS                                        //
//=============================================================================//

// ─────────────────── NOTE: Deduplicated Effects ────────────────────────────
//  Film Halation, Gate Weave, and Cinematic Letterbox have been removed from
//  this shader to eliminate duplication.  Superior implementations exist in:
//    • Halation:   enblens.fx PS_Halation (16-tap Poisson, per-channel wavelength
//                  scatter, veiling glare) + Effect_CinematicFX.fxh PS_FilmHalation
//                  (near/wide field blend, soft-knee threshold, per-channel radii)
//    • Gate Weave: Effect_CinematicFX.fxh PS_GateWeave (4-sinusoid motion,
//                  velocity-based directional blur, breathing, exposure jitter)
//    • Letterbox:  Effect_CinematicFX.fxh PS_Letterbox (6 presets, bar color,
//                  projected-black grain, vertical bar support)
//  These effects are controlled from enblens.fx / CinematicFX UI panels.

// ─────────────────── Chromatic Adaptation ──────────────────────────────────
//  Automatic white balance that adapts to scene illuminant color.
//  Models the human visual system's von Kries chromatic adaptation —
//  the brain normalizes color perception toward "white" over time.
int _spc53 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrCA < string UIName = "======= CHROMATIC ADAPTATION (Realism) ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UICA_Enable         < string UIName = "CA | Enable"; > = false;
float UICA_Strength       < string UIName = "CA | Adaptation Strength"; string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;  float UIStep = 0.01; > = 0.3;
float3 UICA_TargetWhite   < string UIName = "CA | Target White Point"; string UIWidget = "color"; > = {1.0, 1.0, 1.0};

// ─────────────────── Micro-Shadow Enhancement ──────────────────────────────
//  Enhances fine shadow detail from ambient occlusion using a high-pass
//  filter on the AO buffer. Restores surface definition lost to low-res
//  AO without increasing overall shadow darkness.
int _spc54 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrMS < string UIName = "======= MICRO-SHADOW (Realism) ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UIMS_Enable         < string UIName = "MSh | Enable"; > = true;
float UIMS_Intensity      < string UIName = "MSh | Intensity";     string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 2.0;  float UIStep = 0.01; > = 0.4;
float UIMS_Radius         < string UIName = "MSh | Detail Radius"; string UIWidget = "spinner"; float UIMin = 1.0;  float UIMax = 6.0;  float UIStep = 0.5;  > = 2.0;
float UIMS_DepthFade      < string UIName = "MSh | Depth Fade";    string UIWidget = "spinner"; float UIMin = 10.0; float UIMax = 300.0; float UIStep = 5.0;  > = 100.0;

// ─────────────────── Atmospheric Scattering Enhancement ────────────────────
//  Adds Mie (directional) and Rayleigh (wavelength-dependent) separation
//  to the existing fog system for physically-based atmosphere.
int _spc55 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrATM < string UIName = "======= ATMOSPHERIC SCATTERING (Realism) ======="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UIATM_Enable        < string UIName = "Atm | Enhanced Scattering"; > = false;
float UIATM_RayleighStr   < string UIName = "Atm | Rayleigh Strength"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.5;
float UIATM_MieStr        < string UIName = "Atm | Mie Strength";      string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.3;
float UIATM_MieAnisotropy < string UIName = "Atm | Mie Anisotropy (g)"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 0.99; float UIStep = 0.01; > = 0.76;
float3 UIATM_RayleighTint < string UIName = "Atm | Rayleigh Color"; string UIWidget = "color"; > = {0.35, 0.55, 1.0};
float UIATM_SunInfluence  < string UIName = "Atm | Sun Direction Bias"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.5;


//=============================================================================//
//                       ENB EXTERNAL PARAMETERS                               //
//=============================================================================//

float4 Timer;
float4 ScreenSize;
float  AdaptiveQuality;
float4 TimeOfDay1;      // x=dawn, y=sunrise, z=day, w=sunset [0..1]
float4 TimeOfDay2;      // x=dusk, y=night [0..1]
float  ENightDayFactor;  // 0=night, 1=day
float  EInteriorFactor;  // 0=exterior, 1=interior
float  FieldOfView;
float4 Weather;
float4 SunDirection;     // xyz=screen-space direction, w=behind camera
float4 SunColor;         // xyz=color, w=visibility

float4 tempF1;
float4 tempF2;
float4 tempF3;
float4 tempInfo1;
float4 tempInfo2;


//=============================================================================//
//                    SkyrimBridge External Data Parameters                     //
//                                                                             //
//   69 float4 game-state parameters pushed per-frame by SkyrimBridge SKSE     //
//   plugin.  See SkyrimBridge.fxh for full documentation of each parameter.   //
//=============================================================================//
#include "Helper/SkyrimBridge.fxh"


//=============================================================================//
//                           GAME TEXTURES                                     //
//=============================================================================//

Texture2D TextureOriginal;      // R16G16B16A16 HDR scene
Texture2D TextureColor;         // Previous technique output
Texture2D TextureDepth;         // R32F depth
Texture2D TextureJitter;        // Blue noise
Texture2D TextureMask;          // rgb=skin albedo, a=SSS material flag
Texture2D TextureNormal;        // xyz=screen-space normals [0,1]
Texture2D TextureSunMask;       // Cloud occlusion

Texture2D RenderTargetRGBA32;
Texture2D RenderTargetRGBA64;
Texture2D RenderTargetRGBA64F;
Texture2D RenderTargetR16F;
Texture2D RenderTargetR32F;
Texture2D RenderTargetRGB32F;

SamplerState Point_Sampler
{
    Filter   = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};

SamplerState Linear_Sampler
{
    Filter   = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};

#include "Helper/enbHelper_Common.fxh"
static const float2 PixelSize = _HLP_PixelSize;
static const float2 ScreenRes = _HLP_ScreenRes;


//=============================================================================//
//                              STRUCTS                                        //
//=============================================================================//

struct VertexShaderInput
{
    float3 pos     : POSITION;
    float2 txcoord : TEXCOORD0;
};


//=============================================================================//
//                               ADDONS                                        //
//=============================================================================//

#define STYLE_WS1 16
#define STYLE_WS2 17
#define STYLE_WS3 18
#include "Addons/PrePass_StylizationSuite.fxh"

#define PARTICLE_WS1  19
#define PARTICLE_WS2  20
#define PARTICLE_WS3  21
#define PARTICLE_SWS1 22
#define PARTICLE_SWS2 23
#define PARTICLE_SWS3 24
#define PARTICLE_SWS4 25
#include "Addons/PrePass_ParticleField.fxh"

#define PHOTO_WS1  26
#define PHOTO_WS2  27
#define PHOTO_WS3  28
#define PHOTO_SWS1 29
#define PHOTO_SWS2 30
#include "Addons/PrePass_PhotoStudio.fxh"

#define SNOW_WS1  31
#define SNOW_WS2  32
#define SNOW_WS3  33
#define SNOW_SWS1 34
#define SNOW_SWS2 35
#include "Addons/PrePass_SnowCover.fxh"

#ifdef _STYLIZATION_SUITE_
    #define STYLE_LOADED 1
#else
    #define STYLE_LOADED 0
#endif
#ifdef _PARTICLE_FIELD_
    #define PARTICLE_LOADED 1
#else
    #define PARTICLE_LOADED 0
#endif
#ifdef _PHOTO_STUDIO_
    #define PHOTO_LOADED 1
#else
    #define PHOTO_LOADED 0
#endif
#ifdef _SNOW_COVER_
    #define SNOW_LOADED 1
#else
    #define SNOW_LOADED 0
#endif


//=============================================================================//
//                                                                             //
//  TECH 0+1: SEPARABLE SUBSURFACE SCATTERING DIFFUSION                       //
//                                                                             //
//  13-tap separable convolution with per-channel weights evaluated at         //
//  runtime from the Christensen-Burley diffusion profile.                     //
//                                                                             //
//  Improvements over v1:                                                      //
//                                                                             //
//    Runtime kernel: weights are computed from BurleyDiffusion3() using the   //
//    user's scatter distances, not pre-baked. Changing scatter distances       //
//    changes the profile SHAPE — wider red falloff, tighter blue peak —       //
//    not just a scale on static weights.                                      //
//                                                                             //
//    Depth-scaled radius: the pixel-space kernel radius is divided by         //
//    linearized depth so that the screen-space blur matches a fixed           //
//    world-space scattering distance at every camera distance. A face at      //
//    2m scatters across more pixels than one at 20m.                          //
//                                                                             //
//    Normal-bilateral rejection: in addition to depth rejection, we check     //
//    normal similarity between center and sample. This prevents bleeding      //
//    between skin surfaces that overlap in depth but face different           //
//    directions (crossed fingers, ear against cheek).                         //
//                                                                             //
//    Shadow-bleed asymmetry: real SSS causes light to bleed FROM lit          //
//    regions INTO shadow — not symmetrically. We weight samples by their      //
//    relative luminance so bright neighbors contribute more to dark center    //
//    pixels, producing the characteristic warm shadow-edge glow.              //
//                                                                             //
//    Temporal IGN jitter: sub-pixel offset on kernel positions per frame      //
//    converts any remaining banding into imperceptible temporal noise.         //
//                                                                             //
//    Surface following: kernel positions are adjusted along the depth         //
//    gradient so the 1D kernel bends with the surface curvature instead       //
//    of sampling along a flat screen-space line.                              //
//                                                                             //
//=============================================================================//

struct BlurVSOutput
{
    float4 pos       : SV_POSITION;
    float2 texcoord  : TEXCOORD0;
NI float2 BlurDir    : BLUR0;
NI float  DepthBias  : BLUR1;
NI float  NormBilat  : BLUR2;
NI float  FollowSfc  : BLUR3;
NI float  Active     : BLUR4;
NI float3 ScatterDist: BLUR5;
NI float  ShadowBleed: BLUR6;
NI float  UseJitter  : BLUR7;
NI float  DepthScaleRef : BLUR8;
NI float  UseDepthScale : BLUR9;
};


//--------- Core separable SSS blur ----------//

float4 SSSBlur(
    Texture2D    SrcTex,
    float2       UV,
    float2       BaseBlurDir,
    float        DepthBias,
    float        NormBilatPow,
    float        FollowSfc,
    float3       ScatterDist,
    float        ShadowBleed,
    float        UseJitter,
    float        DepthScaleRef,
    float        UseDepthScale)
{
    float3 CenterColor = SrcTex.SampleLevel(Linear_Sampler, UV, 0).rgb;
    float  CenterDepth = TextureDepth.SampleLevel(Point_Sampler, UV, 0).x;

    // Skin mask (quick test to skip non-skin pixels)
    float  SkinRaw   = pow(saturate(1.0 - TextureMask.SampleLevel(Point_Sampler, UV, 0).w), 4);
    float  LinDepth  = FastLinDepth(CenterDepth, 3000.0);
    float  DepthFade = smoothstep(UISSS_DepthFadeFar, UISSS_DepthFadeNear, LinDepth);
    float  SkinTest  = SkinRaw * DepthFade;
    float  IsSkin    = smoothstep(UISSS_MaskThreshold - UISSS_MaskSmooth,
                                  UISSS_MaskThreshold + UISSS_MaskSmooth, SkinTest);

    if(IsSkin < 0.001)
        return float4(CenterColor, IsSkin);

    // ---- Depth-scaled radius ----
    // Scale kernel so world-space scatter distance is constant regardless of
    // camera distance. Reference depth = the depth at which radius is 1:1 pixels.
    // Clamped to prevent blur explosion when camera is very close to skin.
    float RadiusScale = 1.0;
    if(UseDepthScale > 0.5)
    {
        RadiusScale = DepthScaleRef / max(LinDepth, 0.05);
        RadiusScale = min(RadiusScale, 8.0); //Hard cap: max 8× base radius
    }

    float2 BlurDir = BaseBlurDir * saturate(RadiusScale);

    // ---- Temporal jitter ----
    // Sub-pixel offset breaks banding into imperceptible per-frame noise.
    // Scale of 0.25 provides visible dither improvement without smearing.
    float Jitter = 0.0;
    if(UseJitter > 0.5)
        Jitter = (TemporalIGN(UV * ScreenRes, Timer.z) - 0.5) * 0.5;

    // ---- Center normal for bilateral rejection ----
    float3 CenterNormal = TextureNormal.SampleLevel(Point_Sampler, UV, 0).xyz * 2.0 - 1.0;
    float  CenterLuma   = dot(CenterColor, K_LUM);

    // ---- Surface-following depth gradient ----
    float DepthL = TextureDepth.SampleLevel(Point_Sampler, UV - BlurDir, 0).x;
    float DepthR = TextureDepth.SampleLevel(Point_Sampler, UV + BlurDir, 0).x;
    float DepthGradient = (DepthR - DepthL) * FollowSfc;

    // ---- Runtime kernel evaluation + accumulation ----
    // Evaluate Burley profile at center (r=0) for per-channel center weight
    float3 CenterWeight = BurleyDiffusion3(0.001, ScatterDist);
    float3 ColorSum     = CenterColor * CenterWeight;
    float3 WeightSum    = CenterWeight;

    [unroll] for(int i = 1; i < SSS_KERNEL_SIZE; i++)
    {
        float Offset = SSS_KernelPositions[i] + Jitter;

        // Surface-following adjustment
        float2 SampleUV = UV + BlurDir * Offset * (1.0 + Offset * DepthGradient);

        float3 SampleColor  = SrcTex.SampleLevel(Linear_Sampler, SampleUV, 0).rgb;
        float  SampleDepth  = TextureDepth.SampleLevel(Point_Sampler, SampleUV, 0).x;
        float3 SampleNormal = TextureNormal.SampleLevel(Point_Sampler, SampleUV, 0).xyz * 2.0 - 1.0;
        float  SampleSkinW  = pow(saturate(1.0 - TextureMask.SampleLevel(Point_Sampler, SampleUV, 0).w), 4);

        // --- Runtime Burley weight ---
        // Evaluate the actual Christensen-Burley profile at this tap distance.
        // This is the key improvement: changing scatter distances changes the
        // profile shape, not just amplitude. Blue channel drops off faster,
        // red extends further — physically correct per-channel behavior.
        float r = abs(Offset);
        float3 KernelW = BurleyDiffusion3(r, ScatterDist);

        // --- Depth bilateral ---
        float DepthDiff = abs(SampleDepth - CenterDepth) * DepthBias;
        float DepthW = exp(-DepthDiff * DepthDiff);

        // --- Normal bilateral ---
        // Prevents bleeding between surfaces at similar depth but different
        // orientation (crossed fingers, ear overlapping cheek).
        float NormalW = 1.0;
        if(NormBilatPow > 0.01)
            NormalW = NormalBilateralWeight(CenterNormal, SampleNormal, NormBilatPow);

        // --- Skin boundary rejection ---
        // Prevent non-skin pixels from bleeding into skin diffusion.
        float SkinW = saturate(SampleSkinW * 4.0);

        // --- Shadow-bleed asymmetry ---
        // Real SSS bleeds light FROM lit regions INTO shadow — not equally.
        // Bright samples contribute more when the center pixel is darker.
        // This produces the characteristic warm red glow at shadow edges.
        float SampleLuma = dot(SampleColor, K_LUM);
        float LumaDiff   = SampleLuma - CenterLuma;
        // Positive LumaDiff = sample brighter than center → boost weight
        // Negative = sample darker → reduce weight (less bleed from shadow into lit)
        float BleedW = lerp(1.0, saturate(1.0 + LumaDiff * 3.0), ShadowBleed);

        // --- Combined weight ---
        float3 W = KernelW * DepthW * NormalW * SkinW * BleedW;

        ColorSum  += SampleColor * W;
        WeightSum += W;
    }

    // Per-channel normalization (each channel has different total weight
    // because each has different scatter distance → different profile shape)
    float3 Diffused = ColorSum / max(WeightSum, DELTA);

    return float4(Diffused, IsSkin);
}


//--------- Horizontal Pass ----------//

BlurVSOutput VS_SSSBlurH(VertexShaderInput IN)
{
    BlurVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    OUT.BlurDir       = float2(PixelSize.x * UISSS_DiffRadius, 0.0);
    OUT.DepthBias     = UISSS_DepthBias;
    OUT.NormBilat     = UISSS_NormalBilateral;
    OUT.FollowSfc     = UISSS_DiffFollowSfc;
    OUT.Active        = UISSS_Enable;
    OUT.ScatterDist   = float3(UISSS_ScatterR, UISSS_ScatterG, UISSS_ScatterB);
    OUT.ShadowBleed   = UISSS_ShadowBleed;
    OUT.UseJitter     = UISSS_TemporalJitter;
    OUT.DepthScaleRef = UISSS_DepthScaleRef;
    OUT.UseDepthScale = UISSS_DepthScale;

    return OUT;
}

float4 PS_SSSBlurH(BlurVSOutput IN) : SV_Target
{
    if(IN.Active < 0.5)
        return float4(TextureOriginal.Sample(Point_Sampler, IN.texcoord).rgb, 0.0);

    return SSSBlur(TextureOriginal, IN.texcoord, IN.BlurDir,
                   IN.DepthBias, IN.NormBilat, IN.FollowSfc, IN.ScatterDist,
                   IN.ShadowBleed, IN.UseJitter, IN.DepthScaleRef, IN.UseDepthScale);
}


//--------- Vertical Pass ----------//

BlurVSOutput VS_SSSBlurV(VertexShaderInput IN)
{
    BlurVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    OUT.BlurDir       = float2(0.0, PixelSize.y * UISSS_DiffRadius);
    OUT.DepthBias     = UISSS_DepthBias;
    OUT.NormBilat     = UISSS_NormalBilateral;
    OUT.FollowSfc     = UISSS_DiffFollowSfc;
    OUT.Active        = UISSS_Enable;
    OUT.ScatterDist   = float3(UISSS_ScatterR, UISSS_ScatterG, UISSS_ScatterB);
    OUT.ShadowBleed   = UISSS_ShadowBleed;
    OUT.UseJitter     = UISSS_TemporalJitter;
    OUT.DepthScaleRef = UISSS_DepthScaleRef;
    OUT.UseDepthScale = UISSS_DepthScale;

    return OUT;
}

float4 PS_SSSBlurV(BlurVSOutput IN) : SV_Target
{
    if(IN.Active < 0.5)
        return float4(RenderTargetRGBA64.Sample(Point_Sampler, IN.texcoord).rgb, 0.0);

    return SSSBlur(RenderTargetRGBA64, IN.texcoord, IN.BlurDir,
                   IN.DepthBias, IN.NormBilat, IN.FollowSfc, IN.ScatterDist,
                   IN.ShadowBleed, IN.UseJitter, IN.DepthScaleRef, IN.UseDepthScale);
}


//=============================================================================//
//                                                                             //
//  TECH 2: FINAL SSS COMPOSITE                                               //
//                                                                             //
//  Applies in order:                                                          //
//    1. Multi-factor skin mask (material + depth + normal variance)            //
//    2. Fresnel-enhanced specular preservation                                //
//    3. Skin softening (diffusion blend)                                      //
//    4. Color grading (exposure → contrast → tint → sat → levels → SDI)       //
//    5. Penner curvature-adaptive wrap lighting (per-channel)                 //
//    6. Backlit translucency with Burley transmission thickness               //
//    7. Subsurface color shift (hemoglobin absorption)                        //
//    8. Specular recomposition                                                //
//    9. Smooth mask blend                                                     //
//                                                                             //
//=============================================================================//

struct FinalVSOutput
{
    float4 pos              : SV_POSITION;
    float2 texcoord         : TEXCOORD0;

NI float  Grade_Exposure    : PS0;
NI float  Grade_Contrast    : PS1;
NI float  Grade_Saturation  : PS2;
NI float3 Grade_Tint        : PS3;
NI float  Grade_OutBlack    : PS4;
NI float  Grade_OutWhite    : PS5;
NI float  Grade_SDILum      : PS6;

NI float  Trans_Amount      : PS7;
NI float3 Trans_SubColor    : PS8;
NI float  Trans_SubShift    : PS9;
NI float  Trans_Softening   : PS10;
NI float  Trans_WrapAmount  : PS11;
NI float3 ScatterDist       : PS12;
};


FinalVSOutput VS_Final(VertexShaderInput IN)
{
    FinalVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    // DNI-separated color grading
    OUT.Grade_Exposure   = DNI_SEPARATION(UISSS_Exposure);
    OUT.Grade_Contrast   = DNI_SEPARATION(UISSS_Contrast);
    OUT.Grade_Saturation = DNI_SEPARATION(UISSS_Saturation);
    OUT.Grade_Tint       = ColorToChroma(DNI_SEPARATION(UISSS_Tint));
    OUT.Grade_OutBlack   = DNI_SEPARATION(UISSS_OutBlack);
    OUT.Grade_OutWhite   = DNI_SEPARATION(UISSS_OutWhite);
    OUT.Grade_SDILum     = DNI_SEPARATION(UISSS_SDILum);

    // DNI-separated translucency & advanced
    OUT.Trans_Amount     = DNI_SEPARATION(UISSS_TransAmount);
    OUT.Trans_SubColor   = DNI_SEPARATION(UISSS_SubColor);
    OUT.Trans_SubShift   = DNI_SEPARATION(UISSS_SubShift);
    OUT.Trans_Softening  = DNI_SEPARATION(UISSS_Softening);
    OUT.Trans_WrapAmount = DNI_SEPARATION(UISSS_WrapAmount);

    OUT.ScatterDist = float3(UISSS_ScatterR, UISSS_ScatterG, UISSS_ScatterB);

    return OUT;
}


float4 PS_Final(FinalVSOutput IN) : SV_Target
{
    float3 OrigColor     = TextureOriginal.Sample(Point_Sampler, IN.texcoord).rgb;
    float4 DiffusedData  = TextureColor.Sample(Linear_Sampler, IN.texcoord);
    float3 DiffusedColor = DiffusedData.rgb;
    float  BlurSkinMask  = DiffusedData.a;

    float4 Mask     = TextureMask.Sample(Point_Sampler, IN.texcoord);
    float  RawDepth = TextureDepth.Sample(Point_Sampler, IN.texcoord).x;
    float  LinDepth = FastLinDepth(RawDepth, 3000.0);

    float3 Color = OrigColor;
    float3 Normal = normalize(TextureNormal.Sample(Point_Sampler, IN.texcoord).xyz * 2.0 - 1.0);


    //=========================================================================//
    //  Step 1: Multi-Factor Skin Mask                                         //
    //=========================================================================//

    float SkinRaw   = pow(saturate(1.0 - Mask.w), 4);
    float DepthFade = smoothstep(UISSS_DepthFadeFar, UISSS_DepthFadeNear, LinDepth);

    // Normal variance refinement
    float NormalVariance = 0.0;
    [branch] if(UISSS_NormalWeight > 0.01)
    {
        float3 NC = TextureNormal.Sample(Point_Sampler, IN.texcoord).xyz;
        float3 NL = TextureNormal.Sample(Point_Sampler, IN.texcoord + float2(-PixelSize.x * 2, 0)).xyz;
        float3 NR = TextureNormal.Sample(Point_Sampler, IN.texcoord + float2( PixelSize.x * 2, 0)).xyz;
        float3 NU = TextureNormal.Sample(Point_Sampler, IN.texcoord + float2(0, -PixelSize.y * 2)).xyz;
        float3 ND = TextureNormal.Sample(Point_Sampler, IN.texcoord + float2(0,  PixelSize.y * 2)).xyz;

        float3 AvgN = 0.25 * (NL + NR + NU + ND);
        NormalVariance = saturate(length(NC - AvgN) * UISSS_NormalWeight * 20.0);
    }

    float SkinInput = SkinRaw * DepthFade;
    SkinInput = lerp(SkinInput, SkinInput * (0.5 + NormalVariance), UISSS_NormalWeight > 0.01);

    float SkinMask = smoothstep(UISSS_MaskThreshold - UISSS_MaskSmooth,
                                UISSS_MaskThreshold + UISSS_MaskSmooth, SkinInput);

    SkinMask = min(SkinMask, saturate(BlurSkinMask * 2.0 + 0.5));
    SkinMask *= UISSS_Enable;


    //=========================================================================//
    //  Debug visualizations (early out)                                        //
    //=========================================================================//

    #if ENABLE_TOOLS
    if(UI_Vis_SkinMask)     return float4(SkinMask.xxx, 1.0);
    if(UI_Vis_DiffuseOnly)  return float4(DiffusedColor, 1.0);
    if(UI_Vis_Normals)      return float4(Normal * 0.5 + 0.5, 1.0);
    if(UI_Vis_KernelColor)  return float4(abs(DiffusedColor - OrigColor) * 5.0, 1.0);
    if(UI_Vis_ShadowBleed)
    {
        float CL = dot(OrigColor, K_LUM);
        float DL = dot(DiffusedColor, K_LUM);
        float Bleed = saturate(DL - CL) * 3.0;
        return float4(Bleed, Bleed * 0.3, 0, 1.0);
    }
    #endif


    //=========================================================================//
    //  Steps 2-9: Skin Processing                                             //
    //=========================================================================//

    float3 SkinDirectIllu = 0.0;

    [branch] if(SkinMask > 0.001)
    {
        //--- Step 2: Fresnel-Enhanced Specular Preservation ---//
        float SpecAmount;
        if(UISSS_SpecFresnel)
            SpecAmount = EstimateSpecularFresnel(OrigColor, DiffusedColor, Normal) * UISSS_SpecPreserve;
        else
            SpecAmount = EstimateSpecular(OrigColor, DiffusedColor) * UISSS_SpecPreserve;

        // Store for later recomposition
        float3 SpecColor = OrigColor * SpecAmount;


        //--- Step 3: Skin Softening ---//
        float3 SkinColor = lerp(OrigColor, DiffusedColor, IN.Trans_Softening);


        //--- Step 4: Color Grading Pipeline ---//
        SkinColor *= IN.Grade_Exposure;
        SkinColor  = exp2(lerp(log2(0.18), log2(SkinColor + DELTA), IN.Grade_Contrast));
        SkinColor  = zerolim(SkinColor) * IN.Grade_Tint;

        float SkinLuma = dot(SkinColor, K_LUM);
        SkinColor = zerolim(lerp(SkinLuma, SkinColor, IN.Grade_Saturation));
        SkinColor = mad(IN.Grade_OutWhite - IN.Grade_OutBlack, SkinColor, IN.Grade_OutBlack);

        SkinDirectIllu = zerolim(dot(SkinColor, K_LUM) - dot(Mask.rgb, K_LUM)) * Mask.rgb;
        SkinColor      = zerolim(SkinColor - SkinDirectIllu + SkinDirectIllu * IN.Grade_SDILum);


        //--- Step 5: Penner Curvature-Adaptive Wrap Lighting ---//
        //
        // Curvature modulates how far light "wraps" around the shadow
        // terminator. High curvature (nose, chin) = wider wrap = softer,
        // warmer shadow edges. Per-channel wrap means red wraps furthest,
        // producing the characteristic reddened penumbra on skin.

        [branch] if(IN.Trans_WrapAmount > 0.001)
        {
            float Curvature = EstimateCurvature(TextureNormal, Linear_Sampler,
                                                IN.texcoord, UISSS_CurvatureRad);

            float NdL = dot(Normal, normalize(SunDirection.xyz));

            float3 WrappedNdL = PennerWrapLighting(NdL, Curvature, IN.ScatterDist,
                                                   UISSS_WrapMult);

            // Original (hard) N·L for comparison
            float HardNdL = saturate(NdL);

            // Wrap contribution: difference between wrapped and hard lighting
            // colored by subsurface absorption (red survives transmission)
            float3 WrapContrib = (WrappedNdL - HardNdL) * IN.Trans_SubColor;
            WrapContrib *= SunColor.xyz * saturate(SunColor.w) * ENightDayFactor;

            SkinColor += WrapContrib * IN.Trans_WrapAmount;

            #if ENABLE_TOOLS
            if(UI_Vis_WrapLight) return float4(zerolim(WrapContrib) * SkinMask * 3.0, 1.0);
            #endif
        }


        //--- Step 6: Backlit Translucency with Burley Transmission ---//
        //
        // Instead of an ad-hoc power curve, evaluate the Burley profile
        // at the curvature-estimated thickness distance. This gives
        // physically-correct exponential attenuation per wavelength —
        // red transmits through thicker skin, blue is absorbed rapidly.

        [branch] if(IN.Trans_Amount > 0.001)
        {
            float BacklitNdL = saturate(-dot(Normal, normalize(SunDirection.xyz)));

            float Curvature = EstimateCurvature(TextureNormal, Linear_Sampler,
                                                IN.texcoord, UISSS_CurvatureRad);
            float Thickness = saturate(Curvature * UISSS_CurvatureMult);

            // Evaluate Burley transmission at estimated thickness
            // thickness_mm is mapped from curvature [0,1] to a physical range
            // Ear: ~2mm, fingertip: ~4mm, cheek: ~10mm
            float ThicknessMM = lerp(8.0, 1.5, Thickness);
            float3 Transmission = BurleyTransmission(ThicknessMM, IN.ScatterDist * 4.0);

            float TransInt = pow(abs(BacklitNdL), UISSS_TransPower) * IN.Trans_Amount;
            TransInt *= saturate(SunColor.w) * ENightDayFactor;

            float3 TransColor = SkinColor * IN.Trans_SubColor * SunColor.xyz * Transmission;
            SkinColor += TransColor * TransInt;

            #if ENABLE_TOOLS
            if(UI_Vis_Translucency) return float4(TransColor * TransInt * SkinMask, 1.0);
            if(UI_Vis_Thickness)    return float4(Thickness.xxx, 1.0);
            if(UI_Vis_Curvature)    return float4(Curvature.xxx * 5.0, 1.0);
            #endif
        }


        //--- Step 7: Subsurface Color Shift ---//
        float SubStrength = saturate(1.0 - SkinLuma) * IN.Trans_SubShift;
        SkinColor = lerp(SkinColor, SkinColor * IN.Trans_SubColor, SubStrength);


        //--- Step 8: Specular Recomposition ---//
        SkinColor += SpecColor;


        //--- Step 9: Final Blend ---//
        Color = lerp(Color, SkinColor, SkinMask);
    }


    #if ENABLE_TOOLS
    if(UI_Vis_SpecMask)
    {
        float Spec = UISSS_SpecFresnel ?
            EstimateSpecularFresnel(OrigColor, DiffusedColor, Normal) :
            EstimateSpecular(OrigColor, DiffusedColor);
        return float4(Spec.xxx, 1.0);
    }
    #endif

    return float4(Color, 1.0 - SkinMask);
}


//=============================================================================//
//                                                                             //
//  SHARED HELPERS FOR SCREEN-SPACE EFFECTS                                    //
//                                                                             //
//=============================================================================//

static const float PREPASS_FARPLANE = 3000.0;
static const float PI_PP = 3.14159265359;

// Linearize raw depth to [0,1] range (0=near, 1=far).
// When SkyrimBridge is active, uses accurate far clip for correct linearization.
// Without SB, falls back to the 3000-unit estimate (adequate for most scenes,
// but contact shadows / GTAO radius scale may be slightly off at extreme distances).
float LinZ(float rawDepth)
{
#ifdef SKYRIMBRIDGE_FXH
    float farClip = SB_IsActive() ? SB_Camera_Info.z : PREPASS_FARPLANE;
#else
    float farClip = PREPASS_FARPLANE;
#endif
    return FastLinDepth(rawDepth, farClip);
}

// World-space depth from raw depth buffer
float WorldZ(float rawDepth)
{
#ifdef SKYRIMBRIDGE_FXH
    float farClip = SB_IsActive() ? SB_Camera_Info.z : PREPASS_FARPLANE;
#else
    float farClip = PREPASS_FARPLANE;
#endif
    return FastLinDepth(rawDepth, farClip) * farClip;
}

// Temporal rotation offset per-frame (angle in radians)
float TemporalAngleOffset(float2 uv)
{
    float noise = TemporalIGN(uv * ScreenRes, Timer.z);
    return noise * PI_PP;
}

// Multi-bounce AO approximation (Jimenez 2016 / GTR)
// Approximates energy-conserving multiple bounces from a single AO value
// so that dark AO regions aren't unrealistically black.
float3 MultiBounceAO(float ao, float3 albedo)
{
    float3 a = 2.0404 * albedo - 0.3324;
    float3 b = -4.7951 * albedo + 0.6417;
    float3 c = 2.7552 * albedo + 0.6903;
    return max(ao, ((ao * a + b) * ao + c) * ao);
}


// ─── View-Space Reconstruction Helpers (for SSR) ────────────────────────────

// Reconstruct view-space position from UV + raw depth.
// Convention: z+ is INTO screen, y+ is UP.
float3 ViewPosFromUV(float2 uv, float worldDepth)
{
    float aspect  = ScreenSize.x / ScreenSize.y;
    float tanHFov = tan(FieldOfView * 0.5);
    float2 ndc    = uv * 2.0 - 1.0;
    ndc.y         = -ndc.y; // Screen y-down → view y-up
    return float3(ndc.x * aspect * tanHFov, ndc.y * tanHFov, 1.0) * worldDepth;
}

// Project a view-space point back to screen UV.
float2 UVFromViewPos(float3 vp)
{
    float aspect  = ScreenSize.x / ScreenSize.y;
    float tanHFov = tan(FieldOfView * 0.5);
    float2 ndc    = vp.xy / (vp.z * float2(aspect * tanHFov, tanHFov));
    ndc.y         = -ndc.y;
    return ndc * 0.5 + 0.5;
}

// Reconstruct view-space normal from depth buffer partial derivatives.
// Uses smallest-derivative heuristic (Bavoil & Sainz) to handle edges.
// Convention: normal faces TOWARD camera (z-negative in our view-space).
float3 NormalFromDepth(float2 uv)
{
    float cZ = WorldZ(TextureDepth.SampleLevel(Point_Sampler, uv, 0).x);
    float rZ = WorldZ(TextureDepth.SampleLevel(Point_Sampler, uv + float2(PixelSize.x, 0), 0).x);
    float lZ = WorldZ(TextureDepth.SampleLevel(Point_Sampler, uv - float2(PixelSize.x, 0), 0).x);
    float dZ = WorldZ(TextureDepth.SampleLevel(Point_Sampler, uv + float2(0, PixelSize.y), 0).x);
    float uZ = WorldZ(TextureDepth.SampleLevel(Point_Sampler, uv - float2(0, PixelSize.y), 0).x);

    float3 cP = ViewPosFromUV(uv, cZ);

    // Pick the neighbor pair with the smaller derivative (avoids edge artifacts)
    float3 ddxR = ViewPosFromUV(uv + float2(PixelSize.x, 0), rZ) - cP;
    float3 ddxL = cP - ViewPosFromUV(uv - float2(PixelSize.x, 0), lZ);
    float3 ddx  = (abs(ddxR.z) < abs(ddxL.z)) ? ddxR : ddxL;

    float3 ddyD = ViewPosFromUV(uv + float2(0, PixelSize.y), dZ) - cP;
    float3 ddyU = cP - ViewPosFromUV(uv - float2(0, PixelSize.y), uZ);
    float3 ddy  = (abs(ddyD.z) < abs(ddyU.z)) ? ddyD : ddyU;

    // cross(ddy, ddx) gives normal facing toward camera for front-facing surfaces
    return normalize(cross(ddy, ddx));
}


// ─── Sky Color Estimation (from Effect_AtmosphericFog.fxh v3.1) ─────────────
//
// Samples actual sky pixels from the scene using a Poisson disc pattern,
// depth-validated so only real sky contributes (not distant geometry).
// Reinhard soft-clamp prevents sun blowout from contaminating the average.
// Falls back to a static color if no valid sky pixels are found
// (interiors, underground, fully enclosed areas).

static const float2 SkyPoissonDisc[8] = {
    float2(-0.613,  0.617), float2( 0.170, -0.040),
    float2(-0.299, -0.248), float2( 0.685,  0.422),
    float2(-0.799, -0.073), float2( 0.425, -0.639),
    float2( 0.036,  0.455), float2(-0.210,  0.720)
};

float3 EstimateSkyColor(float2 center, float spread, float desat, float skyThreshLin)
{
    float3 accum = 0.0;
    float  validCount = 0.0;

    [unroll] for(int i = 0; i < 8; i++)
    {
        float2 uv = saturate(center + SkyPoissonDisc[i] * spread);
        float  depth = LinZ(TextureDepth.SampleLevel(Point_Sampler, uv, 0).x);

        // Only accept actual sky pixels (depth beyond threshold)
        float isSky = step(skyThreshLin, depth);

        float3 s = TextureOriginal.SampleLevel(Linear_Sampler, uv, 0).rgb;

        // Reinhard soft clamp: prevents sun disc from blowing out the average
        s = s / (1.0 + s);

        accum      += s * isSky;
        validCount += isSky;
    }

    float3 skyColor;

    [branch] if(validCount > 0.5)
    {
        skyColor = accum / validCount;
    }
    else
    {
        // No sky pixels visible (interiors, dense forest, underground).
        // Use static fallback color.  The lens-stage bloom scatter addon
        // provides intelligent ambient estimation from TextureDownsampled
        // and RenderTarget128 — that handles the dynamic matching.
        return UIFOG_ColorFallback;
    }

    // Inverse Reinhard: restore HDR for correct scene blending
    skyColor = skyColor / max(1.0 - skyColor, DELTA);

    // Desaturate: atmospheric scattering preferentially scatters blue,
    // making distant fog color closer to neutral gray than the raw sky.
    float luma = dot(skyColor, K_LUM);
    return lerp(skyColor, luma, desat);
}


//=============================================================================//
//                                                                             //
//  TECH 3: HORIZON-BASED AMBIENT OCCLUSION (HBAO)                            //
//                                                                             //
//  Per Bavoil & Sainz (NVIDIA, 2008) — adapted for single-pass ENB.          //
//                                                                             //
//  For each pixel, march N direction slices across a half-circle (the other   //
//  half is handled by the ± march).  Along each direction, find the maximum   //
//  horizon elevation angle — the highest point at which geometry blocks the   //
//  sky dome.  The AO integral per direction is sin(horizon_angle), averaged   //
//  across all directions.                                                     //
//                                                                             //
//  This is inherently bounded [0,1] per direction, self-normalizing, and      //
//  requires no tuning constants (sigma, epsilon) unlike SAO.                  //
//                                                                             //
//  Algorithm per direction:                                                   //
//    1. March M steps outward in + and - UV direction                         //
//    2. At each step, tangent = (centerZ - sampleZ) / lateralWorldDist        //
//       Positive tangent = sample is closer to camera = above our horizon     //
//    3. Track the max tangent found (the horizon)                             //
//    4. Convert: AO = sin(atan(maxTangent))                                   //
//    5. Average sin-AO across all N directions                                //
//                                                                             //
//  Output: RenderTargetRGBA64                                                 //
//    .r  = AO factor [0,1] (1 = fully lit, 0 = fully occluded)               //
//    .g  = linearized depth (for bilateral blur in composite)                 //
//                                                                             //
//=============================================================================//

struct GTAOVSOutput
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
NI float  Intensity : G0;
NI float  Radius    : G1;
NI float  Power     : G2;
NI float  DepthFade : G3;
NI float  ThickBias : G4;
NI float  NormBias  : G5;
NI float  Active    : G6;
NI float  Temporal  : G7;
};

GTAOVSOutput VS_GTAO(VertexShaderInput IN)
{
    GTAOVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    OUT.Intensity = UIGTAO_Intensity;
    OUT.Radius    = UIGTAO_Radius;
    OUT.Power     = UIGTAO_Power;
    OUT.DepthFade = UIGTAO_DepthFade;
    OUT.ThickBias = UIGTAO_ThickBias;
    OUT.NormBias  = UIGTAO_NormBias;
    OUT.Active    = UIGTAO_Enable;
    OUT.Temporal  = UIGTAO_Temporal;

    return OUT;
}

float4 PS_GTAO(GTAOVSOutput IN) : SV_Target
{
    if(IN.Active < 0.5)
        return float4(1.0, 0.0, 0.5, 1.0);

    float rawDepth = TextureDepth.SampleLevel(Point_Sampler, IN.texcoord, 0).x;
    float centerZ  = WorldZ(rawDepth);
    float linZ     = LinZ(rawDepth);

    if(linZ > 0.95 || linZ < DELTA)
        return float4(1.0, linZ, 0.5, 1.0);

    float depthFade = 1.0 - smoothstep(IN.DepthFade * 0.5, IN.DepthFade, centerZ);
    if(depthFade < 0.001)
        return float4(1.0, linZ, 0.5, 1.0);

    // =========================================================================
    //  HBAO+ — Horizon-Based Ambient Occlusion (Bavoil & Sainz 2008)
    //         with normal-guided horizon (NVIDIA HBAO+ 2012)
    //
    //  For each direction slice:
    //    1. Project the surface normal onto the slice's 2D plane
    //    2. Compute the tangent angle of that projected normal — this is
    //       the geometric horizon below which no occlusion can exist
    //    3. March outward, tracking the maximum tangent (the horizon)
    //    4. Integrate the occluded arc of the cosine hemisphere
    //
    //  The key insight of HBAO+ is that normal-guided initialization means
    //  flat floors facing UP will naturally have low AO (horizon starts
    //  near 0° for horizontal surfaces), while crevices and corners have
    //  high AO (horizon starts negative, so almost any occluder registers).
    //
    //  Without the normal, all surfaces start with the same baseline,
    //  and the result is over-darkened flat surfaces + under-darkened corners.
    //
    //  Cosine integration:
    //    AO = 1 - (1/N) Σ [ sin(horizon) - sin(normalAngle) ]
    //    weighted by cosine of the normal angle in each slice.
    //    Using the identity: ∫cos(θ)dθ from α to β = sin(β) - sin(α)
    // =========================================================================

    float aspect  = ScreenSize.x / ScreenSize.y;
    float tanHFov = tan(FieldOfView * 0.5);

    // Project world-space radius to UV-space radius at this depth
    float projScale = tanHFov * 2.0 * centerZ;
    float uvRadius  = IN.Radius / projScale;

    // Clamp UV radius: at least 2.5px, at most 6% of screen height
    uvRadius = clamp(uvRadius, 2.5 * PixelSize.y, 0.06);

    int numDirs  = clamp((int)UIGTAO_Slices, 2, 8);
    int numSteps = clamp((int)UIGTAO_Steps,  2, 12);

    float angleOff = IN.Temporal > 0.5 ? TemporalAngleOffset(IN.texcoord) : 0.0;

    // ── Reconstruct surface normal ──────────────────────────────────────
    //
    // Try G-buffer normals first (includes normal map detail).
    // Fall back to depth-derived normals if TextureNormal is empty.

    float3 normal = TextureNormal.SampleLevel(Point_Sampler, IN.texcoord, 0).xyz * 2.0 - 1.0;
    float  normalLen = length(normal);

    if(normalLen < 0.1)
    {
        normal = NormalFromDepth(IN.texcoord);
    }
    else
    {
        normal /= normalLen;
        // Ensure camera-facing convention (z-negative in our view-space)
        float3 viewDir = normalize(ViewPosFromUV(IN.texcoord, centerZ));
        if(dot(normal, -viewDir) < 0.0) normal = -normal;
    }

    // Tangent bias: minimum tangent before counting as occluder.
    // The depth-proportional term compensates for decreasing depth buffer
    // precision at distance, but must be capped — without the cap, at
    // centerZ=3000 the bias reaches 0.15 (≈9° horizon floor), which
    // combined with the normal tangent kills all subtle distant AO.
    // Cap at 0.04 tangent (≈2.3°) — enough to suppress depth noise
    // without eating into real occlusion signal.
    float tanBias = IN.NormBias * 0.3 + min(centerZ * 0.00005, 0.04);

    // Thickness: maximum depth difference before rejecting as separate surface.
    // The base range scales with the AO radius, but at distance where depth
    // precision decreases, we need a wider acceptance window.  However, the
    // old uncapped linear growth (2 + ThickBias*12) could accept surfaces
    // that are clearly separate geometry.  Cap at 6× radius.
    float maxThick = IN.Radius * min(2.0 + IN.ThickBias * 12.0, 6.0);

    float aoSum = 0.0;

    // Sub-step jitter: per-pixel offset to reduce banding between steps
    float stepJitter = IN.Temporal > 0.5 ?
        TemporalIGN(IN.texcoord * ScreenRes, Timer.z) : 0.5;

    [loop] for(int d = 0; d < numDirs; d++)
    {
        // Half-circle: directions spaced PI/numDirs apart.
        // The ± march covers the full circle from half the directions.
        float dirAngle = (PI_PP / (float)numDirs) * ((float)d + 0.5) + angleOff;
        float2 dir2D   = float2(cos(dirAngle), sin(dirAngle));

        // UV step direction (aspect-corrected so circular in world space)
        float2 uvDir = dir2D * float2(1.0 / aspect, 1.0) * uvRadius;

        // ── Normal-guided horizon initialization (HBAO+) ────────────
        //
        // Project the 3D normal onto the 2D slice plane.  The slice lies
        // in view-space along (dir2D.x, 0, dir2D.y... wait, we need the
        // 3D direction corresponding to dir2D in view-space.
        //
        // The 2D direction (cos(a), sin(a)) in screen space maps to a
        // 3D view-space direction:
        //   sliceDir3D = normalize(dir2D.x * right + dir2D.y * up)
        // where right = (aspect*tanHFov, 0, 0) and up = (0, tanHFov, 0).
        //
        // But for the horizon calculation, we need the tangent of the
        // normal projected into the slice plane.  The slice plane contains
        // the camera forward direction and the 2D slice direction.
        //
        // Normal projected tangent = dot(normal, sliceDir) / dot(normal, forward)
        // where forward = (0, 0, 1) in our view-space convention.

        float3 sliceDir3D = normalize(float3(dir2D.x * aspect * tanHFov,
                                             -dir2D.y * tanHFov,
                                              0.0));
        // Project normal into slice plane
        float normalDotSlice = dot(normal, sliceDir3D);
        float normalDotFwd   = -normal.z; // normal faces camera (z-negative), forward is z-positive

        // Initial horizon tangent from the normal: surfaces facing the slice
        // direction start with a higher horizon (less room for occluders).
        // Surfaces facing away start with a negative tangent (more room).
        //
        // For a surface with normal perfectly along sliceDir:
        //   tangent = large positive → horizon already high → little AO possible
        // For a surface with normal opposing sliceDir:
        //   tangent = negative → horizon below geometric plane → maximum AO range
        //
        // The tangent of the normal angle in the slice =
        //   -normalDotSlice / max(normalDotFwd, epsilon)
        // Negated because a normal facing INTO the slice means the geometric
        // horizon is ABOVE the plane (positive tangent = less occlusion possible).

        float normalTan = -normalDotSlice / max(normalDotFwd, 0.01);

        // Clamp so that edge-on surfaces (normalDotFwd ≈ 0) don't explode
        normalTan = clamp(normalTan, -5.0, 5.0);

        // Start horizon search at the higher of (normal tangent, bias tangent)
        float initTan = max(normalTan, tanBias);

        float maxTanPos = initTan;
        float maxTanNeg = max(-normalTan, tanBias); // Opposite direction: flip normal projection

        [loop] for(int s = 1; s <= numSteps; s++)
        {
            // Linear step distribution with sub-step jitter.
            // HBAO needs uniform coverage because we track the MAXIMUM tangent.
            // (Unlike SAO where dense-near-center importance-samples the form factor.)
            float frac = ((float)s - 0.5 + stepJitter) / (float)numSteps;
            frac = saturate(frac);

            // World-space lateral distance at this step fraction
            float lateral = frac * IN.Radius;

            // ── Positive direction ──────────────────────────────────
            float2 uvP = IN.texcoord + uvDir * frac;

            if(all(uvP > 0.001) && all(uvP < 0.999))
            {
                float sZp = WorldZ(TextureDepth.SampleLevel(Point_Sampler, uvP, 0).x);
                float dzP = centerZ - sZp; // positive = closer to camera = occluder

                // Only count if within thickness range (reject separate surfaces)
                if(dzP > 0.0 && dzP < maxThick)
                {
                    float tanP = dzP / (lateral + DELTA);
                    maxTanPos = max(maxTanPos, tanP);
                }
            }

            // ── Negative direction ──────────────────────────────────
            float2 uvN = IN.texcoord - uvDir * frac;

            if(all(uvN > 0.001) && all(uvN < 0.999))
            {
                float sZn = WorldZ(TextureDepth.SampleLevel(Point_Sampler, uvN, 0).x);
                float dzN = centerZ - sZn;

                if(dzN > 0.0 && dzN < maxThick)
                {
                    float tanN = dzN / (lateral + DELTA);
                    maxTanNeg = max(maxTanNeg, tanN);
                }
            }
        }

        // ── Convert max tangent → horizon angle → cosine-weighted AO ────
        //
        // Horizon angle: h = atan(maxTan)
        // Normal angle in slice: n = atan(normalTan)
        //
        // GTAO visibility integral per Jimenez 2016:
        //   V = 0.25 * (-cos(2h) + 2h·sin(n) + cos(2n))
        //
        // This properly integrates the cosine-weighted hemisphere visibility
        // from the normal angle to the horizon angle, accounting for the
        // cos(θ) form factor at each elevation.
        //
        // Using sin(atan(x)) = x/√(1+x²) and cos(2·atan(x)) = (1-x²)/(1+x²)

        // Positive direction
        float hPosInv = 1.0 / (1.0 + maxTanPos * maxTanPos);
        float cos2hPos = (1.0 - maxTanPos * maxTanPos) * hPosInv;
        float sinHPos  = maxTanPos * sqrt(hPosInv);

        float nPosInv = 1.0 / (1.0 + initTan * initTan);
        float cos2nPos = (1.0 - initTan * initTan) * nPosInv;
        float sinNPos  = initTan * sqrt(nPosInv);

        float visPos = 0.25 * (-cos2hPos + 2.0 * asin(sinHPos) * sinNPos + cos2nPos);
        float visNBase = 0.25 * (-cos2nPos + 2.0 * asin(sinNPos) * sinNPos + cos2nPos);
        float occPos = max(visPos - visNBase, 0.0);

        // Negative direction
        float initNeg = max(-normalTan, tanBias);
        float hNegInv = 1.0 / (1.0 + maxTanNeg * maxTanNeg);
        float cos2hNeg = (1.0 - maxTanNeg * maxTanNeg) * hNegInv;
        float sinHNeg  = maxTanNeg * sqrt(hNegInv);

        float nNegInv = 1.0 / (1.0 + initNeg * initNeg);
        float cos2nNeg = (1.0 - initNeg * initNeg) * nNegInv;
        float sinNNeg  = initNeg * sqrt(nNegInv);

        float visNeg = 0.25 * (-cos2hNeg + 2.0 * asin(sinHNeg) * sinNNeg + cos2nNeg);
        float visNBase2 = 0.25 * (-cos2nNeg + 2.0 * asin(sinNNeg) * sinNNeg + cos2nNeg);
        float occNeg = max(visNeg - visNBase2, 0.0);

        // Each direction pair contributes the average of both sides
        aoSum += (occPos + occNeg) * 0.5;
    }

    // Average across all direction slices → occlusion in [0, ~1]
    float occlusion = aoSum / (float)numDirs;

    // Intensity: scale the raw occlusion (1.0 = natural, >1 = exaggerated)
    occlusion *= IN.Intensity;

    // Power curve: <1 opens up shadows, >1 deepens them
    float ao = saturate(1.0 - occlusion);
    ao = pow(ao, IN.Power);

    // Depth fade
    ao = lerp(1.0, ao, depthFade);

    // Pack: .r = AO, .g = linearized depth (for bilateral blur in composite)
    return float4(ao, linZ, 0.5, 1.0);
}


//=============================================================================//
//                                                                             //
//  TECH 4: SCREEN-SPACE DIRECTIONAL OCCLUSION (SSDO)                         //
//                                                                             //
//  Per Ritschel, Grosch, Seidel (2009) — adapted for single-pass ENB.        //
//                                                                             //
//  Dual-term indirect illumination:                                           //
//                                                                             //
//  Term 1 — Occluder radiance (SSDO):                                        //
//    Samples that are CLOSER to the camera block ambient light but also       //
//    bounce their own color toward us.  We weight by the same horizon         //
//    tangent geometry as HBAO: dz / (dz² + lat² + ε).  This produces         //
//    color bleeding exactly where AO darkening occurs — physically correct.   //
//                                                                             //
//  Term 2 — Ambient diffusion:                                               //
//    Co-planar neighbors at similar depth contribute gentle color wash.       //
//    This handles wall-to-wall bleeding that SSDO misses (same depth,         //
//    different color).  Weighted by depth bilateral × distance falloff.       //
//                                                                             //
//  Output: RenderTargetRGBA64F                                                //
//    .rgb = indirect illumination color                                       //
//    .a   = linearized depth (for bilateral blur in composite)                //
//                                                                             //
//=============================================================================//

struct SSGIVSOutput
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
NI float  Intensity  : I0;
NI float  Radius     : I1;
NI float  DepthReject: I2;
NI float  NormWeight : I3;
NI float  ColorBleed : I4;
NI float  DepthFade  : I5;
NI float  Temporal   : I6;
NI float  Saturation : I7;
NI float  Active     : I8;
};

SSGIVSOutput VS_SSGI(VertexShaderInput IN)
{
    SSGIVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    OUT.Intensity   = UISSGI_Intensity;
    OUT.Radius      = UISSGI_Radius;
    OUT.DepthReject = UISSGI_DepthReject;
    OUT.NormWeight  = UISSGI_NormWeight;
    OUT.ColorBleed  = UISSGI_ColorBleed;
    OUT.DepthFade   = UISSGI_DepthFade;
    OUT.Temporal    = UISSGI_Temporal;
    OUT.Saturation  = UISSGI_Saturation;
    OUT.Active      = UISSGI_Enable;

    return OUT;
}

float4 PS_SSGI(SSGIVSOutput IN) : SV_Target
{
    if(IN.Active < 0.5)
        return float4(0.0, 0.0, 0.0, 0.0);

    float rawDepth = TextureDepth.SampleLevel(Point_Sampler, IN.texcoord, 0).x;
    float centerZ  = WorldZ(rawDepth);
    float linZ     = LinZ(rawDepth);

    if(linZ > 0.95 || linZ < DELTA)
        return float4(0.0, 0.0, 0.0, linZ);

    float depthFade = 1.0 - smoothstep(IN.DepthFade * 0.5, IN.DepthFade, centerZ);
    if(depthFade < 0.001)
        return float4(0.0, 0.0, 0.0, linZ);

    // =========================================================================
    //  SSDO — Screen-Space Directional Occlusion with ambient diffusion
    //
    //  Term 1 (occluder radiance):
    //    For each sample where dz > 0 (closer to camera = occluder):
    //      weight = dz / (dz² + lat² + ε)     (same as HBAO/SAO geometry)
    //      color  = sampleColor × weight
    //
    //    This is the light bouncing off the occluding surface toward us.
    //    Strongest in tight cavities and near adjacent surfaces.
    //
    //  Term 2 (ambient diffusion):
    //    For all samples at similar depth (co-planar neighbors):
    //      weight = exp(-relDepthDiff² × reject) × distanceFalloff
    //      color  = max(sampleColor - centerColor, 0) × weight
    //
    //    This captures color transfer between surfaces at the same depth —
    //    e.g. red wallpaper bleeding onto a white doorframe at the same depth.
    //
    //  NormWeight controls the balance: 0 = pure diffusion, 2 = pure SSDO.
    // =========================================================================

    float3 centerColor = TextureColor.SampleLevel(Linear_Sampler, IN.texcoord, 0).rgb;

    float aspect   = ScreenSize.x / ScreenSize.y;
    float tanHFov  = tan(FieldOfView * 0.5);
    float projScale = tanHFov * 2.0 * centerZ;

    float uvRadius = IN.Radius / projScale;
    uvRadius = clamp(uvRadius, 3.0 * PixelSize.y, 0.12);

    int numSamples = clamp((int)UISSGI_Samples, 4, 24);
    float baseAngle = IN.Temporal > 0.5 ? TemporalAngleOffset(IN.texcoord) : 0.0;

    // ── Reconstruct surface normal for cosine-weighted SSDO ─────────────
    //
    // Per Ritschel 2009: occluder contributions are weighted by
    // max(dot(N, dirToOccluder), 0) — the cosine of the angle between
    // the surface normal and the direction to the bouncing surface.
    // Without this, samples behind the surface (wrong hemisphere)
    // contribute equally, causing light to bleed through walls.

    float3 normal = TextureNormal.SampleLevel(Point_Sampler, IN.texcoord, 0).xyz * 2.0 - 1.0;
    float  normalLen = length(normal);

    if(normalLen < 0.1)
    {
        normal = NormalFromDepth(IN.texcoord);
    }
    else
    {
        normal /= normalLen;
        float3 vd = normalize(ViewPosFromUV(IN.texcoord, centerZ));
        if(dot(normal, -vd) < 0.0) normal = -normal;
    }

    float3 viewPos = ViewPosFromUV(IN.texcoord, centerZ);

    float3 ssdoAccum    = 0.0;  // Term 1: occluder radiance
    float3 diffuseAccum = 0.0;  // Term 2: ambient diffusion
    float  diffuseWeight = 0.0;

    float maxThick = IN.Radius * 4.0; // Thickness for SSDO term

    // Epsilon: depth-adaptive. The SAO form factor dz/(dz²+lat²+ε) needs ε
    // proportional to depth² because both dz and lat scale with depth.
    // Additionally, a small minimum prevents noise at very close range.
    float epsilon = max(0.01, centerZ * centerZ * 0.000005);

    // Minimum depth delta to count as an occluder.
    // Scales with depth: far geometry has larger dz even for subtle occlusion.
    float minDz = 0.03 + centerZ * 0.0002;

    static const float GA = 2.39996323;

    [loop] for(int i = 0; i < numSamples; i++)
    {
        float fi    = (float)i + 0.5;
        float r     = sqrt(fi / (float)numSamples);
        float theta = fi * GA + baseAngle;

        float2 dir   = float2(cos(theta), sin(theta)) * r;
        float2 uvOff = dir * uvRadius * float2(1.0 / aspect, 1.0);
        float2 sUV   = IN.texcoord + uvOff;

        if(any(sUV < 0.001) || any(sUV > 0.999)) continue;

        float  sZ     = WorldZ(TextureDepth.SampleLevel(Point_Sampler, sUV, 0).x);
        float3 sColor = TextureColor.SampleLevel(Linear_Sampler, sUV, 0).rgb;

        float dz  = centerZ - sZ;         // positive = closer = occluder
        float lat = r * IN.Radius + DELTA; // world-space lateral distance

        // ── Term 1: SSDO (occluder radiance) ────────────────────────
        //
        // Cosine-weighted per Ritschel 2009: weight by max(dot(N, dirToSample), 0)
        // so samples in the surface's front hemisphere contribute, while those
        // behind the surface are rejected.  This prevents light bleeding through
        // walls and ensures GI respects surface orientation.
        //
        // The SAO form factor dz/(dz²+lat²+ε) is already an importance weight
        // representing the fractional solid angle subtended by the occluder.
        // The cosine term additionally ensures energy conservation:
        //
        //   E_indirect ≈ (1/N) Σ L_occluder × dΩ × cos(θ)

        if(dz > minDz && dz < maxThick)
        {
            float vv = dz * dz + lat * lat;
            float w  = dz / (vv + epsilon);

            // Direction from center to sample in view-space
            float3 sampleViewPos = ViewPosFromUV(sUV, sZ);
            float3 dirToSample   = normalize(sampleViewPos - viewPos);

            // Cosine weight: how much the sample direction aligns with our normal
            float cosWeight = max(dot(normal, dirToSample), 0.0);

            ssdoAccum += sColor * w * cosWeight;
        }

        // ── Term 2: Ambient diffusion (co-planar color transfer) ────
        //
        // Luminance-aware: brighter neighbors bounce more light proportionally.
        // A bright torch next to a dark stone wall bleeds more than two dim walls.
        {
            float relDz = abs(dz) / (centerZ + 1.0);
            float dW    = exp(-relDz * relDz * IN.DepthReject);
            float sW    = 1.0 - r * r; // Distance falloff

            float w = dW * sW;

            // Per-channel differential: light the neighbor has that we lack
            float3 diff = max(sColor - centerColor, 0.0);

            // Luminance boost: scale by how bright the source is relative to center
            float sLum = dot(sColor, K_LUM) + DELTA;
            float cLum = dot(centerColor, K_LUM) + DELTA;
            float lumWeight = saturate(sLum / (cLum + sLum));

            diffuseAccum  += diff * w * (0.5 + lumWeight);
            diffuseWeight += w;
        }
    }

    // ── Normalize ─────────────────────────────────────────────────────
    //
    // SSDO: divide by sample count (Monte Carlo normalization).
    // The scale factor compensates for the small solid-angle values produced
    // by the SAO form factor.  Made radius-dependent: larger radii sample
    // weaker occluders, so need more amplification to match perceived GI.
    float ssdoScale = 4.0 + IN.Radius * 0.8;
    float3 ssdoColor = (ssdoAccum / (float)numSamples) * ssdoScale;

    // Diffusion: still normalize by weight sum — it's a weighted average
    // of differential colors, not an irradiance integral.
    float3 diffuseColor = (diffuseWeight > DELTA) ? (diffuseAccum / diffuseWeight) : 0.0;

    // Blend: NormWeight controls the SSDO-to-diffusion balance
    // At 1.0 (default): equal parts.  At 2.0: mostly SSDO.  At 0.0: mostly diffusion.
    float ssdoMix = saturate(IN.NormWeight * 0.5);
    float3 indirect = lerp(diffuseColor, ssdoColor, ssdoMix);

    // Color controls
    float indLuma = dot(indirect, K_LUM);
    indirect = lerp(indLuma, indirect, IN.Saturation) * IN.ColorBleed;
    indirect *= IN.Intensity * depthFade;

    // Pack: .rgb = indirect color, .a = linearized depth (for bilateral blur)
    return float4(indirect, linZ);
}


//=============================================================================//
//                                                                             //
//  TECH 5: SCREEN-SPACE CONTACT SHADOWS                                      //
//                                                                             //
//  Ray-marches from each pixel along the projected sun direction in screen    //
//  space to find micro-occlusion that shadow maps miss — detail under the    //
//  nose, between fingers, where objects rest on surfaces, creases in cloth.   //
//                                                                             //
//  Algorithm:                                                                 //
//    1. Project SunDirection.xy to UV-space ray direction                     //
//    2. March with exponential step sizes (dense near, sparse far)            //
//    3. At each step: is the scene surface closer than my depth? If so,       //
//       something sits between me and the light → shadow hit                  //
//    4. Track the closest hit for penumbra estimation                         //
//    5. Accumulate shadow with distance-dependent softness                    //
//                                                                             //
//  No z-tracking (SunDirection.z sign varies across ENB builds).              //
//  No NdL gate (normal convention unreliable across setups).                  //
//  Pure 2D march + depth comparison — the most robust approach.               //
//                                                                             //
//  Output: RenderTargetR16F                                                   //
//    .r = shadow factor [0,1] (1 = fully lit, 0 = fully shadowed)            //
//                                                                             //
//=============================================================================//

struct ContactShadowVSOutput
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
NI float  Intensity : C0;
NI float  MaxDist   : C1;
NI float  Thickness : C2;
NI float  DepthFade : C3;
NI float  NdLFade   : C4;
NI float  Temporal  : C5;
NI float  Softness  : C6;
NI float  Active    : C7;
};

ContactShadowVSOutput VS_ContactShadow(VertexShaderInput IN)
{
    ContactShadowVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    OUT.Intensity = UICS_Intensity;
    OUT.MaxDist   = UICS_MaxDist;
    OUT.Thickness = UICS_Thickness;
    OUT.DepthFade = UICS_DepthFade;
    OUT.NdLFade   = UICS_NdLFade;
    OUT.Temporal  = UICS_Temporal;
    OUT.Softness  = UICS_Softness;
    OUT.Active    = UICS_Enable;

    return OUT;
}

float4 PS_ContactShadow(ContactShadowVSOutput IN) : SV_Target
{
    if(IN.Active < 0.5)
        return 1.0;

    float rawDepth = TextureDepth.SampleLevel(Point_Sampler, IN.texcoord, 0).x;
    float centerZ  = WorldZ(rawDepth);
    float linZ     = LinZ(rawDepth);

    if(linZ > 0.95 || linZ < DELTA || centerZ < 0.5)
        return 1.0;

    float depthFade = 1.0 - smoothstep(IN.DepthFade * 0.5, IN.DepthFade, centerZ);
    if(depthFade < 0.001)
        return 1.0;

    // =========================================================================
    //  Contact Shadow Ray March
    //
    //  March from each pixel along the projected sun direction in UV space.
    //
    //  SkyrimBridge path: SB_Sun_Direction.xyz is a normalized world-space
    //  vector that we project to screen-space.  This is accurate at all sun
    //  elevations — the old SunDirection.xy direct-use produces near-zero
    //  length at high noon (sun directly overhead) causing shadows to vanish.
    //
    //  Fallback: SunDirection.xy treated as a screen-space direction (same
    //  as the existing working assumption in this shader).
    //
    //  NdL gating: surfaces facing away from the sun are already in shadow
    //  from cascade maps — adding contact shadow darkening over-darkens them.
    //  We fade contact shadows by max(NdL, 0) with a soft ramp.
    // =========================================================================

    float2 sunDir2D;

#ifdef SKYRIMBRIDGE_FXH
    [branch] if(SB_IsActive())
    {
        // Project 3D sun direction to screen-space 2D direction.
        // We don't need the sun's screen POSITION (that's for god rays),
        // just which DIRECTION light comes from in screen space.
        float3 sd = SB_Sun_Direction.xyz;
        float aspect  = ScreenSize.x / ScreenSize.y;
        float tanHFov = tan(FieldOfView * 0.5);

        // View-space light direction: project world-space sun dir onto
        // screen axes.  We only need the normalized 2D direction.
        sunDir2D = float2(sd.x * aspect * tanHFov, -sd.y * tanHFov);
        float sd2Len = length(sunDir2D);
        if(sd2Len < 0.001) return 1.0; // sun directly overhead — no screen-space shadow dir
        sunDir2D /= sd2Len;
    }
    else
#endif
    {
        sunDir2D = SunDirection.xy;
        float sunLen = length(sunDir2D);
        if(sunLen < 0.01) return 1.0;
        sunDir2D /= sunLen;
    }

    // ── NdL gating ──────────────────────────────────────────────────────
    // Read surface normal and compute how much the surface faces the sun.
    // Surfaces in cascade shadow (NdL ≤ 0) don't need contact shadow.
    float ndlGate = 1.0;
    if(IN.NdLFade > 0.01)
    {
        float3 normal = TextureNormal.SampleLevel(Point_Sampler, IN.texcoord, 0).xyz * 2.0 - 1.0;
        float  nLen   = length(normal);
        if(nLen > 0.1)
        {
            normal /= nLen;
            float3 sunWorld;
        #ifdef SKYRIMBRIDGE_FXH
            sunWorld = SB_IsActive() ? SB_Sun_Direction.xyz : normalize(SunDirection.xyz);
        #else
            sunWorld = normalize(SunDirection.xyz);
        #endif
            float NdL = dot(normal, sunWorld);
            ndlGate = smoothstep(-0.1, 0.3, NdL);
            ndlGate = lerp(1.0, ndlGate, IN.NdLFade);
        }
    }

    if(ndlGate < 0.01)
        return 1.0;

    float tanHFov_cs  = tan(FieldOfView * 0.5);
    float projScale = tanHFov_cs * 2.0 * centerZ;

    // Total UV distance to march
    float totalUV = IN.MaxDist / projScale;
    totalUV = min(totalUV, 0.10);

    int numSteps = clamp((int)UICS_Steps, 8, 48);

    // Temporal sub-pixel jitter: offsets each frame to reduce banding
    float dither = IN.Temporal > 0.5 ?
        TemporalIGN(IN.texcoord * ScreenRes, Timer.z) : 0.5;

    // Thickness window: how much closer a surface must be to shadow us.
    //   The UI control (0.001 to 0.5) maps to a world-space thickness range.
    //   At close range, thin occluders (fingers, fabric) need a tight window.
    //   At distance, depth buffer precision decreases → wider window needed.
    //
    //   Base thickness:  UI × world radius, producing sensible values:
    //     0.04 × 5.0 = 0.20 units at default (≈ 2cm for a 10-unit character)
    //   Depth scaling:   gradual with a cap to prevent false positives at
    //     distance (the old uncapped 0.003 × centerZ grew to 3× at 1000 units,
    //     accepting separate geometry as occluders).
    float thickBase = IN.Thickness * IN.MaxDist + 0.05;
    float thickFull = thickBase * (1.0 + min(centerZ * 0.003, 1.5));

    // Self-shadow bias: minimum depth delta to register as occluder.
    // Tighter bias at close range (high precision), wider at distance.
    float bias = 0.08 + centerZ * 0.0003;

    // Shadow accumulation
    float bestShadow  = 0.0;   // Strongest shadow found
    float closestHitT = 2.0;   // Normalized distance of closest hit (>1 = none)
    float hitT_raw    = -1.0;  // Raw t parameter of strongest hit (for binary search)

    [loop] for(int i = 0; i < numSteps; i++)
    {
        // Step distribution: t^1.6 concentrates samples near surface
        float t_raw = ((float)i + dither) / (float)numSteps;
        float t     = pow(t_raw, 1.6);

        float2 rayUV = IN.texcoord + sunDir2D * totalUV * t;

        if(any(rayUV < 0.002) || any(rayUV > 0.998))
            break;

        float sceneZ  = WorldZ(TextureDepth.SampleLevel(Point_Sampler, rayUV, 0).x);
        float occDist = centerZ - sceneZ; // positive = scene is closer = potential occluder

        if(occDist > bias && occDist < thickFull)
        {
            // How solid: ramp from 0 at bias to 1 at 2x base thickness
            float solidness = saturate((occDist - bias) / (thickBase * 0.4 + DELTA));

            // Penumbra: closer hits are harder shadows, far hits are softer
            float hardness = lerp(1.0, max(0.1, 1.0 - t_raw), IN.Softness);

            float shadow = solidness * hardness;
            if(shadow > bestShadow)
            {
                bestShadow = shadow;
                hitT_raw   = t_raw;
            }
            closestHitT = min(closestHitT, t_raw);
        }
    }

    // ── Binary search refinement ────────────────────────────────────────
    //
    // The linear march has step-sized resolution, producing stair-stepped
    // shadow edges.  Binary search between the last non-hit step and the
    // first hit step finds the precise shadow boundary.
    // 4 iterations → 1/16th of a step precision → effectively sub-pixel.

    if(bestShadow > 0.01 && hitT_raw > 0.0)
    {
        float stepSize = 1.0 / (float)numSteps;
        float tLo = max(hitT_raw - stepSize, 0.0);
        float tHi = hitT_raw;

        [loop] for(int r = 0; r < 4; r++)
        {
            float tMid   = (tLo + tHi) * 0.5;
            float tWorld = pow(tMid, 1.6);
            float2 midUV = IN.texcoord + sunDir2D * totalUV * tWorld;

            if(any(midUV < 0.002) || any(midUV > 0.998)) break;

            float sceneZ  = WorldZ(TextureDepth.SampleLevel(Point_Sampler, midUV, 0).x);
            float occDist = centerZ - sceneZ;

            if(occDist > bias && occDist < thickFull)
                tHi = tMid;
            else
                tLo = tMid;
        }

        // Recompute shadow at the refined boundary for smoother edges
        float tRefined = pow(tHi, 1.6);
        float2 refUV = IN.texcoord + sunDir2D * totalUV * tRefined;
        if(all(refUV > 0.002) && all(refUV < 0.998))
        {
            float refZ   = WorldZ(TextureDepth.SampleLevel(Point_Sampler, refUV, 0).x);
            float refOcc = centerZ - refZ;
            if(refOcc > bias && refOcc < thickFull)
            {
                float refSolid = saturate((refOcc - bias) / (thickBase * 0.4 + DELTA));
                float refHard  = lerp(1.0, max(0.1, 1.0 - tHi), IN.Softness);
                bestShadow = max(bestShadow, refSolid * refHard);
            }
        }
    }

    // Screen-edge fade: prevent hard cutoffs at viewport borders
    float2 edgeDist = min(IN.texcoord, 1.0 - IN.texcoord);
    float edgeFade  = saturate(min(edgeDist.x, edgeDist.y) / 0.04);

    float shadow = bestShadow * IN.Intensity * depthFade * edgeFade * ndlGate;

    return 1.0 - saturate(shadow);
}


//=============================================================================//
//                                                                             //
//  TECH 6: SCREEN-SPACE REFLECTIONS (SSR)                                    //
//                                                                             //
//  Linear ray march in screen space along the view-reflected direction,       //
//  with binary search refinement at the hit boundary.                         //
//                                                                             //
//  Algorithm:                                                                 //
//    1. Reconstruct view-space position and depth-derived normal              //
//    2. Reflect the view direction across the normal                          //
//    3. Project a point along the reflection ray to screen space              //
//    4. Linear march in UV+Z: compare expected ray depth vs scene depth       //
//    5. On first intersection, refine with binary search (4 iterations)       //
//    6. Fresnel-weighted output: grazing angles reflect more                  //
//                                                                             //
//  Output: RenderTargetRGBA32                                                 //
//    .rgb = reflected color                                                   //
//    .a   = reflection confidence [0,1]                                       //
//                                                                             //
//=============================================================================//

struct SSRVSOutput
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
NI float  Intensity  : R0;
NI float  MaxDist    : R1;
NI float  Thickness  : R2;
NI float  DepthFade  : R3;
NI float  EdgeFade   : R4;
NI float  FresnelPow : R5;
NI float  Active     : R6;
NI float  Temporal   : R7;
};

SSRVSOutput VS_SSR(VertexShaderInput IN)
{
    SSRVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    OUT.Intensity  = UISSR_Intensity;
    OUT.MaxDist    = UISSR_MaxDist;
    OUT.Thickness  = UISSR_Thickness;
    OUT.DepthFade  = UISSR_DepthFade;
    OUT.EdgeFade   = UISSR_EdgeFade;
    OUT.FresnelPow = UISSR_FresnelPow;
    OUT.Active     = UISSR_Enable;
    OUT.Temporal   = UISSR_Temporal;

    return OUT;
}

float4 PS_SSR(SSRVSOutput IN) : SV_Target
{
    if(IN.Active < 0.5)
        return float4(0.0, 0.0, 0.0, 0.0);

    float rawDepth = TextureDepth.SampleLevel(Point_Sampler, IN.texcoord, 0).x;
    float centerZ  = WorldZ(rawDepth);
    float linZ     = LinZ(rawDepth);

    if(linZ > 0.95 || linZ < DELTA)
        return float4(0.0, 0.0, 0.0, 0.0);

    float depthFade = 1.0 - smoothstep(IN.DepthFade * 0.5, IN.DepthFade, centerZ);
    if(depthFade < 0.001)
        return float4(0.0, 0.0, 0.0, 0.0);

    // ─── Reconstruct view-space geometry ────────────────────────────────
    float3 viewPos = ViewPosFromUV(IN.texcoord, centerZ);

    // Use ENB's G-buffer normals (includes normal map detail: pores, bumps,
    // surface scratches).  Decode from [0,1] → [-1,1].
    // Convention: z < 0 faces the camera in our view-space (z+ into screen).
    // If Skyrim stores z > 0 for camera-facing normals, negate z here.
    float3 normal = TextureNormal.SampleLevel(Point_Sampler, IN.texcoord, 0).xyz * 2.0 - 1.0;
    float  normalLen = length(normal);

    // Fallback to depth-derived normals if TextureNormal is invalid
    // (some ENB configurations may not populate this buffer)
    if(normalLen < 0.1)
    {
        normal = NormalFromDepth(IN.texcoord);
    }
    else
    {
        normal /= normalLen;
        // Ensure normal points toward camera (z-negative in our convention).
        // If the G-buffer stores normals with z > 0 toward camera, negate z.
        float3 viewDir_test = normalize(viewPos);
        if(dot(normal, -viewDir_test) < 0.0)
            normal = -normal;
    }

    // View direction: from camera (origin) toward the pixel
    float3 viewDir = normalize(viewPos);

    // Reflection direction
    float3 reflDir = reflect(viewDir, normal);

    // Backward-facing reflections fade smoothly instead of a hard cutoff.
    // reflDir.z < 0 means pointing toward camera — unlikely to hit anything
    // on screen.  The transition band must be narrow enough that the fade
    // doesn't create a visible vertical seam at screen center (where
    // reflDir.z ≈ 0 for horizontal surfaces), but wide enough to avoid
    // popping.  The asymmetric range [-0.15, 0.2] centers the steepest
    // gradient slightly behind zero so that horizontal reflections at
    // screen center get ~70% strength instead of ~50% (the old [-0.5, 0.2]
    // spread the gradient so wide that screen-center pixels sat at ~30%).
    float backwardFade = smoothstep(-0.15, 0.2, reflDir.z);
    if(backwardFade < 0.01)
        return float4(0.0, 0.0, 0.0, 0.0);

    // ─── Fresnel: grazing angles reflect more ───────────────────────────
    float NdV     = saturate(dot(normal, -viewDir));
    float fresnel = pow(1.0 - NdV, IN.FresnelPow);
    fresnel = lerp(0.04, 1.0, fresnel); // F0=0.04 (dielectric)

    if(fresnel < 0.01)
        return float4(0.0, 0.0, 0.0, 0.0);

    // ─── Set up screen-space ray march ──────────────────────────────────
    float3 endPos = viewPos + reflDir * IN.MaxDist;

    // Clamp end position to near plane (z > 1).  If reflection goes behind
    // the camera, smoothly shorten the ray to avoid projection singularities
    // that cause the vertical center-line artifact.
    float maxRayT = IN.MaxDist;
    if(endPos.z < 1.0 && reflDir.z < -0.001)
    {
        float tClamp = (1.0 - viewPos.z) / reflDir.z;
        maxRayT = clamp(tClamp, 0.1, IN.MaxDist);
        endPos = viewPos + reflDir * maxRayT;
    }
    // Safety: always keep endpoint in front of camera
    endPos.z = max(endPos.z, 0.5);

    float2 startUV = IN.texcoord;
    float2 endUV   = UVFromViewPos(endPos);

    float2 deltaUV = endUV - startUV;

    // Clamp max UV travel — prevent huge marches across the screen.
    // When clamping, recompute the end depth by finding where along the
    // original ray the shortened UV endpoint falls.  We interpolate in
    // the ray's t parameter, not in view-space (which would be wrong
    // because UV distance ≠ view-space distance under perspective).
    float uvLen = length(deltaUV);
    if(uvLen > 0.6)
    {
        float tScale = 0.6 / uvLen;
        deltaUV *= tScale;
        endUV = startUV + deltaUV;
        // endPos at the shortened parameter: lerp along the original ray
        endPos = viewPos + reflDir * IN.MaxDist * tScale;
    }

    int numSteps = clamp((int)UISSR_Steps, 16, 64);
    int refineSteps = clamp((int)UISSR_RefineSteps, 0, 8);

    // ── Minimum step size guarantee ─────────────────────────────────────
    // If the total UV travel is so short that each step is sub-pixel,
    // reduce the step count so each step covers at least 1 pixel.
    float totalPixels = length(deltaUV * ScreenRes);
    if(totalPixels < (float)numSteps)
        numSteps = max((int)totalPixels, 4);

    // Per-pixel jitter to break up stair-stepping
    float jitter = IN.Temporal > 0.5 ?
        TemporalIGN(IN.texcoord * ScreenRes, Timer.z) : 0.5;

    // ── Perspective-correct depth interpolation ─────────────────────────
    //
    // Linear Z interpolation in screen space is incorrect — depth changes
    // non-linearly under perspective projection.  Interpolating in 1/Z
    // (reciprocal depth) IS linear in screen space.
    //
    //   rayZ(t) = 1.0 / lerp(1/startZ, 1/endZ, t)
    //
    // This prevents the ray from "dipping" below surfaces at near-end
    // and "floating" above at far-end, which causes false hits/misses.

    float startInvZ = 1.0 / max(viewPos.z, 0.1);
    float endInvZ   = 1.0 / max(endPos.z, 0.1);

    // ─── Linear ray march ───────────────────────────────────────────────
    bool   hit   = false;
    float  hitT  = 0.0;
    float2 hitUV = startUV;

    float jitterFrac = jitter / (float)numSteps;

    [loop] for(int i = 1; i <= numSteps; i++)
    {
        float t = (float)i / (float)numSteps + jitterFrac;
        float2 rayUV = startUV + deltaUV * t;

        if(any(rayUV < 0.001) || any(rayUV > 0.999))
            break;

        // Perspective-correct depth at parameter t
        float rayZ   = 1.0 / lerp(startInvZ, endInvZ, t);
        float sceneZ = WorldZ(TextureDepth.SampleLevel(Point_Sampler, rayUV, 0).x);

        float diff = rayZ - sceneZ; // positive = ray behind scene surface

        // Depth-adaptive thickness: near surfaces need tight tolerance,
        // distant surfaces have lower depth precision.  Cap the growth
        // factor to prevent false hits at large distances (water surfaces
        // at hundreds of units would blow this out without the cap, causing
        // reflections to "bleed through" the water plane).
        float adaptThick = IN.Thickness * (1.0 + min(sceneZ * 0.005, 1.5));

        // Sky rejection: if the scene sample is at the far plane, the ray
        // has passed through all geometry and hit sky — not a valid hit.
        float sceneLinZ = LinZ(TextureDepth.SampleLevel(Point_Sampler, rayUV, 0).x);

        if(diff > 0.0 && diff < adaptThick && sceneLinZ < 0.98)
        {
            hit   = true;
            hitT  = t;
            hitUV = rayUV;
            break;
        }
    }

    if(!hit)
        return float4(0.0, 0.0, 0.0, 0.0);

    // ─── Binary search refinement ───────────────────────────────────────
    if(refineSteps > 0)
    {
        float tLo = hitT - 1.0 / (float)numSteps;
        float tHi = hitT;

        [loop] for(int r = 0; r < refineSteps; r++)
        {
            float tMid = (tLo + tHi) * 0.5;
            float2 midUV = startUV + deltaUV * tMid;

            if(any(midUV < 0.001) || any(midUV > 0.999)) break;

            float midRayZ = 1.0 / lerp(startInvZ, endInvZ, tMid);
            float sceneZ  = WorldZ(TextureDepth.SampleLevel(Point_Sampler, midUV, 0).x);
            float diff    = midRayZ - sceneZ;
            float adaptThick = IN.Thickness * (1.0 + min(sceneZ * 0.005, 1.5));

            if(diff > 0.0 && diff < adaptThick)
            {
                tHi   = tMid;
                hitUV = midUV;
            }
            else
            {
                tLo = tMid;
            }
        }

        hitT = tHi;
    }

    // ─── Sample reflected color ─────────────────────────────────────────
    float3 reflColor = TextureColor.SampleLevel(Linear_Sampler, hitUV, 0).rgb;

    // ─── Confidence masking ─────────────────────────────────────────────

    // Screen-edge fade: squared smoothstep for natural vignette-like falloff
    // that never creates hard lines at any screen position
    float2 edgeDist  = min(hitUV, 1.0 - hitUV);
    float  edgeFadeX = smoothstep(0.0, IN.EdgeFade, edgeDist.x);
    float  edgeFadeY = smoothstep(0.0, IN.EdgeFade, edgeDist.y);
    float  edgeMask  = edgeFadeX * edgeFadeY;

    // Distance fade: farther hits are less reliable
    float distMask = 1.0 - saturate(hitT);

    // Facing fade: reflections pointing into the screen (z > 0.2) are reliable;
    // sideways ones (z ≈ 0) are less so.  Tightened to match backwardFade range.
    float facingMask = smoothstep(-0.05, 0.25, reflDir.z);

    // Combine all confidence factors
    float confidence = fresnel * edgeMask * distMask * facingMask * backwardFade * depthFade * IN.Intensity;

    return float4(reflColor, saturate(confidence));
}


//=============================================================================//
//                                                                             //
//  TECH 7: VOLUMETRIC GOD RAYS                                               //
//                                                                             //
//  Per Mitchell (Crytek, 2007) "Volumetric Light Scattering as a Post-       //
//  Process" — adapted for ENB with depth-based occlusion masking.             //
//                                                                             //
//  For each pixel, radially march toward the sun's screen position.           //
//  At each step, test if the sample is sky (light passes through) or          //
//  geometry (blocks light).  Accumulate with exponential decay.               //
//                                                                             //
//  The sun's screen position is derived by projecting SunDirection            //
//  (view-space direction to sun) onto the screen plane.                       //
//                                                                             //
//  Output: RenderTargetR32F                                                   //
//    .r = shaft intensity [0,1]                                               //
//                                                                             //
//=============================================================================//

struct GodRayVSOutput
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
NI float  Intensity : LR0;
NI float  Density   : LR1;
NI float  Decay     : LR2;
NI float  Exposure  : LR3;
NI float  Threshold : LR4;
NI float  Active    : LR5;
};

GodRayVSOutput VS_GodRays(VertexShaderInput IN)
{
    GodRayVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    OUT.Intensity = UIGR_Intensity;
    OUT.Density   = UIGR_Density;
    OUT.Decay     = UIGR_Decay;
    OUT.Exposure  = UIGR_Exposure;
    OUT.Threshold = UIGR_Threshold;
    OUT.Active    = UIGR_Enable;

    return OUT;
}

float4 PS_GodRays(GodRayVSOutput IN) : SV_Target
{
    if(IN.Active < 0.5)
        return 0.0;

    // ─── Compute sun screen position ────────────────────────────────────
    //
    //  ENB's SunDirection semantic is ambiguous across builds (documented as
    //  "screen-space direction" but sometimes provides view-space).  The old
    //  code assumed view-space and projected via tanHFov, which double-
    //  transforms if ENB already provides screen-space — sending sunUV
    //  off-screen and producing zero god rays.
    //
    //  Fix: SkyrimBridge gives exact screen UV from SKSE (no ambiguity).
    //  Fallback treats SunDirection.xyz as a view-space direction and
    //  projects with a divide-by-z that handles low-elevation gracefully
    //  (the old sunDir.z < 0.05 guard killed rays at the horizon — exactly
    //  when they look best).
    //
    //  Off-screen sun: god rays should still radiate from a sun position
    //  that's just outside the viewport.  We allow sunUV to sit up to 0.5
    //  beyond [0,1] — the radial march naturally clips samples that leave
    //  the screen, producing the correct partial-shaft appearance.

    float2 sunUV;
    bool   sunBehind = false;

#ifdef SKYRIMBRIDGE_FXH
    [branch] if(SB_IsActive())
    {
        // SkyrimBridge: exact screen-space UV, no projection ambiguity
        sunUV     = SB_SunScreenUV();
        sunBehind = !SB_IsSunOnScreen() && (SB_Sun_NDC.x * SB_Sun_NDC.x +
                    SB_Sun_NDC.y * SB_Sun_NDC.y) > 4.0; // well behind/far off screen
    }
    else
#endif
    {
        // Fallback: project SunDirection as view-space direction
        if(SunDirection.w > 0.5)
            return 0.0; // sun fully behind camera

        float3 sunDir = SunDirection.xyz;
        float  sunLen = length(sunDir);
        if(sunLen < 0.001) return 0.0;
        sunDir /= sunLen;

        // Guard: sun extremely close to camera plane — projection unstable
        // Use a much softer threshold than the old 0.05 to preserve low-sun rays
        float aspect  = ScreenSize.x / ScreenSize.y;
        float tanHFov = tan(FieldOfView * 0.5);

        if(sunDir.z < 0.001)
        {
            // Near-horizon: extrapolate direction as a screen-edge position
            // Project the xy direction to UV space, place sun at the viewport edge
            float2 edgeDir = normalize(float2(sunDir.x / aspect, -sunDir.y));
            sunUV = 0.5 + edgeDir * 0.7; // Push to ~70% toward edge
        }
        else
        {
            float2 sunNDC;
            sunNDC.x =  sunDir.x / (sunDir.z * aspect * tanHFov);
            sunNDC.y = -sunDir.y / (sunDir.z * tanHFov);
            sunUV = sunNDC * 0.5 + 0.5;
        }
    }

    if(sunBehind)
        return 0.0;

    // Clamp sunUV to prevent extreme march distances (allow off-screen up to 0.5)
    sunUV = clamp(sunUV, -0.5, 1.5);

    // ─── Radial march from pixel toward sun ─────────────────────────────
    int numSamples = clamp((int)UIGR_Samples, 16, 128);

    float2 rayToSun = sunUV - IN.texcoord;
    float  rayLen   = length(rayToSun);

    // Scale march distance by density
    float2 totalDelta = rayToSun * IN.Density;
    float2 deltaUV    = totalDelta / (float)numSamples;

    // ── Temporal dithering ──────────────────────────────────────────────
    float dither = TemporalIGN(IN.texcoord * ScreenRes, Timer.z);
    float2 sampleUV = IN.texcoord + deltaUV * dither;

    float  illumination = 0.0;
    float  sampleDecay  = 1.0;

    // Per-sample weight: tuned so a fully-lit path through N=64, decay=0.97
    // produces ~0.8 raw illumination before Exposure/Intensity scaling.
    float weight = 2.0 / (float)numSamples;

    [loop] for(int i = 0; i < numSamples; i++)
    {
        sampleUV += deltaUV;

        if(any(sampleUV < 0.0) || any(sampleUV > 1.0))
        {
            sampleDecay *= IN.Decay;
            continue;
        }

        float sDepth = LinZ(TextureDepth.SampleLevel(Point_Sampler, sampleUV, 0).x);

        // Sky detection: far depth = sky = light passes through
        float skyMask = smoothstep(IN.Threshold - 0.05, IN.Threshold, sDepth);

        // Bright pixel detection: sun disc and bright sky emit rays
        float3 sCol = TextureOriginal.SampleLevel(Linear_Sampler, sampleUV, 0).rgb;
        float brightness = dot(sCol, K_LUM);
        float brightMask = saturate((brightness - 1.5) * 0.3);

        float lightContrib = max(skyMask, brightMask);

        illumination += lightContrib * sampleDecay * weight;
        sampleDecay  *= IN.Decay;
    }

    illumination *= IN.Exposure * IN.Intensity;

    // Radial falloff: 1/(1+d²) for natural inverse-square-like attenuation.
    // Pixels near the sun get strongest shafts; distant pixels still receive
    // attenuated shafts (real volumetric scattering fades slowly).
    float radialFade = 1.0 / (1.0 + rayLen * rayLen * 1.5);
    illumination *= radialFade;

    return saturate(illumination);
}


//=============================================================================//
//                                                                             //
//  TECH 8: EFFECTS COMPOSITE                                                 //
//                                                                             //
//  Applies all computed effects in a single pass:                             //
//    1. HBAO ambient occlusion (from RenderTargetRGBA64)                      //
//    2. SSDO indirect illumination (from RenderTargetRGBA64F)                 //
//    3. Contact shadows (from RenderTargetR16F)                               //
//    4. Screen-space reflections (from RenderTargetRGBA32)                    //
//    5. Volumetric god rays (from RenderTargetR32F)                           //
//    6. Atmospheric fog (sky-sampled, height-attenuated, inscatter)         //
//    7. Skin micro-detail (high-pass pore injection, skin-masked)             //
//    8. Clarity / local contrast enhancement                                  //
//                                                                             //
//  Ordering rationale:                                                        //
//    - AO and Contact Shadows are multiplicative (darken occluded areas)      //
//    - GI is additive (indirect light adds energy)                            //
//    - SSR is additive (reflected light adds energy)                           //
//    - God Rays are additive (scattered light adds energy)                     //
//    - Atmospheric Fog blends toward sky-sampled fog color                     //
//    - Skin detail is additive-on-luma (sharpens high-frequency detail)       //
//    - Clarity is a local contrast operation (midtone-biased)                 //
//                                                                             //
//=============================================================================//

struct CompositeVSOutput
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
NI float  CLR_Amount   : E0;
NI float  CLR_MidPoint : E1;
NI float  CLR_Radius   : E2;
NI float  CLR_DepthAw  : E3;
NI float  CLR_Enable   : E4;
NI float  CLR_PSkin    : E5;
NI float  SMD_Intensity: E6;
NI float  SMD_BlurRad  : E7;
NI float  SMD_HPGain   : E8;
NI float  SMD_MaskPow  : E9;
NI float  SMD_Enable   : E10;
NI float  SMD_LumaOnly : E11;
};

CompositeVSOutput VS_EffectsComposite(VertexShaderInput IN)
{
    CompositeVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    OUT.CLR_Amount   = UICLR_Amount;
    OUT.CLR_MidPoint = UICLR_MidPoint;
    OUT.CLR_Radius   = UICLR_Radius;
    OUT.CLR_DepthAw  = UICLR_DepthAware;
    OUT.CLR_Enable   = UICLR_Enable;
    OUT.CLR_PSkin    = UICLR_PreserveSkin;

    OUT.SMD_Intensity = UISMD_Intensity;
    OUT.SMD_BlurRad   = UISMD_BlurRadius;
    OUT.SMD_HPGain    = UISMD_HighPassGain;
    OUT.SMD_MaskPow   = UISMD_SkinMaskPow;
    OUT.SMD_Enable    = UISMD_Enable;
    OUT.SMD_LumaOnly  = UISMD_LumaOnly;

    return OUT;
}

float4 PS_EffectsComposite(CompositeVSOutput IN) : SV_Target
{
    float3 color = TextureColor.Sample(Point_Sampler, IN.texcoord).rgb;
    float  rawDepth = TextureDepth.SampleLevel(Point_Sampler, IN.texcoord, 0).x;
    float  linDepth = FastLinDepth(rawDepth, PREPASS_FARPLANE);

    // ── Bilateral blur of AO (12-tap: cardinal + diagonal + extended) ────
    //
    // Raw SSAO is always noisy — every production engine blurs it before
    // compositing.  A bilateral blur smooths the noise while preserving edges
    // (depth discontinuities).  We read depth from the .g channel packed by
    // the AO pass to save extra texture fetches.

    float4 aoCenterData = RenderTargetRGBA64.Sample(Point_Sampler, IN.texcoord);
    float  aoCenter     = aoCenterData.r;
    float  aoCenterZ    = aoCenterData.g; // packed linearized depth

    float ao        = aoCenter;
    float aoWTotal  = 1.0;

    static const float AO_BLUR_SHARPNESS = 40.0; // Higher = sharper edges

    // 12-tap: 4 cardinal (1px) + 4 diagonal (1.4px) + 4 cardinal (2px)
    // The diagonals fill the gaps the cross pattern misses — critical for
    // eliminating the "plus-shaped" blur artifacts visible on flat surfaces.
    static const float2 AO_OFFSETS[12] = {
        float2( 1,  0), float2(-1,  0), float2( 0,  1), float2( 0, -1),   // Cardinal 1px
        float2( 1,  1), float2(-1,  1), float2(-1, -1), float2( 1, -1),   // Diagonal 1.4px
        float2( 2,  0), float2(-2,  0), float2( 0,  2), float2( 0, -2)    // Cardinal 2px
    };
    static const float AO_WEIGHTS[12] = {
        0.50, 0.50, 0.50, 0.50,    // 1px cardinal
        0.35, 0.35, 0.35, 0.35,    // 1.4px diagonal
        0.18, 0.18, 0.18, 0.18     // 2px cardinal
    };

    [unroll] for(int bi = 0; bi < 12; bi++)
    {
        float2 bUV = IN.texcoord + AO_OFFSETS[bi] * PixelSize;
        float4 bData = RenderTargetRGBA64.SampleLevel(Point_Sampler, bUV, 0);
        float  bAO   = bData.r;
        float  bZ    = bData.g;

        // Bilateral weight: reject samples at different depths
        float dz = abs(bZ - aoCenterZ);
        float bw = AO_WEIGHTS[bi] * exp(-dz * dz * AO_BLUR_SHARPNESS);

        ao       += bAO * bw;
        aoWTotal += bw;
    }

    ao /= aoWTotal;

    // ── Bilateral blur of GI (same 9-tap pattern) ──

    float4 giCenterData = RenderTargetRGBA64F.Sample(Point_Sampler, IN.texcoord);
    float3 giCenter     = giCenterData.rgb;
    float  giCenterZ    = giCenterData.a; // packed linearized depth

    float3 gi      = giCenter;
    float  giWTotal = 1.0;

    static const float GI_BLUR_SHARPNESS = 30.0;

    [unroll] for(int gi_i = 0; gi_i < 12; gi_i++)
    {
        float2 gUV = IN.texcoord + AO_OFFSETS[gi_i] * PixelSize;
        float4 gData = RenderTargetRGBA64F.SampleLevel(Point_Sampler, gUV, 0);
        float3 gCol  = gData.rgb;
        float  gZ    = gData.a;

        float gdz = abs(gZ - giCenterZ);
        float gw  = AO_WEIGHTS[gi_i] * exp(-gdz * gdz * GI_BLUR_SHARPNESS);

        gi      += gCol * gw;
        giWTotal += gw;
    }

    gi /= giWTotal;

    float3 indirectGI = gi;
    float  contactSh  = RenderTargetR16F.Sample(Linear_Sampler, IN.texcoord).x;


    //--- 1. GTAO Ambient Occlusion ---//
    [branch] if(UIGTAO_Enable)
    {
        // Multi-bounce approximation: instead of multiplying by a single AO
        // scalar (which makes shadowed areas uniformly dark), we tint the AO
        // by an albedo-dependent curve that preserves color in occluded regions.
        // This approximates the energy from multiple diffuse bounces.
        if(UIGTAO_MultiBounce)
        {
            float3 albedo = saturate(color / max(dot(color, K_LUM) * 2.0, DELTA));
            float3 aoMulti = MultiBounceAO(ao, albedo);
            color *= aoMulti;
        }
        else
        {
            // Simple AO with tint: lerp between tinted shadow and full color
            float3 aoTinted = lerp(UIGTAO_Tint * color, color, ao);
            color = aoTinted;
        }
    }


    //--- 2. SSGI Indirect Illumination ---//
    [branch] if(UISSGI_Enable && giCenterData.a > DELTA)
    {
        color += indirectGI;
    }


    //--- 3. Contact Shadows ---//
    [branch] if(UICS_Enable)
    {
        color *= contactSh;
    }


    //--- 4. Screen-Space Reflections ---//
    [branch] if(UISSR_Enable)
    {
        //Bilateral blur of SSR (4-tap cross) — raw SSR is noisy because
        //the discrete ray march produces aliased hit points.  A small
        //blur at composite time smooths the reflection without losing
        //edge detail, similar to how we blur AO.
        float4 ssrCenter = RenderTargetRGBA32.Sample(Linear_Sampler, IN.texcoord);
        float3 ssrBlur   = ssrCenter.rgb * ssrCenter.a;
        float  ssrW      = ssrCenter.a + DELTA;

        static const float2 SSR_OFFS[4] = {
            float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1)
        };

        [unroll] for(int sr = 0; sr < 4; sr++)
        {
            float2 sUV = IN.texcoord + SSR_OFFS[sr] * PixelSize * 1.5;
            float4 sTap = RenderTargetRGBA32.SampleLevel(Linear_Sampler, sUV, 0);
            //Depth bilateral: reject taps at different depths
            float sZ = LinZ(TextureDepth.SampleLevel(Point_Sampler, sUV, 0).x);
            float dz = abs(sZ - linDepth);
            float bw = exp(-dz * dz * 40.0) * 0.5;

            ssrBlur += sTap.rgb * sTap.a * bw;
            ssrW    += sTap.a * bw;
        }

        float3 reflColor = ssrBlur / ssrW;
        float  reflConf  = ssrW / (ssrW + 0.5); //Smooth confidence normalization

        // Additive: reflected light adds energy
        color += reflColor * reflConf;
    }


    //--- 5. Volumetric God Rays ---//
    [branch] if(UIGR_Enable)
    {
        float shaftIntensity = RenderTargetR32F.Sample(Linear_Sampler, IN.texcoord).x;

        // Tinted additive: sun-colored light scattered through atmosphere
        color += UIGR_Tint * shaftIntensity;

        // SkyrimBridge: lightning flash adds burst of volumetric scatter
#ifdef SKYRIMBRIDGE_FXH
        [branch] if (SB_IsActive() && SB_Lightning.z > 0.01)
        {
            float flashBoost = SB_Lightning.z * 0.15;
            color += float3(0.7, 0.75, 0.9) * flashBoost * saturate(1.0 - linDepth * 2.0);
        }
#endif
    }


    //--- 6. Atmospheric Fog ---//
    //
    //  Merged from Effect_AtmosphericFog.fxh v3.1 (enblens) + prepass v1.
    //
    //  From enblens (superior):
    //    - Sky color sampled from actual scene via depth-validated Poisson disc
    //    - Reinhard soft-clamp prevents sun blowout in sky samples
    //    - Aerial perspective: distant objects desaturate before fog obscures
    //    - Max opacity clamp: distant detail always partially visible
    //    - Sky threshold: explicit depth cutoff to never fog the sky
    //    - Tint + brightness controls for per-weather artistic shaping
    //
    //  From prepass (superior):
    //    - True world-space height reconstruction via viewRay.y × depth / viewRay.z
    //      (enblens used screen UV.y — wrong when camera pitches up/down)
    //    - Analytical height-attenuated fog integral:
    //      ∫₀ᵈ ρ·exp(-λ·h(s)) ds has closed form for linear rays
    //      Gives physically correct fog that's thick in valleys, thin at altitude
    //    - Sun inscattering: forward-scatter brightens fog toward sun position
    //    - Euclidean distance correction (depth buffer stores Z, not ray length)

    [branch] if(UIFOG_Enable)
    {
        float worldZ = WorldZ(rawDepth);

        // Skip sky pixels — fog should never be applied to the sky.
        // Distant geometry should get maximum fog, not be skipped.
        if(linDepth < UIFOG_SkyThreshold)
        {
            // ── View ray reconstruction ─────────────────────────────────
            float2 ndc = IN.texcoord * 2.0 - 1.0;
            float tanHFov = tan(FieldOfView * 0.5);
            float aspect  = ScreenSize.x / ScreenSize.y;

            float3 viewRay = normalize(float3(ndc.x * aspect * tanHFov,
                                              -ndc.y * tanHFov,
                                               1.0));

            // Correct height: depth buffer stores view-space Z, not Euclidean distance.
            // viewPos = viewRay × (worldZ / viewRay.z), so height = viewRay.y × worldZ / viewRay.z
            float invViewZ      = 1.0 / max(viewRay.z, 0.3);
            float relHeight     = viewRay.y * worldZ * invViewZ;
            float euclideanDist = worldZ * invViewZ;

            // ── Height-attenuated fog integral ──────────────────────────
            //
            // fogDensity(h) = ρ × exp(-λ × (h - h₀))
            //
            // Analytical integral along a ray with vertical component vy:
            //   ∫₀ᵈ ρ·exp(-λ·h(s)) ds = ρ·exp(-λ·h₀) × (1 - exp(-λ·vy·d)) / (λ·vy)
            //
            // Limit as vy → 0 (horizontal ray):
            //   = ρ·exp(-λ·h₀) × d

            float h       = relHeight - UIFOG_BaseHeight;
            float falloff = UIFOG_HeightFalloff;
            float vy      = viewRay.y;
            float fogDist = min(euclideanDist, UIFOG_MaxDist);
            float fogIntegral;

            if(abs(vy) > 0.001)
            {
                float heightTerm = falloff * vy;
                fogIntegral = UIFOG_Density * exp(-falloff * h) *
                    (1.0 - exp(-heightTerm * fogDist)) / (heightTerm + DELTA);
            }
            else
            {
                fogIntegral = UIFOG_Density * exp(-falloff * h) * fogDist;
            }

            float fogAmount = 1.0 - exp(-max(fogIntegral, 0.0));
            fogAmount = saturate(fogAmount);

            // Max opacity clamp: real fog always transmits some light,
            // and preserving ~15% scene detail reads as more natural than
            // total whiteout.  (From enblens v3.1)
            fogAmount = min(fogAmount, UIFOG_MaxOpacity);

            // ── Aerial perspective desaturation ─────────────────────────
            //
            // Rayleigh scattering desaturates distant objects before fog
            // fully obscures them.  This is the subtle "blue-ification" of
            // distant mountains.  (From enblens v3.1)

            float sceneLuma = dot(color, K_LUM);
            color = lerp(color, sceneLuma, fogAmount * UIFOG_AerialDesat);

            // ── Fog color: sampled from actual sky ──────────────────────
            //
            // Instead of static artist-picked near/far colors, sample the
            // real sky color from the scene.  This automatically adapts to
            // weather, time of day, and ENB weather system changes.
            // Falls back to UIFOG_ColorFallback if no sky pixels visible.
            //
            // Ported from Effect_AtmosphericFog.fxh v3.1 (8-tap Poisson
            // disc, depth-validated, Reinhard-clamped sky sampling).

            float3 fogColor;

            if(UIFOG_SkyColorEnable)
            {
                fogColor = EstimateSkyColor(
                    float2(0.5, UIFOG_SkySampleY),
                    UIFOG_SkySpread,
                    UIFOG_SkyDesat,
                    UIFOG_SkyThreshold
                );
            }
            else
            {
                fogColor = UIFOG_ColorFallback;
            }

            // Artist tint: blend toward a chosen color for stylistic control
            fogColor = lerp(fogColor, fogColor * UIFOG_Tint, UIFOG_TintWeight);
            fogColor *= UIFOG_Brightness;

            // ── SkyrimBridge: game fog color anchor ─────────────────────
            //
            // The sky-sampling approach adapts beautifully but can drift in
            // edge cases (heavily overcast sky, volcanic ash weathers).
            // Blending in the actual game fog color keeps it grounded.
#ifdef SKYRIMBRIDGE_FXH
            [branch] if (SB_IsActive() && !EInteriorFactor)
            {
                float fogLerp = saturate(fogDist / max(UIFOG_MaxDist, 1.0));
                float3 gameFog = lerp(SB_Fog_NearColor.rgb, SB_Fog_FarColor.rgb, fogLerp);
                gameFog = gameFog / (gameFog + 1.0); // Tonemap to match
                fogColor = lerp(fogColor, gameFog, 0.3);
            }
#endif

            // ── Sun inscattering ────────────────────────────────────────
            //
            // Mie forward-scattering: fog in the sun's direction glows
            // warm — the classic "looking into the light through mist" effect.
            // Phase function approximated as pow(cos(θ), 8).

            float3 sunDir = normalize(SunDirection.xyz);
            if(sunDir.z > 0.05 && UIFOG_Inscatter > 0.001)
            {
                float sunDot = saturate(dot(viewRay, sunDir));
                float inscatter = pow(sunDot, 8.0) * UIFOG_Inscatter;
                fogColor += UIFOG_InscatterTint * inscatter;
            }

            // ── Enhanced Atmospheric Scattering (Mie + Rayleigh) ────────
            //
            //  When enabled, replaces the simple inscatter with physically
            //  separated Rayleigh (wavelength-dependent, 1/λ⁴) and Mie
            //  (directional, Henyey-Greenstein phase) scattering terms.
            //
            //  Rayleigh: blue sky color, isotropic, strongest for short λ
            //  Mie: forward-scatter from aerosols, produces bright halo
            //       around sun and silver lining on clouds
            
            [branch] if(UIATM_Enable && sunDir.z > 0.01)
            {
                float sunDotATM = saturate(dot(viewRay, sunDir));
                float cosTheta  = sunDotATM;
                
                //Henyey-Greenstein phase function for Mie scattering
                float g  = UIATM_MieAnisotropy;
                float g2 = g * g;
                float HG = (1.0 - g2) / (4.0 * PI * pow(abs(1.0 + g2 - 2.0*g*cosTheta), 1.5));
                
                //Rayleigh phase: (3/16π)(1 + cos²θ)
                float RayleighPhase = (3.0 / (16.0 * PI)) * (1.0 + cosTheta * cosTheta);
                
                //Rayleigh scattering: wavelength-dependent (blue dominant)
                float3 rayleighScatter = UIATM_RayleighTint * RayleighPhase * UIATM_RayleighStr;
                
                //Mie scattering: forward-peaked, achromatic
                float3 mieScatter = HG * UIATM_MieStr;
                
                //Sun direction influence: Mie only visible looking toward sun
                float sunInfluence = lerp(1.0, sunDotATM, UIATM_SunInfluence);
                mieScatter *= sunInfluence;
                
                //Combine and modulate by fog amount (more scatter = more atmosphere)
                float3 atmScatter = (rayleighScatter + mieScatter) * fogAmount;
                fogColor += atmScatter;
            }

            // ── Composite ───────────────────────────────────────────────
            color = lerp(color, fogColor, fogAmount);
        }
    }


    //--- 7. Skin Micro-Detail (High-Pass Pore Injection) ---//
    //
    // Extracts high-frequency surface detail by subtracting a small-radius
    // Gaussian blur from the original, yielding only pore-scale features.
    // This detail is then re-injected into skin regions with controllable
    // intensity, recovering the fine texture that SSS diffusion smoothed out.

    [branch] if(IN.SMD_Enable > 0.5)
    {
        // Skin mask for detail injection
        float skinRaw  = pow(saturate(1.0 - TextureMask.SampleLevel(Point_Sampler, IN.texcoord, 0).w), 4);
        float skinFade = smoothstep(UISSS_DepthFadeFar, UISSS_DepthFadeNear, linDepth);
        float skinMask = pow(saturate(skinRaw * skinFade * 4.0), IN.SMD_MaskPow);

        [branch] if(skinMask > 0.01)
        {
            // Small-radius Gaussian blur (9-tap cross pattern)
            static const float GaussW[5] = { 0.2270, 0.1945, 0.1216, 0.0541, 0.0162 };
            float3 blurred = color * GaussW[0];

            [unroll] for(int k = 1; k < 5; k++)
            {
                float2 offsetH = float2(PixelSize.x * IN.SMD_BlurRad * (float)k, 0.0);
                float2 offsetV = float2(0.0, PixelSize.y * IN.SMD_BlurRad * (float)k);

                blurred += TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + offsetH, 0).rgb * GaussW[k];
                blurred += TextureColor.SampleLevel(Linear_Sampler, IN.texcoord - offsetH, 0).rgb * GaussW[k];
                blurred += TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + offsetV, 0).rgb * GaussW[k];
                blurred += TextureColor.SampleLevel(Linear_Sampler, IN.texcoord - offsetV, 0).rgb * GaussW[k];
            }
            // Normalize (center + 4 taps × 2 directions × 2 axes = 17 taps)
            // But cross pattern: center + 4 pairs H + 4 pairs V
            // Total weight = GaussW[0] + 4 * 2 * (GaussW[1..4]) per axis
            // For simplicity, normalize by total:
            float totalW = GaussW[0] + 4.0 * (GaussW[1] + GaussW[2] + GaussW[3] + GaussW[4]);
            blurred /= totalW;

            // High-pass = original - blurred = pore-scale detail
            float3 detail = (color - blurred) * IN.SMD_HPGain;

            if(IN.SMD_LumaOnly > 0.5)
            {
                // Apply detail only to luminance channel (avoids color fringing)
                float detailLuma = dot(detail, K_LUM);
                color += detailLuma * skinMask * IN.SMD_Intensity;
            }
            else
            {
                color += detail * skinMask * IN.SMD_Intensity;
            }
        }
    }


    //--- 8. Clarity / Local Contrast Enhancement ---//
    //
    // Inspired by Lightroom's Clarity slider. Uses an 8-tap bilateral kernel
    // (cardinal + diagonal at two radii) for a smoother local average that
    // avoids the cross-shaped artifacts of the old 4-tap version.
    // The midpoint response uses a gaussian bell curve centered on the target
    // tonal zone, producing more natural transitions than a triangular peak.

    [branch] if(IN.CLR_Enable > 0.5 && abs(IN.CLR_Amount) > 0.01)
    {
        // Optionally reduce clarity on skin to preserve softening from SSS
        float clarityMask = 1.0;
        if(IN.CLR_PSkin > 0.5)
        {
            float skinR = pow(saturate(1.0 - TextureMask.SampleLevel(Point_Sampler, IN.texcoord, 0).w), 4);
            float skinF = smoothstep(UISSS_DepthFadeFar, UISSS_DepthFadeNear, linDepth);
            float skinM = smoothstep(UISSS_MaskThreshold - UISSS_MaskSmooth,
                                     UISSS_MaskThreshold + UISSS_MaskSmooth, skinR * skinF);
            clarityMask = 1.0 - skinM * 0.7;
        }

        // 8-tap bilateral kernel: cardinal (1R, 2R) + diagonal (1R)
        // This covers a 5×5 footprint with only 8 samples — enough for a
        // smooth local average without the cross artifacts of 4-tap.
        static const float2 CLR_OFFS[8] = {
            float2( 1,  0), float2(-1,  0), float2( 0,  1), float2( 0, -1),  // Cardinal 1R
            float2( 1,  1), float2(-1, -1), float2( 1, -1), float2(-1,  1)   // Diagonal 1R
        };
        static const float CLR_W[8] = {
            0.14, 0.14, 0.14, 0.14,    // Cardinal
            0.08, 0.08, 0.08, 0.08     // Diagonal (further → less weight)
        };

        float  centerW  = 0.12;
        float3 localAvg = color * centerW;
        float  totalW   = centerW;

        [unroll] for(int j = 0; j < 8; j++)
        {
            float2 off = CLR_OFFS[j] * IN.CLR_Radius * PixelSize;
            float3 sC  = TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + off, 0).rgb;

            float w = CLR_W[j];

            if(IN.CLR_DepthAw > 0.01)
            {
                float depthSens = IN.CLR_DepthAw * 15.0;
                float dS = LinZ(TextureDepth.SampleLevel(Point_Sampler, IN.texcoord + off, 0).x);
                float rD = abs(dS - linDepth) / (linDepth + DELTA);
                w *= exp(-rD * depthSens);
            }

            localAvg += sC * w;
            totalW   += w;
        }

        localAvg /= totalW;

        // Clarity: push color away from (or toward) local average
        // Gaussian midpoint bias: smooth bell curve centered on MidPoint,
        // width ~0.35 stops. Much more natural than the old triangular peak.
        float luma = dot(color, K_LUM);
        float midDist = (luma - IN.CLR_MidPoint);
        float midBias = exp(-midDist * midDist * 8.0); // σ ≈ 0.35 stops

        float3 clarityDelta = (color - localAvg) * IN.CLR_Amount * midBias * clarityMask;
        color += clarityDelta;
    }

    color = max(color, 0.0);

    return float4(color, 1.0);
}


//=============================================================================//
//                                                                             //
//  TECH 7: PAINTERLY FILTER (Anisotropic Kuwahara)                            //
//                                                                             //
//  Based on:                                                                  //
//    Kyprianidis, Kang, Döllner, "Image and Video Abstraction by             //
//    Anisotropic Kuwahara Filtering", Pacific Graphics / CGF, 2009            //
//                                                                             //
//  The classic Kuwahara filter divides a neighborhood into overlapping        //
//  sectors, computes mean and variance in each, then outputs the mean of      //
//  the sector with lowest variance. This produces flat color regions with     //
//  sharp edges — a painterly look.                                            //
//                                                                             //
//  The anisotropic extension:                                                 //
//    1. Computes the structure tensor (gradient covariance matrix) to find    //
//       the local dominant edge orientation and anisotropy                    //
//    2. Elongates the sampling kernel along the edge direction, so that       //
//       "brush strokes" follow surface contours naturally                     //
//    3. Uses a polynomial weighting function within each sector for smooth    //
//       transitions between sectors (avoids block artifacts)                  //
//                                                                             //
//  The result looks like an oil painting where brushstrokes follow the        //
//  contours of objects — hair flows in strands, skin smooths along            //
//  curvature, fabric follows folds.                                           //
//                                                                             //
//=============================================================================//

struct PaintVSOutput
{
    float4 pos       : SV_POSITION;
    float2 texcoord  : TEXCOORD0;
NI float  Intensity  : P0;
NI float  Radius     : P1;
NI float  Sharpness  : P2;
NI float  Anisotropy : P3;
NI float  Hardness   : P4;
NI float  DepthFade  : P5;
NI float  PSkin      : P6;
NI float  SkinReduce : P7;
NI float  Active     : P8;
NI float  EdgeInk    : P9;
NI float  EdgeWidth  : P10;
NI float  Posterize  : P11;
NI float  PaperGrain : P12;
NI float  WetEdge    : P13;
NI float  Bleed      : P14;
};

PaintVSOutput VS_Painterly(VertexShaderInput IN)
{
    PaintVSOutput OUT;
    OUT.pos       = float4(IN.pos.xyz, 1.0);
    OUT.texcoord  = IN.txcoord.xy;

    OUT.Intensity  = UIPAINT_Intensity;
    OUT.Radius     = UIPAINT_Radius;
    OUT.Sharpness  = UIPAINT_Sharpness;
    OUT.Anisotropy = UIPAINT_Anisotropy;
    OUT.Hardness   = UIPAINT_Hardness;
    OUT.DepthFade  = UIPAINT_DepthFade;
    OUT.PSkin      = UIPAINT_PreserveSkin;
    OUT.SkinReduce = UIPAINT_SkinReduce;
    OUT.Active     = UIPAINT_Enable;
    OUT.EdgeInk    = UIPAINT_EdgeInk;
    OUT.EdgeWidth  = UIPAINT_EdgeWidth;
    OUT.Posterize  = UIPAINT_Posterize;
    OUT.PaperGrain = UIPAINT_PaperGrain;
    OUT.WetEdge    = UIPAINT_WetEdge;
    OUT.Bleed      = UIPAINT_Bleed;

    return OUT;
}


//  Procedural paper grain noise (screen-space, tileable)
//  Hash-based for zero texture dependency.
float PaperNoise(float2 uv)
{
    float2 p = uv * ScreenRes * 0.5; // Half-res tile for visible grain
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    float h = frac((p3.x + p3.y) * p3.z);
    // Perlin-ish: blend 2 octaves for organic feel
    float2 p2 = uv * ScreenRes * 0.15;
    float3 q3 = frac(float3(p2.xyx) * float3(0.1031, 0.1030, 0.0973));
    q3 += dot(q3, q3.yzx + 33.33);
    float h2 = frac((q3.x + q3.y) * q3.z);
    return lerp(h, h2, 0.4); // Blend octaves
}


float4 PS_Painterly(PaintVSOutput IN) : SV_Target
{
    float3 origColor = TextureColor.Sample(Point_Sampler, IN.texcoord).rgb;

    if(IN.Active < 0.5 || IN.Intensity < 0.001)
        return float4(origColor, 1.0);

    float rawDepth = TextureDepth.SampleLevel(Point_Sampler, IN.texcoord, 0).x;
    float linDepth = LinZ(rawDepth);
    float worldZ   = linDepth * PREPASS_FARPLANE;

    float depthFade = 1.0 - smoothstep(IN.DepthFade * 0.5, IN.DepthFade, worldZ);
    if(depthFade < 0.001 || linDepth > 0.95)
        return float4(origColor, 1.0);


    //=========================================================================//
    //  STAGE 1: Structure Tensor (edge direction + anisotropy)                //
    //                                                                         //
    //  Simplified 4-tap Sobel — dramatically fewer texture reads than the     //
    //  multi-radius version (~4 reads vs ~72). Eliminates GPU timeout that    //
    //  caused the "lower half only" rendering bug on many GPUs.               //
    //  Still provides accurate edge orientation for anisotropic Kuwahara.     //
    //=========================================================================//

    float lumaC  = dot(origColor, K_LUM);

    // 4-tap cross: cardinal neighbors only (saves 4 corner taps)
    float lumaL  = dot(TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(-PixelSize.x * IN.EdgeWidth, 0), 0).rgb, K_LUM);
    float lumaR  = dot(TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2( PixelSize.x * IN.EdgeWidth, 0), 0).rgb, K_LUM);
    float lumaU  = dot(TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(0, -PixelSize.y * IN.EdgeWidth), 0).rgb, K_LUM);
    float lumaD  = dot(TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(0,  PixelSize.y * IN.EdgeWidth), 0).rgb, K_LUM);

    float gx = lumaR - lumaL;
    float gy = lumaD - lumaU;

    // Edge magnitude for later stages (ink lines, wet edge)
    float edgeMag = sqrt(gx * gx + gy * gy);

    // Structure tensor from gradient outer product
    float stE = gx * gx;
    float stF = gx * gy;
    float stG = gy * gy;

    // Eigendecomposition
    float disc    = sqrt(max((stE - stG) * (stE - stG) + 4.0 * stF * stF, DELTA));
    float lambda1 = 0.5 * (stE + stG + disc);
    float lambda2 = 0.5 * (stE + stG - disc);

    float2 edgeDir = normalize(float2(lambda1 - stG, stF) + DELTA);
    float  aniso   = saturate((lambda1 - lambda2) / (lambda1 + lambda2 + DELTA));
    aniso *= IN.Anisotropy;

    float majorR = IN.Radius;
    float minorR = lerp(IN.Radius, IN.Radius * 0.25, aniso);


    //=========================================================================//
    //  STAGE 2: Anisotropic Kuwahara — flat color washes                      //
    //                                                                         //
    //  Golden-spiral sampling with elliptical kernel. Sectors compete by       //
    //  inverse variance: the most uniform-colored region dominates. This       //
    //  creates the flat wash areas characteristic of watercolor.               //
    //=========================================================================//

    int numSectors = clamp((int)UIPAINT_Sectors, 4, 8);
    float sectorWidth = 2.0 * PI_PP / (float)numSectors;

    float3 secMean[8];
    float  secSqL[8];
    float  secW[8];

    [unroll] for(int s = 0; s < 8; s++)
    {
        secMean[s] = 0.0;
        secSqL[s]  = 0.0;
        secW[s]    = 0.0;
    }

    static const int   NUM_SAMPLES    = 32;
    static const float GOLDEN_ANGLE_P = 2.39996323;

    [loop] for(int i = 0; i < NUM_SAMPLES; i++)
    {
        float fi    = (float)i + 0.5;
        float r     = sqrt(fi / (float)NUM_SAMPLES);
        float theta = fi * GOLDEN_ANGLE_P;

        float2 localOffset = float2(r * cos(theta), r * sin(theta));

        // Elliptical distortion along flow direction
        float2 offset;
        offset.x = edgeDir.x * localOffset.x * majorR - edgeDir.y * localOffset.y * minorR;
        offset.y = edgeDir.y * localOffset.x * majorR + edgeDir.x * localOffset.y * minorR;

        float2 sampleUV = IN.texcoord + offset * PixelSize;
        if(any(sampleUV < 0.0) || any(sampleUV > 1.0)) continue;

        float3 sampleCol = TextureColor.SampleLevel(Linear_Sampler, sampleUV, 0).rgb;
        float  spatialW  = pow(saturate(1.0 - r * r), IN.Hardness);

        float angle = atan2(offset.y, offset.x);
        if(angle < 0.0) angle += 2.0 * PI_PP;

        float sLuma = dot(sampleCol, K_LUM);

        [unroll] for(int sec = 0; sec < 8; sec++)
        {
            if(sec >= numSectors) break;

            float sectorCenter = ((float)sec + 0.5) * sectorWidth;
            float angleDiff = angle - sectorCenter;
            if(angleDiff >  PI_PP) angleDiff -= 2.0 * PI_PP;
            if(angleDiff < -PI_PP) angleDiff += 2.0 * PI_PP;

            float membership = saturate(cos(angleDiff * PI_PP / (sectorWidth * 1.5)));
            float w = spatialW * membership;

            if(w > 0.001)
            {
                secMean[sec] += sampleCol * w;
                secSqL[sec]  += sLuma * sLuma * w;
                secW[sec]    += w;
            }
        }
    }

    // Variance-weighted blend
    float3 washColor = 0.0;
    float  washWSum  = 0.0;

    [unroll] for(int s2 = 0; s2 < 8; s2++)
    {
        if(s2 >= numSectors) break;
        if(secW[s2] < 0.01) continue;

        float invW    = 1.0 / secW[s2];
        float3 mean   = secMean[s2] * invW;
        float meanL   = dot(mean, K_LUM);
        float eSqL    = secSqL[s2] * invW;
        float variance = max(eSqL - meanL * meanL, DELTA);

        float sWeight = 1.0 / pow(variance, IN.Sharpness * 0.5);

        washColor += mean * sWeight;
        washWSum  += sWeight;
    }

    washColor = (washWSum > DELTA) ? (washColor / washWSum) : origColor;


    //=========================================================================//
    //  STAGE 3: Pigment bleed — directional color diffusion along edges       //
    //                                                                         //
    //  Watercolor pigment bleeds along the paper grain, following edges.       //
    //  We do an anisotropic 1D blur along the edge direction, weighted by     //
    //  the Kuwahara wash result. This softens transitions along edges while   //
    //  keeping them crisp across edges.                                       //
    //=========================================================================//

    if(IN.Bleed > 0.01)
    {
        float3 bleedColor = washColor;
        float  bleedW     = 1.0;

        [unroll] for(int b = 1; b <= 4; b++)
        {
            float fb = (float)b;
            float bw = exp(-fb * fb * 0.25) * IN.Bleed;

            float2 bleedOffset = edgeDir * PixelSize * fb * 2.0;

            float3 sP = TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + bleedOffset, 0).rgb;
            float3 sN = TextureColor.SampleLevel(Linear_Sampler, IN.texcoord - bleedOffset, 0).rgb;

            bleedColor += (sP + sN) * bw;
            bleedW     += 2.0 * bw;
        }

        washColor = lerp(washColor, bleedColor / bleedW, IN.Bleed);
    }


    //=========================================================================//
    //  STAGE 4: Soft posterization — limited watercolor palette               //
    //                                                                         //
    //  Watercolor naturally produces discrete color steps because pigments     //
    //  either saturate the paper or don't. We do a soft quantization that     //
    //  rounds colors toward discrete levels, then blend back partially to     //
    //  avoid harsh banding. This gives the "limited palette" feel.            //
    //=========================================================================//

    if(IN.Posterize < 47.0)
    {
        float  levels = IN.Posterize;
        float3 quantized = floor(washColor * levels + 0.5) / levels;
        // Soft blend: don't fully snap, keep some gradient life
        float  softness = 0.35;
        washColor = lerp(washColor, quantized, 1.0 - softness);
    }


    //=========================================================================//
    //  STAGE 5: Edge ink lines — pigment pooling at boundaries                //
    //                                                                         //
    //  Watercolor pigment naturally pools at the edges of wet regions,        //
    //  creating darker outlines. We detect edges via Sobel magnitude and      //
    //  darken those pixels. The ink color is derived from the local color     //
    //  (darker, more saturated version) rather than pure black.               //
    //=========================================================================//

    float edgeStrength = 0.0;

    if(IN.EdgeInk > 0.01)
    {
        // Normalize edge magnitude to [0,1] range
        // Empirical divisor: typical Sobel values in a game scene
        edgeStrength = saturate(edgeMag * 3.0);

        // Sharpen the edge response: thin crisp lines, not gradients
        edgeStrength = smoothstep(0.05, 0.35, edgeStrength);

        // Ink color: darken and saturate the local wash
        float  inkLuma = dot(washColor, K_LUM);
        float3 inkColor = washColor * 0.15; // Very dark version of local color

        // Blend: edges get ink, flat regions untouched
        washColor = lerp(washColor, inkColor, edgeStrength * IN.EdgeInk);
    }


    //=========================================================================//
    //  STAGE 6: Wet-edge saturation — pigment concentrates at transitions     //
    //                                                                         //
    //  Where color regions meet, watercolor pigment pools and becomes more    //
    //  saturated. We boost saturation proportional to edge strength, giving   //
    //  that characteristic vivid border between washes.                       //
    //=========================================================================//

    if(IN.WetEdge > 0.01)
    {
        float wetMask = edgeStrength * IN.WetEdge;

        float  washLuma = dot(washColor, K_LUM);
        float3 chromaDir = washColor - washLuma;

        // Boost chroma at edges: pigment concentrates
        washColor += chromaDir * wetMask * 1.5;

        // Slight darkening at the very edge (pigment pools are denser)
        washColor *= lerp(1.0, 0.85, wetMask * 0.5);
    }


    //=========================================================================//
    //  STAGE 7: Paper grain — simulates rough watercolor paper texture        //
    //                                                                         //
    //  Watercolor paper has a tooth (texture) that grabs pigment unevenly.   //
    //  Dark areas (more pigment) show more grain; light areas (bare paper)    //
    //  show less. The grain is multiplicative, affecting saturation/value.    //
    //=========================================================================//

    if(IN.PaperGrain > 0.001)
    {
        float grain = PaperNoise(IN.texcoord);

        // Remap from [0,1] to [-1,1] centered on 0
        grain = grain * 2.0 - 1.0;

        // Paper grain is more visible in darker (more pigmented) areas
        float washLuma2 = dot(washColor, K_LUM);
        float grainMask = saturate(1.0 - washLuma2 * 0.7); // Less grain in highlights

        // Apply as multiplicative modulation
        washColor *= 1.0 + grain * IN.PaperGrain * grainMask;
    }


    //=========================================================================//
    //  STAGE 8: Masking and final blend                                       //
    //=========================================================================//

    float blendFactor = IN.Intensity * depthFade;

    if(IN.PSkin > 0.5)
    {
        float skinRaw  = pow(saturate(1.0 - TextureMask.SampleLevel(Point_Sampler, IN.texcoord, 0).w), 4);
        float skinFade = smoothstep(UISSS_DepthFadeFar, UISSS_DepthFadeNear, linDepth);
        float skinMask = smoothstep(UISSS_MaskThreshold - UISSS_MaskSmooth,
                                    UISSS_MaskThreshold + UISSS_MaskSmooth, skinRaw * skinFade);
        blendFactor *= lerp(1.0, 1.0 - IN.SkinReduce, skinMask);
    }

    float3 finalColor = lerp(origColor, washColor, blendFactor);

    return float4(max(finalColor, 0.0), 1.0);
}


//=============================================================================//
//  REALISM ENHANCEMENT PASS                                                   //
//                                                                             //
//  Post-process pass for unique prepass-only realism enhancements:            //
//    - Chromatic adaptation (von Kries white balance)                         //
//    - Micro-shadow detail enhancement                                        //
//                                                                             //
//  NOTE: Film halation, gate weave, and letterbox have been consolidated      //
//  into enblens.fx (Effect_CinematicFX.fxh) where superior implementations   //
//  with more features already exist.  See deduplication note above.           //
//=============================================================================//

float4 PS_RealismEnhance(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    float2 UV = txcoord.xy;
    float3 color = TextureColor.Sample(Linear_Sampler, UV).rgb;
    
    
    //=== 1. Chromatic Adaptation: von Kries white balance ===//
    //  Estimates scene illuminant from average scene color and
    //  adjusts channels to normalize toward target white point.
    [branch] if(UICA_Enable)
    {
        //Approximate scene illuminant from downsampled average
        //Using 4-corner + center sampling as low-cost estimator
        float3 sceneAvg = 0.0;
        static const float2 caPoints[5] = {
            float2(0.25, 0.25), float2(0.75, 0.25),
            float2(0.5,  0.5),
            float2(0.25, 0.75), float2(0.75, 0.75)
        };
        
        [unroll] for(int ca = 0; ca < 5; ca++)
            sceneAvg += TextureColor.SampleLevel(Linear_Sampler, caPoints[ca], 4).rgb;
        sceneAvg *= 0.2;
        
        //Von Kries diagonal transform: scale each channel to map
        //estimated illuminant -> target white point
        float3 adaptation = UICA_TargetWhite / max(sceneAvg, 0.01);
        adaptation = lerp(1.0, adaptation, UICA_Strength);
        
        //Clamp to prevent extreme corrections
        adaptation = clamp(adaptation, 0.5, 2.0);
        color *= adaptation;
    }
    
    
    //=== 2. Micro-Shadow Detail Enhancement ===//
    //  High-pass on the scene to extract fine shadow detail that
    //  AO and contact shadows miss at their sampling resolution.
    [branch] if(UIMS_Enable)
    {
        float  rawDepth = TextureDepth.SampleLevel(Point_Sampler, UV, 0).x;
        float  linDepth = FastLinDepth(rawDepth, 2999.0);
        float  depthFade = saturate(1.0 - linDepth * (2999.0 / UIMS_DepthFade));
        
        //4-tap cross high-pass filter
        float3 microBlur = 0.0;
        static const float2 msOff[4] = {
            float2(1.0, 0.0), float2(-1.0, 0.0),
            float2(0.0, 1.0), float2(0.0, -1.0)
        };
        
        [unroll] for(int ms = 0; ms < 4; ms++)
        {
            float2 msUV = UV + msOff[ms] * PixelSize * UIMS_Radius;
            microBlur += TextureColor.SampleLevel(Linear_Sampler, msUV, 0).rgb;
        }
        microBlur *= 0.25;
        
        //High-pass: original minus blurred = fine detail
        float3 microDetail = color - microBlur;
        float  microLuma   = dot(microDetail, K_LUM);
        
        //Only darken (shadow-side detail, not highlights)
        float shadowDetail = min(microLuma, 0.0) * UIMS_Intensity * depthFade;
        color += shadowDetail;
        color = max(color, 0.0);
    }
    
    
    return float4(max(color, 0.0), TextureColor.Sample(Point_Sampler, UV).a);
}

//                              TECHNIQUES                                     //
//=============================================================================//

// --- Original SSS Pipeline (Tech 0-2) ---

TWOPASSTECH11 (KitsuunePrePass <string UIName="Pre Processing - Kitsuune"; string RenderTarget="RenderTargetRGBA64";>,
                                 VS_Basic(),    PS_Blank(),
                                 VS_SSSBlurH(), PS_SSSBlurH())

TECH11        (KitsuunePrePass1, VS_SSSBlurV(), PS_SSSBlurV())

TECH11        (KitsuunePrePass2, VS_Final(),    PS_Final())

// --- New Effects Pipeline (Tech 3-9) ---

TWOPASSTECH11 (KitsuunePrePass3 <string UIName="Pre Processing - HBAO"; string RenderTarget="RenderTargetRGBA64";>,
                                  VS_Basic(),  PS_Blank(),
                                  VS_GTAO(),   PS_GTAO())

TWOPASSTECH11 (KitsuunePrePass4 <string UIName="Pre Processing - SSDO"; string RenderTarget="RenderTargetRGBA64F";>,
                                  VS_Basic(),  PS_Blank(),
                                  VS_SSGI(),   PS_SSGI())

TWOPASSTECH11 (KitsuunePrePass5 <string UIName="Pre Processing - Contact Shadows"; string RenderTarget="RenderTargetR16F";>,
                                  VS_Basic(),            PS_Blank(),
                                  VS_ContactShadow(),    PS_ContactShadow())

TWOPASSTECH11 (KitsuunePrePass6 <string UIName="Pre Processing - SSR"; string RenderTarget="RenderTargetRGBA32";>,
                                  VS_Basic(),  PS_Blank(),
                                  VS_SSR(),    PS_SSR())

TWOPASSTECH11 (KitsuunePrePass7 <string UIName="Pre Processing - God Rays"; string RenderTarget="RenderTargetR32F";>,
                                  VS_Basic(),     PS_Blank(),
                                  VS_GodRays(),   PS_GodRays())

TECH11        (KitsuunePrePass8, VS_EffectsComposite(), PS_EffectsComposite())

TECH11        (KitsuunePrePass9, VS_Painterly(),         PS_Painterly())

TECH11        (KitsuunePrePass10 <string UIName="Pre Processing - Realism Enhance";>,
                                  VS_Basic(),            PS_RealismEnhance())

#include "Helper/PrePassAddonTechniques.fxh"
