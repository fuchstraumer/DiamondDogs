#include "CommandBuffer.hpp"
#include <cassert>
#include <iostream>

#ifdef RHI_SYSTEM_USE_VULKAN
    #include <vulkan/vulkan_core.h>
#elif defined(RHI_SYSTEM_USE_DX12)
    #include <d3d12.h>
    #include <wrl/client.h>
#endif

namespace rhi
{
    CommandBuffer::CommandBuffer(CommandBufferHandle handle) noexcept : handle{ handle }
    {
    }

    CommandBuffer::~CommandBuffer()
    {
#ifdef DIAMOND_DOGS_GRAPHICS_DEBUG
        // In graphics debug builds, assert if still recording
        assert(!isRecording && "CommandBuffer destroyed while still in recording mode. Ensure End() is called before destruction.");
#else
        // In all other builds, just end recording if still in recording mode. Better to clean up gracefully than crash.
        if (isRecording)
        {
            std::cerr << "Warning: CommandBuffer destroyed while still in recording mode. Automatically ending recording.\n";
            End();
        }
#endif
    }

    CommandBuffer::CommandBuffer(CommandBuffer&& other) noexcept
        : isRecording{ other.isRecording }, handle{ std::move(other.handle) }
    {
        other.isRecording = false;
        other.handle = CommandBufferHandle{ 0 };
    }

    CommandBuffer& CommandBuffer::operator=(CommandBuffer&& other) noexcept
    {
        if (this != &other)
        {
            isRecording = other.isRecording;
            handle = std::move(other.handle);
            other.isRecording = false;
            other.handle = CommandBufferHandle{ 0 };
        }
        return *this;
    }

    CommandBufferHandle CommandBuffer::Handle() const noexcept
    {
        return handle;
    }

    Result CommandBuffer::Begin() noexcept
    {
        isRecording = true;
#ifdef RHI_SYSTEM_USE_VULKAN
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = nullptr;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginInfo.pInheritanceInfo = nullptr; // Only relevant for secondary command buffers
        VkResult result = vkBeginCommandBuffer(handle.As<VkCommandBuffer>(), &beginInfo);
        return Result(result);
#elif defined(RHI_SYSTEM_USE_DX12)
        // In DX12, command lists are implicitly open for recording after being created and passed to the ctor of CommandBuffer
        return Result::Success();
#endif
    }

    Result CommandBuffer::End() noexcept
    {
        assert(isRecording && "CommandBuffer::End() called while not in recording mode.");
        isRecording = false;
#ifdef RHI_SYSTEM_USE_VULKAN
        VkResult result = vkEndCommandBuffer(handle.As<VkCommandBuffer>());
        return Result(result);
#elif defined(RHI_SYSTEM_USE_DX12)
        ID3D12GraphicsCommandList* cmdList = handle.As<ID3D12GraphicsCommandList*>();
        HRESULT hr = cmdList->Close();
        if (SUCCEEDED(hr))
        {
            return Result::Success();
        }
        else
        {
            return Result(hr);
        }
#endif
    }

}
