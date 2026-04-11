# Shader Integration Guide

How to use Playground data in ENB shaders.

## Quick Start

### 1. Include the Constant Buffer Header

```hlsl
#include "Helper/SkyrimBridge_CB.fxh"
```

This declares all ~150 `extern float4` parameters with proper `UIName` and `UIHidden` annotations.

### 2. Use Parameters

```hlsl
float4 PS_Main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    float3 color = TextureColor.Sample(Sampler0, uv).rgb;

    // Weather-aware bloom threshold
    float threshold = 0.8 - SB_Weather_Precip.y * 0.3;  // lower in rain

    // Time-of-day tinting
    float3 sunTint = SB_Atmos_Sunlight.rgb;

    // Crosshair-driven autofocus
    float focusDist = SB_XHair_Info.y;  // distance to crosshair target

    return float4(color, 1);
}
```

### 3. Guard Against Missing Data

Playground may not be installed. All extern float4s default to zero, so guard features:

```hlsl
// Check if feedback data is available
bool SB_HasFeedback() { return SB_Computed_Scene.w > 0.5; }

// Check if Playground is active (any non-zero data)
bool SB_IsActive() { return SB_Render_Frame.x > 0; }
```

---

## Parameter Access Patterns

### Celestial

```hlsl
// Sun screen position for lens flare placement
float2 sunUV = SB_Sun_NDC.xy * 0.5 + 0.5;
bool sunOnScreen = SB_Sun_NDC.z > 0.5;
float sunElevation = SB_Sun_NDC.w;  // radians, 0 at horizon

// Sun direction for volumetric scattering
float3 sunDir = SB_Sun_Direction.xyz;

// Moon phases for night sky effects
float masserPhase = SB_Masser_NDC.w;  // 0 = new, 1 = full
```

### Time of Day

```hlsl
float hour = SB_Time_Data.x;        // [0, 24)
float dayPct = SB_Time_Data.w;      // 0 at sunrise, 1 at sunset

// Smooth time-of-day blending
float dawn    = SB_Time_Segments1.x; // [0,1] weight
float day     = SB_Time_Segments1.z;
float sunset  = SB_Time_Segments1.w;
float night   = SB_Time_Segments2.y;
float golden  = SB_Time_Segments2.z; // golden hour weight
```

### Weather

```hlsl
// Weather state helpers
bool SB_IsRaining()  { return SB_Weather_Flags.z > 0.5; }
bool SB_IsSnowing()  { return SB_Weather_Flags.w > 0.5; }
float SB_RainIntensity() { return SB_Weather_Precip.y; }

// Surface conditions
float wetness = SB_Weather_PrecipSurface.x;
float snowDepth = SB_Weather_PrecipSurface.z;

// Wind for particle effects
float windSpeed = SB_Weather_WindLive.x;
float2 windDir = float2(SB_Weather_WindLive.z, SB_Weather_WindLive.w);
```

### Camera Matrices

```hlsl
// Reconstruct world position from depth
float4x4 invVP = float4x4(
    SB_Camera_InvVP0, SB_Camera_InvVP1,
    SB_Camera_InvVP2, SB_Camera_InvVP3
);

float3 WorldFromDepth(float2 uv, float depth)
{
    float4 clip = float4(uv * 2 - 1, depth, 1);
    clip.y = -clip.y;
    float4 world = mul(clip, invVP);
    return world.xyz / world.w;
}

// Motion vectors (current vs previous frame)
float4x4 curVP = float4x4(SB_Camera_VP0, SB_Camera_VP1, SB_Camera_VP2, SB_Camera_VP3);
float4x4 prevVP = float4x4(SB_Camera_PrevVP0, SB_Camera_PrevVP1, SB_Camera_PrevVP2, SB_Camera_PrevVP3);

float2 MotionVector(float3 worldPos)
{
    float4 curClip = mul(float4(worldPos, 1), curVP);
    float4 prevClip = mul(float4(worldPos, 1), prevVP);
    return (curClip.xy / curClip.w) - (prevClip.xy / prevClip.w);
}
```

### Player State

```hlsl
float3 playerPos = SB_Player_Position.xyz;
float healthPct = SB_Player_Vitals.x;

// Combat state (packed bitfield)
bool inCombat = frac(SB_Player_Combat.x / 2.0) >= 0.5;
bool weaponDrawn = frac(SB_Player_Combat.x / 16.0) >= 0.5;

// Underwater effects
float submersion = SB_Player_Water.z;  // depth below surface
bool underwater = SB_Player_Water.x > 0.5;
```

### Interior Lighting

```hlsl
bool isInterior = SB_Interior_IsInterior.x > 0.5;
float3 ambientColor = SB_Interior_Ambient.rgb;
float ambientIntensity = SB_Interior_Ambient.a;
float3 interiorFog = SB_Interior_FogColor.rgb;
```

### GPU Feedback

```hlsl
if (SB_HasFeedback())
{
    float avgLum = SB_Computed_Scene.y;
    float keyValue = SB_Computed_SceneStats.x;      // log-average
    float contrast = SB_Computed_SceneStats.y;       // contrast ratio
    float colorTemp = SB_Computed_SceneColor.w;      // Kelvin

    // 4-bin histogram
    float shadows = SB_Computed_Histogram.x;  // <0.05
    float darks   = SB_Computed_Histogram.y;  // <0.18
    float mids    = SB_Computed_Histogram.z;  // <0.50
    float brights = SB_Computed_Histogram.w;  // >=0.50

    // Temporal
    bool sceneCut = SB_Computed_Temporal.x > 0.5;
    float lumVelocity = SB_Computed_Temporal.y;
    float stability = SB_Computed_Temporal.w;
}
```

### Scene Composition

```hlsl
// Material-aware effects
float skinFraction = SB_Scene_MatCount1.y;
float vegetationFraction = SB_Scene_MatCount1.w;
float avgRoughness = SB_Scene_MatProps1.z;

// Directional ambient (sky irradiance)
float3 skyUp = SB_Scene_DirAmbient3.rgb;
float3 skyRight = SB_Scene_DirAmbient1.rgb;

// Water detection
float3 waterNormal = SB_Scene_WaterPlane.xyz;
float waterLevel = SB_Scene_WaterPlane.w;
float3 waterColor = SB_Scene_WaterColor.rgb;
```

### NPC / Threat

```hlsl
float threat = SB_NPC_Threat.x;       // [0,1] composite threat rating
float stealth = SB_NPC_Threat.y;      // [0,100] stealth meter
int hostiles = (int)SB_NPC_Summary.x; // hostile count within 30m
float nearestDist = SB_NPC_Nearest.x;
```

### Equipment

```hlsl
bool hasTorch = SB_Equip_Flags.z > 0.5;
bool weaponDrawn = SB_Equip_Flags.x > 0.5;
float armorRating = SB_Equip_Armor.x;
```

---

## ENB Pipeline Notes

### Execution Order

```
1. enbeffectprepass   (R16G16B16A16F — HDR)
2. enbdepthoffield    (R16G16B16A16F — HDR)
3. enbbloom           (R16G16B16A16F — HDR)
4. enbadaptation      (R16G16B16A16F — HDR)
5. enblens            (R16G16B16A16F — HDR)
6. enbeffect          (R16G16B16A16F — HDR)
7. enbeffectpostpass  (R10G10B10A2_UNORM — LDR!)
8. enbsunsprite       (R10G10B10A2_UNORM — LDR)
9. enbunderwater      (R10G10B10A2_UNORM — LDR)
```

Stage 7+ is LDR (R10G10B10A2_UNORM) — values clamped to [0,1], only 2-bit alpha.

### Technique Naming

- Sequential: `BaseName`, `BaseName1`, `BaseName2`...
- Only the base technique gets `UIName` annotation
- Sub-techniques run automatically as part of the chain
- **RenderTarget annotations only work on base techniques**

### Required Names

- `enbdepthoffield.fx`: `ReadFocus`, `Focus` (hardcoded), then custom names
- `enbadaptation.fx`: `Downsample`, `Draw` (required)
- Other shaders: custom technique names work

### ENB ScreenSize

```hlsl
// ScreenSize.x = width, .y = 1/width, .z = aspect (w/h), .w = 1/aspect (h/w)
float2 PixelSize = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z); // (1/w, 1/h)
float2 ScreenRes = float2(ScreenSize.x, ScreenSize.x * ScreenSize.w); // (w, h)
// FieldOfView is in DEGREES
```
