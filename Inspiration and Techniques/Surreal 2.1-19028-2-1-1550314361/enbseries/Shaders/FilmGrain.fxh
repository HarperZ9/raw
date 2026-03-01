// ADVANCED FILM GRAIN SHADER //////////////////////
////////////////////////////////////////////////////
// Written by MartyMcFly ~ Pascal Gilcher         //
//                                                //
// Simplex funtion by Nikita Miropolskiy          //
//                                                //
// Film Grain profile integration by TreyM        //
////////////////////////////////////////////////////

// ENABLE CUSTOM GRAIN PROFILE UI ////////////////
    #define FULL_CONTROLS 0

// UI ////////////////////////////////////////////

    #if(!FULL_CONTROLS)

    UI_FLOAT_DNI(hueMid,               "Hue",                      0.0, 1.0, 0.5)
        uniform bool GRAIN_STRUCTURE <
            ui_label   = "Simulate Film Grain Structure";
        > = true;

        uniform int GRAIN_PROFILE <
        	ui_type    = "combo";
            ui_label   = "Grain Profile";
            ui_items   = "Modern 35mm Film\0Vintage 35mm Film\0Coarse Grain\0Medium Grain\0Fine Grain\0Vintage 8mm Film\0Digital ISO\0";
        	ui_min     = 0; ui_max = 6;
        > = 0;

        #define GRAIN_STRUCTURE_FLOAT 0.1
        #define GRAIN_AMOUNT_RGB float3(0.1, 0.1, 0.1)
        #define GRAIN_SIZE_RGB float3(0.1, 0.1, 0.1)
        #define GRAIN_CURVE_LUMA float3(0.1, 0.1, 0.1)
        //#define SHADOW_CUTOFF 0
    #else
        #define GRAIN_PROFILE 7
    #endif

    uniform int INTENSITY <
    	ui_type  = "drag";
        ui_label = "Grain Amount";
    	ui_min   = 0; ui_max = 100;
    > = 50;

    uniform int GRAIN_COLOR <
    	ui_type  = "drag";
        ui_label = "Grain Color Saturation";
    	ui_min   = 0; ui_max = 200;
    > = 100;

    /*
    uniform bool PROTECT_BLACK <
        ui_label   = "Protect Black Level";
    > = false;
    */

    #if(FULL_CONTROLS)
        #define GRAIN_STRUCTURE TRUE

        uniform float GRAIN_STRUCTURE_FLOAT <
            ui_type    = "drag";
            ui_label   = "Simulated Film Grain Structure";
            ui_tooltip = "Red, Green, Blue";
            ui_min = 0.0; ui_max = 1.0;
        > = 0.25;

        uniform float3 GRAIN_AMOUNT_RGB <
            ui_type    = "drag";
            ui_label   = "Grain Amount Per RGB Channel";
            ui_tooltip = "Red, Green, Blue";
        > = float3(0.333, 0.333, 0.333);

        uniform float3 GRAIN_SIZE_RGB <
            ui_type    = "drag";
            ui_label   = "Grain Size Per RGB Channel";
            ui_tooltip = "Red, Green, Blue";
        > = float3(0.2, 0.2, 0.2);

        uniform float3 GRAIN_CURVE_LUMA <
            ui_type    = "drag";
            ui_label   = "Grain Luma Response Curve";
            ui_tooltip = "Shadows, Midtones, Highlights";
        > = float3(0.333, 0.333, 0.333);

        /*
        uniform int SHADOW_CUTOFF <
            ui_type = "drag";
            ui_label = "Shadow Response Cuttoff";
            ui_min = 0; ui_max = 3;
        > = 0;
        */
    #endif


    #define Timer_Multiplier 0.01

// VERTEX SHADER /////////////////////////////////
    struct VSOUT
    {
    	float4   vpos        		: SV_Position;
        float2   uv          		: TEXCOORD0;
    };

    VSOUT VS_FilmGrain(in uint id : SV_VertexID)
    {
        VSOUT o;
        o.uv.x = (id == 2) ? 2.0 : 0.0;
        o.uv.y = (id == 1) ? 2.0 : 0.0;
        o.vpos = float4(o.uv.xy * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);

        return o;
    }

// FUNCTIONS /////////////////////////////////////
    // Discontinuous Pseudorandom Generator //////
    float3 random3(float3 c)
    {
    	float j = 4096.0*sin(dot(c,float3(17.0, 59.4, 15.0)));
    	float3 r;
    	r.z = frac(512.0*j);
    	j *= .125;
    	r.x = frac(512.0*j);
    	j *= .125;
    	r.y = frac(512.0*j);
    	return r-0.5;
    }

    // 3D Simplex Noise //////////////////////////
    float simplex3d(float3 p)
    {
    	/* skew constants for 3d simplex functions */
    	static const float F3 =  0.3333333;
    	static const float G3 =  0.1666667;

    	 /* 1. find current tetrahedron T and it's four vertices */
    	 /* s, s+i1, s+i2, s+1.0 - absolute skewed (integer) coordinates of T vertices */
    	 /* x, x1, x2, x3 - unskewed coordinates of p relative to each of T vertices*/

    	 /* calculate s and x */
    	 float3 s = floor(p + dot(p, F3));
    	 float3 x = p - s + dot(s, G3);

    	 /* calculate i1 and i2 */
    	 float3 e = step(0.0, x - x.yzx);
    	 float3 i1 = e - e * e.zxy; //e * (1.0 - e.zxy);
    	 float3 i2 = 1.0 - e.zxy + e * e.zxy;//1.0 - e.zxy*(1.0 - e);

    	 /* x1, x2, x3 */
    	 float3 x1 = x - i1 + G3;
    	 float3 x2 = x - i2 + 2.0*G3;
    	 float3 x3 = x - 1.0 + 3.0*G3;

    	 /* 2. find four surflets and store them in d */
    	 float4 w, d;

    	 /* calculate surflet weights */
    	 w.x = dot(x, x);
    	 w.y = dot(x1, x1);
    	 w.z = dot(x2, x2);
    	 w.w = dot(x3, x3);

    	 /* w fades from 0.6 at the center of the surflet to 0.0 at the margin */
    	 w = saturate(0.6 - w);

    	 /* calculate surflet components */
    	 d.x = dot(random3(s), x);
    	 d.y = dot(random3(s + i1), x1);
    	 d.z = dot(random3(s + i2), x2);
    	 d.w = dot(random3(s + 1.0), x3);

    	 /* multiply d by w^4 */
    	 w *= w;
    	 w *= w;
    	 d *= w;

    	 /* 3. return the sum of the four surflets */
    	 return dot(d, 52.0);
    }

    // Film Grain ////////////////////////////////
    float3 McFlyNoise(in float2 vpos, in float3 color)
    {
        float3 grey = 0.5;
        float3 simplex_RGB;

        // Intensity Multiplier
        float ProfileIntensity[8] =
        {
            0.33,
            0.1,
            0.15,
            0.15,
            0.2,
            0.33,
            0.05,

            // Full UI Controls
            0.25
        };

        // Grain Structue Effect
        float ProfileStructure[8] =
        {
            0.3,
            0.36,
            0.36,
            0.34,
            0.27,
            0.33,
            0.0,

            // Full UI Controls
            GRAIN_STRUCTURE_FLOAT
        };

        // Grain Amount for RGB Channels
        float3 ProfileAmountRGB[8] =
        {
            // Red, Green, Blue
            float3(0.11, 0.13, 0.27),
            float3(0.135, 0.175, 0.27),
            float3(0.11, 0.22, 0.25),
            float3(0.15, 0.2, 0.25),
            float3(0.27, 0.25, 0.22),
            float3(0.333, 0.175, 0.111),
            float3(0.444, 0.175, 0.444),

            // Full UI Controls
            GRAIN_AMOUNT_RGB
        };

        // Grain Size for RGB Channels
        float3 ProfileSizeRGB[8] =
        {
            // Red, Green, Blue
            float3(0.15, 0.2, 0.28),
            float3(0.2, 0.25, 0.33),
            float3(0.25, 0.22, 0.3),
            float3(0.18, 0.2, 0.3),
            float3(0.1, 0.15, 0.2),
            float3(0.5, 0.4, 0.333),
            float3(0.0125, 0.0125, 0.0125),

            // Full UI Controls
            GRAIN_SIZE_RGB
        };

        // Grain Response Curve for
        // Lows, Mids, and Highs
        float3 ProfileCurveLuma[8] =
        {
            // Lows, Mids, Highs
            float3(0.66, 0.33, 0.22),
            float3(0.77, 0.4, 0.22),
            float3(0.7, 0.5, 0.77),
            float3(0.66, 0.28, 0.5),
            float3(0.4, 0.5, 0.5),
            float3(0.6, 0.75, 0.6),
            float3(0.75, 0.33, 0.15),

            // Full UI Controls
            GRAIN_CURVE_LUMA
        };

        // Grain Saturation Multiplier
        float ProfileSaturation[8] =
        {
            0.9,
            0.9,
            0.9,
            0.9,
            0.9,
            0.8,
            1.0,

            // Full UI Controls
            1.0
        };

        // Grain Generation for RGB Channels /////
    	simplex_RGB.x = simplex3d(float3(vpos * 0.5 * (1 - ProfileSizeRGB[GRAIN_PROFILE].x) + 111, (Timer.x * Timer_Multiplier) % 4096));
    	simplex_RGB.y = simplex3d(float3(vpos * 0.5 * (1 - ProfileSizeRGB[GRAIN_PROFILE].y) + 222, (Timer.x * Timer_Multiplier) % 4096));
    	simplex_RGB.z = simplex3d(float3(vpos * 0.5 * (1 - ProfileSizeRGB[GRAIN_PROFILE].z) + 333, (Timer.x * Timer_Multiplier) % 4096));

    	float2 simplex_Modulate;
    	simplex_Modulate.x = simplex3d(float3(vpos * 1.1 + 111, (Timer.x * Timer_Multiplier) % 4096));
    	simplex_Modulate.y = simplex3d(float3(vpos.yx * 1.1 + 111, (Timer.x * Timer_Multiplier) % 4096));

        float3 blurData[8] =
        {
                float3( 0.5, 1.5,1.50),
                float3( 1.5,-0.5,1.50),
                float3(-0.5,-1.5,1.50),
                float3(-1.5, 0.5,1.50),
                float3( 2.5, 1.5,1.00),
                float3( 1.5,-2.5,1.00),
                float3(-2.5,-1.5,1.00),
                float3(-1.5, 2.5,1.00)
        };

        // Grain Structure Effect Mask Generation
        float4 blurred = 0.0;
        if(GRAIN_STRUCTURE || !GRAIN_PROFILE == 5)
        {
            for(int i=0; i<8; i++)
        	blurred += float4(tex2D(qUINT::sBackBufferTex, (vpos + blurData[i].xy + simplex_Modulate * 4) * qUINT::PIXEL_SIZE).rgb, 1) * blurData[i].z;
        }
        blurred.rgb /= blurred.w;
    	float3 diff = sqrt(abs(blurred.rgb - color));
        diff = pow(diff, 1.66);

        // Grain Intensity ///////////////////////
        simplex_RGB = clamp(simplex_RGB * ((INTENSITY * 0.75) * ProfileIntensity[GRAIN_PROFILE]), -1, 1);

        // Apply Grain Structure Effect //////////
    	if(GRAIN_STRUCTURE || !GRAIN_PROFILE == 5) simplex_RGB *= 1.0 + diff * 33.0 * ProfileStructure[GRAIN_PROFILE];

        // Grain Saturation Control //////////////
        simplex_RGB = lerp(dot(simplex_RGB, float3(0.2126, 0.7152, 0.0722)), simplex_RGB, (GRAIN_COLOR * 0.01) * ProfileSaturation[GRAIN_PROFILE]);

        // Separate Image Into Luma Ranges ///////
    	float3 mask;
        float luma = dot(color, 0.3333); luma *= luma;
    	mask.x = smoothstep(0.5, 0.0, luma); // Shadows
    	mask.y = smoothstep(0.5, 1.0, luma); // Highlights
    	mask.z = saturate(1 - mask.x - mask.y); // Midtones

        //mask.w = saturate(smoothstep(0.0022, 0.0044, pow(luma, 0.9)) + 0.2 - (SHADOW_CUTOFF * 0.2)); // Optional Shadow Protection

        // Apply Grain to 50% Grey for Blending //
    	//if(PROTECT_BLACK || SHADOW_CUTOFF > 0.0) grey = lerp(grey, grey + simplex_RGB * dot(mask.xzy, ProfileCurveLuma[GRAIN_PROFILE]) * (ProfileAmountRGB[GRAIN_PROFILE] * (INTENSITY * 0.0085)), mask.w);
        grey += simplex_RGB * dot(mask.xzy, ProfileCurveLuma[GRAIN_PROFILE]) * (ProfileAmountRGB[GRAIN_PROFILE] * (INTENSITY * 0.0085));

        // Photoshop Style Soft Light Blending ///
        color = (grey < 0.5) ? (2.0 * color * grey + color * color * (1.0 - 2.0 * grey)) : (sqrt(color) * (2.0 * grey - 1.0) + 2.0 * color * (1.0 - grey));

        return color;
    }

// PIXEL SHADER //////////////////////////////////
    void PS_Apply(in VSOUT i, out float4 o : SV_Target0)
    {
    	o = tex2D(qUINT::sBackBufferTex, i.uv);

    	o.rgb = McFlyNoise(i.vpos.xy, o.rgb);
    }