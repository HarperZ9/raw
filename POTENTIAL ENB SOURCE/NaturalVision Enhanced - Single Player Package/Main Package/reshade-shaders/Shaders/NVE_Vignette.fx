#include "ReShadeUI.fxh"
#include "ReShade.fxh"

#include "NVE/nve.fxh"

uniform float VignetteDepth <
    ui_tooltip = "Depth at which the effect is applied.";
    ui_label = "Vignette: Depth";
	ui_type = "drag";
	ui_min = 0.0f;
	ui_max = 10.0f;
> = 6.0f;

uniform float VignetteCurve <
    ui_label = "Vignette: Curve";
	ui_type = "drag";
	ui_min = 0.0f;
	ui_max = 10.0f;
> = 6.0f;

uniform float VignetteStrength <
    ui_label = "Vignette: Strength";
	ui_type = "drag";
	ui_min = 0.0f;
	ui_max = 1.0f;
> = 0.700f;

uniform float OverlayOffset <
    ui_label = "Vignette: Overlay Blend";
	ui_type = "drag";
	ui_min = 0.0f;
	ui_max = 10.0f;
> = 2.5f;

uniform int BlendMode <
    ui_type = "combo";
    ui_label = "Vignette: Blend Mode";
    ui_items = "Classic\0Overlay\0";
> = 1; 


uniform bool FirstPerson <hidden=true;> = false;

float3 OverlayBlend(float3 base, float3 blend, float2 uv)
{
    
    float3 result = lerp(
        2.0 * base * blend,
        1.0 - 2.0 * (1.0 - base) * (1.0 - blend),
        step(0.5, base)
    );

    return lerp(base, result, 1.0-saturate(uv.y*OverlayOffset));
}

float4 VignettePass(float4 vpos : SV_Position, float2 tex : TexCoord) : SV_Target
{
	float4 color = tex2D(ReShade::BackBuffer, tex);
	float4 original = color;
	float distance = (1.0 + pow( (1.0f-tex.y*1.0f), 1.0f * VignetteCurve) * -1.0f); 
	float3 vigCol = float3(0.85,0.85,0.85)-VignettingColour.xyz;
	[branch]
	if(BlendMode == 0){
		color.rgb -= vigCol*(1.0f-distance)*color.rgb;
		return lerp(original, color, smoothstep(0,1.0f,ReShade::GetLinearizedDepth(tex)*VignetteDepth+(FirstPerson*10))*ScanlineFilterParams.y*VignetteStrength);
	}
	else if(BlendMode == 1){
		color.rgb = OverlayBlend(color.rgb,vigCol*(distance),tex);
		return lerp(original, color, smoothstep(0,.0f,ReShade::GetLinearizedDepth(tex)*VignetteDepth+(FirstPerson*10))*ScanlineFilterParams.y*VignetteStrength);
	}
	//color.rgb *= distance;
	//color.rgb = saturate(color.rgb);
	return original;
}

technique NveVignette < ui_label = "NaturalVision Enhanced: Vignette"; >
{
	pass
	{
		VertexShader = PostProcessVS;
		PixelShader = VignettePass;
	}
}