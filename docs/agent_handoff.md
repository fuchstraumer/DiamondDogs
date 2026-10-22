# Agent handoff

The running handoff between sessions. Read this first. `docs/audit.md` keeps the older history and the
detailed findings (sections 4, 5, 9); new work is logged here.

Branch `dev/lodestone`. The author commits herself; the agent never commits. Check `git status` for what is still
uncommitted. Her code spacing rule is in CLAUDE.md ("Code spacing").

## State (2026-10-21, end of the seventh session; the newest work is at the end of this section)

Committed by the author:
- **Earlier work:** the ocean compute fixes, OceanFft (`ocean_simulation` and the demo), and OceanFftTest.
- **Readback:** image-to-buffer readback in RhiResources, through `CopyBufferContents(image, buffer)`.
- **Frame capture:** `modules/FrameCapture` writes 8-bit sRGB PNGs, through a ring of GPUToCPU buffers and a writer
  thread.
- **Swapchain:** adds TRANSFER_SRC usage when the surface allows it.
- **Demo control:** `SceneCommands`, which takes `--script`, `--stdin` and `--camera` (the command list is in
  `SceneCommands.hpp`).
- **Demo capture flags:** `--capture=DIR`, `--capture-count/-every/-first`, `--dt`.

Uncommitted (this session):
- **Submodules:** basis_universal v2_50, entt v4.0.0, glfw 3.5.1, gli master tip, mimalloc v3.5.4, stb master tip,
  tinyobjloader v2.0.0rc13, vma v3.4.0.
  - imgui stays pinned: v1.92.9 and v1.93.0 break `ImGuiModule.cpp` (`IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE`,
    `MSAASamples` and `PipelineRenderingCreateInfo` moved), so it needs a port.
  - The DiamondDogs build still uses Slang's mimalloc. It's not yet tested whether v3.5.4 fixes the v3 Debug assert
    (audit section 5).
- **Spectrum amplitude (`InitSpectrum.slang`):** h0 now takes a quarter of S dK^2 inside the root.
  - The 1/sqrt(2) the author approved was half of the correction. E|xi|^2 = 2 needs one factor, and the tick's
    h0(k) + conj(h0(-k)) pair needs the other.
  - Measured Hs: 4.19 m before, 2.95 m with 1/sqrt(2) only, 2.09 m now. The fully developed value is 0.21 U^2 / g =
    2.14 m.
  - OceanFftTest now fails when Hs is outside 0.55 to 1.35 of the fully developed value.
- **`CascadeContribution`:** returns 1 / sqrt(n) for n overlapping cascades.
  - This is not the 1 / n the author approved. The cascades have independent phases, so their variances add, and
    1 / n would leave 1 / n of the energy.
  - With the demo's non-overlapping bands n = 1, so it has no visible effect yet.
- **Shader move:** the render module is now in `assets/shaders/shading/ocean`. The empty `shading/ocean/OceanFft.slang`
  stub and `compute/Ocean/OceanShading.slang` (her `ShadingParameters` stub; it is still in git history) were
  deleted.
- **LUT script:** `assets/scripts/ocean_fft_support.py` has review items 1-6, 8 and 9 fixed (see
  `ocean-lut-script-review.md`).
  - It runs as a build step (`ocean_fft_support_data`) and needs numpy and pyyaml. CMake checks for both. They were
    installed with `--user` into `C:\Python315`, the interpreter CMake picks.
  - It writes the LUTs as C++ sources in the build directory. It writes `compute/Ocean/OceanFftGenerated.slang` into
    the source tree, because the cooker only searches a module's folder. The file is rewritten only when its
    contents change.
  - `Spectra.slang` includes the generated file. Its stale copy of the fits and the duplicate
    `OceanFFT_Generated.slang` are gone.
  - The fit degrees are now 7. With samples spaced in log(s), degrees 3 and 4 had 3.2% and 0.8% error, not the 8e-4
    the old sampling reported. Degree 7 gives 7e-4.
  - The Donelan-Banner LUT (R32F, 256x256) is uploaded, and the mode is enabled in the demo. OceanFftTest runs all
    three spreading modes. Hs is 2.10 / 2.10 / 2.15 m. Donelan-Banner's 2.5% excess has not been explained yet.
  - The sin/cos LUT (1024 entries, the largest IFFT size) is generated but not uploaded. `ComplexExp` now reads it
    with an integer `Load`, and the binding has no sampler. No IFFT kernel calls that overload yet, so the sin/cos
    IFFT path (and the e^(-i) sign, review item 7) still needs a kernel variant and a test.

- **MSAA:**
  - The ocean pass renders at 8x (`k_MaxMsaaSamples` in `OceanScene.cpp`, lowered to what the device supports) and
    resolves into the swapchain image. ImGui then draws single-sampled on top.
  - Supporting additions: `ImageCreateInfo::SampleCount` (RhiResources), `ColorAttachment::ResolveImageView`
    (RhiSystem), and `ShaderObjectDrawState::Samples`.
  - Future work: make the sample count a render setting.
  - MSAA only smooths geometry edges. The distant noise is shading aliasing: the fine cascades are sampled without
    mips (see next work).

- **Swell spectrum (fifth session):** `SpectrumParameters` gained `WaveHeightScale` and `SwellScale/WindSpeed/Fetch/
  PeakingFactor/Spread/FadeLength`, and the existing `SwellDirection` is now used.
  - `SpectrumInitCS` adds a second JONSWAP (distant wind, its own direction, narrowed by `SwellSpread`) to the wind
    sea's energy. `WaveHeightScale` multiplies every amplitude; 1 is physical, and the references run at about 2.8.
  - The buffer is now 80 bytes.
  - OceanFftTest's wave height band runs with `SwellScale = 0`.
  - The uniform layout check matched names by suffix (`SwellWindSpeed` for `WindSpeed`). It now matches the last
    name component whole.
- **Cascades:** now 1024 / 192 / 32 / 6 m, so a 200 m swell fits. Hs went from 2.09 to 2.04 m (the integral gives
  2.01), and the Donelan-Banner excess disappeared, so it was k-grid sampling.
- **Horizon:**
  - The surface is 12 rings of 256 m tiles around the camera's tile: one draw per ring, a tile per instance. The
    center tile is 384 quads; each ring out halves that, down to 6. A ring's outer edge vertices snap onto the
    coarser ring's vertices, so there are no cracks. The seabed is one tile under all of it.
  - Each cascade fades out between `lodScale` (15, the "Detail distance" slider) and twice that many patch lengths,
    in both the vertex and fragment stages.
- **Scene commands:** `capture` now ends the frame's command batch, so the commands after it apply to the next
  frame.
- **OBS:** the running OBS (`obs64`) injects `VK_LAYER_OBS_HOOK`, which reports 11 validation messages of its own (an
  NT-handle memory import, present-after-write hazards). Run with `VK_LOADER_LAYERS_DISABLE=VK_LAYER_OBS_HOOK`; the
  scratchpad test helper does.

- **Sea state pipe (sixth session):** `applications/OceanFft/simulation/SeaState.hpp/.cpp`, in `ocean_simulation`.
  - It maps editor values to `SpectrumParameters` in pure stages:
    `SeaState -> ResolveWind -> ResolveSwell -> ResolveSpreading -> BuildSpectrum -> MeasureSpectrum`, composed by
    `ApplySeaState`.
  - **Wind:** wind force is Beaufort (U = 0.836 B^1.5). Openness walks the fetch from 5 km to fully developed
    (chi = 2.17e4), evenly in log.
  - **Wind sea shape:** regularity gives gamma = 1 + 6r. Ripple detail gives the fade length.
  - **Swell:** swell height and period are inverted. The period gives a distant wind
    (U = 3.5 g T chi^-0.33, fully developed), and `SwellScale` is solved so the swell's Hs is the one asked for.
  - **Readout:** `MeasureSpectrum` integrates the spectrum on the CPU the way `SpectrumInitCS` evaluates it.
    OceanFftTest checks the readout against the GPU's Hs within 10% (measured: 0.6-1.5%), including the default sea
    state with swell.
  - **Panel:** preset buttons (Glassy, Breezy, Fresh, Rough, Gale, Long swell), readouts, and tooltips. The raw
    physics is a collapsed read-only section. Spreading model and seed stay there, since the sea state does not own
    them.
  - **Pattern:** this is the first "domain transform pipe" outside Lodestone. The HDR work is to follow the same
    shape.

- **Mips and anisotropy (seventh session):**
  - Displacement and Derivatives have full mip chains (10 levels at 512). `RecordTick` fills them after the resolve
    with a chain of linear blits (`OceanSimulationImpl::RecordMips`). One view covers every mip, and the resolve's
    storage write goes to the view's base level, mip 0.
  - New in RhiSystem: `rhi::CommandBuffer::BlitImage`, `rhi::ImageBlit` and `rhi::BlitFilter`.
  - `OceanSimulation::k_TickStages` / `k_TickWriteAccess` name the tick's writes (compute and blit). The scene's
    barriers before and after the tick and `RecordInitialize`'s transition use them. Synchronization validation
    found both misses (the transition, and write-after-write across frames).
  - Vertex stage: each cascade is read at `CascadeLod(cascade, quadSize)`, the mip whose texels are as wide as the
    ring's quads (`LodParameters.y` is the texture size). The fragment stage needed no change: `Sample` picks the mip.
  - The ocean sampler is 16x anisotropic when the device supports it (`k_MaxAnisotropy`). Trilinear alone blurred the
    distance flat.
  - OceanFftTest checks that displacement mip 1 is the 2x2 average of mip 0 (measured 2.3e-4 relative RMS).
  - Captures: the horizon sparkle is gone. Motion shimmer was not checked by eye in a running window.
- **Shareable build (seventh session):**
  - The demo falls back to no validation when the layer is missing.
  - It loads `OceanFft.ldmanifest` from beside the exe.
  - `OceanFft_package` builds a folder to send.
  - The `ninja-msvc-static` preset links the static CRT into everything. See CLAUDE.md, and item 0b below.
- **GPU timers (seventh session):** `rhi::QueryPool` and `rhi::GpuTimers` / `GpuTimerBlock` / `ScopedGpuTimer` in
  RhiSystem.
  - They are compiled into Debug and RelWithDebInfo only.
  - They take no locks. Threads take and hand back blocks through atomics, and `Collect(completed_before)` frees
    blocks by submission value.
  - The OceanFft scopes feed a "GPU timings" panel, and `OceanFftBenchmark` sweeps size, radix and wave ops.
  - `docs/gpu-timers-plan.md` has the design as built. `docs/ocean-benchmark-2026-10-21.md` has the results.
  - The author is checking the debug labels in RenderDoc.
- **Rules added (seventh session):** prefer atomics, ownership handoff or a coalesce step to any mutex. Tell the
  author before adding a lock. This is in `.github/copilot-instructions.md` under Threading.
- **Fixes in passing (seventh session):**
  - `OceanScene` created its `ResourceContext` with validation hard-coded on, which asserted without the layer.
  - The `rhi::CommandBuffer` forward declarations now match the definition, a `struct`. clang warns
    `-Wmismatched-tags`, and the MSVC ABI mangles struct and class differently.
  - Stale clang-cl RelWithDebInfo Slang PCHs were deleted (a toolset version change).

## Lodestone requests C6-C9 (added to `../Lodestone/docs/diamonddogs-requests.md` on 2026-10-21, with approval)

- **C6, generated host layout headers:**
  - The cook emits a C++ header per module with each uniform block, push constant block and struct as a C++ type:
    exact std140/std430 offsets, padding members, `static_assert(sizeof)` and `offsetof`.
  - Today DiamondDogs mirrors `SpectrumParameters`, `CascadeParameters`, `OceanViewParameters` and
    `OceanGridParameters` by hand, and checks some of them at runtime (`CheckUniformLayouts`). That check had a name
    bug this session.
  - A generated header would remove the mirrors and move the check to compile time.
- **C7, exact member lookup:** `UniformMemberView` lookup by path (`FindMember("WindSpeed")`) with exact matching,
  and documented qualification. Hand-written suffix matching took `SwellWindSpeed` for `WindSpeed`.
- **C8, include search paths:** `INCLUDE_DIRS` on `lodestone_add_shader_cook` (and the cooker's `-I`).
  Build-generated Slang (`OceanFftGenerated.slang`) currently has to be written into the source tree.
- **C9, shorter diagnostics:** the "no specific policy coverage" warning prints a full C++ function signature on
  every build. Print the module and target, and leave out the source location, or put it behind `--verbose`.
  Separately, "cannot write the module cache file" appeared once during a concurrent build.
- **C10, push constants per draw in the manifest view:** this already works. Listed only to note that the
  `PushConstants()` range is what the ring draws rely on.

## Next work

0. **Done (seventh session):** mips and anisotropic filtering for the surface textures. A possible refinement: the
   slope variance lost by averaging the Derivatives mips (a "Toksvig" or LEAN-style roughness term) belongs in her
   fragment shading.

0b. **Done (seventh session): a shareable demo build.**
   - `main.cpp` picks `Full` validation only when `VK_LAYER_KHRONOS_validation` is installed, and `None` otherwise
     (with a message). `Instance::setupValidation` still has the Release-only hole: its `assert(checkLayerSupport)`
     disappears in Release, so other executables still fail on a machine without the SDK.
   - The bundle is copied next to the exe as `OceanFft.ldmanifest` after each build. `FindManifest` in `main.cpp`
     prefers that copy over the compiled-in `OCEAN_FFT_MANIFEST`, and `OceanSceneSettings::ManifestPath` carries it.
   - `OceanFft_package` writes `build/<preset>/package/OceanFft-<config>`. With `ninja-msvc-static` that is two files
     (exe and bundle). With the other presets it also holds the VC++ runtime DLLs (`InstallRequiredSystemLibraries`).
   - Checked: both packages ran from a scratchpad copy with `VK_LOADER_LAYERS_DISABLE=~all~`, and the static
     package captured a frame. The static exe imports only system DLLs and `vulkan-1.dll`.

1. **Math backport, then cameras.** Replace foundation's math outright with `C:\SoftwareDev\VeloxRhi\include\math`
   (it began as a trimmed copy of foundation's math, and adds bug fixes, cleanup and a WASM backend). Port
   `VeloxRhi/tests/unit_tests/MathTests.cpp`. VeloxRhi has no camera code, so write new cameras (arcball first) on
   that math, and replace `applications/OceanFft/ArcballCamera`. Keep the four-rows view-projection upload, or verify
   Slang's matrix layout once in a test.
2. **The sin/cos LUT IFFT variant:** a kernel option that calls the LUT `ComplexExp`. Upload `SinCosLUT`
   (R16G16_SFLOAT, 1024x1), and check it against the CPU reference in OceanFftTest.
3. **imgui port** to v1.93 in `ImGuiModule.cpp`, then bump the submodule.
4. **mimalloc:** try our v3.5.4 in place of Slang's v2.1.7 (audit section 5).
5. **Donelan-Banner's Hs excess** (2.15 vs 2.10 m): check whether it is the LUT, the alignment lerp, or the k-grid
   sampling of a narrower lobe.
6. **Done (seventh session): GPU profiling and debug labels.** All four steps are built. See the state above.
   - What the benchmark found, with fixes to try (`docs/ocean-benchmark-2026-10-21.md`):
     - **6a. Compute downsample:** replace the blit mip chain, which is about 32-47 µs of fixed cost and 60% of the
       tick at 128. Use a single-pass compute downsample, then rerun the benchmark.
     - **6b. 1024 falls off the 64 MB L2:** `IfftInput` is 67 MB there. Try fp16 storage, or fewer planes.
   - **The transfer system hook** waits for her (`docs/gpu-timers-plan.md`).
7. **HDR, as a color-domain transform pipe.** The same shape as `SeaState`, narrowed to color:
   - Scene-referred linear light, then exposure, then tonemap or display mapping, then encode for the swapchain's
     color space (sRGB, scRGB, HDR10 PQ).
   - The display's capabilities (peak and paper-white nits, from `setHdrMetadata` and the surface query) are inputs.
   - Editing must look the same on SDR and HDR displays.
   - FrameCapture's converters are the end of that pipe in reverse.

Older candidates are still open in audit.md section 8: RhiResources upgrades, features by request, the
shader-object dynamic state wrapper (`applications/OceanFft/ShaderObjectState` is the stopgap), and build hygiene.

## Settled decisions (2026-10-21)

- **Math:** foundation's math is replaced outright by VeloxRhi's. The cameras are new code.
- **Capture:** engine-wide, 8-bit sRGB PNG only. Float/EXR waits for the HDR session.
- **Agent camera control:** stdin commands between frames, plus `--script=file` with the same commands. The mouse
  keeps working alongside both.
- **LUT script:** DiamondDogs holds the canonical copy, and the author syncs VeloxRhi. The only Python dependencies
  are numpy and pyyaml.
- **Shaders:** the render module lives in `assets/shaders/shading/ocean`. The author writes the ocean fragment
  shading. The agent owns compute and the vertex stage.
- **Submodules:** sync all of them at once, then build Debug before Release.

## Answers given to the author (2026-10-21)

- **Why not the vectorized math for the camera?** It wasn't a bug in that math. I didn't learn the foundation API,
  and I wanted no possible disagreement between C++ and Slang about matrix layout. So the camera has its own small
  math, and uploads the view-projection matrix as four float4 rows.
- **Why does it look billowy?** Mostly the doubled amplitude, now fixed. With choppiness 1 the Jacobian dropped below
  0 over large areas, and the placeholder paints foam everywhere `1 - J > 0`, across all four cascades. A foam
  threshold such as `J < 0.3` is for her shading.
