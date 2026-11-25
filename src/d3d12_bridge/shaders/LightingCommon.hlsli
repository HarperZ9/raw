// LightingCommon.hlsli: what the lighting pass (Lighting.hlsl) and its fog pass (LightFog.hlsl)
// share: the uniform, the G-buffer decode, the camera and the sun cascades. The layout of P
// mirrors LightUniform in LightingPass.cpp. Space is the game's camera-relative world space;
// a matrix row dots a column vector. Original work, MIT.

#define MAX_LIGHTS 128
#define TILES_X 16
#define TILES_Y 9
#define SLICES 16

cbuffer P : register(b0) {
    float4 dims;            // W, H, 1/W, 1/H
    float4 proj;            // projA, projB (depth = A - B / viewZ), frame index, bounce strength
    float4 clipToWorld[4];
    float4 worldToClip[4];
    float4 viewToWorld[3];
    float4 resScale;        // x, y: dynamic resolution; z: the game's knee; w: specular scale
    float4 sunDir;          // xyz towards the light; w: 1 with a sun
    float4 sunColour;       // rgb; w: 1 with cascades
    float4 ambient[3];
    float4 cascade[6];      // cascade k: rows 3k .. 3k+2
    float4 cascadeInfo;     // bias 0, bias 1, split depth, atlas side
    uint4  flags;           // x: FLAG_*; y: lights; z: cascades; w: view mode
    float4 cluster;         // near, far, SLICES / log(far / near), sky visibility strength
    float4 tune;            // fog density, fog anisotropy, soft-shadow range, contact length
    float4 tune2;           // AO strength, fog light scale, fog sun scale, fog max distance
    float4 weather;         // wetness, puddles, snow (Weather.hlsli), rain falling now
    float4 camPosW;         // the camera's world position (absolute: world-fixed patterns); w: seconds
    float4 clouds;          // cover 0 to 1, floor height, thickness (absolute world units), shadow strength (CloudCommon.hlsli)
    float4 cloudWind;       // wind x, y (units a second), density scale, history weight
    float4 waterA;          // RtWater.hlsl: reflection strength, caustic strength, absorption a unit, glint strength
    float4 waterB;          // RtWater.hlsl: view (1 reflection, 2 caustics, 3 Fresnel), caustic cell size (units), refraction share, 0
    float4 prevWorldToClip[4];   // last frame's worldToClip (the clouds' history)
    float4 lightPosR[MAX_LIGHTS];   // xyz, radius
    float4 lightColS[MAX_LIGHTS];   // rgb, shadow-mask channel (-1 none)
    uint4  clusters[TILES_X * TILES_Y * SLICES];   // a bit per light
    float4 footTrail[32];           // persistent world-space footprint & terrain-deformation trail (xyz abs, w strength)
    float4 dofParams;               // x: dofMaxDist (0=off), y: dofRadius (px), z: dofOrbBoost, w: weatherLens (0/1)
    float4 timeEnv;                 // x: gameHour (0..24), y: sunElev (-1..+1), z: lightning, w: menuPaused (0/1)
    float4 engineWorld;             // x: cell/worldspace waterHeight, y: camStateId, z: hasEngineCell (0/1), w: unused
    float4 camOptics[2];            // [0]: sensorWidthMm, focalLengthMm, apertureFStop, bokehShape; [1]: nearScale, farScale, nearBleed, focusSpeed
};

static const uint FLAG_MATCH = 1u, FLAG_SOFT = 2u, FLAG_GGX = 4u, FLAG_FOG = 8u, FLAG_CONTACT = 16u, FLAG_AO = 32u,
                  FLAG_CASCADES = 64u, FLAG_SURFACE = 128u, FLAG_SHADOWS = 256u,
                  FLAG_FOG_REPLACES = 512u, FLAG_RT = 1024u, FLAG_SKY = 2048u, FLAG_BOUNCE = 4096u, FLAG_REFLECT = 8192u, FLAG_BOUNCE_IN = 16384u, FLAG_CLOUDS = 32768u, FLAG_REST = 65536u, FLAG_LOD = 131072u, FLAG_ENV = 262144u, FLAG_CHAR = 524288u, FLAG_UNDERWATER = 1048576u;
static const float PI = 3.14159265358979f;

// Shared 160x90x64 3D Froxel volumetric range (single source of truth for FroxelPass.hlsl and LightFog.hlsl).
static const float kFroxNear = 24.0f;
static const float kFroxFar  = 42000.0f;

float froxelSliceZ(float s) {
    return kFroxNear * pow(kFroxFar / kFroxNear, saturate(s / 64.0f));
}

float froxelZToSlice(float z) {
    const float zCl = clamp(z, kFroxNear, kFroxFar);
    return saturate(log(zCl / kFroxNear) / log(kFroxFar / kFroxNear)) * 63.0f;
}

// GPU architectural hardware tier packed into flags.x bits 24..26 (0=Fallback, 1=RTX 3060, 2=4070, 3=4090, 4=5090).
uint hwTier() { return (flags.x >> 24u) & 7u; }

float4 vs(uint i : SV_VertexID) : SV_Position {
    return float4(i == 1u ? 3.0f : -1.0f, i == 2u ? -3.0f : 1.0f, 0.0f, 1.0f);
}

float viewZOf(float d) { return proj.y / max(proj.x - d, 1.0e-7f); }

// Camera-relative world position of a pixel centre and its depth-buffer value.
float3 worldAt(float2 px, float d) {
    const float2 uv = (px + 0.5f) * dims.zw;
    const float4 c = float4(uv.x * resScale.x * 2.0f - 1.0f, (1.0f - uv.y * resScale.y) * 2.0f - 1.0f, d, 1.0f);
    const float4 w = float4(dot(clipToWorld[0], c), dot(clipToWorld[1], c), dot(clipToWorld[2], c), dot(clipToWorld[3], c));
    return w.xyz / w.w;
}

// The eye in the game's camera-relative space. Float4 40 of the per-frame constants, which that space is relative to,
// is the eye in first person but follows the player in third person, so the eye is read from the projection: the
// point clip space sends to infinity, clipToWorld x (0, 0, 1, 0). The third-person primary-ray view traced from the
// origin drew the player about 15 % larger than the game (rt7-hair-2). Past 2,000 units the matrix is taken as not
// projective and the origin stands in.
float3 eyeAt() {
    const float4 e = float4(clipToWorld[0].z, clipToWorld[1].z, clipToWorld[2].z, clipToWorld[3].z);
    const float3 p = abs(e.w) > 1.0e-9f ? e.xyz / e.w : 0.0f.xxx;
    return dot(p, p) < 2000.0f * 2000.0f ? p : 0.0f.xxx;
}

// A camera-relative world point to (pixel x, pixel y, depth-buffer value, view z).
float4 screenOf(float3 p) {
    const float4 q = float4(p, 1.0f);
    const float4 c = float4(dot(worldToClip[0], q), dot(worldToClip[1], q), dot(worldToClip[2], q), dot(worldToClip[3], q));
    const float2 ndc = c.xy / c.w;
    return float4((ndc.x * 0.5f + 0.5f) / resScale.x * dims.x, (0.5f - ndc.y * 0.5f) / resScale.y * dims.y, c.z / c.w, c.w);
}

// The game's normal target: n.xy / sqrt(8 - 8 n.z) + 0.5 in its view space (z into the screen),
// taken to world space with the inverse view rotation.
float3 worldNormal(float4 t) {
    const float2 e = t.xy - 0.5f;
    const float s = min(dot(e, e), 0.25f);
    const float3 nv = float3(e * (4.0f * sqrt(1.0f - 4.0f * s)), 8.0f * s - 1.0f);
    const float3 n = float3(dot(viewToWorld[0].xyz, nv), dot(viewToWorld[1].xyz, nv), dot(viewToWorld[2].xyz, nv));
    return n / max(length(n), 1.0e-6f);
}

// Interleaved gradient noise (Jimenez 2014), turned each frame.
float ign(float2 px) {
    const float2 p = px + 5.588238f * float(uint(proj.z) & 63u);
    return frac(52.9829189f * frac(dot(p, float2(0.06711056f, 0.00583715f))));
}

// True 24-hour astronomical solar elevation sin(elev) and 3D sun vector driven by Sky::currentGameHour (+0x1B0).
// Decouples physical sky scattering, sunrise/sunset/dusk, cloud under-lighting & solar disc from Skyrim's
// ShadowSceneNode->sunLight (which clamps elevation >15 deg and flips to the Moon at night!).
float astroSunElev() {
    return timeEnv.x > 0.01f ? timeEnv.y : sunDir.z;
}

// Exact hemisphere-sign + clock check: returns 1 ONLY when ShadowSceneNode->sunLight is actually the daytime Sun,
// and 0 the instant Skyrim flips ShadowSceneNode->sunLight to the Moon (eliminates night-time sun disc 100%!).
float isRealSun() {
    if (timeEnv.x <= 0.01f) { return step(0.02f, sunDir.z); }
    const float gh = timeEnv.x;
    const float hourGate = smoothstep(5.6f, 6.3f, gh) * (1.0f - smoothstep(18.6f, 19.4f, gh));
    const float hemiOk = gh < 12.5f ? smoothstep(-0.22f, 0.05f, sunDir.x) : smoothstep(0.22f, -0.05f, sunDir.x);
    return hourGate * hemiOk * step(0.01f, sunDir.z);
}

// True 24-hour astronomical solar vector: keeps Skyrim's EXACT engine sun azimuth when isRealSun() > 0.5,
// and cleanly follows the unbroken east->west orbital arc below the horizon at dusk/night/dawn (zero Moon flip!).
float3 astroSunDir() {
    if (timeEnv.x <= 0.01f) { return normalize(sunDir.xyz); }
    const float sinE = clamp(timeEnv.y, -0.95f, 0.95f);
    const float cosE = sqrt(max(1.0f - sinE * sinE, 1.0e-4f));
    const float hourAng = (timeEnv.x - 12.5f) * 0.26179939f;
    const float2 orbHor = normalize(float2(-sin(hourAng), -cos(hourAng) * 0.62f) + float2(1.0e-4f, 0.0f));
    const float2 sHor = sunDir.xy;
    const float sHorLen = length(sHor);
    const float realS = isRealSun();
    const float2 azHor = (sHorLen > 0.05f && realS > 0.5f) ? (sHor / sHorLen) : orbHor;
    return normalize(float3(azHor * cosE, sinE));
}

// Shared live atmospheric palette: locks sky dome, world hemisphere ambient, distant mountain fog, and clouds
// to one coherent physical color system across midday, dawn/dusk, overcast, and night.
void skyPalette(out float3 zenith, out float3 mid, out float3 horiz) {
    const float cover = saturate(clouds.x);
    const float elev = astroSunElev();
    const float lowSun = saturate(1.0f - max(elev, 0.0f) * 3.0f);
    const float3 clearZ = lerp(float3(0.06f, 0.24f, 0.68f), float3(0.07f, 0.10f, 0.28f), lowSun);
    const float3 clearM = lerp(float3(0.22f, 0.48f, 0.84f), float3(0.46f, 0.26f, 0.42f), lowSun);
    const float3 clearH = lerp(float3(0.62f, 0.76f, 0.88f), float3(1.05f, 0.48f, 0.22f), lowSun);
    const float stormW = saturate(cover * 0.85f - 0.25f + weather.w * 0.35f);
    zenith = lerp(clearZ, float3(0.22f, 0.28f, 0.36f), stormW);
    mid    = lerp(clearM, float3(0.30f, 0.36f, 0.44f), stormW);
    horiz  = lerp(clearH, float3(0.38f, 0.44f, 0.52f), stormW);
}

// Unified World-Space Atmosphere & Lighting Oracle (shared by Lighting, LightFog, Clouds, Volumetric, Froxel, RtWater & RtHit):
// Guarantees 100% seamless color, temperature & luminance synchronization across sky dome, mountain aerial fog,
// volumetric clouds, direct sun/moon light, hemisphere ambient, GI bounce, RT reflections, and weather!
void evalWorldAtmosphere(out float3 outSun, out float3 outSkyZ, out float3 outSkyH, out float3 outAmbUp, out float3 outAmbDown) {
    const float elev = astroSunElev();
    const float nightW = smoothstep(0.03f, -0.07f, elev);
    const float goldenW = smoothstep(-0.04f, 0.04f, elev) * (1.0f - smoothstep(0.14f, 0.34f, elev));
    const float highSun = smoothstep(0.20f, 0.48f, elev);
    const float cover = saturate(clouds.x);
    const float overcastW = saturate(cover * 0.82f - 0.10f + weather.w * 0.45f + weather.z * 0.42f);

    // Unified Physical Radiative-Transfer Sun & Hemisphere (replaces Skyrim's 40 inconsistent 2011 TESWeather color tables!):
    // Solar irradiance & spectrum derive directly from astronomical elevation (Kasten-Young air mass + Rayleigh/Mie extinction)
    // and live cloud/storm transmittance so Whiterun, Winterhold, Riften, Solitude & Dawnstar share ONE cohesive lighting system!
    const float elevDeg = max(degrees(asin(saturate(max(elev, 0.02f)))), 1.5f);
    const float airMass = 1.0f / (sin(radians(elevDeg)) + 0.50572f * pow(elevDeg + 6.07995f, -1.6364f));
    const float3 physTrans = exp(-(0.20f * float3(0.175f, 0.410f, 1.0f) + 0.025f * lerp(1.0f, 4.5f, cover)) * airMass);
    const float3 d65Day = lerp(physTrans, float3(0.98f, 0.99f, 1.01f), highSun * 0.72f);
    const float physSunGain = lerp(0.45f, 1.72f, smoothstep(0.01f, 0.45f, elev)) * (1.0f - overcastW * 0.62f);
    float3 daySun = d65Day * physSunGain;
    daySun = lerp(daySun, daySun * float3(1.24f, 0.85f, 0.54f), goldenW * 0.70f);
    daySun = lerp(daySun, dot(daySun, float3(0.2126f, 0.7152f, 0.0722f)).xxx * float3(0.94f, 0.97f, 1.02f), overcastW * 0.65f);
    const float3 moonSun = float3(0.22f, 0.32f, 0.56f) * lerp(0.36f, 0.14f, overcastW);
    outSun = lerp(daySun, moonSun, nightW);

    // Unified sky zenith, horizon & hemisphere ambient (100% locked across sky dome, fog, clouds, water & terrain!):
    float3 sZ, sM, sH;
    skyPalette(sZ, sM, sH);
    const float dayLumScale = lerp(0.11f, 0.68f, smoothstep(0.03f, 0.42f, elev));
    const float3 overcastZ = float3(0.16f, 0.21f, 0.30f) * dayLumScale;
    const float3 overcastH = float3(0.32f, 0.38f, 0.46f) * dayLumScale;
    const float3 snowH = float3(0.40f, 0.45f, 0.52f) * dayLumScale;
    const float3 nightZ = lerp(float3(0.012f, 0.022f, 0.048f), float3(0.022f, 0.032f, 0.056f), overcastW);
    const float3 nightH = lerp(float3(0.024f, 0.040f, 0.074f), float3(0.042f, 0.054f, 0.082f), overcastW);

    float3 dayZ = lerp(sZ, overcastZ, overcastW * 0.88f);
    float3 dayH = lerp(sH, overcastH, overcastW * 0.88f);
    dayH = lerp(dayH, snowH, weather.z * 0.45f);

    outSkyZ = lerp(dayZ, nightZ, nightW);
    outSkyH = lerp(dayH, nightH, nightW);
    const float ambGain = lerp(0.18f, 0.52f, 1.0f - nightW) * lerp(1.0f, 0.82f, overcastW);
    outAmbUp = lerp(outSkyZ, outSkyH, 0.50f) * ambGain;
    outAmbDown = lerp(outSkyH * float3(0.30f, 0.28f, 0.25f), outSkyH * float3(0.45f, 0.48f, 0.52f), weather.z) * ambGain;
}

#ifndef ATLAS_SLOT
#define ATLAS_SLOT t5
#endif
Texture2D<float> Atlas : register(ATLAS_SLOT);

// Analytic Perez/Preetham sky model ported from superframe/native/src/renderer/sky.cpp.
float perezEval(float A, float B, float C, float D, float E, float cosTheta, float gamma) {
    const float ct = max(cosTheta, 0.02f), cg = cos(gamma);
    return (1.0f + A * exp(B / ct)) * (1.0f + C * exp(D * gamma) + E * cg * cg);
}

float3 preethamSky(float3 dir, float3 sun, float turbidity) {
    const float3 d = normalize(dir);
    const float3 s = normalize(sun);
    const float T = clamp(turbidity, 2.0f, 8.0f);
    const float cosT = max(d.z, 0.02f);
    const float ts = acos(clamp(s.z, 0.02f, 1.0f));
    const float g = acos(clamp(dot(d, s), -1.0f, 1.0f));
    const float rY = perezEval(0.1787f * T - 1.4630f, -0.3554f * T + 0.4275f, -0.0227f * T + 5.3251f, 0.1206f * T - 2.5771f, -0.0670f * T + 0.3703f, cosT, g)
                   / max(perezEval(0.1787f * T - 1.4630f, -0.3554f * T + 0.4275f, -0.0227f * T + 5.3251f, 0.1206f * T - 2.5771f, -0.0670f * T + 0.3703f, 1.0f, ts), 1.0e-3f);
    const float rx = perezEval(-0.0193f * T - 0.2592f, -0.0665f * T + 0.0008f, -0.0004f * T + 0.2125f, -0.0641f * T - 0.8989f, -0.0033f * T + 0.0452f, cosT, g)
                   / max(perezEval(-0.0193f * T - 0.2592f, -0.0665f * T + 0.0008f, -0.0004f * T + 0.2125f, -0.0641f * T - 0.8989f, -0.0033f * T + 0.0452f, 1.0f, ts), 1.0e-3f);
    const float ry = perezEval(-0.0167f * T - 0.2608f, -0.0950f * T + 0.0092f, -0.0079f * T + 0.2102f, -0.0441f * T - 1.6537f, -0.0109f * T + 0.0529f, cosT, g)
                   / max(perezEval(-0.0167f * T - 0.2608f, -0.0950f * T + 0.0092f, -0.0079f * T + 0.2102f, -0.0441f * T - 1.6537f, -0.0109f * T + 0.0529f, 1.0f, ts), 1.0e-3f);
    const float t2 = T * T, ts2 = ts * ts, ts3 = ts2 * ts;
    const float xz = (0.00166f * ts3 - 0.00375f * ts2 + 0.00209f * ts) * t2
                   + (-0.02903f * ts3 + 0.06377f * ts2 - 0.03202f * ts + 0.00394f) * T
                   + (0.11693f * ts3 - 0.21196f * ts2 + 0.06052f * ts + 0.25886f);
    const float yz = (0.00275f * ts3 - 0.00610f * ts2 + 0.00317f * ts) * t2
                   + (-0.04214f * ts3 + 0.08970f * ts2 - 0.04153f * ts + 0.00516f) * T
                   + (0.15346f * ts3 - 0.26756f * ts2 + 0.06670f * ts + 0.26688f);
    const float x = clamp(xz * rx, 0.15f, 0.65f), y = clamp(yz * ry, 0.15f, 0.65f);
    const float Y = clamp(rY * 0.48f, 0.02f, 2.5f);
    const float X = (x / y) * Y, Z = ((1.0f - x - y) / y) * Y;
    const float3 rgb = max(float3(3.2406f * X - 1.5372f * Y - 0.4986f * Z,
                                  -0.9689f * X + 1.8758f * Y + 0.0415f * Z,
                                   0.0557f * X - 0.2040f * Y + 1.0570f * Z), 0.0f.xxx);
    // Keep sky chroma physically restrained (avoids neon-blue / purple cloud tint at low turbidity).
    const float lum = dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
    return lerp(lum.xxx, rgb, 0.52f);
}

// Dynamic physically based environment cubemap (shared by Lighting, RtShadow, and RtWater).
// Combines Preetham/Perez sky physics with live ambient SH, solar disk, and ground bounce.
float3 evalDynamicEnvCube(float3 rd, float roughness) {
    const float3 d = normalize(rd);
    const float3 ambDir = max(float3(dot(ambient[0].xyz, d) + ambient[0].w,
                                     dot(ambient[1].xyz, d) + ambient[1].w,
                                     dot(ambient[2].xyz, d) + ambient[2].w), 0.02f.xxx);
    const bool ext = sunColour.w > 0.5f;
    if (!ext) {
        return ambDir * lerp(0.55f, 1.15f, saturate(d.z * 0.5f + 0.5f));
    }
    const float dayW = saturate(sunDir.z * 3.5f);
    const float turb = lerp(2.6f, 5.2f, saturate(clouds.x));
    const float3 physSky = preethamSky(d.z >= 0.0f ? d : float3(d.xy, 0.02f), sunDir.xyz, turb);
    const float3 skyCol = lerp(ambDir * 1.1f, physSky * max(dot(ambDir, float3(0.3f, 0.5f, 0.2f)) * 1.8f, 0.25f), dayW * 0.65f);
    const float3 groundCol = ambDir * float3(0.42f, 0.40f, 0.36f);
    float3 env = d.z >= 0.0f ? skyCol : lerp(skyCol * 0.55f, groundCol, saturate(-d.z * 2.5f));
    const float cosS = saturate(dot(d, sunDir.xyz));
    const float expS = lerp(180.0f, 8.0f, saturate(roughness));
    const float sunLobe = pow(cosS, expS) * lerp(2.2f, 0.18f, saturate(roughness));
    const float cloudAtten = 1.0f - saturate(clouds.x * 0.75f);
    env += sunColour.rgb * sunLobe * cloudAtten * saturate(sunDir.z * 5.0f + 0.25f) * step(0.0f, d.z);
    return min(env, 4.0f.xxx);
}

float cascadeSample(float3 p, uint k, bool filtered) {
    const float4 q = float4(p, 1.0f);
    const float3 sc = float3(dot(cascade[3 * k], q), dot(cascade[3 * k + 1], q), dot(cascade[3 * k + 2], q));
    if (any(sc.xy <= 0.0f) || any(sc.xy >= 1.0f)) { return 1.0f; }
    const float side = cascadeInfo.w;
    const float z = sc.z - (k == 0u ? cascadeInfo.x : cascadeInfo.y);
    const float2 tF = sc.xy * side;
    const int2 t = int2(tF);
    if (!filtered) { return z <= Atlas.Load(int3(t.x + int(k * side), t.y, 0)) ? 1.0f : 0.0f; }
    // 2027 16-Tap IGN-Rotated Vogel-Disk PCSS Cascade Shadows (blocker-distance variable penumbra, replaces 3x3 box PCF):
    const float ang0 = ign(tF) * 6.2831853f;
    float blockerSum = 0.0f;
    float blockerCnt = 0.0f;
    [unroll] for (int bi = 0; bi < 6; ++bi) {
        const float rB = sqrt((float(bi) + 0.5f) / 6.0f) * 3.5f;
        const float aB = float(bi) * 2.39996323f + ang0;
        const int2 cB = clamp(t + int2(round(float2(cos(aB), sin(aB)) * rB)), int2(0, 0), int2(side - 1.0f, side - 1.0f));
        const float dB = Atlas.Load(int3(cB.x + int(k * side), cB.y, 0));
        if (dB < z) { blockerSum += (z - dB); blockerCnt += 1.0f; }
    }
    if (blockerCnt < 0.5f) { return 1.0f; }
    const float avgDelta = blockerSum / blockerCnt;
    const float penRad = clamp(1.15f + avgDelta * 180.0f, 1.15f, 4.5f);
    float lit = 0.0f;
    [unroll] for (int si = 0; si < 16; ++si) {
        const float rS = sqrt((float(si) + 0.5f) / 16.0f) * penRad;
        const float aS = float(si) * 2.39996323f + ang0;
        const int2 cS = clamp(t + int2(round(float2(cos(aS), sin(aS)) * rS)), int2(0, 0), int2(side - 1.0f, side - 1.0f));
        lit += z <= Atlas.Load(int3(cS.x + int(k * side), cS.y, 0)) ? 1.0f : 0.0f;
    }
    return lit * (1.0f / 16.0f);
}

// Sun visibility from the game's cascades with smooth cascade-transition blending (eliminates hard split seams).
float cascadeShadow(float3 p, float d, bool filtered) {
    if ((flags.x & FLAG_SHADOWS) == 0u || sunColour.w < 0.5f) { return 1.0f; }
    if (flags.z <= 1u) { return cascadeSample(p, 0u, filtered); }
    const float split = cascadeInfo.z;
    const float band = split * 0.90f;
    if (d <= band) { return cascadeSample(p, 0u, filtered); }
    if (d >= split) { return cascadeSample(p, 1u, filtered); }
    const float w = smoothstep(band, split, d);
    return lerp(cascadeSample(p, 0u, filtered), cascadeSample(p, 1u, filtered), w);
}
