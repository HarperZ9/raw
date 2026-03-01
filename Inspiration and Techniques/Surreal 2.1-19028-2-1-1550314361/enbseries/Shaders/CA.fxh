

#define Shift    0.5
#define Strength 0.5

float3 ChromaticAberration(float3 colorInput, float2 texcoord)
{
	float3 color;
	// Sample the color components
	color.r = TextureColor.Sample(LinearSampler, texcoord + (PixelSize * Shift)).r;
	color.g = colorInput.g;
	color.b = TextureColor.Sample(LinearSampler, texcoord - (PixelSize * Shift)).b;

	// Adjust the strength of the effect
	return lerp(colorInput, color, Strength);
}

/*
float4 CA(VS_OUTPUT IN) : SV_Target
{
	float2 coord = IN.txcoord.xy;

    float2 v = -1.0 + 2.0*coord;
    v.x *= Resolution.x/ Resolution.y;
    
    float vign = smoothstep(4.0, 0.6, length(v));
    
    float2 centerToUv = coord-0.5;
	float3 aberr;
    aberr.x = TextureColor.SampleLevel(LinearSampler, (0.5)+centerToUv*0.995,0.0).x; 
    aberr.y = TextureColor.SampleLevel(LinearSampler, (0.5)+centerToUv*0.997, 0.0).y;
    aberr.z = TextureColor.SampleLevel(LinearSampler, (0.5)+centerToUv, 0.0).z;
    return float4 (vign*aberr, 1.0);
}
*/