//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Fast seperated gaussian bloom
// Potential future improvements:
//    Moving offsets/weights to vertex shader?
//    Not using a switch in the final pass... done?
// Source: https://www.shadertoy.com/view/lstSRS
// Original author: SonicEther
// Additional credits are below near the relevant code.
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#include "Include/Internals/Macros.fxh"

// Options...
// Bloom tinting by pass. Provides a cool violet shift effect.
UI_QUALITY(BLOOM_AMOUNT, "Bloom Strength", 0, 2, 1)
#define GaussianBloomColorEffect 0
//UI_FLOAT(fContrast, "Contrast", 0.0, 2.0, 0.85)
#define fContrast lerp(0.875, 1.0, (BLOOM_AMOUNT * 0.33))
#define ECCInBlack 0.7
#define ECCInWhite lerp(0.825, 0.9, (BLOOM_AMOUNT * 0.33))
#define ECCOutBlack 0.0
#define ECCOutWhite 1.0
#define fSaturation float4(0.0, 0.0, 0.0, 0.0)
//UI_FLOAT(post_mixer_bloomShape, "Shape", 0.0, 10.0, 4.0)
#define post_mixer_bloomShape lerp(2.0, 5.0, (BLOOM_AMOUNT * 0.33))
#define post_mixer_bloomColor float3(1.0, 1.0, 1.0)

/*
float	fContrast <
	string UIName="Contrast";
	string UIWidget="spinner";
	float UIMin=0.0;
	float UIMax=1000.0;
> = {1.0};

float	ECCInBlack <
	string UIName="CC: In black";
	string UIWidget="Spinner";
	float UIMin=0.0;
	float UIMax=5.0;
> = {0.0};

float	ECCInWhite <
	string UIName="CC: In white";
	string UIWidget="Spinner";
	float UIMin=0.0;
	float UIMax=500.0;
> = {1.0};

float	ECCOutBlack <
	string UIName="CC: Out black";
	string UIWidget="Spinner";
	float UIMin=0.0;
	float UIMax=1.0;
> = {0.0};

float	ECCOutWhite <
	string UIName="CC: Out white";
	string UIWidget="Spinner";
	float UIMin=0.0;
	float UIMax=1.0;
> = {1.0};

float4	fSaturation <
	string UIName="CC: Saturation";
	string UIWidget="Spinner";
	float UIMin=0.0;
	float UIMax=5.0;
> = {1.0, 1.0, 1.0, 1.0};

float post_mixer_bloomShape <
  string UIName="Gaussian: Bloom Shape";
  string UIWidget="Spinner";
  float UIMin=0.0;
  float UIMax=32.0;
  float UIStep=0.01;
> = {1.0};

#ifdef GaussianBloomColorEffect
float3 post_mixer_bloomColor <
  string UIName="Gaussian: Bloom Color Tint Amount";
  string UIWidget="Color";
> = {1.0, 1.0, 1.0};
#endif
*/

// FUNCTIONS ///////////////////////////////////////
    float3 ColorFetch(Texture2D inputtex, float2 coord) {
     	return inputtex.Sample(Sampler1, coord).rgb;
    }

    //Horizontal gaussian blur leveraging hardware filtering for fewer texture lookups.
    float3  FuncHoriBlur(Texture2D inputtex, float2 uvsrc, float2 pxSize, float Iteration) {
        float weights[5];
        float offsets[5];

        weights[0] = 2.0;
        weights[1] = 1.67;
        weights[2] = 0.8;
        weights[3] = 0.23;
        weights[4] = 0.09;

        offsets[0] = 0.0;
        offsets[1] = 1.0;
        offsets[2] = 2.0;
        offsets[3] = 3.0;
        offsets[4] = 4.0;

        float2 uv = uvsrc; //fragCoord.xy / iResolution.xy;

        float3 color = 0.0;
        float weightSum = 0.0;

        color += ColorFetch(inputtex, uv) * weights[0];
        weightSum += weights[0];

        for(int i = 1; i < 5; i++) {
            float2 offset = offsets[i] * pxSize;
            color += ColorFetch(inputtex, uv + offset * float2(0.6, 0.0)) * weights[i];
            color += ColorFetch(inputtex, uv - offset * float2(0.6, 0.0)) * weights[i];
            weightSum += weights[i] * 2.0;
        }

        color /= weightSum;

        return color;
    }

    //Vertical gaussian blur leveraging hardware filtering for fewer texture lookups.
    float3  FuncVertBlur(Texture2D inputtex, float2 uvsrc, float2 pxSize, float Iteration) {
        float weights[5];
        float offsets[5];

        weights[0] = 2.0;
        weights[1] = 1.67;
        weights[2] = 0.8;
        weights[3] = 0.23;
        weights[4] = 0.09;

        offsets[0] = 0.0;
        offsets[1] = 1.0;
        offsets[2] = 2.0;
        offsets[3] = 3.0;
        offsets[4] = 4.0;

        float2 uv = uvsrc; //fragCoord.xy / iResolution.xy;

        float3 color = 0.0;
        float weightSum = 0.0;

        color += ColorFetch(inputtex, uv) * weights[0];
        weightSum += weights[0];

        for(int i = 1; i < 5; i++) {
            float2 offset = offsets[i] * pxSize;
            color += ColorFetch(inputtex, uv + offset * float2(0.0, 0.6)) * weights[i];
            color += ColorFetch(inputtex, uv - offset * float2(0.0, 0.6)) * weights[i];
            weightSum += weights[i] * 2.0;
        }

        color /= weightSum;

        return color;
    }

// SHADERS /////////////////////////////////////////
    float4  PS_GaussHResize(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
    uniform Texture2D inputtex, uniform float texsize, uniform float Iteration) : SV_Target {
        float4  res;

        float2 pxSize = (1/(texsize))*float2(1, ScreenSize.z);
        res.xyz=FuncHoriBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration);

        #if 1 // Double blur. This removes artifacing in the raw bloom texture.
            // However, it costs more than ENB's bloom and does like 60 passes.
            // The aliasing is only noticeable at 100% bloom anyway.
            res.xyz+=FuncHoriBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration+1);
            res.xyz/=2;
        #endif

        res=max(res, 0.0);
        res=min(res, 16384.0);

        res.w=1.0;
        return res;
    }

    float4  PS_GaussVResize(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
    uniform Texture2D inputtex, uniform float texsize, uniform float Iteration) : SV_Target {
        float4  res;

        float2 pxSize = (1/(texsize))*float2(1, ScreenSize.z);
        res.xyz=FuncVertBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration);

        #if 1 // Double blur. This removes artifacing in the raw bloom texture.
        // However, it costs more than ENB's bloom and does like 60 passes.
        // The aliasing is only noticeable at 100% bloom anyway.
        res.xyz+=FuncVertBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration+1);
        res.xyz/=2;
        #endif

        res=max(res, 0.0);
        res=min(res, 16384.0);

        res.w=1.0;
        return res;
    }

    float4  PS_GaussHResizeFirst(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
    uniform Texture2D inputtex, uniform float texsize, uniform float Iteration) : SV_Target {
        float4  res;

        float2 pxSize = (1/(texsize))*float2(1, ScreenSize.z);
        res.xyz=FuncHoriBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration);

        #if 1 // Double blur. This removes artifacing in the raw bloom texture.
            // However, it costs more than ENB's bloom and does like 60 passes.
            // The aliasing is only noticeable at 100% bloom anyway.
            res.xyz+=FuncHoriBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration+1);
            res.xyz/=2;
        #endif

        res.xyz=max(res.xyz-(ECCInBlack*0.1), 0.0) / max(ECCInWhite-(ECCInBlack*0.1), 0.00001);
        if (fContrast!=1.0) res.xyz=pow(res.xyz, fContrast);
        res.xyz=res.xyz*(ECCOutWhite-ECCOutBlack) + ECCOutBlack;

        res=max(res, 0.0);
        res=min(res, 16384.0);

        res.w=1.0;
        return res;
    }

    float4  PS_GaussVResizeFirst(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
    uniform Texture2D inputtex, uniform float texsize, uniform float Iteration) : SV_Target {
        float4  res;

        float2 pxSize = (1/(texsize))*float2(1, ScreenSize.z);
        res.xyz=FuncVertBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration);

        #if 1 // Double blur. This removes artifacing in the raw bloom texture.
            // However, it costs more than ENB's bloom and does like 60 passes.
            // The aliasing is only noticeable at 100% bloom anyway.
            res.xyz+=FuncVertBlur(inputtex, IN.txcoord0.xy, pxSize*2.0, Iteration+1);
            res.xyz/=2;
        #endif

        res.xyz=max(res.xyz-(ECCInBlack*0.1), 0.0) / max(ECCInWhite-(ECCInBlack*0.1), 0.00001);
        if (fContrast!=1.0) res.xyz=pow(res.xyz, fContrast);
        res.xyz=res.xyz*(ECCOutWhite-ECCOutBlack) + ECCOutBlack;

        res=max(res, 0.0);
        res=min(res, 16384.0);

        res.w=1.0;
        return res;
    }


    float4  PS_GaussMix(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target {
        float4  res = 0.0;

        // Mercury bloom blending code
        // Source: https://imgur.com/a/MZD3l
        // This is kind of messy... sorry!
        float weightSum = 0;
        int maxlevel = 5;
        #define TAU 6.28318

        // This should get optimised by the compiler.
        float weight[6];
        float x[6];

        [unroll]
        for (int i=0; i <= maxlevel; i++) {
            weight[i] = pow(i+1, post_mixer_bloomShape);
            weightSum += weight[i];
            x[i] = i*2;
        }

        if (GaussianBloomColorEffect) {
            res.xyz += ColorFetch(RenderTarget1024, IN.txcoord0.xy) * weight[0] * (1 + post_mixer_bloomColor*float3(sin(x[0]), sin(x[0]+TAU/3), sin(x[0]-TAU/3)));
            res.xyz += ColorFetch(RenderTarget512, IN.txcoord0.xy)  * weight[1] * (1 + post_mixer_bloomColor*float3(sin(x[1]), sin(x[1]+TAU/3), sin(x[1]-TAU/3)));
            res.xyz += ColorFetch(RenderTarget256, IN.txcoord0.xy)  * weight[2] * (1 + post_mixer_bloomColor*float3(sin(x[2]), sin(x[2]+TAU/3), sin(x[2]-TAU/3)));
            res.xyz += ColorFetch(RenderTarget128, IN.txcoord0.xy)  * weight[3] * (1 + post_mixer_bloomColor*float3(sin(x[3]), sin(x[3]+TAU/3), sin(x[3]-TAU/3)));
            res.xyz += ColorFetch(RenderTarget64, IN.txcoord0.xy)   * weight[4] * (1 + post_mixer_bloomColor*float3(sin(x[4]), sin(x[4]+TAU/3), sin(x[4]-TAU/3)));
            res.xyz += ColorFetch(RenderTarget32, IN.txcoord0.xy)   * weight[5] * (1 + post_mixer_bloomColor*float3(sin(x[5]), sin(x[5]+TAU/3), sin(x[5]-TAU/3)));
        } else {
            res.xyz += ColorFetch(RenderTarget1024, IN.txcoord0.xy) * weight[0] * (1 + (post_mixer_bloomColor * 4.0));
            res.xyz += ColorFetch(RenderTarget512, IN.txcoord0.xy)  * weight[1] * (1 + (post_mixer_bloomColor * 2.7889));
            res.xyz += ColorFetch(RenderTarget256, IN.txcoord0.xy)  * weight[2] * (1 + (post_mixer_bloomColor * 0.064));
            res.xyz += ColorFetch(RenderTarget128, IN.txcoord0.xy)  * weight[3] * (1 + (post_mixer_bloomColor * 0.0529));
            res.xyz += ColorFetch(RenderTarget64, IN.txcoord0.xy)   * weight[4] * (1 + (post_mixer_bloomColor * 0.0081));
            res.xyz += ColorFetch(RenderTarget32, IN.txcoord0.xy)   * weight[5] * (1 + (post_mixer_bloomColor * 0.0));
        };

        res /= weightSum;

        float3 Temp = AvgLuma(res.xyz).w;
        res.xyz = lerp(Temp.xyz, res.xyz, fSaturation);

        res=max(res, 0.0);
        res=min(res, 16384.0);

        res.w=1.0;
        return res;
    }


// TECHNIQUES //////////////////////////////////////
    TECHNIQUE_UI(GaussPassMBloom, "CFL BLOOM",
        PASS_ARGS_PS(p0, VS_Quad, PS_GaussHResizeFirst, (TextureDownsampled, 1536.0, 2))
    )

    TECHNIQUE_RT(GaussPassMBloom1, "RenderTarget1024",
        PASS_ARGS_PS(p0, VS_Quad, PS_GaussVResizeFirst, (TextureColor, 1536.0, 2))
    )

    TECHNIQUE(GaussPassMBloom2,
        PASS_ARGS_PS(p0, VS_Quad, PS_GaussHResizeFirst, (RenderTarget1024, 512.0, 4))
    )

    TECHNIQUE_RT(GaussPassMBloom3, "RenderTarget512",
        PASS_ARGS_PS(p0, VS_Quad, PS_GaussVResizeFirst, (TextureColor, 512.0, 4))
    )

    TECHNIQUE(GaussPassMBloom4,
        PASS_ARGS_PS(p0, VS_Quad, PS_GaussHResize, (RenderTarget512, 256.0, 5))
    )

    TECHNIQUE_RT(GaussPassMBloom5, "RenderTarget256",
        PASS_ARGS_PS(p0, VS_Quad, PS_GaussVResize, (TextureColor, 256.0, 5))
    )

    TECHNIQUE(GaussPassMBloom6,
        PASS_ARGS_PS(p0, VS_Quad, PS_GaussHResize, (RenderTarget256, 128.0, 6))
    )

    TECHNIQUE_RT(GaussPassMBloom7, "RenderTarget128",
        PASS_ARGS_PS(p0, VS_Quad, PS_GaussVResize, (TextureColor, 128.0, 6))
    )

    TECHNIQUE(GaussPassMBloom8,
        PASS_ARGS_PS(p0, VS_Quad, PS_GaussHResize, (RenderTarget128, 64.0, 7))
    )

    TECHNIQUE_RT(GaussPassMBloom9, "RenderTarget64",
        PASS_ARGS_PS(p0, VS_Quad, PS_GaussVResize, (TextureColor, 64.0, 7))
    )

    TECHNIQUE(GaussPassMBloom10,
        PASS_ARGS_PS(p0, VS_Quad, PS_GaussHResize, (RenderTarget64, 32.0, 8))
    )

    TECHNIQUE_RT(GaussPassMBloom11, "RenderTarget32",
        PASS_ARGS_PS(p0, VS_Quad, PS_GaussVResize, (TextureColor, 32.0, 8))
    )

    TECHNIQUE(GaussPassMBloom12,
        PASS(p0, VS_Quad, PS_GaussMix)
    )
