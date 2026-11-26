# PlanetGen Handoff (2026-11-26)

Current state, performance baselines, buffer contracts, and prioritized next steps for `applications/PlanetGen` and `assets/shaders/compute/Planet/`.
See also the running agent handbook at [docs/planet-gen-agent.md](../../docs/planet-gen-agent.md) (including lithology/Megascans design notes).

---

## 1. Current Architecture & Targets (PlanetGen v2 — Native Nested HEALPix)

**Phase 1 (Native Nested HEALPix + Workload Reductions), Phase 2 (Pure-GPU Continental Collision `Collide.slang`), Phase 3 Loom Fixes, Multi-Step Determinism, Mantle Plumes (`Plumes.slang`), and Live Interactive Renderer + AgentTools/MCP (`PlanetGenLoom`) are COMPLETE and VERIFIED.** See [planetgenv2.md](planetgenv2.md) for the full v2 architectural blueprint and [doc_search_results.md](doc_search_results.md) for the ±2 citation triage (Arnould & Coltice 2020, Karlbauer 2024, Bolelli/Allegretti 2024, Benyoub/Dupuy 2024 CBT, Westerteiger 2012, cuHPX 2025, Calabretta 2007, GPU Pro 5/7).

`applications/PlanetGen/CMakeLists.txt` builds:
- **`planet_gen_shaders`**: cooks `assets/shaders/compute/Planet/PlanetLab.slang` (**38 compute entrypoints × 2 `CRUST_FLAG_BYTES` variants**) and `PlanetPresent.slang` (**2 graphics entrypoints × 1 variant**) = **78 entrypoint variants** via Lodestone.
- **`planet_gpu`** (static lib): `PlanetSession`, `PlanetShaders`, `GpuBuffers`, `PlanetStep` (**31 step / 32 live-step entrypoints** incl. `CountFrontPointsCS`, indirect dispatch for `LiveDispatch`, `FrontTileDispatch`, and 4-slot `CollideDispatch`), `Healpix`.
- **`planet_reference`** (static lib): `ReferenceSet`, `PlanetMath`, `StepState` (resampling onto HEALPix `order = 4..9`).
- **`PlanetGenCheck`**:
  - `--checks=healpix`: host (`orders 1..5`: 0 errors) + GPU `HealpixProbeCS` (`order = 6`, 49,152 pixels: 0 errors) in `[bytes]` and `[words]`.
  - `--checks=collide`: synthetic multi-face polar+equatorial collision (`order = 5`: `bke_err=0`, `terrane=924/1158`, `0B_lost=0`, `0A_rem=0`, mass conservation `905/924`, Himalayan surge `1,378` cells, non-collision zero-workgroup dispatch) in `[bytes]` and `[words]`.
  - `--checks=plumes`: moving oceanic plate over fixed hotspot (`order = 5`: `peak_relief=10.00 km`, `trail=20`, `bins=(5/7/8)`, `ages=(54.4>40.3>29.5)`, `upstream=0`, `noop_diffs=0`) in `[bytes]` and `[words]`.
- **`PlanetGenBench`**: headless GPU-timer benchmark (`RelWithDebInfo`, `--order=4..9`).
- **`PlanetGenTimelapse`**: headless multi-step runner + `RenderMapCS` + `FrameCapture` + `--stats=<csv>` + legacy `TimelapseAgentTools`.
- **`PlanetGenLoom`** (`applications/PlanetGen/loom/`):
  - **Core Loom Graph** (`PlanetGraph.hpp/.cpp`): 38 registered pass keys (counter zero/reset, 33 simulation passes incl. post-collision `CountFrontPointsCS` and `ApplyMantlePlumesCS`, `RenderMapCS`, `CaptureMapPass`, `PresentMapPass`, `UiPass`).
  - **Bit-exact Multi-Step Parity (`--verify` & `--self-verify`):** **0 byte diffs** across all 5 buffers (`Owners`, `CrustFlags`, `CrustState`, `Rotations`, `PoleRates`) in `order=7` (`--bytes` and `--words`, 10 steps) and `order=8` (active continental collision, 5 steps) under `ValidationLayers::Full`.
  - **Mantle Plumes** (`Plumes.slang`, `MantlePlumes.hpp/.cpp`): 64-slot GPU hotspot uniform buffer, Rodrigues drift, dynamic thermal swell, magma pulses, oceanic island -> coral atoll -> submerged seamount erosion/subsidence, `OrogenyVolcanic = 4`, `--plumes=default|none`, debug layer `6` (`--mode=plumes`).
  - **Live Interactive Renderer & AgentTools/MCP** (`PlanetLiveApp.hpp/.cpp`, `PlanetLoomTools.hpp/.cpp`, `PlanetPresent.slang`): `--live` windowed swapchain renderer + ImGui HUD + pan/zoom, `--paused`, `--frames=N`, `--tool-port=N` (windowed or headless TCP JSON-RPC exposing `planet.sim` and `planet.pause`, `planet.step`, `planet.reset`, `planet.set_layer`, `planet.plume.list/add/update/remove/clear`, `planet.capture`, `planet.capture_region`, `planet.timings`, `planet.quit`).

---

## 2. Loom Fixes Audit Trail (`modules/RenderGraph`)

All edits inside `modules/RenderGraph` are wrapped in `// NEON PLANETGEN FIX BEGIN` ... `// NEON PLANETGEN FIX END` comments:
1. **`CollectingSink` dangling `std::string_view`** (`include/LoomDiagnostic.hpp:7-9,86-90`, `src/LoomDiagnostic.cpp:161-163,166-191`): interns diagnostic text views into `std::deque<std::string> ownedStrings;` so `CollectingSink` stays safe after `Compile()` destroys `FrozenGraph`. Regression test: `LoomDiagnosticSink.CollectingSinkOwnsReportedStrings` (`test_diagnostics.cpp:61`).
2. **`AddDataFlowEdges` false `LM501` cycle on sourced single-writer buffers** (`src/order/Edges.cpp:3-5,11-13,19-25,163-165,178-184,196-210,224-281`): skips `writer -> reader` data-flow edges for single-writer `Imported`/`HasSource`/`PersistentWriter` slots only when explicit ordering already ensures `reader` precedes `writer`. Regression tests: `Order.ImportedSingleWriterReadEarlyInSequenceDoesNotCycle` (`test_order.cpp:471`), `Order.ImportedSingleWriterUnorderedStillOrdersWriterFirst`.
3. Verified `loom_tests` **252/252 passed** (`ninja-clang-cl` Debug) and `--target loom` (`ninja-msvc` Debug under `vcvars64.bat`; note full `ninja-msvc` builds hit pre-existing external `../Lodestone/src/compile/impl/SlangReflector.cpp(1905): error C2137`).

---

## 3. Performance Baseline (RTX 4080, 20 plates, RelWithDebInfo, bytes, 0 dropped scopes)

| Configuration | Cells | State | Total GPU | Breakdown |
| :--- | ---: | :--- | ---: | :--- |
| **Phase 1 Locked (`order=7`)** | 196,608 | Legacy step only | `146.4 µs` | `Owners 19.5`, `CountLive 13.3`, `SortFront 13.3`, `FrontDist+Uplift 38.9` |
| **Phase 2 Legacy (`order=7`)** | 196,608 | Non-collision tick | `167.1 µs` | `3a Collision 37.9 µs` |
| **Phase 2 Legacy (`order=8`)** | 786,432 | Active collision | `514.9 µs` | `3a Collision 156.7 µs` (`BKE CCL 44.0 µs`, `FrontDist+Uplift 229.4`) |
| **`PlanetGenLoom` (`order=7`)** | 196,608 | `--plumes=default` + `RenderMap` | **`311.30 µs`** | Sim `227.33 µs` (`CountFront 4.10`, `Plumes 9.22`, `FrontDist+Uplift 76.67`) + `RenderMap 83.97 µs` |
| **`PlanetGenLoom` (`order=8`)** | 786,432 | `--plumes=default` + `RenderMap` | **`692.70 µs`** | Sim `609.76 µs` (`CountFront 8.19`, `Plumes 24.58`, `FrontDist+Uplift 338.11`) + `RenderMap 82.94 µs` |

---

## 4. Prioritized Next Steps

1. **Shaping, Atoll World & Phase 3b Software Sparse `8×8` Tile Paging:**
   - Mountain/coast shaping (`ShapingParams` + existing `NoiseGraph` kernels like `deCarpentier Swiss` / `Cellular`) and Atoll World generator.
   - Replace dense `P × N` arrays with `8×8` tile `PageTable` + lock-free 2-phase `FreeTileStack` (`~40.5 MB` at `order = 8`, 100% L2-resident, WebGPU-compatible) + transient buffer aliasing in `Loom`.
2. **Phase 4 — Lithology (`TileLithologyPool` + `SampleLithology`), Macro-Climate, Hydrology, Erosion & 3D Meshing:**
   - See [docs/planet-gen-agent.md](../../docs/planet-gen-agent.md) §5 for the provenance-recorder + 3D stratigraphic column design driving the Megascans library.
