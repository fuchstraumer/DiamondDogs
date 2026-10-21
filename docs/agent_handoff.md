# Agent handoff

The running handoff between sessions. Read this first. `docs/audit.md` keeps the older history and the
detailed findings (sections 4, 5, 9); new work is logged here.

Branch `dev/lodestone`. The author commits herself; the agent never commits. Check `git status` for what is still
uncommitted. Her code spacing rule is in CLAUDE.md ("Code spacing").

## State (2026-10-21, end of the fourth session)

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

## Next work

0. **Distant-surface aliasing:** generate mips for Displacement and Derivatives after the resolve pass (or fade the
   fine cascades out with distance), and sample with LOD. MSAA cannot fix this.

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
6. **HDR handling.** A separate session; the author is still forming her thoughts on it.

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
