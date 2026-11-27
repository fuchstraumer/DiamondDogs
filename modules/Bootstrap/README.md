# Bootstrap

`bootstrap` builds an executable's systems in a fixed order of typed stages, and starts the optional modules that a
profile selects. It replaces the hand-written start-up code in each `main`. This target needs no RHI: a cooker
console links it alone. A second target, `bootstrap_vulkan` (`modules/BootstrapVulkan`), adds the window, device and
GPU stages. This file explains both.

## The stages

```
LaunchStage ──> CoreStage ──> ContentStage ──> WindowStage? ──> DeviceRequirements ──> DeviceStage ──> GpuStage
```

`bootstrap` has `LaunchStage`, `CoreStage`, `ContentStage` and the `DeviceRequirements` function. `bootstrap_vulkan`
has `WindowStage`, `DeviceStage` and `GpuStage`. Each stage:

- is constructed empty, and `Start` returns a `BootResult`, because a constructor cannot fail without an exception;
- takes the earlier stages as `const&` in `Start`, so the compiler enforces the order;
- cannot be copied or moved, because later stages and modules store references into it;
- destroys what `Start` made in `Stop`, in reverse order, and the destructor calls `Stop`.

`main` declares the stages as locals, so C++ scope gives the reverse destruction. `Stop` and then `Start` again is the
recovery path.

### The spine

The spine is the fixed chain of systems that every executable gets, whatever modules it selects: the scheduler, the
bundles, the window, the device, `ResourceContext` and the swapchain. Each stage owns its part of the spine as a typed
member with a typed getter (`CoreStage::Scheduler()`, `GpuStage::Resources()`), so the order is plain in the code and
nothing looks a spine part up by name. A module is the opposite: optional, selected by a profile, and found with
`Require`. The spine stays explicit on purpose: UE makes even its renderer a plugin, and that is one reason its start
order is hard to read.

The spine's own code needs device features too. These are the **spine needs**, the `spine_needs` argument of
`GatherDeviceRequirements`. They are named beside the code that uses them, not in each app:

- `GpuSpineNeeds()`: `ResourceContext` needs timeline semaphores and synchronization2 (Required), and
  `VK_EXT_memory_budget` (Wanted, for VMA's budget data).
- `WindowStage::Needs()`: the surface instance extensions from GLFW, `VK_KHR_get_surface_capabilities2` and
  `VK_KHR_swapchain` (Required), and the HDR extensions `VK_EXT_swapchain_colorspace` and `VK_EXT_hdr_metadata`
  (Wanted).

`SpineNeeds(window)` and `HeadlessSpineNeeds()` return the lists. An app appends its own needs (dynamic rendering,
shader objects) and passes the result to `GatherDeviceRequirements`. So a feature that you did not expect comes from
spine or app code, and a capability that you did not expect comes from a shader.

### LaunchStage

`LaunchStage::Start(arguments, linked_modules, last_stage)` reads every argument once, before any other system
exists. It knows three flags:

| Flag | Effect |
|---|---|
| `--config=PATH` | Reads the JSON config file. The file must be a JSON object. Each stage parses its own section from `Config().Text`. |
| `--profile=NAME` | Selects the modules that `Profiles.NAME` lists in the config file. Without it, every linked module starts. |
| `--boot-timings` | Asks the executable to print the boot timings table. |

`Arguments()` returns every argument unchanged, so a module reads its own flags (`--tool-port`).

A profile is a list of module names:

```json
{
    "Profiles":
    {
        "demo": [],
        "debug": [ "agent_tools", "imgui", "frame_capture" ]
    }
}
```

The executable states the linked modules and their order: a constant array of `ModuleDescriptor`. There is no
self-registration through static constructors. A profile is a set: the selected modules start in the linked order.

`Start` refuses, with a named code:

- a descriptor with no name, or with neither or both start functions (`InvalidDescriptor`);
- a name that repeats in the linked list or in a profile (`DuplicateModule`);
- a profile or module name that is not known (`UnknownProfile`, `UnknownModule`, with the nearest names as suggestions);
- a dependency that the profile does not select (`DependencyNotSelected`). The dependency is not added in silence;
- a dependency that is later in the linked list, or that starts in a later stage (`DependencyNotEarlier`);
- a module that starts after the executable's last stage, for example a GPU module in the cooker
  (`ModuleAfterLastStage`).

### CoreStage

`CoreStage::Start(launch)` constructs the `foundation::Scheduler` on the calling thread, which then owns it. Then it
calls `StartCore` of each selected module, in list order. When a module fails, the stage stops what it started and
returns the failure, with the stage and the module name filled in.

### ContentStage

`ContentStage::Start(core, app_bundles)` opens the content that needs no device: today, the shader bundles. It opens
the executable's own list first, then the bundles in each selected module's `Needs.Bundles`, in list order. A
`BundleRequest` names:

- `FileName`: the name of the copy beside the executable (`OceanFft.ldmanifest`). `FindBundle` matches it;
- `BuildTreePath`: the cooked file, for a run from the build tree;
- `Profile`: the Lodestone profile to open (`rhi::k_SpirvProfileName` on Vulkan).

Every Lodestone cook writes `ShaderLibrary.ldmanifest`, so the file name is the app's choice and never comes from the
cook. A request that repeats an earlier one (same file name, profile and build-tree path) opens nothing. `Start`
refuses:

- a file name and profile that an earlier request gave with another build-tree path (`BundleNameConflict`). A copied
  request that keeps the old name would otherwise drop a bundle in silence;
- a file that is neither beside the executable nor at the build-tree path (`BundleNotFound`);
- a file that does not open for the profile, or a module with no code facts (`BundleInvalid`). Without code facts a
  bundle cannot say what the device needs.

`Bundles()` returns `LoadedBundle` records (`LoadedBundle.hpp`): the request fields, the module that asked, the path
that was opened, and the `rhi::ShaderBundle`. `ContentStage.hpp` only forward-declares the record, so it stays light.

### DeviceRequirements

`GatherDeviceRequirements(content, spine_needs)` is a pure function. It reads no device. It returns the union of three
sources, in this order: the spine needs (each one sets its `NeedKind`), the `ModuleNeeds` of each selected module, and
every entry point of every open bundle. The needs go in five lists by kind: instance extensions, device extensions,
device features (`Struct.member`, for example `VkPhysicalDeviceVulkan13Features.synchronization2`), shader
capabilities, and neutral capabilities.

For one entry point, the rule comes from the capability masks in the manifest:

- a capability that **every** variant needs is `Required`: no variant runs without it;
- a capability that **only some** variants need is `Wanted`: a capability axis made the difference, and Lodestone's
  axis check proved that the off path needs a subset of the on path.

The test bundle shows the rule. `BootNeedsWave` has a capability axis, so the wave variant needs
`GroupNonUniformArithmetic` and the other does not: `Wanted`. Both variants do 64-bit math: `Int64` is `Required`.

One name keeps one entry, per kind. `Required` wins over `Wanted`, and the entry keeps the source of the first need at
its level. A failure then names one module, bundle, shader module and entry point, which is enough to act on.

Lodestone lists only the capabilities that the code declares beyond the base set. `Shader` is not listed, so a bundle
of plain compute code (HashFill) adds limits and no capabilities.

Two sets of limits:

- `RequiredLimits`: for each entry point, the smallest value of each limit over its variants, then the largest over
  the entry points. A device below one of these cannot run any variant of some entry point. This is a necessary check,
  not a sufficient one, because each field takes its own minimum.
- `WantedLimits`: the largest value over every entry point instance. A device at these limits runs every variant.
  WebGPU requests these at `requestDevice`.

Push constant bytes count only for an entry point whose constants are push constants. The workgroup size and the
invocations count only for compute.

**Entry points of a fallback path.** A path list (Loom) has one body per device tier, and chooses one when the device
exists. The rule above makes a capability of every variant `Required`, so an entry point that only the higher tier
runs would stop a device without it from booting, before Loom can choose the lower path. The overload
`GatherDeviceRequirements(content, extra_needs, wanted_only)` takes those entry points as `EntryPointRef` (the bundle's
request file name, the shader module and the entry point, as `NeedSource` spells them). Their capabilities are
`Wanted`, and their limits raise `WantedLimits` only. A `Required` need from another entry point or from `extra_needs`
stays `Required`: the caller lists the last path's needs as `Required` there. The two-argument function is the same
call with no entry point listed.

### Neutral capabilities

`DeviceCapability` (`DeviceCapability.hpp`) names a capability in no API's vocabulary: `DrawIndirectCount` today, and
mesh shaders, ray query and others as Loom's tiers arrive. The header includes no graphics API and no other Bootstrap
header, so Loom's core reads it and still links no RHI.

- A need carries one as `NeedKind::NeutralCapability`, with `Need::Neutral` set and `Name` set to its neutral name.
  `NeutralNeed(capability, level)` builds one. `ModuleNeeds` has `RequiredNeutralCapabilities` and
  `WantedNeutralCapabilities`.
- Each device stage maps the value to its own API's needs, at the same level, through its own table: Vulkan
  `VkPhysicalDeviceVulkan12Features.drawIndirectCount`, WebGPU `MultiDrawIndirect` (Chrome offers it only with the
  unsafe-WebGPU flag today). A table entry is a list, because one capability can need an extension and several
  features. The device has the capability only when it has every entry.
- `Declined` takes the neutral name, and the stage then declines every API name that it maps to. One switch tests a
  fallback path on both backends.
- A failure names the neutral capability: `Subject` is the neutral name, `Source` is where the need came from, and
  `Detail` names the API need that the device lacks.
- `DeviceFilter::Capabilities` lists the neutral capabilities that the device was created with, sorted.
  `HasCapability(filter, capability)` answers from it, so a path choice never asks the device again.

No neutral name may also be a shader capability name, because `Declined` matches names of every kind.
`bootstrap_vulkan_tests` checks the names against Lodestone's Vulkan capability table.

### DeviceFilter

`DeviceFilter` (`DeviceFilter.hpp`) is what the device was created with, in the shape that a variant choice
(`ShaderLibrary`, Loom's Compile) takes: the supports function and its user pointer, the limits, and the neutral
capabilities. It holds no graphics API type, so every device stage returns the same type. It answers from the enabled
set, never from the physical device, so a choice cannot pick code that needs something the device was created without.
Its views point into the stage, so it must not outlive the stage's `Stop`.

## The device stages (`bootstrap_vulkan`)

```cpp
bootstrap::WindowStage window;
ReturnOnFailure(window.Start(content, windowInfo));

const std::vector<bootstrap::Need> spine = bootstrap::SpineNeeds(window);
const bootstrap::DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(content, spine);
bootstrap::DeviceConfig config;
ReturnOnFailure(bootstrap::ReadDeviceConfig(launch, config));

bootstrap::DeviceStage device;
ReturnOnFailure(device.Start(content, requirements, config, window));

bootstrap::GpuStage gpu;
ReturnOnFailure(gpu.Start(device, window));
```

`ReturnOnFailure` stands for the `if (!result.Succeeded())` block. A headless executable skips `WindowStage`, and
calls `HeadlessSpineNeeds()`, `DeviceStage::StartHeadless` and `GpuStage::StartHeadless`. The headless calls have
their own names, so a call never hides the headless path behind a null pointer.

**The order that presentation needs.** Device selection checks present support, which needs a surface. The surface
needs the instance and the window. So the chain is: window, instance, surface, physical device, device, swapchain.

- `WindowStage` creates the GLFW window, with no surface and no swapchain, and records the window's needs.
- `DeviceStage` creates the instance (`RhiSystem::StartInstance`), then the surface
  (`PlatformWindowSystem::CreateSurface`), then the device (`RhiSystem::StartDevice`). It passes the surface, so the
  planner refuses a device whose graphics queue family cannot present to it, and `RequiredLimits`, so the planner
  refuses a device below them. Each step records a timing span (`instance`, `surface`, `device`).
- `GpuStage` creates `ResourceContext`, then the swapchain, then starts each selected module that has `StartGpu`.

**Stop order.** `GpuStage::Stop` destroys its modules, the swapchain, then `ResourceContext`. `DeviceStage::Stop`
destroys the surface, then the device and the instance, because the surface belongs to the instance. Locals in `main`
stop in that order without help.

**`DeviceConfig`.** `ReadDeviceConfig(launch, config)` reads the config file's `Device` section. A key that the
section does not name keeps the value in `config`. An unknown key fails with `ConfigInvalid`, so a typo does not
pass in silence.

```json
{
    "Device":
    {
        "ApiVersion": "1.3",
        "Validation": "Full",
        "ApplicationName": "OceanFft",
        "Declined": [ "VK_EXT_memory_budget", "GroupNonUniformArithmetic" ]
    }
}
```

`Declined` takes Wanted needs of every kind: extensions, features, capabilities and neutral names. The device is then created
without them, so the fallback paths run on a device that has them, for example on the RTX 4080. A declined name has no
effect on a Required need. It also does not stop an enable that another need pulls in: a capability whose enable is
the declined extension still enables it.

**`DeviceFilter`.** `DeviceStage::Filter()` returns the `DeviceFilter` that the section above describes.

**A failure names the need and where it came from.** `NoDeviceMeetsRequirements` sets `Subject` to the unmet need,
`Source` to where it came from (`bundle/module/entry point`, a module name, or `spine`) and `Detail` to the closest
device and the reason. For a limit or present support, `Subject` is the limit's Vulkan name or `present`, and
`Source` is empty.

`GpuStartContext` gives a GPU module every stage. `Core()` is `const`, as `ContentStage` holds it, so a GPU module does
not reach the Scheduler. `Require` finds a dependency among the GPU modules first, then among the core modules.

## The WebGPU device stage (`bootstrap_webgpu`)

`WebGpuDeviceStage` is built only in the Emscripten configuration. The browser answers the adapter and device
requests asynchronously, so the stage is a state machine: `Start` (or `StartHeadless`) requests the adapter, and
`Step`, called each frame, moves it on until `IsReady` or `HasFailed`. It takes the same `DeviceRequirements` and
`DeviceConfig` as the Vulkan stage, and applies the same rules:

- A Required need that the adapter lacks fails the boot. A Wanted need is requested when the adapter has it. A
  declined Wanted need is left off.
- The kinds map as follows. A shader capability is a WGSL `enable` name (`f16` needs `shader-f16`). A device feature
  is a WebGPU feature name in the JavaScript spelling (`timestamp-query`). A neutral capability maps through the
  stage's table. WebGPU has no extensions, so a Required Vulkan extension fails the boot, and a Wanted one is skipped.
  A Required name that WebGPU has no feature for also fails.
- **Limits.** The device gets the adapter's own limits, not WebGPU's defaults. On the RTX 4080 that is 32768 bytes of
  workgroup storage and 1024 invocations, where the defaults are 16384 and 256. Loom's Compile and the variant filter
  then check against what the hardware has. The boot fails when `RequiredLimits` is above the adapter. WGSL has no
  push constants (Lodestone delivers them in a uniform buffer), so `MaxPushConstantBytes` is not checked.
- **The adapter.** `PowerPreference::Default` asks for the high-performance adapter, because the limits come from
  the adapter. Chrome on Windows ignores the preference today (crbug.com/369219127), so there the system chooses the
  GPU.
- A failure is a `BootResult` (`Failure()`), with the same `Subject`, `Source` and `Detail` as the Vulkan stage.
  `AdapterRequestFailed` is the code when the browser gives no adapter.
- `Filter()` returns the `DeviceFilter`. Its supports function answers for the WGSL enable names whose features the
  device has. The stage keeps that set on the heap, so the filter stays valid when the stage moves.

`WebGpuBootTest` (`tests/integration_tests/WebGpuBootTest`) proves the rules in the browser. Serve
`build/emscripten/tests/integration_tests` on 127.0.0.1, open the page, and read `window.__ddTestPassed`.

## Modules

A module is an optional part that a profile selects: the tool server, ImGui, frame capture, editor panels. The spine
(the scheduler, the bundles, the window, the device, `ResourceContext`, the swapchain) is not a module. Each stage
owns its part of the spine as a typed member.

A module defines its descriptor and its key beside its own code (`include/<Module>Boot.hpp`):

```cpp
const bootstrap::ModuleKey<ToolServer> k_ToolServerKey{ "agent_tools" };

const bootstrap::ModuleDescriptor k_ToolServerModule
{
    .Name = "agent_tools",
    .StartCore = &StartToolServer,
};
```

- The slot sets the stage. `StartCore` receives a `CoreStartContext`, which exposes the launch and core stages and
  nothing later, so a core module cannot reach the device. `StartGpu` receives a `GpuStartContext`, which the linked
  backend defines.
- The start function hands its object to the stage with `instance.Own(key, std::move(object))`. The key name must be
  the descriptor name, and a module must own an object (`ModuleInstanceInvalid`).
- A module finds another module with `context.Require(key, result)`. `Require` refuses a key that the starting module
  did not list in `DependsOn` (`UndeclaredDependency`), so `DependsOn` always lists the real dependencies. The
  contexts exist only while `Start` runs. After boot, a system keeps the references that it was given.

This is a service locator, but only at the composition root. Nothing global points to a context or to a stage.

## Boot timings

Each stage `Start`, each module start and each opened bundle records one CPU span into `LaunchStage::Timings()`. Later stages take the
launch stage as `const`, and the recorder is `mutable`, because it is a diagnostic and not launch state. The spans go
in a fixed array of `k_MaxBootSpans` (64), so recording allocates nothing. A span past the end is counted as dropped.

`BOOTSTRAP_TIMINGS_ENABLED` is 1 in Debug and RelWithDebInfo, and 0 in Release and MinSizeRel, the same split as
`RHI_GPU_TIMERS_ENABLED`. With 0, the recorder has empty inline bodies, and call sites need no `#if`.

`FormatBootTimings(timings, header_detail)` returns the table. Pass the validation level as the header detail, because
validation inflates instance and device creation:

```
boot timings (Debug, no device)
  stage     step                              ms
  Launch                                     0.5
  Core                                       0.5
  total                                      1.0
```

## The no-RHI check

`bootstrap_assert_no_rhi(target)` walks the link graph of a target at configure time, and fails the configure when
the graph reaches `rhi_system` or the Vulkan loader. `ContentCooker` and `bootstrap_tests` call it. A planted link to
`color_pipe` (which reaches `rhi_system` through `rhi_resources`) failed the configure as it should.

## Results

`BootResult` follows `agent::ToolResult`: a code, the stage, the module, the subject, the source and the detail of the
failure, and suggestions. A step fills the fields and never formats text. `FormatFailure` is the sink:

```
boot failed in Launch: UnknownModule
  module:       beta
  subject:      alpah
  source:       DependsOn
  did you mean: alpha
```
