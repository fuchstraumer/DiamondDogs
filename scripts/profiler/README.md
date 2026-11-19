# DiamondDogs GPU Profiler Scripts

## Overview
These scripts run a workload and store its timings in a SQLite database.
A GPU Trace run also stores the shader profile: the samples and stall reasons of each shader and of each hot
source line, read from the trace through Nsight's `ngfx-mcp`.
They also record which code was measured.
You can then compare two runs.

## Quick start: find the bottleneck of a shader
This is the loop for an agent that optimizes shaders: trace, read the stalls, change the shader, trace again.
1. Trace the app with the shader profiler on (GPU Trace mode). A headless app needs `--trace-start-submits`:
   ```powershell
   python -m scripts.profiler.capture_runner --exe <RelWithDebInfo>/PlanetGenBench.exe --mode gputrace --trace-start-submits 100 --trace-args "<app arguments>" --db profiler.db
   ```
   A windowed app that presents frames does not need `--trace-start-submits`.
2. Find the shaders that take the most samples, and their main stalls:
   ```powershell
   python -m scripts.profiler.query_metrics --stalls --db profiler.db
   ```
3. Find the source lines that stall, with the source text:
   ```powershell
   python -m scripts.profiler.query_metrics --hotspots --shader-name OwnersCS --limit 15 --db profiler.db
   ```
4. Read the [stall reason guide](#stall-reasons-what-they-mean-and-what-to-try) for the top stall. Change the shader.
5. Trace again. Compare the two runs: `--compare-shaders <old run> <new run>` for the stall mix, and a benchmark
   (submit mode, or the app's own bench) for the time. Sample shares are not times.

Run-to-run noise: two traces of PlanetGenBench taken minutes apart agreed within about 1 point on the share of
each shader, and within about 5 points on its top stall. A smaller change is noise.

## What these scripts do not do
- Nsight Compute (NCU) is not installed on this machine.
  NCU profiles CUDA kernels. It does not profile Vulkan dispatches.
  The `--ncu-rep` path can read an `.ncu-rep` file, but this engine cannot make one.
- Present mode stores replay frame times only. They are whole-frame times, not render pass times.
- The shader profile covers the whole trace (one frame or one submit). It is not split by scope or by time yet;
  `gpu_trace.get_shader_profile` takes a `range_selector` that would allow it.
- No SASS (assembly) lines. Nsight gates them, and the source lines were enough so far.
- The scripts give no advice. They do not diagnose bottlenecks. The stall guide below is for the reader.
  The `bottlenecks` table holds NCU rule results only.

## Run the tests
Run this command from the repository root:
```powershell
python -m unittest discover -s scripts/profiler -p "test_*.py" -v
```
The tests that need the real `ncu_report` module skip when it is missing.
`test_shader_profile.py` uses `test_data/shader_profile_sample.json`, a real PlanetGenBench profile cut to two
shaders. No test starts ngfx-mcp: check that path by hand with `--gputrace-dir` on a trace that has the profiler on.

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
GPU Trace mode runs the Nsight Graphics GPU Trace activity on the app, with the Real-Time Shader Profiler on.
It stores the GPU time of each debug-label scope, the counters, and the shader profile: the samples and stall
reasons of each shader and of each hot source line.

The trace records one frame, or one queue submit:
- By default it starts after frame 120 (`--trace-start-frame`) and records one frame. The app must present.
- With `--trace-start-submits N` it starts after N submits and records one submit. Use this for a headless app,
  such as a benchmark: it never presents, so a frame trace reports "Data fetching never started".
  Pick N after the warm-up of the app.

The shader profiler is easy to lose. These three things must all be true, or the trace records
"Real-Time Shader Profiler: Disabled" in `BASE/REPRO_INFO.xls` and has no stall data:
- `--real-time-shader-profiler` is on the ngfx command line.
- `--architecture` names the GPU family. ngfx ignores the per-architecture options without it.
- `--metric-set-id` names a metric set that allows the profiler. On Ada, set 1 (Throughput Metrics) does; set 0
  (Async Compute Triage, the default) does not. `ngfx.exe --activity "GPU Trace Profiler" --help-all` lists the sets.

The runner sets all three. It checks `REPRO_INFO.xls` after the trace and prints a warning when the profiler was off.

- `--trace-args` sets the app arguments (default `--frames=1500 --validation=off`).
- `--trace-start-frame` sets the frame that starts the trace (default 120).
- `--trace-start-submits` starts the trace on submits instead (see above).
- `--trace-architecture` sets the GPU family (default `Ada`, the RTX 4080).
- `--trace-metric-set` sets the metric set index (default 1).
- `--multi-pass-metrics` collects more counters over several frames. It is off by default: it is not tested with
  the shader profiler or with a submit trace.
- `--capture-dir` is the folder for the runs. Each run gets one new subfolder.
  The subfolder holds the `.ngfx-gputrace` file and a `BASE` folder of exports.
- `--timeout-s` applies to the ngfx process and to ngfx-mcp.
- `--gputrace-dir <folder>` ingests an existing export folder, with its shader profile. It runs no trace.
  It stores no code state and no build fingerprint, because the folder does not record them.

The runner finds the newest Nsight Graphics install under `C:\Program Files\NVIDIA Corporation`.

The validation warning checks the app arguments for `--validation=off`. An app with no validation option, such as
PlanetGenBench (it never enables validation), gets the warning anyway. Ignore it there.

The run stores:
- One `runs` row. Its `mode` is `gputrace`. It holds the frame time, the GPU name, the driver, `gpu_clocks`,
  `metric_set`, `shader_profiler` (`Enabled` or `Disabled`), `profiler_samples` and `unattributed_samples`.
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

- With the shader profiler on: one `shaders` row per profiled shader, one `shader_lines` row per hot source line,
  and `shader_stalls` rows for the stall reasons of each shader and of each line. A shader gets the `pass_id` of
  the scope whose last name is its entry point, when the app has a scope per dispatch (PlanetGenBench
  `--dispatches` does).

Use `query_metrics --scopes` for one row per scope. It shows the time and six counters as percent of peak:
DRAM, LTS, SM, L1tex, shared-memory LSU wavefronts and active warps in compute shaders.

### Where the shader profile comes from (`ngfx_mcp.py`)
The text export of a trace has no shader data. The samples are only in the `.ngfx-gputrace` file. Nsight Graphics
2026.4 and later ship `ngfx-mcp.exe`, an MCP server that reads it. `ngfx_mcp.py` starts it with
`--profile native --transport stdio`, and calls these tools:
- `gpu_trace.open_trace` and `gpu_trace.close_trace`.
- `gpu_trace.get_shader_catalog` with `stall_reason_types` (the names of the 18 stall reasons) and `shaders`
  (one record per shader, with its Slang file).
- `gpu_trace.get_shader_profile` with `performance` (the samples and stalls of one shader, or of the
  `unattributed` samples) and `hotspots_shader` (every hot line of one shader, with the source text).

The Native profile has no `initialize` handshake. Each request carries `params._meta` with the protocol version
(`2026-07-28`), the client information and the client capabilities, or the server answers "Invalid request
metadata". NVIDIA's docs are in `<Nsight install>/host/windows-desktop-nomad-x64/docs/ngfx-mcp`. The server has
more tools than these (per-scope ranges, SASS, live state); `McpAgentReference.md` lists them.

Shader objects (`VK_EXT_shader_object`) work: each entry point is one shader with its Slang file and lines.

About half of the samples belong to no shader (`unattributed_samples`; 50 % in a PlanetGenBench trace). Nsight
does not say what they are. Many small dispatches with barriers between them fit that number, but this is not
proven. Read shader shares as shares of all samples, not of shader samples.

## Stall reasons: what they mean and what to try
The shader profiler samples the warps of each SM at a fixed interval and records what each warp was doing. A sample
of a warp that issued an instruction is "Selected"; every other sample names the reason that the warp could not
issue. The reasons below are the 18 that `stall_reason_types` returns on Ada, by their short names.

**How to read a hot line.** A stall is charged to the instruction that waits, not to the instruction that made the
value. A long scoreboard stall on `if (LoadAlive(flat) == 0u)` means that the line waits for the load inside
`LoadAlive`, and the cost is the latency of that load. Look at where the value came from.

**Which stalls matter.** Selected and Not Selected are not problems: they show a warp that was ready. A shader with
a large Selected plus Not Selected share is limited by issue (arithmetic or instruction count). A shader with a large
share of one wait reason is limited by that reason. Fix the largest one first, then trace again.

| Short | Name | What the warp waits for | What to try |
|---|---|---|---|
| SELECT | Selected | Nothing: it issued. | High with NOTSEL means issue bound: fewer instructions, cheaper math. |
| NOTSEL | Not Selected | Nothing: it was ready, and the scheduler picked another warp. | Same as SELECT. Not a stall to fix. |
| LGSB | Long Scoreboard | A result from L1TEX: global, local or texture memory. | Coalesce the loads, keep data in cache, load earlier (start the load before the work that does not need it), unroll so that several loads are in flight, raise occupancy, or move reused data to groupshared memory. A gather with a computed index is the usual cause. |
| SHRTSB | Short Scoreboard | A result from the MIO: groupshared memory, special functions (sin, log, rcp), some conversions. | Fewer bank conflicts in groupshared memory, fewer special-function calls, or more independent work between them. |
| WAIT | Wait | A fixed-latency dependency: the result of the instruction just before. | Break dependency chains: unroll with independent accumulators (two or four `best` values in place of one), interleave independent work. Common in tight loops. |
| MATHTH | Math Pipe Throttle | A busy math pipe (FMA, ALU, FP64, special functions). | Use cheaper operations, move work out of the loop, or precompute. Shows a pipe at its limit. |
| MIOT | MIO Throttle | A full MIO queue: too many groupshared, special-function or shuffle instructions. | Fewer groupshared accesses or shuffles, wider (vector) groupshared accesses. |
| LGTHR | LG Throttle | A full queue of local and global memory instructions. | Fewer, wider loads (one float4 in place of four floats), less register spill to local memory. |
| TEXTHR | Texture Throttle | A full texture pipe queue. | Fewer texture fetches, or gather operations. |
| IMCMIS | Immediate Constant Cache Miss | A constant buffer value that is not in the constant cache. | Make constant accesses uniform across the warp; do not index constants by a value that differs per lane. Small in short shaders, where every warp misses once. |
| THDBAR | Barrier | Other warps of the group at a group barrier (`GroupMemoryBarrierWithGroupSync`). | Fewer barriers, more even work between them, or smaller groups. |
| MEMBAR | Memory Barrier | A memory fence to complete. | Fewer fences, or a narrower scope of fence. |
| BRANCH | Branch Resolving | A branch target to be computed. | Fewer divergent branches; select (`?:`) in place of short branches. |
| DISPCH | Dispatch Stall | The dispatch unit (an internal pipeline conflict). | Usually small. Nothing to do when it is small. |
| NOINST | No Instructions | Instruction fetch: an instruction cache miss. | Smaller code: less unrolling, fewer shader variants in one dispatch. Short shaders show some at their start. |
| DRAIN | Drain | Its memory stores to drain at exit. | Fewer or smaller stores at the end of the shader. |
| SLEEP | Sleeping | A sleep or a yield instruction. | Rare in our shaders. |
| MISC | Miscellaneous | Other reasons. Stores at the end of a shader often show as MISC. | Look at the line; often nothing to do. |

NVIDIA's own descriptions are in the Nsight Graphics documentation (GPU Trace, "Shader Profiler") and the Nsight
Compute "Warp Stall Reasons" table, which uses the same hardware reasons.

**An example (PlanetGen, 2026-11-18).** `OwnersCS` had LGSB 37 %, NOTSEL 21 %, WAIT 12 %, SELECT 12 %,
MATHTH 11 %. Its hottest line was `Owners.slang:57`, `if (LoadAlive(flat) == 0u)`, at 89 % LGSB: a gather whose
index comes from `FibonacciNearest`. So half of the shader waits for scattered loads, and half is issue bound on the
`atan2`, `log` and `pow` of `FibonacciNearest`. `NearestFrontPointCS` had WAIT 32 % and LGSB 29 %, with WAIT on the
compare-and-update lines of its loop: a dependency chain on `best`, which unrolling with independent accumulators
breaks.

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
`PRAGMA user_version` holds the schema version (now 5; version 5 added the shader profile).
On open, `init_db` creates missing tables and adds missing columns with `ALTER TABLE`. It drops and creates the
views again, so a change to a view reaches old databases.
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
- `metric_set`, `shader_profiler`: from `REPRO_INFO.xls` of a GPU Trace. `shader_profiler` is `Enabled` or
  `Disabled`.
- `profiler_samples`: all shader profiler samples of the trace. `unattributed_samples`: the samples of no shader.

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
A GPU Trace shader also has `trace_shader_index` (the index in the trace), `source_file` (its main Slang file),
`module_hash`, `sample_count` and `pct_of_samples` (its share of all samples of the trace).

### `shader_lines`
`line_id`, `run_id`, `shader_id`, `hotspot_rank` (0 is the hottest line of the shader), `file_name`, `line_number`,
`function_scope` (the inline chain, for example `LoadRotation > BoundReadBuffer.Load`), `source_text`,
`sample_count`, `pct_of_samples` (share of all samples of the trace).
A line can be in an included file (for example `LodestoneShims.slang:223`, a buffer load). `function_scope` tells
which call of the shader inlined it.

### `shader_stalls`
`stall_id`, `run_id`, `shader_id`, `line_id` (NULL for the stalls of the whole shader), `stall_short` (for example
`LGSB`), `stall_name` (`Long Scoreboard`), `sample_count`, `pct_of_scope` (share of the shader's or the line's
samples). The stall rows of a shader add up to its `sample_count`.

### Views
The views need no joins, so a short query answers most questions.
- `v_shader_stalls`: one row per shader and stall reason. Columns: `run_id`, `shader_name`, `source_file`,
  `shader_samples`, `shader_pct_of_samples`, `stall_short`, `stall_name`, `stall_samples`, `pct_of_shader`.
- `v_line_stalls`: one row per hot line and stall reason. Columns: `run_id`, `shader_name`, `hotspot_rank`,
  `file_name`, `line_number`, `function_scope`, `source_text`, `line_samples`, `line_pct_of_samples`,
  `stall_short`, `stall_name`, `stall_samples`, `pct_of_line`.

Examples:
```sql
-- the lines of run 2 that wait most on memory
SELECT shader_name, file_name, line_number, source_text, stall_samples
FROM v_line_stalls WHERE run_id = 2 AND stall_short = 'LGSB' ORDER BY stall_samples DESC LIMIT 10;

-- how the stall mix of OwnersCS changed between run 1 and run 2
SELECT run_id, stall_short, ROUND(pct_of_shader, 1) FROM v_shader_stalls
WHERE shader_name = 'OwnersCS' AND run_id IN (1, 2) ORDER BY stall_short, run_id;
```

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

Trace a headless benchmark on its 100th submit, with the shader profiler:
```powershell
python -m scripts.profiler.capture_runner --exe <RelWithDebInfo>/PlanetGenBench.exe --mode gputrace --trace-start-submits 100 --trace-args "--reference=<folder> --dispatches --steps=20000" --db profiler.db
```

Show the shaders of a GPU Trace run with their stalls, then the 15 hottest lines of one shader:
```powershell
python -m scripts.profiler.query_metrics --run 2 --stalls --db profiler.db
python -m scripts.profiler.query_metrics --run 2 --hotspots --shader-name NearestFrontPoint --limit 15 --db profiler.db
```

Compare the shader shares and top stalls of two GPU Trace runs:
```powershell
python -m scripts.profiler.query_metrics --compare-shaders 1 2 --db profiler.db
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
