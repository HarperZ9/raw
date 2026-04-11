//----------------------------------------------------------------------------------------------//
//                                                                                              //
//         PrePass_SSR.fxh — Screen-Space Reflections                                          //
//         ENB of the Elders — Zain Dana Harper                                                 //
//                                                                                              //
//  View-space ray march with binary refinement. Fresnel-weighted, edge-faded.                 //
//  Uses FieldOfView + ScreenSize for view reconstruction (prepass has no MatrixProjection).    //
//                                                                                              //
//  Runs after main prepass + addons so reflections show the fully processed scene              //
//  (AO, GI, fog, SSS, snow, particles, etc. are all visible in reflections).                  //
//                                                                                              //
//  Host: enbeffectprepass.fx (HDR, float16). Reflections naturally get                        //
//  bloom diffusion, DOF bokeh, and tonemapping applied downstream.                            //
//                                                                                              //
//----------------------------------------------------------------------------------------------//

#ifndef _PREPASS_SSR_
#define _PREPASS_SSR_
#define SSR_LOADED 1


//=============================================================================//
//                         UI PARAMETERS                                       //
//=============================================================================//

bool ui_SSR_Enable
<
    string UIName = "SSR | Enable";
> = {false};

float ui_SSR_Intensity
<
    string UIName = "SSR | Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.05;
> = {0.5};

float ui_SSR_MaxDist
<
    string UIName = "SSR | Max Ray Distance";
    string UIWidget = "Spinner";
    float UIMin = 10.0; float UIMax = 1500.0; float UIStep = 10.0;
> = {300.0};

int ui_SSR_Steps
<
    string UIName = "SSR | March Steps";
    string UIWidget = "Spinner";
    int UIMin = 16; int UIMax = 128; int UIStep = 8;
> = {64};

int ui_SSR_RefineSteps
<
    string UIName = "SSR | Refinement Steps";
    string UIWidget = "Spinner";
    int UIMin = 0; int UIMax = 16; int UIStep = 1;
> = {10};

float ui_SSR_Thickness
<
    string UIName = "SSR | Thickness (depth tolerance)";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 50.0; float UIStep = 0.5;
> = {5.0};

float ui_SSR_F0
<
    string UIName = "SSR | Fresnel F0 (base reflectivity)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.04};

float ui_SSR_WaterBoost
<
    string UIName = "SSR | Water/Floor Boost";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.05;
> = {0.5};

float ui_SSR_Roughness
<
    string UIName = "SSR | Roughness (blur reflections)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.1;
> = {0.5};

float ui_SSR_EdgeFade
<
    string UIName = "SSR | Screen Edge Fade";
    string UIWidget = "Spinner";
    float UIMin = 0.01; float UIMax = 0.5; float UIStep = 0.01;
> = {0.15};

float ui_SSR_DepthFade
<
    string UIName = "SSR | Depth Fade (far = less)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.7};

bool ui_SSR_Jitter
<
    string UIName = "SSR | Spatial Jitter";
> = {false};

float ui_SSR_BilateralStr
<
    string UIName = "SSR | Bilateral Denoise";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.0};


//=============================================================================//
//              VIEW-SPACE RECONSTRUCTION (FOV-based, no matrices)             //
//=============================================================================//
//  Uses GetLinearDepth/GetWorldZ from the prepass host.
//  World-Z view space: camera at origin, +Z forward, z = world distance [1,3000].
//  Normals from NormalFromDepth() are compatible (uniform scaling preserves normals).

float SSR_WorldZ(float2 uv)
{
    return GetWorldZ(GetLinearDepth(uv));
}

float3 SSR_ViewPos(float2 uv)
{
    float wz = SSR_WorldZ(uv);
    float tanHFov = tan(FieldOfView * 0.5 * PI / 180.0);
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    return float3(ndc.x * ScreenSize.z * tanHFov * wz,
                  ndc.y * tanHFov * wz,
                  wz);
}

float2 SSR_ViewToUV(float3 vp)
{
    float tanHFov = tan(FieldOfView * 0.5 * PI / 180.0);
    float invZ = 1.0 / max(vp.z, 0.01);
    float ndcX = vp.x * invZ / (ScreenSize.z * tanHFov);
    float ndcY = vp.y * invZ / tanHFov;
    return float2(ndcX * 0.5 + 0.5, -ndcY * 0.5 + 0.5);
}


//=============================================================================//
//                         HELPERS                                             //
//=============================================================================//

float SSR_Fresnel(float NdotV, float f0)
{
    float x = 1.0 - NdotV;
    float x2 = x * x;
    return f0 + (1.0 - f0) * x2 * x2 * x;
}

float SSR_IGNoise(float2 pixelPos)
{
    return frac(52.9829189 * frac(dot(pixelPos, float2(0.06711056, 0.00583715))));
}


//=============================================================================//
//                         RAY MARCH                                           //
//=============================================================================//

struct SSRHit
{
    bool  found;
    float2 uv;
    float confidence;
};

SSRHit SSR_March(float3 origin, float3 dir, float maxDist, int steps, int refineSteps,
                  float thickness, float2 pixelPos, bool jitter)
{
    SSRHit hit;
    hit.found = false;
    hit.uv = float2(0, 0);
    hit.confidence = 0.0;

    float stepLen = maxDist / float(steps);

    // Spatial jitter to reduce banding (no temporal component — no accumulation buffer)
    float jitterOfs = jitter ? SSR_IGNoise(pixelPos) * stepLen : 0.0;
    float3 rayPos = origin + dir * jitterOfs;

    for (int i = 1; i <= steps; i++)
    {
        rayPos += dir * stepLen;

        // Stop if ray goes behind camera (worldZ < near plane)
        if (rayPos.z < 1.0) break;

        // Project to screen
        float2 rayUV = SSR_ViewToUV(rayPos);

        // Out of screen?
        if (any(rayUV < 0.0) || any(rayUV > 1.0)) break;

        // Depth comparison in world-Z space
        float sceneZ = SSR_WorldZ(rayUV);
        float penetration = rayPos.z - sceneZ;

        // Hit: ray just passed behind a surface, within thickness
        if (penetration > 0.0 && penetration < thickness)
        {
            hit.found = true;
            hit.uv = rayUV;

            // Binary refinement
            float3 refPos = rayPos;
            float refStep = stepLen * 0.5;

            for (int j = 0; j < refineSteps; j++)
            {
                refPos -= dir * refStep;
                float2 refUV = SSR_ViewToUV(refPos);

                if (any(refUV < 0.0) || any(refUV > 1.0))
                {
                    refPos += dir * refStep;
                    refStep *= 0.5;
                    continue;
                }

                float refSceneZ = SSR_WorldZ(refUV);
                float refPen = refPos.z - refSceneZ;

                if (refPen > 0.0 && refPen < thickness)
                {
                    hit.uv = refUV;
                    refStep *= 0.5;
                }
                else
                {
                    refPos += dir * refStep;
                    refStep *= 0.5;
                }
            }

            // Confidence based on final depth match tightness (use refined position)
            float finalZ = SSR_WorldZ(hit.uv);
            float finalPen = abs(refPos.z - finalZ);
            hit.confidence = 1.0 - saturate(finalPen / thickness);
            // Suppress hits where the ray traveled mostly along the screen plane
            hit.confidence *= saturate(abs(dir.z) * 4.0);

            break;
        }
    }

    return hit;
}


//=============================================================================//
//                         PIXEL SHADER                                        //
//=============================================================================//

float4 PS_SSR(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = TextureColor.SampleLevel(smpLinear, txcoord, 0).rgb;

    [branch] if (!ui_SSR_Enable)
        return float4(color, 1.0);

    // Skip sky pixels (reversed-Z: far ≈ 0)
    float rawDepth = TextureDepth.SampleLevel(smpPoint, txcoord, 0).x;
    if (rawDepth < 0.0001)
        return float4(color, 1.0);

    // Depth fade
    float linDepth = GetLinearDepth(txcoord);
    float depthFade = 1.0 - smoothstep(0.1, 0.6, linDepth * ui_SSR_DepthFade * 3.0);
    if (depthFade < 0.01)
        return float4(color, 1.0);

    // Reconstruct view-space position (world-Z scale) and normal
    float3 viewPos = SSR_ViewPos(txcoord);
    float3 viewNormal = NormalFromDepth(txcoord);

    // View direction: camera at origin, pixel at viewPos
    float3 viewDir = normalize(viewPos);

    // NdotV for Fresnel (dot of normal with direction toward camera)
    float NdotV = saturate(dot(viewNormal, -viewDir));

    // Water/floor boost: upward-facing surfaces get higher reflectivity
    float f0 = ui_SSR_F0;
    float upness = saturate(viewNormal.y);
    float waterBoost = smoothstep(0.6, 0.95, upness) * ui_SSR_WaterBoost;
    f0 = lerp(f0, max(f0, 0.5), waterBoost);

    float fresnel = SSR_Fresnel(NdotV, f0);

    // Early exit if reflection contribution is negligible
    if (fresnel * ui_SSR_Intensity * depthFade < 0.01)
        return float4(color, 1.0);

    // Reflection direction
    float3 reflDir = normalize(reflect(viewDir, viewNormal));

    // Offset start along normal to avoid self-intersection
    float3 startPos = viewPos + viewNormal * max(0.5, viewPos.z * 0.002);

    // Ray march
    SSRHit hit = SSR_March(startPos, reflDir, ui_SSR_MaxDist, ui_SSR_Steps,
                            ui_SSR_RefineSteps, ui_SSR_Thickness,
                            pos.xy, ui_SSR_Jitter);

    if (hit.found)
    {
        // Sample at higher mip for rougher surfaces to smooth noise
        float reflMip = ui_SSR_Roughness * (1.0 - hit.confidence * 0.5);
        float3 reflColor = TextureColor.SampleLevel(smpLinear, hit.uv, reflMip).rgb;

        // Roughness-dependent bilateral denoise: gather around hit point
        // Mirrors skip filtering, rough surfaces get depth-guided 5-tap blur.
        // Ref: Stachowiak 2015 (Frostbite SSR, roughness-scaled filter)
        if (ui_SSR_BilateralStr > 0.001 && ui_SSR_Roughness > 0.1)
        {
            float2 pxSize = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z);
            float filterScale = ui_SSR_Roughness * ui_SSR_BilateralStr * 2.0;
            float hitDepth = TextureDepth.SampleLevel(smpPoint, hit.uv, 0).x;

            float3 accum = reflColor;
            float wTotal = 1.0;

            static const float2 SSR_TAPS[4] = {
                float2(1,0), float2(-1,0), float2(0,1), float2(0,-1)
            };

            [unroll]
            for (int t = 0; t < 4; t++)
            {
                float2 tapUV = hit.uv + SSR_TAPS[t] * pxSize * filterScale;
                float3 tapCol = TextureColor.SampleLevel(smpLinear, tapUV, reflMip).rgb;
                float tapDepth = TextureDepth.SampleLevel(smpPoint, tapUV, 0).x;

                float wDepth = DN_DepthWeight(hitDepth, tapDepth, 0.005);
                float wLuma = DN_LuminanceWeight(reflColor, tapCol, 0.3);
                float w = 0.25 * wDepth * wLuma;

                accum += tapCol * w;
                wTotal += w;
            }

            reflColor = accum / wTotal;
        }

        // Screen edge fade
        float2 edgeDist = min(hit.uv, 1.0 - hit.uv);
        float edgeFade = saturate(min(edgeDist.x, edgeDist.y) / max(ui_SSR_EdgeFade, 0.001));

        // Direction fade: reflections nearly parallel to screen are less reliable
        float dirFade = saturate(abs(reflDir.z) * 3.0);

        // Combine fade factors
        float reflStrength = fresnel * edgeFade * dirFade * depthFade
                           * hit.confidence * ui_SSR_Intensity;

        // Energy-conserving blend
        color = lerp(color, reflColor, saturate(reflStrength));
    }

    return float4(color, 1.0);
}


#endif // _PREPASS_SSR_
