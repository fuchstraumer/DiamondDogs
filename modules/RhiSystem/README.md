# RhiSystem

RhiSystem is the Vulkan layer of DiamondDogs. It owns the instance, the device, the extension tables, command pools, fences, semaphores, shader objects, and `ShaderLibrary`.

## The start path: a device from its needs

`RhiSystem` has two ways to start. The old constructor (`RhiSystem(const RhiSystemCreateInfo&)`) enables every
feature that the device supports. The start path enables only what it is asked for. The boot stages use the start
path; the old constructor goes away when every executable moves to the stages.

```cpp
rhi::RhiSystem system;
const rhi::RhiSystemResult instance = system.StartInstance(instanceInfo);
const rhi::RhiSystemResult device = system.StartDevice(deviceInfo);
```

Nothing throws. `RhiSystemResult` has a code, the need that failed, and the reason with the device name.

### The needs

`DeviceStartInfo` (`RhiSystemStart.hpp`) has four lists, the required limits and the present surface. Each need is
`Required` or `Wanted`.

| List | Names | Example |
|---|---|---|
| `Extensions` | device extension names | `VK_KHR_swapchain` |
| `Features` | API features, as `Struct.member` | `VkPhysicalDeviceVulkan13Features.synchronization2` |
| `Capabilities` | SPIR-V capability and extension names, Lodestone's vocabulary | `GroupNonUniformArithmetic` |
| `Declined` | Wanted needs of any kind to leave off, to test the fallback paths | `Int64`, `VK_EXT_memory_budget` |

`RequiredLimits` is the smallest compute and push constant limit set that every entry point can run at (bootstrap's
`DeviceRequirements::RequiredLimits`). `PresentSurface` is the window's `VkSurfaceKHR` as an integer, or 0 for a
headless device.

Features are the needs of spine code (sync2, timeline semaphores, dynamic rendering, shader objects). Capabilities
come from the shader bundles, through bootstrap's `GatherDeviceRequirements`.

### How StartDevice chooses

`PlanDevice` (`src/core/DevicePlanner`) runs once per physical device. It reads the device and creates nothing:

1. The device's API version must be at least the instance's.
2. With a `PresentSurface`, the first queue family with graphics (the family that `Device` uses) must present to it.
   Each limit must be at least `RequiredLimits`. A failure names the limit by its Vulkan name, or `present`.
3. A Wanted need in `Declined` is skipped. A Required need ignores `Declined`.
4. Each extension must be supported, or the device fails a Required need.
5. Each feature: the struct must be available (a version the device has, or a supported extension), and the member
   must be supported. A member of an extension struct also enables its extension.
6. Each capability: Lodestone's `VulkanCapabilityTable.hpp` lists its enables, and any one is enough. The planner
   tries them in this order: a Vulkan version, a feature of a core struct, a feature of an extension struct, an
   extension, a property. A property (a subgroup operation bit, for example) enables nothing; it only tests.

`StartDevice` takes the best-scored plan (discrete first, then device-local memory) that meets every Required need.
When none does, it names the first unmet need of the best-scored device.

**Promoted structs fold into the core struct.** `VkPhysicalDeviceTimelineSemaphoreFeatures.timelineSemaphore` is set
in `VkPhysicalDeviceVulkan12Features`, because device creation must not chain a promoted struct beside the
`VkPhysicalDeviceVulkanNNFeatures` that absorbed it. Callers can name either struct.

**The enabled chain starts zeroed.** `FeatureChain` (`src/core/FeatureChain`) holds one heap block per struct, so a
move keeps the pNext pointers valid. It sets and reads one member by its row in the generated table.

### The supports function

`SupportsCapability` and `SupportsCapabilityFn` (the shape of `lodestone::manifest::CapabilitySupportFn`) answer from
the **enabled** set, never from the physical device. A variant filter that asked the physical device could choose a
variant that needs a capability the device was created without. On the RTX 4080, `Float64` is supported, and it
answers false unless a need asked for it. `Limits()` gives the compute and push constant limits for the same filter.

### The generated feature table

`GenerateExtensionAndFeatureHeaders.py` also writes `GeneratedFeatureTable.hpp`: every `VkBool32` member of every
feature struct, and every `VkBool32` or flags member of every property struct, with the sType, the struct size and the
member offset (`offsetof`). It also writes the struct aliases (the `...KHR` names) and the flag bits of the flags
members. Only the structs that `vulkan_core.h` defines go in, without the `VK_ENABLE_BETA_EXTENSIONS` blocks, so the
table always compiles against the SDK headers. Lodestone's capability table can be newer than our headers; a name it
uses that our headers lack counts as unsupported.

## Shader executable statistics

`rhi::CaptureComputeStatistics` asks the driver for compile statistics of a compute shader. The declaration is in `shaders/ExecutableStatistics.hpp`.

### What the function does

1. It takes the same `ShaderBinaryOptions` that the shader object path uses.
2. It compiles them again as a temporary compute pipeline, with `VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR`.
3. It queries `VK_KHR_pipeline_executable_properties` for each executable of the pipeline.
4. It destroys the pipeline, the pipeline layout, and the shader module.

The result is a vector of `ShaderExecutable`. Each one has a name, the stages, the subgroup size, and a list of `ExecutableStatistic`. The variant index of a statistic value is the format that the driver reported.

### Why a second compile

The extension queries a `VkPipeline` only. Our shaders are shader objects (`VkShaderEXT`), and a shader object has no statistics query. So the function compiles a pipeline from the same inputs, and reads the statistics from that.

### Caveat

The numbers describe the pipeline compile. They do not describe the shader object that we bind. The inputs are the same, so for compute the numbers should be close. Nobody has proven that they are identical. A driver can compile a shader object with a different path.

### Compute only

Graphics is not supported. A graphics pipeline needs vertex input, rasterization, blend, and render target formats. The shader object path sets these as dynamic state at draw time, so the probe would have to guess them. Linked pipeline stages also compile differently from separate shader objects.

### What the RTX 4080 reports

The driver returns one executable per compute pipeline, named `CS`, with subgroup size 32. It reports five statistics, all in `UINT64` format:

- `Register Count`
- `Binary Size`
- `Stack Size`
- `Local Memory Size`
- `Shared Memory Size`

`Local Memory Size` has bit 36 set in every value. The low 32 bits are the byte count. The function reports the raw value. A consumer that masks the value must say so.

The statistic names are specific to each vendor. Do not match names on other vendors without a check.

### Requirement

The device must enable `VK_KHR_pipeline_executable_properties`. If it does not, the function returns `ExtensionNotEnabled`.
