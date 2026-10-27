#include "Device.hpp"
#include "DebugUtilFns.hpp"
#include "Instance.hpp"
#include "ExtensionPack.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include "RhiAssert.hpp"
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
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
    static VkDevice k_LossHandlerDevice = VK_NULL_HANDLE;
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

        return result;
    }

    std::string VkDeviceFaultAddressInfoToStr(const VkDeviceFaultAddressInfoKHR& address_info)
    {
        if (address_info.addressPrecision == 0)
        {
            return std::format("Reported Address: 0x{:016X}, AddressType: {}",
                address_info.reportedAddress,
                DeviceFaultAddressTypeToStr(address_info.addressType));
        }

        const uint64_t mask = address_info.addressPrecision - 1;
        const uint64_t lowerAddress = address_info.reportedAddress & ~mask;
        const uint64_t upperAddress = lowerAddress + mask;
        return std::format("Address Range Begin: 0x{:016X}, Address Range End: 0x{:016X}, AddressType: {}",
            lowerAddress,
            upperAddress,
            DeviceFaultAddressTypeToStr(address_info.addressType));
    }

    void DeviceLostHandler(const Result& device_loss_result, const char* message)
    {

        std::cerr << std::format("Device Fault: {}\n", message);
        if (pfn_vkGetDeviceFaultReportsKHR)
        {
            uint32_t faultCount = 0;
            pfn_vkGetDeviceFaultReportsKHR(k_LossHandlerDevice, 0, &faultCount, nullptr);
            std::vector<VkDeviceFaultInfoKHR> faultInfos(faultCount, { VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_KHR, nullptr });
            pfn_vkGetDeviceFaultReportsKHR(k_LossHandlerDevice, 0, &faultCount, faultInfos.data());
            for (auto& faultInfo : faultInfos)
            {
                std::string faultFlagsStr = DeviceFaultFlagBitsToStr(static_cast<VkDeviceFaultFlagBitsKHR>(faultInfo.flags));
                uint64_t groupID = faultInfo.groupId;
                std::string_view faultDescription(faultInfo.description);
                std::string faultAddressInfoStr = VkDeviceFaultAddressInfoToStr(faultInfo.faultAddressInfo);
                std::string faultInstructionAddressInfoStr = VkDeviceFaultAddressInfoToStr(faultInfo.instructionAddressInfo);
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
                    std::cerr << std::format("FaultAddressInfo: {}\n", faultAddressInfoStr);
                }

                if (faultInfo.instructionAddressInfo.addressType != VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_KHR)
                {
                    std::cerr << std::format("InstructionAddressInfo: {}\n", faultInstructionAddressInfoStr);
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
        DeviceImpl(const Instance* instance, uint32_t api_version) :
            ParentInstance{ instance },
            ApiVersion{ api_version },
            PhysicalDevice{ VK_NULL_HANDLE },
            NumGraphicsQueues{ 0u },
            NumComputeQueues{ 0u },
            NumTransferQueues{ 0u },
            NumSparseBindingQueues{ 0u }
        {
            // perform this step first, so that we have the physical device handle for later steps and device creation
            SelectBestPhysicalDevice();
            RetrievePhysicalDeviceProperties();
            QueryQueueProperties();
        }

        void SelectBestPhysicalDevice()
        {
            uint32_t deviceCount = 0;
            vkEnumeratePhysicalDevices(ParentInstance->Handle().As<VkInstance>(), &deviceCount, nullptr);
            
            if (deviceCount == 0)
            {
                throw std::runtime_error("No Vulkan-compatible physical devices found");
            }
            
            std::vector<VkPhysicalDevice> devices(deviceCount);
            vkEnumeratePhysicalDevices(ParentInstance->Handle().As<VkInstance>(), &deviceCount, devices.data());
            
            // Simple scoring system - prefer discrete GPUs
            VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
            int bestScore = -1;
            
            for (VkPhysicalDevice device : devices)
            {
                VkPhysicalDeviceProperties deviceProps;
                vkGetPhysicalDeviceProperties(device, &deviceProps);

                int score = 0;
                
                // Prefer discrete GPUs
                if (deviceProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                {
                    score += 1000;
                }
                else if (deviceProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
                {
                    score += 100;
                }
                
                // Add points for more memory
                VkPhysicalDeviceMemoryProperties memProps;
                vkGetPhysicalDeviceMemoryProperties(device, &memProps);
                
                VkDeviceSize totalMemory = 0;
                for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
                {
                    if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                    {
                        totalMemory += memProps.memoryHeaps[i].size;
                    }
                }
                
                score += static_cast<int>(totalMemory / (1024 * 1024 * 1024)); // GB of memory
                
                if (score > bestScore)
                {
                    bestScore = score;
                    bestDevice = device;
                }
            }
            
            if (bestDevice == VK_NULL_HANDLE)
            {
                throw std::runtime_error("No suitable physical device found");
            }
            
            PhysicalDevice = bestDevice;
        }

        void RetrievePhysicalDeviceProperties()
        {
            Properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            Properties.pNext = nullptr;
            Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            Features.pNext = nullptr;

            // the *2 queries are core from 1.1. A 1.0 instance may only call them through the KHR extension,
            // which we don't require, so it uses the 1.0 queries and leaves both pNext chains empty
            if (ApiVersion >= VK_API_VERSION_1_1)
            {
                vkGetPhysicalDeviceProperties2(PhysicalDevice, &Properties);
                vkGetPhysicalDeviceFeatures2(PhysicalDevice, &Features);
            }
            else
            {
                vkGetPhysicalDeviceProperties(PhysicalDevice, &Properties.properties);
                vkGetPhysicalDeviceFeatures(PhysicalDevice, &Features.features);
            }
            vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemoryProperties);
        }

        void QueryQueueProperties()
        {
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &queueFamilyCount, nullptr);
            QueueFamilyProperties.resize(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &queueFamilyCount, QueueFamilyProperties.data());

            for (uint32_t i = 0; i < queueFamilyCount; ++i)
            {
                const VkQueueFamilyProperties& props = QueueFamilyProperties[i];

                // Graphics queue (also supports compute and transfer)
                if (props.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    if (QueueFamilyIndices.Graphics == ~0u)
                    {
                        QueueFamilyIndices.Graphics = i;
                        NumGraphicsQueues = props.queueCount;
                    }
                }

                // Dedicated compute queue (compute but not graphics)
                if ((props.queueFlags & VK_QUEUE_COMPUTE_BIT) && !(props.queueFlags & VK_QUEUE_GRAPHICS_BIT))
                {
                    if (QueueFamilyIndices.Compute == ~0u)
                    {
                        QueueFamilyIndices.Compute = i;
                        NumComputeQueues = props.queueCount;
                    }
                }

                // Dedicated transfer queue (transfer but not graphics or compute)
                if ((props.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
                    !(props.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                    !(props.queueFlags & VK_QUEUE_COMPUTE_BIT))
                {
                    if (QueueFamilyIndices.Transfer == ~0u)
                    {
                        QueueFamilyIndices.Transfer = i;
                        NumTransferQueues = props.queueCount;
                    }
                }

                // Sparse binding queue
                if (props.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT)
                {
                    if (QueueFamilyIndices.SparseBinding == ~0u)
                    {
                        QueueFamilyIndices.SparseBinding = i;
                        NumSparseBindingQueues = props.queueCount;
                    }
                }
            }
        }

        void SetupQueues(VkDevice handle)
        {
            
            // Setup graphics queues
            if (QueueFamilyIndices.Graphics != VK_QUEUE_FAMILY_IGNORED)
            {
                NumGraphicsQueues = std::min(QueueFamilyProperties[QueueFamilyIndices.Graphics].queueCount, k_MaxQueuesPerFamily);
                GraphicsQueues.resize(NumGraphicsQueues);
                
                for (uint32_t i = 0; i < NumGraphicsQueues; ++i)
                {
                    VkQueue queue;
                    vkGetDeviceQueue(handle, QueueFamilyIndices.Graphics, i, &queue);
                    GraphicsQueues[i] = QueueHandle{ reinterpret_cast<uint64_t>(queue) };
                }
            }
            
            // Setup compute queues
            if (QueueFamilyIndices.Compute != VK_QUEUE_FAMILY_IGNORED)
            {
                NumComputeQueues = std::min(QueueFamilyProperties[QueueFamilyIndices.Compute].queueCount, k_MaxQueuesPerFamily);
                ComputeQueues.resize(NumComputeQueues);
                
                for (uint32_t i = 0; i < NumComputeQueues; ++i)
                {
                    VkQueue queue;
                    vkGetDeviceQueue(handle, QueueFamilyIndices.Compute, i, &queue);
                    ComputeQueues[i] = QueueHandle{ reinterpret_cast<uint64_t>(queue) };
                }
            }
            
            // Setup transfer queues  
            if (QueueFamilyIndices.Transfer != VK_QUEUE_FAMILY_IGNORED)
            {
                NumTransferQueues = std::min(QueueFamilyProperties[QueueFamilyIndices.Transfer].queueCount, k_MaxQueuesPerFamily);
                TransferQueues.resize(NumTransferQueues);
                
                for (uint32_t i = 0; i < NumTransferQueues; ++i)
                {
                    VkQueue queue;
                    vkGetDeviceQueue(handle, QueueFamilyIndices.Transfer, i, &queue);
                    TransferQueues[i] = QueueHandle{ reinterpret_cast<uint64_t>(queue) };
                }
            }
            
            // Setup sparse binding queues
            if (QueueFamilyIndices.SparseBinding != VK_QUEUE_FAMILY_IGNORED)
            {
                NumSparseBindingQueues = std::min(QueueFamilyProperties[QueueFamilyIndices.SparseBinding].queueCount, k_MaxQueuesPerFamily);
                SparseBindingQueues.resize(NumSparseBindingQueues);
                
                for (uint32_t i = 0; i < NumSparseBindingQueues; ++i)
                {
                    VkQueue queue;
                    vkGetDeviceQueue(handle, QueueFamilyIndices.SparseBinding, i, &queue);
                    SparseBindingQueues[i] = QueueHandle{ reinterpret_cast<uint64_t>(queue) };
                }
            }

            std::string queueInfo = std::format("Device queues setup - Graphics: {}, Compute: {}, Transfer: {}, Sparse: {}",
                                                NumGraphicsQueues, 
                                                NumComputeQueues, 
                                                NumTransferQueues, 
                                                NumSparseBindingQueues);

            std::cout << queueInfo << "\n";
        }

        void SetupDebugUtils(VkDevice handle)
        {
            // the extension alone is enough: Nsight and RenderDoc implement the labels themselves, and profiling runs
            // are the ones without validation
            if (!ParentInstance->HasExtension("VK_EXT_debug_utils"))
            {
                return;
            }

            auto checkLoadedPfn = [](const void* ptr, const char* function_name)
            {
                if (ptr == nullptr)
                {
                    std::cerr << std::format("Warning: Debug Utils function {} not loaded.\n", function_name);
                }
            };

            DebugUtilsHandler = {};
            DebugUtilsHandler.vkSetDebugUtilsObjectName = 
                reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetDeviceProcAddr(handle, "vkSetDebugUtilsObjectNameEXT"));
            checkLoadedPfn(reinterpret_cast<void*>(DebugUtilsHandler.vkSetDebugUtilsObjectName), "vkSetDebugUtilsObjectNameEXT");
            DebugUtilsHandler.vkSetDebugUtilsObjectTag =
                reinterpret_cast<PFN_vkSetDebugUtilsObjectTagEXT>(vkGetDeviceProcAddr(handle, "vkSetDebugUtilsObjectTagEXT"));
            checkLoadedPfn(reinterpret_cast<void*>(DebugUtilsHandler.vkSetDebugUtilsObjectTag), "vkSetDebugUtilsObjectTagEXT");
            DebugUtilsHandler.vkQueueBeginDebugUtilsLabel = 
                reinterpret_cast<PFN_vkQueueBeginDebugUtilsLabelEXT>(vkGetDeviceProcAddr(handle, "vkQueueBeginDebugUtilsLabelEXT"));
            checkLoadedPfn(reinterpret_cast<void*>(DebugUtilsHandler.vkQueueBeginDebugUtilsLabel), "vkQueueBeginDebugUtilsLabelEXT");
            DebugUtilsHandler.vkQueueEndDebugUtilsLabel = 
                reinterpret_cast<PFN_vkQueueEndDebugUtilsLabelEXT>(vkGetDeviceProcAddr(handle, "vkQueueEndDebugUtilsLabelEXT"));
            checkLoadedPfn(reinterpret_cast<void*>(DebugUtilsHandler.vkQueueEndDebugUtilsLabel), "vkQueueEndDebugUtilsLabelEXT");
            DebugUtilsHandler.vkCmdBeginDebugUtilsLabel =
                reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetDeviceProcAddr(handle, "vkCmdBeginDebugUtilsLabelEXT"));
            checkLoadedPfn(reinterpret_cast<void*>(DebugUtilsHandler.vkCmdBeginDebugUtilsLabel), "vkCmdBeginDebugUtilsLabelEXT");
            DebugUtilsHandler.vkCmdEndDebugUtilsLabel =
                reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetDeviceProcAddr(handle, "vkCmdEndDebugUtilsLabelEXT"));
            checkLoadedPfn(reinterpret_cast<void*>(DebugUtilsHandler.vkCmdEndDebugUtilsLabel), "vkCmdEndDebugUtilsLabelEXT");
            DebugUtilsHandler.vkCmdInsertDebugUtilsLabel =
                reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(vkGetDeviceProcAddr(handle, "vkCmdInsertDebugUtilsLabelEXT"));
            checkLoadedPfn(reinterpret_cast<void*>(DebugUtilsHandler.vkCmdInsertDebugUtilsLabel), "vkCmdInsertDebugUtilsLabelEXT");
            DebugUtilsHandler.vkCreateDebugUtilsMessenger =
                reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(ParentInstance->Handle().As<VkInstance>(), "vkCreateDebugUtilsMessengerEXT"));
            checkLoadedPfn(reinterpret_cast<void*>(DebugUtilsHandler.vkCreateDebugUtilsMessenger), "vkCreateDebugUtilsMessengerEXT");
            DebugUtilsHandler.vkDestroyDebugUtilsMessenger =
                reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(ParentInstance->Handle().As<VkInstance>(), "vkDestroyDebugUtilsMessengerEXT"));
            checkLoadedPfn(reinterpret_cast<void*>(DebugUtilsHandler.vkDestroyDebugUtilsMessenger), "vkDestroyDebugUtilsMessengerEXT");
            DebugUtilsHandler.vkSubmitDebugUtilsMessage =
                reinterpret_cast<PFN_vkSubmitDebugUtilsMessageEXT>(vkGetInstanceProcAddr(ParentInstance->Handle().As<VkInstance>(), "vkSubmitDebugUtilsMessageEXT"));
            checkLoadedPfn(reinterpret_cast<void*>(DebugUtilsHandler.vkSubmitDebugUtilsMessage), "vkSubmitDebugUtilsMessageEXT");
        }

        void SetupDeviceFaultHandler(VkDevice handle)
        {
            g_DeviceLossHandler = DeviceLostHandler;
            k_LossHandlerDevice = handle;
            pfn_vkGetDeviceFaultReportsKHR = reinterpret_cast<PFN_vkGetDeviceFaultReportsKHR>(vkGetDeviceProcAddr(handle, "vkGetDeviceFaultReportsKHR"));
            if (pfn_vkGetDeviceFaultReportsKHR == nullptr)
            {
                std::cerr << "Warning: Device fault reporting function vkGetDeviceFaultReportsKHR not available. Device loss events may not report detailed fault information.\n";
            }
        }

        const Instance* ParentInstance;
        uint32_t ApiVersion;
        VkPhysicalDevice PhysicalDevice;

        std::vector<QueueHandle> GraphicsQueues;
        std::vector<QueueHandle> ComputeQueues;
        std::vector<QueueHandle> TransferQueues;
        std::vector<QueueHandle> SparseBindingQueues;

        QueueFamilyIndices QueueFamilyIndices;
        uint32_t NumGraphicsQueues;
        uint32_t NumComputeQueues;
        uint32_t NumTransferQueues;
        uint32_t NumSparseBindingQueues;
        
        std::vector<std::string> EnabledExtensions;
        std::vector<std::string> CoreExtensions;
        VkDebugUtilsFunctions DebugUtilsHandler;

        VkPhysicalDeviceProperties2 Properties;
        VkPhysicalDeviceFeatures2 Features;
        VkPhysicalDeviceMemoryProperties MemoryProperties;
        std::vector<VkQueueFamilyProperties> QueueFamilyProperties;
    };

    Device::Device(const Instance* instance,
                   ExtensionPack& extensions) :
        impl{ std::make_unique<DeviceImpl>(instance, extensions.GetVulkanApiVersion()) },
        handle{ 0u }
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
        return impl->GraphicsQueues[index];
    }

    QueueHandle Device::GetComputeQueue(uint32_t index) const noexcept
    {
        return impl->ComputeQueues[index];
    }

    QueueHandle Device::GetTransferQueue(uint32_t index) const noexcept
    {
        return impl->TransferQueues[index];
    }

    QueueHandle Device::GetSparseBindingQueue(uint32_t index) const noexcept
    {
        return impl->SparseBindingQueues[index];
    }

    QueueHandle Device::GetGeneralQueue() const noexcept
    {
        // Prefer graphics queue as it supports all operations
        if (!impl->GraphicsQueues.empty())
        {
            return impl->GraphicsQueues.front();
        }
        
        // Fallback to compute queue
        if (!impl->ComputeQueues.empty())
        {
            return impl->ComputeQueues.front();
        }

        return QueueHandle{ 0u };
    }

    bool Device::HasExtension(std::string_view extension_name) const noexcept
    {
        // a requested extension that is core in the active version is available without being enabled by name
        return std::find(impl->EnabledExtensions.begin(), impl->EnabledExtensions.end(), extension_name) != impl->EnabledExtensions.end() ||
               std::find(impl->CoreExtensions.begin(), impl->CoreExtensions.end(), extension_name) != impl->CoreExtensions.end();
    }

    void Device::WaitDeviceIdle() const
    {
        vkDeviceWaitIdle(handle.As<VkDevice>());
    }

    const VkDebugUtilsFunctions& Device::GetDebugUtilFns() const noexcept
    {
        return impl->DebugUtilsHandler;
    }

    uint32_t Device::GetMemoryTypeIndex(const uint32_t type_bitfield, const uint32_t property_flags) const
    {
        const VkMemoryPropertyFlags requiredFlags = static_cast<VkMemoryPropertyFlags>(property_flags);
        const VkPhysicalDeviceMemoryProperties& memProps = impl->MemoryProperties;
        const uint32_t numMemoryTypes = memProps.memoryTypeCount;
        uint32_t bitfield = type_bitfield;

        for (uint32_t i = 0; i < numMemoryTypes; ++i)
        {
            if (bitfield & 1)
            {
                if ((memProps.memoryTypes[i].propertyFlags & property_flags) == requiredFlags)
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
        return PhysicalDeviceHandle{ reinterpret_cast<uint64_t>(impl->PhysicalDevice) };
    }

    uint32_t Device::GetGraphicsQueueCount() const noexcept
    {
        return impl->NumGraphicsQueues;
    }

    uint32_t Device::GetComputeQueueCount() const noexcept
    {
        return impl->NumComputeQueues;
    }

    uint32_t Device::GetTransferQueueCount() const noexcept
    {
        return impl->NumTransferQueues;
    }

    uint32_t Device::GetSparseBindingQueueCount() const noexcept
    {
        return impl->NumSparseBindingQueues;
    }

    const QueueFamilyIndices& Device::GetQueueFamilyIndices() const noexcept
    {
        return impl->QueueFamilyIndices;
    }

    const std::vector<std::string>& Device::GetEnabledExtensions() const noexcept
    {
        return impl->EnabledExtensions;
    }

    void Device::createDevice(ExtensionPack& extensions)
    {
        const QueueFamilyIndices& queueIndices = impl->QueueFamilyIndices;
        extensions.SetPhysicalDevice(impl->PhysicalDevice);
        extensions.AddAllSupportedMaintenanceExtensions();
        extensions.ResolveDeviceDependencies();
        
        // Collect unique queue families
        std::set<uint32_t> uniqueQueueFamilies = 
        {
            queueIndices.Graphics,
            queueIndices.Compute,
            queueIndices.Transfer,
            queueIndices.SparseBinding
        };
        
        // Create queue create info for each unique family
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::vector<std::vector<float>> queuePrioritiesStorage;

        const std::vector<VkQueueFamilyProperties>& queueProps = impl->QueueFamilyProperties;

        for (uint32_t queueFamily : uniqueQueueFamilies)
        {
            if (queueFamily == VK_QUEUE_FAMILY_IGNORED)
            {
                continue;
            }
            
            const uint32_t queueCount = std::min(queueProps[queueFamily].queueCount, k_MaxQueuesPerFamily);
            
            queuePrioritiesStorage.emplace_back(queueCount, 1.0f);
            
            VkDeviceQueueCreateInfo queueCreateInfo = 
            {
                VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                nullptr,
                0,
                queueFamily,
                queueCount,
                queuePrioritiesStorage.back().data()
            };
            
            queueCreateInfos.push_back(queueCreateInfo);
        }
        
        // Get device extensions
        const std::vector<const char*>& deviceExtensions = extensions.GetDeviceExtensions();
        
        // Store enabled extensions
        impl->EnabledExtensions.reserve(deviceExtensions.size());
        for (const char* ext : deviceExtensions)
        {
            impl->EnabledExtensions.emplace_back(ext);
        }
        impl->CoreExtensions = extensions.GetCoreDeviceExtensions();
        
        // Device features
        const VkPhysicalDeviceFeatures2* deviceFeatures = extensions.GetDeviceFeatures();
        
        VkDeviceCreateInfo createInfo = 
        {
            VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            deviceFeatures, // pNext chain for extended features
            0,
            static_cast<uint32_t>(queueCreateInfos.size()),
            queueCreateInfos.data(),
            0, // Layer count (deprecated at device level)
            nullptr, // Layer names (deprecated at device level)
            static_cast<uint32_t>(deviceExtensions.size()),
            deviceExtensions.data(),
            nullptr
        };

        // a 1.0 device can't take VkPhysicalDeviceFeatures2 in pNext, so it gets the 1.0 features directly
        if (impl->ApiVersion < VK_API_VERSION_1_1)
        {
            createInfo.pNext = nullptr;
            createInfo.pEnabledFeatures = &deviceFeatures->features;
        }
        
        const VkPhysicalDevice pDevice = impl->PhysicalDevice;
        VkDevice resultHandle = VK_NULL_HANDLE;
        rhi::Result result = vkCreateDevice(pDevice, &createInfo, nullptr, &resultHandle);
        RhiAssert(result);
 
        handle.Set<VkDevice>(resultHandle);
        impl->SetupQueues(handle.As<VkDevice>());
        impl->SetupDebugUtils(handle.As<VkDevice>());
        if (HasExtension("VK_KHR_device_fault"))
        {
            impl->SetupDeviceFaultHandler(handle.As<VkDevice>());
        }
        
    }

#endif // RHI_SYSTEM_USE_VULKAN

} // namespace rhi
