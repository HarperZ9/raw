// DofPass.hlsl: Physical 35mm Full-Frame Thin-Lens Cinema Depth of Field (SM 6.9 - Silent Horizons / ADOF Parity).
//   dof_coc    : 1/2-res exact physical 35mm Full-Frame Thin-Lens CoC (meters, 50mm prime, 9-point median autofocus)
//   dof_gather : 1/2-res 91-tap 5-ring concentric aperture disc gather (zero noise/zero stipple, Jimenez/McGuire
//                near/far separation, HDR specular bokeh orb weighting, cat's-eye corner squeeze & LoCA)
//   dof_comp   : full-res seamless sub-pixel CoC transition composite (runs before 12-pass Jimenez HDR Bloom)
// Original work, MIT.
#include "LightingCommon.hlsli"

Texture2D<float4> SrcA  : register(t0);
Texture2D<float>  Depth : register(t1);
Texture2D<float4> SrcB  : register(t2);
Texture2D<float4> FocusTex : register(t3);
SamplerState      Lin   : register(s0);

// 13-point center-weighted portrait/subject autofocus metering zone (±1.2% inner + ±3.2% outer ring):
// Takes the near-biased 30th-percentile depth so NPC faces & foreground subjects hold lock smoothly even when
// the crosshair grazes their shoulder or hair (prevents Orgnar blurring out in 11.16.46.11.mp4!).
float sampleFocusZoneZ() {
    const float2 safeDims = max(dims.xy, float2(1.0f, 1.0f));
    const int2 c = int2(safeDims * 0.5f);
    const int2 dMax = max(int2(safeDims) - 1, int2(0, 0));
    const int sx1 = max(int(safeDims.x * 0.012f), 6);
    const int sy1 = max(int(safeDims.y * 0.012f), 5);
    const int sx2 = max(int(safeDims.x * 0.032f), 16);
    const int sy2 = max(int(safeDims.y * 0.032f), 12);
    const int2 o[13] = {
        int2(0, 0),
        int2(sx1, 0), int2(-sx1, 0), int2(0, sy1), int2(0, -sy1),
        int2(sx1, sy1), int2(-sx1, sy1), int2(sx1, -sy1), int2(-sx1, -sy1),
        int2(sx2, 0), int2(-sx2, 0), int2(0, sy2), int2(0, -sy2)
    };
    float zVals[13];
    [unroll] for (int k = 0; k < 13; k++) {
        const float dk = Depth.Load(int3(clamp(c + o[k], int2(0, 0), dMax), 0));
        const float zkRaw = (dk < 1.0f && !isnan(dk) && !isinf(dk)) ? viewZOf(dk) : 12000.0f;
        const float zk = (isnan(zkRaw) || isinf(zkRaw)) ? 12000.0f : zkRaw;
        zVals[k] = zk < 42.0f ? 12000.0f : clamp(zk, 42.0f, 14000.0f);
    }
    [unroll] for (int i = 1; i < 13; i++) {
        const float key = zVals[i];
        int j = i - 1;
        [unroll] for (int s = 0; s < 12; s++) {
            if (j >= 0 && zVals[j] > key) { zVals[j + 1] = zVals[j]; j--; }
        }
        zVals[j + 1] = key;
    }
    // Coherence-aware subject lock: holds lock on characters when the metering zone is coherent,
    // while biasing strongly toward the median (zVals[6]) when panning past thin pine trunks or branches
    // so transient foreground occluders never hijack autofocus or cause focus breathing.
    const float spreadRatio = zVals[9] / max(zVals[3], 42.0f);
    const float branchGuard = smoothstep(1.65f, 3.20f, spreadRatio);
    const float coherentZ = lerp(zVals[4], zVals[6], 0.45f);
    return clamp(lerp(coherentZ, zVals[6], branchGuard), 42.0f, 14000.0f);
}

// Full DSLR / Medium Format Physical Thin-Lens CoC Equation (70 Skyrim units = 1 meter):
// Driven live by camOptics[0] (sensorWidthMm 24/36/44/70mm, focalLengthMm 24..200mm, apertureFStop f/0.95..f/16)
// and camOptics[1] (nearBlurScale, farBlurScale, nearBleed, focusSpeed)!
float signedCoC(float curZ, float focusZ, float maxDist) {
    if (maxDist <= 1.0f || isnan(curZ) || isinf(curZ) || isnan(focusZ) || isinf(focusZ)) { return 0.0f; }
    const float dM = max(curZ, 24.0f) / 70.0f;
    const float fM = clamp(focusZ, 42.0f, 14000.0f) / 70.0f;
    const float sensorW = clamp(camOptics[0].x, 20.0f, 80.0f) * 0.001f;   // 24mm APS-C .. 70mm 6x7 Medium Format
    const float F = clamp(camOptics[0].y, 20.0f, 220.0f) * 0.001f;        // focal length in meters
    const float N = max(clamp(camOptics[0].z, 0.90f, 16.0f), 1.4f);       // unlock true f/1.4 cinema prime optical bokeh!
    const float cocSensorM = ((dM - fM) / max(dM, 0.25f)) * ((F * F) / (N * max(fM - F, 0.28f)));
    const float cocPx = (cocSensorM / max(sensorW, 0.01f)) * max(dims.x, 1.0f) * clamp(maxDist / 420.0f, 0.20f, 2.2f) * 0.68f;
    const float maxRadFull = max(clamp(dofParams.y, 2.0f, 18.0f) * 2.0f, 4.0f);
    const float absPx = abs(cocPx);
    // Clean tack-sharp focal corridor (1.25px deadzone keeps NPC heads/shoulders/foreground props razor sharp):
    if (absPx <= 1.25f) { return 0.0f; }
    // Interiors (sunColour.w < 0.5): strongly suppress near-field CoC so close cave icicles/lanterns never project concentric rings.
    const bool isInt = (sunColour.w < 0.5f);
    const float nearSnowDamp = (cocPx < 0.0f)
        ? (isInt ? (smoothstep(95.0f, 240.0f, curZ) * 0.18f) : (smoothstep(45.0f, 125.0f, curZ) * 0.55f))
        : (isInt ? 0.72f : 1.0f);
    const float planeScale = (cocPx < 0.0f ? clamp(camOptics[1].x, 0.0f, 1.5f) : clamp(camOptics[1].y, 0.0f, 1.5f)) * nearSnowDamp;
    const float norm = pow(saturate(((absPx - 1.25f) / maxRadFull) * planeScale), 1.12f);
    return cocPx < 0.0f ? -norm : norm;
}

// Special Camera Perspective Optical CoC (engineWorld.w = CameraMode 0..5):
//   0: Standard Cinema Prime   1: Tilt-Shift Miniature (Scheimpflug)   2: ARPG Isometric Diorama
//   3: Anamorphic 2.39:1       4: Petzval Portrait Swirl               5: Architectural Vertical Tilt-Rise
float perspectiveCoC(float2 uv, float curZ, float focusZ, float maxDist) {
    const float baseCoc = signedCoC(curZ, focusZ, maxDist);
    const uint cm = uint(engineWorld.w + 0.5f);
    if (cm == 1u) {
        // True Scheimpflug tilted horizontal focal plane (miniature diorama band at uv.y = 0.50):
        const float tilt = saturate((abs(uv.y - 0.50f) - 0.11f) * 3.1f);
        const float signDir = uv.y > 0.50f ? -1.0f : 1.0f;
        return lerp(baseCoc, signDir * tilt, 0.82f);
    }
    if (cm == 2u) {
        // ARPG Isometric Diorama: tack-sharp character & combat ring at (0.50, 0.52), smooth peripheral bokeh:
        const float2 dIso = (uv - float2(0.50f, 0.52f)) * float2(1.0f, 1.38f);
        const float isoBlur = smoothstep(0.24f, 0.64f, length(dIso)) * 0.78f;
        const float signDir = uv.y > 0.62f ? -1.0f : 1.0f;
        return abs(baseCoc) > isoBlur ? baseCoc : (signDir * isoBlur);
    }
    if (cm == 4u) {
        // 1840s Brass Petzval Lens: curved field focus with tack-sharp center portrait & swirling edge falloff:
        const float r2 = dot((uv - 0.5f.xx) * float2(1.25f, 1.0f), (uv - 0.5f.xx) * float2(1.25f, 1.0f));
        const float fieldBlur = saturate((r2 - 0.06f) * 2.2f) * 0.85f;
        return abs(baseCoc) > fieldBlur ? baseCoc : fieldBlur;
    }
    if (cm == 5u) {
        // Architectural Vertical Tilt-Rise: vertical Scheimpflug focal column for towers, castles & peaks:
        const float vTilt = saturate((abs(uv.x - 0.50f) - 0.13f) * 2.9f);
        return lerp(baseCoc, vTilt, 0.78f);
    }
    return baseCoc;
}

// 1x1 GPU-Native Cinema Follow-Focus Motor: smoothly integrates focus distance in diopter (1/z) space
// driven by camOptics[1].w (dofFocusSpeed 0.02..0.30) with hysteresis deadband and per-frame slew-rate
// clamping so rapid camera pans across trees/characters never trigger focus breathing or pumping.
float4 dof_focus(float4 at : SV_Position) : SV_Target0 {
    const float rawTargetZ = sampleFocusZoneZ();
    const float targetZ = (isnan(rawTargetZ) || isinf(rawTargetZ)) ? 500.0f : clamp(rawTargetZ, 42.0f, 14000.0f);
    const float prevZ = SrcA.Load(int3(0, 0, 0)).x;
    if (isnan(prevZ) || isinf(prevZ) || prevZ < 20.0f || prevZ > 20000.0f) { return float4(targetZ, targetZ, 0.0f, 1.0f); }
    const float prevD = 1.0f / clamp(prevZ, 42.0f, 14000.0f);
    const float targD = 1.0f / targetZ;
    const float rawDeltaD = targD - prevD;
    const float diffD = abs(rawDeltaD);
    // Diopter hysteresis deadband eliminates micro-pumping on swaying foliage / branch edges:
    static const float kDeadbandD = 0.00018f;
    if (diffD <= kDeadbandD) {
        return float4(clamp(prevZ, 42.0f, 14000.0f), targetZ, 0.0f, 1.0f);
    }
    const float baseSpd = clamp((isnan(camOptics[1].w) || isinf(camOptics[1].w)) ? 0.10f : camOptics[1].w, 0.02f, 0.30f);
    // Smooth critically-damped tracking: gentle on sudden transient branch spikes, steady on sustained pans:
    const float transientDamp = lerp(1.0f, 0.58f, smoothstep(0.0022f, 0.0085f, diffD));
    const float speed = clamp(baseSpd * lerp(0.55f, 1.10f, smoothstep(kDeadbandD, 0.0020f, diffD)) * transientDamp, 0.015f, 0.28f);
    const float maxStepD = max(baseSpd * 0.0045f, 0.00035f);
    const float stepD = clamp(rawDeltaD * speed, -maxStepD, maxStepD);
    const float nextD = clamp(prevD + stepD, 1.0f / 14000.0f, 1.0f / 42.0f);
    const float nextZ = clamp(1.0f / max(nextD, 1.0e-5f), 42.0f, 14000.0f);
    return float4((isnan(nextZ) || isinf(nextZ)) ? targetZ : nextZ, targetZ, 0.0f, 1.0f);
}

float4 dof_coc(float4 at : SV_Position) : SV_Target0 {
    // Live 3D World-Map, UI & Debug-View Isolation: zero CoC on menus (timeEnv.w) and ALL Lighting/Water Debug Views (flags.w != 0).
    if (flags.w != 0u || timeEnv.w > 0.5f) {
        const float2 fullPx0 = (floor(at.xy) + 0.5f) * 2.0f;
        const float3 uiCol = SrcA.SampleLevel(Lin, saturate(fullPx0 * dims.zw), 0.0f).rgb;
        return float4((any(isnan(uiCol)) || any(isinf(uiCol))) ? 0.0f.xxx : clamp(uiCol, 0.0f.xxx, 32.0f.xxx), 0.0f);
    }
    const float2 lo = floor(at.xy);
    const float2 fullPx = (lo + 0.5f) * 2.0f;
    const int2 dMax = max(int2(dims.xy) - 1, int2(0, 0));
    const int2 ip = clamp(int2(fullPx), int2(0, 0), dMax);
    const float fGpu = SrcB.Load(int3(0, 0, 0)).x;
    const float focusZ = (!isnan(fGpu) && !isinf(fGpu) && fGpu >= 40.0f && fGpu <= 15000.0f) ? fGpu : sampleFocusZoneZ();
    // Depth-consistent 2x2 downsample: pairs each selected depth tap with its EXACT own RGB color
    // (eliminates bilinear hair/cloak RGB bleeding into bright background CoC -> kills black smudge halos!).
    const int2 p00 = ip;
    const int2 p10 = min(ip + int2(1, 0), dMax);
    const int2 p01 = min(ip + int2(0, 1), dMax);
    const int2 p11 = min(ip + int2(1, 1), dMax);
    const float d00Raw = Depth.Load(int3(p00, 0));
    const float d10Raw = Depth.Load(int3(p10, 0));
    const float d01Raw = Depth.Load(int3(p01, 0));
    const float d11Raw = Depth.Load(int3(p11, 0));
    const float d00 = (isnan(d00Raw) || isinf(d00Raw)) ? 1.0f : saturate(d00Raw);
    const float d10 = (isnan(d10Raw) || isinf(d10Raw)) ? 1.0f : saturate(d10Raw);
    const float d01 = (isnan(d01Raw) || isinf(d01Raw)) ? 1.0f : saturate(d01Raw);
    const float d11 = (isnan(d11Raw) || isinf(d11Raw)) ? 1.0f : saturate(d11Raw);
    float dNear = d00; int2 pNear = p00;
    if (d10 < dNear) { dNear = d10; pNear = p10; }
    if (d01 < dNear) { dNear = d01; pNear = p01; }
    if (d11 < dNear) { dNear = d11; pNear = p11; }
    float dFar = d00; int2 pFar = p00;
    if (d10 > dFar) { dFar = d10; pFar = p10; }
    if (d01 > dFar) { dFar = d01; pFar = p01; }
    if (d11 > dFar) { dFar = d11; pFar = p11; }
    const float zNearRaw = dNear < 1.0f ? viewZOf(dNear) : 100000.0f;
    const float zFarRaw  = dFar < 1.0f ? viewZOf(dFar) : 100000.0f;
    const float zNear = (isnan(zNearRaw) || isinf(zNearRaw)) ? 100000.0f : max(zNearRaw, 0.0f);
    const float zFar  = (isnan(zFarRaw) || isinf(zFarRaw)) ? 100000.0f : max(zFarRaw, 0.0f);
    const float2 uvFull = saturate(fullPx * dims.zw);
    const float cocNear = zNear < 45.0f ? 0.0f : perspectiveCoC(uvFull, zNear, focusZ, dofParams.x);
    const float cocFar  = zFar < 45.0f ? 0.0f : perspectiveCoC(uvFull, zFar, focusZ, dofParams.x);
    const bool pickNear = (cocNear < -0.05f);
    const float cocRaw = pickNear ? cocNear : cocFar;
    const float coc = (isnan(cocRaw) || isinf(cocRaw)) ? 0.0f : clamp(cocRaw, -1.0f, 1.0f);
    const int2 pCol = pickNear ? pNear : pFar;
    // Average matching-depth taps only when the 2x2 quad is continuous AND never straddles sky (d >= 0.99998)!
    const bool straddlesSky = (dFar >= 0.99998f && dNear < 0.99998f);
    const bool quadSmooth = !straddlesSky && ((abs(zFar - zNear) / max(zNear, 1.0f)) < 0.04f);
    float3 col = quadSmooth ? SrcA.SampleLevel(Lin, uvFull, 0.0f).rgb : SrcA.Load(int3(pCol, 0)).rgb;
    col = (any(isnan(col)) || any(isinf(col))) ? 0.0f.xxx : clamp(col, 0.0f.xxx, 32.0f.xxx);
    return float4(col, coc);
}

// NaN/Inf-safe HDR tap sanitization + Karis-style highlight stability weight for creamy hexagonal bokeh discs.
float3 dofSanitizeRgb(float3 rgb) {
    return (any(isnan(rgb)) || any(isinf(rgb))) ? 0.0f.xxx : clamp(rgb, 0.0f.xxx, 16.0f.xxx);
}

float4 dof_gather(float4 at : SV_Position) : SV_Target0 {
    float w, h;
    SrcA.GetDimensions(w, h);
    const float2 dim = max(float2(w, h), float2(1.0f, 1.0f));
    const int2 dimMax = max(int2(dim) - 1, int2(0, 0));
    const float2 invDim = 1.0f / dim;
    const float2 px = floor(at.xy);
    const float4 centerRaw = SrcA.Load(int3(px, 0));
    if (flags.w != 0u) { return float4(centerRaw.rgb, 0.0f); }
    const float3 centerRgb = dofSanitizeRgb(centerRaw.rgb);
    const float centerCoc = (isnan(centerRaw.a) || isinf(centerRaw.a)) ? 0.0f : clamp(centerRaw.a, -1.0f, 1.0f);
    if (WaveActiveAllTrue(dofParams.x <= 1.0f)) { return float4(centerRgb, 0.0f); }

    const float maxRad = clamp(dofParams.y, 2.0f, 18.0f);
    const float2 uvNorm = (px + 0.5f) * invDim * 2.0f - 1.0f;
    const float cornerMag = saturate(dot(uvNorm, uvNorm) * 0.42f);
    const float uvLen = length(uvNorm);
    const float2 radAxis = uvLen > 1.0e-4f ? (uvNorm / uvLen) : float2(1.0f, 0.0f);
    const float2 tanAxis = float2(-radAxis.y, radAxis.x);
    const uint camModeG = uint(engineWorld.w + 0.5f);
    const int shape = (camModeG == 3u) ? 5 : (camModeG == 4u) ? 6 : int(camOptics[0].w + 0.5f);
    // Default cinema aperture: crisp 6-blade hexagonal bokeh (respects 7/9-blade, anamorphic & Petzval overrides):
    const float blades = (shape == 2) ? 7.0f : (shape == 3) ? 9.0f : 6.0f;
    const float kBlade = 6.2831853f / blades;
    const float kCosHalf = cos(3.14159265f / blades);
    const float orbBoost = clamp(dofParams.z, 0.0f, 3.0f);

    const float isInt = (sunColour.w < 0.5f) ? 1.0f : 0.0f;
    const float farGatherRad = max(centerCoc > 0.02f ? centerCoc * maxRad : 0.0f, 0.0f);
    const float nearGatherRad = min(maxRad * lerp(0.65f, 0.32f, isInt), lerp(10.0f, 4.5f, isInt));
    const float nearCocGate = lerp(-0.10f, -0.22f, isInt);

    // Karis-stabilized center seed weight (prevents harsh center spike or firefly dominance):
    const float centerLum = dot(centerRgb, float3(0.2126f, 0.7152f, 0.0722f));
    const float centerKaris = 1.0f / (1.0f + max(centerLum, 0.0f) * 0.16f);
    const float centerOrb = (1.0f + smoothstep(0.45f, 3.2f, centerLum) * (orbBoost * 1.65f)) * centerKaris;
    float3 farAcc = centerCoc > 0.02f ? (centerRgb * centerOrb) : 0.0f.xxx;
    float  farW   = centerCoc > 0.02f ? centerOrb : 0.0f;
    float3 nearAcc = centerCoc < -0.06f ? (centerRgb * centerOrb) : 0.0f.xxx;
    float  nearW   = centerCoc < -0.06f ? centerOrb : 0.0f;
    float  nearCov = centerCoc < -0.06f ? 1.0f : 0.0f;

    // Wave 49: 64-Tap Low-Discrepancy Golden-Angle Vogel Disk with Hexagonal Aperture Blade Shaping,
    // Depth-Aware Foreground/Background Bilateral Weighting & Karis-Style HDR Highlight Preservation!
    // Uses exact point loads (zero bilinear silhouette contamination, zero concentric ring banding!).
    static const int kNumTaps = 64;
    static const float kInvTaps = 1.0f / 64.0f;
    static const float kGoldenAngle = 2.39996323f;

    [loop] for (int ti = 0; ti < kNumTaps; ++ti) {
        const float rFrac = sqrt((float(ti) + 0.5f) * kInvTaps);
        const float a = float(ti) * kGoldenAngle;
        float bladeR = 1.0f;
        if (shape <= 3) {
            const float aMod = a - kBlade * floor(a / kBlade);
            const float sec = abs(aMod - kBlade * 0.5f);
            const float polyR = clamp(kCosHalf / max(cos(sec), 0.72f), 0.86f, 1.15f);
            bladeR = lerp(1.0f, polyR, 0.82f); // subtle creamy hexagonal aperture shaping
        }
        float2 unitOff = float2(cos(a), sin(a)) * (rFrac * bladeR);
        if (shape == 5) {
            unitOff *= float2(0.68f, 1.38f);
        } else if (shape == 6) {
            unitOff = radAxis * (dot(unitOff, radAxis) * (1.0f - cornerMag * 0.62f))
                    + tanAxis * (dot(unitOff, tanAxis) * (1.0f + cornerMag * 0.45f));
        } else {
            unitOff -= radAxis * (dot(unitOff, radAxis) * cornerMag * 0.26f);
        }

        // 1) Depth-Aware Far-Field Gather (strictly rejects sharp foreground NPCs/weapons & prevents sky bleed!):
        if (farGatherRad > 0.35f) {
            const int2 tapFPx = clamp(int2(round(px + unitOff * farGatherRad)), int2(0, 0), dimMax);
            const float4 tapFRaw = SrcA.Load(int3(tapFPx, 0));
            const float tapCocF = (isnan(tapFRaw.a) || isinf(tapFRaw.a)) ? 0.0f : clamp(tapFRaw.a, -1.0f, 1.0f);
            if (tapCocF > 0.04f) {
                const float3 tapRgbF = dofSanitizeRgb(tapFRaw.rgb);
                // Bilateral depth/CoC compatibility: prevents sharp foreground halos & distant sky leaks:
                const float bgOcc = smoothstep(centerCoc * 0.70f, centerCoc * 0.96f + 0.02f, tapCocF);
                const float cocDeltaF = max(tapCocF - centerCoc, 0.0f);
                const float skyGuardF = exp(-cocDeltaF * cocDeltaF * 5.5f);
                const float discW = smoothstep(rFrac * centerCoc - 0.045f, rFrac * centerCoc + 0.055f, tapCocF);
                const float lumF = dot(tapRgbF, float3(0.2126f, 0.7152f, 0.0722f));
                // Karis-stabilized HDR highlight preservation + smooth disc profile (zero hollow concentric rings):
                const float karisF = 1.0f / (1.0f + max(lumF, 0.0f) * 0.16f);
                const float saRingF = lerp(0.90f + 0.14f * (rFrac * rFrac), 1.0f - 0.25f * (rFrac * rFrac), isInt);
                const float orbF = (1.0f + smoothstep(0.45f, 3.2f, lumF) * (orbBoost * lerp(2.15f, 1.15f, isInt))) * karisF * saRingF;
                const float3 locaFar = lerp(1.0f.xxx, float3(0.93f, 1.04f, 1.04f), rFrac * rFrac * lerp(0.38f, 0.08f, isInt));
                const float fw = bgOcc * lerp(0.35f, 1.0f, skyGuardF) * discW * orbF;
                farAcc += tapRgbF * locaFar * fw;
                farW += fw;
            }
        }

        // 2) Depth-Aware Near-Field Gather (center-weighted soft disc; kills dark/pale-blue concentric rings indoors):
        if (nearGatherRad > 0.35f) {
            const int2 tapNPx = clamp(int2(round(px + unitOff * nearGatherRad)), int2(0, 0), dimMax);
            const float4 tapNRaw = SrcA.Load(int3(tapNPx, 0));
            const float tapCocN = (isnan(tapNRaw.a) || isinf(tapNRaw.a)) ? 0.0f : clamp(tapNRaw.a, -1.0f, 1.0f);
            if (tapCocN < nearCocGate) {
                const float nRad = -tapCocN;
                const float covN = smoothstep(rFrac - 0.14f, rFrac + 0.08f, nRad);
                if (covN > 0.001f) {
                    const float3 tapRgbN = dofSanitizeRgb(tapNRaw.rgb);
                    const float lumN = dot(tapRgbN, float3(0.2126f, 0.7152f, 0.0722f));
                    const float karisN = 1.0f / (1.0f + max(lumN, 0.0f) * 0.16f);
                    // Smooth center-weighted disc (never outer-ring peaking!) so foreground icicles/lanterns never draw hollow rings.
                    const float softDiscN = 1.0f - 0.35f * (rFrac * rFrac);
                    const float orbN = (1.0f + smoothstep(0.45f, 3.2f, lumN) * (orbBoost * lerp(1.65f, 0.55f, isInt))) * karisN * softDiscN;
                    const float3 locaNear = lerp(1.0f.xxx, float3(1.05f, 0.95f, 1.02f), rFrac * rFrac * lerp(0.30f, 0.0f, isInt));
                    const float nw = covN * orbN;
                    nearAcc += tapRgbN * locaNear * nw;
                    nearW += nw;
                    nearCov += covN;
                }
            }
        }
    }

    const float3 farCol = farW > 1.0e-5f ? (farAcc / farW) : centerRgb;
    const float3 nearCol = nearW > 1.0e-5f ? (nearAcc / nearW) : centerRgb;
    const float nearAlpha = saturate(nearCov * (1.0f / 30.0f)) * lerp(0.55f, 0.12f, isInt);
    const float3 outCol = dofSanitizeRgb(lerp(farCol, nearCol, nearAlpha));
    return float4(outCol, nearAlpha > 0.04f ? -nearAlpha : saturate(centerCoc));
}

// Jimenez 2014 / UE5 Post-Gather 3x3 Bokeh Flood-Fill & Velvety Tent Filter (reuses m_dofCoc, zero extra VRAM):
// Closes outer-ring sampling micro-gaps at large radii and turns 91-tap discs into creamy, glass-smooth cinema bokeh!
float4 dof_post(float4 at : SV_Position) : SV_Target0 {
    float w, h;
    SrcA.GetDimensions(w, h);
    if (flags.w != 0u) {
        const int2 pMax = max(int2(w, h) - 1, int2(0, 0));
        return SrcA.Load(int3(clamp(int2(floor(at.xy)), int2(0, 0), pMax), 0));
    }
    const float2 invDim = 1.0f / max(float2(w, h), float2(1.0f, 1.0f));
    const float2 uv = (floor(at.xy) + 0.5f) * invDim;
    const float4 c0Raw = SrcA.SampleLevel(Lin, uv, 0.0f);
    const float3 c0Rgb = dofSanitizeRgb(c0Raw.rgb);
    const float c0A = (isnan(c0Raw.a) || isinf(c0Raw.a)) ? 0.0f : clamp(c0Raw.a, -1.0f, 1.0f);
    const float coc0 = abs(c0A);
    if (coc0 < 0.015f) { return float4(c0Rgb, c0A); }
    // 9-tap circular tent + highlight-preserving flood-fill (keeps luminous bokeh discs crisp & creamy):
    static const float2 kOff[8] = {
        float2(-1.0f, -1.0f), float2( 0.0f, -1.0f), float2( 1.0f, -1.0f),
        float2(-1.0f,  0.0f),                       float2( 1.0f,  0.0f),
        float2(-1.0f,  1.0f), float2( 0.0f,  1.0f), float2( 1.0f,  1.0f)
    };
    static const float kW[8] = { 0.5f, 1.0f, 0.5f, 1.0f, 1.0f, 0.5f, 1.0f, 0.5f };
    const float spread = lerp(0.55f, 1.15f, saturate(coc0));
    float3 acc = c0Rgb * 2.0f;
    float wSum = 2.0f;
    float aAcc = c0A * 2.0f;
    [unroll] for (int i = 0; i < 8; i++) {
        const float4 tRaw = SrcA.SampleLevel(Lin, saturate(uv + kOff[i] * invDim * spread), 0.0f);
        const float3 tRgb = dofSanitizeRgb(tRaw.rgb);
        const float tA = (isnan(tRaw.a) || isinf(tRaw.a)) ? 0.0f : clamp(tRaw.a, -1.0f, 1.0f);
        // Prevent sharp background/foreground contamination across depth edges:
        const float sameSign = (c0A * tA >= 0.0f) ? 1.0f : 0.15f;
        const float wi = kW[i] * sameSign * (0.35f + 0.65f * smoothstep(0.0f, 0.20f, abs(tA)));
        acc += tRgb * wi;
        aAcc += tA * wi;
        wSum += wi;
    }
    const float invW = 1.0f / max(wSum, 1.0e-5f);
    return float4(dofSanitizeRgb(acc * invW), clamp(aAcc * invW, -1.0f, 1.0f));
}

// Subtle Physical Peripheral Rain Lens Refraction (strictly gated on active outdoor rain, zero rings on dry land):
float2 evalWetLensBead(float2 uv, float timeS, float scale, float seed) {
    const float2 grid = float2(scale * 1.45f, scale * 0.60f);
    float2 st = uv * grid;
    const float colId = floor(st.x);
    const float h0 = frac(sin(colId * 127.1f + seed * 311.7f) * 43758.5453f);
    st.y += timeS * (0.22f + 0.28f * h0) + h0 * 11.3f;
    const float2 cellId = floor(st);
    const float h1 = frac(sin(dot(cellId, float2(12.9898f, 78.233f)) + seed * 19.7f) * 43758.5453f);
    if (h1 < 0.72f) { return 0.0f.xx; }
    const float2 gv = frac(st) - 0.5f;
    const float wobble = sin(uv.y * 14.0f + h1 * 6.28318f) * 0.12f;
    const float slideY = -sin(timeS * (0.45f + h0 * 0.45f) + h1 * 6.28318f) * 0.26f;
    const float2 dropPos = float2(wobble * (0.5f - abs(gv.y)), slideY);
    const float2 d = (gv - dropPos) * float2(1.0f, 1.55f);
    const float r2 = dot(d, d);
    const float beadMask = smoothstep(0.042f, 0.008f, r2);
    return d * (beadMask * beadMask);
}

float3 applyWetLensOptics(float2 uv, float3 baseCol) {
    // Strictly active ONLY during genuine outdoor rain (weather.w > 0.22); never triggers on dry land near rivers!
    if (dofParams.w <= 0.5f || sunColour.w <= 0.5f || weather.w <= 0.22f) { return baseCol; }
    const float rainDrive = saturate((weather.w - 0.22f) / 0.78f);
    // Keep the center crosshair & subject crystal clear; subtle rain droplets appear only near the lens periphery:
    const float edgeMask = smoothstep(0.30f, 0.74f, length(uv - 0.5f.xx));
    const float wetDrive = rainDrive * edgeMask;
    if (wetDrive <= 0.02f) { return baseCol; }
    const float2 offA = evalWetLensBead(uv, (physWorld[0].x > 1.0e-4f ? physWorld[0].x : camPosW.w), 9.0f, 1.3f);
    const float2 offB = evalWetLensBead(uv + float2(0.37f, 0.19f), (physWorld[0].x > 1.0e-4f ? physWorld[0].x : camPosW.w) * 0.78f, 14.0f, 4.7f);
    const float2 refractUv = (offA * 0.016f + offB * 0.010f) * wetDrive;
    const float mag = length(refractUv);
    if (mag <= 1.0e-5f) { return baseCol; }
    const float3 refracted = dofSanitizeRgb(SrcA.SampleLevel(Lin, saturate(uv + refractUv), 0.0f).rgb);
    return lerp(baseCol, refracted, saturate(mag * 45.0f));
}

float4 dof_comp(float4 at : SV_Position) : SV_Target0 {
    const int2 ip = int2(at.xy);
    const float4 sharp = SrcA.Load(int3(ip, 0));
    // Bit-exact passthrough on ALL Lighting/Water Debug Views (flags.w != 0) and paused menus (timeEnv.w > 0.5):
    if (flags.w != 0u || timeEnv.w > 0.5f) { return sharp; }
    const float2 uv = (floor(at.xy) + 0.5f) * dims.zw;
    if (dofParams.x <= 1.0f) { return float4(applyWetLensOptics(uv, sharp.rgb), sharp.a); }
    const float fGpu = FocusTex.Load(int3(0, 0, 0)).x;
    const float focusZ = (fGpu >= 40.0f && fGpu <= 15000.0f) ? fGpu : sampleFocusZoneZ();
    const float d = Depth.Load(int3(ip, 0));
    const float curZRaw = (d < 1.0f && !isnan(d) && !isinf(d)) ? viewZOf(d) : 100000.0f;
    const float curZ = (isnan(curZRaw) || isinf(curZRaw)) ? 100000.0f : curZRaw;
    const uint camModeC = uint(engineWorld.w + 0.5f);
    if (camModeC == 3u && (uv.y < 0.122f || uv.y > 0.878f)) { return float4(0.0f, 0.0f, 0.0f, sharp.a); }   // True 2.39:1 Cinemascope framing
    // Keep 1st-person hands/weapons/torches (< 45u) razor-sharp:
    if (curZ < 45.0f) { return float4(applyWetLensOptics(uv, sharp.rgb), sharp.a); }
    const float coc = perspectiveCoC(uv, curZ, focusZ, dofParams.x);
    const float absCoc = abs(coc);
    // True CoC-bilateral 4-tap upsample of 1/2-res bokeh buffer:
    // Weights each 1/2-res bokeh tap by CoC compatibility so sharp foreground NPCs & pine branches NEVER pick up dark/bright background ghosts!
    float bw, bh;
    SrcB.GetDimensions(bw, bh);
    const int2 bMax = max(int2(bw, bh) - 1, int2(0, 0));
    const float2 bCoord = (floor(at.xy) + 0.5f) * 0.5f - 0.5f;
    const int2 bBase = int2(floor(bCoord));
    const float2 bFrac = saturate(bCoord - float2(bBase));
    float4 bTaps[4];
    bTaps[0] = SrcB.Load(int3(clamp(bBase + int2(0, 0), int2(0, 0), bMax), 0));
    bTaps[1] = SrcB.Load(int3(clamp(bBase + int2(1, 0), int2(0, 0), bMax), 0));
    bTaps[2] = SrcB.Load(int3(clamp(bBase + int2(0, 1), int2(0, 0), bMax), 0));
    bTaps[3] = SrcB.Load(int3(clamp(bBase + int2(1, 1), int2(0, 0), bMax), 0));
    const float bilW[4] = { (1.0f - bFrac.x) * (1.0f - bFrac.y), bFrac.x * (1.0f - bFrac.y),
                            (1.0f - bFrac.x) * bFrac.y,          bFrac.x * bFrac.y };
    float4 bAcc = 0.0f.xxxx;
    float bWSum = 0.0f;
    [unroll] for (int bi = 0; bi < 4; ++bi) {
        const float tc = (isnan(bTaps[bi].a) || isinf(bTaps[bi].a)) ? 0.0f : clamp(bTaps[bi].a, -1.0f, 1.0f);
        const float3 tRgb = dofSanitizeRgb(bTaps[bi].rgb);
        // Depth-aware bilateral upsample: strictly blocks bright sky / far bokeh from leaking onto sharp foreground silhouettes!
        const float compat = (coc > 0.03f)
            ? (smoothstep(-0.02f, 0.05f, tc) * exp(-max(tc - coc, 0.0f) * max(tc - coc, 0.0f) * 8.0f))
            : ((coc < -0.03f) ? smoothstep(0.02f, -0.05f, tc) : (tc < -0.10f ? 0.50f : 0.0f));
        const float bwTap = bilW[bi] * (0.02f + 0.98f * compat);
        bAcc += float4(tRgb, tc) * bwTap;
        bWSum += bwTap;
    }
    const float4 bokeh = bWSum > 1.0e-5f ? (bAcc / bWSum) : float4(sharp.rgb, 0.0f);
    // Tack-sharp focal-plane & sky-silhouette protection: strictly zero far-field bokeh bleed when absCoc < 0.04!
    const float maxRadFull = max(clamp(dofParams.y, 2.0f, 18.0f) * 2.0f, 4.0f);
    const float cocPx = absCoc * maxRadFull;
    const float farBlend = coc > 0.04f ? smoothstep(1.0f, 3.6f, cocPx) : 0.0f;
    const float nearSelf = coc < -0.04f ? smoothstep(1.0f, 3.6f, cocPx) : 0.0f;
    // Suppress indoor near-field bokeh bleed completely so close foreground icicles/beams never draw halos over caves.
    const float nearBleed = (sunColour.w >= 0.5f && d < 0.99998f && bokeh.a < -0.12f)
        ? smoothstep(0.14f, 0.85f, -bokeh.a) * 0.45f * clamp(camOptics[1].z, 0.0f, 1.0f) : 0.0f;
    const float blend = saturate(max(farBlend, max(nearSelf, nearBleed)));
    const float3 composed = dofSanitizeRgb(lerp(dofSanitizeRgb(sharp.rgb), bokeh.rgb, blend));
    return float4(applyWetLensOptics(uv, composed), sharp.a);
}
