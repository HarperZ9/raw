#include "ReShadeUI.fxh"
#include "ReShade.fxh"
#include "NVE_ColorConfig.fx"

/*
bool    space3            
<
    string UIName=" ";
> = {false};


bool    text3             
<
    string UIName="|| NVE Official Color Grading :";
> = {false};
*/

uniform int   PPPreset
<
    ui_label	=	"Preset Selector";
	ui_type = "radio";
	ui_items = "Default\0Color Boost Filter\0Warm Filter\0Vintage Filter\0Retro Filter\0VHS Filter\0Old Cam Filter\0Matrix Filter\0Noir Filter\0";
> = 0;

/*
bool    space4            
<
    string UIName="  ";
> = {false};

bool    text4             
<
    string UIName="|| Color Grading Customizer :";
> = {false};

bool    text4_2            
<
    string UIName="!! -Preset Selector- must be 0 first";
> = {false};
*/

uniform float   PPGamma
<
    ui_label	=	"Gamma";
    ui_type		=	"slider";
    ui_min		=	0.2;//not zero!!!
    ui_max		=	5.0;
> = 1.0;

uniform float   PPBrightness
<
    ui_label	=	"Brightness";
    ui_type		=	"slider";
    ui_min=0.0;
    ui_max=10.0;
> = 1.0;

uniform float   PPContrast
<
    ui_label="Contrast";
    ui_type		=	"slider";
    ui_min=0.0;
    ui_max=10.0;
> = 1.0;

uniform float   PPSaturation
<
    ui_label="Saturation";
    ui_type		=	"slider";
    ui_min=0.0;
    ui_max=10.0;
> = 1.0;

uniform float   PPCA
<
    ui_label	=	"Chromatic Aberration";
    ui_type		=	"slider";
    ui_min		=	0.0;//not zero!!!
    ui_max		=	1.0;
> = 0.0;

uniform float   PPBleach
<
    ui_label="Bleach Bypass";
    ui_type		=	"slider";
    ui_min=0.0;
    ui_max=1.0;
> = 0.0;

/*
bool    spacep           
<
    ui_label="    ";
> = {false};
*/

uniform float   PPTempStrength
<
    ui_label="Color Temperature Blend";
    ui_type		=	"slider";
    ui_min=0.0;
    ui_max=1.0;
> = 0.0;

uniform float   PPTemperature
<
    ui_label="Color Temperature in Kelvin";
    ui_type		=	"slider";
    ui_min=1000.0;
    ui_max=40000.0;
> = 6500.0;

/*
bool    space5            
<
    ui_label="   ";
> = {false};
*/

uniform float   PPInBlack
<
    ui_label="Levels : Min Input";
    ui_type		=	"slider";
    ui_min=0.0;
    ui_max=1.0;
> = 0.0;

uniform float   PPInWhite
<
    ui_label="Levels : Max Input";
    ui_type		=	"slider";
    ui_min=0.0;
    ui_max=1.0;
> = 1.0;

uniform float   PPOutBlack
<
    ui_label="Levels : Min Output";
    ui_type		=	"slider";
    ui_min=0.0;
    ui_max=1.0;
> = 0.0;

uniform float   PPOutWhite
<
    ui_label="Levels : Max Output";
    ui_type		=	"slider";
    ui_min=0.0;
    ui_max=1.0;
> = 1.0;

/*
bool    space6            
<
    ui_label="      ";
> = {false};

bool    text6            
<
    ui_label="!! Grey color (127,127,127) is neutral/disabled!";
> = {false};
*/

uniform float3  PPColorBalanceShadows <
    ui_label="Shadows Tint";
    ui_type = "color";
> = float3(0.5, 0.5, 0.5);

uniform float3  PPColorBalanceHighlights <
    ui_label="Highlights Tint";
    ui_type = "color";
> = float3(0.5, 0.5, 0.5);

/*
bool    space7          
<
    ui_label="     ";
> = {false};
*/

uniform float3  PPChannellerperR <
    ui_label="Channel lerper : Red";
    ui_type = "color";
> = float3(1.0, 0.0, 0.0);

uniform float3  PPChannellerperG <
    ui_label="Channel lerper : Green";
    ui_type = "color";
> = float3(0.0, 1.0, 0.0);

uniform float3  PPChannellerperB <
    ui_label="Channel lerper : Blue";
    ui_type = "color";
> = float3(0.0, 0.0, 1.0);

uniform float   VHS_Intensity
<
    ui_label="VHS Filter : Intensity";
    ui_type		=	"slider";
    ui_min=0.0;
    ui_max=0.005;
> = 0.001;

uniform float   VHS_Speed
<
    ui_label="VHS Filter : Speed";
    ui_type		=	"slider";
    ui_min=0.0;
    ui_max=0.02;
> = 0.005;

uniform float   VHS_LinesIntensity
<
    ui_label="VHS Filter : Lines Intensity";
    ui_type		=	"slider";
    ui_min=0.0;
    ui_max=0.1;
> = 0.05;

uniform float timer < source = "timer"; >;

texture2D           vintage_tex < source = "vintage.png";>{Width = 1920; Height = 1080;}; 
sampler2D           vintage_smp { Texture = vintage_tex; };

float3 kelvin2rgb(float temp)
{
	float3x3 m;
	if(temp <= 6500.0){
		m = float3x3(	float3(0.0, -2902.1955373783176, -8257.7997278925690),
            			float3(0.0, 1669.5803561666639, 2575.2827530017594),
            			float3(1.0, 1.3302673723350029, 1.8993753891711275));
	}else{
		m = float3x3(	float3(1745.0425298314172, 1216.6168361476490, -8257.7997278925690),
                        float3(-2666.3474220535695, -2173.1012343082230, 2575.2827530017594),
                        float3(0.55995389139931482, 0.70381203140554553, 1.8993753891711275));
	}
											 
    return saturate(float3(m[0] / (clamp(temp, 1000.0, 40000.0) + m[1]) + m[2]));
}

static float randomSeed = 1618.03398875;

float hash(float n) {
    return frac(sin(n/1873.1873) * randomSeed);
}

float2 lens(in float2 uv, in float k1, in float k2)
{
    uv = uv * 2.0 - 1.0;
    float r2 = uv.x*uv.x + uv.y*uv.y;
    uv *= 1.0 + k1 * r2 + k2 * r2 * r2;   
    uv = (uv * .5 + .5);
    return uv;
}

float4 PS_Color(float4 vpos : SV_Position, float2 tex : TexCoord) : SV_Target
{
    float _CA                       = PPCA;
    float3 _ColorBalanceShadows     = PPColorBalanceShadows;
    float3 _ColorBalanceHighlights  = PPColorBalanceHighlights;
    float3 _ChannellerperR          = PPChannellerperR;
    float3 _ChannellerperG          = PPChannellerperG;
    float3 _ChannellerperB          = PPChannellerperB;
    float _InBlack                  = PPInBlack;
    float _InWhite                  = PPInWhite;
    float _OutBlack                 = PPOutBlack;
    float _OutWhite                 = PPOutWhite;
    float _TempStrength             = PPTempStrength;
    float _Temperature              = PPTemperature;
    float _Gamma                    = PPGamma;
    float _Brightness               = PPBrightness;
    float _Contrast                 = PPContrast;
    float _Saturation               = PPSaturation;
    float _Bleach                   = PPBleach;

    float2 uv = tex;

    if(PPPreset == 6){
        float k1 = -.2 + .7 * (sin( -0.7) * .5 + .5);  // [-.25 : 0.75 ]
        float k2 = 0.;//   
        uv = lens( uv, k1, k2 );
    }

    float2 pixeloffset = uv * BUFFER_WIDTH;
    float3 color       = tex2D(ReShade::BackBuffer,uv).xyz;
    float3 rec709      = float3(0.2126f, 0.7152f, 0.0722f);

    ColorCorrection(PPPreset,_InBlack, _InWhite, _OutBlack, _OutWhite,
                   _ColorBalanceShadows, _ColorBalanceHighlights,
                   _ChannellerperR, _ChannellerperG, _ChannellerperB,
                   _TempStrength, _Temperature, _Gamma,
                   _Brightness, _Contrast, _Saturation, _CA, _Bleach);

    if(PPPreset == 5){
        float mover = VHS_Intensity;
        uv.x += mover*hash(sin(uv.y+timer*VHS_Speed)*500);
    }else if(PPPreset == 6){
        //uv += 0.05f*dot(uv,uv);
    } 


    if(_CA > 0){
        if(PPPreset == 6){
            float2 p = smoothstep(0.05,0.5,length(uv - float2(0.5,0.5)));
            color.x = tex2Dlod(ReShade::BackBuffer,float4(uv-float2(0.001,0.001)*_CA*p,0.0,0.0)).x;
	        color.y = tex2Dlod(ReShade::BackBuffer,float4(uv-float2(-0.001,-0.001)*_CA*p,0.0,0.0)).y;
	        color.z = tex2Dlod(ReShade::BackBuffer,float4(uv-float2(0.0005,-0.0005)*_CA*p,0.0,0.0)).z;
        }else{
            color.x = tex2Dlod(ReShade::BackBuffer,float4(uv-float2(0.001,0.001)*_CA,0.0,0.0)).x;
	        color.y = tex2Dlod(ReShade::BackBuffer,float4(uv-float2(-0.001,-0.001)*_CA,0.0,0.0)).y;
	        color.z = tex2Dlod(ReShade::BackBuffer,float4(uv-float2(0.0005,-0.0005)*_CA,0.0,0.0)).z;
        }
    }

    if(PPPreset == 5){
        color.xyz = lerp(color.xyz,uv.y-hash(sin(uv.y+timer*VHS_Speed)*500),VHS_LinesIntensity);
    }     

    float luma  = dot(color, rec709);
    color.xyz = lerp(luma, color.xyz, _Saturation);

    color.xyz   = lerp(color.xyz, color.xyz * kelvin2rgb(_Temperature), _TempStrength);
    float luma2 = dot(color.xyz, rec709);
    color.xyz  *= (luma2 > 1e-6) ? (luma / luma2) : 1.0;

    luma         = dot(color.xyz, rec709);
    float  bluma = min(1.0f, max(0.0f, 10.0f * (luma - 0.45)));
    float3 b1    = 2.0f * color.xyz * luma; 
    float3 b2    = 1.0f - 2.0f * (1.0f - luma) * (1.0f - color.xyz);
    color.xyz    = (_Bleach * lerp(b1,b2,bluma)) + (1.0f - _Bleach) * color.xyz; 
    
    color.xyz  = max(color - _InBlack, 0.0f) / max(_InWhite - _InBlack, 1e-5);
    color.xyz  = pow(color, _Gamma);
    color.xyz *= (_OutWhite - _OutBlack) + _OutBlack;

    luma       = dot(color.xyz, rec709);
    luma      *= luma;
    color.rgb += (_ColorBalanceHighlights * 2.0f - 1.0f) * color.xyz * (luma);
    color.rgb += (_ColorBalanceShadows * 2.0f - 1.0f) * (1.0f - color.xyz) * (1.0f - luma);

    color.x = dot(color.xyz, _ChannellerperR);
    color.y = dot(color.xyz, _ChannellerperG);
    color.z = dot(color.xyz, _ChannellerperB);

    color.xyz  = (color.xyz - 0.5f) * _Contrast + 0.5f;
    color.xyz *= _Brightness;


    if(PPPreset==6){
        color.xyz += hash(timer*0.01f*sin(uv.x*74802.65)*cos(uv.y*21356.21))*0.1f;
        float4 t = tex2D(vintage_smp,tex);
        color.xyz *= 1.0f-t.w+t.xyz;
    }

    return float4(color.xyz, 1.0f);
}

technique NVEColor < ui_label = "NaturalVision Enhanced: Color"; >
{
	pass p0
	{
		VertexShader = PostProcessVS;
		PixelShader = PS_Color;

    }   
}