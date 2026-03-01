// EXTERNAL PARAMETERS /////////////////////////////
    float4	Timer;
    float4	ScreenSize;
    float	AdaptiveQuality;
    float4	Weather;
    float4	TimeOfDay1;
    float4	TimeOfDay2;
    float	ENightDayFactor;
    float	EInteriorFactor;


// DEBUGGING ///////////////////////////////////////
    float4	tempF1;
    float4	tempF2;
    float4	tempF3;
    float4	tempInfo1;
    float4	tempInfo2;


// MOD AND GAME PARAMETERS /////////////////////////
    float4				Params01[7];
    float4				ENBParams01;
    Texture2D			TextureColor;
    Texture2D			TextureBloom;
    Texture2D			TextureLens;
    Texture2D			TextureDepth;
    Texture2D			TextureAdaptation;
    Texture2D			TextureAperture;
    TEXTURE(tDirt, "Include/Textures/Lens/Dirt.png")


// SAMPLERS ////////////////////////////////////////
    SAMPLER(Sampler0, POINT, Clamp)
    SAMPLER(Sampler1, LINEAR, Clamp)


// STRUCTS /////////////////////////////////////////
    struct VS_INPUT_POST {
    	float3 pos		: POSITION;
    	float2 txcoord	: TEXCOORD0;
    };
    struct VS_OUTPUT_POST {
    	float4 pos		: SV_POSITION;
    	float2 txcoord0	: TEXCOORD0;
    };


// VERTEX SHADER ///////////////////////////////////
    VS_OUTPUT_POST	VS_Draw(VS_INPUT_POST IN) {
    	VS_OUTPUT_POST	OUT;
    	float4	pos;
    	pos.xyz=IN.pos.xyz;
    	pos.w=1.0;
    	OUT.pos=pos;
    	OUT.txcoord0.xy=IN.txcoord.xy;
    	return OUT;
    }
