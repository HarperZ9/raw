//------------------LENS DISTORTION--------------------//
// by Weaseltron                                       //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//

#if USE_DISTORTION == 1
   float2 barrelDistortion( float2 p, float2 amt )
{
    p = 2.0 * p - 1.0;
    float maxBarrelPower = sqrt(5.0);
    float radius = dot(p,p); //faster but doesn't match above accurately
    p *= pow(radius, maxBarrelPower * amt);


    return p * 0.5 + 0.5;
}

float2 brownConradyDistortion(float2 uv, float scalar)
{
    uv = (uv - 0.5 ) * 2.0;

    if( true )
    {
        float barrelDistortion1 = -0.02 * scalar; // K1 in text books
        float barrelDistortion2 = 0.0 * scalar; // K2 in text books

        float r2 = dot(uv,uv);
        uv *= 1.0 + barrelDistortion1 * r2 + barrelDistortion2 * r2 * r2;

    }

   return (uv / 2.0) + 0.5;
}

float3 LensCA( float2 uv )
{

    float maxDistort = LCAStrength;//4 * (1.0-iMouse.x/iResolution.x);

    float scalar = 1.0 * maxDistort;
//    float4 colourScalar = float4(2.0, 1.5, 1.0, 1.0);
    float4 colourScalar = float4(700.0, 560.0, 490.0, 1.0);	// Based on the true wavelengths of red, green, blue light.
    colourScalar /= max(max(colourScalar.x, colourScalar.y), colourScalar.z);
    colourScalar *= 2.0;

    colourScalar *= scalar;

    const float numTaps = 24.0; // Original value: 8

    float3 fragColor = 0.0;
    for( float tap = 0.0; tap < numTaps; tap += 1.0 )
    {
        fragColor.r += TextureColor.Sample(Sampler1, brownConradyDistortion(uv, colourScalar.r)).r;
        fragColor.g += TextureColor.Sample(Sampler1, brownConradyDistortion(uv, colourScalar.g)).g;
        fragColor.b += TextureColor.Sample(Sampler1, brownConradyDistortion(uv, colourScalar.b)).b;

        colourScalar *= 0.95;
    }

    fragColor /= numTaps;

    return fragColor;
}
#endif

//------------------------FISHEYE----------------------//
// by Gilcher Pascal aka Marty McFly                   //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//

//kingeric1992:  modified to ouput distortion coordnate only.
void FishEyePass(float2 texcoord, out float2 rCoords, out float2 gCoords, out float2 bCoords)
{
	float4 coord=0.0;
	coord.xy=texcoord.xy;
	coord.w=0.0;

	float4 color = 0.0.xxxx;

	float3 eta = float3(1.0+fFisheyeColorshift*0.9,1.0+fFisheyeColorshift*0.6,1.0+fFisheyeColorshift*0.3);
	float2 center;
	center.x = coord.x-0.5;
	center.y = coord.y-0.5;
	float LensZoom = 1.0/fFisheyeZoom;

	float r2 = (texcoord.x-0.5) * (texcoord.x-0.5) + (texcoord.y-0.5) * (texcoord.y-0.5);
	float f = 0;

		f = 1 + r2 * fFisheyeDistortion;

//	float x = f*LensZoom*(coord.x-0.5)+0.5;
//	float y = f*LensZoom*(coord.y-0.5)+0.5;


  rCoords = (f*eta.r)*LensZoom*(center.xy*0.5)+0.5;
	gCoords = (f*eta.g)*LensZoom*(center.xy*0.5)+0.5;
	bCoords = (f*eta.b)*LensZoom*(center.xy*0.5)+0.5;


//	color.x = tex2D(RFX_backbufferColor,rCoords).r;
//	color.y = tex2D(RFX_backbufferColor,gCoords).g;
//	color.z = tex2D(RFX_backbufferColor,bCoords).b;

//	return color;
}
