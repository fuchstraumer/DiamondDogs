"""Quantitative spherical diagnostics for PlanetGen elevation, plume, and plate captures."""

from __future__ import annotations

import argparse
from collections import deque
import glob
import json
import math
import os
import sys
from typing import Any

import numpy as np
from PIL import Image


ANALYSIS_WIDTH = 512
ANALYSIS_HEIGHT = 256


def _cos_lat_weights(height: int, width: int) -> np.ndarray:
    """Returns normalized per-pixel spherical area weights on an equirectangular grid."""
    y = np.arange(height, dtype=np.float64) + 0.5
    lat = (0.5 - y / float(height)) * math.pi
    row_w = np.maximum(np.cos(lat), 0.0)
    grid_w = np.repeat(row_w[:, None], width, axis=1)
    grid_w /= grid_w.sum()
    return grid_w


def _load_rgb(path: str, width: int = ANALYSIS_WIDTH, height: int = ANALYSIS_HEIGHT) -> np.ndarray:
    with Image.open(path) as im:
        rgb = im.convert("RGB")
        if rgb.size != (width, height):
            rgb = rgb.resize((width, height), Image.Resampling.BILINEAR)
        return np.asarray(rgb, dtype=np.int16)


def _classify_elevation(rgb: np.ndarray) -> dict[str, np.ndarray]:
    """Segments equirectangular elevation RGB into land, ocean, shallow/atoll reef, highland, and snow."""
    r = rgb[..., 0]
    g = rgb[..., 1]
    b = rgb[..., 2]

    max_rg = np.maximum(r, g)
    snow = (r > 175) & (g > 175) & (b > 175) & (np.abs(r - b) < 40)
    highland_brown = (r > b + 14) & (r >= g - 4) & (max_rg > 50) & (~snow)
    green_land = (g > b + 6) & (g >= r - 14) & (max_rg > 38) & (~snow)
    land = green_land | highland_brown | snow

    ocean = ~land
    shallow_reef = ocean & (b > r + 28) & (g > r + 22) & (g > 85) & (b > 105)
    deep_ocean = ocean & (~shallow_reef)

    return {
        "land": land,
        "ocean": ocean,
        "shallow_reef": shallow_reef,
        "deep_ocean": deep_ocean,
        "highland": highland_brown,
        "snow": snow,
    }


def _connected_components_wrapped(mask: np.ndarray, weights: np.ndarray, min_pixels: int = 1) -> list[dict[str, Any]]:
    """4-connected components with east-west longitude wrap and spherical centroid/elongation."""
    h, w = mask.shape
    visited = np.zeros((h, w), dtype=bool)
    ys, xs = np.nonzero(mask)
    components: list[dict[str, Any]] = []

    lat_arr = (0.5 - (np.arange(h, dtype=np.float64) + 0.5) / float(h)) * math.pi
    lon_arr = ((np.arange(w, dtype=np.float64) + 0.5) / float(w) - 0.5) * (2.0 * math.pi)
    cos_lat = np.cos(lat_arr)
    sin_lat = np.sin(lat_arr)
    cos_lon = np.cos(lon_arr)
    sin_lon = np.sin(lon_arr)

    for start_y, start_x in zip(ys.tolist(), xs.tolist()):
        if visited[start_y, start_x]:
            continue

        queue: deque[tuple[int, int]] = deque([(start_y, start_x)])
        visited[start_y, start_x] = True
        pix_y: list[int] = []
        pix_x: list[int] = []
        perimeter = 0

        while queue:
            cy, cx = queue.popleft()
            pix_y.append(cy)
            pix_x.append(cx)

            # 4-neighbors with longitude wrap
            nx_l = (cx - 1) % w
            nx_r = (cx + 1) % w

            if mask[cy, nx_l]:
                if not visited[cy, nx_l]:
                    visited[cy, nx_l] = True
                    queue.append((cy, nx_l))
            else:
                perimeter += 1

            if mask[cy, nx_r]:
                if not visited[cy, nx_r]:
                    visited[cy, nx_r] = True
                    queue.append((cy, nx_r))
            else:
                perimeter += 1

            if cy > 0 and mask[cy - 1, cx]:
                if not visited[cy - 1, cx]:
                    visited[cy - 1, cx] = True
                    queue.append((cy - 1, cx))
            else:
                perimeter += 1

            if cy + 1 < h and mask[cy + 1, cx]:
                if not visited[cy + 1, cx]:
                    visited[cy + 1, cx] = True
                    queue.append((cy + 1, cx))
            else:
                perimeter += 1

        px_count = len(pix_y)
        if px_count < min_pixels:
            continue

        py_np = np.asarray(pix_y, dtype=np.int32)
        px_np = np.asarray(pix_x, dtype=np.int32)
        pw = weights[py_np, px_np]
        area_frac = float(pw.sum())

        elongation = 1.0
        compactness = 1.0
        touches_pole = bool(np.any(py_np <= 1) or np.any(py_np >= h - 2))

        if px_count >= 12:
            clat = cos_lat[py_np]
            slat = sin_lat[py_np]
            clon = cos_lon[px_np]
            slon = sin_lon[px_np]
            pts = np.stack([clat * clon, clat * slon, slat], axis=1)
            centroid = (pts * pw[:, None]).sum(axis=0)
            c_norm = float(np.linalg.norm(centroid))
            if c_norm > 1.0e-8:
                centroid /= c_norm
                up = np.array([0.0, 0.0, 1.0]) if abs(centroid[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
                east = np.cross(up, centroid)
                east /= max(float(np.linalg.norm(east)), 1.0e-8)
                north = np.cross(centroid, east)

                # Project onto tangent plane of unit sphere
                rel = pts - centroid[None, :]
                u = rel @ east
                v = rel @ north
                w_sum = max(area_frac, 1.0e-12)
                wn = pw / w_sum
                mu_u = float((wn * u).sum())
                mu_v = float((wn * v).sum())
                du = u - mu_u
                dv = v - mu_v
                cov = np.array([
                    [float((wn * du * du).sum()), float((wn * du * dv).sum())],
                    [float((wn * du * dv).sum()), float((wn * dv * dv).sum())],
                ])
                evals = np.linalg.eigvalsh(cov)
                l_min = max(float(evals[0]), 1.0e-10)
                l_max = max(float(evals[1]), l_min)
                elongation = math.sqrt(l_max / l_min)

            if perimeter > 0:
                compactness = (4.0 * math.pi * float(px_count)) / float(perimeter * perimeter)

        components.append({
            "pixels": px_count,
            "area_pct": area_frac * 100.0,
            "perimeter": perimeter,
            "elongation": elongation,
            "compactness": compactness,
            "touches_pole": touches_pole,
        })

    components.sort(key=lambda c: c["area_pct"], reverse=True)
    return components


def _coastline_fractal_dimension(land: np.ndarray) -> float:
    """Computes box-counting fractal dimension D of land coastlines excluding |lat| > 75 deg."""
    h, w = land.shape
    padded_y = np.pad(land, ((1, 1), (0, 0)), mode="edge")
    left = np.roll(land, 1, axis=1)
    right = np.roll(land, -1, axis=1)
    up = padded_y[:-2, :]
    down = padded_y[2:, :]
    boundary = land & (~(left & right & up & down))

    lat = (0.5 - (np.arange(h, dtype=np.float64) + 0.5) / float(h)) * 180.0
    band = np.abs(lat) <= 75.0
    sub = boundary[band, :]
    sh, sw = sub.shape
    if int(np.count_nonzero(sub)) < 32:
        return 1.0

    scales = [2, 4, 8, 16, 32]
    log_inv_s: list[float] = []
    log_n: list[float] = []

    for s in scales:
        gh = sh // s
        gw = sw // s
        if gh < 2 or gw < 2:
            continue
        cropped = sub[: gh * s, : gw * s]
        boxes = cropped.reshape(gh, s, gw, s).any(axis=(1, 3))
        count = int(np.count_nonzero(boxes))
        if count > 0:
            log_inv_s.append(math.log(1.0 / float(s)))
            log_n.append(math.log(float(count)))

    if len(log_inv_s) < 2:
        return 1.0

    x = np.asarray(log_inv_s, dtype=np.float64)
    y = np.asarray(log_n, dtype=np.float64)
    slope = float(np.polyfit(x, y, 1)[0])
    return slope


def _analyze_plume_layer(plume_rgb: np.ndarray, elev_land: np.ndarray, weights: np.ndarray) -> dict[str, Any]:
    """Quantifies volcanic track beadiness, age stages, and oceanic continental leak in mode=6."""
    r = plume_rgb[..., 0]
    g = plume_rgb[..., 1]
    b = plume_rgb[..., 2]

    # Mode=6 palette:
    # - ocean background: ~(8, 13, 28)
    # - continent background: ~(38, 41, 46)
    # - cyan conduit/swell marker rings: b > r + 40 and g > r + 30
    # - active yellow core: (255, 245, 140)
    # - warm chain orange/red: (242, 82, 31)
    # - old purple seamount: (92, 31, 138)
    cyan_ring = (b > r + 40) & (g > r + 25)
    swell_tint = (b > r + 25) & (g > r + 15) & (r < 55) & (~cyan_ring)
    warm_track = (r > b + 20) & (r > 75) & (~cyan_ring)
    purple_track = (r > g + 18) & (b > g + 22) & (b > 55) & (~cyan_ring)
    track = warm_track | purple_track

    active_core = track & (r > 195) & (g > 165)
    mid_chain = track & (~active_core) & (r >= b)
    old_seamount = purple_track & (~active_core) & (~mid_chain)

    # Mask out small cyan ring holes that cut through active heads
    track_filled = track | (active_core)
    track_comps = _connected_components_wrapped(track_filled, weights, min_pixels=3)
    ribbon_tracks = [c for c in track_comps if c["elongation"] > 4.5]
    max_plume_elong = max((c["elongation"] for c in track_comps if c["pixels"] >= 12), default=1.0)
    mean_plume_elong = float(np.mean([c["elongation"] for c in track_comps if c["pixels"] >= 12])) if any(c["pixels"] >= 12 for c in track_comps) else 1.0

    # Continental promotion leak check: plume tracks coinciding with land pixels in mode=0
    track_area_pct = float(weights[track].sum() * 100.0)
    track_on_land_pct = float(weights[track & elev_land].sum() * 100.0)
    track_ocean_pct = float(weights[track & (~elev_land)].sum() * 100.0)
    beads_per_pct = float(len(track_comps)) / max(track_area_pct, 0.05)

    return {
        "volcanic_area_pct": round(track_area_pct, 3),
        "volcanic_on_land_pct": round(track_on_land_pct, 3),
        "volcanic_in_ocean_pct": round(track_ocean_pct, 3),
        "active_core_pct": round(float(weights[active_core].sum() * 100.0), 3),
        "mid_chain_pct": round(float(weights[mid_chain].sum() * 100.0), 3),
        "old_seamount_pct": round(float(weights[old_seamount].sum() * 100.0), 3),
        "swell_area_pct": round(float(weights[swell_tint].sum() * 100.0), 2),
        "volcanic_components": len(track_comps),
        "ribbon_track_count": len(ribbon_tracks),
        "max_plume_elongation": round(max_plume_elong, 2),
        "mean_plume_elongation": round(mean_plume_elong, 2),
        "beads_per_pct_area": round(beads_per_pct, 2),
    }


def analyze_frame_metrics(elev_path: str, plume_path: str | None = None, plates_path: str | None = None, label: str | None = None) -> dict[str, Any]:
    weights = _cos_lat_weights(ANALYSIS_HEIGHT, ANALYSIS_WIDTH)
    elev_rgb = _load_rgb(elev_path)
    cls = _classify_elevation(elev_rgb)

    land_comps = _connected_components_wrapped(cls["land"], weights, min_pixels=3)
    water_comps = _connected_components_wrapped(cls["ocean"], weights, min_pixels=3)

    continents = [c for c in land_comps if c["area_pct"] >= 1.0]
    med_islands = [c for c in land_comps if 0.05 <= c["area_pct"] < 1.0]
    small_islands = [c for c in land_comps if c["area_pct"] < 0.05]
    ribbon_artifacts = [
        c for c in land_comps
        if (c["pixels"] >= 12) and (c["elongation"] > 6.0) and (c["area_pct"] < 2.0) and (c["compactness"] < 0.25)
    ]

    largest_pct = land_comps[0]["area_pct"] if land_comps else 0.0
    top5_pct = sum(c["area_pct"] for c in land_comps[:5])
    max_island_elong = max((c["elongation"] for c in land_comps if c["pixels"] >= 12 and not c["touches_pole"]), default=1.0)

    # Largest water component is the global ocean; remaining water components >= 3 px are enclosed lagoons/seas
    enclosed_lagoons = water_comps[1:] if len(water_comps) > 1 else []
    lagoon_area_pct = sum(c["area_pct"] for c in enclosed_lagoons)

    coast_d = _coastline_fractal_dimension(cls["land"])

    metrics: dict[str, Any] = {
        "label": label or os.path.basename(elev_path),
        "land_pct": round(float(weights[cls["land"]].sum() * 100.0), 2),
        "ocean_pct": round(float(weights[cls["ocean"]].sum() * 100.0), 2),
        "deep_ocean_pct": round(float(weights[cls["deep_ocean"]].sum() * 100.0), 2),
        "shallow_reef_pct": round(float(weights[cls["shallow_reef"]].sum() * 100.0), 2),
        "highland_pct": round(float(weights[cls["highland"]].sum() * 100.0), 2),
        "snow_pct": round(float(weights[cls["snow"]].sum() * 100.0), 2),
        "coast_D": round(coast_d, 3),
        "landmasses_total": len(land_comps),
        "continents": len(continents),
        "med_islands": len(med_islands),
        "small_islands": len(small_islands),
        "largest_pct": round(largest_pct, 2),
        "top5_pct": round(top5_pct, 2),
        "max_island_elong": round(max_island_elong, 2),
        "ribbon_artifacts": len(ribbon_artifacts),
        "enclosed_lagoons": len(enclosed_lagoons),
        "lagoon_area_pct": round(lagoon_area_pct, 2),
    }

    if plume_path and os.path.exists(plume_path):
        plume_rgb = _load_rgb(plume_path)
        metrics["plumes"] = _analyze_plume_layer(plume_rgb, cls["land"], weights)

    if plates_path and os.path.exists(plates_path):
        plates_rgb = _load_rgb(plates_path)
        r = plates_rgb[..., 0]
        g = plates_rgb[..., 1]
        b = plates_rgb[..., 2]
        overlap = (r > 195) & (g > 195) & (b > 195)
        metrics["plate_overlap_pct"] = round(float(weights[overlap].sum() * 100.0), 3)

    # Compact status scorecard
    flags: list[str] = []
    if metrics["ribbon_artifacts"] > 0:
        flags.append(f"WARN:ribbon_landmasses={metrics['ribbon_artifacts']}")
    if metrics["coast_D"] < 1.12:
        flags.append(f"WARN:smooth_coast_D={metrics['coast_D']}")
    if "plumes" in metrics:
        p = metrics["plumes"]
        if p["ribbon_track_count"] > 0:
            flags.append(f"WARN:ribbon_plumes={p['ribbon_track_count']}")
        if p["volcanic_on_land_pct"] > 0.35 and p["volcanic_on_land_pct"] > p["volcanic_in_ocean_pct"] * 0.8:
            flags.append(f"WARN:plumes_promoted_to_land={p['volcanic_on_land_pct']}%")
    metrics["verdict"] = "PASS" if not flags else "WARN"
    metrics["flags"] = flags
    return metrics


def _print_frame_report(m: dict[str, Any]) -> None:
    print(f"=== {m['label']} [{m['verdict']}] ===")
    print(
        f"  areas (%) : land={m['land_pct']:5.2f}  ocean={m['ocean_pct']:5.2f} "
        f"(deep={m['deep_ocean_pct']:5.2f}, reef/shelf={m['shallow_reef_pct']:5.2f}, "
        f"highland={m['highland_pct']:5.2f}, snow={m['snow_pct']:5.2f})"
    )
    print(
        f"  shape     : coast_D={m['coast_D']:.3f}  landmasses={m['landmasses_total']} "
        f"(cont={m['continents']}, med={m['med_islands']}, small={m['small_islands']}) "
        f"largest={m['largest_pct']:.2f}%  top5={m['top5_pct']:.2f}%"
    )
    print(
        f"  atoll/el  : lagoons={m['enclosed_lagoons']} (area={m['lagoon_area_pct']:.2f}%)  "
        f"max_elong={m['max_island_elong']:.2f}  ribbons={m['ribbon_artifacts']}"
    )
    if "plate_overlap_pct" in m:
        print(f"  plates    : overlap_band={m['plate_overlap_pct']:.3f}%")
    if "plumes" in m:
        p = m["plumes"]
        print(
            f"  plumes    : area={p['volcanic_area_pct']:.3f}% (ocean={p['volcanic_in_ocean_pct']:.3f}%, "
            f"land_overlap={p['volcanic_on_land_pct']:.3f}%)  comps={p['volcanic_components']} "
            f"beads/pct={p['beads_per_pct_area']:.1f}"
        )
        print(
            f"              ages(core/mid/old)={p['active_core_pct']:.2f}/{p['mid_chain_pct']:.2f}/{p['old_seamount_pct']:.2f}% "
            f"max_elong={p['max_plume_elongation']:.2f}  mean_elong={p['mean_plume_elongation']:.2f}  "
            f"ribbons={p['ribbon_track_count']}"
        )
    if m["flags"]:
        print("  flags     : " + " | ".join(m["flags"]))


def cmd_analyze_frame(args: argparse.Namespace) -> int:
    metrics = analyze_frame_metrics(args.elevation, args.plumes, args.plates, args.label)
    if args.json:
        print(json.dumps(metrics, indent=2))
    else:
        _print_frame_report(metrics)
    return 0


def cmd_compare_targets(args: argparse.Namespace) -> int:
    cand = analyze_frame_metrics(args.candidate, args.plumes, args.plates, f"CANDIDATE:{os.path.basename(args.candidate)}")
    refs = [analyze_frame_metrics(p, None, None, f"REF:{os.path.basename(p)}") for p in args.refs]
    _print_frame_report(cand)
    for ref in refs:
        _print_frame_report(ref)

    avg_land = float(np.mean([r["land_pct"] for r in refs]))
    avg_d = float(np.mean([r["coast_D"] for r in refs]))
    avg_lagoons = float(np.mean([r["enclosed_lagoons"] for r in refs]))
    avg_islands = float(np.mean([r["landmasses_total"] for r in refs]))
    print("=== TARGET DELTA (candidate - ref mean) ===")
    print(
        f"  dLand={cand['land_pct'] - avg_land:+.2f}% (ref {avg_land:.2f}%)  "
        f"dCoast_D={cand['coast_D'] - avg_d:+.3f} (ref {avg_d:.3f})  "
        f"dIslands={cand['landmasses_total'] - avg_islands:+.1f} (ref {avg_islands:.1f})  "
        f"dLagoons={cand['enclosed_lagoons'] - avg_lagoons:+.1f} (ref {avg_lagoons:.1f})"
    )
    return 0


def cmd_analyze_sequence(args: argparse.Namespace) -> int:
    files = sorted(glob.glob(os.path.join(args.frames_dir, args.pattern)))
    if args.stride > 1:
        files = files[:: args.stride]
    if not files:
        print(f"No frames matching {args.pattern} in {args.frames_dir}", file=sys.stderr)
        return 1

    weights = _cos_lat_weights(ANALYSIS_HEIGHT, ANALYSIS_WIDTH)
    prev_rgb: np.ndarray | None = None
    land_series: list[float] = []
    snow_series: list[float] = []
    mean_diffs: list[float] = []
    jump_pcts: list[float] = []

    for path in files:
        rgb = _load_rgb(path)
        cls = _classify_elevation(rgb)
        land_pct = float(weights[cls["land"]].sum() * 100.0)
        snow_pct = float(weights[cls["snow"]].sum() * 100.0)
        land_series.append(land_pct)
        snow_series.append(snow_pct)

        if prev_rgb is not None:
            diff = np.abs(rgb - prev_rgb).max(axis=-1)
            mean_diffs.append(float((diff * weights).sum()))
            jump_pcts.append(float(weights[diff > 30].sum() * 100.0))
        prev_rgb = rgb

    d_land = np.diff(np.asarray(land_series))
    d_snow = np.diff(np.asarray(snow_series))
    reversals = 0
    for i in range(len(d_snow) - 1):
        if abs(d_snow[i]) > 0.25 and (d_snow[i] * d_snow[i + 1] < 0.0) and abs(d_snow[i + 1]) > 0.25:
            reversals += 1

    max_jump = max(jump_pcts, default=0.0)
    max_dland = float(np.max(np.abs(d_land))) if d_land.size else 0.0
    max_mean_diff = max(mean_diffs, default=0.0)
    verdict = "PASS" if (reversals == 0 and max_jump < 1.5) else "WARN"

    print(f"=== SEQUENCE {args.frames_dir} ({len(files)} frames, stride={args.stride}) [{verdict}] ===")
    print(
        f"  land (%)  : start={land_series[0]:.2f} -> end={land_series[-1]:.2f} "
        f"(min={min(land_series):.2f}, max={max(land_series):.2f}, max_|step_d|={max_dland:.2f}%)"
    )
    print(
        f"  stability : max_jump(>30/255)={max_jump:.3f}%  max_weighted_diff={max_mean_diff:.3f}  "
        f"snow_reversals={reversals}"
    )

    first_m = analyze_frame_metrics(files[0], None, None, f"FIRST:{os.path.basename(files[0])}")
    last_m = analyze_frame_metrics(files[-1], None, None, f"LAST:{os.path.basename(files[-1])}")
    _print_frame_report(first_m)
    _print_frame_report(last_m)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Quantitative spherical diagnostics for PlanetGen captures.")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_frame = sub.add_parser("analyze-frame")
    p_frame.add_argument("elevation")
    p_frame.add_argument("--plumes", default=None)
    p_frame.add_argument("--plates", default=None)
    p_frame.add_argument("--label", default=None)
    p_frame.add_argument("--json", action="store_true")
    p_frame.set_defaults(func=cmd_analyze_frame)

    p_seq = sub.add_parser("analyze-sequence")
    p_seq.add_argument("frames_dir")
    p_seq.add_argument("--pattern", default="frame_*.png")
    p_seq.add_argument("--stride", type=int, default=1)
    p_seq.set_defaults(func=cmd_analyze_sequence)

    p_cmp = sub.add_parser("compare-targets")
    p_cmp.add_argument("candidate")
    p_cmp.add_argument("refs", nargs="+")
    p_cmp.add_argument("--plumes", default=None)
    p_cmp.add_argument("--plates", default=None)
    p_cmp.set_defaults(func=cmd_compare_targets)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
