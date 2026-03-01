//QuantV PostFX effect			//Thanks to AMD, Timothy Lottes, Christian Jensen, prod80, dpeasant3, Quant

uniform bool AntiAliasing < ui_label = "AntiAliasing"; > = true;
uniform float Sharpen < ui_label = "Sharpen"; ui_type = "drag"; ui_min = 0.0; ui_max = 4.0; > = 1.4;
uniform float Noise < ui_label = "Filmic Noise"; ui_type = "drag"; ui_min = 0.0; ui_max = 0.1; > = 0.01;
uniform float PaletteLut < ui_label = "Palette Lut"; ui_type = "drag"; ui_min = 0.0; ui_max = 2.0; ui_spacing = 4; ui_tooltip = "Default: 1.0\nDisabled: 0.0";  > = 0.0;
uniform bool UseLevels < ui_label = "Use Levels"; > = false;
uniform float3 iBlack < ui_label = "Input Black Point"; ui_type = "color"; > = float3(0.0314, 0.0353, 0.0392);
uniform float3 iWhite < ui_label = "Input White Point"; ui_type = "color"; > = float3(0.9568, 0.9353, 0.9137);
uniform float3 oBlack < ui_label = "Output Black Point"; ui_spacing = 1; ui_type = "color"; > = float3(0.0, 0.0, 0.0);
uniform float3 oWhite < ui_label = "Output White Point"; ui_type = "color"; > = float3(1.0, 1.0, 1.0);
uniform float3 Tint_d < ui_label = "Tint Color (DayTime)"; ui_spacing = 3; ui_type = "color"; > = float3(0.37, 0.39, 0.46);
uniform float3 Tint_n < ui_label = "Tint Color (NightTime)"; ui_type = "color"; > = float3(0.37, 0.39, 0.46);
uniform float2 Tinti < ui_label = "Tint Intensity"; ui_tooltip = "DAY    |    NIGHT"; ui_type = "drag"; ui_min = 0.0; ui_max = 1.0; > = float2(0.0, 0.0);
uniform float3 ColorMood_day < ui_label = "ColorMood (DayTime)"; ui_spacing = 3; ui_type = "color"; > = float3(0.705, 0.635, 0.55);
uniform float3 ColorMood_night < ui_label = "ColorMood (NightTime)"; ui_type = "color"; > = float3(0.705, 0.635, 0.55);
uniform float2 ColorMoodIntensity < ui_label = "ColorMood Intensity"; ui_tooltip = "DAY    |    NIGHT"; ui_type = "drag"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.001; > = float2(0.0, 0.0);
uniform float2 qBright < ui_label = "Bright"; ui_tooltip = "DAY    |    NIGHT"; ui_spacing = 3; ui_type = "drag"; ui_min = 0.01; ui_max = 5.0; ui_step = 0.001; > = float2(1.0, 1.0);
uniform float2 qContrast < ui_label = "Contrast"; ui_tooltip = "DAY    |    NIGHT"; ui_type = "drag"; ui_min = -1.0; ui_max = 1.0; ui_step = 0.001; > = float2(0.0, 0.0);
uniform float2 qGamma < ui_label = "Gamma"; ui_tooltip = "DAY    |    NIGHT"; ui_type = "drag"; ui_min = 0.01; ui_max = 5.0; ui_step = 0.001; > = float2(1.0, 1.0);
uniform float2 qSaturation < ui_label = "Saturation"; ui_tooltip = "DAY    |    NIGHT"; ui_type = "drag"; ui_max = 5.0; ui_step = 0.001; > = float2(1.0, 1.0);
uniform float2 desatR < ui_label = "Desaturate Red"; ui_tooltip = "DAY    |    NIGHT"; ui_spacing = 3; ui_type = "drag"; ui_min = 0.0; ui_max = 5.0; > = float2(0.0, 0.0);
uniform float2 desatG < ui_label = "Desaturate Green"; ui_tooltip = "DAY    |    NIGHT"; ui_type = "drag"; ui_min = 0.0; ui_max = 5.0; > = float2(0.0, 0.0);
uniform float2 desatB < ui_label = "Desaturate Blue"; ui_tooltip = "DAY    |    NIGHT"; ui_type = "drag"; ui_min = 0.0; ui_max = 5.0; > = float2(0.0, 0.0);
uniform float2 ctemp_intensity < ui_label = "Color Temperature Intensity"; ui_tooltip = "DAY    |    NIGHT"; ui_type = "drag"; ui_min = 0.0; ui_max = 1.0; ui_spacing = 3; > = float2(0.0, 0.0);
uniform int2 ctemp < ui_label = "Color Temperature (K)"; ui_tooltip = "DAY    |    NIGHT"; ui_type = "slider"; ui_min = 1500; ui_max = 15000; > = int2(6600, 6600);
uniform int sep < ui_label = " "; ui_type = "radio"; ui_text = "\n\n"; noedit = true; >;

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
texture n < source = "QuantV/18690.png"; >
{
    Width = 64;
    Height = 64;
    Format = R8;
};
sampler2D n_s
{
    Texture = n;
};

#define vd(x,y) lerp(lerp(y, x, smoothstep(5.0, 5.5, saturate(tex2Dfetch(BackBuffer, int2(0, 1)).w) * 23.99999)), y, smoothstep(21.0, 21.5, saturate(tex2Dfetch(BackBuffer, int2(0, 1)).w) * 23.99999))
void PostProcessVS(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD)
{
    texcoord.x = (id == 2) ? 2.0 : 0.0;
    texcoord.y = (id == 1) ? 2.0 : 0.0;
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
void PostProcessVS0(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD, out nointerpolation float4 d0 : TEXCOORD1, out nointerpolation float4 d1 : TEXCOORD2, out nointerpolation float4 d2 : TEXCOORD3, out nointerpolation float4 d3 : TEXCOORD4, out nointerpolation float4 d4 : TEXCOORD5)
{
    texcoord.x = (id == 2) ? 2.0 : 0.0;
    texcoord.y = (id == 1) ? 2.0 : 0.0;
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    d0 = vd(float4(Tint_d, Tinti.x), float4(Tint_n, Tinti.y));
    d1 = vd(float4(qBright.x, qContrast.x, qGamma.x, qSaturation.x), float4(qBright.y, qContrast.y, qGamma.y, qSaturation.y));
    d2 = vd(float4(desatR.x, desatG.x, desatB.x, 0), float4(desatR.y, desatG.y, desatB.y, 1));
    d3 = vd(float4(ColorMood_day, ColorMoodIntensity.x), float4(ColorMood_night, ColorMoodIntensity.y));
    d4 = vd(float4(ctemp_intensity.x, ctemp.x, 0.0, 0.0), float4(ctemp_intensity.y, ctemp.y, 0.0, 0.0));
}
void PostProcessVS1(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD, out nointerpolation float4 d0 : TEXCOORD1)
{
    texcoord.x = (id == 2) ? 2.0 : 0.0;
    texcoord.y = (id == 1) ? 2.0 : 0.0;
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    d0 = float4(vd(0, 1), 0.0, 0.0, 0.0);
}
float4 PS_ColAA(float4 position : SV_Position, float2 texcoord : TEXCOORD, nointerpolation float4 d0 : TEXCOORD1, nointerpolation float4 d1 : TEXCOORD2, nointerpolation float4 d2 : TEXCOORD3, nointerpolation float4 d3 : TEXCOORD4, nointerpolation float4 d4 : TEXCOORD5) : SV_Target
{
    float4 res = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0));
    float d = tex2Dlod(DepthBuffer, float4(texcoord, 0.0, 0.0)).x == 0.0;
    d += tex2Dlod(DepthBuffer, float4(texcoord, 0.0, 0.0), int2(1, 0)).x == 0.0;
    d += tex2Dlod(DepthBuffer, float4(texcoord, 0.0, 0.0), int2(0, 1)).x == 0.0;
    d += tex2Dlod(DepthBuffer, float4(texcoord, 0.0, 0.0), int2(-1, 0)).x == 0.0;
    d += tex2Dlod(DepthBuffer, float4(texcoord, 0.0, 0.0), int2(0, -1)).x == 0.0;
    d = saturate(d * 0.2);
    float2 uv = texcoord - float2(0.499, 0.64);
    if (position.x > 1 && position.y > 1) res.w = saturate(d + pow(dot(uv, uv), 2.0) * 2.0);
    d = (d > 0.0) ? res.w * d2.w : res.w;
    if ((d < 1.0) && AntiAliasing)
    {
        float3 A = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0), int2(-1, 0)).xyz;
        float3 B = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0), int2(1, 0)).xyz;
        float3 C = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0), int2(0, -1)).xyz;
        float3 D = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0), int2(0, 1)).xyz;
        float3 mixX = (A + B) * 2.0;
        float3 mixY = (C + D) * 2.0;
        float lumX = abs(mixX.y - res.y * 4.0) * 0.25;
        float lumY = abs(mixY.y - res.y * 4.0) * 0.25;
        float3 aaX = (mixX + res.xyz * 2.0) * 0.1666667;
        float3 aaY = (mixY + res.xyz * 2.0) * 0.1666667;
        float3 QAA = lerp(res.xyz, aaX, saturate(lumY / aaX.y));
        QAA = lerp(QAA, aaY, saturate(lumX / aaY.y) * 0.5);
        res.xyz = lerp(QAA, res.xyz, d);
    }
    res.xyz = lerp(res.xyz, res.xyz * d0.xyz * 2.55, d0.w);
    res.xyz *= d1.x;
    res.xyz = lerp(res.xyz, 0.5 * (1.0 + sin((res.xyz - 0.5) * 3.1415926)), d1.y);
    res.xyz = pow(abs(res.xyz), d1.z);
    res.xyz = saturate(lerp(dot(res.xyz, float3(0.2126, 0.7152, 0.0722)), res.xyz, d1.w));
    float sr = saturate((res.x - res.z - res.y + res.x) * d2.x);
    float sg = saturate((res.y - res.x - res.z + res.y) * d2.y);
    float sb = saturate((res.z - res.x - res.y + res.z) * d2.z);
    float fLum = dot(res.xyz, 0.3333333);
    float3 colMood = lerp(d3.xyz * saturate(fLum * 2.0), 1.0, saturate(fLum - 0.5) * 2.0);
    res.xyz = lerp(res.xyz, colMood, saturate(fLum * d3.w));
    if ((position.x < 1) && (position.y < 3)) res.xyz = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0), int2(1, 0)).xyz;
    res.xyz = lerp(res.xyz, dot(res.xyz, float3(0.2126, 0.7152, 0.0722)), saturate(sr + sg + sb));
    const float3 colb[4] = { float3(1.0, 0.2196078, 0.000000001) * 2.45, float3(1.0, 0.6981, 0.447) * 1.39, float3(1.0, 1.0, 1.0), float3(0.7098039, 0.8039215, 1.0) * 1.193447 };
    const float gsz[3] = { 0.1666667, 0.3333333, 0.3333333 };
    const float cumh[3] = { 0.1666667, 0.5, 0.8333333 };
    float qta = saturate(pow(d4.y, 2.0) * -0.0000000021258503 + d4.y * 0.000105442 - 0.103316);
    uint id = (0.1666666 < qta) + (0.5 < qta);
    float3 temp = lerp(colb[id], colb[id + 1], (qta - (cumh[id] - gsz[id])) / gsz[id]);
    if (d4.x > 0.0001) res.xyz = lerp(res.xyz, saturate(res.xyz * temp), d4.x);
    if (UseLevels) res.xyz = (res.xyz - iBlack) / (iWhite - iBlack) * (oWhite - oBlack) + oBlack;
    return res;
}
float4 PS_FXAA(float4 position : SV_Position, float2 texcoord : TEXCOORD, nointerpolation float4 d0 : TEXCOORD1) : SV_Target
{
// ==============================================================================
// NVIDIA FXAA 3.11 for Ray-MMD converted and originaly made by TIMOTHY LOTTES
// ------------------------------------------------------------------------------
// Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
// ------------------------------------------------------------------------------
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
//
// * Neither the name of NVIDIA CORPORATION nor the names of its
//   contributors may be used to endorse or promote products derived
//   from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// ==============================================================================
    float SubPix = 0.1;
    float EdgeThresh = 0.0;
    float EdgeThreshMin = 0.0;
    float2 posM = texcoord;
    float4 rgbyM = tex2Dlod(BackBuffer, float4(posM, 0.0, 0.0));
    float d = rgbyM.w * d0.x;
    float2 uv = posM - float2(0.499, 0.64);
    d = saturate(d + pow(dot(uv, uv), 2.0) * 2.0);
    [branch] if (d == 1.0 || !AntiAliasing) return rgbyM;
    float4 luma4A = tex2DgatherG(BackBuffer, posM);
    float4 luma4B = tex2DgatherG(BackBuffer, posM, int2(-1, -1));
    float maxSM = max(luma4A.x, rgbyM.y);
    float minSM = min(luma4A.x, rgbyM.y);
    float maxESM = max(luma4A.z, maxSM);
    float minESM = min(luma4A.z, minSM);
    float maxWN = max(luma4B.z, luma4B.x);
    float minWN = min(luma4B.z, luma4B.x);
    float rangeMax = max(maxWN, maxESM);
    float rangeMin = min(minWN, minESM);
    float rangeMaxScaled = rangeMax * EdgeThresh;
    float range = rangeMax - rangeMin;
    float rangeMaxClamped = max(EdgeThreshMin, rangeMaxScaled);
    bool earlyExit = range < rangeMaxClamped;
    if (earlyExit) return rgbyM;
    float lumaNE = tex2Dlod(BackBuffer, float4(posM, 0.0, 0.0), int2(1, -1)).y;
    float lumaSW = tex2Dlod(BackBuffer, float4(posM, 0.0, 0.0), int2(-1, 1)).y;
    float lumaNS = luma4B.z + luma4A.x;
    float lumaWE = luma4B.x + luma4A.z;
    float subpixRcpRange = rcp(range);
    float subpixNSWE = lumaNS + lumaWE;
    float edgeHorz1 = -2.0 * rgbyM.y + lumaNS;
    float edgeVert1 = -2.0 * rgbyM.y + lumaWE;

    float lumaNESE = lumaNE + luma4A.y;
    float lumaNWNE = luma4B.w + lumaNE;
    float edgeHorz2 = -2.0 * luma4A.z + lumaNESE;
    float edgeVert2 = -2.0 * luma4B.z + lumaNWNE;

    float lumaNWSW = luma4B.w + lumaSW;
    float lumaSWSE = lumaSW + luma4A.y;
    float edgeHorz4 = abs(edgeHorz1) * 2.0 + abs(edgeHorz2);
    float edgeVert4 = abs(edgeVert1) * 2.0 + abs(edgeVert2);
    float edgeHorz3 = -2.0 * luma4B.x + lumaNWSW;
    float edgeVert3 = -2.0 * luma4A.x + lumaSWSE;
    float edgeHorz = abs(edgeHorz3) + edgeHorz4;
    float edgeVert = abs(edgeVert3) + edgeVert4;

    float subpixNWSWNESE = lumaNWSW + lumaNESE;
    float lengthSign = rcpFrame.x;
    bool horzSpan = edgeHorz >= edgeVert;
    float subpixA = subpixNSWE * 2.0 + subpixNWSWNESE;

    if (!horzSpan) luma4B.z = luma4B.x;
    if (!horzSpan) luma4A.x = luma4A.z;
    if (horzSpan) lengthSign = rcpFrame.y;
    float subpixB = subpixA * (1.0 / 12.0) - rgbyM.y;

    float gradientN = luma4B.z - rgbyM.y;
    float gradientS = luma4A.x - rgbyM.y;
    float lumaNN = luma4B.z + rgbyM.y;
    float lumaSS = luma4A.x + rgbyM.y;
    bool pairN = abs(gradientN) >= abs(gradientS);
    float gradient = max(abs(gradientN), abs(gradientS));
    if (pairN) lengthSign = -lengthSign;
    float subpixC = saturate(abs(subpixB) * subpixRcpRange);

    float2 posB;
    posB.x = posM.x;
    posB.y = posM.y;
    float2 offNP;
    offNP.x = (!horzSpan) ? 0.0 : rcpFrame.x;
    offNP.y = (horzSpan) ? 0.0 : rcpFrame.y;
    if (!horzSpan) posB.x += lengthSign * 0.5;
    if (horzSpan) posB.y += lengthSign * 0.5;

    float2 posN;
    posN.x = posB.x - offNP.x;
    posN.y = posB.y - offNP.y;
    float2 posP;
    posP.x = posB.x + offNP.x;
    posP.y = posB.y + offNP.y;
    float subpixD = -2.0 * subpixC + 3.0;
    float lumaEndN = tex2Dlod(BackBuffer, float4(posN, 0.0, 0.0)).y;
    float subpixE = subpixC * subpixC;
    float lumaEndP = tex2Dlod(BackBuffer, float4(posP, 0.0, 0.0)).y;

    if (!pairN) lumaNN = lumaSS;
    float gradientScaled = gradient * 1.0 / 4.0;
    float lumaMM = rgbyM.y - lumaNN * 0.5;
    float subpixF = subpixD * subpixE;
    bool lumaMLTZero = lumaMM < 0.0;

    lumaEndN -= lumaNN * 0.5;
    lumaEndP -= lumaNN * 0.5;
    bool doneN = abs(lumaEndN) >= gradientScaled;
    bool doneP = abs(lumaEndP) >= gradientScaled;
    if (!doneN) posN.x -= offNP.x;
    if (!doneN) posN.y -= offNP.y;
    bool doneNP = (!doneN) || (!doneP);
    if (!doneP) posP.x += offNP.x;
    if (!doneP) posP.y += offNP.y;

    if (doneNP)
    {
        if (!doneN) lumaEndN = tex2Dlod(BackBuffer, float4(posN.xy, 0.0, 0.0)).y;
        if (!doneP) lumaEndP = tex2Dlod(BackBuffer, float4(posP.xy, 0.0, 0.0)).y;
        if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
        if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
        doneN = abs(lumaEndN) >= gradientScaled;
        doneP = abs(lumaEndP) >= gradientScaled;
        if (!doneN) posN.x -= offNP.x;
        if (!doneN) posN.y -= offNP.y;
        doneNP = (!doneN) || (!doneP);
        if (!doneP) posP.x += offNP.x;
        if (!doneP) posP.y += offNP.y;

        if (doneNP)
        {
            if (!doneN) lumaEndN = tex2Dlod(BackBuffer, float4(posN.xy, 0.0, 0.0)).y;
            if (!doneP) lumaEndP = tex2Dlod(BackBuffer, float4(posP.xy, 0.0, 0.0)).y;
            if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
            if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
            doneN = abs(lumaEndN) >= gradientScaled;
            doneP = abs(lumaEndP) >= gradientScaled;
            if (!doneN) posN.x -= offNP.x;
            if (!doneN) posN.y -= offNP.y;
            doneNP = (!doneN) || (!doneP);
            if (!doneP) posP.x += offNP.x;
            if (!doneP) posP.y += offNP.y;

            if (doneNP)
            {
                if (!doneN) lumaEndN = tex2Dlod(BackBuffer, float4(posN.xy, 0.0, 0.0)).y;
                if (!doneP) lumaEndP = tex2Dlod(BackBuffer, float4(posP.xy, 0.0, 0.0)).y;
                if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
                if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
                doneN = abs(lumaEndN) >= gradientScaled;
                doneP = abs(lumaEndP) >= gradientScaled;
                if (!doneN) posN.x -= offNP.x;
                if (!doneN) posN.y -= offNP.y;
                doneNP = (!doneN) || (!doneP);
                if (!doneP) posP.x += offNP.x;
                if (!doneP) posP.y += offNP.y;

                if (doneNP)
                {
                    if (!doneN) lumaEndN = tex2Dlod(BackBuffer, float4(posN.xy, 0.0, 0.0)).y;
                    if (!doneP) lumaEndP = tex2Dlod(BackBuffer, float4(posP.xy, 0.0, 0.0)).y;
                    if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
                    if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
                    doneN = abs(lumaEndN) >= gradientScaled;
                    doneP = abs(lumaEndP) >= gradientScaled;
                    if (!doneN) posN.x -= offNP.x * 1.5;
                    if (!doneN) posN.y -= offNP.y * 1.5;
                    doneNP = (!doneN) || (!doneP);
                    if (!doneP) posP.x += offNP.x * 1.5;
                    if (!doneP) posP.y += offNP.y * 1.5;

                    if (doneNP)
                    {
                        if (!doneN) lumaEndN = tex2Dlod(BackBuffer, float4(posN.xy, 0.0, 0.0)).y;
                        if (!doneP) lumaEndP = tex2Dlod(BackBuffer, float4(posP.xy, 0.0, 0.0)).y;
                        if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
                        if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
                        doneN = abs(lumaEndN) >= gradientScaled;
                        doneP = abs(lumaEndP) >= gradientScaled;
                        if (!doneN) posN.x -= offNP.x * 2.0;
                        if (!doneN) posN.y -= offNP.y * 2.0;
                        doneNP = (!doneN) || (!doneP);
                        if (!doneP) posP.x += offNP.x * 2.0;
                        if (!doneP) posP.y += offNP.y * 2.0;

                        if (doneNP)
                        {
                            if (!doneN) lumaEndN = tex2Dlod(BackBuffer, float4(posN.xy, 0.0, 0.0)).y;
                            if (!doneP) lumaEndP = tex2Dlod(BackBuffer, float4(posP.xy, 0.0, 0.0)).y;
                            if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
                            if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
                            doneN = abs(lumaEndN) >= gradientScaled;
                            doneP = abs(lumaEndP) >= gradientScaled;
                            if (!doneN) posN.x -= offNP.x * 2.0;
                            if (!doneN) posN.y -= offNP.y * 2.0;
                            doneNP = (!doneN) || (!doneP);
                            if (!doneP) posP.x += offNP.x * 2.0;
                            if (!doneP) posP.y += offNP.y * 2.0;

                            if (doneNP)
                            {
                                if (!doneN) lumaEndN = tex2Dlod(BackBuffer, float4(posN.xy, 0.0, 0.0)).y;
                                if (!doneP) lumaEndP = tex2Dlod(BackBuffer, float4(posP.xy, 0.0, 0.0)).y;
                                if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
                                if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
                                doneN = abs(lumaEndN) >= gradientScaled;
                                doneP = abs(lumaEndP) >= gradientScaled;
                                if (!doneN) posN.x -= offNP.x * 2.0;
                                if (!doneN) posN.y -= offNP.y * 2.0;
                                doneNP = (!doneN) || (!doneP);
                                if (!doneP) posP.x += offNP.x * 2.0;
                                if (!doneP) posP.y += offNP.y * 2.0;

                                if (doneNP)
                                {
                                    if (!doneN) lumaEndN = tex2Dlod(BackBuffer, float4(posN.xy, 0.0, 0.0)).y;
                                    if (!doneP) lumaEndP = tex2Dlod(BackBuffer, float4(posP.xy, 0.0, 0.0)).y;
                                    if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
                                    if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
                                    doneN = abs(lumaEndN) >= gradientScaled;
                                    doneP = abs(lumaEndP) >= gradientScaled;
                                    if (!doneN) posN.x -= offNP.x * 2.0;
                                    if (!doneN) posN.y -= offNP.y * 2.0;
                                    doneNP = (!doneN) || (!doneP);
                                    if (!doneP) posP.x += offNP.x * 2.0;
                                    if (!doneP) posP.y += offNP.y * 2.0;

                                    if (doneNP)
                                    {
                                        if (!doneN) lumaEndN = tex2Dlod(BackBuffer, float4(posN.xy, 0.0, 0.0)).y;
                                        if (!doneP) lumaEndP = tex2Dlod(BackBuffer, float4(posP.xy, 0.0, 0.0)).y;
                                        if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
                                        if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
                                        doneN = abs(lumaEndN) >= gradientScaled;
                                        doneP = abs(lumaEndP) >= gradientScaled;
                                        if (!doneN) posN.x -= offNP.x * 4.0;
                                        if (!doneN) posN.y -= offNP.y * 4.0;
                                        doneNP = (!doneN) || (!doneP);
                                        if (!doneP) posP.x += offNP.x * 4.0;
                                        if (!doneP) posP.y += offNP.y * 4.0;

                                        if (doneNP)
                                        {
                                            if (!doneN) lumaEndN = tex2Dlod(BackBuffer, float4(posN.xy, 0.0, 0.0)).y;
                                            if (!doneP) lumaEndP = tex2Dlod(BackBuffer, float4(posP.xy, 0.0, 0.0)).y;
                                            if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
                                            if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
                                            doneN = abs(lumaEndN) >= gradientScaled;
                                            doneP = abs(lumaEndP) >= gradientScaled;
                                            if (!doneN) posN.x -= offNP.x * 8.0;
                                            if (!doneN) posN.y -= offNP.y * 8.0;
                                            doneNP = (!doneN) || (!doneP);
                                            if (!doneP) posP.x += offNP.x * 8.0;
                                            if (!doneP) posP.y += offNP.y * 8.0;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    float dstN = posM.x - posN.x;
    float dstP = posP.x - posM.x;
    if (!horzSpan) dstN = posM.y - posN.y;
    if (!horzSpan) dstP = posP.y - posM.y;

    bool goodSpanN = (lumaEndN < 0.0) != lumaMLTZero;
    float spanLength = (dstP + dstN);
    bool goodSpanP = (lumaEndP < 0.0) != lumaMLTZero;
    float spanLengthRcp = rcp(spanLength);

    bool directionN = dstN < dstP;
    float dst = min(dstN, dstP);
    bool goodSpan = directionN ? goodSpanN : goodSpanP;
    float subpixG = subpixF * subpixF;
    float pixelOffset = dst * -spanLengthRcp + 0.5;
    float subpixH = subpixG * SubPix;

    float pixelOffsetGood = goodSpan ? pixelOffset : 0.0;
    float pixelOffsetSubpix = max(pixelOffsetGood, subpixH);
    if (!horzSpan) posM.x += pixelOffsetSubpix * lengthSign;
    if (horzSpan) posM.y += pixelOffsetSubpix * lengthSign;
    return float4(lerp(tex2Dlod(BackBuffer, float4(posM, 0.0, 0.0)).xyz, rgbyM.xyz, d), rgbyM.w);
}
float4 PS_CASN(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
// Copyright(c) 2019 Advanced Micro Devices, Inc.All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
    float4 res = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0));
    if (res.w < 1.0)
    {
        float3 A = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0), int2(-1, -1)).xyz;
        float3 B = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0), int2(0, -1)).xyz;
        float3 C = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0), int2(1, -1)).xyz;
        float3 D = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0), int2(-1, 0)).xyz;
        float3 E = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0), int2(1, 0)).xyz;
        float3 F = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0), int2(-1, 1)).xyz;
        float3 G = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0), int2(0, 1)).xyz;
        float3 H = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0), int2(1, 1)).xyz;
        float3 minCol = min(min(min(D, res.xyz), min(E, B)), G);
        minCol += min(minCol, min(min(A, C), min(F, H)));
        float3 maxCol = max(max(max(D, res.xyz), max(E, B)), G);
        maxCol += max(maxCol, max(max(A, C), max(F, H)));
        float3 mix = -rcp(rsqrt(saturate(min(minCol, 2.0 - maxCol) * rcp(maxCol))) * 8.0);
        float3 cas = saturate((((B + D) + (E + G)) * mix + res.xyz) * rcp(1.0 + 4.0 * mix));
        res.xyz = lerp(lerp(res.xyz, cas, Sharpen), res.xyz, res.w);
    }
    int2 p = position.xy + timer * float2(1.13, 1.27);
    float n = tex2Dfetch(n_s, p & 63).x * 2.0 - 1.0;
    float lum = dot(res.xyz, float3(0.2126, 0.7152, 0.0722));
    res.xyz += lerp(n, 0.0, pow(lum + smoothstep(0.9, 0.0, lum), 4.0)) * Noise;
    //res.xyz *= (n - 0.5) * saturate(-(lum / (lum + 1.0)) * 4.0 + 1.0) * Noise * 5.0 + 1.0;
    return res;
}

technique QuantV_PostFX < ui_tooltip = "QuantV_PostFX"; >
{
    pass p0
    {
        VertexShader = PostProcessVS0;
        PixelShader = PS_ColAA;
    }
    pass p1
    {
        VertexShader = PostProcessVS1;
        PixelShader = PS_FXAA;
    }
    pass p2
    {
        VertexShader = PostProcessVS;
        PixelShader = PS_CASN;
    }
}
