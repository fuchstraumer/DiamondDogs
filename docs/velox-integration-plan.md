# WebGPU & Emscripten Integration Plan (formerly Velox Integration)

Updated 2026-11-22.

## Progress Note

The foundational items, Milestone 1, and Milestone 2 are now complete and checked in:
- `foundation/async` (`Scheduler`, `Task`, `Timeline`, `TimelineSource`, `FrameHeap`) is implemented as a single-owner-thread, polled coroutine loop.
- `foundation/math` includes the WASM SIMD128 backend (`MathBackendWASM.inl`, `QuaternionBackendWASM.inl`, `TranscendentalBackendWASM.inl`) wired under `EMSCRIPTEN`.
- `ImageDataFormats.hpp` is decoupled from Vulkan (`ImageDataFormatsVulkan.hpp`).
- The boot and render-graph seams are split and enforced at build time: `Bootstrap` vs `BootstrapVulkan` / `BootstrapWebGpu`, and `loom` (RHI-free via `bootstrap_assert_no_rhi`) vs `loom_vulkan`.
- **Milestone 1 (complete, checked in):** `emscripten` CMake preset, `emsdk` toolchain integration, client-only Lodestone + host cooker detection, CRT `FrameHeapWeb` + `HighResolutionWaitEmscripten` shims, `-fno-exceptions` + `JSON_NOEXCEPTION` + `-msimd128`, and passing Node unit test suites (`foundation_tests`, `loom_tests`, `input_system_tests`, `content_compiler_tests`) with zero `-sASYNCIFY`.
- **Milestone 2 (complete, checked in: `fbaa5929..9ed8c7d8`):** `modules/RhiContext`, shared `Bootstrap::DeviceConfig`, swapped WebGPU `ResourceContextImpl`, `modules/BootstrapWebGpu` (`std::variant` FSM + `WebGpuSurfaceConfig`), `tests/integration_tests/WebGpuTriangleTest`, and multi-target `.clangd` / `.vscode/settings.json` / `.vscode/c_cpp_properties.json` editor tooling.

Next up are the planned M2 refinements (Dawn `emdawnwebgpu_pkg` pinning + lean Emscripten build flags) followed by M3–M4 while keeping native Vulkan first-class.

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

### Milestone 1: Emscripten Build & `foundation` Portability (Complete — Checked In)
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

### Milestone 2: `RhiContext`, Multi-Stage FSM Bootstrap & WebGPU `ResourceContextImpl` (Complete — Checked In)
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
5. **ColorPipe parity & responsive WebGPU display handling:**
   - Extract plain display/surface vocabulary types (`SurfaceFormatPair`, `ColorSpace`) from `PlatformSystem` into a shared vocabulary header so `ColorPipe`'s math, `DisplayTarget`, `ChooseSurface(...)`, and Lodestone-cooked WGSL tonemapping shaders compile identically on Vulkan and WebGPU.
   - Add a swapped `src/webgpu/` TU for `ColorPipe`'s fullscreen composite pass (`FullscreenDrawState` / `ColorComposite`) once WebGPU raster pipelines land.
   - Expose WebGPU surface capabilities (`BGRA8Unorm`/`RGBA8Unorm` SDR and `RGBA16Float` extended/HDR with `srgb`/`display-p3` and `toneMapping.mode: "extended"`) as `SurfaceFormatPair`s fed into `color::ChooseSurface(...)`, and listen to `window.matchMedia("(dynamic-range: high)")` and `"(color-gamut: p3)"` to reconfigure the surface and rebuild `DisplayTarget` when moving between SDR and HDR/P3 displays.
   - **Web/mobile `DisplayOverrides` & optional HDR calibration:** Because browsers withhold exact `PeakNits`, `BlackNits`, and `PaperWhiteNits` for privacy, start with sensible SDR/extended-HDR defaults and plan an optional, config-menu-accessible HDR calibration shader (not a forced startup wizard) so web and mobile users can tune `PeakNits`, `BlackNits`, and `PaperWhiteNits` via `DisplayOverrides` when needed.
6. **WebGPU `ResourceContextImpl` frame-budgeted uploads & async callbacks:**
   - Cap `queue.WriteBuffer` / `queue.WriteTexture` bytes or milliseconds per `Update()` tick (chunking large uploads across `rAF` frames) so main-thread staging copies do not stall the browser event loop.
   - Use WebGPU callbacks (`MapAsync`, `OnSubmittedWorkDone`) to resolve `FutureTable` slots directly without blocking or forcing `co_await` into the shared `ResourceContext` API.

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

## Dawn Dependency Management & Emscripten Build Optimization (Planned M2 Refinements)

### 1. Syncing, Pinning, and Upgrading Dawn (Option A — `emdawnwebgpu_pkg` Releases)
Because DiamondDogs uses native Vulkan on desktop and Lodestone for offline Slang compilation, Web builds only need **`emdawnwebgpu`** (`<webgpu/webgpu.h>`, `<webgpu/webgpu_cpp.h>`, `webgpu.cpp`, and `library_webgpu.js` glue) rather than a full multi-GB Dawn checkout with native backends, Tint, Abseil, or Python/Jinja generators. Google publishes text-only `emdawnwebgpu_pkg-<date>.zip` packages under `https://github.com/google/dawn/releases`.

- **Lockfile (`cmake/dawn-lock.json`):** Check in a small lockfile pinning the Dawn release tag, archive download URL for `emdawnwebgpu_pkg-*.zip`, and SHA-256 checksum.
- **CMake Integration (`cmake/ConfigureDawn.cmake`):**
  - Keep `DIAMOND_DOGS_DAWN_DIR` as an optional local override for testing unreleased Dawn commits or local patches.
  - By default (when `DIAMOND_DOGS_DAWN_DIR` is unset), use CMake `FetchContent` with `URL` + `URL_HASH SHA256=...` from `cmake/dawn-lock.json` to download and unpack `emdawnwebgpu_pkg`.
  - Expose an `INTERFACE` target named **`emdawnwebgpu_cpp`** attaching `--use-port=${EMDAWNWEBGPU_PKG_DIR}/emdawnwebgpu.port.py` to both `INTERFACE_COMPILE_OPTIONS` and `INTERFACE_LINK_OPTIONS`, preserving all downstream `target_link_libraries(... emdawnwebgpu_cpp)` targets unchanged.
  - Moving to a current `emdawnwebgpu_pkg` release also eliminates the `../VeloxRhi` checkout dependency, removes the temporary `<string>` header workaround needed for emsdk 7.x libc++ against VeloxRhi's older snapshot, and automatically registers `webgpu-externs.js` for Closure Compiler.
- **Sync/Upgrade Script (`scripts/sync_dawn.py`):**
  - `--check`: queries the GitHub Releases API (`google/dawn/releases/latest`), compares against `cmake/dawn-lock.json`, and reports available updates.
  - `--upgrade` / `--tag <release>`: downloads the specified `emdawnwebgpu_pkg` release zip, computes/verifies its SHA-256 checksum, and updates `cmake/dawn-lock.json` so the next `cmake --preset emscripten` pulls and builds against it cleanly.

### 2. Lean & Compact Emscripten Build Flags

#### Per-Configuration Optimization & Debug Stripping
Apply configuration-specific flags via CMake generator expressions (`$<$<CONFIG:...>:...>`):

| Config | Compile Flags | Link Flags | Purpose |
| :--- | :--- | :--- | :--- |
| **Debug** | `-O0 -g` | `-g -sASSERTIONS=2 -sSTACK_OVERFLOW_CHECK=2` | Fast iteration, assertions, readable stack traces |
| **RelWithDebInfo** | `-O2 -g2` | `-O2 -g2 -sASSERTIONS=0` | Chrome DevTools CPU profiling (keeps function names, drops heavy DWARF) |
| **Release** | `-O3 -flto` | `-O3 -flto -g0 -sASSERTIONS=0 -sSTACK_OVERFLOW_CHECK=0 --closure=1` | Max runtime speed, LTO + Binaryen `wasm-opt`, stripped debug info |
| **MinSizeRel** | `-Oz -flto` | `-Oz -flto -g0 -sASSERTIONS=0 -sSTACK_OVERFLOW_CHECK=0 --closure=1 -sMALLOC=emmalloc` | Smallest `.wasm` + `.js` binary footprint |

- **`-flto`** (compile + link) lets `wasm-ld` and Binaryen's `wasm-opt` dead-strip unused C++ instantiations across static libraries (`foundation`, `loom`, `EnTT`, `lodestone_client_library`).
- **`-g0`** strips DWARF and name sections in `Release`/`MinSizeRel`.

#### Opt-In Runtime Systems & Binary Trimming
- **Web Threads (`DIAMOND_DOGS_WEB_THREADS`, default `OFF`):** Explicit CMake option (`OFF` = no `SharedArrayBuffer` / COOP+COEP requirement and no worker pool glue; `ON` adds `-pthread` at compile/link plus `-sPTHREAD_POOL_SIZE=...` and `-sMAXIMUM_MEMORY=...` if keeping memory growth).
- **Virtual Filesystem (`-sFILESYSTEM=0` on browser apps):** Strip JS `FS`/`MEMFS` glue on browser executables that fetch/stream assets directly over Web APIs, keeping `FILESYSTEM=1` only on Node unit test targets that embed cooked shaders (`--embed-file`).
- **Environment Pruning:** Pass `-sENVIRONMENT=web` (or `web,worker` when `DIAMOND_DOGS_WEB_THREADS=ON`) to browser executables and `-sENVIRONMENT=node` to Node unit tests.
- **Exception, RTTI & Runtime Trimming:** Keep `-fno-exceptions` alongside `-sDISABLE_EXCEPTION_CATCHING=1` and `-sSUPPORT_LONGJMP=0`; evaluate `-fno-rtti` once verified compatible with `ContentCompiler` and `gtest`. On browser executables, pass `-sEXIT_RUNTIME=0`, `-sDYNAMIC_EXECUTION=0` (CSP compatibility), `-msimd128`, `-sWASM_BIGINT=1`, and explicit `-sINITIAL_MEMORY=32MB`/`64MB` with `-sALLOW_MEMORY_GROWTH=1`. (`-sMINIMAL_RUNTIME=1` remains an optional future extreme JS-glue reduction pass.)

#### Making Closure Compiler (`--closure=1`) Reliable
- **JS/C++ boundary naming:** Always use quoted bracket notation for shared global/module properties in `EM_ASM`/`EM_JS` (e.g., `window['__ddFrameCount']`, `Module['canvas']`) or declare them in a project externs file passed via `--closure-args=--externs=...` so advanced minification does not rename them.
- **WebGPU Externs:** Linking `emdawnwebgpu_cpp` via `--use-port=.../emdawnwebgpu.port.py` automatically supplies Dawn's `webgpu-externs.js` to Closure.
- **Scope & Debugging:** Run `--closure=1` only in `Release` / `MinSizeRel`. If Closure breaks a Release link, diagnose temporarily with `--closure=1 -sASSERTIONS=1 --closure-args=--formatting=PRETTY_PRINT`.

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
- **Decoupled simulation cadence & WebGPU frame pacing:** Allow compute sims (e.g., FFT updates or alternating cascades) to run at 30 Hz while presentation/camera runs at 60 Hz. Browser WebGPU is always compositor-synced (`PresentMode::Fifo` via `rAF`); estimate the active refresh rate using a rolling median of `rAF` timestamp deltas (~15–30 frames) and support a user-facing `Auto / 60 / 30` cap by cleanly skipping alternating `rAF` ticks on 120 Hz mobile displays.

### 3. Runtime `relaxed-simd` Module Branching
WASM validates all opcodes at load time, so a single `.wasm` binary cannot branch around `relaxed-simd` instructions—engines without support reject the module outright.
- **Default during bring-up:** Compile with `-msimd128` ON and `-mrelaxed-simd` **OFF** for maximum mobile browser compatibility.
- **When CPU-side SIMD becomes hot:** Do **not** split out a separate math-only `.wasm` module (cross-module call/memory overhead outweighs FMA gains). Instead, emit two full `.wasm` build artifacts (`app_simd.wasm` and `app_relaxed.wasm`) and have the JS loader run a ~20-byte `WebAssembly.validate()` probe at startup to fetch the appropriate binary with zero runtime dispatch cost.

---

## Open Decisions Before Starting M3–M4

1. **Lodestone WGSL push constants:** Confirm how Lodestone emits push constants in WGSL profiles (explicit uniform binding slot vs convention) before implementing `loom_webgpu`'s `ParamsStore` backing buffer.
2. **ImGui on Web:** Decide whether `ImGuiModule` gets an `imgui_impl_wgpu` path early (for on-device debug UI) or stays desktop-only while mobile uses the ADB telemetry/control channel.
3. **Engine RTTI on Web (`-fno-rtti`):** Audit whether `ContentCompiler`, `foundation/reflect`, or `gtest` require RTTI in WASM builds before adding `-fno-rtti` alongside `-fno-exceptions`.
