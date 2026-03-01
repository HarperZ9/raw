/***************************************************************************
 *  Source:
 *      VHS: drmelon (https://www.shadertoy.com/view/4dBGzK)
 *      VCR: ryk (https://www.shadertoy.com/view/ldjGzV)
 *      VHS Tape Noise: Vladmir Storm (https://www.shadertoy.com/view/MlfSWr)
 *
 *                                      Ported to SM5 HLSL by kingeric
 ***************************************************************************/

//#define VHS_LOAD_UI //comment out this line to disable UI

/***************************************************************************
 *
 *  Shared Functions : move these to shared fxh if there's any
 *
 ***************************************************************************/

 #define tNoise_Color noise_brightness * 0.01
 #define lNoise_Color noise_brightness * 0.01
 #define VHS_bUseTapeNoise lerp(0, VHS_bUseTapeNoiseBool, ENABLE_VHS)
 #define VHS_bUseLayerNoise lerp(0, VHS_bUseLayerNoiseBool, ENABLE_VHS)

//function overloading
float  SinC(float  x) { return (x==0.0)? 1.0: sin(x)/x; }
float2 SinC(float2 x) { return (x==0.0)? 1.0: sin(x)/x; }
float3 SinC(float3 x) { return (x==0.0)? 1.0: sin(x)/x; }
float4 SinC(float4 x) { return (x==0.0)? 1.0: sin(x)/x; }

float  ramp(float  y, float  start, float  end) { return saturate(1.0 - (y-start)/(end-start)); }
float2 ramp(float2 y, float2 start, float2 end) { return saturate(1.0 - (y-start)/(end-start)); }
float3 ramp(float3 y, float3 start, float3 end) { return saturate(1.0 - (y-start)/(end-start)); }
float4 ramp(float4 y, float4 start, float4 end) { return saturate(1.0 - (y-start)/(end-start)); }

SamplerState VHS_SamplerLinear {
    Filter   = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};

SamplerState VHS_SamplerRepeat {
    Filter   = MIN_MAG_MIP_LINEAR;
    AddressU = Wrap;
    AddressV = Wrap;
};

#ifdef VHS_LOAD_UI
    #define UI
#else
    #define UI const static
#endif

Texture2D VHS_TexNoise <string ResourceName = "Textures/vhsNoise.png";>;

static const float  VHS_Timer      = Timer.x * 16777.216;
static const float2 VHS_ScreenSize = float2(ScreenSize.x, ScreenSize.x * ScreenSize.w);

struct VHS_struct {
    float4 pos    : SV_POSITION;
    float2 coord  : TEXCOORD0;
    float4 rand   : TEXCOORD1;
    float4 NTSCuv : TEXCOORD2;
};

VHS_struct VS_Shared( float4 pos, float2 coord)
{
    VHS_struct o = { float4(pos.rgb, 1.0),
             coord,
             VHS_TexNoise.SampleLevel(VHS_SamplerRepeat, VHS_Timer * float2( 0.01, 0.02), 0),
             coord.xxxy - float4(11.0, 10.0, 9.0, 0.0) * ScreenSize.y };
    return o;
}

float4 hash42(float2 p)
{
    float4 p4  = frac( p.xyxy * float4(443.8975,397.2973, 491.1871, 470.7827));
           p4 += dot( p4.wzxy, p4 + 19.19);
    return frac(p4.xxyx * p4.yzww);
}

float hash( float n ){ return frac(sin(n)*43758.5453123); }

/***************************************************************************
 *
 *  NTSC Codec: UltraMoogleMan (https://www.shadertoy.com/view/ldXGRf)
 *
 ***************************************************************************/

UI int    NTSC_Space          <string UIName="::::NTSC::::"; float UIMin = 1.0; float UIMax = 1.0;> = {1};
UI float  NTSC_A              <string UIName="NTSC_A"; > = { 0.27 };  //A value for NTSC signal processing.
UI float  NTSC_NotchHalfWidth <string UIName="NTSC_NotchHalfWidth"; > = { 0.5 };   // Phase Count value for NTSC signal processing. ?
UI float  NTSC_ScanTime       <string UIName="NTSC_ScanTime (u sec)"; > = { 52.5 };               // Horizontal scanline duration for NTSC signal processing. (usec)

static const float  NTSC_B        = 0.5;                // B value for NTSC signal processing.
static const float  NTSC_P        = 1.0;                // Y filter notch width for NTSC signal processing. (fixed to 1.0 for optimization.)
static const float  NTSC_CC       = 3.579545;           // Color Carrier frequency for NTSC signal processing.
static const float3 NTSC_YIQ      = { 6.0, 1.2, 0.6 };  // YQI filter cutoff frequency for NTSC signal processing.
static const float  NTSC_MaxC     = 2.1183;
static const float  NTSC_MinC     = -1.1183;
static const float  NTSC_CRange   = NTSC_MaxC - NTSC_MinC;
static const float  NTSC_W        = 6.283185307 * NTSC_CC * NTSC_ScanTime;

static const float3x3 NTSC_Transform = {
        0.299,     0.587,     0.114,
        0.595716, -0.274453, -0.321263,
        0.211456, -0.522591,  0.311135
    };

static const float3x3 NTSC_InvTransform = {
        1.0,  0.956,  0.621,
        1.0, -0.272, -0.647,
        1.0, -1.106,  1.703
    };

float4 NTSC_encoder(float4 WT, float3 P0, float3 P1, float3 P2) {
    float4x3 M  = { lerp(P1, P0, 0.25), P1, lerp(P1, P2, 0.25), lerp(P1, P2, 0.5)};
             M *= transpose(float3x4(float(1.0).xxxx, cos(WT), sin(WT)));
    return (mul(M, float(1.0).xxx) - NTSC_MinC) / NTSC_CRange;
}

//sinc
float3 VHS_NTSC_decoder(Texture2D texIn, float4 UV) {
    // Frequency cutoffs for the individual portions of the signal that we extract.
    // Y1 and Y2 are the positive and negative frequency limits of the notch filter on Y.
    //
    float Fc_y1  = 0.5 * ScreenSize.y * NTSC_ScanTime * (NTSC_CC + NTSC_NotchHalfWidth);
    float Fc_y2  = 0.5 * ScreenSize.y * NTSC_ScanTime * (NTSC_CC - NTSC_NotchHalfWidth);
    float Fc_y   = 0.5 * ScreenSize.y * NTSC_ScanTime * NTSC_YIQ.x;
    float Fc_i   = 0.5 * ScreenSize.y * NTSC_ScanTime * NTSC_YIQ.y;
    float Fc_q   = 0.5 * ScreenSize.y * NTSC_ScanTime * NTSC_YIQ.z;

    float3x4 filter = { float(0.0).xxxx, float(0.0).xxxx, float(0.0).xxxx };

    float3 p0 = 0.0;
    float3 p1 = mul( NTSC_Transform, saturate(texIn.Sample(VHS_SamplerLinear, UV.xw).rgb));
    float3 p2 = mul( NTSC_Transform, saturate(texIn.Sample(VHS_SamplerLinear, UV.yw).rgb));

    UV.y += 10.0 * ScreenSize.y;

    //optimization: sampling 80 times -> 22 times
    for(float n = -41.0; n < 42.0; n += 4.0) {
        p0 = p1;
        p1 = p2;
        p2 = mul( NTSC_Transform, saturate(texIn.Sample(VHS_SamplerLinear, UV.zw).rgb));

        float4 tmp = float(n) + float4(0.0, 1.0, 2.0, 3.0);

        float3x4 IdealYIQ = {
            Fc_y2 * SinC(3.1415926535 * Fc_y2 * tmp) -
            Fc_y1 * SinC(3.1415926535 * Fc_y1 * tmp) +
            Fc_y  * SinC(3.1415926535 * Fc_y  * tmp),
            Fc_i  * SinC(3.1415926535 * Fc_i  * tmp),
            Fc_q  * SinC(3.1415926535 * Fc_q  * tmp)
        };

        float4 WT  = NTSC_A * 2.0 * ScreenSize.x * UV.w + NTSC_B + UV.y + ScreenSize.y * tmp * 0.25;
               WT *= NTSC_W;
        tmp   = 0.54 + 0.46 * cos(6.283185307 / 82.0 * tmp);
        tmp  *= NTSC_encoder(WT, p0, p1, p2) * NTSC_CRange + NTSC_MinC; // buffer for optimize sampling
        UV.z += ScreenSize.y;

        filter += IdealYIQ * float3x4(tmp, tmp*cos(WT), tmp*sin(WT));
    }

    return mul(NTSC_InvTransform, mul(filter, float(1.0).xxxx) * float3(1.0, 2.0, 2.0));
}

/***************************************************************************
 *
 *  VHS: drmelon (https://www.shadertoy.com/view/4dBGzK)
 *
 ***************************************************************************/

UI int   VHS_Space  <string UIName="::::VHS::::"; float UIMin = 2.0; float UIMax = 2.0;> = {2};
UI float VHS_magnitude <string UIName="VHS_magnitude";> = { 0.9 };
//UI float fVHSredDistortion <string UIName="Red Distortion";> = { 0.0025 };
//UI float fVHSimagenoise <string UIName="Image Noise";> = { 0.00114 };

float VHS_rand(float2 co)
{
    float3 parm = { 12.9898, 78.233, 43758.5453 };
    return frac(sin(dot(co, parm.xy) % 3.14) * parm.z);
}

float4 VHS_VHS(Texture2D texIN, float2 uv )
{
    float4 coord = uv.xxxy;

    coord.r += VHS_rand(float2(VHS_Timer * 0.03, coord.w * 0.42)) * lerp(0.0, (TAPE_DIST_FLIP == 1 ? -0.005 : 0.0045), TAPE_DIST * 0.01); // fVHSredDistortion
    coord.r += sin(VHS_rand(float2(VHS_Timer*0.1, coord.w))) * VHS_magnitude * 0.000135;

    coord.g += VHS_rand(float2(VHS_Timer*0.0003,coord.w*0.2)) * lerp(0.0, (TAPE_DIST_FLIP == 1 ? -0.00228 : 0.00228), TAPE_DIST * 0.01); // fVHSimagenoise
    coord.g += sin(VHS_Timer * 0.108) * 0.0009;

//  coord.b  = coord.w;
//  coord.b += VHS_rand(float2(cos(VHS_Timer*0.01),sin(coord.w)));

    return float4( texIN.Sample(VHS_SamplerLinear, coord.ra).r,
                   texIN.Sample(VHS_SamplerLinear, coord.ga).g,
                   texIN.Sample(VHS_SamplerLinear, coord.ba).ba);
}

/***************************************************************************
 *
 *  VCR: ryk (https://www.shadertoy.com/view/ldjGzV)
 *
 ***************************************************************************/
//todo: swap stripes with tapeNoise

UI int    VCR_Space      <string UIName="::::VCR::::"; float UIMin = 3.0; float UIMax = 3.0;> = {3};
UI float2 VCR_noiseScale <string UIName="VCR_noiseScale";> = { 10.0, 20.0 };
UI float  VCR_Brightness <string UIName="VCR_Brightness";> = { 0.4 };

//this can go to Vertex shader
float VCR_onOff(float a, float b, float c)
{
    return step(c, sin(VHS_Timer + a*cos(VHS_Timer*b)));
}

float3 VHS_VCR(Texture2D texIN, float2 uv)
{

    float2 shift;

    #if(ENB_QUALITY == 2)
        if(TAPE_WARP && ENABLE_TAPE_DIST && ENABLE_VHS && ((ENABLE_BORDER && BORDER_RATIO < 1.34) || (TV_OVERLAY)))
        {
            shift.x  = uv.y - frac(VHS_Timer*0.33);
            shift.x  = VCR_onOff( 1.0, 1.0, 0.033) / ( 0.33 + 1000 * shift.x * shift.x); //1.0 weight at horizontal line
            shift.x *= sin(uv.y+VHS_Timer) / 500.0 * (1.0+cos(VHS_Timer));
        }
        else
        {
            shift.x = 0.0;
        }
    #else
        if(ENABLE_TAPE_DIST && ENABLE_VHS && ((ENABLE_BORDER && BORDER_RATIO < 1.34) || (TV_OVERLAY)))
        {
            shift.x  = uv.y - frac(VHS_Timer*0.33);
            shift.x  = VCR_onOff( 1.0, 1.0, 0.033) / ( 0.33 + 1000 * shift.x * shift.x); //1.0 weight at horizontal line
            shift.x *= sin(uv.y+VHS_Timer) / 500.0 * (1.0+cos(VHS_Timer));
        }
        else
        {
            shift.x = 0.0;
        }
    #endif

    #if(ENB_QUALITY == 2)
        if(TAPE_JITTER && ENABLE_TAPE_DIST && ENABLE_VHS)
        {
            shift.y = cos(VHS_Timer)*sin(VHS_Timer*60.0) * 0.01;
            shift.y *= lerp(0.066, 0.022, (JITTER_STRENGTH * 0.33));
        }
        else
        {
            shift.y = 0.0;
        }
    #else
        if(ENABLE_TAPE_DIST && ENABLE_VHS)
        {
            shift.y = cos(VHS_Timer)*sin(VHS_Timer*60.0) * 0.01;
            shift.y *= 0.033;
        }
        else
        {
            shift.y = 0.0;
        }
    #endif

    shift += uv;

    float3 res  = texIN.Sample(VHS_SamplerLinear, float2(shift.x, frac(shift.y))).rgb * step(shift.x, 1.0) * step(0.0, shift.x);

    float2 delta = abs(shift - uv);
    float3 gamma = pow(res, 0.9) * 9.0;
    res = lerp(res, gamma, saturate(max(delta.x, delta.y)));

    return res;
}

/***************************************************************************
 *
 *  VHS Tape Noise: Vladmir Storm (https://www.shadertoy.com/view/MlfSWr)
 *
 ***************************************************************************/

UI int   tNoise_Space    <string UIName="::::TapeNoise::::"; > = {0};
UI float tNoise_linesN   <string UIName="TapeNoise_linesN"; float UIStep = 1.0;> = {960.0};
UI float tNoise_Curve    <string UIName="TapeNoise_Curve";> = {9.0};
UI float tNoise_Strength <string UIName="TapeNoise_Strength";> = {3.0};
//UI float3 tNoise_Color   <string UIName="TapeNoise_Color"; string UIWidget = "color"; > = { 0.78, 0.78, 0.78 };

// 3d noise function (iq's)
float n( float3 x )
{
    float3 p = floor(x);
    float3 f = frac(x);
    f *= f*(3.0-2.0*f);
    float n = dot(p, float3(1.0, 57.0, 113.0));

    return lerp(lerp(lerp( hash(n+  0.0), hash(n+  1.0),f.x),
                     lerp( hash(n+ 57.0), hash(n+ 58.0),f.x),f.y),
                lerp(lerp( hash(n+113.0), hash(n+114.0),f.x),
                     lerp( hash(n+170.0), hash(n+171.0),f.x),f.y),f.z);
}

float nn(float2 uv)
{
    float v = -VHS_Timer*2.0;
    v = (n(float3(uv.y * 0.01           + v, 1.0, 1.0)) + 0.0) *
        (n(float3(uv.y * 0.011 + 1000.0 + v, 1.0, 1.0)) + 0.0) *
        (n(float3(uv.y * 0.51  + 421.0  + v, 1.0, 1.0)) + 0.0);

    v *= hash42( float2(uv.x + v*0.01, uv.y)).x + 0.3;

    return  min(pow(v, tNoise_Curve)*tNoise_Strength, 1.0);
}

float4 VHS_TapeNoise(float4 color, float2 coord)
{
    const float Steps = VHS_ScreenSize.y/tNoise_linesN;
    float2 pos = coord*VHS_ScreenSize;
    float4 uv;
    uv.xy = floor(pos/Steps)*Steps;
    uv.zw = ceil(pos/Steps)*Steps;

    float2 w = (pos-uv.xy)/Steps;

    return float4(lerp(color.rgb, tNoise_Color,
                  lerp(lerp(nn(uv.xy), nn(uv.xw), w.y),
                       lerp(nn(uv.zy), nn(uv.zw), w.y), w.x)), color.a);
}

/***************************************************************************
 *
 *  Layered Noise:
 *
 ***************************************************************************/

UI int    lNoise_Space  <string UIName="::::LayerNoise::::"; > = {0};
UI float  lNoise_Ratio  <string UIName="LayerNoise_Ratio";> = {40.0};
UI float  lNoise_Scale  <string UIName="LayerNoise_Scale";> = {0.6};
//UI float3 lNoise_Color  <string UIName="LayerNoise_Color"; string UIWidget = "color"; > = { 0.78, 0.78, 0.78 };
UI float  lNoise_freq   <string UIName="LayerNoise_freq ";> = {2.0};
UI float  lNoise_Amp    <string UIName="LayerNoise_Amp  ";> = {0.02};
UI float  lNoise_inner  <string UIName="LayerNoise_inner";> = {1.0};
UI float  lNoise_outer  <string UIName="LayerNoise_outer";> = {1.0};

//implement bilinear
float4 Layer_noise(float2 seed)
{
    float2 size  = float2(lNoise_Ratio, 1.0)*lNoise_Scale;
    float4 p;
    p.xy = floor( seed.xy / size) * size;
    p.zw = ceil( seed.xy / size) * size;

    float2 w = (seed - p.xy)/size;
    float2 q = float2(frac(sin(VHS_Timer)+sin(0.1*VHS_Timer)), 0.0);

    return lerp(lerp(hash42(p.xy+q), hash42(p.xw+q), w.y),
               lerp(hash42(p.zy+q), hash42(p.zw+q), w.y), w.x);
}

float4 VHS_LayerNoise(float4 color, float2 uv )
{
    float2 fragCoord = uv*VHS_ScreenSize;

    float4 noise0 = Layer_noise(fragCoord);
    float4 noise1 = Layer_noise(VHS_ScreenSize-fragCoord)*noise0;

    float  d = smoothstep(lNoise_inner+lNoise_Amp*sin(VHS_Timer*lNoise_freq),
                          lNoise_outer+lNoise_Amp*sin(VHS_Timer*lNoise_freq),
                          length(fragCoord/VHS_ScreenSize-0.5));
    float4 noise = lerp(noise1 * 1.2, noise0, d);

	return float4(lerp(color.rgb, lNoise_Color, min(noise.x*noise.y*noise.z*noise.w*4.0, 1.0)), color.a);
}

/***************************************************************************
 *
 *  EOF
 *
 ***************************************************************************/

VHS_struct VS_VHSnPost(VS_INPUT_POST IN)
{
    return VS_Shared(float4(IN.pos, 1.0), IN.txcoord);
}
