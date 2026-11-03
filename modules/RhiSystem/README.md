# RhiSystem

RhiSystem is the Vulkan layer of DiamondDogs. It owns the instance, the device, the extension tables, command pools, fences, semaphores, shader objects, and `ShaderLibrary`.

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
