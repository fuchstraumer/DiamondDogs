# PlanetGen Handoff (2026-11-20 13:00, Gemini)

Current state, performance baselines, buffer contracts, and prioritized next steps for `applications/PlanetGen` and `assets/shaders/compute/Planet/`.

---

## 1. Current Architecture & Targets (PlanetGen v2 — Native Nested HEALPix)

**Phase 1 (Native Nested HEALPix + Workload Reductions) is COMPLETE and LOCKED.** See [planetgenv2.md](planetgenv2.md) for the full v2 architectural blueprint and [doc_search_results.md](doc_search_results.md) for the ±2 citation triage (Karlbauer 2024, Bolelli/Allegretti 2024, Benyoub/Dupuy 2024 CBT, Westerteiger 2012, cuHPX 2025, Calabretta 2007, GPU Pro 5/7).

`applications/PlanetGen/CMakeLists.txt` builds:
- **`planet_gen_shaders`**: cooks `assets/shaders/compute/Planet/PlanetLab.slang` via Lodestone (**21 entrypoints × 2 variants (`CRUST_FLAG_BYTES = true|false`) = 42 entrypoint variants**).
- **`planet_gpu`** (static lib): `PlanetSession`, `PlanetShaders`, `GpuBuffers`, `PlanetStep` (14 step / 15 live-step entrypoints, indirect dispatch for `LiveDispatch` and `FrontTileDispatch`), `Healpix` (`HealpixNestedIndex`, `HealpixPixToVec`, `HealpixNeighbors8`).
- **`planet_reference`** (static lib): `ReferenceSet`, `PlanetMath` (incl. `FibonacciNearest`), `StepState` (`AddStepBuffers`, `AddHealpixStepBuffers` resampling onto HEALPix `order = 4..9`).
- **`PlanetGenCheck`**: `--checks=healpix` verifies C++ host (`orders 1..5`: 0 bijection/range/symmetry/corner errors) + GPU `HealpixProbeCS` (`order = 6`, 49,152 pixels: 0 vec/round-trip/neighbor errors) in both `[bytes]` and `[words]` with `Full` Vulkan validation.
- **`PlanetGenBench`**: headless GPU-timer benchmark (`RelWithDebInfo`), supports `--order=<4..9>` (default `7` = 196,608 cells).
- **`PlanetGenTimelapse`**: headless multi-step runner (`--order=<4..9>`) + `RenderMapCS` (Calabretta 2007 continuous `(faceU, faceV)` + Karlbauer 2024 24-corner bridge 4-tap bilinear elevation + coastal shallows + world-space tangent hillshading in mode 0; crisp nearest-neighbor debug modes 1–5) + `FrameCapture` + `AgentTools` extracted into `timelapse/TimelapseAgentTools.hpp/.cpp`.

---

## 2. Step Pipeline & Packed GPU Buffer Layout (`Buffers.slang`)

**Locked Step Order (`PlanetStep.cpp`):**
1. `OwnersCS` → 2. `CountVotesCS` → `SwapToMajorityCS` → 3. `FillGapsCS` → `RemoveSubductedCS` *(fused `CountFrontPointsCS`)* → 3b. `CountLiveCellsCS` *(lane-0 HEALPix `32×32` tile cap-cull)* → `OffsetLiveCellsCS` → `ScatterLiveCellsCS` *(`LiveGroupCounts == 0` early-out)* → 4. `OffsetFrontPointsCS` *(writes `FrontTileDispatch`)* → `ScatterFrontPointsCS` → `SortFrontPointsCS` *(3-tier register → wave butterfly → SMEM bitonic sort on `uint` Morton keys, ≤ 2048/plate)* → `BuildFrontTileCapsCS` *(indirect)* → 6. `SumPullCS` → 4+5. `FrontDistanceAndUpliftCS` *(fused `NearestFrontPointCS` + `UpliftCS`, indirect via `LiveDispatch`, precomputed `FrontTileCaps` cull + Ocean 500 km vs Continent 1850 km reach cull)* → *(live loop)* `MovePlatesAndClearCountersCS`.

**New v2 Buffers (added to `Buffers.slang` + `k_ZeroedBuffers`):**
- `LiveGroupCounts` (`Words`, `LiveGroupCount(size)` uints)
- `FrontTileCaps` (`Points`, `((PointCount + 63) / 64)` float4s: `.xyz = tileCenter`, `.w = tileAngle`)
- `FrontTileDispatch` (`Words`, 3 uints indirect dispatch args)

**Uniforms:**
- `Params` (`OwnersParams`): `{ uint PointCount; uint PlateCount; float TimeStepMy; uint HealpixOrder; }`
- `Pass3Constants`, `UpliftConstants`, `MovePlatesConstants`: `{ float Time; }`
- `RenderMapConstants`: `{ uint Width; uint Height; float Time; uint Mode; }`
- `HealpixProbeConstants`: `{ uint Order; uint PixelCount; }`

---

## 3. Performance Baseline (RTX 4080, 20 plates, RelWithDebInfo, bytes)

| Configuration | Cells | Step (median) | **ns / cell** | Key Pass Medians |
| :--- | ---: | ---: | :---: | :--- |
| Fibonacci `100k` (v1) | 100,000 | `106.5 µs` | `1.065 ns` | `OwnersCS 24.6`, `Nearest 14.3`, `Uplift 7.2` |
| **HEALPix `order=7`** | **196,608** | **`146.4 µs`** *(min `143.1`)* | **`0.745 ns`** *(-30%/cell!)* | `OwnersCS 19.5`, `CountLive 13.3`, `ScatterLive 9.5`, `SortFront 13.3`, `FrontDist+Uplift 38.9` |
| Fibonacci `500k` (v1) | 500,000 | `290.8 µs` | `0.582 ns` | `OwnersCS 76.8`, `Nearest 39.9`, `Uplift 66.6` |
| **HEALPix `order=8`** | **786,432** | **`443.4 µs`** *(min `432.1`)* | **`0.564 ns`** *(-3%/cell!)* | `OwnersCS 55.8`, `CountLive 32.8`, `ScatterLive 22.3`, `SortFront 16.4`, `FrontDist+Uplift 229.4` |

- **Profiler DB (`build/profiler_captures/planetgen_diag.db`):** Runs 1–5 (Fibonacci v1), Run 6 (initial HEALPix `order=7`), Run 7 (`151.07 µs` locked), Run 8 (`SortFrontPointsCS` `11.3 µs`, `CountLiveCellsCS` `12.0 µs`, `FrontDistanceAndUpliftCS` warp samples `-16%`).
- **LDS Bank-Conflict Audit:** Clean across all shaders (0 runtime bank conflicts; `SharedWorldCaps` and `SharedFrontPoints` are uniform-broadcast reads; `SharedSortPixels` is 4-byte `uint`).

---

## 4. Prioritized Next Steps

1. **Immediate Next Task — Phase 2: Pure-GPU Continental Collision (`Collide.slang`):**
   - **Important user preference:** The user wants to **type the `Collide.slang` shader implementation themselves** (we provide the architecture, walkthroughs, code review, and C++ host/test plumbing).
   - Design in [planetgenv2.md](planetgenv2.md) §3 + **Bolelli et al. (2024)** `IC`/`WLP` refinements from [doc_search_results.md](doc_search_results.md):
     - Indirect-gated `DetectCollisionsCS` + `SelectCollisionPairCS` (`CollideDispatch`, ~5–10 µs on non-collision ticks).
     - Allegretti/Bolelli **`2×2` Block Equivalence (BKE)** CCL on the 12 HEALPix Nested faces (`16×16` tile `groupshared` union-find + seam stitching via `HealpixNeighbor` — zero neighbor buffer!).
     - `TransferTerraneDirectCS`, `ClearSourceTerraneCS`, 1-dispatch tiled `NearestSuturePointCS`, 64-bin `EscapeHistogramCS`, and `ApplyEscapeAndSurgeCS` (`OrogenyHimalayan = 2`).
   - Add a GPU-vs-CPU BFS exactness check (`--checks=collide`) in `PlanetGenCheck` before running a 300-frame `order = 7/8` collision timelapse!
2. **Phase 3 — Software Sparse Virtual Plate Paging (`planetgenv2.md` §4) & `Loom` (`modules/RenderGraph`) Integration:**
   - Replace dense `P × N` arrays with `8×8` tile `PageTable` + lock-free 2-phase `FreeTileStack` (`~40.5 MB` at `order = 8`, 100% L2-resident, zero `VK_QUEUE_SPARSE_BINDING`, WebGPU-compatible).
   - Port `PlanetStep` + `Collide` + `RenderMapCS` onto `modules/RenderGraph` (`Loom` once indirect dispatch lands) and retire `WriteManifestDescriptors`.
