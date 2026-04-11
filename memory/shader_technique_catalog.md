# Advanced Shader Technique Catalog for ENB Addons
## DX11 SM5.0 | Pixel Shader Only | Single-Pass Per Technique
### Compiled 2026-03-06 by Claude for SkyrimBridge v3

Target: ENB addon .fxh files included into host .fx shaders (enbeffectpostpass.fx, enbeffectprepass.fx, enblens.fx, enbeffect.fx). Each technique is a single pixel shader pass consuming TextureColor, TextureDepth, TextureOriginal, and optionally RenderTargets or custom textures.

Techniques are grouped by category and ranked within each category by a composite score of **visual impact**, **uniqueness**, and **feasibility** (single-pass, no compute).

---

## TIER S — Top Priority ("Happy Accidents")

---

### S1. Anisotropic Kuwahara Filter (Painterly)

**Visual:** Transforms the scene into an oil painting with visible brushstrokes that follow image features. Edges remain sharp while flat regions become creamy, abstract pools of color. Dramatically different from any existing ENB effect.

**Algorithm:**
```hlsl
// Per-pixel: compute structure tensor from luminance gradients
// Eigen decomposition gives local orientation + anisotropy
// Sample N sectors of an elliptical kernel aligned to local orientation
// For each sector: compute mean + variance
// Output = mean of sector with lowest variance (flattest region wins)

float2 ComputeStructureTensor(float2 uv) {
    // Sobel 3x3 on luminance → dLdx, dLdy
    float gx = SobelX(uv); float gy = SobelY(uv);
    // Structure tensor: [gx*gx, gx*gy; gx*gy, gy*gy]
    // Gaussian-smooth the tensor components (5x5)
    // Eigenvalues → anisotropy; eigenvector → orientation angle
    return float2(angle, anisotropy);
}

float4 PS_Kuwahara(float2 uv) {
    float2 st = ComputeStructureTensor(uv);
    float angle = st.x; float aniso = st.y;

    // 8 sectors, elliptical kernel (stretch along eigenvector)
    float4 bestColor; float bestVar = 1e6;
    [unroll] for (int s = 0; s < 8; s++) {
        float sectorAngle = angle + s * PI / 4.0;
        float3 mean = 0; float3 meanSq = 0; float count = 0;
        // Sample ~6-8 points per sector along ellipse
        [unroll] for (int t = 0; t < 8; t++) {
            float2 offset = EllipsePoint(sectorAngle, aniso, t, RADIUS);
            float3 c = TextureColor.Sample(smpLinear, uv + offset * PixelSize);
            mean += c; meanSq += c * c; count++;
        }
        mean /= count; meanSq /= count;
        float var = dot(meanSq - mean * mean, 1.0);
        if (var < bestVar) { bestVar = var; bestColor = float4(mean, 1); }
    }
    return bestColor;
}
```

**Instruction count:** ~400-600 (8 sectors x 8 taps = 64 texture reads + structure tensor). Heavy but single-pass viable.
**Performance:** Medium-High. At half-res would be ~1.5ms at 1080p.
**Textures needed:** TextureColor only. No custom textures.
**Pipeline stage:** enbeffectpostpass.fx (LDR is fine for artistic effect) or enbeffect.fx (HDR).
**SB integration:** Kernel radius driven by `SB_Wind.x` (windy = longer strokes), anisotropy boosted during rain (`SB_Precipitation.y`), effect faded in interiors via `SB_Interior_Flags`.

---

### S2. Ink Wash / Sumi-e (Painterly/NPR)

**Visual:** Transforms scene into monochrome Japanese ink painting with visible brush-edge darkening, paper grain absorption, and ink concentration gradients. Whites bleed into paper texture. Dramatic and immediately recognizable.

**Algorithm:**
```hlsl
// 1. Convert to luminance
// 2. Edge detection (Sobel on depth + luminance) → ink edge mask
// 3. Quantize luminance into 3-5 ink density levels with soft transitions
// 4. Apply paper texture absorption: darker ink pools in paper grain valleys
// 5. Edge darkening: multiply ink density near edges (capillary action)
// 6. Bleed: slight directional blur weighted by ink density (gravity pull)

float4 PS_SumiE(float2 uv) {
    float lum = Luminance(TextureColor.Sample(smpLinear, uv).rgb);
    float depth = GetLinearDepth(uv);

    // Edge detection on depth + luminance
    float edgeD = SobelMagnitude_Depth(uv);
    float edgeL = SobelMagnitude_Lum(uv);
    float edge = saturate(max(edgeD * 3.0, edgeL * 2.0));

    // Ink density: invert luminance, quantize to bands
    float ink = 1.0 - lum;
    ink = floor(ink * ui_InkLevels + 0.5) / ui_InkLevels;

    // Paper texture (procedural: use hash-based noise at screen coords)
    float paper = PaperGrainNoise(uv * ScreenRes * 0.5);
    ink = ink + paper * 0.08 * ink; // grain absorbs more where ink is dense

    // Edge darkening (capillary spread)
    ink = max(ink, edge * ui_EdgeInkDensity);

    // Bleed: 3-tap directional blur downward, weighted by ink
    float2 bleedDir = float2(0, 1) * PixelSize * ui_BleedAmount;
    float bleed = ink * 0.5 + TextureLum(uv + bleedDir) * 0.3
                + TextureLum(uv + bleedDir * 2) * 0.2;
    ink = lerp(ink, bleed, ink * 0.3);

    // Final: warm paper tone with ink overlay
    float3 paperColor = float3(0.95, 0.92, 0.85);
    float3 inkColor = float3(0.05, 0.05, 0.08);
    return float4(lerp(paperColor, inkColor, saturate(ink)), 1);
}
```

**Instruction count:** ~120-180. Edge detection is the bulk (18 taps for dual Sobel).
**Performance:** Low-Medium. ~0.5ms.
**Textures needed:** TextureColor, TextureDepth. Optional: paper texture (can be procedural).
**Pipeline stage:** enbeffectpostpass.fx (output is LDR artistic).
**SB integration:** Ink bleed direction follows `SB_Wind_Live.zw` (wind pushes ink). Rain (`SB_Precipitation.y`) increases bleed. Paper wetness from `SB_Precip_Surface.x`.

---

### S3. Risograph Print Simulation (Photographic/NPR)

**Visual:** Distinctive duotone or tritone printing with halftone dots, slight layer misregistration, grain texture, and ink density variation. The trendy indie-poster aesthetic never seen in game post-processing.

**Algorithm:**
```hlsl
// 1. Separate scene into 2-3 ink channels (user picks spot colors)
// 2. Per channel: convert to halftone dots at angled screens
// 3. Apply slight UV offset per channel (misregistration)
// 4. Overlay grain texture (Riso drum artifacts)
// 5. Multiply-blend channels onto paper base

float HalftoneScreen(float2 uv, float angle, float frequency, float value) {
    float s = sin(angle), c = cos(angle);
    float2 rotUV = float2(uv.x*c - uv.y*s, uv.x*s + uv.y*c);
    float2 gridUV = rotUV * frequency;
    float2 cell = frac(gridUV) - 0.5;
    float dotRadius = sqrt(1.0 - value) * 0.5; // darker = bigger dots
    return smoothstep(dotRadius + 0.02, dotRadius - 0.02, length(cell));
}

float4 PS_Risograph(float2 uv) {
    float3 scene = TextureColor.Sample(smpLinear, uv).rgb;
    float lum = Luminance(scene);

    // Misregistration per channel
    float2 uvR = uv + float2(0.001, 0.0005) * ui_Misregistration;
    float2 uvB = uv - float2(0.0008, 0.001) * ui_Misregistration;
    float lumR = Luminance(TextureColor.Sample(smpLinear, uvR).rgb);
    float lumB = Luminance(TextureColor.Sample(smpLinear, uvB).rgb);

    // Halftone screens at different angles (classic CMYK-style separation)
    float inkA = HalftoneScreen(uvR * ScreenRes, 0.262, ui_DotFreq, lumR); // 15 deg
    float inkB = HalftoneScreen(uvB * ScreenRes, 1.309, ui_DotFreq, lumB); // 75 deg

    // Spot colors (user-defined)
    float3 colorA = ui_SpotColorA; // e.g., Riso Fluorescent Pink
    float3 colorB = ui_SpotColorB; // e.g., Riso Teal
    float3 paper = float3(0.96, 0.94, 0.90);

    // Multiply blend (simulates ink-on-paper absorption)
    float3 result = paper;
    result *= lerp(1.0, colorA, (1.0 - inkA) * ui_InkDensityA);
    result *= lerp(1.0, colorB, (1.0 - inkB) * ui_InkDensityB);

    // Grain (drum texture artifacts)
    float grain = Hash12(uv * ScreenRes + SB_Render_Frame.x * 0.1) * 0.06;
    result += grain;

    return float4(result, 1);
}
```

**Instruction count:** ~100-150. Halftone is cheap (trig + frac + length).
**Performance:** Low. ~0.3ms.
**Textures needed:** TextureColor only. No custom textures needed.
**Pipeline stage:** enbeffectpostpass.fx.
**SB integration:** Misregistration jitter from `SB_Render_Jitter.xy` (TAA offset). Spot colors could shift with time of day via `SB_Time_Segments1`.

---

### S4. Depth-Reconstructed Motion Blur (Temporal)

**Visual:** Per-pixel motion blur derived from camera movement, using current and previous ViewProjection matrices. Objects streak along their apparent motion path. No velocity buffer needed — reconstructed from depth.

**Algorithm:**
```hlsl
// 1. Reconstruct world position from depth + inverse VP
// 2. Project into previous frame's clip space using PrevVP
// 3. Velocity = current NDC - previous NDC
// 4. Sample along velocity vector, depth-weighted to avoid background bleeding

float4 PS_MotionBlur(float2 uv) {
    float depth = TextureDepth.Sample(smpPoint, uv).x;

    // Reconstruct clip-space position
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y; // ENB convention

    // World position via inverse VP
    float4x4 invVP = float4x4(SB_InvVP_Row0, SB_InvVP_Row1,
                                SB_InvVP_Row2, SB_InvVP_Row3);
    float4 worldPos = mul(clipPos, invVP);
    worldPos /= worldPos.w;

    // Reproject to previous frame
    float4x4 prevVP = float4x4(SB_PrevVP_Row0, SB_PrevVP_Row1,
                                 SB_PrevVP_Row2, SB_PrevVP_Row3);
    float4 prevClip = mul(worldPos, prevVP);
    float2 prevUV = prevClip.xy / prevClip.w * float2(0.5, -0.5) + 0.5;

    // Velocity in UV space
    float2 velocity = (uv - prevUV) * ui_Intensity;
    float speed = length(velocity * ScreenRes);

    // Clamp max blur length
    velocity = velocity * min(1.0, ui_MaxPixels / max(speed, 1.0));

    // Sample along velocity (8 taps, depth-aware rejection)
    float3 result = TextureColor.Sample(smpLinear, uv).rgb;
    float totalWeight = 1.0;
    [unroll] for (int i = 1; i <= 8; i++) {
        float t = float(i) / 8.0;
        float2 sampleUV = uv + velocity * t;
        float sampleDepth = TextureDepth.Sample(smpPoint, sampleUV).x;
        float depthDiff = abs(depth - sampleDepth);
        float weight = depthDiff < 0.01 ? 1.0 : 0.1; // reject bg bleeds
        result += TextureColor.Sample(smpLinear, sampleUV).rgb * weight;
        totalWeight += weight;
    }
    return float4(result / totalWeight, 1);
}
```

**Instruction count:** ~200-250. Matrix ops + 8 texture pairs (color+depth).
**Performance:** Medium. ~0.8ms. Depth rejection adds cost but prevents artifacts.
**Textures needed:** TextureColor, TextureDepth. **Requires SkyrimBridge VP matrices.**
**Pipeline stage:** enbeffect.fx (needs HDR for correct blending).
**SB integration:** Core dependency on `SB_InvVP_*`, `SB_PrevVP_*`. Suppressed when `SB_Render_StencilInfo.w` (gamePaused). Intensity scaled by `SB_Render_Jitter.w` (timeDilation — slow-mo gets less blur). Disabled during menu via `SB_UI_State`.

---

### S5. Cross-Hatching / Engraving (NPR)

**Visual:** Scene rendered as if drawn with pen strokes — dark areas get dense cross-hatching, mid-tones get single hatching, highlights are bare paper. Like a banknote engraving or Gustave Dore illustration.

**Algorithm:**
```hlsl
// 6 hatching tones (TAM — Tonal Art Map), generated procedurally:
// Tone 0: blank paper (lightest)
// Tone 1: sparse horizontal lines
// Tone 2: denser horizontal lines
// Tone 3: horizontal + 45-degree cross
// Tone 4: dense cross + 135-degree
// Tone 5: near-solid ink (darkest)

float HatchLine(float2 uv, float angle, float freq, float width) {
    float s = sin(angle), c = cos(angle);
    float coord = uv.x * c + uv.y * s;
    return smoothstep(width, width - 0.1, abs(frac(coord * freq) - 0.5) * 2.0);
}

float4 PS_CrossHatch(float2 uv) {
    float3 scene = TextureColor.Sample(smpLinear, uv).rgb;
    float lum = Luminance(scene);
    float depth = GetLinearDepth(uv);

    // Scale frequency by depth (distant objects get finer hatching)
    float freq = ui_BaseFreq * lerp(1.0, 3.0, saturate(depth / ui_DepthScale));
    float2 screenUV = uv * ScreenRes;

    // Build hatching layers
    float h1 = HatchLine(screenUV, 0.0, freq, 0.35);           // horizontal
    float h2 = HatchLine(screenUV, PI*0.25, freq, 0.35);       // 45 deg
    float h3 = HatchLine(screenUV, PI*0.5, freq * 0.8, 0.30);  // vertical
    float h4 = HatchLine(screenUV, PI*0.75, freq * 0.8, 0.30); // 135 deg

    // Map luminance to hatching density
    float ink = 0;
    ink += h1 * smoothstep(0.8, 0.6, lum); // appears in shadows first
    ink += h2 * smoothstep(0.6, 0.4, lum);
    ink += h3 * smoothstep(0.4, 0.2, lum);
    ink += h4 * smoothstep(0.2, 0.0, lum);
    ink = saturate(ink);

    // Edge reinforcement (Sobel on depth)
    float edge = SobelMagnitude_Depth(uv);
    ink = max(ink, edge * ui_EdgeStrength);

    float3 paper = ui_PaperColor;
    float3 inkCol = ui_InkColor;
    return float4(lerp(paper, inkCol, ink), 1);
}
```

**Instruction count:** ~100-140. Four trig+frac line evaluations + edge detection.
**Performance:** Low. ~0.3ms.
**Textures needed:** TextureColor, TextureDepth. No custom textures.
**Pipeline stage:** enbeffectpostpass.fx.
**SB integration:** Hatching direction rotated by `SB_Wind_Live.y` (wind angle). Paper color tinted by `SB_Atmos_Ambient.rgb` for ambient influence.

---

## TIER A — High Priority (Visually Striking + Feasible)

---

### A1. Voronoi Stained Glass (Geometric)

**Visual:** Scene fractured into colored glass panels with dark lead borders between them. Each panel shows the average color of its region. Light sources create bright panels. Cathedral window aesthetic.

**Algorithm:**
```hlsl
float4 PS_StainedGlass(float2 uv) {
    float2 screenUV = uv * ui_CellDensity;

    // Voronoi: find nearest cell center
    float2 cellID = floor(screenUV);
    float minDist = 1e6; float minDist2 = 1e6;
    float2 nearestCenter;

    [unroll] for (int y = -1; y <= 1; y++)
    [unroll] for (int x = -1; x <= 1; x++) {
        float2 neighbor = cellID + float2(x, y);
        // Deterministic random center within cell
        float2 center = neighbor + Hash22(neighbor) * 0.8 + 0.1;
        float d = length(screenUV - center);
        if (d < minDist) { minDist2 = minDist; minDist = d; nearestCenter = center; }
        else if (d < minDist2) { minDist2 = d; }
    }

    // Edge = thin border where F2-F1 is small
    float border = smoothstep(0.02, 0.05, minDist2 - minDist);

    // Sample color at cell center (map back to UV space)
    float2 centerUV = nearestCenter / ui_CellDensity;
    float3 cellColor = TextureColor.Sample(smpLinear, centerUV).rgb;

    // Boost saturation (stained glass is vivid)
    float lum = Luminance(cellColor);
    cellColor = lerp(lum, cellColor, ui_Saturation);

    // Lead = dark gray border
    float3 lead = float3(0.15, 0.15, 0.12);
    return float4(lerp(lead, cellColor, border), 1);
}
```

**Instruction count:** ~80-100. 9-cell Voronoi search + 1 extra texture read.
**Performance:** Low. ~0.2ms.
**Textures needed:** TextureColor only.
**Pipeline stage:** enbeffectpostpass.fx.
**SB integration:** Cell density driven by depth (`SB_Camera_Info.z` — far clip) for distance-adaptive detail. Interior flag could switch to more ornate pattern.

---

### A2. Thermal Imaging / Predator Vision (Photographic)

**Visual:** False-color thermal map where luminance maps to a heat gradient (black → purple → blue → cyan → green → yellow → red → white). Depth-aware: nearby objects are "warmer." Actor lights create heat signatures.

**Algorithm:**
```hlsl
// Thermal palette (8-stop gradient)
static const float3 kThermalPalette[8] = {
    float3(0.0, 0.0, 0.0),    // cold black
    float3(0.1, 0.0, 0.3),    // deep purple
    float3(0.0, 0.0, 0.8),    // blue
    float3(0.0, 0.6, 0.8),    // cyan
    float3(0.0, 0.8, 0.0),    // green
    float3(0.8, 0.8, 0.0),    // yellow
    float3(1.0, 0.2, 0.0),    // red
    float3(1.0, 1.0, 1.0)     // hot white
};

float4 PS_Thermal(float2 uv) {
    float3 scene = TextureColor.Sample(smpLinear, uv).rgb;
    float lum = Luminance(scene);
    float depth = GetLinearDepth(uv);

    // Heat = luminance + proximity boost + light contribution
    float heat = lum;
    heat += (1.0 - saturate(depth / ui_DepthRange)) * ui_ProximityBoost;

    // Nearby light contribution (SB actor lights → heat sources)
    float3 worldPos = ReconstructWorldPos(uv, depth);
    [unroll] for (int i = 0; i < 3; i++) {
        float4 lightPR = GetSBLight_PosRad(i);
        float dist = length(worldPos.xyz - lightPR.xyz);
        heat += saturate(1.0 - dist / lightPR.w) * ui_LightHeat;
    }

    heat = saturate(heat);

    // Palette lookup with smooth interpolation
    float idx = heat * 7.0;
    int lo = (int)idx; int hi = min(lo + 1, 7);
    float3 color = lerp(kThermalPalette[lo], kThermalPalette[hi], frac(idx));

    // Scanline overlay (thermal camera artifact)
    float scanline = 1.0 - 0.05 * step(0.5, frac(uv.y * ScreenRes.y * 0.5));
    color *= scanline;

    // Noise (sensor noise)
    color += (Hash12(uv * ScreenRes + SB_Render_Frame.x) - 0.5) * 0.03;

    return float4(color, 1);
}
```

**Instruction count:** ~100-130. Palette lerp + 3 light distance checks + noise.
**Performance:** Low. ~0.3ms.
**Textures needed:** TextureColor, TextureDepth. Uses SB light positions.
**Pipeline stage:** enbeffectpostpass.fx or enbeffect.fx.
**SB integration:** `SB_Light0/1/2_PosRad` for heat signatures. `SB_Player_Combat.x` (in combat) could activate it. `SB_FX_Vision.x` (nightEye) as trigger.

---

### A3. Infrared Photography Simulation (Photographic)

**Visual:** Simulates color infrared film (Kodak Aerochrome / Kodak Ektachrome IR). Vegetation appears vivid magenta-red, sky goes deep blue, skin tones shift green. The "Wood Effect" — surreal and instantly recognizable.

**Algorithm:**
```hlsl
// CIR film maps: NIR→Red channel, Red→Green channel, Green→Blue channel
// Since we can't measure real NIR, approximate: vegetation = high green, low blue
// Vegetation index: (G - B) / (G + B) — proxy for chlorophyll reflection

float4 PS_Infrared(float2 uv) {
    float3 scene = TextureColor.Sample(smpLinear, uv).rgb;
    float3 linear = SRGBToLinear(scene);

    // Vegetation proxy (NDVI-like from visible channels)
    float vegIndex = saturate((linear.g - linear.b) /
                              max(linear.g + linear.b, 0.001));

    // Simulated NIR channel: vegetation reflects strongly in NIR
    float nirProxy = linear.g * 0.6 + linear.r * 0.3 + vegIndex * 0.4;

    // CIR channel swap: NIR→R, R→G, G→B
    float3 cir;
    cir.r = lerp(linear.r, nirProxy, ui_IRStrength);
    cir.g = lerp(linear.g, linear.r, ui_IRStrength);
    cir.b = lerp(linear.b, linear.g * 0.7 + linear.b * 0.3, ui_IRStrength);

    // Sky darkening (sky = high blue, low green → appears dark in IR)
    float skyness = saturate((linear.b - linear.g) / max(linear.b + linear.g, 0.001));
    cir *= lerp(1.0, 0.3, skyness * ui_SkyDarken);

    // Film tone curve (Aerochrome has punchy contrast + magenta bias)
    cir = pow(cir, ui_Contrast);
    cir.r *= 1.1; // slight red/magenta push

    // Hotspot (Aerochrome center hotspot from IR filter leakage)
    float2 center = uv - 0.5;
    float vignette = 1.0 - dot(center, center) * ui_HotspotFalloff;
    cir *= vignette;

    return float4(LinearToSRGB(saturate(cir)), 1);
}
```

**Instruction count:** ~60-80. Channel math + one vignette. Very cheap.
**Performance:** Very Low. ~0.15ms.
**Textures needed:** TextureColor only.
**Pipeline stage:** enbeffect.fx (HDR for correct color math).
**SB integration:** Effect strength modulated by `SB_Time_Segments1.z` (day — IR film needs sunlight). Disabled at night. `SB_Weather_Flags` — only works in clear/pleasant weather (overcast washes it out).

---

### A4. VHS / Analog Video Degradation (Cinematic)

**Visual:** Complete analog video degradation: luma/chroma separation with chroma blur, horizontal tracking noise, head-switching distortion at bottom, scanlines, rainbow banding, tape dropout lines, timecode burn-in.

**Algorithm:**
```hlsl
float4 PS_VHS(float2 uv) {
    float time = SB_Render_Frame.x * SB_Render_Frame.y; // frame * dt

    // Tracking distortion: horizontal offset varies by scanline
    float scanY = uv.y * ScreenRes.y;
    float trackNoise = Hash11(floor(scanY * 0.25) + time * 3.7) * 2 - 1;
    float trackOffset = trackNoise * ui_TrackingNoise * 0.01;

    // Head-switching noise (bottom 5% of frame)
    float headSwitch = smoothstep(0.96, 0.98, uv.y);
    trackOffset += headSwitch * sin(scanY * 0.5 + time * 50) * 0.05;

    float2 distUV = float2(uv.x + trackOffset, uv.y);

    // Luma/chroma separation (YIQ color space)
    float3 scene = TextureColor.Sample(smpLinear, distUV).rgb;
    float Y = dot(scene, float3(0.299, 0.587, 0.114));
    float I = dot(scene, float3(0.596, -0.274, -0.322));
    float Q = dot(scene, float3(0.211, -0.523, 0.312));

    // Chroma blur (VHS has ~30 lines chroma resolution)
    float Ib = 0, Qb = 0;
    [unroll] for (int i = -3; i <= 3; i++) {
        float2 sUV = distUV + float2(float(i) * PixelSize.x * 4.0, 0);
        float3 s = TextureColor.Sample(smpLinear, sUV).rgb;
        Ib += dot(s, float3(0.596, -0.274, -0.322));
        Qb += dot(s, float3(0.211, -0.523, 0.312));
    }
    I = Ib / 7.0; Q = Qb / 7.0;

    // Reconstruct RGB from blurred chroma + sharp luma
    float3 result;
    result.r = Y + 0.956 * I + 0.621 * Q;
    result.g = Y - 0.272 * I - 0.647 * Q;
    result.b = Y - 1.106 * I + 1.703 * Q;

    // Scanlines
    float scanline = 1.0 - ui_ScanlineIntensity *
        step(0.5, frac(uv.y * ScreenRes.y * 0.5));
    result *= scanline;

    // Tape noise (horizontal dropout lines)
    float dropout = step(0.995, Hash11(floor(scanY) + time));
    result = lerp(result, float3(1,1,1) * Y, dropout * ui_DropoutIntensity);

    // Rainbow banding (NTSC color artifact)
    float rainbow = sin(uv.x * ScreenRes.x * 0.5 + time * 2.0) * 0.02;
    result.r += rainbow;
    result.b -= rainbow;

    // Noise
    result += (Hash12(uv * ScreenRes + time * 100) - 0.5) * ui_NoiseLevel;

    return float4(saturate(result), 1);
}
```

**Instruction count:** ~180-220. Chroma blur (7 taps) + YIQ conversion + scanlines.
**Performance:** Low-Medium. ~0.5ms.
**Textures needed:** TextureColor only.
**Pipeline stage:** enbeffectpostpass.fx (LDR is correct — VHS was never HDR).
**SB integration:** Tracking noise intensity from `SB_FX_Damage.x` (taking damage = tape jitter). Head-switching during `SB_Player_Combat`. Dropout frequency during `SB_Lightning.y` (flash = interference).

---

### A5. Kaleidoscope (Geometric)

**Visual:** Scene reflected through virtual mirrors arranged in a radial pattern, creating symmetrical mandala-like imagery from the game scene. Mesmerizing, psychedelic.

**Algorithm:**
```hlsl
float4 PS_Kaleidoscope(float2 uv) {
    float2 center = uv - 0.5;

    // Polar coordinates
    float r = length(center);
    float theta = atan2(center.y, center.x);

    // Number of mirror segments
    float segments = ui_Segments; // e.g. 6
    float segmentAngle = TWO_PI / segments;

    // Fold angle into first segment, then reflect
    theta = abs(fmod(theta, segmentAngle) - segmentAngle * 0.5);

    // Optional rotation animation
    theta += SB_Render_Frame.x * SB_Render_Frame.y * ui_RotationSpeed;

    // Back to cartesian
    float2 foldedUV = float2(cos(theta), sin(theta)) * r + 0.5;

    // Zoom control
    foldedUV = (foldedUV - 0.5) * ui_Zoom + 0.5;

    // Clamp/mirror at edges
    foldedUV = abs(foldedUV);
    foldedUV = min(foldedUV, 2.0 - foldedUV);

    return TextureColor.Sample(smpLinear, foldedUV);
}
```

**Instruction count:** ~30-40. Polar conversion + modulo + trig.
**Performance:** Very Low. ~0.1ms.
**Textures needed:** TextureColor only.
**Pipeline stage:** enbeffect.fx or enbeffectpostpass.fx.
**SB integration:** Segment count driven by `SB_FX_Misc` (drunk = more segments). Rotation speed from `SB_FX_Time.x` (skooma slow-time). Trigger via `SB_FX_Vision` effects.

---

### A6. Pointillism (NPR)

**Visual:** Scene decomposed into discrete colored dots of varying size — larger dots in shadows, smaller in highlights. Like a Seurat painting. Dots can be circular or diamond-shaped.

**Algorithm:**
```hlsl
float4 PS_Pointillism(float2 uv) {
    float2 screenUV = uv * ScreenRes;

    // Grid of dots
    float dotSpacing = ui_DotSpacing;
    float2 cellUV = screenUV / dotSpacing;
    float2 cellID = floor(cellUV);
    float2 cellFrac = frac(cellUV) - 0.5;

    // Jitter cell center slightly for natural feel
    float2 jitter = (Hash22(cellID) - 0.5) * 0.3;
    float2 dotCenter = cellFrac - jitter;
    float dist = length(dotCenter);

    // Sample color at cell center
    float2 centerUV = (cellID + 0.5 + jitter) * dotSpacing / ScreenRes;
    float3 color = TextureColor.Sample(smpLinear, centerUV).rgb;
    float lum = Luminance(color);

    // Dot size: darker = larger dots
    float dotSize = lerp(ui_MaxDotSize, ui_MinDotSize, lum) / dotSpacing;

    // Circular dot mask
    float dot = smoothstep(dotSize, dotSize - 0.05, dist);

    // Background (canvas)
    float3 canvas = ui_CanvasColor;

    // Boost saturation for painterly feel
    color = lerp(Luminance(color), color, ui_Saturation);

    return float4(lerp(canvas, color, dot), 1);
}
```

**Instruction count:** ~50-70. Grid math + hash + 1 texture read per dot.
**Performance:** Very Low. ~0.15ms.
**Textures needed:** TextureColor only.
**Pipeline stage:** enbeffectpostpass.fx.
**SB integration:** Dot spacing modulated by depth (TextureDepth) for distance-adaptive detail. Canvas color from `SB_Atmos_Ambient.rgb`.

---

### A7. OKLAB Perceptual Color Grading (Color Science)

**Visual:** Color adjustments in perceptually uniform OKLAB/OKLCH space — hue rotation feels natural, saturation changes don't shift perceived brightness, and color remapping avoids the ugly intermediate tones of RGB/HSL manipulation. Enables effects impossible in RGB: perceptual gamut mapping, uniform chroma boost, hue-selective operations.

**Algorithm:**
```hlsl
// OKLAB conversion (Bjorn Ottosson 2020)
float3 LinearRGBtoOKLAB(float3 c) {
    float l = 0.4122214708*c.r + 0.5363325363*c.g + 0.0514459929*c.b;
    float m = 0.2119034982*c.r + 0.6806995451*c.g + 0.1073969566*c.b;
    float s = 0.0883024619*c.r + 0.2220049874*c.g + 0.6396926158*c.b;
    l = pow(max(l,0), 1.0/3.0); m = pow(max(m,0), 1.0/3.0);
    s = pow(max(s,0), 1.0/3.0);
    return float3(
        0.2104542553*l + 0.7936177850*m - 0.0040720468*s,
        1.9779984951*l - 2.4285922050*m + 0.4505937099*s,
        0.0259040371*l + 0.7827717662*m - 0.8086757660*s);
}

float3 OKLABtoLinearRGB(float3 lab) {
    float l = lab.x + 0.3963377774*lab.y + 0.2158037573*lab.z;
    float m = lab.x - 0.1055613458*lab.y - 0.0638541728*lab.z;
    float s = lab.x - 0.0894841775*lab.y - 1.2914855480*lab.z;
    l = l*l*l; m = m*m*m; s = s*s*s;
    return float3(
        +4.0767416621*l - 3.3077115913*m + 0.2309699292*s,
        -1.2684380046*l + 2.6097574011*m - 0.3413193965*s,
        -0.0041960863*l - 0.7034186147*m + 1.7076147010*s);
}

float4 PS_OKLABGrade(float2 uv) {
    float3 scene = TextureColor.Sample(smpLinear, uv).rgb;
    float3 linear = SRGBToLinear(scene);
    float3 lab = LinearRGBtoOKLAB(linear);

    // OKLCH (cylindrical form)
    float C = length(lab.yz);
    float H = atan2(lab.z, lab.y);
    float L = lab.x;

    // Perceptual grading operations:
    L = pow(L, ui_LightnessGamma);                    // Lightness curve
    C = C * ui_ChromaScale;                             // Uniform saturation
    H = H + ui_HueRotation;                            // Hue shift

    // Hue-selective chroma boost (e.g., boost only warm tones)
    float hueWeight = exp(-pow((H - ui_TargetHue) / ui_HueWidth, 2.0));
    C *= 1.0 + hueWeight * ui_SelectiveChromaBoost;

    // Reconstruct
    lab = float3(L, C * cos(H), C * sin(H));
    float3 result = OKLABtoLinearRGB(lab);
    return float4(LinearToSRGB(saturate(result)), 1);
}
```

**Instruction count:** ~80-100. Matrix math + cbrt + atan2 + exp.
**Performance:** Low. ~0.2ms.
**Textures needed:** TextureColor only.
**Pipeline stage:** enbeffect.fx (needs linear HDR for correct conversion).
**SB integration:** Hue rotation driven by `SB_Time.w` (dayProgress) for shifting color palette through the day. Chroma suppressed during `SB_Weather_Flags` rain/snow. Lightness gamma from `SB_IS_Cinematic.z` (contrast).

---

### A8. Double Exposure (Photographic/Cinematic)

**Visual:** Blends the current scene with a frozen "memory" frame or a depth-silhouetted version. Creates ghostly overlapping imagery — foreground subject filled with distant landscape. Classic photographic technique for film titles and dream sequences.

**Algorithm:**
```hlsl
// Without persistent frame buffer, use creative alternatives:
// Option A: Depth-based separation — near objects silhouetted, filled with far scene
// Option B: Temporal — use TextureOriginal vs TextureColor (pre vs post ENB)
// Option C: Vertical flip blend (scene overlaid with inverted version)

float4 PS_DoubleExposure(float2 uv) {
    float3 sceneA = TextureColor.Sample(smpLinear, uv).rgb;
    float depth = GetLinearDepth(uv);

    // Layer B: shifted/transformed version of scene
    float2 uvB = uv;
    uvB.x = 1.0 - uvB.x;                    // horizontal flip
    uvB = (uvB - 0.5) * ui_ZoomB + 0.5;     // zoom offset
    uvB += ui_OffsetB;                        // pan offset
    float3 sceneB = TextureOriginal.Sample(smpLinear, uvB).rgb;

    // Silhouette mask from depth (near = opaque foreground)
    float mask = smoothstep(ui_DepthNear, ui_DepthFar, depth);

    // Blend mode: Screen (classic double exposure = additive on film)
    float3 blended = 1.0 - (1.0 - sceneA * mask) * (1.0 - sceneB * (1.0 - mask));

    // Desaturate slightly for dreamlike quality
    float lum = Luminance(blended);
    blended = lerp(blended, lum, ui_Desaturate);

    // Tone: warm shift for memory feel
    blended *= float3(1.05, 1.0, 0.92);

    return float4(blended, 1);
}
```

**Instruction count:** ~40-60. Two texture reads + depth + screen blend.
**Performance:** Very Low. ~0.15ms.
**Textures needed:** TextureColor, TextureOriginal, TextureDepth.
**Pipeline stage:** enbeffect.fx.
**SB integration:** Trigger during `SB_FX_Misc` (drunk, paralyzed). Offset driven by `SB_Camera_Angles.xy` for parallax shift. Depth thresholds from `SB_Camera_Info.yz` (near/far clip).

---

## TIER B — Strong Candidates (Niche but Distinctive)

---

### B1. Watercolor Wash (Painterly)

**Visual:** Scene appears painted in watercolor: soft color bleeds beyond edges, paper texture visible in light areas, pigment granulation in mid-tones, dark edge outlines where colors meet (capillary darkening).

**Algorithm:**
```hlsl
float4 PS_Watercolor(float2 uv) {
    // 1. Edge-aware blur: 9-tap Gaussian on color, edge-weighted
    float3 blurred = 0; float totalW = 0;
    float baseLum = Luminance(TextureColor.Sample(smpLinear, uv).rgb);
    [unroll] for (int y = -1; y <= 1; y++)
    [unroll] for (int x = -1; x <= 1; x++) {
        float2 off = float2(x, y) * PixelSize * ui_BlurRadius;
        float3 s = TextureColor.Sample(smpLinear, uv + off).rgb;
        float w = exp(-length(float2(x,y)) * 0.5);
        // Edge-awareness: reduce weight across edges
        float lumDiff = abs(Luminance(s) - baseLum);
        w *= exp(-lumDiff * ui_EdgePreserve);
        blurred += s * w;
        totalW += w;
    }
    blurred /= totalW;

    // 2. Pigment granulation (noise modulates saturation in mid-tones)
    float grain = Hash12(uv * ScreenRes * 2.0 + 0.1);
    float midMask = 1.0 - abs(Luminance(blurred) - 0.5) * 2.0; // peaks at 0.5
    float3 color = lerp(blurred, blurred * (0.85 + grain * 0.3), midMask * ui_Granulation);

    // 3. Edge darkening (Sobel on original)
    float edge = SobelMagnitude_Color(uv);
    color *= 1.0 - edge * ui_EdgeDarken;

    // 4. Paper texture bleed (lighten whites toward paper)
    float lum = Luminance(color);
    float paperBlend = smoothstep(0.6, 0.95, lum);
    float3 paper = float3(0.97, 0.95, 0.91);
    float paperGrain = Hash12(uv * ScreenRes * 3.0) * 0.03;
    color = lerp(color, paper + paperGrain, paperBlend * ui_PaperShow);

    return float4(color, 1);
}
```

**Instruction count:** ~120-160. 9-tap edge-aware blur + Sobel + noise.
**Performance:** Low-Medium. ~0.4ms.
**Textures needed:** TextureColor. Optional: paper texture for higher quality.
**Pipeline stage:** enbeffectpostpass.fx.
**SB integration:** Blur radius from `SB_Precipitation.y` (rain = more wet bleed). Paper wetness from `SB_Precip_Surface.x`. Granulation reduced in interiors.

---

### B2. Film Gate Weave + Projector Flicker (Cinematic)

**Visual:** Subtle frame-to-frame position jitter (gate weave) combined with exposure flicker simulating old mechanical film projection. Frame-quantized motion, not smooth — each "frame" holds for 1/24s equivalent. Note: this is already in CinematicFX — listed here for the projector flicker + frame-hold extension.

**Algorithm:**
```hlsl
float4 PS_ProjectorSim(float2 uv) {
    float time = SB_Render_Frame.x * SB_Render_Frame.y;

    // Frame quantization (24fps film look at 60fps game)
    float filmFrame = floor(time * 24.0);
    float framePhase = frac(time * 24.0);

    // Gate weave (slow, organic drift + fast mechanical vibration)
    float2 weave;
    weave.x = sin(filmFrame * 1.7) * 0.3 + sin(filmFrame * 4.3) * 0.1;
    weave.y = cos(filmFrame * 2.1) * 0.25 + cos(filmFrame * 5.7) * 0.08;
    weave *= PixelSize * ui_WeaveAmount;

    // Breathing (focal length drift)
    float breath = 1.0 + sin(filmFrame * 0.7) * ui_BreathAmount * 0.005;

    float2 filmUV = (uv - 0.5) * breath + 0.5 + weave;
    float3 color = TextureColor.Sample(smpLinear, filmUV).rgb;

    // Projector flicker (exposure variation)
    float flicker = 1.0 + (Hash11(filmFrame) - 0.5) * ui_FlickerAmount;
    // 2-blade shutter = slight brightness dip twice per frame
    float shutterPulse = 1.0 - ui_ShutterDip * abs(sin(framePhase * PI));
    color *= flicker * shutterPulse;

    // Slight warmth (tungsten projection lamp)
    color *= float3(1.04, 1.0, 0.93);

    return float4(color, 1);
}
```

**Instruction count:** ~40-50. Hash + trig + one texture read.
**Performance:** Very Low. ~0.1ms.
**Textures needed:** TextureColor only.
**Pipeline stage:** enbeffectpostpass.fx (last in chain for correct application).
**SB integration:** Flicker suppressed during `SB_Render_StencilInfo.w` (paused). Weave amount from `SB_Render_Jitter.w` (time dilation scales weave speed).

---

### B3. Heat Haze / Mirage (Atmospheric)

**Visual:** Shimmering distortion above hot surfaces, rising columns of warped air. Depth-aware: only affects the mid-ground. Driven by SB temperature/location data.

**Algorithm:**
```hlsl
float4 PS_HeatHaze(float2 uv) {
    float depth = GetLinearDepth(uv);
    float time = SB_Render_Frame.x * SB_Render_Frame.y;

    // Haze mask: only in mid-distance, near ground level
    float depthMask = smoothstep(ui_NearDist, ui_PeakDist, depth)
                    * smoothstep(ui_FarDist, ui_PeakDist, depth);

    // Height mask: distortion stronger near bottom of screen (ground heat)
    float heightMask = smoothstep(0.3, 0.7, uv.y); // stronger in lower half

    float mask = depthMask * heightMask;

    // Distortion: layered sine waves at different frequencies
    float2 distortion;
    distortion.x = sin(uv.y * 80.0 + time * 2.3) * 0.3
                 + sin(uv.y * 150.0 + time * 4.7) * 0.15;
    distortion.y = cos(uv.x * 60.0 + time * 1.9) * 0.5
                 + sin(uv.y * 200.0 + time * 6.1) * 0.1;

    distortion *= PixelSize * ui_Strength * mask;

    float3 color = TextureColor.Sample(smpLinear, uv + distortion).rgb;

    // Slight desaturation in haze region (atmospheric scattering)
    float lum = Luminance(color);
    color = lerp(color, lum, mask * ui_Desaturate * 0.3);

    return float4(color, 1);
}
```

**Instruction count:** ~40-60. Trig for distortion + depth read + one color sample.
**Performance:** Very Low. ~0.1ms.
**Textures needed:** TextureColor, TextureDepth.
**Pipeline stage:** enbeffect.fx (HDR preserving).
**SB integration:** Active only when `SB_Weather_Flags` = pleasant/cloudy (no rain). Strength scales with `SB_Time_Segments1.z` (day) — midday heat. Suppressed in `SB_Interior_Flags` interiors and during `SB_Precipitation`.

---

### B4. Pseudo Pixel Sorting (Geometric/Glitch)

**Visual:** Streaks of sorted pixels based on luminance thresholds — bright pixels "drip" downward or streak horizontally, creating dramatic glitch-art breaks in the image. Used in music videos and experimental art.

**Algorithm:**
```hlsl
// True pixel sorting requires iterative feedback. Pseudo version:
// For each pixel, search along a direction until luminance crosses threshold.
// Blend toward the brightest/darkest pixel found → creates streak illusion.

float4 PS_PseudoPixelSort(float2 uv) {
    float3 baseColor = TextureColor.Sample(smpLinear, uv).rgb;
    float baseLum = Luminance(baseColor);

    // Only sort pixels above/below luminance thresholds
    if (baseLum < ui_ThresholdLow || baseLum > ui_ThresholdHigh)
        return float4(baseColor, 1);

    float2 sortDir = normalize(ui_SortDirection); // e.g., (0, 1) for vertical

    // Search along direction for streak extent
    float3 streakColor = baseColor;
    float streakLum = baseLum;

    [unroll] for (int i = 1; i <= 16; i++) {
        float2 sampleUV = uv + sortDir * PixelSize * float(i) * ui_StreakLength;
        float3 s = TextureColor.Sample(smpLinear, sampleUV).rgb;
        float sLum = Luminance(s);

        // Continue streak if luminance is within threshold band
        if (sLum >= ui_ThresholdLow && sLum <= ui_ThresholdHigh) {
            // Sort: keep brighter (ascending sort)
            if (sLum > streakLum) {
                streakColor = s;
                streakLum = sLum;
            }
        } else break; // threshold boundary = streak end
    }

    return float4(lerp(baseColor, streakColor, ui_Intensity), 1);
}
```

**Instruction count:** ~200-280. Up to 16 sequential texture reads (loop).
**Performance:** Medium. ~0.6ms. Reduce sample count for speed.
**Textures needed:** TextureColor only.
**Pipeline stage:** enbeffectpostpass.fx.
**SB integration:** Sort direction from `SB_Wind_Live.zw` (wind direction = streak direction). Activated during `SB_FX_Time.x` (slow-time) or `SB_FX_Damage` (damage glitch). Threshold driven by `SB_Lightning.z` (lightning flash triggers sort on bright pixels).

---

### B5. Color Blindness Simulation + Daltonize (Color Science)

**Visual:** Simulates protanopia, deuteranopia, or tritanopia vision. Daltonize mode shifts lost color information into visible channels — both an accessibility tool and an artistic filter (the shifted colors create unusual palettes).

**Algorithm:**
```hlsl
// Brettel 1997 / Vienot 1999 simulation matrices (in linear RGB)
// Protanopia: red cones missing
static const float3x3 kProtanopia = float3x3(
    0.152286, 1.052583, -0.204868,
    0.114503, 0.786281,  0.099216,
   -0.003882, -0.048116, 1.051998);

// Deuteranopia: green cones missing
static const float3x3 kDeuteranopia = float3x3(
    0.367322, 0.860646, -0.227968,
    0.280085, 0.672501,  0.047413,
   -0.011820, 0.042940,  0.968881);

// Tritanopia: blue cones missing
static const float3x3 kTritanopia = float3x3(
    1.255528, -0.076749, -0.178779,
   -0.078411,  0.930809,  0.147602,
    0.004733,  0.691367,  0.303900);

float4 PS_CVDSim(float2 uv) {
    float3 scene = TextureColor.Sample(smpLinear, uv).rgb;
    float3 linear = SRGBToLinear(scene);

    float3x3 simMatrix = (ui_CVDType == 0) ? kProtanopia :
                          (ui_CVDType == 1) ? kDeuteranopia : kTritanopia;

    float3 simulated = mul(simMatrix, linear);

    if (ui_Daltonize) {
        // Error = what was lost
        float3 error = linear - simulated;
        // Shift error into visible channels
        float3 correction;
        correction.r = 0;
        correction.g = error.r * 0.7 + error.g * 1.0;
        correction.b = error.r * 0.7 + error.b * 1.0;
        simulated = linear + correction;
    }

    return float4(LinearToSRGB(saturate(
        lerp(linear, simulated, ui_Strength))), 1);
}
```

**Instruction count:** ~40-60. Matrix multiply + conditional correction.
**Performance:** Very Low. ~0.1ms.
**Textures needed:** TextureColor only.
**Pipeline stage:** enbeffect.fx (needs linear color).
**SB integration:** Could be triggered as accessibility option or artistic tool. `SB_FX_Vision.x` (detect life/dead) could trigger daltonize shift for alien perception effect.

---

### B6. Ordered Dithering / Obra Dinn Style (NPR)

**Visual:** Converts scene to 1-bit (or low-bit) rendering with ordered Bayer dithering. The "Return of the Obra Dinn" aesthetic — stark, high-contrast, hauntingly beautiful. Optional: limited color palette (Game Boy, CGA, etc.).

**Algorithm:**
```hlsl
// 8x8 Bayer matrix
static const float kBayer8x8[64] = {
     0,32, 8,40, 2,34,10,42,
    48,16,56,24,50,18,58,26,
    12,44, 4,36,14,46, 6,38,
    60,28,52,20,62,30,54,22,
     3,35,11,43, 1,33, 9,41,
    51,19,59,27,49,17,57,25,
    15,47, 7,39,13,45, 5,37,
    63,31,55,23,61,29,53,21
};

float4 PS_OrderedDither(float2 uv) {
    float3 scene = TextureColor.Sample(smpLinear, uv).rgb;

    // Optional: reduce resolution (pixelate)
    if (ui_Pixelate > 0) {
        float2 pixelUV = floor(uv * ScreenRes / ui_PixelScale)
                       * ui_PixelScale / ScreenRes;
        scene = TextureColor.Sample(smpPoint, pixelUV).rgb;
    }

    float lum = Luminance(scene);

    // Bayer threshold
    int2 pixel = int2(fmod(uv * ScreenRes, 8.0));
    float threshold = kBayer8x8[pixel.y * 8 + pixel.x] / 64.0;

    if (ui_Mode == 0) {
        // 1-bit monochrome
        float bit = step(threshold, lum);
        float3 fg = ui_FGColor; // e.g., slightly warm white
        float3 bg = ui_BGColor; // e.g., deep navy
        return float4(lerp(bg, fg, bit), 1);
    } else {
        // Palette mode: quantize to N colors with dithered transitions
        float3 quantized;
        [unroll] for (int c = 0; c < 3; c++) {
            float val = scene[c] * (ui_PaletteSize - 1);
            float lo = floor(val) / (ui_PaletteSize - 1);
            float hi = ceil(val) / (ui_PaletteSize - 1);
            quantized[c] = (frac(val) > threshold) ? hi : lo;
        }
        return float4(quantized, 1);
    }
}
```

**Instruction count:** ~40-70. Array lookup + comparison + optional pixelate.
**Performance:** Very Low. ~0.1ms.
**Textures needed:** TextureColor only.
**Pipeline stage:** enbeffectpostpass.fx.
**SB integration:** FG/BG colors driven by `SB_Atmos_SkyUpper/Lower` for atmospheric coherence. Pixelate scale from `SB_FX_Time.x` (slow time = more pixelated, dreamlike). Mode switch via `SB_FX_Vision` (night-eye = green monochrome).

---

### B7. Emboss / Bas-Relief from Depth (Photographic/Artistic)

**Visual:** Generates a stone-carved relief effect by computing normals from the depth buffer and applying directional lighting. Scene becomes a carved surface with dramatic shadows and highlights. Like viewing the world through a numismatic lens.

**Algorithm:**
```hlsl
float4 PS_BasRelief(float2 uv) {
    float3 scene = TextureColor.Sample(smpLinear, uv).rgb;

    // Compute screen-space normals from depth
    float dc = GetLinearDepth(uv);
    float dr = GetLinearDepth(uv + float2(PixelSize.x, 0));
    float dl = GetLinearDepth(uv - float2(PixelSize.x, 0));
    float du = GetLinearDepth(uv + float2(0, PixelSize.y));
    float dd = GetLinearDepth(uv - float2(0, PixelSize.y));

    float3 normal;
    normal.x = (dl - dr) * ui_DepthScale;
    normal.y = (dd - du) * ui_DepthScale;
    normal.z = 1.0;
    normal = normalize(normal);

    // Directional light (user-controlled angle)
    float3 lightDir = normalize(float3(
        cos(ui_LightAngle) * sin(ui_LightElevation),
        cos(ui_LightElevation),
        sin(ui_LightAngle) * sin(ui_LightElevation)));

    float NdotL = saturate(dot(normal, lightDir));

    // Relief = base material tinted by directional light
    float3 baseColor = lerp(ui_StoneColor, scene, ui_ColorRetention);
    float3 relief = baseColor * (ui_AmbientLevel + NdotL * (1.0 - ui_AmbientLevel));

    // Edge enhancement (depth discontinuities = chisel marks)
    float edge = abs(dr - dl) + abs(du - dd);
    relief *= 1.0 - saturate(edge * ui_EdgeDarken);

    return float4(relief, 1);
}
```

**Instruction count:** ~50-70. 5 depth reads + normalize + dot product.
**Performance:** Very Low. ~0.15ms.
**Textures needed:** TextureColor, TextureDepth.
**Pipeline stage:** enbeffectpostpass.fx.
**SB integration:** Light angle from `SB_Sun_Direction.xy` (sun drives the relief lighting). Stone color from `SB_Interior_Ambient.rgb` for environmental adaptation.

---

### B8. Spectral Dispersion / Prism Effect (Color Science)

**Visual:** Splits the image into spectral components, sampling RGB at different UV offsets that vary radially from center — creating rainbow fringes around bright objects and edges. More physically motivated than simple chromatic aberration, with wavelength-dependent refraction angles.

**Algorithm:**
```hlsl
float4 PS_SpectralDispersion(float2 uv) {
    float2 center = uv - 0.5;
    float r = length(center);
    float2 dir = center / max(r, 0.001);

    // Wavelength-dependent refraction (Cauchy's equation approx)
    // IOR = A + B/lambda^2, so shorter wavelengths refract more
    // We sample 6 wavelengths and map to RGB via CIE curves

    float3 result = 0;
    float totalWeight = 0;

    // 6 spectral samples across visible range
    static const float kWavelengths[6] = {440, 480, 520, 560, 600, 640}; // nm
    static const float3 kCIE_RGB[6] = {
        float3(0.05, 0.0, 0.6),   // 440nm violet
        float3(0.0, 0.2, 0.8),    // 480nm blue
        float3(0.0, 0.8, 0.1),    // 520nm green
        float3(0.4, 0.7, 0.0),    // 560nm yellow-green
        float3(0.8, 0.3, 0.0),    // 600nm orange
        float3(0.7, 0.0, 0.0),    // 640nm red
    };

    [unroll] for (int i = 0; i < 6; i++) {
        // Shorter wavelength = more refraction = more offset
        float ior = 1.0 + 0.01 / (kWavelengths[i] * kWavelengths[i] * 1e-6);
        float offset = (ior - 1.0) * ui_Strength * r;
        float2 sampleUV = uv + dir * offset;

        float3 s = TextureColor.Sample(smpLinear, sampleUV).rgb;
        float3 weight = kCIE_RGB[i];
        result += s * weight;
        totalWeight += dot(weight, 1);
    }

    return float4(result / (totalWeight / 3.0), 1);
}
```

**Instruction count:** ~80-100. 6 texture reads + wavelength math.
**Performance:** Low. ~0.2ms.
**Textures needed:** TextureColor only.
**Pipeline stage:** enblens.fx (lens optical effect).
**SB integration:** Strength from `SB_Precipitation.y` (water on lens increases dispersion). Sun angle from `SB_Sun_NDC.xy` shifts the dispersion center.

---

### B9. Woodcut / Linocut Print (NPR)

**Visual:** High-contrast black-and-white with hatching lines whose width depends on luminance. Dark areas get thick, closely-spaced cuts; light areas have thin, sparse lines. Strong graphic novel / folk art quality.

**Algorithm:**
```hlsl
float4 PS_Woodcut(float2 uv) {
    float3 scene = TextureColor.Sample(smpLinear, uv).rgb;
    float lum = Luminance(scene);
    float depth = GetLinearDepth(uv);

    // Edge detection for contour lines
    float edge = SobelMagnitude_Depth(uv);
    float contour = step(ui_ContourThreshold, edge);

    // Hatching: lines whose width varies with tone
    float2 screenUV = uv * ScreenRes;

    // Primary hatch direction (can be rotated)
    float freq = ui_LineFrequency;
    float linePhase = frac(screenUV.x * cos(ui_HatchAngle)
                         + screenUV.y * sin(ui_HatchAngle)) / freq;

    // Line width: inversely proportional to luminance
    float lineWidth = lerp(0.8, 0.05, lum);
    float line = step(linePhase, lineWidth);

    // Secondary cross-hatch for deep shadows
    float crossPhase = frac(screenUV.x * cos(ui_HatchAngle + HALF_PI)
                          + screenUV.y * sin(ui_HatchAngle + HALF_PI)) / freq;
    float crossLine = step(crossPhase, lineWidth * 0.7) * step(lum, 0.3);

    float ink = max(max(line, crossLine), contour);

    // Paper and ink colors
    float3 paper = float3(0.95, 0.93, 0.88);
    float3 inkColor = float3(0.05, 0.05, 0.07);

    return float4(lerp(paper, inkColor, ink), 1);
}
```

**Instruction count:** ~70-90. Edge detection + line math.
**Performance:** Very Low. ~0.2ms.
**Textures needed:** TextureColor, TextureDepth.
**Pipeline stage:** enbeffectpostpass.fx.
**SB integration:** Hatch angle from `SB_Sun_Direction` (light direction drives "cut" angle). Ink density from `SB_IS_Cinematic.z` (contrast setting).

---

### B10. Cel Shading with Edge Detection (NPR)

**Visual:** Flat-shaded cartoon look: luminance quantized into 3-5 discrete bands with hard transitions. Black outlines from depth and normal discontinuity detection. The Borderlands / Jet Set Radio aesthetic.

**Algorithm:**
```hlsl
float4 PS_CelShade(float2 uv) {
    float3 scene = TextureColor.Sample(smpLinear, uv).rgb;
    float lum = Luminance(scene);

    // Quantize luminance into bands
    float bands = ui_BandCount; // 3-5
    float quantLum = floor(lum * bands + 0.5) / bands;
    float3 quantized = scene * (quantLum / max(lum, 0.001));

    // Boost saturation for cartoon look
    float3 final = lerp(quantLum, quantized, ui_Saturation);

    // Edge detection: depth-based (large features) + luminance-based (detail)
    // Roberts Cross operator (cheaper than Sobel, 4 taps)
    float dTL = GetLinearDepth(uv + float2(-1, -1) * PixelSize);
    float dTR = GetLinearDepth(uv + float2(1, -1) * PixelSize);
    float dBL = GetLinearDepth(uv + float2(-1, 1) * PixelSize);
    float dBR = GetLinearDepth(uv + float2(1, 1) * PixelSize);
    float depthEdge = abs(dTL - dBR) + abs(dTR - dBL);

    // Luminance edge
    float lTL = Luminance(TextureColor.Sample(smpPoint, uv + float2(-1,-1)*PixelSize).rgb);
    float lBR = Luminance(TextureColor.Sample(smpPoint, uv + float2(1,1)*PixelSize).rgb);
    float lumEdge = abs(lTL - lBR);

    float edge = saturate(depthEdge * ui_DepthEdgeScale + lumEdge * ui_LumEdgeScale);
    float outline = smoothstep(ui_EdgeThreshold, ui_EdgeThreshold + 0.1, edge);

    final = lerp(final, ui_OutlineColor, outline);

    return float4(final, 1);
}
```

**Instruction count:** ~80-100. Quantization + 8 extra taps for edge detection.
**Performance:** Low. ~0.25ms.
**Textures needed:** TextureColor, TextureDepth.
**Pipeline stage:** enbeffectpostpass.fx.
**SB integration:** Band count from `SB_IS_Cinematic.z` (contrast = more bands). Outline color tinted by `SB_Atmos_Ambient.rgb`.

---

## TIER C — Specialist / Experimental

---

### C1. Lomography / Toy Camera (Photographic)

**Visual:** Heavy vignette, cross-processed colors (blues shift green, reds shift yellow), extreme saturation, soft focus at edges. The "cheap plastic lens" aesthetic.

**Key elements:** Strong vignette (cos^4 + mechanical), channel-dependent tone curves mimicking C-41 in E-6 chemistry (green midtone boost, blue highlight suppression, red shadow lift), tunnel vision blur. ~60 instructions. Very cheap. Pipeline: enbeffect.fx. SB: stronger during `SB_Time_Segments2.z` (golden hour).

---

### C2. Zone System Visualization (Color Science)

**Visual:** Overlays Ansel Adams' 11-zone tonal system on the image. Each zone gets a distinct false color or hatching pattern. Zones 0 and X flash as clipping warnings. Diagnostic and artistic.

**Key elements:** Map luminance to 11 zones (0.0-1.0 in ~0.09 steps), assign distinct colors per zone, overlay as translucent wash or replace. ~30 instructions. Negligible cost. Pipeline: enbeffectpostpass.fx. SB: zone exposure offset from `SB_IS_HDR.z` (bloom threshold as middle gray reference).

---

### C3. Posterization + Dithered Transitions (NPR)

**Visual:** Color-reduced flat fills with dithered transitions between tones — combines pop-art posterization with retro dithering. Like a SNES game's pre-rendered backgrounds.

**Key elements:** Quantize per-channel to N levels, apply Bayer dithering at transition boundaries, optional palette mapping. ~50 instructions. Very cheap. Pipeline: enbeffectpostpass.fx.

---

### C4. Fractal Overlay / Julia Set (Geometric)

**Visual:** Animated Julia set fractal blended into the scene via depth-dependent opacity — distant regions dissolve into mathematical patterns. Psychedelic dream-sequence tool.

**Key elements:** Per-pixel Julia iteration (20-40 iterations), depth-weighted alpha blend, animation via SB_Time. ~200-300 instructions (iteration-heavy). Medium cost. Pipeline: enbeffect.fx. SB: Julia c-parameter from `SB_Time.x` (smoothly evolving through the day).

---

### C5. Frequency Separation Detail Enhancement (Advanced Post)

**Visual:** Separates image into low-frequency (color/tone) and high-frequency (detail/texture) layers using Gaussian blur subtraction. Enhances or suppresses each independently — like Photoshop's frequency separation technique for skin retouching, but applied to the whole scene for micro-contrast control.

**Key elements:** 9-tap Gaussian for low-freq, subtract from original for high-freq, independently scale each, recombine. ~80 instructions. Low cost. Pipeline: enbeffect.fx. SB: detail boost from `SB_IS_Cinematic.z` (contrast). Suppressed in fog via `SB_Fog_Density`.

---

### C6. Halftone / Comic Book (NPR)

**Visual:** CMYK dot screens at classic angles (15/45/75/0 degrees), simulating commercial printing. Combined with thick edge outlines, it creates a genuine comic book page aesthetic.

**Key elements:** RGB to CMYK conversion, per-channel halftone screens at unique angles, dot size = ink density, recombine, add Sobel outlines. ~120 instructions. Low cost. Pipeline: enbeffectpostpass.fx.

---

### C7. Chromatic Temporal Grain (Photographic)

**Visual:** Per-channel animated noise that mimics real film stock grain — grain structure correlates with luminance (more grain in mid-tones, less in deep shadows and highlights). Blue noise distribution for perceptually even coverage. Distinct from simple white noise overlay.

**Key elements:** Per-channel hash with different seeds, luminance-dependent amplitude curve, optional blue noise texture for improved distribution, slight blur coupling for realistic grain "clumping." ~50 instructions. Very cheap. Pipeline: enbeffectpostpass.fx. SB: grain intensity from `SB_IS_Cinematic` settings, ISO-like scaling from interior darkness.

---

### C8. Datamosh / Frame Blend Glitch (Temporal)

**Visual:** Pixels displaced along motion vectors from previous frame, creating organic "melting" distortion of the image. Without true temporal feedback, approximate by using depth-reconstructed velocity to smear TextureOriginal over TextureColor.

**Key elements:** Reconstruct velocity from SB_PrevVP (same as motion blur S4), but instead of averaging, use velocity to offset TextureOriginal sampling, blend with current frame using screen/lighten mode. Artifacts are the feature. ~150 instructions. Medium cost. Pipeline: enbeffect.fx. Requires SB VP matrices.

---

### C9. Aurora Borealis Overlay (Atmospheric)

**Visual:** Procedural northern lights curtains in the upper sky, driven by SB aurora fade data. Layered noise creates flowing vertical ribbons with green/purple/pink coloring.

**Key elements:** Polar-coordinate noise (FBM) masked to upper screen, vertical extrusion via ray-march-like stepped sampling (8 steps), color gradient based on "altitude." ~150-200 instructions. Medium cost. Needs sky mask from depth (sky = far depth). Pipeline: enbeffectprepass.fx. SB: `SB_Aurora_Fade` for activation, `SB_Wind_Live` for drift direction, `SB_Atmos_SkyUpper` for color blending.

---

### C10. Tetrahedral LUT Interpolation (Color Science)

**Visual:** Higher-quality 3D LUT application than hardware trilinear — smoother gradients, reduced banding in complex color transforms. The technique DaVinci Resolve uses for superior color accuracy.

**Key elements:** Decompose RGB cube into 5 or 6 tetrahedra per unit cube, determine which tetrahedron contains the input point, barycentric interpolation of 4 LUT vertices instead of 8 (trilinear). Requires a 3D LUT texture (Texture2D atlas encoding). ~60-80 instructions. Low cost. Pipeline: enbeffect.fx. SB: LUT index from weather/ToD for per-weather color grading.

---

## Quick Reference Table

| # | Technique | Category | Inst. Count | Perf. | Custom Tex | Stage |
|---|-----------|----------|-------------|-------|------------|-------|
| S1 | Aniso Kuwahara | Painterly | 400-600 | Med-Hi | No | postpass/effect |
| S2 | Sumi-e Ink Wash | NPR | 120-180 | Low-Med | Optional paper | postpass |
| S3 | Risograph | NPR/Print | 100-150 | Low | No | postpass |
| S4 | Motion Blur | Temporal | 200-250 | Medium | No (needs SB VP) | effect |
| S5 | Cross-Hatching | NPR | 100-140 | Low | No | postpass |
| A1 | Voronoi Glass | Geometric | 80-100 | Low | No | postpass |
| A2 | Thermal Vision | Photographic | 100-130 | Low | No (uses SB lights) | postpass |
| A3 | Infrared Photo | Photographic | 60-80 | V. Low | No | effect |
| A4 | VHS Degradation | Cinematic | 180-220 | Low-Med | No | postpass |
| A5 | Kaleidoscope | Geometric | 30-40 | V. Low | No | effect/postpass |
| A6 | Pointillism | NPR | 50-70 | V. Low | No | postpass |
| A7 | OKLAB Grading | Color Sci | 80-100 | Low | No | effect |
| A8 | Double Exposure | Cinematic | 40-60 | V. Low | No | effect |
| B1 | Watercolor | Painterly | 120-160 | Low-Med | Optional paper | postpass |
| B2 | Projector Flicker | Cinematic | 40-50 | V. Low | No | postpass |
| B3 | Heat Haze | Atmospheric | 40-60 | V. Low | No | effect |
| B4 | Pixel Sorting | Glitch | 200-280 | Medium | No | postpass |
| B5 | CVD Sim/Daltonize | Color Sci | 40-60 | V. Low | No | effect |
| B6 | Ordered Dither | NPR | 40-70 | V. Low | No | postpass |
| B7 | Bas-Relief | Artistic | 50-70 | V. Low | No | postpass |
| B8 | Spectral Dispersion | Color Sci | 80-100 | Low | No | lens |
| B9 | Woodcut | NPR | 70-90 | V. Low | No | postpass |
| B10 | Cel Shading | NPR | 80-100 | Low | No | postpass |
| C1 | Lomography | Photo | ~60 | V. Low | No | effect |
| C2 | Zone System | Color Sci | ~30 | Negligible | No | postpass |
| C3 | Posterize+Dither | NPR | ~50 | V. Low | No | postpass |
| C4 | Fractal Overlay | Geometric | 200-300 | Medium | No | effect |
| C5 | Freq Separation | Adv Post | ~80 | Low | No | effect |
| C6 | Halftone Comic | NPR | ~120 | Low | No | postpass |
| C7 | Chromatic Grain | Photo | ~50 | V. Low | Optional blue noise | postpass |
| C8 | Datamosh | Glitch | ~150 | Medium | No (needs SB VP) | effect |
| C9 | Aurora Overlay | Atmospheric | 150-200 | Medium | No | prepass |
| C10 | Tetrahedral LUT | Color Sci | 60-80 | Low | Yes (LUT texture) | effect |

---

## Implementation Priority Recommendation

**Phase 1 — Immediate high-impact, low-cost:**
1. A3 Infrared Photography (60 inst, unique, never seen in ENB)
2. A5 Kaleidoscope (30 inst, trivial, psychedelic payoff)
3. B6 Ordered Dither (40 inst, Obra Dinn aesthetic)
4. S3 Risograph (100 inst, extremely distinctive)
5. A7 OKLAB Grading (80 inst, foundational color tool)

**Phase 2 — Medium effort, maximum "wow":**
6. S2 Sumi-e Ink Wash (120 inst, dramatic NPR)
7. S5 Cross-Hatching (100 inst, engraving beauty)
8. A4 VHS Degradation (180 inst, nostalgia hit)
9. A2 Thermal Vision (100 inst, gameplay tool + art)
10. S4 Motion Blur (200 inst, uses SB VP — showcases bridge power)

**Phase 3 — Ambitious showpieces:**
11. S1 Anisotropic Kuwahara (400+ inst, the holy grail of NPR)
12. B4 Pseudo Pixel Sorting (200+ inst, glitch art)
13. C9 Aurora Borealis (150 inst, atmospheric magic)
14. C4 Fractal Julia Overlay (200+ inst, psychedelic)

---

## Notes on ENB Pipeline Constraints

- **enbeffectpostpass.fx** is R10G10B10A2 (LDR, clamped [0,1]) — perfect for NPR/artistic effects where HDR is irrelevant
- **enbeffect.fx** is R16G16B16A16F (HDR) — needed for correct color math (OKLAB, infrared, motion blur)
- **enblens.fx** outputs lens effects only (added by enbeffect.fx) — spectral dispersion belongs here
- **enbeffectprepass.fx** runs first — aurora overlay should go here (drawn before everything else)
- All addons should have `[branch] if(!ui_Enable) return input;` early-out guards
- All SB integration should use `SB_HasFeedback()` guards where feedback data is consumed
- Custom textures: only C10 (LUT) strictly requires one; paper textures are optional quality upgrades
- No technique requires compute shaders, geometry shaders, or multi-pass feedback
