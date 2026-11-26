# PlanetGen Handoff (2026-11-25, Gemini)

Current state, performance baselines, buffer contracts, and prioritized next steps for `applications/PlanetGen` and `assets/shaders/compute/Planet/`.
See also the running agent handbook at [docs/planet-gen-agent.md](../../docs/planet-gen-agent.md) (including lithology/Megascans design notes).

---

## 1. Current Architecture & Targets (PlanetGen v2 — Native Nested HEALPix)

**Phase 1 (Native Nested HEALPix + Workload Reductions) and Phase 2 (Pure-GPU Continental Collision `Collide.slang`) are COMPLETE and VERIFIED.** See [planetgenv2.md](planetgenv2.md) for the full v2 architectural blueprint and [doc_search_results.md](doc_search_results.md) for the ±2 citation triage (Karlbauer 2024, Bolelli/Allegretti 2024, Benyoub/Dupuy 2024 CBT, Westerteiger 2012, cuHPX 2025, Calabretta 2007, GPU Pro 5/7).

`applications/PlanetGen/CMakeLists.txt` builds:
- **`planet_gen_shaders`**: cooks `assets/shaders/compute/Planet/PlanetLab.slang` via Lodestone (**37 entrypoints × 2 variants (`CRUST_FLAG_BYTES = true|false`) = 74 entrypoint variants**).
- **`planet_gpu`** (static lib): `PlanetSession`, `PlanetShaders`, `GpuBuffers`, `PlanetStep` (30 step / 31 live-step entrypoints, indirect dispatch for `LiveDispatch`, `FrontTileDispatch`, and 4-slot `CollideDispatch`), `Healpix` (`HealpixNestedIndex`, `HealpixPixToVec`, `HealpixNeighbors8`).
- **`planet_reference`** (static lib): `ReferenceSet`, `PlanetMath` (incl. `FibonacciNearest`), `StepState` (`AddStepBuffers`, `AddHealpixStepBuffers` resampling onto HEALPix `order = 4..9`).
- **`PlanetGenCheck`**:
  - `--checks=healpix`: host (`orders 1..5`: 0 errors) + GPU `HealpixProbeCS` (`order = 6`, 49,152 pixels: 0 errors) in `[bytes]` and `[words]` with `Full` Vulkan validation.
  - `--checks=collide`: synthetic multi-face polar+equatorial collision (`order = 5`, 12,288 cells) verifying exact GPU Allegretti `2×2` BKE CCL vs CPU BFS (`bke_err=0`, `terrane=924/1158` exact), disconnected island preservation (`0B_lost=0`), terrane transfer & escape crust mass conservation (`905/924` = 97.9%), Himalayan surge (`OrogenyHimalayan = 2`, 1,378 cells), and non-collision zero-workgroup indirect gating (`dispatch=(0,0,0,0)`) — **PASS in both `[bytes]` and `[words]` with `Full` Vulkan validation**.
- **`PlanetGenBench`**: headless GPU-timer benchmark (`RelWithDebInfo`, `maxScopes = 64`), supports `--order=<4..9>` (default `7` = 196,608 cells).
- **`PlanetGenTimelapse`**: headless multi-step runner (`--order=<4..9>`) + `RenderMapCS` + `FrameCapture` + `AgentTools` (`timelapse/TimelapseAgentTools.hpp/.cpp`).

---

## 2. Step Pipeline & Packed GPU Buffer Layout (`Buffers.slang`)

**Step Order (`PlanetStep.cpp`):**
1. `OwnersCS` → 2. `CountVotesCS` → `SwapToMajorityCS` → 3. `FillGapsCS` → `RemoveSubductedCS` *(fused `CountFrontPointsCS`)* → **3a. Collision (`Collide.slang`)**: `DetectCollisionsCS` → `SelectCollisionPairCS` *(writes 4-slot `CollideDispatch`)* → indirect `BkeTileInitAndLocalUnionCS` → `BkeStitchSeamsCS` → `BkeFlattenCS` → `MarkTerraneSeedsCS` → `CountTerraneCS` → `DecideTransferCS` → `TransferTerraneDirectCS` → `ClearSourceTerraneCS` → `ComputeSutureCapSpreadCS` → `NearestSuturePointCS` → `EscapeHistogramCS` → `EscapePrefixSumCS` → `ApplyEscapeAndSurgeCS` → 3b. `CountLiveCellsCS` → `OffsetLiveCellsCS` → `ScatterLiveCellsCS` → 4. `OffsetFrontPointsCS` → `ScatterFrontPointsCS` → `SortFrontPointsCS` → `BuildFrontTileCapsCS` → 6. `SumPullCS` → 4+5. `FrontDistanceAndUpliftCS` → *(live loop)* `MovePlatesAndClearCountersCS` *(also clears `CollisionPairCount/SpeedSum/DeepCount`)*.

**Phase 2 Collision Buffers (`Buffers.slang` + `k_ZeroedBuffers`):**
- `CollisionPairCount`, `CollisionPairSpeedSum`, `CollisionPairDeepCount` (`PlatesSquared`, with `*In` read-only aliases)
- `CollisionJob` (`16` uints, with `CollisionJobIn`), `CollideDispatch` (`12` uints = 4 × `{gx, 1, 1}` at byte offsets 0/12/24/36)
- `BkeParent`, `BkeMask`, `BkeSeedFlag` (`PointCount / 2` uints across both colliding plates)
- `CollideScratchCounters` (`8` uints), `SuturePositionSum` (`3` ints), `SuturePoints` (`PointsTimes4`), `SutureDistance` (`Points` floats), `EscapeHistogram` (`64` uints)

---

## 3. Performance Baseline (RTX 4080, 20 plates, RelWithDebInfo, bytes)

| Configuration | Cells | Collision State | Step | Key Pass Timings |
| :--- | ---: | :--- | ---: | :--- |
| **Phase 1 Locked (`order=7`)** | 196,608 | *(pre-collision)* | `146.4 µs` | `Owners 19.5`, `CountLive 13.3`, `SortFront 13.3`, `FrontDist+Uplift 38.9` |
| **Phase 2 (`order=7`, gated)** | 196,608 | Non-collision tick | **`167.1 µs`** *(min)* / `178.4 µs` *(disp med)* | `3a Collision 37.9 µs` (`Detect 7.2`, `Select 3.1`, 13 empty indirect barriers `~27 µs`) |
| **Phase 1 Locked (`order=8`)** | 786,432 | *(pre-collision)* | `443.4 µs` | `Owners 55.8`, `CountLive 32.8`, `SortFront 16.4`, `FrontDist+Uplift 229.4` |
| **Phase 2 (`order=8`, active!)** | 786,432 | **Full active collision!** | **`514.9 µs`** *(min)* / `541.5 µs` *(med)* | **`3a Collision 156.7 µs`** (`BkeTileInit 7.2`, `BkeStitch 31.7`, `BkeFlatten 5.1` → **44 µs BKE CCL**; `Transfer 15.4`, `NearestSuture 18.4`, `EscapeHist 26.6`, `Surge 14.3`) |

- **Profiler DB (`build/profiler_captures/planetgen_diag.db`):** Runs 1–8 (Phase 1).
- **LDS Bank-Conflict Audit:** Clean across all shaders (0 runtime bank conflicts; `BkeTileInitAndLocalUnionCS` uses 4-byte `uint` Morton-indexed `SharedBlockMask/Parent[64]`).

---

## 4. Prioritized Next Steps

1. **Phase 3 — Software Sparse Virtual Plate Paging (`planetgenv2.md` §4) & `Loom` (`modules/RenderGraph`) Integration:**
   - Replace dense `P × N` arrays with `8×8` tile `PageTable` + lock-free 2-phase `FreeTileStack` (`~40.5 MB` at `order = 8`, 100% L2-resident, zero `VK_QUEUE_SPARSE_BINDING`, WebGPU-compatible).
   - Port `PlanetStep` + `Collide` + `RenderMapCS` onto `modules/RenderGraph` (`Loom`, Vulkan + WebGPU) and retire `WriteManifestDescriptors`; alias transient collision buffers on a `rhi::placement` heap.
2. **Phase 4 — Lithology (`TileLithologyPool` + `SampleLithology`), Macro-Climate, Hydrology, Erosion & 3D Meshing:**
   - See [docs/planet-gen-agent.md](../../docs/planet-gen-agent.md) §5 for the provenance-recorder + 3D stratigraphic column design driving the Megascans library.
