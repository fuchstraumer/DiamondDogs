# Agent handoff

The running handoff between sessions. Read this first. `docs/audit.md` keeps the older history and the
detailed findings (sections 4, 5, 9); new work is logged here.

Branch `dev/lodestone`. The author commits herself; the agent never commits. Check `git status` for what is still
uncommitted. Her code spacing rule is in CLAUDE.md ("Code spacing").

## Start here (written in the ninth session, 2026-10-22)

The ninth session ran as Opus, the planner, and sent the build and test work to Haiku and Sonnet subagents. The
eighth session's work is committed (`3d24a659` and earlier).

**Done this session (uncommitted; check `git status`):**
- HDR steps 1-3: `DisplayProbe`, the PlatformSystem capability fixes, and `modules/ColorPipe` + `ddColor`. The state
  and the pick-up list are at the top of `docs/hdr-plan.md`. **HDR is paused** until Lodestone C13 lands.
- Lodestone request C13 (cross-module imports) was added to `../Lodestone/docs/diamonddogs-requests.md`.
- The "Single-word parameters" rule was narrowed in both `.github/copilot-instructions.md` copies (DiamondDogs and
  Lodestone): the underscore applies only in constructors and member functions.

**Hygiene done so far (uncommitted, both presets Debug, suite failed: 0):**
- Precompiled headers: `cmake/DiamondDogsPch.cmake`, `DIAMOND_DOGS_USE_PCH`. See `docs/build-time-2026-10-22.md`.
- The math port: VeloxRhi `include/math` (commit `0a1c91b`) is copied byte for byte into `foundation/include/math/`, and
  `foundation/include/Math.hpp` aliases `namespace math = velox::math;`. Sync by plain file copy. `MathTests.cpp` is
  ported to gtest (12 cases, 230 checks), and the old math tests, the integer vectors, `MathHashes.hpp`, the Lottes
  `Tonemapper.hpp` and `ddTonemapping.slang` are deleted. `foundation_tests` also passes in Release.
- Smaller items, all done: `RhiAssert.hpp` out of line, `dd_run_tests.ps1` (PS 5 and 7, plus the ColorPipe tests),
  `dd-one.cmd` (several targets), and the Release `/Ob2` flags. See `docs/build-time-2026-10-22.md`.
- `docs/container-inventory-2026-10-22.md`: no hot path justifies a container swap yet. A fixed-capacity inline vector
  and interned names come first. It also found the locks in `FrameCapture` and `SceneCommands`, which wait for the
  author's decision.
- Open for the next session with the author: the ContentCompiler rework (recipes as data, coroutines as the
  executor; see `docs/our-machinery/SYNTHESIS.md`), the two locks, and the Debug/Release cooker bundle difference
  (a Lodestone question).
- The our_machinery summaries and the decisions are in `docs/our-machinery/` (`SYNTHESIS.md` first).

**Now: code hygiene** (the author's order):
1. Build health: where the build time goes, and how to cut it (measure first).
2. The math backport from VeloxRhi (Next work, item 1), used where it applies, `ColorPipe` included.
3. The our_machinery docs (`C:\SoftwareDev\DocParse\results\our_machinery`, 18 files): subagents summarize them,
   and the author and Opus decide what to adopt, in a discussion session.
4. Then the include cleanup and the purge of unused foundation code.

**Pick up here (end of the ninth session, 2026-10-22):**
1. Read `docs/working-relationship.md` first.
2. Lodestone C7, C8, C12 and C13 are adopted. HDR is unblocked, and steps 1-3 and step 4 part A are done (see the top
   of `docs/hdr-plan.md`). The author staged part A herself, so that she can split the commits.
3. A Sonnet comment pass was running at the end: it compresses part A's comments and adds `modules/ColorPipe/README.md`.
   Its edits are unstaged on top of her staged part A. Review its diff before anything else.
4. **Next: step 4 part B, OceanFft adopts the pipe.** It is planned for the tenth session. Its inputs are in the
   part A notes in `docs/hdr-plan.md`:
   - an RGBA16F MSAA target resolving into `SceneTarget`;
   - ImGui into `UiTarget` (RGBA8 UNORM, cleared to 0);
   - `RecordComposite`;
   - the merged composite policy and `COLOR_PIPE_COMPOSITE_INCLUDE_DIRS`;
   - `Rec709ToAp1` at the ocean shaders' output (4e);
   - merge `FullscreenDrawState` with OceanFft's `ShaderObjectState`, ideally into `rhi_system`;
   - run `--resize` on the PQ pair.
5. Small open items for the author:
   - the HDR metadata uses the container's primaries, and the panel's may be better;
   - the explicit-pair path throws `std::runtime_error`;
   - the pyramid binding-array benchmark (she has not answered);
   - `git rm --cached` of the two generated `.slang` files.

**How to dispatch:**
- Only one subagent builds at a time. Two builds in one `build/<preset>` folder race.
- Read-only agents can run in parallel.
- Haiku: narrow jobs with a pass/fail check.
- Sonnet: multi-file ports and tests.
- Opus and the author: design.
- Every brief says: read the style file, edit only the listed files, no locks, no default arguments, no commit, no
  Lodestone source edits, build both presets, and report in a fixed short format.
- Another agent edits Lodestone's source now. A DiamondDogs build compiles its half-done edits, so a failure in
  Lodestone is reported, not fixed.

## Start here (written at the end of the eighth session, 2026-10-21)

The first eight sessions ran in one long conversation that was started from `C:\SoftwareDev\CUDA_Noise`. From now on
sessions start in DiamondDogs, so that conversation's memory folder and scratchpad are gone. Everything needed is in
this file, CLAUDE.md, `.github/copilot-instructions.md` and `docs/agent-scripts`.

**First steps:**
1. Read CLAUDE.md, `.github/copilot-instructions.md` (its Threading section too), and this file.
2. Run `git status` in DiamondDogs and in `../Lodestone`, and `git log --oneline -5` in both. She commits often, so
   check what landed before you assume something is uncommitted.
3. Check whether Lodestone compiles again (see "Open items").
4. Then the next task: **the HDR color pipe** (Next work, item 7). Plan it first, as a doc, and get her answers
   before you write code (see "HDR: how to start" below).

**Uncommitted at the end of the eighth session** (check `git status`; she may have committed some of it):
- `modules/GpuPyramid` (new), `assets/shaders/compute/Pyramid` (new: four `.slang` files and
  `LICENSE.nvpro_pyramid`), and `tests/integration_tests/GpuPyramidTest` (new).
- `modules/RhiResources`: `ManifestDescriptors.hpp/.cpp`, moved here from OceanFft. She already edited its includes.
- `RhiSystem`: `CommandBuffer::CopyBufferToImage`.
- OceanFft: `OceanSimulation` uses `pyramid::MipGenerator`, `OceanMips.slang` is deleted, and the CMake file has
  the bundle-copy step and the pyramid module in the cook.
- CLAUDE.md (module and test tables), and `modules/CMakeLists.txt` and `tests/integration_tests/CMakeLists.txt`.
- In Lodestone: rows C11 and C12 in `docs/diamonddogs-requests.md`. She approved editing that file. She did not
  approve Lodestone source edits.

**Open items:**
- **MSVC Debug rebuild.** After the last GpuPyramid change (5 fast levels per dispatch), MSVC Debug did not build:
  `Lodestone/client/src/ShaderManifestValidator.cpp(100): error C2078`. That file is part of another agent's
  in-flight Lodestone edit. When Lodestone compiles again, rebuild MSVC Debug and run the suite. clang-cl Debug
  passed with the final code.
- **Lodestone C11 and C12** may land with that edit: the other agent was changing `SpvModuleReader` and the
  validators.
  - C12 (binding arrays): fold `PyramidLevel0`-`15` back into `PyramidLevels[16]`. The host writer already
    handles arrays.
  - C11 (`ls_format`): replace `[format("unknown")]`.
- **RenderDoc:** she checked the debug labels; they look right.

**Scripts** (`docs/agent-scripts`; run them from PowerShell):
- `dd-build.cmd <preset> <config> [configure]` builds every test target and the demos, after VS18 vcvars64.
- `dd-one.cmd <preset> <config> <configure|noconf> <target>` builds one target.
- `dd_run_tests.ps1 -Preset <preset> -Config <config>` runs every suite and counts validation messages. A pass is
  "failed: 0". The logs go to `docs/agent-scripts/logs`. It sets `VK_LOADER_LAYERS_DISABLE=VK_LAYER_OBS_HOOK`,
  because OBS's layer reports its own messages.

**Environment facts that cost time before:**
- **Builds:** use PowerShell, not the Bash tool. The Bash sandbox does not write outside the repositories.
- **Deleting files:** `Remove-Item` is sometimes blocked. `[IO.File]::Delete(path)` works.
- **Another agent edits `../Lodestone` at the same time.** A DiamondDogs build compiles its half-done edits:
  - this session saw a cooker crash (0xC0000005) and an MSVC compile error, and neither came from DiamondDogs;
  - check Lodestone's `git status` before full builds;
  - say so when a failure is in Lodestone. Do not "fix" it there.
- **Stale clang-cl Slang PCH** ("file modified since the precompiled header was built"): delete that config's
  `cmake_pch.cxx.obj` and `.pch` under `build/ninja-clang-cl/third_party`.
- **Benchmarks:** measure on an idle GPU without validation (`VK_LOADER_LAYERS_DISABLE=~all~`). Check the GPU first:
  `(Get-Counter '\GPU Engine(*engtype_3D)\Utilization Percentage').CounterSamples | Measure-Object CookedValue -Sum`.
  She plays games on this machine, and a game made the numbers meaningless once.
- **Slang storage formats:** a storage image without `[format]` is guessed from its element type (`float4` becomes
  `rgba32f`). Validation then reports `Undefined-Value-StorageImage-FormatMismatch-ImageView` on any other view
  format. Test new compute code on the format the real consumer uses.
- **The demo's bundle:** it reads `OceanFft.ldmanifest` next to the exe. The copy is a build step now, so it is no
  longer stale after a shader-only change.

### HDR: how to start

Item 7 under Next work holds the design notes so far: a color-domain transform pipe shaped like `SeaState`, from
scene-referred linear light to exposure, then a tonemap or display mapping, then an encode for the swapchain's color
space. Write `docs/hdr-plan.md` (the shape of `docs/gpu-timers-plan.md`) and ask her before you build:
- **The display:** which surface formats and color spaces does her display report? Query them first:
  `vkGetPhysicalDeviceSurfaceFormatsKHR`, and whether `VK_EXT_swapchain_colorspace` and `VK_EXT_hdr_metadata` are
  there. Is it HDR10 (PQ, `A2B10G10R10` + `HDR10_ST2084`), or scRGB (`R16G16B16A16_SFLOAT` + `EXTENDED_SRGB_LINEAR`)?
- **The ownership line:** she writes the ocean's fragment shading. The HDR pipe changes what that shading outputs
  (linear scene light, no encode). Agree where her code ends and the pipe begins before you touch the render path.
- **Exposure:** manual first, or auto-exposure? Auto-exposure fits `pyramid::MipGenerator`: an average-luminance
  chain (log luminance, `Reduction::Average`), read from the 1x1 level. That is the first planned reuse of the
  module.
- **The tonemapper:** for example ACES-like, AgX, or a simple Reinhard on luminance, and how SDR and HDR stay
  matched (editing must look the same on both).
- **Smaller parts the pipe changes:**
  - the MSAA color target becomes RGBA16F;
  - FrameCapture's converters must handle the new swapchain formats, and PNG capture of an HDR frame needs a
    defined SDR conversion;
  - `--capture` comparisons in agent workflows depend on that.

## State (2026-10-21, eighth session; the newest work is at the end of this section, and in Next work 6a)

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
     - **6a+. Done (eighth session), uncommitted: `modules/GpuPyramid`.** NVIDIA's nvpro_pyramid (from
       `C:\SoftwareDev\vk_compute_mipmaps-main\nvpro_pyramid`, Apache 2.0) transcribed to Slang in
       `assets/shaders/compute/Pyramid`, as a general mip module. It replaced `OceanMipsCS` (below), which she had
       committed.
       - **Attribution:** every `.slang` file starts with NVIDIA's copyright and SPDX line, says that the algorithm
         is NVIDIA's, and lists our changes (Apache 2.0 section 4(b)). `LICENSE.nvpro_pyramid` sits next to them.
       - **Shaders:** `NvproPyramidFastCS` (subgroup shuffles, up to 6 levels) and `NvproPyramidGeneralCS` (any
         size, the 3-tap kernel for odd sizes).
         - Axes: `PYRAMID_REDUCTION`, an interface axis (`PyramidAverage`, `PyramidMin`, `PyramidMax`), and
           `PYRAMID_SHARED_F16`.
         - Our additions: a layer index (SV_GroupID.z), and if-chains in place of the fallthrough switches.
       - **Host:** `pyramid::MipGenerator` (programs, sampler) and `pyramid::MipTarget` (views, sets, schedule per
         texture).
         - `Record(cmd, targets)` interleaves the targets' dispatches, with one barrier between steps.
         - The fast pipeline runs only with full subgroups of 32 (nvpro never tested other sizes).
         - The dispatcher stops at 5 levels per dispatch, not nvpro's 6: 6 was 11-46% slower here.
       - **Consumers:** they cook `GPU_PYRAMID_SHADER_MODULE` into their own bundle. OceanFft does.
       - **Two Lodestone stubs**, rows C11 and C12 in the requests file (added with her approval):
         - the level bindings are `[format("unknown")]`, because Slang guesses `rgba32f` without a format, so the
           module needs `shaderStorageImageWriteWithoutFormat` and has no WGSL target yet;
         - they are 16 separate bindings with a switch, because the cook's SPIR-V validator rejects binding arrays.
       - **Moved:** `WriteManifestDescriptors` moved from OceanFft to RhiResources (global namespace), and now writes
         array bindings from `ArrayCount()`.
       - **Added:** `rhi::CommandBuffer::CopyBufferToImage`.
       - **Fixed:** the demo's bundle copy next to the exe is now its own build step. As a POST_BUILD step it went
         stale whenever only the shaders changed.
       - **Tests:** `GpuPyramidTest` has 16 cases (rgba32f and rgba16f; power-of-two, odd and non-square; average, min,
         max; fp16 cache). Min and max match bit for bit, and averages are within 1.4e-7 (rgba32f) or 4.3e-4
         (rgba16f).
       - **Checked:** all suites pass with zero validation messages on MSVC Debug and Release and clang-cl Debug, with
         the 6-level dispatcher. After the switch to 5 levels: clang-cl Debug passes and MSVC RelWithDebInfo builds
         (the benchmark ran on it).
       - **Not checked:** MSVC Debug did not build after the switch to 5 levels. The other agent's uncommitted
         Lodestone edit (`client/src/ShaderManifestValidator.cpp` line 100, C2078) breaks the MSVC client build. It is
         not a DiamondDogs error. Rebuild once Lodestone compiles again.
       - **Speed:** level with `OceanMipsCS` (`docs/ocean-benchmark-2026-10-21.md`, last section).
     - **6a. Done (eighth session), replaced by GpuPyramid above:** `OceanMipsCS` (`compute/Ocean/OceanMips.slang`,
       now deleted) replaced the blit mip chain.
       - Two dispatches per image, each five levels through groupshared memory. The source is a sampled view of one
         level, and the five targets are storage views of single levels (`MipSource`, `MipTarget0`-`4` in
         `OceanFftBindings.slang`).
       - `OceanSimulation` owns the per-level views and four descriptor sets. A dispatch with fewer than five levels
         binds its extra targets to the last level and does not write them.
       - `k_TickStages` is now `ComputeShader` only, and `k_TickWriteAccess` is `ShaderStorageWrite` only. The images
         lost `TransferDestination` and gained `SrvComputeShader`.
       - OceanFftTest now checks every displacement mip against the 2x2 average of the level the GPU wrote above it,
         not only mip 1. At 256 that covers both dispatches.
       - Mips: 32.8 to 6.1 µs at 128, and 47.1 to 16.4 µs at 512. 1024 is bandwidth-bound at 132 µs. Numbers:
         `docs/ocean-benchmark-2026-10-21.md`.
       - Checked: OceanFftTest on MSVC Debug and Release and on clang-cl Debug, with zero validation messages. The
         demo ran 240 frames in Debug with sync validation and no messages. All six configurations build. The other
         suites were not rerun: only ocean code changed.
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
