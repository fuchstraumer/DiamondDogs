"""Run a workload and record the timings in the profiler database.

Submit mode runs a benchmark and parses its stdout table.
Present mode captures one frame with Nsight Graphics and replays it.
Present mode stores replay frame times only. It collects no hardware counters.
GPU Trace mode runs the Nsight Graphics GPU Trace activity and stores its scope times and counters.
"""

import argparse
import csv
import hashlib
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

try:
    from .db_store import (
        ingest_gpu_trace,
        ingest_ncu_report,
        init_db,
        insert_pass,
        insert_run,
    )
except ImportError:
    script_dir = Path(__file__).resolve().parent
    if str(script_dir) not in sys.path:
        sys.path.insert(0, str(script_dir))
    from db_store import (
        ingest_gpu_trace,
        ingest_ncu_report,
        init_db,
        insert_pass,
        insert_run,
    )

REPO_DIR = Path(__file__).resolve().parents[2]
GIT_TIMEOUT_S = 30
MANIFEST_NAME = "OceanFft.ldmanifest"
CAPTURE_FRAME_MAX = 600
GPU_TRACE_ACTIVITY = "GPU Trace Profiler"
GPU_TRACE_REGIMES = Path("BASE") / "GPUTRACE_REGIMES.xls"
GPU_TRACE_NO_DATA_TEXT = "Data fetching never started"
GPU_TRACE_HEADLESS_MESSAGE = (
    "[Profiler] Error: ngfx reports 'Data fetching never started'. The target did not present a frame. "
    "A headless app cannot be traced with GPU Trace. Use a windowed app that presents frames."
)

# Columns that ngfx-replay writes to iteration_times.csv. We read them by exact name.
REPLAY_COLUMNS = ("i", "msSubmitTime", "msFinishTime", "msFrameTime", "msGpuTime")


def run_command(cmd: List[str], timeout_s: int, capture: bool) -> Optional[subprocess.CompletedProcess]:
    """Run a subprocess with a timeout.

    Return the completed process, or None when the timeout expires.
    """
    try:
        return subprocess.run(cmd, capture_output=capture, text=capture, timeout=timeout_s)
    except subprocess.TimeoutExpired:
        print(f"[Profiler] Timeout after {timeout_s} s: {' '.join(cmd)}", file=sys.stderr)
        return None


def get_code_state(repo_dir: Path) -> Dict[str, Any]:
    """Read the git state of the code that the run measures.

    The diff hash covers tracked files only. The dirty flag covers untracked files too.
    Return commit, dirty flag (0 or 1) and the first 16 hex digits of sha256 of `git diff HEAD`.
    """
    state: Dict[str, Any] = {"git_commit": "unknown", "git_dirty": None, "git_diff_hash": None}

    try:
        head = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=repo_dir, capture_output=True, text=True,
            check=True, timeout=GIT_TIMEOUT_S,
        )
        state["git_commit"] = head.stdout.strip()

        status = subprocess.run(
            ["git", "status", "--porcelain"], cwd=repo_dir, capture_output=True, text=True,
            check=True, timeout=GIT_TIMEOUT_S,
        )
        state["git_dirty"] = 1 if status.stdout.strip() else 0

        diff = subprocess.run(
            ["git", "diff", "HEAD"], cwd=repo_dir, capture_output=True,
            check=True, timeout=GIT_TIMEOUT_S,
        )
        state["git_diff_hash"] = hashlib.sha256(diff.stdout).hexdigest()[:16]
    except (OSError, subprocess.SubprocessError) as err:
        print(f"[Profiler] Warning: git state is incomplete: {err}", file=sys.stderr)

    return state


def fingerprint_file(path: Path) -> Tuple[Optional[str], Optional[str]]:
    """Hash a file and read its modification time.

    Return the first 16 hex digits of sha256 and the UTC mtime. Both are None when the file is missing.
    """
    if not path.is_file():
        return None, None

    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)

    mtime = datetime.fromtimestamp(path.stat().st_mtime, tz=timezone.utc).isoformat()
    return digest.hexdigest()[:16], mtime


def get_build_fingerprint(exe_path: Path) -> Dict[str, Optional[str]]:
    """Fingerprint the executable and the cooked shader manifest next to it."""
    exe_hash, exe_mtime = fingerprint_file(exe_path)
    manifest_hash, manifest_mtime = fingerprint_file(exe_path.parent / MANIFEST_NAME)
    return {
        "exe_hash": exe_hash,
        "exe_mtime": exe_mtime,
        "shader_manifest_hash": manifest_hash,
        "shader_manifest_mtime": manifest_mtime,
    }


def get_gpu_info() -> Tuple[str, str]:
    """Retrieve GPU model name and driver version through nvidia-smi.

    Return a tuple containing the GPU name and driver version.
    """
    try:
        res = subprocess.run(
            ["nvidia-smi", "--query-gpu=name,driver_version", "--format=csv,noheader"],
            capture_output=True,
            text=True,
            check=True,
            timeout=GIT_TIMEOUT_S,
        )
        line = res.stdout.strip()
        if line:
            parts = [p.strip() for p in line.split(",")]
            gpu_name = parts[0] if len(parts) > 0 else "NVIDIA GPU"
            driver_ver = parts[1] if len(parts) > 1 else "Unknown"
            return gpu_name, driver_ver
    except (OSError, subprocess.SubprocessError):
        print("[Profiler] Warning: nvidia-smi failed. GPU name and driver are unknown.", file=sys.stderr)
    return "NVIDIA GPU", "Unknown"


def locate_nsight_tools() -> Tuple[Path, Path]:
    """Locate Nsight Graphics capture and replay executable tools.

    Return paths to ngfx-capture and ngfx-replay binaries.
    """
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


def parse_benchmark_output(output_text: str) -> Dict[str, Any]:
    """Parse the benchmark stdout table.

    The header line has the words size and radix in its first cell. The cells after the first
    are the scopes. Each row has the configuration values, then "median p95" per scope, in microseconds.
    Return a dictionary with gpu_name, ticks_per_config, warmup_ticks, config_columns, scopes and rows.
    Each row has a config dictionary and a timings dictionary of scope -> median_us and p95_us.
    """
    result: Dict[str, Any] = {
        "gpu_name": None,
        "ticks_per_config": None,
        "warmup_ticks": None,
        "config_columns": [],
        "scopes": [],
        "rows": [],
    }

    for line in output_text.splitlines():
        line_clean = line.strip()
        if not line_clean:
            continue

        if "timestamp period" in line_clean:
            match_gpu = re.search(r"^(.*?),\s*timestamp period", line_clean)
            if match_gpu:
                result["gpu_name"] = match_gpu.group(1).strip()
            match_ticks = re.search(r"(\d+)\s+ticks per configuration", line_clean)
            if match_ticks:
                result["ticks_per_config"] = int(match_ticks.group(1))
            match_warm = re.search(r"(\d+)\s+warm-up ticks", line_clean)
            if match_warm:
                result["warmup_ticks"] = int(match_warm.group(1))
            continue

        if "|" not in line_clean:
            continue

        parts = [p.strip() for p in line_clean.split("|")]
        first_words = parts[0].lower().split()

        if not result["scopes"]:
            if "size" in first_words and "radix" in first_words:
                result["config_columns"] = first_words
                result["scopes"] = parts[1:]
            continue

        cfg_values = parts[0].split()
        if len(cfg_values) != len(result["config_columns"]):
            continue

        config = dict(zip(result["config_columns"], cfg_values))
        timings: Dict[str, Dict[str, float]] = {}
        for idx, col_val in enumerate(parts[1:]):
            if idx >= len(result["scopes"]):
                break
            nums = [float(x) for x in col_val.split() if re.match(r"^-?\d+(\.\d+)?$", x)]
            if len(nums) == 2:
                timings[result["scopes"][idx]] = {"median_us": nums[0], "p95_us": nums[1]}

        if timings:
            result["rows"].append({"config": config, "timings": timings})

    return result


def parse_iteration_times(csv_path: Path) -> List[Dict[str, Any]]:
    """Parse the iteration_times.csv file of ngfx-replay by exact column name.

    Iteration 0 is in the list. It includes first-use overhead, so the callers must skip it in averages.
    msGpuTime is -1 when replay did not collect it. The result holds None then.
    Raise ValueError when a column is missing.
    """
    iterations: List[Dict[str, Any]] = []

    with open(csv_path, "r", encoding="utf-8", errors="replace", newline="") as f:
        reader = csv.reader(f)
        header = [h.strip() for h in next(reader, [])]
        missing = [c for c in REPLAY_COLUMNS if c not in header]
        if missing:
            raise ValueError(f"{csv_path} has no column {missing}. Columns found: {header}")

        index = {name: header.index(name) for name in REPLAY_COLUMNS}
        for cells in reader:
            if len(cells) < len(header):
                continue

            try:
                gpu_ms = float(cells[index["msGpuTime"]])
                iterations.append({
                    "iteration": int(float(cells[index["i"]])),
                    "submit_ms": float(cells[index["msSubmitTime"]]),
                    "finish_ms": float(cells[index["msFinishTime"]]),
                    "frame_ms": float(cells[index["msFrameTime"]]),
                    "gpu_ms": gpu_ms if gpu_ms != -1.0 else None,
                })
            except ValueError:
                continue

    return iterations


def run_benchmark_mode(args: argparse.Namespace) -> int:
    """Execute the benchmark binary and ingest its timing table.

    Return the process exit code.
    """
    exe_path = Path(args.exe).resolve()
    if not exe_path.exists():
        raise FileNotFoundError(f"Executable not found: {exe_path}")

    code_state = get_code_state(REPO_DIR)
    fingerprint = get_build_fingerprint(exe_path)
    gpu_name_smi, driver_version = get_gpu_info()

    cmd = [str(exe_path)]
    if args.args:
        cmd.extend(args.args.split())

    print(f"[Profiler] Launching benchmark: {' '.join(cmd)}")
    result = run_command(cmd, args.timeout_s, capture=True)
    if result is None:
        return 1

    if result.returncode != 0:
        print(f"[Profiler] Benchmark failed with code {result.returncode}:\n{result.stderr}", file=sys.stderr)
        return result.returncode

    print(result.stdout)

    parsed = parse_benchmark_output(result.stdout)
    rows = parsed["rows"]
    if not rows:
        print("[Profiler] Error: no timing table rows parsed from benchmark output.", file=sys.stderr)
        return 1

    conn = init_db(args.db)
    try:
        with conn:
            run_id = insert_run(conn, {
                "timestamp": datetime.now(timezone.utc).isoformat(),
                "binary_name": exe_path.name,
                "binary_path": str(exe_path),
                "command_args": args.args or "",
                "gpu_name": parsed["gpu_name"] or gpu_name_smi,
                "driver_version": driver_version,
                "frame_count": None,
                "avg_frame_time_ms": None,
                "avg_fps": None,
                "mode": "submit",
                "ticks_per_config": parsed["ticks_per_config"],
                "warmup_ticks": parsed["warmup_ticks"],
                **code_state,
                **fingerprint,
            })

            for row in rows:
                config = row["config"]
                for scope_name, timing in row["timings"].items():
                    insert_pass(conn, {
                        "run_id": run_id,
                        "pass_name": scope_name,
                        "queue_family": "Compute",
                        "duration_ns": timing["median_us"] * 1000.0,
                        "p95_ns": timing["p95_us"] * 1000.0,
                        "config_size": int(config["size"]),
                        "config_radix": int(config["radix"]),
                        "config_wave": config.get("wave"),
                        "config_path": config.get("path"),
                    })
    finally:
        conn.close()

    print(f"[Profiler] Ingested run {run_id}: {len(rows)} configurations, {len(parsed['scopes'])} scopes each.")
    return 0


def find_capture_file(cap_dir: Path, base_name: str) -> Optional[Path]:
    """Find the capture file that ngfx-capture wrote.

    ngfx-capture appends .ngfx-capture to the -o name. Return the newest non-empty file, or None.
    """
    matches = [p for p in cap_dir.glob(base_name + "*") if p.is_file() and p.stat().st_size > 0]
    if not matches:
        return None
    return max(matches, key=lambda p: p.stat().st_mtime)


def run_present_mode(args: argparse.Namespace) -> int:
    """Capture one frame with Nsight Graphics, replay it, and store the replay frame times.

    The replay reports whole-frame CPU times (msFrameTime) and GPU time (msGpuTime) when it has it.
    Iteration 0 includes first-use overhead. We do not store it and we skip it in the averages.
    Return the process exit code.
    """
    exe_path = Path(args.exe).resolve()
    if not exe_path.exists():
        raise FileNotFoundError(f"Executable not found: {exe_path}")

    capture_exe, replay_exe = locate_nsight_tools()
    print(f"[Profiler] Found Nsight tools:\n  Capture: {capture_exe}\n  Replay:  {replay_exe}")

    code_state = get_code_state(REPO_DIR)
    fingerprint = get_build_fingerprint(exe_path)
    gpu_name, driver_version = get_gpu_info()

    cap_dir = Path(args.capture_dir).resolve()
    cap_dir.mkdir(parents=True, exist_ok=True)
    base_name = f"{exe_path.stem}_{datetime.now().strftime('%Y%m%d_%H%M%S')}"

    capture_cmd = [
        str(capture_exe),
        "-e", str(exe_path),
        "--wd", str(exe_path.parent),
        "-o", str(cap_dir / base_name),
        "-n", str(args.frames),
        "--capture-frame", str(args.warmup),
        "--no-hud",
        "--terminate-after-capture",
        "--bundle-replayer",
    ]
    if args.args:
        capture_cmd.extend(["--args", args.args])

    print(f"[Profiler] Launching ngfx-capture: {' '.join(capture_cmd)}")
    cap_res = run_command(capture_cmd, args.timeout_s, capture=False)
    if cap_res is None:
        return 1

    if cap_res.returncode != 0:
        print(f"[Profiler] ngfx-capture failed with code {cap_res.returncode}", file=sys.stderr)
        return cap_res.returncode

    # ngfx-capture exits 0 even when it fails, so we look for the file.
    cap_file = find_capture_file(cap_dir, base_name)
    if cap_file is None:
        print(f"[Profiler] Error: ngfx-capture exited 0 but wrote no capture file '{base_name}*' in {cap_dir}.",
              file=sys.stderr)
        return 1

    print(f"[Profiler] Capture file: {cap_file} ({cap_file.stat().st_size / 1e6:.1f} MB)")

    report_dir = cap_dir / f"{base_name}_report"
    report_dir.mkdir(parents=True, exist_ok=True)

    replay_cmd = [
        str(replay_exe),
        "-n", str(args.replay_loops),
        "--perf-report-dir", str(report_dir),
        "--present-hidden",
        "--bundle-replayer",
        "--no-block-on-incompatibility",
        str(cap_file),
    ]

    print(f"[Profiler] Launching ngfx-replay: {' '.join(replay_cmd)}")
    rep_res = run_command(replay_cmd, args.timeout_s, capture=False)
    if rep_res is None:
        return 1

    if rep_res.returncode != 0:
        print(f"[Profiler] ngfx-replay failed with code {rep_res.returncode}", file=sys.stderr)
        return rep_res.returncode

    csv_candidates = sorted(report_dir.glob("**/iteration_times.csv"), key=lambda p: p.stat().st_mtime)
    if not csv_candidates:
        print(f"[Profiler] Error: iteration_times.csv not found in {report_dir}", file=sys.stderr)
        return 1

    csv_path = csv_candidates[-1]
    print(f"[Profiler] Parsing report CSV: {csv_path}")

    try:
        iterations = parse_iteration_times(csv_path)
    except ValueError as err:
        print(f"[Profiler] Error: {err}", file=sys.stderr)
        return 1

    used = [it for it in iterations if it["iteration"] > 0]
    if not used:
        print(f"[Profiler] Error: {csv_path} has no usable rows after iteration 0.", file=sys.stderr)
        return 1

    avg_frame_time_ms = sum(it["frame_ms"] for it in used) / len(used)
    avg_fps = (1000.0 / avg_frame_time_ms) if avg_frame_time_ms > 0.0 else None
    gpu_times = [it["gpu_ms"] for it in used if it["gpu_ms"] is not None]

    conn = init_db(args.db)
    try:
        with conn:
            run_id = insert_run(conn, {
                "timestamp": datetime.now(timezone.utc).isoformat(),
                "binary_name": exe_path.name,
                "binary_path": str(exe_path),
                "command_args": (
                    f"{args.args or ''} [capture frames={args.frames} frame={args.warmup}, "
                    f"replay loops={args.replay_loops}]"
                ).strip(),
                "gpu_name": gpu_name,
                "driver_version": driver_version,
                "frame_count": len(used),
                "avg_frame_time_ms": avg_frame_time_ms,
                "avg_fps": avg_fps,
                "mode": "present",
                "artifact_path": str(cap_file),
                **code_state,
                **fingerprint,
            })

            for it in used:
                insert_pass(conn, {
                    "run_id": run_id,
                    "pass_name": "ReplayFrameCpu",
                    "duration_ns": it["frame_ms"] * 1_000_000.0,
                    "iteration": it["iteration"],
                    "cpu_submit_ms": it["submit_ms"],
                    "cpu_finish_ms": it["finish_ms"],
                })
                if it["gpu_ms"] is not None:
                    insert_pass(conn, {
                        "run_id": run_id,
                        "pass_name": "ReplayFrameGpu",
                        "duration_ns": it["gpu_ms"] * 1_000_000.0,
                        "iteration": it["iteration"],
                    })
    finally:
        conn.close()

    gpu_text = f"{sum(gpu_times) / len(gpu_times):.4f} ms" if gpu_times else "not collected"
    print(f"[Profiler] Ingested run {run_id}: {len(used)} replay iterations (iteration 0 skipped), "
          f"mean CPU frame time {avg_frame_time_ms:.4f} ms, GPU time {gpu_text}.")
    return 0


def locate_ngfx() -> Path:
    """Locate ngfx.exe, the Nsight Graphics command-line front end.

    Return the path to ngfx.exe.
    """
    primary_exe = Path(
        r"C:\Program Files\NVIDIA Corporation\Nsight Graphics 2026.3.1\host\windows-desktop-nomad-x64\ngfx.exe"
    )
    if primary_exe.exists():
        return primary_exe

    base_dir = Path(r"C:\Program Files\NVIDIA Corporation")
    if base_dir.exists():
        for candidate in base_dir.glob("**/ngfx.exe"):
            return candidate

    raise FileNotFoundError("Could not locate ngfx.exe in the Nsight Graphics directory.")


def require_regimes_file(out_dir: Path) -> Path:
    """Return the regimes file of a GPU Trace export folder.

    Raise FileNotFoundError when the folder has no regimes file.
    """
    regimes_path = out_dir / GPU_TRACE_REGIMES
    if not regimes_path.is_file():
        raise FileNotFoundError(
            f"No {GPU_TRACE_REGIMES} in {out_dir}. The GPU Trace export is incomplete or the trace failed."
        )
    return regimes_path


def run_gputrace_mode(args: argparse.Namespace) -> int:
    """Run the Nsight Graphics GPU Trace activity on the app, then ingest the export.

    The app must present frames. The trace uses locked base GPU clocks, so its times are not
    comparable with submit mode times. Return the process exit code.
    """
    exe_path = Path(args.exe).resolve()
    if not exe_path.exists():
        raise FileNotFoundError(f"Executable not found: {exe_path}")

    ngfx_exe = locate_ngfx()
    print(f"[Profiler] Found Nsight Graphics: {ngfx_exe}")

    provenance = {**get_code_state(REPO_DIR), **get_build_fingerprint(exe_path)}

    cap_dir = Path(args.capture_dir).resolve()
    out_dir = cap_dir / f"{exe_path.stem}_gputrace_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
    out_dir.mkdir(parents=True, exist_ok=True)

    trace_cmd = [
        str(ngfx_exe),
        "--activity", GPU_TRACE_ACTIVITY,
        "--exe", str(exe_path),
        "--args", args.trace_args,
        "--dir", str(exe_path.parent),
        "--output-dir", str(out_dir),
        "--start-after-frames", str(args.trace_start_frame),
        "--limit-to-frames", "1",
        "--auto-export",
        # ngfx ignores the per-architecture options below unless --architecture names the GPU family
        "--architecture", args.trace_architecture,
        "--multi-pass-metrics",
        "--real-time-shader-profiler",
    ]

    print(f"[Profiler] Launching ngfx: {' '.join(trace_cmd)}")
    trace_res = run_command(trace_cmd, args.timeout_s, capture=True)
    if trace_res is None:
        return 1

    output_text = (trace_res.stdout or "") + (trace_res.stderr or "")
    if GPU_TRACE_NO_DATA_TEXT in output_text:
        print(GPU_TRACE_HEADLESS_MESSAGE, file=sys.stderr)
        return 1

    if trace_res.returncode != 0:
        print(f"[Profiler] ngfx failed with code {trace_res.returncode}:\n{output_text}", file=sys.stderr)
        return trace_res.returncode

    try:
        require_regimes_file(out_dir)
    except FileNotFoundError as err:
        print(f"[Profiler] Error: {err}", file=sys.stderr)
        return 1

    conn = init_db(args.db)
    try:
        run_id = ingest_gpu_trace(conn, out_dir, exe_path, args.trace_args, provenance)
    except (OSError, ValueError) as err:
        print(f"[Profiler] Error: {err}", file=sys.stderr)
        return 1
    finally:
        conn.close()

    print(f"[Profiler] Ingested run {run_id} from {out_dir}.")
    return 0


def run_gputrace_ingest(args: argparse.Namespace) -> int:
    """Ingest an existing GPU Trace export folder without running anything.

    The run gets no code state and no build fingerprint, because the folder does not record them.
    Return the process exit code.
    """
    out_dir = Path(args.gputrace_dir).resolve()
    try:
        require_regimes_file(out_dir)
    except FileNotFoundError as err:
        print(f"[Profiler] Error: {err}", file=sys.stderr)
        return 1

    conn = init_db(args.db)
    try:
        run_id = ingest_gpu_trace(conn, out_dir, None, "", None)
    except (OSError, ValueError) as err:
        print(f"[Profiler] Error: {err}", file=sys.stderr)
        return 1
    finally:
        conn.close()

    print(f"[Profiler] Ingested GPU Trace export '{out_dir}' as run {run_id}.")
    return 0


def capture_frame_arg(text: str) -> int:
    """Argparse type: a 1-based capture frame that is greater than 1."""
    value = int(text)
    if value <= 1:
        raise argparse.ArgumentTypeError("must be greater than 1 (ngfx-capture rejects frame 1)")
    return value


def frames_arg(text: str) -> int:
    """Argparse type: a frame count from 1 to 600."""
    value = int(text)
    if not 1 <= value <= CAPTURE_FRAME_MAX:
        raise argparse.ArgumentTypeError(f"must be from 1 to {CAPTURE_FRAME_MAX}")
    return value


def replay_loops_arg(text: str) -> int:
    """Argparse type: a replay loop count of at least 2. Iteration 0 is skipped, so 1 leaves no data."""
    value = int(text)
    if value < 2:
        raise argparse.ArgumentTypeError("must be at least 2 (iteration 0 is skipped)")
    return value


def timeout_arg(text: str) -> int:
    """Argparse type: a positive timeout in seconds."""
    value = int(text)
    if value <= 0:
        raise argparse.ArgumentTypeError("must be greater than 0")
    return value


def main() -> int:
    """Parse command-line arguments and run the selected profiler workflow.

    Return the exit status code.
    """
    parser = argparse.ArgumentParser(description="Benchmark and Nsight replay profiler runner")
    parser.add_argument("--exe", default=None, help="Path to executable binary")
    parser.add_argument("--ncu-rep", default=None, help="Path to an .ncu-rep file to import directly")
    parser.add_argument("--args", default="", help="Command-line arguments to pass to the binary")
    parser.add_argument("--frames", type=frames_arg, default=1,
                        help="Present mode: number of frames IN the capture, 1 to 600 (default: 1)")
    parser.add_argument("--warmup", type=capture_frame_arg, default=5,
                        help="Present mode: the 1-based frame to capture, greater than 1 (default: 5)")
    parser.add_argument("--replay-loops", type=replay_loops_arg, default=10,
                        help="Present mode: replay iterations, at least 2 (default: 10)")
    parser.add_argument("--capture-dir", default="build/profiler_captures",
                        help="Present mode: folder for capture files and replay reports. GPU Trace mode: "
                             "folder that gets one new subfolder per run "
                             "(default: build/profiler_captures under the current directory)")
    parser.add_argument("--trace-args", default="--frames=1500 --validation=off",
                        help="GPU Trace mode: command-line arguments of the app "
                             "(default: --frames=1500 --validation=off)")
    parser.add_argument("--trace-start-frame", type=int, default=120,
                        help="GPU Trace mode: the frame after which the trace starts (default: 120)")
    parser.add_argument("--trace-architecture", default="Ada",
                        help="GPU Trace mode: the GPU family for the metric options, as ngfx.exe --help-all "
                             "lists them, for example Ada or Blackwell GB20x (default: Ada, the RTX 4080)")
    parser.add_argument("--gputrace-dir", default=None,
                        help="Import an existing GPU Trace export folder (the folder that holds BASE) "
                             "without running anything")
    parser.add_argument("--timeout-s", type=timeout_arg, default=600,
                        help="Timeout in seconds for each subprocess (default: 600)")
    parser.add_argument("--db", default="profiler.db", help="Path to SQLite database (default: profiler.db)")
    parser.add_argument(
        "--mode",
        choices=["auto", "present", "submit", "gputrace"],
        default="auto",
        help="Profiling mode: auto, present (Nsight Graphics capture and replay), submit (benchmark run), "
             "or gputrace (Nsight Graphics GPU Trace of a presenting app)",
    )

    args = parser.parse_args()

    if args.ncu_rep:
        conn = init_db(args.db)
        try:
            run_id = ingest_ncu_report(conn, args.ncu_rep)
            print(f"[Profiler] Ingested NCU report '{args.ncu_rep}' as run {run_id}.")
            return 0
        except RuntimeError as err:
            print(f"[Profiler] Error: {err}", file=sys.stderr)
            return 1
        finally:
            conn.close()

    if args.gputrace_dir:
        return run_gputrace_ingest(args)

    if not args.exe:
        parser.error("argument --exe is required when not importing an .ncu-rep file or a GPU Trace folder")

    mode = args.mode
    if mode == "auto":
        mode = "submit" if "benchmark" in Path(args.exe).name.lower() else "present"

    if mode == "submit":
        return run_benchmark_mode(args)
    if mode == "gputrace":
        return run_gputrace_mode(args)
    return run_present_mode(args)


if __name__ == "__main__":
    sys.exit(main())
