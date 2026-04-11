"""
ENB of the Elders — Procedural Texture Generator
Generates improved replacement textures for the ENB shader suite.

Usage: python generate_textures.py [--output-dir DIR]
"""

import numpy as np
from PIL import Image
import os
import sys
import struct

OUTPUT_DIR = os.path.dirname(os.path.abspath(__file__))
if "--output-dir" in sys.argv:
    OUTPUT_DIR = sys.argv[sys.argv.index("--output-dir") + 1]


# =============================================================================
#  1. BLUE NOISE ATLAS — 512x512 RGBA 16-bit
#     Void-and-cluster algorithm for high-quality spatiotemporal dithering.
#     4 channels = 4 independent noise layers for RGBA jitter.
# =============================================================================

def generate_blue_noise_channel(size, seed=0):
    """Generate a single blue noise channel using iterative void-and-cluster."""
    rng = np.random.default_rng(seed)

    # Start with a sparse initial binary pattern
    n_initial = size * size // 10
    pattern = np.zeros((size, size), dtype=bool)

    # Place initial points using Mitchell's best-candidate
    points = []
    candidates_per_point = 20
    for i in range(n_initial):
        if i == 0:
            y, x = rng.integers(0, size, 2)
            pattern[y, x] = True
            points.append((y, x))
            continue

        best_dist = -1
        best_pos = (0, 0)
        for _ in range(candidates_per_point):
            cy, cx = rng.integers(0, size, 2)
            if pattern[cy, cx]:
                continue
            min_d = float('inf')
            for py, px in points:
                # Toroidal distance
                dy = min(abs(cy - py), size - abs(cy - py))
                dx = min(abs(cx - px), size - abs(cx - px))
                d = dy * dy + dx * dx
                if d < min_d:
                    min_d = d
            if min_d > best_dist:
                best_dist = min_d
                best_pos = (cy, cx)

        pattern[best_pos[0], best_pos[1]] = True
        points.append(best_pos)

    # Build ranking via energy minimization
    # Use Gaussian energy kernel for void-and-cluster
    sigma = 1.5
    kernel_size = int(sigma * 4) | 1
    half = kernel_size // 2
    ky, kx = np.mgrid[-half:half+1, -half:half+1]
    kernel = np.exp(-(kx*kx + ky*ky) / (2.0 * sigma * sigma))

    from scipy.signal import fftconvolve

    def compute_energy(binary):
        # Toroidal convolution via tiling
        tiled = np.pad(binary.astype(np.float64), half, mode='wrap')
        energy = fftconvolve(tiled, kernel, mode='same')
        return energy[half:-half, half:-half] if half > 0 else energy

    result = np.zeros((size, size), dtype=np.float64)
    rank = 0

    # Phase 1: Remove points from densest areas (assign low ranks)
    remaining = pattern.copy()
    active_count = np.sum(remaining)

    while active_count > 0:
        energy = compute_energy(remaining)
        energy[~remaining] = -1e10
        idx = np.unravel_index(np.argmax(energy), energy.shape)
        remaining[idx] = False
        result[idx] = rank
        rank += 1
        active_count -= 1

    # Phase 2: Add points to largest voids (assign high ranks)
    placed = pattern.copy()
    total = size * size

    while rank < total:
        energy = compute_energy(placed)
        energy[placed] = 1e10
        idx = np.unravel_index(np.argmin(energy), energy.shape)
        placed[idx] = True
        result[idx] = rank
        rank += 1

    # Normalize to [0, 1]
    result = result / (total - 1)
    return result


def generate_blue_noise_fast(size, seed=0):
    """Fast blue noise approximation using high-pass filtered white noise."""
    rng = np.random.default_rng(seed)
    white = rng.random((size, size))

    # Apply iterative high-pass to push energy to high frequencies
    from scipy.ndimage import gaussian_filter

    result = white.copy()
    for iteration in range(8):
        sigma = max(size / (8.0 * (iteration + 1)), 0.5)
        low = gaussian_filter(result, sigma=sigma, mode='wrap')
        result = result - low + 0.5
        # Histogram equalize to maintain uniform distribution
        flat = result.flatten()
        order = np.argsort(flat)
        ranks = np.empty_like(order)
        ranks[order] = np.arange(len(flat))
        result = ranks.reshape(size, size) / (size * size - 1)

    return result


def generate_blue_noise_atlas():
    """Generate 512x512 RGBA 16-bit blue noise atlas."""
    print("Generating Blue Noise Atlas (512x512 RGBA 16-bit)...")
    size = 512

    # Use fast method — true void-and-cluster is too slow for 512x512
    channels = []
    for ch in range(4):
        print(f"  Channel {ch+1}/4...")
        noise = generate_blue_noise_fast(size, seed=42 + ch * 1337)
        channels.append(noise)

    # Pack into 8-bit RGBA (ENB loads as standard D3D texture — 8-bit sufficient for dithering)
    atlas = np.stack(channels, axis=-1)
    atlas_u8 = (atlas * 255).clip(0, 255).astype(np.uint8)

    img = Image.fromarray(atlas_u8, mode='RGBA')
    path = os.path.join(OUTPUT_DIR, "BlueNoiseAtlas.png")
    img.save(path, optimize=True)
    print(f"  Saved: {path} ({os.path.getsize(path)//1024}KB)")


# =============================================================================
#  2. BOKEH MASK ATLAS — 768x768 RGBA 8-bit
#     6x6 grid of 128x128 bokeh shapes for DOF.
#     Shapes: circle, hexagon, octagon, cat-eye, anamorphic, star variants
# =============================================================================

def draw_bokeh_shape(size, shape_type, params=None):
    """Generate a single bokeh shape mask."""
    y, x = np.mgrid[:size, :size]
    cx, cy = size / 2.0, size / 2.0
    nx = (x - cx) / (size / 2.0 - 2)
    ny = (y - cy) / (size / 2.0 - 2)

    if shape_type == "circle":
        r = np.sqrt(nx*nx + ny*ny)
        mask = np.clip(1.0 - (r - 0.85) * 8.0, 0.0, 1.0)

    elif shape_type == "circle_soft":
        r = np.sqrt(nx*nx + ny*ny)
        mask = np.clip(1.0 - r * r, 0.0, 1.0)

    elif shape_type == "hexagon":
        angle = np.arctan2(ny, nx)
        r = np.sqrt(nx*nx + ny*ny)
        hex_r = np.cos(np.pi / 6) / np.cos((angle % (np.pi / 3)) - np.pi / 6)
        mask = np.clip(1.0 - (r / hex_r - 0.85) * 8.0, 0.0, 1.0)

    elif shape_type == "hexagon_rotated":
        angle = np.arctan2(ny, nx) + np.pi / 6
        r = np.sqrt(nx*nx + ny*ny)
        hex_r = np.cos(np.pi / 6) / np.cos((angle % (np.pi / 3)) - np.pi / 6)
        mask = np.clip(1.0 - (r / hex_r - 0.85) * 8.0, 0.0, 1.0)

    elif shape_type == "octagon":
        abs_nx, abs_ny = np.abs(nx), np.abs(ny)
        oct_d = np.maximum(abs_nx, np.maximum(abs_ny, (abs_nx + abs_ny) * 0.7071))
        mask = np.clip(1.0 - (oct_d - 0.85) * 8.0, 0.0, 1.0)

    elif shape_type == "cat_eye":
        # Simulates mechanical vignetting at edge of frame
        squeeze = params.get("squeeze", 0.6) if params else 0.6
        ny_sq = ny * (1.0 + np.abs(nx) * squeeze)
        r = np.sqrt(nx*nx + ny_sq*ny_sq)
        mask = np.clip(1.0 - (r - 0.85) * 8.0, 0.0, 1.0)

    elif shape_type == "anamorphic":
        # Horizontal stretch for anamorphic lens simulation
        stretch = params.get("stretch", 1.8) if params else 1.8
        r = np.sqrt((nx/stretch)**2 + ny*ny)
        mask = np.clip(1.0 - (r - 0.85) * 8.0, 0.0, 1.0)

    elif shape_type == "star":
        n_points = params.get("points", 6) if params else 6
        angle = np.arctan2(ny, nx)
        r = np.sqrt(nx*nx + ny*ny)
        star_mod = 1.0 - 0.3 * np.abs(np.sin(angle * n_points / 2))
        mask = np.clip(1.0 - (r / star_mod - 0.85) * 8.0, 0.0, 1.0)

    elif shape_type == "ring":
        r = np.sqrt(nx*nx + ny*ny)
        inner = 0.5
        mask = np.clip(1.0 - abs(r - 0.7) / 0.2, 0.0, 1.0)

    elif shape_type == "soap_bubble":
        r = np.sqrt(nx*nx + ny*ny)
        # Bright edge, dim center (soap bubble caustic)
        edge = np.exp(-((r - 0.85)**2) / 0.01)
        center = np.clip(1.0 - r, 0.0, 1.0) * 0.15
        mask = np.clip(edge + center, 0.0, 1.0)

    else:
        mask = np.ones((size, size))

    return (mask * 255).clip(0, 255).astype(np.uint8)


def generate_bokeh_atlas():
    """Generate 768x768 RGBA bokeh mask atlas (6x6 grid of 128x128 tiles)."""
    print("Generating Bokeh Mask Atlas (768x768 RGBA 8-bit)...")
    tile = 128
    grid = 6
    atlas_size = tile * grid
    atlas = np.zeros((atlas_size, atlas_size, 4), dtype=np.uint8)

    shapes = [
        # Row 0: Clean geometric shapes
        ("circle", None),
        ("circle_soft", None),
        ("hexagon", None),
        ("hexagon_rotated", None),
        ("octagon", None),
        ("star", {"points": 6}),
        # Row 1: Cat-eye variants (mechanical vignetting at different field positions)
        ("cat_eye", {"squeeze": 0.3}),
        ("cat_eye", {"squeeze": 0.5}),
        ("cat_eye", {"squeeze": 0.7}),
        ("cat_eye", {"squeeze": 1.0}),
        ("cat_eye", {"squeeze": 1.5}),
        ("cat_eye", {"squeeze": 2.0}),
        # Row 2: Anamorphic variants
        ("anamorphic", {"stretch": 1.3}),
        ("anamorphic", {"stretch": 1.5}),
        ("anamorphic", {"stretch": 1.8}),
        ("anamorphic", {"stretch": 2.0}),
        ("anamorphic", {"stretch": 2.5}),
        ("anamorphic", {"stretch": 3.0}),
        # Row 3: Star variants
        ("star", {"points": 4}),
        ("star", {"points": 5}),
        ("star", {"points": 6}),
        ("star", {"points": 7}),
        ("star", {"points": 8}),
        ("star", {"points": 12}),
        # Row 4: Special shapes
        ("ring", None),
        ("soap_bubble", None),
        ("circle", None),  # duplicate for blending targets
        ("hexagon", None),
        ("octagon", None),
        ("circle_soft", None),
        # Row 5: Reserved / duplicates for interpolation
        ("circle", None),
        ("circle_soft", None),
        ("hexagon", None),
        ("octagon", None),
        ("anamorphic", {"stretch": 1.5}),
        ("cat_eye", {"squeeze": 0.5}),
    ]

    for idx, (shape, params) in enumerate(shapes):
        row = idx // grid
        col = idx % grid
        mask = draw_bokeh_shape(tile, shape, params)
        y0, x0 = row * tile, col * tile
        # Store in all 4 channels (RGBA)
        atlas[y0:y0+tile, x0:x0+tile, 0] = mask
        atlas[y0:y0+tile, x0:x0+tile, 1] = mask
        atlas[y0:y0+tile, x0:x0+tile, 2] = mask
        atlas[y0:y0+tile, x0:x0+tile, 3] = mask

    img = Image.fromarray(atlas, mode='RGBA')
    path = os.path.join(OUTPUT_DIR, "BokehMaskAtlas.png")
    img.save(path, optimize=True)
    print(f"  Saved: {path} ({os.path.getsize(path)//1024}KB)")


# =============================================================================
#  3. UNDERWATER NOISE — 256x256 RGB 8-bit (upgraded from 128x128)
#     Tileable Perlin-style noise for underwater distortion.
# =============================================================================

def perlin_noise_2d(size, scale=4, octaves=4, persistence=0.5, seed=0):
    """Generate tileable 2D Perlin-style noise."""
    rng = np.random.default_rng(seed)
    result = np.zeros((size, size))
    amplitude = 1.0
    frequency = scale

    for _ in range(octaves):
        # Generate tileable gradient noise at this frequency
        grad_size = int(frequency) + 1
        # Random gradient vectors
        angles = rng.random((grad_size, grad_size)) * 2 * np.pi
        gx = np.cos(angles)
        gy = np.sin(angles)

        # Make tileable by wrapping
        gx[-1, :] = gx[0, :]
        gx[:, -1] = gx[:, 0]
        gy[-1, :] = gy[0, :]
        gy[:, -1] = gy[:, 0]

        # Sample positions
        y_coords = np.linspace(0, frequency - frequency/grad_size, size, endpoint=False)
        x_coords = np.linspace(0, frequency - frequency/grad_size, size, endpoint=False)

        layer = np.zeros((size, size))
        for py in range(size):
            for px in range(size):
                fx = x_coords[px]
                fy = y_coords[py]
                x0 = int(fx) % (grad_size - 1)
                y0 = int(fy) % (grad_size - 1)
                x1 = (x0 + 1) % grad_size
                y1 = (y0 + 1) % grad_size

                dx = fx - int(fx)
                dy = fy - int(fy)

                # Smoothstep
                sx = dx * dx * (3 - 2 * dx)
                sy = dy * dy * (3 - 2 * dy)

                # Dot products
                d00 = gx[y0, x0] * dx + gy[y0, x0] * dy
                d10 = gx[y0, x1] * (dx - 1) + gy[y0, x1] * dy
                d01 = gx[y1, x0] * dx + gy[y1, x0] * (dy - 1)
                d11 = gx[y1, x1] * (dx - 1) + gy[y1, x1] * (dy - 1)

                val = (d00 * (1 - sx) + d10 * sx) * (1 - sy) + \
                      (d01 * (1 - sx) + d11 * sx) * sy
                layer[py, px] = val

        result += layer * amplitude
        amplitude *= persistence
        frequency *= 2

    # Normalize to [0, 1]
    result = (result - result.min()) / (result.max() - result.min() + 1e-10)
    return result


def generate_underwater_noise_fast(size=256, seed=0):
    """Fast tileable noise using FFT-based approach."""
    rng = np.random.default_rng(seed)

    # Generate in frequency domain for perfect tileability
    noise = rng.standard_normal((size, size)) + 1j * rng.standard_normal((size, size))

    # Shape spectrum: 1/f noise (pink noise) for natural water caustics
    fy = np.fft.fftfreq(size)[:, None]
    fx = np.fft.fftfreq(size)[None, :]
    freq = np.sqrt(fx**2 + fy**2)
    freq[0, 0] = 1.0

    # 1/f^0.8 spectrum with low-freq boost for large-scale undulation
    spectrum = 1.0 / (freq ** 0.8 + 0.01)
    spectrum[0, 0] = 0  # Remove DC

    shaped = noise * spectrum
    result = np.real(np.fft.ifft2(shaped))

    # Normalize to [0, 1]
    result = (result - result.min()) / (result.max() - result.min() + 1e-10)
    return result


def generate_underwater_noise():
    """Generate 256x256 RGB tileable underwater noise."""
    print("Generating Underwater Noise (256x256 RGB 8-bit)...")
    size = 256

    # Three independent noise channels for RGB distortion
    r = generate_underwater_noise_fast(size, seed=100)
    g = generate_underwater_noise_fast(size, seed=200)
    b = generate_underwater_noise_fast(size, seed=300)

    rgb = np.stack([r, g, b], axis=-1)
    rgb_u8 = (rgb * 255).clip(0, 255).astype(np.uint8)

    img = Image.fromarray(rgb_u8, mode='RGB')
    path = os.path.join(OUTPUT_DIR, "UnderwaterNoise.png")
    img.save(path, optimize=True)
    print(f"  Saved: {path} ({os.path.getsize(path)//1024}KB)")


# =============================================================================
#  4. PARTICLE TEXTURE ATLAS — 768x768 RGBA 8-bit
#     6x6 grid of 128x128 particle shapes for snow, rain, dust, embers, etc.
# =============================================================================

def draw_particle(size, ptype, params=None):
    """Generate a single particle texture tile."""
    y, x = np.mgrid[:size, :size]
    cx, cy = size / 2.0, size / 2.0
    nx = (x - cx) / (size / 2.0)
    ny = (y - cy) / (size / 2.0)
    r = np.sqrt(nx*nx + ny*ny)

    if ptype == "soft_dot":
        # Soft radial falloff — generic particle
        alpha = np.exp(-r * r * 3.0)
        rgb = np.ones_like(r)

    elif ptype == "hard_dot":
        alpha = np.clip(1.0 - (r - 0.6) * 5.0, 0.0, 1.0)
        rgb = np.ones_like(r)

    elif ptype == "snowflake":
        # 6-fold symmetry with fractal detail
        angle = np.arctan2(ny, nx)
        sym6 = np.abs(np.sin(angle * 3))
        branch = np.exp(-((r - sym6 * 0.4) ** 2) * 20.0)
        core = np.exp(-r * r * 8.0)
        alpha = np.clip(branch * 0.7 + core, 0.0, 1.0)
        alpha *= np.clip(1.0 - r, 0.0, 1.0)
        rgb = np.ones_like(r)

    elif ptype == "raindrop":
        # Elongated vertical streak
        stretch = params.get("stretch", 3.0) if params else 3.0
        r_stretch = np.sqrt(nx*nx + (ny/stretch)**2)
        alpha = np.exp(-r_stretch * r_stretch * 5.0)
        rgb = np.ones_like(r)

    elif ptype == "dust":
        # Irregular soft shape
        rng = np.random.default_rng(params.get("seed", 0) if params else 0)
        # Perturb radius with low-freq noise
        angle = np.arctan2(ny, nx)
        n_modes = 5
        perturbation = np.zeros_like(r)
        for m in range(1, n_modes + 1):
            phase = rng.random() * 2 * np.pi
            amp = rng.random() * 0.3 / m
            perturbation += amp * np.sin(angle * m + phase)
        r_perturbed = r - perturbation
        alpha = np.exp(-r_perturbed * r_perturbed * 4.0)
        rgb = np.ones_like(r) * 0.9

    elif ptype == "ember":
        # Bright core, warm falloff
        alpha = np.exp(-r * r * 6.0)
        # Orange-yellow gradient
        rgb = np.ones_like(r)  # Will tint in RGBA

    elif ptype == "spark":
        # Tiny bright point with cross flare
        core = np.exp(-r * r * 50.0)
        cross = np.exp(-np.minimum(np.abs(nx), np.abs(ny)) * 20.0) * np.exp(-r * 2.0)
        alpha = np.clip(core + cross * 0.3, 0.0, 1.0)
        rgb = np.ones_like(r)

    elif ptype == "smoke":
        # Soft, large, with irregular edges
        rng = np.random.default_rng(params.get("seed", 0) if params else 0)
        angle = np.arctan2(ny, nx)
        perturbation = np.zeros_like(r)
        for m in range(1, 8):
            phase = rng.random() * 2 * np.pi
            amp = 0.15 / m
            perturbation += amp * np.sin(angle * m + phase)
        r_p = r - perturbation
        alpha = np.clip(1.0 - r_p * 1.5, 0.0, 1.0) ** 2
        rgb = np.ones_like(r) * 0.7

    elif ptype == "fog_tuft":
        # Very soft, fills most of the tile
        alpha = np.exp(-r * r * 1.5) * 0.6
        rgb = np.ones_like(r) * 0.85

    else:
        alpha = np.exp(-r * r * 3.0)
        rgb = np.ones_like(r)

    result = np.zeros((size, size, 4), dtype=np.uint8)
    result[:, :, 0] = (rgb * 255).clip(0, 255).astype(np.uint8)
    result[:, :, 1] = (rgb * 255).clip(0, 255).astype(np.uint8)
    result[:, :, 2] = (rgb * 255).clip(0, 255).astype(np.uint8)
    result[:, :, 3] = (alpha * 255).clip(0, 255).astype(np.uint8)
    return result


def generate_particle_atlas():
    """Generate 768x768 RGBA particle atlas."""
    print("Generating Particle Texture Atlas (768x768 RGBA 8-bit)...")
    tile = 128
    grid = 6
    atlas = np.zeros((tile * grid, tile * grid, 4), dtype=np.uint8)

    particles = [
        # Row 0: Basic shapes
        ("soft_dot", None),
        ("hard_dot", None),
        ("snowflake", None),
        ("raindrop", {"stretch": 2.0}),
        ("raindrop", {"stretch": 4.0}),
        ("raindrop", {"stretch": 6.0}),
        # Row 1: Dust/debris variants
        ("dust", {"seed": 1}),
        ("dust", {"seed": 2}),
        ("dust", {"seed": 3}),
        ("dust", {"seed": 4}),
        ("dust", {"seed": 5}),
        ("dust", {"seed": 6}),
        # Row 2: Fire/energy
        ("ember", None),
        ("spark", None),
        ("soft_dot", None),
        ("hard_dot", None),
        ("ember", None),
        ("spark", None),
        # Row 3: Smoke/fog
        ("smoke", {"seed": 10}),
        ("smoke", {"seed": 20}),
        ("smoke", {"seed": 30}),
        ("fog_tuft", None),
        ("fog_tuft", None),
        ("smoke", {"seed": 40}),
        # Row 4: Snow variants
        ("snowflake", None),
        ("soft_dot", None),
        ("snowflake", None),
        ("soft_dot", None),
        ("hard_dot", None),
        ("soft_dot", None),
        # Row 5: Misc / reserved
        ("soft_dot", None),
        ("hard_dot", None),
        ("dust", {"seed": 100}),
        ("ember", None),
        ("smoke", {"seed": 50}),
        ("fog_tuft", None),
    ]

    for idx, (ptype, params) in enumerate(particles):
        row = idx // grid
        col = idx % grid
        tile_img = draw_particle(tile, ptype, params)
        y0, x0 = row * tile, col * tile
        atlas[y0:y0+tile, x0:x0+tile] = tile_img

    img = Image.fromarray(atlas, mode='RGBA')
    path = os.path.join(OUTPUT_DIR, "ParticleTexAtlas.png")
    img.save(path, optimize=True)
    print(f"  Saved: {path} ({os.path.getsize(path)//1024}KB)")


# =============================================================================
#  5. CHAR TEXTURE — 512x512 RGB 8-bit
#     Improved character silhouette/mask texture.
# =============================================================================

def generate_char_texture():
    """Generate 512x512 RGB character texture (gradient field for character detection)."""
    print("Generating Character Texture (512x512 RGB 8-bit)...")
    size = 512
    y, x = np.mgrid[:size, :size]
    ny = y / (size - 1.0)
    nx = x / (size - 1.0)

    # Simple radial gradient from center — used as a weight/mask texture
    cx, cy = 0.5, 0.5
    r = np.sqrt((nx - cx)**2 + (ny - cy)**2) * 2.0

    # Smooth bell curve centered on screen
    mask = np.exp(-r * r * 2.0)

    # Slight vertical bias (characters are vertically centered)
    vert_bias = np.exp(-((ny - 0.45) ** 2) * 4.0)
    mask = mask * 0.7 + vert_bias * 0.3

    # Normalize
    mask = (mask - mask.min()) / (mask.max() - mask.min())

    rgb = np.stack([mask, mask, mask], axis=-1)
    rgb_u8 = (rgb * 255).clip(0, 255).astype(np.uint8)

    img = Image.fromarray(rgb_u8, mode='RGB')
    path = os.path.join(OUTPUT_DIR, "CharTexture.png")
    img.save(path, optimize=True)
    print(f"  Saved: {path} ({os.path.getsize(path)//1024}KB)")


# =============================================================================
#  6. LENS DIRT TEXTURE ATLAS — 3840x2160 RGBA 8-bit
#     Procedural lens dirt/smudge patterns.
#     Grid of dirt variants for runtime selection.
# =============================================================================

def generate_lens_dirt_atlas():
    """Generate 3840x2160 RGBA lens dirt atlas with multiple dirt patterns."""
    print("Generating Lens Dirt Atlas (3840x2160 RGBA 8-bit)...")
    width, height = 3840, 2160

    # 4x2 grid of 960x1080 dirt patterns, or 2x1 of full 1920x1080
    # Using 2 rows x 2 cols of 1920x1080 panels
    panel_w, panel_h = 1920, 1080
    atlas = np.zeros((height, width, 4), dtype=np.uint8)

    rng = np.random.default_rng(42)

    for panel_row in range(2):
        for panel_col in range(2):
            panel_idx = panel_row * 2 + panel_col
            print(f"  Panel {panel_idx + 1}/4...")

            y, x = np.mgrid[:panel_h, :panel_w]
            ny = y / panel_h
            nx = x / panel_w

            # Base: subtle radial vignette dirt (more at edges)
            cx, cy = 0.5, 0.5
            r = np.sqrt((nx - cx)**2 + (ny - cy)**2) * 2.0
            base_dirt = np.clip(r - 0.3, 0.0, 1.0) ** 2 * 0.3

            # Add random smudge spots
            n_smudges = 15 + panel_idx * 5
            smudge_layer = np.zeros((panel_h, panel_w))
            for _ in range(n_smudges):
                sx = rng.random()
                sy = rng.random()
                srad = rng.random() * 0.15 + 0.02
                sintensity = rng.random() * 0.4 + 0.1

                dist = np.sqrt((nx - sx)**2 + (ny - sy)**2)
                smudge = np.exp(-(dist / srad)**2) * sintensity
                smudge_layer += smudge

            # Add fingerprint-like streaks
            n_streaks = 3 + panel_idx
            for _ in range(n_streaks):
                angle = rng.random() * np.pi
                offset = rng.random() * 0.6 + 0.2
                streak_width = rng.random() * 0.03 + 0.01
                intensity = rng.random() * 0.2 + 0.05

                proj = np.cos(angle) * (nx - 0.5) + np.sin(angle) * (ny - 0.5)
                perp = -np.sin(angle) * (nx - 0.5) + np.cos(angle) * (ny - 0.5)

                streak = np.exp(-(perp / streak_width)**2) * intensity
                streak *= np.exp(-(proj)**2 * 4.0)
                smudge_layer += streak

            # Add fine dust specks
            n_specks = 200 + panel_idx * 100
            for _ in range(n_specks):
                spx = int(rng.random() * panel_w)
                spy = int(rng.random() * panel_h)
                sprad = rng.integers(1, 4)
                spint = rng.random() * 0.8 + 0.2

                yy, xx = np.ogrid[max(0,spy-sprad):min(panel_h,spy+sprad+1),
                                   max(0,spx-sprad):min(panel_w,spx+sprad+1)]
                dd = np.sqrt((xx - spx)**2 + (yy - spy)**2)
                smudge_layer[max(0,spy-sprad):min(panel_h,spy+sprad+1),
                             max(0,spx-sprad):min(panel_w,spx+sprad+1)] += \
                    np.clip(1.0 - dd / sprad, 0.0, 1.0) * spint

            # Combine
            dirt = np.clip(base_dirt + smudge_layer, 0.0, 1.0)

            # Slight color variation (warm tint for dirt)
            r_ch = (dirt * 255 * 1.0).clip(0, 255).astype(np.uint8)
            g_ch = (dirt * 255 * 0.95).clip(0, 255).astype(np.uint8)
            b_ch = (dirt * 255 * 0.85).clip(0, 255).astype(np.uint8)
            a_ch = (dirt * 255).clip(0, 255).astype(np.uint8)

            y0 = panel_row * panel_h
            x0 = panel_col * panel_w
            atlas[y0:y0+panel_h, x0:x0+panel_w, 0] = r_ch
            atlas[y0:y0+panel_h, x0:x0+panel_w, 1] = g_ch
            atlas[y0:y0+panel_h, x0:x0+panel_w, 2] = b_ch
            atlas[y0:y0+panel_h, x0:x0+panel_w, 3] = a_ch

    img = Image.fromarray(atlas, mode='RGBA')
    path = os.path.join(OUTPUT_DIR, "LensDirtTexAtlas.png")
    img.save(path, optimize=True)
    print(f"  Saved: {path} ({os.path.getsize(path)//1024}KB)")


# =============================================================================
#  7. LENS FROST TEXTURES — 1920x1080 RGBA 8-bit
#     Procedural frost crystal patterns for cold weather effect.
# =============================================================================

def generate_frost_textures():
    """Generate 1920x1080 RGBA frost overlay texture."""
    print("Generating Frost Textures (1920x1080 RGBA 8-bit)...")
    width, height = 1920, 1080

    rng = np.random.default_rng(77)

    y, x = np.mgrid[:height, :width]
    ny = y / height
    nx = x / width

    # Frost grows from edges inward
    edge_dist = np.minimum(
        np.minimum(nx, 1.0 - nx),
        np.minimum(ny, 1.0 - ny)
    )
    edge_mask = np.clip(1.0 - edge_dist * 4.0, 0.0, 1.0) ** 1.5

    # Add crystalline fractal patterns using FFT noise
    frost_detail = np.zeros((height, width))
    for octave in range(5):
        freq_scale = 2 ** (octave + 2)
        noise_r = rng.standard_normal((height, width))
        noise_i = rng.standard_normal((height, width))
        noise_c = noise_r + 1j * noise_i

        fy = np.fft.fftfreq(height)[:, None]
        fx = np.fft.fftfreq(width)[None, :]
        freq = np.sqrt(fx**2 + fy**2)

        # Band-pass at this octave
        center = freq_scale / max(width, height)
        band = np.exp(-((freq - center) / (center * 0.5))**2)

        shaped = np.fft.ifft2(np.fft.fft2(noise_c) * band)
        layer = np.real(shaped)
        layer = (layer - layer.min()) / (layer.max() - layer.min() + 1e-10)

        frost_detail += layer * (0.5 ** octave)

    frost_detail = (frost_detail - frost_detail.min()) / (frost_detail.max() - frost_detail.min() + 1e-10)

    # Combine edge mask with detail
    frost = edge_mask * (0.4 + frost_detail * 0.6)

    # Add bright crystal highlights
    highlights = (frost_detail > 0.7).astype(np.float64) * frost * 0.3
    frost += highlights
    frost = np.clip(frost, 0.0, 1.0)

    # RGBA: white frost with alpha for blending
    r_ch = (frost * 240 + 15).clip(0, 255).astype(np.uint8)
    g_ch = (frost * 245 + 10).clip(0, 255).astype(np.uint8)
    b_ch = (frost * 255).clip(0, 255).astype(np.uint8)
    a_ch = (frost * 255).clip(0, 255).astype(np.uint8)

    img_data = np.stack([r_ch, g_ch, b_ch, a_ch], axis=-1)
    img = Image.fromarray(img_data, mode='RGBA')
    path = os.path.join(OUTPUT_DIR, "LensFrostTextures.png")
    img.save(path, optimize=True)
    print(f"  Saved: {path} ({os.path.getsize(path)//1024}KB)")


# =============================================================================
#  8. LENS FROST REFRACTION — 1920x1080 RGB 8-bit
#     Normal map for frost refraction distortion.
# =============================================================================

def generate_frost_refraction():
    """Generate 1920x1080 RGB frost refraction normal map."""
    print("Generating Frost Refraction Texture (1920x1080 RGB 8-bit)...")
    width, height = 1920, 1080

    rng = np.random.default_rng(88)

    # Generate height field from FFT noise
    noise = rng.standard_normal((height, width)) + 1j * rng.standard_normal((height, width))

    fy = np.fft.fftfreq(height)[:, None]
    fx = np.fft.fftfreq(width)[None, :]
    freq = np.sqrt(fx**2 + fy**2)
    freq[0, 0] = 1.0

    # Spectrum shaped for crystalline patterns (emphasize mid frequencies)
    spectrum = np.exp(-((np.log(freq + 1e-10) - np.log(0.02))**2) / 0.5)
    spectrum[0, 0] = 0

    heightfield = np.real(np.fft.ifft2(np.fft.fft2(noise) * spectrum))
    heightfield = (heightfield - heightfield.min()) / (heightfield.max() - heightfield.min() + 1e-10)

    # Edge mask — refraction strongest at edges where frost forms
    y, x = np.mgrid[:height, :width]
    ny, nx = y / height, x / width
    edge_dist = np.minimum(np.minimum(nx, 1.0 - nx), np.minimum(ny, 1.0 - ny))
    edge_mask = np.clip(1.0 - edge_dist * 4.0, 0.0, 1.0) ** 1.5

    heightfield *= edge_mask

    # Compute normals from height field (Sobel)
    from scipy.ndimage import sobel
    dx_field = sobel(heightfield, axis=1, mode='wrap')
    dy_field = sobel(heightfield, axis=0, mode='wrap')

    # Pack as normal map: R = dx, G = dy, B = 1.0 (tangent space)
    # Normalize and map to [0, 255] with 128 = zero displacement
    strength = 2.0
    nx_map = (-dx_field * strength)
    ny_map = (-dy_field * strength)
    nz_map = np.ones_like(nx_map)

    # Normalize
    length = np.sqrt(nx_map**2 + ny_map**2 + nz_map**2)
    nx_map /= length
    ny_map /= length
    nz_map /= length

    # Map [-1,1] to [0,255]
    r = ((nx_map * 0.5 + 0.5) * 255).clip(0, 255).astype(np.uint8)
    g = ((ny_map * 0.5 + 0.5) * 255).clip(0, 255).astype(np.uint8)
    b = ((nz_map * 0.5 + 0.5) * 255).clip(0, 255).astype(np.uint8)

    img_data = np.stack([r, g, b], axis=-1)
    img = Image.fromarray(img_data, mode='RGB')
    path = os.path.join(OUTPUT_DIR, "LensFrostRefractionTexture.png")
    img.save(path, optimize=True)
    print(f"  Saved: {path} ({os.path.getsize(path)//1024}KB)")


# =============================================================================
#  9. LENS RAIN DROPLETS — 4096x4096 RGBA 8-bit
#     Atlas of rain droplet normal maps for wet lens effect.
# =============================================================================

def generate_rain_droplets():
    """Generate 4096x4096 RGBA rain droplet atlas."""
    print("Generating Rain Droplets Atlas (4096x4096 RGBA 8-bit)...")
    size = 4096

    rng = np.random.default_rng(99)

    # Initialize with flat normal (128, 128, 255, 0)
    atlas = np.zeros((size, size, 4), dtype=np.uint8)
    atlas[:, :, 0] = 128  # nx = 0
    atlas[:, :, 1] = 128  # ny = 0
    atlas[:, :, 2] = 255  # nz = 1
    atlas[:, :, 3] = 0    # alpha = no droplet

    # Place droplets of varying sizes
    n_large = 80    # large droplets (radius 30-80px)
    n_medium = 400  # medium droplets (radius 8-30px)
    n_small = 3000  # small droplets (radius 2-8px)
    n_tiny = 8000   # tiny droplets (radius 1-2px)

    droplets = []
    for _ in range(n_large):
        droplets.append((rng.integers(80, size-80), rng.integers(80, size-80),
                         rng.integers(30, 80), rng.random() * 0.3 + 0.7))
    for _ in range(n_medium):
        droplets.append((rng.integers(30, size-30), rng.integers(30, size-30),
                         rng.integers(8, 30), rng.random() * 0.4 + 0.5))
    for _ in range(n_small):
        droplets.append((rng.integers(8, size-8), rng.integers(8, size-8),
                         rng.integers(2, 8), rng.random() * 0.3 + 0.3))
    for _ in range(n_tiny):
        droplets.append((rng.integers(2, size-2), rng.integers(2, size-2),
                         rng.integers(1, 3), rng.random() * 0.2 + 0.2))

    print(f"  Placing {len(droplets)} droplets...")

    for dy, dx, rad, intensity in droplets:
        # Compute droplet normal map in local region
        y0 = max(0, dy - rad - 1)
        y1 = min(size, dy + rad + 2)
        x0 = max(0, dx - rad - 1)
        x1 = min(size, dx + rad + 2)

        yy, xx = np.mgrid[y0:y1, x0:x1]
        lx = (xx - dx) / max(rad, 1)
        ly = (yy - dy) / max(rad, 1)
        lr = np.sqrt(lx*lx + ly*ly)

        # Droplet shape: hemisphere with slightly flattened top
        inside = lr < 1.0
        if not np.any(inside):
            continue

        # Height = sqrt(1 - r^2) hemisphere
        h = np.zeros_like(lr)
        h[inside] = np.sqrt(1.0 - lr[inside]**2)

        # Normal from height gradient
        # For a hemisphere: n = (x/r, y/r, sqrt(1-r^2)) normalized
        norm_x = np.zeros_like(lr)
        norm_y = np.zeros_like(lr)
        norm_z = np.ones_like(lr)

        norm_x[inside] = -lx[inside] * intensity
        norm_y[inside] = -ly[inside] * intensity
        norm_z[inside] = h[inside]

        length = np.sqrt(norm_x**2 + norm_y**2 + norm_z**2)
        length = np.maximum(length, 1e-6)
        norm_x /= length
        norm_y /= length
        norm_z /= length

        alpha = np.zeros_like(lr)
        alpha[inside] = intensity * (1.0 - lr[inside]**2)

        # Composite onto atlas (max alpha blending)
        r_val = ((norm_x * 0.5 + 0.5) * 255).clip(0, 255).astype(np.uint8)
        g_val = ((norm_y * 0.5 + 0.5) * 255).clip(0, 255).astype(np.uint8)
        b_val = ((norm_z * 0.5 + 0.5) * 255).clip(0, 255).astype(np.uint8)
        a_val = (alpha * 255).clip(0, 255).astype(np.uint8)

        # Only write where new alpha > existing alpha
        existing_a = atlas[y0:y1, x0:x1, 3]
        mask = a_val > existing_a
        atlas[y0:y1, x0:x1, 0][mask] = r_val[mask]
        atlas[y0:y1, x0:x1, 1][mask] = g_val[mask]
        atlas[y0:y1, x0:x1, 2][mask] = b_val[mask]
        atlas[y0:y1, x0:x1, 3][mask] = a_val[mask]

    img = Image.fromarray(atlas, mode='RGBA')
    path = os.path.join(OUTPUT_DIR, "LensRainDroplets.png")
    img.save(path, optimize=True)
    print(f"  Saved: {path} ({os.path.getsize(path)//1024}KB)")


# =============================================================================
#  MAIN
# =============================================================================

if __name__ == "__main__":
    print(f"Output directory: {OUTPUT_DIR}\n")

    # Check for scipy (needed by blue noise and frost refraction)
    has_scipy = False
    try:
        import scipy
        has_scipy = True
    except ImportError:
        print("WARNING: scipy not found. Blue noise will use fast approximation.")
        print("         Frost refraction will skip Sobel normals.")
        print("         Install with: pip install scipy\n")

    generate_blue_noise_atlas()
    generate_bokeh_atlas()
    generate_underwater_noise()
    generate_particle_atlas()
    generate_char_texture()
    generate_lens_dirt_atlas()
    generate_frost_textures()
    if has_scipy:
        generate_frost_refraction()
    else:
        print("Skipping frost refraction (needs scipy)")
    generate_rain_droplets()

    print("\nDone! All textures generated.")
