////////////////////////////////////////////////////
//        ::::::::        ::::::::::        :::   //
//      :+:    :+:       :+:               :+:    //
//     +:+              +:+               +:+     //
//    +#+              :#::+::#          +#+      //
//   +#+              +#+               +#+       //
//  #+#    #+#       #+#               #+#        //
//  ########        ###               ##########  //
//                                                //
//           CINEMATIC FILM LOOKS II              //
//        CREATED BY TREYM FOR SKYRIM SE          //
////////////////////////////////////////////////////


// MACROS //////////////////////////////////////////
    #include "Include/Internals/Macros.fxh"


// ENB SETUP ///////////////////////////////////////
    #include "Include/Internals/ToneMap.fxh"


// UI //////////////////////////////////////////////
    #include "Include/UI/UI_ToneMap.fxh"


// FUNCTIONS ///////////////////////////////////////
    #include "Include/Functions/enbeffect/AGCC.fxh"
    #include "Include/Functions/enbeffect/PP2.fxh"
    #include "Include/Functions/generic/LOG.fxh"


// PIXEL SHADERS ///////////////////////////////////
    float4	PS_Draw(VS_OUTPUT_POST IN) : SV_Target
    {
        float4	res;
        float4	color;

        color = TextureColor.Sample(Sampler0, IN.txcoord0.xy); //hdr scene color

        // ENB Lens
        float3	lens;
        lens.xyz = TextureLens.Sample(Sampler1, IN.txcoord0.xy).xyz;
        color.xyz += lens.xyz * ENBParams01.y; //lens amount

        // Gaussian Bloom and Lens Dirt
    	float3	bloom = TextureBloom.Sample(Sampler1, IN.txcoord0.xy);
    	float3 dirt = tDirt.Sample(Sampler1, IN.txcoord0.xy).rgb;
    	// if(ENABLE_DIRT) bloom = lerp(bloom, pow(dirt, 1.2), saturate(pow(bloom, 1.0) * 12));
        color.xyz += bloom * ENBParams01.x; //bloom amount

        // Adaptation
        float4 eadapt = TextureAdaptation.Sample(Sampler0, IN.txcoord0.xy);

        // Apply Game Color Correction
        color = AGCC(color, IN.txcoord0.xy);

        // Boris' Post Process 2
        color.rgb = PP2(color.rgb, eadapt.xyz);

        // Gamma Adjustment
        color = pow(color, 1.2);

        // Convert to LOG
        color.rgb = Lin2Log(color.rgb);
        color = saturate(pow(color, 1.0) * 1.3);

        res.xyz = saturate(color);
        res.w = 1.0;
        return res;
    }

// TECHNIQUES //////////////////////////////////////
    TECHNIQUE_UI(Draw, "CFL II - LOG",
        PASS(p0, VS_Draw, PS_Draw)
    )
