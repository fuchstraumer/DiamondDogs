# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

The style rules live in `.github/copilot-instructions.md`. Read that file first, and obey it. This file adds code
context only. It does not repeat the style rules.

Write your text in ASD-STE100 (Simplified Technical English), as the style file says.

## What this repository is now

DiamondDogs is a Vulkan engine that the author uses for research. Its current job is to host the noise graph and
planet generator (the plan is in `C:\SoftwareDev\CUDA_Noise\noise-audit-and-slang-plan.md`). The live modules are
`foundation`, `ShaderBundle`, `Bootstrap`, `BootstrapVulkan`, `RhiSystem`, `PlatformSystem`, `RhiResources`, `RenderGraph` (Loom), `FrameCapture`, `GpuPyramid`,
`ColorPipe`, `NoiseGraph`, `ImGuiModule`, `InputSystem`, `ContentCompiler` and `AgentTools`. `TerrainModule`,
`VtfModule` and `modules/old` are out of the build; `docs/audit.md` section 6 says which to keep. OceanFft
(`applications/OceanFft`) is the main application, and the test bed for the agent tools.

**Lodestone is the only shader path.** Lodestone (`../Lodestone`, a sibling checkout, not a submodule) cooks Slang to
SPIR-V as a build step, and `rhi::ShaderLibrary` reads the cooked bundle at runtime. There is no runtime Slang
compiler and no Slang in this repository. When DiamondDogs needs something from Lodestone, stub it here, keep the
build green, and add a row to `Lodestone/docs/diamonddogs-requests.md`.

## Build and test

Ninja Multi-Config, with two presets: `ninja-msvc` (cl.exe, in `build/ninja-msvc`) and `ninja-clang-cl` (LLVM's
clang-cl, in `build/ninja-clang-cl`). Configure and build from a shell that has run `vcvars64.bat` from Visual
Studio 18, as Lodestone's `scripts\build.bat` does. A configure from another environment writes a cache that names
another toolset. Both compilers must build and pass: check both after a change to CMake or to a header.

```bat
cmake --preset ninja-msvc
```

```bat
cmake --build build/ninja-msvc --config Debug --target foundation_tests rhi_system_tests platform_system_tests resource_context_tests loom_tests color_pipe_tests frame_capture_tests input_system_tests ocean_camera_tests sea_state_tests content_compiler_tests agent_tools_tests bootstrap_tests bootstrap_vulkan_tests ContentCooker TriangleTest ComputeManifestTest ImGuiTest OceanFft
```

For a build that runs on another machine, build `OceanFft_package` in Release on `ninja-msvc`. It writes
`package/OceanFft-Release` with every DLL the exe needs, the VC++ runtime DLLs included (app-local deployment). There
is no static-runtime preset: the author removed it on 2026-11-02, because she ships the bundled-DLL package.

CMake sets `MSVC` for clang-cl too, so `if(MSVC)` means "cl-style flags", not "cl.exe". clang-cl has no `/MP` (and
gtest builds with `-WX`, so the warning fails the build) and no SVML library (`_XM_SVML_INTRINSICS_` breaks
DirectXMath). Guard those with `DIAMOND_DOGS_COMPILER_IS_CL`, which the root `CMakeLists.txt` sets.

Requirements:

- Lodestone at `../Lodestone` with its submodules. `-DDIAMOND_DOGS_LODESTONE_DIR=<path>` overrides it.
- Python with `lark` (`python -m pip install --user lark`). The extension header generator imports it.
- The same Python needs numpy and pyyaml (`python -m pip install --user numpy pyyaml`). OceanFft's
  `assets/scripts/ocean_fft_support.py` build step imports them, and configure fails without them. On this machine
  CMake picks `C:\Python315`.
- A Python with Pillow and numpy packs the OceanFft foam textures. Set it with
  `-DOCEAN_FFT_TEXTURE_PYTHON=C:/Python313/python.exe`. Configure fails without it.
- CMake 4.x. `third_party/gli` asks for CMake 3.1, so the root file sets `CMAKE_POLICY_VERSION_MINIMUM` around it.
- For the agent sidecar and `check_engine.py`: the MCP SDK, numpy and Pillow in `C:\Python315`
  (`python -m pip install --user "mcp>=2,<3"`). `scripts/agent_sidecar/README.md` has the setup.

For agents on Windows: run builds and tests through PowerShell. The Bash tool runs in a sandbox, and its writes
outside the repositories are not real.

### What counts as a pass

**A test passes only when it exits 0 and prints no validation message.** Search stdout and stderr for
`Validation Error` and `VUID_NAME`. RhiSystem's debug callback prints each message and does not fail the run, so
exit code 0 alone proved nothing in the first audit (a filter hid every `vkCreate*` error).

**One known false positive: OBS.** OBS Studio installs an implicit Vulkan layer, `VK_LAYER_OBS_HOOK`
(`C:\ProgramData\obs-studio-hook\obs-vulkan64.json`, `graphics-hook64.dll`). The loader puts it into every Vulkan
process, and its calls can make the validation layer print messages that our code did not cause. The author has
confirmed these and ignores them. Do not ignore any other message. If you are not sure that a message comes from
OBS, run the test again with the layer off (`set DISABLE_VULKAN_OBS_CAPTURE=1`, or `$env:DISABLE_VULKAN_OBS_CAPTURE=1`
in PowerShell). A message that remains is ours. The loader line "Failed to open JSON file ... EOSOverlayVkLayer" is
not a validation message: an Epic overlay layer that is registered but no longer installed causes it.

Synchronization validation is on for `ValidationLayers::WithSynchronization` and above, and every test uses `Full`.
`Instance` sets the validation layer's `validate_sync` setting through `VK_EXT_layer_settings`, so no environment
variable is needed. This is proven: a planted present hazard in ImGuiTest reported `SYNC-HAZARD-PRESENT-AFTER-WRITE`
with the variable unset.

Run each test from its own output folder. The windowed tests (TriangleTest, ImGuiTest) take `--frames=N` and exit
after N frames, so that an unattended run ends. With `--resize` they also resize their window three times, and exit
1 unless the swapchain was recreated once per resize and matches the framebuffer. Use at least 120 frames.

A Debug `assert` in a test opens a dialog and blocks. An unattended run then looks like a hang until its timeout.
VMA asserts this way when a mapped allocation is destroyed.

| Target | Proves |
|---|---|
| `foundation_tests` | Containers (the MWSR queue, including IDs that cross 2^31, 2^32 and 2^62), math, threading primitives, timing (the monotonic clock, `HighResolutionWait`, `FrameLimiter`), delegates. |
| `rhi_system_tests` | Instance and device creation per API version, extension resolution, shader objects. |
| `platform_system_tests` | Displays, windows, swapchain creation, JSON configuration. |
| `resource_context_tests` | `ResourceContext` on a real device: every create path, uploads read back word for word, fills, copies, maps, samplers, four threads at once, and shutdown with queued work. |
| `loom_tests` | Loom with no GPU: the builder, the diagnostic catalog, the sinks (counts, text and JSON Lines output), the JSON writer, Freeze, Order and explain. |
| `content_compiler_tests` | The content compiler with no GPU: kinds, the kind registry, the parameter box, the recipe table and ingestion. |
| `input_system_tests` | InputSystem with no window: the control space, frame assembly and the record-and-replay log. |
| `ocean_camera_tests` | The OceanFft camera and its controller: the view basis, picking, pan and smoothed zoom. |
| `sea_state_tests` | The sea state descriptor and the sea command that reads it. |
| `bootstrap_tests` | The boot stages with no GPU: arguments, the config file, profiles, module selection and its refusals, module start and stop order, `Require`, the content stage against cooked test bundles, the device requirements (Required and Wanted capabilities, limits), and the boot timings. Its CMake file checks that it links no RHI. |
| `bootstrap_vulkan_tests` | The window, device and GPU stages on a real device: headless and windowed boot, the spine needs at work (a device-local upload), `Declined`, a limit and a module need that no device meets, a GPU module that requires a core module, stop and start again, and `ReadDeviceConfig`. One test opens a small window. |
| `agent_tools_tests` | AgentTools with no GPU: the registry, its schemas and text form, and the transport (`ToolServer`, `ToolRpc`) over real loopback sockets. |
| `frame_capture_tests` | FrameCapture's conversions with no GPU (the decode of every captured format and encoding, the sRGB encode, the downsample), against the double reference in `color_math`. Also `CaptureSchedule`. |
| `ComputeManifestTest` | The cook-to-dispatch path. Lodestone cooks `HashFill.slang`, `rhi::ShaderLibrary` selects each variant, `ResourceContext` creates every buffer, and every output word matches the CPU. |
| `GpuPyramidTest` | `pyramid::MipGenerator` fills every level of rgba32f and rgba16f 2D arrays, on the fast and the general pipeline, for power-of-two, odd and non-square sizes. Each level matches the CPU reduction of the level above it: min and max bit for bit, averages within float rounding. |
| `OceanFftTest` | The ocean compute chain (IFFT kernels, spectrum, tick, resolve, mips) against double precision on the CPU. |
| `TriangleTest` | A windowed frame loop with shader objects and dynamic rendering. With `--resize`, swapchain recreation and depth images rebuilt at the new size. |
| `ImGuiTest` | `ImGuiModule` on the official backends draws the demo window. With `--resize`, swapchain recreation. |
| `color_pipe_tests` | The double-precision color reference (gamuts, adaptation, transfer functions, Kelvin), `ChooseSurface` and `ResolveDisplayTarget` on the author's real display pairs, and the CPU model of the composite. |
| `ColorMathTest` | Every `ddColor` Slang function against the double reference, on the GPU. |
| `ColorPipeTest` | The HDR composite, headless: each tonemapper x encoding against the CPU model of the whole composite, within about one code value. |
| `DisplayProbe` | Not a test: prints the display's Vulkan surface pairs, DXGI and DisplayConfig data, read-only. `--platform` adds PlatformSystem's view. |
| `check_engine.py <OceanFft.exe> checks` | Not a CMake target (`scripts/agent_sidecar`). OceanFft through the MCP sidecar: captures, repeatable time, raw targets, auto exposure and GPU timings, against numpy. Run it with `C:\Python315\python.exe`. |

## Where the code lives

| Path | Holds |
|---|---|
| `foundation/` | Containers, math, threading, delegates, image formats. No graphics API. `timing/`: `MonotonicNanoseconds` and `Stopwatch` read the time (header-only, so `bootstrap` uses them without linking `foundation`); `HighResolutionWait` and `FrameLimiter` sleep. |
| `modules/ShaderBundle/` | `rhi::ShaderBundle`: reads a cooked Lodestone bundle with no device, so code that runs before the device exists can open bundles without the RHI. `rhi_system` links it. |
| `modules/Bootstrap/` | `bootstrap`: the boot stages that need no RHI (`LaunchStage`, `CoreStage`), module descriptors and profiles, `BootResult`, and the boot timings. `README.md` explains it. `bootstrap_assert_no_rhi` fails the configure when a target reaches `rhi_system`. |
| `modules/BootstrapVulkan/` | `bootstrap_vulkan`: the Vulkan boot stages (`WindowStage`, `DeviceStage`, `GpuStage`), `DeviceConfig`, and the spine needs. `modules/Bootstrap/README.md` explains it. |
| `modules/RhiSystem/` | Instance, device, extensions, command pools, fences, semaphores, shader objects, and `ShaderLibrary` (the Lodestone client). `GenerateExtensionAndFeatureHeaders.py` writes the extension tables from `vk.xml`. |
| `modules/PlatformSystem/` | GLFW windows, displays, the swapchain. |
| `modules/RhiResources/` | `ResourceContext`: buffers, images, samplers, uploads, through messages to a worker thread and a transfer thread. Also `WriteManifestDescriptors`, which writes a descriptor set from the names, bindings and array counts that the cook reflected. |
| `modules/GpuPyramid/` | `pyramid::MipGenerator`: fills mip chains with compute shaders. The shaders, `assets/shaders/compute/Pyramid`, are a Slang transcription of NVIDIA's nvpro_pyramid (Apache 2.0, attribution in each file). A consumer cooks `GPU_PYRAMID_SHADER_MODULE` into its own bundle. |
| `modules/ColorPipe/` | The HDR color pipe. `color_math` is the double-precision CPU reference. `color_pipe` is the runtime: the scene and UI targets, and the composite (exposure, tonemapper, display primaries, encode). `README.md` explains it. |
| `modules/ImGuiModule/` | Dear ImGui on `imgui_impl_glfw` and `imgui_impl_vulkan`. |
| `modules/RenderGraph/` | Loom, the render graph (namespace `loom`): the builders, the diagnostics, Freeze and Order. The author designs it. |
| `modules/FrameCapture/` | Copies images and buffers out to PNG and raw files, for screenshots and the agent tools. `src/CaptureConversion` holds the CPU conversions. |
| `modules/NoiseGraph/` | The cook of the noise field library. The Slang is in `assets/shaders/compute/Noise`, and its README credits the sources. |
| `modules/InputSystem/` | Platform-free input: the control space, frame assembly, record and replay, and the GLFW source. |
| `modules/ContentCompiler/` | The content compiler (namespace `cc`): kinds, recipes and ingestion. It depends on `foundation` only. |
| `modules/AgentTools/` | The tool surface for agents (namespace `agent`): the registry and the JSON-RPC transport, `ToolServer`. |
| `foundation/include/net/` | TCP sockets and `SocketWaker`, over `std::byte` spans. Only `src/net/SocketWin32.cpp` touches Winsock. |
| `applications/ContentCooker/` | The offline cooker console. A stub today: it boots the launch and core stages, and its CMake file proves that it links no RHI. `--boot-timings` prints the table. |
| `applications/OceanFft/` | The ocean demo. `--tool-port=N` starts the tool server on 127.0.0.1 (`0` prints the port that the system chose). `OceanSceneTools.cpp` holds the scene's tools. |
| `scripts/agent_sidecar/` | The MCP sidecar between an agent and the tool server, its tests, and `check_engine.py`. The README lists the tools. |
| `cmake/ConfigureLodestone.cmake` | Adds Lodestone, and works around its embedding issues (each one is listed in the Lodestone requests file). |
| `tests/unit_tests/`, `tests/integration_tests/` | Google Test suites, and executables with their own `main`. |
| `docs/audit.md` | The running notes: state, fixes, open findings, decisions, next steps, log. |

## RhiResources in one paragraph

Any thread calls `ResourceContext`, which turns the call into a message, pushes it on an MWSR queue, and returns a
future (a slot in the context's lock-free `FutureTable`; `ResourceContext::Wait` blocks on it). The worker thread owns
the EnTT registry and a VMA allocator, creates the objects, and writes host-visible data directly. Device-local data
goes to the transfer system, which owns its own thread, allocator, staging buffers, and command pools, and submits to
the transfer queue. Each transfer gets a ticket on the transfer timeline. `future.Completion()` returns the timeline
and the ticket, and a consumer on another queue must wait on them in its own submit: `ComputeManifestTest` shows the
pattern. Destroy every future before its context. Every resource uses concurrent sharing across the queue families, so no
ownership transfer exists. The author likes this design. Keep it.

## Working with this author

The author is a woman. Use she and her.

Read `docs/working-relationship.md` at the start of every session, and again after a compaction. It explains who she
is, how she works, the tone of this partnership, and how the planner works with subagents. Keep it current: when you
learn something new about how to work with her, add it there.

New since the ninth session (2026-10-22):

- **Explanations go in module READMEs, not in code comments.** Code keeps only short "why" lines. Agents and humans
  both read the README.
- **She stages work herself**, sometimes during an agent's run, to watch the diffs and split the commits. Never touch
  the git index. Tell her if it differs from what you expect.
- **Teach before you delegate** where she says a topic is new to her. Use first principles and concrete numbers.

Blunt and specific beats reassuring. If a plan is wrong, say which part and why. Check a claim rather than hedge it.

She reserves the design work she enjoys. The transfer system rework, streaming, buffer device address, and bindless
are hers: plan and explain, and do not write that code unless she asks. Conversion and port work is fine to do.

She dislikes default arguments. Propose an explicit argument, an overload, or a named constant instead.

Commit only when she asks.

What eight sessions of working with her showed (2026-10-21):

- **Report results straight.** When a change measures slower, say so with the numbers, then find out why. She
  wants the real outcome, not a positive spin. The nvpro_pyramid port first measured slower than the code it
  replaced. The cause was found and fixed, and the docs record both.
- **Design forks:** bring her a question with a recommendation and the reason, and she answers fast. When she says
  "go ahead exactly as you planned, no notes", do that, and do not grow the scope.
- **Approvals are as wide as their words.** "Feel free to edit the file over in Lodestone" meant the requests doc,
  not Lodestone's source. Read approvals narrowly. Stub on the DiamondDogs side, and add a request row (see "What this repository is now").
- **Locks:** she has been hurt by mutexes ("ask me how I know ._."). Tell her before you add any lock, and offer an
  atomic, an ownership handoff or a single-threaded coalesce step first.
- **Credit:** credit other people's work fully. Ported code keeps its license and copyright, says plainly that the
  algorithm is not ours, and lists what we changed ("building on the greats who came before us").
- **Tone:** candid, warm and a little dry. She jokes and apologizes for interruptions (gaming on the GPU, usage
  limits). Answer in kind briefly, then get back to the work. No flattery, no padding.
- **The chat UI can hide text.** In the desktop app's next-step box, a recap and question can be all she sees. Put
  every answer, finding and decision in the message text itself. Never write "below" about content that is only in
  the box.
- **Practical context:** the RTX 4080 desktop is shared with her games, so check that the GPU is idle before you
  benchmark. Her husband sometimes runs a packaged demo (`OceanFft_package`, DLLs bundled). That is an occasional
  build, not a constraint on every change.

### Code spacing

She reads and audits every line, so code must not look dense. Whitespace is free. In C++:

- **Before a block:** put a blank line before each control block or brace block (`if`/`else`, `for`, `while`,
  `switch`, a scoped `{}`, a multi-line lambda). Variables that exist only to feed that block's test sit directly
  above it, with no blank line, as part of its chunk.
- **After a block:** put a blank line after each closing `}`, before the next statement. That includes `continue`,
  `break` and the final `return`.
- **Between chunks:** separate chunks of straight-line code where the work changes step, for example a result
  variable declared at the top, then the setup below it.
- **No padding inside braces:** no blank line right after `{` or right before `}`. `else` stays attached to its `if`.
  Inside a `switch`, no blank lines between cases. Never put a case on one line, not even one that only returns a
  value: the label, each statement, and the `break` or `return` go on separate lines (the style file shows it).
- **Private member functions are camelCase**, like private member variables (`applyButtonDown`). The style file has
  the rule.
- **Comments:** a comment line can take the place of a blank line.
- **Reference files:** `applications/OceanFft/OceanScene.cpp` and `SceneCommands.cpp`.
- **Scope:** apply this to new code and to code being edited. Don't reformat files that aren't otherwise being
  touched.

## Documents

`docs/` is not tracked. Code comments must not point into it; state the fact in the comment instead. `docs/audit.md`
is the running note. Add to its log at the end of each work session.
