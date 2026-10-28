"""Packs material textures (ambientCG-style maps) into KTX1 textures with full mip chains and a JSON descriptor.

Supported modes:
    monochrome (default):
        Packs foam textures for the OceanFft simulation:
            <name>_Surface.ktx (RGBA8): R opacity mask (histogram-equalized), G height, B roughness, A albedo luminance.
            <name>_Normal.ktx  (RG8):   tangent-space normal x and y (renormalized mip chain).
            <name>_Material.json:       albedo tint and mean luminance.
    ord:
        Packs PBR textures for seabed / surface rendering:
            <name>_Albedo.ktx  (RGBA8): linear RGB color in RGB, 1.0 in A.
            <name>_Ord.ktx     (RGBA8): R ambient occlusion, G roughness, B displacement, A 1.0.
            <name>_Normal.ktx  (RG8):   tangent-space normal x and y (renormalized mip chain).
            <name>_Material.json:       material name, mode ("ord"), size, mean_albedo, mean_roughness.

Usage:
    python texture_pack.py <folder> <name> <output> <size>
    python texture_pack.py --mode monochrome <folder> <name> <output> <size>
    python texture_pack.py --mode ord <folder> <name> <output> <size>
"""

import argparse
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
            z = np.sqrt(np.clip(1.0 - (level ** 2).sum(axis=2, keepdims=True), 0.0, 1.0))
            n = np.concatenate([level, z], axis=2)
            norm = np.linalg.norm(n, axis=2, keepdims=True)
            norm = np.where(norm < 1e-6, 1.0, norm)
            n /= norm
            level = n[..., :2]
        levels.append(level)
    return levels


def write_ktx1(path: Path, levels: list[np.ndarray], gl_format: int, gl_internal_format: int) -> None:
    channels = levels[0].shape[2]
    size = levels[0].shape[0]
    header = KTX1_IDENTIFIER + struct.pack(
        "<13I",
        0x04030201,
        GL_UNSIGNED_BYTE,
        1,
        gl_format,
        gl_internal_format,
        gl_format,
        size,
        size,
        0,
        0,
        1,
        len(levels),
        0,
    )
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


def detect_prefix(folder: Path, name: str) -> str:
    """Auto-detect whether the file prefix uses <name>_4K-PNG_ or <name>_4K_."""
    if any(folder.glob(f"{name}_4K-PNG_*")):
        return f"{name}_4K-PNG_"
    if any(folder.glob(f"{name}_4K_*")):
        return f"{name}_4K_"
    return f"{name}_4K_"


def find_texture_file(folder: Path, prefix: str, map_name: str) -> Path:
    for ext in [".png", ".jpg", ".jpeg", ".PNG", ".JPG", ".JPEG"]:
        candidate = folder / f"{prefix}{map_name}{ext}"
        if candidate.exists():
            return candidate
    raise FileNotFoundError(f"Texture file for map '{map_name}' not found with prefix '{prefix}' in '{folder}'")


def pack_monochrome(folder: Path, name: str, output: Path, size: int) -> None:
    output.mkdir(parents=True, exist_ok=True)
    prefix = detect_prefix(folder, name)

    color_file = find_texture_file(folder, prefix, "Color")
    opacity_file = find_texture_file(folder, prefix, "Opacity")
    disp_file = find_texture_file(folder, prefix, "Displacement")
    roughness_file = find_texture_file(folder, prefix, "Roughness")
    normal_file = find_texture_file(folder, prefix, "NormalGL")

    color = srgb_to_linear(load(color_file, "RGB", size))
    luminance = color @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    tint = color.reshape(-1, 3).mean(axis=0) / max(float(luminance.mean()), 1e-6)

    mask = equalize(load(opacity_file, "L", size))
    height = load(disp_file, "L", size)
    roughness = load(roughness_file, "L", size)
    surface = np.stack([mask, height, roughness, luminance], axis=2)

    normal = load(normal_file, "RGB", size)[..., :2] * 2.0 - 1.0

    write_ktx1(output / f"{name}_Surface.ktx", mip_chain(surface, False), GL_RGBA, GL_RGBA8)
    normal_levels = [(level * 0.5 + 0.5) for level in mip_chain(normal, True)]
    write_ktx1(output / f"{name}_Normal.ktx", normal_levels, GL_RG, GL_RG8)

    material = {
        "name": name,
        "size": size,
        "albedo_tint": [round(float(t), 5) for t in tint],
        "mean_luminance": round(float(luminance.mean()), 5),
    }
    (output / f"{name}_Material.json").write_text(json.dumps(material, indent=2) + "\n")

    print(
        f"{name} (monochrome): {size}x{size}, mean linear luminance {luminance.mean():.4f}, "
        f"albedo tint {tint[0]:.4f} {tint[1]:.4f} {tint[2]:.4f}"
    )


def pack_ord(folder: Path, name: str, output: Path, size: int) -> None:
    output.mkdir(parents=True, exist_ok=True)
    prefix = detect_prefix(folder, name)

    color_file = find_texture_file(folder, prefix, "Color")
    ao_file = find_texture_file(folder, prefix, "AmbientOcclusion")
    roughness_file = find_texture_file(folder, prefix, "Roughness")
    disp_file = find_texture_file(folder, prefix, "Displacement")
    normal_file = find_texture_file(folder, prefix, "NormalGL")

    # Albedo: linear RGB color in RGB, 1.0 in A
    color = srgb_to_linear(load(color_file, "RGB", size))
    alpha = np.ones((size, size, 1), dtype=np.float32)
    albedo = np.concatenate([color, alpha], axis=2)
    write_ktx1(output / f"{name}_Albedo.ktx", mip_chain(albedo, False), GL_RGBA, GL_RGBA8)

    # ORD: R=AmbientOcclusion, G=Roughness, B=Displacement, A=1.0
    ao = load(ao_file, "L", size)
    roughness = load(roughness_file, "L", size)
    displacement = load(disp_file, "L", size)
    one = np.ones((size, size), dtype=np.float32)
    ord_tex = np.stack([ao, roughness, displacement, one], axis=2)
    write_ktx1(output / f"{name}_Ord.ktx", mip_chain(ord_tex, False), GL_RGBA, GL_RGBA8)

    # Normal: renormalized tangent-space normal XY in [0, 1]
    normal = load(normal_file, "RGB", size)[..., :2] * 2.0 - 1.0
    normal_levels = [(level * 0.5 + 0.5) for level in mip_chain(normal, True)]
    write_ktx1(output / f"{name}_Normal.ktx", normal_levels, GL_RG, GL_RG8)

    # Material JSON: name, mode ("ord"), size, mean_albedo, mean_roughness
    mean_albedo = [round(float(c), 5) for c in color.reshape(-1, 3).mean(axis=0)]
    mean_roughness = round(float(roughness.mean()), 5)
    material = {
        "name": name,
        "mode": "ord",
        "size": size,
        "mean_albedo": mean_albedo,
        "mean_roughness": mean_roughness,
    }
    (output / f"{name}_Material.json").write_text(json.dumps(material, indent=2) + "\n")

    print(
        f"{name} (ord): {size}x{size}, "
        f"mean linear albedo {mean_albedo[0]:.4f} {mean_albedo[1]:.4f} {mean_albedo[2]:.4f}, "
        f"mean roughness {mean_roughness:.4f}"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Pack material textures into KTX1 textures with mip chains")
    parser.add_argument(
        "--mode",
        choices=["monochrome", "ord"],
        default="monochrome",
        help="Texture pack mode: monochrome (default) or ord",
    )
    parser.add_argument("folder", type=Path, help="Folder containing source textures")
    parser.add_argument("name", type=str, help="Material name (e.g. Foam002, Ground095A)")
    parser.add_argument("output", type=Path, help="Output folder")
    parser.add_argument("size", type=int, help="Output texture resolution (e.g. 1024, 2048)")

    args = parser.parse_args()

    if args.mode == "ord":
        pack_ord(args.folder, args.name, args.output, args.size)
    else:
        pack_monochrome(args.folder, args.name, args.output, args.size)


if __name__ == "__main__":
    main()
