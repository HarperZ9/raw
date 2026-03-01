//////////////////////////////////////////////////
// ENB INTERNALS FOR ENBEFFECTPOSTPASS.FX       //
//////////////////////////////////////////////////


// EXTERNAL PARAMETERS ///////////////////////////
    //x = generic timer in range 0..1, period of 16777216 ms (4.6 hours), y = average fps, w = frame time elapsed (in seconds)
    float4	Timer;
    //x = Width, y = 1/Width, z = aspect, w = 1/aspect, aspect is Width/Height
    float4	ScreenSize;
    //changes in range 0..1, 0 means full quality, 1 lowest dynamic quality (0.33, 0.66 are limits for quality levels)
    float	AdaptiveQuality;
    //x = current weather index, y = outgoing weather index, z = weather transition, w = time of the day in 24 standart hours. Weather index is value from weather ini file, for example WEATHER002 means index==2, but index==0 means that weather not captured.
    float4	Weather;
    //x = dawn, y = sunrise, z = day, w = sunset. Interpolators range from 0..1
    float4	TimeOfDay1;
    //x = dusk, y = night. Interpolators range from 0..1
    float4	TimeOfDay2;
    //changes in range 0..1, 0 means that night time, 1 - day time
    float	ENightDayFactor;
    //changes 0 or 1. 0 means that exterior, 1 - interior
    float	EInteriorFactor;


// DEBUGGING PARAMETERS //////////////////////////
    //keyboard controlled temporary variables. Press and hold key 1,2,3...8 together with PageUp or PageDown to modify. By default all set to 1.0
    float4	tempF1; //0,1,2,3
    float4	tempF2; //5,6,7,8
    float4	tempF3; //9,0
    // xy = cursor position in range 0..1 of screen;
    // z = is shader editor window active;
    // w = mouse buttons with values 0..7 as follows:
    //    0 = none
    //    1 = left
    //    2 = right
    //    3 = left+right
    //    4 = middle
    //    5 = left+middle
    //    6 = right+middle
    //    7 = left+right+middle (or rather cat is sitting on your mouse)
    float4	tempInfo1;
    // xy = cursor position of previous left mouse button click
    // zw = cursor position of previous right mouse button click
    float4	tempInfo2;



// MOD PARAMETERS ////////////////////////////////
    Texture2D			TextureOriginal; //color R10B10G10A2 32 bit ldr format
    Texture2D			TextureColor; //color which is output of previous technique (except when drawed to temporary render target), R10B10G10A2 32 bit ldr format
    Texture2D			TextureDepth; //scene depth R32F 32 bit hdr format
    Texture2D			RenderTargetRGBA32; //R8G8B8A8 32 bit ldr format
    Texture2D			RenderTargetRGBA64; //R16B16G16A16 64 bit ldr format
    Texture2D			RenderTargetRGBA64F; //R16B16G16A16F 64 bit hdr format
    Texture2D			RenderTargetR16F; //R16F 16 bit hdr format with red channel only
    Texture2D			RenderTargetR32F; //R32F 32 bit hdr format with red channel only
    Texture2D			RenderTargetRGB32F; //32 bit hdr format without alpha


// SAMPLERS //////////////////////////////////////
    SAMPLER(Sampler0, POINT, Clamp)
    SAMPLER(Sampler1, LINEAR, Clamp)


// STRUCTURE /////////////////////////////////////
    struct VS_INPUT_POST
    {
    	float3 pos		: POSITION;
    	float2 txcoord	: TEXCOORD0;
    };
    struct VS_OUTPUT_POST
    {
    	float4 pos		: SV_POSITION;
    	float2 txcoord0	: TEXCOORD0;
    };


// VERTEX SHADER /////////////////////////////////
    VS_OUTPUT_POST	VS_PostProcess(VS_INPUT_POST IN)
    {
    	VS_OUTPUT_POST	OUT;
    	float4	pos;
    	pos.xyz=IN.pos.xyz;
    	pos.w=1.0;
    	OUT.pos=pos;
    	OUT.txcoord0.xy=IN.txcoord.xy;
    	return OUT;
    }
