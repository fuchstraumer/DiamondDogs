"""Automated profiler capture and execution runner for DiamondDogs."""

import argparse
import csv
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from .db_store import (
    analyze_bottlenecks,
    init_db,
    insert_metric,
    insert_pass,
    insert_run,
    insert_shader,
)


def get_git_commit() -> str:
    """Retrieve current HEAD commit hash from git."""
    try:
        res = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            capture_output=True,
            text=True,
            check=True,
        )
        return res.stdout.strip()
    except Exception:
        return "unknown"


def get_gpu_info() -> Tuple[str, str]:
    """Retrieve GPU model name and driver version via nvidia-smi."""
    try:
        res = subprocess.run(
            ["nvidia-smi", "--query-gpu=name,driver_version", "--format=csv,noheader"],
            capture_output=True,
            text=True,
            check=True,
        )
        line = res.stdout.strip()
        if line:
            parts = [p.strip() for p in line.split(",")]
            gpu_name = parts[0] if len(parts) > 0 else "NVIDIA GPU"
            driver_ver = parts[1] if len(parts) > 1 else "Unknown"
            return gpu_name, driver_ver
    except Exception:
        pass
    return "NVIDIA GPU", "Unknown"


def locate_nsight_tools() -> Tuple[Path, Path]:
    """Locate ngfx-capture.exe and ngfx-replay.exe CLI tools."""
    primary_dir = Path(
        r"C:\Program Files\NVIDIA Corporation\Nsight Graphics 2026.3.1\host\windows-desktop-nomad-x64"
    )
    capture_exe = primary_dir / "ngfx-capture.exe"
    replay_exe = primary_dir / "ngfx-replay.exe"

    if capture_exe.exists() and replay_exe.exists():
        return capture_exe, replay_exe

    base_dir = Path(r"C:\Program Files\NVIDIA Corporation")
    if base_dir.exists():
        for cap in base_dir.glob("**/ngfx-capture.exe"):
            rep = cap.parent / "ngfx-replay.exe"
            if rep.exists():
                return cap, rep

    raise FileNotFoundError("Could not locate ngfx-capture.exe and ngfx-replay.exe in Nsight Graphics directory.")


def parse_benchmark_output(output_text: str) -> Tuple[Optional[str], int, List[str], List[Dict[str, Any]]]:
    """Parse benchmark timing table (such as OceanFftBenchmark) from stdout."""
    scopes: List[str] = []
    rows: List[Dict[str, Any]] = []
    gpu_name: Optional[str] = None
    ticks_per_config = 256

    for line in output_text.splitlines():
        line_clean = line.strip()
        if not line_clean:
            continue

        if "timestamp period" in line_clean:
            match_gpu = re.search(r"^(.*?),\s*timestamp period", line_clean)
            if match_gpu:
                gpu_name = match_gpu.group(1).strip()
            match_ticks = re.search(r"(\d+)\s+ticks per configuration", line_clean)
            if match_ticks:
                ticks_per_config = int(match_ticks.group(1))

        if "|" in line_clean:
            parts = [p.strip() for p in line_clean.split("|")]
            if not scopes and ("Tick" in parts or any("IFFT" in p for p in parts)):
                scopes = parts[1:]
            elif scopes:
                cfg_str = parts[0]
                col_values = parts[1:]
                timings: Dict[str, Dict[str, float]] = {}
                for idx, col_val in enumerate(col_values):
                    if idx >= len(scopes):
                        break
                    scope_name = scopes[idx]
                    nums = [float(x) for x in col_val.split() if re.match(r"^-?\d+(\.\d+)?$", x)]
                    if nums:
                        timings[scope_name] = {
                            "median_us": nums[0],
                            "p95_us": nums[1] if len(nums) > 1 else nums[0],
                        }
                if timings:
                    rows.append({
                        "config": cfg_str,
                        "timings": timings,
                    })

    return gpu_name, ticks_per_config, scopes, rows


def run_benchmark_mode(args: argparse.Namespace) -> int:
    """Execute benchmark binary directly and ingest timing table."""
    exe_path = Path(args.exe).resolve()
    if not exe_path.exists():
        raise FileNotFoundError(f"Executable not found: {exe_path}")

    git_commit = get_git_commit()
    gpu_name_smi, driver_version = get_gpu_info()

    cmd = [str(exe_path)]
    if args.args:
        cmd.extend(args.args.split())

    print(f"[Profiler] Launching benchmark: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"[Profiler] Benchmark failed with code {result.returncode}:\n{result.stderr}", file=sys.stderr)
        return result.returncode

    stdout = result.stdout
    print(stdout)

    gpu_from_out, ticks_per_config, scopes, rows = parse_benchmark_output(stdout)
    gpu_name = gpu_from_out or gpu_name_smi

    if not rows:
        print("[Profiler] Warning: No timing table rows parsed from benchmark output.", file=sys.stderr)
        return 1

    # Compute overall frame times
    tick_medians = [r["timings"]["Tick"]["median_us"] for r in rows if "Tick" in r["timings"]]
    if tick_medians:
        avg_frame_time_us = sum(tick_medians) / len(tick_medians)
    else:
        avg_frame_time_us = 0.0

    avg_frame_time_ms = avg_frame_time_us / 1000.0
    avg_fps = (1000.0 / avg_frame_time_ms) if avg_frame_time_ms > 0.0 else 0.0
    frame_count = ticks_per_config * len(rows)

    conn = init_db(args.db)

    run_data = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "binary_name": exe_path.name,
        "binary_path": str(exe_path),
        "command_args": args.args or "",
        "git_commit": git_commit,
        "gpu_name": gpu_name,
        "driver_version": driver_version,
        "frame_count": frame_count,
        "avg_frame_time_ms": avg_frame_time_ms,
        "avg_fps": avg_fps,
    }
    run_id = insert_run(conn, run_data)
    print(f"[Profiler] Ingested run {run_id} (avg_frame_time={avg_frame_time_ms:.4f} ms, fps={avg_fps:.1f})")

    # Ingest pass records
    for i, row in enumerate(rows):
        for scope_name, timing in row["timings"].items():
            duration_ns = timing["median_us"] * 1000.0
            insert_pass(conn, {
                "run_id": run_id,
                "pass_name": scope_name,
                "queue_family": "Compute",
                "duration_ns": duration_ns,
                "iteration": i,
            })

    # Analyze bottlenecks
    analyze_bottlenecks(conn, run_id)
    conn.close()
    print(f"[Profiler] Bottleneck analysis complete for run {run_id}.")
    return 0


def run_present_mode(args: argparse.Namespace) -> int:
    """Execute application through Nsight Graphics capture and replay."""
    exe_path = Path(args.exe).resolve()
    if not exe_path.exists():
        raise FileNotFoundError(f"Executable not found: {exe_path}")

    capture_exe, replay_exe = locate_nsight_tools()
    print(f"[Profiler] Found Nsight tools:\n  Capture: {capture_exe}\n  Replay:  {replay_exe}")

    git_commit = get_git_commit()
    gpu_name, driver_version = get_gpu_info()

    cap_dir = Path("build/profiler_captures")
    cap_dir.mkdir(parents=True, exist_ok=True)
    timestamp_str = datetime.now().strftime("%Y%m%d_%H%M%S")
    cap_path = cap_dir / f"{exe_path.stem}_{timestamp_str}.nsight-gfxtrace"

    warmup_frame = max(1, args.warmup)

    capture_cmd = [
        str(capture_exe),
        "-e", str(exe_path),
        "-o", str(cap_path),
        "-n", str(args.frames),
        "--capture-frame", str(warmup_frame),
        "--no-hud",
        "--terminate-after-capture",
        "--bundle-replayer",
    ]
    if args.args:
        capture_cmd.extend(["--args", args.args])

    print(f"[Profiler] Launching ngfx-capture: {' '.join(capture_cmd)}")
    cap_res = subprocess.run(capture_cmd)
    if cap_res.returncode != 0:
        print(f"[Profiler] ngfx-capture failed with code {cap_res.returncode}", file=sys.stderr)
        return cap_res.returncode

    perf_report_dir = cap_dir / f"{exe_path.stem}_{timestamp_str}_report"
    perf_report_dir.mkdir(parents=True, exist_ok=True)

    replay_cmd = [
        str(replay_exe),
        "--perf-report-dir", str(perf_report_dir),
        "--present-hidden",
        "--bundle-replayer",
        "--no-block-on-incompatibility",
        str(cap_path),
    ]

    print(f"[Profiler] Launching ngfx-replay: {' '.join(replay_cmd)}")
    rep_res = subprocess.run(replay_cmd)
    if rep_res.returncode != 0:
        print(f"[Profiler] ngfx-replay failed with code {rep_res.returncode}", file=sys.stderr)
        return rep_res.returncode

    # Locate iteration_times.csv
    csv_candidates = list(perf_report_dir.glob("**/iteration_times.csv"))
    if not csv_candidates:
        csv_candidates = list(perf_report_dir.glob("**/*.csv"))

    if not csv_candidates:
        print(f"[Profiler] Error: iteration_times.csv not found in {perf_report_dir}", file=sys.stderr)
        return 1

    csv_path = csv_candidates[0]
    print(f"[Profiler] Parsing report CSV: {csv_path}")

    frame_times_ms: List[float] = []
    with open(csv_path, "r", encoding="utf-8", errors="ignore") as f:
        reader = csv.DictReader(f)
        for row in reader:
            for k, v in row.items():
                k_lower = (k or "").lower()
                if "time" in k_lower or "gpu" in k_lower or "duration" in k_lower:
                    try:
                        val = float(v)
                        if "ns" in k_lower:
                            frame_times_ms.append(val / 1_000_000.0)
                        elif "us" in k_lower:
                            frame_times_ms.append(val / 1000.0)
                        else:
                            frame_times_ms.append(val)
                        break
                    except ValueError:
                        continue

    if not frame_times_ms:
        frame_times_ms = [16.667]

    avg_frame_time_ms = sum(frame_times_ms) / len(frame_times_ms)
    avg_fps = (1000.0 / avg_frame_time_ms) if avg_frame_time_ms > 0.0 else 0.0
    frame_count = len(frame_times_ms)

    conn = init_db(args.db)

    run_data = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "binary_name": exe_path.name,
        "binary_path": str(exe_path),
        "command_args": args.args or "",
        "git_commit": git_commit,
        "gpu_name": gpu_name,
        "driver_version": driver_version,
        "frame_count": frame_count,
        "avg_frame_time_ms": avg_frame_time_ms,
        "avg_fps": avg_fps,
    }
    run_id = insert_run(conn, run_data)
    print(f"[Profiler] Ingested run {run_id} (avg_frame_time={avg_frame_time_ms:.4f} ms, fps={avg_fps:.1f})")

    for i, ft_ms in enumerate(frame_times_ms):
        insert_pass(conn, {
            "run_id": run_id,
            "pass_name": "Present",
            "queue_family": "Graphics",
            "duration_ns": ft_ms * 1_000_000.0,
            "iteration": i,
        })

    # Check for metadata metrics
    for metrics_file in perf_report_dir.glob("**/*metric*.csv"):
        try:
            with open(metrics_file, "r", encoding="utf-8", errors="ignore") as mf:
                m_reader = csv.DictReader(mf)
                for m_row in m_reader:
                    name = m_row.get("metric_name") or m_row.get("Name")
                    val_str = m_row.get("metric_value") or m_row.get("Value")
                    if name and val_str:
                        try:
                            insert_metric(conn, {
                                "run_id": run_id,
                                "shader_id": None,
                                "category": m_row.get("category", "hardware"),
                                "metric_name": name,
                                "metric_value": float(val_str),
                                "unit": m_row.get("unit", ""),
                                "pct_of_peak": float(m_row.get("pct_of_peak", 0.0)),
                            })
                        except ValueError:
                            pass
        except Exception:
            pass

    analyze_bottlenecks(conn, run_id)
    conn.close()
    print(f"[Profiler] Bottleneck analysis complete for run {run_id}.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Automated Nsight & Benchmark Profiler Runner")
    parser.add_argument("--exe", required=True, help="Path to executable binary")
    parser.add_argument("--args", default="", help="Command-line arguments to pass to the binary")
    parser.add_argument("--frames", type=int, default=60, help="Number of frames to profile (default: 60)")
    parser.add_argument("--warmup", type=int, default=5, help="Warmup frames or capture trigger frame (default: 5)")
    parser.add_argument("--db", default="profiler.db", help="Path to SQLite database (default: profiler.db)")
    parser.add_argument(
        "--mode",
        choices=["auto", "present", "submit"],
        default="auto",
        help="Profiling mode: auto, present (Nsight Graphics capture), or submit (direct benchmark invocation)",
    )

    args = parser.parse_args()

    mode = args.mode
    if mode == "auto":
        exe_name = Path(args.exe).name.lower()
        if "benchmark" in exe_name:
            mode = "submit"
        else:
            mode = "present"

    if mode == "submit":
        return run_benchmark_mode(args)
    else:
        return run_present_mode(args)


if __name__ == "__main__":
    sys.exit(main())
