# DiamondDogs audit and running notes

Started 2026-10-19. This document contained a much longer log of information, but those were redundant tokens that just served to bloat context. it has now been collapsed to only updates since the last month by the repository author, and it is expected that it remains compacted and maintained. Agents: use this as a board for communication between yourselves, as necessary. Post short update logs, stating who you are / what model you are, when you did the work, and where to learn more *if an agent needs it*. I often make sure to pass communication between you explicitly: use this for shared awareness, the same that human engineers would use their email inbox of review requests and general conversations to stay abreast of their engine's states (without getting too lost, and staying in their niche since they're experts there!).

### 2026-11-09 (Opus: tools)

- Held-time repeatability fixed (foam drift pinned to a tool-set time; foam material chosen per pixel, not per wave). Details in `docs/tools-agent.md`.
- `frame_capture_tests` added; it found and fixed a transposed BT.2020 to BT.709 matrix in HDR10 captures. Ticket template for small models in the sidecar README. CLAUDE.md tables updated. Details in `docs/tools-agent.md`.

### 2026-11-10 (Opus: render graph)

- Resolve plumbing and its test bundle, the Freeze graph-pin fix (LM108), and the engine boot brief (`docs/engine-boot-brief.md`). Details in `docs/rendergraph-agent.md`.

### 2026-11-11 (Opus: RhiSystem and bootstrap)

- New role and handbook (`docs/rhi-agent.md`). Bootstrap module designed (`docs/bootstrap-api-draft.md`); step 1 done: JSON RhiSystem constructor removed, `ShaderBundle` moved to its own target, no default argument on `ShaderLibrary::Open`. Details in `docs/rhi-agent.md`.
- Step 2: `modules/Bootstrap` (`LaunchStage`, `CoreStage`, profiles, boot timings), `bootstrap_tests` (35), the `ContentCooker` stub with a configure-time no-RHI check. Item 6 deferred to `DeviceStage`. Details in `docs/rhi-agent.md`.
- Step 3: `ContentStage` and `GatherDeviceRequirements` (Required and Wanted capabilities from the manifests, two limit sets), `bootstrap_tests` 54. `rhi_system_tests` crashed in instance creation with a game on the GPU; open in `docs/rhi-agent.md` 4c.
- Step 4, RhiSystem half: `StartInstance` and `StartDevice` enable only the needs (generated feature table, device planner, supports from the enabled set). A generator bug put `VK_EXT_debug_utils` in the device table; fixed. Details in `docs/rhi-agent.md` 4e.
- `foundation/timing/`: `MonotonicNanoseconds`, `Stopwatch`, `HighResolutionWait` (was `PrecisionTimer`), `FrameLimiter` (no default argument). Details in `docs/rhi-agent.md` log, 2026-11-12.
- Step 4 done: `modules/BootstrapVulkan` (`WindowStage`, `DeviceStage`, `GpuStage`, spine needs), the surface split from the swapchain in PlatformSystem, present and limit checks and `Declined` for every kind in the planner. `bootstrap_vulkan_tests` (10). Item 6 moved to step 5. Details in `docs/rhi-agent.md` 4f.

### 2026-11-21 (Opus: RhiResources for Loom)

- Resource description, barrier vocabulary and stage-first usage bits, buffer and image regions, BDA tests, destroy after a `GpuWait`, transfer sets: committed `69b92176` to `9a32184b`. Details in `docs/rhi-resources-agent.md`, sections 3 and 8.

### 2026-11-23 (Opus: Loom step 7c)

- Loom draws on the GPU: `LoomDrawTest` and `LoomTriangleTest`, present fences (swapchain maintenance1), the frame target Clear fallback as a rendering pass, three GPU plants caught. Details in `docs/rendergraph-agent.md` (log, 2026-11-23 step 7c).
- Presentation P0 and P1: `DisplayProbe` prints presentation support; `Swapchain` owns acquire, present, release and the present fences (swapchain maintenance1 where present). Details in `docs/rendergraph-agent.md` and `docs/presentation-plan.md`.

### 2026-11-24 (Opus: scalability design)

- Design draft for scaling (knobs, settings, the governor): `docs/scalability-plan.md`. Its Lodestone and Loom requests stay local to that file until the author moves them.
- **2026-11-24:** Loom agent: presentation plan finished (P3 present ids and waits, P4 `foundation::FramePacer` and the
  EDID range and adaptive-sync flag, P5 the paced loop in LoomTriangleTest), the NVIDIA deferred-memory crash isolated
  (`docs/nvidia-deferred-swapchain/`), the EDID now read from the live connection's registry key. Details in
  `docs/rendergraph-agent.md` (log) and `docs/presentation-plan.md`.
- **2026-11-24 (night):** Loom agent: OceanFftLoom shell (her port starts), one shader bundle per app
  (`cmake/DiamondDogsShaderModules.cmake`), a Loom Record-load fix, the `k_` rule for keys, Lodestone requests C28 to
  C31. Details in `docs/rendergraph-agent.md` (section 4 NEXT and log).

### 2026-11-26 (PlanetGen)

- Fixed two Loom issues in `modules/RenderGraph` (wrapped in `// NEON PLANETGEN FIX BEGIN/END`), resolved Step-2+ `CrustState` GPU non-determinism (`0 byte diffs`), and added live interactive swapchain UI + `AgentTools`/TCP JSON-RPC controls in `PlanetGenLoom`. Details in `docs/planet-gen-agent.md` §4 and `applications/PlanetGen/HANDOFF.md`.
- Added Python spherical diagnostics & probe scripts (`applications/PlanetGen/scripts/`), refined mantle plumes into discrete episodic volcanic/atoll/seamount chains, integrated `ShapingParams` (Cortial 2019 slab dip + 16-sample LUT) + 3D spherical `NoiseGraph` (`Fbm` warp, `Swiss`, `RidgedMulti`, `Cellular3`) + procedural `--preset=atoll|earth`, and rendered a verified 2K 241-frame Atoll World timelapse. Details in `docs/planet-gen-agent.md` §6–8.
