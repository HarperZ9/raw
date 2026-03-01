//////////////////////////////////////////////////////////////////////////
//                                                                      //
//  ENBSeries dx11 effect file                                          //
//                                                                      //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//  Scopes by Kingeric1992                                              //
//  June.6.2017                                                         //
//                                                                      //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

//defines these in enbeffectpostpass.fx if required.

//#define LayoutHelper //un comment this to enable tweaking scope layout

#define SCOPE_PASS    MyPass5
#define SCOPE_PASS1   MyPass6
#define SCOPE_PASS2   MyPass7

static const float3 backgroundColor   = {0.25, 0.25, 0.25};
static const float3 Scope_CursorColor = {0.8, 0.75, 0.5};

#define UI static const

UI int    Scope_Bar3       <string UIName="scope layout";        float UIMin= 0;float UIMax=0;> = {0};
UI float2 Scope_Offset     <string UIName="Wavefrom Offset";     float UIMin=-1;float UIMax=1;> = {0.0, 0.75};
UI float2 Scope_Scale      <string UIName="Wavefrom Scale";      float UIMin= 0;float UIMax=1;> = {0.75, 0.75};
UI float2 Scope1_Offset    <string UIName="Histogram Offset";    float UIMin=-1;float UIMax=1;> = {0.75, 0.75};
UI float2 Scope1_Scale     <string UIName="Histogram Scale";     float UIMin= 0;float UIMax=1;> = {0.75, 0.75};
UI float2 Scope2_Offset    <string UIName="Minimap Offset";      float UIMin=-1;float UIMax=1;> = {-0.75, 0.75};
UI float  Scope2_Scale     <string UIName="Minimap Scale";       float UIMin= 0;float UIMax=1;> = {0.75};
UI float2 Scope3_Offset    <string UIName="vectorScope Offset";  float UIMin=-1;float UIMax=1;> = {-0.75, -0.5};
UI float  Scope3_Scale     <string UIName="vectorScope Scale";   float UIMin= 0;float UIMax=1;> = {0.6};
UI float2 Scope4_Offset    <string UIName="memoryColor Offset";  float UIMin=-1;float UIMax=1;> = {-0.75, 0.25};
UI float  Scope4_Scale     <string UIName="memoryColor Scale";   float UIMin= 0;float UIMax=1;> = {0.75};
UI float  View_Scale       <string UIName="View Scale";          float UIMin= 0;              > = {0.75};
UI float2 View_Offset      <string UIName="View Offset";         float UIMin=-1;float UIMax=1;> = {0.25, -0.25};

Texture2D vectorOverlayTex  <string ResourceName="Textures/Scopes/vector_overlay.png";>;

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

#ifdef SCOPE_UNIFORMS
float4 Timer;           //x = [0,1]timer, map to 16777216 ms (4.6 hours), y = average fps, w = frame time elapsed (in seconds)
float4 ScreenSize;      //x = Width, y = 1/Width, z = Width/Height, w = Height/Width
float  AdaptiveQuality; //changes in range 0..1, 0 means full quality, 1 lowest dynamic quality (0.33, 0.66 are limits for quality levels)
float4 Weather;         //x = current weather index, y = outgoing weather index (0 if non indexed), z = weather transition, w = time of the day in 24 standart hours.
float4 TimeOfDay1;      //x = dawn, y = sunrise, z = day, w = sunset. Interpolators range from 0..1
float4 TimeOfDay2;      //x = dusk, y = night. Interpolators range from 0..1
float  ENightDayFactor; //changes in range 0..1, 0 means that night time, 1 - day time
float  EInteriorFactor; //changes 0 or 1. 0 means that exterior, 1 - interior

//keyboard controlled temporary variables. Press and hold key 1,2,3...8 together with PageUp or PageDown to modify. By default all set to 1.0
float4 tempF1;          //1,2,3,4
float4 tempF2;          //5,6,7,8
float4 tempF3;          //9,0

float4 tempInfo1;       //xy = cursor position in range 0..1 of screen, z  = is shader editor window active, w  = mouse buttons with values 0..7 as follows:
//0 = none, 1 = left, 2 = right, 3 = left+right, 4 = middle, 5 = left+middle, 6 = right+middle, 7 = left+right+middle (or rather cat is sitting on your mouse)
float4 tempInfo2;       //xy = previous left click pos, zw = previous right click pos.

Texture2D TextureOriginal;  //color R10B10G10A2 32 bit ldr format
Texture2D TextureColor;     //color which is output of previous technique (except when drawed to temporary render target), R10B10G10A2 32 bit ldr format
Texture2D TextureDepth;     //scene depth R32F 32 bit hdr format

//temporary textures which can be set as render target for techniques via annotations like <string RenderTarget="RenderTargetRGBA32";>
Texture2D RenderTargetRGBA32;   //R8G8B8A8 32 bit ldr format
Texture2D RenderTargetRGBA64;   //R16B16G16A16 64 bit ldr format
Texture2D RenderTargetRGBA64F;  //R16B16G16A16F 64 bit hdr format
Texture2D RenderTargetR16F;     //R16F 16 bit hdr format with red channel only
Texture2D RenderTargetR32F;     //R32F 32 bit hdr format with red channel only
Texture2D RenderTargetRGB32F;   //32 bit hdr format without alpha
#endif

SamplerState ScopeSamplerPoint {
    Filter   = MIN_LINEAR_MAG_POINT_MIP_LINEAR;
    AddressU = BORDER;
    AddressV = BORDER;
};

SamplerState ScopeSamplerLinear {
    Filter   = MIN_MAG_MIP_LINEAR;
    AddressU = BORDER;
    AddressV = BORDER;
};

struct VS_Scope_IO {
    float4 pos     : SV_POSITION;
    float4 txcoord : TEXCOORD0;
};


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

static const float2 Scope_InvSize = float2(ScreenSize.y, ScreenSize.y*ScreenSize.z);

float3 Scope_Hue_to_RGB( float h) {
    return saturate(float3( abs(h * 6.0f - 3.0f) - 1.0f,
                            2.0f - abs(h * 6.0f - 2.0f),
                            2.0f - abs(h * 6.0f - 4.0f)));
}

float3 Scope_HSL_to_RGB( float3 HSL ) {
    return (Scope_Hue_to_RGB(HSL.x) - 0.5) * (1.0 - abs(2.0 * HSL.z - 1)) * HSL.y + HSL.z;
}

float3 Scope_RGB_to_HSL(float3 color)
{
    float3 HSL   = 0.0f;
    float  M     = max(color.r, max(color.g, color.b));
    float  C     = M - min(color.r, min(color.g, color.b));
           HSL.z = M - 0.5 * C;
    if (C != 0.0f)
    {
        float3 Delta  = (color.brg - color.rgb) / C + float3(2.0f, 4.0f, 6.0f);
               Delta *= step(M, color.gbr); //if max = rgb
        HSL.x = frac(max(Delta.r, max(Delta.g, Delta.b)) / 6.0);
        HSL.y = (HSL.z == 1)? 0.0: C/ (1 - abs( 2 * HSL.z - 1));
    }
    return HSL;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

void VS_Waveform( inout VS_Scope_IO io, out float2 uv : TEXCOORD1)
{
    io.pos = float4(io.pos.x*0.5, io.pos.y*256.0*Scope_InvSize.y, io.pos.z, 1.0);
    uv = io.txcoord.xy;
    if(Scope_LocalData)
    io.txcoord.x = 0.5-Scope_ZoomCenter.x+(io.txcoord.x-0.5)/Scope_ZoomScale;
    io.txcoord.z = (1.0 - io.txcoord.y) * 256.0 - 0.5;
    io.txcoord.w = (1.0 - io.txcoord.y) * 256.0 + 0.5;
    io.txcoord.y = 0.5-Scope_ZoomCenter.y-0.5/Scope_ZoomScale;
}

//sampling on center of 4 nearby points
float4 PS_Waveform( VS_Scope_IO i, float2 uv : TEXCOORD1) : SV_Target
{
    if(!Scope_Enable||uv.x>=1.0||uv.y>=1.0) discard;
    float2 coord = float2(i.txcoord.x, Scope_InvSize.y);
    float4 bin   = 0.0;
    float  steps = 2.0*coord.y;
    float  bound = 1.0;

    if(Scope_LocalData) {
        coord.y += i.txcoord.y;
        steps /= Scope_ZoomScale;
        bound = 0.5-Scope_ZoomCenter.y+0.5/Scope_ZoomScale;
    }

    bound -= Scope_InvSize.y;

    while(coord.y < bound) {
        float4 color    = TextureColor.SampleLevel(ScopeSamplerLinear, coord, 0.0)*255.0; //10 bit
               color.a  = dot(color.rgb, 0.333);
               bin     += step(i.txcoord.z, color) * step(color, i.txcoord.w);
               coord.y += steps;
    }
    return bin*Scope_TapWeight/16.0;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

void VS_VectorScope( inout VS_Scope_IO io)
{
    io.pos = float4(io.pos.xy/4.0 - 3.0/4.0, io.pos.z, 1.0);
    if(Scope_LocalData)
    io.txcoord.xy = 0.5-Scope_ZoomCenter+(io.txcoord.xy-0.5)/Scope_ZoomScale;
    io.txcoord.zw = io.txcoord.xy + Scope_InvSize;
    io.txcoord.xy = io.txcoord.xy - Scope_InvSize;
}

float4 PS_VectorScope(VS_Scope_IO i) : SV_Target
{
    if(!Scope_Enable) discard;
    return float4(Scope_RGB_to_HSL((TextureColor.Sample(ScopeSamplerLinear, i.txcoord.xy).rgb+
                                    TextureColor.Sample(ScopeSamplerLinear, i.txcoord.xw).rgb+
                                    TextureColor.Sample(ScopeSamplerLinear, i.txcoord.zy).rgb+
                                    TextureColor.Sample(ScopeSamplerLinear, i.txcoord.zw).rgb)*0.25), 1.0);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

void VS_Histogram( inout VS_Scope_IO io, out float2 uv : TEXCOORD1 )
{
    io.pos.x = (io.pos.x+1.0)*Scope_InvSize.x-1.0;
    io.pos.y = io.pos.y*256.0*Scope_InvSize.y;
    io.pos.w = 1.0;
    uv = io.txcoord.xy;
    io.txcoord.y  = (io.txcoord.y-0.5)*256.0*Scope_InvSize.y+0.5+Scope_InvSize.y*0.5;
    io.txcoord.x  = 0.25;
}

float4 PS_Histogram( VS_Scope_IO i, float2 uv : TEXCOORD1 ) : SV_Target
{
    if(!Scope_Enable||uv.x>=1.0||uv.y>=1.0) discard;
    float2 coord = i.txcoord.xy;
    float4 bin   = 0.0;
    float bound = 0.75 - Scope_InvSize.x*0.5;
    //from 0.25 to 0.75
    while(coord.x < bound) {
       bin     += RenderTargetRGBA32.SampleLevel(ScopeSamplerLinear, coord, 0.0);
       coord.x += Scope_InvSize.x;
    }
    return bin*Scope1_TapWeight/Scope_TapWeight*Scope_InvSize.x*2.0;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

void VS_Quad( inout VS_Scope_IO io ) {
    io.pos.w = 1.0;
}

float4 PS_Blank( VS_Scope_IO i ) : SV_Target {
    return float4(backgroundColor, 1.0);
}

float4 PS_Passthrough( VS_Scope_IO i ) : SV_Target {
    if(!Scope_Enable) discard;
    return RenderTargetRGBA32.Sample(ScopeSamplerLinear, i.txcoord.xy);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

static const float VectorOffset = 2.93; //use reference hue to calibrate

void VS_VectorScopeTex( inout VS_Scope_IO io )
{
    io.pos.w  = 1.0;
    io.pos.xy = (io.pos.xy+1.0)*Scope_InvSize*256.0-1.0;
    io.txcoord.zw = (io.txcoord.xy-0.5)*2.0;
}

float4 PS_VectorScopeTex( VS_Scope_IO i ) : SV_Target
{
    if(!Scope_Enable||!Scope_Vector) discard;

    float2 cent  = float2(length(i.txcoord.zw), atan(i.txcoord.w/i.txcoord.z)+1.57);
    if(i.txcoord.z > 0.0) cent.y += 3.14;

    if(cent.x > 1.0) discard;

    cent.y = 6.28-cent.y;
    cent.y = (cent.y+VectorOffset)%6.28;

    float  w     = 0.0;
    float2 coord = Scope_InvSize*2.0;
    float2 steps = coord;
    coord.y  += 0.75;
    float2 bound = float2(0.25, 1.0) - Scope_InvSize;

    [loop]
    while(coord.x<bound.x) {
        [loop]
        while(coord.y<bound.y) {
            float3 hsl      = RenderTargetRGBA32.SampleLevel(ScopeSamplerLinear, coord, 0.0).rgb;
                   w       += step(abs(hsl.x*6.28-cent.y),0.005/cent.x)*step(abs(hsl.y-cent.x), 0.005);
                   coord.y += steps.y;
        }
        coord.y  = Scope_InvSize.y*2.0+0.75;
        coord.x += steps.x;
    }

    return float4(Scope_HSL_to_RGB(float3(cent.y/6.28, 1.0, 0.6))*w*Scope3_TapWeight, 1.0);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

void VS_Scene( inout VS_Scope_IO io )
{
    if(Scope_Enable) io.pos.xy = io.pos.xy*View_Scale-View_Offset;
    io.pos.w = 1.0;
    io.txcoord.zw = (io.txcoord.xy-0.5)/Scope_ZoomScale-Scope_ZoomCenter+0.5;
}

float4 PS_Scene( VS_Scope_IO i ) : SV_Target
{
    return TextureColor.Sample(ScopeSamplerPoint, Scope_Enable? i.txcoord.zw:i.txcoord.xy);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

void VS_Scope_Minimap( inout VS_Scope_IO io )
{
    io.pos = float4( io.pos.xy*Scope2_Scale/3.0-Scope2_Offset, io.pos.z, 1.0);
    io.txcoord.zw = (io.txcoord.xy-0.5+Scope_ZoomCenter)*Scope_ZoomScale*2.0;
}

//sampling on center of 4 nearby points
float4 PS_Scope_Minimap( VS_Scope_IO i ) : SV_Target
{
    if(!Scope_Enable) discard;
    float w = step(abs(i.txcoord.z),1.0 ) * step(abs(i.txcoord.w),1.0) -
              step(abs(i.txcoord.z),1.0-0.01*Scope_ZoomScale) *
              step(abs(i.txcoord.w),1.0-0.01*Scope_ZoomScale*ScreenSize.z);
    return lerp(TextureColor.Sample(ScopeSamplerLinear, i.txcoord.xy), float4(1.0, 0.0, 0.0, 1.0), w);
}
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

void VS_Scope_Waveform( inout VS_Scope_IO io, out float2 uv : TEXCOORD1)
{
    io.pos = float4( io.pos.xy*Scope_Scale/float2(1.5,3.0)-Scope_Offset, io.pos.z, 1.0);
    uv = io.txcoord.xy;
    io.txcoord.a   = ((uv.y-0.5)*255.0+0.5)*Scope_InvSize.y+0.5;
    io.txcoord.rgb = Scope_Parade ? uv.x * 1.5 + 0.25: uv.x * 0.5 + 0.25;
    io.txcoord.gb -= Scope_Parade ? float2(0.5, 1.0): 0.0;
}

//sampling on center of 4 nearby points
float4 PS_Scope_Waveform( VS_Scope_IO i, float2 uv : TEXCOORD1) : SV_Target
{
    if(!Scope_Enable) discard;

    float3 weight = step(i.txcoord.rgb, 0.75-0.5*Scope_InvSize.x) *
                    step(0.25+0.5*Scope_InvSize.x, i.txcoord.rgb);
    weight.r *= RenderTargetRGBA64.Sample(ScopeSamplerLinear, i.txcoord.ra).r;
    weight.g *= RenderTargetRGBA64.Sample(ScopeSamplerLinear, i.txcoord.ga).g;
    weight.b *= RenderTargetRGBA64.Sample(ScopeSamplerLinear, i.txcoord.ba).b;

    if(Scope_Parade) {
        weight  = Scope_HSL_to_RGB(float3(0.0,   1.0, weight.r)) +
                  Scope_HSL_to_RGB(float3(0.333, 1.0, weight.g)) +
                  Scope_HSL_to_RGB(float3(0.666, 1.0, weight.b));
    }

    if(abs(1.0-Scope_Cursor.x/255.0-uv.y)*Scope_Scale.y < 0.002) return float4(Scope_CursorColor, 1.0);
    if(abs(1.0-Scope_Cursor.y/255.0-uv.y)*Scope_Scale.y < 0.002) return float4(Scope_CursorColor, 1.0);
    if(abs(1.0-Scope_Cursor.z/255.0-uv.y)*Scope_Scale.y < 0.002) return float4(Scope_CursorColor, 1.0);
    if(abs(1.0-Scope_Cursor.w/255.0-uv.y)*Scope_Scale.y < 0.002) return float4(Scope_CursorColor, 1.0);

    return float4(weight, 1.0);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

void VS_Scope_Histogram( inout VS_Scope_IO io )
{
    io.pos       = float4( io.pos.xy*Scope1_Scale/3.0-Scope1_Offset, io.pos.z, 1.0);
    io.txcoord.z = (0.5-io.txcoord.x)*256.0*Scope_InvSize.y+0.5;
}

float4 PS_Scope_Histogram( VS_Scope_IO i ) : SV_Target
{
    if(!Scope_Enable) discard;

    float4 weight = step(1.0-i.txcoord.y, RenderTargetRGBA64.Sample(ScopeSamplerLinear, float2(Scope_InvSize.x*0.5, i.txcoord.z)));

    if(Scope_mode == 1) weight.rgb = weight.a;
    else if(Scope_mode == 2) weight.gb  = 0.0;
    else if(Scope_mode == 3) weight.rb  = 0.0;
    else if(Scope_mode == 4) weight.rg  = 0.0;

    if(abs(Scope_Cursor.x/255.0-i.txcoord.x)*Scope1_Scale.x < 0.002) return float4(Scope_CursorColor, 1.0);
    if(abs(Scope_Cursor.y/255.0-i.txcoord.x)*Scope1_Scale.x < 0.002) return float4(Scope_CursorColor, 1.0);
    if(abs(Scope_Cursor.z/255.0-i.txcoord.x)*Scope1_Scale.x < 0.002) return float4(Scope_CursorColor, 1.0);
    if(abs(Scope_Cursor.w/255.0-i.txcoord.x)*Scope1_Scale.x < 0.002) return float4(Scope_CursorColor, 1.0);

    weight.a = 1.0;
    return weight;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

void VS_Scope_Vector( inout VS_Scope_IO io )
{
    io.pos.xy    = io.pos.xy*Scope3_Scale*float2(1.0,ScreenSize.z)/3.0-Scope3_Offset;
    io.pos.w     = 1.0;
    io.txcoord.z = io.txcoord.x*256.0*Scope_InvSize.x;
    io.txcoord.w = 1.0-(1.0-io.txcoord.y)*256.0*Scope_InvSize.y;
}

float4 PS_Scope_Vector( VS_Scope_IO i ) : SV_Target
{
    if(!Scope_Enable||!Scope_Vector||length(i.txcoord.xy*2.0-1.0)>1.0) discard;
    float4 overlay = vectorOverlayTex.Sample(ScopeSamplerLinear, i.txcoord.xy);
    return float4(lerp( RenderTargetRGBA64.Sample(ScopeSamplerPoint, i.txcoord.zw).rgb,
                        overlay.rgb, overlay.a), 1.0);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

void VS_Scope_MemoryColor( inout VS_Scope_IO io )
{
    io.pos.xyw = float3( io.pos.xy*Scope4_Scale/3.0-Scope4_Offset, 1.0);
    if(Scope_LocalData)
    io.txcoord.xy = 0.5-Scope_ZoomCenter+(io.txcoord.xy-0.5)/Scope_ZoomScale;
}

float4 PS_Scope_MemoryColor( VS_Scope_IO i ) : SV_Target
{
    if(!Scope_Enable) discard;

    float3 hsl = Scope_RGB_to_HSL(TextureColor.Sample(ScopeSamplerLinear, i.txcoord.xy).rgb);
    float4 res = 0.0;

    //hue(red)
    res.r = (1.0-smoothstep(0.00, 0.06, abs(hsl.x-0.0)))+
            (1.0-smoothstep(0.00, 0.06, abs(hsl.x-1.0)));

    //hue(green)
    res.g = (1.0-smoothstep(0.00, 0.05, hsl.x-0.333))*
            (1.0-smoothstep(0.00, 0.2, 0.333-hsl.x));

    //hue(red)
    res.b = (1.0-smoothstep(0.00, 0.05, hsl.x-0.666))*
            (1.0-smoothstep(0.00, 0.2, 0.666-hsl.x));

    //sat(rgb)
    res.rgb *= smoothstep(0.05, 0.2, abs(hsl.y-0.0));

    //hue & sat(skin 25 degree)
    res.rgb += (1.0-smoothstep(0.0, 0.02, abs(hsl.x-25.0/360.0)))*
           (1.0-smoothstep(0.47, 0.50, abs(hsl.y-0.49)))*
            HSL_to_RGB(float3(25.0/360.0, 0.7, 0.7));

    //lightness (red blue)
    res.rb *= 1.0-smoothstep(0.38, 0.48, abs(hsl.z-0.5));

    //lightness (green)
    res.g *= 1.0-smoothstep(0.35, 0.4, abs(hsl.z-0.5));

    return res;
}
