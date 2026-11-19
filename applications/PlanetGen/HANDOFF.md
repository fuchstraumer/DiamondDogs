# PlanetGen Handoff (2026-11-19 14:35, Gemini)

Current state, performance baselines, buffer contracts, and prioritized next steps for `applications/PlanetGen` and `assets/shaders/compute/Planet/`.

---

## 1. Current Architecture & Targets

`applications/PlanetGen/CMakeLists.txt` builds:
- **`planet_gen_shaders`**: cooks `assets/shaders/compute/Planet/PlanetLab.slang` via Lodestone (**17 entrypoints × 2 variants (`CRUST_FLAG_BYTES = true|false`) = 34 entrypoint variants**).
- **`planet_gpu`** (static lib): `PlanetSession`, `PlanetShaders` (supports 1D/2D/indirect dispatch + storage images), `GpuBuffers` (storage buffers, host-visible UBOs with `UpdateUniform`/`UpdateUniformFrom`, storage images), `PlanetStep` (`RecordStep`, `RecordLiveStep`, `RecordReset`, `RecordDispatch2D`), `Healpix`.
- **`planet_reference`** (static lib): `ReferenceSet`, `PlanetMath`, `StepState` (`AddStepBuffers` + pack-on-upload helpers).
- **`PlanetGenCheck`**: validates all 9 checks (`map`, `owners`, `front`, `crust`, `live`, `distance`, `uplift`, `torque`, `chain`) in both `[bytes]` and `[words]` with `Full` Vulkan validation. Currently **100% PASS** on both `reference_10000` and `reference_100000`.
- **`PlanetGenBench`**: headless GPU-timer benchmark (`RelWithDebInfo`).
- **`PlanetGenTimelapse`**: headless multi-step runner using `PlanetStep::RecordLiveStep` + `RenderMapCS` + `modules/FrameCapture` (`RGBA8_UNORM` storage image → background-threaded PNG writer).

---

## 2. Packed GPU Buffer Layout (`Buffers.slang`)

All read-only/aliased views use `[ls_resource_tags("Aliased:<Target>")]`, resolved by `modules/RhiResources/src/ManifestDescriptors.cpp` (which also rejects same-dispatch aliasing of two bindings to the same `VkBuffer`).

| Backing Buffer | Type / Stride | Layout | Aliased Views |
|---|---|---|---|
| `CrustFlags` / `CrustFlagsWords` | `uint8_t` / `uint` (`P × N`) | `bit 0` = alive, `bit 1` = continental | `CrustFlagsIn` (`FlagBytesIn`), `CrustFlagsPackedIn` (`WordsIn`, 4 cells/load in `LiveCells.slang`), `CrustFlagsWordsIn` (`WordsIn`) |
| `CrustState` | `float4` (`P × N`) | `.x` = relief, `.y` = birth, `.z` = orogenyTime, `.w` = `asfloat(orogenyType)` | `CrustStateIn` (`PointsIn`) |
| `Owners` | `int4` (`N`) | `.x` = BestPlate, `.y` = BestCell, `.z` = SecondPlate, `.w` = SecondCell (updated in-place by `FillGapsCS`) | `OwnersIn` (`IntPointsIn`) |
| `FrontData` | `float4` (`N`) | `.x` = strength, `.y` = speed, `.z` = slabAge, `.w` = `asfloat(subduction)` | `FrontDataIn` (`PointsIn`) |
| `CellFronts` | `int2` (`P × N`) | `.x` = frontPoint (`-1` if none), `.y` = `asint(distanceKm)` | — |
| `PlateCounters` | `int` atomic (`4 × P`) | `xyz` = fixed-point positionSum (`×16384`), `w` = cellCount | `PlateCountersIn` (`IntPointsIn`) |
| `PlatePull` | `int` atomic (`3 × P`) | fixed-point pull vector (`×4096`) | — |
| `PlateContinentalCount` | `uint` atomic (`P`) | continental live cell count | — |
| `Rotations` | `float4` (`3 × P`) | 3 rows of plate-to-world 3×3 matrix (`.w = 0`) | `RotationsRW` (`Points`, `MovePlatesAndClearCountersCS`) |
| `PoleRatesIn` | `float4` (`P`) | `.xyz` = Euler pole, `.w` = rate (rad/My) | `PoleRatesRW` (`Points`, `MovePlatesAndClearCountersCS`) |
| `PlateRiftIn` | `float4` (`P`) | `.xyz` = rift axis (zero if inactive), `.w` = rift time (My) | — |
| `PreviousPlate` | `int` (`N`) | carried buffer updated in-place by `FillGapsCS` | `PreviousPlateIn` (`IntsIn`, `OwnersCS`) |
| `PlateCapCenters` / `PlateCapSpread` | `float4` / `uint` atomic (`P`) | carried plate bounding caps (`w`: 0=none, 1=valid, 2=empty) | `PlateCapSpreadIn` (`WordsIn`, `OwnersCS`) |
| `PairVotes` / `LowVotes` | `uint` atomic (`P × P`) | ocean-overlap majority votes | `PairVotesIn`, `LowVotesIn` (`WordsIn`) |
| `PlateFrontPointCount` | `uint` atomic (`P`) | subduction front point count per plate | `PlateFrontPointCountIn` (`WordsIn`) |
| `MapOutput` | `RWTexture2D<float4>` (`W × H`) | `RGBA8_UNORM` storage image (`UavComputeShader \| TransferSource`), `GENERAL` layout | — |

**Uniforms:**
- `Params` (`OwnersParams`): `{ uint PointCount; uint PlateCount; float TimeStepMy; }`
- `Pass3Constants`, `UpliftConstants`, `MovePlatesConstants`: `{ float Time; }`
- `RenderMapConstants`: `{ uint Width; uint Height; float Time; }`

---

## 3. Performance Baseline (RTX 4080, 100k points, 20 plates, RelWithDebInfo, bytes)

- **Unlocked `PlanetGenBench` (`--steps=500 --warmup=500 --dispatches`):**
  - **Step:** `106.5 µs` median (`96.3 µs` min), down from `339.5 µs` baseline (**3.18× speedup**).
  - `OwnersCS`: `24.6 µs` | `CountLiveCellsCS`: `8.2 µs` | `ScatterLiveCellsCS`: `7.8 µs` | `NearestFrontPointCS`: `14.3 µs` | `UpliftCS`: `7.2 µs`.
- **Locked-clock Nsight GPU Trace (`build/profiler_captures/planetgen_diag.db`, 2205 MHz base clock):**
  - Run 1 (`161.8 µs`) → Run 2 (`137.0 µs`) → Run 3 (`130.0 µs`, post-packing) → Run 5 (`127.0 µs`, dot-product `NearestFrontPointCS` before `CrustFlagsPackedIn`).
  - Occupancy is NOT register-limited (`RegStall = 0–1%`); 100k threads (3,125 warps) is simply smaller than one full wave of an RTX 4080 (76 SMs × 48 warps = 3,648 warps).
  - Note: `vkCmdCopyBuffer` + `vkCmdFillBuffer` in `RecordReset` are benchmark/check-only; in a live loop, `MovePlatesAndClearCountersCS` clears the 8 small counters in ~2 µs with zero copies.

---

## 4. Visual Findings from First `RenderMapCS` Dump (`build/timelapse_test/frame_0000.png`)

1. **No coastal shallows (and bright mid-ocean ridges / dark nearshore ocean):**
   - Physically, young ocean crust at mid-ocean ridges (`-2.6 km`) *is* shallower than old ocean crust near continents (`-6.0 km`, Parsons & Sclater $d = 2.6 + 0.345\sqrt{\text{age}}$).
   - Coastal shelves (`-0.15 km`) and continental slopes (`-1.5 km`) are **not** in the raw crust state — Python's `tectonics.Elevation()` (`applications/PlanetLab/planetlab/tectonics.py:577-607`) computes them as a post-pass using `coast_distance` (`COAST_BLEND_DISTANCE = 300 km`, `SLOPE_BLEND_DISTANCE = 200 km`) plus a Gaussian trench depression (`TRENCH_DEPTH = 3.5 km`, `TRENCH_WIDTH = 90 km`).
2. **Ridge/trough stair-stepping & distortion:**
   - At `N = 100,000`, each Fibonacci Voronoi cell is ~71 km wide, and `RenderMapCS` currently does nearest-neighbor lookup (`FibonacciNearest`). Higher-resolution dumps (`2048×1024` / `4096×2048`) + debug layers are needed to separate Voronoi sampling aliasing from any `OwnersCS` / `FillGapsCS` boundary artifacts.

---

## 5. Prioritized Next Steps

1. **Visualization Debugging & Inspection:**
   - Add debug render modes (`RenderMapConstants.Mode`: elevation, plate IDs, ocean age, relief, subduction/collision fronts, `SecondPlate` overlaps, Voronoi cell index) — GBuffer-style debug layers.
   - Integrate `modules/AgentTools` (`ToolServer` / `ToolRegistry`) + `FrameCapture` subregion/downsample captures (`CaptureSource::OffsetX/Y/Width/Height/Downsample`) so agents can inspect downsampled full-planet maps and zoom into full-res problem regions along ridges/trenches.
   - Port `tectonics.Elevation()`'s coastal shelf/slope blend + smooth trench profile (and optional multi-neighbor interpolation in `RenderMapCS` to eliminate Voronoi stair-stepping).
2. **WebGPU Portability:**
   - Make `OffsetLiveCellsCS`, `OffsetFrontPointsCS`, `CountLiveCellsCS`, and `ScatterLiveCellsCS` wave-size agnostic (supporting subgroup sizes 16 / 32 / 64 and `PlateCount > waveSize`, matching the `OceanFft` IDFT pattern).
   - Audit `CRUST_FLAG_BYTES` fallback (`Words`) and storage buffer limits for WebGPU.
3. **RenderGraph & Interactive Editor:**
   - Migrate binding/barriers from `WriteManifestDescriptors` + manual barriers into `modules/RenderGraph` (also clarifying the command/resource surface needed for the WebGPU RHI).
   - Add timeline keyframe ringbuffer (~34.4 MB/keyframe every ~20–50 ticks; <2 ms GPU resim on scrub) and art-direction controls (constant-buffer shaping parameters, per-plate overrides, mantle plumes, interactive brushes).
4. **Validation of `MovePlatesAndClearCountersCS`:**
   - Add a `PlanetGenCheck` unit check comparing GPU `MovePlatesAndClearCountersCS` output (`PoleRatesRW`, `RotationsRW`) against Python `tectonics.MovePlates` (including `rift_axis` / `rift_time` once exported by `write_gpu_reference.py`).
