#include "ReShadeUI.fxh"
#include "ReShade.fxh"

uniform float   GreenScreenDistance
<
    ui_label="  ---> Green Screen : Distance";
    ui_type="slider";
    ui_min=0.0;
    ui_max=0.99;
> = 0.99;

uniform float   GreenScreenRadial
<
    ui_label="  ---> Green Screen : Radial Size";
    ui_type="slider";
    ui_min=0.0;
    ui_max=10.0;
> = 0.0;

uniform bool    GreenScreenMask             
<
    ui_label="  ---> Green Screen : Mask Mode";
> = false;

uniform bool    GreenScreenCustom             
<
    ui_label="  ---> Green Screen : Enable Custom Color";
> = false;


uniform float3  GreenScreenColor <
    ui_label="  ---> Green Screen : Custom Color";
    ui_type="color";
> = float3(0.0, 0.0, 1.0);

float4  PS_GreenScreen(float4 vpos : SV_Position, float2 uv : TexCoord) : SV_Target
{
    float depth  = ReShade::GetLinearizedDepth(uv);
    //depth /= 10.0f - depth * 10.0f;

	float3 bgcolor = float3(0.0f,1.0f,0.0f);
	if (GreenScreenCustom) bgcolor = GreenScreenColor;

	depth *= length(float2((uv.x - 0.5f) * GreenScreenRadial, 1.0f));
	depth *= length(float2((uv.y - 0.5f) * GreenScreenRadial / (BUFFER_WIDTH/BUFFER_HEIGHT), 1.0f));
	float res = depth > GreenScreenDistance;

	if (!GreenScreenMask)
    {
        float3 color  = tex2D(ReShade::BackBuffer,uv).xyz;
               color  = lerp(color, bgcolor, saturate(res));
        return float4(color.xyz,1.0f);
    }
    else
    {
        return float4(res.xxx,1.0f);
    }
}

technique NveGreenScreen < ui_label = "NaturalVision Enhanced: Green Screen"; >
{
	pass p0
	{
		VertexShader = PostProcessVS;
		PixelShader = PS_GreenScreen;
    }

    
}