// DofPass.hlsl: Physical 35mm Full-Frame Thin-Lens Cinema Depth of Field (SM 6.9 — Silent Horizons / ADOF Parity).
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
    const int2 c = int2(dims.xy * 0.5f);
    const int2 dMax = int2(dims.xy) - 1;
    const int sx1 = max(int(dims.x * 0.012f), 6);
    const int sy1 = max(int(dims.y * 0.012f), 5);
    const int sx2 = max(int(dims.x * 0.032f), 16);
    const int sy2 = max(int(dims.y * 0.032f), 12);
    const int2 o[13] = {
        int2(0, 0),
        int2(sx1, 0), int2(-sx1, 0), int2(0, sy1), int2(0, -sy1),
        int2(sx1, sy1), int2(-sx1, sy1), int2(sx1, -sy1), int2(-sx1, -sy1),
        int2(sx2, 0), int2(-sx2, 0), int2(0, sy2), int2(0, -sy2)
    };
    float zVals[13];
    [unroll] for (int k = 0; k < 13; k++) {
        const float dk = Depth.Load(int3(clamp(c + o[k], int2(0, 0), dMax), 0));
        const float zk = dk < 1.0f ? viewZOf(dk) : 12000.0f;
        zVals[k] = zk < 42.0f ? 12000.0f : zk;
    }
    [unroll] for (int i = 1; i < 13; i++) {
        const float key = zVals[i];
        int j = i - 1;
        [unroll] for (int s = 0; s < 12; s++) {
            if (j >= 0 && zVals[j] > key) { zVals[j + 1] = zVals[j]; j--; }
        }
        zVals[j + 1] = key;
    }
    // Smooth blend between 30th-percentile (subject lock) and median (zVals[6]) prevents instant background hijack:
    return lerp(zVals[3], zVals[6], 0.35f);
}

// Full DSLR / Medium Format Physical Thin-Lens CoC Equation (70 Skyrim units = 1 meter):
// Driven live by camOptics[0] (sensorWidthMm 24/36/44/70mm, focalLengthMm 24..200mm, apertureFStop f/0.95..f/16)
// and camOptics[1] (nearBlurScale, farBlurScale, nearBleed, focusSpeed)!
float signedCoC(float curZ, float focusZ, float maxDist) {
    if (maxDist <= 1.0f) { return 0.0f; }
    const float dM = max(curZ, 24.0f) / 70.0f;
    const float fM = clamp(focusZ, 42.0f, 14000.0f) / 70.0f;
    const float sensorW = clamp(camOptics[0].x, 20.0f, 80.0f) * 0.001f;   // 24mm APS-C .. 70mm 6x7 Medium Format
    const float F = clamp(camOptics[0].y, 20.0f, 220.0f) * 0.001f;        // focal length in meters
    const float N = max(clamp(camOptics[0].z, 0.90f, 16.0f), 2.8f);       // refined gameplay f-stop floor (zero macro soup)
    const float cocSensorM = ((dM - fM) / max(dM, 0.25f)) * ((F * F) / (N * max(fM - F, 0.28f)));
    const float cocPx = (cocSensorM / sensorW) * dims.x * clamp(maxDist / 420.0f, 0.20f, 2.2f) * 0.52f;
    const float maxRadFull = max(clamp(dofParams.y, 2.0f, 18.0f) * 2.0f, 4.0f);
    const float absPx = abs(cocPx);
    // Clean tack-sharp focal corridor (1.45px deadzone keeps NPC heads/shoulders/foreground props razor sharp):
    if (absPx <= 1.45f) { return 0.0f; }
    const float nearSnowDamp = (cocPx < 0.0f) ? smoothstep(45.0f, 125.0f, curZ) * 0.55f : 1.0f;
    const float planeScale = (cocPx < 0.0f ? clamp(camOptics[1].x, 0.0f, 1.5f) * nearSnowDamp : clamp(camOptics[1].y, 0.0f, 1.5f));
    const float norm = pow(saturate(((absPx - 1.45f) / maxRadFull) * planeScale), 1.15f);
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
// driven by camOptics[1].w (dofFocusSpeed 0.02..0.30)!
float4 dof_focus(float4 at : SV_Position) : SV_Target0 {
    const float targetZ = clamp(sampleFocusZoneZ(), 42.0f, 14000.0f);
    const float prevZ = SrcA.Load(int3(0, 0, 0)).x;
    if (prevZ < 20.0f || prevZ > 20000.0f) { return float4(targetZ, 0.0f, 0.0f, 1.0f); }
    const float prevD = 1.0f / prevZ;
    const float targD = 1.0f / targetZ;
    const float diffD = abs(targD - prevD);
    const float baseSpd = clamp(camOptics[1].w, 0.02f, 0.30f);
    const float speed = lerp(baseSpd * 0.72f, min(baseSpd * 1.85f, 0.45f), saturate(diffD * 180.0f));
    const float nextD = lerp(prevD, targD, speed);
    return float4(clamp(1.0f / max(nextD, 1.0e-5f), 42.0f, 14000.0f), targetZ, 0.0f, 1.0f);
}

float4 dof_coc(float4 at : SV_Position) : SV_Target0 {
    // Live 3D World-Map & UI Depth Isolation: exact UI::Singleton menuPaused (timeEnv.w) + aerial world-map altitude.
    if (timeEnv.w > 0.5f || camPosW.z > 12500.0f) {
        const float2 fullPx0 = (floor(at.xy) + 0.5f) * 2.0f;
        return float4(SrcA.SampleLevel(Lin, fullPx0 * dims.zw, 0.0f).rgb, 0.0f);
    }
    const float2 lo = floor(at.xy);
    const float2 fullPx = (lo + 0.5f) * 2.0f;
    const int2 dMax = int2(dims.xy) - 1;
    const int2 ip = clamp(int2(fullPx), int2(0, 0), dMax);
    const float fGpu = SrcB.Load(int3(0, 0, 0)).x;
    const float focusZ = (fGpu >= 40.0f && fGpu <= 15000.0f) ? fGpu : sampleFocusZoneZ();
    // Depth-consistent 2x2 downsample: pairs each selected depth tap with its EXACT own RGB color
    // (eliminates bilinear hair/cloak RGB bleeding into bright background CoC -> kills black smudge halos!).
    const int2 p00 = ip;
    const int2 p10 = min(ip + int2(1, 0), dMax);
    const int2 p01 = min(ip + int2(0, 1), dMax);
    const int2 p11 = min(ip + int2(1, 1), dMax);
    const float d00 = Depth.Load(int3(p00, 0));
    const float d10 = Depth.Load(int3(p10, 0));
    const float d01 = Depth.Load(int3(p01, 0));
    const float d11 = Depth.Load(int3(p11, 0));
    float dNear = d00; int2 pNear = p00;
    if (d10 < dNear) { dNear = d10; pNear = p10; }
    if (d01 < dNear) { dNear = d01; pNear = p01; }
    if (d11 < dNear) { dNear = d11; pNear = p11; }
    float dFar = d00; int2 pFar = p00;
    if (d10 > dFar) { dFar = d10; pFar = p10; }
    if (d01 > dFar) { dFar = d01; pFar = p01; }
    if (d11 > dFar) { dFar = d11; pFar = p11; }
    const float zNear = dNear < 1.0f ? viewZOf(dNear) : 100000.0f;
    const float zFar  = dFar < 1.0f ? viewZOf(dFar) : 100000.0f;
    const float2 uvFull = fullPx * dims.zw;
    const float cocNear = zNear < 45.0f ? 0.0f : perspectiveCoC(uvFull, zNear, focusZ, dofParams.x);
    const float cocFar  = zFar < 45.0f ? 0.0f : perspectiveCoC(uvFull, zFar, focusZ, dofParams.x);
    const bool pickNear = (cocNear < -0.05f);
    const float coc = pickNear ? cocNear : cocFar;
    const int2 pCol = pickNear ? pNear : pFar;
    // Average matching-depth taps only when the 2x2 quad is continuous; point-load on silhouette edges!
    const bool quadSmooth = (abs(zFar - zNear) / max(zNear, 1.0f)) < 0.04f;
    const float3 col = quadSmooth ? SrcA.SampleLevel(Lin, uvFull, 0.0f).rgb : SrcA.Load(int3(pCol, 0)).rgb;
    return float4(col, coc);
}

float4 dof_gather(float4 at : SV_Position) : SV_Target0 {
    float w, h;
    SrcA.GetDimensions(w, h);
    const float2 dim = max(float2(w, h), float2(1.0f, 1.0f));
    const int2 dimMax = max(int2(dim) - 1, int2(0, 0));
    const float2 invDim = 1.0f / dim;
    const float2 px = floor(at.xy);
    const float4 center = SrcA.Load(int3(px, 0));
    if (WaveActiveAllTrue(dofParams.x <= 1.0f)) { return float4(center.rgb, 0.0f); }

    const float maxRad = clamp(dofParams.y, 2.0f, 18.0f);
    const float centerCoc = center.a;
    const float2 uvNorm = (px + 0.5f) * invDim * 2.0f - 1.0f;
    const float cornerMag = saturate(dot(uvNorm, uvNorm) * 0.42f);
    const float2 radAxis = length(uvNorm) > 1.0e-4f ? normalize(uvNorm) : float2(1.0f, 0.0f);
    const float2 tanAxis = float2(-radAxis.y, radAxis.x);
    const uint camModeG = uint(engineWorld.w + 0.5f);
    const int shape = (camModeG == 3u) ? 5 : (camModeG == 4u) ? 6 : int(camOptics[0].w + 0.5f);
    const float blades = (shape == 0) ? 5.0f : (shape == 1) ? 6.0f : (shape == 2) ? 7.0f : 9.0f;
    const float kBlade = 6.2831853f / blades;
    const float kCosHalf = cos(3.14159265f / blades);

    const float farGatherRad = max(centerCoc > 0.02f ? centerCoc * maxRad : 0.0f, 0.0f);
    const float nearGatherRad = min(maxRad * 0.65f, 10.0f);

    float3 farAcc = centerCoc > 0.02f ? center.rgb : 0.0f.xxx;
    float  farW   = centerCoc > 0.02f ? 1.0f : 0.0f;
    float3 nearAcc = centerCoc < -0.06f ? center.rgb : 0.0f.xxx;
    float  nearW   = centerCoc < -0.06f ? 1.0f : 0.0f;
    float  nearCov = centerCoc < -0.06f ? 1.0f : 0.0f;

    // 91-tap 5-Ring Concentric Aperture Disc Gather using EXACT POINT LOADS (zero bilinear silhouette contamination ->
    // completely eliminates dark hair/shoulder smudge halos and translucent NPC ghosting!).
    const float kInvRings = 1.0f / 5.0f;
    [loop] for (int ring = 1; ring <= 5; ring++) {
        const float rFrac = float(ring) * kInvRings;
        const int nTaps = ring * 6;
        const float dAng = 6.2831853f / float(nTaps);
        const float angOff = float(ring) * 0.5235988f;
        [loop] for (int t = 0; t < nTaps; t++) {
            const float a = float(t) * dAng + angOff;
            float bladeR = 1.0f;
            if (shape <= 3) {
                const float sec = abs(fmod(a, kBlade) - kBlade * 0.5f);
                bladeR = clamp(kCosHalf / max(cos(sec), 0.72f), 0.88f, 1.12f);
            }
            float2 unitOff = float2(cos(a), sin(a)) * (rFrac * bladeR);
            if (shape == 5) {
                unitOff *= float2(0.68f, 1.38f);
            } else if (shape == 6) {
                unitOff = radAxis * (dot(unitOff, radAxis) * (1.0f - cornerMag * 0.62f))
                        + tanAxis * (dot(unitOff, tanAxis) * (1.0f + cornerMag * 0.45f));
            } else {
                unitOff -= radAxis * (dot(unitOff, radAxis) * cornerMag * 0.28f);
            }

            // 1) Far-field gather (point-loaded from SrcA -> strictly rejects sharp NPC silhouettes & dark hair edges!):
            if (farGatherRad > 0.35f) {
                const int2 tapFPx = clamp(int2(round(px + unitOff * farGatherRad)), int2(0, 0), dimMax);
                const float4 tapF = SrcA.Load(int3(tapFPx, 0));
                if (tapF.a > 0.04f) {
                    const float tapCocF = tapF.a;
                    const float bgOcc = smoothstep(centerCoc * 0.65f, centerCoc * 0.95f + 0.02f, tapCocF);
                    const float discW = smoothstep(rFrac * centerCoc - 0.05f, rFrac * centerCoc + 0.05f, tapCocF);
                    const float lumF = dot(tapF.rgb, float3(0.2126f, 0.7152f, 0.0722f));
                    const float saRingF = 0.65f + 0.70f * (rFrac * rFrac * rFrac);
                    const float orbF = (1.0f + pow(saturate(lumF - 0.45f), 2.0f) * (dofParams.z * 2.4f)) * saRingF;
                    const float3 locaFar = lerp(1.0f.xxx, float3(0.97f, 1.02f, 1.01f), rFrac * rFrac * 0.25f);
                    const float fw = bgOcc * discW * orbF;
                    farAcc += tapF.rgb * locaFar * fw;
                    farW += fw;
                }
            }

            // 2) Near-field gather (point-loaded, strictly gated on genuinely out-of-focus foreground tapN.a < -0.10f):
            const int2 tapNPx = clamp(int2(round(px + unitOff * nearGatherRad)), int2(0, 0), dimMax);
            const float4 tapN = SrcA.Load(int3(tapNPx, 0));
            if (tapN.a < -0.10f) {
                const float nRad = -tapN.a;
                const float covN = smoothstep(rFrac - 0.14f, rFrac + 0.08f, nRad);
                if (covN > 0.001f) {
                    const float lumN = dot(tapN.rgb, float3(0.2126f, 0.7152f, 0.0722f));
                    const float saRingN = 0.65f + 0.70f * (rFrac * rFrac * rFrac);
                    const float orbN = (1.0f + pow(saturate(lumN - 0.45f), 2.0f) * (dofParams.z * 2.0f)) * saRingN;
                    const float nw = covN * orbN;
                    nearAcc += tapN.rgb * nw;
                    nearW += nw;
                    nearCov += covN;
                }
            }
        }
    }

    const float3 farCol = farW > 1.0e-4f ? (farAcc / farW) : center.rgb;
    const float3 nearCol = nearW > 1.0e-4f ? (nearAcc / nearW) : center.rgb;
    const float nearAlpha = saturate(nearCov / 42.0f) * 0.55f;
    const float3 outCol = lerp(farCol, nearCol, nearAlpha);
    return float4(outCol, nearAlpha > 0.04f ? -nearAlpha : saturate(centerCoc));
}

// Jimenez 2014 / UE5 Post-Gather 3x3 Bokeh Flood-Fill & Velvety Tent Filter (reuses m_dofCoc, zero extra VRAM):
// Closes outer-ring sampling micro-gaps at large radii and turns 91-tap discs into creamy, glass-smooth cinema bokeh!
float4 dof_post(float4 at : SV_Position) : SV_Target0 {
    float w, h;
    SrcA.GetDimensions(w, h);
    const float2 invDim = 1.0f / max(float2(w, h), float2(1.0f, 1.0f));
    const float2 uv = (floor(at.xy) + 0.5f) * invDim;
    const float4 c0 = SrcA.SampleLevel(Lin, uv, 0.0f);
    const float coc0 = abs(c0.a);
    if (coc0 < 0.015f) { return c0; }
    // 9-tap circular tent + highlight-preserving flood-fill (keeps luminous bokeh discs crisp & creamy):
    static const float2 kOff[8] = {
        float2(-1.0f, -1.0f), float2( 0.0f, -1.0f), float2( 1.0f, -1.0f),
        float2(-1.0f,  0.0f),                       float2( 1.0f,  0.0f),
        float2(-1.0f,  1.0f), float2( 0.0f,  1.0f), float2( 1.0f,  1.0f)
    };
    static const float kW[8] = { 0.5f, 1.0f, 0.5f, 1.0f, 1.0f, 0.5f, 1.0f, 0.5f };
    const float spread = lerp(0.55f, 1.15f, saturate(coc0));
    float3 acc = c0.rgb * 2.0f;
    float wSum = 2.0f;
    float aAcc = c0.a * 2.0f;
    [unroll] for (int i = 0; i < 8; i++) {
        const float4 t = SrcA.SampleLevel(Lin, saturate(uv + kOff[i] * invDim * spread), 0.0f);
        // Prevent sharp background/foreground contamination across depth edges:
        const float sameSign = (c0.a * t.a >= 0.0f) ? 1.0f : 0.15f;
        const float wi = kW[i] * sameSign * (0.35f + 0.65f * smoothstep(0.0f, 0.20f, abs(t.a)));
        acc += t.rgb * wi;
        aAcc += t.a * wi;
        wSum += wi;
    }
    return float4(acc / wSum, aAcc / wSum);
}

float4 dof_comp(float4 at : SV_Position) : SV_Target0 {
    const int2 ip = int2(at.xy);
    const float4 sharp = SrcA.Load(int3(ip, 0));
    if (dofParams.x <= 1.0f || timeEnv.w > 0.5f || camPosW.z > 12500.0f) { return sharp; }
    const float2 uv = (floor(at.xy) + 0.5f) * dims.zw;
    const float fGpu = FocusTex.Load(int3(0, 0, 0)).x;
    const float focusZ = (fGpu >= 40.0f && fGpu <= 15000.0f) ? fGpu : sampleFocusZoneZ();
    const float d = Depth.Load(int3(ip, 0));
    const float curZ = d < 1.0f ? viewZOf(d) : 100000.0f;
    const uint camModeC = uint(engineWorld.w + 0.5f);
    if (camModeC == 3u && (uv.y < 0.122f || uv.y > 0.878f)) { return float4(0.0f, 0.0f, 0.0f, sharp.a); }   // True 2.39:1 Cinemascope framing
    // Keep 1st-person hands/weapons/torches (< 45u) razor-sharp:
    if (curZ < 45.0f) { return sharp; }
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
        const float tc = bTaps[bi].a;
        const float compat = (coc > 0.03f) ? smoothstep(-0.02f, 0.05f, tc)
                           : ((coc < -0.03f) ? smoothstep(0.02f, -0.05f, tc) : (tc < -0.10f ? 0.5f : 0.05f));
        const float w = bilW[bi] * (0.04f + 0.96f * compat);
        bAcc += bTaps[bi] * w;
        bWSum += w;
    }
    const float4 bokeh = bWSum > 1.0e-4f ? (bAcc / bWSum) : float4(sharp.rgb, 0.0f);
    // Tack-sharp focal-plane protection: strictly zero far-field bokeh bleed when absCoc < 0.04!
    const float maxRadFull = max(clamp(dofParams.y, 2.0f, 18.0f) * 2.0f, 4.0f);
    const float cocPx = absCoc * maxRadFull;
    const float farBlend = coc > 0.04f ? smoothstep(1.0f, 3.6f, cocPx) : 0.0f;
    const float nearSelf = coc < -0.04f ? smoothstep(1.0f, 3.6f, cocPx) : 0.0f;
    const float nearBleed = bokeh.a < -0.12f ? smoothstep(0.14f, 0.85f, -bokeh.a) * 0.45f * clamp(camOptics[1].z, 0.0f, 1.0f) : 0.0f;
    const float blend = max(farBlend, max(nearSelf, nearBleed));
    return float4(lerp(sharp.rgb, bokeh.rgb, blend), sharp.a);
}
