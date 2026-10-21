# Agent handoff

The running handoff between sessions. Read this first. `docs/audit.md` keeps the older history and the
detailed findings (sections 4, 5, 9); new work is logged here.

## State (2026-10-21)

Branch `dev/lodestone`. The author commits herself; the agent never commits.

Done this round (uncommitted unless the author committed it since; check `git status`):
- **Ocean compute** (`assets/shaders/compute/Ocean`): shaders fixed, plus new tick and resolve passes, and push
  constants for per-pass data. The full detail is the 2026-10-21 OceanFft entry in audit.md section 9.
- **RhiSystem:**
  - `ManifestProgram::PushConstants()` (audit section 8, candidate 1, done).
  - `ManifestProgramOptions` and `ShaderBinaryOptions::RequiredSubgroupSize` / `RequireFullSubgroups`, used for
    the wave-op IFFT variants (wave 32 on this NVIDIA GPU).
- **`applications/OceanFft`:**
  - `ocean_simulation`: OceanSimulation and ManifestDescriptors.
  - The demo: arcball camera, ImGui panel, a placeholder fragment shader, and a procedural seabed.
  - The render module in `assets/shaders/Ocean`.
- **`tests/integration_tests/OceanFftTest`:** headless, compared against a double-precision CPU reference. Every
  IFFT variant has relative RMS error around 2e-3.
- **Verification:** all 12 runs (the 9 earlier ones, plus OceanFftTest, OceanFft --frames=240 and a resize run)
  pass on MSVC and clang-cl, Debug and Release, with zero validation messages.
- **`docs/ocean-lut-script-review.md`:** the review of `assets/scripts/ocean_fft_support.py`.
- **Ownership:** the author writes the ocean fragment shading. The agent owns compute and the vertex stage. Her
  stubs `compute/Ocean/OceanShading.slang` and `shading/ocean/` overlap with `assets/shaders/Ocean/OceanShading.slang`
  (the placeholder), so ask where the render module should live.
  - Q6 answered (2026-10-21): move the whole render module to `assets/shaders/shading/ocean`. Remove the
    overlapping placeholder and the `compute/Ocean/OceanShading.slang` stub. Not done yet.

## Done 2026-10-21, second session (uncommitted)

- **Items 1-3:**
  - **Image readback (item 1):** `processCopyImageToBufferMessage` copies mip 0 of every layer into the buffer.
    It is reached through `ResourceContext::CopyBufferContents(image, buffer)`. Tested by
    `ImageReadsBackIntoABuffer`.
  - **Frame capture (item 2):** the new module `modules/FrameCapture` (`frame_capture`) writes 8-bit sRGB PNGs.
    It keeps one GPUToCPU buffer per frame in flight, and a worker thread writes the files with stb. It converts
    from sRGB, scRGB and HDR10 PQ.
  - **Swapchain:** it now adds TRANSFER_SRC usage when the surface allows it (`Swapchain::SupportsTransferSource`).
  - **Camera control (item 3):** `applications/OceanFft/SceneCommands` adds `--script`, `--stdin` and `--camera`.
    The command list is in the header.
- **Demo flags:** `--capture=DIR`, `--capture-count/-every/-first`, `--dt=SECONDS` (a fixed step, so captures N
  frames apart are N steps apart).
- **Verification:**
  - All 11 MSVC Debug runs pass with zero validation messages. Clang-cl and Release were not run this session.
  - Example: `OceanFft --capture=cap --capture-count=8 --capture-every=30 --dt=0.0333`.
- **Submodule sync (item 7):** done in the worktree `C:\SoftwareDev\DiamondDogs-submodules`, branch
  `dev/submodule-sync`, uncommitted.
  - Updated: basis_universal v2_50, entt v4.0.0, glfw 3.5.1, gli master tip, mimalloc v3.5.4, stb master tip,
    tinyobjloader v2.0.0rc13, vma v3.4.0.
  - imgui stays pinned: v1.92.9 and v1.93.0 break ImGuiModule.cpp, so it needs a port.
  - Debug and Release build and pass there. The exception is foundation SourcePathsTest, which fails only because
    of the worktree's folder name.
  - Not yet run against this session's uncommitted work. Also check whether mimalloc v3.5.4 fixes the old v3
    Debug assert.
- **Still owed from the grill answers:**
  - The Q5 physics change.
  - The Q6 shader move.
  - The LUT script fixes (Q4).
  - Math and cameras (item 4).

## Next work (author's list, 2026-10-21; order not fixed)

1. **Image readback in RhiResources.** Implement the long-deferred image-to-buffer copy (`CopyImageToBuffer` is
   "not implemented" in ResourceContext, audit 4.4). It's for viewing rendered frames.
2. **Frame capture for the agent.** Capture the rendered image to PNG efficiently, several frames N steps apart, to
   watch the spectrum evolve.
   - Plan: a render target with TransferSource usage, or a copy from the swapchain image if its usage allows. Then
     a ring of GPUToCPU readback buffers, so a capture doesn't stall the frame. Write the PNG on a worker thread
     with stb_image_write (stb is in third_party).
   - Demo flags: `--capture=dir --capture-every=N --capture-count=K`.
   - Q2 answered (2026-10-21): an engine-wide facility (any app can use it), 8-bit sRGB PNG only for now.
     Float/EXR output waits for the HDR session.
   - Mind HDR: the swapchain may be RGBA16F scRGB, so convert to sRGB8 for the PNG.
3. **Camera control for the agent.** Scripted camera input, so the agent can pan and rotate without a mouse. For
   example `--camera=yaw,pitch,distance,tx,ty,tz`, plus a keyframe file or stdin commands
   (`orbit dx dy`, `pan dx dy`, `zoom s`, `capture`) read between frames.
   - Q3 answered (2026-10-21): read stdin commands between frames, and add `--script=file`, which uses the same
     commands. Mouse input keeps working alongside both.
4. **Math backport, then cameras (after items 1-3).** Replace foundation's math outright with
   `C:\SoftwareDev\VeloxRhi\include\math` (it began as a trimmed copy of foundation's math; it adds bug fixes,
   cleanup and a WASM backend) and port `VeloxRhi/tests/unit_tests/MathTests.cpp`. VeloxRhi has no camera code, so
   write new cameras (arcball first) on that math and replace `applications/OceanFft/ArcballCamera`. (Q1, answered
   2026-10-21.)
5. **Fix `ocean_fft_support.py`.** Fix items 1-8 of the review, then run it as a DiamondDogs build step and upload
   the Donelan-Banner LUT (R32F) and the sin/cos LUT (R16G16F). Then enable the Donelan-Banner spreading mode and
   test the sin/cos LUT IFFT path against the CPU in OceanFftTest.
   - Q4 answered (2026-10-21): DiamondDogs holds the canonical copy, and the author syncs VeloxRhi. The only
     build dependencies allowed are numpy and pyyaml (remove matplotlib).
6. **Spectrum amplitude.** Decide on h0 * 1/sqrt(2) (Hs 4.19 m now, about 2.1 m expected) and on
   `CascadeContribution` (it multiplies by the overlap count). These are the author's physics calls.
   - Q5 answered (2026-10-21): apply h0 * 1/sqrt(2), and change `CascadeContribution` to divide by the overlap
     count. After the change, update OceanFftTest's Hs expectation (about 2.1 m).
7. **Update the submodules** in `third_party`; most are years old. One at a time, with a build and a test run after
   each. Q7 answered (2026-10-21): sync all of them at once, try a Debug build, then a Release build only if Debug
   works. A Haiku subagent does this in a separate worktree, `C:\SoftwareDev\DiamondDogs-submodules`. Watch mimalloc (it was blocked in audit section 5: the v3 page-map assert) and the imgui backend API changes.
8. **HDR handling.** A separate session; the author is still forming her thoughts on it.

Older candidates are still open in audit.md section 8: RhiResources upgrades, features by request, the
shader-object dynamic state wrapper (`applications/OceanFft/ShaderObjectState` is the stopgap), and build hygiene.

## Answers given to the author (2026-10-21)

- **Why not the vectorized math for the camera?** It wasn't a bug in that math. I didn't learn the foundation API
  (a large DirectXMath-style library), and I wanted no possible disagreement between C++ and Slang about matrix
  layout, so the camera has its own small math and uploads the view-projection matrix as four float4 rows. Pulling
  in the VeloxRhi camera is fine. Keep the rows upload, or verify Slang's matrix layout once in a test.
- **Why does it look billowy?**
  - Mostly the doubled amplitude: wave steepness is about 2x, so with choppiness 1 the Jacobian drops below 0
    over large areas. The placeholder then paints foam everywhere `1 - J > 0`, multiplied across all four cascades.
  - The flat sky color, with no reflections, adds to the effect.
  - Expected after h0 / sqrt(2) and a foam threshold such as `J < 0.3`: sharper crests and darker troughs.

## Grill the author on reinit (ask these first, one at a time)

1. **VeloxRhi camera:** what is the path to the camera and its math, and does it bring a dependency (the math
   library or the WASM fork) into foundation? Should it replace foundation's math or sit beside it?
2. **Capture:** is it a demo-only feature, or a general RhiResources/PlatformSystem facility (screenshots for
   every app)? Should captures be 8-bit sRGB PNG only, or also EXR/float for HDR debugging?
3. **Agent camera control:** command-line keyframes, a stdin command channel, or a small file the demo polls? Does
   it need to work while she is also driving the camera with the mouse?
4. **LUT script:** fix it in DiamondDogs only and port it to VeloxRhi yourself, or keep one shared copy? Which
   Python dependencies are acceptable in the DiamondDogs build (numpy, scipy, pyyaml)?
5. **Spectrum physics:** apply the 1/sqrt(2)? Divide or keep in `CascadeContribution`?
6. **Render module location:** `assets/shaders/Ocean` or `assets/shaders/shading/ocean`? Where does her shading
   go?
7. **Submodules:** update all of them, or a list? Pin to release tags or to latest?
