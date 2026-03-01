//QuantV 3.0.0 Depth Of Field shader	//Copyright gp65cj04, Quant.

uniform float2 mfp < ui_label = "Focus:: Manual FocusPoint"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; > = float2(0.48, 0.59);
uniform bool mf1 < ui_label = "Focus:: Mouse Left Button"; ui_tooltip = "Set new focus point by clicking where you want it"; > = false;
uniform bool mf2 < ui_label = "Focus:: Mouse Right Button"; ui_tooltip = "Set new focus point by clicking where you want it"; > = false;
uniform float ts < ui_label = "Focus:: Tilt Shift Angle"; ui_type = "slider"; ui_min = -60.0; ui_max = 60.0; ui_step = 0.5; > = 10.0;
uniform bool sfpv < ui_label = "Focus:: Skip FPV Weapon"; ui_tooltip = "Avoid blurring player's weapon on near plane"; > = false;
uniform float nears < ui_label = "Static Focus:: Near Distance"; ui_type = "slider"; ui_min = 0.5; ui_max = 20.0; ui_step = 0.005; ui_spacing = 3; > = 1.0;
uniform float far < ui_label = "Static Focus:: Far Distance"; ui_type = "slider"; ui_min = 0.25; ui_max = 20.0; ui_step = 0.005; > = 0.9;
uniform bool use_dfocus < ui_label = "Use Dynamic Focus"; ui_spacing = 3; > = false;
uniform float neard < ui_label = "Dynamic Focus:: Near Intensity"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; > = 0.5;
uniform float fard < ui_label = "Dynamic Focus:: Far Intensity"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; > = 1.0;
uniform float SensorSize < ui_label = "Dynamic Focus:: Sensor Size"; ui_type = "slider"; ui_min = 4.8; ui_max = 36.0; > = 24.0;
uniform uint poly < ui_label = "Bokeh:: Shape"; ui_type = "slider"; ui_min = 5; ui_max = 8; ui_spacing = 3; > = 8;
uniform uint sz < ui_label = "Bokeh:: Size"; ui_type = "slider"; ui_min = 0; ui_max = 8; > = 3;
uniform float rot < ui_label = "Bokeh:: Shape Rotation"; ui_type = "slider"; ui_min = -1.0; ui_max = 1.0; > = 0.17;
uniform float vig < ui_label = "Bokeh:: Optical Vignette/Distortion"; ui_type = "slider"; ui_min = 0.0; ui_max = 2.0; > = 1.0;
uniform float anam < ui_label = "Bokeh:: Anamorphic"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float bright < ui_label = "Bokeh:: Highlight"; ui_type = "slider"; ui_min = 1.0; ui_max = 8.0; ui_step = 0.01; > = 2.0;//1.3
uniform float BokehBias < ui_label = "Bokeh:: Bias"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 0.6;
uniform float BokehBiasCurve < ui_label = "Bokeh:: Bias Curve"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 0.5;
uniform bool df < ui_label = "Post:: Chroma&Vignette using dynamic focus"; ui_spacing = 3; > = true;
uniform float rca < ui_label = "Post:: Radial Chroma Intensity"; ui_type = "slider"; ui_min = 0.001; ui_max = 0.1; ui_step = 0.0001; > = 0.002;
uniform uint cat < ui_label = "Post:: Radial Chroma Type"; ui_type = "slider"; ui_min = 1; ui_max = 6; > = 2;
uniform float cascale < ui_label = "Post:: Radial Chroma Scale"; ui_type = "slider"; ui_min = -4.0; ui_max = 4.0; > = 1.25;
uniform float BlurringAmount < ui_label = "Post:: Blurring Amount"; ui_type = "slider"; ui_min = 0.0; ui_max = 2.0; > = 0.5;
uniform float Noise < ui_label = "Post:: Noise"; ui_type = "slider"; ui_min = 0.0; ui_max = 900.0; ui_step = 2.0; > = 170.0;//165
uniform float FrameScaling < ui_label = "Frame Scaling"; ui_type = "slider"; ui_tooltip = "1.0 = Max Quality but decreases performance"; ui_min = 0.7; ui_max = 1.0; ui_step = 0.001; ui_spacing = 3; > = 0.9;
uniform bool sep < ui_label = " "; ui_spacing = 6; noedit = true; >;

uniform float timer < source = "timer"; >;
uniform bool overlay_open < source = "overlay_open"; >;
uniform float wasdf < hidden = true; >;
uniform int waasdf < hidden = true; > = __APPLICATION__;
static const float2 rcpFrame = float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);
uniform float4 QuantV_1 < hidden = true; >;
uniform float4 QuantV_2 < hidden = true; >;
uniform float4 QuantV_3 < hidden = true; >;

texture BackBufferTex : COLOR;
texture DepthBufferTex : DEPTH;
sampler BackBuffer
{
    Texture = BackBufferTex;
};
sampler DepthBuffer
{
    Texture = DepthBufferTex;
};
texture2D TextureOriginald
{
    Width = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = RGBA16F;
};
sampler2D sTextureOriginald
{
    Texture = TextureOriginald;
};

#define fp (mf1||mf2)?saturate(QuantV_2.xy*rcpFrame):saturate(mfp)
void VS_QO(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD)
{
    texcoord.x = (id == 2) ? 2.0 : 0.0;
    texcoord.y = (id == 1) ? 2.0 : 0.0;
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
void VS_Q0(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD, out nointerpolation float4 t1 : TEXCOORD1, out nointerpolation float4 t3 : TEXCOORD3)
{
    texcoord.x = (id == 2) ? 2.0 : 0.0;
    texcoord.y = (id == 1) ? 2.0 : 0.0;
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    float f0 = tex2Dlod(DepthBuffer, float4(fp, 0, 0)).x;
    float2 offset = float2(0.082, 0.047 * BUFFER_RCP_HEIGHT * BUFFER_WIDTH);
    float f1 = tex2Dlod(DepthBuffer, float4(fp + float2(offset.x, 0.0), 0, 0)).x;
    float f2 = tex2Dlod(DepthBuffer, float4(fp - float2(offset.x, 0.0), 0, 0)).x;
    float f3 = tex2Dlod(DepthBuffer, float4(fp + float2(0.0, offset.y), 0, 0)).x;
    float f4 = tex2Dlod(DepthBuffer, float4(fp - float2(0.0, offset.y), 0, 0)).x;
    float f5 = tex2Dlod(DepthBuffer, float4(fp + offset * 0.58, 0, 0)).x;
    float f6 = tex2Dlod(DepthBuffer, float4(fp - offset * 0.58, 0, 0)).x;
    float f7 = tex2Dlod(DepthBuffer, float4(fp + offset * float2(-0.6, 0.6), 0, 0)).x;
    float f8 = tex2Dlod(DepthBuffer, float4(fp + offset * float2(0.6, -0.6), 0, 0)).x;
    float f9 = tex2Dlod(DepthBuffer, float4(fp - float2(offset.x * 0.5, 0.0), 0, 0)).x;
    float f10 = tex2Dlod(DepthBuffer, float4(fp + float2(offset.x * 0.5, 0.0), 0, 0)).x;
    float f11 = tex2Dlod(DepthBuffer, float4(fp + offset * float2(-0.25, 0.43), 0, 0)).x;
    float f12 = tex2Dlod(DepthBuffer, float4(fp - offset * float2(0.25, 0.43), 0, 0)).x;
    float f13 = tex2Dlod(DepthBuffer, float4(fp + offset * float2(-1.5, 0.29), 0, 0)).x;
    f0 = max(max(max(max(max(max(max(max(max(max(max(max(max(f0, f13), f12), f11), f10), f9), f8), f7), f6), f5), f4), f3), f2), f1);
    t1.x = use_dfocus ? f0 : -0.1500022 / (-0.000015 - f0);
    float tiltshift = (0.5 - texcoord.x) * tan(-radians(ts));
    t1.y = t1.x * (1.0 + tiltshift * 0.09);
    t1.z = (1.0 + tiltshift) * pow(4.0, far);
    t1.w = tiltshift;
    t3.x = t1.x * (1.0 + t1.w);
    t3.y = t1.x * t1.z - t3.x;
    t3.zw = 0.0;
}
void VS_Q1(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD, out nointerpolation float4 t1 : TEXCOORD1, out nointerpolation int t2 : TEXCOORD2, out nointerpolation float4 t3 : TEXCOORD3, out float4 t4 : TEXCOORD4)
{
    texcoord.x = (id == 2) ? 2.0 : 0.0;
    texcoord.y = (id == 1) ? 2.0 : 0.0;
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    texcoord = texcoord / FrameScaling;
    t1.x = use_dfocus ? (1.0 / neard) : (1.0 / nears);
    t1.yzw = 0.0;
	switch (sz) {
	case 1:t1.w = 3.7; break;
	case 2:t1.w = 6.9; break;
	case 3:t1.w = 10.0; break;
	case 4:t1.w = 13.3; break;
	case 5:t1.w = 16.5; break;
	case 6:t1.w = 19.2; break;
	case 7:t1.w = 22.2; break;
	case 8:t1.w = 31.0; break;
	}
    t1.w *= saturate(BlurringAmount * 0.5 + 0.5);
    t2 = float((sz + 1) * sz * poly) * 0.5;
    sincos(radians(360 / poly), t3.y, t3.x);
    sincos(rot, t3.w, t3.z);
    float2 centerPoint = df ? fp : 0.5;
    t4.xy = texcoord.xy - centerPoint;
    t4.zw = float2(texcoord.y - centerPoint.y, centerPoint.x - texcoord.x);
}
void VS_Q2(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD, out nointerpolation float4 t1 : TEXCOORD1, out nointerpolation int t2 : TEXCOORD2, out nointerpolation float4 t3 : TEXCOORD3, out float4 t4 : TEXCOORD4)
{
    texcoord.x = (id == 2) ? 2.0 : 0.0;
    texcoord.y = (id == 1) ? 2.0 : 0.0;
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    float2 tmptexcoord = texcoord;
    texcoord = texcoord * FrameScaling;
	t2 = floor(rca * 1600);
	float3 offset = float3(2,1,4);
	switch (cat) {
	case 1:offset = float3(1,2,4); break;
	case 2:offset = float3(2,1,4); break;
	case 3:offset = float3(1,4,2); break;
	case 4:offset = float3(2,4,1); break;
	case 5:offset = float3(4,2,1); break;
	case 6:offset = float3(4,1,2); break;
	}
    t1.xyz = 1.0 / (rca * 1600) * offset;
    t1.w = 0.0;
	switch (sz) {
	case 1:t1.w = 3.7; break;
	case 2:t1.w = 6.9; break;
	case 3:t1.w = 10.0; break;
	case 4:t1.w = 13.3; break;
	case 5:t1.w = 16.5; break;
	case 6:t1.w = 19.2; break;
	case 7:t1.w = 22.2; break;
	case 8:t1.w = 31.0; break;
	}
    t3.xyz = 0.0;
    t3.w = use_dfocus ? (1.0 / neard) : (1.0 / nears);
    float2 pointcenter = df ? fp : 0.5;
    t4.xy = texcoord - (pointcenter * FrameScaling);
    t4.zw = tmptexcoord;
}
void VS_Q3(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD, out nointerpolation float4 t1 : TEXCOORD1)
{
    texcoord.x = (id == 2) ? 2.0 : 0.0;
    texcoord.y = (id == 1) ? 2.0 : 0.0;
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    t1.x = use_dfocus ? (1.0 / neard) : (1.0 / nears);
    t1.yzw = 0.0;
	switch (sz) {
	case 1:t1.w = 3.7; break;
	case 2:t1.w = 6.9; break;
	case 3:t1.w = 10.0; break;
	case 4:t1.w = 13.3; break;
	case 5:t1.w = 16.5; break;
	case 6:t1.w = 19.2; break;
	case 7:t1.w = 22.2; break;
	case 8:t1.w = 31.0; break;
	}
}
float4 PS_Q0(float4 position : SV_Position, float2 texcoord : TEXCOORD, nointerpolation float4 t1 : TEXCOORD1, nointerpolation float4 t3 : TEXCOORD3) : SV_Target
{
    float4 res = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0));
    float depth = tex2Dlod(DepthBuffer, float4(texcoord, 0.0, 0.0)).x;
    if (use_dfocus)
    {
        res.w = clamp((depth - t1.y) * SensorSize, -1.8, 1.8);
        res.w *= (depth < t1.y) ? -neard : fard;
        if (-0.1500022 / (-0.000015 - depth) < 2.69 && sfpv) res.w = 0.0;
    }
    else
    {
        depth = -0.1500022 / (-0.000015 - depth);
        res.w = (depth < t3.x) ? (depth - t3.x) / t3.x : saturate((depth - t3.x) / t3.y);
        if (depth < 2.69 && sfpv) res.w = 0.0;
    }
    float q = frac(52.982918 * frac(0.06711056 * position.x + 0.00583715 * position.y)) * 2.0 - 1.0;
    if ((position.x < 1) && (position.y < 3)) res.xyz = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0), int2(1, 0)).xyz;
    res.xyz = lerp(res.xyz, pow(q * 0.066 + sqrt(res.xyz), 2.0), res.w);
    res.w = res.w * 0.5 + 0.5;
    return res;
}
float4 PS_Q1(float4 position : SV_Position, float2 texcoord : TEXCOORD, nointerpolation float4 t1 : TEXCOORD1, nointerpolation int t2 : TEXCOORD2, nointerpolation float4 t3 : TEXCOORD3, float4 t4 : TEXCOORD4) : SV_Target
{
    if (max(texcoord.x, texcoord.y) > 1.001) discard;
    float4 res = tex2Dlod(sTextureOriginald, float4(texcoord, 0.0, 0.0));
	[branch] if (sz > 0)
    {
        float centerDepth = res.w;
        float discRadius = abs(centerDepth * 2.0 - 1.0) * t1.w;
        discRadius *= (centerDepth < 0.5) ? t1.x : 1.0;
        float3 weight = 1.0;
        res.xyz *= BokehBias;
        res.xyz = pow(abs(res.xyz), bright) * weight;
        res.w = weight.x;
        float sampleCycleCounter = 0.0;
        float sampleCounterInCycle;
        float2 currentVertex;
        float2 nextVertex = t3.zw;
        float2 rotangle;
        sincos(length(t4.xy) * vig, rotangle.x, rotangle.y);
        float2 centerVig = normalize(t4.zw);
        float2 ps = rcpFrame * discRadius;
        ps.x *= anam;
        for (int i = 0; i < t2; ++i)
        {
            if (sampleCounterInCycle == (sampleCycleCounter * poly))
            {
                sampleCounterInCycle = 0.0;
                sampleCycleCounter++;
            }
            float2 sampleOffset;
			[branch] if (poly > 7)
            {
                sincos(0.78539816 * (sampleCounterInCycle / sampleCycleCounter), sampleOffset.y, sampleOffset.x);
                sampleOffset *= ps * sampleCycleCounter / float(sz);
            }
            else
            {
                float sideOffset = frac(sampleCounterInCycle / sampleCycleCounter);
                if (sideOffset < 0.1)
                {
                    currentVertex = nextVertex;
                    nextVertex = float2(dot(currentVertex, float2(t3.x, -t3.y)), dot(currentVertex, t3.yx));
                }
                sampleOffset = lerp(currentVertex, nextVertex, sideOffset);
                sampleOffset *= ps * sampleCycleCounter / float(sz + 1.0);
            }
            sampleOffset = sampleOffset * rotangle.y + centerVig * dot(centerVig, sampleOffset) * (1.0 - rotangle.y);
            sampleCounterInCycle++;
            float4 tap = tex2Dlod(sTextureOriginald, float4(texcoord + sampleOffset, 0.0, 0.0));
            weight.xyz = (tap.w >= centerDepth) ? 1.0 : abs(tap.w * 2.0 - 1.0);
            tap.xyz *= lerp(1.0, pow(abs(sampleCycleCounter / sz), BokehBiasCurve), BokehBias);
            res.xyz += pow(abs(tap.xyz * weight.xyz * 1.1), bright);
            res.w += pow(abs(weight.y), bright);
        }
        res.xyz = pow(abs(res.xyz / res.w), rcp(bright));
        res.w = centerDepth;
    }
    return res;
}
float4 PS_Q2(float4 position : SV_Position, float2 texcoord : TEXCOORD, nointerpolation float4 t1 : TEXCOORD1, nointerpolation int t2 : TEXCOORD2, nointerpolation float4 t3 : TEXCOORD3, float4 t4 : TEXCOORD4) : SV_Target
{
    float4 res = 0.0;
    res.w = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0)).w;
    float discRadius = abs(res.w * 2.0 - 1.0);
    discRadius *= (res.w < 0.5) ? t3.w : 1.0;
    float2 uvx, uvy, uvz = 0.0;
    float2 vig = normalize(t4.xy) * length(t4.xy) * rca * discRadius * cascale;
    for (int i = 0; i < t2; ++i)
    {
        res.x += tex2Dlod(BackBuffer, float4(texcoord + uvx, 0.0, 0.0)).x;
        res.y += tex2Dlod(BackBuffer, float4(texcoord + uvy, 0.0, 0.0)).y;
        res.z += tex2Dlod(BackBuffer, float4(texcoord + uvz, 0.0, 0.0)).z;
        uvx -= vig * t1.x;
        uvy -= vig * t1.y;
        uvz -= vig * t1.z;
    }
    res.xyz /= float(t2);
    float3 origcol = tex2Dlod(sTextureOriginald, float4(t4.zw, 0.0, 0.0)).xyz;
    float blurAmount = abs(res.w * 2.0 - 1.0) * t1.w;
    blurAmount *= (res.w < 0.5) ? t3.w : 1.0;
    res.xyz = lerp(origcol, res.xyz, saturate(blurAmount));
    return res;
}
static const float weight[5] = { 0.227027027, 0.1945945946, 0.1216216216, 0.0540540541, 0.0162162162 };
float4 PS_Q3(float4 position : SV_Position, float2 texcoord : TEXCOORD, nointerpolation float4 t1 : TEXCOORD1) : SV_Target
{
    float4 res = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0));
    float discRadius = abs(res.w * 2.0 - 1.0) * t1.w;
    discRadius *= (res.w < 0.5) ? t1.x : 1.0;
    float bsize = discRadius * 6.0;
    float blur = res.w;
    blur += tex2Dlod(BackBuffer, float4(texcoord + float2(rcpFrame.x * bsize, 0.0), 0.0, 0.0)).w;
    blur += tex2Dlod(BackBuffer, float4(texcoord + rcpFrame * bsize * float2(0.5, 0.8660254), 0.0, 0.0)).w;
    blur += tex2Dlod(BackBuffer, float4(texcoord + rcpFrame * bsize * float2(-0.5, 0.8660254), 0.0, 0.0)).w;
    blur += tex2Dlod(BackBuffer, float4(texcoord + float2(-rcpFrame.x * bsize, 0.0), 0.0, 0.0)).w;
    blur += tex2Dlod(BackBuffer, float4(texcoord + rcpFrame * bsize * float2(-0.5, -0.8660254), 0.0, 0.0)).w;
    blur += tex2Dlod(BackBuffer, float4(texcoord + rcpFrame * bsize * float2(0.5, -0.8660254), 0.0, 0.0)).w;
    blur *= (1.0 / 7.0);
    res.w = max(res.w, blur);
    float blurAmount = abs(res.w * 2.0 - 1.0);
    blurAmount = smoothstep(0.15, 1.0, blurAmount);
    res.xyz *= weight[0];
    float pix = rcpFrame.x * blurAmount * BlurringAmount;
	[unroll] for (int i = 1; i < 5; ++i)
    {
        res.xyz += tex2Dlod(BackBuffer, float4(texcoord + float2(pix * i, 0.0), 0.0, 0.0)).xyz * weight[i];
        res.xyz += tex2Dlod(BackBuffer, float4(texcoord - float2(pix * i, 0.0), 0.0, 0.0)).xyz * weight[i];
    }
    return res;
}
float4 PS_Q4(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    float4 res = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0));
    float blurAmount = abs(res.w * 2.0 - 1.0);
    blurAmount = smoothstep(0.15, 1.0, blurAmount);
    res.xyz *= weight[0];
    float pix = rcpFrame.y * blurAmount * BlurringAmount;
	[unroll] for (int i = 1; i < 5; ++i)
    {
        res.xyz += tex2Dlod(BackBuffer, float4(texcoord + float2(0.0, pix * i), 0.0, 0.0)).xyz * weight[i];
        res.xyz += tex2Dlod(BackBuffer, float4(texcoord - float2(0.0, pix * i), 0.0, 0.0)).xyz * weight[i];
    }
    float3 noize = texcoord.xyy + 4.0;
    noize = float3(1000.0, 76.9231, 8.13008) * 0.000001 * timer * noize.x * noize.y;
    noize.yz = floor(noize.yz);
    noize.xy = (noize.yz * float2(-13.0, -123.0) + noize.x) + 1.0;
    noize.x = noize.x * noize.y;
    noize.y = floor(noize.x * 100.0);
    noize.x = (noize.y * -0.01 + noize.x) - 0.006;
    noize.y = dot(res.xyz, 0.3333333);
    noize.x = noize.x * Noise + noize.y;
    noize.xz = float2(-0.5, 1.0) + noize.xy;
    noize.y = saturate(-(noize.y / noize.z) * 4.0 + 1.0);
    res.xyz *= noize.x * noize.y * res.w * 0.1 + 1.0;
    return res;
}

technique QuantV_DoF < ui_label = "QuantV_DoF - OUTDATED -"; ui_tooltip = "\n  OUTDATED  \n\n"; >
{
    pass p0
    {
        VertexShader = VS_Q0;
        PixelShader = PS_Q0;
        RenderTarget = TextureOriginald;
    }
    pass p1
    {
        VertexShader = VS_Q1;
        PixelShader = PS_Q1;
    }
    pass p2
    {
        VertexShader = VS_Q2;
        PixelShader = PS_Q2;
    }
    pass p3
    {
        VertexShader = VS_Q3;
        PixelShader = PS_Q3;
    }
    pass p4
    {
        VertexShader = VS_QO;
        PixelShader = PS_Q4;
    }
}
