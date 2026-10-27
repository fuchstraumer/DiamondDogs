"""Packs a foam material (ambientCG-style maps: Color, Opacity, Displacement, Roughness, NormalGL) into two KTX1
textures with full mip chains, and a <name>_Material.json with its albedo tint, for the OceanFft foam.

    surface (RGBA8): R the opacity mask, histogram-equalized, so that the shader's area-preserving dissolve holds
                     exactly (P(mask > 1 - a) = a). G height. B roughness. A albedo luminance (linear).
    normal  (RG8):   tangent-space normal x and y (OpenGL convention, +y up); the shader rebuilds z.

Foam albedo is near grey (Foam002: mean saturation 0.055), so one luminance channel plus a constant tint carries it.
The script prints that tint (the mean linear color over the mean luminance).

Prototype only: the content compiler will take this over. Mips are box-filtered here, in linear values for every
channel; the normal is renormalized at each level.

Usage (Python with Pillow and numpy):
    python foam_texture_pack.py <material folder> <material name> <output folder> <size>
    python foam_texture_pack.py assets/textures/Foam002 Foam002 build/foam 1024
"""

import json
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None

GL_UNSIGNED_BYTE = 0x1401
GL_RG = 0x8227
GL_RGBA = 0x1908
GL_RG8 = 0x822B
GL_RGBA8 = 0x8058
KTX1_IDENTIFIER = bytes([0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A])


def load(path: Path, mode: str, size: int) -> np.ndarray:
    image = Image.open(path).convert(mode)
    if image.width != size or image.height != size:
        image = image.resize((size, size), Image.Resampling.LANCZOS)
    return np.asarray(image, dtype=np.float32) / 255.0


def srgb_to_linear(c: np.ndarray) -> np.ndarray:
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def equalize(values: np.ndarray) -> np.ndarray:
    """Maps each value to its mid-rank in [0, 1], so the result is uniform. Ties share their average rank."""
    flat = values.ravel()
    order = np.argsort(flat, kind="stable")
    sorted_values = flat[order]
    ranks = np.empty(flat.size, dtype=np.float64)
    ranks[order] = np.arange(flat.size)
    # ties (the mask's many exact zeros) get the mean rank of their run
    unique, first, counts = np.unique(sorted_values, return_index=True, return_counts=True)
    mean_rank = first + 0.5 * (counts - 1)
    ranks = mean_rank[np.searchsorted(unique, flat)]
    return ((ranks + 0.5) / flat.size).reshape(values.shape).astype(np.float32)


def downsample(level: np.ndarray) -> np.ndarray:
    h, w = level.shape[:2]
    return level.reshape(h // 2, 2, w // 2, 2, -1).mean(axis=(1, 3))


def mip_chain(level0: np.ndarray, renormalize_xy: bool) -> list[np.ndarray]:
    levels = [level0]
    while levels[-1].shape[0] > 1:
        level = downsample(levels[-1])
        if renormalize_xy:
            # the averaged xy is shorter than a unit normal's: rebuild z, normalize, keep xy
            z = np.sqrt(np.clip(1.0 - (level ** 2).sum(axis=2, keepdims=True), 0.0, 1.0))
            n = np.concatenate([level, z], axis=2)
            n /= np.linalg.norm(n, axis=2, keepdims=True)
            level = n[..., :2]
        levels.append(level)
    return levels


def write_ktx1(path: Path, levels: list[np.ndarray], gl_format: int, gl_internal_format: int) -> None:
    channels = levels[0].shape[2]
    size = levels[0].shape[0]
    header = KTX1_IDENTIFIER + struct.pack("<13I", 0x04030201, GL_UNSIGNED_BYTE, 1, gl_format, gl_internal_format,
                                           gl_format, size, size, 0, 0, 1, len(levels), 0)
    with open(path, "wb") as out:
        out.write(header)
        for level in levels:
            texels = np.clip(np.round(level * 255.0), 0, 255).astype(np.uint8)
            row_bytes = texels.shape[1] * channels
            padded_row = (row_bytes + 3) & ~3
            rows = texels.reshape(texels.shape[0], row_bytes)
            if padded_row != row_bytes:
                rows = np.pad(rows, ((0, 0), (0, padded_row - row_bytes)))
            data = rows.tobytes()
            out.write(struct.pack("<I", len(data)))
            out.write(data)
            # mipPadding: each level's data ends on a 4-byte boundary (already true after the row padding)


def main() -> None:
    folder, name, output, size = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3]), int(sys.argv[4])
    output.mkdir(parents=True, exist_ok=True)
    prefix = folder / f"{name}_4K_"

    color = srgb_to_linear(load(Path(f"{prefix}Color.jpg"), "RGB", size))
    luminance = color @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    tint = color.reshape(-1, 3).mean(axis=0) / max(float(luminance.mean()), 1e-6)

    mask = equalize(load(Path(f"{prefix}Opacity.jpg"), "L", size))
    height = load(Path(f"{prefix}Displacement.jpg"), "L", size)
    roughness = load(Path(f"{prefix}Roughness.jpg"), "L", size)
    surface = np.stack([mask, height, roughness, luminance], axis=2)

    normal = load(Path(f"{prefix}NormalGL.jpg"), "RGB", size)[..., :2] * 2.0 - 1.0

    write_ktx1(output / f"{name}_Surface.ktx", mip_chain(surface, False), GL_RGBA, GL_RGBA8)
    # stored as unorm: the shader maps xy back with * 2 - 1
    normal_levels = [(level * 0.5 + 0.5) for level in mip_chain(normal, True)]
    write_ktx1(output / f"{name}_Normal.ktx", normal_levels, GL_RG, GL_RG8)

    # the host reads the tint from here, so the shader multiplies the luminance back to the material's color
    material = {"name": name, "size": size, "albedo_tint": [round(float(t), 5) for t in tint],
                "mean_luminance": round(float(luminance.mean()), 5)}
    (output / f"{name}_Material.json").write_text(json.dumps(material, indent=2) + "\n")

    print(f"{name}: {size}x{size}, mean linear luminance {luminance.mean():.4f}, "
          f"albedo tint {tint[0]:.4f} {tint[1]:.4f} {tint[2]:.4f}")


if __name__ == "__main__":
    main()
