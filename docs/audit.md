# DiamondDogs audit and running notes

Started 2026-10-19. This is a running document: add to the log at the end of each work session, and move an
item from "Open" to "Fixed" when it lands. Text follows the style of Lodestone's docs (short sentences, one
fact per sentence).

Purpose of the current work: make DiamondDogs the host for the noise graph and planet generator described in
`C:\SoftwareDev\CUDA_Noise\noise-audit-and-slang-plan.md` (decision D1 there: a Vulkan compute host that reads
Lodestone-cooked SPIR-V).

---

## 1. State at a glance

| Module | State | Notes |
|---|---|---|
| foundation | Builds, 320/320 tests pass | `mwsrQueue` read-ID limit fixed 2026-10-20 (section 4.1). |
| RhiSystem | Builds, 31/31 tests pass, zero validation messages | Runtime Slang compiler removed (3 tests went with it). Command recording is `CommandBuffer` methods plus `rhi::QueueSubmit` (section 9). |
| PlatformSystem | Builds, 33/33 tests pass | Swapchain recreation implemented 2026-10-20, tested by `--resize` (section 4.3). |
| RhiResources | Builds, 26/26 tests pass on a real device, zero validation messages, also with synchronization validation (`VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true`) | Ported onto RhiSystem (section 4.4, log). Design questions in section 10. |
| ImGuiModule | Builds. ImGuiTest: 180 frames, and 150 with `--resize`, zero validation messages with sync validation | Official `imgui_impl_glfw` + `imgui_impl_vulkan` (dynamic rendering). The custom ImGuiWrapper is deleted (section 4.5). |
| TriangleTest | 240 frames, full validation, clean | `--frames=N` added for unattended runs. Correction: `ValidationLayers::WithSynchronization` loads the sync2 emulation layer, not synchronization validation. Sync validation needs `VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true` (or VkValidationFeaturesEXT). |
| ComputeManifestTest | Passes, exact | Lodestone cook, `rhi::ShaderLibrary`, headless dispatch, CPU comparison. Every buffer now comes from `ResourceContext` (host path, transfer path with a semaphore wait, readback map). |
| Everything else in `modules/` | Not in the build | Triage in section 6. |

## 2. Build and test

Build tree: `build/ninja-msvc` (Ninja Multi-Config, MSVC from Visual Studio 18, toolset 14.51). Run the
configure and the build from a shell that has the Visual Studio 18 environment (`vcvars64.bat`), exactly as
Lodestone's `scripts\build.bat` does. A configure from another environment writes a cache that names another
toolset.

```bat
cmake -S . -B build/ninja-msvc -G "Ninja Multi-Config" -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe
cmake --build build/ninja-msvc --config Debug --target foundation_tests rhi_system_tests platform_system_tests resource_context_tests TriangleTest ComputeManifestTest ImGuiTest
```

Requirements found during this session:

- **Lodestone** checked out beside DiamondDogs (`..\Lodestone`), with its submodules. Override with
  `-DDIAMOND_DOGS_LODESTONE_DIR=<path>`.
- **Python `lark`** (`python -m pip install --user lark`). `GenerateExtensionAndFeatureHeaders.py` imports it,
  and the build failed with `ModuleNotFoundError` without it. todo: check for it at configure time.
- CMake 4.x. `third_party/gli` asks for CMake 3.1, so the root CMakeLists sets `CMAKE_POLICY_VERSION_MINIMUM`
  around it.

Run the tests from each executable's own folder. TriangleTest takes `--frames=N`. Every test also needs a clean
validation log: grep stdout and stderr for `Validation Error` and `VUID_NAME`. Exit code 0 alone was not enough
in this session (section 3).

## 3. Fixed in this session (2026-10-19, branch `dev/lodestone`, based on `origin/dev/rhi`)

`origin/dev/rhi` has six commits (2026-06-15/16) that never reached `master`. `master` has no content that
`dev/rhi` lacks, so the work is based on `dev/rhi`.

### Build

- CMake 4 removed compatibility with `cmake_minimum_required` below 3.5, which `gli` uses. Root CMakeLists sets
  `CMAKE_POLICY_VERSION_MINIMUM 3.5` around `add_subdirectory(third_party/gli)`.
- `tests/unit_tests/rhi_system/CMakeLists.txt` listed `TestShaderBlob.cpp`, which does not exist in the
  repository. Removed from the list. (It may exist uncommitted on another machine.)
- `resource_context_tests` linked a library named `resource_context` that no target produces. Now links
  `rhi_resources` (which does not compile yet, see 4.4).
- DiamondDogs' own Slang (`third_party/slang`, v2026.10.2) is no longer built. Slang now comes from Lodestone
  (v2026.19-87), see section 5. `cmake/ConfigureSlang.cmake` is deleted.

### RhiSystem

| Defect | Effect | Fix |
|---|---|---|
| The debug messenger callback dropped every message that contained the text `CREATE`. | Every `vkCreate*` validation error was hidden, and so was any message that names a `*_CREATE_*` flag. Removing it showed real errors in TriangleTest at once. | Filter removed (`RhiSystem.cpp`). |
| `GenerateExtensionAndFeatureHeaders.py` wrote version dependencies as `VK_VERSION_1_x`. That macro is a feature guard defined to `1`, not the packed version (`VK_API_VERSION_1_x`). | Every extension that "requires VK_VERSION_1_x" depended on extension index 1, `VK_KHR_swapchain`. Every device with almost any modern extension enabled the swapchain extension, and a headless device failed validation (`VK_KHR_surface` missing). | Generator writes `VK_API_VERSION_1_x`. |
| The same generator keyed `promotedExtensionsMap` by `VK_VERSION_1_x`. | All four keys were `1`, so only the 1.1 row survived, and the lookup by real API version never matched. `ExtensionCoreInActiveVersion` always returned false. | Generator writes `VK_API_VERSION_1_x`. |
| `ExtensionCoreInActiveVersion` checked only the row of the exact active version. | An extension promoted in 1.1 read as "not core" on 1.4. | Checks every version up to the active one. |
| `deviceExtensionTable` was not sorted (alias entries were appended out of order), and `ExtensionIsDeviceExtension` binary-searches it. | Some device extensions, `VK_KHR_dynamic_rendering` among them, read as neither instance nor device extensions. | Generator emits a sorted, unique table. `ExtensionWrangler.cpp` `static_assert`s that both tables are sorted. |
| `ExtensionPack::ResolveDeviceDependencies` sent unsupported optional extensions to the dependency resolver, which rejects unknown names. | One unknown or unsupported optional extension threw "Failed to resolve device extension dependencies: 6". 4 RhiSystem tests failed. | Optional extensions are filtered by support first. |
| Dependency resolution went one level deep. | `maintenance5` pulled in `VK_KHR_dynamic_rendering` on 1.1, but not its dependency `VK_KHR_depth_stencil_resolve`. | Resolves to a fixed point. |
| Extensions that are core in the active version were still enabled by name. | Legal, but the validation layer then asks for the extension's pre-promotion dependencies. | Core extensions are kept out of `ppEnabledExtensionNames`. `Device::HasExtension` still returns true for them. |
| The feature chain was built from the unfiltered extension list. | Feature structs of extensions that are not enabled could be chained. | Built from the filtered list. |
| Final extension name pointers pointed into vectors that were still growing. | Safe today only because every extension name is longer than the small-string buffer. | Names are collected first, then the pointers are taken. |
| A Vulkan 1.0 instance called `vkGetPhysicalDeviceProperties2`/`Features2` and chained `VkPhysicalDeviceFeatures2` into `vkCreateDevice`. Maintenance extensions were added on 1.0. | `UNASSIGNED-API-Version-Violation` and missing-dependency errors on the 1.0 and 1.1 tests. | 1.0 uses the 1.0 queries and `pEnabledFeatures`. Maintenance extensions are only added from 1.1, and only when their instance dependencies are enabled. |
| `ShaderProgram` compute path: `VK_SHADER_CODE_TYPE_BINARY_EXT` for SPIR-V, no set layouts, no push constants, `REQUIRE_FULL_SUBGROUPS`. | Any compute shader with a binding failed to create. Any workgroup X size that is not a multiple of the subgroup size failed. | SPIR-V code type, layouts and push constants passed through, flag removed. |
| `rhi::Fence::Handle`, `Reset`, `Wait` declared, never defined. | Link errors for any caller. | Defined. |
| `TestJsonConfig.cpp` hard-coded `D:\DiamondDogs\...`. | 3 tests skipped on every other checkout. | Uses `SourcePaths.hpp`. |

### foundation tests

- `MWSRQueueTest.MultipleWritersSingleReader` stopped reading at `total_items - 1` writes with no final drain,
  so it failed now and then with 127 of 128 items. The queue was not at fault. The reader now reads until every
  item arrives, with a 10 second deadline. 50 repeats pass.

### TriangleTest

- `main()` called `Destroy()` and the destructor called it again, so every Vulkan object was destroyed twice
  (exit `0x80000003`). `Destroy()` is now idempotent.
- `VkPipeline pipeline` was never created (the test uses shader objects) and was destroyed uninitialized
  (`0xcccccccccccccccc`). Initialized to `VK_NULL_HANDLE`.
- One command pool held both frames' command buffers and was never reset, so each `vkBeginCommandBuffer`
  implicitly reset a buffer from a pool without `RESET_COMMAND_BUFFER_BIT`. Now one pool per frame in flight,
  reset after that frame's fence, which is the pattern `rhi::CommandPool` was written for.
- Updated for `ShaderProgram::CreateFromBinary` (the constructor it called was removed in `dev/rhi`).

## 4. Open findings

### 4.1 foundation

- Fixed 2026-10-20: `mwsrQueue`'s `ExitReactorData` kept "reader is locked" at bit 31 of the read ID. It is bit 63
  now, as in the ITHare original. `explicit mwsrQueue(uint64_t first_id)` starts the IDs anywhere, and two tests read
  across 2^31, 2^32 and 2^62. Checked against the old layout in Release (no asserts): the single-thread test failed
  just past 2^31, and the blocking test hung (a lost reader wakeup, the real failure mode).
- Fixed 2026-10-20: `countTrailingOnes` is `std::countr_one`, defined for a full mask.

### 4.2 RhiSystem

- Fixed 2026-10-20: the command API. `RhiFunctions.hpp` declared functions whose definitions took other parameter
  types or were empty, and nothing called them. The author chose thin `CommandBuffer` methods; the stub files are
  deleted. See the log for the API. Callers other than ComputeManifestTest still record with raw `vkCmd*`.
- `ShaderCompiler` and `ShaderBlob` (runtime Slang) are deleted (section 7). A live cook path is Lodestone request C3.
- `ExtensionWrangler::GetExtensionFeatures` asks `vkGetPhysicalDeviceFeatures2` to fill every chained struct and
  passes the result to `vkCreateDevice`, so every supported feature of every requested extension is enabled,
  `robustBufferAccess` included. That costs shader performance. Decide whether to enable features by request.
  Proposal (not implemented, the author's call): `RhiSystemCreateInfo` (and the JSON config) gets a list of
  requested feature names, for example `"VkPhysicalDeviceVulkan12Features::timelineSemaphore"`. The generator
  already emits each struct's boolean toggle names, so it can also emit a name-to-offset table. The device queries
  the chain as today, then copies out only the requested toggles into a zeroed chain and passes that to
  `vkCreateDevice`. A required toggle the device lacks throws; an optional one is skipped and reported through
  `Device::HasFeature`. The baseline that every DiamondDogs path needs today (timeline semaphores, sync2, dynamic
  rendering, shader objects, BDA once RhiResources uses it) would be a built-in default list, so callers that pass
  nothing keep working. `robustBufferAccess` stays off unless asked for.
- Fixed 2026-10-20 (first RhiSystem pass):
  - `RhiSystem::Destroy` checks every pointer and clears the messenger, so the destructor after an explicit
    `Destroy` is safe (test `DestroyTwiceIsSafe`). It also clears the static object-name device if it was ours.
  - The JSON constructor's shadowed `shaderCacheDir` was already gone in the author's cleanup commit.
  - `SplitVersionString` uses `std::from_chars` per component; it moved to `rhi::detail` so a test can reach it
    (test `SplitVersionString.ParsesEveryComponent`).
  - The debug messenger is created right after the instance, with its own `vkGetInstanceProcAddr` pointers, and
    a copy of its create info is chained into `VkInstanceCreateInfo`, so `vkCreateInstance`, `vkCreateDevice`
    and `vkDestroyInstance` messages reach our callback. It is destroyed after the device. Object naming still
    takes its function pointer from the device (`setupObjectNaming`). No test: the layer emits nothing on a clean
    instance or device creation to observe.
  - `ShaderProgram` tests for task or mesh stages first, so a mesh program is `Mesh` and binds null to the vertex
    pipeline stages. `set_difference` gets a sorted copy of the used stages (it was given caller order).
    `ConvertStageToVulkan` converts every bit of a mask (test `PushConstantRangeSharedByTwoStages`). No mesh test:
    it needs mesh SPIR-V and `VK_EXT_mesh_shader`.
  - The header generator flattens each `depends` expression into alternatives (extension set plus minimum
    version), and for every API version writes the cheapest satisfiable alternative, with its version
    requirement. A version where none is satisfiable lists the lowest version needed, which the lookup reports as
    unsupported. Before, rows read "requires VK_VERSION_1_0" where vk.xml says 1.1 (`VK_KHR_video_queue`,
    `VK_KHR_acceleration_structure`, `VK_EXT_subgroup_size_control`, ...), and some rows dropped extensions
    (`VK_KHR_ray_tracing_pipeline` lost `VK_KHR_acceleration_structure`, `VK_KHR_dynamic_rendering` lost
    `VK_KHR_depth_stencil_resolve`). Still a limit: the table holds one alternative per version, chosen by fewest
    extensions then name, so `VK_EXT_descriptor_heap` below 1.4 picks `VK_KHR_extended_flags` over the more common
    `VK_KHR_maintenance5`. The `ExtensionPack` maintenance workaround is untouched.
  - `RhiHandle::Set`/`As` already use `U` after the author's cleanup.
- Fixed 2026-10-20: synchronization validation. `WithSynchronization` used to load `VK_LAYER_KHRONOS_synchronization2`
  (sync2 emulation, which validates nothing on 1.3). It now sets `validate_sync` through `VK_EXT_layer_settings`.
- Fixed 2026-10-20: Release did not build with either compiler. `g_DeviceLossHandler` was declared only in Debug
  (`RhiAssert.hpp`), and `CommonCreationFunctions.cpp` got `<stdexcept>` only through the Debug branch.

### 4.3 PlatformSystem

- Fixed 2026-10-20: `Swapchain::Recreate` is implemented (old swapchain handed to the new one, object identity kept,
  `SurfaceHasZeroArea` for a minimized window), with `PlatformWindowSystem::RecreateSwapchain()`. The extent now
  comes from the surface's `currentExtent`. Before, it came from `ActiveDisplay`, which is the whole screen on the JSON
  constructor path. The JSON constructor also read `InitialWindowWidth` from the wrong object (fixed). `SwapchainImpl`
  no longer keeps a destroyed handle as `OldSwapchain`.
- The test fixture (`VulkanScene`) handles out of date and suboptimal, and resets the frame fence only after a
  successful acquire. Applications will need the same pattern. A recreation hook (`onSwapchainRecreated`) rebuilds
  size-dependent images. `ImGuiModule::SetMinImageCount` is not called: the image count did not change in tests.
- `PlatformWindowMode::MaximizedWindowed` throws. `FullScreenWindowed` does not clear `GLFW_DECORATED`, so it is
  not borderless.
- `Swapchain` and `DisplayColorCapabilities` declare member functions with the same name as a type
  (`ImageFormat ImageFormat()`). This changes the meaning of the name in class scope, which is ill-formed
  (no diagnostic required). MSVC accepts it.

### 4.4 RhiResources

Ported 2026-10-19 (second session). The design is unchanged: message-passing `ResourceContext`, reply objects,
EnTT registry, a worker thread, and a transfer system on its own thread. What changed:

- **VulpesRender to RhiSystem.** `ResourceContextCreateInfo` takes `rhi::Instance`, `rhi::Device`, the packed API
  version and a validation flag. Command pools are `rhi::CommandPool`, fences `rhi::Fence`, names go through
  `RhiSystem::SetObjectName`.
- **`thsvs` to sync2.** `ResourceBarriers.cpp` compiles (enum tests use `!= Invalid`). The transfer system records its
  own sync2 barriers, limited to the stages a transfer queue supports.
- **Old flag types to `ResourceFlags.hpp`.** `CreateImage` takes `ImageCreateInfo`, `CreateSampler` takes
  `SamplerCreateInfo`. `CreateInfoWrappers` is replaced by `VulkanConversions` (our create infos to Vulkan values).
- `TransferSystem.hpp` moved from `include/` to `src/`: it includes internal message types and VMA.
- 24 real tests in `tests/unit_tests/resource_context` (buffers, images, samplers, copies, fills, maps, a 4-thread
  stress test, shutdown with queued work). The placeholders are deleted.

Defects found and fixed during the port (the module never ran, so none of these showed before):

| Defect | Effect |
|---|---|
| Every transfer process function built its `TransferCommand` as a local. | Nothing recorded was ever submitted. The destructor then waited one second and threw. |
| The batch fence was created after `vkQueueSubmit` and not passed to it. | Any wait on it never returned. |
| `ForceCompleteTransfers` joined the worker without telling it to exit, and `destroy()` restarted the worker. | Deadlock on shutdown. |
| `~ResourceContext` did nothing. | Worker threads still joinable at destruction: `std::terminate`. |
| `destroy()` discarded queued messages and cleared the registry without destroying objects. | Replies never completed; every live resource leaked. |
| Buffers and images were destroyed with `vkDestroy*`, not VMA. | Every allocation leaked. |
| The sampler destroy compared against the image handle. | Destroying a sampler always failed. |
| `createImage`/`createBufferView` and their callers both emplaced the same component. | EnTT assert on any image or buffer view. |
| The registry was read with `VkBufferCreateInfo`/`VkImageCreateInfo`, but the wrapper types were stored. | Every Set/Fill/Copy failed. |
| `SetBufferDataMessage`/`SetImageDataMessage` move operations dropped the reply. | Those replies never completed. |
| `IsCompleted()` returned true for every status but Invalid; `WaitForCompletion` waited for Completed only. | Pending read as done; a Failed reply waited until the timeout (forever by default). |
| A reply with a semaphore always waited on the semaphore. | Hang for any reply that never reached the transfer system. |
| `UploadBuffer::SetData` (images) sized the vector and also emplaced each copy; it used the image's layer count for every region. | Every region recorded twice; wrong layer counts. |
| The image upload asserted `sharingMode != EXCLUSIVE` (inverted). Copies named usage layouts instead of transfer layouts. | Assert, or invalid copies. |
| Image barriers in `ResourceBarriers.cpp` had an empty subresource range. Depth writes used the fragment shader stage. `RenderTargetRead|Write` mapped to a read-only layout. `SampledImage` alone mapped to no layout (assert). | Invalid barriers. |
| `setBufferDataHostOnly` existed but nothing called it, and it always unmapped. | Host-visible buffers went through staging; persistently mapped buffers lost their mapping. |
| Map ignored the offset; Unmap never completed its reply; `CopyBuffer` did nothing; `CopyImage` was never defined. | |
| `GraphicsResource` comparison operators were `constexpr` and defined in a `.cpp`. | Unusable from any other translation unit. |
| The transfer system's message dispatch used explicit specializations in class scope. | MSVC-only. |
| `foundation`: `mwsrQueue.hpp` defined a non-inline namespace-scope `thread_local`. | LNK2005 as soon as two translation units include the queue. |
### 4.5 ImGuiModule

Moved to the official Dear ImGui backends (2026-10-19, second session). `ImGuiModule` creates the context, inits
`imgui_impl_glfw` (input callbacks chain to the ones PlatformSystem installed) and `imgui_impl_vulkan` with dynamic
rendering and a backend-owned descriptor pool, and offers `BeginFrame`, `Render(cmd)` (inside the caller's
`vkCmdBeginRendering`) and `SetMinImageCount`. The 700-line custom `ImGuiWrapper` (own pipeline, font upload,
pre-1.80 input) is deleted. `imgui_tables.cpp` was missing from the old source list.

`tests/integration_tests/ImGuiTest` draws the demo window over a cleared swapchain image. `--frames=N` now lives in
`fixtures/FrameLimit` and TriangleTest uses it too.

Open: the backend uploads font textures with its own command buffer on graphics queue 0, the queue the
application submits on. That is safe while ImGui draws on the submitting thread, which is the documented rule.
Swapchain recreation (4.3) will need `SetMinImageCount`.
## 5. Lodestone integration

### What exists

- `cmake/ConfigureLodestone.cmake` adds Lodestone (`DIAMOND_DOGS_LODESTONE_DIR`, default `../Lodestone`) with
  `LODESTONE_ENABLE_WGSL=OFF` (no Tint, no Dawn fetch). Lodestone provides the cooker and
  `lodestone::client_library`. Nothing in DiamondDogs links Slang any more.
- `rhi::ShaderLibrary` (`modules/RhiSystem/include/shaders/ShaderLibrary.hpp`): opens a cooked bundle for the
  `spirv` profile, gives each module's `ManifestIndex` for variant queries, and builds a `ManifestProgram` from
  the entry points of one variant: descriptor set layouts from the merged layout, a pipeline layout, and shader
  objects from the cooked SPIR-V. The requested entry point name is the SPIR-V name: since Lodestone request C2
  (2026-10-21) the cook sets `VulkanUseEntryPointName` and fails when `OpEntryPoint` differs from the manifest.
- `tests/integration_tests/ComputeManifestTest`: `lodestone_add_shader_cook` cooks `HashFill.slang` (one boolean
  technique axis) as a build step. The test queries each variant with `Where(...).SingleFor(...)`, reads the
  uniform member offsets and binding numbers from the manifest, dispatches headless, and compares all 4099
  output words of both variants with the same hash on the CPU.

### Lodestone embedding issues (all fixed upstream in `acce8d5`, 2026-10-21)

Lodestone's `docs/diamonddogs-requests.md` tracks E1 to E3 and C1 to C5 with their status. E1 (`${CMAKE_SOURCE_DIR}`
in the tool and test CMake files) and E2 (the `FindVulkanHeaders` module path) needed workarounds here, and they
are removed. E3: Lodestone now keeps a parent's value for `SLANG_ENABLE_MIMALLOC` and
`SLANG_ENABLE_SPIRV_TOOLS_MIMALLOC`, but DiamondDogs still uses Slang's mimalloc v2.1.7, for two reasons:
- With both options OFF, Slang skips setting SPIRV-Tools' own `SPIRV_TOOLS_USE_MIMALLOC` options, which
  default ON on Windows, so configure fails (`install TARGETS given target "mimalloc-static" which does not
  exist`). Lodestone should also force those two OFF when the Slang option is OFF. DiamondDogs can set them.
- Our mimalloc v3.0.7 then asserts at startup of every Debug executable (`page-map.c:339`,
  `_mi_page_map_register`: the page map is not initialized), because foundation's new/delete override
  allocates first. Fix that (a newer v3, or initialize early) before switching. Steps in `ConfigureLodestone.cmake`.
`MI_WIN_USE_FIXED_TLS` stays OFF.

### A Lodestone defect found and fixed (committed upstream as `5ee6508`, 2026-10-19)

`src/target/impl/SpvModuleReader.cpp`: `ParseModule` parsed a local copy of the SPIR-V words, and the parse
callbacks stored `std::string_view` names that point into that copy. Every name dangled once `ParseModule`
returned. RelWithDebInfo builds still read the old bytes by luck. A Debug cooker read the debug heap fill and
failed every cook that has a binding with `ReflectionMismatch`. Fix: `ParseState` owns the word copy
(`ParseState::Words`). The author committed it in Lodestone `5ee6508`.

### Open questions for the integration

- Push constants. `ShaderLibrary` still creates programs with no push constant ranges. Unblocked 2026-10-20: L14
  is done (format 11, `EntryPointConstants`), and `MergedLayoutView::PushConstants()` returns one
  `MergedPushConstants` (the largest stage block, offset 0, with the stage mask) when any stage has the
  `PushConstant` delivery. A `UniformBuffer` delivery is an ordinary resource in the merged layout.
- SPIR-V entry point name: done (C2, 2026-10-21). `ShaderLibrary` no longer parses `OpEntryPoint`.
- Lodestone timeline for the open requests (from its requests doc): C3 (live cook) is deferred until DiamondDogs
  needs hot reload; `RunCook` with `MemoryOutputSink` works in-process but has no public header yet. C4 (Pointer
  and Handle placements on SPIR-V, for BDA and bindless) is deferred to phase F step F4, after the VeloxRhi
  rendergraph works on WebGPU. C5 (device capability filter) is deferred to F3. So RhiResources BDA and bindless
  work in DiamondDogs can't get manifest support soon; plan it around Bound placements, or raw addresses.
- Lodestone review 2026-10-20 (read only, no build): the three CMake embedding issues in the table above are
  unchanged (`${CMAKE_SOURCE_DIR}` in the tool and test CMake files, the module path prepended only when this
  file finds Vulkan itself, mimalloc forced ON). L18 (`ls_extent_1d`) is done. L17 (resource roles such as
  `IndirectDispatch`/`IndexData`, free `[ls_resource_tags]`, and `ResolvedResource::RequiredBufferUsage` for the
  creation usage) is in the uncommitted working tree, format 12. It bears on the RhiResources usage flags: a buffer's
  creation usage could come from the manifest. L17 was committed upstream as `0a4e3fd` on 2026-10-21.
- Runtime compilation: decided (section 7). The runtime compiler is gone; a live cook is Lodestone request C3.

## 6. Inactive modules: keep or discard

| Module | Last change | Depends on | Recommendation |
|---|---|---|---|
| RenderGraph | 2025-07 | RenderingContext, ShaderTools (`st::`), VulpesRender | Discard the code. VeloxRhi's rendergraph is the live design; port ideas, not code. |
| TaskGraph | 2025-08 | C++ coroutines only | Keep as reference (shelved on purpose per `modules/CMakeLists.txt`). Revisit for cook scheduling. |
| ContentCompiler | 2025-08 | coroutines, tinyobjloader | Keep for mesh import, but it is not on the noise path. Light audit only. |
| MaterialModule | 2025-07 | RenderingContext, VulpesRender, ShaderTools | Discard. Lodestone's manifest and parameter blocks replace descriptor templates. |
| TerrainModule | 2025-08 | ECS, glm, one `vpr::Device` | Keep the quadtree idea for the planet renderer later. The CPU `NoiseGen` is superseded by the Slang field library. |
| VtfModule | 2025-08 | RenderingContext, ShaderTools, VulpesRender, coroutines | Discard the module. Its Slang shaders compile in `TestShaderCompiler` (`CompileVtf`), so keep the shaders as a Lodestone cook test asset. |
| applications/NoiseGraph | 2019 | GLSL compute shaders, empty CMakeLists | Discard. The shaders predate the field library plan, and `Jordan2D.comp` does not compile (`p` and `seed` are undeclared). |

## 7. Decisions (author, 2026-10-19)

- `dev/lodestone` is committed and pushed. The Lodestone `SpvModuleReader` fix is committed by the author.
- Lodestone stays a sibling checkout (`../Lodestone`), not a submodule.
- `third_party/slang` submodule is removed. Slang comes only from Lodestone.
- **The runtime Slang `ShaderCompiler` is replaced by Lodestone.** Delete `ShaderCompiler`, `ShaderCompilerReply`,
  `ShaderCompilerMessages`, `ShaderBlob` and their tests; `ShaderLibrary` + `ShaderProgram::CreateFromBinary` are
  the shader path. A capability Lodestone lacks gets a stub here (keep the build green) and an entry in
  `Lodestone/docs/diamonddogs-requests.md`.
- **RhiResources: full port onto RhiSystem.** Be careful: the author is happy with the design (message-passing
  `ResourceContext`, reply objects, ECS registry, worker thread). Keep the design; replace VulpesRender, `thsvs`,
  and the old flag types. The transfer system likely needs rework. No streaming exists. Buffer device address and
  bindless (setting and selectively filling those resources) are an open design space: write a design note before
  code.
- **ImGuiModule: move to the official Dear ImGui backends** (`imgui_impl_glfw`, `imgui_impl_vulkan` with dynamic
  rendering), driven by PlatformSystem and RhiSystem.

## 8. Next steps, in order

Status 2026-10-20: steps 1, 2, 4 and 5 are done (log). Step 3 waits for a session with the author.

1. Remove the runtime compiler (section 7) and fix the fallout (tests, RhiSystem CMake still links `slang` and
   includes `${DIAMOND_DOGS_LODESTONE_DIR}/third_party/slang/include`; drop both if nothing else needs them).
2. RhiResources port. Order: `ResourceFlags`/`ResourceBarriers` compile (enum to bool via `BitmaskTrueType` or
   `!= Invalid`), then `CreateInfoWrappers` and the internal message types on the new flag types, then
   `UploadBuffer`/`TransferSystem` onto `rhi::Device`, `rhi::CommandPool`, `rhi::Fence` and sync2 barriers, then
   `ResourceContextImpl` (VMA init from `rhi::Device`/`Instance`, `RhiSystem::SetObjectName`). Public API: move
   `CreateImage` to the new `ImageCreateInfo`. Replace the placeholder tests with real ones, and switch
   `ComputeManifestTest` buffers to `ResourceContext` as the integration proof.
3. Design note: transfer system rework, streaming, buffer device address, bindless.
4. ImGuiModule on official backends, plus a small windowed test.
5. Port agent instructions (`CLAUDE.md`, `.github/copilot-instructions.md`) from Lodestone's, adapted to DD.

### Candidates for the next work (2026-10-20, after the RhiSystem passes)

Not ordered by the author yet. Roughly in order of how much they unblock the noise host:

1. ~~Push constants through `ShaderLibrary`~~: done 2026-10-21 for the ocean demo (`ManifestProgram::PushConstants`).
   OceanFftTest is its test: the IFFT, tick and resolve passes all take their per-dispatch data as push constants.
2. **RhiResources upgrades** (docs/rhi-resources-design.md, the author's design): the transfer timeline,
   retirement, streaming, buffer device address, bindless. Questions in section 10. The design is the author's,
   so this one starts with a session together. L17's `RequiredBufferUsage` may feed `BufferUsageBits`.
3. **Enable device features by request** (section 4.2 proposal). Needed before BDA (bufferDeviceAddress) is
   enabled on purpose rather than by accident, and to turn `robustBufferAccess` off.
4. **Small RhiSystem gaps found while porting:** the transfer thread still polls with the backoff sleeper (the
   author doubts the sleeper); `Device` has no wrapper for the shader-object dynamic state, so TriangleTest still
   loads ~20 `vkCmdSet*EXT` pointers; the present semaphore is per frame in flight, while the spec wants one
   per swapchain image (fixed 2026-10-21); acquire and present are still raw Vulkan in `VulkanScene`.
5. ~~Lodestone embedding fixes upstream~~: done upstream (`acce8d5`), workarounds removed 2026-10-21.
6. **Build hygiene** (noticed, untouched): Release flags copied from RelWithDebInfo, `XM_AVX2_INTRINSICS_`
   missing its leading underscore, `SwapchainImpl.hpp` includes above its include guard, an unused variable in
   `Math.inl:2188`, `-fno-omit-frame-pointer` ignored by clang-cl.

## 9. Log

- 2026-10-19. First audit. Branch `dev/lodestone` from `origin/dev/rhi`. Build green for foundation, RhiSystem,
  PlatformSystem, TriangleTest, ComputeManifestTest. Lodestone integrated (sibling checkout). Fixes in section 3.
  Paused before the RhiResources port: headers read, no RhiResources code changed yet. Nothing is committed in
  either repository. Next: port RhiResources onto RhiSystem (section 4.4), then move ImGuiModule to the official
  Dear ImGui GLFW and Vulkan backends (dynamic rendering), then port the agent instructions from Lodestone.
- 2026-10-19 (second session). Step 1 done: `ShaderCompiler`, `ShaderCompilerReply`, `ShaderCompilerMessages`,
  `ShaderBlob`, `TestShaderCompiler` deleted. `ShaderTypes.hpp` lost the compile options, `ShaderStage`, and the
  reflection structs that only the compiler filled. `rhi_system` no longer links `slang` or includes its headers.
  Every `copy_slang_dlls_to_target` call is gone, and the integration test DLL copy skips an empty list (it failed
  the link step). No Slang DLL sits beside any test now. All suites green, zero validation messages
  (rhi_system 25/25: the 3 removed tests were the compiler's). No Lodestone capability was missing for this step.
- 2026-10-19 (second session, cont.). Step 2 done: RhiResources ported and tested (section 4.4). ComputeManifestTest
  now takes every buffer from `ResourceContext`. All suites green with zero validation messages, and also with
  `VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true`. The setting is proven to take effect: it caught a real present hazard in the first ImGuiTest draft. One hang during the run: a VMA debug assert (destroy of a mapped buffer)
  opened a dialog and blocked the unattended test. Fixed in the context (destroy undoes user maps), and noted: a Debug
  assert in an unattended run blocks until the runner's timeout. Step 3 (the design note) is left for the session with
  the author, as she asked. Inputs for it are in section 10.
- 2026-10-19 (second session, cont.). Step 4 done: ImGuiModule on the official backends, ImGuiTest added (section
  4.5). TriangleTest and ImGuiTest pass 180 frames under sync validation. Correction to an earlier line in this file:
  `ValidationLayers::WithSynchronization` loads the sync2 emulation layer; sync validation itself is the environment
  setting above (RhiSystem does not enable it; worth adding as a `ValidationLayers` level).

- 2026-10-20. Step 5 done: `CLAUDE.md` added (code context, build, the pass rule, module map, the RhiResources design
  in one paragraph, working with the author). `.github/copilot-instructions.md` merged: Lodestone's current
  "Response style" and "Code style" sections (copied, with Lodestone as the source of truth), DiamondDogs' own
  conventions kept, the rules they superseded listed, and the PR review section unchanged. One contradiction in the
  old file is flagged, not resolved: it gave the depth range as both [0,1] and [-1,1]. Nothing is committed.
- 2026-10-20. Author's answers applied (section 10): spans of bytes for initial data, opt-in transfer usage bits
  (new `ImageUsageBits::TransferSource`), two new tests (26 total). Depth range recorded as [-1,1] with Claude's
  objection. The design note is now its own file, `docs/rhi-resources-design.md`. All suites green under sync
  validation. Nothing is committed.
- 2026-10-20. Tooling: `CMakePresets.json` (Lodestone's, plus compile-command export), `.clang-format`, `.clang-tidy`,
  `.clangd`, and a local `.vscode/settings.json` (clangd first, Microsoft IntelliSense off). Two clang errors fixed in
  foundation: `enum VkFormat : int` forward declaration (the conversions moved to `ImageDataFormatsVulkan.hpp`), and
  `Float4`'s union declared `w` twice (`u, v, w, t` is now `u, v`). clangd parses cleanly; all suites green under sync
  validation. `docs/velox-integration-plan.md` written (deferred by the author). The author is reviewing the changes.
  Nothing is committed.
- 2026-10-20 (later). The author committed everything except `docs/`. Work this session, uncommitted:
  - clang-cl builds. The presets were already on clang-cl; the breakage was `if(MSVC)` passing `/MP` (an error under
    gtest's `-WX`) and `_XM_SVML_INTRINSICS_` (DirectXMath has no SVML on clang). `DIAMOND_DOGS_COMPILER_IS_CL`
    guards both. `/JMC` moved to the configs with `/Zi`. Two code errors: `reinterpret_cast` in a `constexpr`
    (`ResourceMessageReply.cpp`), and `GetWindowHandle()` returned `const void*` after the const_cast removal (it
    returns `void*` now, and TestSwapchain's const_cast is gone).
  - Swapchain recreation, the `mwsrQueue` read-ID fix, and sync validation through `ValidationLayers` (section 4).
  - Release fixed for both compilers (section 4.2).
  - Result: MSVC and clang-cl, Debug and Release, all 9 runs each (7 suites plus both `--resize` runs) pass with zero
    validation messages, and no environment variable.
  - MWSR queue pass (the author asked for open-ended tweaks):
    - The ResourceContext worker polled `try_pop` with `ExponentialBackoffSleeper` defaults (1 ms to 500 ms, ±30%).
      After about 75 ms idle, a request waited up to about 650 ms. The worker now blocks in `pop()`, and shutdown is
      an `ExitWorkerMessage` in the payload variant (no `shouldExitWorker` flag). New test
      `RequestAfterIdleIsServedPromptly` (3 rounds of 800 ms idle, each request under 100 ms). The old loop failed it
      (214 ms, 260 ms). The transfer thread still polls (1 to 30 ms), because it also waits on fences; it could block
      when nothing is queued or in flight. The author doubts the sleeper is worth keeping.
    - `mwsrQueue` static_asserts that `T` is nothrow move-constructible and move-assignable (a throwing move after the
      ID allocation would jam the queue forever). Removed the namespace-scope `lockedThreadsListTLS_data`, which the
      class's own static member shadowed. `startRead` uses `countTrailingOnes`. No `std::move` on returned locals.
    - Entrance data, exit data and the locked writer list each `alignas(128)`. Measured with a standalone Release
      benchmark (7800X3D, 1 to 7 writers, `pop` and `try_pop` readers, 2 passes of 9): 10-25% more throughput than
      packed, and 64 and 128 alike. 128 for Intel's paired-line prefetcher.
  - Not touched, noticed: `CMAKE_CXX_FLAGS_RELEASE` is built from the RelWithDebInfo flags (so Release has `/Zi` and
    `/Ob1`). `XM_AVX2_INTRINSICS_` lacks DirectXMath's leading underscore, so it does nothing. Lodestone passes
    `-fno-omit-frame-pointer`, which clang-cl ignores with a warning (it wants `/Oy-`).
- 2026-10-20 (RhiSystem pass 1). The author's clang-tidy and cleanup commits landed first. Fixed the section 4.2
  bugs except the command API (details there). Proposed, not built: enabling features by request. Two fixes
  outside RhiSystem: `TriangleTest.cpp` used an unqualified `Result` after the cleanup (now `rhi::Result`), and
  the clang-cl Release slang PCHs were stale after a Windows SDK update (deleted the `cmake_pch.cxx.obj`/`.pch`
  pairs so ninja rebuilt them). MSVC and clang-cl, Debug and Release: 9 of 9 runs each, zero validation messages,
  rhi_system_tests at 28. Next: the command API (`RhiFunctions.hpp`), then the RhiResources upgrades.

- 2026-10-20 (RhiSystem pass 2, command API). The author picked thin `CommandBuffer` methods over keeping the free
  functions or staying on raw Vulkan. `RhiFunctions.hpp` and `RhiFunctionsVk.cpp` are deleted (no callers).
  - `RhiFlags.hpp`: `PipelineStage` and `Access` (64-bit, sync2 values), `ImageLayout`, `ImageAspect`,
    `PipelineBindPoint`, `IndexType`. Their values equal the Vulkan ones, like `LoadOp`, and
    `src/core/VulkanConversions.hpp` (internal) static_asserts every one, so the Vulkan side casts. A DX12 backend
    would map them to enhanced barriers.
  - `RhiTypes.hpp`: `GlobalBarrier` (not `MemoryBarrier`, which winnt.h defines as a macro), `BufferBarrier`,
    `ImageBarrier`, `ImageSubresourceRange`, `DependencyInfo`, `BufferCopy`, `BufferImageCopy`,
    `SemaphoreSubmitInfo` (built from a timeline handle and value, or a binary handle), `SubmitInfo`. Attachments
    gained a `Layout`, and `DepthStencilAttachment` a `HasStencil` that also binds the stencil attachment.
  - `CommandBuffer`: `BeginRendering`/`EndRendering`, `SetViewports`/`SetScissors` (the with-count forms, which
    shader objects need), `BindShaders(ShaderProgram)`, `BindDescriptorSets`, `PushConstants`, `BindIndexBuffer`,
    `BindVertexBuffers`, `Draw`, `DrawIndexed`, `Dispatch`, `Barrier(DependencyInfo)`, `FillBuffer`,
    `CopyBuffer`, `CopyImageToBuffer`. Each records one 1.3 core command and asserts it is recording.
  - `Queue.hpp`: `rhi::QueueSubmit` (one or many batches, `vkQueueSubmit2`, optional fence) and `QueueWaitIdle`.
  - The shader stage conversion moved from `ShaderProgram` into `VulkanConversions.hpp` (`ToVulkanStages`), shared
    with `PushConstants`.
  - Tests: `TestCommandBuffer.cpp` runs with synchronization validation. Fill, barrier, copy, host barrier, timeline
    signal (`FillCopyAndSignalTimeline`); undefined-to-attachment transition, clear through dynamic rendering,
    attachment-to-transfer transition, image readback, checked per pixel (`ClearRenderAndReadBack`); two batches
    chained by a timeline wait in one submit (`BatchesChainThroughTimelineWaits`). Removing the fill-to-copy
    barrier gives `SYNC-HAZARD-READ-AFTER-WRITE`, so the zero-message result means something. ComputeManifestTest
    now records and submits through the new API (shaders, descriptor sets, dispatch, barriers, copy, and a
    timeline wait on the upload).
  - Not ported: TriangleTest, ImGuiScene and the VulkanScene fixture still use raw `vkCmd*` and `vkQueueSubmit2`.
    The clang-cl Debug slang PCHs went stale too (same Windows SDK cause); cleared the same way.
  - MSVC and clang-cl, Debug and Release: 9 of 9 runs each, zero validation messages, rhi_system_tests at 31.
    Next: the RhiResources upgrades (docs/rhi-resources-design.md).

- 2026-10-20 (RhiSystem pass 3, callers ported). The author asked to port the remaining raw-Vulkan callers.
  - `VulkanScene` fixture: the frame semaphores and fences are `rhi::BinarySemaphore` and `rhi::Fence`, and a
    new `submitFrame(CommandBufferHandle)` submits through `rhi::QueueSubmit` (wait on the acquire semaphore at
    color attachment output, signal the present semaphore at all graphics, the frame fence). Acquire and present
    are still raw Vulkan (no RHI swapchain calls exist).
  - `ImGuiScene` and `TriangleTest` record through `rhi::CommandBuffer` (barriers, dynamic rendering, shaders,
    descriptor sets, vertex and index buffers, viewport and scissor, draw) and submit with `submitFrame`. The
    barriers are the same stages, accesses and layouts as before. TriangleTest dropped the
    `vkCmdBindVertexBuffers2EXT` and `vkCmd*WithCountEXT` pointers; the ~20 shader-object dynamic state pointers
    stay (no RHI equivalent).
  - **Not verified at the time (verified 2026-10-21, below).** The MSVC Debug build compiled every changed file, and every test target linked except
    ComputeManifestTest: Lodestone's cooker console failed to link (`EnvironmentView::ResourceTags`) because
    another agent is changing Lodestone's working tree right now (L17, format 12). The author stopped the test
    run, so that work is not disturbed and no files are locked. Run the 9-run suite on both compilers once
    Lodestone settles. ComputeManifestTest's sources did not change in this pass.
- 2026-10-21 (pass 3 verified; the author committed passes 2 and 3 as b6824418 and 113b2d9e). Built against
  Lodestone's staged L17 work (the author reports it complete and validated).
  - The first run failed TriangleTest on all 4 configurations with `VUID-vkCmdDraw-None-09600`. This bug is older
    than the port: the color barrier's old layout came from `firstFrame[currentFrame]`, which is per frame in
    flight, but the image is the one acquired, so a fresh image could be claimed to be in `PRESENT_SRC`. Fix: the
    color attachment is cleared on load, so the barrier always starts from `Undefined`.
  - One MSVC Debug run then reported `VUID-vkQueueSubmit2-semaphore-03868` (present semaphore still in use by
    the swapchain), the section 8 item. Fix: `renderFinishedSemaphores` is indexed by `currentAcquiredImage`.
    Both arrays are sized to the image count. Swapchain recreation does not resize them, which is only correct
    while the image count stays the same.
  - Result: 9/9 on ninja-msvc and ninja-clang-cl, Debug and Release, with zero validation messages.
  - Lodestone warning on clang-cl: `SlangReflector` is declared as a struct and defined as a class
    (`-Wmismatched-tags`, SlangReflector.hpp:21). Pass to the author for the Lodestone side.

- 2026-10-21 (Lodestone embedding fixes and C2 adopted).
  - `ShaderLibrary` uses the requested entry point name; the `OpEntryPoint` parser is deleted.
  - `ConfigureLodestone.cmake`: the `cooker_console` include workaround (E1) and the module path prepend (E2) are
    removed. Switching to our own mimalloc (E3) failed twice; see section 5. Slang's copy stays.
  - The mimalloc assert showed as a CRT Retry dialog during the build's gtest discovery step. Debug asserts
    block unattended runs; the run helper now skips tests when the build fails, so stale binaries don't pass.
  - Result: 9/9 on ninja-msvc and ninja-clang-cl, Debug and Release, zero validation messages.

- 2026-10-21 (OceanFft compute, headless test and demo). The author asked for the compute shaders fixed, a headless
  test, the subgroup variant with full subgroups on this NVIDIA GPU, the vertex stage, and a demo under `applications`.
  - Shaders (`assets/shaders/compute/Ocean`): `IFFT_SIZE` limited to 128-1024 (one row per workgroup of
    `IFFT_SIZE` threads, 1024 is the limit). `IFFT_LOG4_WAVE_SIZE` = (log2 wave - 1) / 2: the old constant 2 was
    only right for waves 32 and 64. `InitSpectrum` fixed (names, includes, the spread-mode constants, per-set
    bindings gone) and `SpectrumConjugateCS` written. New `OceanTick.slang` (time evolution, eight fields packed
    two per complex value into two slices per cascade) and `OceanResolve.slang` (unpack, checkerboard sign, scale,
    Jacobian, rgba16f array textures). The IFFT scale is split as IFFT_SIZE before and after, so heights stay normal
    in half. IFFT parameters, time and choppiness are entry point uniforms (push constants). `SampleHasselmanLUT`
    reads the globals (the cook could not trace a sampler pair through parameters) and uses `SampleLevel`.
  - RhiSystem: `ShaderBinaryOptions::RequiredSubgroupSize` and `RequireFullSubgroups` (compute only, opt-in), and
    `ManifestProgramOptions` on `ShaderLibrary::CreateProgram`. `ManifestProgram::PushConstants()`.
  - `applications/OceanFft`: `ocean_simulation` (OceanSimulation, ManifestDescriptors) shared by the demo and
    `tests/integration_tests/OceanFftTest`. `ocean::PickWaveSize` picks the device subgroup size (32 here) when
    size control, full subgroups and shuffles are supported.
  - OceanFftTest: Radix2 and Radix4, with and without wave 32, at 256 and 512, against a double CPU FFT: relative
    RMS 2e-3. Conjugate pass bit exact. Tick, IFFT and resolve against the CPU from the GPU's own h0: all eight
    fields 5e-4 to 3e-3.
  - Render module `assets/shaders/Ocean` (OceanRender): grid from SV_VertexID, all cascades displaced at the vertex,
    a placeholder fragment shader (the author writes the shading), a flat procedural seabed. The demo: arcball
    camera, ImGui panel (spectrum, choppiness, cascade toggles, sun, seabed depth). Validation clean.
  - Findings for the author: the spectrum gives Hs 4.19 m at 10 m/s and 100 km fetch, about 2x JONSWAP (h0 has no
    1/sqrt(2), and the two Gaussians already have E|xi|^2 = 2). `CascadeContribution` multiplies by the number of
    cascades that cover k instead of dividing. The Donelan-Banner LUT script needs fixes before it is used: see
    docs/ocean-lut-script-review.md (quadrature at large swell, linear V axis, transposed LUT, sampling function,
    sin/cos LUT size, C++ linkage). Donelan-Banner mode has a 1x1 stand-in LUT until then.
- 2026-10-21, sessions five to seven: logged in `docs/agent_handoff.md` ("State").
  - The swell spectrum and the SeaState pipe.
  - The horizon rings, MSAA, mips and anisotropy.
  - The shareable static-CRT package.
  - The GPU timers (`QueryPool`, `GpuTimers`) and `OceanFftBenchmark`. The results are in
    `docs/ocean-benchmark-2026-10-21.md`.
- 2026-10-21, session eight: the `OceanMipsCS` compute downsample replaces the blit mip chain (handoff, Next work
  6a). The mip chain is 4-5x faster up to 512. OceanFftTest now checks every mip level.
  - Then `modules/GpuPyramid`: NVIDIA's nvpro_pyramid transcribed to Slang with attribution. It replaced
    `OceanMipsCS` at the same speed, and is checked by `GpuPyramidTest`. Lodestone requests C11 and C12 were added.

### 2026-10-22 (ninth session, Opus planning with Haiku and Sonnet subagents)

- HDR plan written (`docs/hdr-plan.md`), and every design question answered: AP1 working space, SB2383 AgX on
  Rec.2020 primaries, the shoulder limit from headroom, and the pyramid-based metering (the author writes it).
- The DisplayProbe found the `SDRWhiteLevel` units bug (4350 raw = 348 nits) and confirmed the display's pairs: HDR10
  PQ 10-bit and scRGB FP16.
- PlatformSystem no longer writes display state during a capability query, and DXGI is its first source.
- `ColorPipe` + `ddColor`: the GPU agrees with the double reference within about 2e-7 (matrices) and 5.3e-5 (PQ).
- Lodestone C13 added. HDR paused until it lands. The next work is code hygiene.
- Hygiene, all verified on both presets:
  - precompiled headers (`DIAMOND_DOGS_USE_PCH`);
  - the VeloxRhi math port (byte-identical copy and a namespace alias);
  - Release `/Ob2`;
  - `RhiAssert` out of line;
  - the script fixes;
  - `FrameCapture` and `SceneCommands` moved off mutexes onto `mwsrQueue` plus `std::atomic::wait`, with a memory cap
    on capture jobs.
- The our_machinery review: `docs/our-machinery/SYNTHESIS.md`. Build time: `docs/build-time-2026-10-22.md`.
  Containers: `docs/container-inventory-2026-10-22.md`.
- Lodestone C7, C8, C12 and C13 adopted. HDR step 4 part A (`color_pipe` and `ColorPipeTest`) is done. Part B
  (OceanFft) is next.
- `docs/working-relationship.md` written, so that the working relationship survives compaction.

### 2026-10-22 (tenth session, Sonnet: HDR step 4, part A)

- `modules/ColorPipe` has two targets now. `color_math` is the double reference. `color_pipe` is the runtime: `ChooseSurface`,
  `ResolveDisplayTarget`, the CPU reference of the composite, and `color::ColorPipe`.
- `assets/shaders/shading/ColorPipe/ColorComposite.slang` is the composite pass. It has the `TONEMAPPER` interface axis
  (`Passthrough`, `FalseColorEv`) and the `ENCODE` enum axis, and cooks 6 variants.
- PlatformSystem: `Swapchain::QuerySurfaceFormats`, `PlatformWindowSystem::QuerySurfaceFormats`,
  `SwapchainCreateInfo::ExplicitSurfaceFormat`, `CreateSwapchainWithSurfaceFormat` and `Swapchain::SetHdrMetadata`.
  `TriangleTest --surface-pairs` creates a swapchain with each pair of the LG (7), PQ and scRGB included.
- Foundation fix: `ToVkFormat` had no BGRA8 case, so it returned `VK_FORMAT_UNDEFINED` for `B8G8R8A8_UNORM`. An explicit
  swapchain pair and an offscreen BGRA8 image both need it.
- `ColorPipeTest` (headless) compares every pixel of the composite with the double reference, for 2 tonemappers and 3
  encodings, after 3 exposures, a `SetOverrides` and a `Resize`. The largest errors are 0.56 code (sRGB), 0.56 code (PQ)
  and 1 ulp (scRGB). This GPU truncates when it writes `R16G16B16A16_SFLOAT`, so 1 ulp is the bound there.
- OceanFft does not use the pipe yet (part B).

### 2026-10-24 (Opus with a Sonnet subagent: shader executable statistics, phase 1)

- `PipelineExecutableInfo.*` is deleted. It had no live caller, it could not query shader objects, and it had memory
  bugs (heap overflow when the driver count exceeded the caller's count, leaking move assignments, `sType` 0).
- New `rhi::CaptureComputeStatistics` (`shaders/ExecutableStatistics.*`) compiles the same `ShaderBinaryOptions` as a
  temporary compute pipeline with the capture flag and returns plain values. `modules/RhiSystem/README.md` is new and
  explains the probe and its caveat.
- RTX 4080 (driver 617.14): five `UINT64` statistics (Register Count, Binary Size, Stack Size, Local Memory Size, Shared
  Memory Size). Local Memory Size always has bit 36 set; the low 32 bits are the byte count.
- `TestExecutableStatistics.cpp`: 5 cases, 42/42 `rhi_system_tests` on both presets, zero validation messages.
- A planted bug (no capture flag) passes every case. Only validation catches it
  (`VUID-vkGetPipelineExecutableStatisticsKHR-pipeline-03274`), because the driver returns statistics anyway.
- The dead function-pointer copy in `tests/integration_tests/fixtures/CommonCreationFunctions.cpp` is deleted (author
  approved). TriangleTest builds and runs 120 frames clean on both presets. Phases 2 and 3 are in
  `docs/executable-statistics-plan.md`.
- Warning pass (clang-cl): deleted the unused `strdup` macro (`CommonCreationFunctions.cpp`), `ApiVersionFromUint32`
  (`RhiSystem.cpp`), the `ImageFormatFromString` stub (`PlatformSystem.cpp`), and `k_AdobeRgb` plus the monitor GUID
  with its `SetupApi.h` include and `setupapi.lib` pragma (`PlatformDisplayInfo.cpp`). `Device`'s initializer list now
  follows member order. `cmake/ConfigureLodestone.cmake` removes `-fno-omit-frame-pointer` from `lodestone` and
  `lodestone_client_library` under clang-cl (Lodestone request E4). `MAKE_ENUM_CLASS_FLAGS` is left for the author.
  All affected tests pass on both presets with zero validation messages.

## 10. RhiResources: questions for the author, and inputs for the design session

### Questions (the port made a choice; each one is easy to reverse)

1. `CreateBuffer` and `CreateImage` lost their separate `userData` parameter, because `BufferCreateInfo::UserData`
   and `ImageCreateInfo::UserData` carry the same thing. Keep it that way?
2. Initial data is still pointer plus count, as in your API. The house style asks for `std::span`. Change it?
3. Every buffer and image gets `TRANSFER_SRC | TRANSFER_DST` usage, so that Set/Fill/Copy work on any resource.
   Before, `TRANSFER_DST` was added only with initial data, so `FillBuffer` on a buffer created empty always failed.
   Keep "always", or add a usage bit for it?
4. `DeviceAddressBuffer` and the raytracing usages are refused (the reply fails) until the device address design
   exists. VMA would also need `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT`, and RhiSystem has no feature query.
5. `ResourceBarriers` now takes `rhi::CommandBufferHandle` and spans, and `ImageBarrierInfo` gained an `ImageRange`
   (a layout transition is invalid without an aspect mask). `ExecuteImageBarriers` and `ExecuteBarriers` were
   declared but never defined; now they batch into one `vkCmdPipelineBarrier2`.
6. `ImageLayoutFromUsageBits` changed order: read-only layout only when no attachment write is set; storage (UAV)
   before sampled, so an image that is both stays in GENERAL; `SampledImage` maps to read-only.

### Answers (author, 2026-10-20), and what changed

1. The `userData` parameter stays gone.
2. Initial data is a `std::span` of regions, and each region's `Data` is a `std::span<const std::byte>` (build it with
   `std::as_bytes`). Done.
3. Transfer usage is opt-in: `CopySource`/`CopyDestination` on buffers, `TransferSource` (new) and
   `TransferDestination` on images. The context adds the destination bit only when it stages data itself (initial
   data on a device-local resource, the target of `CopyBuffer`/`CopyImage`). Set, Fill and Copy fail the reply when
   the bit is missing. Done, with tests. `ImageLayoutFromUsageBits` now ranks the transfer bits last, so an
   uploaded texture rests in its shader layout.
4. The device address refusal stays until there is implementation progress.
5. and 6. Accepted as they are.
7. Depth range: the author picked [-1,1]. Claude's objection, recorded so it is not lost: on Vulkan, [-1,1] needs
   `VK_EXT_depth_clip_control`, and it gives less float precision than [0,1], not more (half of the float range maps
   near the middle of the depth range). The precision-best layout is reversed-Z with [0,1] and a float depth buffer.
   Revisit before the planet renderer depends on it.
### Inputs for the transfer, streaming, device address and bindless design

Facts that the port found, for the session that writes the design note:

- **Cross-queue visibility.** The transfer queue signals the reply's timeline semaphore (value 2), and a consumer
  must wait on it in its own submit. ComputeManifestTest shows the pattern. Nothing makes a host-side
  `WaitForCompletion` enough on its own for another queue, strictly speaking.
- **One `VkSemaphore` per reply**, created on the caller's thread, and one command pool plus one staging buffer per
  transfer. Fine for tests; the obvious cost centre for streaming. One timeline per transfer queue with increasing
  values would remove the per-reply semaphore.
- **Concurrent sharing.** Every buffer and image is `VK_SHARING_MODE_CONCURRENT` across the unique graphics, compute
  and transfer families, as `ResourceBarriers.hpp` assumed. This removes ownership transfers, but some drivers
  disable image compression for concurrent images. Worth measuring before streaming textures.
- **Queue choice.** A dedicated transfer family when it exists (it does here: 2 queues). Otherwise graphics queue 1,
  and queue 0 as the last resort, which races the application's submits (`todo-ship` in `TransferSystem.cpp`).
- **Layout tracking** follows only the transfer system (`TrackedImageLayout`). Barriers that the user records
  through `ResourceBarriers` are invisible to it, so a Set after a user transition starts from a stale layout.
- **Copies issued by the context cannot wait on user work.** `CopyBufferContents` after a dispatch has no way to
  wait for that dispatch's semaphore. The tests copy only what the transfer queue itself wrote.
- **No image readback.** `CopyImageToBuffer` is still "not implemented", so image uploads are checked for validity,
  not for content.
- **Two VMA allocators** (context thread and transfer thread), each externally synchronized.
