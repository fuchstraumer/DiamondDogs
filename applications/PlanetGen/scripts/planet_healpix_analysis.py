"""Native 1D equal-area HEALPix data-science diagnostics for PlanetGen .pghp snapshots.

Computes exact equal-area metrics without 2D map projection distortion:
  - Plate ecology: HHI, N_eff = 1/HHI, largest_plate_share, active_plates, free_rift_slots, tiny plates
  - HEALPix 4-neighbor graph topology: continental connected components, confetti count (<= 4 cells),
    1-cell oceanic pinholes surrounded by continent, perimeter-to-area compactness 4*pi*A/P^2
  - Spatial frequency audit: real spherical harmonic angular power spectrum C_l (l=0..32) and slope
  - Hypsometry & seafloor age: 1D Wasserstein distance vs Earth ETOPO1, ocean/land modes, relief quantiles,
    ocean age mean & young spreading ridge fraction (< 5 My)
"""

from __future__ import annotations

import argparse
import json
import math
import os
import struct
from typing import Any

import numpy as np
from scipy.sparse import csr_matrix
from scipy.sparse.csgraph import connected_components

PGHP_HEADER_FMT = "<4sIIIIifIII24x"
PGHP_HEADER_SIZE = struct.calcsize(PGHP_HEADER_FMT)
assert PGHP_HEADER_SIZE == 64

PGHP_CELL_DTYPE = np.dtype([
    ("owner_plate", "u1"),
    ("flags", "u1"),
    ("orogeny", "u1"),
    ("front", "u1"),
    ("elevation_km", "<f4"),
    ("relief_km", "<f4"),
    ("swell_km", "<f4"),
    ("crust_age_my", "<f4"),
    ("orogeny_age_my", "<f4"),
])
assert PGHP_CELL_DTYPE.itemsize == 24

# Earth ETOPO1 equal-area global hypsometry quantiles (area fraction -> elevation km)
ETOPO1_Q = np.array([
    0.000, 0.050, 0.100, 0.200, 0.300, 0.400, 0.500, 0.600,
    0.650, 0.700, 0.708, 0.750, 0.800, 0.850, 0.900, 0.950,
    0.980, 0.990, 0.995, 0.999, 1.000,
], dtype=np.float64)
ETOPO1_ELEV_KM = np.array([
    -6.50, -5.40, -5.00, -4.50, -4.20, -3.90, -3.50, -2.60,
    -0.60, -0.10,  0.00,  0.15,  0.30,  0.48,  0.80,  1.50,
     2.60,  3.90,  4.80,  5.60,  8.85,
], dtype=np.float64)

# HEALPix nested lookup tables matching applications/PlanetGen/gpu/Healpix.cpp (Gorski et al. 2005)
_FACE_SOUTH_RING = np.array([2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4], dtype=np.int64)
_FACE_AZIMUTH_OFF = np.array([1, 3, 5, 7, 0, 2, 4, 6, 1, 3, 5, 7], dtype=np.int64)
_ADJ_BASE_FACE = np.array([
    [8, 9, 10, 11, -1, -1, -1, -1, 10, 11, 8, 9],
    [5, 6, 7, 4, 8, 9, 10, 11, 9, 10, 11, 8],
    [-1, -1, -1, -1, 5, 6, 7, 4, -1, -1, -1, -1],
    [4, 5, 6, 7, 11, 8, 9, 10, 11, 8, 9, 10],
    [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
    [1, 2, 3, 0, 0, 1, 2, 3, 5, 6, 7, 4],
    [-1, -1, -1, -1, 7, 4, 5, 6, -1, -1, -1, -1],
    [3, 0, 1, 2, 3, 0, 1, 2, 4, 5, 6, 7],
    [2, 3, 0, 1, -1, -1, -1, -1, 0, 1, 2, 3],
], dtype=np.int32)
_SEAM_TRANSFORM = np.array([
    [0, 0, 3],
    [0, 0, 6],
    [0, 0, 0],
    [0, 0, 5],
    [0, 0, 0],
    [5, 0, 0],
    [0, 0, 0],
    [6, 0, 0],
    [3, 0, 0],
], dtype=np.int32)

# Cache HEALPix geometry & 4-neighbor tables per order so multi-age scripts pay once.
_HEALPIX_CACHE: dict[int, tuple[np.ndarray, np.ndarray, np.ndarray]] = {}


def _compact_bits_vec(v: np.ndarray) -> np.ndarray:
    v = v & np.uint64(0x5555555555555555)
    v = (v | (v >> np.uint64(1))) & np.uint64(0x3333333333333333)
    v = (v | (v >> np.uint64(2))) & np.uint64(0x0F0F0F0F0F0F0F0F)
    v = (v | (v >> np.uint64(4))) & np.uint64(0x00FF00FF00FF00FF)
    v = (v | (v >> np.uint64(8))) & np.uint64(0x0000FFFF0000FFFF)
    v = (v | (v >> np.uint64(16))) & np.uint64(0x00000000FFFFFFFF)
    return v.astype(np.int64)


def _spread_bits_vec(v: np.ndarray) -> np.ndarray:
    v = v.astype(np.uint64) & np.uint64(0x00000000FFFFFFFF)
    v = (v | (v << np.uint64(16))) & np.uint64(0x0000FFFF0000FFFF)
    v = (v | (v << np.uint64(8))) & np.uint64(0x00FF00FF00FF00FF)
    v = (v | (v << np.uint64(4))) & np.uint64(0x0F0F0F0F0F0F0F0F)
    v = (v | (v << np.uint64(2))) & np.uint64(0x3333333333333333)
    v = (v | (v << np.uint64(1))) & np.uint64(0x5555555555555555)
    return v


def get_healpix_geometry(order: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Returns cached (z, phi, neighbors4) for all N = 12 * 4^order nested HEALPix pixels."""
    if order in _HEALPIX_CACHE:
        return _HEALPIX_CACHE[order]

    nside = 1 << order
    n_pix = 12 * (nside * nside)
    face_bits = nside * nside
    pix = np.arange(n_pix, dtype=np.uint64)
    face = (pix >> np.uint64(2 * order)).astype(np.int64)
    in_face = pix & np.uint64(face_bits - 1)
    ix = _compact_bits_vec(in_face)
    iy = _compact_bits_vec(in_face >> np.uint64(1))

    # Spherical coordinates (z = cos(theta), phi) matching Healpix.cpp
    jr = _FACE_SOUTH_RING[face] * nside - ix - iy - 1
    nr = np.full(n_pix, nside, dtype=np.int64)
    kshift = np.zeros(n_pix, dtype=np.int64)
    z = np.zeros(n_pix, dtype=np.float64)

    north = jr < nside
    south = jr > 3 * nside
    eq = (~north) & (~south)

    nr[north] = jr[north]
    z[north] = 1.0 - (nr[north] * nr[north]) / (3.0 * float(nside * nside))

    nr[south] = 4 * nside - jr[south]
    z[south] = -1.0 + (nr[south] * nr[south]) / (3.0 * float(nside * nside))

    z[eq] = (2 * nside - jr[eq]) * (2.0 / (3.0 * float(nside)))
    kshift[eq] = (jr[eq] - nside) & 1

    jp = (_FACE_AZIMUTH_OFF[face] * nr + ix - iy + 1 + kshift) // 2
    nl4 = 4 * nr
    jp = np.where(jp > nl4, jp - nl4, jp)
    jp = np.where(jp < 1, jp + nl4, jp)
    phi = (jp.astype(np.float64) - 0.5 * (kshift.astype(np.float64) + 1.0)) * (0.5 * math.pi / nr.astype(np.float64))

    # 4 cardinal neighbors: (-1, 0), (0, +1), (+1, 0), (0, -1)
    zone = (face // 4).astype(np.int32)
    last_in_face = nside - 1
    deltas = [(-1, 0), (0, 1), (1, 0), (0, -1)]
    neighbors4 = np.empty((n_pix, 4), dtype=np.int32)

    for d_idx, (dx, dy) in enumerate(deltas):
        nx = ix + dx
        ny = iy + dy
        x_off = np.zeros(n_pix, dtype=np.int32)
        y_off = np.zeros(n_pix, dtype=np.int32)

        lo_x = nx < 0
        hi_x = nx >= nside
        nx = np.where(lo_x, nx + nside, np.where(hi_x, nx - nside, nx))
        x_off = np.where(lo_x, -1, np.where(hi_x, 1, 0))

        lo_y = ny < 0
        hi_y = ny >= nside
        ny = np.where(lo_y, ny + nside, np.where(hi_y, ny - nside, ny))
        y_off = np.where(lo_y, -1, np.where(hi_y, 1, 0))

        nbnum = (x_off + 1) + 3 * (y_off + 1)
        nb_face = _ADJ_BASE_FACE[nbnum, face]
        mask = _SEAM_TRANSFORM[nbnum, zone]

        # Apply flipX (1), flipY (2), then swapXY (4) — exact order from Healpix.cpp
        flip_x = (mask & 1) != 0
        flip_y = (mask & 2) != 0
        swap_xy = (mask & 4) != 0

        nx = np.where(flip_x, last_in_face - nx, nx)
        ny = np.where(flip_y, last_in_face - ny, ny)
        tx = np.where(swap_xy, ny, nx)
        ty = np.where(swap_xy, nx, ny)

        in_nb = _spread_bits_vec(tx) | (_spread_bits_vec(ty) << np.uint64(1))
        nb_pix = ((nb_face.astype(np.uint64) << np.uint64(2 * order)) + in_nb).astype(np.int32)
        neighbors4[:, d_idx] = nb_pix

    _HEALPIX_CACHE[order] = (z, phi, neighbors4)
    return z, phi, neighbors4


def load_pghp(path: str) -> tuple[dict[str, Any], np.ndarray]:
    """Reads a .pghp binary HEALPix snapshot into (header_dict, structured_numpy_array)."""
    with open(path, "rb") as f:
        raw = f.read()

    if len(raw) < PGHP_HEADER_SIZE:
        raise ValueError(f"{path}: truncated header ({len(raw)} bytes)")

    magic, version, order, point_count, plate_slot_count, step, time_my, stride, active_plate_count, flags = struct.unpack_from(
        PGHP_HEADER_FMT, raw, 0
    )
    if magic != b"PGHP" or version != 1 or stride != 24:
        raise ValueError(f"{path}: unexpected PGHP header (magic={magic!r}, version={version}, stride={stride})")

    expected = PGHP_HEADER_SIZE + point_count * stride
    if len(raw) != expected:
        raise ValueError(f"{path}: size mismatch ({len(raw)} != {expected})")

    cells = np.frombuffer(raw, dtype=PGHP_CELL_DTYPE, count=point_count, offset=PGHP_HEADER_SIZE)
    header = {
        "order": int(order),
        "point_count": int(point_count),
        "plate_slot_count": int(plate_slot_count),
        "active_plate_count": int(active_plate_count),
        "step": int(step),
        "time_my": float(time_my),
        "flags": int(flags),
    }
    return header, cells


def _spherical_power_spectrum(field: np.ndarray, z: np.ndarray, phi: np.ndarray, l_max: int = 32) -> np.ndarray:
    """Computes exact equal-area angular power spectrum C_l (l=0..l_max) via orthonormal real Legendre recurrence."""
    n = field.shape[0]
    w = (4.0 * math.pi) / float(n)
    f = (field.astype(np.float64) - float(np.mean(field))) * w
    sin_theta = np.sqrt(np.maximum(1.0 - z * z, 0.0))

    c_l = np.zeros(l_max + 1, dtype=np.float64)
    p_mm = np.full(n, 1.0 / math.sqrt(4.0 * math.pi), dtype=np.float64)

    for m in range(l_max + 1):
        if m > 0:
            p_mm = -p_mm * sin_theta * math.sqrt((2.0 * m + 1.0) / (2.0 * m))

        if m == 0:
            cos_m = None
            sin_m = None
        else:
            m_phi = float(m) * phi
            cos_m = np.cos(m_phi) * math.sqrt(2.0)
            sin_m = np.sin(m_phi) * math.sqrt(2.0)

        p_prev2 = p_mm
        if m == 0:
            a0 = float(np.dot(f, p_prev2))
            c_l[0] += a0 * a0
        else:
            ac = float(np.dot(f, p_prev2 * cos_m))
            as_ = float(np.dot(f, p_prev2 * sin_m))
            c_l[m] += ac * ac + as_ * as_

        if m + 1 <= l_max:
            p_prev1 = z * math.sqrt(2.0 * m + 3.0) * p_prev2
            if m == 0:
                a1 = float(np.dot(f, p_prev1))
                c_l[1] += a1 * a1
            else:
                ac = float(np.dot(f, p_prev1 * cos_m))
                as_ = float(np.dot(f, p_prev1 * sin_m))
                c_l[m + 1] += ac * ac + as_ * as_

            for l in range(m + 2, l_max + 1):
                c1 = math.sqrt((4.0 * l * l - 1.0) / (l * l - m * m))
                c2 = math.sqrt(((2.0 * l + 1.0) * ((l - 1.0) ** 2 - m * m)) / ((2.0 * l - 3.0) * (l * l - m * m)))
                p_cur = c1 * z * p_prev1 - c2 * p_prev2
                if m == 0:
                    al = float(np.dot(f, p_cur))
                    c_l[l] += al * al
                else:
                    ac = float(np.dot(f, p_cur * cos_m))
                    as_ = float(np.dot(f, p_cur * sin_m))
                    c_l[l] += ac * ac + as_ * as_
                p_prev2 = p_prev1
                p_prev1 = p_cur

    ells = np.arange(l_max + 1, dtype=np.float64)
    c_l /= (2.0 * ells + 1.0)
    return c_l


def analyze_pghp(path: str, l_max: int = 32) -> dict[str, Any]:
    """Runs all native equal-area HEALPix data-science checks on a .pghp snapshot."""
    header, cells = load_pghp(path)
    order = header["order"]
    n_pix = header["point_count"]
    plate_count = header["plate_slot_count"]
    z, phi, neighbors4 = get_healpix_geometry(order)

    owner = cells["owner_plate"]
    flags = cells["flags"]
    orogeny = cells["orogeny"]
    front = cells["front"]
    elev = cells["elevation_km"].astype(np.float64)
    relief = cells["relief_km"].astype(np.float64)
    crust_age = cells["crust_age_my"].astype(np.float64)

    is_cont = (flags & 2) != 0
    is_ocean = ~is_cont
    is_volcanic = orogeny == 4

    # 1. Plate ecology
    valid_owner = owner < plate_count
    plate_counts = np.bincount(owner[valid_owner], minlength=plate_count)
    plate_cont_counts = np.bincount(owner[valid_owner & is_cont], minlength=plate_count)
    total_owned = int(plate_counts.sum())
    shares = plate_counts.astype(np.float64) / max(float(total_owned), 1.0)
    hhi = float(np.sum(shares * shares))
    n_eff = (1.0 / hhi) if hhi > 1.0e-9 else 0.0
    largest_plate_share = float(np.max(shares)) if shares.size else 0.0
    active_plates = int(np.count_nonzero(plate_counts > 0))
    free_rift_slots = int(np.count_nonzero((plate_counts == 0) & (plate_cont_counts == 0)))
    tiny_plates = int(np.count_nonzero((shares > 0.0) & (shares < 0.01)))

    # 2. Native HEALPix 4-neighbor graph topology & confetti detection
    cont_idx = np.nonzero(is_cont)[0]
    if cont_idx.size > 0:
        nbs = neighbors4[cont_idx]
        nb_is_cont = is_cont[nbs]
        src = np.repeat(cont_idx, 4)
        dst = nbs.ravel()
        same_mask = nb_is_cont.ravel()
        graph = csr_matrix(
            (np.ones(int(np.count_nonzero(same_mask)), dtype=np.int8), (src[same_mask], dst[same_mask])),
            shape=(n_pix, n_pix),
        )
        # Remap to contiguous continental nodes for fast component counting
        n_comp_all, labels_all = connected_components(graph, directed=False, return_labels=True)
        cont_labels = labels_all[cont_idx]
        raw_sizes = np.bincount(cont_labels)
        non_empty = raw_sizes > 0
        comp_sizes = raw_sizes[non_empty]
        comp_sizes_sorted = np.sort(comp_sizes)[::-1]

        # Mismatched edges per continental cell (each ocean neighbor contributes 1 edge)
        boundary_edges_per_cell = np.sum(~nb_is_cont, axis=1)
        total_cont_edges = int(boundary_edges_per_cell.sum())

        # Per-component perimeter & compactness (4 * pi * A / P^2 in cell units)
        comp_perims = np.bincount(cont_labels, weights=boundary_edges_per_cell, minlength=raw_sizes.size)[non_empty]
        valid_c = comp_sizes >= 16
        if np.any(valid_c):
            areas_c = comp_sizes[valid_c].astype(np.float64)
            perims_c = np.maximum(comp_perims[valid_c].astype(np.float64), 1.0)
            compact_arr = (4.0 * math.pi * areas_c) / (perims_c * perims_c)
            mean_compactness = float(np.mean(compact_arr))
            min_compactness = float(np.min(compact_arr))
        else:
            mean_compactness = 1.0
            min_compactness = 1.0

        cont_components_total = int(comp_sizes_sorted.size)
        cont_components_ge_1pct = int(np.count_nonzero(comp_sizes_sorted >= int(0.01 * n_pix)))
        cont_confetti_le4 = int(np.count_nonzero(comp_sizes_sorted <= 4))
        largest_continent_frac = float(comp_sizes_sorted[0]) / float(n_pix)
    else:
        cont_components_total = 0
        cont_components_ge_1pct = 0
        cont_confetti_le4 = 0
        largest_continent_frac = 0.0
        total_cont_edges = 0
        mean_compactness = 1.0
        min_compactness = 1.0

    # 1-cell oceanic pinholes completely surrounded by 4 continental neighbors
    ocean_idx = np.nonzero(is_ocean)[0]
    if ocean_idx.size > 0:
        ocean_nb_cont = np.all(is_cont[neighbors4[ocean_idx]], axis=1)
        ocean_pinholes_1cell = int(np.count_nonzero(ocean_nb_cont))
    else:
        ocean_pinholes_1cell = 0

    # 3. Spatial frequency audit (spherical angular power spectrum C_l)
    c_l = _spherical_power_spectrum(elev, z, phi, l_max=l_max)
    ells_fit = np.arange(6, min(l_max, 32) + 1, dtype=np.float64)
    c_fit = np.maximum(c_l[6:min(l_max, 32) + 1], 1.0e-18)
    spectral_slope = float(np.polyfit(np.log10(ells_fit), np.log10(c_fit), 1)[0])
    low_deg_power = float(c_l[1] + c_l[2])
    mid_deg_power = float(np.sum(c_l[3:13]))
    low_deg_ratio = low_deg_power / max(mid_deg_power, 1.0e-12)

    # 4. Hypsometry & seafloor age
    q_grid = np.linspace(0.0, 1.0, 1001, dtype=np.float64)
    sim_q = np.quantile(elev, q_grid)
    earth_q = np.interp(q_grid, ETOPO1_Q, ETOPO1_ELEV_KM)
    wasserstein_km = float(np.mean(np.abs(sim_q - earth_q)))

    land_frac = float(np.mean(is_cont))
    land_elev = elev[is_cont]
    ocean_elev = elev[is_ocean]
    land_relief = relief[is_cont]
    ocean_age = crust_age[is_ocean]

    def _mode_bin(arr: np.ndarray, lo: float, hi: float, step_km: float = 0.25) -> float:
        if arr.size == 0:
            return 0.0
        bins = np.arange(lo, hi + step_km, step_km)
        hist, edges = np.histogram(arr, bins=bins)
        idx = int(np.argmax(hist))
        return float(0.5 * (edges[idx] + edges[idx + 1]))

    abyssal_mode_km = _mode_bin(ocean_elev, -7.0, 0.5, 0.25)
    lowland_mode_km = _mode_bin(land_elev, -0.5, 6.0, 0.25)
    land_below_0_5km_frac = float(np.mean(land_elev <= 0.55)) if land_elev.size else 0.0
    relief_p50_km = float(np.quantile(land_relief, 0.50)) if land_relief.size else 0.0
    relief_p95_km = float(np.quantile(land_relief, 0.95)) if land_relief.size else 0.0
    relief_p99_km = float(np.quantile(land_relief, 0.99)) if land_relief.size else 0.0
    relief_max_km = float(np.max(land_relief)) if land_relief.size else 0.0

    ocean_age_mean_my = float(np.mean(ocean_age)) if ocean_age.size else 0.0
    ocean_age_p95_my = float(np.quantile(ocean_age, 0.95)) if ocean_age.size else 0.0
    young_ridge_frac_under_5my = float(np.mean(ocean_age < 5.0)) if ocean_age.size else 0.0

    # Seafloor age must change smoothly inside one plate (it grows away from the ridge). A jump of more than
    # 20 My between two ocean neighbours on the same plate shows interleaved old and new crust: the striations
    # in the 2026-11-27 earth run (bands of ~140 My crust between ~40 My crust on one plate).
    src_all = np.repeat(np.arange(n_pix), 4)
    dst_all = neighbors4.ravel()
    pair_ok = (dst_all > src_all) & is_ocean[src_all] & is_ocean[dst_all] & (owner[src_all] == owner[dst_all])
    pair_jump = pair_ok & (np.abs(crust_age[src_all] - crust_age[dst_all]) > 20.0)
    ocean_pairs = int(np.count_nonzero(pair_ok))
    age_jump_pairs = int(np.count_nonzero(pair_jump))
    age_jump_pair_frac = age_jump_pairs / max(ocean_pairs, 1)

    # Island-arc relief belongs within a few hundred km of a trench; a large share of the ocean floor with it
    # means the arc uplift reaches too far or its relief never decays
    ocean_arc_relief = is_ocean & (orogeny == 3) & (relief > 0.5)
    ocean_arc_relief_frac = float(np.count_nonzero(ocean_arc_relief)) / max(float(ocean_idx.size), 1.0)

    volcanic_cont_cells = int(np.count_nonzero(is_cont & is_volcanic))
    volcanic_ocean_cells = int(np.count_nonzero(is_ocean & is_volcanic & (relief > 0.05)))
    subduction_front_cells = int(np.count_nonzero((front == 1) | (front == 2)))
    collision_front_cells = int(np.count_nonzero(front == 3))

    # 5. Hard PASS / WARN / FAIL gates from docs/planetgen-agent-tools.md
    flags_out: list[str] = []
    has_fail = False
    has_warn = False

    if header["step"] > 0 and ocean_age.size > 0 and young_ridge_frac_under_5my <= 0.0:
        has_fail = True
        flags_out.append("FAIL:young_ridge_frac_under_5my=0")

    if relief_p99_km > 7.5:
        has_fail = True
        flags_out.append(f"FAIL:relief_p99_km={relief_p99_km:.2f}>7.5")
    elif relief_p99_km > 6.5:
        has_warn = True
        flags_out.append(f"WARN:relief_p99_km={relief_p99_km:.2f}>6.5")

    if largest_plate_share > 0.35:
        has_warn = True
        flags_out.append(f"WARN:largest_plate_share={largest_plate_share:.3f}>0.35")

    if n_eff < 5.0:
        has_warn = True
        flags_out.append(f"WARN:N_eff={n_eff:.2f}<5.0")

    if cont_confetti_le4 > 25:
        has_warn = True
        flags_out.append(f"WARN:confetti_le4={cont_confetti_le4}>25")

    if ocean_pinholes_1cell > 20:
        has_warn = True
        flags_out.append(f"WARN:pinholes_1cell={ocean_pinholes_1cell}>20")

    if age_jump_pair_frac > 0.02:
        has_warn = True
        flags_out.append(f"WARN:seafloor_age_jump_pair_frac={age_jump_pair_frac:.3f}>0.02")

    if ocean_arc_relief_frac > 0.10:
        has_warn = True
        flags_out.append(f"WARN:ocean_arc_relief_frac={ocean_arc_relief_frac:.3f}>0.10")

    if spectral_slope > -1.0:
        has_warn = True
        flags_out.append(f"WARN:flat_spectrum_slope={spectral_slope:.2f}")

    verdict = "FAIL" if has_fail else ("WARN" if has_warn else "PASS")

    return {
        "path": os.path.abspath(path),
        "header": header,
        "verdict": verdict,
        "flags": flags_out,
        "plate_ecology": {
            "active_plates": active_plates,
            "free_rift_slots": free_rift_slots,
            "tiny_plates_under_1pct": tiny_plates,
            "largest_plate_share": round(largest_plate_share, 4),
            "hhi": round(hhi, 4),
            "n_eff": round(n_eff, 2),
        },
        "topology": {
            "continental_components_total": cont_components_total,
            "continents_ge_1pct": cont_components_ge_1pct,
            "continental_components_le4_cells": cont_confetti_le4,
            "largest_continent_frac": round(largest_continent_frac, 4),
            "ocean_pinholes_1cell": ocean_pinholes_1cell,
            "continental_boundary_edges": total_cont_edges,
            "mean_compactness": round(mean_compactness, 4),
            "min_compactness": round(min_compactness, 4),
        },
        "spectrum": {
            "l_max": l_max,
            "slope_l6_32": round(spectral_slope, 3),
            "low_degree_ratio": round(low_deg_ratio, 3),
            "c_l_sample": [round(float(c_l[i]), 6) for i in (1, 2, 4, 8, 16, 32) if i <= l_max],
        },
        "hypsometry": {
            "wasserstein_earth_km": round(wasserstein_km, 4),
            "land_frac": round(land_frac, 4),
            "abyssal_mode_km": round(abyssal_mode_km, 3),
            "lowland_mode_km": round(lowland_mode_km, 3),
            "land_below_0_5km_frac": round(land_below_0_5km_frac, 4),
            "relief_p50_km": round(relief_p50_km, 3),
            "relief_p95_km": round(relief_p95_km, 3),
            "relief_p99_km": round(relief_p99_km, 3),
            "relief_max_km": round(relief_max_km, 3),
        },
        "seafloor_and_orogeny": {
            "ocean_age_mean_my": round(ocean_age_mean_my, 2),
            "ocean_age_p95_my": round(ocean_age_p95_my, 2),
            "young_ridge_frac_under_5my": round(young_ridge_frac_under_5my, 4),
            "age_jump_pairs_over_20my": age_jump_pairs,
            "age_jump_pair_frac": round(age_jump_pair_frac, 4),
            "ocean_arc_relief_frac": round(ocean_arc_relief_frac, 4),
            "volcanic_ocean_cells": volcanic_ocean_cells,
            "volcanic_cont_cells": volcanic_cont_cells,
            "subduction_front_cells": subduction_front_cells,
            "collision_front_cells": collision_front_cells,
        },
    }


def print_pghp_report(report: dict[str, Any]) -> None:
    h = report["header"]
    pe = report["plate_ecology"]
    tp = report["topology"]
    sp = report["spectrum"]
    hy = report["hypsometry"]
    sf = report["seafloor_and_orogeny"]

    print(f"=== HEALPix order={h['order']} step={h['step']} t={h['time_my']:.1f} My [{report['verdict']}] ===")
    print(
        f"  plates   : active={pe['active_plates']} free={pe['free_rift_slots']} tiny(<1%)={pe['tiny_plates_under_1pct']} "
        f"largest={pe['largest_plate_share']*100:.1f}% HHI={pe['hhi']:.3f} N_eff={pe['n_eff']:.2f}"
    )
    print(
        f"  topology : comps={tp['continental_components_total']} (cont>=1%={tp['continents_ge_1pct']}, "
        f"confetti<=4={tp['continental_components_le4_cells']}) pinholes={tp['ocean_pinholes_1cell']} "
        f"compact(mean/min)={tp['mean_compactness']:.3f}/{tp['min_compactness']:.3f}"
    )
    print(
        f"  hypsom   : land={hy['land_frac']*100:.2f}% W1_earth={hy['wasserstein_earth_km']:.3f}km "
        f"modes(sea/land)={hy['abyssal_mode_km']:.2f}/{hy['lowland_mode_km']:.2f}km "
        f"relief(p50/p95/p99/max)={hy['relief_p50_km']:.2f}/{hy['relief_p95_km']:.2f}/{hy['relief_p99_km']:.2f}/{hy['relief_max_km']:.2f}km"
    )
    print(
        f"  seafloor : age_mean={sf['ocean_age_mean_my']:.1f}My (p95={sf['ocean_age_p95_my']:.1f}My) "
        f"young(<5My)={sf['young_ridge_frac_under_5my']*100:.2f}% "
        f"age_jumps={sf['age_jump_pair_frac']*100:.2f}% arc_relief={sf['ocean_arc_relief_frac']*100:.1f}% "
        f"volc(ocean/cont)={sf['volcanic_ocean_cells']}/{sf['volcanic_cont_cells']} "
        f"fronts(sub/col)={sf['subduction_front_cells']}/{sf['collision_front_cells']}"
    )
    print(
        f"  spectrum : slope(l=6..32)={sp['slope_l6_32']:.2f} low_deg_ratio={sp['low_degree_ratio']:.2f}"
    )
    if report["flags"]:
        print("  flags    : " + " | ".join(report["flags"]))


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze a native PlanetGen .pghp HEALPix snapshot.")
    parser.add_argument("snapshot", help="Path to .pghp file produced by planet.dump_state")
    parser.add_argument("--l-max", type=int, default=32)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    report = analyze_pghp(args.snapshot, l_max=args.l_max)
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print_pghp_report(report)
    return 1 if report["verdict"] == "FAIL" else 0


if __name__ == "__main__":
    raise SystemExit(main())
