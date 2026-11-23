// WorldFieldSim.hlsl: Persistent World-Anchored 512x512 RGBA16F GPU PDE Fluid & Snowpack Simulation (SM 6.9).
// Covers a 2,048 x 2,048 world-unit window centered on the camera (4.0u/texel) with exact toroidal world-locking:
//   R : 2D Shallow-Water Wave-Equation PDE height h(x,y,t)  (9-point isotropic Laplacian, symplectic Verlet)
//   G : 2D Shallow-Water vertical wave velocity v(x,y,t)
//   B : Persistent wake / crest whitewater foam f(x,y,t)
//   A : Persistent world-anchored rising/deepening storm snowpack depth & carved foot-trench / hearth-melt field s(x,y,t)
// Original work, MIT.
#include "LightingCommon.hlsli"
#include "CloudCommon.hlsli"

Texture2D<float4> PrevField : register(t0);
SamplerState      Lin       : register(s0);

static const float kGridRes   = 512.0f;
static const float kCellUnits = 4.0f;
static const float kGridSpan  = kGridRes * kCellUnits;   // 2,048 world units

float4 fs(float4 at : SV_Position) : SV_Target0 {
    const int2 ip = int2(at.xy);
    const float2 uv = (float2(ip) + 0.5f) / kGridRes;
    const float2 invRes = 1.0f / kGridRes;

    // World XY coordinate of this simulation cell (centered on camera):
    const float2 worldXY = camPosW.xy + (uv - 0.5f.xx) * kGridSpan;

    // Sample previous frame's PDE state using 9-point isotropic Laplacian stencil (zero axis bias):
    const float4 c0 = PrevField.SampleLevel(Lin, uv, 0.0f);
    const float hL  = PrevField.SampleLevel(Lin, uv + float2(-invRes.x, 0.0f), 0.0f).r;
    const float hR  = PrevField.SampleLevel(Lin, uv + float2( invRes.x, 0.0f), 0.0f).r;
    const float hD  = PrevField.SampleLevel(Lin, uv + float2(0.0f, -invRes.y), 0.0f).r;
    const float hU  = PrevField.SampleLevel(Lin, uv + float2(0.0f,  invRes.y), 0.0f).r;
    const float hDL = PrevField.SampleLevel(Lin, uv + float2(-invRes.x, -invRes.y), 0.0f).r;
    const float hDR = PrevField.SampleLevel(Lin, uv + float2( invRes.x, -invRes.y), 0.0f).r;
    const float hUL = PrevField.SampleLevel(Lin, uv + float2(-invRes.x,  invRes.y), 0.0f).r;
    const float hUR = PrevField.SampleLevel(Lin, uv + float2( invRes.x,  invRes.y), 0.0f).r;

    const float lapH = (hL + hR + hD + hU) * 0.20f + (hDL + hDR + hUL + hUR) * 0.05f - c0.r;

    // Symplectic Verlet 2D Wave-Equation step (stable CFL c^2 <= 0.35, gentle viscous damping):
    float h = c0.r;
    float v = c0.g;
    float foam = c0.b * 0.965f;
    float snow = c0.a;

    // Initialize snowpack smoothly on first frame from live weather snow state:
    if (proj.z < 1.5f) {
        snow = saturate(weather.z * 0.65f);
        h = 0.0f; v = 0.0f; foam = 0.0f;
    }

    // 1) Storm snowpack accumulation (rises in blizzards) & rain/sun melt:
    if (weather.z > 0.04f) {
        const float driftNoise = 0.72f + 0.56f * cNoise(float3(worldXY * 0.008f, camPosW.w * 0.02f));
        snow = saturate(snow + weather.z * 0.00085f * driftNoise);
    } else if (weather.w > 0.05f) {
        snow = saturate(snow - weather.w * 0.0014f);
    }

    // 2) Raindrop impulse forcing into 2D wave heightfield:
    float force = 0.0f;
    if (weather.w > 0.08f) {
        const float rH = cHash(float3(floor(worldXY * 0.25f), floor(camPosW.w * 18.0f)));
        if (rH > 0.996f) { force -= weather.w * 0.22f; }
    }

    // 3) Player & actor footstep wakes (water PDE) + persistent deep snow-trench excavation (snow PDE):
    [loop] for (uint fi = 0u; fi < 8u; ++fi) {
        const float4 ft = footTrail[fi];
        if (ft.w <= 0.05f) { continue; }
        const float2 fd = worldXY - ft.xy;
        const float d2 = dot(fd, fd);
        if (d2 < 324.0f) {
            const float g = exp(-d2 / 96.0f) * ft.w;
            force -= g * 0.16f;
            foam = saturate(foam + g * 0.28f);
            snow = saturate(snow - g * 0.22f);
        }
    }

    // 4) Thermal campfire / hearth / torch snowpack melt:
    const uint maxL = min(flags.y, 12u);
    [loop] for (uint li = 0u; li < maxL; ++li) {
        const float3 lc = lightColS[li].rgb;
        if (lc.r > lc.b * 1.25f && lc.r > 0.16f) {
            const float2 lWorld = lightPosR[li].xy + camPosW.xy;
            const float rMelt = min(lightPosR[li].w * 0.38f, 220.0f);
            const float d2 = dot(worldXY - lWorld, worldXY - lWorld);
            if (d2 < rMelt * rMelt) {
                const float att = 1.0f - sqrt(d2) / max(rMelt, 1.0f);
                snow = saturate(snow - att * att * 0.025f);
            }
        }
    }

    v = (v + lapH * 0.32f + force) * 0.984f;
    h = clamp(h + v, -1.5f, 1.5f);
    // Soft damping sponge at simulation boundary so waves never reflect off the 2,048u window edge:
    const float edgeDist = min(min(uv.x, 1.0f - uv.x), min(uv.y, 1.0f - uv.y));
    const float sponge = smoothstep(0.0f, 0.06f, edgeDist);
    h *= sponge;
    v *= sponge;
    foam = saturate(foam + saturate(-lapH * 2.2f) * 0.18f) * sponge;

    return float4(h, v, foam, snow);
}
