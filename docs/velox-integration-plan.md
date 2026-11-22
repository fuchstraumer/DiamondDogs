# WebGPU & Emscripten Integration Plan (formerly Velox Integration)

Updated 2026-11-21.

## Progress Note

The foundational items from the original 2026-10-20 draft are now complete in DiamondDogs:
- `foundation/async` (`Scheduler`, `Task`, `Timeline`, `TimelineSource`, `FrameHeap`) is implemented as a single-owner-thread, polled coroutine loop.
- `foundation/math` includes the WASM SIMD128 backend (`MathBackendWASM.inl`, `QuaternionBackendWASM.inl`, `TranscendentalBackendWASM.inl`) wired under `EMSCRIPTEN`.
- `ImageDataFormats.hpp` is decoupled from Vulkan (`ImageDataFormatsVulkan.hpp`).
- The boot and render-graph seams are split and enforced at build time: `Bootstrap` vs `BootstrapVulkan`, and `loom` (RHI-free via `bootstrap_assert_no_rhi`) vs `loom_vulkan`.

Everything below tracks only what remains to bring WebGPU + Emscripten online (targeting desktop browsers and mobile Android/iOS testing) while keeping native Vulkan first-class.

---

## Architecture & Non-Negotiables

- **No lossy common RHI, but unify top-level ownership with `RhiContext` + shared `RhiResources`.**
  - Add a thin, non-virtual **`rhi::RhiContext`** wrapper (PImpl'd per backend) that owns device lifecycle, enabled capability/extension queries (`DeviceFilter`, `IsFeatureEnabled(std::string_view)`, `ShaderLimits()`, `LayoutLimits()`), and an opaque backend handle (unwrapped only in backend-private headers: `RhiContextVulkan.hpp` / `RhiContextWebGpu.hpp`).
  - Keep `RhiSystem` / `loom_vulkan` native and direct, and add `loom_webgpu` behind the existing `loom` work-level seam.
  - Keep **`ResourceContext`** as one shared public interface and swap only its PImpl (`ResourceContextImpl`) per backend—reusing `rhi_resource_types`, `rhi_resource_barriers`, `ResourceFuture`, `FutureTable`, and EnTT registry tracking as-is.
  - `ResourceContextCreateInfo` and `loom::RealizeInputs` take `const rhi::RhiContext*` / `const rhi::RhiContext&`, eliminating `#ifdef __EMSCRIPTEN__` from public headers and allowing `GpuStage` / `GpuStartContext` to stay shared across backends.
- **Single-threaded, polled Web build.** No pthreads, no `SharedArrayBuffer` (avoids COOP/COEP hosting headaches), and **no `-sASYNCIFY`**. Everything async drives through `emscripten_set_main_loop_arg` → `foundation::Scheduler::Tick()` → `wgpu::Instance::ProcessEvents()`.
- **Use `emdawnwebgpu_cpp` (Dawn's WebGPU C++ port), not Emscripten's deprecated `-sUSE_WEBGPU`.** Reference `../VeloxRhi` (`CMakeLists.txt`, `cmake/ConfigureDawn.cmake`, `CMakePresets.json`, `src/core/Context.cpp`, `src/core/Application.cpp`) for working Emscripten + Dawn wiring.

---

## Concrete Work Map

### Milestone 1: Emscripten Build & `foundation` Portability
*Goal: `foundation`, `shader_bundle`, `bootstrap`, and `loom` build cleanly under an `emscripten` CMake preset, and `loom_tests` pass under Node (zero GPU required).*

1. **Gate native-only build config in `CMakeLists.txt` and `modules/CMakeLists.txt`:**
   - Wrap `FIND_PACKAGE(Vulkan REQUIRED)`, `mimalloc`, `vma`, `glfw`, MSVC compiler flags (`/Zi`, `/EHsc`, `/arch:AVX2`), and Vulkan-only modules (`RhiSystem`, `PlatformSystem`, `BootstrapVulkan`, `FrameCapture`, `GpuPyramid`) in `if(NOT EMSCRIPTEN)`. (`RhiResources` stays enabled on both, selecting its backend PImpl inside `modules/RhiResources/CMakeLists.txt`.)
   - Add an `emscripten` configure/build preset to `CMakePresets.json` (modeled on `../VeloxRhi/CMakePresets.json`), linking `emdawnwebgpu_cpp` and `webgpu_glfw`.
2. **Split `foundation/CMakeLists.txt` for Emscripten:**
   - Under `EMSCRIPTEN`, exclude `src/miMallocOverride.cpp` (and `mimalloc-static` link—use Emscripten's allocator), `src/net/SocketWin32.cpp` / `SocketWaker.cpp`, `src/threading/*` (`atomic128`, `CriticalSection`, `SrwLock`, `mwsrQueue`, `ExponentialBackoffSleeper`), and `${Vulkan_INCLUDE_DIR}`.
   - **Important CMake gotcha:** Emscripten sets `UNIX=TRUE`. Change `if(UNIX)` linking `c++fs`, `dl`, `pthread` to `if(UNIX AND NOT EMSCRIPTEN)`.
   - Guard owner-thread checks in `foundation/async/Scheduler` if `<thread>` causes issues under single-threaded Emscripten.
   - Handle C++ exceptions: Emscripten defaults to `-fno-exceptions`. Define `JSON_NOEXCEPTION` for `nlohmann::json` in `Bootstrap` (`LaunchStage.cpp`)—or explicitly opt into `-fwasm-exceptions` if exceptions are desired.
3. **Host-side Lodestone WGSL cooking (`cmake/ConfigureLodestone.cmake`):**
   - `lodestone_cooker_console` (Slang + Tint) cannot cross-compile to WASM.
   - In the Emscripten build: set `LODESTONE_BUILD_COOKER=OFF` (compile only `lodestone_client_library` + `xxHash::xxhash` for WASM) and point CMake at a **native host-built cooker** compiled with `LODESTONE_ENABLE_WGSL=ON` (e.g., via `DIAMOND_DOGS_HOST_COOKER`).
   - Load cooked `.wgsl` bundles on Web via `ShaderBundle::FromBytes(bytes, "wgsl")` (from `fetch()` or `--preload-file`).

---

### Milestone 2: `RhiContext`, Multi-Stage FSM Bootstrap & WebGPU `ResourceContextImpl`
*Goal: Unify device boot behind `RhiContext` + a `std::variant` type-state FSM, and swap `ResourceContextImpl` for WebGPU.*

1. **`rhi::RhiContext` + `std::variant` Bootstrap FSM (replacing VeloxRhi's manual phase enum):**
   - Instead of a single struct with nullable members and a manual `switch (phase)` (which caused state-tracking bugs in `VeloxRhi/src/core/Context.cpp`), model device initialization as a **`std::variant` type-state FSM** where each state is an encapsulated struct owning *only* the handles/futures valid in that step and moves them into the next state on completion:
     - **WebGPU states:** `Unstarted` → `RequestingAdapter` (holds `Instance`, `Surface`, adapter future) → `RequestingDevice` (holds `Instance`, `Surface`, `Adapter`, device future, requested `wgpu::Limits` / `FeatureName`s from `Bootstrap::DeviceRequirements`) → `Ready` (produces `RhiContext`) | `Failed` (`BootResult`).
     - **Vulkan states:** Same `Step()` / `Poll()` outer contract, transitioning straight to `Ready` (producing `RhiContext` wrapping `rhi::RhiSystem`) in a single tick.
   - On WASM, `emscripten_set_main_loop_arg` ticks `scheduler.Tick()`, `instance.ProcessEvents()`, and `deviceStage.Step()`—advancing cleanly one discrete setup step per browser tick (and trivially wrappable in a `foundation::Task<BootResult>` via `co_await Yield()`).
   - Once `Ready`, `DeviceStage` exposes `RhiContext&` + `DeviceFilter`, and **`GpuStage` / `GpuStartContext` remain shared** across Vulkan and WebGPU.
2. **Swap `ResourceContextImpl` for WebGPU inside `modules/RhiResources`:**
   - **Reuse as-is:** `rhi_resource_types`, `rhi_resource_barriers` (already RHI-free via `bootstrap_assert_no_rhi`), `ResourceContext.hpp`, `ResourceContext.cpp`, `ResourceFuture.hpp`, `FutureTable.cpp`, `ResourceTypes.cpp`, and EnTT registry storage.
   - **Clean up `ResourceContextCreateInfo`:** Replace `const rhi::Instance*` / `const rhi::Device*` / `VulkanApiVersion` with `const rhi::RhiContext* Context` (+ backend-neutral knobs like `MaxFutures`, `StagingRingSize`, `ValidationEnabled`). Each `ResourceContextImpl` unwraps its own backend handle internally.
   - **CMake backend switch (`modules/RhiResources/CMakeLists.txt`):** Under `if(EMSCRIPTEN)`, compile `src/webgpu/ResourceContextImpl.cpp` instead of the Vulkan implementation (`ResourceContextImpl.cpp`, `TransferSystem.cpp`, `UploadBuffer.cpp`, `VulkanConversions.cpp`), and link `emdawnwebgpu_cpp` + `EnTT` instead of `rhi_system`, `${Vulkan_LIBRARY}`, and `VulkanMemoryAllocator`.
   - **Implementation:** No worker threads, no VMA, no staging ring, no transfer queue. `CreateBuffer` / `CreateTexture` call `device.Create*` + `queue.WriteBuffer` / `queue.WriteTexture` directly; barrier/usage transitions and `RegisterConsumerTimeline` / `GpuWait` become no-ops; `rhi::placement` heap calls return `UnsupportedUsage`; async readbacks (`buffer.MapAsync`) and `queue.OnSubmittedWorkDone` complete `FutureTable` slots (and bridge to `foundation::TimelineSource` for `co_await`).

---

### Milestone 3: `loom_webgpu` Backend
*Goal: Execute `loom::CompiledGraph` on WebGPU without duplicating scheduling logic.*

1. **Reuse `loom` core directly:**
   - Consume `CompiledGraph`, `DecideRuns()`, `ParamsStore`, and `Ordered.Levels`.
   - **Skip `PlanBarriers()` for barrier emission:** Dawn/wgpu tracks hazards implicitly. Instead, ride `Ordered.Levels`—open **one `GPUComputePassEncoder`** across contiguous compute levels and dispatch all independent passes in Level `k` back-to-back before Level `k+1`. This clusters implicit barriers at level boundaries and maximizes GPU overlap.
2. **Uploads & bindings:**
   - Hoist `queue.WriteBuffer` / staging copies to frame start (before opening passes) so copies never split passes.
   - Cache immutable `GPUBindGroup`s per `(pass, group)` keyed by resource `Epoch` (mirroring Vulkan's `WrittenEpochs`). Steady-state frames allocate zero bind groups.
   - Keep `AccessModifier::ReadOnly` strictly mapped to `read-only-storage` in bind group layouts so Dawn/wgpu does not insert false write hazards between concurrent readers.
   - Submit **one `GPUCommandBuffer`** per `Tick()` via `queue.Submit()` to minimize browser IPC and validation overhead.
3. **Push constants (`ParamsStore`) lowering:**
   - WebGPU has no `vkCmdPushConstants`. Back `ParamsStore` with a small per-frame uniform buffer (aligned to `minUniformBufferOffsetAlignment`, typically 256 bytes) written via `queue.WriteBuffer()` before encoding.
   - *Prerequisite check:* Verify how Lodestone lowers Slang push-constant blocks into WGSL uniform bindings.
4. **Render-pass coalescing (when Loom Step 7 lands):**
   - Merge consecutive raster passes that share attachments (without reading prior outputs as sampled textures) into a single `GPURenderPassEncoder` to avoid `storeOp`/`loadOp` round-trips on mobile TBDR GPUs.

---

### Milestone 4: End-to-End Proof & First Demo
1. Port `LoomComputeTest` (`LoomHashFill.slang` cooked to WGSL) to run in desktop Chrome and on Android Chrome (Pixel 10 XL), verifying outputs against the CPU PCG hash reference.
2. Convert one real rendering demo (e.g., `OceanFFT`) once Loom supports images + raster passes.

---

## Emscripten & Mobile Pitfalls Checklist

- **Never block or spin-wait on Web:** `WaitForFrameSlot()` and `resources.Wait()` must never be called on Emscripten—WebGPU callbacks only progress when yielding to the browser event loop + calling `ProcessEvents()`.
- **WASM32 is 32-bit `size_t` / pointers:** Audit any `uint64_t` handle casts in shared headers; Loom's `int32_t` indices are already safe.
- **Tighter mobile GPU limits:** Expect 256 max compute invocations/workgroup, 16 KB workgroup shared memory, and missing `shader-f16` / `subgroups` / `timestamp-query`. Ensure shader capability axes have clean fallback variants.
- **HTTPS / Secure Context for `navigator.gpu`:** Plain `http://192.168.x.x` disables WebGPU on mobile browsers. Use `adb reverse` (`localhost` is a secure context) on Android over USB-C, or local HTTPS (`mkcert`) over LAN.
- **Mobile DPR & viewport resize thrashing:** Mobile URL bars animate on scroll and fire resize events at 2×–3× `devicePixelRatio`. Debounce `surface.Configure()` and clamp DPR.
- **Pixel 10 (Tensor G5 / PowerVR DXT-48-1536) driver maturity:** Imagination PowerVR Vulkan/Dawn support on Android is relatively young; keep CPU-reference compute tests runnable on-device to catch driver/compiler regressions early.

---

## Short-Term Future Plans

### 1. Mobile Profiling & Telemetry Over ADB
Because hardware counters (like Nsight) are awkward through Chrome's sandboxed GPU process, build lightweight telemetry directly on top of Loom + `adb reverse`:
- **USB-C WebSocket telemetry pipe:** Reuse `adb reverse tcp:<port> tcp:<port>` to stream JSONL frames from the phone back to a desktop receiver (per-pass timings, `DeviceLimits`, `FramePlan` run/skip counts, frame delta times).
- **GPU pass timings:** Use WebGPU `timestamp-query` (`timestampWrites` on pass begin/end) when supported. For local ADB profiling, pass `--enable-dawn-features=disable_timestamp_quantization` to Chrome to bypass the default 100 µs security quantization. When `timestamp-query` is unavailable, fall back to `queue.OnSubmittedWorkDone()` wall-clock latency + remote `graph.Enable(pass, false)` toggles to isolate pass costs.
- **System-level correlation:** Pair Loom's WebSocket timings with **Chrome DevTools** (`chrome://inspect` for WASM CPU flamegraphs) and **Perfetto** (`ui.perfetto.dev` over ADB for Android GPU slices, SurfaceFlinger vsyncs, and thermal/clock throttling).

### 2. Unified Scalability System (Separate from `DeviceRequirements`)
Keep runtime quality/scalability knobs decoupled from pre-boot capability/variant selection (`Bootstrap::DeviceRequirements`). Target mobile bandwidth and TBDR efficiency first:
- **Simulation resolution & cascade count:** Primary lever for `OceanFFT` (e.g., 512² → 256², fewer cascades).
- **Canvas DPR / render scale clamp:** Avoid native ~3× DPR fillrate on high-DPI phones (clamp to `1.0–1.5` or scale internal targets).
- **Precision (`f16` / `rgba16float`):** Prefer 16-bit storage/sampled formats to halve memory bandwidth on PowerVR/mobile GPUs.
- **Decoupled simulation cadence:** Allow compute sims (e.g., FFT updates or alternating cascades) to run at 30 Hz while presentation/camera runs at 60 Hz.

### 3. Runtime `relaxed-simd` Module Branching
WASM validates all opcodes at load time, so a single `.wasm` binary cannot branch around `relaxed-simd` instructions—engines without support reject the module outright.
- **Default during bring-up:** Compile with `-msimd128` ON and `-mrelaxed-simd` **OFF** for maximum mobile browser compatibility.
- **When CPU-side SIMD becomes hot:** Do **not** split out a separate math-only `.wasm` module (cross-module call/memory overhead outweighs FMA gains). Instead, emit two full `.wasm` build artifacts (`app_simd.wasm` and `app_relaxed.wasm`) and have the JS loader run a ~20-byte `WebAssembly.validate()` probe at startup to fetch the appropriate binary with zero runtime dispatch cost.

---

## Open Decisions Before Starting M1–M3

1. **Lodestone WGSL push constants:** Confirm how Lodestone emits push constants in WGSL profiles (explicit uniform binding slot vs convention) before implementing `loom_webgpu`'s `ParamsStore` backing buffer.
2. **Exceptions on Web:** Prefer `JSON_NOEXCEPTION` + `-fno-exceptions` (smaller/faster WASM) vs `-fwasm-exceptions`.
3. **ImGui on Web:** Decide whether `ImGuiModule` gets an `imgui_impl_wgpu` path early (for on-device debug UI) or stays desktop-only while mobile uses the ADB telemetry/control channel.
