# DiamondDogs GPU Profiler Scripts

## Overview
These scripts run a workload and store its timings in a SQLite database.
They also record which code was measured.
You can then compare two runs.

## What these scripts do not do
- They collect no hardware counters on this machine yet. The `metrics` table has 0 rows.
- Nsight Compute (NCU) is not installed on this machine.
  NCU profiles CUDA kernels. It does not profile Vulkan dispatches.
  The `--ncu-rep` path can read an `.ncu-rep` file, but this engine cannot make one.
- Present mode stores replay frame times only. They are whole-frame times, not render pass times.
- GPU Trace stores scope times and counters. It does not store stall reasons or per-line shader data.
  Those stay in the `.ngfx-gputrace` file, which you open in the Nsight Graphics UI.
- The scripts give no advice. They do not diagnose bottlenecks.
  The `bottlenecks` table holds NCU rule results only.

## Run the tests
Run this command from the repository root:
```powershell
python -m unittest discover -s scripts/profiler -p "test_*.py" -v
```
The tests that need the real `ncu_report` module skip when it is missing.

## Modes

### Submit mode (`--mode submit`)
Submit mode runs a benchmark executable and parses its stdout table.
The header line of the table has the words `size` and `radix`.
The parser reads the configuration columns (size, radix, wave, path) from this header.
Each row gives the median and the p95 of each scope, in microseconds.
The runner stores one `passes` row for each configuration and scope.
It stores the median in `duration_ns` and the p95 in `p95_ns`.

The runner stores no average frame time in this mode.
An average across sizes 128 to 1024 has no meaning.
`avg_frame_time_ms`, `avg_fps` and `frame_count` are NULL.
The runner stores `ticks_per_config` and `warmup_ticks` as the benchmark prints them.

### Present mode (`--mode present`)
Present mode captures one frame with `ngfx-capture`, then replays it with `ngfx-replay`.
- `--frames` is the number of frames in the capture (1 to 600, default 1).
- `--warmup` is the 1-based frame to capture. It must be greater than 1.
- `--replay-loops` is the number of replay iterations (at least 2, default 10).
- `--capture-dir` is the folder for the capture and the report.
  The default is `build/profiler_captures` under the current directory. The runner makes it absolute.
- `--timeout-s` is the timeout of each subprocess (default 600). A timeout is a failure.

`ngfx-capture` appends `.ngfx-capture` to the output name, and it returns 0 even when it fails.
The runner looks for the file on disk. It fails when no capture file exists.
One captured OceanFft frame is about 250 MB. Delete the capture files when you finish.

The replay writes `iteration_times.csv`. The runner reads it by column name.
It stores these values for each iteration:
- `ReplayFrameCpu`: `msFrameTime` (CPU time, submit plus finish) in `duration_ns`.
  `cpu_submit_ms` and `cpu_finish_ms` hold the two parts.
- `ReplayFrameGpu`: `msGpuTime`, only when it is not -1. The replay often does not collect it.

Iteration 0 includes first-use overhead. The runner does not store it and skips it in the averages.
`avg_frame_time_ms` is the mean `msFrameTime` of the other iterations. It is a CPU time.

### GPU Trace mode (`--mode gputrace`)
GPU Trace mode runs the Nsight Graphics GPU Trace activity on the app.
It stores the GPU time of each debug-label scope and the counters of one frame.

The target must present frames. The runner starts the trace after frame 120 and records one frame.
A headless app, such as OceanFftBenchmark, never finishes a trace. The runner then reports
"Data fetching never started".

- `--trace-args` sets the app arguments (default `--frames=1500 --validation=off`).
- `--trace-start-frame` sets the frame that starts the trace (default 120).
- `--capture-dir` is the folder for the runs. Each run gets one new subfolder.
  The subfolder holds the `.ngfx-gputrace` file and a `BASE` folder of exports.
- `--timeout-s` applies to the ngfx process.
- `--gputrace-dir <folder>` ingests an existing export folder. It runs nothing.
  It stores no code state and no build fingerprint, because the folder does not record them.

The run stores:
- One `runs` row. Its `mode` is `gputrace`. It holds the frame time, the GPU name, the driver and `gpu_clocks`.
- One `passes` row for each scope. `pass_name` is the scope path, such as `Ocean simulation/IFFT`.
  The path joins the nested scope names with `/`. `queue_family` is `Graphics`.
- One `metrics` row for each counter of each scope. `pass_id` links it to its scope.
  Whole-frame counters have `pass_id` NULL.
- Each counter name is normalized. The unit instance and the metric set are dropped, so
  `SM_A.TriageAC.l1tex__throughput.avg.pct_of_peak_sustained_elapsed` becomes
  `l1tex__throughput.avg.pct_of_peak_sustained_elapsed`. `raw_name` keeps the full name.

The GPU clocks are locked to base during the trace. Its times are not comparable with submit mode times.

The validation layer changes GPU times. Mips measured 0.87 ms with it on and 0.18 ms with it off.
The runner prints a warning when the app arguments do not contain `--validation=off`.

Use `query_metrics --scopes` for one row per scope. It shows the time and six counters as percent of peak:
DRAM, LTS, SM, L1tex, shared-memory LSU wavefronts and active warps in compute shaders.

### NCU report import (`--ncu-rep`)
This mode reads an `.ncu-rep` file with the `ncu_report` Python module.
The runner looks for the module on `sys.path`.
If the import fails, it searches `C:\Program Files\NVIDIA Corporation\Nsight Compute*\extras\python`.
The import runs in one transaction. It prints the number of metrics that it skipped.
- Metric `category` is the text before the first `__` in the metric name (for example `sm`, `dram`, `smsp`).
  A name without `__` has the category `general`.
- `block_size` holds `launch__block_size`. `subgroup_size` stays NULL.
- Each NCU rule becomes a `bottlenecks` row with category `ncu_rule`.
  The severity is the type or severity that the rule gives, or `info`. The recommendation is NULL.
- The run has no frame time, no git commit and no GPU name. The importer cannot know them.

## Which code was measured
Each run in submit mode and present mode stores:
- `git_commit`, `git_dirty` (1 when `git status --porcelain` is not empty), and `git_diff_hash`
  (the first 16 hex digits of the sha256 of `git diff HEAD`).
  The diff hash covers tracked files only. Untracked files set the dirty flag but not the hash.
- `exe_hash` and `exe_mtime` of the executable.
- `shader_manifest_hash` and `shader_manifest_mtime` of `OceanFft.ldmanifest` in the executable folder.
  They are NULL when the file does not exist.

## Database schema
`PRAGMA user_version` holds the schema version (now 4).
On open, `init_db` creates missing tables and adds missing columns with `ALTER TABLE`.
Old rows keep NULL in the new columns. A version 2 database gets `gpu_clocks`, `pass_id` and `raw_name`.
The `pass_id` column of an old database has no foreign key constraint. A new database has one.
Each ingest is one transaction. The insert helpers do not commit.

### `runs`
- `run_id`, `timestamp` (UTC ISO 8601), `binary_name`, `binary_path`, `command_args`.
- `mode`: `submit`, `present`, `ncu` or `gputrace`.
- `gpu_clocks`: the GPU clock setting of a GPU Trace run, for example `Locked to Base`.
- `git_commit`, `git_dirty`, `git_diff_hash`.
- `exe_hash`, `exe_mtime`, `shader_manifest_hash`, `shader_manifest_mtime`.
- `gpu_name`, `driver_version`.
- `frame_count`: the replay iterations used in the average. NULL in submit mode.
- `avg_frame_time_ms`, `avg_fps`: NULL in submit mode and in NCU import.
- `ticks_per_config`, `warmup_ticks`: from the benchmark header.
- `artifact_path`: the capture file or the NCU report.

### `passes`
- `pass_id`, `run_id`, `pass_name`, `queue_family`, `iteration`.
- `duration_ns`: the median (submit), the CPU or GPU frame time (present), the kernel duration (NCU),
  or the GPU time of the scope (GPU Trace).
- `p95_ns`: the p95 (submit only).
- `config_size`, `config_radix`, `config_wave`, `config_path`: the benchmark configuration. `config_wave` is text, `off` or a number. `config_path` is `fused` or `unfused`, and NULL for a run from before schema 4.
- `cpu_submit_ms`, `cpu_finish_ms`: the replay parts of `msFrameTime`.

A pass is identified by the configuration and the pass name together.

### `shaders`
`shader_id`, `run_id`, `pass_id`, `stage`, `shader_name`, `entry_point`, `register_count`, `shared_mem_bytes`,
`local_mem_bytes`, `subgroup_size` (always NULL now), `block_size`.

### `metrics`
`metric_id`, `run_id`, `shader_id`, `pass_id`, `category`, `metric_name`, `raw_name`, `metric_value`, `unit`,
`pct_of_peak`.
An NCU import writes this table with `shader_id`. A GPU Trace run writes it with `pass_id`.
`raw_name` is the counter name as ngfx wrote it. It is NULL for NCU rows.

### `bottlenecks`
`bottleneck_id`, `run_id`, `shader_id`, `severity`, `category`, `summary`, `detail`, `recommendation`.
Only an NCU import writes this table.

## CLI examples
Run these commands from the repository root.

Run the benchmark and store the table:
```powershell
python -m scripts.profiler.capture_runner --exe build/ninja-msvc/applications/OceanFft/RelWithDebInfo/OceanFftBenchmark.exe --mode submit --db profiler.db
```

Capture frame 30 of the windowed app and store the replay times:
```powershell
python -m scripts.profiler.capture_runner --exe build/ninja-msvc/applications/OceanFft/RelWithDebInfo/OceanFft.exe --mode present --frames 1 --warmup 30 --replay-loops 10 --capture-dir build/profiler_captures --db profiler.db
```

Import an NCU report:
```powershell
python -m scripts.profiler.capture_runner --ncu-rep reports/kernel.ncu-rep --db profiler.db
```

Run a GPU Trace of the windowed app, with the validation layer off:
```powershell
python -m scripts.profiler.capture_runner --exe build/ninja-msvc/applications/OceanFft/RelWithDebInfo/OceanFft.exe --mode gputrace --trace-args "--frames=1500 --size=1024 --validation=off" --capture-dir build/profiler_captures --db profiler.db
```

Import an existing GPU Trace export folder (the folder that holds `BASE`):
```powershell
python -m scripts.profiler.capture_runner --gputrace-dir build/profiler_captures/OceanFft_gputrace_20261026_200000 --db profiler.db
```

Show the scopes of a GPU Trace run, one row per scope:
```powershell
python -m scripts.profiler.query_metrics --run 5 --scopes --db profiler.db
```

Show the metrics of the IFFT scope only:
```powershell
python -m scripts.profiler.query_metrics --run 5 --metrics --pass IFFT --db profiler.db
```

Show the latest run:
```powershell
python -m scripts.profiler.query_metrics --latest --db profiler.db
```

Compare run 2 with run 4. The comparison lines up the same configuration and pass in both runs, and shows median and p95:
```powershell
python -m scripts.profiler.query_metrics --compare 2 4 --db profiler.db
```

Show a run as JSON:
```powershell
python -m scripts.profiler.query_metrics --run 4 --json --db profiler.db
```

Show the NCU bottlenecks or the metrics of a run:
```powershell
python -m scripts.profiler.query_metrics --run 4 --bottlenecks --db profiler.db
python -m scripts.profiler.query_metrics --run 4 --metrics --category sm --db profiler.db
```

## Python query API
```python
from scripts.profiler.query_metrics import BenchmarkDB

with BenchmarkDB("profiler.db") as db:
    comparison = db.compare_runs(run_id_a=1, run_id_b=2)
    for p in comparison["pass_comparisons"]:
        print(p["config"], p["pass_name"], p["median_us_a"], p["median_us_b"], p["pct_change"])
```
`avg_frame_time_ms`, `avg_fps` and the frame time deltas are `None` for a benchmark run.
