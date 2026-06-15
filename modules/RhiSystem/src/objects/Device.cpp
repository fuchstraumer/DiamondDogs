#include "Device.hpp"
#include "Instance.hpp"
#include "ExtensionPack.hpp"
#include "RhiResult.hpp"
#include "RhiAssert.hpp"
#include <stdexcept>
#include <vector>
#include <set>
#include <algorithm>
#include <format>
#include <iostream>
#include <vulkan/vulkan_core.h>

namespace rhi
{
#ifdef RHI_SYSTEM_USE_VULKAN

    // Exceedingly rare to have more than 1 queue per family in the actual hardware anyways, otherwise it's just multiplexing to the actual command processor.
    // Two queues at least gives us some flexibility for parallelism without going overboard.
    constexpr static uint32_t k_MaxQueuesPerFamily = 2u;
    static VkDevice LossHandlerDevice = VK_NULL_HANDLE;
    PFN_vkGetDeviceFaultReportsKHR pfn_vkGetDeviceFaultReportsKHR = nullptr;

    std::string DeviceFaultAddressTypeToStr(VkDeviceFaultAddressTypeKHR type)
    {
        switch (type)
        {
        case VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_KHR:
            return "None";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_KHR:
            return "Read Invalid";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_KHR:
            return "Write Invalid";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_EXECUTE_INVALID_KHR:
            return "Execute Invalid";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_UNKNOWN_KHR:
            return "Instruction Pointer Unknown";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_INVALID_KHR:
            return "Instruction Pointer Invalid";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_KHR:
            return "Instruction Pointer Fault";
        default:
            return "Unknown VkDeviceFaultAddressTypeKHR value";
        }
    }

    std::string DeviceFaultFlagBitsToStr(VkDeviceFaultFlagBitsKHR bits)
    {
        if (bits == 0)
        {
            return "None";
        }

        std::string result;

        auto appendFault = [&](const char* name)
        {
            result += name;
            if (!result.empty())
            {
                result += " | ";
            }
        };

        if (bits & VK_DEVICE_FAULT_FLAG_DEVICE_LOST_KHR) 
        {
            appendFault("DeviceLost");
        }

        if (bits & VK_DEVICE_FAULT_FLAG_MEMORY_ADDRESS_KHR)
        {
            appendFault("MemoryAddress");
        }

        if (bits & VK_DEVICE_FAULT_FLAG_INSTRUCTION_ADDRESS_KHR)
        {
            appendFault("InstructionAddress");
        }

        if (bits & VK_DEVICE_FAULT_FLAG_VENDOR_KHR)
        {
            appendFault("VendorSpecificFault");
        }

        if (bits & VK_DEVICE_FAULT_FLAG_WATCHDOG_TIMEOUT_KHR)
        {
            appendFault("WatchdogTimeout");
        }

        if (bits & VK_DEVICE_FAULT_FLAG_OVERFLOW_KHR)
        {
            appendFault("Overflow");
        }

        // trim any trailing spaces and " | "
        if (result.ends_with(" | "))
        {
            result = result.substr(0, result.size() - 3);
        }

        return result.c_str();
    }

    std::string VkDeviceFaultAddressInfoToStr(const VkDeviceFaultAddressInfoKHR& addressInfo)
    {
        // what are the units of addressPrecision
        // power of two value specifying how precisely device can report the address. How to convey that?

        return std::format("Address: 0x{:016X}, AddressType: {}, AddressPrecision: {}",
            addressInfo.reportedAddress,
            DeviceFaultAddressTypeToStr(addressInfo.addressType),
            addressInfo.addressPrecision);
    }

    void DeviceLostHandler(const Result& deviceLossResult, const char* message)
    {

        std::cerr << std::format("Device Fault: {}\n", message);
        if (pfn_vkGetDeviceFaultReportsKHR)
        {
            uint32_t faultCount = 0;
            pfn_vkGetDeviceFaultReportsKHR(LossHandlerDevice, 0, &faultCount, nullptr);
            std::vector<VkDeviceFaultInfoKHR> faultInfos(faultCount, { VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_KHR, nullptr });
            pfn_vkGetDeviceFaultReportsKHR(LossHandlerDevice, 0, &faultCount, faultInfos.data());
            for (auto& faultInfo : faultInfos)
            {
                std::string faultFlagsStr = DeviceFaultFlagBitsToStr(static_cast<VkDeviceFaultFlagBitsKHR>(faultInfo.flags));
                uint64_t groupID = faultInfo.groupId;
                std::string_view faultDescription(faultInfo.description);
                std::string FaultAddressInfoStr = VkDeviceFaultAddressInfoToStr(faultInfo.faultAddressInfo);
                std::string FaultInstructionAddressInfoStr = VkDeviceFaultAddressInfoToStr(faultInfo.instructionAddressInfo);
                std::string_view vendorDescription(faultInfo.vendorInfo.description);
                // no idea whatr we do with these vendor fault data currently? Old format used to suggest we could dump things like aftermath
                // reports or nvdbg dumps or something
                uint64_t vendorFaultCode = faultInfo.vendorInfo.vendorFaultCode;
                uint64_t vendorFaultData = faultInfo.vendorInfo.vendorFaultData;

                std::cerr << std::format("Fault Flags: {}, Group ID: {}\n", faultFlagsStr, groupID);

                if (!faultDescription.empty())
                {
                    std::cerr << std::format("Description: {}\n", faultDescription);
                }

                if (faultInfo.faultAddressInfo.addressType != VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_KHR)
                {
                    std::cerr << std::format("FaultAddressInfo: {}\n", FaultAddressInfoStr);
                }

                if (faultInfo.instructionAddressInfo.addressType != VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_KHR)
                {
                    std::cerr << std::format("InstructionAddressInfo: {}\n", FaultInstructionAddressInfoStr);
                }

                if (!vendorDescription.empty() || vendorFaultCode != 0 || vendorFaultData != 0)
                {
                    std::cerr << "Vendor Fault Info:\n"; 
                    if (!vendorDescription.empty())
                    {
                        std::cerr << std::format("VendorDescription: {}\n", vendorDescription);
                    }
                    if (vendorFaultCode != 0)
                    {
                        std::cerr << std::format("VendorFaultCode: 0x{:016X}\n", vendorFaultCode);
                    }
                    if (vendorFaultData != 0)
                    {
                        std::cerr << std::format("VendorFaultData: 0x{:016X}\n", vendorFaultData);
                    }
                }
            }
        }
    }

    struct DeviceImpl
    {
        DeviceImpl(const Instance* instance) :
            parentInstance{ instance },
            physicalDevice{ VK_NULL_HANDLE },
            numGraphicsQueues{ 0u },
            numComputeQueues{ 0u },
            numTransferQueues{ 0u },
            numSparseBindingQueues{ 0u }
        {
            // perform this step first, so that we have the physical device handle for later steps and device creation
            selectBestPhysicalDevice();
            retrievePhysicalDeviceProperties();
            queryQueueProperties();
        }

        void selectBestPhysicalDevice()
        {
            uint32_t device_count = 0;
            vkEnumeratePhysicalDevices(parentInstance->Handle().As<VkInstance>(), &device_count, nullptr);
            
            if (device_count == 0)
            {
                throw std::runtime_error("No Vulkan-compatible physical devices found");
            }
            
            std::vector<VkPhysicalDevice> devices(device_count);
            vkEnumeratePhysicalDevices(parentInstance->Handle().As<VkInstance>(), &device_count, devices.data());
            
            // Simple scoring system - prefer discrete GPUs
            VkPhysicalDevice best_device = VK_NULL_HANDLE;
            int best_score = -1;
            
            for (VkPhysicalDevice device : devices)
            {
                VkPhysicalDeviceProperties device_props;
                vkGetPhysicalDeviceProperties(device, &device_props);

                int score = 0;
                
                // Prefer discrete GPUs
                if (device_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                {
                    score += 1000;
                }
                else if (device_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
                {
                    score += 100;
                }
                
                // Add points for more memory
                VkPhysicalDeviceMemoryProperties mem_props;
                vkGetPhysicalDeviceMemoryProperties(device, &mem_props);
                
                VkDeviceSize total_memory = 0;
                for (uint32_t i = 0; i < mem_props.memoryHeapCount; ++i)
                {
                    if (mem_props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                    {
                        total_memory += mem_props.memoryHeaps[i].size;
                    }
                }
                
                score += static_cast<int>(total_memory / (1024 * 1024 * 1024)); // GB of memory
                
                if (score > best_score)
                {
                    best_score = score;
                    best_device = device;
                }
            }
            
            if (best_device == VK_NULL_HANDLE)
            {
                throw std::runtime_error("No suitable physical device found");
            }
            
            physicalDevice = best_device;
        }

        void retrievePhysicalDeviceProperties()
        {
            properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            properties.pNext = nullptr;
            features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features.pNext = nullptr;

            vkGetPhysicalDeviceProperties2(physicalDevice, &properties);
            vkGetPhysicalDeviceFeatures2(physicalDevice, &features);
            vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
        }

        void queryQueueProperties()
        {
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
            queueFamilyProperties.resize(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());

            for (uint32_t i = 0; i < queueFamilyCount; ++i)
            {
                const VkQueueFamilyProperties& props = queueFamilyProperties[i];

                // Graphics queue (also supports compute and transfer)
                if (props.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    if (queueFamilyIndices.Graphics == ~0u)
                    {
                        queueFamilyIndices.Graphics = i;
                        numGraphicsQueues = props.queueCount;
                    }
                }

                // Dedicated compute queue (compute but not graphics)
                if ((props.queueFlags & VK_QUEUE_COMPUTE_BIT) && !(props.queueFlags & VK_QUEUE_GRAPHICS_BIT))
                {
                    if (queueFamilyIndices.Compute == ~0u)
                    {
                        queueFamilyIndices.Compute = i;
                        numComputeQueues = props.queueCount;
                    }
                }

                // Dedicated transfer queue (transfer but not graphics or compute)
                if ((props.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
                    !(props.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                    !(props.queueFlags & VK_QUEUE_COMPUTE_BIT))
                {
                    if (queueFamilyIndices.Transfer == ~0u)
                    {
                        queueFamilyIndices.Transfer = i;
                        numTransferQueues = props.queueCount;
                    }
                }

                // Sparse binding queue
                if (props.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT)
                {
                    if (queueFamilyIndices.SparseBinding == ~0u)
                    {
                        queueFamilyIndices.SparseBinding = i;
                        numSparseBindingQueues = props.queueCount;
                    }
                }
            }
        }

        void setupQueues(VkDevice handle)
        {
            
            // Setup graphics queues
            if (queueFamilyIndices.Graphics != VK_QUEUE_FAMILY_IGNORED)
            {
                numGraphicsQueues = std::min(queueFamilyProperties[queueFamilyIndices.Graphics].queueCount, k_MaxQueuesPerFamily);
                graphicsQueues.resize(numGraphicsQueues);
                
                for (uint32_t i = 0; i < numGraphicsQueues; ++i)
                {
                    VkQueue queue;
                    vkGetDeviceQueue(handle, queueFamilyIndices.Graphics, i, &queue);
                    graphicsQueues[i] = QueueHandle{ reinterpret_cast<uint64_t>(queue) };
                }
            }
            
            // Setup compute queues
            if (queueFamilyIndices.Compute != VK_QUEUE_FAMILY_IGNORED)
            {
                numComputeQueues = std::min(queueFamilyProperties[queueFamilyIndices.Compute].queueCount, k_MaxQueuesPerFamily);
                computeQueues.resize(numComputeQueues);
                
                for (uint32_t i = 0; i < numComputeQueues; ++i)
                {
                    VkQueue queue;
                    vkGetDeviceQueue(handle, queueFamilyIndices.Compute, i, &queue);
                    computeQueues[i] = QueueHandle{ reinterpret_cast<uint64_t>(queue) };
                }
            }
            
            // Setup transfer queues  
            if (queueFamilyIndices.Transfer != VK_QUEUE_FAMILY_IGNORED)
            {
                numTransferQueues = std::min(queueFamilyProperties[queueFamilyIndices.Transfer].queueCount, k_MaxQueuesPerFamily);
                transferQueues.resize(numTransferQueues);
                
                for (uint32_t i = 0; i < numTransferQueues; ++i)
                {
                    VkQueue queue;
                    vkGetDeviceQueue(handle, queueFamilyIndices.Transfer, i, &queue);
                    transferQueues[i] = QueueHandle{ reinterpret_cast<uint64_t>(queue) };
                }
            }
            
            // Setup sparse binding queues
            if (queueFamilyIndices.SparseBinding != VK_QUEUE_FAMILY_IGNORED)
            {
                numSparseBindingQueues = std::min(queueFamilyProperties[queueFamilyIndices.SparseBinding].queueCount, k_MaxQueuesPerFamily);
                sparseBindingQueues.resize(numSparseBindingQueues);
                
                for (uint32_t i = 0; i < numSparseBindingQueues; ++i)
                {
                    VkQueue queue;
                    vkGetDeviceQueue(handle, queueFamilyIndices.SparseBinding, i, &queue);
                    sparseBindingQueues[i] = QueueHandle{ reinterpret_cast<uint64_t>(queue) };
                }
            }

            std::string queue_info = std::format("Device queues setup - Graphics: {}, Compute: {}, Transfer: {}, Sparse: {}",
                                                numGraphicsQueues, 
                                                numComputeQueues, 
                                                numTransferQueues, 
                                                numSparseBindingQueues);

            std::cout << queue_info << "\n";
        }

        void setupDebugUtils(VkDevice handle)
        {
            if (!parentInstance->HasValidation() || !parentInstance->HasExtension("VK_EXT_debug_utils"))
            {
                return;
            }

            auto check_loaded_pfn = [](const void* ptr, const char* function_name)
            {
                if (ptr == nullptr)
                {
                    std::cerr << std::format("Warning: Debug Utils function {} not loaded.\n", function_name);
                }
            };

            debugUtilsHandler = {};
            debugUtilsHandler.vkSetDebugUtilsObjectName = 
                reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetDeviceProcAddr(handle, "vkSetDebugUtilsObjectNameEXT"));
            check_loaded_pfn((void*)debugUtilsHandler.vkSetDebugUtilsObjectName, "vkSetDebugUtilsObjectNameEXT");
            debugUtilsHandler.vkSetDebugUtilsObjectTag =
                reinterpret_cast<PFN_vkSetDebugUtilsObjectTagEXT>(vkGetDeviceProcAddr(handle, "vkSetDebugUtilsObjectTagEXT"));
            check_loaded_pfn((void*)debugUtilsHandler.vkSetDebugUtilsObjectTag, "vkSetDebugUtilsObjectTagEXT");
            debugUtilsHandler.vkQueueBeginDebugUtilsLabel = 
                reinterpret_cast<PFN_vkQueueBeginDebugUtilsLabelEXT>(vkGetDeviceProcAddr(handle, "vkQueueBeginDebugUtilsLabelEXT"));
            check_loaded_pfn((void*)debugUtilsHandler.vkQueueBeginDebugUtilsLabel, "vkQueueBeginDebugUtilsLabelEXT");
            debugUtilsHandler.vkQueueEndDebugUtilsLabel = 
                reinterpret_cast<PFN_vkQueueEndDebugUtilsLabelEXT>(vkGetDeviceProcAddr(handle, "vkQueueEndDebugUtilsLabelEXT"));
            check_loaded_pfn((void*)debugUtilsHandler.vkQueueEndDebugUtilsLabel, "vkQueueEndDebugUtilsLabelEXT");
            debugUtilsHandler.vkCmdBeginDebugUtilsLabel =
                reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetDeviceProcAddr(handle, "vkCmdBeginDebugUtilsLabelEXT"));
            check_loaded_pfn((void*)debugUtilsHandler.vkCmdBeginDebugUtilsLabel, "vkCmdBeginDebugUtilsLabelEXT");
            debugUtilsHandler.vkCmdEndDebugUtilsLabel =
                reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetDeviceProcAddr(handle, "vkCmdEndDebugUtilsLabelEXT"));
            check_loaded_pfn((void*)debugUtilsHandler.vkCmdEndDebugUtilsLabel, "vkCmdEndDebugUtilsLabelEXT");
            debugUtilsHandler.vkCmdInsertDebugUtilsLabel =
                reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(vkGetDeviceProcAddr(handle, "vkCmdInsertDebugUtilsLabelEXT"));
            check_loaded_pfn((void*)debugUtilsHandler.vkCmdInsertDebugUtilsLabel, "vkCmdInsertDebugUtilsLabelEXT");
            debugUtilsHandler.vkCreateDebugUtilsMessenger =
                reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(parentInstance->Handle().As<VkInstance>(), "vkCreateDebugUtilsMessengerEXT"));
            check_loaded_pfn((void*)debugUtilsHandler.vkCreateDebugUtilsMessenger, "vkCreateDebugUtilsMessengerEXT");
            debugUtilsHandler.vkDestroyDebugUtilsMessenger =
                reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(parentInstance->Handle().As<VkInstance>(), "vkDestroyDebugUtilsMessengerEXT"));
            check_loaded_pfn((void*)debugUtilsHandler.vkDestroyDebugUtilsMessenger, "vkDestroyDebugUtilsMessengerEXT");
            debugUtilsHandler.vkSubmitDebugUtilsMessage =
                reinterpret_cast<PFN_vkSubmitDebugUtilsMessageEXT>(vkGetInstanceProcAddr(parentInstance->Handle().As<VkInstance>(), "vkSubmitDebugUtilsMessageEXT"));
            check_loaded_pfn((void*)debugUtilsHandler.vkSubmitDebugUtilsMessage, "vkSubmitDebugUtilsMessageEXT");
        }

        void setupDeviceFaultHandler(VkDevice handle)
        {
            g_DeviceLossHandler = DeviceLostHandler;
            LossHandlerDevice = handle;
            pfn_vkGetDeviceFaultReportsKHR = reinterpret_cast<PFN_vkGetDeviceFaultReportsKHR>(vkGetDeviceProcAddr(handle, "vkGetDeviceFaultReportsKHR"));
            if (pfn_vkGetDeviceFaultReportsKHR == nullptr)
            {
                std::cerr << "Warning: Device fault reporting function vkGetDeviceFaultReportsKHR not available. Device loss events may not report detailed fault information.\n";
            }
        }

        const Instance* parentInstance;
        VkPhysicalDevice physicalDevice;

        std::vector<QueueHandle> graphicsQueues;
        std::vector<QueueHandle> computeQueues;
        std::vector<QueueHandle> transferQueues;
        std::vector<QueueHandle> sparseBindingQueues;

        QueueFamilyIndices queueFamilyIndices;
        uint32_t numGraphicsQueues;
        uint32_t numComputeQueues;
        uint32_t numTransferQueues;
        uint32_t numSparseBindingQueues;
        
        std::vector<std::string> enabledExtensions;
        VkDebugUtilsFunctions debugUtilsHandler;

        VkPhysicalDeviceProperties2 properties;
        VkPhysicalDeviceFeatures2 features;
        VkPhysicalDeviceMemoryProperties memoryProperties;
        std::vector<VkQueueFamilyProperties> queueFamilyProperties;
    };

    Device::Device(const Instance* instance,
                   ExtensionPack& extensions) :
        handle{ 0u },
        impl{ std::make_unique<DeviceImpl>(instance) }
    {
        createDevice(extensions);
    }

    Device::~Device()
    {
        if (handle.IsValid())
        {
            vkDestroyDevice(handle.As<VkDevice>(), nullptr);
        }
    }

    QueueHandle Device::GetGraphicsQueue(uint32_t index) const noexcept
    {
        return impl->graphicsQueues[index];
    }

    QueueHandle Device::GetComputeQueue(uint32_t index) const noexcept
    {
        return impl->computeQueues[index];
    }

    QueueHandle Device::GetTransferQueue(uint32_t index) const noexcept
    {
        return impl->transferQueues[index];
    }

    QueueHandle Device::GetSparseBindingQueue(uint32_t index) const noexcept
    {
        return impl->sparseBindingQueues[index];
    }

    QueueHandle Device::GetGeneralQueue() const noexcept
    {
        // Prefer graphics queue as it supports all operations
        if (!impl->graphicsQueues.empty())
        {
            return impl->graphicsQueues.front();
        }
        
        // Fallback to compute queue
        if (!impl->computeQueues.empty())
        {
            return impl->computeQueues.front();
        }

        return QueueHandle{ 0u };
    }

    bool Device::HasExtension(std::string_view extension_name) const noexcept
    {
        return std::find(impl->enabledExtensions.begin(), impl->enabledExtensions.end(), extension_name) != impl->enabledExtensions.end();
    }

    void Device::WaitDeviceIdle() const
    {
        vkDeviceWaitIdle(handle.As<VkDevice>());
    }

    const VkDebugUtilsFunctions& Device::GetDebugUtilFns() const noexcept
    {
        return impl->debugUtilsHandler;
    }

    uint32_t Device::GetMemoryTypeIndex(const uint32_t type_bitfield, const uint32_t property_flags) const
    {
        const VkMemoryPropertyFlags required_flags = static_cast<VkMemoryPropertyFlags>(property_flags);
        const VkPhysicalDeviceMemoryProperties& mem_props = impl->memoryProperties;
        const uint32_t num_memory_types = mem_props.memoryTypeCount;
        uint32_t bitfield = type_bitfield;

        for (uint32_t i = 0; i < num_memory_types; ++i)
        {
            if (bitfield & 1)
            {
                if ((mem_props.memoryTypes[i].propertyFlags & property_flags) == required_flags)
                {
                    return i;
                }
            }
            bitfield >>= 1;
        }
        
        return std::numeric_limits<uint32_t>::max();
    }

    DeviceHandle Device::Handle() const noexcept
    {
        return handle;
    }

    PhysicalDeviceHandle Device::GetPhysicalDevice() const noexcept
    {
        return PhysicalDeviceHandle{ reinterpret_cast<uint64_t>(impl->physicalDevice) };
    }

    uint32_t Device::GetGraphicsQueueCount() const noexcept
    {
        return impl->numGraphicsQueues;
    }

    uint32_t Device::GetComputeQueueCount() const noexcept
    {
        return impl->numComputeQueues;
    }

    uint32_t Device::GetTransferQueueCount() const noexcept
    {
        return impl->numTransferQueues;
    }

    uint32_t Device::GetSparseBindingQueueCount() const noexcept
    {
        return impl->numSparseBindingQueues;
    }

    const QueueFamilyIndices& Device::GetQueueFamilyIndices() const noexcept
    {
        return impl->queueFamilyIndices;
    }

    const std::vector<std::string>& Device::GetEnabledExtensions() const noexcept
    {
        return impl->enabledExtensions;
    }

    void Device::createDevice(ExtensionPack& extensions)
    {
        const QueueFamilyIndices& queue_indices = impl->queueFamilyIndices;
        extensions.SetPhysicalDevice(impl->physicalDevice);
        extensions.AddAllSupportedMaintenanceExtensions();
        extensions.ResolveDeviceDependencies();
        
        // Collect unique queue families
        std::set<uint32_t> unique_queue_families = 
        {
            queue_indices.Graphics,
            queue_indices.Compute,
            queue_indices.Transfer,
            queue_indices.SparseBinding
        };
        
        // Create queue create info for each unique family
        std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
        std::vector<std::vector<float>> queue_priorities_storage;

        const std::vector<VkQueueFamilyProperties>& queue_props = impl->queueFamilyProperties;

        for (uint32_t queue_family : unique_queue_families)
        {
            if (queue_family == VK_QUEUE_FAMILY_IGNORED)
            {
                continue;
            }
            
            const uint32_t queue_count = std::min(queue_props[queue_family].queueCount, k_MaxQueuesPerFamily);
            
            queue_priorities_storage.emplace_back(queue_count, 1.0f);
            
            VkDeviceQueueCreateInfo queue_create_info = 
            {
                VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                nullptr,
                0,
                queue_family,
                queue_count,
                queue_priorities_storage.back().data()
            };
            
            queue_create_infos.push_back(queue_create_info);
        }
        
        // Get device extensions
        const std::vector<const char*>& device_extensions = extensions.GetDeviceExtensions();
        
        // Store enabled extensions
        impl->enabledExtensions.reserve(device_extensions.size());
        for (const char* ext : device_extensions)
        {
            impl->enabledExtensions.emplace_back(ext);
        }
        
        // Device features
        const VkPhysicalDeviceFeatures2* device_features = extensions.GetDeviceFeatures();
        
        VkDeviceCreateInfo create_info = 
        {
            VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            device_features, // pNext chain for extended features
            0,
            static_cast<uint32_t>(queue_create_infos.size()),
            queue_create_infos.data(),
            0, // Layer count (deprecated at device level)
            nullptr, // Layer names (deprecated at device level)
            static_cast<uint32_t>(device_extensions.size()),
            device_extensions.data(),
            nullptr
        };
        
        const VkPhysicalDevice pDevice = impl->physicalDevice;
        VkDevice resultHandle = VK_NULL_HANDLE;
        rhi::Result result = vkCreateDevice(pDevice, &create_info, nullptr, &resultHandle);
        RhiAssert(result);
 
        handle.Set<VkDevice>(resultHandle);
        impl->setupQueues(handle.As<VkDevice>());
        impl->setupDebugUtils(handle.As<VkDevice>());
        if (HasExtension("VK_KHR_device_fault"))
        {
            impl->setupDeviceFaultHandler(handle.As<VkDevice>());
        }
        
    }

#endif // RHI_SYSTEM_USE_VULKAN

} // namespace rhi
