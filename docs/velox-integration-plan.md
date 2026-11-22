# WebGPU & Emscripten State Handbook (formerly Velox Integration)

Updated 2026-11-22. **Status:** Milestones 1–4 and M2 Refinements are complete (M1–M3 committed; M4 `WebGpuLoomComputeTest` built and verified live in Chrome Debug & Release, awaiting Pixel 10 XL on-device verification and commit). Native Vulkan remains first-class.

---

## 1. Architectural Invariants & Toolchain

- **Shared seams, swapped backend TUs:** `rhi::RhiContext` (PImpl per backend) + `ResourceContext` (swapped `ResourceContextImpl` `.cpp` TUs) keep `#ifdef __EMSCRIPTEN__` out of public headers. `Bootstrap`, `ColorPipe`, `loom` (`bootstrap_assert_no_rhi`), and `ResourceContext` stay shared; `RhiSystem`, `PlatformSystem`, `BootstrapVulkan`, and `loom_vulkan` remain native-only.
- **Single-threaded polled Web loop:** No `-sASYNCIFY`, and no pthreads or `SharedArrayBuffer` by default (`DIAMOND_DOGS_WEB_THREADS=OFF`, avoiding COOP/COEP headers). Async steps drive through `emscripten_set_main_loop_arg` → `foundation::Scheduler::Tick()` / `ResourceContext::Tick()` / `wgpu::Instance::ProcessEvents()`.
- **Compiler & runtime baseline:** `-fno-exceptions`, `-fno-rtti`, `-msimd128`, `JSON_NOEXCEPTION`, `-sDISABLE_EXCEPTION_CATCHING=1`, `-sSUPPORT_LONGJMP=0`, `-sEXIT_RUNTIME=0`, `-sDYNAMIC_EXECUTION=0`, `-sALLOW_MEMORY_GROWTH=1`.
- **Dawn (`emdawnwebgpu_cpp`) pinning:** Pinned via `cmake/dawn-lock.json` (`v20261120.121236`, SHA-256 `15e946ab48abb1777aa8f0300fd00ca452292d7948d692e0a405926b646944a2`). `cmake/ConfigureDawn.cmake` fetches `emdawnwebgpu_pkg` and attaches `--use-port=emdawnwebgpu.port.py` (with `DIAMOND_DOGS_DAWN_DIR` as local override). Manage with `scripts/sync_dawn.py --check | --upgrade | --tag <release>`.
- **Host Lodestone WGSL cooker:** `cmake/ConfigureLodestone.cmake` sets `LODESTONE_BUILD_COOKER=OFF` under Emscripten and auto-detects a host `lodestone_cooker_console.exe` (preferring `../Lodestone/build/` with WGSL cooking enabled).
- **Closure-safe JS/C++ boundary:** `--closure=1` runs in `Release`/`MinSizeRel`. Every JS property access in `EM_ASM` / `EM_JS` must use quoted bracket notation (e.g. `window['__ddTestDone']`, `document['getElementById']`).
- **Multi-target editor tooling:** `.clangd` and `.vscode/` use `build/ninja-msvc` by default and switch to `build/emscripten` (with `Remove: ["--use-port=*"]`) for `BootstrapWebGpu`, `WebGpu*Test`, `/webgpu/`, and `WebGpu.(hpp|cpp)`.

| Config | Compile Flags | Link Flags | Purpose |
| :--- | :--- | :--- | :--- |
| **Debug** | `-O0 -g` | `-g -sASSERTIONS=2 -sSTACK_OVERFLOW_CHECK=2` | Fast iteration, assertions, stack traces |
| **RelWithDebInfo** | `-O2 -g2` | `-O2 -g2 -sASSERTIONS=0` | DevTools CPU profiling without heavy DWARF |
| **Release** | `-O3 -flto` | `-O3 -flto -g0 -sASSERTIONS=0 -sSTACK_OVERFLOW_CHECK=0 --closure=1` | Max runtime speed, LTO + `wasm-opt` |
| **MinSizeRel** | `-Oz -flto` | `-Oz -flto -g0 -sASSERTIONS=0 -sSTACK_OVERFLOW_CHECK=0 --closure=1 -sMALLOC=emmalloc` | Smallest `.wasm` + `.js` footprint |

---

## 2. Completed Milestones

| Milestone | Scope & Verification |
| :--- | :--- |
| **M1** (committed) | `emscripten` CMake preset, `emsdk` toolchain, client-only Lodestone + host cooker detection, `FrameHeapWeb` + `HighResolutionWaitEmscripten`, WASM SIMD128 math (`MathBackendWASM.inl`), `ImageDataFormats` split, Node unit tests (`foundation_tests`, `loom_tests`, `input_system_tests`, `content_compiler_tests`). |
| **M2 + Refinements** (committed) | `modules/RhiContext`, shared `Bootstrap::DeviceConfig`, swapped WebGPU `ResourceContextImpl`, `modules/BootstrapWebGpu` (`std::variant` FSM + `WebGpuSurfaceConfig`), `WebGpuTriangleTest` (Debug 12.0 MB / Release 404 KB / MinSizeRel 339 KB `.wasm`), `dawn-lock.json`, lean per-config flags, `-fno-rtti`. |
| **M3** (committed) | Core Loom WGSL support (`CompiledStage::ShaderCode` byte vector + `StartLifetimeAsArray<T>`, `UniformBuffer` constants delivery via Lodestone format v11, `CheckUnboundResources` skip), `loom_webgpu` backend (epoch-keyed `GPUBindGroup` cache, `ParamsStore` uniform buffer), `platform_types` + `ColorPipe` parity (`46/46` `color_pipe_tests` on Node), `WebGpuDisplayObserver` (`matchMedia` HDR/P3), WebGPU `ResourceContextImpl` 4 MB/tick budgeted uploads & `aliveToken` lifetime guard. |
| **M4** (built & Chrome-verified) | `WebGpuLoomComputeTest` (`LoomHashFill.slang` cooked to WGSL, 16,396 GPU PCG hash words matched CPU bit-for-bit across both `HASH_TWICE` variants + `LM701` short-import check; verified live in Chrome Debug 35.2 MB / Release 746 KB `.wasm` with zero validation errors; `loom_tests` 218/218 on Node). |

---

## 3. Resolved Decisions

- **Lodestone WGSL push constants:** Lodestone format v11 exposes them via `EntryPointInstanceView::Constants()` in an explicit `UniformBuffer` slot; `loom_webgpu`'s `ParamsStore` backs that slot (256-byte aligned).
- **ImGui on Web:** Deferred through M3–M4; ADB telemetry/control comes first, with `imgui_impl_wgpu` later when needed for `OceanFft`.
- **RTTI on Web (`-fno-rtti`):** Enabled on Emscripten and verified across all 5 Node unit test suites (`foundation_tests`, `loom_tests`, `input_system_tests`, `content_compiler_tests`, `color_pipe_tests`).

---

## 4. Active Next Steps & Backlog

1. **Pixel 10 XL on-device verification:** Run `WebGpuLoomComputeTest` over `adb reverse` on PowerVR DXT-48-1536 and commit M4.
2. **WebGPU raster & ColorPipe composite:** Implement WebGPU raster pass support once Loom Step 7 raster lands, add `ColorPipe`'s fullscreen composite WebGPU TU, and add an optional config-menu HDR calibration shader for `DisplayOverrides` (`PeakNits`, `BlackNits`, `PaperWhiteNits`).
3. **First real WebGPU rendering demo:** Port `OceanFFT` (with `imgui_impl_wgpu` when UI is needed).
4. **Mobile profiling & ADB WebSocket telemetry pipe:** Stream JSONL frame/pass metrics over `adb reverse` using `timestamp-query` (with `--enable-dawn-features=disable_timestamp_quantization` locally) or `OnSubmittedWorkDone` fallback + remote `graph.Enable(pass, false)` isolation, correlated with `chrome://inspect` and Perfetto.
5. **Unified Scalability System:** Decoupled from `DeviceRequirements`—simulation resolution/cascades, DPR clamp (`1.0–1.5`), `f16`/`rgba16float` preference, and rolling-median `rAF` refresh-rate estimator for `Auto / 60 / 30` frame-pacing cap.
6. **Runtime `relaxed-simd` loader:** Keep `-mrelaxed-simd` off for bring-up; when CPU SIMD becomes hot, emit dual `.wasm` artifacts (`app_simd.wasm` / `app_relaxed.wasm`) selected via a startup `WebAssembly.validate()` probe.

---

## 5. Emscripten & Mobile Pitfalls Checklist

- **Never block or spin-wait on Web:** `WaitForFrameSlot()` and `resources.Wait()` must never be called on Emscripten—WebGPU callbacks only progress when yielding to the browser event loop + calling `ProcessEvents()`.
- **WASM32 is 32-bit `size_t` / pointers:** Audit any `uint64_t` handle casts in shared headers; Loom's `int32_t` indices are already safe.
- **Tighter mobile GPU limits:** Expect 256 max compute invocations/workgroup, 16 KB workgroup shared memory, and missing `shader-f16` / `subgroups` / `timestamp-query`. Ensure shader capability axes have clean fallback variants.
- **HTTPS / Secure Context for `navigator.gpu`:** Plain `http://192.168.x.x` disables WebGPU on mobile browsers. Use `adb reverse` (`localhost` is a secure context) over USB-C, or local HTTPS (`mkcert`) over LAN.
- **Mobile DPR & viewport resize thrashing:** Mobile URL bars animate on scroll and fire resize events at 2×–3× `devicePixelRatio`. Debounce `surface.Configure()` and clamp DPR.
- **Pixel 10 (Tensor G5 / PowerVR DXT-48-1536) driver maturity:** Imagination PowerVR Vulkan/Dawn support on Android is young; keep CPU-reference compute tests runnable on-device to catch driver/compiler regressions early.

---

## 6. Session Log

- **2026-11-21:** Completed M1 (`emscripten` preset, `foundation`/`loom`/`input`/`content_compiler` Node tests), M2 (`RhiContext`, `BootstrapWebGpu`, WebGPU `ResourceContextImpl`, `WebGpuTriangleTest`), and M2 Refinements (`dawn-lock.json`, lean per-config flags, `-fno-rtti`).
- **2026-11-22:** Completed M3 (`loom_webgpu`, WGSL `ParamsStore` uniform delivery, `ColorPipe` parity `46/46` on Node, `WebGpuDisplayObserver`, 4 MB/tick budgeted uploads) and M4 (`WebGpuLoomComputeTest` built and verified live in Chrome Debug & Release; `loom_tests` 218/218 on Node).
