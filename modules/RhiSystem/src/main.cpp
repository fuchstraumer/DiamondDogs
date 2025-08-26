#include "ExtensionWrangler.hpp"
#include <vulkan/vulkan.h>
#include <array>
#include <vector>
#include <stdexcept>

constexpr bool useExtensionWrangler = false;

int main(int argc, char** argv)
{
    const char* requiredDeviceRtExtensions[]
    {
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME
    };

    // you have to enumerate extensions based on the physical device, even though we are creating the logical (software)
    // device. a split that makes sense but is yet another fiddly little detail about vulkan lol
    VkPhysicalDevice physicalDevice;

    if constexpr (!useExtensionWrangler)
    {

        uint32_t numDeviceExtensions = 0;
        std::vector<VkExtensionProperties> deviceExtensions;
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &numDeviceExtensions, nullptr);
        deviceExtensions.resize(numDeviceExtensions);
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &numDeviceExtensions, deviceExtensions.data());

        // Check if all required RT extensions are available
        bool allRtExtensionsAvailable = true;
        for (const char* requiredExt : requiredDeviceRtExtensions)
        {
            bool found = false;
            for (const auto& ext : deviceExtensions)
            {
                // yes we have to strcmp because everything is cstrings. yay!!!!!!!
                if (strcmp(requiredExt, ext.extensionName) == 0)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                allRtExtensionsAvailable = false;
                break;
            }
        }

        // Now get all the features for the extensions, starting with the base features struct and setting the pNext
        // pointer sequentially as appropriate
        VkPhysicalDeviceFeatures2 deviceFeatures2 = {};
        deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures = {};
        accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        deviceFeatures2.pNext = &accelerationStructureFeatures;
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures = {};
        rayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        accelerationStructureFeatures.pNext = &rayTracingPipelineFeatures;
        VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = {};
        rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        rayTracingPipelineFeatures.pNext = &rayQueryFeatures;
        VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2Features = {};
        synchronization2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
        rayQueryFeatures.pNext = &synchronization2Features;

        vkGetPhysicalDeviceFeatures2(physicalDevice, &deviceFeatures2);
        
        VkDeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        // set pNext to the features structs, as that will enable all the features supported on current hardware
        deviceCreateInfo.pNext = &deviceFeatures2;
        deviceCreateInfo.enabledExtensionCount = std::size(requiredDeviceRtExtensions);
        deviceCreateInfo.ppEnabledExtensionNames = requiredDeviceRtExtensions;

        VkDevice device;
        VkResult result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device);
    }
    else
    {
        // use the extension wrangler. assume we enter here with a physical device already selected
        ExtensionWrangler extensionWrangler(VK_API_VERSION_1_4, physicalDevice);
        bool allRtExtensionsSupported = extensionWrangler.AreExtensionsSupported(
            std::size(requiredDeviceRtExtensions),
            requiredDeviceRtExtensions);

        if (!allRtExtensionsSupported)
        {
            throw std::runtime_error("Required RT extensions not supported");
        }

        VkPhysicalDeviceFeatures2 deviceFeatures2 = extensionWrangler.GetExtensionFeatures(
            std::size(requiredDeviceRtExtensions),
            requiredDeviceRtExtensions,
            ExtensionWrangler::GetVersionFeatures::True);

        VkDeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.pNext = &deviceFeatures2;
        deviceCreateInfo.enabledExtensionCount = std::size(requiredDeviceRtExtensions);
        deviceCreateInfo.ppEnabledExtensionNames = requiredDeviceRtExtensions;

        VkDevice device;
        VkResult result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device);
    }

    return 0;
}
