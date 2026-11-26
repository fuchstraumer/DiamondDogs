# PlanetGen Handoff (2026-11-26)

Current state, performance baselines, buffer contracts, and prioritized next steps for `applications/PlanetGen` and `assets/shaders/compute/Planet/`.
See also the running agent handbook at [docs/planet-gen-agent.md](../../docs/planet-gen-agent.md) (including lithology/Megascans design notes).

---

## 1. Current Architecture & Targets (PlanetGen v2 — Native Nested HEALPix)

**Phase 1 (Native Nested HEALPix + Workload Reductions), Phase 2 (Pure-GPU Continental Collision `Collide.slang`), Phase 3 Loom Fixes, Multi-Step Determinism, Refined Discrete Mantle Plumes, Live Interactive Renderer + AgentTools/MCP, Cortial (2019) Slab-Dip `ShapingParams`, Spherical `NoiseGraph` Integration, and Procedural `--preset=atoll|earth` (`PlanetGenLoom`) are COMPLETE and VERIFIED.** See [planetgenv2.md](planetgenv2.md) for the full v2 architectural blueprint and [doc_search_results.md](doc_search_results.md) for the ±2 citation triage (Arnould & Coltice 2020, Karlbauer 2024, Bolelli/Allegretti 2024, Benyoub/Dupuy 2024 CBT, Westerteiger 2012, cuHPX 2025, Calabretta 2007, GPU Pro 5/7).

`applications/PlanetGen/CMakeLists.txt` builds:
- **`planet_gen_shaders`**: cooks `assets/shaders/compute/Planet/PlanetLab.slang` (**38 compute entrypoints × 2 `CRUST_FLAG_BYTES` variants**, importing `NoiseBases`/`NoiseFractal`/`NoiseRules`/`NoiseCellular`) and `PlanetPresent.slang` (**2 graphics entrypoints × 1 variant**) = **78 entrypoint variants** via Lodestone.
- **`planet_gpu`** (static lib): `PlanetSession`, `PlanetShaders`, `GpuBuffers`, `PlanetStep` (**31 step / 32 live-step entrypoints** incl. `CountFrontPointsCS`, indirect dispatch for `LiveDispatch`, `FrontTileDispatch`, and 4-slot `CollideDispatch`), `Healpix`.
- **`planet_reference`** (static lib): `ReferenceSet`, `PlanetMath`, `StepState` (incl. `ComputeHealpixPlateCaps` and `AddDefaultShapingUniform`).
- **`PlanetGenCheck`**:
  - `--checks=healpix`: host (`orders 1..5`: 0 errors) + GPU `HealpixProbeCS` (`order = 6`, 49,152 pixels: 0 errors) in `[bytes]` and `[words]`.
  - `--checks=collide`: synthetic multi-face polar+equatorial collision (`order = 5`: `bke_err=0`, `terrane=924/1158`, `0B_lost=0`, `0A_rem=0`, mass conservation `905/924`, Himalayan surge `1,378` cells, non-collision zero-workgroup dispatch) in `[bytes]` and `[words]`.
  - `--checks=plumes`: moving oceanic plate over fixed hotspot (`order = 5`: `peak_relief=8.50 km`, `trail=20`, `bins=(5/7/8)`, `ages=(54.2>40.6>29.2)`, `upstream=0`, `noop_diffs=0`) in `[bytes]` and `[words]`.
  - `--checks=uplift`: `UpliftCS` exact parity (`0 differ`, max diff `4.77e-07`) in `[bytes]` and `[words]`.
- **`PlanetGenBench`**: headless GPU-timer benchmark (`RelWithDebInfo`, `--order=4..9`).
- **`PlanetGenTimelapse`**: headless multi-step runner + `RenderMapCS` + `FrameCapture` + `--stats=<csv>` + legacy `TimelapseAgentTools`.
- **`PlanetGenLoom`** (`applications/PlanetGen/loom/`):
  - **Core Loom Graph** (`PlanetGraph.hpp/.cpp`): 38 registered pass keys (counter zero/reset, 33 simulation passes incl. post-collision `CountFrontPointsCS` and `ApplyMantlePlumesCS`, `RenderMapCS`, `CaptureMapPass`, `PresentMapPass`, `UiPass`).
  - **Bit-exact Multi-Step Parity (`--verify` & `--self-verify`):** **0 byte diffs** across all 5 buffers (`Owners`, `CrustFlags`, `CrustState`, `Rotations`, `PoleRates`) in `order=7` (`--bytes` and `--words`, 10 steps) and `order=8` (active continental collision, 5 steps) under `ValidationLayers::Full`.
  - **Refined Mantle Plumes** (`Plumes.slang`, `MantlePlumes.hpp/.cpp`): accretion-promotion guard (`OrogenyVolcanic` exempt from continental conversion), sharpened episodic magma bursts + lateral fracture-zone vent stepping, plate-frame `Fractal<Simplex3, Swiss>` shields, two-stage subaerial-erosion / coral-atoll / submerged-seamount subsidence, 64-slot hotspot UBO, debug layer `6` (`--mode=plumes`).
  - **Art Direction, Spherical `NoiseGraph` & Presets** (`Uplift.slang`, `FrontPoints.slang`, `RenderMap.slang`, `PlanetPresets.hpp/.cpp`): `ShapingParams` UBO, Cortial (2019) `100 km / tan(dip)` slab-dip curve, 16-sample LUT, 3D spherical `Fractal<Simplex3, Fbm>` warp + `Swiss` + `RidgedMulti` + `Cellular3` lagoons, `--preset=reference|earth|atoll`, `--shaping=legacy|earth|atoll`, `--seed=N`, `--plates=4..32`, ImGui `"Art Direction & Shaping"` panel, `AgentTools` `planet.shaping` + `planet.shaping.preset`.
  - **Quantitative Diagnostics (`applications/PlanetGen/scripts/`):** `planet_diagnostics.py` (spherical area fractions, `coast_D`, wrapped components, elongation/ribbons, enclosed lagoons, plume beadiness, sequence stability) and `planet_probe.py`.
  - **2K Atoll World Showcase MP4:** `build/atoll_world_timelapse/planetgen_atoll_world_order8_120my_30fps.mp4` (`241` frames, `0..120 My`, `coast_D=1.337`, `land=24.21%`, `lagoons=88`, `plumes`: `19` comps / `93.7 beads/%` / `0` ribbons).

---

## 2. Loom Fixes Audit Trail (`modules/RenderGraph`)

All edits inside `modules/RenderGraph` are wrapped in `// NEON PLANETGEN FIX BEGIN` ... `// NEON PLANETGEN FIX END` comments:
1. **`CollectingSink` dangling `std::string_view`** (`include/LoomDiagnostic.hpp:7-9,86-90`, `src/LoomDiagnostic.cpp:161-163,166-191`): interns diagnostic text views into `std::deque<std::string> ownedStrings;` so `CollectingSink` stays safe after `Compile()` destroys `FrozenGraph`. Regression test: `LoomDiagnosticSink.CollectingSinkOwnsReportedStrings` (`test_diagnostics.cpp:61`).
2. **`AddDataFlowEdges` false `LM501` cycle on sourced single-writer buffers** (`src/order/Edges.cpp:3-5,11-13,19-25,163-165,178-184,196-210,224-281`): skips `writer -> reader` data-flow edges for single-writer `Imported`/`HasSource`/`PersistentWriter` slots only when explicit ordering already ensures `reader` precedes `writer`. Regression tests: `Order.ImportedSingleWriterReadEarlyInSequenceDoesNotCycle` (`test_order.cpp:471`), `Order.ImportedSingleWriterUnorderedStillOrdersWriterFirst`.
3. Verified `loom_tests` **252/252 passed** (`ninja-clang-cl` Debug) and `--target loom` (`ninja-msvc` Debug under `vcvars64.bat`; note full `ninja-msvc` builds hit pre-existing external `../Lodestone/src/compile/impl/SlangReflector.cpp(1905): error C2137`).

---

## 3. Performance Baseline (RTX 4080, RelWithDebInfo, bytes, 0 dropped scopes)

| Configuration | Cells | State | Total GPU | Breakdown |
| :--- | ---: | :--- | ---: | :--- |
| **Phase 1 Locked (`order=7`)** | 196,608 | 20 plates, legacy step | `146.4 µs` | `Owners 19.5`, `CountLive 13.3`, `SortFront 13.3`, `FrontDist+Uplift 38.9` |
| **Phase 2 Legacy (`order=7`)** | 196,608 | 20 plates, non-collision | `167.1 µs` | `3a Collision 37.9 µs` |
| **Phase 2 Legacy (`order=8`)** | 786,432 | 20 plates, active collision | `514.9 µs` | `3a Collision 156.7 µs` (`BKE CCL 44.0 µs`, `FrontDist+Uplift 229.4`) |
| **`PlanetGenLoom` Ref (`order=7`)** | 196,608 | 20 plates, `--plumes=default`, 1024×512 | **`311.30 µs`** | Sim `227.33 µs` + `RenderMap 83.97 µs` |
| **`PlanetGenLoom` Ref (`order=8`)** | 786,432 | 20 plates, `--plumes=default`, 1024×512 | **`776.22 µs`** | Sim `665.63 µs` (`FrontDist+Uplift 356.54`, `Plumes 59.39`) + `RenderMap 110.59 µs` |
| **`PlanetGenLoom` Atoll (`order=7`)** | 196,608 | 28 plates, `--preset=atoll`, 1024×512 | **`679.39 µs`** | Sim `237.02 µs` (`FrontDist+Uplift 62.88`, `Plumes 29.70`) + `RenderMap 442.37 µs` |
| **`PlanetGenLoom` Atoll 2K (`order=8`)** | 786,432 | 28 plates, `--preset=atoll`, 2048×1024 | **`2332.54 µs`** | Sim `825.21 µs` (`FrontDist+Uplift 301.06`, `Plumes 99.33`) + `RenderMap 1507.33 µs` |

---

## 4. Prioritized Next Steps

1. **Phase 3b — Software Sparse `8×8` Tile Paging:**
   - Replace dense `P × N` arrays with `8×8` tile `PageTable` + lock-free 2-phase `FreeTileStack` (`~40.5 MB` at `order = 8`, 100% L2-resident, WebGPU-compatible) + transient buffer aliasing in `Loom`.
2. **Phase 4 — Lithology (`TileLithologyPool` + `SampleLithology`), Macro-Climate, Hydrology, Erosion & 3D Meshing:**
   - See [docs/planet-gen-agent.md](../../docs/planet-gen-agent.md) §5 for the provenance-recorder + 3D stratigraphic column design driving the Megascans library.
