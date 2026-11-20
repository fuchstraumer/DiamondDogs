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
- **`PlanetGenTimelapse`**: headless multi-step runner using `PlanetStep::RecordLiveStep` + `RenderMapCS` (6 debug modes: `elevation`, `plates`, `age`, `relief`, `fronts`, `voronoi`) + `modules/FrameCapture` + `modules/AgentTools` (`--tool-port=0` JSON-RPC server exposing `planet.sim`, `planet.step`, `planet.reset`, `planet.capture`, `planet.capture_region`, `planet.quit`).

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

## 3. Performance Baseline (RTX 4080, 20 plates, RelWithDebInfo, bytes)

- **100k points (`PlanetGenBench`, `--steps=500 --warmup=500 --dispatches`):**
  - **Step:** `106.5 µs` median (`96.3 µs` min), down from `339.5 µs` baseline (**3.18× speedup**).
  - `OwnersCS`: `24.6 µs` | `CountLiveCellsCS`: `8.2 µs` | `ScatterLiveCellsCS`: `7.8 µs` | `NearestFrontPointCS`: `14.3 µs` | `UpliftCS`: `7.2 µs`.
- **500k points (`build/reference_500000`, `--steps=200 --warmup=200 --dispatches`):**
  - **Step:** `290.8 µs` median (`284.7 µs` min) — **2.73× time for 5× points** (~3,400 ticks/sec!).
  - `OwnersCS`: `76.8 µs` | `CountLiveCellsCS`: `25.6 µs` | `ScatterLiveCellsCS`: `23.4 µs` | `NearestFrontPointCS`: `39.9 µs` | `UpliftCS`: `66.6 µs` (jumped 9.5× due to 240 MB `CrustState`+`CellFronts` spilling the 4080's 64 MB L2; moving `LoadCrustState` below `if (frontPoint < 0 && !isContinental) return;` will cut DRAM reads too).
- **Locked-clock Nsight GPU Trace (`build/profiler_captures/planetgen_diag.db`, 2205 MHz base clock):**
  - Run 1 (`161.8 µs`) → Run 2 (`137.0 µs`) → Run 3 (`130.0 µs`, post-packing) → Run 5 (`127.0 µs`, dot-product `NearestFrontPointCS` before `CrustFlagsPackedIn`).
  - Occupancy is NOT register-limited (`RegStall = 0–1%`); 100k threads (3,125 warps) is simply smaller than one full wave of an RTX 4080 (76 SMs × 48 warps = 3,648 warps).
  - Note: `vkCmdCopyBuffer` + `vkCmdFillBuffer` in `RecordReset` are benchmark/check-only; in a live loop, `MovePlatesAndClearCountersCS` clears the 8 small counters in ~2 µs with zero copies.

---

## 4. Visual Findings & 500k Timelapse (`build/planet_500k_timelapse.mp4`)

1. **Debug layers (`build/debug_layers/{plates,age,fronts}` & `build/tool_test/region.png`)**: Zero Z-fighting or simulation bugs; seafloor spreading stripes, subduction trenches, and `NearestFrontPointCS` halos are completely clean.
2. **500k 10-second timelapse (`build/planet_500k_timelapse.mp4`, 300 frames @ 30 fps, `dt = 0.5 My`, `1920×960`):** Ran 150 My of 100% GPU-only plate drift with zero NaNs/drift, crisp coastlines, island arc accretion, and triple-junction spreading — **and exposed the #1 missing physics feature**:
   - **Continental collisions currently ghost through each other:** In `RemoveSubductedCS`, `collision == true` sets `subduction = 0` and leaves `underFlat` alive, because `tectonics.Collide()` / `TransferTerrane()` (`applications/PlanetLab/planetlab/tectonics.py:269-379`) is not yet on the GPU.
3. **No coastal shallows yet:** Python's `tectonics.Elevation()` (`tectonics.py:577-607`) computes coastal shelves (`-0.15 km`) and slopes (`-1.5 km`) via `coast_distance` as a post-pass.

---

## 5. Prioritized Next Steps

1. **Immediate Next Task — Pure-GPU Continental Collision & Terrane Transfer (`Collide`):**
   - Design and prototype a **GPU connected-component flood-fill / label-propagation** (e.g. CUDA-style Shiloach-Vishkin / pointer-jumping union-find or frontier BFS on the Fibonacci sphere neighbor graph) to detect colliding continental terranes (`TERRANE_PENETRATION = 300 km`), transfer terranes to the overriding plate, and apply Himalayan orogeny uplift (`OrogenyHimalayan = 2`, surge).
   - Keep everything 100% on the GPU (no CPU readback).
2. **Visualization Lookdev & Upscaling:**
   - Domain-warped 4-corner Fibonacci IDW interpolation + 3D fBm detail in `RenderMapCS` for crisp 1440p/4K coastlines + automatic coastal shelf/slope blend + optional Slang `[Differentiable]` / `fwd_diff` normals & curvature hillshading.
3. **WebGPU Portability & RenderGraph:**
   - Make `OffsetLiveCellsCS`, `OffsetFrontPointsCS`, `CountLiveCellsCS`, and `ScatterLiveCellsCS` wave-size agnostic (16 / 32 / 64 lanes, `PlateCount > waveSize`, matching `OceanFft` IDFT).
   - Migrate bindings/barriers from `WriteManifestDescriptors` into `modules/RenderGraph` for the upcoming WebGPU RHI.
