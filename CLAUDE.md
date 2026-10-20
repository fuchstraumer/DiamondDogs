# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

The style rules live in `.github/copilot-instructions.md`. Read that file first, and obey it. This file adds code
context only. It does not repeat the style rules.

Write your text in ASD-STE100 (Simplified Technical English), as the style file says.

## What this repository is now

DiamondDogs is a Vulkan engine that the author uses for research. Its current job is to host the noise graph and
planet generator (the plan is in `C:\SoftwareDev\CUDA_Noise\noise-audit-and-slang-plan.md`). The live modules are
`foundation`, `RhiSystem`, `PlatformSystem`, `RhiResources`, and `ImGuiModule`. Everything else in `modules/` is out
of the build; `docs/audit.md` section 6 says which to keep.

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
cmake --build build/ninja-msvc --config Debug --target foundation_tests rhi_system_tests platform_system_tests resource_context_tests TriangleTest ComputeManifestTest ImGuiTest
```

CMake sets `MSVC` for clang-cl too, so `if(MSVC)` means "cl-style flags", not "cl.exe". clang-cl has no `/MP` (and
gtest builds with `-WX`, so the warning fails the build) and no SVML library (`_XM_SVML_INTRINSICS_` breaks
DirectXMath). Guard those with `DIAMOND_DOGS_COMPILER_IS_CL`, which the root `CMakeLists.txt` sets.

Requirements:

- Lodestone at `../Lodestone` with its submodules. `-DDIAMOND_DOGS_LODESTONE_DIR=<path>` overrides it.
- Python with `lark` (`python -m pip install --user lark`). The extension header generator imports it.
- CMake 4.x. `third_party/gli` asks for CMake 3.1, so the root file sets `CMAKE_POLICY_VERSION_MINIMUM` around it.

For agents on Windows: run builds and tests through PowerShell. The Bash tool runs in a sandbox, and its writes
outside the repositories are not real.

### What counts as a pass

**A test passes only when it exits 0 and prints no validation message.** Search stdout and stderr for
`Validation Error` and `VUID_NAME`. RhiSystem's debug callback prints each message and does not fail the run, so
exit code 0 alone proved nothing in the first audit (a filter hid every `vkCreate*` error).

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
| `foundation_tests` | Containers (the MWSR queue, including IDs that cross 2^31, 2^32 and 2^62), math, threading primitives, delegates. |
| `rhi_system_tests` | Instance and device creation per API version, extension resolution, shader objects. |
| `platform_system_tests` | Displays, windows, swapchain creation, JSON configuration. |
| `resource_context_tests` | `ResourceContext` on a real device: every create path, uploads read back word for word, fills, copies, maps, samplers, four threads at once, and shutdown with queued work. |
| `ComputeManifestTest` | The cook-to-dispatch path. Lodestone cooks `HashFill.slang`, `rhi::ShaderLibrary` selects each variant, `ResourceContext` creates every buffer, and every output word matches the CPU. |
| `TriangleTest` | A windowed frame loop with shader objects and dynamic rendering. With `--resize`, swapchain recreation and depth images rebuilt at the new size. |
| `ImGuiTest` | `ImGuiModule` on the official backends draws the demo window. With `--resize`, swapchain recreation. |

## Where the code lives

| Path | Holds |
|---|---|
| `foundation/` | Containers, math, threading, delegates, image formats. No graphics API. |
| `modules/RhiSystem/` | Instance, device, extensions, command pools, fences, semaphores, shader objects, and `ShaderLibrary` (the Lodestone client). `GenerateExtensionAndFeatureHeaders.py` writes the extension tables from `vk.xml`. |
| `modules/PlatformSystem/` | GLFW windows, displays, the swapchain. |
| `modules/RhiResources/` | `ResourceContext`: buffers, images, samplers, uploads, through messages to a worker thread and a transfer thread. |
| `modules/ImGuiModule/` | Dear ImGui on `imgui_impl_glfw` and `imgui_impl_vulkan`. |
| `cmake/ConfigureLodestone.cmake` | Adds Lodestone, and works around its embedding issues (each one is listed in the Lodestone requests file). |
| `tests/unit_tests/`, `tests/integration_tests/` | Google Test suites, and executables with their own `main`. |
| `docs/audit.md` | The running notes: state, fixes, open findings, decisions, next steps, log. |

## RhiResources in one paragraph

Any thread calls `ResourceContext`, which turns the call into a message, pushes it on an MWSR queue, and returns a
reply. The worker thread owns the EnTT registry and a VMA allocator, creates the objects, and writes host-visible
data directly. Device-local data goes to the transfer system, which owns its own thread, allocator, staging buffers,
and command pools, and submits to the transfer queue. Each transfer signals the reply's timeline semaphore to
`k_TransferCompleteSemaphoreValue`. A consumer on another queue must wait on that semaphore in its own submit:
`ComputeManifestTest` shows the pattern. Every resource uses concurrent sharing across the queue families, so no
ownership transfer exists. The author likes this design. Keep it.

## Working with this author

The author is a woman. Use she and her.

Blunt and specific beats reassuring. If a plan is wrong, say which part and why. Check a claim rather than hedge it.

She reserves the design work she enjoys. The transfer system rework, streaming, buffer device address, and bindless
are hers: plan and explain, and do not write that code unless she asks. Conversion and port work is fine to do.

She dislikes default arguments. Propose an explicit argument, an overload, or a named constant instead.

Commit only when she asks.

## Documents

`docs/` is not tracked. Code comments must not point into it; state the fact in the comment instead. `docs/audit.md`
is the running note. Add to its log at the end of each work session.
