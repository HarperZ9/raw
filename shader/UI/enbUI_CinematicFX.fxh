//----------------------------------------------------------------------------------------------//
//                                                                                              //
//                UI Parameters for Cinematic FX Suite  v2.0                                    //
//                                                                                              //
//  SHADERGROUP 15: Lens Diffusion / Pro-Mist                                                   //
//  SHADERGROUP 16: Film Halation                                                               //
//  SHADERGROUP 17: Light Leaks                                                                 //
//  SHADERGROUP 18: Gate Weave / Film Jitter                                                    //
//  SHADERGROUP 19: Cinematic Letterbox                                                         //
//  SHADERGROUP 20: Anamorphic Lens                                                             //
//  SHADERGROUP 21: Optical Vignette                                                            //
//  SHADERGROUP 22: Film Damage                                                                 //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//========================= SHADERGROUP 15 — LENS DIFFUSION ===================================//
#if SHADERGROUP == 15

int DiffHeader   <string UIName="      >>>>>>LENS DIFFUSION v2<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

float  UIDIFF_Strength     <string UIName="|- Diffusion Strength";           float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.25};
float  UIDIFF_HighlightBias <string UIName="|- Highlight Bias";              float UIMin=0.5;  float UIMax=4.0;  float UIStep=0.01; > = {1.5};
float  UIDIFF_HighlightRetain <string UIName="|- Highlight Retention";       float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.50};
float  UIDIFF_BlackLift    <string UIName="|- Black Lift";                   float UIMin=0.0;  float UIMax=0.15; float UIStep=0.001;> = {0.02};
float  UIDIFF_Desaturate   <string UIName="|- Diffusion Desaturation";       float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.30};
float  UIDIFF_LocalRadius  <string UIName="|- Local Blur Radius (px)";       float UIMin=0.0;  float UIMax=6.0;  float UIStep=0.1;  > = {2.0};
float  UIDIFF_LocalWeight  <string UIName="|- Local vs Wide Blur Mix";       float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.35};
float3 UIDIFF_Tint         <string UIName="|- Diffusion Tint";               string UIWidget="Color";                               > = {1.0, 0.98, 0.95};


//========================= SHADERGROUP 16 — FILM HALATION ====================================//
#elif SHADERGROUP == 16

int FHaloHeader  <string UIName="      >>>>>>FILM HALATION v2<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

float  UIFHALO_Strength    <string UIName="|- Halation Strength";            float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.20};
float  UIFHALO_Threshold   <string UIName="|- Brightness Threshold";         float UIMin=0.0;  float UIMax=2.0;  float UIStep=0.01; > = {0.60};
float  UIFHALO_Knee        <string UIName="|- Threshold Knee (softness)";    float UIMin=0.01; float UIMax=1.0;  float UIStep=0.01; > = {0.25};
float  UIFHALO_SpreadMix   <string UIName="|- Wide vs Narrow Spread";        float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.65};
float  UIFHALO_WaveSpread  <string UIName="|- Wavelength Spread (R > B)";    float UIMin=0.5;  float UIMax=3.0;  float UIStep=0.01; > = {1.50};
float  UIFHALO_Desaturate  <string UIName="|- Halation Desaturation";        float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.40};
float3 UIFHALO_Tint        <string UIName="|- Halation Color (film base)";   string UIWidget="Color";                               > = {1.0, 0.75, 0.50};


//========================= SHADERGROUP 17 — LIGHT LEAKS ======================================//
#elif SHADERGROUP == 17

int LeakHeader   <string UIName="      >>>>>>LIGHT LEAKS v2<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

float  UILEAK_Intensity    <string UIName="|- Leak Intensity";               float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.15};
float  UILEAK_Speed        <string UIName="|- Animation Speed";              float UIMin=0.05; float UIMax=3.0;  float UIStep=0.05; > = {0.40};
float  UILEAK_Coverage     <string UIName="|- Coverage (how much screen)";   float UIMin=0.1;  float UIMax=1.0;  float UIStep=0.01; > = {0.45};
float  UILEAK_EdgeBias     <string UIName="|- Edge Bias";                    float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.60};
float  UILEAK_Softness     <string UIName="|- Softness";                     float UIMin=0.5;  float UIMax=5.0;  float UIStep=0.1;  > = {2.0};
float  UILEAK_SceneAdapt   <string UIName="|- Scene Brightness Adapt";       float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.30};
float3 UILEAK_Color1       <string UIName="|- Warm Color A";                 string UIWidget="Color";                               > = {1.0, 0.65, 0.20};
float3 UILEAK_Color2       <string UIName="|- Warm Color B";                 string UIWidget="Color";                               > = {0.95, 0.35, 0.55};
int    UILEAK_BlendMode    <string UIName="|- Blend (0=Screen 1=Add 2=Softlight)"; int UIMin=0; int UIMax=2;                       > = {0};


//========================= SHADERGROUP 18 — GATE WEAVE =======================================//
#elif SHADERGROUP == 18

int WeaveHeader  <string UIName="      >>>>>>GATE WEAVE v2<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

float  UIWEAVE_AmplitudeX  <string UIName="|- Horizontal Amplitude (px)";   float UIMin=0.0;  float UIMax=3.0;  float UIStep=0.01; > = {0.40};
float  UIWEAVE_AmplitudeY  <string UIName="|- Vertical Amplitude (px)";     float UIMin=0.0;  float UIMax=3.0;  float UIStep=0.01; > = {0.60};
float  UIWEAVE_Speed       <string UIName="|- Weave Speed";                 float UIMin=0.1;  float UIMax=5.0;  float UIStep=0.1;  > = {1.0};
float  UIWEAVE_Rotation    <string UIName="|- Rotational Jitter (deg)";     float UIMin=0.0;  float UIMax=0.5;  float UIStep=0.01; > = {0.05};
float  UIWEAVE_Breathe     <string UIName="|- Breathing (zoom jitter)";     float UIMin=0.0;  float UIMax=0.01; float UIStep=0.0001;> = {0.001};
float  UIWEAVE_MotionBlur  <string UIName="|- Motion Blur from Weave";      float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.30};
float  UIWEAVE_ExpJitter   <string UIName="|- Per-Frame Exposure Jitter";   float UIMin=0.0;  float UIMax=0.10; float UIStep=0.001;> = {0.02};


//========================= SHADERGROUP 19 — CINEMATIC LETTERBOX ==============================//
#elif SHADERGROUP == 19

int LboxHeader   <string UIName="      >>>>>>LETTERBOX v2<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

int    UILBOX_Ratio        <string UIName="|- Ratio (0=2.39 1=2.00 2=1.85 3=1.66 4=4:3 5=Custom)"; int UIMin=0; int UIMax=5;     > = {0};
float  UILBOX_CustomRatio  <string UIName="|- Custom Aspect Ratio";          float UIMin=1.0;  float UIMax=3.5;  float UIStep=0.01; > = {2.39};
float  UILBOX_Opacity      <string UIName="|- Bar Opacity";                  float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {1.0};
float  UILBOX_EdgeSoftness <string UIName="|- Bar Edge Softness";            float UIMin=0.0;  float UIMax=0.05; float UIStep=0.001;> = {0.005};
float3 UILBOX_BarColor     <string UIName="|- Bar Color";                    string UIWidget="Color";                               > = {0.0, 0.0, 0.0};


//========================= SHADERGROUP 20 — ANAMORPHIC LENS ==================================//
#elif SHADERGROUP == 20

int AnamHeader   <string UIName="      >>>>>>ANAMORPHIC LENS v2<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

float  UIANAM_HBlur        <string UIName="|- Horizontal Blur (reduced MTF)";  float UIMin=0.0;  float UIMax=4.0;  float UIStep=0.01; > = {1.5};
float  UIANAM_FieldCurve   <string UIName="|- Field Curvature (edge blur+)";   float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.20};
float  UIANAM_ChromaH      <string UIName="|- Horizontal Chroma Aberration";   float UIMin=0.0;  float UIMax=3.0;  float UIStep=0.01; > = {0.50};
float  UIANAM_Mumps        <string UIName="|- Edge Stretch (mumps)";           float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.15};
float  UIANAM_Breathe      <string UIName="|- Focus Breathing Amount";         float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.15};
float  UIANAM_BreatheSpeed <string UIName="|- Breathing Speed";                float UIMin=0.1;  float UIMax=3.0;  float UIStep=0.1;  > = {0.50};
float  UIANAM_Streak       <string UIName="|- Vertical Streak (highlight)";    float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.20};


//========================= SHADERGROUP 21 — OPTICAL VIGNETTE ================================//
#elif SHADERGROUP == 21

int VigHeader    <string UIName="      >>>>>>OPTICAL VIGNETTE v2<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

float  UIVIG_Strength      <string UIName="|- Vignette Strength";              float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.50};
float  UIVIG_Softness      <string UIName="|- Plateau (center flat zone)";     float UIMin=0.0;  float UIMax=0.95; float UIStep=0.01; > = {0.55};
float  UIVIG_Power         <string UIName="|- Falloff Steepness";              float UIMin=1.0;  float UIMax=12.0; float UIStep=0.1;  > = {3.5};
float  UIVIG_Roundness     <string UIName="|- Roundness (< 1 = wide)";        float UIMin=0.3;  float UIMax=2.0;  float UIStep=0.01; > = {0.85};
float  UIVIG_CatEye        <string UIName="|- Cat-Eye Shape at Edges";         float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.30};
float  UIVIG_ChromaShift   <string UIName="|- Edge Color Shift";               float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.10};
float3 UIVIG_ChromaTint    <string UIName="|- Edge Color Tint";                string UIWidget="Color";                               > = {1.0, 0.92, 0.85};


//========================= SHADERGROUP 22 — FILM DAMAGE =====================================//
#elif SHADERGROUP == 22

int DmgHeader    <string UIName="      >>>>>>FILM DAMAGE v2<<<<<<"; int UIMin=0; int UIMax=0;> = {0};

int DmgSpacer0   <string UIName="|--------- Scratches ---------"; int UIMin=0; int UIMax=0;> = {0};
float  UIDMG_ScratchInt    <string UIName="|- Scratch Intensity";              float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.20};
float  UIDMG_ScratchWidth  <string UIName="|- Scratch Width";                  float UIMin=0.5;  float UIMax=4.0;  float UIStep=0.1;  > = {1.5};
float  UIDMG_ScratchDensity <string UIName="|- Scratch Density";               float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.30};

int DmgSpacer1   <string UIName="|--------- Dust / Dirt ---------"; int UIMin=0; int UIMax=0;> = {0};
float  UIDMG_DustInt       <string UIName="|- Dust Intensity";                 float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.15};
float  UIDMG_DustDensity   <string UIName="|- Dust Particle Count";            float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.40};

int DmgSpacer2   <string UIName="|--------- Gate Hair ---------"; int UIMin=0; int UIMax=0;> = {0};
float  UIDMG_HairInt       <string UIName="|- Gate Hair Intensity";            float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.15};

int DmgSpacer3   <string UIName="|--------- Film Aging ----------"; int UIMin=0; int UIMax=0;> = {0};
float  UIDMG_FlickerInt    <string UIName="|- Exposure Flicker";               float UIMin=0.0;  float UIMax=0.5;  float UIStep=0.01; > = {0.05};
float  UIDMG_SpliceInt     <string UIName="|- Splice Marks (reel change)";     float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.10};
float  UIDMG_ColorFade     <string UIName="|- Color Fading (vinegar)";         float UIMin=0.0;  float UIMax=1.0;  float UIStep=0.01; > = {0.0};
float3 UIDMG_VintageTint   <string UIName="|- Aging Tint";                     string UIWidget="Color";                               > = {1.0, 0.94, 0.82};


#endif
