#include "CommandPool.hpp"
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>

#ifdef RHI_SYSTEM_USE_VULKAN
    #include <vulkan/vulkan_core.h>
    #include <vector>
#elif defined(RHI_SYSTEM_USE_DX12)
    #include <d3d12.h>
    #include <wrl/client.h>
#endif

namespace rhi
{
    struct CommandPoolImpl
    {
        DeviceHandle Device;
        CommandPoolImpl(const DeviceHandle& _device, uint32_t _queueFamilyIndex) noexcept : Device{ _device }, QueueFamilyIndex{ _queueFamilyIndex }
        {
            GetTrimFunction();
        }
#ifdef RHI_SYSTEM_USE_VULKAN
        uint32_t QueueFamilyIndex;
        std::vector<VkCommandBuffer> AllocatedBuffers; 
        PFN_vkTrimCommandPoolKHR VkTrimCommandPool = nullptr;
#elifdef RHI_SYSTEM_USE_DX12
        D3D12_COMMAND_LIST_TYPE commandListType;
        std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>> commandLists;
        std::vector<bool> commandListInUse;
        uint32_t nextAvailableIndex = 0;
#endif
        void GetTrimFunction()
        {
#ifdef RHI_SYSTEM_USE_VULKAN
            if (!VkTrimCommandPool)
            {
                VkTrimCommandPool = reinterpret_cast<PFN_vkTrimCommandPoolKHR>(vkGetDeviceProcAddr(Device.As<VkDevice>(), "vkTrimCommandPoolKHR"));
            }
#endif
        }

        Result AllocateCommandBuffers(const CommandPoolHandle& handle, uint32_t count)
        {
#ifdef RHI_SYSTEM_USE_VULKAN
            // For simplicity, this implementation assumes all command buffers are freed together with the pool.
            // More complex tracking would be needed for individual frees.
            assert(AllocatedBuffers.empty()); 
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = handle.As<VkCommandPool>();
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = count;
            
            std::vector<VkCommandBuffer> newBuffers(count);
            VkResult result = vkAllocateCommandBuffers(Device.As<VkDevice>(), &allocInfo, newBuffers.data());
            
            if (result == VK_SUCCESS)
            {
                AllocatedBuffers.insert(AllocatedBuffers.end(), newBuffers.begin(), newBuffers.end());
            }
            
            return Result(result);
#elifdef RHI_SYSTEM_USE_DX12
            // Create the requested number of command lists
            for (uint32_t i = 0; i < count; ++i)
            {
                Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
                HRESULT hr = device.As<ID3D12Device>()->CreateCommandList(
                    0,                              // Node mask
                    impl->commandListType,          // Type
                    handle.As<ID3D12CommandAllocator>(), // Allocator
                    nullptr,                        // Initial pipeline state
                    IID_PPV_ARGS(&commandList)
                );
                
                if (FAILED(hr))
                {
                    return Result(hr);
                }
                
                // Close the command list since DX12 creates them in recording state
                // We want them to start closed like Vulkan command buffers
                hr = commandList->Close();
                if (FAILED(hr))
                {
                    return Result(hr);
                }
                
                impl->commandLists.push_back(commandList);
                impl->commandListInUse.push_back(false);  // Start as available
            }
            
            return Result::Success();
#endif
        }

    };

    CommandPool::CommandPool(const DeviceHandle& device, Type pool_type, uint32_t queue_family_index)
        : device{ device }, poolType{ pool_type }, impl{ std::make_unique<CommandPoolImpl>(device, queue_family_index) }
    {
#ifdef RHI_SYSTEM_USE_VULKAN
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queue_family_index;
        // Only use transient bit, we reset the pool each frame and don't need individual buffer resets
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        
        VkCommandPool vkPool = VK_NULL_HANDLE;
        VkResult result = vkCreateCommandPool(device.As<VkDevice>(), &poolInfo, nullptr, &vkPool);
        if (result == VK_SUCCESS)
        {
            handle = CommandPoolHandle{ reinterpret_cast<uint64_t>(vkPool) };
        }
        
#elif defined(RHI_SYSTEM_USE_DX12)
        switch (poolType)
        {
            case Type::Graphics: impl->commandListType = D3D12_COMMAND_LIST_TYPE_DIRECT; break;
            case Type::Compute:  impl->commandListType = D3D12_COMMAND_LIST_TYPE_COMPUTE; break;
            case Type::Transfer: impl->commandListType = D3D12_COMMAND_LIST_TYPE_COPY; break;
            case Type::Bundle:   impl->commandListType = D3D12_COMMAND_LIST_TYPE_BUNDLE; break;
        }
        
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        HRESULT hr = device.As<ID3D12Device>()->CreateCommandAllocator(
            impl->commandListType, IID_PPV_ARGS(&allocator)
        );
        
        if (SUCCEEDED(hr))
        {
            handle = CommandPoolHandle{ reinterpret_cast<uint64_t>(allocator.Get()) };
        }
#endif
    }

    CommandPool::~CommandPool()
    {
#ifdef RHI_SYSTEM_USE_VULKAN
        if (handle.IsValid())
        {
            // Free all allocated command buffers
            if (!impl->AllocatedBuffers.empty())
            {
                vkFreeCommandBuffers(device.As<VkDevice>(), handle.As<VkCommandPool>(), 
                    static_cast<uint32_t>(impl->AllocatedBuffers.size()), impl->AllocatedBuffers.data());
            }
            vkDestroyCommandPool(device.As<VkDevice>(), handle.As<VkCommandPool>(), nullptr);
        }
#elif defined(RHI_SYSTEM_USE_DX12)
        // Close any open command lists before destruction
        if (impl)
        {
            for (size_t i = 0; i < impl->commandLists.size(); ++i)
            {
                if (impl->commandListInUse[i] && impl->commandLists[i])
                {
                    impl->commandLists[i]->Close();  // Ensure command list is closed
                }
            }
        }
        // COM objects handle their own cleanup
#endif
    }

    CommandPool::CommandPool(CommandPool&& other) noexcept
        : device{ other.device }, 
          handle{ std::move(other.handle) },
          poolType{ other.poolType },
          impl{ std::move(other.impl) }
    {
        other.handle = CommandPoolHandle{};
    }

    CommandPool& CommandPool::operator=(CommandPool&& other) noexcept
    {
        if (this != &other)
        {
            device = other.device;
            handle = std::move(other.handle);
            poolType = other.poolType;
            impl = std::move(other.impl);
            
            other.handle = CommandPoolHandle{};
        }
        return *this;
    }

    CommandPoolHandle CommandPool::Handle() const noexcept
    {
        return handle;
    }

    CommandPool::Type CommandPool::GetType() const noexcept
    {
        return poolType;
    }

    CommandBufferHandle CommandPool::GetCommandBuffer(uint32_t idx) const noexcept
    {
#ifdef RHI_SYSTEM_USE_VULKAN
        if (idx < impl->AllocatedBuffers.size())
        {
            return CommandBufferHandle{ reinterpret_cast<uint64_t>(impl->AllocatedBuffers[idx]) };
        }
        return CommandBufferHandle{};
        
#elifdef RHI_SYSTEM_USE_DX12
        // For DX12, return the next available command list
        // Find first unused command list starting from nextAvailableIndex
        for (uint32_t i = 0; i < impl->commandLists.size(); ++i)
        {
            uint32_t checkIndex = (impl->nextAvailableIndex + i) % impl->commandLists.size();
            if (!impl->commandListInUse[checkIndex])
            {
                // Mark as in use and advance next available index
                impl->commandListInUse[checkIndex] = true;
                impl->nextAvailableIndex = (checkIndex + 1) % impl->commandLists.size();
                // under DX12, this handle is actually storing the pointer to the command list
                return CommandBufferHandle{ reinterpret_cast<uint64_t>(impl->commandLists[checkIndex].Get()) };
            }
        }
        
        // No available command lists - could expand pool or return invalid handle
        return CommandBufferHandle{};
#endif
    }

    Result CommandPool::Reset() noexcept
    {
#ifdef RHI_SYSTEM_USE_VULKAN
        // We don't reuse individual command buffers, and don't need the resources at this point.
        // Also means more parity with dx12 behavior.
        VkCommandPoolResetFlags resetFlags = VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT;
        VkResult result = vkResetCommandPool(device.As<VkDevice>(), handle.As<VkCommandPool>(), resetFlags);
        return Result(result);
#elifdef RHI_SYSTEM_USE_DX12

        HRESULT hr = handle.As<ID3D12CommandAllocator>()->Reset();
        
        if (SUCCEEDED(hr))
        {
            // Reset all command lists and mark as available
            for (size_t i = 0; i < impl->commandLists.size(); ++i)
            {
                if (impl->commandLists[i])
                {
                    HRESULT resetHr = impl->commandLists[i]->Reset(handle.As<ID3D12CommandAllocator>(), nullptr);
                    if (FAILED(resetHr))
                    {
                        hr = resetHr;  // Propagate any reset failures
                    }
                }
                impl->commandListInUse[i] = false;  // Mark as available
            }
            impl->nextAvailableIndex = 0;  // Reset allocation index
        }
        
        return Result(hr);
#endif
    }

    // Helper method to allocate command buffers (similar to Vulkan's vkAllocateCommandBuffers)
    Result CommandPool::AllocateCommandBuffers(uint32_t count) noexcept
    {
        return impl->AllocateCommandBuffers(handle, count);
    }

}
