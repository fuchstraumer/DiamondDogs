#pragma once
#ifndef DIAMOND_DOGS_RHI_COMMAND_POOL_HPP
#define DIAMOND_DOGS_RHI_COMMAND_POOL_HPP
#include "utility/EnumFlagUtils.hpp"
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include <memory>

namespace rhi
{
    struct CommandPoolImpl;

    namespace detail
    {
        struct CommandPoolTag {};
    }

    using CommandPoolHandle = RhiHandle<detail::CommandPoolTag>;

    class CommandPool
    {
    public:
        enum class Type
        {
            Graphics,
            Compute,
            Transfer,  // Maps to D3D12_COMMAND_LIST_TYPE_COPY
        };

        /** @brief Creates a command pool for allocating command buffers from.
         *  @param device The device to create the pool on
         *  @param poolType The type of command buffers this pool will allocate
         *  @param queueFamilyIndex The queue family index to associate with this pool. Ignored on DX12
         */
        CommandPool(DeviceHandle device, Type poolType, uint32_t queueFamilyIndex = 0);
        ~CommandPool();
        
        CommandPool(const CommandPool&) = delete;
        CommandPool& operator=(const CommandPool&) = delete;
        CommandPool(CommandPool&& other) noexcept;
        CommandPool& operator=(CommandPool&& other) noexcept;

        CommandPoolHandle Handle() const noexcept;
        
        Type GetType() const noexcept;

        /** @brief Allocates command buffers from the pool. */
        Result AllocateCommandBuffers(uint32_t count) noexcept;
        /** @brief Retrieves a command buffer from the pool. If you attempt to retrieve beyond the end of the pool, an invalid handle is returned.
         *  @note Wrap these in CommandBuffer objects to manage their lifetimes and recording state, as the raw handles do not manage that for you.
         */
        CommandBufferHandle GetCommandBuffer(uint32_t idx) const noexcept;
        /** @brief Resets the whole command pool, returning all command buffers contained to their initial state for recording into again. */
        Result Reset() noexcept;

    private:
        DeviceHandle device;
        CommandPoolHandle handle;
        Type poolType;
        std::unique_ptr<CommandPoolImpl> impl;
    };

}

#endif // !DIAMOND_DOGS_RHI_COMMAND_POOL_HPP
