# RAW Phase 2 -- GPU Performance Targets

Copyright (c) 2026 Zain D. Harper (papacr0w). All rights reserved.

---

## Reference Hardware

| Tier | GPU | VRAM | Target FPS (1080p) |
|------|-----|------|-------------------|
| Minimum | GTX 1060 6GB | 6 GB | 30 fps (33.3ms budget) |
| Recommended | RTX 3060 | 12 GB | 60 fps (16.6ms budget) |
| High-end | RTX 3070+ | 8+ GB | 60 fps with Ultra preset |

All budgets below measured at 1920x1080. Multiply by ~1.8x for 1440p, ~2.5x for 4K.

---

## Per-Effect GPU Budget

### Tier 1: Core Effects (always-on candidates)

| # | Effect | Pass Count | Resolution | Budget (ms) | VRAM (MB) | Priority |
|---|--------|-----------|------------|-------------|-----------|----------|
| 1 | Scene Compositor | 1 PS | Full | 0.30 | 16 | 90 (PrePresent) |
| 2 | GTAO (4dir/8step) | 3 CS | Full | 1.00 | 32 | PostGeometry |
| 3 | Contact Shadows (16 steps) | 2 CS | Full | 0.50 | 6 | PostGeometry |
| 4 | Skylighting (6dir/8step) | 4 CS | Full + 3D | 0.80 | 10 | PostGeometry |
| 5 | Bloom | 8 CS + 1 PS | Half chain | 0.80 | 20 | 10 (PrePresent) |
| 6 | Color Pipeline (all stages) | 1 PS | Full | 0.50 | 24 | 50 (PrePresent) |
| | **Tier 1 Total** | | | **3.90** | **108** | |

### Tier 2: Advanced Effects (Quality/Ultra preset)

| # | Effect | Pass Count | Resolution | Budget (ms) | VRAM (MB) | Priority |
|---|--------|-----------|------------|-------------|-----------|----------|
| 7 | SSR (64 steps) | 3 CS | Half | 1.50 | 24 | PostGeometry |
| 8 | SSGI (8 rays/32 steps) | 4 CS+PS | Half + 3D | 2.00 | 32 | PostGeometry |
| 9 | Indirect Specular | 1 CS | Full | 0.50 | 32 | 21 (PostGeometry) |
| 10 | Volumetric Lighting (64 steps) | 2 CS | Half | 1.50 | 20 | 19 (PostGeometry) |
| 11 | Volumetric Clouds | 1 CS + 1 PS | Quarter | 3.00 | 20 | PostGeometry |
| | **Tier 2 Total** | | | **8.50** | **128** | |

### Tier 3: Polish Effects (all optional)

| # | Effect | Pass Count | Resolution | Budget (ms) | VRAM (MB) | Priority |
|---|--------|-----------|------------|-------------|-----------|----------|
| 12 | DoF (Medium quality) | 5 CS+PS | Full + Half | 2.00 | 40 | 30 (PrePresent) |
| 13 | Lens Effects | 4 CS + 1 PS | Quarter + Full | 1.20 | 28 | 20 (PrePresent) |
| 14 | SSS (H+V pass) | 2 CS | Full | 0.80 | 40 | 20 (PostGeometry) |
| 15 | Atmosphere (LUT amortized) | LUT CS + sky | LUT + Full | 0.30 | 4 | PostGeometry |
| 16 | Underwater | 3 CS + 1 PS | Quarter + Full | 1.50 | 16 | 5 (PrePresent) |
| | **Tier 3 Total** | | | **5.80** | **128** | |

### Tier 4: Lightweight Effects

| # | Effect | Pass Count | Resolution | Budget (ms) | VRAM (MB) | Priority |
|---|--------|-----------|------------|-------------|-----------|----------|
| 17 | Grass Lighting | 1 CS | Full | 0.20 | 2 | PostGeometry |
| 18 | Tree LOD Lighting | 1 CS | Full | 0.20 | 2 | PostGeometry |
| 19 | Particle Lighting | 3 CS | Quarter + Full | 0.80 | 12 | 23 (PostGeometry) |
| 20 | Screen-Space Decals | 1 CS | Full | 0.30 | 4 | 22 (PostGeometry) |
| 21 | Water Blending | 1 CS | Full | 0.30 | 4 | PostGeometry |
| 22 | Dynamic Cubemap (1 face/frame) | 2 CS | 128x128 | 0.30 | 1 | PostGeometry |
| | **Tier 4 Total** | | | **2.10** | **25** | |

### Infrastructure (always running)

| # | System | Budget (ms) | VRAM (MB) | Notes |
|---|--------|-------------|-----------|-------|
| 23 | ToneMap Manager | 0.30 | 8 | Auto-exposure CS + tonemap PS |
| 24 | Luminance Histogram | 0.20 | 2 | 256-bin GPU histogram + reduction |
| 25 | HiZ Pyramid | 0.15 | 8 | Depth copy + mip chain (feeds all effects) |
| 26 | Denoise Manager | --- | --- | Profiled within consuming effects |
| 27 | Motion Vector Gen | 0.20 | 8 | Per-pixel motion vectors |
| 28 | TAA Resolve | 0.30 | 16 | Temporal anti-aliasing |
| | **Infrastructure Total** | **1.15** | **42** | |

---

## Preset GPU Budgets

### Performance Preset
Target: GTX 1060, 60 fps at 1080p (16.6ms total, ~8ms for RAW)

| Effect | Setting | Budget |
|--------|---------|--------|
| GTAO | 2 dirs, 4 steps | 0.50ms |
| Contact Shadows | 12 steps | 0.35ms |
| Bloom | default | 0.80ms |
| Color Pipeline | Exposure + ToneMap + Dither only | 0.30ms |
| Compositor | AO + Shadows only | 0.20ms |
| Infrastructure | HiZ + Histogram + ToneMap | 0.65ms |
| **Total** | | **2.80ms** |
| **VRAM** | | **~80 MB** |

### Quality Preset
Target: RTX 3060, 60 fps at 1080p (16.6ms total, ~12ms for RAW)

| Effect | Setting | Budget |
|--------|---------|--------|
| GTAO | 4 dirs, 8 steps + bounce | 1.00ms |
| Contact Shadows | 16 steps | 0.50ms |
| Skylighting | 6 dirs, 8 steps | 0.80ms |
| SSR | 64 steps, half-res | 1.50ms |
| Bloom | default | 0.80ms |
| Color Pipeline | all stages | 0.50ms |
| Compositor | AO + GI + SSR + Shadows + Sky | 0.30ms |
| Dynamic Cubemap | 1 face/frame | 0.30ms |
| Grass + Tree LOD Lighting | default | 0.40ms |
| Infrastructure | HiZ + Histogram + ToneMap + TAA + MoVec | 1.15ms |
| **Total** | | **7.25ms** |
| **VRAM** | | **~200 MB** |

### Ultra Preset
Target: RTX 3070+, 60 fps at 1080p (16.6ms total, ~14ms for RAW)

| Effect | Setting | Budget |
|--------|---------|--------|
| GTAO | 6 dirs, 12 steps + bounce | 1.50ms |
| Contact Shadows | 24 steps | 0.60ms |
| Skylighting | 8 dirs, 12 steps | 1.00ms |
| SSR | 64 steps, half-res | 1.50ms |
| SSGI | 8 rays, 32 steps | 2.00ms |
| Indirect Specular | default | 0.50ms |
| Volumetric Lighting | 64 steps | 1.50ms |
| Bloom | default | 0.80ms |
| DoF | Medium quality | 2.00ms |
| Color Pipeline | all stages + FILM | 0.50ms |
| Lens Effects | all sub-effects | 1.20ms |
| Compositor | all inputs | 0.30ms |
| Grass + Tree + Particle + Water + Decals | default | 1.80ms |
| Dynamic Cubemap | 1 face/frame | 0.30ms |
| Infrastructure | full | 1.15ms |
| **Total** | | **16.65ms** |
| **VRAM** | | **~390 MB** |

> NOTE: Ultra at 1080p is at the 60 fps limit. Users at higher resolutions
> should drop to Quality preset or disable Clouds + SSGI first.

---

## Scaling Guidelines

### Resolution Scaling

| Effect | 720p | 1080p | 1440p | 4K |
|--------|------|-------|-------|----|
| Full-res CS (GTAO, Shadows, SSS) | 0.56x | 1.0x | 1.78x | 4.0x |
| Half-res CS (SSR, SSGI trace, VolumetricLight) | 0.56x | 1.0x | 1.78x | 4.0x |
| Quarter-res CS (Clouds, Particles, Underwater) | 0.56x | 1.0x | 1.78x | 4.0x |
| Fullscreen PS (Compositor, ColorPipeline, DoF composite) | 0.56x | 1.0x | 1.78x | 4.0x |
| Fixed-res (Cubemap 128^2, Noise 128^3, LUT 256x64) | 1.0x | 1.0x | 1.0x | 1.0x |

### What to Disable First (Cost-Benefit)

When over budget, disable in this order (highest cost, lowest visual impact first):

1. **Volumetric Clouds** (3.0ms) -- most expensive single effect
2. **SSGI** (2.0ms) -- expensive, GTAO bounce provides partial substitute
3. **DoF** (2.0ms) -- artistic effect, not lighting-critical
4. **Volumetric Lighting** (1.5ms) -- god rays are nice but not essential
5. **SSR** (1.5ms) -- fallback to cubemap-only reflections
6. **Lens Effects** (1.2ms) -- purely cosmetic
7. **GTAO directions/steps** -- drop from 6/12 to 4/8 to 2/4

### What to Never Disable

These are essentially free and provide foundational data:

- HiZ Pyramid (0.15ms) -- feeds every depth-reading effect
- Luminance Histogram (0.20ms) -- feeds auto-exposure
- Scene Compositor (0.30ms) -- composites whatever effects are active

---

## VRAM Budget Summary

| Category | VRAM at 1080p |
|----------|--------------|
| Performance preset | ~80 MB |
| Quality preset | ~200 MB |
| Ultra preset | ~390 MB |
| Maximum theoretical (all effects, Ultra settings) | ~430 MB |

All budgets are additional to Skyrim's base VRAM usage (~2-3 GB with textures).
A 6 GB card can run Quality. An 8 GB card can run Ultra at 1080p.

---

## Profiling Methodology

1. Use the GPU profiler overlay (F11) for per-pass timing
2. Sort by cost (Phase 1C.1 feature -- heaviest first toggle)
3. Record timings at each test location (GPU utilization varies by scene complexity)
4. Take the 95th percentile over 30 seconds of gameplay (not min/avg)
5. Frame capture (F10) exports CSV for offline analysis

### Red Flags

- Any single effect > 4ms = investigate
- Total RAW overhead > 16ms at 1080p = over budget
- Any effect with 0.0ms = not executing (broken or disabled)
- Frame-to-frame variance > 2x = potential GPU stall or sync issue

---

## Measurement Template

Use this table during profiling sessions:

| Effect | Location | Setting | GPU ms (95th pct) | Budget | Status |
|--------|----------|---------|-------------------|--------|--------|
| GTAO | L1 | 4d/8s | ___ | 1.0 | ___ |
| GTAO | L3 | 4d/8s | ___ | 1.0 | ___ |
| ContactShadow | L1 | 16 steps | ___ | 0.5 | ___ |
| Skylighting | L1 | 6d/8s | ___ | 0.8 | ___ |
| SSR | L7 | 64 steps | ___ | 1.5 | ___ |
| SSGI | L3 | 8r/32s | ___ | 2.0 | ___ |
| Bloom | L1 | default | ___ | 0.8 | ___ |
| ColorPipeline | L2 | all stages | ___ | 0.5 | ___ |
| DoF | L1 | Medium | ___ | 2.0 | ___ |
| Lens | L1 | all | ___ | 1.2 | ___ |
| Clouds | L4 | default | ___ | 3.0 | ___ |
| VolumetricLight | L2 | 64 steps | ___ | 1.5 | ___ |
| SSS | L8 | default | ___ | 0.8 | ___ |
| WaterBlend | L7 | default | ___ | 0.3 | ___ |
| GrassLight | L1 | default | ___ | 0.2 | ___ |
| TreeLOD | L1 | default | ___ | 0.2 | ___ |
| Particles | L3 | default | ___ | 0.8 | ___ |
| Decals | L1 | default | ___ | 0.3 | ___ |
| IndirectSpec | L8 | default | ___ | 0.5 | ___ |
| Cubemap | L8 | default | ___ | 0.3 | ___ |
| Atmosphere | L1 | default | ___ | 0.3 | ___ |
| Underwater | L7 | default | ___ | 1.5 | ___ |
| ToneMap | L1 | AgX | ___ | 0.3 | ___ |
| Histogram | L1 | default | ___ | 0.2 | ___ |
| HiZ | L1 | default | ___ | 0.15 | ___ |
| MotionVec | L1 | default | ___ | 0.2 | ___ |
| TAA | L1 | default | ___ | 0.3 | ___ |
| Compositor | L1 | all inputs | ___ | 0.3 | ___ |
| **TOTAL** | | | ___ | **~16.5** | ___ |
