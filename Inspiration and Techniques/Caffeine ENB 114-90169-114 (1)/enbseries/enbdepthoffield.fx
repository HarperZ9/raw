//////////////////////////////////////////////////////////////////////////
//  Test ver.1   4.2.16                                                 //
//                                                                      //
//  ENBSeries effect file                                               //
//  visit http://enbdev.com for updates                                 //
//  Copyright (c) 2007- Boris Vorontsov                                 //
//                                                                      //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//  enbeffectprepass.fx depth of field by kingeric1992,                 //
//                                  based on GDC 2004 presentation:     //
//          "Advanced Depth of Field" by Thorsten Scheuermann           //
//                                                                      //
//  with CoC calculate from lens model, adaptive quality, tilt shift,   //
//  chromatic aberration(Trans & Axial), auto dof, optical vignette,    //
//  and CoC based film grain.                                           //
//                                                                      //
//  Alternate CoCtoAlpha by gp65cj04 (modified for competibility)       //
//                                                                      //
//  Film Grain by Boris (modified for CoC info)                         //
//                                                                      //
//  Gameplay Friendly Auto DoF by kingeric1992,                         //
//              inspired by Canon EOS Automatic depth of field "A-DEP"  //
//                                                                      //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//  ENB DoF Calculator:                                                 //
//      A online graph visualizing dof range. tweak the sliders on the  //
//  left side to get a better idea how GUI parameters affacts DoF.      //
//                                                                      //
//      DSLR:   https://www.desmos.com/calculator/tlkj3c4la6            //
//      GP:     https://www.desmos.com/calculator/zjrawyrh18            //
//                                                                      //
//  for more info, visit                                                //
//          http://enbseries.enbdev.com/forum/viewtopic.php?f=7&t=3224  //
//                                                                      //
//  update: March.13.14                                                 //
//                                                                      //
//////////////////////////////////////////////////////////////////////////
//                                                                      //
//  Effects                                                             //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//  place "//" before "#define" to disable specific feature entirely,   //
//  equivalent to setting effect intensity 0, but save some performance //
//  by skipping computation.                                            //
//                                                                      //
//  example:                                                            //
//      //#define ENABLE_EXAMPLE                                        //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#define ENABLE_NOISE                //film grain
#define ENABLE_GAUSSIAN             //post gaussian blur, not recommend to disable
#define ENABLE_TILT_SHIFT           //tilt shift
#define ENABLE_AUTO_DOF             //auto dof
//#define ENABLE_CHROMA               //chromatic aberration(slow), quality
#define ENABLE_POST_CHROMA          //chromatic aberration(fast), performance
#define ENABLE_ADAPTIVE_QUALITY     //assign different quality according to CoC size, increase performance
#define ENABLE_GP                   //use gp65cj04's coc calculation, switch to DSLR if disabled.
//#define ENABLE_VIGNETTE             //optical vignette where bokeh at screen edge are partially obscured

//////////////////////////////////////////////////////////////////////////
//  Gloable Varibles                                                    //
//////////////////////////////////////////////////////////////////////////

#define zF                  750     // == fBlockMaximumDistance/100 in SkyrimPerfs.ini
#define zN                  0.15    // == fNearDistance/100 in Skyrim.ini, game default == 15(i.e., set 0.15 here.)
#define FILM_WIDE           0.0359  // D3X, 35mm film.
#define Fmin                2.8     //maximum F-number to have circle shape.
#define Fmax                4       //minimum F-number to have solid polygon shape.
#define COC_CAP             0.01    // near blur cap, usage: lerp(FarEndCoC, NearEndCoC, COC_CAP). [0, 1]
#define AD_COUNT            10      // sample point for auto dof mode 1.
#define FIX                 10      //fix to adapt lens parameters to "smart blur".
#define FIX_FIX             4       //fix"fix"...curve control.
#define TILT_SHIFT_ANGLE    10      // do not change

#ifdef  ENABLE_POST_CHROMA
#define ENABLE_CHROMA
#endif
#ifdef  ENABLE_GP
#undef  ENABLE_AUTO_DOF
#define TILT_SHIFT_ANGLE    90
#endif
string  LENS_settings                 ="++++++ LENS SETTINGS ++++++";
bool    MF_Mode         <string UIName="MF mode";               > = {false};    //switch between auto/manual focus
bool    AF_Cursor       <string UIName="Display AF Cursor";     > = {false};    //display focus point for AF
float2  AF_Pos          <string UIName="AF pos";                float UIMin=0.00; float UIMax=1.00; float UIStep=0.001; > = {0.5, 0.5}; //(0, 0) at top left corner.
#ifdef  ENABLE_GP
float   AF_NearBlurCurve<string UIName="AF Near Blur Curve";    float UIMin=1;    float UIMax=50.00;  > = {12.00};  //scale CoC size in foreground
float   AF_FarBlurCurve <string UIName="AF Far Blur Curve";     float UIMin=0.00; float UIMax=100.00; > = {2.00};   //used to compute Far Blur Plane in AF
float   MF_FarBlurDepth <string UIName="MF Far Blur Plane(m)";  float UIMin=0.00; float UIMax=1000.00;> = {10};     //depth to apply maximum CoC
float   MF_Focused      <string UIName="MF Focused Plane(m)";   float UIMin=zN;   float UIMax=zF;     > = {1};      //Manual focused depth
float   LENS_Radius     <string UIName="Aperture Size(%)";      float UIMin=0.00; float UIMax=20;     > = {1};      //CoC Multiplier.
float   LENS_ShapeDeform<string UIName="Aperture Deform";       float UIMin=0.00; float UIMax=1.00;   > = {1};      //Aperture Shape deformation( 0 == circle, 1 == polygon).
#else
float   MF_Focused      <string UIName="MF Focused Plane(m)";   float UIMin=zN;   float UIMax=zF;     > = {1};      //Manual focused depth
float   LENS_FocalLength<string UIName="Focal Length(mm)";      float UIMin=10;   float UIMax=200.00; > = {50};     //Focal length
float   LENS_F_Number   <string UIName="F_Number";              float UIMin=1.00; float UIMax=22;     > = {5.6};    //f-number(f-stop)
#endif
int     LENS_Shape      <string UIName="Aperture Shape";        int   UIMin=5;    int   UIMax=9;      > = {0};      //Aperture shape, polygons
float   LENS_Ratio      <string UIName="Aperture Ratio";        float UIMin=0.2;  float UIMax=5;      > = {1};      //Aperture xy ratio
int     LENS_AngleOffset<string UIName="Aperture Angle(\xB0)";  int   UIMin=0;    int   UIMax=72;     > = {0};      //Base aperture angle.
#ifdef  ENABLE_AUTO_DOF
string  AD_settings                   ="++++++++ AUTO-DOF +++++++++";   //Automatically calculate F-number
int     AD_Mode         <string UIName="AD mode";               int   UIMin=0;    int   UIMax=2;      > = {0};      //0 == off( use f-number), 1 == use sample points, 2 == use Near DoF Plane
float   AD_Level        <string UIName="CoC Size(px)";          float UIMin=0.00; float UIMax=100.00; > = {1};      //CoC Size at sample point( AD mode == 1) or at certain depth (AD mode == 2)
float   AD_Depth        <string UIName="Near DoF Plane(m)";     float UIMin=zN;   float UIMax=zF;     > = {0.2};    //specified depth to assign AD_Level (AD mode == 2)
float   AD_Range        <string UIName="Sample Range";          float UIMin=0.00; float UIMax=2.00;   > = {0.33};   //AD_Offset scaling.( AD_Offset is sample points for AD mode == 1)
#endif
#ifdef  ENABLE_TILT_SHIFT
string  TS_settings                   ="+++++++ TILT-SHIFT ++++++++";   //give an optical illusion of a photograph of a miniature scale model
int     TS_Axis         <string UIName="Tilt Shift Axis(\xB0)"; int   UIMin=0;    int   UIMax=90;     > = {0};      //Rotate tilt axis
float   TS_Angle        <string UIName="Tilt Shift Angle(\xB0)";float UIMin=-TILT_SHIFT_ANGLE; float UIMax=TILT_SHIFT_ANGLE;  > = {0.00}; //0 == no tilt shift
#endif
string  BS_settings                   ="+++++ BOKEH SETTINGS ++++++";
int     BS_Quality      <string UIName="Quality";               int   UIMin=0;    int   UIMax=7;      > = {3};      //quality, [0, 7]
float   BS_Highlight    <string UIName="Bokeh Highlight";       float UIMin=0;    float UIMax=10;     > = {3};      // > 1 to increace highlight, < 1 to decreace. (not recommend over 5)
float   BS_Bias         <string UIName="Bokeh Bias";            float UIMin=0.00; float UIMax=1;      > = {0.5};    //Brightness of center point
float   BS_BiasCurve    <string UIName="Bokeh Bias Curve";      float UIMin=0.00; float UIMax=10.00;  > = {0.5};    //brightness curve from center to edge
#ifdef  ENABLE_GAUSSIAN
float   BS_GRadius      <string UIName="Guassian Radius";       float UIMin=0;    float UIMax=5;      > = {1};      //Guassian Size.
#endif
#ifdef  ENABLE_VIGNETTE
string  Vig_settings                  ="++++ OPTICAL VIGNETTE +++++";   //bokeh vignette, create "rotational blur" effect
float   Vig_Bias        <string UIName="Vignette Bias";         float UIMin=0;    float UIMax=1;      > = {0};      //0 == no vignette.
float   Vig_Scale       <string UIName="Vignette Radius";       float UIMin=1;    float UIMax=10;     > = {2};      //vignette radius, minimum == max CoC.
float   Vig_Curve       <string UIName="Vignette Curve";        float UIMin=0;    float UIMax=10;     > = {1};      //vignette curve from screen center to screen edge
#endif
#ifdef  ENABLE_NOISE
string  Noise_settings                ="++++++++++ NOISE ++++++++++";   //Draw noise to blured areas
float   Noise_Amount    <string UIName="Noise Amount";          float UIMin=0.00; float UIMax=100.00; > = {0.01};   //noise amount
float   Noise_Curve     <string UIName="Noise Curve";           float UIMin=0.00; float UIMax=1.00;   > = {1};      //pow( CoC, Curve)
#endif
#ifdef  ENABLE_CHROMA
string  CA_settings                   ="++++++++++ CHROMA +++++++++";   //Axial(longitudinal) & Transverse(lateral) CA
float   CA_TransCurve   <string UIName="Chroma Curve  (Trans)"; float UIMin=0.00; float UIMax=100.00; > = {2};      //distortion curve
float   CA_Trans        <string UIName="Chroma Amount (Trans)"; float UIMin=0.00; float UIMax=100.00; > = {0.01};   //distortion at screen edge
float   CA_AxialCurve   <string UIName="Chroma Curve  (Axial)"; float UIMin=0.00; float UIMax=100.00; > = {2};      //distortion curve
float   CA_Axial        <string UIName="Chroma Amount (Axial)"; float UIMin=0.00; float UIMax=100.00; > = {0.01};   //distortion at bokeh edge
float   CA_AxialMod     <string UIName="Chroma Red Multiplier"; float UIMin=-10;  float UIMax=10.00;  > = {0.33};   //radius scale for RB color channel
#endif
#ifdef  ENABLE_AUTO_DOF
float2  AD_Offset[AD_COUNT]=
{
    float2(1, 0),
    float2(-1,0),
    float2(0, 1),
    float2(0,-1),
    float2(0.5, 0.5),
    float2(0.5, -0.5),
    float2(-0.5, -0.5),
    float2(-0.5, 0.5),
    float2(-0.5, 0),
    float2(0.5, 0)
};
#endif
#ifdef  ENABLE_POST_CHROMA
#undef  ENABLE_CHROMA
#endif
//////////////////////////////////////////////////////////////////////////
//  external parameters, do not modify                                  //
//////////////////////////////////////////////////////////////////////////

//keyboard controlled temporary variables (in some versions exists in the config file).
//Press and hold key 1,2,3...8 together with PageUp or PageDown to modify. By default all set to 1.0
float4  tempF1;     //1,2,3,4
float4  tempF2;     //5,6,7,8
float4  tempF3;     //9,0
float4  ScreenSize; //x=Width, y=1/Width, z=ScreenScaleY, w=1/ScreenScaleY
float4  Timer;      //x=generic timer in range 0..1, period of 16777216 ms (4.6 hours), w=frame time elapsed (in seconds)
float   FadeFactor; //adaptation delta time for focusing

//textures
texture2D texColor;
texture2D texDepth;
texture2D texNoise;
texture2D texFocus; //computed focusing depth
texture2D texCurr;  //4*4 texture for focusing
texture2D texPrev;  //4*4 texture for focusing

sampler2D SamplerColor = sampler_state
{
    Texture = <texColor>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = NONE;//NONE;
    AddressU = Clamp;
    AddressV = Clamp;
    SRGBTexture=FALSE;
    MaxMipLevel=0;
    MipMapLodBias=0;
};

sampler2D SamplerDepth = sampler_state
{
    Texture = <texDepth>;
    MinFilter = POINT;
    MagFilter = POINT;
    MipFilter = NONE;
    AddressU = Clamp;
    AddressV = Clamp;
    SRGBTexture=FALSE;
    MaxMipLevel=0;
    MipMapLodBias=0;
};

sampler2D SamplerNoise = sampler_state
{
    Texture = <texNoise>;
    MinFilter = POINT;
    MagFilter = POINT;
    MipFilter = NONE;//NONE;
    AddressU = Wrap;
    AddressV = Wrap;
    SRGBTexture=FALSE;
    MaxMipLevel=0;
    MipMapLodBias=0;
};

//for focus computation
sampler2D SamplerCurr = sampler_state
{
    Texture = <texCurr>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = LINEAR;//NONE;
    AddressU = Clamp;
    AddressV = Clamp;
    SRGBTexture=FALSE;
    MaxMipLevel=0;
    MipMapLodBias=0;
};

//for focus computation
sampler2D SamplerPrev = sampler_state
{
    Texture = <texPrev>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = NONE;
    AddressU = Clamp;
    AddressV = Clamp;
    SRGBTexture=FALSE;
    MaxMipLevel=0;
    MipMapLodBias=0;
};

//for dof only in PostProcess techniques
sampler2D SamplerFocus = sampler_state
{
    Texture = <texFocus>;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = NONE;
    AddressU = Clamp;
    AddressV = Clamp;
    SRGBTexture=FALSE;
    MaxMipLevel=0;
    MipMapLodBias=0;
};

//////////////////////////////////////////////////////////////////////////
// Functions                                                            //
//////////////////////////////////////////////////////////////////////////

struct VS_OUTPUT_POST
{
    float4 vpos    : POSITION;
    float2 txcoord : TEXCOORD0;
};

struct VS_INPUT_POST
{
    float3 pos     : POSITION;
    float2 txcoord : TEXCOORD0;
};

struct VS_DoF_POST
{
    float4 vpos    : POSITION;
    float2 txcoord : TEXCOORD0;
    float2 CoC     : TEXCOORD1;
};

float2 Distort( float2 coord, float curve, float scale)
{
    float2 dist   = coord - 0.5;
    float  r      = length(float2( dist.x * ScreenSize.z, dist.y));
    float2 offset = pow( 2 * r, curve) * (dist / r) * scale;

    return coord + offset;
}

//temp.x == curve, .y == scale
float4 Chroma( sampler2D inputtex, float2 coord, float2 temp)
{
    float4 res = 0;
    res.r  = tex2D(inputtex, Distort(coord, temp.x, temp.y)).r;
    res.ga = tex2D(inputtex, coord).ga;
    res.b  = tex2D(inputtex, Distort(coord, temp.x, -temp.y)).b;
    return res;
}

float linearizeDepth(float zB)
{
    return zF * zN / (zF + zB * ( zN - zF));
}

//////////////////////////////////////////////////////////////////////////
//  begin focusing code                                                 //
//////////////////////////////////////////////////////////////////////////

VS_OUTPUT_POST VS_Focus(VS_INPUT_POST IN)
{
    VS_OUTPUT_POST OUT;
    OUT.vpos    = float4(IN.pos,1.0);
    OUT.txcoord = IN.txcoord;
    return OUT;
}

//SRCpass1X=ScreenWidth;
//SRCpass1Y=ScreenHeight;
//DESTpass2X=4;
//DESTpass2Y=4;
//Input fullres, Output 4x4
float4 PS_ReadFocus(VS_OUTPUT_POST IN) : COLOR
{
#ifdef ENABLE_AUTO_DOF
    float2 pos = (AD_Mode == 1)? 0.5: AF_Pos;
#else
    float2 pos = AF_Pos;
#endif
    float  res = linearizeDepth(tex2D(SamplerDepth, pos).x);
    return res;
}
//SRCpass1X=4;
//SRCpass1Y=4;
//DESTpass2X=4;
//DESTpass2Y=4;
//Input 4x4, Output 4x4
float4 PS_WriteFocus(VS_OUTPUT_POST IN) : COLOR
{
    float curr  = ( MF_Mode == true)? MF_Focused : tex2D(SamplerCurr, 0.5).x;
    float prev  = tex2D(SamplerPrev, 0.5).x;
    return lerp(prev, curr, saturate(FadeFactor));//time elapsed factor
}

//////////////////////////////////////////////////////////////////////////
//  Focusing pass                                                       //
//////////////////////////////////////////////////////////////////////////

technique ReadFocus
{
    pass P0
    {
        VertexShader = compile vs_3_0 VS_Focus();
        PixelShader = compile ps_3_0 PS_ReadFocus();

        ZEnable=FALSE;
        CullMode=NONE;
        ALPHATESTENABLE=FALSE;
        SEPARATEALPHABLENDENABLE=FALSE;
        AlphaBlendEnable=FALSE;
        FogEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
    }
}

technique WriteFocus
{
    pass P0
    {
        VertexShader = compile vs_3_0 VS_Focus();
        PixelShader = compile ps_3_0 PS_WriteFocus();

        ZEnable=FALSE;
        CullMode=NONE;
        ALPHATESTENABLE=FALSE;
        SEPARATEALPHABLENDENABLE=FALSE;
        AlphaBlendEnable=FALSE;
        FogEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
    }
}

//////////////////////////////////////////////////////////////////////////
//  end focusing, starting DoF                                          //
//////////////////////////////////////////////////////////////////////////

//OUT.CoC.x == max coc radius ; .y == aperture size
VS_DoF_POST VS_PostProcess(VS_INPUT_POST IN)
{
    VS_DoF_POST OUT;
#ifdef ENABLE_GP
    float  S1 = tex2Dlod(SamplerFocus, float4(0.5, 0.5, 0, 0)).x;
    float  fbd = (MF_Mode == true)? MF_FarBlurDepth: S1 * pow( 4.0, AF_FarBlurCurve);
    float  nbc = (MF_Mode == true)? 1.0: AF_NearBlurCurve;
    OUT.CoC    = LENS_Radius / 100;
    OUT.CoC.x *= min(max((S1 - zN) / (S1 * nbc), (zF - S1) / (fbd - S1)), 1);
#else
    float  f    = LENS_FocalLength / 1000;
#ifdef ENABLE_AUTO_DOF
    float3 S1  = tex2Dlod(SamplerFocus, float4(0.5, 0.5, 0, 0)).x;
    for(int i=0; i<AD_COUNT; i++)
    {
        float sample = linearizeDepth(tex2Dlod(SamplerDepth, float4(float2(0.5 + AD_Offset[i] * AD_Range), 0, 0)).x);
        S1.y = max(sample, S1.y);
        S1.z = min(sample, S1.z);
    }
    S1.yz      = (AD_Mode == 2)? AD_Depth: S1.yz;
    OUT.CoC    = FILM_WIDE * AD_Level * ScreenSize.y * (S1.x - f) / f;
    OUT.CoC    = (AD_Mode == 0)? f / LENS_F_Number: clamp(OUT.CoC * min(S1.y / abs(S1.x - S1.y), S1.z / abs(S1.x - S1.z)), f / 22, f / 5);//clamp between max/min f-number
    OUT.CoC.x *= lerp((zF - S1.x) / zF, (S1.x - zN) / zN, COC_CAP) * (f / (S1.x - f)) / FILM_WIDE;
#else
    float S1   = tex2Dlod(SamplerFocus, float4(0.5, 0.5, 0, 0)).x;
    OUT.CoC    = f / LENS_F_Number;
    OUT.CoC.x *= lerp((zF - S1) / zF, (S1 - zN) / zN, COC_CAP) * (f / (S1 - f)) / FILM_WIDE;
#endif
#endif
    OUT.vpos    = float4(IN.pos,1.0);
    OUT.txcoord = IN.txcoord;
    return OUT;
}

//Calculate CoC & Tilt_Shift
#ifdef ENABLE_GP
float4 PS_CoCtoAlpha(VS_DoF_POST IN, float2 vPos : VPOS) : COLOR
{
    float2 coord = IN.txcoord.xy;
    float4 res   = tex2D(SamplerColor, coord);;
    float  S1 = tex2D(SamplerFocus, 0.5).x;
    float  S2 = linearizeDepth(tex2D(SamplerDepth, coord).x);
    float  fbd   = (MF_Mode == true)? MF_FarBlurDepth: S1 * pow( 4.0, AF_FarBlurCurve);
    float  nbc   = (MF_Mode == true)? 1.0: AF_NearBlurCurve;
#ifdef ENABLE_TILT_SHIFT
    float  shiftAngle = (TS_Angle == 90)? 0.0 : TS_Angle;
    float2 othogonal  = float2(tan(TS_Axis * 0.0174533), -ScreenSize.z);
    float  TS_Dist    = dot(coord - 0.5, othogonal) / length(othogonal);
    float  depthShift = 1 + TS_Dist * tan(-shiftAngle * 0.017453292);
    S1 *= max(depthShift, 0);
    fbd   *= max(depthShift, 0.001);
#endif
    res.w  = (S1 - S2);
    res.w /= (S2 < S1)? (S1 * nbc): (fbd - S1);
    res.w *= IN.CoC.y / IN.CoC.x; 
    res.w  = clamp(res.w + 1, 0, 2);
    return res;
}
#else
float4 PS_CoCtoAlpha(VS_DoF_POST IN, float2 vPos : VPOS) : COLOR
{
    float2 coord= IN.txcoord;
    float  S2   = linearizeDepth(tex2D(SamplerDepth, coord).x);
    float  f    = LENS_FocalLength / 1000;
    float  S1   = tex2D(SamplerFocus, 0.5).x;
#ifdef ENABLE_TILT_SHIFT
    float  delta = ((coord.x - 0.5) * tan(TS_Axis * 0.0174533) - ScreenSize.z * ( coord.y - 0.5)) * FILM_WIDE;
    delta *= tan( TS_Angle * 0.0174533) / length(float2( tan(TS_Axis * 0.0174533), ScreenSize.z));
    delta += f * S1 / (S1 - f);
    S1 = f * delta / (delta - f);//S1'
#endif
    float m   = f / (S1 - f);//magfication
    float a   = IN.CoC.y;//aperture size
    float coc = a * m * (S1 - S2) / (S2 * FILM_WIDE);//CoC in % of film, [-1, 1]
    return float4(tex2D(SamplerColor, coord).rgb, clamp(coc / IN.CoC.x + 1, 0, 2));//manually clamp coc size of foreground, in blurdisk unit
}
#endif

//Dof pass
float4 PS_DepthOfField(VS_DoF_POST IN, float2 vPos : VPOS) : COLOR
{
    float2  coord       = IN.txcoord.xy;
    float2  pixelSize   = float2(LENS_Ratio, ScreenSize.z);
    float4  CenterColor = tex2D(SamplerColor, coord.xy); //.a is CenterDepth, near is larger than far,
    float   CenterCoC   = abs(CenterColor.a - 1);
#ifdef ENABLE_ADAPTIVE_QUALITY
    int     quality = CenterCoC * BS_Quality + 1;
#else
    int     quality = BS_Quality + 1;
#endif
#ifdef ENABLE_VIGNETTE
    float   vigradius   = IN.CoC.x * Vig_Scale;
    float2  vigcenter   = Distort(coord, Vig_Curve, lerp(0, vigradius, Vig_Bias)/ 0.707) - coord;//in sample coord sys
#endif
#ifdef ENABLE_CHROMA
    float2  chroma;
    chroma.x = pow(CenterCoC, CA_AxialCurve) * CA_Axial;
    chroma.y = CenterCoC * CA_Trans;
    chroma  /= 10;
#endif
    CenterCoC *=  IN.CoC.x;
    float4  res;
    res.rgb = CenterColor.rgb * (1 - BS_Bias);
    res.rgb = pow(res.rgb, (CenterCoC + 1) * BS_Highlight);
    res.a   = 1;
    float   leafangle   = 6.28318530 / LENS_Shape;
#ifdef ENABLE_GP
    float   shutterShape = LENS_ShapeDeform;   //[0, 1]
#else
    float   shutterShape = smoothstep(Fmin, Fmax, LENS_F_Number);
#endif
    float2  sampleOffset;

    int     dofTaps = quality * (quality + 1) * 3;
    
    [unroll(8)] //max ring count == 8
    for(int i=0; i < dofTaps; i++)
    {
        [loop]
        for(int j=0; j < i * 6; j++)
        {
            float sampleAngle = 1.04719755 * j / i;
            sincos(sampleAngle, sampleOffset.y, sampleOffset.x);
            sampleOffset *= pixelSize * CenterCoC * i / quality / 2;

            float deltaAngle = (sampleAngle + leafangle * shutterShape + LENS_AngleOffset * 0.017453) % leafangle;
            deltaAngle -= leafangle / 2;
            sampleOffset *= lerp( 1, (cos(leafangle/2)/cos(deltaAngle)), shutterShape);

            float4  tap;
            float3  weight;

    #ifdef ENABLE_CHROMA
            tap.ra      = tex2Dlod(SamplerColor, float4(Distort(coord + sampleOffset * ( 1 + chroma.x * CA_AxialMod), CA_TransCurve, chroma.y), 0, 0)).ra;
            weight.r    = (tap.a > CenterColor.a)? abs(tap.a - 1): 1.0;
            tap.ga      = tex2Dlod(SamplerColor, float4(coord + sampleOffset, 0, 0)).ga;
            weight.g    = (tap.a > CenterColor.a)? abs(tap.a - 1): 1.0;
            tap.ba      = tex2Dlod(SamplerColor, float4(Distort(coord + sampleOffset * ( 1 + chroma.x), CA_TransCurve, -chroma.y), 0, 0)).ba;
            weight.b    = (tap.a > CenterColor.a)? abs(tap.a - 1): 1.0;
    #else
            tap         = tex2Dlod(SamplerColor, float4(coord + sampleOffset, 0, 0));
            weight.rgb  = (tap.a > CenterColor.a)? abs(tap.a - 1): 1.0;
    #endif
            weight      = saturate(pow(weight * FIX, FIX_FIX));
            tap.rgb    *= lerp(1.0, pow(i/quality, BS_BiasCurve), BS_Bias);//Brightness of each ring
    #ifdef ENABLE_VIGNETTE
            weight     *= (length((sampleOffset - vigcenter) / pixelSize) > vigradius)? 0 : 1;
    #endif
            res.rgb    += pow(tap.rgb * weight.rgb, (CenterCoC + 1) * BS_Highlight);
            res.a      += pow(weight.g, (CenterCoC + 1) * BS_Highlight);
        }
    }
    res.rgb = pow( res.rgb / res.a, 1 / ((CenterCoC + 1) * BS_Highlight));
    res.a   = abs(CenterColor.a - 1);
    return res;
}

#ifdef ENABLE_GAUSSIAN
float4 PS_Guassian(VS_DoF_POST IN, float2 vPos : VPOS, uniform float2 offset) : COLOR
{
    float2  coord       = IN.txcoord;
    float4  CenterColor = tex2D(SamplerColor, coord);
#ifdef ENABLE_ADAPTIVE_QUALITY
    int     quality = CenterColor.a * BS_Quality + 1;
#else
    int     quality = BS_Quality;
#endif
    float   blurAmount  = CenterColor.a * IN.CoC.x / 10 / quality;
    float   weight[5]   = { 0.198596, 0.175713, 0.121703, 0.065984, 0.028002 };
    float4  res         = CenterColor * weight[0];
    for(int i=1; i < 5; i++)
    {
        res += tex2D(SamplerColor, coord + offset * i * blurAmount * BS_GRadius) * weight[i];
        res += tex2D(SamplerColor, coord - offset * i * blurAmount * BS_GRadius) * weight[i];
    }
    return res;
}
#endif
#ifdef ENABLE_POST_CHROMA
//Axial Chroma
float4 PS_Chroma(VS_DoF_POST IN, float2 vPos : VPOS, uniform float2 offset) : COLOR
{
    float2  coord       = IN.txcoord;
    float4  CenterColor = tex2D(SamplerColor, coord);
    float   blurAmount  = pow(CenterColor.a, CA_AxialCurve) * CA_Axial * 0.1;
    float   weight[5]   = { 0.198596, 0.175713, 0.121703, 0.065984, 0.028002 };
    float4  res         = CenterColor * weight[0];
    for(int i=1; i < 5; i++)
    {
        res.r += tex2D(SamplerColor, coord + offset * i * blurAmount * CA_AxialMod).r * weight[i];
        res.r += tex2D(SamplerColor, coord - offset * i * blurAmount * CA_AxialMod).r * weight[i];
        res.b += tex2D(SamplerColor, coord + offset * i * blurAmount).b * weight[i];
        res.b += tex2D(SamplerColor, coord - offset * i * blurAmount).b * weight[i];
    }
    return res;
}
#endif
//Noise + AF cursor
float4 PS_PostProcess(VS_DoF_POST IN, float2 vPos : VPOS) : COLOR
{
    float2 coord = IN.txcoord.xy;
#ifdef ENABLE_POST_CHROMA
    float  blurAmount = tex2D(SamplerColor, coord).a;
    float4 res        = Chroma(SamplerColor, coord, float2(CA_TransCurve, CA_Trans * blurAmount * 0.1));
#else
    float4 res        = tex2D(SamplerColor, coord);
    float  blurAmount = res.a;
#endif
//boris code
#ifdef ENABLE_NOISE
    float origgray = dot(res.xyz, 0.3333);
    origgray      /= origgray + 1.0;
    float4 cnoi    = tex2D(SamplerNoise, coord * 16.0 + origgray);
    float noiseAmount = Noise_Amount * pow(blurAmount, Noise_Curve);
    res *= lerp( 1, (cnoi.x+0.5), noiseAmount * saturate( 1.0 - origgray * 1.8));
#endif
#ifdef ENABLE_AUTO_DOF
    float2 pos = (AD_Mode == 1)? 0.5: AF_Pos;
#else
    float2 pos = AF_Pos;
#endif
    if(AF_Cursor == true)
    {
        float2 pixelSize = ScreenSize.y;
        pixelSize.y *= ScreenSize.z;
        if( ( abs(coord.x - pos.x) < 5 * pixelSize.x) && ( abs(coord.y - pos.y) < 5 * pixelSize.y))
            res.rgb = float3(2.0, 0, 0);
    }
    return res;
}

//////////////////////////////////////////////////////////////////////////
//  DoF Pass                                                            //
//////////////////////////////////////////////////////////////////////////
technique PostProcess
{
    pass P0
    {

        VertexShader = compile vs_3_0 VS_PostProcess();
        PixelShader = compile ps_3_0 PS_CoCtoAlpha();

        DitherEnable=FALSE;
        ZEnable=FALSE;
        CullMode=NONE;
        ALPHATESTENABLE=FALSE;
        SEPARATEALPHABLENDENABLE=FALSE;
        AlphaBlendEnable=FALSE;
        StencilEnable=FALSE;
        FogEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
    }
}

technique PostProcess2
{
    pass P0
    {
        VertexShader = compile vs_3_0 VS_PostProcess();
        PixelShader = compile ps_3_0 PS_DepthOfField();

        DitherEnable=FALSE;
        ZEnable=FALSE;
        CullMode=NONE;
        ALPHATESTENABLE=FALSE;
        SEPARATEALPHABLENDENABLE=FALSE;
        AlphaBlendEnable=FALSE;
        StencilEnable=FALSE;
        FogEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
    }
}

#define CHROMA_PASS0 PostProcess4
#define CHROMA_PASS1 PostProcess5
#define POST_PROCESS PostProcess4

#ifdef ENABLE_GAUSSIAN
#define CHROMA_PASS0 PostProcess5
#define CHROMA_PASS1 PostProcess6
#define POST_PROCESS PostProcess5
technique PostProcess3
{
    pass P0
    {
        VertexShader = compile vs_3_0 VS_PostProcess();
        PixelShader = compile ps_3_0 PS_Guassian(float2(LENS_Ratio, 0));

        DitherEnable=FALSE;
        ZEnable=FALSE;
        CullMode=NONE;
        ALPHATESTENABLE=FALSE;
        SEPARATEALPHABLENDENABLE=FALSE;
        AlphaBlendEnable=FALSE;
        StencilEnable=FALSE;
        FogEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
    }
}

technique PostProcess4
{
    pass P0
    {
        VertexShader = compile vs_3_0 VS_PostProcess();
        PixelShader = compile ps_3_0 PS_Guassian(float2(0, ScreenSize.z));

        DitherEnable=FALSE;
        ZEnable=FALSE;
        CullMode=NONE;
        ALPHATESTENABLE=FALSE;
        SEPARATEALPHABLENDENABLE=FALSE;
        AlphaBlendEnable=FALSE;
        StencilEnable=FALSE;
        FogEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
    }
}
#endif
#ifdef ENABLE_POST_CHROMA
#define POST_PROCESS PostProcess5
#ifdef ENABLE_GAUSSIAN
#define POST_PROCESS PostProcess7
#endif
technique CHROMA_PASS0
{
    pass P0
    {
        VertexShader = compile vs_3_0 VS_PostProcess();
        PixelShader  = compile ps_3_0 PS_Chroma(float2(LENS_Ratio, 0));

        ColorWriteEnable=RED|BLUE;
        DitherEnable=FALSE;
        ZEnable=FALSE;
        CullMode=NONE;
        ALPHATESTENABLE=FALSE;
        SEPARATEALPHABLENDENABLE=FALSE;
        AlphaBlendEnable=FALSE;
        StencilEnable=FALSE;
        FogEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
    }
}

technique CHROMA_PASS1
{
    pass P0
    {
        VertexShader = compile vs_3_0 VS_PostProcess();
        PixelShader  = compile ps_3_0 PS_Chroma(float2(0, ScreenSize.z));

        ColorWriteEnable=RED|BLUE;
        DitherEnable=FALSE;
        ZEnable=FALSE;
        CullMode=NONE;
        ALPHATESTENABLE=FALSE;
        SEPARATEALPHABLENDENABLE=FALSE;
        AlphaBlendEnable=FALSE;
        StencilEnable=FALSE;
        FogEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
    }
}
#endif
technique POST_PROCESS
{
    pass P0
    {
        VertexShader = compile vs_3_0 VS_PostProcess();
        PixelShader = compile ps_3_0 PS_PostProcess();

        ColorWriteEnable=RED|GREEN|BLUE;
        DitherEnable=FALSE;
        ZEnable=FALSE;
        CullMode=NONE;
        ALPHATESTENABLE=FALSE;
        SEPARATEALPHABLENDENABLE=FALSE;
        AlphaBlendEnable=FALSE;
        StencilEnable=FALSE;
        FogEnable=FALSE;
        SRGBWRITEENABLE=FALSE;
    }
}