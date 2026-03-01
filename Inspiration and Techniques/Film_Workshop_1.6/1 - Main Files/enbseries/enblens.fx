//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
// ENBSeries Fallout4 effect file                                   //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                  //
//  Flares enblens.fx by kingeric1992                               //
//                                                                  //
//  Featuring:                                                      //
//      Fully customizable lens flare with the capability of        //
//  achieving multiple visual representation of various lens flares //
//  observed in physicle cameras.                                   //
//                                                                  //
//  For more info,  visit                                           //
//    http://enbseries.enbdev.com/forum/viewtopic.php?f=7&t=5347    //
//                                                                  //
//  update: Dec.19.2016                                             //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

#define FlareCount 12

int    Title0           <string UIName="\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4";  float UIMin=0.0; float UIMax=0.0; > = { 0 };
int    ME               <string UIName="        _(:3 \xB8/)_";      float UIMin=0.0; float UIMax=0.0; > = { 0 };
int    Title1           <string UIName="\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4\xA4 "; float UIMin=0.0; float UIMax=0.0; > = { 0 };
int    LENS_String0     <string UIName="|----Lens Settings";        float UIMin=0.0; float UIMax=0.0;  > = { 0 };
float  LENS_F_Number    <string UIName="|  Lens F number";          float UIMin=2.0; float UIMax=8.0; > = {7.0};
float  LENS_Shape       <string UIName="|  Lens Shape";             float UIMin=5.0; float UIMax=9.0;  float UIStep=1; > = {0};
float  LENS_AngleOffset <string UIName="|  Lens Angle(\xB0)";       float UIMin=0.0; float UIMax=72.0; float UIStep=1; > = {0};
float  LENS_Deform      <string UIName="|  Lens Forced Deform";     float UIMin=0.0; float UIMax=1.0;  > = {1}; //( 0 == circle, 1 == polygon).
bool   LENS_ForceDeform <string UIName="|     Enable Forced Deform";                 > = {false};
float  LENS_Ratio       <string UIName="|  Lens Ratio";             float UIMin=0.0; > = {1.0};
float  LENS_Grain       <string UIName="|  Lens Grain";             float UIMin=0.0; > = {0.05};
float2 LENS_Sensitivity <string UIName="|  Lens Sensitivity";       float UIMin=0.0; > = { 8.0, 1.0 };
int    LENS_String1     <string UIName="|     .x power, .y scale";  float UIMin=0.0; float UIMax=0.0; > = { 0 };
int    LENS_String2     <string UIName="|--------------------";     float UIMin=0.0; float UIMax=0.0; > = { 0 };
int    LENS_String3     <string UIName="  ";                        float UIMin=0.0; float UIMax=0.0; > = { 0 };
int    LENS_String4     <string UIName="|----Chromatic Aberration"; float UIMin=0.0; float UIMax=0.0; > = { 0 };
float  LENS_CA_Curve    <string UIName="|  CA Curve";               float UIMin=0.0; > = { 1.0 };
float  LENS_CA_Scale    <string UIName="|  CA Scale";               > = { -2.0 };
float3 LENS_CA_Color0   <string UIName="|  CA Color0";              string UIWidget = "color";        > = {1.0, 0.0, 0.0};
float3 LENS_CA_Color1   <string UIName="|  CA Color1";              string UIWidget = "color";        > = {0.0, 1.0, 0.0};
float3 LENS_CA_Color2   <string UIName="|  CA Color2";              string UIWidget = "color";        > = {0.0, 0.0, 1.0};
int    LENS_String5     <string UIName="|-------------------- ";    float UIMin=0.0; float UIMax=0.0; > = { 0 };
int    LENS_String6     <string UIName="   ";                       float UIMin=0.0; float UIMax=0.0; > = { 0 };

static const float Fmin                = 2.8; //maximum F-number to have circle shape.
static const float Fmax                = 4.0; //minimum F-number to have solid polygon shape.
static const float GaussianSensitivity = 0.1; //controles the sensitivity of gaussian mask that used to create ring flares.
static const float PrefilterRange      = 4.0;

//+++++++++++++++++++++++++++++
//external enb parameters, do not modify
//+++++++++++++++++++++++++++++
float4  Timer;                  //x = generic timer in range 0..1, period of 16777216 ms (4.6 hours), y = average fps, w = frame time elapsed (in seconds)
float4  ScreenSize;             //x = Width, y = 1/Width, z = Width/Height, w = Height/Width
float   AdaptiveQuality;        //changes in range 0..1, 0 means full quality, 1 lowest dynamic quality (0.33, 0.66 are limits for quality levels)
float4  Weather;                //x = current weather index, y = outgoing weather index, z = weather transition, w = time of the day in 24 standart hours. 
float4  TimeOfDay1, TimeOfDay2; //x2 = dawn, y2 = sunrise, z2 = day, w2 = sunse, x2 = dusk, y2 = night. Interpolators range from 0..1
float   ENightDayFactor;        //changes in range 0..1, 0 means that night time, 1 - day time
float   EInteriorFactor;        //changes 0 or 1. 0 means that exterior, 1 - interior

//keyboard controlled temporary variables. Press and hold key 1,2,3...8 together with PageUp or PageDown to modify. By default all set to 1.0
float4  tempF1, tempF2, tempF3; //1,2,3,4,5,6,7,8,9,0
//    0 = none      1 = left         2 = right         3 = left+right
//    4 = middle    5 = left+middle  6 = right+middle  7 = left+right+middle (or rather cat is sitting on your mouse)
float4  tempInfo1;  // xy = cursor position in range 0..1 of screen; z = is shader editor window active; w = mouse buttons with values 0..7 as aboves
float4  tempInfo2;  // xy = cursor position of previous left mouse button click, zw = cursor position of previous right mouse button click

//+++++++++++++++++++++++++++++
//mod parameters, do not modify
//+++++++++++++++++++++++++++++
Texture2D TextureDownsampled; //color R16B16G16A16 64 bit or R11G11B10 32 bit hdr format. 1024*1024 size
Texture2D TextureColor;       //color which is output of previous technique (except when drawed to temporary render target), R16B16G16A16 64 bit hdr format. screen size
Texture2D TextureOriginal;    //color R16B16G16A16 64 bit or R11G11B10 32 bit hdr format, screen size. PLEASE AVOID USING IT BECAUSE OF ALIASING ARTIFACTS, UNLESS YOU FIX THEM
Texture2D TextureDepth;       //scene depth R32F 32 bit hdr format, screen size. PLEASE AVOID USING IT BECAUSE OF ALIASING ARTIFACTS, UNLESS YOU FIX THEM
Texture2D TextureAperture;    //this frame aperture 1*1 R32F hdr red channel only. computed in PS_Aperture of enbdepthoffield.fx

//temporary textures which can be set as render target for techniques via annotations like <string RenderTarget="RenderTargetRGBA32";>
Texture2D RenderTarget1024;   //R16B16G16A16F 64 bit hdr format, 1024*1024 size
Texture2D RenderTarget512;    //R16B16G16A16F 64 bit hdr format, 512*512 size
Texture2D RenderTarget256;    //R16B16G16A16F 64 bit hdr format, 256*256 size
Texture2D RenderTarget128;    //R16B16G16A16F 64 bit hdr format, 128*128 size
Texture2D RenderTarget64;     //R16B16G16A16F 64 bit hdr format, 64*64 size
Texture2D RenderTarget32;     //R16B16G16A16F 64 bit hdr format, 32*32 size
Texture2D RenderTarget16;     //R16B16G16A16F 64 bit hdr format, 16*16 size
Texture2D RenderTargetRGBA32; //R8G8B8A8 32 bit ldr format, screen size
Texture2D RenderTargetRGBA64F;//R16B16G16A16F 64 bit hdr format, screen size

SamplerState Sampler0 { Filter = MIN_MAG_MIP_POINT;  AddressU = Border; AddressV = Border; };
SamplerState Sampler1 { Filter = MIN_MAG_MIP_LINEAR; AddressU = Border; AddressV = Border; };

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void getVertices( out float2 array[10])
{
    [unroll]
    for(int i=0; i< 10; i++) { sincos(radians(LENS_AngleOffset + i * 360.0 / LENS_Shape) , array[i].y, array[i].x); }
}

static float2 blurratio = ScreenSize.y * float2( min( LENS_Ratio, 1.0), ScreenSize.z / max( LENS_Ratio, 1.0) );

static float  blurshape = LENS_ForceDeform? LENS_Deform : smoothstep(Fmin, Fmax, LENS_F_Number);

float2 Distort( float2 coord, float curve, float scale)
{
    float  r      = length(coord * ScreenSize.y / blurratio);
    return coord + pow( 2 * r, curve) * (coord / r) * scale;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

Texture2D TextureGrain <string ResourceName = "enbGrain.png";>;

struct FlareStruct
{
    float  index;
    float  curve;
    float  scale;
    float4 tint;
};

float4 FlareFunc( float2 coord, FlareStruct IN )
{
    static const float2 FlareUV[4]    = { float2(0.25, 0.75), float2(0.75, 0.75), float2(0.75, 0.25), float2(0.25, 0.25) }; 
    
           coord     = Distort(coord, IN.curve, IN.scale) + FlareUV[IN.index];
    float2 border    = 1.0 - saturate( (coord <= FlareUV[IN.index] - 0.25) + (coord >= FlareUV[IN.index] + 0.25));
    float4 lens      = border.x * border.y * TextureColor.Sample(Sampler1, coord); 
           lens.rgb  = saturate(lens.rgb - lens.a * 4.0 * ( 1.0 - IN.tint.a ) / LENS_F_Number ) * IN.tint.rgb;         
           lens.rgb  = saturate(lens.rgb - TextureGrain.Sample(Sampler1, coord).r * min(0.5, abs(IN.curve - 1.0)) * LENS_Grain / LENS_F_Number); //or use procedural grain         
    return lens;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void VS_LENS_Prepass( inout float4 pos : SV_POSITION, inout float4 offset0 : TEXCOORD0)
{
    float2 steps    = ScreenSize.y * PrefilterRange;
           steps.y *= ScreenSize.z;
    offset0 = (offset0.xyxy * 3.0 - 1.5) + float4(steps, -steps);
    pos     = float4( pos.xy * 0.5 - float2(0.5, -0.5), pos.z, 1.0);
}

float4 PS_LENS_Prepass( float4 pos : SV_POSITION, float4 offset0 : TEXCOORD0) : SV_Target
{
    float2 coord[4]         = { offset0.xy, offset0.xw, offset0.zy, offset0.zw };
    float4 LENS_CA_Tint[3]  = { float4(LENS_CA_Color0, 1.0), float4(LENS_CA_Color1, 1.0), float4(LENS_CA_Color2, 1.0) };
    
    float4 color;
    float4 res = 0.0;
    float4 sum = 0.0;
    
    float2 offset = (offset0.xy + offset0.zw) * 0.5;
           offset = Distort( offset, LENS_CA_Curve, LENS_CA_Scale * 0.01 ) - offset;

    for(int i=0; i<3; i++)
    {
        for(int j=0; j<4; j++)
        {
            color   = TextureDownsampled.Sample( Sampler1, coord[j] + offset * i + 0.5);
            color.a = max(color.r, max(color.g, color.b));
            sum    += color / (color.a + 1.0);
        }
        res += saturate( pow( sum * 0.25, LENS_Sensitivity.x) * LENS_Sensitivity.y) * LENS_CA_Tint[i];
        sum  = 0.0;
    }   

    return res * 0.3333;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void VS_LENS_Main( inout float4 pos : SV_POSITION, inout float2 txcoord0 : TEXCOORD0)
{
    txcoord0 *= 0.5;
    pos       = float4( pos.xy * 0.5 - float2(0.5, -0.5), pos.z, 1.0);
}

float4 PS_LENS_Main( float4 pos : SV_POSITION, float2 txcoord0 : TEXCOORD0, uniform float range) : SV_Target
{
    clip( ( txcoord0.x >= 0.5 || txcoord0.y >= 0.5 ) ? -1.0:1.0);
    
    float2 vertices[10];
    getVertices(vertices);            
    
    float4 color  = TextureColor.Sample(Sampler1, txcoord0);
    float  avg    = color.a;

    for(int i=1; i < 3; i++)
    {
        for(int j=0; j<LENS_Shape; j++)
        {
            float2 offset = txcoord0 + vertices[j] * blurratio * range *  i;
            float4 tmp    = TextureColor.SampleLevel(Sampler1, offset, 0.0) * step(offset.x, 0.495) * step(offset.y, 0.495);
                   avg   += tmp.a;
                   color  = max(color, tmp);
        }
    }

    for(int k=0; k<LENS_Shape; k++)
    {
        float2 offset = vertices[k] + vertices[k + 1];
               offset = txcoord0 + offset / lerp(length(offset), 2.0, blurshape) * blurratio * range * 2.0;      
        float4 tmp    = TextureColor.SampleLevel(Sampler1, offset, 0.0) * step(offset.x, 0.495) * step(offset.y, 0.495);    
               avg   += tmp.a;
               color  = max(color, tmp);
    }    
    
    return float4( color.rgb, lerp( avg / (1.0 + 3.0 * LENS_Shape), color.a, GaussianSensitivity));
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void VS_LENS_Bypass( inout float4 pos : SV_POSITION, inout float2 txcoord0 : TEXCOORD0, uniform float2 offset, uniform float2 scale)
{
    txcoord0 = txcoord0 * scale;
    pos      = float4( ( pos.xy - offset) * scale, pos.z, 1.0);
}

float4 PS_LENS_Bypass( float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    return TextureColor.Sample(Sampler1, txcoord);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//test your flares here

float4 Intensity1 < string UIName="Intensity1"; > = {1.0,  1.0,   1.0, 1.0};
float4 Curve1  <    string UIName="Curve1";     > = {1.0,  1.25,  1.0, 1.0};
float4 Scale1  <    string UIName="Scale1";     > = {0.0, -0.30, -0.5, 1.0};
float4 Tint1   <    string UIName="Tint1";  string UIWidget="color";   > = {0.25, 0.25, 1.0, 1.0};

float4 Intensity2 < string UIName="Intensity2"; > = {1.0,  1.0,  1.0,   1.0};
float4 Curve2  <    string UIName="Curve2";     > = {1.4,  1.0,  1.0,   0.7};
float4 Scale2  <    string UIName="Scale2";     > = {0.66, 2.0, -0.66, -0.5};
float4 Tint2   <    string UIName="Tint2";  string UIWidget="color";   > = {0.66, 1.0, 1.0, 0.25};

float4 Intensity3 < string UIName="Intensity3"; > = {1.0,  1.0,  1.0,   1.0};
float4 Curve3  <    string UIName="Curve3";     > = {1.0,  0.8,  1.0,   3.0};
float4 Scale3  <    string UIName="Scale3";     > = {3.0, -1.5, -2.0, -20.0};
float4 Tint3   <    string UIName="Tint3";  string UIWidget="color";   > = {1.0, 1.0, 1.0, 0.0};


static FlareStruct FlareData[FlareCount] =
{
    {1, Curve1.x, Scale1.x, Tint1 * Intensity1.x },
    {1, Curve1.y, Scale1.y, Tint1 * Intensity1.y },
    {1, Curve1.z, Scale1.z, Tint1 * Intensity1.z },
    {1, Curve1.w, Scale1.w, Tint1 * Intensity1.w },

    {2, Curve2.x, Scale2.x, Tint2 * Intensity2.x },
    {2, Curve2.y, Scale2.y, Tint2 * Intensity2.y },
    {2, Curve2.z, Scale2.z, Tint2 * Intensity2.z },
    {2, Curve2.w, Scale2.w, Tint2 * Intensity2.w },

    {3, Curve3.x, Scale3.x, Tint3 * Intensity3.x },
    {3, Curve3.y, Scale3.y, Tint3 * Intensity3.y },
    {3, Curve3.z, Scale3.z, Tint3 * Intensity3.z },
    {3, Curve3.w, Scale3.w, Tint3 * Intensity3.w }
};

void VS_LENS_Mix( inout float4 pos : SV_POSITION, inout float2 txcoord : TEXCOORD0)
{
    txcoord  = txcoord * 0.5 - 0.25;
    pos      = float4( pos.xy * 0.5 - float2(0.5, -0.5), pos.z, 1.0);
}

float4 PS_LENS_Mix( float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float4 flare = 0.0;
    
    for(int i=0; i<FlareCount; i++)
    {
        flare += FlareFunc(txcoord, FlareData[i]);
    }
    return flare;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void VS_LENS_Fullres( inout float4 pos : SV_POSITION, inout float2 txcoord : TEXCOORD0)
{
    txcoord *= 0.5;
    pos.w    = 1.0;
}

float4 PS_LENS_Fullres( float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    return TextureColor.Sample(Sampler1, txcoord);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Techniques are drawn one after another and they use the result of the previous technique as input color to the next one.  
// The number of techniques is limited to 255.  If UIName is specified, then it is a base technique which may have extra techniques with indexing
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//prepass downsample --> filter out chunck of block
technique11 Lens   <string UIName="lens";> 
{
    pass p0
    {
        SetVertexShader( CompileShader(vs_5_0, VS_LENS_Prepass()));
        SetPixelShader(  CompileShader(ps_5_0, PS_LENS_Prepass()));
    }
}

technique11 Lens1
{
    pass p0 
    {
        SetVertexShader( CompileShader(vs_5_0, VS_LENS_Main()));
        SetPixelShader(  CompileShader(ps_5_0, PS_LENS_Main( 4.0 / LENS_F_Number )));
    }
}

technique11 Lens2
{
    pass p0
    {
        SetVertexShader( CompileShader(vs_5_0, VS_LENS_Main()));
        SetPixelShader(  CompileShader(ps_5_0, PS_LENS_Main( 4.0 / LENS_F_Number * 2.0)));
    }
}

technique11 Lens3
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_LENS_Main()));
        SetPixelShader( CompileShader(ps_5_0, PS_LENS_Main( 4.0 / LENS_F_Number * 4.0)));
    }
    pass p1
    {
        SetVertexShader(CompileShader(vs_5_0, VS_LENS_Bypass( float2( 1.0, 1.0), float2(0.5, 0.5))));
        SetPixelShader( CompileShader(ps_5_0, PS_LENS_Bypass()));
    }    
}

//watch out for AB swap chain access
technique11 Lens4 
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_LENS_Main()));
        SetPixelShader( CompileShader(ps_5_0, PS_LENS_Main( 4.0 / LENS_F_Number * 8.0)));
    }
    pass p1
    {
        SetVertexShader(CompileShader(vs_5_0, VS_LENS_Bypass( float2(1.0, 1.0), float2(0.5, 0.5))));
        SetPixelShader( CompileShader(ps_5_0, PS_LENS_Bypass()));
    }  
}

technique11 Lens5
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_LENS_Main()));
        SetPixelShader( CompileShader(ps_5_0, PS_LENS_Main( 4.0 / LENS_F_Number * 16.0)));
    }
    pass p1
    {
        SetVertexShader(CompileShader(vs_5_0, VS_LENS_Bypass( float2( -1.0, 0.0), float2(0.5, 1.0))));
        SetPixelShader( CompileShader(ps_5_0, PS_LENS_Bypass()));
    }  
}

/*
  16x | 8x
------------
   2x | 4x
*/

technique11 Lens6
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_LENS_Mix()));
        SetPixelShader( CompileShader(ps_5_0, PS_LENS_Mix()));
    }
}

//fullres. Preferrebly, do txcoord scaling in enbeffect.fx instead
technique11 Lens7
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_LENS_Fullres()));
        SetPixelShader( CompileShader(ps_5_0, PS_LENS_Fullres()));
    }
}
