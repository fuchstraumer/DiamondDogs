# Velox integration and a WASM build: plan notes

Draft, 2026-10-20, by Claude, for the author. Deferred by her decision: RhiResources gets shored up first. Nothing
here is implemented. Read with `docs/rhi-resources-design.md`, which this plan depends on.

## Goal

One repository (DiamondDogs) that builds two ways:

- **Native:** a direct Vulkan mapping, written by us, with the full feature set (timelines, explicit barriers,
  device address, bindless later).
- **Web (WASM):** the app modules (node graph, noise system) plus a WebGPU backend, from VeloxRhi. No threads in
  this build.

Dawn could run WebGPU over Vulkan on native, but the author wants our own code to own the Vulkan mapping. The DX12
backend is dropped: Vulkan covers Windows and the Unix-based desktops.

## What VeloxRhi is today (read 2026-10-20)

About 40 source files. The parts that matter here:

| Area | Files | Portable? |
|---|---|---|
| Async: coroutine `Future`, `Scheduler` (a `SlotMap` of tagged coroutine handles, resumed in `Tick()`), `CoroutineAllocator` | `include/async/`, `src/async/` | Yes. `Scheduler.cpp` already has `#ifndef __EMSCRIPTEN__` atomic paths for native worker threads. |
| `Context`: WebGPU instance, adapter, device, surface, async bootstrap | `core/Context.*` (443 lines) | WebGPU only. Says it is based on DiamondDogs' RhiSystem. |
| Math: DirectXMath on native, WASM SIMD128 (optionally relaxed-simd) on web, with `MathTests` | `include/math/` | Yes. Two backends, tested against each other. |
| Graph: `GraphBuilder`, `PipelineDesc`, `ResourceBinding`, `SubpassBuilder` | `include/graph/` | Backend-neutral by shape. |
| `SlotMap`, `EnumClassUtils`, `VeloxErrors` | `utility/`, `common/` | Yes. |
| `ResourceRegistry` | `resource/` | A stub: create buffer, create texture, write buffer. |
| `InputManager`, `Application` | `core/` | Platform layer (GLFW on native, the browser on web). |

Velox handles async with coroutines and continuations, and bans threads.

## The key observation: the resource design ports

ResourceContext does not need to become a different source file for Velox. The author's intent for it is "an event
bus that does not look threaded to the user". That intent survives. Only two things are platform-specific: what
drains the messages, and the backend calls.

The transfer timeline from the design note (one timeline semaphore per transfer queue, tickets on replies) is what
makes this work. A reply becomes a status plus a ticket. That is exactly what a Velox awaitable needs:

- **Native:** a reply is a `SchedulerDispatchedAwaitable`. `Scheduler::Tick()` reads the timeline counter once and
  resumes every coroutine whose ticket passed. The worker thread can stay (users never see it) or go (the context
  drains its queue in `Tick()`).
- **Web:** the same awaitable resumes from WebGPU's completion callbacks (`onSubmittedWorkDone`, `mapAsync`), in
  `Tick()` on the main thread.

| | Drains the messages | Uploads |
|---|---|---|
| Native Vulkan | worker thread, or `Tick()` | transfer queue, staging ring |
| Web | `Scheduler::Tick()` on the main thread | `queue.writeBuffer` / `writeTexture` (WebGPU stages internally) |

On web the transfer system mostly disappears. So the WebGPU `ResourceContext` is a second, small implementation of
the same public header, not a translation of the Vulkan one.

## The real decision: where the backend seam goes

- **Full RHI abstraction** (one device, buffer and command API over both): expensive and lossy. WebGPU cannot express
  timelines, explicit barriers, device address, or bindless. The common API either reduces Vulkan to WebGPU, or fills
  with "not on web" holes. That defeats the reason for a direct Vulkan mapping. **Not recommended.**
- **Work-level seam (recommended).** Lodestone already provides one shader contract: one cook writes WGSL and
  SPIR-V with one manifest (bindings, variant keys, layouts). Above the shaders, the shared interface is narrow:
  - create a resource from a declaration;
  - write it, read it back;
  - run a compute pass;
  - record a draw.

  The node graph and the noise system are written against that. Each backend executes it in its own way. The Velox
  graph types (`GraphBuilder`, `PipelineDesc`, `ResourceBinding`) are already shaped like this layer.

Keep the Vulkan-only features reachable directly on native. Do not hide them behind the shared interface, and do
not let that interface grow into a full RHI.

## Steps

1. **Drop DX12.** Trivial: `#ifdef RHI_SYSTEM_USE_DX12` stubs and one option in `modules/RhiSystem/CMakeLists.txt`.
2. **Bring in Velox's portable core** as foundation pieces: `async/` (Scheduler, Future, CoroutineAllocator),
   `SlotMap`, `EnumClassUtils`, `VeloxErrors`, and `math/`. Keep one math library: Velox's (it has both SIMD
   backends and `MathTests`). Retire `foundation/math` after comparing the two test suites.
3. **Split foundation by platform.** Native-only: the MWSR queue, critical sections, SRW locks, `atomic128`/MCAS, the
   mimalloc override, the Win32 plugin and PDB code. The web build does not link them.
4. **Awaitable replies.** The transfer timeline and tickets (design note section 1), then the `Scheduler` integration
   on Vulkan. The transfer rework does most of this anyway.
5. **Velox's `Context` becomes the WebGPU sibling of `RhiSystem`**, with a small WebGPU `ResourceContext` behind the
   same public header.
6. **Define the work-level interface**, starting from the Velox graph types. First proof: port ComputeManifestTest
   onto it. One test, both backends, both compared against the same CPU reference.
7. **An Emscripten preset** in DiamondDogs that builds only the portable modules, the async core, the WebGPU backend,
   and the app modules. Velox's presets and its `FindVulkanHeaders` ordering show how.

## Difficulty

| Step | Size | Kind |
|---|---|---|
| 1 to 3 | small to medium | moving files, CMake |
| 4 | medium | planned anyway (design note) |
| 5 | small | `Context` is 443 lines; WebGPU resources are simple |
| 6 | the real work | design, which the author will want to own |
| 7 | small to medium | presets, the Emscripten toolchain, tests under `node` |

Risk: the shared interface creeps toward a full RHI. Watch for every "not on web" branch in it; each one is a sign
that a feature belongs on the native side only.

## Facts to keep in mind

- `ImageDataFormats.hpp` is now free of Vulkan (the VkFormat conversions moved to `ImageDataFormatsVulkan.hpp`,
  2026-10-20), so the web build can use it.
- Velox's WASM math has a relaxed-simd option that fails module validation on engines without support. Its default is
  ON; turn it off for wider browser coverage.
- Lodestone's WGSL path needs Tint, which DiamondDogs turns off today (`LODESTONE_ENABLE_WGSL=OFF` in
  `cmake/ConfigureLodestone.cmake`, to avoid the Dawn fetch). The web build needs it on, or a host cooker.

## Decisions for the author

- The seam: work level (recommended) or full RHI.
- Native threads: keep the ResourceContext worker thread behind the awaitables, or drain in `Tick()` everywhere.
- Which math library survives, and when.
- Whether the ImGui module goes web too (Dear ImGui has a WebGPU backend).
