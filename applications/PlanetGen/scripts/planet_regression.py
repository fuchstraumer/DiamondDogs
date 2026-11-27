"""Unified 0 -> 400 My data-first regression & equal-area contact-sheet runner for PlanetGenLoom.

Unifies planet_probe.py, planet_diagnostics.py, series_probe.py, and hypsometry.py into one
repeatable invariant-first check using GPU telemetry (planet.telemetry), native equal-area
HEALPix snapshots (planet.dump_state -> planet_healpix_analysis.py), and multi-projection captures.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import socket
import subprocess
import sys
import time
from typing import Any

from PIL import Image, ImageDraw

from planet_healpix_analysis import analyze_pghp


class JsonRpcClient:
    def __init__(self, host: str, port: int):
        self.sock = socket.create_connection((host, port), timeout=180.0)
        self.file = self.sock.makefile("r", encoding="utf-8", newline="\n")
        self.next_id = 1

    def request(self, method: str, params: dict[str, Any]) -> Any:
        req_id = self.next_id
        self.next_id += 1
        payload = json.dumps({
            "jsonrpc": "2.0",
            "id": req_id,
            "method": method,
            "params": params,
        }) + "\n"
        self.sock.sendall(payload.encode("utf-8"))
        line = self.file.readline()
        if not line:
            raise RuntimeError(f"EOF waiting for reply to {method}")
        resp = json.loads(line)
        if "error" in resp:
            raise RuntimeError(f"{method} failed: {resp['error']}")
        return resp["result"]

    def call(self, name: str, arguments: dict[str, Any] | None = None) -> Any:
        return self.request("tool.call", {"name": name, "arguments": arguments or {}})

    def set_value(self, path: str, value: Any) -> Any:
        return self.request("tool.set", {"path": path, "value": value})

    def close(self) -> None:
        self.file.close()
        self.sock.close()


BUILD_DIR_CANDIDATES = ("build/ninja-clang-cl", "build/ninja-msvc")


def _resolve_exe(build_dir: str, config: str) -> str:
    """With no build dir, takes the newest PlanetGenLoom.exe of the two presets, so a stale tree is never used."""
    def exe_in(directory: str) -> str:
        return os.path.abspath(os.path.join(directory, "applications", "PlanetGen", config, "PlanetGenLoom.exe"))

    if build_dir:
        return exe_in(build_dir)

    found = [exe_in(d) for d in BUILD_DIR_CANDIDATES if os.path.exists(exe_in(d))]

    if not found:
        return exe_in(BUILD_DIR_CANDIDATES[0])

    return max(found, key=os.path.getmtime)


def _build_contact_sheet(paths_and_labels: list[tuple[str, str]], out_path: str, thumb_w: int = 640, thumb_h: int = 320) -> None:
    if not paths_and_labels:
        return
    cols = min(3, len(paths_and_labels))
    rows = (len(paths_and_labels) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * thumb_w, rows * thumb_h), (12, 15, 22))
    draw = ImageDraw.Draw(sheet)

    for idx, (img_path, label) in enumerate(paths_and_labels):
        if not os.path.exists(img_path):
            continue
        with Image.open(img_path) as im:
            tile = im.convert("RGB").resize((thumb_w, thumb_h), Image.Resampling.BILINEAR)
        col = idx % cols
        row = idx // cols
        x0 = col * thumb_w
        y0 = row * thumb_h
        sheet.paste(tile, (x0, y0))
        draw.rectangle([x0 + 6, y0 + 6, x0 + 180, y0 + 26], fill=(10, 12, 18))
        draw.text((x0 + 12, y0 + 10), label, fill=(240, 245, 250))

    sheet.save(out_path)


def _merge_verdict(*verdicts: str) -> str:
    if "FAIL" in verdicts:
        return "FAIL"
    if "WARN" in verdicts:
        return "WARN"
    return "PASS"


def _print_summary_table(checkpoints: list[dict[str, Any]], preset: str, order: int, elapsed_s: float) -> None:
    print(f"\n=== PlanetGen Regression Summary (preset={preset}, order={order}, elapsed={elapsed_s:.1f}s) ===")
    header = (
        f"{'Age':>6} {'Verdict':>7} {'Land%':>6} {'W1km':>6} {'p99':>5} {'N_eff':>5} {'MaxP%':>6} "
        f"{'Conf':>5} {'Hole':>5} {'Slope':>6} {'AgeM':>5} {'Yng%':>5} {'VolcF':>5} {'Col':>4} {'Rift':>4} {'RBlk':>4}"
    )
    print(header)
    print("-" * len(header))
    for cp in checkpoints:
        hp = cp["healpix"]
        tm = cp["telemetry"]
        pe = hp["plate_ecology"]
        tp = hp["topology"]
        hy = hp["hypsometry"]
        sf = hp["seafloor_and_orogeny"]
        sp = hp["spectrum"]
        volc_bad = int(tm.get("UpliftVolcanicToContFlips", 0)) + int(tm.get("UpliftVolcanicOverwritten", 0))
        print(
            f"{cp['age_my']:6.0f} {cp['verdict']:>7} {hy['land_frac']*100:6.2f} {hy['wasserstein_earth_km']:6.3f} "
            f"{hy['relief_p99_km']:5.2f} {pe['n_eff']:5.2f} {pe['largest_plate_share']*100:6.1f} "
            f"{tp['continental_components_le4_cells']:5d} {tp['ocean_pinholes_1cell']:5d} {sp['slope_l6_32']:6.2f} "
            f"{sf['ocean_age_mean_my']:5.1f} {sf['young_ridge_frac_under_5my']*100:5.1f} "
            f"{volc_bad:5d} {int(tm.get('CollisionsTriggered', 0)):4d} "
            f"{int(tm.get('RiftsCompleted', 0)):4d} {int(tm.get('RiftsBlockedNoFreeSlot', 0)):4d}"
        )

    all_flags: list[str] = []
    for cp in checkpoints:
        for flag in cp["flags"]:
            all_flags.append(f"t={cp['age_my']:.0f}My:{flag}")
    if all_flags:
        print("Flags:")
        for flag in all_flags:
            print(f"  - {flag}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Unified 0 -> 400 My PlanetGen invariant regression runner.")
    parser.add_argument("--preset", default="earth", choices=["earth", "atoll", "reference"])
    parser.add_argument("--reference", default="")
    parser.add_argument("--shaping", default="")
    parser.add_argument("--order", type=int, default=7)
    parser.add_argument("--dt", type=float, default=0.5)
    parser.add_argument("--ages", default="0,50,100,200,400", help="Comma-separated checkpoint ages in My")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--plates", type=int, default=0)
    parser.add_argument("--plumes", default="default", choices=["default", "none"])
    parser.add_argument("--rift", default="on", choices=["on", "off"])
    parser.add_argument("--erosion", default="on", choices=["on", "off"])
    parser.add_argument("--width", type=int, default=1024)
    parser.add_argument("--height", type=int, default=512)
    parser.add_argument("--projection", default="equal_earth",
                        choices=["equirectangular", "equal_earth", "mollweide", "orthographic", "polar_north", "polar_south"])
    parser.add_argument("--layers", default="atlas,plates,age,plumes", help="Comma-separated capture layers")
    parser.add_argument("--no-images", action="store_true", help="Skip PNG captures and run pure data/telemetry checks")
    parser.add_argument("--build-dir", default="",
                        help="CMake build tree; empty takes the newest exe of build/ninja-clang-cl and build/ninja-msvc")
    parser.add_argument("--config", default="RelWithDebInfo", choices=["Debug", "RelWithDebInfo", "Release"])
    parser.add_argument("--exe", default="", help="Explicit path to PlanetGenLoom.exe (overrides --build-dir/--config)")
    parser.add_argument("--validation", default="", choices=["", "full", "none"])
    parser.add_argument("--set", action="append", default=[], help="Override tool field key=value after connect")
    parser.add_argument("--out", default="build/planet_regression")
    args = parser.parse_args()

    exe = os.path.abspath(args.exe) if args.exe else _resolve_exe(args.build_dir, args.config)
    if not os.path.exists(exe):
        print(f"Missing executable: {exe}", file=sys.stderr)
        return 1

    print(f"Using {exe}")

    ages = sorted({float(x.strip()) for x in args.ages.split(",") if x.strip()})
    if not ages:
        print("No valid ages supplied in --ages", file=sys.stderr)
        return 1

    layer_map = {
        "elevation": 0, "plates": 1, "age": 2, "relief": 3,
        "fronts": 4, "voronoi": 5, "plumes": 6, "atlas": 7, "raw": 8,
    }
    proj_map = {
        "equirectangular": 0, "equal_earth": 1, "mollweide": 2,
        "orthographic": 3, "polar_north": 4, "polar_south": 5,
    }
    layers = [l.strip() for l in args.layers.split(",") if l.strip()]
    for l in layers:
        if l not in layer_map:
            print(f"Unknown layer '{l}' in --layers", file=sys.stderr)
            return 1
    proj_id = proj_map[args.projection]

    out_dir = os.path.abspath(args.out)
    os.makedirs(out_dir, exist_ok=True)

    env = os.environ.copy()
    env["VK_LOADER_LAYERS_DISABLE"] = "VK_LAYER_OBS_HOOK"

    cmd = [
        exe,
        f"--preset={args.preset}",
        f"--order={args.order}",
        f"--dt={args.dt}",
        f"--width={args.width}",
        f"--height={args.height}",
        f"--plumes={args.plumes}",
        f"--rift={args.rift}",
        f"--erosion={args.erosion}",
        f"--projection={args.projection}",
        "--tool-port=0",
        "--telemetry",
        "--timestamps",
        f"--capture-dir={out_dir}",
    ]
    if args.reference:
        cmd.append(f"--reference={args.reference}")
    if args.shaping:
        cmd.append(f"--shaping={args.shaping}")
    if args.seed != 42:
        cmd.append(f"--seed={args.seed}")
    if args.plates > 0:
        cmd.append(f"--plates={args.plates}")
    if args.validation:
        cmd.append(f"--validation={args.validation}")

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
        env=env,
    )

    port = None
    deadline = time.time() + 45.0
    startup_logs: list[str] = []
    while time.time() < deadline:
        line = proc.stdout.readline()
        if not line:
            break
        startup_logs.append(line.rstrip())
        m = re.search(r"ToolServer listening on 127\.0\.0\.1:(\d+)", line)
        if m:
            port = int(m.group(1))
            break

    if port is None:
        out_rest, err_rest = proc.communicate(timeout=15)
        print("\n".join(startup_logs), file=sys.stderr)
        print(out_rest, file=sys.stderr)
        print(err_rest, file=sys.stderr)
        return 1

    t0 = time.time()
    client = JsonRpcClient("127.0.0.1", port)
    checkpoints: list[dict[str, Any]] = []
    sheet_tiles: dict[str, list[tuple[str, str]]] = {l: [] for l in layers}
    current_steps = 0

    try:
        for kv in args.set:
            if "=" not in kv:
                raise RuntimeError(f"Expected key=value in --set: {kv}")
            key, raw = kv.split("=", 1)
            val = json.loads(raw) if raw.lower() in ("true", "false") or raw[0].isdigit() or raw[0] == "-" else raw
            path = key if "." in key else f"planet.shaping.{key}"
            client.set_value(path, val)

        for target_age in ages:
            target_steps = int(round(target_age / args.dt))
            delta_steps = target_steps - current_steps
            while delta_steps > 0:
                chunk = min(50, delta_steps)
                client.call("planet.step", {"Steps": chunk, "Dt": args.dt})
                current_steps += chunk
                delta_steps -= chunk

            tag = f"{int(round(target_age)):04d}"
            telemetry = client.call("planet.telemetry")
            dump = client.call("planet.dump_state", {"Name": f"state_{tag}"})
            healpix = analyze_pghp(dump["Path"])

            captures: dict[str, str] = {}
            if not args.no_images:
                for layer in layers:
                    cap = client.call("planet.capture", {
                        "Downsample": 1,
                        "Mode": layer_map[layer],
                        "Projection": proj_id,
                        "Name": f"{layer}_{args.projection}_{tag}",
                    })
                    captures[layer] = cap["Path"]
                    sheet_tiles[layer].append((cap["Path"], f"{layer} {target_age:.0f} My ({args.projection})"))

            if not args.no_images and target_age == ages[-1]:
                # Also capture equal-area polar caps at final age to rule out polar distortion illusions
                cap_n = client.call("planet.capture", {
                    "Downsample": 1, "Mode": 1, "Projection": proj_map["polar_north"],
                    "Name": f"plates_polar_north_{tag}",
                })
                cap_s = client.call("planet.capture", {
                    "Downsample": 1, "Mode": 1, "Projection": proj_map["polar_south"],
                    "Name": f"plates_polar_south_{tag}",
                })
                captures["polar_north"] = cap_n["Path"]
                captures["polar_south"] = cap_s["Path"]

            combined_verdict = _merge_verdict(telemetry.get("Verdict", "PASS"), healpix["verdict"])
            combined_flags = list(dict.fromkeys(list(telemetry.get("Flags", [])) + list(healpix["flags"])))
            checkpoints.append({
                "age_my": target_age,
                "step": current_steps,
                "verdict": combined_verdict,
                "flags": combined_flags,
                "telemetry": telemetry,
                "healpix": healpix,
                "captures": captures,
            })

        timings = client.call("planet.timings")
        client.call("planet.quit")
    except Exception:
        try:
            client.call("planet.quit")
        except Exception:
            proc.terminate()
        raise
    finally:
        client.close()

    out_rest, err_rest = proc.communicate(timeout=30)
    elapsed = time.time() - t0
    if proc.returncode != 0:
        print(f"PlanetGenLoom exited with {proc.returncode}", file=sys.stderr)
        print(out_rest, file=sys.stderr)
        print(err_rest, file=sys.stderr)
        return 1

    for marker in ("Validation Error", "VUID-", "VUID_NAME"):
        if marker in out_rest or marker in err_rest:
            print(f"Vulkan validation error detected: {marker}", file=sys.stderr)
            print(err_rest, file=sys.stderr)
            return 1

    if not args.no_images:
        for layer, tiles in sheet_tiles.items():
            sheet_path = os.path.join(out_dir, f"sheet_{layer}_{args.projection}.png")
            _build_contact_sheet(tiles, sheet_path)

    overall_verdict = _merge_verdict(*(cp["verdict"] for cp in checkpoints))
    report_path = os.path.join(out_dir, "regression_report.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump({
            "preset": args.preset,
            "order": args.order,
            "dt": args.dt,
            "seed": args.seed,
            "projection": args.projection,
            "elapsed_s": round(elapsed, 2),
            "overall_verdict": overall_verdict,
            "timings": timings,
            "checkpoints": checkpoints,
        }, f, indent=2)

    _print_summary_table(checkpoints, args.preset, args.order, elapsed)
    print(f"\nOverall: {overall_verdict} | Report: {report_path}")
    return 1 if overall_verdict == "FAIL" else 0


if __name__ == "__main__":
    raise SystemExit(main())
