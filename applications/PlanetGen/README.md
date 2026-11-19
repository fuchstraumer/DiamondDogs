# PlanetGen

The C++ and Lodestone port of the PlanetLab plate model. PlanetLab (`applications/PlanetLab`) is the Python
prototype, and git ignores it. Its file `gpu_port/README.md` is the pass specification, and its script
`write_gpu_reference.py` writes the reference files that the checks here read.

## Parts

| Path | Holds |
|---|---|
| `assets/shaders/compute/Planet/` | The Slang module `PlanetLab` (passes 0 to 6). Lodestone cooks it to `ShaderLibrary.ldmanifest` as a build step (target `planet_gen_shaders`). `Buffers.slang` declares the buffer shims and the crust flag axis. |
| `gpu/` | `planet_gpu`, a static library. `PlanetShaders` selects a variant for the device, makes one program for each entry point, and writes the descriptor sets by the names that the cook reflected (`WriteManifestDescriptors`). It also writes uniform buffers from the member offsets in the manifest. `GpuBuffers` holds buffers by their shader names, fills and copies them, and reads them back. `PlanetStep` records passes 1 to 6 of one step (see "The step"). |
| `check/` | `planet_reference`, a static library: `ReferenceSet` reads the reference files, and `AddStepBuffers` makes every buffer of one step from them. `PlanetGenCheck`, a headless executable: it runs each pass on the reference inputs, and the whole step, and compares the results with the reference outputs. |
| `bench/` | `PlanetGenBench`, a headless executable: the GPU time of each pass of the step (see "The bench"). |

## Sources

- The inverse map (`Fibonacci.slang`) is the algorithm of Keinert, Innmann, Saenger and Stamminger, "Spherical
  Fibonacci Mapping" (SIGGRAPH Asia 2015). It is not ours. Our changes: a 64-bit integer azimuth, a closed form for
  the grid basis, and an integer candidate index. Without them the map fails in float32 from 100 000 points.
- The plate model follows Cortial, Peytavie, Galin and Guérin, "Procedural Tectonic Planets" (2019). The
  PlanetLab README lists the other sources and which rules are ours.

## The check

Make the reference files first, from `applications/PlanetLab`. The script is in `python/` and the package
`planetlab/` is beside that folder, so the package path must be set:

```bat
set PYTHONPATH=.
C:\Python313\python.exe python\write_gpu_reference.py 10000 40
C:\Python313\python.exe python\write_gpu_reference.py 100000 40
```

Then run the check for each size, from its output folder:

```bat
PlanetGenCheck --reference=C:\SoftwareDev\DiamondDogs\applications\PlanetLab\gpu_port\reference_100000
```

`--checks=map,owners,front,crust,live,distance,uplift,torque,chain` selects checks; the default is all of them. Each check runs once for each crust flag
storage (bytes, then words). A storage that the device cannot run prints `SKIP`. The program exits 0 only if each check
passes. Validation is `Full`, so a pass also needs output with no `Validation Error` and no `VUID`. If the reference
folder is missing, the check fails; it does not skip.

To test a device without 8-bit storage, decline it with `--config=<file>`:

```json
{ "Device": { "Declined": [ "StorageBuffer8BitAccess" ] } }
```

The device then prefers words, and the byte runs print `SKIP`.

The tolerances are the ones of the pass specification:

| Check | Pass | Tolerance |
|---|---|---|
| `map` | 0, the inverse map | Equal, or a near tie (5 % of the point spacing); fewer than 10 ties |
| `owners` | 1, owners | Fewer than 1 point in 10 000 differs |
| `front` | 2, one plate on top | Exact |
| `crust` | 3, new crust and removal | Exact, and a relative 1e-4 for the front values |
| `live` | 3b, the live cell list | Counts, offsets and the indirect group counts exact; each plate's range of the list holds exactly its live cells (as a set) |
| `distance` | 4, distance to fronts (brute force) | Front point lists exact (as sets); on live cells, a cell front point equal, or a near tie (1 % of the spacing), fewer than 10 ties; distances within the larger of 1 % of the spacing and 1e-4 of the distance |
| `uplift` | 5, uplift, erosion, accretion | On live cells: relief and orogeny time within 1e-4 (and a relative 1e-5); orogeny and continental exact |
| `torque` | 6, sums for the torque balance | Cell counts exact; position sums and pulls within a relative 1e-4 of the largest component of each plate |
| `chain` | 1 to 6 as one step (`PlanetStep`) | The tolerances above, at the end of the step. The plate and cell on top allow the border points of `owners`; the cell front points allow the near ties of `distance`; everything else that the single-pass checks hold exact is exact |

Before each pass was in the module, its check was tested against a throwaway implementation of the contract in the
pass specification (`gpu_port/README.md`), which was not kept:

- `distance`: it passed at both sizes in both storages, and it failed when the search skipped the last front point of
  each plate (1699 and 998 cells wrong at 10 000 points).
- `uplift`: it passed at both sizes in both storages (largest relief difference 4.8e-7 km), and it failed with no arc
  accretion (3 cells wrong at 100 000 points).
- `torque`: it passed at both sizes in both storages (pull 7.9e-5 relative at 100 000 points, of the 1e-4 allowed),
  and it failed without the square root of the slab age weight (9 plates wrong at 10 000 points).
- `live`: it passed at both sizes in both storages, on both compilers. It failed when the scatter did not advance its
  slot (9 plate ranges wrong at 10 000 points), and when the group count rounded down (156 groups for 10 014 cells,
  and 30 cells with no front point in `chain`).

`distance`, `uplift` and the end of `chain` compare live cells only. The passes on the live cell list do not write
dead cells, and the reference also erodes dead continental cells, which nothing reads: `FillGapsCS` sets the relief of
a cell to 0 when it comes back. The single-pass checks make the live cell list on the host, from `p3_ref_alive`.

The orogeny and accretion arrays are exact, not "a few cells", on
purpose: only 3 cells accrete at 100 000 points (0 at 10 000), so an allowance of a few cells would pass a kernel that
never accretes.

`chain` runs the step twice in one submit, with a reset before each run, so a reset that misses a buffer fails it.
That was tested: with no reset of `PlateCellCount`, all 20 plate cell counts were wrong at 10 000 points. At
100 000 points one cell on top differs (`p3_cell`); it is the one border point of `owners`, and nothing after pass 3
changes because of it.

## The step

`PlanetStep` records passes 1 to 6 into one command buffer, with a compute barrier between each two dispatches. Each
pass reads the buffers that the passes before it wrote. Create the shaders with `k_StepEntryPoints`, and the buffers
with `AddStepBuffers` (or the same names and sizes from another source).

The order is 1, 2, 3, 3b, 4, **6**, 5. That is the order of `tectonics.Step`: the torque balance (`MovePlates`) reads
the continental flags before arc accretion changes them, and pass 5 holds the accretion. Pass 6 reads nothing that
uplift writes.

Pass 3b makes the live cell list (`LiveCells.slang`), right after pass 3, the last pass that changes which cells are
alive. About 5 % of the `[plate][cell]` grid is alive (100 048 of 2 000 000 cells at 100 000 points), and in the grid
form 73 % of the waves of 32 had no live lane. The list is grouped by plate:

- `CountLiveCellsCS`: each workgroup of 256 threads covers 1024 cells of one plate. It adds the live cells, the
  continental cells and the fixed-point position sums of the group, then does one atomic for each value: about 100
  for each plate at 100 000 points. These are the sums of pass 6, so pass 6 has only `SumPullCS` now.
- `OffsetLiveCellsCS`: one thread. It writes `PlateLiveOffset` and the indirect group counts `LiveDispatch`.
- `ScatterLiveCellsCS`: the same groups. Each group takes its slots with one atomic, each wave takes a range of the
  group's with a groupshared atomic, and each lane a range of the wave's (`WavePrefixSum`). The order in a plate's
  range is not fixed; every pass on the list writes to the cell it reads, so the results do not depend on it.

`NearestFrontPointCS` and `UpliftCS` dispatch indirectly, one thread for each live cell (`PlanetShaders::
RecordDispatchIndirect`, after `RecordIndirectBarrier`).

`RecordReset` puts the buffers back in the state of the start of the step, so that one start state can be stepped many
times (the bench) and checked after any run (`chain`):

- The six crust arrays are copied from their initial copies, which have the prefix `Initial.` (`InitialName`).
- Every output and counter (`StepZeroedBuffers`) is filled with zero.

A real time loop will not reset: it will turn the rotations on the CPU (the torque balance) and step again. That part
is not ported yet.

## The bench

```bat
PlanetGenBench --reference=C:\SoftwareDev\DiamondDogs\applications\PlanetLab\gpu_port\reference_100000 --dispatches --json=<file>
```

Each measured step is one submit: `RecordReset` (not timed), then the passes, each in its own GPU timer scope. The
table gives min, median and p95 in microseconds, after 20 warm-up steps (`--warmup=N`) and over 200 steps
(`--steps=N`). `--dispatches` adds a scope for each dispatch inside its pass; the extra timestamps cost about 4 µs over
the whole step. `--json` appends one line per run, so that later runs can be compared with a baseline. The device has
no validation layer.

Measure in RelWithDebInfo. Release compiles the GPU timers out (`RHI_GPU_TIMERS_ENABLED`), and the bench then exits
1. Check that the GPU is idle first. The clocks are not locked, so two runs of the same build can differ by about
8 % (310 and 336 µs for the step at 100 000 points).

### Nsight Graphics GPU Trace

The bench has no present, so start the trace by submits: each step is one submit. From the command line:

```bat
ngfx.exe --activity "GPU Trace Profiler" --exe <RelWithDebInfo>\PlanetGenBench.exe --dir <RelWithDebInfo> --args "--reference=<folder> --dispatches --steps=20000" --start-after-submits 100 --limit-to-submits 1 --architecture Ada --metric-set-id 1 --real-time-shader-profiler --auto-export --output-dir <folder>
```

`ngfx.exe` is in `C:\Program Files\NVIDIA Corporation\Nsight Graphics 2026.4.0\host\windows-desktop-nomad-x64`. It
writes a `.ngfx-gputrace` file into `--output-dir`; open it in the GUI with File, Open. The GUI cannot start this
trace itself: it starts on frames, and the bench never presents. `VK_EXT_frame_boundary` does not help: only the
Nsight Capture layer offers it, not the driver and not the GPU Trace layer. Do not pass `--frame-boundaries`.

- After the trace, Nsight ends the process and prints "connection error encountered". That is the normal end.
- The shader profiler options are per architecture. `--real-time-shader-profiler` alone is ignored; it needs
  `--architecture Ada`, and the default Ada metric set (0, Async Compute Triage) keeps it off. Set 1 (Throughput
  Metrics) turns it on. Check the line "Real-Time Shader Profiler" in `BASE\REPRO_INFO.xls` of the export. With it off,
  every entry point gives the warning "Metric <entry point> has no data".
- The `--auto-export` text files hold scope times and counters only, never shader samples. `scripts/profiler`
  runs the whole trace and reads the shader profile through Nsight's `ngfx-mcp` into a SQLite database:
  `capture_runner --mode gputrace --trace-start-submits 100`, then `query_metrics --stalls` and `--hotspots`. Its
  README has the commands and a guide to the stall reasons.
- GPU Trace locks the clocks to base, so its times are about 15 % longer than the bench's.

First read of the throughput (% of peak, 100 000 points, after the live cell list): `OwnersCS` SM 67, L2 53, DRAM 42
(compute bound); `NearestFrontPointCS` SM 52, occupancy 38, DRAM 5 (latency in the loop); `UpliftCS` L2 49, DRAM 51,
SM 5 (memory bound); the small passes use under 5 % of the SM, so their 3 to 5 µs is the cost of a dispatch.

### Baseline (2026-11-18)

RTX 4080, clang-cl RelWithDebInfo, crust flags in bytes, 20 plates. Medians in µs, `--dispatches`:

| Scope | 10 000 points | 100 000 points |
|---|---|---|
| Step | 60.6 | 339.5 |
| 1 Owners (`OwnersCS`) | 14.0 | 47.1 |
| 2 One plate on top | 7.2 | 9.2 |
| 3 Crust | 6.1 | 10.2 |
| 4 Front distance | 14.3 | 126.0 |
| `NearestFrontPointCS` | | 112.6 |
| 6 Torque sums | 14.3 | 123.9 |
| `SumPlatesCS` | | 118.8 |
| `SumPullCS` | | 6.1 |
| 5 Uplift (`UpliftCS`) | 4.1 | 22.5 |

The 10 000-point column is from a run without `--dispatches`. CPU time from submit to fence: 0.52 ms at 100 000 points.

Two dispatches take two thirds of the step at 100 000 points: `NearestFrontPointCS` (the brute-force search) and
`SumPlatesCS`. `SumPlatesCS` runs as many threads as `UpliftCS` and takes five times as long. Two scratch builds
measured why:

- With `LatticePoint` replaced by a cheap stand-in, it was still 117.8 µs. The lattice math is not the cost.
- With the three position-sum atomics removed (the two count atomics kept), it was 68.6 µs: about 16 µs for each
  atomic that the first lane of each wave does. Each plate's five counters get one atomic from each of its 3125 waves
  (wave32), so the cost is probably contention on those few addresses. That cut also removes the `WaveActiveSum`, so
  the 49 µs is the atomics and that wave op together.

### After the live cell list (2026-11-18)

The same machine and build, on a throwaway implementation of the live cell contract. Medians in µs:

| Scope | Baseline, 100 000 | Live list, 100 000 | Baseline, 10 000 | Live list, 10 000 |
|---|---|---|---|---|
| Step (pass scopes only) | 310 to 336 | 148.2 | 60.6 | 66.1 |
| `NearestFrontPointCS` | 112.6 | 33.8 | | 5.1 |
| `SumPlatesCS`, then the 3b pass | 118.8 | 25.6 (all of 3b) | | 15.4 |
| `UpliftCS` | 22.5 | 7.2 | | 4.1 |

At 100 000 points the step is 2.1 to 2.3 times faster. At 10 000 points it is 9 % slower: pass 3b adds three
dispatches, and each dispatch costs about 4 to 5 µs even when it does almost nothing (`OffsetLiveCellsCS` is one thread
and takes 5 µs). At that size the floor of the dispatches is more than the work they save. `OwnersCS` (47 µs) is now
the largest dispatch at 100 000 points.

### The front point search in groupshared tiles (2026-11-18)

`NearestFrontPointCS` compares each live cell with every front point of its plate: 147 front points on average at
100 000 points, and 277 on the largest plate. It has fewer warps than one full wave of the GPU (about 3100 warps of
the 64-thread groups, against 76 SMs x 48), so the scheduler cannot hide the latency of each step with other warps.
The profiler showed WAIT 33 % (each compare needs the `best` of the step before) and long scoreboard 29 % (each step
waits for its front point load).

Medians in µs at 100 000 points, alternated runs (`--dispatches`):

| Search loop | `NearestFrontPointCS` | Step |
|---|---|---|
| One candidate, one load each step (float4 front points) | 25.6 | 137.0 |
| Four candidates, four loads each step | 24.6 to 25.6 | 136.7 |
| Four candidates, eight loads each step | 23.6 | 134.8 |
| Groupshared tile of 64 front points, four candidates | 18.4 | 130.1 |

Four candidates broke the chain (WAIT fell to 12 %), but the wait moved to the loads (long scoreboard rose to 41 %):
each step still paid one L1 round trip. The tile pays it once for 64 front points: each lane of the group loads one
slot, and then all lanes read the tile from groupshared memory. The live cell list and the front point lists are both
in plate order, so the plates of a group's first and last cells bound one contiguous range of slots, and a group that
spans two plates still loads one range; each lane compares only the slots of its own plate. Every lane takes part in
the loads and barriers, so a lane past the end of the list returns only after the tile loop. The four candidates merge
with a total order (distance, then front point index), so the result does not depend on the slot order.

With pass scopes only, the step is now 127.1 µs at 100 000 points and 60.7 µs at 10 000 (pass 4: 29.7 and 13.3 µs).
`OwnersCS` (about 45 µs) is the next target: cone culling (see the PlanetLab handoff).

## Buffers

Each storage buffer is a Lodestone shim (`IReadBuffer`, `IReadWriteBuffer`, `IAtomicBuffer`). The `spirv` cook binds
each one as a descriptor. A `spirv-modern` cook would store a buffer device address instead, with no change to the
Slang; the host side here writes descriptors only.

### Storage of the crust flags

All passes read and write one set of crust arrays: `CrustAlive`, `CrustContinental` and `CrustBirth`. The Capability
axis `CRUST_FLAG_BYTES` selects the storage of the two flags:

| Value | Buffers | Cost | Device needs |
|---|---|---|---|
| `true` | `CrustAliveBytes`, `CrustContinentalBytes`: one byte for each cell | 1 byte | `StorageBuffer8BitAccess`, `UniformAndStorageBuffer8BitAccess`, `Int8` (what the cook recorded) |
| `false` | `CrustAliveWords`, `CrustContinentalWords`: one word for each cell | 4 bytes | nothing more |

A byte store changes only its own byte, so the threads of pass 3 need no atomic in either form. The 8-bit needs are
`Wanted`, not `Required`, because only one value of a Capability axis needs them. `PlanetShaders::Create` with
`CrustFlagChoice::BestForDevice` uses `PreferCapable`, so it takes bytes when the device was created with 8-bit
storage. `PlanetShaders::CrustFlags()` tells the host which buffers to create; `CrustFlagBufferNames` gives their
names. Each variant binds only the buffers of its own storage.

### Votes

Pass 2 counts votes with `IAtomicBuffer<uint>.Add`, one atomic for each vote. `AtomicAddWaveAggregated` does not fit:
it needs one index on every lane, and the pair index changes from lane to lane. It would also save little. In the
100 000-point capture, 808 points vote (0.8 %), a wave of 32 that has a voter has 1.1 voters on average, and a perfect
aggregation would remove 18 of the 808 atomics.
