#!/usr/bin/env python3
"""
Film Stock LUT Generator for ENB / SkyrimBridge
================================================

Generates scientifically-grounded 3D LUT atlases from film stock
characteristic curve (H&D curve) parameters. Supports:

  - Negative → Print dual-stage pipeline
  - Tiled 2D atlas output (ENB-compatible PNG)
  - Industry-standard .cube file export
  - Per-channel H&D curve modeling
  - Color temperature sensitivity
  - Multiple film stocks with published-data-based parameters

Output: 512×512 PNG (64³ 3D LUT as 8×8 grid of 64×64 tiles)
        Also exports .cube for validation in DaVinci Resolve

Usage:
  python3 film_lut_generator.py                    # Generate all stocks
  python3 film_lut_generator.py --stock kodak_500t  # Specific stock
  python3 film_lut_generator.py --size 33           # 33³ resolution
  python3 film_lut_generator.py --negative-only     # Just negative, no print
  python3 film_lut_generator.py --list              # List available stocks

Author: Zain Dana Harper / SkyrimBridge Project
Date: March 2026
"""

import numpy as np
from PIL import Image
import argparse
import os
import sys
from dataclasses import dataclass, field
from typing import Optional, Tuple, Dict

# ─────────────────────────────────────────────────────────────────────────────
# Film Stock Parameter Definitions
# ─────────────────────────────────────────────────────────────────────────────

@dataclass
class FilmStock:
    """Parameters defining a film stock's characteristic behavior.
    
    Based on published sensitometric data from Kodak and Fujifilm technical
    data sheets, calibrated against known film emulation references (Juan Melara
    PFE, Dehancer documentation).
    
    Each parameter is a 3-tuple for (Red, Green, Blue) emulsion layers.
    """
    name: str
    stock_type: str          # 'negative', 'print', 'reversal'
    color_temp: float        # Balanced color temperature (K)
    
    fog: np.ndarray          # Base fog density per layer [R, G, B]
    gamma: np.ndarray        # Linear region slope per layer
    dmax: np.ndarray         # Maximum density per layer
    speed: np.ndarray        # Log exposure offset per layer
    toe_length: np.ndarray   # Toe region extent per layer
    shoulder_len: np.ndarray # Shoulder region extent per layer
    
    # Optional descriptive
    description: str = ""
    
    def __post_init__(self):
        """Ensure all arrays are numpy float64."""
        for attr in ['fog', 'gamma', 'dmax', 'speed', 'toe_length', 'shoulder_len']:
            val = getattr(self, attr)
            if not isinstance(val, np.ndarray):
                setattr(self, attr, np.array(val, dtype=np.float64))


# ── Negative Stocks ──────────────────────────────────────────────────────────

KODAK_5219_500T = FilmStock(
    name="Kodak Vision3 500T 5219",
    stock_type="negative",
    color_temp=3200.0,
    fog=np.array([0.10, 0.08, 0.12]),
    gamma=np.array([0.62, 0.65, 0.58]),
    dmax=np.array([2.60, 2.70, 2.50]),
    speed=np.array([-1.80, -1.90, -1.70]),
    toe_length=np.array([0.40, 0.35, 0.45]),
    shoulder_len=np.array([0.30, 0.30, 0.35]),
    description="Fast tungsten-balanced cinema negative. Slightly cool in daylight."
)

KODAK_5207_250D = FilmStock(
    name="Kodak Vision3 250D 5207",
    stock_type="negative",
    color_temp=5500.0,
    fog=np.array([0.08, 0.06, 0.10]),
    gamma=np.array([0.60, 0.63, 0.55]),
    dmax=np.array([2.80, 2.90, 2.70]),
    speed=np.array([-1.50, -1.55, -1.40]),
    toe_length=np.array([0.35, 0.30, 0.40]),
    shoulder_len=np.array([0.35, 0.35, 0.40]),
    description="Medium-speed daylight negative. Clean, fine grain, natural colors."
)

KODAK_5213_200T = FilmStock(
    name="Kodak Vision3 200T 5213",
    stock_type="negative",
    color_temp=3200.0,
    fog=np.array([0.09, 0.07, 0.11]),
    gamma=np.array([0.58, 0.61, 0.54]),
    dmax=np.array([2.70, 2.80, 2.60]),
    speed=np.array([-1.60, -1.65, -1.50]),
    toe_length=np.array([0.38, 0.33, 0.43]),
    shoulder_len=np.array([0.32, 0.32, 0.37]),
    description="Medium-speed tungsten negative. Classic cinema look."
)

CINESTILL_800T = FilmStock(
    name="CineStill 800T",
    stock_type="negative",
    color_temp=3200.0,
    fog=np.array([0.12, 0.10, 0.14]),
    gamma=np.array([0.64, 0.67, 0.60]),
    dmax=np.array([2.40, 2.50, 2.30]),
    speed=np.array([-2.00, -2.10, -1.90]),
    toe_length=np.array([0.42, 0.37, 0.47]),
    shoulder_len=np.array([0.28, 0.28, 0.33]),
    description="Vision3 500T without remjet. Halation-prone, distinctive look."
)

KODACHROME_64 = FilmStock(
    name="Kodachrome 64",
    stock_type="reversal",
    color_temp=5500.0,
    fog=np.array([0.04, 0.03, 0.05]),
    gamma=np.array([1.80, 1.90, 1.70]),  # High gamma — reversal film
    dmax=np.array([3.20, 3.30, 3.10]),
    speed=np.array([-1.20, -1.30, -1.10]),
    toe_length=np.array([0.20, 0.18, 0.22]),
    shoulder_len=np.array([0.15, 0.15, 0.18]),
    description="Legendary reversal film. Extremely saturated, warm, iconic."
)

FUJI_VELVIA_50 = FilmStock(
    name="Fuji Velvia 50",
    stock_type="reversal",
    color_temp=5500.0,
    fog=np.array([0.03, 0.02, 0.04]),
    gamma=np.array([1.90, 2.00, 1.85]),  # Even higher contrast than Kodachrome
    dmax=np.array([3.30, 3.40, 3.20]),
    speed=np.array([-1.10, -1.20, -1.00]),
    toe_length=np.array([0.18, 0.16, 0.20]),
    shoulder_len=np.array([0.12, 0.12, 0.15]),
    description="Hyper-saturated reversal. Legendary for landscape photography."
)

# ── Print Stocks ─────────────────────────────────────────────────────────────

KODAK_2383 = FilmStock(
    name="Kodak 2383",
    stock_type="print",
    color_temp=5500.0,
    fog=np.array([0.05, 0.04, 0.06]),
    gamma=np.array([2.80, 2.70, 2.60]),  # High gamma — contrast expansion
    dmax=np.array([3.20, 3.10, 3.00]),
    speed=np.array([-0.50, -0.55, -0.45]),
    toe_length=np.array([0.20, 0.18, 0.22]),
    shoulder_len=np.array([0.25, 0.25, 0.30]),
    description="The cinema print stock. Warm, rich mid-tones, iconic highlight rolloff."
)

KODAK_2393 = FilmStock(
    name="Kodak 2393",
    stock_type="print",
    color_temp=5500.0,
    fog=np.array([0.04, 0.03, 0.05]),
    gamma=np.array([3.00, 2.90, 2.80]),  # Higher contrast than 2383
    dmax=np.array([3.30, 3.20, 3.10]),
    speed=np.array([-0.45, -0.50, -0.40]),
    toe_length=np.array([0.18, 0.16, 0.20]),
    shoulder_len=np.array([0.22, 0.22, 0.28]),
    description="Higher contrast variant of 2383. Punchier, more dramatic."
)

FUJI_3513DI = FilmStock(
    name="Fujifilm 3513DI",
    stock_type="print",
    color_temp=6000.0,
    fog=np.array([0.04, 0.03, 0.05]),
    gamma=np.array([2.60, 2.70, 2.80]),  # Blue gamma higher → cooler rendering
    dmax=np.array([3.00, 3.10, 3.20]),
    speed=np.array([-0.55, -0.50, -0.50]),
    toe_length=np.array([0.22, 0.20, 0.18]),
    shoulder_len=np.array([0.30, 0.28, 0.22]),
    description="Digital intermediate print. Cleaner, cooler than Kodak 2383."
)

# ── Stock Registry ───────────────────────────────────────────────────────────

NEGATIVE_STOCKS: Dict[str, FilmStock] = {
    'kodak_500t': KODAK_5219_500T,
    'kodak_250d': KODAK_5207_250D,
    'kodak_200t': KODAK_5213_200T,
    'cinestill_800t': CINESTILL_800T,
    'kodachrome_64': KODACHROME_64,
    'fuji_velvia_50': FUJI_VELVIA_50,
}

PRINT_STOCKS: Dict[str, FilmStock] = {
    'kodak_2383': KODAK_2383,
    'kodak_2393': KODAK_2393,
    'fuji_3513di': FUJI_3513DI,
}

ALL_STOCKS = {**NEGATIVE_STOCKS, **PRINT_STOCKS}


# ─────────────────────────────────────────────────────────────────────────────
# Film Characteristic Curve Engine
# ─────────────────────────────────────────────────────────────────────────────

def characteristic_curve(linear_rgb: np.ndarray, stock: FilmStock,
                         exposure_offset: float = 0.0) -> np.ndarray:
    """Apply a film stock's characteristic curve to linear RGB data.
    
    Models the H&D (Hurter-Driffield) curve as an asymmetric sigmoid:
    D = Fog + (Dmax - Fog) * asymmetric_sigmoid(Gamma * (logE - Speed))
    
    Args:
        linear_rgb: Input in linear light [0, 1+], shape (..., 3)
        stock: Film stock parameters
        exposure_offset: EV offset (positive = overexpose, negative = underexpose)
    
    Returns:
        Density values normalized to [0, 1] range, shape (..., 3)
    """
    # Clamp to avoid log of zero
    rgb = np.maximum(linear_rgb, 1e-10)
    
    # Apply exposure offset (in EV stops)
    rgb = rgb * (2.0 ** exposure_offset)
    
    # Convert to log exposure (base 10, matching sensitometric convention)
    log_e = np.log10(rgb)
    
    result = np.zeros_like(rgb)
    
    for ch in range(3):
        x = (log_e[..., ch] - stock.speed[ch]) * stock.gamma[ch]
        toe = stock.toe_length[ch]
        sho = stock.shoulder_len[ch]
        
        # Asymmetric smooth curve:
        # Toe region: soft log compression (gentle shadow rendering)
        # Shoulder region: soft saturation (graceful highlight rolloff)
        
        # Use a smooth polynomial S-curve that approximates the H&D shape
        # This is a combination of softplus functions for toe and shoulder
        toe_curve = toe * np.log1p(np.exp(np.clip(x / toe, -20, 20)))
        sho_curve = sho - sho * np.log1p(np.exp(np.clip((sho - x) / sho, -20, 20)))
        
        # Blend between toe and shoulder regions
        blend = 1.0 / (1.0 + np.exp(-2.0 * (x - 1.0)))  # Sigmoid crossover
        curve = toe_curve * (1.0 - blend) + sho_curve * blend
        
        # Normalize to [0, 1] and apply fog/dmax range
        curve_norm = np.clip(curve / max(sho + toe * 0.5, 0.01), 0, 1)
        density = stock.fog[ch] + (stock.dmax[ch] - stock.fog[ch]) * curve_norm
        
        # Normalize density to [0, 1] output range
        result[..., ch] = np.clip(density / stock.dmax[ch], 0, 1)
    
    return result


def apply_color_temp_sensitivity(rgb: np.ndarray, film_temp: float,
                                  scene_temp: float = 5500.0) -> np.ndarray:
    """Apply color temperature mismatch between film balance and scene illuminant.
    
    Tungsten film in daylight → cool/blue shift
    Daylight film in tungsten → warm/orange shift
    """
    if abs(film_temp - scene_temp) < 100:
        return rgb  # Close enough, no adjustment
    
    ratio = film_temp / max(scene_temp, 100.0)
    
    # Simplified von Kries-style chromatic adaptation
    adaptation = np.array([
        ratio ** 0.45,      # Red: moderate sensitivity
        1.0,                 # Green: reference
        (1.0 / ratio) ** 0.45  # Blue: most sensitive
    ])
    
    return rgb * adaptation


def negative_to_print_pipeline(linear_rgb: np.ndarray,
                                negative: FilmStock,
                                print_stock: FilmStock,
                                scene_temp: float = 5500.0,
                                exposure_offset: float = 0.0) -> np.ndarray:
    """Full negative → print film emulation pipeline.
    
    1. Apply color temperature sensitivity of negative
    2. Negative characteristic curve (compression)
    3. Print characteristic curve (expansion)
    4. Normalize to display range
    """
    # Step 1: Color temperature sensitivity
    rgb = apply_color_temp_sensitivity(linear_rgb, negative.color_temp, scene_temp)
    
    # Step 2: Negative stock — compresses scene to narrow density range
    neg_density = characteristic_curve(rgb, negative, exposure_offset)
    
    # Step 3: Print stock — expands density back to viewable contrast
    # The print stock receives the negative's density as its input
    # We treat the normalized density as the "exposure" input to the print curve
    print_result = characteristic_curve(neg_density, print_stock)
    
    return np.clip(print_result, 0, 1)


# ─────────────────────────────────────────────────────────────────────────────
# LUT Generation Engine
# ─────────────────────────────────────────────────────────────────────────────

def generate_identity_lattice(size: int) -> np.ndarray:
    """Generate a 3D identity lattice.
    
    Returns: ndarray of shape (size, size, size, 3) where each point
    contains its own normalized RGB coordinates.
    """
    axis = np.linspace(0, 1, size)
    r, g, b = np.meshgrid(axis, axis, axis, indexing='ij')
    return np.stack([r, g, b], axis=-1)


def srgb_to_linear(srgb: np.ndarray) -> np.ndarray:
    """Convert sRGB gamma-encoded values to linear light."""
    linear = np.where(
        srgb <= 0.04045,
        srgb / 12.92,
        ((srgb + 0.055) / 1.055) ** 2.4
    )
    return linear


def linear_to_srgb(linear: np.ndarray) -> np.ndarray:
    """Convert linear light values to sRGB gamma-encoded."""
    srgb = np.where(
        linear <= 0.0031308,
        linear * 12.92,
        1.055 * np.power(np.maximum(linear, 0), 1.0/2.4) - 0.055
    )
    return np.clip(srgb, 0, 1)


def bake_3d_lut(size: int,
                negative: Optional[FilmStock] = None,
                print_stock: Optional[FilmStock] = None,
                scene_temp: float = 5500.0,
                exposure_offset: float = 0.0) -> np.ndarray:
    """Bake a 3D LUT from film stock parameters.
    
    Input assumed to be sRGB gamma-encoded (display-referred, post-tonemap).
    
    Args:
        size: Lattice dimension (e.g., 33 or 64)
        negative: Negative film stock (or None to skip)
        print_stock: Print film stock (or None to skip)
        scene_temp: Scene illuminant color temperature
        exposure_offset: EV exposure adjustment
    
    Returns:
        ndarray of shape (size, size, size, 3) — the baked LUT
    """
    print(f"  Generating {size}³ lattice ({size**3:,} points)...")
    lattice = generate_identity_lattice(size)
    
    # Flatten for vectorized processing
    flat = lattice.reshape(-1, 3)
    
    # Decode sRGB gamma → linear light (film responds to linear light)
    linear = srgb_to_linear(flat)
    
    if negative is not None and print_stock is not None:
        # Full negative → print pipeline
        print(f"  Applying: {negative.name} → {print_stock.name}")
        result = negative_to_print_pipeline(
            linear, negative, print_stock, scene_temp, exposure_offset
        )
    elif negative is not None:
        # Negative only
        print(f"  Applying negative only: {negative.name}")
        rgb = apply_color_temp_sensitivity(linear, negative.color_temp, scene_temp)
        result = characteristic_curve(rgb, negative, exposure_offset)
    elif print_stock is not None:
        # Print only
        print(f"  Applying print only: {print_stock.name}")
        result = characteristic_curve(linear, print_stock, exposure_offset)
    else:
        result = flat  # Identity
    
    # Encode back to sRGB gamma for display-referred output
    result_srgb = linear_to_srgb(result)
    
    return result_srgb.reshape(size, size, size, 3)


# ─────────────────────────────────────────────────────────────────────────────
# Export: Tiled 2D Atlas (ENB-compatible PNG)
# ─────────────────────────────────────────────────────────────────────────────

def lut_to_tiled_atlas(lut: np.ndarray) -> np.ndarray:
    """Convert a 3D LUT to a tiled 2D atlas.
    
    Layout: Blue slices arranged in a grid.
    For 64³: 8×8 grid of 64×64 tiles = 512×512 pixels.
    For 33³: 6×6 grid of 33×33 tiles = 198×198 pixels.
    
    Args:
        lut: 3D LUT of shape (size, size, size, 3)
    
    Returns:
        2D array of shape (atlas_h, atlas_w, 3)
    """
    size = lut.shape[0]
    
    # Determine grid layout
    tiles_per_row = int(np.ceil(np.sqrt(size)))
    tiles_per_col = int(np.ceil(size / tiles_per_row))
    
    atlas_w = tiles_per_row * size
    atlas_h = tiles_per_col * size
    
    atlas = np.zeros((atlas_h, atlas_w, 3), dtype=np.float64)
    
    for b in range(size):
        tile_x = b % tiles_per_row
        tile_y = b // tiles_per_row
        
        x_start = tile_x * size
        y_start = tile_y * size
        
        # Each tile: rows = green axis, columns = red axis
        # lut[r, g, b, :] → atlas[y_start + g, x_start + r, :]
        atlas[y_start:y_start+size, x_start:x_start+size, :] = lut[:, :, b, :].transpose(1, 0, 2)
    
    return atlas


def lut_to_enb_strip(lut: np.ndarray) -> np.ndarray:
    """Convert a 3D LUT to ENB's traditional 256×16 strip format (16³ only).
    
    For backward compatibility with existing ENB LUT loading.
    """
    size = lut.shape[0]
    assert size == 16, f"ENB strip format requires 16³ LUT, got {size}³"
    
    strip = np.zeros((16, 256, 3), dtype=np.float64)
    
    for b in range(16):
        for g in range(16):
            for r in range(16):
                x = b * 16 + r
                y = g
                strip[y, x, :] = lut[r, g, b, :]
    
    return strip


# ─────────────────────────────────────────────────────────────────────────────
# Export: .cube file (Industry Standard)
# ─────────────────────────────────────────────────────────────────────────────

def export_cube_file(lut: np.ndarray, filepath: str, title: str = "Film Emulation"):
    """Export 3D LUT as .cube file for validation in DaVinci Resolve."""
    size = lut.shape[0]
    
    with open(filepath, 'w') as f:
        f.write(f"# Generated by SkyrimBridge Film LUT Generator\n")
        f.write(f"TITLE \"{title}\"\n")
        f.write(f"LUT_3D_SIZE {size}\n")
        f.write(f"DOMAIN_MIN 0.0 0.0 0.0\n")
        f.write(f"DOMAIN_MAX 1.0 1.0 1.0\n")
        f.write(f"\n")
        
        # .cube format iterates: B outermost, G middle, R innermost
        for b in range(size):
            for g in range(size):
                for r in range(size):
                    rgb = lut[r, g, b, :]
                    f.write(f"{rgb[0]:.6f} {rgb[1]:.6f} {rgb[2]:.6f}\n")
    
    print(f"  Exported .cube: {filepath}")


# ─────────────────────────────────────────────────────────────────────────────
# Batch Generation
# ─────────────────────────────────────────────────────────────────────────────

def generate_film_combination(negative_key: str, print_key: str,
                               size: int = 64, output_dir: str = "output",
                               scene_temp: float = 5500.0) -> str:
    """Generate a single negative × print combination."""
    negative = NEGATIVE_STOCKS[negative_key]
    print_stock = PRINT_STOCKS[print_key]
    
    combo_name = f"{negative_key}_{print_key}"
    safe_name = combo_name.replace(' ', '_')
    
    print(f"\n{'='*60}")
    print(f"Generating: {negative.name} → {print_stock.name}")
    print(f"  Scene temperature: {scene_temp}K")
    print(f"{'='*60}")
    
    # Bake the LUT
    lut = bake_3d_lut(size, negative, print_stock, scene_temp)
    
    # Export as tiled atlas PNG
    atlas = lut_to_tiled_atlas(lut)
    atlas_u8 = (np.clip(atlas, 0, 1) * 255).astype(np.uint8)
    
    png_path = os.path.join(output_dir, f"{safe_name}_{size}.png")
    Image.fromarray(atlas_u8).save(png_path, optimize=True)
    print(f"  Exported atlas PNG: {png_path} ({atlas_u8.shape[1]}×{atlas_u8.shape[0]})")
    
    # Export .cube for validation
    cube_path = os.path.join(output_dir, f"{safe_name}_{size}.cube")
    export_cube_file(lut, cube_path, f"{negative.name} on {print_stock.name}")
    
    # Also generate ENB-compatible 16³ strip version
    if size >= 16:
        # Downsample to 16³
        lut16 = bake_3d_lut(16, negative, print_stock, scene_temp)
        strip = lut_to_enb_strip(lut16)
        strip_u8 = (np.clip(strip, 0, 1) * 255).astype(np.uint8)
        
        strip_path = os.path.join(output_dir, f"{safe_name}_strip16.png")
        Image.fromarray(strip_u8).save(strip_path, optimize=True)
        print(f"  Exported ENB strip: {strip_path} (256×16)")
    
    return png_path


def generate_all(size: int = 64, output_dir: str = "output",
                 scene_temp: float = 5500.0):
    """Generate all negative × print combinations."""
    os.makedirs(output_dir, exist_ok=True)
    
    generated = []
    
    for neg_key in NEGATIVE_STOCKS:
        neg = NEGATIVE_STOCKS[neg_key]
        
        if neg.stock_type == 'reversal':
            # Reversal stocks are direct (no print stage)
            print(f"\n{'='*60}")
            print(f"Generating reversal: {neg.name} (direct, no print)")
            print(f"{'='*60}")
            
            lut = bake_3d_lut(size, negative=neg, print_stock=None,
                             scene_temp=scene_temp)
            
            atlas = lut_to_tiled_atlas(lut)
            atlas_u8 = (np.clip(atlas, 0, 1) * 255).astype(np.uint8)
            
            safe = neg_key.replace(' ', '_')
            png_path = os.path.join(output_dir, f"{safe}_direct_{size}.png")
            Image.fromarray(atlas_u8).save(png_path, optimize=True)
            print(f"  Exported: {png_path}")
            
            cube_path = os.path.join(output_dir, f"{safe}_direct_{size}.cube")
            export_cube_file(lut, cube_path, f"{neg.name} (Direct Reversal)")
            
            # ENB strip
            lut16 = bake_3d_lut(16, negative=neg, print_stock=None,
                               scene_temp=scene_temp)
            strip = lut_to_enb_strip(lut16)
            strip_u8 = (np.clip(strip, 0, 1) * 255).astype(np.uint8)
            strip_path = os.path.join(output_dir, f"{safe}_direct_strip16.png")
            Image.fromarray(strip_u8).save(strip_path, optimize=True)
            
            generated.append(png_path)
        else:
            # Negative stocks get paired with each print stock
            for print_key in PRINT_STOCKS:
                path = generate_film_combination(
                    neg_key, print_key, size, output_dir, scene_temp
                )
                generated.append(path)
    
    # Also generate identity LUT for reference
    print(f"\n{'='*60}")
    print(f"Generating identity reference LUT")
    print(f"{'='*60}")
    
    identity = generate_identity_lattice(size)
    atlas = lut_to_tiled_atlas(identity)
    atlas_u8 = (np.clip(atlas, 0, 1) * 255).astype(np.uint8)
    id_path = os.path.join(output_dir, f"identity_{size}.png")
    Image.fromarray(atlas_u8).save(id_path, optimize=True)
    print(f"  Exported identity: {id_path}")
    
    return generated


# ─────────────────────────────────────────────────────────────────────────────
# CLI Entry Point
# ─────────────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Film Stock LUT Generator for ENB / SkyrimBridge",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                              Generate all combinations
  %(prog)s --stock kodak_500t           Just one negative (with all prints)
  %(prog)s --stock kodak_500t --print kodak_2383   Specific combo
  %(prog)s --size 33                    Lower resolution (faster)
  %(prog)s --list                       List available stocks
        """
    )
    
    parser.add_argument('--size', type=int, default=64, choices=[16, 33, 64],
                       help='LUT lattice size (default: 64)')
    parser.add_argument('--stock', type=str, default=None,
                       help='Specific negative stock key')
    parser.add_argument('--print', type=str, default=None, dest='print_stock',
                       help='Specific print stock key')
    parser.add_argument('--scene-temp', type=float, default=5500.0,
                       help='Scene color temperature in Kelvin (default: 5500)')
    parser.add_argument('--output', type=str, default='output',
                       help='Output directory (default: output)')
    parser.add_argument('--negative-only', action='store_true',
                       help='Generate negative-only LUTs (no print stage)')
    parser.add_argument('--list', action='store_true',
                       help='List available film stocks')
    
    args = parser.parse_args()
    
    if args.list:
        print("\n  NEGATIVE STOCKS:")
        for key, stock in NEGATIVE_STOCKS.items():
            print(f"    {key:20s}  {stock.name:35s}  ({stock.stock_type}, {stock.color_temp:.0f}K)")
            print(f"    {'':20s}  {stock.description}")
        print("\n  PRINT STOCKS:")
        for key, stock in PRINT_STOCKS.items():
            print(f"    {key:20s}  {stock.name:35s}  ({stock.stock_type}, {stock.color_temp:.0f}K)")
            print(f"    {'':20s}  {stock.description}")
        return
    
    os.makedirs(args.output, exist_ok=True)
    
    print(f"\n  Film Stock LUT Generator for ENB")
    print(f"  ================================")
    print(f"  Resolution:  {args.size}³ ({args.size**3:,} lattice points)")
    print(f"  Scene temp:  {args.scene_temp:.0f}K")
    print(f"  Output dir:  {args.output}/")
    
    if args.stock and args.print_stock:
        # Single combination
        if args.stock not in ALL_STOCKS:
            print(f"  ERROR: Unknown stock '{args.stock}'. Use --list to see options.")
            sys.exit(1)
        if args.print_stock not in PRINT_STOCKS:
            print(f"  ERROR: Unknown print stock '{args.print_stock}'. Use --list to see options.")
            sys.exit(1)
        
        generate_film_combination(
            args.stock, args.print_stock, args.size, args.output, args.scene_temp
        )
    elif args.stock:
        # One negative with all prints
        if args.stock not in NEGATIVE_STOCKS:
            print(f"  ERROR: Unknown negative stock '{args.stock}'. Use --list to see options.")
            sys.exit(1)
        
        if args.negative_only:
            neg = NEGATIVE_STOCKS[args.stock]
            lut = bake_3d_lut(args.size, negative=neg, scene_temp=args.scene_temp)
            atlas = lut_to_tiled_atlas(lut)
            atlas_u8 = (np.clip(atlas, 0, 1) * 255).astype(np.uint8)
            path = os.path.join(args.output, f"{args.stock}_neg_only_{args.size}.png")
            Image.fromarray(atlas_u8).save(path, optimize=True)
            print(f"\n  Generated: {path}")
        else:
            for print_key in PRINT_STOCKS:
                generate_film_combination(
                    args.stock, print_key, args.size, args.output, args.scene_temp
                )
    else:
        # Generate everything
        generated = generate_all(args.size, args.output, args.scene_temp)
        
        print(f"\n{'='*60}")
        print(f"  COMPLETE: Generated {len(generated)} LUT files")
        print(f"  Output directory: {args.output}/")
        print(f"{'='*60}")


if __name__ == '__main__':
    main()
