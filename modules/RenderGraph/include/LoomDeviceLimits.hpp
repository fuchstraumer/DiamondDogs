#pragma once
#ifndef DIAMOND_DOGS_LOOM_DEVICE_LIMITS_HPP
#define DIAMOND_DOGS_LOOM_DEVICE_LIMITS_HPP
#include <cstdint>
#include <string_view>

// The device facts that Compile checks, in Loom's own terms: the core links no RHI, and Lodestone's ShaderManifest
// headers stay out of Loom's public headers (G1). Each backend fills them from its device.
namespace loom
{

    // Answers whether the device has a capability enabled. Answer from the enabled set, not from what the physical
    // device supports: a variant that needs a capability the device left off fails validation with no named error.
    using CapabilitySupportFn = bool (*)(void* user, std::string_view capability_name);

    // The limits that Resolve checks for each shader (LM207). Fill every field: a limit of 0 rejects each instance
    // that has a size to compare.
    struct ShaderDeviceLimits
    {
        uint32_t MaxWorkgroupMemoryBytes{ 0u };
        uint32_t MaxInvocationsPerWorkgroup{ 0u };
        uint32_t MaxWorkgroupSizeX{ 0u };
        uint32_t MaxWorkgroupSizeY{ 0u };
        uint32_t MaxWorkgroupSizeZ{ 0u };
        uint32_t MaxPushConstantBytes{ 0u };
    };

    // The device limits that Layout checks, in VkPhysicalDeviceLimits terms. Resolve checks the workgroup size, the
    // invocations, the workgroup memory and the push constant bytes (LM207), so they are not here. Fill every field:
    // a limit of 0 rejects each pass that uses that kind of thing.
    struct LayoutLimits
    {
        uint32_t MaxComputeWorkGroupCountX{ 0u };
        uint32_t MaxComputeWorkGroupCountY{ 0u };
        uint32_t MaxComputeWorkGroupCountZ{ 0u };
        uint32_t MaxColorAttachments{ 0u };
        uint32_t MaxBoundDescriptorSets{ 0u };
        uint32_t MaxPerStageDescriptorSamplers{ 0u };
        uint32_t MaxPerStageDescriptorUniformBuffers{ 0u };
        uint32_t MaxPerStageDescriptorStorageBuffers{ 0u };
        uint32_t MaxPerStageDescriptorSampledImages{ 0u };
        uint32_t MaxPerStageDescriptorStorageImages{ 0u };
        uint32_t MaxPerStageResources{ 0u };
        uint32_t MaxUniformBufferRange{ 0u };
        uint32_t MaxStorageBufferRange{ 0u };
        uint32_t MaxImageDimension1D{ 0u };
        uint32_t MaxImageDimension2D{ 0u };
        uint32_t MaxImageDimension3D{ 0u };
    };

}

#endif // !DIAMOND_DOGS_LOOM_DEVICE_LIMITS_HPP
