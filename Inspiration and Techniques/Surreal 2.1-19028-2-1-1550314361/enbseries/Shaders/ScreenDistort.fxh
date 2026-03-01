// Souce: https://www.shadertoy.com/view/XdfGzH

float4 PS_ScreenDistortion(VS_OUTPUT IN) : SV_Target
{
	float2 uv = IN.txcoord.xy;

	const float2 ctr = float2(0.5,0.5);
	float2 ctrvec = ctr - uv;
	float ctrdist = length( ctrvec );
	ctrvec /= ctrdist;
	uv += ctrvec * max(0.0, pow(ctrdist, distpow)-0.0025);

    return TextureColor.Sample(LinearSampler, float2(1,1) *uv );
}