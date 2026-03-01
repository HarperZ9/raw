// https://www.shadertoy.com/view/4dSBDt

float hash( float n )
{
    return frac(sin(n)*43758.5453);
}

float4 PS_TempBloom(VS_OUTPUT IN) : SV_Target
{
	float2 q = IN.txcoord.xy;
    float2 blurRadius    = (20.0) / Resolution.xy;

    float4 sum = 0.0;
    float NUM_SAMPLES = 20.;
    float phiOffset = hash(dot(IN.txcoord.xy, (1.12,2.251)) + Timer.x);
    for(float i = 0.; i < NUM_SAMPLES; i++)
    {
        float2 r = blurRadius * i / NUM_SAMPLES;
        float phi = (i / NUM_SAMPLES + phiOffset) * 2.0 * 3.1415926;
        float2 uv = q + (sin(phi), cos(phi))*r;
        sum += TextureColor.Sample(LinearSampler, uv, 0.0);
    }
    const float BLOOM_AMOUNT = 0.05;
    sum.xyz = lerp(TextureColor.Sample(LinearSampler, q, 0.0).xyz, sum.xyz / NUM_SAMPLES, BLOOM_AMOUNT);
    float exposure = 1 * (1.0+0.2*sin(0.5*Timer.x)*sin(1.8*Timer.x));
	return float4 (exposure*sum.xyz, 1.0);
}
