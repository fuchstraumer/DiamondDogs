# PlanetGen

The C++ and Lodestone port of the PlanetLab plate model. PlanetLab (`applications/PlanetLab`) is the Python
prototype, and git ignores it. Its file `gpu_port/README.md` is the pass specification, and its script
`write_gpu_reference.py` writes the reference files that the checks here read.

## Parts

| Path | Holds |
|---|---|
| `assets/shaders/compute/Planet/` | The Slang module `PlanetLab` (passes 0 to 6). Lodestone cooks it to `ShaderLibrary.ldmanifest` as a build step (target `planet_gen_shaders`). `Buffers.slang` declares the buffer shims and the crust flag axis. |
| `gpu/` | `planet_gpu`, a static library. `PlanetSession` owns the headless Vulkan boot stages, shader library, and compute command pool. `PlanetShaders` selects a variant for the device, makes one program for each entry point, and writes the descriptor sets by the names that the cook reflected (`WriteManifestDescriptors`). It also writes uniform buffers from the member offsets in the manifest. `GpuBuffers` holds buffers by their shader names, fills and copies them, and reads them back. `PlanetStep` records passes 1 to 6 of one step (see "The step"). |
| `check/` | `planet_reference`, a static library: `ReferenceSet` reads the reference files, `PlanetMath` holds double-precision lattice and sphere math, and `AddStepBuffers` makes every buffer of one step from them. `PlanetGenCheck`, a headless executable (`CheckHarness`, `PassChecks`, `SurfaceChecks`, `StepChecks`, `main.cpp`): it runs each pass on the reference inputs, and the whole step, and compares the results with the reference outputs. |
| `bench/` | `PlanetGenBench`, a headless executable (`BenchReport`, `main.cpp`): the GPU time of each pass of the step, console table, and JSON Lines output (see "The bench"). |

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

### Cone culling in OwnersCS, and wave-op offsets (2026-11-19)

Pass 3b writes a cap for each plate (`PlateCapCenters`, `PlateCapSpread`) around its live cells. `OwnersCS` of the next
step puts each plate's cap into groupshared memory, turned to the world frame, with a margin of 2 cells. It skips a
plate when the point is outside the cap, before it computes `FibonacciNearest` and loads the crust flags. The offset
passes (`OffsetLiveCellsCS`, `OffsetFrontPointsCS`) are now one wave of 32 threads (`WavePrefixSum`), not one thread.

Medians in µs, the build before these changes against the build after them, alternated runs (`--dispatches`):

| Scope | Before, 100 000 | After, 100 000 | Before, 10 000 | After, 10 000 |
|---|---|---|---|---|
| Step (`--dispatches`) | 129.9 | 112.5 | | |
| Step (pass scopes only) | | 108.1 | 60.2 | 54.3 |
| `OwnersCS` | 44.1 | 27.8 | 13.6 | 11.2 |
| `OffsetLiveCellsCS` + `OffsetFrontPointsCS` | 10.2 | 6.2 | | 6.2 |
| `ScatterLiveCellsCS` (now also makes the cap spread) | 7.7 | 9.3 | | 3.5 |
| `NearestFrontPointCS` | 18.4 | 18.4 | | 4.1 |

The machine state changes the times: the first runs of the new build on 2026-11-19 gave 125 to 131 µs for the step,
with about 1 µs more in every dispatch, and the same exe gave 112.5 µs later with no change between. Compare builds only
in alternated runs.

The shader profiler after the change (100 000 points): `OwnersCS` long scoreboard 46 %, charged to the first uses of
`LoadAlive` and `LoadContinental` (the crust flag gathers, one of each for each plate that passes the cap test).
`NearestFrontPointCS` is now issue bound: not selected 22 %, wait 21 %, selected 19 %, on the (distance, index) merge.
`CountLiveCellsCS` long scoreboard 49 % on the first use of `LoadAlive`: each thread does its four cells one after the
other, so each step waits for its own load. `ScatterLiveCellsCS` barrier 23 %.

Notes for the mobile target: the offset passes need all plates in one wave. With a subgroup of 16 lanes (some mobile
GPUs) or more than 32 plates, the prefix sum covers only the first wave, and every first lane writes the total.
`OwnersCS` keeps at most 64 plates in groupshared memory.

### HEALPix point order for OwnersCS (2026-11-19, not adopted)

OwnersCS took the points in spiral order, so the 32 lanes of a wave cover a thin ring around the planet, and the
cone cull passes many plates. `HealpixPointOrder` (in `check/StepState.cpp`) sorts the points by their HEALPix nested
index, and `OwnersOrder[i]` gives thread i its point. The lab script `compare_point_orders.py` measured the cull:

| Order | Plates tested per wave, 100 000 points | 10 000 points |
|---|---|---|
| Spiral | 7.96 | 8.27 |
| HEALPix nested | 2.82 | 3.55 |

(At step 20 of the lab run. 3D Morton and cube plus Morton are within 2 % of HEALPix.)

The bench, 3 alternated runs, median `OwnersCS`: 28.8 to 30.8 µs at 100 000 points (slower), 12.2 to 10.3 µs at
10 000 (faster). `owners_gather_sectors.py` shows the cause. In spiral order, neighbouring lanes gather neighbouring
cells, so a tested plate costs about 10 sectors of 32 bytes for each wave. In HEALPix order the lanes are close on the
sphere but far apart in the spiral numbering, so a tested plate costs about 28 sectors. The traffic for each wave stays
at about 80 sectors in both orders. The outputs (`BestPlate`, `BestCell`, `SecondPlate`, `SecondCell`) and
`PreviousPlate` are indexed by point, so they become scattered too. At 10 000 points the crust fits in L2, the sectors
are cheap, and the fewer compares win.

A point order helps only when the memory order agrees with it. To get both, the lattice itself must be numbered in
HEALPix order, so that every per-point and per-cell array is stored in that order.

#### How HEALPix builds its mapping

Gorski et al. 2005. `gpu/Healpix.cpp` is a transcription of their `ang2pix_nest`, and the lab script
`check_healpix.py` tests the Python version (equal area, nesting, base faces, compactness).

1. **12 base faces of equal area.** Cut the sphere at z = 2/3 and z = −2/3. Each band between the cuts then has a third
   of the area, because on a sphere the area above a height is proportional to the height (Archimedes). The polar caps
   are split into 4 faces each, and the equatorial belt into 4 more, which sit between them like the faces of a
   rhombic dodecahedron. Faces 0 to 3 touch the north pole, 4 to 7 are on the equator (centred on longitudes 0°, 90°,
   180° and 270°), and 8 to 11 touch the south pole.
2. **Coordinates that keep the area equal.** In the belt, z and the longitude are already equal-area coordinates (a
   cylindrical equal-area projection). The face edges are diagonals in that projection, so the code turns
   (longitude, z) into two diagonal coordinates, `jp` and `jm`, scaled by Nside. Their integer parts give the face and
   the column and row inside it. In the caps, the distance from the pole is proportional to sqrt(1 − |z|), because the
   area of a cap is proportional to 1 − |z|. Scaling by that root keeps the pixels equal in area as they get close to
   the pole.
3. **Nside x Nside pixels in each face.** Each face is a curved square, and (ix, iy) is the pixel inside it. Nside is
   2^order, so ix and iy have `order` bits each.
4. **Nested numbering.** The index is face × Nside² plus the bits of ix and iy interleaved (a 2D Morton code). The
   interleave makes the index a quadtree: index >> 2 is the parent pixel one level coarser, and the 4^k indices under
   one parent are one compact patch. This is the property that makes runs of consecutive indices local on the sphere.

## Buffers

Each storage buffer is a Lodestone shim (`IReadBuffer`, `IReadWriteBuffer`, `IAtomicBuffer`). The `spirv` cook binds
each one as a descriptor. A `spirv-modern` cook would store a buffer device address instead, with no change to the
Slang; the host side here writes descriptors only.

### Storage of the crust flags

The Capability axis `CRUST_FLAG_BYTES` selects the storage of the packed crust flags (`bit 0` = alive, `bit 1` = continental):

| Value | Buffer | Cost | Device needs |
|---|---|---|---|
| `true` | `CrustFlags`: one byte for each cell (aliased as `CrustFlagsIn` for 1-byte RO loads and `CrustFlagsPackedIn` as `WordsIn` for 4-byte coalesced loads in `LiveCells.slang`). | 1 byte | `StorageBuffer8BitAccess`, `UniformAndStorageBuffer8BitAccess`, `Int8` |
| `false` | `CrustFlagsWords`: one 32-bit word for each cell (aliased as `CrustFlagsWordsIn`). | 4 bytes | nothing more |

A byte store changes only its own byte, so the threads of pass 3 need no atomic in either form. The 8-bit needs are
`Wanted`, not `Required`, because only one value of a Capability axis needs them. `PlanetShaders::Create` with
`CrustFlagChoice::BestForDevice` uses `PreferCapable`, so it takes bytes when the device was created with 8-bit
storage. `PlanetShaders::CrustFlags()` tells the host which buffer to create; `CrustFlagBufferNames` gives its
name. Each variant binds only the buffers of its own storage.

### Votes

Pass 2 counts votes with `IAtomicBuffer<uint>.Add`, one atomic for each vote. `AtomicAddWaveAggregated` does not fit:
it needs one index on every lane, and the pair index changes from lane to lane. It would also save little. In the
100 000-point capture, 808 points vote (0.8 %), a wave of 32 that has a voter has 1.1 voters on average, and a perfect
aggregation would remove 18 of the 808 atomics.

---

### Tail Pointer Updates

#### 2026-11-20 13:00 (Gemini) — PlanetGen v2 Phase 1 Locked (Native Nested HEALPix)
- **Architectural pivot & blueprint:** Authored [planetgenv2.md](planetgenv2.md) (unified Nested HEALPix `12 × 4^order` grid, Allegretti `2×2` BKE collision CCL, software `8×8` tile virtual paging, cross-referenced to `DocParse/results`) and ingested ±2 citation triage in [doc_search_results.md](doc_search_results.md).
- **Native Nested HEALPix (`HEALPix.slang` + host `Healpix.hpp/.cpp`):** Implemented 32-bit WGSL-safe `HealpixPixToVec`, `HealpixVecToPix` (pole-stable $1-|z|=(x^2+y^2)/(1+|z|)$), `HealpixNeighbor` (exact 12-face seam tables, 24 seven-neighbor corners), and `HealpixVecToFaceUV` (Calabretta 2007). Verified bit-for-bit via `PlanetGenCheck --checks=healpix` (`orders 1..5` host + `order 6` GPU `HealpixProbeCS`: 0 errors, Full validation).
- **Simulation swap & workload reductions:** Migrated passes 1–6 and `RenderMapCS` (4-tap bilinear elevation + Karlbauer 2024 24-corner bridge + coastal shallows + world-space tangent hillshading) to HEALPix; extracted `TimelapseAgentTools.hpp/.cpp` from `timelapse/main.cpp`; fused `CountFrontPointsCS` into `RemoveSubductedCS`; added lane-0 `32×32` tile cap-culling in `CountLiveCellsCS` + `LiveGroupCounts` zero-skip in `ScatterLiveCellsCS`; added 3-tier register/wave/SMEM `SortFrontPointsCS` + `BuildFrontTileCapsCS` (indirect) + fused `FrontDistanceAndUpliftCS` (500 km ocean vs 1850 km continent workgroup reach).
- **Locked baselines (RTX 4080, `RelWithDebInfo`, bytes):** `order = 7` (`196,608` cells) = **`146.4 µs`** (`0.745 ns/cell`, 30% faster/cell than 100k Fibonacci); `order = 8` (`786,432` cells) = **`443.4 µs`** (`0.564 ns/cell`, faster/cell than 500k Fibonacci). Nsight GPU Trace Runs 6–8 stored in `build/profiler_captures/planetgen_diag.db`.
- **Next:** Phase 2 — Pure-GPU Continental Collision (`Collide.slang` — Allegretti `2×2` BKE CCL, user typing the Slang implementation). See [HANDOFF.md](HANDOFF.md).

#### 2026-11-19 14:35 (Gemini)
- **Packed buffer layout & read-only aliasing (`[ls_resource_tags("Aliased:...")]`):**
  - Coalesced split buffers into 16-byte/8-byte vector buffers: `Owners` (`int4`, updated in-place by `FillGapsCS`, replacing `BestPlate`/`BestCell`/`SecondPlate`/`SecondCell`/`Pass3Plate`/`Pass3Cell`), `CrustFlags` (1-byte bitfield), `CrustState` (`float4`: relief, birth, orogenyTime, `asfloat(orogeny)`), `FrontData` (`float4`: strength, speed, slabAge, `asfloat(subduction)`), `CellFronts` (`int2`: frontPoint, `asint(distance)`), `PlateCounters` (`int4`: positionSum.xyz, cellCount), `PoleRatesIn` (`float4`: pole.xyz, rate), `Rotations` (`3 × float4` per plate).
  - `ManifestDescriptors.cpp` resolves `Aliased:<Target>` tags and enforces a same-dispatch aliasing guard.
- **Shader optimizations (100 000 points, RTX 4080, RelWithDebInfo, bytes):**
  - `OwnersCS`: `groupshared` world-space cap prologue + `Aliased:PlateCapSpread` RO view + skip `LoadCrustStateIn` on continental cells (`47.1 µs → 24.6 µs` bench).
  - `LiveCells.slang`: `CrustFlagsPackedIn` (`WordsIn` alias of `CrustFlags`) loads 4 contiguous cells in one `LDG.E.32`, skips dead chunks with `(packed4 & 0x01010101u) == 0u`, uses `countbits` + uniform `GroupCursor == 0u` early-out (`CountLiveCellsCS` `8.2 µs`, `ScatterLiveCellsCS` `7.8 µs`).
  - `NearestFrontPointCS`: maximizes `dot(worldPoint, frontPoint.xyz)` without per-candidate tie-breaker, recomputing exact chord `asin` once after the loop (`20.5 µs → 14.3 µs` bench; `21.6 µs → 16.9 µs` locked 2205 MHz).
  - `RemoveSubductedCS`: linearized single `STG.128` write to `FrontData`; `FillGapsCS` skips non-gap `Owners` writes and updates `PreviousPlate` in-place.
  - **Bench median:** `106.5 µs` (`96.3 µs` min), down from `339.5 µs` baseline (**3.18× speedup**). Locked-clock Nsight GPU Trace runs 1–5 live in `build/profiler_captures/planetgen_diag.db`.
- **Live multi-step & visualization (`MovePlates.slang`, `RenderMap.slang`, `PlanetGenTimelapse`):**
  - Added `MovePlatesAndClearCountersCS` (1-workgroup, wave-size agnostic): advances `PoleRatesRW` and `RotationsRW` (slab pull + `PlateRiftIn` rift push + basal/keel drag + Rodrigues + Gram-Schmidt) and clears all 8 per-plate/per-pair atomic counters in one dispatch.
  - Added `Params.TimeStepMy` so sub-stepping (`--dt`) scales uplift, erosion, trench area, and plate motion uniformly.
  - Added `RenderMapCS` (`RWTexture2D<float4> MapOutput`) and `PlanetGenTimelapse` wired to `modules/FrameCapture` for headless PNG dumps.
- **Next steps / TODOs:**
  - See [HANDOFF.md](HANDOFF.md) for detailed next steps: coastal shelf/slope elevation pass, GBuffer-style debug visualization modes + `AgentTools` / `FrameCapture` subregion inspection, WebGPU wave-size portability (`Offset*CS`, `LiveCells.slang`), and RenderGraph integration.

## Rifting (2026-11-26)

`Rift.slang` ports `tectonics.Rift` and `AddPlate` from PlanetLab. Before it, plates could only merge, so every long
run ended as one supercontinent on one plate. Three passes run after `ApplyMantlePlumesCS` and before
`MovePlatesAndClearCountersCS`:

1. **`DecideRiftCS`**, one wave, one lane for each plate. Each plate keeps an incubation clock (`PlateIncubation`):
   the mantle under continental crust warms at `dt × min(continental share / 0.1, 1)`, where 0.1 is the area of a
   supercontinent cap (`SUPERCONTINENT_RADIUS` mantle depths). A plate from the start state gets a random head start of
   up to 100 My, as `tectonics.Start` does. A plate can rift when it is warm (100 My), larger than 4 % of the sphere,
   has continental crust, and passes a trial with probability `dt / 40 My`. At most one plate rifts in a step. The new
   half needs a **free slot**: a plate with no live cells. Lane 0 picks two continental cells by hashing cell indices of
   the plate's own grid (not slots of the live list, whose order comes from atomic cursors and would make runs differ),
   and the cut line passes between them. The halves get opposite poles about `first × second` at 5 km/My, and
   `PlateRift` gets the axis and time, so `MovePlatesAndClearCountersCS` applies the rift push (5 km/My for 20 My, then
   20 km/My until 100 My).
2. **`SplitPlateCS`** (indirect, zero groups when no plate rifts) moves every live cell on the far side of the warped
   cut line to the free slot. The warp is the lab's sum of 8 plane waves on each axis, amplitude 0.075. The new plate
   copies the old rotation, so a moved cell keeps its index.
3. **`RefreshOwnersAfterRiftCS`** points the `Owners` and `PreviousPlate` entries of moved cells at the new plate, so
   the map is right on the rift step itself.

The host reserves free slots: `AddRiftSlots` appends up to 8 empty plates (`k_DefaultRiftSlots`, never past
`k_MaxPlateSlots` = 32), and plates that lose all their crust free their slots too. `--rift=off` turns rifting off.
`--verify` and `--self-verify` always turn it off, so the parity checks still compare like with like. The random
choices hash the step time, so a run repeats exactly.

Cost at order 7 (earth preset, RTX 4080): `DecideRiftCS` 3.1 µs, `SplitPlateCS` 1.7 µs and
`RefreshOwnersAfterRiftCS` 2.1 µs (median, mostly the cost of a dispatch), on a sim of about 217 µs.

Effect (earth preset, seed 42, order 7, dt 0.5): the largest landmass at 200 My falls from 20.9 % to 10.1 % of the
sphere, and coast_D rises from 1.26 to 1.32. Land area still grows (26.6 % at 0 My, 37 % at 400 My), because arc
accretion adds continent and nothing removes it.

## The atlas layer (mode 7, 2026-11-26)

Modes 0 to 6 are for agents and debugging: `planet_diagnostics.py` classifies their colours, so they keep them. Mode 7
(`--mode=atlas`, `planet.set_layer atlas`, "7: Atlas" in the live UI) is a map for people:

- **One continuous ramp** by elevation in km, from -6.5 (abyss, navy) through 0 (shelf cyan, then lowland green) to 8
  (white). The same colour always means the same height, and nothing snaps between bands.
- **Hillshade** with the cartographic convention: sun from the north-west, 45° high, relief exaggerated 25× on land
  and 8× at sea.
- **A coastline** at elevation 0, about one pixel wide, and faint **contours** every km.
- **A graticule** every 30°, the equator a little stronger.
- **A scale bar** at the bottom left: -6.5 km at the left end to 8 km at the right, a tick every km, and a tall white
  tick at sea level.

It shows the raw simulation (no ShapingParams noise), so what you see is what the crust arrays hold.

## The landscape budget: diffusion and orogenic collapse (2026-11-26)

Before this pass the only sink for relief was `ContinentErosion` (relief × (1 − 0.06 dt / 10 km) each step, Cortial
2019), and uplift outran it: on the earth preset the snow area (above about 6 km) grew to 8.3 % of the sphere by
400 My, and most continental interiors became plateaus. `Erosion.slang` adds two terms on land (the crust above sea
level). They are the first, cheap stand-in for real erosion:

- **Diffusion of land height.** `h' = h + κ Σ (h_n − h)` over the 4 edge neighbours in the plate's own HEALPix grid,
  with `h = max(elevation, 0)` and `κ = D dt / dx²`. Mountains widen into foothills and lose height. Land at the coast
  wears down toward sea level, and that sediment leaves the model (no shelf deposit yet). A neighbour that is dead in
  the plate frame is the plate edge: no flux crosses it. `D` is `ErosionDiffusivity`, 500 km²/My by default
  (`√(D t)` is about 220 km after 100 My).
- **Orogenic collapse.** Elevation above `CollapseElevationKm` (5 km) relaxes toward it with time constant
  `CollapseTimeMy` (2 My). Thick crust flows sideways under its own weight, which is why real plateaus stall near
  5 km (Tibet, the Altiplano).

`ErodeReliefCS` computes the new relief of every live cell from the relief of the step before, into `ErodedRelief`
(by live-list slot), and `ApplyErosionCS` copies it back, so no cell reads a neighbour that this step already changed.
Volcanic cells (`OrogenyVolcanic`) keep the erosion of `Plumes.slang`. The passes run after `ApplyMantlePlumesCS`
and before the rift passes.

The four values are in `ShapingParams` (and the reflected `planet.shaping` object). `ErosionEnabled` is false in every
shaping preset, so `LegacyShapingState` and the check harness are unchanged; PlanetGenLoom turns it on
(`--erosion=on|off`, on by default), and `--verify` and `--self-verify` force it off.

**Limit:** explicit diffusion needs `κ ≤ 0.25`, so the pass clamps it at 0.24. At order 7 (dx ≈ 51 km, dt 0.5 My)
κ is 0.096. At order 8 (dx ≈ 25 km) the nominal κ is 0.39, so the clamp cuts the effective `D` to about 62 %. Two
substeps would fix it.

Cost (RTX 4080, GPU idle): `ErodeReliefCS` 8.2 µs and `ApplyErosionCS` 5.1 µs at order 7; 15.4 and 9.2 µs at order 8.

Measured on the earth preset, seed 42, order 7, dt 0.5, rifting on (`planet_diagnostics.py`, percent of the sphere):

| Run, 400 My | Land | Highland | Snow | Largest landmass |
|---|---|---|---|---|
| No erosion | 37.3 | 26.0 | 8.30 | 28.9 |
| Diffusion, collapse τ 5 My | 37.0 | 28.6 | 0.73 | 33.3 |
| Collapse τ 2 My | 37.3 | 26.3 | 0.00 | 23.3 |
| τ 2, reach 800 / 500 km | 36.1 | 23.5 | 0.04 | 19.2 |
| τ 2, reach 800 / 500 km, `ContinentErosion` 0.25 | 34.8 | 8.9 | 0.00 | 17.4 |

Collapse removes the snow plateaus. The broad highlands come from the uplift reach (`ContinentalReachKm` 1800 and
`CollisionReachKm` 900 put most continental area inside some uplift zone) and from a weak `ContinentErosion` (0.06 is
an e-folding of about 170 My). With the last row the mountains read as belts on low continents. Those three values
are not changed in any preset yet. Land still grows (27 % to 35–37 %): arc accretion adds continent and nothing
removes it.

Next: a stream power law on a flow network (`DocParse/results`: Jain 2024 FastFlow, Barnes 2019 parallel FastScape,
Tzathas 2024 analytical erosion, Schott 2023), Airy isostasy (Braun 2018: the surface falls only about 17 % of the
eroded thickness), sediment on the shelves, and a continental sink (subduction erosion).

### Hypsometry against Earth (2026-11-26)

`--mode=raw` (mode 8) writes the simulated elevation into the red and green bytes:
`km = 22 * (R * 256 + G) / 65535 - 11`. Scripts decode it and weight each row by cos(latitude). Share of **land** area
in each band, earth preset (seed 42, order 7, dt 0.5, rifting and erosion on). Earth is approximate, from the standard
hypsographic curve (ETOPO1):

| | Land % of sphere | Mean land (km) | Mean sea (km) | 0–0.5 km | 0.5–1 | 1–2 | 2–3 | > 3 |
|---|---|---|---|---|---|---|---|---|
| Earth | 29 | 0.84 | −3.7 | 52 | 19 | 18 | 7 | 4 |
| Old values, 400 My | 35.3 | 2.43 | −4.08 | 7 | 10 | 26 | 24 | 34 |
| Calibrated, legacy shaping, 400 My | 37.2 | 1.00 | −3.91 | 13 | 53 | 27 | 4 | 3 |
| Earth shaping preset, 200 My | 30.0 | 1.04 | −4.07 | 13 | 49 | 29 | 6 | 3 |
| Earth shaping preset, 400 My | 35.3 | 1.00 | −3.96 | 11 | 46 | 39 | 4 | 0.2 |

The calibrated values (`ContinentErosion` 0.25, `ContinentalReachKm` 800, `CollisionReachKm` 500; now in the earth
and atoll shaping presets) bring mean land elevation from 2.4 km to about 1.0 km (Earth 0.84). Two gaps remain:

- **Too little low land** (11–13 % below 0.5 km, Earth 52 %). Continental crust has a fixed base elevation of 0.5 km,
  so land can only sit at 0.5 km or above, except at coasts that erosion has worn down. A thickness field with basins
  and thermal subsidence is the fix.
- **Too much land over time** (30 % at 200 My, 35 % at 400 My). Arc accretion adds continent and nothing removes it.

## Loom baseline: time and memory (2026-11-26)

Nsight GPU Trace of `PlanetGenLoom --preset=earth --shaping=earth --dt=0.5 --plumes=default --timestamps`, one submit
after 400, with rifting and erosion on, 28 plate slots (20 plates + 8 rift slots). Runs 9 (order 7) and 10 (order 8)
in `build/profiler_captures/planetgen_diag.db`. The trace locks the GPU clocks to base, so these times are higher
than bench times; compare traces with traces. Runs 1 to 8 traced `PlanetGenBench` (the PlanetStep path, no plumes,
rifting, erosion or map), so they are not the same workload.

| | Order 7 (196,608 cells) | Order 8 (786,432 cells) |
|---|---|---|
| Simulation, 38 dispatches | 300 µs | 902 µs |
| `FrontDistanceAndUpliftCS` | 116 µs (39 %) | 519 µs (58 %) |
| `OwnersCS` | 23 µs | 66 µs |
| `ApplyMantlePlumesCS` | 19 µs | 63 µs |
| Collision phase, 16 dispatches, no collision in the step | 29 µs | 34 µs |
| Dispatches under 3 µs | 20, 31 µs in all | 19, 29 µs in all |
| `RenderMapCS` (earth shaping noise, 1024 × 512) | 507 µs | 506 µs |

Findings, largest first:

1. **`RenderMapCS` runs every step and costs more than the simulation at order 7.** With the earth shaping noise it
   is 507 µs at any order. A bake loop needs the map only on the steps it captures.
2. **`FrontDistanceAndUpliftCS` grows 4.5× for 4× the cells.** The group cull reads
   `max(ContinentalReachKm * 1.45 + 50, 1850)` and `max(IslandArcReachKm * 1.45 + 50, 500)`, so the fixed floors
   (1850 km, 500 km) win since the presets use an 800 km reach, and every group searches further than any uplift
   reaches. The hot lines (run 10) are the `Nearest` compare (`FrontPoints.slang:30`, 12.7 % of the samples: WAIT,
   NOTSEL, MATHTH), so the kernel is bound on instruction issue: fewer candidates is the lever, not latency.
3. **About 30 µs of fixed cost each step.** An empty dispatch with its barrier costs about 1.25 µs. The 16 collision
   dispatches cost 29 to 34 µs in a step with no collision, through zero-group indirect dispatches.
4. **Memory: the plate-cell buffers are sized for the worst case.** Each world point has one top plate plus a few
   buried cells, so live cells are near N, about 1/28 of P × N:

| Buffer | Lifetime | Order 7 | Order 8 |
|---|---|---|---|
| `CrustState` (P × N × 16 B) | imported | 84 MiB | 336 MiB |
| `CrustFlags` (P × N × 1 B) | imported | 5 MiB | 21 MiB |
| `CellFronts` (P × N × 8 B) | frame, live in interactive mode | 42 MiB | 168 MiB |
| `LiveCell` (P × N × 4 B) | frame | 21 MiB | 84 MiB |
| `ErodedRelief` (P × N × 4 B) | frame | 21 MiB | 84 MiB |
| Per-point buffers (Owners, FrontData, FrontPoints, suture, BKE, PreviousPlate) | mixed | 15 MiB | 59 MiB |
| **Total** | | **188 MiB** | **752 MiB** |

`CellFronts`, `LiveCell` and `ErodedRelief` are indexed by the live list or written only for live cells, so a
capacity of about 1.5 N with an overflow check would take 392 MiB to about 45 MiB at order 8, with no paging. Sparse
pages are the answer for `CrustState` and `CrustFlags`. Loom aliasing has less to win after that resize: the
collision buffers (BKE, suture) and `ErodedRelief` never live at the same time, about 20 MiB at order 8.

**After the first two fixes (same day).** The bake loop (`RunLoomSimulation`) now renders the map only on its last
step (`RenderOnTrigger`), which removes 507 µs from every other step. The group cull now uses the largest reach the
uplift can use (slab-dip clamp, and `CollisionReachKm` for continents) in place of the 1850 / 500 km floors; it is
bit-exact with the legacy shaping, and gave 0 differing pixels over 200 My on the earth preset. On the same world
(runs 13 and 14 with the old floors, 11 and 12 with the new), `FrontDistanceAndUpliftCS` went from 110 to 111 µs at
order 7 (no change) and from 713 to 652 µs at order 8 (−9 %). Most of its cost is candidates inside the group's own
plate, not tiles beyond reach; the `Nearest` compare is still the lever.

**Repeatability.** The first `DecideRiftCS` picked cells from live-list slots, and the live list's order comes from
atomic cursors, so two runs of one build diverged by 200 My. It now hashes cell indices of the plate's own grid; two
runs agree to the pixel at 100 and 200 My. Any new pass must not depend on the order of `LiveCell` or `FrontPoints`.
