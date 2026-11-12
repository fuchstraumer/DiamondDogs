# Bootstrap

`bootstrap` builds an executable's systems in a fixed order of typed stages, and starts the optional modules that a
profile selects. It replaces the hand-written start-up code in each `main`. This target needs no RHI: a cooker
console links it alone. A later target, `bootstrap_vulkan`, adds the window, device and GPU stages.

## The stages

```
LaunchStage ──> CoreStage ──> ContentStage ──> WindowStage? ──> DeviceRequirements ──> DeviceStage ──> GpuStage
```

`LaunchStage`, `CoreStage`, `ContentStage` and the `DeviceRequirements` function exist today. Each stage:

- is constructed empty, and `Start` returns a `BootResult`, because a constructor cannot fail without an exception;
- takes the earlier stages as `const&` in `Start`, so the compiler enforces the order;
- cannot be copied or moved, because later stages and modules store references into it;
- destroys what `Start` made in `Stop`, in reverse order, and the destructor calls `Stop`.

`main` declares the stages as locals, so C++ scope gives the reverse destruction. `Stop` and then `Start` again is the
recovery path.

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
every entry point of every open bundle.

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

`BootResult` follows `agent::ToolResult`: a code, the stage, the module, the subject and the source of the failure,
and suggestions. A step fills the fields and never formats text. `FormatFailure` is the sink:

```
boot failed in Launch: UnknownModule
  module:       beta
  subject:      alpah
  source:       DependsOn
  did you mean: alpha
```
