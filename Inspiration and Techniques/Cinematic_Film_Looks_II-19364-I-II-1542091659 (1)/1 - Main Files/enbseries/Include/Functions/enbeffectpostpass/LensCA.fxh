// CHROMATIC ABERRATION ////////////////////////////
////////////////////////////////////////////////////
// Copyright (c) 2018 Jacob Maximilian Fober      //
//                                                //
// This work is licensed under the Creative       //
// Commons Attribution-ShareAlike 4.0             //
// International License. To view a copy of this  //
// license, visit:                                //
// http://creativecommons.org/licenses/by-sa/4.0  //
////////////////////////////////////////////////////


// SAMPLER ///////////////////////////////////////
    SAMPLER(SamplerMirror, LINEAR, Mirror)

// FUNCTIONS /////////////////////////////////////
    float Formula(float SqrTanFOVq, float2 Coordinates)
    {
    	float Result = 1.0 - SqrTanFOVq;
    	Result /= 1.0 - SqrTanFOVq * (Coordinates.x * Coordinates.x + (Coordinates.y * Coordinates.y));
    	return Result;
    }

    float3 Spectrum(float Hue)
    {
    	float Hue4 = Hue * 4.0;
    	float3 HueColor = abs(Hue4 - float3(1.0, 2.0, 1.0));
    	HueColor = saturate(1.5 - HueColor);
    	HueColor.xz += saturate(Hue4 - 3.5);
    	HueColor.z = 1.0 - HueColor.z;
    	return HueColor;
    }

// PIXEL SHADERS /////////////////////////////////

    // FISHEYE LENS DISTORTION BY MARTY MCFLY ////
    float4 PS_Dist(float4 vpos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target0
    {
        float4 coord=0.0;
        coord.xy=texcoord.xy;
        coord.w=0.0;

        float4 color;
        if(!ENABLE_DISTORTION || !ENABLE_LENS)
        {
            color = TextureColor.Sample(Sampler0, texcoord);
            return color;
        }

        float2 center;
        center.x = coord.x-0.5;
        center.y = coord.y-0.5;
        //#define fFisheyeZoom lerp(0.5, 0.5 + LENS_DIST * (LENS_DIST > 0.0 ? 0.00076 : 0.00019), ENABLE_DISTORTION)
        float LensZoom = 1.0/lerp(0.5, 0.5 + LENS_DIST * (LENS_DIST > 0.0 ? 0.00076 : 0.00019), ENABLE_DISTORTION);

        float r2 = (texcoord.x-0.5) * (texcoord.x-0.5) + (texcoord.y-0.5) * (texcoord.y-0.5);
        float f = 0;

        //#define fFisheyeDistortion lerp(LENS_CA * 0.0005, (LENS_CA * 0.0005) + LENS_DIST * (LENS_DIST > 0.0 ? 0.003 : 0.0015), LENS_DIST == 0 ? 0.0 : 1.0)
        f = 1 + r2 * lerp(LENS_CA * 0.0005, (LENS_CA * 0.0005) + LENS_DIST * (LENS_DIST > 0.0 ? 0.003 : 0.0015), LENS_DIST == 0 ? 0.0 : 1.0);

        float x = f*LensZoom*(coord.x-0.5)+0.5;
        float y = f*LensZoom*(coord.y-0.5)+0.5;
        float2 Coords = f*LensZoom*(center.xy*0.5)+0.5;

        color = TextureColor.Sample(Sampler1,Coords);

        return color;
    }

    // CHROMATIC ABERRATION //////////////////////
    void PS_CA(VS_OUTPUT_POST IN, float4 v0 : SV_Position, float2 texcoord : TEXCOORD0,
    out float3 BluredImage : SV_Target)
    {
        if(LENS_CA == 0 || !ENABLE_DISTORTION || !ENABLE_LENS) BluredImage = TextureColor.Sample(Sampler0, texcoord).rgb;
        else
        {
            // Grab Aspect Ratio
            float Aspect = ScreenSize.z;//ScreenSize.z;
            // Grab Pixel V size
            float Pixel = ScreenSize.y;

            int CAValue;

            CAValue = (LENS_CA * 0.5);

            // Adjust number of samples
            int Samples = max(6, 2 * ceil(abs(CAValue) * 0.5) + 2);

            // Clamp maximum sample count
            Samples = min(Samples, 48);

            // Convert UVs to radial coordinates with correct Aspect Ratio
            float2 RadialCoord = texcoord * 2.0 - 1.0;
            RadialCoord.x *= Aspect;

            // Generate radial mask from center (0) to the corner of the screen (1)
            float Mask = pow(length(RadialCoord) * rsqrt(Aspect * Aspect + 1.0), lerp(1.0, 0.25, abs(LENS_DIST * 0.0066)));

            float OffsetBase = Mask * CAValue * Pixel * 2.0;

            // Each loop represents one pass
            if (abs(OffsetBase) < Pixel)
            {
                BluredImage = TextureColor.Sample(SamplerMirror, texcoord).rgb;
            }
            else
            {
                for (int P = 0; P < Samples && P <= 48; P++)
                {
                    // Calculate current sample
                    float CurrentSample = float(P) / float(Samples);

                    float Offset = OffsetBase * CurrentSample + 1.0;

                    // Scale UVs at center
                    float2 Position = RadialCoord / Offset;
                    // Convert aspect ratio back to square
                    Position.x /= Aspect;
                    // Convert radial coordinates to UV
                    Position = Position * 0.5 + 0.5;

                    // Multiply texture sample by HUE color
                    BluredImage += Spectrum(CurrentSample) * TextureColor.SampleLevel(SamplerMirror,  Position, 0).rgb;
                }
                BluredImage = BluredImage / Samples * 2.0;
            }
        }
    }
