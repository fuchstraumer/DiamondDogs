"""Per-cell diff of two PlanetGen .pghp snapshots of one run (A earlier, B later).

The dump holds, for each world point, the crust of the plate that owns it. Plates move, so between two steps a world
point often maps to a neighbouring cell of the same plate: its crust age then differs a little from "age + dt". That is
resampling, not a change of crust. Each point falls in one class:

  aged          same owner, age grew by dt (the same crust)
  resampled     same owner, age off by up to 20 My (a neighbouring cell of the plate; normal motion)
  jump          same owner, age off by more than 20 My (old and young crust side by side on one plate)
  born          new crust this interval (age <= dt), where A had no owner or older crust
  owner_change  another plate owns the point now, with crust that is not new
  dead_owner    the owner's cell is not alive (the alive flag is clear)
  unowned       no plate owns the point in B

"interior births" are births with 2 or more same-plate ocean neighbours older than 20 My in B: new crust made inside
old crust, not at a ridge. Their A state tells how they got there (a dead cell of the same plate, another plate, or no
owner).

Use it on dumps one step apart, or on stage dumps inside one step (planet.dump_state with a Stage) to find the pass
that kills or creates cells. Filter a region with --box (degrees) or one plate with --plate.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from typing import Any

import numpy as np

from planet_healpix_analysis import PGHP_FLAG_ALIVE, PGHP_FLAG_CONTINENTAL, get_healpix_geometry, load_pghp

CLASSES = ("aged", "resampled", "jump", "born", "owner_change", "dead_owner", "unowned")
AGE_TOLERANCE_MY = 0.05
JUMP_MY = 20.0


def classify(a: np.ndarray, b: np.ndarray, dt_my: float, plate_slots: int) -> np.ndarray:
    """Returns one class index (into CLASSES) for each world point."""
    owned_a = a["owner_plate"] < plate_slots
    owned_b = b["owner_plate"] < plate_slots
    same_owner = owned_b & (a["owner_plate"] == b["owner_plate"])
    age_a = a["crust_age_my"].astype(np.float64)
    age_b = b["crust_age_my"].astype(np.float64)
    mismatch = np.abs(age_b - (age_a + dt_my))
    alive_b = (b["flags"] & PGHP_FLAG_ALIVE) != 0

    if dt_my > AGE_TOLERANCE_MY:
        # Crust born strictly after t_A has age_b <= dt_my - step_dt < dt_my; cells already born at t_A have
        # age_b == dt_my and must not be double-counted when a neighbouring world point resamples onto them.
        born = owned_b & (age_b < dt_my - AGE_TOLERANCE_MY)
    else:
        # Same-step stage diff (dt_my == 0): a cell created between stage A and stage B has age_b == 0 where A
        # either had no owner or older crust.
        born = owned_b & (age_b <= AGE_TOLERANCE_MY) & (~owned_a | (age_a > AGE_TOLERANCE_MY))

    out = np.full(a.shape[0], CLASSES.index("aged"), dtype=np.int8)
    out[same_owner & (mismatch > AGE_TOLERANCE_MY)] = CLASSES.index("resampled")
    out[same_owner & (mismatch > JUMP_MY)] = CLASSES.index("jump")
    out[owned_b & ~same_owner] = CLASSES.index("owner_change")
    out[born] = CLASSES.index("born")
    out[owned_b & ~alive_b] = CLASSES.index("dead_owner")
    out[~owned_b] = CLASSES.index("unowned")
    return out


def interior_births(b: np.ndarray, born: np.ndarray, neighbors4: np.ndarray) -> np.ndarray:
    """Ocean births with 2 or more same-plate ocean neighbours older than JUMP_MY: new crust inside old crust."""
    ocean = (b["flags"] & PGHP_FLAG_CONTINENTAL) == 0
    age = b["crust_age_my"].astype(np.float64)
    owner = b["owner_plate"]
    old_neighbours = np.zeros(b.shape[0], dtype=np.int8)

    for k in range(4):
        n = neighbors4[:, k]
        old_neighbours += (ocean[n] & (owner[n] == owner) & (age[n] > JUMP_MY)).astype(np.int8)

    return born & ocean & (old_neighbours >= 2)


def region_mask(order: int, box: list[float] | None) -> np.ndarray:
    z, phi, _ = get_healpix_geometry(order)

    if box is None:
        return np.ones(z.shape[0], dtype=bool)

    lat = np.degrees(np.arcsin(np.clip(z, -1.0, 1.0)))
    lon = np.degrees(np.where(phi > math.pi, phi - 2.0 * math.pi, phi))
    lat0, lat1, lon0, lon1 = box
    return (lat >= lat0) & (lat <= lat1) & (lon >= lon0) & (lon <= lon1)


def diff(path_a: str, path_b: str, box: list[float] | None, plate: int | None, examples: int) -> dict[str, Any]:
    ha, a = load_pghp(path_a)
    hb, b = load_pghp(path_b)

    if ha["order"] != hb["order"]:
        raise ValueError("the two dumps have different HEALPix orders")

    dt_my = hb["time_my"] - ha["time_my"]

    if dt_my < -AGE_TOLERANCE_MY:
        raise ValueError(f"dump A ({ha['time_my']:.2f} My) is later than dump B ({hb['time_my']:.2f} My); pass earlier dump first")
    slots = max(ha["plate_slot_count"], hb["plate_slot_count"])
    cls = classify(a, b, dt_my, slots)
    mask = region_mask(ha["order"], box)

    if plate is not None:
        mask &= (a["owner_plate"] == plate) | (b["owner_plate"] == plate)

    ocean_b = (b["flags"] & PGHP_FLAG_CONTINENTAL) == 0
    total = int(np.count_nonzero(mask))
    counts = {name: int(np.count_nonzero(mask & (cls == i))) for i, name in enumerate(CLASSES)}

    _, _, neighbors4 = get_healpix_geometry(ha["order"])
    born = cls == CLASSES.index("born")
    interior = mask & interior_births(b, born, neighbors4)
    owned_a = a["owner_plate"] < slots
    same_plate_a = owned_a & (a["owner_plate"] == b["owner_plate"])
    alive_a = (a["flags"] & PGHP_FLAG_ALIVE) != 0
    interior_from = {
        "no_owner": int(np.count_nonzero(interior & ~owned_a)),
        "same_plate_alive": int(np.count_nonzero(interior & same_plate_a & alive_a)),
        "same_plate_dead": int(np.count_nonzero(interior & same_plate_a & ~alive_a)),
        "other_plate": int(np.count_nonzero(interior & owned_a & ~same_plate_a)),
    }

    sample = []

    for index in np.nonzero(mask & (cls != CLASSES.index("aged")))[0][:examples]:
        sample.append({
            "point": int(index),
            "class": CLASSES[int(cls[index])],
            "owner": [int(a["owner_plate"][index]), int(b["owner_plate"][index])],
            "crust_age_my": [round(float(a["crust_age_my"][index]), 2), round(float(b["crust_age_my"][index]), 2)],
            "ocean": bool(ocean_b[index]),
            "front": [int(a["front"][index]), int(b["front"][index])],
        })

    return {
        "a": {"path": path_a, "step": ha["step"], "time_my": ha["time_my"], "stage": ha["stage"]},
        "b": {"path": path_b, "step": hb["step"], "time_my": hb["time_my"], "stage": hb["stage"]},
        "dt_my": round(dt_my, 4),
        "region_points": total,
        "counts": counts,
        "fractions": {name: round(count / max(total, 1), 5) for name, count in counts.items()},
        "births_ocean": int(np.count_nonzero(mask & born & ocean_b)),
        "interior_births": int(np.count_nonzero(interior)),
        "interior_births_from": interior_from,
        "interior_birth_owners": {int(p): int(c) for p, c in zip(*np.unique(b["owner_plate"][interior], return_counts=True))},
        "examples": sample,
    }


def print_diff(report: dict[str, Any]) -> None:
    a = report["a"]
    b = report["b"]
    print(f"=== A step {a['step']} ({a['stage']}, {a['time_my']:.2f} My) -> B step {b['step']} ({b['stage']}, "
          f"{b['time_my']:.2f} My), dt = {report['dt_my']} My, {report['region_points']} points ===")

    for name in CLASSES:
        print(f"  {name:13} {report['counts'][name]:8d}  {100.0 * report['fractions'][name]:7.3f} %")

    print(f"  ocean births: {report['births_ocean']}, interior births: {report['interior_births']} "
          f"(from {report['interior_births_from']}), by owner {report['interior_birth_owners']}")

    for example in report["examples"]:
        print(f"    point {example['point']:7d} {example['class']:13} owner {example['owner'][0]}->{example['owner'][1]} "
              f"age {example['crust_age_my'][0]}->{example['crust_age_my'][1]} ocean={example['ocean']} "
              f"front {example['front'][0]}->{example['front'][1]}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Per-cell diff of two PlanetGen .pghp snapshots of one run.")
    parser.add_argument("a", help="earlier snapshot")
    parser.add_argument("b", help="later snapshot")
    parser.add_argument("--box", type=float, nargs=4, metavar=("LAT0", "LAT1", "LON0", "LON1"),
                        help="limit to a latitude/longitude box in degrees (longitude in -180..180)")
    parser.add_argument("--plate", type=int, help="limit to points owned by this plate in A or in B")
    parser.add_argument("--examples", type=int, default=0, help="print this many changed points")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    report = diff(args.a, args.b, args.box, args.plate, args.examples)

    if args.json:
        json.dump(report, sys.stdout, indent=2)
        print()
    else:
        print_diff(report)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
